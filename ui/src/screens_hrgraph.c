/* Heart-rate graph: the last 5 minutes of heart rate as a line, opened by tapping HEART RATE on the
 * Main page while recording (QUESTIONS, the HR graph entry). Not a mockup board: added at Daniel's
 * request on 18 November 2026, in the visual language of the ride pages (spec 12).
 *
 * Drawing cost: the screen is rebuilt once a second like every ride page. The plot is one plain
 * object with a draw callback over a static table of 300 y positions (no allocation per frame), plus
 * a handful of labels and grid lines. */
#include <stdio.h>
#include <string.h>

#include "cc700/hrhist.h"
#include "ui.h"

/* Plot box (screen pixels) */
#define HRG_X 30
#define HRG_Y 172
#define HRG_W 202
#define HRG_H 90
#define HRG_PAD 6 /* the plot object is this much larger on every side, so the end marker is not clipped */

typedef struct {
    int16_t y[CC_HRHIST_N]; /* by age (0 newest): pixel row inside the box, -1 for no reading */
    int any;
} hrg_plot_t;

static hrg_plot_t hrg; /* read by the draw callback while the screen it belongs to is shown */

static int32_t hrg_x(int W, int age) { return (int32_t)((W - 1) * (CC_HRHIST_N - 1 - age) / (CC_HRHIST_N - 1)); }

static void hrg_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const hrg_plot_t *p = lv_obj_get_user_data(o);
    if (!p || !p->any) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    a.x1 += HRG_PAD;
    a.y1 += HRG_PAD;
    const int W = HRG_W;
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(C_TEXT);
    ld.width = 2;
    ld.round_start = ld.round_end = 1;
    lv_draw_rect_dsc_t dd;
    lv_draw_rect_dsc_init(&dd);
    dd.bg_color = lv_color_hex(C_TEXT);
    dd.radius = 1;
    /* Oldest to newest. A gap breaks the line; a reading with gaps on both sides is a dot. */
    for (int age = CC_HRHIST_N - 1; age >= 0; age--) {
        int y = p->y[age];
        if (y < 0) continue;
        int32_t x = a.x1 + hrg_x(W, age);
        int newer = age > 0 ? p->y[age - 1] : -1;
        int older = age < CC_HRHIST_N - 1 ? p->y[age + 1] : -1;
        if (newer >= 0) {
            ld.p1.x = x;
            ld.p1.y = a.y1 + y;
            ld.p2.x = a.x1 + hrg_x(W, age - 1);
            ld.p2.y = a.y1 + newer;
            lv_draw_line(layer, &ld);
        } else if (older < 0) {
            lv_area_t d = {x - 1, a.y1 + y - 1, x + 1, a.y1 + y + 1};
            lv_draw_rect(layer, &dd, &d);
        }
    }
    /* The newest reading, marked as on the climb profile */
    if (p->y[0] >= 0) {
        lv_draw_rect_dsc_t rd;
        lv_draw_rect_dsc_init(&rd);
        rd.bg_color = lv_color_hex(C_TEXT);
        rd.radius = LV_RADIUS_CIRCLE;
        rd.border_width = 2;
        rd.border_color = lv_color_hex(C_BG);
        int32_t x = a.x1 + W - 1, y = a.y1 + p->y[0];
        lv_area_t c = {x - 5, y - 5, x + 5, y + 5};
        lv_draw_rect(layer, &rd, &c);
    }
}

