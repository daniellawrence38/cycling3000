# Third-party code and licences

No GPL or AGPL code in the core, firmware or app (START-HERE, brief 3).

| Component | Version | Licence | Where | Used for |
| --- | --- | --- | --- | --- |
| Unity | v2.6.1 (commit cbcd08f) | MIT | `core/tests/unity/` (vendored, `LICENSE.txt` alongside) | Host C unit tests only; not linked into firmware |
| pytest | latest from PyPI at install time | MIT | developer machine | Python tool tests |
| fitdecode | latest from PyPI at install time | MIT | developer machine | Independent FIT decoding in `tools/fitcheck` (WP2, WP3) |
| Pillow | latest from PyPI at install time | MIT-CMU (HPND) | developer machine | `tools/design/contact_sheet.py` builds the WP4 contact sheet; not shipped |

| LVGL | v9.6.0 | MIT | fetched by `ui/deps.sh` into `build/deps/lvgl` | UI library (simulator now, firmware in WP7) |
| Barlow, Barlow Condensed | Fontsource 5.3.0 (WOFF) | SIL OFL 1.1 | fetched by `ui/deps.sh`; generated bitmaps in `ui/fonts/gen/` | All text; the generated fonts are a derived work under the OFL |
| lv_font_conv | 1.5.3 | MIT | fetched by `ui/deps.sh` (npm) | Font conversion at build time only |
| lodepng | commit 7b2893e | zlib | `ui/sim/lodepng/` (vendored, `LICENSE` alongside) | PNG output in ccsim only |
| SDL2 | 2.30 (system package) | zlib | developer machine | ccsim's window only |
| Playwright, Chromium | as installed | Apache-2.0, BSD-3 | developer machine | Rendering the mockups for comparison (tools/design) and icons; not shipped |

| Kotlin, kotlinx.serialization | 2.0.21, 1.7.3 | Apache-2.0 | Gradle (Maven Central) | android :core |
| OkHttp, MockWebServer | 4.12.0 | Apache-2.0 | Gradle | HTTP clients (Intervals.icu, elevation); MockWebServer in tests only |
| JUnit 5 | 5.11.3 | EPL-2.0 | Gradle | JVM tests only, not shipped |
| Gradle wrapper | 8.14.3 | Apache-2.0 | `android/gradlew`, `android/gradle/wrapper/` | Build only |

| Ghidra | 12.2 | Apache-2.0 | developer machine (`/opt/re/ghidra`, installed by `tools/re/setup_wsl.sh`) | Offline reading of the stock firmware backup (`tools/re`); not shipped, nothing of it is in the firmware or the app |
| REA (`rea-agents`) | 6.9.0 | MIT (bundles third-party tools under their own licences, listed in its `THIRD_PARTY_NOTICES.md`) | developer machine (`/opt/re/rea`) | Offline inspection of the flash backup's layout and of the vendor APK's signature (`tools/re/rea.sh`); not shipped |
| Node.js | 24.22.3 | MIT | developer machine (`/opt/re/node`) | Runs REA only |
| OpenJDK | 25 | GPL-2.0 with Classpath Exception | developer machine (system package) | Runs Ghidra only; a tool, not linked into anything shipped |

Mockup icons in `ui/icons/icons.json` are the SVG paths from Daniel's mockups.

Pinned for later work packages, not yet fetched: ESP-IDF v5.5.6 (Apache-2.0, includes NimBLE, Apache-2.0).

- **ESP-IDF v5.5.6** (Apache-2.0), with NimBLE (Apache-2.0): the firmware build in `fw/`, installed by the developer, not vendored. LVGL 9.6.0 for the firmware comes from the ESP component registry (`lvgl/lvgl`, MIT).
