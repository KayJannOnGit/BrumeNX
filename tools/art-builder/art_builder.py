#!/usr/bin/env python3
# Brume - art-builder (CLAUDE.md section 5): the game covers are fetched ON THE PC, never on the console.
#   python3 tools/art-builder/art_builder.py <sd root> <out dir>
# For every rom in <sd>/roms/<console>/, the front cover (libretro-thumbnails, « Named_Boxarts ») is downloaded
# ONE request at a time with a pause, kept in a cache (never the same image twice), then turned into a texture the
# console reads as is: 'BTEX', u32 w, h, levels, BC1 (DXT1) levels - 256 x 512 stretched (the box's face gives the real
# proportions back), 3 mip levels. Output: <out>/jaquettes/<console>/<rom name>.btex (copied to sdmc:/brume/jaquettes/).
# Official covers stay OUT of the repository (like the logos): the cache and the output live in ignored folders.
import os, sys, time, struct, subprocess, urllib.request, urllib.parse, urllib.error

SYSTEMS = {   # sdmc:/roms/<folder> -> libretro-thumbnails repository
    'gamecube': 'Nintendo_-_GameCube', 'gba': 'Nintendo_-_Game_Boy_Advance', 'n64': 'Nintendo_-_Nintendo_64',
    'ps1': 'Sony_-_PlayStation', 'ps2': 'Sony_-_PlayStation_2', 'psp': 'Sony_-_PlayStation_Portable',
}
PORTS = {     # native ports: sdmc:/switch/<folder>/<x>.nro -> the ORIGINAL game's libretro-thumbnails repository
    'mcdub3-nx': 'Microsoft_-_Xbox',                       # Midnight Club 3: DUB Edition (Xbox recompilation)
    'mcdub3remix-nx': 'Microsoft_-_Xbox',                  # (its Remix, when it comes out)
    'nfsmw-nx': 'Microsoft_-_Xbox_360',                    # Need for Speed: Most Wanted (Xbox 360 recompilation)
    'nfscx': 'Microsoft_-_Xbox',                           # Need for Speed: Carbon (Xbox recompilation)
    'nfsu2x': 'Microsoft_-_Xbox',                          # Need for Speed: Underground 2 (Xbox recompilation)
}
EXT = ('.iso', '.chd', '.cso', '.bin', '.gba', '.z64', '.n64', '.v64', '.rvz', '.gcm')
PAUSE = 2.5                      # seconds between two requests (sites have blocked Yass for too many requests)
UA = 'brume-art-builder/1.0'     # generic, no personal data

def clean(name):
    # « [PS2](2001) SSX Tricky » -> « SSX Tricky »; keeps « (USA) » etc. that the user's file may carry
    while name.startswith('[') or name.startswith('('):
        close = ']' if name[0] == '[' else ')'
        i = name.find(close)
        if i < 0: break
        name = name[i + 1:].lstrip()
    return name.strip()

def libretro_name(n):
    for ch in '&*/:`<>?\\|"':      # characters libretro replaces in file names
        n = n.replace(ch, '_')
    return n

def candidates(base):
    n = clean(base)
    out = [n]
    if '(' not in n:
        out += [n + ' (USA)', n + ' (Europe)', n + ' (USA, Europe)']
    return [libretro_name(c) for c in out]

def nacp_title(path):
    # the port's own name: its .nro's NACP (American English, else the first one there) - read defensively
    try:
        with open(path, 'rb') as f:
            hdr = f.read(0x80)
            if len(hdr) < 0x80 or hdr[0x10:0x14] != b'NRO0': return None
            size = struct.unpack_from('<I', hdr, 0x18)[0]
            f.seek(size)
            aset = f.read(0x38)
            if len(aset) < 0x38 or aset[:4] != b'ASET': return None
            no, ns = struct.unpack_from('<QQ', aset, 0x18)
            if ns < 0x3000: return None
            f.seek(size + no)
            nacp = f.read(0x3000)
    except OSError:
        return None
    for lang in [0] + list(range(1, 16)):
        n = nacp[lang * 0x300:lang * 0x300 + 0x200].split(b'\0')[0].decode('utf-8', 'replace').strip()
        if n: return n
    return None

