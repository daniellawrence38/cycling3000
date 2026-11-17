/* hal_fs_esp.c: the 4 GB eMMC on SDMMC slot 1, mounted at /sdcard and rooted at /sdcard/CC700.
 *
 * PROVENANCE. Read out of MidVFSMount at 0x4200b9b0 in ota_0 (docs/STOCK-FIRMWARE-FINDINGS.md):
 *   - SDMMC host slot 1 (SDMMC_HOST_SLOT_1), 4-bit bus, max_freq_khz = 20000 (0x4e20), internal pull-ups
 *     enabled (SDMMC_SLOT_FLAG_INTERNAL_PULLUP).
 *   - Pins: CLK = GPIO 13, CMD = GPIO 14, D0 = GPIO 16, D1 = GPIO 17, D2 = GPIO 18, D3 = GPIO 15,
 *     CD = -1, WP = -1 (matching the six remaining high-idling pins seen in the RAM probe).
 *   - Mounted at "/sdcard" with format_if_mount_failed = false, max_files = 16, allocation_unit_size = 16384.
 *
 * SAFETY. format_if_mount_failed is false, and every path passed to hal_fs_t is resolved strictly inside
 * "/sdcard/CC700/" (rejecting leading slashes and ".."), so the stock firmware's directories on /sdcard
 * (/sdcard/FIT, /sdcard/MAP, /sdcard/FONT, /sdcard/ROUTES, etc.) are never modified or formatted. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "fs_null.h"
#include "fw.h"
#include "hal_clock.h"
#include "hal_fs.h"
#include "sdmmc_cmd.h"

static const char *TAG = "fs";

#define SD_MOUNT "/sdcard"
#define CC700_ROOT "/sdcard/CC700"

static int mounted;
static sdmmc_card_t *card;

static int sd_full(const char *rel, char *out, size_t cap)
{
    if (!rel || !*rel || rel[0] == '/' || strstr(rel, ".."))
        return -1;
    int n = snprintf(out, cap, "%s/%s", CC700_ROOT, rel);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static hal_file_t sd_open(hal_fs_t *fs, const char *path, int flags)
{
    (void)fs;
    char p[256];
    if (sd_full(path, p, sizeof p) != 0)
        return -1;
    int o = (flags & HAL_FS_WRITE) ? ((flags & HAL_FS_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & HAL_FS_CREATE) o |= O_CREAT;
    if (flags & HAL_FS_TRUNC) o |= O_TRUNC;
    if (flags & HAL_FS_APPEND) o |= O_APPEND;
    int fd = open(p, o, 0644);
    return fd < 0 ? -1 : fd;
}

static int32_t sd_read(hal_fs_t *fs, hal_file_t f, void *buf, uint32_t len)
{
    (void)fs;
    ssize_t n = read(f, buf, len);
    return n < 0 ? -1 : (int32_t)n;
}

static int32_t sd_write(hal_fs_t *fs, hal_file_t f, const void *buf, uint32_t len)
{
    (void)fs;
    ssize_t n = write(f, buf, len);
    return n < 0 ? -1 : (int32_t)n;
}

static int sd_seek(hal_fs_t *fs, hal_file_t f, uint32_t pos)
{
    (void)fs;
    return lseek(f, (off_t)pos, SEEK_SET) == (off_t)pos ? 0 : -1;
}

static int32_t sd_size(hal_fs_t *fs, hal_file_t f)
{
    (void)fs;
    struct stat st;
    return fstat(f, &st) == 0 ? (int32_t)st.st_size : -1;
}

static int sd_fsync(hal_fs_t *fs, hal_file_t f)
{
    (void)fs;
    return fsync(f) == 0 ? 0 : -1;
}

static int sd_close(hal_fs_t *fs, hal_file_t f)
{
    (void)fs;
    return close(f) == 0 ? 0 : -1;
}

static int sd_rename(hal_fs_t *fs, const char *from, const char *to)
{
    (void)fs;
    char a[256], b[256];
    if (sd_full(from, a, sizeof a) != 0 || sd_full(to, b, sizeof b) != 0)
        return -1;
    struct stat st;
    if (stat(b, &st) == 0)
        return -3; /* FAT: never onto an existing name */
    return rename(a, b) == 0 ? 0 : -1;
}

