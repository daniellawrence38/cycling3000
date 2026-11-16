/* nrf_proto.h: the framing and the decoded sensor messages of the nRF co-processor link
 * (docs/NRF-PROTOCOL.md). No ESP-IDF dependency; host-tested by core/tests/test_nrf_proto.c against
 * frames copied byte for byte from a real capture (outputs/nrf-capture-2026-11-04.txt).
 *
 * Receive side only. Nothing here builds a frame, on purpose (Q38). */
#ifndef FW_NRF_PROTO_H
#define FW_NRF_PROTO_H
#include <stddef.h>
#include <stdint.h>

#define FW_NRF_SYNC 0xA5
#define FW_NRF_SYNC2 0x6F
#define FW_NRF_MAX_LEN 132 /* the stock parser rejects a length byte above this */
#define FW_NRF_OVERHEAD 8  /* six header bytes and two of CRC */

typedef struct {
    uint8_t kind;           /* 1..5 */
    uint8_t id;
    const uint8_t *payload; /* points into the caller's buffer */
    size_t plen;
} fw_nrf_frame_t;

/* CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR. */
uint16_t fw_nrf_crc16(const uint8_t *p, size_t n);

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
#endif
