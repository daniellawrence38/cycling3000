/* fit.h: FIT activity writer, recovery and time correction (brief 5.6). Knows nothing of the ride
 * engine: it is driven by begin/event/record/lap/finish calls, and computes its lap and session
 * totals from the records it writes, so a normal finish and a recovery produce the same totals.
 *
 * Messages: file_id, event, record, lap, session, activity (public FIT SDK profile). All definitions
 * are written right after the file_id, so every later message is a data message. Unknown values are
 * written as the FIT invalid value for the field, never 0. */
#ifndef CC700_FIT_H
#define CC700_FIT_H

#include <stddef.h>
#include <stdint.h>

#include "hal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_FIT_EPOCH 631065600u      /* 1989-12-31T00:00:00Z in Unix seconds */
#define CC_FIT_PROVISIONAL_BASE 946684800u /* 2000-01-01T00:00:00Z: base for rides without UTC */
#define CC_FIT_BUF 24576             /* RAM ring: about 5 minutes of records while storage fails */
#define CC_FIT_RESERVE 2048          /* kept free for events and laps when the buffer fills */

enum { CC_FIT_LAP_MANUAL = 0, CC_FIT_LAP_TIME = 1, CC_FIT_LAP_SESSION_END = 7 };

typedef struct {
    uint32_t ts;                /* Unix seconds (provisional if UTC unknown) */
    int has_pos;
    int32_t lat_e7, lon_e7;
    int has_alt;
    int32_t alt_cm;
    int has_speed;
    uint32_t speed_mms;
    uint32_t dist_cm;           /* always written */
    int has_hr;
    uint8_t hr;
    int has_cad;
    uint8_t cad;
    int has_power;
    uint16_t power;
} cc_fit_record_t;

/* Running totals over a span of records (a lap or the session). */
typedef struct {
    uint32_t start_ts, last_ts;
    uint32_t timer_ms;          /* time with the timer running */
    int timer_running;
    uint32_t timer_start_ts;
    uint32_t records;
    uint32_t dist_start_cm, dist_cm;
    uint64_t power_sum; uint32_t power_n; uint16_t power_max;
    uint64_t hr_sum; uint32_t hr_n; uint8_t hr_max;
    uint64_t cad_sum; uint32_t cad_n; uint8_t cad_max;
    uint32_t speed_max_mms;
    int alt_have; int32_t alt_ref_cm; uint32_t ascent_cm;
} cc_fit_acc_t;

/* Summary of a finished ride, as stored in .meta and shown on the Summary screen. */
typedef struct {
    uint32_t start_ts;          /* Unix seconds */
    uint32_t elapsed_s, timer_s;
    uint32_t distance_m;
    uint16_t avg_power, max_power; int has_power;
    uint8_t avg_hr, max_hr; int has_hr;
    uint8_t avg_cad; int has_cad;
    uint16_t ascent_m;
    uint32_t avg_speed_mms;
    uint32_t records;
    uint16_t laps;
} cc_fit_summary_t;

typedef struct {
    hal_fs_t *fs;
    char path[64];
    hal_file_t f;
    int open;
    uint8_t buf[CC_FIT_BUF];
    uint32_t buf_len;
    uint32_t file_pos;          /* bytes confirmed written to the file */
    uint32_t data_size;         /* message bytes after the header (written or buffered) */
    cc_fit_acc_t session, lap;
    uint16_t laps;
    int final_lap_written;
    uint32_t dropped_records;
    int io_error;               /* the last write or sync failed */
    uint32_t error_since_ms;    /* caller-set time of the first failure in the current run */
} cc_fit_t;

/* Creates path (which must not exist) and writes the header, file_id and definitions. 0 on success.
 * Even if the first write fails the writer keeps buffering (storage alert path). */
int cc_fit_begin(cc_fit_t *w, hal_fs_t *fs, const char *path, uint32_t start_ts);
void cc_fit_event_timer(cc_fit_t *w, uint32_t ts, int start); /* start=1 timer start, 0 timer stop */
void cc_fit_record(cc_fit_t *w, const cc_fit_record_t *r);
void cc_fit_lap(cc_fit_t *w, uint32_t ts, int trigger);       /* closes the current lap */
/* Writes the buffer and fsyncs. Returns 0 on success. */
int cc_fit_flush(cc_fit_t *w);
/* Closing messages (timer stop if running, final lap, session, activity), then flush. The ascent
 * override, when >= 0, replaces the ascent computed from record altitudes (the ride engine's
 * barometric ascent). Does not close the file: call cc_fit_finalise next. */
int cc_fit_close_messages(cc_fit_t *w, uint32_t end_ts, int32_t ascent_override_m);
/* Shifts times if offset != 0, writes the real header and the file CRC, fsyncs, closes and renames
 * to final_path (a new name; NULL leaves the file at its current path). Fills sum if non-NULL. Returns 0 on success. */
int cc_fit_finalise(cc_fit_t *w, int32_t time_offset, const char *final_path, cc_fit_summary_t *sum,
                    uint32_t *file_crc32, uint32_t *file_size);
void cc_fit_summary(const cc_fit_t *w, cc_fit_summary_t *s);

/* ---------------------------------------------------------------- reading */
/* Walks a FIT byte stream (definition-aware). Returns the offset just past the last complete message
 * (>= 14), or 0 if there is no valid header. Counts records and parses into acc if non-NULL. */
typedef struct {
    uint32_t end;               /* offset after the last complete message kept */
    uint32_t records;
    uint32_t laps;
    int session_seen;           /* the walk stopped at a session message (not kept) */
    int final_lap;              /* the last kept lap had trigger session_end */
    int timer_running;
    cc_fit_acc_t session, lap;
    uint32_t last_ts;
    uint32_t first_ts;
} cc_fit_walk_t;

int cc_fit_walk(const uint8_t *b, uint32_t len, int stop_at_session, cc_fit_walk_t *out);

/* Checks a finished file: header, header CRC, data size and file CRC. Returns 1 if valid. */
int cc_fit_check(const uint8_t *b, uint32_t len);

/* Adds offset seconds to every timestamp (253), start_time (lap and session field 2) and
 * time_created (file_id field 4) of the messages in b[14..end) in place. Returns the count changed. */
uint32_t cc_fit_shift_times(uint8_t *b, uint32_t end, int32_t offset);

/* Recovers a .part file into final_path: copies every complete message up to (not including) any
 * session message, rebuilds totals from them, writes the closing messages, applies the time offset and
 * finalises (final_path NULL leaves the result at work_path; sum->start_ts is then the corrected start). Returns 0 on success, 1 if the .part had no complete record (it is then deleted), and a
 * negative value on a storage error (the .part is left for the next boot). work_path must not exist. */
int cc_fit_recover(hal_fs_t *fs, const char *part_path, const char *work_path, const char *final_path,
                   int32_t time_offset, cc_fit_summary_t *sum, uint32_t *file_crc32, uint32_t *file_size);

uint16_t cc_fit_crc16(uint16_t crc, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif
#endif
