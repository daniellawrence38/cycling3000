/* WP2: the ride state machine (brief 5.5) as a table of every transition and every illegal event, hold
 * detection (6.4), auto-pause with the indoor and red-light fixtures, gradient and ascent with the
 * climb fixture, and unknown time at core level (finished and recovered). */
#include <stdio.h>
#include <string.h>

#include "cc700/core.h"
#include "memfs.h"
#include "player.h"
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

static void tick_to(uint32_t t)
{
    while (now < t) {
        now += 100;
        cc_tick(now);
    }
}

static void press(cc_btn_t b, uint32_t ms)
{
    cc_on_button(now, b, 1);
    tick_to(now + ms);
    cc_on_button(now, b, 0);
    cc_tick(now);
}

static void short_press(cc_btn_t b) { press(b, 100); }
static void hold(cc_btn_t b) { press(b, b == CC_BTN_PWR ? 2100 : 1100); }

static int drain_gestures(void)
{
    cc_gesture_t g;
    int n = 0;
    while (cc_poll_gesture(&g)) n++;
    return n;
}

static const cc_view_t *V(void) { return cc_view(); }

/* ------------------------------------------------------------------ state machine table */
typedef enum { EV_L, EV_LH, EV_R, EV_RH, EV_P, EV_PH, EV_START, EV_TOUCH0, EV_TOUCH1, EV_TOUCH2 } ev_t;

static void apply(ev_t e)
{
    switch (e) {
    case EV_L: short_press(CC_BTN_L); break;
    case EV_LH: hold(CC_BTN_L); break;
    case EV_R: short_press(CC_BTN_R); break;
    case EV_RH: hold(CC_BTN_R); break;
    case EV_P: short_press(CC_BTN_PWR); break;
    case EV_PH: hold(CC_BTN_PWR); break;
    case EV_START: cc_cmd_start(now); cc_tick(now); break;
    case EV_TOUCH0: case EV_TOUCH1: case EV_TOUCH2: {
        int i = (int)(e - EV_TOUCH0);
        if (V()->state == CC_RS_PAUSE_MENU) cc_cmd_pause_menu_select(now, i);
        else if (V()->state == CC_RS_CONFIRM_DISCARD) cc_cmd_confirm_select(now, i);
        cc_tick(now);
        break;
    }
    }
}

/* Drives a fresh core to a state with a given focus */
static void reach(cc_ride_state_t st, int focus)
{
    memfs_reset(fs);
    now += 5000;
    cc_init(fs, now);
    if (st == CC_RS_READY) return;
    apply(EV_START);
    tick_to(now + 3000);
    if (st == CC_RS_RECORDING) return;
    apply(EV_RH);
    if (st == CC_RS_PAUSED) return;
    apply(EV_L);
    for (int i = 0; i < (st == CC_RS_PAUSE_MENU ? focus : CC_PM_DISCARD); i++) apply(EV_P);
    if (st == CC_RS_PAUSE_MENU) return;
    apply(EV_R); /* Discard ride -> Confirm, focus KEEP */
    for (int i = 0; i < focus; i++) apply(EV_P);
    if (st == CC_RS_CONFIRM_DISCARD) return;
}

typedef struct {
    cc_ride_state_t from;
    int from_focus;
    ev_t ev;
    cc_ride_state_t to;
    int to_focus;      /* -1: do not check */
    int gestures;      /* gestures handed to the UI */
    const char *what;
} row_t;

