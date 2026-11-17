# The nRF co-processor link ("SPU")

Everything here is read out of the stock firmware in `outputs/esp32s3-backup/original-flash-16MB.bin`
by static analysis. **The unit was not involved in producing any of it.** Addresses are virtual
addresses in the `ota_0` application image; the method — find a string, find the `l32r` that loads it,
read the code around it — is described in `BRINGUP-LOG.md`.

**How to read this document.** It is a record in the order things were learned, so later sections
correct earlier ones. The first part (down to "Confirmed on the wire") was written before any capture
and is kept as written; the framing in it was then confirmed on the unit (115200 baud, over 200,000
frames, no CRC reject), and two of its statements were wrong and are corrected below (header byte 3
is a constant, not a direction; id 16 is not only "the power channel"). The current sensor map is the
last part, "The sensor map", and the code that implements it is `fw/main/nrf_proto.c`.

**"SPU" is not the co-processor's name** (corrected 17 November 2026). The vendor's phone app uses
`spu_id` for the product model (390 is the CC700 Pro) and the factory screen's `SPUVer 1.711` is the
product's firmware version, so `SPUID=%d Not Match DevType=%d` is a product-id check that happens to
sit in the link code. This document's title and earlier notes call the nRF "the SPU"; read that as
"the nRF co-processor".

## Why this matters

The barometer, the light sensor, the battery gauge and the IMU are **not** on the ESP32's I2C bus
(Q36). Every I2C transfer in the vendor application goes through two wrappers; those wrappers are the
only code in the whole image that references `i2c_master_transmit` or `i2c_master_transmit_receive`;
and all sixteen of their call sites are inside the touch driver. The sensors are messages on this
link, as — on the evidence so far — are ANT+ and the buttons.

So this one protocol is the gate in front of barometric altitude, the battery percentage, the
auto-backlight light sensor, the IMU, the buttons, and the deferred Di2 gear display. It is worth
doing once, properly.

## The wire

UART1 in the vendor's naming, **ESP port 2**: TX **GPIO 42**, RX **GPIO 41**, 8 data bits, no parity,
one stop bit, no hardware flow control (`rts` and `cts` are both −1). Decoded from
`uart_set_pin(UartNum, UART1TXDPIN, UART1RXDPIN, -1, -1)` and the `uart_config_t` built at
`0x420452f7`.

**The baud rate is not in the image.** `uart_param_config` takes it as a parameter, and there is no
plausible rate constant anywhere near `MidComm` — the `1000000` literals that looked promising are
decimal-parsing multipliers next to `addi a8, a8, -48` on ASCII digits. There is also a
`MidCommBaudrateSwitch`, so it is changeable at run time. `fw/main/hal_nrf_esp.c` therefore searches
the standard rates and lets the CRC decide which one is right.

## The frame

| Offset | Field | Value | Evidence |
| --- | --- | --- | --- |
| 0 | sync | **`0xA5`** | `movi a3, 165` + `bne` at `0x4205077d`; `movi a8, -91` + `s8i` at `0x42050674` |
| 1 | length | **payload + 4** | builder writes `len16 + 4` at `0x4205067f`; parser computes `total = hdr[1] + 4` |
| 2 | sync 2 | **`0x6F`** | `movi a9, 111` + `bne` at `0x420507da`; `movi a8, 111` at `0x42050684` |
| 3 | direction | **`0xF1`** outbound | `movi a8, -15` + `s8i a8, a7, 3` at `0x4205068a`. Read but not validated on receive, so the nRF's own value is unknown and is one of the things the first capture will show. |
| 4 | kind | **1…5** | `addi a8, -1; bltui a8, 5` at `0x42050789`, else `Com Instr Err:%x` |
| 5 | id | message id | what the dispatcher at `0x42051590` switches on |
| 6… | payload | | |
| last 2 | CRC-16 | little-endian | `buf[total-2]` low, `buf[total-1]` high, at `0x42050857`–`0x4205086a` |

Total frame length is `len + 4`, i.e. six header bytes + payload + two CRC bytes, so
`payload = total − 8`. The encoder and the decoder agree on this independently.

