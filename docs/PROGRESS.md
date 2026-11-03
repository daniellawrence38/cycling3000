# CC700 progress

Appended at the end of each work package. Nothing here has run on the unit; everything is host-side only.

## Environment used

Linux container: GCC 13.3, CMake 3.28, Python 3.13 (pytest, fitdecode from PyPI), JDK and Gradle present, no SDL2 development package yet, no ESP-IDF installed.

## WP0 Repository and harness · 3 November 2026

**What exists.** The layout from brief section 2 at the repository root: `core/` (C11 library, `include/cc700/`, `src/`, `tests/`), `hal/`, `ui/` (`sim/`, `golden/`, `fonts/`), `simunit/`, `fw/`, `android/`, `tools/` (`reference/`, `design/` copied from `claude/tools/`, plus `fitcheck/`, `gen_ride/`, `tests/`), `docs/` and a root `Makefile` with a CMake build for the C code. `core/` has `util.h`/`util.c` (CRC-32, SHA-256, varint, zigzag, little-endian helpers, `cc_floordiv`, half-up rounding). Unity v2.6.1 is vendored in `core/tests/unity/`.

**How to run.**

- `make test`: runs `make vectors`, then every host C test twice (plain, and under ASan and UBSan with `-fno-sanitize-recover`), then the Python tool tests. C is built with `-Wall -Wextra -Werror -Wshadow -Wstrict-prototypes`.
- `make vectors`: runs `tools/reference/vectors.py`, which writes `tools/reference/cc700-test-vectors.md` (git-ignored), and diffs it against `claude/cc700-test-vectors.md`; any difference fails.
- `make sim`, `shots`, `android`, `fw-build` exist and fail with "not built yet" until their work package.

**Verified.** `make vectors` reproduces `claude/cc700-test-vectors.md` byte for byte. Unity tests pass for CRC-32 of `123456789` (`CBF43926`), incremental CRC, SHA-256 of `abc` and the two-block FIPS 180-2 example, in both builds.

**Pinned versions.** ESP-IDF v5.5.6 (`fw/IDF_VERSION`, the newest 5.5 patch tag on 3 November 2026), LVGL v9.6.0 (`ui/LVGL_VERSION`, the newest 9.x tag). Neither is fetched yet.

**Deviations.** No CI (the brief asks for local scripts only). Unity is vendored rather than fetched at build time, so builds work offline.

**Unverified.** Nothing hardware-related was touched or tested.

## WP1 Core: workouts, zones, sensors · 3 November 2026

**What exists.** In `core/`: `provisional.h` (every provisional constant in one header: pill dwell 2 s, settle 5 s, short step 60 s, next-step lead 5 s, FTP limits, sensor timeouts, auto-pause, STEEP, off-route, retention, library caps, low battery), `zones.h` (watts, widening, bands, zone of power, ramp target, zone colours), `workout.h` (CW1 decode and validate, encode for tests and tools, object ID, step range at FTP and elapsed time, target zone, the status pill, and the workout engine), `sensors.h` (Cycling Power and Heart Rate parsers, cadence, half-up bin mean, a one-second channel with the 2 s stale rule, 3 s and 10 s averages and loss, and a combined sensors object with the cadence rules). `tools/reference/make_fixtures.py` encodes the six starter workouts with `ref.py` into `core/tests/fixtures/`.

**How to run.** `make test` (or `make test-c` for C only).

**Verified, in both the plain and the ASan+UBSan builds.**

- V§2.1 to V§2.3: W1 and W2 golden bytes decode to every field in the tables (labels, seconds, p0, p1, ramp, rpm, repetitions), ID and CRC-32 match, watts at FTP 260 and 250 at each step start match, and re-encoding gives the same bytes.
- V§2.4 every row including 300 % (a `p1` of 3150 decodes); V§2.5 every ramp row and the floor-division table (the test also shows that truncating `/` gives the wrong column); V§3 every row and the target-zone examples.
- V§4 as decode-time structure checks: every accept case encoded and decoded; the rejections that exist at object level (seconds, percentages, fixed and ramp ordering, rpm, label and name length, total over 24 h, magic, version, flags, step count, ID, trailing bytes, every truncation of W1). The JSON-level rejections (`E_JSON`, `E_VERSION` and so on) belong to the phone validator in WP5.
- V§6.1 to V§6.4 every row; stale carry at exactly 2000 ms and invalid at 2001 ms; power lost at 3000 ms and recovery; a valid 0 W bin; 3 s and 10 s averages skipping invalid bins; cadence held, then 0 after 3 s without a new crank event, invalid when power is lost; negative power clamped.
- V§7 every row through `cc_pill_feed`; V§7.1 every row through the workout engine, plus a 30 s step (hidden throughout, no next-step banner), a 60 s step (normal rules) and invalid power (no state, no actual zone).
- Engine over W1 at FTP 260: step changes at the right seconds (9 lap events), one COMPLETE at 4080 s, next-step banner exactly in the last 5 s of each of the 9 steps before the last, ramp ranges each second (cool-down at 300 s is 117 to 130 W), FTP change to 250 recomputes the range to 238 to 250 W.
- The six starter workouts: `ref.py` objects decode in C with the same size, step count, total, ID and CRC; through the engine none shows a pill on a step under 60 s and the next-step banner fires once for each step of 60 s or longer that is not the last (5 in VO2max 30-15).
- Fuzz: 1,000,000 inputs into the CW1 decoder (random bytes, and mutated W1 with the ID re-stamped so the deep checks run), each in an exactly-sized heap buffer: no crash and no ASan or UBSan report.

