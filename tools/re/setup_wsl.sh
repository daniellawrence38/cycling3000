#!/usr/bin/env bash
# setup_wsl.sh: install the offline reverse-engineering tools under /opt/re (WSL or Linux, as root).
#
# Everything here works on FILES ONLY: the flash backup in outputs/esp32s3-backup/ and a vendor APK.
# Nothing in this directory talks to the unit, a serial port or USB (START-HERE rule 1).
#
# Installs, each pinned and checksum-verified:
#   - Node.js 24.22.3       /opt/re/node     (REA needs ^22.23.4 || ^24.22.1; the system node is older)
#   - OpenJDK 25            apt              (REA requires JDK 25+ for Ghidra 12.2; JDK 17 stays default)
#   - Ghidra 12.2           /opt/re/ghidra
#   - rea-agents 6.9.0      /opt/re/rea      (github.com/morluto/rea, MIT; installed with --ignore-scripts)
#
# It does NOT run `rea setup`, which edits the user's Claude configuration. That is a separate,
# deliberate step (see tools/re/README.md).
set -euo pipefail

RE=/opt/re
NODE_V=v24.22.3
NODE_SHA=4f2b175a6d53a5c617a80dba2676238a0688cef28c6da4eba35c7d844e66fcfb
GHIDRA_URL=https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip
GHIDRA_SHA=2c2cd776db3c1f6a097ba418922378ac3649cadd5e844a6f5f088ae628a7be67
REA_V=6.9.0
REA_INTEGRITY='sha512-KqDn1UR5BMqHoQ3MGAnPuWiSKwgdFaz1L/uUNgrKt+1W8kPtgYP8AWEOXIF1lDIOBVyPuYIIst2iamo1Wf3r8g=='

mkdir -p "$RE/dl"
cd "$RE/dl"

echo "== JDK 25 and unzip"
if ! [ -x /usr/lib/jvm/java-25-openjdk-amd64/bin/java ]; then
    DEBIAN_FRONTEND=noninteractive apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq openjdk-25-jdk-headless unzip xz-utils
fi
# Installing JDK 21 must not change which java the Android build picks up.
update-alternatives --list java 2>/dev/null | grep -q java-17 &&
    update-alternatives --set java "$(update-alternatives --list java | grep java-17 | head -1)" || true

echo "== Node $NODE_V"
if ! [ -x "$RE/node/bin/node" ]; then
    curl -fsSLO "https://nodejs.org/dist/$NODE_V/node-$NODE_V-linux-x64.tar.xz"
    echo "$NODE_SHA  node-$NODE_V-linux-x64.tar.xz" | sha256sum -c -
    mkdir -p "$RE/node"
    tar -xJf "node-$NODE_V-linux-x64.tar.xz" -C "$RE/node" --strip-components=1
    rm -f "node-$NODE_V-linux-x64.tar.xz"
fi

echo "== Ghidra"
if ! [ -x "$RE/ghidra/support/analyzeHeadless" ]; then
    curl -fsSL -o ghidra.zip "$GHIDRA_URL"
    echo "$GHIDRA_SHA  ghidra.zip" | sha256sum -c -
    rm -rf "$RE/ghidra.tmp"
    unzip -q ghidra.zip -d "$RE/ghidra.tmp"
    mv "$RE"/ghidra.tmp/ghidra_* "$RE/ghidra"
    rmdir "$RE/ghidra.tmp"
    rm -f ghidra.zip
fi

echo "== rea-agents $REA_V"
if ! [ -f "$RE/rea/node_modules/rea-agents/package.json" ]; then
    got=$(PATH="$RE/node/bin:$PATH" npm view "rea-agents@$REA_V" dist.integrity)
    [ "$got" = "$REA_INTEGRITY" ] || { echo "rea-agents integrity changed: $got" >&2; exit 1; }
    mkdir -p "$RE/rea"
    PATH="$RE/node/bin:$PATH" npm install --prefix "$RE/rea" --ignore-scripts --no-audit --no-fund \
        "rea-agents@$REA_V"
fi

echo "== done"
"$RE/node/bin/node" --version
/usr/lib/jvm/java-25-openjdk-amd64/bin/java -version 2>&1 | head -1
ls "$RE/ghidra/support/analyzeHeadless"
grep '"version"' "$RE/rea/node_modules/rea-agents/package.json"
df -h / | tail -1
