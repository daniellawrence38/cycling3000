#!/bin/sh
# make fw-build (brief WP7): builds fw/ (app.bin only) and fw/probe (RAM image) with ESP-IDF, checks
# the pinned version, the size limit and that the build printed no warnings. Never flashes, never opens
# a serial port: there is no flash, monitor or port argument anywhere in this script.
set -e
cd "$(dirname "$0")/.."
WANT=$(cat fw/IDF_VERSION)
if ! command -v idf.py >/dev/null 2>&1; then
    echo "fw-build: idf.py not found. Install ESP-IDF $WANT and run '. \$IDF_PATH/export.sh' first." >&2
    exit 1
fi
HAVE=$(idf.py --version 2>/dev/null | sed -n 's/.*\(v[0-9][0-9.]*\).*/\1/p')
[ "$HAVE" = "$WANT" ] || { echo "fw-build: ESP-IDF $HAVE found, $WANT required (fw/IDF_VERSION)" >&2; exit 1; }

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
