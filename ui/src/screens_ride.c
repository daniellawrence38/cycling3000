/* Ride pages (boards 1 to 4), Paused (17), Ride alerts (20, the overlays) and Empty states (21). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cc700/zones.h"
#include "ui.h"

static const char *DASH = "--";

static int page_index(const ui_model_t *m, cc_page_t p)
{
    for (int i = 0; i < m->v.ring_n; i++)
        if (m->v.ring[i] == p) return i;
    return 0;
}

static int ring_n(const ui_model_t *m) { return m->v.ring_n > 0 ? m->v.ring_n : 4; }

static void zone_chip(lv_obj_t *r, int xr, int y, int h, const lv_font_t *f, int zone)
{
    char t[12];
    if (zone >= 1) snprintf(t, sizeof t, "Z%d", zone);
    else snprintf(t, sizeof t, "--");
    int tw = ui_text_w(f, 0, t);
    int w = tw + 16;
    ui_rect(r, xr - w, y, w, h, zone >= 1 ? cc_zone_rgb[zone] : C_PANEL, 5);
    ui_text(r, xr - w + 8, y, 0, h, f, zone >= 1 ? C_BG : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
}

/* ------------------------------------------------------------------ 1 Main */
static void main_cell(lv_obj_t *r, int x, int y, int h, const char *lbl, const char *val, const char *unit, int right,
                      int bottom)
{
    if (right) ui_vline(r, x + 119, y, h, C_DIV);
    if (bottom) ui_hline(r, x, y + h - 1, 120, C_DIV);
    ui_caps(r, x + 8, y + 4, 0, LV_TEXT_ALIGN_LEFT, lbl);
    int row_bot = y + 4 + 14 + 34;
    lv_obj_t *n = ui_text(r, x + 8, row_bot - 34, 0, 34, &font_c600_36, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, val);
    if (unit && *unit) {
        int w = ui_text_w(&font_c600_36, 0, val);
        ui_text_bottom(r, x + 8 + w + 4, row_bot - 3, &font_b600_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, unit);
        /* unit: CSS normal line box ends 3 px above the row bottom */
    }
    (void)n;
}

