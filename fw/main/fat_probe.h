/* fat_probe.h: reading a FAT volume's layout from raw sectors, for the read-only eMMC probe
 * (hal_fs_esp.c, FS_EMMC_MODE 1). No ESP-IDF dependency; host-tested by core/tests/test_fat_probe.c.
 *
 * Everything here PARSES bytes it is handed. Nothing here can write: there is no write call in this
 * file or in the probe that uses it, which is the point of doing the probe without FatFs. */
#ifndef FW_FAT_PROBE_H
#define FW_FAT_PROBE_H
#include <stddef.h>
#include <stdint.h>

#define FW_FAT_SECTOR 512

typedef struct {
    uint8_t type;       /* the partition type byte */
    uint32_t lba_start; /* first sector */
    uint32_t sectors;
} fw_part_t;

/* Sector 0 as an MBR. Fills out[4] (unused entries have sectors == 0) and returns how many are in
 * use, or -1 if the sector has no 55 AA signature. A volume with no partition table has a boot
 * sector here instead: check fw_fat_parse_bpb() on the same sector first. */
int fw_fat_parse_mbr(const uint8_t s[FW_FAT_SECTOR], fw_part_t out[4]);

#define FW_FS_UNKNOWN 0
#define FW_FS_FAT12 12
#define FW_FS_FAT16 16
#define FW_FS_FAT32 32
#define FW_FS_EXFAT 64

typedef struct {
    int kind;                    /* FW_FS_* */
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;   /* the FAT starts this many sectors into the volume */
    uint8_t fats;
    uint32_t fat_sectors;        /* sectors in one FAT */
    uint32_t total_sectors;
    uint32_t root_dir_first;     /* FAT12/16: first sector of the fixed root directory, in the volume */
    uint32_t root_dir_sectors;   /* FAT12/16 only; 0 on FAT32 */
    uint32_t root_cluster;       /* FAT32 only */
    uint32_t data_first;         /* first sector of cluster 2, in the volume */
    uint32_t clusters;           /* data clusters; valid cluster numbers are 2 .. clusters + 1 */
    char label[12];              /* the boot sector's volume label, trimmed */
} fw_fat_bpb_t;

/* A boot sector. Returns 0 and fills *b (kind says which file system), or -1 if it is not a boot
 * sector this code understands. exFAT is recognised by name and nothing else is filled in. */
int fw_fat_parse_bpb(const uint8_t s[FW_FAT_SECTOR], fw_fat_bpb_t *b);

/* First sector of a cluster, in the volume. */
uint32_t fw_fat_cluster_sector(const fw_fat_bpb_t *b, uint32_t cluster);

/* Counts the free clusters in a run of FAT bytes. buf holds whole entries starting at entry number
 * `first`; entries outside 2 .. clusters + 1 are ignored. FAT16 and FAT32 only. */
uint32_t fw_fat_count_free(const fw_fat_bpb_t *b, const uint8_t *buf, size_t len, uint32_t first);

/* Following a cluster chain (FAT16 and FAT32): fw_fat_entry_sector says which sector of the volume
 * holds a cluster's FAT entry; read that sector and fw_fat_entry_value gives the next cluster. */
uint32_t fw_fat_entry_sector(const fw_fat_bpb_t *b, uint32_t cluster);
uint32_t fw_fat_entry_value(const fw_fat_bpb_t *b, const uint8_t s[FW_FAT_SECTOR], uint32_t cluster);
/* 1 if a FAT entry value ends a chain or is otherwise not a next cluster. */
int fw_fat_is_end(const fw_fat_bpb_t *b, uint32_t value);

typedef struct {
    char name[13]; /* NAME.EXT */
    uint8_t is_dir;
    uint32_t size;
    uint32_t cluster;
} fw_fat_dirent_t;

/* One 32-byte directory entry. Returns 1 and fills *out for a file or directory; 0 for an entry to
 * skip (deleted, a long-name part, the volume label, "." and ".."); -1 at the end of the directory. */
int fw_fat_parse_dirent(const uint8_t e[32], fw_fat_dirent_t *out);
#endif
