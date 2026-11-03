/* hal_fs.h: storage under the root `CC700/` on the separate storage (spec 7.2, brief 4).
 *
 * Written for the weakest file system the unit may have (FAT through FatFS): rename fails if the
 * destination exists, fsync is f_sync, and a power cut can leave a file at its size as of the last
 * sync. Every path is relative to the storage root; nothing outside it is touched.
 * The file system, its mount and free space on the unit are Unverified (spec 13.1 item 4). */
#ifndef HAL_FS_H
#define HAL_FS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_fs hal_fs_t;
typedef int hal_file_t; /* handle >= 0; negative values are errors */

enum { HAL_FS_READ = 1, HAL_FS_WRITE = 2, HAL_FS_CREATE = 4, HAL_FS_TRUNC = 8, HAL_FS_APPEND = 16 };

#define HAL_FS_MAX_NAME 64

typedef struct {
    char name[HAL_FS_MAX_NAME];
    uint32_t size;
    int is_dir;
} hal_dirent_t;

/* Each function returns 0 (or a byte count, or a handle) on success and a negative value on error. */
struct hal_fs {
    hal_file_t (*open)(hal_fs_t *fs, const char *path, int flags);
    int32_t (*read)(hal_fs_t *fs, hal_file_t f, void *buf, uint32_t len);
    int32_t (*write)(hal_fs_t *fs, hal_file_t f, const void *buf, uint32_t len);
    int (*seek)(hal_fs_t *fs, hal_file_t f, uint32_t pos);
    int32_t (*size)(hal_fs_t *fs, hal_file_t f);
    int (*fsync)(hal_fs_t *fs, hal_file_t f);
    int (*close)(hal_fs_t *fs, hal_file_t f);
    int (*rename)(hal_fs_t *fs, const char *from, const char *to); /* fails if `to` exists */
    int (*remove)(hal_fs_t *fs, const char *path);
    int (*mkdir)(hal_fs_t *fs, const char *path);                  /* 0 if it already exists */
    /* Calls cb for each entry of dir; stops early if cb returns non-zero. Returns the entry count. */
    int (*list)(hal_fs_t *fs, const char *dir, int (*cb)(void *ctx, const hal_dirent_t *e), void *ctx);
    int64_t (*free_bytes)(hal_fs_t *fs);
    void *ctx;
};

#ifdef __cplusplus
}
#endif
#endif
