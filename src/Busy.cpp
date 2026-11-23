// Brume - v87: the orbiting lights. See Busy.hpp.
#include "Busy.hpp"
#include "Ease.hpp"
#include "Theme.hpp"
#include <cmath>

namespace brume::busy {

    namespace {
        constexpr int kSeg = 6;                          // trail pieces (every 0.1 s: 0.6 s of trail, like the overlay's 0.7)
        constexpr float kStep = 0.1f;
        float Lit(int k) { return 0.25f + 0.75f * powf(0.62f, (float)k); }   // the leading one brightest, the others fading behind
        // the trail's colour at age f (1 new .. 0 old): the overlay's Aged (f^3, f^2, f); Chrome: it ages into silver instead
        Rgba Aged(float f, float a) {
            const float chr = theme::Chrome();
            const float r = 0.8f * f * f * f, g = 0.9f * f * f, b = 1.0f * f;
            const float s = 0.62f + 0.38f * f;                   // (silver: grey-blue, dimmer with age)
            return Rgba{ r + (0.80f * s * f - r) * chr, g + (0.86f * s * f - g) * chr, b + (0.96f * s * f - b) * chr, a * (0.35f + 0.65f * f) };
        }
    }

    void OrbAt(int k, float t, float cx, float cy, float rad, float &x, float &y) {
        const float ang = t * 1.6f + k * 1.0471976f, dx = 0.50f * sinf(t * 0.23f), dy = 0.50f * sinf(t * 0.17f + 1.3f);
        x = cx + sinf(ang + dx) * rad; y = cy - cosf(ang + dy) * rad * 0.92f;
    }

    void Orb(Ui &ui, int k, float cx, float cy, float rad, float a, float t, Rgba tint) {
        if (a <= 0.003f || rad < 0.5f) return;
        const float lit = Lit(k), sc = fmaxf(0.55f, fminf(1.6f, rad / 22.0f));   // (sizes follow the ring, within reason)
        float hx, hy;
        OrbAt(k, t, cx, cy, rad, hx, hy);
        const float g = 15.0f * sc;                                              // the glow under it (the overlay's Splat 16)
        ui.Light(hx - g, hy - g, hx + g, hy + g, ps2::A(tint, 0.55f * lit * a), true);
        float lx = hx, ly = hy;
        for (int i = 1; i <= kSeg; i++) {                                        // the trail, back in time
            float x, y;
            OrbAt(k, t - i * kStep, cx, cy, rad, x, y);
            const float f = 1.0f - (i - 0.5f) / (kSeg + 1.0f);
            ui.Streak(lx, ly, x, y, 1.9f * sc * (0.6f + 0.4f * f), Aged(f, 1.5f * lit * a));
            lx = x; ly = y;
        }
        const float r = 2.3f * sc;                                               // the light itself, near-white
        ui.Shape(hx - r, hy - r, hx + r, hy + r, 0, 0, 2 * r, 2 * r, ps2::A(ps2::Mix(ps2::kChosen, tint, 0.15f), (0.35f + 0.65f * lit) * a), 24, 2 * r, 2 * r, r);
    }

    void Orbs(Ui &ui, float cx, float cy, float rad, float a, float t, Rgba tint) {
        if (a <= 0.003f) return;
        for (int k = 5; k >= 0; k--) Orb(ui, k, cx, cy, rad, a, t, tint);      // (the leading one last: on top)
    }

    float Fade::Step(bool on, float now, float delay) {
        const float dt = last < 0 ? 0.0f : fminf(0.1f, fmaxf(0.0f, now - last));
        last = now;
        if (on) { if (since < 0) since = now; } else since = -1;
        Glide(k, v, on && now - since >= delay ? 1.0f : 0.0f, dt, on ? 9.0f : 6.0f);
        return k < 0 ? 0.0f : (k > 1 ? 1.0f : k);
    }

}
