/* ui_app.c: the shared screen manager on top of the core (spec 4 and 6, brief 6.1 and 6.4).
 *
 * Used by both the host simulator (ui/sim/app.c) and the unit firmware (fw/main/main.c). The UI owns
 * screen navigation, focus and blanking; ride state belongs to the core. Large route buffers are
 * allocated once on first use (calloc) so they land in PSRAM on the ESP32-S3 rather than overflowing
 * internal SRAM .bss (see core/src/core.c:big_mem). */
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/nav.h"
#include "cc700/provisional.h"
#include "cc700/store.h"

typedef struct {
    hal_fs_t *fs;
    ui_app_hooks_t hooks;
    int has_hooks;
    ui_model_t m;
    ui_screen_id_t stack[12];
    int depth;
    int blank;
    uint32_t now;
    uint32_t last_input_ms;
    uint32_t last_state;
    int cal_t;           /* simulated calibration countdown when hooks are absent */
    uint32_t pair_until; /* simulated 60 s pairing window when hooks are absent */
    int scan_on;         /* the sensor_scan hook was last called with 1 */
    int dirty;
    int had_input;
    int touch_down;
    uint32_t touch_since; /* U.now when the current touch began */
    int redraw_owed;      /* a once-a-second redraw was held back by a touch */
    int touch_dragged;
    int touch_ate;
    int tap_pending;
    int16_t touch_x0, touch_y0, touch_last_y;
    int16_t tap_x, tap_y;
    lv_obj_t *scr;
    lv_group_t *grp;
} ui_app_t;

static ui_app_t U;

/* Longest a touch may hold back the once-a-second redraw outside a ride (ui_app_step). */
#define UI_TOUCH_REDRAW_HOLD_MS 1500u
static cc_route_t *rt_ui; /* allocated once on first use (208 KB: PSRAM on the unit) */
static uint8_t *obj_buf;  /* allocated once on first use (131 KB: PSRAM on the unit) */
static int rt_ui_ok;
static uint8_t rt_ui_id[8];

static int ui_big_mem(void)
{
    if (!obj_buf) obj_buf = calloc(1, CC_RT_MAX_BYTES);
    if (!rt_ui) rt_ui = calloc(1, sizeof *rt_ui);
    return obj_buf && rt_ui;
}

ui_screen_id_t ui_app_top(void) { return U.stack[U.depth - 1]; }
int ui_app_blank(void) { return U.blank; }
int ui_app_dirty(void) { return U.dirty; }

/* What the last released tap did, for the firmware's bring-up tap log (fw/main/main.c). Written by
   on_click, tap_xy and the release path; a sequence number says when it changed. */
static char tap_what[48];
static uint32_t tap_seq;
static void tap_note(const char *what, int a, int b)
{
    snprintf(tap_what, sizeof tap_what, "%s %d %d", what, a, b);
    tap_seq++;
}
const char *ui_app_last_tap(uint32_t *seq)
{
    if (seq) *seq = tap_seq;
    return tap_what;
}
ui_model_t *ui_app_model(void) { return &U.m; }

int ui_app_take_input(void)
{
    int r = U.had_input;
    U.had_input = 0;
    return r;
}

static void push(ui_screen_id_t s)
{
    if (U.depth < 12) U.stack[U.depth++] = s;
    U.m.focus = 0;
    U.m.scroll = 0;
    U.dirty = 1;
}

static void pop(void)
{
    if (U.depth > 1) U.depth--;
    U.m.focus = 0;
    U.m.scroll = 0;
    U.dirty = 1;
}

static void reset_to(ui_screen_id_t s)
{
    U.depth = 0;
    push(s);
}

/* ------------------------------------------------------------------ model from the store */
static int obj_cb(void *ctx, const hal_dirent_t *e)
{
    (void)ctx;
    size_t n = strlen(e->name);
    if (n != 20 || !ui_big_mem()) return 0;
    char p[96];
    snprintf(p, sizeof p, "objects/%s", e->name);
    hal_file_t f = U.fs->open(U.fs, p, HAL_FS_READ);
    if (f < 0) return 0;
    int32_t len = U.fs->read(U.fs, f, obj_buf, CC_RT_MAX_BYTES);
    U.fs->close(U.fs, f);
    if (len <= 0) return 0;
    if (!strcmp(e->name + 16, ".cw1") && U.m.n_workouts < UI_MAX_LIST) {
        static cc_workout_t w;
        if (cc_cw1_decode(obj_buf, (size_t)len, &w) == CC_CW1_OK) {
            ui_wo_item_t *it = &U.m.workouts[U.m.n_workouts++];
            snprintf(it->name, sizeof it->name, "%s", w.name);
            it->secs = w.total_secs;
            it->steps = w.nsteps;
            memcpy(it->id, w.id, 8);
        }
    } else if (!strcmp(e->name + 16, ".cr1") && U.m.n_routes < UI_MAX_LIST) {
        rt_ui_ok = 0;
        if (cc_cr1_decode(obj_buf, (size_t)len, rt_ui) == CC_CR1_OK) {
            ui_rt_item_t *it = &U.m.routes[U.m.n_routes++];
            snprintf(it->name, sizeof it->name, "%s", rt_ui->name);
            it->dist_m = rt_ui->dist_m;
            it->ascent_m = rt_ui->ascent_m;
            it->climbs = rt_ui->nclimbs;
            it->has_profile = rt_ui->nprof > 0;
            memcpy(it->id, rt_ui->id, 8);
        }
    }
    return 0;
}

