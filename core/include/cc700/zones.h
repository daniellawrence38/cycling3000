/* zones.h: watts from percentages, zones, ramp targets (V§2.4, V§2.5, V§3). Integer only. */
#ifndef CC700_ZONES_H
#define CC700_ZONES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watts for a percentage in tenths at an FTP: (ftp*p10 + 500) // 1000. */
int32_t cc_watts(int32_t ftp, int32_t p10);

/* Single target n (tenths) widened to plus or minus 5 %: lo = (n*95+50)//100, hi = (n*105+50)//100. */
void cc_widen(int32_t n, int32_t *lo, int32_t *hi);

/* Zone band 1..7 of a rounded percentage r. */
int cc_band(int32_t r);

/* Zone 1..7 of a measured power at an FTP: band((w*200 + ftp) // (2*ftp)). */
int cc_zone_of_power(int32_t w, int32_t ftp);

/* Ramp target at elapsed e: p0 + ((p1-p0)*e*2 + secs) // (2*secs), floor division. */
int32_t cc_ramp_p10(int32_t p0, int32_t p1, int32_t secs, int32_t e);

/* Zone of a target in tenths of a percent: band((p + 5) // 10). */
int cc_zone_of_p10(int32_t p10);

/* Zone colours (spec 5), index 1..7; index 0 is unused. 0xRRGGBB. */
extern const uint32_t cc_zone_rgb[8];

#ifdef __cplusplus
}
#endif
#endif
