#include "cc700/settings.h"

#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"

#define SET_LEN 39
#define SLOT_MAX 256

void cc_settings_defaults(cc_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->ftp = CC_FTP_DEFAULT_W;
    s->brightness = CC_BRIGHT_AUTO;
    s->screen_off = CC_SCREENOFF_1MIN;
    s->units = CC_UNITS_KM;
    s->autopause = 1;
    s->tz_min = 0;
}

size_t cc_settings_encode(const cc_settings_t *s, uint32_t seq, uint8_t *b)
{
    b[0] = 'C'; b[1] = 'S'; b[2] = 2;
    cc_put_u32(b + 3, seq);
    cc_put_u16(b + 7, s->ftp);
    b[9] = s->brightness;
    b[10] = s->screen_off;
    b[11] = s->units;
    b[12] = s->autopause;
    cc_put_u16(b + 13, (uint16_t)s->tz_min);
    memcpy(b + 15, s->workout_id, 8);
    memcpy(b + 23, s->route_id, 8);
    cc_put_u32(b + 31, s->ride_counter);
    cc_put_u32(b + 35, cc_crc32(b, 35));
    return SET_LEN;
}

int cc_settings_decode(const uint8_t *b, size_t len, cc_settings_t *s, uint32_t *seq)
{
    if (len < SET_LEN || b[0] != 'C' || b[1] != 'S' || b[2] != 2)
        return 0;
    if (cc_get_u32(b + 35) != cc_crc32(b, 35))
        return 0;
    *seq = cc_get_u32(b + 3);
    s->ftp = cc_get_u16(b + 7);
    s->brightness = b[9];
    s->screen_off = b[10];
    s->units = b[11];
    s->autopause = b[12];
    s->tz_min = cc_get_i16(b + 13);
    memcpy(s->workout_id, b + 15, 8);
    memcpy(s->route_id, b + 23, 8);
    s->ride_counter = cc_get_u32(b + 31);
    /* Out-of-range values fall back to defaults rather than refusing the record */
    if (s->ftp < CC_FTP_MIN_W || s->ftp > CC_FTP_MAX_W) s->ftp = CC_FTP_DEFAULT_W;
    if (s->brightness >= CC_BRIGHT_N) s->brightness = CC_BRIGHT_AUTO;
    if (s->screen_off >= CC_SCREENOFF_N) s->screen_off = CC_SCREENOFF_1MIN;
    if (s->units >= CC_UNITS_N) s->units = CC_UNITS_KM;
    s->autopause = s->autopause ? 1 : 0;
    return 1;
}

static int32_t read_file(hal_fs_t *fs, const char *path, uint8_t *buf, uint32_t cap)
{
    hal_file_t f = fs->open(fs, path, HAL_FS_READ);
    if (f < 0)
        return -1;
    int32_t n = fs->read(fs, f, buf, cap);
    fs->close(fs, f);
    return n;
}

