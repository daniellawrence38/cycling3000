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
    /* A percentage above 100 is not a battery reading. */
    uint8_t bad_pct[8] = {0x52, 0xff, 0xff, 0xff, 0xc2, 0x10, 101, 0xff};
    fw_nrf_frame_t fb = {.kind = 4, .id = 0, .payload = bad_pct, .plen = 8};
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_battery(&fb, &b));

    fw_nrf_light_t l;
    fw_nrf_take_frame(F_04, 16, &f);
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_light(&f, &l));
    TEST_ASSERT_EQUAL_UINT32(600u, l.value); /* the factory screen's "Light lux" on this unit */
    TEST_ASSERT_EQUAL(1310, l.aux);
    fw_nrf_take_frame(F_BARO, 16, &f);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_light(&f, &l));

    /* Auto-backlight mapping from the stock table at 0x3c36eb7c, plus 60 % when absent (spec 10.1). */
    TEST_ASSERT_EQUAL(60, fw_nrf_light_backlight_pct(-1));
    TEST_ASSERT_EQUAL(1, fw_nrf_light_backlight_pct(0));
    TEST_ASSERT_EQUAL(1, fw_nrf_light_backlight_pct(10));
    TEST_ASSERT_EQUAL(5, fw_nrf_light_backlight_pct(30));
    TEST_ASSERT_EQUAL(5, fw_nrf_light_backlight_pct(50));
    TEST_ASSERT_EQUAL(10, fw_nrf_light_backlight_pct(600));
    TEST_ASSERT_EQUAL(10, fw_nrf_light_backlight_pct(2000));
    TEST_ASSERT_EQUAL(20, fw_nrf_light_backlight_pct(3000));
    TEST_ASSERT_EQUAL(30, fw_nrf_light_backlight_pct(6000));
    TEST_ASSERT_EQUAL(40, fw_nrf_light_backlight_pct(9000));
    TEST_ASSERT_EQUAL(100, fw_nrf_light_backlight_pct(110000));
}

/* Key frames from outputs/nrf-capture-2.txt (17 November 2026), payloads as logged: power, left and
 * right pressed once each, then power held for two seconds. */
static const uint8_t K_PWR_CLICK[8] = {0x49, 0x00, 0, 0, 0, 0, 0x01, 0x80};
static const uint8_t K_LEFT_CLICK[8] = {0x49, 0x01, 0, 0, 0, 0, 0x01, 0x80};
static const uint8_t K_RIGHT_CLICK[8] = {0x49, 0x02, 0, 0, 0, 0, 0x01, 0x80};
static const uint8_t K_PWR_HELD[8] = {0x49, 0x00, 0, 0, 0, 0, 0x04, 0x80};
static const uint8_t K_PWR_END[8] = {0x49, 0x00, 0, 0, 0, 0, 0x05, 0x80};

static int feed(fw_nrf_keys_t *ks, const uint8_t *payload, uint32_t now, fw_nrf_edge_t *out)
{
    fw_nrf_frame_t f = {.kind = 4, .id = 16, .payload = payload, .plen = 8};
    fw_nrf_key_t k;
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_key(&f, &k));
    return fw_nrf_keys_feed(ks, &k, now, out);
}

static void test_keys_as_captured(void)
{
    fw_nrf_keys_t ks;
    memset(&ks, 0, sizeof ks);
    fw_nrf_edge_t e[3];
    /* A click is a press and a release at once, for each of the three buttons. */
    const uint8_t *clicks[] = {K_PWR_CLICK, K_LEFT_CLICK, K_RIGHT_CLICK};
    for (uint8_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(2, feed(&ks, clicks[i], 56579 + 2000u * i, e));
        TEST_ASSERT_EQUAL(i, e[0].key);
        TEST_ASSERT_EQUAL(1, e[0].down);
        TEST_ASSERT_EQUAL(i, e[1].key);
        TEST_ASSERT_EQUAL(0, e[1].down);
    }
    /* The hold exactly as it arrived: held at 64379 and 64679 ms, released at 64679 ms. */
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_HELD, 64379, e));
    TEST_ASSERT_EQUAL(0, e[0].key);
    TEST_ASSERT_EQUAL(1, e[0].down);
    TEST_ASSERT_EQUAL(0, feed(&ks, K_PWR_HELD, 64679, e)); /* a repeat is not a second press */
    TEST_ASSERT_EQUAL(0, fw_nrf_keys_tick(&ks, 64679, e));
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_END, 64679, e));
    TEST_ASSERT_EQUAL(0, e[0].key);
    TEST_ASSERT_EQUAL(0, e[0].down);
    TEST_ASSERT_EQUAL(0, feed(&ks, K_PWR_END, 64700, e)); /* a second release is nothing */
}

