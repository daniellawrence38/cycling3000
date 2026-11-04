/* The live simulator: a screen manager on top of the core (spec 4 and 6), the scripted walk that is
 * WP4's acceptance test, and the SDL window. The UI holds screen navigation only; ride state is the
 * core's (brief 2). */
#define _POSIX_C_SOURCE 200809L
#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cc700/nav.h"
#include "cc700/provisional.h"
#include "cc700/store.h"
#include "dirfs.h"
#include "player.h"

#if CC_LV_USE_SDL
#include <SDL2/SDL.h>
#endif

/* ------------------------------------------------------------------ state */
typedef struct {
    hal_fs_t *fs;
    ui_model_t m;
    ui_screen_id_t stack[12];
    int depth;
    int blank;
    uint32_t now;
    int cal_t;               /* simulated calibration: seconds left */
    uint32_t pair_until;     /* simulated pairing window */
    lv_obj_t *scr;
    lv_group_t *grp;
    char last_shot[64];
    /* perf */
    int perf;
    double lat[4096];
    int n_lat;
    double pending_input;
} app_t;

static app_t A;
static cc_route_t rt_ui; /* the UI's own copy of the loaded route, for the map and profile strip */
static int rt_ui_ok;
static uint8_t rt_ui_id[8];

static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static ui_screen_id_t top(void) { return A.stack[A.depth - 1]; }
static void push(ui_screen_id_t s) { if (A.depth < 12) A.stack[A.depth++] = s; A.m.focus = 0; A.m.scroll = 0; }
static void pop(void) { if (A.depth > 1) A.depth--; A.m.focus = 0; A.m.scroll = 0; }
static void reset_to(ui_screen_id_t s) { A.depth = 0; push(s); }

/* ------------------------------------------------------------------ model from the store */
static int obj_cb(void *ctx, const hal_dirent_t *e)
{
    (void)ctx;
    size_t n = strlen(e->name);
    if (n != 20) return 0;
    static uint8_t buf[CC_RT_MAX_BYTES];
    char p[96];
    snprintf(p, sizeof p, "objects/%s", e->name);
    hal_file_t f = A.fs->open(A.fs, p, HAL_FS_READ);
    if (f < 0) return 0;
    int32_t len = A.fs->read(A.fs, f, buf, sizeof buf);
    A.fs->close(A.fs, f);
    if (!strcmp(e->name + 16, ".cw1") && A.m.n_workouts < UI_MAX_LIST) {
        static cc_workout_t w;
        if (cc_cw1_decode(buf, (size_t)len, &w) == CC_CW1_OK) {
            ui_wo_item_t *it = &A.m.workouts[A.m.n_workouts++];
            snprintf(it->name, sizeof it->name, "%s", w.name);
            it->secs = w.total_secs;
            it->steps = w.nsteps;
            memcpy(it->id, w.id, 8);
        }
    } else if (!strcmp(e->name + 16, ".cr1") && A.m.n_routes < UI_MAX_LIST) {
        static cc_route_t r;
        if (cc_cr1_decode(buf, (size_t)len, &r) == CC_CR1_OK) {
            ui_rt_item_t *it = &A.m.routes[A.m.n_routes++];
            snprintf(it->name, sizeof it->name, "%s", r.name);
            it->dist_m = r.dist_m;
            it->ascent_m = r.ascent_m;
            it->climbs = r.nclimbs;
            it->has_profile = r.nprof > 0;
            memcpy(it->id, r.id, 8);
        }
    }
    return 0;
}

static int load_object(int kind, const uint8_t id[8], uint8_t *buf, size_t cap)
{
    char p[64];
    cc_object_path(p, sizeof p, id, kind);
    hal_file_t f = A.fs->open(A.fs, p, HAL_FS_READ);
    if (f < 0) return -1;
    int32_t n = A.fs->read(A.fs, f, buf, (uint32_t)cap);
    A.fs->close(A.fs, f);
    return n;
}

