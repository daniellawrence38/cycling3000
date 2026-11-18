#include "cc700/zones.h"

#include "cc700/util.h"

const uint32_t cc_zone_rgb[8] = {0x000000, 0x8E9AA6, 0x3B8BEB, 0x3DBE6B, 0xF2C230, 0xF08A24, 0xE5484D, 0xB46BE8};

int32_t cc_watts(int32_t ftp, int32_t p10)
{
    return (int32_t)cc_floordiv((int64_t)ftp * p10 + 500, 1000);
}

void cc_widen(int32_t n, int32_t *lo, int32_t *hi)
{
    *lo = (int32_t)cc_floordiv((int64_t)n * 95 + 50, 100);
    *hi = (int32_t)cc_floordiv((int64_t)n * 105 + 50, 100);
}

int cc_band(int32_t r)
{
    if (r <= 55) return 1;
    if (r <= 75) return 2;
    if (r <= 90) return 3;
    if (r <= 105) return 4;
    if (r <= 120) return 5;
    if (r <= 150) return 6;
    return 7;
}

int cc_zone_of_power(int32_t w, int32_t ftp)
{
    if (ftp <= 0)
        return 1;
    return cc_band((int32_t)cc_floordiv((int64_t)w * 200 + ftp, 2 * (int64_t)ftp));
}

int32_t cc_ramp_p10(int32_t p0, int32_t p1, int32_t secs, int32_t e)
{
    if (secs <= 0)
        return p0;
    return p0 + (int32_t)cc_floordiv((int64_t)(p1 - p0) * e * 2 + secs, 2 * (int64_t)secs);
}

int cc_zone_of_p10(int32_t p10)
{
    return cc_band((int32_t)cc_floordiv(p10 + 5, 10));
}

/* ---------------------------------------------------------------- heart-rate zones (QUESTIONS Q47) */
const uint32_t cc_hr_zone_rgb[6] = {0x000000, 0x8E9AA6, 0x3B8BEB, 0x3DBE6B, 0xF2C230, 0xE5484D};

static const int32_t HR_ZONE_PCT[CC_HR_ZONES] = {50, 60, 70, 80, 90};

int32_t cc_hr_zone_floor(int32_t max_hr, int z)
{
    if (max_hr <= 0 || z < 1 || z > CC_HR_ZONES)
        return 0;
    return (int32_t)cc_floordiv((int64_t)max_hr * HR_ZONE_PCT[z - 1] + 99, 100);
}

int cc_zone_of_hr(int32_t bpm, int32_t max_hr)
{
    if (max_hr <= 0 || bpm <= 0)
        return 0;
    int z = 1;
    for (int k = 2; k <= CC_HR_ZONES; k++)
        if (bpm >= cc_hr_zone_floor(max_hr, k))
            z = k;
    return z;
}
