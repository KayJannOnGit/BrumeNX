#!/usr/bin/env python3
# Brume - a ui.bin WITHOUT the consoles' logos, for the testers' package (no Sony / Nintendo artwork in what we hand out).
#
# The name banner of the wheel has 7 logo sprites (logo0..logo6, baked by tools/bake-assets.html from Wikimedia Commons
# files). This tool erases them from BOTH sheets of ui.bin and takes them out of every drawn group:
#   - Switch, GameCube, PlayStation (an icon + the name in our font): the icon goes, the name is centred on the banner;
#   - PS2, N64, PSP, GBA (the logo WAS the name): the name is written instead, in Outfit 200 (OFL, the font of the other
#     names), measured on the baked « PlayStation » so that size, colour and height match, and centred on the banner.
# Every other sprite, every other pixel stays byte for byte the same; the file keeps its format (the menu reads it as
# before, src/Assets.cpp LoadUi). Running it on its own output is harmless (nothing left to erase).
#
#   python3 tools/ui_sans_logos.py <ui.bin> <out.bin> <Outfit-wght.ttf>
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# the wheel's order (src/Games.cpp kConsoles): the names a removed logo leaves to write
NAMES = {1: 'PlayStation 2', 3: 'Nintendo 64', 5: 'PSP', 6: 'Game Boy Advance'}
REF = 'name4'                  # the baked « PlayStation »: our ruler for size, colour, height
REF_TEXT = 'PlayStation'
MARGIN = 14                    # the bake's margin around each image (menu px at 1x; Wheel.cpp « x0 + 14 »)


def cstr(b):
    return b.split(b'\0')[0].decode('utf-8', 'replace')


def pad(s, n):
    b = s.encode('utf-8')[:n - 1]
    return b + b'\0' * (n - len(b))


def font(path, size):
    f = ImageFont.truetype(path, size)
    try:
        f.set_variation_by_axes([200])          # Outfit is a variable font: weight 200 like the mockup's .name
    except Exception:
        pass
    return f


def render(path, size, text, rgb):
    # the text alone, straight rgba, with 2 px of air: (pixels, the baseline's row in them)
    f = font(path, size)
    l, t, r, b = f.getbbox(text, anchor='ls')
    w, h = r - l + 4, b - t + 4
    img = Image.new('L', (w, h), 0)
    ImageDraw.Draw(img).text((2 - l, 2 - t), text, font=f, fill=255, anchor='ls')
    a = np.array(img, dtype=np.uint8)
    out = np.zeros((h, w, 4), np.uint8)
    out[:, :, 0], out[:, :, 1], out[:, :, 2], out[:, :, 3] = rgb[0], rgb[1], rgb[2], a
    return out, 2 - t