static int load_object(int kind, const uint8_t id[8], uint8_t *buf, size_t cap)
{
    char p[64];
    cc_object_path(p, sizeof p, id, kind);
    hal_file_t f = U.fs->open(U.fs, p, HAL_FS_READ);
    if (f < 0) return -1;
    int32_t n = U.fs->read(U.fs, f, buf, (uint32_t)cap);
    U.fs->close(U.fs, f);
    return n;
}

void ui_app_refresh_lists(void)
{
    U.m.n_workouts = U.m.n_routes = 0;
    if (!U.fs) return;
    U.fs->list(U.fs, "objects", obj_cb, NULL);
    static uint32_t ids[300];
    int n = cc_rides_list(U.fs, ids, 300);
    U.m.rides_total = n;
    U.m.n_rides = 0;
    int tz = U.m.v.settings.tz_min * 60;
    for (int i = n - 1; i >= 0 && U.m.n_rides < UI_MAX_LIST; i--) {
        cc_ride_meta_t mt;
        if (!cc_ride_meta_read(U.fs, ids[i], &mt)) continue;
        ui_ride_item_t *it = &U.m.rides[U.m.n_rides++];
        it->ride_id = mt.ride_id;
        it->start_local = mt.sum.start_ts + (uint32_t)tz;
        it->dist_m = mt.sum.distance_m;
        it->timer_s = mt.sum.timer_s;
        it->avg_power = mt.sum.avg_power;
        it->has_power = mt.sum.has_power;
        it->uploaded = (mt.flags & CC_RIDE_UPLOADED) != 0;
    }
    U.dirty = 1;
}

/* Map points around the rider and the profile strip of the active climb, from the UI's route copy. */
static void nav_extras(void)
{
    ui_model_t *m = &U.m;
    const cc_view_t *v = &m->v;
    m->map_n = 0;
    m->climb_prof_n = 0;
    if (!v->route_loaded) { rt_ui_ok = 0; return; }
    if (!rt_ui_ok || memcmp(rt_ui_id, v->settings.route_id, 8)) {
        if (!ui_big_mem()) { rt_ui_ok = 0; return; }
        int n = load_object(2, v->settings.route_id, obj_buf, CC_RT_MAX_BYTES);
        rt_ui_ok = n > 0 && cc_cr1_decode(obj_buf, (size_t)n, rt_ui) == CC_CR1_OK;
        memcpy(rt_ui_id, v->settings.route_id, 8);
    }
    if (!rt_ui_ok) return;
    float rx, ry;
    cc_route_project(rt_ui, v->lat_e7, v->lon_e7, &rx, &ry);
    float lim = (float)(v->nav.map_scale_m ? v->nav.map_scale_m : 400) * 1.2f;
    m->map_ridden = 0;
    for (int i = 0; i < rt_ui->npts && m->map_n < 128; i++) {
        float dx = rt_ui->x[i] - rx, dy = rt_ui->y[i] - ry;
        if (fabsf(dx) > lim || fabsf(dy) > lim) {
            if (m->map_n) break; /* leaving the window after being inside it */
            continue;
        }
        m->map_x[m->map_n] = dx;
        m->map_y[m->map_n] = dy;
        if (rt_ui->cum[i] <= v->nav.route_pos_m) m->map_ridden = m->map_n;
        m->map_n++;
    }
    m->course_deg = v->course_deg;
    int ci = v->nav.climb_idx;
    if (v->nav.climb_show && ci >= 0 && ci < rt_ui->nclimbs) {
        const cc_climb_t *c = &rt_ui->climbs[ci];
        for (int k = 0; k < 24; k++) {
            float e = 0;
            cc_route_profile_at(rt_ui, (float)c->start_m + (float)c->length_m * (float)k / 23.0f, &e);
            m->climb_prof[k] = e;
        }
        m->climb_prof_n = 24;
        float p = (v->nav.route_pos_m - (float)c->start_m) / (float)(c->length_m ? c->length_m : 1);
        m->climb_pos = p < 0 ? 0 : p > 1 ? 1 : p;
    }
}

