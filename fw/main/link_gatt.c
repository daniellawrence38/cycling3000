/* link_gatt.c: the phone link on GATT (protocol 2) and the BLE peripheral half of hal_ble.h.
 *
 * Service c7a1e6b0-..., RX (write, write without response; encrypted, authenticated, bonded) and TX
 * (notify), plus Device Information (0x180A: model "CC700 Pro" and the firmware revision). LE Secure
 * Connections only (CONFIG_BT_NIMBLE_SM_LEGACY=n), MITM, bonding, DisplayOnly: the unit shows a random
 * 6-digit passkey on the Phone screen. Pairing is accepted only while the 60 s window is open (protocol
 * 2.2); a pairing that completes replaces any older phone bond, so there is still only one (Q43).
 * Received packets are queued for the core task; the core's replies go out as notifications from the
 * core task. The unit asks for an ATT MTU of 247 on connect, and for the idle connection parameters of
 * protocol 2.4 once the link has been quiet for 10 s.
 *
 * BONDS (Q43). NVS is never initialised (Q29), so NimBLE's own persistence is off. NimBLE's RAM store
 * (ble_store_config) stays the live store; its write and delete callbacks are wrapped here so that every
 * change re-takes a snapshot of the phone's records (phone_bond.c keeps only authenticated peers, never
 * a sensor's Just Works bond, Q42 point 5). The core task, which owns the file system, writes that
 * snapshot to CC700/config/phone.a|b (two slots, protocol 7) or removes both files when no phone is
 * bonded. At boot the file is loaded into the RAM store before the host starts, so the phone's keys
 * and its IRK are in place when NimBLE syncs. No key is ever logged.
 *
 * LOGS. Every line here is ESP_LOGW under the tag "link", so it reaches the eMMC log file: advertising
 * on and off, connect and disconnect with the reason, pairing start, success and failure, bond stored,
 * loaded and erased, MTU, connection parameters, and each protocol message received and sent (name,
 * req_id, status, length and packet count; never the payload). The passkey is logged when it is shown,
 * for bring-up; it is only good for the 60 s window it was drawn for.
 *
 * Shared with the sensor manager (sensors.c, central role): the NimBLE host, its store and the
 * connection limit. Sensor connections have their own GAP callbacks and never reach gap_event below. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cc700/core.h"
#include "cc700/version.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "fw.h"
#include "hal_ble.h"
#include "hal_clock.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "link_trace.h"
#include "mem_esp.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "phone_bond.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "link";

#define PAIR_WINDOW_MS 60000u
#define UNAUTH_DROP_MS 60000u /* a connection that has not encrypted with a passkey bond by then is dropped */
#define IDLE_PARAMS_MS 10000u /* quiet time before the unit asks for the idle connection parameters */

/* c7a1e6b0-4d52-4f0a-9c3e-0b5d7a2f8e11 and its RX (b1) and TX (b2) siblings, little-endian */
#define CC_UUID(b) BLE_UUID128_INIT(0x11, 0x8e, 0x2f, 0x7a, 0x5d, 0x0b, 0x3e, 0x9c, 0x0a, 0x4f, 0x52, 0x4d, b, 0xe6, 0xa1, 0xc7)
static const ble_uuid128_t SVC_UUID = CC_UUID(0xb0);
static const ble_uuid128_t RX_UUID = CC_UUID(0xb1);
static const ble_uuid128_t TX_UUID = CC_UUID(0xb2);

static QueueHandle_t rxq;
static uint16_t tx_handle;
static volatile uint16_t phone_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile int phone_subscribed, phone_encrypted;
static volatile int pairing_now;      /* a passkey was shown on this connection */
static volatile uint32_t conn_ms;     /* when the phone connected */
static uint16_t att_mtu = 23;
static volatile uint32_t window_until_ms;
static uint32_t passkey;
static uint8_t own_addr_type;
static int adv_wanted = 1;

/* NimBLE's RAM store (ble_store_config.c); declared here as the header is not on the include path. */
void ble_store_config_init(void);
int ble_store_config_read(int obj_type, const union ble_store_key *key, union ble_store_value *value);
int ble_store_config_write(int obj_type, const union ble_store_value *val);
int ble_store_config_delete(int obj_type, const union ble_store_key *key);

