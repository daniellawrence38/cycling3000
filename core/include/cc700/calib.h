/* calib.h: zero-offset calibration of a Bluetooth power meter (brief 5.11).
 *
 * The firmware's sensor manager owns the GATT work; this module owns the state the Power meter screen
 * shows and the rules: supported only if the Cycling Power Feature (0x2A65) declares offset compensation
 * and the Control Point (0x2A66) exists; a request is opcode 0x0C; only an indication 0x20, 0x0C, 0x01
 * within 10 s is success. Values checked in WP7 (docs/PROGRESS.md): CPS Feature bit 9 "Offset
 * Compensation Supported" (bit 8 is the separate "Offset Compensation Indicator"), Start Offset
 * Compensation 0x0C, Response Code 0x20, Success 0x01. */
#ifndef CC700_CALIB_H
#define CC700_CALIB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_CPS_FEATURE_OFFSET_COMP_BIT 9
#define CC_CPS_OP_START_OFFSET_COMP 0x0C
#define CC_CPS_OP_RESPONSE 0x20
#define CC_CPS_RESP_SUCCESS 0x01
#define CC_CAL_TIMEOUT_MS 10000u

typedef enum { CC_CAL_IDLE, CC_CAL_RUNNING, CC_CAL_DONE, CC_CAL_FAILED, CC_CAL_UNSUPPORTED } cc_cal_state_t;

/* On power meter connect: the Feature value and whether the Control Point exists. On disconnect pass
 * connected 0. */
void cc_calibration_meter(int connected, uint32_t features, int has_control_point);
/* The UI's Calibrate. Returns 0 when started (state RUNNING), -1 otherwise (unsupported or running). */
int cc_calibration_start(uint32_t now_ms);
/* The sensor manager polls this; it returns 1 once per start, meaning: enable indications on the
 * Control Point and write { 0x0C }. */
int cc_calibration_take_request(void);
/* A Control Point indication. */
void cc_calibration_indication(uint32_t now_ms, const uint8_t *b, size_t len);
/* The write failed at the GATT level. */
void cc_calibration_write_failed(void);
void cc_calibration_tick(uint32_t now_ms);
cc_cal_state_t cc_calibration_state(void);
/* Offset reported with the success response (logged, never shown); returns 0 if none. */
int cc_calibration_offset(int16_t *offset);
/* After DONE or FAILED the screen returns to IDLE when left. */
void cc_calibration_ack(void);

#ifdef __cplusplus
}
#endif
#endif
