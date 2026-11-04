#include "cc700/store.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"

#define META_LEN 64

void cc_ride_path(char *out, int cap, uint32_t ride_id, const char *ext)
{
    snprintf(out, (size_t)cap, "rides/%" PRIu32 "%s", ride_id, ext);
}

void cc_object_path(char *out, int cap, const uint8_t id[8], int kind)
{
    snprintf(out, (size_t)cap, "objects/%02x%02x%02x%02x%02x%02x%02x%02x.%s", id[0], id[1], id[2], id[3], id[4], id[5],
             id[6], id[7], kind == 1 ? "cw1" : kind == 2 ? "cr1" : "tmp");
}

/* ------------------------------------------------------------------ meta */
static void meta_encode(const cc_ride_meta_t *m, uint8_t *b)
{
    const cc_fit_summary_t *s = &m->sum;
    memset(b, 0, META_LEN);
    b[0] = 'C'; b[1] = 'M'; b[2] = 1;
    b[3] = m->flags & (CC_RIDE_RECOVERED | CC_RIDE_TIME_UNKNOWN);
    cc_put_u32(b + 4, m->ride_id);
    cc_put_u32(b + 8, m->size);
    cc_put_u32(b + 12, m->crc32);
    cc_put_u32(b + 16, s->start_ts);
    cc_put_u32(b + 20, s->elapsed_s);
    cc_put_u32(b + 24, s->timer_s);
    cc_put_u32(b + 28, s->distance_m);
    cc_put_u16(b + 32, s->avg_power);
    cc_put_u16(b + 34, s->max_power);
    b[36] = (uint8_t)((s->has_power ? 1 : 0) | (s->has_hr ? 2 : 0) | (s->has_cad ? 4 : 0));
    b[37] = s->avg_hr;
    b[38] = s->max_hr;
    b[39] = s->avg_cad;
    cc_put_u16(b + 40, s->ascent_m);
    cc_put_u32(b + 42, s->avg_speed_mms);
    cc_put_u32(b + 46, s->records);
    cc_put_u16(b + 50, s->laps);
    cc_put_u32(b + 60, cc_crc32(b, 60));
}

static int meta_decode(const uint8_t *b, uint32_t len, cc_ride_meta_t *m)
{
    if (len < META_LEN || b[0] != 'C' || b[1] != 'M' || b[2] != 1 || cc_get_u32(b + 60) != cc_crc32(b, 60))
        return 0;
    cc_fit_summary_t *s = &m->sum;
    memset(m, 0, sizeof *m);
    m->flags = b[3];
    m->ride_id = cc_get_u32(b + 4);
    m->size = cc_get_u32(b + 8);
    m->crc32 = cc_get_u32(b + 12);
    s->start_ts = cc_get_u32(b + 16);
    s->elapsed_s = cc_get_u32(b + 20);
    s->timer_s = cc_get_u32(b + 24);
    s->distance_m = cc_get_u32(b + 28);
    s->avg_power = cc_get_u16(b + 32);
    s->max_power = cc_get_u16(b + 34);
    s->has_power = b[36] & 1;
    s->has_hr = (b[36] >> 1) & 1;
    s->has_cad = (b[36] >> 2) & 1;
    s->avg_hr = b[37];
    s->max_hr = b[38];
    s->avg_cad = b[39];
    s->ascent_m = cc_get_u16(b + 40);
    s->avg_speed_mms = cc_get_u32(b + 42);
    s->records = cc_get_u32(b + 46);
    s->laps = cc_get_u16(b + 50);
    return 1;
}

