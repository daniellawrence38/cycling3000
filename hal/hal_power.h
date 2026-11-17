/* hal_power.h: battery, charging, backlight, light sensor. All Unverified. */
#ifndef HAL_POWER_H
#define HAL_POWER_H

int hal_power_battery_pct(void);
int hal_power_charging(void);

/* Asks the power controller to switch the unit off.
 *
 * On the unit (fw/main/hal_nrf_esp.c) a request that went out over a link that is known to work does
 * not return: either the power drops, or, with the rail held up by USB, it waits for the power button
 * and restarts. So when it returns, the unit is still on, and the return value says why:
 *
 *    0  the request was written; power is expected to drop (an implementation may return before it does)
 *   -1  nothing was sent: there is no link to the power controller, or the write failed
 *   -2  the request was sent, but nothing confirms the power controller received it, and the unit is
 *       still running
 *
 * The caller restores the screen for anything but 0 and lets the rider try again (main.c). */
#define HAL_POWER_OFF_SENT 0
#define HAL_POWER_OFF_NOT_SENT (-1)
#define HAL_POWER_OFF_UNCONFIRMED (-2)
int hal_power_off(void);

void hal_power_backlight(int pct);
int hal_power_ambient_lux(void); /* -1 if there is no light sensor */
#endif
