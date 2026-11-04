# The nRF co-processor link ("SPU")

Everything here is read out of the stock firmware in `outputs/esp32s3-backup/original-flash-16MB.bin`
by static analysis. **The unit was not involved in producing any of it.** Addresses are virtual
addresses in the `ota_0` application image; the method — find a string, find the `l32r` that loads it,
read the code around it — is described in `BRINGUP-LOG.md`.

Nothing in this document has yet been seen on the wire. It is Unverified in the sense of START-HERE
rule 3, but it is read from the vendor's own encoder *and* its own decoder, and the two agree, which
is a strong internal check.

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
