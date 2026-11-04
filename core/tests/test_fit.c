/* Brief 5.6: FIT writer, recovery at every truncation point, unknown-time correction.
 * If CC_FIT_OUT is set, the finished file and the .part snapshot are also written there for the
 * independent fitdecode checks in tools/fitcheck. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/fit.h"
#include "cc700/util.h"
#include "memfs.h"
#include "unity.h"

static hal_fs_t *fs;
void setUp(void) { fs = memfs_new(); fs->mkdir(fs, "rides"); }
void tearDown(void) { memfs_free(fs); }

#define T0 1793000000u /* 2026-10-26 */
#define RIDE_S 1800

static cc_fit_t w;

/* Expected totals, computed alongside the synthetic ride */
static struct {
    uint32_t records, timer_s, elapsed_s, dist_cm;
    uint64_t psum; uint32_t pn; uint16_t pmax;
    uint64_t hsum; uint32_t hn;
} ex;

static void fill(cc_fit_record_t *r, uint32_t s, uint32_t ts_base)
{
    memset(r, 0, sizeof *r);
    r->ts = ts_base + s;
    r->has_pos = 1;
    r->lat_e7 = -319640000 + (int32_t)s * 30;
    r->lon_e7 = 1158300000 + (int32_t)s * 50;
    r->has_alt = 1;
    r->alt_baro = 1;                                    /* a unit with a barometer: ascent is claimable */
    r->alt_cm = 2000 + (int32_t)(s % 300) * 10;        /* saw-tooth: 30 m up per 300 s */
    r->has_speed = 1;
    r->speed_mms = 8000;
    ex.dist_cm += 800;
    r->dist_cm = ex.dist_cm;
    r->has_hr = (s % 97) != 0;                          /* occasional dropout: omitted, not 0 */
    r->hr = (uint8_t)(120 + s % 40);
    r->has_cad = 1;
    r->cad = (uint8_t)(s % 50 == 0 ? 0 : 85);
    r->has_power = !(s >= 1000 && s < 1010);            /* 10 s power loss */
    r->power = (uint16_t)(150 + (s * 7) % 200);
    if (r->has_power) { ex.psum += r->power; ex.pn++; if (r->power > ex.pmax) ex.pmax = r->power; }
    if (r->has_hr) { ex.hsum += r->hr; ex.hn++; }
    ex.records++;
}

/* Writes the synthetic ride up to (not including) the closing messages. Pause 600..659 (no records),
 * manual lap at 900. Returns the end timestamp. */
static uint32_t synth_ride(const char *path, uint32_t ts_base)
{
    memset(&ex, 0, sizeof ex);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_begin(&w, fs, path, ts_base));
    cc_fit_event_timer(&w, ts_base, 1);
    for (uint32_t s = 0; s < RIDE_S + 60; s++) {
        if (s == 600) { cc_fit_event_timer(&w, ts_base + s, 0); cc_fit_flush(&w); }
        if (s >= 600 && s < 660) continue;
        if (s == 660) cc_fit_event_timer(&w, ts_base + s, 1);
        if (s == 900) { cc_fit_lap(&w, ts_base + s, CC_FIT_LAP_MANUAL); cc_fit_flush(&w); }
        cc_fit_record_t r;
        fill(&r, s, ts_base);
        cc_fit_record(&w, &r);
        if (s % 10 == 0) cc_fit_flush(&w);
    }
    uint32_t end = ts_base + RIDE_S + 60;
    ex.timer_s = RIDE_S + 60 - 60;
    ex.elapsed_s = RIDE_S + 60;
    return end;
}

static void dump(const char *name, const uint8_t *b, uint32_t n)
{
    const char *dir = getenv("CC_FIT_OUT");
    if (!dir) return;
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (f) { fwrite(b, 1, n, f); fclose(f); }
}

