/* sensors.c: the sensor manager (brief 4, 5.1, 5.11): the BLE central half of hal_ble.h. Up to two
 * sensor connections: a power meter (Cycling Power, 0x1818) and a heart-rate strap (0x180D).
 *
 * NimBLE callbacks run in the host task, but the core belongs to the core task, so everything the core
 * must hear (samples, sensor states, the calibration capability and indications) goes through a queue
 * that main.c drains. Skeleton status: the GATT sequence is written to the specification but has not
 * run on hardware; pairing with saved sensors, reconnect backoff (spec 10.7) and the Sensors screen's
 * scan list come with bring-up step 6. */
#include <string.h>

#include "ble_parse.h"
#include "cc700/calib.h"
#include "cc700/core.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "fw.h"
#include "hal_ble.h"
#include "hal_clock.h"
#include "host/ble_hs.h"
#include "mem_esp.h"

static const char *TAG = "sensors";

#define UUID_CPS 0x1818
#define UUID_CP_MEAS 0x2A63
#define UUID_CP_FEATURE 0x2A65
#define UUID_CP_CTRL 0x2A66
#define UUID_HRS 0x180D
#define UUID_HR_MEAS 0x2A37

typedef enum { EV_POWER, EV_HR, EV_STATE, EV_CAL_METER, EV_CAL_IND, EV_CAL_WRITE_FAILED } ev_kind_t;
typedef struct {
    ev_kind_t kind;
    uint32_t at_ms;
    fw_cp_meas_t cp;
    int bpm;
    int sensor;              /* 1 power, 2 heart rate */
    cc_sens_state_t state;
    uint32_t features;
    int has_cp, connected;
    uint8_t ind[8];
    uint8_t ind_len;
} sens_ev_t;

typedef struct {
    uint16_t conn;
    uint16_t svc_start, svc_end;
    uint16_t meas, feature, ctrl;   /* value handles, 0 if absent */
    uint32_t features;
} link_t;

static QueueHandle_t q;
static link_t L[3]; /* indexed by hal_sensor_kind_t (1 power, 2 heart rate) */

static void post(const sens_ev_t *e) { xQueueSend(q, e, 0); }

static void post_state(int kind, cc_sens_state_t st)
{
    sens_ev_t e = {.kind = EV_STATE, .sensor = kind, .state = st};
    post(&e);
}

static int kind_of(uint16_t conn)
{
    for (int k = 1; k <= 2; k++)
        if (L[k].conn == conn && conn != BLE_HS_CONN_HANDLE_NONE)
            return k;
    return 0;
}

/* CCCD: written at value handle + 1. Most sensors place it there; proper descriptor discovery is a
 * bring-up item (step 6). */
static int write_cccd(uint16_t conn, uint16_t val_handle, uint16_t bits)
{
    uint8_t v[2] = {(uint8_t)bits, (uint8_t)(bits >> 8)};
    return ble_gattc_write_flat(conn, val_handle + 1, v, sizeof v, NULL, NULL);
}

static int on_feature(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    int k = kind_of(conn);
    if (k != HAL_SENSOR_POWER)
        return 0;
    uint32_t f = 0;
    if (err->status == 0 && attr) {
        uint8_t b[4];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(attr->om, b, sizeof b, &n) == 0)
            fw_parse_cp_feature(b, n, &f);
    }
    L[k].features = f;
    sens_ev_t e = {.kind = EV_CAL_METER, .connected = 1, .features = f, .has_cp = L[k].ctrl != 0};
    post(&e);
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    int k = kind_of(conn);
    if (!k)
        return 0;
    if (err->status == 0 && chr) {
        uint16_t u = ble_uuid_u16(&chr->uuid.u);
        if (u == UUID_CP_MEAS || u == UUID_HR_MEAS) L[k].meas = chr->val_handle;
        if (u == UUID_CP_FEATURE) L[k].feature = chr->val_handle;
        if (u == UUID_CP_CTRL) L[k].ctrl = chr->val_handle;
        return 0;
    }
    /* discovery finished */
    if (!L[k].meas) {
        ESP_LOGW(TAG, "sensor %d has no measurement characteristic", k);
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    write_cccd(conn, L[k].meas, 0x0001);
    if (k == HAL_SENSOR_POWER) {
        if (L[k].feature)
            ble_gattc_read(conn, L[k].feature, on_feature, NULL);
        else {
            sens_ev_t e = {.kind = EV_CAL_METER, .connected = 1, .features = 0, .has_cp = L[k].ctrl != 0};
            post(&e);
        }
    }
    post_state(k, CC_SENS_SEARCHING); /* OK once data arrives (spec 10.7) */
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    int k = kind_of(conn);
    if (!k)
        return 0;
    if (err->status == 0 && svc) {
        L[k].svc_start = svc->start_handle;
        L[k].svc_end = svc->end_handle;
        return 0;
    }
    if (!L[k].svc_start) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    ble_gattc_disc_all_chrs(conn, L[k].svc_start, L[k].svc_end, on_chr, NULL);
    return 0;
}

