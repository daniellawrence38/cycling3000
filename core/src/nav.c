#include "cc700/nav.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"
#include "cc700/workout.h" /* cc_object_id */

/* ------------------------------------------------------------------ CR1 */
static int printable(const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] > 0x7E)
            return 0;
    return 1;
}

static int coord_ok(int64_t lat, int64_t lon)
{
    return lat >= -900000000LL && lat <= 900000000LL && lon >= -1800000000LL && lon <= 1800000000LL;
}

cc_cr1_err_t cc_cr1_decode(const uint8_t *b, size_t len, cc_route_t *r)
{
    if (len > CC_RT_MAX_BYTES)
        return CC_CR1_TOO_BIG;
    if (len < 28)
        return CC_CR1_TRUNCATED;
    if (b[0] != 'C' || b[1] != 'R')
        return CC_CR1_MAGIC;
    if (b[2] != 1)
        return CC_CR1_VERSION;
    if (b[3] != 0)
        return CC_CR1_FLAGS;
    memcpy(r->id, b + 4, 8);
    r->dist_m = cc_get_u32(b + 12);
    r->ascent_m = cc_get_u16(b + 16);
    r->npts = cc_get_u16(b + 18);
    r->nturns = cc_get_u16(b + 20);
    r->nclimbs = b[22];
    r->nprof = cc_get_u16(b + 23);
    r->prof_step = cc_get_u16(b + 25);
    size_t nl = b[27];
    if (r->npts < 2 || r->npts > CC_RT_MAX_PTS || r->nturns > CC_RT_MAX_TURNS || r->nclimbs > CC_RT_MAX_CLIMBS ||
        r->nprof > CC_RT_MAX_PROF || (r->nprof > 0 && r->prof_step == 0) || (r->nprof == 0 && r->prof_step != 0))
        return CC_CR1_COUNTS;
    if (nl < 1 || nl > CC_RT_MAX_NAME)
        return CC_CR1_NAME;
    size_t off = 28;
    if (off + nl > len)
        return CC_CR1_TRUNCATED;
    if (!printable(b + off, nl))
        return CC_CR1_NAME;
    memcpy(r->name, b + off, nl);
    r->name[nl] = 0;
    off += nl;
    if (off + 8 > len)
        return CC_CR1_TRUNCATED;
    int64_t lat = cc_get_i32(b + off), lon = cc_get_i32(b + off + 4);
    off += 8;
    if (!coord_ok(lat, lon))
        return CC_CR1_COORD;
    r->lat[0] = (int32_t)lat;
    r->lon[0] = (int32_t)lon;
    for (int i = 1; i < r->npts; i++) {
        uint64_t u;
        size_t k = cc_varint_get(b + off, len - off, &u);
        if (!k)
            return off >= len ? CC_CR1_TRUNCATED : CC_CR1_VARINT;
        off += k;
        if (u > 8000000000ULL)
            return CC_CR1_COORD;
        lat += cc_unzigzag(u);
        k = cc_varint_get(b + off, len - off, &u);
        if (!k)
            return off >= len ? CC_CR1_TRUNCATED : CC_CR1_VARINT;
        off += k;
        if (u > 8000000000ULL)
            return CC_CR1_COORD;
        lon += cc_unzigzag(u);
        if (!coord_ok(lat, lon))
            return CC_CR1_COORD;
        r->lat[i] = (int32_t)lat;
        r->lon[i] = (int32_t)lon;
    }
    if (off + 2u * r->nprof > len)
        return CC_CR1_TRUNCATED;
    for (int i = 0; i < r->nprof; i++)
        r->prof[i] = cc_get_i16(b + off + 2 * i);
    off += 2u * r->nprof;
    for (int i = 0; i < r->nturns; i++) {
        if (off + 7 > len)
            return CC_CR1_TRUNCATED;
        cc_turn_t *t = &r->turns[i];
        t->dist_m = cc_get_u32(b + off);
        t->type = b[off + 4];
        t->exit = b[off + 5];
        size_t sl = b[off + 6];
        off += 7;
        if (t->type > CC_TURN_DEPART)
            return CC_CR1_TURN;
        if (off + sl > len)
            return CC_CR1_TRUNCATED;
        if (!printable(b + off, sl))
            return CC_CR1_TURN;
        size_t keep = sl > CC_RT_MAX_STREET ? CC_RT_MAX_STREET : sl;
        memcpy(t->street, b + off, keep);
        t->street[keep] = 0;
        off += sl;
    }
    for (int i = 0; i < r->nclimbs; i++) {
        if (off + 12 > len)
            return CC_CR1_TRUNCATED;
        cc_climb_t *c = &r->climbs[i];
        c->start_m = cc_get_u32(b + off);
        c->length_m = cc_get_u16(b + off + 4);
        c->ascent_m = cc_get_u16(b + off + 6);
        c->avg_x10 = cc_get_i16(b + off + 8);
        c->max_x10 = cc_get_i16(b + off + 10);
        off += 12;
    }
    if (off != len)
        return CC_CR1_TRAILING;
    uint8_t id[8];
    cc_object_id(b, len, id);
    if (memcmp(id, r->id, 8) != 0)
        return CC_CR1_ID;

    /* Projection about the midpoint latitude, cumulative distance */
    int32_t mn = r->lat[0], mx = r->lat[0];
    for (int i = 1; i < r->npts; i++) {
        if (r->lat[i] < mn) mn = r->lat[i];
        if (r->lat[i] > mx) mx = r->lat[i];
    }
    r->lat0 = (int32_t)(((int64_t)mn + mx) / 2);
    r->lon0 = r->lon[0];
    r->kx = (float)(1e-7 * 111320.0 * cos((double)r->lat0 * 1e-7 * 3.14159265358979323846 / 180.0));
    r->cum[0] = 0;
    for (int i = 0; i < r->npts; i++) {
        cc_route_project(r, r->lat[i], r->lon[i], &r->x[i], &r->y[i]);
        if (i > 0)
            r->cum[i] = r->cum[i - 1] + hypotf(r->x[i] - r->x[i - 1], r->y[i] - r->y[i - 1]);
    }
    return CC_CR1_OK;
}