static void fill_model(void)
{
    ui_model_t *m = &U.m;
    m->v = *cc_view();
    const cc_view_t *v = &m->v;
    if (v->utc_known) {
        uint32_t loc = v->utc + (uint32_t)(v->tz_min * 60);
        m->clock_min = (int)((loc / 60) % 1440);
    } else {
        m->clock_min = -1;
    }
    if (!U.has_hooks) {
        m->pm_paired = 1;
        m->hr_paired = 1;
    } else {
        if (U.hooks.sensor_saved) {
            /* Paired means saved in config/sensors (or being paired now), not "heard from": a sensor
               that is asleep is still paired and shows as searching. */
            m->pm_paired = U.hooks.sensor_saved(1, m->pm_name, (int)sizeof m->pm_name, m->pm_id4,
                                                (int)sizeof m->pm_id4, NULL);
            m->hr_paired = U.hooks.sensor_saved(2, NULL, 0, m->hr_id4, (int)sizeof m->hr_id4, &m->hr_bars);
        } else {
            m->pm_paired = v->power_state != CC_SENS_OFF;
            m->hr_paired = v->hr_state != CC_SENS_OFF;
        }
        if (U.hooks.sensor_scan_list && ui_app_top() == SCR_ADD_SENSOR) {
            m->n_scan = U.hooks.sensor_scan_list(m->scan, 3); /* the board has room for three rows */
            if (m->focus >= m->n_scan) m->focus = m->n_scan > 0 ? m->n_scan - 1 : 0;
        }
        m->pm_live_w = v->power3_ok ? (int)v->power3 : 0;
        m->pm_live_rpm = v->cad_ok ? (int)v->cad : 0;
        m->hr_live = v->hr_ok ? (int)v->hr : 0;
        if (U.hooks.cal_state)
            m->cal = U.hooks.cal_state();
    }
    if (!m->pm_name[0] && !(U.has_hooks && U.hooks.sensor_saved)) { /* the mockups' sensors */
        snprintf(m->pm_name, sizeof m->pm_name, "PM500");
        snprintf(m->pm_id4, sizeof m->pm_id4, "7C2E");
        snprintf(m->hr_id4, sizeof m->hr_id4, "41B9");
    }
    m->paused_s = v->paused_s;
    if (U.has_hooks) {
        int pairing = U.hooks.pairing && U.hooks.pairing();
        m->phone = v->phone_connected ? UI_PHONE_CONNECTED : pairing ? UI_PHONE_PAIRING : UI_PHONE_NOT_PAIRED;
        m->passkey = U.hooks.passkey ? U.hooks.passkey() : 0;
        if (v->phone_connected && !m->phone_name[0])
            snprintf(m->phone_name, sizeof m->phone_name, "Phone");
    } else if (m->phone != UI_PHONE_PAIRING) {
        m->phone = v->phone_connected ? UI_PHONE_CONNECTED : m->phone;
    }
    nav_extras();
}

/* Which screen shows: the core's ride state wins over the UI stack while a ride is active. */
ui_screen_id_t ui_app_visible(void)
{
    const cc_view_t *v = &U.m.v;
    switch (v->state) {
    case CC_RS_RECORDING: {
        if (U.m.page >= v->ring_n) U.m.page = 0;
        cc_page_t p = v->ring[U.m.page];
        if (p == CC_PAGE_WORKOUT) return SCR_WORKOUT;
        if (p == CC_PAGE_CLIMB) return SCR_CLIMB;
        if (p == CC_PAGE_NAV) {
            if (!v->nav.have_pos) { U.m.empty = UI_EMPTY_GPSWAIT; return SCR_EMPTY; }
            return SCR_NAV;
        }
        return SCR_MAIN;
    }
    case CC_RS_PAUSED: return SCR_PAUSED;
    case CC_RS_PAUSE_MENU: return SCR_PAUSE_MENU;
    case CC_RS_CONFIRM_DISCARD: U.m.confirm = UI_CONFIRM_DISCARD; return SCR_CONFIRM;
    case CC_RS_SUMMARY: return SCR_SUMMARY;
    default: return ui_app_top();
    }
}

static void summary_from_view(void)
{
    const cc_view_t *v = &U.m.v;
    U.m.summary = v->summary.sum;
    U.m.summary_ride.start_local = v->summary.sum.start_ts + (uint32_t)(v->tz_min * 60);
    U.m.summary_uploaded = (v->summary.flags & CC_RIDE_UPLOADED) != 0;
}

/* ------------------------------------------------------------------ rendering */
static void on_click(lv_event_t *e);
static void on_foot_left(lv_event_t *e);
static void on_foot_right(lv_event_t *e);

static void add_foot_hit(lv_obj_t *scr, int x, int y, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, true);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
}