static int write_slot(hal_fs_t *fs, const char *path, const uint8_t *buf, uint32_t len)
{
    fs->mkdir(fs, "config");
    hal_file_t f = fs->open(fs, path, HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    if (f < 0)
        return -1;
    int ok = fs->write(fs, f, buf, len) == (int32_t)len && fs->fsync(fs, f) == 0;
    if (fs->close(fs, f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

typedef int (*decode_fn)(const uint8_t *b, size_t len, void *out, uint32_t *seq);

static int load_slots(hal_fs_t *fs, const char *a, const char *b, decode_fn dec, void *out, size_t out_size,
                      cc_slots_t *slots)
{
    uint8_t buf[SLOT_MAX];
    union { cc_settings_t s; cc_bonds_t b; cc_profile_t p; } tmp[2];
    uint32_t seq[2];
    int ok[2];
    const char *paths[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        int32_t n = read_file(fs, paths[i], buf, sizeof buf);
        ok[i] = n > 0 && dec(buf, (size_t)n, &tmp[i], &seq[i]);
    }
    int pick = -1;
    if (ok[0] && ok[1])
        pick = cc_seq_newer(seq[1], seq[0]) ? 1 : 0;
    else if (ok[0])
        pick = 0;
    else if (ok[1])
        pick = 1;
    slots->newest = pick;
    slots->seq = pick >= 0 ? seq[pick] : 0;
    if (pick < 0)
        return 0;
    memcpy(out, &tmp[pick], out_size);
    return 1;
}

static int save_slots(hal_fs_t *fs, const char *a, const char *b, const uint8_t *rec, uint32_t len,
                      cc_slots_t *slots, uint32_t seq)
{
    int target = slots->newest == 0 ? 1 : 0; /* none valid: slot a */
    if (write_slot(fs, target ? b : a, rec, len) != 0)
        return -1;
    slots->newest = target;
    slots->seq = seq;
    return 0;
}

static int dec_settings(const uint8_t *b, size_t len, void *out, uint32_t *seq)
{
    return cc_settings_decode(b, len, out, seq);
}

int cc_settings_load(hal_fs_t *fs, cc_settings_t *s, cc_slots_t *slots)
{
    cc_settings_t tmp;
    if (load_slots(fs, CC_SETTINGS_A, CC_SETTINGS_B, dec_settings, &tmp, sizeof tmp, slots)) {
        *s = tmp;
        return 1;
    }
    cc_settings_defaults(s);
    return 0;
}

int cc_settings_save(hal_fs_t *fs, const cc_settings_t *s, cc_slots_t *slots)
{
    uint8_t rec[SET_LEN];
    uint32_t seq = slots->newest >= 0 ? slots->seq + 1 : 1;
    cc_settings_encode(s, seq, rec);
    return save_slots(fs, CC_SETTINGS_A, CC_SETTINGS_B, rec, SET_LEN, slots, seq);
}

/* ---------------------------------------------------------------- rider profile */
int cc_max_hr_valid(int32_t v)
{
    return v == CC_MAXHR_OFF || (v >= CC_MAXHR_MIN_BPM && v <= CC_MAXHR_MAX_BPM);
}

void cc_profile_defaults(cc_profile_t *p)
{
    memset(p, 0, sizeof *p);
    p->max_hr = CC_MAXHR_DEFAULT_BPM;
}

size_t cc_profile_encode(const cc_profile_t *p, uint32_t seq, uint8_t *b)
{
    b[0] = 'C'; b[1] = 'P'; b[2] = 1;
    cc_put_u32(b + 3, seq);
    cc_put_u16(b + 7, p->max_hr);
    cc_put_u32(b + 9, cc_crc32(b, 9));
    return CC_PROFILE_LEN;
}

int cc_profile_decode(const uint8_t *b, size_t len, cc_profile_t *p, uint32_t *seq)
{
    if (len < CC_PROFILE_LEN || b[0] != 'C' || b[1] != 'P' || b[2] != 1)
        return 0;
    if (cc_get_u32(b + 9) != cc_crc32(b, 9))
        return 0;
    *seq = cc_get_u32(b + 3);
    cc_profile_defaults(p);
    p->max_hr = cc_get_u16(b + 7);
    /* Out of range falls back to "not set" rather than refusing the record, as settings do */
    if (!cc_max_hr_valid(p->max_hr)) p->max_hr = CC_MAXHR_DEFAULT_BPM;
    return 1;
}

static int dec_profile(const uint8_t *b, size_t len, void *out, uint32_t *seq)
{
    return cc_profile_decode(b, len, out, seq);
}

int cc_profile_load(hal_fs_t *fs, cc_profile_t *p, cc_slots_t *slots)
{
    cc_profile_t tmp;
    if (load_slots(fs, CC_PROFILE_A, CC_PROFILE_B, dec_profile, &tmp, sizeof tmp, slots)) {
        *p = tmp;
        return 1;
    }
    cc_profile_defaults(p);
    return 0;
}

int cc_profile_save(hal_fs_t *fs, const cc_profile_t *p, cc_slots_t *slots)
{
    uint8_t rec[CC_PROFILE_LEN];
    uint32_t seq = slots->newest >= 0 ? slots->seq + 1 : 1;
    cc_profile_encode(p, seq, rec);
    return save_slots(fs, CC_PROFILE_A, CC_PROFILE_B, rec, CC_PROFILE_LEN, slots, seq);
}

/* ---------------------------------------------------------------- bonds */
static uint32_t bonds_encode(const cc_bonds_t *bd, uint32_t seq, uint8_t *b)
{
    uint32_t o = 0;
    b[o++] = 'C'; b[o++] = 'B'; b[o++] = 1;
    cc_put_u32(b + o, seq); o += 4;
    b[o++] = bd->count;
    for (int i = 0; i < bd->count; i++) {
        const cc_bond_t *e = &bd->b[i];
        size_t nl = strlen(e->name);
        b[o++] = e->kind;
        b[o++] = e->addr_type;
        memcpy(b + o, e->addr, 6); o += 6;
        b[o++] = (uint8_t)nl;
        memcpy(b + o, e->name, nl); o += (uint32_t)nl;
    }
    cc_put_u32(b + o, cc_crc32(b, o));
    return o + 4;
}

static int dec_bonds(const uint8_t *b, size_t len, void *out, uint32_t *seq)
{
    cc_bonds_t *bd = out;
    if (len < 12 || b[0] != 'C' || b[1] != 'B' || b[2] != 1)
        return 0;
    *seq = cc_get_u32(b + 3);
    size_t o = 7;
    bd->count = b[o++];
    if (bd->count > CC_MAX_BONDS)
        return 0;
    for (int i = 0; i < bd->count; i++) {
        cc_bond_t *e = &bd->b[i];
        if (o + 9 > len)
            return 0;
        e->kind = b[o++];
        e->addr_type = b[o++];
        memcpy(e->addr, b + o, 6); o += 6;
        size_t nl = b[o++];
        if (nl > CC_BOND_NAME || o + nl > len)
            return 0;
        memcpy(e->name, b + o, nl);
        e->name[nl] = 0;
        o += nl;
    }
    if (o + 4 > len || cc_get_u32(b + o) != cc_crc32(b, o))
        return 0;
    return 1;
}

int cc_bonds_load(hal_fs_t *fs, cc_bonds_t *b, cc_slots_t *slots)
{
    cc_bonds_t tmp;
    memset(&tmp, 0, sizeof tmp);
    if (load_slots(fs, CC_SENSORS_A, CC_SENSORS_B, dec_bonds, &tmp, sizeof tmp, slots)) {
        *b = tmp;
        return 1;
    }
    memset(b, 0, sizeof *b);
    return 0;
}

const cc_bond_t *cc_bonds_find(const cc_bonds_t *b, uint8_t kind)
{
    for (int i = 0; i < b->count && i < CC_MAX_BONDS; i++)
        if (b->b[i].kind == kind)
            return &b->b[i];
    return NULL;
}

int cc_bonds_remove(cc_bonds_t *b, uint8_t kind)
{
    int removed = 0;
    for (int i = 0; i < b->count && i < CC_MAX_BONDS;) {
        if (b->b[i].kind == kind) {
            memmove(&b->b[i], &b->b[i + 1], (size_t)(b->count - i - 1) * sizeof b->b[0]);
            b->count--;
            memset(&b->b[b->count], 0, sizeof b->b[0]);
            removed = 1;
        } else {
            i++;
        }
    }
    return removed;
}

int cc_bonds_put(cc_bonds_t *b, const cc_bond_t *e)
{
    if (e->kind != 1 && e->kind != 2)
        return -1;
    cc_bond_t copy = *e; /* e may point into b */
    copy.name[CC_BOND_NAME] = 0;
    cc_bonds_remove(b, copy.kind);
    if (b->count >= CC_MAX_BONDS)
        return -1;
    b->b[b->count++] = copy;
    return 0;
}

int cc_bonds_save(hal_fs_t *fs, const cc_bonds_t *b, cc_slots_t *slots)
{
    uint8_t rec[SLOT_MAX];
    uint32_t seq = slots->newest >= 0 ? slots->seq + 1 : 1;
    uint32_t n = bonds_encode(b, seq, rec);
    return save_slots(fs, CC_SENSORS_A, CC_SENSORS_B, rec, n, slots, seq);
}
