#include "cc700/workout.h"

#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"
#include "cc700/zones.h"

static int printable(const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] < 0x20 || s[i] > 0x7E)
            return 0;
    return 1;
}

void cc_object_id(const uint8_t *obj, size_t len, uint8_t id[8])
{
    static const uint8_t zero[8];
    uint8_t h[32];
    cc_sha256_t s;
    cc_sha256_init(&s);
    cc_sha256_update(&s, obj, 4);
    cc_sha256_update(&s, zero, 8);
    cc_sha256_update(&s, obj + 12, len - 12);
    cc_sha256_final(&s, h);
    memcpy(id, h, 8);
}

cc_cw1_err_t cc_cw1_decode(const uint8_t *b, size_t len, cc_workout_t *out)
{
    if (len > CC_WO_MAX_BYTES)
        return CC_CW1_TOO_BIG;
    if (len < 18)
        return CC_CW1_TRUNCATED;
    if (b[0] != 'C' || b[1] != 'W')
        return CC_CW1_MAGIC;
    if (b[2] != 1)
        return CC_CW1_VERSION;
    if (b[3] != 0)
        return CC_CW1_FLAGS;
    memcpy(out->id, b + 4, 8);
    out->total_secs = cc_get_u32(b + 12);
    out->nsteps = b[16];
    if (out->nsteps < 1 || out->nsteps > CC_WO_MAX_STEPS)
        return CC_CW1_NSTEPS;
    size_t nl = b[17];
    if (nl < 1 || nl > CC_WO_MAX_NAME)
        return CC_CW1_NAME;
    if (18 + nl > len)
        return CC_CW1_TRUNCATED;
    if (!printable(b + 18, nl))
        return CC_CW1_NAME;
    memcpy(out->name, b + 18, nl);
    out->name[nl] = 0;
    size_t off = 18 + nl;
    uint32_t sum = 0;
    for (int i = 0; i < out->nsteps; i++) {
        cc_step_t *s = &out->steps[i];
        if (off + 11 > len)
            return CC_CW1_TRUNCATED;
        const uint8_t *p = b + off;
        s->secs = cc_get_u16(p);
        s->p0 = cc_get_u16(p + 2);
        s->p1 = cc_get_u16(p + 4);
        uint8_t fl = p[6];
        s->rpm = p[7];
        s->rep_idx = p[8];
        s->rep_total = p[9];
        size_t ll = p[10];
        if (fl & ~1u)
            return CC_CW1_FLAGS;
        s->ramp = fl & 1;
        if (s->secs < CC_WO_SECS_MIN || s->secs > CC_WO_SECS_MAX)
            return CC_CW1_SECS;
        if (s->p0 < CC_WO_P10_MIN || s->p0 > CC_WO_P10_MAX || s->p1 < CC_WO_P10_MIN || s->p1 > CC_WO_P10_MAX)
            return CC_CW1_PCT;
        if (s->ramp ? s->p0 == s->p1 : s->p0 >= s->p1)
            return CC_CW1_PCT;
        if (s->rpm != 0 && (s->rpm < 40 || s->rpm > 130))
            return CC_CW1_RPM;
        if (ll < 1 || ll > CC_WO_MAX_LABEL)
            return CC_CW1_LABEL;
        if (off + 11 + ll > len)
            return CC_CW1_TRUNCATED;
        if (!printable(p + 11, ll))
            return CC_CW1_LABEL;
        memcpy(s->label, p + 11, ll);
        s->label[ll] = 0;
        sum += s->secs;
        off += 11 + ll;
    }
    if (off != len)
        return CC_CW1_TRAILING;
    if (sum != out->total_secs || sum > CC_WO_TOTAL_MAX)
        return CC_CW1_TOTAL;
    uint8_t id[8];
    cc_object_id(b, len, id);
    if (memcmp(id, out->id, 8) != 0)
        return CC_CW1_ID;
    return CC_CW1_OK;
}

size_t cc_cw1_encode(const cc_workout_t *wo, uint8_t *out, size_t cap)
{
    size_t nl = strlen(wo->name);
    size_t off = 18 + nl;
    if (off > cap)
        return 0;
    uint32_t total = 0;
    out[0] = 'C'; out[1] = 'W'; out[2] = 1; out[3] = 0;
    memset(out + 4, 0, 8);
    out[16] = wo->nsteps;
    out[17] = (uint8_t)nl;
    memcpy(out + 18, wo->name, nl);
    for (int i = 0; i < wo->nsteps; i++) {
        const cc_step_t *s = &wo->steps[i];
        size_t ll = strlen(s->label);
        if (off + 11 + ll > cap)
            return 0;
        uint8_t *p = out + off;
        cc_put_u16(p, s->secs);
        cc_put_u16(p + 2, s->p0);
        cc_put_u16(p + 4, s->p1);
        p[6] = s->ramp ? 1 : 0;
        p[7] = s->rpm;
        p[8] = s->rep_idx;
        p[9] = s->rep_total;
        p[10] = (uint8_t)ll;
        memcpy(p + 11, s->label, ll);
        off += 11 + ll;
        total += s->secs;
    }
    cc_put_u32(out + 12, total);
    cc_object_id(out, off, out + 4);
    return off;
}

void cc_step_range(const cc_step_t *s, int32_t ftp, int32_t e, int32_t *lo_w, int32_t *hi_w)
{
    if (!s->ramp) {
        *lo_w = cc_watts(ftp, s->p0);
        *hi_w = cc_watts(ftp, s->p1);
        return;
    }
    int32_t lo, hi;
    cc_widen(cc_ramp_p10(s->p0, s->p1, s->secs, e), &lo, &hi);
    *lo_w = cc_watts(ftp, lo);
    *hi_w = cc_watts(ftp, hi);
}

