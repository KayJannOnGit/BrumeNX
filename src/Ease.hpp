// Brume - ONE motion everywhere (Yass). v34 was « ease out expo » (too fast). v35 (Yass: « une accélération puis
// une décélération progressive, usage possible pendant que ça continue de décélérer ») = a critically damped glide:
// it speeds up smoothly from rest, then slows down for a long time to a perfect stop, never overshoots; and when you
// press again while it is still braking, it KEEPS its speed and simply heads for the new place (no restart, no jerk).
// v36 (Yass: « trop lentes, accélère un peu, mais c'est ça l'idée ») = the same, kPace times quicker, everywhere.
//  - Glide(): the live version (the carousels, the pages, the console's slide, the Cover Flow, the grids).
//  - EaseGlide(): the same curve for one-shot animations; their lengths go through Pace().
//  - FlickTarget(): a carousel let go with the finger's speed settles on the next element (or a few, if thrown hard),
//    never past it (the glide keeps the finger's speed: no jerk at the release).
// To change the feel of the whole menu, change it here.
#pragma once
#include <cmath>

namespace brume {
    constexpr float kPace = 1.35f;                       // v36: everything a little livelier
    inline float Pace(float seconds) { return seconds / kPace; }

    // the exact step of a critically damped spring (stable for any dt)
    inline void Glide(float &x, float &v, float target, float dt, float omega) {
        omega *= kPace;
        const float h = dt < 0 ? 0 : (dt > 0.1f ? 0.1f : dt);
        const float d = x - target, c = v + omega * d, e = expf(-omega * h);
        x = target + (d + c * h) * e;
        v = (c - omega * (d + c * h)) * e;
    }
    constexpr float kGlideW = 7.0f;                      // the one-shot curve: 1 - (1 + wt) e^-wt over [0, 1]
    inline float EaseGlide(float p) {
        if (p <= 0) return 0;
        if (p >= 1) return 1;
        const float a = kGlideW * p;
        return (1.0f - (1.0f + a) * expf(-a)) / (1.0f - (1.0f + kGlideW) * expf(-kGlideW));
    }
    inline float EaseOutExpo(float p) { return EaseGlide(p); }   // (v34's name, kept: same motion everywhere)

    // where a thrown carousel stops: `v` = the finger's speed (elements per second, + = towards the next), `omega` = its
    // Glide's. A slow release settles on the nearest; a throw goes at least to the next one, further if it is fast, and
    // always far enough that the glide never overshoots (a critically damped spring passes its target only if
    // v > omega x distance).
    inline float FlickTarget(float cur, float v, float omega, float maxSteps = 4.0f) {
        if (fabsf(v) < 0.9f) return roundf(cur);
        const float proj = cur + v / (omega * kPace);
        float t = v > 0 ? ceilf(proj - 0.001f) : floorf(proj + 0.001f);
        if (t > roundf(cur) + maxSteps) t = roundf(cur) + maxSteps;
        if (t < roundf(cur) - maxSteps) t = roundf(cur) - maxSteps;
        return t;
    }

    // v80 (Yass: « l'animation de sortie est propre mais saccade un peu ») - back from a game / an app, the first frames are
    // slow (the console is still closing it, the menu's clock follows the real time: the staging jumped). The way back waits,
    // black, until 4 frames in a row took a 60th of a second (or `most` seconds of menu), then it starts: it never jumps.
    struct Settle {
        bool on = false; float t0 = 0; int n = 0;
        void Begin(float now) { on = true; t0 = now; n = 0; }
        bool Step(float now, float dt, float most = 0.45f) {      // true once: steady (or waited long enough)
            if (!on) return false;
            n = dt < 0.021f ? n + 1 : 0;
            if (n < 4 && now - t0 < most) return false;
            on = false;
            return true;
        }
    };
}
