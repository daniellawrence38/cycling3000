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
