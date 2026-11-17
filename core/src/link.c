#include "cc700/link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/nav.h"
#include "cc700/provisional.h"
#include "cc700/store.h"
#include "cc700/util.h"
#include "cc700/version.h"
#include "cc700/workout.h"

/* ------------------------------------------------------------------ framing */
int cc_link_fragment(const uint8_t *msg, size_t len, size_t cap, void (*emit)(void *, const uint8_t *, size_t), void *ctx)
{
    uint8_t pkt[256];
    if (cap == 0 || cap > sizeof pkt - 1)
        cap = sizeof pkt - 1;
    size_t n = len ? (len + cap - 1) / cap : 1;
    for (size_t i = 0; i < n; i++) {
        size_t off = i * cap;
        size_t take = len - off < cap ? len - off : cap;
        pkt[0] = (uint8_t)((i == 0 ? 0x80 : 0) | (i == n - 1 ? 0x40 : 0) | (i & 0x3F));
        if (take)
            memcpy(pkt + 1, msg + off, take);
        emit(ctx, pkt, take + 1);
    }
    return (int)n;
}

void cc_reasm_init(cc_reasm_t *r) { memset(r, 0, sizeof *r); }

int cc_reasm_feed(cc_reasm_t *r, const uint8_t *pkt, size_t len, uint32_t now_ms)
{
    if (len < 1)
        return 0;
    uint8_t h = pkt[0];
    int start = (h & 0x80) != 0, end = (h & 0x40) != 0;
    uint8_t ctr = h & 0x3F;
    if (r->active && now_ms - r->last_ms > CC_LINK_GAP_MS)
        r->active = 0; /* 2 s of silence: drop the partial message */
    if (start) {
        r->active = 1; /* a START always begins a new message */
        r->len = 0;
        r->next_counter = 0;
    } else if (!r->active) {
        return 0; /* no START while idle */
    }
    if (ctr != r->next_counter || r->len + (len - 1) > CC_LINK_MSG_MAX) {
        r->active = 0;
        return 0;
    }
    memcpy(r->buf + r->len, pkt + 1, len - 1);
    r->len += len - 1;
    r->next_counter = (uint8_t)((r->next_counter + 1) & 0x3F);
    r->last_ms = now_ms;
    if (end) {
        r->active = 0;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ helpers */
static int kind_suffix_ok(const char *name, int kind)
{
    size_t n = strlen(name);
    return n == 20 && strcmp(name + 16, kind == 1 ? ".cw1" : ".cr1") == 0;
}

typedef struct { int kind; int n; } count_t;
static int count_cb(void *ctx, const hal_dirent_t *e)
{
    count_t *c = ctx;
    if (kind_suffix_ok(e->name, c->kind))
        c->n++;
    return 0;
}

int cc_objects_count(hal_fs_t *fs, int kind)
{
    count_t c = {kind, 0};
    fs->list(fs, "objects", count_cb, &c);
    return c.n;
}

static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static int parse_hex_id(const char *s, uint8_t id[8])
{
    for (int i = 0; i < 8; i++) {
        int a = hexval(s[2 * i]), b = hexval(s[2 * i + 1]);
        if (a < 0 || b < 0)
            return 0;
        id[i] = (uint8_t)(a << 4 | b);
    }
    return 1;
}

static int file_exists(hal_fs_t *fs, const char *p)
{
    hal_file_t f = fs->open(fs, p, HAL_FS_READ);
    if (f < 0)
        return 0;
    fs->close(fs, f);
    return 1;
}

static void close_put(cc_link_t *l, int remove_tmp)
{
    if (!l->put_open)
        return;
    l->fs->close(l->fs, l->put_f);
    if (remove_tmp) {
        char p[64];
        cc_object_path(p, sizeof p, l->put_id, 0);
        l->fs->remove(l->fs, p);
    }
    l->put_open = 0;
}

/* ------------------------------------------------------------------ init and transport */
void cc_link_init(cc_link_t *l, hal_fs_t *fs, const cc_link_host_t *host,
                  void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx, uint16_t att_mtu)
{
    memset(l, 0, sizeof *l);
    l->fs = fs;
    l->host = *host;
    l->tx = tx;
    l->tx_ctx = tx_ctx;
    cc_reasm_init(&l->rx);
    cc_link_set_mtu(l, att_mtu);
    l->ev_state = -1;
    l->ev_batt5 = -1;
}

void cc_link_set_mtu(cc_link_t *l, uint16_t att_mtu)
{
    l->data_cap = att_mtu > 4 ? (size_t)att_mtu - 4 : 19;
}

static void send_msg(cc_link_t *l, const uint8_t *msg, size_t len)
{
    if (l->tx && len)
        cc_link_fragment(msg, len, l->data_cap, l->tx, l->tx_ctx);
}

void cc_link_rx(cc_link_t *l, const uint8_t *pkt, size_t len, uint32_t now_ms)
{
    if (!cc_reasm_feed(&l->rx, pkt, len, now_ms))
        return;
    size_t n = cc_link_handle(l, l->rx.buf, l->rx.len, now_ms, l->out);
    send_msg(l, l->out, n);
}

void cc_link_tick(cc_link_t *l, uint32_t now_ms)
{
    if (l->put_open && now_ms - l->put_last_ms > CC_LINK_PUT_IDLE_MS)
        close_put(l, 1);
}

void cc_link_event_state(cc_link_t *l, int state, int batt)
{
    uint8_t m[4] = {CC_EV_STATE, 0, (uint8_t)state, (uint8_t)(batt < 0 ? 0 : batt)};
    l->ev_state = state;
    l->ev_batt5 = batt / 5;
    send_msg(l, m, 4);
}

void cc_link_event_ride_saved(cc_link_t *l, uint32_t ride_id)
{
    uint8_t m[6] = {CC_EV_RIDE_SAVED, 0};
    cc_put_u32(m + 2, ride_id);
    send_msg(l, m, 6);
}

void cc_link_poll_events(cc_link_t *l)
{
    int st = l->host.ride_state(l->host.ctx);
    int b = l->host.battery_pct(l->host.ctx);
    if (st != l->ev_state || (b >= 0 && b / 5 != l->ev_batt5))
        cc_link_event_state(l, st, b);
}

/* ------------------------------------------------------------------ requests */
typedef struct { uint8_t *o; size_t n; } resp_t;
static void r8(resp_t *r, uint32_t v) { r->o[r->n++] = (uint8_t)v; }
static void r16(resp_t *r, uint32_t v) { cc_put_u16(r->o + r->n, (uint16_t)v); r->n += 2; }
static void r32(resp_t *r, uint32_t v) { cc_put_u32(r->o + r->n, v); r->n += 4; }

typedef struct { resp_t *r; hal_fs_t *fs; int kind; int count_pos; int count; } olist_t;

static int olist_cb(void *ctx, const hal_dirent_t *e)
{
    olist_t *L = ctx;
    uint8_t id[8];
    if (!kind_suffix_ok(e->name, L->kind) || !parse_hex_id(e->name, id))
        return 0;
    char p[80];
    snprintf(p, sizeof p, "objects/%s", e->name);
    hal_file_t f = L->fs->open(L->fs, p, HAL_FS_READ);
    if (f < 0)
        return 0;
    uint8_t h[80];
    int32_t n = L->fs->read(L->fs, f, h, sizeof h);
    int32_t size = L->fs->size(L->fs, f);
    L->fs->close(L->fs, f);
    resp_t *r = L->r;
    if (L->kind == 1) {
        if (n < 18 || 18 + h[17] > n) return 0;
        if (r->n + 8 + 4 + 1 + h[17] + 5 > CC_LINK_MSG_MAX) return 1;
        memcpy(r->o + r->n, id, 8); r->n += 8;
        r32(r, (uint32_t)size);
        r8(r, h[17]);
        memcpy(r->o + r->n, h + 18, h[17]); r->n += h[17];
        r32(r, cc_get_u32(h + 12));
        r8(r, h[16]);
    } else {
        if (n < 28 || 28 + h[27] > n) return 0;
        if (r->n + 8 + 4 + 1 + h[27] + 7 > CC_LINK_MSG_MAX) return 1;
        memcpy(r->o + r->n, id, 8); r->n += 8;
        r32(r, (uint32_t)size);
        r8(r, h[27]);
        memcpy(r->o + r->n, h + 28, h[27]); r->n += h[27];
        r32(r, cc_get_u32(h + 12));
        r16(r, cc_get_u16(h + 16));
        r8(r, h[22]);
    }
    L->count++;
    return 0;
}

/* The transfer check buffer (128 KB) and the route it decodes into (204 KB) cannot live in the
   ESP32-S3's internal SRAM; see the same note in core.c. One calloc each on the first transfer puts
   them in PSRAM on the unit (ESP-IDF sends any allocation over 16 KB there) and changes nothing on
   the host. A failure to get them is reported as CC_ST_IO_ERROR: the object is not accepted, and
   nothing has been renamed into place, so the phone can send it again. */
static uint8_t *objbuf;
static cc_route_t *route_check;
static cc_workout_t *wo_check;

static int check_mem(void)
{
    if (!objbuf) objbuf = calloc(1, CC_RT_OBJ_MAX);
    if (!route_check) route_check = calloc(1, sizeof *route_check);
    if (!wo_check) wo_check = calloc(1, sizeof *wo_check);
    return objbuf && route_check && wo_check;
}

static int put_end(cc_link_t *l)
{
    hal_fs_t *fs = l->fs;
    if (!check_mem()) {
        close_put(l, 1);
        return CC_ST_IO_ERROR;
    }
    if (l->put_next != l->put_size) {
        close_put(l, 1);
        return CC_ST_BAD_ARG;
    }
    if (fs->fsync(fs, l->put_f) != 0) {
        close_put(l, 1);
        return CC_ST_IO_ERROR;
    }
    fs->seek(fs, l->put_f, 0);
    int32_t n = fs->read(fs, l->put_f, objbuf, l->put_size);
    if (n != (int32_t)l->put_size) {
        close_put(l, 1);
        return CC_ST_IO_ERROR;
    }
    if (cc_crc32(objbuf, (size_t)n) != l->put_crc) {
        close_put(l, 1);
        return CC_ST_BAD_CRC;
    }
    int ok = l->put_kind == 1 ? cc_cw1_decode(objbuf, (size_t)n, wo_check) == CC_CW1_OK
                              : cc_cr1_decode(objbuf, (size_t)n, route_check) == CC_CR1_OK;
    if (!ok || memcmp(objbuf + 4, l->put_id, 8) != 0) {
        close_put(l, 1);
        return CC_ST_BAD_OBJECT;
    }
    if (fs->close(fs, l->put_f) != 0) {
        l->put_open = 0;
        char t[64];
        cc_object_path(t, sizeof t, l->put_id, 0);
        fs->remove(fs, t);
        return CC_ST_IO_ERROR;
    }
    l->put_open = 0;
    char tmp[64], fin[64];
    cc_object_path(tmp, sizeof tmp, l->put_id, 0);
    cc_object_path(fin, sizeof fin, l->put_id, l->put_kind);
    if (fs->rename(fs, tmp, fin) != 0) {
        fs->remove(fs, tmp);
        return CC_ST_IO_ERROR;
    }
    return CC_ST_OK;
}

size_t cc_link_handle(cc_link_t *l, const uint8_t *m, size_t len, uint32_t now_ms, uint8_t *out)
{
    if (len < 2)
        return 0;
    uint8_t op = m[0], req = m[1];
    if (req == 0)
        return 0; /* reserved: a response with req_id 0 would read as an event (QUESTIONS E7) */
    const uint8_t *p = m + 2;
    size_t pl = len - 2;
    resp_t r = {out, 0};
    r8(&r, op | 0x80);
    r8(&r, req);
    hal_fs_t *fs = l->fs;
    int state = l->host.ride_state(l->host.ctx);

    if (op >= 0x80) {
        r8(&r, CC_ST_BAD_OPCODE);
        return r.n;
    }
    int busy_ok = op == CC_OP_HELLO || op == CC_OP_FTP_GET || op == CC_OP_TIME_SET || op == CC_OP_OBJ_LIST;
    int known = op == CC_OP_HELLO || op == CC_OP_FTP_GET || op == CC_OP_FTP_SET || op == CC_OP_TIME_SET ||
                (op >= CC_OP_OBJ_LIST && op <= CC_OP_OBJ_DELETE) || (op >= CC_OP_RIDE_LIST && op <= CC_OP_RIDE_DELETE);
    if (!known) {
        r8(&r, CC_ST_BAD_OPCODE);
        return r.n;
    }
    if (state != 0 && !busy_ok) {
        r8(&r, CC_ST_BUSY);
        return r.n;
    }
    if (l->put_open)
        l->put_last_ms = now_ms; /* any message keeps the session alive */

    switch (op) {
    case CC_OP_HELLO: {
        int b = l->host.battery_pct(l->host.ctx);
        int wf = CC_MAX_WORKOUTS - cc_objects_count(fs, 1), rf = CC_MAX_ROUTES - cc_objects_count(fs, 2);
        r8(&r, pl >= 1 && p[0] == CC_LINK_PROTO ? CC_ST_OK : CC_ST_BAD_ARG);
        r8(&r, CC_LINK_PROTO);
        r8(&r, CC_FW_MAJOR);
        r8(&r, CC_FW_MINOR);
        r8(&r, CC_FW_PATCH);
        r8(&r, (uint32_t)(b < 0 ? 0 : b));
        r8(&r, (uint32_t)state);
        r8(&r, (uint32_t)(wf < 0 ? 0 : wf));
        r8(&r, (uint32_t)(rf < 0 ? 0 : rf));
        return r.n;
    }
    case CC_OP_FTP_GET:
        r8(&r, CC_ST_OK);
        r16(&r, l->host.get_ftp(l->host.ctx));
        return r.n;
    case CC_OP_FTP_SET: {
        if (pl < 2) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        uint16_t ftp = cc_get_u16(p);
        if (ftp < CC_FTP_MIN_W || ftp > CC_FTP_MAX_W) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        int rc = l->host.set_ftp(l->host.ctx, ftp);
        r8(&r, rc == 0 ? CC_ST_OK : rc == -1 ? CC_ST_BUSY : CC_ST_IO_ERROR);
        return r.n;
    }
    case CC_OP_TIME_SET:
        if (pl < 6) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        l->host.set_time(l->host.ctx, cc_get_u32(p), cc_get_i16(p + 4));
        l->lib_changes++; /* History's dates are local: computed with this offset when the list is built */
        r8(&r, CC_ST_OK);
        return r.n;
    case CC_OP_OBJ_LIST: {
        if (pl < 1 || (p[0] != 1 && p[0] != 2)) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        r8(&r, CC_ST_OK);
        olist_t L = {&r, fs, p[0], (int)r.n, 0};
        r8(&r, 0);
        fs->list(fs, "objects", olist_cb, &L);
        out[L.count_pos] = (uint8_t)L.count;
        return r.n;
    }
    case CC_OP_OBJ_PUT_BEGIN: {
        if (pl < 17) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        uint8_t kind = p[0];
        uint32_t size = cc_get_u32(p + 9), crc = cc_get_u32(p + 13);
        if (kind != 1 && kind != 2) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        close_put(l, 1); /* aborts any earlier session */
        if (size > (kind == 1 ? (uint32_t)CC_WO_OBJ_MAX : (uint32_t)CC_RT_OBJ_MAX)) { r8(&r, CC_ST_TOO_BIG); return r.n; }
        if (size < 12) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        char fin[64], tmp[64];
        cc_object_path(fin, sizeof fin, p + 1, kind);
        if (file_exists(fs, fin)) { r8(&r, CC_ST_EXISTS); return r.n; }
        if (cc_objects_count(fs, kind) >= (kind == 1 ? CC_MAX_WORKOUTS : CC_MAX_ROUTES)) { r8(&r, CC_ST_FULL); return r.n; }
        if (fs->free_bytes(fs) < (int64_t)size + 4096) { r8(&r, CC_ST_FULL); return r.n; }
        fs->mkdir(fs, "objects");
        cc_object_path(tmp, sizeof tmp, p + 1, 0);
        fs->remove(fs, tmp);
        hal_file_t f = fs->open(fs, tmp, HAL_FS_READ | HAL_FS_WRITE | HAL_FS_CREATE | HAL_FS_TRUNC);
        if (f < 0) { r8(&r, CC_ST_IO_ERROR); return r.n; }
        l->put_open = 1;
        l->put_kind = kind;
        memcpy(l->put_id, p + 1, 8);
        l->put_size = size;
        l->put_crc = crc;
        l->put_next = 0;
        l->put_f = f;
        l->put_last_ms = now_ms;
        r8(&r, CC_ST_OK);
        return r.n;
    }
    case CC_OP_OBJ_PUT_DATA: {
        if (!l->put_open) { r8(&r, CC_ST_NO_SESSION); return r.n; }
        if (pl < 5 || pl - 4 > CC_LINK_CHUNK_MAX) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        uint32_t off = cc_get_u32(p);
        uint32_t n = (uint32_t)(pl - 4);
        if (off != l->put_next) { r8(&r, CC_ST_OFFSET); r32(&r, l->put_next); return r.n; }
        if (off + n > l->put_size) { r8(&r, CC_ST_BAD_ARG); r32(&r, l->put_next); return r.n; }
        if (fs->seek(fs, l->put_f, off) != 0 || fs->write(fs, l->put_f, p + 4, n) != (int32_t)n) {
            r8(&r, CC_ST_IO_ERROR);
            r32(&r, l->put_next);
            return r.n;
        }
        l->put_next += n;
        r8(&r, CC_ST_OK);
        r32(&r, l->put_next);
        return r.n;
    }
    case CC_OP_OBJ_PUT_END: {
        if (!l->put_open) { r8(&r, CC_ST_NO_SESSION); return r.n; }
        int rc = put_end(l);
        if (rc == CC_ST_OK)
            l->lib_changes++; /* the object is now in the library under its final name */
        r8(&r, (uint32_t)rc);
        return r.n;
    }
    case CC_OP_OBJ_DELETE: {
        if (pl < 9 || (p[0] != 1 && p[0] != 2)) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        char fin[64];
        cc_object_path(fin, sizeof fin, p + 1, p[0]);
        if (fs->remove(fs, fin) != 0) { r8(&r, CC_ST_NOT_FOUND); return r.n; }
        if (l->host.object_deleted)
            l->host.object_deleted(l->host.ctx, p[0], p + 1);
        l->lib_changes++;
        r8(&r, CC_ST_OK);
        return r.n;
    }
    case CC_OP_RIDE_LIST: {
        static uint32_t ids[CC_RIDES_HARD_MAX + 64];
        int n = cc_rides_list(fs, ids, (int)(sizeof ids / sizeof ids[0]));
        int first = n > CC_MAX_RIDES_LISTED ? n - CC_MAX_RIDES_LISTED : 0;
        r8(&r, CC_ST_OK);
        size_t cpos = r.n;
        r8(&r, 0);
        int count = 0;
        for (int i = first; i < n; i++) {
            cc_ride_meta_t mt;
            if (!cc_ride_meta_read(fs, ids[i], &mt))
                continue;
            r32(&r, mt.ride_id);
            r32(&r, mt.size);
            r8(&r, mt.flags);
            r32(&r, mt.sum.timer_s);
            r32(&r, mt.sum.distance_m);
            count++;
        }
        out[cpos] = (uint8_t)count;
        return r.n;
    }
    case CC_OP_RIDE_OPEN: {
        if (pl < 4) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        cc_ride_meta_t mt;
        char fp[64];
        cc_ride_path(fp, sizeof fp, cc_get_u32(p), ".fit");
        if (!file_exists(fs, fp) || !cc_ride_meta_read(fs, cc_get_u32(p), &mt)) { r8(&r, CC_ST_NOT_FOUND); return r.n; }
        r8(&r, CC_ST_OK);
        r32(&r, mt.size);
        r32(&r, mt.crc32);
        return r.n;
    }
    case CC_OP_RIDE_READ: {
        if (pl < 10) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        uint32_t off = cc_get_u32(p + 4);
        uint16_t n = cc_get_u16(p + 8);
        if (n < 1 || n > CC_LINK_CHUNK_MAX) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        char fp[64];
        cc_ride_path(fp, sizeof fp, cc_get_u32(p), ".fit");
        hal_file_t f = fs->open(fs, fp, HAL_FS_READ);
        if (f < 0) { r8(&r, CC_ST_NOT_FOUND); return r.n; }
        r8(&r, CC_ST_OK);
        int32_t size = fs->size(fs, f);
        int32_t got = 0;
        if (size >= 0 && off < (uint32_t)size && fs->seek(fs, f, off) == 0)
            got = fs->read(fs, f, out + r.n, n);
        fs->close(fs, f);
        if (got < 0) { out[2] = CC_ST_IO_ERROR; return 3; }
        r.n += (size_t)got;
        return r.n;
    }
    case CC_OP_RIDE_MARK: {
        if (pl < 5) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        int rc = cc_ride_mark(fs, cc_get_u32(p), p[4] & 1);
        if (rc == 0)
            l->lib_changes++; /* History shows UPLOADED or WAITING from this flag */
        r8(&r, rc == 0 ? CC_ST_OK : rc == -2 ? CC_ST_NOT_FOUND : CC_ST_IO_ERROR);
        return r.n;
    }
    case CC_OP_RIDE_DELETE: {
        if (pl < 4) { r8(&r, CC_ST_BAD_ARG); return r.n; }
        int rc = cc_ride_delete(fs, cc_get_u32(p));
        if (rc == 0)
            l->lib_changes++;
        r8(&r, rc == 0 ? CC_ST_OK : CC_ST_NOT_FOUND);
        return r.n;
    }
    default:
        r8(&r, CC_ST_BAD_OPCODE);
        return r.n;
    }
}
