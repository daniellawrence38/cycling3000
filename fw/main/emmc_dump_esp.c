/* emmc_dump_esp.c: the eMMC read-out (FS_EMMC_MODE 3; FS_EMMC_MODE 4 sends the /CC700 folder only,
 * see send_cc700). Sends every sector of the storage that holds
 * anything to the PC over the USB serial port, as checksummed text records, over and over until the
 * unit is reset. tools/emmc_dump.py collects them and stops when two passes agree on every record.
 *
 * WHY. The storage has never been copied (bring-up step 1) and nothing should write to it until it
 * has been. The read-only probe showed one FAT32 volume with 286 MB in use out of 7.3 GB, so the copy
 * is small if only what is in use is sent.
 *
 * WHAT IS SENT. (1) Everything before the first data cluster: the boot sector, the reserved sectors
 * and both copies of the FAT. (2) Every cluster whose FAT entry is not zero. Together that is every
 * sector a FAT volume gives meaning to, and it is enough to rebuild the volume exactly. Free clusters
 * are not sent: their contents are whatever was left there and belong to no file.
 *
 * READ ONLY, BY CONSTRUCTION. This file calls sdmmc_read_sectors and nothing else. Nothing is
 * mounted, FatFs is not linked into a mode 3 image, and there is no write, erase or trim call in it.
 *
 * NOTHING ELSE RUNS. In this mode the firmware starts no display, no UI, no Bluetooth and no nRF
 * listener: the screen stays dark. That keeps other tasks' log lines out of the stream and leaves the
 * memory the storage driver needs free (the probe found only 31 KB of it in one piece otherwise). */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "driver/usb_serial_jtag.h"
#include "dump_fmt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fat_probe.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fw.h"
#include "sdmmc_cmd.h"

static sdmmc_card_t card;
static char line[FW_DUMP_LINE_MAX + 64];
static uint32_t n_records, n_sectors, crc_xor, n_errors, n_dropped;

/* Blocking, so nothing is lost while the PC is reading. If the PC is not listening the write times
 * out and the line is dropped; the PC tool sees the gap and takes the record from the next pass. */
