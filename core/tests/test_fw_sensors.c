/* test_fw_sensors.c: the sensor manager's radio-free decisions (fw/main/sens_logic.c, bring-up step 6):
 * advertising data, the Add sensor list, signal bars, the spec 10.7 backoff and CCCD ownership. */
#include <string.h>

#include "../../fw/main/sens_logic.c"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t PM_ADDR[6] = {0x2E, 0x7C, 0x03, 0x04, 0x05, 0xC6};
static const uint8_t HR_ADDR[6] = {0xB9, 0x41, 0x07, 0x06, 0x05, 0xC4};

static void test_adv_uuid_lists_and_names(void)
{
    fw_adv_t a;
    /* flags, complete 16-bit list {0x180A, 0x1818}, shortened name "PM5" */
    const uint8_t pm[] = {2, 0x01, 0x06, 5, 0x03, 0x0A, 0x18, 0x18, 0x18, 4, 0x08, 'P', 'M', '5'};
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(pm, sizeof pm, &a));
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_POWER), a.kinds);
    TEST_ASSERT_TRUE(a.has_name);
    TEST_ASSERT_EQUAL_STRING("PM5", a.name);
    /* complete name wins over a shortened one that follows it; incomplete list with 0x180D */
    const uint8_t hr[] = {6, 0x09, 'H', 'R', 'M', 'P', 'r', 3, 0x02, 0x0D, 0x18, 3, 0x08, 'H', 'R'};
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(hr, sizeof hr, &a));
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_HR), a.kinds);
    TEST_ASSERT_EQUAL_STRING("HRMPr", a.name);
    /* service data for 0x1818 counts; an unrelated UUID does not; zero-length field ends the data */
    const uint8_t sd[] = {5, 0x16, 0x18, 0x18, 0xAA, 0xBB, 3, 0x03, 0x0F, 0x18, 0, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(sd, sizeof sd, &a));
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_POWER), a.kinds);
    TEST_ASSERT_FALSE(a.has_name);
    /* non-printable bytes become '?', and a long name is cut to FW_NAME_MAX */
    uint8_t ln[2 + 30] = {31, 0x09};
    memset(ln + 2, 'x', 30);
    ln[2] = 0xC3;
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(ln, sizeof ln, &a));
    TEST_ASSERT_EQUAL_UINT(FW_NAME_MAX, strlen(a.name));
    TEST_ASSERT_EQUAL_CHAR('?', a.name[0]);
    /* a field that runs past the end: what came before is kept */
    const uint8_t cut[] = {3, 0x03, 0x0D, 0x18, 9, 0x09, 'A'};
    TEST_ASSERT_EQUAL_INT(-1, fw_adv_parse(cut, sizeof cut, &a));
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_HR), a.kinds);
    /* both services in one list */
    const uint8_t both[] = {5, 0x03, 0x0D, 0x18, 0x18, 0x18};
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(both, sizeof both, &a));
    TEST_ASSERT_EQUAL_HEX8(3, a.kinds);
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(NULL, 0, &a));
    TEST_ASSERT_EQUAL_HEX8(0, a.kinds);
}

/* The UUID in the advertisement and the name in the scan response arrive as two reports for one
 * address: they merge, and the entry is announced once as a sensor and once more for its name. */
