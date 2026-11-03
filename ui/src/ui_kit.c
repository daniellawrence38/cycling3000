/* ui_kit.c: the mockups' building blocks in LVGL, laid out with absolute coordinates taken from the
 * board HTML (tools/design/gen.py and tools/design/boards). */
#include <stdio.h>
#include <string.h>

#include "ui.h"

lv_group_t *ui_focus_group;
int ui_last_baseline;
static const char KEBAB_TAG[] = "kebab";

static lv_obj_t *bare(lv_obj_t *p)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *ui_rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int radius)
{
    lv_obj_t *o = bare(p);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

lv_obj_t *ui_frame(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int radius, int bw, uint32_t border)
{
    lv_obj_t *o = ui_rect(p, x, y, w, h, bg, radius);
    if (bg == 0xFFFFFFFFu)
        lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    return o;
}

int ui_text_w(const lv_font_t *f, int ls, const char *s)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, ls, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

/* Browsers place a glyph lower in its line box than LVGL does for the same Barlow metrics: measured
 * against the mockup renders, about 5 % of the font's line height and never less than 2 px. */
static int css_shift(const lv_font_t *f)
{
    int d = (lv_font_get_line_height(f) * 46 + 500) / 1000;
    return d < 2 ? 2 : d;
}

lv_obj_t *ui_text(lv_obj_t *p, int x, int y, int w, int lh, const lv_font_t *f, uint32_t color, lv_text_align_t al,
                  int ls, const char *s)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_remove_style_all(l);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(l, ls, 0);
    int flh = lv_font_get_line_height(f);
    if (lh <= 0)
        lh = flh;
    lv_obj_set_style_text_line_space(l, lh - flh, 0);
    int tw = ui_text_w(f, ls, s);
    if (w <= 0) {
        /* content width; alignment relative to x */
        int xx = al == LV_TEXT_ALIGN_RIGHT ? x - tw : al == LV_TEXT_ALIGN_CENTER ? x - tw / 2 : x;
        lv_obj_set_pos(l, xx, y + (lh - flh) / 2 + css_shift(f));
        ui_last_baseline = y + (lh - flh) / 2 + css_shift(f) + flh - f->base_line;
        lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    } else {
        lv_obj_set_pos(l, x, y + (lh - flh) / 2 + css_shift(f));
        ui_last_baseline = y + (lh - flh) / 2 + css_shift(f) + flh - f->base_line;
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, al, 0);
        if (tw > w && !strchr(s, '\n'))
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    }
    return l;
}

