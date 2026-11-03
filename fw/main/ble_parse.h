/* ble_parse.h: the sensor characteristics the sensor manager reads (no ESP-IDF dependency, host-tested
 * by core/tests/test_fw_parse.c). */
#ifndef FW_BLE_PARSE_H
#define FW_BLE_PARSE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t watts;
    int has_crank;
    uint16_t crank_revs;      /* cumulative */
    uint16_t crank_time;      /* last crank event, 1/1024 s */
} fw_cp_meas_t;

/* Cycling Power Measurement (0x2A63). Returns 0, or -1 if too short for the flags it declares. */
int fw_parse_cp_measurement(const uint8_t *b, size_t len, fw_cp_meas_t *out);
/* Heart Rate Measurement (0x2A37). Returns bpm, or -1. */
int fw_parse_hr_measurement(const uint8_t *b, size_t len);
/* Cycling Power Feature (0x2A65): a uint32. Returns 0, or -1 if short. */
int fw_parse_cp_feature(const uint8_t *b, size_t len, uint32_t *features);
#endif
