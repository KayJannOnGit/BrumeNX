#!/bin/sh
# Brume - rebuild the qlaunch-ext LOADER (atmosphere/contents/0100000000001000/exefs/main) with our changes.
#   wsl -d archlinux -u root -- sh <checkout>/scripts/build-loader.sh
# Changes (idempotent, applied to /root/qlaunch-ext on top of sLaunch's loader.patch):
#   v42.4 - the loader no longer keeps the « avm » service open for its whole life: it never used it, and avm takes
#           very few sessions, so installers (sphaira) failed at the last step with 2021-0003 (sm: OutOfSessions).
# Safety: the exports of the new loader must include every export of the old one (else the menus would not link),
# and the old loader is kept next to the new one (loader.nso.precedent) - the SD copy is made by hand afterwards.
set -eu
export DEVKITPRO=/opt/devkitpro
QEXT=/root/qlaunch-ext
LIBNX=/root/libnx-master-install/opt/devkitpro/libnx
UNDEF=/root/slaunch-build/projects/sMenu/build-qext/sMenu.undef.all
NM=$DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm
OUT=${OUT:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)/builds/loader}   # (next to the checkout)
cd "$QEXT/loader"
[ -f "$UNDEF" ] || { echo "ERREUR : $UNDEF manquant" >&2; exit 1; }

# the old one, kept
"$NM" -D --defined-only loader.elf | awk '{print $NF}' | sort -u > /tmp/loader-old.exports
cp loader.nso loader.nso.precedent
cp loader.npdm loader.npdm.precedent

python3 -I - <<'EOF'
p = '/root/qlaunch-ext/loader/src/main.cpp'
s = open(p, encoding='utf-8').read()
a = '    avmInitialize();\n'
b = '    /* Brume v42.4: avm is not kept open (never used here; installers need its few sessions) */\n'
if a in s:
    assert s.count(a) == 1
    s = s.replace(a, b)
e = '    avmExit();\n'
if e in s:
    assert s.count(e) == 1
    s = s.replace(e, '')
open(p, 'w', encoding='utf-8').write(s)
print('main.cpp: avm', 'still there' if 'avmInitialize();' in s else 'removed')
EOF

rm -f loader.elf loader.nso loader.nsp
make -j"$(nproc)" QEXT_EXTRA_UNDEF="$UNDEF" LIBNX="$LIBNX"
"$NM" -D --defined-only loader.elf | awk '{print $NF}' | sort -u > /tmp/loader-new.exports
LOST=$(comm -23 /tmp/loader-old.exports /tmp/loader-new.exports || true)
if [ -n "$LOST" ]; then
    echo "ERREUR : le nouveau loader a perdu des exports :" >&2
    echo "$LOST" | head -20 >&2
    cp loader.nso.precedent loader.nso; cp loader.npdm.precedent loader.npdm
    exit 1
fi
echo "exports : $(wc -l < /tmp/loader-new.exports) (aucun perdu)"
mkdir -p "$OUT"
cp loader.nso "$OUT/main"
cp loader.npdm "$OUT/main.npdm"
cp loader.nso.precedent "$OUT/main.precedent"
cp loader.npdm.precedent "$OUT/main.npdm.precedent"
git -C "$QEXT" diff -- loader/src/main.cpp > "$OUT/loader-main.diff" || true
ls -l "$OUT"
