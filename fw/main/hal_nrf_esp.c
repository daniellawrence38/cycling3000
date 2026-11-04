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
 * CRC-16/XMODEM of the message, which is what crc16() below computes.
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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal_clock.h"
#include "hal_nrf.h"

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

/* CRC-16/XMODEM: poly 0x1021, init 0, no reflection, no final XOR. */
static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
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
    if (len < 8)
        return 0; /* the stock parser likewise refuses to look at fewer than eight bytes */
    if (acc[0] != 0xA5 || acc[2] != 0x6F)
        return -1;
    if (acc[1] > 132)
        return -1;
    size_t total = (size_t)acc[1] + 4;
    if (total < 8)
        return -1;
    if (len < total)
        return 0;
    uint16_t want = (uint16_t)(acc[total - 2] | ((uint16_t)acc[total - 1] << 8));
    if (crc16(acc, total - 2) != want) {
        bad_crc++;
        return -1;
    }
    frame_count++;
    if (keep)
        record(acc, total);
    return (int)total;
}

/* Listens at one baud rate for ms. Returns how many valid frames arrived. */
static uint32_t listen_at(uint32_t baud, uint32_t ms, int keep)
{
    uart_config_t cfg = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(UART_NRF_PORT, &cfg) != ESP_OK)
        return 0;
    uart_flush_input(UART_NRF_PORT);

    uint32_t before = frame_count;
    static uint8_t acc[ACC_MAX]; /* static: this task has a modest stack */
    size_t acc_len = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    uint32_t bytes = 0;

    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
        uint8_t chunk[128];
        int n = uart_read_bytes(UART_NRF_PORT, chunk, sizeof chunk, pdMS_TO_TICKS(200));
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
    ESP_LOGI(TAG, "%" PRIu32 " baud: %" PRIu32 " byte(s), %" PRIu32 " valid frame(s)", baud, bytes, got);
    return got;
}

static void nrf_task(void *arg)
{
    (void)arg;
    if (uart_driver_install(UART_NRF_PORT, RX_RING, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart driver install failed");
        vTaskDelete(NULL);
        return;
    }
    uart_config_t boot = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NRF_PORT, &boot);
    /* TX is named because uart_set_pin wants a number, but nothing here writes to the port. */
    if (uart_set_pin(UART_NRF_PORT, PIN_UART_NRF_TX, PIN_UART_NRF_RX, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed");
        vTaskDelete(NULL);
        return;
    }
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
    vTaskDelete(NULL);
}

void hal_nrf_start(void)
{
    xTaskCreatePinnedToCore(nrf_task, "nrf", 4096, NULL, 2, NULL, 0);
}
