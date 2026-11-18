/* V§2.4, V§2.5 (including the floor-division table) and V§3. */
#include "cc700/util.h"
#include "cc700/zones.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

/* V§2.4: target %, p0, p1, W at 260, W at 200, W at 305 */
static const int32_t widen_rows[][9] = {
    {40, 380, 420, 99, 109, 76, 84, 116, 128},
    {55, 523, 578, 136, 150, 105, 116, 160, 176},
    {65, 618, 683, 161, 178, 124, 137, 188, 208},
    {75, 713, 788, 185, 205, 143, 158, 217, 240},
    {88, 836, 924, 217, 240, 167, 185, 255, 282},
    {100, 950, 1050, 247, 273, 190, 210, 290, 320},
    {105, 998, 1103, 259, 287, 200, 221, 304, 336},
    {120, 1140, 1260, 296, 328, 228, 252, 348, 384},
    {150, 1425, 1575, 371, 410, 285, 315, 435, 480},
    {300, 2850, 3150, 741, 819, 570, 630, 869, 961},
};

static void test_widen_and_watts(void)
{
    for (unsigned i = 0; i < sizeof widen_rows / sizeof widen_rows[0]; i++) {
        const int32_t *r = widen_rows[i];
        int32_t lo, hi;
        cc_widen(r[0] * 10, &lo, &hi);
        TEST_ASSERT_EQUAL_INT32(r[1], lo);
        TEST_ASSERT_EQUAL_INT32(r[2], hi);
        TEST_ASSERT_EQUAL_INT32(r[3], cc_watts(260, lo));
        TEST_ASSERT_EQUAL_INT32(r[4], cc_watts(260, hi));
        TEST_ASSERT_EQUAL_INT32(r[5], cc_watts(200, lo));
        TEST_ASSERT_EQUAL_INT32(r[6], cc_watts(200, hi));
        TEST_ASSERT_EQUAL_INT32(r[7], cc_watts(305, lo));
        TEST_ASSERT_EQUAL_INT32(r[8], cc_watts(305, hi));
    }
    TEST_ASSERT_EQUAL_INT32(297, cc_watts(305, 975)); /* 297.375 */
    TEST_ASSERT_EQUAL_INT32(238, cc_watts(250, 950)); /* 237.5 rounds up */
}

/* V§2.5: e, warm-up p, warm-up W lo, hi, cool-down p, cool-down W lo, hi (FTP 260) */
static const int32_t ramp_rows[][7] = {
    {0, 500, 124, 137, 550, 136, 150},
    {1, 500, 124, 137, 550, 136, 150},
    {150, 550, 136, 150, 513, 127, 140},
    {300, 600, 148, 164, 475, 117, 130},
    {450, 650, 161, 178, 438, 108, 120},
    {599, 700, 173, 191, 400, 99, 109},
    {600, 700, 173, 191, 400, 99, 109},
};

static void range_at(int32_t p, int32_t ftp, int32_t *lo_w, int32_t *hi_w)
{
    int32_t lo, hi;
    cc_widen(p, &lo, &hi);
    *lo_w = cc_watts(ftp, lo);
    *hi_w = cc_watts(ftp, hi);
}

static void test_ramp_targets(void)
{
    for (unsigned i = 0; i < sizeof ramp_rows / sizeof ramp_rows[0]; i++) {
        const int32_t *r = ramp_rows[i];
        int32_t lo, hi;
        int32_t pw = cc_ramp_p10(500, 700, 600, r[0]);
        int32_t pc = cc_ramp_p10(550, 400, 600, r[0]);
        TEST_ASSERT_EQUAL_INT32(r[1], pw);
        range_at(pw, 260, &lo, &hi);
        TEST_ASSERT_EQUAL_INT32(r[2], lo);
        TEST_ASSERT_EQUAL_INT32(r[3], hi);
        TEST_ASSERT_EQUAL_INT32(r[4], pc);
        range_at(pc, 260, &lo, &hi);
        TEST_ASSERT_EQUAL_INT32(r[5], lo);
        TEST_ASSERT_EQUAL_INT32(r[6], hi);
    }
    TEST_ASSERT_EQUAL_INT32(501, cc_ramp_p10(500, 700, 600, 2));
}

/* V§2.5 floor-division check: e, numerator, denominator, floor p, truncating p */
static void test_floor_division_table(void)
{
    static const int32_t rows[][5] = {
        {150, -44400, 1200, 513, 513},
        {300, -89400, 1200, 475, 476},
        {450, -134400, 1200, 438, 438},
        {599, -179100, 1200, 400, 401},
    };
    for (unsigned i = 0; i < 4; i++) {
        const int32_t *r = rows[i];
        TEST_ASSERT_EQUAL_INT32(r[1], (400 - 550) * r[0] * 2 + 600);
        TEST_ASSERT_EQUAL_INT32(r[3], 550 + (int32_t)cc_floordiv(r[1], r[2]));
        TEST_ASSERT_EQUAL_INT32(r[4], 550 + r[1] / r[2]); /* what a truncating port gets */
        TEST_ASSERT_EQUAL_INT32(r[3], cc_ramp_p10(550, 400, 600, r[0]));
    }
    TEST_ASSERT_EQUAL_INT64(-75, cc_floordiv(-89400, 1200));
    TEST_ASSERT_EQUAL_INT64(-1, cc_floordiv(-1, 2));
    TEST_ASSERT_EQUAL_INT64(0, cc_floordiv(1, 2));
    TEST_ASSERT_EQUAL_INT64(-1, cc_floordiv(1, -2));
    TEST_ASSERT_EQUAL_INT64(2, cc_floordiv(-4, -2));
}

