/* Scenario fixtures for the screenshot tool: one per Board:scenario of the mockups, named identically
 * (brief 6.1). Each fills a model with the values the mockup shows, so the LVGL screen can be compared
 * with the board. Live values come from the core instead (ccsim live mode). */
#include <string.h>

#include "cc700/zones.h"
#include "ui.h"

#define LOCAL(d) (d) /* times below are already local (UTC+8) seconds */

static void set_str(char *d, size_t n, const char *s) { strncpy(d, s, n - 1); d[n - 1] = 0; }

void ui_model_demo(ui_model_t *m)
{
    memset(m, 0, sizeof *m);
    cc_view_t *v = &m->v;
    m->clock_min = 14 * 60 + 32;
    v->battery_pct = 68;
    v->gps_fix = 1;
    v->power_state = CC_SENS_OK;
    v->hr_state = CC_SENS_OK;
    v->phone_connected = 1;
    v->ftp = 260;
    v->settings.ftp = 260;
    v->settings.brightness = CC_BRIGHT_AUTO;
    v->settings.screen_off = CC_SCREENOFF_1MIN;
    v->settings.units = CC_UNITS_KM;
    v->settings.autopause = 1;
    v->settings.tz_min = 480;
    v->tz_min = 480;
    v->ring_n = 4;
    v->ring[0] = CC_PAGE_MAIN; v->ring[1] = CC_PAGE_WORKOUT; v->ring[2] = CC_PAGE_CLIMB; v->ring[3] = CC_PAGE_NAV;
    /* ride values shown on Main and Alerts */
    v->state = CC_RS_RECORDING;
    v->power3_ok = 1; v->power3 = 248; v->zone = 4;
    v->power10_ok = 1; v->power10 = 242;
    v->hr_ok = 1; v->hr = 152;
    v->cad_ok = 1; v->cad = 88;
    v->speed_ok = 1; v->speed_mms = 9000;
    v->dist_m = 32600;
    v->timer_s = 4367;
    v->lap_s = 312;
    v->avg_power_ok = 1; v->avg_power = 221;
    v->workout_loaded = 1;
    set_str(v->workout_name, sizeof v->workout_name, "4 x 8 min threshold");
    v->workout_secs = 4080; v->workout_steps = 10;
    v->route_loaded = 1;
    set_str(v->route_name, sizeof v->route_name, "Swan River loop");
    v->route_dist_m = 38400; v->route_ascent_m = 210; v->route_climbs = 2;
    m->pm_paired = m->hr_paired = 1;
    set_str(m->pm_name, sizeof m->pm_name, "PM500");
    set_str(m->pm_id4, sizeof m->pm_id4, "7C2E");
    set_str(m->hr_id4, sizeof m->hr_id4, "41B9");
    m->phone = UI_PHONE_CONNECTED;
    set_str(m->phone_name, sizeof m->phone_name, "Pixel 8");
    set_str(m->last_sync, sizeof m->last_sync, "14:20 today");
    m->rides_waiting = 1;
    /* libraries */
    static const ui_wo_item_t wo[] = {
        {"4 x 8 min threshold", 4080, 10, {0}}, {"Sweet spot 3 x 12", 4260, 8, {0}}, {"VO2max 5 x 3 min", 3000, 12, {0}},
        {"Endurance 90", 5400, 3, {0}}, {"Recovery spin 45", 2700, 3, {0}}, {"Threshold 2 x 20", 4500, 8, {0}},
        {"Ramp test to failure", 2460, 24, {0}}};
    memcpy(m->workouts, wo, sizeof wo);
    m->n_workouts = 7;
    static const ui_rt_item_t rt[] = {{"Swan River loop", 38400, 210, 2, {0}}, {"Kings Park hills", 24600, 380, 4, {0}},
                                      {"Perth Hills 80", 81200, 1150, 7, {0}}};
    memcpy(m->routes, rt, sizeof rt);
    m->n_routes = 3;
    static const ui_ride_item_t rides[] = {
        {1793430900, LOCAL(1793457120), 42300, 6492, 214, 1, 0}, {1793258100, LOCAL(1793284320), 31700, 4865, 196, 1, 1},
        {1793085300, LOCAL(1793111520), 56000, 8380, 188, 1, 1}, {1792912500, LOCAL(1792938720), 18900, 3138, 175, 1, 1},
        {1792826100, LOCAL(1792852320), 63400, 9682, 201, 1, 1}};
    memcpy(m->rides, rides, sizeof rides);
    m->n_rides = 5;
    m->rides_total = 12;
}