/* ------------------------------------------------------------------ bond persistence (Q43) */
static SemaphoreHandle_t bond_mtx;
static fw_bond_t bond_pending;        /* the phone's records as last snapshotted (bond_mtx) */
static volatile int bond_dirty;       /* bond_pending differs from what the eMMC holds */
static volatile int phone_bonds;      /* authenticated phone bonds in the RAM store */
static fw_bond_t snap_all, snap_keep; /* scratch; used only under ble_hs_lock or before the host runs */
static fw_bond_t bond_io;             /* core task's copy for the file write */
static fw_bond_slots_t bond_slots = {-1, 0};

static void addr_to_fw(const ble_addr_t *a, fw_bond_addr_t *o)
{
    o->type = a->type;
    memcpy(o->val, a->val, 6);
}

static void addr_from_fw(const fw_bond_addr_t *a, ble_addr_t *o)
{
    o->type = a->type;
    memcpy(o->val, a->val, 6);
}

static void sec_to_fw(const struct ble_store_value_sec *v, uint8_t kind, fw_bond_sec_t *s)
{
    memset(s, 0, sizeof *s);
    s->kind = kind;
    addr_to_fw(&v->peer_addr, &s->peer);
    s->key_size = v->key_size;
    s->ediv = v->ediv;
    s->rand_num = v->rand_num;
    memcpy(s->ltk, v->ltk, 16);
    memcpy(s->irk, v->irk, 16);
    memcpy(s->csrk, v->csrk, 16);
    s->ltk_present = v->ltk_present;
    s->irk_present = v->irk_present;
    s->csrk_present = v->csrk_present;
    s->sign_counter = v->sign_counter;
    s->authenticated = v->authenticated;
    s->sc = v->sc;
}

static void sec_from_fw(const fw_bond_sec_t *s, struct ble_store_value_sec *v)
{
    memset(v, 0, sizeof *v);
    addr_from_fw(&s->peer, &v->peer_addr);
    v->key_size = s->key_size;
    v->ediv = s->ediv;
    v->rand_num = s->rand_num;
    memcpy(v->ltk, s->ltk, 16);
    memcpy(v->irk, s->irk, 16);
    memcpy(v->csrk, s->csrk, 16);
    v->ltk_present = s->ltk_present;
    v->irk_present = s->irk_present;
    v->csrk_present = s->csrk_present;
    v->sign_counter = s->sign_counter;
    v->authenticated = s->authenticated;
    v->sc = s->sc;
}

/* Reads the whole RAM store and keeps the phone's part. Caller holds ble_hs_lock (store callbacks run
 * under it) or the host is not running yet. Returns 1 if the kept part changed. */
static int snapshot(void)
{
    union ble_store_key k;
    union ble_store_value v;
    memset(&snap_all, 0, sizeof snap_all);
    static const int SEC_TYPES[2] = {BLE_STORE_OBJ_TYPE_PEER_SEC, BLE_STORE_OBJ_TYPE_OUR_SEC};
    for (int t = 0; t < 2; t++) {
        for (uint8_t idx = 0; snap_all.nsec < FW_BOND_MAX_SEC; idx++) {
            memset(&k, 0, sizeof k);
            k.sec.peer_addr = *BLE_ADDR_ANY;
            k.sec.idx = idx;
            if (ble_store_config_read(SEC_TYPES[t], &k, &v) != 0)
                break;
            sec_to_fw(&v.sec, t == 0 ? FW_BOND_PEER : FW_BOND_OUR, &snap_all.sec[snap_all.nsec++]);
        }
    }
    for (uint8_t idx = 0; snap_all.ncccd < FW_BOND_MAX_CCCD; idx++) {
        memset(&k, 0, sizeof k);
        k.cccd.peer_addr = *BLE_ADDR_ANY;
        k.cccd.idx = idx;
        if (ble_store_config_read(BLE_STORE_OBJ_TYPE_CCCD, &k, &v) != 0)
            break;
        fw_bond_cccd_t *c = &snap_all.cccd[snap_all.ncccd++];
        addr_to_fw(&v.cccd.peer_addr, &c->peer);
        c->chr_val_handle = v.cccd.chr_val_handle;
        c->flags = v.cccd.flags;
        c->value_changed = v.cccd.value_changed;
    }
    memset(&k, 0, sizeof k);
    k.local_irk.addr = *BLE_ADDR_ANY;
    if (ble_store_config_read(BLE_STORE_OBJ_TYPE_LOCAL_IRK, &k, &v) == 0) {
        snap_all.has_local_irk = 1;
        addr_to_fw(&v.local_irk.addr, &snap_all.local_addr);
        memcpy(snap_all.local_irk, v.local_irk.irk, 16);
    }
    phone_bonds = fw_bond_filter(&snap_all, &snap_keep);
    memset(&snap_all, 0, sizeof snap_all);
    int changed = 0;
    xSemaphoreTake(bond_mtx, portMAX_DELAY);
    if (memcmp(&snap_keep, &bond_pending, sizeof snap_keep) != 0) {
        bond_pending = snap_keep;
        changed = 1;
    }
    xSemaphoreGive(bond_mtx);
    memset(&snap_keep, 0, sizeof snap_keep);
    return changed;
}