/* Free space, cached. esp_vfs_fat_info calls FatFs's f_getfree, which counts the free clusters by
 * reading the whole FAT whenever it has no free count it trusts: 14,561 sectors per FAT on this unit's
 * eMMC. With this build's settings (CONFIG_FATFS_DONT_TRUST_FREE_CLUSTER_CNT 0) FatFs trusts the
 * FSINFO count and keeps it up to date itself, so the full scan should happen at most once, at mount.
 * But sd_free runs on the core task, inside requests from the phone (OBJ_PUT_BEGIN asks it), and a
 * scan there of a second or more could drop the BLE link. So this asks once at mount, again after a
 * file is removed (free space went up), and otherwise at most once every FREE_REFRESH_MS. Between
 * refreshes the figure can be high by what was written since; every caller wants a margin, not an
 * exact count, so that is acceptable. */
#define FREE_REFRESH_MS 10000u
static int64_t free_cached = -1;
static uint32_t free_at_ms;
static int free_stale = 1;

static void free_refresh(void)
{
    uint64_t total = 0, free_b = 0;
    free_cached = esp_vfs_fat_info(SD_MOUNT, &total, &free_b) == ESP_OK ? (int64_t)free_b : -1;
    free_at_ms = hal_clock_monotonic_ms();
    free_stale = 0;
}

static int sd_remove(hal_fs_t *fs, const char *path)
{
    (void)fs;
    char p[256];
    if (sd_full(path, p, sizeof p) != 0)
        return -1;
    if (unlink(p) != 0)
        return -2;
    free_stale = 1; /* recounted on the next sd_free, not here: retention removes several in a row */
    return 0;
}

static int sd_mkdir(hal_fs_t *fs, const char *path)
{
    (void)fs;
    char p[256];
    if (sd_full(path, p, sizeof p) != 0)
        return -1;
    if (mkdir(p, 0755) == 0)
        return 0;
    if (errno != EEXIST)
        return -1;
    /* EEXIST only says the name is taken. A FILE called "objects" or "rides" would pass for the
       directory and every later write into it would fail, so say so here instead. */
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

static int sd_list(hal_fs_t *fs, const char *dir, int (*cb)(void *, const hal_dirent_t *), void *ctx)
{
    (void)fs;
    char p[256];
    if (sd_full(dir, p, sizeof p) != 0)
        return -1;
    DIR *d = opendir(p);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t nl = strlen(e->d_name);
        if (e->d_name[0] == '.' || nl == 0 || nl >= HAL_FS_MAX_NAME)
            continue;
        hal_dirent_t de;
        memset(&de, 0, sizeof de);
        memcpy(de.name, e->d_name, nl + 1);
        char fp[340];
        struct stat st;
        int fn = snprintf(fp, sizeof fp, "%s/%s", p, e->d_name);
        if (fn > 0 && (size_t)fn < sizeof fp && stat(fp, &st) == 0) {
            de.size = (uint32_t)st.st_size;
            de.is_dir = S_ISDIR(st.st_mode);
        }
        n++;
        if (cb && cb(ctx, &de))
            break;
    }
    closedir(d);
    return n;
}

static int64_t sd_free(hal_fs_t *fs)
{
    (void)fs;
    if (free_stale || free_cached < 0 || hal_clock_monotonic_ms() - free_at_ms >= FREE_REFRESH_MS)
        free_refresh();
    return free_cached;
}

static hal_fs_t SD_FS = {
    sd_open, sd_read, sd_write, sd_seek, sd_size, sd_fsync, sd_close,
    sd_rename, sd_remove, sd_mkdir, sd_list, sd_free, NULL,
};