void ui_app_render(void)
{
    fill_model();
    ui_screen_id_t id = ui_app_visible();
    if (id == SCR_SUMMARY && !U.m.summary_from_history) summary_from_view();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_group_t *g = lv_group_create();
    ui_focus_group = g;
    if (U.blank) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    } else {
        ui_build(id, scr, &U.m);
        int ride_page = id == SCR_MAIN || id == SCR_WORKOUT || id == SCR_CLIMB || id == SCR_NAV || id == SCR_EMPTY;
        if (ride_page && U.m.v.state == CC_RS_RECORDING) ui_overlays(scr, &U.m);
        /* Footer touch targets (BACK / MENU on the left, OPEN / CHANGE / LOAD / DONE / ... on the right).
         * Placed on scr in ui_app_render rather than ui_footer so ui_focus_group indices and layout lint
         * stay unchanged. SCR_PAUSED already creates both targets in scr_paused, and SCR_READY creates
         * the right START target in scr_ready. */
        if (!ride_page && id != SCR_ALERTS && id != SCR_PAUSED) {
            add_foot_hit(scr, 0, 282, 108, 38, on_foot_left);
            if (id != SCR_READY && id != SCR_HEART_RATE)
                add_foot_hit(scr, 132, 282, 108, 38, on_foot_right);
        }
    }
    uint32_t n = lv_group_get_obj_count(g);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *o = lv_group_get_obj_by_index(g, i);
        lv_obj_add_event_cb(o, on_click, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    lv_screen_load(scr);
    if (U.scr) lv_obj_delete(U.scr);
    if (U.grp) lv_group_delete(U.grp);
    U.scr = scr;
    U.grp = g;
    U.dirty = 0;
}

/* ------------------------------------------------------------------ actions */
static int list_len(ui_screen_id_t s)
{
    switch (s) {
    case SCR_MENU: return 7;
    case SCR_WORKOUTS: return U.m.n_workouts;
    case SCR_ROUTES: return U.m.n_routes;
    case SCR_HISTORY: return U.m.n_rides;
    case SCR_SETTINGS: return 6;
    case SCR_SENSORS: return 2 + !(U.m.pm_paired && U.m.hr_paired);
    case SCR_ADD_SENSOR: return U.m.n_scan;
    case SCR_PROFILE: return 5;
    case SCR_CONFIRM: return 2;
    case SCR_POWER_METER: case SCR_HEART_RATE: return 2; /* the button (or nothing), then the kebab */
    case SCR_PHONE: return U.m.phone == UI_PHONE_CONNECTED ? 2 : 1;
    default: return 0;
    }
}

static void focus_next(void)
{
    ui_screen_id_t s = ui_app_top();
    int n = list_len(s);
    if (n <= 0) return;
    if (s == SCR_CONFIRM) { U.m.focus = U.m.focus ? 0 : 1; U.dirty = 1; return; }
    U.m.focus = (U.m.focus + 1) % n;
    if (U.m.focus < U.m.scroll) U.m.scroll = U.m.focus;
    if (U.m.focus >= U.m.scroll + 5) U.m.scroll = U.m.focus - 4;
    U.dirty = 1;
}

static void open_confirm(ui_confirm_kind_t k)
{
    U.m.confirm = k;
    push(SCR_CONFIRM);
    U.m.focus = 0; /* the safe button */
}

