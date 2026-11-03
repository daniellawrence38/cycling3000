/* ble_parse.c: Cycling Power and Heart Rate characteristic parsing (Bluetooth CPS 1.1 and HRS 1.0
 * field order; the optional fields follow in flag order). */
#include "ble_parse.h"

int fw_parse_cp_measurement(const uint8_t *b, size_t len, fw_cp_meas_t *out)
{
    if (len < 4)
        return -1;
    uint16_t flags = (uint16_t)(b[0] | (b[1] << 8));
    out->watts = (int16_t)(uint16_t)(b[2] | (b[3] << 8));
    out->has_crank = 0;
    size_t o = 4;
    if (flags & (1u << 0)) o += 1; /* pedal power balance */
    if (flags & (1u << 2)) o += 2; /* accumulated torque */
    if (flags & (1u << 4)) o += 6; /* wheel revolutions u32 + last wheel event time u16 */
    if (flags & (1u << 5)) {       /* crank revolutions u16 + last crank event time u16 */
        if (o + 4 > len)
            return -1;
        out->has_crank = 1;
        out->crank_revs = (uint16_t)(b[o] | (b[o + 1] << 8));
        out->crank_time = (uint16_t)(b[o + 2] | (b[o + 3] << 8));
    } else if (o > len) {
        return -1;
    }
    return 0;
}

int fw_parse_hr_measurement(const uint8_t *b, size_t len)
{
    if (len < 2)
        return -1;
    if (b[0] & 1) {
        if (len < 3)
            return -1;
        return b[1] | (b[2] << 8);
    }
    return b[1];
}

int fw_parse_cp_feature(const uint8_t *b, size_t len, uint32_t *features)
{
    if (len < 4)
        return -1;
    *features = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}
