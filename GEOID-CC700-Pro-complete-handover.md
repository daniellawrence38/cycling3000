# GEOID CC700 Pro — complete project handover

Prepared **2 November 2026, Australia/Perth**.

This document consolidates the hardware inspection, firmware analysis, backup evidence, research, owner requirements, development proposals, and unresolved questions gathered in this chat. The binary backup and raw evidence remain separate files; the document records their locations and key contents.

## Current status

- The device is a GEOID CC700 Pro cycling computer, identified by the owner and supported by firmware metadata.
- The complete **16 MB SPI firmware flash was captured and verified against the physical device**, with a matching digest.
- **The whole-device backup is incomplete.** The separate storage used for maps, rides, settings, fonts, and other files has not been copied.
- **Restoration has not been tested.** We expect the saved SPI image to support restoration on this same unit while hardware/security conditions remain compatible, but full recovery cannot yet be guaranteed.
- No replacement firmware was installed, no SPI flash was erased or rewritten, and no eFuses were changed. Espressif's temporary helper was uploaded to RAM for reading and verification.
- No Android companion, custom firmware, peripheral driver, or performance benchmark has been implemented. Targets and architecture below are proposals.
- The owner paused work and was told the device could be disconnected after device operations finished. This document was prepared from saved information without resuming hardware operations.

## Owner requirements and decisions

1. “Clone” means backing up this unit so it can return to the original software, rather than duplicating it onto a second unit.
2. Target phone platform: **Android**.
3. Automatically upload completed rides to Strava.
4. Phone connection for navigation and official Strava Live Segments, if licensed access is available.
5. Maps with roads and cycle paths.
6. Automatic route recalculation after going off course.
7. Direct GEOID PM500 power-meter support, preserving power/cadence in recordings and uploads.
8. **Better overall responsiveness is essential** for the replacement to be worthwhile.
9. Other useful features are welcome as suggestions, rather than mandatory scope.

The owner believes files are transferred through the phone app. A user-accessible USB storage mode has not been confirmed.

## Hardware confirmed on this unit

| Item | Observed value |
| --- | --- |
| Chip | Espressif ESP32-S3 |
| Silicon revision / package | v0.2 / QFN56 |
| Processor capability reported | 240 MHz; application operating frequency was not measured |
| PSRAM | 8 MB in the chip package; esptool reported AP_3v3 |
| SPI flash capacity | 16,777,216 bytes: 16 MiB, reported by esptool as 16 MB |
| Flash manufacturer / device IDs | c8 / 4018; exact physical part marking not inspected |
| Flash configuration | Quad SPI, four data lines, 3.3 V according to eFuses |
| Crystal | 40 MHz |
| USB mode | USB Serial/JTAG |
| USB VID / PID | 303A / 1001 |
| MAC / USB serial identifier | dc:b4:d9:25:0f:10 |
| Windows serial port during capture | COM3; may change on another connection/computer |
| Secure Boot / flash encryption | Both disabled at capture |
| Security flags | 0x00000000 |
| SPI_BOOT_CRYPT_CNT | 0x0 |
| Security query chip ID / API version | 9 / 0 |

The security query reported key purposes (0, 0, 0, 0, 0, 0, 12), labeling BLOCK_KEY0 through BLOCK_KEY5 USER/EMPTY. The saved eFuse summary records readable configuration. It is not a restorable eFuse image and does not establish that protected secrets could be copied.

### Windows interfaces

| Interface | Class / service | Microsoft driver version | Status |
| --- | --- | --- | --- |
| USB Composite Device | USB / usbccgp | 10.0.26100.9711 | OK |
| USB JTAG/serial debug unit, interface 2 | USBDevice / WINUSB | 10.0.26100.9549 | OK |
| USB Serial Device, interface 0, COM3 | Ports / usbser | 10.0.26100.9278 | OK |

COM1 was the PC's own communications port, not the cycling computer. Windows did not expose a corresponding storage disk or mounted volume. The JTAG interface was enumerated, but no OpenOCD debugging session was established. Serial communication and flash readback were tested successfully.

## Standard ESP32-S3 capabilities

These are chip-family specifications, rather than measurements or confirmation that all interfaces are accessible on the GEOID board.

