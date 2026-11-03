#include "cc700/alerts.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cc700/util.h"

void cc_alerts_init(cc_alerts_t *a) { memset(a, 0, sizeof *a); }

static void show(cc_alerts_t *a, const cc_banner_t *b, uint32_t now_ms)
{
    a->visible = *b;
    a->visible.shown_ms = now_ms;
}

void cc_alerts_raise(cc_alerts_t *a, const cc_banner_t *b, uint32_t now_ms)
{
    cc_banner_t nb = *b;
    nb.trigger_ms = now_ms;
    if (a->visible.id == nb.id) { /* same alert again: refresh in place */
        show(a, &nb, now_ms);
        return;
    }
    if (a->visible.id == CC_AL_NONE || nb.id < a->visible.id) {
        show(a, &nb, now_ms);
        return;
    }
    /* Equal or lower priority waits, one deep. A higher-priority waiting banner is kept. */
    if (a->queued.id == CC_AL_NONE || nb.id <= a->queued.id)
        a->queued = nb;
}

void cc_alerts_tick(cc_alerts_t *a, uint32_t now_ms)
{
    if (a->visible.id != CC_AL_NONE && a->visible.duration_ms != CC_AL_UNTIL_CLEARED &&
        now_ms - a->visible.shown_ms >= a->visible.duration_ms)
        a->visible.id = CC_AL_NONE;
    if (a->queued.id != CC_AL_NONE && now_ms - a->queued.trigger_ms > CC_AL_LATE_MS)
        a->queued.id = CC_AL_NONE; /* would show too late */
    if (a->visible.id == CC_AL_NONE && a->queued.id != CC_AL_NONE) {
        show(a, &a->queued, now_ms);
        a->queued.id = CC_AL_NONE;
    }
}

void cc_alerts_clear(cc_alerts_t *a, cc_alert_id_t id, uint32_t now_ms)
{
    if (a->queued.id == id)
        a->queued.id = CC_AL_NONE;
    if (a->visible.id == id) {
        a->visible.id = CC_AL_NONE;
        cc_alerts_tick(a, now_ms);
    }
}

void cc_alerts_set_right(cc_alerts_t *a, cc_alert_id_t id, const char *right)
{
    if (a->visible.id == id)
        snprintf(a->visible.right, sizeof a->visible.right, "%s", right);
    if (a->queued.id == id)
        snprintf(a->queued.right, sizeof a->queued.right, "%s", right);
}

void cc_alerts_button(cc_alerts_t *a, uint32_t now_ms)
{
    cc_alerts_clear(a, CC_AL_COMPLETE, now_ms);
}

