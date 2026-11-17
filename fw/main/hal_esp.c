/* hal_esp.c: the HAL on the ESP32-S3. The clock is real here; the display, touch and barometer are real
 * in their own files, and the buttons come from the nRF link; everything else that depends on unknown wiring
 * (GNSS, battery, light sensor, power-off) is a stub that returns
 * "not implemented" and logs once (brief 4), until bring-up identifies the hardware (BRINGUP.md step 3). */
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_clock.h"
#include "hal_gnss.h"
#include "hal_power.h"

static const char *TAG = "hal";
#define NOT_IMPLEMENTED()                                       \
    do {                                                        \
        static int once;                                        \
        if (!once) {                                            \
            once = 1;                                           \
            ESP_LOGW(TAG, "%s: not implemented", __func__);     \
        }                                                       \
    } while (0)

/* clock */
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

/* display: the real ST7789 i80 driver is in hal_display_esp.c */

/* input: the buttons are in hal_nrf_esp.c, decoded from the nRF link; touch goes straight to LVGL */

/* GNSS */
int hal_gnss_poll(hal_gnss_fix_t *fix)
{
    (void)fix;
    NOT_IMPLEMENTED();
    return 0;
}

/* barometer: the real one is in hal_nrf_esp.c, decoded from the nRF link */

/* power */
int hal_power_battery_pct(void)
{
    NOT_IMPLEMENTED();
    return -1;
}

int hal_power_charging(void)
{
    NOT_IMPLEMENTED();
    return 0;
}

void hal_power_off(void) { NOT_IMPLEMENTED(); }

void hal_power_backlight(int pct)
{
    (void)pct;
    NOT_IMPLEMENTED();
}

int hal_power_ambient_lux(void)
{
    NOT_IMPLEMENTED();
    return -1;
}
