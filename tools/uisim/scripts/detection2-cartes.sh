#!/bin/sh
# uisim v93 - two fake cards for scripts/detection2-*.txt (COPIES of a uisim card's brume/ folder, never the real card):
#   sh detection2-cartes.sh <a uisim card, e.g. /root/brume-uisim-detection/run> <out folder>
#   <out>/neuf : a new user - Switch + PS2 known last time; Tico's Snes9x module installed since (it arrives after the intro),
#                Mega Drive games without their emulator (the hint); then the script adds RetroArch's melonDS and Genesis Plus GX
#                cores and a DS game: the DS and the Mega Drive arrive (the Master System / Game Gear: no games, they stay away)
#   <out>/yass : Yass's card as his logs show it (v90's known file: the 7 consoles + no DS slot) -> the same + the DS
set -eu
SRC="$1/sdmc:"
OUT="$2"
[ -d "$SRC/brume" ] || { echo "pas de fausse carte dans $1"; exit 1; }
nro() { mkdir -p "$(dirname "$1")"; : > "$1"; }
base() {                                     # $1 = card root
    rm -rf "$1"; mkdir -p "$1/sdmc:"
    cp -r "$SRC/brume" "$1/sdmc:/brume"
    rm -f "$1/sdmc:/brume/emulateurs.ini" "$1/sdmc:/brume/emulateurs-detectes.txt"
}
# ---- a new user ----
C="$OUT/neuf/sdmc:"
base "$OUT/neuf"
nro "$C/switch/NetherSX2.nro"
nro "$C/switch/retroarch_switch.nro"
nro "$C/switch/JKSV.nro"
mkdir -p "$C/roms/ps2" "$C/roms/megadrive" "$C/roms/snes"
: > "$C/roms/ps2/SSX 3 (USA).iso"
: > "$C/roms/ps2/Frequency (USA).iso"
: > "$C/roms/megadrive/Sonic The Hedgehog 2 (World).md"
: > "$C/roms/megadrive/Streets of Rage 2 (USA).md"
: > "$C/roms/snes/Super Mario World (USA).sfc"
printf 'roue=0003\nastuces=0000\nps2=NetherSX2|sdmc:/switch/NetherSX2.nro\n' > "$C/brume/emulateurs-detectes.txt"   # last time: Switch + PS2
# Tico's SNES module (installed since last time: it arrives after the intro) and a game in Tico's own folder
mkdir -p "$C/tico/modules/tico-snes9x" "$C/tico/roms/snes"
: > "$C/tico/modules/tico-snes9x/tico-snes9x.nro"
: > "$C/tico/roms/snes/Chrono Trigger (USA).sfc"
cat > "$C/tico/modules/tico-snes9x/module.json" <<'EOF'
{
  // tico module (test copy for Brume's uisim, the documented format)
  "id": "tico-snes9x",
  "name": "Snes9x",
  "nro": "tico-snes9x.nro",
  "launch": "\"{nro}\" \"{slug}\" \"{rom}\" \"{title}\"",
  "config_file": "snes9x.cfg", /* comment */ "system_dir": "snes9x",
  "systems": [
    { "slug": "snes", "title": "Super Nintendo", "short_title": "SNES", "extensions": [".sfc", ".smc", ".zip"],
      "games_file": "snes.dat", "colors": { "start": "#5a4fcf", "end": "#2b2477" } }
  ]
}
EOF
# ---- Yass's card (his logs, 2026-11-22) ----
C="$OUT/yass/sdmc:"
base "$OUT/yass"
for f in switch/NetherSX2.nro switch/nezumiiruka/nezumiiruka.nro switch/dolphin-nx/dolphin.nro switch/ppsspp-nx/PPSSPP.nro \
         switch/retroarch_switch.nro switch/retroarch-gba/retroarch-gba.nro switch/retroarch-n64/retroarch-n64.nro \
         switch/retroarch-ps1/retroarch-ps1.nro switch/retroarch-ds/retroarch-ds.nro switch/retroarch-ds-desmume/retroarch-ds-desmume.nro \
         retroarch/cores/desmume_libretro_libnx.nro retroarch/cores/melonds_libretro_libnx.nro retroarch/cores/mgba_libretro_libnx.nro \
         retroarch/cores/mupen64plus_next_libretro_libnx.nro retroarch/cores/pcsx_rearmed_libretro_libnx.nro; do nro "$C/$f"; done
for d in gamecube gba n64 nds ps1 ps2 psp; do mkdir -p "$C/roms/$d"; done
: > "$C/roms/ps2/SSX 3 (USA).iso"; : > "$C/roms/gamecube/Sonic Adventure 2 - Battle (USA).iso"; : > "$C/roms/n64/Mario Kart 64 (USA).z64"
: > "$C/roms/ps1/Crash Bandicoot (USA).chd"; : > "$C/roms/psp/WipEout Pure (USA).iso"; : > "$C/roms/gba/Pokemon Emerald (USA).gba"
: > "$C/roms/nds/Mario Kart DS (USA).nds"
cat > "$C/brume/emulateurs-detectes.txt" <<'EOF'
# Brume - ce que Brume a trouve sur la carte SD (refait tout seul a chaque demarrage, inutile de le modifier).
# Pour choisir toi-meme un emulateur : brume/emulateurs.ini
roue=7F
astuces=00
ps2=NetherSX2|sdmc:/switch/NetherSX2.nro
gamecube=Nezumiiruka|sdmc:/switch/nezumiiruka/nezumiiruka.nro
n64=RetroArch|sdmc:/switch/retroarch-n64/retroarch-n64.nro
ps1=RetroArch|sdmc:/switch/retroarch-ps1/retroarch-ps1.nro
psp=PPSSPP|sdmc:/switch/ppsspp-nx/PPSSPP.nro
gba=RetroArch|sdmc:/switch/retroarch-gba/retroarch-gba.nro
EOF
echo "cartes pretes : $OUT/neuf $OUT/yass"