static void test_keys_never_stick(void)
{
    fw_nrf_keys_t ks;
    memset(&ks, 0, sizeof ks);
    fw_nrf_edge_t e[3];
    /* The repeats stop with no hold end: released one second after the last one, not before. */
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_HELD, 1000, e));
    TEST_ASSERT_EQUAL(0, feed(&ks, K_PWR_HELD, 1300, e));
    TEST_ASSERT_EQUAL(0, fw_nrf_keys_tick(&ks, 2300, e));
    TEST_ASSERT_EQUAL(1, fw_nrf_keys_tick(&ks, 2301, e));
    TEST_ASSERT_EQUAL(0, e[0].key);
    TEST_ASSERT_EQUAL(0, e[0].down);
    TEST_ASSERT_EQUAL(0, fw_nrf_keys_tick(&ks, 5000, e));
    /* A long hold stays down as long as the repeats keep coming, across a timer wrap too. */
    uint32_t t = 0xFFFFFF00u;
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_HELD, t, e));
    for (int i = 1; i <= 20; i++) {
        TEST_ASSERT_EQUAL(0, feed(&ks, K_PWR_HELD, t + 300u * (uint32_t)i, e));
        TEST_ASSERT_EQUAL(0, fw_nrf_keys_tick(&ks, t + 300u * (uint32_t)i + 299u, e));
    }
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_END, t + 6300u, e));
    /* A click arriving while a hold is open closes it with one release, not a press. */
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_HELD, 100, e));
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_CLICK, 200, e));
    TEST_ASSERT_EQUAL(0, e[0].down);
    /* Two buttons held together are tracked separately. */
    static const uint8_t K_LEFT_HELD[8] = {0x49, 0x01, 0, 0, 0, 0, 0x04, 0x80};
    TEST_ASSERT_EQUAL(1, feed(&ks, K_PWR_HELD, 10000, e));
    TEST_ASSERT_EQUAL(1, feed(&ks, K_LEFT_HELD, 10100, e));
    TEST_ASSERT_EQUAL(1, e[0].key);
    TEST_ASSERT_EQUAL(1, fw_nrf_keys_tick(&ks, 11050, e)); /* only power has gone quiet */
    TEST_ASSERT_EQUAL(0, e[0].key);
    /* An index or an event code never seen on the unit produces nothing. */
    static const uint8_t K_IDX3[8] = {0x49, 0x03, 0, 0, 0, 0, 0x01, 0x80};
    static const uint8_t K_EVT2[8] = {0x49, 0x00, 0, 0, 0, 0, 0x02, 0x80};
    TEST_ASSERT_EQUAL(0, feed(&ks, K_IDX3, 20000, e));
    TEST_ASSERT_EQUAL(0, feed(&ks, K_EVT2, 20000, e));
}

