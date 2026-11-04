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