static void out(const char *p, size_t n)
{
    while (n) {
        int w = usb_serial_jtag_write_bytes(p, n, pdMS_TO_TICKS(2000));
        if (w <= 0) {
            n_dropped++;
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > 0)
        out(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1);
}

/* Reads sectors and sends them as one record. A read that fails is reported and counted, not sent. */
static void send(uint8_t *buf, uint32_t lba, uint32_t sectors)
{
    if (sdmmc_read_sectors(&card, buf, lba, sectors) != ESP_OK) {
        n_errors++;
        say("#CC7 X read failed lba=%08" PRIx32 " sectors=%" PRIu32 "\n", lba, sectors);
        return;
    }
    uint32_t crc = 0;
    size_t n = fw_dump_record(line, sizeof line, lba, sectors, buf, &crc);
    if (!n)
        return;
    out(line, n);
    n_records++;
    n_sectors += sectors;
    crc_xor ^= crc;
}

/* ------------------------------------------------------------------ FS_EMMC_MODE 4: /CC700 only
 * After a ride the PC needs the rides, settings and logs this firmware wrote, not the stock firmware's
 * 300 MB. This pass sends the reserved sectors, the root directory, every cluster of the /CC700 tree
 * and the FAT sectors that describe those clusters, in the same record format, so emmc_dump.py
 * capture works unchanged and list/extract read it with --under /CC700. Still read-only:
 * sdmmc_read_sectors is the only storage call. */
static uint8_t *fatbuf, *dsec, *fat_used;
static uint32_t fat_cached = UINT32_MAX, sent_clusters;

/* The cluster after cl in its chain: 1 and *next, 0 at the end of the chain, -1 on a read error. */
static int next_cluster(const fw_fat_bpb_t *b, uint32_t cl, uint32_t *next)
{
    uint32_t s = fw_fat_entry_sector(b, cl);
    uint32_t idx = s - b->reserved_sectors;
    if (idx < b->fat_sectors)
        fat_used[idx / 8] |= (uint8_t)(1u << (idx % 8));
    if (s != fat_cached) {
        if (sdmmc_read_sectors(&card, fatbuf, s, 1) != ESP_OK) {
            n_errors++;
            fat_cached = UINT32_MAX;
            say("#CC7 X read failed fat lba=%08" PRIx32 "\n", s);
            return -1;
        }
        fat_cached = s;
    }
    uint32_t v = fw_fat_entry_value(b, fatbuf, cl);
    if (fw_fat_is_end(b, v) || v < 2 || v > b->clusters + 1u)
        return 0;
    *next = v;
    return 1;
}

static void send_cluster(uint8_t *buf, const fw_fat_bpb_t *b, uint32_t cl)
{
    uint32_t first = fw_fat_cluster_sector(b, cl);
    for (uint32_t s = 0; s < b->sectors_per_cluster; s += FW_DUMP_MAX_SECTORS) {
        uint32_t n = b->sectors_per_cluster - s < FW_DUMP_MAX_SECTORS ? b->sectors_per_cluster - s
                                                                       : FW_DUMP_MAX_SECTORS;
        send(buf, first + s, n);
    }
    sent_clusters++;
}

/* Sends a whole chain. Returns 0, or -1 if it loops or a FAT read failed. */
static int send_chain(uint8_t *buf, const fw_fat_bpb_t *b, uint32_t cl)
{
    for (uint32_t guard = 0; guard <= b->clusters; guard++) {
        send_cluster(buf, b, cl);
        uint32_t nx;
        int r = next_cluster(b, cl, &nx);
        if (r <= 0)
            return r;
        cl = nx;
    }
    say("#CC7 X cluster chain loops\n");
    return -1;
}

#define WALK_STACK 256
/* Reads directory dir (a cluster chain). Files in it are sent whole and subdirectories are pushed on
   stack; or, if want is not NULL, nothing is sent or pushed and the first subdirectory called want is
   returned (0 if there is none). */
static uint32_t scan_dir(uint8_t *buf, const fw_fat_bpb_t *b, uint32_t dir, const char *want, uint32_t *stack,
                         int *sp)
{
    uint32_t cl = dir;
    for (uint32_t guard = 0; guard <= b->clusters; guard++) {
        uint32_t first = fw_fat_cluster_sector(b, cl);
        for (uint32_t s = 0; s < b->sectors_per_cluster; s++) {
            if (sdmmc_read_sectors(&card, dsec, first + s, 1) != ESP_OK) {
                n_errors++;
                say("#CC7 X read failed dir lba=%08" PRIx32 "\n", first + s);
                return 0;
            }
            for (int e = 0; e < FW_FAT_SECTOR / 32; e++) {
                fw_fat_dirent_t d;
                int r = fw_fat_parse_dirent(dsec + e * 32, &d);
                if (r < 0)
                    return 0;
                if (r == 0 || d.cluster < 2)
                    continue;
                if (want) {
                    if (d.is_dir && strcmp(d.name, want) == 0)
                        return d.cluster;
                    continue;
                }
                if (d.is_dir) {
                    if (*sp < WALK_STACK)
                        stack[(*sp)++] = d.cluster;
                    else
                        say("#CC7 X too many folders; some not sent\n");
                } else {
                    send_chain(buf, b, d.cluster);
                }
            }
        }
        uint32_t nx;
        if (next_cluster(b, cl, &nx) <= 0)
            return 0;
        cl = nx;
    }
    return 0;
}

static void send_cc700(uint8_t *buf, const fw_fat_bpb_t *b)
{
    static uint32_t stack[WALK_STACK];
    int sp = 0;
    sent_clusters = 0;
    memset(fat_used, 0, (b->fat_sectors + 7) / 8);
    fat_cached = UINT32_MAX;
    /* (1) The boot sector, FSInfo and the other reserved sectors, but not the FATs. */
    for (uint32_t s = 0; s < b->reserved_sectors; s += FW_DUMP_MAX_SECTORS) {
        uint32_t n = b->reserved_sectors - s < FW_DUMP_MAX_SECTORS ? b->reserved_sectors - s : FW_DUMP_MAX_SECTORS;
        send(buf, s, n);
    }
    /* (2) The root directory, so the PC can find /CC700 in it. */
    send_chain(buf, b, b->root_cluster);
    uint32_t top = scan_dir(buf, b, b->root_cluster, "CC700", stack, &sp);
    if (!top) {
        say("#CC7 X no CC700 folder in the root directory\n");
    } else {
        /* (3) The /CC700 tree: each folder's own clusters, then every file in it. */
        stack[sp++] = top;
        while (sp > 0) {
            uint32_t dir = stack[--sp];
            send_chain(buf, b, dir);
            scan_dir(buf, b, dir, NULL, stack, &sp);
        }
    }
    /* (4) The FAT sectors that hold those chains (the first FAT only). */
    for (uint32_t i = 0; i < b->fat_sectors; i++)
        if (fat_used[i / 8] & (1u << (i % 8)))
            send(buf, b->reserved_sectors + i, 1);
}

static int start_card(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_MAX_FREQ_KHZ;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = PIN_SDMMC_CLK;
    slot.cmd = PIN_SDMMC_CMD;
    slot.d0 = PIN_SDMMC_D0;
    slot.d1 = PIN_SDMMC_D1;
    slot.d2 = PIN_SDMMC_D2;
    slot.d3 = PIN_SDMMC_D3;
    slot.width = SDMMC_BUS_WIDTH;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t e = sdmmc_host_init();
    if (e == ESP_OK)
        e = sdmmc_host_init_slot(host.slot, &slot);
    if (e == ESP_OK)
        e = sdmmc_card_init(&host, &card);
    if (e != ESP_OK) {
        say("#CC7 X no card: %s\n", esp_err_to_name(e));
        sdmmc_host_deinit();
        return -1;
    }
    return 0;
}

void fw_emmc_dump_task(void *arg)
{
    /* Our own records only on this port from here on. */
    esp_log_level_set("*", ESP_LOG_NONE);
    usb_serial_jtag_driver_config_t ucfg = {.tx_buffer_size = 16384, .rx_buffer_size = 256};
    usb_serial_jtag_driver_install(&ucfg); /* an "already installed" answer is fine */

    const int only_cc700 = arg != NULL; /* FS_EMMC_MODE 4 */
    uint8_t *buf = heap_caps_malloc(FW_DUMP_MAX_SECTORS * FW_FAT_SECTOR, MALLOC_CAP_DMA);
    uint8_t *fat = heap_caps_malloc(FW_FAT_SECTOR, MALLOC_CAP_DMA);
    fatbuf = fat;
    dsec = heap_caps_malloc(FW_FAT_SECTOR, MALLOC_CAP_DMA);
    fat_used = heap_caps_calloc(1, 65536, MALLOC_CAP_8BIT); /* one bit per FAT sector: up to 524,288 */
    while (!buf || !fat || !dsec || !fat_used || start_card() != 0) {
        say("#CC7 X cannot start (buffers %s); retrying in 5 s\n", buf && fat ? "ok" : "missing");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    for (uint32_t cycle = 1;; cycle++) {
        n_records = n_sectors = crc_xor = n_errors = 0;
        fw_fat_bpb_t b;
        if (sdmmc_read_sectors(&card, buf, 0, 1) != ESP_OK || fw_fat_parse_bpb(buf, &b) != 0 ||
            b.kind != FW_FS_FAT32 || b.bytes_per_sector != FW_FAT_SECTOR) {
            say("#CC7 X sector 0 is not the FAT32 boot sector the probe saw; nothing sent\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (b.fat_sectors > 65536u * 8u) {
            say("#CC7 X FAT too large for this read-out\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        say("#CC7 H cycle=%" PRIu32 " subset=%s card=%.8s card_sectors=%" PRIu32 " volume_sectors=%" PRIu32
            " reserved=%u fats=%u fat_sectors=%" PRIu32 " spc=%u data_first=%" PRIu32 " clusters=%" PRIu32
            " root=%" PRIu32 "\n",
            cycle, only_cc700 ? "CC700" : "all", card.cid.name, (uint32_t)card.csd.capacity, b.total_sectors, b.reserved_sectors, b.fats,
            b.fat_sectors, b.sectors_per_cluster, b.data_first, b.clusters, b.root_cluster);

        if (only_cc700) {
            send_cc700(buf, &b);
            say("#CC7 E cycle=%" PRIu32 " records=%" PRIu32 " sectors=%" PRIu32 " used_clusters=%" PRIu32
                " xor=%08" PRIx32 " read_errors=%" PRIu32 " dropped_lines=%" PRIu32 "\n",
                cycle, n_records, n_sectors, sent_clusters, crc_xor, n_errors, n_dropped);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        /* (1) Everything before the data area. */
        for (uint32_t s = 0; s < b.data_first; s += FW_DUMP_MAX_SECTORS) {
            uint32_t n = b.data_first - s < FW_DUMP_MAX_SECTORS ? b.data_first - s : FW_DUMP_MAX_SECTORS;
            send(buf, s, n);
        }
        /* (2) Every cluster in use, walking the first FAT one sector at a time. */
        uint32_t used = 0;
        for (uint32_t fs = 0; fs < b.fat_sectors; fs++) {
            if (sdmmc_read_sectors(&card, fat, b.reserved_sectors + fs, 1) != ESP_OK) {
                n_errors++;
                say("#CC7 X read failed fat_sector=%" PRIu32 "\n", fs);
                continue;
            }
            for (uint32_t i = 0; i < FW_FAT_SECTOR / 4; i++) {
                uint32_t cl = fs * (FW_FAT_SECTOR / 4) + i;
                if (cl < 2 || cl > b.clusters + 1u)
                    continue;
                if (fw_fat_entry_value(&b, fat, cl) == 0)
                    continue;
                used++;
                uint32_t first = fw_fat_cluster_sector(&b, cl);
                for (uint32_t s = 0; s < b.sectors_per_cluster; s += FW_DUMP_MAX_SECTORS) {
                    uint32_t n = b.sectors_per_cluster - s < FW_DUMP_MAX_SECTORS ? b.sectors_per_cluster - s
                                                                                   : FW_DUMP_MAX_SECTORS;
                    send(buf, first + s, n);
                }
            }
        }
        say("#CC7 E cycle=%" PRIu32 " records=%" PRIu32 " sectors=%" PRIu32 " used_clusters=%" PRIu32
            " xor=%08" PRIx32 " read_errors=%" PRIu32 " dropped_lines=%" PRIu32 "\n",
            cycle, n_records, n_sectors, used, crc_xor, n_errors, n_dropped);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
