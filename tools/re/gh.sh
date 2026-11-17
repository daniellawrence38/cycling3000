#!/usr/bin/env bash
# gh.sh: run one of tools/re/ghidra/*.java against the analysed stock firmware, read-only.
#
#   tools/re/gh.sh DecompileAt.java 0x4222137c
#   tools/re/gh.sh StringXrefs.java "SendPowerOffCmd"
#
# The Ghidra project is built once by:
#   python3 tools/re/esp_image_to_elf.py outputs/esp32s3-backup/original-flash-16MB.bin \
#       --offset 0x20000 --out /opt/re/work/stock-ota0.elf \
#       --rom-ld /opt/esp/esp-idf/components/esp_rom/esp32s3/ld --verify
#   JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64 /opt/re/ghidra/support/analyzeHeadless \
#       /opt/re/work/ghidra-proj cc700 -import /opt/re/work/stock-ota0.elf
#
# Files only: this reads the flash backup's ELF. It never talks to the unit (START-HERE rule 1).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
script="$1"
shift
export JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64
log="$(mktemp)"
if ! /opt/re/ghidra/support/analyzeHeadless /opt/re/work/ghidra-proj cc700 \
        -process stock-ota0.elf -noanalysis -readOnly \
        -scriptPath "$HERE/ghidra" -postScript "$script" "$@" >"$log" 2>&1; then
    cat "$log" >&2
    rm -f "$log"
    exit 1
fi
# The scripts print their results between BEGIN and END markers; everything else is Ghidra's logging.
if grep -q '^===== BEGIN' "$log"; then
    sed -n '/^===== BEGIN/,/^===== END/p' "$log"
else
    cat "$log" >&2
    rm -f "$log"
    exit 1
fi
rm -f "$log"