**Deviations and choices.** The unit also rejects non-zero header flags, reserved step flag bits and non-printable name or label bytes (Q14). The pill state is held, not reset, while the 3 s power is invalid (Q15). The engine computes the workout total from the steps rather than trusting the header (the decoder already rejects a mismatch). The engine expects one call per ride-timer second, so it stops with the ride timer during pauses.

**Unverified.** All of it on the unit. Real Cycling Power notifications from the PM500 (flags it actually sets, notification rate) are unknown.

## WP2 Core: ride engine, FIT, altitude, navigation, alerts, settings · 3 November 2026

**What exists.** In `core/`: `fit.h` (FIT writer with a RAM buffer, definition-aware walker, recovery, time shifting, CRC-16 and CRC-32), `settings.h` (two-slot settings and sensor bonds), `altitude.h` (pressure to altitude, 0.25 filter, GNSS offset over the first 30 s, ascent with 1 m hysteresis, the gradient ring, STEEP), `nav.h` (CR1 decode, equirectangular projection about the midpoint latitude, windowed and global snapping, off route, turns and their texts, ETA, map scale, live climb including a mid-climb join), `alerts.h` (one banner, priority, one-deep queue with the 5 s drop, the spec 10.2 texts), `store.h` (ride files: `.fit.part`, `.time`, `.fit`, `.meta` written through `.meta.tmp`, an empty `.up` marker for uploaded, boot recovery and repair, retention), and `core.h` (the single-threaded core: state machine, hold detection, the per-second pipeline, auto-pause, provisional time, FIT recording, alerts and the `cc_view_t` snapshot). `hal/` has all eight HAL headers; `hal_fs` is a function table so tests can inject faults. In `tools/`: `gen_ride/gen_ride.py` (indoor, red-light, climb and route-replay fixtures, the route as CR1 and GPX), `fitcheck/fitcheck.py` (independent FIT validation with fitdecode) and `fitcheck/test_fit_recovery.py`. Test helpers: `core/tests/memfs.c` (an in-memory file system with FAT rename rules, a power cut after N bytes, failing writes) and `player.c` (plays a fixture into the core, synthesising crank events from cadence).

**How to run.** `make test`. The full every-byte FIT check with fitdecode: `make test-c-plain && CC_FIT_FULL=1 python3 -m pytest -q tools/fitcheck/test_fit_recovery.py -k cuts` (about 7 minutes on 4 cores). `python3 tools/gen_ride/gen_ride.py` regenerates the fixtures (deterministic).

**Verified.**

- State machine: a table of 44 rows covering every transition in brief 5.5, every illegal event in each state (START while riding, holds while paused, touches outside the menus), focus movement and wrap with the power key in PAUSE_MENU and CONFIRM_DISCARD, BACK as KEEP on Confirm even with DISCARD focused, touch selection, power-off from each state, and which gestures go to the UI. Hold detection fires at 1 s with the bar showing elapsed time, an early release is a short press, the release after a fired hold is swallowed. A finished ride leaves `.fit` and `.meta`; a discarded one leaves nothing.
- FIT, in C: a synthetic 30-minute ride (pause, manual lap, 10 s power loss, heart-rate gaps) finishes with header and file CRCs valid, 1,800 records, 2 laps, and session totals equal to values computed alongside the ride. Recovery at **every** byte offset from the end of the first definition to the end of the `.part` (45,277 cuts) gives a valid file whose record count equals the complete records before the cut; recovery after a cut anywhere inside the closing messages keeps every record and writes one session. Storage failing for 120 s keeps every record in RAM and the file is valid after storage returns.
- FIT, independently with fitdecode: the finished file decodes with CRC checking on and its session totals (timer 1,800 s, elapsed 1,860 s, distance 14,400 m, average power as the half-up mean of the 1,790 valid powers) match; power and heart rate are absent, not 0, where they were invalid; all 45,277 recovered cuts decode with the expected record count (run once with `CC_FIT_FULL=1`; `make test` checks about 1,500 sampled cuts).
- Unknown time: a ride started with no UTC that learns UTC at 120 s finishes, and separately recovers after a power cut, with every timestamp equal to the true UTC and the file and `.meta` renamed to the true start; a ride that never learns UTC keeps its provisional ID and has the "time unknown" flag. Checked in the FIT module and through the core.
- Settings: power cut at every byte of a save (0 to 40) always loads the previous settings, or the new ones once complete; sequence numbers wrap; corrupt slots load defaults and are left in place; bonds the same.
- Auto-pause: the indoor fixture (20 minutes, GNSS speed 0 to 0.5 m/s, cadence 85, 200 W) never pauses; the red-light fixture pauses after row 66 and resumes after row 121, both worked out by hand in the test; the ride timer matches.
- Gradient and ascent (climb fixture, 5 % at 5 m/s): gradient 5.0 % in the steady state, STEEP never on, ascent 30 m. **Watch item 5.3:** with this fixture the gradient never exceeded 5.0 %, so no overshoot was seen; the fixture's flat-to-climb change is abrupt, so this suggests the overshoot is small, but real roads and real barometer noise are untested.
- Navigation: V§9 decodes to every field and re-encodes to the same bytes; every truncation is refused. The replayed ride gives OFF COURSE at 114 s and back at 137 s, the detour OFF at 427 s and back at 482 s (all worked out by hand), one CLIMB AHEAD, the four turns in order, the instruction "Turn left · Fraser Ave", distances to the turn, ETA, map scale, and the climb page fields; a mid-climb join uses the profile for the gain so far. Turn distance text follows spec 8.2.
- Alerts: every spec 10.2 text, colour style and duration; expiry at exactly the duration; WORKOUT COMPLETE dismissed by any button; STORAGE ERROR until cleared; next step until the step ends with a live countdown; higher priority replaces, lower waits and is dropped when more than 5 s late. Through the core: power lost after 3 s (no banner on recovery), GPS lost after 10 s with distance held, low battery once at 15 % and once at 5 %, storage error after 30 s of failing writes with recording continuing and the banner clearing when storage works, next-step and complete banners from W2, and the page ring with a workout loaded.
- Everything above passes in the plain build and under ASan and UBSan.

