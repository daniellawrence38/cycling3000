# CC700 questions and document errors (implementation)

Kept by the implementing model. The design-review entries Q1 to Q13 and E1 to E4 are in `claude/cc700-questions.md`; new questions here are numbered from Q14 and new document errors from E5. Daniel answers by adding an `Answer:` line or in chat. A question never blocks work: the default is used until answered.

Format:

```
### Q<n> · <work package> · <open | answered>
Question:
Options:
Default used:
Answer:
```

Document errors use `### E<n> · <document and section>` and `Reading used:` in place of `Default used:`.

**Convention added 3 November 2026.** Daniel asked for the open questions to be researched overnight.
Where evidence could actually settle a question — a specification, a source check, a measurement, or the
flash backup read offline — the finding is written on the `Answer:` line, the heading is marked
`answered`, and the evidence is named so it can be checked. Where the question is Daniel's preference,
or needs a real ride, the question stays `open` and gains a `Research:` line with what was found and a
recommendation; the `Answer:` line is left empty for him. Provisional decisions are not reopened
(START-HERE rule 5): research that *supports* a default says so and changes nothing.

## Open questions

### Q14 · WP1 · open
Question: Protocol 3.2 lists what the unit validates in a CW1 object, but not the header `flags` byte (documented as 0), the reserved step flag bits (documented as 0), or the characters of the name and labels. Should the unit reject an object that breaks these?
Options: (a) accept anything the listed checks allow; (b) also reject non-zero header flags, reserved step flag bits, and name or label bytes outside printable ASCII 0x20 to 0x7E.
Default used: (b). It is the safer reading: a future format that sets a flag the unit does not understand is refused (protocol 8 says unknown versions are refused for the same reason), and the UI never has to draw a control character. The phone never produces such objects, so nothing valid is lost. All are reported as BAD_OBJECT.
Answer:

### Q15 · WP1 · open
Question: When the 3 s power becomes invalid during a step (meter lost), what happens to the status pill's state machine?
Options: (a) reset it; (b) hold its state and dwell count, show no state, and resume feeding when power is valid again.
Default used: (b). The spec says only that the pill shows nothing while the 3 s power is invalid; holding the state means a short dropout does not restart the 2 s dwell or the 5 s settle period.
Answer:

### Q16 · WP2 · answered
Question: Are FIT `record` messages written while the ride is paused?
Options: (a) one record per second from START to finish, paused or not; (b) records only while RECORDING, with the pause shown by timer stop and start events.
Default used: (b). Brief 5.1 already limits ride statistics to RECORDING seconds; Intervals.icu and other tools read the timer events for moving time. Totals in the session come from the records, so a pause adds elapsed time but no samples.
Answer: **(b), and the implementation is already safe against the one way this goes wrong.** (b) is the ordinary convention: a pause is a timer `stop` event and a resume a timer `start` event, which is what the Garmin FIT SDK forum shows people doing and what importers read.
The risk worth naming is **a stop with no matching start**. A reported GPSMAP 67 file had exactly that, and Strava imported only up to the first stop — the rest of the ride was silently dropped. So (b) is only correct if every stop is paired.
Checked in this codebase and it is: `core/src/core.c` writes `cc_fit_event_timer(..., 1)` on start (line 353) and on resume (line 398), and `..., 0)` on pause (line 387); `cc_fit_finish` (`core/src/fit.c:348`) writes a closing stop *only if the timer is running*, so it never emits a second, dangling one. `core/tests/test_fit.c` drives a pause at 600 s and a resume at 660 s and checks the session's timer total excludes the gap. A power cut while paused leaves a file ending in a stop, which is a truthful end rather than a truncation.
Not checked against the live service: how Intervals.icu specifically derives moving time. It is unaffected by the choice above and stays part of Daniel's first live upload.

### Q17 · WP2 · open
Question: While storage fails, brief 5.6 asks for recording "in RAM as far as a 5-minute ring allows". A ring keeps the newest records, but dropping the oldest unsaved bytes out of the middle of a FIT stream is awkward.
Options: (a) a true ring that drops the oldest unsaved records; (b) a 24 KB buffer (about 10 minutes of records at 25 bytes each) that keeps the oldest unsaved records and drops new ones once full; events and laps have a reserved 2 KB.
Default used: (b), simpler and never corrupts the stream; the buffer holds more than 5 minutes. The test fills it with 2,000 records during an outage, checks at least 300 are kept and that the file finalises with a valid CRC once storage returns.
Answer:

### Q18 · WP2 · open
Question: The workout banner and the Lap banner say "LAP n". Should n count only manual laps, or every FIT lap including the automatic laps at workout step changes?
Options: (a) manual laps only; (b) every lap in the file.
Default used: (b), so the number on the banner matches the lap number in Intervals.icu. With a workout loaded this makes the numbers jump.
Research: this one is genuinely yours — it is a preference about what the number on the banner is *for*, and both readings are defensible. The trade is: (b) keeps the banner honest against what you will later see in Intervals.icu, at the cost of the number jumping mid-workout as automatic step laps are counted; (a) keeps "LAP 3" meaning "the third time I pressed lap", which is what the number means to a rider pressing the button, at the cost of disagreeing with the upload. The deciding evidence is a ride with a workout loaded, watching whether the jumping number is confusing in practice. Nothing in the code depends on which you pick — it is one counter — so changing it later is cheap.
Answer:

### Q19 · WP2 · open
Question: Without a barometer, what altitude goes in the FIT file?
Options: (a) none; (b) the GNSS altitude.
Default used: (b) for the file only. Gradient, ascent and the Climb page stay invalid ("--") without a barometer, as brief 5.3 says.
Answer:

### Q20 · WP4 · answered
Question: Brief 6.2 asks for the glyph `→` in every font. The Barlow files available (Fontsource 5.3.0, all subsets) have no `→`, and no mockup uses it as text (arrows are drawn as icons).
Options: (a) leave it out; (b) take it from another font.
Default used: (a). If a screen ever needs it in text, it can be drawn as an icon.
Answer: **(a), and it was not a choice.** Verified directly against the fetched fonts by reading every `cmap` with fontTools: across the 54 normal-weight Barlow and Barlow Condensed files in `build/deps/node/node_modules/@fontsource/barlow*/files/` (subsets `latin`, `vietnamese`, `condensed`), `U+2192 RIGHTWARDS ARROW` appears in **0**. Every other glyph brief 6.2 asks for is there, in the 18 latin-subset files: `·` U+00B7, `–` U+2013, `−` U+2212, `…` U+2026, `°` U+00B0, `×` U+00D7 — 18/54 each. So the brief asks for one glyph the chosen typeface does not contain, and (b) would mean mixing a second typeface for a single arrow that no mockup uses as text. Leaving it out is right; drawing it as an icon stays the escape route if a screen ever needs one.

