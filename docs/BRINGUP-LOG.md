# CC700 bring-up log

What actually happened, step by step, as `docs/BRINGUP.md` is worked through. The plan is that
document; this one is the record. Every command that touched the unit was typed by Daniel
(START-HERE rule 1). Nothing has been written to the unit.

Started 4 November 2026.

---

## Step 1 · Preserve the separate 4 GB storage · **blocked on a decision**

**No removable card.** Daniel confirmed there is no SD slot anywhere on the outside of the case, and
the case is sealed (IPX7), so there is nothing to take out and read. `backup_report.py` agrees with
that: the firmware's storage strings are `SDMMC` (45 hits, including `vfs_fat_sdmmc.c`,
`diskio_sdmmc.c`, `sdmmc_cmd.c`) and `EMMC`, i.e. a soldered eMMC on the SDMMC bus, not a card.

**USB offers no storage.** With the unit switched on and plugged into the PC it **beeps, the screen
does not change, and no drive appears**. Windows enumerates it as one composite device with two
interfaces and nothing else:

```
USB\VID_303A&PID_1001\DC:B4:D9:25:0F:10        USB Composite Device
USB\VID_303A&PID_1001&MI_00\…                  USB Serial Device (COM3)
USB\VID_303A&PID_1001&MI_02\…                  USB JTAG/serial debug unit
```

`303A:1001` is Espressif's built-in **USB-Serial/JTAG** controller, and the serial number is the MAC
from the handover, `dc:b4:d9:25:0f:10`, so this is certainly the unit. Interfaces 0 and 2 are the
CDC serial port and the JTAG debug unit: the standard ESP32-S3 USB-Serial/JTAG composite. **There is
no mass-storage interface**, and `Win32_DiskDrive` lists only the PC's two SSDs.

That is consistent with the firmware image. It does contain TinyUSB mass-storage code
(`tusb_msc_init(&msc_cfg)` in `./Modules/Middlewares/MidUsb/MidUsb.c`, alongside CDC-ACM), but a
search of all 38,971 strings in the 16 MB image finds **no USB-related interface text at all** — no
"USB", "U-disk", "mass storage", "file transfer" or equivalent in any of the nine UI languages. The
only `ModeSwitch` strings belong to Di2 gear-shift modes. So the MSC code is compiled in but appears
to be unreachable in normal operation: on the ESP32-S3, mass storage would have to come up on the
USB-OTG controller with a different PID, and it does not.

**Where that leaves the 4 GB.** It holds the stock maps, rides and settings and has never been
backed up. Remaining options, which is Daniel's decision:

- (a) Go ahead without a copy. The custom software only ever writes inside a `CC700/` folder of its
  own and never formats or repartitions, but a FAT corruption would be unrecoverable.
- (b) After step 3 identifies the storage bus, a RAM-only reader dumps it over serial. It writes
  nothing, but 4 GB at serial speeds takes many hours.
- (c) Keep hunting for a hidden mode: the unit's own menus, the official manual, or GEOID support.

**Still to check:** the unit's own menus for anything about USB, storage or export, and whether
holding a button while plugging in changes what enumerates (that test is folded into step 2).

---

## Step 2 · Prove the BOOT/RESET recovery entry · **in progress**

