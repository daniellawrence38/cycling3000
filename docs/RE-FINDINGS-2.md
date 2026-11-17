# Stock firmware, second pass: GNSS, power handshake, the unexplained messages, ANT+ ids, backlight

Read from the stock application in `ota_0` with the offline tools in `tools/re/` (Ghidra decompiles
through `gh.sh`, the image's bytes through `rd.py`, and a linear `objdump` of the ELF where the
decompiler drops an argument). **The unit was not involved.** Addresses are virtual addresses in the
image. Each claim says where it comes from. **Read** means read from the code at that address.
**Inferred** means my interpretation of what was read, with the reason given.

`docs/NRF-PROTOCOL.md` is the base for this document. Where this document disagrees with it, the
disagreement is stated in a table at the start of each section.

## 0. Two facts every timeout below depends on

| Fact | Evidence |
| --- | --- |
| **`CONFIG_FREERTOS_HZ` is 1000**, so every `vTaskDelay(n)` in the vendor code is n milliseconds | `esp_log_timestamp` at `0x4038ef5b` returns `base + tick_count` with no multiply (`add a2, a10, a8` at `0x4038efba`). IDF computes `tick_count * (1000 / configTICK_RATE_HZ)`, so the factor is 1. |
| `0x40389f38` is `vTaskDelay` | Its body asserts with `tasks.c` / `vTaskDelay` / `xTaskGetSchedulerState` strings (`0x40389f38`). Almost every "delay" pointer in the vendor code (`PTR_FUN_42140fc0`, `PTR_FUN_4203ec08`) resolves to it. |

---

## 1. GNSS

### 1.1 The driver supports six receiver families, and this unit's build is set to Airoha

The GNSS driver ("`GpsInternalCommandSend`", `0x421b7488`) sends command *n* by looking it up in one
of five tables chosen by a **module type** held at `0x3fca5588` (`PTR_DAT_4218337c`):

| Module type | Table (pointer, then base) | Command set | Inferred vendor |
| --- | --- | --- | --- |
| 0 | `0x42183368` → `0x3fca5814` | `$CFGMSG`, `$CFGPRT`, `$CFGNAV`, `$CFGSYS`, `$CFGSAVE`, `$PDTINFO`, `$reset,0,h00/h01/h85` | Unicore-style NMEA config |
| 1, 2 | `0x42183374` → `0x3fca5754` | `$CCMSG`, `$CCINV`, binary `#>` frames; detected by a `"T3"` / `"T5"` reply | unknown |
| 3 | `0x4218336c` → `0x3fca5694` | binary frames with header `F1 D9` | Allystar (its binary header is `F1 D9`) |
| **4, 5** | `0x42183370` → `0x3fca55d4` | **`$PAIR`** | **Airoha** (AG3352 family; `GpsPthreadFullColdBoot_AM3352`) |
| 7 | — | "unknown": `GpsInternalCommandSend` logs `UNKNOWN` and sends nothing | — |

**Read:** the initial value at `0x3fca5588` is **4** (`rd.py`). The driver keeps it unless it is 7
(`FUN_421b841c` overwrites the type only when the current type is 7). So the stock build runs the
Airoha table from the start and never runs the module auto-detection (`MODULE_ADAPTIVE`,
`FUN_421b87d0`, which probes `$PDTINFO`, `#> 05 01` and `F1 D9 0A 04` in turn). The other tables are
dead weight for this unit. That is also why `$CCMSG...` and `$reset,0,h..` strings appear in the
image without the unit speaking those dialects.

**Inferred:** the phone app's "allystar_agnss" download is the same multi-vendor support on the app
side, and is probably not what this unit uses. The firmware's own Wi-Fi A-GNSS path is
`/indoor/v1/app/config/loda_agnss/download` (string table, near "AGNSS WIFI Server Return").

### 1.2 UART0 and the baud rate

| Claim | Evidence |
| --- | --- |
| UART0 is opened once at **115200** | `AppDevInit` → `FUN_421566c0(115200)` → `MidCommInit(0, 115200, …, 0x400, 1)` (`0x421566c0`, already in `STOCK-FIRMWARE-FINDINGS.md`) |
| The GPS thread's own baud variable is set to **115200** at thread start | `FUN_421566dc` stores `0x1c200` to `0x3c72115c` (`0x42156716`–`0x42156719`) |
| **No baud switch for this receiver.** State 0 of the machine calls `MidCommBaudrateSwitch(0, baud)` (`0x42045398`) only if that variable is not 115200 (`0x4215695e`), and nothing else writes the variable | `objdump` shows only two references to `0x3c72115c`: the store above and the compare at `0x42156954` |
| The `$CFGPRT,…,9600` / `115200` commands are sent only for module type 0 | `FUN_421b759c` (`0x421b759c`): the `0x1b`/`0x1c` sends are inside `if (type == 0)` |

So UART0 stays at 115200 for its whole life. `hal_gnss_esp.c` already does that.

### 1.3 The start-up command sequence, in order (module type 4)