static void refresh_lists(void)
{
    A.m.n_workouts = A.m.n_routes = 0;
    A.fs->list(A.fs, "objects", obj_cb, NULL);
    static uint32_t ids[300];
    int n = cc_rides_list(A.fs, ids, 300);
    A.m.rides_total = n;
    A.m.n_rides = 0;
    int tz = A.m.v.settings.tz_min * 60;
    for (int i = n - 1; i >= 0 && A.m.n_rides < UI_MAX_LIST; i--) {
        cc_ride_meta_t mt;
        if (!cc_ride_meta_read(A.fs, ids[i], &mt)) continue;
        ui_ride_item_t *it = &A.m.rides[A.m.n_rides++];
        it->ride_id = mt.ride_id;
        it->start_local = mt.sum.start_ts + (uint32_t)tz;
        it->dist_m = mt.sum.distance_m;
        it->timer_s = mt.sum.timer_s;
        it->avg_power = mt.sum.avg_power;
        it->has_power = mt.sum.has_power;
        it->uploaded = (mt.flags & CC_RIDE_UPLOADED) != 0;
    }
}

/* Map points around the rider and the profile strip of the active climb, from the UI's route copy. */
static void nav_extras(void)
{
    ui_model_t *m = &A.m;
    const cc_view_t *v = &m->v;
    m->map_n = 0;
    m->climb_prof_n = 0;
    if (!v->route_loaded) { rt_ui_ok = 0; return; }
    if (!rt_ui_ok || memcmp(rt_ui_id, v->settings.route_id, 8)) {
        static uint8_t buf[CC_RT_MAX_BYTES];
        int n = load_object(2, v->settings.route_id, buf, sizeof buf);
        rt_ui_ok = n > 0 && cc_cr1_decode(buf, (size_t)n, &rt_ui) == CC_CR1_OK;
        memcpy(rt_ui_id, v->settings.route_id, 8);
    }
    if (!rt_ui_ok) return;
    float rx, ry;
    cc_route_project(&rt_ui, v->lat_e7, v->lon_e7, &rx, &ry);
    float lim = (float)(v->nav.map_scale_m ? v->nav.map_scale_m : 400) * 1.2f;
    m->map_ridden = 0;
    for (int i = 0; i < rt_ui.npts && m->map_n < 128; i++) {
        float dx = rt_ui.x[i] - rx, dy = rt_ui.y[i] - ry;
        if (fabsf(dx) > lim || fabsf(dy) > lim) {
            if (m->map_n) break; /* leaving the window after being inside it */
            continue;
        }
        m->map_x[m->map_n] = dx;
        m->map_y[m->map_n] = dy;
        if (rt_ui.cum[i] <= v->nav.route_pos_m) m->map_ridden = m->map_n;
        m->map_n++;
    }
    m->course_deg = v->course_deg;
    int ci = v->nav.climb_idx;
    if (v->nav.climb_show && ci >= 0 && ci < rt_ui.nclimbs) {
        const cc_climb_t *c = &rt_ui.climbs[ci];
        for (int k = 0; k < 24; k++) {
            float e = 0;
            cc_route_profile_at(&rt_ui, (float)c->start_m + (float)c->length_m * (float)k / 23.0f, &e);
            m->climb_prof[k] = e;
        }
        m->climb_prof_n = 24;
        float p = (v->nav.route_pos_m - (float)c->start_m) / (float)(c->length_m ? c->length_m : 1);
        m->climb_pos = p < 0 ? 0 : p > 1 ? 1 : p;
    }
}

static void fill_model(void)
{
    ui_model_t *m = &A.m;
    m->v = *cc_view();
    const cc_view_t *v = &m->v;
    if (v->utc_known) {
        uint32_t loc = v->utc + (uint32_t)(v->tz_min * 60);
        m->clock_min = (int)((loc / 60) % 1440);
    } else {
        m->clock_min = -1;
    }
    m->pm_paired = 1;
    m->hr_paired = 1;
    if (!m->pm_name[0]) {
        snprintf(m->pm_name, sizeof m->pm_name, "PM500");
        snprintf(m->pm_id4, sizeof m->pm_id4, "7C2E");
        snprintf(m->hr_id4, sizeof m->hr_id4, "41B9");
    }
    m->paused_s = v->paused_s;
    if (m->phone != UI_PHONE_PAIRING)
        m->phone = v->phone_connected ? UI_PHONE_CONNECTED : m->phone;
    nav_extras();
}

