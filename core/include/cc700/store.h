/* store.h: rides, objects and their files on top of hal_fs (brief 5.6, 5.9, protocol 7).
 *
 * Layout under the storage root `CC700/`:
 *   rides/<id>.fit.part   ride being recorded
 *   rides/<id>.time       i32 offset learned when UTC arrived mid-ride (written once)
 *   rides/<id>.fit.rec    work file of an interrupted recovery
 *   rides/<id>.fit        finished ride
 *   rides/<id>.meta       summary (written to .meta.tmp, renamed to the new name)
 *   rides/<id>.up         empty marker: uploaded (created and removed, never rewritten)
 *   objects/<idhex>.cw1 | .cr1 | .tmp
 *   config/settings.a|b, config/sensors.a|b
 * Nothing is renamed onto an existing name and nothing outside the root is touched. */
#ifndef CC700_STORE_H
#define CC700_STORE_H

#include <stdint.h>

#include "cc700/fit.h"
#include "hal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CC_RIDE_UPLOADED = 1, CC_RIDE_RECOVERED = 2, CC_RIDE_TIME_UNKNOWN = 4 };

typedef struct {
    uint32_t ride_id;
    uint8_t flags;              /* CC_RIDE_* (UPLOADED comes from the .up marker) */
    uint32_t size, crc32;       /* of the .fit file */
    cc_fit_summary_t sum;
} cc_ride_meta_t;

#define CC_MAX_RIDES_LISTED 240

void cc_ride_path(char *out, int cap, uint32_t ride_id, const char *ext);

int cc_ride_meta_write(hal_fs_t *fs, const cc_ride_meta_t *m);
int cc_ride_meta_read(hal_fs_t *fs, uint32_t ride_id, cc_ride_meta_t *m);

/* Boot: delete objects/<id>.tmp, recover every rides/<id>.fit.part (applying its .time offset), rebuild a
 * missing .meta from its .fit, remove leftovers. Returns the number of rides recovered. */
int cc_store_boot(hal_fs_t *fs);

/* Finishes the recording `w` for ride_id: closing messages, time offset, rename, .meta, removes .time.
 * new_id gets the final ride ID (corrected if offset != 0). Then runs retention. */
int cc_ride_finish(hal_fs_t *fs, cc_fit_t *w, uint32_t ride_id, uint32_t end_ts, int32_t ascent_m, int have_offset,
                   int32_t offset, cc_ride_meta_t *meta_out);
/* Stops and deletes the recording. */
int cc_ride_discard(hal_fs_t *fs, cc_fit_t *w, uint32_t ride_id);

/* Sorted ride IDs, oldest first. Returns the count (at most cap). */
int cc_rides_list(hal_fs_t *fs, uint32_t *ids, int cap);
int cc_ride_uploaded(hal_fs_t *fs, uint32_t ride_id);
int cc_ride_mark(hal_fs_t *fs, uint32_t ride_id, int uploaded);
int cc_ride_delete(hal_fs_t *fs, uint32_t ride_id);
/* Spec 10.4: above 100 rides delete the oldest uploaded; never an un-uploaded one unless above 240. */
int cc_rides_retention(hal_fs_t *fs);

/* Objects (workouts and routes) */
void cc_object_path(char *out, int cap, const uint8_t id[8], int kind); /* kind 1 workout, 2 route, 0 tmp */

#ifdef __cplusplus
}
#endif
#endif