The parser rejects a length byte above **132**, logging `InstrLen:%d, InstrLen Error!`. On any
mismatch — sync, second sync, kind out of range or a bad CRC — it discards bytes and resynchronises
rather than giving up.

### The checksum

`0x4222137c` is a bitwise **CRC-16** with polynomial **0x1021** (the literal is at `0x4220e4a8`),
initial value **0**, no input or output reflection and no final XOR. It feeds the message bits in low
end first and then shifts sixteen zero bits through the register. That augmented form is
arithmetically identical to the ordinary **CRC-16/XMODEM** of the message, so any standard
implementation will agree with it byte for byte.

## The message ids

The dispatcher at `0x42051590` reads the id from offset 5 and acts on:

| id(s) | Handling |
| --- | --- |
| 0 | its own handler at `0x42050d78` |
| 1 | its own handler at `0x42050fa4`, given the kind and the payload |
| 11, 17, 34, 35, 40 | forwarded to the application: malloc 12, store the id, copy **8 payload bytes**, post as event 5 |
| 16 | its own handler at `0x42051200` — the power and slave-mode channel (see below) |
| 120, 121, 122, 123, 128 | forwarded the same way as 11…40 |
| anything else | returns 32, an error |

The 11-to-40 set is **not a range check**: it is the bitmask `0x21800041` at `0x4203eeb0`, tested with
`bbs` after subtracting 11. That is why 12 to 16 and 18 to 33 are absent.

### id 16 is power and slave mode

The four senders sit together at `0x42055bb0`–`0x42055d00` and all call the builder
`0x4205062c(kind, id, payload, payload_len)`:

| Function | kind | id |
| --- | --- | --- |
| `SendPowerOffCmd` | 2 | 16 |
| `SendPowerOnCmd` | 2 | 16 |
| `SendCheckPowerOnReasonCmd` | 1 | 16 |
| `SendQuerySlaveModeCmd` | 1 | 16 |

So `kind` separates a query (1) from a command (2) on the same id, and the payload distinguishes the
rest. That the ESP32 asks the nRF *why it powered on* is a strong hint that the power button, and
probably buttons A and B, are the nRF's to report — which fits the other evidence: every
`gpio_config`, `gpio_set_direction`, `gpio_set_level` and `gpio_reset_pin` call site in the image was
enumerated, and all the ones with immediate pin numbers belong to the LCD. There is no GPIO key scan
and no key interrupt in the stock firmware.

`KeyEventQueueInit`, `KeyQueueReceive` and `ArmSendBtnEvent` live in the same compilation unit as
those four senders (`0x42055a5b`, `0x42055e4e`, `0x42055d25`), which is further circumstantial support.

## What is deliberately not decided here

**Which id carries which sensor.** Chasing it statically means following five hops — parser, intrusive
list at `0x3c70b008`, event dispatcher `0x42050a48`, a repackage into a 16-byte struct, then another
queue through `0x42057be4` — and the meaning is only resolved at the far end. Watching real numbers
move is both faster and more certain, so `hal_nrf_esp.c` is built to make a single capture
self-interpreting: per-id period, which payload bytes are constant and which vary and between what
values, and each payload read as 16- and 32-bit little-endian integers.

Guessing the mapping would be START-HERE rule 3 all over again, and the display already showed what
confident guesses cost.

## The listener

`fw/main/hal_nrf_esp.c`, started from `ui_start()` in its own task on core 0 at priority 2.

- **Receive only.** It never writes to the port. The nRF is also holding the ANT+ radio and, on this
  evidence, the buttons; a passive sniff cannot upset it and a guessed frame could.
- **Finds its own baud rate** by trying 115200, 921600, 460800, 230400, 1000000, 57600, 38400 and 9600
  for twenty seconds each — comfortably longer than the ~13 s heartbeat the RAM probe saw — and
  accepting the first rate that yields a frame whose CRC checks out. For a wrong rate to produce a
  false positive, noise would have to open with `0xA5`, carry `0x6F` third, declare a length under 132
  *and* match a 16-bit CRC.
- **Prints a frame when it is new or when its payload has changed**, not on every arrival, so a
  multi-minute capture stays readable.
