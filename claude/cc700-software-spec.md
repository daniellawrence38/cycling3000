# CC700 Pro custom software: specification

Draft 0.5 · 3 November 2026 · Perth, WA

## 1. Purpose and status

This records the decisions made for custom ride software on the GEOID CC700 Pro and its Android companion app. Each item is tagged **Decided** (Daniel confirmed it), **Decided (provisional)** (Daniel left the choice to Claude and will revise it after real use; see 13.2), **Proposed** (a recommendation not yet confirmed) or **Unverified** (depends on a fact nobody has checked). Nothing here has been built or run on the unit; the UI exists only as mockups.

Daniel's instruction for this draft was to settle the remaining design so an implementing model can start without further decisions, and to revise after he has used it. Every choice made on that basis is tagged provisional and listed in 13.2.

Related documents (all in the Bike Computer Project):

- `GEOID-CC700-Pro-complete-handover.md`: hardware findings, flash backup, recovery, research. It governs any hardware fact.
- `claude/workout-coach-instructions.md`: source of truth for the workout file format (v1). If it disagrees with this spec, it wins.
- `claude/cc700-ble-protocol.md`: byte-level phone to unit protocol and the stored object formats.
- `claude/cc700-engineering-brief.md`: architecture, algorithms, work packages, acceptance tests.
- `claude/cc700-test-vectors.md`: golden numbers and bytes; authoritative for every number in it.
- `claude/cc700-START-HERE.md`: ground rules and reading order for the implementing model.
- `claude/cc700-questions.md`: questions and document errors, including the log of the 0.5 review.
- Mockups: 24 artboards at 240 × 320 with scenarios, https://claude.ai/artifact/6VKNT6PhDD9q2R31oGEFgN (private). The mockups are the source of truth for layout, colour, type and on-screen text; this spec is the source of truth for behaviour. The new boards (13 to 24) were checked with local renders; the older boards (1 to 12) have not been re-reviewed since they were first made. The Navigation mockup still draws roads and cycle paths, which are phase 2 (8.3), the climb-ahead and storage-error alerts have no mockup (10.2), and board 1 (Main) still shows the 2 × 2 grid that 0.5 replaces with 2 × 3 (section 5).

Superseded handover requirements: automatic Strava upload (requirement 3) and official Strava Live Segments (part of requirement 4) are dropped; see section 9. Requirements 5 to 8 stand, with maps and rerouting phased as in section 8.

Changes in 0.2: FTP is stored on the unit and workouts are stored as percentages of FTP, resolved to watts when loaded.

Changes in 0.3: the workout status pill judges power against the step's own range, and the riding button map is set (both provisional).

Changes in 0.4: all 24 screens are designed and specified (section 6); behaviour rules for settings, alerts, auto-pause, retention, pairing, time and sensor loss (section 10); navigation phased (route line first, base map later) and routes come from GPX files (section 8); climb defaults and the live climb page rules set; the status pill needs two seconds outside the range on the same side; the Android app and the work order moved to the engineering brief.

Changes in 0.5 (design review, 3 November 2026; each item is logged in `cc700-questions.md`): the power button moves focus on the pause menu and Confirm screens while paused (section 4); Main shows distance and lap time in a 2 × 3 grid, and the ride page ring leaves out pages that would be empty (section 5); auto-pause also requires no pedalling, so indoor rides are not paused (10.3); un-uploaded rides are never deleted by retention (10.4); ride time is corrected when UTC arrives mid-ride (10.6); the storage location is decided: the separate storage, in a folder of its own (7.2, 13.1); calibration support is read from the meter's feature flags (section 6); a single target of 300 % is accepted by the unit (7.1); Intervals.icu API access is confirmed as free for every user (section 9). Added later the same day: the status pill waits 5 s into each step before judging and is not shown on steps shorter than 60 s, and those short steps get no next-step banner (7.6, 10.2).

## 2. Decisions at a glance

