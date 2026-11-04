/* main.c: the firmware skeleton (brief WP7). One core task owns the core, the link server and LVGL
 * (brief 2: single-threaded, event driven); NimBLE runs in its own host task and talks to the core task
 * through queues (link_gatt.c, sensors.c).
 *
 * What runs: the core on null storage (fs_null.c) until the separate storage is identified; the link
 * server on GATT; the sensor manager; LVGL drawing the Ready screen from the core's view into a buffer
 * that hal_display_flush sends to the ST7789 over the i80 bus (fw/main/hal_display_esp.c).
 * The full screen manager is ui/sim/app.c in the simulator; it moves to a shared ui/ file in bring-up
 * step 7, when buttons and touch exist.
 *
 * Deliberately absent: nvs_flash_init (the stock NVS partition is left alone, Q29), TinyUSB, anything
 * that writes flash. */
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
#include "hal_ble.h"
#include "hal_clock.h"
#include "hal_display.h"
#include "hal_i2c.h"
#include "hal_nrf.h"
#include "hal_input.h"
#include "hal_touch.h"
#include "lvgl.h"
#include "ui.h"

static const char *TAG = "cc700";

static cc_link_t link_srv;
static volatile uint16_t pending_mtu;
static volatile int phone_state = -1; /* -1 nothing new, 0 disconnected, 1 connected */
static uint32_t last_saved_seq;
static ui_model_t model;              /* large: static, never on a task stack */
static lv_obj_t *root;

/* ------------------------------------------------------------------ link glue */
static void link_tx(void *ctx, const uint8_t *pkt, size_t len)
{
    (void)ctx;
    hal_ble_notify(pkt, len);
}

void fw_link_on_mtu(uint16_t mtu) { pending_mtu = mtu; }
void fw_link_on_connect(int connected) { phone_state = connected; }

/* ------------------------------------------------------------------ LVGL glue */
static uint32_t tick_cb(void) { return hal_clock_monotonic_ms(); }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    hal_display_flush((int16_t)a->x1, (int16_t)a->y1, (int16_t)a->x2, (int16_t)a->y2, (const uint16_t *)px);
    lv_display_flush_ready(d);
}

#if TOUCH_LOG_PRESSES
/* A bring-up aid, not a feature: a 16-pixel dot that follows the finger while the screen is touched.
 * The Ready screen has nothing tappable, so without this the only evidence that touch works is the
 * serial log -- and a dot under the finger also shows at a glance whether the axes are the right way
 * round, which a column of numbers does not. It lives on LVGL's top layer so the once-a-second screen
 * rebuild in ui_refresh does not delete it. Remove it with TOUCH_LOG_PRESSES. */
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

/* LVGL's pointer read callback. hal_touch_read leaves the coordinates at the last known position
 * when nothing is touching, which is what a pointer device must report on release: LVGL uses the
 * coordinate that arrives with the release to decide which widget was clicked, so returning 0,0
 * there would send every tap to the top-left corner. */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int16_t x = 0, y = 0;
    int down = hal_touch_read(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = down == 1 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
#if TOUCH_LOG_PRESSES
    /* The bring-up dot (see touch_dot above). Moved from inside LVGL's own callback, which is where
       LVGL expects objects to be touched, and on the top layer so ui_refresh's lv_obj_clean cannot
       take it away. */
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

static void ui_start(void)
{
    if (hal_display_init() != 0)
        ESP_LOGE(TAG, "display did not start: the UI runs but nothing is drawn");
    /* The touch controller is the only thing on this bus. The barometer, light sensor, battery
       gauge and IMU are not on it: they live behind the nRF co-processor on UART1 (see
       docs/BRINGUP-LOG.md). A failure here costs touch and nothing else, so it is logged. */
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
    /* Touch, if the panel answers. LVGL polls this through lv_timer_handler in the loop below, so
       there is no interrupt line to configure -- which matches the stock firmware, whose
       touch_driver_read is likewise just an LVGL read callback. */
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
    /* The nRF link, in its own task: the sensors and ANT+ are behind it (Q36) and it is a listener
       only for now, searching for the baud rate and logging the frames it can verify. */
    hal_nrf_start();
    root = lv_obj_create(NULL);
    lv_screen_load(root);
}

static void ui_refresh(void)
{
    model.v = *cc_view();
    model.phone = fw_link_connected() ? UI_PHONE_CONNECTED : fw_link_pairing() ? UI_PHONE_PAIRING : UI_PHONE_NOT_PAIRED;
    model.passkey = hal_ble_passkey();
    model.clock_min = -1;
    switch (cc_calibration_state()) {
    case CC_CAL_IDLE: model.cal = UI_CAL_IDLE; break;
    case CC_CAL_RUNNING: model.cal = UI_CAL_RUNNING; break;
    case CC_CAL_DONE: model.cal = UI_CAL_DONE; break;
    case CC_CAL_FAILED: model.cal = UI_CAL_FAILED; break;
    default: model.cal = UI_CAL_UNSUPPORTED; break;
    }
    lv_obj_clean(root);
    ui_build(SCR_READY, root, &model);
}

/* ------------------------------------------------------------------ the core task */
static void core_task(void *arg)
{
    (void)arg;
    uint32_t now = hal_clock_monotonic_ms();
    int rec = cc_init(fw_fs_null(), now);
    ESP_LOGI(TAG, "core started (init %d); storage not implemented, nothing is saved", rec);
    cc_link_host_t host;
    cc_link_host_bind(&host);
    cc_link_init(&link_srv, cc_fs(), &host, link_tx, NULL, 23);
    ui_start();
    fw_link_start();

    uint32_t next_tick = now, next_ui = now;
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
        fw_pkt_t p;
        while (fw_link_take(&p))
            cc_link_rx(&link_srv, p.data, p.len, now);
        hal_input_event_t ie;
        while (hal_input_poll(&ie)) {
            if (ie.kind <= HAL_PWR_UP)
                cc_on_button(now, (cc_btn_t)(ie.kind / 2), (ie.kind % 2) == 0);
        }
        fw_sensors_tick(now);
        if ((int32_t)(now - next_tick) >= 0) {
            next_tick += 100;
            cc_tick(now);
            cc_link_tick(&link_srv, now);
            const cc_view_t *v = cc_view();
            if (fw_link_connected()) {
                cc_link_poll_events(&link_srv);
                if (v->saved_seq != last_saved_seq)
                    cc_link_event_ride_saved(&link_srv, v->saved_ride_id);
            }
            last_saved_seq = v->saved_seq;
        }
        if ((int32_t)(now - next_ui) >= 0) {
            next_ui += 1000;
            ui_refresh();
        }
        uint32_t wait = lv_timer_handler();
        if (wait > 10)
            wait = 10;
        vTaskDelay(pdMS_TO_TICKS(wait ? wait : 1));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CC700 firmware skeleton; hardware drivers are stubs until bring-up");
    fw_sensors_start();
    /* Core 1 for the core task; NimBLE's host runs on core 0 by default. 16 KB stack: the model is
     * static, LVGL's layout recursion is the deepest user. */
    xTaskCreatePinnedToCore(core_task, "core", 16384, NULL, 5, NULL, 1);
}
