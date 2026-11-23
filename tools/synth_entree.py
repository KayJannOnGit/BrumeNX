#!/usr/bin/env python3
# Brume - v80: the entry and exit sounds of a launch (Yass: « Lancer / quitter une app ou un jeu : l'animation de sortie
# est propre mais saccade un peu, et le son se coupe d'un coup -> un vrai son d'entrée et de sortie, très stylé, pour tous
# les logiciels et jeux »). Made from scratch (numpy only, nothing recorded, nothing borrowed).
# Each sound has two layers, so its timing never moves with the key:
#   - the AIR (noise and soft thuds, no pitch: it belongs to every chord) is played as it is, in time with the picture;
#   - the TONE (root, fifth, octave, ninth: in every chord of the wheel) is recorded on D and replayed in the chosen
#     console's key (Audio.cpp: Keyed). It ends sooner than the air, so even replayed slower (the lowest key, x 0.75) it is
#     silent before the game takes the screen (audio::kEntryLen).
#   entree_plongee  a portable: the dive into its screen - a big soft woosh rushing in with the camera, the air goes
#                   muffled as the screen goes black (we are inside), a deep round « whomp », then silence
#   entree_salon    a home console: it turns (« vwoom » following the spin, which brakes), then a soft low « thoom » as
#                   the light goes out, then silence
#   entree_monte    the dive's tone: D3 rising to D4 with the camera, it opens on D4-A4-D5, then it is gone
#   entree_descend  the home console's tone: D4 falling to D3 like a machine powering down, then gone
#   sortie          back from the game: a breath drawn in from the silence, the light blooming open, a long airy tail
#   sortie_ton      its tone: a soft glass bloom D4-A4-D5-E5 (a slow arpeggio) with a few grains of crystal, it rests
# 16-bit mono; 16 kHz (the air), 12 kHz (the tones: memory). All six: ~230 KB.
#   python3 tools/synth_entree.py <out dir>      -> copy the 6 files to sdmc:/brume/sons/ (scripts/package.sh does it)
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from synth_popups import mtof, smooth, onepole_lp, room, write   # (the same small dark room as the popups)

rng = np.random.default_rng(20261180)
ENTRY = 1.25        # = audio::kEntryLen: the game is started this long after the entry sound (it must be silent by then)

def glide(p):
    """the menu's one motion (Ease.hpp EaseGlide): speeds up smoothly, brakes for a long time"""
    p = np.clip(p, 0, 1); a = 7.0 * p
    return (1 - (1 + a) * np.exp(-a)) / (1 - 8.0 * np.exp(-7.0))

def svf_bp(x, fc, q, sr):
    """a band-pass whose centre may move (state-variable filter, unity gain at the centre)"""
    fc = np.broadcast_to(np.asarray(fc, dtype=float), x.shape)
    g = np.tan(np.pi * np.clip(fc, 20, sr * 0.45) / sr); k = 1.0 / q
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2
    y = np.empty_like(x); ic1 = ic2 = 0.0
    for i in range(len(x)):
        v3 = x[i] - ic2
        v1 = a1[i] * ic1 + a2[i] * v3
        v2 = ic2 + a2[i] * ic1 + a3[i] * v3
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2
        y[i] = k * v1
    return y

def thud(sr, n, at, f, tau, drop=0.8):
    """a soft round low knock: a sine falling onto f (harmonics: heard on the small speaker too), no click"""
    t = np.arange(n) / sr; tt = np.clip(t - at, 0, None); on = t >= at
    fr = f * (1 + drop * np.exp(-tt / 0.05))
    ph = 2 * np.pi * np.cumsum(fr * on) / sr
    x = (np.sin(ph) + 0.32 * np.sin(2 * ph) + 0.12 * np.sin(3 * ph)) * np.exp(-tt / tau) * smooth(tt / 0.018) * on
    return x + 0.35 * onepole_lp(rng.uniform(-1, 1, n), 320, sr) * np.exp(-tt / 0.05) * smooth(tt / 0.006) * on