The GPS thread is `FUN_42157460`: it calls the state machine `GpsPthreadMachine` (`0x42156850`) and
then `vTaskDelay(10)`, for ever. It skips the machine while either of the upgrade or test flags
checked by `FUN_42154694` and `FUN_421543d0` is set. Command *n* in the machine is table index
*n* + `0x1f` (`FUN_421b83c4`, `0x421b83c4`). After each command the driver calls the delay callback
with 2, so there is a 2 ms gap (`0x421b7584`, `puVar1[3](2)`).

The reply flags are set by the sentence parser `FUN_421b75f0`, which matches by prefix:

| Flag | Set by the reply | Read by |
| --- | --- | --- |
| `0x421833e0` | `$PAIR867,1*2E` | `FUN_421b81bc` |
| `0x421833a4` | `$PAIR867,0*2F` | `FUN_421b81c8` |
| `0x421833c0` | `$PAIR001,866,0*33` | `FUN_421b81a4` |
| `0x421833a8` (and `0x4218339c`) | `$PAIR001,490,0*36` | `FUN_421b818c` |
| `0x421833b4` | `$PAIR491` (any) | `FUN_421b8198` |
| `0x421833ec` | `$PAIR470` (any) | `FUN_421b81b0` |
| `0x421833d0` | `$PAIR001,007,0*3C` | `FUN_421b81d4` |
| `0x421833d8` | `$PAIR001,006,0*3D` | — |
| `0x421833c8` | `$PAIR001,382,0*32` | — |
| `0x42183404` | `$PAIR001,650` (any) | cleared by `GpsOpen` |
| version string at `0x421833fc` | `$PAIR020,` + 6 characters from offset 17 | factory version check |

The machine, state by state (state variable at `0x3c721108`; retry counter `0x3c721158`;
attempt counter `0x3c721154`):

| State | Action | Next |
| --- | --- | --- |
| 0 | If the baud variable is 0, wait. If it is not 115200, switch the baud. | 1 |
| 1 | Register the callback table (below) with module type "7 if unknown" (`0x421b841c`) | 12 |
| 12 | Poll UART; `FUN_421b837c(0x14)` returns 0 once the driver is in its parse state (immediately for a preset type) | 2 |
| **2** | send **`$PAIR867,0,0*33`** (cmd 9 → index `0x28`), wait 100 ms | 3 |
| 3 | Poll. Reply `$PAIR867,1` → 4. Reply `$PAIR867,0` → 8. No reply after 11 polls → back to 2. After 11 such retries → 8 | 4 / 8 / 2 |
| **4** | send **`$PAIR866,0,0,0*2E`** (cmd 8 → `0x27`; the argument `movi a10, 8` is at `0x421568bf`), wait 100 ms | 5 |
| 5 | Poll for `$PAIR001,866,0` → 8; retries as in 3 → back to 4, then 8 | 8 / 4 |
| **8** | send **`$PAIR490,1*2A`** (cmd 15 → `0x2e`), wait 100 ms | 9 |
| 9 | Poll for `$PAIR001,490,0` → 10; retries → 8, then 10 | 10 / 8 |
| **10** | send **`$PAIR491*36`** (cmd 16 → `0x2f`), wait 100 ms | 11 |
| 11 | Poll for any `$PAIR491…` → 15; retries → 10, then 15 | 15 / 10 |
| 15 | **Running** (1.5 below) | 13, 16 |
| 13 | A-GNSS / EPO injection (1.6) | 6, 15, 18 |
| **6** | wait 500 ms, log `send470`, send **`$PAIR470,0*25`** (cmd 11 → `0x2a`), wait 100 ms | 7 |
| 7 | Poll for any `$PAIR470…` → record the injected file and segment, go to 15; retries → back to 6 | 15 / 6 |
| 16 | `GPS_RESET_STATUS`: **hardware reset** through the nRF (1.4), wait 100 ms | 1 |
| 17 | `GPSIC_DEAD_STATUS`: log only, for ever | — |
| 18 | `GpsInsertInterrupt` landing state | 15 |

So the stock start-up sends exactly four `$PAIR` sentences, in this order, each sent until it is
answered: `$PAIR867,0,0` (answer decides whether the next one is needed), `$PAIR866,0,0,0` (only
if the answer was `,1`), `$PAIR490,1`, `$PAIR491`. Nothing is sent before the receiver has spoken:
the poll states run the UART reader first. `$PAIR` sentences in the table that the machine does not
send at start-up: `$PAIR382,1` / `$PAIR382,0` (cmd 5/6), `$PAIR023` (cmd 10), `$PAIR590,` (cmd 13,
no checksum, so it is a prefix that has fields appended elsewhere), `$PAIR020` (version query,
sent only by the factory checks `0x421b8214` / `0x421b8290`), `$PAIR004/005/006` (hot, warm and cold
start, the high table at `0x3fca558c`), `$PAIR007` (full cold start, `GpsPthreadFullColdBoot_AM3352`
`0x4215672c`: up to 21 tries 50 ms apart until `$PAIR001,007,0`), and `$PAIR490,0`
(`GpsPthreadFullDisableEPOC` `0x421567bc` / `CloseEPOCSend` `0x421b8308`: up to 21 tries 50 ms apart
until `$PAIR001,490,0`).

