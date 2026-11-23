#!/usr/bin/env python3
# Brume - v38: two sounds made from scratch (pure Python, no library), for the new unlock (Yass: « les ding sont trop
# aigus, frustrants... fais un vrai truc incroyable quand on unlock »):
#   bowl.wav - a singing bowl struck softly, recorded at C4 (midi 60): round, low, long, NO sharp attack, no high
#              partial that stings. The engine replays it at any note of the chord (audio::Bowl).
#   rise.wav - the deep rise at the moment of the unlock: a sub-bass gliding up to D2 under a breath of air that
#              opens, felt more than heard (the engine transposes it into the console's key).
# 24 kHz, 16-bit, mono (like every Brume sound).   python3 tools/synth_sounds.py <out dir>
import math, random, struct, sys

RATE = 24000

def write_wav(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    k = 0.72 / peak
    data = b''.join(struct.pack('<h', max(-32767, min(32767, int(s * k * 32767)))) for s in samples)
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE * 2, 2, 16))
        f.write(b'data' + struct.pack('<I', len(data)) + data)

def bowl():
    f0 = 261.6256                                    # C4
    # a bowl's partials (inharmonic), each a close pair that beats slowly (the shimmer of a real bowl)
    parts = [(1.000, 1.00, 5.5, 0.55), (2.732, 0.30, 3.2, 0.9), (5.130, 0.09, 1.7, 1.4), (8.210, 0.025, 0.9, 2.0)]
    n = int(RATE * 7.0)
    out = [0.0] * n
    for ratio, amp, tau, beat in parts:
        f = f0 * ratio
        w1, w2 = 2 * math.pi * f / RATE, 2 * math.pi * (f + beat) / RATE
        for i in range(n):
            t = i / RATE
            env = math.exp(-t / tau)
            out[i] += amp * env * (math.sin(w1 * i) + 0.6 * math.sin(w2 * i + 1.3)) / 1.6
    att = int(RATE * 0.018)                          # a soft mallet: 18 ms raised cosine, no click
    for i in range(att):
        out[i] *= 0.5 - 0.5 * math.cos(math.pi * i / att)
    rel = int(RATE * 0.5)                            # and a clean end
    for i in range(rel):
        out[n - 1 - i] *= i / rel
    return out

def rise():
    n = int(RATE * 3.2)
    out = [0.0] * n
    rnd = random.Random(7)
    ph = 0.0
    lp = 0.0
    lp2 = 0.0
    f_end = 73.416                                   # D2
    for i in range(n):
        t = i / RATE
        # the sub: glides up a fifth to D2, swells for 1.3 s, then lets go
        g = min(1.0, t / 1.5)
        f = f_end * (0.667 + 0.333 * (1 - (1 - g) ** 2))
        ph += 2 * math.pi * f / RATE
        env = (min(1.0, t / 1.3) ** 2) * math.exp(-max(0.0, t - 1.3) / 0.7)
        sub = (math.sin(ph) + 0.35 * math.sin(2 * ph) + 0.12 * math.sin(3 * ph)) * env
        # the breath: noise through a lowpass that opens (200 Hz -> 2.2 kHz), quiet
        cut = 200 + 2000 * min(1.0, t / 1.4)
        a = 1 - math.exp(-2 * math.pi * cut / RATE)
        lp += (rnd.uniform(-1, 1) - lp) * a
        lp2 += (lp - lp2) * a
        air = lp2 * (min(1.0, t / 1.2) ** 3) * math.exp(-max(0.0, t - 1.35) / 0.45) * 0.9
        out[i] = sub * 0.85 + air
    fade = int(RATE * 0.02)
    for i in range(fade):
        out[i] *= i / fade
    return out

if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    write_wav(d + '/bowl.wav', bowl())
    write_wav(d + '/rise.wav', rise())
    print('ok')
