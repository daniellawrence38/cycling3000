/* Spec 10.2 and brief 5.8: every alert fires and expires per the table, in a test that drives time.
 * The banner module alone, then the core's triggers for the sensor, GPS, battery and storage alerts. */
#include <string.h>

#include "cc700/core.h"
#include "memfs.h"
#include "unity.h"

static hal_fs_t *fs;
static uint32_t now;
static cc_alerts_t al;

void setUp(void)
{
    fs = memfs_new();
    now = 2000000;
    cc_init(fs, now);
    cc_alerts_init(&al);
}
void tearDown(void) { memfs_free(fs); }

static void at(uint32_t t) { cc_alerts_tick(&al, t); }

/* ------------------------------------------------------------------ texts */
static void test_texts(void)
{
    cc_banner_t b;
    cc_banner_nextstep(&b, "Recover", 136, 150, 240, 5, 0x8E9AA6, 5000);
    TEST_ASSERT_EQUAL_STRING("NEXT \xC2\xB7 RECOVER", b.title);
    TEST_ASSERT_EQUAL_STRING("136\xE2\x80\x93" "150 W for 4:00", b.sub);
    TEST_ASSERT_EQUAL_STRING("5 s", b.right);
    TEST_ASSERT_EQUAL_INT(CC_AL_STYLE_PANEL, b.style);
    cc_banner_lap(&b, 3, 312, 2400, 0);
    TEST_ASSERT_EQUAL_STRING("LAP 3", b.title);
    TEST_ASSERT_EQUAL_STRING("5:12 \xC2\xB7 2.4 km", b.sub);
    TEST_ASSERT_EQUAL_UINT32(4000, b.duration_ms);
    cc_banner_complete(&b, "4 x 8 min threshold");
    TEST_ASSERT_EQUAL_STRING("WORKOUT COMPLETE", b.title);
    TEST_ASSERT_EQUAL_STRING("4 x 8 min threshold", b.sub);
    TEST_ASSERT_EQUAL_INT(CC_AL_STYLE_GREEN, b.style);
    TEST_ASSERT_EQUAL_UINT32(10000, b.duration_ms);
    cc_banner_powerlost(&b);
    TEST_ASSERT_EQUAL_STRING("POWER METER LOST", b.title);
    TEST_ASSERT_EQUAL_STRING("Trying to reconnect", b.sub);
    TEST_ASSERT_EQUAL_INT(CC_AL_STYLE_RED, b.style);
    TEST_ASSERT_EQUAL_UINT32(6000, b.duration_ms);
    cc_banner_gpslost(&b);
    TEST_ASSERT_EQUAL_STRING("GPS SIGNAL LOST", b.title);
    TEST_ASSERT_EQUAL_STRING("No speed or distance", b.sub);
    TEST_ASSERT_EQUAL_INT(CC_AL_STYLE_YELLOW, b.style);
    TEST_ASSERT_EQUAL_UINT32(6000, b.duration_ms);
    cc_banner_lowbatt(&b, 15);
    TEST_ASSERT_EQUAL_STRING("LOW BATTERY 15%", b.title);
    TEST_ASSERT_EQUAL_STRING("Recording continues", b.sub);
    TEST_ASSERT_EQUAL_INT(CC_AL_STYLE_ORANGE, b.style);
    TEST_ASSERT_EQUAL_UINT32(8000, b.duration_ms);
    cc_banner_climbahead(&b, 1100, 48, 45, 400);
    TEST_ASSERT_EQUAL_STRING("CLIMB AHEAD", b.title);
    TEST_ASSERT_EQUAL_STRING("1.1 km \xC2\xB7 48 m \xC2\xB7 4.5 %", b.sub);
    TEST_ASSERT_EQUAL_STRING("400 m", b.right);
    TEST_ASSERT_EQUAL_UINT32(6000, b.duration_ms);
    cc_banner_storage(&b);
    TEST_ASSERT_EQUAL_STRING("STORAGE ERROR", b.title);
    TEST_ASSERT_EQUAL_STRING("Ride may not be saved", b.sub);
    TEST_ASSERT_EQUAL_UINT32(CC_AL_UNTIL_CLEARED, b.duration_ms);
}

