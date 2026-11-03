# CC700 engineering brief

Version 1.1 · 3 November 2026 · matches spec 0.5

How to build it. The spec (`cc700-software-spec.md`) says what the product does; the protocol document says what goes over the air; the test vectors pin the numbers. This brief gives the architecture, the algorithms the spec leaves open, the work packages and the acceptance tests. Read `cc700-START-HERE.md` first.

Changes in 1.1 (logged in `cc700-questions.md`): floor division made explicit (5.0); power and heart-rate bins are the mean of the second's samples (5.1); auto-pause needs no pedalling (5.5); pause menu and Confirm focus by button (5.5, 6.4); FIT time correction when UTC arrives mid-ride (5.6); settings and sensor bonds in two alternating slots, and FAT write rules (4, 5.9); the storage root (4, 5.9); calibration support from the meter's feature flags (5.11); page ring and Main grid (6.1); larger transfer chunks (5.10, 7.1); Intervals.icu access (7.6); bring-up order with RAM-only probing and USB safety (section 9); new acceptance tests in WP1 to WP3; short-step cues and the pill settle period (5.4, 5.8).

## 1. What can be built now

Nothing in this brief needs the unit. Everything is built and tested on a PC and an Android phone or emulator, against a simulated unit. The firmware for the real hardware cannot be written until the hardware gate (spec 3) is cleared by Daniel, so WP7 stops at "builds, never flashed".

## 2. Architecture and repository

One repository, `cc700/`.

```
cc700/
  core/             C11 library, no OS dependencies. Everything the unit decides.
    include/cc700/  Public headers (one per module, below)
    src/
    tests/          Host unit tests (Unity, CTest), vectors from cc700-test-vectors.md
  hal/              HAL interface headers only (section 4)
  ui/               LVGL 9 screens, depends on core
    sim/            PC simulator (SDL2) and the headless screenshot tool
    golden/         Approved screenshots, one per screen scenario
    fonts/          Generated LVGL fonts and the script that makes them
  simunit/          Host program: core + file-backed HAL + scripted sensors + TCP link
  fw/               ESP-IDF project: HAL implementations, NimBLE, glue. Build only.
  android/          Gradle project: :core (pure Kotlin), :testing, :app
  tools/
    reference/      ref.py, vectors.py (from the Project); regenerates the vectors file
    design/         gen.py, shoot.py (from the Project): the mockup source and renderer
    fitcheck/       Python: validates FIT files with an independent decoder
    gen_ride/       Python: makes synthetic rides, sensor scripts and GPX files
  docs/             PROGRESS.md, QUESTIONS.md, DEM-FINDINGS.md, BRINGUP.md, THIRD_PARTY.md
  Makefile          make test | sim | shots | android | fw-build | vectors
```

Core is single-threaded and event driven: `cc_tick(now_ms)` at 10 Hz or faster plus event functions (`cc_on_button`, `cc_on_touch`, `cc_on_power_sample`, ...). No locks, no heap allocation after `cc_init`, no floating point in anything covered by the test vectors (float32 is allowed for altitude, gradient and map projection). On the unit one task owns the core; BLE and driver callbacks post events to its queue. The UI reads a snapshot (`cc_view_t`) at up to 10 Hz and sends events back; it holds navigation state but never ride state.

Module list and public headers in `core/include/cc700/`:

| Header | Responsibility |
| --- | --- |
| `util.h` | CRC-32, SHA-256, varint, little-endian helpers, half-up integer rounding, `cc_floordiv` |
| `zones.h` | `watts()`, `zone_of_power()`, `target_zone()`, ramp targets (V§2.4, V§2.5, V§3) |
| `workout.h` | Decode and validate CW1, resolve at FTP, workout engine, pill (V§7) |
| `sensors.h` | CPS and HR parsers, cadence, 1 s bins (V§6.4), 3 s and 10 s averages, loss detection |
| `nav.h` | Local projection, CR1 decode, snapping, off-route, turns, live climb |
| `altitude.h` | Barometer smoothing, ascent, gradient |
| `ride.h` | Ride state machine, timers, laps, auto-pause, totals, page ring |
| `fit.h` | FIT writer, recovery, time correction, independent of the rest |
| `alerts.h` | Banner rules (spec 10.2) |
| `settings.h` | Settings model and its two-slot file |
| `link.h` | Packets, messages and the protocol server (protocol document) |
| `store.h` | Object, ride and settings storage on top of `hal_fs` |
| `view.h` | The snapshot the UI renders |

## 3. Pinned choices

| Area | Choice |
| --- | --- |
| C | C11, `-Wall -Wextra -Werror`, host tests also under ASan and UBSan |
| C tests | Unity (MIT) driven by CTest |
| Firmware | ESP-IDF 5.5.x (pin the exact patch release in `fw/IDF_VERSION`), NimBLE host |
| UI | LVGL 9.x (pin the exact release), SDL2 for the simulator, lodepng for PNG output |
| Fonts | Barlow and Barlow Condensed (OFL), converted with `lv_font_conv` (section 6.2) |
| Python | 3.11 or later, pytest, `fitdecode` for FIT checks |
| Android | Kotlin 2.x, Jetpack Compose with Material 3, minSdk 31, target the latest stable SDK, JDK 17 |
| Android libraries | Nordic Android-BLE-Library (BSD-3) behind the `LinkTransport` interface, OkHttp, kotlinx.serialization, Room, DataStore, WorkManager, Tink for the key, JUnit 5, MockWebServer |
| Licences | Keep `docs/THIRD_PARTY.md` current. No GPL or AGPL dependency in the core, the firmware or the app |

