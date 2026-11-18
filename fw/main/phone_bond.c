/* phone_bond.c: the bonded phone's record on the eMMC (phone_bond.h, Q43). Pure C. */
#include "phone_bond.h"

#include <string.h>

#include "cc700/util.h"

#define SEC_LEN 72
#define CCCD_LEN 12
#define IRK_LEN 23
#define HDR_LEN 10

static int addr_eq(const fw_bond_addr_t *a, const fw_bond_addr_t *b)
{
    return a->type == b->type && memcmp(a->val, b->val, 6) == 0;
}

int fw_bond_phone_count(const fw_bond_t *b)
{
    int n = 0;
    for (int i = 0; i < b->nsec && i < FW_BOND_MAX_SEC; i++)
        n += b->sec[i].kind == FW_BOND_PEER && b->sec[i].authenticated;
    return n;
}

static int is_phone(const fw_bond_t *all, const fw_bond_addr_t *a)
{
    for (int i = 0; i < all->nsec && i < FW_BOND_MAX_SEC; i++)
        if (all->sec[i].kind == FW_BOND_PEER && all->sec[i].authenticated && addr_eq(&all->sec[i].peer, a))
            return 1;
    return 0;
}

int fw_bond_filter(const fw_bond_t *all, fw_bond_t *out)
{
    memset(out, 0, sizeof *out);
    for (int i = 0; i < all->nsec && i < FW_BOND_MAX_SEC; i++)
        if (is_phone(all, &all->sec[i].peer))
            out->sec[out->nsec++] = all->sec[i];
    for (int i = 0; i < all->ncccd && i < FW_BOND_MAX_CCCD; i++)
        if (is_phone(all, &all->cccd[i].peer))
            out->cccd[out->ncccd++] = all->cccd[i];
    int phones = fw_bond_phone_count(out);
    if (phones && all->has_local_irk) {
        out->has_local_irk = 1;
        out->local_addr = all->local_addr;
        memcpy(out->local_irk, all->local_irk, 16);
    }
    return phones;
}

static void put_addr(uint8_t *p, const fw_bond_addr_t *a)
{
    p[0] = a->type;
    memcpy(p + 1, a->val, 6);
}

static void get_addr(const uint8_t *p, fw_bond_addr_t *a)
{
    a->type = p[0];
    memcpy(a->val, p + 1, 6);
}

size_t fw_bond_encode(const fw_bond_t *b, uint32_t seq, uint8_t *out, size_t cap)
{
    if (b->nsec > FW_BOND_MAX_SEC || b->ncccd > FW_BOND_MAX_CCCD)
        return 0;
    size_t need = HDR_LEN + (size_t)b->nsec * SEC_LEN + (size_t)b->ncccd * CCCD_LEN +
                  (b->has_local_irk ? IRK_LEN : 0) + 4;
    if (need > cap)
        return 0;
    size_t o = 0;
    out[o++] = 'P';
    out[o++] = 'B';
    out[o++] = 1;
    cc_put_u32(out + o, seq);
    o += 4;
    out[o++] = b->nsec;
    out[o++] = b->ncccd;
    out[o++] = b->has_local_irk ? 1 : 0;
    for (int i = 0; i < b->nsec; i++) {
        const fw_bond_sec_t *s = &b->sec[i];
        out[o++] = s->kind;
        put_addr(out + o, &s->peer);
        o += 7;
        out[o++] = s->key_size;
        cc_put_u16(out + o, s->ediv);
        o += 2;
        cc_put_u32(out + o, (uint32_t)s->rand_num);
        cc_put_u32(out + o + 4, (uint32_t)(s->rand_num >> 32));
        o += 8;
        out[o++] = (uint8_t)((s->ltk_present ? 1 : 0) | (s->irk_present ? 2 : 0) | (s->csrk_present ? 4 : 0) |
                             (s->authenticated ? 8 : 0) | (s->sc ? 16 : 0));
        memcpy(out + o, s->ltk, 16);
        memcpy(out + o + 16, s->irk, 16);
        memcpy(out + o + 32, s->csrk, 16);
        o += 48;
        cc_put_u32(out + o, s->sign_counter);
        o += 4;
    }
    for (int i = 0; i < b->ncccd; i++) {
        const fw_bond_cccd_t *c = &b->cccd[i];
        put_addr(out + o, &c->peer);
        o += 7;
        cc_put_u16(out + o, c->chr_val_handle);
        cc_put_u16(out + o + 2, c->flags);
        o += 4;
        out[o++] = c->value_changed ? 1 : 0;
    }
    if (b->has_local_irk) {
        put_addr(out + o, &b->local_addr);
        o += 7;
        memcpy(out + o, b->local_irk, 16);
        o += 16;
    }
    cc_put_u32(out + o, cc_crc32(out, o));
    return o + 4;
}

