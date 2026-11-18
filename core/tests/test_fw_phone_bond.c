/* test_fw_phone_bond.c: the phone's bond record on the eMMC (fw/main/phone_bond.c, Q43) and the link
 * log's message tracer (fw/main/link_trace.c), on the host. */
#include <string.h>

#include "../../fw/main/link_trace.c"
#include "../../fw/main/phone_bond.c"
#include "cc700/link.h"
#include "memfs.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const fw_bond_addr_t PHONE = {0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}};
static const fw_bond_addr_t SENSOR = {1, {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xC6}};

static fw_bond_sec_t sec(uint8_t kind, fw_bond_addr_t a, int auth, uint8_t fill)
{
    fw_bond_sec_t s;
    memset(&s, 0, sizeof s);
    s.kind = kind;
    s.peer = a;
    s.key_size = 16;
    s.ediv = 0x1234;
    s.rand_num = 0x0102030405060708ull;
    memset(s.ltk, fill, 16);
    memset(s.irk, fill + 1, 16);
    s.ltk_present = 1;
    s.irk_present = 1;
    s.authenticated = (uint8_t)auth;
    s.sc = 1;
    s.sign_counter = 7;
    return s;
}

/* The RAM store as NimBLE might hold it: the phone (passkey, authenticated) and a sensor that bonded
 * through Just Works (not authenticated), each with our and their records and a CCCD. */
static void store_with_phone_and_sensor(fw_bond_t *all)
{
    memset(all, 0, sizeof *all);
    all->sec[all->nsec++] = sec(FW_BOND_PEER, SENSOR, 0, 0x50);
    all->sec[all->nsec++] = sec(FW_BOND_OUR, SENSOR, 0, 0x51);
    all->sec[all->nsec++] = sec(FW_BOND_PEER, PHONE, 1, 0x60);
    all->sec[all->nsec++] = sec(FW_BOND_OUR, PHONE, 1, 0x61);
    all->cccd[all->ncccd++] = (fw_bond_cccd_t){PHONE, 0x0012, 1, 0};
    all->cccd[all->ncccd++] = (fw_bond_cccd_t){SENSOR, 0x0020, 1, 0};
    all->has_local_irk = 1;
    all->local_addr = (fw_bond_addr_t){0, {1, 2, 3, 4, 5, 6}};
    memset(all->local_irk, 0x77, 16);
}

static void test_filter_keeps_only_the_phone(void)
{
    fw_bond_t all, keep;
    store_with_phone_and_sensor(&all);
    TEST_ASSERT_EQUAL(1, fw_bond_phone_count(&all)); /* the sensor's record never counts (Q42 point 5) */
    TEST_ASSERT_EQUAL(1, fw_bond_filter(&all, &keep));
    TEST_ASSERT_EQUAL(2, keep.nsec);
    TEST_ASSERT_EQUAL_MEMORY(&PHONE, &keep.sec[0].peer, sizeof PHONE);
    TEST_ASSERT_EQUAL_MEMORY(&PHONE, &keep.sec[1].peer, sizeof PHONE);
    TEST_ASSERT_EQUAL(1, keep.ncccd);
    TEST_ASSERT_EQUAL_HEX16(0x0012, keep.cccd[0].chr_val_handle);
    TEST_ASSERT_EQUAL(1, keep.has_local_irk);
}

static void test_sensor_only_store_keeps_nothing(void)
{
    fw_bond_t all, keep;
    memset(&all, 0, sizeof all);
    all.sec[all.nsec++] = sec(FW_BOND_PEER, SENSOR, 0, 0x50);
    all.cccd[all.ncccd++] = (fw_bond_cccd_t){SENSOR, 0x0020, 1, 0};
    all.has_local_irk = 1;
    TEST_ASSERT_EQUAL(0, fw_bond_phone_count(&all));
    TEST_ASSERT_EQUAL(0, fw_bond_filter(&all, &keep));
    TEST_ASSERT_EQUAL(0, keep.nsec);
    TEST_ASSERT_EQUAL(0, keep.ncccd);
    TEST_ASSERT_EQUAL(0, keep.has_local_irk); /* no phone: no key material written at all */
}

