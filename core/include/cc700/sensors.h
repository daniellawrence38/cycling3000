/* sensors.h: Cycling Power and Heart Rate parsers (V§6.1, V§6.3), cadence (V§6.2), one-second bins
 * (V§6.4), 3 s and 10 s averages and loss detection (brief 5.1). BLE central logic is not here. */
#ifndef CC700_SENSORS_H
#define CC700_SENSORS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ parsers */
typedef struct {
    int16_t power;     /* as sent; the caller clamps negatives to 0 */
    int has_crank;
    uint16_t crank_revs;
    uint16_t crank_time; /* 1/1024 s */
} cc_cps_t;

/* Cycling Power Measurement (0x2A63). Returns 0 if the buffer is too short for its flags. */
int cc_cps_parse(const uint8_t *b, size_t len, cc_cps_t *out);

/* Heart Rate Measurement (0x2A37). Returns 0 if too short. */
int cc_hr_parse(const uint8_t *b, size_t len, uint16_t *bpm);

/* rpm = (dr*60*1024*2 + dt) // (2*dt) on u16 differences. Returns 0 when there is no new event. */
int cc_cadence_rpm(uint16_t prev_revs, uint16_t prev_time, uint16_t revs, uint16_t time, int32_t *rpm);

/* Half-up integer mean (sum*2 + n) // (2*n). n > 0. */
int32_t cc_bin_mean(int64_t sum, int32_t n);

/* ------------------------------------------------------------------ one-second channel */
#define CC_CHAN_RING 10

typedef struct {
    uint32_t lost_ms;       /* silence after which the channel is lost */
    int64_t sum;            /* samples received in the current second */
    int32_t n;
    int have_sample;        /* at least one sample ever */
    uint32_t last_ms;       /* time of the last sample */
    int32_t ring[CC_CHAN_RING];
    uint8_t ring_ok[CC_CHAN_RING];
    int head;               /* index of the newest bin */
    int count;              /* bins pushed, saturating at CC_CHAN_RING */
} cc_chan_t;

void cc_chan_init(cc_chan_t *c, uint32_t lost_ms);
void cc_chan_sample(cc_chan_t *c, uint32_t now_ms, int32_t value);
/* Push the bin for the second that just ended. */
void cc_chan_second(cc_chan_t *c, uint32_t now_ms);
/* Newest bin. Returns 0 if invalid. */
int cc_chan_last(const cc_chan_t *c, int32_t *v);
/* Half-up mean of the valid bins among the newest n (1..10). Returns 0 if none valid. */
int cc_chan_avg(const cc_chan_t *c, int n, int32_t *v);
int cc_chan_lost(const cc_chan_t *c, uint32_t now_ms);

/* ------------------------------------------------------------------ sensors together */
typedef struct {
    cc_chan_t power;
    cc_chan_t hr;
    int crank_have;
    uint16_t crank_revs, crank_time;
    uint32_t crank_event_ms;  /* time of the last new crank event */
    int cad_have;             /* a cadence has been computed */
    int32_t cad;              /* last computed rpm */
} cc_sensors_t;

void cc_sensors_init(cc_sensors_t *s);
void cc_sensors_power(cc_sensors_t *s, uint32_t now_ms, int32_t watts, int has_crank, uint16_t revs, uint16_t time);
void cc_sensors_hr(cc_sensors_t *s, uint32_t now_ms, int32_t bpm);
/* At each second boundary. */
void cc_sensors_second(cc_sensors_t *s, uint32_t now_ms);
/* Cadence now: returns 0 if invalid (power lost or none yet). */
int cc_sensors_cadence(const cc_sensors_t *s, uint32_t now_ms, int32_t *rpm);

#ifdef __cplusplus
}
#endif
#endif
