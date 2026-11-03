#define _POSIX_C_SOURCE 200809L
#include "dirfs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

typedef struct {
    hal_fs_t fs;
    char root[512];
} dirfs_t;

static int full(hal_fs_t *fs, const char *rel, char *out, size_t cap)
{
    if (!rel || rel[0] == '/' || strstr(rel, ".."))
        return -1;
    int n = snprintf(out, cap, "%s/%s", ((dirfs_t *)fs->ctx)->root, rel);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static hal_file_t d_open(hal_fs_t *fs, const char *path, int flags)
{
    char p[1024];
    if (full(fs, path, p, sizeof p))
        return -1;
    int o = (flags & HAL_FS_WRITE) ? ((flags & HAL_FS_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (flags & HAL_FS_CREATE) o |= O_CREAT;
    if (flags & HAL_FS_TRUNC) o |= O_TRUNC;
    if (flags & HAL_FS_APPEND) o |= O_APPEND;
    int fd = open(p, o, 0644);
    return fd < 0 ? -1 : fd;
}

static int32_t d_read(hal_fs_t *fs, hal_file_t f, void *buf, uint32_t len)
{
    (void)fs;
    ssize_t n = read(f, buf, len);
    return n < 0 ? -1 : (int32_t)n;
}

static int32_t d_write(hal_fs_t *fs, hal_file_t f, const void *buf, uint32_t len)
{
    (void)fs;
    ssize_t n = write(f, buf, len);
    return n < 0 ? -1 : (int32_t)n;
}

static int d_seek(hal_fs_t *fs, hal_file_t f, uint32_t pos)
{
    (void)fs;
    return lseek(f, (off_t)pos, SEEK_SET) == (off_t)pos ? 0 : -1;
}

static int32_t d_size(hal_fs_t *fs, hal_file_t f)
{
    (void)fs;
    struct stat st;
    return fstat(f, &st) == 0 ? (int32_t)st.st_size : -1;
}

static int d_fsync(hal_fs_t *fs, hal_file_t f) { (void)fs; return fsync(f); }
static int d_close(hal_fs_t *fs, hal_file_t f) { (void)fs; return close(f); }

static int d_rename(hal_fs_t *fs, const char *from, const char *to)
{
    char a[1024], b[1024];
    if (full(fs, from, a, sizeof a) || full(fs, to, b, sizeof b))
        return -1;
    if (access(b, F_OK) == 0)
        return -3; /* FAT: never onto an existing name */
    return rename(a, b) == 0 ? 0 : -1;
}

static int d_remove(hal_fs_t *fs, const char *path)
{
    char p[1024];
    if (full(fs, path, p, sizeof p))
        return -1;
    return unlink(p) == 0 ? 0 : -2;
}

static int d_mkdir(hal_fs_t *fs, const char *path)
{
    char p[1024];
    if (full(fs, path, p, sizeof p))
        return -1;
    if (mkdir(p, 0755) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

static int d_list(hal_fs_t *fs, const char *dir, int (*cb)(void *, const hal_dirent_t *), void *ctx)
{
    char p[1024];
    if (full(fs, dir, p, sizeof p))
        return -1;
    DIR *d = opendir(p);
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(((hal_dirent_t *)0)->name))
            continue;
        hal_dirent_t de;
        memset(&de, 0, sizeof de);
        memcpy(de.name, e->d_name, strlen(e->d_name) + 1);
        char fp[1300];
        struct stat st;
        snprintf(fp, sizeof fp, "%s/%s", p, e->d_name);
        if (stat(fp, &st) == 0) {
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

static int64_t d_free(hal_fs_t *fs)
{
    struct statvfs v;
    if (statvfs(((dirfs_t *)fs->ctx)->root, &v) != 0)
        return -1;
    return (int64_t)v.f_bavail * (int64_t)v.f_frsize;
}

hal_fs_t *dirfs_new(const char *root)
{
    dirfs_t *d = calloc(1, sizeof *d);
    snprintf(d->root, sizeof d->root, "%s", root);
    mkdir(root, 0755);
    d->fs = (hal_fs_t){d_open, d_read, d_write, d_seek, d_size, d_fsync, d_close,
                       d_rename, d_remove, d_mkdir, d_list, d_free, d};
    return &d->fs;
}

void dirfs_free(hal_fs_t *fs) { free(fs->ctx); }
