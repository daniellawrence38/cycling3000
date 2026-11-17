/* nrf_proto.h: the framing and the decoded sensor messages of the nRF co-processor link
 * (docs/NRF-PROTOCOL.md). No ESP-IDF dependency; host-tested by core/tests/test_nrf_proto.c against
 * frames copied byte for byte from real captures.
 *
 * Outbound commands this file can build (all kind 2, 16 bytes; which ones are actually SENT, and when,
 * is the owner's decision in docs/QUESTIONS.md Q38, amendment of 17 November 2026, and is listed in
 * hal/hal_nrf.h):
 *   - SendQuerySlaveModeCmd (0x42055ca8): id 1,  24 01 ff ff 00 ff ff ff
 *   - SendPowerOffCmd       (0x42055bac): id 16, e2 02 00 00 00 00 00 00
 *   - SendPowerOnCmd        (0x42055c00): id 16, e2 02 00 00 00 01 00 00
 *   - GNSS power/reset (0x42155f84..0x42155fac): id 16, e2 02 07 00 00 <00=off|01=on|02=reset> 00 00
 *     (the "off" frame is built and pinned by the tests but is NOT sent: Q38 does not approve it) */
#ifndef FW_NRF_PROTO_H
#define FW_NRF_PROTO_H
#include <stddef.h>
#include <stdint.h>

#define FW_NRF_SYNC 0xA5
#define FW_NRF_SYNC2 0x6F
#define FW_NRF_DIR_OUT 0xF1
#define FW_NRF_MAX_LEN 132 /* the stock parser rejects a length byte above this */
#define FW_NRF_OVERHEAD 8  /* six header bytes and two of CRC */
#define FW_NRF_KIND_QUERY 1
#define FW_NRF_KIND_CMD 2
#define FW_NRF_POWER_OFF_FRAME_LEN 16
#define FW_NRF_CMD_FRAME_LEN 16

#define FW_NRF_GNSS_OFF 0
#define FW_NRF_GNSS_ON 1
#define FW_NRF_GNSS_RESET 2

typedef struct {
    uint8_t kind;           /* 1..5 */
    uint8_t id;
    const uint8_t *payload; /* points into the caller's buffer */
    size_t plen;
} fw_nrf_frame_t;

/* CRC-16/XMODEM: polynomial 0x1021, initial value 0, no reflection, no final XOR. */
uint16_t fw_nrf_crc16(const uint8_t *p, size_t n);

/* Builds one frame in out (needs plen + FW_NRF_OVERHEAD bytes), matching the stock builder at
 * 0x4205062c byte for byte. Returns the frame length (plen + 8), or -1 if kind/plen/cap is invalid. */
int fw_nrf_build_frame(uint8_t kind, uint8_t id, const uint8_t *payload, size_t plen, uint8_t *out, size_t cap);

/* Builds the 16-byte SendPowerOffCmd frame (0x42055bac: kind 2, id 16, payload e2 02 00 00 00 00 00 00,
 * Q38). Returns FW_NRF_POWER_OFF_FRAME_LEN (16). */
int fw_nrf_build_power_off(uint8_t out[FW_NRF_POWER_OFF_FRAME_LEN]);

/* Builds the 16-byte SendPowerOnCmd frame (0x42055c00: kind 2, id 16, payload e2 02 00 00 00 01 00 00). */
int fw_nrf_build_power_on(uint8_t out[FW_NRF_CMD_FRAME_LEN]);

/* Builds the 16-byte GNSS power/reset frame (0x42155f84..0x42155fac: kind 2, id 16,
 * payload e2 02 07 00 00 <mode> 00 00, where mode is FW_NRF_GNSS_OFF/ON/RESET). */
int fw_nrf_build_gnss_power(uint8_t out[FW_NRF_CMD_FRAME_LEN], uint8_t mode);

/* Builds the 16-byte SendQuerySlaveModeCmd frame (0x42055ca8: kind 2, id 1, payload
 * 24 01 ff ff 00 ff ff ff, the constant at 0x3c375e70; docs/NRF-PROTOCOL.md, "The senders, exactly"). */
