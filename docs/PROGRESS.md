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
