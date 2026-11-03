# ui/

The LVGL 9 screens for all 24 mockup boards, the `ccsim` simulator, fonts and icons (WP4). LVGL is pinned in `LVGL_VERSION` and fetched by `deps.sh` into `build/deps/` with the Barlow fonts and `lv_font_conv` (`make deps`; the build calls it).

- `src/ui.h`: the model screens draw from (`ui_model_t`: the core's `cc_view_t` plus lists, sensors, phone and UI navigation state) and the layout kit.
- `src/ui_kit.c`: status bar, headers, 46 px rows, footer, buttons, chips (dashed for pending), page dots, signal bars, formatting. Absolute layout with coordinates taken from the board HTML; text is placed in CSS-style line boxes.
- `src/screens_ride.c`: Main, Workout, Climb, Navigation, Paused, Ride alerts (banner and hold bar overlays), Empty states. `src/screens_menus.c`: every other board.
- `src/scenarios.c`: one fixture per `Board:scenario` of the mockups (48), named identically.
- `fonts/gen_fonts.py`: generates exactly the font symbols the code uses (`font_b600_15`, `font_c700_96`, …) into `fonts/gen/`. `icons/gen_icons.py`: renders the mockup icons to A8 images (`icons/icons.c`).
- `lv_conf.h`: simulator configuration (RGB565, compressed fonts, SDL when available).
- `lint-exceptions.txt`: layout lint exceptions with reasons (none at present).

## ccsim

```
build/host/ui/ccsim                                 interactive window (SDL2): P power, A bottom left, L bottom right, mouse = touch
build/host/ui/ccsim --root DIR --script FILE.csv    ... with a storage folder and a sensor/GNSS script (played while recording)
build/host/ui/ccsim --shot Alerts:nextstep --out a.png
build/host/ui/ccsim --shots DIR                     every scenario, plus index.json
build/host/ui/ccsim --lint                          layout lint over every scenario (exit 1 on a problem)
build/host/ui/ccsim --walk DIR [--perf]             the WP4 scripted ride walk with screenshots (exit 1 on a failure)
```

`make shots` renders every scenario to `ui/out/lvgl/`, renders the mockups to `ui/out/mockup/` (Node and Playwright) and builds `ui/out/contact-sheet.png`, each pair mockup on the left and simulator on the right. Golden images (`golden/`) are made only after Daniel approves the contact sheet.