void cc_route_project(const cc_route_t *r, int32_t lat_e7, int32_t lon_e7, float *x, float *y)
{
    *x = (float)(lon_e7 - r->lon0) * r->kx;
    *y = (float)(lat_e7 - r->lat0) * (float)(1e-7 * 111320.0);
}

int cc_route_profile_at(const cc_route_t *r, float d, float *e)
{
    if (r->nprof == 0)
        return 0;
    float fi = d / (float)r->prof_step;
    if (fi <= 0) { *e = r->prof[0] / 10.0f; return 1; }
    int i = (int)fi;
    if (i >= r->nprof - 1) { *e = r->prof[r->nprof - 1] / 10.0f; return 1; }
    float f = fi - (float)i;
    *e = (r->prof[i] + f * (float)(r->prof[i + 1] - r->prof[i])) / 10.0f;
    return 1;
}

size_t cc_cr1_encode(const char *name, const int32_t *lat, const int32_t *lon, uint16_t npts, uint32_t dist_m,
                     uint16_t ascent_m, const int16_t *prof, uint16_t nprof, uint16_t prof_step, const cc_turn_t *turns,
                     uint16_t nturns, const cc_climb_t *climbs, uint8_t nclimbs, uint8_t *b, size_t cap)
{
    size_t nl = strlen(name);
    size_t off = 28 + nl + 8;
    if (off > cap)
        return 0;
    b[0] = 'C'; b[1] = 'R'; b[2] = 1; b[3] = 0;
    memset(b + 4, 0, 8);
    cc_put_u32(b + 12, dist_m);
    cc_put_u16(b + 16, ascent_m);
    cc_put_u16(b + 18, npts);
    cc_put_u16(b + 20, nturns);
    b[22] = nclimbs;
    cc_put_u16(b + 23, nprof);
    cc_put_u16(b + 25, prof_step);
    b[27] = (uint8_t)nl;
    memcpy(b + 28, name, nl);
    cc_put_u32(b + 28 + nl, (uint32_t)lat[0]);
    cc_put_u32(b + 32 + nl, (uint32_t)lon[0]);
    for (int i = 1; i < npts; i++) {
        if (off + 20 > cap) return 0;
        off += cc_varint_put(b + off, cc_zigzag((int64_t)lat[i] - lat[i - 1]));
        off += cc_varint_put(b + off, cc_zigzag((int64_t)lon[i] - lon[i - 1]));
    }
    for (int i = 0; i < nprof; i++) {
        if (off + 2 > cap) return 0;
        cc_put_u16(b + off, (uint16_t)prof[i]);
        off += 2;
    }
    for (int i = 0; i < nturns; i++) {
        size_t sl = strlen(turns[i].street);
        if (off + 7 + sl > cap) return 0;
        cc_put_u32(b + off, turns[i].dist_m);
        b[off + 4] = turns[i].type;
        b[off + 5] = turns[i].exit;
        b[off + 6] = (uint8_t)sl;
        memcpy(b + off + 7, turns[i].street, sl);
        off += 7 + sl;
    }
    for (int i = 0; i < nclimbs; i++) {
        if (off + 12 > cap) return 0;
        cc_put_u32(b + off, climbs[i].start_m);
        cc_put_u16(b + off + 4, climbs[i].length_m);
        cc_put_u16(b + off + 6, climbs[i].ascent_m);
        cc_put_u16(b + off + 8, (uint16_t)climbs[i].avg_x10);
        cc_put_u16(b + off + 10, (uint16_t)climbs[i].max_x10);
        off += 12;
    }
    cc_object_id(b, off, b + 4);
    return off;
}

