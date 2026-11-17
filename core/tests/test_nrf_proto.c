/* test_nrf_proto.c: the nRF link's framing and sensor decode, on the host. Every frame below is copied
 * byte for byte from outputs/nrf-capture-2026-11-04.txt (the "raw #" lines), so the CRCs are the
 * nRF's own and not this code's. */
#include <string.h>

#include "../../fw/main/nrf_proto.c"
#include "cc700/altitude.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t F_ACCEL[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x10, 0xf1, 0x01,
                                  0x09, 0x00, 0xf8, 0xff, 0x18, 0x04, 0x55, 0x13}; /* raw #6089 */
static const uint8_t F_BARO[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x10, 0xf1, 0x03,
                                 0xbc, 0x0c, 0xd3, 0x18, 0x9b, 0x00, 0x8f, 0x42}; /* raw #6093 */
static const uint8_t F_CLOCK[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x00, 0x53, 0xff,
                                  0x09, 0x00, 0x09, 0x64, 0x0a, 0x7e, 0x09, 0x8f}; /* raw #6094 */
static const uint8_t F_IDLE[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x10, 0xf0, 0x02,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xdb}; /* raw #6098 */
static const uint8_t F_04[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x10, 0xf1, 0x04,
                               0x58, 0x02, 0x00, 0x00, 0x1e, 0x05, 0x2f, 0x74}; /* raw #6099 */
static const uint8_t F_0A[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x10, 0xf1, 0x0a,
                               0xec, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x8a, 0xae}; /* raw #6100 */

static void test_crc_is_xmodem(void)
{
    /* The CRC-16/XMODEM check value. */
    TEST_ASSERT_EQUAL_HEX16(0x31C3, fw_nrf_crc16((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX16(0x0000, fw_nrf_crc16(NULL, 0));
}

static void test_captured_frames_are_accepted(void)
{
    const uint8_t *all[] = {F_ACCEL, F_BARO, F_CLOCK, F_IDLE, F_04, F_0A};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        fw_nrf_frame_t f;
        TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(all[i], 16, &f));
        TEST_ASSERT_EQUAL(4, f.kind);
        TEST_ASSERT_EQUAL(8, f.plen);
        TEST_ASSERT_EQUAL_PTR(all[i] + 6, f.payload);
    }
    fw_nrf_frame_t f;
    fw_nrf_take_frame(F_CLOCK, 16, &f);
    TEST_ASSERT_EQUAL(0, f.id);
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(16, f.id);
}

static void test_incomplete_and_invalid(void)
{
    fw_nrf_frame_t f;
    for (size_t n = 0; n < 16; n++)
        TEST_ASSERT_EQUAL(FW_NRF_MORE, fw_nrf_take_frame(F_BARO, n, &f));

    uint8_t b[16];
    /* Every single-bit corruption of every byte is refused; none is accepted as a frame. */
    for (size_t i = 0; i < 16; i++) {
        for (int bit = 0; bit < 8; bit++) {
            memcpy(b, F_BARO, 16);
            b[i] ^= (uint8_t)(1u << bit);
            int r = fw_nrf_take_frame(b, 16, &f);
            if (i == 1 && r == FW_NRF_MORE)
                continue; /* a longer declared length just waits for more bytes */
            TEST_ASSERT_TRUE_MESSAGE(r == FW_NRF_NOT_FRAME || r == FW_NRF_BAD_CRC, "corruption accepted");
        }
    }
    memcpy(b, F_BARO, 16);
    b[13] ^= 0x01;
    TEST_ASSERT_EQUAL(FW_NRF_BAD_CRC, fw_nrf_take_frame(b, 16, &f));
    memcpy(b, F_BARO, 16);
    b[0] = 0xA4;
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
    memcpy(b, F_BARO, 16);
    b[2] = 0x6E;
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
    memcpy(b, F_BARO, 16);
    b[1] = 133;
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
    b[1] = 3; /* shorter than the header and CRC */
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
    memcpy(b, F_BARO, 16);
    b[4] = 0;
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
    b[4] = 6;
    TEST_ASSERT_EQUAL(FW_NRF_NOT_FRAME, fw_nrf_take_frame(b, 16, &f));
}

static void test_barometer(void)
{
    fw_nrf_frame_t f;
    fw_nrf_baro_t b;
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(F_BARO, 16, &f));
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_baro(&f, &b));
    TEST_ASSERT_EQUAL(3260, b.temp_c100);          /* 32.60 degrees C */
    TEST_ASSERT_EQUAL_UINT32(10164435u, b.pressure_cpa); /* 1016.44 hPa */
    TEST_ASSERT_EQUAL(101644, fw_nrf_baro_pa(&b));
    /* Through the core's own conversion (brief 5.3): 1016.44 hPa is about 27 m below the standard
       atmosphere's sea level, an ordinary reading on a high-pressure day near the coast. */
    float h = cc_alt_from_pa((float)fw_nrf_baro_pa(&b));
    TEST_ASSERT_FLOAT_WITHIN(1.0f, -26.6f, h);

    /* The other sub-messages on the same id are not the barometer. */
    const uint8_t *others[] = {F_ACCEL, F_CLOCK, F_IDLE, F_04, F_0A};
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        fw_nrf_take_frame(others[i], 16, &f);
        TEST_ASSERT_EQUAL(-1, fw_nrf_parse_baro(&f, &b));
    }
}

