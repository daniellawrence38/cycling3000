/* The core: ride state machine (brief 5.5), timers and distance (5.2), auto-pause, hold detection (6.4),
 * the per-second pipeline and the view snapshot. */
#include "cc700/core.h"
#include "cc700/calib.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"
#include "cc700/zones.h"

typedef struct {
    hal_fs_t *fs;
    cc_settings_t set;
    cc_slots_t slots;
    /* sensors */
    cc_sensors_t sens;
    cc_sens_state_t fw_state[3];
    int power_alerted;
    /* GNSS */
    hal_gnss_fix_t fix;
    int fix_fresh;                /* a fix arrived since the last second boundary */
    uint32_t spd[3];
    uint8_t spd_ok[3];
    int spd_head;
    uint32_t no_fix_s;
    int gps_lost, gps_alerted;
    /* altitude */
    cc_alt_t alt;
    /* workout and route */
    cc_workout_t wo;
    int wo_loaded;
    cc_wo_engine_t eng;
    int nextstep_active;
    int rt_loaded;
    cc_nav_t nav;
    /* ride */
    cc_ride_state_t state;
    int pause_auto;
    int focus;
    uint32_t timer_s, elapsed_s, paused_s, lap_s;
    uint32_t dist_cm, lap_start_cm;
    uint64_t psum;
    uint32_t pn;
    int ap_below, ap_above;
    uint32_t ride_id;
    int provisional;
    int offset_known;
    int32_t offset;
    cc_fit_t fit;
    int storage_failing, storage_alerted;
    uint32_t storage_since_ms;
    int lowbatt15, lowbatt5;
    cc_alerts_t al;
    cc_ride_meta_t summary;
    uint32_t saved_seq;
    int power_off;
    /* buttons */
    struct { int down; uint32_t down_ms; int fired; } btn[CC_BTN_N];
    cc_gesture_t gq[8];
    int gq_head, gq_n;
    /* time */
    int utc_known, utc_from_gnss;
    uint32_t utc_val, utc_at_ms;
    uint32_t last_sec;
    int phone;
    int battery, charging;
    cc_view_t v;
} core_t;

static core_t S;
static uint32_t hal_now_ms_hint; /* the latest now_ms the core has seen */

/* ------------------------------------------------------------------ helpers */
static uint32_t utc_now(uint32_t now_ms) { return S.utc_val + (now_ms - S.utc_at_ms) / 1000u; }

static uint32_t ts_now(uint32_t now_ms)
{
    return S.provisional ? CC_FIT_PROVISIONAL_BASE + now_ms / 1000u : utc_now(now_ms);
}

static void save_settings(void) { cc_settings_save(S.fs, &S.set, &S.slots); }

static void push_gesture(cc_btn_t b, int hold)
{
    if (S.gq_n == (int)(sizeof S.gq / sizeof S.gq[0]))
        return;
    S.gq[(S.gq_head + S.gq_n) % 8] = (cc_gesture_t){b, hold};
    S.gq_n++;
}

int cc_poll_gesture(cc_gesture_t *g)
{
    if (!S.gq_n)
        return 0;
    *g = S.gq[S.gq_head];
    S.gq_head = (S.gq_head + 1) % 8;
    S.gq_n--;
    return 1;
}

static int p3(int32_t *v) { return cc_chan_avg(&S.sens.power, 3, v); }

static int speed3(uint32_t *mms)
{
    uint64_t sum = 0;
    int n = 0;
    for (int i = 0; i < 3; i++)
        if (S.spd_ok[i]) { sum += S.spd[i]; n++; }
    if (!n)
        return 0;
    *mms = (uint32_t)(sum / (uint64_t)n);
    return 1;
}

static void flush(uint32_t now_ms)
{
    if (cc_fit_flush(&S.fit) == 0) {
        S.storage_failing = 0;
        if (S.storage_alerted) {
            cc_alerts_clear(&S.al, CC_AL_STORAGE, now_ms);
            S.storage_alerted = 0;
        }
        return;
    }
    if (!S.storage_failing) {
        S.storage_failing = 1;
        S.storage_since_ms = now_ms;
    }
}

static void storage_check(uint32_t now_ms)
{
    if (S.storage_failing && !S.storage_alerted && now_ms - S.storage_since_ms >= 30000u) {
        cc_banner_t b;
        cc_banner_storage(&b);
        cc_alerts_raise(&S.al, &b, now_ms);
        S.storage_alerted = 1;
    }
}

