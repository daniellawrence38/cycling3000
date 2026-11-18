/* link_trace.h: names each protocol message crossing the phone link for the unit's log, from the packet
 * headers alone (protocol 2.5), so the log shows the type, req_id, status and length of every message
 * without its content. No ESP-IDF dependency; host-tested by core/tests/test_fw_link_trace.c.
 *
 * It only watches: the real reassembly is the core's (cc_reasm_feed). A packet sequence the core would
 * drop (counter mismatch, no START) is reported as a drop here as well, so the two agree in the log. */
#ifndef LINK_TRACE_H
#define LINK_TRACE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int active;
    uint8_t next;   /* expected counter */
    uint8_t op, req;
    int status;     /* first payload byte of a response, -1 otherwise or not yet seen */
    uint32_t len;   /* message bytes so far */
    uint16_t pkts;
} fw_trace_t;

typedef struct {
    uint8_t op, req;
    int status;     /* responses: the status byte; requests and events: -1 */
    uint32_t len;   /* the whole message: opcode, req_id and payload */
    uint16_t pkts;
} fw_trace_msg_t;

enum { FW_TRACE_NONE = 0, FW_TRACE_DONE = 1, FW_TRACE_DROP = -1 };

/* Feeds one packet. FW_TRACE_DONE when a message ended (m filled), FW_TRACE_DROP when the packet broke
 * the sequence (a partial message was abandoned, or a packet without START arrived while idle),
 * FW_TRACE_NONE otherwise. */
int fw_trace_feed(fw_trace_t *t, const uint8_t *pkt, size_t n, fw_trace_msg_t *m);

/* "HELLO", "RIDE_READ", "EVENT_STATE", ... or "?". A response (req_id not 0) is named by its request
 * (E7: req_id, not the opcode, tells an event from a response). */
const char *fw_trace_name(uint8_t op, uint8_t req);
/* 1 for a response (req_id not 0 and bit 7 set), 0 for a request, 2 for an event. */
int fw_trace_kind(uint8_t op, uint8_t req);
#endif