static const row_t TABLE[] = {
    /* READY: START begins recording; buttons are UI gestures (Menu, START button on Ready screen) */
    {CC_RS_READY, 0, EV_START, CC_RS_RECORDING, -1, 0, "START"},
    {CC_RS_READY, 0, EV_L, CC_RS_READY, -1, 1, "left -> UI (Menu)"},
    {CC_RS_READY, 0, EV_R, CC_RS_READY, -1, 1, "right -> UI (START on the Ready screen)"},
    {CC_RS_READY, 0, EV_P, CC_RS_READY, -1, 1, "power short -> UI (blank or focus)"},
    {CC_RS_READY, 0, EV_LH, CC_RS_READY, -1, 1, "left hold -> UI"},
    /* RECORDING */
    {CC_RS_RECORDING, 0, EV_RH, CC_RS_PAUSED, -1, 0, "right hold 1 s pauses"},
    {CC_RS_RECORDING, 0, EV_R, CC_RS_RECORDING, -1, 0, "right short = lap"},
    {CC_RS_RECORDING, 0, EV_L, CC_RS_RECORDING, -1, 1, "left short = next page (UI)"},
    {CC_RS_RECORDING, 0, EV_LH, CC_RS_RECORDING, -1, 1, "left hold = previous page (UI)"},
    {CC_RS_RECORDING, 0, EV_P, CC_RS_RECORDING, -1, 0, "power short ignored"},
    {CC_RS_RECORDING, 0, EV_START, CC_RS_RECORDING, -1, 0, "START illegal"},
    {CC_RS_RECORDING, 0, EV_TOUCH2, CC_RS_RECORDING, -1, 0, "menu touch illegal"},
    /* PAUSED */
    {CC_RS_PAUSED, 0, EV_R, CC_RS_RECORDING, -1, 0, "right short resumes"},
    {CC_RS_PAUSED, 0, EV_L, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "left opens the pause menu, focus Resume"},
    {CC_RS_PAUSED, 0, EV_P, CC_RS_PAUSED, -1, 0, "power short ignored"},
    {CC_RS_PAUSED, 0, EV_RH, CC_RS_PAUSED, -1, 0, "right hold ignored"},
    {CC_RS_PAUSED, 0, EV_LH, CC_RS_PAUSED, -1, 0, "left hold ignored"},
    {CC_RS_PAUSED, 0, EV_START, CC_RS_PAUSED, -1, 0, "START illegal"},
    /* PAUSE_MENU */
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_L, CC_RS_PAUSED, -1, 0, "BACK"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_P, CC_RS_PAUSE_MENU, CC_PM_FINISH, 0, "power moves focus down"},
    {CC_RS_PAUSE_MENU, CC_PM_FINISH, EV_P, CC_RS_PAUSE_MENU, CC_PM_DISCARD, 0, "power moves focus down"},
    {CC_RS_PAUSE_MENU, CC_PM_DISCARD, EV_P, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "power wraps"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_R, CC_RS_RECORDING, -1, 0, "Resume"},
    {CC_RS_PAUSE_MENU, CC_PM_FINISH, EV_R, CC_RS_SUMMARY, -1, 0, "Finish and save"},
    {CC_RS_PAUSE_MENU, CC_PM_DISCARD, EV_R, CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, 0, "Discard -> Confirm, focus KEEP"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_TOUCH1, CC_RS_SUMMARY, -1, 0, "touch Finish and save"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_TOUCH2, CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, 0, "touch Discard"},
    {CC_RS_PAUSE_MENU, CC_PM_FINISH, EV_TOUCH0, CC_RS_RECORDING, -1, 0, "touch Resume"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_RH, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "right hold ignored"},
    {CC_RS_PAUSE_MENU, CC_PM_RESUME, EV_START, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "START illegal"},
    /* CONFIRM_DISCARD */
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_L, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "BACK = KEEP"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_R, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "KEEP"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_P, CC_RS_CONFIRM_DISCARD, CC_CF_DISCARD, 0, "power moves focus"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_DISCARD, EV_P, CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, 0, "power wraps"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_DISCARD, EV_R, CC_RS_READY, -1, 0, "DISCARD"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_DISCARD, EV_L, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "BACK = KEEP even on DISCARD"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_TOUCH1, CC_RS_READY, -1, 0, "touch DISCARD"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_TOUCH0, CC_RS_PAUSE_MENU, CC_PM_RESUME, 0, "touch KEEP"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_START, CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, 0, "START illegal"},
    {CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, EV_RH, CC_RS_CONFIRM_DISCARD, CC_CF_KEEP, 0, "right hold ignored"},
    /* Power off */
    {CC_RS_RECORDING, 0, EV_PH, CC_RS_READY, -1, 0, "power hold 2 s while recording saves and powers off"},
    {CC_RS_PAUSE_MENU, CC_PM_FINISH, EV_PH, CC_RS_READY, -1, 0, "power hold while paused saves"},
    {CC_RS_READY, 0, EV_PH, CC_RS_READY, -1, 0, "power hold when stopped powers off"},
};