int fw_nrf_build_query_slave_mode(uint8_t out[FW_NRF_CMD_FRAME_LEN]);

/* Returns 1 if f is the nRF power-on event (id 16, sub-tag 0xE2 0x02, payload[5] == 5 or 6, matching
 * ArmSendPowerOnSysEvent at 0x420560f4 and confirmed in outputs/nrf-capture-2.txt). */
int fw_nrf_is_power_on_event(const fw_nrf_frame_t *f);

/* The power event's code (payload[5]) if f is an id 16 / e2 02 frame, else -1. Like the vendor
 * (0x420560f4) it does not look at the device byte. The codes that mean something (RE-FINDINGS-2 2.1):
 *   4    SYS_EVENT_DEVICE_OFF_CHARGE: the unit was woken by the charger while off. NOT a manual power-on.
 *   5, 6 SYS_EVENT_POWER_ON_MANUAL: switched on by hand (5 seen on the wire, capture 2).
 * Anything else: the vendor posts no event. */
#define FW_NRF_PWR_EV_CHARGER 4
#define FW_NRF_PWR_EV_MANUAL 5
#define FW_NRF_PWR_EV_MANUAL2 6
int fw_nrf_power_event_code(const fw_nrf_frame_t *f);

/* id 1, sub-tag 0x24, payload[1] == 1: the reply to SendQuerySlaveModeCmd. payload[5] is the slave
 * mode the vendor stores at 0x3fca4bc4 (handler 0x42050fa4 -> 0x4214f030): 1 = running its
 * application ("Get Slave Work inApp"), 0 = in its bootloader. Returns the mode (0..255) or -1 if f is
 * not that reply. The vendor keeps querying while the stored byte is 2 or more. Never seen on the wire:
 * nothing has ever sent the query. */
#define FW_NRF_ID_SLAVE 1
#define FW_NRF_SUB_SLAVE 0x24
#define FW_NRF_SLAVE_BOOTLOADER 0
#define FW_NRF_SLAVE_APP 1
int fw_nrf_parse_slave_mode(const fw_nrf_frame_t *f);

/* id 1, sub-tag 0x01: the nRF's firmware version, sent when its sensor side powers on
 * (`01 01 00 00 00 00 00 01 04 11` in capture 2). The vendor (0x42050fa4) shows it as
 * %d.%d%02d from payload[9] / 10, payload[9] % 10, payload[8]: 1.704 for that frame. Returns 0 and the
 * three parts, or -1. For the log only. */
typedef struct {
    uint8_t major, minor, patch;
} fw_nrf_version_t;
int fw_nrf_parse_version(const fw_nrf_frame_t *f, fw_nrf_version_t *out);

/* id 16, sub-tag 0xF0 0x02: the charge state. The vendor posts payload[2] as event 3 -> app message
 * 0x14, and its factory screen shows "Chargeing" when that is 0 and "No Charge" otherwise (0x42072cb4,
 * RE-FINDINGS-2 3.4). Returns 1 charging, 0 not charging, -1 not this frame.
 * UNVERIFIED: on the wire it has only ever been f0 02 00 ..., always with the USB cable in (captures of
 * 4 and 17 November). Nobody has unplugged the cable during a capture, so "non-zero = not charging" is
 * the vendor's screen text, not an observation. */
#define FW_NRF_SUB_CHARGE 0xF0
int fw_nrf_parse_charge(const fw_nrf_frame_t *f);

