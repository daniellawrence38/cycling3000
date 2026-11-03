# CC700 default workouts

Six starter workouts in the v1 format (`claude/workout-coach-instructions.md`), 3 November 2026. Each file was checked with `tools/reference/ref.py` (validation, conversion and CW1 encoding). Watts below are at the unit's default FTP of 200 W; they scale with whatever FTP is set on the unit.

| File | Name | Time | Steps | Main set | Purpose |
| --- | --- | --- | --- | --- | --- |
| `endurance-90.workout.json` | Endurance 90 | 1:30 | 10 | 4 × 16.5 min at 65–72 % with a 1 min 105 rpm spin-up after each | Aerobic base, just below the first threshold |
| `sweet-spot-3x12.workout.json` | Sweet spot 3 x 12 | 1:06 | 8 | 3 × 12 min at 86–90 %, 4 min recovery | High aerobic load that is still repeatable |
| `threshold-2x20.workout.json` | Threshold 2 x 20 | 1:15 | 8 | 2 × 20 min at 95–100 %, 5 min recovery | Raise FTP and time at FTP |
| `vo2max-30-15.workout.json` | VO2max 30-15 3 x 13 | 1:03 | 84 | 3 sets of 13 × (30 s at 112–120 %, 15 s at 45–55 %), 3 min between sets | VO2max and power at threshold (Rønnestad short intervals) |
| `vo2max-5x4-fast-start.workout.json` | VO2max 5 x 4 fast start | 1:03 | 19 | 5 × (1 min at 121–130 % then 3 min at 106–112 %), 3 min recovery | VO2max, long-interval format with a fast start |
| `ramp-test.workout.json` | Ramp test to failure | 0:41 max | 24 | 1 min steps from 50 % rising 6 % a minute to 170 % | Set FTP: 75 % of the best 1 min power |

## Why these six

The current evidence does not crown one intensity distribution. A 2025 review in Frontiers in Physiology compares pyramidal, threshold and polarised models and concludes each suits different phases and athletes; gains come from matching the model to the training phase and the person. Pyramidal training (lots of easy riding, less tempo and threshold, a little very hard work) has improved power at 4 mmol/L in recreational cyclists over 16 weeks. So the set covers each band: one easy session, two threshold-side sessions and two VO2max sessions, plus a test so the targets are right.

- **Endurance** sits at 65–72 % FTP. A 2025 expert panel (14 sport scientists and coaches, IJSPP) placed zone 2 immediately below the first lactate or ventilatory threshold, and said it can be continuous or broken up. The spin-ups are only there to break up the ride and keep pedalling smooth.
- **Sweet spot** is kept at 86–90 %, entirely in Z3, so the unit's target-zone chip and actual-zone ring agree while you are on target (88–94 % would straddle Z3 and Z4 on the display).
- **Threshold 2 x 20** is the classic format, 95–100 %, entirely in Z4.
- **VO2max 30-15** follows Rønnestad's short intervals (30 s on, 15 s off, in sets). In a 2021 effort-matched study in trained cyclists, short intervals improved VO2max by 5.7 % more and power at 4 mmol/L by 3.8 % more than 5 min intervals. The study used about 86–88 % of the ramp-test peak power, which is roughly 112–120 % of FTP for a typical rider. Three sets of 13 is the common version for non-elite riders; the study's elite group did five sets of 12.
- **VO2max 5 x 4 fast start** gives a long-interval option. Starting each interval harder has been reported to raise oxygen uptake sooner, so more of the interval is spent near VO2max. A 2024 network meta-analysis of trained cyclists found no clear winner by interval length, so alternating the two VO2max formats is reasonable.
- **Ramp test** gives an FTP to set on the unit. FTP is commonly taken as 75 % of the best 1 min power in the test. Ramp tests can overestimate FTP for riders with a strong anaerobic finish; if Threshold 2 x 20 feels impossible at the new value, take 2 to 3 % off.

## How to use them

- Most of the week should be easy riding. One or two hard sessions a week is plenty: pick one from Sweet spot or Threshold and one VO2max format, and leave at least a day between hard days.
- Do the ramp test first, then set FTP in Profile on the unit; every other workout scales from it.
- **Ramp test:** ride each step until you cannot hold it, then stop and pause; the remaining steps and the cool-down are not meant to be reached. Read your best 1 min power from the ride in Intervals.icu. It works best indoors or on a long steady climb, since the steps need steady power.
- **VO2max 30-15 on the unit:** the 30 s and 15 s steps show no status pill and no next-step banner (steps under 60 s, spec 7.6 and 10.2), because the 3 s power average cannot settle inside them. Ride the efforts by the step timer and feel; power and the target range still show. The banner still announces each set at the end of the 3 min recovery. On every other step the pill appears 5 s after the step starts.

## Sources

- Sun et al. (2025), Recent advances in training intensity distribution theory for cyclic endurance sports, Frontiers in Physiology. https://www.frontiersin.org/journals/physiology/articles/10.3389/fphys.2025.1657892/pdf
- Magalhães et al. (2024), Effects of a 16-week training program with a pyramidal intensity distribution on recreational male cyclists, Sports 12(1):17. https://www2.mdpi.com/2075-4663/12/1/17
- What is "Zone 2 Training"? Experts' viewpoint on definition, training methods, and expected adaptations (2025), IJSPP. https://zaguan.unizar.es/record/152001
- Rønnestad et al. (2021), Superior physiological adaptations after a microcycle of short intervals versus long intervals in cyclists. https://e-space.mmu.ac.uk/630338/
- Norte, Steele and Wright (2024), Physiological and performance adaptations to interval training in endurance-trained cyclists: an exploratory systematic review and meta-analysis. https://researchonline.ljmu.ac.uk/id/eprint/24748/
- Knowledge is Watt, notes on long intervals and fast starts. https://knowledgeiswatt.substack.com/p/42-hit-long-intervals-4x8-better
- TrainerRoad, How to test with the Ramp Test (75 % of best 1 min power). https://support.trainerroad.com/hc/en-us/articles/360003910912-How-to-Test-With-the-Ramp-Test
