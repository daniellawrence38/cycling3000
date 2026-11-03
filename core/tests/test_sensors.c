/* V§6.1 to V§6.4 and the brief 5.1 rules for averages, staleness and loss. */
#include "cc700/sensors.h"
#include "support.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_cps_parse(void)
{
    static const struct { const char *hex; int power, has_crank, revs, time; } rows[] = {
        {"20 00 c8 00 0a 00 00 04", 200, 1, 10, 1024},
        {"21 00 c8 00 32 0a 00 00 04", 200, 1, 10, 1024},
        {"30 00 c8 00 01 00 00 00 02 00 0a 00 00 04", 200, 1, 10, 1024},
        {"34 00 c8 00 07 00 01 00 00 00 02 00 0a 00 00 04", 200, 1, 10, 1024},
        {"00 00 c8 00", 200, 0, 0, 0},
        {"00 00 fb ff", -5, 0, 0, 0},
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        uint8_t b[32];
        size_t n = hex_to_bytes(rows[i].hex, b, sizeof b);
        cc_cps_t c;
        TEST_ASSERT_TRUE(cc_cps_parse(b, n, &c));
        TEST_ASSERT_EQUAL_INT(rows[i].power, c.power);
        TEST_ASSERT_EQUAL_INT(rows[i].has_crank, c.has_crank);
        if (rows[i].has_crank) {
            TEST_ASSERT_EQUAL_UINT16(rows[i].revs, c.crank_revs);
            TEST_ASSERT_EQUAL_UINT16(rows[i].time, c.crank_time);
        }
        /* Truncated input never reads past the end */
        for (size_t k = 0; k < n; k++)
            if (cc_cps_parse(b, k, &c))
                TEST_ASSERT_TRUE(k >= 4);
    }
}

static void test_cadence(void)
{
    int32_t rpm = -1;
    TEST_ASSERT_TRUE(cc_cadence_rpm(10, 1024, 13, 3072, &rpm));
    TEST_ASSERT_EQUAL_INT32(90, rpm);
    TEST_ASSERT_TRUE(cc_cadence_rpm(65535, 65000, 2, 1000, &rpm));
    TEST_ASSERT_EQUAL_INT32(120, rpm);
    TEST_ASSERT_TRUE(cc_cadence_rpm(100, 5000, 101, 5683, &rpm));
    TEST_ASSERT_EQUAL_INT32(90, rpm);
    TEST_ASSERT_FALSE(cc_cadence_rpm(100, 5000, 100, 5000, &rpm));
    TEST_ASSERT_TRUE(cc_cadence_rpm(0, 0, 1, 400, &rpm));
    TEST_ASSERT_EQUAL_INT32(154, rpm);
}

static void test_hr_parse(void)
{
    static const struct { const char *hex; int bpm; } rows[] = {
        {"00 48", 72}, {"00 ff", 255}, {"01 b4 00", 180}, {"01 2c 01", 300}};
    for (unsigned i = 0; i < 4; i++) {
        uint8_t b[4];
        uint16_t bpm;
        size_t n = hex_to_bytes(rows[i].hex, b, sizeof b);
        TEST_ASSERT_TRUE(cc_hr_parse(b, n, &bpm));
        TEST_ASSERT_EQUAL_UINT16(rows[i].bpm, bpm);
        TEST_ASSERT_FALSE(cc_hr_parse(b, n - 1, &bpm));
    }
}

static void test_bins_v64(void)
{
    TEST_ASSERT_EQUAL_INT32(200, cc_bin_mean(200, 1));
    TEST_ASSERT_EQUAL_INT32(202, cc_bin_mean(403, 2));
    TEST_ASSERT_EQUAL_INT32(200, cc_bin_mean(399, 2));
    TEST_ASSERT_EQUAL_INT32(0, cc_bin_mean(0, 2));
    TEST_ASSERT_EQUAL_INT32(252, cc_bin_mean(757, 3));

    cc_chan_t c;
    int32_t v;
    cc_chan_init(&c, 3000);
    cc_chan_second(&c, 1000);
    TEST_ASSERT_FALSE(cc_chan_last(&c, &v)); /* nothing yet */
    cc_chan_sample(&c, 1100, 251);
    cc_chan_sample(&c, 1400, 252);
    cc_chan_sample(&c, 1900, 254);
    cc_chan_second(&c, 2000);
    TEST_ASSERT_TRUE(cc_chan_last(&c, &v));
    TEST_ASSERT_EQUAL_INT32(252, v);
}