static void test_scan_merges_reports_and_announces(void)
{
    fw_scan_list_t l;
    fw_scan_clear(&l);
    fw_adv_t adv = {.kinds = FW_KIND_BIT(FW_KIND_POWER)}, rsp = {.has_name = 1}, none = {0};
    strcpy(rsp.name, "PM500");
    int ann;
    fw_scan_ent_t *e = fw_scan_report(&l, PM_ADDR, 0, -55, &adv, 1000, &ann);
    TEST_ASSERT_EQUAL_INT(1, ann);
    TEST_ASSERT_EQUAL_STRING("", e->name);
    e = fw_scan_report(&l, PM_ADDR, 0, -58, &rsp, 1010, &ann);
    TEST_ASSERT_EQUAL_INT(1, ann); /* the name arrived */
    TEST_ASSERT_EQUAL_STRING("PM500", e->name);
    e = fw_scan_report(&l, PM_ADDR, 0, -70, &none, 1100, &ann);
    TEST_ASSERT_EQUAL_INT(0, ann);
    TEST_ASSERT_EQUAL_INT(1, l.n);
    TEST_ASSERT_EQUAL_INT(-70, e->rssi);
    TEST_ASSERT_EQUAL_UINT32(1000, e->first_ms);
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_POWER), e->kinds);
    /* same address, other type: a different device */
    fw_scan_report(&l, PM_ADDR, 1, -70, &none, 1200, &ann);
    TEST_ASSERT_EQUAL_INT(0, ann); /* not a sensor (yet) */
    TEST_ASSERT_EQUAL_INT(2, l.n);
    /* name first, then the UUID: one announcement with the name already known */
    fw_scan_report(&l, HR_ADDR, 1, -80, &rsp, 1300, &ann);
    TEST_ASSERT_EQUAL_INT(0, ann);
    fw_adv_t hr = {.kinds = FW_KIND_BIT(FW_KIND_HR)};
    fw_scan_report(&l, HR_ADDR, 1, -80, &hr, 1310, &ann);
    TEST_ASSERT_EQUAL_INT(1, ann);
    fw_scan_report(&l, HR_ADDR, 1, -80, &rsp, 1320, &ann);
    TEST_ASSERT_EQUAL_INT(0, ann);
}

/* Visible rows: wanted kinds only, in first-heard order whatever the signal, at most max; entries
 * expire after FW_SCAN_AGE_MS; a full list evicts non-sensors first. */