static void test_finished_file(void)
{
    uint32_t end = synth_ride("rides/1.fit.part", T0);
    cc_fit_flush(&w);
    uint32_t plen;
    const uint8_t *part = memfs_data(fs, "rides/1.fit.part", &plen);
    dump("synthetic.fit.part", part, plen);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_close_messages(&w, end, -1));
    cc_fit_summary_t s;
    uint32_t crc32, size;
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, 0, "rides/1.fit", &s, &crc32, &size));
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1.fit.part"));
    uint32_t len;
    const uint8_t *b = memfs_data(fs, "rides/1.fit", &len);
    TEST_ASSERT_NOT_NULL(b);
    dump("synthetic.fit", b, len);
    TEST_ASSERT_EQUAL_UINT32(size, len);
    TEST_ASSERT_EQUAL_HEX32(cc_crc32(b, len), crc32);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    cc_fit_walk_t wk;
    TEST_ASSERT_TRUE(cc_fit_walk(b, len - 2, 0, &wk) > 0);
    TEST_ASSERT_EQUAL_UINT32(ex.records, wk.records);
    TEST_ASSERT_EQUAL_UINT32(2, wk.laps); /* manual at 900 and the final one */

    TEST_ASSERT_EQUAL_UINT32(T0, s.start_ts);
    TEST_ASSERT_EQUAL_UINT32(ex.elapsed_s, s.elapsed_s);
    TEST_ASSERT_EQUAL_UINT32(ex.timer_s, s.timer_s);
    TEST_ASSERT_EQUAL_UINT32(ex.dist_cm / 100, s.distance_m);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)cc_div_half_up((int64_t)ex.psum, ex.pn), s.avg_power);
    TEST_ASSERT_EQUAL_UINT16(ex.pmax, s.max_power);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)cc_div_half_up((int64_t)ex.hsum, ex.hn), s.avg_hr);
    TEST_ASSERT_EQUAL_UINT8(85, s.avg_cad); /* zeros excluded */
    /* Saw-tooth: 29 m up per 300 s ramp in 1 m steps; ramps restart at 0, 300, 900, 1200, 1500, 1800
     * with the pause removing 600..659 so the 600 ramp starts at 660 (alt 2.0+0.6..). */
    TEST_ASSERT_TRUE(s.has_ascent);
    TEST_ASSERT_TRUE(s.ascent_m > 100 && s.ascent_m < 200);
    TEST_ASSERT_EQUAL_UINT32(ex.records, s.records);
}

/* ------------------------------------------------------------------ recovery at every cut */
static void test_recover_every_cut(void)
{
    if (getenv("CC_FIT_QUICK"))
        TEST_IGNORE_MESSAGE("CC_FIT_QUICK set (run by tools/fitcheck)");
    synth_ride("rides/1.fit.part", T0);
    cc_fit_flush(&w);
    uint32_t plen;
    const uint8_t *p0 = memfs_data(fs, "rides/1.fit.part", &plen);
    uint8_t *part = malloc(plen);
    memcpy(part, p0, plen);
    fs->close(fs, w.f);

    /* Record end offsets via the walker on growing prefixes is circular; instead find them from the
     * fixed layout: every message after the definitions starts with a local type byte 0..5. */
    cc_fit_walk_t full;
    TEST_ASSERT_EQUAL_UINT32(plen, cc_fit_walk(part, plen, 0, &full));
    uint32_t first_def_end = 14 + 6 + 3 * 5; /* file_id definition */

    uint32_t checked = 0;
    for (uint32_t cut = first_def_end; cut <= plen; cut++) {
        memfs_reset(fs);
        memfs_put(fs, "rides/1.fit.part", part, cut);
        cc_fit_walk_t pre;
        cc_fit_walk(part, cut, 1, &pre);
        cc_fit_summary_t s;
        int rc = cc_fit_recover(fs, "rides/1.fit.part", "rides/1.fit.rec", "rides/1.fit", 0, &s, NULL, NULL);
        if (pre.records == 0) {
            TEST_ASSERT_EQUAL_INT(1, rc);
            TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1.fit.part"));
            continue;
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "recover failed");
        uint32_t len;
        const uint8_t *b = memfs_data(fs, "rides/1.fit", &len);
        TEST_ASSERT_NOT_NULL(b);
        TEST_ASSERT_TRUE_MESSAGE(cc_fit_check(b, len), "bad CRC");
        cc_fit_walk_t wk;
        cc_fit_walk(b, len - 2, 0, &wk);
        TEST_ASSERT_EQUAL_UINT32(pre.records, wk.records);
        TEST_ASSERT_EQUAL_UINT32(pre.records, s.records);
        TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1.fit.part"));
        TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1.fit.rec"));
        checked++;
    }
    TEST_ASSERT_TRUE(checked > 40000);
    free(part);
}

/* A power cut during the finish: closing messages partly written. Recovery must not duplicate the
 * session and must keep every record. */
static void test_recover_after_partial_finish(void)
{
    uint32_t end = synth_ride("rides/1.fit.part", T0);
    cc_fit_flush(&w);
    uint32_t records = ex.records;
    uint32_t before;
    memfs_data(fs, "rides/1.fit.part", &before); /* every record is in the file */
    cc_fit_close_messages(&w, end, -1);
    uint32_t plen;
    const uint8_t *p0 = memfs_data(fs, "rides/1.fit.part", &plen);
    uint8_t *part = malloc(plen);
    memcpy(part, p0, plen);
    fs->close(fs, w.f);
    for (uint32_t cut = before; cut <= plen; cut++) {
        memfs_reset(fs);
        memfs_put(fs, "rides/1.fit.part", part, cut);
        cc_fit_summary_t s;
        TEST_ASSERT_EQUAL_INT(0, cc_fit_recover(fs, "rides/1.fit.part", "rides/1.fit.rec", "rides/1.fit", 0, &s, NULL, NULL));
        uint32_t len;
        const uint8_t *b = memfs_data(fs, "rides/1.fit", &len);
        TEST_ASSERT_TRUE(cc_fit_check(b, len));
        cc_fit_walk_t wk;
        cc_fit_walk(b, len - 2, 0, &wk);
        TEST_ASSERT_EQUAL_UINT32(records, wk.records);
        TEST_ASSERT_EQUAL_UINT32(2, wk.laps);
        TEST_ASSERT_TRUE(wk.session_seen == 0);
        TEST_ASSERT_EQUAL_UINT32(ex.timer_s, s.timer_s);
    }
    free(part);
}

