/* altitude.h: barometer smoothing, ascent, gradient and STEEP (brief 5.3). float32 is allowed here.
 * Whether the unit has a working barometer is Unverified (spec 13.1 item 7). */
#ifndef CC700_ALTITUDE_H
#define CC700_ALTITUDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_GRAD_RING 8

typedef struct {
    /* barometer */
    float sum;
    int n;
    int have;              /* a smoothed altitude exists */
    float alt;             /* smoothed barometric altitude, m */
    /* GNSS offset over the first 30 s of usable fixes */
    float off_sum;
    int off_n;
    int off_done;
    float offset;
    /* ascent */
    int ref_have;
    float ref;
    float ascent;          /* m, while recording */
    /* gradient */
    uint32_t ring_dist[CC_GRAD_RING];
    float ring_alt[CC_GRAD_RING];
    int ring_n, ring_head;
    uint32_t next_dist_cm;
    int grad_valid;
    int32_t grad_x10;
    int steep;
    int steep_secs;
} cc_alt_t;

float cc_alt_from_pa(float pa);

void cc_alt_init(cc_alt_t *a);
/* Each barometer reading (pascals; ignored if negative). */
void cc_alt_baro(cc_alt_t *a, int32_t pa);
/* At each second boundary. dist_cm is the ride distance; speed in mm/s (3 s mean); gnss_ok and
 * gnss_alt describe this second's fix. */
void cc_alt_second(cc_alt_t *a, int recording, uint32_t dist_cm, uint32_t speed_mms, int gnss_ok, float gnss_alt);
/* Absolute altitude for the FIT file (barometric plus the GNSS offset). Returns 0 if none. */
int cc_alt_absolute(const cc_alt_t *a, float *m);
/* Resets ascent and the gradient ring at START (the smoothed altitude carries over). */
void cc_alt_ride_start(cc_alt_t *a);

#ifdef __cplusplus
}
#endif
#endif
