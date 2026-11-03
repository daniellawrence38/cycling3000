# CC700 questions and document errors

Kept by the implementing model. Daniel and his assistant answer by adding a line `Answer:` under the question or by saying so in chat. A question never blocks work: use the default and carry on.

Format:

```
### Q<n> · <work package> · <open | answered>
Question: what is unclear or missing.
Options: the realistic choices.
Default used: what you did and why.
Answer: (filled in by Daniel or his assistant)
```

Doc errors use the same format with a title `### E<n> · <document and section>` and the line `Reading used:` in place of `Default used:`.

## Open questions

None yet. Number new questions from Q14 on.

## Answered questions

These came from the design review of 3 November 2026 (Daniel approved the fixes). They are already applied in spec 0.5, brief 1.1, protocol 1.1, vectors 1.1 and the coach instructions; they are recorded here so the reasons are not lost.

### Q1 · design review · answered
Question: Auto-pause uses GNSS speed only. Indoors on a trainer, a weak fix near a window reports about 0 m/s, so the ride pauses after 5 s and freezes the workout clock.
Options: (a) leave it and tell Daniel to turn auto-pause off indoors; (b) add an Indoor ride mode; (c) also require that the rider is not pedalling.
Default used: (c).
Answer: (c). Pause only when speed is below 0.7 m/s for 5 s and cadence is 0 or invalid and 3 s power is 0 or invalid. Spec 10.3, brief 5.5, WP2 indoor and red-light fixtures.

### Q2 · design review · answered
Question: Where are workouts, routes, rides and settings stored on the unit? The spare 256 KB of flash cannot hold rides (a three-hour ride is about 300 to 400 KB).
Options: spare flash for settings and the separate storage for the rest; everything on the separate storage.
Default used: everything on the separate storage.
Answer: The separate storage, in a `CC700/` folder at its root; nothing outside it is ever written. Writing there makes backing up the separate storage a hard prerequisite. Spec 3, 7.2, 13.1 item 4; brief 4; protocol 7.

### Q3 · design review · answered
Question: Retention deleted the oldest ride when none had been uploaded, the one silent data-loss path.
Options: raise the cap; refuse to record; keep un-uploaded rides above the cap.
Default used: keep them.
Answer: Never delete an un-uploaded ride while 240 or fewer rides are stored (240 is the most RIDE_LIST can report in one 4096-byte message). The History count turns yellow above 100. Spec 10.4, brief 5.6, protocol 4.

### Q4 · design review · answered
Question: Main had no ride distance, and the page ring stepped through Empty screens when nothing was loaded.
Options: change the Main grid; skip empty pages; make the ring configurable.
Default used: 2 × 3 grid on Main (heart rate, cadence, speed, distance, elapsed, lap time); leave Workout out of the ring with no workout and Navigation out with no route.
Answer: As the default, both provisional. Board 1 must be updated in `gen.py` before WP4. Spec 5, 13.1 item 15, 13.2; brief 6.1, WP4.

### Q5 · design review · answered
Question: A ride started without UTC (no fix, no phone) had a counter for its ID and wrong FIT timestamps, which breaks the Intervals.icu duplicate check and dates the ride wrongly.
Options: refuse START until UTC is known; correct the file afterwards; let the app fix it.
Default used: correct at finish when UTC arrives mid-ride; otherwise flag the ride "time unknown" and have the app ask for the date.
Answer: As the default. Spec 10.6; brief 5.6; protocol 4 (RIDE_LIST flags bit 2), 5.

### Q6 · design review · answered
Question: Ride sync with 512-byte chunks and one message in flight takes about 700 round trips for a three-hour ride.
Options: keep 512; raise to near the 4096-byte message limit; negotiate.
Default used: chunks of up to 4000 bytes, used by the phone when the MTU is 185 or more.
Answer: As the default. Protocol 4, 6; brief 5.10, 7.1; V§5.

### Q7 · design review · answered
Question: Each one-second power bin took the last sample in that second, which discards samples from meters that send more than once a second.
Options: last sample; mean of the second's samples.
Default used: half-up mean of the second's samples, falling back to the 2 s stale rule for an empty second.
Answer: As the default. Brief 5.1; V§6.4; `ref.bin_mean`.

### Q8 · design review · answered
Question: Is Intervals.icu API upload available on the free plan?
Options: free; paid only.
Default used: free.
Answer: Intervals.icu is free and every user can call the API with a personal key. Only Daniel's first live upload is unchecked. Intervals.icu also has a workout builder and its API can download planned workouts, so it is noted as a possible second workout source after v1 (not in scope). Spec 2, 9, 13.1 item 5; brief 7.6.