static void test_barometer_refuses_what_it_does_not_recognise(void)
{
    uint8_t p[8] = {0xf1, 0x03, 0xbc, 0x0c, 0xd3, 0x18, 0x9b, 0x00};
    fw_nrf_frame_t f = {.kind = 4, .id = 16, .payload = p, .plen = 8};
    fw_nrf_baro_t b;
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_baro(&f, &b));
    f.kind = 2;
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_baro(&f, &b));
    f.kind = 4;
    f.id = 0;
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_baro(&f, &b));
    f.id = 16;
    f.plen = 7;
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_baro(&f, &b));
    f.plen = 8;

    /* Pressure limits: 300.00 hPa and 1100.00 hPa are accepted, one count beyond is not. */
    static const struct { uint32_t cpa; int ok; } lim[] = {
        {3000000u, 1}, {2999999u, 0}, {11000000u, 1}, {11000001u, 0}, {0u, 0}, {0xFFFFFFFFu, 0}};
    for (size_t i = 0; i < sizeof lim / sizeof lim[0]; i++) {
        p[4] = (uint8_t)lim[i].cpa;
        p[5] = (uint8_t)(lim[i].cpa >> 8);
        p[6] = (uint8_t)(lim[i].cpa >> 16);
        p[7] = (uint8_t)(lim[i].cpa >> 24);
        TEST_ASSERT_EQUAL(lim[i].ok ? 0 : -1, fw_nrf_parse_baro(&f, &b));
    }
    /* Rounding to whole pascals is half up. */
    b.pressure_cpa = 10132549u;
    TEST_ASSERT_EQUAL(101325, fw_nrf_baro_pa(&b));
    b.pressure_cpa = 10132550u;
    TEST_ASSERT_EQUAL(101326, fw_nrf_baro_pa(&b));
    /* A temperature below zero is signed. */
    p[2] = 0x38;
    p[3] = 0xff; /* -200 = -2.00 degrees C */
    p[4] = 0xd3; p[5] = 0x18; p[6] = 0x9b; p[7] = 0x00;
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_baro(&f, &b));
    TEST_ASSERT_EQUAL(-200, b.temp_c100);
}

static void test_accelerometer(void)
{
    fw_nrf_frame_t f;
    fw_nrf_accel_t a;
    fw_nrf_take_frame(F_ACCEL, 16, &f);
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_accel(&f, &a));
    TEST_ASSERT_EQUAL(9, a.x);
    TEST_ASSERT_EQUAL(-8, a.y);
    TEST_ASSERT_EQUAL(1048, a.z); /* the unit lay flat: one g */
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_accel(&f, &a));
}

static void test_clock_decodes_the_capture_date(void)
{
    fw_nrf_frame_t f;
    fw_nrf_clock_t c;
    fw_nrf_take_frame(F_CLOCK, 16, &f);
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_clock(&f, &c));
    /* The capture was taken on 4 November 2026: the frame says so, which is the check on the layout. */
    TEST_ASSERT_EQUAL(2026, c.year);
    TEST_ASSERT_EQUAL(11, c.month);
    TEST_ASSERT_EQUAL(4, c.day);
    TEST_ASSERT_EQUAL(9, c.hour);
    TEST_ASSERT_EQUAL(0, c.minute);
    TEST_ASSERT_EQUAL(9, c.second);
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_clock(&f, &c));
    const uint8_t p[8] = {0x53, 0xff, 59, 59, 23, 31, 11, 126};
    fw_nrf_frame_t g = {.kind = 4, .id = 0, .payload = p, .plen = 8};
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_clock(&g, &c));
    static const struct { int at; uint8_t v; } bad[] = {{2, 60}, {3, 60}, {4, 24}, {5, 0}, {6, 12}, {7, 99}};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t q[8];
        memcpy(q, p, 8);
        q[bad[i].at] = bad[i].v;
        g.payload = q;
        TEST_ASSERT_EQUAL(-1, fw_nrf_parse_clock(&g, &c));
    }
}

