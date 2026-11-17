#include "cc700/fit.h"

#include <stdlib.h>
#include <string.h>

#include "cc700/util.h"

/* ------------------------------------------------------------------ profile subset */
enum { G_FILE_ID = 0, G_SESSION = 18, G_LAP = 19, G_RECORD = 20, G_EVENT = 21, G_ACTIVITY = 34 };
enum { L_FILE_ID, L_EVENT, L_RECORD, L_LAP, L_SESSION, L_ACTIVITY, L_COUNT };
enum { BT_ENUM = 0x00, BT_U8 = 0x02, BT_U16 = 0x84, BT_S32 = 0x85, BT_U32 = 0x86, BT_U32Z = 0x8C };
enum { EV_TIMER = 0, EV_SESSION = 8, EV_LAP = 9, EV_ACTIVITY = 26 };
enum { ET_START = 0, ET_STOP = 1, ET_STOP_ALL = 4 };

typedef struct { uint8_t num, size, base; } fdef_t;

static const fdef_t D_FILE_ID[] = {{0, 1, BT_ENUM}, {1, 2, BT_U16}, {2, 2, BT_U16}, {3, 4, BT_U32Z}, {4, 4, BT_U32}};
static const fdef_t D_EVENT[] = {{253, 4, BT_U32}, {0, 1, BT_ENUM}, {1, 1, BT_ENUM}};
static const fdef_t D_RECORD[] = {{253, 4, BT_U32}, {0, 4, BT_S32}, {1, 4, BT_S32}, {2, 2, BT_U16}, {3, 1, BT_U8},
                                  {4, 1, BT_U8}, {5, 4, BT_U32}, {6, 2, BT_U16}, {7, 2, BT_U16}};
static const fdef_t D_LAP[] = {{253, 4, BT_U32}, {254, 2, BT_U16}, {0, 1, BT_ENUM}, {1, 1, BT_ENUM}, {2, 4, BT_U32},
                               {7, 4, BT_U32}, {8, 4, BT_U32}, {9, 4, BT_U32}, {13, 2, BT_U16}, {14, 2, BT_U16},
                               {15, 1, BT_U8}, {16, 1, BT_U8}, {17, 1, BT_U8}, {19, 2, BT_U16}, {20, 2, BT_U16},
                               {21, 2, BT_U16}, {24, 1, BT_ENUM}, {25, 1, BT_ENUM}};
static const fdef_t D_SESSION[] = {{253, 4, BT_U32}, {254, 2, BT_U16}, {0, 1, BT_ENUM}, {1, 1, BT_ENUM}, {2, 4, BT_U32},
                                   {5, 1, BT_ENUM}, {6, 1, BT_ENUM}, {7, 4, BT_U32}, {8, 4, BT_U32}, {9, 4, BT_U32},
                                   {14, 2, BT_U16}, {15, 2, BT_U16}, {16, 1, BT_U8}, {17, 1, BT_U8}, {18, 1, BT_U8},
                                   {20, 2, BT_U16}, {21, 2, BT_U16}, {22, 2, BT_U16}, {25, 2, BT_U16}, {26, 2, BT_U16},
                                   {28, 1, BT_ENUM}};
static const fdef_t D_ACTIVITY[] = {{253, 4, BT_U32}, {0, 4, BT_U32}, {1, 2, BT_U16}, {2, 1, BT_ENUM}, {3, 1, BT_ENUM},
                                    {4, 1, BT_ENUM}};

#define N(a) (sizeof(a) / sizeof((a)[0]))
static const struct { uint16_t global; const fdef_t *f; uint8_t n; } DEFS[L_COUNT] = {
    {G_FILE_ID, D_FILE_ID, N(D_FILE_ID)}, {G_EVENT, D_EVENT, N(D_EVENT)},       {G_RECORD, D_RECORD, N(D_RECORD)},
    {G_LAP, D_LAP, N(D_LAP)},             {G_SESSION, D_SESSION, N(D_SESSION)}, {G_ACTIVITY, D_ACTIVITY, N(D_ACTIVITY)},
};

/* ------------------------------------------------------------------ CRC-16 (FIT SDK) */
static uint16_t crc16_nibble(uint16_t crc, uint8_t byte)
{
    static const uint16_t t[16] = {0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
                                   0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400};
    uint16_t tmp = t[crc & 0xF];
    crc = (crc >> 4) & 0x0FFF;
    crc = crc ^ tmp ^ t[byte & 0xF];
    tmp = t[crc & 0xF];
    crc = (crc >> 4) & 0x0FFF;
    return crc ^ tmp ^ t[(byte >> 4) & 0xF];
}

