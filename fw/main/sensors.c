/* sensors.c: the sensor manager (brief 4, 5.1, 5.11; spec 6 screens 9, 10, 11, 22; spec 10.7): the BLE
 * central half of hal_ble.h. One power meter (Cycling Power, 0x1818) and one heart-rate strap (0x180D).
 *
 * Two sides:
 *  - NimBLE callbacks run in the host task. They run the GATT sequence on their own (service, then
 *    characteristics, then descriptors, then the measurement CCCD), log each step, and post what the
 *    core task must hear through a queue. Scan reports go into a mutex-guarded list instead, because a
 *    busy scan would flood the queue.
 *  - Everything that decides runs in the core task (fw_sensors_tick and the UI hooks): which sensors
 *    are saved, when to scan, when to connect, the reconnect backoff, saving and forgetting.
 *
 * Identities live in config/sensors.a|b on the separate storage (brief 5.9, cc_bonds_*), not in NimBLE's
 * store: the firmware never initialises NVS (Q29). Sensors are not bonded at the link layer; their
 * measurements are readable without encryption on the meters and straps seen so far (Q42).
 *
 * The radio is shared with the phone link's advertising (link_gatt.c). Only one GAP procedure runs at
 * a time (NimBLE refuses a connect while scanning and a scan while connecting), and both the scan and
 * the connection set-up listen for at most half the time, so advertising events keep their slots.
 *
 * Every step logs at WARN under the tag "sensors" (bring-up step 6 reads them from the capture). */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ble_parse.h"
#include "cc700/calib.h"
#include "cc700/core.h"
#include "cc700/settings.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "fw.h"
#include "fw_sensors.h"
#include "hal_ble.h"
#include "hal_clock.h"
#include "host/ble_hs.h"
#include "mem_esp.h"
#include "sens_logic.h"

static const char *TAG = "sensors";

#define UUID_CPS 0x1818
#define UUID_CP_MEAS 0x2A63
#define UUID_CP_FEATURE 0x2A65
#define UUID_CP_CTRL 0x2A66
#define UUID_HRS 0x180D
#define UUID_HR_MEAS 0x2A37
#define UUID_CCCD 0x2902

#define CONNECT_TIMEOUT_MS 10000 /* one attempt; then the spec 10.7 backoff */
#define PAIR_TRIES 3             /* a new sensor that cannot be set up this often is not saved */
/* Scan: active (the name usually comes in the scan response), 50 ms of every 100 ms. Units 0.625 ms. */
#define SCAN_ITVL 160
#define SCAN_WINDOW 80
/* Connection set-up: listen 30 ms of every 60 ms; then a 30-50 ms interval, 4 s supervision timeout. */
static const struct ble_gap_conn_params CONN_PARAMS = {
    .scan_itvl = 96, .scan_window = 48, .itvl_min = 24, .itvl_max = 40, .latency = 0,
    .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
};

static const char *const KIND_NAME[3] = {"?", "power meter", "heart rate"};

/* ------------------------------------------------------------------ host task side */
typedef enum {
    EV_POWER, EV_HR, EV_CAL_METER, EV_CAL_IND, EV_CAL_WRITE_FAILED,
    EV_CONNECTED, EV_CONNECT_FAILED, EV_SUBSCRIBED, EV_DISCONNECTED, EV_SYNC,
} ev_kind_t;
typedef struct {
    ev_kind_t kind;
    uint32_t at_ms;
    fw_cp_meas_t cp;
    int bpm;
    int sensor;              /* 1 power, 2 heart rate */
    int status;              /* connect status or disconnect reason */
    uint32_t features;
    int has_cp, connected;
    uint8_t ind[8];
    uint8_t ind_len;
} sens_ev_t;

#define MAX_CHRS 16
typedef struct {
    uint16_t conn;
    uint16_t svc_start, svc_end;
    uint16_t meas, feature, ctrl;   /* value handles, 0 if absent */
    uint16_t meas_cccd, ctrl_cccd;  /* 0 until found */
    uint16_t chr_vals[MAX_CHRS];    /* every characteristic value handle in the service */
    int n_chrs;
} link_t;