static void activate(void)
{
    ui_model_t *m = &U.m;
    U.dirty = 1;
    switch (ui_app_top()) {
    case SCR_READY:
        cc_cmd_start(U.now);
        m->page = 0;
        break;
    case SCR_MENU: {
        static const ui_screen_id_t to[7] = {SCR_WORKOUTS, SCR_ROUTES, SCR_SENSORS, SCR_PROFILE, SCR_HISTORY, SCR_PHONE, SCR_SETTINGS};
        int f = m->focus;
        if (to[f] == SCR_WORKOUTS || to[f] == SCR_ROUTES || to[f] == SCR_HISTORY)
            ui_app_refresh_lists();
        push(to[f]);
        if (to[f] == SCR_PROFILE) m->ftp_edit = m->v.ftp;
        break;
    }
    case SCR_WORKOUTS:
        if (m->focus < m->n_workouts && ui_big_mem()) {
            int n = load_object(1, m->workouts[m->focus].id, obj_buf, CC_WO_MAX_BYTES);
            if (n > 0 && cc_cw1_decode(obj_buf, (size_t)n, &m->detail_wo) == CC_CW1_OK) push(SCR_WORKOUT_DETAIL);
        }
        break;
    case SCR_WORKOUT_DETAIL:
        if (cc_cmd_load_workout(m->detail_wo.id) == 0) reset_to(SCR_READY);
        break;
    case SCR_ROUTES:
        if (m->focus < m->n_routes && ui_big_mem()) {
            rt_ui_ok = 0;
            int n = load_object(2, m->routes[m->focus].id, obj_buf, CC_RT_MAX_BYTES);
            if (n > 0 && cc_cr1_decode(obj_buf, (size_t)n, rt_ui) == CC_CR1_OK) {
                m->detail_route = m->focus;
                m->detail_prof_len_m = rt_ui->dist_m;
                m->detail_prof_n = 0;
                for (int k = 0; k < 96 && rt_ui->nprof; k++) {
                    float e = 0;
                    cc_route_profile_at(rt_ui, (float)rt_ui->dist_m * (float)k / 95.0f, &e);
                    m->detail_prof[m->detail_prof_n++] = (int16_t)(e * 10.0f);
                }
                m->detail_n_climbs = rt_ui->nclimbs < 8 ? rt_ui->nclimbs : 8;
                for (int c = 0; c < m->detail_n_climbs; c++) {
                    m->detail_climbs[c].start_m = rt_ui->climbs[c].start_m;
                    m->detail_climbs[c].len_m = rt_ui->climbs[c].length_m;
                }
                push(SCR_ROUTE_DETAIL);
            }
        }
        break;
    case SCR_ROUTE_DETAIL:
        if (cc_cmd_load_route(m->routes[m->detail_route].id) == 0) reset_to(SCR_READY);
        break;
    case SCR_HISTORY:
        if (m->focus < m->n_rides) {
            cc_ride_meta_t mt;
            if (cc_ride_meta_read(U.fs, m->rides[m->focus].ride_id, &mt)) {
                m->summary = mt.sum;
                m->summary_ride = m->rides[m->focus];
                m->summary_uploaded = (mt.flags & CC_RIDE_UPLOADED) != 0;
                m->summary_from_history = 1;
                push(SCR_SUMMARY);
            }
        }
        break;
    case SCR_SUMMARY:
        m->summary_from_history = 0;
        pop();
        break;
    case SCR_SENSORS: {
        int f = m->focus;
        if (f == 0 && m->pm_paired) push(SCR_POWER_METER);
        else if ((f == 1 || (f == 0 && !m->pm_paired)) && m->hr_paired) push(SCR_HEART_RATE);
        else {
            push(SCR_ADD_SENSOR);
            m->scanning = 1;
            if (U.hooks.sensor_scan_list) m->n_scan = 0; /* filled by the scan (ui_app_step starts it) */
        }
        break;
    }
    case SCR_ADD_SENSOR:
        if (m->focus < m->n_scan) {
            if (U.hooks.sensor_pair) U.hooks.sensor_pair(&m->scan[m->focus]); /* saved once subscribed */
            else if (m->scan[m->focus].kind == 1) m->pm_paired = 1;
            else m->hr_paired = 1;
            pop();
        }
        break;
    case SCR_POWER_METER:
        if (m->focus == 1) { m->forget_kind = 1; open_confirm(UI_CONFIRM_FORGET_SENSOR); break; }
        if (m->cal == UI_CAL_IDLE || m->cal == UI_CAL_FAILED) {
            if (U.has_hooks && U.hooks.cal_start) U.hooks.cal_start(U.now);
            else { m->cal = UI_CAL_RUNNING; U.cal_t = 3; }
        } else if (m->cal == UI_CAL_DONE) {
            if (U.has_hooks && U.hooks.cal_ack) U.hooks.cal_ack();
            else m->cal = UI_CAL_IDLE;
            pop();
        }
        break;
    case SCR_HEART_RATE:
        if (m->focus == 1) { m->forget_kind = 2; open_confirm(UI_CONFIRM_FORGET_SENSOR); }
        break;
    case SCR_PROFILE:
        if (m->focus < 4) {
            static const int d[4] = {-5, -1, 1, 5};
            int v = m->ftp_edit + d[m->focus];
            m->ftp_edit = v < CC_FTP_MIN_W ? CC_FTP_MIN_W : v > CC_FTP_MAX_W ? CC_FTP_MAX_W : v;
        } else {
            if (m->ftp_edit != m->v.ftp) cc_cmd_set_ftp((uint16_t)m->ftp_edit);
            pop();
        }
        break;
    case SCR_PHONE:
        if (m->phone == UI_PHONE_NOT_PAIRED) {
            if (U.has_hooks && U.hooks.pair_window) {
                U.hooks.pair_window(1);
            } else {
                m->phone = UI_PHONE_PAIRING;
                m->passkey = (uint32_t)(100000 + (U.now * 7919u) % 900000);
                U.pair_until = U.now + 60000;
            }
        } else if (m->phone == UI_PHONE_CONNECTED && m->focus == 1) {
            open_confirm(UI_CONFIRM_FORGET_PHONE);
        }
        break;
    case SCR_SETTINGS: {
        cc_settings_t s = m->v.settings;
        switch (m->focus) {
        case 0: s.brightness = (uint8_t)((s.brightness + 1) % CC_BRIGHT_N); break;
        case 1: s.screen_off = (uint8_t)((s.screen_off + 1) % CC_SCREENOFF_N); break;
        case 2: s.units = (uint8_t)((s.units + 1) % CC_UNITS_N); break;
        case 3: s.autopause = !s.autopause; break;
        default: break;
        }
        cc_cmd_settings(&s);
        break;
    }
    case SCR_CONFIRM:
        if (m->focus == 1) {
            if (m->confirm == UI_CONFIRM_FORGET_PHONE) {
                if (U.has_hooks && U.hooks.erase_bond) U.hooks.erase_bond();
                m->phone = UI_PHONE_NOT_PAIRED;
                cc_on_phone(0);
            } else {
                int kind = U.depth >= 2 && U.stack[U.depth - 2] == SCR_POWER_METER ? 1 : 2;
                if (U.has_hooks && U.hooks.sensor_forget) U.hooks.sensor_forget(kind);
                if (kind == 1) m->pm_paired = 0;
                else m->hr_paired = 0;
            }
            pop();
            pop();
        } else {
            pop();
        }
        break;
    default:
        break;
    }
}