/* The FIT SDK's nibble algorithm, run through a 256-entry table built from it (same result). */
uint16_t cc_fit_crc16(uint16_t crc, const uint8_t *b, size_t len)
{
    static uint16_t tab[256];
    static int ready;
    if (!ready) {
        for (int i = 0; i < 256; i++)
            tab[i] = crc16_nibble(0, (uint8_t)i);
        ready = 1;
    }
    while (len--)
        crc = (uint16_t)((crc >> 8) ^ tab[(crc ^ *b++) & 0xFF]);
    return crc;
}

/* ------------------------------------------------------------------ accumulators */
static void acc_start(cc_fit_acc_t *a, uint32_t ts, uint32_t dist_cm, int running)
{
    memset(a, 0, sizeof *a);
    a->start_ts = a->last_ts = ts;
    a->dist_start_cm = a->dist_cm = dist_cm;
    a->timer_running = running;
    a->timer_start_ts = ts;
}

static void acc_timer(cc_fit_acc_t *a, uint32_t ts, int start)
{
    if (start && !a->timer_running) {
        a->timer_running = 1;
        a->timer_start_ts = ts;
    } else if (!start && a->timer_running) {
        a->timer_ms += (ts - a->timer_start_ts) * 1000u;
        a->timer_running = 0;
    }
    if (ts > a->last_ts)
        a->last_ts = ts;
}

static uint32_t acc_timer_ms(const cc_fit_acc_t *a, uint32_t ts)
{
    return a->timer_ms + (a->timer_running && ts > a->timer_start_ts ? (ts - a->timer_start_ts) * 1000u : 0);
}

static void acc_record(cc_fit_acc_t *a, const cc_fit_record_t *r)
{
    a->records++;
    if (r->ts > a->last_ts)
        a->last_ts = r->ts;
    a->dist_cm = r->dist_cm;
    if (r->has_power) {
        a->power_sum += r->power; a->power_n++;
        if (r->power > a->power_max) a->power_max = r->power;
    }
    if (r->has_hr) {
        a->hr_sum += r->hr; a->hr_n++;
        if (r->hr > a->hr_max) a->hr_max = r->hr;
    }
    if (r->has_cad) {
        if (r->cad > 0) { a->cad_sum += r->cad; a->cad_n++; }
        if (r->cad > a->cad_max) a->cad_max = r->cad;
    }
    if (r->has_speed && r->speed_mms > a->speed_max_mms)
        a->speed_max_mms = r->speed_mms;
    if (r->has_alt) {
        if (r->alt_baro)
            a->ascent_claim = 1;
        if (!a->alt_have) {
            a->alt_have = 1;
            a->alt_ref_cm = r->alt_cm;
        } else if (r->alt_cm - a->alt_ref_cm >= 100) {
            a->ascent_cm += (uint32_t)(r->alt_cm - a->alt_ref_cm);
            a->alt_ref_cm = r->alt_cm;
        } else if (a->alt_ref_cm - r->alt_cm >= 100) {
            a->alt_ref_cm = r->alt_cm;
        }
    }
}

static uint32_t mean_u(uint64_t sum, uint32_t n) { return n ? (uint32_t)cc_div_half_up((int64_t)sum, n) : 0; }

/* ------------------------------------------------------------------ message serialising */
typedef struct { uint8_t b[96]; uint32_t n; } msg_t;
static void m8(msg_t *m, uint32_t v) { m->b[m->n++] = (uint8_t)v; }
static void m16(msg_t *m, uint32_t v) { cc_put_u16(m->b + m->n, (uint16_t)v); m->n += 2; }
static void m32(msg_t *m, uint32_t v) { cc_put_u32(m->b + m->n, v); m->n += 4; }
static uint32_t fts(uint32_t unix_ts) { return unix_ts - CC_FIT_EPOCH; }

static int buf_write_out(cc_fit_t *w)
{
    if (w->buf_len == 0)
        return 0;
    if (!w->open) {
        w->f = w->fs->open(w->fs, w->path, HAL_FS_READ | HAL_FS_WRITE | HAL_FS_CREATE);
        if (w->f < 0) { w->io_error = 1; return -1; }
        w->open = 1;
    }
    if (w->fs->seek(w->fs, w->f, w->file_pos) != 0 ||
        w->fs->write(w->fs, w->f, w->buf, w->buf_len) != (int32_t)w->buf_len) {
        w->io_error = 1;
        return -1;
    }
    w->file_pos += w->buf_len;
    w->buf_len = 0;
    w->io_error = 0;
    return 0;
}

