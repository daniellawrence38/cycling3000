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

/* VERIFIED ON HARDWARE, 4 November 2026, by the colour-bar test: with the byte order fixed and
 * inversion off, every one of the eight bars came back as its exact complement -- red as cyan, white
 * as black, and so on. That is a pure inversion with no channel-order error, so this panel needs
 * INVON. Two faults were stacked: the missing byte swap kept white looking white (0xFFFF is
 * byte-symmetric) and hid the inversion underneath it. */
#define LCD_INVERT_COLOR true
#define LCD_SWAP_COLOR_BYTES 1
/* Colour bars at boot. Served their purpose: they proved the byte order and the inversion when
 * reasoning from photographs of anti-aliased text could not. Left in, off, because the next person
 * to doubt a panel will want them. */
#define LCD_TEST_PATTERN 0

/* The stock firmware sets these two to outputs and drives them high immediately before creating the
 * i80 bus. They belong to none of its three config structs, and esp_lcd's reset_gpio_num is -1, so
 * panel power enable and panel reset are the obvious candidates. Purpose Unverified; the sequence is
 * reproduced because the panel may not come up without it. */
#define PIN_LCD_AUX_A 43
#define PIN_LCD_AUX_B 44

/* I2C. Read from the vendor's InitI2CBus: i2c_master_bus_config_t with port 0, sda 21, scl 12,
 * clk_source 11, glitch_ignore_cnt 7 and internal pull-ups enabled. The RAM probe corroborates it
 * independently -- it recorded GPIO 21 and GPIO 12 both idling high, which is what pulled-up I2C
 * lines do. Touch sits on this bus, and so do at least two more devices (the vendor registers three
 * through I2CManagerAddDev). What they are is answered by the boot-time scan, not by guessing. */
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 12
#define I2C_PORT_NUM 0
#define I2C_GLITCH_IGNORE 7
#define I2C_SCAN_AT_BOOT 1 /* log every address that answers; set to 0 once the devices are known */

/* Touch: a Hynitron CST3240 at I2C address 0x5A, read out of the stock firmware.
 *
 * The vendor's `TouchInit` probes address 90 (0x5A) and installs its 3240 read function if that
 * answers; the alternative it supports is a 6336 (the factory test screen prints either
 * " 6336: 0x%02x" or " 3240: 0x%x%x%x%x"). This unit's factory screen answered on the 3240 path with
 * 0x1004, so this panel is the CST3240 -- now corroborated by the vendor code probing exactly 0x5A.
 *
 * 400 kHz is the vendor's own scl_speed_hz (0x00061a80 in its i2c_device_config_t), not a guess.
 *
 * STATUS: Unverified on hardware. The register sequence and the coordinate arithmetic are read from
 * the vendor's driver, which is far stronger than a guess, but nothing here has yet decoded a real
 * finger press (START-HERE rule 3). */
#define TOUCH_I2C_ADDR 0x5A
#define TOUCH_I2C_HZ 400000
#define TOUCH_I2C_TIMEOUT_MS 50 /* the vendor waits forever; hal_touch_esp.c says why we do not */
#define TOUCH_DATA_LEN 7        /* bytes the vendor reads from register 0xD000 */
#define TOUCH_STATUS_DOWN 0x06  /* value of (buf[0] & 0x0F) when a finger is down */

/* The vendor's write helper takes a register buffer and a data buffer and concatenates them, and
 * every call passes the 16-bit register twice, once little-endian and once big-endian. The bytes that
 * actually reach the panel are therefore four or five long rather than two or three. That looks like a
 * slip in the vendor's wrapper, but it is what this panel is known to work with, so it is reproduced
 * exactly. Set this to 0 to send the shorter, published CST3240 sequence instead. */
#define TOUCH_VENDOR_QUIRK_WRITES 1

/* No transform: the vendor stores the decoded x and y straight into LVGL's point, so any rotation or
 * mirroring is already in the controller's own configuration. The defines are here because the first
 * real press is what will say whether that holds for our LVGL geometry too. */
#define TOUCH_SWAP_XY 0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0
#define TOUCH_LOG_PRESSES 1 /* log the first presses, raw and mapped, so the axes can be checked */

/* UARTs.
 UART0 is most likely the GNSS (the image carries Airoha $PAIR command strings) and the
 * second is the nRF co-processor that carries ANT+ (its RX line showed a heartbeat every ~13 s
 * during the RAM probe). Which is which is Unverified. */
#define PIN_UART0_TX 1
#define PIN_UART0_RX 0 /* also the ESP32 boot strapping pin */
#define PIN_UART_NRF_TX 42
#define PIN_UART_NRF_RX 41
#define UART_NRF_PORT 2

#endif
