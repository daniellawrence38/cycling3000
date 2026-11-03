# CC700 Pro custom ride software

Before doing anything else in a session, read `claude/cc700-START-HERE.md`, then the documents in its reading order. Those documents are the specification. This file only says where things are and how to resume.

## Where things are

- `claude/`: the design documents (spec, engineering brief, protocol, test vectors, coach format, questions log), the reference implementation that produced the vectors (`claude/tools/reference/`), the mockup tools (`claude/tools/design/`) and six starter workouts (`claude/workouts/`). Treat all of `claude/` as read-only input. If a document is wrong or contradictory, follow START-HERE rule 6: choose the safest reading, log it in `docs/QUESTIONS.md`, and carry on. Do not edit the document.
- `GEOID-CC700-Pro-complete-handover.md`: hardware facts and the recovery gate. It governs any hardware question.
- Build the repository layout from engineering brief section 2 at the root of this repository (`core/`, `hal/`, `ui/`, `simunit/`, `fw/`, `android/`, `tools/`, `docs/`, `Makefile`). In WP0, copy `claude/tools/reference/` and `claude/tools/design/` into `tools/`; leave the originals in place. `make vectors` must reproduce `claude/cc700-test-vectors.md` exactly.
- The starter workouts in `claude/workouts/` are valid v1 files; use them as extra fixtures alongside W1 and W2.

## Rules that are easy to break

- Never connect to, read, flash, reset or open the serial port of the GEOID unit or any attached USB device, and never run `esptool`, `openocd`, `dfu-util` or a serial monitor against hardware (START-HERE rule 1). If a device appears attached, ignore it.
- Never ask for, store, log or write an API key, token or password (rule 2).
- Every `//` in the vectors and `ref.py` is floor division; use `cc_floordiv` in C and `Math.floorDiv` in Kotlin (brief 5.0).

## Resuming

- Work packages run in order (brief section 8). Stop and report to Daniel after the WP4 contact sheet and at the end of WP7.
- At the end of each work package, update `docs/PROGRESS.md` and `docs/QUESTIONS.md` and commit. A new session reads START-HERE, then `docs/PROGRESS.md` and `docs/QUESTIONS.md`, and resumes where the last one stopped.
- `claude/cc700-questions.md` already holds Q1 to Q13 and E1 to E4 from the design review. Number new questions in `docs/QUESTIONS.md` from Q14 and new doc errors from E5.
- This repository has no access to the claude.ai Project. Where START-HERE says to mirror files to the Project, skip it; Daniel copies `docs/PROGRESS.md` and `docs/QUESTIONS.md` back himself.