If a pinned item is unavailable or clearly wrong, pick the nearest alternative, record it in `docs/PROGRESS.md` and raise it in `docs/QUESTIONS.md`. Do not stop.

## 4. Hardware abstraction layer

Headers only in `hal/`. The simulator and `simunit` implement them with files, SDL and scripts; `fw/` implements them with drivers once the hardware is known. A `fw/` stub for every function returns "not implemented" and logs once.

| Interface | Functions (indicative) |
| --- | --- |
| `hal_clock.h` | `monotonic_ms()`, `utc_seconds()` (0 if unknown), `set_utc()` |
| `hal_fs.h` | `open/read/write/close`, `fsync`, `rename`, `remove`, `list(dir)`, `free_bytes()`, `mkdir`. Every path is relative to the storage root, which is `CC700/` on the separate storage (spec 7.2) |
| `hal_gnss.h` | `poll(&fix)`: `valid, lat_e7, lon_e7, alt_m, speed_mps, course_deg, hdop_x10, utc_seconds, sats` (`utc_seconds` may be set while `valid` is false) |
| `hal_baro.h` | `read_pa()` returning a value in pascals or -1 |
| `hal_power.h` | `battery_pct()`, `charging()`, `power_off()`, `backlight(pct)`, `ambient_lux()` (-1 if none) |
| `hal_input.h` | Events: `BTN_L_DOWN/UP`, `BTN_R_DOWN/UP`, `PWR_DOWN/UP`, `TOUCH(x,y,down)`. Hold detection is in core, not the HAL |
| `hal_display.h` | LVGL flush callback and the display geometry (240 by 320) |
| `hal_ble.h` | Peripheral: advertise, notify, passkey display, bond erase, pairing window. Central: scan, connect, discover, subscribe, write, indicate (sensors) |

**File-system semantics.** Write `store.h` against the weakest file system the unit may have: FAT through FatFS, where `rename` fails if the destination exists, `fsync` is `f_sync`, and a power cut can leave a file at its size as of the last sync. Concretely: never rename onto an existing name; a file that is replaced in place (settings, sensor bonds) uses two alternating slots (5.9); new objects are written to `*.tmp` and renamed to a name that does not yet exist. The simulator's `hal_fs` enforces the same rules (it refuses a rename onto an existing file), so code that relies on POSIX replace semantics fails in host tests rather than on the unit. Nothing outside the storage root is ever created, changed or deleted.

The firmware's sensor manager (BLE central) lives in `fw/` and calls the core's `cc_on_power_sample`, `cc_on_hr_sample` and `cc_on_sensor_state`. The simulator replaces it with a script player. Do not put BLE central logic in core.

## 5. Core algorithms

All times are core time `now_ms` from `hal_clock`. "Second boundary" means `now_ms / 1000` changed since the last tick; per-second work runs there.

### 5.0 Integer arithmetic