hal_fs_t *fw_fs_sdmmc(void)
{
    if (mounted)
        return &SD_FS;

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

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t e = esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mcfg, &card);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "eMMC mount at %s failed (%s); falling back to null storage", SD_MOUNT, esp_err_to_name(e));
        return fw_fs_null();
    }
    struct stat rst;
    int root_ok = mkdir(CC700_ROOT, 0755) == 0;
    int err = errno;
    if (!root_ok && err == EEXIST)
        root_ok = stat(CC700_ROOT, &rst) == 0 && S_ISDIR(rst.st_mode); /* not a FILE named CC700 */
    if (!root_ok) {
        ESP_LOGE(TAG, "%s could not be created or is not a directory (errno %d); unmounting and falling "
                      "back to null storage", CC700_ROOT, err);
        /* Unmount, so a volume this code will not use is not left mounted with FatFs's buffers held
           and the card initialised, and so nothing (a later call here included) writes to it. */
        esp_err_t ue = esp_vfs_fat_sdcard_unmount(SD_MOUNT, card);
        if (ue != ESP_OK)
            ESP_LOGE(TAG, "unmount of %s failed (%s)", SD_MOUNT, esp_err_to_name(ue));
        card = NULL;
        return fw_fs_null();
    }
    mounted = 1;
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(SD_MOUNT, &total, &free_b) == ESP_OK) {
        /* The first sd_free answers from this, without asking FatFs again. */
        free_cached = (int64_t)free_b;
        free_at_ms = hal_clock_monotonic_ms();
        free_stale = 0;
        ESP_LOGW(TAG, "mounted eMMC at %s (root %s): %" PRIu64 " MB total, %" PRIu64 " MB free",
                 SD_MOUNT, CC700_ROOT, total / (1024u * 1024u), free_b / (1024u * 1024u));
    } else {
        ESP_LOGW(TAG, "mounted eMMC at %s (root %s)", SD_MOUNT, CC700_ROOT);
    }
    return &SD_FS;
}

/* ------------------------------------------------------------------ the read-only probe
 *
 * FS_EMMC_MODE 1 (board_pins.h). Brings the eMMC up and READS it: the card's identity, the partition
 * table, each FAT volume's geometry, how much of it is in use, and the root directory. It does not
 * mount anything and it cannot write: it uses sdmmc_read_sectors and nothing else, FatFs is not
 * involved, and no file or directory is created. The storage has never been backed up (bring-up
 * step 1), and this is the step that says how big a backup would be before anything writes to it.
 *
 * The output is a block of "emmc:" lines in the boot log. */
#include "esp_heap_caps.h"
#include "fat_probe.h"

#define PROBE_CHUNK 4 /* sectors per read: 2 KB. It was 32 KB, which the unit could not spare: see below. */

static void probe_entry(const uint8_t *e, int *shown, int *done)
{
    fw_fat_dirent_t d;
    int r = fw_fat_parse_dirent(e, &d);
    if (r < 0 || *shown >= 200) {
        *done = 1;
    } else if (r > 0) {
        (*shown)++;
        if (d.is_dir)
            ESP_LOGW("emmc", "    %-12s  <DIR>", d.name);
        else
            ESP_LOGW("emmc", "    %-12s  %" PRIu32 " bytes", d.name, d.size);
    }
}