static void test_scan_visible_order_expiry_eviction(void)
{
    fw_scan_list_t l;
    fw_scan_clear(&l);
    fw_adv_t pm = {.kinds = FW_KIND_BIT(FW_KIND_POWER)}, hr = {.kinds = FW_KIND_BIT(FW_KIND_HR)}, none = {0};
    int ann, idx[4];
    uint8_t a[6] = {1, 0, 0, 0, 0, 0};
    fw_scan_report(&l, a, 0, -90, &pm, 100, &ann); /* 0: PM, weak, heard first */
    a[0] = 2;
    fw_scan_report(&l, a, 0, -40, &hr, 200, &ann); /* 1: HR, strong */
    a[0] = 3;
    fw_scan_report(&l, a, 0, -40, &none, 250, &ann); /* 2: phone */
    a[0] = 4;
    fw_scan_report(&l, a, 0, -50, &pm, 300, &ann); /* 3: PM */
    a[0] = 1;
    fw_scan_report(&l, a, 0, -30, &pm, 400, &ann); /* the first PM again, now strongest */
    int n = fw_scan_visible(&l, 3, idx, 4);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_UINT8(1, l.e[idx[0]].addr[0]);
    TEST_ASSERT_EQUAL_UINT8(2, l.e[idx[1]].addr[0]);
    TEST_ASSERT_EQUAL_UINT8(4, l.e[idx[2]].addr[0]);
    /* a power meter already paired: only heart-rate straps are offered */
    n = fw_scan_visible(&l, FW_KIND_BIT(FW_KIND_HR), idx, 4);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_UINT8(2, l.e[idx[0]].addr[0]);
    TEST_ASSERT_EQUAL_INT(2, fw_scan_visible(&l, 3, idx, 2));
    TEST_ASSERT_EQUAL_INT(0, fw_scan_visible(&l, 3, idx, 0));
    /* expiry: at 10,250 ms the HR (200) and the phone (250) are 10,050 and 10,000 ms old */
    fw_scan_expire(&l, 10250, FW_SCAN_AGE_MS);
    TEST_ASSERT_EQUAL_INT(3, l.n);
    n = fw_scan_visible(&l, 3, idx, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_UINT8(1, l.e[idx[0]].addr[0]);
    TEST_ASSERT_EQUAL_UINT8(4, l.e[idx[1]].addr[0]);
    /* fill the list with sensors and one non-sensor: the non-sensor goes first, though it is newer */
    fw_scan_clear(&l);
    for (int i = 0; i < FW_SCAN_MAX - 1; i++) {
        a[0] = (uint8_t)(10 + i);
        fw_scan_report(&l, a, 0, -60, &pm, (uint32_t)i, &ann);
    }
    a[0] = 99;
    fw_scan_report(&l, a, 0, -60, &none, 500, &ann);
    TEST_ASSERT_EQUAL_INT(FW_SCAN_MAX, l.n);
    a[0] = 100;
    fw_scan_report(&l, a, 0, -60, &hr, 600, &ann);
    TEST_ASSERT_EQUAL_INT(FW_SCAN_MAX, l.n);
    for (int i = 0; i < l.n; i++)
        TEST_ASSERT_TRUE(l.e[i].addr[0] != 99);
    /* all sensors: the one heard longest ago goes */
    a[0] = 101;
    fw_scan_report(&l, a, 0, -60, &hr, 700, &ann);
    for (int i = 0; i < l.n; i++)
        TEST_ASSERT_TRUE(l.e[i].addr[0] != 10);
}

static void test_kind_bars_ids(void)
{
    TEST_ASSERT_EQUAL_INT(FW_KIND_POWER, fw_scan_kind(3, 3));
    TEST_ASSERT_EQUAL_INT(FW_KIND_HR, fw_scan_kind(3, FW_KIND_BIT(FW_KIND_HR)));
    TEST_ASSERT_EQUAL_INT(0, fw_scan_kind(FW_KIND_BIT(FW_KIND_POWER), FW_KIND_BIT(FW_KIND_HR)));
    TEST_ASSERT_EQUAL_INT(4, fw_rssi_bars(-60));
    TEST_ASSERT_EQUAL_INT(3, fw_rssi_bars(-61));
    TEST_ASSERT_EQUAL_INT(3, fw_rssi_bars(-70));
    TEST_ASSERT_EQUAL_INT(2, fw_rssi_bars(-80));
    TEST_ASSERT_EQUAL_INT(1, fw_rssi_bars(-81));
    TEST_ASSERT_EQUAL_INT(1, fw_rssi_bars(-127));
    char id4[5], s[18];
    fw_addr_id4(PM_ADDR, id4);
    TEST_ASSERT_EQUAL_STRING("7C2E", id4); /* the last four characters of the printed address */
    fw_addr_str(HR_ADDR, s);
    TEST_ASSERT_EQUAL_STRING("C4:05:06:07:41:B9", s);
}

/* Spec 10.7: 1, 2, 4, 8, 15 then 30 s. */
static void test_backoff(void)
{
    static const uint32_t want[] = {0, 1000, 2000, 4000, 8000, 15000, 30000, 30000, 30000};
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++)
        TEST_ASSERT_EQUAL_UINT32(want[i], fw_backoff_ms(i));
    TEST_ASSERT_EQUAL_UINT32(30000, fw_backoff_ms(1000000u));
}

/* A Cycling Power service as a typical meter lays it out: measurement (notify, CCCD then SCCD),
 * feature, sensor location, control point (indicate, CCCD). Find Information over the service returns
 * every handle; only 0x2902 entries are passed here. */
static void test_dsc_owner(void)
{
    const uint16_t vals[] = {0x0016, 0x001B, 0x0012, 0x0019}; /* out of order on purpose */
    TEST_ASSERT_EQUAL_HEX16(0x0012, fw_dsc_owner(vals, 4, 0x0013)); /* measurement CCCD */
    TEST_ASSERT_EQUAL_HEX16(0x001B, fw_dsc_owner(vals, 4, 0x001C)); /* control point CCCD */
    TEST_ASSERT_EQUAL_HEX16(0x0000, fw_dsc_owner(vals, 4, 0x0011));
    TEST_ASSERT_EQUAL_HEX16(0x0000, fw_dsc_owner(vals, 0, 0x0013));
}