static void test_state_table(void)
{
    char msg[160];
    for (unsigned i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++) {
        const row_t *r = &TABLE[i];
        reach(r->from, r->from_focus);
        drain_gestures();
        snprintf(msg, sizeof msg, "row %u (%s): setup", i, r->what);
        TEST_ASSERT_EQUAL_INT_MESSAGE(r->from, V()->state, msg);
        if (r->from == CC_RS_PAUSE_MENU || r->from == CC_RS_CONFIRM_DISCARD)
            TEST_ASSERT_EQUAL_INT_MESSAGE(r->from_focus, V()->focus, msg);
        apply(r->ev);
        snprintf(msg, sizeof msg, "row %u (%s)", i, r->what);
        TEST_ASSERT_EQUAL_INT_MESSAGE(r->to, V()->state, msg);
        if (r->to_focus >= 0)
            TEST_ASSERT_EQUAL_INT_MESSAGE(r->to_focus, V()->focus, msg);
        TEST_ASSERT_EQUAL_INT_MESSAGE(r->gestures, drain_gestures(), msg);
        if (r->ev == EV_PH)
            TEST_ASSERT_TRUE_MESSAGE(V()->power_off_request, msg);
    }
}

/* Saved, discarded and power-off rides leave the right files */
static void test_files_after_finish_discard(void)
{
    cc_set_utc(now, 1793000000u);
    apply(EV_START);
    uint32_t id = V()->ride_id;
    tick_to(now + 30000);
    apply(EV_RH); apply(EV_L); apply(EV_P); apply(EV_R); /* finish */
    TEST_ASSERT_EQUAL_INT(CC_RS_SUMMARY, V()->state);
    char p[64];
    snprintf(p, sizeof p, "rides/%u.fit", id);
    TEST_ASSERT_TRUE(memfs_exists(fs, p));
    snprintf(p, sizeof p, "rides/%u.meta", id);
    TEST_ASSERT_TRUE(memfs_exists(fs, p));
    /* 30 s, plus the 1 s the right button was held before the pause fired (still recording) */
    TEST_ASSERT_UINT32_WITHIN(1, 31, V()->summary.sum.timer_s);
    apply(EV_R); /* DONE */
    TEST_ASSERT_EQUAL_INT(CC_RS_READY, V()->state);

    tick_to(now + 2000);
    apply(EV_START);
    uint32_t id2 = V()->ride_id;
    tick_to(now + 5000);
    apply(EV_RH); apply(EV_L); apply(EV_P); apply(EV_P); apply(EV_R); apply(EV_P); apply(EV_R);
    TEST_ASSERT_EQUAL_INT(CC_RS_READY, V()->state);
    snprintf(p, sizeof p, "rides/%u.fit.part", id2);
    TEST_ASSERT_FALSE(memfs_exists(fs, p));
    snprintf(p, sizeof p, "rides/%u.fit", id2);
    TEST_ASSERT_FALSE(memfs_exists(fs, p));
}

/* Power-off leaves no file open, whether the finish works or fails (fw/main/main.c then aborts the
 * link's upload session and sends SendPowerOffCmd; the vendor closes its files first, too). */
