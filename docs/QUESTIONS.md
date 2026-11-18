# CC700 questions and document errors (implementation)

Kept by the implementing model. The design-review entries Q1 to Q13 and E1 to E4 are in `claude/cc700-questions.md`; new questions here are numbered from Q14 and new document errors from E5. Daniel answers by adding an `Answer:` line or in chat. A question never blocks work: the default is used until answered.

Format:

```
### Q<n> · <work package> · <open | answered>
Question:
Options:
Default used:
Answer:
```

Document errors use `### E<n> · <document and section>` and `Reading used:` in place of `Default used:`.

**Convention added 3 November 2026.** Daniel asked for the open questions to be researched overnight.
Where evidence could actually settle a question — a specification, a source check, a measurement, or the
flash backup read offline — the finding is written on the `Answer:` line, the heading is marked
`answered`, and the evidence is named so it can be checked. Where the question is Daniel's preference,
or needs a real ride, the question stays `open` and gains a `Research:` line with what was found and a
recommendation; the `Answer:` line is left empty for him. Provisional decisions are not reopened
(START-HERE rule 5): research that *supports* a default says so and changes nothing.


**Status, 17 November 2026.** Q36 is answered and implemented for the barometer. Q38 is answered:
the buttons arrived on a message that needs no request,Daniel confirmed the button edges work on the
unit, and he approved sending the `SendPowerOffCmd` frame so the unit can power off. Q39 and Q40 are
new. Open and needing Daniel: Q18, Q21, Q22, Q23, Q24, Q32, Q34, Q35, Q37, Q39, Q40. The paragraph
below is the 4 November status, kept as written.

**Status, 4 November 2026.** Answered: Q14, Q15, Q16, Q17, Q19, Q20, Q25, Q26, Q27, Q28, Q29, Q30,
Q31 and Q33 — fourteen of the twenty-two. The eight still open all need Daniel, and none of them
blocks anything: **Q18** (what "LAP n" counts), **Q21**, **Q22**, **Q23** and **Q24** (preference and
mockup-versus-spec calls on the UI, each with a `Research:` line setting out the trade and what it
costs to change later), **Q32** (whether 30 m elevation sampling is good enough for climb detection,
which needs a ride over a known climb), **Q34** (whether ANT+ comes into scope for a later version)
and **Q35**, added tonight: its default is implemented, but it changes what two screens say for a
route with no elevation and no mockup covers that state.
Three are worth reading even though two of them are answered. **Q19** found a fault: the file was
claiming an ascent it had measured with GNSS. **Q29** is now proved from the linked firmware rather
than from the source. **Q35** is the same fault on the phone and in the route screens.

## Open questions

### Q14 · WP1 · answered
Question: Protocol 3.2 lists what the unit validates in a CW1 object, but not the header `flags` byte (documented as 0), the reserved step flag bits (documented as 0), or the characters of the name and labels. Should the unit reject an object that breaks these?
Options: (a) accept anything the listed checks allow; (b) also reject non-zero header flags, reserved step flag bits, and name or label bytes outside printable ASCII 0x20 to 0x7E.
Default used: (b). It is the safer reading: a future format that sets a flag the unit does not understand is refused (protocol 8 says unknown versions are refused for the same reason), and the UI never has to draw a control character. The phone never produces such objects, so nothing valid is lost. All are reported as BAD_OBJECT.
Answer: **(b) stands, implemented and fuzzed.** `core/src/workout.c` rejects a non-zero header `flags` byte (`CC_CW1_FLAGS`, line 41), a non-zero reserved bit in a step's flags (line 72, the same code), and any byte outside printable ASCII 0x20 to 0x7E in the name or a step label (`printable()`, lines 49 to 53 and 86, reported as `CC_CW1_NAME`). All reach the phone as `BAD_OBJECT`, as protocol 3.2 requires.
Two reasons to keep it, beyond the safety argument already recorded. First, it is the same rule protocol 8 applies to versions: refuse what you do not understand, rather than ignore it. A future CW1 that sets a flag to mean "the last step repeats" would, under (a), be accepted and then run wrongly — silently, which is the failure mode this project keeps finding (E8, E9). Second, it is free: the phone is the only producer, and `importWorkout` never emits such an object, so nothing valid is refused.
The risk of (b) — that a future *reader* of an old object becomes stricter than the writer that made it — does not arise, because nothing stored on the unit was written by anything but this code.
`test_fuzz_cw1` exercises the decoder against mutated objects and finds no accept that should have been a reject, and no crash.

### Q15 · WP1 · answered
Question: When the 3 s power becomes invalid during a step (meter lost), what happens to the status pill's state machine?
Options: (a) reset it; (b) hold its state and dwell count, show no state, and resume feeding when power is valid again.
Default used: (b). The spec says only that the pill shows nothing while the 3 s power is invalid; holding the state means a short dropout does not restart the 2 s dwell or the 5 s settle period.
Answer: **(b) stands, and the code does exactly it.** In `core/src/workout.c`, the per-second step update sets `v->pill = CC_PILL_HIDDEN` and `v->pill_delta = 0` unconditionally, and then calls `cc_pill_feed` **only if `p3_valid`**. So while the 3 s power is invalid the state machine is not fed, not reset, and not read: its state and dwell count sit untouched, and the screen shows nothing, which is all the spec asks for. `cc_pill_reset` happens once per step, guarded by `e->pill_started`, so a dropout cannot restart the 2 s dwell or the 5 s settle window either.
Why (b) rather than (a): a power meter dropout of a second or two is routine (a dead spot in the cadence, a radio collision), and under (a) every one of them would cost the rider a fresh 2 s dwell before the pill could say "TOO LOW" again — the pill would be least useful exactly when the data is patchy. Holding state means a dropout costs only the seconds it lasts. The cost of (b) is that the pill can reappear instantly on a stale state after a long dropout; that is bounded by the state only ever being one of four values computed from the same thresholds, so the worst case is a correct-looking pill one second early.
`test_pill_v71` covers the invalid-power cases, including the pill staying hidden.

### Q16 · WP2 · answered
Question: Are FIT `record` messages written while the ride is paused?
Options: (a) one record per second from START to finish, paused or not; (b) records only while RECORDING, with the pause shown by timer stop and start events.
Default used: (b). Brief 5.1 already limits ride statistics to RECORDING seconds; Intervals.icu and other tools read the timer events for moving time. Totals in the session come from the records, so a pause adds elapsed time but no samples.
Answer: **(b), and the implementation is already safe against the one way this goes wrong.** (b) is the ordinary convention: a pause is a timer `stop` event and a resume a timer `start` event, which is what the Garmin FIT SDK forum shows people doing and what importers read.
The risk worth naming is **a stop with no matching start**. A reported GPSMAP 67 file had exactly that, and Strava imported only up to the first stop — the rest of the ride was silently dropped. So (b) is only correct if every stop is paired.
Checked in this codebase and it is: `core/src/core.c` writes `cc_fit_event_timer(..., 1)` on start (line 353) and on resume (line 398), and `..., 0)` on pause (line 387); `cc_fit_finish` (`core/src/fit.c:348`) writes a closing stop *only if the timer is running*, so it never emits a second, dangling one. `core/tests/test_fit.c` drives a pause at 600 s and a resume at 660 s and checks the session's timer total excludes the gap. A power cut while paused leaves a file ending in a stop, which is a truthful end rather than a truncation.
Not checked against the live service: how Intervals.icu specifically derives moving time. It is unaffected by the choice above and stays part of Daniel's first live upload.

### Q17 · WP2 · answered
Question: While storage fails, brief 5.6 asks for recording "in RAM as far as a 5-minute ring allows". A ring keeps the newest records, but dropping the oldest unsaved bytes out of the middle of a FIT stream is awkward.
Options: (a) a true ring that drops the oldest unsaved records; (b) a 24 KB buffer (about 10 minutes of records at 25 bytes each) that keeps the oldest unsaved records and drops new ones once full; events and laps have a reserved 2 KB.
Default used: (b), simpler and never corrupts the stream; the buffer holds more than 5 minutes. The test fills it with 2,000 records during an outage, checks at least 300 are kept and that the file finalises with a valid CRC once storage returns.
Answer: **(b) stands.** `CC_FIT_BUF` is 24,576 bytes with `CC_FIT_RESERVE` 2,048 of it kept for events and laps (`core/include/cc700/fit.h`); records stop being buffered at `CC_FIT_BUF - CC_FIT_RESERVE` and increment `dropped_records`, while an event or a lap may still use the reserve (`core/src/fit.c:160`). `test_storage_error_keeps_records_in_ram` fails 2,000 record writes, checks that some were dropped, that at least 300 — five minutes — were kept, and that the file still finalises with a valid CRC when storage returns.
Why keeping the oldest is right, which is the part the brief's wording leaves open. A FIT file is a stream of definition-dependent messages, so dropping bytes out of the middle of what has already been buffered is not a matter of moving a tail pointer: it would require re-serialising from a record boundary, and a reader that hits a partial message stops there. Keeping the oldest and refusing new ones needs no such surgery and leaves a file that is valid all the way to its end. The data cost is small and the right way round: a storage outage near the end of a ride loses the newest minutes, but the ride up to the outage is intact and uploadable, whereas a ring would sacrifice the start of the ride to keep the part most likely to be ridden home on a dead unit anyway.
The gap this leaves in the file is harmless to readers: every record carries its own timestamp, so an outage reads as a period with no samples, exactly like the pause gap that Q16 already relies on.

