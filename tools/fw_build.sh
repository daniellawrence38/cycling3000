#!/bin/sh
# make fw-build (brief WP7): builds fw/ (app.bin only) and fw/probe (RAM image) with ESP-IDF, checks
# the pinned version, the size limit and that the build printed no warnings. Never flashes, never opens
# a serial port: there is no flash, monitor or port argument anywhere in this script.
set -e
# Both builds below pipe into tee, so the pipeline's status is tee's, not idf.py's: without pipefail
# a failed build is masked and the script runs on to report a missing binary instead of the real
# error. `set -o pipefail` is not in POSIX sh, so ask for it only where the shell has it (dash and
# bash both do; the fallback keeps the script working anywhere else).
(set -o pipefail 2>/dev/null) && set -o pipefail
cd "$(dirname "$0")/.."
WANT=$(cat fw/IDF_VERSION)
if ! command -v idf.py >/dev/null 2>&1; then
    echo "fw-build: idf.py not found. Install ESP-IDF $WANT and run '. \$IDF_PATH/export.sh' first." >&2
    exit 1
fi
HAVE=$(idf.py --version 2>/dev/null | sed -n 's/.*\(v[0-9][0-9.]*\).*/\1/p')
[ "$HAVE" = "$WANT" ] || { echo "fw-build: ESP-IDF $HAVE found, $WANT required (fw/IDF_VERSION)" >&2; exit 1; }


# A clone of ESP-IDF made with `--depth 1 --recursive` can leave submodules registered with an empty
# working tree (`unity`, `spiffs` and `protobuf-c` did so here, 3 of 23). The build then fails deep in
# CMake with "include directory ... is not a directory", a long way from the cause, and
# `git submodule foreach` gives a false all-clear because it skips uninitialised ones. `submodule
# status` marks them with a leading `-`, so say it plainly instead (IDEAS.md I3).
#
# Two deliberate details. The status is captured with `|| :` because `submodule status --recursive`
# exits 128 if it cannot read any nested submodule, and under `set -e` with pipefail that would kill
# the build over a submodule this firmware does not use (one such index is damaged on this PC).
# And a failure to inspect is reported rather than swallowed: an unreadable list is not an all-clear,
# which is the same trap `submodule foreach` falls into.
if [ -n "${IDF_PATH:-}" ] && [ -d "$IDF_PATH/.git" ]; then
    SUBS=$(git -C "$IDF_PATH" submodule status --recursive 2>/tmp/cc700-subs-err || :)
    MISSING=$(printf '%s\n' "$SUBS" | sed -n 's/^-[0-9a-f]* \([^ ]*\).*/\1/p')
    if [ -n "$MISSING" ]; then
        echo "fw-build: these ESP-IDF submodules are registered but not checked out:" >&2
        echo "$MISSING" | sed 's/^/  /' >&2
        echo "fw-build: run  git -C \"\$IDF_PATH\" submodule update --init --recursive" >&2
        exit 1
    fi
    if [ -s /tmp/cc700-subs-err ]; then
        echo "fw-build: note, git could not inspect every ESP-IDF submodule, so the check above is" >&2
        echo "          incomplete. If the build fails on a missing include directory, this is why:" >&2
        sed 's/^/          /' /tmp/cc700-subs-err >&2
    fi
    rm -f /tmp/cc700-subs-err
fi

# Every target-dependent warning in our own sources in about ten seconds, before spending forty
# minutes to find the first one (IDEAS.md I7, I11). Only possible once the build has been configured
# at least once, so a missing compile database is not an error here.
if [ -f build/fw/compile_commands.json ]; then
    "${PYTHON:-python3}" tools/fw_check.py || exit 1
fi

mkdir -p build/fw-logs
# `app` builds the application only: no bootloader, and the partition table is never written anywhere.
idf.py -C fw -B build/fw app 2>&1 | tee build/fw-logs/app.log
idf.py -C fw/probe -B build/fw-probe build 2>&1 | tee build/fw-logs/probe.log

if grep -n "warning:" build/fw-logs/app.log build/fw-logs/probe.log; then
    echo "fw-build: the build printed warnings (brief WP7 requires none)" >&2
    exit 1
fi
SIZE=$(wc -c < build/fw/cc700.bin)
LIMIT=8192000
echo "app.bin: $SIZE bytes of $LIMIT ($((SIZE * 100 / LIMIT)) %)"
[ "$SIZE" -le "$LIMIT" ] || { echo "fw-build: app.bin is larger than the ota_1 partition" >&2; exit 1; }
cp build/fw/cc700.bin build/fw/app.bin
echo "fw-build: build/fw/app.bin and build/fw-probe/cc700_probe.bin built. Nothing was flashed."
