# Mockup tooling

`gen.py` defines boards 13 to 24 of the CC700 mockups (Routes, Route detail, History, Ride summary, Paused, Pause menu, Confirm, Ride alerts, Empty states, Heart rate sensor, Phone, Settings) once (layout, colours, text and scenario values) and writes two things: the `.dc.html` design boards into `$CC700_DESIGN_ROOT/canvas/project/` and static HTML previews of every scenario into `$CC700_DESIGN_ROOT/out/`. Boards 1 to 12 (the ride pages, workout library, Ready, Menu, sensors, Power meter, Profile) were authored by hand and are not generated; read them from the published design artifact (spec section 1 has the link) with the Artifact tool. To render them offline, read each board and the type's `support.js` into one folder and screenshot them with Playwright; this has not been tried. `shoot.py` renders each preview to PNG at 2x with Playwright, prints any text that overflows its box, and builds `out/_sheet.png`, a contact sheet.

Setup: `CC700_DESIGN_ROOT` defaults to this folder. Install the fonts with `npm install @fontsource/barlow @fontsource/barlow-condensed` in that folder, and `pip install playwright pillow` then `playwright install chromium`.

Run: `python3 gen.py` then `python3 shoot.py` (or `python3 shoot.py Alerts Confirm` for named boards).

Use these PNGs as the visual reference when building the LVGL screens. The published artifact stays the source of truth if the two ever differ. Do not publish to the artifact from this tooling; ask Daniel.