| Area | Decision | Status |
| --- | --- | --- |
| Phone platform | Android companion, Kotlin with Jetpack Compose | Decided |
| Firmware stack | ESP-IDF with LVGL; UI built first in the LVGL PC simulator at 240 × 320 | Proposed |
| Ride pages | Four: Main, Workout, Climb, Navigation; pages with nothing to show are left out of the ring | Decided (ring rule provisional) |
| Use with gloves | Touch targets 44 px or larger, large numerals, state never by colour alone, every control reachable by buttons | Decided |
| Climb data | Detected on the phone from a DEM; live ascent from the barometer; no Strava dependency | Decided |
| Strava | Out (no subscription; Live Segments needs a licence plus a user subscription) | Decided |
| Garmin Connect | Out (business-only developer programme; personal API is read-only) | Decided |
| Ride upload | Intervals.icu via its API, FIT file, personal API key | Decided (upload with Daniel's own key still to be confirmed) |
| Workout authoring | A Claude "coach" project writes v1 JSON; the Android app imports it | Decided |
| Workout library | Up to 20 workouts on the unit; deleted from the app only; no transfers during a ride | Decided |
| FTP | One value on the unit (Profile). Workouts stored as % of FTP, converted to watts on load. The app reads and writes it over BLE | Decided |
| Stopped-state flow | Ready → Menu → Workouts → detail → Load → Ready | Decided |
| Workout status pill | Judges 3 s power against the step's own watts range; chip shows the target zone | Decided (provisional) |
| Riding buttons | Bottom left: next page, hold for previous. Bottom right: lap, hold to pause | Decided (provisional) |
| Sensors | Bluetooth LE only for now; PM500 expected over BLE Cycling Power | Proposed |
| Navigation, phase 1 | Route line, rider marker and turn banner on a plain background; no base map | Decided (provisional) |
| Route source | GPX files imported in the app; no routing engine and no rerouting in v1 | Decided (provisional) |
| Routes on the unit | Up to 10, deleted from the app only | Decided (provisional) |
| Storage on the unit | Workouts, routes, rides and settings on the separate 4 GB storage, in a `CC700` folder; stock folders never written | Decided (provisional) |
| Phone link | One bonded phone, passkey shown on the unit | Decided (provisional) |
| Rides on the unit | Up to 100; uploaded rides are deleted first when full; a ride not yet uploaded is never deleted by the unit | Decided (provisional) |
| Alerts | Visual banners only; no sound or vibration in v1 | Decided (provisional) |
| Power button | Hold 2 s powers off; short press blanks the screen when stopped and moves focus down in lists, the pause menu and Confirm | Decided (provisional) |
| Firmware updates | USB only after the hardware gate; no over-the-air updates in v1 | Decided (provisional) |

## 3. Hardware constraints that shape the design

The CC700 Pro uses an ESP32-S3 (silicon revision v0.2, QFN56, 8 MB PSRAM in the package, 16 MB quad flash, Bluetooth 5 LE, no ANT+), a 2.8 in 240 × 320 ST7789 touchscreen, and three buttons: power (top right), one bottom left, one bottom right. The stock unit advertises ANT+, which must come from a separate chip that has not been identified. The stock firmware (CC700Pro_V1.710) is built on ESP-IDF with LVGL. The PM500 power meter advertises both ANT+ and Bluetooth.

Flash is 16 MB with two 8,192,000-byte OTA slots (`ota_0` at 0x20000, `ota_1` at 0x7f0000), `nvs` at 0x9000, `otadata` at 0xd000, `phy_init` at 0xf000 and a 64 KB `coredump` at 0x10000. About 256 KB is unallocated after `ota_1`; whether the stock firmware uses it is Unverified. It is too small for rides in any case (a three-hour ride is roughly 300 to 400 KB of FIT), so the software's files live on the separate storage (7.2). The separate 4 GB storage (maps, rides, settings) has not been backed up, and restoring the flash image has not been tested. Secure Boot and flash encryption are off; eFuses must never be changed.

**Gate:** nothing is flashed to the unit, and no tool is run against it, until the handover's first two steps are done: separate storage preserved, and a reliable recovery entry (BOOT/RESET) documented. Because the custom software will write to the separate storage, preserving it is a hard prerequisite, not a nicety. The GPIO map, touch controller, GNSS, barometer and power management are all unidentified. Opening the unit's serial port restarts it. The simulator, the core library and the Android app need none of this and proceed now. An implementing model never touches the hardware (see `cc700-START-HERE.md`).

## 4. Controls

| Control | Stopped (Ready, menus) | Riding (recording) | Paused |
| --- | --- | --- | --- |
| Top right (power) | Hold 2 s: power off. Short: blank or wake the screen; in a list, move focus down (wraps) | Hold 2 s: save the ride and power off. Short: ignored | Hold 2 s: save the ride and power off. Short: on the pause menu and Confirm, move focus down (wraps); otherwise ignored |
| Bottom left | Opens the Menu from Ready; Back in menus | Short: next page. Hold 1 s: previous page | Opens the pause menu; Back on the pause menu and Confirm |
| Bottom right | Start (from Ready); Open, Load, Pair, Save, Change in menus | Short: lap. Hold 1 s: pause | Resume; on the pause menu and Confirm, activate the focused row or button |
| Touch | Tap a row or button to activate it; swipe scrolls lists | Not required | Pause menu rows and Confirm buttons |

Every action on every screen can be reached with the three buttons alone; touch is a convenience, never the only way.

Menu screens carry a 32 px footer showing the bottom-left and bottom-right labels (for example BACK / OPEN) so the physical buttons never need guessing. In a list, one row has the focus (lighter panel, 3 px left bar). The right button activates the focused row; tapping a row focuses and activates it.

The riding assignments are provisional. The reasoning: an accidental lap is harmless but an accidental pause is not, so pause takes the hold while lap takes the short press. A manual lap marks a FIT lap and does not advance a workout. While a button is held, a fill bar along the bottom edge shows progress with the text "0.6 / 1 s" (seconds held of 1), on the side of the button held, and the action fires at 1 s; releasing earlier cancels it, and a hold that has fired swallows the release. The map zooms automatically; there is no manual zoom while riding. There is no skip-step control in v1. Powering off during a ride is safe: a power-off request while recording or paused saves the ride first.

## 5. Ride pages

All pages share a 22 px status bar (connection icons, clock, battery) and page dots at the bottom, one dot per page in the ring. Numerals are Barlow Condensed; labels are Barlow. The status bar shows an icon for each connected sensor or service only; a sensor that is lost has no icon.

**Page ring.** The ring is Main, Workout, Climb, Navigation in that order, but Workout is left out while no workout is loaded and Navigation is left out while no route is loaded (Decided, provisional). Main and Climb are always in the ring (Climb shows live gradient and ascent without a route). The page dots show only the pages in the ring. The Empty boards "noworkout" and "noroute" stay in the simulator as scenarios but are not reached during a ride in v1; "gpswait" is still shown on Navigation when a route is loaded but there is no fix yet.

**Main.** Power (3 s average) as the hero at 96 px with a zone chip and a seven-segment zone strip. A 2 × 3 grid below: heart rate, cadence, speed, distance, elapsed time (ride timer, excluding pauses) and lap time (ride timer since the last lap: a manual lap or a workout step change, or since START). An invalid reading shows "--". The grid replaces the 2 × 2 grid on board 1, which still needs updating; the cells shrink to fit the same area, numerals no smaller than 34 px (Decided, provisional).

**Workout.** Interval time remaining with a progress bar. Power (3 s) with the step's target range in watts beside it (for example 247–260 W). A status pill with icon and text compares 3 s power with that range: IN RANGE, TOO HIGH with the distance above the top of the range (for example "+31 W"), or TOO LOW with the distance below the bottom (for example "−33 W"). A TARGET zone against ACTUAL zone bar (seven segments, ring on the target). Cadence row, with the cadence target when the step has one. Scenarios: inrange, high, low. After the last step it shows the same layout with the title "Workout complete" and the hint "Keep riding, or pause and finish the ride from the menu."

**Climb.** Gradient in % as the hero, tinted orange with a STEEP chip when the gradient is 8.0 % or more for 3 s (off again below 7.0 %). Power (10 s) with a zone chip. An elevation profile strip with a position marker, the climb summary (for example "3.4 km · 212 m climb"), then DIST LEFT and ASCENT LEFT. Rules for the live page are in 8.5. With no route, or off route, or no climbs left, only gradient, power and ascent show.

**Navigation.** A 76 px turn banner (icon tile, distance, instruction). Below it the map (phase 1: route line only, see 8.3 and the brief 6.3) and a strip with DIST LEFT, ETA and SPEED. Scenarios: ontrack (distance in white), turn (banner tile turns blue and the distance counts down, from 100 m), reroute (the board's name for off course: "OFF COURSE" in yellow, route dashed and faded; the sub-line gives the distance back to the route, for example "120 m to the route"). With a route loaded but no fix yet it shows Empty "gpswait".

Zone colours (used everywhere): Z1 #8E9AA6, Z2 #3B8BEB, Z3 #3DBE6B, Z4 #F2C230, Z5 #F08A24, Z6 #E5484D, Z7 #B46BE8. Zone is always also shown as text (Z4) or segment position, never colour alone.

## 6. Other screens

The 24 boards, numbered as on the canvas. "Scenarios" are the board's variants; an implementation renders every one of them.

| # | Screen | What it is and does | Footer (left / right) | Scenarios |
| --- | --- | --- | --- | --- |
| 1 to 4 | Ride pages | Section 5 | none (page dots) | see section 5 |
| 5 | Workouts list | Five rows visible, count "7 / 20", scroll bar. Row: name, duration, step count | BACK / OPEN | none |
| 6 | Workout detail | Step profile (ramps, repeats, dashed FTP line), time, steps, current FTP, LOAD WORKOUT. Loading returns to Ready | BACK / LOAD | none |
| 7 | Ready | Checklist rows for GPS, Power meter, Heart rate, Phone, each with a status pill; the loaded workout (name, duration, steps, FTP) and the loaded route (name, distance, ascent, climbs). START is always enabled | MENU / START | ready, searching |
| 8 | Menu | Rows in this order: Workouts (n of 20), Routes (n saved), Sensors, Profile (FTP), History (n rides), Phone, Settings. Scrolls | BACK / OPEN | none |
| 9 | Sensors | Paired sensors with state, plus Add sensor (hidden once a power meter and a heart-rate strap are both paired; forget one to replace it) | BACK / OPEN | none |
| 10 | Add sensor | Scanning, then results with signal bars and the last four characters of the sensor ID (to tell two nearby PM500s apart) | BACK / PAIR | none |
| 11 | Power meter | Identity, live power and cadence, zero-offset calibration. Kebab in the header opens Forget sensor (screen 19) | BACK / CALIBRATE, WAIT, DONE or RETRY | idle, calibrating, done, failed |
| 12 | Profile (FTP) | FTP large with four 48 px steppers (−5, −1, +1, +5 W; holding a stepper repeats at 5 per second after 0.5 s). Range 50 to 600 W. Applies on SAVE; BACK discards. A CHANGED chip and "was 260 W" show while unsaved. Reachable only when stopped | BACK / SAVE | unchanged, changed |
| 13 | Routes list | Up to 10. Row: name, distance, ascent, climbs. Count "3 / 10" | BACK / OPEN | none |
| 14 | Route detail | Elevation profile with climbs in orange, DIST, ASCENT, CLIMBS, LOAD ROUTE | BACK / LOAD | none |
| 15 | History list | Newest first. Row: date and distance, time and average power, chip UPLOADED or WAITING. Count "12 rides". OPEN shows the ride on the Summary screen | BACK / OPEN | none |
| 16 | Ride summary | Title "Ride saved" with date and time, chip, six stats: TIME, DISTANCE, AVG POWER, AVG HEART RATE, ASCENT, AVG SPEED. Shown after Finish and save and from History | BACK / DONE | waiting (WAITING FOR PHONE), uploaded (UPLOADED) |
| 17 | Paused | Title PAUSED or AUTO-PAUSED, time since the pause, a hint, ELAPSED, DISTANCE, AVG POWER | MENU / RESUME | manual, auto |
| 18 | Pause menu | "Ride paused" with the elapsed time. Rows: Resume (Keep recording), Finish and save (Stop and save this ride), Discard ride (Delete without saving, shown in red). Focus starts on Resume; the power button moves it down (wraps) | BACK / SELECT | none |
| 19 | Confirm | Destructive confirmation. The destructive button is red; the safe button is outlined and focused first. The power button moves the focus between the two buttons. BACK always acts as KEEP; the right button activates the focused button | BACK / KEEP | discard, forgetsensor, forgetphone |
| 20 | Ride alerts | Banner at the bottom of a ride page, and the hold bar | none | nextstep, lap, complete, powerlost, gpslost, lowbatt, holdpause |
| 21 | Empty states | Icon, title, hint, page dots | none | noworkout, noroute, gpswait |
| 22 | Heart rate sensor | Sensor identity, live bpm, signal bars, last seen. Kebab in the header opens Forget sensor | BACK / (none) | connected, searching |
| 23 | Phone | State of the phone link. Kebab (connected only) opens Forget phone. The pairing state shows a 6-digit passkey and the text "Pairing stops after 60 seconds" | BACK or CANCEL / PAIR (not paired only) | connected, pairing, notpaired |
| 24 | Settings | Rows described in 10.1. A tap, or the right button, cycles the focused value | BACK / CHANGE | none |

Rules that the table does not carry:

- **Ready and Menu.** Left from Ready opens the Menu; BACK from the Menu returns to Ready. START from Ready begins recording at once. After START the first ride page is Main.
- **Pause menu and Discard.** BACK on the pause menu returns to the Paused screen. Discard ride opens Confirm "discard". KEEP RIDE returns to the pause menu. DISCARD RIDE deletes the recording and returns to Ready. Finish and save writes the file and shows the Summary; BACK or DONE then returns to Ready. Opened from History, BACK or DONE returns to the History list.
- **Kebab menus.** Where a header has a kebab (screens 11 and 22), it is also the last focusable item in the screen's focus order, so the power button reaches it.
- **Forgetting.** Forget sensor removes the sensor's stored identity; the Sensors list updates at once. Forget phone deletes the bond and returns to the Phone screen in "notpaired".
- **Power meter calibration.** Opcode and states are in the brief 5.11. Whether zeroing is supported is read from the meter's Cycling Power Feature flags when it connects. If the meter does not support zeroing over Bluetooth the screen shows the "failed" layout with the chip UNSUPPORTED, the help text "This meter cannot be zeroed over Bluetooth." and a disabled button. Unverified for the PM500.
- **Load.** Loading a workout or a route replaces the previously loaded one and returns to Ready. Loading is refused (button disabled) while a ride is recorded; those screens are not reachable then.
- **History.** There is no delete on the unit; rides are removed by the retention rule (10.4) or from the app.

## 7. Workouts

### 7.1 File format (v1)

Written by the coach project; full rules are in `claude/workout-coach-instructions.md`. In short: `{"v":1,"name":…,"steps":[…]}` with at most 100 steps after repeats are expanded. A step has `label` (16 characters or fewer, ASCII), `secs` (5 or more), `ftp_pct` (one number, meaning a band of 5% of the target either side, or a pair `[a, b]`), optional `ramp` (only with a pair) and optional `rpm` (40 to 130). A repeat group is `{"reps":2–30,"steps":[…]}` and cannot nest. Targets are percentages of FTP; the file never contains watts. Zone bands: Z1 up to 55, Z2 56–75, Z3 76–90, Z4 91–105, Z5 106–120, Z6 121–150, Z7 over 150.

Earlier versions of the coach document left exactly 55 % unassigned; it now assigns 55 to Z1, as this spec does (provisional). Descending ramps are accepted, and the coach document now says so (its example uses one).

Additional limits applied by the app and the unit (Proposed; now also stated in the coach document): `ftp_pct` values from 1.0 to 300.0 in steps of 0.1; `secs` at most 43,200 per step and 86,400 in total; `name` printable ASCII (0x20 to 0x7E); `label` letters, digits and spaces with at least one non-space. A single target widens by 5 % either side, so a 300 % target becomes 285.0 to 315.0 %; the unit therefore accepts stored percentages up to 315.0 % (protocol 3.2). The complete accept and reject lists, with error codes and the first-error order, are in the test vectors section 4.

### 7.2 On-unit model

The unit stores each workout as percentages of FTP, never watts. The app expands repeats and turns a single-number target into a low and high percentage (the target less and plus 5% of itself), so the unit only has to multiply by FTP. Layout and sizes are in the protocol document, 3.2: a 12-byte common header, a 6-byte total, then per step 11 bytes plus the label, so a 100-step workout is under 3 KB.

FTP is a single value stored on the unit (Profile), default 200 W until set. When a workout is loaded the unit resolves every step to a watts range, rounded half-up to the nearest watt (`(ftp × p10 + 500) // 1000`), and holds the table in RAM; a ramp interpolates linearly and is resolved each second. If FTP changes while a workout is loaded, the table is recalculated. Zones come from the percentage bands, so no zone is stored per step.

Storage (Decided, provisional): workouts, routes, rides and settings are stored on the separate storage, in a folder `CC700/` at its root, with subfolders `objects/`, `rides/` and `config/`. The software never creates, changes or deletes anything outside `CC700/`, so the stock firmware's folders are left as they are. The stock storage appears to be a FAT file system (`/sdcard` paths and FatFS strings in the firmware), whose rename cannot replace an existing file; the brief 5.9 and protocol 7 give the write rules that stay safe on it. The core uses a storage interface, so if the hardware work finds a reason to change this the rest of the software is unaffected.

### 7.3 Library rules

Up to 20 workouts. Workouts are deleted from the Android app only, never on the unit. No add, delete or transfer happens while a ride is being recorded. Identical workouts are deduplicated by the object ID, a hash of the percentage form, so the same workout is one entry whatever the FTP.

### 7.4 Android import flow

Three ways in: paste text, the Android share sheet, or a file picker. Before validation the app normalises typography (× to x, en and em dashes to a hyphen, curly quotes to straight quotes, non-breaking space to space) and applies no other correction. Then validate with clear messages that name the field and position, preview, and convert on Send. The preview shows the profile and step list in watts at the unit's current FTP when connected, or at the last FTP read from the unit (labelled as such) when not. Conversion expands repeats and produces percent low and high, so it does not depend on FTP. The validator and converter are a plain Kotlin module with unit tests, independent of the UI.

### 7.5 Phone to unit transfer

Specified at byte level in `cc700-ble-protocol.md`. In short: a custom GATT service with LIST, PUT (BEGIN, DATA, END with CRC-32 and an atomic rename), DELETE, FTP get and set, time set, and ride list, read and mark operations. FTP is owned by the unit: the app reads it on connect and writes it back only while connected; the unit refuses FTP_SET, PUT and DELETE while recording. Throughput and MTU on this hardware are untested.

### 7.6 Executing a workout

At each step the unit shows the step's watts range, the target zone, time remaining and the next step. The workout clock is the ride timer, so pausing pauses the workout. Rules for the status pill:

- The pill compares 3 s power with the step's own range, not the whole zone band.
- It changes to TOO HIGH or TOO LOW only after 2 consecutive seconds outside the range on the same side, so a narrow range does not make it flicker and a brief swing across the range does not trigger it. Once out of range, crossing to the other side switches at once. It returns to IN RANGE as soon as power is back inside (test vectors, section 7).
- At each step start the pill resets to IN RANGE and shows no state for the first 5 s of the step, because the 3 s average needs that long to reflect the new target. From the fifth second the rules above apply, with the 2 s count starting then (test vectors, section 7.1). Decided (provisional).
- On a step shorter than 60 s the pill shows no state at all; power, the range and the zone bar still show. In a 30/15 set the 3 s average cannot settle inside a 15 s or 30 s step, so a pill there would only show lag. Decided (provisional).
- The watts figure is the distance to the nearest edge of the range.
- For a ramp the target is the current point on the ramp, and the range is that point plus and minus 5%, as for single-number steps.
- The target zone is the zone containing the middle of the range (the current target for a ramp).
- If the 3 s power is invalid the pill shows nothing and the actual-zone ring is hidden.

## 8. Routes and climbs

### 8.1 Routes

Routes come from GPX files imported in the Android app (`<trk>` segments joined, or `<rte>`; waypoints ignored). The app simplifies the line, samples elevation, detects climbs, generates turns and sends one route object (protocol 3.3). The unit holds up to 10 routes and one is loaded at a time (Menu, Routes, detail, Load). There is no routing engine and no route planning in the app in v1; planning is done elsewhere and exported as GPX. Maximum 8000 points, 12000 profile samples and 131,072 bytes per route.

### 8.2 Turn instructions

Turns are generated from the geometry (brief 7.5) because a GPX file carries no road network: a turn is a sharp enough change of direction in the line. Instruction text on the banner: "Continue", "Slight left", "Turn left", "Sharp left", "Slight right", "Turn right", "Sharp right", "U-turn", "Roundabout, exit n", "Arrive", "Depart", followed by " · " and the street name when the route object has one (phase 1 has none). Distance to the turn: 1 km or more shows kilometres to one decimal ("1.2 km"); under 1 km rounds to 10 m, and under 100 m to 5 m. The banner tile turns blue when the turn is 100 m or less away. This is Provisional and expected to be replaced when a routing engine supplies real instructions.

### 8.3 Following and off course

The unit snaps its position to the route once per second (brief 5.7). Off course starts after 8 s beyond 40 m at 3 km/h or faster and ends after 3 s within 25 m. While off course the banner reads "OFF COURSE" with the sub-line "n m to the route" (distance to the nearest point of the route, same rounding as turn distances), and the route is drawn dashed and faded. There is no automatic rerouting in v1.

Phase 1 map: plain background `#0B0D10`, the route line `#4C9AFF` 3 px with the ridden part at 40 % opacity, the rider marker, a scale bar, north up, automatic scale (brief 6.3). Phase 2 adds roads and cycle paths (`#4FD1C5` dashed for cycle paths, as on the Navigation mockup) once the unit's map storage format is understood. The mockup's roads and cycle paths show the phase 2 intent; build phase 1 first.

### 8.4 Climb detection

The phone detects climbs along a loaded route from a digital elevation model and sends the climb list (start, length, ascent, average and maximum gradient) and the elevation profile with the route. Sources, tried in order: Geoscience Australia's 5 m LiDAR DEM (national coverage built from about 236 surveys, focused on populated areas; Perth coverage and the practicality of per-point queries are Unverified); the roughly 30 m SRTM data through Open Topo Data; the GPX file's own elevations as a last resort (CC BY 4.0 data from ELVIS is a manual route to the 5 m data if the service is impractical). Defaults, adjustable in the app and provisional: hysteresis 5 m; merge two climbs when the dip between them is at most 8 m and the gap at most 150 m; keep a climb only if it is at least 300 m long, gains at least 15 m and averages at least 3.0 %. The algorithm and a test are in the test vectors, section 8, and in `tools/reference/ref.py`.

### 8.5 Live climb page

On a route, the active climb is the first one whose end is ahead. When the rider is within 500 m before its start a banner "CLIMB AHEAD" appears (10.2). Inside the climb the page shows gradient from the barometer, power (10 s), DIST LEFT to the summit along the route, ASCENT LEFT (climb ascent less the barometric gain since the climb start; if the rider joins mid-climb, the gain implied by the profile), the profile strip with the position marker, and the summary. Without a route the page shows live gradient and ascent only. Live ascent and gradient come from the barometer in every case (5.3 in the brief). Whether the barometer exists and works is Unverified (the manufacturer advertises one).

## 9. Rides, upload and integrations

The unit records rides as FIT files with position, speed, altitude, distance, power, heart rate and cadence, laps (manual laps and workout step changes) and a session summary. Fields are omitted, never zero, when a sensor is lost. The Android app collects rides over the protocol (RIDE_LIST, RIDE_OPEN, RIDE_READ with CRC-32 check), uploads each to Intervals.icu with `POST /api/v1/athlete/0/activities` (multipart FIT), authenticating with a personal API key over basic auth (OAuth with `ACTIVITY:WRITE` is the alternative), and then marks the ride uploaded on the unit. Intervals.icu is free and every user can call its API with a personal API key, so a paid plan is not needed; the remaining check is Daniel's first upload with his own (regenerated) key. Duplicate protection: before uploading, the app checks for an Intervals.icu activity starting within 60 s of the ride; if one exists the ride is marked uploaded without sending the file again. A power loss during a ride is recovered at the next boot (brief 5.6): the file is closed, flagged "recovered", and listed like any other.

Strava: no subscription, so the Strava API is out. Uploading to Strava is either a paid subscription or a manual upload. Official Live Segments needs a licensed integration plus a user subscription, so it is dropped; personal timed sections from Daniel's own rides remain a possible later feature. Garmin Connect is not useful: the developer programme is business-only and the personal API is read-only.

Workouts from Intervals.icu are not a v1 source; workouts come from the coach project in our own format. Intervals.icu does have a workout builder and its API can download planned workouts from the calendar (as .zwo, .fit, .mrc or .erg, or in its own JSON form), so pulling planned workouts from the Intervals.icu calendar is a candidate second source after v1 (Proposed, not scheduled).

## 10. Behaviour rules

### 10.1 Settings

Screen 24. Each row cycles through its values on a tap or the right button (CHANGE); changes apply and save at once.

| Setting | Values | Default | Notes |
| --- | --- | --- | --- |
| Brightness | Auto, 25 %, 50 %, 75 %, 100 % | Auto | Auto uses the ambient light sensor; without one it is 60 % |
| Screen off | While stopped: Never, 30 s, 1 min, 5 min | 1 min | The screen never turns off while recording or paused |
| Units | km, mi | km | km: kilometres, km/h, metres. mi: miles, mph, feet |
| Auto-pause | On, Off | On | Rules in 10.3 |
| Time zone | Set by the phone | UTC+0:00 until set | Read-only on the unit. The row shows the offset (for example UTC+8:00) |
| Firmware | version | | Read-only, for example v0.1.0 |

### 10.2 Alerts

Banners appear at the bottom of a ride page, above the page dots, and the page stays visible above them. One banner at a time; priority from highest: storage error, power lost, GPS lost, low battery, workout complete, climb ahead, next step, lap. A higher priority replaces the visible banner; an equal or lower one waits (one deep) and is dropped if it would show more than 5 s late. Text is exactly as in the mockup (uppercase titles).

| Alert | Trigger | Title | Sub-line | Right | Style | Shown for |
| --- | --- | --- | --- | --- | --- | --- |
| nextstep | Last 5 s of a step of 60 s or longer, not the last step. Shorter steps (for example the 30 s and 15 s steps of a 30/15 set) get no banner; the step timer counts them down | NEXT · (label of the next step, uppercase) | (range of the next step) for (its length), for example "136–150 W for 4:00" | seconds left, for example "5 s" | Panel, accent = zone colour of the next step | Until the step ends |
| lap | Manual lap | LAP n | lap time · lap distance, for example "5:12 · 2.4 km" | none | Panel, white accent, flag icon | 4 s |
| complete | The last step ends | WORKOUT COMPLETE | workout name | none | Green `#3DBE6B`, dark text, check icon | 10 s or any button |
| powerlost | No power data for 3 s | POWER METER LOST | Trying to reconnect | none | Red `#E5484D`, dark text | 6 s; power and cadence show "--" while lost; no banner on recovery |
| gpslost | No usable fix for 10 s while recording | GPS SIGNAL LOST | No speed or distance | none | Yellow `#F2C230`, dark text | 6 s; speed shows "--" while lost; no banner on recovery |
| lowbatt | Battery reaches 15 %, and again at 5 % | LOW BATTERY 15 % (or 5 %) | Recording continues | none | Orange `#F08A24`, dark text | 8 s, once per threshold per ride |
| climbahead | Within 500 m before a climb on the loaded route | CLIMB AHEAD | length · ascent · average gradient, for example "1.1 km · 48 m · 4.5 %" | distance to the start | Panel, white accent. No mockup: reuse the lap layout | 6 s |
| storage | Saving fails for 30 s | STORAGE ERROR | Ride may not be saved | none | Red, as powerlost. No mockup | Until storage works again |
| holdpause | A bottom button is held | (no banner) | | | Hold bar along the bottom with "n.n / 1 s" | While held |

Alerts are visual only in v1. Leave a hook (`alert_sound()`, `alert_vibrate()`) that does nothing, because no buzzer or vibration motor has been identified.

### 10.3 Timers and auto-pause

Elapsed time on the ride pages and the Summary is the ride timer: seconds in RECORDING, excluding pauses. Average speed is distance over ride timer. With Auto-pause on, recording pauses automatically when, for 5 s in a row, the GNSS speed is below 0.7 m/s (2.5 km/h) **and** the rider is not pedalling (cadence is 0 or invalid, and 3 s power is 0 or invalid). It resumes when the speed is 1.4 m/s (5 km/h) or more for 2 s in a row, or when the right button is pressed. The pedalling condition stops an indoor ride on a trainer, where GNSS speed sits near zero, from pausing and freezing the workout clock. The Paused screen shows AUTO-PAUSED in that case. Auto-pause is suspended while the GNSS has no usable fix. Provisional.

### 10.4 Retention

The unit keeps up to 100 rides. When a new ride is saved beyond that, it deletes the oldest ride whose uploaded flag is set. It never deletes a ride that has not been uploaded: if none of the stored rides is uploaded, the new ride is kept anyway and the count may rise above 100, up to 240 (the most RIDE_LIST can report in one message, protocol 4). Only beyond 240 is the oldest ride deleted whatever its flag. The History count shows in yellow while more than 100 rides are stored, as a cue to sync. It never deletes a ride because it is old. Workouts and routes are never deleted by the unit. Provisional.

### 10.5 Pairing and connection

One phone can be bonded. Pairing is opened from Menu, Phone, PAIR and lasts 60 s; the unit shows a random 6-digit passkey and the app asks for it. Any other pairing request is rejected. Forget phone deletes the bond. The unit advertises whenever no phone is connected, including while riding, and the app reconnects by itself; transfers that change the unit are refused during a ride (protocol 4). Details: protocol 2.2 to 2.4.

### 10.6 Time

The unit's clock comes from the GNSS (the receiver often reports UTC before it has a position fix). Until then it uses the time sent by the phone. The time zone offset is sent by the phone on every connection and applies to the clock, the History dates and the file names shown on screen. FIT timestamps are UTC. If UTC is unknown at START, the ride is recorded against the unit's monotonic clock; when UTC becomes known during the ride (GNSS or the phone), the unit stores the offset, and at finish or recovery it corrects every timestamp in the file and takes the corrected start time as the ride's ID. A ride that ends without UTC ever being known is flagged "time unknown" (protocol 4, RIDE_LIST flags) and the app asks for its date before upload.

### 10.7 Sensors, loss and recovery

Sensors reconnect by themselves with a backoff of 1, 2, 4, 8, 15 then 30 s. A power meter is lost after 3 s and a heart-rate strap after 5 s without data (brief 5.1). Data from a lost sensor is shown as "--" and omitted from the file. A valid 0 W is a number; never show an old value in place of a missing one. Each one-second value is the mean of the readings received in that second (brief 5.1). A sensor appears on the Ready screen as SEARCHING until its first data arrives.

### 10.8 Power-off

A power-off request (hold 2 s) while stopped powers off after saving settings. While recording or paused it first does Finish and save without showing the Summary. At boot, an unfinished ride is recovered before the Ready screen.

## 11. Android app

Kotlin and Jetpack Compose. Screens: Unit (status, pairing, forget), Workouts (library mirror with delete, import, preview, send), Routes (GPX import, preview with profile and climbs, send, delete), Rides (rides on the unit, sync, upload status and retry; a date prompt for a ride flagged "time unknown"), Profile (FTP: read from the unit on connect, written back when edited, read-only when disconnected), Settings (Intervals.icu key, climb thresholds, elevation source). The Intervals.icu API key lives in Android secure storage (a Keystore-wrapped key), never in source, logs or documents. Module split, libraries and tests are in the engineering brief, section 7. Route planning, rerouting and map downloads are carried over from the handover as later phases; the routing engine and offline graph are not selected.

## 12. Visual design system

| Token | Value |
| --- | --- |
| Background | #0B0D10 |
| Bars (status, footer) | #12161B |
| Panels | #1B2027 |
| Dividers | #232830 |
| Muted text | #9AA4B0 |
| Text | #F2F5F8 |
| Route / cycle path | #4C9AFF / #4FD1C5 |
| Zones | Z1 #8E9AA6, Z2 #3B8BEB, Z3 #3DBE6B, Z4 #F2C230, Z5 #F08A24, Z6 #E5484D, Z7 #B46BE8 |

Type: Barlow Condensed for numerals, Barlow for labels, sizes in the engineering brief 6.2. Status and error chips use dark text on the colour (contrast at least 4.5:1). Rows are 46 px, headers 34 to 44 px, footer 32 px, status bar 22 px, touch targets at least 44 px. Focus in lists is a lighter panel with a 3 px left bar. Pending or in-progress states use a dashed yellow outline. Destructive screens default focus to the safe option. State is never conveyed by colour alone.

## 13. Open questions, unverified items and provisional decisions

### 13.1 Open questions and unverified items

| # | Item | Why it matters | Resolve by |
| --- | --- | --- | --- |
| 1 | Zero-offset calibration over Bluetooth on the PM500 | The Power meter screen assumes the unit can trigger it. The Cycling Power Service defines a Start Offset Compensation command (opcode 0x0C by recollection, not checked against the specification) and a feature flag saying whether a meter supports it; the PM500 may not | Check the specification; read the PM500's Cycling Power Feature flags; PM500 manual or GEOID support; test with the real meter |
| 2 | ANT+ availability | Decides whether Bluetooth-only sensors is enough | Identify the unit's radio hardware |
| 3 | Hardware identification and recovery path | Gate for flashing | Handover steps 1 and 2; BRINGUP.md |
| 4 | Storage location: **decided (provisional)** as the separate storage, folder `CC700/` (7.2). Still unverified: that it is FAT, how it is mounted, and its free space | Write rules and recovery depend on the file system | Hardware identification; BRINGUP.md step 3 |
| 5 | Intervals.icu upload with Daniel's account. API access is free for every user; only the first real upload is unchecked | Confirms the whole sync path end to end | Daniel uploads one FIT file with his regenerated key |
| 6 | 5 m DEM coverage of Perth and per-point query practicality | Falls back to SRTM at 30 m, which is coarse for short climbs | WP5 elevation spike, results in DEM-FINDINGS.md |
| 7 | Whether the unit has a working barometer | Gradient, ascent and the Climb page depend on it | Hardware identification |
| 8 | Bluetooth throughput, MTU and three concurrent connections on this hardware | Sets transfer times and whether sensors and phone coexist | Bring-up |
| 9 | Routing engine and offline graph | Needed for rerouting and real turn instructions | Handover item, not started |
| 10 | Map data format on the separate storage | Needed for phase 2 base map | Hardware work |
| 11 | Responsiveness baseline of the stock unit | The replacement must beat it; none measured | Handover measurement plan |
| 12 | Whether `ota_1` is empty and the stock bootloader honours `otadata` | The safer bring-up path installs into `ota_1` | BRINGUP.md step 4 |
| 13 | Whether a buzzer or vibration motor exists | Alerts are visual only until then | Hardware identification |
| 14 | Whether the stock firmware switches the USB port to its own USB stack (TinyUSB strings are present), and whether test images can run from RAM | Decides whether recovery keeps working over USB without the physical BOOT pin, and whether hardware probing can avoid flash writes | BRINGUP.md steps 2 and 3 (brief section 9) |
| 15 | Board 1 (Main) mockup shows the old 2 × 2 grid | The contact sheet must match the spec | Update `gen.py` before WP4; until then the spec governs the field list |

The mockups use 260 W as a placeholder FTP, not Daniel's actual value; the unit's default is 200 W until set.

Housekeeping: the Intervals.icu API key was pasted into a chat once. Regenerate it in Intervals.icu settings before using it in the app. It has not been stored or used anywhere.

### 13.2 Provisional decisions to revisit after real use

| Decision | Current choice | What to watch for |
| --- | --- | --- |
| Status pill range | Step's own range, 2 s on the same side before leaving IN RANGE, zone of the range midpoint | Flicker on narrow steps, or too forgiving on wide ones. Adjust the 2 s, or add a tolerance |
| Riding buttons | Bottom left: next page, hold for previous. Bottom right: lap, hold 1 s to pause | Accidental laps or pauses with gloves; whether 1 s is the right hold |
| Power button | Hold 2 s off; short press blanks the screen or moves focus (lists, pause menu, Confirm) | Accidental power-off; whether focus needs a better control |
| Main page fields | 2 × 3: heart rate, cadence, speed, distance, elapsed, lap time | Numerals too small at a glance; wanting a different field |
| Page ring | Workout left out with no workout, Navigation left out with no route | Confusion when a page "disappears"; wanting a fixed ring |
| Map zoom | Automatic only | Missing manual zoom on unfamiliar routes |
| Skip step | Not included | Wanting to skip or extend a step mid-workout |
| Navigation phase 1 | Route line only, no base map | Whether the route line is enough without roads |
| Turn generation | From geometry: 35, 60, 120, 165 degree bands, merge within 40 m | Too many or too few prompts on real routes |
| Off course | 40 m for 8 s out, 25 m for 3 s back; no rerouting | False alarms at junctions; wanting rerouting |
| Climb defaults | 5 m hysteresis, merge 8 m / 150 m, minimum 300 m, 15 m, 3.0 % | Which climbs appear on known local climbs |
| STEEP chip | 8.0 % for 3 s on, below 7.0 % off | Flicker; whether 8 % suits |
| Short-step cues | No next-step banner and no pill on steps under 60 s; pill hidden for the first 5 s of every step | Missing cues in short-interval sessions; whether 60 s and 5 s are right |
| Auto-pause | Below 2.5 km/h for 5 s while not pedalling; resume above 5 km/h for 2 s | Pausing at slow climbs or in traffic; not pausing when soft-pedalling at lights |
| Alerts | Thresholds and durations in 10.2; visual only | Missing sound; banners covering data |
| Retention | 100 rides, oldest uploaded deleted first, un-uploaded kept up to 240 | Running out of space; long gaps between syncs |
| Storage location | Separate storage, `CC700/` folder only | Conflicts with the stock firmware's files, if it is ever restored |
| FTP default | 200 W until set | None; set it on first use |
| Library caps | 20 workouts, 10 routes | Needing more routes |

## 14. Order of work

See the engineering brief, section 8: WP0 repository and harness; WP1 core workouts, zones, sensors; WP2 core ride engine, FIT, navigation; WP3 protocol, storage and the simulated unit; WP4 LVGL simulator and screenshots (stop for Daniel's review); WP5 Android core and tools; WP6 Android app; WP7 firmware skeleton and the bring-up plan, build only. After the first real rides, revise 13.2.
