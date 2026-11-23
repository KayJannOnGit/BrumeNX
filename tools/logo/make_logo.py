#!/usr/bin/env python3
# BrumeNX - the logo, generated (every light placed by maths, nothing drawn by hand, no font needed to view it).
#   python3 tools/logo/make_logo.py [police...]   -> docs/logo/*.svg + docs/logo/apercu.html
#
# The mark: the PS2 clock's ORBS (reports/Rendu des menus PS2.md, part 4) - seven lights on a tilted ring in 3D, each
# dragging its trail behind it. The trail ages like Sony's: for the point i of n, f = 1 - i/n, colour (R f^3, G f^2, B f),
# so red dies first, then green, and the old trail ends in deep blue. Every light is added on black (screen blend):
# a wide coloured halo, a glow, a white-hot core, and for the nearest ones a thin horizontal streak. Far lights are
# smaller and dimmer (depth), and the blue fog of the boot screen sits behind (fractal noise, only in the framed versions).
# The wordmark « BrumeNX » is outlined from an OFL font (assets/fonts), so the SVG looks the same everywhere.
# Needs python-fonttools (only for the wordmark).
import math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'docs', 'logo')
FONTS = os.path.join(REPO, 'assets', 'fonts')
os.makedirs(OUT, exist_ok=True)
BG = '#020309'


def f(x):
    return ('%.2f' % x).rstrip('0').rstrip('.')


# ---------------------------------------------------------------- the orbs
def ring_point(phi, cx, cy, R, k=0.34, tilt=-13.0):
    """A point of the ring (a circle seen from above at an angle), its depth z (-1 far .. 1 near)."""
    x, y, z = R * math.cos(phi), R * math.sin(phi) * k, math.sin(phi)
    t = math.radians(tilt)
    return cx + x * math.cos(t) - y * math.sin(t), cy + x * math.sin(t) + y * math.cos(t), z


def orbs(cx=256, cy=262, R=196, n_orbs=7, phase=0.62, uid='m', k=0.34, boost=1.0):
    golden = 0.381966
    items = []                                  # (depth, svg) - drawn from far to near
    for j in range(n_orbs):
        phi0 = phase + j * 2 * math.pi / n_orbs + 0.22 * math.sin(j * 2.3)       # not quite even: alive, never a dial
        span = 2 * math.pi / n_orbs * (0.80 + 0.25 * ((j * golden) % 1))
        N = 54
        for i in range(N, 0, -1):               # the trail, oldest first
            fi = 1 - i / N
            x, y, z = ring_point(phi0 - span * (i / N), cx, cy, R, k)
            d = 0.62 + 0.38 * (z + 1) / 2
            r = (0.9 + 2.9 * fi ** 0.8) * d * boost
            col = '#%02x%02x%02x' % (round(255 * fi ** 3), round(255 * fi ** 2), round(255 * fi))
            items.append((z - 0.001 * i, '<circle cx="%s" cy="%s" r="%s" fill="%s" fill-opacity="%s"/>' %
                          (f(x), f(y), f(r), col, f((0.15 + 0.6 * fi) * (0.55 + 0.45 * d)))))
        x, y, z = ring_point(phi0, cx, cy, R, k)
        d = 0.62 + 0.38 * (z + 1) / 2           # 0.62 far .. 1 near
        head = ('<circle cx="{x}" cy="{y}" r="{h}" fill="url(#{u}-halo)" opacity="{o1}"/>'
                '<circle cx="{x}" cy="{y}" r="{g}" fill="url(#{u}-glow)" opacity="{o2}"/>'
                '<circle cx="{x}" cy="{y}" r="{c}" fill="#ffffff"/>').format(
            x=f(x), y=f(y), h=f(44 * d * boost), g=f(15 * d * boost), c=f(3.9 * d * boost), u=uid, o1=f(0.55 + 0.45 * d), o2=f(0.7 + 0.3 * d))
        if z > 0.15:                            # the nearest: a thin streak of light, like a lens
            head += '<ellipse cx="%s" cy="%s" rx="%s" ry="%s" fill="url(#%s-streak)" opacity="%s"/>' % (
                f(x), f(y), f(70 * d), f(1.3 * d), uid, f(0.35 + 0.5 * z))
        items.append((z + 0.5, head))
    items.sort(key=lambda it: it[0])
    return ''.join(s for _, s in items)