### Q9 · design review · answered
Question: The bring-up plan wrote to flash for its first live hardware probe, and did not address the stock firmware's USB stack.
Options: as before; probe from RAM first; keep USB on the built-in USB Serial/JTAG controller.
Default used: both additions.
Answer: Probes run from RAM only (`esptool load-ram`, ESP-IDF RAM app build type, if the pinned IDF supports it on the ESP32-S3); custom images never enable TinyUSB or move the USB pins, so automatic reset into download mode keeps working; BOOT/RESET entry is tested independently of firmware. Brief 9, WP7; spec 13.1 item 14.

### Q10 · design review · answered
Question: Calibration "unsupported" was decided from whether the Control Point characteristic exists.
Options: characteristic presence; the meter's Cycling Power Feature flags.
Default used: feature flags plus characteristic presence.
Answer: As the default; opcode, response and feature bit to be checked against the specification in WP7. Spec 6, 13.1 item 1; brief 5.11.

### Q11 · design review · answered
Question: The coach document did not state the limits the app enforces (ASCII name, 1 to 300 %, 12 h per step, 24 h total), so the coach could write files the app rejects; and its example's single 55 % recovery straddles Z1 and Z2.
Options: state the limits; change the example.
Default used: both.
Answer: The coach document now states every limit, assigns 55 % to Z1, says descending ramps are allowed, advises ranges for recovery, and its example's Recover step is [45, 55]. W1 in the vectors follows it, so W1's object, ID (`1812240e1809c011`), CRC-32 (`09201A8E`) and the framing bytes in V§5 changed. W2 and the route object are unchanged.

### Q12 · design review · answered
Question: The gradient's 0.25 altitude filter lags about 3 s while distance from the 3 s speed mean lags about 1.5 s.
Options: retune now; watch on real rides.
Default used: watch.
Answer: Watch item only; record it in PROGRESS.md if the WP2 fixtures show overshoot. Brief 5.3.

### Q13 · design review · answered
Question: In short-interval sessions (the default VO2max 30-15 workout), the next-step banner fired in the last 5 s of almost every step, and the 3 s power average lagged each 15 s or 30 s step, so the pill showed TOO LOW at the start of every effort and TOO HIGH at the start of every recovery. The same lag gave a false TOO LOW for a few seconds at the start of any hard step entered from a recovery.
Options: leave it; suppress cues on short steps; add a settling period at every step start; both.
Default used: both.
Answer: Both, provisional. No next-step banner and no pill on steps shorter than 60 s; on longer steps the pill resets at the step start, is hidden for the first 5 s, and its 2 s count starts from there. Constants `SHORT_STEP_S` = 60 and `PILL_SETTLE_S` = 5. Spec 7.6, 10.2, 13.2; brief 5.4, 5.8, WP1; V§7.1; `ref.pill_step`.

## Document errors

### E1 · test vectors 2.5, brief 5.4 · answered
Problem: The ramp formula uses Python floor division. C and Kotlin `/` truncate toward zero, so a straightforward port of a descending ramp fails two rows of V§2.5 (cool-down at 300 s gives 476 instead of 475; at 599 s, 401 instead of 400).
Reading used: floor division is meant everywhere.
Answer: Fixed. Brief 5.0 requires `cc_floordiv` and `Math.floorDiv`; V§2.5 has a floor-division check table; `ref.ramp_p10` says so; WP1 and WP5 must pass it.

### E2 · protocol 3.2 against test vectors 4 · answered
Problem: The phone accepts a single target up to 300.0 %, which widens to `p1` = 3150, but the unit accepted `p0` and `p1` only up to 3000 and would return BAD_OBJECT for any single target above 285.7 %.
Reading used: the unit bound is too tight.
Answer: Fixed. The unit accepts 10 to 3150 (protocol 3.2); V§2.4 has a 300 % row and V§4.1 a 300 % accept case.

### E3 · spec 4 and 6 (pause menu, Confirm) · answered
Problem: While paused, a short power press was ignored, but it is the only button that moves focus, so Finish and save, Discard and the destructive Confirm button could be reached only by touch.
Reading used: every action must be reachable with buttons, gloves on.
Answer: Fixed. While paused, a short power press moves focus on the pause menu and Confirm; BACK always means KEEP on Confirm; the right button activates the focused item. Spec 4, 6; brief 5.5, 6.4 (plus a lint rule that every interactive object is in a focus order).

### E4 · brief 5.9, protocol 7 · answered
Problem: The stock storage is very likely FAT (FatFS strings, `/sdcard` paths), where rename fails if the destination exists. Rewriting `settings.bin` "atomically" by rename therefore meant delete then rename, and a power cut in between lost the settings.
Reading used: design for FAT.
Answer: Fixed. Settings and sensor bonds use two alternating slots with a sequence number and CRC, never renamed; new objects are renamed only to names that do not exist yet; the simulator's `hal_fs` refuses a rename onto an existing file so the mistake shows up in host tests. Brief 4, 5.9, WP2, WP3; protocol 7.
