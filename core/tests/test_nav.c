/* V§9 route object, and the route follower over a replayed ride (tools/gen_ride route_ride.csv). */
#include <stdio.h>
#include <string.h>

#include "cc700/nav.h"
#include "cc700/util.h"
#include "player.h"
#include "support.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static cc_route_t rt;
static cc_nav_t nav;

static const char *V9 =
    "43 52 01 00 99 ed 92 63 a8 98 d4 34 d1 08 00 00 11 00 06 00 02 00 01 17 00 64 00 09 54 65 73 74"
    "20 6c 6f 6f 70 40 ae f2 ec 60 41 0a 45 a0 9c 01 a0 9c 01 c0 b8 02 e0 d4 03 a0 9c 01 c0 a9 07 9f"
    "8d 06 c0 b8 02 9f 8d 06 bf b8 02 c8 00 d0 00 db 00 e0 00 e8 00 f0 00 fb 00 00 01 08 01 10 01 1b"
    "01 20 01 28 01 30 01 3b 01 40 01 48 01 50 01 5b 01 60 01 68 01 70 01 7b 01 fa 00 00 00 05 00 0a"
    "46 72 61 73 65 72 20 41 76 65 08 02 00 00 02 00 00 64 00 00 00 2c 01 12 00 3c 00 55 00";

static void test_v9_decode(void)
{
    uint8_t b[200];
    size_t n = hex_to_bytes(V9, b, sizeof b);
    TEST_ASSERT_EQUAL_size_t(157, n);
    TEST_ASSERT_EQUAL_HEX32(0x34340AE7u, cc_crc32(b, n));
    TEST_ASSERT_EQUAL_INT(CC_CR1_OK, cc_cr1_decode(b, n, &rt));
    static const uint8_t id[8] = {0x99, 0xed, 0x92, 0x63, 0xa8, 0x98, 0xd4, 0x34};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(id, rt.id, 8);
    TEST_ASSERT_EQUAL_STRING("Test loop", rt.name);
    TEST_ASSERT_EQUAL_UINT32(2257, rt.dist_m);
    TEST_ASSERT_EQUAL_UINT16(17, rt.ascent_m);
    TEST_ASSERT_EQUAL_UINT16(6, rt.npts);
    static const int32_t lat[6] = {-319640000, -319630000, -319610000, -319600000, -319650000, -319700000};
    static const int32_t lon[6] = {1158300000, 1158310000, 1158340000, 1158400000, 1158420000, 1158400000};
    TEST_ASSERT_EQUAL_INT32_ARRAY(lat, rt.lat, 6);
    TEST_ASSERT_EQUAL_INT32_ARRAY(lon, rt.lon, 6);
    TEST_ASSERT_EQUAL_UINT16(23, rt.nprof);
    TEST_ASSERT_EQUAL_UINT16(100, rt.prof_step);
    TEST_ASSERT_EQUAL_INT16(200, rt.prof[0]);
    TEST_ASSERT_EQUAL_INT16(219, rt.prof[2]);
    TEST_ASSERT_EQUAL_INT16(379, rt.prof[22]);
    TEST_ASSERT_EQUAL_UINT16(2, rt.nturns);
    TEST_ASSERT_EQUAL_UINT32(250, rt.turns[0].dist_m);
    TEST_ASSERT_EQUAL_UINT8(5, rt.turns[0].type);
    TEST_ASSERT_EQUAL_STRING("Fraser Ave", rt.turns[0].street);
    TEST_ASSERT_EQUAL_UINT32(520, rt.turns[1].dist_m);
    TEST_ASSERT_EQUAL_UINT8(2, rt.turns[1].type);
    TEST_ASSERT_EQUAL_STRING("", rt.turns[1].street);
    TEST_ASSERT_EQUAL_UINT8(1, rt.nclimbs);
    TEST_ASSERT_EQUAL_UINT32(100, rt.climbs[0].start_m);
    TEST_ASSERT_EQUAL_UINT16(300, rt.climbs[0].length_m);
    TEST_ASSERT_EQUAL_UINT16(18, rt.climbs[0].ascent_m);
    TEST_ASSERT_EQUAL_INT16(60, rt.climbs[0].avg_x10);
    TEST_ASSERT_EQUAL_INT16(85, rt.climbs[0].max_x10);

    /* Delta coding check: (+10000, +10000) -> a0 9c 01 a0 9c 01 */
    uint8_t v[10];
    TEST_ASSERT_EQUAL_size_t(3, cc_varint_put(v, cc_zigzag(10000)));
    TEST_ASSERT_EQUAL_HEX8(0xa0, v[0]);
    TEST_ASSERT_EQUAL_HEX8(0x9c, v[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, v[2]);
    TEST_ASSERT_EQUAL_UINT64(1, cc_zigzag(-1));
    TEST_ASSERT_EQUAL_UINT64(3, cc_zigzag(-2));

    /* Re-encoding gives the same bytes */
    uint8_t again[200];
    size_t m = cc_cr1_encode(rt.name, rt.lat, rt.lon, rt.npts, rt.dist_m, rt.ascent_m, rt.prof, rt.nprof, rt.prof_step,
                             rt.turns, rt.nturns, rt.climbs, rt.nclimbs, again, sizeof again);
    TEST_ASSERT_EQUAL_size_t(n, m);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, again, n);

    /* Corruption and truncation are refused */
    for (size_t k = 0; k < n; k++)
        TEST_ASSERT_NOT_EQUAL(CC_CR1_OK, cc_cr1_decode(b, k, &rt));
    b[30] ^= 1;
    TEST_ASSERT_NOT_EQUAL(CC_CR1_OK, cc_cr1_decode(b, n, &rt));
}