**Inferred, from Airoha's published PAIR command list and not from this image:** 866 / 867 are "set /
get UART flow control" (so the stock code reads the flow-control setting and switches it off if it
is on), 490 / 491 are "enable / query EASY" (the receiver's self-generated orbit prediction), 382 is
"lock system sleep", 004–007 are hot, warm, cold and full-cold starts, 020 is "get firmware
version", 023 is "reboot", and `$PAIR001,<cmd>,<result>` is the acknowledgement with result 0 meaning
success. The control flow above is consistent with those meanings. The meaning of 470 and 590 is not
settled.

### 1.4 Power, open, close and hardware reset

The callback table registered in state 1 (`0x42156985`–`0x421569a3`, slots stored at `sp+0`…`sp+20`):

| Slot | Function | Does |
| --- | --- | --- |
| 0 write | `0x42155fc0` | `MidComm` write on port 0 (UART0 TX, GPIO 1) |
| 1 | 0 | — |
| 2 hard reset | `0x42155fac` | nRF frame kind 2, id 16, `e2 02 07 00 00 02 00 00` |
| 3 delay | `0x42155f74` | `vTaskDelay` |
| 4 power on | `0x42155f98` | nRF frame kind 2, id 16, `e2 02 07 00 00 01 00 00` |
| 5 power off | `0x42155f84` | nRF frame kind 2, id 16, `e2 02 07 00 00 00 00 00` |

| Function | What it does (read) |
| --- | --- |
| `GpsOpen` `0x421b8708` | If the "closed" flag (byte 2 of the fix record pointed to by `DAT_42183414`; `FUN_421b86f0` reads it) is 1, logs `GpsOpen!(%d)` and calls **slot 4 (power on)**. Then clears the flag and the `$PAIR001,650` flag. If the flag is already 0 it sends **nothing**. |
| `GpsClose` `0x421b8770` | If the flag is 0, logs `GpsClose!(%d)` and calls **slot 5 (power off)**. Then sets the flag to 1. |
| `GpsHdRst` `0x421b8694` | Calls **slot 2 (reset)** once, increments the reset counter at `0x42183390`'s target, logs `GpsHdRst:%d`. **No power-on frame follows.** |

**Read:** the "closed" flag starts at 0 (open), because registration clears the fix record
(`FUN_421b841c` memsets it). So **at boot the stock firmware sends no GNSS power frame at all**. It
assumes the receiver is already powered when the nRF is on. GNSS power frames go out only from
`GpsOpen` / `GpsClose`, whose only callers are `RideStateProccess` (`FUN_42053b58`: `GpsOpen` at
`0x4205526c` and `0x420553ee`, on ride-state transitions logged as `RideState:%s -> %s`; `GpsClose` at
`0x420547af`) and the MCU upgrade path (`GpsClose` at `0x421549c1` in `UpgProcess` `0x421547a8`).

**The silence watchdog (read, state 15).** Every 10th machine pass (about 100 ms), the machine reads
UART0. If no bytes arrive and the receiver is not closed, a counter at `0x4214186c`'s target goes up.
At **51 consecutive empty passes (about 5.1 s)** the machine goes to state 16: one hardware-reset
frame, 100 ms, then back to state 1 and the whole command sequence again. If the receiver is closed,
it instead posts up to three empty fixes and does not count. The reset counter would stop the
retries after 3 resets with `GPSIC_DEAD_STATUS`. But registration in state 1 zeroes that counter,
and so does every byte the parser reads (`FUN_421b75f0`). **As decompiled, the dead state looks
unreachable**, so the stock firmware resets a silent receiver about every 5 s for ever. I have not
proved there is no other path to state 17.

**Other device bytes in the same `e2 02` command** (read; the meaning of the device numbers is
inferred):

| Payload | Where | Context |
| --- | --- | --- |
| `e2 02 00 00 00 00/01 00 00` | `SendPowerOffCmd` / `SendPowerOnCmd` | whole unit |
| `e2 02 01 00 00 02 00 00` | `UpgProcess` `0x421547a8`, constant at `0x3c6d71e4`, logged `SendPowerRstCmd` | sent 2 s after a successful co-processor ("Slave MCU") upgrade; device 1 = the nRF, mode 2 = reset |
| `e2 02 07 00 00 00/01/02 00 00` | GNSS callbacks above | GNSS off / on / reset |
| `e2 02 08 00 00 00 00 00` | `ArmStateProcess` FACTORY_INIT (state 12), immediate `0x802e2` at `0x4203f410` | factory mode only; meaning unknown |

### 1.5 Which sentences are parsed

`FUN_421b75f0` reads one `$`-started sentence at a time, checks the `*XX` XOR checksum, matches the
`$PAIR` replies above, then classifies the sentence by its three-letter type with `FUN_421b93b8`
(type names at `0x42183514`–`0x42183534`): RMC=1, GGA=2, GSA=3, GLL=4, GST=5, GSV=6, VTG=7, ZDA=8,
EPE=9. **Only RMC, GGA, GSA and GSV are acted on.** The talker is not checked for RMC, GGA or GSA.

| Sentence | What the stock code does (read) |
| --- | --- |
| RMC (`GpsParseRmc`, `FUN_421b9470`) | A fix is declared valid only after **3 consecutive** valid RMCs (counter at `0x42183428`'s target). A not-valid RMC clears validity, speed and the altitude flag at once. A speed filter logs `SpeedOverCnt` / `speed over filter` and rejects spikes after more than 10 consecutive over-limit samples. |
| GGA (`FUN_421b94f4`) | Stores the satellite count. Accumulates altitude ×10 (decimetres) and reports the **mean of the first 15 samples**, then a filtered value. Altitude is used only when at least 6 satellites are used and the GSA VDOP is at most 5.00; it is clamped to −2000…`0x186a0` (−200 m … 10 000 m in decimetres). |
| GSA (`FUN_421b956c`) | PDOP, HDOP and VDOP into the fix record (+0x20, +0x1c, +0x24). |
| GSV (`FUN_421b95f4`) | Satellite SNRs per constellation by talker: `GP`→GPS (signal 8 = L5 flagged), `GL`→GLONASS, `GA`→Galileo (signal 1 flagged), `BD`→BeiDou (signals 4–5 flagged); a sum over four entries is stored as signal quality. `GB` is skipped. |

### 1.6 EPO / A-GNSS injection

**The file (read).** `GpsThreadGetAgnssFileName` / `GpsPthreadGetAgnssFileInfor` (`0x421560a8`) and
`CheckLastestAgnssFile` (`0x42156380`) scan **`/sdcard/EPHEMERIS`** for names accepted by
`is_valid_agnssname` (`0x42155ee0`). They take the decimal number before the first `_` as a
timestamp, keep the newest file and delete the others. If the timestamp is not after the current
time (`AgnssTimeOut`) they forget it. The kept path goes to `0x3c72116c` and the timestamp to
`0x3c721164`. The string table also has `_agnss.bin`, so **inferred:** names are
`<unix-time>_agnss.bin`, with the time being the data's expiry.

**When (read, state 15).** Injection is considered when there has been no valid fix, or the last
valid fix is more than **3000 s** old (`0x421418d4` = `0xbb8`). It also needs a file whose timestamp
is more than 14 days (`0x42141804` = `0x127500` s) in the future, and fewer than 3 attempts. The
starting segment is `(now − (expiry − 14 days)) / 259 200 s` (3 days). That divisor comes from
the multiply by `0x205d0b9` (`0x421418d0`) and shift by 36 in state 15's A-GNSS check (from
`0x42156c38`), applied after a shift by 7; together they divide by 128 × 2025 = 259 200.

**How (read, state 13).** Open the file `rb`. Byte 0 is a segment count N (<11). N 32-byte segment
headers follow, each with an 8-character name tested against `EPO_GPS_` (type byte `'G'`) and
`EPO_BDS_` (type `'C'`), then 23 + 4 + 4 more bytes, and a magic `0xAA555AA5` at header offset
`0x160`. For each segment from the starting one: Airoha binary command **`0x4B0`** with the 1-byte
type; 100 ms; **`0x4B1`** frames of **72 bytes** of data each, 50 ms apart; **`0x4B2`**; 1000 ms.
Then log `ToGPSIcOk`, 500 ms, and state 6 sends `$PAIR470,0`. The binary frame (`FUN_421565fc`) is
`04 24 <cmd u16 LE> <len u16 LE> <payload> <xor checksum> AA 44`. That is Airoha's binary framing.
The decompiler shows the checksum as an XOR but not clearly its span. A "stop" flag (`0x421834e0`)
aborts the injection with a `0x4B2` and state 18.

---

## 2. Power: what the nRF sends and what the stock firmware does with it

### Corrections to `NRF-PROTOCOL.md`

| It says | The code says |
| --- | --- |
| code 4 is "the other event … presumably a request to power off that the ESP32 is expected to answer with `SendPowerOffCmd`" | **Code 4 is `SYS_EVENT_DEVICE_OFF_CHARGE`**: the unit was woken by the charger while off. It is a boot reason, not a power-off request (`0x420560f4`). |
| (implicit) the power button's long press reaches the ESP as a power message | A long press **while on** reaches the ESP only as **key events** (`49 00 … 04 …` held, then `05` released). The nRF does not send an `e2` frame for it. Power-off is decided on the ESP side, by the UI, and ends in `SendPowerOffCmd`. |

### 2.1 The `e2 02` power event (`0x42051200` → event 8 → `ArmSendPowerOnSysEvent` `0x420560f4`)

- The id 16 handler posts event 8 only if payload[1] == 2 **and not (kind == 5 and payload[5] == 0)**.
  The event carries payload[2] (device) and payload[5] (code) (`0x42051200`, the `0xe2` branch).
- `ArmSendPowerOnSysEvent` looks **only at payload[5]**. It reads the code at `0x42056168` and does
  not check the device byte:

| payload[5] | System event (names from the table at `0x3fca14e0`) |
| --- | --- |
| 4 | 2 = `SYS_EVENT_DEVICE_OFF_CHARGE` |
| 5 or 6 | 1 = `SYS_EVENT_POWER_ON_MANUAL` |
| anything else | nothing is posted (`bgeui a8, 2` at `0x42056174` returns) |

  The system event queue (`ArmSendSysEvent` `0x42056090`) also carries 0 = `SYS_EVENT_POWER_OFF` and
  3 = `SYS_EVENT_POWER_SLEEP`, which are posted by the UI, not by the nRF.

### 2.2 The arm state machine (`ArmStateProcess`, `0x420563c0`)

It is called every **10 ms** (`FUN_42051fac`: `call8 0x420563c0`, then `vTaskDelay(10)` at
`0x42051fc4`). The state is at `0x3c70c1dc`'s target (`DAT_4203f2b8`). Power-on, in order:

| State | Name in the log | What it sends and waits for (read) |
| --- | --- | --- |
| 0 | (slave query) | The slave-mode byte (`0x3fca4bc4`, set by the id 1 reply below) **starts at `0xFF`**. While it is ≥ 2: **`SendQuerySlaveModeCmd`** (kind 2, id 1, `24 01 ff ff 00 ff ff ff`) every **20th pass (200 ms)**. The divisibility test is `mull` by `0xcccccccd`, rotate right by 2, compare with `0x0ccccccc`, at `0x42056462`. After **60 000** passes (about 10 min) with no answer: reboot (`FUN_42055b68`, `0x42056487`). When the byte is **1** ("`Get Slave Work inApp,Run PowerOn Process`") → state 1. When it is **0**: keep querying every 200 ms; after **200 passes (2 s)** "`Get Slave Work in Bootloader,Start to Auto SlaveUpg`" → `0x421546bc` (co-processor upgrade). |
| 1 | `INIT_QUERY` | A blocking loop. **`SendPowerOnCmd`** (`0x420565c1`), then wait up to **100 ms** for a system event. The loop goes back to `0x420565c1` (`bnez … 420565c1` at `0x42056765`), so **the power-on command repeats about every 100 ms** until an event arrives. Every **301** empty waits (about 30 s) it also sends **`SendCheckPowerOnReasonCmd`** (`0x42056682`). `POWER_ON_MANUAL` → boot mode 0. `DEVICE_OFF_CHARGE` → boot mode 1. After `POWER_ON_MANUAL` it watches keys for **40 × 30 ms = 1.2 s**: if key events `0x400`, `0x401` and `0x402` (index 0, 1 and 2 with event 4, "A/B/C L press") are all seen → boot mode 2 (factory). |
| 2 | `BOOTUP_POWER_ON_MANUAL` / `…_POWER_OFF_CHARGE` / `…_FACTORY` | mode 0 → 3; mode 1 → 8 (charging screen while off); mode 2 → 12 (and buzzer tone `0x17`) |
| 3, 4, 5 | `USER_INIT`, `FILE CHECK`, `Next, USER` | file system and UI start |
| 6 | (user) | normal running: keys to UI events (2.4), screen handling, `RideStateProccess`, system events |
| 8, 9 | (off-charge) | `FUN_420561ec`: a power-key hold (`0x400`) → `FUN_42055b68` (reboot into the normal boot, inferred: "turn on from the charging screen") |
| 12, 13 | `FACTORY_INIT` | sends `e2 02 08 00 00 00 00 00` |
| 14 | `FACTORY_UNINIT` | `SendPowerOffCmd`, 500 ms |

`FUN_42055b68` (used above and by the upgrade code) suspends thread 4, sets the backlight to 0,
waits 10 ms and runs the IDF shutdown handlers into a restart (`FUN_42002738`). **It reboots; it does
not power off.**

### 2.3 The id 1 replies (`0x42050fa4`)

| payload[0] | Handling (read, `objdump` of `0x42050fa4`–`0x420510c4`) |
| --- | --- |
| `0x01` | **Co-processor firmware version.** Stores payload[7] → `0x3fca1310[0]`, payload[8] → `[1]`, payload[9] % 10 → `[2]`, payload[9] / 10 → `[3]`. Shown as `%d.%d%02d` from `[3]`, `[2]`, `[1]` (`0x42174528`, "`node_fw_version`" for the phone) and as `S:%d.%d%02d` on the factory screen (`FV M:%d.%d%02d S:%d.%d%02d`, `0x4214bac5`). |
| `0x17` | malloc 12, `{payload[1], payload[0..7]}` → event 6 → ANT queue as type 0 (2.4 of section 4). If payload[5] == 4, logs `%d Com Disconn`. |
| `0x18` | ignored (returns 0) |
| `0x24` (`'$'`) | `SendPackDataToStoreQueue` `0x4214f030(0, p, 8)`: if payload[1] == 1, **payload[5] is the slave mode** stored at `0x3fca4bc4` (`Get Slave Work in %d`), then the 8 bytes go to the store queue |
| `0x25`, `0x26` | same store queue, 11 and 6 bytes |
| `0x27`, `0xF2` | 128 bytes to the BLE queue (`0x42057d44`, "`BleQueue Full`") |
| anything else | returns 32 (error) |

**The power-on id 1 message `01 01 00 00 00 00 00 01 04 11` decodes as the co-processor's firmware
version 1.704** (`[3]` = 0x11 / 10 = 1, `[2]` = 0x11 % 10 = 7, `[1]` = 0x04; `[0]` = 0x01 is stored
but its meaning is unknown). That is read from the code. That the "Slave" is the nRF is inferred
from the surrounding strings (`Slave MCU Upgrade`).

### 2.4 Long press of the power button while on, and the power-off handshake

**Read**, in order:

1. The nRF sends key frames (`49 00 … 04 80` every 300 ms while held, `05` on release; confirmed on
   the wire). The id 16 handler posts them as event 0. The dispatcher (`0x42050a48`) hands them to
   `ArmSendBtnEvent` and to the app as message `0x11`.
2. In the user state, `ArmStateProcess` maps (key index, event) to a UI event code posted with
   `0x420ded7c`. Index 0: event 1 → `0x2f`, **event 4 → `0x34`**, event 5 → `0x3e`, event 6 → `0x39`.
   Index 1: 1/4/5/6 → `0x30/0x35/0x3f/0x3a`. Index 2: `0x31/0x36/0x40/0x3b`. Indices 3 and 4 have
   codes too. **Event 6 exists in the vendor's key handling** and has not been seen on the wire. If
   the screen is off, the first key only wakes it (`Screen up for Key`, `ScreenOffWakeUpHandle`
   `0x42155214`).
3. The UI opens `SystemPop_ManualPowerOff` (`0x4208c548`). Its confirm action `0x4208d01c` posts
   **`SYS_EVENT_POWER_OFF`** (`ArmSendSysEvent(0)`). Other posters of event 0: the low-battery popup
   timer `_LowPowerOffTimerCb` (`0x4208c80c`), `0x4208c800` and `0x4208cb34`, and the phone command
   `SMRAT_SEND_POWER_OFF` (`0x42187680`). **Not traced:** which UI event code opens the popup; that
   it is `0x34` (power held) is inferred.
4. In the user state, `ArmAyalyzeSysEvent` (`0x42055abc`) takes the event without waiting and stores
   it at `0x3fca14f0` (5 means none). When it is 0: `ScreenPowerOffHandle` (`0x42155288`), a buzzer
   call (`0x420559fc`), state 15.
5. State 15: `0x42153d18` frees the ride buffers and **closes the open files**. State 16, then
   state 17.
6. **State 17**, every pass: buzzer tone 7 (`FUN_42051138(7)`), **1000 ms**, **`SendPowerOffCmd`**
   (`0x420572d5`, its only call site), **500 ms**, and stay in state 17. So **`SendPowerOffCmd` is
   repeated about every 1.5 s until the power goes**. No GNSS-off frame is sent on this path.

---

## 3. The unexplained messages

### 3.1 id 16 / `0xF1` / `0x0a` is a second temperature, in hundredths of a degree

- **Read:** the handler stores `(float)(int16)payload[2..3]` at `0x3fcac12c` (`0x42051200`).
- **Read:** the only reader is the getter `0x420511f4`. Its only caller is the factory test
  `FUN_42071e04` at `0x42072664`. The `objdump` of `0x42040d6a` is data, not code.
- **Read:** the factory test divides it by **100.0** (`0x420409f4` = `42c80000`). It shows it beside
  the barometer's temperature and pressure (app message 7 = `{temp, pressure, …}` floats from the
  barometer handler) and the battery percent. It **passes only if `|baro_temp − value/100| < 4` and
  the battery is 60–85 %** (`0x42072cb4`, `0x42072d40`, decompile lines around `0x42072698`).
