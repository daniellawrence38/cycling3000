/* hal_nrf_esp.c: listening to the nRF co-processor, finding the baud rate, and making one capture
 * tell us as much as it can.
 *
 * WHY THIS EXISTS. The on-board sensors are not on I2C (Q36). They are messages from the nRF on
 * UART1 -- ESP port 2, GPIO 42 out, GPIO 41 in -- and so are ANT+ and, on the evidence so far, the
 * buttons. One protocol decode therefore unlocks altitude, battery, the light sensor, the IMU and the
 * deferred Di2 gear display together, which is why it is worth doing properly rather than four times.
 *
 * THE FRAME FORMAT, read out of the stock firmware's own parser at 0x42050766..0x4205088d:
 *
 *     offset 0   0xA5          sync; the parser discards bytes until it sees this
 *     offset 1   len           payload length + 4, so the whole frame is len + 4; rejected above 132
 *     offset 2   0x6F          a second constant, checked as strictly as the sync
 *     offset 3   0xF1          CORRECTED 4 November 2026: this is a plain constant, not a direction
 *                              marker. The builder writes 0xF1 and the first live capture shows the
 *                              nRF sending 0xF1 too, on every frame of both ids. The parser reads it
 *                              and does not validate it.
 *     offset 4   kind          1..5, else "Com Instr Err:%x"
 *     offset 5   id            the message id the dispatcher at 0x42051590 switches on
 *     offset 6+  payload
 *     last two   CRC-16, little-endian, over every byte before it
 *
 * The builder at 0x4205062c confirms the arithmetic from the other side: it allocates payload + 8,
 * writes 0xA5, then payload_len + 4 as the length byte, then 0x6F, then 0xF1, then the kind and the
 * id, then the payload, then the checksum. Six header bytes plus payload plus two of CRC, which is
 * why this file reads the payload length as total - 8.
 *
 * The checksum function at 0x4222137c is a bitwise CRC-16 with polynomial 0x1021 (the literal sits at
 * 0x4220e4a8), initial value 0, no reflection and no final XOR, feeding the message bits in low end
 * first and then shifting in sixteen zero bits. That augmented form is identical to the ordinary
 * CRC-16/XMODEM of the message, which is what fw_nrf_crc16() in nrf_proto.c computes.
 *
 * The dispatcher acts on ids 0, 1, 11, 16, 17, 34, 35, 40, 120..123 and 128 (the 11..40 set comes out
 * of the bitmask 0x21800041 at 0x4203eeb0). Most are forwarded to the application as an eight-byte
 * payload, so the sensor values are almost certainly among them.
 *
 * WHICH ID IS WHICH IS NOT IN THE PARSER, and it is not guessed here. Chasing it statically means
 * following five queue hops -- parser, intrusive list, event dispatcher, repackage, another queue --
 * and the meaning is only resolved at the far end. Watching the real numbers move is faster and more
 * certain, so this listener is built to make a single capture self-interpreting:
 *
 *   - it keeps per-id statistics: how often the id arrives, its period, and for each payload byte
 *     whether that byte is constant or varies and between what values;
 *   - it prints a frame when the id is new or its payload has changed, not every time, so a
 *     several-minute capture stays readable;
 *   - and it prints each id's payload read as 16- and 32-bit little-endian integers, because a
 *     pressure in pascals, a battery in millivolts or a lux value is recognisable on sight once the
 *     bytes are grouped the way the sender meant them.
 *
 * THE BAUD RATE IS NOT IN THE FIRMWARE. uart_param_config takes it as a parameter and no constant in
 * the MidComm area is a plausible rate. So this tries the standard rates in turn and lets the CRC
 * decide: a wrong rate produces byte salsa, and byte salsa that happens to begin 0xA5, carry 0x6F in
 * the third byte, declare a length under 132 and then match a 16-bit CRC is a one in millions event.
 * The first rate that yields a valid frame is the right one, and it is reported rather than assumed.
 *
 * WHAT IT DELIVERS. The barometer is decoded (id 16, kind 4, sub-tag 0xF1, sensor 0x03: nrf_proto.c)
 * and each reading is kept with its arrival time for hal_baro_read_pa(), which the core task polls.
 * That part runs for as long as the unit is on. The capture logging around it is bring-up scaffolding:
 * it runs for MAX_WINDOWS windows after boot and then goes quiet, while the decode carries on.
 *
 * RECEIVE ONLY. Nothing here ever writes to the port. A passive sniff cannot upset a co-processor
 * that is also holding the ANT+ radio and, on the evidence so far, the buttons; sending it a guessed
 * frame could.
 *
 * This runs in its own low-priority task on core 0. The core task owns the core, the link and LVGL
 * and must not be made to wait on a serial line that may be silent for thirteen seconds at a time.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hal_baro.h"
#include "hal_clock.h"
#include "hal_display.h"
#include "hal_input.h"
#include "hal_nrf.h"
#include "hal_power.h"
#include "nrf_proto.h"

static const char *TAG = "nrf";

/* Ordered by likelihood rather than by value: 115200 first because it is the default almost
 * everywhere, then the high rates a sensor hub streaming at 10 Hz would want. */