static void test_key_event_layout(void)
{
    /* The vendor's field positions, on a made-up frame with every byte different. */
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

static void test_build_frame_and_power_off(void)
{
    /* Building each captured frame from its decoded fields reproduces the wire bytes, CRC and all. */
    const uint8_t *all[] = {F_ACCEL, F_BARO, F_CLOCK, F_IDLE, F_04, F_0A};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        fw_nrf_frame_t f;
        TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(all[i], 16, &f));
        uint8_t built[16];
        TEST_ASSERT_EQUAL(16, fw_nrf_build_frame(f.kind, f.id, f.payload, f.plen, built, sizeof built));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(all[i], built, 16);
    }

    /* Every frame the firmware transmits, pinned whole: header, payload AND the two CRC bytes.
     *
     * Where the golden bytes come from. The header is the frame format (docs/NRF-PROTOCOL.md, "The
     * frame"): a5, length = 8 + 4 = 0x0c, 6f, f1, kind 2 (command), id 16 (0x10). The payloads are
     * docs/NRF-PROTOCOL.md, "The senders, exactly" (SendPowerOffCmd, SendPowerOnCmd) and "Additional
     * outbound commands on id 16" (the three GNSS frames). The CRC bytes were NOT taken from the code
     * under test: they were computed with Python's binascii.crc_hqx(data, 0), which is CRC-16/XMODEM,
     * the checksum the vendor's routine at 0x4222137c is (NRF-PROTOCOL.md, "The checksum"), under WSL:
     *
     *   python3 -c 'import binascii; hdr = bytes([0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10]); ...
     *               c = binascii.crc_hqx(hdr + payload, 0)'   (low byte first on the wire)
     *
     *   SendPowerOffCmd  a5 0c 6f f1 02 10 e2 02 00 00 00 00 00 00 f8 1a  crc 0x1af8
     *   SendPowerOnCmd   a5 0c 6f f1 02 10 e2 02 00 00 00 01 00 00 c8 2d  crc 0x2dc8
     *   GNSS off         a5 0c 6f f1 02 10 e2 02 07 00 00 00 00 00 b9 d2  crc 0xd2b9
     *   GNSS on          a5 0c 6f f1 02 10 e2 02 07 00 00 01 00 00 89 e5  crc 0xe589
     *   GNSS reset       a5 0c 6f f1 02 10 e2 02 07 00 00 02 00 00 d9 bc  crc 0xbcd9
     *
     * and crc_hqx(b"123456789", 0) gave 0x31c3, the published CRC-16/XMODEM check value. So a change
     * to the builder, the CRC or a payload constant fails here even if the parser changes with it. */
    static const uint8_t POWER_OFF[16] = {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10, 0xe2, 0x02,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x1a};
    static const uint8_t POWER_ON[16] = {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10, 0xe2, 0x02,
                                         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0xc8, 0x2d};
    static const uint8_t GNSS[3][16] = {
        {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10, 0xe2, 0x02, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb9, 0xd2},
        {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10, 0xe2, 0x02, 0x07, 0x00, 0x00, 0x01, 0x00, 0x00, 0x89, 0xe5},
        {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x10, 0xe2, 0x02, 0x07, 0x00, 0x00, 0x02, 0x00, 0x00, 0xd9, 0xbc},
    };

    /* SendPowerOffCmd (0x42055bac, Q38) */
    uint8_t po[FW_NRF_POWER_OFF_FRAME_LEN];
    TEST_ASSERT_EQUAL(16, fw_nrf_build_power_off(po));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(POWER_OFF, po, 16);
    fw_nrf_frame_t f;
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(po, sizeof po, &f)); /* and our own parser accepts it */
    TEST_ASSERT_EQUAL(8, f.plen);

    /* Bounds: invalid kind, oversized payload, short output buffer or NULL pointers are refused. */
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_frame(0, 16, POWER_OFF + 6, 8, po, sizeof po));
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_frame(6, 16, POWER_OFF + 6, 8, po, sizeof po));
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_frame(2, 16, POWER_OFF + 6, 8, po, 15));
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_frame(2, 16, NULL, 1, po, sizeof po));
    uint8_t big[140] = {0};
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_frame(2, 16, big, 129, big, sizeof big));

    /* SendPowerOnCmd (0x42055c00) */
    uint8_t pon[FW_NRF_CMD_FRAME_LEN];
    TEST_ASSERT_EQUAL(16, fw_nrf_build_power_on(pon));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(POWER_ON, pon, 16);
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(pon, sizeof pon, &f));

    /* GNSS power off, on and reset (0x42155f84, 0x42155f98, 0x42155fac) */
    for (uint8_t m = 0; m <= 2; m++) {
        uint8_t gp[FW_NRF_CMD_FRAME_LEN];
        TEST_ASSERT_EQUAL(16, fw_nrf_build_gnss_power(gp, m));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(GNSS[m], gp, 16);
        TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(gp, sizeof gp, &f));
    }
    TEST_ASSERT_EQUAL(-1, fw_nrf_build_gnss_power(pon, 3));

    /* Power-on event from capture 2 (e2 02 00 00 ff 05 00 ff on id 16). */
    const uint8_t p_on_ev[8] = {0xe2, 0x02, 0x00, 0x00, 0xff, 0x05, 0x00, 0xff};
    fw_nrf_frame_t fev = {.kind = 4, .id = 16, .payload = p_on_ev, .plen = 8};
    TEST_ASSERT_EQUAL(1, fw_nrf_is_power_on_event(&fev));
    /* Event code 4: the documents say only "code 4, the other event" (NRF-PROTOCOL.md); what it
       means has not been seen or decoded. It is not a power-on event, which is all this asserts. */
    const uint8_t p_code4_ev[8] = {0xe2, 0x02, 0x00, 0x00, 0xff, 0x04, 0x00, 0xff};
    fev.payload = p_code4_ev;
    TEST_ASSERT_EQUAL(0, fw_nrf_is_power_on_event(&fev));

    /* SendQuerySlaveModeCmd (0x42055ca8): kind 2, id 1 (0x01), payload 24 01 ff ff 00 ff ff ff (the
     * constant at 0x3c375e70). Its CRC was computed independently, under WSL:
     *
     *   python3 -c 'import binascii
     *   hdr = bytes([0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x01]); payload = bytes([0x24, 0x01, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff])
     *   c = binascii.crc_hqx(hdr + payload, 0) ...'
     *   -> crc 0x2d41  a5 0c 6f f1 02 01 24 01 ff ff 00 ff ff ff 41 2d   (check value 0x31c3) */
    static const uint8_t QUERY[16] = {0xa5, 0x0c, 0x6f, 0xf1, 0x02, 0x01, 0x24, 0x01,
                                      0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x41, 0x2d};
    uint8_t q[FW_NRF_CMD_FRAME_LEN];
    TEST_ASSERT_EQUAL(16, fw_nrf_build_query_slave_mode(q));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(QUERY, q, 16);
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(q, sizeof q, &f));
    TEST_ASSERT_EQUAL(2, f.kind);
    TEST_ASSERT_EQUAL(1, f.id);
    /* Our own query is not mistaken for the reply: its payload[5] is 0xff, a mode that keeps the
       vendor querying, and that is what the parser reports. */
    TEST_ASSERT_EQUAL(0xff, fw_nrf_parse_slave_mode(&f));
}

