#include "player.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc700/core.h"

int fx_load(fx_t *f, const char *path)
{
    memset(f, 0, sizeof *f);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    char line[256];
    if (!fgets(line, sizeof line, fp)) { fclose(fp); return -1; }
    int cap = 0;
    while (fgets(line, sizeof line, fp)) {
        if (f->n == cap) {
            cap = cap ? cap * 2 : 256;
            f->rows = realloc(f->rows, (size_t)cap * sizeof *f->rows);
        }
        fx_row_t *r = &f->rows[f->n];
        if (sscanf(line, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%f", &r->t, &r->fix, &r->lat, &r->lon, &r->speed_mms, &r->pa,
                   &r->power, &r->cad, &r->hr, &r->alt) == 10)
            f->n++;
    }
    fclose(fp);
    return 0;
}

void fx_free(fx_t *f) { free(f->rows); }

uint32_t fx_play_row(fx_t *f, int i, uint32_t base_ms)
{
    const fx_row_t *r = &f->rows[i];
    uint32_t now = base_ms + (uint32_t)r->t * 1000u + 500u;
    hal_gnss_fix_t g;
    memset(&g, 0, sizeof g);
    g.valid = r->fix;
    g.lat_e7 = r->lat;
    g.lon_e7 = r->lon;
    g.speed_mps = (float)r->speed_mms / 1000.0f;
    g.alt_m = r->alt;
    cc_on_gnss(now, &g);
    if (r->pa > 0)
        cc_on_baro(now, r->pa);
    if (r->power >= 0) {
        int has_crank = r->cad >= 0;
        if (has_crank && r->cad > 0) {
            double t = r->t + 0.5;
            if (!f->crank_started) {
                f->next_event_s = t;
                f->crank_started = 1;
            }
            while (f->next_event_s <= t) {
                f->revs++;
                f->ctime = (uint16_t)lround(f->next_event_s * 1024.0);
                f->next_event_s += 60.0 / r->cad;
            }
        } else if (has_crank) {
            f->crank_started = 0; /* stopped: the same event repeats */
        }
        cc_on_power_sample(now, r->power, has_crank, f->revs, f->ctime);
    }
    if (r->hr >= 0)
        cc_on_hr_sample(now, r->hr);
    uint32_t boundary = base_ms + (uint32_t)(r->t + 1) * 1000u;
    cc_tick(boundary);
    return boundary;
}
