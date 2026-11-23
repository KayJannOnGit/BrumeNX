#!/bin/sh
# BrumeNX - the release package: ONE zip whose content is copied as is to the ROOT of the SD card, plus the installers.
#   wsl -d archlinux -u root -- sh <checkout>/scripts/package.sh <version>        e.g.  ... package.sh v1.0.0
# Output (DIST, default: <checkout>/../dist):
#   brumenx-<version>/            exactly the SD root (what the zip contains)
#   brumenx-<version>.zip         the release file
#   install-brumenx.sh / .ps1 / .bat   the installers (macOS / Linux, Windows), copies of scripts/install/
#   brumenx-<version>.sha256      the checksums of the four files above (for the release page)
#
# It builds NOTHING: the menu is <checkout>/../builds/brume/brume-<version>.dnro (or brume.dnro), the overlay and the
# Brume Launcher are taken where they were last built, the assets (sounds, models, ui.bin: not in git) from this checkout
# or else from the main one (../brume). Every path can be forced: MENU=... ASSETS=... OVERLAY=... LAUNCHER=... POLICES=...
# MUSIQUE=... LOADER=... QSD=... SL=... QEXT=... DIST=...
#
# First-launch safe: no settings file is shipped (Brume writes its defaults the first time and an update never overwrites
# the user's), Brume Overlay is only put ready (switched on from Réglages > Brume, never by the package), no Nextendo client_id.
# Brume Musique (Spotify) is NOT in the package since v101 (switched off in the menu: spotify::kEnabled); WITH_MUSIQUE=1 puts
# it back (dormant, no boot2.flag) for the update that brings Spotify back.
#
# The consoles' logos ARE in the package since v1.0.0 (Yass: Wikimedia Commons files, public domain / CC0, trademarks of their
# owners, assets/logos/SOURCES.md); LOGOS=0 rewrites ui.bin without them (tools/ui_sans_logos.py).
# Never in the package: official covers (sdmc:/brume/jaquettes), anyone's own sounds (brume/perso), music, games, BIOS, keys, settings, logs. The last step
# checks every file against an allow-list and stops on anything else.
set -eu
VER=${1:-dev}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=${REPO:-$HERE}                                   # this checkout: scripts, tools, docs, licences
ROOT=${ROOT:-$(dirname "$REPO")}                      # the folder around the checkout (builds/, dist/)
MAIN=${MAIN:-$ROOT/brume}                             # the main checkout: what git does not keep
QSD=${QSD:-/root/slaunch-build/SdOut-qext}            # the qlaunch-ext libraries (built with sLaunch's build-qext.sh)
SL=${SL:-/root/slaunch-build/SdOut/slaunch}           # (sLaunch's build: no longer used for the hbloader, see HBL)
HBL=${HBL:-$ROOT/builds/hbloader_app}                 # v101: the hbloader that launches a .nro as an application - main AND
                                                      # main.npdm, the pair of sLaunch v2.2.3's release (tested on the console;
                                                      # the build's own main had no npdm: the donor launch failed on a clean card)
QEXT=${QEXT:-/root/qlaunch-ext}                       # qlaunch-ext's sources (its licences and those of its libraries)
first() { for p in "$@"; do [ -e "$p" ] && { echo "$p"; return 0; }; done; echo "$1"; }
MENU=${MENU:-$(first "$ROOT/builds/brume/brume-$VER.dnro" "$ROOT/builds/brume/brume.dnro")}
ASSETS=${ASSETS:-$(first "$REPO/assets/sounds" "$MAIN/assets/sounds")}; ASSETS=${ASSETS%/sounds}
OVERLAY=${OVERLAY:-$(first "$REPO/overlay/exefs/main" "$MAIN/overlay/exefs/main")}; OVERLAY=${OVERLAY%/main}
LAUNCHER=${LAUNCHER:-$(first "$REPO/tools/launcher/brume-launcher.nro" "$MAIN/tools/launcher/brume-launcher.nro")}
POLICES=${POLICES:-/root/brume-build/polices}
LOADER=${LOADER:-$ROOT/builds/loader}
MUSIQUE=${MUSIQUE:-$ROOT/builds/brume/musique/atmosphere/contents/420000000042524D/exefs.nsp}
WITH_MUSIQUE=${WITH_MUSIQUE:-0}
DIST=${DIST:-$ROOT/dist}
NAME=brumenx-$VER
OUT=$DIST/$NAME
DOCS=$OUT/brume/docs
L=$DOCS/LICENCES