static QueueHandle_t q;
static link_t L[3]; /* indexed by hal_sensor_kind_t (1 power, 2 heart rate); host task only */
static SemaphoreHandle_t scan_lock;
static fw_scan_list_t scan_list; /* under scan_lock */

static void post(const sens_ev_t *e)
{
    if (q && xQueueSend(q, e, 0) != pdTRUE)
        ESP_LOGW(TAG, "event queue full: event %d for sensor %d dropped", (int)e->kind, e->sensor);
}

static void post_simple(ev_kind_t kind, int sensor, int status)
{
    sens_ev_t e = {.kind = kind, .sensor = sensor, .status = status, .at_ms = hal_clock_monotonic_ms()};
    post(&e);
}

static int kind_of(uint16_t conn)
{
    for (int k = 1; k <= 2; k++)
        if (L[k].conn == conn && conn != BLE_HS_CONN_HANDLE_NONE)
            return k;
    return 0;
}

static void drop_link(int k, uint16_t conn, const char *why, int status)
{
    ESP_LOGW(TAG, "%s: %s (status %d); disconnecting", KIND_NAME[k], why, status);
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
}

static int write_cccd(uint16_t conn, uint16_t cccd_handle, uint16_t bits, ble_gatt_attr_fn *cb)
{
    uint8_t v[2] = {(uint8_t)bits, (uint8_t)(bits >> 8)};
    return ble_gattc_write_flat(conn, cccd_handle, v, sizeof v, cb, NULL);
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
    ESP_LOGW(TAG, "power meter: feature read %s, flags 0x%08" PRIx32 ", control point %s",
             err->status == 0 ? "ok" : "failed", f, L[k].ctrl ? "present" : "absent");
    sens_ev_t e = {.kind = EV_CAL_METER, .connected = 1, .features = f, .has_cp = L[k].ctrl != 0};
    post(&e);
    return 0;
}

static int on_subscribe(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)attr; (void)arg;
    int k = kind_of(conn);
    if (!k)
        return 0;
    if (err->status != 0) {
        drop_link(k, conn, "subscribe failed: the measurement CCCD write was refused", err->status);
        return 0;
    }
    ESP_LOGW(TAG, "%s: subscribe ok, notifications on (measurement 0x%04x, CCCD 0x%04x)", KIND_NAME[k],
             L[k].meas, L[k].meas_cccd);
    post_simple(EV_SUBSCRIBED, k, 0);
    if (k == HAL_SENSOR_POWER) {
        if (L[k].feature && ble_gattc_read(conn, L[k].feature, on_feature, NULL) == 0)
            return 0;
        ESP_LOGW(TAG, "power meter: no feature characteristic; calibration unsupported");
        sens_ev_t e = {.kind = EV_CAL_METER, .connected = 1, .features = 0, .has_cp = L[k].ctrl != 0};
        post(&e);
    }
    return 0;
}

