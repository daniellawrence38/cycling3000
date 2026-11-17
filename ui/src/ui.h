/* ui.h: the LVGL screens (one file per mockup board, brief 6.1), the model they draw from, and the
 * layout kit. Screens are built from a ui_model_t only, so the screenshot tool can force any scenario
 * by filling a model, and the live simulator fills it from the core's view and the store. */
#ifndef CC700_UI_H
#define CC700_UI_H

#include <stdint.h>

#include "cc700/core.h"
#include "fonts.h"
#include "icons.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ design tokens (spec 12) */
#define C_BG 0x0B0D10
#define C_BAR 0x12161B
#define C_PANEL 0x1B2027
#define C_DIV 0x232830
#define C_MUTED 0x9AA4B0
#define C_TEXT 0xF2F5F8
#define C_STATUS 0xC9D1DA
#define C_DOT 0x5B6673
#define C_BORDER 0x3A424C
#define C_ROUTE 0x4C9AFF
#define C_CYCLE 0x4FD1C5
#define C_MAP 0x10151B
#define C_GREEN 0x3DBE6B
#define C_YELLOW 0xF2C230
#define C_ORANGE 0xF08A24
#define C_RED 0xE5484D
#define C_BLUE 0x3B8BEB
#define C_RED_TEXT 0xF2767A

/* ------------------------------------------------------------------ the model */
#define UI_MAX_LIST 24

typedef enum {
    SCR_MAIN, SCR_WORKOUT, SCR_CLIMB, SCR_NAV, SCR_WORKOUTS, SCR_WORKOUT_DETAIL, SCR_READY, SCR_MENU, SCR_SENSORS,
    SCR_ADD_SENSOR, SCR_POWER_METER, SCR_PROFILE, SCR_ROUTES, SCR_ROUTE_DETAIL, SCR_HISTORY, SCR_SUMMARY, SCR_PAUSED,
    SCR_PAUSE_MENU, SCR_CONFIRM, SCR_ALERTS, SCR_EMPTY, SCR_HEART_RATE, SCR_PHONE, SCR_SETTINGS,
    SCR_HR_GRAPH, /* not a mockup board: the heart-rate graph Daniel asked for (QUESTIONS) */
    SCR_COUNT
} ui_screen_id_t;

typedef struct { char name[41]; uint32_t secs; int steps; uint8_t id[8]; } ui_wo_item_t;
/* has_profile: the route object carried an elevation profile (CR1 nprof > 0). Without one its
   ascent and climb count are 0 because nothing was known, not because the route is flat, so the
   screens show "--" instead of a figure (QUESTIONS Q35). */
typedef struct { char name[41]; uint32_t dist_m; uint32_t ascent_m; int climbs; int has_profile; uint8_t id[8]; } ui_rt_item_t;
typedef struct { uint32_t ride_id; uint32_t start_local; uint32_t dist_m, timer_s; int avg_power, has_power; int uploaded; } ui_ride_item_t;
/* One Add sensor row. addr and addr_type identify the device to the firmware's sensor manager; the
   screens show only id4. */
typedef struct { char name[24]; char id4[8]; int kind; int bars; uint8_t addr[6]; uint8_t addr_type; } ui_scan_item_t;

typedef enum { UI_CONFIRM_DISCARD, UI_CONFIRM_FORGET_SENSOR, UI_CONFIRM_FORGET_PHONE } ui_confirm_kind_t;
typedef enum { UI_PHONE_CONNECTED, UI_PHONE_PAIRING, UI_PHONE_NOT_PAIRED } ui_phone_state_t;
typedef enum { UI_CAL_IDLE, UI_CAL_RUNNING, UI_CAL_DONE, UI_CAL_FAILED, UI_CAL_UNSUPPORTED } ui_cal_state_t;
typedef enum { UI_EMPTY_NOWORKOUT, UI_EMPTY_NOROUTE, UI_EMPTY_GPSWAIT } ui_empty_kind_t;