lv_obj_t *ui_icon(lv_obj_t *p, const lv_image_dsc_t *ic, int x, int y, uint32_t color)
{
    lv_obj_t *im = lv_image_create(p);
    lv_obj_remove_style_all(im);
    lv_image_set_src(im, ic);
    lv_obj_set_pos(im, x, y);
    lv_obj_set_style_image_recolor(im, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    return im;
}

void ui_hline(lv_obj_t *p, int x, int y, int w, uint32_t c) { ui_rect(p, x, y, w, 1, c, 0); }
void ui_vline(lv_obj_t *p, int x, int y, int h, uint32_t c) { ui_rect(p, x, y, 1, h, c, 0); }

lv_obj_t *ui_hit(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *o = bare(p);
    lv_obj_set_clickable(o, true);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    if (ui_focus_group)
        lv_group_add_obj(ui_focus_group, o);
    return o;
}

lv_obj_t *ui_caps(lv_obj_t *p, int x, int y, int w, lv_text_align_t al, const char *s)
{
    return ui_text(p, x, y, w, 14, &font_b600_11, C_MUTED, al, 1, s);
}

/* ------------------------------------------------------------------ status bar (22 px) */
void ui_status(lv_obj_t *r, const ui_model_t *m)
{
    ui_rect(r, 0, 0, 240, 22, C_BAR, 0);
    int x = 8;
    const cc_view_t *v = &m->v;
    if (v->gps_fix) { ui_icon(r, &ic_pin_13, x, 5, C_STATUS); x += 22; }
    if (v->power_state == CC_SENS_OK) { ui_icon(r, &ic_bolt_13, x, 5, C_STATUS); x += 22; }
    if (v->hr_state == CC_SENS_OK) { ui_icon(r, &ic_heart_13, x, 5, C_STATUS); x += 22; }
    if (v->phone_connected) { ui_icon(r, &ic_phone_13, x, 5, C_STATUS); }
    char pct[8], clk[8];
    int b = v->battery_pct < 0 ? 0 : v->battery_pct;
    snprintf(pct, sizeof pct, "%d%%", b);
    int pw = ui_text_w(&font_b600_11, 0, pct);
    ui_text(r, 232, 0, 0, 22, &font_b600_11, C_STATUS, LV_TEXT_ALIGN_RIGHT, 0, pct);
    int bx = 232 - pw - 3 - 16;
    ui_icon(r, &ic_batt_16x13, bx, 5, C_STATUS);
    int lvl = (b * 65 + 500) / 1000; /* inner width 6.5 px at 100 % */
    if (lvl > 0)
        ui_rect(r, bx + 4, 10, lvl, 3, C_STATUS, 0);
    if (m->clock_min >= 0)
        snprintf(clk, sizeof clk, "%d:%02d", m->clock_min / 60, m->clock_min % 60);
    else
        snprintf(clk, sizeof clk, "--:--");
    ui_text(r, bx - 8, 0, 0, 22, &font_b600_11, C_STATUS, LV_TEXT_ALIGN_RIGHT, 0, clk);
}

/* ------------------------------------------------------------------ headers */
void ui_header36(lv_obj_t *r, int y, const char *title, const char *right)
{
    ui_text(r, 8, y, 0, 35, &font_b700_18, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, title);
    if (right && *right)
        ui_text(r, 232, y, 0, 35, &font_b600_13, C_MUTED, LV_TEXT_ALIGN_RIGHT, 0, right);
    ui_hline(r, 0, y + 35, 240, C_DIV);
}

void ui_header44(lv_obj_t *r, int y, const char *title, const lv_font_t *f, int kebab)
{
    lv_obj_t *b = ui_hit(r, 0, y, 44, 44); /* 44 px touch target; the divider is drawn over its last row */
    ui_icon(b, &ic_chev_l_20, 12, 12, C_TEXT);
    lv_obj_t *t = ui_text(r, 44, y, kebab ? 152 : 188, 43, f ? f : &font_b700_18, C_TEXT, LV_TEXT_ALIGN_LEFT, 0, title);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(t, lv_font_get_line_height(f ? f : &font_b700_18));
    if (kebab) {
        /* the kebab is the last item in the focus order (spec 6), so it is added after the screen's
         * other items by ui_kebab_late; here we only draw it */
        lv_obj_t *k = lv_obj_create(r);
        lv_obj_remove_style_all(k);
        lv_obj_set_scrollable(k, false);
        lv_obj_set_clickable(k, true);
        lv_obj_set_pos(k, 196, y);
        lv_obj_set_size(k, 44, 44);
        lv_obj_set_user_data(k, (void *)KEBAB_TAG);
        ui_icon(k, &ic_kebab_20, 12, 12, C_TEXT);
    }
    ui_hline(r, 0, y + 43, 240, C_DIV);
}

/* ------------------------------------------------------------------ list rows */
lv_obj_t *ui_row46(lv_obj_t *p, int y, const char *name, const char *sub, int focused, uint32_t name_color)
{
    return ui_row46_sub(p, y, name, sub, focused, name_color, C_MUTED);
}

lv_obj_t *ui_row46_sub(lv_obj_t *p, int y, const char *name, const char *sub, int focused, uint32_t name_color,
                       uint32_t sub_color)
{
    lv_obj_t *row = ui_hit(p, 0, y, 240, 46);
    if (focused) {
        ui_rect(row, 0, 0, 240, 45, C_PANEL, 0);
        ui_rect(row, 0, 0, 3, 45, C_TEXT, 0);
    }
    ui_hline(row, 0, 45, 240, C_DIV);
    lv_obj_t *n;
    if (sub && *sub) {
        n = ui_text(row, 11, 6, 190, 18, &font_b600_15, name_color, LV_TEXT_ALIGN_LEFT, 0, name);
        lv_obj_t *s = ui_text(row, 11, 24, 190, 14, &font_b500_12, sub_color, LV_TEXT_ALIGN_LEFT, 0, sub);
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(s, lv_font_get_line_height(&font_b500_12));
    } else {
        n = ui_text(row, 11, 0, 190, 45, &font_b600_15, name_color, LV_TEXT_ALIGN_LEFT, 0, name);
    }
    lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(n, lv_font_get_line_height(&font_b600_15));
    return row;
}

void ui_chev(lv_obj_t *row) { ui_icon(row, &ic_chev_r_14, 214, 16, C_MUTED); }

void ui_scrollbar(lv_obj_t *p, int x, int y, int h) { ui_rect(p, x, y, 3, h, C_DOT, 2); }

/* ------------------------------------------------------------------ footer (32 px) */
void ui_footer(lv_obj_t *r, const char *left, ui_foot_left_t lk, const char *right, ui_foot_right_t rk)
{
    const lv_font_t *f = &font_b700_12;
    ui_rect(r, 0, 288, 240, 32, C_BAR, 0);
    if (lk == FOOT_BACK) {
        ui_icon(r, &ic_chev_l_12, 8, 298, C_STATUS);
        ui_text(r, 24, 288, 0, 32, f, C_STATUS, LV_TEXT_ALIGN_LEFT, 1, left);
    } else {
        ui_icon(r, &ic_menu_13, 8, 297, C_STATUS);
        ui_text(r, 27, 288, 0, 32, f, C_STATUS, LV_TEXT_ALIGN_LEFT, 1, left);
    }
    if (rk == FOOT_NONE || !right || !*right)
        return;
    int tw = ui_text_w(f, 1, right);
    if (rk == FOOT_PLAIN) {
        ui_icon(r, &ic_chev_r_12, 220, 298, C_STATUS);
        ui_text(r, 216, 288, 0, 32, f, C_STATUS, LV_TEXT_ALIGN_RIGHT, 1, right);
    } else {
        int w = 8 + tw + 5 + 11 + 8;
        ui_rect(r, 232 - w, 295, w, 18, C_TEXT, 5);
        ui_text(r, 232 - w + 8, 295, 0, 18, f, C_BG, LV_TEXT_ALIGN_LEFT, 1, right);
        ui_icon(r, &ic_play_11, 232 - 8 - 11, 298, C_BG);
    }
}

/* ------------------------------------------------------------------ buttons and chips */
lv_obj_t *ui_btn(lv_obj_t *p, int x, int y, int w, int h, const char *text, ui_btn_kind_t k, const lv_font_t *f, int ring)
{
    uint32_t bg = C_TEXT, fg = C_BG;
    if (k == BTN_DANGER) bg = C_RED;
    if (k == BTN_OUTLINE) { bg = C_PANEL; fg = C_TEXT; }
    if (k == BTN_DISABLED) { bg = C_PANEL; fg = C_MUTED; }
    lv_obj_t *b = k == BTN_DISABLED ? bare(p) : ui_hit(p, x, y, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 10, 0);
    if (k == BTN_OUTLINE) {
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(C_BORDER), 0);
    }
    if (ring) {
        lv_obj_set_style_outline_width(b, 2, 0);
        lv_obj_set_style_outline_color(b, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_outline_opa(b, LV_OPA_COVER, 0);
    }
    int bw = k == BTN_OUTLINE ? 1 : 0; /* children sit inside the border */
    ui_text(b, 0, 0, w - 2 * bw, h - 2 * bw, f ? f : &font_b700_16, fg, LV_TEXT_ALIGN_CENTER, 1, text);
    return b;
}

static void dashed_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    uint32_t c = (uint32_t)(uintptr_t)lv_obj_get_user_data(o);
    int h = lv_area_get_height(&a), rr = h / 2;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(c);
    d.width = 1;
    d.dash_width = 3;
    d.dash_gap = 2;
    d.p1.x = a.x1 + rr; d.p2.x = a.x2 - rr;
    d.p1.y = d.p2.y = a.y1;
    lv_draw_line(layer, &d);
    d.p1.y = d.p2.y = a.y2;
    lv_draw_line(layer, &d);
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_hex(c);
    ad.width = 1;
    ad.radius = (uint16_t)rr;
    ad.center.x = a.x1 + rr; ad.center.y = a.y1 + rr;
    ad.start_angle = 90; ad.end_angle = 270;
    lv_draw_arc(layer, &ad);
    ad.center.x = a.x2 - rr;
    ad.start_angle = 270; ad.end_angle = 90;
    lv_draw_arc(layer, &ad);
}

