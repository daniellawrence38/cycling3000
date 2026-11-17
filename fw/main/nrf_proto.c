/* nrf_proto.c: see nrf_proto.h. The frame format is read out of the stock firmware's parser and
 * builder and confirmed on the wire (over 200,000 frames, no CRC reject); the sensor layouts come
 * from the 4 November 2026 capture. docs/NRF-PROTOCOL.md has the evidence for every byte. */
#include "nrf_proto.h"

uint16_t fw_nrf_crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

int fw_nrf_take_frame(const uint8_t *buf, size_t len, fw_nrf_frame_t *out)
{
    if (len < FW_NRF_OVERHEAD)
        return FW_NRF_MORE; /* the stock parser likewise refuses to look at fewer than eight bytes */
    if (buf[0] != FW_NRF_SYNC || buf[2] != FW_NRF_SYNC2)
        return FW_NRF_NOT_FRAME;
    if (buf[1] > FW_NRF_MAX_LEN)
        return FW_NRF_NOT_FRAME;
    size_t total = (size_t)buf[1] + 4;
    if (total < FW_NRF_OVERHEAD)
        return FW_NRF_NOT_FRAME;
    if (buf[4] < 1 || buf[4] > 5)
        return FW_NRF_NOT_FRAME; /* "Com Instr Err" in the stock parser */
    if (len < total)
        return FW_NRF_MORE;
    uint16_t want = (uint16_t)(buf[total - 2] | ((uint16_t)buf[total - 1] << 8));
    if (fw_nrf_crc16(buf, total - 2) != want)
        return FW_NRF_BAD_CRC;
    if (out) {
        out->kind = buf[4];
        out->id = buf[5];
        out->payload = buf + 6;
        out->plen = total - FW_NRF_OVERHEAD;
    }
    return (int)total;
}

static int is_sensor(const fw_nrf_frame_t *f, uint8_t which)
{
    return f->id == FW_NRF_ID_SENSOR && f->kind == FW_NRF_KIND_STREAM && f->plen == 8 &&
           f->payload[0] == FW_NRF_SUB_SENSOR && f->payload[1] == which;
}

static uint16_t u16le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

int fw_nrf_parse_baro(const fw_nrf_frame_t *f, fw_nrf_baro_t *out)
{
    if (!is_sensor(f, FW_NRF_SENSOR_BARO))
        return -1;
    const uint8_t *p = f->payload;
    uint32_t cpa = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    if (cpa < (uint32_t)FW_NRF_BARO_MIN_PA * 100u || cpa > (uint32_t)FW_NRF_BARO_MAX_PA * 100u)
        return -1;
    out->temp_c100 = (int16_t)u16le(p + 2);
    out->pressure_cpa = cpa;
    return 0;
}

int32_t fw_nrf_baro_pa(const fw_nrf_baro_t *b) { return (int32_t)((b->pressure_cpa + 50u) / 100u); }

int fw_nrf_parse_accel(const fw_nrf_frame_t *f, fw_nrf_accel_t *out)
{
    if (!is_sensor(f, FW_NRF_SENSOR_ACCEL))
        return -1;
    out->x = (int16_t)u16le(f->payload + 2);
    out->y = (int16_t)u16le(f->payload + 4);
    out->z = (int16_t)u16le(f->payload + 6);
    return 0;
}

int fw_nrf_parse_key(const fw_nrf_frame_t *f, fw_nrf_key_t *out)
{
    if (f->id != FW_NRF_ID_SENSOR || f->plen < 7 || f->payload[0] != FW_NRF_SUB_KEY)
        return -1;
    out->index = f->payload[1];
    out->extra = f->payload[5];
    out->event = f->payload[6];
    return 0;
}

int fw_nrf_parse_clock(const fw_nrf_frame_t *f, fw_nrf_clock_t *out)
{
    if (f->id != FW_NRF_ID_STATUS || f->plen < 8 || f->payload[0] != FW_NRF_SUB_CLOCK)
        return -1;
    const uint8_t *p = f->payload;
    uint8_t day = p[5] & 0x1F;
    /* The vendor's own limits, tightened where it is lax (it lets 60 seconds and 24 hours through). */
    if (p[2] > 59 || p[3] > 59 || p[4] > 23 || day < 1 || p[6] > 11 || p[7] < 100 || p[7] > 138)
        return -1;
    out->second = p[2];
    out->minute = p[3];
    out->hour = p[4];
    out->day = day;
    out->month = (uint8_t)(p[6] + 1);
    out->year = (uint16_t)(1900 + p[7]);
    return 0;
}

int fw_nrf_parse_battery(const fw_nrf_frame_t *f, fw_nrf_battery_t *out)
{
    if (f->id != FW_NRF_ID_STATUS || f->plen < 7 || f->payload[0] != FW_NRF_SUB_BATTERY)
        return -1;
    out->millivolts = u16le(f->payload + 4);
    out->percent = f->payload[6];
    return 0;
}

int fw_nrf_parse_light(const fw_nrf_frame_t *f, fw_nrf_light_t *out)
{
    if (!is_sensor(f, FW_NRF_SENSOR_LIGHT))
        return -1;
    const uint8_t *p = f->payload;
    out->value = (uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);
    out->aux = u16le(p + 6);
    return 0;
}

int fw_nrf_keys_feed(fw_nrf_keys_t *k, const fw_nrf_key_t *ev, uint32_t now_ms, fw_nrf_edge_t out[2])
{
    if (ev->index >= FW_NRF_KEYS)
        return 0;
    uint8_t i = ev->index;
    int n = 0;
    switch (ev->event) {
    case FW_NRF_KEY_CLICK:
        /* A click while we believe the button is held means a hold end was lost: finish the hold. */
        if (!k->held[i])
            out[n++] = (fw_nrf_edge_t){i, 1};
        out[n++] = (fw_nrf_edge_t){i, 0};
        k->held[i] = 0;
        break;
    case FW_NRF_KEY_HELD:
        if (!k->held[i]) {
            k->held[i] = 1;
            out[n++] = (fw_nrf_edge_t){i, 1};
        }
        k->seen_ms[i] = now_ms;
        break;
    case FW_NRF_KEY_HOLD_END:
        if (k->held[i]) {
            k->held[i] = 0;
            out[n++] = (fw_nrf_edge_t){i, 0};
        }
        break;
    default: /* an event code never seen on the unit: not guessed at */
        break;
    }
    return n;
}

int fw_nrf_keys_tick(fw_nrf_keys_t *k, uint32_t now_ms, fw_nrf_edge_t out[FW_NRF_KEYS])
{
    int n = 0;
    for (uint8_t i = 0; i < FW_NRF_KEYS; i++) {
        if (k->held[i] && now_ms - k->seen_ms[i] > FW_NRF_KEY_LOST_MS) {
            k->held[i] = 0;
            out[n++] = (fw_nrf_edge_t){i, 0};
        }
    }
    return n;
}