typedef struct {
    cc_view_t v;                 /* everything the core knows */
    /* status bar */
    int clock_min;               /* local minutes since midnight, -1 unknown */
    /* libraries and history */
    ui_wo_item_t workouts[UI_MAX_LIST]; int n_workouts;
    ui_rt_item_t routes[UI_MAX_LIST]; int n_routes;
    ui_ride_item_t rides[UI_MAX_LIST]; int n_rides; int rides_total;
    /* detail screens */
    cc_workout_t detail_wo;      /* the workout being viewed */
    int detail_route;            /* index into routes; profile from detail_prof */
    int16_t detail_prof[96]; int detail_prof_n; uint32_t detail_prof_len_m;
    /* climb page profile strip: elevations (m) across the climb and the rider's fraction along it */
    float climb_prof[24]; int climb_prof_n; float climb_pos;
    /* navigation map: route points in metres relative to the rider (x east, y north); ridden index */
    float map_x[128], map_y[128]; int map_n, map_ridden; float course_deg;
    struct { uint32_t start_m, len_m; } detail_climbs[8]; int detail_n_climbs;
    ui_ride_item_t summary_ride; cc_fit_summary_t summary; int summary_uploaded; int summary_from_history;
    /* sensors */
    int pm_paired, hr_paired;
    char pm_name[24], pm_id4[8], hr_id4[8];
    ui_scan_item_t scan[4]; int n_scan; int scanning;
    int forget_kind;             /* the sensor Confirm "forgetsensor" is about: 2 heart rate, else power */
    ui_cal_state_t cal;
    int pm_live_w, pm_live_rpm;
    int hr_live, hr_bars;
    /* phone */
    ui_phone_state_t phone;
    char phone_name[24];
    uint32_t passkey;
    int rides_waiting;
    char last_sync[24];
    /* profile */
    int ftp_edit;                /* value being edited */
    /* confirm, empty */
    ui_confirm_kind_t confirm;
    ui_empty_kind_t empty;
    /* navigation state owned by the UI */
    int focus;                   /* focused row or button on list screens */
    int scroll;                  /* first visible row */
    int page;                    /* index into v.ring on ride pages */
    /* paused screen */
    uint32_t paused_s;
} ui_model_t;

/* Builds one screen into root (a 240 x 320 object) from the model. */
void ui_build(ui_screen_id_t id, lv_obj_t *root, const ui_model_t *m);
const char *ui_screen_name(ui_screen_id_t id); /* board name, for example "Main" */
int ui_screen_by_name(const char *name);

/* ------------------------------------------------------------------ kit */
extern lv_group_t *ui_focus_group;   /* the focus order of the screen being built */

lv_obj_t *ui_rect(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int radius);
lv_obj_t *ui_frame(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int radius, int border_w, uint32_t border);
/* Text in a CSS-like line box: top y, line height lh; the font's line is centred in it. */
lv_obj_t *ui_text(lv_obj_t *p, int x, int y, int w, int lh, const lv_font_t *f, uint32_t color, lv_text_align_t al,
                  int letter_space, const char *s);
int ui_text_w(const lv_font_t *f, int letter_space, const char *s);
/* Baseline (y) of the last text placed by ui_text, for aligning a unit to a number. */
extern int ui_last_baseline;
/* Text whose baseline sits at base_y (CSS align-items: baseline). Returns the label. */
lv_obj_t *ui_text_base(lv_obj_t *p, int x, int base_y, const lv_font_t *f, uint32_t color, lv_text_align_t al,
                       int letter_space, const char *s);
/* Text whose line box bottom is at bottom_y (CSS align-self: flex-end). */
lv_obj_t *ui_text_bottom(lv_obj_t *p, int x, int bottom_y, const lv_font_t *f, uint32_t color, lv_text_align_t al,
                         int letter_space, const char *s);
/* Adds the header kebab (drawn by ui_header44) as the last item of the focus order. */
void ui_kebab_late(lv_obj_t *root);
/* A dashed circle (Empty states icon ring). */
void ui_dashed_ring(lv_obj_t *p, int cx, int cy, int r, uint32_t c);
lv_obj_t *ui_icon(lv_obj_t *p, const lv_image_dsc_t *ic, int x, int y, uint32_t color);
void ui_hline(lv_obj_t *p, int x, int y, int w, uint32_t c);
void ui_vline(lv_obj_t *p, int x, int y, int h, uint32_t c);
/* A focusable, clickable object (button, row) added to the focus group. */
lv_obj_t *ui_hit(lv_obj_t *p, int x, int y, int w, int h);
/* 11 px caps label, letter spacing .08em, muted, 14 px line */
lv_obj_t *ui_caps(lv_obj_t *p, int x, int y, int w, lv_text_align_t al, const char *s);