int ui_chip(lv_obj_t *p, int xr, int y, int h, const lv_font_t *f, ui_chip_kind_t k, const lv_image_dsc_t *icon,
            const char *text)
{
    int tw = ui_text_w(f, 1, text);
    int iw = icon ? (int)icon->header.w + 3 : 0;
    int pad = h <= 18 ? 6 : 8;
    int w = pad + iw + tw + pad;
    int x = xr - w;
    uint32_t bg = C_PANEL, fg = C_TEXT;
    lv_obj_t *o;
    switch (k) {
    case CHIP_PENDING:
        o = bare(p);
        lv_obj_set_pos(o, x, y);
        lv_obj_set_size(o, w, h);
        lv_obj_set_user_data(o, (void *)(uintptr_t)C_YELLOW);
        lv_obj_add_event_cb(o, dashed_draw, LV_EVENT_DRAW_MAIN, NULL);
        fg = C_YELLOW;
        break;
    case CHIP_OK: o = ui_rect(p, x, y, w, h, C_GREEN, h / 2); fg = C_BG; break;
    case CHIP_BAD: o = ui_rect(p, x, y, w, h, C_RED, h / 2); fg = C_BG; break;
    case CHIP_OUTLINE: o = ui_frame(p, x, y, w, h, 0xFFFFFFFFu, h / 2, 1, C_BORDER); break;
    default: o = ui_frame(p, x, y, w, h, bg, h / 2, 1, C_BORDER); break;
    }
    if (icon)
        ui_icon(o, icon, pad, (h - (int)icon->header.h) / 2, fg);
    ui_text(o, pad + iw, 0, 0, h, f, fg, LV_TEXT_ALIGN_LEFT, 1, text);
    return x;
}

