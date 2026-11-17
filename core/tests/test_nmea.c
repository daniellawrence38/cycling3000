/* test_nmea.c: host unit tests for the GNSS NMEA 0183 parser (fw/main/nmea.c). */
#include <string.h>

#include "../../fw/main/nmea.c"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* Perth (Kings Park) GGA + RMC pair at 2026-11-17 01:30:00 UTC (1794879000). */
static const char GGA_PERTH[] =
    "$GNGGA,013000.00,3157.6300,S,11550.5200,E,1,12,0.8,48.5,M,-30.0,M,,*73\r\n";
static const char RMC_PERTH[] =
    "$GNRMC,013000.00,A,3157.6300,S,11550.5200,E,10.00,182.5,171126,,,A*50\r\n";
/* Void fix that still carries valid UTC date and time (spec 10.6: UTC before a position fix). */
static const char RMC_VOID[] =
    "$GNRMC,013005.00,V,,,,,,,171126,,,N*66\r\n";

static void test_valid_gga_and_rmc_produce_fix(void)
{
    fw_nmea_t st;
    hal_gnss_fix_t fix;
    fw_nmea_init(&st);
    TEST_ASSERT_EQUAL(0, fw_nmea_line(&st, GGA_PERTH, &fix));
    TEST_ASSERT_EQUAL(1, fw_nmea_line(&st, RMC_PERTH, &fix));
    TEST_ASSERT_EQUAL(1, fix.valid);
    /* 31 deg 57.6300' S = -31.9605 deg = -319605000 e7; 115 deg 50.5200' E = 115.8420 deg = 1158420000 e7 */
    TEST_ASSERT_EQUAL_INT32(-319605000, fix.lat_e7);
    TEST_ASSERT_EQUAL_INT32(1158420000, fix.lon_e7);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.5f, fix.alt_m);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.1444f, fix.speed_mps); /* 10 knots */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 182.5f, fix.course_deg);
    TEST_ASSERT_EQUAL_UINT16(8, fix.hdop_x10);
    TEST_ASSERT_EQUAL_UINT8(12, fix.sats);
    TEST_ASSERT_EQUAL_UINT32(1794879000u, fix.utc_seconds);
}

static void test_void_rmc_reports_utc_without_fix(void)
{
    fw_nmea_t st;
    hal_gnss_fix_t fix;
    fw_nmea_init(&st);
    TEST_ASSERT_EQUAL(1, fw_nmea_line(&st, RMC_VOID, &fix));
    TEST_ASSERT_EQUAL(0, fix.valid);
    TEST_ASSERT_EQUAL_UINT32(1794879005u, fix.utc_seconds);
}

static void test_checksum_and_malformed_rejected(void)
{
    fw_nmea_t st;
    hal_gnss_fix_t fix;
    fw_nmea_init(&st);
    /* Corrupt one character or the checksum itself */
    TEST_ASSERT_EQUAL(-1, fw_nmea_line(&st, "$GNRMC,013000.00,A,3157.6300,S,11550.5200,E,10.00,182.5,171126,,,A*7D", &fix));
    TEST_ASSERT_EQUAL(-1, fw_nmea_line(&st, "$GNRMC,013000.00,A,3157.6300,S,11550.5200,E,10.00,182.5,171126,,,A", &fix));
    TEST_ASSERT_EQUAL(-1, fw_nmea_line(&st, "GNRMC,013000.00,A*00", &fix));
    /* Old/default RTC year (2023) is not accepted as UTC */
    static const char RMC_2023[] = "$GNRMC,122000.00,V,,,,,,,010623,,,N*64\r\n";
    TEST_ASSERT_EQUAL(1, fw_nmea_line(&st, RMC_2023, &fix));
    TEST_ASSERT_EQUAL_UINT32(0u, fix.utc_seconds);
}

static void test_byte_stream_with_noise_and_pair_lines(void)
{
    fw_nmea_t st;
    hal_gnss_fix_t fix;
    fw_nmea_init(&st);
    const char *stream =
        "garbage before start\r\n"
        "$PAIR001,470,0*37\r\n"
        "$GNGGA,013000.00,3157.6300,S,11550.5200,E,1,12,0.8,48.5,M,-30.0,M,,*73\r\n"
        "$GNRMC,013000.00,A,3157.6300,S,11550.5200,E,10.00,182.5,171126,,,A*50\r\n";
    int fixes = 0;
    for (const char *p = stream; *p; p++)
        fixes += fw_nmea_feed_byte(&st, *p, &fix);
    TEST_ASSERT_EQUAL(1, fixes);
    TEST_ASSERT_EQUAL(1, fix.valid);
    TEST_ASSERT_EQUAL_INT32(-319605000, fix.lat_e7);
    TEST_ASSERT_EQUAL_INT32(1158420000, fix.lon_e7);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_gga_and_rmc_produce_fix);
    RUN_TEST(test_void_rmc_reports_utc_without_fix);
    RUN_TEST(test_checksum_and_malformed_rejected);
    RUN_TEST(test_byte_stream_with_noise_and_pair_lines);
    return UNITY_END();
}