- **Inferred:** it is a **temperature in units of 0.01 °C**, from a sensor other than the barometer.
  The test expects it within 4 °C of the barometer's reading. 3560–3700 is 35.6–37.0 °C, about 3–4 °C
  above the barometer's 32.6 °C in the same capture. Which part it is (battery thermistor, nRF die,
  IMU) is not in the ESP code.

### 3.2 The id 1 message at power-on

The co-processor firmware version, **1.704** (2.3).

### 3.3 The light sensor's 600/2000 alternation

- **Read:** `0x420511ac` loops over 14 entries of an 8-byte `{u8 pct, pad, u32 value}` table: if
  `tbl[i].value ≤ x < tbl[i+1].value` it returns `tbl[i].pct`. If x is at or above the last value it
  returns `tbl[14].pct` (`objdump` `0x420511bf`–`0x420511e4`; the decompiler dropped the loop). The
  table is chosen by `FUN_420378c8()`: `0x3c36eb7c` if 0, else `0x3c36ebf4`. **The two tables are
  byte-identical** (`rd.py`), so the choice changes nothing in this build.
- **Read:** the handler builds `{u32 value, u16 payload[6..7], u8 pct}`, copies it to `0x3c70b010`
  (getter `0x420511ec`), and posts it as event 9 → app message `0x15`.
