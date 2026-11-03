/* alerts.h: the one ride banner (spec 10.2, brief 5.8). Priority high to low: storage error, power lost,
 * GPS lost, low battery, workout complete, climb ahead, next step, lap. A higher priority replaces the
 * visible banner at once; an equal or lower one waits in a one-deep queue and is dropped if it would
 * show more than 5 s after its trigger. The hold bar is not a banner. */
#ifndef CC700_ALERTS_H
#define CC700_ALERTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CC_AL_NONE = 0,
    CC_AL_STORAGE,     /* highest */
    CC_AL_POWERLOST,
    CC_AL_GPSLOST,
    CC_AL_LOWBATT,
    CC_AL_COMPLETE,
    CC_AL_CLIMBAHEAD,
    CC_AL_NEXTSTEP,
    CC_AL_LAP,         /* lowest */
} cc_alert_id_t;

typedef enum {
    CC_AL_STYLE_PANEL,    /* panel, accent colour */
    CC_AL_STYLE_GREEN,    /* #3DBE6B, dark text */
    CC_AL_STYLE_RED,      /* #E5484D, dark text */
    CC_AL_STYLE_YELLOW,   /* #F2C230, dark text */
    CC_AL_STYLE_ORANGE,   /* #F08A24, dark text */
} cc_alert_style_t;

#define CC_AL_UNTIL_CLEARED 0xFFFFFFFFu
#define CC_AL_LATE_MS 5000u

typedef struct {
    cc_alert_id_t id;
    cc_alert_style_t style;
    uint32_t accent_rgb;      /* panel style: the accent bar colour */
    char title[32];
    char sub[48];
    char right[12];
    uint32_t trigger_ms;      /* when it was raised */
    uint32_t duration_ms;     /* from when it is shown; CC_AL_UNTIL_CLEARED for none */
    uint32_t shown_ms;
} cc_banner_t;

typedef struct {
    cc_banner_t visible;      /* id CC_AL_NONE: nothing shown */
    cc_banner_t queued;
} cc_alerts_t;

void cc_alerts_init(cc_alerts_t *a);
/* Raise a banner. b->id, style, texts and duration_ms must be set; trigger_ms is set here. */
void cc_alerts_raise(cc_alerts_t *a, const cc_banner_t *b, uint32_t now_ms);
/* Expire the visible banner and promote the queued one. Call at least every 100 ms. */
void cc_alerts_tick(cc_alerts_t *a, uint32_t now_ms);
/* Remove a banner (visible or queued), for example storage working again or the step ended. */
void cc_alerts_clear(cc_alerts_t *a, cc_alert_id_t id, uint32_t now_ms);
/* Update the right-hand text of a visible or queued banner (the next-step countdown). */
void cc_alerts_set_right(cc_alerts_t *a, cc_alert_id_t id, const char *right);
/* Any button dismisses WORKOUT COMPLETE. */
void cc_alerts_button(cc_alerts_t *a, uint32_t now_ms);

/* Builders for the spec 10.2 texts. */
void cc_banner_nextstep(cc_banner_t *b, const char *label, int32_t lo_w, int32_t hi_w, uint32_t secs,
                        uint32_t left_s, uint32_t accent_rgb, uint32_t duration_ms);
void cc_banner_lap(cc_banner_t *b, int lap_no, uint32_t lap_s, uint32_t lap_dist_m, int miles);
void cc_banner_complete(cc_banner_t *b, const char *workout_name);
void cc_banner_powerlost(cc_banner_t *b);
void cc_banner_gpslost(cc_banner_t *b);
void cc_banner_lowbatt(cc_banner_t *b, int pct);
void cc_banner_climbahead(cc_banner_t *b, uint32_t length_m, uint32_t ascent_m, int32_t avg_x10, uint32_t to_start_m);
void cc_banner_storage(cc_banner_t *b);

/* Formatting helpers shared with the UI. */
void cc_fmt_mmss(char *out, int cap, uint32_t secs);     /* 4:00, 1:02:03 */
void cc_fmt_km1(char *out, int cap, uint32_t metres);    /* 2.4 km (or miles with miles=1) */

#ifdef __cplusplus
}
#endif
#endif
