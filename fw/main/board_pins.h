/* board_pins.h: the CC700 Pro's pin assignments.
 *
 * PROVENANCE. Every number here was read out of the stock firmware's own initialisation code in the
 * flash backup, not guessed and not measured. The method is recorded in docs/BRINGUP-LOG.md: the
 * vendor built with ESP_ERROR_CHECK, which stringifies its argument, so the image contains the text
 * "esp_lcd_new_i80_bus(&bus_config, &i80_bus)" and friends; scanning IROM for the l32r instruction
 * that loads each string's address locates the exact call site, and the struct fields are plain
 * movi/s32i pairs that can be read straight off.
 *
 * The decode is corroborated by the struct layout: `bus_width` lands at exactly +12 + 16*4 in
 * esp_lcd_i80_bus_config_t, matching ESP-IDF v5.5.6's header field for field, so the offsets cannot
 * be coincidental.
 *
 * STATUS: Unverified on hardware (START-HERE rule 3). Reading the vendor's code is much stronger
 * than a guess, but no pin here has been measured on the board and this driver has not yet lit the
 * panel. Nothing in this file may be treated as confirmed until it does.
 */
#ifndef CC700_BOARD_PINS_H
#define CC700_BOARD_PINS_H

/* Display: ST7789 on an 8-bit Intel i80 (8080) parallel bus, not SPI. */
#define PIN_LCD_DC 40
#define PIN_LCD_WR 3
#define PIN_LCD_RD 39 /* not part of esp_lcd's config: held high, so reads are disabled */
#define PIN_LCD_CS 2
#define PIN_LCD_D0 4
#define PIN_LCD_D1 38
#define PIN_LCD_D2 5
#define PIN_LCD_D3 48
#define PIN_LCD_D4 6
#define PIN_LCD_D5 47
#define PIN_LCD_D6 7
#define PIN_LCD_D7 11
#define PIN_LCD_BACKLIGHT 45

#define LCD_PCLK_HZ (15 * 1000 * 1000) /* 0x00e4e1c0 in the stock firmware */
#define LCD_BL_FREQ_HZ 20000           /* 0x4e20 */
#define LCD_BL_DUTY_BITS 10            /* LEDC low-speed timer 0, channel 0 */

/* Colour inversion. VERIFIED ON HARDWARE, 4 November 2026: with this false the dark UI came up pale
 * and hard to read; with it true the screen is correct. ESP-IDF's stock ST7789 driver defaults to
 * inversion off, and the stock firmware uses its own fork of that driver, which is where the
 * difference comes from. The element order is left RGB because the stock firmware passes 0 for it. */
#define LCD_INVERT_COLOR true

/* The stock firmware sets these two to outputs and drives them high immediately before creating the
 * i80 bus. They belong to none of its three config structs, and esp_lcd's reset_gpio_num is -1, so
 * panel power enable and panel reset are the obvious candidates. Purpose Unverified; the sequence is
 * reproduced because the panel may not come up without it. */
#define PIN_LCD_AUX_A 43
#define PIN_LCD_AUX_B 44

/* UARTs. UART0 is most likely the GNSS (the image carries Airoha $PAIR command strings) and the
 * second is the nRF co-processor that carries ANT+ (its RX line showed a heartbeat every ~13 s
 * during the RAM probe). Which is which is Unverified. */
#define PIN_UART0_TX 1
#define PIN_UART0_RX 0 /* also the ESP32 boot strapping pin */
#define PIN_UART_NRF_TX 42
#define PIN_UART_NRF_RX 41
#define UART_NRF_PORT 2

#endif