void ui_status(lv_obj_t *root, const ui_model_t *m);
void ui_header36(lv_obj_t *root, int y, const char *title, const char *right);
/* 44 px header with a back button and optional kebab (both focusable). */
void ui_header44(lv_obj_t *root, int y, const char *title, const lv_font_t *f, int kebab);
/* 46 px list row: name and sub; returns the row (focusable). right_x is where right-hand content ends. */
lv_obj_t *ui_row46(lv_obj_t *p, int y, const char *name, const char *sub, int focused, uint32_t name_color);
lv_obj_t *ui_row46_sub(lv_obj_t *p, int y, const char *name, const char *sub, int focused, uint32_t name_color,
                       uint32_t sub_color);
void ui_chev(lv_obj_t *row);
/* Signal bars (20 x 16) with n of 4 lit. */
void ui_bars(lv_obj_t *p, int x, int y, int n);
typedef enum { FOOT_BACK, FOOT_MENU } ui_foot_left_t;
typedef enum { FOOT_PLAIN, FOOT_PRIMARY, FOOT_NONE } ui_foot_right_t;
void ui_footer(lv_obj_t *root, const char *left, ui_foot_left_t lk, const char *right, ui_foot_right_t rk);
typedef enum { BTN_PRIMARY, BTN_DANGER, BTN_OUTLINE, BTN_DISABLED } ui_btn_kind_t;
lv_obj_t *ui_btn(lv_obj_t *p, int x, int y, int w, int h, const char *text, ui_btn_kind_t k, const lv_font_t *f, int focus_ring);
typedef enum { CHIP_NEUTRAL, CHIP_PENDING, CHIP_OK, CHIP_BAD, CHIP_OUTLINE } ui_chip_kind_t;
/* Right-aligned chip ending at x_right; returns its left edge. */
int ui_chip(lv_obj_t *p, int x_right, int y, int h, const lv_font_t *f, ui_chip_kind_t k, const lv_image_dsc_t *icon,
            const char *text);
void ui_dots(lv_obj_t *root, int n, int active);
void ui_scrollbar(lv_obj_t *p, int x, int y, int h);

/* ------------------------------------------------------------------ formatting */
void ui_fmt_hms(char *o, int cap, uint32_t s);        /* 1:12:47 or 5:12 */
void ui_fmt_hm(char *o, int cap, uint32_t s);         /* 1:08 (h:mm) */
void ui_fmt_dist(char *o, int cap, uint32_t m, int miles); /* 32.6 */
void ui_fmt_speed(char *o, int cap, uint32_t mms, int miles); /* 32.4 */
void ui_fmt_thousands(char *o, int cap, uint32_t v);  /* 1,150 */
void ui_fmt_date(char *o, int cap, uint32_t local_s, int with_weekday); /* 31 Oct, Sat 31 Oct */
void ui_fmt_clock(char *o, int cap, uint32_t local_s); /* 14:32 */