static void back(void)
{
    ui_screen_id_t s = ui_app_top();
    U.dirty = 1;
    if (s == SCR_READY) { ui_app_refresh_lists(); push(SCR_MENU); return; } /* left from Ready opens the Menu */
    if (s == SCR_PHONE && U.m.phone == UI_PHONE_PAIRING) {
        if (U.has_hooks && U.hooks.pair_window) U.hooks.pair_window(0);
        U.m.phone = UI_PHONE_NOT_PAIRED;
        return;
    }
    if (s == SCR_POWER_METER && U.has_hooks && U.hooks.cal_ack) U.hooks.cal_ack();
    if (s == SCR_PROFILE) U.m.ftp_edit = U.m.v.ftp; /* BACK discards */
    if (s == SCR_SUMMARY) U.m.summary_from_history = 0;
    pop();
}

/* Gestures the core hands to the UI */
static void gestures(void)
{
    cc_gesture_t g;
    while (cc_poll_gesture(&g)) {
        U.had_input = 1;
        U.last_input_ms = U.now;
        U.dirty = 1;
        const cc_view_t *v = cc_view();
        if (v->state == CC_RS_RECORDING) {
            if (g.btn == CC_BTN_L) {
                int n = v->ring_n ? v->ring_n : 1;
                U.m.page = g.hold ? (U.m.page + n - 1) % n : (U.m.page + 1) % n;
            }
            continue;
        }
        if (g.hold) continue;
        if (U.blank) { U.blank = 0; continue; }
        switch (g.btn) {
        case CC_BTN_PWR:
            if (ui_app_top() == SCR_READY) U.blank = !U.blank; /* short press blanks or wakes when stopped */
            else focus_next();
            break;
        case CC_BTN_L: back(); break;
        case CC_BTN_R: activate(); break;
        default: break;
        }
    }
}

static int is_scrollable_list(ui_screen_id_t s)
{
    return s == SCR_MENU || s == SCR_SETTINGS || s == SCR_WORKOUTS || s == SCR_ROUTES || s == SCR_HISTORY;
}

static void foot_press(cc_btn_t b)
{
    U.tap_pending = 0;
    if (U.touch_dragged) return;
    U.had_input = 1;
    U.last_input_ms = U.now;
    U.dirty = 1;
    if (U.blank) { U.blank = 0; return; }
    cc_on_button(U.now, b, 1);
    cc_on_button(U.now, b, 0);
    gestures();
}

static void on_foot_left(lv_event_t *e)
{
    (void)e;
    foot_press(CC_BTN_L);
}

static void on_foot_right(lv_event_t *e)
{
    (void)e;
    foot_press(CC_BTN_R);
}

static void on_click(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    U.tap_pending = 0;
    tap_note(U.touch_dragged ? "click-ignored-drag item/screen" : "click item/screen", idx, (int)ui_app_top());
    if (U.touch_dragged) return;
    U.had_input = 1;
    U.last_input_ms = U.now;
    U.dirty = 1;
    if (U.blank) { U.blank = 0; return; }
    const cc_view_t *v = cc_view();
    if (v->state == CC_RS_PAUSE_MENU) { cc_cmd_pause_menu_select(U.now, idx); return; }
    if (v->state == CC_RS_CONFIRM_DISCARD) { cc_cmd_confirm_select(U.now, idx == 0 ? CC_CF_KEEP : CC_CF_DISCARD); return; }
    if (v->state == CC_RS_PAUSED) {
        if (idx == 0) { cc_on_button(U.now, CC_BTN_L, 1); cc_on_button(U.now, CC_BTN_L, 0); }
        else { cc_on_button(U.now, CC_BTN_R, 1); cc_on_button(U.now, CC_BTN_R, 0); }
        return;
    }
    ui_screen_id_t s = ui_app_top();
    /* the header back button is the first item on screens that have one */
    int has_back = s == SCR_WORKOUT_DETAIL || s == SCR_ADD_SENSOR || s == SCR_POWER_METER || s == SCR_PROFILE ||
                   s == SCR_ROUTE_DETAIL || s == SCR_CONFIRM || s == SCR_HEART_RATE || s == SCR_PHONE;
    if (has_back && idx == 0) { back(); return; }
    U.m.focus = idx - has_back + (is_scrollable_list(s) ? U.m.scroll : 0);
    if (s == SCR_CONFIRM) U.m.focus = idx == 1 ? 0 : 1; /* safe button is first in the order */
    activate();
}