static void test_encode_decode_round_trip(void)
{
    fw_bond_t all, keep, back;
    store_with_phone_and_sensor(&all);
    fw_bond_filter(&all, &keep);
    uint8_t buf[FW_BOND_REC_MAX];
    size_t n = fw_bond_encode(&keep, 42, buf, sizeof buf);
    TEST_ASSERT_EQUAL(10 + 2 * 72 + 12 + 23 + 4, n);
    uint32_t seq = 0;
    TEST_ASSERT_EQUAL(1, fw_bond_decode(buf, n, &back, &seq));
    TEST_ASSERT_EQUAL(42, seq);
    TEST_ASSERT_EQUAL(2, back.nsec);
    for (int i = 0; i < 2; i++) {
        const fw_bond_sec_t *a = &keep.sec[i], *b = &back.sec[i];
        TEST_ASSERT_EQUAL(a->kind, b->kind);
        TEST_ASSERT_EQUAL_MEMORY(&a->peer, &b->peer, sizeof a->peer);
        TEST_ASSERT_EQUAL(a->key_size, b->key_size);
        TEST_ASSERT_EQUAL(a->ediv, b->ediv);
        TEST_ASSERT_TRUE(a->rand_num == b->rand_num);
        TEST_ASSERT_EQUAL_MEMORY(a->ltk, b->ltk, 16);
        TEST_ASSERT_EQUAL_MEMORY(a->irk, b->irk, 16);
        TEST_ASSERT_EQUAL(a->ltk_present, b->ltk_present);
        TEST_ASSERT_EQUAL(a->irk_present, b->irk_present);
        TEST_ASSERT_EQUAL(a->csrk_present, b->csrk_present);
        TEST_ASSERT_EQUAL(a->authenticated, b->authenticated);
        TEST_ASSERT_EQUAL(a->sc, b->sc);
        TEST_ASSERT_EQUAL(a->sign_counter, b->sign_counter);
    }
    TEST_ASSERT_EQUAL(1, back.ncccd);
    TEST_ASSERT_EQUAL_HEX16(0x0012, back.cccd[0].chr_val_handle);
    TEST_ASSERT_EQUAL(1, back.cccd[0].flags);
    TEST_ASSERT_EQUAL(1, back.has_local_irk);
    TEST_ASSERT_EQUAL_MEMORY(keep.local_irk, back.local_irk, 16);
    /* the whole store also fits the buffer */
    TEST_ASSERT_TRUE(fw_bond_encode(&all, 1, buf, sizeof buf) > 0);
    TEST_ASSERT_EQUAL(0, fw_bond_encode(&keep, 1, buf, 100)); /* too small: refused, not truncated */
}

static void test_decode_rejects_damage(void)
{
    fw_bond_t all, keep, back;
    store_with_phone_and_sensor(&all);
    fw_bond_filter(&all, &keep);
    uint8_t buf[FW_BOND_REC_MAX];
    uint32_t seq;
    size_t n = fw_bond_encode(&keep, 3, buf, sizeof buf);
    for (size_t i = 0; i < n; i++) { /* every single-byte change is caught */
        buf[i] ^= 0x01;
        TEST_ASSERT_EQUAL_MESSAGE(0, fw_bond_decode(buf, n, &back, &seq), "flipped byte accepted");
        buf[i] ^= 0x01;
    }
    for (size_t cut = 0; cut < n; cut++) /* and every truncation */
        TEST_ASSERT_EQUAL(0, fw_bond_decode(buf, cut, &back, &seq));
    TEST_ASSERT_EQUAL(1, fw_bond_decode(buf, n, &back, &seq));
}

