/* The heart-rate graph's 5-minute history (hrhist.h): the ring on its own (wrap-around, gaps, clamping,
 * statistics) and inside the core (recorded while recording and paused, not before START, reset at
 * the next START, gaps where the strap is silent). */
#include <string.h>

#include "cc700/core.h"
#include "cc700/hrhist.h"
#include "memfs.h"
#include "unity.h"

static hal_fs_t *fs;
static uint32_t now;

void setUp(void)
{
    fs = memfs_new();
    now = 1000000;
    cc_init(fs, now);
}
void tearDown(void) { memfs_free(fs); }

/* ------------------------------------------------------------------ the ring */
static void test_empty(void)
{
    cc_hrhist_t h;
    cc_hrhist_reset(&h);
    TEST_ASSERT_EQUAL_INT(0, h.n);
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, 0));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, -1));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, CC_HRHIST_N));
    cc_hrhist_stats_t st;
    TEST_ASSERT_EQUAL_INT(0, cc_hrhist_stats(&h, &st));
    TEST_ASSERT_EQUAL_INT(0, st.min);
    TEST_ASSERT_EQUAL_INT(0, st.max);
    TEST_ASSERT_EQUAL_INT(0, st.avg);
}

static void test_order_and_gaps(void)
{
    cc_hrhist_t h;
    cc_hrhist_reset(&h);
    cc_hrhist_push(&h, 1, 120);
    cc_hrhist_push(&h, 0, 999); /* not ok: a gap whatever the value */
    cc_hrhist_push(&h, 1, 0);   /* 0 bpm is no reading, never a value */
    cc_hrhist_push(&h, 1, -5);
    cc_hrhist_push(&h, 1, 131);
    TEST_ASSERT_EQUAL_INT(5, h.n);
    TEST_ASSERT_EQUAL_INT(131, cc_hrhist_at(&h, 0));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, 1));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, 2));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, 3));
    TEST_ASSERT_EQUAL_INT(120, cc_hrhist_at(&h, 4));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, 5)); /* not recorded yet */
    cc_hrhist_stats_t st;
    TEST_ASSERT_EQUAL_INT(2, cc_hrhist_stats(&h, &st));
    TEST_ASSERT_EQUAL_INT(120, st.min);
    TEST_ASSERT_EQUAL_INT(131, st.max);
    TEST_ASSERT_EQUAL_INT(126, st.avg); /* 125.5 rounds half up */
}

static void test_clamp(void)
{
    cc_hrhist_t h;
    cc_hrhist_reset(&h);
    cc_hrhist_push(&h, 1, 1);
    cc_hrhist_push(&h, 1, 254);
    cc_hrhist_push(&h, 1, 255);
    cc_hrhist_push(&h, 1, 70000);
    TEST_ASSERT_EQUAL_INT(254, cc_hrhist_at(&h, 0));
    TEST_ASSERT_EQUAL_INT(254, cc_hrhist_at(&h, 1));
    TEST_ASSERT_EQUAL_INT(254, cc_hrhist_at(&h, 2));
    TEST_ASSERT_EQUAL_INT(1, cc_hrhist_at(&h, 3));
}

static void test_wrap_around(void)
{
    cc_hrhist_t h;
    cc_hrhist_reset(&h);
    /* 2.5 windows: value = 60 + second number % 150, and every 7th second a gap */
    int total = CC_HRHIST_N * 5 / 2 + 3;
    for (int i = 0; i < total; i++)
        cc_hrhist_push(&h, i % 7 != 0, 60 + i % 150);
    TEST_ASSERT_EQUAL_INT(CC_HRHIST_N, h.n);
    int64_t sum = 0;
    int nv = 0, mn = 1000, mx = 0;
    for (int a = 0; a < CC_HRHIST_N; a++) {
        int i = total - 1 - a;
        int want = i % 7 != 0 ? 60 + i % 150 : -1;
        TEST_ASSERT_EQUAL_INT_MESSAGE(want, cc_hrhist_at(&h, a), "age");
        if (want > 0) { sum += want; nv++; if (want < mn) mn = want; if (want > mx) mx = want; }
    }
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, CC_HRHIST_N)); /* older than the window */
    cc_hrhist_stats_t st;
    TEST_ASSERT_EQUAL_INT(nv, cc_hrhist_stats(&h, &st));
    TEST_ASSERT_EQUAL_INT(mn, st.min);
    TEST_ASSERT_EQUAL_INT(mx, st.max);
    TEST_ASSERT_EQUAL_INT((int)((sum * 2 + nv) / (2 * nv)), st.avg);
    /* Exactly one full window: the oldest entry is the first pushed after the wrap */
    cc_hrhist_reset(&h);
    for (int i = 0; i < CC_HRHIST_N; i++) cc_hrhist_push(&h, 1, 100 + i % 50);
    TEST_ASSERT_EQUAL_INT(100, cc_hrhist_at(&h, CC_HRHIST_N - 1));
    cc_hrhist_push(&h, 1, 200);
    TEST_ASSERT_EQUAL_INT(101, cc_hrhist_at(&h, CC_HRHIST_N - 1));
    TEST_ASSERT_EQUAL_INT(200, cc_hrhist_at(&h, 0));
}

static void test_all_gaps(void)
{
    cc_hrhist_t h;
    cc_hrhist_reset(&h);
    for (int i = 0; i < CC_HRHIST_N + 10; i++) cc_hrhist_push(&h, 0, 0);
    cc_hrhist_stats_t st;
    TEST_ASSERT_EQUAL_INT(0, cc_hrhist_stats(&h, &st));
    for (int a = 0; a < CC_HRHIST_N; a++) TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(&h, a));
}

