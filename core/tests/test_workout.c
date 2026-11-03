/* V§2.1 to V§2.3 (decode byte for byte), V§4 as decode-time structure checks, V§7, V§7.1, engine. */
#include <stdio.h>
#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"
#include "cc700/workout.h"
#include "support.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const char *W1_HEX =
    "43 57 01 00 18 12 24 0e 18 09 c0 11 f0 0f 00 00"
    "0a 13 34 20 78 20 38 20 6d 69 6e 20 74 68 72 65"
    "73 68 6f 6c 64 58 02 f4 01 bc 02 01 00 00 00 07"
    "57 61 72 6d 20 75 70 e0 01 b6 03 e8 03 00 00 01"
    "04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2 01 26"
    "02 00 00 01 04 07 52 65 63 6f 76 65 72 e0 01 b6"
    "03 e8 03 00 00 02 04 09 54 68 72 65 73 68 6f 6c"
    "64 f0 00 c2 01 26 02 00 00 02 04 07 52 65 63 6f"
    "76 65 72 e0 01 b6 03 e8 03 00 00 03 04 09 54 68"
    "72 65 73 68 6f 6c 64 f0 00 c2 01 26 02 00 00 03"
    "04 07 52 65 63 6f 76 65 72 e0 01 b6 03 e8 03 00"
    "00 04 04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2"
    "01 26 02 00 00 04 04 07 52 65 63 6f 76 65 72 58"
    "02 26 02 90 01 01 00 00 00 09 43 6f 6f 6c 20 64"
    "6f 77 6e";

static const char *W2_HEX =
    "43 57 01 00 34 a5 60 4c 1d 73 98 5e 84 03 00 00"
    "05 11 43 61 64 65 6e 63 65 20 61 6e 64 20 74 65"
    "6d 70 6f 3c 00 c2 01 26 02 00 6e 00 00 07 53 70"
    "69 6e 20 75 70 2c 01 f8 02 48 03 00 55 01 02 05"
    "54 65 6d 70 6f 78 00 c2 01 26 02 00 00 01 02 07"
    "52 65 63 6f 76 65 72 2c 01 f8 02 48 03 00 55 02"
    "02 05 54 65 6d 70 6f 78 00 c2 01 26 02 00 00 02"
    "02 07 52 65 63 6f 76 65 72";

static uint8_t w1[300], w2[200];
static size_t w1n, w2n;
static cc_workout_t wo;

static void load_w1(void) { w1n = hex_to_bytes(W1_HEX, w1, sizeof w1); }
static void load_w2(void) { w2n = hex_to_bytes(W2_HEX, w2, sizeof w2); }

