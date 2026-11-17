/* mem_esp.h: RAM placement helpers and the memory snapshot (mem_esp.c). ESP-IDF only. */
#ifndef CC700_MEM_ESP_H
#define CC700_MEM_ESP_H
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* A queue whose item storage is in PSRAM, or an ordinary internal queue if PSRAM is missing or full.
   Only for queues used from tasks: never from an ISR, never while the flash cache is disabled. Never
   deleted, like every queue in this firmware. */
QueueHandle_t fw_queue_create_psram(UBaseType_t length, UBaseType_t item_size);

/* Two log lines: internal, DMA-capable and PSRAM heap, LVGL's share, and each task's stack
   high-water mark. `when` labels the lines ("boot", "60 s", ...). Core task only. */
void fw_mem_log(const char *when);
#endif