- **Summarises every thirty seconds**: one line per id with its kind, direction byte, length, count,
  mean period and a per-byte constant-or-range digest, then the payload as `u16le` and `u32le`.

### How to read the first capture

- A byte that tracks **covering the light sensor and then pointing it at a lamp** is the light sensor.
- A `u32le` near **101325**, or a `u16le` near **1013**, is pressure in pascals or hectopascals;
  carrying the unit up or down a flight of stairs should move it by roughly 12 Pa per metre.
- A `u16le` between **3300 and 4200** that drifts slowly is the battery in millivolts; a byte that
  sits between 0 and 100 is a percentage.
- A value that changes **only when a button is pressed** is the key channel. Pressing A and B during
  the capture is worth doing for that reason alone.

---

# Confirmed on the wire, 4 November 2026

First live capture. **The protocol decode is right.**

| Claim | Result |
| --- | --- |
| Baud rate | **115200**, found on the first candidate |
| Framing and CRC | **2184 frames, 0 CRC rejects** |
| Frame arithmetic | 872 frames × 16 bytes = **13,952 bytes — every byte of the stream**, nothing discarded |
| `len` byte | 12, so payload 8, total 16: exactly `len + 4` |

Zero rejects over 2184 frames is the strongest possible confirmation. Random or misaligned data passes
a 16-bit CRC once in 65,536 tries; passing 2184 times consecutively cannot happen by accident, and the
byte count shows the parser is not skipping or overlapping anything.

## Two corrections

**Offset 3 is a constant, not a direction marker.** The builder writes `0xF1` and the nRF sends `0xF1`
too, on every frame of both ids. Calling it a direction marker was an inference from seeing it written
on only one side, and it was wrong.

**Id 16 is not "the power channel".** The four senders use id 16 with kind 1 and 2, but the nRF streams
id 16 with **kind 4** at 36 Hz. So `kind` is a real selector, not a modifier: the same id carries
different things depending on it. The earlier description overstated what one id means.

## What the link carries when the unit is idle

| id | kind | len | period | frames in 50 s |
| --- | --- | --- | --- | --- |
| 16 | 4 | 12 | **28 ms** (≈36 Hz) | 1765 |
| 0 | 4 | 12 | **119 ms** (≈8 Hz) | 419 |

**Nothing else.** Ids 11, 17, 34, 35, 40, 120–123 and 128 — the ones the dispatcher forwards to the
application as eight-byte payloads — never arrived. Two readings, and the next capture separates them:
either those are **responses to requests** the ESP32 has to send first, or they only appear when the
thing they describe exists (a paired ANT+ sensor, a charger, a button press).

## What those two streams might be, and the experiment that decides it

The first capture cannot say, for a reason worth recording: the min-to-max digest was accumulated over
the whole run, and for a 36 Hz stream almost every byte saturates to `00-ff`, which carries no
information. Worse, the capture included twenty screen taps inside the window — and tapping the screen
moves the unit, so if either stream is the accelerometer the test contaminated itself.

Two things stand out from the little that was legible:

- **id 0 byte 1 is constant `0xff`** and byte 0 is only ever `0x52` or `0x53`, so that stream has fixed
  structure rather than being a raw radio dump.
- **id 0 bytes 5 and 6 both top out at exactly `0x64` = 100**, which is what a percentage looks like.

And the leading hypothesis for id 16: **36 Hz with an eight-byte payload is an IMU**. The firmware has
`ImuDataInit`, `ImuHandleAccData` and `ImuEventFuncCbReg`, and the factory test screen shows raw
accelerometer and gyroscope axes. Three 16-bit axes plus two bytes is eight.

**The experiment** needs no new code and carries no risk, because it only requires leaving the unit
alone:

1. **One full window (30 s) with the unit untouched on a table.** No taps. If id 16 is the IMU its
   payload bytes will be nearly constant and the digest will show narrow ranges.
2. **The next window spent moving it** — tilt, rotate, shake. If the same bytes go wide, that stream is
   the accelerometer, and the still-window values give the zero offsets for free.
3. **A third window with a hand over the light sensor**, then uncovered. A byte in id 0 that follows
   that is the light sensor.

