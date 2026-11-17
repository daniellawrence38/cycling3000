/* sens_logic.c: see sens_logic.h. No ESP-IDF dependency; host-tested by core/tests/test_fw_sensors.c. */
#include "sens_logic.h"

#include <stdio.h>
#include <string.h>

#define UUID_CPS 0x1818
#define UUID_HRS 0x180D

static uint8_t kind_bit_of(uint16_t uuid)
{
    return uuid == UUID_CPS ? FW_KIND_BIT(FW_KIND_POWER) : uuid == UUID_HRS ? FW_KIND_BIT(FW_KIND_HR) : 0;
}

static void copy_name(char *dst, const uint8_t *src, size_t n)
{
    if (n > FW_NAME_MAX)
        n = FW_NAME_MAX;
    for (size_t i = 0; i < n; i++)
        dst[i] = src[i] >= 0x20 && src[i] < 0x7F ? (char)src[i] : '?';
    dst[n] = 0;
}

static void add_uuid(fw_adv_t *a, uint16_t u)
{
    a->kinds |= kind_bit_of(u);
    for (int i = 0; i < a->n_uuids; i++)
        if (a->uuids[i] == u)
            return;
    if (a->n_uuids < (int)(sizeof a->uuids / sizeof a->uuids[0]))
        a->uuids[a->n_uuids++] = u;
}

int fw_adv_parse(const uint8_t *d, size_t len, fw_adv_t *out)
{
    memset(out, 0, sizeof *out);
    int complete_name = 0;
    size_t o = 0;
    while (o < len) {
        size_t flen = d[o];
        if (flen == 0) /* early termination: the rest is padding */
            break;
        if (o + 1 + flen > len)
            return -1;
        uint8_t type = d[o + 1];
        const uint8_t *v = d + o + 2;
        size_t vlen = flen - 1;
        if (type == 0x02 || type == 0x03) {
            for (size_t i = 0; i + 1 < vlen; i += 2)
                add_uuid(out, (uint16_t)(v[i] | v[i + 1] << 8));
        } else if (type == 0x16 && vlen >= 2) {
            add_uuid(out, (uint16_t)(v[0] | v[1] << 8));
        } else if (type == 0xFF) {
            out->has_mfr = 1;
        } else if ((type == 0x09 || (type == 0x08 && !complete_name)) && vlen > 0) {
            copy_name(out->name, v, vlen);
            out->has_name = 1;
            complete_name = type == 0x09;
        }
        o += 1 + flen;
    }
    return 0;
}

void fw_scan_clear(fw_scan_list_t *l) { memset(l, 0, sizeof *l); }

fw_scan_ent_t *fw_scan_report(fw_scan_list_t *l, const uint8_t addr[6], uint8_t addr_type, int rssi,
                              const fw_adv_t *adv, uint32_t now_ms, int *announce)
{
    *announce = 0;
    fw_scan_ent_t *e = NULL;
    for (int i = 0; i < l->n; i++)
        if (l->e[i].addr_type == addr_type && !memcmp(l->e[i].addr, addr, 6)) {
            e = &l->e[i];
            break;
        }
    if (!e) {
        if (l->n < FW_SCAN_MAX) {
            e = &l->e[l->n++];
        } else {
            int victim = -1;
            for (int pass = 0; pass < 2 && victim < 0; pass++)
                for (int i = 0; i < l->n; i++)
                    if ((pass == 1 || l->e[i].kinds == 0) &&
                        (victim < 0 || (int32_t)(l->e[i].seen_ms - l->e[victim].seen_ms) < 0))
                        victim = i;
            e = &l->e[victim];
        }
        memset(e, 0, sizeof *e);
        memcpy(e->addr, addr, 6);
        e->addr_type = addr_type;
        e->first_ms = now_ms;
    }
    e->seen_ms = now_ms;
    e->rssi = (int8_t)(rssi < -128 ? -128 : rssi > 127 ? 127 : rssi);
    e->kinds |= adv->kinds;
    if (adv->has_name)
        snprintf(e->name, sizeof e->name, "%s", adv->name);
    if (e->kinds && !(e->told & 1)) {
        e->told |= 1;
        if (e->name[0])
            e->told |= 2;
        *announce = 1;
    } else if (e->kinds && e->name[0] && !(e->told & 2)) {
        e->told |= 2;
        *announce = 1;
    }
    return e;
}