/* ------------------------------------------------------------------ in the core */
static void tick_to(uint32_t t)
{
    while (now < t) {
        now += 100;
        cc_tick(now);
    }
}

/* One second with a reading half-way through it (bpm > 0), or with none. */
static void sec_hr(int bpm)
{
    tick_to(now + 500);
    if (bpm > 0) cc_on_hr_sample(now, bpm);
    tick_to(now + 500);
}

static void press(cc_btn_t b, uint32_t ms)
{
    cc_on_button(now, b, 1);
    tick_to(now + ms);
    cc_on_button(now, b, 0);
    cc_tick(now);
}

static const cc_hrhist_t *H(void) { return &cc_view()->hr_hist; }

static void test_core_records_ride(void)
{
    tick_to(now + 1000);
    for (int i = 0; i < 5; i++) sec_hr(100);
    TEST_ASSERT_EQUAL_INT(0, H()->n); /* nothing before START */

    TEST_ASSERT_EQUAL_INT(0, cc_cmd_start(now));
    cc_tick(now);
    for (int i = 0; i < 10; i++) sec_hr(120);
    /* The strap is silent for 4 s. The first empty second repeats the last bin (V§6.4: the last
       sample is under CC_POWER_STALE_MS old), exactly as the FIT record does; the other three are gaps. */
    for (int i = 0; i < 4; i++) sec_hr(0);
    for (int i = 0; i < 5; i++) sec_hr(130);
    int n = H()->n;
    TEST_ASSERT_TRUE(n >= 18 && n <= 20); /* the second START fell in may or may not have a bin */
    TEST_ASSERT_EQUAL_INT(130, cc_hrhist_at(H(), 0));
    TEST_ASSERT_EQUAL_INT(130, cc_hrhist_at(H(), 4));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(H(), 5));
    TEST_ASSERT_EQUAL_INT(-1, cc_hrhist_at(H(), 7));
    TEST_ASSERT_EQUAL_INT(120, cc_hrhist_at(H(), 8)); /* the carried second */
    TEST_ASSERT_EQUAL_INT(120, cc_hrhist_at(H(), 18));
    /* The newest entry is the value the HEART RATE field shows */
    TEST_ASSERT_TRUE(cc_view()->hr_ok);
    TEST_ASSERT_EQUAL_INT(cc_view()->hr, cc_hrhist_at(H(), 0));

    /* Paused (hold right): still recorded, wall seconds */
    press(CC_BTN_R, 1100);
    TEST_ASSERT_EQUAL_INT(CC_RS_PAUSED, cc_view()->state);
    int before = H()->n;
    for (int i = 0; i < 6; i++) sec_hr(98);
    TEST_ASSERT_EQUAL_INT(before + 6, H()->n);
    TEST_ASSERT_EQUAL_INT(98, cc_hrhist_at(H(), 0));
    /* The pause menu counts as paused too */
    press(CC_BTN_L, 100);
    TEST_ASSERT_EQUAL_INT(CC_RS_PAUSE_MENU, cc_view()->state);
    sec_hr(97);
    TEST_ASSERT_EQUAL_INT(97, cc_hrhist_at(H(), 0));
    TEST_ASSERT_EQUAL_INT(before + 7, H()->n);

    /* Finish, back to Ready: no longer recorded */
    cc_cmd_pause_menu_select(now, CC_PM_FINISH);
    cc_tick(now);
    TEST_ASSERT_EQUAL_INT(CC_RS_SUMMARY, cc_view()->state);
    int after = H()->n;
    sec_hr(90);
    cc_cmd_summary_done();
    cc_tick(now);
    sec_hr(90);
    TEST_ASSERT_EQUAL_INT(after, H()->n);

    /* The next START begins an empty window */
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_start(now));
    cc_tick(now);
    TEST_ASSERT_TRUE(H()->n <= 1);
    sec_hr(140);
    TEST_ASSERT_EQUAL_INT(140, cc_hrhist_at(H(), 0));
    TEST_ASSERT_TRUE(H()->n <= 2);
}

static void test_core_window_slides(void)
{
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_start(now));
    cc_tick(now);
    for (int i = 0; i < CC_HRHIST_N + 60; i++) sec_hr(100 + (i % 40));
    TEST_ASSERT_EQUAL_INT(CC_HRHIST_N, H()->n);
    /* the last value fed was 100 + (359 % 40) = 139 */
    TEST_ASSERT_EQUAL_INT(139, cc_hrhist_at(H(), 0));
    TEST_ASSERT_EQUAL_INT(100 + ((CC_HRHIST_N + 59 - 299) % 40), cc_hrhist_at(H(), CC_HRHIST_N - 1));
    cc_hrhist_stats_t st;
    TEST_ASSERT_EQUAL_INT(CC_HRHIST_N, cc_hrhist_stats(H(), &st));
    TEST_ASSERT_EQUAL_INT(100, st.min);
    TEST_ASSERT_EQUAL_INT(139, st.max);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty);
    RUN_TEST(test_order_and_gaps);
    RUN_TEST(test_clamp);
    RUN_TEST(test_wrap_around);
    RUN_TEST(test_all_gaps);
    RUN_TEST(test_core_records_ride);
    RUN_TEST(test_core_window_slides);
    return UNITY_END();
}