**Toolchain rebuilt first.** The isolated Python that made the backup was no longer on the PC, and
Windows had only Microsoft Store stubs. esptool has to run on Windows because WSL 2 cannot see COM
ports. Rebuilt at `D:\cc700-toolchain\python-esptool\` from the embeddable CPython 3.12.8 — nothing
on PATH, nothing in the registry, deletable in one step — with **esptool 5.6.0 and pyserial 3.5**,
the exact versions in the backup's `capture-tool-requirements.txt`.

**Backup verified before anything else.** `original-flash-16MB.bin` hashes to
`8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7`, matching the handover.

### 2.1 · The normal route works — and needs no button

```
esptool v5.6.0
Connected to ESP32-S3 on COM3:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                dc:b4:d9:25:0f:10
Stub flasher running.
WARNING: ESP32-S3 has no chip ID. Reading MAC address instead.
MAC:                dc:b4:d9:25:0f:10
Hard resetting via RTS pin...
```

Read-only, and the stub flasher runs from RAM, which is what the original backup did. Nothing was
written to flash.

Three things this settles:

- **esptool can reset the unit into download mode by itself, with no button held.** On a chip with
  native USB-Serial/JTAG that reset goes through the USB peripheral rather than a DTR/RTS circuit
  ("Hard resetting via RTS pin" is esptool's generic wording). This is the convenient route, and it
  is exactly the route that disappears if custom firmware crashes early or takes the USB peripheral
  somewhere else — which is why 2.2 still has to find a physical entry before anything is flashed.
- **`USB mode: USB-Serial/JTAG`** confirms the native controller, matching the Windows enumeration.
  `fw/sdkconfig.defaults` sets `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and the tests forbid TinyUSB,
  so the custom firmware keeps this route working by construction.
- The chip, revision, PSRAM and MAC all match the handover.

### 2.2 · No physical download-mode entry found — but a factory menu was

Holding a button and plugging in gives the **charge screen** every time, for each of the three
buttons. That is decisive on its own: a charge screen means the application booted, so the ROM did
not enter download mode and GPIO0 was not held low at that reset.

**Two bottom buttons held with power opens a hidden factory test menu** (`AppFactoryManage` in the
firmware). `--before no-reset --after no-reset chip-id` still times out there, as expected: it is a
firmware mode, not the ROM.

Its screens, read out of the image, are:

1. `MAC QR` — MAC and a QR code
2. `PWM TEST` — `BackLight:`, `ScreenLight:`, `Environment Light:`, `LightLux:`
3. `RECOVERY FUNC TEST` — the version block below, **with a `FactoryData Reset?` /
   `[PRESS B TO RECOVERY]` prompt on the same screen**
4. `FACTORY UPDATE TEST` — `Factory Update?` / `[PRESS B TO Update]` / `No file to Updating`

**Screens 3 and 4 are destructive if B is pressed**: the factory reset is described elsewhere in the
image as "all data will be cleared and all settings will be reset", and the update flashes from a
file. Those are the two things that could destroy the un-backed-up 4 GB. Only navigate; never press B.

### Hardware inventory, read off `RECOVERY FUNC TEST`

| Field | Value | What it means |
| --- | --- | --- |
| `SPUVer` | 1.711 | rendered `%d.%d%02d`, so **1.7.11** |
| `ESP32Ver` | 1.711 | same build |
| `52810Ver` | 1.704 | **1.7.4** on a part the firmware calls `52810` |
| `GPSVer` | V3.13 | GNSS firmware |
| `TOUCHVer` | `3240: 0x1004` | touch controller |
| `SPUHardVer` | V1 | board revision |
| `HardwareVer` | V1 | board revision |

From `PWM TEST`: **`LightLux: 600`** and **`ScreenLight: 10`**, so there is a working ambient-light
sensor and a PWM backlight — both of which the spec assumes.

What this adds to `docs/STOCK-FIRMWARE-FINDINGS.md`:

- **The co-processor is a 52810.** That document could only call it "an nRF co-processor". With 67
  ANT+ strings and a UART link, an **nRF52810** is the obvious reading — Nordic's part that carries
  ANT. Still inferred from a version label, so Unverified until the board is seen.
- **Two touch controllers are supported, and this unit has the `3240`.** The firmware prints either
  `6336: 0x%02x` or `3240: 0x%x%x%x%x`; this unit answers on the 3240 path with `0x1004`. `6336`
  reads as a FocalTech FT6336; `3240` is most likely a Hynitron CST3240. Both Unverified.
