#!/usr/bin/env python3
# Brume - v59: « Aurore », the second unlock sound (Réglages > Brume > Déverrouillage), made from scratch (numpy only).
#
# Yass's rules, from every past remark: NOTHING sharp or high (no « pshhht », no ding above F#5), no two notes a
# semitone apart, the heartbeat he loved (v38.2) at the start, everything deep, soft and wide, then « une grosse
# explosion de sons harmonieux ». In D major (the engine transposes it into the chosen console's key):
#   0.00  the heartbeat, « boum-boum »
#   0.05  a breath held: a low D-A fifth rising from nothing
#   0.45  the bloom: a wide Dsus2 chord (D A E, 6 voices x 3 detuned) whose filter opens like a sunrise, over a deep
#         boom gliding 60 -> 34 Hz
#   1.50  the shiver: the third (F#) blooms in, the choir (« aah ») sings it, a rain of light (D pentatonic, D4..F#5)
#   1.90  a warm low bell settles the chord; then everything drifts away in a dark 5 s reverb
# 24 kHz, 16-bit, stereo, ~9 s (the same size as unlock12.wav: the engine loads one or the other in the same place).
#   python3 tools/synth_aurore.py <out.wav> [unlock12.wav: its heartbeat sets the loudness] [bloom .3] [boom .4] [rest .6]
import sys, wave
import numpy as np

SR, DUR = 24000, 9.0
N = int(SR * DUR)
t = np.arange(N) / SR
rng = np.random.default_rng(20261117)
DRY = np.zeros((2, N)); WET = np.zeros((2, N))
REST = float(sys.argv[5]) if len(sys.argv) > 5 else 0.6   # everything after the heartbeat, relative to it

def mtof(m): return 440.0 * 2 ** ((m - 69) / 12)
def pan_gains(p): return np.cos((p + 1) * np.pi / 4), np.sin((p + 1) * np.pi / 4)
def place(sig, start, pan=0.0, dry=1.0, wet=0.0, rest=True):
    i0 = int(start * SR); n = min(len(sig), N - i0)
    if rest: sig = sig * REST
    if n <= 0: return
    gl, gr = pan_gains(pan)
    DRY[0, i0:i0 + n] += sig[:n] * gl * dry; DRY[1, i0:i0 + n] += sig[:n] * gr * dry
    WET[0, i0:i0 + n] += sig[:n] * gl * wet; WET[1, i0:i0 + n] += sig[:n] * gr * wet
def smooth(x): x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)
def adsr(tt, att, rel_at, rel_tau):
    a = smooth(tt / att)
    r = np.where(tt < rel_at, 1.0, np.exp(-(tt - rel_at) / rel_tau))
    return a * r

# ---- the heartbeat (« boum-boum »): a low note that falls, its 2nd and 3rd harmonics so the small speaker hears it
def beat(amp):
    n = int(SR * 0.7); tt = np.arange(n) / SR
    f = 42 + 30 * np.exp(-tt / 0.07)
    ph = 2 * np.pi * np.cumsum(f) / SR
    env = (1 - np.exp(-tt / 0.006)) * np.exp(-tt / 0.17)
    return amp * env * (np.sin(ph) + 0.32 * np.sin(2 * ph) * np.exp(-tt / 0.08) + 0.12 * np.sin(3 * ph) * np.exp(-tt / 0.05))
place(beat(1.0), 0.00, 0, 1.0, 0.15, rest=False)
place(beat(0.62), 0.27, 0, 1.0, 0.15, rest=False)

# ---- the breath held: D3 + A3, rising from nothing, cut into the bloom
n = int(SR * 0.5); tt = np.arange(n) / SR
held = (np.sin(2 * np.pi * mtof(50) * tt) + 0.7 * np.sin(2 * np.pi * mtof(57) * tt)) * (tt / 0.5) ** 3 * np.where(tt > 0.42, np.exp(-(tt - 0.42) / 0.03), 1)
place(0.10 * held, 0.05, 0, 0.6, 0.6)

# ---- the deep boom under the bloom: 60 -> 34 Hz (softer than the heartbeat, like the v38.2 one)
BOOM = float(sys.argv[4]) if len(sys.argv) > 4 else 0.4
n = int(SR * 3.0); tt = np.arange(n) / SR
f = 34 + 26 * np.exp(-tt / 0.45)
ph = 2 * np.pi * np.cumsum(f) / SR
env = (1 - np.exp(-tt / 0.02)) * np.exp(-tt / 1.3)
place(BOOM * env * (np.sin(ph) + 0.35 * np.sin(2 * ph) + 0.12 * np.sin(3 * ph)), 0.45, 0, 1.0, 0.25)

