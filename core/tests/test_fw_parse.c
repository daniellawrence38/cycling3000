/* test_fw_parse.c: the firmware's IDF-free pieces, on the host: sensor characteristic parsing, and the
 * core running on the null storage the skeleton uses until the real storage is known. */
#include <string.h>

#include "../../fw/main/ble_parse.c"
#include "../../fw/main/fs_null.c"
#include "cc700/core.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_cp_measurement(void)
{
    fw_cp_meas_t m;
    const uint8_t plain[] = {0x00, 0x00, 0xFA, 0x00}; /* 250 W */
    TEST_ASSERT_EQUAL(0, fw_parse_cp_measurement(plain, sizeof plain, &m));
    TEST_ASSERT_EQUAL(250, m.watts);
    TEST_ASSERT_FALSE(m.has_crank);
    /* balance + crank: flags 0x0021, 300 W, balance 100, revs 0x0102, time 0x0400 */
    const uint8_t crank[] = {0x21, 0x00, 0x2C, 0x01, 100, 0x02, 0x01, 0x00, 0x04};
    TEST_ASSERT_EQUAL(0, fw_parse_cp_measurement(crank, sizeof crank, &m));
    TEST_ASSERT_EQUAL(300, m.watts);
    TEST_ASSERT_TRUE(m.has_crank);
    TEST_ASSERT_EQUAL(0x0102, m.crank_revs);
    TEST_ASSERT_EQUAL(0x0400, m.crank_time);
    /* torque + wheel + crank: 2 + 6 bytes before the crank fields */
    const uint8_t all[] = {0x34, 0x00, 0xFF, 0xFF, 1, 2, 1, 2, 3, 4, 5, 6, 0x10, 0x00, 0x20, 0x00};
    TEST_ASSERT_EQUAL(0, fw_parse_cp_measurement(all, sizeof all, &m));
    TEST_ASSERT_EQUAL(-1, m.watts);
    TEST_ASSERT_EQUAL(0x10, m.crank_revs);
    TEST_ASSERT_EQUAL(0x20, m.crank_time);
    TEST_ASSERT_EQUAL(-1, fw_parse_cp_measurement(crank, 7, &m));
    TEST_ASSERT_EQUAL(-1, fw_parse_cp_measurement(plain, 3, &m));
}

static void test_hr_and_feature(void)
{
    const uint8_t u8[] = {0x00, 142};
    const uint8_t u16[] = {0x01, 0x2C, 0x01};
    TEST_ASSERT_EQUAL(142, fw_parse_hr_measurement(u8, 2));
    TEST_ASSERT_EQUAL(300, fw_parse_hr_measurement(u16, 3));
    TEST_ASSERT_EQUAL(-1, fw_parse_hr_measurement(u16, 2));
    uint32_t f = 0;
    const uint8_t feat[] = {0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL(0, fw_parse_cp_feature(feat, 4, &f));
    TEST_ASSERT_EQUAL_HEX32(1u << 9, f);
}

static void test_core_runs_on_null_storage(void)
{
    cc_init(fw_fs_null(), 1000);
    for (uint32_t t = 1000; t < 20000; t += 100)
        cc_tick(t);
    const cc_view_t *v = cc_view();
    TEST_ASSERT_EQUAL(CC_RS_READY, v->state);
    TEST_ASSERT_EQUAL(200, v->ftp); /* defaults: nothing could be loaded */
    cc_link_host_t h;
    cc_link_host_bind(&h);
    TEST_ASSERT_EQUAL(0, cc_objects_count(fw_fs_null(), 1) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cp_measurement);
    RUN_TEST(test_hr_and_feature);
    RUN_TEST(test_core_runs_on_null_storage);
    return UNITY_END();
}
