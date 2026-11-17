#include "memfs.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FILES 512
#define MAX_OPEN 16
#define MAX_DIRS 32

typedef struct {
    int used;
    char name[128];
    uint8_t *data;
    uint32_t len, cap;
    uint32_t synced_len;
} mfile_t;

typedef struct {
    int used;
    int file;
    uint32_t pos;
    int flags;
} mhandle_t;

typedef struct {
    hal_fs_t fs;
    mfile_t files[MAX_FILES];
    mhandle_t open[MAX_OPEN];
    char dirs[MAX_DIRS][64];
    int ndirs;
    int64_t cut_budget;   /* -1 none */
    int dead;             /* after a power cut */
    int64_t fail_from;    /* 0 none */
    int64_t write_calls;
    int64_t written;
    int64_t free_override;
} memfs_t;

static memfs_t *M(hal_fs_t *fs) { return (memfs_t *)fs->ctx; }

static int find(memfs_t *m, const char *name)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (m->files[i].used && strcmp(m->files[i].name, name) == 0)
            return i;
    return -1;
}

static int create(memfs_t *m, const char *name)
{
    for (int i = 0; i < MAX_FILES; i++)
        if (!m->files[i].used) {
            memset(&m->files[i], 0, sizeof m->files[i]);
            m->files[i].used = 1;
            strncpy(m->files[i].name, name, sizeof m->files[i].name - 1);
            return i;
        }
    return -1;
}

static int is_open(memfs_t *m, int file)
{
    for (int i = 0; i < MAX_OPEN; i++)
        if (m->open[i].used && m->open[i].file == file)
            return 1;
    return 0;
}

static hal_file_t fs_open(hal_fs_t *fs, const char *path, int flags)
{
    memfs_t *m = M(fs);
    if (m->dead)
        return -1;
    int f = find(m, path);
    if (f < 0) {
        if (!(flags & HAL_FS_CREATE))
            return -2;
        f = create(m, path);
        if (f < 0)
            return -3;
    }
    if (flags & HAL_FS_TRUNC) {
        m->files[f].len = 0;
        m->files[f].synced_len = 0;
    }
    for (int h = 0; h < MAX_OPEN; h++)
        if (!m->open[h].used) {
            m->open[h].used = 1;
            m->open[h].file = f;
            m->open[h].flags = flags;
            m->open[h].pos = (flags & HAL_FS_APPEND) ? m->files[f].len : 0;
            return h;
        }
    return -4;
}

static mhandle_t *H(memfs_t *m, hal_file_t h)
{
    if (h < 0 || h >= MAX_OPEN || !m->open[h].used)
        return NULL;
    return &m->open[h];
}

static int32_t fs_read(hal_fs_t *fs, hal_file_t h, void *buf, uint32_t len)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o || m->dead)
        return -1;
    mfile_t *f = &m->files[o->file];
    if (o->pos >= f->len)
        return 0;
    if (len > f->len - o->pos)
        len = f->len - o->pos;
    memcpy(buf, f->data + o->pos, len);
    o->pos += len;
    return (int32_t)len;
}

static int32_t fs_write(hal_fs_t *fs, hal_file_t h, const void *buf, uint32_t len)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o || m->dead || !(o->flags & HAL_FS_WRITE))
        return -1;
    m->write_calls++;
    if (m->fail_from && m->write_calls >= m->fail_from)
        return -1;
    uint32_t take = len;
    int cut = 0;
    if (m->cut_budget >= 0 && (int64_t)len > m->cut_budget) {
        take = (uint32_t)m->cut_budget;
        cut = 1;
    }
    mfile_t *f = &m->files[o->file];
    if (o->pos + take > f->cap) {
        uint32_t nc = f->cap ? f->cap : 256;
        while (nc < o->pos + take)
            nc *= 2;
        f->data = realloc(f->data, nc);
        memset(f->data + f->cap, 0, nc - f->cap);
        f->cap = nc;
    }
    if (take)
        memcpy(f->data + o->pos, buf, take);
    o->pos += take;
    if (o->pos > f->len)
        f->len = o->pos;
    m->written += take;
    if (m->cut_budget >= 0)
        m->cut_budget -= take;
    if (cut) {
        m->dead = 1;
        return -1;
    }
    return (int32_t)len;
}

static int fs_seek(hal_fs_t *fs, hal_file_t h, uint32_t pos)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o || m->dead)
        return -1;
    o->pos = pos;
    return 0;
}

static int32_t fs_size(hal_fs_t *fs, hal_file_t h)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o || m->dead)
        return -1;
    return (int32_t)m->files[o->file].len;
}

static int fs_fsync(hal_fs_t *fs, hal_file_t h)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o || m->dead)
        return -1;
    m->files[o->file].synced_len = m->files[o->file].len;
    return 0;
}

static int fs_close(hal_fs_t *fs, hal_file_t h)
{
    memfs_t *m = M(fs);
    mhandle_t *o = H(m, h);
    if (!o)
        return -1;
    if (!m->dead) /* f_close syncs */
        m->files[o->file].synced_len = m->files[o->file].len;
    o->used = 0;
    return m->dead ? -1 : 0;
}