To support it, `hal_nrf_esp.c` now **resets its digest at every window boundary** and prints a fresh
sample of 60 verbatim frames at the top of each window. A per-window digest is the difference between
"these bytes vary" and "these bytes vary *when the unit moves*", which is the whole question.

## On transmitting

The sensor ids may well need asking for, but sending to the nRF is not a step to take casually: id 16
is also what `SendPowerOffCmd` uses, so a malformed frame on that id could switch the unit off, and the
payloads of the four senders are built from globals rather than constants, so they cannot be replayed
byte-exactly from the image alone. The listener stays receive-only until the passive experiment above
has been run and Daniel has agreed to the first transmit.

---

# The 75-minute capture: id 16 kind 4 is a multiplexed transport

196,464 frames, still **0 CRC rejects**. The structural finding came from comparing two summaries
sixty seconds apart rather than from the digests, which had saturated as expected.

```
id 16  last as u16le:  1009  3281  5942  155     (t = 4537348)
id 16  last as u16le:  1009  3280  6053  155     (t = 4597538)
```

Word 0 is **constant 1009 = 0x03F1**, so payload byte 0 is `0xF1` and byte 1 is `0x03`. The digest's
byte 0 range of `49-f1` and byte 1 range of `00-0a` then read as **a sub-message tag and a small
index**, not as data. Id 16 kind 4 is a *transport*, carrying many different sub-messages interleaved
at 36 Hz — and a digest that mixes them together saturates every byte to `00-ff`, which is exactly
what the first two captures reported. That was a flaw in the listener, not in the data.

`hal_nrf_esp.c` now keys its statistics on **(id, payload byte 0)**.

## Hypothesis: id 16, sub-tag 0xF1 index 3, is the barometer

Taking bytes 2–7 of that sub-message as one 16-bit value and one 32-bit value:

| | t = 4537348 | t = 4597538 | change |
| --- | --- | --- | --- |
| bytes 2–3, `u16le` | 3281 | 3280 | −1 |
| bytes 4–7, `u32le` | 10,164,022 | 10,164,133 | **+111** |

- **3281 → 32.81 °C** at 0.01 °C per count. The unit had been running for 75 minutes, so a little over
  32 °C internal is reasonable, and a drift of one count a minute is what a settled temperature does.
- **10,164,022 → 1016.40 hPa** at 0.01 Pa per count (101,640.22 Pa). That is an unremarkable
  mean-sea-level pressure for Perth, and the +111 over 60.2 s is **1.11 Pa, about 9 cm of altitude** —
  which is precisely the scale of barometer noise and weather drift.

Two independent, plausible physical quantities falling out of one sub-message is a good sign, but it is
**a hypothesis, not a finding** (START-HERE rule 3). It predicts two things that are easy to check:
pressure should move by roughly **12 Pa — 1200 counts — per metre** of height, and the temperature
should read noticeably lower on a cold start than after an hour of running.

Also seen on id 16: sub-tag `0xF0` index 2 with all six data bytes zero, which looks like an idle or
keep-alive sub-message.

## Hypothesis: id 0, sub-tag 0x53, carries battery and charge state

Id 0 byte 0 is only ever `0x52` or `0x53` and byte 1 is constant `0xFF`, so it is multiplexed the same
way. For sub-tag `0x53`, two samples sixty seconds apart:

```
65363  6434  25608  32266
65363  6690  25608  32266
```

Bytes 4–7 are **identical** across the minute, and byte 5 is exactly `0x64` = **100** while byte 6 is
`0x0A` = **10**. A value pinned at 100 on a unit that is plugged into USB is what a battery percentage
looks like, and the digest's ranges for those bytes (`10-64` and `0a-64`, both topping out at exactly
100) support it. Bytes 2–3 move by +256 a minute, i.e. one count in the high byte — a slow counter.

Testable the same way: the percentage should fall once the unit runs on battery, and a charge-state
byte should change the moment the cable comes out.

## What is still missing

None of the ids the dispatcher forwards to the application — 11, 17, 34, 35, 40, 120–123, 128 — has
ever appeared, over 196,464 frames and 75 minutes. They are either responses to requests the ESP32 must
send first, or they only exist when the thing they describe does (a paired sensor, a button press, a
charger state change). The next capture should include **pressing buttons A and B**, which costs
nothing and tests the button hypothesis directly.

