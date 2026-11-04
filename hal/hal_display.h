/* hal_display.h: panel bring-up, the LVGL flush and the geometry.
 *
 * The panel is an ST7789 on an 8-bit Intel i80 (8080) parallel bus, driven by the ESP32-S3's LCD
 * peripheral with an LEDC PWM backlight. That is read from the stock firmware's own initialisation
 * code, not measured: see fw/main/board_pins.h for the pins and their provenance. Unverified on
 * hardware until the driver lights the panel. */
#ifndef CC700_HAL_DISPLAY_H
/* Not HAL_DISPLAY_H: that is the panel height below, and a guard of the same name is redefined on the
   first include, which -Werror rejects (the guard still worked, but only by accident). */
#define CC700_HAL_DISPLAY_H
#include <stdint.h>

#define HAL_DISPLAY_W 240
#define HAL_DISPLAY_H 320

/* Brings up the backlight, the i80 bus and the panel. Returns 0 on success, -1 on any failure, and
 * must be called before hal_display_flush, which does nothing until it succeeds. */
int hal_display_init(void);

/* Backlight brightness, 0 to 100 per cent. hal_display_init leaves it at 0 until the panel is up,
 * then takes it to 100, so a cold start shows no noise and no false "dark screen". */
void hal_display_backlight(int pct);

/* Copies a rectangle of RGB565 pixels to the panel and returns when the buffer may be reused. */
void hal_display_flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const uint16_t *px);
#endif
