/* settings.h: the settings model and the two-slot files (brief 5.9).
 *
 * A record is never renamed: each save writes the older (or invalid) of two slots and fsyncs it, so a
 * power cut at any byte leaves the previous record loadable. Sensor bonds use the same scheme. */
#ifndef CC700_SETTINGS_H
#define CC700_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include "hal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_SETTINGS_A "config/settings.a"
#define CC_SETTINGS_B "config/settings.b"
#define CC_SENSORS_A "config/sensors.a"
#define CC_SENSORS_B "config/sensors.b"

enum { CC_BRIGHT_AUTO, CC_BRIGHT_25, CC_BRIGHT_50, CC_BRIGHT_75, CC_BRIGHT_100, CC_BRIGHT_N };
enum { CC_SCREENOFF_NEVER, CC_SCREENOFF_30S, CC_SCREENOFF_1MIN, CC_SCREENOFF_5MIN, CC_SCREENOFF_N };
enum { CC_UNITS_KM, CC_UNITS_MI, CC_UNITS_N };

typedef struct {
    uint16_t ftp;
    uint8_t brightness;  /* CC_BRIGHT_* */
    uint8_t screen_off;  /* CC_SCREENOFF_* */
    uint8_t units;       /* CC_UNITS_* */
    uint8_t autopause;   /* 1 on */
    int16_t tz_min;      /* minutes east of UTC, set by the phone */
    uint8_t workout_id[8]; /* all zero: none loaded */
    uint8_t route_id[8];
    uint32_t ride_counter; /* provisional ride IDs (brief 5.6) */
} cc_settings_t;

/* Where the newest valid record is, so the next save goes to the other slot. */
typedef struct {
    int newest;   /* 0 = slot a, 1 = slot b, -1 = none valid */
    uint32_t seq;
} cc_slots_t;

void cc_settings_defaults(cc_settings_t *s);
/* Loads the newest valid record. Returns 1 if one was found, 0 if defaults were used (both files are
 * then left as they are for diagnosis and the next save goes to slot a). */
int cc_settings_load(hal_fs_t *fs, cc_settings_t *s, cc_slots_t *slots);
/* Returns 0 on success. */
int cc_settings_save(hal_fs_t *fs, const cc_settings_t *s, cc_slots_t *slots);

/* Encoded record (exposed for tests). Returns the length (39). */
size_t cc_settings_encode(const cc_settings_t *s, uint32_t seq, uint8_t *out);
int cc_settings_decode(const uint8_t *b, size_t len, cc_settings_t *s, uint32_t *seq);

/* ---------------------------------------------------------------- sensor bonds */
#define CC_MAX_BONDS 4
#define CC_BOND_NAME 24

typedef struct {
    uint8_t kind;       /* 1 power, 2 heart rate */
    uint8_t addr_type;
    uint8_t addr[6];
    char name[CC_BOND_NAME + 1];
} cc_bond_t;

typedef struct {
    uint8_t count;
    cc_bond_t b[CC_MAX_BONDS];
} cc_bonds_t;

int cc_bonds_load(hal_fs_t *fs, cc_bonds_t *b, cc_slots_t *slots);
int cc_bonds_save(hal_fs_t *fs, const cc_bonds_t *b, cc_slots_t *slots);

/* Wrapping sequence comparison: a newer than b. */
static inline int cc_seq_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

#ifdef __cplusplus
}
#endif
#endif