# ---- the bloom: a wide chord whose filter opens (fc 250 -> 1100 Hz in 1.1 s, then back to 520 Hz): mellow, never buzzy
T0 = 0.45
fc = np.where(t < T0, 250.0, 250 + 850 * smooth((t - T0) / 1.1))
fc = np.where(t > T0 + 1.1, 520 + 580 * np.exp(-(t - T0 - 1.1) / 1.8), fc)
def voice(midi, gain, start, att, pan, rel_at=3.1, rel_tau=1.5):
    out = np.zeros((2, N))
    tt = np.clip(t - start, 0, None)
    env = np.where(t >= start, adsr(tt, att, rel_at - start + T0, rel_tau), 0.0)
    for k, cents in enumerate((-7, 0, 7)):
        f0 = mtof(midi) * 2 ** (cents / 1200)
        ph0 = rng.uniform(0, 2 * np.pi)
        sig = np.zeros(N)
        for h in range(1, 64):
            fh = f0 * h
            if fh > 3000: break
            tilt = 1 / np.sqrt(1 + (fh / fc) ** 4)
            sig += np.sin(2 * np.pi * fh * t + ph0 * h) * tilt / h ** 2.0
        p = np.clip(pan + (k - 1) * 0.22, -1, 1)
        gl, gr = pan_gains(p)
        out[0] += sig * gl; out[1] += sig * gr
    return out * env * gain / 3
bloom = np.zeros((2, N))
for midi, gain, start, att, pan in ((38, 0.80, T0, 0.9, 0.0), (45, 0.62, T0, 0.9, -0.35), (50, 0.60, T0, 0.9, 0.35),
                                    (57, 0.46, T0, 0.9, -0.5), (64, 0.30, T0, 1.0, 0.5), (69, 0.20, T0 + 0.1, 1.0, -0.2),
                                    (54, 0.46, 1.50, 0.8, 0.25), (66, 0.26, 1.55, 0.8, -0.3)):   # the third arrives
    bloom += voice(midi, gain, start, att, pan)
BLOOM = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3
DRY += 0.55 * BLOOM * REST * bloom; WET += 0.6 * BLOOM * REST * bloom

# ---- the choir (« aah »): formants of the vowel a, a slow vibrato that comes in
def choir(midi, gain, start, pan):
    tt = np.clip(t - start, 0, None)
    vib = 1 + 0.0035 * smooth((tt - 0.8) / 1.0) * np.sin(2 * np.pi * 5.2 * tt)
    f0 = mtof(midi)
    ph = 2 * np.pi * np.cumsum(f0 * vib) / SR
    sig = np.zeros(N)
    for h in range(1, 40):
        fh = f0 * h
        if fh > 3400: break
        w = 1.0 * np.exp(-((fh - 730) / 110) ** 2) + 0.5 * np.exp(-((fh - 1090) / 130) ** 2) + 0.10 * np.exp(-((fh - 2440) / 180) ** 2) + 0.03
        sig += np.sin(h * ph) * w / h ** 0.6
    env = np.where(t >= start, adsr(tt, 1.3, 3.8 - start + 1.5, 1.9), 0.0)
    return sig * env * gain
for midi, gain, start, pan in ((57, 0.15, 1.45, -0.3), (62, 0.13, 1.5, 0.3), (66, 0.11, 1.6, 0.0)):
    c = choir(midi, gain, start, pan) * REST
    gl, gr = pan_gains(pan)
    DRY[0] += 0.3 * c * gl; DRY[1] += 0.3 * c * gr; WET[0] += 0.75 * c * gl; WET[1] += 0.75 * c * gr

# ---- the rain of light: D pentatonic, D4..F#5 at most, soft harp-like notes, deep in the reverb
PENTA = [62, 64, 66, 69, 71, 74, 76, 78]
seq = [0, 2, 4, 3, 5, 7, 6, 4, 5, 7, 6, 5, 3, 4, 2, 0]
def harp(midi, amp):
    n = int(SR * 3.0); tt = np.arange(n) / SR; f0 = mtof(midi)
    env = (1 - np.exp(-tt / 0.006)) * np.exp(-tt / 1.3)
    return amp * env * (np.sin(2 * np.pi * f0 * tt) + 0.25 * np.sin(4 * np.pi * f0 * tt) * np.exp(-tt / 0.5) + 0.05 * np.sin(6 * np.pi * f0 * tt) * np.exp(-tt / 0.25))
