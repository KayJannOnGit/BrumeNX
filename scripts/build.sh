#!/bin/sh
# Build Brume in WSL (archlinux, as root), then check every import resolves
# against the installed loader + shared libs (a missing one = black screen).
#   wsl -d archlinux -u root -- sh <checkout>/scripts/build.sh      (the checkout as WSL sees it, e.g. /mnt/c/...)
set -eu
export DEVKITPRO=/opt/devkitpro
SRC=${SRC:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}   # (env: another checkout, e.g. a worktree)
BUILD=${BUILD:-/root/brume-build}
QEXT=/root/qlaunch-ext
NM=$DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm
cd "$SRC"
# v65: the languages - the table is made and every French text of the menu must have its English (else: stop)
python3 tools/lang/build.py --check
make -j"$(nproc)" BUILD="$BUILD"

# Import check: what we import vs what the loader and sdmc:/lib modules export.
{ "$NM" -D --defined-only "$QEXT/loader/loader.elf" | awk '{print $NF}'
  cat "$QEXT/extern/SDL-dlink/extern/neko3d/neko3d-module/neko3d.def" "$QEXT"/lib/*-module/*.def
} | tr -d '\r' | sort -u > "$BUILD/have.txt"
MISSING=$(comm -23 "$BUILD/brume.undef" "$BUILD/have.txt" || true)
if [ -n "$MISSING" ]; then
    echo "ERREUR : imports introuvables :" >&2
    echo "$MISSING" >&2
    exit 1
fi
echo "imports OK ($(wc -l < "$BUILD/brume.undef") symboles)"
ls -la "$BUILD/brume.dnro"
mkdir -p "$SRC/../builds/brume"
OUTNAME=${OUTNAME:-brume.dnro}
cp "$BUILD/brume.dnro" "$SRC/../builds/brume/$OUTNAME"
echo "copie : builds/brume/$OUTNAME"
