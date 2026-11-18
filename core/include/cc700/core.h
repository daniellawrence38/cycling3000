/* core.h: the unit's single-threaded, event-driven core (brief 2). One task owns it: cc_tick at 10 Hz
 * or faster plus the event functions below. The UI reads cc_view() and sends commands back; it holds
 * screen navigation but never ride state. No heap after cc_init. */
#ifndef CC700_CORE_H
#define CC700_CORE_H

#include <stdint.h>

#include "cc700/alerts.h"
#include "cc700/altitude.h"
#include "cc700/fit.h"
#include "cc700/hrhist.h"
#include "cc700/link.h"
#include "cc700/nav.h"
#include "cc700/sensors.h"
#include "cc700/settings.h"
#include "cc700/store.h"
#include "cc700/workout.h"
#include "hal_fs.h"
#include "hal_gnss.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CC_RS_READY,
    CC_RS_RECORDING,
    CC_RS_PAUSED,
    CC_RS_PAUSE_MENU,
    CC_RS_CONFIRM_DISCARD,
    CC_RS_SAVING,
    CC_RS_SUMMARY,
} cc_ride_state_t;

typedef enum { CC_BTN_L, CC_BTN_R, CC_BTN_PWR, CC_BTN_N } cc_btn_t;

/* A button gesture the core leaves to the UI (menus, page changes, blanking). */
typedef struct {
    cc_btn_t btn;
    int hold;
} cc_gesture_t;

typedef enum { CC_PAGE_MAIN, CC_PAGE_WORKOUT, CC_PAGE_CLIMB, CC_PAGE_NAV } cc_page_t;
typedef enum { CC_SENS_OFF, CC_SENS_SEARCHING, CC_SENS_OK, CC_SENS_LOST } cc_sens_state_t;
enum { CC_PM_RESUME, CC_PM_FINISH, CC_PM_DISCARD, CC_PM_N };
enum { CC_CF_KEEP, CC_CF_DISCARD, CC_CF_N };

#define CC_HOLD_BOTTOM_MS 1000u
#define CC_HOLD_POWER_MS 2000u

typedef struct {
    cc_ride_state_t state;
    int pause_auto;                  /* PAUSED by the auto-pause rule */
    int focus;                       /* pause menu row or Confirm button */
    /* timers (s) */
    uint32_t timer_s, elapsed_s, paused_s, lap_s;
    /* live values; *_ok false means "--" */
    int power3_ok, power10_ok, hr_ok, cad_ok, speed_ok;
    int32_t power3, power10, hr, cad;
    uint32_t speed_mms;
    uint32_t dist_m;
    uint32_t avg_speed_mms;
    int32_t avg_power; int avg_power_ok;
    int zone;                         /* of the 3 s power, 0 if invalid */
    int grad_ok; int32_t grad_x10; int steep;
    int alt_ok; int32_t alt_m; uint32_t ascent_m;
    /* status */
    cc_sens_state_t power_state, hr_state;
    int gps_fix, gps_lost;
    int32_t lat_e7, lon_e7;           /* last usable fix (valid while gps_fix) */
    float course_deg;
    int phone_connected;
    int battery_pct, charging;
    int utc_known;
    uint32_t utc;                     /* 0 if unknown */
    int16_t tz_min;
    /* loaded content */
    int workout_loaded;
    char workout_name[CC_WO_MAX_NAME + 1];
    uint32_t workout_secs; int workout_steps;
    cc_wo_view_t wo;
    int route_loaded;
    char route_name[CC_RT_MAX_NAME + 1];
    uint32_t route_dist_m; uint16_t route_ascent_m; uint8_t route_climbs;
    cc_nav_view_t nav;
    /* page ring */
    cc_page_t ring[4];
    int ring_n;
    /* overlays */
    cc_banner_t banner;               /* id CC_AL_NONE: none */
    struct { int active; cc_btn_t side; uint32_t elapsed_ms; } hold;
    /* finished ride (SUMMARY) */
    cc_ride_meta_t summary;
    int storage_error;
    int power_off_request;
    uint16_t ftp;
    cc_settings_t settings;
    uint32_t ride_id;
    int ride_count;
    uint32_t saved_seq;               /* increments each time a ride is saved (EVENT_RIDE_SAVED) */
    uint32_t saved_ride_id;
    /* Heart rate over the last 5 min of this ride, one entry a second while recording or paused,
       reset at START (hrhist.h). The heart-rate graph draws it; read it with cc_hrhist_at. */
    cc_hrhist_t hr_hist;
} cc_view_t;

int cc_init(hal_fs_t *fs, uint32_t now_ms);
void cc_tick(uint32_t now_ms);

/* Inputs */
void cc_on_button(uint32_t now_ms, cc_btn_t b, int down);
void cc_on_power_sample(uint32_t now_ms, int32_t watts, int has_crank, uint16_t revs, uint16_t time);
void cc_on_hr_sample(uint32_t now_ms, int32_t bpm);
void cc_on_sensor_state(int kind, cc_sens_state_t st); /* kind 1 power, 2 heart rate */
void cc_on_gnss(uint32_t now_ms, const hal_gnss_fix_t *fix);
void cc_on_baro(uint32_t now_ms, int32_t pa);
void cc_on_battery(int pct, int charging);
void cc_on_phone(int connected);
void cc_set_utc(uint32_t now_ms, uint32_t unix_s);
void cc_set_tz(int16_t tz_min);

/* Commands from the UI */
int cc_cmd_start(uint32_t now_ms);
void cc_cmd_pause_menu_select(uint32_t now_ms, int row);  /* touch on a pause menu row */
void cc_cmd_confirm_select(uint32_t now_ms, int button);  /* touch on a Confirm button */
void cc_cmd_summary_done(void);
int cc_cmd_set_ftp(uint16_t ftp);                         /* stopped only; 0 on success */
int cc_cmd_settings(const cc_settings_t *s);              /* brightness, screen off, units, auto-pause */
int cc_cmd_load_workout(const uint8_t id[8]);
int cc_cmd_load_route(const uint8_t id[8]);
void cc_cmd_unload_workout(void);
void cc_cmd_unload_route(void);
/* The host could not switch the unit off (hal_power_off returned): clears power_off_request, so the
 * rider's next power-off request is a new one the host acts on, instead of a flag that stays set and
 * is either resent every tick or ignored until reboot. The ride, if there was one, is already saved. */
void cc_power_off_cancel(void);

/* Fills the link server's host callbacks with the core's. */
void cc_link_host_bind(cc_link_host_t *h);

/* Gestures for the UI, oldest first. Returns 0 when none. */
int cc_poll_gesture(cc_gesture_t *g);

const cc_view_t *cc_view(void);
hal_fs_t *cc_fs(void);
/* Ride state code for the protocol: 0 ready, 1 recording, 2 paused. */
int cc_ride_active(void);

#ifdef __cplusplus
}
#endif
#endif
