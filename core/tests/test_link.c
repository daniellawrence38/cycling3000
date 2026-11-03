/* WP3: V§5 framing round trips, every protocol request with OK and each listed error, chunks of 512
 * and 4000 bytes, atomic object commit under fault injection, and ride retention (spec 10.4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/link.h"
#include "cc700/nav.h"
#include "cc700/store.h"
#include "cc700/util.h"
#include "cc700/workout.h"
#include "memfs.h"
#include "support.h"
#include "unity.h"

static hal_fs_t *fs;
static cc_link_t lk;
static int h_state, h_batt = 68;
static uint16_t h_ftp = 200;
static uint32_t h_time;
static int16_t h_tz;
static int h_deleted_kind;

static int hs(void *c) { (void)c; return h_state; }
static int hb(void *c) { (void)c; return h_batt; }
static uint16_t hg(void *c) { (void)c; return h_ftp; }
static int hf(void *c, uint16_t f) { (void)c; if (h_state) return -1; h_ftp = f; return 0; }
static void ht(void *c, uint32_t u, int16_t tz) { (void)c; h_time = u; h_tz = tz; }
static void hd(void *c, int k, const uint8_t id[8]) { (void)c; (void)id; h_deleted_kind = k; }

/* Collected packets from the server */
static uint8_t txbuf[200000];
static size_t txlen;
static int txpkts;
static void tx(void *c, const uint8_t *p, size_t n)
{
    (void)c;
    memcpy(txbuf + txlen, p, n);
    txlen += n;
    txpkts++;
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
static uint8_t w1[300];
static size_t w1n;

void setUp(void)
{
    fs = memfs_new();
    h_state = 0;
    h_batt = 68;
    h_ftp = 200;
    cc_link_host_t host = {hs, hb, hg, hf, ht, hd, NULL};
    cc_link_init(&lk, fs, &host, tx, NULL, 23);
    txlen = 0;
    txpkts = 0;
    w1n = hex_to_bytes(W1_HEX, w1, sizeof w1);
}
void tearDown(void) { memfs_free(fs); }

/* Sends a message through framing and returns the reassembled response */
static uint8_t resp[CC_LINK_MSG_MAX];
static size_t resp_n;
static void feed_pkt(void *c, const uint8_t *p, size_t n) { (void)c; cc_link_rx(&lk, p, n, 1000); }

static void call(const uint8_t *msg, size_t n)
{
    txlen = 0;
    txpkts = 0;
    cc_link_fragment(msg, n, lk.data_cap, feed_pkt, NULL);
    cc_reasm_t r;
    cc_reasm_init(&r);
    size_t off = 0;
    resp_n = 0;
    while (off < txlen) {
        size_t plen = txlen - off < lk.data_cap + 1 ? txlen - off : lk.data_cap + 1;
        /* packets were appended back to back; recover their boundaries from END/START */
        if (cc_reasm_feed(&r, txbuf + off, plen, 1000)) {
            memcpy(resp, r.buf, r.len);
            resp_n = r.len;
        }
        off += plen;
    }
}

static uint8_t st(void) { return resp_n >= 3 ? resp[2] : 0xFF; }

/* ------------------------------------------------------------------ V§5 */
typedef struct { uint8_t p[16][260]; size_t n[16]; int k; } pkts_t;
static void collect(void *c, const uint8_t *p, size_t n)
{
    pkts_t *P = c;
    if (P->k < 16) { memcpy(P->p[P->k], p, n); P->n[P->k] = n; }
    P->k++;
}

static void check_frag(const char *hex, int p19, int p243)
{
    uint8_t m[64];
    size_t n = hex_to_bytes(hex, m, sizeof m);
    static pkts_t P;
    P.k = 0;
    TEST_ASSERT_EQUAL_INT(p19, cc_link_fragment(m, n, 19, collect, &P));
    cc_reasm_t r;
    cc_reasm_init(&r);
    int done = 0;
    for (int i = 0; i < P.k; i++)
        done = cc_reasm_feed(&r, P.p[i], P.n[i], 0);
    TEST_ASSERT_TRUE(done);
    TEST_ASSERT_EQUAL_size_t(n, r.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(m, r.buf, n);
    P.k = 0;
    TEST_ASSERT_EQUAL_INT(p243, cc_link_fragment(m, n, 243, collect, &P));
}

static void test_v5_messages(void)
{
    check_frag("01 01 01", 1, 1);
    check_frag("81 01 00 01 00 01 00 44 00 0d 07", 1, 1);
    check_frag("03 02 04 01", 1, 1);
    check_frag("83 02 00", 1, 1);
    check_frag("11 03 01 18 12 24 0e 18 09 c0 11 e3 00 00 00 8e 1a 20 09", 1, 1);
    check_frag("13 05", 1, 1);

    /* Server responses with the exact bytes */
    for (int i = 0; i < 7; i++) { /* 7 workouts and 3 routes stored: 13 and 7 free */
        char p[64];
        uint8_t id[8] = {0, 0, 0, 0, 0, 0, 0, (uint8_t)(i + 1)};
        cc_object_path(p, sizeof p, id, 1);
        memfs_put(fs, p, w1, (uint32_t)w1n);
        if (i < 3) { cc_object_path(p, sizeof p, id, 2); memfs_put(fs, p, "x", 1); }
    }
    uint8_t hello[] = {0x01, 0x01, 0x01};
    call(hello, 3);
    uint8_t want[11];
    hex_to_bytes("81 01 00 01 00 01 00 44 00 0d 07", want, 11);
    TEST_ASSERT_EQUAL_size_t(11, resp_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, resp, 11);
    uint8_t ftp[] = {0x03, 0x02, 0x04, 0x01};
    call(ftp, 4);
    TEST_ASSERT_EQUAL_size_t(3, resp_n);
    TEST_ASSERT_EQUAL_HEX8(0x83, resp[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, resp[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, resp[2]);
    TEST_ASSERT_EQUAL_UINT16(260, h_ftp);
}

static void test_v5_put_data_packets(void)
{
    uint8_t m[300];
    m[0] = 0x12; m[1] = 0x04;
    cc_put_u32(m + 2, 0);
    memcpy(m + 6, w1, w1n);
    size_t n = 6 + w1n;
    TEST_ASSERT_EQUAL_size_t(233, n);
    static pkts_t P;
    P.k = 0;
    TEST_ASSERT_EQUAL_INT(13, cc_link_fragment(m, n, 19, collect, &P));
    uint8_t hdrs[13];
    for (int i = 0; i < 13; i++) hdrs[i] = P.p[i][0];
    uint8_t want_h[13];
    hex_to_bytes("80 01 02 03 04 05 06 07 08 09 0a 0b 4c", want_h, 13);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_h, hdrs, 13);
    uint8_t p0[20], p1[20], pl[6];
    hex_to_bytes("80 12 04 00 00 00 00 43 57 01 00 18 12 24 0e 18 09 c0 11 f0", p0, 20);
    hex_to_bytes("01 0f 00 00 0a 13 34 20 78 20 38 20 6d 69 6e 20 74 68 72 65", p1, 20);
    hex_to_bytes("4c 20 64 6f 77 6e", pl, 6);
    TEST_ASSERT_EQUAL_size_t(20, P.n[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p0, P.p[0], 20);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p1, P.p[1], 20);
    TEST_ASSERT_EQUAL_size_t(6, P.n[12]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pl, P.p[12], 6);
    P.k = 0;
    TEST_ASSERT_EQUAL_INT(1, cc_link_fragment(m, n, 243, collect, &P));

    /* The full upload, and the reply bytes `92 04 00 e3 00 00 00` */
    uint8_t begin[19];
    hex_to_bytes("11 03 01 18 12 24 0e 18 09 c0 11 e3 00 00 00 8e 1a 20 09", begin, 19);
    call(begin, 19);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    call(m, n);
    TEST_ASSERT_EQUAL_INT(1, txpkts);
    uint8_t want[7];
    hex_to_bytes("92 04 00 e3 00 00 00", want, 7);
    TEST_ASSERT_EQUAL_size_t(7, resp_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, resp, 7);
    uint8_t end[2] = {0x13, 0x05};
    call(end, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_TRUE(memfs_exists(fs, "objects/1812240e1809c011.cw1"));
    TEST_ASSERT_FALSE(memfs_exists(fs, "objects/1812240e1809c011.tmp"));
    /* A second BEGIN for the same object: EXISTS */
    call(begin, 19);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_EXISTS, st());

    /* 4000-byte chunk: 4006-byte message, 17 packets at MTU 247 */
    static uint8_t big[4006];
    big[0] = 0x12; big[1] = 0x06;
    P.k = 0;
    TEST_ASSERT_EQUAL_INT(17, cc_link_fragment(big, 4006, 243, collect, &P));
}

static void test_v5_bad_crc(void)
{
    uint8_t bad[300];
    memcpy(bad, w1, w1n);
    bad[100] ^= 1;
    TEST_ASSERT_EQUAL_HEX32(0xF5C8CCE3u, cc_crc32(bad, w1n));
    uint8_t begin[19];
    hex_to_bytes("11 03 01 18 12 24 0e 18 09 c0 11 e3 00 00 00 8e 1a 20 09", begin, 19);
    call(begin, 19);
    uint8_t m[300] = {0x12, 0x04};
    cc_put_u32(m + 2, 0);
    memcpy(m + 6, bad, w1n);
    call(m, 6 + w1n);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    uint8_t end[2] = {0x13, 0x05};
    call(end, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_CRC, st());
    TEST_ASSERT_EQUAL_INT(0, memfs_count(fs, "objects/"));
}

/* ------------------------------------------------------------------ reassembly rules */
static void test_reassembly_drops(void)
{
    cc_reasm_t r;
    cc_reasm_init(&r);
    uint8_t a[] = {0x80, 1, 2}, b_bad[] = {0x02, 3}, b[] = {0x41, 3}, lone[] = {0x41, 9};
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, lone, 2, 0));      /* no START when idle */
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, a, 3, 0));
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, b_bad, 2, 10));    /* counter mismatch drops */
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, b, 2, 20));        /* nothing to continue */
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, a, 3, 100));
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, b, 2, 2101));      /* 2.001 s gap */
    TEST_ASSERT_FALSE(cc_reasm_feed(&r, a, 3, 3000));
    TEST_ASSERT_TRUE(cc_reasm_feed(&r, b, 2, 5000));       /* exactly 2 s: kept */
    TEST_ASSERT_EQUAL_size_t(3, r.len);
    /* Over 4096 bytes */
    uint8_t pk[244];
    memset(pk, 0, sizeof pk);
    pk[0] = 0x80;
    cc_reasm_feed(&r, pk, 244, 6000);
    int got = 0;
    for (int i = 1; i < 20; i++) {
        pk[0] = (uint8_t)(i & 0x3F);
        got |= cc_reasm_feed(&r, pk, 244, 6000);
    }
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_FALSE(r.active);
}