/* A PM500 that also lists CSC (0x1816) and carries manufacturer data: power meter, with 0x1816 logged
 * and otherwise ignored. */
static void test_adv_csc_mfr_and_dump(void)
{
    fw_adv_t a;
    const uint8_t pm[] = {7, 0x03, 0x18, 0x18, 0x16, 0x18, 0x18, 0x18, 5, 0xFF, 0x59, 0x00, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_INT(0, fw_adv_parse(pm, sizeof pm, &a));
    TEST_ASSERT_EQUAL_HEX8(FW_KIND_BIT(FW_KIND_POWER), a.kinds);
    TEST_ASSERT_EQUAL_INT(2, a.n_uuids); /* the repeated 0x1818 is listed once */
    TEST_ASSERT_TRUE(a.has_mfr);
    char uu[41], hex[9];
    fw_uuid_list(&a, uu, sizeof uu);
    TEST_ASSERT_EQUAL_STRING("1818,1816", uu);
    fw_uuid_list(&a, uu, 7); /* room for one only */
    TEST_ASSERT_EQUAL_STRING("1818", uu);
    TEST_ASSERT_EQUAL_INT(8, fw_hex(pm, sizeof pm, hex, sizeof hex));
    TEST_ASSERT_EQUAL_STRING("07031818", hex);
    /* CSC alone is not a sensor kind here */
    const uint8_t csc[] = {3, 0x03, 0x16, 0x18};
    fw_adv_parse(csc, sizeof csc, &a);
    TEST_ASSERT_EQUAL_HEX8(0, a.kinds);

    TEST_ASSERT_TRUE(fw_name_interesting("CYCPLUS H1"));
    TEST_ASSERT_TRUE(fw_name_interesting("geoid pm500"));
    TEST_ASSERT_TRUE(fw_name_interesting("Heart"));
    TEST_ASSERT_FALSE(fw_name_interesting("Pixel 8"));

    fw_scan_list_t l;
    fw_scan_clear(&l);
    fw_adv_t none = {0}, phone = {.has_name = 1}, h1 = {.has_name = 1};
    strcpy(phone.name, "Pixel 8");
    strcpy(h1.name, "CYCPLUS H1");
    int ann;
    uint8_t ad[6] = {1};
    fw_scan_ent_t *e = fw_scan_report(&l, ad, 0, -50, &none, 0, &ann);
    TEST_ASSERT_TRUE(fw_scan_should_dump(e, 0)); /* no name yet: dumped once */
    TEST_ASSERT_FALSE(fw_scan_should_dump(e, 0));
    e = fw_scan_report(&l, ad, 0, -50, &phone, 1, &ann);
    TEST_ASSERT_FALSE(fw_scan_should_dump(e, 1)); /* a phone's scan response is not */
    ad[0] = 2;
    e = fw_scan_report(&l, ad, 0, -50, &h1, 2, &ann);
    TEST_ASSERT_TRUE(fw_scan_should_dump(e, 1)); /* a sensor-like name, before its UUID is seen */
    TEST_ASSERT_TRUE(fw_scan_should_dump(e, 0));
    TEST_ASSERT_FALSE(fw_scan_should_dump(e, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_adv_csc_mfr_and_dump);
    RUN_TEST(test_adv_uuid_lists_and_names);
    RUN_TEST(test_scan_merges_reports_and_announces);
    RUN_TEST(test_scan_visible_order_expiry_eviction);
    RUN_TEST(test_kind_bars_ids);
    RUN_TEST(test_backoff);
    RUN_TEST(test_dsc_owner);
    return UNITY_END();
}