void scr_main(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    int mi = v->settings.units == CC_UNITS_MI;
    ui_status(r, m);
    /* hero: POWER · 3S */
    ui_caps(r, 8, 30, 0, LV_TEXT_ALIGN_LEFT, "POWER \xC2\xB7 3S");
    char pw[12];
    if (v->power3_ok) snprintf(pw, sizeof pw, "%d", (int)v->power3);
    else snprintf(pw, sizeof pw, "%s", DASH);
    ui_text(r, 8, 44, 0, 84, &font_c700_96, v->power3_ok ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, pw);
    int nw = ui_text_w(&font_c700_96, 0, pw);
    ui_text_bottom(r, 8 + nw + 6, 115, &font_b600_22, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    int zh = lv_font_get_line_height(&font_b700_20) + 2;
    zone_chip(r, 232, 44 + (83 - zh) / 2, zh, &font_b700_20, v->zone);
    for (int i = 0; i < 7; i++) {
        int x0 = 8 + (i * 226) / 7, x1 = 8 + ((i + 1) * 226) / 7 - 2;
        ui_rect(r, x0, 127, x1 - x0, 8, (v->zone == i + 1) ? cc_zone_rgb[i + 1] : C_PANEL, 2);
    }
    ui_hline(r, 0, 139, 240, C_DIV);
    /* 2 x 3 grid */
    char hr[8], cad[8], spd[12], dist[12], el[16], lap[16];
    snprintf(hr, sizeof hr, v->hr_ok ? "%d" : "--", (int)v->hr);
    snprintf(cad, sizeof cad, v->cad_ok ? "%d" : "--", (int)v->cad);
    if (v->speed_ok) ui_fmt_speed(spd, sizeof spd, v->speed_mms, mi); else snprintf(spd, sizeof spd, "--");
    ui_fmt_dist(dist, sizeof dist, v->dist_m, mi);
    ui_fmt_hms(el, sizeof el, v->timer_s);
    ui_fmt_hms(lap, sizeof lap, v->lap_s);
    int ys[4] = {140, 193, 247, 300};
    main_cell(r, 0, ys[0], ys[1] - ys[0], "HEART RATE", hr, "bpm", 1, 1);
    main_cell(r, 120, ys[0], ys[1] - ys[0], "CADENCE", cad, "rpm", 0, 1);
    main_cell(r, 0, ys[1], ys[2] - ys[1], "SPEED", spd, mi ? "mph" : "km/h", 1, 1);
    main_cell(r, 120, ys[1], ys[2] - ys[1], "DISTANCE", dist, mi ? "mi" : "km", 0, 1);
    main_cell(r, 0, ys[2], ys[3] - ys[2], "ELAPSED", el, "", 1, 0);
    main_cell(r, 120, ys[2], ys[3] - ys[2], "LAP TIME", lap, "", 0, 0);
    ui_dots(r, ring_n(m), page_index(m, CC_PAGE_MAIN));
}

/* ------------------------------------------------------------------ 2 Workout */
void scr_workout(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    const cc_wo_view_t *w = &v->wo;
    char t[48];
    ui_status(r, m);
    if (!w->complete) {
        ui_caps(r, 8, 27, 0, LV_TEXT_ALIGN_LEFT, "TIME REMAINING");
        snprintf(t, sizeof t, "INTERVAL %d / %d", w->step + 1, w->nsteps);
        ui_caps(r, 232, 27, 0, LV_TEXT_ALIGN_RIGHT, t);
        ui_fmt_hms(t, sizeof t, w->remaining_s);
        ui_text(r, 8, 41, 0, 42, &font_c700_48, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
        char lab[20];
        snprintf(lab, sizeof lab, "%s", w->label ? w->label : "");
        for (char *c = lab; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        ui_text(r, 232, 41, 0, 42, &font_b700_15, C_TEXT, LV_TEXT_ALIGN_RIGHT, 1, lab);
        ui_rect(r, 8, 87, 224, 6, C_PANEL, 3);
        int pct = w->step_secs ? (int)(224 * w->elapsed_s / w->step_secs) : 0;
        if (pct > 0) ui_rect(r, 8, 87, pct, 6, C_TEXT, 3);
    } else {
        ui_text(r, 8, 27, 0, 24, &font_b700_20, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, "Workout complete");
        ui_text(r, 8, 52, 224, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
                "Keep riding, or pause and finish the ride from the menu.");
    }
    ui_hline(r, 0, 93, 240, C_DIV);
    /* power and target */
    ui_caps(r, 8, 100, 0, LV_TEXT_ALIGN_LEFT, "POWER \xC2\xB7 3S");
    char pw[12];
    snprintf(pw, sizeof pw, v->power3_ok ? "%d" : "--", (int)v->power3);
    ui_text(r, 8, 114, 0, 70, &font_c700_76, v->power3_ok ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, pw);
    ui_text_bottom(r, 8 + ui_text_w(&font_c700_76, 0, pw) + 4, 174, &font_b600_18, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    ui_caps(r, 232, 129, 0, LV_TEXT_ALIGN_RIGHT, "TARGET");
    snprintf(t, sizeof t, "%d\xE2\x80\x93%d", (int)w->lo_w, (int)w->hi_w);
    ui_text(r, 232, 143, 0, 26, &font_c600_24, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_hline(r, 0, 187, 240, C_DIV);
    /* status pill */
    uint32_t bg = C_PANEL;
    const lv_image_dsc_t *ic = NULL;
    const char *txt = "", *sub = "";
    char subb[16];
    switch (w->pill) {
    case CC_PILL_IN: bg = C_GREEN; ic = &ic_check_14; txt = "IN RANGE"; sub = "on target"; break;
    case CC_PILL_HIGH: bg = C_ORANGE; ic = &ic_up_14; txt = "TOO HIGH"; snprintf(subb, sizeof subb, "+%d W", (int)w->pill_delta); sub = subb; break;
    case CC_PILL_LOW: bg = C_BLUE; ic = &ic_down_14; txt = "TOO LOW"; snprintf(subb, sizeof subb, "\xE2\x88\x92%d W", (int)w->pill_delta); sub = subb; break;
    default: break;
    }
    if (!w->complete) {
        ui_rect(r, 8, 194, 224, 24, bg, 6);
        if (ic) ui_icon(r, ic, 16, 199, C_BG);
        ui_text(r, 36, 194, 0, 24, &font_b700_13, C_BG, LV_TEXT_ALIGN_LEFT, 1, txt);
        ui_text(r, 224, 194, 0, 24, &font_b600_13, C_BG, LV_TEXT_ALIGN_RIGHT, 1, sub);
    }
    /* target zone against actual zone */
    snprintf(t, sizeof t, "TARGET Z%d", w->target_zone);
    ui_caps(r, 8, 222, 0, LV_TEXT_ALIGN_LEFT, t);
    if (w->actual_zone) snprintf(t, sizeof t, "ACTUAL Z%d", w->actual_zone);
    else snprintf(t, sizeof t, "ACTUAL --");
    ui_caps(r, 232, 222, 0, LV_TEXT_ALIGN_RIGHT, t);
    for (int i = 0; i < 7; i++) {
        int x0 = 8 + (i * 227) / 7, x1 = 8 + ((i + 1) * 227) / 7 - 3;
        int live = w->actual_zone == i + 1;
        lv_obj_t *s = ui_frame(r, x0, 240, x1 - x0, 18, live ? cc_zone_rgb[i + 1] : C_PANEL, 4, 2,
                               w->target_zone == i + 1 ? C_TEXT : (live ? cc_zone_rgb[i + 1] : C_PANEL));
        char z[4];
        snprintf(z, sizeof z, "Z%d", i + 1);
        ui_text(s, -2, -2, x1 - x0, 18, &font_b700_11, live ? C_BG : C_MUTED, LV_TEXT_ALIGN_CENTER, 0, z);
    }
    ui_hline(r, 0, 263, 240, C_DIV);
    /* cadence */
    ui_caps(r, 8, 275, 0, LV_TEXT_ALIGN_LEFT, "CADENCE");
    char cad[16];
    snprintf(cad, sizeof cad, v->cad_ok ? "%d" : "--", (int)v->cad);
    const char *unit = "rpm";
    char ub[24];
    if (w->rpm) { snprintf(ub, sizeof ub, "/ %d rpm", w->rpm); unit = ub; }
    int uw = ui_text_w(&font_b600_13, 0, unit);
    ui_text(r, 232 - uw - 4, 266, 0, 32, &font_c600_32, C_TEXT, LV_TEXT_ALIGN_RIGHT, 0, cad);
    ui_text_base(r, 232, ui_last_baseline, &font_b600_13, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, unit);
    ui_dots(r, ring_n(m), page_index(m, CC_PAGE_WORKOUT));
}

/* ------------------------------------------------------------------ 3 Climb */
typedef struct { float y[24]; int n; float pos; } prof_t;

static void prof_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const prof_t *p = lv_obj_get_user_data(o);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
    if (p->n < 2) return;
    float mx = p->pos * (float)(W - 1);
    lv_draw_rect_dsc_t cd;
    lv_draw_rect_dsc_init(&cd);
    for (int x = 0; x < W; x++) { /* 1 px columns: no seams */
        float fi = (float)x / (float)(W - 1) * (float)(p->n - 1);
        int i = (int)fi;
        if (i >= p->n - 1) i = p->n - 2;
        float y = p->y[i] + (p->y[i + 1] - p->y[i]) * (fi - (float)i);
        cd.bg_color = lv_color_hex((float)x <= mx ? C_ORANGE : C_PANEL);
        cd.bg_opa = (float)x <= mx ? (lv_opa_t)217 : LV_OPA_COVER;
        lv_area_t ar = {a.x1 + x, a.y1 + (int32_t)(y + 0.5f), a.x1 + x, a.y1 + H - 1};
        lv_draw_rect(layer, &cd, &ar);
    }
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(C_TEXT);
    ld.width = 2;
    ld.round_start = ld.round_end = 1;
    for (int i = 0; i + 1 < p->n; i++) {
        ld.p1.x = a.x1 + (float)(W - 1) * (float)i / (float)(p->n - 1); ld.p1.y = a.y1 + p->y[i];
        ld.p2.x = a.x1 + (float)(W - 1) * (float)(i + 1) / (float)(p->n - 1); ld.p2.y = a.y1 + p->y[i + 1];
        lv_draw_line(layer, &ld);
    }
    float fi = p->pos * (float)(p->n - 1);
    int i = (int)fi;
    if (i >= p->n - 1) i = p->n - 2;
    float my = p->y[i] + (p->y[i + 1] - p->y[i]) * (fi - (float)i);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.bg_color = lv_color_hex(C_TEXT);
    rd.radius = LV_RADIUS_CIRCLE;
    rd.border_width = 2;
    rd.border_color = lv_color_hex(C_BG);
    lv_area_t c = {(int32_t)(a.x1 + mx - 6), (int32_t)(a.y1 + my - 6), (int32_t)(a.x1 + mx + 6), (int32_t)(a.y1 + my + 6)};
    lv_draw_rect(layer, &rd, &c);
}

static prof_t climb_prof;

void scr_climb(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    const cc_nav_view_t *n = &v->nav;
    char t[48];
    ui_status(r, m);
    ui_caps(r, 8, 29, 0, LV_TEXT_ALIGN_LEFT, "GRADIENT");
    if (v->steep) {
        int w = ui_text_w(&font_b700_11, 1, "STEEP") + 12;
        ui_rect(r, 232 - w, 28, w, 16, C_ORANGE, 4);
        ui_text(r, 232 - w + 6, 28, 0, 16, &font_b700_11, C_BG, LV_TEXT_ALIGN_LEFT, 1, "STEEP");
    }
    if (v->grad_ok) {
        int g = (int)v->grad_x10;
        snprintf(t, sizeof t, "%s%d.%d", g < 0 ? "\xE2\x88\x92" : "", (g < 0 ? -g : g) / 10, (g < 0 ? -g : g) % 10);
    } else {
        snprintf(t, sizeof t, "--");
    }
    uint32_t gc = v->steep ? C_ORANGE : (v->grad_ok ? C_TEXT : C_MUTED);
    ui_text(r, 8, 44, 0, 66, &font_c700_76, gc, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_base(r, 8 + ui_text_w(&font_c700_76, 0, t) + 4, ui_last_baseline, &font_b600_30, gc, LV_TEXT_ALIGN_LEFT, 0, "%");
    ui_hline(r, 0, 113, 240, C_DIV);
    ui_caps(r, 8, 120, 0, LV_TEXT_ALIGN_LEFT, "POWER \xC2\xB7 10S");
    snprintf(t, sizeof t, v->power10_ok ? "%d" : "--", (int)v->power10);
    ui_text(r, 8, 134, 0, 40, &font_c700_46, v->power10_ok ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_bottom(r, 8 + ui_text_w(&font_c700_46, 0, t) + 4, 168, &font_b600_16, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    int zh = lv_font_get_line_height(&font_b700_18) + 2;
    zone_chip(r, 232, 134 + (40 - zh) / 2, zh, &font_b700_18,
              v->power10_ok ? cc_zone_of_power(v->power10, v->ftp ? v->ftp : 200) : 0);
    ui_hline(r, 0, 175, 240, C_DIV);
    if (n->climb_show && m->climb_prof_n >= 2) {
        climb_prof.n = m->climb_prof_n;
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < m->climb_prof_n; i++) {
            if (m->climb_prof[i] < lo) lo = m->climb_prof[i];
            if (m->climb_prof[i] > hi) hi = m->climb_prof[i];
        }
        float span = hi - lo > 1 ? hi - lo : 1;
        for (int i = 0; i < m->climb_prof_n; i++)
            climb_prof.y[i] = 58.0f - (m->climb_prof[i] - lo) / span * 52.0f;
        climb_prof.pos = m->climb_pos;
        lv_obj_t *o = ui_rect(r, 8, 179, 224, 64, C_BG, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_set_user_data(o, &climb_prof);
        lv_obj_add_event_cb(o, prof_draw, LV_EVENT_DRAW_MAIN, NULL);
        ui_text(r, 14, 183, 0, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, n->climb_summary);
        ui_hline(r, 0, 247, 240, C_DIV);
        ui_vline(r, 119, 248, 52, C_DIV);
        ui_caps(r, 8, 252, 0, LV_TEXT_ALIGN_LEFT, "DIST LEFT");
        ui_caps(r, 128, 252, 0, LV_TEXT_ALIGN_LEFT, "ASCENT LEFT");
        int mi = v->settings.units == CC_UNITS_MI;
        ui_fmt_dist(t, sizeof t, n->climb_dist_left_m, mi);
        ui_text(r, 8, 266, 0, 34, &font_c600_32, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
        int b = ui_last_baseline;
        ui_text_base(r, 8 + ui_text_w(&font_c600_32, 0, t) + 4, b, &font_b600_13, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, mi ? "mi" : "km");
        snprintf(t, sizeof t, "%u", n->climb_ascent_left_m);
        ui_text_base(r, 128, b, &font_c600_32, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
        ui_text_base(r, 128 + ui_text_w(&font_c600_32, 0, t) + 4, b, &font_b600_13, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "m");
    } else {
        /* no route, off route or no climbs left: live ascent only (spec 5) */
        ui_caps(r, 8, 182, 0, LV_TEXT_ALIGN_LEFT, "ASCENT");
        snprintf(t, sizeof t, v->alt_ok ? "%u" : "--", v->ascent_m);
        ui_text(r, 8, 196, 0, 44, &font_c600_46, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
        int b = ui_last_baseline;
        ui_text_base(r, 8 + ui_text_w(&font_c600_46, 0, t) + 4, b, &font_b600_16, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "m");
    }
    ui_dots(r, ring_n(m), page_index(m, CC_PAGE_CLIMB));
}

/* ------------------------------------------------------------------ 4 Navigation */
typedef struct { const ui_model_t *m; } map_ctx_t;
static map_ctx_t map_ctx;

static void map_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const ui_model_t *m = map_ctx.m;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int W = lv_area_get_width(&a), H = lv_area_get_height(&a);
    float scale = m->v.nav.map_scale_m ? (float)W / (float)m->v.nav.map_scale_m : 0.6f; /* px per m */
    float rx = a.x1 + W / 2.0f, ry = a.y1 + H * 0.6f;
    int off = m->v.nav.off_route;
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.width = 3;
    ld.round_start = ld.round_end = 1;
    ld.color = lv_color_hex(C_ROUTE);
    if (off) { ld.dash_width = 9; ld.dash_gap = 7; }
    for (int i = 0; i + 1 < m->map_n; i++) {
        int ridden = i < m->map_ridden;
        ld.opa = off ? (lv_opa_t)140 : (ridden ? (lv_opa_t)102 : LV_OPA_COVER);
        ld.round_start = ld.round_end = !ridden && !off; /* overlapping caps would stack the 40 % opacity */
        ld.p1.x = rx + m->map_x[i] * scale; ld.p1.y = ry - m->map_y[i] * scale;
        ld.p2.x = rx + m->map_x[i + 1] * scale; ld.p2.y = ry - m->map_y[i + 1] * scale;
        lv_draw_line(layer, &ld);
    }
    /* rider marker: an arrow pointing along the course (north up) */
    float c = m->course_deg * 3.14159265f / 180.0f, s = sinf(c), k = cosf(c);
    float pts[4][2] = {{0, -10}, {9, 10}, {0, 4}, {-9, 10}};
    lv_point_precise_t q[4];
    for (int i = 0; i < 4; i++) {
        q[i].x = rx + pts[i][0] * k - pts[i][1] * s;
        q[i].y = ry + pts[i][0] * s + pts[i][1] * k;
    }
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    td.color = lv_color_hex(C_TEXT);
    td.p[0] = q[0]; td.p[1] = q[1]; td.p[2] = q[2];
    lv_draw_triangle(layer, &td);
    td.p[0] = q[0]; td.p[1] = q[2]; td.p[2] = q[3];
    lv_draw_triangle(layer, &td);
    ld.width = 2; ld.dash_width = 0; ld.dash_gap = 0; ld.opa = LV_OPA_COVER;
    ld.color = lv_color_hex(C_BG);
    for (int i = 0; i < 4; i++) {
        ld.p1 = q[i]; ld.p2 = q[(i + 1) % 4];
        lv_draw_line(layer, &ld);
    }
    /* scale bar: a quarter of the view */
    uint32_t bar_m = m->v.nav.map_scale_m ? m->v.nav.map_scale_m / 4 : 100;
    float bw = (float)bar_m * scale;
    ld.color = lv_color_hex(C_MUTED);
    ld.round_start = ld.round_end = 0;
    ld.p1.x = a.x1 + 8; ld.p1.y = a.y1 + 150; ld.p2.x = a.x1 + 8 + bw; ld.p2.y = a.y1 + 150;
    lv_draw_line(layer, &ld);
    ld.p1.y = a.y1 + 146; ld.p2.y = a.y1 + 154; ld.p2.x = ld.p1.x;
    lv_draw_line(layer, &ld);
    ld.p1.x = ld.p2.x = a.x1 + 8 + bw;
    lv_draw_line(layer, &ld);
}

void scr_nav(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    const cc_nav_view_t *n = &v->nav;
    int mi = v->settings.units == CC_UNITS_MI;
    char t[64];
    ui_status(r, m);
    /* map first so the banner and strip sit above it */
    lv_obj_t *map = ui_rect(r, 0, 98, 240, 158, C_BG, 0);
    lv_obj_set_style_clip_corner(map, 1, 0);
    map_ctx.m = m;
    lv_obj_add_event_cb(map, map_draw, LV_EVENT_DRAW_MAIN_END, NULL);
    snprintf(t, sizeof t, "%u m", n->map_scale_m ? n->map_scale_m / 4 : 100);
    if (n->map_scale_m / 4 >= 1000) snprintf(t, sizeof t, "%u.%u km", n->map_scale_m / 4000, (n->map_scale_m / 400) % 10);
    ui_text(r, 8, 98 + 131, 0, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    /* turn banner */
    int near = !n->off_route && n->turn_idx >= 0 && n->dist_to_turn_m <= 100;
    uint32_t tile = n->off_route ? C_YELLOW : near ? C_ROUTE : C_PANEL;
    uint32_t tfg = n->off_route || near ? C_BG : C_TEXT;
    ui_rect(r, 8, 31, 58, 58, tile, 10);
    ui_icon(r, n->off_route ? &ic_reroute_40 : &ic_turn_r_40, 17, 40, tfg);
    if (n->off_route) {
        ui_text(r, 76, 30, 0, 44, &font_c700_30, C_YELLOW, LV_TEXT_ALIGN_LEFT, 0, "OFF COURSE");
        char d[24];
        cc_fmt_turn_dist(d, sizeof d, (uint32_t)n->dist_to_route_m);
        snprintf(t, sizeof t, "%s to the route", d);
    } else {
        cc_fmt_turn_dist(t, sizeof t, n->dist_to_turn_m);
        ui_text(r, 76, 30, 0, 44, &font_c700_44, near ? C_ROUTE : C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
        snprintf(t, sizeof t, "%s", n->instr);
    }
    lv_obj_t *sub = ui_text(r, 76, 74, 156, 16, &font_b600_14, C_STATUS, LV_TEXT_ALIGN_LEFT, 0, t);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(sub, lv_font_get_line_height(&font_b600_14));
    ui_hline(r, 0, 97, 240, C_DIV);
    /* strip */
    ui_hline(r, 0, 256, 240, C_DIV);
    ui_vline(r, 79, 257, 43, C_DIV);
    ui_vline(r, 159, 257, 43, C_DIV);
    ui_caps(r, 8, 260, 0, LV_TEXT_ALIGN_LEFT, "DIST LEFT");
    ui_caps(r, 88, 260, 0, LV_TEXT_ALIGN_LEFT, "ETA");
    ui_caps(r, 168, 260, 0, LV_TEXT_ALIGN_LEFT, "SPEED");
    ui_fmt_dist(t, sizeof t, n->dist_left_m, mi);
    ui_text(r, 8, 274, 0, 26, &font_c600_28, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    int b = ui_last_baseline;
    ui_text_base(r, 8 + ui_text_w(&font_c600_28, 0, t) + 3, b, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, mi ? "mi" : "km");
    if (n->eta_valid && v->utc_known) {
        uint32_t loc = v->utc + n->eta_s + (uint32_t)(v->tz_min * 60);
        ui_fmt_clock(t, sizeof t, loc);
    } else if (n->eta_valid && m->clock_min >= 0) {
        uint32_t loc = (uint32_t)m->clock_min * 60 + n->eta_s;
        ui_fmt_clock(t, sizeof t, loc);
    } else {
        snprintf(t, sizeof t, "--");
    }
    ui_text_base(r, 88, b, &font_c600_28, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    if (v->speed_ok) ui_fmt_speed(t, sizeof t, v->speed_mms, mi); else snprintf(t, sizeof t, "--");
    ui_text_base(r, 168, b, &font_c600_28, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_base(r, 168 + ui_text_w(&font_c600_28, 0, t) + 3, b, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, mi ? "mph" : "km/h");
    ui_dots(r, ring_n(m), page_index(m, CC_PAGE_NAV));
}

/* ------------------------------------------------------------------ 17 Paused */
void scr_paused(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    int mi = v->settings.units == CC_UNITS_MI;
    char t[32];
    ui_status(r, m);
    ui_rect(r, 0, 22, 240, 44, C_YELLOW, 0);
    ui_icon(r, &ic_pause_20, 10, 34, C_BG);
    ui_text(r, 38, 22, 0, 44, &font_b700_20, C_BG, LV_TEXT_ALIGN_LEFT, 1, v->pause_auto ? "AUTO-PAUSED" : "PAUSED");
    ui_fmt_hms(t, sizeof t, m->paused_s ? m->paused_s : v->paused_s);
    ui_text(r, 230, 22, 0, 44, &font_c700_24, C_BG, LV_TEXT_ALIGN_RIGHT, 0, t);
    ui_text(r, 10, 72, 220, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_LEFT, 0,
            v->pause_auto ? "You stopped moving. Recording resumes when you ride." : "Paused by you. Press RESUME to continue.");
    ui_caps(r, 8, 112, 0, LV_TEXT_ALIGN_LEFT, "ELAPSED");
    ui_fmt_hms(t, sizeof t, v->timer_s);
    ui_text(r, 8, 128, 0, 72, &font_c700_68, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_hline(r, 0, 205, 240, C_DIV);
    ui_vline(r, 119, 206, 65, C_DIV);
    ui_caps(r, 8, 214, 0, LV_TEXT_ALIGN_LEFT, "DISTANCE");
    ui_caps(r, 128, 214, 0, LV_TEXT_ALIGN_LEFT, "AVG POWER");
    ui_fmt_dist(t, sizeof t, v->dist_m, mi);
    ui_text(r, 8, 228, 0, 34, &font_c600_36, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    int b = ui_last_baseline;
    ui_text_base(r, 8 + ui_text_w(&font_c600_36, 0, t) + 3, b, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, mi ? "mi" : "km");
    snprintf(t, sizeof t, v->avg_power_ok ? "%d" : "--", (int)v->avg_power);
    ui_text_base(r, 128, b, &font_c600_36, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, t);
    ui_text_base(r, 128 + ui_text_w(&font_c600_36, 0, t) + 3, b, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "W");
    ui_hline(r, 0, 271, 240, C_DIV);
    lv_obj_t *menu = ui_hit(r, 0, 276, 100, 44);
    lv_obj_t *res = ui_hit(r, 140, 276, 100, 44);
    (void)menu; (void)res;
    ui_footer(r, "MENU", FOOT_MENU, "RESUME", FOOT_PRIMARY);
}

/* ------------------------------------------------------------------ 20 overlays */
void ui_overlays(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    if (v->hold.active) {
        ui_rect(r, 0, 284, 240, 36, C_BAR, 0);
        ui_hline(r, 0, 284, 240, C_BORDER);
        char t[20];
        snprintf(t, sizeof t, "%u.%u / 1 s", v->hold.elapsed_ms / 1000, (v->hold.elapsed_ms / 100) % 10);
        ui_text(r, 10, 290, 0, 14, &font_b700_12, C_TEXT, LV_TEXT_ALIGN_LEFT, 1,
                v->hold.side == CC_BTN_R ? "HOLD TO PAUSE" : "HOLD FOR PREVIOUS PAGE");
        ui_text(r, 230, 290, 0, 14, &font_b700_12, C_MUTED, LV_TEXT_ALIGN_RIGHT, 1, t);
        ui_rect(r, 10, 308, 220, 6, C_PANEL, 3);
        int w = (int)(220 * v->hold.elapsed_ms / 1000);
        if (w > 0) ui_rect(r, 10, 308, w, 6, C_TEXT, 3); /* fills from the left, as the mockup */
        return;
    }
    const cc_banner_t *b = &v->banner;
    if (b->id == CC_AL_NONE)
        return;
    uint32_t bg = C_PANEL, fg = C_TEXT, acc = b->accent_rgb;
    switch (b->style) {
    case CC_AL_STYLE_GREEN: bg = acc = C_GREEN; fg = C_BG; break;
    case CC_AL_STYLE_RED: bg = acc = C_RED; fg = C_BG; break;
    case CC_AL_STYLE_YELLOW: bg = acc = C_YELLOW; fg = C_BG; break;
    case CC_AL_STYLE_ORANGE: bg = acc = C_ORANGE; fg = C_BG; break;
    default: break;
    }
    ui_rect(r, 0, 266, 240, 2, C_BG, 0);
    ui_rect(r, 0, 268, 240, 52, bg, 0);
    ui_rect(r, 0, 268, 6, 52, acc, 0);
    const lv_image_dsc_t *ic = &ic_flag_20;
    switch (b->id) {
    case CC_AL_NEXTSTEP: ic = &ic_next_20; break;
    case CC_AL_COMPLETE: ic = &ic_check_20; break;
    case CC_AL_POWERLOST: case CC_AL_GPSLOST: case CC_AL_STORAGE: ic = &ic_warn_20; break;
    case CC_AL_LOWBATT: ic = &ic_batt_20; break;
    default: break;
    }
    ui_icon(r, ic, 16, 284, fg);
    int rw = b->right[0] ? ui_text_w(&font_c700_28, 0, b->right) + 8 : 0;
    lv_obj_t *t1 = ui_text(r, 44, 278, 186 - rw, 18, &font_b700_15, fg, LV_TEXT_ALIGN_LEFT, 1, b->title);
    lv_label_set_long_mode(t1, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t1, lv_font_get_line_height(&font_b700_15));
    lv_obj_t *t2 = ui_text(r, 44, 296, 186 - rw, 14, &font_b600_12, fg, LV_TEXT_ALIGN_LEFT, 0, b->sub);
    lv_label_set_long_mode(t2, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t2, lv_font_get_line_height(&font_b600_12));
    if (b->right[0])
        ui_text(r, 230, 268, 0, 52, &font_c700_28, fg, LV_TEXT_ALIGN_RIGHT, 0, b->right);
}

void scr_alerts(lv_obj_t *r, const ui_model_t *m)
{
    scr_main(r, m);
    ui_overlays(r, m);
}

/* ------------------------------------------------------------------ 21 Empty states */
void scr_empty(lv_obj_t *r, const ui_model_t *m)
{
    static const char *title[] = {"No workout loaded", "No route loaded", "Waiting for GPS"};
    static const char *hint[] = {
        "Load one from Menu, then Workouts, before you start. You can still ride without one.",
        "Load a route from Menu, then Routes, for turn prompts. Your track is still recorded.",
        "Move to open sky. Navigation starts once your position is fixed."};
    ui_status(r, m);
    lv_point_t sz;
    lv_text_get_size(&sz, hint[m->empty], &font_b500_13, 0, 17 - lv_font_get_line_height(&font_b500_13), 196,
                     LV_TEXT_FLAG_NONE);
    int lines = (sz.y + 16) / 17;
    int h = 68 + 14 + 24 + 6 + lines * 17;
    int top = 22 + (278 - h) / 2;
    ui_dashed_ring(r, 120, top + 34, 34, C_DOT);
    const lv_image_dsc_t *ic = m->empty == UI_EMPTY_NOWORKOUT ? &ic_bolt_30 : m->empty == UI_EMPTY_NOROUTE ? &ic_route_30 : &ic_pin_30;
    ui_icon(r, ic, 105, top + 19, C_MUTED);
    ui_text(r, 0, top + 82, 240, 24, &font_b700_20, C_TEXT, LV_TEXT_ALIGN_CENTER, 0, title[m->empty]);
    ui_text(r, 22, top + 112, 196, 17, &font_b500_13, C_MUTED, LV_TEXT_ALIGN_CENTER, 0, hint[m->empty]);
    ui_dots(r, 4, m->empty == UI_EMPTY_NOWORKOUT ? 1 : 3);
}