- **The GNSS is Airoha-family.** Independent of the version number, the image carries `$PAIR001`,
  `$PAIR491` and `$PAIR867` command strings, which are Airoha's command set.

**A correction to the earlier report.** `backup_report.py` lists `AXP` with 2 hits, which I nearly
took for an AXP power-management IC. Both are false positives inside the format string
`%s: MaxPathNum:%d` — "M-**axP**-athNum". There is **no evidence of an AXP PMIC**, and in fact a
search for every common barometer, IMU, PMIC, charger and light-sensor part number
(SPL06, BMP/BME, LPS2x, DPS3xx, QMI8, ICM, MPU, AXP, BQ, CW201x, LTR, BH17xx, AP3216, STK) finds
**nothing** in all 38,971 strings. The vendor's drivers do not name their parts, so the sensors will
have to be identified by an I2C scan during step 3, not from the image.

### An open question the screen raised: does the backup still match the unit?

The backup's `ota_0` app descriptor says `CC700Pro_V1.710-2-g44e3c9e-dirty`; the unit now displays
**1.7.11**. That may be nothing — "V1.710-2" is two commits past tag V1.710, and a version constant
bumped to 1.7.11 in those two commits would explain it exactly. But the alternative is that the unit
has taken an update since 2 November, and that matters a great deal: **the whole plan assumes
`ota_1` is empty and `otadata` points at `ota_0`**. An OTA update would have written the new firmware
into `ota_1` and repointed `otadata` at it. Writing our app into `ota_1` would then overwrite the
unit's current firmware.

Settled by reading three small regions off the unit, read-only (see next).

### The backup still describes the unit — checked on the device, read-only

Three regions read off the unit with `read-flash` (writes nothing; esptool resets into download mode
by itself) and compared with the 2 November image:

| Check | Result |
| --- | --- |
| `ota_0` first 256 bytes vs backup | **byte-for-byte identical** |
| `ota_0` app descriptor | `CC700Pro_V1.710-2-g44e3c9e-dirty`, `Project`, 14:08:30 Nov 20 2025, IDF `423e6536` — same in both |
| `ota_1` first 4096 bytes on the unit | **all `0xFF`** — still completely erased |
| `otadata` 8 KB vs backup | **identical**: copy 0 seq `0x00000001` state `0x00000002` (valid), copy 1 erased |

So **no OTA update has been taken**, and the `1.7.11` on the factory screen is the benign
explanation after all: a version constant bumped in the two commits past tag `V1.710`, not a newer
firmware. An update would have had to write an image header at the start of `ota_1` and repoint
`otadata`; neither has a bit out of place.

Every precondition step 4 depends on — `ota_1` erased, `otadata` naming `ota_0`, the partition table
and bootloader untouched, the backup matching — now holds **as measured on the unit**, not only as
recorded in a file from two days earlier.

## Step 3b · RAM probe, first run · one button found

The probe loaded and ran from RAM on the first attempt. Download mode was entered by esptool itself
(`--after no-reset --no-stub load-ram`), since step 2 found no physical entry. Nothing was written to
flash; the unit returns to stock on a power-cycle.

```
CC700 probe (RAM only, read only)
chip: model 9, revision v0.2, 2 cores, features 0x00000012
reset reason: 11
initial levels: 0=1 1=1 2=0 3=0 4=0 5=0 6=0 7=0 8=0 9=0 10=0 11=0 12=1 13=1 14=1 15=1
                16=1 17=1 18=1 21=1 38=0 39=1 40=1 41=1 42=0 43=1 44=1 45=0 46=0 47=0 48=0
watching for 120 s
  55975 ms  GPIO 41  1 -> 0        (ten transitions in 130 ms: one press with contact bounce)
  …
  56105 ms  GPIO 41  0 -> 1
```

**Found: a button on GPIO 41, active low** (idle high, pressed low). Which of the three it is still
has to be confirmed — the first run was not narrated.

