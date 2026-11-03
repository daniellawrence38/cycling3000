# CC700 phone–unit protocol and object formats

Version 1.1 · 3 November 2026 · matches spec 0.5

Status: **Proposed**. Designed on paper from the Bluetooth LE specifications; nothing has run on the unit. Throughput, MTU and concurrent-connection limits on this hardware are untested. Golden bytes for every format below are in `cc700-test-vectors.md` (section numbers cited as V§). Where this document and the vectors disagree, the vectors are authoritative for numbers and this document for intent; raise the conflict in `cc700-questions.md`.

Changes in 1.1 (logged in `cc700-questions.md`): stored percentages accepted up to 3150 (3.2); OBJ_PUT_DATA and RIDE_READ chunks up to 4000 bytes (4, 5, 6); RIDE_LIST reports up to 240 rides and a "time unknown" flag (4); storage rules for a FAT file system and the `CC700/` root (7). `HELLO.proto` stays 1, because nothing has been built against 1.0.

All multi-byte integers are little-endian. `u8/u16/u32` unsigned, `i16/i32` signed. Strings are ASCII (0x20 to 0x7E), length-prefixed, not terminated.

## 1. Roles

The unit is a BLE peripheral to the phone and a central to the power meter and heart-rate strap, so it holds up to three connections at once. Set the NimBLE connection limit to at least 4. The phone app is the central. The protocol is a request/response protocol started by the phone, with two optional unit-initiated events.

## 2. Transport

### 2.1 GATT

| Item | UUID | Properties | Security |
| --- | --- | --- | --- |
| CC700 service (primary) | `c7a1e6b0-4d52-4f0a-9c3e-0b5d7a2f8e11` | | |
| RX: phone to unit | `c7a1e6b1-4d52-4f0a-9c3e-0b5d7a2f8e11` | Write, Write Without Response | Encrypted, authenticated (passkey), bonded |
| TX: unit to phone | `c7a1e6b2-4d52-4f0a-9c3e-0b5d7a2f8e11` | Notify | CCCD write needs encrypted link |

Also expose Device Information (0x180A) with Model Number "CC700 Pro" and Firmware Revision. Nothing else is required.

### 2.2 Pairing and bonding

LE Secure Connections, MITM required, bonding on. The unit's I/O capability is DisplayOnly: it shows a random 6-digit passkey (screen 23, state "pairing") and the user types it into the phone. One bond only. The unit accepts a new pairing only during a 60 s pairing window opened from Menu, Phone, Pair, and only when no bond exists; otherwise it rejects pairing requests. Forgetting the phone (screen 19, "forgetphone") deletes the bond and returns the Phone screen to "not paired". While the window is closed, an unbonded central may connect but every characteristic access fails with Insufficient Authentication.

### 2.3 Advertising

Connectable undirected, 1 s interval, local name `CC700-` plus the last four hex digits of the Bluetooth address, the 128-bit service UUID in the scan response. Advertise whenever no phone is connected, including while recording.

### 2.4 Connection and MTU

The unit requests an ATT MTU of 247 and accepts whatever the phone grants. The phone must work at the default MTU of 23 (19 data bytes per packet). While idle the unit prefers a 100 to 200 ms interval with latency 4; during a transfer the phone should use the shortest interval it can get.

### 2.5 Packets and messages

A message is `opcode u8, req_id u8, payload`. Messages are at most 4096 bytes. A message is split into packets. A packet is one header byte plus data:

| Bit | Meaning |
| --- | --- |
| 7 | START: first packet of a message |
| 6 | END: last packet of a message (both bits set for a one-packet message) |
| 5 to 0 | Counter: 0 at START, +1 per packet, modulo 64 |

Data per packet is at most `ATT_MTU - 3 - 1` bytes. The phone sends packets with Write Without Response and keeps **one message in flight**: it waits for the response (or a 3 s timeout) before sending the next request. The unit answers with notifications on TX in the same framing. The receiver drops a partial message silently if it sees a counter mismatch, a packet without START when idle, or 2 s of silence between packets. No response is sent for a dropped message; the phone's timeout covers it. Worked examples: V§5.

### 2.6 Request and response

Requests have opcodes 0x01 to 0x7F. The response has opcode `request | 0x80`, echoes `req_id`, and its payload starts with a **status** byte. Unit-initiated events use opcodes 0xA0 and above, `req_id` 0 and no status byte. The phone chooses `req_id`; 0 is reserved.