/* ------------------------------------------------------------------ objects */
/* The decoded route (204 KB) and the object read buffer (128 KB) are far too large for the
   ESP32-S3's 512 KB of internal SRAM once LVGL, NimBLE and the task stacks are in it: a build with
   them in .bss overflows dram0_0_seg by 518 KB. The brief puts the decoded route in PSRAM (brief
   5.7), and ESP-IDF's heap does exactly that for any allocation larger than
   CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 KB), so one calloc each is all it takes and the host
   build is unchanged. Allocated on first use and never freed, so no ride can fail part way through
   on a short heap; if the allocation fails the object simply does not load, which is an error path
   the UI already has. That also keeps Q30's safety net intact: PSRAM is configured octal on an
   Unverified guess with CONFIG_SPIRAM_IGNORE_NOTFOUND=y, so if the guess is wrong the unit boots
   with no PSRAM, and here that costs routes and workouts rather than the whole firmware, which is
   what placing .bss in external RAM (EXT_RAM_BSS_ATTR) would have cost. */
static cc_route_t *g_route;
static uint8_t *g_objbuf;

static int big_mem(void)
{
    if (!g_objbuf) g_objbuf = calloc(1, CC_RT_MAX_BYTES);
    if (!g_route) g_route = calloc(1, sizeof *g_route);
    return g_objbuf && g_route;
}

static int32_t read_object(const uint8_t id[8], int kind)
{
    char p[64];
    cc_object_path(p, sizeof p, id, kind);
    if (!big_mem())
        return -1;
    hal_file_t f = S.fs->open(S.fs, p, HAL_FS_READ);
    if (f < 0)
        return -1;
    int32_t n = S.fs->read(S.fs, f, g_objbuf, CC_RT_MAX_BYTES);
    S.fs->close(S.fs, f);
    return n;
}

static int id_zero(const uint8_t id[8])
{
    for (int i = 0; i < 8; i++)
        if (id[i]) return 0;
    return 1;
}

static int load_workout(const uint8_t id[8])
{
    int32_t n = read_object(id, 1);
    if (n <= 0 || cc_cw1_decode(g_objbuf, (size_t)n, &S.wo) != CC_CW1_OK)
        return -1;
    S.wo_loaded = 1;
    cc_wo_engine_load(&S.eng, &S.wo, S.set.ftp);
    return 0;
}

static int load_route(const uint8_t id[8])
{
    int32_t n = read_object(id, 2);
    if (n <= 0 || cc_cr1_decode(g_objbuf, (size_t)n, g_route) != CC_CR1_OK)
        return -1;
    S.rt_loaded = 1;
    cc_nav_load(&S.nav, g_route);
    return 0;
}

/* ------------------------------------------------------------------ init */
int cc_init(hal_fs_t *fs, uint32_t now_ms)
{
    memset(&S, 0, sizeof S);
    S.fs = fs;
    cc_sensors_init(&S.sens);
    cc_alt_init(&S.alt);
    cc_alerts_init(&S.al);
    cc_nav_init(&S.nav);
    S.battery = -1;
    cc_settings_load(fs, &S.set, &S.slots);
    int recovered = cc_store_boot(fs);
    if (!id_zero(S.set.workout_id) && load_workout(S.set.workout_id) != 0)
        memset(S.set.workout_id, 0, 8);
    if (!id_zero(S.set.route_id) && load_route(S.set.route_id) != 0)
        memset(S.set.route_id, 0, 8);
    S.state = CC_RS_READY;
    S.last_sec = now_ms / 1000u;
    cc_tick(now_ms);
    return recovered;
}

hal_fs_t *cc_fs(void) { return S.fs; }

