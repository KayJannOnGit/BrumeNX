// Brume - v87: the orbiting lights (Yass, about the overlay's power menu: « super stylés, on doit les utiliser ailleurs »).
// The overlay's ring of six lights (overlay/source/main.cpp, OrbPos / OrbsLight / OrbsDraw), in the menu: six small lights
// turning on a ring that seems to tilt in space (its sine and cosine drift apart), each with a short trail that ages into
// blue (red fades first, then green: the PS2's clock), the leading one brightest, a soft glow under each.
// It is Brume's « busy » mark (the task centre, a cover on its way, the album / the apps loading, the clock asking Internet)
// and its emblem (the power-on, the unlock's burst).
// Cost: 6 x (1 glow + kSeg streaks + 1 dot) interface quads, nothing else.
#pragma once
#include "Ui.hpp"

namespace brume::busy {

    // cx, cy: its centre; rad: its radius (px); a: how much it shows; t: its own clock (seconds: now, or a clock that slows
    // down when the work rests); tint: its glow (ps2::kHalo by default: working; ps2::kPink: stuck; ps2::kGreen: done)
    void Orbs(Ui &ui, float cx, float cy, float rad, float a, float t, Rgba tint);
    inline void Orbs(Ui &ui, float cx, float cy, float rad, float a, float t) { Orbs(ui, cx, cy, rad, a, t, ps2::kHalo); }
    // where light k (0..5) of the ring is at time t (the boot's emblem lights them one by one)
    void OrbAt(int k, float t, float cx, float cy, float rad, float &x, float &y);
    // only light k (the boot's emblem: they come one after the other), lit 0..1
    void Orb(Ui &ui, int k, float cx, float cy, float rad, float a, float t, Rgba tint);

    // a fade that only shows when the work lasts: `on` must stay true `delay` seconds before it comes (then it glides in),
    // it glides out when the work is done - a quick load never flashes a ring. Call Step once per frame.
    struct Fade {
        float since = -1, k = 0, v = 0, last = -1;
        float Step(bool on, float now, float delay = 0.25f);
    };

}
