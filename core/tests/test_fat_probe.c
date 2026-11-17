/* test_fat_probe.c: the read-only eMMC probe's sector parsing, on the host. The sectors are built here
 * to the FAT specification's layout; nothing from the unit's storage has been read yet. */
#include <string.h>

#include "../../fw/main/fat_probe.c"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, uint32_t v) { p16(p, (uint16_t)v); p16(p + 2, (uint16_t)(v >> 16)); }

/* A FAT32 boot sector for a volume of `total` sectors with 32-sector (16 KB) clusters. */
static void make_fat32(uint8_t s[512], uint32_t total, uint32_t fatsz)
{
    memset(s, 0, 512);
    s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90;
    memcpy(s + 3, "MSDOS5.0", 8);
    p16(s + 11, 512);
    s[13] = 32;
    p16(s + 14, 32);   /* reserved */
    s[16] = 2;         /* FATs */
    p16(s + 17, 0);    /* root entries: 0 on FAT32 */
    p16(s + 19, 0);
    p16(s + 22, 0);
    p32(s + 32, total);
    p32(s + 36, fatsz);
    p32(s + 44, 2);    /* root cluster */
    memcpy(s + 71, "CC700      ", 11);
    memcpy(s + 82, "FAT32   ", 8);
    s[510] = 0x55; s[511] = 0xAA;
}

static void test_mbr(void)
{
    uint8_t s[512];
    fw_part_t p[4];
    memset(s, 0, sizeof s);
    TEST_ASSERT_EQUAL(-1, fw_fat_parse_mbr(s, p)); /* no signature */
    s[510] = 0x55; s[511] = 0xAA;
    TEST_ASSERT_EQUAL(0, fw_fat_parse_mbr(s, p));  /* signed but empty */
    uint8_t *e = s + 446;
    e[4] = 0x0C;
    p32(e + 8, 8192);
    p32(e + 12, 7626752);
    TEST_ASSERT_EQUAL(1, fw_fat_parse_mbr(s, p));
    TEST_ASSERT_EQUAL_HEX8(0x0C, p[0].type);
    TEST_ASSERT_EQUAL_UINT32(8192, p[0].lba_start);
    TEST_ASSERT_EQUAL_UINT32(7626752, p[0].sectors);
    TEST_ASSERT_EQUAL_UINT32(0, p[1].sectors);
    /* An entry with a type but no length is not a partition. */
    s[446 + 16 + 4] = 0x83;
    TEST_ASSERT_EQUAL(1, fw_fat_parse_mbr(s, p));
}

static void test_fat32_boot_sector(void)
{
    uint8_t s[512];
    fw_fat_bpb_t b;
    /* About 3.6 GB: 7,626,752 sectors, 16 KB clusters, a 1,862-sector FAT. */
    make_fat32(s, 7626752u, 1862u);
    TEST_ASSERT_EQUAL(0, fw_fat_parse_bpb(s, &b));
    TEST_ASSERT_EQUAL(FW_FS_FAT32, b.kind);
    TEST_ASSERT_EQUAL(512, b.bytes_per_sector);
    TEST_ASSERT_EQUAL(32, b.sectors_per_cluster);
    TEST_ASSERT_EQUAL(32, b.reserved_sectors);
    TEST_ASSERT_EQUAL_UINT32(1862, b.fat_sectors);
    TEST_ASSERT_EQUAL_UINT32(2, b.root_cluster);
    TEST_ASSERT_EQUAL_UINT32(32 + 2 * 1862, b.data_first);
    TEST_ASSERT_EQUAL_UINT32((7626752u - (32 + 2 * 1862)) / 32, b.clusters);
    TEST_ASSERT_EQUAL_STRING("CC700", b.label);
    TEST_ASSERT_EQUAL_UINT32(b.data_first, fw_fat_cluster_sector(&b, 2));
    TEST_ASSERT_EQUAL_UINT32(b.data_first + 32, fw_fat_cluster_sector(&b, 3));

    /* The same sector is not mistaken for something else, and damage is refused. */
    uint8_t t[512];
    memcpy(t, s, 512); t[511] = 0;            TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b));
    memcpy(t, s, 512); t[0] = 0x00;           TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b));
    memcpy(t, s, 512); t[13] = 3;             TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b)); /* not a power of 2 */
    memcpy(t, s, 512); t[13] = 0;             TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b));
    memcpy(t, s, 512); p16(t + 11, 513);      TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b));
    memcpy(t, s, 512); t[16] = 0;             TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b));
    memcpy(t, s, 512); p32(t + 32, 100);      TEST_ASSERT_EQUAL(-1, fw_fat_parse_bpb(t, &b)); /* smaller than its FATs */
}

