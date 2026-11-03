/* A small C API for the Python tools (ctypes), built as libcc_testshim.so in the plain host build.
 * Not part of the firmware. */
#include <string.h>

#include "cc700/fit.h"
#include "memfs.h"

/* Recovers part[0..cut) as a .part file and copies the finished file to out. Returns its length, 0 if
 * the .part had no complete record, or -1 on error. records gets the record count of the result. */
int shim_fit_recover(const uint8_t *part, uint32_t cut, uint8_t *out, uint32_t cap, int32_t offset)
{
    static hal_fs_t *fs;
    if (!fs)
        fs = memfs_new();
    memfs_reset(fs);
    fs->mkdir(fs, "rides");
    memfs_put(fs, "rides/1.fit.part", part, cut);
    int rc = cc_fit_recover(fs, "rides/1.fit.part", "rides/1.fit.rec", "rides/1.fit", offset, NULL, NULL, NULL);
    if (rc == 1)
        return 0;
    if (rc != 0)
        return -1;
    uint32_t len;
    const uint8_t *b = memfs_data(fs, "rides/1.fit", &len);
    if (!b || len > cap)
        return -1;
    memcpy(out, b, len);
    return (int)len;
}

/* Complete records in b[0..len) as the recovery walk sees them (stops at a session message). */
int shim_fit_records(const uint8_t *b, uint32_t len)
{
    cc_fit_walk_t w;
    if (!cc_fit_walk(b, len, 1, &w))
        return -1;
    return (int)w.records;
}
