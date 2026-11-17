/* Brief 5.9: two-slot settings and sensor bonds, including a power cut at every byte of a save. */
#include <string.h>

#include "cc700/settings.h"
#include "memfs.h"
#include "unity.h"

static hal_fs_t *fs;
void setUp(void) { fs = memfs_new(); }
void tearDown(void) { memfs_free(fs); }

static void test_defaults_when_empty(void)
{
    cc_settings_t s;
    cc_slots_t sl;
    TEST_ASSERT_EQUAL_INT(0, cc_settings_load(fs, &s, &sl));
    TEST_ASSERT_EQUAL_UINT16(200, s.ftp);
    TEST_ASSERT_EQUAL_INT(-1, sl.newest);
    TEST_ASSERT_EQUAL_INT(0, cc_settings_save(fs, &s, &sl));
    TEST_ASSERT_TRUE(memfs_exists(fs, CC_SETTINGS_A)); /* first save goes to slot a */
    TEST_ASSERT_FALSE(memfs_exists(fs, CC_SETTINGS_B));
}

static void test_alternates_and_newest_wins(void)
{
    cc_settings_t s, r;
    cc_slots_t sl;
    cc_settings_load(fs, &s, &sl);
    for (int i = 0; i < 7; i++) {
        s.ftp = (uint16_t)(250 + i);
        TEST_ASSERT_EQUAL_INT(0, cc_settings_save(fs, &s, &sl));
        TEST_ASSERT_EQUAL_INT(i % 2, sl.newest);
        cc_slots_t l2;
        TEST_ASSERT_EQUAL_INT(1, cc_settings_load(fs, &r, &l2));
        TEST_ASSERT_EQUAL_UINT16(250 + i, r.ftp);
    }
}