/* ------------------------------------------------------------------ every request */
static void put_object(const uint8_t *obj, size_t n, int kind, size_t chunk)
{
    uint8_t m[CC_LINK_MSG_MAX];
    m[0] = 0x11; m[1] = 7; m[2] = (uint8_t)kind;
    memcpy(m + 3, obj + 4, 8);
    cc_put_u32(m + 11, (uint32_t)n);
    cc_put_u32(m + 15, cc_crc32(obj, n));
    call(m, 19);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    for (size_t off = 0; off < n; off += chunk) {
        size_t take = n - off < chunk ? n - off : chunk;
        m[0] = 0x12; m[1] = 8;
        cc_put_u32(m + 2, (uint32_t)off);
        memcpy(m + 6, obj + off, take);
        call(m, 6 + take);
        TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
        TEST_ASSERT_EQUAL_UINT32(off + take, cc_get_u32(resp + 3));
    }
    m[0] = 0x13; m[1] = 9;
    call(m, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
}

static size_t make_route(uint8_t *out, size_t cap, int npts, uint16_t nprof)
{
    static int32_t lat[8000], lon[8000];
    static int16_t prof[12000];
    for (int i = 0; i < npts; i++) {
        lat[i] = -319640000 + i * 4500 + (i * 7919) % 300; /* irregular deltas: 2- and 3-byte varints */
        lon[i] = 1158300000 + i * 5300 - (i * 104729) % 700;
    }
    for (int i = 0; i < nprof; i++) prof[i] = (int16_t)(200 + (i % 50));
    return cc_cr1_encode("Big", lat, lon, (uint16_t)npts, 40000, 300, prof, nprof, 25, NULL, 0, NULL, 0, out, cap);
}

static void test_put_chunks_512_and_4000(void)
{
    static uint8_t obj[CC_RT_OBJ_MAX];
    size_t n = make_route(obj, sizeof obj, 8000, 12000);
    TEST_ASSERT_TRUE(n > 40000 && n <= CC_RT_OBJ_MAX);
    put_object(obj, n, 2, 512);
    TEST_ASSERT_EQUAL_INT(1, cc_objects_count(fs, 2));
    memfs_reset(fs);
    cc_link_set_mtu(&lk, 247);
    put_object(obj, n, 2, 4000);
    TEST_ASSERT_EQUAL_INT(1, cc_objects_count(fs, 2));
    /* 4001 bytes of data: BAD_ARG */
    static uint8_t m[CC_LINK_MSG_MAX];
    memfs_reset(fs);
    m[0] = 0x11; m[1] = 1; m[2] = 2;
    memcpy(m + 3, obj + 4, 8);
    cc_put_u32(m + 11, (uint32_t)n);
    cc_put_u32(m + 15, cc_crc32(obj, n));
    call(m, 19);
    m[0] = 0x12;
    cc_put_u32(m + 2, 0);
    memcpy(m + 6, obj, 4001);
    call(m, 6 + 4001);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
}

static void make_ride(uint32_t id, uint32_t size, int uploaded)
{
    static uint8_t data[9000];
    for (uint32_t i = 0; i < size && i < sizeof data; i++) data[i] = (uint8_t)(i * 31 + id);
    char p[64];
    cc_ride_path(p, sizeof p, id, ".fit");
    memfs_put(fs, p, data, size);
    cc_ride_meta_t m;
    memset(&m, 0, sizeof m);
    m.ride_id = id;
    m.size = size;
    m.crc32 = cc_crc32(data, size);
    m.sum.timer_s = 3600;
    m.sum.distance_m = 30000;
    TEST_ASSERT_EQUAL_INT(0, cc_ride_meta_write(fs, &m));
    if (uploaded) cc_ride_mark(fs, id, 1);
}

static void test_each_request(void)
{
    uint8_t m[64];
    /* HELLO: other protocol */
    m[0] = 0x01; m[1] = 1; m[2] = 2;
    call(m, 3);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    TEST_ASSERT_EQUAL_HEX8(1, resp[3]); /* its own proto */
    /* FTP_GET */
    m[0] = 0x02; call(m, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_UINT16(200, cc_get_u16(resp + 3));
    /* FTP_SET: out of range, short, busy */
    m[0] = 0x03; cc_put_u16(m + 2, 49); call(m, 4); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    cc_put_u16(m + 2, 601); call(m, 4); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    call(m, 3); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    h_state = 1; cc_put_u16(m + 2, 250); call(m, 4); TEST_ASSERT_EQUAL_HEX8(CC_ST_BUSY, st());
    h_state = 0;
    /* TIME_SET: any state */
    h_state = 2;
    m[0] = 0x04; cc_put_u32(m + 2, 1793000000u); cc_put_u16(m + 6, 480); call(m, 8);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_UINT32(1793000000u, h_time);
    TEST_ASSERT_EQUAL_INT16(480, h_tz);
    call(m, 5); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    /* Recording: only HELLO, FTP_GET, TIME_SET, OBJ_LIST */
    static const uint8_t busy_ops[] = {0x03, 0x11, 0x12, 0x13, 0x14, 0x20, 0x21, 0x22, 0x23, 0x24};
    for (unsigned i = 0; i < sizeof busy_ops; i++) {
        memset(m, 0, sizeof m);
        m[0] = busy_ops[i];
        m[1] = 1;
        call(m, 20);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(CC_ST_BUSY, st(), "busy while riding");
    }
    m[0] = 0x10; m[2] = 1; call(m, 3); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    m[0] = 0x01; m[2] = 1; call(m, 3); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_HEX8(2, resp[8]); /* state paused */
    h_state = 0;
    /* Unknown opcodes */
    m[0] = 0x05; call(m, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_OPCODE, st());
    m[0] = 0x7F; call(m, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_OPCODE, st());
    TEST_ASSERT_EQUAL_HEX8(0xFF, resp[0]);
    /* OBJ_LIST: bad kind; contents */
    m[0] = 0x10; m[2] = 3; call(m, 3); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    put_object(w1, w1n, 1, 512);
    m[0] = 0x10; m[2] = 1; call(m, 3);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_HEX8(1, resp[3]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(w1 + 4, resp + 4, 8);
    TEST_ASSERT_EQUAL_UINT32(227, cc_get_u32(resp + 12));
    TEST_ASSERT_EQUAL_HEX8(19, resp[16]);
    TEST_ASSERT_EQUAL_MEMORY("4 x 8 min threshold", resp + 17, 19);
    TEST_ASSERT_EQUAL_UINT32(4080, cc_get_u32(resp + 36));
    TEST_ASSERT_EQUAL_HEX8(10, resp[40]);
    m[2] = 2; call(m, 3); TEST_ASSERT_EQUAL_HEX8(0, resp[3]);
    /* OBJ_PUT_BEGIN errors: bad kind, too big, full, short */
    uint8_t b[19] = {0x11, 1, 3};
    call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    b[2] = 1; cc_put_u32(b + 11, 4097); call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_TOO_BIG, st());
    b[2] = 2; cc_put_u32(b + 11, 131073); call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_TOO_BIG, st());
    call(b, 10); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    for (int i = 0; i < 20; i++) {
        char p[64];
        uint8_t id[8] = {9, 9, 9, 9, 9, 9, 9, (uint8_t)i};
        cc_object_path(p, sizeof p, id, 1);
        memfs_put(fs, p, w1, (uint32_t)w1n);
    }
    b[2] = 1; b[3] = 0x55; cc_put_u32(b + 11, 200); call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_FULL, st());
    /* PUT_DATA and PUT_END without a session */
    uint8_t d[10] = {0x12, 1};
    call(d, 10); TEST_ASSERT_EQUAL_HEX8(CC_ST_NO_SESSION, st());
    uint8_t e[2] = {0x13, 1};
    call(e, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_NO_SESSION, st());
    /* OFFSET returns the expected next offset; overrun; short object; bad object */
    memfs_reset(fs);
    b[2] = 1; memcpy(b + 3, w1 + 4, 8); cc_put_u32(b + 11, (uint32_t)w1n); cc_put_u32(b + 15, cc_crc32(w1, w1n));
    call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    uint8_t dm[300] = {0x12, 2};
    cc_put_u32(dm + 2, 0); memcpy(dm + 6, w1, 100); call(dm, 106);
    cc_put_u32(dm + 2, 50); call(dm, 106);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OFFSET, st());
    TEST_ASSERT_EQUAL_UINT32(100, cc_get_u32(resp + 3));
    cc_put_u32(dm + 2, 100); memcpy(dm + 6, w1 + 100, 200); call(dm, 206);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st()); /* past the declared size */
    call(e, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st()); /* only 100 of 227 bytes */
    TEST_ASSERT_EQUAL_INT(0, memfs_count(fs, "objects/"));
    uint8_t junk[227];
    memcpy(junk, w1, 227);
    junk[17] = 30; /* name length breaks the structure */
    b[2] = 1; memcpy(b + 3, junk + 4, 8); cc_put_u32(b + 15, cc_crc32(junk, 227));
    call(b, 19);
    cc_put_u32(dm + 2, 0); memcpy(dm + 6, junk, 227); call(dm, 233);
    call(e, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_OBJECT, st());
    /* ID in BEGIN differs from the object's own: BAD_OBJECT */
    b[3] ^= 1; memcpy(junk, w1, 227); cc_put_u32(b + 15, cc_crc32(w1, 227));
    call(b, 19);
    memcpy(dm + 6, w1, 227); call(dm, 233);
    call(e, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_OBJECT, st());
    /* A new BEGIN aborts an open session */
    b[3] ^= 1;
    call(b, 19);
    cc_put_u32(dm + 2, 0); call(dm, 106);
    call(b, 19); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    cc_put_u32(dm + 2, 100); call(dm, 106);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OFFSET, st()); /* the new session starts at 0 */
    /* Idle session expires after 15 s and removes its temporary file */
    cc_link_tick(&lk, 1000 + 15001);
    TEST_ASSERT_EQUAL_INT(0, memfs_count(fs, "objects/"));
    call(e, 2); TEST_ASSERT_EQUAL_HEX8(CC_ST_NO_SESSION, st());
    /* OBJ_DELETE */
    put_object(w1, w1n, 1, 4000);
    uint8_t del[11] = {0x14, 3, 1};
    memcpy(del + 3, w1 + 4, 8);
    call(del, 11); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_INT(1, h_deleted_kind);
    call(del, 11); TEST_ASSERT_EQUAL_HEX8(CC_ST_NOT_FOUND, st());
    del[2] = 5; call(del, 11); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());

    /* Rides */
    make_ride(1793000000u, 8500, 0);
    make_ride(1793100000u, 300, 1);
    uint8_t rl[2] = {0x20, 4};
    call(rl, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_HEX8(2, resp[3]);
    TEST_ASSERT_EQUAL_UINT32(1793000000u, cc_get_u32(resp + 4));
    TEST_ASSERT_EQUAL_UINT32(8500, cc_get_u32(resp + 8));
    TEST_ASSERT_EQUAL_HEX8(0, resp[12]);
    TEST_ASSERT_EQUAL_UINT32(3600, cc_get_u32(resp + 13));
    TEST_ASSERT_EQUAL_UINT32(30000, cc_get_u32(resp + 17));
    TEST_ASSERT_EQUAL_HEX8(1, resp[21 + 8]); /* second ride uploaded */
    uint8_t ro[6] = {0x21, 5};
    cc_put_u32(ro + 2, 1793000000u);
    call(ro, 6);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_UINT32(8500, cc_get_u32(resp + 3));
    uint32_t crc = cc_get_u32(resp + 7);
    cc_put_u32(ro + 2, 42); call(ro, 6); TEST_ASSERT_EQUAL_HEX8(CC_ST_NOT_FOUND, st());
    /* RIDE_READ at 512 and 4000: reassemble and check the CRC */
    for (int pass = 0; pass < 2; pass++) {
        uint16_t chunk = pass ? 4000 : 512;
        cc_link_set_mtu(&lk, pass ? 247 : 23);
        static uint8_t got[9000];
        uint32_t off = 0;
        for (;;) {
            uint8_t rr[12] = {0x22, 6};
            cc_put_u32(rr + 2, 1793000000u);
            cc_put_u32(rr + 6, off);
            cc_put_u16(rr + 10, chunk);
            call(rr, 12);
            TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
            size_t n = resp_n - 3;
            memcpy(got + off, resp + 3, n);
            off += (uint32_t)n;
            if (n < chunk) break;
        }
        TEST_ASSERT_EQUAL_UINT32(8500, off);
        TEST_ASSERT_EQUAL_HEX32(crc, cc_crc32(got, off));
    }
    uint8_t rr[12] = {0x22, 6};
    cc_put_u32(rr + 2, 1793000000u);
    cc_put_u16(rr + 10, 0); call(rr, 12); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    cc_put_u16(rr + 10, 4001); call(rr, 12); TEST_ASSERT_EQUAL_HEX8(CC_ST_BAD_ARG, st());
    cc_put_u32(rr + 6, 9000); cc_put_u16(rr + 10, 100); call(rr, 12);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_size_t(3, resp_n); /* past the end: empty */
    cc_put_u32(rr + 2, 7); call(rr, 12); TEST_ASSERT_EQUAL_HEX8(CC_ST_NOT_FOUND, st());
    /* RIDE_MARK sets and clears */
    uint8_t mk[7] = {0x23, 7};
    cc_put_u32(mk + 2, 1793000000u); mk[6] = 1;
    call(mk, 7); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_TRUE(cc_ride_uploaded(fs, 1793000000u));
    mk[6] = 0; call(mk, 7);
    TEST_ASSERT_FALSE(cc_ride_uploaded(fs, 1793000000u));
    cc_put_u32(mk + 2, 5); call(mk, 7); TEST_ASSERT_EQUAL_HEX8(CC_ST_NOT_FOUND, st());
    /* RIDE_DELETE */
    uint8_t rd[6] = {0x24, 8};
    cc_put_u32(rd + 2, 1793100000u);
    call(rd, 6); TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    call(rd, 6); TEST_ASSERT_EQUAL_HEX8(CC_ST_NOT_FOUND, st());
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1793100000.meta"));
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1793100000.up"));
}

