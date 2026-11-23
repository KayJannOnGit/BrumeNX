#!/bin/sh
# uisim v90 - prepares the fake card for scripts/detection.txt (a COPY of a uisim card, never the real one):
#   sh detection-carte.sh /root/brume-uisim-detection/run
set -eu
C="$1/sdmc:"
[ -d "$C/brume" ] || { echo "pas de fausse carte dans $1"; exit 1; }
rm -rf "$C/switch/ppsspp-nx" "$C/retroarch" "$C/roms/psp" "$C/roms/gba" "$C/roms/n64"
mkdir -p "$C/roms/gamecube"
: > "$C/roms/gamecube/Sonic Adventure 2 - Battle (USA).iso"
printf 'roue=01\nastuces=00\n' > "$C/brume/emulateurs-detectes.txt"   # last time: only the Switch
echo "carte prete : $(ls "$C/switch" | tr '\n' ' ')"