static int emit(cc_fit_t *w, const uint8_t *b, uint32_t n, int is_record)
{
    uint32_t limit = is_record ? CC_FIT_BUF - CC_FIT_RESERVE : CC_FIT_BUF;
    if (w->buf_len + n > limit && !w->io_error)
        buf_write_out(w);
    if (w->buf_len + n > limit) {
        if (is_record)
            w->dropped_records++;
        return -1;
    }
    memcpy(w->buf + w->buf_len, b, n);
    w->buf_len += n;
    w->data_size += n;
    if (w->buf_len >= 4096 && !w->io_error)
        buf_write_out(w);
    return 0;
}

static void write_definitions(cc_fit_t *w)
{
    for (int l = 0; l < L_COUNT; l++) {
        uint8_t b[5 + 3 * 32];
        uint32_t n = 0;
        b[n++] = (uint8_t)(0x40 | l);
        b[n++] = 0;  /* reserved */
        b[n++] = 0;  /* little endian */
        cc_put_u16(b + n, DEFS[l].global); n += 2;
        b[n++] = DEFS[l].n;
        for (int i = 0; i < DEFS[l].n; i++) {
            b[n++] = DEFS[l].f[i].num;
            b[n++] = DEFS[l].f[i].size;
            b[n++] = DEFS[l].f[i].base;
        }
        emit(w, b, n, 0);
    }
}

static void header_bytes(uint8_t h[14], uint32_t data_size)
{
    h[0] = 14;
    h[1] = 0x10;            /* protocol 1.0 */
    cc_put_u16(h + 2, 2132); /* profile 21.32 */
    cc_put_u32(h + 4, data_size);
    memcpy(h + 8, ".FIT", 4);
    cc_put_u16(h + 12, cc_fit_crc16(0, h, 12));
}