/* --------------------------------------------------------------- V§2.1, V§2.2 */
static void test_w1_decode(void)
{
    load_w1();
    TEST_ASSERT_EQUAL_size_t(227, w1n);
    TEST_ASSERT_EQUAL_HEX32(0x09201A8Eu, cc_crc32(w1, w1n));
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, cc_cw1_decode(w1, w1n, &wo));
    static const uint8_t id[8] = {0x18, 0x12, 0x24, 0x0e, 0x18, 0x09, 0xc0, 0x11};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(id, wo.id, 8);
    TEST_ASSERT_EQUAL_STRING("4 x 8 min threshold", wo.name);
    TEST_ASSERT_EQUAL_UINT32(4080, wo.total_secs);
    TEST_ASSERT_EQUAL_UINT8(10, wo.nsteps);

    /* label, secs, p0, p1, ramp, rep_idx, rep_total, W@260 lo hi, W@250 lo hi (at step start) */
    static const struct { const char *l; int secs, p0, p1, ramp, ri, rt, a0, a1, b0, b1; } rows[10] = {
        {"Warm up", 600, 500, 700, 1, 0, 0, 124, 137, 119, 131},
        {"Threshold", 480, 950, 1000, 0, 1, 4, 247, 260, 238, 250},
        {"Recover", 240, 450, 550, 0, 1, 4, 117, 143, 113, 138},
        {"Threshold", 480, 950, 1000, 0, 2, 4, 247, 260, 238, 250},
        {"Recover", 240, 450, 550, 0, 2, 4, 117, 143, 113, 138},
        {"Threshold", 480, 950, 1000, 0, 3, 4, 247, 260, 238, 250},
        {"Recover", 240, 450, 550, 0, 3, 4, 117, 143, 113, 138},
        {"Threshold", 480, 950, 1000, 0, 4, 4, 247, 260, 238, 250},
        {"Recover", 240, 450, 550, 0, 4, 4, 117, 143, 113, 138},
        {"Cool down", 600, 550, 400, 1, 0, 0, 136, 150, 131, 145},
    };
    for (int i = 0; i < 10; i++) {
        const cc_step_t *s = &wo.steps[i];
        int32_t lo, hi;
        TEST_ASSERT_EQUAL_STRING(rows[i].l, s->label);
        TEST_ASSERT_EQUAL_UINT16(rows[i].secs, s->secs);
        TEST_ASSERT_EQUAL_UINT16(rows[i].p0, s->p0);
        TEST_ASSERT_EQUAL_UINT16(rows[i].p1, s->p1);
        TEST_ASSERT_EQUAL_UINT8(rows[i].ramp, s->ramp);
        TEST_ASSERT_EQUAL_UINT8(0, s->rpm);
        TEST_ASSERT_EQUAL_UINT8(rows[i].ri, s->rep_idx);
        TEST_ASSERT_EQUAL_UINT8(rows[i].rt, s->rep_total);
        cc_step_range(s, 260, 0, &lo, &hi);
        TEST_ASSERT_EQUAL_INT32(rows[i].a0, lo);
        TEST_ASSERT_EQUAL_INT32(rows[i].a1, hi);
        cc_step_range(s, 250, 0, &lo, &hi);
        TEST_ASSERT_EQUAL_INT32(rows[i].b0, lo);
        TEST_ASSERT_EQUAL_INT32(rows[i].b1, hi);
    }
    TEST_ASSERT_EQUAL_INT(1, cc_step_target_zone(&wo.steps[0], 0));
    TEST_ASSERT_EQUAL_INT(2, cc_step_target_zone(&wo.steps[0], 600));
    TEST_ASSERT_EQUAL_INT(4, cc_step_target_zone(&wo.steps[1], 0));
    TEST_ASSERT_EQUAL_INT(1, cc_step_target_zone(&wo.steps[2], 0));

    /* Re-encoding gives the same bytes */
    uint8_t again[300];
    TEST_ASSERT_EQUAL_size_t(w1n, cc_cw1_encode(&wo, again, sizeof again));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(w1, again, w1n);
}

/* --------------------------------------------------------------- V§2.3 */
static void test_w2_decode(void)
{
    load_w2();
    TEST_ASSERT_EQUAL_size_t(121, w2n);
    TEST_ASSERT_EQUAL_HEX32(0xD18A6A5Au, cc_crc32(w2, w2n));
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, cc_cw1_decode(w2, w2n, &wo));
    static const uint8_t id[8] = {0x34, 0xa5, 0x60, 0x4c, 0x1d, 0x73, 0x98, 0x5e};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(id, wo.id, 8);
    TEST_ASSERT_EQUAL_STRING("Cadence and tempo", wo.name);
    TEST_ASSERT_EQUAL_UINT32(900, wo.total_secs);
    static const struct { const char *l; int secs, p0, p1, rpm, ri, rt; } rows[5] = {
        {"Spin up", 60, 450, 550, 110, 0, 0}, {"Tempo", 300, 760, 840, 85, 1, 2},
        {"Recover", 120, 450, 550, 0, 1, 2}, {"Tempo", 300, 760, 840, 85, 2, 2},
        {"Recover", 120, 450, 550, 0, 2, 2},
    };
    TEST_ASSERT_EQUAL_UINT8(5, wo.nsteps);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_STRING(rows[i].l, wo.steps[i].label);
        TEST_ASSERT_EQUAL_UINT16(rows[i].secs, wo.steps[i].secs);
        TEST_ASSERT_EQUAL_UINT16(rows[i].p0, wo.steps[i].p0);
        TEST_ASSERT_EQUAL_UINT16(rows[i].p1, wo.steps[i].p1);
        TEST_ASSERT_EQUAL_UINT8(0, wo.steps[i].ramp);
        TEST_ASSERT_EQUAL_UINT8(rows[i].rpm, wo.steps[i].rpm);
        TEST_ASSERT_EQUAL_UINT8(rows[i].ri, wo.steps[i].rep_idx);
        TEST_ASSERT_EQUAL_UINT8(rows[i].rt, wo.steps[i].rep_total);
    }
    uint8_t again[200];
    TEST_ASSERT_EQUAL_size_t(w2n, cc_cw1_encode(&wo, again, sizeof again));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(w2, again, w2n);
}