def tone(sr, t, midi, parts, chorus=0.004):
    """a soft glass voice on a (moving) note: partials (ratio, amp), two copies a few cents apart (it breathes)"""
    f = mtof(np.asarray(midi, dtype=float)) * np.ones_like(t)
    out = np.zeros_like(t)
    for det in (1 - chorus / 2, 1 + chorus / 2):
        ph = 2 * np.pi * np.cumsum(f * det) / sr
        for ratio, amp in parts:
            out += amp * np.sin(ratio * ph + ratio)
    return out * 0.5

def master(x, sr, peak, end):
    """the level (its peak), and a clean end: faded to nothing at `end` seconds, silence after"""
    x = x * (peak / max(1e-9, np.abs(x).max()))
    t = np.arange(len(x)) / sr
    f0 = end - 0.30
    x = x * np.where(t < f0, 1.0, np.clip(1 - (t - f0) / 0.30, 0, 1) ** 2)
    return x[:int(sr * (end + 0.02))]

def entree_plongee():
    sr, d = 16000, 1.6
    n = int(sr * d); t = np.arange(n) / sr
    tp = 0.20                                            # the rush peaks with the camera (the dive is an ease-out)
    after = np.clip(t - tp, 0, None)
    env = np.where(t < tp, smooth(t / tp) ** 1.5, np.exp(-after / 0.16))
    fc = np.where(t < tp, 320 * (2600 / 320) ** (t / tp), 380 + 2220 * np.exp(-after / 0.10))   # inside: muffled
    air = svf_bp(rng.standard_normal(n), fc, 0.8, sr) * env
    top = np.where(t < tp, 1.0, np.exp(-after / 0.05))                                          # the rush's airy top
    air += 0.40 * svf_bp(rng.standard_normal(n), fc * 2.2, 1.4, sr) * env * top
    air *= 1 + 0.12 * np.sin(2 * np.pi * (11 * t + 9 * t * t))                                  # the air flutters
    x = air + 0.50 * thud(sr, n, 0.40, 52, 0.17)                                                # black: « whomp »
    x = room(x, sr, 0.9, 0.34, 1700)
    return master(x, sr, 0.26, ENTRY - 0.08), sr

def entree_salon():
    sr, d = 16000, 1.6
    n = int(sr * d); t = np.arange(n) / sr
    T = 0.80 / 1.35                                      # the console's turn (Wheel: 1.1 pi over Pace(.8), EaseGlide)
    ang = 1.1 * np.pi * glide(t / T)
    speed = np.gradient(ang, 1 / sr); speed /= speed.max()
    face = 0.45 + 0.55 * np.cos(2 * ang + 0.6) ** 2                                             # a face sweeps by
    env = speed ** 0.8 * face
    fc = 420 + 1100 * speed * (0.6 + 0.4 * face)                                                # brighter as it passes
    air = svf_bp(rng.standard_normal(n), fc, 1.1, sr) * env
    air += 0.25 * svf_bp(rng.standard_normal(n), fc * 2.6, 2.0, sr) * env ** 2
    x = air + 0.75 * thud(sr, n, 0.46, 46, 0.24, drop=0.6)                                     # the light goes out
    tt = np.clip(t - 0.46, 0, None)
    x += 0.20 * onepole_lp(rng.uniform(-1, 1, n), 700, sr) * smooth(tt / 0.03) * np.exp(-tt / 0.12) * (t >= 0.46)
    x = room(x, sr, 1.0, 0.36, 1500)
    return master(x, sr, 0.24, ENTRY - 0.08), sr

def entree_monte():
    sr, d = 12000, 0.9
    t = np.arange(int(sr * d)) / sr
    m = 50 + 12 * glide(t / 0.24)                        # D3 -> D4 with the camera
    v = tone(sr, t, m, [(1, 1.0), (2, 0.45), (3, 0.20), (4, 0.08)])
    bloom = smooth((t - 0.16) / 0.10)                    # it opens: D5, A5 (the octave, the fifth above)
    v += bloom * tone(sr, t, m + 12, [(1, 0.40), (1.5, 0.28), (2, 0.10)], 0.006)
    env = smooth(t / 0.07) * (0.55 + 0.45 * smooth(t / 0.22)) * np.exp(-np.clip(t - 0.26, 0, None) / 0.12)
    x = room(v * env, sr, 0.6, 0.30, 2600)
    return master(x, sr, 0.11, 0.84), sr