/* ------------------------------------------------------------------ text */
const char *cc_turn_text(uint8_t t)
{
    static const char *s[] = {"Continue", "Slight left", "Turn left", "Sharp left", "Slight right", "Turn right",
                              "Sharp right", "U-turn", "Roundabout", "Arrive", "Depart"};
    return t <= CC_TURN_DEPART ? s[t] : "Continue";
}

void cc_fmt_turn_dist(char *out, int cap, uint32_t m)
{
    if (m >= 1000) {
        uint32_t t = (uint32_t)cc_div_half_up(m, 100);
        snprintf(out, (size_t)cap, "%u.%u km", t / 10, t % 10);
    } else if (m >= 100) {
        snprintf(out, (size_t)cap, "%u m", (uint32_t)cc_div_half_up(m, 10) * 10);
    } else {
        snprintf(out, (size_t)cap, "%u m", (uint32_t)cc_div_half_up(m, 5) * 5);
    }
}

/* ------------------------------------------------------------------ follower */
void cc_nav_init(cc_nav_t *n)
{
    memset(n, 0, sizeof *n);
    n->climb_idx = -1;
    n->ahead_alerted = -1;
}

void cc_nav_load(cc_nav_t *n, const cc_route_t *r)
{
    cc_nav_init(n);
    n->r = r;
    n->loaded = 1;
}

void cc_nav_unload(cc_nav_t *n) { cc_nav_init(n); }

/* Nearest point on segment i to (x, y): returns squared distance, sets along-route position. */
static float seg_dist2(const cc_route_t *r, int i, float x, float y, float *pos)
{
    float ax = r->x[i], ay = r->y[i], bx = r->x[i + 1], by = r->y[i + 1];
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    float px = ax + t * dx - x, py = ay + t * dy - y;
    *pos = r->cum[i] + t * (r->cum[i + 1] - r->cum[i]);
    return px * px + py * py;
}

static int nearest(const cc_route_t *r, int from, int to, float x, float y, float *best_d2, float *best_pos)
{
    int best = -1;
    *best_d2 = 1e30f;
    for (int i = from; i < to && i < r->npts - 1; i++) {
        float pos;
        float d2 = seg_dist2(r, i, x, y, &pos);
        if (d2 < *best_d2) {
            *best_d2 = d2;
            *best_pos = pos;
            best = i;
        }
    }
    return best;
}

