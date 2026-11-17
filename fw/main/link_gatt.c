/* link_gatt.c: the phone link on GATT (protocol 2) and the BLE peripheral half of hal_ble.h.
 *
 * Service c7a1e6b0-..., RX (write, write without response; encrypted, authenticated, bonded) and TX
 * (notify). LE Secure Connections, MITM, bonding, DisplayOnly: the unit shows a random 6-digit passkey.
 * Pairing is accepted only while the 60 s window is open and no bond exists (protocol 2.2). Received
 * packets are queued for the core task; the core's replies go out as notifications from the core task.
 *
 * Bonds are kept in RAM only for now (CONFIG_BT_NIMBLE_NVS_PERSIST=n, Q29): the stock firmware's NVS
 * partition is not touched, so a reboot forgets the phone until bonds move to the separate storage. */
#include <stdio.h>
#include <string.h>

#include "esp_err.h"

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "fw.h"
#include "hal_ble.h"
#include "hal_clock.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "mem_esp.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "link";

/* c7a1e6b0-4d52-4f0a-9c3e-0b5d7a2f8e11 and its RX (b1) and TX (b2) siblings, little-endian */
#define CC_UUID(b) BLE_UUID128_INIT(0x11, 0x8e, 0x2f, 0x7a, 0x5d, 0x0b, 0x3e, 0x9c, 0x0a, 0x4f, 0x52, 0x4d, b, 0xe6, 0xa1, 0xc7)
static const ble_uuid128_t SVC_UUID = CC_UUID(0xb0);
static const ble_uuid128_t RX_UUID = CC_UUID(0xb1);
static const ble_uuid128_t TX_UUID = CC_UUID(0xb2);

static QueueHandle_t rxq;
static uint16_t tx_handle;
static uint16_t phone_conn = BLE_HS_CONN_HANDLE_NONE;
static int phone_subscribed, phone_encrypted;
static uint16_t att_mtu = 23;
static uint32_t window_until_ms;
static uint32_t passkey;
static uint8_t own_addr_type;
static int adv_wanted = 1;

void ble_store_config_init(void);

/* ------------------------------------------------------------------ GATT */
static int rx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || conn != phone_conn)
        return BLE_ATT_ERR_UNLIKELY;
    fw_pkt_t p;
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, p.data, sizeof p.data, &n) != 0)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    p.len = n;
    if (xQueueSend(rxq, &p, 0) != pdTRUE)
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    return 0;
}

static int tx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &RX_UUID.u,
                .access_cb = rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC |
                         BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &TX_UUID.u,
                .access_cb = tx_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_handle,
            },
            {0},
        },
    },
    {0},
};

