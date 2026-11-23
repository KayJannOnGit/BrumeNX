// Brume - v81: the themes become REAL themes (Yass: « Thème Y2K : concrètement c'est juste une couleur, c'est pas ouf »;
// then « Y2K Chrome s'appeler juste Chrome, et toute la console doit devenir un jouet de chrome »).
// A theme is a whole look, not a hue: the fog's character (density, shape, pace), the drifting lights (how many, their
// pace, their strength), the dust's sparkle, the room's darkness, the cubes' reflections, the stars and comets, the
// interface's colours (ps2:: palette, the accent). Every number glides (a spring, Ease.hpp): a theme change crossfades.
//   Brume          the default look, exactly as before v81 (every shader number at 0)
//   Nuit profonde  very dark and calm: thin slow smooth fog, one or two dim lights, little dust, dim stars
//   Chrome         the console becomes a chrome toy: a LIQUID CHROME background instead of the fog (haze_fsh), mercury
//                  drops that merge and split and lean in the wheel's wind, polished chrome cubes and pebbles, a thin
//                  chrome ring around the chosen console, comets that leave chrome sparkles, A ripples the liquid
// Also here (v81, Yass: « on ne les voit pas vraiment »): « Ciel selon l'heure » (colour, light and density by the hour)
// and « La batterie, c'est la brume » (low: thicker, darker, amber; charging: a slow visible breath), with a short
// notification when each kicks in. The shaders' numbers ride in the Scene block's free halves (see common.glsl).
#pragma once

namespace brume::theme {

    enum { kBrume = 0, kNuit = 1, kChrome = 2, kCount = 3 };
    void  Set(int v);                    // the chosen theme: everything glides to it (~2.5 s)
    void  Snap(int v);                   // at boot: no glide
    int   Current();
    // once a frame (Entry FillScene): glides the look, reads the battery, follows the sky, colours the interface, and
    // writes the shaders' numbers. ambient = uAmbient (rgb + light, in/out), haze = uHaze (strength in/out),
    // bo / cubeFx = the blocks whose free halves carry the theme (zw / w), wind = the wheel's wind (Wheel::WindDrift)
    void  Frame(float now, float dt, float *ambient, float *haze, float (*bo)[4], float (*cubeFx)[4], bool psmOk, float wind);
    void  PressA(float now);             // Chrome: the liquid ripples from the console, like mercury touched
    float Chrome();                      // 0..1: how much Chrome is there (the stars, comets and ring follow it)
    float StarsK();                      // the stars' amount (1 = Brume)
    float CometsK();                     // the comets' amount (1 = Brume)
    // the sky through the day: its colour (linear rgb), and (optional) the fog's light (x 1) and density (+) at hour h
    void  SkyAt(float h, float *rgb, float *light = nullptr, float *dens = nullptr);
    float Hour(float h);                 // the local hour (uisim: « demo heure » forces it)

} // namespace brume::theme