/* ------------------------------------------------------------------ unknown time */
static void check_times(const uint8_t *b, uint32_t len, uint32_t true_t0)
{
    /* Every record timestamp equals the true UTC of its second */
    cc_fit_walk_t wk;
    cc_fit_walk(b, len - 2, 0, &wk);
    TEST_ASSERT_EQUAL_UINT32(true_t0, wk.first_ts);
    TEST_ASSERT_EQUAL_UINT32(true_t0, wk.session.start_ts);
    TEST_ASSERT_EQUAL_UINT32(true_t0 + RIDE_S + 60, wk.session.last_ts);
}

static void test_unknown_time_finish_and_recover(void)
{
    const uint32_t base = CC_FIT_PROVISIONAL_BASE + 5000; /* provisional: base + monotonic */
    const uint32_t truth = T0;
    /* UTC learned at 120 s: offset = utc - provisional for that second */
    int32_t offset = (int32_t)((truth + 120) - (base + 120));

    uint32_t end = synth_ride("rides/5000.fit.part", base);
    cc_fit_close_messages(&w, end, -1);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, offset, "rides/1793000000.fit", NULL, NULL, NULL));
    uint32_t len;
    const uint8_t *b = memfs_data(fs, "rides/1793000000.fit", &len);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    check_times(b, len, truth);
    dump("unknown-time.fit", b, len);

    /* The same ride recovered from its .part */
    memfs_reset(fs);
    fs->mkdir(fs, "rides");
    synth_ride("rides/5000.fit.part", base);
    cc_fit_flush(&w);
    fs->close(fs, w.f);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_recover(fs, "rides/5000.fit.part", "rides/5000.fit.rec", "rides/1793000000.fit",
                                            offset, NULL, NULL, NULL));
    b = memfs_data(fs, "rides/1793000000.fit", &len);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    cc_fit_walk_t wk;
    cc_fit_walk(b, len - 2, 0, &wk);
    TEST_ASSERT_EQUAL_UINT32(truth, wk.first_ts);
    TEST_ASSERT_EQUAL_UINT32(truth + RIDE_S + 60, wk.last_ts);
}

/* ------------------------------------------------------------------ storage errors */
static void test_storage_error_keeps_records_in_ram(void)
{
    TEST_ASSERT_EQUAL_INT(0, cc_fit_begin(&w, fs, "rides/9.fit.part", T0));
    cc_fit_event_timer(&w, T0, 1);
    memfs_fail_writes_from(fs, 1);
    memset(&ex, 0, sizeof ex);
    for (uint32_t s = 0; s < 120; s++) {
        cc_fit_record_t r;
        fill(&r, s, T0);
        cc_fit_record(&w, &r);
        if (s % 10 == 0) TEST_ASSERT_NOT_EQUAL(0, cc_fit_flush(&w));
    }
    TEST_ASSERT_EQUAL_UINT32(0, w.dropped_records);
    memfs_fail_writes_from(fs, 0); /* storage back */
    TEST_ASSERT_EQUAL_INT(0, cc_fit_flush(&w));
    cc_fit_close_messages(&w, T0 + 120, -1);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, 0, "rides/9.fit", NULL, NULL, NULL));
    uint32_t len;
    const uint8_t *b = memfs_data(fs, "rides/9.fit", &len);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    cc_fit_walk_t wk;
    cc_fit_walk(b, len - 2, 0, &wk);
    TEST_ASSERT_EQUAL_UINT32(120, wk.records);

    /* A long outage fills the RAM buffer: records are dropped, never corrupting the file */
    memfs_reset(fs);
    fs->mkdir(fs, "rides");
    cc_fit_begin(&w, fs, "rides/10.fit.part", T0);
    memfs_fail_writes_from(fs, 1);
    for (uint32_t s = 0; s < 2000; s++) {
        cc_fit_record_t r;
        fill(&r, s, T0);
        cc_fit_record(&w, &r);
    }
    TEST_ASSERT_TRUE(w.dropped_records > 0);
    TEST_ASSERT_TRUE(2000 - w.dropped_records >= 300); /* at least 5 minutes kept */
    memfs_fail_writes_from(fs, 0);
    cc_fit_close_messages(&w, T0 + 2000, -1);
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, 0, "rides/10.fit", NULL, NULL, NULL));
    b = memfs_data(fs, "rides/10.fit", &len);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
}