static const uint32_t CANDIDATE_BAUD[] = {115200, 921600, 460800, 230400, 1000000, 57600, 38400, 9600};
#define NCANDIDATES ((int)(sizeof CANDIDATE_BAUD / sizeof CANDIDATE_BAUD[0]))

/* The nRF heartbeat was about 13 s apart during the RAM probe, so a search window has to be
 * comfortably longer than that or a correct baud rate would be rejected for being quiet. */
#define SEARCH_MS 20000
/* Once locked, a longer window: the summary is the useful output and one every half minute is plenty. */
#define WINDOW_MS 30000
#define RX_RING 2048
#define ACC_MAX 512 /* the longest frame is 132 + 4; this leaves room to resynchronise */

/* Streams are keyed on the id AND the first payload byte, not the id alone.
 *
 * The first long capture made the reason plain. Id 16 kind 4 arrives at 36 Hz and its payload byte 0
 * ranges over 0x49..0xF1 while byte 1 stays between 0 and 10: a sub-message tag and a small index. It
 * is a multiplexed transport, not one stream, and a digest that mixes every sub-message together
 * saturates every byte to 00-ff and says nothing -- which is precisely what the first two captures
 * reported. Split on the tag and the constants inside each sub-message stand out immediately. */
#define MAX_IDS 48     /* ids times sub-tags; the first capture showed two ids and several tags */
#define TRACK_BYTES 12 /* most payloads are eight, but ids 0, 1 and 16 carry more */

typedef struct {
    uint8_t id;
    uint8_t sub; /* payload byte 0: the sub-message tag */
    uint8_t kind;
    uint8_t dir;   /* header byte 3: 0xF1 is the ESP32's own; the nRF's value is a finding */
    uint8_t len;   /* the frame's length byte */
    uint8_t plen;  /* payload bytes actually present */
    uint32_t count;
    uint32_t first_ms, last_ms;
    uint8_t lo[TRACK_BYTES], hi[TRACK_BYTES], last[TRACK_BYTES];
    uint8_t shown; /* frames printed for this id */
} id_stat_t;

static id_stat_t stats[MAX_IDS];
static int nstats;
static uint32_t locked_baud;
static uint32_t frame_count;
static uint32_t bad_crc;

/* Frames printed in full, header and CRC included, immediately after the baud rate is found.
 *
 * The first capture showed why this is needed: in idle the nRF sends only ids 0 and 16, their payload
 * bytes cover most of 0x00..0xFF, and a min-to-max digest of a fifty-second window therefore says
 * almost nothing. Sequences are what carry the structure -- a counter, a timestamp, an axis that
 * barely moves while the unit sits still -- and sequences can only be seen frame by frame. 240 frames
 * is about six seconds of this link and a few kilobytes of log, which is cheap for the first look at
 * a protocol nobody outside the vendor has written down. */
static uint32_t verbatim_left = 240;
#define VERBATIM_PER_WINDOW 60 /* about a second and a half of this link, per window */

/* The capture ends itself after this many windows. It used to run for ever, which meant the only way
 * to end a capture was to kill the monitor -- and killing a monitor that is piped to a file can lose
 * the tail of it. Six windows is three minutes, which is the still / moving / light-sensor experiment
 * with a window of slack at each end. */
#define MAX_WINDOWS 6

/* The digest is only useful per window. Accumulated over minutes every byte saturates to 00-ff and
 * says nothing, which is exactly what the first capture showed. */
static void reset_stats(void)
{
    nstats = 0;
    memset(stats, 0, sizeof stats);
}

uint32_t hal_nrf_baud(void) { return locked_baud; }
uint32_t hal_nrf_frames(void) { return frame_count; }