/* ------------------------------------------------------------------ expiry and priority */
static void test_expiry_each(void)
{
    struct { void (*make)(cc_banner_t *); uint32_t ms; } rows[] = {
        {cc_banner_powerlost, 6000}, {cc_banner_gpslost, 6000},
    };
    for (unsigned i = 0; i < 2; i++) {
        cc_banner_t b;
        rows[i].make(&b);
        cc_alerts_init(&al);
        cc_alerts_raise(&al, &b, 1000);
        at(1000 + rows[i].ms - 1);
        TEST_ASSERT_EQUAL_INT(b.id, al.visible.id);
        at(1000 + rows[i].ms);
        TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    }
    cc_banner_t b;
    cc_banner_lap(&b, 1, 60, 500, 0);
    cc_alerts_raise(&al, &b, 0);
    at(3999); TEST_ASSERT_EQUAL_INT(CC_AL_LAP, al.visible.id);
    at(4000); TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    cc_banner_lowbatt(&b, 5);
    cc_alerts_raise(&al, &b, 10000);
    at(17999); TEST_ASSERT_EQUAL_INT(CC_AL_LOWBATT, al.visible.id);
    at(18000); TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    cc_banner_complete(&b, "W");
    cc_alerts_raise(&al, &b, 20000);
    at(25000); TEST_ASSERT_EQUAL_INT(CC_AL_COMPLETE, al.visible.id);
    cc_alerts_button(&al, 25000); /* any button */
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    cc_alerts_raise(&al, &b, 30000);
    at(40000); TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id); /* or 10 s */
    cc_banner_storage(&b);
    cc_alerts_raise(&al, &b, 50000);
    at(500000); TEST_ASSERT_EQUAL_INT(CC_AL_STORAGE, al.visible.id); /* until storage works */
    cc_alerts_clear(&al, CC_AL_STORAGE, 500000);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    cc_banner_nextstep(&b, "Recover", 117, 143, 240, 5, 0, 5000);
    cc_alerts_raise(&al, &b, 60000);
    cc_alerts_set_right(&al, CC_AL_NEXTSTEP, "3 s");
    TEST_ASSERT_EQUAL_STRING("3 s", al.visible.right);
    at(64999); TEST_ASSERT_EQUAL_INT(CC_AL_NEXTSTEP, al.visible.id);
    at(65000); TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id); /* until the step ends */
}

static void test_priority_and_queue(void)
{
    cc_banner_t lap, pl, gl, st;
    cc_banner_lap(&lap, 1, 60, 500, 0);
    cc_banner_powerlost(&pl);
    cc_banner_gpslost(&gl);
    cc_banner_storage(&st);
    /* Higher priority replaces at once */
    cc_alerts_raise(&al, &lap, 0);
    cc_alerts_raise(&al, &pl, 1000);
    TEST_ASSERT_EQUAL_INT(CC_AL_POWERLOST, al.visible.id);
    /* Lower priority waits; shows when the visible one expires if not more than 5 s late */
    cc_alerts_init(&al);
    cc_alerts_raise(&al, &gl, 0);       /* visible until 6000 */
    cc_alerts_raise(&al, &lap, 2000);   /* waits; 6000 is 4 s after its trigger */
    at(5999); TEST_ASSERT_EQUAL_INT(CC_AL_GPSLOST, al.visible.id);
    at(6000); TEST_ASSERT_EQUAL_INT(CC_AL_LAP, al.visible.id);
    /* ... and is dropped if it would show more than 5 s late */
    cc_alerts_init(&al);
    cc_alerts_raise(&al, &gl, 0);
    cc_alerts_raise(&al, &lap, 500);    /* would show at 6000: 5.5 s late */
    at(6000); TEST_ASSERT_EQUAL_INT(CC_AL_NONE, al.visible.id);
    /* Equal priority waits too */
    cc_alerts_init(&al);
    cc_alerts_raise(&al, &gl, 0);
    cc_alerts_raise(&al, &st, 100);     /* storage is highest: replaces */
    TEST_ASSERT_EQUAL_INT(CC_AL_STORAGE, al.visible.id);
    /* One deep: a later lower one does not displace a waiting higher one */
    cc_alerts_init(&al);
    cc_alerts_raise(&al, &st, 0);
    cc_alerts_raise(&al, &gl, 100);
    cc_alerts_raise(&al, &lap, 200);
    TEST_ASSERT_EQUAL_INT(CC_AL_GPSLOST, al.queued.id);
}