static volatile int bond_applying; /* bond_apply is writing: no snapshot per record */

static int store_write(int obj_type, const union ble_store_value *val)
{
    int rc = ble_store_config_write(obj_type, val);
    if (rc == 0 && !bond_applying && snapshot())
        bond_dirty = 1;
    return rc;
}

static int store_delete(int obj_type, const union ble_store_key *key)
{
    int rc = ble_store_config_delete(obj_type, key);
    if (rc == 0 && snapshot())
        bond_dirty = 1;
    return rc;
}

/* WHY THE BOND IS LOADED AT SYNC, NOT BEFORE THE HOST STARTS. ESP-IDF's NimBLE (BLE_STATIC_TO_DYNAMIC)
 * calls ble_store_config_init() from ble_hs_startup_go -> ble_hs_pvcy_set_default_irk at the first
 * host sync. That empties the RAM store and puts back the plain store_write_cb/store_delete_cb, so a
 * bond loaded before nimble_port_freertos_init was wiped and our wrappers were lost: on the unit the
 * phone paired, no "bond: stored" line ever followed, and it had to pair again after every power-off
 * (outputs/phone-1.txt, 18 November 2026). So: the file is read on the core task at start (bond_read),
 * and on_sync, which runs after that reset and after ble_hs_misc_restore_irks, puts the wrappers back
 * and writes the phone into the store (bond_apply). Peer keys go through ble_store_write_peer_sec,
 * which also adds the phone's IRK to the controller's resolving list, so its private address is
 * recognised when it reconnects. bond_apply runs on every sync, from the latest snapshot, so a host
 * reset that empties the store again is repaired the same way. */

/* Core task, before the host starts: the stored phone, read from the eMMC into bond_pending. */
static void bond_read(void)
{
    hal_fs_t *fs = cc_fs();
    if (!fs || !fw_bond_load(fs, &bond_io, &bond_slots)) {
        ESP_LOGW(TAG, "bond: no phone bond on the eMMC (config/phone.a|b absent or unreadable)");
        memset(&bond_io, 0, sizeof bond_io);
        return;
    }
    const fw_bond_addr_t *p = NULL;
    for (int i = 0; i < bond_io.nsec; i++)
        if (bond_io.sec[i].kind == FW_BOND_PEER && bond_io.sec[i].authenticated)
            p = &bond_io.sec[i].peer;
    ESP_LOGW(TAG, "bond: read from the eMMC (slot %c, seq %" PRIu32 "): %d phone, ..:%02X:%02X, %u "
                  "subscription(s)", bond_slots.newest ? 'b' : 'a', bond_slots.seq, fw_bond_phone_count(&bond_io),
             p ? p->val[1] : 0, p ? p->val[0] : 0, bond_io.ncccd);
    xSemaphoreTake(bond_mtx, portMAX_DELAY);
    bond_pending = bond_io;
    xSemaphoreGive(bond_mtx);
    bond_dirty = 0;
    memset(&bond_io, 0, sizeof bond_io);
}

/* Host task, from on_sync: the wrappers back in place, and the phone's records (the last snapshot,
   which starts as the file) written into NimBLE's store. */
