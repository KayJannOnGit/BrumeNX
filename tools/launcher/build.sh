#!/bin/sh
# builds tools/launcher/brume-launcher.nro (WSL archlinux, devkitPro)
set -e
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
cd "$(dirname "$0")"
python3 make-icon.py icon.jpg
make -j4
ls -la brume-launcher.nro