def ink(a, thr=128):
    ys, xs = np.nonzero(a > thr)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def free_spot(occ, w, h):
    # first free (w x h) place, row by row, with 1 px of air around it (bilinear filtering never bleeds a neighbour)
    H, W = occ.shape
    ww, hh = w + 2, h + 2
    if ww > W or hh > H:
        return None
    s = np.zeros((H + 1, W + 1), np.int64)
    s[1:, 1:] = occ.astype(np.int64).cumsum(0).cumsum(1)
    win = s[hh:, ww:] - s[:-hh, ww:] - s[hh:, :-ww] + s[:-hh, :-ww]
    ys, xs = np.nonzero(win == 0)
    if len(ys) == 0:
        return None
    return int(xs[0]) + 1, int(ys[0]) + 1


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__ or 'usage: ui_sans_logos.py <ui.bin> <out.bin> <Outfit-wght.ttf>')
    src, dst, ttf = sys.argv[1:4]
    data = bytearray(open(src, 'rb').read())
    if data[:3] != b'BUI':
        sys.exit('pas un ui.bin')
    n = struct.unpack_from('<I', data, 4)[0]
    if not 0 < n <= 512:
        sys.exit('ui.bin abime')
    recs = []                                       # [name, group, x, y, w, h]
    o = 8
    for _ in range(n):
        recs.append([cstr(data[o:o + 24]), cstr(data[o + 24:o + 36])] + list(struct.unpack_from('<4f', data, o + 36)))
        o += 52
    sheets = []                                     # (offset of W,H, rects offset, pixels offset, W, H)
    for _ in range(2):
        W, H = struct.unpack_from('<II', data, o)
        sheets.append((o, o + 8, o + 8 + 16 * n, W, H))
        o += 8 + 16 * n + W * H * 4
    if o != len(data):
        sys.exit('ui.bin : taille inattendue')
    idx = {r[0]: i for i, r in enumerate(recs)}
    logos = [i for i, r in enumerate(recs) if r[0].startswith('logo') and r[0][4:].isdigit()]
    kof = {i: int(recs[i][0][4:]) for i in logos}  # logoK -> console K
    if not logos:
        print('aucun logo dans ' + src + ' : rien a faire')
        open(dst, 'wb').write(data)
        return

    # the banner's centre per console (the union of what the bake laid out there: logo + name, not the sample « count »)
    centre = {}
    for r in recs:
        if r[1].startswith('name') and not r[0].startswith('count'):
            lo, hi = centre.get(r[1], (1e9, -1e9))
            centre[r[1]] = (min(lo, r[2]), max(hi, r[2] + r[4]))
    centre = {g: (lo + hi) * 0.5 for g, (lo, hi) in centre.items()}

    # the ruler: the baked « PlayStation » (1x sheet) - its colour, its ink width, where its ink starts in the tile
    _, ro, po, W0, H0 = sheets[0]
    px0 = np.frombuffer(data, np.uint8, W0 * H0 * 4, po).reshape(H0, W0, 4)
    rx, ry, rw, rh = struct.unpack_from('<4I', data, ro + 16 * idx[REF])
    ref = px0[ry:ry + rh, rx:rx + rw]
    rgb = tuple(int(v) for v in np.round(ref[ref[:, :, 3] > 200][:, :3].mean(0)))
    bx0, by0, bx1, by1 = ink(ref[:, :, 3])
    probe, _ = render(ttf, 40, REF_TEXT, rgb)
    qx0, _, qx1, _ = ink(probe[:, :, 3])
    size = 40.0 * (bx1 - bx0) / (qx1 - qx0)         # the font size that gives the baked ink width (1x sheet)
    # where the baked baseline sits from the tile's middle (so the new names stand on the same line as the others)
    probe, base = render(ttf, size, REF_TEXT, rgb)
    _, qy0, _, _ = ink(probe[:, :, 3])
    base_from_mid = (by0 - (qy0 - base)) - rh * 0.5
    ref_mid_y = recs[idx[REF]][3] + recs[idx[REF]][5] * 0.5
    print('regle : %s, police %.2f px, couleur %s' % (REF, size, rgb))

    chosen = {}                                     # console -> the tile height picked on the 1x sheet
    for si, (wo, ro, po, W, H) in enumerate(sheets):
        scale = (1.0, 1.5)[si]                      # the 720p sheet, then the 1080p one (tools/ui_sdf.py: same scales)
        px = np.frombuffer(data, np.uint8, W * H * 4, po).reshape(H, W, 4).copy()
        rects = [list(struct.unpack_from('<4I', data, ro + 16 * i)) for i in range(n)]
        for i in logos:                             # 1) erase every logo, keep its place free
            x, y, w, h = rects[i]
            px[y:y + h, x:x + w] = 0
        occ = np.zeros((H, W), bool)
        for i in range(n):
            if i in logos:
                continue
            x, y, w, h = rects[i]
            occ[y:y + h, x:x + w] = True
        for i in logos:                             # 2) the names the logos carried, written in our font
            k = kof[i]
            if k not in NAMES:
                continue
            t, tbase = render(ttf, size * scale, NAMES[k], rgb)
            tx0, ty0, tx1, ty1 = ink(t[:, :, 3], 0)
            m = int(round(MARGIN * scale))
            w = (tx1 - tx0) + 2 * m
            # the tile's height: the baked names' (78 at 1x), or less if the sheet has no room (never cutting the ink)
            spot = None
            heights = [78, 68, 64, 56, 50, 48, 46]
            if k in chosen:                         # the 1080p tile: the same shape as the 720p one when it fits
                heights = [hh for hh in heights if hh <= chosen[k]]
            for hh in heights:
                h = int(round(hh * scale))
                top = int(round(h * 0.5 + base_from_mid * scale)) - tbase + ty0    # the ink's first row in the tile
                if top < 1 or top + (ty1 - ty0) > h - 1:
                    continue
                spot = free_spot(occ, w, h)
                if spot:
                    break
            if not spot:
                sys.exit('pas de place dans la feuille %d pour « %s »' % (si, NAMES[k]))
            x, y = spot
            tile = np.zeros((h, w, 4), np.uint8)
            src_ = t[ty0:ty1, tx0:tx1]
            tile[top:top + (ty1 - ty0), m:m + (tx1 - tx0)] = src_
            px[y:y + h, x:x + w] = tile
            occ[y - 1:y + h + 1, x - 1:x + w + 1] = True
            rects[i] = [x, y, w, h]
            chosen.setdefault(k, hh)
            if si == 0:                             # the sprite itself (menu px = the 1x sheet's px)
                g = recs[i][1]
                recs[i][0] = 'name%d' % k
                recs[i][4], recs[i][5] = float(w), float(h)
                recs[i][2] = centre.get(g, 640.0) - w * 0.5
                recs[i][3] = ref_mid_y - h * 0.5
            print('feuille %d : « %s » %dx%d en (%d, %d)' % (si, NAMES[k], w, h, x, y))
        for i in logos:                             # the icons that go: no place in the sheet any more
            if kof[i] not in NAMES:
                rects[i] = [0, 0, 0, 0]
        data[po:po + W * H * 4] = px.tobytes()
        for i in range(n):
            struct.pack_into('<4I', data, ro + 16 * i, *rects[i])

    for i in logos:                                 # 3) the icons' records: out of every group (never drawn)
        k = kof[i]
        if k in NAMES:
            continue
        g = recs[i][1]
        recs[i][0], recs[i][1] = 'sanslogo%d' % k, ''
        name = idx.get('name%d' % k)
        if name is not None:                        # its name alone, centred on the banner
            recs[name][2] = centre.get(g, 640.0) - recs[name][4] * 0.5
    o = 8
    for r in recs:
        data[o:o + 24] = pad(r[0], 24)
        data[o + 24:o + 36] = pad(r[1], 12)
        struct.pack_into('<4f', data, o + 36, *r[2:6])
        o += 52
    open(dst, 'wb').write(data)
    print('%d logos retires -> %s' % (len(logos), dst))


if __name__ == '__main__':
    main()
