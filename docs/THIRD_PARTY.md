# Third-party code and licences

No GPL or AGPL code in the core, firmware or app (START-HERE, brief 3).

| Component | Version | Licence | Where | Used for |
| --- | --- | --- | --- | --- |
| Unity | v2.6.1 (commit cbcd08f) | MIT | `core/tests/unity/` (vendored, `LICENSE.txt` alongside) | Host C unit tests only; not linked into firmware |
| pytest | latest from PyPI at install time | MIT | developer machine | Python tool tests |
| fitdecode | latest from PyPI at install time | MIT | developer machine | Independent FIT decoding in `tools/fitcheck` (WP2, WP3) |

| LVGL | v9.6.0 | MIT | fetched by `ui/deps.sh` into `build/deps/lvgl` | UI library (simulator now, firmware in WP7) |
| Barlow, Barlow Condensed | Fontsource 5.3.0 (WOFF) | SIL OFL 1.1 | fetched by `ui/deps.sh`; generated bitmaps in `ui/fonts/gen/` | All text; the generated fonts are a derived work under the OFL |
| lv_font_conv | 1.5.3 | MIT | fetched by `ui/deps.sh` (npm) | Font conversion at build time only |
| lodepng | commit 7b2893e | zlib | `ui/sim/lodepng/` (vendored, `LICENSE` alongside) | PNG output in ccsim only |
| SDL2 | 2.30 (system package) | zlib | developer machine | ccsim's window only |
| Playwright, Chromium | as installed | Apache-2.0, BSD-3 | developer machine | Rendering the mockups for comparison (tools/design) and icons; not shipped |

Mockup icons in `ui/icons/icons.json` are the SVG paths from Daniel's mockups.

Pinned for later work packages, not yet fetched: ESP-IDF v5.5.6 (Apache-2.0, includes NimBLE, Apache-2.0).