/* ------------------------------------------------------------------ screens (one per board) */
void scr_main(lv_obj_t *r, const ui_model_t *m);
void scr_workout(lv_obj_t *r, const ui_model_t *m);
void scr_climb(lv_obj_t *r, const ui_model_t *m);
void scr_nav(lv_obj_t *r, const ui_model_t *m);
void scr_workouts(lv_obj_t *r, const ui_model_t *m);
void scr_workout_detail(lv_obj_t *r, const ui_model_t *m);
void scr_ready(lv_obj_t *r, const ui_model_t *m);
void scr_menu(lv_obj_t *r, const ui_model_t *m);
void scr_sensors(lv_obj_t *r, const ui_model_t *m);
void scr_add_sensor(lv_obj_t *r, const ui_model_t *m);
void scr_power_meter(lv_obj_t *r, const ui_model_t *m);
void scr_profile(lv_obj_t *r, const ui_model_t *m);
void scr_routes(lv_obj_t *r, const ui_model_t *m);
void scr_route_detail(lv_obj_t *r, const ui_model_t *m);
void scr_history(lv_obj_t *r, const ui_model_t *m);
void scr_summary(lv_obj_t *r, const ui_model_t *m);
void scr_paused(lv_obj_t *r, const ui_model_t *m);
void scr_pause_menu(lv_obj_t *r, const ui_model_t *m);
void scr_confirm(lv_obj_t *r, const ui_model_t *m);
void scr_alerts(lv_obj_t *r, const ui_model_t *m);
void scr_empty(lv_obj_t *r, const ui_model_t *m);
void scr_heart_rate(lv_obj_t *r, const ui_model_t *m);
void scr_phone(lv_obj_t *r, const ui_model_t *m);
void scr_settings(lv_obj_t *r, const ui_model_t *m);
void scr_hr_graph(lv_obj_t *r, const ui_model_t *m); /* heart rate, last 5 min (screens_hrgraph.c) */

/* 1 if (x, y) is on Main's HEART RATE field, the tap that opens the heart-rate graph. */
int ui_hr_field_hit(int x, int y);

/* The ride-page overlays (banner, hold bar), drawn above any ride page. */
void ui_overlays(lv_obj_t *r, const ui_model_t *m);

/* ------------------------------------------------------------------ screen manager (ui_app.c) */
typedef struct {
    void (*pair_window)(int open_60s);
    uint32_t (*passkey)(void);
    int (*pairing)(void);
    void (*erase_bond)(void);
    int (*cal_start)(uint32_t now_ms);
    ui_cal_state_t (*cal_state)(void);
    void (*cal_ack)(void);
    /* Sensors (bring-up step 6). The scan runs while Add sensor is on top and stops when it closes;
       the list is read at every render. sensor_saved fills the identity of the saved (or pairing)
       sensor of that kind and returns 1, or returns 0 if there is none; name and bars may be NULL.
       Without these hooks the screens behave as in the mockups (both sensors paired). */
    void (*sensor_scan)(int on);
    int (*sensor_scan_list)(ui_scan_item_t *out, int max);
    void (*sensor_pair)(const ui_scan_item_t *it);
    void (*sensor_forget)(int kind);
    int (*sensor_saved)(int kind, char *name, int name_cap, char *id4, int id4_cap, int *bars);
} ui_app_hooks_t;

void ui_app_init(hal_fs_t *fs, uint32_t now_ms, const ui_app_hooks_t *hooks);
void ui_app_step(uint32_t now_ms);     /* drains gestures, updates screen-off and model; returns 1 on input */
void ui_app_render(void);              /* builds and loads the visible LVGL screen */
int ui_app_dirty(void);                /* 1 if input, state change or active hold wants an immediate render */
int ui_app_take_input(void);           /* returns 1 and clears the flag when a button or tap was handled */
int ui_app_blank(void);                /* 1 if the screen is blanked (power short press or screen-off timer) */
ui_screen_id_t ui_app_top(void);       /* top of the stopped-state UI stack */
ui_screen_id_t ui_app_visible(void);   /* the screen currently shown (ride state overrides the stack) */
ui_model_t *ui_app_model(void);
void ui_app_refresh_lists(void);       /* reloads workout, route and ride lists from the store */
void ui_app_touch(int down, int16_t x, int16_t y); /* feeds pointer state for drag scroll, swipe and tap */
void ui_app_post_touch(void);          /* dispatches a released tap if LVGL's click event did not fire */

/* ------------------------------------------------------------------ scenarios (screenshot tool) */
typedef struct { const char *board; const char *scenario; ui_screen_id_t id; void (*fill)(ui_model_t *m); } ui_scenario_t;
extern const ui_scenario_t ui_scenarios[];
extern const int ui_n_scenarios;
void ui_model_demo(ui_model_t *m); /* the mockups' common values (FTP 260, 14:32, 68 %) */

#ifdef __cplusplus
}
#endif
#endif
