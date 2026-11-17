/* log_file_esp.c: keeps each boot's log in /sdcard/CC700/log/LOGnnnn.TXT, so a test away from the PC
 * (an outdoor ride) can be read afterwards with the FS_EMMC_MODE 4 read-out (`make fw-export`).
 *
 * HOW. esp_log's output function is wrapped: every line still goes to the USB console as before, and
 * a copy goes into a 64 KB ring in PSRAM. The core task, which owns the file system, drains the ring
 * into the file once a second and syncs it every two seconds, so a power cut loses at most about two
 * seconds of log. Lines logged before the eMMC is mounted wait in the ring, so the boot is in the file.
 *
 * LIMITS. A line is dropped (and counted) rather than wait: if another task holds the ring, if the
 * caller is an interrupt, or if the ring is full. Each file stops at 8 MB. Only this boot's file is
 * written; older ones are left alone. */
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fw.h"
#include "hal_clock.h"

#define LOG_DIR "/sdcard/CC700/log"
#define RING_SIZE (64 * 1024)
#define FILE_CAP (8u * 1024u * 1024u)
#define SYNC_MS 2000u

static const char *TAG = "logfile";
static char *ring;
static size_t head, tail; /* head: next write; tail: next read; empty when equal */
static SemaphoreHandle_t mtx;
static vprintf_like_t prev_vprintf;
static char line[256];
static volatile uint32_t dropped;
static FILE *f;
static uint32_t written, last_sync_ms;
static int capped;
static char path[48];

static int hook(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int r = prev_vprintf ? prev_vprintf(fmt, ap) : vprintf(fmt, ap);
    if (ring && !xPortInIsrContext() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        if (xSemaphoreTake(mtx, 0) == pdTRUE) {
            int n = vsnprintf(line, sizeof line, fmt, copy);
            if (n > (int)sizeof line - 1)
                n = (int)sizeof line - 1;
            for (int i = 0; i < n; i++) {
                size_t nh = (head + 1) % RING_SIZE;
                if (nh == tail) {
                    dropped++;
                    break;
                }
                ring[head] = line[i];
                head = nh;
            }
            xSemaphoreGive(mtx);
        } else {
            dropped++;
        }
    }
    va_end(copy);
    return r;
}

void fw_log_file_init(void)
{
    ring = heap_caps_malloc(RING_SIZE, MALLOC_CAP_SPIRAM);
    mtx = xSemaphoreCreateMutex();
    if (!ring || !mtx) {
        ring = NULL;
        ESP_LOGE(TAG, "no memory for the log ring: the log is not kept on the eMMC");
        return;
    }
    prev_vprintf = esp_log_set_vprintf(hook);
}

/* Opens LOGnnnn.TXT with the first free number. Called by the core task once the eMMC is mounted. */
void fw_log_file_open(void)
{
    if (!ring || f)
        return;
    if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "%s could not be created (errno %d): the log is not kept on the eMMC", LOG_DIR, errno);
        return;
    }
    /* One directory read, not up to 9999 stats: the highest number in use, plus one. */
    unsigned top = 0;
    DIR *d = opendir(LOG_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            unsigned n;
            if (sscanf(e->d_name, "LOG%4u.TXT", &n) == 1 && n > top)
                top = n;
        }
        closedir(d);
    }
    if (top >= 9999) {
        ESP_LOGE(TAG, "log numbers used up: the log is not kept on the eMMC");
        return;
    }
    snprintf(path, sizeof path, LOG_DIR "/LOG%04u.TXT", top + 1);
    f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "%s could not be opened (errno %d)", path, errno);
        return;
    }
    last_sync_ms = hal_clock_monotonic_ms();
    ESP_LOGW(TAG, "this boot's log is kept in %s", path);
}

static void drain(void)
{
    static char chunk[4096];
    for (;;) {
        size_t n = 0;
        xSemaphoreTake(mtx, portMAX_DELAY);
        while (tail != head && n < sizeof chunk) {
            chunk[n++] = ring[tail];
            tail = (tail + 1) % RING_SIZE;
        }
        xSemaphoreGive(mtx);
        if (!n)
            return;
        if (capped)
            continue;
        if (written + n > FILE_CAP) {
            fprintf(f, "\n--- log file reached %u bytes; nothing more is kept this boot ---\n", FILE_CAP);
            capped = 1;
            continue;
        }
        if (fwrite(chunk, 1, n, f) != n) {
            /* Close it rather than keep failing: the ride's own files matter more than the log. */
            fclose(f);
            f = NULL;
            ESP_LOGE(TAG, "write to %s failed (errno %d): the log is no longer kept", path, errno);
            return;
        }
        written += (uint32_t)n;
    }
}

/* Core task, about once a second. */
void fw_log_file_tick(uint32_t now_ms)
{
    if (!f)
        return;
    static uint32_t reported;
    if (dropped != reported) {
        uint32_t d = dropped;
        ESP_LOGW(TAG, "%" PRIu32 " log line(s) not kept so far (ring busy or full)", d);
        reported = d;
    }
    drain();
    if (f && now_ms - last_sync_ms >= SYNC_MS) {
        last_sync_ms = now_ms;
        fflush(f);
        fsync(fileno(f));
    }
}

/* Before power-off: everything so far onto the eMMC, and the file closed. */
void fw_log_file_close(void)
{
    if (!f)
        return;
    drain();
    if (f) {
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        f = NULL;
    }
}