### Q18 · WP2 · open
Question: The workout banner and the Lap banner say "LAP n". Should n count only manual laps, or every FIT lap including the automatic laps at workout step changes?
Options: (a) manual laps only; (b) every lap in the file.
Default used: (b), so the number on the banner matches the lap number in Intervals.icu. With a workout loaded this makes the numbers jump.
Research: this one is genuinely yours — it is a preference about what the number on the banner is *for*, and both readings are defensible. The trade is: (b) keeps the banner honest against what you will later see in Intervals.icu, at the cost of the number jumping mid-workout as automatic step laps are counted; (a) keeps "LAP 3" meaning "the third time I pressed lap", which is what the number means to a rider pressing the button, at the cost of disagreeing with the upload. The deciding evidence is a ride with a workout loaded, watching whether the jumping number is confusing in practice. Nothing in the code depends on which you pick — it is one counter — so changing it later is cheap.
Answer:

### Q19 · WP2 · answered
Question: Without a barometer, what altitude goes in the FIT file?
Options: (a) none; (b) the GNSS altitude.
Default used: (b) for the file only. Gradient, ascent and the Climb page stay invalid ("--") without a barometer, as brief 5.3 says.
Answer: **(b) stands for the record stream, and it exposed a real fault in the totals, now fixed.**

Research first, on what the receiving platforms actually do with the altitude in a file. **Strava** decides by whether the device has a barometer: for a device without one it discards the file's elevation and substitutes its own from the GPS track and its terrain data, and for a barometric device it keeps the file's. **Intervals.icu** has an "elevation correction" setting: with it on, it looks up an elevation for each GPS point from map services and computes ascent from those; with it off, it uses the altitude and the ascent total that are in the file, with its own algorithm. So on both services an altitude stream from GNSS is at worst ignored and at best correctable — which is the argument for (b), and (b) stands. A file with no altitude at all is strictly worse: an Intervals.icu user with correction off then sees a flat ride, with nothing to correct from.

Provenance: those two behaviours come from Strava and Intervals.icu support threads and from the Intervals.icu developer's own replies, not from API documentation, so they are Unverified in the strict sense. The decision does not rest on them — (b) is at worst ignored and at best useful on either service — but the number the file *claims* does, which is what follows.

The fault that this search turned up is in the **totals**, not the stream. Writing the GNSS altitude into each record set `has_alt`, which made the FIT accumulator treat the ride as having usable altitude, so every lap's and the session's `total_ascent` was computed from GNSS altitude with brief 5.3's 1 m threshold — and the `.meta` summary, and therefore the Summary screen's ASCENT tile, took the same number. That is wrong three times over:
- Brief 5.3 defines ascent from the *smoothed barometer* altitude alone. Nothing authorises a GNSS ascent.
- The unit's own ride screen shows `--` for ascent without a barometer (spec 5, `v->alt_ok`). The Summary screen for the same ride showed a confident figure. A device should not print a number it has just refused to print.
- GNSS vertical error is several metres and wanders, so a 1 m threshold accumulates a large phantom ascent. This is exactly the "inflated gain" effect the Garmin and Suunto forum threads describe for unsmoothed barometric data, and GNSS noise is worse. The number would have been not just unauthorised but badly wrong, and Intervals.icu with correction off would have taken it at face value.

Fixed by separating "there is an altitude" from "an ascent may be claimed". `cc_fit_record_t` gains `alt_baro`, set only on the barometric branch in `core.c`; the accumulator gains `ascent_claim`, set only by a record with `alt_baro` or by the ride engine's explicit barometric override in `cc_fit_close_messages`; lap and session write `total_ascent` as **FIT-invalid (0xFFFF), not 0**, without a claim. `cc_fit_summary_t` gains `has_ascent`, carried in the `.meta` flags byte's spare bit 3 (an older `.meta` decodes as "no ascent", which is the safe reading, so the format version stays 1), and the Summary screen shows `--`, the same idiom it already uses for power and heart rate. 0xFFFF rather than 0 matters: a 0 asserts "this ride was flat", which is a different false claim.

Two tests pin it. `test_no_barometer_claims_no_ascent` records a clean 30 m climb with GNSS altitude only and asserts no claim, then the identical ride with `alt_baro` set and asserts 29 m (the threshold leaves the last 90 cm uncounted). `test_no_barometer_file_has_altitudes_but_no_ascent` decodes the resulting file with fitdecode and asserts `session.total_ascent is None` while all 300 record altitudes are present and span the climb — so the stream is there to correct and the total is absent rather than zero.

One consequence worth stating: a **recovered** ride claims no ascent either, because recovery rebuilds the totals by re-reading the records and a FIT record does not say where its altitude came from. A recovered barometric ride therefore loses its session `total_ascent` while keeping every altitude, so any platform can still compute it. That is the truthful answer from the file alone; carrying the flag through recovery would need a byte in the `.part` sidecar and is not worth it.

Still Unverified: whether this unit has a usable barometer at all, and its noise, which is a bring-up question. The no-barometer path above is the fallback, not the expected case.

### Q20 · WP4 · answered
Question: Brief 6.2 asks for the glyph `→` in every font. The Barlow files available (Fontsource 5.3.0, all subsets) have no `→`, and no mockup uses it as text (arrows are drawn as icons).
Options: (a) leave it out; (b) take it from another font.
Default used: (a). If a screen ever needs it in text, it can be drawn as an icon.
Answer: **(a), and it was not a choice.** Verified directly against the fetched fonts by reading every `cmap` with fontTools: across the 54 normal-weight Barlow and Barlow Condensed files in `build/deps/node/node_modules/@fontsource/barlow*/files/` (subsets `latin`, `vietnamese`, `condensed`), `U+2192 RIGHTWARDS ARROW` appears in **0**. Every other glyph brief 6.2 asks for is there, in the 18 latin-subset files: `·` U+00B7, `–` U+2013, `−` U+2212, `…` U+2026, `°` U+00B0, `×` U+00D7 — 18/54 each. So the brief asks for one glyph the chosen typeface does not contain, and (b) would mean mixing a second typeface for a single arrow that no mockup uses as text. Leaving it out is right; drawing it as an icon stays the escape route if a screen ever needs one.

