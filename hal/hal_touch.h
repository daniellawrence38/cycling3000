/* hal_touch.h: the capacitive touch panel.
 *
 * One controller, one finger. The CC700 Pro's stock firmware reads a single point and reports it to
 * LVGL as a pointer device, and nothing in the UI needs more, so this interface is deliberately
 * narrower than the controller's multi-touch capability.
 *
 * The chip is a Hynitron CST3240 at I2C address 0x5A; see fw/main/board_pins.h for the provenance.
 * hal_i2c_init() must succeed first. Unverified on hardware until a real press is decoded. */
#ifndef CC700_HAL_TOUCH_H
#define CC700_HAL_TOUCH_H
#include <stdint.h>

/* Registers the controller on the I2C bus and reads its version register. Returns 0 if the panel
 * answered, -1 otherwise. A failure is not fatal: the buttons remain. */
int hal_touch_init(void);

/* 1 if a finger is down, with *x and *y set in display coordinates; 0 if nothing is touching;
 * -1 if the bus did not answer. On 0 and -1, *x and *y are left at the last known position, which is
 * what a pointer device must report on release so a tap lands where the finger was. */
int hal_touch_read(int16_t *x, int16_t *y);

/* Non-zero once hal_touch_init has found the panel. */
int hal_touch_present(void);

/* The four bytes of the controller's version register, as the factory screen prints them
 * (this unit's stock firmware showed 0x1004). Zero if unread. */
uint32_t hal_touch_version(void);
#endif
