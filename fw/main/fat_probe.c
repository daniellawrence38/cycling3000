/* fat_probe.c: see fat_probe.h. Layouts are Microsoft's FAT specification (fatgen103). */
#include "fat_probe.h"

#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int fw_fat_parse_mbr(const uint8_t s[FW_FAT_SECTOR], fw_part_t out[4])
{
    if (s[510] != 0x55 || s[511] != 0xAA)
        return -1;
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = s + 446 + 16 * i;
        out[i].type = e[4];
        out[i].lba_start = u32(e + 8);
        out[i].sectors = u32(e + 12);
        if (out[i].type == 0 || out[i].sectors == 0) {
            out[i].type = 0;
            out[i].lba_start = 0;
            out[i].sectors = 0;
        } else {
            n++;
        }
    }
    return n;
}

static void trim_label(char dst[12], const uint8_t *src)
{
    memcpy(dst, src, 11);
    dst[11] = '\0';
    for (int i = 10; i >= 0 && (dst[i] == ' ' || dst[i] == '\0'); i--)
        dst[i] = '\0';
    for (int i = 0; i < 11 && dst[i]; i++)
        if ((unsigned char)dst[i] < 0x20 || (unsigned char)dst[i] > 0x7E)
            dst[i] = '?';
}

int fw_fat_parse_bpb(const uint8_t s[FW_FAT_SECTOR], fw_fat_bpb_t *b)
{
    memset(b, 0, sizeof *b);
    if (s[510] != 0x55 || s[511] != 0xAA)
        return -1;
    if (memcmp(s + 3, "EXFAT   ", 8) == 0) {
        b->kind = FW_FS_EXFAT;
        return 0;
    }
    /* A boot sector starts with a jump: EB xx 90 or E9 xx xx. An MBR does not have to. */
    if (!((s[0] == 0xEB && s[2] == 0x90) || s[0] == 0xE9))
        return -1;
    uint16_t bps = u16(s + 11);
    uint8_t spc = s[13];
    uint16_t reserved = u16(s + 14);
    uint8_t fats = s[16];
    uint16_t root_entries = u16(s + 17);
    uint32_t total = u16(s + 19);
    uint32_t fatsz = u16(s + 22);
    if (total == 0)
        total = u32(s + 32);
    if (fatsz == 0)
        fatsz = u32(s + 36);
    if ((bps != 512 && bps != 1024 && bps != 2048 && bps != 4096) || spc == 0 || (spc & (spc - 1)) != 0 ||
        reserved == 0 || fats == 0 || fatsz == 0 || total == 0)
        return -1;
    uint32_t root_secs = ((uint32_t)root_entries * 32u + bps - 1u) / bps;
    uint64_t meta = (uint64_t)reserved + (uint64_t)fats * fatsz + root_secs;
    if (meta >= total)
        return -1;
    uint32_t clusters = (uint32_t)((total - meta) / spc);

    b->bytes_per_sector = bps;
    b->sectors_per_cluster = spc;
    b->reserved_sectors = reserved;
    b->fats = fats;
    b->fat_sectors = fatsz;
    b->total_sectors = total;
    b->root_dir_sectors = root_secs;
    b->root_dir_first = reserved + (uint32_t)fats * fatsz;
    b->data_first = (uint32_t)meta;
    b->clusters = clusters;
    /* The type is decided by the cluster count and by nothing else (fatgen103, "FAT Type Determination"). */
    if (clusters < 4085) {
        b->kind = FW_FS_FAT12;
        trim_label(b->label, s + 43);
    } else if (clusters < 65525) {
        b->kind = FW_FS_FAT16;
        trim_label(b->label, s + 43);
    } else {
        b->kind = FW_FS_FAT32;
        b->root_cluster = u32(s + 44);
        b->root_dir_sectors = 0;
        trim_label(b->label, s + 71);
    }
    return 0;
}

uint32_t fw_fat_cluster_sector(const fw_fat_bpb_t *b, uint32_t cluster)
{
    return b->data_first + (cluster - 2u) * b->sectors_per_cluster;
}

static unsigned entry_bytes(const fw_fat_bpb_t *b) { return b->kind == FW_FS_FAT32 ? 4u : 2u; }

uint32_t fw_fat_count_free(const fw_fat_bpb_t *b, const uint8_t *buf, size_t len, uint32_t first)
{
    if (b->kind != FW_FS_FAT16 && b->kind != FW_FS_FAT32)
        return 0;
    unsigned w = entry_bytes(b);
    uint32_t free_n = 0;
    for (size_t i = 0; i + w <= len; i += w) {
        uint32_t idx = first + (uint32_t)(i / w);
        if (idx < 2 || idx > b->clusters + 1u)
            continue;
        uint32_t v = w == 4 ? (u32(buf + i) & 0x0FFFFFFFu) : u16(buf + i);
        if (v == 0)
            free_n++;
    }
    return free_n;
}

uint32_t fw_fat_entry_sector(const fw_fat_bpb_t *b, uint32_t cluster)
{
    return b->reserved_sectors + (cluster * entry_bytes(b)) / b->bytes_per_sector;
}

uint32_t fw_fat_entry_value(const fw_fat_bpb_t *b, const uint8_t s[FW_FAT_SECTOR], uint32_t cluster)
{
    uint32_t off = (cluster * entry_bytes(b)) % b->bytes_per_sector;
    if (off + entry_bytes(b) > FW_FAT_SECTOR)
        return 0x0FFFFFFFu;
    return b->kind == FW_FS_FAT32 ? (u32(s + off) & 0x0FFFFFFFu) : u16(s + off);
}

int fw_fat_is_end(const fw_fat_bpb_t *b, uint32_t value)
{
    uint32_t top = b->kind == FW_FS_FAT32 ? 0x0FFFFFF7u : 0xFFF7u; /* bad cluster, then end marks */
    return value < 2 || value >= top || value > b->clusters + 1u;
}

int fw_fat_parse_dirent(const uint8_t e[32], fw_fat_dirent_t *out)
{
    if (e[0] == 0x00)
        return -1;
    if (e[0] == 0xE5)
        return 0;
    uint8_t attr = e[11];
    if ((attr & 0x0F) == 0x0F) /* a long-name part */
        return 0;
    if (attr & 0x08) /* the volume label */
        return 0;
    if (e[0] == '.')
        return 0;
    int k = 0;
    for (int i = 0; i < 8 && e[i] != ' '; i++)
        out->name[k++] = (char)(e[i] == 0x05 ? 0xE5 : e[i]);
    if (e[8] != ' ') {
        out->name[k++] = '.';
        for (int i = 8; i < 11 && e[i] != ' '; i++)
            out->name[k++] = (char)e[i];
    }
    out->name[k] = '\0';
    for (int i = 0; i < k; i++)
        if ((unsigned char)out->name[i] < 0x20 || (unsigned char)out->name[i] > 0x7E)
            out->name[i] = '?';
    out->is_dir = (attr & 0x10) != 0;
    out->size = u32(e + 28);
    out->cluster = ((uint32_t)u16(e + 20) << 16) | u16(e + 26);
    return 1;
}