/* V§3: watts, r, zone at FTP 260 */
static void test_zone_of_power(void)
{
    static const int32_t rows[][3] = {
        {0, 0, 1}, {100, 38, 1}, {142, 55, 1}, {143, 55, 1}, {144, 55, 1}, {145, 56, 2},
        {195, 75, 2}, {196, 75, 2}, {197, 76, 3}, {234, 90, 3}, {235, 90, 3}, {236, 91, 4},
        {273, 105, 4}, {274, 105, 4}, {275, 106, 5}, {312, 120, 5}, {313, 120, 5}, {314, 121, 6},
        {390, 150, 6}, {391, 150, 6}, {392, 151, 7}, {600, 231, 7},
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        TEST_ASSERT_EQUAL_INT32(rows[i][1], (int32_t)cc_floordiv(rows[i][0] * 200 + 260, 520));
        TEST_ASSERT_EQUAL_INT(rows[i][2], cc_band(rows[i][1]));
        TEST_ASSERT_EQUAL_INT(rows[i][2], cc_zone_of_power(rows[i][0], 260));
    }
}

static void test_target_zone_of_p10(void)
{
    TEST_ASSERT_EQUAL_INT(1, cc_zone_of_p10(500));  /* W1 warm-up at start */
    TEST_ASSERT_EQUAL_INT(4, cc_zone_of_p10(975));  /* threshold midpoint */
    TEST_ASSERT_EQUAL_INT(2, cc_zone_of_p10(700));  /* warm-up at end */
    TEST_ASSERT_EQUAL_INT(1, cc_zone_of_p10(554));  /* rounds to 55 */
    TEST_ASSERT_EQUAL_INT(2, cc_zone_of_p10(555));  /* rounds to 56 */
}

/* Heart-rate zones (QUESTIONS Q47): five zones of max HR, each starting at its exact percentage */
static void test_hr_zone_floors(void)
{
    /* max HR, then the first bpm of Z1..Z5: ceil(max * 50, 60, 70, 80, 90 %) */
    static const int32_t rows[][6] = {
        {185, 93, 111, 130, 148, 167},
        {190, 95, 114, 133, 152, 171},
        {187, 94, 113, 131, 150, 169},
        {100, 50, 60, 70, 80, 90},
        {230, 115, 138, 161, 184, 207},
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
        for (int z = 1; z <= CC_HR_ZONES; z++)
            TEST_ASSERT_EQUAL_INT32(rows[i][z], cc_hr_zone_floor(rows[i][0], z));
    TEST_ASSERT_EQUAL_INT32(0, cc_hr_zone_floor(0, 2));
    TEST_ASSERT_EQUAL_INT32(0, cc_hr_zone_floor(185, 0));
    TEST_ASSERT_EQUAL_INT32(0, cc_hr_zone_floor(185, 6));
}

static void test_zone_of_hr(void)
{
    /* bpm, zone at max HR 185: each boundary from both sides */
    static const int32_t rows[][2] = {
        {1, 1}, {60, 1}, {92, 1}, {93, 1}, {110, 1}, {111, 2}, {129, 2}, {130, 3}, {147, 3},
        {148, 4}, {166, 4}, {167, 5}, {185, 5}, {200, 5}, {254, 5},
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
        TEST_ASSERT_EQUAL_INT(rows[i][1], cc_zone_of_hr(rows[i][0], 185));
    /* zones off, or no reading */
    TEST_ASSERT_EQUAL_INT(0, cc_zone_of_hr(150, 0));
    TEST_ASSERT_EQUAL_INT(0, cc_zone_of_hr(0, 185));
    TEST_ASSERT_EQUAL_INT(0, cc_zone_of_hr(-5, 185));
    /* Every max HR the Profile accepts and every bpm a strap can send, against the definition in exact
       integers: zone z starts where 100 * bpm >= pct * max */
    for (int32_t mx = 100; mx <= 230; mx++)
        for (int32_t bpm = 1; bpm <= 255; bpm++) {
            int want = 1;
            for (int z = 2; z <= 5; z++)
                if (100 * bpm >= (40 + 10 * z) * mx) want = z;
            TEST_ASSERT_EQUAL_INT(want, cc_zone_of_hr(bpm, mx));
        }
}

static void test_hr_zone_colours(void)
{
    /* grey, blue, green, yellow, red: the power palette's Z1 to Z4 and Z6 */
    TEST_ASSERT_EQUAL_HEX32(cc_zone_rgb[1], cc_hr_zone_rgb[1]);
    TEST_ASSERT_EQUAL_HEX32(cc_zone_rgb[2], cc_hr_zone_rgb[2]);
    TEST_ASSERT_EQUAL_HEX32(cc_zone_rgb[3], cc_hr_zone_rgb[3]);
    TEST_ASSERT_EQUAL_HEX32(cc_zone_rgb[4], cc_hr_zone_rgb[4]);
    TEST_ASSERT_EQUAL_HEX32(cc_zone_rgb[6], cc_hr_zone_rgb[5]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hr_zone_floors);
    RUN_TEST(test_zone_of_hr);
    RUN_TEST(test_hr_zone_colours);
    RUN_TEST(test_widen_and_watts);
    RUN_TEST(test_ramp_targets);
    RUN_TEST(test_floor_division_table);
    RUN_TEST(test_zone_of_power);
    RUN_TEST(test_target_zone_of_p10);
    return UNITY_END();
}