/* --------------------------------------------------------------- V§4 as structure checks */
static cc_workout_t base;

static void make_base(int nsteps)
{
    memset(&base, 0, sizeof base);
    strcpy(base.name, "t");
    base.nsteps = (uint8_t)nsteps;
    for (int i = 0; i < nsteps; i++) {
        cc_step_t *s = &base.steps[i];
        strcpy(s->label, "Steady");
        s->secs = 60;
        s->p0 = 475; /* single 50 % widened */
        s->p1 = 525;
    }
}

static cc_cw1_err_t roundtrip(void)
{
    static uint8_t buf[CC_WO_MAX_BYTES + 64];
    size_t n = cc_cw1_encode(&base, buf, sizeof buf);
    TEST_ASSERT_NOT_EQUAL(0, n);
    return cc_cw1_decode(buf, n, &wo);
}

static void test_accept_cases(void)
{
    make_base(1); base.steps[0].secs = 5; strcpy(base.steps[0].label, "a");
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* minimal */
    make_base(100);
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* exactly 100 steps */
    make_base(1); base.steps[0].p0 = 550; base.steps[0].p1 = 400; base.steps[0].ramp = 1;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* descending ramp */
    make_base(1); base.steps[0].p0 = 450; base.steps[0].p1 = 550;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* range step */
    make_base(1); base.steps[0].p0 = 527; base.steps[0].p1 = 583;          /* 55.5 % widened */
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* 0.1 % resolution */
    make_base(1); base.steps[0].p0 = 2850; base.steps[0].p1 = 3150;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* 300 %: p1 3150 decodes */
    TEST_ASSERT_EQUAL_UINT16(3150, wo.steps[0].p1);
    make_base(2); base.steps[0].rpm = 40; base.steps[1].rpm = 130;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* rpm at both limits */
    make_base(1); memset(base.name, 'x', 40); base.name[40] = 0;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* name of 40 */
    make_base(1); base.steps[0].secs = 43200;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* 12 h step */
    make_base(2); base.steps[0].secs = 43200; base.steps[1].secs = 43200;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* exactly 24 h */
    make_base(32);
    base.steps[0].rep_idx = 1; base.steps[0].rep_total = 2;
    base.steps[1].rep_idx = 2; base.steps[1].rep_total = 2;
    for (int i = 0; i < 30; i++) { base.steps[2 + i].rep_idx = (uint8_t)(i + 1); base.steps[2 + i].rep_total = 30; }
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* reps 2 and 30 */
    make_base(1); strcpy(base.steps[0].label, "VO2max 3");
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());                        /* digits and spaces */
}