**Idle levels.** High: 0, 1, 12–18, 21, 39, 40, 41, 43, 44. Low: 2–11, 38, 42, 45–48. GPIO 19 and 20
(USB) and 26–37 (flash and PSRAM) are deliberately not watched. 43 and 44 reading high is consistent
with the default UART0 pins.

**GPIO 0 idles high and never moved**, so none of the buttons pressed in this run is the boot strap.

**Three of the four actions produced nothing**, which has several possible explanations and the next
run is designed to separate them:

- the actions may simply have fallen outside the window, or been too light;
- **the power button very probably does not reach the ESP32 at all** — on almost every design it goes
  to the power-management IC, which is why a long press can cut power;
- **the touch controller is not initialised during the probe**, so it has no reason to pulse its
  interrupt line. Touch needs its rail and I2C configuration first, which this probe deliberately
  does not do (it never drives a pin).


## Step 3b · Second run · the unit stopped responding, and two findings were wrong

**What happened.** The second probe run completed its full 120 s, but Daniel pressed nothing during
it ("I didn't get to test anything") and afterwards **the unit would not turn on** — dark screen, no
response to the power button. `miniterm` ended with a `SerialTimeoutException` as the probe's USB
went away.

**Recovery, and what it proves.** `esptool chip-id` connected normally and its closing hard reset
brought the unit back to the stock firmware. Nothing had to be reflashed. This is the first time the
USB-Serial/JTAG recovery route has been needed rather than merely tested, and **it worked**: the
route step 2 could not establish with a button turns out to be good enough to recover a unit that
will not boot on its own.

**Nothing was ever written to flash.** The whole session is `chip-id`, three `read-flash` reads and
two `load-ram`. `load-ram` writes to RAM, which any power loss clears. `ota_0`, `otadata` and the
partition table are as verified against the backup earlier the same day.

**Likely cause, not confirmed:** for about two minutes the unit ran a 104 KB probe instead of its own
firmware. The probe never drives a pin and cannot damage anything, but neither does it do any of the
housekeeping the stock firmware does — including whatever keeps the power-management IC awake. A
PMIC that latches off without a periodic kick from the host would produce exactly this symptom. Worth
designing around before any further RAM app is run for minutes at a time.

### Correction: GPIO 41 is not a button, and GPIO 0 is driven

The first run's single burst on GPIO 41 was recorded here as "a button, active low". **That was
wrong.** In the second run GPIO 41 produced bursts at 21.5, 34.5, 47.5, 58.8, 71.8, 84.8, 97.7 and
110 s — **about 13 seconds apart, while nothing was being pressed.** Nothing a thumb does is that
regular. It is periodic traffic on a signal line, sampled coarsely by the probe's polling; the most
likely source is the UART to the nRF co-processor sending a heartbeat, which fits the `MidComm`
layer and `mSysTimeStampFromNrf` already found in the firmware.

**GPIO 0 is actively driven by something.** It produced roughly three solid seconds of edges from
5.6 s to 8.5 s and then went quiet. That is not a strapping resistor sitting idle. What drives it is
unknown.

**So no button has been identified.** Two possibilities worth separating next: the presses simply
fell outside the window, or **the buttons are wired to the nRF rather than to the ESP32** and arrive
over that UART — which would also explain why a button combination reaches a firmware factory menu
but nothing reaches the ROM.

### What this says about the probe's design

A 120 s window with no feedback is poor: there is no way to tell a button edge from line traffic
while it runs, and no summary at the end. Before another live run it should: settle in 2 s, run for
about 45 s, print a per-pin edge count every 10 s, and mark pins whose edges are periodic so traffic
is visually separated from presses.


## Step 3a (offline) · The display is fully decoded from the stock firmware

Daniel chose option A: read the vendor's own initialisation code instead of probing the live unit.
This is all static analysis of `ota_0` in the backup — **the unit was not touched**.