static void climb_update(cc_nav_t *n, float baro_ascent, unsigned *ev)
{
    const cc_route_t *r = n->r;
    int idx = -1;
    for (int i = 0; i < r->nclimbs; i++)
        if ((float)(r->climbs[i].start_m + r->climbs[i].length_m) > n->route_pos_m) {
            idx = i;
            break;
        }
    if (idx != n->climb_idx) {
        n->climb_idx = idx;
        n->climb_entered = 0;
    }
    n->on_climb = n->approaching = 0;
    if (idx < 0 || n->off_route)
        return;
    const cc_climb_t *c = &r->climbs[idx];
    float start = (float)c->start_m;
    if (n->route_pos_m < start) {
        n->approaching = start - n->route_pos_m <= CC_CLIMB_AHEAD_M;
        if (n->approaching && n->ahead_alerted != idx) {
            n->ahead_alerted = idx;
            *ev |= CC_NAV_EV_CLIMB_AHEAD;
        }
    } else {
        n->on_climb = 1;
        if (!n->climb_entered) {
            n->climb_entered = 1;
            n->climb_base_ascent = baro_ascent >= 0 ? baro_ascent : 0;
            n->climb_gain_at_entry = 0;
            float e0, e1;
            if (n->route_pos_m - start > 30.0f && cc_route_profile_at(r, start, &e0) &&
                cc_route_profile_at(r, n->route_pos_m, &e1))
                n->climb_gain_at_entry = e1 - e0 > 0 ? e1 - e0 : 0; /* joined mid-climb */
        }
    }
}

unsigned cc_nav_second(cc_nav_t *n, int fix_ok, int32_t lat_e7, int32_t lon_e7, uint32_t speed_mms, float baro_ascent)
{
    unsigned ev = 0;
    if (!n->loaded)
        return 0;
    const cc_route_t *r = n->r;

    /* speed history for ETA (one sample per second with a usable fix) */
    if (fix_ok) {
        uint16_t s = speed_mms > 65535 ? 65535 : (uint16_t)speed_mms;
        if (n->spd_n == 600) {
            uint16_t old = n->spd[n->spd_head];
            n->spd_sum -= old;
            if (old >= 500) n->spd_moving--;
        } else {
            n->spd_n++;
        }
        n->spd[n->spd_head] = s;
        n->spd_head = (n->spd_head + 1) % 600;
        n->spd_sum += s;
        if (s >= 500) n->spd_moving++;
    }
    if (!fix_ok)
        return 0;

    float x, y, d2 = 1e30f, pos = 0;
    cc_route_project(r, lat_e7, lon_e7, &x, &y);
    int seg = -1;
    if (n->have_pos && n->no_accept_s < 8) {
        /* window: back at most 100 m, forward until 500 m past the last position */
        int from = n->pos_idx - 5 < 0 ? 0 : n->pos_idx - 5;
        while (from > 0 && r->cum[from] > n->route_pos_m - 100.0f)
            from--;
        while (from < n->pos_idx && r->cum[from + 1] < n->route_pos_m - 100.0f)
            from++;
        int to = from;
        while (to < r->npts - 1 && r->cum[to] <= n->route_pos_m + 500.0f)
            to++;
        seg = nearest(r, from, to, x, y, &d2, &pos);
    }
    if (seg < 0 || d2 > (float)CC_OFFROUTE_OUT_M * CC_OFFROUTE_OUT_M) {
        if (!n->have_pos || n->no_accept_s >= 8 || n->off_route) {
            float gd2, gpos;
            int g = nearest(r, 0, r->npts - 1, x, y, &gd2, &gpos);
            if (g >= 0 && (seg < 0 || gd2 < d2)) {
                seg = g;
                d2 = gd2;
                pos = gpos;
            }
        }
    }
    float dist = sqrtf(d2);
    n->dist_to_route_m = dist;
    if (seg >= 0 && dist <= (float)CC_OFFROUTE_OUT_M) {
        n->have_pos = 1;
        n->pos_idx = seg;
        n->route_pos_m = pos;
        n->no_accept_s = 0;
    } else {
        n->no_accept_s++;
    }

    /* off route: 8 s beyond 40 m at 3 km/h or more; back after 3 s within 25 m */
    if (!n->off_route) {
        if (dist > (float)CC_OFFROUTE_OUT_M && speed_mms * 36u >= CC_OFFROUTE_MIN_KMH * 10000u)
            n->out_s++;
        else if (dist <= (float)CC_OFFROUTE_OUT_M)
            n->out_s = 0;
        if (n->out_s >= CC_OFFROUTE_OUT_SECS) {
            n->off_route = 1;
            n->back_s = 0;
            ev |= CC_NAV_EV_OFF_START;
        }
    } else {
        if (dist <= (float)CC_OFFROUTE_BACK_M)
            n->back_s++;
        else
            n->back_s = 0;
        if (n->back_s >= CC_OFFROUTE_BACK_SECS) {
            n->off_route = 0;
            n->out_s = 0;
            ev |= CC_NAV_EV_OFF_END;
        }
    }
    if (n->have_pos)
        climb_update(n, baro_ascent, &ev);
    return ev;
}