static void test_reject_cases(void)
{
    make_base(1); base.steps[0].secs = 4;      TEST_ASSERT_EQUAL_INT(CC_CW1_SECS, roundtrip());
    make_base(1); base.steps[0].secs = 43201;  TEST_ASSERT_EQUAL_INT(CC_CW1_SECS, roundtrip());
    make_base(1); base.steps[0].p0 = 9;        TEST_ASSERT_EQUAL_INT(CC_CW1_PCT, roundtrip());
    make_base(1); base.steps[0].p1 = 3151;     TEST_ASSERT_EQUAL_INT(CC_CW1_PCT, roundtrip());
    make_base(1); base.steps[0].p0 = 600; base.steps[0].p1 = 500; TEST_ASSERT_EQUAL_INT(CC_CW1_PCT, roundtrip());
    make_base(1); base.steps[0].p0 = 500; base.steps[0].p1 = 500; TEST_ASSERT_EQUAL_INT(CC_CW1_PCT, roundtrip());
    make_base(1); base.steps[0].ramp = 1; base.steps[0].p0 = base.steps[0].p1 = 600;
    TEST_ASSERT_EQUAL_INT(CC_CW1_PCT, roundtrip());
    make_base(1); base.steps[0].rpm = 39;      TEST_ASSERT_EQUAL_INT(CC_CW1_RPM, roundtrip());
    make_base(1); base.steps[0].rpm = 131;     TEST_ASSERT_EQUAL_INT(CC_CW1_RPM, roundtrip());
    make_base(1); base.steps[0].label[0] = 0;  TEST_ASSERT_EQUAL_INT(CC_CW1_LABEL, roundtrip());
    make_base(1); memset(base.steps[0].label, 'x', 16); base.steps[0].label[16] = 0;
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, roundtrip());
    make_base(1); base.name[0] = 0;            TEST_ASSERT_EQUAL_INT(CC_CW1_NAME, roundtrip());
    make_base(3); base.steps[0].secs = base.steps[1].secs = base.steps[2].secs = 43200;
    TEST_ASSERT_EQUAL_INT(CC_CW1_TOTAL, roundtrip());

    /* Byte-level corruption of W1 */
    uint8_t b[300];
    load_w1();
    memcpy(b, w1, w1n); b[0] = 'X';           TEST_ASSERT_EQUAL_INT(CC_CW1_MAGIC, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[2] = 2;             TEST_ASSERT_EQUAL_INT(CC_CW1_VERSION, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[3] = 1;             TEST_ASSERT_EQUAL_INT(CC_CW1_FLAGS, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[16] = 0;            TEST_ASSERT_EQUAL_INT(CC_CW1_NSTEPS, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[16] = 101;          TEST_ASSERT_EQUAL_INT(CC_CW1_NSTEPS, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[17] = 41;           TEST_ASSERT_EQUAL_INT(CC_CW1_NAME, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[12] ^= 1;           TEST_ASSERT_EQUAL_INT(CC_CW1_TOTAL, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[4] ^= 1;            TEST_ASSERT_EQUAL_INT(CC_CW1_ID, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[100] ^= 1;          TEST_ASSERT_NOT_EQUAL(CC_CW1_OK, cc_cw1_decode(b, w1n, &wo));
    memcpy(b, w1, w1n); b[w1n] = 0;           TEST_ASSERT_EQUAL_INT(CC_CW1_TRAILING, cc_cw1_decode(b, w1n + 1, &wo));
    for (size_t n = 0; n < w1n; n++)
        TEST_ASSERT_NOT_EQUAL(CC_CW1_OK, cc_cw1_decode(w1, n, &wo));
    /* Reserved step flag bit set (first step's flags at offset 18+19+6) */
    memcpy(b, w1, w1n); b[18 + 19 + 6] |= 2;  TEST_ASSERT_EQUAL_INT(CC_CW1_FLAGS, cc_cw1_decode(b, w1n, &wo));
}

/* --------------------------------------------------------------- V§7 pill */
static void test_pill_v7(void)
{
    static const int32_t pw[14] = {255, 262, 263, 264, 258, 245, 244, 243, 250, 261, 245, 270, 271, 240};
    static const cc_pill_state_t st[14] = {CC_PILL_IN, CC_PILL_IN, CC_PILL_HIGH, CC_PILL_HIGH, CC_PILL_IN,
                                           CC_PILL_IN, CC_PILL_LOW, CC_PILL_LOW, CC_PILL_IN, CC_PILL_IN,
                                           CC_PILL_IN, CC_PILL_IN, CC_PILL_HIGH, CC_PILL_LOW};
    static const int32_t dl[14] = {0, 0, 3, 4, 0, 0, 3, 4, 0, 0, 0, 0, 11, 7};
    cc_pill_t p;
    cc_pill_reset(&p);
    for (int t = 0; t < 14; t++) {
        int32_t d;
        TEST_ASSERT_EQUAL_INT_MESSAGE(st[t], cc_pill_feed(&p, 247, 260, pw[t], &d), "state");
        TEST_ASSERT_EQUAL_INT32_MESSAGE(dl[t], d, "delta");
    }
}

/* Engine on a single-step workout, so elapsed in step == timer */
static void one_step(uint16_t secs, int32_t ftp_for_range_247_260)
{
    memset(&base, 0, sizeof base);
    strcpy(base.name, "p");
    base.nsteps = 2;
    strcpy(base.steps[0].label, "Threshold");
    base.steps[0].secs = secs;
    base.steps[0].p0 = 950;
    base.steps[0].p1 = 1000;
    strcpy(base.steps[1].label, "Recover");
    base.steps[1].secs = 240;
    base.steps[1].p0 = 450;
    base.steps[1].p1 = 550;
    (void)ftp_for_range_247_260;
}

/* --------------------------------------------------------------- V§7.1 */
static void test_pill_v71(void)
{
    static cc_wo_engine_t e;
    static const int32_t pw[10] = {150, 190, 230, 252, 262, 264, 265, 258, 245, 244};
    static const cc_pill_state_t st[10] = {CC_PILL_HIDDEN, CC_PILL_HIDDEN, CC_PILL_HIDDEN, CC_PILL_HIDDEN,
                                           CC_PILL_HIDDEN, CC_PILL_IN, CC_PILL_HIGH, CC_PILL_IN, CC_PILL_IN,
                                           CC_PILL_LOW};
    static const int32_t dl[10] = {0, 0, 0, 0, 0, 0, 5, 0, 0, 3};
    one_step(480, 260);
    cc_wo_engine_load(&e, &base, 260);
    for (uint32_t t = 0; t < 10; t++) {
        cc_wo_engine_second(&e, t, 1, pw[t]);
        TEST_ASSERT_EQUAL_INT32(247, e.view.lo_w);
        TEST_ASSERT_EQUAL_INT32(260, e.view.hi_w);
        TEST_ASSERT_EQUAL_INT_MESSAGE(st[t], e.view.pill, "pill");
        TEST_ASSERT_EQUAL_INT32(dl[t], e.view.pill_delta);
    }

    /* 30 s step: hidden throughout */
    one_step(30, 260);
    cc_wo_engine_load(&e, &base, 260);
    for (uint32_t t = 0; t < 30; t++) {
        cc_wo_engine_second(&e, t, 1, t % 2 ? 200 : 330);
        TEST_ASSERT_EQUAL_INT(CC_PILL_HIDDEN, e.view.pill);
        TEST_ASSERT_EQUAL_INT(0, e.view.nextstep_banner);
    }

    /* 60 s step follows the 480 s rules */
    one_step(60, 260);
    cc_wo_engine_load(&e, &base, 260);
    for (uint32_t t = 0; t < 5; t++) {
        cc_wo_engine_second(&e, t, 1, 255);
        TEST_ASSERT_EQUAL_INT(CC_PILL_HIDDEN, e.view.pill);
    }
    cc_wo_engine_second(&e, 5, 1, 255);
    TEST_ASSERT_EQUAL_INT(CC_PILL_IN, e.view.pill);

    /* Invalid 3 s power: no state, no actual zone ring */
    cc_wo_engine_second(&e, 6, 0, 0);
    TEST_ASSERT_EQUAL_INT(CC_PILL_HIDDEN, e.view.pill);
    TEST_ASSERT_EQUAL_INT(0, e.view.actual_zone);
}

/* --------------------------------------------------------------- engine over W1 */
static void test_engine_w1(void)
{
    static cc_wo_engine_t e;
    load_w1();
    TEST_ASSERT_EQUAL_INT(CC_CW1_OK, cc_cw1_decode(w1, w1n, &wo));
    cc_wo_engine_load(&e, &wo, 260);
    int laps = 0, complete = 0, banners = 0;
    for (uint32_t t = 0; t <= 4100; t++) {
        unsigned ev = cc_wo_engine_second(&e, t, 1, 250);
        if (ev & CC_WO_EV_STEP_CHANGE) laps++;
        if (ev & CC_WO_EV_COMPLETE) complete++;
        if (e.view.nextstep_banner) banners++;
        if (t == 0) {
            TEST_ASSERT_EQUAL_INT(0, e.view.step);
            TEST_ASSERT_EQUAL_STRING("Warm up", e.view.label);
            TEST_ASSERT_EQUAL_INT32(124, e.view.lo_w);
            TEST_ASSERT_EQUAL_STRING("Threshold", e.view.next_label);
            TEST_ASSERT_EQUAL_INT32(247, e.view.next_lo_w);
            TEST_ASSERT_EQUAL_INT32(260, e.view.next_hi_w);
            TEST_ASSERT_EQUAL_UINT32(480, e.view.next_secs);
            TEST_ASSERT_EQUAL_INT(4, e.view.next_zone);
        }
        if (t == 300) {
            TEST_ASSERT_EQUAL_INT32(148, e.view.lo_w);
            TEST_ASSERT_EQUAL_INT32(164, e.view.hi_w);
            TEST_ASSERT_EQUAL_UINT32(300, e.view.remaining_s);
        }
        if (t == 595) TEST_ASSERT_EQUAL_INT(1, e.view.nextstep_banner);
        if (t == 594) TEST_ASSERT_EQUAL_INT(0, e.view.nextstep_banner);
        if (t == 600) {
            TEST_ASSERT_EQUAL_INT(1, e.view.step);
            TEST_ASSERT_EQUAL_INT(1, e.view.rep_idx);
            TEST_ASSERT_EQUAL_INT(4, e.view.rep_total);
            TEST_ASSERT_EQUAL_UINT32(480, e.view.remaining_s);
        }
        if (t == 3480 + 300) { /* cool-down at 300 s: floor division case */
            TEST_ASSERT_EQUAL_INT(9, e.view.step);
            TEST_ASSERT_EQUAL_INT32(117, e.view.lo_w);
            TEST_ASSERT_EQUAL_INT32(130, e.view.hi_w);
            TEST_ASSERT_EQUAL_INT(0, e.view.has_next);
        }
        if (t >= 4080) TEST_ASSERT_EQUAL_INT(1, e.view.complete);
    }
    TEST_ASSERT_EQUAL_INT(9, laps);
    TEST_ASSERT_EQUAL_INT(1, complete);
    TEST_ASSERT_EQUAL_INT(9 * 5, banners); /* every step but the last is 60 s or longer */

    /* FTP change recomputes */
    cc_wo_engine_load(&e, &wo, 260);
    cc_wo_engine_second(&e, 0, 1, 100);
    cc_wo_engine_second(&e, 600, 1, 240);
    cc_wo_engine_set_ftp(&e, 250);
    TEST_ASSERT_EQUAL_INT32(238, e.view.lo_w);
    TEST_ASSERT_EQUAL_INT32(250, e.view.hi_w);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_w1_decode);
    RUN_TEST(test_w2_decode);
    RUN_TEST(test_accept_cases);
    RUN_TEST(test_reject_cases);
    RUN_TEST(test_pill_v7);
    RUN_TEST(test_pill_v71);
    RUN_TEST(test_engine_w1);
    return UNITY_END();
}