for k, idx in enumerate(seq):
    at = 1.35 + k * 0.125 + rng.uniform(-0.02, 0.02)
    place(harp(PENTA[idx], 0.055 + 0.02 * np.sin(np.pi * k / len(seq))), at, rng.uniform(-0.7, 0.7), 0.25, 0.9)

# ---- the warm low bell that settles the chord (D3, A3, D4)
def bell(midi, amp):
    n = int(SR * 6.0); tt = np.arange(n) / SR; f0 = mtof(midi)
    s = np.zeros(n)
    for r, a, tau in ((1.0, 1.0, 3.2), (2.0, 0.32, 1.8), (3.01, 0.10, 1.0), (4.2, 0.03, 0.6)):
        if f0 * r < 2600: s += a * np.exp(-tt / tau) * np.sin(2 * np.pi * f0 * r * tt)
    return amp * s * (1 - np.exp(-tt / 0.012))
place(bell(50, 0.14), 1.90, -0.15, 0.45, 0.7)
place(bell(57, 0.10), 1.96, 0.15, 0.45, 0.7)
place(bell(62, 0.07), 2.02, 0.0, 0.45, 0.7)

# ---- the dark reverb (5 s, its highs die first) and the master
def fft_filter(x, f):
    X = np.fft.rfft(x, axis=-1); fr = np.fft.rfftfreq(x.shape[-1], 1 / SR)
    return np.fft.irfft(X * f(fr), n=x.shape[-1], axis=-1)
ni = int(SR * 5.0); ti = np.arange(ni) / SR
ir = np.zeros((2, ni))
for c in range(2):
    bright = fft_filter(rng.standard_normal(ni), lambda fr: 1 / np.sqrt(1 + (fr / 2600) ** 4)) * np.exp(-ti / 0.55)
    dark = fft_filter(rng.standard_normal(ni), lambda fr: 1 / np.sqrt(1 + (fr / 1100) ** 4)) * np.exp(-ti / 1.5)
    ir[c] = (0.45 * bright + dark) * (1 - np.exp(-ti / 0.015))
pre = int(SR * 0.028); ir = np.concatenate([np.zeros((2, pre)), ir], axis=1)[:, :ni]
ir /= np.sqrt((ir ** 2).sum(axis=1, keepdims=True))
M = 1 << int(np.ceil(np.log2(N + ni)))
rev = np.fft.irfft(np.fft.rfft(WET, M, axis=-1) * np.fft.rfft(ir, M, axis=-1), M, axis=-1)[:, :N]
mix = DRY + 0.9 * rev
mix = fft_filter(mix, lambda fr: (fr / 30) ** 2 / (1 + (fr / 30) ** 2) / np.sqrt(1 + (fr / 6500) ** 6))   # 30 Hz .. 6.5 kHz
# v70.8 (Yass: « aurore se cut juste après l'unlock, et ça brusquement »): the tail no longer stops loud - from 5 s it
# fades out slowly (like the reverb dying, ~ -30 dB at the end), then the last 0.5 s go to silence
T0 = DUR - 4.0
mix *= np.where(t > T0, np.exp(-(t - T0) / 1.15), 1.0)
mix *= np.where(t > DUR - 0.5, 0.5 + 0.5 * np.cos(np.pi * (t - (DUR - 0.5)) / 0.5), 1.0)

def rms(x): return float(np.sqrt(np.mean(x ** 2)))
def peak_win(x, sr=SR):                     # the loudest 50 ms (mono)
    m = x.mean(axis=0) if x.ndim == 2 else x
    w = int(sr * 0.05)
    return max(rms(m[i:i + w]) for i in range(0, len(m) - w, w // 2))
target = 0.25
if len(sys.argv) > 2:                       # the heartbeat as loud as unlock12's
    with wave.open(sys.argv[2]) as w:
        ref = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64).reshape(-1, w.getnchannels()).T / 32768
    target = peak_win(ref)
beatwin = mix[:, : int(SR * 0.4)]
mix *= target / max(peak_win(beatwin), 1e-9)
bl = mix[:, int(SR * 0.6): int(SR * 3.0)]
print('bloom / battement : %.2f' % (peak_win(bl) / target))
mix = 0.95 * np.tanh(mix / 0.95)            # a soft ceiling, never a hard clip
pcm = (np.clip(mix, -1, 1) * 32767).astype('<i2').T.copy()
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())
hi = fft_filter(mix, lambda fr: (fr > 3000).astype(float))
print('ok %s : %.1f s, peak %.2f, rms %.3f (cible %.3f), part > 3 kHz %.1f dB' %
      (sys.argv[1], DUR, float(np.abs(mix).max()), rms(mix[:, : N // 2]), target, 20 * np.log10(rms(hi) / rms(mix) + 1e-12)))