/* ------------------------------------------------------------------ messages around power */
static fw_nrf_frame_t mk(uint8_t kind, uint8_t id, const uint8_t *p, size_t n)
{
    fw_nrf_frame_t f = {.kind = kind, .id = id, .payload = p, .plen = n};
    return f;
}

/* From the captures. */
static const uint8_t P_BATT2[8] = {0x52, 0xff, 0xff, 0xff, 0x94, 0x0d, 0x02, 0xff}; /* capture 2, 3476 mV 2 % */
static const uint8_t P_CHARGE[8] = {0xf0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t P_EV5[8] = {0xe2, 0x02, 0x00, 0x00, 0xff, 0x05, 0x00, 0xff};   /* capture 2 #427 */
static const uint8_t P_VERSION[10] = {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x11}; /* #428 */
static const uint8_t P_BARO2[8] = {0xf1, 0x03, 0xf9, 0x0b, 0x23, 0x4e, 0x99, 0x00};  /* #429 */
/* Made up: never seen, because nothing has ever sent the query. Only bytes 0, 1 and 5 are the vendor's
   (0x42050fa4 / 0x4214f030); the others are filler. */
static const uint8_t P_SLAVE_APP[8] = {0x24, 0x01, 0xff, 0xff, 0x00, 0x01, 0xff, 0xff};
static const uint8_t P_SLAVE_BOOT[8] = {0x24, 0x01, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff};
static const uint8_t P_SLAVE_BUSY[8] = {0x24, 0x01, 0xff, 0xff, 0x00, 0x02, 0xff, 0xff};
static const uint8_t P_EV4[8] = {0xe2, 0x02, 0x00, 0x00, 0xff, 0x04, 0x00, 0xff};
static const uint8_t P_EV6[8] = {0xe2, 0x02, 0x00, 0x00, 0xff, 0x06, 0x00, 0xff};

static void test_power_messages(void)
{
    fw_nrf_frame_t f;
    /* Charge state: f0 02 00 ..., the very frame of both captures with the cable in (raw #6098 of 4
       November is F_IDLE). 0 is "Chargeing" on the vendor's factory screen. */
    TEST_ASSERT_EQUAL(16, fw_nrf_take_frame(F_IDLE, 16, &f));
    TEST_ASSERT_EQUAL(1, fw_nrf_parse_charge(&f));
    uint8_t nc[8] = {0xf0, 0x02, 0x01, 0, 0, 0, 0, 0};
    f = mk(4, 16, nc, 8);
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_charge(&f)); /* "No Charge": the vendor's text, never seen */
    f = mk(4, 0, P_CHARGE, 8);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_charge(&f));
    f = mk(4, 16, P_BARO2, 8);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_charge(&f));

    /* Power event codes; the device byte is not looked at, as in the vendor's handler. */
    f = mk(4, 16, P_EV5, 8);
    TEST_ASSERT_EQUAL(5, fw_nrf_power_event_code(&f));
    f = mk(4, 16, P_EV4, 8);
    TEST_ASSERT_EQUAL(4, fw_nrf_power_event_code(&f));
    TEST_ASSERT_EQUAL(0, fw_nrf_is_power_on_event(&f));
    f = mk(4, 16, P_EV6, 8);
    TEST_ASSERT_EQUAL(1, fw_nrf_is_power_on_event(&f));
    f = mk(4, 1, P_EV5, 8);
    TEST_ASSERT_EQUAL(-1, fw_nrf_power_event_code(&f));
    f = mk(4, 16, P_CHARGE, 8);
    TEST_ASSERT_EQUAL(-1, fw_nrf_power_event_code(&f));

    /* Slave mode. */
    f = mk(4, 1, P_SLAVE_APP, 8);
    TEST_ASSERT_EQUAL(FW_NRF_SLAVE_APP, fw_nrf_parse_slave_mode(&f));
    f = mk(4, 1, P_SLAVE_BOOT, 8);
    TEST_ASSERT_EQUAL(FW_NRF_SLAVE_BOOTLOADER, fw_nrf_parse_slave_mode(&f));
    f = mk(4, 16, P_SLAVE_APP, 8);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_slave_mode(&f));
    f = mk(4, 1, P_VERSION, 10);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_slave_mode(&f));

    /* The power-on version message decodes as 1.704. */
    fw_nrf_version_t v;
    TEST_ASSERT_EQUAL(0, fw_nrf_parse_version(&f, &v));
    TEST_ASSERT_EQUAL(1, v.major);
    TEST_ASSERT_EQUAL(7, v.minor);
    TEST_ASSERT_EQUAL(4, v.patch);
    f = mk(4, 1, P_VERSION, 9);
    TEST_ASSERT_EQUAL(-1, fw_nrf_parse_version(&f, &v));
}

/* ------------------------------------------------------------------ the boot handshake */
typedef struct {
    int query, power_on, gnss_on;
    uint32_t last_query_ms, last_power_on_ms, gnss_on_ms;
    uint32_t min_query_gap, min_power_on_gap;
} txlog_t;