static void test_battery_and_light_candidates(void)
{
    static const uint8_t F_BATT[] = {0xa5, 0x0c, 0x6f, 0xf1, 0x04, 0x00, 0x52, 0xff,
                                     0xff, 0xff, 0xc2, 0x10, 0x64, 0xff, 0xd7, 0x8e}; /* raw #6097 */
    fw_nrf_frame_t f;
    fw_nrf_battery_t b;
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(F_BATT, 16, &f));
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_battery(&f, &b));
    TEST_ASSERT_EQUAL(4290, b.millivolts);
    TEST_ASSERT_EQUAL(100, b.percent);
    fw_nrf_take_frame(F_CLOCK, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_battery(&f, &b));

    fw_nrf_light_t l;
    fw_nrf_take_frame(F_04, 16, &f);
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_light(&f, &l));
    TEST_ASSERT_EQUAL_UINT32(600u, l.value); /* the factory screen's "Light lux" on this unit */
    TEST_ASSERT_EQUAL(1310, l.aux);
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_light(&f, &l));
}

static void test_key_event_layout(void)
{
    /* No key frame has been captured yet, so this pins only the vendor's field positions. */
    const uint8_t p[8] = {0x49, 0x02, 0xAA, 0xBB, 0xCC, 0x07, 0x04, 0xDD};
    fw_nrf_frame_t f = {.kind = 4, .id = 16, .payload = p, .plen = 8};
    fw_nrf_key_t k;
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_key(&f, &k));
    TEST_ASSERT_EQUAL(2, k.index);
    TEST_ASSERT_EQUAL(4, k.event);
    TEST_ASSERT_EQUAL(7, k.extra);
    f.id = 0;
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_key(&f, &k));
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_key(&f, &k));
}

/* The receive loop the firmware uses, fed in awkward chunk sizes with noise between the frames. */
static void test_stream_resynchronises(void)
{
    uint8_t stream[256];
    size_t n = 0;
    const uint8_t noise[] = {0x00, 0xA5, 0x13, 0x6F, 0xA5, 0xA5, 0x0c, 0x6f}; /* looks like a start, twice */
    memcpy(stream + n, noise, sizeof noise); n += sizeof noise;
    memcpy(stream + n, F_ACCEL, 16); n += 16;
    memcpy(stream + n, F_BARO, 16); n += 16;
    stream[n++] = 0xFF;
    memcpy(stream + n, F_CLOCK, 16); n += 16;
    memcpy(stream + n, F_BARO, 9); n += 9; /* a frame cut short, then a whole one */
    memcpy(stream + n, F_0A, 16); n += 16;

    for (size_t chunk = 1; chunk <= 17; chunk++) {
        uint8_t acc[64];
        size_t acc_len = 0;
        int frames = 0, baros = 0;
        for (size_t at = 0; at < n; at += chunk) {
            size_t take = n - at < chunk ? n - at : chunk;
            for (size_t i = 0; i < take; i++) {
                TEST_ASSERT_TRUE(acc_len < sizeof acc);
                acc[acc_len++] = stream[at + i];
                while (acc_len) {
                    fw_nrf_frame_t f;
                    fw_nrf_baro_t b;
                    int used = fw_nrf_take_frame(acc, acc_len, &f);
                    if (used == FW_NRF_MORE)
                        break;
                    if (used > 0) {
                        frames++;
                        baros += fw_nrf_parse_baro(&f, &b) == 0;
                    } else {
                        used = 1;
                    }
                    memmove(acc, acc + used, acc_len - (size_t)used);
                    acc_len -= (size_t)used;
                }
            }
        }
        TEST_ASSERT_EQUAL(4, frames);
        TEST_ASSERT_EQUAL(1, baros);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_is_xmodem);
    RUN_TEST(test_captured_frames_are_accepted);
    RUN_TEST(test_incomplete_and_invalid);
    RUN_TEST(test_barometer);
    RUN_TEST(test_barometer_refuses_what_it_does_not_recognise);
    RUN_TEST(test_accelerometer);
    RUN_TEST(test_clock_decodes_the_capture_date);
    RUN_TEST(test_battery_and_light_candidates);
    RUN_TEST(test_key_event_layout);
    RUN_TEST(test_stream_resynchronises);
    return UNITY_END();
}
