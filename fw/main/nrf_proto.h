/* nrf_proto.h: the framing and the decoded sensor messages of the nRF co-processor link
 * (docs/NRF-PROTOCOL.md). No ESP-IDF dependency; host-tested by core/tests/test_nrf_proto.c against
 * frames copied byte for byte from real captures.
 *
 * The only outbound frame the firmware ever sends is SendPowerOffCmd (fw_nrf_build_power_off, Q38). */
#ifndef FW_NRF_PROTO_H
#define FW_NRF_PROTO_H
#include <stddef.h>
#include <stdint.h>

#define FW_NRF_SYNC 0xA5
#define FW_NRF_SYNC2 0x6F
#define FW_NRF_DIR_OUT 0xF1
#define FW_NRF_MAX_LEN 132 /* the stock parser rejects a length byte above this */
#define FW_NRF_OVERHEAD 8  /* six header bytes and two of CRC */
#define FW_NRF_KIND_QUERY 1
#define FW_NRF_KIND_CMD 2
#define FW_NRF_POWER_OFF_FRAME_LEN 16

typedef struct {
    uint8_t kind;           /* 1..5 */
    uint8_t id;
    const uint8_t *payload; /* points into the caller's buffer */
    size_t plen;
} fw_nrf_frame_t;

/* CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR. */
uint16_t fw_nrf_crc16(const uint8_t *p, size_t n);

/* Builds one frame in out (needs plen + FW_NRF_OVERHEAD bytes), matching the stock builder at
 * 0x4205062c byte for byte. Returns the frame length (plen + 8), or -1 if kind/plen/cap is invalid. */
int fw_nrf_build_frame(uint8_t kind, uint8_t id, const uint8_t *payload, size_t plen, uint8_t *out, size_t cap);

/* Builds the 16-byte SendPowerOffCmd frame (0x42055bac: kind 2, id 16, payload e2 02 00 00 00 00 00 00,
 * Q38). Returns FW_NRF_POWER_OFF_FRAME_LEN (16). */
int fw_nrf_build_power_off(uint8_t out[FW_NRF_POWER_OFF_FRAME_LEN]);

/* Tries to take one frame from the front of buf.
 * Returns the bytes consumed (> 0) and fills *out; FW_NRF_MORE if the frame is not complete yet;
 * FW_NRF_NOT_FRAME or FW_NRF_BAD_CRC if buf does not start a valid frame, in which case the caller
 * drops ONE byte and tries again, as the stock parser does. */
#define FW_NRF_MORE 0
#define FW_NRF_NOT_FRAME (-1)
#define FW_NRF_BAD_CRC (-2)
int fw_nrf_take_frame(const uint8_t *buf, size_t len, fw_nrf_frame_t *out);

/* id 16, kind 4, sub-tag 0xF1: the sensor transport. Payload byte 1 selects the sensor. */
#define FW_NRF_ID_SENSOR 16
#define FW_NRF_KIND_STREAM 4
#define FW_NRF_SUB_SENSOR 0xF1
#define FW_NRF_SENSOR_ACCEL 0x01
#define FW_NRF_SENSOR_BARO 0x03

typedef struct {
    int16_t temp_c100;     /* hundredths of a degree Celsius */
    uint32_t pressure_cpa; /* hundredths of a pascal */
} fw_nrf_baro_t;

/* The barometer message. Returns 0 and fills *out, or -1 if the frame is not one, or if its pressure
 * is outside FW_NRF_BARO_MIN_PA..FW_NRF_BARO_MAX_PA. The range is the usual 300 to 1100 hPa of a MEMS
 * barometer (about 9000 m to below sea level): a value outside it means the message is not what the
 * capture says it is, and no altitude is better than a wrong one (Q19). */
#define FW_NRF_BARO_MIN_PA 30000
#define FW_NRF_BARO_MAX_PA 110000
int fw_nrf_parse_baro(const fw_nrf_frame_t *f, fw_nrf_baro_t *out);
/* Pressure in whole pascals, rounded half up. */
int32_t fw_nrf_baro_pa(const fw_nrf_baro_t *b);

typedef struct {
    int16_t x, y, z; /* about 1024 counts per g; axis order against the case is Unverified */
} fw_nrf_accel_t;

/* The accelerometer message. Returns 0 and fills *out, or -1. Not used by v1 (rule 4). */
int fw_nrf_parse_accel(const fw_nrf_frame_t *f, fw_nrf_accel_t *out);

/* The messages below are read out of the stock firmware's own handlers with Ghidra
 * (docs/NRF-PROTOCOL.md, "What the vendor's handlers say"). The layouts are the vendor's; what has
 * and has not been seen on the wire is said for each. */