### Q21 · WP4 · open
Question: Brief 6.3 picks the map scale so "the next turn and 1.5 times the distance to it fit" in the view width, with the rider at 60 % of the map height. Only about 95 px of the 158 px map is above the rider, so at the 400 m view that rule gives for a turn 250 m ahead (the mockup's case) the turn is off the top of the map.
Options: (a) fit across the view width as written; (b) fit 1.5 times the distance in the space above the rider.
Default used: (b): view width at least 1.5 × distance × 240 / 95, from {100 … 3200} m, never below 200 m. The turn is then always on the map, as the mockup shows.
Research: the numbers are now measurable, and they are what makes this a real choice rather than a bug fix. The rider sits at 60 % of the 158 px map, so 95 px of map lies ahead; the view is 240 px wide. For the mockup's case — a turn 250 m ahead — the brief as written needs 1.5 × 250 = 375 m across the width, which picks the 400 m step and puts the turn at 250/400 × 240 = 150 px ahead of the rider, 55 px past the top edge. So the mockup's own "400 m" label and its visible turn chevron cannot both be right. Fitting the same 375 m in the 95 px actually available needs 1.5 × 250 × 240 / 95 = 947 m, which picks **1600 m** from {100, 200, 400, 800, 1600, 3200}.
That is the cost of (b), and it is visual: where the mockup reads "400 m", the unit will read "1600 m", and the route line and surroundings will be drawn four times smaller than the mockup suggests. The turn is always on the map, which is what (b) was chosen for, but the Navigation page looks more zoomed-out than the design.
Two other ways out, both of which reopen something and so were not taken: move the rider lower than 60 % of the map (brief 6.3 fixes 60 %), or accept the turn being off the top and rely on the banner, which already states the distance and the instruction in words. The third is to keep (b) and coarsen nothing — the current code (`core/src/nav.c:464`).
Deciding evidence is a look at the Navigation page in the simulator at both scales with a 250 m turn; it is one constant either way (`240.0f / 95.0f`), so changing your mind later is a one-line edit and a golden-image refresh.
Answer:

### Q22 · WP4 · open
Question: The Navigation mockup draws the route 8 px wide on a `#10151B` map; brief 6.3 and spec 8.3 say 3 px on the plain background `#0B0D10` for phase 1.
Options: follow the mockup; follow the spec.
Default used: the spec (3 px, `#0B0D10`), since phase 1 is defined there; the mockup's roads are phase 2. Easy to change if Daniel prefers the wider line.
Research: the two documents are describing different phases, not disagreeing, which is why the spec wins for now. Spec 8.3 and brief 6.3 specify phase 1: no base map, a 3 px route line on the plain `#0B0D10` background. The mockup's 8 px line on `#10151B` is drawn over roads, where a thicker line is needed to stay legible against map detail and the lighter panel makes the map read as a surface. Taking the mockup's line weight into phase 1 would give a heavy line floating on an empty background — the weight without the reason for it.
So this is a preference about phase 1 only, and it is cheap: the width and the colour are two constants in `ui/src/screens_ride.c`. If you want the wider line now, the thing to look at is the simulator's Navigation scenario at both widths, because 8 px of route on an empty background also eats the space the position marker and the turn chevron need.
Answer:

### Q23 · WP4 · open
Question: Profile (screen 12) has four steppers and "Applies on SAVE", with SAVE on the right button. With the three buttons alone, how is a stepper pressed?
Options: (a) the power button moves focus through the four steppers and then SAVE; the right button activates the focused item; (b) the right button always saves and steppers are touch-only (breaks "every control reachable by buttons").
Default used: (a). Focus starts on −5; with focus on SAVE the footer's SAVE is what the right button does. Holding a focused stepper's button repeats at 5 per second after 0.5 s, as spec 6 describes for touch.
Research: (a) is not really a choice — it follows from "every control reachable by buttons", which the spec states and which the unit's three buttons make a hard constraint. With only power, left and right, and the right button owing a SAVE, the power button is the only candidate for moving focus, so the question is really what the right button does when focus is on a stepper rather than on SAVE. (a) answers: it activates the focused item, and the footer label follows the focus, so the rider always sees what the right button will do.
What is worth checking on the real unit rather than in the simulator is the repeat: holding a focused stepper repeats at 5 per second after 0.5 s, the rate spec 6 gives for touch. Four steppers with a ±5 W step means a 40 W change is 8 presses or about 1.6 s of hold, which is fine; a larger range would want acceleration. Nothing here is expensive to change — it is the repeat constants in the Profile screen.
Answer:

### Q24 · WP4 · open
Question: The Sensors mockup shows "Add sensor" with both a power meter and a heart-rate strap paired; spec 6 says it is hidden once both are paired. Also the status bar mockups show all four icons, while spec 5 shows an icon only for each connected sensor or service.
Options: follow the mockups; follow the spec.
Default used: the spec, for both (behaviour is the spec's). The screenshots therefore differ from the mockups there.
Research: both halves are cases where the mockup shows a *populated* screen rather than a rule. "Add sensor" appears in the Sensors mockup with both sensors already paired because the mockup is showing the row style, and all four status-bar icons appear together because the mockup is showing the icon set. Spec 5 and 6 state the behaviour: the row is hidden once both are paired, and the status bar shows an icon only for a connected sensor or service. Following the spec means the screenshots differ from the mockups on those two screens, which is the only cost and is already recorded.
Worth saying plainly, because it decides it: a visible "Add sensor" with nothing left to add is a control that cannot do anything, and a status icon for a sensor that is not connected is the one case where an icon actively misleads — the rider glances at the bar to see whether the power meter is there. If you would rather the bar always showed four icons with the unconnected ones dimmed, that is a different and defensible design, but it is a spec change, not a mockup-versus-spec call, and it would want a mockup of the dimmed state.
Answer:

### Q25 · WP5 · answered
Question: Brief 7.5 takes bearings over 30 m before and after each vertex, then merges candidates within 40 m "summing the angle". Neighbouring vertices' 30 m windows overlap, so summing them counts one corner several times (a 90° corner drawn with three points would read as about 180°, a U-turn).
Options: (a) as written; (b) sum the local heading change at each vertex over a cluster of vertices less than 40 m apart, which is the true total change of direction; (c) keep the largest windowed angle in each cluster.
Default used: (b). The bands (35, 60, 120, 165 degrees) are unchanged and the turn is placed at the cluster's sharpest vertex. Tested on synthetic corners drawn with one and with several points.
Answer: **(b) stands, and the brief's wording really would have produced U-turns.** The fault is arithmetic, not interpretation: with 30 m windows before and after each vertex, two vertices 10 m apart share most of their windows, so the same change of direction is measured by both and then added. A single 90° corner drawn with three points reads as roughly 180°, which falls in brief 7.5's `>= 165°` band — a U-turn instruction at an ordinary left turn, on a route the phone drew faithfully. The denser the GPX, the worse it gets, so the bug would have been most visible on the best data.
(b) sums the heading change *at each vertex* across a cluster of vertices less than 40 m apart, which is the real total change of direction through the corner and is independent of how many points were used to draw it. The bands (35, 60, 120, 165 degrees) and the 40 m merge distance are unchanged, so nothing else in brief 7.5 moves, and the turn is placed at the cluster's sharpest vertex, which is where a rider would say the corner is. Implemented in `android/core/.../route/Route.kt` (the `clusterAngle` / `clusterAt` loop).
(c), keeping the largest single windowed angle in a cluster, was considered and rejected: it is right for a corner drawn with one point and wrong for a corner drawn with four, where no single vertex carries the whole turn and the instruction would come out as "slight left" for a right-angle bend. It fails in the opposite direction to the bug, which is not an improvement.
`RouteTest.cornersCountOnce` pins it: a 90° left drawn with three points 10 m apart gives one LEFT, not a U-turn.

### Q26 · WP6 · answered
Question: Spec 10.6 and brief 7.2 say the app "asks for its date" for a ride flagged "time unknown". A date alone does not fix the start time, and the provisional time of day (2000-01-01 plus the unit's uptime) means nothing.
Options: (a) date only, start at a fixed hour; (b) date and start time; (c) date, and take the time of day from the provisional clock.
Default used: (b). Set date opens a date picker then a time picker (local time, starting at 07:00); every FIT time is shifted by the same offset (the `timestamp`, `start_time` and `time_created` fields, as the unit does in brief 5.6) and the upload uses the corrected start for the duplicate check and the file name.
Answer: **(b) stands.** A date alone cannot place a ride in time, and the point of the question is that the alternative is not "approximate" but *meaningless*: the provisional clock starts at 2000-01-01 plus the unit's uptime, so the time of day in the file is a number of hours since the unit was switched on. (c) would therefore upload a ride that claims to have started at, say, 00:41 — and would do it confidently, with nothing on the activity to say the time was invented. (a) is the same fault with a rounder number.
So the app asks for both: `Set date` opens a date picker and then a time picker in local time, starting at 07:00 because that is when a ride is most likely to have begun, and every FIT time field is shifted by one offset — `timestamp` on every record, lap, event, session and activity, plus `start_time` and `time_created` — exactly as the unit does when it learns the time during a ride (brief 5.6). The corrected start then drives both the duplicate check's day window and the file name, so a ride dated after the fact is indistinguishable from one recorded with a known clock.
Two details worth recording. The shift is applied to the stored file, not at upload, so re-uploading does not re-shift. And the entered time is read as *local* and converted once, which is what a rider means by "I started at half past seven"; the FIT fields themselves stay UTC seconds.
`FitShiftTest.shiftsEveryTimeFieldAndKeepsTheFileValid` moves a real simunit-recorded ride and checks every time field moved by the same offset, that nothing else changed, that both CRCs are valid and that `tools/fitcheck` accepts the result; `SimunitAppFlowTest.fullFlow` drives the whole path, entering 1 November 2026 07:30 Perth time and checking the uploaded file starts there.

### Q27 · WP6 · answered
Question: Brief 7.1 lists three modules (`:core`, `:testing`, `:app`). The app's behaviour (connection and pairing, the mirror, ride sync, the upload queue, the FIT time shift, import) needs testing against simunit, and this environment cannot build `:app` (no Android SDK, and Google's Maven repository is blocked).
Options: (a) put it all in `:app`, untested here; (b) a fourth pure JVM module, `:sync`, that `:app` uses, with `:app` only adding the screens and the Android implementations of its store and connector interfaces.
Default used: (b). `:sync` depends on `:core` and `:testing` (the app needs `SocketTransport` at run time to reach simunit).
Answer: **(b) stands, and it turned out to be the reason WP6 could be verified at all.** The argument for the fourth module was testability; what it actually bought was the whole WP6 done-condition flow running against the real C core, through the same classes the Compose screens call, on a plain JVM with no Android SDK and no device — `SimunitAppFlowTest.fullFlow`, 19 tests in `:sync`. Had the behaviour sat in `:app`, none of it could have been exercised in the container where it was written, and the first opportunity to find a fault would have been an emulator on Daniel's PC.
That is not hypothetical: the flow test is what found E7 (response and event opcodes overlap, so a receiver classifying by opcode alone mistakes a ride response for an event), which no unit test of either side alone would have shown.
The split as built: `:core` holds the protocol, FIT, CW1/CR1 and the Intervals.icu client (OkHttp only); `:testing` the fixtures and `SocketTransport`; `:sync` the app's behaviour (`UnitSession`, `RideSync`, the upload queue, the FIT time shift, import, the store interfaces with in-memory versions); `:app` adds only the Compose screens and the Android implementations — Nordic BLE, Room, DataStore, Tink, WorkManager. `:app` is the only module that needs the Android SDK, and it is included in the Gradle build only when one is configured.
The cost is one more module boundary and the interfaces across it, which is the usual price and was paid once.

### Q28 · WP6 · answered
Question: The WP6 flow needs pairing "with the passkey simulated", but simunit has no BLE security (connecting counts as the bonded phone).
Options: (a) add a passkey exchange to simunit's TCP framing; (b) simulate it in the app's simulated connector, which plays the unit's part.
Default used: (b). The simulated unit's passkey is 123456; the app asks for it in its own dialog (over BLE, Android's pairing dialog asks instead), refuses a wrong one, and stores the unit only after a good one. Nothing on the unit or in the protocol changes.
Answer: **(b) stands.** The choice is between simulating the passkey where the *unit's* part is played and where the *phone's* part is played, and (b) is the only one that leaves the shipped artefacts alone: nothing in `simunit`, in `core/` or in the protocol changes, so the simulated path cannot drift from the real one by having taught the unit a second security model. `SimulatedConnector` (`android/sync/.../UnitSession.kt`) holds the passkey, defaulting to `123456`, refuses a wrong one, and stores the unit only after a good one; over real BLE the same `pair(unit, passkey)` signature is satisfied by Android's own pairing dialog instead.
(a) — adding a passkey exchange to simunit's TCP framing — would have meant inventing a handshake that the real transport does not have, in the one component whose job is to be the C core's honest stand-in. A bug in that invented handshake would look like a protocol bug.
What this deliberately does **not** test is real BLE bonding: Android's dialog, the unit's NimBLE security configuration (`CONFIG_BT_NIMBLE_SM_SC=y`, legacy pairing off) and the passkey the unit actually displays. That stays Unverified until a phone and the unit are in the same room, and it is listed as such in WP6. The simulated path tests everything above the bond: that a wrong code is refused, that nothing is stored on refusal, and that the connect sequence and mirror follow protocol 5.
`SimunitAppFlowTest.fullFlow` starts with a wrong passkey and asserts nothing was stored, then pairs with the right one.

### Q29 · WP7 · answered
Question: An ESP-IDF app normally initialises the `nvs` partition and keeps PHY calibration data, BLE bonds and core dumps in flash data partitions. On this unit those partitions belong to the stock firmware (its settings and pairing may live in `nvs`), and step 4 of the bring-up must leave the stock firmware able to run unchanged.
Options: (a) let the custom app use `nvs`, `phy_init` and `coredump`; (b) the custom app touches none of them: no `nvs_flash_init`, no PHY data storage, core dumps off, NimBLE bonds in RAM.
Default used: (b). The cost is that the phone bond is forgotten at every reboot until bonds move to the separate storage (`CC700/config/`, the same two-slot scheme as settings) during bring-up step 6. Tests in `tools/tests/test_fw_partitions.py` check the configuration.
Answer: **(b), and the built firmware now proves it rather than asserting it.** `tools/tests/test_fw_partitions.py` checks the *source* for the absence of `nvs_flash_init` and of partition and OTA writes. Now that `make fw-build` passes, the same claim can be made from the linked image, which is a much stronger statement because it covers everything ESP-IDF pulls in behind the application's back.

Read with `xtensa-esp32s3-elf-nm --defined-only` over `build/fw/cc700.elf` (8,321 defined symbols):

- **No flash write or erase entry point of any kind.** No `esp_flash_write`, `esp_flash_erase`, `spi_flash_write`, `spi_flash_erase`, `esp_partition_write`, `esp_partition_erase`, `esp_efuse_write` or `esp_efuse_burn`. The firmware is physically unable to modify flash or eFuses: the code to do it is not in the image. That covers the whole of the stock firmware's data — `nvs`, `phy_init`, `coredump`, `otadata`, the partition table and `ota_0` — in one statement, and it holds however the app is driven.
- **No NVS API.** No `nvs_flash_init`, no `nvs_open`, no `nvs::NVSPartitionManager`. Three symbols match the name "nvs" and none is an NVS call: `nvs_sec_provider_include_impl` is the linker anchor named in the link line (`-u nvs_sec_provider_include_impl`), and `ble_restore_our_sec_nvs` and `ble_restore_peer_sec_nvs` are NimBLE store helpers which, with no NVS API linked at all, cannot reach NVS. That is independent evidence that `CONFIG_BT_NIMBLE_NVS_PERSIST=n` took effect, which matters because a mistyped Kconfig name in a defaults file is ignored silently.
- **One OTA symbol, and it is a read.** `esp_ota_get_running_partition`, which is how the app knows which partition it booted from; `esp_partition`'s own code calls it. No `esp_ota_begin`, `esp_ota_write` or `esp_ota_set_boot_partition`.

So the cost named in the default is confirmed as the only cost: the phone bond lives in RAM and is forgotten at every reboot until bring-up step 6 moves bonds to `CC700/config/`. Nothing else about the stock firmware's state is reachable from this image.

Not covered by this: that the stock firmware still *runs* after step 4 writes `ota_1` and flips `otadata`. That is a hardware step and stays Unverified until bring-up; what is settled here is that the custom app, once running, cannot touch anything.

### Q30 · WP7 · answered
Question: The handover gives 8 MB of in-package PSRAM ("AP_3v3") but not its interface. ESP32-S3 parts with 8 MB in-package PSRAM are octal; the wrong mode makes PSRAM fail to start.
Options: (a) octal; (b) quad; (c) no PSRAM until confirmed.
Default used: (a) with `CONFIG_SPIRAM_IGNORE_NOTFOUND=y`, so a wrong guess boots without PSRAM rather than crashing. Settled by the eFuse fields in BRINGUP.md step 3a.
Answer: **(a) octal, and the default stands.** The eFuse summary already in the backup (`outputs/esp32s3-backup/efuse-summary.json`, read offline) gives `PSRAM_CAP = 8M`, `PSRAM_VENDOR = AP_3v3`, `PSRAM_TEMP = 85C`, and for the flash `FLASH_TYPE = 4 data lines`. Espressif's in-package PSRAM options for the ESP32-S3 are 2 MB quad (the R2/RH2 part) and 8 MB octal (ESP32-S3R8, 3.3 V; the 1.8 V version is R8V). There is no 8 MB quad in-package variant, so 8 MB fixes the bus as octal, and `AP_3v3` additionally identifies this as the 3.3 V **R8** rather than R8V — which matters because VDD_SPI is tied to 3.3 V here (`VDD_SPI_FORCE`, `VDD_SPI_XPD` and `VDD_SPI_TIEH = VDD_SPI connects to VDD3P3_RTC_IO` are all set).
One false trail worth recording so it is not re-walked: `AP_3v3` alone does **not** imply quad. AP Memory sells a 3 V octal part (APS6408L-3OBM) and a 1.8 V quad one, so the vendor/voltage field fixes the voltage and nothing else. The capacity is what settles it.
All `SPI_PAD_CONFIG_*` eFuses (including `D4`–`D7` and `DQS`) read `0x00`, i.e. default pins, which is normal for in-package memory and does not discriminate either way.
Bearing on the firmware, added 4 November 2026: this answer is now load-bearing in a second way. The decoded route and the object buffers (about 680 KB) do not fit in internal SRAM and are allocated from the heap so that ESP-IDF places them in PSRAM (brief 5.7). They are deliberately *not* placed with `EXT_RAM_BSS_ATTR`, because static `.bss` in external RAM makes PSRAM a boot requirement and would turn a wrong mode guess into a dead firmware. With allocations, `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` still means what it says: no PSRAM costs routes and workouts, through an error path the UI already has, and nothing else.
Still Unverified until bring-up: that PSRAM actually starts. `CONFIG_SPIRAM_IGNORE_NOTFOUND=y` keeps a wrong guess harmless, so nothing in `fw/` needs to change.

### Q31 · WP7 · answered
Question: The custom app in `ota_1` will be started by the stock bootloader, which stays in place. The stock app reports ESP-IDF `423e6536`, which is not in any public ESP-IDF branch or tag, so the bootloader's version is unknown, and an old bootloader may not start an IDF v5.5.6 app.
Options: (a) find the bootloader's version in the backup (BRINGUP.md step 3a, `backup_report.py`) and confirm compatibility before step 4; (b) also replace the bootloader.
Default used: (a). Replacing the bootloader is outside this plan and would need its own recovery route; nothing in the plan writes the bootloader or the partition table.
Answer: **(a), carried out, and the bootloader's version is no longer unknown.** Done offline against the backup; no device involved.
1. **The bootloader was built from the same commit as the app.** The string `423e6536` is present in the bootloader region (`0x0`–`0x8000`) of the backup, alongside `bootloader_esp32s3.c`. So bootloader and stock app came from one vendor tree, and the version WP7 searched for was in the file all along.
2. **It is a v4.4-to-v5.1 ESP-IDF.** It has no `esp_bootloader_desc_t` (that structure is IDF 5.2 and later, which is why `backup_report.py` printed `bootloader_desc_idf: None`), but its source layout — `bootloader_support/bootloader_flash/src/bootloader_flash.c`, `esp_hw_support/port/esp32s3/rtc_clk.c` — and the assert text `"LDO dbias need to modified"` are all still present in v5.5.6, so it is not an ancient tree.
3. **The direction is the supported one.** Espressif's bootloader documentation states the bootloader supports booting apps built from *newer* ESP-IDF versions; only the reverse (an app older than the bootloader) is unsupported. A v4.4–v5.1 bootloader starting a v5.5.6 app is therefore expected to work.
4. **The one documented pre-v5.1 caveat does not apply.** Bootloaders older than v5.1 do not support `CONFIG_ESP_SYSTEM_ESP32_SRAM1_REGION_AS_IRAM`; `fw/sdkconfig.defaults` does not set it, and it is an ESP32 option in any case. Checked.
Also confirmed from the same read, all as BRINGUP.md assumes: `ota_1` is completely erased (all `0xFF`), the partition table matches `fw/partitions.csv` exactly, `otadata` copy 0 is seq `0x00000001` state `0x00000002` with copy 1 erased, and the bootloader's flash mode is DIO (which `fw/sdkconfig.defaults` already matches).
Not settled: what `423e6536` *is*. It is still in no public ESP-IDF branch or tag, so it remains a vendor commit; the bound above comes from structure, not from the hash. See `docs/STOCK-FIRMWARE-FINDINGS.md`.

### Q34 · WP7 / future · open
Question: The stock firmware implements a full ANT+ stack — seven profiles under `Modules/Middlewares/MidAntDeviceManage/` (speed and cadence, heart rate, FE-C trainer, radar, tail light, shifting and **Shimano Di2**) — and an nRF co-processor is present on a UART (`mSysTimeStampFromNrf`, two configured UARTs, a `MidComm` layer with a baud-rate switch). The Di2 module already reads manufacturer and model, gear index, front and rear tooth counts, battery status and shift mode. Two written assumptions are therefore wrong: START-HERE and the handover list "the radio for ANT+" as unknown, and the Di2 enhancement in `docs/PROGRESS.md` says the unit has no identified ANT+ radio and proposes a BLE capture as the first step. Found by reading the flash backup offline (`docs/STOCK-FIRMWARE-FINDINGS.md`); no device was touched.
Options: (a) leave ANT+ out of v1 as START-HERE rule 4 says, and correct the two documents so the facts are right; (b) also bring ANT+ into scope for a later version, with Di2 gear display as the first use; (c) investigate the nRF link during bring-up (what protocol `MidComm` speaks over the UART) and decide afterwards.
Default used: (a) for now. Rule 4 puts ANT+ out of scope for v1 and rule 5 says not to reopen settled scope, so nothing in `fw/` or the app changes tonight. The documents are corrected because they state something untrue, which rule 6 does not allow to stand silently.
Research: the custom firmware cannot use any of this yet — `fw/hal_esp.c` has stub drivers only and the nRF's own firmware is not in this backup, so the UART protocol is unknown. The cheapest real step is (c), and it costs nothing extra: bring-up step 3 already probes GPIO, and the two UART pin pairs are the obvious thing to look at while there. Recorded as `IDEAS.md` I5.
Answer:

### Q32 · WP5 · open
Question: The GA 5 m LiDAR DEM returns `NoData` over the Perth Hills (Kalamunda, Lesmurdie Falls, Mundaring Weir all empty; the coastal plain answers normally), measured by the spike on 3 November 2026. The hills are where the climbs are, so a hills ride's gradients come from the Open Topo Data fallback at 30 m sampling, not from GA at 5 m.
Options: (a) keep the chain as it is (GA, then Open Topo Data, then the file) and accept 30 m in the hills; (b) make Open Topo Data the default source and drop GA; (c) find a DEM that covers the hills at a finer resolution.
Default used: (a). The chain already does the right thing and nothing is lost by trying GA first: it costs one request per cell, which is cached. Whether 30 m sampling is good enough for the climb detection in brief 5.9 is Unverified; it needs a ride over a known climb to judge, which is Daniel's call after riding.
Answer:

### Q33 · WP5/WP6 · open
Question: `simunit` does not build on Windows. It includes `<arpa/inet.h>` and `<sys/statvfs.h>`, which MinGW-w64 does not provide, so on a Windows PC the 3 Python simunit tests, the 9 JVM simunit tests and the whole WP6 manual flow (which needs `tools/android/demo_unit.sh`) cannot run. The C core itself builds and all 14 of its tests pass.
Options: (a) run simunit under WSL; (b) port it with a small socket and free-space shim (Winsock, `GetDiskFreeSpaceEx`); (c) leave it POSIX-only and document that the simunit-backed tests need Linux, macOS or WSL.
Default used: (c) for now, because nothing in the brief asks for a Windows build and the tests self-skip cleanly rather than failing. (a) is the cheap unlock on Daniel's PC: WSL is present but has no distribution installed.
Answer: **(c) stands, and (a) is now done on Daniel's PC, so nothing is blocked.** Ubuntu was installed into the existing WSL 2 (no elevation needed — only a distribution was missing) and the full gate then ran natively: `make test` green end to end, 16/16 C tests plain, 14/14 under ASan and UBSan, and 20/20 Python with no skips. The JVM suite run under WSL is 61 tests, 0 failures, **0 skipped** — the 9 simunit-backed tests that had always skipped now run and pass. `simunit` itself builds unchanged under Linux.
So the Windows port (b) buys nothing now and is not worth doing; `IDEAS.md` I1 keeps it on record if a Windows-native workflow is ever wanted. Two practical notes for anyone repeating this: the WSL virtual disk defaults to C:, which is where it filled the system drive here, and `make test-c-asan` still cannot link on Windows at all because MinGW ships no sanitizer runtime (`IDEAS.md` I2).


### Q35 · WP4/WP5/WP6 · open
Question: A route can end up with no elevation at all — the GPX carries no `<ele>` and neither elevation service answers (no network at import time, which is ordinary on a phone). `buildRoute` then produces a CR1 with `nprof = 0` and `ascent_m = 0`, and an empty climb list. Nothing in the mockups or the spec says what the screens should show for such a route, and both the app and the unit showed "0 m up, 0 climbs" — which asserts the route is flat. The import preview also said "Elevation: from file" in that case, because the code set that flag when it *fell back* to the file rather than when the file actually supplied anything, so it named a source that had given nothing.
Options: (a) show the zeros, as before; (b) show that the elevation is unknown, and keep "from file" only when the file really supplied elevations; (c) refuse the import until an elevation source can be reached.
Default used: (b). `nprof == 0` already distinguishes "no elevation known" from "flat" in the object itself (protocol 3.3), so no format change is needed — only the screens had to ask. The phone's import preview says "elevation unknown" with a warning-coloured line naming why ("… returned nothing and the file has no elevation, so this route has no profile and no climbs"), its library row says "elevation unknown", and the unit's Routes list says "no elevation" with `--` for ASCENT and CLIMBS on the route detail. (c) was rejected because the route is still useful for navigation: the line, the turns and the distance are all real, and only the climb features are missing.
Why it matters beyond tidiness: a flat-looking profile is not a cosmetic error. The Climb page, the "climb ahead" alert and `ASCENT LEFT` all come from the climb list, so a route with no elevation behaves exactly like a route with no climbs, and the rider has no way to tell which they are looking at. This is the same shape of fault as Q19 on the unit and E8 on the phone: a confident value standing in for a missing one.
`RouteTest.noElevationAnywhereIsReportedAsUnknownNotFlat` pins both halves, including that a genuinely flat route (elevation known and constant) still reads as known with an ascent of 0.
Not covered by a screenshot: no simulator scenario has a route without elevation, so the "no elevation" row has not been through `make shots` or the lint. It is not a clipping risk — "38.4 km · no elevation" is shorter than the existing worst case, "81.2 km · 1,150 m · 7 climbs" — and adding a scenario was left alone because it would change the contact sheet that is waiting for your review. Say the word and it becomes a fourth route in the Routes scenario.
Answer:

### Q36 · bring-up · answered 4 November 2026
Question: The spec, the brief and `hal/` all assume the barometer, the light sensor and the battery
gauge are devices this firmware talks to. Static analysis of the stock firmware says they are not.
Every I2C transfer in the whole vendor application goes through two wrappers at `0x42032b78` and
`0x42032bcc`, those wrappers are the only code in the image that references
`i2c_master_transmit_receive` and `i2c_master_transmit`, and all sixteen of their call sites are
inside the touch driver. **The touch controller is the only thing the ESP32 reaches over I2C.**
The on-board sensors instead sit behind the nRF co-processor the firmware calls the "SPU", reached
over UART1 (GPIO 42 TX / 41 RX) with a framed protocol of the vendor's own
(`MidCommBaudrateSwitch`, `UartDataSaveToRingBuffer`, `%s:Uart Read Data Result is MAGENE_ERROR!`,
`%s:SPUID=%d Not Match DevType=%d!`, `TimePara->mSysTimeStampFromNrf`). That also accounts for the
accelerometer and gyroscope the factory test screen shows, and for the ~13 s heartbeat the RAM probe
saw on GPIO 41.
Options: (a) write `hal_baro`, `hal_power` and the light sensor against I2C anyway and find out on
hardware; (b) decode the nRF frame protocol and implement the sensors on top of it; (c) leave the
sensors stubbed and ship v1 without barometric altitude, auto-backlight or a battery percentage.
Default used: (b), but not yet started. The I2C scan already in `hal_i2c_esp.c` stays, because it
makes this a falsifiable prediction rather than an assertion: if the only address that answers is
`0x5A`, the reading above is right; if a barometer answers as well, it is wrong and (a) is back.
(a) was rejected as exactly the kind of guess START-HERE rule 3 forbids, and (c) only after (b) has
been tried, because `total_ascent` without a barometer is the Q19 problem again in a different place.
Why it matters: this is not one driver, it is the shape of the next work package. It also means the
nRF link is on the critical path for ANT+ *and* for altitude, battery and the light sensor at once,
so it is worth doing properly rather than four times.
Answer: **Confirmed, and (b) is done as far as it can go without transmitting.** The link runs at
**115200** baud and the frame format decoded from the binary is exactly right: over 200,000 frames
across three captures, **not one CRC reject**, with every byte of the stream accounted for. The sensors
are all on this link, as predicted, and three of them are now decoded from real data
(`docs/NRF-PROTOCOL.md`): the **barometer** (temperature in hundredths of a degree, pressure in
hundredths of a pascal, reading 32.6 °C and 1016.4 hPa), the **accelerometer** (three int16 at about
1024 counts per g, which read 9, -8, 1048 with the unit flat), and a **clock** of seconds, minutes and
hours. A probable **battery voltage** around 3.56 V and an unresolved message that may be the light
sensor each need one more experiment. The I2C scan reported only 0x5A, so the falsifiable prediction
held: touch is the only I2C device.

What this changes: `hal_baro_read_pa` is implementable now, which closes the real hole behind Q19 --
`total_ascent` can come from a barometer instead of GPS elevation.

### Q38 · bring-up · answered 17 November 2026
Question: None of the message ids the vendor's dispatcher forwards to the application -- 11, 17, 34,
35, 40, 120 to 123 and 128 -- has ever appeared on the link, over 200,000 frames and more than an hour
of listening. Only ids 0 and 16 arrive unprompted. Either those ids are responses to requests the
ESP32 has to send first, or they only exist when the thing they describe does (a paired ANT+ sensor, a
button press, a charger state change). Reading them may need us to transmit, and transmitting to the
nRF is not a free action: **id 16 is also the id `SendPowerOffCmd` uses**, so a malformed frame on it
could switch the unit off mid-ride, and the four senders build their payloads from globals rather than
constants, so they cannot be replayed byte-exactly from the image alone.
Options: (a) stay receive-only and accept whatever the nRF volunteers; (b) transmit a reconstructed
`SendQuerySlaveModeCmd` (kind 1, id 16) and watch for a reply; (c) press buttons and change the charger
state during a passive capture first, and only consider transmitting if nothing new appears.
Default used: **(c)**, and no frame has been sent. The passive tests cost one window each and carry no
risk at all, and if the buttons do arrive on this link then the ids resolve themselves without us
touching the power channel. If (b) becomes necessary it needs Daniel's explicit go-ahead and a
narrower target than id 16.
Why it matters: this is the only remaining unknown on the link, and it gates the buttons, ANT+ and the
Di2 gear display. It is also the first point in this project where the safe and the fast option differ,
which is worth recording rather than deciding quietly.
Research (17 November 2026, from the vendor's handlers decompiled with Ghidra; `docs/NRF-PROTOCOL.md`,
"What the vendor's handlers say"): **the premise was partly wrong, in the direction that favours (c).**
The buttons are not among the never-seen ids. The id 16 handler turns sub-tag `0x49` into a key event
(index in payload byte 1, event code in byte 6) and passes it to `ArmSendBtnEvent`, with no request
involved, and the first long capture's byte-0 range of `49-f1` shows that sub-tag does occur. So a
passive capture with the buttons pressed should show them, and the listener now prints a `KEY` line
for each. Two statements in the question are also corrected: `SendQuerySlaveModeCmd` is kind 2 on
**id 1**, not kind 1 on id 16, and the senders' payloads are constants, so all four frames are known
byte for byte. That makes a transmit possible; it does not make it agreed. The never-seen ids (11,
17, 34, 35, 40, 120 to 123, 128) are still unexplained and are now most likely ANT+ traffic, which
only exists with a paired sensor.
Result (17 November 2026, `outputs/nrf-capture-2.txt`): **(c) settled the buttons, with nothing sent.**
Keys arrive as id 16 / `0x49`: index 0 power, 1 left, 2 right; event 1 a short press, 4 held (every
300 ms), 5 released after a hold. They are implemented as `hal_input`. The same capture showed the
nRF switching its sensors on by itself when the power button is held, and announcing it with an
`e2 … 05 …` power event, so powering **on** needs no transmit either. What still needs one is
powering **off**: the stock firmware answers the user's request with `SendPowerOffCmd`, whose frame
is now known exactly (kind 2, id 16, payload `e2 02 00 00 00 00 00 00`). That is the narrower target
this question asked for. It stays open for one decision only: whether our firmware may send that one
frame. Until it can, the unit runs until the battery is flat, which is what happened between 4 and
17 November.
Answer: **Yes: the firmware may send `SendPowerOffCmd` (Daniel, 17 November 2026).** Buttons are
confirmed on the unit, and `fw_nrf_build_power_off` builds the exact 16-byte `SendPowerOffCmd` frame
(kind 2, id 16, payload `e2 02 00 00 00 00 00 00`, CRC-16/XMODEM `0x418F` little-endian `8f 41`) for
`hal_power_off()`. Nothing else is transmitted on the nRF link.

**Correction and amendment, 17 November 2026.** Two things in the answer above did not hold.

The CRC quoted is wrong. The power-off frame is `a5 0c 6f f1 02 10 e2 02 00 00 00 00 00 00 f8 1a`
(CRC `0x1AF8`), computed independently with Python's CRC-16/XMODEM and equal to what the builder
produces, which is the frame that switched the unit off on hardware. `8f 41` was never what was sent.

"Nothing else is transmitted" stopped being true in `c57ba48`, which also sends power-on at boot with
timed retries and three GNSS power frames, without this question being reopened (found by the
independent review; see `docs/PROGRESS.md`). The vendor's own behaviour has since been read out of
the stock firmware (`docs/RE-FINDINGS-2.md`), and **Daniel approved the following on 17 November
2026**, on the recommendation that the firmware should send what the nRF was designed to receive:

| Frame | Decision |
| --- | --- |
| `SendPowerOffCmd`, repeated until power drops, after open files are closed | **Approved.** The vendor repeats it about every 1.5 s and closes files first. |
| `SendPowerOnCmd`, repeated until the nRF confirms with its power event | **Approved.** The vendor repeats it about every 100 ms. |
| `SendQuerySlaveModeCmd` (kind 2, id 1) before power-on, until answered | **Approved.** The vendor does this first; the reply says the nRF is in its application. |
| GNSS reset (`e2 02 07 00 00 02 …`) after about 5 s with no bytes from the receiver | **Approved.** The vendor's watchdog does this. |
| GNSS power-on (`e2 02 07 00 00 01 …`) at boot | **Kept for now, to be tested.** The vendor does not send it at boot. It was added to fix "no GPS" and no capture shows whether it is needed. |
| The four `$PAIR` set-up commands to the GNSS receiver itself, on GPIO 1 | **Not approved yet.** A transmit to a different chip; first capture what the receiver sends unprompted. |

Anything not in this table is not to be transmitted without asking again. In particular, if the
slave-mode reply says the nRF is in its bootloader, the vendor starts an nRF firmware upgrade; ours
must do nothing of the kind, only log it.

### Q37 · bring-up · open
Question: `fw/main/hal_touch_esp.c` reproduces the vendor's CST3240 sequence byte for byte, but
differs from it in two places. The vendor passes `-1` as the I2C timeout, i.e. wait forever; and it
sends its "data consumed" write and decodes the report whether or not the read succeeded.
Options: (a) copy the vendor exactly, including the infinite timeout; (b) bound the timeout and
refuse to decode a buffer that was never filled.
Default used: (b). Touch is polled from the one task that also runs LVGL, the core and the link, so a
wedged I2C bus would stop the whole device rather than cost a frame; `TOUCH_I2C_TIMEOUT_MS` is 50 ms.
A failed read returns -1 and leaves the last position alone. The acknowledging write is still sent
unconditionally, as the vendor does, because a write to a chip that is not answering is harmless.
Why it matters: an infinite timeout inside a UI poll is the sort of thing that works on the bench for
months and then hangs a rider's computer on a cold morning. The deviation is small, deliberate and
recorded so that if touch misbehaves this is the first thing to look at.
Answer:

### Q39 · stock app and firmware · open
Question: The vendor's phone app (OnelapFit) updates the unit's firmware from the vendor's server
(`api/v1/app/device/firmware/checkVersion` and `.../upgrade` on `api-fitness.rfsvr.com`), and the nRF
co-processor's own firmware is not in our flash backup. That image would answer the remaining link
questions from the other side (what the nRF sends for ANT+, what it accepts) without transmitting
anything to the nRF. Should we try to obtain it, and how? (`docs/STOCK-APP-FINDINGS.md`.)
Options: (a) do not; rely on passive captures and the ESP32 side of the code; (b) Daniel captures the
update download on his own phone or network, with his own account, and hands over the file; (c) call
the vendor's update endpoint directly from this project.
Default used: **(a)**. (c) is not something I will do unasked: it sends requests to the vendor's
service on the project's behalf and would very likely need an account token, which START-HERE rule 2
keeps out of my hands. (b) is clean if Daniel wants it, but it only works when the vendor offers an
update, and applying an update would also replace the stock firmware in `ota_0` that the backup
describes, so it should be captured and not installed.
Why it matters: it decides whether ANT+ and the Di2 display (Q34) can be decoded offline or need
live experiments. It is not on the path to a first recorded ride.
Answer:

### Q40 · bring-up · open
Question: The spec's holds are timed from the moment a button goes down (1 s for a bottom button,
2 s for power, with a bar showing progress). The nRF does not report the moment a button goes down.
It reports a short press as one frame when the button is let go, and a hold as a "held" frame that
first arrives some time after the press (the delay is the nRF's own long-press threshold, not
measured yet) and then repeats every 300 ms. So the unit cannot know a button is down until the nRF
has already decided it is a long press.
Options: (a) start the core's hold timer at the first "held" frame, so a hold takes the nRF's
threshold plus the spec's time, and the progress bar runs for the spec's time; (b) treat the first
"held" frame as the hold itself and fire at once, with no progress bar; (c) backdate the press by an
assumed threshold so the bar starts part-filled.
Default used: **(a)**. It invents nothing: every timestamp the core sees is real, the bar behaves as
the mockup shows, and the only cost is that holds feel longer than the spec says. (b) changes what
the screen shows and (c) needs a number nobody has measured. `CC_HOLD_BOTTOM_MS` and
`CC_HOLD_POWER_MS` are the two constants to shorten if (a) feels slow, and they are untouched.
Why it matters: every hold in the product goes through this (previous page, pause, the pause menu,
power). It also means a short press acts on release, not on press, on the real unit.
Answer:
Update 17 November 2026 (`outputs/handshake-2.txt`): (a) does not work for **power**. About 1.5 to
1.9 s after its first "held" frame the nRF cuts or restarts the ESP by itself, before the core's 2 s
ran out, so `SendPowerOffCmd` was never sent. For the power button only, the firmware now uses (c):
the press is backdated by 1.5 s (`PWR_HOLD_HEAD_START_MS` in `fw/main/main.c`), so power-off fires
0.5 s after the first held frame. The progress bar starts three-quarters full. The bottom buttons
keep (a).

