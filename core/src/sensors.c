#include "cc700/sensors.h"

#include <string.h>

#include "cc700/provisional.h"
#include "cc700/util.h"

int cc_cps_parse(const uint8_t *b, size_t len, cc_cps_t *out)
{
    if (len < 4)
        return 0;
    uint16_t flags = cc_get_u16(b);
    out->power = cc_get_i16(b + 2);
    out->has_crank = 0;
    size_t off = 4;
    if (flags & 0x0001) off += 1; /* pedal power balance */
    if (flags & 0x0004) off += 2; /* accumulated torque */
    if (flags & 0x0010) off += 6; /* wheel revolution data */
    if (flags & 0x0020) {
        if (off + 4 > len)
            return 0;
        out->has_crank = 1;
        out->crank_revs = cc_get_u16(b + off);
        out->crank_time = cc_get_u16(b + off + 2);
    }
    return 1;
}

int cc_hr_parse(const uint8_t *b, size_t len, uint16_t *bpm)
{
    if (len < 2)
        return 0;
    if (b[0] & 1) {
        if (len < 3)
            return 0;
        *bpm = cc_get_u16(b + 1);
    } else {
        *bpm = b[1];
    }
    return 1;
}

int cc_cadence_rpm(uint16_t prev_revs, uint16_t prev_time, uint16_t revs, uint16_t time, int32_t *rpm)
{
    uint32_t dr = (uint16_t)(revs - prev_revs);
    uint32_t dt = (uint16_t)(time - prev_time);
    if (dr == 0 || dt == 0)
        return 0;
    *rpm = (int32_t)cc_floordiv((int64_t)dr * 60 * 1024 * 2 + dt, 2 * (int64_t)dt);
    return 1;
}

int32_t cc_bin_mean(int64_t sum, int32_t n)
{
    return (int32_t)cc_floordiv(sum * 2 + n, 2 * (int64_t)n);
}

/* ------------------------------------------------------------------ channel */
void cc_chan_init(cc_chan_t *c, uint32_t lost_ms)
{
    memset(c, 0, sizeof *c);
    c->lost_ms = lost_ms;
    c->head = CC_CHAN_RING - 1;
}

void cc_chan_sample(cc_chan_t *c, uint32_t now_ms, int32_t value)
{
    c->sum += value;
    c->n++;
    c->have_sample = 1;
    c->last_ms = now_ms;
}

void cc_chan_second(cc_chan_t *c, uint32_t now_ms)
{
    int32_t v = 0;
    int ok = 0;
    if (c->n > 0) {
        v = cc_bin_mean(c->sum, c->n);
        ok = 1;
    } else if (c->have_sample && c->count > 0 && c->ring_ok[c->head] && now_ms - c->last_ms <= CC_POWER_STALE_MS) {
        v = c->ring[c->head];
        ok = 1;
    }
    c->head = (c->head + 1) % CC_CHAN_RING;
    c->ring[c->head] = v;
    c->ring_ok[c->head] = (uint8_t)ok;
    if (c->count < CC_CHAN_RING)
        c->count++;
    c->sum = 0;
    c->n = 0;
}

int cc_chan_last(const cc_chan_t *c, int32_t *v)
{
    if (c->count == 0 || !c->ring_ok[c->head])
        return 0;
    *v = c->ring[c->head];
    return 1;
}

int cc_chan_avg(const cc_chan_t *c, int n, int32_t *v)
{
    int64_t sum = 0;
    int32_t k = 0;
    if (n > c->count)
        n = c->count;
    for (int i = 0; i < n; i++) {
        int idx = (c->head - i + CC_CHAN_RING) % CC_CHAN_RING;
        if (c->ring_ok[idx]) {
            sum += c->ring[idx];
            k++;
        }
    }
    if (k == 0)
        return 0;
    *v = cc_bin_mean(sum, k);
    return 1;
}

int cc_chan_lost(const cc_chan_t *c, uint32_t now_ms)
{
    return c->have_sample && now_ms - c->last_ms >= c->lost_ms;
}

/* ------------------------------------------------------------------ sensors */
void cc_sensors_init(cc_sensors_t *s)
{
    memset(s, 0, sizeof *s);
    cc_chan_init(&s->power, CC_POWER_LOST_MS);
    cc_chan_init(&s->hr, CC_HR_LOST_MS);
}

void cc_sensors_power(cc_sensors_t *s, uint32_t now_ms, int32_t watts, int has_crank, uint16_t revs, uint16_t time)
{
    cc_chan_sample(&s->power, now_ms, watts < 0 ? 0 : watts);
    if (!has_crank)
        return;
    if (s->crank_have) {
        int32_t rpm;
        if (cc_cadence_rpm(s->crank_revs, s->crank_time, revs, time, &rpm)) {
            s->cad = rpm;
            s->cad_have = 1;
            s->crank_event_ms = now_ms;
        }
    } else {
        s->crank_event_ms = now_ms;
    }
    s->crank_have = 1;
    s->crank_revs = revs;
    s->crank_time = time;
}

void cc_sensors_hr(cc_sensors_t *s, uint32_t now_ms, int32_t bpm)
{
    cc_chan_sample(&s->hr, now_ms, bpm);
}

void cc_sensors_second(cc_sensors_t *s, uint32_t now_ms)
{
    cc_chan_second(&s->power, now_ms);
    cc_chan_second(&s->hr, now_ms);
}

int cc_sensors_cadence(const cc_sensors_t *s, uint32_t now_ms, int32_t *rpm)
{
    if (!s->power.have_sample || cc_chan_lost(&s->power, now_ms))
        return 0;
    if (!s->crank_have)
        return 0; /* the meter sends no crank data */
    if (now_ms - s->crank_event_ms >= CC_CADENCE_ZERO_MS) {
        *rpm = 0; /* no new crank event for 3 s while power is not lost */
        return 1;
    }
    if (!s->cad_have)
        return 0; /* only one crank sample so far, no difference yet */
    *rpm = s->cad;
    return 1;
}
