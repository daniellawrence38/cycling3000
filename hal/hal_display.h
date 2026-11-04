/* hal_display.h: LVGL flush callback and geometry. ST7789 per the firmware strings; the bus and pins
 * are Unverified. */
#ifndef CC700_HAL_DISPLAY_H
/* Not HAL_DISPLAY_H: that is the panel height below, and a guard of the same name is redefined on the
   first include, which -Werror rejects (the guard still worked, but only by accident). */
#define CC700_HAL_DISPLAY_H
#include <stdint.h>

#define HAL_DISPLAY_W 240
#define HAL_DISPLAY_H 320

/* Copies a rectangle of RGB565 pixels to the panel and returns when the buffer may be reused. */
void hal_display_flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const uint16_t *px);
#endif
