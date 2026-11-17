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

static int sd_remove(hal_fs_t *fs, const char *path)
{
    (void)fs;
    char p[256];
    if (sd_full(path, p, sizeof p) != 0)
        return -1;
    return unlink(p) == 0 ? 0 : -2;
}

static int sd_mkdir(hal_fs_t *fs, const char *path)
{
    (void)fs;
    char p[256];
    if (sd_full(path, p, sizeof p) != 0)
        return -1;
    if (mkdir(p, 0755) == 0 || errno == EEXIST)
        return 0;
    return -1;
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
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(SD_MOUNT, &total, &free_b) != ESP_OK)
        return -1;
    return (int64_t)free_b;
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
    if (mkdir(CC700_ROOT, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s failed (errno %d); falling back to null storage", CC700_ROOT, errno);
        return fw_fs_null();
    }
    mounted = 1;
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(SD_MOUNT, &total, &free_b) == ESP_OK) {
        ESP_LOGW(TAG, "mounted eMMC at %s (root %s): %" PRIu64 " MB total, %" PRIu64 " MB free",
                 SD_MOUNT, CC700_ROOT, total / (1024u * 1024u), free_b / (1024u * 1024u));
    } else {
        ESP_LOGW(TAG, "mounted eMMC at %s (root %s)", SD_MOUNT, CC700_ROOT);
    }
    return &SD_FS;
}
