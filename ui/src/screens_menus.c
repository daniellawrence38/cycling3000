/* Menu, list and detail screens: boards 5 to 16, 18, 19 and 22 to 24. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cc700/version.h"
#include "cc700/zones.h"
#include "cc700/provisional.h"
#include "ui.h"

#define LIST_Y 58      /* first row under a 36 px header */
#define LIST_ROWS 5

static void list_thumb(lv_obj_t *r, int top, int n, int scroll)
{
    if (n <= LIST_ROWS) return;
    int h = 230 * LIST_ROWS / n;
    int y = top + 2 + (scroll * (226 - h)) / (n - LIST_ROWS);
    ui_scrollbar(r, 235, y, h);
}

static int ride_on(const ui_model_t *m)
{
    return m->v.state != CC_RS_READY && m->v.state != CC_RS_SUMMARY;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------------ 5 Workouts list */
void scr_workouts(lv_obj_t *r, const ui_model_t *m)
{
    char t[48];
    ui_status(r, m);
    snprintf(t, sizeof t, "%d / %d", m->n_workouts, CC_MAX_WORKOUTS);
    ui_header36(r, 22, "Workouts", t);
    int s = clampi(m->scroll, 0, m->n_workouts > LIST_ROWS ? m->n_workouts - LIST_ROWS : 0);
    for (int i = s; i < m->n_workouts && i < s + LIST_ROWS; i++) {
        char hm[12];
        ui_fmt_hm(hm, sizeof hm, m->workouts[i].secs);
        snprintf(t, sizeof t, "%s \xC2\xB7 %d steps", hm, m->workouts[i].steps);
        ui_chev(ui_row46(r, LIST_Y + (i - s) * 46, m->workouts[i].name, t, i == m->focus, C_TEXT));
    }
    if (m->n_workouts == 0)
        ui_text(r, 11, LIST_Y + 10, 218, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
                "Send workouts from the phone app. Up to 20 are kept on the unit.");
    list_thumb(r, LIST_Y, m->n_workouts, s);
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 6 Workout detail */
typedef struct { const cc_workout_t *wo; } wprof_t;
static wprof_t wprof;

static float pct_y(int p10, float scale) { return 84.0f - (float)p10 / 10.0f * scale; }

static void wprof_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const cc_workout_t *w = wprof.wo;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    if (!w || !w->total_secs) return;
    int maxp = 1000;
    for (int i = 0; i < w->nsteps; i++) {
        int p = w->steps[i].ramp ? (w->steps[i].p0 > w->steps[i].p1 ? w->steps[i].p0 : w->steps[i].p1)
                                 : (w->steps[i].p0 + w->steps[i].p1) / 2;
        if (p > maxp) maxp = p;
    }
    float scale = 0.727f; /* px per percent: FTP at y 11.3, as in the mockup */
    if ((float)maxp / 10.0f * scale > 80.0f) scale = 80.0f / ((float)maxp / 10.0f);
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(C_DOT);
    ld.width = 1;
    ld.dash_width = 3;
    ld.dash_gap = 3;
    ld.p1.x = a.x1; ld.p2.x = a.x1 + 224;
    ld.p1.y = ld.p2.y = a.y1 + pct_y(1000, scale);
    lv_draw_line(layer, &ld);
    uint32_t t = 0;
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    for (int i = 0; i < w->nsteps; i++) {
        const cc_step_t *s = &w->steps[i];
        float x0 = a.x1 + 224.0f * (float)t / (float)w->total_secs;
        t += s->secs;
        float x1 = a.x1 + 224.0f * (float)t / (float)w->total_secs;
        int pa = s->ramp ? s->p0 : (s->p0 + s->p1) / 2, pb = s->ramp ? s->p1 : pa;
        int pm = (pa + pb) / 2;
        td.color = lv_color_hex(cc_zone_rgb[cc_zone_of_p10(pm)]);
        float ya = a.y1 + pct_y(pa, scale), yb = a.y1 + pct_y(pb, scale), yz = (float)a.y1 + 84;
        td.p[0].x = x0; td.p[0].y = ya; td.p[1].x = x1; td.p[1].y = yb; td.p[2].x = x1; td.p[2].y = yz;
        lv_draw_triangle(layer, &td);
        td.p[1].x = x1; td.p[1].y = yz; td.p[2].x = x0; td.p[2].y = yz;
        lv_draw_triangle(layer, &td);
        if (i > 0) { /* the dark stroke between bars */
            lv_draw_line_dsc_t sd;
            lv_draw_line_dsc_init(&sd);
            sd.color = lv_color_hex(C_BG);
            sd.width = 1;
            sd.p1.x = sd.p2.x = x0;
            sd.p1.y = ya; sd.p2.y = yz;
            lv_draw_line(layer, &sd);
        }
    }
}

static void stat3(lv_obj_t *r, int x, int y, const char *lbl, const char *val, const char *unit, int right)
{
    if (right) ui_vline(r, x + 79, y, 47, C_DIV);
    ui_caps(r, x + 8, y + 4, 0, LV_TEXT_ALIGN_LEFT, lbl);
    ui_text(r, x + 8, y + 18, 0, 26, &font_c600_28, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, val);
    int b = ui_last_baseline;
    if (unit && *unit)
        ui_text_base(r, x + 8 + ui_text_w(&font_c600_28, 0, val) + 3, b, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, unit);
}

void scr_workout_detail(lv_obj_t *r, const ui_model_t *m)
{
    char t[32];
    const cc_workout_t *w = &m->detail_wo;
    ui_status(r, m);
    ui_header44(r, 22, w->name, &font_b700_16, 0);
    lv_obj_t *o = ui_rect(r, 8, 76, 224, 84, C_BG, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    wprof.wo = w;
    lv_obj_add_event_cb(o, wprof_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_text(r, 232, 75, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, "FTP");
    ui_text(r, 8, 160, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "0:00");
    ui_fmt_hm(t, sizeof t, w->total_secs);
    ui_text(r, 232, 160, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_hline(r, 0, 172, 240, C_DIV);
    stat3(r, 0, 173, "TIME", t, "", 1);
    snprintf(t, sizeof t, "%d", w->nsteps);
    stat3(r, 80, 173, "STEPS", t, "", 1);
    snprintf(t, sizeof t, "%u", m->v.ftp);
    stat3(r, 160, 173, "FTP", t, "W", 0);
    ui_hline(r, 0, 220, 240, C_DIV);
    ui_btn(r, 8, 229, 224, 51, "LOAD WORKOUT", ride_on(m) ? BTN_DISABLED : BTN_PRIMARY, &font_b700_18, 0);
    ui_footer(r, "BACK", FOOT_BACK, "LOAD", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 7 Ready */
static void ready_row(lv_obj_t *r, int y, const lv_image_dsc_t *ic, const char *name, ui_chip_kind_t k, const char *chip)
{
    ui_icon(r, ic, 8, y + 8, C_STATUS);
    ui_text(r, 32, y, 0, 31, &font_b600_15, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, name);
    ui_chip(r, 232, y + 5, 22, &font_b700_11, k, NULL, chip);
    ui_hline(r, 0, y + 31, 240, C_DIV);
}

static void sens_chip(cc_sens_state_t s, int paired, ui_chip_kind_t *k, const char **t)
{
    if (!paired || s == CC_SENS_OFF) { *k = CHIP_NEUTRAL; *t = "NOT PAIRED"; return; }
    if (s == CC_SENS_OK) { *k = CHIP_NEUTRAL; *t = "CONNECTED"; return; }
    *k = CHIP_PENDING; *t = "SEARCHING";
}

void scr_ready(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    char t[64], hm[12];
    ui_status(r, m);
    ui_chip_kind_t kg = v->gps_fix ? CHIP_NEUTRAL : CHIP_PENDING, kp, kh;
    const char *tp, *th;
    sens_chip(v->power_state, m->pm_paired, &kp, &tp);
    sens_chip(v->hr_state, m->hr_paired, &kh, &th);
    int waiting = kg == CHIP_PENDING || kp == CHIP_PENDING || kh == CHIP_PENDING;
    ui_text(r, 8, 22, 0, 33, &font_b700_20, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, waiting ? "Waiting for sensors" : "Ready to ride");
    ui_hline(r, 0, 55, 240, C_DIV);
    ready_row(r, 56, &ic_pin_16, "GPS", kg, v->gps_fix ? "FIXED" : "SEARCHING");
    ready_row(r, 88, &ic_bolt_16, "Power meter", kp, tp);
    ready_row(r, 120, &ic_heart_16, "Heart rate", kh, th);
    ready_row(r, 152, &ic_phone_16, "Phone", CHIP_NEUTRAL, v->phone_connected ? "CONNECTED" : "NOT CONNECTED");
    /* loaded workout and route */
    ui_text(r, 8, 188, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 1, "WORKOUT");
    if (v->workout_loaded) {
        lv_obj_t *n = ui_text(r, 8, 200, 224, 20, &font_b700_16, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, v->workout_name);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(n, lv_font_get_line_height(&font_b700_16));
        ui_fmt_hm(hm, sizeof hm, v->workout_secs);
        snprintf(t, sizeof t, "%s \xC2\xB7 %d steps \xC2\xB7 FTP %u W", hm, v->workout_steps, v->ftp);
        ui_text(r, 8, 220, 0, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    } else {
        ui_text(r, 8, 200, 0, 20, &font_b700_16, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "None");
        ui_text(r, 8, 220, 0, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "Load one from Menu, Workouts");
    }
    ui_hline(r, 0, 235, 240, C_DIV);
    ui_text(r, 8, 240, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 1, "ROUTE");
    if (v->route_loaded) {
        lv_obj_t *n = ui_text(r, 8, 252, 224, 20, &font_b700_16, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, v->route_name);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(n, lv_font_get_line_height(&font_b700_16));
        char d[16];
        ui_fmt_dist(d, sizeof d, v->route_dist_m, v->settings.units == CC_UNITS_MI);
        snprintf(t, sizeof t, "%s %s \xC2\xB7 %u m \xC2\xB7 %u climb%s", d, v->settings.units == CC_UNITS_MI ? "mi" : "km",
                 v->route_ascent_m, v->route_climbs, v->route_climbs == 1 ? "" : "s");
        ui_text(r, 8, 272, 0, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    } else {
        ui_text(r, 8, 252, 0, 20, &font_b700_16, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "None");
        ui_text(r, 8, 272, 0, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "Load one from Menu, Routes");
    }
    ui_hit(r, 140, 276, 100, 44); /* START by touch */
    ui_footer(r, "MENU", FOOT_MENU, "START", FOOT_PRIMARY);
}

/* ------------------------------------------------------------------ 8 Menu */
void scr_menu(lv_obj_t *r, const ui_model_t *m)
{
    char a[24], b[24], c[24], d[24];
    ui_status(r, m);
    ui_header36(r, 22, "Menu", "");
    snprintf(a, sizeof a, "%d of %d", m->n_workouts, CC_MAX_WORKOUTS);
    snprintf(b, sizeof b, "%d saved", m->n_routes);
    if (m->v.max_hr) snprintf(c, sizeof c, "FTP %u \xC2\xB7 HR %u", m->v.ftp, m->v.max_hr);
    else snprintf(c, sizeof c, "FTP %u W", m->v.ftp);
    snprintf(d, sizeof d, "%d ride%s", m->rides_total, m->rides_total == 1 ? "" : "s");
    const char *sens = m->pm_paired && m->hr_paired ? "Power \xC2\xB7 HR" : m->pm_paired ? "Power" : m->hr_paired ? "HR" : "None";
    const char *names[7] = {"Workouts", "Routes", "Sensors", "Profile", "History", "Phone", "Settings"};
    const char *vals[7] = {a, b, sens, c, d, m->phone == UI_PHONE_CONNECTED ? "Connected" : m->phone == UI_PHONE_PAIRING ? "Pairing" : "Not paired",
                           "Display \xC2\xB7 Units"};
    int s = clampi(m->scroll, 0, 7 - LIST_ROWS);
    for (int i = s; i < 7 && i < s + LIST_ROWS; i++) {
        lv_obj_t *row = ui_hit(r, 0, LIST_Y + (i - s) * 46, 240, 46);
        if (i == m->focus) {
            ui_rect(row, 0, 0, 240, 45, C_PANEL, 0);
            ui_rect(row, 0, 0, 3, 45, C_TEXT, 0);
        }
        ui_hline(row, 0, 45, 240, C_DIV);
        ui_text(row, 11, 0, 0, 45, &font_b600_16, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, names[i]);
        uint32_t vc = C_MUTED;
        if (i == 4 && m->rides_total > CC_RIDES_KEEP) vc = C_YELLOW; /* cue to sync (spec 10.4) */
        ui_text(row, 206, 0, 0, 45, &font_b500_13, vc, LV_TEXT_ALIGN_RIGHT, 0, vals[i]);
        ui_chev(row);
    }
    list_thumb(r, LIST_Y, 7, s);
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 9 Sensors */
void scr_sensors(lv_obj_t *r, const ui_model_t *m)
{
    char t[48];
    const cc_view_t *v = &m->v;
    ui_status(r, m);
    ui_header36(r, 22, "Sensors", "");
    int y = LIST_Y, i = 0;
    if (m->pm_paired) {
        const char *st = v->power_state == CC_SENS_OK ? "Connected" : "Searching\xE2\x80\xA6";
        snprintf(t, sizeof t, "%s \xC2\xB7\xC2\xB7\xC2\xB7%s \xC2\xB7 %s", m->pm_name, m->pm_id4, st);
        ui_chev(ui_row46_sub(r, y, "Power meter", t, m->focus == i, C_TEXT, v->power_state == CC_SENS_OK ? C_MUTED : C_YELLOW));
        y += 46; i++;
    }
    if (m->hr_paired) {
        const char *st = v->hr_state == CC_SENS_OK ? "Connected" : "Searching\xE2\x80\xA6";
        snprintf(t, sizeof t, "\xC2\xB7\xC2\xB7\xC2\xB7%s \xC2\xB7 %s", m->hr_id4, st);
        ui_chev(ui_row46_sub(r, y, "Heart rate", t, m->focus == i, C_TEXT, v->hr_state == CC_SENS_OK ? C_MUTED : C_YELLOW));
        y += 46; i++;
    }
    if (!(m->pm_paired && m->hr_paired)) {
        lv_obj_t *row = ui_row46(r, y, "", "", m->focus == i, C_TEXT);
        ui_icon(row, &ic_plus_16, 11, 15, C_TEXT);
        ui_text(row, 35, 0, 0, 45, &font_b600_15, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, "Add sensor");
        y += 46;
    }
    ui_text(r, 11, y + 10, 218, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
            "Bluetooth sensors only. Paired sensors reconnect automatically when they wake.");
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 10 Add sensor */
void scr_add_sensor(lv_obj_t *r, const ui_model_t *m)
{
    char t[48];
    ui_status(r, m);
    ui_header44(r, 22, "Add sensor", &font_b700_18, 0);
    if (m->scanning) {
        ui_icon(r, &ic_scan_16, 8, 75, C_YELLOW);
        ui_text(r, 30, 74, 0, 18, &font_b700_11, C_YELLOW, LV_TEXT_ALIGN_LEFT, 1, "SCANNING");
    }
    ui_text(r, 8, 96, 224, 18, &font_b600_14, C_TEXT, LV_TEXT_ALIGN_LEFT, 0,
            "Turn the crank to wake the power meter, then select it.");
    ui_text(r, 8, 132, 0, 14, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "Bluetooth sensors only.");
    ui_hline(r, 0, 149, 240, C_DIV);
    for (int i = 0; i < m->n_scan && i < 3; i++) {
        snprintf(t, sizeof t, "%s \xC2\xB7\xC2\xB7\xC2\xB7%s", m->scan[i].name, m->scan[i].id4);
        lv_obj_t *row = ui_row46(r, 150 + 46 * i, t, m->scan[i].kind == 1 ? "Power meter" : "Heart rate", m->focus == i, C_TEXT);
        ui_bars(row, 208, 15, m->scan[i].bars);
    }
    ui_footer(r, "BACK", FOOT_BACK, "PAIR", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 11 Power meter */
static void identity(lv_obj_t *r, const char *name, const char *sub, ui_chip_kind_t k, const char *chip)
{
    ui_text(r, 8, 70, 0, 20, &font_b700_16, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, name);
    ui_text(r, 8, 90, 0, 14, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, sub);
    ui_chip(r, 232, 77, 22, &font_b700_11, k, NULL, chip);
    ui_hline(r, 0, 109, 240, C_DIV);
}

void scr_power_meter(lv_obj_t *r, const ui_model_t *m)
{
    char t[48];
    ui_status(r, m);
    ui_header44(r, 22, "Power meter", &font_b700_18, 1);
    snprintf(t, sizeof t, "\xC2\xB7\xC2\xB7\xC2\xB7%s \xC2\xB7 Bluetooth", m->pm_id4);
    int conn = m->v.power_state == CC_SENS_OK;
    identity(r, m->pm_name, t, conn ? CHIP_NEUTRAL : CHIP_PENDING, conn ? "CONNECTED" : "SEARCHING");
    ui_caps(r, 8, 116, 0, LV_TEXT_ALIGN_LEFT, "LIVE POWER");
    ui_caps(r, 232, 116, 0, LV_TEXT_ALIGN_RIGHT, "CADENCE");
    snprintf(t, sizeof t, conn ? "%d" : "--", m->pm_live_w);
    ui_text(r, 8, 130, 0, 44, &font_c700_44, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    int b = ui_last_baseline;
    ui_text_base(r, 8 + ui_text_w(&font_c700_44, 0, t) + 4, b, &font_b600_16, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    int rw = ui_text_w(&font_b600_16, 0, "rpm");
    ui_text_base(r, 232, b, &font_b600_16, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, "rpm");
    snprintf(t, sizeof t, conn ? "%d" : "--", m->pm_live_rpm);
    ui_text_base(r, 232 - rw - 4, b, &font_c700_44, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_hline(r, 0, 175, 240, C_DIV);
    static const char *chip[] = {"READY", "IN PROGRESS", "ZERO SET", "FAILED", "UNSUPPORTED"};
    static const ui_chip_kind_t ck[] = {CHIP_OUTLINE, CHIP_PENDING, CHIP_OK, CHIP_BAD, CHIP_BAD};
    static const char *help[] = {"Hold the bike upright with the cranks still and unloaded.",
                                 "Keep the cranks still. This takes a few seconds.",
                                 "Zero offset set. Power should read about 0 W when still.",
                                 "No response. Turn the crank once to wake the meter, then retry.",
                                 "This meter cannot be zeroed over Bluetooth."};
    static const char *btn[] = {"CALIBRATE ZERO", "CALIBRATING\xE2\x80\xA6", "DONE", "RETRY", "CALIBRATE ZERO"};
    static const char *foot[] = {"CALIBRATE", "WAIT", "DONE", "RETRY", ""};
    int c = m->cal;
    ui_caps(r, 8, 183, 0, LV_TEXT_ALIGN_LEFT, "ZERO OFFSET");
    ui_chip(r, 232, 182, 16, &font_b700_10, ck[c], NULL, chip[c]);
    ui_text(r, 8, 198, 224, 17, &font_b500_13, C_STATUS, LV_TEXT_ALIGN_LEFT, 0, help[c]);
    ui_btn(r, 8, 236, 224, 44, btn[c], (c == UI_CAL_RUNNING || c == UI_CAL_UNSUPPORTED) ? BTN_DISABLED : BTN_PRIMARY, &font_b700_16, 0);
    ui_kebab_late(r);
    ui_footer(r, "BACK", FOOT_BACK, foot[c], c == UI_CAL_UNSUPPORTED ? FOOT_NONE : FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 12 Profile (FTP) */
void scr_profile(lv_obj_t *r, const ui_model_t *m)
{
    char t[48];
    int cur = m->v.ftp, ed = m->ftp_edit ? m->ftp_edit : cur;
    int changed = ed != cur;
    ui_status(r, m);
    ui_header44(r, 22, "Profile", &font_b700_18, 0);
    ui_caps(r, 8, 75, 0, LV_TEXT_ALIGN_LEFT, "FTP");
    ui_chip(r, 232, 74, 16, &font_b700_10, changed ? CHIP_PENDING : CHIP_OUTLINE, NULL, changed ? "CHANGED" : "CURRENT");
    snprintf(t, sizeof t, "%d", ed);
    int nw = ui_text_w(&font_c700_64, 0, t), uw = ui_text_w(&font_b600_20, 0, "W");
    int x0 = 120 - (nw + 6 + uw) / 2;
    ui_text(r, x0, 90, 0, 64, &font_c700_64, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_base(r, x0 + nw + 6, ui_last_baseline, &font_b600_20, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    if (changed) snprintf(t, sizeof t, "was %d W \xC2\xB7 %s%d W", cur, ed > cur ? "+" : "\xE2\x88\x92", ed > cur ? ed - cur : cur - ed);
    else snprintf(t, sizeof t, "No change");
    ui_text(r, 0, 154, 240, 14, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_CENTER, 0, t);
    static const char *st[] = {"\xE2\x88\x92" "5", "\xE2\x88\x92" "1", "+1", "+5"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b2 = ui_hit(r, 8 + i * 58, 178, 50, 48);
        lv_obj_set_style_bg_color(b2, lv_color_hex(C_PANEL), 0);
        lv_obj_set_style_bg_opa(b2, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b2, 10, 0);
        lv_obj_set_style_border_width(b2, 1, 0);
        lv_obj_set_style_border_color(b2, lv_color_hex(m->focus == i ? C_TEXT : C_BORDER), 0);
        ui_text(b2, -1, -1, 50, 48, &font_c700_24, C_TEXT, LV_TEXT_ALIGN_CENTER, 0, st[i]);
    }
    ui_text(r, 8, 238, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
            "Workouts and zones on this unit use this value. The phone app can set it too.");
    ui_footer(r, "BACK", FOOT_BACK, changed ? "SAVE" : "DONE", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ Profile menu (not a board, Q47)
 * Menu, Profile opens this list: FTP (the board 12 editor) and Max heart rate (the same editor for
 * bpm), with the five heart-rate zones below, as the Sensors list is laid out. */
void scr_profile_menu(lv_obj_t *r, const ui_model_t *m)
{
    char t[64];
    int mx = m->v.max_hr;
    ui_status(r, m);
    ui_header36(r, 22, "Profile", "");
    snprintf(t, sizeof t, "%u W \xC2\xB7 workouts and power zones", m->v.ftp);
    ui_chev(ui_row46(r, LIST_Y, "FTP", t, m->focus == 0, C_TEXT));
    if (mx) snprintf(t, sizeof t, "%d bpm \xC2\xB7 heart-rate zones", mx);
    else snprintf(t, sizeof t, "Off \xC2\xB7 set it to show zones");
    ui_chev(ui_row46(r, LIST_Y + 46, "Max heart rate", t, m->focus == 1, C_TEXT));
    int y = LIST_Y + 92 + 10;
    ui_caps(r, 8, y, 0, LV_TEXT_ALIGN_LEFT, "HEART-RATE ZONES");
    if (!mx) {
        ui_text(r, 8, y + 20, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
                "Set a max heart rate to see five zones. They colour HEART RATE on Main and the heart-rate graph.");
    } else {
        ui_caps(r, 232, y, 0, LV_TEXT_ALIGN_RIGHT, "OF MAX");
        static const char *pct[CC_HR_ZONES] = {"below 60 %", "60\xE2\x80\x93" "70 %", "70\xE2\x80\x93" "80 %",
                                               "80\xE2\x80\x93" "90 %", "90 % and up"};
        for (int z = 1; z <= CC_HR_ZONES; z++) {
            int ry = y + 18 + (z - 1) * 21;
            ui_zone_chip(r, 40, ry + 1, 18, &font_b700_12, z, cc_hr_zone_rgb[z]);
            int lo = cc_hr_zone_floor(mx, z), hi = z < CC_HR_ZONES ? cc_hr_zone_floor(mx, z + 1) - 1 : 0;
            if (z == 1) snprintf(t, sizeof t, "up to %d bpm", hi);
            else if (z == CC_HR_ZONES) snprintf(t, sizeof t, "%d bpm and up", lo);
            else snprintf(t, sizeof t, "%d\xE2\x80\x93%d bpm", lo, hi);
            ui_text(r, 50, ry, 0, 20, &font_b600_14, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
            ui_text(r, 232, ry, 0, 20, &font_b500_13, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, pct[z - 1]);
        }
    }
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ Max heart rate (not a board, Q47)
 * Laid out as board 12 (Profile, FTP): the value large, four steppers, SAVE applies, BACK discards.
 * "--" with "off" is not set: the large digits have no letters (fonts/gen_fonts.py LARGE_SET). */
void scr_max_hr(lv_obj_t *r, const ui_model_t *m)
{
    char t[64];
    int cur = m->v.max_hr, ed = m->maxhr_edit;
    int changed = ed != cur;
    ui_status(r, m);
    ui_header44(r, 22, "Profile", &font_b700_18, 0);
    ui_caps(r, 8, 75, 0, LV_TEXT_ALIGN_LEFT, "MAX HEART RATE");
    ui_chip(r, 232, 74, 16, &font_b700_10, changed ? CHIP_PENDING : CHIP_OUTLINE, NULL, changed ? "CHANGED" : "CURRENT");
    if (ed) snprintf(t, sizeof t, "%d", ed);
    else snprintf(t, sizeof t, "--");
    const char *unit = ed ? "bpm" : "off";
    int nw = ui_text_w(&font_c700_64, 0, t), uw = ui_text_w(&font_b600_20, 0, unit);
    int x0 = 120 - (nw + 6 + uw) / 2;
    ui_text(r, x0, 90, 0, 64, &font_c700_64, ed ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_base(r, x0 + nw + 6, ui_last_baseline, &font_b600_20, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, unit);
    if (!changed) snprintf(t, sizeof t, ed ? "No change" : "Zones off");
    else if (!cur) snprintf(t, sizeof t, "was off \xC2\xB7 zones on");
    else if (!ed) snprintf(t, sizeof t, "was %d bpm \xC2\xB7 zones off", cur);
    else snprintf(t, sizeof t, "was %d bpm \xC2\xB7 %s%d bpm", cur, ed > cur ? "+" : "\xE2\x88\x92", ed > cur ? ed - cur : cur - ed);
    ui_text(r, 0, 154, 240, 14, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_CENTER, 0, t);
    static const char *st[] = {"\xE2\x88\x92" "5", "\xE2\x88\x92" "1", "+1", "+5"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b2 = ui_hit(r, 8 + i * 58, 178, 50, 48);
        lv_obj_set_style_bg_color(b2, lv_color_hex(C_PANEL), 0);
        lv_obj_set_style_bg_opa(b2, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b2, 10, 0);
        lv_obj_set_style_border_width(b2, 1, 0);
        lv_obj_set_style_border_color(b2, lv_color_hex(m->focus == i ? C_TEXT : C_BORDER), 0);
        ui_text(b2, -1, -1, 50, 48, &font_c700_24, C_TEXT, LV_TEXT_ALIGN_CENTER, 0, st[i]);
    }
    ui_text(r, 8, 238, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
            "Five heart-rate zones, 50 to 100 % of this. Step below 100 bpm to turn them off.");
    ui_footer(r, "BACK", FOOT_BACK, changed ? "SAVE" : "DONE", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 13 Routes list */
void scr_routes(lv_obj_t *r, const ui_model_t *m)
{
    char t[64], d[16], a[16];
    int mi = m->v.settings.units == CC_UNITS_MI;
    ui_status(r, m);
    snprintf(t, sizeof t, "%d / %d", m->n_routes, CC_MAX_ROUTES);
    ui_header36(r, 22, "Routes", t);
    int s = clampi(m->scroll, 0, m->n_routes > LIST_ROWS ? m->n_routes - LIST_ROWS : 0);
    int i;
    for (i = s; i < m->n_routes && i < s + LIST_ROWS; i++) {
        ui_fmt_dist(d, sizeof d, m->routes[i].dist_m, mi);
        if (m->routes[i].has_profile) {
            ui_fmt_thousands(a, sizeof a, m->routes[i].ascent_m);
            snprintf(t, sizeof t, "%s %s · %s m · %d climb%s", d, mi ? "mi" : "km", a, m->routes[i].climbs,
                     m->routes[i].climbs == 1 ? "" : "s");
        } else {
            /* No profile: the object's ascent and climb count are 0 because nothing was known (Q35) */
            snprintf(t, sizeof t, "%s %s · no elevation", d, mi ? "mi" : "km");
        }
        ui_chev(ui_row46(r, LIST_Y + (i - s) * 46, m->routes[i].name, t, i == m->focus, C_TEXT));
    }
    if (i - s < LIST_ROWS)
        ui_text(r, 11, LIST_Y + (i - s) * 46 + 10, 218, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
                "Send routes from the phone app. Up to 10 are kept on the unit.");
    list_thumb(r, LIST_Y, m->n_routes, s);
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 14 Route detail */
static const ui_model_t *rd_model;

static void rprof_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const ui_model_t *m = rd_model;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int n = m->detail_prof_n;
    if (n < 2) return;
    int lo = m->detail_prof[0], hi = lo;
    for (int i = 1; i < n; i++) { if (m->detail_prof[i] < lo) lo = m->detail_prof[i]; if (m->detail_prof[i] > hi) hi = m->detail_prof[i]; }
    float span = hi - lo > 10 ? (float)(hi - lo) : 10.0f;
    float L = (float)(m->detail_prof_len_m ? m->detail_prof_len_m : 1);
    /* one 1 px column per x: no seams between segments */
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    for (int x = 0; x < 224; x++) {
        float d = L * ((float)x + 0.5f) / 224.0f;
        float fi = d / L * (float)(n - 1);
        int i = (int)fi;
        if (i >= n - 1) i = n - 2;
        float el = (float)m->detail_prof[i] + (float)(m->detail_prof[i + 1] - m->detail_prof[i]) * (fi - (float)i);
        int y = (int)(80.0f - (el - (float)lo) / span * 64.0f + 0.5f);
        int climb = 0;
        for (int c = 0; c < m->detail_n_climbs; c++)
            if (d >= (float)m->detail_climbs[c].start_m && d <= (float)(m->detail_climbs[c].start_m + m->detail_climbs[c].len_m))
                climb = 1;
        rd.bg_color = lv_color_hex(climb ? C_ORANGE : C_BORDER);
        lv_area_t ar = {a.x1 + x, a.y1 + y, a.x1 + x, a.y1 + 83};
        lv_draw_rect(layer, &rd, &ar);
    }
}

void scr_route_detail(lv_obj_t *r, const ui_model_t *m)
{
    char t[32], d[16];
    int mi = m->v.settings.units == CC_UNITS_MI;
    const ui_rt_item_t *rt = &m->routes[m->detail_route];
    ui_status(r, m);
    ui_header44(r, 22, rt->name, &font_b700_16, 0);
    lv_obj_t *o = ui_rect(r, 8, 76, 224, 84, C_BG, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    rd_model = m;
    lv_obj_add_event_cb(o, rprof_draw, LV_EVENT_DRAW_MAIN, NULL);
    ui_fmt_dist(d, sizeof d, rt->dist_m, mi);
    ui_text(r, 8, 160, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "0");
    snprintf(t, sizeof t, "%s %s", d, mi ? "mi" : "km");
    ui_text(r, 232, 160, 0, 12, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_hline(r, 0, 172, 240, C_DIV);
    stat3(r, 0, 173, "DIST", d, mi ? "mi" : "km", 1);
    if (rt->has_profile) ui_fmt_thousands(t, sizeof t, rt->ascent_m);
    else snprintf(t, sizeof t, "--");            /* unknown, not flat (Q35) */
    stat3(r, 80, 173, "ASCENT", t, "m", 1);
    snprintf(t, sizeof t, rt->has_profile ? "%d" : "--", rt->climbs);
    stat3(r, 160, 173, "CLIMBS", t, "", 0);
    ui_hline(r, 0, 220, 240, C_DIV);
    ui_btn(r, 8, 228, 224, 52, "LOAD ROUTE", ride_on(m) ? BTN_DISABLED : BTN_PRIMARY, &font_b700_18, 0);
    ui_footer(r, "BACK", FOOT_BACK, "LOAD", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 15 History */
void scr_history(lv_obj_t *r, const ui_model_t *m)
{
    char t[64], d[16], dt[16], hms[16];
    int mi = m->v.settings.units == CC_UNITS_MI;
    ui_status(r, m);
    snprintf(t, sizeof t, "%d ride%s", m->rides_total, m->rides_total == 1 ? "" : "s");
    ui_text(r, 8, 22, 0, 35, &font_b700_18, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, "History");
    ui_text(r, 232, 22, 0, 35, &font_b600_13, m->rides_total > CC_RIDES_KEEP ? C_YELLOW : C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_hline(r, 0, 57, 240, C_DIV);
    int s = clampi(m->scroll, 0, m->n_rides > LIST_ROWS ? m->n_rides - LIST_ROWS : 0);
    for (int i = s; i < m->n_rides && i < s + LIST_ROWS; i++) {
        const ui_ride_item_t *rd = &m->rides[i];
        ui_fmt_date(dt, sizeof dt, rd->start_local, 0);
        ui_fmt_dist(d, sizeof d, rd->dist_m, mi);
        snprintf(t, sizeof t, "%s \xC2\xB7 %s %s", dt, d, mi ? "mi" : "km");
        char sub[40];
        snprintf(hms, sizeof hms, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, rd->timer_s / 3600, (rd->timer_s / 60) % 60, rd->timer_s % 60);
        if (rd->has_power) snprintf(sub, sizeof sub, "%s \xC2\xB7 avg %d W", hms, rd->avg_power);
        else snprintf(sub, sizeof sub, "%s", hms);
        lv_obj_t *row = ui_row46(r, LIST_Y + (i - s) * 46, t, sub, i == m->focus, C_TEXT);
        for (int c = 1; c <= 2; c++) /* name and sub stop short of the chip */
            lv_obj_set_width(lv_obj_get_child(row, -c), 132);
        ui_chip(row, 228, 14, 18, &font_b700_10, rd->uploaded ? CHIP_NEUTRAL : CHIP_PENDING,
                rd->uploaded ? &ic_check_10 : &ic_clock_10, rd->uploaded ? "UPLOADED" : "WAITING");
    }
    if (m->n_rides == 0)
        ui_text(r, 11, LIST_Y + 10, 218, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "No rides yet.");
    list_thumb(r, LIST_Y, m->rides_total, s);
    ui_footer(r, "BACK", FOOT_BACK, "OPEN", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 16 Ride summary */
void scr_summary(lv_obj_t *r, const ui_model_t *m)
{
    char t[40];
    int mi = m->v.settings.units == CC_UNITS_MI;
    const cc_fit_summary_t *s = &m->summary;
    ui_status(r, m);
    ui_text(r, 8, 22, 0, 39, &font_b700_20, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, "Ride saved");
    ui_hline(r, 0, 61, 240, C_DIV);
    char dt[24], ck[8];
    ui_fmt_date(dt, sizeof dt, m->summary_ride.start_local, 1);
    ui_fmt_clock(ck, sizeof ck, m->summary_ride.start_local);
    snprintf(t, sizeof t, "%s \xC2\xB7 %s", dt, ck);
    ui_text(r, 8, 62, 0, 27, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_chip(r, 232, 66, 18, &font_b700_10, m->summary_uploaded ? CHIP_OUTLINE : CHIP_PENDING, NULL,
            m->summary_uploaded ? "UPLOADED" : "WAITING FOR PHONE");
    ui_hline(r, 0, 89, 240, C_DIV);
    char v[6][16];
    ui_fmt_hms(v[0], 16, s->timer_s);
    ui_fmt_dist(v[1], 16, s->distance_m, mi);
    snprintf(v[2], 16, s->has_power ? "%u" : "--", s->avg_power);
    snprintf(v[3], 16, s->has_hr ? "%u" : "--", s->avg_hr);
    snprintf(v[4], 16, s->has_ascent ? "%u" : "--", s->ascent_m);
    ui_fmt_speed(v[5], 16, s->avg_speed_mms, mi);
    const char *lbl[6] = {"TIME", "DISTANCE", "AVG POWER", "AVG HEART RATE", "ASCENT", "AVG SPEED"};
    const char *un[6] = {"", mi ? "mi" : "km", "W", "bpm", "m", mi ? "mph" : "km/h"};
    for (int i = 0; i < 6; i++) {
        int x = (i % 2) * 120, y = 90 + (i / 2) * 66;
        if (i % 2 == 0) ui_vline(r, 119, y, 66, C_DIV);
        ui_hline(r, x, y + 65, 120, C_DIV);
        ui_caps(r, x + 8, y + 8, 0, LV_TEXT_ALIGN_LEFT, lbl[i]);
        ui_text(r, x + 8, y + 26, 0, 36, &font_c600_34, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, v[i]);
        int b = ui_last_baseline;
        if (*un[i])
            ui_text_base(r, x + 8 + ui_text_w(&font_c600_34, 0, v[i]) + 4, b, &font_b600_13, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, un[i]);
    }
    ui_footer(r, "BACK", FOOT_BACK, "DONE", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 18 Pause menu */
void scr_pause_menu(lv_obj_t *r, const ui_model_t *m)
{
    char t[16];
    ui_status(r, m);
    ui_fmt_hms(t, sizeof t, m->v.timer_s);
    ui_header36(r, 22, "Ride paused", t);
    ui_chev(ui_row46(r, LIST_Y, "Resume", "Keep recording", m->v.focus == CC_PM_RESUME, C_TEXT));
    ui_chev(ui_row46(r, LIST_Y + 46, "Finish and save", "Stop and save this ride", m->v.focus == CC_PM_FINISH, C_TEXT));
    ui_chev(ui_row46(r, LIST_Y + 92, "Discard ride", "Delete without saving", m->v.focus == CC_PM_DISCARD, C_RED_TEXT));
    ui_footer(r, "BACK", FOOT_BACK, "SELECT", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 19 Confirm */
void scr_confirm(lv_obj_t *r, const ui_model_t *m)
{
    char msg[160], d[16], hms[16];
    static const char *title[] = {"Discard ride?", "Forget sensor?", "Forget phone?"};
    static const char *destr[] = {"DISCARD RIDE", "FORGET SENSOR", "FORGET PHONE"};
    static const char *safe[] = {"KEEP RIDE", "KEEP SENSOR", "KEEP PHONE"};
    int k = m->confirm;
    ui_status(r, m);
    ui_header44(r, 22, title[k], &font_b700_18, 0);
    if (k == UI_CONFIRM_DISCARD) {
        ui_fmt_hms(hms, sizeof hms, m->v.timer_s);
        ui_fmt_dist(d, sizeof d, m->v.dist_m, m->v.settings.units == CC_UNITS_MI);
        snprintf(msg, sizeof msg, "This permanently deletes the ride (%s, %s %s). It has not been uploaded.", hms, d,
                 m->v.settings.units == CC_UNITS_MI ? "mi" : "km");
    } else if (k == UI_CONFIRM_FORGET_SENSOR) {
        int hr = m->forget_kind == 2; /* the heart-rate screen names its strap "Sensor" (board 22) */
        snprintf(msg, sizeof msg, "%s \xC2\xB7\xC2\xB7\xC2\xB7%s will be removed from this unit. You can pair it again from Sensors.",
                 hr ? "Sensor" : m->pm_name, hr ? m->hr_id4 : m->pm_id4);
    } else {
        snprintf(msg, sizeof msg, "The phone must be paired again before it can send workouts or routes, or collect rides.");
    }
    ui_text(r, 10, 78, 220, 19, &font_b500_14, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, msg);
    int focus = k == UI_CONFIRM_DISCARD ? m->v.focus : m->focus; /* KEEP is 0, the destructive button 1 */
    /* focus order: the safe button first (spec 6, Confirm) */
    lv_obj_t *sb = ui_btn(r, 8, 228, 224, 52, safe[k], BTN_OUTLINE, &font_b700_16, focus == 0);
    lv_obj_t *db = ui_btn(r, 8, 168, 224, 52, destr[k], BTN_DANGER, &font_b700_16, focus == 1);
    (void)sb; (void)db;
    ui_footer(r, "BACK", FOOT_BACK, "KEEP", FOOT_PLAIN);
}

/* ------------------------------------------------------------------ 22 Heart rate sensor */
void scr_heart_rate(lv_obj_t *r, const ui_model_t *m)
{
    char t[32];
    int conn = m->v.hr_state == CC_SENS_OK;
    ui_status(r, m);
    ui_header44(r, 22, "Heart rate", &font_b700_18, 1);
    snprintf(t, sizeof t, "Sensor \xC2\xB7\xC2\xB7\xC2\xB7%s", m->hr_id4);
    identity(r, t, "Bluetooth", conn ? CHIP_NEUTRAL : CHIP_PENDING, conn ? "CONNECTED" : "SEARCHING");
    ui_caps(r, 8, 116, 0, LV_TEXT_ALIGN_LEFT, "LIVE HEART RATE");
    snprintf(t, sizeof t, conn ? "%d" : "--", m->hr_live);
    ui_text(r, 8, 130, 0, 58, &font_c700_56, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    int b = ui_last_baseline;
    ui_text_base(r, 8 + ui_text_w(&font_c700_56, 0, t) + 6, b, &font_b600_18, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "bpm");
    ui_hline(r, 0, 187, 240, C_DIV);
    ui_caps(r, 8, 203, 0, LV_TEXT_ALIGN_LEFT, "SIGNAL");
    ui_bars(r, 212, 202, conn ? m->hr_bars : 0);
    ui_hline(r, 0, 231, 240, C_DIV);
    ui_text(r, 8, 240, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
            conn ? "Wet the strap contacts if the reading looks wrong. The strap wakes when worn."
                 : "Put the strap on to wake it. The unit keeps looking and reconnects by itself.");
    ui_kebab_late(r);
    ui_footer(r, "BACK", FOOT_BACK, "", FOOT_NONE);
}

/* ------------------------------------------------------------------ 23 Phone */
void scr_phone(lv_obj_t *r, const ui_model_t *m)
{
    char t[32];
    ui_status(r, m);
    ui_header44(r, 22, "Phone", &font_b700_18, m->phone == UI_PHONE_CONNECTED);
    if (m->phone == UI_PHONE_CONNECTED)
        identity(r, m->phone_name, "Paired \xC2\xB7 Bluetooth", CHIP_NEUTRAL, "CONNECTED");
    else if (m->phone == UI_PHONE_PAIRING)
        identity(r, "Pairing\xE2\x80\xA6", "Waiting for the phone", CHIP_PENDING, "PAIRING");
    else
        identity(r, "No phone paired", "For workouts and routes", CHIP_NEUTRAL, "NOT PAIRED");
    const char *hint;
    if (m->phone == UI_PHONE_CONNECTED) {
        ui_caps(r, 8, 125, 0, LV_TEXT_ALIGN_LEFT, "LAST SYNC");
        ui_text(r, 232, 110, 0, 43, &font_b600_14, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, m->last_sync);
        ui_hline(r, 0, 153, 240, C_DIV);
        ui_caps(r, 8, 169, 0, LV_TEXT_ALIGN_LEFT, "RIDES WAITING");
        snprintf(t, sizeof t, "%d", m->rides_waiting);
        ui_text(r, 232, 154, 0, 43, &font_b600_14, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, t);
        ui_hline(r, 0, 197, 240, C_DIV);
        hint = "Open the CC700 app on your phone to send workouts and routes.";
    } else if (m->phone == UI_PHONE_PAIRING) {
        ui_text(r, 0, 120, 240, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_CENTER, 1, "PAIRING CODE");
        snprintf(t, sizeof t, "%03" PRIu32 " %03" PRIu32, m->passkey / 1000, m->passkey % 1000);
        ui_text(r, 0, 138, 240, 62, &font_c700_60, C_TEXT, LV_TEXT_ALIGN_CENTER, 0, t);
        hint = "Enter this code in the CC700 app on your phone. Pairing stops after 60 seconds.";
    } else {
        ui_btn(r, 8, 154, 224, 44, "PAIR PHONE", BTN_PRIMARY, &font_b700_16, 0);
        hint = "Open the CC700 app, choose Add unit, then enter the code shown here.";
    }
    ui_hline(r, 0, 241, 240, C_DIV);
    ui_text(r, 8, 250, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, hint);
    ui_kebab_late(r);
    if (m->phone == UI_PHONE_PAIRING) ui_footer(r, "CANCEL", FOOT_BACK, "", FOOT_NONE);
    else if (m->phone == UI_PHONE_NOT_PAIRED) ui_footer(r, "BACK", FOOT_BACK, "PAIR", FOOT_PLAIN);
    else ui_footer(r, "BACK", FOOT_BACK, "", FOOT_NONE);
}

/* ------------------------------------------------------------------ 24 Settings */
void scr_settings(lv_obj_t *r, const ui_model_t *m)
{
    const cc_settings_t *s = &m->v.settings;
    static const char *bright[] = {"Auto", "25%", "50%", "75%", "100%"};
    static const char *off[] = {"Never", "30 s", "1 min", "5 min"};
    char tz[16];
    int tzm = s->tz_min;
    snprintf(tz, sizeof tz, "UTC%c%d:%02d", tzm < 0 ? '-' : '+', (tzm < 0 ? -tzm : tzm) / 60, (tzm < 0 ? -tzm : tzm) % 60);
    const char *names[6] = {"Brightness", "Screen off", "Units", "Auto-pause", "Time zone", "Firmware"};
    const char *subs[6] = {"", "While stopped", "Distance and speed", "Stop recording when still", "Set by the phone", ""};
    const char *vals[6] = {bright[s->brightness % 5], off[s->screen_off % 4], s->units == CC_UNITS_MI ? "mi" : "km",
                           s->autopause ? "On" : "Off", tz, CC_FW_STRING};
    ui_status(r, m);
    ui_header36(r, 22, "Settings", "");
    int sc = clampi(m->scroll, 0, 6 - LIST_ROWS);
    for (int i = sc; i < 6 && i < sc + LIST_ROWS; i++) {
        lv_obj_t *row = ui_row46(r, LIST_Y + (i - sc) * 46, names[i], subs[i], i == m->focus, C_TEXT);
        int cyc = i < 4;
        ui_text(row, cyc ? 208 : 228, 0, 0, 45, &font_b600_14, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, vals[i]);
        if (cyc) ui_icon(row, &ic_cycle_14, 214, 16, C_MUTED);
    }
    list_thumb(r, LIST_Y, 6, sc);
    ui_footer(r, "BACK", FOOT_BACK, "CHANGE", FOOT_PLAIN);
}