### Q21 · WP4 · open
Question: Brief 6.3 picks the map scale so "the next turn and 1.5 times the distance to it fit" in the view width, with the rider at 60 % of the map height. Only about 95 px of the 158 px map is above the rider, so at the 400 m view that rule gives for a turn 250 m ahead (the mockup's case) the turn is off the top of the map.
Options: (a) fit across the view width as written; (b) fit 1.5 times the distance in the space above the rider.
Default used: (b): view width at least 1.5 × distance × 240 / 95, from {100 … 3200} m, never below 200 m. The turn is then always on the map, as the mockup shows.
Answer:

### Q22 · WP4 · open
Question: The Navigation mockup draws the route 8 px wide on a `#10151B` map; brief 6.3 and spec 8.3 say 3 px on the plain background `#0B0D10` for phase 1.
Options: follow the mockup; follow the spec.
Default used: the spec (3 px, `#0B0D10`), since phase 1 is defined there; the mockup's roads are phase 2. Easy to change if Daniel prefers the wider line.
Answer:

### Q23 · WP4 · open
Question: Profile (screen 12) has four steppers and "Applies on SAVE", with SAVE on the right button. With the three buttons alone, how is a stepper pressed?
Options: (a) the power button moves focus through the four steppers and then SAVE; the right button activates the focused item; (b) the right button always saves and steppers are touch-only (breaks "every control reachable by buttons").
Default used: (a). Focus starts on −5; with focus on SAVE the footer's SAVE is what the right button does. Holding a focused stepper's button repeats at 5 per second after 0.5 s, as spec 6 describes for touch.
Answer:

### Q24 · WP4 · open
Question: The Sensors mockup shows "Add sensor" with both a power meter and a heart-rate strap paired; spec 6 says it is hidden once both are paired. Also the status bar mockups show all four icons, while spec 5 shows an icon only for each connected sensor or service.
Options: follow the mockups; follow the spec.
Default used: the spec, for both (behaviour is the spec's). The screenshots therefore differ from the mockups there.
Answer:

### Q25 · WP5 · open
Question: Brief 7.5 takes bearings over 30 m before and after each vertex, then merges candidates within 40 m "summing the angle". Neighbouring vertices' 30 m windows overlap, so summing them counts one corner several times (a 90° corner drawn with three points would read as about 180°, a U-turn).
Options: (a) as written; (b) sum the local heading change at each vertex over a cluster of vertices less than 40 m apart, which is the true total change of direction; (c) keep the largest windowed angle in each cluster.
Default used: (b). The bands (35, 60, 120, 165 degrees) are unchanged and the turn is placed at the cluster's sharpest vertex. Tested on synthetic corners drawn with one and with several points.
Answer:

### Q26 · WP6 · open
Question: Spec 10.6 and brief 7.2 say the app "asks for its date" for a ride flagged "time unknown". A date alone does not fix the start time, and the provisional time of day (2000-01-01 plus the unit's uptime) means nothing.
Options: (a) date only, start at a fixed hour; (b) date and start time; (c) date, and take the time of day from the provisional clock.
Default used: (b). Set date opens a date picker then a time picker (local time, starting at 07:00); every FIT time is shifted by the same offset (the `timestamp`, `start_time` and `time_created` fields, as the unit does in brief 5.6) and the upload uses the corrected start for the duplicate check and the file name.
Answer:

### Q27 · WP6 · open
Question: Brief 7.1 lists three modules (`:core`, `:testing`, `:app`). The app's behaviour (connection and pairing, the mirror, ride sync, the upload queue, the FIT time shift, import) needs testing against simunit, and this environment cannot build `:app` (no Android SDK, and Google's Maven repository is blocked).
Options: (a) put it all in `:app`, untested here; (b) a fourth pure JVM module, `:sync`, that `:app` uses, with `:app` only adding the screens and the Android implementations of its store and connector interfaces.
Default used: (b). `:sync` depends on `:core` and `:testing` (the app needs `SocketTransport` at run time to reach simunit).
Answer:

### Q28 · WP6 · open
Question: The WP6 flow needs pairing "with the passkey simulated", but simunit has no BLE security (connecting counts as the bonded phone).
Options: (a) add a passkey exchange to simunit's TCP framing; (b) simulate it in the app's simulated connector, which plays the unit's part.
Default used: (b). The simulated unit's passkey is 123456; the app asks for it in its own dialog (over BLE, Android's pairing dialog asks instead), refuses a wrong one, and stores the unit only after a good one. Nothing on the unit or in the protocol changes.
Answer:

### Q29 · WP7 · open
Question: An ESP-IDF app normally initialises the `nvs` partition and keeps PHY calibration data, BLE bonds and core dumps in flash data partitions. On this unit those partitions belong to the stock firmware (its settings and pairing may live in `nvs`), and step 4 of the bring-up must leave the stock firmware able to run unchanged.
Options: (a) let the custom app use `nvs`, `phy_init` and `coredump`; (b) the custom app touches none of them: no `nvs_flash_init`, no PHY data storage, core dumps off, NimBLE bonds in RAM.
Default used: (b). The cost is that the phone bond is forgotten at every reboot until bonds move to the separate storage (`CC700/config/`, the same two-slot scheme as settings) during bring-up step 6. Tests in `tools/tests/test_fw_partitions.py` check the configuration.
Answer:

### Q30 · WP7 · answered
Question: The handover gives 8 MB of in-package PSRAM ("AP_3v3") but not its interface. ESP32-S3 parts with 8 MB in-package PSRAM are octal; the wrong mode makes PSRAM fail to start.
Options: (a) octal; (b) quad; (c) no PSRAM until confirmed.
Default used: (a) with `CONFIG_SPIRAM_IGNORE_NOTFOUND=y`, so a wrong guess boots without PSRAM rather than crashing. Settled by the eFuse fields in BRINGUP.md step 3a.
Answer: **(a) octal, and the default stands.** The eFuse summary already in the backup (`outputs/esp32s3-backup/efuse-summary.json`, read offline) gives `PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3`, `PSRAM_TEMP = 85C`, and for the flash `FLASH_TYPE = 4 data lines`. Espressif's in-package PSRAM options for the ESP32-S3 are 2 MB quad (the R2/RH2 part) and 8 MB octal (ESP32-S3R8, 3.3 V; the 1.8 V version is R8V). There is no 8 MB quad in-package variant, so 8 MB fixes the bus as octal, and `AP_3v3` additionally identifies this as the 3.3 V **R8** rather than R8V — which matters because VDD_SPI is tied to 3.3 V here (`VDD_SPI_FORCE`, `VDD_SPI_XPD` and `VDD_SPI_TIEH = VDD_SPI connects to VDD3P3_RTC_IO` are all set).
One false trail worth recording so it is not re-walked: `AP_3v3` alone does **not** imply quad. AP Memory sells a 3 V octal part (APS6408L-3OBM) and a 1.8 V quad one, so the vendor/voltage field fixes the voltage and nothing else. The capacity is what settles it.
All `SPI_PAD_CONFIG_*` eFuses (including `D4`–`D7` and `DQS`) read `0x00`, i.e. default pins, which is normal for in-package memory and does not discriminate either way.
Bearing on the firmware, added 4 November 2026: this answer is now load-bearing in a second way. The decoded route and the object buffers (about 680 KB) do not fit in internal SRAM and are allocated from the heap so that ESP-IDF places them in PSRAM (brief 5.7). They are deliberately *not* placed with `EXT_RAM_BSS_ATTR`, because static `.bss` in external RAM makes PSRAM a boot requirement and would turn a wrong mode guess into a dead firmware. With allocations, `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` still means what it says: no PSRAM costs routes and workouts, through an error path the UI already has, and nothing else.
Still Unverified until bring-up: that PSRAM actually starts. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` keeps a wrong guess harmless, so nothing in `fw/` needs to change.

### Q31 · WP7 · answered
Question: The custom app in `ota_1` will be started by the stock bootloader, which stays in place. The stock app reports ESP-IDF `423e6536`, which is not in any public ESP-IDF branch or tag, so the bootloader's version is unknown, and an old bootloader may not start an IDF v5.5.6 app.
Options: (a) find the bootloader's version in the backup (BRINGUP.md step 3a, `backup_report.py`) and confirm compatibility before step 4; (b) also replace the bootloader.
Default used: (a). Replacing the bootloader is outside this plan and would need its own recovery route; nothing in the plan writes the bootloader or the partition table.
Answer: **(a), carried out, and the bootloader's version is no longer unknown.** Done offline against the backup; no device involved.
1. **The bootloader was built from the same commit as the app.** The string `423e6536` is present in the bootloader region (`0x0`–`0x8000`) of the backup, alongside `bootloader_esp32s3.c`. So bootloader and stock app came from one vendor tree, and the version WP7 searched for was in the file all along.
2. **It is a v4.4-to-v5.1 ESP-IDF.** It has no `esp_bootloader_desc_t` (that structure is IDF 5.2 and later, which is why `backup_report.py` printed `bootloader_desc_idf: None`), but its source layout — `bootloader_support/bootloader_flash/src/bootloader_flash.c`, `esp_hw_support/port/esp32s3/rtc_clk.c` — and the assert text `"LDO dbias need to modified"` are all still present in v5.5.6, so it is not an ancient tree.
3. **The direction is the supported one.** Espressif's bootloader documentation states the bootloader supports booting apps built from *newer* ESP-IDF versions; only the reverse (an app older than the bootloader) is unsupported. A v4.4–v5.1 bootloader starting a v5.5.6 app is therefore expected to work.
4. **The one documented pre-v5.1 caveat does not apply.** Bootloaders older than v5.1 do not support `CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM`; `fw/sdkconfig.defaults` does not set it, and it is an ESP32 option in any case. Checked.
Also confirmed from the same read, all as BRINGUP.md assumes: `ota_1` is completely erased (all `0xFF`), the partition table matches `fw/partitions.csv` exactly, `otadata` copy 0 is seq `0x00000001` state `0x00000002` with copy 1 erased, and the bootloader's flash mode is DIO (which `fw/sdkconfig.defaults` already matches).
Not settled: what `423e6536` *is*. It is still in no public ESP-IDF branch or tag, so it remains a vendor commit; the bound above comes from structure, not from the hash. See `docs/STOCK-FIRMWARE-FINDINGS.md`.

### Q34 · WP7 / future · open
Question: The stock firmware implements a full ANT+ stack — seven profiles under `Modules/Middlewares/MidAntDeviceManage/` (speed and cadence, heart rate, FE-C trainer, radar, tail light, shifting and **Shimano Di2**) — and an nRF co-processor is present on a UART (`mSysTimeStampFromNrf`, two configured UARTs, a `MidComm` layer with a baud-rate switch). The Di2 module already reads manufacturer and model, gear index, front and rear tooth counts, battery status and shift mode. Two written assumptions are therefore wrong: START-HERE and the handover list "the radio for ANT+" as unknown, and the Di2 enhancement in `docs/PROGRESS.md` says the unit has no identified ANT+ radio and proposes a BLE capture as the first step. Found by reading the flash backup offline (`docs/STOCK-FIRMWARE-FINDINGS.md`); no device was touched.
Options: (a) leave ANT+ out of v1 as START-HERE rule 4 says, and correct the two documents so the facts are right; (b) also bring ANT+ into scope for a later version, with Di2 gear display as the first use; (c) investigate the nRF link during bring-up (what protocol `MidComm` speaks over the UART) and decide afterwards.
Default used: (a) for now. Rule 4 puts ANT+ out of scope for v1 and rule 5 says not to reopen settled scope, so nothing in `fw/` or the app changes tonight. The documents are corrected because they state something untrue, which rule 6 does not allow to stand silently.
Research: the custom firmware cannot use any of this yet — `fw/hal_esp.c` has stub drivers only and the nRF's own firmware is not in this backup, so the UART protocol is unknown. The cheapest real step is (c), and it costs nothing extra: bring-up step 3 already probes GPIO, and the two UART pin pairs are the obvious thing to look at while there. Recorded as `IDEAS.md` I5.
Answer:

### Q32 · WP5 · open
Question: The GA 5 m LiDAR DEM returns `NoData` over the Perth Hills (Kalamunda, Lesmurdie Falls, Mundaring Weir all empty; the coastal plain answers normally), measured by the spike on 3 November 2026. The hills are where the climbs are, so a hills ride's gradients come from the Open Topo Data fallback at 30 m sampling, not from GA at 5 m.
Options: (a) keep the chain as it is (GA, then Open Topo Data, then the file) and accept 30 m in the hills; (b) make Open Topo Data the default source and drop GA; (c) find a DEM that covers the hills at a finer resolution.
Default used: (a). The chain already does the right thing and nothing is lost by trying GA first: it costs one request per cell, which is cached. Whether 30 m sampling is good enough for the climb detection in brief 5.9 is Unverified; it needs a ride over a known climb to judge, which is Daniel's call after riding.
Answer:

### Q33 · WP5/WP6 · open
Question: `simunit` does not build on Windows. It includes `<arpa/inet.h>` and `<sys/statvfs.h>`, which MinGW-w64 does not provide, so on a Windows PC the 3 Python simunit tests, the 9 JVM simunit tests and the whole WP6 manual flow (which needs `tools/android/demo_unit.sh`) cannot run. The C core itself builds and all 14 of its tests pass.
Options: (a) run simunit under WSL; (b) port it with a small socket and free-space shim (Winsock, `GetDiskFreeSpaceEx`); (c) leave it POSIX-only and document that the simunit-backed tests need Linux, macOS or WSL.
Default used: (c) for now, because nothing in the brief asks for a Windows build and the tests self-skip cleanly rather than failing. (a) is the cheap unlock on Daniel's PC: WSL is present but has no distribution installed.
Answer: **(c) stands, and (a) is now done on Daniel's PC, so nothing is blocked.** Ubuntu was installed into the existing WSL 2 (no elevation needed — only a distribution was missing) and the full gate then ran natively: `make test` green end to end, 16/16 C tests plain, 14/14 under ASan and UBSan, and 20/20 Python with no skips. The JVM suite run under WSL is 61 tests, 0 failures, **0 skipped** — the 9 simunit-backed tests that had always skipped now run and pass. `simunit` itself builds unchanged under Linux.
So the Windows port (b) buys nothing now and is not worth doing; `IDEAS.md` I1 keeps it on record if a Windows-native workflow is ever wanted. Two practical notes for anyone repeating this: the WSL virtual disk defaults to C:, which is where it filled the system drive here, and `make test-c-asan` still cannot link on Windows at all because MinGW ships no sanitizer runtime (`IDEAS.md` I2).

## Document errors

### E5 · brief 5.6 (finish sequence)
Problem: The finish sequence says "rename to `.fit` ..., write `.meta` ..., then remove `.part`", but renaming the `.part` already removes it.
Reading used: rename `.part` to `.fit`, then write `.meta` (via `.meta.tmp`). A power cut between the rename and the `.meta` write leaves a `.fit` without `.meta`; boot rebuilds the `.meta` from the file. Tested in the store boot path.

### E6 · brief 5.6 (closing message order)
Problem: The closing messages are listed as "final lap, session with totals, timer-stop event, activity". The usual FIT order puts the timer-stop event before the final lap, so the lap and session timer totals include the stop.
Reading used: timer-stop event (if the timer is running), final lap, session, activity. Recovery recognises a file cut anywhere in that sequence: it drops a partial session and does not write a second final lap.

### E7 · protocol 2.6 and 4 (response and event opcodes overlap)
Problem: A response's opcode is the request's opcode with bit 7 set, and events use 0xA0 and above. RIDE_LIST (0x20) therefore answers with 0xA0, the same opcode as EVENT_STATE, and RIDE_OPEN (0x21) answers with 0xA1, the same as EVENT_RIDE_SAVED; RIDE_READ, RIDE_MARK and RIDE_DELETE answer with 0xA2 to 0xA4, the range reserved for future events. A receiver that classifies by opcode alone mistakes the ride responses for events (found by the end-to-end test against simunit).
Reading used: the protocol already says events carry req_id 0 and that 0 is reserved for the phone, so req_id decides: a message with req_id 0 is an event, anything else is a response. The unit ignores a request with req_id 0 (no response; the phone's timeout covers it) so the rule always holds. The Python test client and the Kotlin client (WP5) classify this way. No opcode was changed, so no protocol version change is needed; a later protocol revision could move events to 0xE0 and above.


### E8 · ArcGIS REST documentation (GA identify attribute name)
Problem: The ArcGIS REST documentation for `identify` on an image service shows the pixel value under `attributes."Pixel Value"`, and both `GaProvider` and `tools/dem_spike/dem_spike.py` read exactly that name. The live `DEM_LiDAR_5m_2025` service returns `attributes."Stretch.Pixel Value"` instead: the renderer prefixes the field. `GaProvider` would therefore have found no value at any point and fallen silently through to Open Topo Data for every route, and the mock in `HttpTest.gaIdentify` encoded the same wrong name, so the unit tests confirmed the mistake rather than catching it. Found by running the WP5 spike against the live service on 3 November 2026 (DEM-FINDINGS.md).
Reading used: accept either name: `Pixel Value`, or any key ending in `.Pixel Value`, so the renderer prefix does not matter and a service configured without one still works. `HttpTest.gaIdentifyLiveAttributeName` pins the live shape (a value and a `NoData`) alongside the original test, which is kept because the documented plain name remains valid. No request parameter changed.

### E9 · Intervals.icu API (activity list start-time field)
Problem: `IntervalsClient.findDuplicate` reads each listed activity's `start_date`, the name the API documentation gives. Community examples of `GET /api/v1/athlete/0/activities` show `start_date_local` as well, and at least one reported response carried only the local one. The old code skipped any entry without `start_date`, so a list using the other name made the function return null — "no duplicate" — and the ride uploaded a second time with nothing to show anything had gone wrong. `Instant.parse` also rejects a zone-less local time, with the same silent result. This is the same shape of fault as E8: a documented field name that the live service does not use, confirmed by a mock that encoded the same assumption. Found by reading the public documentation; no key was used and no live call was made (rule 2).
Reading used: accept `start_date` or `start_date_local`, but compare only a value that parses as an exact instant. A zone-less local time is skipped rather than assumed to be UTC: converting it needs the athlete's time zone, and guessing one would either invent duplicates or miss real ones. If the list returns entries but none give a comparable start time, `UnreadableActivityList` is raised and the upload reports `Failed`, which the rider sees and can retry, instead of silently uploading again. Three tests in `HttpTest` pin the behaviour. This lowers the risk but does not close it: which field the endpoint really returns is still unconfirmed, so the first live upload remains the check, and the existing checklist item stands.