static fw_bond_t bond_apply_buf;
static void bond_apply(void)
{
    ble_hs_cfg.store_write_cb = store_write;
    ble_hs_cfg.store_delete_cb = store_delete;
    xSemaphoreTake(bond_mtx, portMAX_DELAY);
    bond_apply_buf = bond_pending;
    xSemaphoreGive(bond_mtx);
    const fw_bond_t *b = &bond_apply_buf;
    int phones = fw_bond_phone_count(b), fails = 0;
    union ble_store_value v;
    bond_applying = 1;
    for (int i = 0; i < b->nsec; i++) {
        sec_from_fw(&b->sec[i], &v.sec);
        int rc = b->sec[i].kind == FW_BOND_PEER ? ble_store_write_peer_sec(&v.sec) : ble_store_write_our_sec(&v.sec);
        fails += rc != 0;
    }
    for (int i = 0; i < b->ncccd; i++) {
        memset(&v, 0, sizeof v);
        addr_from_fw(&b->cccd[i].peer, &v.cccd.peer_addr);
        v.cccd.chr_val_handle = b->cccd[i].chr_val_handle;
        v.cccd.flags = b->cccd[i].flags;
        v.cccd.value_changed = b->cccd[i].value_changed;
        fails += ble_store_write_cccd(&v.cccd) != 0;
    }
    if (b->has_local_irk) {
        memset(&v, 0, sizeof v);
        addr_from_fw(&b->local_addr, &v.local_irk.addr);
        memcpy(v.local_irk.irk, b->local_irk, 16);
        fails += ble_store_config_write(BLE_STORE_OBJ_TYPE_LOCAL_IRK, &v) != 0;
    }
    bond_applying = 0;
    memset(&bond_apply_buf, 0, sizeof bond_apply_buf);
    memset(&v, 0, sizeof v);
    /* The store now holds what bond_pending held, so there is nothing new to write back. If a record
       was refused the snapshot differs, and the next flush rewrites the file from what the store has. */
    if (snapshot() && fails == 0)
        bond_dirty = 0;
    if (phones)
        ESP_LOGW(TAG, "bond: %d phone put into the BLE store at sync%s", phones,
                 fails ? " (some records refused)" : "");
}

/* Core task: writes a changed snapshot, or removes the files once no phone is bonded. */
static void bond_flush(void)
{
    if (!bond_dirty)
        return;
    xSemaphoreTake(bond_mtx, portMAX_DELAY);
    bond_io = bond_pending;
    bond_dirty = 0;
    xSemaphoreGive(bond_mtx);
    hal_fs_t *fs = cc_fs();
    int phones = fw_bond_phone_count(&bond_io);
    if (phones) {
        if (fs && fw_bond_save(fs, &bond_io, &bond_slots) == 0)
            ESP_LOGW(TAG, "bond: stored on the eMMC (slot %c, seq %" PRIu32 ", %u subscription(s))",
                     bond_slots.newest ? 'b' : 'a', bond_slots.seq, bond_io.ncccd);
        else
            ESP_LOGW(TAG, "bond: NOT stored, the eMMC write failed: the phone is remembered only until "
                          "the next reboot");
    } else {
        if (fs && fw_bond_erase(fs, &bond_slots) == 0)
            ESP_LOGW(TAG, "bond: erased from the eMMC (no phone bonded)");
        else
            ESP_LOGW(TAG, "bond: could not remove config/phone.a|b from the eMMC");
    }
    memset(&bond_io, 0, sizeof bond_io);
}

/* Addresses of the bonded phones, from the last snapshot. */
static int phone_addrs(ble_addr_t *out, int max)
{
    int n = 0;
    xSemaphoreTake(bond_mtx, portMAX_DELAY);
    for (int i = 0; i < bond_pending.nsec && n < max; i++)
        if (bond_pending.sec[i].kind == FW_BOND_PEER && bond_pending.sec[i].authenticated)
            addr_from_fw(&bond_pending.sec[i].peer, &out[n++]);
    xSemaphoreGive(bond_mtx);
    return n;
}

/* ------------------------------------------------------------------ message log */
static fw_trace_t rx_trace, tx_trace;
static uint16_t tx_fails;
static volatile uint32_t last_rx_ms;
static volatile int idle_params_asked;

