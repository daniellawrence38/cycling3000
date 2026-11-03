/* calib.c: zero-offset calibration state (brief 5.11). */
#include "cc700/calib.h"

static struct {
    int connected, supported;
    cc_cal_state_t st;
    int request;
    uint32_t started_ms;
    int has_offset;
    int16_t offset;
} C = {0, 0, CC_CAL_UNSUPPORTED, 0, 0, 0, 0};

void cc_calibration_meter(int connected, uint32_t features, int has_control_point)
{
    C.connected = connected;
    C.supported = connected && has_control_point && ((features >> CC_CPS_FEATURE_OFFSET_COMP_BIT) & 1u);
    C.request = 0;
    if (C.st == CC_CAL_RUNNING && !connected) {
        C.st = CC_CAL_FAILED; /* the meter went away mid-calibration: never claim success */
        return;
    }
    if (C.st != CC_CAL_DONE && C.st != CC_CAL_FAILED)
        C.st = C.supported ? CC_CAL_IDLE : CC_CAL_UNSUPPORTED;
}

int cc_calibration_start(uint32_t now_ms)
{
    if (!C.supported || C.st == CC_CAL_RUNNING)
        return -1;
    C.st = CC_CAL_RUNNING;
    C.request = 1;
    C.started_ms = now_ms;
    C.has_offset = 0;
    return 0;
}

int cc_calibration_take_request(void)
{
    int r = C.request;
    C.request = 0;
    return r;
}

void cc_calibration_indication(uint32_t now_ms, const uint8_t *b, size_t len)
{
    (void)now_ms;
    if (C.st != CC_CAL_RUNNING || len < 3 || b[0] != CC_CPS_OP_RESPONSE || b[1] != CC_CPS_OP_START_OFFSET_COMP)
        return; /* not the answer to our request */
    if (b[2] == CC_CPS_RESP_SUCCESS) {
        C.st = CC_CAL_DONE;
        if (len >= 5) {
            C.offset = (int16_t)(uint16_t)(b[3] | (b[4] << 8));
            C.has_offset = 1;
        }
    } else {
        C.st = CC_CAL_FAILED;
    }
}

void cc_calibration_write_failed(void)
{
    if (C.st == CC_CAL_RUNNING)
        C.st = CC_CAL_FAILED;
}

void cc_calibration_tick(uint32_t now_ms)
{
    if (C.st == CC_CAL_RUNNING && now_ms - C.started_ms >= CC_CAL_TIMEOUT_MS)
        C.st = CC_CAL_FAILED;
}

cc_cal_state_t cc_calibration_state(void) { return C.st; }

int cc_calibration_offset(int16_t *offset)
{
    if (C.has_offset && offset)
        *offset = C.offset;
    return C.has_offset;
}

void cc_calibration_ack(void)
{
    if (C.st == CC_CAL_DONE || C.st == CC_CAL_FAILED)
        C.st = C.supported ? CC_CAL_IDLE : CC_CAL_UNSUPPORTED;
}