/* ------------------------------------------------------------------ ride pages */
static void f_main(ui_model_t *m) { ui_model_demo(m); }

static void workout_base(ui_model_t *m, int power, int zone, cc_pill_state_t pill, int delta)
{
    ui_model_demo(m);
    cc_wo_view_t *w = &m->v.wo;
    w->loaded = 1; w->step = 2; w->nsteps = 6; w->label = "Threshold";
    w->step_secs = 360; w->elapsed_s = 199; w->remaining_s = 161;
    w->lo_w = 247; w->hi_w = 260; w->target_zone = 4;
    w->pill = pill; w->pill_delta = delta; w->actual_zone = zone;
    m->v.power3 = power; m->v.zone = zone; m->v.cad = 92;
}
static void f_wo_in(ui_model_t *m) { workout_base(m, 252, 4, CC_PILL_IN, 0); }
static void f_wo_high(ui_model_t *m) { workout_base(m, 291, 5, CC_PILL_HIGH, 31); }
static void f_wo_low(ui_model_t *m) { workout_base(m, 214, 3, CC_PILL_LOW, 33); }

static void f_climb(ui_model_t *m)
{
    ui_model_demo(m);
    cc_view_t *v = &m->v;
    v->grad_ok = 1; v->grad_x10 = 74; v->steep = 1;
    cc_nav_view_t *n = &v->nav;
    n->loaded = 1; n->have_pos = 1; n->climb_show = 1; n->on_climb = 1; n->climb_idx = 0;
    n->climb_dist_left_m = 1800; n->climb_ascent_left_m = 126;
    set_str(n->climb_summary, sizeof n->climb_summary, "3.4 km \xC2\xB7 212 m climb");
    static const float e[] = {10, 11.5f, 15, 22, 29.5f, 37, 45, 52, 58};
    memcpy(m->climb_prof, e, sizeof e);
    m->climb_prof_n = 9;
    m->climb_pos = 105.0f / 224.0f;
}

static void nav_base(ui_model_t *m, uint32_t to_turn)
{
    ui_model_demo(m);
    cc_nav_view_t *n = &m->v.nav;
    n->loaded = 1; n->have_pos = 1; n->turn_idx = 1; n->dist_to_turn_m = to_turn;
    set_str(n->instr, sizeof n->instr, "Turn right \xC2\xB7 Main St");
    n->dist_left_m = 18400; n->eta_valid = 1; n->eta_s = 52 * 60;
    float need = 1.5f * (float)to_turn * 240.0f / 95.0f;
    static const uint32_t sc[] = {100, 200, 400, 800, 1600, 3200};
    n->map_scale_m = 3200;
    for (int i = 0; i < 6; i++) if ((float)sc[i] >= (need < 200 ? 200 : need)) { n->map_scale_m = sc[i]; break; }
    m->v.speed_mms = 6833;
    /* the route: up from behind the rider, a right turn ahead, then east */
    int k = 0;
    for (float y = -400; y <= (float)to_turn; y += 50) { m->map_x[k] = 0; m->map_y[k++] = y; }
    m->map_x[k] = 0; m->map_y[k++] = (float)to_turn;
    for (float x = 50; x <= 2000; x += 100) { m->map_x[k] = x; m->map_y[k++] = (float)to_turn; }
    m->map_n = k;
    m->map_ridden = 8;
    m->course_deg = 0;
}
static void f_nav_on(ui_model_t *m) { nav_base(m, 250); }
static void f_nav_turn(ui_model_t *m) { nav_base(m, 50); }
static void f_nav_off(ui_model_t *m)
{
    nav_base(m, 250);
    m->v.nav.off_route = 1;
    m->v.nav.dist_to_route_m = 120;
    for (int i = 0; i < m->map_n; i++) m->map_x[i] -= 120; /* the route is 120 m west */
}

