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

int fw_nrf_build_frame(uint8_t kind, uint8_t id, const uint8_t *payload, size_t plen, uint8_t *out, size_t cap)
{
    if (!out || kind < 1 || kind > 5 || plen + 4 > FW_NRF_MAX_LEN || (plen && !payload))
        return -1;
    size_t total = plen + FW_NRF_OVERHEAD;
    if (cap < total)
        return -1;
    out[0] = FW_NRF_SYNC;
    out[1] = (uint8_t)(plen + 4);
    out[2] = FW_NRF_SYNC2;
    out[3] = FW_NRF_DIR_OUT;
    out[4] = kind;
    out[5] = id;
    for (size_t i = 0; i < plen; i++)
        out[6 + i] = payload[i];
    uint16_t crc = fw_nrf_crc16(out, total - 2);
    out[total - 2] = (uint8_t)(crc & 0xFFu);
    out[total - 1] = (uint8_t)(crc >> 8);
    return (int)total;
}

int fw_nrf_build_power_off(uint8_t out[FW_NRF_POWER_OFF_FRAME_LEN])
{
    /* SendPowerOffCmd at 0x42055bac: kind 2, id 16, payload from immediate 0x02E2 then zeros. */
    static const uint8_t payload[8] = {0xE2, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    return fw_nrf_build_frame(FW_NRF_KIND_CMD, FW_NRF_ID_SENSOR, payload, sizeof payload, out,
                              FW_NRF_POWER_OFF_FRAME_LEN);
}

int fw_nrf_build_power_on(uint8_t out[FW_NRF_CMD_FRAME_LEN])
{
    /* SendPowerOnCmd at 0x42055c00: kind 2, id 16, payload at 0x3c375e58. */
    static const uint8_t payload[8] = {0xE2, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    return fw_nrf_build_frame(FW_NRF_KIND_CMD, FW_NRF_ID_SENSOR, payload, sizeof payload, out,
                              FW_NRF_CMD_FRAME_LEN);
}

int fw_nrf_build_gnss_power(uint8_t out[FW_NRF_CMD_FRAME_LEN], uint8_t mode)
{
    /* 0x42155f84..0x42155fac: kind 2, id 16, payload at 0x3c6d73d4..0x3c6d73e4 (mode 0/1/2). */
    if (mode > FW_NRF_GNSS_RESET)
        return -1;
    const uint8_t payload[8] = {0xE2, 0x02, 0x07, 0x00, 0x00, mode, 0x00, 0x00};
    return fw_nrf_build_frame(FW_NRF_KIND_CMD, FW_NRF_ID_SENSOR, payload, sizeof payload, out,
                              FW_NRF_CMD_FRAME_LEN);
}

int fw_nrf_build_query_slave_mode(uint8_t out[FW_NRF_CMD_FRAME_LEN])
{
    /* SendQuerySlaveModeCmd at 0x42055ca8: builder called with (2, 1, ...), payload at 0x3c375e70. */
    static const uint8_t payload[8] = {0x24, 0x01, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF};
    return fw_nrf_build_frame(FW_NRF_KIND_CMD, FW_NRF_ID_SLAVE, payload, sizeof payload, out,
                              FW_NRF_CMD_FRAME_LEN);
}

int fw_nrf_power_event_code(const fw_nrf_frame_t *f)
{
    if (!f || f->id != FW_NRF_ID_SENSOR || f->plen < 6 || f->payload[0] != 0xE2 || f->payload[1] != 0x02)
        return -1;
    return f->payload[5];
}

int fw_nrf_is_power_on_event(const fw_nrf_frame_t *f)
{
    int c = fw_nrf_power_event_code(f);
    return c == FW_NRF_PWR_EV_MANUAL || c == FW_NRF_PWR_EV_MANUAL2;
}

int fw_nrf_parse_slave_mode(const fw_nrf_frame_t *f)
{
    /* The vendor reads payload[5] only when payload[1] == 1 (0x4214f030), and passes 8 bytes on. */
    if (!f || f->id != FW_NRF_ID_SLAVE || f->plen < 6 || f->payload[0] != FW_NRF_SUB_SLAVE || f->payload[1] != 0x01)
        return -1;
    return f->payload[5];
}

int fw_nrf_parse_version(const fw_nrf_frame_t *f, fw_nrf_version_t *out)
{
    if (!f || f->id != FW_NRF_ID_SLAVE || f->plen < 10 || f->payload[0] != 0x01)
        return -1;
    out->major = (uint8_t)(f->payload[9] / 10);
    out->minor = (uint8_t)(f->payload[9] % 10);
    out->patch = f->payload[8];
    return 0;
}

int fw_nrf_parse_charge(const fw_nrf_frame_t *f)
{
    if (!f || f->id != FW_NRF_ID_SENSOR || f->plen < 3 || f->payload[0] != FW_NRF_SUB_CHARGE || f->payload[1] != 0x02)
        return -1;
    return f->payload[2] == 0 ? 1 : 0;
}

/* ---------------------------------------------------------------- the boot handshake */
static int is_sensor_stream(const fw_nrf_frame_t *f)
{
    /* Any id 16 / 0xF1 sensor message: barometer, light, accelerometer, temperature. None arrived in
       the 64 s of the nRF's "off" state in capture 2; they started with its power-on event. */
    return f->id == FW_NRF_ID_SENSOR && f->plen >= 2 && f->payload[0] == FW_NRF_SUB_SENSOR;
}

static void hs_enter(fw_nrf_hs_t *hs, fw_nrf_hs_state_t s, uint32_t now_ms)
{
    hs->state = s;
    hs->state_ms = now_ms;
    hs->sent_any = 0;
    hs->ev_in_state = 0;
}

static void hs_fail(fw_nrf_hs_t *hs, fw_nrf_hs_fail_t why, uint32_t now_ms)
{
    hs->fail = why;
    hs_enter(hs, FW_NRF_HS_FAILED, now_ms);
}

void fw_nrf_hs_init(fw_nrf_hs_t *hs, uint32_t now_ms)
{
    *hs = (fw_nrf_hs_t){0};
    hs->slave_mode = -1;
    hs->init_ms = now_ms;
    hs_enter(hs, FW_NRF_HS_QUERY, now_ms);
}

void fw_nrf_hs_stop(fw_nrf_hs_t *hs)
{
    hs_fail(hs, FW_NRF_HS_FAIL_STOPPED, hs->state_ms);
}

/* Due when nothing has been sent in this state yet, or the period has passed since the last send. */
static int hs_due(const fw_nrf_hs_t *hs, uint32_t now_ms, uint32_t period)
{
    return !hs->sent_any || now_ms - hs->last_tx_ms >= period;
}

static int hs_send(fw_nrf_hs_t *hs, uint32_t now_ms, int what)
{
    hs->sent_any = 1;
    hs->last_tx_ms = now_ms;
    if (what == FW_NRF_HS_TX_QUERY)
        hs->queries++;
    else if (what == FW_NRF_HS_TX_POWER_ON)
        hs->power_ons++;
    else if (what == FW_NRF_HS_TX_GNSS_ON)
        hs->gnss_on = 1;
    return what;
}

int fw_nrf_hs_step(fw_nrf_hs_t *hs, uint32_t now_ms, const fw_nrf_frame_t *f)
{
    int ev = -1;
    if (f) {
        if (!hs->heard)
            hs->heard_ms = now_ms;
        hs->heard = 1;
        if (is_sensor_stream(f))
            hs->sensors = 1;
        ev = fw_nrf_power_event_code(f);
        if (ev == FW_NRF_PWR_EV_MANUAL || ev == FW_NRF_PWR_EV_MANUAL2)
            hs->manual = 1;
        int mode = fw_nrf_parse_slave_mode(f);
        if (mode >= 0)
            hs->slave_mode = (int16_t)mode;
        if (hs->state == FW_NRF_HS_QUERY && mode == FW_NRF_SLAVE_APP) {
            hs_enter(hs, FW_NRF_HS_POWER_ON, now_ms);
        } else if (hs->state == FW_NRF_HS_QUERY && mode == FW_NRF_SLAVE_BOOTLOADER) {
            /* The vendor starts a co-processor firmware upgrade here (0x421546bc). Ours sends
               nothing more, ever: Q38. */
            hs_fail(hs, FW_NRF_HS_FAIL_BOOTLOADER, now_ms);
        }
        if (hs->state == FW_NRF_HS_POWER_ON &&
            (ev == FW_NRF_PWR_EV_CHARGER || ev == FW_NRF_PWR_EV_MANUAL || ev == FW_NRF_PWR_EV_MANUAL2))
            hs->ev_in_state = (uint8_t)ev;
    }

    switch (hs->state) {
    case FW_NRF_HS_QUERY:
        if (!hs->heard)
            return FW_NRF_HS_TX_NONE; /* nothing is sent into a link never heard, and silence is not a no */
        if (now_ms - hs->heard_ms >= FW_NRF_HS_QUERY_MAX_MS) {
            hs_fail(hs, FW_NRF_HS_FAIL_NO_REPLY, now_ms);
            return FW_NRF_HS_TX_NONE;
        }
        if (hs->heard && hs_due(hs, now_ms, FW_NRF_HS_QUERY_PERIOD_MS))
            return hs_send(hs, now_ms, FW_NRF_HS_TX_QUERY);
        return FW_NRF_HS_TX_NONE;

    case FW_NRF_HS_POWER_ON:
        if (hs->ev_in_state) {
            hs->on = hs->ev_in_state == FW_NRF_PWR_EV_CHARGER ? FW_NRF_HS_ON_CHARGER : FW_NRF_HS_ON_MANUAL;
            hs_enter(hs, FW_NRF_HS_RUNNING, now_ms);
            break; /* to RUNNING below, in this same call */
        }
        if (hs->sensors && hs->sent_any && now_ms - hs->state_ms >= FW_NRF_HS_SENSOR_GRACE_MS) {
            hs->on = FW_NRF_HS_ON_SENSORS;
            hs_enter(hs, FW_NRF_HS_RUNNING, now_ms);
            break;
        }
        if (now_ms - hs->state_ms >= FW_NRF_HS_POWER_ON_MAX_MS) {
            hs_fail(hs, FW_NRF_HS_FAIL_NO_POWER_EVENT, now_ms);
            return FW_NRF_HS_TX_NONE;
        }
        if (hs_due(hs, now_ms, FW_NRF_HS_POWER_ON_PERIOD_MS))
            return hs_send(hs, now_ms, FW_NRF_HS_TX_POWER_ON);
        return FW_NRF_HS_TX_NONE;

    case FW_NRF_HS_FAILED:
        /* Receive-only. Positive evidence that the nRF's sensor side is on brings it to RUNNING with no
           frame sent, except after a bootloader answer or a power-off. */
        if (hs->fail == FW_NRF_HS_FAIL_NO_REPLY || hs->fail == FW_NRF_HS_FAIL_NO_POWER_EVENT) {
            if (hs->manual || hs->sensors) {
                hs->on = hs->manual ? FW_NRF_HS_ON_MANUAL : FW_NRF_HS_ON_SENSORS;
                hs_enter(hs, FW_NRF_HS_RUNNING, now_ms);
                break;
            }
        }
        return FW_NRF_HS_TX_NONE;

    case FW_NRF_HS_RUNNING:
        break;
    }

    /* RUNNING. A charger wake alone does not switch the sensor side on (the vendor goes to its
       charging screen, state 8), so GNSS power waits for a manual event or sensor frames. */
    if (hs->state == FW_NRF_HS_RUNNING && !hs->gnss_on && (hs->manual || hs->sensors))
        return hs_send(hs, now_ms, FW_NRF_HS_TX_GNSS_ON);
    return FW_NRF_HS_TX_NONE;
}

const char *fw_nrf_hs_state_name(fw_nrf_hs_state_t s)
{
    switch (s) {
    case FW_NRF_HS_QUERY: return "QUERY";
    case FW_NRF_HS_POWER_ON: return "POWER_ON";
    case FW_NRF_HS_RUNNING: return "RUNNING";
    case FW_NRF_HS_FAILED: return "FAILED";
    }
    return "?";
}

const char *fw_nrf_hs_fail_name(fw_nrf_hs_fail_t f)
{
    switch (f) {
    case FW_NRF_HS_FAIL_NONE: return "none";
    case FW_NRF_HS_FAIL_NO_REPLY: return "no slave-mode reply";
    case FW_NRF_HS_FAIL_BOOTLOADER: return "nRF in its bootloader";
    case FW_NRF_HS_FAIL_NO_POWER_EVENT: return "no power event and no sensors";
    case FW_NRF_HS_FAIL_STOPPED: return "stopped for power-off";
    }
    return "?";
}

const char *fw_nrf_hs_on_name(fw_nrf_hs_on_t o)
{
    switch (o) {
    case FW_NRF_HS_ON_NONE: return "none";
    case FW_NRF_HS_ON_MANUAL: return "manual power-on event";
    case FW_NRF_HS_ON_CHARGER: return "charger wake event (code 4)";
    case FW_NRF_HS_ON_SENSORS: return "sensor frames, no event";
    }
    return "?";
}

/* ---------------------------------------------------------------- power-off and the wake decision */
void fw_nrf_wake_init(fw_nrf_wake_t *w, uint8_t last_key0_event, uint32_t last_key0_ms)
{
    w->released = last_key0_event == FW_NRF_KEY_HOLD_END || last_key0_event == FW_NRF_KEY_CLICK;
    w->released_ms = last_key0_ms;
}

int fw_nrf_wake_feed(fw_nrf_wake_t *w, const fw_nrf_frame_t *f, uint32_t now_ms)
{
    int ev = fw_nrf_power_event_code(f);
    if (ev == FW_NRF_PWR_EV_CHARGER)
        return FW_NRF_WAKE_CHARGER;
    if (ev == FW_NRF_PWR_EV_MANUAL || ev == FW_NRF_PWR_EV_MANUAL2) {
        if (w->released && now_ms - w->released_ms > FW_NRF_WAKE_SAME_HOLD_MS)
            return FW_NRF_WAKE_EVENT;
        return FW_NRF_WAKE_EVENT_IGNORED;
    }
    fw_nrf_key_t k;
    if (fw_nrf_parse_key(f, &k) != 0 || k.index != 0)
        return FW_NRF_WAKE_NONE;
    if (!w->released) {
        /* Still the hold that asked for the power-off. HELD repeats never wake, however long the gap
           between them: only an explicit end of the hold counts. */
        if (k.event == FW_NRF_KEY_HOLD_END || k.event == FW_NRF_KEY_CLICK) {
            w->released = 1;
            w->released_ms = now_ms;
            return FW_NRF_WAKE_RELEASED;
        }
        return FW_NRF_WAKE_NONE;
    }
    if (k.event == FW_NRF_KEY_HELD || k.event == FW_NRF_KEY_CLICK)
        return FW_NRF_WAKE_KEY;
    return FW_NRF_WAKE_NONE;
}

/* ---------------------------------------------------------------- the GNSS silence watchdog */
void fw_gnss_wd_init(fw_gnss_wd_t *w) { *w = (fw_gnss_wd_t){0}; }

void fw_gnss_wd_arm(fw_gnss_wd_t *w, uint32_t now_ms)
{
    if (w->armed)
        return;
    w->armed = 1;
    w->quiet_since_ms = now_ms;
}

void fw_gnss_wd_bytes(fw_gnss_wd_t *w, uint32_t now_ms) { w->quiet_since_ms = now_ms; }

int fw_gnss_wd_tick(fw_gnss_wd_t *w, uint32_t now_ms)
{
    if (!w->armed || now_ms - w->quiet_since_ms < FW_GNSS_WD_SILENCE_MS)
        return FW_GNSS_WD_NONE;
    if (w->resets < FW_GNSS_WD_MAX_RESETS) {
        w->resets++;
        w->quiet_since_ms = now_ms; /* the next one no sooner than FW_GNSS_WD_SILENCE_MS from now */
        return FW_GNSS_WD_RESET;
    }
    if (!w->gave_up) {
        w->gave_up = 1;
        return FW_GNSS_WD_GAVE_UP;
    }
    return FW_GNSS_WD_NONE;
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
    if (f->id != FW_NRF_ID_STATUS || f->plen < 7 || f->payload[0] != FW_NRF_SUB_BATTERY || f->payload[6] > 100)
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

int fw_nrf_light_backlight_pct(int32_t lux)
{
    if (lux < 0)
        return 60; /* spec 10.1: without a light sensor, Auto is 60 % */
    /* Stock firmware table at 0x3c36eb7c (15 pairs of {pct, value}):
       (1, 0), (1, 10), (5, 30), (5, 50), (10, 600), (10, 2000), (20, 3000), (30, 6000),
       (40, 9000), (50, 50000), (60, 65000), (70, 80000), (80, 90000), (90, 100000), (100, 110000).
       Both 600 and 2000 map to 10 %, so the room-light alternation between 600 and 2000 in capture 2
       keeps a steady 10 % backlight. */
    static const struct {
        int32_t max_lux;
        uint8_t pct;
    } tbl[] = {
        {10, 1},     {50, 5},     {2000, 10},  {3000, 20},  {6000, 30},   {9000, 40},
        {50000, 50}, {65000, 60}, {80000, 70}, {90000, 80}, {100000, 90},
    };
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++)
        if (lux <= tbl[i].max_lux)
            return tbl[i].pct;
    return 100;
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
