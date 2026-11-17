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

**Not done here, and why.** `:app` was not compiled or run in this container: it has no Android SDK, and `dl.google.com` (the SDK and Google's Maven repository, which serves the Android Gradle plugin and every AndroidX library) is refused by its network policy. **Since built and run on Daniel's PC — see "WP6 verified on hardware-free Windows" below.** The BLE transport remains Unverified (it needs a phone and the unit).

**Deviations and choices.** Q26 (date and time for a "time unknown" ride), Q27 (the `:sync` module), Q28 (pairing simulated in the app's connector). Room allows main-thread reads (small tables; writes are on background threads). The app keeps fetched FIT files until they are uploaded so the upload does not need the unit. Forget unit drops the phone's bond only; the unit's own bond is removed on the unit, as the confirmation says.

**For Daniel: run the WP6 flow on an emulator (one step at a time).**

1. In Android Studio, open `android/`, let it create `local.properties`, build and install `app` on an emulator. **Use API 35** and create the AVD with **`hw.keyboard = yes`**. Both matter: on an API 37 image every instrumented test dies in Espresso with `NoSuchMethodException: android.hardware.input.InputManager.getInstance` (Espresso reflects on a method recent Android levels no longer expose), and with `hw.keyboard = no` the soft keyboard makes `ImportErrorsTest.everyInvalidRowShowsItsMessage` fail on its second case (`assertIsDisplayed` rejects the error row even though it renders on screen above the keyboard).
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

**Not done, and why.** `make fw-build` has not run. ESP-IDF's source is reachable here over git, but its compilers come from `dl.espressif.com` and GitHub release downloads, and both are refused by this container's network policy. So these were all Unverified at the time. **All four have since been verified on Daniel's PC — see "The firmware builds" below.**
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

## First run on Daniel's PC (Windows) · 3 November 2026

The first time any of this was built or run outside the cloud container. Windows 11, no hardware involved: the unit was never connected, opened or addressed. Everything below is a host or emulator result.

**Toolchain installed, all under `D:\cc700-toolchain\`, nothing on PATH and nothing in the registry.** Temurin JDK 17.0.20.1 (the project needs 17; Android Studio's bundled JBR is 25, which Gradle 8.14.3 refuses). Android SDK with platform 35 and build-tools 35.0.0, `emulator` and the API-37 system image junctioned from the existing `%LOCALAPPDATA%` SDK rather than copied. CMake 4.4.5, Ninja 1.13.2, MinGW-w64 GCC 16.2.0 and Clang 23.1.3. A Python venv with pytest 9.2.0 and fitdecode (both already listed in `docs/THIRD_PARTY.md`). `android/local.properties` points at that SDK and stays gitignored. To undo all of it: delete the folder and `android/local.properties`.

**Verified.**

- **`make vectors`:** `tools/reference/vectors.py` reproduces `claude/cc700-test-vectors.md` byte for byte (`diff` clean, 20272 chars).
- **C core, plain build (GCC 16.2.0, `-Wall -Wextra -Werror -Wshadow -Wstrict-prototypes`):** configures and builds with no warnings, **14 of 14 ctest tests pass** in 54.8 s — `test_alerts`, `test_anchors`, `test_calib`, `test_fit`, `test_fuzz_cw1`, `test_fw_parse`, `test_link`, `test_nav`, `test_ride`, `test_sensors`, `test_settings`, `test_starter_workouts`, `test_workout`, `test_zones`. This is the first time the C core has been compiled by anything other than the container's compiler, and a two-major-version-newer GCC at that; it needed no change.
- **Python tools: 17 passed, 3 skipped.** Was 14 passed, 6 skipped until a portability fix: `tools/fitcheck/test_fit_recovery.py` looked for `build/host/core/test_fit` and `libcc_testshim.so`, which on Windows are `test_fit.exe` and `libcc_testshim.dll`, so three real tests skipped while claiming the build was missing. It now takes whichever name CMake produced. The 3 remaining skips are simunit (Q33).
- **`:app` builds.** `:app:assembleDebug` and `:app:assembleDebugAndroidTest` both succeed — Kotlin, KSP/Room and Compose. WP6 said "expect a first build to need small fixes"; it needed **none**.
- **Instrumented tests: 3 of 3 pass** (`ImportErrorsTest`) on an API 35 `google_apis` emulator — all 39 V§4 reject rows through the real Compose pane, normalisation accepted, nothing shortened. See the two AVD requirements in the WP6 checklist above; both cost a full diagnostic cycle to find.
- **JVM modules unchanged at 51 passed, 9 skipped** (60 total). The 9 skips are simunit (Q33).
- **The WP5 elevation spike, at last** (`docs/DEM-FINDINGS.md`). Both services reachable from Perth. It found E8 (the GA attribute is `Stretch.Pixel Value`, so `GaProvider` would have returned null for every point and fallen silently through to Open Topo Data) and Q32 (the 5 m LiDAR returns `NoData` over the whole Perth Hills, so hill climbs depend on the 30 m fallback). `GaProvider` now accepts either attribute name and `HttpTest.gaIdentifyLiveAttributeName` pins the live shape; `HttpTest` is 10 tests, all passing. The rate test confirms 4 concurrent requests at 8.0 per second is safe, so brief 7.4's limit needs no change.

**Still Unverified, and what each needs.**

- **ASan and UBSan did not run.** MinGW-w64 ships no sanitizer runtime (`libclang_rt.asan_dynamic.dll.a` is absent for both GCC and Clang), so `-DCC_SANITIZE=ON` configures but cannot link. This is a Windows toolchain limit, not a code problem; the 14 tests pass clean without it. Needs Linux, macOS or WSL.
- **`simunit` does not build on Windows** (`<arpa/inet.h>`, `<sys/statvfs.h>`), so the 3 Python and 9 JVM simunit tests and the whole WP6 manual flow (steps 2 to 4) could not run. Q33. WSL is installed on the PC but has no distribution.
- **The 2 UI C tests** were not built: `ui/` needs LVGL from `ui/deps.sh`, which also wants node and npm, neither installed. `make test` counts 16 C tests; 14 ran.
- **The BLE transport, and everything in `fw/`.** Unchanged — they need a phone and the unit.
- **The live Intervals.icu upload.** Unchanged; still Daniel's, with his own regenerated key.

## The firmware builds · 4 November 2026

`make fw-build` had never run anywhere. It now passes on Daniel's PC, under WSL 2 (Ubuntu) with
ESP-IDF v5.5.6 installed at `/opt/esp/esp-idf`. **Nothing was flashed and no serial port was opened**
(START-HERE rule 1): the target is `idf.py app`, which builds the application only.

**Result.** `app.bin` is **1,146,640 bytes of the 8,192,000-byte `ota_1` limit (13 %)** — 1,146,736 after the Q19 fix below, and the probe
RAM image is 106,768 bytes. No compiler warning anywhere, which is what brief WP7 asks for. The
memory report:

| Memory | Used | of | Note |
| --- | --- | --- | --- |
| DIRAM | 188,863 (55 %) | 341,760 | `.bss` 101,144, `.text` 70,651, `.data` 17,068; 152,897 free |
| IRAM | 16,384 (100 %) | 16,384 | `.text` 15,356 plus the 1,028-byte vector table |
| Flash code | 681,232 | | `.text` |
| Flash data | 361,152 | | `.rodata` 360,896 (the fonts) |

So all four items the WP7 section listed as Unverified for want of a toolchain are now verified: the
IDF build, the no-warnings check, `app.bin`'s size, and the Kconfig names. On the last of those, the
37 names in `fw/sdkconfig.defaults` are all real and all take effect — worth stating plainly because
ESP-IDF ignores an unknown `CONFIG_` name in a defaults file **silently**, so a typo there would have
looked exactly like success.

**Two real bug classes had to be fixed first, and neither is visible on a host.**

1. **`%u` for `uint32_t`, in 7 files.** `uint32_t` is `unsigned int` on x86-64 but
   `long unsigned int` on xtensa, so every `printf("%u", some_uint32)` compiles clean in all 16 host C
   tests and then fails the firmware's `-Werror=format` gate. They were in `core/src/alerts.c`,
   `core.c`, `nav.c` and `store.c` and in `ui/src/ui_kit.c`, `screens_ride.c` and `screens_menus.c` —
   that is most of the formatting code in the project. All now use `PRIu32` from `<inttypes.h>`.
   Eight lines were fixed in the first pass, from the build log; `make fw-check` then found the
   other 18 in one go (30 compiler errors, since the warning counts each argument). 26 lines across
   those seven files use `PRIu32` today.
   A `uint16_t` or `uint8_t` argument is *not* affected (it promotes to `int`, which GCC accepts for
   `%u`), so only the 32-bit cases changed.
2. **An include guard that was also a constant.** `hal/hal_display.h` opened with `#ifndef
   HAL_DISPLAY_H` / `#define HAL_DISPLAY_H` and then defined `HAL_DISPLAY_H 320` as the panel height.
   The guard worked by accident, but redefining it is an error under `-Werror`. The guard is now
   `CC700_HAL_DISPLAY_H`; `HAL_DISPLAY_W` and `HAL_DISPLAY_H` are unchanged, so `main.c` is unchanged.

**Then the link failed, and that was a design fault, not a typo.** `.dram0.bss` overflowed
`dram0_0_seg` by **529,944 bytes**. The cause: `core/src/core.c` held a whole decoded route
(`cc_route_t`, 208,800 bytes — 8,000 points × five 4-byte arrays, plus a 12,000-sample profile and
500 turns) and a 131,072-byte object read buffer as static `.bss`, and `core/src/link.c` held another
of each for checking a received object. About 740 KB of static buffers for a chip with 512 KB of
internal SRAM, of which 341,760 bytes are addressable as DIRAM.

Brief 5.7 already says to decode CR1 "into arrays in **PSRAM**", so this was the code not matching the
design rather than a new decision. All four are now a one-time `calloc`, which on the unit lands in
PSRAM because ESP-IDF's heap sends any allocation larger than `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`
(16 KB) there, and which is an ordinary heap allocation on the host. That is also why `EXT_RAM_BSS_ATTR`
was *not* used: Q30 configures octal PSRAM on an Unverified guess with `CONFIG_SPIRAM_IGNORE_NOTFOUND=y`
so that a wrong guess still boots. With `.bss` in external RAM a wrong guess would take the whole
firmware down; with the allocations, it costs routes and workouts and nothing else, through the
object-load failure path the UI already has. The whole host suite is unchanged by the refactor:
16 of 16 C tests plain, 14 of 14 under ASan and UBSan, 20 of 20 Python, and the three JVM modules green.

**`make fw-check` is new** (`tools/fw_check.py`, `IDEAS.md` I7). It re-compiles only the project's own
25 sources — core/, ui/, hal/, fw/main and the probe's main — with the xtensa compiler and
`-fsyntax-only`, reusing the exact flags the real build recorded in the two
`compile_commands.json` files. It reports every target-dependent warning in the whole
project in about ten seconds. This matters more than it sounds: a full `fw-build` takes roughly forty
minutes and stops at the first failing file, so the `%u` class above surfaced a few files at a time and
cost three rebuild cycles before the fast check was written and found the remaining 30 at once.
It needs a configured `build/fw` and never links, so it cannot catch a section overflow (`IDEAS.md` I11).

**Still Unverified after this.** Everything about running the firmware: the stock bootloader accepting
an IDF v5.5.6 app, the PSRAM mode (Q30), the display, touch, GNSS, barometer, battery and the separate
storage, the real BLE behaviour, and whether 152,897 free DIRAM bytes are enough once the task stacks,
LVGL buffers and NimBLE's runtime allocations are all live. A build that links says nothing about any
of that. The bring-up plan in `docs/BRINGUP.md` is still waiting for Daniel's review, and no step of it
has been attempted.

## Open questions closed, and an ascent the file should not have claimed · 4 November 2026

Nine of the open questions are now answered in `docs/QUESTIONS.md`: **Q14, Q15, Q17, Q19, Q25, Q26,
Q27, Q28 and Q29**, with research notes added to **Q21, Q22, Q23 and Q24** so the four that are
genuinely Daniel's to decide can be decided in one pass. One new question, **Q35**, came out of the
work below. Each answer records the evidence rather than
restating the default; two of them found or proved something new.

**Q19 found a real fault.** The question was only "what altitude goes in the file without a
barometer", and the answer — the GNSS altitude — stands. But writing it set `has_alt`, which made the
FIT accumulator treat the ride as having usable altitude, so **every lap's and the session's
`total_ascent` was computed from GNSS altitude**, and the `.meta` summary and the Summary screen's
ASCENT tile took the same number. Three things wrong with that: brief 5.3 defines ascent from the
smoothed *barometer* alone; the ride screen already shows `--` for ascent without a barometer, so the
unit was printing on one screen a number it refused to print on another; and GNSS vertical noise of
several metres through a 1 m threshold inflates the total badly. Intervals.icu with elevation
correction off takes the file's figure as given, so it would have been believed.

Fixed by separating "there is an altitude" from "an ascent may be claimed": `cc_fit_record_t.alt_baro`,
set only on the barometric branch; `cc_fit_acc_t.ascent_claim`, set only by such a record or by the
ride engine's explicit barometric override; lap and session now write `total_ascent` as **FIT-invalid
(0xFFFF), not 0** without a claim — a 0 would assert "this ride was flat", a different false claim.
`cc_fit_summary_t.has_ascent` carries it to the `.meta` flags byte's spare bit 3 (an older `.meta`
reads as "no ascent", the safe answer, so the format version stays 1) and the Summary screen shows
`--`, the idiom it already uses for power and heart rate. Nothing in the protocol or the app changes:
RIDE_LIST never carried ascent, and every ascent in the Android app is a *route* ascent the phone
computed itself. Two new tests: one in `test_fit.c` for the claim, and one in
`tools/fitcheck/test_fit_recovery.py` that decodes the file with fitdecode and asserts
`session.total_ascent is None` while all 300 record altitudes are present.
One consequence, stated in the answer: a **recovered** ride claims no ascent either, because recovery
rebuilds the totals from the records and a FIT record does not say where its altitude came from.

**Q29 is now proved from the binary, not the source.** `tools/tests/test_fw_partitions.py` checks that
`fw/` contains no `nvs_flash_init` and no partition or OTA write. With the firmware finally linking,
`nm` over `build/fw/cc700.elf` says something much stronger about all 8,321 defined symbols: there is
**no flash write or erase entry point of any kind** in the image — no `esp_flash_write`,
`esp_flash_erase`, `spi_flash_write`, `spi_flash_erase`, `esp_partition_write`, `esp_partition_erase`,
`esp_efuse_write` or `esp_efuse_burn` — and no NVS API at all. The custom app is physically unable to
modify the stock firmware's `nvs`, `phy_init`, `coredump`, `otadata` or `ota_0`, however it is driven,
because the code to do it was never linked in. The only OTA symbol present is
`esp_ota_get_running_partition`, a read. That also independently confirms
`CONFIG_BT_NIMBLE_NVS_PERSIST=n` took effect.

**The WP6 done-condition flow is confirmed to have run**, which the Windows-only run could not do.
`:sync`'s `SimunitAppFlowTest.fullFlow` reaches simunit through `Sim.need()`, an `assumeTrue` that
marks the test *skipped* when the binary is missing — which is what happened on Windows. Under WSL the
Gradle XML reports `:core` 31, `:testing` 14 and `:sync` 19 tests with **0 skipped and 0 failures**, so
the whole flow really did drive the C core over a socket. The manual emulator walkthrough in the WP6
checklist (steps 1 to 5) is still Daniel's and has not been done.

**Two firmware build gates added**, both paying for time lost tonight. `tools/fw_build.sh` now refuses to start
when an ESP-IDF submodule is registered but not checked out, naming each one and the command to fix it
(`IDEAS.md` I3 — that failure cost two rebuild cycles and surfaced as a CMake error about an include
directory), and it runs `fw_check.py` first whenever a compile database exists, so a target-dependent
warning costs ten seconds instead of forty minutes (I7, I11).

A third gate closes a stale-green. `make android` ran the JVM suites through Gradle's up-to-date
checks, and the simunit-backed tests execute a **C binary Gradle does not know is an input** — so after
tonight's changes to `core/` the test tasks were skipped as up to date and the target reported green
from results that predated the change. Caught by looking at the timestamps on the JUnit XML, not by
anything failing. The target now runs `cleanTest` first; all 65 tests re-run in about 35 s, and the
re-run confirms 65 passed (:core 32, :testing 14, :sync 19), 0 skipped, 0 failures against the new core.

One local finding while writing the submodule gate, worth knowing before trusting this PC's ESP-IDF
clone: `git submodule status --recursive` exits 128 there because
`components/mqtt/esp-mqtt`'s index is damaged ("index file smaller than expected"), almost certainly
from the disk-full event during installation. Nothing in this firmware uses mqtt, so the build is
unaffected, but it is why the gate reports what it could not inspect instead of treating an unreadable
list as an all-clear — the same false-all-clear that `git submodule foreach` gave earlier.

### The same fault on the phone: a route with no elevation (Q35)

Looking for more of the shape Q19 had — a confident value standing in for a missing one — turned one up
in the Android app's route import, which is the least reality-tested path in the project and the one
the DEM spike already found E8 in.

A route can end up with no elevation at all: the GPX has no `<ele>` and neither elevation service
answers, which on a phone means nothing more exotic than importing a file with no signal.
`buildRoute` then produces a CR1 with `nprof = 0`, `ascent_m = 0` and no climbs — and both the app and
the unit showed that as **"0 m up, 0 climbs"**, which says the route is flat. Worse, the import preview
said **"Elevation: from file"**, because the flag was set when the code *fell back* to the file rather
than when the file actually supplied anything: it named a source that had given nothing.

That is not cosmetic. The Climb page, the "climb ahead" alert and `ASCENT LEFT` all come from the
climb list, so a route with no elevation behaves exactly like a route with no climbs and the rider
cannot tell which they are looking at.

No format change was needed: `nprof == 0` already distinguishes "unknown" from "flat" in the object
(protocol 3.3), so only the screens had to ask. `BuiltRoute.hasElevation` and `cr1HasProfile(cr1)` —
two bytes of the CR1 header, cheap enough for a list row — now drive the import preview ("elevation
unknown", with a warning-coloured line saying why), the phone's library row, the unit's Routes list
("no elevation") and the route detail (`--` for ASCENT and CLIMBS). `fromFile` is set only when the
file really supplied elevations. `RouteTest.noElevationAnywhereIsReportedAsUnknownNotFlat` pins both
halves, including that a genuinely flat route — elevation known and constant — still reads as known
with an ascent of 0. Logged as **Q35**, open, because the default changes what two screens say and no
mockup covers the state.

### `make shots` was broken on this PC, and the contact sheet you want is the one already committed

A correction to something I nearly claimed. The WP4 contact sheet **already exists and always did**:
`docs/wp4/contact-sheet.png`, committed on 3 November, is the proper two-column sheet — each pair is
the mockup on the left and the LVGL simulator on the right, 48 scenarios, 2988 × 2798. That is the
artefact waiting for your approval, and nothing tonight improves on it.

What tonight actually found is that **`make shots` cannot regenerate it on this PC**, for two reasons
that would stop anyone else too:

- **A space in the path broke the recipe.** `CC700_DESIGN_ROOT=$(CURDIR)/ui/out/design` was unquoted,
  so with the checkout at `C:\Users\danie\Desktop\Bike Project` the shell split the path and reported
  `/bin/sh: 1: Project/ui/out/design: not found`. Every `$(CURDIR)` in the recipe is now quoted. This
  would have bitten any checkout whose path has a space in it, which is most of them on Windows.
- **Pillow was an undeclared dependency.** `tools/design/contact_sheet.py` imports PIL, but
  `docs/THIRD_PARTY.md` did not list Pillow, so a machine set up by following the documented steps
  could not run a committed tool. Installed into the project venv and now declared (MIT-CMU, so the
  no-GPL rule in START-HERE still holds).

With those fixed, `make shots` runs here and writes `ui/out/contact-sheet.png` — but **single-column,
LVGL only**, because `render_boards.js` needs Playwright and a Chromium that this machine does not
have, so there is no mockup to put beside each screenshot. Review `docs/wp4/contact-sheet.png`, not
that one. To make the regenerated sheet equal to the committed one:
`npm i -g playwright && npx playwright install --with-deps chromium` in WSL, then `make shots`.
That is a few hundred megabytes of apt packages on your machine, which is why it was left for you.

The 48 LVGL screenshots themselves did regenerate cleanly from tonight's code, which is a useful check
in its own right: the UI changes for Q35 did not disturb any existing screen.

## Future enhancements (not scheduled)

- **Shimano 12-speed Di2 gear display (105 R7100).** Requested by Daniel on 3 November 2026, deferred.
  **Corrected 3 November 2026 (Q34).** This entry previously said the unit has no identified ANT+ radio and that the first step was a BLE capture with nRF Connect. Both are wrong. Reading the flash backup offline (`docs/STOCK-FIRMWARE-FINDINGS.md`) shows the stock firmware implements seven ANT+ profiles under `Modules/Middlewares/MidAntDeviceManage/`, including `ant_di2/ant_di2.c` and `ant_shft/ant_shft.c`, and that an nRF co-processor sits on a UART (`mSysTimeStampFromNrf`, two configured UARTs, a `MidComm` layer). The stock Di2 module already reads manufacturer and model, gear index, front and rear tooth counts, Di2 battery status and shift mode.
  So the gear data arrives over **ANT, through the nRF**, not over BLE, and it is demonstrably readable on this hardware. The first step when picked up is therefore to learn what `MidComm` speaks over that UART — cheapest during bring-up step 3, which already probes GPIO. Then: gear in the ride data, FIT front and rear gear-change events, and a gear display (needs a mockup change; the Main grid is full). Still out of scope for v1 under START-HERE rule 4.

## Bring-up step 3c · 4 November 2026 · touch decoded and implemented

The display is finished and confirmed on the unit (colour bars in the right order with both the byte
swap and inversion in). The touch controller is now fully decoded from the stock firmware and
implemented: a Hynitron **CST3240 at I2C address 0x5A**, 400 kHz, register 0xD000 read seven bytes,
with the coordinate arithmetic and the no-transform behaviour lifted from the vendor's own LVGL
callback. `hal/hal_touch.h` and `fw/main/hal_touch_esp.c` are new; `main.c` registers an LVGL pointer
device and, while bringing up, draws a dot under the finger. See `docs/BRINGUP-LOG.md` for the
provenance of every byte and **Q37** for the two deliberate differences from the vendor.

**A finding that redirects the sensor work, logged as Q36.** The barometer, light sensor, battery
gauge and IMU are **not on I2C**. Every I2C transfer in the vendor application goes through two
wrappers, those wrappers are the only references to `i2c_master_transmit`/`transmit_receive` anywhere
in the image, and all sixteen of their call sites are inside the touch driver. The sensors sit behind
the nRF co-processor on UART1 with a framed protocol of the vendor's own — which is the same link the
deferred Di2 gear display needs. So `hal_baro` and `hal_power` are not I2C drivers waiting to be
written; they are one protocol decode, and that is the next piece of work. The boot-time I2C scan
stays in so the claim is testable on the next flash rather than merely asserted.

## Bring-up step 4 · 4 November 2026 · the nRF link decoded, listener ready to flash

Touch is **confirmed on hardware**: the bring-up dot follows a finger, which in one test validates the
I2C bus on SDA 21 / SCL 12, the CST3240 at 0x5A, the 0xD000 seven-byte report, the coordinate
arithmetic and the vendor's odd doubled-register writes. No axis transform is needed.

The nRF link is decoded from the stock firmware and written up in **`docs/NRF-PROTOCOL.md`**: `0xA5`
sync, length byte equal to payload + 4, `0x6F`, a direction byte (`0xF1` outbound), kind 1–5, message
id, payload, and a little-endian **CRC-16/XMODEM** over everything before it. The encoder at
`0x4205062c` and the decoder at `0x42050766` agree independently, which is the strongest internal
check available without hardware. The dispatcher acts on ids 0, 1, 11, 16, 17, 34, 35, 40, 120–123 and
128; id 16 is the power and slave-mode channel, and the fact that the ESP32 asks the nRF *why it
powered on* is the strongest evidence yet that the buttons are the nRF's to report.

`fw/main/hal_nrf_esp.c` is a **receive-only** listener that finds its own baud rate by accepting the
first standard rate that yields a CRC-valid frame, then keeps per-id statistics and prints payloads as
`u16le` and `u32le` so one capture identifies the sensors by how their numbers behave. Waiting on
hardware (task #19 and #20).

**Regression check, with all the firmware work in place:** `make vectors` reproduces
`claude/cc700-test-vectors.md` byte for byte, 14 of 14 C tests pass under both plain and ASan+UBSan
builds, and 21 of 21 Python tool tests pass. Note for a fresh WSL shell: `make test-py` needs a
Python with pytest, so `make test PYTHON=/root/venv/bin/python`; the Makefile already honours the
override and needed no change.

## Session end · 4 November 2026 · where to pick this up

**What now works on the real unit.** The display (ST7789 on the i80 bus, inversion and byte swap both
needed), the touch panel (CST3240 at 0x5A — the bring-up dot follows a finger), and the nRF link at
115200 baud, from which the barometer, the accelerometer and a clock are decoded. Three subsystems
brought up entirely from static analysis of the vendor binary; the only one that needed correcting on
hardware was the display.

**Read these first:** `claude/cc700-START-HERE.md`, then this file, then `docs/QUESTIONS.md`, then
`docs/BRINGUP-LOG.md` for the narrative and **`docs/NRF-PROTOCOL.md`** for the link specification and
the decoded sensor map. `fw/main/board_pins.h` holds every pin with its provenance and its verified
or Unverified status.

### The next three things, in order

1. **Two experiments, one window each, to finish the sensor map.** Flash `build/fw/app.bin` and run
   `python tools/capture_serial.py COM3 --seconds 300 --out capture.txt`. The firmware prints
   `==== window N of 6 ====` and ends itself after three minutes.
   - **Cover the light sensor, then a lamp.** Decides whether id 16 sub-tag 0xF1 byte 1 = 0x04 is the
     light sensor. It shows two shapes, `600 / 0 / ~1308` and `2000 / 0 / 3`; 600 matches the factory
     screen's `Light lux` exactly, but the round numbers look more like identifiers than readings, so
     the field that *moves* is the answer.
   - **Unplug the USB for one window.** Decides whether byte 1 = 0x0a (3560–3576) is the battery in
     millivolts.
   - **Press A and B.** Tests whether the buttons arrive on this link (Q38). No id outside 0 and 16
     has ever been seen in 200,000 frames.
2. **Implement `hal_baro_read_pa` against id 16 / 0xF1 / 0x03.** This is ready now: temperature is an
   int16 in hundredths of a degree at payload bytes 2–3, pressure a uint32 in hundredths of a pascal at
   bytes 4–7. It closes the real hole behind Q19 — `total_ascent` from a barometer rather than GPS
   elevation. The listener task already parses and verifies frames; it needs a consumer, not a rewrite.
3. **Then `hal_power_battery_pct` and `hal_power_ambient_lux`**, once step 1 says which message is
   which.

### Do not do these without asking Daniel

- **Transmitting to the nRF** (Q38). Id 16 is also what `SendPowerOffCmd` uses, so a malformed frame
  could switch the unit off. Nothing has been sent, and the passive tests above come first.
- **Bring-up step 1, preserving the 4 GB storage** (task #16). Still open, still Daniel's call: no copy
  route has been found and the decision is his.
- **The WP4 contact sheet review gate.** Work packages stop there by instruction; the firmware
  bring-up has been running alongside it, not past it.

### State of the tree

Everything is committed on `ccr-555714d9-9rv8ob`; nothing has been pushed, as instructed. The full
regression passes with all of the firmware work in place: `make vectors` reproduces
`claude/cc700-test-vectors.md` byte for byte, 14 of 14 C tests pass under both plain and ASan+UBSan,
and 21 of 21 Python tool tests pass — use `make test PYTHON=/root/venv/bin/python` in WSL, since the
system interpreter has no pytest. `make fw-build` reports 29 sources compiling clean for xtensa with no
warnings and an `app.bin` of about 1.24 MB in an 8 MB slot.

Captures are kept in `outputs/`; `nrf-capture*.txt` in the root is now git-ignored.

## Review, barometer and offline reverse-engineering tools · 17 November 2026

Daniel asked for a review of the work so far, a trial of the "Reverse Engineer Anything" tool, and
the next steps carried out. **I did not connect to the unit, open its serial port or flash anything**:
every result below is from the host tests, the firmware build, the flash backup file and a copy of
the vendor's phone app. The device steps this leaves for Daniel are at the end.

**What exists now.**

- **The barometer is implemented.** `fw/main/nrf_proto.c` (no ESP-IDF dependency) holds the link's
  framing and the sensor decode; `hal_nrf_esp.c` keeps the newest pressure for `hal_baro_read_pa()`,
  offers it for two seconds, and keeps decoding after the capture windows end instead of deleting its
  task; the core task polls it every 100 ms into `cc_on_baro`. A pressure outside 300 to 1100 hPa is
  refused, so a misread message gives no altitude instead of a wrong one (Q19's lesson).
- **`tools/re/`**: a reproducible way to read the stock firmware. `esp_image_to_elf.py` turns the
  backup's application into an Xtensa ELF (the image's own checksum and SHA-256 both pass, and the ELF
  is re-read against the image byte for byte); Ghidra 12.2 analyses it once (about four minutes);
  `gh.sh` with `DecompileAt.java` and `StringXrefs.java` then answers a question in under a minute.
  This replaces the hand method the bring-up used, whose scripts were never committed.
- **`docs/NRF-PROTOCOL.md` gained "What the vendor's handlers say"** and
  **`docs/STOCK-APP-FINDINGS.md`** is new.

**What the decompiler found** (all from the stock firmware's own code; detail and addresses in
`docs/NRF-PROTOCOL.md`):

- **The buttons are on the link, on a message that needs no request**: id 16, sub-tag `0x49`, key
  index in payload byte 1 and event code in byte 6, handed to `ArmSendBtnEvent`. Q38 assumed they
  might be behind ids that have to be asked for; they are not.
- **The clock is a calendar clock**, and the 4 November capture decodes to 4 November 2026. That is a
  confirmation from data already in hand: the layout came from the code, the date from the wire.
- **Battery, probably**: id 0 sub-tag `0x52` carries 4290 and 100 (millivolts and percent) with the
  unit on USB. **Light sensor, probably**: id 16 / `0xF1` / `0x04`, whose value the vendor maps through
  a threshold table to a level. Both wait for one passive test each.
- **The four power commands are known byte for byte.** Power off is kind 2, id 16, payload
  `e2 02 00 00 00 00 00 00`. So `hal_power_off` is a known frame. Nothing has been transmitted and
  the firmware has no transmit path; that is still Daniel's decision (Q38).
- **Three written statements were wrong and are corrected in place**: `SendQuerySlaveModeCmd` is kind
  2 on id 1 (not kind 1 on id 16); the senders' payloads are constants (not built from globals); and
  the 3560-ish value on `0xF1`/`0x0a` is not what the battery path reads.

**What the phone app found** (`docs/STOCK-APP-FINDINGS.md`): the app's bike-computer BLE service UUID
is in the stock firmware image; the CC700 Pro is product id 390 in Magene's C606 family; "SPU" is the
vendor's word for the product, not for the nRF co-processor, which our documents had wrong; firmware
updates come from the vendor's server (Q39).

**The REA trial: useful at the edges, not for the firmware.** REA 6.9.0 reads the flash backup's
layout correctly and independently agrees with our findings (partition table, both app slots,
`otadata`, bootloader IDF `423e6536`), and reads the APK's signature. It **refuses the firmware
itself** ("unsupported ELF architecture: e_machine 94", Xtensa), and its guidance rules out Cortex-M
raw images too, which is what an nRF image would be. Ghidra, which REA would have driven anyway,
supports Xtensa and is used directly. `rea setup` was not run, so the Claude configuration is
unchanged. Detail in `tools/re/README.md`.

**How to run.** `tools/re/README.md` for the offline tools. Everything else is unchanged:
`make test PYTHON=/root/venv/bin/python` and `make fw-build` under WSL.

**Verified.**

- `make vectors`: identical to `claude/cc700-test-vectors.md`.
- Host C: **17 of 17 plain, 15 of 15 under ASan and UBSan** (the plain count includes the two UI
  tests). `test_nrf_proto` is new: ten tests on frames copied byte for byte from the 4 November
  capture, every single-bit corruption of a frame refused, resynchronisation through noise at every
  chunk size from 1 to 17, the pressure limits, and the clock decoding the capture's own date.
- Python tools: 21 of 21.
- `make fw-build`: no warnings, **`app.bin` 1,239,952 bytes of 8,192,000 (15 %)**. Nothing flashed.
- Ghidra against known facts: the CRC routine at `0x4222137c` with its polynomial at `0x4220e4a8`, the
  frame builder at `0x4205062c`, and the barometer's divide-by-100 scaling all decompile to what the
  wire had already confirmed.

**Not verified.**

- **Nothing in this entry has run on the unit.** The barometer has never been read by our firmware on
  hardware; the key, battery and light layouts have never been seen in a clean capture.
- The key event codes (which number means press, release, long press) and which index is which
  button are unknown until buttons are pressed during a capture.
- The Android JVM suites were not re-run: nothing under `android/` or `core/src/` changed.
- The vendor app's code was searched as text, not decompiled.

**Deviations and choices.** REA is installed with `--ignore-scripts` under `/opt/re` and wrapped by
`tools/re/rea.sh`, which refuses its live-target commands (adb, gdb, process and browser capture) so
it cannot be pointed at the unit by accident. The vendor APK and the firmware ELF stay outside the
repository. Q39 is new. `docs/BRINGUP-LOG.md`'s header said nothing had been written to the unit,
which stopped being true on 4 November, and is corrected.

**A machine note.** Windows ran out of virtual memory once during this work and took the WSL VM down
with it, losing a Ghidra run; drive C: has 2.7 GB free, which caps the page file. Heavy jobs were run
one at a time afterwards. Freeing space on C: is worth doing before the next long session.

**Review notes for Daniel** (unchanged gates, restated because work has moved on around them): the
WP4 contact sheet is still unreviewed, so there are no golden images; the 4 GB storage is still not
backed up (bring-up step 1); and the stock firmware's responsiveness was never measured, which the
original plan made the yardstick for "better than stock" and which can still be done by switching
`otadata` back.

### For Daniel: the next session on the unit

One flash and one capture, about ten minutes. Everything is passive: the firmware only listens.

1. Flash `build\fw\app.bin` to `ota_1` at `0x7f0000`, exactly as for the previous builds (`otadata`
   already selects `ota_1`; nothing else is written).
2. Start `python tools\capture_serial.py COM3 --seconds 300 --out outputs\nrf-capture-2.txt` and wait
   for `==== window 1 of 6 starting ====`. Each window is 30 seconds.
   - Window 1: leave it flat on the desk.
   - Window 2: press each of the three buttons, one at a time, short presses a few seconds apart, then
     hold one for two seconds. Note the order you pressed them in.
   - Window 3: cover the light sensor with a thumb for ten seconds, then shine a phone torch on it.
   - Window 4: lift the unit from the desk to above your head and hold it there ten seconds, then put
     it on the floor for ten seconds. (About 2 m of height is about 24 Pa.)
   - Windows 5 and 6: nothing. USB has to stay in for the capture, so the battery test is separate.
3. Send the file. The lines that matter are `KEY`, `LIGHT?`, `BATTERY?`, `CLOCK` and `baro … Pa`.

The unit still cannot switch itself off under our firmware, so do not leave it running on battery.

## Capture 2: the link confirmed on the unit, and buttons as an input · 17 November 2026

Daniel flashed `a990a63` and ran the capture (`outputs/nrf-capture-2.txt`): power, left, right pressed
once each, power held two seconds, the light sensor covered then lit, the unit raised then put on the
floor. He typed every command; I read the file. 6390 frames, 0 CRC rejects. Detail in
`docs/NRF-PROTOCOL.md`, "Confirmed on the unit".

**Confirmed on hardware.**

- **Barometer, through our own firmware into the core.** Desk 100478 to 100480 Pa, overhead about
  12 Pa lower, floor about 7 Pa higher; the core reported 71 m. First time `hal_baro_read_pa` has run
  on the unit.
- **Buttons**: index 0 power, 1 left, 2 right; event 1 short press, 4 held, 5 released after a hold.
- **Battery**: 3477 mV and 2 % rising on charge (4290 mV and 100 % on 4 November).
- **Light sensor**: 0 to 30 covered, 600 in the room, 6000 to 9000 under a torch, in coarse bands.
- **Clock layout**, and that the clock had reset to 2023-06-01: it has to be set, not trusted.

**Found, not predicted.**

- The nRF has an off state. Until the power button was held it sent only the battery; on release it
  sent a power-on event and started the sensors, with nothing transmitted by us.
- The battery had run flat since 4 November, because our firmware cannot switch the unit off.
- A thumb on the case moved the barometer by up to 29 Pa (2.4 m) for a few seconds.

**What exists now.** `hal_input_poll` is real: `fw_nrf_keys_feed` turns the nRF's clicks into the
edges the core expects, with a one-second release if a hold's repeats stop. The core task logs each
edge while `NRF_DECODE_LOG` is on. The firmware still draws only the Ready screen (the screen manager
is in `ui/sim/app.c` until it is moved), so a button reaches the core but changes little on screen yet.

**Verified.** Host C 17 of 17 plain and 15 of 15 under ASan and UBSan; `test_nrf_proto` now has twelve
tests, two of them replaying the capture's key frames at their own times. `make fw-build` clean.
**Not verified:** the button input has not run on the unit (the capture was taken with the build
before it); the nRF's long-press threshold is unmeasured (Q40).

**Next, in order.** (1) Flash this build and check the `button:` lines against real presses. (2) Daniel
decides Q38's one remaining point, the power-off frame; without it the unit cannot be switched off.
(3) Battery and light into `hal_power`. (4) Move the screen manager into the firmware so buttons and
touch drive the real UI. (5) Storage pins and GNSS, read from the stock firmware with `tools/re`.
