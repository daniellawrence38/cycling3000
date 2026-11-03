/* fs_null.h: the storage until the separate storage's bus and file system are known (BRINGUP.md step 3
 * and 6). Every call fails, so the core runs with nothing stored and writes nothing anywhere. */
#ifndef FW_FS_NULL_H
#define FW_FS_NULL_H
#include "hal_fs.h"
hal_fs_t *fw_fs_null(void);
#endif