**Deviations and choices.** Q16 (records only while recording), Q17 (RAM buffer drops new records when full), Q18 (lap numbering), Q19 (GNSS altitude without a barometer), E5 and E6 (finish sequence). Uploaded is an empty `rides/<id>.up` marker file, so marking never rewrites a file in place. A recovered ride's end time is one second after its last record. Two route-replay thresholds fell exactly on a sample, so the fixture keeps 2.5 m clear of them and the turn checks look at the seconds either side of each switch. Settings changes made from the unit are saved at once; the core has no "save later".

**Unverified.** Everything on the unit: the file system really being FAT, its write speed and sync cost, barometer behaviour and noise, GNSS fix and speed quality, the PM500's notifications. Navigation was tested on a synthetic route, not a real GPX ride.

## WP3 Core: protocol, storage, simunit · 3 November 2026

**What exists.** `core/include/cc700/link.h` and `link.c`: packet framing (fragment and reassemble, with the drop rules), the protocol server for every request in protocol section 4, the 15 s idle limit on PUT sessions, EVENT_STATE (state change or a 5 % battery step) and EVENT_RIDE_SAVED. Objects are written to `objects/<id>.tmp`, checked (length, CRC-32, structure, ID) and renamed to a name that does not exist yet. `version.h` holds the firmware version (0.1.0). The core binds itself to the server through `cc_link_host_bind`, so FTP_SET goes through the same rules as the Profile screen, TIME_SET sets the clock (GNSS time wins) and time zone, and deleting the loaded workout or route unloads it. `simunit/`: `main.c` (the simulated unit), `dirfs.c` (directory-backed `hal_fs` that refuses a rename onto an existing file), `player.c` (moved here from the tests; shared with them). `tools/linkclient/cc700link.py` is a Python client for the end-to-end tests. `tools/gen_ride` adds `ride30.csv` (30 minutes with a red light, a GNSS gap and a power-meter gap).

**How to run.** `make test` builds and tests everything. `simunit/README.md` shows how to run the unit by hand.

**Verified.**

