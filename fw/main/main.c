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
#include <stdio.h>
#include <stdlib.h>
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
#include "fw_sensors.h"
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
#include "mem_esp.h"
#include "nrf_proto.h"
#include "ui.h"

static const char *TAG = "cc700";

static cc_link_t link_srv;
static volatile uint16_t pending_mtu;
static volatile int phone_state = -1; /* -1 nothing new, 0 disconnected, 1 connected */
static uint32_t last_saved_seq;
static uint32_t lib_seen; /* link_srv.lib_changes at the last list refresh */
static int last_backlight_pct = -1;
#define PWR_HOLD_HEAD_START_MS 1500u /* see the power-down edge in the main loop */
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
    .sensor_scan = fw_sensors_scan,
    .sensor_scan_list = fw_sensors_scan_list,
    .sensor_pair = fw_sensors_pair,
    .sensor_forget = fw_sensors_forget,
    .sensor_saved = fw_sensors_saved,
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
#if TOUCH_LOG_TAPS
    /* Every tap: where it went down and came up, how far it wandered, how long it lasted. With
       the "tap:" line after it (what the UI did), that is enough to tell an offset panel from a
       jittery one from a target that is simply too small. */
    static int was_down;
    static int16_t x0, y0, xl, yl;
    static int wander;
    static uint32_t t0;
    if (down == 1) {
        if (!was_down) {
            x0 = x; y0 = y; wander = 0; t0 = hal_clock_monotonic_ms();
        } else {
            int d = abs(x - x0) > abs(y - y0) ? abs(x - x0) : abs(y - y0);
            if (d > wander) wander = d;
        }
        xl = x; yl = y;
    } else if (was_down) {
        ESP_LOGW(TAG, "touch: down %d,%d up %d,%d, wandered %d px, %" PRIu32 " ms", x0, y0, xl, yl, wander,
                 hal_clock_monotonic_ms() - t0);
    }
    was_down = down == 1;
#endif
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

/* One line on the ride for the periodic status log: what an outdoor test needs to be read back. */
void fw_ride_status_log(const char *when)
{
    static const char *const STATE[] = {"ready", "recording", "paused", "pause menu", "confirm discard", "saving", "summary"};
    const cc_view_t *v = cc_view();
    unsigned st = (unsigned)v->state;
    ESP_LOGW(TAG,
             "status %s: ride %s, timer %" PRIu32 " s, %" PRIu32 " m, speed %s%" PRIu32 " mm/s, gps fix %d, "
             "alt %s%" PRId32 " m, ascent %" PRIu32 " m, storage error %d, utc %" PRIu32,
             when, st < sizeof STATE / sizeof STATE[0] ? STATE[st] : "?", v->timer_s, v->dist_m,
             v->speed_ok ? "" : "(none) ", v->speed_mms, v->gps_fix, v->alt_ok ? "" : "(none) ", v->alt_m,
             v->ascent_m, v->storage_error, hal_clock_utc_seconds());
}