int cc_fit_begin(cc_fit_t *w, hal_fs_t *fs, const char *path, uint32_t start_ts)
{
    memset(w, 0, sizeof *w);
    w->fs = fs;
    strncpy(w->path, path, sizeof w->path - 1);
    w->f = fs->open(fs, path, HAL_FS_READ | HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    w->open = w->f >= 0;
    uint8_t h[14];
    header_bytes(h, 0);
    memcpy(w->buf, h, 14);
    w->buf_len = 14;
    write_definitions(w);
    msg_t m = {.n = 0};
    m8(&m, L_FILE_ID);
    m8(&m, 4);          /* type: activity */
    m16(&m, 255);       /* manufacturer: development */
    m16(&m, 0);         /* product */
    m32(&m, 0x0000C700); /* serial number */
    m32(&m, fts(start_ts));
    emit(w, m.b, m.n, 0);
    acc_start(&w->session, start_ts, 0, 0);
    acc_start(&w->lap, start_ts, 0, 0);
    int rc = buf_write_out(w);
    if (rc == 0 && w->fs->fsync(w->fs, w->f) != 0) {
        w->io_error = 1;
        rc = -1;
    }
    return rc;
}

void cc_fit_event_timer(cc_fit_t *w, uint32_t ts, int start)
{
    msg_t m = {.n = 0};
    m8(&m, L_EVENT);
    m32(&m, fts(ts));
    m8(&m, EV_TIMER);
    m8(&m, start ? ET_START : ET_STOP_ALL);
    emit(w, m.b, m.n, 0);
    acc_timer(&w->session, ts, start);
    acc_timer(&w->lap, ts, start);
}

void cc_fit_record(cc_fit_t *w, const cc_fit_record_t *r)
{
    msg_t m = {.n = 0};
    m8(&m, L_RECORD);
    m32(&m, fts(r->ts));
    if (r->has_pos) {
        m32(&m, (uint32_t)(int32_t)cc_floordiv((int64_t)r->lat_e7 * 2147483648LL * 2 + 1800000000LL, 3600000000LL));
        m32(&m, (uint32_t)(int32_t)cc_floordiv((int64_t)r->lon_e7 * 2147483648LL * 2 + 1800000000LL, 3600000000LL));
    } else {
        m32(&m, 0x7FFFFFFF);
        m32(&m, 0x7FFFFFFF);
    }
    if (r->has_alt) {
        int64_t a = cc_floordiv(((int64_t)r->alt_cm + 50000) * 2 + 20, 40); /* (m + 500) * 5, half-up */
        m16(&m, a < 0 ? 0 : a > 0xFFFE ? 0xFFFE : (uint32_t)a);
    } else {
        m16(&m, 0xFFFF);
    }
    m8(&m, r->has_hr ? r->hr : 0xFF);
    m8(&m, r->has_cad ? r->cad : 0xFF);
    m32(&m, r->dist_cm);
    m16(&m, r->has_speed ? (r->speed_mms > 0xFFFE ? 0xFFFE : r->speed_mms) : 0xFFFF);
    m16(&m, r->has_power ? (r->power > 0xFFFE ? 0xFFFE : r->power) : 0xFFFF);
    if (emit(w, m.b, m.n, 1) != 0)
        return; /* dropped: totals must match the records in the file */
    acc_record(&w->session, r);
    acc_record(&w->lap, r);
}

static void lap_message(msg_t *m, const cc_fit_acc_t *a, uint16_t index, uint32_t ts, int trigger)
{
    uint32_t timer_ms = acc_timer_ms(a, ts);
    uint32_t dist = a->dist_cm - a->dist_start_cm;
    m8(m, L_LAP);
    m32(m, fts(ts));
    m16(m, index);
    m8(m, EV_LAP);
    m8(m, ET_STOP);
    m32(m, fts(a->start_ts));
    m32(m, (ts - a->start_ts) * 1000u);
    m32(m, timer_ms);
    m32(m, dist);
    m16(m, timer_ms ? (uint32_t)cc_div_half_up((int64_t)dist * 10000, timer_ms) : 0); /* mm/s */
    m16(m, a->speed_max_mms);
    m8(m, a->hr_n ? mean_u(a->hr_sum, a->hr_n) : 0xFF);
    m8(m, a->hr_n ? a->hr_max : 0xFF);
    m8(m, a->cad_n ? mean_u(a->cad_sum, a->cad_n) : 0xFF);
    m16(m, a->power_n ? mean_u(a->power_sum, a->power_n) : 0xFFFF);
    m16(m, a->power_n ? a->power_max : 0xFFFF);
    m16(m, a->ascent_claim ? (uint32_t)cc_div_half_up(a->ascent_cm, 100) : 0xFFFF);
    m8(m, (uint32_t)trigger);
    m8(m, 2); /* sport: cycling */
}

void cc_fit_lap(cc_fit_t *w, uint32_t ts, int trigger)
{
    msg_t m = {.n = 0};
    lap_message(&m, &w->lap, w->laps, ts, trigger);
    emit(w, m.b, m.n, 0);
    w->laps++;
    if (trigger == CC_FIT_LAP_SESSION_END)
        w->final_lap_written = 1;
    acc_start(&w->lap, ts, w->session.dist_cm, w->session.timer_running);
}

int cc_fit_flush(cc_fit_t *w)
{
    if (buf_write_out(w) != 0)
        return -1;
    if (!w->open || w->fs->fsync(w->fs, w->f) != 0) {
        w->io_error = 1;
        return -1;
    }
    return 0;
}

void cc_fit_summary(const cc_fit_t *w, cc_fit_summary_t *s)
{
    const cc_fit_acc_t *a = &w->session;
    memset(s, 0, sizeof *s);
    s->start_ts = a->start_ts;
    s->elapsed_s = a->last_ts - a->start_ts;
    s->timer_s = acc_timer_ms(a, a->last_ts) / 1000u;
    s->distance_m = a->dist_cm / 100u;
    s->has_power = a->power_n > 0;
    s->avg_power = (uint16_t)mean_u(a->power_sum, a->power_n);
    s->max_power = a->power_max;
    s->has_hr = a->hr_n > 0;
    s->avg_hr = (uint8_t)mean_u(a->hr_sum, a->hr_n);
    s->max_hr = a->hr_max;
    s->has_cad = a->cad_n > 0;
    s->avg_cad = (uint8_t)mean_u(a->cad_sum, a->cad_n);
    s->has_ascent = a->ascent_claim;
    s->ascent_m = a->ascent_claim ? (uint16_t)cc_div_half_up(a->ascent_cm, 100) : 0;
    s->avg_speed_mms = s->timer_s ? (uint32_t)cc_div_half_up((int64_t)a->dist_cm * 10, s->timer_s) : 0;
    s->records = a->records;
    s->laps = w->laps;
}

int cc_fit_close_messages(cc_fit_t *w, uint32_t end_ts, int32_t ascent_override_m)
{
    if (end_ts < w->session.last_ts)
        end_ts = w->session.last_ts;
    if (w->session.timer_running)
        cc_fit_event_timer(w, end_ts, 0);
    if (!w->final_lap_written)
        cc_fit_lap(w, end_ts, CC_FIT_LAP_SESSION_END);
    if (ascent_override_m >= 0) {
        w->session.ascent_cm = (uint32_t)ascent_override_m * 100u;
        w->session.alt_have = 1;
        w->session.ascent_claim = 1; /* the ride engine only has an ascent when it has a barometer */
    }
    w->session.last_ts = end_ts;
    const cc_fit_acc_t *a = &w->session;
    uint32_t timer_ms = acc_timer_ms(a, end_ts);
    msg_t m = {.n = 0};
    m8(&m, L_SESSION);
    m32(&m, fts(end_ts));
    m16(&m, 0);
    m8(&m, EV_SESSION);
    m8(&m, ET_STOP);
    m32(&m, fts(a->start_ts));
    m8(&m, 2); /* cycling */
    m8(&m, 0); /* generic */
    m32(&m, (end_ts - a->start_ts) * 1000u);
    m32(&m, timer_ms);
    m32(&m, a->dist_cm);
    m16(&m, timer_ms ? (uint32_t)cc_div_half_up((int64_t)a->dist_cm * 10000, timer_ms) : 0);
    m16(&m, a->speed_max_mms);
    m8(&m, a->hr_n ? mean_u(a->hr_sum, a->hr_n) : 0xFF);
    m8(&m, a->hr_n ? a->hr_max : 0xFF);
    m8(&m, a->cad_n ? mean_u(a->cad_sum, a->cad_n) : 0xFF);
    m16(&m, a->power_n ? mean_u(a->power_sum, a->power_n) : 0xFFFF);
    m16(&m, a->power_n ? a->power_max : 0xFFFF);
    m16(&m, a->ascent_claim ? (uint32_t)cc_div_half_up(a->ascent_cm, 100) : 0xFFFF);
    m16(&m, 0);        /* first_lap_index */
    m16(&m, w->laps);  /* num_laps */
    m8(&m, 0);         /* trigger: activity_end */
    emit(w, m.b, m.n, 0);
    m.n = 0;
    m8(&m, L_ACTIVITY);
    m32(&m, fts(end_ts));
    m32(&m, timer_ms);
    m16(&m, 1);
    m8(&m, 0);           /* manual */
    m8(&m, EV_ACTIVITY);
    m8(&m, ET_STOP);
    emit(w, m.b, m.n, 0);
    return cc_fit_flush(w);
}

/* ------------------------------------------------------------------ reading */
typedef int (*rd_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n); /* returns bytes read */

typedef struct { const uint8_t *b; uint32_t len; } memrd_t;
static int mem_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    memrd_t *m = ctx;
    if (off >= m->len)
        return 0;
    if (n > m->len - off)
        n = m->len - off;
    memcpy(dst, m->b + off, n);
    return (int)n;
}

