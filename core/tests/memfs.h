/* memfs: an in-memory hal_fs with FAT semantics for host tests.
 * - rename refuses an existing destination;
 * - a power cut keeps bytes written in place, but a file that grew keeps only its size as of its last
 *   fsync (FatFS updates the directory entry on f_sync);
 * - faults: fail every write after a byte budget, or fail the Nth write call. */
#ifndef MEMFS_H
#define MEMFS_H
#include <stdint.h>

#include "hal_fs.h"

hal_fs_t *memfs_new(void);
void memfs_free(hal_fs_t *fs);
void memfs_reset(hal_fs_t *fs);                         /* delete everything, clear faults */
/* Power cut after `bytes` more bytes are written: the write that crosses the budget is applied up to
 * the budget, then every operation fails until memfs_power_cycle. -1 disables. */
void memfs_cut_after_bytes(hal_fs_t *fs, int64_t bytes);
/* Every write call fails (returns -1) from the Nth call on (1-based); 0 disables. Not a power cut. */
void memfs_fail_writes_from(hal_fs_t *fs, int64_t nth);
void memfs_power_cycle(hal_fs_t *fs);                   /* apply the cut: truncate to synced sizes */
int memfs_exists(hal_fs_t *fs, const char *path);
int64_t memfs_file_size(hal_fs_t *fs, const char *path);
/* Direct access for tests: returns a pointer to the file bytes, or NULL. */
const uint8_t *memfs_data(hal_fs_t *fs, const char *path, uint32_t *len);
int memfs_put(hal_fs_t *fs, const char *path, const void *data, uint32_t len); /* create, synced */
int64_t memfs_bytes_written(hal_fs_t *fs);
int memfs_count(hal_fs_t *fs, const char *dir_prefix);
#endif