/* id 16, sub-tag 0x49: a key event. The vendor handler (0x42051200) copies payload[1] as the key
 * index and payload[6] as the event code into the record its key queue reader logs as
 * "Index:%d Evt:%d", and keeps payload[5] beside them.
 * Confirmed on the unit, 17 November 2026 (outputs/nrf-capture-2.txt): index 0 is the power button,
 * 1 the bottom left, 2 the bottom right. A short press is ONE frame with event 1, sent when the
 * button is let go. A held button sends event 4 every 300 ms while it stays down and event 5 when it
 * is released. The nRF reports clicks, not edges. */
#define FW_NRF_SUB_KEY 0x49
#define FW_NRF_KEY_CLICK 1
#define FW_NRF_KEY_HELD 4
#define FW_NRF_KEY_HOLD_END 5
#define FW_NRF_KEYS 3
typedef struct {
    uint8_t index; /* which key */
    uint8_t event; /* the vendor's event code */
    uint8_t extra; /* payload[5]; purpose unknown */
} fw_nrf_key_t;
int fw_nrf_parse_key(const fw_nrf_frame_t *f, fw_nrf_key_t *out);

/* Turning the nRF's clicks into the down and up edges the core's hold detection wants (brief 6.4).
 *
 *   click (1)     -> down, then up at once: a short press
 *   held (4)      -> down at the first one; the repeats only say the button is still down
 *   hold end (5)  -> up
 *
 * If the repeats stop without a hold end (a lost frame, or the nRF restarting), the button is
 * released after FW_NRF_KEY_LOST_MS, so a button can never stay down for ever. The repeats come every
 * 300 ms, so one second is three missed in a row. */
#define FW_NRF_KEY_LOST_MS 1000u
typedef struct {
    uint8_t key;  /* 0 power, 1 left, 2 right */
    uint8_t down; /* 1 pressed, 0 released */
} fw_nrf_edge_t;
typedef struct {
    uint8_t held[FW_NRF_KEYS];
    uint32_t seen_ms[FW_NRF_KEYS];
} fw_nrf_keys_t;
/* Feeds one key event. Writes up to two edges to out and returns how many. */
int fw_nrf_keys_feed(fw_nrf_keys_t *k, const fw_nrf_key_t *ev, uint32_t now_ms, fw_nrf_edge_t out[2]);
/* Call regularly. Writes a release for each held button whose repeats have stopped; returns how many. */
int fw_nrf_keys_tick(fw_nrf_keys_t *k, uint32_t now_ms, fw_nrf_edge_t out[FW_NRF_KEYS]);

/* id 0, sub-tag 0x53: the nRF's calendar clock. The vendor handler (0x42050d78) reads seconds,
 * minutes, hours, day of month (low five bits), month from 0 and year from 1900, and range-checks
 * them. Seen on the wire: the 4 November 2026 capture decodes to 2026-11-04. Which time zone it keeps
 * is Unverified. */
#define FW_NRF_ID_STATUS 0
#define FW_NRF_SUB_CLOCK 0x53
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} fw_nrf_clock_t;
int fw_nrf_parse_clock(const fw_nrf_frame_t *f, fw_nrf_clock_t *out);

/* id 0, sub-tag 0x52: the battery. The vendor handler passes on payload[6] as a byte and
 * payload[4..5] as a uint16. Confirmed on the unit (outputs/nrf-capture-2.txt): 3477 mV and 2 %
 * rising on charge, and 4290 mV and 100 % when full. */
#define FW_NRF_SUB_BATTERY 0x52
typedef struct {
    uint16_t millivolts;
    uint8_t percent;
} fw_nrf_battery_t;
int fw_nrf_parse_battery(const fw_nrf_frame_t *f, fw_nrf_battery_t *out);

/* id 16, sub-tag 0xF1, sensor 0x04: the ambient light sensor. The vendor handler runs the uint32 at
 * payload[2..5] through a threshold table to pick a level (0x420511ac). Confirmed on the unit
 * (outputs/nrf-capture-2.txt): 0 to 30 covered, 600 in room light, 6000 to 9000 under a torch. */
#define FW_NRF_SENSOR_LIGHT 0x04
typedef struct {
    uint32_t value; /* payload[2..5] */
    uint16_t aux;   /* payload[6..7]; purpose unknown */
} fw_nrf_light_t;
int fw_nrf_parse_light(const fw_nrf_frame_t *f, fw_nrf_light_t *out);

/* Maps the nRF light sensor's banded value to a backlight percentage (1..100) using the stock
 * firmware's threshold table at 0x3c36eb7c (FUN_420511ac). Returns 60 when lux < 0 (no light sensor
 * reading, spec 10.1). */
int fw_nrf_light_backlight_pct(int32_t lux);
#endif