/* ------------------------------------------------------------------ hal_ble.h, peripheral half */
int hal_ble_notify(const uint8_t *data, size_t len)
{
    if (phone_conn == BLE_HS_CONN_HANDLE_NONE || !phone_subscribed || !phone_encrypted)
        return -1;
    /* Out of mbufs means the controller is still sending earlier packets: wait a little and retry. */
    for (int tries = 0; tries < 50; tries++) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
        if (om) {
            int rc = ble_gatts_notify_custom(phone_conn, tx_handle, om);
            if (rc == 0)
                return 0;
            if (rc != BLE_HS_ENOMEM)
                return -1;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return -1;
}

uint16_t hal_ble_att_mtu(void) { return att_mtu; }

void hal_ble_pairing_window(int open_60s)
{
    window_until_ms = open_60s ? hal_clock_monotonic_ms() + 60000u : 0;
    if (open_60s)
        passkey = esp_random() % 1000000u;
}

uint32_t hal_ble_passkey(void) { return fw_link_pairing() ? passkey : 0; }

int fw_link_pairing(void) { return window_until_ms && (int32_t)(hal_clock_monotonic_ms() - window_until_ms) < 0; }

int hal_ble_bonded(void)
{
    int n = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
    return n > 0;
}

void hal_ble_erase_bond(void)
{
    if (phone_conn != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(phone_conn, BLE_ERR_REM_USER_CONN_TERM);
    ble_store_clear();
}

int fw_link_connected(void) { return phone_conn != BLE_HS_CONN_HANDLE_NONE && phone_encrypted; }

int fw_link_take(fw_pkt_t *p) { return xQueueReceive(rxq, p, 0) == pdTRUE; }

/* ------------------------------------------------------------------ GAP */
static int gap_event(struct ble_gap_event *ev, void *arg);

static void advertise(void)
{
    if (!adv_wanted || phone_conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active())
        return;
    /* Name CC700- plus the last four hex digits of the address; the service UUID in the scan response. */
    uint8_t a[6] = {0};
    ble_hs_id_copy_addr(own_addr_type, a, NULL);
    char name[16];
    snprintf(name, sizeof name, "CC700-%02X%02X", a[1], a[0]);
    ble_svc_gap_device_name_set(name);

    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)name;
    f.name_len = (uint8_t)strlen(name);
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);

    struct ble_hs_adv_fields r;
    memset(&r, 0, sizeof r);
    r.uuids128 = &SVC_UUID;
    r.num_uuids128 = 1;
    r.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&r);

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(1000);
    p.itvl_max = BLE_GAP_ADV_ITVL_MS(1000);
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY)
        ESP_LOGW(TAG, "advertising failed: %d", rc);
}

void hal_ble_advertise(int on)
{
    adv_wanted = on;
    if (on)
        advertise();
    else if (ble_gap_adv_active())
        ble_gap_adv_stop();
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc d;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            advertise();
            return 0;
        }
        if (ble_gap_conn_find(ev->connect.conn_handle, &d) == 0 && d.role == BLE_GAP_ROLE_SLAVE) {
            phone_conn = ev->connect.conn_handle;
            phone_subscribed = phone_encrypted = 0;
            att_mtu = 23;
            /* Bonded phones encrypt themselves; asking here starts pairing for a new one. */
            ble_gap_security_initiate(phone_conn);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (ev->disconnect.conn.conn_handle == phone_conn) {
            phone_conn = BLE_HS_CONN_HANDLE_NONE;
            phone_subscribed = phone_encrypted = 0;
            fw_link_on_connect(0);
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    case BLE_GAP_EVENT_MTU:
        if (ev->mtu.conn_handle == phone_conn) {
            att_mtu = ev->mtu.value;
            fw_link_on_mtu(att_mtu);
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.conn_handle == phone_conn && ev->subscribe.attr_handle == tx_handle) {
            phone_subscribed = ev->subscribe.cur_notify;
            if (phone_subscribed && phone_encrypted)
                fw_link_on_connect(1);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.conn_handle == phone_conn && ev->enc_change.status == 0 &&
            ble_gap_conn_find(phone_conn, &d) == 0 && d.sec_state.encrypted && d.sec_state.authenticated) {
            phone_encrypted = 1;
            if (phone_subscribed)
                fw_link_on_connect(1);
        }
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        /* Only during the window and only without a bond (one bond only). */
        if (ev->passkey.params.action == BLE_SM_IOACT_DISP && fw_link_pairing() && !hal_ble_bonded()) {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = passkey};
            ble_sm_inject_io(ev->passkey.conn_handle, &io);
        } else {
            ble_gap_terminate(ev->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
        }
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The phone lost its bond: allow a fresh pairing only inside the window. */
        if (fw_link_pairing() && ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ host start */
static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &own_addr_type);
    advertise();
    fw_sensors_start();
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset %d", reason); }

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void fw_link_start(void)
{
    /* 32 x 250 bytes of packets: item storage in PSRAM (mem_esp.c), internal if there is none. Only
       the NimBLE host task (rx_access) and the core task (fw_link_take) touch it. */
    rxq = fw_queue_create_psram(32, sizeof(fw_pkt_t));
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(SERVICES));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(SERVICES));
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
}