/* Q19: with no barometer the GNSS altitude still goes in every record, but no ascent is claimed.
 * Brief 5.3 defines ascent from the smoothed barometer alone, and the unit's own screens show "--"
 * for ascent without one, so a lap or session total_ascent derived from GNSS altitude would be a
 * number the device refuses to display -- and GNSS vertical noise inflates it badly through the 1 m
 * threshold. The file therefore carries the altitude stream (so a platform can correct it) and an
 * invalid total_ascent. The companion check that the written field really is FIT-invalid rather than
 * zero is in tools/fitcheck/test_fit_recovery.py, which decodes no-baro.fit with fitdecode. */
static void test_no_barometer_claims_no_ascent(void)
{
    TEST_ASSERT_EQUAL_INT(0, cc_fit_begin(&w, fs, "rides/2.fit.part", T0));
    cc_fit_event_timer(&w, T0, 1);
    for (uint32_t s = 0; s < 300; s++) {
        cc_fit_record_t r;
        memset(&r, 0, sizeof r);
        r.ts = T0 + s;
        r.has_pos = 1;
        r.lat_e7 = -319640000;
        r.lon_e7 = 1158300000;
        r.has_alt = 1;                 /* GNSS altitude: in the file ... */
        r.alt_baro = 0;                /* ... but not from the barometer, so not summed */
        r.alt_cm = 2000 + (int32_t)s * 10; /* a clean 30 m climb: 0 only because nothing claims it */
        r.dist_cm = s * 800;
        cc_fit_record(&w, &r);
    }
    /* -1: the ride engine has no barometric ascent to override with (core.c do_finish) */
    TEST_ASSERT_EQUAL_INT(0, cc_fit_close_messages(&w, T0 + 300, -1));
    cc_fit_summary_t s2;
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, 0, "rides/2.fit", &s2, NULL, NULL));
    TEST_ASSERT_FALSE(s2.has_ascent);
    TEST_ASSERT_EQUAL_UINT16(0, s2.ascent_m);
    uint32_t len;
    const uint8_t *b = memfs_data(fs, "rides/2.fit", &len);
    TEST_ASSERT_TRUE(cc_fit_check(b, len));
    dump("no-baro.fit", b, len);

    /* The same ride with a barometer does claim it, so the difference is the flag and nothing else. */
    TEST_ASSERT_EQUAL_INT(0, cc_fit_begin(&w, fs, "rides/3.fit.part", T0));
    cc_fit_event_timer(&w, T0, 1);
    for (uint32_t s = 0; s < 300; s++) {
        cc_fit_record_t r;
        memset(&r, 0, sizeof r);
        r.ts = T0 + s;
        r.has_alt = 1;
        r.alt_baro = 1;
        r.alt_cm = 2000 + (int32_t)s * 10;
        r.dist_cm = s * 800;
        cc_fit_record(&w, &r);
    }
    TEST_ASSERT_EQUAL_INT(0, cc_fit_close_messages(&w, T0 + 300, -1));
    cc_fit_summary_t s3;
    TEST_ASSERT_EQUAL_INT(0, cc_fit_finalise(&w, 0, "rides/3.fit", &s3, NULL, NULL));
    TEST_ASSERT_TRUE(s3.has_ascent);
    /* 2000 cm to 4990 cm in 10 cm steps. The reference only advances when the rise reaches a whole
     * metre, so 29 m is counted and the last 90 cm is not: brief 5.3's threshold, not a rounding. */
    TEST_ASSERT_EQUAL_UINT16(29, s3.ascent_m);
}

static void test_crc16_known(void)
{
    /* FIT header of the SDK's example files: crc of the 12 header bytes is self-consistent */
    uint8_t h[14] = {14, 0x10, 0x54, 0x08, 0, 0, 0, 0, '.', 'F', 'I', 'T', 0, 0};
    uint16_t c = cc_fit_crc16(0, h, 12);
    h[12] = (uint8_t)c;
    h[13] = (uint8_t)(c >> 8);
    TEST_ASSERT_EQUAL_HEX16(0, cc_fit_crc16(0, h, 14)); /* CRC over data plus its CRC is 0 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known);
    RUN_TEST(test_finished_file);
    RUN_TEST(test_recover_after_partial_finish);
    RUN_TEST(test_unknown_time_finish_and_recover);
    RUN_TEST(test_storage_error_keeps_records_in_ram);
    RUN_TEST(test_no_barometer_claims_no_ascent);
    RUN_TEST(test_recover_every_cut);
    return UNITY_END();
}