void ui_dots(lv_obj_t *r, int n, int active)
{
    int w = 0;
    for (int i = 0; i < n; i++) w += (i == active ? 18 : 6) + (i ? 6 : 0);
    int x = (240 - w) / 2;
    for (int i = 0; i < n; i++) {
        int dw = i == active ? 18 : 6;
        ui_rect(r, x, 307, dw, 6, i == active ? C_TEXT : C_DOT, 3);
        x += dw + 6;
    }
}

/* ------------------------------------------------------------------ formatting */
void ui_fmt_hms(char *o, int cap, uint32_t s) { cc_fmt_mmss(o, cap, s); }

void ui_fmt_hm(char *o, int cap, uint32_t s) { snprintf(o, (size_t)cap, "%u:%02u", s / 3600, (s / 60) % 60); }

void ui_fmt_dist(char *o, int cap, uint32_t m, int miles)
{
    if (miles) m = (uint32_t)(((uint64_t)m * 1000 + 804) / 1609);
    uint32_t t = (m + 50) / 100;
    snprintf(o, (size_t)cap, "%u.%u", t / 10, t % 10);
}

void ui_fmt_speed(char *o, int cap, uint32_t mms, int miles)
{
    uint32_t x10 = miles ? (uint32_t)(((uint64_t)mms * 36 * 1000 + 804500) / 1609000) : (mms * 36 + 500) / 1000;
    snprintf(o, (size_t)cap, "%u.%u", x10 / 10, x10 % 10);
}