void cc_nav_view(const cc_nav_t *n, float baro_ascent, cc_nav_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->turn_idx = -1;
    v->climb_idx = -1;
    v->loaded = n->loaded;
    if (!n->loaded)
        return;
    const cc_route_t *r = n->r;
    v->have_pos = n->have_pos;
    v->off_route = n->off_route;
    v->route_pos_m = n->route_pos_m;
    v->dist_to_route_m = n->dist_to_route_m;
    float pos = n->have_pos ? n->route_pos_m : 0;
    float left = (float)r->dist_m - pos;
    v->dist_left_m = left > 0 ? (uint32_t)lroundf(left) : 0;
    for (int i = 0; i < r->nturns; i++)
        if ((float)r->turns[i].dist_m > pos - 5.0f) {
            v->turn_idx = i;
            break;
        }
    if (v->turn_idx >= 0) {
        const cc_turn_t *t = &r->turns[v->turn_idx];
        v->turn_type = t->type;
        float d = (float)t->dist_m - pos;
        v->dist_to_turn_m = d > 0 ? (uint32_t)lroundf(d) : 0;
        if (t->type == CC_TURN_ROUNDABOUT)
            snprintf(v->instr, sizeof v->instr, "Roundabout, exit %u", t->exit);
        else
            snprintf(v->instr, sizeof v->instr, "%s", cc_turn_text(t->type));
        if (t->street[0]) {
            size_t l = strlen(v->instr);
            snprintf(v->instr + l, sizeof v->instr - l, " \xC2\xB7 %s", t->street);
        }
    }
    /* ETA: mean moving speed of the last 10 minutes, at least 10 km/h; invalid under 2 minutes */
    if (n->spd_n >= 120) {
        uint64_t sum = 0;
        uint32_t k = 0;
        for (int i = 0; i < n->spd_n; i++)
            if (n->spd[i] >= 500) { sum += n->spd[i]; k++; }
        double mps = k ? (double)sum / k / 1000.0 : 0;
        if (mps < 10.0 / 3.6)
            mps = 10.0 / 3.6;
        v->eta_valid = 1;
        v->eta_s = (uint32_t)lround(v->dist_left_m / mps);
    }
    /* Map scale (brief 6.3) */
    static const uint32_t scales[] = {100, 200, 400, 800, 1600, 3200};
    /* 1.5 x the distance to the next turn must fit in the space above the rider, who sits at 60 % of the
     * 158 px map height (about 95 px of a 240 px view width), not just across the view (QUESTIONS Q21) */
    float need = v->turn_idx >= 0 ? 1.5f * (float)v->dist_to_turn_m * 240.0f / 95.0f : 200.0f;
    if (need < 200.0f) need = 200.0f;
    v->map_scale_m = 3200;
    for (unsigned i = 0; i < sizeof scales / sizeof scales[0]; i++)
        if ((float)scales[i] >= need) { v->map_scale_m = scales[i]; break; }

    /* Climb page */
    v->climb_idx = n->climb_idx;
    if (n->have_pos && !n->off_route && n->climb_idx >= 0) {
        const cc_climb_t *c = &r->climbs[n->climb_idx];
        v->climb_show = 1;
        v->on_climb = n->on_climb;
        v->approaching = n->approaching;
        float end = (float)(c->start_m + c->length_m);
        v->climb_dist_left_m = end - pos > 0 ? (uint32_t)lroundf(end - pos) : 0;
        float gain = 0;
        if (n->on_climb && n->climb_entered)
            gain = n->climb_gain_at_entry + (baro_ascent >= 0 ? baro_ascent - n->climb_base_ascent : 0);
        float al = (float)c->ascent_m - gain;
        if (al < 0) al = 0;
        if (al > c->ascent_m) al = c->ascent_m;
        v->climb_ascent_left_m = (uint32_t)lroundf(al);
        v->climb_to_start_m = (float)c->start_m > pos ? (uint32_t)lroundf((float)c->start_m - pos) : 0;
        uint32_t t = (uint32_t)cc_div_half_up(c->length_m, 100);
        snprintf(v->climb_summary, sizeof v->climb_summary, "%u.%u km \xC2\xB7 %u m climb", t / 10, t % 10, c->ascent_m);
    }
}