### Q41 · bring-up · answered 17 November 2026
Question: The handshake (commit `e0e46b0`) sends only frames from the Q38 table, but it adds timing
and gating choices that Q38 does not specify. Which of these should stay?
1. Event 5/6 wakes the unit from power-off only if it arrives more than 500 ms after the
   requesting hold has been released. In capture 2 the event arrived in the same tick as the release.
2. The caps: the slave-mode query gives up after 10 s, power-on after 5 s, power-off stops after 30
   frames (45 s), and the GNSS gets at most 3 resets per boot.
3. If the handshake fails (no reply, or no power event) and the nRF is later seen to be on (event 5/6
   or sensor frames), it moves to RUNNING without sending anything, so the one GNSS power-on can still
   go out.
4. At power-off, a `.part` left open by a failed finish is closed, and the link's upload `.tmp` is
   aborted (`cc_link_abort`).
5. Outside power-off, a failed ride finish still leaves the writer open (this predates the review).
   Should that be fixed?
Default used: all of 1 to 4 as listed, and 5 left as it is. Each one transmits less than the vendor
does, or closes files that would otherwise be left open.
Why it matters: these decide how long the unit keeps sending to the nRF when something is wrong, and
whether a charger plug-in can restart it.
Answer: Daniel, 17 November 2026: "go with what you think". All of 1 to 4 stay as listed. For 5, the writer is left as it is for now, and is to be fixed with the store work.

