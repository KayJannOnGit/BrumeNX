#!/bin/sh
# Brume Overlay on the PC: its own drawing code with a fake <switch.h> -> sim/power.png, quick.png, volume.png, planche.png
# and the cost of every frame. WSL archlinux: needs python3-pillow (the text is baked by ../tools/bakefont.py).
set -e
cd "$(dirname "$0")"
python3 ../tools/bakefont.py ../../assets/fonts ../source/glyphs.inc
python3 render.py game
g++ -O2 -std=gnu++17 -Wall -I. sim.cpp -o sim
./sim
python3 render.py