**Method.** The vendor built with `ESP_ERROR_CHECK`, whose macro stringifies its argument, so the
image contains the literal text `esp_lcd_new_i80_bus(&bus_config, &i80_bus)` and friends. Parsing the
app image header gives the segment map (DROM `0x3c2d0020`, IROM `0x42000020`, IRAM `0x40374000`),
which turns a file offset into a virtual address. Scanning IROM for `l32r` instructions whose
computed target is the literal holding each string's address locates the exact call sites. The
display init sits at `0x42031d94`–`0x4203203x`, and the struct fields are plain `movi`/`s32i` pairs
that can be read straight off.

### Display: ST7789 on an 8-bit Intel i80 bus

| Signal | GPIO | Where it comes from |
| --- | --- | --- |
| DC (command/data) | **40** | `esp_lcd_i80_bus_config_t` +0 |
| WR | **3** | +4 |
| D0–D7 | **4, 38, 5, 48, 6, 47, 7, 11** | +12…+40, `bus_width = 8` at +76 |
| RD | **39** | `gpio_config` with `pin_bit_mask` high word `0x80`, mode OUTPUT, then driven high |
| CS | **2** | `esp_lcd_panel_io_i80_config_t` +0 |
| Backlight | **45** | `ledc_channel_config_t.gpio_num` |
| Reset | **none** | `esp_lcd_panel_dev_config_t.reset_gpio_num = -1` |

Settings: **pclk 15 MHz** (`0x00e4e1c0`), command and parameter widths 8 bits, `dc_data_level = 1`
(DC low for command, high for data), transaction queue depth 10, **16 bits per pixel (RGB565)**,
backlight PWM **20 kHz at 10-bit resolution** on LEDC low-speed timer 0 channel 0.

`bus_width = 8` landing exactly at `+12 + 16×4` is what confirms the field alignment: the data array
is 16 ints wide in the struct and only the first eight are set, so the offsets cannot be
coincidental.

**GPIO 43 and 44 are driven high immediately before the bus is created** (set to output, level 1).
They are not part of any of the three config structs. Panel power enable and panel reset are the
obvious candidates — `reset_gpio_num` being `-1` means the panel's reset is *not* handled by
`esp_lcd`, so something else must do it. Unconfirmed.

**Status: Unverified on hardware.** This is read from the stock firmware's own code, which is far
stronger than a guess, but no pin has been measured and our driver has not lit the panel. It stays
Unverified until it does.

**What it changes.** `hal/hal_display.h` says "ST7789 per the firmware strings; the bus and pins are
Unverified". The bus is now known and it is **not SPI** — an i80 parallel bus needs a completely
different driver, `esp_lcd_new_i80_bus` rather than an SPI panel IO. Writing that driver no longer
needs the unit at all.


### The two UARTs — and what the probe was actually seeing

Same method, from `uart_set_pin(UartNum, UART0TXDPIN, UART0RXDPIN, -1, -1)`. The function at
`0x420452a0` takes a selector and branches:

| Vendor name | ESP port | TX | RX |
| --- | --- | --- | --- |
| UART0 | 0 | **GPIO 1** | **GPIO 0** |
| UART1 | 2 | **GPIO 42** | **GPIO 41** |

`rts` and `cts` are `-1` on both, so neither uses hardware flow control.

**This explains both of the probe's mysteries, and neither was a button.**

- **GPIO 41 is a UART RX.** The bursts every ~13 s with nothing being pressed are **incoming bytes**,
  not contact bounce — the nRF co-processor's heartbeat over `MidComm`, sampled coarsely by a polling
  loop. Retracting the "button on GPIO 41" finding was right, and now there is a mechanism for it.
- **GPIO 0 is a UART RX too.** The three solid seconds of edges from 5.6 s to 8.5 s are incoming
  serial data, then silence. The obvious source is the GNSS streaming NMEA until its power rail drops
  — our RAM probe asserts nothing, so whatever normally keeps the receiver powered was never set.