static void test_fat16_and_exfat(void)
{
    uint8_t s[512];
    fw_fat_bpb_t b;
    memset(s, 0, sizeof s);
    s[0] = 0xEB; s[1] = 0x3C; s[2] = 0x90;
    p16(s + 11, 512);
    s[13] = 4;
    p16(s + 14, 1);
    s[16] = 2;
    p16(s + 17, 512);   /* root entries: 32 sectors */
    p16(s + 19, 40000); /* total */
    p16(s + 22, 40);    /* FAT size */
    memcpy(s + 43, "RIDES      ", 11);
    s[510] = 0x55; s[511] = 0xAA;
    TEST_ASSERT_EQUAL(0, fw_fat_parse_bpb(s, &b));
    TEST_ASSERT_EQUAL(FW_FS_FAT16, b.kind);
    TEST_ASSERT_EQUAL_UINT32(1 + 80, b.root_dir_first);
    TEST_ASSERT_EQUAL_UINT32(32, b.root_dir_sectors);
    TEST_ASSERT_EQUAL_UINT32(1 + 80 + 32, b.data_first);
    TEST_ASSERT_EQUAL_UINT32((40000 - 113) / 4, b.clusters);
    TEST_ASSERT_EQUAL_STRING("RIDES", b.label);

    memset(s, 0, sizeof s);
    s[0] = 0xEB; s[1] = 0x76; s[2] = 0x90;
    memcpy(s + 3, "EXFAT   ", 8);
    s[510] = 0x55; s[511] = 0xAA;
    TEST_ASSERT_EQUAL(0, fw_fat_parse_bpb(s, &b));
    TEST_ASSERT_EQUAL(FW_FS_EXFAT, b.kind);
}

static void test_free_count_and_chain(void)
{
    uint8_t s[512];
    fw_fat_bpb_t b;
    make_fat32(s, 7626752u, 1862u);
    fw_fat_parse_bpb(s, &b);

    /* The first FAT sector: entries 0 and 1 are reserved, 2 -> 3 -> end, 4 free, 5 bad, 6.. free. */
    uint8_t f[512];
    memset(f, 0, sizeof f);
    p32(f + 0, 0x0FFFFFF8u);
    p32(f + 4, 0xFFFFFFFFu);
    p32(f + 8, 3);
    p32(f + 12, 0x0FFFFFFFu);
    p32(f + 20, 0x0FFFFFF7u);
    /* 128 entries in the sector, 2 reserved, 3 used (2, 3 and the bad one): 123 free. */
    TEST_ASSERT_EQUAL_UINT32(123, fw_fat_count_free(&b, f, sizeof f, 0));
    /* The top four bits of a FAT32 entry are not part of the value. */
    p32(f + 16, 0xF0000000u);
    TEST_ASSERT_EQUAL_UINT32(123, fw_fat_count_free(&b, f, sizeof f, 0));
    /* Entries past the last cluster do not count as free space. */
    uint8_t z[512];
    memset(z, 0, sizeof z);
    TEST_ASSERT_EQUAL_UINT32(128, fw_fat_count_free(&b, z, sizeof z, 1024));
    TEST_ASSERT_EQUAL_UINT32(2, fw_fat_count_free(&b, z, sizeof z, b.clusters));       /* clusters, clusters + 1 */
    TEST_ASSERT_EQUAL_UINT32(0, fw_fat_count_free(&b, z, sizeof z, b.clusters + 2u));

    TEST_ASSERT_EQUAL_UINT32(32, fw_fat_entry_sector(&b, 2));
    TEST_ASSERT_EQUAL_UINT32(32, fw_fat_entry_sector(&b, 127));
    TEST_ASSERT_EQUAL_UINT32(33, fw_fat_entry_sector(&b, 128));
    TEST_ASSERT_EQUAL_UINT32(3, fw_fat_entry_value(&b, f, 2));
    TEST_ASSERT_FALSE(fw_fat_is_end(&b, 3));
    TEST_ASSERT_TRUE(fw_fat_is_end(&b, fw_fat_entry_value(&b, f, 3)));
    TEST_ASSERT_TRUE(fw_fat_is_end(&b, 0));
    TEST_ASSERT_TRUE(fw_fat_is_end(&b, 0x0FFFFFF7u));
    TEST_ASSERT_TRUE(fw_fat_is_end(&b, b.clusters + 2u)); /* a pointer off the end of the volume */
}