static void txlog_init(txlog_t *l)
{
    memset(l, 0, sizeof *l);
    l->min_query_gap = l->min_power_on_gap = 0xFFFFFFFFu;
}

static void count(txlog_t *l, int tx, uint32_t t)
{
    switch (tx) {
    case FW_NRF_HS_TX_QUERY:
        if (l->query && t - l->last_query_ms < l->min_query_gap)
            l->min_query_gap = t - l->last_query_ms;
        l->query++;
        l->last_query_ms = t;
        break;
    case FW_NRF_HS_TX_POWER_ON:
        if (l->power_on && t - l->last_power_on_ms < l->min_power_on_gap)
            l->min_power_on_gap = t - l->last_power_on_ms;
        l->power_on++;
        l->last_power_on_ms = t;
        break;
    case FW_NRF_HS_TX_GNSS_ON:
        l->gnss_on++;
        l->gnss_on_ms = t;
        break;
    default:
        TEST_ASSERT_EQUAL(FW_NRF_HS_TX_NONE, tx);
    }
}

/* Runs the handshake in 10 ms steps from t0 to t1 (as the nRF task's read loop would), feeding a frame
 * from the given set every 300 ms -- the nRF's own heartbeat rate in capture 2. */
static void run(fw_nrf_hs_t *hs, uint32_t t0, uint32_t t1, const fw_nrf_frame_t *beat, size_t nbeat, txlog_t *l)
{
    for (uint32_t t = t0; t != t1; t += 10) {
        if (nbeat && (t - t0) % 300 == 0)
            for (size_t i = 0; i < nbeat; i++)
                count(l, fw_nrf_hs_step(hs, t, &beat[i]), t);
        count(l, fw_nrf_hs_step(hs, t, NULL), t);
    }
}

static void feed1(fw_nrf_hs_t *hs, uint32_t t, fw_nrf_frame_t f, txlog_t *l) { count(l, fw_nrf_hs_step(hs, t, &f), t); }

