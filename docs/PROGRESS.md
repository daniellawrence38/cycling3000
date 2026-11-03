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
