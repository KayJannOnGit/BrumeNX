#!/bin/sh
# builds the overlay (exefs/main + exefs/main.npdm) and its preview .nro (WSL archlinux, devkitPro; python3-pillow for the text)
set -e
export DEVKITPRO=/opt/devkitpro
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
cd "$(dirname "$0")"
python3 - <<'PY'
import json
j = json.load(open('/root/qlaunch-ext/loader/loader.json'))
j['name'] = 'brume-overlay'
j['program_id'] = j['program_id_range_min'] = j['program_id_range_max'] = '0x010000000000100C'
j['main_thread_priority'] = 44
j['main_thread_stack_size'] = '0x00040000'
json.dump(j, open('overlay.json', 'w'), indent=4)
PY
# v90: the menu's font (Outfit, SIL OFL 1.1) baked into the program at the sizes it draws: no font file, no FreeType
python3 tools/bakefont.py ../assets/fonts source/glyphs.inc
make -j4
make -j4 -f preview.mk
mkdir -p exefs
cp brume-overlay.nso exefs/main
cp brume-overlay.npdm exefs/main.npdm
ls -la exefs brume-overlay-apercu.nro
