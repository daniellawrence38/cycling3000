#!/usr/bin/env bash
# rea.sh: run REA (github.com/morluto/rea) with this project's fixed, offline configuration.
#
#   tools/re/rea.sh decompile /opt/re/work/stock-ota0.elf 0x4222137c --provider ghidra
#
# Files only. REA also has commands that run or drive live targets (adb, gdb, process and browser
# capture); none of them is to be pointed at the unit or its serial port (START-HERE rule 1), and this
# wrapper refuses the ones that could be.
set -euo pipefail
RE=/opt/re
case "${1:-}" in
    capture-*|debug-with-*|collect-adb-*|inspect-adb-*|cutter-command|setup|update)
        echo "rea.sh: '$1' is not used in this project (live targets, or changes to the Claude configuration)" >&2
        exit 2 ;;
esac
export PATH="$RE/node/bin:$PATH"
export JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64
export GHIDRA_INSTALL_DIR="$RE/ghidra"
# Keep the analysed Ghidra program between runs: the first import and auto-analysis of the 7 MB
# image is slow, and every later query should reuse it.
export REA_GHIDRA_CHECKPOINT_DIR="${REA_GHIDRA_CHECKPOINT_DIR:-$RE/work/ghidra-checkpoints}"
export REA_GHIDRA_STARTUP_TIMEOUT_MS="${REA_GHIDRA_STARTUP_TIMEOUT_MS:-3600000}"
mkdir -p "$REA_GHIDRA_CHECKPOINT_DIR"
exec node "$RE/rea/node_modules/rea-agents/scripts/rea.mjs" "$@"
