/* WP1 fuzz: 1 million inputs into the CW1 decoder never crash (ASan and UBSan build catches reads
 * past the end). Half are random bytes, half are mutations of W1 with a valid ID re-stamped, so the
 * decoder's deeper checks are reached. Deterministic seed. */
#include <stdlib.h>
#include <string.h>

#include "cc700/workout.h"
#include "support.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}

static const char *W1_HEX =
    "43 57 01 00 18 12 24 0e 18 09 c0 11 f0 0f 00 00 0a 13 34 20 78 20 38 20 6d 69 6e 20 74 68 72 65"
    "73 68 6f 6c 64 58 02 f4 01 bc 02 01 00 00 00 07 57 61 72 6d 20 75 70 e0 01 b6 03 e8 03 00 00 01"
    "04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2 01 26 02 00 00 01 04 07 52 65 63 6f 76 65 72 e0 01 b6"
    "03 e8 03 00 00 02 04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2 01 26 02 00 00 02 04 07 52 65 63 6f"
    "76 65 72 e0 01 b6 03 e8 03 00 00 03 04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2 01 26 02 00 00 03"
    "04 07 52 65 63 6f 76 65 72 e0 01 b6 03 e8 03 00 00 04 04 09 54 68 72 65 73 68 6f 6c 64 f0 00 c2"
    "01 26 02 00 00 04 04 07 52 65 63 6f 76 65 72 58 02 26 02 90 01 01 00 00 00 09 43 6f 6f 6c 20 64"
    "6f 77 6e";

static void test_fuzz(void)
{
    static cc_workout_t wo;
    uint8_t w1[300];
    size_t w1n = hex_to_bytes(W1_HEX, w1, sizeof w1);
    long ok = 0;
    for (long i = 0; i < 1000000; i++) {
        size_t n;
        uint8_t *buf;
        if (i & 1) {
            n = rnd() % 600;
            buf = malloc(n ? n : 1); /* exact size so ASan sees any overread */
            for (size_t k = 0; k < n; k++)
                buf[k] = (uint8_t)rnd();
            if (n >= 3 && (rnd() & 1)) { buf[0] = 'C'; buf[1] = 'W'; buf[2] = 1; }
        } else {
            n = w1n;
            if (rnd() % 4 == 0)
                n = rnd() % (w1n + 8);
            buf = malloc(n ? n : 1);
            for (size_t k = 0; k < n; k++)
                buf[k] = k < w1n ? w1[k] : (uint8_t)rnd();
            int muts = 1 + (int)(rnd() % 4);
            for (int m = 0; m < muts && n > 0; m++)
                buf[rnd() % n] = (uint8_t)rnd();
            if (n > 12 && (rnd() & 1))
                cc_object_id(buf, n, buf + 4);
        }
        if (cc_cw1_decode(buf, n, &wo) == CC_CW1_OK)
            ok++;
        free(buf);
    }
    TEST_ASSERT_TRUE(ok >= 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fuzz);
    return UNITY_END();
}