**A hypothesis for the unit that would not turn on.** GPIO 0 is both the UART0 RX line *and* the
ESP32's boot strapping pin. If the device on that line were transmitting across a reset, it would
hold GPIO 0 low at exactly the wrong moment and the ROM would enter download mode: dark screen, no
firmware, "won't turn on" — and recoverable by precisely what recovered it, an esptool reset. This is
a hypothesis, not a conclusion, but it fits every observation and it means the incident was likely
benign rather than a power fault.

It also explains why no button is on GPIO 0: the pin is taken.


### Buttons, touch and I2C — what the firmware does and does not say

**Every `gpio_config`, `gpio_set_direction`, `gpio_set_level` and `gpio_reset_pin` call site in the
image was enumerated** by the same literal-and-`l32r` method: 2, 4, 5 and 6 sites respectively. All
the ones with immediate pin numbers belong to the LCD init already decoded. The rest are **generic
wrappers** — the pin arrives in a register (`sll a10, a2` to build `1 << pin`), so the vendor keeps
pin numbers in a table rather than inline, and they cannot be read straight out of the code.

**There is no `gpio_install_isr_service` anywhere in the firmware.** The ESP32 never takes a GPIO
interrupt. So the buttons are either polled, or they do not reach the ESP32 at all.

`Btn1`…`Btn5` in the strings are **LVGL widget demo text**, not this unit's buttons. A false lead.

**Touch is on I2C, from the ESP32.** `MidLcd` is followed by an I2C device manager whose messages are
`Dev Handle Is NULL,Port:%d,Addr:0x%x`, `New Dev Addr,Port:%d,Addr:0x%x`, `Dev List Is Full,…`, plus
`Touch driver init failed`, `touch screen is on`/`off` and `X=%u,Y=%u`. The `Port`/`Addr` pair and the
"Dev Handle" vocabulary match ESP-IDF's newer `i2c_master` driver with a per-device handle.

In the touch init at `0x42032230` the code does `movi a10, 90` and calls `0x42032c74`. **90 is
`0x5A`**, which is the I2C address of the Hynitron CST3240 family — consistent with the factory
screen reporting `TOUCHVer 3240: 0x1004`. Treat the address as a strong candidate rather than a fact
until an I2C scan confirms it; the register could be an argument of another kind.

**Still unknown after this pass:** the SDA and SCL pins, the barometer, the ambient-light sensor
(which demonstrably works — the factory screen read 600 lux), battery and charger sensing, the
SD/eMMC pins, the GNSS power enable, and what GPIO 43 and 44 do.

### Pin map so far

| Function | GPIO | Confidence |
| --- | --- | --- |
| LCD DC | 40 | from vendor code |
| LCD WR | 3 | from vendor code |
| LCD RD | 39 | from vendor code |
| LCD CS | 2 | from vendor code |
| LCD D0–D7 | 4, 38, 5, 48, 6, 47, 7, 11 | from vendor code |
| LCD backlight | 45 | from vendor code |
| UART0 TX / RX | 1 / 0 | from vendor code |
| UART1 (ESP port 2) TX / RX | 42 / 41 | from vendor code |
| unexplained, driven high at LCD init | 43, 44 | observed, purpose unknown |

Seventeen of the chip's pins are now accounted for, none of them measured on the board. Everything in
this table is Unverified until our own driver uses it.


## The real display driver is written — `fw/main/hal_display_esp.c`

Written entirely from the decoded configuration; **the unit was not involved**.

`hal/hal_display.h` previously described an ST7789 whose "bus and pins are Unverified" and
`fw/main/hal_esp.c` answered `hal_display_flush` with "not implemented". Both are replaced:

- **`fw/main/board_pins.h`** holds every pin, with the provenance and the Unverified status stated at
  the top of the file so nobody downstream mistakes a decode for a measurement.