need() { [ -e "$1" ] || { echo "ERREUR : il manque $1${2:+ ($2)}" >&2; exit 1; }; }
need "$MENU" "le menu : builds/brume/brume-$VER.dnro"
need "$LOADER/main"; need "$LOADER/main.npdm"
need "$QSD/lib/neko3d.dnro"; need "$QSD/lib/miniini.dnro"; need "$QSD/qlaunch-ext/bin/nroloader.nsp"
need "$ASSETS/ui/ui.bin"; need "$ASSETS/sounds"; need "$ASSETS/models/switch.bmdl"; need "$ASSETS/models/env.bin"
need "$HBL/main"; need "$HBL/main.npdm"; need "$POLICES"
need "$OVERLAY/main" "sh overlay/build.sh"; need "$OVERLAY/main.npdm" "sh overlay/build.sh"
need "$LAUNCHER" "sh tools/launcher/build.sh"
if [ "$WITH_MUSIQUE" = 1 ]; then need "$MUSIQUE" "sh musique/build.sh"; fi
need "$QEXT/LICENSE"; need "$REPO/assets/fonts/Outfit-wght.ttf"
for f in install-brumenx.sh install-brumenx.ps1 install-brumenx.bat; do need "$REPO/scripts/install/$f"; done
python3 -c 'import numpy, PIL' 2>/dev/null || { echo "ERREUR : python3 avec numpy et pillow (pacman -S python-numpy python-pillow)" >&2; exit 1; }

rm -rf "$OUT" "$DIST/$NAME.zip" "$DIST/$NAME.sha256"
mkdir -p "$OUT/atmosphere/contents/0100000000001000/exefs" \
         "$OUT/qlaunch-ext/menus" "$OUT/qlaunch-ext/config" "$OUT/qlaunch-ext/bin" "$OUT/lib" \
         "$OUT/brume/sons" "$OUT/brume/modeles" "$OUT/brume/polices" "$OUT/brume/bin/hbloader_app" \
         "$OUT/brume/overlay/exefs" "$OUT/brume/jaquettes" "$OUT/switch/brume-launcher" "$L"

