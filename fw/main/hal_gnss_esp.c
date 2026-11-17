/* hal_gnss_esp.c: the GNSS receiver on UART0 (GPIO 1 TX, GPIO 0 RX, 115200 baud).
 *
 * PROVENANCE. Read out of the stock firmware's AppDevInit (0x4200a5dc), FUN_421566c0, MidCommInit
 * (0x420452a4), GpsPthreadUartDataProcess (0x42155fd4) and FUN_421b75f0 (docs/STOCK-FIRMWARE-FINDINGS.md):
 *   - UART0 (ESP port 0), TX GPIO 1, RX GPIO 0, 8N1, no flow control, 115200 baud (0x1c200 at literal
 *     0x42000b8c, shared with the nRF UART init).
 *   - Receiver: Airoha AM3352 / AG3352 family (GpsPthreadFullColdBoot_AM3352 at 0x4215672c), speaking
 *     NMEA 0183 ($GNRMC / $GNGGA / $GPGGA / ...) plus $PAIR extensions.
 *   - Power and reset are behind the nRF co-processor on kind 2, id 16, sub-tag 0xE2 0x02, device 0x07
 *     (0x42155f84..0x42155fac: payload[5] = 0 off, 1 on, 2 reset).
 *
 * RECEIVE ONLY. This driver installs UART0 on RX GPIO 0 with TX left unconnected (UART_PIN_NO_CHANGE)
 * and never transmits, neither on UART0 nor to the nRF (Q38 authorises SendPowerOffCmd only). Whether
 * the nRF powers the GNSS on automatically when it wakes its sensors or waits for the 0xE2 0x02 0x07
 * power-on frame is answered by watching for NMEA sentences here. Unverified on hardware. */
#include <inttypes.h>

#include "board_pins.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "fw.h"
#include "hal_gnss.h"
#include "nmea.h"

static const char *TAG = "gnss";

static int gnss_up;
static fw_nmea_t nmea;
#if GNSS_LOG
static uint32_t rx_bytes;
static int logged_first_fix;
#endif

int hal_gnss_init(void)
{
    if (gnss_up)
        return 0;
    fw_nmea_init(&nmea);
    if (uart_driver_install(UART_GNSS_PORT, 2048, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart0 driver install failed");
        return -1;
    }
    uart_config_t cfg = {
        .baud_rate = UART_GNSS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_param_config(UART_GNSS_PORT, &cfg) != ESP_OK ||
        uart_set_pin(UART_GNSS_PORT, UART_PIN_NO_CHANGE, PIN_UART0_RX, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "uart0 config on RX GPIO %d failed", PIN_UART0_RX);
        return -1;
    }
    gnss_up = 1;
    ESP_LOGI(TAG, "listening on UART%d RX GPIO %d at %d baud (NMEA 0183, receive-only)", UART_GNSS_PORT,
             PIN_UART0_RX, UART_GNSS_BAUD);
    return 0;
}

int hal_gnss_poll(hal_gnss_fix_t *fix)
{
    if (!gnss_up)
        return 0;
    uint8_t buf[128];
    int got_fix = 0;
    hal_gnss_fix_t latest = {0};
    for (;;) {
        int n = uart_read_bytes(UART_GNSS_PORT, buf, sizeof buf, 0);
        if (n <= 0)
            break;
#if GNSS_LOG
        if (rx_bytes == 0)
            ESP_LOGW(TAG, "first bytes received on UART0 RX GPIO %d", PIN_UART0_RX);
        rx_bytes += (uint32_t)n;
#endif
        for (int i = 0; i < n; i++) {
            hal_gnss_fix_t cand;
            if (fw_nmea_feed_byte(&nmea, (char)buf[i], &cand) == 1) {
                latest = cand;
                got_fix = 1;
#if GNSS_LOG
                if (cand.valid && !logged_first_fix) {
                    logged_first_fix = 1;
                    ESP_LOGW(TAG, "first GNSS fix: lat %" PRId32 " e-7, lon %" PRId32 " e-7, sats %u, utc %" PRIu32,
                             cand.lat_e7, cand.lon_e7, cand.sats, cand.utc_seconds);
                }
#endif
            }
        }
    }
    if (got_fix)
        *fix = latest;
    return got_fix;
}
