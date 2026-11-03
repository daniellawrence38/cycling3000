/* provisional.h: every provisional constant in one place (START-HERE rule 5, spec 13.2).
 * Daniel revises these after riding. Change them here only. */
#ifndef CC700_PROVISIONAL_H
#define CC700_PROVISIONAL_H

/* Workout status pill (spec 7.6, V§7, V§7.1) */
#define CC_PILL_DWELL_S        2    /* consecutive seconds outside on the same side before leaving IN RANGE */
#define CC_PILL_SETTLE_S       5    /* pill hidden for the first seconds of each step */
#define CC_SHORT_STEP_S        60   /* steps shorter than this: no pill, no next-step banner */
#define CC_NEXTSTEP_LEAD_S     5    /* next-step banner shows in the last seconds of a step */

/* FTP (spec 2, 6) */
#define CC_FTP_DEFAULT_W       200
#define CC_FTP_MIN_W           50
#define CC_FTP_MAX_W           600

/* Sensors (brief 5.1, spec 10.7) */
#define CC_POWER_STALE_MS      2000 /* an empty second reuses the previous bin if the last sample is this recent */
#define CC_POWER_LOST_MS       3000
#define CC_HR_LOST_MS          5000
#define CC_CADENCE_ZERO_MS     3000 /* no new crank event for this long: cadence 0 */

/* Auto-pause (spec 10.3) in mm/s */
#define CC_AUTOPAUSE_BELOW_MMS 700
#define CC_AUTOPAUSE_SECS      5
#define CC_AUTORESUME_MMS      1400
#define CC_AUTORESUME_SECS     2

/* Gradient (brief 5.3), tenths of a percent */
#define CC_STEEP_ON_X10        80
#define CC_STEEP_OFF_X10       70
#define CC_STEEP_ON_SECS       3

/* Off route (spec 8.3) */
#define CC_OFFROUTE_OUT_M      40
#define CC_OFFROUTE_OUT_SECS   8
#define CC_OFFROUTE_BACK_M     25
#define CC_OFFROUTE_BACK_SECS  3
#define CC_OFFROUTE_MIN_KMH    3
#define CC_CLIMB_AHEAD_M       500

/* Retention (spec 10.4) */
#define CC_RIDES_KEEP          100
#define CC_RIDES_HARD_MAX      240

/* Libraries (spec 7.3, 8.1) */
#define CC_MAX_WORKOUTS        20
#define CC_MAX_ROUTES          10

/* Low battery thresholds (spec 10.2) */
#define CC_LOWBATT_1_PCT       15
#define CC_LOWBATT_2_PCT       5

#endif
