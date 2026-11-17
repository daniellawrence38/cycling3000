/* nmea.c: see nmea.h. */
#include "nmea.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void fw_nmea_init(fw_nmea_t *st)
{
    memset(st, 0, sizeof *st);
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Checks '$...*XX' and returns a pointer to the '*' if the XOR checksum matches, else NULL. */
static const char *check_nmea(const char *s)
{
    if (!s || s[0] != '$')
        return NULL;
    uint8_t x = 0;
    const char *p = s + 1;
    while (*p && *p != '*' && *p != '\r' && *p != '\n') {
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7E)
            return NULL;
        x ^= (uint8_t)*p++;
    }
    if (*p != '*')
        return NULL;
    int hi = hex_val(p[1]), lo = hex_val(p[2]);
    if (hi < 0 || lo < 0)
        return NULL;
    if (p[3] != '\0' && p[3] != '\r' && p[3] != '\n')
        return NULL;
    if (x != (uint8_t)((hi << 4) | lo))
        return NULL;
    return p;
}

/* Splits comma-separated fields between s+1 and star into tok[0..cap-1]. */
static int split_fields(char *body, char *tok[], int cap)
{
    int n = 0;
    char *p = body;
    while (n < cap) {
        tok[n++] = p;
        char *c = strchr(p, ',');
        if (!c)
            break;
        *c = '\0';
        p = c + 1;
    }
    return n;
}

/* Parses NMEA coordinate ddmm.mmmm (or dddmm.mmmm) with hemisphere N/S/E/W into degrees * 1e7. */
static int parse_coord_e7(const char *val, const char *hemi, int is_lon, int32_t *out)
{
    if (!val || !*val || !hemi || !*hemi)
        return -1;
    int sign = 0;
    if (!is_lon && (*hemi == 'N' || *hemi == 'S'))
        sign = (*hemi == 'S') ? -1 : 1;
    else if (is_lon && (*hemi == 'E' || *hemi == 'W'))
        sign = (*hemi == 'W') ? -1 : 1;
    if (!sign)
        return -1;
    const char *dot = strchr(val, '.');
    size_t head = dot ? (size_t)(dot - val) : strlen(val);
    if (head < 3 || head > (size_t)(is_lon ? 5 : 4))
        return -1;
    for (const char *q = val; *q; q++)
        if (q != dot && (*q < '0' || *q > '9'))
            return -1;
    size_t deg_len = head - 2;
    int deg = 0;
    for (size_t i = 0; i < deg_len; i++)
        deg = deg * 10 + (val[i] - '0');
    double mins = strtod(val + deg_len, NULL);
    if (mins < 0.0 || mins >= 60.0)
        return -1;
    double total_deg = (double)deg + mins / 60.0;
    if (total_deg > (is_lon ? 180.0 : 90.0))
        return -1;
    *out = (int32_t)llround(total_deg * 1e7 * (double)sign);
    return 0;
}

/* Converts NMEA hhmmss(.ss) and ddmmyy into Unix UTC seconds, or 0 if the date cannot be real.
 *
 * The year is accepted only from 2025 to 2040. NMEA sends two digits, so a receiver's default date is
 * read in this century: the GPS epoch default, 6 January 1980, arrives as "060180" and reads as 2080,
 * which the old upper bound of 2099 let through. 2040 is a deliberate horizon: well past this unit's
 * service life, and it keeps "80" and every other default in the 1980s and 1990s out. Raise it if
 * this code is still running in 2040. Before 2025 the date cannot be real either: this software did
 * not exist. A plausible year is not enough on its own; fw_nmea_line also wants evidence of a fix. */
static uint32_t parse_utc(const char *hms, const char *dmy)
{
    if (!hms || !dmy || strlen(hms) < 6 || strlen(dmy) != 6)
        return 0;
    for (int i = 0; i < 6; i++)
        if (hms[i] < '0' || hms[i] > '9' || dmy[i] < '0' || dmy[i] > '9')
            return 0;
    int hh = (hms[0] - '0') * 10 + (hms[1] - '0');
    int mm = (hms[2] - '0') * 10 + (hms[3] - '0');
    int ss = (hms[4] - '0') * 10 + (hms[5] - '0');
    int d = (dmy[0] - '0') * 10 + (dmy[1] - '0');
    int m = (dmy[2] - '0') * 10 + (dmy[3] - '0');
    int y = 2000 + (dmy[4] - '0') * 10 + (dmy[5] - '0');
    if (hh > 23 || mm > 59 || ss > 59 || m < 1 || m > 12 || d < 1 || d > 31 || y < 2025 || y > 2040)
        return 0;
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int maxd = mdays[m - 1] + (m == 2 ? leap : 0);
    if (d > maxd)
        return 0;
    /* Days from 1970-01-01 (Howard Hinnant's civil-to-days algorithm). */
    int yr = y - (m <= 2);
    int era = yr / 400;
    int yoe = yr - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    uint32_t days = (uint32_t)(era * 146097 + doe - 719468);
    return days * 86400u + (uint32_t)(hh * 3600 + mm * 60 + ss);
}