static void apply_backlight(void)
{
    int pct = 100;
    if (ui_app_blank() || hal_nrf_charge_only()) {
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
    /* ONE 40-line buffer in internal DMA-capable RAM (19,200 bytes), sized to the i80 bus
       max_transfer_bytes. There used to be two, but a second buffer only helps when the flush returns
       before the DMA finishes, so that LVGL can render the next band while this one is clocked out.
       hal_display_flush waits for the transfer to complete (hal_display_esp.c says why) and flush_cb
       calls lv_display_flush_ready straight after, so LVGL never had anything to overlap: the second
       buffer was 19,200 bytes of DMA RAM bought for nothing. If 40 lines cannot be had, 20 and then
       10 are tried (more bands per frame, same picture); with none at all LVGL logs "No draw buffer"
       and draws nothing, but the unit keeps running. */
    void *buf = NULL;
    size_t sz = 0;
    for (int lines = 40; lines >= 10 && !buf; lines /= 2) {
        sz = (size_t)HAL_DISPLAY_W * (size_t)lines * 2;
        buf = heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!buf)
            ESP_LOGE(TAG, "no %u bytes of DMA RAM for a %d-line draw buffer", (unsigned)sz, lines);
    }
    if (buf)
        lv_display_set_buffers(d, buf, NULL, (uint32_t)sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
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
    if (fw_fs_sdmmc_mounted())
        fw_log_file_open();
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
#if MEM_LOG
    fw_mem_log("boot"); /* after the display, LVGL, the UI, the UARTs and NimBLE have all allocated */
    uint32_t next_mem_log = now + 60000;
#endif

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
        fw_pkt_t p;
        while (fw_link_take(&p))
            cc_link_rx(&link_srv, p.data, p.len, now);
        /* Refresh the Workouts, Routes and History lists only when the phone has changed what they
           show (link.h, lib_changes), not after every batch of packets. A refresh reads and decodes
           every stored workout and route (up to 131 KB each) and every ride's metadata; done per
           packet batch it ran once per chunk of a route upload, on this task, which also owns the
           link, LVGL and the buttons. During a ride the phone cannot change the library (protocol 4
           answers BUSY), and leaving the counter unmatched means the refresh happens once the ride
           ends; ui_app_step refreshes on the return to Ready as well. */
        if (link_srv.lib_changes != lib_seen && !cc_ride_active()) {
            lib_seen = link_srv.lib_changes;
            ui_app_refresh_lists();
        }
        hal_input_event_t ie;
        while (hal_input_poll(&ie)) {
#if NRF_DECODE_LOG
            static const char *const NAME[] = {"left down", "left up", "right down", "right up",
                                               "power down", "power up"};
            if (ie.kind <= HAL_PWR_UP)
                ESP_LOGI(TAG, "button: %s", NAME[ie.kind]);
#endif
            /* Off and charging: the screen is dark and the buttons do nothing; holding power makes
               the nRF send its manual power-on event itself (seen 300 ms into the hold). The hold
               that switched the unit on is then swallowed until power is released, or it would
               carry straight on into a power-off. */
            static int swallow_pwr;
            if (hal_nrf_charge_only()) {
                swallow_pwr = 1;
                continue;
            }
            if (swallow_pwr && (ie.kind == HAL_PWR_DOWN || ie.kind == HAL_PWR_UP)) {
                if (ie.kind == HAL_PWR_UP)
                    swallow_pwr = 0;
                continue;
            }
            if (ie.kind == HAL_PWR_DOWN)
                /* The nRF reports a power hold only once it is already a long press, and about
                   1.5 s after its first "held" frame it cuts or restarts the ESP by itself
                   (outputs/handshake-2.txt: three holds, each cut 1.5 to 1.9 s after the first held
                   frame, before CC_HOLD_POWER_MS ran out, so SendPowerOffCmd was never sent). The
                   press is backdated so the core's 2 s power hold completes 0.5 s after the first
                   held frame, leaving about a second for the ride to be saved and SendPowerOffCmd to
                   go out. A click's down and up arrive together and are not affected (Q40). */
                cc_on_button(now - PWR_HOLD_HEAD_START_MS, CC_BTN_PWR, 1);
            else if (ie.kind <= HAL_PWR_UP)
                cc_on_button(now, (cc_btn_t)(ie.kind / 2), (ie.kind % 2) == 0);
        }
        fw_sensors_tick(now);
        /* A status line at 10 s, 20 s and 40 s, then every 60 s: the capture usually starts a few
           seconds after a reset and misses the handshake's own log lines. */
        static uint32_t next_status = 10000;
        if ((int32_t)(now - next_status) >= 0) {
            char when[16];
            snprintf(when, sizeof when, "%" PRIu32 " s", now / 1000u);
            fw_nrf_status_log(when);
            fw_gnss_status_log(when);
            fw_ride_status_log(when);
            fw_sensors_status_log(when);
            next_status = next_status < 20000 ? 20000 : next_status < 40000 ? 40000 : next_status + 30000;
        }
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
            static uint8_t log_div;
            if (++log_div >= 10) {
                log_div = 0;
                fw_log_file_tick(now);
            }
            ui_app_step(now);
            cc_link_tick(&link_srv, now);
            fw_link_tick(now); /* phone link: bond to the eMMC, pairing window expiry, idle parameters */
            const cc_view_t *v = cc_view();
            if (fw_link_connected()) {
                cc_link_poll_events(&link_srv);
                if (v->saved_seq != last_saved_seq)
                    cc_link_event_ride_saved(&link_srv, v->saved_ride_id);
            }
            last_saved_seq = v->saved_seq;
            apply_backlight();
            if (v->power_off_request && !power_off_sent) {
                /* hal_power_off blanks the backlight behind apply_backlight's back, so the next
                   apply_backlight must set it again whatever it last set. */
                last_backlight_pct = -1;
                /* Files closed before the power goes, as the vendor does (RE-FINDINGS-2 2.4). By now
                   the core has finished and saved any ride (closing the writer even if that failed)
                   and saved the settings. The link server's upload session is the only other file
                   that stays open between calls: abort it. Nothing else opens files outside this
                   task, and this task is parked inside hal_power_off from here on. */
                cc_link_abort(&link_srv);
                fw_log_file_close();
                int rc = hal_power_off();
                if (rc == HAL_POWER_OFF_SENT) {
                    /* Written, and the power is expected to drop: never send it twice. */
                    power_off_sent = 1;
                } else {
                    /* Still on. The latch stays open and the core's request is cleared, so the
                       screen comes back on the next tick and the rider's next 2 s hold tries again.
                       Latching here was the old behaviour: one failed attempt and power-off did
                       nothing until a reboot. */
                    ESP_LOGE(TAG, "power-off %s; the unit stays on",
                             rc == HAL_POWER_OFF_UNCONFIRMED ? "sent but not confirmed" : "not sent");
                    cc_power_off_cancel();
                }
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
#if MEM_LOG
        if ((int32_t)(now - next_mem_log) >= 0) {
            next_mem_log += 60000;
            char when[16];
            snprintf(when, sizeof when, "%" PRIu32 " s", now / 1000u);
            fw_mem_log(when);
        }
#endif
        uint32_t wait = lv_timer_handler();
        ui_app_post_touch();
#if TOUCH_LOG_TAPS
        static uint32_t tap_seen;
        uint32_t tap_now;
        const char *tap_what = ui_app_last_tap(&tap_now);
        if (tap_now != tap_seen) {
            tap_seen = tap_now;
            ESP_LOGW(TAG, "tap: %s", tap_what);
        }
#endif
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
    if (FS_EMMC_MODE == 3 || FS_EMMC_MODE == 4) {
        /* The eMMC read-out and nothing else: no display, UI, Bluetooth or nRF listener
           (emmc_dump_esp.c says why). Mode 4 sends the /CC700 folder only. */
        static const char only_cc700 = 1;
        ESP_LOGW(TAG, "FS_EMMC_MODE %d: eMMC read-out only. The screen stays dark; run tools/emmc_dump.py",
                 FS_EMMC_MODE);
        xTaskCreatePinnedToCore(fw_emmc_dump_task, "dump", 8192,
                                FS_EMMC_MODE == 4 ? (void *)&only_cc700 : NULL, 5, NULL, 1);
        return;
    }
    fw_log_file_init();
    fw_sensors_start();
    /* Core 1 for the core task; NimBLE's host runs on core 0 by default. 16 KB stack: the model is
     * static, LVGL's layout recursion is the deepest user. */
    xTaskCreatePinnedToCore(core_task, "core", 16384, NULL, 5, NULL, 1);
}
