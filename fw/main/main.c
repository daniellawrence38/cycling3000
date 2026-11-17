/* main.c: the unit firmware (brief WP7). One core task owns the core, the link server and LVGL
 * (brief 2: single-threaded, event driven); NimBLE runs in its own host task and talks to the core task
 * through queues (link_gatt.c, sensors.c).
 *
 * What runs: the core on null storage (fs_null.c) until bring-up step 1 (preserving the 4 GB storage)
 * is decided; the link server on GATT; the sensor manager; the nRF link (barometer, buttons, battery,
 * ambient light and SendPowerOffCmd); the UART0 GNSS NMEA listener; and the shared UI screen manager
 * (ui/src/ui_app.c) driving all 24 screens on the ST7789 over the i80 bus (hal_display_esp.c).
 *
 * Deliberately absent: nvs_flash_init (the stock NVS partition is left alone, Q29), TinyUSB, anything
 * that writes flash. */
#include <inttypes.h>
#include <string.h>

#include "board_pins.h"
#include "cc700/calib.h"
#include "cc700/core.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fs_null.h"
#include "fw.h"
#include "hal_baro.h"
#include "hal_ble.h"
#include "hal_clock.h"
#include "hal_display.h"
#include "hal_gnss.h"
#include "hal_i2c.h"
#include "hal_input.h"
#include "hal_nrf.h"
#include "hal_power.h"
#include "hal_touch.h"
#include "lvgl.h"
#include "nrf_proto.h"
#include "ui.h"

static const char *TAG = "cc700";

static cc_link_t link_srv;
static volatile uint16_t pending_mtu;
static volatile int phone_state = -1; /* -1 nothing new, 0 disconnected, 1 connected */
static uint32_t last_saved_seq;
static int last_backlight_pct = -1;
static int power_off_sent;

/* ------------------------------------------------------------------ link glue */
static void link_tx(void *ctx, const uint8_t *pkt, size_t len)
{
    (void)ctx;
    hal_ble_notify(pkt, len);
}

void fw_link_on_mtu(uint16_t mtu) { pending_mtu = mtu; }
void fw_link_on_connect(int connected) { phone_state = connected; }

/* ------------------------------------------------------------------ LVGL & UI glue */
static uint32_t tick_cb(void) { return hal_clock_monotonic_ms(); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    hal_display_flush((int16_t)a->x1, (int16_t)a->y1, (int16_t)a->x2, (int16_t)a->y2, (const uint16_t *)px);
    lv_display_flush_ready(d);
}

static ui_cal_state_t cal_state_hook(void)
{
    switch (cc_calibration_state()) {
    case CC_CAL_IDLE: return UI_CAL_IDLE;
    case CC_CAL_RUNNING: return UI_CAL_RUNNING;
    case CC_CAL_DONE: return UI_CAL_DONE;
    case CC_CAL_FAILED: return UI_CAL_FAILED;
    default: return UI_CAL_UNSUPPORTED;
    }
}

static const ui_app_hooks_t UI_HOOKS = {
    .pair_window = hal_ble_pairing_window,
    .passkey = hal_ble_passkey,
    .pairing = fw_link_pairing,
    .erase_bond = hal_ble_erase_bond,
    .cal_start = cc_calibration_start,
    .cal_state = cal_state_hook,
    .cal_ack = cc_calibration_ack,
};

#if TOUCH_LOG_PRESSES
/* A bring-up aid: a 16-pixel dot on LVGL's top layer that follows the finger while the screen is
 * touched. Lives on lv_layer_top() so screen transitions in ui_app_render do not delete it. */
static lv_obj_t *touch_dot;