/* ------------------------------------------------------------------ alerts */
static void alert(ui_model_t *m, cc_alert_id_t id, cc_alert_style_t st, uint32_t acc, const char *t, const char *s,
                  const char *r)
{
    ui_model_demo(m);
    cc_banner_t *b = &m->v.banner;
    b->id = id; b->style = st; b->accent_rgb = acc;
    set_str(b->title, sizeof b->title, t);
    set_str(b->sub, sizeof b->sub, s);
    set_str(b->right, sizeof b->right, r);
}
static void f_al_next(ui_model_t *m)
{
    alert(m, CC_AL_NEXTSTEP, CC_AL_STYLE_PANEL, cc_zone_rgb[1], "NEXT \xC2\xB7 RECOVER", "136\xE2\x80\x93" "150 W for 4:00", "5 s");
}
static void f_al_lap(ui_model_t *m) { alert(m, CC_AL_LAP, CC_AL_STYLE_PANEL, C_TEXT, "LAP 3", "5:12 \xC2\xB7 2.4 km", ""); }
static void f_al_complete(ui_model_t *m) { alert(m, CC_AL_COMPLETE, CC_AL_STYLE_GREEN, C_GREEN, "WORKOUT COMPLETE", "4 x 8 min threshold", ""); }
static void f_al_power(ui_model_t *m)
{
    alert(m, CC_AL_POWERLOST, CC_AL_STYLE_RED, C_RED, "POWER METER LOST", "Trying to reconnect", "");
    m->v.power3_ok = 0; m->v.zone = 0; m->v.cad_ok = 0; m->v.power_state = CC_SENS_LOST;
}
static void f_al_gps(ui_model_t *m) { alert(m, CC_AL_GPSLOST, CC_AL_STYLE_YELLOW, C_YELLOW, "GPS SIGNAL LOST", "No speed or distance", ""); }
static void f_al_batt(ui_model_t *m)
{
    alert(m, CC_AL_LOWBATT, CC_AL_STYLE_ORANGE, C_ORANGE, "LOW BATTERY 15%", "Recording continues", "");
    m->v.battery_pct = 15;
}
static void f_al_hold(ui_model_t *m)
{
    ui_model_demo(m);
    m->v.hold.active = 1; m->v.hold.side = CC_BTN_R; m->v.hold.elapsed_ms = 600;
}

/* ------------------------------------------------------------------ menus */
static void f_list(ui_model_t *m) { ui_model_demo(m); m->v.state = CC_RS_READY; }

static void f_wo_detail(ui_model_t *m)
{
    f_list(m);
    cc_workout_t *w = &m->detail_wo;
    set_str(w->name, sizeof w->name, "4 x 8 min threshold");
    w->nsteps = 10;
    w->total_secs = 4080;
    cc_step_t wu = {600, 500, 700, 1, 0, 0, 0, "Warm up"};
    w->steps[0] = wu;
    for (int i = 0; i < 4; i++) {
        cc_step_t th = {480, 950, 1000, 0, 0, (uint8_t)(i + 1), 4, "Threshold"};
        cc_step_t rc = {240, 450, 550, 0, 0, (uint8_t)(i + 1), 4, "Recover"};
        w->steps[1 + 2 * i] = th;
        w->steps[2 + 2 * i] = rc;
    }
    cc_step_t cd = {600, 550, 400, 1, 0, 0, 0, "Cool down"};
    w->steps[9] = cd;
}

static void f_ready(ui_model_t *m) { f_list(m); }
static void f_ready_search(ui_model_t *m)
{
    f_list(m);
    m->v.gps_fix = 0;
    m->v.power_state = CC_SENS_SEARCHING;
}
static void f_sensors(ui_model_t *m) { f_list(m); m->v.hr_state = CC_SENS_SEARCHING; }
static void f_add(ui_model_t *m)
{
    f_list(m);
    m->scanning = 1;
    ui_scan_item_t s[3] = {{"PM500", "7C2E", 1, 3}, {"PM500", "90D1", 1, 1}, {"Sensor", "41B9", 2, 2}};
    memcpy(m->scan, s, sizeof s);
    m->n_scan = 3;
}
static void pm(ui_model_t *m, ui_cal_state_t c) { f_list(m); m->cal = c; }
static void f_pm_idle(ui_model_t *m) { pm(m, UI_CAL_IDLE); }
static void f_pm_cal(ui_model_t *m) { pm(m, UI_CAL_RUNNING); }
static void f_pm_done(ui_model_t *m) { pm(m, UI_CAL_DONE); }
static void f_pm_failed(ui_model_t *m) { pm(m, UI_CAL_FAILED); }
static void f_prof(ui_model_t *m) { f_list(m); m->ftp_edit = 260; }
static void f_prof_ch(ui_model_t *m) { f_list(m); m->ftp_edit = 265; }

