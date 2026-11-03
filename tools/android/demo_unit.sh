#!/bin/sh
# Prepares a simulated unit for the app (brief WP6) and serves it on TCP port 7700 until Ctrl-C:
# one normal ride and one ride flagged "time unknown". Build simunit first with `make test-c-plain`.
#   tools/android/demo_unit.sh [ROOT]     (default ROOT /tmp/cc700-demo/CC700, a fresh copy each run)
# The app connects in Settings, Connection, Simulated unit: host 10.0.2.2 (emulator) port 7700, passkey 123456.
set -e
cd "$(dirname "$0")/../.."
SIM=build/host/simunit/simunit
ROOT=${1:-/tmp/cc700-demo/CC700}
[ -x "$SIM" ] || { echo "build simunit first: make test-c-plain" >&2; exit 1; }
[ -d "$ROOT" ] && { echo "$ROOT exists; give an empty path or remove it" >&2; exit 1; }
FX=core/tests/fixtures
"$SIM" --root "$ROOT" --port 0 --script $FX/redlight.csv --speed 0 --finish --exit --utc 1793000000 --quiet
"$SIM" --root "$ROOT" --port 0 --script $FX/redlight.csv --speed 0 --finish --exit --utc none --quiet
echo "two rides recorded in $ROOT; serving on port 7700 (Ctrl-C to stop)"
exec "$SIM" --root "$ROOT" --port 7700 --utc "$(date +%s)"