/* Fallback coordinate dispatcher if a screen rebuild during a tap cleared LVGL's active press object. */
static void tap_xy(int16_t x, int16_t y)
{
    tap_note("fallback-by-position screen/state", (int)ui_app_top(), (int)cc_view()->state);
    U.had_input = 1;
    U.last_input_ms = U.now;
    U.dirty = 1;
    if (U.blank) { U.blank = 0; return; }
    const cc_view_t *v = cc_view();
    if (v->state == CC_RS_RECORDING) return;
    if (y >= 282) {
        if (x < 108) foot_press(CC_BTN_L);
        else if (x >= 132) foot_press(CC_BTN_R);
        return;
    }
    if (v->state == CC_RS_PAUSED) return;
    if (v->state == CC_RS_PAUSE_MENU) {
        if (y >= 58 && y < 58 + 3 * 46) cc_cmd_pause_menu_select(U.now, (y - 58) / 46);
        return;
    }
    if (v->state == CC_RS_CONFIRM_DISCARD) {
        if (y >= 168 && y < 222) cc_cmd_confirm_select(U.now, CC_CF_DISCARD);
        else if (y >= 226 && y < 280) cc_cmd_confirm_select(U.now, CC_CF_KEEP);
        return;
    }
    if (v->state == CC_RS_SUMMARY) return;
    ui_screen_id_t s = ui_app_top();
    int has_back = s == SCR_WORKOUT_DETAIL || s == SCR_ADD_SENSOR || s == SCR_POWER_METER || s == SCR_PROFILE ||
                   s == SCR_ROUTE_DETAIL || s == SCR_CONFIRM || s == SCR_HEART_RATE || s == SCR_PHONE;
    if (has_back && x < 56 && y >= 22 && y < 66) { back(); return; }
    int has_kebab = s == SCR_POWER_METER || s == SCR_HEART_RATE || (s == SCR_PHONE && U.m.phone == UI_PHONE_CONNECTED);
    if (has_kebab && x >= 184 && y >= 22 && y < 66) { U.m.focus = 1; activate(); return; }
    if (is_scrollable_list(s) || s == SCR_SENSORS) {
        if (y >= 58 && y < 282) {
            int idx = (y - 58) / 46 + (is_scrollable_list(s) ? U.m.scroll : 0);
            if (idx >= 0 && idx < list_len(s)) { U.m.focus = idx; activate(); }
        }
        return;
    }
    if (s == SCR_ADD_SENSOR && y >= 150 && y < 150 + 3 * 46) {
        int idx = (y - 150) / 46;
        if (idx < U.m.n_scan) { U.m.focus = idx; activate(); }
        return;
    }
    if (s == SCR_PROFILE && y >= 170 && y < 234) {
        int col = (x - 4) / 58;
        if (col >= 0 && col < 4) { U.m.focus = col; activate(); }
        return;
    }
    if (s == SCR_CONFIRM) {
        if (y >= 168 && y < 222) { U.m.focus = 1; activate(); }
        else if (y >= 226 && y < 280) { U.m.focus = 0; activate(); }
        return;
    }
    if ((s == SCR_WORKOUT_DETAIL || s == SCR_ROUTE_DETAIL || s == SCR_POWER_METER) && y >= 225 && y < 280) {
        U.m.focus = 0;
        activate();
        return;
    }
    if (s == SCR_PHONE && U.m.phone == UI_PHONE_NOT_PAIRED && y >= 150 && y < 204) {
        U.m.focus = 0;
        activate();
    }
}

void ui_app_touch(int down, int16_t x, int16_t y)
{
    if (down) {
        U.last_input_ms = U.now;
        if (!U.touch_down) {
            U.touch_down = 1;
            U.touch_since = U.now;
            U.touch_dragged = 0;
            U.touch_ate = 0;
            U.tap_pending = 0;
            U.touch_x0 = x;
            U.touch_y0 = y;
            U.touch_last_y = y;
            if (U.blank) {
                U.blank = 0;
                U.dirty = 1;
                U.touch_ate = 1;
                U.touch_dragged = 1;
            }
            return;
        }
        if (U.touch_ate) return;
        int dx = (int)x - (int)U.touch_x0;
        int dy0 = (int)y - (int)U.touch_y0;
        if (dx < -18 || dx > 18 || dy0 < -18 || dy0 > 18)
            U.touch_dragged = 1;
        ui_screen_id_t s = ui_app_visible();
        if (cc_view()->state == CC_RS_READY && is_scrollable_list(s)) {
            int n = list_len(s);
            int max_sc = n > 5 ? n - 5 : 0;
            int dy = (int)y - (int)U.touch_last_y;
            if (dy <= -28 || dy >= 28) {
                int step = dy <= -28 ? 1 : -1;
                int next_sc = U.m.scroll + step;
                if (next_sc < 0) next_sc = 0;
                if (next_sc > max_sc) next_sc = max_sc;
                U.touch_last_y = y;
                U.touch_dragged = 1;
                if (next_sc != U.m.scroll) {
                    U.m.scroll = next_sc;
                    if (U.m.focus < U.m.scroll) U.m.focus = U.m.scroll;
                    if (U.m.focus >= U.m.scroll + 5) U.m.focus = U.m.scroll + 4;
                    U.dirty = 1;
                }
            }
        }
    } else if (U.touch_down) {
        U.touch_down = 0;
        if (U.touch_ate) return;
        /* No swipe page change while recording. Spec 4 gives touch no job while riding ("Not
           required"; the left button changes the page) and lists touch only for the pause menu and
           Confirm while paused. A page that changes under a wet glove or a raindrop is worse than one
           that cannot be swiped, so a horizontal drag here does nothing, and a tap is ignored by
           tap_xy while recording. */
        if (!U.touch_dragged) {
            U.tap_pending = 1;
            U.tap_x = x;
            U.tap_y = y;
        } else {
            tap_note("release-after-drag dx/dy", (int)x - (int)U.touch_x0, (int)y - (int)U.touch_y0);
        }
    }
}

