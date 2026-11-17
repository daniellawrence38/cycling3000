/* link_trace.c: protocol message names and lengths for the unit's log (link_trace.h). Pure C. */
#include "link_trace.h"

#include <string.h>

int fw_trace_kind(uint8_t op, uint8_t req)
{
    if (req == 0)
        return op >= 0xA0 ? 2 : 0;
    return (op & 0x80) ? 1 : 0;
}

const char *fw_trace_name(uint8_t op, uint8_t req)
{
    if (fw_trace_kind(op, req) == 2) {
        switch (op) {
        case 0xA0: return "EVENT_STATE";
        case 0xA1: return "EVENT_RIDE_SAVED";
        default: return "EVENT_?";
        }
    }
    switch (op & 0x7F) {
    case 0x01: return "HELLO";
    case 0x02: return "FTP_GET";
    case 0x03: return "FTP_SET";
    case 0x04: return "TIME_SET";
    case 0x10: return "OBJ_LIST";
    case 0x11: return "OBJ_PUT_BEGIN";
    case 0x12: return "OBJ_PUT_DATA";
    case 0x13: return "OBJ_PUT_END";
    case 0x14: return "OBJ_DELETE";
    case 0x20: return "RIDE_LIST";
    case 0x21: return "RIDE_OPEN";
    case 0x22: return "RIDE_READ";
    case 0x23: return "RIDE_MARK";
    case 0x24: return "RIDE_DELETE";
    default: return "?";
    }
}

int fw_trace_feed(fw_trace_t *t, const uint8_t *pkt, size_t n, fw_trace_msg_t *m)
{
    if (n < 1)
        return FW_TRACE_NONE;
    uint8_t h = pkt[0];
    int start = (h & 0x80) != 0, end = (h & 0x40) != 0;
    uint8_t ctr = h & 0x3F;
    const uint8_t *d = pkt + 1;
    size_t dn = n - 1;
    if (start) {
        /* a new START abandons any partial message, as cc_reasm_feed does */
        if (ctr != 0) {
            t->active = 0;
            return FW_TRACE_DROP;
        }
        memset(t, 0, sizeof *t);
        t->active = 1;
        t->status = -1;
    } else if (!t->active) {
        return FW_TRACE_DROP;
    } else if (ctr != t->next) {
        t->active = 0;
        return FW_TRACE_DROP;
    }
    /* Bytes 0 and 1 of the message are opcode and req_id; byte 2 is a response's status. */
    for (size_t i = 0; i < dn; i++) {
        uint32_t at = t->len + (uint32_t)i;
        if (at == 0)
            t->op = d[i];
        else if (at == 1)
            t->req = d[i];
        else if (at == 2 && fw_trace_kind(t->op, t->req) == 1)
            t->status = d[i];
    }
    t->len += (uint32_t)dn;
    t->pkts++;
    t->next = (uint8_t)((ctr + 1) & 0x3F);
    if (end) {
        t->active = 0;
        if (m) {
            m->op = t->op;
            m->req = t->req;
            m->status = t->status;
            m->len = t->len;
            m->pkts = t->pkts;
        }
        return FW_TRACE_DONE;
    }
    return FW_TRACE_NONE;
}