int fw_nmea_line(fw_nmea_t *st, const char *line, hal_gnss_fix_t *fix)
{
    const char *star = check_nmea(line);
    if (!star)
        return -1;
    size_t blen = (size_t)(star - (line + 1));
    if (blen < 5 || blen >= FW_NMEA_MAX_LINE)
        return -1;
    char body[FW_NMEA_MAX_LINE];
    memcpy(body, line + 1, blen);
    body[blen] = '\0';

    char *f[20];
    int nf = split_fields(body, f, 20);
    if (nf < 1 || strlen(f[0]) != 5)
        return -1;
    const char *type = f[0] + 2;

    if (!strcmp(type, "GGA")) {
        if (nf < 10)
            return -1;
        int qual = *f[6] ? atoi(f[6]) : 0;
        if (qual > 0)
            st->fix_seen = 1; /* from now on the receiver's clock has been set from the satellites */
        if (*f[7]) {
            int s = atoi(f[7]);
            st->sats = (uint8_t)(s < 0 ? 0 : s > 99 ? 99 : s);
        }
        if (*f[8]) {
            double h = strtod(f[8], NULL);
            if (h >= 0.0 && h <= 999.0)
                st->hdop_x10 = (uint16_t)lround(h * 10.0);
        }
        if (qual > 0 && *f[9])
            st->alt_m = (float)strtod(f[9], NULL);
        return 0;
    }

    if (!strcmp(type, "RMC")) {
        if (nf < 10)
            return -1;
        memset(fix, 0, sizeof *fix);
        /* UTC only from a receiver that has shown it has a fix: this RMC's status is A, or a GGA
           with fix quality above 0 has been seen since start-up.
           This is deliberately stricter than spec 10.6, which lets the unit learn UTC before a
           position fix ("the receiver often reports UTC before it has a position fix"). The core
           takes any non-zero utc_seconds as the truth (cc_on_gnss), fixes the ride's ID and every
           FIT timestamp from it, and has no way to take it back. A receiver straight out of a cold
           start sends its built-in default date in void RMC sentences, and the year check alone
           cannot tell a default from the real date if the default happens to be plausible. Waiting
           for a fix costs, at worst, the time to first fix; meanwhile the phone's TIME_SET supplies
           the time, and a ride started without either is corrected when UTC arrives (10.6). */
        if (f[2][0] == 'A' || st->fix_seen)
            fix->utc_seconds = parse_utc(f[1], f[9]);
        fix->sats = st->sats;
        fix->hdop_x10 = st->hdop_x10;
        fix->alt_m = st->alt_m;
        if (f[2][0] == 'A') {
            int32_t lat = 0, lon = 0;
            if (parse_coord_e7(f[3], f[4], 0, &lat) == 0 && parse_coord_e7(f[5], f[6], 1, &lon) == 0) {
                fix->valid = 1;
                fix->lat_e7 = lat;
                fix->lon_e7 = lon;
                if (*f[7]) {
                    double kn = strtod(f[7], NULL);
                    if (kn >= 0.0 && kn <= 200.0)
                        fix->speed_mps = (float)(kn * (1852.0 / 3600.0));
                }
                if (*f[8]) {
                    double crs = strtod(f[8], NULL);
                    if (crs >= 0.0 && crs <= 360.0)
                        st->last_course_deg = (float)crs;
                }
                fix->course_deg = st->last_course_deg;
            }
        }
        return 1;
    }

    return 0;
}

int fw_nmea_feed_byte(fw_nmea_t *st, char c, hal_gnss_fix_t *fix)
{
    if (c == '$') {
        st->buf[0] = '$';
        st->len = 1;
        st->overflow = 0;
        return 0;
    }
    if (c == '\r' || c == '\n') {
        if (st->len > 0 && !st->overflow) {
            st->buf[st->len] = '\0';
            st->len = 0;
            return fw_nmea_line(st, st->buf, fix) == 1 ? 1 : 0;
        }
        st->len = 0;
        st->overflow = 0;
        return 0;
    }
    if (st->len == 0 || st->overflow)
        return 0;
    if (st->len + 1 >= FW_NMEA_MAX_LINE) {
        st->overflow = 1;
        return 0;
    }
    st->buf[st->len++] = c;
    return 0;
}
