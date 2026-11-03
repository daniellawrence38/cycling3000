# CC700 bring-up plan

For Daniel to run by hand, one step at a time (brief section 9). Nothing in this document has been run, and no AI runs any of it: every command that touches the unit is typed by Daniel. After each step, send the results and wait for a reply before starting the next. When a step says **stop**, stop there even if everything looked fine.

The stock firmware stays in `ota_0` throughout, untouched. Everything custom goes into `ota_1`, and the way back is always the same: restore the 8 KB `otadata` region, which puts the unit back on `ota_0`.

## Before any step

- **Computer.** Use the same Windows PC as the backup, with the same isolated Python that has `esptool` 5.6.0 and `pyserial` 3.5 (handover, "Backup integrity and file locations"). In the commands below, `python` means that interpreter and `COM3` means the unit's port; check the port in Device Manager each time, because it can change.
- **Backup copies.** Have the backup folder (`original-flash-16MB.bin`, `SHA256SUMS.txt` and the rest) in two places, one of them off the PC (a USB drive kept elsewhere, or cloud storage). Check its SHA-256 before every step that writes flash:
  ```powershell
  Get-FileHash -Algorithm SHA256 -LiteralPath '.\original-flash-16MB.bin'
  ```
  It must be `8ec2dfd530d6dbf2f078d23114c589d88ac0fbd728b926da70c0526e5c7b3ec7`.
- **Commands that are never used.** `erase-flash`, `erase-region`, `--force`, `espefuse` writes (`burn-*`), `write-flash` to any address other than the one a step names, and `idf.py flash`. eFuses are never changed.
- **Opening the serial port.** The handover records that opening the port can reset the unit (USB_UART_CHIP_RESET). That is harmless (it restarts into the stock firmware), but it explains a reset you did not ask for.
- **Repository tools.** These run on files only, never on the unit:
  - `tools/fw/backup_report.py` reports on the backup.
  - `tools/fw/otadata.py` extracts, inspects and prepares `otadata`. It does the same calculation as ESP-IDF's `otatool.py` and is covered by tests.
  - `make fw-build` builds `app.bin` and the RAM probe. It never flashes.

---

## Step 1: Preserve the separate 4 GB storage

**Why.** The custom software will write rides and settings to the separate storage, in a `CC700/` folder of its own. That storage holds the stock maps, rides and settings, and it has not been backed up (handover: "The whole-device backup is incomplete").

**Before.** Nothing is connected to the unit for this step.

**Do.**
1. Read the manual's sections on USB connection and file export (the official PDF is linked at the end of the handover). Look for a USB storage, U-disk or file-transfer mode. The firmware contains TinyUSB mass-storage code, but no menu for it has been confirmed.
2. Look for a removable card slot (microSD) on the outside of the unit or under a rubber cover. Do not open the sealed case for this; the unit is rated IPX7.
3. In the Onelapfit app, export what can be exported: rides (FIT), routes, and settings worth keeping.
4. If a storage mode or card exists, copy the whole storage as an image or a full folder copy. Keep it on the PC and also off the PC, then record the capacity, the file system shown, the folder list and the free space.

**After.** Send me what you found: whether there is a storage mode or a card, and the capacity, file system and folders if so.

**If nothing gives access:** stop. The options are:
- (a) Go ahead without a full copy. The custom software only ever writes inside `CC700/` and never formats or repartitions, but a FAT corruption would still be unrecoverable.
- (b) After step 3 identifies the storage bus, I write a RAM-only reader that dumps the storage over USB. At USB serial speeds 4 GB takes many hours, but it writes nothing.

The choice is yours.

**Getting back.** Nothing was changed.

---

## Step 2: Prove the BOOT/RESET recovery entry

**Why.** Once custom code runs, it might crash or take over USB. The way in must then be ROM download mode, entered with the physical controls, which works whatever is in flash. The stock firmware contains TinyUSB: if it ever moves the USB port off the built-in USB Serial/JTAG controller, automatic reset into download mode over USB stops working, and only the physical controls remain. The custom firmware never does this (`fw/sdkconfig.defaults` keeps the console on USB Serial/JTAG, and the tests check that nothing uses TinyUSB).

**Before.** The unit is charged and running the stock firmware. Close any serial monitor.

