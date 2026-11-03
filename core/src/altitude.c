#include "cc700/altitude.h"

#include <math.h>
#include <string.h>

#include "cc700/provisional.h"

float cc_alt_from_pa(float pa)
{
    return 44330.0f * (1.0f - powf(pa / 101325.0f, 0.190263f));
}

void cc_alt_init(cc_alt_t *a)
{
    memset(a, 0, sizeof *a);
    a->next_dist_cm = 0;
}

void cc_alt_ride_start(cc_alt_t *a)
{
    a->ascent = 0;
    a->ref_have = 0;
    a->ring_n = 0;
    a->ring_head = 0;
    a->next_dist_cm = 0;
    a->grad_valid = 0;
    a->grad_x10 = 0;
    a->steep = 0;
    a->steep_secs = 0;
}

void cc_alt_baro(cc_alt_t *a, int32_t pa)
{
    if (pa <= 0)
        return;
    a->sum += cc_alt_from_pa((float)pa);
    a->n++;
}

int cc_alt_absolute(const cc_alt_t *a, float *m)
{
    if (!a->have)
        return 0;
    *m = a->alt + (a->off_done ? a->offset : 0.0f);
    return 1;
}

void cc_alt_second(cc_alt_t *a, int recording, uint32_t dist_cm, uint32_t speed_mms, int gnss_ok, float gnss_alt)
{
    if (a->n > 0) {
        float s = a->sum / (float)a->n;
        if (!a->have) {
            a->alt = s;
            a->have = 1;
        } else {
            a->alt = a->alt + 0.25f * (s - a->alt);
        }
        a->sum = 0;
        a->n = 0;
    }
    if (!a->have)
        return; /* no barometer: gradient and ascent invalid */

    if (gnss_ok && !a->off_done) {
        a->off_sum += gnss_alt - a->alt;
        if (++a->off_n >= 30) {
            a->offset = a->off_sum / (float)a->off_n;
            a->off_done = 1;
        }
    }

    /* Ascent: 1 m hysteresis; the reference follows while not recording so pauses add nothing */
    if (!a->ref_have) {
        a->ref = a->alt;
        a->ref_have = 1;
    } else if (a->alt - a->ref >= 1.0f) {
        if (recording)
            a->ascent += a->alt - a->ref;
        a->ref = a->alt;
    } else if (a->ref - a->alt >= 1.0f) {
        a->ref = a->alt;
    }

    /* Gradient: an entry every 10 m of distance while recording */
    if (recording && speed_mms >= 500 && dist_cm >= a->next_dist_cm) {
        a->ring_head = (a->ring_head + 1) % CC_GRAD_RING;
        a->ring_dist[a->ring_head] = dist_cm;
        a->ring_alt[a->ring_head] = a->alt;
        if (a->ring_n < CC_GRAD_RING)
            a->ring_n++;
        a->next_dist_cm = (dist_cm / 1000u) * 1000u + 1000u;
        if (a->ring_n >= 6) {
            int old = (a->ring_head - 5 + CC_GRAD_RING) % CC_GRAD_RING;
            float g = (a->ring_alt[a->ring_head] - a->ring_alt[old]) / 50.0f * 100.0f;
            if (g > 40.0f) g = 40.0f;
            if (g < -40.0f) g = -40.0f;
            a->grad_x10 = (int32_t)lroundf(g * 10.0f);
            a->grad_valid = 1;
        }
    }

    if (a->grad_valid && a->grad_x10 >= CC_STEEP_ON_X10) {
        if (a->steep_secs < CC_STEEP_ON_SECS)
            a->steep_secs++;
        if (a->steep_secs >= CC_STEEP_ON_SECS)
            a->steep = 1;
    } else {
        a->steep_secs = 0;
        if (!a->grad_valid || a->grad_x10 < CC_STEEP_OFF_X10)
            a->steep = 0;
    }
}