- **Read:** the info screen (`0x4214b7dd`–`0x4214ba61`) prints the uint32 under **"EnvLight"** and
  the trailing uint16 under **"LightVoltage"**.
- **What this explains:** the uint16 is the sensor's raw reading (a voltage) and the uint32 is the
  nRF's banded value. **What it does not explain:** why the nRF alternates 600/≈1590 and 2000/≈2 in
  steady light. Nothing in the ESP code feeds back to the sensor, so the alternation is the nRF's
  own behaviour. One possibility, a two-range or auto-gain reading, is a guess. Both bands give
  10 %, so the stock backlight does not hunt.
- **Not found:** the consumer of app message `0x15`. `BackLightWakeUpHandle` does not use the light
  value (section 5). So I could not confirm that the stock firmware drives the backlight from this
  sensor at all.

### 3.4 A charge-state bit (new)

- **Read:** id 16 / `0xF0` / `0x02` posts payload[2] as event 3 (`0x42051200`). The dispatcher sends
  event 3 as app message `0x14` (`0x42050a48`). The factory screen shows message `0x14`'s first word
  as **"Chargeing" when it is 0 and "No Charge" otherwise** (`0x42072cb4`).
- **Inferred:** id 16 / `f0 02 <c> …` with **c == 0 means charging**. On the wire it was
  `f0 02 00 …` with the unit on USB, which agrees. It still wants the unplug test.

