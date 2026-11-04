/* hal_display_esp.c: the ST7789 panel on the ESP32-S3's i80 LCD peripheral.
 *
 * The bus, the pins and the timings all come from the stock firmware's own initialisation code; see
 * fw/main/board_pins.h for the provenance and docs/BRINGUP-LOG.md for how they were read. None of it
 * has been confirmed against the hardware yet, so this driver is Unverified until it lights the
 * panel (START-HERE rule 3).
 *
 * hal_display.h promises that hal_display_flush "returns when the buffer may be reused". The i80
 * driver is asynchronous: esp_lcd_panel_draw_bitmap queues a DMA transfer and returns immediately,
 * which would let LVGL start redrawing a buffer that is still being clocked out. So the transfer's
 * completion callback gives a semaphore and the flush waits on it, which keeps the documented
 * contract and leaves main.c's flush_cb correct as it stands.
 */
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_dev.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "hal_display.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t io_handle;
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t flush_done;

/* The largest single transfer LVGL can ask for: main.c allocates two 40-line buffers. */
#define LCD_MAX_TRANSFER_BYTES (HAL_DISPLAY_W * 40 * (int)sizeof(uint16_t))

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    (void)io;
    (void)ev;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(flush_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LCD_BL_DUTY_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = LCD_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t e = ledc_timer_config(&timer);
    if (e != ESP_OK)
        return e;
    ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0, /* dark until the panel is up, so no flash of noise on a cold start */
        .hpoint = 0,
    };
    return ledc_channel_config(&ch);
}

void hal_display_backlight(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint32_t max = (1u << LCD_BL_DUTY_BITS) - 1u;
    uint32_t duty = (max * (uint32_t)pct + 50u) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

#if LCD_TEST_PATTERN
/* Eight known colours, top to bottom, held briefly before the UI starts. If they come out in this
 * order the pixel format is right; if red and blue are swapped the element order needs BGR; if light
 * and dark are flipped the panel needs inversion. One photograph settles what guessing could not. */
static void test_pattern(void)
{
    static const uint16_t bars[8] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xFFE0, 0x07FF, 0xF81F};
    const int rows = HAL_DISPLAY_H / 8;
    uint16_t *line = heap_caps_malloc((size_t)HAL_DISPLAY_W * rows * sizeof(uint16_t),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!line)
        return;
    for (int b = 0; b < 8; b++) {
        for (int i = 0; i < HAL_DISPLAY_W * rows; i++)
            line[i] = bars[b];
        esp_lcd_panel_draw_bitmap(panel, 0, b * rows, HAL_DISPLAY_W, (b + 1) * rows, line);
        xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(4000));
    free(line);
}
#endif

int hal_display_init(void)

{
    if (panel)
        return 0;
    flush_done = xSemaphoreCreateBinary();
    if (!flush_done)
        return -1;

    if (backlight_init() != ESP_OK) {
        ESP_LOGE(TAG, "backlight (GPIO %d) failed", PIN_LCD_BACKLIGHT);
        return -1;
    }

    /* Reproduce the stock firmware's order: the two unexplained lines high, then RD high, then the
       bus. Their purpose is unknown, so this is imitation rather than understanding. */
    gpio_config_t aux = {
        .pin_bit_mask = (1ULL << PIN_LCD_AUX_A) | (1ULL << PIN_LCD_AUX_B) | (1ULL << PIN_LCD_RD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&aux) != ESP_OK)
        return -1;
    gpio_set_level(PIN_LCD_AUX_A, 1);
    gpio_set_level(PIN_LCD_AUX_B, 1);
    gpio_set_level(PIN_LCD_RD, 1); /* reads disabled: this panel is written to only */

    esp_lcd_i80_bus_handle_t bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = PIN_LCD_DC,
        .wr_gpio_num = PIN_LCD_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT, /* the stock firmware stores 6, i.e. PLL160M, the default */
        .data_gpio_nums = {PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
                           PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7},
        .bus_width = 8,
        .max_transfer_bytes = LCD_MAX_TRANSFER_BYTES,
    };
    if (esp_lcd_new_i80_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "i80 bus failed");
        return -1;
    }

    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        /* DC low for command, high for data: dc_data_level is the only bit the stock firmware sets. */
        .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1},
        /* LVGL 9 renders RGB565 in the CPU's native little-endian order; the ST7789 wants the high
           byte first. Without this the panel reads each pixel's bytes the wrong way round, which
           leaves white (0xFFFF, symmetric) white while turning the near-black background (0x0862)
           into a mid-tone (0x6208) -- a grey screen with pale text that looks like a contrast or
           inversion fault but is neither. Measured on the unit, 4 November 2026. */
        .flags = {.swap_color_bytes = LCD_SWAP_COLOR_BYTES},
    };
    if (esp_lcd_new_panel_io_i80(bus, &io_cfg, &io_handle) != ESP_OK) {
        ESP_LOGE(TAG, "panel io failed");
        return -1;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1, /* the stock firmware passes -1: reset is not esp_lcd's to drive */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel) != ESP_OK) {
        ESP_LOGE(TAG, "st7789 failed");
        return -1;
    }

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    /* This panel needs inversion on. ESP-IDF's stock ST7789 driver leaves it off; the stock firmware
       uses its own fork (mg_esp_lcd_panel_st7789.c), which evidently turns it on. Measured on the
       unit on 4 November 2026: with inversion off the dark UI came up pale and barely readable, with
       it on the screen is correct. This is the first fact here confirmed on hardware rather than
       read out of the firmware. */
    esp_lcd_panel_invert_color(panel, LCD_INVERT_COLOR);
    esp_lcd_panel_disp_on_off(panel, true);
    /* Only now is there something worth lighting. Leaving this out would give a perfectly
       initialised panel behind a dark backlight, which looks exactly like wrong pins. */
    hal_display_backlight(100);
#if LCD_TEST_PATTERN
    test_pattern();
#endif
    ESP_LOGI(TAG, "ST7789 up on i80: %dx%d, pclk %d Hz, backlight on", HAL_DISPLAY_W, HAL_DISPLAY_H,
             LCD_PCLK_HZ);
    return 0;
}

void hal_display_flush(int16_t x1, int16_t y1, int16_t x2, int16_t y2, const uint16_t *px)
{
    if (!panel)
        return;
    /* esp_lcd takes an exclusive end coordinate; LVGL gives an inclusive one. */
    if (esp_lcd_panel_draw_bitmap(panel, x1, y1, x2 + 1, y2 + 1, px) != ESP_OK)
        return;
    /* Hold the caller until the DMA has finished with the buffer (see the note at the top). */
    xSemaphoreTake(flush_done, pdMS_TO_TICKS(1000));
}