**Do.**
1. **Check the normal route still works.** It is read-only and loads only Espressif's helper into RAM, as the backup did:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 chip-id
   ```
   Note whether it connects ("Chip is ESP32-S3 … MAC dc:b4:d9:25:0f:10") and whether the unit then restarts by itself.
2. **Find the physical entry.** The ESP32-S3 enters download mode when GPIO0 is held low at reset. One of the three buttons may be wired to GPIO0.
   - Turn the unit off.
   - Hold one button, plug in USB (or press power), keep holding for 3 s, then release.
   - In download mode the screen stays dark, and the port reappears in Device Manager.
   - Test with a command that does **not** reset the chip, so it only answers if the unit is already in download mode:
     ```powershell
     python -m esptool --chip esp32s3 --port COM3 --before no-reset --after no-reset chip-id
     ```
   - If it answers, note which button and sequence did it.
   - If not, unplug, wait for the unit to power down (or hold power to turn it off), and try the next button.
3. **Leave download mode.** Unplug USB and power-cycle the unit; it should start the stock firmware. Note exactly what it took (a long press of power, waiting for the battery, and so on).

**After.** Send me:
- whether step 2.1 connected;
- the button and sequence that enters download mode, or "none found";
- how you left download mode.

**Stop if no physical entry is found.** Do not flash anything until a recovery route that does not depend on the firmware is known. The next options are test pads (needs the case opened) or GEOID support; that is your decision.

**Getting back.** Power-cycle. Nothing was written.

---

## Step 3: Identify the hardware

**Why.** Every driver behind `hal/` is a stub until the parts and pins are known: display and touch, buttons, backlight, GNSS, barometer, battery and charging, light sensor, the PSRAM and flash modes, and the separate storage's bus, file system and free space. This step reads; nothing is written to flash.

### 3a. Offline, from files you already have

Nothing is connected for 3a.

1. Run the report on the backup and send me `backup-report.txt`:
   ```powershell
   python tools\fw\backup_report.py original-flash-16MB.bin > backup-report.txt
   ```
   It lists:
   - the bootloader's flash mode and IDF version strings, and the partition table;
   - each app slot's descriptor, and whether `ota_1` is completely erased;
   - the otadata entries;
   - chip and driver names found in the firmware (display, touch, GNSS, barometer, charger and others).

   It prints no NVS contents and no free text, so it carries no Wi-Fi credentials. The bootloader's IDF version decides whether an IDF v5.5.6 app can start under it (Q31). The stock app's IDF commit, `423e6536`, is not in any public ESP-IDF branch or tag (checked against all 117,349 commits), so the bootloader version has to come from the image.
2. In `efuse-summary.json`, find the PSRAM and package fields (names like `PSRAM_CAP`, `PSRAM_VENDOR`, `PKG_VERSION`, `FLASH_CAP`) and send me their values. They settle whether the 8 MB PSRAM is octal, which `fw/sdkconfig.defaults` assumes (Q30).
3. Optional, and only if you want to: share the full backup image with me for static analysis of the stock firmware's pin set-up. It may hold Wi-Fi credentials and pairing data, so the decision is yours. Without it, pins come from 3b and from tracing the board.

### 3b. Live, from RAM only

**Before.**
- Step 2 found a physical download-mode entry.
- `make fw-build` has produced `build/fw-probe/cc700_probe.bin` (ESP-IDF v5.5.6 on the PC). It is built as ESP-IDF's RAM app with no flash driver compiled in. v5.5.6 lists the ESP32-S3 as supported for this build type (`tools/test_apps/system/ram_loadable_app`), but marks the build type experimental.
- What the probe does:
  - prints the chip;
  - watches the input level of every GPIO except USB (19, 20) and flash and PSRAM (26 to 37), and prints changes for 120 s;
  - never drives a pin and never changes a pull.

**Do.**
1. Put the unit in download mode (step 2).
2. Load the probe into RAM; this writes no flash:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 --before no-reset --after no-reset --no-stub load-ram build\fw-probe\cc700_probe.bin
   ```
3. Within 10 s, open the monitor without toggling reset lines:
   ```powershell
   python -m serial.tools.miniterm --dtr 0 --rts 0 COM3 115200
   ```
   If the banner never appears, opening the port has reset the chip into the stock firmware. That is harmless: close the monitor and tell me.
4. When it says "watching", press each button in turn (left, right, power), one press each with 5 s between them. Then touch the screen, then plug and unplug the charger. Note the order you did them in.
5. Copy the whole monitor output into a text file.

**After.** Send me the output and your order of actions. From these I map buttons, touch interrupt and charger detect to pins, and write the next probes. Display, I2C devices and GNSS need probes that drive pins, so each will be described for your approval before you run it.

**Getting back.** Power-cycle; the RAM image is gone and the stock firmware starts.

---

## Step 4: A display-and-buttons image in `ota_1`, switched by `otadata`

**Why.** This is the first custom code to run from flash. It goes into the empty `ota_1` only. The stock firmware in `ota_0`, the bootloader, the partition table, `nvs`, `phy_init` and `coredump` are not written. The custom firmware also never initialises NVS and stores no PHY data or core dumps (Q29). Switching between the two is a matter of which slot `otadata` names.

**Before** (all must hold; otherwise stop):
- Steps 1 to 3 are done, and you have approved the display and button pins I derive from them. The image for this step is built after that; the skeleton built now has stub drivers.
- `backup-report.txt` shows `ota_1` completely erased, and a bootloader IDF version I have confirmed can start an IDF v5.5.6 app (Q31). This is **Proposed**: the stock bootloader's handling of `otadata` must be confirmed first.
- The backup's SHA-256 checks.
- `make fw-build` passes: `app.bin` fits in 8,192,000 bytes and the build has no warnings.