/* ------------------------------------------------------------------ atomic commit */
static void test_atomic_commit_fault_at_each_write(void)
{
    /* Count the write calls of a clean upload, then fail at each one in turn */
    put_object(w1, w1n, 1, 64);
    int clean_ok = memfs_exists(fs, "objects/1812240e1809c011.cw1");
    TEST_ASSERT_TRUE(clean_ok);
    for (int nth = 1; nth <= 8; nth++) {
        memfs_reset(fs);
        cc_link_host_t host = {hs, hb, hg, hf, ht, hd, NULL};
        cc_link_init(&lk, fs, &host, tx, NULL, 23);
        memfs_fail_writes_from(fs, nth);
        uint8_t m[300];
        m[0] = 0x11; m[1] = 1; m[2] = 1;
        memcpy(m + 3, w1 + 4, 8);
        cc_put_u32(m + 11, (uint32_t)w1n);
        cc_put_u32(m + 15, cc_crc32(w1, w1n));
        call(m, 19);
        for (size_t off = 0; off < w1n; off += 64) {
            size_t take = w1n - off < 64 ? w1n - off : 64;
            m[0] = 0x12;
            cc_put_u32(m + 2, (uint32_t)off);
            memcpy(m + 6, w1 + off, take);
            call(m, 6 + take);
            if (st() != CC_ST_OK) break;
        }
        m[0] = 0x13;
        call(m, 2);
        uint8_t status = st();
        memfs_fail_writes_from(fs, 0);
        uint32_t len;
        const uint8_t *b = memfs_data(fs, "objects/1812240e1809c011.cw1", &len);
        if (b) {
            /* the new state: the complete, valid object */
            TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, status);
            TEST_ASSERT_EQUAL_size_t(w1n, len);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(w1, b, w1n);
        } else {
            /* the old state: nothing at the final name */
            TEST_ASSERT_NOT_EQUAL(CC_ST_OK, status);
        }
    }
    /* Power cut at every byte of the upload, then boot: old state or new, and no .tmp left */
    for (int64_t cut = 0; cut <= (int64_t)w1n + 2; cut++) {
        memfs_reset(fs);
        cc_link_host_t host = {hs, hb, hg, hf, ht, hd, NULL};
        cc_link_init(&lk, fs, &host, tx, NULL, 247);
        memfs_cut_after_bytes(fs, cut);
        uint8_t m[300];
        m[0] = 0x11; m[1] = 1; m[2] = 1;
        memcpy(m + 3, w1 + 4, 8);
        cc_put_u32(m + 11, (uint32_t)w1n);
        cc_put_u32(m + 15, cc_crc32(w1, w1n));
        call(m, 19);
        m[0] = 0x12;
        cc_put_u32(m + 2, 0);
        memcpy(m + 6, w1, w1n);
        call(m, 6 + w1n);
        m[0] = 0x13;
        call(m, 2);
        memfs_power_cycle(fs);
        cc_store_boot(fs);
        uint32_t len;
        const uint8_t *b = memfs_data(fs, "objects/1812240e1809c011.cw1", &len);
        if (b) {
            TEST_ASSERT_EQUAL_size_t(w1n, len);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(w1, b, w1n);
        }
        TEST_ASSERT_FALSE(memfs_exists(fs, "objects/1812240e1809c011.tmp"));
    }
}

