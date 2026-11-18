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

/* Heart-rate zones (Daniel's request, 18 November 2026; QUESTIONS Q47): five zones as a percentage of
 * max heart rate, Z1 50 to 60 %, Z2 60 to 70 %, Z3 70 to 80 %, Z4 80 to 90 %, Z5 90 to 100 %. A zone
 * starts at its exact percentage (60 % of max is Z2). Below 50 % is still Z1 and above max is Z5, as
 * power's Z1 and Z7 are open-ended. Integer only. */
#define CC_HR_ZONES 5

/* Lowest bpm of zone z (1..5) at a max HR: the smallest bpm at or above the zone's percentage,
 * (max_hr*pct + 99) // 100 with pct 50, 60, 70, 80, 90. 0 if max_hr <= 0 or z is out of range. */
int32_t cc_hr_zone_floor(int32_t max_hr, int z);

/* Zone 1..5 of a heart rate at a max HR: the highest z with bpm >= cc_hr_zone_floor(max_hr, z),
 * else 1. 0 when the zones are off (max_hr <= 0) or there is no reading (bpm <= 0). */
int cc_zone_of_hr(int32_t bpm, int32_t max_hr);

/* Heart-rate zone colours, index 1..5 (grey, blue, green, yellow, red: power's Z1 to Z4 and Z6, so a
 * colour means about the same effort on both); index 0 is unused. 0xRRGGBB. */
extern const uint32_t cc_hr_zone_rgb[6];

#ifdef __cplusplus
}
#endif
#endif