static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle; (void)arg;
    int k = kind_of(conn);
    if (!k)
        return 0;
    if (err->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == UUID_CCCD) {
            uint16_t owner = fw_dsc_owner(L[k].chr_vals, L[k].n_chrs, dsc->handle);
            if (owner && owner == L[k].meas && !L[k].meas_cccd) L[k].meas_cccd = dsc->handle;
            if (owner && owner == L[k].ctrl && !L[k].ctrl_cccd) L[k].ctrl_cccd = dsc->handle;
        }
        return 0;
    }
    if (err->status != BLE_HS_EDONE)
        ESP_LOGW(TAG, "%s: descriptor discovery ended with status %d", KIND_NAME[k], err->status);
    if (!L[k].meas_cccd) {
        /* Not found (or discovery failed): most sensors put it straight after the value. */
        L[k].meas_cccd = (uint16_t)(L[k].meas + 1);
        ESP_LOGW(TAG, "%s: no CCCD found for the measurement; trying handle 0x%04x", KIND_NAME[k], L[k].meas_cccd);
    }
    int rc = write_cccd(conn, L[k].meas_cccd, 0x0001, on_subscribe);
    if (rc != 0)
        drop_link(k, conn, "subscribe could not start", rc);
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
        if (L[k].n_chrs < MAX_CHRS) L[k].chr_vals[L[k].n_chrs++] = chr->val_handle;
        if ((k == HAL_SENSOR_POWER && u == UUID_CP_MEAS) || (k == HAL_SENSOR_HR && u == UUID_HR_MEAS))
            L[k].meas = chr->val_handle;
        if (k == HAL_SENSOR_POWER && u == UUID_CP_FEATURE) L[k].feature = chr->val_handle;
        if (k == HAL_SENSOR_POWER && u == UUID_CP_CTRL) L[k].ctrl = chr->val_handle;
        return 0;
    }
    ESP_LOGW(TAG, "%s: characteristics (status %d): %d found, measurement 0x%04x, feature 0x%04x, control 0x%04x",
             KIND_NAME[k], err->status == BLE_HS_EDONE ? 0 : err->status, L[k].n_chrs, L[k].meas, L[k].feature,
             L[k].ctrl);
    if (!L[k].meas) {
        drop_link(k, conn, "discover failed: no measurement characteristic", err->status);
        return 0;
    }
    int rc = ble_gattc_disc_all_dscs(conn, L[k].svc_start, L[k].svc_end, on_dsc, NULL);
    if (rc != 0)
        drop_link(k, conn, "descriptor discovery could not start", rc);
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
        drop_link(k, conn, k == HAL_SENSOR_POWER ? "discover failed: no service 0x1818" : "discover failed: no service 0x180D",
                  err->status);
        return 0;
    }
    ESP_LOGW(TAG, "%s: discover ok, service handles 0x%04x-0x%04x", KIND_NAME[k], L[k].svc_start, L[k].svc_end);
    int rc = ble_gattc_disc_all_chrs(conn, L[k].svc_start, L[k].svc_end, on_chr, NULL);
    if (rc != 0)
        drop_link(k, conn, "characteristic discovery could not start", rc);
    return 0;
}

static int central_event(struct ble_gap_event *ev, void *arg)
{
    int k = (int)(intptr_t)arg;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            post_simple(EV_CONNECT_FAILED, k, ev->connect.status);
            return 0;
        }
        memset(&L[k], 0, sizeof L[k]);
        L[k].conn = ev->connect.conn_handle;
        post_simple(EV_CONNECTED, k, 0);
        {
            ble_uuid16_t u = BLE_UUID16_INIT(k == HAL_SENSOR_POWER ? UUID_CPS : UUID_HRS);
            int rc = ble_gattc_disc_svc_by_uuid(L[k].conn, &u.u, on_svc, NULL);
            if (rc != 0)
                drop_link(k, L[k].conn, "service discovery could not start", rc);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (ev->disconnect.conn.conn_handle != L[k].conn) {
            /* An older link of this kind (forgotten, then a new sensor paired) closing late. */
            ESP_LOGW(TAG, "%s: an earlier link closed (reason 0x%x)", KIND_NAME[k], ev->disconnect.reason);
            return 0;
        }
        L[k].conn = BLE_HS_CONN_HANDLE_NONE;
        post_simple(EV_DISCONNECTED, k, ev->disconnect.reason);
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
        sens_ev_t e = {.at_ms = hal_clock_monotonic_ms(), .sensor = k};
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
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGW(TAG, "%s: encryption change, status %d", KIND_NAME[k], ev->enc_change.status);
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* A sensor asking for a passkey: the unit cannot enter one (Q42). Refuse rather than hang. */
        ESP_LOGW(TAG, "%s: the sensor asked for pairing with a passkey (action %d); refused", KIND_NAME[k],
                 ev->passkey.params.action);
        ble_gap_terminate(ev->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
        return 0;
    default:
        return 0;
    }
}