static int probe_volume(sdmmc_card_t *c, uint8_t *buf, uint32_t base, const fw_fat_bpb_t *b)
{
    const char *kind = b->kind == FW_FS_FAT12 ? "FAT12" : b->kind == FW_FS_FAT16 ? "FAT16"
                     : b->kind == FW_FS_FAT32 ? "FAT32" : "unknown";
    if (b->kind == FW_FS_EXFAT) {
        ESP_LOGW("emmc", "  volume at sector %" PRIu32 ": exFAT. FatFs in this build has no exFAT support, "
                         "so mode 2 would not mount it.", base);
        return 0;
    }
    uint32_t cluster_bytes = (uint32_t)b->bytes_per_sector * b->sectors_per_cluster;
    ESP_LOGW("emmc", "  volume at sector %" PRIu32 ": %s, label \"%s\", %" PRIu32 " sectors, %" PRIu32
                     " clusters of %" PRIu32 " bytes, %u FAT(s) of %" PRIu32 " sectors",
             base, kind, b->label, b->total_sectors, b->clusters, cluster_bytes, b->fats, b->fat_sectors);
    if (b->bytes_per_sector != FW_FAT_SECTOR || (b->kind != FW_FS_FAT16 && b->kind != FW_FS_FAT32)) {
        ESP_LOGW("emmc", "  not examined further (sector size %u, %s)", b->bytes_per_sector, kind);
        return 0;
    }

    /* How much is in use: count the free entries in the whole first FAT. */
    unsigned w = b->kind == FW_FS_FAT32 ? 4 : 2;
    uint32_t free_n = 0;
    for (uint32_t s = 0; s < b->fat_sectors; s += PROBE_CHUNK) {
        uint32_t n = b->fat_sectors - s < PROBE_CHUNK ? b->fat_sectors - s : PROBE_CHUNK;
        if (sdmmc_read_sectors(c, buf, base + b->reserved_sectors + s, n) != ESP_OK) {
            ESP_LOGE("emmc", "  read of FAT sector %" PRIu32 " failed", s);
            return -1;
        }
        free_n += fw_fat_count_free(b, buf, (size_t)n * FW_FAT_SECTOR, s * (FW_FAT_SECTOR / w));
    }
    uint32_t used = b->clusters - free_n;
    uint64_t used_b = (uint64_t)used * cluster_bytes, free_b = (uint64_t)free_n * cluster_bytes;
    ESP_LOGW("emmc", "  in use: %" PRIu32 " clusters = %" PRIu32 " MB; free: %" PRIu32 " clusters = %" PRIu32 " MB",
             used, (uint32_t)(used_b / (1024u * 1024u)), free_n, (uint32_t)(free_b / (1024u * 1024u)));

    /* The root directory, names and sizes only. */
    ESP_LOGW("emmc", "  root directory:");
    int shown = 0, done = 0;
    if (b->kind == FW_FS_FAT16) {
        for (uint32_t s = 0; s < b->root_dir_sectors && !done; s++) {
            if (sdmmc_read_sectors(c, buf, base + b->root_dir_first + s, 1) != ESP_OK)
                return -1;
            for (int i = 0; i < FW_FAT_SECTOR / 32 && !done; i++)
                probe_entry(buf + 32 * i, &shown, &done);
        }
    } else {
        uint32_t cl = b->root_cluster;
        for (int hops = 0; hops < 64 && !done && !fw_fat_is_end(b, cl); hops++) {
            for (uint32_t s = 0; s < b->sectors_per_cluster && !done; s++) {
                if (sdmmc_read_sectors(c, buf, base + fw_fat_cluster_sector(b, cl) + s, 1) != ESP_OK)
                    return -1;
                for (int i = 0; i < FW_FAT_SECTOR / 32 && !done; i++)
                    probe_entry(buf + 32 * i, &shown, &done);
            }
            if (sdmmc_read_sectors(c, buf, base + fw_fat_entry_sector(b, cl), 1) != ESP_OK)
                return -1;
            cl = fw_fat_entry_value(b, buf, cl);
        }
    }
    ESP_LOGW("emmc", "  %d entr%s shown (short names; long names are not decoded)", shown, shown == 1 ? "y" : "ies");
    return 0;
}