---

# The sensor map, from the first sub-tag-aware capture

`outputs/nrf-capture-2026-11-04.txt`. 6088 frames, **0 CRC rejects**, four id/sub-tag pairs. The raw
frames confirm the header byte for byte:

```
a5 0c 6f f1 04 10 | f1 01 09 00 f8 ff 18 04 | 55 13
^sync ^len ^6f ^f1 ^kind ^id=0x10        ^payload (8)   ^CRC
```

Splitting on the sub-tag worked. **Payload byte 1 is a second level of type**, and with both levels
separated the sensors are legible.

## id 16, sub-tag 0xF1 — the sensor transport

| byte 1 | rate | reading | confidence |
| --- | --- | --- | --- |
| **0x01** | ~8 ms | **accelerometer**, three int16 little-endian | **high** |
| **0x03** | ~100 ms | **barometer**: int16 temperature ×0.01 °C, then uint32 pressure ×0.01 Pa | **high** |
| **0x04** | ~70 ms | two alternating shapes, unresolved | low |
| **0x0a** | ~1 s | one uint16 around 3560–3576 | medium |
| 0x0d | once | seen a single time | — |

### Accelerometer (byte 1 = 0x01)

```
f1 01  09 00  f8 ff  18 04   ->  x = 9      y = -8     z = 1048
f1 01  10 00  fc ff  18 04   ->  x = 16     y = -4     z = 1048
f1 01  02 00  fc ff  1d 04   ->  x = 2      y = -4     z = 1053
```

Across the whole capture x stays within ±16, y within ±9, and **z sits at 1037–1062** while the unit
lay flat on a table. One axis reading ~1024 while the other two read ~0 is gravity, so this is the
accelerometer and the scale is about **1024 counts per g**. The exact scale and the axis order still
want the tilt test — stand the unit on each edge and whichever axis goes to ~1024 is that edge.

### Barometer (byte 1 = 0x03)

```
f1 03  bc 0c  d3 18 9b 00   ->  3260 = 32.60 degC   10,164,435 = 1016.44 hPa
f1 03  ba 0c  ce 18 9b 00   ->  3258 = 32.58 degC   10,164,430 = 1016.44 hPa
```

Temperature is bytes 2–3 as an int16 in hundredths of a degree. Pressure is bytes **4–7** as a uint32
in hundredths of a pascal — the byte 6 value of `0x9b` is the stable high byte, which is why reading
bytes 4–5 alone looked like noise. 1016.4 hPa is an ordinary Perth pressure and 32.6 °C is reasonable
for a unit that had been running a while. This confirms the earlier hypothesis on clean data.

**That is barometric altitude solved**, which closes the real hole behind Q19: `total_ascent` can come
from a barometer rather than from GPS elevation.

### Byte 1 = 0x04, unresolved — and why I am not calling it the light sensor

Two shapes alternate:

```
f1 04  58 02  00 00  1e 05    ->  600    0    1310
f1 04  d0 07  00 00  03 00    ->  2000   0    3
```

**600** is exactly what the factory test screen showed for `Light lux` on this unit, which is
tempting. But 2000 is also a suspiciously round number, and a field that is only ever 600 or 2000 looks
like an identifier or an interval, not a measurement — so the thing that moves would be the third
field (1306–1310 against 3). Calling this the light sensor on the strength of one coincidence is
exactly the mistake the display taught; the cover-and-uncover test decides it in one window.

### Byte 1 = 0x0a — probably battery millivolts

A single uint16 rising slowly through 3560, 3564, 3565, 3567, 3568, 3569, 3571, 3576. That reads
naturally as **3.56–3.58 V**. Caveat worth keeping: the unit was on USB throughout, and a charging
cell would normally read higher, so this may be something else that happens to live in the same range.
Unplugging for one window settles it.

## id 0 — status and a clock

| sub-tag | rate | content |
| --- | --- | --- |
| **0x53** | ~200 ms | `53 ff SS MM HH 64 0a 7e` — **seconds, minutes, hours** |
| **0x52** | ~300 ms | `52 ff ff ff c2 10 64 ff` — every byte constant: a status or configuration record |

