# Enhancement ideas

A running log of ideas noticed while building and testing, kept so they are not lost and not
silently acted on. **Nothing here is scheduled, agreed or implemented.** Anything in scope for v1
belongs in the spec and the work packages instead; anything out of scope for v1 (START-HERE rule 4)
stays here until Daniel decides.

Each entry records what was noticed, why it might matter, and what it would cost. Entries are
numbered from I1 and never renumbered. Add new ones at the end.

Started 3 November 2026.

---

### I1 · Make `simunit` build on Windows
Noticed while running the test suite on a Windows PC for the first time (Q33). `simunit` includes
`<arpa/inet.h>` and `<sys/statvfs.h>`, so it does not build with MinGW-w64, which silently removes
the 3 Python and 9 JVM simunit-backed tests and the whole WP6 manual flow on a Windows-only machine.
WSL solves it completely, so this is convenience rather than need. Cost: a small shim for the two
headers (Winsock2 for the socket calls, `GetDiskFreeSpaceEx` for the free-space query) and a CMake
branch. Worth it only if a Windows-native workflow is ever wanted.

### I2 · Keep the sanitizer gate honest about where it ran
`make test-c-asan` cannot link on Windows at all: neither MinGW's GCC nor its Clang ships a
sanitizer runtime. The target currently just fails there, which reads like a code fault. It could
detect the missing runtime and skip with a clear message naming WSL/Linux/macOS, the way the Python
and JVM simunit tests already self-skip with `assumeTrue`. Cost: a few lines in the Makefile or a
CMake check. Keeps "green" meaning the same thing on every machine.

### I3 · Pin the ESP-IDF submodule set, or check it before building
The first `make fw-build` failed twice in a row because a `git clone --depth 1 --recursive` of
ESP-IDF v5.5.6 leaves some submodules registered but with an empty working tree (`unity`, `spiffs`
and `protobuf-c` here, 3 of 23). The failure surfaces as a confusing CMake error about an include
directory not being a directory, a long way from the cause. `tools/fw_build.sh` already checks the
IDF *version*; it could also check that the submodules a build needs are populated and say plainly
what to run (`git submodule update --init --recursive`). Cost: a handful of lines. Would have saved
two full rebuild cycles tonight.

### I4 · Record elevation provenance per route, not just per sample
`docs/DEM-FINDINGS.md` now shows the GA 5 m LiDAR has no coverage in the Perth Hills, so a hills
route silently comes from SRTM 30 m while a river-flat route comes from 5 m LiDAR. The app already
labels file-sourced elevations "from file", but a route built from a mix of GA and Open Topo Data
samples does not say so. Showing which source a route's profile actually came from would make a
surprising climb gradient explainable rather than mysterious. Cost: one field on the stored route
and a line in the route detail screen. Related to Q32.

### I5 · Learn the nRF UART protocol during bring-up
The stock firmware shows an nRF co-processor on a UART and a full ANT+ stack on the ESP32 side
(`docs/STOCK-FIRMWARE-FINDINGS.md`, Q34). Everything ANT — speed and cadence, heart rate, FE-C, radar,
tail light, shifting and Di2 — arrives through that link, and the custom firmware currently cannot use
any of it. Bring-up step 3 already probes GPIO read-only; the two configured UART pin pairs are the
obvious thing to watch at the same time, which costs nothing extra and no additional risk. Knowing the
framing would unlock ANT+ sensors and the Di2 gear display in one go. Out of scope for v1 (rule 4).

### I6 · `backup_report.py` should find the bootloader version by string, not only by struct
It reports `bootloader_desc_idf: None` for this unit, which reads as "the version is unknown" and sent
WP7 off to search 117,349 public commits. In fact `423e6536` is sitting in the bootloader region as a
plain string; the report misses it because it only looks for `esp_bootloader_desc_t`, which is IDF 5.2
and later. A fallback that scans the region for an IDF-version-shaped string would have answered Q31
immediately. Cost: a few lines next to the existing check.

### I7 · The `-Werror=format` class of bug needs a host-side guard
`make fw-build` failed on 11 `%u`-for-`uint32_t` errors in `core/src/alerts.c` and `core/src/core.c`.
They are invisible on the host because `uint32_t` *is* `unsigned int` on x86_64, and only appear on
xtensa where it is `long unsigned int`. The whole shared `core/` is exposed to this, and today it was
caught only because the firmware was finally built. A cheap guard: build the host tests once with
`-D__INT32_TYPE__=long` or, more practically, add a CI/Make target that compiles `core/` with the
xtensa toolchain even if it never links. Cost: moderate; value: this class of bug otherwise waits
until someone builds firmware.

### I8 · The unit has Wi-Fi and offline maps already
`AppWifiManage.c`, `WifiList.c`, `wpa_supplicant`, and a set of offline-map pages by province and city
are in the stock firmware. v1 rules Wi-Fi and base maps out of scope (rule 4) and nothing here argues
with that. Worth recording only because "the map storage format" is listed as unknown in rule 3, and
the stock firmware plainly has one: if maps are ever wanted, there is a format on the SD card to read
rather than invent.