static void stat_cell(lv_obj_t *r, int x, const char *lbl, int ok, int val)
{
    char t[8];
    snprintf(t, sizeof t, ok ? "%d" : "--", val);
    ui_caps(r, x + 8, 124, 0, LV_TEXT_ALIGN_LEFT, lbl);
    ui_text(r, x + 8, 138, 0, 24, &font_c600_24, ok ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
}

void scr_hr_graph(lv_obj_t *r, const ui_model_t *m)
{
    const cc_view_t *v = &m->v;
    const cc_hrhist_t *h = &v->hr_hist;
    char t[24];
    ui_status(r, m);
    ui_header36(r, 22, "Heart rate", "LAST 5 MIN");

    /* Now: the same value as the Main page's HEART RATE field */
    snprintf(t, sizeof t, v->hr_ok ? "%d" : "--", (int)v->hr);
    ui_text(r, 8, 60, 0, 58, &font_c700_56, v->hr_ok ? C_TEXT : C_MUTED, LV_TEXT_ALIGN_LEFT, 0, t);
    int b = ui_last_baseline;
    ui_text_base(r, 8 + ui_text_w(&font_c700_56, 0, t) + 6, b, &font_b600_18, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "bpm");
    if (v->hr_state != CC_SENS_OK) {
        int lh = lv_font_get_line_height(&font_b700_12) + 6;
        ui_chip(r, 232, 60 + (58 - lh) / 2, lh, &font_b700_12, v->hr_state == CC_SENS_OFF ? CHIP_OUTLINE : CHIP_PENDING,
                NULL, v->hr_state == CC_SENS_OFF ? "NO STRAP" : "SEARCHING");
    }
    ui_hline(r, 0, 119, 240, C_DIV);

    /* Min, average and max of the window */
    cc_hrhist_stats_t st;
    int ok = cc_hrhist_stats(h, &st) > 0;
    stat_cell(r, 0, "MIN", ok, st.min);
    stat_cell(r, 80, "AVG", ok, st.avg);
    stat_cell(r, 160, "MAX", ok, st.max);
    ui_vline(r, 79, 120, 44, C_DIV);
    ui_vline(r, 159, 120, 44, C_DIV);
    ui_hline(r, 0, 164, 240, C_DIV);

    /* Time axis: 5 min ago at the left edge, now at the right */
    ui_text(r, HRG_X, 266, 0, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_LEFT, 0, "\xE2\x88\x92" "5:00");
    ui_text(r, HRG_X, 266, HRG_W, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_CENTER, 0, "\xE2\x88\x92" "2:30");
    ui_text(r, HRG_X + HRG_W, 266, 0, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_RIGHT, 1, "NOW");
    ui_vline(r, HRG_X + HRG_W / 2, HRG_Y, HRG_H, C_DIV);

    if (!ok) {
        hrg.any = 0;
        ui_hline(r, HRG_X, HRG_Y + HRG_H - 1, HRG_W, C_DIV);
        ui_text(r, HRG_X, HRG_Y + 18, HRG_W, 20, &font_b700_15, C_TEXT, LV_TEXT_ALIGN_CENTER, 0, "No heart rate");
        ui_text(r, HRG_X + 4, HRG_Y + 42, HRG_W - 8, 16, &font_b500_12, C_MUTED, LV_TEXT_ALIGN_CENTER, 0,
                v->hr_state == CC_SENS_OFF ? "No strap is paired. Pair one in Menu, Sensors, when stopped."
                                           : "Put the strap on to wake it. The graph fills as readings arrive.");
    } else {
        int lo, hi, step;
        cc_hrhist_axis(st.min, st.max, &lo, &hi, &step);
        for (int g = ((lo + step - 1) / step) * step; g <= hi; g += step) {
            int y = HRG_Y + (HRG_H - 1) - (HRG_H - 1) * (g - lo) / (hi - lo);
            ui_hline(r, HRG_X, y, HRG_W, C_DIV);
            snprintf(t, sizeof t, "%d", g);
            int ly = y - 7;
            if (ly < HRG_Y - 6) ly = HRG_Y - 6;
            ui_text(r, HRG_X - 4, ly, 0, 14, &font_b600_11, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, t);
        }
        hrg.any = 0;
        for (int age = 0; age < CC_HRHIST_N; age++) {
            int bpm = cc_hrhist_at(h, age);
            if (bpm < 0) { hrg.y[age] = -1; continue; }
            hrg.y[age] = (int16_t)((HRG_H - 1) - (HRG_H - 1) * (bpm - lo) / (hi - lo));
            hrg.any = 1;
        }
        lv_obj_t *plot = lv_obj_create(r);
        lv_obj_remove_style_all(plot);
        lv_obj_set_scrollable(plot, false);
        lv_obj_remove_flag(plot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(plot, HRG_X - HRG_PAD, HRG_Y - HRG_PAD);
        lv_obj_set_size(plot, HRG_W + 2 * HRG_PAD, HRG_H + 2 * HRG_PAD);
        lv_obj_set_user_data(plot, &hrg);
        lv_obj_add_event_cb(plot, hrg_draw, LV_EVENT_DRAW_MAIN, NULL);
    }
    ui_footer(r, "BACK", FOOT_BACK, "", FOOT_NONE);
}