static int disc_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    if (ev->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGW(TAG, "scan ended by the stack (reason %d)", ev->disc_complete.reason);
        return 0;
    }
    if (ev->type != BLE_GAP_EVENT_DISC)
        return 0;
    fw_adv_t a;
    fw_adv_parse(ev->disc.data, ev->disc.length_data, &a);
    int announce = 0, is_rsp = ev->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP, dump;
    fw_scan_ent_t snap = {0};
    if (xSemaphoreTake(scan_lock, pdMS_TO_TICKS(5)) != pdTRUE)
        return 0;
    fw_scan_ent_t *e = fw_scan_report(&scan_list, ev->disc.addr.val, ev->disc.addr.type, ev->disc.rssi, &a,
                                      hal_clock_monotonic_ms(), &announce);
    dump = fw_scan_should_dump(e, is_rsp);
    if (announce || dump)
        snap = *e;
    xSemaphoreGive(scan_lock);
    if (dump) {
        /* The raw payload once per device and packet type, for sensors, devices with no name yet and
           sensor-like names: the H1's and PM500's advertising is Unverified, and the first capture
           settles where their UUIDs, name and manufacturer data sit (0x1816 CSC is listed, not used). */
        char s[18], hex[2 * 64 + 1], uu[8 * 5 + 1];
        fw_uuid_list(&a, uu, sizeof uu);
        fw_addr_str(snap.addr, s);
        fw_hex(ev->disc.data, ev->disc.length_data, hex, sizeof hex);
        ESP_LOGW(TAG, "adv %s (type %u) %s rssi %d: uuids [%s], name \"%s\", mfr %s, raw %s", s, snap.addr_type,
                 is_rsp ? "scan-rsp" : "adv", ev->disc.rssi, uu, a.has_name ? a.name : "", a.has_mfr ? "yes" : "no",
                 hex);
    }
    if (announce) {
        char s[18];
        fw_addr_str(snap.addr, s);
        ESP_LOGW(TAG, "found %s (type %u): %s%s%s, rssi %d, name \"%s\"", s, snap.addr_type,
                 snap.kinds & FW_KIND_BIT(FW_KIND_POWER) ? "power meter" : "",
                 snap.kinds == 3 ? " + " : "", snap.kinds & FW_KIND_BIT(FW_KIND_HR) ? "heart rate" : "",
                 snap.rssi, snap.name[0] ? snap.name : "(none yet)");
    }
    return 0;
}

/* ------------------------------------------------------------------ core task side */
typedef enum { SL_WAIT, SL_CONNECTING, SL_LINKED, SL_UP } slot_state_t;
static const char *const SLOT_STATE[] = {"waiting", "connecting", "connected, setting up", "subscribed"};

typedef struct {
    int have;           /* a target: a saved sensor, or one being paired */
    int pairing;        /* not saved yet: saved when the first subscribe succeeds */
    cc_bond_t id;
    slot_state_t st;
    unsigned failures;  /* attempts or drops in a row since the last subscribe (spec 10.7) */
    uint32_t due_ms;    /* next attempt, in SL_WAIT */
    uint32_t last_sample_ms;
    int have_sample;
    int rssi;           /* of the live connection, 0 unknown */
} slot_t;

static slot_t S[3];
static cc_bonds_t bonds;
static cc_slots_t bond_slots;
static int bonds_loaded;
static int scan_wanted, scan_running, cancel_sent;
static int connecting; /* the kind whose connect is in flight, 0 none */
static uint32_t scan_retry_ms, rssi_next_ms;

static const char *addr_s(const cc_bond_t *b)
{
    static char s[2][18];
    static int i;
    i ^= 1;
    fw_addr_str(b->addr, s[i]);
    return s[i];
}

static void core_state(int k)
{
    cc_on_sensor_state(k, S[k].have ? CC_SENS_SEARCHING : CC_SENS_OFF); /* OK comes with data (10.7) */
}

static void save_bonds(const char *why)
{
    hal_fs_t *fs = cc_fs();
    int rc = fs ? cc_bonds_save(fs, &bonds, &bond_slots) : -1;
    ESP_LOGW(TAG, "%s: config/sensors %s (%d saved)", why, rc == 0 ? "written" : "NOT written (storage error)",
             bonds.count);
}

static void retry_later(int k, uint32_t now, const char *what, int status)
{
    slot_t *s = &S[k];
    s->failures++;
    s->st = SL_WAIT;
    if (s->pairing && s->failures >= PAIR_TRIES) {
        ESP_LOGW(TAG, "%s %s: %s (status %d); pairing abandoned after %d tries, not saved", KIND_NAME[k],
                 addr_s(&s->id), what, status, PAIR_TRIES);
        memset(s, 0, sizeof *s);
        core_state(k);
        return;
    }
    uint32_t d = fw_backoff_ms(s->failures);
    s->due_ms = now + d;
    ESP_LOGW(TAG, "%s %s: %s (status %d); next try in %" PRIu32 " s (failure %u)", KIND_NAME[k],
             addr_s(&s->id), what, status, d / 1000u, s->failures);
}

