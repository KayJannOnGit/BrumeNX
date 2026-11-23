#!/bin/sh
# uisim - build the menu for the PC and run a script (WSL archlinux, as root, after scripts/build.sh made the headers):
#   wsl -d archlinux -u root -- sh <checkout>/tools/uisim/build.sh [script]   (or SRC=<checkout> sh /tmp/build.sh)
# Then open http://localhost:8766/tools/uisim/view.html (the « brume-apercu » server of .claude/launch.json).
set -eu
SRC=${SRC:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
GEN=${BUILD:-/root/brume-build}                       # fonts.h, shaders.h, ui_layout.h (made by scripts/build.sh)
LIBNX=/root/libnx-master-install/opt/devkitpro/libnx
NEKO=/root/qlaunch-ext/extern/SDL-dlink/extern/neko3d
OBJ=${UISIM_OBJ:-/root/brume-uisim/obj}
RUN=${UISIM_RUN:-/root/brume-uisim/run}                   # the fake card: ./sdmc:/brume (a COPY, never the real card)
OUT=$SRC/tools/uisim/out
SCRIPT=${1:-$SRC/tools/uisim/scripts/tour.txt}
mkdir -p "$OBJ" "$RUN/sdmc:/brume" "$OUT"
FLAGS="-std=gnu++20 -O1 -g -w -fno-exceptions -fno-rtti -fno-threadsafe-statics -D__SWITCH__ -DBRUME_UISIM -I$SRC/tools/uisim/include -idirafter $LIBNX/include -I$NEKO/include -I$GEN -I$SRC/src"
OBJS=""
for f in "$SRC"/src/*.cpp "$SRC/tools/uisim/GpuSim.cpp" "$SRC/tools/uisim/main.cpp"; do
    b=$(basename "$f" .cpp)
    [ "$b" = "Gpu" ] && continue
    o="$OBJ/$b.o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find "$SRC/src" "$SRC/tools/uisim/include" -name '*.h*' -newer "$o" 2>/dev/null | head -1)" ]; then
        g++ $FLAGS -c "$f" -o "$o" &
    fi
    OBJS="$OBJS $o"
done
wait
# the stubs: link once, every missing function becomes a stub, link again
if ! g++ -o "$OBJ/uisim" $OBJS -Wl,--no-demangle 2> "$OBJ/ld.txt"; then
    python3 "$SRC/tools/uisim/gen_stubs.py" "$OBJ/ld.txt" "$LIBNX/include" "$OBJ/stubs.c"
    gcc -c -O1 -w "$OBJ/stubs.c" -o "$OBJ/stubs.o"
    if ! g++ -o "$OBJ/uisim" $OBJS "$OBJ/stubs.o" 2> "$OBJ/ld2.txt"; then head -30 "$OBJ/ld2.txt"; exit 1; fi
fi
rm -f "$OUT"/*.bin "$OUT"/index.json
cd "$RUN"
unshare -n "$OBJ/uisim" "$SCRIPT" "$OUT"
