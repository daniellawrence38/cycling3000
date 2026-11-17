/* hal_esp.c: the HAL on the ESP32-S3.
 *
 *   - clock: esp_timer here;
 *   - display: ST7789 i80 driver in hal_display_esp.c;
 *   - touch: CST3240 I2C driver in hal_touch_esp.c;
 *   - buttons, barometer, battery, ambient light, power-off: nRF UART1 link in hal_nrf_esp.c;
 *   - GNSS: UART0 NMEA 0183 listener in hal_gnss_esp.c;
 *   - backlight: delegates to hal_display_backlight here. */
#include "esp_timer.h"
#include "hal_clock.h"
#include "hal_display.h"
#include "hal_power.h"

static uint32_t utc_base_s, utc_base_ms;

uint32_t hal_clock_monotonic_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

uint32_t hal_clock_utc_seconds(void)
{
    if (!utc_base_s)
        return 0;
    return utc_base_s + (hal_clock_monotonic_ms() - utc_base_ms) / 1000u;
}

void hal_clock_set_utc(uint32_t unix_seconds)
{
    utc_base_s = unix_seconds;
    utc_base_ms = hal_clock_monotonic_ms();
}

void hal_power_backlight(int pct)
{
    hal_display_backlight(pct);
}