/* The newest barometer reading, written by the nRF task and read by the core task. */
static portMUX_TYPE baro_mux = portMUX_INITIALIZER_UNLOCKED;
static int32_t baro_pa = -1;
static int16_t baro_temp_c100;
static uint32_t baro_ms;
static uint32_t baro_count;
/* A reading older than this is not offered: the nRF sends one about every 100 ms, so two seconds of
 * silence means the link or the sensor has stopped, and a stale pressure would read as flat road. */
#define BARO_STALE_MS 2000

int32_t hal_baro_read_pa(void)
{
    int32_t pa;
    uint32_t at;
    portENTER_CRITICAL(&baro_mux);
    pa = baro_pa;
    at = baro_ms;
    portEXIT_CRITICAL(&baro_mux);
    if (pa < 0 || hal_clock_monotonic_ms() - at > BARO_STALE_MS)
        return -1;
    return pa;
}

int hal_nrf_baro_temp_c100(int16_t *out)
{
    if (hal_baro_read_pa() < 0)
        return -1;
    portENTER_CRITICAL(&baro_mux);
    *out = baro_temp_c100;
    portEXIT_CRITICAL(&baro_mux);
    return 0;
}

/* Battery (id 0 / 0x52, every ~300 ms) and ambient light (id 16 / 0xF1 / 0x04, every ~70 ms),
 * confirmed on the unit in outputs/nrf-capture-2.txt. */
static portMUX_TYPE pwr_mux = portMUX_INITIALIZER_UNLOCKED;
static int bat_pct = -1;
static uint16_t bat_mv;
static uint32_t bat_ms;
static int32_t light_val = -1;
static uint32_t light_ms;
static volatile int nrf_uart_up;
static volatile int power_on_seen;
static volatile int pending_gnss_mode = -1;
static int boot_pwr_swallow;
#define BATT_STALE_MS 5000
#define LIGHT_STALE_MS 2000

static void nrf_send_power_on(void)
{
    uint8_t frame[FW_NRF_CMD_FRAME_LEN];
    int n = fw_nrf_build_power_on(frame);
    if (n > 0 && nrf_uart_up) {
        ESP_LOGW(TAG, "sending SendPowerOnCmd (%d bytes) to nRF", n);
        uart_write_bytes(UART_NRF_PORT, frame, (size_t)n);
    }
}

void hal_nrf_gnss_power(uint8_t mode)
{
    if (!nrf_uart_up) {
        pending_gnss_mode = (int)mode;
        return;
    }
    uint8_t frame[FW_NRF_CMD_FRAME_LEN];
    int n = fw_nrf_build_gnss_power(frame, mode);
    if (n > 0) {
        ESP_LOGW(TAG, "sending GNSS power mode %u (%d bytes) to nRF", (unsigned)mode, n);
        uart_write_bytes(UART_NRF_PORT, frame, (size_t)n);
    }
}

int hal_power_battery_pct(void)
{
    int pct;
    uint32_t at;
    portENTER_CRITICAL(&pwr_mux);
    pct = bat_pct;
    at = bat_ms;
    portEXIT_CRITICAL(&pwr_mux);
    if (pct < 0 || hal_clock_monotonic_ms() - at > BATT_STALE_MS)
        return -1;
    return pct;
}

int hal_nrf_battery_mv(uint16_t *out)
{
    if (hal_power_battery_pct() < 0)
        return -1;
    portENTER_CRITICAL(&pwr_mux);
    *out = bat_mv;
    portEXIT_CRITICAL(&pwr_mux);
    return 0;
}

int hal_power_charging(void)
{
    /* No charging bit has been identified in id 0 / 0x52 (payload[7] is 0xFF on USB at both 2 % and 100 %). */
    return 0;
}

int hal_power_ambient_lux(void)
{
    int32_t v;
    uint32_t at;
    portENTER_CRITICAL(&pwr_mux);
    v = light_val;
    at = light_ms;
    portEXIT_CRITICAL(&pwr_mux);
    if (v < 0 || hal_clock_monotonic_ms() - at > LIGHT_STALE_MS)
        return -1;
    return (int)v;
}