/* ---------------------------------------------------------------- the boot handshake
 *
 * What the vendor does at power-on (ArmStateProcess 0x420563c0, called every 10 ms; RE-FINDINGS-2 2.2):
 * state 0 sends SendQuerySlaveModeCmd every 200 ms until the id 1 reply says "in application", then
 * state 1 sends SendPowerOnCmd about every 100 ms until the nRF posts a power event (4, 5 or 6). The
 * vendor never gives up: it reboots after 10 minutes without a query reply, and waits for the power
 * event for ever. Q38 (amendment, 17 November 2026) approves both frames; it does not approve a reboot,
 * and an unbounded transmit is not something to leave running on a co-processor that owns the power.
 *
 * So this is the same sequence with every wait bounded, and a FAILED state that transmits nothing and
 * keeps listening, so the unit still does whatever it can receive-only (which is how it ran until
 * 17 November: buttons, sensors and the nRF's own power-on by a held power key all work without a
 * single frame sent; capture 2).
 *
 *   QUERY     waits for the first verified frame from the nRF (nothing is sent into a link that has
 *             never been heard), then sends the query every FW_NRF_HS_QUERY_PERIOD_MS.
 *             reply mode 1 -> POWER_ON; mode 0 -> FAILED (BOOTLOADER), nothing more is ever sent and no
 *             upgrade of any kind is attempted; FW_NRF_HS_QUERY_MAX_MS after init with no reply ->
 *             FAILED (NO_REPLY).
 *   POWER_ON  sends SendPowerOnCmd every FW_NRF_HS_POWER_ON_PERIOD_MS until a power event arrives
 *             (any of 4, 5, 6, as the vendor's loop) -> RUNNING; or sensor frames (id 16 / 0xF1) have
 *             been seen and FW_NRF_HS_SENSOR_GRACE_MS has passed in this state -> RUNNING; after
 *             FW_NRF_HS_POWER_ON_MAX_MS -> FAILED (NO_POWER_EVENT).
 *   RUNNING   the handshake sends nothing, except GNSS power-on ONCE per boot (Q38: kept for now, to
 *             be tested), and only once the nRF's sensor side is known to be on: a manual power event
 *             (5/6) or sensor frames. After code 4 alone (woken by the charger: the vendor shows its
 *             charging screen and powers nothing) it waits for one of those.
 *   FAILED    sends nothing. From NO_REPLY or NO_POWER_EVENT, positive evidence that the nRF is on (a
 *             manual power event, or sensor frames) moves it to RUNNING without a frame being sent, so
 *             GNSS power-on still happens if the rider switches the unit on with the power key. From
 *             BOOTLOADER and STOPPED nothing moves it.
 *
 * Why each number:
 *   200 ms   the vendor's query cadence: every 20th pass of the 10 ms loop (divisibility test at
 *            0x42056462).
 *   10 s     QUERY cap. The nRF sends a frame at least every 300 ms whenever it has power, even in its
 *            "off" state (capture 2: battery and f0 02 every 300 ms from the first second of the
 *            log), so a nRF that answers the query at all answers within a few 200 ms periods. 10 s is
 *            50 queries and five times the 2 s the vendor itself waits (200 passes) before acting on a
 *            "bootloader" answer. It also ends well inside the 20 s first baud-search window at 115200.
 *   100 ms   the vendor's power-on cadence: the 100 ms wait for a system event in state 1, looping back
 *            to the send at 0x420565c1 (branch at 0x42056765).
 *   5 s      POWER_ON cap: 50 frames. In capture 2 the power event, the nRF's version message and the
 *            first barometer frame all arrived in the same 10 ms log tick as the key release that
 *            switched the sensor side on (64679 ms): the nRF announces a power change at once. Five
 *            seconds without either a power event or a sensor frame means this frame is not going to
 *            produce one. (The vendor's own fallback, SendCheckPowerOnReasonCmd after about 30 s, is
 *            not approved and not sent.)
 *   1 s      sensor grace: if the sensors are already streaming (the nRF was on before this boot, as in
 *            the 4 November captures), give the nRF ten power-on periods to answer with its reason
 *            before taking the sensor frames as the confirmation. In capture 2 no id 16 / 0xF1 frame
 *            arrived in the 64 s of the "off" state and they started with the power event, so sensor
 *            frames are evidence that the sensor side is on. */
#define FW_NRF_HS_QUERY_PERIOD_MS 200u
#define FW_NRF_HS_QUERY_MAX_MS 10000u
#define FW_NRF_HS_POWER_ON_PERIOD_MS 100u
#define FW_NRF_HS_POWER_ON_MAX_MS 5000u
#define FW_NRF_HS_SENSOR_GRACE_MS 1000u

typedef enum {
    FW_NRF_HS_QUERY = 0,
    FW_NRF_HS_POWER_ON,
    FW_NRF_HS_RUNNING,
    FW_NRF_HS_FAILED,
} fw_nrf_hs_state_t;

