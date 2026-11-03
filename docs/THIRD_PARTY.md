# Third-party code and licences

No GPL or AGPL code in the core, firmware or app (START-HERE, brief 3).

| Component | Version | Licence | Where | Used for |
| --- | --- | --- | --- | --- |
| Unity | v2.6.1 (commit cbcd08f) | MIT | `core/tests/unity/` (vendored, `LICENSE.txt` alongside) | Host C unit tests only; not linked into firmware |
| pytest | latest from PyPI at install time | MIT | developer machine | Python tool tests |
| fitdecode | latest from PyPI at install time | MIT | developer machine | Independent FIT decoding in `tools/fitcheck` (WP2, WP3) |

Pinned for later work packages, not yet fetched: LVGL v9.6.0 (MIT), ESP-IDF v5.5.6 (Apache-2.0, includes NimBLE, Apache-2.0), SDL2 (zlib), lodepng (zlib), Barlow and Barlow Condensed (SIL OFL 1.1), lv_font_conv (MIT).
