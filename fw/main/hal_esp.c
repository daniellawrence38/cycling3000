/* hal_esp.c: the HAL on the ESP32-S3. Only the clock is real; everything that depends on unknown wiring
 * (buttons, touch, GNSS, barometer, battery, light sensor) is a stub that returns
 * "not implemented" and logs once (brief 4), until bring-up identifies the hardware (BRINGUP.md step 3). */
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_baro.h"
#include "hal_clock.h"
#include "hal_gnss.h"
#include "hal_input.h"
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

/* input */
int hal_input_poll(hal_input_event_t *ev)
{
    (void)ev;
    NOT_IMPLEMENTED();
    return 0;
}

/* GNSS */
int hal_gnss_poll(hal_gnss_fix_t *fix)
{
    (void)fix;
    NOT_IMPLEMENTED();
    return 0;
}

/* barometer */
int32_t hal_baro_read_pa(void)
{
    NOT_IMPLEMENTED();
    return -1;
}

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