/* ------------------------------------------------------------------ core triggers */
static void tick_s(int secs, int power, int fix)
{
    hal_gnss_fix_t g;
    memset(&g, 0, sizeof g);
    g.valid = fix;
    g.lat_e7 = -319640000;
    g.lon_e7 = 1158300000;
    g.speed_mps = 8.0f;
    for (int s = 0; s < secs; s++) {
        for (int k = 0; k < 10; k++) {
            now += 100;
            if (k == 5) {
                cc_on_gnss(now, &g);
                if (power) cc_on_power_sample(now, 200, 0, 0, 0);
            }
            cc_tick(now);
        }
    }
}

static void test_core_triggers(void)
{
    cc_set_utc(now, 1793000000u);
    cc_on_battery(50, 0);
    tick_s(2, 1, 1);
    cc_cmd_start(now);
    tick_s(10, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    /* Power meter silent: lost after 3 s, banner 6 s, power shows "--" */
    tick_s(3, 0, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_POWERLOST, cc_view()->banner.id);
    TEST_ASSERT_FALSE(cc_view()->power3_ok);
    tick_s(6, 0, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    tick_s(1, 1, 1); /* recovery: no banner */
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    TEST_ASSERT_TRUE(cc_view()->power3_ok);
    /* GPS: no usable fix for 10 s */
    tick_s(9, 1, 0);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    uint32_t d = cc_view()->dist_m;
    tick_s(1, 1, 0);
    TEST_ASSERT_EQUAL_INT(CC_AL_GPSLOST, cc_view()->banner.id);
    TEST_ASSERT_FALSE(cc_view()->speed_ok);
    tick_s(5, 1, 0);
    TEST_ASSERT_EQUAL_UINT32(d, cc_view()->dist_m); /* distance held */
    tick_s(2, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    /* Battery: once at 15 %, once at 5 % */
    cc_on_battery(15, 0);
    tick_s(1, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_LOWBATT, cc_view()->banner.id);
    TEST_ASSERT_EQUAL_STRING("LOW BATTERY 15%", cc_view()->banner.title);
    tick_s(8, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id);
    cc_on_battery(14, 0);
    tick_s(2, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_NONE, cc_view()->banner.id); /* once per threshold per ride */
    cc_on_battery(5, 0);
    tick_s(1, 1, 1);
    TEST_ASSERT_EQUAL_STRING("LOW BATTERY 5%", cc_view()->banner.title);
    tick_s(9, 1, 1);
    /* Storage: writes fail for 30 s while recording */
    memfs_fail_writes_from(fs, 1);
    tick_s(29, 1, 1);
    TEST_ASSERT_NOT_EQUAL(CC_AL_STORAGE, cc_view()->banner.id);
    tick_s(12, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_STORAGE, cc_view()->banner.id);
    TEST_ASSERT_TRUE(cc_view()->storage_error);
    TEST_ASSERT_EQUAL_INT(CC_RS_RECORDING, cc_view()->state); /* never crash, keep recording */
    memfs_fail_writes_from(fs, 0);
    tick_s(10, 1, 1);
    TEST_ASSERT_NOT_EQUAL(CC_AL_STORAGE, cc_view()->banner.id);
    TEST_ASSERT_FALSE(cc_view()->storage_error);
}

/* Next-step and complete banners from a loaded workout (W2 at the default FTP of 200 W) */
static void test_core_workout_banners(void)
{
    static const uint8_t w2[] = {
        0x43, 0x57, 0x01, 0x00, 0x34, 0xa5, 0x60, 0x4c, 0x1d, 0x73, 0x98, 0x5e, 0x84, 0x03, 0x00, 0x00, 0x05, 0x11, 0x43,
        0x61, 0x64, 0x65, 0x6e, 0x63, 0x65, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x74, 0x65, 0x6d, 0x70, 0x6f, 0x3c, 0x00, 0xc2,
        0x01, 0x26, 0x02, 0x00, 0x6e, 0x00, 0x00, 0x07, 0x53, 0x70, 0x69, 0x6e, 0x20, 0x75, 0x70, 0x2c, 0x01, 0xf8, 0x02,
        0x48, 0x03, 0x00, 0x55, 0x01, 0x02, 0x05, 0x54, 0x65, 0x6d, 0x70, 0x6f, 0x78, 0x00, 0xc2, 0x01, 0x26, 0x02, 0x00,
        0x00, 0x01, 0x02, 0x07, 0x52, 0x65, 0x63, 0x6f, 0x76, 0x65, 0x72, 0x2c, 0x01, 0xf8, 0x02, 0x48, 0x03, 0x00, 0x55,
        0x02, 0x02, 0x05, 0x54, 0x65, 0x6d, 0x70, 0x6f, 0x78, 0x00, 0xc2, 0x01, 0x26, 0x02, 0x00, 0x00, 0x02, 0x02, 0x07,
        0x52, 0x65, 0x63, 0x6f, 0x76, 0x65, 0x72};
    char p[64];
    cc_object_path(p, sizeof p, w2 + 4, 1);
    memfs_put(fs, p, w2, sizeof w2);
    TEST_ASSERT_EQUAL_INT(0, cc_cmd_load_workout(w2 + 4));
    cc_set_utc(now, 1793000000u);
    now = (now / 1000u + 1) * 1000u - 50;
    cc_tick(now);
    TEST_ASSERT_EQUAL_INT(3, cc_view()->ring_n); /* Main, Workout, Climb */
    TEST_ASSERT_EQUAL_INT(CC_PAGE_WORKOUT, cc_view()->ring[1]);
    cc_cmd_start(now);
    tick_s(55, 1, 1); /* timer 55: 5 s left of the 60 s Spin up */
    TEST_ASSERT_EQUAL_INT(CC_AL_NEXTSTEP, cc_view()->banner.id);
    TEST_ASSERT_EQUAL_STRING("NEXT \xC2\xB7 TEMPO", cc_view()->banner.title);
    TEST_ASSERT_EQUAL_STRING("152\xE2\x80\x93" "168 W for 5:00", cc_view()->banner.sub);
    TEST_ASSERT_EQUAL_STRING("5 s", cc_view()->banner.right);
    tick_s(2, 1, 1);
    TEST_ASSERT_EQUAL_STRING("3 s", cc_view()->banner.right);
    tick_s(3, 1, 1); /* step change at 60 s */
    TEST_ASSERT_NOT_EQUAL(CC_AL_NEXTSTEP, cc_view()->banner.id);
    TEST_ASSERT_EQUAL_INT(1, cc_view()->wo.step);
    TEST_ASSERT_EQUAL_UINT32(0, cc_view()->lap_s); /* a step change is a lap */
    tick_s(900 - 60, 1, 1);
    TEST_ASSERT_EQUAL_INT(CC_AL_COMPLETE, cc_view()->banner.id);
    TEST_ASSERT_EQUAL_STRING("Cadence and tempo", cc_view()->banner.sub);
    TEST_ASSERT_TRUE(cc_view()->wo.complete);
    TEST_ASSERT_EQUAL_INT(CC_RS_RECORDING, cc_view()->state); /* the ride continues */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_texts);
    RUN_TEST(test_expiry_each);
    RUN_TEST(test_priority_and_queue);
    RUN_TEST(test_core_triggers);
    RUN_TEST(test_core_workout_banners);
    return UNITY_END();
}