typedef struct {
    hal_fs_t *fs;
    hal_file_t f;
    uint8_t cache[4096];
    uint32_t off, len;
    int valid;
} fsrd_t;
static int fs_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    fsrd_t *r = ctx;
    uint32_t done = 0;
    while (done < n) {
        uint32_t o = off + done;
        if (!r->valid || o < r->off || o >= r->off + r->len) {
            if (r->fs->seek(r->fs, r->f, o) != 0)
                return (int)done;
            int32_t got = r->fs->read(r->fs, r->f, r->cache, sizeof r->cache);
            if (got <= 0) {
                r->valid = 0;
                return (int)done;
            }
            r->off = o;
            r->len = (uint32_t)got;
            r->valid = 1;
        }
        uint32_t take = r->off + r->len - o;
        if (take > n - done)
            take = n - done;
        memcpy(dst + done, r->cache + (o - r->off), take);
        done += take;
    }
    return (int)done;
}

typedef struct {
    int defined;
    uint16_t global;
    uint8_t nf;
    uint8_t num[64], size[64];
    uint32_t total;
} ldef_t;

/* Patch callback for time shifting: offset of a 4-byte time field in the stream. */
typedef void (*patch_fn)(void *ctx, uint32_t off, uint32_t value);

static uint32_t fld(const uint8_t *d, const ldef_t *ld, uint8_t num, int *present)
{
    uint32_t o = 0;
    for (int i = 0; i < ld->nf; i++) {
        if (ld->num[i] == num) {
            uint32_t v = 0;
            if (ld->size[i] == 1) v = d[o];
            else if (ld->size[i] == 2) v = cc_get_u16(d + o);
            else if (ld->size[i] == 4) v = cc_get_u32(d + o);
            else break;
            *present = 1;
            return v;
        }
        o += ld->size[i];
    }
    *present = 0;
    return 0;
}