static void test_turn_text(void)
{
    char s[24];
    cc_fmt_turn_dist(s, sizeof s, 1234); TEST_ASSERT_EQUAL_STRING("1.2 km", s);
    cc_fmt_turn_dist(s, sizeof s, 1000); TEST_ASSERT_EQUAL_STRING("1.0 km", s);
    cc_fmt_turn_dist(s, sizeof s, 954);  TEST_ASSERT_EQUAL_STRING("950 m", s);
    cc_fmt_turn_dist(s, sizeof s, 955);  TEST_ASSERT_EQUAL_STRING("960 m", s);
    cc_fmt_turn_dist(s, sizeof s, 100);  TEST_ASSERT_EQUAL_STRING("100 m", s);
    cc_fmt_turn_dist(s, sizeof s, 97);   TEST_ASSERT_EQUAL_STRING("95 m", s);
    cc_fmt_turn_dist(s, sizeof s, 98);   TEST_ASSERT_EQUAL_STRING("100 m", s);
    cc_fmt_turn_dist(s, sizeof s, 3);    TEST_ASSERT_EQUAL_STRING("5 m", s);
}

/* ------------------------------------------------------------------ replay */
static uint8_t obj[CC_RT_MAX_BYTES];

static void load_fixture_route(void)
{
    FILE *f = fopen("fixtures/route.cr1", "rb");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(obj, 1, sizeof obj, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(CC_CR1_OK, cc_cr1_decode(obj, n, &rt));
    cc_nav_load(&nav, &rt);
}

static float baro_for(int t)
{
    /* The test supplies the barometric ascent by hand: 5 % of the distance climbed on leg B */
    if (t < 240) return 0;
    if (t < 360) return 0.05f * 5.0f * (float)(t - 240);
    return 30.0f;
}

static void test_replay(void)
{
    fx_t fx;
    load_fixture_route();
    TEST_ASSERT_EQUAL_INT(0, fx_load(&fx, "fixtures/route_ride.csv"));
    int off_start[4], off_end[4], ns = 0, ne = 0, ahead = 0, ahead_t = -1;
    int turn_at[600], nt = 0, last_turn = -2;
    for (int i = 0; i < fx.n; i++) {
        const fx_row_t *r = &fx.rows[i];
        unsigned ev = cc_nav_second(&nav, r->fix, r->lat, r->lon, (uint32_t)r->speed_mms, baro_for(r->t));
        cc_nav_view_t v;
        cc_nav_view(&nav, baro_for(r->t), &v);
        if (ev & CC_NAV_EV_OFF_START) off_start[ns++] = r->t;
        if (ev & CC_NAV_EV_OFF_END) off_end[ne++] = r->t;
        if (ev & CC_NAV_EV_CLIMB_AHEAD) { ahead++; ahead_t = r->t; }
        turn_at[r->t] = v.turn_idx;
        if (v.turn_idx != last_turn) {
            nt++;
            last_turn = v.turn_idx;
        }
        if (r->t == 10) {
            TEST_ASSERT_EQUAL_STRING("Turn left \xC2\xB7 Fraser Ave", v.instr);
            TEST_ASSERT_EQUAL_UINT32(950, v.dist_to_turn_m);
        }
        if (r->t == 110) {
            TEST_ASSERT_FLOAT_WITHIN(0.5f, 57.5f, v.dist_to_route_m); /* 5 m/s south for 11 s, plus 2.5 m */
            TEST_ASSERT_FLOAT_WITHIN(1.0f, 500.0f, v.route_pos_m);         /* held while off the line */
        }
        if (r->t == 200) {
            /* 800 m along; 2397 m route: 1597 m left at 5 m/s mean moving speed: 319 s */
            TEST_ASSERT_UINT32_WITHIN(2, 1597, v.dist_left_m);
            TEST_ASSERT_TRUE(v.eta_valid);
            TEST_ASSERT_UINT32_WITHIN(1, 319, v.eta_s);
            TEST_ASSERT_EQUAL_UINT32(200, v.dist_to_turn_m);
            TEST_ASSERT_EQUAL_UINT32(800, v.map_scale_m); /* 1.5 x 200 m above the rider: 758 m wide -> 800 */
            TEST_ASSERT_TRUE(v.climb_show);
            TEST_ASSERT_TRUE(v.approaching);
            TEST_ASSERT_EQUAL_UINT32(200, v.climb_to_start_m);
        }
        if (r->t == 300) {
            /* on the climb at 1300 m: 300 m left; ascent left 30 - 15 = 15 m */
            TEST_ASSERT_TRUE(v.on_climb);
            TEST_ASSERT_UINT32_WITHIN(1, 300, v.climb_dist_left_m);
            TEST_ASSERT_UINT32_WITHIN(1, 15, v.climb_ascent_left_m);
            TEST_ASSERT_EQUAL_STRING("0.6 km \xC2\xB7 30 m climb", v.climb_summary);
        }
        if (r->t == 400) {
            TEST_ASSERT_FALSE(v.climb_show); /* no climbs left */
        }
        if (r->t == 430) {
            TEST_ASSERT_TRUE(v.off_route);
            TEST_ASSERT_UINT32_WITHIN(1, 60, (uint32_t)v.dist_to_route_m);
        }
    }
    fx_free(&fx);
    /* By hand (gen_ride route_ride docstring):
     * leaving south from t 100 at 5 m/s (7.5 m at t 100): beyond 40 m from t 107 (42.5 m), 8 s -> OFF
     * at t 114; coming back: within 25 m from t 135 (22.5 m), 3 s -> back at t 137;
     * detour 60 m north from t 420: beyond 40 m at once, 8 s -> OFF at t 427;
     * back on leg C at t 480 (0 m): 3 s -> back at t 482. */
    TEST_ASSERT_EQUAL_INT(2, ns);
    TEST_ASSERT_EQUAL_INT(2, ne);
    TEST_ASSERT_EQUAL_INT(114, off_start[0]);
    TEST_ASSERT_EQUAL_INT(137, off_end[0]);
    TEST_ASSERT_EQUAL_INT(427, off_start[1]);
    TEST_ASSERT_EQUAL_INT(482, off_end[1]);
    /* Climb ahead once, when the route position first reaches 500 m before the climb at 1000 m: at
     * t 100 (500 m) or, if rounding leaves it a hair short, the next on-route second (t 140/141). */
    TEST_ASSERT_EQUAL_INT(1, ahead);
    TEST_ASSERT_TRUE(ahead_t == 100 || ahead_t == 140 || ahead_t == 141);
    /* Turns: the next turn is the first with dist_m > route_pos - 5. Depart (0) until 5 m, the left at
     * 1000 m until 1005 m, the right at 1600 m until 1605 m, then arrive. At 5 m/s those switch points
     * fall exactly on t 1, 241 and 361, where the 1e-7 degree rounding decides, so the test checks the
     * seconds either side. Exactly four distinct turns are shown, in order. */
    TEST_ASSERT_EQUAL_INT(4, nt);
    TEST_ASSERT_EQUAL_INT(0, turn_at[0]);
    TEST_ASSERT_EQUAL_INT(1, turn_at[2]);
    TEST_ASSERT_EQUAL_INT(1, turn_at[240]);
    TEST_ASSERT_EQUAL_INT(2, turn_at[242]);
    TEST_ASSERT_EQUAL_INT(2, turn_at[360]);
    TEST_ASSERT_EQUAL_INT(3, turn_at[362]);
    TEST_ASSERT_EQUAL_INT(3, turn_at[519]);
}

static void test_join_mid_climb(void)
{
    load_fixture_route();
    /* First fix at 1300 m on leg B: the profile says 15 m are already climbed */
    int32_t lat = rt.lat[0] + (int32_t)(300.0 / (1e-7 * 111320.0)) + 0, lon;
    (void)lat;
    /* leg B points: index 20 is (1000, 0); index 26 is (1000, 300) */
    lat = rt.lat[26];
    lon = rt.lon[26];
    for (int s = 0; s < 3; s++)
        cc_nav_second(&nav, 1, lat, lon, 5000, 100.0f + (float)s * 0);
    cc_nav_view_t v;
    cc_nav_view(&nav, 100.0f, &v);
    TEST_ASSERT_TRUE(v.on_climb);
    TEST_ASSERT_UINT32_WITHIN(1, 15, v.climb_ascent_left_m);
    /* 4 m more barometric ascent */
    cc_nav_view(&nav, 104.0f, &v);
    TEST_ASSERT_UINT32_WITHIN(1, 11, v.climb_ascent_left_m);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_v9_decode);
    RUN_TEST(test_turn_text);
    RUN_TEST(test_replay);
    RUN_TEST(test_join_mid_climb);
    return UNITY_END();
}
