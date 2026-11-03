#!/bin/sh
# Fetches the pinned UI dependencies into build/deps (not committed):
#   LVGL (ui/LVGL_VERSION), and from npm: Barlow fonts (OFL), lv_font_conv (MIT), for font generation
#   and the mockup reference renderer. Idempotent.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEPS="$ROOT/build/deps"
mkdir -p "$DEPS"
LV=$(cat "$ROOT/ui/LVGL_VERSION")
if [ ! -f "$DEPS/lvgl/lvgl.h" ]; then
  rm -rf "$DEPS/lvgl"
  git clone -q --depth 1 --branch "$LV" https://github.com/lvgl/lvgl.git "$DEPS/lvgl"
fi
if [ ! -x "$DEPS/node/node_modules/.bin/lv_font_conv" ]; then
  mkdir -p "$DEPS/node"
  cd "$DEPS/node"
  [ -f package.json ] || npm init -y >/dev/null
  npm install --silent --no-audit --no-fund @fontsource/barlow@5.3.0 @fontsource/barlow-condensed@5.3.0 lv_font_conv@1.5.3
fi
echo "deps ready in $DEPS"