static void test_directory_entries(void)
{
    fw_fat_dirent_t d;
    uint8_t e[32];

    memset(e, 0, sizeof e);
    TEST_ASSERT_EQUAL(-1, fw_fat_parse_dirent(e, &d)); /* end of directory */

    memset(e, ' ', 11);
    memcpy(e, "FITS", 4);
    e[11] = 0x10;
    p16(e + 20, 0x0001);
    p16(e + 26, 0x2345);
    p32(e + 28, 0);
    TEST_ASSERT_EQUAL(1, fw_fat_parse_dirent(e, &d));
    TEST_ASSERT_EQUAL_STRING("FITS", d.name);
    TEST_ASSERT_TRUE(d.is_dir);
    TEST_ASSERT_EQUAL_HEX32(0x00012345u, d.cluster);

    memset(e, 0, sizeof e);
    memcpy(e, "PRODUCT C  ", 11);
    e[11] = 0x20;
    p32(e + 28, 123456);
    TEST_ASSERT_EQUAL(1, fw_fat_parse_dirent(e, &d));
    TEST_ASSERT_EQUAL_STRING("PRODUCT.C", d.name);
    TEST_ASSERT_FALSE(d.is_dir);
    TEST_ASSERT_EQUAL_UINT32(123456, d.size);

    e[0] = 0xE5;                 TEST_ASSERT_EQUAL(0, fw_fat_parse_dirent(e, &d)); /* deleted */
    e[0] = 'P'; e[11] = 0x0F;    TEST_ASSERT_EQUAL(0, fw_fat_parse_dirent(e, &d)); /* long-name part */
    e[11] = 0x08;                TEST_ASSERT_EQUAL(0, fw_fat_parse_dirent(e, &d)); /* volume label */
    memset(e, ' ', 11); e[0] = '.'; e[11] = 0x10;
    TEST_ASSERT_EQUAL(0, fw_fat_parse_dirent(e, &d));                              /* "." */
    /* A name with a byte that is not printable ASCII is shown with a question mark, never raw. */
    memset(e, ' ', 11); e[0] = 0xB5; e[1] = 'A'; e[11] = 0x20;
    TEST_ASSERT_EQUAL(1, fw_fat_parse_dirent(e, &d));
    TEST_ASSERT_EQUAL_STRING("?A", d.name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mbr);
    RUN_TEST(test_fat32_boot_sector);
    RUN_TEST(test_fat16_and_exfat);
    RUN_TEST(test_free_count_and_chain);
    RUN_TEST(test_directory_entries);
    return UNITY_END();
}