| Status | Name | Meaning |
| --- | --- | --- |
| 0 | OK | Done |
| 1 | BAD_CRC | CRC-32 of the received object does not match |
| 2 | FULL | Library limit reached (20 workouts, 10 routes) or no free storage |
| 3 | BUSY | Refused: recording or paused, or another transfer is open |
| 4 | BAD_OBJECT | Structure or ID check failed |
| 5 | BAD_OPCODE | Unknown opcode |
| 6 | BAD_ARG | Payload length or a field is out of range |
| 7 | NOT_FOUND | No such object or ride |
| 8 | TOO_BIG | Size over the limit (workout 4,096 bytes, route 131,072 bytes) |
| 9 | EXISTS | An object with this ID is already stored; send no data |
| 10 | NO_SESSION | PUT_DATA or PUT_END without an open PUT_BEGIN |
| 11 | OFFSET | PUT_DATA offset is not the expected next offset |
| 12 | IO_ERROR | Storage failure on the unit |

## 3. Object formats

An object is what the unit stores. The phone builds it; the unit validates it and never changes it. Percentages are integer tenths of a percent (`p10`, 55.0 % is 550).

### 3.1 Common header (12 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 2 | Magic: `CW` (workout) or `CR` (route) |
| 2 | 1 | Format version: 1 |
| 3 | 1 | Flags: 0 |
| 4 | 8 | ID: first 8 bytes of SHA-256 over the whole object with these 8 bytes set to zero |

### 3.2 Workout, CW1

| Offset | Size | Field |
| --- | --- | --- |
| 12 | 4 | `total_secs` u32: sum of step seconds |
| 16 | 1 | `nsteps` u8: 1 to 100, after repeats are expanded |
| 17 | 1 | `name_len` u8: 1 to 40 |
| 18 | n | `name` |
| then, per step | | |
| +0 | 2 | `secs` u16: 5 to 43200 |
| +2 | 2 | `p0` u16: start of the range, in p10 |
| +4 | 2 | `p1` u16: end of the range, in p10 |
| +6 | 1 | `flags` u8: bit 0 ramp, other bits 0 |
| +7 | 1 | `rpm` u8: 0 for none, otherwise 40 to 130 |
| +8 | 1 | `rep_idx` u8: 1-based repetition, 0 if not in a group |
| +9 | 1 | `rep_total` u8: repetitions in the group, 0 if not in a group |
| +10 | 1 | `label_len` u8: 1 to 16 |
| +11 | n | `label` |

For a fixed step, `p0 < p1` is the range. For a ramp, `p0` and `p1` are the start and end targets (either may be larger, never equal). The phone derives them as in V§2.1 and V§2.4. Unit validation: magic and version; `nsteps` 1 to 100; the lengths consume the object exactly; every step in bounds; `p0` and `p1` in 10 to 3150 (a single 300.0 % target widens to 2850 to 3150, V§2.4); fixed steps have `p0 < p1`; ramps have `p0 != p1`; `sum(secs) == total_secs` and at most 86,400; the ID recomputes. Any failure returns BAD_OBJECT. Golden object: V§2.2 and V§2.3.

### 3.3 Route, CR1

| Offset | Size | Field |
| --- | --- | --- |
| 12 | 4 | `dist_m` u32: route length |
| 16 | 2 | `ascent_m` u16 |
| 18 | 2 | `npts` u16: 2 to 8000 |
| 20 | 2 | `nturns` u16: 0 to 500 |
| 22 | 1 | `nclimbs` u8: 0 to 60 |
| 23 | 2 | `nprof` u16: 0 to 12000 |
| 25 | 2 | `prof_step` u16: metres between profile samples (0 only if `nprof` is 0) |
| 27 | 1 | `name_len` u8: 1 to 40 |
| 28 | n | `name` |
| then | 8 | First point: `lat_e7` i32, `lon_e7` i32 (degrees times 10^7) |
| then | var | `npts - 1` deltas: for each, zigzag-varint latitude delta then zigzag-varint longitude delta, in 1e-7 degrees |
| then | `2*nprof` | Profile: elevation i16 in decimetres, sample `i` at `i * prof_step` m along the route |
| then, per turn | | `dist_m` u32 (from route start), `type` u8, `exit` u8, `street_len` u8, `street` |
| then, per climb | 12 | `start_m` u32, `length_m` u16, `ascent_m` u16, `avg_x10` i16, `max_x10` i16 |

