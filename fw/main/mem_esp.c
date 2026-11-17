/* mem_esp.c: where the firmware's RAM goes, and keeping internal DMA-capable RAM for what needs it.
 *
 * Three things live here.
 *
 * 1. LVGL's allocator (CONFIG_LV_USE_CUSTOM_MALLOC). With the C library allocator LVGL called plain
 *    malloc(), and ESP-IDF serves every malloc() up to CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 KB)
 *    from internal RAM. LVGL's allocations are all small (objects, styles, label text, draw tasks,
 *    glyph buffers), so every one of them landed in internal, DMA-capable SRAM -- the RAM the i80
 *    transfer buffer, the eMMC and the Bluetooth controller need. Here they go to PSRAM first and fall
 *    back to internal RAM if PSRAM is missing (Q30: CONFIG_SPIRAM_IGNORE_NOTFOUND=y) or full, so a unit
 *    without PSRAM behaves exactly as before. Nothing LVGL allocates is touched by DMA: the only DMA
 *    buffer is the draw buffer main.c allocates itself in internal RAM. LVGL runs only on the core task
 *    and this firmware never writes flash, so the cache is never disabled while LVGL's memory is in use.
 *
 * 2. fw_queue_create_psram(): a FreeRTOS queue whose item storage is in PSRAM, with the same fallback.
 *    The control block stays internal (it holds the queue's spinlock).
 *
 * 3. fw_mem_log(): one snapshot of the heap and of every known task's stack low-water mark, for the
 *    MEM_LOG switch in board_pins.h.
 */
#include "mem_esp.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "mem";

#define CAPS_PSRAM (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define CAPS_INTERNAL (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

/* ------------------------------------------------------------------ LVGL's allocator */
/* Bytes LVGL holds in each kind of RAM, and how often it had to fall back to internal RAM although
   PSRAM exists. Only the core task touches LVGL, so plain counters are enough. */
static size_t lv_in_psram, lv_in_internal, lv_peak;
static uint32_t lv_fallbacks;

static void lv_account(void *p, int sign)
{
    if (!p)
        return;
    size_t n = heap_caps_get_allocated_size(p);
    size_t *bucket = esp_ptr_external_ram(p) ? &lv_in_psram : &lv_in_internal;
    if (sign > 0)
        *bucket += n;
    else
        *bucket -= n < *bucket ? n : *bucket;
    if (lv_in_psram + lv_in_internal > lv_peak)
        lv_peak = lv_in_psram + lv_in_internal;
}

static int have_psram(void) { return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0; }

void lv_mem_init(void) {}

void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL; /* not supported, as with the C library allocator */
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, CAPS_PSRAM);
    if (!p) {
        p = heap_caps_malloc(size, CAPS_INTERNAL);
        if (p && have_psram())
            lv_fallbacks++;
    }
    lv_account(p, +1);
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    /* lv_realloc() handles new_size == 0 and the zero-size sentinel before it gets here. */
    size_t old = p ? heap_caps_get_allocated_size(p) : 0;
    int old_ext = p ? esp_ptr_external_ram(p) : 0;
    void *q = heap_caps_realloc(p, new_size, CAPS_PSRAM);
    if (!q) {
        /* PSRAM missing or full: realloc() keeps the block wherever it fits, internal included.
           On failure the old block is untouched, as realloc promises. */
        q = heap_caps_realloc(p, new_size, CAPS_INTERNAL);
        if (q && have_psram())
            lv_fallbacks++;
    }
    if (q) {
        size_t *bucket = old_ext ? &lv_in_psram : &lv_in_internal;
        *bucket -= old < *bucket ? old : *bucket;
        lv_account(q, +1);
    }
    return q;
}

void lv_free_core(void *p)
{
    lv_account(p, -1);
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    memset(mon_p, 0, sizeof *mon_p);
    mon_p->total_size = heap_caps_get_total_size(CAPS_PSRAM);
    mon_p->free_size = heap_caps_get_free_size(CAPS_PSRAM);
    mon_p->free_biggest_size = heap_caps_get_largest_free_block(CAPS_PSRAM);
    mon_p->max_used = lv_peak;
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

/* ------------------------------------------------------------------ queues */
QueueHandle_t fw_queue_create_psram(UBaseType_t length, UBaseType_t item_size)
{
    uint8_t *storage = heap_caps_malloc((size_t)length * item_size, CAPS_PSRAM);
    StaticQueue_t *ctl = storage ? heap_caps_malloc(sizeof *ctl, CAPS_INTERNAL) : NULL;
    QueueHandle_t q = ctl ? xQueueCreateStatic(length, item_size, storage, ctl) : NULL;
    if (q)
        return q;
    heap_caps_free(ctl);
    heap_caps_free(storage);
    return xQueueCreate(length, item_size); /* no PSRAM: internal, exactly as before */
}

/* ------------------------------------------------------------------ the snapshot */
/* Every long-lived task this firmware creates or causes to be created; a name that is not running is
   skipped. Names: IDLE<n> from FreeRTOS tasks.c, ipc<n> from esp_ipc.c, "esp_timer" from esp_timer.c,
   "Tmr Svc" from CONFIG_FREERTOS_TIMER_SERVICE_TASK_NAME, "nimble_host" from nimble_port_freertos.c,
   "btController" from libbtdm_app.a; "core" and "nrf" are ours. "main" is deliberately absent: it
   deletes itself when app_main returns, and a handle found while it waits for the idle task to free it
   would point at freed memory a moment later. xTaskGetHandle needs INCLUDE_xTaskGetHandle and the
   high-water mark INCLUDE_uxTaskGetStackHighWaterMark; ESP-IDF's FreeRTOSConfig.h sets both to 1
   unconditionally, so no Kconfig option is needed (configUSE_TRACE_FACILITY stays off). */
static const char *const TASKS[] = {"core", "nrf", "nimble_host", "btController", "esp_timer",
                                    "Tmr Svc", "ipc0", "ipc1", "IDLE0", "IDLE1"};

void fw_mem_log(const char *when)
{
    const unsigned int_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const unsigned int_big = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const unsigned int_min = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const unsigned dma_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const unsigned dma_big = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const unsigned ps_total = (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const unsigned ps_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const unsigned ps_big = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG,
             "%s: internal free %u (largest %u, lowest ever %u); DMA free %u (largest %u); "
             "PSRAM free %u of %u (largest %u); LVGL holds %u in PSRAM + %u internal (peak %u, "
             "%" PRIu32 " fallbacks)",
             when, int_free, int_big, int_min, dma_free, dma_big, ps_free, ps_total, ps_big,
             (unsigned)lv_in_psram, (unsigned)lv_in_internal, (unsigned)lv_peak, lv_fallbacks);

    char line[200];
    size_t used = 0;
    line[0] = '\0';
    for (size_t i = 0; i < sizeof TASKS / sizeof TASKS[0]; i++) {
        TaskHandle_t h = xTaskGetHandle(TASKS[i]);
        if (!h)
            continue;
        /* StackType_t is uint8_t on this port, so the high-water mark is in bytes. */
        unsigned hw = (unsigned)uxTaskGetStackHighWaterMark(h);
        int n = snprintf(line + used, sizeof line - used, "%s%s %u", used ? ", " : "", TASKS[i], hw);
        if (n < 0 || (size_t)n >= sizeof line - used)
            break;
        used += (size_t)n;
    }
    ESP_LOGI(TAG, "%s: stack never used, bytes: %s", when, line);
}