The clock is not a guess. Across the capture byte 2 counts up and rolls over from `0x3b` (59) to
`0x00`, and on that rollover byte 3 does the same, and when byte 3 rolls over byte 4 goes from `0x08`
to `0x09`. Two carries observed. Whether it is time of day or elapsed time is open — `09:00` matched
neither UTC nor Perth time at the moment of capture, so elapsed is the better guess. Bytes 5–7
(`0x64`, `0x0a`, `0x7e` = 100, 10, 126) never changed.

## What this means for the HAL

`hal_baro_read_pa` and the temperature are implementable now. `hal_power_battery_pct` and
`hal_power_ambient_lux` need the two experiments above, each one window long. The IMU is a bonus the
spec never asked for and is out of scope for v1 (rule 4), but it is recorded here because it is free.

**No id outside 0 and 16 has ever been seen**, over more than 200,000 frames. The ids the dispatcher
forwards to the application are still unaccounted for, and the button question is still open.

---

# What the vendor's handlers say, 17 November 2026

Read from the stock firmware with Ghidra's decompiler (`tools/re/`, files only; the unit was not
involved). Earlier sections stopped at the parser because following the data further by hand meant
five queue hops; the decompiler follows them in seconds. Each claim below names the function it
comes from, so it can be re-read with `tools/re/gh.sh DecompileAt.java <address>`.

**Cross-checks on the method.** The decompiler reproduces what was already known and confirmed on
the wire: `0x4222137c` is the bitwise CRC with the polynomial at `0x4220e4a8`, and `0x4205062c`
builds `A5, len+4, 6F, F1, kind, id, payload, CRC low, CRC high`. The barometer handler divides the
int16 at payload 2 and the uint32 at payload 4 by `100.0` (`0x4203ed20`), which is the scaling the
capture had already given.

## Corrections to what is written above

| Earlier statement | What the code says |
| --- | --- |
| "the four senders build their payloads from globals rather than constants, so they cannot be replayed byte-exactly" | Three of the four use **constants in read-only data**, and `SendPowerOffCmd` builds its payload from the immediate `0x2E2`. All four payloads are known exactly (below). |
| `SendQuerySlaveModeCmd` is kind 1, id 16 | It is **kind 2, id 1** (`0x42055ca8` calls the builder with `(2, 1, …)`). Id 1 is the slave-mode channel; its reply is what the id 1 handler `0x42050fa4` takes. |
| "None of the ids the dispatcher forwards … has ever appeared", read as "the buttons may be among them" (Q38) | The buttons are **not** on those ids. They are on id 16, sub-tag `0x49` (below), unsolicited. |
| id 0 sub-tag 0x53 "bytes 5–7 never changed", clock "elapsed is the better guess" | Bytes 5–7 are **day, month and year**. It is a calendar clock, and the capture decodes to its own date. |
| id 0 sub-tag 0x52 "a status or configuration record" | Probably the **battery**: millivolts and percent. |
| id 16 / 0xF1 / 0x0a "probably battery millivolts" | Not supported by the code: the handler stores the int16 as a float in a global and posts no event. What it is remains unknown; it is not what the battery path reads. |

## The senders, exactly

All four call the builder `0x4205062c(kind, id, payload, 8)`.

| Function | Address | kind | id | Payload (8 bytes) | Source of the payload |
| --- | --- | --- | --- | --- | --- |
| `SendPowerOffCmd` | `0x42055bac` | 2 | 16 | `e2 02 00 00 00 00 00 00` | immediate `0x2E2`, then zero |
| `SendPowerOnCmd` | `0x42055c00` | 2 | 16 | `e2 02 00 00 00 01 00 00` | constant at `0x3c375e58` |
| `SendCheckPowerOnReasonCmd` | `0x42055c54` | 1 | 16 | `e2 02 00 00 00 01 00 00` | the same constant |
| `SendQuerySlaveModeCmd` | `0x42055ca8` | 2 | 1 | `24 01 ff ff 00 ff ff ff` | constant at `0x3c375e70` |

So power off and power on differ in one byte (payload byte 5), and "why did you power on" is the
power-on payload sent as a query (kind 1) instead of a command (kind 2). The reply comes back on id 16
with sub-tag `0xE2` and is handled as a power event (`0x420560f4`, `ArmSendPowerOnSysEvent`).