void ui_app_post_touch(void)
{
    if (U.tap_pending) {
        U.tap_pending = 0;
        if (!U.touch_dragged)
            tap_xy(U.tap_x, U.tap_y);
    }
}

void ui_app_init(hal_fs_t *fs, uint32_t now_ms, const ui_app_hooks_t *hooks)
{
    lv_obj_t *old_scr = U.scr;
    lv_group_t *old_grp = U.grp;
    memset(&U, 0, sizeof U);
    U.scr = old_scr;
    U.grp = old_grp;
    U.fs = fs;
    U.now = now_ms;
    U.last_input_ms = now_ms;
    U.last_state = 0xFF;
    if (hooks) {
        U.hooks = *hooks;
        U.has_hooks = 1;
    }
    U.m.phone = UI_PHONE_NOT_PAIRED;
    reset_to(SCR_READY);
    ui_app_refresh_lists();
}

void ui_app_step(uint32_t now_ms)
{
    uint32_t prev_sec = U.now / 1000u;
    int prev_focus = cc_view()->focus;
    uint32_t prev_banner_id = cc_view()->banner.id;
    U.now = now_ms;
    gestures();
    const cc_view_t *v = cc_view();
    if (v->state != U.last_state) {
        if (v->state == CC_RS_READY && U.last_state != 0xFF) { reset_to(SCR_READY); ui_app_refresh_lists(); }
        if (v->state == CC_RS_RECORDING && U.last_state == CC_RS_READY) U.m.page = 0;
        U.last_state = v->state;
        U.last_input_ms = now_ms;
        U.dirty = 1;
    }
    if (v->focus != prev_focus || v->banner.id != prev_banner_id || v->hold.active)
        U.dirty = 1;
    /* Screen-off timeout while stopped (spec 10.1): never blanks while recording or paused. */
    if (v->state == CC_RS_READY || v->state == CC_RS_SUMMARY) {
        static const uint32_t off_ms[CC_SCREENOFF_N] = {0, 30000u, 60000u, 300000u};
        uint32_t lim = off_ms[v->settings.screen_off % CC_SCREENOFF_N];
        if (lim && !U.blank && now_ms - U.last_input_ms >= lim) {
            U.blank = 1;
            U.dirty = 1;
        }
    } else if (U.blank) {
        U.blank = 0;
        U.dirty = 1;
    }
    if (now_ms / 1000u != prev_sec) {
        if (!U.has_hooks) {
            if (U.cal_t > 0 && --U.cal_t == 0) U.m.cal = UI_CAL_DONE;
            if (U.m.phone == UI_PHONE_PAIRING && now_ms >= U.pair_until) U.m.phone = UI_PHONE_NOT_PAIRED;
        }
        U.redraw_owed = 1;
    }
    /* The once-a-second redraw is what moves the timer, speed and power on screen. It is held back
     * while a finger is on the panel, because rebuilding U.scr mid-tap makes LVGL lose the pressed
     * object. But a phantom touch (rain, a wet glove, a panel that reports "down" for ever) must not
     * freeze the display, so the hold-back is limited:
     *   - never while a ride is recording or paused: those screens carry live numbers and touch has
     *     no job on them except the pause menu and Confirm, whose taps tap_xy recovers by position;
     *   - elsewhere, for at most UI_TOUCH_REDRAW_HOLD_MS of continuous touch, which is longer than
     *     any real tap or list drag. */
    if (U.redraw_owed) {
        int riding = v->state == CC_RS_RECORDING || v->state == CC_RS_PAUSED;
        if (!U.touch_down || riding || now_ms - U.touch_since >= UI_TOUCH_REDRAW_HOLD_MS) {
            U.redraw_owed = 0;
            U.dirty = 1;
        }
    }
    /* The sensor scan runs exactly while Add sensor is on top with no ride in progress, however the
       screen was reached or left (BACK, PAIR, a tap, a reset to Ready). */
    int want_scan = v->state == CC_RS_READY && ui_app_top() == SCR_ADD_SENSOR;
    if (U.hooks.sensor_scan && want_scan != U.scan_on) {
        U.scan_on = want_scan;
        U.hooks.sensor_scan(want_scan);
    }
    cc_tick(now_ms); /* commands issued by the UI this step show in the view now */
    fill_model();
}