---

## 4. The forwarded ids are ANT+ device types

**Read:** two converters map the nRF message id to and from a device slot. `0x422b2438` maps id →
slot. `0x422b24a8` maps slot → id. Their callers are `0x421a8404`, `0x421a88d8`, `0x421a8a1c` and
`0x421a8b70`.

| id | hex | slot | ANT+ device type with that number (inferred from the public ANT+ list) | Stock module |
| --- | --- | --- | --- | --- |
| 120 | `0x78` | 0 | Heart rate | `ant_hrm` |
| 122 | `0x7A` | 1 | Bike cadence | `ant_bsc` |
| 123 | `0x7B` | 2 | Bike speed | `ant_bsc` |
| 121 | `0x79` | 3 | Bike speed and cadence | `ant_bsc` |
| 11 | `0x0B` | 4 | Bike power | `mg_ant_bpwr` |
| 17 | `0x11` | 5 | Fitness equipment (FE-C trainer) | `ant_fec` |
| 40 | `0x28` | 6 | Bike radar | `ant_rdr` |
| 34 | `0x22` | 7 | Shifting | `ant_shft` |
| 128 | `0x80` | 7 (same slot as 34; the reverse map gives 34) | not a public ANT+ type; **inferred** Shimano's private Di2 profile | `ant_di2` |
| 35 | `0x23` | 8 | Bike light | `ant_tail_light` |