### Q42 · bring-up step 6 (BLE sensors) · open
Question: The spec gives the Add sensor screen ("Scanning, then results", board 10), one sensor of
each kind, and the 10.7 backoff, but leaves these open:
1. What the unit shows between PAIR and a working sensor, and when pairing fails.
2. The order of the result rows.
3. Whether the backoff delay counts from the start or the end of a failed attempt, and what resets it.
4. What happens to reconnects while Add sensor is scanning (NimBLE runs one scan or one connect at a
   time, and refuses the other).
5. What to do if a sensor asks for link-layer pairing (encryption).
Options and defaults used (the safest readings):
1. PAIR returns to Sensors at once and the sensor shows there as "Searching…" while the unit
   connects. It is written to `config/sensors.a|b` (brief 5.9, `cc_bonds_*`) only once the measurement
   subscription succeeds. After 3 failed tries (1 s, then 2 s apart) it is dropped, not saved, and the
   log says so. No new screen or text was added (rule 4).
2. In the order first heard, not by signal: a row never moves under the rider's finger. The mockup's
   rows are not sorted by signal either. At most three rows; a device not heard for 10 s leaves.
   A kind already paired is not offered (one of each; forget to replace, spec 6 screen 9).
3. From the end of the failed attempt (each attempt waits up to 10 s for the sensor). A successful
   subscription resets it to 1 s. So a sleeping sensor is tried after 1, 2, 4, 8, 15, then every 30 s,
   plus the 10 s each try listens.