static void load_bonds(uint32_t now)
{
    hal_fs_t *fs = cc_fs();
    if (!fs)
        return;
    bonds_loaded = 1;
    int got = cc_bonds_load(fs, &bonds, &bond_slots);
    ESP_LOGW(TAG, "config/sensors: %s, %d saved", got ? "loaded" : "none valid", bonds.count);
    for (int k = 1; k <= 2; k++) {
        const cc_bond_t *b = cc_bonds_find(&bonds, (uint8_t)k);
        if (!b)
            continue;
        memset(&S[k], 0, sizeof S[k]);
        S[k].have = 1;
        S[k].id = *b;
        S[k].st = SL_WAIT;
        S[k].due_ms = now;
        ESP_LOGW(TAG, "saved %s %s (type %u) \"%s\": reconnecting", KIND_NAME[k], addr_s(b), b->addr_type, b->name);
        core_state(k);
    }
}

static void on_event(const sens_ev_t *e, uint32_t now)
{
    int k = e->sensor;
    slot_t *s = &S[k >= 1 && k <= 2 ? k : 0];
    switch (e->kind) {
    case EV_POWER:
        if (!s->have) break; /* forgotten while its link closes */
        s->last_sample_ms = e->at_ms;
        s->have_sample = 1;
        cc_on_sensor_state(1, CC_SENS_OK);
        cc_on_power_sample(e->at_ms, e->cp.watts, e->cp.has_crank, e->cp.crank_revs, e->cp.crank_time);
        break;
    case EV_HR:
        if (!s->have) break;
        s->last_sample_ms = e->at_ms;
        s->have_sample = 1;
        cc_on_sensor_state(2, CC_SENS_OK);
        cc_on_hr_sample(e->at_ms, e->bpm);
        break;
    case EV_CAL_METER: cc_calibration_meter(e->connected, e->features, e->has_cp); break;
    case EV_CAL_IND: cc_calibration_indication(now, e->ind, e->ind_len); break;
    case EV_CAL_WRITE_FAILED: cc_calibration_write_failed(); break;
    case EV_CONNECTED:
        if (connecting == k)
            connecting = 0;
        cancel_sent = 0; /* a cancel that lost the race to the connection */
        if (!s->have) {
            ESP_LOGW(TAG, "%s: connected after it was forgotten; disconnecting", KIND_NAME[k]);
            hal_ble_disconnect((hal_sensor_kind_t)k);
            break;
        }
        s->st = SL_LINKED;
        ESP_LOGW(TAG, "%s %s: connected; discovering", KIND_NAME[k], addr_s(&s->id));
        break;
    case EV_CONNECT_FAILED:
        if (connecting == k)
            connecting = 0;
        if (!s->have)
            break;
        if (cancel_sent) {
            /* Cancelled by us to make way for the scan: not the sensor's failure. */
            cancel_sent = 0;
            s->st = SL_WAIT;
            s->due_ms = now;
            ESP_LOGW(TAG, "%s %s: connect cancelled for the scan", KIND_NAME[k], addr_s(&s->id));
            break;
        }
        retry_later(k, now, e->status == BLE_HS_ETIMEOUT ? "connect failed: not found within 10 s" : "connect failed",
                    e->status);
        break;
    case EV_SUBSCRIBED:
        if (!s->have)
            break;
        s->st = SL_UP;
        s->failures = 0;
        if (s->pairing) {
            s->pairing = 0;
            if (cc_bonds_put(&bonds, &s->id) == 0)
                save_bonds("paired");
            ESP_LOGW(TAG, "paired %s %s (type %u) \"%s\"", KIND_NAME[k], addr_s(&s->id), s->id.addr_type, s->id.name);
        }
        core_state(k);
        break;
    case EV_DISCONNECTED:
        if (connecting == k)
            connecting = 0;
        if (!s->have) {
            ESP_LOGW(TAG, "%s: disconnected (reason 0x%x); forgotten, not reconnecting", KIND_NAME[k], e->status);
            break;
        }
        s->rssi = 0;
        retry_later(k, now, "disconnected", e->status);
        break;
    case EV_SYNC:
        /* NimBLE (re)started: every link and procedure is gone. */
        connecting = 0;
        cancel_sent = 0;
        scan_running = 0;
        for (int i = 1; i <= 2; i++)
            if (S[i].have) {
                S[i].st = SL_WAIT;
                S[i].due_ms = now;
            }
        break;
    }
}