static void f_route_detail(ui_model_t *m)
{
    f_list(m);
    static const float km[] = {0, 6, 8, 9.5f, 12, 18, 24, 27, 29, 34, 38.4f};
    static const float el[] = {20, 22, 25, 85, 60, 35, 30, 175, 140, 40, 20};
    m->detail_route = 0;
    m->detail_prof_n = 96;
    m->detail_prof_len_m = 38400;
    for (int i = 0; i < 96; i++) {
        float d = 38.4f * (float)i / 95.0f;
        int k = 0;
        while (k < 9 && km[k + 1] < d) k++;
        float f = (d - km[k]) / (km[k + 1] - km[k]);
        m->detail_prof[i] = (int16_t)((el[k] + f * (el[k + 1] - el[k])) * 10.0f);
    }
    m->detail_n_climbs = 2;
    m->detail_climbs[0].start_m = 8000; m->detail_climbs[0].len_m = 1500;
    m->detail_climbs[1].start_m = 24000; m->detail_climbs[1].len_m = 3000;
}

static void summary(ui_model_t *m, int up)
{
    f_list(m);
    m->summary_ride = m->rides[0];
    m->summary.timer_s = 6492; m->summary.distance_m = 42300;
    m->summary.has_power = 1; m->summary.avg_power = 214;
    m->summary.has_hr = 1; m->summary.avg_hr = 148;
    m->summary.ascent_m = 312; m->summary.avg_speed_mms = 6528;
    m->summary_uploaded = up;
}
static void f_sum_wait(ui_model_t *m) { summary(m, 0); }
static void f_sum_up(ui_model_t *m) { summary(m, 1); }

static void f_paused(ui_model_t *m) { ui_model_demo(m); m->v.state = CC_RS_PAUSED; m->paused_s = 42; }
static void f_paused_auto(ui_model_t *m) { f_paused(m); m->v.pause_auto = 1; m->paused_s = 15; }
static void f_pmenu(ui_model_t *m) { ui_model_demo(m); m->v.state = CC_RS_PAUSE_MENU; m->v.focus = CC_PM_RESUME; }
static void confirm(ui_model_t *m, ui_confirm_kind_t k)
{
    ui_model_demo(m);
    m->confirm = k;
    m->v.state = k == UI_CONFIRM_DISCARD ? CC_RS_CONFIRM_DISCARD : CC_RS_READY;
    m->v.focus = CC_CF_KEEP;
}
static void f_cf_discard(ui_model_t *m) { confirm(m, UI_CONFIRM_DISCARD); }
static void f_cf_sensor(ui_model_t *m) { confirm(m, UI_CONFIRM_FORGET_SENSOR); }
static void f_cf_phone(ui_model_t *m) { confirm(m, UI_CONFIRM_FORGET_PHONE); }
static void empty(ui_model_t *m, ui_empty_kind_t k) { ui_model_demo(m); m->empty = k; }
static void f_e_wo(ui_model_t *m) { empty(m, UI_EMPTY_NOWORKOUT); }
static void f_e_rt(ui_model_t *m) { empty(m, UI_EMPTY_NOROUTE); }
static void f_e_gps(ui_model_t *m) { empty(m, UI_EMPTY_GPSWAIT); }
static void f_hr(ui_model_t *m) { f_list(m); m->hr_live = 138; m->hr_bars = 3; }
static void f_hr_search(ui_model_t *m) { f_list(m); m->v.hr_state = CC_SENS_SEARCHING; }
static void f_ph(ui_model_t *m) { f_list(m); }
static void f_ph_pair(ui_model_t *m) { f_list(m); m->phone = UI_PHONE_PAIRING; m->passkey = 482916; m->v.phone_connected = 0; }
static void f_ph_none(ui_model_t *m) { f_list(m); m->phone = UI_PHONE_NOT_PAIRED; m->v.phone_connected = 0; }
static void f_settings(ui_model_t *m) { f_list(m); }

