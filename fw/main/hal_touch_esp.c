/* hal_touch_esp.c: the Hynitron CST3240 capacitive touch panel.
 *
 * PROVENANCE. Every byte of this protocol is read out of the stock firmware, by the same method that
 * decoded the display (docs/BRINGUP-LOG.md). The vendor's touch driver lives at 0x42032820..0x42032e76
 * in ota_0 and the relevant pieces are:
 *
 *   TouchInit        0x42032213  movi a10, 90 ; call 0x42032c74   -> probes I2C address 0x5A
 *   probe/version    0x42032c74  write 0xD045, read 4 bytes       -> the " 3240: 0x%x%x%x%x" line
 *   read (LVGL cb)   0x42032cc4  write 0xD000, read 7 bytes       -> one touch point
 *   write helper     0x42032bcc  i2c_master_transmit(buf1 || buf2)
 *   xfer helper      0x42032b78  i2c_master_transmit_receive(...)
 *
 * The coordinate arithmetic is lifted straight from 0x42032d93..0x42032db4:
 *
 *   down = (buf[0] & 0x0F) == 0x06
 *   x    = (buf[1] << 4) | (buf[3] >> 4)
 *   y    = (buf[2] << 4) | (buf[3] & 0x0F)
 *
 * and the vendor stores x and y into LVGL's point with no rotation, mirroring or scaling, so whatever
 * orientation the panel has is already inside the controller.
 *
 * TWO DELIBERATE DIFFERENCES FROM THE VENDOR, both logged in docs/QUESTIONS.md:
 *
 *  1. The vendor passes -1 as the I2C timeout, i.e. wait forever. This driver is polled from the
 *     task that also runs LVGL and the core, so a wedged bus would freeze the whole device rather
 *     than cost us a frame. TOUCH_I2C_TIMEOUT_MS bounds it instead.
 *  2. A failed read reports -1 and leaves the last position alone rather than decoding a buffer that
 *     was never filled.
 *
 * Everything else is imitation, including the odd write lengths; see TOUCH_VENDOR_QUIRK_WRITES in
 * board_pins.h for what those are and why they are copied rather than tidied.
 */
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "hal_display.h"
#include "hal_i2c.h"
#include "hal_touch.h"

static const char *TAG = "touch";

static i2c_master_dev_handle_t dev;
static uint32_t version;
static int16_t last_x, last_y;
#if TOUCH_LOG_PRESSES
static int logged;
#endif

/* The register selector, big-endian on the wire: the CST3240 takes a 16-bit register address high
 * byte first. The vendor builds these two bytes with a little-endian 16-bit store of 0xD000 and then
 * sends them after a byte-swapped copy of the same value; QUIRK below reproduces that. */
#define REG_BE(r) ((uint8_t)((r) >> 8)), ((uint8_t)((r) & 0xFF))
#define REG_LE(r) ((uint8_t)((r) & 0xFF)), ((uint8_t)((r) >> 8))

