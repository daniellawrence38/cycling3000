/* hal_gnss.h: GNSS receiver. The part and its protocol are Unverified (spec 13.1). */
#ifndef HAL_GNSS_H
#define HAL_GNSS_H
#include <stdint.h>

typedef struct {
    int valid;              /* a usable position fix */
    int32_t lat_e7, lon_e7; /* degrees * 1e7 */
    float alt_m;
    float speed_mps;
    float course_deg;
    uint16_t hdop_x10;
    uint32_t utc_seconds;   /* may be set while valid is false; 0 if unknown */
    uint8_t sats;
} hal_gnss_fix_t;

/* Polled at 1 Hz. Returns 1 if a new fix record was read. */
int hal_gnss_poll(hal_gnss_fix_t *fix);
#endif