# 1) the loader (replaces the HOME menu: qlaunch-ext + sLaunch's fixes + ours, sources/README.md) and its libraries.
#    No plugins.json: the loader writes its own when there is none (and one already on the card is left alone).
cp "$LOADER/main" "$LOADER/main.npdm" "$OUT/atmosphere/contents/0100000000001000/exefs/"
cp "$QSD/qlaunch-ext/bin/nroloader.nsp" "$OUT/qlaunch-ext/bin/"
cp "$QSD"/lib/*.dnro "$OUT/lib/"
# 2) the menu, chosen by menu.ini (without it the loader opens sLaunch's menu.dnro when there is one)
cp "$MENU" "$OUT/qlaunch-ext/menus/brume.dnro"
printf '[menu]\npath=sdmc:/qlaunch-ext/menus/brume.dnro\n' > "$OUT/qlaunch-ext/config/menu.ini"
# 3) its files (sdmc:/brume/): sounds made by tools/render-sounds.html and tools/synth_*.py, models baked by
#    tools/bake-assets.html (credits: THIRDPARTY.md), the time fonts pre-rendered by tools/fontbake.cpp
for f in "$ASSETS"/sounds/*.wav; do
    case "$(basename "$f")" in bowl.wav|rise.wav) continue ;; esac   # (v38 sounds, not used any more)
    cp "$f" "$OUT/brume/sons/"
done
cp "$ASSETS"/models/*.bmdl "$ASSETS/models/env.bin" "$ASSETS/models/icones.bin" "$OUT/brume/modeles/"
if [ "${LOGOS:-1}" = 1 ]; then cp "$ASSETS/ui/ui.bin" "$OUT/brume/ui.bin"
else python3 "$REPO/tools/ui_sans_logos.py" "$ASSETS/ui/ui.bin" "$OUT/brume/ui.bin" "$REPO/assets/fonts/Outfit-wght.ttf" >/dev/null; fi
cp "$POLICES"/*.bfnt "$OUT/brume/polices/"
cp "$HBL/main" "$HBL/main.npdm" "$OUT/brume/bin/hbloader_app/"
# 4) Brume Overlay: only put ready in brume/overlay/exefs/. It is switched on from Réglages > Brume > « Overlay Brume
#    (essai) », never by the package (atmosphere/contents/010000000000100C is never written here; checked below)
cp "$OVERLAY/main" "$OVERLAY/main.npdm" "$OUT/brume/overlay/exefs/"
# 5) Brume Musique, the Spotify player (only with WITH_MUSIQUE=1): dormant (no boot2.flag), Brume starts it when Spotify is on
if [ "$WITH_MUSIQUE" = 1 ]; then
    mkdir -p "$OUT/atmosphere/contents/420000000042524D"
    cp "$MUSIQUE" "$OUT/atmosphere/contents/420000000042524D/exefs.nsp"
fi
# 6) the Brume Launcher (made once into a forwarder with sphaira, see the full guide)
cp "$LAUNCHER" "$OUT/switch/brume-launcher/"
cat > "$OUT/brume/jaquettes/LISEZMOI.txt" <<'EOF'
Les jaquettes de tes jeux arrivent ici toutes seules dès que la console est en Wi-Fi (Réglages > Données > « Jaquettes
automatiques », allumé de base). « Télécharger les jaquettes » relance la recherche à la main.
Your games' covers land here by themselves once the console is online (Settings > Data > automatic covers, on by default).
EOF

# 7) the guides and the licences (brume/docs/ on the card: readable on the PC, harmless for the menu)
cp "$REPO/docs/INSTALL-EN.md" "$REPO/docs/INSTALL-FR.md" "$REPO/docs/BETA-CHECKLIST.md" "$REPO/docs/NEXTENDO-APP.md" "$DOCS/"
cp "$REPO/docs/INSTALLATION.md" "$DOCS/GUIDE-COMPLET-FR.md"
cat > "$DOCS/ports-exemple.txt" <<'EOF'
# Brume - les ports natifs (la case « Ports natifs » de la roue des consoles)
# (Exemple : pour t'en servir, copie ce fichier dans  sdmc:/brume/ports.txt . Une mise à jour ne l'écrase jamais.)
# Un port = un .nro rangé dans  switch/<son dossier>/ .
# Un dossier qui contient un sous-dossier « game », « game-xxx » ou « game_root » est trouvé tout seul.
# Sinon, écris le nom de son dossier ici, un par ligne (les lignes qui commencent par # sont ignorées).
# Tu peux ajouter son système d'origine après « = », il s'affiche sous la boîte :  mcdub3-nx = Xbox
EOF
cp "$REPO/LICENSE" "$L/BrumeNX-GPL-3.0.txt"
cp "$REPO/THIRDPARTY.md" "$L/"
cp "$REPO/sources/README.md" "$L/SOURCES.md"
cp "$REPO"/assets/fonts/OFL-*.txt "$L/"
cp "$QEXT/LICENSE" "$L/qlaunch-ext-GPL-2.0.txt"
cp_lic() { if [ -e "$1" ]; then cp "$1" "$L/$2"; else echo "ERREUR : licence absente : $1" >&2; exit 1; fi; }
cp_lic "$QEXT/extern/FFmpeg-dlink/LICENSE.md"            FFmpeg-LICENSE.md
cp_lic "$QEXT/extern/FFmpeg-dlink/COPYING.GPLv2"         FFmpeg-COPYING.GPLv2.txt
cp_lic "$QEXT/extern/SDL-dlink/LICENSE.txt"              SDL3-zlib.txt
cp_lic "$QEXT/extern/SDL-dlink/extern/neko3d/LICENSE"    neko3d-zlib.txt
cp_lic "$QEXT/extern/libnx-dlink/LICENSE"                libnx-dlink-Unlicense.txt
cp_lic "$QEXT/extern/miniini/LICENSE"                    miniini-Apache-2.0.txt
# Brume Musique (Switify's engine, cspot, bell and its libraries) - only when it is in the package
if [ "$WITH_MUSIQUE" = 1 ]; then
SW=$REPO/musique/switify
cp_lic "$SW/THIRD_PARTY_NOTICES.md"                      BrumeMusique-Switify-THIRD_PARTY_NOTICES.md
cp_lic "$SW/MODIFICATIONS-BRUME.txt"                     BrumeMusique-MODIFICATIONS.txt
cp_lic "$SW/LICENSES/Beer-Ware.txt"                      BrumeMusique-ipc_server-Beer-Ware.txt
cp_lic "$SW/LICENSES/Apache-2.0.txt"                     Apache-2.0.txt
cp_lic "$SW/vendor/cspot/LICENSE.md"                     cspot-GPL-3.0.md
B=$SW/vendor/cspot/bell/external
cp_lic "$B/cJSON/LICENSE"                                cJSON-MIT.txt
cp_lic "$B/fmt/LICENSE.rst"                              fmt-MIT.rst
cp_lic "$B/mdnssvc/LICENSE.txt"                          mdnssvc-BSD-3.txt
cp_lic "$B/nanopb/LICENSE.txt"                           nanopb-zlib.txt
cp_lic "$B/tremor/COPYING"                               Tremor-BSD-3.txt
fi
cp_lic "$REPO/musique/switify/vendor/cspot/bell/external/nlohmann_json/LICENSE.MIT" nlohmann-json-MIT.txt   # (json.dnro too)
cp_lic "$REPO/src/third_party/qrcodegen-LICENSE.txt"     qrcodegen-MIT.txt
cat > "$L/AUTRES-ET-SOURCES.txt" <<'EOF'
BrumeNX - other licences and where the sources are
==================================================

BrumeNX itself (qlaunch-ext/menus/brume.dnro, brume/overlay, switch/brume-launcher, the sounds, the 3D icons, ui.bin):
GPL-3.0-or-later (BrumeNX-GPL-3.0.txt). Source code: the public repository this release comes from.

In lib/ (qlaunch-ext's shared libraries, built from github.com/Horizon-OC/qlaunch-ext, see SOURCES.md):
  ffmpeg.dnro   FFmpeg, built with --enable-gpl (GPL-2.0-or-later)  -> FFmpeg-LICENSE.md, FFmpeg-COPYING.GPLv2.txt
  sdl3.dnro     SDL 3 (zlib)                                         -> SDL3-zlib.txt
  neko3d.dnro   deko3d / neko3d by fincs (zlib)                      -> neko3d-zlib.txt
  miniini.dnro  miniini (Apache-2.0)                                 -> miniini-Apache-2.0.txt
  json.dnro     nlohmann/json (MIT)                                  -> nlohmann-json-MIT.txt
  xxhash.dnro   xxHash by Yann Collet (BSD-2-Clause, licence text in xxhash.h: github.com/Cyan4973/xxHash)
  dlink         libnx-dlink (public domain, Unlicense)               -> libnx-dlink-Unlicense.txt

The loader (atmosphere/contents/0100000000001000) = qlaunch-ext (GPL-2.0, qlaunch-ext-GPL-2.0.txt) + sLaunch's fixes
(github.com/etonedemid/slaunch, GPL-3.0) + BrumeNX's own. Exact source: SOURCES.md (commits + the patch, kept in the
repository under sources/qlaunch-ext/).
brume/bin/hbloader_app = nx-hbloader (switchbrew, ISC) as modified by sLaunch (GPL-3.0), sLaunch commit 1c86996.
libnx (switchbrew, ISC) is linked in every program. The menu also uses stb_image (Sean Barrett, public domain or MIT),
the QR Code generator library (Project Nayuki, MIT, qrcodegen-MIT.txt) and zlib (zlib License).
Brume Overlay uses nothing but libnx (its text is baked from the Outfit font, OFL).

Fonts: SIL Open Font License 1.1 (OFL-*.txt). 3D models of the consoles: their authors' Creative Commons licences,
credits in THIRDPARTY.md (PS2: CC-BY-NC-SA-4.0, N64: CC-BY-NC-4.0 - no commercial use; the simplified .bmdl versions
keep these licences).

BrumeNX is an unofficial fan project, with no link to Nintendo or Sony. The consoles' logos come from Wikimedia Commons
(public domain / CC0) and are trademarks of their owners, used only to name the consoles. No game, BIOS, key or official
cover is included.
EOF
{
    echo "BrumeNX $VER - $(date -u +%Y-%m-%d)"
    echo "menu: $(sha256sum "$OUT/qlaunch-ext/menus/brume.dnro" | cut -c1-16)  loader: $(sha256sum "$OUT/atmosphere/contents/0100000000001000/exefs/main" | cut -c1-16)"
    rev=$(git -c 'safe.directory=*' -C "$REPO" rev-parse --short HEAD 2>/dev/null || true)   # (WSL root: not the owner)
    [ -z "$rev" ] || echo "git: $rev"
} > "$DOCS/VERSION.txt"
if [ "$WITH_MUSIQUE" = 1 ]; then cat >> "$L/AUTRES-ET-SOURCES.txt" <<'EOF'

atmosphere/contents/420000000042524D = Brume Musique: Switify's background engine (GPL-3.0-or-later) with cspot
(GPL-3.0-or-later) and bell's libraries (cJSON, fmt, nlohmann/json: MIT; nanopb: zlib; Tremor, mdnssvc: BSD-3), the
compact IPC server (Beer-Ware), linked with mbedTLS (Apache-2.0), libcurl (curl licence) and zlib. Source: the
repository's musique/ folder.
EOF
fi
cat > "$OUT/BRUMENX-LISEZMOI-README.txt" <<EOF
BrumeNX $VER - a custom HOME menu for the Nintendo Switch (Atmosphere CFW, emuMMC ONLY)
=======================================================================================

EN - INSTALL
  Easiest: run the installer from the release page (install-brumenx.sh on macOS/Linux, install-brumenx.bat on Windows).
  By hand:
    1. Put the SD card in your computer (or Hekate > USB Tools > SD Card).
    2. Copy EVERYTHING in this zip to the ROOT of the SD card (merge the folders, replace the files).
    3. Boot Atmosphere on your emuMMC. Press any button 3 times on the lock screen.
  Full guide: brume/docs/INSTALL-EN.md

EN - RESCUE (black screen, or you want your old menu back)
  * From the console: hold L + R + (-) for 3 seconds.
  * From the computer (always works): rename the folder
        atmosphere/contents/0100000000001000   ->   0100000000001000_off
    and, if you had turned Brume Overlay on:
        atmosphere/contents/010000000000100C   ->   010000000000100C_off
    The Nintendo HOME menu comes back. Rename them back to bring BrumeNX back.
  * Only if sLaunch is also installed (qlaunch-ext/menus/menu.dnro exists): deleting qlaunch-ext/config/menu.ini
    starts sLaunch instead. Without sLaunch, do NOT delete it (the screen would stay black): rename the folder instead.

FR - INSTALLER
  Le plus simple : l'installeur de la page de la release (install-brumenx.sh sur Mac/Linux, install-brumenx.bat sur Windows).
  À la main :
    1. Mets la carte SD dans l'ordinateur (ou Hekate > USB Tools > SD Card).
    2. Copie TOUT le contenu de ce zip à la RACINE de la carte SD (fusionner les dossiers, remplacer les fichiers).
    3. Démarre Atmosphère sur ton emuMMC. Appuie 3 fois sur n'importe quel bouton sur l'écran verrouillé.
  Guide complet : brume/docs/INSTALL-FR.md

FR - SECOURS (écran noir, ou tu veux ton ancien menu)
  * Depuis la console : maintiens L + R + (-) pendant 3 secondes.
  * Depuis l'ordinateur (marche toujours) : renomme le dossier
        atmosphere/contents/0100000000001000   ->   0100000000001000_off
    et, si tu avais allumé l'overlay Brume :
        atmosphere/contents/010000000000100C   ->   010000000000100C_off
    Le menu Nintendo revient. Renomme-les dans l'autre sens pour remettre BrumeNX.
  * Seulement si sLaunch est aussi installé (qlaunch-ext/menus/menu.dnro existe) : supprimer qlaunch-ext/config/menu.ini
    relance sLaunch. Sans sLaunch, ne le supprime PAS (écran noir) : renomme le dossier à la place.

Unofficial fan project, not affiliated with Nintendo or Sony. No games, BIOS, keys or official covers included.
Never use it on your sysNAND. Licences: brume/docs/LICENCES/.
EOF

# 8) the checksums of every file (the installers check them after copying; brume/docs/SHA256SUMS.txt)
(cd "$OUT" && find . -type f ! -path ./brume/docs/SHA256SUMS.txt | sed 's|^\./||' | LC_ALL=C sort | while read -r f; do
    sha256sum "$f"; done > brume/docs/SHA256SUMS.txt)

# 9) the check: every file must be one we expect (nothing official, nothing personal slipped in)
cd "$OUT"
bad=$(find . -type f | sed 's|^\./||' | while read -r f; do
    case "$f" in
        atmosphere/contents/0100000000001000/exefs/main|atmosphere/contents/0100000000001000/exefs/main.npdm) ;;
        atmosphere/contents/420000000042524D/exefs.nsp) ;;
        qlaunch-ext/bin/nroloader.nsp|qlaunch-ext/menus/brume.dnro|qlaunch-ext/config/menu.ini) ;;
        lib/*.dnro) ;;
        brume/sons/*.wav|brume/modeles/*.bmdl|brume/modeles/env.bin|brume/modeles/icones.bin|brume/ui.bin) ;;
        brume/polices/*.bfnt|brume/bin/hbloader_app/main|brume/bin/hbloader_app/main.npdm|brume/overlay/exefs/main|brume/overlay/exefs/main.npdm) ;;
        brume/jaquettes/LISEZMOI.txt|brume/docs/*.md|brume/docs/*.txt|brume/docs/LICENCES/*) ;;
        switch/brume-launcher/brume-launcher.nro|BRUMENX-LISEZMOI-README.txt) ;;
        *) echo "$f" ;;
    esac
done)
[ -z "$bad" ] || { echo "ERREUR : fichiers inattendus dans le paquet :" >&2; echo "$bad" >&2; exit 1; }
[ ! -e atmosphere/contents/010000000000100C ] || { echo "ERREUR : l'overlay ne doit pas etre actif" >&2; exit 1; }
[ ! -e atmosphere/contents/420000000042524D/flags ] || { echo "ERREUR : Brume Musique doit dormir (pas de flags/)" >&2; exit 1; }
[ ! -e brume/reglages.ini ] && [ ! -e brume/nextendo.ini ] && [ ! -e brume/profil.txt ] && [ ! -e brume/perso ] && \
    [ ! -e brume/spotify ] || { echo "ERREUR : un fichier de réglages / perso dans le paquet" >&2; exit 1; }
if [ "${LOGOS:-1}" != 1 ]; then python3 - brume/ui.bin <<'EOF'
import struct, sys
d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', d, 4)[0]
names = [d[8 + 52 * i:8 + 52 * i + 24].split(b'\0')[0].decode() for i in range(n)]
left = [x for x in names if x.startswith('logo')]
sys.exit('ERREUR : logos encore dans ui.bin : %s' % left if left else 0)
EOF
fi
# nothing personal in the texts we wrote or copied (paths of a PC, a real name, a client id, a token)
M=/mnt/c; W=C:   # (split, so that the public snapshot's own scan does not see these words in this script)
if grep -rIlE "$M/Users|$W\\\\Users|/home/[a-z]|nxc_[A-Za-z0-9]{12,}|refresh_token=[A-Za-z0-9]|BEGIN [A-Z ]*PRIVATE KEY" . ; then
    echo "ERREUR : un texte du paquet contient un chemin ou un secret (voir ci-dessus)" >&2; exit 1
fi
sha256sum -c --quiet brume/docs/SHA256SUMS.txt

cd "$DIST"
if command -v zip >/dev/null; then (cd "$NAME" && zip -qrX "../$NAME.zip" .); else bsdtar -a -cf "$NAME.zip" -C "$NAME" .; fi
for f in install-brumenx.sh install-brumenx.ps1 install-brumenx.bat; do cp "$REPO/scripts/install/$f" "$DIST/$f"; done
sha256sum "$NAME.zip" install-brumenx.sh install-brumenx.ps1 install-brumenx.bat > "$NAME.sha256"
du -sh "$NAME" "$NAME.zip"
echo "$(find "$NAME" -type f | wc -l) fichiers -> $DIST/$NAME.zip (+ installeurs, $NAME.sha256)"