/* ------------------------------------------------------------------ retention */
static void test_retention(void)
{
    /* 101 rides, some uploaded: the oldest uploaded one goes */
    for (uint32_t i = 0; i < 101; i++)
        make_ride(1793000000u + i * 10000u, 10, i == 5 || i == 50);
    TEST_ASSERT_EQUAL_INT(1, cc_rides_retention(fs));
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1793050000.fit"));
    TEST_ASSERT_TRUE(memfs_exists(fs, "rides/1793000000.fit"));
    TEST_ASSERT_TRUE(memfs_exists(fs, "rides/1793500000.fit"));
    uint32_t ids[300];
    TEST_ASSERT_EQUAL_INT(100, cc_rides_list(fs, ids, 300));

    /* 101 rides, none uploaded: nothing is deleted */
    memfs_reset(fs);
    for (uint32_t i = 0; i < 101; i++)
        make_ride(1793000000u + i * 10000u, 10, 0);
    TEST_ASSERT_EQUAL_INT(0, cc_rides_retention(fs));
    TEST_ASSERT_EQUAL_INT(101, cc_rides_list(fs, ids, 300));

    /* 241 un-uploaded: only the oldest goes, whatever its flag */
    memfs_reset(fs);
    for (uint32_t i = 0; i < 241; i++)
        make_ride(1793000000u + i * 10000u, 10, 0);
    TEST_ASSERT_EQUAL_INT(1, cc_rides_retention(fs));
    TEST_ASSERT_EQUAL_INT(240, cc_rides_list(fs, ids, 300));
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1793000000.fit"));
    TEST_ASSERT_FALSE(memfs_exists(fs, "rides/1793000000.meta"));

    /* RIDE_LIST reports all 240 in one message of at most 4084 bytes */
    uint8_t rl[2] = {0x20, 1};
    call(rl, 2);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    TEST_ASSERT_EQUAL_HEX8(240, resp[3]);
    TEST_ASSERT_EQUAL_size_t(4 + 240 * 17, resp_n);
    TEST_ASSERT_TRUE(resp_n <= 4084);

    /* 100 rides with uploaded ones: nothing to do */
    memfs_reset(fs);
    for (uint32_t i = 0; i < 100; i++)
        make_ride(1793000000u + i * 10000u, 10, 1);
    TEST_ASSERT_EQUAL_INT(0, cc_rides_retention(fs));
}