**Read:** the dispatcher posts these as event 5. `0x42057be4` packs `{1, id, 8 payload bytes}` into
"AntQueue". So **each forwarded frame is one device's 8-byte ANT data page, tagged with the device
type**. Messages are dropped while the upgrade or test flags are set. **Not traced:** the AntQueue
reader (the queue handle at `0x3c70c270` has only a sender and a flush as direct references), and the
per-profile decoding of the pages. Note that the code uses the ANT+ device-type numbers themselves
(120–123, 11, 17, 34, 35, 40), which is why the dispatcher's set looked irregular.

---

## 5. Backlight

| Function | What it does (read) |
| --- | --- |
| `0x42031e0c` set backlight(pct) | `f = pct × 0.7` (double `0.7` at `0x42001638`). `duty = f × 1024 / 100`: **10-bit duty, capped at about 70 %** (716/1023 at 100 %). LEDC speed mode 0, channel 0. If `f ≤ 40` (pct ≤ 57): `ledc_set_fade_with_time(0, 0, duty, 40 ms)` + `ledc_fade_start(0, 0, 0)` (`0x4222f1fc`, `0x4222f288`); else `ledc_set_duty` + `ledc_update_duty` (`0x4222f030`, `0x4222ecd0`). Branch at `0x42031e61`. The LEDC function names are inferred from argument counts and order. |
| `0x420362dc` get setting | the user brightness byte (`& 0x7f`) from the settings record at `+0x77`; **0 reads as 10** |
| `SetScreenLightSetValue` `0x42036350` | stores the setting, clamped to ≤ 100 |
| `ScreenPowerOnHandle` `0x421552f8` | backlight = max(setting, **5**) |
| `BackLightWakeUpHandle` `0x42155100` | if the screen was dimmed (flag at `+4`): backlight = setting clamped to 1…100, log `Light=%d,K=%f` with K = 1.0. Then it enforces a minimum of K (1 %). **It does not read the light sensor.** |
| `BackLightOffHandle` `0x4215507c` | saves the current level, sets flag `+4`, backlight 0 |
| `ScreenPowerOffHandle` `0x42155288` | used on the power-off path; passes the level to `0x42141698` (not traced) |