/* ------------------------------------------------------------------ texts (spec 10.2) */
void cc_fmt_mmss(char *out, int cap, uint32_t s)
{
    /* uint32_t is `unsigned int` on the host but `long unsigned int` on xtensa, so %u is wrong
       there and -Werror=format rejects it. PRIu32 is right on both (ESP-IDF's own advice). */
    if (s >= 3600)
        snprintf(out, (size_t)cap, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32, s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(out, (size_t)cap, "%" PRIu32 ":%02" PRIu32, s / 60, s % 60);
}

void cc_fmt_km1(char *out, int cap, uint32_t metres)
{
    /* tenths, half up */
    uint32_t t = (uint32_t)cc_div_half_up(metres, 100);
    snprintf(out, (size_t)cap, "%" PRIu32 ".%" PRIu32, t / 10, t % 10);
}

static void upper(char *s)
{
    for (; *s; s++)
        *s = (char)toupper((unsigned char)*s);
}

static void base(cc_banner_t *b, cc_alert_id_t id, cc_alert_style_t st, uint32_t dur)
{
    memset(b, 0, sizeof *b);
    b->id = id;
    b->style = st;
    b->accent_rgb = 0xF2F5F8;
    b->duration_ms = dur;
}

void cc_banner_nextstep(cc_banner_t *b, const char *label, int32_t lo_w, int32_t hi_w, uint32_t secs,
                        uint32_t left_s, uint32_t accent_rgb, uint32_t duration_ms)
{
    char t[12];
    base(b, CC_AL_NEXTSTEP, CC_AL_STYLE_PANEL, duration_ms);
    b->accent_rgb = accent_rgb;
    snprintf(b->title, sizeof b->title, "NEXT \xC2\xB7 %s", label);
    upper(b->title);
    cc_fmt_mmss(t, sizeof t, secs);
    snprintf(b->sub, sizeof b->sub, "%d\xE2\x80\x93%d W for %s", (int)lo_w, (int)hi_w, t);
    snprintf(b->right, sizeof b->right, "%" PRIu32 " s", left_s);
}

void cc_banner_lap(cc_banner_t *b, int lap_no, uint32_t lap_s, uint32_t lap_dist_m, int miles)
{
    char t[12], d[16];
    base(b, CC_AL_LAP, CC_AL_STYLE_PANEL, 4000);
    snprintf(b->title, sizeof b->title, "LAP %d", lap_no);
    cc_fmt_mmss(t, sizeof t, lap_s);
    cc_fmt_km1(d, sizeof d, miles ? (uint32_t)cc_div_half_up((int64_t)lap_dist_m * 1000, 1609) : lap_dist_m);
    snprintf(b->sub, sizeof b->sub, "%s \xC2\xB7 %s %s", t, d, miles ? "mi" : "km");
}

void cc_banner_complete(cc_banner_t *b, const char *workout_name)
{
    base(b, CC_AL_COMPLETE, CC_AL_STYLE_GREEN, 10000);
    snprintf(b->title, sizeof b->title, "WORKOUT COMPLETE");
    snprintf(b->sub, sizeof b->sub, "%s", workout_name);
}

void cc_banner_powerlost(cc_banner_t *b)
{
    base(b, CC_AL_POWERLOST, CC_AL_STYLE_RED, 6000);
    snprintf(b->title, sizeof b->title, "POWER METER LOST");
    snprintf(b->sub, sizeof b->sub, "Trying to reconnect");
}

void cc_banner_gpslost(cc_banner_t *b)
{
    base(b, CC_AL_GPSLOST, CC_AL_STYLE_YELLOW, 6000);
    snprintf(b->title, sizeof b->title, "GPS SIGNAL LOST");
    snprintf(b->sub, sizeof b->sub, "No speed or distance");
}

void cc_banner_lowbatt(cc_banner_t *b, int pct)
{
    base(b, CC_AL_LOWBATT, CC_AL_STYLE_ORANGE, 8000);
    snprintf(b->title, sizeof b->title, "LOW BATTERY %d%%", pct);
    snprintf(b->sub, sizeof b->sub, "Recording continues");
}

void cc_banner_climbahead(cc_banner_t *b, uint32_t length_m, uint32_t ascent_m, int32_t avg_x10, uint32_t to_start_m)
{
    char d[16];
    base(b, CC_AL_CLIMBAHEAD, CC_AL_STYLE_PANEL, 6000);
    snprintf(b->title, sizeof b->title, "CLIMB AHEAD");
    cc_fmt_km1(d, sizeof d, length_m);
    snprintf(b->sub, sizeof b->sub, "%s km \xC2\xB7 %" PRIu32 " m \xC2\xB7 %d.%d %%", d, ascent_m, (int)(avg_x10 / 10),
             (int)((avg_x10 < 0 ? -avg_x10 : avg_x10) % 10));
    snprintf(b->right, sizeof b->right, "%" PRIu32 " m", to_start_m);
}

void cc_banner_storage(cc_banner_t *b)
{
    base(b, CC_AL_STORAGE, CC_AL_STYLE_RED, CC_AL_UNTIL_CLEARED);
    snprintf(b->title, sizeof b->title, "STORAGE ERROR");
    snprintf(b->sub, sizeof b->sub, "Ride may not be saved");
}
