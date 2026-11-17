/* sens_logic.h: the sensor manager's decisions that need no radio (bring-up step 6), kept free of
 * ESP-IDF so core/tests/test_fw_sensors.c can run them on the host: advertising-data parsing, the
 * Add sensor scan list, signal bars, the reconnect backoff (spec 10.7) and which characteristic a
 * descriptor belongs to. */
#ifndef FW_SENS_LOGIC_H
#define FW_SENS_LOGIC_H
#include <stddef.h>
#include <stdint.h>

#define FW_KIND_POWER 1 /* Cycling Power, 0x1818 (hal_ble.h HAL_SENSOR_POWER) */
#define FW_KIND_HR 2    /* Heart Rate, 0x180D (HAL_SENSOR_HR) */
#define FW_KIND_BIT(k) ((uint8_t)(1u << ((k) - 1)))
#define FW_NAME_MAX 24  /* same as CC_BOND_NAME */

/* ------------------------------------------------------------------ advertising data */
typedef struct {
    uint8_t kinds;               /* FW_KIND_BIT(FW_KIND_POWER) | FW_KIND_BIT(FW_KIND_HR) */
    int has_name;
    char name[FW_NAME_MAX + 1];  /* printable ASCII; anything else becomes '?' */
    uint16_t uuids[8];           /* every 16-bit service UUID listed (0x1816 CSC included), for the log */
    int n_uuids;
    int has_mfr;                 /* manufacturer data (0xFF) present; the raw hex dump carries it */
} fw_adv_t;

/* Parses one advertising or scan-response payload (Core spec Vol 3 Part C 11): 16-bit service UUID
 * lists (0x02, 0x03), 16-bit service data (0x16) and the local name (0x08 shortened, 0x09 complete;
 * complete wins). Returns 0, or -1 if a field runs past the end (what came before it is kept). */
int fw_adv_parse(const uint8_t *d, size_t len, fw_adv_t *out);

/* ------------------------------------------------------------------ the scan list */
#define FW_SCAN_MAX 12
#define FW_SCAN_AGE_MS 10000u /* an entry not heard for this long leaves the list */

typedef struct {
    uint8_t addr[6];             /* NimBLE order: addr[0] is the least significant byte */
    uint8_t addr_type;
    uint8_t kinds;               /* 0 until a payload from it names 0x1818 or 0x180D */
    int8_t rssi;
    char name[FW_NAME_MAX + 1];  /* empty if none heard yet */
    uint32_t first_ms, seen_ms;
    uint8_t told;                /* bit 0: logged as a sensor; bit 1: logged with its name */
} fw_scan_ent_t;

typedef struct {
    fw_scan_ent_t e[FW_SCAN_MAX];
    int n;
} fw_scan_list_t;

void fw_scan_clear(fw_scan_list_t *l);
/* One report (advertisement or scan response) merged into the list by address. A full list drops
 * the entry not known as a sensor that was heard longest ago, else the one heard longest ago.
 * *announce is set to 1 the first time the entry is known to be a sensor, and again once if its name
 * arrives after that: the caller logs the entry then. Returns the entry. */
fw_scan_ent_t *fw_scan_report(fw_scan_list_t *l, const uint8_t addr[6], uint8_t addr_type, int rssi,
                              const fw_adv_t *adv, uint32_t now_ms, int *announce);
void fw_scan_expire(fw_scan_list_t *l, uint32_t now_ms, uint32_t max_age_ms);
/* Whether to log this report's raw payload (bring-up: the advertising of the CYCPLUS H1 and GEOID
 * PM500 is Unverified). Once per entry for advertisements and once for scan responses, and only for a
 * sensor, a device with no name heard yet, or a name that looks like a sensor (fw_name_interesting).
 * Marks the entry so the next report of the same kind returns 0. Call after fw_scan_report. */
int fw_scan_should_dump(fw_scan_ent_t *e, int is_scan_rsp);
/* Case-insensitive: CYCPLUS, H1, GEOID, PM500, PM, HR, HEART, POWER. */
int fw_name_interesting(const char *name);
/* Lower-case hex of b, cut to fit cap (always terminated). Returns the characters written. */
int fw_hex(const uint8_t *b, size_t n, char *out, size_t cap);
/* The UUIDs of an advertisement as "1818,180a", cut to fit cap (always terminated). */
void fw_uuid_list(const fw_adv_t *a, char *out, size_t cap);
/* The kind an entry offers among the wanted kinds (power first), 0 if none. */
int fw_scan_kind(uint8_t kinds, uint8_t want_mask);
/* Indexes of the sensors offering a wanted kind, in the order they were first heard (a row never
 * moves under the rider's finger as signal strengths change). Returns how many, at most max. */
int fw_scan_visible(const fw_scan_list_t *l, uint8_t want_mask, int *idx, int max);

/* ------------------------------------------------------------------ small helpers */
int fw_rssi_bars(int rssi);                                /* 1 to 4 (the Add sensor rows) */
void fw_addr_id4(const uint8_t addr[6], char out[5]);      /* last four hex digits, "7C2E" */
void fw_addr_str(const uint8_t addr[6], char out[18]);     /* "C4:05:06:07:41:B9" */

/* Spec 10.7: the delay before the next connection attempt after `failures` failed attempts or drops
 * in a row: 1, 2, 4, 8, 15, then 30 s for ever. 0 failures: no delay. */
uint32_t fw_backoff_ms(unsigned failures);

/* Which characteristic a descriptor at dsc_handle belongs to: the largest value handle below it among
 * the n characteristic value handles in vals (any order). 0 if none. */
uint16_t fw_dsc_owner(const uint16_t *vals, int n, uint16_t dsc_handle);
#endif