int cc_ride_active(void)
{
    switch (S.state) {
    case CC_RS_RECORDING: return 1;
    case CC_RS_PAUSED:
    case CC_RS_PAUSE_MENU:
    case CC_RS_CONFIRM_DISCARD: return 2;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ time */
static void time_learned(uint32_t now_ms)
{
    if (S.provisional && !S.offset_known && (S.state != CC_RS_READY && S.state != CC_RS_SUMMARY)) {
        S.offset = (int32_t)(utc_now(now_ms) - (CC_FIT_PROVISIONAL_BASE + now_ms / 1000u));
        S.offset_known = 1;
        uint8_t b[8];
        cc_put_u32(b, (uint32_t)S.offset);
        cc_put_u32(b + 4, cc_crc32(b, 4));
        char p[64];
        cc_ride_path(p, sizeof p, S.ride_id, ".time");
        hal_file_t f = S.fs->open(S.fs, p, HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
        if (f >= 0) {
            S.fs->write(S.fs, f, b, 8);
            S.fs->fsync(S.fs, f);
            S.fs->close(S.fs, f);
        }
    }
}

void cc_set_utc(uint32_t now_ms, uint32_t unix_s)
{
    if (S.utc_from_gnss || unix_s == 0)
        return; /* GNSS time wins once it is known */
    S.utc_val = unix_s;
    S.utc_at_ms = now_ms;
    S.utc_known = 1;
    time_learned(now_ms);
}

void cc_set_tz(int16_t tz_min)
{
    if (S.set.tz_min != tz_min) {
        S.set.tz_min = tz_min;
        save_settings();
    }
}

/* ------------------------------------------------------------------ inputs */
void cc_on_power_sample(uint32_t now_ms, int32_t watts, int has_crank, uint16_t revs, uint16_t time)
{
    cc_sensors_power(&S.sens, now_ms, watts, has_crank, revs, time);
}

void cc_on_hr_sample(uint32_t now_ms, int32_t bpm) { cc_sensors_hr(&S.sens, now_ms, bpm); }

void cc_on_sensor_state(int kind, cc_sens_state_t st)
{
    if (kind >= 1 && kind <= 2)
        S.fw_state[kind] = st;
}

void cc_on_gnss(uint32_t now_ms, const hal_gnss_fix_t *fix)
{
    S.fix = *fix;
    S.fix_fresh = 1;
    if (fix->utc_seconds) {
        S.utc_val = fix->utc_seconds;
        S.utc_at_ms = now_ms;
        S.utc_from_gnss = 1;
        S.utc_known = 1;
        time_learned(now_ms);
    }
}

void cc_on_baro(uint32_t now_ms, int32_t pa)
{
    (void)now_ms;
    cc_alt_baro(&S.alt, pa);
}

void cc_on_battery(int pct, int charging)
{
    S.battery = pct;
    S.charging = charging;
}

void cc_on_phone(int connected) { S.phone = connected; }

/* ------------------------------------------------------------------ ride actions */
static void reset_ride(void)
{
    S.timer_s = S.elapsed_s = S.paused_s = S.lap_s = 0;
    S.dist_cm = S.lap_start_cm = 0;
    S.psum = 0;
    S.pn = 0;
    S.ap_below = S.ap_above = 0;
    S.pause_auto = 0;
    S.focus = 0;
    S.offset_known = 0;
    S.offset = 0;
    S.storage_failing = S.storage_alerted = 0;
    S.lowbatt15 = S.lowbatt5 = 0;
    S.power_alerted = 0;
    S.gps_alerted = 0;
    S.nextstep_active = 0;
    cc_alerts_init(&S.al);
}

int cc_cmd_start(uint32_t now_ms)
{
    if (S.state != CC_RS_READY)
        return -1;
    reset_ride();
    char p[64];
    S.fs->mkdir(S.fs, "rides");
    if (S.utc_known) {
        S.provisional = 0;
        S.ride_id = utc_now(now_ms);
    } else {
        S.provisional = 1;
        S.set.ride_counter++;
        save_settings();
        S.ride_id = S.set.ride_counter;
    }
    /* Never reuse a name */
    for (int k = 0; k < 100; k++) {
        cc_ride_path(p, sizeof p, S.ride_id, ".fit.part");
        hal_file_t f = S.fs->open(S.fs, p, HAL_FS_READ);
        char q[64];
        cc_ride_path(q, sizeof q, S.ride_id, ".fit");
        hal_file_t g = S.fs->open(S.fs, q, HAL_FS_READ);
        if (f >= 0) S.fs->close(S.fs, f);
        if (g >= 0) S.fs->close(S.fs, g);
        if (f < 0 && g < 0)
            break;
        S.ride_id++;
    }
    uint32_t ts = ts_now(now_ms);
    cc_fit_begin(&S.fit, S.fs, p, ts);
    cc_fit_event_timer(&S.fit, ts, 1);
    if (S.fit.io_error) {
        S.storage_failing = 1;
        S.storage_since_ms = now_ms;
    }
    cc_alt_ride_start(&S.alt);
    if (S.wo_loaded) {
        cc_wo_engine_load(&S.eng, &S.wo, S.set.ftp);
        int32_t v;
        int ok = p3(&v);
        cc_wo_engine_second(&S.eng, 0, ok, v);
    }
    if (S.rt_loaded)
        cc_nav_load(&S.nav, g_route);
    S.state = CC_RS_RECORDING;
    return 0;
}

static void do_lap(uint32_t now_ms, int trigger)
{
    uint32_t ts = ts_now(now_ms);
    cc_fit_lap(&S.fit, ts, trigger);
    if (trigger == CC_FIT_LAP_MANUAL) {
        cc_banner_t b;
        cc_banner_lap(&b, S.fit.laps, S.lap_s, (S.dist_cm - S.lap_start_cm) / 100u, S.set.units == CC_UNITS_MI);
        cc_alerts_raise(&S.al, &b, now_ms);
    }
    S.lap_s = 0;
    S.lap_start_cm = S.dist_cm;
    flush(now_ms);
}

static void do_pause(uint32_t now_ms, int automatic)
{
    cc_fit_event_timer(&S.fit, ts_now(now_ms), 0);
    flush(now_ms);
    S.state = CC_RS_PAUSED;
    S.pause_auto = automatic;
    S.paused_s = 0;
    S.ap_above = 0;
    S.ap_below = 0;
}

static void do_resume(uint32_t now_ms)
{
    cc_fit_event_timer(&S.fit, ts_now(now_ms), 1);
    S.state = CC_RS_RECORDING;
    S.pause_auto = 0;
    S.ap_below = 0;
    S.ap_above = 0;
}

static void after_ride(void)
{
    if (S.wo_loaded)
        cc_wo_engine_load(&S.eng, &S.wo, S.set.ftp);
    if (S.rt_loaded)
        cc_nav_load(&S.nav, g_route);
    cc_alerts_init(&S.al);
}

static void do_finish(uint32_t now_ms, int show_summary)
{
    S.state = CC_RS_SAVING;
    int32_t ascent = S.alt.have ? (int32_t)lroundf(S.alt.ascent) : -1;
    int time_known = !S.provisional || S.offset_known;
    memset(&S.summary, 0, sizeof S.summary);
    if (cc_ride_finish(S.fs, &S.fit, S.ride_id, ts_now(now_ms), ascent, time_known, S.provisional ? S.offset : 0,
                       &S.summary) != 0) {
        /* The .part stays and is recovered at the next boot; show what we have. */
        S.summary.ride_id = S.ride_id;
        cc_fit_summary(&S.fit, &S.summary.sum);
    } else {
        S.saved_seq++;
    }
    after_ride();
    S.state = show_summary ? CC_RS_SUMMARY : CC_RS_READY;
}

static void do_discard(void)
{
    cc_ride_discard(S.fs, &S.fit, S.ride_id);
    after_ride();
    S.state = CC_RS_READY;
}

static void power_off(uint32_t now_ms)
{
    if (S.state == CC_RS_RECORDING || S.state == CC_RS_PAUSED || S.state == CC_RS_PAUSE_MENU ||
        S.state == CC_RS_CONFIRM_DISCARD)
        do_finish(now_ms, 0);
    S.state = CC_RS_READY;
    save_settings();
    S.power_off = 1;
}

static void pause_menu_activate(uint32_t now_ms, int row)
{
    switch (row) {
    case CC_PM_RESUME: do_resume(now_ms); break;
    case CC_PM_FINISH: do_finish(now_ms, 1); break;
    case CC_PM_DISCARD: S.state = CC_RS_CONFIRM_DISCARD; S.focus = CC_CF_KEEP; break;
    default: break;
    }
}

static void confirm_activate(int button)
{
    if (button == CC_CF_DISCARD)
        do_discard();
    else {
        S.state = CC_RS_PAUSE_MENU;
        S.focus = CC_PM_RESUME;
    }
}

void cc_cmd_pause_menu_select(uint32_t now_ms, int row)
{
    if (S.state != CC_RS_PAUSE_MENU || row < 0 || row >= CC_PM_N)
        return;
    S.focus = row;
    pause_menu_activate(now_ms, row);
}

void cc_cmd_confirm_select(uint32_t now_ms, int button)
{
    (void)now_ms;
    if (S.state != CC_RS_CONFIRM_DISCARD || button < 0 || button >= CC_CF_N)
        return;
    S.focus = button;
    confirm_activate(button);
}

void cc_cmd_summary_done(void)
{
    if (S.state == CC_RS_SUMMARY)
        S.state = CC_RS_READY;
}

int cc_cmd_set_ftp(uint16_t ftp)
{
    if (S.state != CC_RS_READY && S.state != CC_RS_SUMMARY)
        return -1;
    if (ftp < CC_FTP_MIN_W || ftp > CC_FTP_MAX_W)
        return -2;
    S.set.ftp = ftp;
    save_settings();
    if (S.wo_loaded)
        cc_wo_engine_set_ftp(&S.eng, ftp);
    return 0;
}

int cc_cmd_settings(const cc_settings_t *s)
{
    S.set.brightness = s->brightness < CC_BRIGHT_N ? s->brightness : S.set.brightness;
    S.set.screen_off = s->screen_off < CC_SCREENOFF_N ? s->screen_off : S.set.screen_off;
    S.set.units = s->units < CC_UNITS_N ? s->units : S.set.units;
    S.set.autopause = s->autopause ? 1 : 0;
    return cc_settings_save(S.fs, &S.set, &S.slots);
}

int cc_cmd_load_workout(const uint8_t id[8])
{
    if (cc_ride_active())
        return -1;
    if (load_workout(id) != 0)
        return -2;
    memcpy(S.set.workout_id, id, 8);
    save_settings();
    return 0;
}

int cc_cmd_load_route(const uint8_t id[8])
{
    if (cc_ride_active())
        return -1;
    if (load_route(id) != 0)
        return -2;
    memcpy(S.set.route_id, id, 8);
    save_settings();
    return 0;
}

void cc_cmd_unload_workout(void)
{
    S.wo_loaded = 0;
    cc_wo_engine_unload(&S.eng);
    memset(S.set.workout_id, 0, 8);
    save_settings();
}

void cc_cmd_unload_route(void)
{
    S.rt_loaded = 0;
    cc_nav_unload(&S.nav);
    memset(S.set.route_id, 0, 8);
    save_settings();
}

/* ------------------------------------------------------------------ buttons */
static void gesture(uint32_t now_ms, cc_btn_t b, int hold)
{
    if (b == CC_BTN_PWR && hold) {
        power_off(now_ms);
        return;
    }
    switch (S.state) {
    case CC_RS_RECORDING:
        if (b == CC_BTN_R)
            hold ? do_pause(now_ms, 0) : do_lap(now_ms, CC_FIT_LAP_MANUAL);
        else if (b == CC_BTN_L)
            push_gesture(b, hold); /* page change, UI */
        break;
    case CC_RS_PAUSED:
        if (hold)
            break;
        if (b == CC_BTN_R)
            do_resume(now_ms);
        else if (b == CC_BTN_L) {
            S.state = CC_RS_PAUSE_MENU;
            S.focus = CC_PM_RESUME;
        }
        break;
    case CC_RS_PAUSE_MENU:
        if (hold)
            break;
        if (b == CC_BTN_L)
            S.state = CC_RS_PAUSED;
        else if (b == CC_BTN_PWR)
            S.focus = (S.focus + 1) % CC_PM_N;
        else
            pause_menu_activate(now_ms, S.focus);
        break;
    case CC_RS_CONFIRM_DISCARD:
        if (hold)
            break;
        if (b == CC_BTN_L)
            confirm_activate(CC_CF_KEEP);
        else if (b == CC_BTN_PWR)
            S.focus = (S.focus + 1) % CC_CF_N;
        else
            confirm_activate(S.focus);
        break;
    case CC_RS_SUMMARY:
        if (!hold && (b == CC_BTN_L || b == CC_BTN_R))
            S.state = CC_RS_READY;
        else
            push_gesture(b, hold);
        break;
    case CC_RS_SAVING:
        break;
    case CC_RS_READY:
    default:
        push_gesture(b, hold);
        break;
    }
}

void cc_on_button(uint32_t now_ms, cc_btn_t b, int down)
{
    if (b >= CC_BTN_N)
        return;
    if (down) {
        if (S.btn[b].down)
            return;
        S.btn[b].down = 1;
        S.btn[b].down_ms = now_ms;
        S.btn[b].fired = 0;
        cc_alerts_button(&S.al, now_ms);
        return;
    }
    if (!S.btn[b].down)
        return;
    S.btn[b].down = 0;
    if (S.btn[b].fired)
        return; /* the release after a fired hold is swallowed */
    gesture(now_ms, b, 0);
}

static void buttons_tick(uint32_t now_ms)
{
    for (int b = 0; b < CC_BTN_N; b++) {
        if (!S.btn[b].down || S.btn[b].fired)
            continue;
        uint32_t th = b == CC_BTN_PWR ? CC_HOLD_POWER_MS : CC_HOLD_BOTTOM_MS;
        if (now_ms - S.btn[b].down_ms >= th) {
            S.btn[b].fired = 1;
            gesture(now_ms, (cc_btn_t)b, 1);
        }
    }
}

/* ------------------------------------------------------------------ the second */
static void second(uint32_t now_ms)
{
    int recording = S.state == CC_RS_RECORDING;
    int paused = S.state == CC_RS_PAUSED || S.state == CC_RS_PAUSE_MENU || S.state == CC_RS_CONFIRM_DISCARD;

    cc_sensors_second(&S.sens, now_ms);
    int32_t pw3 = 0, cad = 0, hr = 0, bin = 0;
    int pw3_ok = p3(&pw3);
    int cad_ok = cc_sensors_cadence(&S.sens, now_ms, &cad);
    int hr_ok = cc_chan_last(&S.sens.hr, &hr);
    int bin_ok = cc_chan_last(&S.sens.power, &bin);

    /* GNSS: one entry per second */
    int fix_ok = S.fix_fresh && S.fix.valid;
    S.spd_head = (S.spd_head + 1) % 3;
    S.spd_ok[S.spd_head] = (uint8_t)fix_ok;
    S.spd[S.spd_head] = fix_ok ? (uint32_t)lroundf(S.fix.speed_mps * 1000.0f) : 0;
    S.fix_fresh = 0;
    uint32_t spd = 0;
    int spd_ok = speed3(&spd);
    if (fix_ok) {
        S.no_fix_s = 0;
        S.gps_lost = 0;
        S.gps_alerted = 0;
    } else if (S.no_fix_s < 0xFFFF) {
        S.no_fix_s++;
    }

    if (recording) {
        S.timer_s++;
        S.lap_s++;
        S.elapsed_s++;
        if (spd_ok && fix_ok && spd >= 500)
            S.dist_cm += spd / 10u;
        if (bin_ok) {
            S.psum += (uint64_t)bin;
            S.pn++;
        }
        if (S.no_fix_s >= 10) {
            S.gps_lost = 1;
            if (!S.gps_alerted) {
                cc_banner_t b;
                cc_banner_gpslost(&b);
                cc_alerts_raise(&S.al, &b, now_ms);
                S.gps_alerted = 1;
            }
        }
    } else if (paused) {
        S.elapsed_s++;
        S.paused_s++;
    }

    cc_alt_second(&S.alt, recording, S.dist_cm, spd_ok ? spd : 0, fix_ok, S.fix.alt_m);

    if (recording && S.wo_loaded) {
        unsigned ev = cc_wo_engine_second(&S.eng, S.timer_s, pw3_ok, pw3);
        if (ev & CC_WO_EV_STEP_CHANGE) {
            if (S.nextstep_active) {
                cc_alerts_clear(&S.al, CC_AL_NEXTSTEP, now_ms);
                S.nextstep_active = 0;
            }
            do_lap(now_ms, CC_FIT_LAP_TIME);
        }
        if (ev & CC_WO_EV_COMPLETE) {
            cc_banner_t b;
            cc_banner_complete(&b, S.wo.name);
            cc_alerts_raise(&S.al, &b, now_ms);
        }
        const cc_wo_view_t *w = &S.eng.view;
        if (w->nextstep_banner) {
            char right[12];
            snprintf(right, sizeof right, "%" PRIu32 " s", w->remaining_s);
            if (!S.nextstep_active) {
                cc_banner_t b;
                cc_banner_nextstep(&b, w->next_label, w->next_lo_w, w->next_hi_w, w->next_secs, w->remaining_s,
                                   cc_zone_rgb[w->next_zone], w->remaining_s * 1000u);
                cc_alerts_raise(&S.al, &b, now_ms);
                S.nextstep_active = 1;
            } else {
                cc_alerts_set_right(&S.al, CC_AL_NEXTSTEP, right);
            }
        }
    }

    if (S.rt_loaded) {
        unsigned ev = cc_nav_second(&S.nav, fix_ok, S.fix.lat_e7, S.fix.lon_e7, spd_ok ? spd : 0,
                                    S.alt.have ? S.alt.ascent : -1.0f);
        if ((ev & CC_NAV_EV_CLIMB_AHEAD) && recording) {
            const cc_climb_t *c = &g_route->climbs[S.nav.climb_idx];
            cc_nav_view_t nv;
            cc_nav_view(&S.nav, S.alt.ascent, &nv);
            cc_banner_t b;
            cc_banner_climbahead(&b, c->length_m, c->ascent_m, c->avg_x10, nv.climb_to_start_m);
            cc_alerts_raise(&S.al, &b, now_ms);
        }
    }

    /* Alerts that depend on sensors and battery, while recording */
    if (recording) {
        if (cc_chan_lost(&S.sens.power, now_ms)) {
            if (!S.power_alerted) {
                cc_banner_t b;
                cc_banner_powerlost(&b);
                cc_alerts_raise(&S.al, &b, now_ms);
                S.power_alerted = 1;
            }
        } else {
            S.power_alerted = 0;
        }
        if (S.battery >= 0 && S.battery <= CC_LOWBATT_2_PCT && !S.lowbatt5) {
            cc_banner_t b;
            cc_banner_lowbatt(&b, CC_LOWBATT_2_PCT);
            cc_alerts_raise(&S.al, &b, now_ms);
            S.lowbatt5 = S.lowbatt15 = 1;
        } else if (S.battery >= 0 && S.battery <= CC_LOWBATT_1_PCT && !S.lowbatt15) {
            cc_banner_t b;
            cc_banner_lowbatt(&b, CC_LOWBATT_1_PCT);
            cc_alerts_raise(&S.al, &b, now_ms);
            S.lowbatt15 = 1;
        }
    }

    /* FIT record for this second */
    if (recording) {
        cc_fit_record_t r;
        memset(&r, 0, sizeof r);
        r.ts = ts_now(now_ms);
        if (fix_ok) {
            r.has_pos = 1;
            r.lat_e7 = S.fix.lat_e7;
            r.lon_e7 = S.fix.lon_e7;
        }
        float am;
        if (cc_alt_absolute(&S.alt, &am)) {
            r.has_alt = 1;
            r.alt_baro = 1;
            r.alt_cm = (int32_t)lroundf(am * 100.0f);
        } else if (fix_ok) {
            /* No barometer: the GNSS altitude goes in the file (Q19) but alt_baro stays 0, so no
               ascent is claimed from it. Brief 5.3 defines ascent from the barometer alone. */
            r.has_alt = 1;
            r.alt_cm = (int32_t)lroundf(S.fix.alt_m * 100.0f);
        }
        if (spd_ok && !S.gps_lost) {
            r.has_speed = 1;
            r.speed_mms = spd;
        }
        r.dist_cm = S.dist_cm;
        if (hr_ok) { r.has_hr = 1; r.hr = (uint8_t)(hr > 255 ? 255 : hr); }
        if (cad_ok) { r.has_cad = 1; r.cad = (uint8_t)(cad > 254 ? 254 : cad); }
        if (bin_ok) { r.has_power = 1; r.power = (uint16_t)(bin > 65534 ? 65534 : bin); }
        cc_fit_record(&S.fit, &r);
        if (S.timer_s % 10 == 0)
            flush(now_ms);
    }
    if (recording || paused)
        storage_check(now_ms);

    /* Auto-pause (spec 10.3): suspended without a usable fix */
    if (S.set.autopause && fix_ok && spd_ok) {
        int pedalling = (cad_ok && cad > 0) || (pw3_ok && pw3 > 0);
        if (recording) {
            S.ap_below = (spd < CC_AUTOPAUSE_BELOW_MMS && !pedalling) ? S.ap_below + 1 : 0;
            if (S.ap_below >= CC_AUTOPAUSE_SECS)
                do_pause(now_ms, 1);
        } else if (S.state == CC_RS_PAUSED && S.pause_auto) {
            S.ap_above = spd >= CC_AUTORESUME_MMS ? S.ap_above + 1 : 0;
            if (S.ap_above >= CC_AUTORESUME_SECS)
                do_resume(now_ms);
        }
    } else {
        S.ap_below = 0;
        S.ap_above = 0;
    }
    (void)hr_ok;
}

/* ------------------------------------------------------------------ view */
static cc_sens_state_t sens_state(int kind, const cc_chan_t *c, uint32_t now_ms)
{
    cc_sens_state_t fw = S.fw_state[kind];
    if (fw == CC_SENS_OFF && !c->have_sample)
        return CC_SENS_OFF;
    if (!c->have_sample)
        return CC_SENS_SEARCHING;
    if (cc_chan_lost(c, now_ms))
        return CC_SENS_LOST;
    return CC_SENS_OK;
}

static void fill_view(uint32_t now_ms)
{
    cc_view_t *v = &S.v;
    v->state = S.state;
    v->pause_auto = S.pause_auto;
    v->focus = S.focus;
    v->timer_s = S.timer_s;
    v->elapsed_s = S.elapsed_s;
    v->paused_s = S.paused_s;
    v->lap_s = S.lap_s;
    v->power3_ok = cc_chan_avg(&S.sens.power, 3, &v->power3);
    v->power10_ok = cc_chan_avg(&S.sens.power, 10, &v->power10);
    if (cc_chan_lost(&S.sens.power, now_ms))
        v->power3_ok = v->power10_ok = 0;
    v->hr_ok = cc_chan_last(&S.sens.hr, &v->hr) && !cc_chan_lost(&S.sens.hr, now_ms);
    v->cad_ok = cc_sensors_cadence(&S.sens, now_ms, &v->cad);
    v->speed_ok = speed3(&v->speed_mms) && !S.gps_lost;
    v->dist_m = S.dist_cm / 100u;
    v->avg_speed_mms = S.timer_s ? (uint32_t)((uint64_t)S.dist_cm * 10u / S.timer_s) : 0;
    v->avg_power_ok = S.pn > 0;
    v->avg_power = S.pn ? (int32_t)cc_div_half_up((int64_t)S.psum, S.pn) : 0;
    v->zone = v->power3_ok ? cc_zone_of_power(v->power3, S.set.ftp) : 0;
    v->grad_ok = S.alt.grad_valid;
    v->grad_x10 = S.alt.grad_x10;
    v->steep = S.alt.steep;
    float am;
    v->alt_ok = cc_alt_absolute(&S.alt, &am);
    v->alt_m = v->alt_ok ? (int32_t)lroundf(am) : 0;
    v->ascent_m = (uint32_t)lroundf(S.alt.ascent);
    v->power_state = sens_state(1, &S.sens.power, now_ms);
    v->hr_state = sens_state(2, &S.sens.hr, now_ms);
    v->gps_fix = S.no_fix_s == 0;
    if (S.fix.valid) {
        v->lat_e7 = S.fix.lat_e7;
        v->lon_e7 = S.fix.lon_e7;
        v->course_deg = S.fix.course_deg;
    }
    v->gps_lost = S.gps_lost;
    v->phone_connected = S.phone;
    v->battery_pct = S.battery;
    v->charging = S.charging;
    v->utc_known = S.utc_known;
    v->utc = S.utc_known ? utc_now(now_ms) : 0;
    v->tz_min = S.set.tz_min;
    v->workout_loaded = S.wo_loaded;
    if (S.wo_loaded) {
        snprintf(v->workout_name, sizeof v->workout_name, "%s", S.wo.name);
        v->workout_secs = S.wo.total_secs;
        v->workout_steps = S.wo.nsteps;
        v->wo = S.eng.view;
    } else {
        memset(&v->wo, 0, sizeof v->wo);
    }
    v->route_loaded = S.rt_loaded;
    if (S.rt_loaded) {
        snprintf(v->route_name, sizeof v->route_name, "%s", g_route->name);
        v->route_dist_m = g_route->dist_m;
        v->route_ascent_m = g_route->ascent_m;
        v->route_climbs = g_route->nclimbs;
    }
    cc_nav_view(&S.nav, S.alt.have ? S.alt.ascent : -1.0f, &v->nav);
    v->ring_n = 0;
    v->ring[v->ring_n++] = CC_PAGE_MAIN;
    if (S.wo_loaded) v->ring[v->ring_n++] = CC_PAGE_WORKOUT;
    v->ring[v->ring_n++] = CC_PAGE_CLIMB;
    if (S.rt_loaded) v->ring[v->ring_n++] = CC_PAGE_NAV;
    v->banner = S.al.visible;
    v->hold.active = 0;
    if (S.state == CC_RS_RECORDING) {
        for (int b = CC_BTN_L; b <= CC_BTN_R; b++)
            if (S.btn[b].down && !S.btn[b].fired) {
                v->hold.active = 1;
                v->hold.side = (cc_btn_t)b;
                uint32_t e = now_ms - S.btn[b].down_ms;
                v->hold.elapsed_ms = e > CC_HOLD_BOTTOM_MS ? CC_HOLD_BOTTOM_MS : e;
            }
    }
    v->summary = S.summary;
    v->storage_error = S.storage_alerted;
    v->power_off_request = S.power_off;
    v->ftp = S.set.ftp;
    v->settings = S.set;
    v->ride_id = S.ride_id;
    v->saved_seq = S.saved_seq;
    v->saved_ride_id = S.summary.ride_id;
}

void cc_tick(uint32_t now_ms)
{
    hal_now_ms_hint = now_ms;
    cc_calibration_tick(now_ms);
    uint32_t sec = now_ms / 1000u;
    while (S.last_sec < sec) { /* one pass per second boundary, even after a stall */
        S.last_sec++;
        second(S.last_sec * 1000u > now_ms ? now_ms : (S.last_sec == sec ? now_ms : S.last_sec * 1000u));
    }
    buttons_tick(now_ms);
    cc_alerts_tick(&S.al, now_ms);
    fill_view(now_ms);
}

const cc_view_t *cc_view(void) { return &S.v; }

/* ------------------------------------------------------------------ link host */
static int lh_state(void *c) { (void)c; return cc_ride_active(); }
static int lh_batt(void *c) { (void)c; return S.battery; }
static uint16_t lh_ftp(void *c) { (void)c; return S.set.ftp; }
static int lh_set_ftp(void *c, uint16_t f)
{
    (void)c;
    int rc = cc_cmd_set_ftp(f);
    return rc == 0 ? 0 : rc == -1 ? -1 : -2;
}
static void lh_time(void *c, uint32_t u, int16_t tz)
{
    (void)c;
    cc_set_utc(hal_now_ms_hint, u);
    cc_set_tz(tz);
}
static void lh_deleted(void *c, int kind, const uint8_t id[8])
{
    (void)c;
    if (kind == 1 && S.wo_loaded && memcmp(S.set.workout_id, id, 8) == 0)
        cc_cmd_unload_workout();
    if (kind == 2 && S.rt_loaded && memcmp(S.set.route_id, id, 8) == 0)
        cc_cmd_unload_route();
}

void cc_link_host_bind(cc_link_host_t *h)
{
    h->ride_state = lh_state;
    h->battery_pct = lh_batt;
    h->get_ftp = lh_ftp;
    h->set_ftp = lh_set_ftp;
    h->set_time = lh_time;
    h->object_deleted = lh_deleted;
    h->ctx = NULL;
}