static int walk(rd_fn rd, void *ctx, uint32_t len, int stop_at_session, cc_fit_walk_t *out, patch_fn patch,
                void *pctx)
{
    static ldef_t defs[16];
    uint8_t h[14];
    memset(out, 0, sizeof *out);
    memset(defs, 0, sizeof defs);
    if (len < 14 || rd(ctx, 0, h, 14) != 14 || h[0] != 14 || memcmp(h + 8, ".FIT", 4) != 0)
        return 0;
    uint32_t off = 14;
    out->end = 14;
    int started = 0;
    while (off < len) {
        uint8_t hdr;
        if (rd(ctx, off, &hdr, 1) != 1)
            break;
        if (hdr & 0x80)
            break; /* compressed timestamp headers are never written */
        int local = hdr & 0x0F;
        if (hdr & 0x40) {
            if (hdr & 0x20)
                break; /* developer fields are never written */
            uint8_t d[5];
            if (off + 6 > len || rd(ctx, off + 1, d, 5) != 5)
                break;
            if (d[1] != 0)
                break; /* only little endian */
            uint8_t nf = d[4];
            if (nf > 64 || off + 6 + 3u * nf > len)
                break;
            uint8_t f[192];
            if (rd(ctx, off + 6, f, 3u * nf) != 3 * nf)
                break;
            ldef_t *ld = &defs[local];
            ld->defined = 1;
            ld->global = cc_get_u16(d + 2);
            ld->nf = nf;
            ld->total = 0;
            for (int i = 0; i < nf; i++) {
                ld->num[i] = f[3 * i];
                ld->size[i] = f[3 * i + 1];
                ld->total += f[3 * i + 1];
            }
            off += 6 + 3u * nf;
            out->end = off;
            continue;
        }
        ldef_t *ld = &defs[local];
        if (!ld->defined || ld->total > 255 || off + 1 + ld->total > len)
            break;
        uint8_t d[256];
        if (rd(ctx, off + 1, d, ld->total) != (int)ld->total)
            break;
        int p;
        if (ld->global == G_SESSION && stop_at_session) {
            out->session_seen = 1;
            break;
        }
        uint32_t ts = fld(d, ld, 253, &p);
        int has_ts = p && ts != 0xFFFFFFFFu;
        uint32_t uts = ts + CC_FIT_EPOCH;
        switch (ld->global) {
        case G_FILE_ID: {
            uint32_t tc = fld(d, ld, 4, &p);
            if (p && !started) {
                acc_start(&out->session, tc + CC_FIT_EPOCH, 0, 0);
                acc_start(&out->lap, tc + CC_FIT_EPOCH, 0, 0);
                out->first_ts = tc + CC_FIT_EPOCH;
                started = 1;
            }
            break;
        }
        case G_EVENT: {
            uint32_t ev = fld(d, ld, 0, &p);
            uint32_t et = fld(d, ld, 1, &p);
            if (ev == EV_TIMER && has_ts) {
                int start = et == ET_START;
                acc_timer(&out->session, uts, start);
                acc_timer(&out->lap, uts, start);
                out->timer_running = out->session.timer_running;
            }
            break;
        }
        case G_RECORD: {
            cc_fit_record_t r;
            memset(&r, 0, sizeof r);
            r.ts = uts;
            uint32_t v = fld(d, ld, 2, &p);
            if (p && v != 0xFFFF) { r.has_alt = 1; r.alt_cm = (int32_t)v * 20 - 50000; }
            v = fld(d, ld, 3, &p);
            if (p && v != 0xFF) { r.has_hr = 1; r.hr = (uint8_t)v; }
            v = fld(d, ld, 4, &p);
            if (p && v != 0xFF) { r.has_cad = 1; r.cad = (uint8_t)v; }
            v = fld(d, ld, 5, &p);
            if (p && v != 0xFFFFFFFFu) r.dist_cm = v;
            else r.dist_cm = out->session.dist_cm;
            v = fld(d, ld, 6, &p);
            if (p && v != 0xFFFF) { r.has_speed = 1; r.speed_mms = v; }
            v = fld(d, ld, 7, &p);
            if (p && v != 0xFFFF) { r.has_power = 1; r.power = (uint16_t)v; }
            acc_record(&out->session, &r);
            acc_record(&out->lap, &r);
            out->records++;
            break;
        }
        case G_LAP: {
            uint32_t trig = fld(d, ld, 24, &p);
            out->laps++;
            out->final_lap = p && trig == CC_FIT_LAP_SESSION_END;
            if (has_ts)
                acc_start(&out->lap, uts, out->session.dist_cm, out->session.timer_running);
            break;
        }
        default:
            break;
        }
        if (has_ts && uts > out->last_ts)
            out->last_ts = uts;
        if (patch) {
            uint32_t o = 0;
            for (int i = 0; i < ld->nf; i++) {
                int is_time = ld->size[i] == 4 &&
                              (ld->num[i] == 253 || ((ld->global == G_LAP || ld->global == G_SESSION) && ld->num[i] == 2) ||
                               (ld->global == G_FILE_ID && ld->num[i] == 4));
                if (is_time)
                    patch(pctx, off + 1 + o, cc_get_u32(d + o));
                o += ld->size[i];
            }
        }
        off += 1 + ld->total;
        out->end = off;
    }
    return 1;
}

