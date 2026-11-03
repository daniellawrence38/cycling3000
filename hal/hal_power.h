/* hal_power.h: battery, charging, backlight, light sensor. All Unverified. */
#ifndef HAL_POWER_H
#define HAL_POWER_H

int hal_power_battery_pct(void);
int hal_power_charging(void);
void hal_power_off(void);
void hal_power_backlight(int pct);
int hal_power_ambient_lux(void); /* -1 if there is no light sensor */
#endif