void fw_scan_expire(fw_scan_list_t *l, uint32_t now_ms, uint32_t max_age_ms)
{
    int w = 0;
    for (int i = 0; i < l->n; i++)
        if (now_ms - l->e[i].seen_ms <= max_age_ms)
            l->e[w++] = l->e[i];
    l->n = w;
}

int fw_name_interesting(const char *name)
{
    static const char *const KEY[] = {"CYCPLUS", "H1", "GEOID", "PM500", "PM", "HR", "HEART", "POWER"};
    char up[FW_NAME_MAX + 1];
    size_t n = 0;
    for (; name[n] && n < FW_NAME_MAX; n++)
        up[n] = name[n] >= 'a' && name[n] <= 'z' ? (char)(name[n] - 32) : name[n];
    up[n] = 0;
    for (size_t i = 0; i < sizeof KEY / sizeof KEY[0]; i++)
        if (strstr(up, KEY[i]))
            return 1;
    return 0;
}

int fw_scan_should_dump(fw_scan_ent_t *e, int is_scan_rsp)
{
    uint8_t bit = is_scan_rsp ? 8 : 4;
    if (e->told & bit)
        return 0;
    if (!e->kinds && e->name[0] && !fw_name_interesting(e->name))
        return 0;
    e->told |= bit;
    return 1;
}

int fw_hex(const uint8_t *b, size_t n, char *out, size_t cap)
{
    static const char H[] = "0123456789abcdef";
    size_t o = 0;
    if (cap == 0)
        return 0;
    for (size_t i = 0; i < n && o + 2 < cap; i++) {
        out[o++] = H[b[i] >> 4];
        out[o++] = H[b[i] & 15];
    }
    out[o] = 0;
    return (int)o;
}

void fw_uuid_list(const fw_adv_t *a, char *out, size_t cap)
{
    size_t o = 0;
    if (cap == 0)
        return;
    for (int i = 0; i < a->n_uuids; i++) {
        uint8_t be[2] = {(uint8_t)(a->uuids[i] >> 8), (uint8_t)a->uuids[i]};
        size_t need = (i ? 1u : 0u) + 4u;
        if (o + need >= cap)
            break;
        if (i)
            out[o++] = ',';
        o += (size_t)fw_hex(be, 2, out + o, cap - o);
    }
    out[o] = 0;
}

int fw_scan_kind(uint8_t kinds, uint8_t want_mask)
{
    uint8_t k = kinds & want_mask;
    return k & FW_KIND_BIT(FW_KIND_POWER) ? FW_KIND_POWER : k & FW_KIND_BIT(FW_KIND_HR) ? FW_KIND_HR : 0;
}

int fw_scan_visible(const fw_scan_list_t *l, uint8_t want_mask, int *idx, int max)
{
    int all[FW_SCAN_MAX], n = 0;
    for (int i = 0; i < l->n && i < FW_SCAN_MAX; i++) {
        if (!fw_scan_kind(l->e[i].kinds, want_mask))
            continue;
        int j = n++; /* insertion sort by first heard; ties keep list order */
        while (j > 0 && (int32_t)(l->e[i].first_ms - l->e[all[j - 1]].first_ms) < 0) {
            all[j] = all[j - 1];
            j--;
        }
        all[j] = i;
    }
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        idx[i] = all[i];
    return n;
}

int fw_rssi_bars(int rssi) { return rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : 1; }

void fw_addr_id4(const uint8_t addr[6], char out[5]) { snprintf(out, 5, "%02X%02X", addr[1], addr[0]); }

void fw_addr_str(const uint8_t addr[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

uint32_t fw_backoff_ms(unsigned failures)
{
    static const uint32_t STEP[] = {1000, 2000, 4000, 8000, 15000, 30000};
    if (failures == 0)
        return 0;
    return failures >= sizeof STEP / sizeof STEP[0] ? 30000 : STEP[failures - 1];
}

uint16_t fw_dsc_owner(const uint16_t *vals, int n, uint16_t dsc_handle)
{
    uint16_t best = 0;
    for (int i = 0; i < n; i++)
        if (vals[i] < dsc_handle && vals[i] > best)
            best = vals[i];
    return best;
}