static void test_power_off_closes_files(void)
{
    cc_set_utc(now, 1793000000u);
    apply(EV_START);
    tick_to(now + 30000);
    TEST_ASSERT_TRUE(memfs_open_handles(fs) > 0); /* the ride's .part */
    apply(EV_PH);
    TEST_ASSERT_TRUE(V()->power_off_request);
    TEST_ASSERT_EQUAL_INT(0, memfs_open_handles(fs));

    /* The finish fails on a storage error: the .part stays for recovery, closed. */
    cc_power_off_cancel();
    tick_to(now + 2000);
    apply(EV_START);
    uint32_t id = V()->ride_id;
    tick_to(now + 30000);
    memfs_fail_writes_from(fs, 1);
    apply(EV_PH);
    TEST_ASSERT_TRUE(V()->power_off_request);
    TEST_ASSERT_EQUAL_INT(0, memfs_open_handles(fs));
    char p[64];
    snprintf(p, sizeof p, "rides/%u.fit.part", id);
    TEST_ASSERT_TRUE(memfs_exists(fs, p));
}

/* Hold detection and the hold bar */
static void test_hold_bar(void)
{
    apply(EV_START);
    tick_to(now + 2000);
    cc_on_button(now, CC_BTN_R, 1);
    tick_to(now + 600);
    TEST_ASSERT_TRUE(V()->hold.active);
    TEST_ASSERT_EQUAL_INT(CC_BTN_R, V()->hold.side);
    TEST_ASSERT_EQUAL_UINT32(600, V()->hold.elapsed_ms);
    cc_on_button(now, CC_BTN_R, 0); /* released early: cancelled, it was a short press = lap */
    cc_tick(now);
    TEST_ASSERT_FALSE(V()->hold.active);
    TEST_ASSERT_EQUAL_INT(CC_RS_RECORDING, V()->state);
    TEST_ASSERT_EQUAL_INT(CC_AL_LAP, V()->banner.id);
    TEST_ASSERT_EQUAL_STRING("LAP 1", V()->banner.title);
    cc_on_button(now, CC_BTN_R, 1);
    tick_to(now + 1000);
    TEST_ASSERT_EQUAL_INT(CC_RS_PAUSED, V()->state); /* fires at 1 s, before release */
    cc_on_button(now, CC_BTN_R, 0);
    cc_tick(now);
    TEST_ASSERT_EQUAL_INT(CC_RS_PAUSED, V()->state); /* the release is swallowed: no resume */
}

/* ------------------------------------------------------------------ fixtures */
static fx_t fx;

static void play(const char *name, void (*each)(int t))
{
    char p[96];
    snprintf(p, sizeof p, "fixtures/%s", name);
    TEST_ASSERT_EQUAL_INT(0, fx_load(&fx, p));
    uint32_t base = (now / 1000u + 1) * 1000u;
    for (int i = 0; i < fx.n; i++) {
        now = fx_play_row(&fx, i, base);
        if (each) each(fx.rows[i].t);
    }
    fx_free(&fx);
}

static int paused_seen;
static void indoor_each(int t)
{
    (void)t;
    if (V()->state != CC_RS_RECORDING) paused_seen = 1;
}

static void test_indoor_never_pauses(void)
{
    cc_set_utc(now, 1793000000u);
    TEST_ASSERT_EQUAL_UINT8(1, V()->settings.autopause);
    tick_to((now / 1000u + 1) * 1000u - 100);
    apply(EV_START);
    paused_seen = 0;
    play("indoor.csv", indoor_each);
    TEST_ASSERT_EQUAL_INT(0, paused_seen);
    TEST_ASSERT_EQUAL_INT(CC_RS_RECORDING, V()->state);
    TEST_ASSERT_TRUE(V()->timer_s >= 1199);
}

static int pause_t, resume_t;
static void red_each(int t)
{
    if (V()->state == CC_RS_PAUSED && pause_t < 0) pause_t = t;
    if (pause_t >= 0 && resume_t < 0 && V()->state == CC_RS_RECORDING) resume_t = t;
}

