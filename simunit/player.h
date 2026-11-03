/* Plays a tools/gen_ride CSV fixture into the core, one row per second. */
#ifndef PLAYER_H
#define PLAYER_H
#include <stdint.h>

typedef struct {
    int t, fix;
    int32_t lat, lon;
    int32_t speed_mms, pa, power, cad, hr;
    float alt;
} fx_row_t;

typedef struct {
    fx_row_t *rows;
    int n;
    /* crank event synthesis */
    double next_event_s;
    uint16_t revs, ctime;
    int crank_started;
} fx_t;

int fx_load(fx_t *f, const char *path);
void fx_free(fx_t *f);
/* Feeds row i at base_ms + t*1000 + 500 and ticks the core to the next second boundary.
 * Returns the now_ms of that boundary. */
uint32_t fx_play_row(fx_t *f, int i, uint32_t base_ms);
#endif
