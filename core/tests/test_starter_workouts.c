/* The six starter workouts (claude/workouts/) encoded by ref.py decode in C with the same size, step
 * count, total, ID and CRC, and run through the engine with the short-step rules (spec 7.6, 10.2). */
#include <stdio.h>
#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"
#include "cc700/workout.h"
#include "support.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t buf[CC_WO_MAX_BYTES + 1];
static cc_workout_t wo;
static cc_wo_engine_t eng;

static size_t read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n;
}

static void run_engine(int *banners, int *pills_on_short, int *laps)
{
    *banners = *pills_on_short = *laps = 0;
    cc_wo_engine_load(&eng, &wo, 200);
    int last_banner_step = -1;
    for (uint32_t t = 0; t <= wo.total_secs; t++) {
        unsigned ev = cc_wo_engine_second(&eng, t, 1, 200);
        if (ev & CC_WO_EV_STEP_CHANGE) (*laps)++;
        if (eng.view.nextstep_banner && eng.view.step != last_banner_step) {
            (*banners)++;
            last_banner_step = eng.view.step;
            TEST_ASSERT_TRUE(eng.view.step_secs >= CC_SHORT_STEP_S);
        }
        if (!eng.view.complete && eng.view.step_secs < CC_SHORT_STEP_S && eng.view.pill != CC_PILL_HIDDEN)
            (*pills_on_short)++;
    }
    TEST_ASSERT_TRUE(eng.view.complete);
}

static void test_all(void)
{
    FILE *idx = fopen("fixtures/workouts.txt", "r");
    TEST_ASSERT_NOT_NULL(idx);
    char file[64], idhex[17];
    unsigned size, nsteps, total, crc;
    int count = 0;
    while (fscanf(idx, "%63s %u %u %u %16s %x", file, &size, &nsteps, &total, idhex, &crc) == 6) {
        char path[96];
        snprintf(path, sizeof path, "fixtures/%s", file);
        size_t n = read_file(path);
        TEST_ASSERT_EQUAL_size_t(size, n);
        TEST_ASSERT_EQUAL_HEX32(crc, cc_crc32(buf, n));
        TEST_ASSERT_EQUAL_INT_MESSAGE(CC_CW1_OK, cc_cw1_decode(buf, n, &wo), file);
        TEST_ASSERT_EQUAL_UINT(nsteps, wo.nsteps);
        TEST_ASSERT_EQUAL_UINT(total, wo.total_secs);
        char got[17];
        for (int i = 0; i < 8; i++)
            snprintf(got + 2 * i, 3, "%02x", wo.id[i]);
        TEST_ASSERT_EQUAL_STRING(idhex, got);

        int banners, pills, laps;
        run_engine(&banners, &pills, &laps);
        TEST_ASSERT_EQUAL_INT(0, pills);
        TEST_ASSERT_EQUAL_INT((int)nsteps - 1, laps);
        int expect = 0;
        for (int i = 0; i + 1 < wo.nsteps; i++)
            expect += wo.steps[i].secs >= CC_SHORT_STEP_S;
        TEST_ASSERT_EQUAL_INT(expect, banners);
        if (strcmp(file, "vo2max-30-15.cw1") == 0) {
            /* Banners only at the end of the warm-up, the tempo and the three 3 min recoveries; none
             * inside the 39 short efforts and their 15 s recoveries */
            TEST_ASSERT_EQUAL_INT(5, banners);
        }
        count++;
    }
    fclose(idx);
    TEST_ASSERT_EQUAL_INT(6, count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all);
    return UNITY_END();
}