/* The ordinary case: the nRF answers, confirms power-on with its event, and the GNSS gets one power-on. */
static void test_handshake_normal(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 1000);
    fw_nrf_frame_t beat[2] = {mk(4, 0, P_BATT2, 8), mk(4, 16, P_CHARGE, 8)};
    run(&hs, 1000, 1700, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_QUERY, hs.state);
    TEST_ASSERT_EQUAL(4, l.query); /* 1000, 1200, 1400, 1600 */
    TEST_ASSERT_EQUAL(200, l.min_query_gap);
    TEST_ASSERT_EQUAL(0, l.power_on);
    /* The reply says "in application": power-on goes out at once, in the same call. */
    feed1(&hs, 1700, mk(4, 1, P_SLAVE_APP, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_POWER_ON, hs.state);
    TEST_ASSERT_EQUAL(1, l.power_on);
    run(&hs, 1710, 2000, beat, 2, &l);
    TEST_ASSERT_EQUAL(3, l.power_on); /* 1700, 1800, 1900 */
    TEST_ASSERT_EQUAL(100, l.min_power_on_gap);
    TEST_ASSERT_EQUAL(4, l.query);    /* no more queries once answered */
    /* The power event, then the version message and the first barometer frame, as in capture 2. */
    feed1(&hs, 2000, mk(4, 16, P_EV5, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_RUNNING, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_ON_MANUAL, hs.on);
    TEST_ASSERT_EQUAL(1, l.gnss_on); /* the rail is the nRF's, so it is asked for only now */
    TEST_ASSERT_EQUAL(3, l.power_on);
    feed1(&hs, 2000, mk(4, 1, P_VERSION, 10), &l);
    feed1(&hs, 2000, mk(4, 16, P_BARO2, 8), &l);
    /* An hour of ordinary traffic, more events and more slave replies: nothing more is sent. */
    fw_nrf_frame_t later[4] = {mk(4, 16, P_BARO2, 8), mk(4, 16, P_EV5, 8), mk(4, 1, P_SLAVE_APP, 8),
                               mk(4, 1, P_SLAVE_BOOT, 8)};
    run(&hs, 2010, 2010 + 3600000u, later, 4, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_RUNNING, hs.state);
    TEST_ASSERT_EQUAL(4, l.query);
    TEST_ASSERT_EQUAL(3, l.power_on);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
}

/* Capture 2: the nRF in its "off" state, sending only the battery and f0 02 every 300 ms, and in this
 * test also never answering the power-on. The handshake must give up after 5 s, send nothing more, and
 * still follow the rider's power key into RUNNING without sending anything but the one GNSS power-on. */
static void test_handshake_off_state(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 0);
    fw_nrf_frame_t beat[2] = {mk(4, 0, P_BATT2, 8), mk(4, 16, P_CHARGE, 8)};
    run(&hs, 0, 300, beat, 2, &l);
    feed1(&hs, 300, mk(4, 1, P_SLAVE_APP, 8), &l);
    run(&hs, 300, 300 + 5000, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_POWER_ON, hs.state);
    run(&hs, 5300, 5310, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAIL_NO_POWER_EVENT, hs.fail);
    TEST_ASSERT_EQUAL(50, l.power_on); /* 5 s at 100 ms, and not one more */
    TEST_ASSERT_EQUAL(100, l.min_power_on_gap);
    int q = l.query;
    TEST_ASSERT_EQUAL(2, q); /* at 0 and 200 */
    /* The rest of capture 2's 64 s of "off": nothing is sent. */
    run(&hs, 5310, 64380, beat, 2, &l);
    TEST_ASSERT_EQUAL(50, l.power_on);
    TEST_ASSERT_EQUAL(q, l.query);
    TEST_ASSERT_EQUAL(0, l.gnss_on);
    /* The power key held and released, then the power event and the sensors (64379..64679 ms). */
    feed1(&hs, 64380, mk(4, 16, K_PWR_HELD, 8), &l);
    feed1(&hs, 64680, mk(4, 16, K_PWR_HELD, 8), &l);
    feed1(&hs, 64680, mk(4, 16, K_PWR_END, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    feed1(&hs, 64680, mk(4, 16, P_EV5, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_RUNNING, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_ON_MANUAL, hs.on);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
    fw_nrf_frame_t on[3] = {mk(4, 16, P_BARO2, 8), mk(4, 0, P_BATT2, 8), mk(4, 16, P_CHARGE, 8)};
    run(&hs, 64690, 64690 + 600000u, on, 3, &l);
    TEST_ASSERT_EQUAL(50, l.power_on);
    TEST_ASSERT_EQUAL(q, l.query);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
}

/* The nRF talks but never answers the query: 50 queries over 10 s, then silence for good. */
static void test_handshake_no_reply(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 0);
    fw_nrf_frame_t beat[2] = {mk(4, 0, P_BATT2, 8), mk(4, 16, P_CHARGE, 8)};
    run(&hs, 0, 10000, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_QUERY, hs.state);
    TEST_ASSERT_EQUAL(50, l.query);
    run(&hs, 10000, 10000 + 3600000u, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAIL_NO_REPLY, hs.fail);
    TEST_ASSERT_EQUAL(50, l.query);
    TEST_ASSERT_EQUAL(0, l.power_on);
    TEST_ASSERT_EQUAL(0, l.gnss_on);
    /* A late reply does not restart anything: power-on is never sent without an answered query in time. */
    feed1(&hs, 3700000u, mk(4, 1, P_SLAVE_APP, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    /* A reply with a mode of 2 or more keeps the vendor querying, and keeps ours querying too. */
    fw_nrf_hs_init(&hs, 0);
    txlog_init(&l);
    run(&hs, 0, 1000, beat, 2, &l);
    feed1(&hs, 1000, mk(4, 1, P_SLAVE_BUSY, 8), &l);
    run(&hs, 1000, 2000, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_QUERY, hs.state);
    TEST_ASSERT_EQUAL(10, l.query);
}

/* A silent link: not one frame is sent into it. */
static void test_handshake_silent_link(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 500);
    run(&hs, 500, 500 + 60000u, NULL, 0, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAIL_NO_REPLY, hs.fail);
    TEST_ASSERT_EQUAL(0, l.query + l.power_on + l.gnss_on);
}

/* Bootloader: stop at once and for good, whatever arrives afterwards. */
static void test_handshake_bootloader(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 0);
    fw_nrf_frame_t beat[1] = {mk(4, 0, P_BATT2, 8)};
    run(&hs, 0, 500, beat, 1, &l);
    TEST_ASSERT_EQUAL(3, l.query);
    feed1(&hs, 500, mk(4, 1, P_SLAVE_BOOT, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAIL_BOOTLOADER, hs.fail);
    fw_nrf_frame_t later[3] = {mk(4, 1, P_SLAVE_APP, 8), mk(4, 16, P_EV5, 8), mk(4, 16, P_BARO2, 8)};
    run(&hs, 500, 500 + 600000u, later, 3, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(3, l.query);
    TEST_ASSERT_EQUAL(0, l.power_on + l.gnss_on);
}

/* The 4 November case: the sensors already stream when the firmware boots, and (in this test) no event
 * answers the power-on. One second of power-on, then the sensors are the confirmation. */
static void test_handshake_sensors_without_event(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 0);
    fw_nrf_frame_t beat[2] = {mk(4, 16, P_BARO2, 8), mk(4, 0, P_BATT2, 8)};
    run(&hs, 0, 200, beat, 2, &l);
    feed1(&hs, 200, mk(4, 1, P_SLAVE_APP, 8), &l);
    run(&hs, 200, 1200, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_POWER_ON, hs.state);
    run(&hs, 1200, 1210, beat, 2, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_RUNNING, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_ON_SENSORS, hs.on);
    TEST_ASSERT_EQUAL(10, l.power_on); /* 200 .. 1100 */
    TEST_ASSERT_EQUAL(1, l.gnss_on);
    run(&hs, 1210, 61210, beat, 2, &l);
    TEST_ASSERT_EQUAL(10, l.power_on);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
}

/* Code 4, woken by the charger: power-on stops (the vendor's loop ends on it too), but the GNSS waits for
 * evidence that the sensor side is on. */
static void test_handshake_charger(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    fw_nrf_hs_init(&hs, 0);
    fw_nrf_frame_t beat[2] = {mk(4, 0, P_BATT2, 8), mk(4, 16, P_CHARGE, 8)};
    run(&hs, 0, 300, beat, 2, &l);
    feed1(&hs, 300, mk(4, 1, P_SLAVE_APP, 8), &l);
    run(&hs, 300, 600, beat, 2, &l);
    feed1(&hs, 600, mk(4, 16, P_EV4, 8), &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_RUNNING, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_ON_CHARGER, hs.on);
    int p = l.power_on;
    TEST_ASSERT_EQUAL(3, p);
    run(&hs, 600, 600 + 600000u, beat, 2, &l);
    TEST_ASSERT_EQUAL(p, l.power_on);
    TEST_ASSERT_EQUAL(0, l.gnss_on);
    feed1(&hs, 700000u, mk(4, 16, P_BARO2, 8), &l);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
    run(&hs, 700010u, 760010u, beat, 2, &l);
    TEST_ASSERT_EQUAL(1, l.gnss_on);
}

/* Stopped for a power-off: nothing more, in any state, from any frame; and the clock may wrap. */
static void test_handshake_stop_and_wrap(void)
{
    fw_nrf_hs_t hs;
    txlog_t l;
    txlog_init(&l);
    uint32_t t0 = 0xFFFFF000u; /* 4 s before the millisecond counter wraps */
    fw_nrf_hs_init(&hs, t0);
    fw_nrf_frame_t beat[1] = {mk(4, 0, P_BATT2, 8)};
    run(&hs, t0, t0 + 10000u, beat, 1, &l);
    TEST_ASSERT_EQUAL(50, l.query); /* the wrap changes nothing */
    TEST_ASSERT_EQUAL(FW_NRF_HS_QUERY, hs.state);
    run(&hs, t0 + 10000u, t0 + 10010u, beat, 1, &l);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);

    fw_nrf_hs_init(&hs, 0);
    txlog_init(&l);
    run(&hs, 0, 100, beat, 1, &l);
    feed1(&hs, 100, mk(4, 1, P_SLAVE_APP, 8), &l);
    TEST_ASSERT_EQUAL(1, l.power_on);
    fw_nrf_hs_stop(&hs);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAILED, hs.state);
    TEST_ASSERT_EQUAL(FW_NRF_HS_FAIL_STOPPED, hs.fail);
    fw_nrf_frame_t later[3] = {mk(4, 1, P_SLAVE_APP, 8), mk(4, 16, P_EV5, 8), mk(4, 16, P_BARO2, 8)};
    run(&hs, 110, 60110, later, 3, &l);
    TEST_ASSERT_EQUAL(1, l.power_on);
    TEST_ASSERT_EQUAL(0, l.gnss_on);
}

/* ------------------------------------------------------------------ the wake decision */
static int wake(fw_nrf_wake_t *w, const uint8_t *p, uint32_t t)
{
    fw_nrf_frame_t f = mk(4, 16, p, 8);
    return fw_nrf_wake_feed(w, &f, t);
}

static void test_wake_needs_a_fresh_press(void)
{
    fw_nrf_wake_t w;
    /* Power-off sent at 10000 ms during the 2 s hold: the last key-0 frame before it was HELD. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HELD, 9900);
    /* The same hold goes on. The HELD repeats pause for 1.5 s (longer than FW_NRF_KEY_LOST_MS, after
       which the key decoder would have released the button) and then resume: none of it wakes. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_PWR_HELD, 10200));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_PWR_HELD, 11700));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_PWR_HELD, 12000));
    /* Other buttons never wake. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_LEFT_CLICK, 12100));
    /* A power-on event before the hold has ended belongs to it. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT_IGNORED, wake(&w, P_EV5, 12200));
    /* The release, then -- as in capture 2 -- the nRF's power-on event in the same tick: not a wake. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_RELEASED, wake(&w, K_PWR_END, 12300));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT_IGNORED, wake(&w, P_EV5, 12300));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT_IGNORED, wake(&w, P_EV6, 12300 + FW_NRF_WAKE_SAME_HOLD_MS));
    /* A second release is not a press. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_PWR_END, 12400));
    /* Charger wake: reported, never a wake. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_CHARGER, wake(&w, P_EV4, 13000));
    /* A new press: a hold's first HELD frame wakes... */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_KEY, wake(&w, K_PWR_HELD, 20000));
    /* ...and so does a click. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HELD, 9900);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_RELEASED, wake(&w, K_PWR_END, 10100));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_KEY, wake(&w, K_PWR_CLICK, 30000));
}

static void test_wake_other_paths(void)
{
    fw_nrf_wake_t w;
    /* The hold had already ended when the power-off was sent: the next press wakes. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HOLD_END, 9990);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_KEY, wake(&w, K_PWR_CLICK, 15000));
    /* Same, and the hold's own power event arrives just after the send: still that hold's. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HOLD_END, 9990);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT_IGNORED, wake(&w, P_EV5, 10010));
    /* A manual power-on event well after the release wakes. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT, wake(&w, P_EV5, 9990 + FW_NRF_WAKE_SAME_HOLD_MS + 1));
    /* A lost HOLD_END: the first click only ends the hold, the second wakes. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HELD, 9900);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_RELEASED, wake(&w, K_PWR_CLICK, 40000));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_KEY, wake(&w, K_PWR_CLICK, 41000));
    /* No key-0 frame at all before the send (cannot happen from a hold, but is handled): not released. */
    fw_nrf_wake_init(&w, 0, 0);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, K_PWR_HELD, 100));
    /* Non-key, non-event frames are nothing. */
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, P_BARO2, 200));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_NONE, wake(&w, P_CHARGE, 200));
    /* Across the millisecond wrap. */
    fw_nrf_wake_init(&w, FW_NRF_KEY_HOLD_END, 0xFFFFFF00u);
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT_IGNORED, wake(&w, P_EV5, 0x00000010u));
    TEST_ASSERT_EQUAL(FW_NRF_WAKE_EVENT, wake(&w, P_EV5, 0x00000200u));
}

