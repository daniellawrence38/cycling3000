# What the stock firmware reveals about the hardware

Read from the flash backup only (`outputs/esp32s3-backup/original-flash-16MB.bin`, SHA-256 matches the
handover). **No device was connected, opened or addressed** at any point: this is `strings` and struct
parsing over a file, which START-HERE rule 1 allows and rule 3 asks for instead of invention.

Run 3 November 2026 with `tools/fw/backup_report.py` and direct string extraction.

START-HERE rule 3 lists these as unknown: *pin assignments, the touch controller, GNSS and barometer
parts, the radio for ANT+, the map storage format and the separate storage's file system.* Several are
now evidenced. **Evidence from strings is strong but not proof**: a string shows code was compiled in,
not that the part is fitted. Nothing here should be treated as a pin-level fact, and nothing in `fw/`
should depend on it without a bring-up check.

## Confirmed by name

| Thing | Evidence | Confidence |
|---|---|---|
| Display controller **ST7789** | `mg_esp_lcd_panel_st7789.c`, `mg_esp_lcd_new_panel_st7789(`, `mg_panel_st7789_draw_bitmap` | High. A driver file compiled for one named controller. |
| Separate storage is **FAT on SD/MMC** | 45 `SDMMC` hits incl. `//IDF/components/fatfs/vfs/vfs_fat_sdmmc.c`, `diskio_sdmmc.c`, `sdmmc_cmd.c`; paths like `/sdcard/CONFIG/ProductInfo.c`, `/sdcard/FITS/CONFIG/TrainingList.c` | High. Confirms brief 4's instruction to write for FAT, and shows the mount point is `/sdcard`. |
| **ANT+ radio present, via an nRF co-processor** | `mSysTimeStampFromNrf`, `TimePara->mSysTimeStampFromNrf`; two UARTs configured (`UART0TXDPIN/RXDPIN`, `UART1TXDPIN/RXDPIN`); `MidComm`, `MidCommBaudrateSwitch` | Medium-high. An nRF part is definitely present and supplies a timestamp. That it also carries the ANT radio is the obvious reading (nRF52 is the usual ANT+ part) but is **not** directly proven by a string. |
| **Wi-Fi** | `AppWifiManage.c`, `WifiList.c`, `wpa_supplicant` components | High. Out of scope for v1 (rule 4) but it exists. |
| USB mass storage | `tinyusb_driver_install(`, `tusb_msc_init(`, 10 TinyUSB hits | High. Note `fw/` must never add TinyUSB (brief section 9). |
| LVGL on the stock firmware too | `lvgl_tick`, `lvgl_tick_timer_args` | High. |
| Power management **AXP-series** | 2 `AXP` hits, one a bare `1AXp` | Low. Too few hits to name a part. |

## The ANT+ stack the stock firmware implements

Seven profiles, each its own module under `Modules/Middlewares/MidAntDeviceManage/`:

| Module | Profile |
|---|---|
| `ant_bsc/ant_bsc.c` | Bike speed and cadence |
| `ant_hrm/ant_hrm.c` | Heart rate |
| `ant_fec/ant_fe_c.c` | Trainer control (FE-C) |
| `ant_rdr/ant_rdr.c` | Radar |
| `ant_tail_light/ant_tail_light.c` | Tail light (`DEV_ANT_LGT`) |
| `ant_shft/ant_shft.c` | Shifting |
| **`ant_di2/ant_di2.c`** | **Shimano Di2** |

Bike power is there too (`mg_ant_bpwr_default`).

### Di2 specifically

The stock firmware already reads real Di2 data, not just a connection:

- `......ReqDi2SysInfo........`
- `%s:Get DI2 manu=0x%x,model_num=0x%x` — manufacturer and model
- `%s:Front Teech Num Error,Gear=%d,TeechNum=%d,info=%d %d %d` and the matching `Rear` line — gear
  index and chainring/sprocket tooth counts
- `%s:Get Di2Hand Bat status=%d,%d` — Di2 battery
- `LV_EVENT_SET_SHIFT_MODE_RESULT`, `LV_EVENT_SET_SHIFT_SCENE_RESULT`,
  `LV_EVENT_SHIFT_SCENE_MODE_REFRESH` — shift mode and scene are settable from the unit
- UI strings in several languages: `BATT Di2`, `BATTERIA DI2`, `A Di2`, `Are you sure to shift [%s]?`

**This overturns two written assumptions** and is logged as Q34 and in `IDEAS.md` I5:

1. The handover and START-HERE list "the radio for ANT+" as unknown. An nRF co-processor is present
   and the firmware implements seven ANT+ profiles.
2. `docs/PROGRESS.md`'s Di2 enhancement says "no public specification exists for the Di2 gear data
   over BLE; the only open work decodes Shimano's private ANT profile, and the unit has no identified
   ANT+ radio. First step when picked up: a BLE capture with nRF Connect while shifting." That first
   step is the wrong one for this unit: the data arrives over **ANT**, through the nRF, and the stock
   firmware already decodes it.

## Bootloader and partitions (Q31)

- Bootloader at `0x0`: magic OK, 4 segments, flash mode **DIO**, `chip_id 9` (ESP32-S3),
  `min_chip_rev 0`.
- **The bootloader was built from the same IDF commit as the app: `423e6536`.** The string is in the
  bootloader region (`0x0`–`0x8000`). `backup_report.py` reports `bootloader_desc_idf: None` because it
  looks for the `esp_bootloader_desc_t` structure, which is IDF 5.2 and later; this bootloader predates
  that. Its source paths (`bootloader_support/bootloader_flash/src/`,
  `esp_hw_support/port/esp32s3/rtc_clk.c`) and the assert text `"LDO dbias need to modified"` all still
  exist in v5.5.6, so the layout is a v4.4-or-later IDF. Taken together: **a v4.4 to v5.1 bootloader**.
- Stock app in `ota_0`: `CC700Pro_V1.710-2-g44e3c9e-dirt`, built 14:08:30 on Nov 20 2025, idf `423e6536`.
- **`ota_1` is completely erased (all `0xFF`)**, exactly as BRINGUP.md assumes.
- `otadata` copy 0: seq `0x00000001`, state `0x00000002`; copy 1 erased.
- Partition table matches `fw/partitions.csv`: `nvs`, `otadata`, `phy_init`, `coredump`, `ota_0` at
  `0x020000` and `ota_1` at `0x7f0000`, both 8,192,000 bytes.
- The vendor built with a Windows toolchain
  (`/builds/idf/crosstool-NG/.build/HOST-x86_64-w64-mingw32/xtensa-esp-elf/...`).

## Still not evidenced

Counts of loose tokens in the 38,971 extracted strings: `gps` 104, `imu` 84, `touch` 27, `gnss` 24,
`altim` 12, `compass` 11, `gyro` 6, `baro` 2, `nmea` 0, `accel` 0, `fuel_gauge` 0. None of
`backup_report.py`'s specific part names for touch controllers (CST816, GT911, FT6336, …), barometers
(BMP280, SPL06, DPS310, …) or GNSS receivers (UBLOX, ATGM, CASIC, …) appear at all.

So GNSS, an IMU and touch all plainly exist as features, but **no part is named** for any of them, and
no barometer part is named. Do not read the low `baro` count as "there is no barometer": the GNSS and
sensor work may live on the nRF side, whose firmware is not in this backup. Q19's default (GNSS
altitude into the FIT file, gradient and ascent invalid without a barometer) is unaffected and stays.