4. Reconnects wait while the scan runs, and a connect in flight is cancelled so the scan can start;
   the cancelled try does not count as a failure. Both the scan and the connect set-up listen for half
   the time (50 of 100 ms, 30 of 60 ms), so the phone's advertising keeps its slots.
5. Sensors are not bonded. A passkey request from a sensor is refused (the unit cannot type one) and
   logged. Not handled: a sensor that bonds with "Just Works" would leave a record in NimBLE's RAM store,
   which `hal_ble_bonded()` counts, and phone pairing would then be refused until reboot. No standard
   HR strap or power meter is known to require this; the log line "encryption change" would show it.
Why it matters: these decide what Daniel sees during the first pairing test; each is one constant or
a few lines in `fw/main/sensors.c` if riding says otherwise.
Answer:
Update (phone-link branch): point 5's "not handled" case is closed. `hal_ble_bonded()` now counts only
authenticated (passkey) bonds, which a Just Works sensor never is, and phone pairing no longer depends
on that count at all (Q44). A sensor's record is also never written to the eMMC (Q43).

### Q43 · phone link · open
Question: Where do the phone's bond keys live across reboots? NimBLE keeps bonds in NVS, which the
firmware never initialises because the partition belongs to the stock firmware (Q29), so until now the
phone was forgotten at every reboot and had to pair again.
Options: (a) keep bonds in RAM only (re-pair after every power-off); (b) initialise NVS after all;
(c) mirror the phone's bond to a file under `CC700/config/` on the eMMC.
Default used: (c), as Q29 already foresaw. NimBLE's RAM store stays the live store; its write and
delete callbacks are wrapped (`fw/main/link_gatt.c`) and the core task writes a snapshot to
`CC700/config/phone.a` and `phone.b`: two alternating slots with a sequence number and CRC-32, fsynced,
never renamed (protocol 7, brief 5.9), so a power cut leaves the previous record loadable. At boot the
file is loaded into the RAM store before the NimBLE host starts. What is stored is the minimum that
reconnection needs: for the one authenticated phone, its and our security records (LTK, EDIV/Rand,
the phone's IRK for its private address, key size, flags), its CCC descriptor state, and the unit's
own IRK. Nothing else: no sensor record (a Just Works bond is never authenticated, Q42 point 5), no
name. The keys are never logged; the log names the slot, the sequence number, the count and the last
two bytes of the phone's address. Forget phone on the unit deletes both files, so no key is left on
the eMMC. The record format is `fw/main/phone_bond.c` (magic `PB`, version 1), tested on the host by
`core/tests/test_fw_phone_bond.c` (round trip, every flipped byte and truncation refused, a power cut
mid-write, erase).
What this means: anyone who copies the eMMC's `CC700/config/phone.*` could decrypt a capture of that
phone's link with the unit, or impersonate the phone to the unit. That is the same exposure the stock
firmware's NVS bond store has (flash is not encrypted on this unit), and the link carries workouts,
routes and rides, not credentials (the Intervals.icu key never leaves the phone).
Answer:

### Q44 · phone link · open
Question: Protocol 2.2 accepts a new pairing "only during a 60 s pairing window ... and only when no bond
exists". On the unit, Forget phone is reachable only from the Phone screen's connected state (spec
screen 23, kebab), and a bonded phone that is away shows the "notpaired" state with PAIR (there is no
"paired, not connected" state). So a lost or replaced phone could never be forgotten, and the unit
could never pair again.
Options: (a) as written: refuse while a bond exists, and add a way to forget a phone that is not
connected (a new screen state, which rule 4 does not allow); (b) inside the window, accept the pairing
even if a bond exists, and delete the older phone's bond only once the new pairing has completed.
Default used: (b). There is still one bond; a new one needs the rider at the unit (Menu, Phone, PAIR)
and the passkey; and cancelling the window or a failed pairing leaves the old phone's bond untouched.
The window closes as soon as a pairing completes, so a second phone cannot use the rest of it. Outside
the window every pairing request is refused, as written. Log line: `pairing: bonded with ..:xx:xx ...
the older phone's bond was replaced`.
Answer:

### Q45 · phone link · open
Question: Smaller choices the protocol leaves open on a real link.
1. A central that connects and never encrypts holds the unit's one peripheral link, and the unit stops
   advertising while connected (protocol 2.3), so the real phone cannot get in.
2. Protocol 2.4 says the unit "prefers" 100 to 200 ms with latency 4 while idle and that the phone uses
   the shortest interval during a transfer, but not who asks or when.
3. On reconnection the phone may send HELLO before the unit has finished encrypting the link; RX needs
   an encrypted link and Write Without Response has no error reply, so the HELLO is lost silently and
   the 3 s timeout reports a failure.
4. Spec 10.5 says the app reconnects by itself, but not how often.
5. Protocol 1 asks for a NimBLE connection limit of at least 4; `fw/sdkconfig.defaults` has 3 (one
   phone and two sensors), which the sensors work has now run with on the unit.
Defaults used:
1. A connection that has not encrypted with a passkey bond 60 s after connecting is dropped (and
   logged), so advertising resumes. 60 s covers a first pairing typed slowly inside the window.
2. The unit asks for 100 to 200 ms, latency 4, supervision 6 s once the link has carried no request
   for 10 s; the app asks Android for high priority before its first write after 5 s of quiet.
3. The app waits up to 5 s for the unit's first notification (the unit sends EVENT_STATE as soon as
   the link is encrypted and subscribed) before HELLO, and goes ahead without it, since events are a
   convenience (protocol 4).