int fw_bond_decode(const uint8_t *buf, size_t len, fw_bond_t *b, uint32_t *seq)
{
    memset(b, 0, sizeof *b);
    if (len < HDR_LEN + 4 || buf[0] != 'P' || buf[1] != 'B' || buf[2] != 1)
        return 0;
    uint8_t nsec = buf[7], ncccd = buf[8], irk = buf[9];
    if (nsec > FW_BOND_MAX_SEC || ncccd > FW_BOND_MAX_CCCD || irk > 1)
        return 0;
    size_t body = HDR_LEN + (size_t)nsec * SEC_LEN + (size_t)ncccd * CCCD_LEN + (irk ? IRK_LEN : 0);
    if (len != body + 4 || cc_get_u32(buf + body) != cc_crc32(buf, body))
        return 0;
    *seq = cc_get_u32(buf + 3);
    size_t o = HDR_LEN;
    for (int i = 0; i < nsec; i++) {
        fw_bond_sec_t *s = &b->sec[i];
        s->kind = buf[o++];
        if (s->kind != FW_BOND_OUR && s->kind != FW_BOND_PEER) {
            memset(b, 0, sizeof *b);
            return 0;
        }
        get_addr(buf + o, &s->peer);
        o += 7;
        s->key_size = buf[o++];
        s->ediv = cc_get_u16(buf + o);
        o += 2;
        s->rand_num = (uint64_t)cc_get_u32(buf + o) | ((uint64_t)cc_get_u32(buf + o + 4) << 32);
        o += 8;
        uint8_t f = buf[o++];
        s->ltk_present = f & 1;
        s->irk_present = (f >> 1) & 1;
        s->csrk_present = (f >> 2) & 1;
        s->authenticated = (f >> 3) & 1;
        s->sc = (f >> 4) & 1;
        memcpy(s->ltk, buf + o, 16);
        memcpy(s->irk, buf + o + 16, 16);
        memcpy(s->csrk, buf + o + 32, 16);
        o += 48;
        s->sign_counter = cc_get_u32(buf + o);
        o += 4;
    }
    for (int i = 0; i < ncccd; i++) {
        fw_bond_cccd_t *c = &b->cccd[i];
        get_addr(buf + o, &c->peer);
        o += 7;
        c->chr_val_handle = cc_get_u16(buf + o);
        c->flags = cc_get_u16(buf + o + 2);
        o += 4;
        c->value_changed = buf[o++] & 1;
    }
    if (irk) {
        b->has_local_irk = 1;
        get_addr(buf + o, &b->local_addr);
        o += 7;
        memcpy(b->local_irk, buf + o, 16);
    }
    b->nsec = nsec;
    b->ncccd = ncccd;
    return 1;
}

/* ------------------------------------------------------------------ two slots */
static int read_slot(hal_fs_t *fs, const char *path, fw_bond_t *b, uint32_t *seq)
{
    static uint8_t buf[FW_BOND_REC_MAX + 1]; /* one caller at a time: the core task */
    hal_file_t f = fs->open(fs, path, HAL_FS_READ);
    if (f < 0)
        return 0;
    int32_t n = fs->read(fs, f, buf, sizeof buf);
    fs->close(fs, f);
    return n > 0 && n <= FW_BOND_REC_MAX && fw_bond_decode(buf, (size_t)n, b, seq);
}

int fw_bond_load(hal_fs_t *fs, fw_bond_t *b, fw_bond_slots_t *slots)
{
    static fw_bond_t tmp[2];
    uint32_t seq[2] = {0, 0};
    int ok[2];
    ok[0] = read_slot(fs, FW_BOND_A, &tmp[0], &seq[0]);
    ok[1] = read_slot(fs, FW_BOND_B, &tmp[1], &seq[1]);
    int pick = ok[0] && ok[1] ? ((int32_t)(seq[1] - seq[0]) > 0 ? 1 : 0) : ok[0] ? 0 : ok[1] ? 1 : -1;
    slots->newest = pick;
    slots->seq = pick >= 0 ? seq[pick] : 0;
    if (pick < 0) {
        memset(b, 0, sizeof *b);
        return 0;
    }
    *b = tmp[pick];
    memset(tmp, 0, sizeof tmp); /* no stray copy of the keys */
    return 1;
}

int fw_bond_save(hal_fs_t *fs, const fw_bond_t *b, fw_bond_slots_t *slots)
{
    static uint8_t rec[FW_BOND_REC_MAX];
    uint32_t seq = slots->newest >= 0 ? slots->seq + 1 : 1;
    size_t n = fw_bond_encode(b, seq, rec, sizeof rec);
    if (!n)
        return -1;
    int target = slots->newest == 0 ? 1 : 0;
    const char *path = target ? FW_BOND_B : FW_BOND_A;
    fs->mkdir(fs, "config");
    hal_file_t f = fs->open(fs, path, HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    int ok = f >= 0;
    if (ok) {
        ok = fs->write(fs, f, rec, (uint32_t)n) == (int32_t)n && fs->fsync(fs, f) == 0;
        if (fs->close(fs, f) != 0)
            ok = 0;
    }
    memset(rec, 0, sizeof rec);
    if (!ok)
        return -1;
    slots->newest = target;
    slots->seq = seq;
    return 0;
}

static int exists(hal_fs_t *fs, const char *path)
{
    hal_file_t f = fs->open(fs, path, HAL_FS_READ);
    if (f < 0)
        return 0;
    fs->close(fs, f);
    return 1;
}

int fw_bond_erase(hal_fs_t *fs, fw_bond_slots_t *slots)
{
    if (exists(fs, FW_BOND_A))
        fs->remove(fs, FW_BOND_A);
    if (exists(fs, FW_BOND_B))
        fs->remove(fs, FW_BOND_B);
    slots->newest = -1;
    slots->seq = 0;
    return exists(fs, FW_BOND_A) || exists(fs, FW_BOND_B) ? -1 : 0;
}
