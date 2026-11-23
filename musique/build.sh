#!/bin/sh
# Brume Musique - build the Spotify background service (WSL archlinux, as root):
#   SRC=<worktree> BUILD=/root/brume-musique-build sh <worktree>/musique/build.sh
# Output: $BUILD/brume-musique.nsp, copied to builds/brume/musique/atmosphere/contents/420000000042524D/exefs.nsp
# (the SD card layout: copy that « atmosphere » folder to the root of the card). See docs/SPOTIFY.md.
# The source is Switify's GPL-3.0-or-later background engine (musique/switify, cspot inside), changed for Brume
# (musique/switify/MODIFICATIONS-BRUME.txt). Needs devkitA64, libnx, switch-cmake, switch-mbedtls, switch-curl.
set -eu
SRC=${SRC:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
BUILD=${BUILD:-/root/brume-musique-build}
export DEVKITPRO=/opt/devkitpro DEVKITA64=/opt/devkitpro/devkitA64
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:/usr/sbin:/usr/bin:/bin
cmake -S "$SRC/musique/switify/phase2/sysmodule" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.13 > "$BUILD.cmake.log" 2>&1 || { tail -20 "$BUILD.cmake.log"; exit 1; }
ninja -C "$BUILD" -j"$(nproc)" switify-background-package > "$BUILD.ninja.log" 2>&1 || { grep -E "error|Error" "$BUILD.ninja.log" | head -30; exit 1; }
WARN=$(grep -c "warning:" "$BUILD.ninja.log" || true)
echo "avertissements : $WARN (journal : $BUILD.ninja.log)"
OUT="$SRC/../builds/brume/musique/atmosphere/contents/420000000042524D"
mkdir -p "$OUT"
cp "$BUILD/switify-background.nsp" "$OUT/exefs.nsp"
cp "$BUILD/switify-background.nsp" "$BUILD/brume-musique.nsp"
ls -la "$OUT/exefs.nsp"
echo "service OK : builds/brume/musique/atmosphere/contents/420000000042524D/exefs.nsp"