int cc_fit_walk(const uint8_t *b, uint32_t len, int stop_at_session, cc_fit_walk_t *out)
{
    memrd_t m = {b, len};
    if (!walk(mem_rd, &m, len, stop_at_session, out, NULL, NULL))
        return 0;
    return (int)out->end;
}

int cc_fit_check(const uint8_t *b, uint32_t len)
{
    if (len < 16 || b[0] != 14 || memcmp(b + 8, ".FIT", 4) != 0)
        return 0;
    if (cc_get_u16(b + 12) != cc_fit_crc16(0, b, 12))
        return 0;
    uint32_t ds = cc_get_u32(b + 4);
    if ((uint64_t)14 + ds + 2 != len)
        return 0;
    return cc_get_u16(b + 14 + ds) == cc_fit_crc16(0, b, 14 + ds);
}

typedef struct { uint8_t *b; int32_t offset; uint32_t n; } mempatch_t;
static void mem_patch(void *ctx, uint32_t off, uint32_t v)
{
    mempatch_t *p = ctx;
    if (v == 0xFFFFFFFFu)
        return;
    cc_put_u32(p->b + off, v + (uint32_t)p->offset);
    p->n++;
}

uint32_t cc_fit_shift_times(uint8_t *b, uint32_t end, int32_t offset)
{
    memrd_t m = {b, end};
    mempatch_t p = {b, offset, 0};
    cc_fit_walk_t w;
    walk(mem_rd, &m, end, 0, &w, mem_patch, &p);
    return p.n;
}

/* ------------------------------------------------------------------ finalise */
typedef struct { hal_fs_t *fs; hal_file_t f; int32_t offset; int err; fsrd_t *rd; } fspatch_t;
static void fs_patch(void *ctx, uint32_t off, uint32_t v)
{
    fspatch_t *p = ctx;
    if (v == 0xFFFFFFFFu)
        return;
    uint8_t b[4];
    cc_put_u32(b, v + (uint32_t)p->offset);
    if (p->fs->seek(p->fs, p->f, off) != 0 || p->fs->write(p->fs, p->f, b, 4) != 4)
        p->err = 1;
    p->rd->valid = 0; /* the cache may hold the old bytes */
}

int cc_fit_finalise(cc_fit_t *w, int32_t time_offset, const char *final_path, cc_fit_summary_t *sum,
                    uint32_t *file_crc32, uint32_t *file_size)
{
    hal_fs_t *fs = w->fs;
    if (cc_fit_flush(w) != 0)
        return -1;
    uint32_t end = 14 + w->data_size;
    static fsrd_t rd;
    memset(&rd, 0, sizeof rd);
    rd.fs = fs;
    rd.f = w->f;
    if (time_offset != 0) {
        fspatch_t p = {fs, w->f, time_offset, 0, &rd};
        cc_fit_walk_t tmp;
        walk(fs_rd, &rd, end, 0, &tmp, fs_patch, &p);
        if (p.err)
            return -1;
        w->session.start_ts += (uint32_t)time_offset;
        w->session.last_ts += (uint32_t)time_offset;
    }
    uint8_t h[14];
    header_bytes(h, w->data_size);
    if (fs->seek(fs, w->f, 0) != 0 || fs->write(fs, w->f, h, 14) != 14)
        return -1;
    rd.valid = 0;
    uint16_t crc = 0;
    uint32_t crc32 = 0;
    uint8_t chunk[4096];
    for (uint32_t o = 0; o < end;) {
        uint32_t n = end - o < sizeof chunk ? end - o : (uint32_t)sizeof chunk;
        if (fs_rd(&rd, o, chunk, n) != (int)n)
            return -1;
        crc = cc_fit_crc16(crc, chunk, n);
        crc32 = cc_crc32_update(crc32, chunk, n);
        o += n;
    }
    uint8_t c[2];
    cc_put_u16(c, crc);
    crc32 = cc_crc32_update(crc32, c, 2);
    if (fs->seek(fs, w->f, end) != 0 || fs->write(fs, w->f, c, 2) != 2 || fs->fsync(fs, w->f) != 0)
        return -1;
    if (fs->close(fs, w->f) != 0)
        return -1;
    w->open = 0;
    if (final_path && strcmp(w->path, final_path) != 0 && fs->rename(fs, w->path, final_path) != 0)
        return -1;
    if (sum)
        cc_fit_summary(w, sum);
    if (file_crc32)
        *file_crc32 = crc32;
    if (file_size)
        *file_size = end + 2;
    return 0;
}