So the stock "auto-brightness" from the light sensor is not in the wake-up path. Whatever consumes
message `0x15` is still unfound.

---

## 6. Where `fw/main/` differs from the vendor

| File / function | Ours | Stock | Weight |
| --- | --- | --- | --- |
| `hal_gnss_esp.c` `hal_gnss_init` | sends GNSS **power-on** at boot | sends **no** GNSS power frame at boot; on only from `GpsOpen` after a `GpsClose` | low: on is idempotent, as far as is known |
| `hal_gnss_esp.c` `hal_gnss_poll` | after **3 s** of silence: reset, then **power-on 200 ms later**, once | after **≈5.1 s** of silence: **reset only** (`e2 02 07 00 00 02`), then re-run the `$PAIR` sequence; repeats about every 5 s | medium: our extra power-on after reset is not what the vendor does |
| `hal_gnss_esp.c` | RX only; sends no `$PAIR` | TX on GPIO 1: `$PAIR867,0,0` → (`$PAIR866,0,0,0` if flow control is on) → `$PAIR490,1` → `$PAIR491`, each retried until acknowledged | medium: EASY prediction and flow control off are vendor choices. NMEA output works without them, because the 17 Nov capture parsed fixes. |
| `nmea.c` | RMC fix valid at once | valid only after **3 consecutive** valid RMCs; GGA altitude averaged (15) and gated on ≥ 6 satellites and VDOP ≤ 5 | low/medium |
| `nrf_proto.c` `fw_nrf_is_power_on_event` | id 16, `e2 02`, payload[5] 5 or 6 | same codes; the vendor also drops kind 5 with payload[5] = 0 and **ignores the device byte** | none |
| `nrf_proto.h` comment / `NRF-PROTOCOL.md` | code 4 = "the other event" / power-off request | code 4 = `DEVICE_OFF_CHARGE` (woken by the charger) | doc fix |
| `hal_nrf_esp.c` `nrf_task` | at start: `SendPowerOnCmd` + GNSS on; up to 5 more retries 1 s apart while no barometer | `SendQuerySlaveModeCmd` every 200 ms **until id 1 `24 01 … [5]=1`**, then `SendPowerOnCmd` every 100 ms **until an `e2` event**, plus `SendCheckPowerOnReasonCmd` every ≈30 s | medium: we never ask the slave mode, so a co-processor stuck in its bootloader would look like silence |
| `hal_nrf_esp.c` `hal_power_off` | backlight 0, GNSS off, **one** `SendPowerOffCmd`, then wait for a wake | close files and free buffers, tone, 1 s, `SendPowerOffCmd`, 0.5 s, **repeated every ≈1.5 s**; no GNSS-off | **high for FAT**: the vendor closes files before cutting power. Repeating the command is cheap insurance. |
| `hal_nrf_esp.c` `hal_power_charging` | always 0 | id 16 `f0 02 <c>`: c == 0 → "Chargeing" on the factory screen | gain: implementable after one unplug test |
| `nrf_proto.c` `fw_nrf_light_backlight_pct` | "≤ next threshold" bands | `[v_i, v_{i+1})` → `pct_i` | none for the band values the nRF sends (0, 10, 30, 50, 600, 2000, 6000, 9000 give the same result); values in between would differ by one step |
| `hal_display_esp.c` `hal_display_backlight` | 100 % → duty 1023 | 100 % → duty ≈716 (×0.7 cap); fades 40 ms below ≈57 % | low: brighter and hotter than stock at the top end |

---

## 7. What I could not determine

- What `$PAIR470` and `$PAIR590` do, and what the `e2 02 08` factory frame does.
- Whether `GPSIC_DEAD_STATUS` can be reached at all (1.4).
- The exact span of the Airoha binary checksum, and the A-GNSS file layout beyond what is listed
  (what the 23 + 4 + 4 bytes are, and how segments map to days).
- Which UI event opens `SystemPop_ManualPowerOff`; that it is the power-held code `0x34` is inferred.
- What key event 6 is (it is handled for all five key indices; never seen on the wire).
- Which part produces the 0.01 °C temperature on `0xF1` / `0x0a`.
- Why the nRF alternates 600/≈1590 and 2000/≈2 for the light sensor; that is decided on the nRF side.
- Who consumes app message `0x15` (the light record with its backlight percent).
- The AntQueue reader and the per-profile decoding of the 8-byte pages.
- What the first byte of the id 1 version message (`0x01`, stored at `0x3fca1310[0]`) means.
- The backlight PWM frequency used by the stock firmware (the LEDC timer configuration was not read).
