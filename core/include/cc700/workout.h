/* workout.h: CW1 decode and validation (protocol 3.2), resolution at FTP, the status pill (V§7, V§7.1)
 * and the workout engine (brief 5.4). Static memory only. */
#ifndef CC700_WORKOUT_H
#define CC700_WORKOUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_WO_MAX_STEPS    100
#define CC_WO_MAX_NAME     40
#define CC_WO_MAX_LABEL    16
#define CC_WO_MAX_BYTES    4096
#define CC_WO_P10_MIN      10
#define CC_WO_P10_MAX      3150
#define CC_WO_SECS_MIN     5
#define CC_WO_SECS_MAX     43200
#define CC_WO_TOTAL_MAX    86400u

typedef struct {
    uint16_t secs;
    uint16_t p0, p1;   /* tenths of a percent */
    uint8_t ramp;      /* flags bit 0 */
    uint8_t rpm;       /* 0 = none */
    uint8_t rep_idx, rep_total;
    char label[CC_WO_MAX_LABEL + 1];
} cc_step_t;

typedef struct {
    uint8_t id[8];
    uint32_t total_secs;
    uint8_t nsteps;
    char name[CC_WO_MAX_NAME + 1];
    cc_step_t steps[CC_WO_MAX_STEPS];
} cc_workout_t;

/* Why a CW1 object was refused (the unit reports every one as BAD_OBJECT). */
typedef enum {
    CC_CW1_OK = 0,
    CC_CW1_TRUNCATED,   /* a field runs past the end */
    CC_CW1_MAGIC,
    CC_CW1_VERSION,
    CC_CW1_FLAGS,       /* header flags or reserved step flag bits not 0 */
    CC_CW1_NSTEPS,
    CC_CW1_NAME,        /* name length 1..40, printable ASCII */
    CC_CW1_SECS,
    CC_CW1_PCT,         /* p0/p1 outside 10..3150, fixed p0 >= p1, ramp p0 == p1 */
    CC_CW1_RPM,
    CC_CW1_LABEL,       /* label length 1..16, printable ASCII */
    CC_CW1_TOTAL,       /* sum(secs) != total_secs or total over 86400 */
    CC_CW1_TRAILING,    /* bytes left over */
    CC_CW1_ID,          /* ID does not recompute */
    CC_CW1_TOO_BIG,
} cc_cw1_err_t;

/* Decodes and validates a CW1 object. On error *out is unspecified. */
cc_cw1_err_t cc_cw1_decode(const uint8_t *obj, size_t len, cc_workout_t *out);

/* Encodes a workout (name and steps; id and total are computed). Returns the object length, 0 if it
 * does not fit in cap. Used by tests, tools and simunit; the unit itself never encodes. */
size_t cc_cw1_encode(const cc_workout_t *wo, uint8_t *out, size_t cap);

/* Object ID: first 8 bytes of SHA-256 over the object with bytes 4..11 zeroed. */
void cc_object_id(const uint8_t *obj, size_t len, uint8_t id[8]);

/* Watts range of a step at an FTP. Fixed steps ignore e; ramps are resolved at elapsed e (V§2.5). */
void cc_step_range(const cc_step_t *s, int32_t ftp, int32_t e, int32_t *lo_w, int32_t *hi_w);

/* Target zone of a step at elapsed e (V§3). */
int cc_step_target_zone(const cc_step_t *s, int32_t e);

/* ------------------------------------------------------------------ status pill */
typedef enum { CC_PILL_HIDDEN = 0, CC_PILL_IN, CC_PILL_HIGH, CC_PILL_LOW } cc_pill_state_t;

typedef struct {
    cc_pill_state_t state; /* IN, HIGH or LOW */
    int k;                 /* consecutive seconds outside on `side` while IN */
    int side;              /* +1 above, -1 below, 0 none */
} cc_pill_t;

void cc_pill_reset(cc_pill_t *p);
/* One 3 s power sample per second against [lo, hi]. Returns IN, HIGH or LOW; *delta is the distance to
 * the nearest edge while HIGH or LOW, else 0. */
cc_pill_state_t cc_pill_feed(cc_pill_t *p, int32_t lo, int32_t hi, int32_t power, int32_t *delta);

/* ------------------------------------------------------------------ engine */
enum {
    CC_WO_EV_STEP_CHANGE = 1 << 0, /* a new step started (not the first): record a FIT lap */
    CC_WO_EV_COMPLETE = 1 << 1,    /* the clock passed the end of the last step (once) */
};

typedef struct {
    int loaded;
    int complete;
    int step;                 /* 0-based index of the active step; last step once complete */
    int nsteps;
    const char *label;
    uint32_t step_secs, elapsed_s, remaining_s;
    int32_t lo_w, hi_w;
    int target_zone;
    int rep_idx, rep_total;
    int rpm;                  /* 0 = no target */
    int has_next;
    const char *next_label;
    int32_t next_lo_w, next_hi_w;
    uint32_t next_secs;
    int next_zone;
    int nextstep_banner;      /* last CC_NEXTSTEP_LEAD_S of a step of CC_SHORT_STEP_S or longer, not the last */
    cc_pill_state_t pill;     /* HIDDEN, IN, HIGH or LOW */
    int32_t pill_delta;
    int actual_zone;          /* 0 when the 3 s power is invalid */
} cc_wo_view_t;

typedef struct {
    cc_workout_t wo;
    int32_t ftp;
    uint32_t start[CC_WO_MAX_STEPS];
    int cur;
    int pill_started;
    cc_pill_t pill;
    cc_wo_view_t view;
} cc_wo_engine_t;

/* Loads a decoded workout. The engine keeps its own copy. */
void cc_wo_engine_load(cc_wo_engine_t *e, const cc_workout_t *wo, int32_t ftp);
void cc_wo_engine_unload(cc_wo_engine_t *e);
/* FTP change: the next second resolves at the new FTP; the current view is recomputed now. */
void cc_wo_engine_set_ftp(cc_wo_engine_t *e, int32_t ftp);
/* Call with timer_s = 0 at START and once for each new value of the ride timer. p3_valid false means
 * the 3 s power is invalid. Returns CC_WO_EV_* bits. */
unsigned cc_wo_engine_second(cc_wo_engine_t *e, uint32_t timer_s, int p3_valid, int32_t p3);

#ifdef __cplusplus
}
#endif
#endif