int cc_step_target_zone(const cc_step_t *s, int32_t e)
{
    int32_t p = s->ramp ? cc_ramp_p10(s->p0, s->p1, s->secs, e) : (int32_t)cc_floordiv((int32_t)s->p0 + s->p1, 2);
    return cc_zone_of_p10(p);
}

/* ------------------------------------------------------------------ pill (ref.pill) */
void cc_pill_reset(cc_pill_t *p)
{
    p->state = CC_PILL_IN;
    p->k = 0;
    p->side = 0;
}

cc_pill_state_t cc_pill_feed(cc_pill_t *p, int32_t lo, int32_t hi, int32_t power, int32_t *delta)
{
    if (power >= lo && power <= hi) {
        p->state = CC_PILL_IN;
        p->k = 0;
        p->side = 0;
    } else {
        int s = power > hi ? 1 : -1;
        if (p->state != CC_PILL_IN) {
            p->state = s > 0 ? CC_PILL_HIGH : CC_PILL_LOW;
        } else {
            p->k = (s == p->side) ? p->k + 1 : 1;
            p->side = s;
            if (p->k >= CC_PILL_DWELL_S)
                p->state = s > 0 ? CC_PILL_HIGH : CC_PILL_LOW;
        }
    }
    *delta = p->state == CC_PILL_HIGH ? power - hi : p->state == CC_PILL_LOW ? lo - power : 0;
    return p->state;
}

/* ------------------------------------------------------------------ engine */
static void fill_view(cc_wo_engine_t *e, uint32_t timer_s)
{
    cc_wo_view_t *v = &e->view;
    const cc_workout_t *wo = &e->wo;
    int i = e->cur;
    const cc_step_t *s = &wo->steps[i];
    uint32_t el = e->view.complete ? s->secs : timer_s - e->start[i];
    if (el > s->secs)
        el = s->secs;
    v->loaded = 1;
    v->step = i;
    v->nsteps = wo->nsteps;
    v->label = s->label;
    v->step_secs = s->secs;
    v->elapsed_s = el;
    v->remaining_s = s->secs - el;
    cc_step_range(s, e->ftp, (int32_t)el, &v->lo_w, &v->hi_w);
    v->target_zone = cc_step_target_zone(s, (int32_t)el);
    v->rep_idx = s->rep_idx;
    v->rep_total = s->rep_total;
    v->rpm = s->rpm;
    v->has_next = !v->complete && i + 1 < wo->nsteps;
    if (v->has_next) {
        const cc_step_t *n = &wo->steps[i + 1];
        v->next_label = n->label;
        cc_step_range(n, e->ftp, 0, &v->next_lo_w, &v->next_hi_w);
        v->next_secs = n->secs;
        v->next_zone = cc_step_target_zone(n, 0);
    } else {
        v->next_label = "";
        v->next_lo_w = v->next_hi_w = 0;
        v->next_secs = 0;
        v->next_zone = 0;
    }
    v->nextstep_banner = v->has_next && s->secs >= CC_SHORT_STEP_S && v->remaining_s >= 1 &&
                         v->remaining_s <= CC_NEXTSTEP_LEAD_S;
}

void cc_wo_engine_load(cc_wo_engine_t *e, const cc_workout_t *wo, int32_t ftp)
{
    memset(e, 0, sizeof *e);
    e->wo = *wo;
    e->ftp = ftp;
    uint32_t t = 0;
    for (int i = 0; i < wo->nsteps; i++) {
        e->start[i] = t;
        t += wo->steps[i].secs;
    }
    e->wo.total_secs = t; /* trust the steps, not the header */
    e->cur = 0;
    cc_pill_reset(&e->pill);
    fill_view(e, 0);
    e->view.pill = CC_PILL_HIDDEN;
}

void cc_wo_engine_unload(cc_wo_engine_t *e)
{
    memset(e, 0, sizeof *e);
}

void cc_wo_engine_set_ftp(cc_wo_engine_t *e, int32_t ftp)
{
    if (!e->view.loaded)
        return;
    e->ftp = ftp;
    fill_view(e, e->view.complete ? 0 : e->start[e->cur] + e->view.elapsed_s);
}

unsigned cc_wo_engine_second(cc_wo_engine_t *e, uint32_t timer_s, int p3_valid, int32_t p3)
{
    cc_wo_view_t *v = &e->view;
    unsigned ev = 0;
    if (!v->loaded || v->complete)
        return 0;
    if (timer_s >= e->wo.total_secs) {
        v->complete = 1;
        e->cur = e->wo.nsteps - 1;
        fill_view(e, timer_s);
        v->pill = CC_PILL_HIDDEN;
        v->pill_delta = 0;
        v->actual_zone = p3_valid ? cc_zone_of_power(p3, e->ftp) : 0;
        return CC_WO_EV_COMPLETE;
    }
    int i = e->cur;
    while (i + 1 < e->wo.nsteps && timer_s >= e->start[i + 1])
        i++;
    if (i != e->cur) {
        e->cur = i;
        e->pill_started = 0;
        ev |= CC_WO_EV_STEP_CHANGE;
    }
    fill_view(e, timer_s);
    const cc_step_t *s = &e->wo.steps[i];
    v->actual_zone = p3_valid ? cc_zone_of_power(p3, e->ftp) : 0;
    v->pill = CC_PILL_HIDDEN;
    v->pill_delta = 0;
    if (s->secs >= CC_SHORT_STEP_S && v->elapsed_s >= CC_PILL_SETTLE_S) {
        if (!e->pill_started) {
            cc_pill_reset(&e->pill);
            e->pill_started = 1;
        }
        if (p3_valid)
            v->pill = cc_pill_feed(&e->pill, v->lo_w, v->hi_w, p3, &v->pill_delta);
    }
    return ev;
}