def defs(uid):
    return ('<radialGradient id="{u}-halo"><stop offset="0" stop-color="#8fb8ff" stop-opacity=".42"/>'
            '<stop offset=".35" stop-color="#4f72e8" stop-opacity=".16"/><stop offset="1" stop-color="#2a3cb0" stop-opacity="0"/></radialGradient>'
            '<radialGradient id="{u}-glow"><stop offset="0" stop-color="#ffffff" stop-opacity=".95"/>'
            '<stop offset=".3" stop-color="#d8e8ff" stop-opacity=".55"/><stop offset="1" stop-color="#7aa6ff" stop-opacity="0"/></radialGradient>'
            '<radialGradient id="{u}-streak"><stop offset="0" stop-color="#ffffff" stop-opacity=".9"/>'
            '<stop offset="1" stop-color="#9cc0ff" stop-opacity="0"/></radialGradient>'
            '<radialGradient id="{u}-cloud"><stop offset="0" stop-color="#2f56e0" stop-opacity=".34"/>'
            '<stop offset=".5" stop-color="#2440b8" stop-opacity=".13"/><stop offset="1" stop-color="#101a60" stop-opacity="0"/></radialGradient>'
            '<radialGradient id="{u}-mask"><stop offset="0" stop-color="#fff"/><stop offset=".6" stop-color="#fff" stop-opacity=".55"/>'
            '<stop offset="1" stop-color="#fff" stop-opacity="0"/></radialGradient>'
            '<mask id="{u}-fogmask"><rect width="100%" height="100%" fill="url(#{u}-mask)"/></mask>'
            '<filter id="{u}-fog" x="0" y="0" width="100%" height="100%" color-interpolation-filters="sRGB">'
            '<feTurbulence type="fractalNoise" baseFrequency=".0045 .009" numOctaves="5" seed="11"/>'
            '<feColorMatrix values="0 0 0 0 .09  0 0 0 0 .17  0 0 0 0 .52  0 0 0 2.4 -1.02"/></filter>'
            '<filter id="{u}-soft" x="-20%" y="-60%" width="140%" height="220%"><feGaussianBlur stdDeviation="9"/></filter>'
            '<linearGradient id="{u}-word" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff"/>'
            '<stop offset=".55" stop-color="#dfe8ff"/><stop offset="1" stop-color="#8fb0f0"/></linearGradient>').format(u=uid)


def dust(w, h, n, seed=3):
    """Faint specks of light (the boot screen's dust), deterministic."""
    out, s = [], seed
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7fffffff; x = s % w
        s = (s * 1103515245 + 12345) & 0x7fffffff; y = s % h
        s = (s * 1103515245 + 12345) & 0x7fffffff; r = 0.4 + (s % 100) / 100 * 0.9
        out.append('<circle cx="%d" cy="%d" r="%s" fill="#a8c4ff" fill-opacity="%s"/>' % (x, y, f(r), f(0.08 + (s % 37) / 37 * 0.25)))
    return ''.join(out)


def mark(uid, framed, cx=256, cy=262, R=196, k=0.34, boost=1.0):
    cloud = '<ellipse cx="%s" cy="%s" rx="230" ry="%s" fill="url(#%s-cloud)"/>' % (f(cx), f(cy + 6), f(150 + 200 * (k - 0.34)), uid)
    fog = ('<g mask="url(#%s-fogmask)"><rect width="512" height="512" filter="url(#%s-fog)"/></g>' % (uid, uid)) if framed else ''
    return fog + cloud + '<g style="mix-blend-mode:screen">' + orbs(cx, cy, R, uid=uid, k=k, boost=boost) + '</g>'


# ---------------------------------------------------------------- the wordmark
FONT_CHOICES = {                                  # (Yass, 2026-11-23: Zen Dots, chosen among 10 OFL fonts)
    'zendots': ('ZenDots-Regular.ttf', {}, 'Zen Dots', 'large et ronde, un côté console japonaise des années 2000'),
    'lexend':  ('LexendExa-wght.ttf', {'wght': 300}, 'Lexend Exa', ''),          # (the tagline only)
}


def outline(key, text, size, track):
    from fontTools.ttLib import TTFont
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.boundsPen import BoundsPen
    file, axes, _, _ = FONT_CHOICES[key]
    font = TTFont(os.path.join(FONTS, file))
    if axes:
        from fontTools.varLib.instancer import instantiateVariableFont
        font = instantiateVariableFont(font, axes)
    upm = font['head'].unitsPerEm
    gs, cmap, hmtx = font.getGlyphSet(), font.getBestCmap(), font['hmtx']
    k = size / upm
    x, paths = 0.0, []
    bp = BoundsPen(gs)
    for ch in text:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (k, 0, 0, -k, x, 0)))
        gs[g].draw(TransformPen(bp, (k, 0, 0, -k, x, 0)))
        paths.append(pen.getCommands())
        x += hmtx[g][0] * k + track
    xmin, ymin, xmax, ymax = bp.bounds
    return ' '.join(paths), xmin, ymin, xmax, ymax