void fw_emmc_probe(void)
{
    ESP_LOGW("emmc", "==== eMMC read-only probe: nothing is mounted and nothing is written ====");
    ESP_LOGW("emmc", "memory: DMA-capable free %u bytes, largest block %u; internal free %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
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

    static sdmmc_card_t pc; /* static: it is large and this runs on the core task's stack */
    uint8_t *buf = heap_caps_malloc((size_t)PROBE_CHUNK * FW_FAT_SECTOR, MALLOC_CAP_DMA);
    if (!buf) {
        /* The first run on the unit failed here asking for 32 KB: once LVGL, NimBLE and the display
           buffers are up there is little DMA-capable internal RAM left. Say how little. */
        ESP_LOGE("emmc", "no memory for the read buffer (%u bytes wanted; DMA-capable free %u, largest block %u)",
                 (unsigned)(PROBE_CHUNK * FW_FAT_SECTOR), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
        return;
    }
    esp_err_t e = sdmmc_host_init();
    if (e != ESP_OK) {
        ESP_LOGE("emmc", "could not start the SDMMC host (%s)", esp_err_to_name(e));
        heap_caps_free(buf);
        return;
    }
    e = sdmmc_host_init_slot(host.slot, &slot);
    if (e == ESP_OK)
        e = sdmmc_card_init(&host, &pc);
    if (e != ESP_OK) {
        ESP_LOGE("emmc", "no card answered on CLK %d CMD %d D0-D3 %d %d %d %d (%s). Either the pins are "
                         "wrong or the storage is not powered.",
                 PIN_SDMMC_CLK, PIN_SDMMC_CMD, PIN_SDMMC_D0, PIN_SDMMC_D1, PIN_SDMMC_D2, PIN_SDMMC_D3,
                 esp_err_to_name(e));
        goto out;
    }
    uint64_t bytes = (uint64_t)pc.csd.capacity * (uint64_t)pc.csd.sector_size;
    ESP_LOGW("emmc", "card: %s \"%.8s\", %d sectors of %d bytes = %" PRIu32 " MB, running at %d kHz, %d-bit",
             pc.is_mmc ? "eMMC/MMC" : "SD", pc.cid.name, (int)pc.csd.capacity, (int)pc.csd.sector_size,
             (uint32_t)(bytes / (1024u * 1024u)), (int)pc.real_freq_khz, SDMMC_BUS_WIDTH);
    if (pc.csd.sector_size != FW_FAT_SECTOR) {
        ESP_LOGW("emmc", "sector size is not 512: not examined further");
        goto out;
    }
    if (sdmmc_read_sectors(&pc, buf, 0, 1) != ESP_OK) {
        ESP_LOGE("emmc", "read of sector 0 failed");
        goto out;
    }
    fw_fat_bpb_t b;
    if (fw_fat_parse_bpb(buf, &b) == 0) {
        ESP_LOGW("emmc", "sector 0 is a boot sector: one volume, no partition table");
        probe_volume(&pc, buf, 0, &b);
        goto out;
    }
    fw_part_t parts[4];
    int np = fw_fat_parse_mbr(buf, parts);
    if (np < 0) {
        ESP_LOGW("emmc", "sector 0 is neither a boot sector nor a partition table (first bytes %02x %02x %02x %02x)",
                 buf[0], buf[1], buf[2], buf[3]);
        goto out;
    }
    ESP_LOGW("emmc", "partition table: %d partition(s)", np);
    for (int i = 0; i < 4; i++) {
        if (!parts[i].sectors)
            continue;
        ESP_LOGW("emmc", " partition %d: type 0x%02x, starts at sector %" PRIu32 ", %" PRIu32 " sectors = %" PRIu32 " MB",
                 i + 1, parts[i].type, parts[i].lba_start, parts[i].sectors, parts[i].sectors / 2048u);
        if (sdmmc_read_sectors(&pc, buf, parts[i].lba_start, 1) != ESP_OK) {
            ESP_LOGE("emmc", "  read of its first sector failed");
            continue;
        }
        if (fw_fat_parse_bpb(buf, &b) == 0)
            probe_volume(&pc, buf, parts[i].lba_start, &b);
        else
            ESP_LOGW("emmc", "  no FAT boot sector there (first bytes %02x %02x %02x %02x)", buf[0], buf[1],
                     buf[2], buf[3]);
    }
out:
    sdmmc_host_deinit();
    heap_caps_free(buf);
    ESP_LOGW("emmc", "==== eMMC probe finished ====");
}