Zigzag maps a signed value `n` to `(n << 1) ^ (n >> 63)`; a varint is 7 bits per byte, low group first, high bit set on all but the last byte (V§9 has a worked case). Turn types: 0 continue, 1 slight left, 2 left, 3 sharp left, 4 slight right, 5 right, 6 sharp right, 7 U-turn, 8 roundabout (`exit` is the exit number), 9 arrive, 10 depart. Climbs are sorted by `start_m` and do not overlap. The unit trusts `dist_m`, `ascent_m` and the climb list (the phone computes them) but validates counts, that every length is consumed exactly, that coordinates are within range, and the ID. It does not cross-check fields against each other (V§9).

## 4. Messages

Every payload below is the part after `opcode, req_id`. "R:" is the response payload, which always begins with the status byte.

| Op | Name | Request payload | R: payload after status | Notes |
| --- | --- | --- | --- | --- |
| 0x01 | HELLO | `proto u8` (1) | `proto u8, fw_major u8, fw_minor u8, fw_patch u8, battery_pct u8, state u8, workout_free u8, route_free u8` | `state`: 0 ready, 1 recording, 2 paused. Always answered. If the unit does not speak the requested protocol it returns BAD_ARG with its own `proto`. |
| 0x02 | FTP_GET | none | `ftp u16` | |
| 0x03 | FTP_SET | `ftp u16` (50 to 600) | none | BUSY unless state is 0. Recalculates the loaded workout. |
| 0x04 | TIME_SET | `unix u32, tz_min i16` | none | `tz_min` minutes east of UTC (Perth +480). Allowed in any state. The unit takes the clock from GNSS and uses `unix` only until it has GNSS time. During a ride recorded without UTC, the first TIME_SET supplies the offset that corrects the ride (spec 10.6). |
| 0x10 | OBJ_LIST | `kind u8` (1 workout, 2 route) | `count u8`, then per object: `id[8], size u32, name_len u8, name`, then kind 1: `total_secs u32, nsteps u8`; kind 2: `dist_m u32, ascent_m u16, nclimbs u8` | Allowed in any state. |
| 0x11 | OBJ_PUT_BEGIN | `kind u8, id[8], size u32, crc32 u32` | none | OK opens a session. EXISTS means stop, nothing to send. FULL, BUSY, TOO_BIG, BAD_ARG otherwise. Aborts any earlier open session. |
| 0x12 | OBJ_PUT_DATA | `offset u32, data[1..4000]` | `next_offset u32` | Offsets must be contiguous from 0. OFFSET returns the expected `next_offset` so the phone can resume. |
| 0x13 | OBJ_PUT_END | none | none | Checks length, CRC-32 (BAD_CRC), structure and ID (BAD_OBJECT), then commits atomically. |
| 0x14 | OBJ_DELETE | `kind u8, id[8]` | none | NOT_FOUND, BUSY. Deleting the loaded workout or route while stopped unloads it. |
| 0x20 | RIDE_LIST | none | `count u8` (at most 240), then per ride: `ride_id u32, size u32, flags u8, duration_s u32, distance_m u32` | `ride_id` is the start time as Unix seconds (provisional if flag bit 2 is set). `flags` bit 0 uploaded, bit 1 recovered after a power loss, bit 2 time unknown (spec 10.6). Normally at most 100 rides; up to 240 when un-uploaded rides are kept (spec 10.4). The largest response is 4084 bytes. |
| 0x21 | RIDE_OPEN | `ride_id u32` | `size u32, crc32 u32` | CRC-32 of the whole FIT file, computed when the ride is saved. |
| 0x22 | RIDE_READ | `ride_id u32, offset u32, len u16` (1 to 4000) | `data` | Short or empty data at the end of file. |
| 0x23 | RIDE_MARK | `ride_id u32, flags u8` | none | Bit 0 sets or clears the uploaded flag. |
| 0x24 | RIDE_DELETE | `ride_id u32` | none | |
| 0xA0 | EVENT_STATE | `state u8, battery_pct u8` | | Unit to phone, on change of state, and when battery crosses a 5 % step. |
| 0xA1 | EVENT_RIDE_SAVED | `ride_id u32` | | Unit to phone, after a ride is saved and the link is up. |