static int fs_rename(hal_fs_t *fs, const char *from, const char *to)
{
    memfs_t *m = M(fs);
    if (m->dead)
        return -1;
    int f = find(m, from);
    if (f < 0)
        return -2;
    if (find(m, to) >= 0)
        return -3; /* FAT: never onto an existing name */
    strncpy(m->files[f].name, to, sizeof m->files[f].name - 1);
    m->files[f].name[sizeof m->files[f].name - 1] = 0;
    return 0;
}

static int fs_remove(hal_fs_t *fs, const char *path)
{
    memfs_t *m = M(fs);
    if (m->dead)
        return -1;
    int f = find(m, path);
    if (f < 0)
        return -2;
    if (is_open(m, f))
        return -3;
    free(m->files[f].data);
    memset(&m->files[f], 0, sizeof m->files[f]);
    return 0;
}

static int fs_mkdir(hal_fs_t *fs, const char *path)
{
    memfs_t *m = M(fs);
    if (m->dead)
        return -1;
    for (int i = 0; i < m->ndirs; i++)
        if (strcmp(m->dirs[i], path) == 0)
            return 0;
    if (m->ndirs >= MAX_DIRS)
        return -2;
    strncpy(m->dirs[m->ndirs++], path, 63);
    return 0;
}

static int fs_list(hal_fs_t *fs, const char *dir, int (*cb)(void *, const hal_dirent_t *), void *ctx)
{
    memfs_t *m = M(fs);
    if (m->dead)
        return -1;
    size_t dl = strlen(dir);
    int n = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        mfile_t *f = &m->files[i];
        if (!f->used || strncmp(f->name, dir, dl) != 0 || f->name[dl] != '/' || strchr(f->name + dl + 1, '/'))
            continue;
        hal_dirent_t e;
        memset(&e, 0, sizeof e);
        strncpy(e.name, f->name + dl + 1, sizeof e.name - 1);
        e.size = f->len;
        n++;
        if (cb && cb(ctx, &e))
            break;
    }
    return n;
}

static int64_t fs_free(hal_fs_t *fs)
{
    return M(fs)->free_override >= 0 ? M(fs)->free_override : 1LL << 32;
}

void memfs_set_free(hal_fs_t *fs, int64_t bytes) { M(fs)->free_override = bytes; }

hal_fs_t *memfs_new(void)
{
    memfs_t *m = calloc(1, sizeof *m);
    m->fs = (hal_fs_t){fs_open, fs_read, fs_write, fs_seek, fs_size, fs_fsync, fs_close,
                       fs_rename, fs_remove, fs_mkdir, fs_list, fs_free, m};
    m->cut_budget = -1;
    m->free_override = -1;
    return &m->fs;
}

void memfs_reset(hal_fs_t *fs)
{
    memfs_t *m = M(fs);
    for (int i = 0; i < MAX_FILES; i++)
        free(m->files[i].data);
    memset(m->files, 0, sizeof m->files);
    memset(m->open, 0, sizeof m->open);
    m->ndirs = 0;
    m->cut_budget = -1;
    m->dead = 0;
    m->fail_from = 0;
    m->write_calls = 0;
    m->written = 0;
    m->free_override = -1;
}

void memfs_free(hal_fs_t *fs)
{
    memfs_reset(fs);
    free(M(fs));
}

void memfs_cut_after_bytes(hal_fs_t *fs, int64_t bytes) { M(fs)->cut_budget = bytes; }

void memfs_fail_writes_from(hal_fs_t *fs, int64_t nth)
{
    M(fs)->fail_from = nth ? M(fs)->write_calls + nth : 0;
}

void memfs_power_cycle(hal_fs_t *fs)
{
    memfs_t *m = M(fs);
    for (int i = 0; i < MAX_FILES; i++)
        if (m->files[i].used && m->files[i].len > m->files[i].synced_len)
            m->files[i].len = m->files[i].synced_len;
    memset(m->open, 0, sizeof m->open);
    m->dead = 0;
    m->cut_budget = -1;
    m->fail_from = 0;
}

int memfs_exists(hal_fs_t *fs, const char *path) { return find(M(fs), path) >= 0; }

int memfs_open_handles(hal_fs_t *fs)
{
    int n = 0;
    for (int i = 0; i < MAX_OPEN; i++)
        n += M(fs)->open[i].used != 0;
    return n;
}

int64_t memfs_file_size(hal_fs_t *fs, const char *path)
{
    int f = find(M(fs), path);
    return f < 0 ? -1 : (int64_t)M(fs)->files[f].len;
}

const uint8_t *memfs_data(hal_fs_t *fs, const char *path, uint32_t *len)
{
    int f = find(M(fs), path);
    if (f < 0)
        return NULL;
    *len = M(fs)->files[f].len;
    return M(fs)->files[f].data;
}

int memfs_put(hal_fs_t *fs, const char *path, const void *data, uint32_t len)
{
    hal_file_t h = fs->open(fs, path, HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    if (h < 0)
        return -1;
    if (len && fs->write(fs, h, data, len) != (int32_t)len)
        return -1;
    return fs->close(fs, h);
}

int64_t memfs_bytes_written(hal_fs_t *fs) { return M(fs)->written; }

int memfs_count(hal_fs_t *fs, const char *prefix)
{
    memfs_t *m = M(fs);
    int n = 0;
    for (int i = 0; i < MAX_FILES; i++)
        if (m->files[i].used && strncmp(m->files[i].name, prefix, strlen(prefix)) == 0)
            n++;
    return n;
}