static int write_new_file(hal_fs_t *fs, const char *path, const void *data, uint32_t len)
{
    hal_file_t f = fs->open(fs, path, HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    if (f < 0)
        return -1;
    int ok = (len == 0 || fs->write(fs, f, data, len) == (int32_t)len) && fs->fsync(fs, f) == 0;
    if (fs->close(fs, f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

int cc_ride_meta_write(hal_fs_t *fs, const cc_ride_meta_t *m)
{
    uint8_t b[META_LEN];
    char tmp[64], fin[64];
    meta_encode(m, b);
    cc_ride_path(tmp, sizeof tmp, m->ride_id, ".meta.tmp");
    cc_ride_path(fin, sizeof fin, m->ride_id, ".meta");
    fs->remove(fs, tmp);
    if (write_new_file(fs, tmp, b, META_LEN) != 0)
        return -1;
    fs->remove(fs, fin); /* only ever a stale or corrupt one: meta is written once per ride */
    return fs->rename(fs, tmp, fin);
}

int cc_ride_meta_read(hal_fs_t *fs, uint32_t ride_id, cc_ride_meta_t *m)
{
    char p[64];
    uint8_t b[META_LEN];
    cc_ride_path(p, sizeof p, ride_id, ".meta");
    hal_file_t f = fs->open(fs, p, HAL_FS_READ);
    if (f < 0)
        return 0;
    int32_t n = fs->read(fs, f, b, META_LEN);
    fs->close(fs, f);
    if (n != META_LEN || !meta_decode(b, (uint32_t)n, m))
        return 0;
    if (cc_ride_uploaded(fs, ride_id))
        m->flags |= CC_RIDE_UPLOADED;
    return 1;
}

/* ------------------------------------------------------------------ listing */
typedef struct {
    const char *suffix;
    uint32_t *ids;
    int cap, n;
    char (*names)[HAL_FS_MAX_NAME];
} lister_t;

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && strcmp(s + a - b, suf) == 0;
}

static int parse_id(const char *name, const char *suf, uint32_t *id)
{
    if (!ends_with(name, suf))
        return 0;
    size_t n = strlen(name) - strlen(suf);
    if (n == 0 || n > 10)
        return 0;
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (name[i] < '0' || name[i] > '9')
            return 0;
        v = v * 10 + (uint64_t)(name[i] - '0');
    }
    if (v > 0xFFFFFFFFu)
        return 0;
    *id = (uint32_t)v;
    return 1;
}

static int list_cb(void *ctx, const hal_dirent_t *e)
{
    lister_t *l = ctx;
    uint32_t id;
    if (l->n < l->cap && parse_id(e->name, l->suffix, &id))
        l->ids[l->n++] = id;
    return 0;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int list_suffix(hal_fs_t *fs, const char *suffix, uint32_t *ids, int cap)
{
    lister_t l = {suffix, ids, cap, 0, NULL};
    fs->list(fs, "rides", list_cb, &l);
    qsort(ids, (size_t)l.n, sizeof ids[0], cmp_u32);
    return l.n;
}

int cc_rides_list(hal_fs_t *fs, uint32_t *ids, int cap) { return list_suffix(fs, ".fit", ids, cap); }

int cc_ride_uploaded(hal_fs_t *fs, uint32_t ride_id)
{
    char p[64];
    cc_ride_path(p, sizeof p, ride_id, ".up");
    hal_file_t f = fs->open(fs, p, HAL_FS_READ);
    if (f < 0)
        return 0;
    fs->close(fs, f);
    return 1;
}

int cc_ride_mark(hal_fs_t *fs, uint32_t ride_id, int uploaded)
{
    char p[64];
    cc_ride_path(p, sizeof p, ride_id, ".fit");
    hal_file_t f = fs->open(fs, p, HAL_FS_READ);
    if (f < 0)
        return -2; /* no such ride */
    fs->close(fs, f);
    cc_ride_path(p, sizeof p, ride_id, ".up");
    if (uploaded)
        return cc_ride_uploaded(fs, ride_id) ? 0 : write_new_file(fs, p, NULL, 0);
    fs->remove(fs, p);
    return 0;
}

int cc_ride_delete(hal_fs_t *fs, uint32_t ride_id)
{
    static const char *ext[] = {".fit", ".meta", ".up", ".meta.tmp", ".time"};
    char p[64];
    cc_ride_path(p, sizeof p, ride_id, ".fit");
    if (fs->remove(fs, p) != 0)
        return -2;
    for (unsigned i = 1; i < sizeof ext / sizeof ext[0]; i++) {
        cc_ride_path(p, sizeof p, ride_id, ext[i]);
        fs->remove(fs, p);
    }
    return 0;
}

int cc_rides_retention(hal_fs_t *fs)
{
    static uint32_t ids[CC_RIDES_HARD_MAX + 64];
    int n = cc_rides_list(fs, ids, (int)(sizeof ids / sizeof ids[0]));
    int deleted = 0;
    int i = 0;
    while (n - deleted > CC_RIDES_KEEP && i < n) {
        if (ids[i] && cc_ride_uploaded(fs, ids[i])) {
            cc_ride_delete(fs, ids[i]);
            ids[i] = 0;
            deleted++;
        }
        i++;
    }
    for (i = 0; n - deleted > CC_RIDES_HARD_MAX && i < n; i++) {
        if (ids[i]) {
            cc_ride_delete(fs, ids[i]);
            ids[i] = 0;
            deleted++;
        }
    }
    return deleted;
}

/* ------------------------------------------------------------------ finish, discard */
static int read_offset(hal_fs_t *fs, uint32_t ride_id, int32_t *off)
{
    char p[64];
    uint8_t b[8];
    cc_ride_path(p, sizeof p, ride_id, ".time");
    hal_file_t f = fs->open(fs, p, HAL_FS_READ);
    if (f < 0)
        return 0;
    int32_t n = fs->read(fs, f, b, 8);
    fs->close(fs, f);
    if (n != 8 || cc_get_u32(b + 4) != cc_crc32(b, 4))
        return 0;
    *off = cc_get_i32(b);
    return 1;
}

static uint32_t free_id(hal_fs_t *fs, uint32_t id)
{
    char p[64];
    for (int k = 0; k < 100; k++, id++) {
        cc_ride_path(p, sizeof p, id, ".fit");
        hal_file_t f = fs->open(fs, p, HAL_FS_READ);
        if (f < 0)
            return id;
        fs->close(fs, f);
    }
    return id;
}

static int complete(hal_fs_t *fs, uint32_t old_id, uint32_t new_id, uint8_t flags, const cc_fit_summary_t *s,
                    uint32_t crc, uint32_t size, cc_ride_meta_t *out)
{
    cc_ride_meta_t m;
    memset(&m, 0, sizeof m);
    m.ride_id = new_id;
    m.flags = flags;
    m.size = size;
    m.crc32 = crc;
    m.sum = *s;
    if (cc_ride_meta_write(fs, &m) != 0)
        return -1;
    char p[64];
    cc_ride_path(p, sizeof p, old_id, ".time");
    fs->remove(fs, p);
    if (out)
        *out = m;
    cc_rides_retention(fs);
    return 0;
}

int cc_ride_finish(hal_fs_t *fs, cc_fit_t *w, uint32_t ride_id, uint32_t end_ts, int32_t ascent_m, int have_offset,
                   int32_t offset, cc_ride_meta_t *meta_out)
{
    if (cc_fit_close_messages(w, end_ts, ascent_m) != 0)
        return -1;
    /* A ride started without UTC is renamed to its corrected start time once the offset is known */
    int correct = have_offset && (offset != 0 || ride_id < CC_FIT_PROVISIONAL_BASE);
    uint32_t new_id = correct ? free_id(fs, w->session.start_ts + (uint32_t)offset) : ride_id;
    char fin[64];
    cc_ride_path(fin, sizeof fin, new_id, ".fit");
    cc_fit_summary_t s;
    uint32_t crc, size;
    if (cc_fit_finalise(w, correct ? offset : 0, fin, &s, &crc, &size) != 0)
        return -1;
    return complete(fs, ride_id, new_id, have_offset ? 0 : CC_RIDE_TIME_UNKNOWN, &s, crc, size, meta_out);
}

int cc_ride_discard(hal_fs_t *fs, cc_fit_t *w, uint32_t ride_id)
{
    char p[64];
    if (w->open)
        fs->close(fs, w->f);
    w->open = 0;
    cc_ride_path(p, sizeof p, ride_id, ".fit.part");
    fs->remove(fs, p);
    cc_ride_path(p, sizeof p, ride_id, ".time");
    fs->remove(fs, p);
    return 0;
}

/* ------------------------------------------------------------------ boot */
typedef struct { char names[64][HAL_FS_MAX_NAME]; int n; const char *suffix; } names_t;
static int names_cb(void *ctx, const hal_dirent_t *e)
{
    names_t *l = ctx;
    if (l->n < 64 && ends_with(e->name, l->suffix))
        snprintf(l->names[l->n++], HAL_FS_MAX_NAME, "%s", e->name);
    return 0;
}

int cc_store_boot(hal_fs_t *fs)
{
    static names_t l;
    char p[96];
    fs->mkdir(fs, "objects");
    fs->mkdir(fs, "rides");
    fs->mkdir(fs, "config");

    l.n = 0; l.suffix = ".tmp";
    fs->list(fs, "objects", names_cb, &l);
    for (int i = 0; i < l.n; i++) {
        snprintf(p, sizeof p, "objects/%s", l.names[i]);
        fs->remove(fs, p);
    }
    l.n = 0; l.suffix = ".meta.tmp";
    fs->list(fs, "rides", names_cb, &l);
    for (int i = 0; i < l.n; i++) {
        snprintf(p, sizeof p, "rides/%s", l.names[i]);
        fs->remove(fs, p);
    }

    int recovered = 0;
    static uint32_t ids[64];
    int n = list_suffix(fs, ".fit.part", ids, 64);
    for (int i = 0; i < n; i++) {
        int32_t off = 0;
        int have = read_offset(fs, ids[i], &off);
        char part[64], work[64], fin[64];
        cc_ride_path(part, sizeof part, ids[i], ".fit.part");
        cc_ride_path(work, sizeof work, ids[i], ".fit.rec");
        cc_fit_summary_t s;
        uint32_t crc, size;
        /* Rides started without UTC have a small counter ID; anything below the provisional base was
         * never given real time. */
        int time_unknown = !have && ids[i] < CC_FIT_PROVISIONAL_BASE;
        int rc = cc_fit_recover(fs, part, work, NULL, have ? off : 0, &s, &crc, &size);
        uint32_t new_id = ids[i];
        if (rc == 0 && have) {
            new_id = free_id(fs, s.start_ts);
        }
        if (rc == 0) {
            cc_ride_path(fin, sizeof fin, new_id, ".fit");
            if (fs->rename(fs, work, fin) != 0)
                continue;
            complete(fs, ids[i], new_id, (uint8_t)(CC_RIDE_RECOVERED | (time_unknown ? CC_RIDE_TIME_UNKNOWN : 0)), &s,
                     crc, size, NULL);
            recovered++;
        } else if (rc == 1) {
            cc_ride_path(p, sizeof p, ids[i], ".time");
            fs->remove(fs, p);
        }
    }

    /* A .fit without .meta: power cut between the rename and the meta write */
    static uint32_t fits[CC_RIDES_HARD_MAX + 64];
    n = cc_rides_list(fs, fits, (int)(sizeof fits / sizeof fits[0]));
    for (int i = 0; i < n; i++) {
        cc_ride_meta_t m;
        if (cc_ride_meta_read(fs, fits[i], &m))
            continue;
        char fp[64];
        cc_ride_path(fp, sizeof fp, fits[i], ".fit");
        hal_file_t f = fs->open(fs, fp, HAL_FS_READ);
        if (f < 0)
            continue;
        int32_t len = fs->size(fs, f);
        uint8_t *buf = len > 0 ? malloc((size_t)len) : NULL; /* boot only, before the ride engine runs */
        if (buf && fs->read(fs, f, buf, (uint32_t)len) == len && cc_fit_check(buf, (uint32_t)len)) {
            cc_fit_walk_t wk;
            cc_fit_walk(buf, (uint32_t)len - 2, 0, &wk);
            cc_fit_t tmp;
            memset(&tmp, 0, sizeof tmp);
            tmp.session = wk.session;
            tmp.laps = (uint16_t)wk.laps;
            cc_fit_summary_t s;
            cc_fit_summary(&tmp, &s);
            memset(&m, 0, sizeof m);
            m.ride_id = fits[i];
            m.size = (uint32_t)len;
            m.crc32 = cc_crc32(buf, (uint32_t)len);
            m.flags = fits[i] < CC_FIT_PROVISIONAL_BASE ? CC_RIDE_TIME_UNKNOWN : 0;
            m.sum = s;
            cc_ride_meta_write(fs, &m);
        }
        free(buf);
        fs->close(fs, f);
    }
    return recovered;
}
