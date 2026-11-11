#!/usr/bin/env bash
# Build script for Virtual FaderFox PCn (Windows / MSYS2 MINGW64).
#
# The project may live on a UNC/network path (e.g. \\cube\...), which the
# MinGW toolchain handles poorly, so sources are mirrored to a local build
# directory before compiling. MSYS2 must be installed at C:\msys64.
#
# Usage (from Git Bash or MSYS2 bash):
#   ./build.sh           build + install into your Rack 2 user folder
#   ./build.sh clean     remove local build artifacts

set -euo pipefail

RACK_DIR="C:/vcv/Rack-SDK"
BUILD_ROOT="/c/vcv/build"
MSYS_BASH="/c/msys64/usr/bin/bash.exe"
SLUG="VirtualFaderFox"

# Project directory = this script's location (kept in POSIX form; UNC works for cp).
SRC="$(cd "$(dirname "$0")" && pwd)"

DEST="$BUILD_ROOT/$SLUG"

if [ "${1:-}" = "clean" ]; then
  rm -rf "$DEST"
  echo "Cleaned $DEST"
  exit 0
fi

mkdir -p "$DEST"
cp -f "$SRC"/plugin.json "$SRC"/Makefile "$DEST/"
cp -f "$SRC"/LICENSE* "$DEST/" 2>/dev/null || true
rm -rf "$DEST/src" "$DEST/presets" "$DEST/res"
cp -r "$SRC/src" "$DEST/"
[ -d "$SRC/presets" ] && cp -r "$SRC/presets" "$DEST/"
[ -d "$SRC/res" ] && cp -r "$SRC/res" "$DEST/"

"$MSYS_BASH" -lc "export PATH=\"/mingw64/bin:\$PATH\"; export USERPROFILE=\"$USERPROFILE\"; export LOCALAPPDATA=\"${LOCALAPPDATA:-$USERPROFILE/AppData/Local}\"; cd '$DEST' && make -j4 RACK_DIR='$RACK_DIR' \${1:-dist} && PD=\"\$(cygpath -u \"\$LOCALAPPDATA\")/Rack2/plugins-win-x64\" && mkdir -p \"\$PD\" && rm -rf \"\$PD/$SLUG\" && tar -xf dist/${SLUG}-*.vcvplugin -C \"\$PD\" && rm -f \"\$PD\"/${SLUG}-*.vcvplugin && echo \"Installed to: \$PD/$SLUG\""