4. Every 20 s while a unit is paired, the user last chose Connect or Add unit, the app is running, and
   Bluetooth is on with permission. Disconnect and Forget unit stop it.
5. Kept at 3 (no change), because the steady state is one phone and two sensors and each connection
   costs internal RAM; the unit does not advertise while the phone is connected, so a fourth slot would
   only serve a stray central. Raise it if a log ever shows a connection refused for want of a slot.
Answer:

### Q43 · heart-rate graph (new screen, Daniel's request) · open
Question: On 18 November 2026 Daniel asked: "could you also add a screen when in a ride that i can tap
and i will get a time series plot of the heart rate over the last 5 minutes". The spec has no such
screen (START-HERE rule 4: no new screen not in the mockups; spec 4: touch has no job while riding).
His request authorises it. What it does, and the choices made where he did not say:
1. **Data.** The core keeps 300 one-second entries (`cc_hrhist_t`, `core/include/cc700/hrhist.h`), each
   a reading or "no reading", in `cc_view_t.hr_hist`. An entry is the same one-second value the FIT
   record and the HEART RATE field use, so the graph never disagrees with the file: an empty second
   repeats the last bin only if the last sample is under 2 s old (V§6.4, as today), otherwise it is a
   gap. 0 bpm is stored as no reading (a strap sends 0 without skin contact; it would wreck the scale).
   Readings above 254 are clamped, as in FIT.
2. **When it records.** While recording and while paused (including the pause menu and Confirm), in
   wall seconds, so the x axis is real time and a stop shows the recovery. Not before START, not after
   Finish. Reset at START.
3. **Opening and closing.** While recording, a tap on Main's HEART RATE field (120 × 53 px) opens it.
   A tap anywhere on it, or the left button (short or held), closes it back to Main; left does not
   also change the page. Right keeps its ride meaning (short lap, hold 1 s pause). Banners and the
   hold bar show over the graph as over any ride page. A pause (by hand or auto) shows Paused as
   always and closes the graph, so the resume returns to Main. The Paused screen has no heart-rate
   field, so the graph is not opened from there. With no strap paired the tap still opens it, with an
   empty state.
4. **Layout** (240 × 320, ride-page style): status bar; header "Heart rate" / "LAST 5 MIN"; the
   current bpm at 56 px (the field's value, "--" when invalid) with a NO STRAP or SEARCHING chip when
   the strap is not delivering; MIN / AVG / MAX of the window (half-up mean, "--" with no readings);
   the plot, x from −5:00 to NOW with a −2:30 mark, y on whole tens with the readings strictly inside,
   at least 40 bpm tall, grid every 10, 20, 40 or 50 bpm (`cc_hrhist_axis`); footer BACK. The line is
   white, 2 px, broken at every gap; a lone reading between gaps is a dot; the newest reading has the
   climb profile's ringed marker. Early in a ride the line starts part-way across. Empty state: "No
   heart rate", with "No strap is paired. Pair one in Menu, Sensors, when stopped." (no strap) or
   "Put the strap on to wake it. The graph fills as readings arrive." (searching or lost).
5. **No zone bands.** There are no heart-rate zones in the profile or settings (only power zones from
   FTP), so none are drawn. Adding them needs a max HR or threshold HR setting first.
6. **Not reachable without touch.** Spec 4 says every action can be reached with the buttons alone.
   The graph is the one exception: making it a page in the ring means changing the core's page ring
   (spec 5, decided provisional) and the page dots. Left as a follow-up if Daniel wants it.
7. **Cost.** The screen is rebuilt once a second like every ride page; the plot is one object with a
   draw callback over a static 300-entry table (no allocation per frame). Not measured on the unit.
Options: as built; or a page in the ring (button-reachable); or zone bands after an HR-zone setting.
Default used: as built, above. A stray tap (rain, glove) can open or close the graph; both are
harmless and undone by one more tap or the left button.
Why it matters: a new screen and new on-screen text outside the spec; Daniel reviews the screenshots
(`docs/hr-graph/`: the four scenarios and three walk shots) before it is golden.
Answer: Daniel asked for the screen (18 November 2026). Review of the details pending.

## Document errors

### E5 · brief 5.6 (finish sequence)
Problem: The finish sequence says "rename to `.fit` ..., write `.meta` ..., then remove `.part`", but renaming the `.part` already removes it.
Reading used: rename `.part` to `.fit`, then write `.meta` (via `.meta.tmp`). A power cut between the rename and the `.meta` write leaves a `.fit` without `.meta`; boot rebuilds the `.meta` from the file. Tested in the store boot path.

### E6 · brief 5.6 (closing message order)
Problem: The closing messages are listed as "final lap, session with totals, timer-stop event, activity". The usual FIT order puts the timer-stop event before the final lap, so the lap and session timer totals include the stop.
Reading used: timer-stop event (if the timer is running), final lap, session, activity. Recovery recognises a file cut anywhere in that sequence: it drops a partial session and does not write a second final lap.

### E7 · protocol 2.6 and 4 (response and event opcodes overlap)
Problem: A response's opcode is the request's opcode with bit 7 set, and events use 0xA0 and above. RIDE_LIST (0x20) therefore answers with 0xA0, the same opcode as EVENT_STATE, and RIDE_OPEN (0x21) answers with 0xA1, the same as EVENT_RIDE_SAVED; RIDE_READ, RIDE_MARK and RIDE_DELETE answer with 0xA2 to 0xA4, the range reserved for future events. A receiver that classifies by opcode alone mistakes the ride responses for events (found by the end-to-end test against simunit).
Reading used: the protocol already says events carry req_id 0 and that 0 is reserved for the phone, so req_id decides: a message with req_id 0 is an event, anything else is a response. The unit ignores a request with req_id 0 (no response; the phone's timeout covers it) so the rule always holds. The Python test client and the Kotlin client (WP5) classify this way. No opcode was changed, so no protocol version change is needed; a later protocol revision could move events to 0xE0 and above.


### E8 · ArcGIS REST documentation (GA identify attribute name)
Problem: The ArcGIS REST documentation for `identify` on an image service shows the pixel value under `attributes."Pixel Value"`, and both `GaProvider` and `tools/dem_spike/dem_spike.py` read exactly that name. The live `DEM_LiDAR_5m_2025` service returns `attributes."Stretch.Pixel Value"` instead: the renderer prefixes the field. `GaProvider` would therefore have found no value at any point and fallen silently through to Open Topo Data for every route, and the mock in `HttpTest.gaIdentify` encoded the same wrong name, so the unit tests confirmed the mistake rather than catching it. Found by running the WP5 spike against the live service on 3 November 2026 (DEM-FINDINGS.md).
Reading used: accept either name: `Pixel Value`, or any key ending in `.Pixel Value`, so the renderer prefix does not matter and a service configured without one still works. `HttpTest.gaIdentifyLiveAttributeName` pins the live shape (a value and a `NoData`) alongside the original test, which is kept because the documented plain name remains valid. No request parameter changed.

### E9 · Intervals.icu API (activity list start-time field)
Problem: `IntervalsClient.findDuplicate` reads each listed activity's `start_date`, the name the API documentation gives. Community examples of `GET /api/v1/athlete/0/activities` show `start_date_local` as well, and at least one reported response carried only the local one. The old code skipped any entry without `start_date`, so a list using the other name made the function return null — "no duplicate" — and the ride uploaded a second time with nothing to show anything had gone wrong. `Instant.parse` also rejects a zone-less local time, with the same silent result. This is the same shape of fault as E8: a documented field name that the live service does not use, confirmed by a mock that encoded the same assumption. Found by reading the public documentation; no key was used and no live call was made (rule 2).
Reading used: accept `start_date` or `start_date_local`, but compare only a value that parses as an exact instant. A zone-less local time is skipped rather than assumed to be UTC: converting it needs the athlete's time zone, and guessing one would either invent duplicates or miss real ones. If the list returns entries but none give a comparable start time, `UnreadableActivityList` is raised and the upload reports `Failed`, which the rider sees and can retry, instead of silently uploading again. Three tests in `HttpTest` pin the behaviour. This lowers the risk but does not close it: which field the endpoint really returns is still unconfirmed, so the first live upload remains the check, and the existing checklist item stands.
