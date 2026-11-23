#!/usr/bin/env python3
# v73 - the settings icons' THICKNESS: a signed distance field written into the blue channel of every « ico_* » sprite
# of ui.bin (the interface sheet baked by tools/bake-assets.html). Red (the shape), green (the baked edge light), alpha
# and every other sprite stay byte for byte the same: the old glass (ui_fsh mode 22) does not change, the new one
# (mode 29, the PS3 XMB's liquid glass) reads the distance to know where the glass is thick.
#
#   b = 0.5 - d / 16      d = signed distance to the shape's edge in DRAWING px (the 1x sheet's px; < 0 inside),
#                         so -8 .. +8 px with a step of 1/16 px. 0 everywhere outside 8 px (and in the gaps).
#
# Same algorithm as bake-assets.html (sdfIcons): coverage = red x alpha, upsampled x4 (bilinear), cut at 0.5, exact
# Euclidean distance transform inside and outside (Felzenszwalb & Huttenlocher, written by us), averaged back per pixel.
#
#   python3 tools/ui_sdf.py assets/ui/ui.bin [out.bin]      (in place when out is omitted; running it twice is harmless)
import struct
import sys

import numpy as np

INF = 1e20


def edt_1d(f):
    # squared distance transform of one row (lower envelope of parabolas)
    n = len(f)
    d = [0.0] * n
    v = [0] * n
    z = [0.0] * (n + 1)
    k = 0
    v[0] = 0
    z[0], z[1] = -INF, INF
    for q in range(1, n):
        while True:
            p = v[k]
            s = ((f[q] + q * q) - (f[p] + p * p)) / (2.0 * q - 2.0 * p)
            if s <= z[k] and k > 0:
                k -= 1
                continue
            if s <= z[k]:            # k == 0: replace
                v[0] = q
                z[0], z[1] = -INF, INF
                break
            k += 1
            v[k] = q
            z[k], z[k + 1] = s, INF
            break
    k = 0
    for q in range(n):
        while z[k + 1] < q:
            k += 1
        p = v[k]
        d[q] = (q - p) * (q - p) + f[p]
    return d


def edt(mask):
    # Euclidean distance from every pixel to the nearest True pixel of mask (0 on them)
    h, w = mask.shape
    f = np.where(mask, 0.0, INF)
    for y in range(h):
        f[y] = edt_1d(f[y].tolist())
    for x in range(w):
        f[:, x] = edt_1d(f[:, x].tolist())
    return np.sqrt(f)


def sdf_of(cov, scale):
    # cov: coverage 0..1 (h x w). Returns d in drawing px (< 0 inside), same size.
    h, w = cov.shape
    U = 4
    # bilinear upsample x4, sample centres: (i + .5) / U - .5 in source px
    ys = (np.arange(h * U) + 0.5) / U - 0.5
    xs = (np.arange(w * U) + 0.5) / U - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, h - 1); y1 = np.clip(y0 + 1, 0, h - 1); fy = np.clip(ys - np.floor(ys), 0, 1)
    x0 = np.clip(np.floor(xs).astype(int), 0, w - 1); x1 = np.clip(x0 + 1, 0, w - 1); fx = np.clip(xs - np.floor(xs), 0, 1)
    top = cov[y0][:, x0] * (1 - fx) + cov[y0][:, x1] * fx
    bot = cov[y1][:, x0] * (1 - fx) + cov[y1][:, x1] * fx
    up = top * (1 - fy)[:, None] + bot * fy[:, None]
    inside = up >= 0.5
    if not inside.any():
        return np.full((h, w), 8.0 * scale)
    dout = edt(inside)            # outside: distance to the shape
    din = edt(~inside)            # inside: distance to the outside
    d = np.where(inside, -(din - 0.5), dout - 0.5) / U    # atlas px
    d = d.reshape(h, U, w, U).mean(axis=(1, 3))
    return d / scale              # drawing px (the 1x sheet)


def main():
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src
    data = bytearray(open(src, 'rb').read())
    if data[:3] != b'BUI':
        sys.exit('not a ui.bin')
    n = struct.unpack_from('<I', data, 4)[0]
    o = 8
    names = []
    for _ in range(n):
        names.append(data[o:o + 24].split(b'\0')[0].decode('utf-8', 'replace'))
        o += 24 + 12 + 16
    done = 0
    for a, scale in enumerate((1.0, 1.5)):
        W, H = struct.unpack_from('<II', data, o); o += 8
        rects = [struct.unpack_from('<4I', data, o + 16 * i) for i in range(n)]
        o += 16 * n
        px = np.frombuffer(data, dtype=np.uint8, count=W * H * 4, offset=o).reshape(H, W, 4).copy()
        for nm, (x, y, w, h) in zip(names, rects):
            if not nm.startswith('ico_') or w == 0 or h == 0:
                continue
            blk = px[y:y + h, x:x + w].astype(np.float64) / 255.0
            cov = blk[:, :, 0] * blk[:, :, 3]
            d = sdf_of(cov, scale)
            px[y:y + h, x:x + w, 2] = np.clip(np.round((0.5 - d / 16.0) * 255.0), 0, 255).astype(np.uint8)
            done += 1
        data[o:o + W * H * 4] = px.tobytes()
        o += W * H * 4
    open(dst, 'wb').write(data)
    print(f'{done} icones : distance au bord ecrite dans le bleu -> {dst}')


if __name__ == '__main__':
    main()
