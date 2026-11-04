#!/usr/bin/env python3
"""make fw-check: recompile only this project's own sources with the xtensa compiler, syntax only.

Why this exists (IDEAS.md I7). `uint32_t` is `unsigned int` on an x86-64 host but `long unsigned int`
on xtensa, so `printf("%u", some_uint32)` compiles clean in every host test and then fails the
firmware's -Werror=format gate. The same goes for any other target-dependent warning. A full
`make fw-build` takes about forty minutes and stops at the first file, so the class surfaces one file
per rebuild; this target compiles all of them in a few seconds and reports every one at once.

It reuses the exact flags the real build recorded in build/fw/compile_commands.json, including the
absolute path of the cross compiler, so it needs no ESP-IDF environment of its own: only a build
directory that has been configured once. Nothing is linked, nothing is written to flash, and no
serial port is opened (START-HERE rule 1).
"""
import json
import os
import shlex
import subprocess
import sys

# The project's own code. ESP-IDF's and LVGL's sources are not ours to fix and are not checked.
OURS = ("/core/src/", "/ui/src/", "/hal/", "/fw/main/", "/fw/probe/main/", "/simunit/")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def entries(db):
    with open(db) as f:
        cc = json.load(f)
    out = []
    for e in cc:
        f = e["file"].replace("\\", "/")
        if any(w in f for w in OURS) and "/managed_components/" not in f and "esp-idf/components" not in f:
            out.append(e)
    return out


def check(e):
    """Run one recorded compile with -fsyntax-only. Returns (ok, output)."""
    args = [a for a in shlex.split(e.get("command") or " ".join(e["arguments"]))]
    cleaned, skip = [], False
    for a in args:
        if skip:                      # the argument of -o
            skip = False
            continue
        if a == "-o":
            skip = True
            continue
        if a == "-c":                 # no object file: syntax and warnings only
            continue
        cleaned.append(a)
    cleaned.append("-fsyntax-only")
    r = subprocess.run(cleaned, cwd=e["directory"], capture_output=True, text=True)
    return (r.returncode == 0 and "warning:" not in r.stderr), r.stderr


def main():
    dbs = [os.path.join(ROOT, "build", d, "compile_commands.json") for d in ("fw", "fw-probe")]
    dbs = [d for d in dbs if os.path.exists(d)]
    if not dbs:
        print("fw-check: build/fw/compile_commands.json not found. Configure the firmware build once\n"
              "          first: . $IDF_PATH/export.sh && idf.py -C fw -B build/fw reconfigure", file=sys.stderr)
        return 2
    seen, bad, n = set(), 0, 0
    for db in dbs:
        for e in entries(db):
            if e["file"] in seen:
                continue
            seen.add(e["file"])
            n += 1
            ok, out = check(e)
            if not ok:
                bad += 1
                sys.stdout.write(out)
                sys.stdout.flush()
    if bad:
        print(f"fw-check: {bad} of {n} project source(s) warn or fail to compile for xtensa", file=sys.stderr)
        return 1
    print(f"fw-check: {n} project sources compile clean for xtensa with no warnings")
    return 0


if __name__ == "__main__":
    sys.exit(main())