- V§5: every example message fragments to the stated packet counts at 19 and 243 data bytes and reassembles to the same bytes; the HELLO response is byte for byte `81 01 00 01 00 01 00 44 00 0d 07` (with 7 workouts and 3 routes stored and battery 68); FTP_SET 260 answers `83 02 00`; OBJ_PUT_BEGIN for W1 is the stated bytes; OBJ_PUT_DATA with all of W1 is 233 bytes, 13 packets with headers `80 01 … 0b 4c` and the stated first two and last packets at MTU 23, 1 packet at 247, and the unit replies `92 04 00 e3 00 00 00`; the BAD_CRC case (CRC `F5C8CCE3`) returns status 1 and leaves no file; a 4000-byte chunk is 17 packets at MTU 247.
- Reassembly drops: a packet without START when idle, a counter mismatch, a gap over 2 s (exactly 2 s is kept), more than 4096 bytes.
- Every request: OK, and each error the protocol lists. HELLO with another protocol (BAD_ARG with the unit's own proto); FTP_SET out of range, short and while riding; TIME_SET in any state; everything except HELLO, FTP_GET, TIME_SET and OBJ_LIST is BUSY while riding or paused; unknown opcodes; OBJ_LIST contents for workouts and routes; PUT_BEGIN bad kind, TOO_BIG for each kind, FULL by library count and by free space, EXISTS, an earlier session aborted; PUT_DATA NO_SESSION, OFFSET with the expected offset, past the declared size, IO_ERROR with the offset to resume from, more than 4000 bytes; PUT_END NO_SESSION, short object, BAD_CRC, BAD_OBJECT (broken structure, and an ID that differs from the BEGIN); the idle session expiring after 15 s and removing its temporary file; OBJ_DELETE OK (and the core is told), NOT_FOUND, bad kind; RIDE_LIST, RIDE_OPEN, RIDE_READ (in range, past the end, length 0 and 4001, NOT_FOUND), RIDE_MARK set and clear and NOT_FOUND, RIDE_DELETE (removes `.fit`, `.meta` and `.up`) and NOT_FOUND; trailing unknown bytes ignored.
- PUT_DATA with a 41 KB route at 512 and 4000 bytes per chunk; RIDE_READ of a ride at 512 and 4000, reassembled and CRC-checked.
- Atomic commit: a write failure at each write of an upload leaves either the complete object or nothing at the final name; a power cut at every byte of an upload followed by a boot leaves the old state or the new one and no temporary file.
- Retention: 101 rides with uploaded ones deletes the oldest uploaded; 101 with none uploaded deletes nothing; 241 un-uploaded deletes only the oldest; RIDE_LIST then reports 240 rides in one 4,084-byte message.
- simunit: the 30-minute scripted ride runs headlessly in well under a second and leaves one `.fit` and `.meta` that fitcheck (fitdecode) accepts, with the auto-pause, the 8 s power gap (no power field) and the 20 s GNSS gap (no position) visible in the records; only `objects/`, `rides/` and `config/` are created. Over TCP: HELLO, FTP get and set, TIME_SET, all six starter workouts sent (EXISTS on a repeat) and listed with their names and step counts, a route sent and listed, a workout deleted, and a full ride sync at MTU 23 with 512-byte chunks and a CRC check, then RIDE_MARK.
- All C tests pass in the plain build and under ASan and UBSan.

**Deviations and choices.** E7: response opcodes for the ride requests coincide with event opcodes; events are told apart by req_id 0, and the unit ignores requests with req_id 0. HELLO answers BAD_ARG for another protocol version but still fills in the rest of its payload. PUT_BEGIN refuses an object below 12 bytes with BAD_ARG. A PUT_END with fewer bytes than declared is BAD_ARG. Pairing is not simulated yet: any TCP client counts as the bonded phone (WP6 adds the simulated passkey).

**Unverified.** Real BLE: MTU negotiation, throughput, GATT security, three concurrent connections, notification pacing. Real FatFS behaviour.

## WP4 UI simulator · 3 November 2026 · stopped for Daniel's review

**What exists.** `ui/`: LVGL 9.6.0 screens for all 24 boards (`src/`), a layout kit that reproduces the mockups' building blocks, 48 scenario fixtures named exactly as the boards' scenarios, generated fonts (39 combinations, the ones the code uses), 39 icons rendered from the mockups' SVG paths, and `ccsim` (headless screenshots, layout lint, a scripted walk, an interactive SDL window with `--perf`). The live simulator is a screen manager on top of the core: the menu tree with focus moved by the power button and tap-to-activate through the focus order, the ride page ring, and the paused, pause menu, confirm, summary, history, profile, settings, sensors and phone flows (sensors, calibration and phone pairing are simulated). `tools/design/gen.py` now generates board 1 (Main) with the 2 × 3 grid; `tools/design/render_boards.js` renders all 48 mockup scenarios offline; `tools/design/contact_sheet.py` builds the sheet. Core changes in this package: the LOW BATTERY title is "15%" as in the mockup (it was "15 %"); the map scale keeps the next turn above the rider (Q21); the view carries the last position and course; the next turn's type is in the nav view.

**How to run.** `make sim` then `build/host/ui/ccsim` (window: P power, A bottom left, L bottom right, mouse for touch). `make shots` writes `ui/out/contact-sheet.png`. `make test` now also runs `ui_lint` and `ui_walk`. A copy of the contact sheet and the walk's screenshots is in `docs/wp4/`.

**Verified.**

- Every one of the 48 scenarios renders; the contact sheet pairs each with the mockup rendered by a browser. Text placement was measured against the mockup renders: within 1 px for labels and numerals, 2 px for the 96 px power figure.
- Layout lint over every scenario: interactive objects at least 44 × 44 px and in the focus order (including the kebab menus, added last), no child outside its parent, no clipped text unless it ends in an ellipsis, no text below 10 px. 0 problems, no exceptions.
- Scripted walk (`ccsim --walk`, a ctest): Ready, Menu, Workouts, detail, LOAD (back to Ready), Routes (focus moved with the power key), detail, LOAD; START; all four ride pages in order with the left button, wrap to Main, hold left for the previous page; a lap banner; the hold bar at 0.6 s; manual pause; pause menu with focus on Resume, moved to Finish and save with the power key only; Summary; DONE to Ready with the ride in History; a second ride with no workout and no route shows the two-page ring Main, Climb. 0 failures; 19 screenshots.
- `--perf` prints input-to-redraw percentiles. On this PC the walk measured p95 about 3 ms; that says nothing about the unit (brief 6.6: targets are checked on hardware).

**Deviations and choices.** Q20 (no `→` glyph), Q21 (map scale), Q22 (route 3 px on `#0B0D10` per the spec, not the mockup's 8 px on `#10151B`), Q23 (Profile steppers with buttons), Q24 (spec over mockup for Add sensor and status icons). Condensed fonts of 44 px and above also carry `k` and `m`, because the Navigation banner shows "250 m" and "1.2 km" at 44 px (brief 6.2 lists digits only). The hold bar fills from the left, as the mockup shows, with the text "HOLD FOR PREVIOUS PAGE" for the left button (the mockup shows only the right). History shows ride times as h:mm:ss as the mockup does. Screens are rebuilt from the model when they redraw rather than updated field by field; that is fine on a PC and will be reworked for the unit if WP7 or bring-up shows it is too slow. Tabular figures are not available from the generated fonts, so digits are proportional (numbers can shift by a pixel as they change). Golden images are not made yet: they wait for Daniel's approval of the contact sheet.

**Flash cost of the fonts.** 39 fonts, 106,648 bytes of glyph bitmaps (compressed, 4 bits per pixel), plus their tables.

**Unverified.** Everything on the unit: the ST7789 colours and gamma, touch accuracy with gloves, real redraw speed, the font cost in the real build.

**Stop.** WP4 stops here for Daniel's review of `docs/wp4/contact-sheet.png`. Golden images follow approval. Next in order: WP5 (Android core and tools) and WP6 (Android app), which the brief allows while waiting.

## WP5 Android core and tools · 3 November 2026

**What exists.** `android/` is a Gradle project (wrapper 8.14.3, Kotlin 2.0.21, JVM target 17, built here with JDK 21) with `:core` (pure Kotlin/JVM) and `:testing`. `:core` holds `workout` (typography normalisation, validation with V§4's codes and JSON-pointer paths, readable messages such as "Step 3: label must be 1 to 16 letters, digits or spaces", conversion, CW1 encode and decode, the unit's maths for the preview), `route` (GPX `<trk>`/`<rte>` import, 1 m de-duplication and Douglas-Peucker from 3 m up to at most 8000 points, haversine distances, profile spacing `max(25, ceil(dist/12000))`, gap filling then a 3-sample median and a 5-sample mean, `detect_climbs` ported exactly, geometric turns, CR1 encode and decode, and `buildRoute` for the whole pipeline), `link` (framing, reassembly, `LinkTransport`, and `LinkClient` with the connect sequence, 4000- or 512-byte chunks by MTU, PUT with OFFSET resume, ride fetch with size and CRC check, events by req_id 0), `dem` (`ElevationProvider`, `GaProvider`, `OpenTopoProvider`, `ChainProvider`, a cache keyed by 1e-4 degree cell) and `upload` (`IntervalsClient`). `:testing` holds `FakeUnit` (the unit's protocol in Kotlin, mirroring `link.c`), `SocketTransport` (simunit's TCP framing) and the shared `ProtocolSuite`. `tools/reference/export_fixtures.py` exports the reference objects behind the vectors to `android/core/src/test/resources/vectors.json`; `tools/dem_spike/dem_spike.py` is the elevation spike for Daniel to run.

**How to run.** `make android` (builds simunit first, then runs both modules' tests). `make test` is unchanged and does not need Gradle.

**Verified (JVM tests, 41 in all).**

- V§1 anchors; V§2.1 and V§2.3: W1 and W2 from their JSON give byte-identical CW1 objects, IDs and CRCs, and decode back; every W1 step's watts at FTP 260 and 250; V§2.4 every row including 300 %; V§2.5 every ramp row and the floor-division table (and that Kotlin `/` gives the wrong column); V§3 every zone row and the target zones.
- V§4: all 12 accept cases and all 39 reject cases with the exact code and path, taken from the reference's own case list.
- V§5: every example message fragments and reassembles at 19 and 243 bytes; the 233-byte PUT_DATA's 13 packet headers and its first, second and last packets byte for byte; a 4000-byte chunk is 17 packets; the BAD_CRC CRC.
- V§8: the synthetic profile gives exactly `start 490, length 1060, ascent 48, avg 45, max 60`, and the three no-climb profiles give none. V§9: the route object byte for byte (157 bytes, ID, CRC, distance 2257, ascent 17) and the varint and zigzag checks.
- The six starter workouts encode to the same bytes as `ref.py` (the C fixtures). Typography is normalised and nothing else (an accented name is refused, not corrected).
- Route pipeline on the gen_ride GPX: 49 points, 25 m profile, one climb (about 950 m to 1675 m and 30 m; the smoothing widens it), turns depart, left at 1000 m, right at 1600 m, arrive; the GPX's own elevations are used when the DEM returns nothing. Turns: a corner drawn with three points is one LEFT, a 20 degree bend is no turn, a hairpin is a U-turn (Q25).
- Shared protocol suite, 7 tests run against both FakeUnit and simunit (the real C core over TCP), at MTU 23 (512-byte chunks) and 247 (4000-byte chunks): HELLO, FTP set and bounds, TIME_SET, workouts and a route sent, EXISTS, listed, deleted; PUT errors (NO_SESSION, TOO_BIG, BAD_ARG, BAD_CRC, OFFSET with resume, BAD_OBJECT); unknown opcodes; ride list, fetch with CRC check, mark, delete; BUSY while recording; the connect sequence. Both units give the same answers.
- Intervals.icu client against MockWebServer: success (basic auth user `API_KEY`, multipart `file`), duplicate within 60 s not sent (61 s is sent), 401, 429, 5xx, no connection, and another 4xx failing with the server's message. Elevation providers against MockWebServer: GA `identify` request and `Pixel Value`, NoData, the cache; Open Topo Data batches of 100 with a one-second pause; the chain filling gaps and skipping a failing provider.

**Not done, and why.** The elevation spike: this environment's network policy refused `services.ga.gov.au` and `api.opentopodata.org`, so `docs/DEM-FINDINGS.md` says so and nothing is invented; `tools/dem_spike/dem_spike.py` runs it on any PC with internet access. `fitcheck` and `gen_ride` were done in WP2 and WP3.

**Deviations and choices.** Q25 (turn angles summed per vertex). The Intervals.icu duplicate check uses each activity's `start_date` from `GET /api/v1/athlete/0/activities?oldest=&newest=`; the field name follows the Intervals.icu API documentation and is Unverified until a live call. The Intervals.icu client lives in `:core` (it needs only OkHttp) so it can be tested on the JVM; the app wires it to WorkManager and Tink in WP6.

**Unverified.** The GA and Open Topo Data response shapes and limits against the live services; the Intervals.icu responses against the live service; anything on the phone (WP6).

## WP6 Android app · 3 November 2026 · app code written, not built here

**What exists.** A new pure JVM module, `android/sync` (Q27), holds the app's behaviour: `UnitSession` (scan, pair, connect, the connect sequence and mirror from protocol 5, FTP, send, delete, ride list, fetch and mark, events while idle; a refusal from the unit keeps the link, a timeout or drop sets "not connected" with the reason), the `Connector` interface with `SimulatedConnector` (simunit over `SocketTransport`, passkey simulated, Q28), `RideSync` (fetch new rides into local storage, the upload queue with states Needs date, Waiting, Uploaded, Failed and Needs key, RIDE_MARK after a 2xx or a duplicate, marks owed from uploads done while the unit was away sent on the next connect), the FIT time shift for rides flagged "time unknown" (Q26), workout and route import with readable errors and the preview data, and the store interfaces with memory versions. `:core` gained `LinkClient.pollEvents`.

`android/app` is the Android app (brief 7.1, 7.2): Compose screens Unit, Workouts, Routes, Rides, Profile and Settings in the spec 12 dark tokens; the Nordic BLE library transport (service and characteristics from protocol 2.1, MTU 247, notifications on TX, writes without response, bonding through Android's passkey dialog); Room (rides, the phone's workout and route library, the elevation cache by 1e-4 degree cell); DataStore (thresholds, elevation source, connection mode, the paired unit); Tink AEAD under an Android Keystore key for the Intervals.icu key, which is never shown, logged or written elsewhere; a WorkManager upload job (network required, exponential backoff from 30 s on 429, 5xx or no connection); share target and file picker for workouts and GPX files; and a Settings switch between Bluetooth and the simulated unit, with an overridable Intervals.icu server for testing against a mock. `android/app/src/androidTest/.../ImportErrorsTest.kt` is the instrumented UI test of the import errors (all 39 V§4 reject rows through the real Compose pane, normalisation accepted, nothing shortened), generated by `tools/android/gen_import_cases.py`. `tools/android/demo_unit.sh` prepares and serves a simulated unit with a normal ride and a "time unknown" ride; `tools/android/mock_intervals.py` is a mock Intervals.icu that keeps uploads and answers the duplicate check.

**How to run.** `make android` runs the JVM tests of `:core`, `:testing` and `:sync` (no Android SDK needed). `:app` is included automatically when an Android SDK is configured (`android/local.properties` with `sdk.dir`, or `ANDROID_HOME`): open `android/` in Android Studio, or `./gradlew :app:assembleDebug` and `./gradlew :app:connectedDebugAndroidTest` with an emulator running.

**Verified (JVM, 19 new tests in `:sync`; 60 in all with `:core` and `:testing`).**

- The WP6 done-condition flow end to end, through the same classes the screens call, against the real C core in simunit over `SocketTransport`: a wrong passkey refused and nothing stored, the right one pairs and stores the unit; FTP read on connect (200) and set (250); W1 and W2 imported, previewed in watts at FTP, sent, seen in the unit's list with their duration and step count, a duplicate send answered EXISTS, one deleted; the gen_ride GPX imported with elevations from a mock Open Topo Data, one climb found and carried into the CR1 object, sent, listed with its climb count; two rides listed, the normal one fetched and uploaded to a mock Intervals.icu (valid FIT, basic auth, start time from the file) and marked uploaded on the unit; the "time unknown" ride held until a date is entered, then shifted, uploaded with a valid FIT starting at the entered time, and marked; Forget unit.
- The upload queue against FakeUnit and MockWebServer: duplicate found and not sent, 401 stops the pass and holds every waiting ride until a new key, no key asks for one without a request, 503 and 429 stay waiting with the attempt counted, 422 fails with the server's message and Retry requeues it, an upload while the unit is away is marked on the next connect, the corrected day is used for the duplicate query, BUSY while recording keeps the link, an unreachable unit reports why.
- The FIT shift on a simunit ride recorded without UTC: every time field moved by the same offset, nothing else changed, both CRCs valid, and `tools/fitcheck` accepts the result. FIT CRC-16 check value.
- Import: every V§4 reject row gives its code and readable message through `importWorkout`, step numbering in messages, curly quotes and a non-breaking space normalised, identical workouts give the same ID.
- `tools/android/mock_intervals.py` against curl with a simunit ride: upload saved, duplicate listing returned, the saved file passes `fitcheck`.

**Not done, and why.** `:app` has not been compiled or run: this container has no Android SDK, and `dl.google.com` (the SDK and Google's Maven repository, which serves the Android Gradle plugin and every AndroidX library) is refused by its network policy. So the app on an emulator or phone, the instrumented tests, and the BLE transport are all Unverified. Expect a first build to need small fixes; the JVM modules it sits on are tested. The elevation spike is still not run (WP5).

**Deviations and choices.** Q26 (date and time for a "time unknown" ride), Q27 (the `:sync` module), Q28 (pairing simulated in the app's connector). Room allows main-thread reads (small tables; writes are on background threads). The app keeps fetched FIT files until they are uploaded so the upload does not need the unit. Forget unit drops the phone's bond only; the unit's own bond is removed on the unit, as the confirmation says.

**For Daniel: run the WP6 flow on an emulator (one step at a time).**

1. In Android Studio, open `android/`, let it create `local.properties`, build and install `app` on an emulator (API 29 or later).
2. On the PC: `make test-c-plain`, then `tools/android/demo_unit.sh` (serves on port 7700) and, in another terminal, `python3 tools/android/mock_intervals.py`.
3. In the app: Settings, Connection, Simulated unit; host `10.0.2.2`, port `7700`; Intervals.icu server `http://10.0.2.2:8800`, Apply; save any text as the API key (the mock accepts anything).
4. Unit, Add unit, select CC700-SIM, passkey 123456. Then Profile (FTP), Workouts (paste W1 from `android/sync/src/test/resources/w1.json`, Validate, Save, Send), Routes (Import GPX: copy `core/tests/fixtures/route.gpx` to the emulator), Rides (Sync now; set the date of the "Date unknown" ride).
5. `./gradlew :app:connectedDebugAndroidTest` runs the instrumented import-error tests.

**Checklist for the first live upload (Daniel, with his regenerated key).** Switch Connection back to Bluetooth and the server to the real one (Real server), save the key in Settings, pair the unit, record a short ride, Sync now, and check on intervals.icu that the activity appears once with the right start time; then Sync now again and check it is not uploaded twice. Report the response if anything fails; the duplicate check's `start_date` field is the part most likely to differ.

## WP7 Firmware skeleton and bring-up plan · 3 November 2026 · stopped for Daniel's review

**What exists.**
- `fw/` is an ESP-IDF project for the ESP32-S3, pinned to v5.5.6 in `fw/IDF_VERSION`:
  - `partitions.csv` is the stock layout, never to be flashed;
  - `sdkconfig.defaults` holds the hardware facts from the handover, with the Unverified ones marked;
  - `components/cc700` builds the shared `core/`, `ui/` screens, fonts and icons, with LVGL 9.6.0 from the component manager.
- `fw/main` has:
  - the core task (core, link server and LVGL in one task);
  - `link_gatt.c`, the phone link on NimBLE: the protocol 2.1 service and characteristics, LE Secure Connections with MITM, DisplayOnly passkey, one bond, pairing only inside the 60 s window, advertising as `CC700-xxxx` with the UUID in the scan response, RX queued to the core task, TX notifications with retry when buffers are full;
  - `sensors.c`, the sensor manager (BLE central: Cycling Power and Heart Rate discovery, notifications, the Feature read and the calibration write, everything queued to the core task);
  - `hal_esp.c` (a real clock; every hardware driver is a stub that logs "not implemented" once);
  - `fs_null.c` (storage that refuses everything until the separate storage is known);
  - `ble_parse.c` (the measurement parsers).
- `fw/probe` is the RAM-only, read-only GPIO probe for bring-up step 3.
- `make fw-build` (`tools/fw_build.sh`) checks the IDF version, builds `app.bin` (application only) and the probe, fails on any compiler warning or if `app.bin` exceeds 8,192,000 bytes, and has no flash or monitor step.
- `docs/BRINGUP.md` is the seven-step plan from brief section 9.
- `tools/fw/backup_report.py` produces an offline, shareable report on the flash backup. `tools/fw/otadata.py` extracts, inspects and switches `otadata` offline, computed as ESP-IDF's `otatool.py` does.
- The core gained `calib.c` (brief 5.11), the `cc_calibration_*` states the Power meter screen shows.

**Checked against sources (no device involved).**
- **Calibration (5.11).** The recalled values are right:
  - Cycling Power Feature bit 9 is "Offset Compensation Supported";
  - bit 8 is a different flag, the "Offset Compensation Indicator";
  - the Control Point opcode for Start Offset Compensation is 0x0C, the Response Code opcode is 0x20, and Success is 0x01.

  Source: BTstack's Cycling Power Service server (`src/ble/gatt-service/cycling_power_service_server.c` and `.h`, cloned from bluekitchen/btstack), whose enums list these in specification order. The Bluetooth SIG specification text itself (CPS 1.1, with Errata 23224) could not be opened from here, so this is a check against an independent implementation, not the specification document.
- **RAM-only probe.** ESP-IDF v5.5.6 offers `CONFIG_APP_BUILD_TYPE_RAM` (marked experimental) and `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP`. Its `tools/test_apps/system/ram_loadable_app` lists the ESP32-S3 as a supported target and runs CI on it, loading with `esptool --no-stub load_ram`. So the pinned IDF does support this build type on the ESP32-S3, and `fw/probe` uses it with no flash support compiled in.
- **Partition table.** `fw/partitions.csv` passes ESP-IDF v5.5.6's `gen_esp32part.py` for a 16 MB flash and equals the handover's manifest entry for entry (pytest).
- **Stock IDF version.** `423e6536` is not in any public ESP-IDF branch or tag (searched all 117,349 commits), so the stock firmware was built from a private or vendor commit (Q31).

**Verified (host).**
- `test_calib` (C): support needs both bit 9 and the Control Point, and bit 8 alone is not support; success only on 0x20, 0x0C, 0x01; failure on any other value, on a 10 s timeout, on a late answer, on meter loss mid-run and on a failed write; the offset is kept for logging.
- `test_fw_parse` (C): the firmware's Cycling Power Measurement parser with balance, torque, wheel and crank fields and truncation; Heart Rate 8- and 16-bit; the Feature parser; the core running on `fs_null` with defaults.
- `test_fw_partitions.py`: the layout; USB Serial/JTAG kept; no TinyUSB, `nvs_flash_init`, partition or OTA writes in `fw/`; the probe RAM-only.
- `test_otadata.py`: CRC as the bootloader computes it; switching from erased, from `ota_0`, with higher sequence numbers and with invalid or aborted copies.
- `test_backup_report.py`: the report on a synthetic image, which leaks no free text.

`make test` passes: 16 C tests plain and 14 under ASan, plus 20 Python tests.

**Not done, and why.** `make fw-build` has not run. ESP-IDF's source is reachable here over git, but its compilers come from `dl.espressif.com` and GitHub release downloads, and both are refused by this container's network policy. So these are all Unverified:
- the IDF build itself;
- the "no warnings" check;
- `app.bin`'s size;
- the Kconfig names for LVGL and NimBLE in `sdkconfig.defaults`.

Expect the first build on a PC with ESP-IDF v5.5.6 to need small fixes. The firmware's IDF-free parts (`ble_parse.c`, `fs_null.c`) and the shared `core/` and `ui/` code are tested on the host.

**Deviations and choices.**
- Q29: stock data partitions are untouched, so bonds are kept in RAM for now.
- Q30: octal PSRAM is assumed.
- Q31: the bootloader's version comes from the backup before step 4.
- The firmware draws only the Ready screen from the core's view. The screen manager stays in `ui/sim/app.c` until bring-up step 7, when buttons and touch exist.
- The sensor manager writes the CCCD at value handle + 1, the common layout, instead of discovering descriptors; proper discovery is a step 6 item.
- `BRINGUP.md` adds offline tools (`backup_report.py`, `otadata.py`) so that each flash write in step 4 is one `write-flash` to a named address, with a file prepared and checked beforehand.

**Unverified.** Everything in `fw/` on hardware; the stock bootloader's acceptance of an IDF v5.5.6 app; the PSRAM mode; the calibration constants against the SIG text, and whether the PM500 supports calibration at all.

## Future enhancements (not scheduled)

- **Shimano 12-speed Di2 gear display (105 R7100).** Requested by Daniel on 3 November 2026, deferred. No public specification exists for the Di2 gear data over BLE; the only open work decodes Shimano's private ANT profile, and the unit has no identified ANT+ radio. First step when picked up: a BLE capture with nRF Connect while shifting, to see whether the gear is readable. Then: gear in the ride data, FIT front and rear gear-change events, and a gear display (needs a mockup change; the Main grid is full).