/* Which screen shows: the core's ride state wins over the UI stack while a ride is active. */
static ui_screen_id_t visible(void)
{
    const cc_view_t *v = &A.m.v;
    switch (v->state) {
    case CC_RS_RECORDING: {
        if (A.m.page >= v->ring_n) A.m.page = 0;
        cc_page_t p = v->ring[A.m.page];
        if (p == CC_PAGE_WORKOUT) return SCR_WORKOUT;
        if (p == CC_PAGE_CLIMB) return SCR_CLIMB;
        if (p == CC_PAGE_NAV) {
            if (!v->nav.have_pos) { A.m.empty = UI_EMPTY_GPSWAIT; return SCR_EMPTY; }
            return SCR_NAV;
        }
        return SCR_MAIN;
    }
    case CC_RS_PAUSED: return SCR_PAUSED;
    case CC_RS_PAUSE_MENU: return SCR_PAUSE_MENU;
    case CC_RS_CONFIRM_DISCARD: A.m.confirm = UI_CONFIRM_DISCARD; return SCR_CONFIRM;
    case CC_RS_SUMMARY: return SCR_SUMMARY;
    default: return top();
    }
}

static void summary_from_view(void)
{
    const cc_view_t *v = &A.m.v;
    A.m.summary = v->summary.sum;
    A.m.summary_ride.start_local = v->summary.sum.start_ts + (uint32_t)(v->tz_min * 60);
    A.m.summary_uploaded = (v->summary.flags & CC_RIDE_UPLOADED) != 0;
}

/* ------------------------------------------------------------------ rendering */
static void on_click(lv_event_t *e);