static void test_two_slots_alternate_and_survive_a_cut(void)
{
    hal_fs_t *fs = memfs_new();
    fw_bond_t all, keep, back;
    fw_bond_slots_t slots;
    store_with_phone_and_sensor(&all);
    fw_bond_filter(&all, &keep);
    TEST_ASSERT_EQUAL(0, fw_bond_load(fs, &back, &slots));
    TEST_ASSERT_EQUAL(-1, slots.newest);
    TEST_ASSERT_EQUAL(0, fw_bond_save(fs, &keep, &slots));
    TEST_ASSERT_TRUE(memfs_exists(fs, FW_BOND_A));
    TEST_ASSERT_FALSE(memfs_exists(fs, FW_BOND_B));
    keep.sec[0].sign_counter = 99;
    TEST_ASSERT_EQUAL(0, fw_bond_save(fs, &keep, &slots));
    TEST_ASSERT_TRUE(memfs_exists(fs, FW_BOND_B));
    fw_bond_slots_t s2;
    TEST_ASSERT_EQUAL(1, fw_bond_load(fs, &back, &s2));
    TEST_ASSERT_EQUAL(1, s2.newest);
    TEST_ASSERT_EQUAL(99, back.sec[0].sign_counter);
    /* A power cut in the middle of the third save (into slot a) leaves slot b loadable. */
    keep.sec[0].sign_counter = 100;
    memfs_cut_after_bytes(fs, 40);
    TEST_ASSERT_NOT_EQUAL(0, fw_bond_save(fs, &keep, &slots));
    memfs_power_cycle(fs);
    TEST_ASSERT_EQUAL(1, fw_bond_load(fs, &back, &s2));
    TEST_ASSERT_EQUAL(99, back.sec[0].sign_counter);
    TEST_ASSERT_EQUAL(1, fw_bond_phone_count(&back));
    /* Forget phone: both slots go. */
    TEST_ASSERT_EQUAL(0, fw_bond_erase(fs, &s2));
    TEST_ASSERT_FALSE(memfs_exists(fs, FW_BOND_A));
    TEST_ASSERT_FALSE(memfs_exists(fs, FW_BOND_B));
    TEST_ASSERT_EQUAL(0, fw_bond_load(fs, &back, &s2));
    TEST_ASSERT_EQUAL(0, back.nsec);
    memfs_free(fs);
}

/* ------------------------------------------------------------------ link trace */
typedef struct {
    fw_trace_t t;
    fw_trace_msg_t m;
    int done, drops;
} sink_t;

static void emit(void *ctx, const uint8_t *pkt, size_t len)
{
    sink_t *s = ctx;
    int r = fw_trace_feed(&s->t, pkt, len, &s->m);
    s->done += r == FW_TRACE_DONE;
    s->drops += r == FW_TRACE_DROP;
}

static void test_trace_names_messages_by_req_id(void)
{
    TEST_ASSERT_EQUAL_STRING("HELLO", fw_trace_name(0x01, 7));
    TEST_ASSERT_EQUAL_STRING("HELLO", fw_trace_name(0x81, 7));
    TEST_ASSERT_EQUAL(1, fw_trace_kind(0x81, 7));
    /* E7: 0xA0 with a req_id is RIDE_LIST's response, with req_id 0 it is EVENT_STATE. */
    TEST_ASSERT_EQUAL_STRING("RIDE_LIST", fw_trace_name(0xA0, 3));
    TEST_ASSERT_EQUAL_STRING("EVENT_STATE", fw_trace_name(0xA0, 0));
    TEST_ASSERT_EQUAL_STRING("EVENT_RIDE_SAVED", fw_trace_name(0xA1, 0));
    TEST_ASSERT_EQUAL(2, fw_trace_kind(0xA1, 0));
    TEST_ASSERT_EQUAL_STRING("RIDE_OPEN", fw_trace_name(0xA1, 9));
    TEST_ASSERT_EQUAL_STRING("?", fw_trace_name(0x7E, 1));
}

