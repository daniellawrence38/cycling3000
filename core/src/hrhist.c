/* The last five minutes of heart rate (hrhist.h). */
#include "cc700/hrhist.h"

#include <string.h>

#include "cc700/sensors.h"

void cc_hrhist_reset(cc_hrhist_t *h) { memset(h, 0, sizeof *h); }

void cc_hrhist_push(cc_hrhist_t *h, int ok, int32_t bpm)
{
    uint8_t v = CC_HRHIST_NONE;
    if (ok && bpm > 0)
        v = (uint8_t)(bpm > (int32_t)CC_HRHIST_MAX ? CC_HRHIST_MAX : (uint32_t)bpm);
    h->head = h->n ? (uint16_t)((h->head + 1u) % CC_HRHIST_N) : 0u;
    h->bpm[h->head] = v;
    if (h->n < CC_HRHIST_N)
        h->n++;
}

int cc_hrhist_at(const cc_hrhist_t *h, int age_s)
{
    if (age_s < 0 || age_s >= (int)h->n)
        return -1;
    uint8_t v = h->bpm[(h->head + CC_HRHIST_N - (unsigned)age_s) % CC_HRHIST_N];
    return v == CC_HRHIST_NONE ? -1 : (int)v;
}

int cc_hrhist_stats(const cc_hrhist_t *h, cc_hrhist_stats_t *st)
{
    memset(st, 0, sizeof *st);
    int64_t sum = 0;
    for (int a = 0; a < (int)h->n; a++) {
        int v = cc_hrhist_at(h, a);
        if (v < 0)
            continue;
        if (!st->n_valid || v < st->min) st->min = v;
        if (!st->n_valid || v > st->max) st->max = v;
        sum += v;
        st->n_valid++;
    }
    if (st->n_valid)
        st->avg = (int)cc_bin_mean(sum, st->n_valid);
    return st->n_valid;
}
