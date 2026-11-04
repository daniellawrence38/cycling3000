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