int hal_power_off(void)
{
    hal_display_backlight(0);
    if (!nrf_uart_up) {
        ESP_LOGW(TAG, "hal_power_off: nRF UART not up; nothing sent");
        return HAL_POWER_OFF_NOT_SENT;
    }
    hal_nrf_gnss_power(FW_NRF_GNSS_OFF);
    uint8_t frame[FW_NRF_POWER_OFF_FRAME_LEN];
    int n = fw_nrf_build_power_off(frame);
    if (n <= 0)
        return HAL_POWER_OFF_NOT_SENT;
    ESP_LOGW(TAG, "sending SendPowerOffCmd (%d bytes) to nRF", n);
    /* uart_write_bytes returns the bytes queued, or -1. Anything short of the whole frame is not a
       power-off request the nRF can accept (its CRC covers every byte), so it counts as not sent. */
    if (uart_write_bytes(UART_NRF_PORT, frame, (size_t)n) != n) {
        ESP_LOGE(TAG, "hal_power_off: the frame was not queued in full");
        return HAL_POWER_OFF_NOT_SENT;
    }
    uart_wait_tx_done(UART_NRF_PORT, pdMS_TO_TICKS(200));
    /* Wait for the user to finish releasing the power button from the power-off hold, then drain any
     * queued edges. If the nRF cuts the 3.3 V rail (battery mode), execution stops here. If the 3.3 V
     * rail stays up (USB charge mode, stock ArmStateProcess State 8/9 FUN_420561ec), wait for the next
     * power-button press or nRF power-on event and reboot cleanly. */
    vTaskDelay(pdMS_TO_TICKS(1200));
    hal_input_event_t ev;
    while (hal_input_poll(&ev)) {
    }
    power_on_seen = 0;
    for (;;) {
        int wake = power_on_seen;
        while (hal_input_poll(&ev)) {
            if (ev.kind == HAL_PWR_DOWN)
                wake = 1;
        }
        if (wake) {
            ESP_LOGW(TAG, "power-on detected while off: sending SendPowerOnCmd and restarting");
            nrf_send_power_on();
            uart_wait_tx_done(UART_NRF_PORT, pdMS_TO_TICKS(100));
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void hexdump(char *out, size_t outsz, const uint8_t *p, size_t n)
{
    size_t k = 0;
    if (!outsz)
        return;
    out[0] = '\0';
    for (size_t i = 0; i < n && k + 4 < outsz; i++)
        k += (size_t)snprintf(out + k, outsz - k, "%02x ", p[i]);
    if (k)
        out[k - 1] = '\0';
}

/* The buttons. The nRF reports them as clicks (nrf_proto.h); fw_nrf_keys turns those into the down
 * and up edges the core wants, and they wait in a queue for the core task's hal_input_poll(). */
static QueueHandle_t key_queue;
static fw_nrf_keys_t key_state;

static void push_edges(const fw_nrf_edge_t *e, int n)
{
    /* Key index to button, confirmed on the unit: 0 power, 1 bottom left, 2 bottom right. */
    static const hal_input_kind_t DOWN[FW_NRF_KEYS] = {HAL_PWR_DOWN, HAL_BTN_L_DOWN, HAL_BTN_R_DOWN};
    for (int i = 0; i < n; i++) {
        hal_input_event_t ev = {0};
        ev.kind = (hal_input_kind_t)(DOWN[e[i].key] + (e[i].down ? 0 : 1));
        if (!key_queue || xQueueSend(key_queue, &ev, 0) != pdTRUE)
            ESP_LOGW(TAG, "key queue full or missing: a button edge was dropped");
    }
}

int hal_input_poll(hal_input_event_t *ev)
{
    return key_queue && xQueueReceive(key_queue, ev, 0) == pdTRUE;
}

/* Every verified frame comes through here, whether or not the capture is still logging. */
static void consume(const fw_nrf_frame_t *f)
{
    if (fw_nrf_is_power_on_event(f))
        power_on_seen = 1;
    fw_nrf_key_t key;
    if (fw_nrf_parse_key(f, &key) == 0) {
        uint32_t now_k = hal_clock_monotonic_ms();
        /* When the unit wakes from a power-button hold, the nRF continues sending FW_NRF_KEY_HELD
         * for key 0 until the user lifts their finger. Swallow a key-0 hold that started in the
         * first 2.5 s after boot so turning the unit ON does not immediately trigger a 2 s power-off. */
        if (key.index == 0) {
            if (key.event == FW_NRF_KEY_HELD && !key_state.held[0] && now_k < 2500)
                boot_pwr_swallow = 1;
            if (boot_pwr_swallow) {
                if (key.event != FW_NRF_KEY_HELD || now_k >= 6000)
                    boot_pwr_swallow = 0;
                goto after_key_feed;
            }
        }
        fw_nrf_edge_t edges[2];
        push_edges(edges, fw_nrf_keys_feed(&key_state, &key, now_k, edges));
        /* No return: the bring-up log below still names the frame. */
    }
after_key_feed:;
    fw_nrf_baro_t b;
    if (fw_nrf_parse_baro(f, &b) == 0) {
        int32_t pa = fw_nrf_baro_pa(&b);
        uint32_t now = hal_clock_monotonic_ms();
        portENTER_CRITICAL(&baro_mux);
        baro_pa = pa;
        baro_temp_c100 = b.temp_c100;
        baro_ms = now;
        portEXIT_CRITICAL(&baro_mux);
        if (baro_count++ == 0) {
            int frac = b.temp_c100 % 100;
            ESP_LOGW(TAG, "barometer: first reading %" PRId32 " Pa, %d.%02d C", pa, b.temp_c100 / 100,
                     frac < 0 ? -frac : frac);
        }
        return;
    }
    fw_nrf_battery_t bat_msg;
    if (fw_nrf_parse_battery(f, &bat_msg) == 0) {
        uint32_t now = hal_clock_monotonic_ms();
        portENTER_CRITICAL(&pwr_mux);
        bat_pct = bat_msg.percent;
        bat_mv = bat_msg.millivolts;
        bat_ms = now;
        portEXIT_CRITICAL(&pwr_mux);
    }
    fw_nrf_light_t li_msg;
    if (fw_nrf_parse_light(f, &li_msg) == 0) {
        uint32_t now = hal_clock_monotonic_ms();
        int32_t lv = li_msg.value > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)li_msg.value;
        portENTER_CRITICAL(&pwr_mux);
        light_val = lv;
        light_ms = now;
        portEXIT_CRITICAL(&pwr_mux);
    }
#if NRF_DECODE_LOG
    /* Bring-up: say in words what the vendor's handlers say these messages are, so one capture with
       the buttons pressed, the sensor covered and the cable out confirms or refutes each of them. */
    fw_nrf_key_t k;
    if (fw_nrf_parse_key(f, &k) == 0) {
        /* Every key frame, raw as well: this message has never been seen in a clean capture. */
        char hex[3 * 12 + 4];
        hexdump(hex, sizeof hex, f->payload, f->plen > 12 ? 12 : f->plen);
        ESP_LOGW(TAG, "KEY index %u event %u extra %u (kind %u) | %s", k.index, k.event, k.extra, f->kind, hex);
        return;
    }
    static uint32_t last_ms[3];
    static uint32_t last_val[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    uint32_t now = hal_clock_monotonic_ms();
    fw_nrf_battery_t bat;
    fw_nrf_light_t li;
    fw_nrf_clock_t c;
    if (fw_nrf_parse_battery(f, &bat) == 0) {
        uint32_t v = ((uint32_t)bat.percent << 16) | bat.millivolts;
        if (v != last_val[0] && now - last_ms[0] >= 1000) {
            last_val[0] = v;
            last_ms[0] = now;
            ESP_LOGW(TAG, "BATTERY? %u mV, %u %%", bat.millivolts, bat.percent);
        }
    } else if (fw_nrf_parse_light(f, &li) == 0) {
        if (li.value != last_val[1] && now - last_ms[1] >= 1000) {
            last_val[1] = li.value;
            last_ms[1] = now;
            ESP_LOGW(TAG, "LIGHT? value %" PRIu32 ", aux %u", li.value, li.aux);
        }
    } else if (fw_nrf_parse_clock(f, &c) == 0) {
        if (last_val[2] == 0xFFFFFFFFu || now - last_ms[2] >= 60000) {
            last_val[2] = 0;
            last_ms[2] = now;
            ESP_LOGW(TAG, "CLOCK %04u-%02u-%02u %02u:%02u:%02u", c.year, c.month, c.day, c.hour, c.minute,
                     c.second);
        }
    }
#endif
}

static id_stat_t *stat_for(uint8_t id, uint8_t sub)
{
    for (int i = 0; i < nstats; i++)
        if (stats[i].id == id && stats[i].sub == sub)
            return &stats[i];
    if (nstats == MAX_IDS)
        return NULL;
    id_stat_t *s = &stats[nstats++];
    memset(s, 0, sizeof *s);
    s->id = id;
    s->sub = sub;
    memset(s->lo, 0xFF, sizeof s->lo);
    return s;
}

/* Records a frame and decides whether it is worth printing.
 * A frame is printed when its id is new, or when its payload differs from the last one printed for
 * that id -- which is exactly the information a capture needs and a tenth of the lines. */
static void record(const uint8_t *f, size_t total)
{
    uint8_t id = f[5];
    size_t plen = total - 8; /* six header bytes, two of CRC */
    if (plen > TRACK_BYTES)
        plen = TRACK_BYTES;
    uint32_t now = hal_clock_monotonic_ms();

    uint8_t sub = plen ? f[6] : 0;
    id_stat_t *s = stat_for(id, sub);
    if (!s) {
        static int once;
        if (!once) {
            once = 1;
            ESP_LOGW(TAG, "more than %d id/sub-tag pairs; id %u sub 0x%02x not tracked", MAX_IDS, id,
                     sub);
        }
        return;
    }
    int changed = s->count == 0 || memcmp(s->last, f + 6, plen) != 0 || s->plen != (uint8_t)plen;
    if (!s->count)
        s->first_ms = now;
    s->last_ms = now;
    s->count++;
    s->kind = f[4];
    s->dir = f[3];
    s->len = f[1];
    s->plen = (uint8_t)plen;
    for (size_t i = 0; i < plen; i++) {
        uint8_t v = f[6 + i];
        if (v < s->lo[i]) s->lo[i] = v;
        if (v > s->hi[i]) s->hi[i] = v;
        s->last[i] = v;
    }

    if (verbatim_left) {
        verbatim_left--;
        char hex[3 * (TRACK_BYTES + 8) + 4];
        hexdump(hex, sizeof hex, f, total > TRACK_BYTES + 8 ? TRACK_BYTES + 8 : total);
        ESP_LOGI(TAG, "raw #%" PRIu32 ": %s", frame_count, hex);
        return;
    }
    /* Three of every id unconditionally, then only when something moves, and never more than a
       handful per id, so a five-minute capture does not scroll the summary off the screen. */
    if (s->shown < 3 || (changed && s->shown < 40)) {
        s->shown++;
        char hex[3 * TRACK_BYTES + 4];
        hexdump(hex, sizeof hex, f + 6, plen);
        ESP_LOGI(TAG, "#%" PRIu32 " id %3u sub 0x%02x kind %u | %s", frame_count, id, sub, f[4], hex);
    }
}

/* One line per id: how often, how fast, and which bytes move. Then the same payload read as 16- and
 * 32-bit little-endian integers, because that is the form a sensor value was written in. */
static void summarise(void)
{
    if (!nstats) {
        ESP_LOGI(TAG, "summary: no frames");
        return;
    }
    ESP_LOGW(TAG, "---- summary: %" PRIu32 " frames, %" PRIu32 " CRC rejects, %d id/sub-tag pair(s) ----",
             frame_count, bad_crc, nstats);
    for (int i = 0; i < nstats; i++) {
        id_stat_t *s = &stats[i];
        uint32_t span = s->last_ms - s->first_ms;
        uint32_t period = s->count > 1 ? span / (s->count - 1) : 0;
        char bytes[TRACK_BYTES * 12 + 4];
        size_t k = 0;
        bytes[0] = '\0';
        for (int b = 0; b < s->plen && k + 12 < sizeof bytes; b++) {
            if (s->lo[b] == s->hi[b])
                k += (size_t)snprintf(bytes + k, sizeof bytes - k, "%02x ", s->lo[b]);
            else
                k += (size_t)snprintf(bytes + k, sizeof bytes - k, "%02x-%02x ", s->lo[b], s->hi[b]);
        }
        ESP_LOGW(TAG, "  id %3u sub 0x%02x kind %u dir 0x%02x: %5" PRIu32 " frame(s), every %5" PRIu32
                      " ms | %s",
                 s->id, s->sub, s->kind, s->dir, s->count, period, bytes);

        char nums[160];
        k = 0;
        nums[0] = '\0';
        for (int b = 0; b + 1 < s->plen && k + 10 < sizeof nums; b += 2) {
            unsigned u16 = (unsigned)s->last[b] | ((unsigned)s->last[b + 1] << 8);
            k += (size_t)snprintf(nums + k, sizeof nums - k, "%u ", u16);
        }
        ESP_LOGW(TAG, "      last as u16le: %s", nums);
        if (s->plen >= 4) {
            k = 0;
            nums[0] = '\0';
            for (int b = 0; b + 3 < s->plen && k + 14 < sizeof nums; b += 4) {
                uint32_t u32 = (uint32_t)s->last[b] | ((uint32_t)s->last[b + 1] << 8) |
                               ((uint32_t)s->last[b + 2] << 16) | ((uint32_t)s->last[b + 3] << 24);
                k += (size_t)snprintf(nums + k, sizeof nums - k, "%" PRIu32 " ", u32);
            }
            ESP_LOGW(TAG, "      last as u32le: %s", nums);
        }
    }
    ESP_LOGW(TAG, "---- end of summary ----");
}

/* Tries to take one frame from the front of acc.
 * Returns the bytes consumed, 0 if more data is needed, or -1 to drop one byte and retry. */
static int take_frame(const uint8_t *acc, size_t len, int keep)
{
    fw_nrf_frame_t f;
    int used = fw_nrf_take_frame(acc, len, &f);
    if (used == FW_NRF_MORE)
        return 0;
    if (used < 0) {
        if (used == FW_NRF_BAD_CRC)
            bad_crc++;
        return -1;
    }
    frame_count++;
    consume(&f);
    if (keep)
        record(acc, (size_t)used);
    return used;
}

/* The rate the port is set to now; 0 before the first set_baud() or after one failed. */
static uint32_t port_baud;

/* Sets the port to baud, and does nothing if it is already there.
 *
 * WHY ONCE PER RATE. uart_param_config is not a cheap "set the divider". In ESP-IDF v5.5.6
 * (components/esp_driver_uart/src/uart.c, uart_param_config) it runs, among other things:
 *
 *     uart_hal_init(&(uart_context[uart_num].hal), uart_num);
 *     ...
 *     success = uart_hal_set_baudrate(&(uart_context[uart_num].hal), uart_config->baud_rate, sclk_freq);
 *     ...
 *     UART_EXIT_CRITICAL(&(uart_context[uart_num].spinlock));
 *     uart_hal_rxfifo_rst(&(uart_context[uart_num].hal));
 *     uart_hal_txfifo_rst(&(uart_context[uart_num].hal));
 *
 * So every call re-initialises the port and empties both hardware FIFOs. Bytes the driver has already
 * moved into the TX FIFO are thrown away: a command frame written just before (the power-on frame and
 * the GNSS frame at boot, a wake retry, a GNSS frame from the core task) can go out cut short, which
 * the nRF's CRC check then rejects. Received bytes in the RX FIFO are lost as well. The listener used
 * to call it at the start of every listen window, every 30 s for as long as the unit was on.
 *
 * Now it runs only when the rate actually changes, which is during the search; once the rate is
 * locked the port is never reconfigured again. Before changing rate it waits for anything
 * still being sent to finish, so a frame queued at the old rate is not truncated. */
static int set_baud(uint32_t baud)
{
    if (baud == port_baud)
        return 0;
    uart_wait_tx_done(UART_NRF_PORT, pdMS_TO_TICKS(50));
    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(UART_NRF_PORT, &cfg) != ESP_OK) {
        port_baud = 0; /* unknown state: the next call tries again */
        return -1;
    }
    port_baud = baud;
    /* Whatever is in the ring was received at the old rate and is noise at the new one. */
    uart_flush_input(UART_NRF_PORT);
    return 0;
}

/* Listens at one baud rate for ms. Returns how many valid frames arrived. */
static uint32_t listen_at(uint32_t baud, uint32_t ms, int keep)
{
    if (set_baud(baud) != 0)
        return 0;

    uint32_t before = frame_count;
    static uint8_t acc[ACC_MAX]; /* static: this task has a modest stack */
    size_t acc_len = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    uint32_t bytes = 0;
    static int wake_retries;
    static uint32_t last_wake_ms;

    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
        uint8_t chunk[128];
        int n = uart_read_bytes(UART_NRF_PORT, chunk, sizeof chunk, pdMS_TO_TICKS(200));
        uint32_t now_l = hal_clock_monotonic_ms();
        fw_nrf_edge_t lost[FW_NRF_KEYS];
        push_edges(lost, fw_nrf_keys_tick(&key_state, now_l, lost));
        if (pending_gnss_mode >= 0) {
            int gm = pending_gnss_mode;
            pending_gnss_mode = -1;
            hal_nrf_gnss_power((uint8_t)gm);
        }
        if (baro_count == 0 && wake_retries < 5 && now_l - last_wake_ms >= 1000) {
            wake_retries++;
            last_wake_ms = now_l;
            nrf_send_power_on();
            hal_nrf_gnss_power(FW_NRF_GNSS_ON);
        }
        if (n <= 0)
            continue;
        bytes += (uint32_t)n;
        for (int i = 0; i < n; i++) {
            if (acc_len == sizeof acc) {
                /* Full without a frame, so the oldest byte cannot be the start of one. */
                memmove(acc, acc + 1, acc_len - 1);
                acc_len--;
            }
            acc[acc_len++] = chunk[i];
            while (acc_len) {
                int used = take_frame(acc, acc_len, keep);
                if (used > 0) {
                    memmove(acc, acc + used, acc_len - (size_t)used);
                    acc_len -= (size_t)used;
                } else if (used < 0) {
                    memmove(acc, acc + 1, acc_len - 1);
                    acc_len--;
                } else {
                    break;
                }
            }
        }
    }
    uint32_t got = frame_count - before;
    if (keep)
        ESP_LOGI(TAG, "%" PRIu32 " baud: %" PRIu32 " byte(s), %" PRIu32 " valid frame(s)", baud, bytes, got);
    return got;
}

static void nrf_task(void *arg)
{
    (void)arg;
    if (uart_driver_install(UART_NRF_PORT, RX_RING, 256, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed");
        vTaskDelete(NULL);
        return;
    }
    /* 115200 before anything is sent: it is the first candidate, so the search's first listen_at
       finds the port already there and does not reset the FIFO under the frames sent just below. */
    set_baud(115200);
    /* TX is GPIO 42; sends SendPowerOnCmd, GNSS power on/off/reset, and SendPowerOffCmd (Q38). */
    if (uart_set_pin(UART_NRF_PORT, PIN_UART_NRF_TX, PIN_UART_NRF_RX, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed");
        vTaskDelete(NULL);
        return;
    }
    nrf_uart_up = 1;
    nrf_send_power_on();
    hal_nrf_gnss_power(FW_NRF_GNSS_ON);
    ESP_LOGI(TAG, "listening on port %d, RX GPIO %d; %d candidate baud rates, %" PRIu32 " s each",
             UART_NRF_PORT, PIN_UART_NRF_RX, NCANDIDATES, (uint32_t)(SEARCH_MS / 1000));

    for (int i = 0; i < NCANDIDATES; i++) {
        if (listen_at(CANDIDATE_BAUD[i], SEARCH_MS, 1) > 0) {
            locked_baud = CANDIDATE_BAUD[i];
            ESP_LOGW(TAG, "baud rate found: %" PRIu32 ". Staying here; summary every %" PRIu32 " s.",
                     locked_baud, (uint32_t)(WINDOW_MS / 1000));
            break;
        }
    }
    if (!locked_baud) {
        ESP_LOGW(TAG, "no valid frame at any candidate rate. Either the nRF stays silent until it is "
                      "spoken to, or the rate is not a standard one, or GPIO %d is not its TX line.",
                 PIN_UART_NRF_RX);
        vTaskDelete(NULL);
        return;
    }
    /* The search phase's frames are not part of any window's digest. */
    reset_stats();
    for (int win = 1; win <= MAX_WINDOWS; win++) {
        ESP_LOGW(TAG, "==== window %d of %d starting ====", win, MAX_WINDOWS);
        /* A fresh verbatim sample at the top of every window, not just the first. The experiment that
           identifies this data is to leave the unit untouched for one window and move it for the
           next, so each window needs its own raw sample to compare and its own min-to-max digest. */
        verbatim_left = VERBATIM_PER_WINDOW;
        listen_at(locked_baud, WINDOW_MS, 1);
        summarise();
        /* Cleared so each window's digest stands on its own rather than accumulating for ever. */
        reset_stats();
    }
    ESP_LOGW(TAG, "==== capture finished: %" PRIu32 " frames, %" PRIu32 " CRC rejects, %" PRIu32
                  " baud. Nothing more will be logged; the monitor can be closed. ====",
             frame_count, bad_crc, locked_baud);
    /* The capture is over; the sensors are not. Keep decoding, silently, for as long as the unit is on. */
    for (;;)
        listen_at(locked_baud, WINDOW_MS, 0);
}

void hal_nrf_start(void)
{
    key_queue = xQueueCreate(16, sizeof(hal_input_event_t));
    xTaskCreatePinnedToCore(nrf_task, "nrf", 4096, NULL, 2, NULL, 0);
}
