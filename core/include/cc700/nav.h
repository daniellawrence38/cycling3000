/* nav.h: route object CR1 (protocol 3.3), local projection, snapping, off route, turns, ETA and the live
 * climb (brief 5.7, spec 8). float32 is used for the projection. */
#ifndef CC700_NAV_H
#define CC700_NAV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CC_RT_MAX_PTS 8000
#define CC_RT_MAX_TURNS 500
#define CC_RT_MAX_CLIMBS 60
#define CC_RT_MAX_PROF 12000
#define CC_RT_MAX_BYTES 131072
#define CC_RT_MAX_NAME 40
#define CC_RT_MAX_STREET 40

enum {
    CC_TURN_CONTINUE, CC_TURN_SLIGHT_LEFT, CC_TURN_LEFT, CC_TURN_SHARP_LEFT, CC_TURN_SLIGHT_RIGHT, CC_TURN_RIGHT,
    CC_TURN_SHARP_RIGHT, CC_TURN_UTURN, CC_TURN_ROUNDABOUT, CC_TURN_ARRIVE, CC_TURN_DEPART,
};

typedef struct {
    uint32_t dist_m;
    uint8_t type, exit;
    char street[CC_RT_MAX_STREET + 1];
} cc_turn_t;

typedef struct {
    uint32_t start_m;
    uint16_t length_m, ascent_m;
    int16_t avg_x10, max_x10;
} cc_climb_t;

typedef struct {
    uint8_t id[8];
    char name[CC_RT_MAX_NAME + 1];
    uint32_t dist_m;
    uint16_t ascent_m;
    uint16_t npts, nturns, nprof, prof_step;
    uint8_t nclimbs;
    int32_t lat[CC_RT_MAX_PTS], lon[CC_RT_MAX_PTS];
    float x[CC_RT_MAX_PTS], y[CC_RT_MAX_PTS], cum[CC_RT_MAX_PTS];
    int32_t lat0, lon0;
    float kx;                          /* metres per 1e-7 degree of longitude at lat0 */
    int16_t prof[CC_RT_MAX_PROF];      /* decimetres */
    cc_turn_t turns[CC_RT_MAX_TURNS];
    cc_climb_t climbs[CC_RT_MAX_CLIMBS];
} cc_route_t;

typedef enum {
    CC_CR1_OK = 0, CC_CR1_TRUNCATED, CC_CR1_MAGIC, CC_CR1_VERSION, CC_CR1_FLAGS, CC_CR1_COUNTS, CC_CR1_NAME,
    CC_CR1_COORD, CC_CR1_VARINT, CC_CR1_TRAILING, CC_CR1_ID, CC_CR1_TOO_BIG, CC_CR1_TURN, CC_CR1_CLIMB,
} cc_cr1_err_t;

cc_cr1_err_t cc_cr1_decode(const uint8_t *obj, size_t len, cc_route_t *out);

/* Local projection (equirectangular about the route's midpoint latitude). */
void cc_route_project(const cc_route_t *r, int32_t lat_e7, int32_t lon_e7, float *x, float *y);
/* Profile elevation at a distance, metres (linear between samples). Returns 0 if no profile. */
int cc_route_profile_at(const cc_route_t *r, float dist_m, float *elev_m);

/* ------------------------------------------------------------------ follower */
typedef struct {
    const cc_route_t *r;
    int loaded;
    /* snapping */
    int have_pos;
    int pos_idx;               /* segment index of the last accepted position */
    float route_pos_m;
    float dist_to_route_m;     /* distance from the fix to the nearest point found */
    int no_accept_s;
    /* off route */
    int off_route;
    int out_s, back_s;
    /* ETA: speeds of the last 10 minutes */
    uint16_t spd[600];
    int spd_n, spd_head;
    uint64_t spd_sum;
    uint32_t spd_moving;
    /* live climb */
    int climb_idx;             /* active climb, -1 none */
    int on_climb, approaching;
    int climb_entered;         /* the baseline is set for climb_idx */
    float climb_base_ascent;
    float climb_gain_at_entry;
    int ahead_alerted;         /* climb index already announced, -1 none */
} cc_nav_t;

enum { CC_NAV_EV_OFF_START = 1, CC_NAV_EV_OFF_END = 2, CC_NAV_EV_CLIMB_AHEAD = 4, CC_NAV_EV_TURN_CHANGED = 8 };

typedef struct {
    int loaded, have_pos, off_route;
    float route_pos_m, dist_to_route_m;
    int turn_idx;              /* -1 none */
    uint8_t turn_type;         /* CC_TURN_* of the next turn */
    uint32_t dist_to_turn_m;
    char instr[64];
    uint32_t dist_left_m;
    int eta_valid;
    uint32_t eta_s;            /* seconds from now */
    uint32_t map_scale_m;      /* metres per view width */
    /* climb page */
    int climb_show;            /* a route, on route, and a climb left */
    int climb_idx;
    int on_climb, approaching;
    uint32_t climb_dist_left_m;
    uint32_t climb_ascent_left_m;
    uint32_t climb_to_start_m;
    char climb_summary[40];
} cc_nav_view_t;

void cc_nav_init(cc_nav_t *n);
void cc_nav_load(cc_nav_t *n, const cc_route_t *r);
void cc_nav_unload(cc_nav_t *n);
/* Once per second. fix_ok: usable fix. speed in mm/s. baro_ascent: the ride's barometric ascent (m),
 * negative if none. Returns CC_NAV_EV_* bits. */
unsigned cc_nav_second(cc_nav_t *n, int fix_ok, int32_t lat_e7, int32_t lon_e7, uint32_t speed_mms, float baro_ascent);
void cc_nav_view(const cc_nav_t *n, float baro_ascent, cc_nav_view_t *v);

/* Turn distance text (spec 8.2): 1 km or more "1.2 km"; under 1 km to 10 m; under 100 m to 5 m. */
void cc_fmt_turn_dist(char *out, int cap, uint32_t m);
const char *cc_turn_text(uint8_t type);

/* Test and tool helper: encode a CR1 object. Returns length or 0. */
size_t cc_cr1_encode(const char *name, const int32_t *lat, const int32_t *lon, uint16_t npts, uint32_t dist_m,
                     uint16_t ascent_m, const int16_t *prof, uint16_t nprof, uint16_t prof_step, const cc_turn_t *turns,
                     uint16_t nturns, const cc_climb_t *climbs, uint8_t nclimbs, uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