| Item | Specification |
| --- | --- |
| Main CPU | Two 32-bit Xtensa LX7 cores |
| Maximum clock | 240 MHz |
| Internal SRAM | 512 KB, in addition to this unit's 8 MB PSRAM |
| Internal ROM | 384 KB |
| Wi-Fi | 2.4 GHz 802.11 b/g/n |
| Bluetooth | Bluetooth 5 Low Energy |
| GPIO | Up to 45; some are used by memory and board functions |
| Interfaces | SPI, I²C, UART, I²S, LCD/camera, SD/MMC, USB OTG, USB Serial/JTAG, PWM, and others |

The exact chip ordering suffix, module part number, and board pin assignments remain unknown. A generic ESP32 development board with similar memory does not establish this board's wiring.

Sources: [Espressif ESP32-S3 overview](https://www.espressif.com/products/socs/esp32-s3), [ESP32-S3 datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf).

## Manufacturer-published product details

These are published specifications, not independently tested properties of the owner's unit.

| GEOID CC700 Pro item | Published information |
| --- | --- |
| Display | 2.8-inch TFT touchscreen, 240 × 320 pixels |
| Controls | Touchscreen and three buttons |
| Separate storage | Advertised 4 GB; actual capacity/type not measured |
| Wireless | ANT+, Bluetooth, 2.4 GHz Wi-Fi |
| Satellite systems | GPS, BeiDou, GLONASS, QZSS, Galileo; assisted-GNSS support |
| Other sensors | Barometer and light sensor advertised |
| Connection | USB-C |
| Battery life | Advertised 15–25 hours; not measured |
| Weight / water resistance | Advertised 104 g / IPX7 |
| Existing navigation | Offline maps, GPX routes, turn guidance, phone-assisted rerouting |
| Existing sync | Ride export and Strava/TrainingPeaks sync after account binding |
| Companion app | Onelapfit |

Source: [GEOID CC700 Pro specifications](https://geoid.com/en-gb/collections/frontpage/products/geoid-cc700pro). Existing sync and rerouting should be checked with the owner's Android setup before deciding what to replace. Stock support does not establish support in new firmware.

GEOID lists the PM500 with ANT+ and Bluetooth, power, cadence, left/right balance, and automatic/manual/backpedal calibration. It advertises ±1.5% accuracy, 0–2300 W power, 20–240 RPM cadence, up to 300 hours of battery life, magnetic charging, and IPX7 water resistance. Its pairing instructions say to rotate the crank to wake it, then select its ANT+ identity or Bluetooth signal. The manufacturer recommends zero calibration after initial installation or removal/reinstallation. No live readings, pairing, calibration, optional fields, or concurrent phone connection have been tested with the owner's meter.

Source: [GEOID PM500 specifications](https://geoid.com/en-gb/collections/frontpage/products/geoid-pm500-bike-power-meter-crankset).

## Serial access and reset observation

A three-second read at 115200 baud, eight data bits, no parity, one stop bit succeeded. DTR and RTS were configured disabled and no application data was sent. The boot log reported a USB_UART_CHIP_RESET. Opening this unit's serial port may therefore restart it even without application commands.

The received ROM banner was ESP-ROM:esp32s3-20210327, with build date 27 March 2021. This is the ROM date, not the cycling application's build date. The complete captured boot text appears in the evidence appendix.

## Firmware and storage findings

The application descriptor identifies:

| Field | Value |
| --- | --- |
| Version | CC700Pro_V1.710-2-g44e3c9e-dirt |
| Project | Project |
| Embedded build timestamp | 20 November 2025, 14:08:30; timezone unknown |
| ESP-IDF metadata string | 423e6536; not resolved to an SDK release |
| Descriptor location | ota_0 partition |
| Partition table | 0x8000; its MD5 record validated offline |

The exact version suffix was preserved as stored. The descriptor location does not establish which OTA slot was active during the last normal boot. The manifest appendix contains the full partition table and offsets.

Readable binary strings reference Espressif/ESP-IDF, LVGL, an ST7789 display driver, FAT filesystem, SD/MMC, TinyUSB CDC/MSC/HID, and GPS-version factory functions. These are clues rather than verified hardware wiring or usable commands.

Paths under /sdcard refer to MAP, FONT, GPS, EPHEMERIS, FITS, FITS/RECORD, CONFIG, USER, WIFI, ROUTE, ROUTES, NAVIGATION, SEGMENT, COURSE, APP, BOOT, AUDIO, LOG, and other working directories. Filenames reference ride databases, system/user information, sensor settings, Wi-Fi settings, and navigation configuration.

The presence of USB mass-storage code does not prove an accessible menu mode. Likewise, climb_plan_plate.bin, riding_mode_segment.bin, and /sdcard/SEGMENT do not prove usable climb analysis or Strava Live Segments. A time-related string mentioning Nrf was seen, but does not identify a specific radio coprocessor.

No complete GPIO map, general serial shell, supported storage-dump command, exact storage chip, or original source project was established. Initial public-source/schematic searches were inconclusive.

## Integration findings

- Strava documents activity upload formats including FIT, TCX, and GPX. FIT is proposed to preserve richer ride data including power and cadence. No developer app was registered, account connected, or activity uploaded. [Strava uploads](https://developers.strava.com/docs/uploads/)
- Official Strava Live Segments requires licensed integration access and a qualifying user subscription. An ordinary token and phone connection do not establish that entitlement. No license was obtained. [Live Segments documentation](https://developers.strava.com/docs/partner/strava-live-segments/)
- Strava's researched 2026 announcement introduced subscription requirements for Standard Tier developer access, limits Segments Explore access from September 2026, and announces API base-URL/token-handling changes for June 2027. Recheck the applicable tier, terms, and endpoints when implementation starts; older segment examples may be obsolete. [Developer-program changes](https://communityhub.strava.com/insider-journal-9/an-update-to-our-developer-program-13428)
- OpenStreetMap describes road-based cycle lanes/tracks, separate cycleways, and shared paths. Rendering and route calculation both need suitable coverage and access restrictions. Cached map images alone cannot calculate an offline reroute. A routing engine, provider, offline graph strategy, and distribution licence have not been selected. [Cycleway data](https://wiki.openstreetmap.org/wiki/Key:cycleway)

## How to use the remaining sections

The development plan below preserves the detailed feature requirements, PM500 behaviour, provisional responsiveness targets, proposed architecture, testing approach, and staged work. The recovery section preserves the backup scope and unexecuted restore procedure. Evidence appendices preserve the manifest, capture output, tool versions, and key boot observations.

The next work when the owner resumes is to preserve separate storage, establish reliable recovery entry, identify hardware/protocols, and measure stock responsiveness. No hardware/development work should resume merely because this handover has been created.


## Detailed development plan

### Feature proposal

| Feature | Proposed implementation and dependency |
| --- | --- |
| Automatic Strava uploads | GEOID already advertises this through account binding. For custom software, produce FIT activities, queue them on Android, and upload after explicit Strava OAuth authorization. Handle interrupted transfers, retries, and duplicate uploads. Current API access conditions must be checked when registering the application. |
| Official Strava Live Segments | Conditional: Strava documents licensed API access and a qualifying Strava subscription. A normal API token and phone connection do not by themselves provide this access. Do not make this a requirement for the first working firmware. |
| Personal timed sections | An alternative feature using the owner's own GPX/FIT recordings or manually defined sections: start/finish alerts and comparison to the owner's prior effort. This is separate from official Strava Live Segments. |
| Roads and cycle paths | Use OpenStreetMap-derived data with road/cycleway styling and a cycling routing profile. Displayed map coverage and the routing graph must both include relevant paths and access restrictions. Map/provider licensing and offline distribution support must be selected before implementation. |
| Automatic rerouting | Begin with routing on Android or a routing service. Detect sustained route deviation, recalculate from the current position, and send new geometry and turn prompts to the computer. Avoid repeated reroutes from GPS noise. Offline rerouting requires a routing engine and downloaded graph on the phone; cached map pictures alone cannot calculate a route. |
| Custom ride screens | Configurable fields, large high-contrast navigation prompts, configurable button actions, and views that remain usable with gloves. Requires verified display, touch, and button drivers. |
| GEOID PM500 power meter | Required: direct head-unit pairing, remembered sensor identity, automatic reconnection, power and cadence display/recording, and zero-offset calibration where exposed by the sensor protocol. GEOID specifies ANT+ and Bluetooth, power, cadence, left/right balance, and calibration functions. Verify which fields and controls are actually available over each transport before promising them. |
| Overall responsiveness | Required: demonstrably faster, more consistent everyday interaction than the stock firmware. Measure input feedback, page changes, map interactions, recording controls, and behaviour during background work on this same unit. Extra features do not compensate for sluggish operation. |

GEOID already advertises phone-assisted rerouting, offline maps, and Strava auto-sync. Test the current Android Onelapfit workflow as a baseline before deciding which components need replacement.

### Proposed architecture

The head unit records GPS and sensor data, saves recoverable rides locally, and displays navigation and data pages. The Android app handles account authorization, upload queues, destination search, map management, and route calculation. Bluetooth carries compact route/turn/status updates; bulk file transfer over Bluetooth or Wi-Fi depends on verified device support. Cached route guidance and local ride recording should continue if the phone disconnects.

The PM500 should connect directly to the GEOID so power and cadence recording continue without the phone. Evaluate Bluetooth Cycling Power support first; retain ANT+ as a transport option if the head unit's radio interface and protocol can be supported. Using a Bluetooth power sensor while maintaining the Android connection must be tested on the actual hardware. A protocol supported by the stock firmware is not automatically available to replacement firmware.

#### PM500 acceptance requirements

- Pair the intended PM500 by persistent sensor identity; reconnect after the meter sleeps, the head unit restarts, or a temporary radio dropout occurs.
- Show current watts, 3-second average power, cadence, lap/ride average power, and configurable power zones based on the owner's FTP. Confirm units and averaging behaviour.
- Distinguish missing/stale data from a valid zero-watt reading. Do not continue displaying an old power value after a disconnect. Cadence may be derived from crank-event data; use correct event timestamps and counter-wrap handling.
- Provide zero-offset calibration when supported, with clear success/failure feedback. Do not alter power scaling or firmware settings as part of ordinary pairing.
- Include power and cadence in timestamped FIT records, verify that the Strava upload retains them, and avoid recording both ANT+ and Bluetooth reports from the same meter as separate sensors.
- Treat left/right balance and sensor battery status as optional until the actual PM500 data and protocol capabilities have been inspected. Do not infer independent left/right power measurement from a balance field.
- Validate concurrent phone navigation and power-meter reception, including phone disconnection and sensor sleep/reconnection. Initial pairing and live readings have not yet been tested with the owner's meter.

ESP-IDF and LVGL are candidate foundations because the existing image contains Espressif and LVGL components. An ST7789 driver string is a useful display-controller clue. These strings do not establish the precise display variant, touch controller, GPIO assignments, or other peripheral protocols. Reusing the stock Bluetooth/file protocol, if understood, may allow companion-app improvements before replacing the head-unit firmware.

#### Responsiveness and performance acceptance

First measure the stock unit under repeatable conditions: normal ride pages, navigation with a populated map, PM500 reception, phone connection, file transfer, and saving a ride. Capture both typical latency and occasional long stalls. Compare the same routes, map areas, settings, sensor update rates, and battery conditions where practical. Cold startup and GPS acquisition are separate measurements; a faster interface does not imply a faster satellite fix.

Initial engineering targets below are provisional until the display bus, touch hardware, and stock baseline have been measured. They are targets, not current capabilities or guaranteed results:

| Interaction | Initial target and measurement |
| --- | --- |
| Touch/button feedback | Visible feedback within 100 ms for at least 95% of inputs during representative riding workloads; report worst stalls separately. |
| Ordinary ride-page changes | Useful new page content visible within 200 ms for at least 95% of transitions; avoid waiting for map/network work. |
| Map pan/zoom | Feedback begins within 100 ms and remains interactive while detail loads; benchmark completed rendering separately at defined map density and zoom. |
| Start/pause/lap controls | Immediate feedback within the input target; preserve the event timestamp independently of slower screen drawing or file operations. |
| Sensor display | Process each received PM500 update promptly and measure receipt-to-display delay. Screen refresh must not imply a higher sensor sampling rate. |
| Sync, rerouting, and ride saves | Run as bounded background work with visible status; ride controls, sensor capture, and navigation interaction remain usable. Report and diagnose long stalls. |

Keep rendering and input handling free of blocking network, storage, and routing operations. Use bounded queues, appropriate task priorities, partial display updates where supported, and caching justified by profiling. Prefer phone-side route calculation initially. Measure actual display throughput before selecting a frame-rate goal; smooth interaction and low input latency matter more than an unsupported frame-rate claim.

Validate sustained performance during a long ride, not just a fresh boot. Check memory use, dropped sensor records, storage contention, and power consumption. Do not gain apparent speed by silently dropping ride data, reducing required sensor fidelity, or accepting materially worse battery life without the owner's agreement. Ship only after the everyday interactions are demonstrably better than the stock baseline and the core recording/navigation requirements still pass.

### Development sequence

1. Preserve the separate storage, document a reliable BOOT/RESET recovery path, and keep a private copy of the backup off this computer. Investigate the stock app's file-transfer protocol and any accessible USB storage mode. Do not format or repartition the separate storage. Measure stock responsiveness before replacing its firmware.
2. Identify the board revision and hardware: LCD/touch, buttons, backlight, GNSS, storage, charging/power controls, battery sensing, barometer, and ANT+/Bluetooth sensor hardware. Seek manufacturer source/schematics first; use targeted analysis of the original binary and hardware if needed.
3. Evaluate stock-protocol Android integration. If it supports importing routes and retrieving FIT files, prototype custom routing and upload workflows without replacing firmware. Access and protocol compatibility remain unproven.
4. Build a minimal board-specific firmware only after the hardware details are established. Bring up the display and controls, then GNSS, storage, power management, and required sensors. Generic ESP32 demo firmware is not a usable replacement for this product.
5. Validate PM500 pairing, power/cadence recording, calibration support, ride recording, and interrupted-power recovery before navigation features. Test route/turn transfers, phone disconnects, off-route decisions, upload retries, battery runtime, and responsiveness under concurrent workloads. Add richer screens and mapping after these foundations work. Compare against the measured stock baseline before treating any build as a worthwhile replacement.
6. Integrate licensed Strava Live Segments only if access is obtained; otherwise keep personal timed sections independent of Strava's licensed data.

Useful additions after the core features: return-to-start, road-versus-cycle-path routing preferences, climb/elevation preview, battery estimates, hydration/fuelling reminders, and automatic ride recovery. These are proposed features, not verified current capabilities.

### Sources

- [GEOID CC700 Pro specifications and existing features](https://geoid.com/en-gb/collections/frontpage/products/geoid-cc700pro)
- [GEOID CC700 Pro official manual downloads](https://geoid.com/en-gb/pages/download-cc700pro)
- [GEOID PM500 protocols, metrics, and calibration functions](https://geoid.com/en-gb/collections/frontpage/products/geoid-pm500-bike-power-meter-crankset)
- [Espressif flash backup commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)
- [Espressif verification and security information commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-commands.html)
- [Strava upload documentation](https://developers.strava.com/docs/uploads/)
- [Strava Live Segments licensing and access](https://developers.strava.com/docs/partner/strava-live-segments/)
- [Strava's 2026 developer-program changes](https://communityhub.strava.com/insider-journal-9/an-update-to-our-developer-program-13428)
- [OpenStreetMap cycleway data](https://wiki.openstreetmap.org/wiki/Key:cycleway)

Device-specific findings above come from the local read-only hardware queries and offline backup analysis. Proposed architecture and engineering steps are recommendations, not claims of proven compatibility.

## Backup scope and recovery instructions

### Files

- `original-flash-16MB.bin`: full SPI flash image, addresses `0x000000` through `0xFFFFFF`. Includes bootloader, partition table, application partitions, and data stored in this flash.
- `SHA256SUMS.txt`: checksum for checking the backup file later.
- `backup-manifest.json`: image size, checksum, and detected partition/application metadata.
- `security-info.txt`, `flash-info.txt`: device query results.
- `verification.txt`: successful verification of all 16 MB against the device.
- `efuse-summary.json`: read-only snapshot of readable chip configuration.
- `capture-tool-requirements.txt`: installed capture tool and Python dependency versions.
- `hardware-string-evidence.txt`: selected driver strings found offline in the backup; these are clues rather than verified wiring information.

The flash image may contain saved settings, Wi-Fi credentials, pairing information, and ride data. Keep a separate private copy of this folder before experimenting.

This is a compiled firmware backup, not the original source code. It does not capture volatile RAM, separate storage such as an SD card, or internal firmware/settings of GPS and other peripherals. eFuses are chip configuration outside SPI flash; a summary is documentation, not a restorable eFuse image. Do not burn or change eFuses during custom firmware development.



### Recovery procedure (not executed)

Restoration has not been tested by overwriting the device. A successful read and verification establish that the file matches flash at capture; they do not test a complete recovery cycle.

These instructions are for restoring this same physical computer while its security and hardware configuration remain compatible. A different unit may need different calibration, identity, and pairing data.

1. Connect this computer by USB. Confirm its current COM port and close any serial monitor.
2. Use Python with `esptool==5.6.0` installed. In the environment used for capture, the interpreter is `../../work/esp-tools/Scripts/python.exe` relative to this folder.
3. Check the SHA-256 of `original-flash-16MB.bin` against `SHA256SUMS.txt`:

   ```powershell
   Get-FileHash -Algorithm SHA256 -LiteralPath '.\original-flash-16MB.bin'
   ```

4. Only when intentionally restoring, run from this backup folder (replace `python` with the interpreter path if needed):

   ```powershell
   python -m esptool --chip esp32s3 --port COM3 --baud 460800 write-flash 0x0 original-flash-16MB.bin
   ```

   This overwrites the device's flash and replaces settings and data with the captured versions. Do not run an erase first or use `--force`. If the tool reports a security or compatibility conflict, stop and investigate it.

5. Allow the normal reset to complete and check the display, controls, GPS, sensors, charging, and saved settings.

If automatic bootloader entry stops working under custom firmware, recovery may require the board's BOOT/RESET controls or test pads. Locate the manufacturer's procedure before installing replacement firmware.


## Backup integrity and file locations

The current raw image contains 16,777,216 bytes. Its SHA-256 is:

```text
8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7
```

During this document assembly, the raw file checksum was checked against the manifest. The ZIP passed CRC checks, and its archived firmware image matched the raw image SHA-256. These offline checks do not constitute a device restoration test.

| Artifact | Location relative to this handover |
| --- | --- |
| Firmware backup ZIP | [cc700-pro-original-firmware-backup.zip](cc700-pro-original-firmware-backup.zip) |
| Raw image | [original-flash-16MB.bin](esp32s3-backup/original-flash-16MB.bin) |
| Backup notes | [README.md](esp32s3-backup/README.md) |
| Readable eFuse snapshot | [efuse-summary.json](esp32s3-backup/efuse-summary.json) |
| Earlier development plan | [cc700-pro-development-plan.md](cc700-pro-development-plan.md) |

Keep the folder structure when copying these linked files. The handover alone does not contain the firmware binary or full eFuse JSON. The eFuse JSON is an observation record, not a restorable eFuse image. No off-computer backup copy has been made by this work.

The isolated capture interpreter is at:

```text
C:\Users\danie\Documents\Codex\2026-11-02\the\work\esp-tools\Scripts\python.exe
```

Capture used esptool 5.6.0 and pyserial 3.5. The 16 MB read took approximately 168.8 seconds at a reported 795.1 kbit/s, using a 460800 baud setting. Flash verification followed while the RAM helper was still running. A read-only eFuse summary was then saved and the tool requested a hard reset. The logs below preserve those individual steps; the final verification log alone ends before that subsequent reset.


## Evidence appendix

The following are saved capture records or observations from this chat. They are historical output, not commands to run.


### Firmware and partition manifest

Source: [backup-manifest.json](esp32s3-backup/backup-manifest.json).

```json
{
  "file": "original-flash-16MB.bin",
  "size_bytes": 16777216,
  "sha256": "8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7",
  "partition_tables": [
    {
      "offset": "0x8000",
      "md5_valid": true,
      "partitions": [
        {
          "label": "nvs",
          "type": 1,
          "subtype": 2,
          "offset": "0x9000",
          "size_bytes": 16384,
          "flags": 0
        },
        {
          "label": "otadata",
          "type": 1,
          "subtype": 0,
          "offset": "0xd000",
          "size_bytes": 8192,
          "flags": 0
        },
        {
          "label": "phy_init",
          "type": 1,
          "subtype": 1,
          "offset": "0xf000",
          "size_bytes": 4096,
          "flags": 0
        },
        {
          "label": "coredump",
          "type": 1,
          "subtype": 3,
          "offset": "0x10000",
          "size_bytes": 65536,
          "flags": 0
        },
        {
          "label": "ota_0",
          "type": 0,
          "subtype": 16,
          "offset": "0x20000",
          "size_bytes": 8192000,
          "flags": 0,
          "application": {
            "version": "CC700Pro_V1.710-2-g44e3c9e-dirt",
            "project": "Project",
            "build_time": "14:08:30",
            "build_date": "Nov 20 2025",
            "esp_idf": "423e6536"
          }
        },
        {
          "label": "ota_1",
          "type": 0,
          "subtype": 17,
          "offset": "0x7f0000",
          "size_bytes": 8192000,
          "flags": 0
        }
      ]
    }
  ],
  "device_model": "GEOID CC700 Pro",
  "captured_date_local": "2026-11-02",
  "timezone": "Australia/Perth",
  "verification": "esptool verify-flash: full 16MB digest matched",
  "separate_mass_storage_backed_up": false,
  "restore_tested": false
}
```


### Security information

Source: [security-info.txt](esp32s3-backup/security-info.txt).

```text
esptool v5.6.0
Serial port COM3:
Connecting...
Connected to ESP32-S3 on COM3:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                dc:b4:d9:25:0f:10


Security Information:
=====================
Flags: 0x00000000 (0b0)
Key Purposes: (0, 0, 0, 0, 0, 0, 12)
  BLOCK_KEY0 - USER/EMPTY
  BLOCK_KEY1 - USER/EMPTY
  BLOCK_KEY2 - USER/EMPTY
  BLOCK_KEY3 - USER/EMPTY
  BLOCK_KEY4 - USER/EMPTY
  BLOCK_KEY5 - USER/EMPTY
Chip ID: 9
API Version: 0
Secure Boot: Disabled
Flash Encryption: Disabled
SPI Boot Crypt Count (SPI_BOOT_CRYPT_CNT): 0x0

Hard resetting via RTS pin...
```


### Flash identification

Source: [flash-info.txt](esp32s3-backup/flash-info.txt).

```text
esptool v5.6.0
Serial port COM3:
Connecting...
Connected to ESP32-S3 on COM3:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                dc:b4:d9:25:0f:10

Uploading stub flasher...
Running stub flasher...
Stub flasher running.

Flash Memory Information:
=========================
Manufacturer: c8
Device: 4018
Detected flash size: 16MB
Flash type set in eFuse: quad (4 data lines)
Flash voltage set by eFuse: 3.3V

Hard resetting via RTS pin...
```


### Full-image verification

Source: [verification.txt](esp32s3-backup/verification.txt).

```text
esptool v5.6.0
Serial port COM3:
NOTE: Pre-connection option "no-reset" was selected. Connection may fail if the chip is not in bootloader or flasher stub mode.
Connecting...
Connected to ESP32-S3 on COM3:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
MAC:                dc:b4:d9:25:0f:10

Stub flasher is already running. No upload is necessary.
Changing baud rate to 460800...
Changed.

Configuring flash size...
Verifying 0x1000000 (16777216) bytes at 0x00000000 in flash against 'outputs\esp32s3-backup\original-flash-16MB.bin'...
Verification successful (digest matched).

Staying in bootloader.
```


### Selected hardware strings

Source: [hardware-string-evidence.txt](esp32s3-backup/hardware-string-evidence.txt).

```text
mg_esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle)
/./Modules/Middlewares/MidLcd/mg_esp_lcd_panel_st7789.c
tinyusb_driver_install(&tusb_cfg)
tinyusb_cdcacm_register_callback( TINYUSB_CDC_ACM_0, CDC_EVENT_LINE_STATE_CHANGED, &tinyusb_cdc_line_state_changed_callback)
tusb_msc_init(&msc_cfg)
TinyUSB
TinyUSB Device
TinyUSB CDC
TinyUSB MSC
TinyUSB HID
//IDF/components/tinyusb/additions/src/tusb_cdc_acm.c
Mass Storage
mg_panel_st7789_draw_bitmap
FactoryCheckGpsVersion
FactoryGetGpsVersion
```


### Capture tool dependency versions

Source: [capture-tool-requirements.txt](esp32s3-backup/capture-tool-requirements.txt).

```text
cffi==2.1.1
click==8.5.0
colorama==0.4.6
cryptography==50.0.2
esp-pylib==1.2.0
esptool==5.6.0
intelhex==2.3.0
markdown-it-py==4.2.0
mdurl==0.1.2
pycparser==3.12
Pygments==2.21.0
pyserial==3.5
PyYAML==6.0.3
reedsolo==1.7.0
rich==15.0.0
rich-click==1.9.9
websockets==17.4
```


### Initial serial boot output

Transcribed from the serial-read result in this chat. The ROM date is separate from the application build date.

```text
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0xa (SPI_FAST_FLASH_BOOT)
Saved PC:0x40384966
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fce2810,len:0x18a0
load:0x403c8700,len:0x4
load:0x403c8704,len:0xc38
load:0x403cb700,len:0x2f50
entry 0x403c8904
```


### Windows instance IDs

Recorded during the original device inspection.

```text
USB\VID_303A&PID_1001\DC:B4:D9:25:0F:10
USB\VID_303A&PID_1001&MI_02\7&178FB9AD&0&0002
USB\VID_303A&PID_1001&MI_00\7&178FB9AD&0&0000
```


## Unresolved questions and continuation checklist

| Unresolved item | Next investigation |
| --- | --- |
| Separate storage backup | Find a supported export/storage mode or understand the phone file-transfer protocol; do not format or repartition storage. |
| Reliable recovery entry | Establish the actual BOOT/RESET procedure or accessible recovery contacts before installing custom firmware. |
| Original source and schematic | Seek manufacturer/developer material; initial searches did not establish availability. |
| Display/touch/buttons | Identify the exact components, bus, timing, backlight controls, and GPIO assignments. |
| GNSS/storage/radios | Identify component models, protocols, and whether other processors have separate firmware to preserve. |
| Power management | Identify charging, battery sensing, shutdown controls, and sensor power arrangements. |
| Android stock-protocol integration | Determine whether custom routing and FIT transfer can use the existing firmware before replacing it. |
| PM500 support | Inspect the actual meter and validate transport, fields, calibration, reconnection, and phone coexistence. |
| Live Segments entitlement | Check licensed developer access and the user's subscription before promising official integration. |
| Responsiveness baseline | Measure stock input/page/map latency, background-work stalls, and battery runtime; none has been measured yet. |
| Complete restoration | Finish separate backups and recovery preparation; no end-to-end recovery cycle has been tested. |

The first working software should prioritise reliable recording, power-meter support, responsive interaction, and recoverability. Richer maps and integrations depend on the hardware/protocol findings.

## Additional research references

- [Espressif ESP32-S3 overview](https://www.espressif.com/products/socs/esp32-s3)
- [ESP32-S3 datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
- [USB Serial/JTAG introduction](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_serial_jtag.html)
- [ESP32-S3 flash encryption restrictions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/security/flash-encryption.html)
- [GEOID official CC700 Pro manual downloads](https://geoid.com/en-gb/pages/download-cc700pro)
- [Official English manual PDF](https://cdn.shopify.com/s/files/1/0665/5197/1945/files/CC700_Pro-US-20260105.pdf?v=1777275437)

Other source links are retained beside claims and in the development plan above. Research reflects the information gathered on 2 November 2026. Recheck changing service terms, API access, and manufacturer offerings when implementation resumes.