/* ------------------------------------------------------------------ recovery */
/* Recovery's working set: the rebuilt writer (about 24 KB, nearly all of it the record ring), a read
   cache, the walk result and a copy buffer. It used to be function-local static storage, which on the
   unit kept about 29 KB of internal SRAM in .bss for the whole ride although recovery only runs at
   boot. Now it is one heap block for the duration of the call. On the ESP32-S3 a block this size is
   above CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL, so ESP-IDF puts it in PSRAM, and falls back to internal
   RAM if there is no PSRAM (Q30); at boot either has room. If the allocation fails anyway, recovery
   returns -1, which leaves the .part file in place to be tried again on the next boot -- the same path
   as a storage error. cc_fit_finalise's own read cache stays static on purpose: it runs at the end of
   every ride, when a failed allocation would cost the ride. */
typedef struct {
    fsrd_t rd;
    cc_fit_t w;
    cc_fit_walk_t wk;
    uint8_t chunk[4096];
} recover_mem_t;

static int recover(recover_mem_t *m, hal_fs_t *fs, const char *part_path, const char *work_path,
                   const char *final_path, int32_t time_offset, cc_fit_summary_t *sum, uint32_t *file_crc32,
                   uint32_t *file_size)
{
    fsrd_t *rd = &m->rd;
    cc_fit_t *w = &m->w;
    cc_fit_walk_t *wk = &m->wk;
    memset(rd, 0, sizeof *rd);
    rd->fs = fs;
    rd->f = fs->open(fs, part_path, HAL_FS_READ);
    if (rd->f < 0)
        return -1;
    int32_t len = fs->size(fs, rd->f);
    if (len < 0) {
        fs->close(fs, rd->f);
        return -1;
    }
    int ok = walk(fs_rd, rd, (uint32_t)len, 1, wk, NULL, NULL);
    if (!ok || wk->records == 0) {
        fs->close(fs, rd->f);
        return fs->remove(fs, part_path) == 0 ? 1 : -1;
    }
    /* Copy the complete messages into the work file */
    fs->remove(fs, work_path); /* leftover from an interrupted recovery */
    hal_file_t out = fs->open(fs, work_path, HAL_FS_READ | HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
    if (out < 0) {
        fs->close(fs, rd->f);
        return -1;
    }
    uint8_t *chunk = m->chunk;
    const uint32_t chunk_size = (uint32_t)sizeof m->chunk;
    int err = 0;
    for (uint32_t o = 0; o < wk->end && !err;) {
        uint32_t n = wk->end - o < chunk_size ? wk->end - o : chunk_size;
        if (fs_rd(rd, o, chunk, n) != (int)n || fs->write(fs, out, chunk, n) != (int32_t)n)
            err = 1;
        o += n;
    }
    fs->close(fs, rd->f);
    if (err) {
        fs->close(fs, out);
        return -1;
    }
    memset(w, 0, sizeof *w);
    w->fs = fs;
    strncpy(w->path, work_path, sizeof w->path - 1);
    w->f = out;
    w->open = 1;
    w->file_pos = wk->end;
    w->data_size = wk->end - 14;
    w->session = wk->session;
    w->lap = wk->lap;
    w->laps = (uint16_t)wk->laps;
    w->final_lap_written = wk->final_lap;
    uint32_t end_ts = wk->last_ts ? wk->last_ts : wk->first_ts;
    if (wk->session.timer_running && wk->last_ts)
        end_ts += 1; /* the last record covers its whole second */
    if (cc_fit_close_messages(w, end_ts, -1) != 0)
        return -1;
    if (cc_fit_finalise(w, time_offset, final_path, sum, file_crc32, file_size) != 0)
        return -1;
    return fs->remove(fs, part_path) == 0 ? 0 : -1;
}

int cc_fit_recover(hal_fs_t *fs, const char *part_path, const char *work_path, const char *final_path,
                   int32_t time_offset, cc_fit_summary_t *sum, uint32_t *file_crc32, uint32_t *file_size)
{
    recover_mem_t *m = calloc(1, sizeof *m);
    if (!m)
        return -1;
    int rc = recover(m, fs, part_path, work_path, final_path, time_offset, sum, file_crc32, file_size);
    free(m);
    return rc;
}
