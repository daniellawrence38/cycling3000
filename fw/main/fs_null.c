#include "fs_null.h"

static hal_file_t n_open(hal_fs_t *fs, const char *p, int f) { (void)fs; (void)p; (void)f; return -1; }
static int32_t n_read(hal_fs_t *fs, hal_file_t f, void *b, uint32_t n) { (void)fs; (void)f; (void)b; (void)n; return -1; }
static int32_t n_write(hal_fs_t *fs, hal_file_t f, const void *b, uint32_t n) { (void)fs; (void)f; (void)b; (void)n; return -1; }
static int n_seek(hal_fs_t *fs, hal_file_t f, uint32_t p) { (void)fs; (void)f; (void)p; return -1; }
static int32_t n_size(hal_fs_t *fs, hal_file_t f) { (void)fs; (void)f; return -1; }
static int n_fsync(hal_fs_t *fs, hal_file_t f) { (void)fs; (void)f; return -1; }
static int n_close(hal_fs_t *fs, hal_file_t f) { (void)fs; (void)f; return -1; }
static int n_rename(hal_fs_t *fs, const char *a, const char *b) { (void)fs; (void)a; (void)b; return -1; }
static int n_remove(hal_fs_t *fs, const char *p) { (void)fs; (void)p; return -1; }
static int n_mkdir(hal_fs_t *fs, const char *p) { (void)fs; (void)p; return -1; }
static int n_list(hal_fs_t *fs, const char *d, int (*cb)(void *, const hal_dirent_t *), void *c)
{
    (void)fs; (void)d; (void)cb; (void)c;
    return -1;
}
static int64_t n_free(hal_fs_t *fs) { (void)fs; return -1; }

static hal_fs_t NULLFS = {n_open, n_read, n_write, n_seek, n_size, n_fsync, n_close, n_rename, n_remove, n_mkdir,
                          n_list, n_free, 0};

hal_fs_t *fw_fs_null(void) { return &NULLFS; }