def fetch(repo, name, cache):
    path = os.path.join(cache, repo, name + '.png')
    if os.path.exists(path) and os.path.getsize(path) > 1000:
        return path, False                                   # already here: never twice
    miss = os.path.join(cache, 'introuvables.txt')           # names already asked for nothing: never asked again
    key = repo + '/' + name
    if os.path.exists(miss) and key in open(miss, encoding='utf-8').read().splitlines():
        return None, False
    def missing():
        os.makedirs(cache, exist_ok=True)
        with open(miss, 'a', encoding='utf-8') as f: f.write(key + '\n')
        return None, True
    url = 'https://raw.githubusercontent.com/libretro-thumbnails/%s/master/Named_Boxarts/%s.png' % (repo, urllib.parse.quote(name))
    req = urllib.request.Request(url, headers={'User-Agent': UA})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            data = r.read(16 << 20)
    except urllib.error.HTTPError as e:
        return missing() if e.code == 404 else (None, True)
    except Exception:
        return None, True                                    # network trouble: we'll try again next time
    if len(data) < 1000 or data[:8] != b'\x89PNG\r\n\x1a\n':
        return missing()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return path, True

def tree(repo, cache):
    # the list of every cover of a system: ONE request per system, kept in the cache (never asked twice)
    path = os.path.join(cache, repo + '.tree.json')
    if not os.path.exists(path):
        req = urllib.request.Request('https://api.github.com/repos/libretro-thumbnails/%s/git/trees/master?recursive=1' % repo,
                                     headers={'User-Agent': UA})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read(64 << 20)
        except Exception:
            return [], True
        os.makedirs(cache, exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
        net = True
    else:
        net = False
    import json
    try:
        items = json.load(open(path, encoding='utf-8')).get('tree', [])
    except Exception:
        return [], net
    names = [i['path'][len('Named_Boxarts/'):-4] for i in items
             if isinstance(i, dict) and str(i.get('path', '')).startswith('Named_Boxarts/') and i['path'].endswith('.png')]
    return names, net

def best_match(base, names):
    # « WipEout Pure (USA) » -> « WipEout Pure (USA) (En,Fr,Es) (v2.00) »: same title, same region first, never a beta / demo
    n = clean(base)
    title = n.split(' (')[0].strip().lower()
    tags = [t.strip(' )').lower() for t in n.split(' (')[1:]]
    def score(c):
        cl = c.lower()
        if not cl.startswith(title + ' ('): return None
        s = 0
        for t in tags:
            if '(' + t in cl: s += 10
        if '(europe' in cl or '(france' in cl: s += 2
        if '(usa' in cl: s += 1
        for bad in ('beta', 'demo', 'promo', 'proto', 'sample', 'kiosk'):
            if bad in cl: s -= 50
        if '(v' in cl: s += 0.1 * cl.count('.')           # a later version, if several
        return s
    ranked = sorted((score(c), c) for c in names if score(c) is not None)
    return ranked[-1][1] if ranked else None

def rgba(png, w, h):
    raw = subprocess.run(['magick', png, '-resize', '%dx%d!' % (w, h), '-depth', '8', 'RGBA:-'], capture_output=True, check=True).stdout
    assert len(raw) == w * h * 4
    return raw

def c565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def bc1(raw, w, h):
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            px = []
            for y in range(4):
                o = ((by + y) * w + bx) * 4
                for x in range(4):
                    px.append((raw[o + x * 4], raw[o + x * 4 + 1], raw[o + x * 4 + 2]))
            lum = [p[0] * 3 + p[1] * 6 + p[2] for p in px]
            lo, hi = px[lum.index(min(lum))], px[lum.index(max(lum))]
            c0, c1 = c565(*hi), c565(*lo)
            if c0 == c1:
                out += struct.pack('<HHI', c0, c1, 0)
                continue
            if c0 < c1: c0, c1, hi, lo = c1, c0, lo, hi
            # the 4 colours of the block (c0 > c1: opaque mode)
            def ex(c): return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
            e0, e1 = ex(c0), ex(c1)
            pal = [e0, e1, tuple((2 * a + b) // 3 for a, b in zip(e0, e1)), tuple((a + 2 * b) // 3 for a, b in zip(e0, e1))]
            idx = 0
            for i, p in enumerate(px):
                best = min(range(4), key=lambda k: (p[0] - pal[k][0]) ** 2 + (p[1] - pal[k][1]) ** 2 + (p[2] - pal[k][2]) ** 2)
                idx |= best << (2 * i)
            out += struct.pack('<HHI', c0, c1, idx)
    return bytes(out)

def png_size(png):
    with open(png, 'rb') as f:
        b = f.read(24)
    w, h = struct.unpack('>II', b[16:24])
    if not (16 <= w <= 8192 and 16 <= h <= 8192): raise ValueError('taille de jaquette bizarre')
    return w, h

def btex(png, dst):
    # the texture keeps the cover's orientation (portrait 256 x 512, square 256 x 256, landscape 512 x 256) and stores the
    # scan's real proportions (width / height x 1000): the console builds the box's face with them - no distortion
    sw, sh = png_size(png)
    aspect = sw / sh
    w, h = (512, 256) if aspect > 1.2 else (256, 512) if aspect < 0.83 else (256, 256)
    levels = [(w, h), (w // 2, h // 2), (w // 4, h // 4)]
    body = b''.join(bc1(rgba(png, lw, lh), lw, lh) for lw, lh in levels)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, 'wb') as f:
        f.write(b'BTEX' + struct.pack('<IIII', w, h, len(levels), int(round(aspect * 1000))) + body)

def main():
    sd, out = sys.argv[1], sys.argv[2]
    cache = os.path.join(out, 'cache')
    asked = 0
    for folder, repo in SYSTEMS.items():
        d = os.path.join(sd, 'roms', folder)
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            base, ext = os.path.splitext(f)
            if ext.lower() not in EXT: continue
            dst = os.path.join(out, 'jaquettes', folder, base + '.btex')
            if os.path.exists(dst):
                print('déjà prêt :', folder, base); continue
            got = None
            for name in candidates(base):
                if asked: time.sleep(PAUSE)                  # one request at a time, with a pause
                path, net = fetch(repo, name, cache)
                asked += 1 if net else 0
                if path: got = path; break
            if not got:                                      # the name differs a bit: look in the system's list
                if asked: time.sleep(PAUSE)
                names, net = tree(repo, cache)
                asked += 1 if net else 0
                name = best_match(base, names)
                if name:
                    time.sleep(PAUSE)
                    got, net = fetch(repo, name, cache)
                    asked += 1 if net else 0
            if not got:
                print('pas de jaquette trouvée :', folder, base); continue
            btex(got, dst)
            print('jaquette :', folder, base, '<-', os.path.basename(got))
    # the native ports: their cover is the original game's (« Midnight Club 3: DUB Edition (Europe) » ->
    # « Midnight Club 3 - DUB Edition (Europe) », libretro's way), output jaquettes/ports/<nro name>.btex
    for folder, repo in PORTS.items():
        d = os.path.join(sd, 'switch', folder)
        if not os.path.isdir(d): continue
        for f in sorted(os.listdir(d)):
            base, ext = os.path.splitext(f)
            if ext.lower() != '.nro': continue
            dst = os.path.join(out, 'jaquettes', 'ports', base + '.btex')
            if os.path.exists(dst):
                print('déjà prêt : ports', base); continue
            title = nacp_title(os.path.join(d, f))
            if not title:
                print('port sans nom lisible :', f); continue
            want = title.replace(': ', ' - ')
            if asked: time.sleep(PAUSE)
            got, net = fetch(repo, libretro_name(want), cache)
            asked += 1 if net else 0
            if not got:
                if asked: time.sleep(PAUSE)
                names, net = tree(repo, cache)
                asked += 1 if net else 0
                name = best_match(libretro_name(want), names)
                if name:
                    time.sleep(PAUSE)
                    got, net = fetch(repo, name, cache)
                    asked += 1 if net else 0
            if not got:
                print('pas de jaquette trouvée : ports', base, '(' + want + ')'); continue
            btex(got, dst)
            print('jaquette : ports', base, '<-', os.path.basename(got))
    print('requêtes envoyées :', asked)

if __name__ == '__main__':
    main()