typedef enum {
    FW_NRF_HS_FAIL_NONE = 0,
    FW_NRF_HS_FAIL_NO_REPLY,       /* no slave-mode reply within FW_NRF_HS_QUERY_MAX_MS */
    FW_NRF_HS_FAIL_BOOTLOADER,     /* the nRF says it is in its bootloader */
    FW_NRF_HS_FAIL_NO_POWER_EVENT, /* no power event and no sensors within FW_NRF_HS_POWER_ON_MAX_MS */
    FW_NRF_HS_FAIL_STOPPED,        /* stopped by fw_nrf_hs_stop (power-off) */
} fw_nrf_hs_fail_t;

/* How RUNNING was reached, i.e. what the nRF said about why it is on. */
typedef enum {
    FW_NRF_HS_ON_NONE = 0,
    FW_NRF_HS_ON_MANUAL,  /* power event 5 or 6 */
    FW_NRF_HS_ON_CHARGER, /* power event 4: woken by the charger */
    FW_NRF_HS_ON_SENSORS, /* no event; sensor frames seen */
} fw_nrf_hs_on_t;

/* What fw_nrf_hs_step asks the caller to send now. At most one per call. */
#define FW_NRF_HS_TX_NONE 0
#define FW_NRF_HS_TX_QUERY 1    /* fw_nrf_build_query_slave_mode */
#define FW_NRF_HS_TX_POWER_ON 2 /* fw_nrf_build_power_on */
#define FW_NRF_HS_TX_GNSS_ON 3  /* fw_nrf_build_gnss_power(FW_NRF_GNSS_ON) */

typedef struct {
    fw_nrf_hs_state_t state;
    fw_nrf_hs_fail_t fail;
    fw_nrf_hs_on_t on;
    uint32_t init_ms, state_ms, last_tx_ms;
    uint16_t queries, power_ons; /* frames asked for, for the log and the tests */
    uint8_t heard;               /* a verified frame has arrived */
    uint8_t sensors;             /* an id 16 / 0xF1 frame has arrived */
    uint8_t manual;              /* a power event 5 or 6 has arrived at some point */
    uint8_t ev_in_state;         /* a power event (4, 5, 6) arrived while in POWER_ON */
    int16_t slave_mode;          /* last reported mode, -1 none */
    uint8_t sent_any;            /* the current state has asked for a frame */
    uint8_t gnss_on;             /* GNSS power-on has been asked for (once per boot) */
} fw_nrf_hs_t;

void fw_nrf_hs_init(fw_nrf_hs_t *hs, uint32_t now_ms);
/* Advances the handshake. f is a verified frame that has just arrived, or NULL for a timing-only call.
 * Returns FW_NRF_HS_TX_*: the one frame to send now. Call it for every frame and regularly (at least
 * every FW_NRF_HS_POWER_ON_PERIOD_MS / 2 for the cadence to hold). */
int fw_nrf_hs_step(fw_nrf_hs_t *hs, uint32_t now_ms, const fw_nrf_frame_t *f);
/* Ends the handshake for good (power-off): FAILED / STOPPED, nothing more is ever asked for. */
void fw_nrf_hs_stop(fw_nrf_hs_t *hs);
const char *fw_nrf_hs_state_name(fw_nrf_hs_state_t s);
const char *fw_nrf_hs_fail_name(fw_nrf_hs_fail_t f);
const char *fw_nrf_hs_on_name(fw_nrf_hs_on_t o);