static int central_event(struct ble_gap_event *ev, void *arg)
{
    int k = (int)(intptr_t)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            post_state(k, CC_SENS_LOST);
            return 0;
        }
        L[k].conn = ev->connect.conn_handle;
        L[k].svc_start = L[k].svc_end = L[k].meas = L[k].feature = L[k].ctrl = 0;
        {
            ble_uuid16_t u = BLE_UUID16_INIT(k == HAL_SENSOR_POWER ? UUID_CPS : UUID_HRS);
            ble_gattc_disc_svc_by_uuid(L[k].conn, &u.u, on_svc, NULL);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        L[k].conn = BLE_HS_CONN_HANDLE_NONE;
        post_state(k, CC_SENS_LOST);
        if (k == HAL_SENSOR_POWER) {
            sens_ev_t e = {.kind = EV_CAL_METER, .connected = 0};
            post(&e);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t b[32];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(ev->notify_rx.om, b, sizeof b, &n) != 0)
            return 0;
        sens_ev_t e = {.at_ms = hal_clock_monotonic_ms()};
        if (k == HAL_SENSOR_POWER && ev->notify_rx.attr_handle == L[k].ctrl && ev->notify_rx.indication) {
            e.kind = EV_CAL_IND;
            e.ind_len = (uint8_t)(n < sizeof e.ind ? n : sizeof e.ind);
            memcpy(e.ind, b, e.ind_len);
            post(&e);
        } else if (k == HAL_SENSOR_POWER && ev->notify_rx.attr_handle == L[k].meas) {
            if (fw_parse_cp_measurement(b, n, &e.cp) == 0) {
                e.kind = EV_POWER;
                post(&e);
            }
        } else if (k == HAL_SENSOR_HR && ev->notify_rx.attr_handle == L[k].meas) {
            e.bpm = fw_parse_hr_measurement(b, n);
            if (e.bpm >= 0) {
                e.kind = EV_HR;
                post(&e);
            }
        }
        return 0;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ hal_ble.h, central half */
void hal_ble_scan(int on)
{
    (void)on;
    /* The Sensors screen's scan list (screen "Sensors", searching state) comes with bring-up step 6. */
    static int once;
    if (!once) {
        once = 1;
        ESP_LOGW(TAG, "hal_ble_scan: not implemented");
    }
}

int hal_ble_connect(hal_sensor_kind_t kind, const uint8_t addr[6], uint8_t addr_type)
{
    if (kind != HAL_SENSOR_POWER && kind != HAL_SENSOR_HR)
        return -1;
    ble_addr_t a = {.type = addr_type};
    memcpy(a.val, addr, 6);
    uint8_t own;
    if (ble_hs_id_infer_auto(0, &own) != 0)
        return -1;
    post_state(kind, CC_SENS_SEARCHING);
    return ble_gap_connect(own, &a, 10000, NULL, central_event, (void *)(intptr_t)kind) == 0 ? 0 : -1;
}

void hal_ble_disconnect(hal_sensor_kind_t kind)
{
    if ((kind == HAL_SENSOR_POWER || kind == HAL_SENSOR_HR) && L[kind].conn != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(L[kind].conn, BLE_ERR_REM_USER_CONN_TERM);
}

/* ------------------------------------------------------------------ core task side */
static int on_cal_write(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (err->status != 0) {
        sens_ev_t e = {.kind = EV_CAL_WRITE_FAILED};
        post(&e);
    }
    return 0;
}

void fw_sensors_start(void)
{
    if (!q)
        q = fw_queue_create_psram(64, sizeof(sens_ev_t)); /* host task to core task only; PSRAM if any */
    for (int k = 0; k < 3; k++)
        L[k].conn = BLE_HS_CONN_HANDLE_NONE;
}

void fw_sensors_tick(uint32_t now_ms)
{
    if (!q)
        return;
    sens_ev_t e;
    while (xQueueReceive(q, &e, 0) == pdTRUE) {
        switch (e.kind) {
        case EV_POWER:
            cc_on_sensor_state(1, CC_SENS_OK);
            cc_on_power_sample(e.at_ms, e.cp.watts, e.cp.has_crank, e.cp.crank_revs, e.cp.crank_time);
            break;
        case EV_HR:
            cc_on_sensor_state(2, CC_SENS_OK);
            cc_on_hr_sample(e.at_ms, e.bpm);
            break;
        case EV_STATE: cc_on_sensor_state(e.sensor, e.state); break;
        case EV_CAL_METER: cc_calibration_meter(e.connected, e.features, e.has_cp); break;
        case EV_CAL_IND: cc_calibration_indication(now_ms, e.ind, e.ind_len); break;
        case EV_CAL_WRITE_FAILED: cc_calibration_write_failed(); break;
        }
    }
    /* Calibration (5.11): indications on the Control Point, then opcode 0x0C. */
    if (cc_calibration_take_request()) {
        link_t *p = &L[HAL_SENSOR_POWER];
        static const uint8_t op = CC_CPS_OP_START_OFFSET_COMP;
        if (p->conn == BLE_HS_CONN_HANDLE_NONE || !p->ctrl || write_cccd(p->conn, p->ctrl, 0x0002) != 0 ||
            ble_gattc_write_flat(p->conn, p->ctrl, &op, 1, on_cal_write, NULL) != 0)
            cc_calibration_write_failed();
    }
}
