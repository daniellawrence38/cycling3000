# CC700 Pro software: start here

For the implementing model. Updated 3 November 2026 (spec 0.5, brief 1.1, protocol 1.1, vectors 1.1). Read this whole file before doing anything else.

## The job

Daniel (water-utility engineer, Perth WA) wants custom ride software for a GEOID CC700 Pro cycling computer (ESP32-S3, 240 × 320 touchscreen, three buttons) plus an Android companion app. Everything he has decided, and everything his assistant decided on his behalf pending real use, is written down. Your job is to build it: a portable C core library, an LVGL simulator of the unit, a simulated unit program, an Android app, test tooling, and a firmware skeleton that builds but is never flashed. You will not touch the hardware.

You have been given enough to work without asking questions. Choose the documented default when something is unclear, write it down, and keep going.

## Hard rules

1. **Never touch the unit.** Do not connect to it, read it, flash it, erase it, reset it, run `esptool`, `openocd`, `dfu-util` or any serial or USB tool against any attached device, or change any eFuse. Opening its serial port restarts it. If a device appears attached, ignore it. The only hardware output you produce is `docs/BRINGUP.md`, written for Daniel to run himself, one step at a time, after he asks for it. The reason: the unit's separate 4 GB storage is not backed up and flash restoration is untested, so a mistake could be unrecoverable.
2. **Never handle secrets.** Do not ask for, store, log, print or write any API key, token or password: not in source, tests, fixtures, comments, commit messages or documents. An Intervals.icu key was once pasted into a chat and must be treated as compromised; do not use it. Tests use mock servers. The live account check is Daniel's.
3. **Do not invent hardware facts.** Pin assignments, the touch controller, GNSS and barometer parts, the radio for ANT+, the map storage format and the separate storage's file system are all unknown. Put them behind the HAL interfaces and mark anything that depends on them *Unverified*. Where files live on the unit is decided (the `CC700/` folder on the separate storage, spec 7.2), but write the storage code for the weakest file system it may be (FAT, brief 4).
4. **Stay in scope.** In: the four ride pages and all 24 screens, workouts, FTP, routes from GPX, climbs, FIT recording and recovery, phone link, Intervals.icu upload, the simulator, the Android app, the firmware skeleton. Out for v1: Strava, Garmin, a routing engine or rerouting, base maps, over-the-air updates, sound or vibration, Wi-Fi, iOS, ANT+, Intervals.icu workout download, any new screen not in the mockups.
5. **Implement provisional decisions as written.** They are decided for now; Daniel will revise them after riding. Make them easy to change (named constants in one header, one place in the app), and do not reopen them.
6. **Do not deviate silently.** If a document is wrong, contradictory or impossible, choose the safest reading, log it in `docs/QUESTIONS.md` under "Doc errors" with what you did, and continue.

## Precedence when documents disagree

1. The hardware handover for any hardware fact.
2. `claude/workout-coach-instructions.md` for the workout JSON format.
3. The test vectors for any number or byte they contain.
4. The protocol document for bytes on the air and in stored objects.
5. The spec for behaviour and on-screen text.
6. The mockups for layout, colour, type and on-screen text where the spec is silent.
7. The engineering brief for how to build it.

Known gap: the Main mockup (board 1) still shows a 2 × 2 grid; spec 0.5 makes it 2 × 3 with distance and lap time. The spec governs the field list here; update the board in `gen.py` at the start of WP4 (brief 6.1).

## Reading order

All in the Bike Computer Project (or as files if you were given them that way):

1. `claude/cc700-START-HERE.md` (this file)
2. `claude/cc700-software-spec.md` (what the product does)
3. `claude/cc700-engineering-brief.md` (architecture, algorithms, work packages)
4. `claude/cc700-ble-protocol.md` (bytes on the air and stored objects)
5. `claude/cc700-test-vectors.md` (golden numbers) with `claude/tools/reference/ref.py` and `vectors.py` (the reference that produced them)
6. `claude/workout-coach-instructions.md` (workout format)
7. `claude/cc700-questions.md` (the 3 November review: what changed and why; your own questions go here too)
8. `GEOID-CC700-Pro-complete-handover.md` (hardware facts and the recovery gate; skim, then use as reference)
9. The mockups: https://claude.ai/artifact/6VKNT6PhDD9q2R31oGEFgN. Read a board with the Artifact tool (`action: "read"`, `path: "project/<Board>.dc.html"`). `claude/tools/design/gen.py` defines every board once; `shoot.py` renders each scenario to PNG, so you can regenerate reference images offline.

## Working method

- Start with WP0 in the engineering brief and follow the work packages in order. Stop only where a stop is written (after the WP4 contact sheet, and at the end of WP7).
- Write tests first from the vectors. A vector that fails in your code means your code is wrong; if you believe the vector is wrong, say why in `docs/QUESTIONS.md` and keep your implementation matching the vector until Daniel decides.
- Every `//` in the vectors and `ref.py` is floor division; C and Kotlin `/` truncate toward zero. Use `cc_floordiv` and `Math.floorDiv` (brief 5.0).
- Keep `make test` green. Compile with warnings as errors. Run host tests under ASan and UBSan.
- At the end of each work package append to `docs/PROGRESS.md`: what exists, how to run it, what you verified and how, what deviates from the documents, and what remains unverified. Be plain about what you did not test. If you can write to the Project, mirror it to `claude/cc700-progress.md`.
- Questions go in `docs/QUESTIONS.md` as `### Q<n> · <WP> · <open|answered>` with the question, the options, the default you used and why. Mirror to `claude/cc700-questions.md` if you can, numbering on from the entries already there. Never wait for an answer; work on something else.
- Third-party code: record every dependency and licence in `docs/THIRD_PARTY.md`. No GPL or AGPL in the core, firmware or app.
- Commit in small steps with clear messages. Do not push anywhere unless Daniel asks.

## What you will need from Daniel

His review of the screenshot contact sheet (end of WP4) before you make golden images. A phone test of the app against the simulated unit (WP6). His review of `docs/BRINGUP.md` (WP7). His first live Intervals.icu upload with his own, regenerated key (API access is free for every user; the upload itself is unchecked). His actual FTP (the mockups use 260 W as a placeholder).

## What done means

Section 10 of the engineering brief. In short: all vectors pass in C, Kotlin and Python; the app runs the whole flow against the simulated unit; the simulator shows every screen and scenario and Daniel has approved them; the firmware skeleton builds; `docs/PROGRESS.md` says honestly what is and is not verified. Nothing you build is proven on the real unit; say so.