**This does not change the rule about transmitting.** Knowing the bytes makes a transmit *possible*,
not agreed: nothing has been sent, the listener has no transmit path, and the first transmit is still
Daniel's decision (Q38). What it changes is that `hal_power_off` is now a known frame, not a guess.

## Id 16 by sub-tag (`0x42051200`)

| Payload byte 0 | Byte 1 | What the handler does | Status |
| --- | --- | --- | --- |
| `0x49` | key index | **Key event.** Builds `{timestamp, index = payload[1], event = payload[6], payload[5]}` and posts it as event 0, which `0x42050a48` hands to `ArmSendBtnEvent` (`0x42055cfc`) and the "ESP32 Key Queue". The reader (`KeyQueueReceive`, `0x42055db4`) logs `Index:%d Evt:%d`, accepts index below 5, and treats event 4 as the start of a long press. | Layout from code. Not yet seen in a clean capture; the first long capture's byte-0 range `49-f1` shows the sub-tag does occur. |
| `0xE2` | 2 | Power event (reply to the power queries); event code in payload[5]: 4, 5 or 6 | From code only |
| `0xF0` | 2 | Posts payload[2] as event 3 | From code only; seen on the wire with zeros |
| `0xF1` | 1, 2 | Two motion handlers taking a uint32 at payload 2 and a uint16 at payload 6 (`0x42214dfc`, `0x42214eec`) | 1 is the accelerometer on the wire; 2 never seen |
| `0xF1` | 3 | **Barometer**: int16 / 100 degrees, uint32 / 100 pascals, then an altitude | Confirmed on the wire |
| `0xF1` | 4 | Takes the uint32 at payload 2, maps it through a threshold table to a level (`0x420511ac`), posts event 9. That is a **light sensor driving a backlight level**. | Probable; cover test still to do |
| `0xF1` | 10 | Stores the int16 at payload 2 as a float; no event | Unknown quantity |
| `0xF3` | 3 | Posts payload as event 7 | From code only |

## Id 0 by sub-tag (`0x42050d78`)

| Payload byte 0 | Layout | Status |
| --- | --- | --- |
| `0x53` | `53 ?? SS MM HH DD MO YY`: seconds, minutes, hours, day of month in the low five bits of DD, month from 0, year from 1900. The handler range-checks every field and logs `ERR TIME PARA` otherwise. | **Confirmed against data already captured**: `53 ff 09 00 09 64 0a 7e` is 4 November 2026, 09:00:09, and that capture was taken on 4 November 2026. The three high bits of DD read 3, which is Wednesday, the day it was. |
| `0x52` | uint16 at payload 4, byte at payload 6, posted together as event 4 | On the wire: 4290 and 100 with the unit on USB. Probably battery millivolts and percent. Unplug test still to do. |

## What the firmware now does with this