/* ---------------------------------------------------------------- power-off and the wake decision
 *
 * The vendor's power-off (RE-FINDINGS-2 2.4): close the open files (state 15), then in state 17, every
 * pass, a tone, 1000 ms, SendPowerOffCmd, 500 ms: the frame repeats about every 1.5 s until the power
 * goes. No GNSS-off frame on that path. Approved in Q38.
 *
 * On USB the ESP32 keeps its power after the frame, so the unit must be able to come back. The old
 * code woke on any "power down" edge after a 1.2 s pause, and the key decoder releases a button whose
 * HELD repeats stop for a second (FW_NRF_KEY_LOST_MS), so a hold that paused and resumed produced a
 * fresh "down" edge from the SAME hold that asked for the power-off: a spurious wake. The decision is
 * therefore made here, from the raw key frames, with no timeout that can invent a release:
 *
 *   - the hold that requested the power-off has to be seen to END first: a HOLD_END (5) or a CLICK (1)
 *     of key 0, after the power-off was sent (or already the last key-0 frame before it was sent);
 *     HELD frames before that never wake, however long they pause;
 *   - then a new press of key 0 wakes: HELD (4) or CLICK (1);
 *   - a power event 5 or 6 wakes too, but only once that hold has ended and not within
 *     FW_NRF_WAKE_SAME_HOLD_MS of its end. In capture 2 the nRF's power-on event came in the same 10 ms
 *     tick as the HOLD_END of the hold that caused it (64679 ms), so an event that close to the release
 *     belongs to that hold; a genuinely new switch-on is a new hold, whose HELD frames wake anyway;
 *   - code 4 (woken by the charger) never wakes: it is reported so the caller can log it.
 * A HOLD_END after the release is not a wake either: it is not a press, and it could be a repeat. */
#define FW_NRF_POWER_OFF_PERIOD_MS 1500u /* state 17: 1000 ms + 500 ms per pass */
/* After this many power-off frames without the power dropping (45 s), the unit is on USB or the nRF is
 * not acting on the frame; a 31st frame will not change that, so stop sending and just wait for a wake. */
#define FW_NRF_POWER_OFF_MAX_SENDS 30
#define FW_NRF_WAKE_SAME_HOLD_MS 500u

#define FW_NRF_WAKE_NONE 0
#define FW_NRF_WAKE_KEY 1           /* wake: a fresh press of the power key */
#define FW_NRF_WAKE_EVENT 2         /* wake: a manual power-on event (5 or 6) */
#define FW_NRF_WAKE_CHARGER 3       /* not a wake: power event 4, log it */
#define FW_NRF_WAKE_EVENT_IGNORED 4 /* not a wake: a 5/6 event before or right after the hold ended */
#define FW_NRF_WAKE_RELEASED 5      /* not a wake: the requesting hold has now been seen to end */

typedef struct {
    uint8_t released;     /* the hold that requested the power-off has been seen to end */
    uint32_t released_ms; /* when */
} fw_nrf_wake_t;
/* last_key0_event is the event code of the last key-0 frame received before the power-off frame was
 * sent (0 if none) and last_key0_ms its arrival time. */
void fw_nrf_wake_init(fw_nrf_wake_t *w, uint8_t last_key0_event, uint32_t last_key0_ms);
/* Feeds one verified frame received after the power-off frame was sent. Returns FW_NRF_WAKE_*. */
int fw_nrf_wake_feed(fw_nrf_wake_t *w, const fw_nrf_frame_t *f, uint32_t now_ms);

/* ---------------------------------------------------------------- the GNSS silence watchdog
 *
 * The vendor (GpsPthreadMachine state 15, RE-FINDINGS-2 1.4) counts 100 ms passes with no byte from the
 * receiver and at 51 of them (about 5.1 s) sends ONE hardware-reset frame (e2 02 07 00 00 02); no
 * power-on follows (GpsHdRst 0x421b8694). It then counts again, so a silent receiver is reset about
 * every 5 s, for ever as decompiled. Approved in Q38: the reset only.
 *
 * Ours: armed when the GNSS power-on frame has been sent (before that the rail may be off and a reset
 * means nothing); after FW_GNSS_WD_SILENCE_MS without a byte (since arming, the last byte or the last
 * reset) it asks for a reset; at most FW_GNSS_WD_MAX_RESETS per boot. 3 is the vendor's own limit: its
 * reset counter goes to GPSIC_DEAD_STATUS after 3 resets. As decompiled that state looks unreachable,
 * because the counter is cleared by every byte and every re-registration, but it is what the vendor
 * meant, and a receiver that has not spoken after three hardware resets will not speak after a fourth.
 * Unlike the vendor, a byte does not give back the resets: the cap is per boot. */