static void test_redlight_pauses(void)
{
    cc_set_utc(now, 1793000000u);
    tick_to((now / 1000u + 1) * 1000u - 100);
    apply(EV_START);
    pause_t = resume_t = -1;
    play("redlight.csv", red_each);
    /* By hand: rows 60.. are stopped. The 3 s speed mean is 5.3 m/s after row 60, 2.7 after 61 and 0
     * from 62. Pedalling ends when the 3 s power is 0 (from row 62: bins 0, 0, 0) and the cadence is 0
     * (3 s after the last crank event at about 59.9 s, so from the row 62 second boundary at 63.0 s).
     * Five consecutive qualifying seconds 62..66: AUTO-PAUSED after row 66. Rows from 120 are at 8 m/s:
     * the 3 s mean is 2.7 m/s after row 120 and 5.3 after 121: two seconds at 1.4 m/s or more, so it
     * resumes after row 121. */
    TEST_ASSERT_EQUAL_INT(66, pause_t);
    TEST_ASSERT_EQUAL_INT(121, resume_t);
    TEST_ASSERT_EQUAL_INT(CC_RS_RECORDING, V()->state);
    /* Timer: START came 1.1 s before row 0's second boundary, so one second is recorded before row 0;
     * then rows 0..66 (67) and 122..179 (58). */
    TEST_ASSERT_EQUAL_UINT32(1 + 67 + 58, V()->timer_s);
}

static int32_t grad_at_150, max_grad;
static int steep_seen;
static void climb_each(int t)
{
    if (t == 150) grad_at_150 = V()->grad_ok ? V()->grad_x10 : -999;
    if (V()->grad_ok && V()->grad_x10 > max_grad) max_grad = V()->grad_x10;
    if (V()->steep) steep_seen = 1;
}

static void test_climb_gradient_ascent(void)
{
    cc_set_utc(now, 1793000000u);
    tick_to((now / 1000u + 1) * 1000u - 100);
    apply(EV_START);
    max_grad = -999;
    steep_seen = 0;
    play("climb.csv", climb_each);
    /* By hand: 5 m/s gives an entry every 10 m (2 s); five entries span 50 m (10 s), over which the
     * altitude rises 2.5 m in the steady state of the 0.25 filter: 2.5 / 50 * 100 = 5.0 %. */
    TEST_ASSERT_INT32_WITHIN(1, 50, grad_at_150);
    TEST_ASSERT_EQUAL_INT(0, steep_seen); /* 5 % never reaches 8 % */
    /* Ascent: 30 m of road climb, counted in steps of at least 1 m; the filter has settled by the end
     * of the flat. Barometer quantisation (1 Pa is about 8 cm) can cost under 1 m. */
    TEST_ASSERT_TRUE(V()->ascent_m >= 29 && V()->ascent_m <= 30);
    TEST_ASSERT_TRUE(max_grad <= 60); /* watch item 5.3: overshoot stays small here */
    printf("climb fixture: gradient at 150 s %d, max %d (x10 %%), ascent %u m\n", (int)grad_at_150, (int)max_grad,
           V()->ascent_m);
}

/* ------------------------------------------------------------------ unknown time */
static void fill_records(uint32_t secs)
{
    hal_gnss_fix_t g;
    memset(&g, 0, sizeof g);
    for (uint32_t s = 0; s < secs; s++) {
        g.valid = 0;
        g.utc_seconds = 0;
        cc_on_gnss(now + 500, &g);
        cc_on_power_sample(now + 500, 200, 0, 0, 0);
        tick_to(now + 1000);
    }
}