`fw/main/nrf_proto.c` decodes all of the above and `core/tests/test_nrf_proto.c` pins them to frames
from the capture (the clock test asserts the capture's own date). With `NRF_DECODE_LOG` on, the
listener names each message in the log (`KEY`, `BATTERY?`, `LIGHT?`, `CLOCK`), so the next capture
with the buttons pressed, the sensor covered and the cable out confirms or refutes each one in words.
Only the barometer is wired through to the core so far.

---

# Confirmed on the unit, 17 November 2026 (`outputs/nrf-capture-2.txt`)

Daniel flashed the build stamped `a990a63` and ran one capture: buttons in window 2, the light sensor
covered then lit in window 3, the unit raised then put on the floor in window 4. 6390 frames, **0 CRC
rejects**. Every layout the previous section read out of the vendor's code is what the wire shows.

| Message | Result |
| --- | --- |
| Keys, id 16 / `0x49` | **Confirmed.** Daniel pressed power, left, right, then held power for two seconds. The three short presses gave `49 00 … 01 80`, `49 01 … 01 80`, `49 02 … 01 80`: so **index 0 is power, 1 is bottom left, 2 is bottom right**, event 1, one frame per press. Holding a button gave event 4 twice, 300 ms apart, then event 5 when it was let go. So **1 is a short press, 4 is "being held" (repeating), 5 is "released after a hold"**. There is no separate press and release for a short press: the nRF reports clicks, not edges. Payload byte 7 was `0x80` throughout. |
| Battery, id 0 / `0x52` | **Confirmed.** 3477 mV and 2 % at boot, climbing a millivolt every four seconds to 3495 mV and 3 % on USB. On 4 November the same fields read 4290 and 100. Two states, both sensible. |
| Barometer, id 16 / `0xF1` / `0x03` | **Confirmed end to end, through our own firmware.** `hal_baro_read_pa` fed the core, which reported 71 m. On the desk 100478 to 100480 Pa; raised above head height 100465 to 100468 (about 12 Pa lower, about 1 m); on the floor 100484 to 100486 (about 7 Pa higher, about 0.6 m). Sign and size both right at 12 Pa per metre. |
| Light, id 16 / `0xF1` / `0x04` | **Confirmed as the light sensor, with one thing unexplained.** The uint32 read 600 in room light, fell to 30, 10 and 0 under a thumb, and rose to 6000 and 9000 under a torch. It moves in coarse steps (0, 10, 30, 50, 600, 2000, 6000, 9000), so it is a banded level, not raw lux. Unexplained: in steady room light it alternates between 600 with the uint16 near 1590 and 2000 with the uint16 near 2. |
| Clock, id 0 / `0x53` | Layout confirmed again, and a new fact: it read **2023-06-01 12:20:00**. The battery had run flat and the nRF's clock restarted from a default. It is a clock that has to be set, not a source of time. |

## Three things nobody predicted

**1. The nRF has an "off" state, and it was in it.** For the first 64 seconds after boot the link
carried only the battery message and `f0 02 00 …`, every 300 ms: no barometer, no light, no clock.
The sensors started at the instant the held button was released:

```
49 00 00 00 00 00 04 80        key 0 held
49 00 00 00 00 00 04 80        key 0 still held
49 00 00 00 00 00 05 80        key 0 released
e2 02 00 00 ff 05 00 ff        power event, code 5        (id 16)
01 01 00 00 00 00 00 01 04 11  first id 1 message ever seen
f1 03 f9 0b 23 4e 99 00        barometer starts
```

So **key 0 is the power button, and a long press of it switches the nRF's sensor side on without the
ESP32 sending anything.** The `e2` frame is the event the vendor handler turns into
`ArmSendPowerOnSysEvent` (code 5 or 6 is "power on", code 4 the other event). The 4 November captures
had sensors from the start because the unit had been switched on normally before our firmware ran.

**2. Why it was off: the battery had run flat.** 2 % and a clock reset to its default both say the
unit ran until it died since 4 November. That is the cost of our firmware having no way to switch the
unit off, seen for real.

**3. Pressing on the case moves the barometer.** While the light sensor was covered with a thumb the
pressure rose by up to 29 Pa (100478 to 100507) for about four seconds, the equivalent of 2.4 m of
descent. A hand on the unit is a pressure event. The core's one-second mean and 0.25 filter will damp
it, but it is worth knowing before blaming the altitude code for a blip.

## What is still unknown on the link

- `0xF1` / `0x0a`: an int16 around 3690 (`71 0e`), a little above the battery's millivolts and not
  the same quantity. Unknown.
- The id 1 message `01 01 00 00 00 00 00 01 04 11` that arrives with power-on.
- What a long press of key 0 does when the sensors are already on (presumably the code 4 event, a
  request to power off that the ESP32 is expected to answer with `SendPowerOffCmd`).
- Ids 11, 17, 34, 35, 40, 120 to 123 and 128: still never seen.

## The buttons are now an input

`fw_nrf_keys_feed` (`fw/main/nrf_proto.c`) turns the clicks into the down and up edges the core's
hold detection expects: a click is a press and a release at once; the first "held" frame is a press;
"released after a hold" is the release; and if the repeats stop for more than a second with no
release, the button is released anyway, so it can never stick. `hal_input_poll` serves them from a
queue. `test_keys_as_captured` replays the capture's own frames at their own times. The consequence
for hold timing is Q40.