#define FW_GNSS_WD_SILENCE_MS 5100u
#define FW_GNSS_WD_MAX_RESETS 3

#define FW_GNSS_WD_NONE 0
#define FW_GNSS_WD_RESET 1   /* send the GNSS reset frame now */
#define FW_GNSS_WD_GAVE_UP 2 /* returned once, when the cap is reached and the receiver is still silent */

typedef struct {
    uint8_t armed;
    uint8_t resets;
    uint8_t gave_up;
    uint32_t quiet_since_ms;
} fw_gnss_wd_t;
void fw_gnss_wd_init(fw_gnss_wd_t *w);
void fw_gnss_wd_arm(fw_gnss_wd_t *w, uint32_t now_ms);
void fw_gnss_wd_bytes(fw_gnss_wd_t *w, uint32_t now_ms);
int fw_gnss_wd_tick(fw_gnss_wd_t *w, uint32_t now_ms);

/* Tries to take one frame from the front of buf.
 * Returns the bytes consumed (> 0) and fills *out; FW_NRF_MORE if the frame is not complete yet;
 * FW_NRF_NOT_FRAME or FW_NRF_BAD_CRC if buf does not start a valid frame, in which case the caller
 * drops ONE byte and tries again, as the stock parser does. */
#define FW_NRF_MORE 0
#define FW_NRF_NOT_FRAME (-1)
#define FW_NRF_BAD_CRC (-2)
int fw_nrf_take_frame(const uint8_t *buf, size_t len, fw_nrf_frame_t *out);

/* id 16, kind 4, sub-tag 0xF1: the sensor transport. Payload byte 1 selects the sensor. */
#define FW_NRF_ID_SENSOR 16
#define FW_NRF_KIND_STREAM 4
#define FW_NRF_SUB_SENSOR 0xF1
#define FW_NRF_SENSOR_ACCEL 0x01
#define FW_NRF_SENSOR_BARO 0x03

typedef struct {
    int16_t temp_c100;     /* hundredths of a degree Celsius */
    uint32_t pressure_cpa; /* hundredths of a pascal */
} fw_nrf_baro_t;

/* The barometer message. Returns 0 and fills *out, or -1 if the frame is not one, or if its pressure
 * is outside FW_NRF_BARO_MIN_PA..FW_NRF_BARO_MAX_PA. The range is the usual 300 to 1100 hPa of a MEMS
 * barometer (about 9000 m to below sea level): a value outside it means the message is not what the
 * capture says it is, and no altitude is better than a wrong one (Q19). */
#define FW_NRF_BARO_MIN_PA 30000
#define FW_NRF_BARO_MAX_PA 110000
int fw_nrf_parse_baro(const fw_nrf_frame_t *f, fw_nrf_baro_t *out);
/* Pressure in whole pascals, rounded half up. */
int32_t fw_nrf_baro_pa(const fw_nrf_baro_t *b);

typedef struct {
    int16_t x, y, z; /* about 1024 counts per g; axis order against the case is Unverified */
} fw_nrf_accel_t;

/* The accelerometer message. Returns 0 and fills *out, or -1. Not used by v1 (rule 4). */
int fw_nrf_parse_accel(const fw_nrf_frame_t *f, fw_nrf_accel_t *out);

/* The messages below are read out of the stock firmware's own handlers with Ghidra
 * (docs/NRF-PROTOCOL.md, "What the vendor's handlers say"). The layouts are the vendor's; what has
 * and has not been seen on the wire is said for each. */

/* id 16, sub-tag 0x49: a key event. The vendor handler (0x42051200) copies payload[1] as the key
 * index and payload[6] as the event code into the record its key queue reader logs as
 * "Index:%d Evt:%d", and keeps payload[5] beside them.
 * Confirmed on the unit, 17 November 2026 (outputs/nrf-capture-2.txt): index 0 is the power button,
 * 1 the bottom left, 2 the bottom right. A short press is ONE frame with event 1, sent when the
 * button is let go. A held button sends event 4 every 300 ms while it stays down and event 5 when it
 * is released. The nRF reports clicks, not edges. */