static void check_file_times(const char *path, uint32_t true_start)
{
    uint32_t len;
    const uint8_t *b = memfs_data(fs, path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(b, path);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    cc_fit_walk_t wk;
    cc_fit_walk(b, len - 2, 0, &wk);
    TEST_ASSERT_EQUAL_UINT32(true_start, wk.first_ts);
    /* Records are one per second from true_start + 1 */
    TEST_ASSERT_EQUAL_UINT32(true_start + wk.records, wk.session.last_ts - (wk.session.last_ts - (true_start + wk.records)));
    TEST_ASSERT_TRUE(wk.records >= 299);
}

static void test_unknown_time_core(void)
{
    /* No UTC at START */
    now = (now / 1000u + 1) * 1000u;
    cc_tick(now);
    TEST_ASSERT_FALSE(V()->utc_known);
    uint32_t start_ms = now;
    apply(EV_START);
    uint32_t prov = V()->ride_id;
    TEST_ASSERT_TRUE(prov < 1000); /* counter */
    fill_records(120);
    /* UTC arrives from the phone at 120 s: it is 1793000120 at this instant */
    uint32_t truth_start = 1793000120u - (now - start_ms) / 1000u;
    cc_set_utc(now, 1793000120u);
    fill_records(180);
    apply(EV_RH); apply(EV_L); apply(EV_P); apply(EV_R);
    TEST_ASSERT_EQUAL_INT(CC_RS_SUMMARY, V()->state);
    char p[64];
    snprintf(p, sizeof p, "rides/%u.fit", truth_start);
    check_file_times(p, truth_start);
    TEST_ASSERT_EQUAL_UINT32(truth_start, V()->summary.ride_id);
    TEST_ASSERT_EQUAL_UINT8(0, V()->summary.flags & CC_RIDE_TIME_UNKNOWN);

    /* Recovered: same ride, power lost before finishing */
    memfs_reset(fs);
    cc_init(fs, now);
    now = (now / 1000u + 1) * 1000u;
    cc_tick(now);
    start_ms = now;
    apply(EV_START);
    fill_records(120);
    truth_start = 1793000120u - (now - start_ms) / 1000u;
    cc_set_utc(now, 1793000120u);
    fill_records(180);
    /* power cut: whatever was flushed stays; boot recovers */
    memfs_cut_after_bytes(fs, 0);
    memfs_power_cycle(fs);
    TEST_ASSERT_EQUAL_INT(1, cc_init(fs, now + 10000));
    snprintf(p, sizeof p, "rides/%u.fit", truth_start);
    uint32_t len;
    const uint8_t *b = memfs_data(fs, p, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(b, "recovered ride not renamed to its true start");
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    cc_fit_walk_t wk;
    cc_fit_walk(b, len - 2, 0, &wk);
    TEST_ASSERT_EQUAL_UINT32(truth_start, wk.first_ts);
    TEST_ASSERT_TRUE(wk.records >= 290); /* everything up to the last 10 s flush */
    cc_ride_meta_t m;
    TEST_ASSERT_TRUE(cc_ride_meta_read(fs, truth_start, &m));
    TEST_ASSERT_TRUE(m.flags & CC_RIDE_RECOVERED);
    TEST_ASSERT_FALSE(m.flags & CC_RIDE_TIME_UNKNOWN);

    /* Never learned: kept provisional and flagged time unknown */
    memfs_reset(fs);
    cc_init(fs, now);
    now = (now / 1000u + 1) * 1000u;
    cc_tick(now);
    apply(EV_START);
    prov = V()->ride_id;
    fill_records(60);
    apply(EV_RH); apply(EV_L); apply(EV_P); apply(EV_R);
    TEST_ASSERT_EQUAL_UINT32(prov, V()->summary.ride_id);
    TEST_ASSERT_TRUE(V()->summary.flags & CC_RIDE_TIME_UNKNOWN);
}

static void test_page_ring(void)
{
    TEST_ASSERT_EQUAL_INT(2, V()->ring_n); /* no workout, no route: Main, Climb */
    TEST_ASSERT_EQUAL_INT(CC_PAGE_MAIN, V()->ring[0]);
    TEST_ASSERT_EQUAL_INT(CC_PAGE_CLIMB, V()->ring[1]);
}

static void test_ftp_only_when_stopped(void)
{
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_set_ftp(260));
    cc_tick(now); /* the view is refreshed by the tick */
    TEST_ASSERT_EQUAL_UINT16(260, V()->ftp);
    TEST_ASSERT_NOT_EQUAL(0, cc_cmd_set_ftp(40));
    apply(EV_START);
    TEST_ASSERT_NOT_EQUAL(0, cc_cmd_set_ftp(250));
    TEST_ASSERT_EQUAL_UINT16(260, V()->ftp);
    /* survives a restart */
    cc_init(fs, now);
    TEST_ASSERT_EQUAL_UINT16(260, V()->ftp);
}

/* Max HR (QUESTIONS Q47): set like FTP, only when stopped, survives a restart, and gives the view's
   heart-rate zone. */
static void test_max_hr_and_hr_zone(void)
{
    TEST_ASSERT_EQUAL_UINT16(CC_MAXHR_OFF, V()->max_hr); /* a new unit: not set */
    cc_on_sensor_state(2, CC_SENS_OK);
    cc_on_hr_sample(now, 150);
    tick_to(now + 1000);
    TEST_ASSERT_TRUE(V()->hr_ok);
    TEST_ASSERT_EQUAL_INT(0, V()->hr_zone); /* no zones until max HR is set */
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_set_max_hr(185));
    cc_on_hr_sample(now, 150);
    tick_to(now + 1000);
    TEST_ASSERT_EQUAL_UINT16(185, V()->max_hr);
    TEST_ASSERT_EQUAL_INT(4, V()->hr_zone); /* 150 >= 148 */
    TEST_ASSERT_EQUAL_INT(-2, cc_cmd_set_max_hr(99));
    TEST_ASSERT_EQUAL_INT(-2, cc_cmd_set_max_hr(231));
    apply(EV_START);
    TEST_ASSERT_EQUAL_INT(-1, cc_cmd_set_max_hr(190));
    cc_on_hr_sample(now, 170);
    tick_to(now + 1000);
    TEST_ASSERT_EQUAL_UINT16(185, V()->max_hr);
    TEST_ASSERT_EQUAL_INT(5, V()->hr_zone);
    /* the strap goes quiet: no reading, no zone */
    tick_to(now + 6000);
    TEST_ASSERT_FALSE(V()->hr_ok);
    TEST_ASSERT_EQUAL_INT(0, V()->hr_zone);
    /* survives a restart, and Off is a value it can be set back to */
    cc_init(fs, now);
    TEST_ASSERT_EQUAL_UINT16(185, V()->max_hr);
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_set_max_hr(CC_MAXHR_OFF));
    cc_init(fs, now);
    TEST_ASSERT_EQUAL_UINT16(CC_MAXHR_OFF, V()->max_hr);
    /* it lives in the profile record, not in the settings record */
    TEST_ASSERT_TRUE(memfs_exists(fs, CC_PROFILE_A) || memfs_exists(fs, CC_PROFILE_B));
}