/* Selects a register. With the quirk on, the bytes are exactly what the vendor's helper emits. */
static esp_err_t select_reg(uint16_t reg)
{
#if TOUCH_VENDOR_QUIRK_WRITES
    const uint8_t w[4] = {REG_LE(reg), REG_BE(reg)};
#else
    const uint8_t w[2] = {REG_BE(reg)};
#endif
    return i2c_master_transmit(dev, w, sizeof w, TOUCH_I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    const uint8_t w[2] = {REG_BE(reg)};
    return i2c_master_transmit_receive(dev, w, sizeof w, buf, len, TOUCH_I2C_TIMEOUT_MS);
}

/* "Data consumed": the CST3240 holds its report until this is written. The vendor sends
 * {0xD0,0x00} followed by {0xD0,0x00,0xAB}; the published sequence is just the three bytes. */
static esp_err_t ack_report(void)
{
#if TOUCH_VENDOR_QUIRK_WRITES
    const uint8_t w[5] = {REG_BE(0xD000), REG_BE(0xD000), 0xAB};
#else
    const uint8_t w[3] = {REG_BE(0xD000), 0xAB};
#endif
    return i2c_master_transmit(dev, w, sizeof w, TOUCH_I2C_TIMEOUT_MS);
}

int hal_touch_present(void) { return dev != NULL; }
uint32_t hal_touch_version(void) { return version; }

int hal_touch_init(void)
{
    if (dev)
        return 0;
    i2c_master_bus_handle_t bus = hal_i2c_bus();
    if (!bus) {
        ESP_LOGE(TAG, "no I2C bus; touch is unavailable");
        return -1;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = TOUCH_I2C_HZ, /* the vendor's own 400 kHz */
    };
    esp_err_t e = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "add device 0x%02x failed: %s", TOUCH_I2C_ADDR, esp_err_to_name(e));
        dev = NULL;
        return -1;
    }

    /* The vendor's probe: select 0xD045, then read four bytes of version. If the panel does not
       answer, this is the point at which the stock firmware logs "Touch driver init failed". */
    uint8_t v[4] = {0};
    (void)select_reg(0xD045);
    e = read_reg(0xD045, v, sizeof v);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "0x%02x did not answer (%s): no touch", TOUCH_I2C_ADDR, esp_err_to_name(e));
        i2c_master_bus_rm_device(dev);
        dev = NULL;
        return -1;
    }
    version = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | v[3];
    /* The factory screen prints these four bytes one hex digit each, which is how this unit showed
       "3240: 0x1004". Printing all eight digits here says more and costs nothing. */
    ESP_LOGI(TAG, "CST3240 at 0x%02x, version %02x %02x %02x %02x (factory screen reads 0x%x%x%x%x)",
             TOUCH_I2C_ADDR, v[0], v[1], v[2], v[3], v[0], v[1], v[2], v[3]);
    return 0;
}

int hal_touch_read(int16_t *x, int16_t *y)
{
    if (x) *x = last_x;
    if (y) *y = last_y;
    if (!dev)
        return 0;

    uint8_t buf[TOUCH_DATA_LEN];
    esp_err_t e;
    /* Select, read, and on a bus error try exactly once more -- which is what the vendor does. */
    for (int attempt = 0; attempt < 2; attempt++) {
        (void)select_reg(0xD000);
        e = read_reg(0xD000, buf, sizeof buf);
        if (e == ESP_OK)
            break;
    }
    (void)ack_report(); /* the vendor acknowledges whether or not the read worked */
    if (e != ESP_OK) {
        static int complained;
        if (!complained) {
            complained = 1;
            ESP_LOGW(TAG, "read failed: %s (logged once)", esp_err_to_name(e));
        }
        return -1;
    }

    if ((buf[0] & 0x0F) != TOUCH_STATUS_DOWN)
        return 0;

    int rx = ((int)buf[1] << 4) | (buf[3] >> 4);
    int ry = ((int)buf[2] << 4) | (buf[3] & 0x0F);

#if TOUCH_SWAP_XY
    int t = rx; rx = ry; ry = t;
#endif
#if TOUCH_INVERT_X
    rx = (HAL_DISPLAY_W - 1) - rx;
#endif
#if TOUCH_INVERT_Y
    ry = (HAL_DISPLAY_H - 1) - ry;
#endif
    /* Clamped so a stray report cannot hand LVGL a point off the screen. The raw values are logged
       below precisely so that clamping never hides a wrong axis mapping. */
    int cx = rx < 0 ? 0 : rx > HAL_DISPLAY_W - 1 ? HAL_DISPLAY_W - 1 : rx;
    int cy = ry < 0 ? 0 : ry > HAL_DISPLAY_H - 1 ? HAL_DISPLAY_H - 1 : ry;

#if TOUCH_LOG_PRESSES
    /* The first presses only. Enough to check the axes against where the finger actually was, and
       quiet afterwards so the log stays readable during a ride. */
    if (logged < 20) {
        logged++;
        ESP_LOGI(TAG, "press %d: raw %d,%d -> %d,%d  [%02x %02x %02x %02x %02x %02x %02x]",
                 logged, rx, ry, cx, cy, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
    }
#endif
    last_x = (int16_t)cx;
    last_y = (int16_t)cy;
    if (x) *x = last_x;
    if (y) *y = last_y;
    return 1;
}