static void render(void)
{
    fill_model();
    ui_screen_id_t id = visible();
    if (id == SCR_SUMMARY && !A.m.summary_from_history) summary_from_view();
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_group_t *g = lv_group_create();
    ui_focus_group = g;
    if (A.blank) {
        lv_obj_set_style_bg_color(scr, lv_color_hex(0), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    } else {
        ui_build(id, scr, &A.m);
        int ride_page = id == SCR_MAIN || id == SCR_WORKOUT || id == SCR_CLIMB || id == SCR_NAV || id == SCR_EMPTY;
        if (ride_page && A.m.v.state == CC_RS_RECORDING) ui_overlays(scr, &A.m);
    }
    uint32_t n = lv_group_get_obj_count(g);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *o = lv_group_get_obj_by_index(g, i);
        lv_obj_add_event_cb(o, on_click, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    lv_screen_load(scr);
    if (A.scr) lv_obj_delete(A.scr);
    if (A.grp) lv_group_delete(A.grp);
    A.scr = scr;
    A.grp = g;
}

/* ------------------------------------------------------------------ actions */
static int list_len(ui_screen_id_t s)
{
    switch (s) {
    case SCR_MENU: return 7;
    case SCR_WORKOUTS: return A.m.n_workouts;
    case SCR_ROUTES: return A.m.n_routes;
    case SCR_HISTORY: return A.m.n_rides;
    case SCR_SETTINGS: return 6;
    case SCR_SENSORS: return 2 + !(A.m.pm_paired && A.m.hr_paired);
    case SCR_ADD_SENSOR: return A.m.n_scan;
    case SCR_PROFILE: return 5;
    case SCR_CONFIRM: return 2;
    case SCR_POWER_METER: case SCR_HEART_RATE: return 2; /* the button (or nothing), then the kebab */
    case SCR_PHONE: return A.m.phone == UI_PHONE_CONNECTED ? 2 : 1;
    default: return 0;
    }
}

static void focus_next(void)
{
    ui_screen_id_t s = top();
    int n = list_len(s);
    if (n <= 0) return;
    if (s == SCR_CONFIRM) { A.m.focus = A.m.focus ? 0 : 1; return; }
    A.m.focus = (A.m.focus + 1) % n;
    if (A.m.focus < A.m.scroll) A.m.scroll = A.m.focus;
    if (A.m.focus >= A.m.scroll + 5) A.m.scroll = A.m.focus - 4;
}

static void open_confirm(ui_confirm_kind_t k)
{
    A.m.confirm = k;
    push(SCR_CONFIRM);
    A.m.focus = 0; /* the safe button */
}

static void activate(void)
{
    ui_model_t *m = &A.m;
    switch (top()) {
    case SCR_READY:
        cc_cmd_start(A.now);
        m->page = 0;
        break;
    case SCR_MENU: {
        static const ui_screen_id_t to[7] = {SCR_WORKOUTS, SCR_ROUTES, SCR_SENSORS, SCR_PROFILE, SCR_HISTORY, SCR_PHONE, SCR_SETTINGS};
        int f = m->focus;
        push(to[f]);
        if (to[f] == SCR_PROFILE) m->ftp_edit = m->v.ftp;
        break;
    }
    case SCR_WORKOUTS:
        if (m->focus < m->n_workouts) {
            static uint8_t buf[CC_WO_MAX_BYTES];
            int n = load_object(1, m->workouts[m->focus].id, buf, sizeof buf);
            if (n > 0 && cc_cw1_decode(buf, (size_t)n, &m->detail_wo) == CC_CW1_OK) push(SCR_WORKOUT_DETAIL);
        }
        break;
    case SCR_WORKOUT_DETAIL:
        if (cc_cmd_load_workout(m->detail_wo.id) == 0) reset_to(SCR_READY);
        break;
    case SCR_ROUTES:
        if (m->focus < m->n_routes) {
            static uint8_t buf[CC_RT_MAX_BYTES];
            static cc_route_t r;
            int n = load_object(2, m->routes[m->focus].id, buf, sizeof buf);
            if (n > 0 && cc_cr1_decode(buf, (size_t)n, &r) == CC_CR1_OK) {
                m->detail_route = m->focus;
                m->detail_prof_len_m = r.dist_m;
                m->detail_prof_n = 0;
                for (int k = 0; k < 96 && r.nprof; k++) {
                    float e = 0;
                    cc_route_profile_at(&r, (float)r.dist_m * (float)k / 95.0f, &e);
                    m->detail_prof[m->detail_prof_n++] = (int16_t)(e * 10.0f);
                }
                m->detail_n_climbs = r.nclimbs < 8 ? r.nclimbs : 8;
                for (int c = 0; c < m->detail_n_climbs; c++) {
                    m->detail_climbs[c].start_m = r.climbs[c].start_m;
                    m->detail_climbs[c].len_m = r.climbs[c].length_m;
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
            if (cc_ride_meta_read(A.fs, m->rides[m->focus].ride_id, &mt)) {
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
        else { push(SCR_ADD_SENSOR); m->scanning = 1; }
        break;
    }
    case SCR_ADD_SENSOR:
        if (m->focus < m->n_scan) {
            if (m->scan[m->focus].kind == 1) m->pm_paired = 1; else m->hr_paired = 1;
            pop();
        }
        break;
    case SCR_POWER_METER:
        if (m->focus == 1) { open_confirm(UI_CONFIRM_FORGET_SENSOR); break; }
        if (m->cal == UI_CAL_IDLE || m->cal == UI_CAL_FAILED) { m->cal = UI_CAL_RUNNING; A.cal_t = 3; }
        else if (m->cal == UI_CAL_DONE) { m->cal = UI_CAL_IDLE; pop(); }
        break;
    case SCR_HEART_RATE:
        if (m->focus == 1) open_confirm(UI_CONFIRM_FORGET_SENSOR);
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
            m->phone = UI_PHONE_PAIRING;
            m->passkey = (uint32_t)(100000 + (A.now * 7919u) % 900000);
            A.pair_until = A.now + 60000;
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
            if (m->confirm == UI_CONFIRM_FORGET_PHONE) { m->phone = UI_PHONE_NOT_PAIRED; cc_on_phone(0); }
            else if (A.depth >= 2 && A.stack[A.depth - 2] == SCR_POWER_METER) m->pm_paired = 0;
            else m->hr_paired = 0;
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
    ui_screen_id_t s = top();
    if (s == SCR_READY) { push(SCR_MENU); return; } /* left from Ready opens the Menu */
    if (s == SCR_PHONE && A.m.phone == UI_PHONE_PAIRING) { A.m.phone = UI_PHONE_NOT_PAIRED; return; }
    if (s == SCR_PROFILE) A.m.ftp_edit = A.m.v.ftp; /* BACK discards */
    if (s == SCR_SUMMARY) A.m.summary_from_history = 0;
    pop();
}

/* Gestures the core hands to the UI */
static void gestures(void)
{
    cc_gesture_t g;
    while (cc_poll_gesture(&g)) {
        A.pending_input = wall_ms();
        const cc_view_t *v = cc_view();
        if (v->state == CC_RS_RECORDING) {
            if (g.btn == CC_BTN_L) {
                int n = v->ring_n ? v->ring_n : 1;
                A.m.page = g.hold ? (A.m.page + n - 1) % n : (A.m.page + 1) % n;
            }
            continue;
        }
        if (g.hold) continue;
        if (A.blank && g.btn != CC_BTN_PWR) { A.blank = 0; continue; }
        switch (g.btn) {
        case CC_BTN_PWR:
            if (top() == SCR_READY) A.blank = !A.blank; /* short press blanks or wakes when stopped */
            else focus_next();
            break;
        case CC_BTN_L: back(); break;
        case CC_BTN_R: activate(); break;
        default: break;
        }
    }
}

static void on_click(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    A.pending_input = wall_ms();
    const cc_view_t *v = cc_view();
    if (v->state == CC_RS_PAUSE_MENU) { cc_cmd_pause_menu_select(A.now, idx); return; }
    if (v->state == CC_RS_CONFIRM_DISCARD) { cc_cmd_confirm_select(A.now, idx == 0 ? CC_CF_KEEP : CC_CF_DISCARD); return; }
    if (v->state == CC_RS_PAUSED) { if (idx == 0) cc_on_button(A.now, CC_BTN_L, 1), cc_on_button(A.now, CC_BTN_L, 0); else cc_on_button(A.now, CC_BTN_R, 1), cc_on_button(A.now, CC_BTN_R, 0); return; }
    ui_screen_id_t s = top();
    /* the header back button is the first item on screens that have one */
    int has_back = s == SCR_WORKOUT_DETAIL || s == SCR_ADD_SENSOR || s == SCR_POWER_METER || s == SCR_PROFILE ||
                   s == SCR_ROUTE_DETAIL || s == SCR_CONFIRM || s == SCR_HEART_RATE || s == SCR_PHONE;
    if (has_back && idx == 0) { back(); return; }
    A.m.focus = idx - has_back + (s == SCR_MENU || s == SCR_WORKOUTS || s == SCR_ROUTES || s == SCR_HISTORY || s == SCR_SETTINGS ? A.m.scroll : 0);
    if (s == SCR_CONFIRM) A.m.focus = idx == 1 ? 0 : 1; /* safe button is first in the order */
    activate();
}

/* One 100 ms step of the simulator */
static uint32_t last_state = 0xFF;
static void step(void)
{
    A.now += 100;
    cc_tick(A.now);
    gestures();
    const cc_view_t *v = cc_view();
    if (v->state != last_state) {
        if (v->state == CC_RS_READY && last_state != 0xFF) { reset_to(SCR_READY); refresh_lists(); }
        if (v->state == CC_RS_RECORDING && last_state == CC_RS_READY) A.m.page = 0;
        last_state = v->state;
    }
    if (A.now % 1000 == 0) {
        if (A.cal_t > 0 && --A.cal_t == 0) A.m.cal = UI_CAL_DONE;
        if (A.m.phone == UI_PHONE_PAIRING && A.now >= A.pair_until) A.m.phone = UI_PHONE_NOT_PAIRED;
    }
    cc_tick(A.now); /* commands issued by the UI this step show in the view now */
    fill_model();
}

static void redraw(void)
{
    render();
    lv_refr_now(NULL);
    if (A.perf && A.pending_input > 0 && A.n_lat < 4096) {
        A.lat[A.n_lat++] = wall_ms() - A.pending_input;
        A.pending_input = 0;
    }
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void perf_report(void)
{
    if (!A.perf || !A.n_lat) return;
    qsort(A.lat, (size_t)A.n_lat, sizeof A.lat[0], cmpd);
    printf("perf: input to redraw complete, %d inputs (simulator on this PC, not the unit)\n", A.n_lat);
    int ps[] = {50, 90, 95, 99, 100};
    for (int i = 0; i < 5; i++) {
        int k = (A.n_lat - 1) * ps[i] / 100;
        printf("  p%-3d %7.2f ms\n", ps[i], A.lat[k]);
    }
}

static int app_setup(const char *root)
{
    A.fs = dirfs_new(root);
    A.now = 1000;
    cc_init(A.fs, A.now);
    cc_on_battery(80, 0);
    cc_on_sensor_state(1, CC_SENS_SEARCHING);
    cc_on_sensor_state(2, CC_SENS_SEARCHING);
    A.m.phone = UI_PHONE_NOT_PAIRED;
    reset_to(SCR_READY);
    refresh_lists();
    return 0;
}

/* ------------------------------------------------------------------ the scripted walk (WP4) */
static const char *walk_dir;
static int walk_fail;
static int shot_no;

static void shot(const char *name)
{
    char p[512];
    redraw();
    snprintf(p, sizeof p, "%s/%02d_%s.png", walk_dir, ++shot_no, name);
    sim_write_png(p);
}

static void expect(int cond, const char *what)
{
    if (!cond) { printf("walk: FAILED: %s\n", what); walk_fail++; }
    else printf("walk: ok: %s\n", what);
}

static void run_ms(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 100) { step(); } }

static void press(cc_btn_t b, uint32_t ms)
{
    cc_on_button(A.now, b, 1);
    run_ms(ms);
    cc_on_button(A.now, b, 0);
    step();
}

static fx_t fx;
static int fx_row;
static uint32_t fx_base;

static void ride_for(int secs)
{
    for (int s = 0; s < secs && fx_row < fx.n; s++) {
        while (A.now % 1000 != 400) step();
        A.now = fx_play_row(&fx, fx_row++, fx_base) - 100;
        step();
        gestures();
    }
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return -1; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
    return 0;
}

int app_walk(const char *dir, int perf)
{
    walk_dir = dir;
    A.perf = perf;
    mkdir(dir, 0755);
    char root[512], p[700];
    snprintf(root, sizeof root, "%s/CC700", dir);
    snprintf(p, sizeof p, "rm -rf '%s'", root);
    if (system(p) != 0) return 1;
    mkdir(root, 0755);
    snprintf(p, sizeof p, "%s/objects", root);
    mkdir(p, 0755);
    /* a workout and a route already sent by the phone */
    snprintf(p, sizeof p, "%s/objects/1812240e1809c011.cw1", root);
    static const char *w1 = "core/tests/fixtures/threshold-2x20.cw1";
    FILE *ix = fopen("core/tests/fixtures/workouts.txt", "r");
    (void)ix;
    if (ix) fclose(ix);
    /* use the starter workout's own ID for the file name */
    static uint8_t buf[CC_RT_MAX_BYTES];
    FILE *f = fopen(w1, "rb");
    if (!f) { printf("walk: run from the repository root\n"); return 1; }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static cc_workout_t wo;
    if (cc_cw1_decode(buf, n, &wo) != CC_CW1_OK) return 1;
    cc_object_path(p, sizeof p, wo.id, 1);
    char full[900];
    snprintf(full, sizeof full, "%s/%s", root, p);
    copy_file(w1, full);
    f = fopen("core/tests/fixtures/route.cr1", "rb");
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static cc_route_t r;
    if (cc_cr1_decode(buf, n, &r) != CC_CR1_OK) return 1;
    cc_object_path(p, sizeof p, r.id, 2);
    snprintf(full, sizeof full, "%s/%s", root, p);
    copy_file("core/tests/fixtures/route.cr1", full);

    app_setup(root);
    cc_set_utc(A.now, 1793000000u);
    cc_set_tz(480);
    cc_on_phone(1);
    A.m.phone = UI_PHONE_CONNECTED;
    snprintf(A.m.phone_name, sizeof A.m.phone_name, "Simulated phone");
    run_ms(1000);
    shot("ready");
    expect(top() == SCR_READY, "starts on Ready");

    /* Load the workout and the route through the menus, with buttons only */
    press(CC_BTN_L, 100);
    expect(top() == SCR_MENU, "left opens the Menu");
    shot("menu");
    press(CC_BTN_R, 100);
    expect(top() == SCR_WORKOUTS, "Workouts list");
    shot("workouts");
    press(CC_BTN_R, 100);
    expect(top() == SCR_WORKOUT_DETAIL, "workout detail");
    shot("workout_detail");
    press(CC_BTN_R, 100);
    expect(top() == SCR_READY && cc_view()->workout_loaded, "LOAD returns to Ready with the workout loaded");
    press(CC_BTN_L, 100);
    press(CC_BTN_PWR, 100);
    expect(A.m.focus == 1, "power moves the focus to Routes");
    press(CC_BTN_R, 100);
    expect(top() == SCR_ROUTES, "Routes list");
    press(CC_BTN_R, 100);
    expect(top() == SCR_ROUTE_DETAIL, "route detail");
    shot("route_detail");
    press(CC_BTN_R, 100);
    expect(top() == SCR_READY && cc_view()->route_loaded, "LOAD returns to Ready with the route loaded");
    shot("ready_loaded");

    /* Ride: START, then the four pages */
    if (fx_load(&fx, "core/tests/fixtures/route_ride.csv") != 0) return 1;
    fx_row = 0;
    fx_base = (A.now / 1000 + 1) * 1000;
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_RECORDING, "START begins recording");
    ride_for(30);
    expect(cc_view()->ring_n == 4, "four pages in the ring with a workout and a route");
    expect(visible() == SCR_MAIN, "first page is Main");
    shot("page_main");
    static const ui_screen_id_t pages[3] = {SCR_WORKOUT, SCR_CLIMB, SCR_NAV};
    static const char *names[3] = {"page_workout", "page_climb", "page_nav"};
    for (int i = 0; i < 3; i++) {
        press(CC_BTN_L, 100);
        ride_for(2);
        expect(visible() == pages[i], names[i]);
        shot(names[i]);
    }
    press(CC_BTN_L, 100);
    expect(visible() == SCR_MAIN, "the ring wraps to Main");
    cc_on_button(A.now, CC_BTN_L, 1);
    run_ms(1100);
    cc_on_button(A.now, CC_BTN_L, 0);
    step();
    expect(visible() == SCR_NAV, "hold left goes to the previous page");
    press(CC_BTN_R, 100);
    run_ms(200);
    expect(cc_view()->banner.id == CC_AL_LAP, "right short is a lap: LAP banner");
    shot("lap_banner");
    ride_for(60);

    /* Manual pause, the pause menu moved with the power key only, Finish and save */
    cc_on_button(A.now, CC_BTN_R, 1);
    run_ms(600);
    expect(cc_view()->hold.active, "hold bar while the right button is held");
    shot("hold_bar");
    run_ms(500);
    cc_on_button(A.now, CC_BTN_R, 0);
    step();
    expect(cc_view()->state == CC_RS_PAUSED && !cc_view()->pause_auto, "hold right pauses (manual)");
    shot("paused");
    press(CC_BTN_L, 100);
    expect(cc_view()->state == CC_RS_PAUSE_MENU && cc_view()->focus == CC_PM_RESUME, "pause menu, focus on Resume");
    shot("pause_menu");
    press(CC_BTN_PWR, 100);
    expect(cc_view()->focus == CC_PM_FINISH, "power moves the focus to Finish and save");
    shot("pause_menu_finish");
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_SUMMARY, "Finish and save shows the Summary");
    shot("summary");
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_READY && top() == SCR_READY, "DONE returns to Ready");
    expect(A.m.rides_total == 1, "the ride is in History");
    shot("ready_after");

    /* Second ride with no workout and no route: two-page ring */
    cc_cmd_unload_workout();
    cc_cmd_unload_route();
    run_ms(200);
    press(CC_BTN_R, 100);
    fx_row = 0;
    fx_base = (A.now / 1000 + 1) * 1000;
    ride_for(5);
    expect(cc_view()->ring_n == 2 && cc_view()->ring[0] == CC_PAGE_MAIN && cc_view()->ring[1] == CC_PAGE_CLIMB,
           "no workout and no route: the ring is Main, Climb");
    shot("ring2_main");
    press(CC_BTN_L, 100);
    ride_for(1);
    expect(visible() == SCR_CLIMB, "second page is Climb");
    shot("ring2_climb");
    press(CC_BTN_L, 100);
    ride_for(1);
    expect(visible() == SCR_MAIN, "and back to Main");
    fx_free(&fx);
    perf_report();
    printf("walk: %d screenshot(s) in %s, %d failure(s)\n", shot_no, dir, walk_fail);
    return walk_fail ? 1 : 0;
}

/* ------------------------------------------------------------------ interactive (SDL) */
#if CC_LV_USE_SDL
static int key_watch(void *ud, SDL_Event *e)
{
    (void)ud;
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 0;
    if (e->type == SDL_KEYDOWN && e->key.repeat) return 0;
    int down = e->type == SDL_KEYDOWN;
    cc_btn_t b;
    switch (e->key.keysym.sym) {
    case SDLK_p: b = CC_BTN_PWR; break;
    case SDLK_a: b = CC_BTN_L; break;
    case SDLK_l: b = CC_BTN_R; break;
    default: return 0;
    }
    if (down) A.pending_input = wall_ms();
    cc_on_button(A.now, b, down);
    return 0;
}
#endif

int app_interactive(const char *root, const char *script, int scale, int perf)
{
#if CC_LV_USE_SDL
    A.perf = perf;
    lv_init();
    lv_display_t *d = lv_sdl_window_create(240, 320);
    lv_sdl_window_set_zoom(d, (float)(scale > 0 ? scale : 1));
    lv_sdl_window_set_title(d, "CC700 simulator (P power, A left, L right)");
    lv_sdl_mouse_create();
    SDL_AddEventWatch(key_watch, NULL);
    char tmp[256];
    if (!root) {
        snprintf(tmp, sizeof tmp, "/tmp/ccsim-%d", (int)getpid());
        mkdir(tmp, 0755);
        root = tmp;
    }
    app_setup(root);
    cc_set_utc(A.now, (uint32_t)time(NULL));
    cc_set_tz(480);
    if (script && fx_load(&fx, script) == 0) fx_base = (A.now / 1000 + 1) * 1000;
    double t0 = wall_ms();
    uint32_t sim0 = A.now;
    for (;;) {
        uint32_t target = sim0 + (uint32_t)(wall_ms() - t0);
        while (A.now + 100 <= target) {
            if (script && fx.n && cc_view()->state == CC_RS_RECORDING && fx_row < fx.n && A.now % 1000 == 400) {
                A.now = fx_play_row(&fx, fx_row++, fx_base) - 100;
            }
            step();
        }
        render();
        lv_timer_handler();
        if (A.perf && A.pending_input > 0 && A.n_lat < 4096) { A.lat[A.n_lat++] = wall_ms() - A.pending_input; A.pending_input = 0; }
        if (cc_view()->power_off_request) break;
        SDL_Delay(30);
    }
    perf_report();
    return 0;
#else
    (void)root; (void)script; (void)scale; (void)perf;
    fprintf(stderr, "ccsim: built without SDL2; use --shot, --shots, --lint or --walk\n");
    return 2;
#endif
}