#define FW_NRF_SUB_KEY 0x49
#define FW_NRF_KEY_CLICK 1
#define FW_NRF_KEY_HELD 4
#define FW_NRF_KEY_HOLD_END 5
#define FW_NRF_KEYS 3
typedef struct {
    uint8_t index; /* which key */
    uint8_t event; /* the vendor's event code */
    uint8_t extra; /* payload[5]; purpose unknown */
} fw_nrf_key_t;
int fw_nrf_parse_key(const fw_nrf_frame_t *f, fw_nrf_key_t *out);

/* Turning the nRF's clicks into the down and up edges the core's hold detection wants (brief 6.4).
 *
 *   click (1)     -> down, then up at once: a short press
 *   held (4)      -> down at the first one; the repeats only say the button is still down
 *   hold end (5)  -> up
 *
 * If the repeats stop without a hold end (a lost frame, or the nRF restarting), the button is
 * released after FW_NRF_KEY_LOST_MS, so a button can never stay down for ever. The repeats come every
 * 300 ms, so one second is three missed in a row. */
#define FW_NRF_KEY_LOST_MS 1000u
typedef struct {
    uint8_t key;  /* 0 power, 1 left, 2 right */
    uint8_t down; /* 1 pressed, 0 released */
} fw_nrf_edge_t;
typedef struct {
    uint8_t held[FW_NRF_KEYS];
    uint32_t seen_ms[FW_NRF_KEYS];
} fw_nrf_keys_t;
/* Feeds one key event. Writes up to two edges to out and returns how many. */
int fw_nrf_keys_feed(fw_nrf_keys_t *k, const fw_nrf_key_t *ev, uint32_t now_ms, fw_nrf_edge_t out[2]);
/* Call regularly. Writes a release for each held button whose repeats have stopped; returns how many. */
int fw_nrf_keys_tick(fw_nrf_keys_t *k, uint32_t now_ms, fw_nrf_edge_t out[FW_NRF_KEYS]);

/* id 0, sub-tag 0x53: the nRF's calendar clock. The vendor handler (0x42050d78) reads seconds,
 * minutes, hours, day of month (low five bits), month from 0 and year from 1900, and range-checks
 * them. Seen on the wire: the 4 November 2026 capture decodes to 2026-11-04. Which time zone it keeps
 * is Unverified. */
#define FW_NRF_ID_STATUS 0
#define FW_NRF_SUB_CLOCK 0x53
typedef struct {
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} fw_nrf_clock_t;
int fw_nrf_parse_clock(const fw_nrf_frame_t *f, fw_nrf_clock_t *out);

/* id 0, sub-tag 0x52: the battery. The vendor handler passes on payload[6] as a byte and
 * payload[4..5] as a uint16. Confirmed on the unit (outputs/nrf-capture-2.txt): 3477 mV and 2 %
 * rising on charge, and 4290 mV and 100 % when full. */
#define FW_NRF_SUB_BATTERY 0x52
typedef struct {
    uint16_t millivolts;
    uint8_t percent;
} fw_nrf_battery_t;
int fw_nrf_parse_battery(const fw_nrf_frame_t *f, fw_nrf_battery_t *out);

/* id 16, sub-tag 0xF1, sensor 0x04: the ambient light sensor. The vendor handler runs the uint32 at
 * payload[2..5] through a threshold table to pick a level (0x420511ac). Confirmed on the unit
 * (outputs/nrf-capture-2.txt): 0 to 30 covered, 600 in room light, 6000 to 9000 under a torch. */
#define FW_NRF_SENSOR_LIGHT 0x04
typedef struct {
    uint32_t value; /* payload[2..5] */
    uint16_t aux;   /* payload[6..7]; purpose unknown */
} fw_nrf_light_t;
int fw_nrf_parse_light(const fw_nrf_frame_t *f, fw_nrf_light_t *out);

/* Maps the nRF light sensor's banded value to a backlight percentage (1..100) using the stock
 * firmware's threshold table at 0x3c36eb7c (FUN_420511ac). Returns 60 when lux < 0 (no light sensor
 * reading, spec 10.1). */
int fw_nrf_light_backlight_pct(int32_t lux);
#endif