static void test_full_io_and_reserved_id(void)
{
    /* FULL: no free storage */
    memfs_set_free(fs, 1000);
    uint8_t b[19] = {0x11, 1, 1};
    memcpy(b + 3, w1 + 4, 8);
    cc_put_u32(b + 11, (uint32_t)w1n);
    cc_put_u32(b + 15, cc_crc32(w1, w1n));
    call(b, 19);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_FULL, st());
    memfs_set_free(fs, -1);
    /* IO_ERROR: the write fails */
    call(b, 19);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    memfs_fail_writes_from(fs, 1);
    uint8_t dm[300] = {0x12, 2};
    cc_put_u32(dm + 2, 0);
    memcpy(dm + 6, w1, 100);
    call(dm, 106);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_IO_ERROR, st());
    TEST_ASSERT_EQUAL_UINT32(0, cc_get_u32(resp + 3)); /* where to resume */
    memfs_fail_writes_from(fs, 0);
    call(dm, 106);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
    /* req_id 0 is reserved: no response at all */
    uint8_t h0[3] = {0x01, 0x00, 0x01};
    call(h0, 3);
    TEST_ASSERT_EQUAL_size_t(0, resp_n);
    TEST_ASSERT_EQUAL_INT(0, txpkts);
    /* Trailing bytes the unit does not know are ignored (protocol 8) */
    uint8_t h1[5] = {0x01, 0x09, 0x01, 0xAA, 0xBB};
    call(h1, 5);
    TEST_ASSERT_EQUAL_HEX8(CC_ST_OK, st());
}