static void manage_scan(uint32_t now)
{
    int active = ble_gap_disc_active();
    if (scan_running && !active) {
        int n = 0, idx[FW_SCAN_MAX];
        xSemaphoreTake(scan_lock, portMAX_DELAY);
        n = fw_scan_visible(&scan_list, 3, idx, FW_SCAN_MAX);
        xSemaphoreGive(scan_lock);
        ESP_LOGW(TAG, "scan stop: %d sensor(s) in the list", n);
        scan_running = 0;
    }
    if (!scan_wanted) {
        if (active)
            ble_gap_disc_cancel(); /* logged as "scan stop" on the next tick */
        return;
    }
    if (active)
        return;
    if (ble_gap_conn_active()) {
        if (!cancel_sent) {
            cancel_sent = 1;
            ESP_LOGW(TAG, "scan waits: cancelling the %s connect first", KIND_NAME[connecting]);
            ble_gap_conn_cancel();
        }
        return;
    }
    if ((int32_t)(now - scan_retry_ms) < 0)
        return;
    uint8_t own;
    struct ble_gap_disc_params p;
    memset(&p, 0, sizeof p);
    p.itvl = SCAN_ITVL;
    p.window = SCAN_WINDOW;
    p.passive = 0;
    p.filter_duplicates = 0; /* keep the signal bars current */
    int rc = ble_hs_id_infer_auto(0, &own);
    if (rc == 0)
        rc = ble_gap_disc(own, BLE_HS_FOREVER, &p, disc_event, NULL);
    if (rc == 0) {
        scan_running = 1;
        ESP_LOGW(TAG, "scan start: active, 50 ms of every 100 ms, for 0x1818 and 0x180D");
    } else {
        scan_retry_ms = now + 1000;
        ESP_LOGW(TAG, "scan start failed (%d); retrying in 1 s", rc);
    }
}

static void manage_connects(uint32_t now)
{
    if (scan_wanted || scan_running || connecting || ble_gap_disc_active() || ble_gap_conn_active())
        return;
    int pick = 0;
    for (int k = 1; k <= 2; k++) {
        slot_t *s = &S[k];
        if (!s->have || s->st != SL_WAIT || (int32_t)(now - s->due_ms) < 0)
            continue;
        if (!pick || (s->pairing && !S[pick].pairing) || (int32_t)(s->due_ms - S[pick].due_ms) < 0)
            pick = k;
    }
    if (!pick)
        return;
    slot_t *s = &S[pick];
    ESP_LOGW(TAG, "connect %s %s (type %u) \"%s\": %s, attempt %u", KIND_NAME[pick], addr_s(&s->id),
             s->id.addr_type, s->id.name, s->pairing ? "pairing" : "saved", s->failures + 1);
    s->st = SL_CONNECTING;
    connecting = pick;
    if (hal_ble_connect((hal_sensor_kind_t)pick, s->id.addr, s->id.addr_type) != 0) {
        connecting = 0;
        retry_later(pick, now, "connect could not start", -1);
    }
}

/* ------------------------------------------------------------------ hal_ble.h, central half */
void hal_ble_scan(int on) { fw_sensors_scan(on); }

int hal_ble_connect(hal_sensor_kind_t kind, const uint8_t addr[6], uint8_t addr_type)
{
    if (kind != HAL_SENSOR_POWER && kind != HAL_SENSOR_HR)
        return -1;
    ble_addr_t a = {.type = addr_type};
    memcpy(a.val, addr, 6);
    uint8_t own;
    int rc = ble_hs_id_infer_auto(0, &own);
    if (rc == 0)
        rc = ble_gap_connect(own, &a, CONNECT_TIMEOUT_MS, &CONN_PARAMS, central_event, (void *)(intptr_t)kind);
    if (rc != 0)
        ESP_LOGW(TAG, "%s: ble_gap_connect refused (%d)", KIND_NAME[kind], rc);
    return rc == 0 ? 0 : -1;
}