def svg(w, h, uid, inner, bg=True):
    b = '<rect width="100%%" height="100%%" fill="%s"/>' % BG if bg else ''
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d"><defs>%s</defs>%s%s</svg>\n'
            % (w, h, w, h, defs(uid), b, inner))


def write(name, text):
    with open(os.path.join(OUT, name), 'w', encoding='utf-8') as fh:
        fh.write(text)


def banner(key, uid):
    """1280 x 640 (GitHub's social preview size): the orbs left, the name right, the fog behind both."""
    W, H = 1280, 640
    d, x0, y0, x1, y1 = outline(key, 'BrumeNX', 150, 12 if key != 'doto' else 6)
    tw, th = x1 - x0, y1 - y0
    scale = min(1.0, 560 / tw)
    left, mid = 664, 292                                                  # the word: from x = 664, centred on y = 292
    tx, ty = left - x0 * scale, mid + th * scale / 2 - y1 * scale
    word = ('<g transform="translate(%s %s) scale(%s)">'
            '<path d="%s" fill="#9fc0ff" opacity=".45" filter="url(#%s-soft)"/>'
            '<path d="%s" fill="url(#%s-word)"/></g>') % (f(tx), f(ty), f(scale), d, uid, d, uid)
    tag_d, a0, b0, a1, b1 = outline('lexend', 'A PS2-INSPIRED HOME MENU FOR SWITCH', 15, 3.2)
    ts = min(1.0, (tw * scale) / (a1 - a0))
    tag = '<g transform="translate(%s %s) scale(%s)"><path d="%s" fill="#7d93c6" opacity=".9"/></g>' % (
        f(left + 4 - a0 * ts), f(mid + th * scale / 2 + 54), f(ts), tag_d)
    fog = '<g mask="url(#%s-fogmask)"><rect width="%d" height="%d" filter="url(#%s-fog)"/></g>' % (uid, W, H, uid)
    m = '<g transform="translate(16 30) scale(1.1)">%s</g>' % ('<ellipse cx="256" cy="268" rx="300" ry="190" fill="url(#%s-cloud)"/>' % uid +
                                                              '<g style="mix-blend-mode:screen">' + orbs(uid=uid) + '</g>')
    return svg(W, H, uid, fog + dust(W, H, 140) + m + word + tag)


def data_uri(name):
    import base64
    with open(os.path.join(OUT, name), 'rb') as fh:
        return 'data:image/svg+xml;base64,' + base64.b64encode(fh.read()).decode()


def main():
    keys = [k for k in sys.argv[1:] if k in FONT_CHOICES and k != 'lexend'] or ['zendots']
    write('brumenx-orbes.svg', svg(512, 512, 'o', mark('o', False), bg=False))        # transparent, for dark backgrounds
    write('brumenx-icone.svg', svg(512, 512, 'i', '<g>%s</g>' % mark('i', True, 256, 256, 208, 0.56, 1.75)).replace(
        '<rect width="100%" height="100%" fill="#020309"/>', '<rect width="512" height="512" rx="112" fill="#020309"/>'))
    cards = ''
    for k in keys:
        write('brumenx-banniere-%s.svg' % k, banner(k, 'b' + k))
        _, _, name, why = FONT_CHOICES[k]
        cards += '<section><img src="%s" alt=""><h2>%s</h2><p>%s</p></section>' % (data_uri('brumenx-banniere-%s.svg' % k), name, why)
    ic = data_uri('brumenx-icone.svg')
    write('apercu.html', '''<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Logo BrumeNX</title><style>
:root{color-scheme:dark}body{margin:0;background:#000;color:#cfdcff;font:15px/1.5 system-ui,sans-serif;padding:28px 16px}
h1{font-weight:300;letter-spacing:.12em;text-align:center;margin:0 0 24px}main{max-width:1100px;margin:auto;display:grid;gap:30px}
section img{width:100%%;height:auto;display:block;border-radius:14px}h2{margin:10px 0 2px;font-weight:500;color:#eef4ff}p{margin:0;color:#8fa2cc}
.icons{display:flex;gap:24px;align-items:end;justify-content:center;margin:6px 0 30px}.icons img{border-radius:22%%}
</style></head><body><h1>BrumeNX — les orbes de la PS2</h1>
<div class="icons"><img src="%s" width="160" height="160" alt=""><img src="%s" width="64" height="64" alt=""><img src="%s" width="32" height="32" alt=""></div>
<main>%s</main></body></html>
''' % (ic, ic, ic, cards))
    print('logo : %s' % ', '.join(sorted(os.listdir(OUT))))


main()