static void test_seq_wraps(void)
{
    uint8_t a[64], b[64];
    cc_settings_t s, r;
    cc_slots_t sl;
    cc_settings_defaults(&s);
    s.ftp = 111;
    cc_settings_encode(&s, 0xFFFFFFFFu, a);
    s.ftp = 222;
    cc_settings_encode(&s, 0u, b); /* wrapped: newer */
    memfs_put(fs, CC_SETTINGS_A, a, 39);
    memfs_put(fs, CC_SETTINGS_B, b, 39);
    TEST_ASSERT_EQUAL_INT(1, cc_settings_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT16(222, r.ftp);
    TEST_ASSERT_EQUAL_INT(1, sl.newest);
}

static void test_corrupt_both_keeps_files(void)
{
    uint8_t junk[39] = {'C', 'S', 2};
    cc_settings_t r;
    cc_slots_t sl;
    memfs_put(fs, CC_SETTINGS_A, junk, 39);
    memfs_put(fs, CC_SETTINGS_B, junk, 10);
    TEST_ASSERT_EQUAL_INT(0, cc_settings_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT16(200, r.ftp);
    TEST_ASSERT_EQUAL_INT64(39, memfs_file_size(fs, CC_SETTINGS_A)); /* left for diagnosis */
    TEST_ASSERT_EQUAL_INT64(10, memfs_file_size(fs, CC_SETTINGS_B));
    r.ftp = 300;
    TEST_ASSERT_EQUAL_INT(0, cc_settings_save(fs, &r, &sl));
    TEST_ASSERT_EQUAL_INT64(10, memfs_file_size(fs, CC_SETTINGS_B)); /* went to slot a */
    TEST_ASSERT_EQUAL_INT(1, cc_settings_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT16(300, r.ftp);
}

static void test_power_cut_every_byte(void)
{
    /* Two saves already made (slots a and b both valid), then a third save cut at every byte. */
    for (int cut = 0; cut <= 40; cut++) {
        memfs_reset(fs);
        cc_settings_t s, r;
        cc_slots_t sl;
        cc_settings_load(fs, &s, &sl);
        s.ftp = 240; cc_settings_save(fs, &s, &sl);
        s.ftp = 250; cc_settings_save(fs, &s, &sl);
        s.ftp = 260;
        memfs_cut_after_bytes(fs, cut);
        int rc = cc_settings_save(fs, &s, &sl);
        memfs_power_cycle(fs);
        cc_slots_t l2;
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, cc_settings_load(fs, &r, &l2), "nothing loadable");
        if (cut < 39) {
            TEST_ASSERT_NOT_EQUAL(0, rc);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(250, r.ftp, "previous settings lost");
        } else {
            TEST_ASSERT_EQUAL_UINT16(260, r.ftp);
        }
    }
    /* And the very first save cut at every byte: defaults or the new record, never a crash */
    for (int cut = 0; cut <= 40; cut++) {
        memfs_reset(fs);
        cc_settings_t s, r;
        cc_slots_t sl;
        cc_settings_load(fs, &s, &sl);
        s.ftp = 260;
        memfs_cut_after_bytes(fs, cut);
        cc_settings_save(fs, &s, &sl);
        memfs_power_cycle(fs);
        int got = cc_settings_load(fs, &r, &sl);
        TEST_ASSERT_EQUAL_UINT16(got ? 260 : 200, r.ftp);
        TEST_ASSERT_EQUAL_INT(cut >= 39, got);
    }
}

static void test_bonds(void)
{
    cc_bonds_t b, r;
    cc_slots_t sl;
    memset(&b, 0, sizeof b);
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
    b.count = 2;
    b.b[0] = (cc_bond_t){1, 0, {1, 2, 3, 4, 5, 6}, "PM500 A1B2"};
    b.b[1] = (cc_bond_t){2, 1, {9, 8, 7, 6, 5, 4}, "HRM"};
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_save(fs, &b, &sl));
    b.count = 1; /* forget the strap */
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_save(fs, &b, &sl));
    TEST_ASSERT_EQUAL_INT(1, cc_bonds_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_STRING("PM500 A1B2", r.b[0].name);
    for (int cut = 0; cut < 30; cut++) {
        cc_slots_t s2;
        cc_bonds_load(fs, &r, &s2);
        b.count = 2;
        memfs_cut_after_bytes(fs, cut);
        cc_bonds_save(fs, &b, &s2);
        memfs_power_cycle(fs);
        TEST_ASSERT_EQUAL_INT(1, cc_bonds_load(fs, &r, &s2));
        TEST_ASSERT_TRUE(r.count == 1 || r.count == 2);
        if (r.count == 2) { /* save completed: put it back to 1 for the next round */
            b.count = 1;
            cc_bonds_save(fs, &b, &s2);
        }
    }
}

/* One sensor of each kind: put replaces the same kind, remove forgets it, and the table survives a
 * save and load with the HR strap's random address type intact. */
static void test_bonds_one_of_each_kind(void)
{
    cc_bonds_t b, r;
    cc_slots_t sl;
    memset(&b, 0, sizeof b);
    cc_bonds_load(fs, &r, &sl);
    TEST_ASSERT_NULL(cc_bonds_find(&b, 1));
    cc_bond_t pm = {1, 0, {0x2E, 0x7C, 3, 4, 5, 6}, "PM500"};
    cc_bond_t pm2 = {1, 0, {0xD1, 0x90, 3, 4, 5, 6}, "PM500"};
    cc_bond_t hr = {2, 1, {0xB9, 0x41, 7, 6, 5, 0xC4}, ""};
    memset(hr.name, 'H', sizeof hr.name); /* no terminator: put must cut it to CC_BOND_NAME */
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_put(&b, &pm));
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_put(&b, &hr));
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_put(&b, &pm2)); /* replaces, never a second power meter */
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    TEST_ASSERT_EQUAL_HEX8(0xD1, cc_bonds_find(&b, 1)->addr[0]);
    TEST_ASSERT_EQUAL_UINT(CC_BOND_NAME, strlen(cc_bonds_find(&b, 2)->name)); /* truncated to fit the file */
    cc_bond_t bad = {3, 0, {0}, "x"};
    TEST_ASSERT_EQUAL_INT(-1, cc_bonds_put(&b, &bad));
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_put(&b, cc_bonds_find(&b, 2))); /* aliasing its own entry */
    TEST_ASSERT_EQUAL_UINT8(2, b.count);
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_save(fs, &b, &sl));
    TEST_ASSERT_EQUAL_INT(1, cc_bonds_load(fs, &r, &sl));
    TEST_ASSERT_EQUAL_UINT8(1, cc_bonds_find(&r, 2)->addr_type);
    TEST_ASSERT_EQUAL_HEX8(0xC4, cc_bonds_find(&r, 2)->addr[5]);
    TEST_ASSERT_EQUAL_INT(1, cc_bonds_remove(&r, 1));
    TEST_ASSERT_EQUAL_INT(0, cc_bonds_remove(&r, 1));
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_NULL(cc_bonds_find(&r, 1));
    TEST_ASSERT_NOT_NULL(cc_bonds_find(&r, 2));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bonds_one_of_each_kind);
    RUN_TEST(test_defaults_when_empty);
    RUN_TEST(test_alternates_and_newest_wins);
    RUN_TEST(test_seq_wraps);
    RUN_TEST(test_corrupt_both_keeps_files);
    RUN_TEST(test_power_cut_every_byte);
    RUN_TEST(test_bonds);
    return UNITY_END();
}