static void touch_dot_create(void)
{
    touch_dot = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(touch_dot);
    lv_obj_set_size(touch_dot, 16, 16);
    lv_obj_set_style_radius(touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(touch_dot, lv_color_hex(0x00E5FF), 0);
    lv_obj_set_style_bg_opa(touch_dot, LV_OPA_COVER, 0);
    lv_obj_set_hidden(touch_dot, true);
    lv_obj_set_clickable(touch_dot, false);
}
#endif

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int16_t x = 0, y = 0;
    int down = hal_touch_read(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = down == 1 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    ui_app_touch(down == 1, x, y);
#if TOUCH_LOG_PRESSES
    if (touch_dot) {
        if (down == 1) {
            lv_obj_set_pos(touch_dot, x - 8, y - 8);
            lv_obj_set_hidden(touch_dot, false);
        } else {
            lv_obj_set_hidden(touch_dot, true);
        }
    }
#endif
}

static void apply_backlight(void)
{
    int pct = 100;
    if (ui_app_blank()) {
        pct = 0;
    } else {
        switch (cc_view()->settings.brightness) {
        case CC_BRIGHT_25: pct = 25; break;
        case CC_BRIGHT_50: pct = 50; break;
        case CC_BRIGHT_75: pct = 75; break;
        case CC_BRIGHT_100: pct = 100; break;
        case CC_BRIGHT_AUTO:
        default:
            pct = fw_nrf_light_backlight_pct(hal_power_ambient_lux());
            break;
        }
    }
    if (pct != last_backlight_pct) {
        last_backlight_pct = pct;
        hal_power_backlight(pct);
    }
}

static void ui_start(uint32_t now)
{
    if (hal_display_init() != 0)
        ESP_LOGE(TAG, "display did not start: the UI runs but nothing is drawn");
    if (hal_i2c_init() != 0)
        ESP_LOGE(TAG, "I2C did not start: no touch");
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(HAL_DISPLAY_W, HAL_DISPLAY_H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    /* Two 40-line buffers in internal DMA-capable RAM, sized to the i80 bus max_transfer_bytes. */
    size_t sz = HAL_DISPLAY_W * 40 * 2;
    void *b1 = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *b2 = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_buffers(d, b1, b2, sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    if (hal_touch_init() == 0) {
        lv_indev_t *in = lv_indev_create();
        lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(in, touch_read_cb);
        lv_indev_set_display(in, d);
#if TOUCH_LOG_PRESSES
        touch_dot_create();
#endif
    } else {
        ESP_LOGW(TAG, "no touch panel: the UI is button-only");
    }
    hal_nrf_start();
    hal_gnss_init();
    ui_app_init(cc_fs(), now, &UI_HOOKS);
    ui_app_render();
}

/* ------------------------------------------------------------------ the core task */
static void core_task(void *arg)
{
    (void)arg;
    uint32_t now = hal_clock_monotonic_ms();
#if FS_EMMC_MODE == 2
    struct hal_fs *fs = fw_fs_sdmmc();
#else
    struct hal_fs *fs = fw_fs_null();
    ESP_LOGW(TAG, "storage is the null stub (FS_EMMC_MODE %d): nothing is saved, and the STORAGE ERROR "
                  "banner is expected", FS_EMMC_MODE);
#endif
    int rec = cc_init(fs, now);
    ESP_LOGI(TAG, "core started (recovered %d)", rec);
    cc_link_host_t host;
    cc_link_host_bind(&host);
    cc_link_init(&link_srv, cc_fs(), &host, link_tx, NULL, 23);
    ui_start(now);
    fw_link_start();

    uint32_t next_tick = now;
#if BARO_LOG
    uint32_t next_log = now + 2000;
#endif
    for (;;) {
        now = hal_clock_monotonic_ms();
        if (pending_mtu) {
            cc_link_set_mtu(&link_srv, pending_mtu);
            pending_mtu = 0;
        }
        if (phone_state >= 0) {
            cc_on_phone(phone_state);
            if (phone_state)
                link_srv.ev_state = -1; /* first EVENT_STATE after connect */
            phone_state = -1;
        }
        int rx_any = 0;
        fw_pkt_t p;
        while (fw_link_take(&p)) {
            cc_link_rx(&link_srv, p.data, p.len, now);
            rx_any = 1;
        }
        if (rx_any && !cc_ride_active())
            ui_app_refresh_lists();
        hal_input_event_t ie;
        while (hal_input_poll(&ie)) {
#if NRF_DECODE_LOG
            static const char *const NAME[] = {"left down", "left up", "right down", "right up",
                                               "power down", "power up"};
            if (ie.kind <= HAL_PWR_UP)
                ESP_LOGI(TAG, "button: %s", NAME[ie.kind]);
#endif
            if (ie.kind <= HAL_PWR_UP)
                cc_on_button(now, (cc_btn_t)(ie.kind / 2), (ie.kind % 2) == 0);
        }
        fw_sensors_tick(now);
#if FS_EMMC_MODE == 1
        /* The read-only eMMC probe, at 10 s and again at 40 s after boot, not at boot. The first
           capture of it started 2.6 s in and missed it: the PC's serial port is not listening for
           the first seconds after a reset. Reading twice costs nothing and writes nothing, and two
           identical reports are a small check in themselves. The core task stops for about a
           second each time. */
        static int probe_runs;
        if (probe_runs < 2 && now >= (probe_runs ? 40000u : 10000u)) {
            probe_runs++;
            fw_emmc_probe();
        }
#endif
        if ((int32_t)(now - next_tick) >= 0) {
            next_tick += 100;
            int32_t pa = hal_baro_read_pa();
            if (pa >= 0)
                cc_on_baro(now, pa);
            hal_gnss_fix_t fix;
            if (hal_gnss_poll(&fix))
                cc_on_gnss(now, &fix);
            int bat = hal_power_battery_pct();
            if (bat >= 0)
                cc_on_battery(bat, hal_power_charging());
            cc_tick(now);
            ui_app_step(now);
            cc_link_tick(&link_srv, now);
            const cc_view_t *v = cc_view();
            if (fw_link_connected()) {
                cc_link_poll_events(&link_srv);
                if (v->saved_seq != last_saved_seq)
                    cc_link_event_ride_saved(&link_srv, v->saved_ride_id);
            }
            last_saved_seq = v->saved_seq;
            apply_backlight();
            if (v->power_off_request && !power_off_sent) {
                power_off_sent = 1;
                last_backlight_pct = -1;
                hal_power_off();
            }
        }
        if (ui_app_dirty())
            ui_app_render();
#if BARO_LOG
        if ((int32_t)(now - next_log) >= 0) {
            next_log += 2000;
            int32_t bp = hal_baro_read_pa();
            int16_t bt = 0;
            if (bp >= 0 && hal_nrf_baro_temp_c100(&bt) == 0)
                ESP_LOGI(TAG, "baro %" PRId32 " Pa, %d.%02d C, core alt %s%" PRId32 " m", bp, bt / 100,
                         bt % 100 < 0 ? -(bt % 100) : bt % 100, cc_view()->alt_ok ? "" : "(none) ",
                         cc_view()->alt_m);
            else
                ESP_LOGW(TAG, "baro: no fresh reading");
        }
#endif
        uint32_t wait = lv_timer_handler();
        ui_app_post_touch();
        if (ui_app_dirty())
            ui_app_render();
        if (wait > 10)
            wait = 10;
        vTaskDelay(pdMS_TO_TICKS(wait ? wait : 1));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CC700 firmware starting");
    fw_sensors_start();
    /* Core 1 for the core task; NimBLE's host runs on core 0 by default. 16 KB stack: the model is
     * static, LVGL's layout recursion is the deepest user. */
    xTaskCreatePinnedToCore(core_task, "core", 16384, NULL, 5, NULL, 1);
}