- **`fw/main/hal_display_esp.c`** brings up the LEDC backlight, drives the two unexplained lines and
  RD high in the stock firmware's order, creates the i80 bus, the panel IO and the ST7789, and
  implements the flush.
- `hal_display.h` gains `hal_display_init()` and `hal_display_backlight(pct)`; `main.c` calls the
  first before `lv_init()` and logs if it fails rather than dying.

**One design point worth recording.** `hal_display.h` promises that `hal_display_flush` "returns when
the buffer may be reused", but `esp_lcd_panel_draw_bitmap` is asynchronous: it queues a DMA transfer
and returns, which would let LVGL start redrawing a buffer still being clocked out, and the tearing
would have looked like a wiring fault. The driver therefore takes a binary semaphore given from the
transfer-complete callback, so the flush blocks until the DMA is done. That honours the documented
contract and leaves `main.c`'s existing `flush_cb` correct as it stands.

**An independent check on the decode.** ESP-IDF v5.5.6's own `esp_lcd_i80_bus_config_t` has
`dc_gpio_num`, `wr_gpio_num`, `clk_src`, `data_gpio_nums[16]`, then `bus_width` — field for field
where the stock firmware's stores landed, including `bus_width` at `+12 + 16×4`. The vendor's IDF and
ours lay the struct out identically, which is strong corroboration that the pins were read correctly
rather than off by a field.

**Build:** `make fw-build` passes, `app.bin` **1,183,040 of 8,192,000 bytes (14 %)**, 36 KB larger
than before for `esp_lcd` and LEDC, no warnings. `make fw-check` now covers 26 sources. Host tests
unaffected, 16 of 16.

**Still Unverified, and this is the point:** nothing here has driven a pin on the real board. The
first time this driver runs is bring-up step 4, and the honest list of what could still be wrong is:
the RGB element order (RGB assumed, BGR is equally likely and shows as swapped colours), the panel's
rotation and offsets, whether GPIO 43 and 44 really are power and reset, and whether the panel needs
a reset pulse this driver does not give because `esp_lcd`'s `reset_gpio_num` is `-1`. All are
cheap to correct once there is something on the screen to look at.


## Step 4 · First write · **the screen works**

`app.bin` (1,183,312 bytes, stamped `7211bc9`) was written to `ota_1` at `0x7f0000`, verified, and
`otadata` switched to select it. Daniel reports: **the layout is right, the colours are inverted.**

That is the headline result of the whole exercise. **Right layout means the entire i80 decode was
correct** — the 8-bit bus, all eight data lines, DC, WR, CS, the backlight and the 15 MHz pixel
clock, every one of them read out of the vendor's compiled firmware and none of them ever measured.
A single wrong data line would have given garbage, not a correct screen in the wrong colours.

**First fact confirmed on hardware rather than read from the firmware: this panel needs colour
inversion on.** ESP-IDF's stock ST7789 driver leaves inversion off; the stock firmware uses its own
fork (`mg_esp_lcd_panel_st7789.c`), which evidently turns it on. Fixed by
`esp_lcd_panel_invert_color(panel, LCD_INVERT_COLOR)` with `LCD_INVERT_COLOR` defined `true` in
`board_pins.h`. The element order stays RGB, because the stock firmware passes 0 for it and the
layout and brightness were otherwise right.

**Also learned: the power button does nothing under our firmware.** `hal_input` is still a stub, and
the button almost certainly reaches a power-management IC that the stock firmware services and ours
does not — so the unit cannot be switched off or power-cycled by hand while running our image. The
way to reset it is esptool (`chip-id` ends in a hard reset). Worth remembering: until the power path
is understood, **our firmware leaves the unit unable to turn itself off**, so it should not be left
running on battery.

Still untouched and still true: `ota_0` holds the stock firmware, and writing `otadata-now.bin` back
to `0xd000` returns the unit to it.