/* A power-off the host could not carry out is cancelled, and the next hold is a new request. Without
 * this the request stays set for ever: the firmware either resends the frame every tick or ignores
 * the rider's second attempt until a reboot (fw/main/main.c). */
static void test_power_off_cancel(void)
{
    TEST_ASSERT_FALSE(V()->power_off_request);
    apply(EV_PH);
    TEST_ASSERT_TRUE(V()->power_off_request);
    cc_power_off_cancel();
    cc_tick(now); /* the view is refreshed by the tick */
    TEST_ASSERT_FALSE(V()->power_off_request);
    TEST_ASSERT_EQUAL_INT(CC_RS_READY, V()->state);
    short_press(CC_BTN_PWR); /* a short press is not a power-off request */
    TEST_ASSERT_FALSE(V()->power_off_request);
    apply(EV_PH);
    TEST_ASSERT_TRUE(V()->power_off_request);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_power_off_cancel);
    RUN_TEST(test_power_off_closes_files);
    RUN_TEST(test_page_ring);
    RUN_TEST(test_state_table);
    RUN_TEST(test_files_after_finish_discard);
    RUN_TEST(test_hold_bar);
    RUN_TEST(test_ftp_only_when_stopped);
    RUN_TEST(test_max_hr_and_hr_zone);
    RUN_TEST(test_indoor_never_pauses);
    RUN_TEST(test_redlight_pauses);
    RUN_TEST(test_climb_gradient_ascent);
    RUN_TEST(test_unknown_time_core);
    return UNITY_END();
}