static void test_stale_and_lost(void)
{
    cc_chan_t c;
    int32_t v;
    cc_chan_init(&c, 3000);
    cc_chan_sample(&c, 900, 200);
    cc_chan_second(&c, 1000);                      /* bin 200 */
    cc_chan_second(&c, 2000);                      /* empty, last sample 1100 ms old: carried */
    TEST_ASSERT_TRUE(cc_chan_last(&c, &v));
    TEST_ASSERT_EQUAL_INT32(200, v);
    cc_chan_second(&c, 2900);                      /* 2000 ms old: still carried */
    TEST_ASSERT_TRUE(cc_chan_last(&c, &v));
    cc_chan_second(&c, 2901);                      /* 2001 ms old: invalid */
    TEST_ASSERT_FALSE(cc_chan_last(&c, &v));
    TEST_ASSERT_FALSE(cc_chan_lost(&c, 3899));
    TEST_ASSERT_TRUE(cc_chan_lost(&c, 3900));
    cc_chan_sample(&c, 4000, 0);                   /* recovers; a valid zero is a number */
    TEST_ASSERT_FALSE(cc_chan_lost(&c, 4000));
    cc_chan_second(&c, 4001);
    TEST_ASSERT_TRUE(cc_chan_last(&c, &v));
    TEST_ASSERT_EQUAL_INT32(0, v);
}

static void test_averages(void)
{
    cc_chan_t c;
    int32_t v;
    cc_chan_init(&c, 3000);
    TEST_ASSERT_FALSE(cc_chan_avg(&c, 3, &v));
    uint32_t t = 0;
    int32_t vals[12] = {100, 200, 300, 0, 0, 0, 400, 400, 401, 10, 20, 30};
    for (int i = 0; i < 12; i++) {
        t += 1000;
        cc_chan_sample(&c, t - 500, vals[i]);
        cc_chan_second(&c, t);
    }
    TEST_ASSERT_TRUE(cc_chan_avg(&c, 3, &v));
    TEST_ASSERT_EQUAL_INT32(20, v);
    TEST_ASSERT_TRUE(cc_chan_avg(&c, 10, &v));
    /* last 10: 300 0 0 0 400 400 401 10 20 30 = 1561 / 10 = 156.1 */
    TEST_ASSERT_EQUAL_INT32(156, v);

    /* Invalid bins are skipped; none valid gives invalid */
    cc_chan_init(&c, 3000);
    cc_chan_sample(&c, 500, 100);
    cc_chan_second(&c, 1000);
    cc_chan_second(&c, 4000); /* invalid */
    cc_chan_second(&c, 5000); /* invalid */
    TEST_ASSERT_TRUE(cc_chan_avg(&c, 3, &v));
    TEST_ASSERT_EQUAL_INT32(100, v);
    cc_chan_second(&c, 6000);
    TEST_ASSERT_FALSE(cc_chan_avg(&c, 3, &v));
}

static void test_sensors_cadence_rules(void)
{
    cc_sensors_t s;
    int32_t rpm;
    cc_sensors_init(&s);
    TEST_ASSERT_FALSE(cc_sensors_cadence(&s, 0, &rpm));
    cc_sensors_power(&s, 100, 200, 1, 10, 1024);
    TEST_ASSERT_FALSE(cc_sensors_cadence(&s, 150, &rpm)); /* one crank sample, no difference */
    cc_sensors_power(&s, 1100, 200, 1, 13, 3072);
    TEST_ASSERT_TRUE(cc_sensors_cadence(&s, 1200, &rpm));
    TEST_ASSERT_EQUAL_INT32(90, rpm);
    /* Notifications keep arriving with no new crank event: hold, then 0 after 3 s */
    cc_sensors_power(&s, 2100, 0, 1, 13, 3072);
    cc_sensors_power(&s, 3100, 0, 1, 13, 3072);
    TEST_ASSERT_TRUE(cc_sensors_cadence(&s, 4099, &rpm));
    TEST_ASSERT_EQUAL_INT32(90, rpm);
    cc_sensors_power(&s, 4100, 0, 1, 13, 3072);
    TEST_ASSERT_TRUE(cc_sensors_cadence(&s, 4100, &rpm));
    TEST_ASSERT_EQUAL_INT32(0, rpm);
    /* Power lost: cadence invalid */
    TEST_ASSERT_FALSE(cc_sensors_cadence(&s, 7100, &rpm));
    /* Negative power clamped to 0 */
    cc_sensors_init(&s);
    cc_sensors_power(&s, 100, -5, 0, 0, 0);
    cc_sensors_second(&s, 1000);
    int32_t v;
    TEST_ASSERT_TRUE(cc_chan_last(&s.power, &v));
    TEST_ASSERT_EQUAL_INT32(0, v);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cps_parse);
    RUN_TEST(test_cadence);
    RUN_TEST(test_hr_parse);
    RUN_TEST(test_bins_v64);
    RUN_TEST(test_stale_and_lost);
    RUN_TEST(test_averages);
    RUN_TEST(test_sensors_cadence_rules);
    return UNITY_END();
}