void hal_ble_disconnect(hal_sensor_kind_t kind)
{
    if ((kind == HAL_SENSOR_POWER || kind == HAL_SENSOR_HR) && L[kind].conn != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(L[kind].conn, BLE_ERR_REM_USER_CONN_TERM);
}

/* ------------------------------------------------------------------ UI hooks (core task) */
void fw_sensors_scan(int on)
{
    if (on == scan_wanted)
        return;
    scan_wanted = on;
    if (on && scan_lock) {
        xSemaphoreTake(scan_lock, portMAX_DELAY);
        fw_scan_clear(&scan_list);
        xSemaphoreGive(scan_lock);
    }
}

int fw_sensors_scan_list(ui_scan_item_t *out, int max)
{
    if (!scan_lock || max <= 0)
        return 0;
    uint8_t want = 0;
    for (int k = 1; k <= 2; k++)
        if (!S[k].have)
            want |= FW_KIND_BIT(k); /* one of each: a kind already paired is not offered (spec 6, 9) */
    int idx[FW_SCAN_MAX];
    if (max > FW_SCAN_MAX)
        max = FW_SCAN_MAX;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    fw_scan_expire(&scan_list, hal_clock_monotonic_ms(), FW_SCAN_AGE_MS);
    int n = fw_scan_visible(&scan_list, want, idx, max);
    for (int i = 0; i < n; i++) {
        const fw_scan_ent_t *e = &scan_list.e[idx[i]];
        ui_scan_item_t *it = &out[i];
        memset(it, 0, sizeof *it);
        snprintf(it->name, sizeof it->name, "%s", e->name[0] ? e->name : "Sensor");
        fw_addr_id4(e->addr, it->id4);
        it->kind = fw_scan_kind(e->kinds, want);
        it->bars = fw_rssi_bars(e->rssi);
        memcpy(it->addr, e->addr, 6);
        it->addr_type = e->addr_type;
    }
    xSemaphoreGive(scan_lock);
    return n;
}

void fw_sensors_pair(const ui_scan_item_t *it)
{
    int k = it->kind;
    if (k != 1 && k != 2)
        return;
    if (S[k].have) {
        ESP_LOGW(TAG, "pair refused: a %s is already saved; forget it first", KIND_NAME[k]);
        return;
    }
    slot_t *s = &S[k];
    memset(s, 0, sizeof *s);
    s->have = 1;
    s->pairing = 1;
    s->id.kind = (uint8_t)k;
    s->id.addr_type = it->addr_type;
    memcpy(s->id.addr, it->addr, 6);
    snprintf(s->id.name, sizeof s->id.name, "%s", it->name);
    s->st = SL_WAIT;
    s->due_ms = hal_clock_monotonic_ms();
    scan_wanted = 0; /* the connect needs the radio's one GAP procedure */
    ESP_LOGW(TAG, "pair %s %s (type %u) \"%s\", %d bars: connecting", KIND_NAME[k], addr_s(&s->id), it->addr_type,
             s->id.name, it->bars);
    core_state(k);
}

void fw_sensors_forget(int kind)
{
    if (kind != 1 && kind != 2)
        return;
    slot_t *s = &S[kind];
    ESP_LOGW(TAG, "forget %s %s", KIND_NAME[kind], s->have ? addr_s(&s->id) : "(none)");
    int was_connecting = connecting == kind;
    memset(s, 0, sizeof *s);
    if (was_connecting)
        ble_gap_conn_cancel(); /* its failure event finds no target and is ignored */
    hal_ble_disconnect((hal_sensor_kind_t)kind);
    if (kind == HAL_SENSOR_POWER)
        cc_calibration_meter(0, 0, 0);
    if (cc_bonds_remove(&bonds, (uint8_t)kind))
        save_bonds("forgotten");
    core_state(kind);
}

int fw_sensors_saved(int kind, char *name, int name_cap, char *id4, int id4_cap, int *bars)
{
    if (kind != 1 && kind != 2)
        return 0;
    const slot_t *s = &S[kind];
    if (name && name_cap > 0)
        snprintf(name, (size_t)name_cap, "%s", s->have ? s->id.name : "");
    if (id4 && id4_cap > 0) {
        char t[5] = "";
        if (s->have)
            fw_addr_id4(s->id.addr, t);
        snprintf(id4, (size_t)id4_cap, "%s", t);
    }
    if (bars)
        *bars = s->have && s->rssi ? fw_rssi_bars(s->rssi) : 0;
    return s->have;
}

/* ------------------------------------------------------------------ start, tick, status */
void fw_sensors_start(void)
{
    if (!q)
        q = fw_queue_create_psram(64, sizeof(sens_ev_t)); /* host task to core task only; PSRAM if any */
    if (!scan_lock)
        scan_lock = xSemaphoreCreateMutex();
    for (int k = 0; k < 3; k++)
        L[k].conn = BLE_HS_CONN_HANDLE_NONE;
    post_simple(EV_SYNC, 0, 0);
}

void fw_sensors_tick(uint32_t now_ms)
{
    if (!q)
        return;
    if (!bonds_loaded)
        load_bonds(now_ms);
    sens_ev_t e;
    while (xQueueReceive(q, &e, 0) == pdTRUE)
        on_event(&e, now_ms);
    if (ble_hs_synced()) {
        manage_scan(now_ms);
        manage_connects(now_ms);
        /* Signal bars for the heart-rate screen (board 22), once a second. */
        if ((int32_t)(now_ms - rssi_next_ms) >= 0) {
            rssi_next_ms = now_ms + 1000;
            for (int k = 1; k <= 2; k++) {
                int8_t r = 0;
                if (S[k].st == SL_UP && L[k].conn != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_rssi(L[k].conn, &r) == 0)
                    S[k].rssi = r;
            }
        }
    }
    /* Calibration (5.11): indications on the Control Point, then opcode 0x0C. */
    if (cc_calibration_take_request()) {
        link_t *p = &L[HAL_SENSOR_POWER];
        static const uint8_t op = CC_CPS_OP_START_OFFSET_COMP;
        uint16_t cccd = p->ctrl_cccd ? p->ctrl_cccd : (uint16_t)(p->ctrl + 1);
        if (p->conn == BLE_HS_CONN_HANDLE_NONE || !p->ctrl || write_cccd(p->conn, cccd, 0x0002, NULL) != 0 ||
            ble_gattc_write_flat(p->conn, p->ctrl, &op, 1, NULL, NULL) != 0)
            cc_calibration_write_failed();
    }
}

void fw_sensors_status_log(const char *when)
{
    static const char *const CORE[] = {"off", "searching", "ok", "lost"};
    const cc_view_t *v = cc_view();
    uint32_t now = hal_clock_monotonic_ms();
    for (int k = 1; k <= 2; k++) {
        const slot_t *s = &S[k];
        unsigned cs = (unsigned)(k == 1 ? v->power_state : v->hr_state);
        const char *core = cs < 4 ? CORE[cs] : "?";
        if (!s->have) {
            ESP_LOGW(TAG, "status %s: %s none saved (core %s)", when, KIND_NAME[k], core);
            continue;
        }
        char age[24];
        if (s->have_sample)
            snprintf(age, sizeof age, "%" PRIu32 " ms ago", now - s->last_sample_ms);
        else
            snprintf(age, sizeof age, "never");
        char due[24] = "";
        if (s->st == SL_WAIT)
            snprintf(due, sizeof due, ", next try in %" PRId32 " ms", (int32_t)(s->due_ms - now));
        ESP_LOGW(TAG, "status %s: %s %s \"%s\" %s%s%s, failures %u, core %s, last sample %s, rssi %d", when,
                 KIND_NAME[k], addr_s(&s->id), s->id.name, s->pairing ? "pairing, " : "", SLOT_STATE[s->st], due,
                 s->failures, core, age, s->rssi);
    }
    ESP_LOGW(TAG, "status %s: scan %s, connect in flight %s, phone %s", when, scan_running ? "on" : "off",
             connecting ? KIND_NAME[connecting] : "none", fw_link_connected() ? "connected" : "not connected");
}
