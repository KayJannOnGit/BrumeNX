#!/bin/sh
# BrumeNX - build the CLEAN public repository: one fresh git repo, ONE commit, no history, in a separate folder.
#   sh scripts/snapshot-public.sh <output folder> [git ref, default HEAD] [version, default v1.0.0]
#   e.g.  sh scripts/snapshot-public.sh ../brumenx-public publication v1.0.0
#
# Why: the private history (200 commits) contains old paths of the author's PC (with his real name) and work notes.
# Rewriting it is risky; publishing a fresh snapshot is not. This script NEVER pushes, never adds a remote and never
# touches this repository: it only reads it (git archive) and writes the output folder, which must not exist yet.
#
# The commit's author comes from AUTHOR_NAME / AUTHOR_EMAIL (default: "BrumeNX" <brumenx@users.noreply.github.com>;
# use the GitHub « noreply » address of the account that will publish, e.g. 12345678+name@users.noreply.github.com).
# Before committing, the snapshot is scanned: personal paths, real names, keys, tokens, client ids, .env files,
# ROM / BIOS / key / disc files, official artwork. Any hit stops the script (nothing is committed).
set -eu
OUTDIR=${1:?usage: sh scripts/snapshot-public.sh <output folder> [ref] [version]}
REF=${2:-HEAD}
VER=${3:-v1.0.0}
AUTHOR_NAME=${AUTHOR_NAME:-BrumeNX}
AUTHOR_EMAIL=${AUTHOR_EMAIL:-brumenx@users.noreply.github.com}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

[ ! -e "$OUTDIR" ] || { echo "ERREUR : $OUTDIR existe déjà (choisis un dossier neuf)" >&2; exit 1; }
git -C "$HERE" rev-parse --verify -q "$REF^{commit}" >/dev/null || { echo "ERREUR : ref inconnue : $REF" >&2; exit 1; }
mkdir -p "$OUTDIR"
OUTDIR=$(CDPATH= cd -- "$OUTDIR" && pwd)
git -C "$HERE" archive --format=tar "$REF" | tar -xf - -C "$OUTDIR"
cd "$OUTDIR"

# 1) what must never be public (the patterns are split so that this script does not match itself)
fail=0
hit() { echo "  ! $*" >&2; fail=1; }
U=guer; U=${U}gouri; N=yac; N=${N}ine; M=/mnt/c; M=$M/Users/; W=C:; W1=$W/Users/; W2="$W\\\\Users\\\\"
for f in $(grep -rIlis -e "$U" -e "$N" -e "$M" -e "$W1" -e "$W2" . || true); do hit "chemin / nom perso : $f"; done
for f in $(grep -rIlE -e 'BEGIN [A-Z ]*PRIVATE KEY' -e 'nxc_[A-Za-z0-9]{12,}' -e 'gh[pousr]_[A-Za-z0-9]{30,}' \
           -e 'AKIA[0-9A-Z]{16}' -e '(client_secret|refresh_token|access_token|api_key)[\"'\'' ]*[:=][\"'\'' ]*[A-Za-z0-9_\-]{20,}' . || true); do
    hit "secret possible : $f"
done
find . -type f \( -name '.env' -o -name '.env.*' -o -name '*.pem' -o -name '*.key' -o -name 'prod.keys' -o -name 'title.keys' \
    -o -iname '*.iso' -o -iname '*.cso' -o -iname '*.chd' -o -iname '*.nsp' -o -iname '*.xci' -o -iname '*.nsz' -o -iname '*.xcz' \
    -o -iname '*.rom' -o -iname '*.bios' -o -iname 'scph*' -o -iname '*.sfc' -o -iname '*.z64' -o -iname '*.n64' -o -iname '*.gba' \
    -o -iname '*.nds' -o -iname '*.3ds' -o -iname '*.wav' -o -iname '*.ogg' -o -iname '*.mp3' -o -iname '*.flac' \
    -o -iname '*.glb' -o -iname '*.gltf' -o -iname '*.fbx' -o -iname '*.obj' -o -iname '*.bmdl' -o -iname '*.btex' \
    -o -iname 'ui.bin' -o -iname 'ui-*.bin' -o -iname '*.jpg' \) | while read -r f; do
    echo "  ! fichier interdit : $f" >&2; echo x
done | grep -q x && fail=1
# images: only our own UI previews are allowed
for f in $(find . -type f \( -iname '*.png' -o -iname '*.gif' -o -iname '*.webp' -o -iname '*.svg' \) | grep -v -e '^\./docs/apercus/' -e '^\./docs/logo/' -e '^\./assets/logos/' || true); do
    case "$f" in ./musique/switify/vendor/*) ;; *) hit "image à vérifier : $f" ;; esac
done
[ "$fail" = 0 ] || { echo "ERREUR : le snapshot contient des choses à retirer (voir ci-dessus). Rien n'a été commité." >&2; exit 1; }

# 2) one commit, a fresh history
git init -q -b main 2>/dev/null || { git init -q && git symbolic-ref HEAD refs/heads/main; }
git -c core.autocrlf=false add -A
GIT_AUTHOR_NAME=$AUTHOR_NAME GIT_AUTHOR_EMAIL=$AUTHOR_EMAIL GIT_COMMITTER_NAME=$AUTHOR_NAME GIT_COMMITTER_EMAIL=$AUTHOR_EMAIL \
    git -c commit.gpgsign=false commit -q -m "BrumeNX $VER" -m "First public release: a PS2-boot-inspired custom HOME menu for the Nintendo Switch (qlaunch-ext module). Licence: GPL-3.0-or-later; third-party notices in THIRDPARTY.md."
echo "OK : $OUTDIR"
echo "  $(git rev-list --count HEAD) commit, $(git ls-files | wc -l) fichiers, $(du -sh .git | cut -f1) d'historique, branche main, aucun remote"
echo "  auteur : $AUTHOR_NAME <$AUTHOR_EMAIL>"
echo "Étapes suivantes (seulement quand Yass dit « publie ») : docs/RELEASE-CHECKLIST.md"