Every `//` in the test vectors and `ref.py` is floor division. C `/` and Kotlin `/` truncate toward zero, which differs whenever the numerator is negative: a descending ramp (W1's cool-down) gives 476 instead of 475 at 300 s and 401 instead of 400 at 599 s. Use `cc_floordiv(a, b)` from `util.h` (and `Math.floorDiv` in Kotlin) for every division that can see a negative numerator, and for clarity in all half-up rounding. V§2.5 has a floor-division check table; WP1 must include it.

### 5.1 Power, cadence and heart rate

Inputs: `cc_on_power_sample(now_ms, watts, has_crank, crank_revs, crank_time)` for every Cycling Power notification, parsed per V§6.1 by the firmware (the parser lives in core and is tested); `cc_on_hr_sample(now_ms, bpm)`.

Power: keep the samples received in the current second (negative clamped to 0) and the time of the last one. At each second boundary push a bin: the half-up mean of that second's samples if there were any (V§6.4); otherwise the previous bin's value if the last sample is 2000 ms old or less; otherwise "invalid". The meter is **lost** when no notification has arrived for 3000 ms after at least one; it recovers on the next notification. The 3 s average is the half-up integer mean of the valid bins among the last 3 bins, invalid if none. The 10 s average is the same over the last 10. A valid zero is a number; an invalid bin is not, and the UI shows "--" for invalid. Never carry a stale value forward past 2 s.

Cadence: compute per V§6.2 on each notification that carries crank data. Hold the last value; if 3000 ms pass without a new crank event while power is not lost, cadence is 0; if power is lost, cadence is invalid.

Heart rate: bins as for power, lost after 5000 ms without a notification.

Ride statistics use bins recorded while the ride state is RECORDING only: average power is the mean of valid bins including zeros, maximum power the largest bin, average heart rate the mean of valid bins, average cadence the mean of bins where cadence is greater than 0. FIT `record` messages carry power, heart rate and cadence only for valid bins; invalid means the field is omitted (FIT invalid value), never 0.

### 5.2 Position, speed, distance and timers

GNSS `poll` at 1 Hz. A fix is usable when `valid` is true. Speed shown and recorded is the mean of the usable speeds in the last 3 s, in m/s internally, shown in km/h to one decimal. Distance accumulates `speed * dt` once per second while RECORDING and speed is at least 0.5 m/s; position deltas are not used for distance (they accumulate noise). If no usable fix for 10 s while recording, raise the GPS lost alert (spec 10.2) and hold distance.

Timers: `timer_s` counts seconds in RECORDING (it is the ELAPSED figure and the workout clock); `elapsed_s` counts seconds from START to the end including pauses; `paused_s` since the current pause began; `lap_s` counts RECORDING seconds since the last lap (manual lap, workout step change, or START) and is the Main page's lap time. Average speed is distance divided by `timer_s`.

### 5.3 Altitude, ascent and gradient

Convert pressure to altitude with `h = 44330 * (1 - (p / 101325) ^ 0.190263)` metres in float32. Take the mean of the barometer samples in each second, then smooth with an exponential filter, `alt = alt + 0.25 * (sample - alt)`, started from the first sample. When GNSS altitude is usable, average `gnss_alt - alt` over the first 30 s of usable fixes and add that offset to every altitude from then on (absolute altitude for the FIT file); gradient and ascent use differences and ignore it.

Ascent: keep a reference `r`, initially the first smoothed altitude. If `alt - r >= 1.0`, add the difference to the ascent and set `r = alt`; if `r - alt >= 1.0`, set `r = alt`. Ascent accrues only while RECORDING.

Gradient: every 10 m of distance travelled while RECORDING, append `(distance, alt)` to a ring of 8 entries. Once 6 entries exist, `gradient = (alt[newest] - alt[newest - 5]) / 50 * 100`, clamped to plus or minus 40 and rounded to 0.1. Below 0.5 m/s hold the last value. With no barometer or fewer than 6 entries the gradient is invalid ("--"). STEEP is on when gradient is 8.0 or more for 3 s, off when below 7.0 (provisional, spec 13).

Watch item (no change for v1): the 0.25 filter delays altitude by about 3 s while distance from the 3 s speed mean lags about 1.5 s, so gradient overshoots briefly where the road changes from flat to climbing. Record it in `docs/PROGRESS.md` if the WP2 fixtures show it; it is tuned on real rides.

### 5.4 Workout engine

On load, decode and validate the CW1 object (protocol 3.2) into static arrays (100 steps at 12 bytes) and resolve each fixed step to `(low_w, high_w)` with `watts(ftp, p10)` (V§2.4). Ramp steps are resolved each second from `ramp_p10(step, e)` with floor division (5.0) and the 95 to 105 widening (V§2.5). On `FTP` change recompute all. Memory is static.

The workout clock is `timer_s`. Step `i` is active while `start_i <= timer_s < start_i + secs_i`. At each second boundary the engine outputs: step index and count, label, seconds remaining, target range in watts, target zone (V§3), repeat text ("3 of 4" from `rep_idx` and `rep_total`), target cadence or none, the next step's label, range and length, the status pill state and delta from the 3 s power (V§7, `dwell` 2; the pill resets at each step start, is hidden for the first `PILL_SETTLE_S` = 5 s of each step, and is hidden for the whole of any step shorter than `SHORT_STEP_S` = 60 s, V§7.1), and the actual zone from the 3 s power. With invalid 3 s power the pill shows no state and the zone bar shows no ring.

Each step change records a FIT lap (trigger `time`) and resets `lap_s`. When the clock passes the end of the last step the engine goes to COMPLETE once (alert `complete`) and stays there; the ride continues unaffected. There is no skip and no extend in v1.

### 5.5 Ride state machine

```
READY --START (right button, or touch START)--> RECORDING
RECORDING --right hold 1 s--> PAUSED(manual)
RECORDING --auto-pause rule--> PAUSED(auto)
PAUSED(manual) --right short--> RECORDING
PAUSED(auto) --auto-resume rule or right short--> RECORDING
PAUSED --left short--> PAUSE_MENU
PAUSE_MENU --BACK--> PAUSED
PAUSE_MENU --Resume--> RECORDING
PAUSE_MENU --Finish and save--> SAVING --> SUMMARY --BACK or DONE--> READY
PAUSE_MENU --Discard ride--> CONFIRM_DISCARD --KEEP or BACK--> PAUSE_MENU
                                             --DISCARD--> READY (file deleted)
RECORDING|PAUSED --power-off request--> SAVING (no summary) --> power off
boot with ride.part present --> RECOVER --> READY (ride listed as recovered)
```

In PAUSE_MENU and CONFIRM_DISCARD a short power press moves the focus down (wrapping) and the right button activates the focused row or button; focus starts on Resume and on KEEP respectively (spec 4, 6). The focused index is part of the ride state, so a UI restart keeps it.

Auto-pause (spec 10.3): pause when, for 5 consecutive seconds, the 3 s GNSS speed is below 0.7 m/s **and** the rider is not pedalling, meaning cadence is 0 or invalid and the 3 s power is 0 or invalid; resume when the speed is 1.4 m/s or more for 2 consecutive seconds or on the right button. Suspended while there is no usable fix. START is always allowed, with or without GPS or sensors. Starting records a FIT timer-start event; pausing a timer-stop; resuming a timer-start. Pausing, the pause menu and the confirm screen are all ride states, not UI states: a UI restart must not change them.

### 5.6 FIT writer, saving and recovery

The writer is a separate module (`fit.h`) with no knowledge of the ride engine: it takes calls like `fit_begin`, `fit_event`, `fit_record`, `fit_lap`, `fit_finish`. Build it from the public FIT SDK profile (messages `file_id`, `event`, `record`, `lap`, `session`, `activity`; sport cycling). Each ride is `rides/<ride_id>.fit.part` while recording and `rides/<ride_id>.fit` plus `rides/<ride_id>.meta` when saved. `ride_id` is the start time in Unix seconds, UTC.

Write buffered and flush and `fsync` every 10 s and at every pause, lap and step change. The 14-byte header carries a data size of 0 until finish. At finish, write the closing messages (final lap, session with totals, timer-stop event, activity), then rewrite the header with the real data size and header CRC, append the file CRC, `fsync`, rename to `.fit` (a new name, so safe on FAT), write `.meta` (to `.meta.tmp`, then rename to the new name), then remove `.part`.

**Unknown time.** If `utc_seconds()` is 0 at START, record every timestamp as a provisional value `base + monotonic seconds`, with `base` a fixed constant (2000-01-01 UTC in Unix seconds), use a provisional `ride_id` from a counter stored in settings, and name the file from it. When UTC becomes known during the ride, record `offset = utc - provisional` in the `.part` file's companion `.time` file. At finish or recovery, if an offset is known, walk the finished file (the same definition-aware parser recovery uses) and add the offset to every `timestamp`, `start_time` and `time_created` field in place before the header and file CRCs are computed, then rename the files to the corrected `ride_id`. If no offset is ever known, keep the provisional times and set the "time unknown" flag in `.meta` (protocol 4, RIDE_LIST flags bit 2).

Recovery at boot: for each `.fit.part`, parse messages from the first definition (definition-aware) to the last complete message, discard a truncated tail, rebuild totals from the records walked, write the closing messages, apply any stored time offset, finalise exactly as above and set the "recovered" flag. A `.part` with no complete record is deleted. Recovery must never throw away a complete record.

Acceptance (also the WP2 test): for a synthetic 30-minute ride, truncate the `.part` at every byte offset from the end of the first definition message to the end of the file; every case recovers to a file that `fitdecode` reads with no error and whose record count equals the number of complete records before the cut. Also: the finished file from the same ride decodes with the expected session totals (distance, time, average power). Also: a ride that starts with no UTC and learns it at 120 s finishes with every record timestamp equal to the true UTC of that second.

Storage errors during a ride: retry for 30 s, then raise the storage alert (spec 10.2) and keep recording in RAM as far as a 5-minute ring allows. Never crash.

Retention (spec 10.4) runs after each save: above 100 rides delete the oldest uploaded ride; never delete one without the uploaded flag unless more than 240 rides are stored.

### 5.7 Route, snapping and live climb

Decode CR1 (protocol 3.3) into arrays in PSRAM: points as `int32` pairs, a float32 local projection (equirectangular about the route's midpoint latitude: `x = dlon * 1e-7 * 111320 * cos(lat0)`, `y = dlat * 1e-7 * 111320`), cumulative distance per point, the profile, turns and climbs.

Snap once per second with a usable fix: search segments from `pos_idx - 5` forward until the cumulative distance exceeds the last position plus 500 m, and back at most 100 m; pick the nearest segment; project to get `route_pos_m`. Accept if within 40 m. If nothing is accepted for 8 s, scan all segments once and accept the nearest within 40 m. **Off route** begins after 8 consecutive seconds beyond 40 m at a speed of at least 3 km/h and ends after 3 consecutive seconds within 25 m. While off route the Navigation page shows OFF COURSE with the sub-line "n m to the route" (spec 8.3).

Outputs for the Navigation page: next turn (first turn with `dist_m > route_pos_m - 5`), distance to it, instruction text (spec 8.2), `dist_left = dist_m - route_pos_m`, ETA (now plus `dist_left` divided by the mean moving speed of the last 10 minutes, at least 10 km/h assumed; invalid under 2 minutes of data), and the map window (section 6.3).

Live climb: the active climb is the first with `start_m + length_m > route_pos_m`. "Approaching" is when `route_pos_m` is within 500 m before `start_m` (alert `climb ahead`, spec 10.2); "on climb" when it is inside. On entering a climb record the barometer ascent as the baseline; if the rider joins mid-climb use `profile(route_pos_m) - profile(start_m)` as the gain so far. Page fields: gradient (5.3), power 10 s, `DIST LEFT = start_m + length_m - route_pos_m`, `ASCENT LEFT = clamp(climb.ascent_m - gain_so_far, 0, climb.ascent_m)` where `gain_so_far` is the barometer ascent since the baseline, the profile strip around the position, and the summary text ("3.4 km · 212 m climb": length to the nearest 0.1 km and ascent in metres). With no route, or off route, or no climbs left, the page shows live gradient and ascent only and hides the profile strip, DIST LEFT and ASCENT LEFT.

### 5.8 Alerts

One module owns the banner. It takes events (step ending, lap, workout complete, power lost, GPS lost, battery, climb ahead, storage error, hold progress) and produces at most one visible banner with title, subtitle, optional right text and an expiry. Priority, high to low: storage error, power lost, GPS lost, low battery, complete, climb ahead, next step, lap. A higher priority replaces the visible banner immediately; an equal or lower one waits in a one-deep queue and is dropped if it would show later than 5 s after its trigger. Hold progress is not a banner; it is the bar overlay (spec 4). The exact strings, triggers and durations are in spec 10.2 and must match the mockup text. The next-step event is raised only for a step of `SHORT_STEP_S` (60 s) or longer. Both constants live in the one header of provisional values (START-HERE rule 5).

### 5.9 Settings and configuration

Settings live in two alternating slots, `config/settings.a` and `config/settings.b`, each: magic `CS`, version 2, `seq u32`, `ftp u16`, `brightness u8`, `screen_off u8`, `units u8`, `autopause u8`, `tz_min i16`, `workout_id[8]`, `route_id[8]`, `ride_counter u32` (for provisional ride IDs, 5.6), then CRC-32 of the preceding bytes. To save: build the record with `seq` one more than the newest valid slot, write it over the other (older or invalid) slot, `fsync`. To load: read both, keep those with good magic, version and CRC, use the one with the higher `seq` (wrapping comparison). If neither is valid, load defaults (FTP 200 W), leave both files as they are for diagnosis, and write the next save to slot `a`. This never needs a rename, so a power cut at any byte leaves the previous settings loadable. Sensor bonds use the same two-slot scheme in `config/sensors.a` and `config/sensors.b` (magic `CB`, version 1, `seq u32`, count u8, then per sensor: kind u8, address type u8, address[6], name_len u8, name; then CRC-32), written by the firmware's sensor manager through `store.h`.

### 5.10 Link server

`cc_link_rx(bytes, len)` takes a packet, reassembles, dispatches per protocol section 4 and answers through the `cc_link_tx(bytes, len)` callback the caller installs. It uses `store.h` only. The same code runs in `simunit` behind a TCP socket and in `fw/` behind GATT. Refusals while recording follow protocol section 4. Pairing window and bond handling are in the HAL's BLE peripheral, not in the link server. OBJ_PUT_DATA and RIDE_READ accept chunks up to 4000 bytes (protocol 4); the server's reassembly buffer is the 4096-byte message limit.

### 5.11 Zero-offset calibration

When the sensor manager connects to a power meter it reads the Cycling Power Feature characteristic (0x2A65) and records whether the meter declares offset compensation support, and whether the Cycling Power Control Point (0x2A66) exists. If either is missing the calibration state is `unsupported`. Otherwise it enables indications on the Control Point, writes opcode `0x0C` (Start Offset Compensation) and waits for an indication with response opcode `0x20`, request opcode `0x0C` and response value `0x01` (success) within 10 s; a success response may carry the measured offset, which is logged, not shown. The opcode, the response format and the feature bit position are **as best recalled and not checked against the specification text**; check them in WP7 against the Bluetooth Cycling Power Service specification and record the result in `docs/PROGRESS.md`. Whether the PM500 implements it is unknown. Core exposes `cc_calibration_*` states (idle, running, done, failed, unsupported) that the Power meter screen renders. Do not claim success without the success response.

## 6. UI layer

### 6.1 Structure

`ui/` holds one source file per mockup board (24 boards, `screen_main.c` and so on). A board's `data-props` scenario options are the screen's scenarios, named identically, so the tools can force any of them: `ccsim --shot Alerts:nextstep --out a.png`. The mockups are the visual source of truth for layout, colour, type and text; the spec is the source of truth for behaviour. Read the published boards with the Artifact tool (`read`, `path` `project/<Board>.dc.html`); the generator in `tools/design/gen.py` defines every board once and `shoot.py` renders each scenario to PNG with Playwright, so you can regenerate the reference images at any time.

Known gap: board 1 (Main) still shows a 2 × 2 grid. Spec 0.5 makes it 2 × 3 (heart rate, cadence, speed, distance, elapsed, lap time). Before WP4, update the Main board in `gen.py` to the 2 × 3 grid in the existing style (cells in the same area, numerals at least 34 px), re-render it, and note the change in `docs/PROGRESS.md`; the contact sheet then shows it for Daniel's approval.

Screen manager: a stack of screens. Back (bottom-left in menus) pops. Overlays (Alerts banner, hold bar) are LVGL layer objects above the stack. Ride pages form a ring whose members come from core (`view.ring`, spec 5): Workout is left out with no workout loaded and Navigation with no route loaded; the page dots show the ring's members only. If the ring changes while a page is shown (a route is loaded only when stopped, so in practice never during a ride), the UI moves to Main.

### 6.2 Fonts

Generate LVGL bitmap fonts with `lv_font_conv`, 4 bits per pixel, from the OFL Barlow families. Needed sizes (px), taken from the boards:

| Family and weights | Sizes |
| --- | --- |
| Barlow 500, 600, 700 | 10, 11, 12, 13, 14, 15, 16, 18, 20, 22, 24, 30 |
| Barlow Condensed 600, 700 | 24, 28, 32, 34, 36, 44, 46, 48, 56, 60, 64, 68, 76, 96 |

Use the weight each board uses; generate only the combinations that occur (the mockup CSS is the list). Glyph set: printable ASCII plus `· – − … ° × →`. Condensed sizes of 44 px and above need only `0123456789.:-+ ` and the minus sign. Record the combinations and flash cost in `docs/PROGRESS.md`.

### 6.3 Navigation map (phase 1)

No base map. A north-up view on a plain background: the route polyline (colour `#4C9AFF`, 3 px, the part already ridden dimmed to 40 % opacity), the rider marker, a scale bar, the turn banner above (spec 5, Navigation). Scale: choose the smallest of {100, 200, 400, 800, 1600, 3200} metres per view width such that the next turn and 1.5 times the distance to it fit, never less than 200 m. The rider sits at 60 % of the map height. Phase 2 adds roads and cycle paths once the map data format on the unit is known; leave a clean layer boundary so that can be added without changing the route layer.

### 6.4 Input

Buttons produce events with hold detection in core: a hold fires at 1000 ms and the release after a fired hold is swallowed. The hold bar (spec 4) is drawn by the UI from `view.hold {active, side, elapsed_ms}`. Touch is LVGL's pointer input. All controls follow spec 4's table; footers show the physical-button labels. Every screen must be fully operable with the three buttons alone: each focusable item, including kebab menus, is in a focus order that the power button steps through (in the pause menu and Confirm too, while paused). Touch targets are at least 44 by 44 px, tested by the layout lint, and the lint also checks that every interactive object is in a focus order.

### 6.5 Simulator and screenshot tool

`ccsim` shows the unit at 240 by 320 (window scale 2 optional) and accepts keys for the three buttons (power = P, left = A, right = L), mouse for touch, and a scenario script that drives sensors. Headless mode renders any screen scenario to PNG. `make shots` renders every scenario of every board into `ui/out/` and builds `contact-sheet.png` (a grid, each cell captioned `Board:scenario`) for Daniel's review against the mockups.

Tests: (1) golden images in `ui/golden/` compared with a tolerance (a pixel differs if any channel differs by more than 8 of 255; fail if more than 0.5 % of pixels differ); goldens are created only after Daniel approves the contact sheet. (2) Layout lint over the LVGL object tree for every scenario: interactive objects at least 44 by 44 px and in a focus order, no child outside its parent unless the parent scrolls, no text clipped unless the label is set to ellipsis, text at least 10 px. Exceptions go in `ui/lint-exceptions.txt` with a reason. Pixel parity with the mockup PNGs is judged by Daniel, not by a test, because font rasterisers differ.

### 6.6 Performance hooks

Mark input time, first redraw and flush complete with `perf_mark()` (a no-op in release). The targets in the handover (feedback within 100 ms, page change within 200 ms for 95 % of inputs) are checked on hardware later; in the simulator just make sure the marks exist and a `--perf` run prints the percentile table.

## 7. Android

### 7.1 Modules

`:core` is pure Kotlin/JVM so it tests fast and has no Android dependency. It contains: `workout` (parse, validate, normalise typography, convert, encode CW1, V§4 rules), `route` (GPX parser, simplification, turn generation, profile smoothing, climb detection, encode CR1), `link` (framing, messages, client state machine over a `LinkTransport` interface; chunk size 4000 bytes when the negotiated MTU is 185 or more, otherwise 512), `util` (CRC-32, SHA-256, varint, floor division via `Math.floorDiv`), and `dem` (an `ElevationProvider` interface). `:testing` holds `FakeUnit` (a Kotlin implementation of the unit's protocol for fast tests) and `SocketTransport` that talks to `simunit` for end-to-end tests. `:app` holds Compose screens, the Nordic BLE transport, Room, DataStore, WorkManager jobs and the Intervals.icu client.

### 7.2 Screens

1. **Unit**: connection state, battery, firmware, FTP, Add unit (pairing), Forget unit. 2. **Workouts**: library mirror (names, duration, steps; delete), Import (paste, share target, file picker), Validate and Preview (chart in watts at the unit's FTP, step list, readable errors), Send. 3. **Routes**: GPX import, preview with elevation profile and climbs and the climb thresholds, Send, delete. 4. **Rides**: rides on the unit with uploaded or waiting state, Sync now, Upload status and retry, and a date prompt for a ride flagged "time unknown" (its FIT times are shifted before upload). 5. **Profile**: FTP (read from the unit on connect, editable only while connected). 6. **Settings**: Intervals.icu API key, climb thresholds, elevation source, about. Dark theme using the design tokens (spec 12). Not mocked; keep it plain and fast, Daniel will review it on a phone.

### 7.3 Workout import

Rules are the coach document plus the limits in V§4. Before validation, normalise typography: `×` to `x`, `–` and `—` to `-`, curly quotes to straight, non-breaking space to space. Errors name the field and the position ("Step 3: label is longer than 16 characters"). Never auto-correct beyond the normalisation list. Conversion follows V§2.1; the object ID is a hash of the object, so identical workouts dedupe (V§2.2).

### 7.4 Route import, elevation and climbs

GPX only in v1 (`<trk>` segments joined, or `<rte>`; waypoints ignored). Remove points closer than 1 m to the previous one, simplify with Douglas–Peucker at 3 m (raise the tolerance in 1 m steps until at most 8000 points remain), compute distance with haversine in double precision, resample the profile at `S = max(25, ceil(dist / 12000))` metres.

Elevation providers behind `ElevationProvider.sample(points): List<Double?>` (null where there is no data), tried in this order:

1. `GaProvider`: Geoscience Australia 5 m LiDAR DEM through its ArcGIS `Identify` operation at `https://services.ga.gov.au/gis/rest/services/DEM_LiDAR_5m_2025/MapServer`. Coverage of Perth and the practicality of one request per sample point are **unverified**; the service describes national coverage built from about 236 LiDAR surveys centred on populated areas. Spike it first in WP5: sample Kings Park, the Swan River foreshore and the Perth Hills, record the results and the request rate that works in `docs/DEM-FINDINGS.md`. Use at most 4 concurrent requests and cache every sample.
2. `OpenTopoProvider`: `https://api.opentopodata.org/v1/srtm30m` (about 30 m; public limits 100 locations per request, 1 call per second, 1000 calls per day).
3. The GPX file's own `<ele>` values if both fail, labelled "from file" in the preview.

Cache samples in Room keyed by a 1e-4 degree cell. Smooth the profile with a 3-sample median then a 5-sample mean before climb detection. Climb detection is `detect_climbs` in `tools/reference/ref.py` ported exactly, called with `spacing = S`; defaults and the test case are in V§8, and the thresholds are user-adjustable in Settings.

### 7.5 Turn generation (phase 1)

GPX has no road network, so turns come from geometry. For each vertex of the simplified polyline take the bearing of the segment arriving over the previous 30 m and of the one leaving over the next 30 m. Merge candidates within 40 m into one, summing the angle. Let the signed change be positive to the right. Below 35 degrees: no turn. 35 to 60: slight; 60 to 120: turn; 120 to 165: sharp; 165 or more: U-turn. Always add `depart` at 0 m and `arrive` at the end. Street names are empty. This is provisional and will be replaced when a routing engine provides real instructions (spec 8.2).

### 7.6 Intervals.icu

`POST https://intervals.icu/api/v1/athlete/0/activities`, multipart form with the FIT file, HTTP basic auth with user `API_KEY` and the key as the password. Intervals.icu is free and every user can call the API with a personal key, so no plan check is needed. Before uploading, query that day's activities and skip the upload if one starts within 60 s of the ride, marking the ride uploaded. On 401 stop and ask for a new key; on 429 and 5xx retry with backoff through WorkManager; on other 4xx mark the ride failed with the server message. Store the key with Tink AEAD wrapped by an Android Keystore key. **Never log, display or put the key in source, tests, fixtures or documents.** Tests use MockWebServer; the first live upload is Daniel's, with a checklist in `docs/PROGRESS.md`.

Not in v1, but keep the client's shape open for it: Intervals.icu can return planned workouts from the athlete's calendar (`GET /api/v1/athlete/0/events?category=WORKOUT`, with the workout as a file or in its own JSON form). A later converter from that form to v1 workout JSON would make the Intervals.icu calendar a second workout source (spec 9).

## 8. Work packages

Work in this order. Packages marked "parallel" can run alongside the previous one. After each package: run `make test`, update `docs/PROGRESS.md` (what exists, how to run it, deviations, anything unverified), mirror it to the Project if you can write there, and carry on with the next unless a stop is stated.

**WP0 Repository and harness.** Create the layout in section 2, the Makefile targets, CI-less local scripts, `docs/` files, pinned versions. Port the reference: `tools/reference/ref.py` and `vectors.py` into the repo and make `make vectors` regenerate the vectors file and fail on any difference from the one in the Project. *Done when:* `make test` runs (empty suites pass), `make vectors` passes, and a Unity test checks CRC-32 `123456789` and SHA-256 `abc`.

**WP1 Core: workouts, zones, sensors (parallel with WP5).** `util`, `zones`, `workout`, `sensors`. *Done when:* every table in V§1 to V§3, V§4 (as decode-time structure checks), V§6, V§7 and V§7.1 passes as host tests in C, byte for byte for V§2.2 and V§2.3 decode, including the V§2.5 floor-division table and the V§2.4 300 % row (an object with `p1` 3150 decodes), with ASan and UBSan clean, and a fuzz test (random bytes into the CW1 decoder, 1 million cases) never crashes.

**WP2 Core: ride engine, FIT, altitude, navigation, alerts.** `altitude`, `ride`, `fit`, `nav`, `alerts`, `settings`. *Done when:* the state machine table in 5.5 is a table-driven test with every transition and every illegal event, including focus movement in PAUSE_MENU and CONFIRM_DISCARD; the FIT recovery test in 5.6 passes at every truncation point; the unknown-time test in 5.6 passes, finished and recovered; gradient, ascent and auto-pause have unit tests built from `tools/gen_ride` fixtures with expected values worked out by hand in the test, including an indoor fixture (speed jittering 0 to 0.5 m/s, cadence 85, power 200 W for 20 minutes) that never auto-pauses and a red-light fixture (stopped, no pedalling) that does; the route follower passes a replayed GPX ride (on route, off route, back on, detour) with the expected off-route and turn events; every alert in spec 10.2 fires and expires per the table in a test that drives time; the settings two-slot scheme loads the previous settings after a simulated power cut at every byte of a save.

**WP3 Core: protocol, storage, simunit.** `link`, `store`, then `simunit` (core plus a directory-backed `hal_fs` that refuses a rename onto an existing file, a script player for sensors and GNSS, TCP on localhost speaking the packet framing). *Done when:* every example in V§5 round-trips; every request in protocol section 4 has tests for OK and each listed error, and PUT_DATA and RIDE_READ work at both 512 and 4000 bytes; storage tests prove atomic commit by killing the process (simulate with a fault-injecting `hal_fs` that fails at each write) and finding either the old state or the new; retention tests cover 101 rides with and without uploaded ones, and 241 un-uploaded; `simunit` runs a scripted 30-minute ride headlessly and leaves a FIT file that `tools/fitcheck` accepts.

**WP4 UI simulator.** Update board 1 in `gen.py` first (6.1). All 24 boards and every scenario, input handling, overlays, the page ring, `ccsim`, `make shots`, lint, `--perf`. *Stop and report to Daniel here:* produce `contact-sheet.png` and wait for his comments before creating golden images. While waiting, continue with WP5 and WP6. *Done when:* the contact sheet contains every scenario, lint passes, a scripted ride in the simulator walks through Ready, START, all four ride pages, a manual pause, the pause menu (moving focus with the power key only), Finish and save, Summary and back to Ready, a second scripted ride with no workout and no route shows a two-page ring (Main, Climb), and the screenshots of that walk match the goldens after approval.

**WP5 Android core and tools (parallel with WP1).** `:core` as in 7.1, `tools/fitcheck`, `tools/gen_ride`, the elevation spike and `docs/DEM-FINDINGS.md`. *Done when:* the Kotlin validator reproduces every row of V§4, the converter and encoder reproduce V§2 byte for byte (including the floor-division table), the route encoder reproduces V§9 byte for byte, climb detection reproduces V§8, the framing reproduces V§5, `FakeUnit` passes the same protocol tests as `simunit` (one shared suite, run against both), and the Intervals.icu client passes MockWebServer tests for success, duplicate, 401, 429 and 5xx.

**WP6 Android app.** Screens in 7.2, BLE transport, pairing, sync service, upload queue. *Done when:* the app, on an emulator or Daniel's phone, against `simunit` over `SocketTransport`, can pair (passkey simulated), read FTP, set FTP, import the W1 and W2 workouts, send them, see them in the library, delete one, import a GPX route, see its climbs, send it, list a ride and upload it to a mock server, and handle a ride flagged "time unknown". Instrumented UI tests cover the import errors.

**WP7 Firmware skeleton and bring-up plan (stops short of hardware).** `fw/` ESP-IDF project for the ESP32-S3 that builds `app.bin` with NimBLE (peripheral and two central connections), the HAL stubs, the link server on GATT, LVGL and the core. Partition table equal to the stock layout (`nvs` 0x9000, `otadata` 0xd000, `phy_init` 0xf000, `coredump` 0x10000, `ota_0` 0x20000 size 8,192,000, `ota_1` 0x7f0000 size 8,192,000) in `fw/partitions.csv`, **never to be flashed**. USB stays on the chip's built-in USB Serial/JTAG controller: do not enable TinyUSB or anything that reconfigures the USB pins (section 9). Also build `fw/probe/`, a minimal RAM-only image (ESP-IDF's RAM app build type) for hardware probing, if the pinned IDF supports that build type on the ESP32-S3; record whether it does. Check the calibration opcode, response and feature bit (5.11) against the specification. Write `docs/BRINGUP.md` for Daniel (section 9). *Done when:* `make fw-build` produces `app.bin` that fits in 8,192,000 bytes, the build has no warnings, and BRINGUP.md is reviewed by Daniel. Do not run anything that touches a device.

## 9. Bring-up plan Opus must write for Daniel (WP7)

`docs/BRINGUP.md` is written for Daniel to run, step by step, one step at a time (he wants a single step with confirmation before the next). It must include, in this order, and must not be executed by an AI:

1. Preserve the separate 4 GB storage (a full copy, kept off the PC as well), because the custom software will write to it (`CC700/` folder).
2. Document and test the BOOT/RESET recovery entry: confirm that ROM download mode can be entered by the physical BOOT and RESET controls, independently of the firmware. Note that the stock firmware contains TinyUSB, which may take the USB port away from the built-in USB Serial/JTAG controller in some modes; if it does, automatic reset into download mode over USB stops working and only the physical controls remain.
3. Identify the hardware (display and touch pins and controller, buttons, backlight, GNSS, barometer, battery and charging, light sensor, PSRAM and flash modes, the separate storage's bus, file system and free space) from the stock firmware and board. Where a live probe is needed, run it **from RAM only** (`esptool load-ram` with the `fw/probe/` image), so nothing is written to flash; if a RAM-only image is not possible, say so and stop for Daniel's decision.
4. Build and flash a minimal display-and-buttons image **into `ota_1` only** and switch to it by writing `otadata`, keeping the stock firmware in `ota_0` untouched, so rollback is restoring the 8 KB `otadata` region saved from the backup (Proposed; the stock bootloader's behaviour with `otadata` must be confirmed first, and `ota_1` is believed to be empty because the manifest shows no application there). Every custom image keeps USB on the built-in USB Serial/JTAG controller.
5. Measure the stock firmware's responsiveness baseline.
6. Bring up GNSS, barometer, storage (writing only inside `CC700/`), power, then sensors.
7. Only then the full app.

Each step lists what to check before and after, and how to get back.

## 10. Definition of done for the whole job

`make test` passes everywhere; every vector passes in C, Kotlin and Python; the contact sheet is approved; the Android app runs the full flow against `simunit`; `fw/` builds; `docs/PROGRESS.md` is current and honest about everything unverified; `docs/QUESTIONS.md` has every open question with the default you used. Nothing here means the product works on the unit: that is proven only after bring-up on hardware by Daniel.