def entree_descend():
    sr, d = 12000, 0.9
    t = np.arange(int(sr * d)) / sr
    m = 62 - 12 * glide((t - 0.04) / 0.60)               # D4 -> D3, braking like the spin
    dark = 1 - 0.7 * glide(t / 0.6)                      # the high partials go first (powering down)
    v = tone(sr, t, m, [(1, 1.0), (2, 0.40), (3, 0.16), (1.5, 0.22)])
    v = v * (0.6 + 0.4 * dark) + tone(sr, t, m, [(4, 0.06), (6, 0.03)]) * dark
    env = smooth(t / 0.05) * np.exp(-np.clip(t - 0.10, 0, None) / 0.17)
    x = room(v * env, sr, 0.6, 0.28, 2000)
    return master(x, sr, 0.10, 0.84), sr

def sortie():
    sr, d = 16000, 1.8
    n = int(sr * d); t = np.arange(n) / sr
    tb = 0.34                                            # the bloom: the light is back
    after = np.clip(t - tb, 0, None)
    env = np.where(t < tb, (t / tb) ** 2.4, np.exp(-after / 0.42))
    fc = np.where(t < tb, 300 * (2600 / 300) ** (t / tb), 1500 + 1100 * np.exp(-after / 0.3))
    x = svf_bp(rng.standard_normal(n), fc, 0.9, sr) * env
    shimmer = smooth(after / 0.04) * np.exp(-after / 0.28) * (t >= tb)                          # the light, airy
    x += 0.30 * svf_bp(rng.standard_normal(n), 5200, 1.2, sr) * shimmer
    x += 0.30 * onepole_lp(rng.uniform(-1, 1, n), 220, sr) * smooth(after / 0.05) * np.exp(-after / 0.22) * (t >= tb)
    x = room(x, sr, 1.2, 0.40, 2400)
    return master(x, sr, 0.13, 1.70), sr

def sortie_ton():
    sr, d = 12000, 2.4
    t = np.arange(int(sr * d)) / sr
    x = np.zeros_like(t)
    settle = -0.12 * np.exp(-t / 0.18)                   # it finds its note (a little flat at first, like a breath)
    for midi, amp, at in [(62, 1.0, 0.05), (69, 0.60, 0.10), (74, 0.45, 0.16), (76, 0.12, 0.24)]:   # D4 A4 D5 E5
        tt = np.clip(t - at, 0, None)
        env = smooth(tt / 0.32) * np.exp(-np.clip(tt - 0.34, 0, None) / 0.70) * (t >= at)
        x += amp * env * tone(sr, t, midi + settle, [(1, 1.0), (2, 0.24), (3, 0.07)], 0.005)
    x *= 1 + 0.05 * np.sin(2 * np.pi * 4.5 * t)          # a slow shimmer
    for midi, amp, at in [(86, 0.10, 0.36), (93, 0.07, 0.44), (88, 0.05, 0.55), (98, 0.04, 0.68)]:  # grains of crystal
        tt = np.clip(t - at, 0, None)
        ph = 2 * np.pi * mtof(midi) * tt
        x += amp * (np.sin(ph) + 0.2 * np.sin(2.76 * ph)) * smooth(tt / 0.004) * np.exp(-tt / 0.22) * (t >= at)
    x = room(x, sr, 1.6, 0.42, 3000)
    return master(x, sr, 0.10, 2.30), sr

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    total = 0
    for name, fn in [('entree_plongee', entree_plongee), ('entree_salon', entree_salon), ('entree_monte', entree_monte),
                     ('entree_descend', entree_descend), ('sortie', sortie), ('sortie_ton', sortie_ton)]:
        x, sr = fn()
        write('%s/%s.wav' % (out, name), x, sr)
        total += len(x) * 2
        print('%s.wav  %d Hz  %.2f s  crete %.3f' % (name, sr, len(x) / sr, np.abs(x).max()))
    print('total %d Ko' % (total // 1024))
