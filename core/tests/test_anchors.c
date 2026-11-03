/* V§1 anchors (WP0 done-condition). */
#include <string.h>

#include "cc700/util.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_crc32_123456789(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, cc_crc32("123456789", 9));
}

static void test_crc32_incremental(void)
{
    uint32_t c = cc_crc32_update(0, "1234", 4);
    c = cc_crc32_update(c, "56789", 5);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c);
}

static void test_sha256_abc(void)
{
    static const uint8_t want[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                     0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                     0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t out[32];
    cc_sha256("abc", 3, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32);
}

static void test_sha256_two_blocks(void)
{
    /* FIPS 180-2 example: 448-bit message */
    static const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const uint8_t want[8] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8};
    uint8_t out[32];
    cc_sha256(m, strlen(m), out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 8);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_123456789);
    RUN_TEST(test_crc32_incremental);
    RUN_TEST(test_sha256_abc);
    RUN_TEST(test_sha256_two_blocks);
    return UNITY_END();
}