static void trace(fw_trace_t *t, const char *dir, const uint8_t *pkt, size_t n)
{
    fw_trace_msg_t m;
    int r = fw_trace_feed(t, pkt, n, &m);
    if (r == FW_TRACE_DROP) {
        ESP_LOGW(TAG, "%s: packet out of sequence (header 0x%02X, %u bytes): partial message dropped", dir,
                 n ? pkt[0] : 0, (unsigned)n);
        return;
    }
    if (r != FW_TRACE_DONE)
        return;
    const char *name = fw_trace_name(m.op, m.req);
    int kind = fw_trace_kind(m.op, m.req);
    char fails[40] = "";
    if (t == &tx_trace && tx_fails) {
        snprintf(fails, sizeof fails, ", %u packet(s) NOT sent", tx_fails);
        tx_fails = 0;
    }
    if (kind == 1)
        ESP_LOGW(TAG, "%s %s reply (0x%02X) req %u status %d, %" PRIu32 " bytes in %u packet(s)%s", dir, name,
                 m.op, m.req, m.status, m.len, m.pkts, fails);
    else
        ESP_LOGW(TAG, "%s %s (0x%02X) req %u, %" PRIu32 " bytes in %u packet(s)%s", dir, name, m.op, m.req,
                 m.len, m.pkts, fails);
}

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

/* Device Information (protocol 2.1): read-only strings, no security. */
static int dis_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
        return BLE_ATT_ERR_UNLIKELY;
    const char *s = arg;
    return os_mbuf_append(ctxt->om, s, (uint16_t)strlen(s)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
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
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID16_DECLARE(0x2A24), .access_cb = dis_access, .arg = (void *)"CC700 Pro",
             .flags = BLE_GATT_CHR_F_READ},
            {.uuid = BLE_UUID16_DECLARE(0x2A26), .access_cb = dis_access, .arg = (void *)CC_FW_STRING,
             .flags = BLE_GATT_CHR_F_READ},
            {0},
        },
    },
    {0},
};