const ui_scenario_t ui_scenarios[] = {
    {"Main", "only", SCR_MAIN, f_main},
    {"Workout", "inrange", SCR_WORKOUT, f_wo_in},
    {"Workout", "high", SCR_WORKOUT, f_wo_high},
    {"Workout", "low", SCR_WORKOUT, f_wo_low},
    {"Climb", "only", SCR_CLIMB, f_climb},
    {"Nav", "ontrack", SCR_NAV, f_nav_on},
    {"Nav", "turn", SCR_NAV, f_nav_turn},
    {"Nav", "reroute", SCR_NAV, f_nav_off},
    {"Workouts", "only", SCR_WORKOUTS, f_list},
    {"WorkoutDetail", "only", SCR_WORKOUT_DETAIL, f_wo_detail},
    {"Ready", "ready", SCR_READY, f_ready},
    {"Ready", "searching", SCR_READY, f_ready_search},
    {"Menu", "only", SCR_MENU, f_list},
    {"Sensors", "only", SCR_SENSORS, f_sensors},
    {"AddSensor", "only", SCR_ADD_SENSOR, f_add},
    {"PowerMeter", "idle", SCR_POWER_METER, f_pm_idle},
    {"PowerMeter", "calibrating", SCR_POWER_METER, f_pm_cal},
    {"PowerMeter", "done", SCR_POWER_METER, f_pm_done},
    {"PowerMeter", "failed", SCR_POWER_METER, f_pm_failed},
    {"Profile", "unchanged", SCR_PROFILE, f_prof},
    {"Profile", "changed", SCR_PROFILE, f_prof_ch},
    {"Routes", "only", SCR_ROUTES, f_list},
    {"RouteDetail", "only", SCR_ROUTE_DETAIL, f_route_detail},
    {"History", "only", SCR_HISTORY, f_list},
    {"Summary", "waiting", SCR_SUMMARY, f_sum_wait},
    {"Summary", "uploaded", SCR_SUMMARY, f_sum_up},
    {"Paused", "manual", SCR_PAUSED, f_paused},
    {"Paused", "auto", SCR_PAUSED, f_paused_auto},
    {"PauseMenu", "only", SCR_PAUSE_MENU, f_pmenu},
    {"Confirm", "discard", SCR_CONFIRM, f_cf_discard},
    {"Confirm", "forgetsensor", SCR_CONFIRM, f_cf_sensor},
    {"Confirm", "forgetphone", SCR_CONFIRM, f_cf_phone},
    {"Alerts", "nextstep", SCR_ALERTS, f_al_next},
    {"Alerts", "lap", SCR_ALERTS, f_al_lap},
    {"Alerts", "complete", SCR_ALERTS, f_al_complete},
    {"Alerts", "powerlost", SCR_ALERTS, f_al_power},
    {"Alerts", "gpslost", SCR_ALERTS, f_al_gps},
    {"Alerts", "lowbatt", SCR_ALERTS, f_al_batt},
    {"Alerts", "holdpause", SCR_ALERTS, f_al_hold},
    {"Empty", "noworkout", SCR_EMPTY, f_e_wo},
    {"Empty", "noroute", SCR_EMPTY, f_e_rt},
    {"Empty", "gpswait", SCR_EMPTY, f_e_gps},
    {"HeartRate", "connected", SCR_HEART_RATE, f_hr},
    {"HeartRate", "searching", SCR_HEART_RATE, f_hr_search},
    {"Phone", "connected", SCR_PHONE, f_ph},
    {"Phone", "pairing", SCR_PHONE, f_ph_pair},
    {"Phone", "notpaired", SCR_PHONE, f_ph_none},
    {"Settings", "only", SCR_SETTINGS, f_settings},
};
const int ui_n_scenarios = (int)(sizeof ui_scenarios / sizeof ui_scenarios[0]);
