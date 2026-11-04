/* hal_nrf_esp.c: listening to the nRF co-processor, and finding the baud rate while we are at it.
 *
 * WHY THIS EXISTS. The on-board sensors are not on I2C (Q36). They are messages from the nRF on
 * UART1 -- ESP port 2, GPIO 42 out, GPIO 41 in -- and so are ANT+ and, on the evidence so far, the
 * buttons. One protocol decode therefore unlocks altitude, battery, the light sensor, the IMU and the
 * deferred Di2 gear display together, which is why it is worth doing properly rather than four times.
 *
 * THE FRAME FORMAT, read out of the stock firmware's own parser at 0x42050766..0x4205088d:
 *
 *     offset 0   0xA5          sync; the parser discards bytes until it sees this
 *     offset 1   len           total frame length is len + 4; rejected above 132
 *     offset 2   0x6F          a second constant, checked as strictly as the sync
 *     offset 3   ?             unknown; carried through untouched
 *     offset 4   kind          1..5, else "Com Instr Err:%x"
 *     offset 5   id            the message id the dispatcher at 0x42051590 switches on
 *     offset 6+  payload
 *     last two   CRC-16, little-endian, over every byte before it
 *
 * The checksum function at 0x4222137c is a bitwise CRC-16 with polynomial 0x1021 (the literal sits at
 * 0x4220e4a8), initial value 0, no reflection and no final XOR, feeding the message bits in low end
 * first and then shifting in sixteen zero bits. That augmented form is identical to the ordinary
 * CRC-16/XMODEM of the message, which is what crc16() below computes.
 *
 * The dispatcher acts on ids 0, 1, 11, 16, 17, 34, 35, 40, 120..123 and 128 (the 11..40 set comes out
 * of the bitmask 0x21800041 at 0x4203eeb0). Most are forwarded to the application as an eight-byte
 * payload, so the sensor values are almost certainly among them -- but which id is which is exactly
 * what this listener is for. Guessing would be START-HERE rule 3 all over again.
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
#include "hal_nrf.h"

static const char *TAG = "nrf";

/* Ordered by likelihood rather than by value: 115200 first because it is the default almost
 * everywhere, then the high rates a sensor hub streaming at 10 Hz would want. */
static const uint32_t CANDIDATE_BAUD[] = {115200, 921600, 460800, 230400, 1000000, 57600, 38400, 9600};
#define NCANDIDATES ((int)(sizeof CANDIDATE_BAUD / sizeof CANDIDATE_BAUD[0]))

/* The nRF heartbeat was about 13 s apart during the RAM probe, so a window has to be comfortably
 * longer than that or a correct baud rate would be rejected for being quiet. */
#define LISTEN_MS 20000
#define RX_RING 2048
#define ACC_MAX 512 /* the longest frame is 132 + 4; this leaves room to resynchronise */

static uint32_t locked_baud;
static uint32_t frame_count;

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

/* Tries to take one frame from the front of acc.
 * Returns the bytes consumed, 0 if more data is needed, or -1 to drop one byte and retry. */
static int take_frame(const uint8_t *acc, size_t len, int log_it)
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
    if (crc16(acc, total - 2) != want)
        return -1;
    frame_count++;
    if (log_it) {
        char hex[3 * 64];
        size_t plen = total - 8; /* six header bytes, two of CRC */
        hexdump(hex, sizeof hex, acc + 6, plen > 48 ? 48 : plen);
        ESP_LOGI(TAG, "frame #%" PRIu32 ": id %3u  kind %u  hdr3 0x%02x  len %u  payload[%u] %s",
                 frame_count, acc[5], acc[4], acc[3], acc[1], (unsigned)plen, hex);
    }
    return (int)total;
}

/* Listens at one baud rate for LISTEN_MS. Returns how many valid frames arrived. */
static uint32_t listen_at(uint32_t baud, int log_frames)
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
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(LISTEN_MS);
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
                int used = take_frame(acc, acc_len, log_frames);
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
    ESP_LOGI(TAG, "listening on port %d, RX GPIO %d; %d candidate baud rates, %d s each",
             UART_NRF_PORT, PIN_UART_NRF_RX, NCANDIDATES, LISTEN_MS / 1000);

    for (int i = 0; i < NCANDIDATES; i++) {
        if (listen_at(CANDIDATE_BAUD[i], 1) > 0) {
            locked_baud = CANDIDATE_BAUD[i];
            ESP_LOGW(TAG, "baud rate found: %" PRIu32 ". Staying here and logging every frame.",
                     locked_baud);
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
    /* Locked: keep listening, so a long capture shows which ids repeat and how often. */
    for (;;)
        listen_at(locked_baud, 1);
}

void hal_nrf_start(void)
{
    xTaskCreatePinnedToCore(nrf_task, "nrf", 4096, NULL, 2, NULL, 0);
}