**Do.**
1. Save the unit's current `otadata`; it may have changed since the backup. Then inspect it:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 read-flash 0xd000 0x2000 otadata-now.bin
   python tools\fw\otadata.py inspect otadata-now.bin
   ```
   It should say the bootloader would start `ota_0`. Keep `otadata-now.bin` with the backup; it is the way back.
2. Prepare the `otadata` that selects `ota_1` (a file operation only):
   ```powershell
   python tools\fw\otadata.py switch otadata-now.bin --to 1 --out otadata-ota1.bin
   ```
3. Put the unit in download mode (step 2), then write the app into `ota_1` and nothing else:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 --before no-reset --after no-reset write-flash 0x7f0000 build\fw\app.bin
   ```
4. Switch to `ota_1`:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 --before no-reset --after no-reset write-flash 0xd000 otadata-ota1.bin
   ```
5. Power-cycle. The custom image should show its display test and respond to the buttons.

**After.** Tell me what the screen shows and whether each button responds. Also send the monitor output, using the same monitor command as 3b.

**Getting back to stock** (works even if the custom image crashes):
1. Enter download mode (step 2).
2. Write back the saved selection:
   ```powershell
   python -m esptool --chip esp32s3 --port COM3 --before no-reset --after no-reset write-flash 0xd000 otadata-now.bin
   ```
3. Power-cycle. The stock firmware starts from the untouched `ota_0`.

If even that fails, the full restore in the handover ("Recovery procedure") writes the whole backup back.

---

## Step 5: Measure the stock firmware's responsiveness

**Why.** Better responsiveness is the reason to replace the firmware (owner requirement 8), so the stock numbers are the baseline the custom firmware must beat.

**Before.** Restore stock if step 4 left `ota_1` selected (see "Getting back to stock" above). The unit is charged, with GPS fixed outdoors where relevant.

**Do.** Film each action with a phone at 240 fps (slow motion), with the button and the screen in frame, 10 times each:
- button press to screen change, on the ride pages and in the menu;
- touch to screen change;
- opening the map and panning it;
- boot to a usable screen;
- time to GPS fix (cold, after a day off, and warm).

Count frames from the press to the first visible change; 240 fps gives about 4 ms per frame. Also note the battery drop over one hour of recording with the screen on.

**After.** Send me the counts, or the videos. I'll record median, 90th percentile and worst case in `docs/BASELINE.md`.

**Getting back.** Nothing changed.

---

## Step 6: GNSS, barometer, storage, power, then sensors

**Why.** These subsystems come one at a time, each as a new image in `ota_1` (step 4's write-and-switch, and its way back), each preceded by a RAM-only probe if its pins are not yet known.

**Order and gates:**
1. **GNSS.** Identify UART pins, baud rate and protocol (NMEA, UBX or CASIC), then read positions. Never send configuration commands that save to the receiver's own flash (for example a UBX `CFG-CFG` save, or a CASIC `PCAS00` save) without asking you: the receiver has firmware and settings of its own (handover).
2. **Barometer.** I2C address and part, then read pressure only.
3. **Storage.**
   - First mount read-only and list. Report the file system, size and free space, and check it against step 1.
   - Then the first write: create `CC700/` and nothing else. The storage tests on the PC already enforce the FAT rules: never rename onto an existing name, and use two-slot settings files.
   - Nothing outside `CC700/` is ever created, changed or deleted.
4. **Power.** Battery voltage and percentage, the charging indication and backlight, then power-off last. Power-off may be a latch pin, and holding it wrongly could leave the unit unable to stay on without USB. That probe is described for your approval first.
5. **Sensors.** Pair the PM500 and a heart-rate strap, check power, cadence and heart rate against the stock unit on the same ride, then calibration:
   - The firmware reads the Cycling Power Feature and reports whether bit 9 (offset compensation supported) is set, and whether the Control Point exists.
   - If both are present, Calibrate sends opcode 0x0C and shows success only on a response of 0x20, 0x0C, 0x01 within 10 s.
   - Send me the monitor log either way, because whether the PM500 supports this is unknown (spec 13.1 item 1).

**After each.** Send me the monitor log and what you observed, then wait for the next image.

**Getting back.** As step 4.

---

## Step 7: The full app

Only when steps 1 to 6 are done:
- the screen manager moves from the simulator into the shared UI code;
- the link server is tested with the Android app over real BLE: pairing with the passkey on screen 23, sending a workout, ride sync;
- the remaining acceptance items in the handover's development plan come next.

The same `ota_1` and `otadata` routine applies. Making the custom firmware the default, or reclaiming `ota_0`, is a later decision and is not part of this plan.
