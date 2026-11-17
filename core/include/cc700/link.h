/* link.h: packet framing and the protocol server (protocol document sections 2.5 to 7, brief 5.10).
 * The same code runs in simunit behind a TCP socket and in fw/ behind GATT. It stores through store.h
 * and reaches the ride engine only through the host callbacks below. */
#ifndef CC700_LINK_H
#define CC700_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "hal_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CC_LINK_MSG_MAX 4096
#define CC_LINK_CHUNK_MAX 4000
#define CC_LINK_GAP_MS 2000
#define CC_LINK_PUT_IDLE_MS 15000
#define CC_LINK_PROTO 1
#define CC_WO_OBJ_MAX 4096
#define CC_RT_OBJ_MAX 131072

enum {
    CC_OP_HELLO = 0x01, CC_OP_FTP_GET = 0x02, CC_OP_FTP_SET = 0x03, CC_OP_TIME_SET = 0x04,
    CC_OP_OBJ_LIST = 0x10, CC_OP_OBJ_PUT_BEGIN = 0x11, CC_OP_OBJ_PUT_DATA = 0x12, CC_OP_OBJ_PUT_END = 0x13,
    CC_OP_OBJ_DELETE = 0x14,
    CC_OP_RIDE_LIST = 0x20, CC_OP_RIDE_OPEN = 0x21, CC_OP_RIDE_READ = 0x22, CC_OP_RIDE_MARK = 0x23,
    CC_OP_RIDE_DELETE = 0x24,
    CC_EV_STATE = 0xA0, CC_EV_RIDE_SAVED = 0xA1,
};

enum {
    CC_ST_OK = 0, CC_ST_BAD_CRC = 1, CC_ST_FULL = 2, CC_ST_BUSY = 3, CC_ST_BAD_OBJECT = 4, CC_ST_BAD_OPCODE = 5,
    CC_ST_BAD_ARG = 6, CC_ST_NOT_FOUND = 7, CC_ST_TOO_BIG = 8, CC_ST_EXISTS = 9, CC_ST_NO_SESSION = 10,
    CC_ST_OFFSET = 11, CC_ST_IO_ERROR = 12,
};

/* ------------------------------------------------------------------ framing */
/* Splits a message into packets of at most data_cap + 1 bytes (data_cap = ATT_MTU - 3 - 1) and calls
 * emit for each. Returns the packet count. */
int cc_link_fragment(const uint8_t *msg, size_t len, size_t data_cap,
                     void (*emit)(void *ctx, const uint8_t *pkt, size_t len), void *ctx);

typedef struct {
    uint8_t buf[CC_LINK_MSG_MAX];
    size_t len;
    int active;
    uint8_t next_counter;
    uint32_t last_ms;
} cc_reasm_t;

void cc_reasm_init(cc_reasm_t *r);
/* Feeds one packet. Returns 1 when a message is complete (in r->buf, r->len), 0 otherwise. A partial
 * message is dropped silently on a counter mismatch, a packet without START when idle, more than
 * 4096 bytes, or 2 s of silence between packets. */
int cc_reasm_feed(cc_reasm_t *r, const uint8_t *pkt, size_t len, uint32_t now_ms);

/* ------------------------------------------------------------------ server */
typedef struct {
    int (*ride_state)(void *ctx);                 /* 0 ready, 1 recording, 2 paused */
    int (*battery_pct)(void *ctx);
    uint16_t (*get_ftp)(void *ctx);
    int (*set_ftp)(void *ctx, uint16_t ftp);       /* 0 on success */
    void (*set_time)(void *ctx, uint32_t unix_s, int16_t tz_min);
    void (*object_deleted)(void *ctx, int kind, const uint8_t id[8]); /* unload if loaded */
    void *ctx;
} cc_link_host_t;

typedef struct {
    hal_fs_t *fs;
    cc_link_host_t host;
    void (*tx)(void *ctx, const uint8_t *pkt, size_t len);
    void *tx_ctx;
    size_t data_cap;           /* ATT_MTU - 4 */
    cc_reasm_t rx;
    /* PUT session */
    int put_open;
    uint8_t put_kind;
    uint8_t put_id[8];
    uint32_t put_size, put_crc, put_next;
    uint32_t put_last_ms;
    hal_file_t put_f;
    /* response buffer */
    uint8_t out[CC_LINK_MSG_MAX];
    /* last state reported in EVENT_STATE */
    int ev_state, ev_batt5;
    /* Bumped once for every request that changed what the unit's lists show: an object committed by
     * OBJ_PUT_END, an object removed by OBJ_DELETE, a ride removed by RIDE_DELETE, or a ride's
     * uploaded flag set or cleared by RIDE_MARK. Only successful requests count. TIME_SET counts too,
     * because History shows each ride's local date computed with the time zone when the list is built,
     * and the phone sends the zone once per connection.
     *
     * WHY: the screen manager's list refresh reads and decodes every stored workout and route (up to
     * 131 KB each) and every ride's metadata. The firmware used to run it after every batch of
     * received packets, which during a route upload meant once per chunk, on the task that also owns
     * the link, LVGL and the buttons. A host compares this number with the one it last saw and
     * refreshes only when it has moved. It wraps; compare for inequality, never for order. */
    uint32_t lib_changes;
} cc_link_t;

void cc_link_init(cc_link_t *l, hal_fs_t *fs, const cc_link_host_t *host,
                  void (*tx)(void *ctx, const uint8_t *pkt, size_t len), void *tx_ctx, uint16_t att_mtu);
void cc_link_set_mtu(cc_link_t *l, uint16_t att_mtu);
/* One received packet. */
void cc_link_rx(cc_link_t *l, const uint8_t *pkt, size_t len, uint32_t now_ms);
/* Expires an idle PUT session (15 s). Call at least once a second. */
void cc_link_tick(cc_link_t *l, uint32_t now_ms);
/* Handles one complete message and writes the response message (opcode, req_id, status, ...) to out.
 * Returns the response length (0 for none). Exposed for tests. */
size_t cc_link_handle(cc_link_t *l, const uint8_t *msg, size_t len, uint32_t now_ms, uint8_t *out);
/* Unit-initiated events. */
void cc_link_event_state(cc_link_t *l, int state, int battery_pct);
void cc_link_event_ride_saved(cc_link_t *l, uint32_t ride_id);
/* Sends EVENT_STATE if the state changed or the battery crossed a 5 % step since the last one. */
void cc_link_poll_events(cc_link_t *l);

/* Library counts (for HELLO and the Menu). */
int cc_objects_count(hal_fs_t *fs, int kind);

#ifdef __cplusplus
}
#endif
#endif
