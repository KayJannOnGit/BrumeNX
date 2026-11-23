#!/bin/sh
# BrumeNX installer for macOS and Linux - copies the release zip to a Switch SD card, checks every file, deletes nothing.
#
#   sh install-brumenx.sh                         (finds brumenx-*.zip next to this script, asks for the SD card)
#   sh install-brumenx.sh brumenx-v1.0.0.zip /Volumes/SWITCH
#
# What it does:
#   1. unzips the release in a temporary folder and checks it against brume/docs/SHA256SUMS.txt;
#   2. checks that the target looks like a Switch SD card with Atmosphère (an « atmosphere » folder);
#   3. copies every file. A file that is already there and identical is left alone. A file that is already there and
#      DIFFERENT (for example another menu's loader) is first saved in  <SD>/brumenx-sauvegarde/<date>/  then replaced;
#   4. reads every copied file back and checks its SHA-256.
# It never deletes anything on the card, and never touches your settings (BrumeNX ships none).
#
# FR : installeur de BrumeNX pour Mac et Linux. Il copie le zip sur la carte SD, vérifie chaque fichier, ne supprime
#      rien. Un fichier déjà présent et différent est d'abord sauvegardé dans <SD>/brumenx-sauvegarde/<date>/.
set -eu

say()  { printf '%s\n' "$*"; }
die()  { printf '\nERREUR / ERROR: %s\n' "$*" >&2; exit 1; }
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# --- the zip ------------------------------------------------------------------------------------------------------
zip=${1:-}
if [ -z "$zip" ]; then
    zip=$(ls -1t "$here"/brumenx-*.zip 2>/dev/null | head -n 1 || true)
    [ -n "$zip" ] || die "no brumenx-*.zip next to this script. Usage: sh install-brumenx.sh <zip> <SD card folder>"
fi
[ -f "$zip" ] || die "zip not found: $zip"

# --- the SD card --------------------------------------------------------------------------------------------------
sd=${2:-}
if [ -z "$sd" ]; then
    say "BrumeNX installer - $(basename "$zip")"
    say ""
    say "Cards that look like a Switch SD (they have an « atmosphere » folder):"
    found=0
    for d in /Volumes/* /media/*/* /run/media/*/* /mnt/*; do
        [ -d "$d/atmosphere" ] || continue
        say "   $d"; found=1
    done
    [ "$found" = 1 ] || say "   (none found: put the card in, or start Hekate > USB Tools > SD Card, then try again)"
    say ""
    printf 'Type the SD card folder (e.g. /Volumes/SWITCH) / Dossier de la carte SD : '
    read -r sd
fi
sd=${sd%/}
[ -n "$sd" ] && [ -d "$sd" ] || die "folder not found: $sd"
case "$sd" in /|/Users|/home|/Volumes|/media|/mnt|"$HOME") die "$sd is not an SD card" ;; esac
[ -d "$sd/atmosphere" ] || die "$sd has no « atmosphere » folder: BrumeNX needs Atmosphère on this card (wrong folder?)"

# --- tools --------------------------------------------------------------------------------------------------------
if command -v sha256sum >/dev/null 2>&1; then sha() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then sha() { shasum -a 256 "$1" | cut -d' ' -f1; }
else die "no sha256sum or shasum on this computer"; fi
cpx() { cp "$1" "$2"; }
if [ "$(uname -s)" = Darwin ]; then
    export COPYFILE_DISABLE=1                      # no « ._ » files on the FAT32 card
    cpx() { cp -X "$1" "$2"; }
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/brumenx.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM                 # (only our own temporary folder)
say "1/4 Unzipping / décompression..."
if command -v unzip >/dev/null 2>&1; then unzip -q "$zip" -d "$tmp/z"
elif command -v bsdtar >/dev/null 2>&1; then mkdir -p "$tmp/z" && bsdtar -xf "$zip" -C "$tmp/z"
elif command -v python3 >/dev/null 2>&1; then python3 -I -m zipfile -e "$zip" "$tmp/z"
else die "no unzip, bsdtar or python3 to open the zip"; fi
sums=$tmp/z/brume/docs/SHA256SUMS.txt
[ -f "$sums" ] || die "this zip has no brume/docs/SHA256SUMS.txt (not a BrumeNX release?)"

say "2/4 Checking the download / vérification du téléchargement..."
n=0
while read -r h f; do
    case "$f" in ""|/*|*..*) die "bad line in SHA256SUMS.txt: $f" ;; esac
    [ -f "$tmp/z/$f" ] || die "missing in the zip: $f"
    [ "$(sha "$tmp/z/$f")" = "$h" ] || die "damaged file in the zip (download it again): $f"
    n=$((n + 1))
done < "$sums"
say "    $n files OK"

say "3/4 Copying to $sd / copie sur la carte..."
stamp=$(date +%Y-%m-%d_%H%M%S)
backup=$sd/brumenx-sauvegarde/$stamp
new=0; same=0; saved=0
copy_one() {   # $1 = path inside the zip
    src=$tmp/z/$1; dst=$sd/$1
    if [ -f "$dst" ]; then
        if [ "$(sha "$dst")" = "$(sha "$src")" ]; then same=$((same + 1)); return 0; fi
        mkdir -p "$(dirname "$backup/$1")"
        cpx "$dst" "$backup/$1"
        saved=$((saved + 1))
    else
        new=$((new + 1))
    fi
    mkdir -p "$(dirname "$dst")"
    cpx "$src" "$dst"
}
while read -r h f; do copy_one "$f"; done < "$sums"
copy_one brume/docs/SHA256SUMS.txt
# an older HOME menu packed as exefs.nsp would win over BrumeNX's exefs/ (Atmosphère takes the .nsp first): moved to the backup
q=atmosphere/contents/0100000000001000/exefs.nsp
if [ -f "$sd/$q" ]; then
    mkdir -p "$(dirname "$backup/$q")"
    cpx "$sd/$q" "$backup/$q" && [ "$(sha "$sd/$q")" = "$(sha "$backup/$q")" ] && rm -f "$sd/$q" && saved=$((saved + 1)) &&
        say "    an older HOME menu (exefs.nsp) was moved to the backup / un ancien menu (exefs.nsp) est mis de côté"
fi
sync

say "4/4 Checking the card / vérification de la carte..."
while read -r h f; do
    [ "$(sha "$sd/$f")" = "$h" ] || die "$f was not written correctly: copy again, or check the card"
done < "$sums"

say ""
say "Done / Terminé: $new new, $same already there, $saved replaced."
[ "$saved" = 0 ] || say "Replaced files were saved first in: $backup"
say ""
say "Next: eject the card safely, put it back in the Switch, boot Atmosphère on your emuMMC."
say "Ensuite : éjecte la carte proprement, remets-la dans la Switch, démarre Atmosphère sur ton emuMMC."
say "Rescue / secours: BRUMENX-LISEZMOI-README.txt (at the root of the card / à la racine de la carte)."