static void test_events(void)
{
    txlen = 0;
    txpkts = 0;
    cc_link_poll_events(&lk);
    TEST_ASSERT_EQUAL_INT(1, txpkts);
    TEST_ASSERT_EQUAL_HEX8(0xC0, txbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA0, txbuf[1]);
    TEST_ASSERT_EQUAL_HEX8(0, txbuf[2]);
    TEST_ASSERT_EQUAL_HEX8(0, txbuf[3]);
    TEST_ASSERT_EQUAL_HEX8(68, txbuf[4]);
    txpkts = 0;
    cc_link_poll_events(&lk);
    TEST_ASSERT_EQUAL_INT(0, txpkts); /* no change */
    h_batt = 64;
    cc_link_poll_events(&lk);
    TEST_ASSERT_EQUAL_INT(1, txpkts); /* crossed 65 % */
    txpkts = 0;
    h_state = 1;
    cc_link_poll_events(&lk);
    TEST_ASSERT_EQUAL_INT(1, txpkts);
    txlen = 0;
    cc_link_event_ride_saved(&lk, 1793000000u);
    TEST_ASSERT_EQUAL_HEX8(0xA1, txbuf[1]);
    TEST_ASSERT_EQUAL_UINT32(1793000000u, cc_get_u32(txbuf + 3));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_v5_messages);
    RUN_TEST(test_v5_put_data_packets);
    RUN_TEST(test_v5_bad_crc);
    RUN_TEST(test_reassembly_drops);
    RUN_TEST(test_each_request);
    RUN_TEST(test_put_chunks_512_and_4000);
    RUN_TEST(test_atomic_commit_fault_at_each_write);
    RUN_TEST(test_retention);
    RUN_TEST(test_full_io_and_reserved_id);
    RUN_TEST(test_events);
    return UNITY_END();
}