/* ------------------------------------------------------------------ the GNSS watchdog */
static void test_gnss_watchdog(void)
{
    fw_gnss_wd_t w;
    fw_gnss_wd_init(&w);
    /* Not armed (GNSS power-on not sent yet): never a reset. */
    for (uint32_t t = 0; t < 60000; t += 100)
        TEST_ASSERT_EQUAL(FW_GNSS_WD_NONE, fw_gnss_wd_tick(&w, t));
    fw_gnss_wd_arm(&w, 60000);
    /* A talking receiver is never reset. */
    for (uint32_t t = 60000; t < 120000; t += 100) {
        fw_gnss_wd_bytes(&w, t);
        TEST_ASSERT_EQUAL(FW_GNSS_WD_NONE, fw_gnss_wd_tick(&w, t + 50));
    }
    /* Silence from 119900: a reset at 5.1 s, then no sooner than every 5.1 s, three in all, then one
       "gave up", then nothing, however long the silence lasts. */
    int resets = 0, gave_up = 0;
    uint32_t at[4] = {0};
    for (uint32_t t = 120000; t < 120000 + 3600000u; t += 100) {
        int r = fw_gnss_wd_tick(&w, t);
        if (r == FW_GNSS_WD_RESET)
            at[resets++] = t;
        else if (r == FW_GNSS_WD_GAVE_UP)
            gave_up++;
    }
    TEST_ASSERT_EQUAL(FW_GNSS_WD_MAX_RESETS, resets);
    TEST_ASSERT_EQUAL(1, gave_up);
    TEST_ASSERT_EQUAL_UINT32(119900u + 5100u, at[0]);
    TEST_ASSERT_EQUAL_UINT32(at[0] + 5100u, at[1]);
    TEST_ASSERT_EQUAL_UINT32(at[1] + 5100u, at[2]);
    /* The cap is per boot: bytes and a new silence do not give the resets back. */
    fw_gnss_wd_bytes(&w, 4000000u);
    for (uint32_t t = 4000000u; t < 4100000u; t += 100)
        TEST_ASSERT_EQUAL(FW_GNSS_WD_NONE, fw_gnss_wd_tick(&w, t));
    /* Just under and at the threshold, across the wrap; arming twice does not restart the count. */
    fw_gnss_wd_init(&w);
    fw_gnss_wd_arm(&w, 0xFFFFF000u);
    fw_gnss_wd_arm(&w, 0xFFFFF800u);
    TEST_ASSERT_EQUAL(FW_GNSS_WD_NONE, fw_gnss_wd_tick(&w, 0xFFFFF000u + 5099u));
    TEST_ASSERT_EQUAL(FW_GNSS_WD_RESET, fw_gnss_wd_tick(&w, 0xFFFFF000u + 5100u));
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
    RUN_TEST(test_keys_as_captured);
    RUN_TEST(test_keys_never_stick);
    RUN_TEST(test_stream_resynchronises);
    RUN_TEST(test_build_frame_and_power_off);
    RUN_TEST(test_power_messages);
    RUN_TEST(test_handshake_normal);
    RUN_TEST(test_handshake_off_state);
    RUN_TEST(test_handshake_no_reply);
    RUN_TEST(test_handshake_silent_link);
    RUN_TEST(test_handshake_bootloader);
    RUN_TEST(test_handshake_sensors_without_event);
    RUN_TEST(test_handshake_charger);
    RUN_TEST(test_handshake_stop_and_wrap);
    RUN_TEST(test_wake_needs_a_fresh_press);
    RUN_TEST(test_wake_other_paths);
    RUN_TEST(test_gnss_watchdog);
    return UNITY_END();
}