/* ------------------------------------------------------------------ hal_ble.h, peripheral half */
int hal_ble_notify(const uint8_t *data, size_t len)
{
    int rc = -1;
    if (phone_conn != BLE_HS_CONN_HANDLE_NONE && phone_subscribed && phone_encrypted) {
        /* Out of mbufs means the controller is still sending earlier packets: wait a little and retry. */
        for (int tries = 0; tries < 50; tries++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
            if (om) {
                int r = ble_gatts_notify_custom(phone_conn, tx_handle, om);
                if (r == 0) {
                    rc = 0;
                    break;
                }
                if (r != BLE_HS_ENOMEM)
                    break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    if (rc != 0 && tx_fails < UINT16_MAX)
        tx_fails++;
    trace(&tx_trace, "tx", data, len);
    return rc;
}

uint16_t hal_ble_att_mtu(void) { return att_mtu; }

void hal_ble_pairing_window(int open_60s)
{
    if (open_60s) {
        passkey = esp_random() % 1000000u;
        window_until_ms = hal_clock_monotonic_ms() + PAIR_WINDOW_MS;
        if (!window_until_ms)
            window_until_ms = 1;
        ESP_LOGW(TAG, "pairing: window open for 60 s (passkey %06" PRIu32 " on the Phone screen)%s", passkey,
                 phone_bonds ? "; the bonded phone is replaced if a new one pairs" : "");
    } else if (window_until_ms) {
        window_until_ms = 0;
        ESP_LOGW(TAG, "pairing: window closed (cancelled on the unit)");
    }
}

uint32_t hal_ble_passkey(void) { return fw_link_pairing() ? passkey : 0; }

int fw_link_pairing(void)
{
    uint32_t until = window_until_ms;
    return until && (int32_t)(hal_clock_monotonic_ms() - until) < 0;
}

/* The phone's bond only: a sensor's record is never authenticated and never counts (Q42 point 5). */
int hal_ble_bonded(void) { return phone_bonds > 0; }

void hal_ble_erase_bond(void)
{
    ble_addr_t a[FW_BOND_MAX_SEC];
    int n = phone_addrs(a, FW_BOND_MAX_SEC);
    window_until_ms = 0;
    uint16_t c = phone_conn;
    if (c != BLE_HS_CONN_HANDLE_NONE)
        ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
    for (int i = 0; i < n; i++)
        ble_store_util_delete_peer(&a[i]);
    ESP_LOGW(TAG, "bond: forget phone on the unit: %d phone bond(s) deleted from the RAM store", n);
}

int fw_link_connected(void) { return phone_conn != BLE_HS_CONN_HANDLE_NONE && phone_encrypted; }

int fw_link_take(fw_pkt_t *p)
{
    if (xQueueReceive(rxq, p, 0) != pdTRUE)
        return 0;
    last_rx_ms = hal_clock_monotonic_ms();
    idle_params_asked = 0;
    trace(&rx_trace, "rx", p->data, p->len);
    return 1;
}

/* ------------------------------------------------------------------ GAP */
static int gap_event(struct ble_gap_event *ev, void *arg);

static const char *reason_text(int reason)
{
    switch (reason) {
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_REM_USER_CONN_TERM: return "the phone closed the link";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_TERM_LOCAL: return "the unit closed the link";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_SPVN_TMO: return "supervision timeout (out of range?)";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_AUTH_FAIL: return "authentication failure";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING: return "key missing";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_TERM_MIC: return "MIC failure";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_ESTABLISHMENT: return "failed to establish";
    default: return "see NimBLE ble_hs.h / HCI error codes";
    }
}

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
    if (rc == 0)
        ESP_LOGW(TAG, "advertising on as %s (address type %u, 1 s interval)", name, own_addr_type);
    else if (rc != BLE_HS_EALREADY)
        ESP_LOGW(TAG, "advertising failed to start: %d", rc);
}

void hal_ble_advertise(int on)
{
    adv_wanted = on;
    if (on) {
        advertise();
    } else if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
        ESP_LOGW(TAG, "advertising off (asked by the unit)");
    }
}

static int mtu_done(uint16_t conn, const struct ble_gatt_error *err, uint16_t mtu, void *arg)
{
    (void)conn; (void)arg;
    if (err && err->status != 0)
        ESP_LOGW(TAG, "MTU exchange asked by the unit failed: status %d (the phone's own request still counts)",
                 err->status);
    else
        ESP_LOGW(TAG, "MTU exchange asked by the unit done: %u", mtu);
    return 0;
}

static void phone_link_up_if_ready(void)
{
    if (phone_subscribed && phone_encrypted) {
        ESP_LOGW(TAG, "phone link ready: encrypted, bonded, subscribed to TX (MTU %u)", att_mtu);
        fw_link_on_connect(1);
    }
}

static int gap_event(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc d;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status != 0) {
            ESP_LOGW(TAG, "phone connect failed: status %d", ev->connect.status);
            advertise();
            return 0;
        }
        if (ble_gap_conn_find(ev->connect.conn_handle, &d) == 0 && d.role == BLE_GAP_ROLE_SLAVE) {
            phone_conn = ev->connect.conn_handle;
            phone_subscribed = phone_encrypted = pairing_now = 0;
            att_mtu = 23;
            conn_ms = last_rx_ms = hal_clock_monotonic_ms();
            idle_params_asked = 0;
            memset(&rx_trace, 0, sizeof rx_trace);
            memset(&tx_trace, 0, sizeof tx_trace);
            ESP_LOGW(TAG, "advertising off (a central connected)");
            ESP_LOGW(TAG, "phone connected: handle %u, peer address type %u ..:%02X:%02X, interval %u x 1.25 ms, "
                          "latency %u, timeout %u x 10 ms; %s",
                     phone_conn, d.peer_ota_addr.type, d.peer_ota_addr.val[1], d.peer_ota_addr.val[0],
                     d.conn_itvl, d.conn_latency, d.supervision_timeout,
                     d.sec_state.bonded ? "bonded" : phone_bonds ? "not yet identified" : "no phone bonded");
            /* Protocol 2.4: the unit requests 247 and accepts whatever the phone grants. */
            int rc = ble_gattc_exchange_mtu(phone_conn, mtu_done, NULL);
            if (rc != 0)
                ESP_LOGW(TAG, "MTU exchange not started: %d", rc);
            /* A bonded phone encrypts with its stored key; a new one is asked to pair (Android then shows
               its pairing dialog), which the unit accepts only inside the window. */
            rc = ble_gap_security_initiate(phone_conn);
            if (rc != 0)
                ESP_LOGW(TAG, "security request not sent: %d", rc);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        if (ev->disconnect.conn.conn_handle == phone_conn) {
            ESP_LOGW(TAG, "phone disconnected: reason 0x%03X (%s)%s", ev->disconnect.reason,
                     reason_text(ev->disconnect.reason), pairing_now ? "; pairing did not complete" : "");
            phone_conn = BLE_HS_CONN_HANDLE_NONE;
            phone_subscribed = phone_encrypted = pairing_now = 0;
            fw_link_on_connect(0);
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (phone_conn == BLE_HS_CONN_HANDLE_NONE)
            ESP_LOGW(TAG, "advertising stopped (reason %d); restarting", ev->adv_complete.reason);
        advertise();
        return 0;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if (ev->conn_update.conn_handle == phone_conn && ble_gap_conn_find(phone_conn, &d) == 0)
            ESP_LOGW(TAG, "phone connection parameters: status %d, interval %u x 1.25 ms, latency %u, timeout "
                          "%u x 10 ms", ev->conn_update.status, d.conn_itvl, d.conn_latency, d.supervision_timeout);
        return 0;
    case BLE_GAP_EVENT_MTU:
        if (ev->mtu.conn_handle == phone_conn) {
            att_mtu = ev->mtu.value;
            ESP_LOGW(TAG, "MTU %u (%u data bytes per packet)", att_mtu, att_mtu > 4 ? att_mtu - 4 : 0);
            fw_link_on_mtu(att_mtu);
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (ev->subscribe.conn_handle == phone_conn && ev->subscribe.attr_handle == tx_handle) {
            phone_subscribed = ev->subscribe.cur_notify;
            ESP_LOGW(TAG, "phone %s TX notifications%s", phone_subscribed ? "enabled" : "disabled",
                     phone_encrypted ? "" : " (link not encrypted yet)");
            phone_link_up_if_ready();
        }
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        uint16_t c = ev->passkey.conn_handle;
        if (c != phone_conn) {
            ESP_LOGW(TAG, "pairing: passkey request on a connection that is not the phone's; refused");
            ble_gap_terminate(c, BLE_ERR_AUTH_FAIL);
        } else if (ev->passkey.params.action != BLE_SM_IOACT_DISP) {
            ESP_LOGW(TAG, "pairing: refused, the phone asked for method %d (only passkey display is allowed)",
                     ev->passkey.params.action);
            ble_gap_terminate(c, BLE_ERR_AUTH_FAIL);
        } else if (!fw_link_pairing()) {
            ESP_LOGW(TAG, "pairing: refused, the pairing window is closed (Menu, Phone, PAIR opens it)");
            ble_gap_terminate(c, BLE_ERR_AUTH_FAIL);
        } else {
            struct ble_sm_io io = {.action = BLE_SM_IOACT_DISP, .passkey = passkey};
            int rc = ble_sm_inject_io(c, &io);
            pairing_now = rc == 0;
            ESP_LOGW(TAG, "pairing: started, passkey %06" PRIu32 " shown; type it on the phone (%d)", passkey, rc);
        }
        return 0;
    }
    case BLE_GAP_EVENT_PARING_COMPLETE:
        if (ev->pairing_complete.conn_handle == phone_conn) {
            if (ev->pairing_complete.status == 0)
                ESP_LOGW(TAG, "pairing: succeeded");
            else
                ESP_LOGW(TAG, "pairing: FAILED, status %d (a wrong passkey or a cancel on the phone shows here)",
                         ev->pairing_complete.status);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.conn_handle != phone_conn)
            return 0;
        if (ev->enc_change.status != 0) {
            ESP_LOGW(TAG, "encryption FAILED: status %d%s", ev->enc_change.status,
                     ev->enc_change.status == BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING
                         ? " (the phone holds a key this unit no longer has: forget the unit in the app, then "
                           "pair again from Menu, Phone, PAIR)"
                         : "");
            return 0;
        }
        if (ble_gap_conn_find(phone_conn, &d) != 0)
            return 0;
        if (!d.sec_state.encrypted || !d.sec_state.authenticated) {
            /* Just Works or a key without a passkey behind it: not the bonded phone of protocol 2.2. */
            ESP_LOGW(TAG, "encryption without a passkey (encrypted %d, authenticated %d): refused and forgotten",
                     d.sec_state.encrypted, d.sec_state.authenticated);
            ble_store_util_delete_peer(&d.peer_id_addr);
            ble_gap_terminate(phone_conn, BLE_ERR_AUTH_FAIL);
            return 0;
        }
        phone_encrypted = 1;
        if (pairing_now) {
            /* A new bond: one phone only (Q43), so any older phone's keys go now. */
            pairing_now = 0;
            window_until_ms = 0;
            ble_addr_t a[FW_BOND_MAX_SEC];
            int n = phone_addrs(a, FW_BOND_MAX_SEC), dropped = 0;
            for (int i = 0; i < n; i++)
                if (ble_addr_cmp(&a[i], &d.peer_id_addr) != 0 && ble_store_util_delete_peer(&a[i]) == 0)
                    dropped++;
            ESP_LOGW(TAG, "pairing: bonded with ..:%02X:%02X (key size %u)%s; pairing window closed",
                     d.peer_id_addr.val[1], d.peer_id_addr.val[0], d.sec_state.key_size,
                     dropped ? ", the older phone's bond was replaced" : "");
        } else {
            ESP_LOGW(TAG, "link encrypted with the stored bond (..:%02X:%02X, key size %u)",
                     d.peer_id_addr.val[1], d.peer_id_addr.val[0], d.sec_state.key_size);
        }
        phone_link_up_if_ready();
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* The phone lost its bond and pairs again: allowed only inside the window. */
        if (fw_link_pairing() && ble_gap_conn_find(ev->repeat_pairing.conn_handle, &d) == 0) {
            ESP_LOGW(TAG, "pairing: a bonded phone pairs again inside the window; its old bond is deleted");
            ble_store_util_delete_peer(&d.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        ESP_LOGW(TAG, "pairing: a bonded phone tried to pair again with the window closed; refused");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        if (ev->identity_resolved.conn_handle == phone_conn)
            ESP_LOGW(TAG, "phone identity resolved (private address matched a stored IRK)");
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ core task tick */
void fw_link_tick(uint32_t now_ms)
{
    bond_flush();
    uint32_t until = window_until_ms;
    if (until && (int32_t)(now_ms - until) >= 0) {
        window_until_ms = 0;
        ESP_LOGW(TAG, "pairing: window closed (60 s passed)");
    }
    uint16_t c = phone_conn;
    if (c == BLE_HS_CONN_HANDLE_NONE)
        return;
    if (!phone_encrypted && now_ms - conn_ms > UNAUTH_DROP_MS) {
        /* Someone connected and never encrypted with a passkey bond. While they hold the link the unit
           does not advertise, so the real phone could not reach it: drop them (Q43). */
        ESP_LOGW(TAG, "dropping a connection that did not encrypt within %u s", UNAUTH_DROP_MS / 1000u);
        conn_ms = now_ms;
        ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    if (phone_encrypted && !idle_params_asked && now_ms - last_rx_ms > IDLE_PARAMS_MS) {
        /* Protocol 2.4: 100 to 200 ms with latency 4 while idle. The phone asks for a short interval
           again when it starts a transfer. Supervision timeout 6 s > (1 + 4) x 200 ms x 2. */
        idle_params_asked = 1;
        struct ble_gap_upd_params u = {.itvl_min = 80, .itvl_max = 160, .latency = 4, .supervision_timeout = 600};
        int rc = ble_gap_update_params(c, &u);
        ESP_LOGW(TAG, "link idle for %u s: asked for 100 to 200 ms, latency 4 (%d)", IDLE_PARAMS_MS / 1000u, rc);
    }
}

/* ------------------------------------------------------------------ host start */
static void on_sync(void)
{
    bond_apply(); /* first: before anything can connect or bond (see bond_read) */
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
    bond_mtx = xSemaphoreCreateMutex();
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
    /* NimBLE's RAM store, with its write and delete wrapped so the phone's bond reaches the eMMC (Q43).
       The wrappers and the stored phone are (re)applied in on_sync, because the host's first sync
       resets the store (bond_read says why); here the file is only read. */
    ble_store_config_init();
    ble_hs_cfg.store_write_cb = store_write;
    ble_hs_cfg.store_delete_cb = store_delete;
    bond_read();
    nimble_port_freertos_init(host_task);
}