void ui_fmt_thousands(char *o, int cap, uint32_t v)
{
    if (v >= 1000) snprintf(o, (size_t)cap, "%u,%03u", v / 1000, v % 1000);
    else snprintf(o, (size_t)cap, "%u", v);
}

static void civil(uint32_t local, int *y, int *mo, int *d, int *wd)
{
    int64_t days = local / 86400;
    *wd = (int)((days + 4) % 7); /* 1970-01-01 was a Thursday */
    int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*mo <= 2));
}

void ui_fmt_date(char *o, int cap, uint32_t local, int wd_on)
{
    static const char *mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *wn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    int y, m, d, wd;
    civil(local, &y, &m, &d, &wd);
    if (wd_on) snprintf(o, (size_t)cap, "%s %d %s", wn[wd], d, mn[m - 1]);
    else snprintf(o, (size_t)cap, "%d %s", d, mn[m - 1]);
}

void ui_fmt_clock(char *o, int cap, uint32_t local)
{
    snprintf(o, (size_t)cap, "%u:%02u", (local / 3600) % 24, (local / 60) % 60);
}

lv_obj_t *ui_text_base(lv_obj_t *p, int x, int base_y, const lv_font_t *f, uint32_t color, lv_text_align_t al, int ls,
                       const char *s)
{
    int flh = lv_font_get_line_height(f);
    int top = base_y - (flh - f->base_line);
    int keep = ui_last_baseline;
    lv_obj_t *l = ui_text(p, x, top - css_shift(f), 0, flh, f, color, al, ls, s);
    ui_last_baseline = keep;
    return l;
}

lv_obj_t *ui_text_bottom(lv_obj_t *p, int x, int bottom_y, const lv_font_t *f, uint32_t color, lv_text_align_t al,
                         int ls, const char *s)
{
    int flh = lv_font_get_line_height(f);
    return ui_text(p, x, bottom_y - flh, 0, flh, f, color, al, ls, s);
}

void ui_kebab_late(lv_obj_t *root)
{
    uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(root, (int32_t)i);
        if (lv_obj_get_user_data(c) == (void *)KEBAB_TAG && ui_focus_group)
            lv_group_add_obj(ui_focus_group, c);
    }
}

typedef struct { int r; uint32_t c; } ring_t;
static void ring_draw(lv_event_t *e)
{
    lv_obj_t *o = lv_event_get_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    uint32_t c = (uint32_t)(uintptr_t)lv_obj_get_user_data(o);
    int r = lv_area_get_width(&a) / 2 - 1;
    lv_draw_arc_dsc_t ad;
    lv_draw_arc_dsc_init(&ad);
    ad.color = lv_color_hex(c);
    ad.width = 2;
    ad.radius = (uint16_t)r;
    ad.center.x = a.x1 + r + 1;
    ad.center.y = a.y1 + r + 1;
    int n = (int)(2 * 3.14159 * r / 8); /* 4 px dash, 4 px gap */
    for (int i = 0; i < n; i++) {
        ad.start_angle = (lv_value_precise_t)(i * 360 / n);
        ad.end_angle = (lv_value_precise_t)(i * 360 / n + 180 / n);
        lv_draw_arc(layer, &ad);
    }
}

void ui_dashed_ring(lv_obj_t *p, int cx, int cy, int r, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_size(o, 2 * r, 2 * r);
    lv_obj_set_user_data(o, (void *)(uintptr_t)c);
    lv_obj_add_event_cb(o, ring_draw, LV_EVENT_DRAW_MAIN, NULL);
}

void ui_bars(lv_obj_t *p, int x, int y, int n)
{
    static const int bx[4] = {0, 5, 11, 16}, by[4] = {11, 7, 4, 0};
    for (int i = 0; i < 4; i++)
        ui_rect(p, x + bx[i], y + by[i], 3, 16 - by[i], i < n ? C_TEXT : C_BORDER, 1);
}
