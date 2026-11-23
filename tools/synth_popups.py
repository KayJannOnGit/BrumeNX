#!/usr/bin/env python3
# Brume - v78: the popups' sounds, made from scratch (numpy only, nothing recorded, nothing borrowed).
# Yass's rules: soft, never sharp, nothing that stings or frightens, « tout est dans l'accord ». So each file is ONE note
# (plus breath / air, which has no pitch) recorded at a reference pitch; the menu replays it faster or slower so that it
# lands on a note of the chord sounding now (Popup.cpp: audio::HarmonyNote), and builds the little figures (open = two
# notes of the chord, OK = a rising interval) from several plays. Every file ends in its own small dark room (a reverb).
#   pop_open   D5  a breath of mist that opens and a glass note blooming in it (the popup comes in)
#   pop_close  D4  the breath going back, a muted round note (B, « Annuler », the popup goes)
#   pop_move   D5  the smallest round « tup » with a glint (the focus moves to the other answer)
#   pop_ok     D5  a soft mallet on glass (OK / the action chosen; played twice, a rising interval)
#   pop_armed  D4  a deletion's « Oui » arms: a swell that lights up, alive (a slow shimmer), then rests
#   pop_thud   D2  the deletion itself: a soft low thud, felt more than heard, with its low note (harmonics for the small speaker)
#   pop_error  D3  an error: « doum-doum », two muted round hits on the same note (never an alarm)
# 16-bit mono; 16 kHz (12 kHz for the low ones: memory), mastered at the level of the other Brume sounds.
#   python3 tools/synth_popups.py <out dir>        -> copy the 7 files to sdmc:/brume/sons/ (scripts/package.sh does it)
import sys, wave
import numpy as np

rng = np.random.default_rng(20261117)

def mtof(m): return 440.0 * 2 ** ((m - 69) / 12)
def smooth(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)

def onepole_lp(x, cut, sr):
    """a one-pole low-pass whose cutoff may move (an array, Hz)"""
    cut = np.broadcast_to(np.asarray(cut, dtype=float), x.shape)
    a = 1 - np.exp(-2 * np.pi * cut / sr)
    y = np.empty_like(x); s = 0.0
    for i in range(len(x)):
        s += (x[i] - s) * a[i]; y[i] = s
    return y

def note(sr, dur, f, parts, att, tau, start=0.0, drop=None):
    """a note: partials (ratio, amp, tau scale), raised-cosine attack (no click), exponential decay;
    drop = (from ratio, seconds): the pitch glides down to f at the start (a mallet, a thud)"""
    n = int(sr * dur); t = np.arange(n) / sr
    tt = np.clip(t - start, 0, None)
    on = (t >= start).astype(float)
    fr = np.full(n, f)
    if drop: fr = f * (1 + (drop[0] - 1) * np.exp(-tt / drop[1]))
    ph = 2 * np.pi * np.cumsum(fr) / sr
    out = np.zeros(n)
    for ratio, amp, ts in parts:
        out += amp * np.sin(ratio * ph) * np.exp(-tt / (tau * ts))
    a = np.where(tt < att, 0.5 - 0.5 * np.cos(np.pi * np.clip(tt / att, 0, 1)), 1.0)
    return out * a * on

def breath(sr, dur, cut0, cut1, rise, att, tau, start=0.0):
    n = int(sr * dur); t = np.arange(n) / sr
    tt = np.clip(t - start, 0, None)
    cut = cut0 + (cut1 - cut0) * smooth(tt / rise)
    x = onepole_lp(onepole_lp(rng.uniform(-1, 1, n), cut, sr), cut, sr)
    env = smooth(tt / att) * np.exp(-np.clip(tt - att, 0, None) / tau) * (t >= start)
    return x * env

def room(x, sr, rt, wet, dark):
    """a small dark room: the signal convolved with decaying, low-passed noise (rt = seconds to -60 dB)"""
    n = int(sr * rt)
    t = np.arange(n) / sr
    ir = onepole_lp(rng.standard_normal(n), dark, sr) * np.exp(-6.9 * t / rt)
    ir[:int(sr * 0.012)] *= np.linspace(0, 1, int(sr * 0.012))      # (no early slap: it blooms)
    ir /= np.sqrt((ir ** 2).sum())
    m = len(x) + n
    y = np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(ir, m), m)[:len(x)]
    return x * (1 - wet) + y * wet * 2.2

def finish(x, sr, rms_target, fade=0.12):
    f = int(sr * fade)
    x = x.copy(); x[-f:] *= np.linspace(1, 0, f) ** 2               # a clean end
    w = x[:int(sr * 0.15)]
    return x * (rms_target / max(1e-9, np.sqrt((w ** 2).mean())))

def write(path, x, sr):
    pcm = np.clip(np.round(x * 32767), -32767, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())

