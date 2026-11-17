/* test_dump_fmt.c: the eMMC read-out's record lines, on the host. The golden line here is also in
 * tools/tests/test_emmc_dump.py, which parses it with the PC tool: the two ends agree or a test fails. */
#include <string.h>

#include "../../fw/main/dump_fmt.c"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_base64_rfc4648_vectors(void)
{
    static const struct { const char *in, *out; } v[] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"}};
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        char out[16];
        size_t n = fw_b64_encode((const uint8_t *)v[i].in, strlen(v[i].in), out);
        out[n] = '\0';
        TEST_ASSERT_EQUAL_STRING(v[i].out, out);
    }
    const uint8_t hi[3] = {0xFB, 0xFF, 0xFE};
    char out[8];
    out[fw_b64_encode(hi, 3, out)] = '\0';
    TEST_ASSERT_EQUAL_STRING("+//+", out);
}

static void test_record_golden(void)
{
    /* One sector whose byte i is (i * 7 + 3) & 0xFF, at LBA 0x00012345. */
    uint8_t sec[512];
    for (int i = 0; i < 512; i++)
        sec[i] = (uint8_t)(i * 7 + 3);
    static char line[FW_DUMP_LINE_MAX];
    uint32_t crc = 0;
    size_t n = fw_dump_record(line, sizeof line, 0x00012345u, 1, sec, &crc);
    TEST_ASSERT_EQUAL(7 + 8 + 1 + 2 + 1 + 8 + 1 + 684 + 1, n);
    TEST_ASSERT_EQUAL('\n', line[n - 1]);
    TEST_ASSERT_EQUAL(0, strncmp(line, "#CC7 D 00012345 01 ", 19));
    /* The CRC is zlib's CRC-32 over 45 23 01 00 and then the sector; the value is the one Python's
       zlib gives (tools/tests/test_emmc_dump.py computes it independently). */
    TEST_ASSERT_EQUAL_HEX32(fw_dump_crc(0x00012345u, sec, 512), crc);
    char want[9];
    static const char H[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++)
        want[i] = H[(crc >> (28 - 4 * i)) & 15];
    want[8] = '\0';
    TEST_ASSERT_EQUAL(0, strncmp(line + 19, want, 8));
    TEST_ASSERT_EQUAL(' ', line[27]);
    /* The first twelve data bytes are 03 0a 11 18 1f 26 2d 34 3b 42 49 50. */
    TEST_ASSERT_EQUAL(0, strncmp(line + 28, "AwoRGB8mLTQ7QklQ", 16));
    TEST_ASSERT_EQUAL_HEX32(0xA6154B91u, crc);

    /* A different address with the same data has a different CRC. */
    uint32_t crc2 = 0;
    fw_dump_record(line, sizeof line, 0x00012346u, 1, sec, &crc2);
    TEST_ASSERT_NOT_EQUAL(crc, crc2);
}

static void test_record_limits(void)
{
    static uint8_t data[FW_DUMP_MAX_SECTORS * 512];
    static char line[FW_DUMP_LINE_MAX];
    memset(data, 0xA5, sizeof data);
    TEST_ASSERT_EQUAL(0, fw_dump_record(line, sizeof line, 0, 0, data, NULL));
    TEST_ASSERT_EQUAL(0, fw_dump_record(line, sizeof line, 0, FW_DUMP_MAX_SECTORS + 1, data, NULL));
    size_t n = fw_dump_record(line, sizeof line, 0xFFFFFFFFu, FW_DUMP_MAX_SECTORS, data, NULL);
    TEST_ASSERT_TRUE(n > 0 && n < FW_DUMP_LINE_MAX);
    TEST_ASSERT_EQUAL(strlen(line), n);
    TEST_ASSERT_EQUAL(0, strncmp(line, "#CC7 D ffffffff 08 ", 19));
    /* One byte too little room is refused, not overrun. */
    TEST_ASSERT_EQUAL(0, fw_dump_record(line, n, 0, FW_DUMP_MAX_SECTORS, data, NULL));
    TEST_ASSERT_EQUAL(n, fw_dump_record(line, n + 1, 0, FW_DUMP_MAX_SECTORS, data, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_base64_rfc4648_vectors);
    RUN_TEST(test_record_golden);
    RUN_TEST(test_record_limits);
    return UNITY_END();
}