While state is 1 or 2 (recording or paused) the unit answers only HELLO, FTP_GET, TIME_SET and OBJ_LIST; everything else returns BUSY. A BUSY reply is never an error to retry in a loop: the app waits for the next EVENT_STATE or a HELLO that reports state 0.

Events are a convenience. The app must work without them by calling HELLO and RIDE_LIST on connect and after every ride it knows about.

**Chunk size.** The phone uses 4000-byte chunks for OBJ_PUT_DATA and RIDE_READ when the negotiated MTU is 185 or more (a 4000-byte chunk is 17 packets at an MTU of 247, V§5), and 512-byte chunks below that, so a chunk always arrives well inside the 3 s request timeout. A three-hour ride of about 350 KB then takes about 90 round trips instead of about 700. The unit accepts any length from 1 to 4000 in either case.

## 5. Sequences

**First pairing.** On the unit: Menu, Phone, Pair (opens the 60 s window and shows the passkey). In the app: Add unit, scan for the service UUID, select `CC700-xxxx`, connect, type the passkey, bond. The app then sends HELLO and stores the bonded address and unit name.

**Connect.** The app connects with auto-connect on the bonded address, requests MTU 247, enables notifications on TX, sends HELLO, then FTP_GET, TIME_SET, OBJ_LIST for both kinds and RIDE_LIST, and updates its mirror.

**Send a workout.** App validates and converts (spec 7.4), builds the object, computes its ID and CRC-32, then OBJ_PUT_BEGIN. On OK it sends OBJ_PUT_DATA in chunks (section 4, chunk size), waiting for each response, then OBJ_PUT_END. On EXISTS it stops and reports "already on the unit". On any failure it aborts by sending a fresh BEGIN later; the unit discards an open session after 15 s without a message and removes the temporary file. Routes use the same sequence.

**Set FTP.** FTP_SET; the app then calls FTP_GET and shows the value the unit returned.

**Ride sync.** RIDE_LIST; for each ride without the uploaded flag: RIDE_OPEN, then RIDE_READ in chunks (section 4), verify size and CRC-32, upload to Intervals.icu, and only after a 2xx response send RIDE_MARK with the uploaded bit. A ride with flag bit 2 (time unknown) is held until Daniel enters its date in the app, which shifts the FIT timestamps before upload. If the app dies between upload and RIDE_MARK, it must check Intervals.icu for an activity with the same start time (plus or minus 60 s) before uploading again (spec 9).

## 6. Timeouts and limits

| Item | Value |
| --- | --- |
| Reassembly gap | 2 s |
| Phone request timeout | 3 s (10 s for OBJ_PUT_END and RIDE_OPEN) |
| Open PUT session idle limit | 15 s |
| Library | 20 workouts, 10 routes, 100 rides (up to 240 while un-uploaded rides are kept) |
| Message size | 4096 bytes |
| PUT_DATA / RIDE_READ chunk | 1 to 4000 bytes (phone uses 4000 at MTU 185 or more, else 512) |
| Workout object | 4,096 bytes |
| Route object | 131,072 bytes |

Throughput target (not verified): a 60 KB route in under 15 s at MTU 247, and a 350 KB ride in under 60 s.

## 7. Storage rules for the unit

All paths are under the storage root `CC700/` on the separate storage (spec 7.2): `objects/` for workouts and routes, `rides/`, `config/`. Nothing outside `CC700/` is created, changed or deleted. Assume a FAT file system, where a rename cannot replace an existing file:

- A new stored object is written to `objects/<id hex>.tmp`, flushed and synced, then renamed to `objects/<id hex>.cw1` or `.cr1`. The final name never exists beforehand (an existing ID returns EXISTS at PUT_BEGIN), so the rename is safe, and a power loss leaves either no object or the complete one.
- Files that are replaced in place (settings, sensor bonds) use two alternating slots with a sequence number and CRC (engineering brief 5.9); they are never renamed.
- Delete leftover `.tmp` files at boot.

The storage layer is an interface (`cc_store`). Rides are written by the ride engine (engineering brief 5.6), not through this protocol.

## 8. Versioning

`HELLO.proto` is 1. A change that breaks existing messages needs a new `proto`. New optional trailing fields may be appended to payloads; receivers ignore trailing bytes they do not know. Unknown opcodes return BAD_OPCODE. Object format changes need a new version byte; a unit that does not know a version returns BAD_OBJECT.