static void test_trace_follows_the_cores_framing(void)
{
    /* A 4000-byte RIDE_READ response at MTU 247 (17 packets, V§5), framed by the core itself. */
    static uint8_t msg[4003];
    memset(msg, 0xEE, sizeof msg);
    msg[0] = 0xA2;
    msg[1] = 5;
    msg[2] = 0; /* OK */
    sink_t s;
    memset(&s, 0, sizeof s);
    int pk = cc_link_fragment(msg, sizeof msg, 247 - 4, emit, &s);
    TEST_ASSERT_EQUAL(17, pk);
    TEST_ASSERT_EQUAL(1, s.done);
    TEST_ASSERT_EQUAL(0, s.drops);
    TEST_ASSERT_EQUAL_HEX8(0xA2, s.m.op);
    TEST_ASSERT_EQUAL(5, s.m.req);
    TEST_ASSERT_EQUAL(0, s.m.status);
    TEST_ASSERT_EQUAL(4003, s.m.len);
    TEST_ASSERT_EQUAL(17, s.m.pkts);
    TEST_ASSERT_EQUAL_STRING("RIDE_READ", fw_trace_name(s.m.op, s.m.req));

    /* A one-packet request at MTU 23: no status. */
    const uint8_t hello[] = {0x01, 9, 1};
    memset(&s, 0, sizeof s);
    cc_link_fragment(hello, sizeof hello, 23 - 4, emit, &s);
    TEST_ASSERT_EQUAL(1, s.done);
    TEST_ASSERT_EQUAL(-1, s.m.status);
    TEST_ASSERT_EQUAL(3, s.m.len);

    /* A status byte that arrives in the second packet (a 1-byte first packet) is still found. */
    memset(&s, 0, sizeof s);
    const uint8_t p1[] = {0x80, 0x82};
    const uint8_t p2[] = {0x41, 4, 6, 0, 0};
    emit(&s, p1, sizeof p1);
    emit(&s, p2, sizeof p2);
    TEST_ASSERT_EQUAL(1, s.done);
    TEST_ASSERT_EQUAL(6, s.m.status);
    TEST_ASSERT_EQUAL(5, s.m.len);
}

static void test_trace_reports_drops(void)
{
    sink_t s;
    memset(&s, 0, sizeof s);
    const uint8_t cont[] = {0x01, 1, 2}; /* no START while idle */
    emit(&s, cont, sizeof cont);
    TEST_ASSERT_EQUAL(1, s.drops);
    const uint8_t first[] = {0x80, 0x12, 3, 0, 0};
    const uint8_t wrong[] = {0x45, 1, 2}; /* counter 5, expected 1 */
    emit(&s, first, sizeof first);
    emit(&s, wrong, sizeof wrong);
    TEST_ASSERT_EQUAL(2, s.drops);
    TEST_ASSERT_EQUAL(0, s.done);
    const uint8_t bad_start[] = {0xC3, 1, 2}; /* START with a non-zero counter */
    emit(&s, bad_start, sizeof bad_start);
    TEST_ASSERT_EQUAL(3, s.drops);
    TEST_ASSERT_EQUAL(FW_TRACE_NONE, fw_trace_feed(&s.t, first, 0, NULL)); /* empty write: ignored */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_filter_keeps_only_the_phone);
    RUN_TEST(test_sensor_only_store_keeps_nothing);
    RUN_TEST(test_encode_decode_round_trip);
    RUN_TEST(test_decode_rejects_damage);
    RUN_TEST(test_two_slots_alternate_and_survive_a_cut);
    RUN_TEST(test_trace_names_messages_by_req_id);
    RUN_TEST(test_trace_follows_the_cores_framing);
    RUN_TEST(test_trace_reports_drops);
    return UNITY_END();
}
