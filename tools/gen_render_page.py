#!/usr/bin/env python3
# Fabrique brume/tools/render-sounds.html en recopiant TELLES QUELLES les fonctions de son de la maquette
# (docs/M1/maquette-brume.html) : aucune recette retapée à la main, donc la console joue exactement la maquette.
#   python3 brume/tools/gen_render_page.py
import os, re

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
SRC = open(os.path.join(ROOT, 'docs', 'M1', 'maquette-brume.html'), encoding='utf-8').read()
NAMES = ['makeIR', 'noiseNode', 'bell', 'softLow', 'rippleAir', 'shockSound', 'heartbeat', 'unlockChime', 'sighSound', 'chime', 'thock', 'swish', 'whoosh', 'navSound', 'selectTick', 'glassTick', 'popNote', 'angelSwell', 'bigBangBurst', 'crystalTouch', 'pageSweep', 'unlockBloom']

def extract(name):
    i = SRC.index('function ' + name + '(')
    j = SRC.index('{', SRC.index(')', i))
    depth = 0
    for k in range(j, len(SRC)):
        c = SRC[k]
        if c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return SRC[i:k + 1]
    raise SystemExit('fonction introuvable : ' + name)

def const(name):
    m = re.search(r'^const ' + name + r'\b[^\n]*', SRC, re.M)
    return m.group(0)

funcs = '\n'.join(extract(n) for n in NAMES)
consts = '\n'.join(const(n) for n in ['LOCK_CHORD', 'mtof', 'HB_GAP', 'STAGE_VOICES'])
consts += '\nconst CAM_DUR=' + re.search(r'CAM_DUR=([0-9.]+)', SRC).group(1) + ';'
cons = re.search(r"const CONSOLES = (\[[\s\S]*?\n\]);", SRC).group(1)
consts += '\nconst CONSOLES = ' + cons + ';\nconst GROW=2.0;'

page = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'render-sounds.tmpl.html'), encoding='utf-8').read()
page = page.replace('/*@CONSTS@*/', consts).replace('/*@FUNCS@*/', funcs)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'render-sounds.html'), 'w', encoding='utf-8').write(page)
print('render-sounds.html écrit :', ', '.join(NAMES))
