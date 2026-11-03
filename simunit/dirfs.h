/* dirfs: hal_fs backed by a host directory (the simulated `CC700/` root). Like FAT, rename refuses an
 * existing destination, so code that relies on POSIX replace semantics fails here, not on the unit.
 * Paths are relative to the root and may not contain "..". */
#ifndef DIRFS_H
#define DIRFS_H
#include "hal_fs.h"

hal_fs_t *dirfs_new(const char *root);
void dirfs_free(hal_fs_t *fs);
#endif