def pop_open():
    sr, d, f = 16000, 1.2, mtof(74)
    x = 0.30 * breath(sr, d, 300, 2400, 0.25, 0.12, 0.30)
    x += note(sr, d, f, [(1, 1.0, 1), (2, 0.30, 0.6), (3, 0.10, 0.4), (4.01, 0.04, 0.3), (0.5, 0.22, 1.2)], 0.045, 0.42, start=0.04)
    return finish(room(x, sr, 1.0, 0.38, 2200), sr, 0.0060), sr

def pop_close():
    sr, d, f = 12000, 0.8, mtof(62)
    x = 0.28 * breath(sr, d, 1800, 300, 0.2, 0.02, 0.16)
    x += note(sr, d, f, [(1, 1.0, 1), (2, 0.22, 0.5), (3, 0.06, 0.35)], 0.012, 0.16, start=0.01)
    return finish(room(x, sr, 0.7, 0.30, 1600), sr, 0.0040), sr

def pop_move():
    sr, d, f = 16000, 0.5, mtof(74)
    x = note(sr, d, f, [(1, 1.0, 1), (2, 0.18, 0.6), (3, 0.05, 0.4)], 0.004, 0.075, drop=(1.03, 0.012))
    x += note(sr, d, f, [(2, 0.07, 1), (3.0, 0.025, 0.7)], 0.006, 0.18)       # the glint
    return finish(room(x, sr, 0.5, 0.22, 2600), sr, 0.0035), sr

def pop_ok():
    sr, d, f = 16000, 1.0, mtof(74)
    x = note(sr, d, f, [(1, 1.0, 1), (2, 0.28, 0.6), (3, 0.09, 0.35), (4.2, 0.035, 0.2)], 0.005, 0.30, drop=(1.012, 0.01))
    return finish(room(x, sr, 0.8, 0.32, 2400), sr, 0.0055), sr

def pop_armed():
    sr, d, f = 16000, 1.4, mtof(62)
    n = int(sr * d); t = np.arange(n) / sr
    swell = smooth(t / 0.13) * np.where(t < 0.22, 1.0, np.exp(-(t - 0.22) / 0.38))
    trem = 1 + 0.12 * np.sin(2 * np.pi * 6.5 * t)                        # alive: a slow shimmer
    ph = 2 * np.pi * f * t
    x = (np.sin(ph) + 0.40 * np.sin(2 * ph) + 0.14 * np.sin(3 * ph) + 0.05 * np.sin(5 * ph)) * swell * trem
    x += 0.06 * np.sin(4 * ph) * smooth((t - 0.05) / 0.2) * np.exp(-np.clip(t - 0.25, 0, None) / 0.3) * (1 + 0.5 * np.sin(2 * np.pi * 11 * t))
    x += 0.20 * breath(sr, d, 400, 3000, 0.12, 0.10, 0.12)               # the light coming on: a rising breath
    return finish(room(x, sr, 1.1, 0.36, 2400), sr, 0.0075), sr

def pop_thud():
    sr, d, f = 12000, 1.2, mtof(38)
    x = note(sr, d, f, [(1, 1.0, 1), (2, 0.38, 0.8), (3, 0.16, 0.6)], 0.004, 0.18, drop=(2.2, 0.035))
    x += 0.5 * note(sr, d, f, [(1, 1.0, 1), (2, 0.32, 0.7), (3, 0.10, 0.5)], 0.02, 0.45)   # its low note, it rests
    n = int(sr * d); t = np.arange(n) / sr
    x += 0.18 * onepole_lp(rng.uniform(-1, 1, n), 500, sr) * np.exp(-t / 0.03)            # the soft pad of the hit
    return finish(room(x, sr, 0.9, 0.25, 900), sr, 0.014), sr

def pop_error():
    sr, d, f = 12000, 1.0, mtof(50)
    parts = [(1, 1.0, 1), (2, 0.45, 0.55), (3, 0.18, 0.35)]
    x = note(sr, d, f, parts, 0.006, 0.12, drop=(1.04, 0.015))
    x += 0.75 * note(sr, d, f, parts, 0.008, 0.20, start=0.17, drop=(1.04, 0.015))
    return finish(room(x, sr, 0.8, 0.26, 1500), sr, 0.012), sr

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else '.'
    total = 0
    for name, fn in [('pop_open', pop_open), ('pop_close', pop_close), ('pop_move', pop_move), ('pop_ok', pop_ok),
                     ('pop_armed', pop_armed), ('pop_thud', pop_thud), ('pop_error', pop_error)]:
        x, sr = fn()
        write('%s/%s.wav' % (out, name), x, sr)
        total += len(x) * 2
        print('%s.wav  %d Hz  %.2f s  crete %.3f' % (name, sr, len(x) / sr, np.abs(x).max()))
    print('total %d Ko' % (total // 1024))
