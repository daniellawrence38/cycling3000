/* hrhist.h: the last five minutes of heart rate at 1 Hz, for the heart-rate graph screen (QUESTIONS
 * Q43 or later: added at Daniel's request, 18 November 2026; not in spec 0.5).
 *
 * A ring of CC_HRHIST_N one-second entries. Each entry is either a reading (1..254 bpm) or "no
 * reading" (CC_HRHIST_NONE), so a gap is never confused with a value. The core pushes one entry per
 * second while a ride is recording or paused, using the same one-second value the FIT record and
 * the Main page's HEART RATE field use, and resets the ring at START. No heap. */
#ifndef CC700_HRHIST_H
#define CC700_HRHIST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_HRHIST_N 300     /* 5 min at 1 Hz */
#define CC_HRHIST_NONE 0u   /* no reading that second (0 bpm is never stored as a value) */
#define CC_HRHIST_MAX 254u  /* readings above are clamped, as the FIT record's 8-bit field */

typedef struct {
    uint8_t bpm[CC_HRHIST_N]; /* CC_HRHIST_NONE or 1..254 */
    uint16_t head;            /* index of the newest entry (valid when n > 0) */
    uint16_t n;               /* entries pushed, saturating at CC_HRHIST_N */
} cc_hrhist_t;

typedef struct {
    int n_valid;            /* readings in the window; the rest are 0 when this is 0 */
    int min, max, avg;      /* avg: half-up integer mean, as the one-second bins */
} cc_hrhist_stats_t;

void cc_hrhist_reset(cc_hrhist_t *h);
/* One second. ok 0, or bpm <= 0, records "no reading". */
void cc_hrhist_push(cc_hrhist_t *h, int ok, int32_t bpm);
/* The entry age_s seconds old (0 = newest). Returns the bpm, or -1 for no reading, for a second not
   yet recorded in this ride, or for age_s outside 0..CC_HRHIST_N-1. */
int cc_hrhist_at(const cc_hrhist_t *h, int age_s);
/* Min, max and mean of the readings in the window. Returns n_valid. */
int cc_hrhist_stats(const cc_hrhist_t *h, cc_hrhist_stats_t *st);

#ifdef __cplusplus
}
#endif
#endif
