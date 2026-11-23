// Brume - v87: the lock code, like the PS4's (Yass: « un vrai PIN pour unlock comme sur la PS4 »).
// The PS4's login passcode is four presses taken from ten buttons (the four directions, L1, L2, R1, R2, triangle, square: X
// and O stay « confirm » and « back »); the screen shows a key of the buttons and four places that fill as you press, it is
// typed twice to be set, and the current one is asked before it is changed or deleted. Brume's: four presses taken from ten
// buttons too - ▲ ▶ ▼ ◀ (the D-pad or the left stick), L, R, ZL, ZR, X, Y (A and B stay « confirm » and « back ») - each
// code a symbol 0..9 (0..3 = the directions of the v44 code: an old codeseq= is still a valid code). The key is drawn like
// a controller (the directions on the left, the shoulders above, X and Y on the right): it is also the touch pad.
// Codes are per profile: sdmc:/brume/codes.txt, one line per account « <uid in 32 hex> <4 symbols> » (or « - »: this profile
// has none, even if the old global code of reglages.ini is on). Parsed defensively (a broken line is skipped).
#pragma once
#include "Ui.hpp"
#include <switch.h>

namespace brume::pin {

    constexpr int kLen = 4, kSyms = 10;
    int  FromButtons(u64 down);                          // the symbol pressed this frame (0..9), -1 = none
    const char *Name(int sym);                           // « haut », « L », « ZR »... (for the hints)

    // the codes
    void Load();                                         // reads codes.txt (once at start; again after a change)
    bool Get(AccountUid uid, uint8_t out[kLen]);         // this profile's code (its own, else the old global one), false: none
    bool Own(AccountUid uid);                            // it has its own line (a code or « - »)
    bool Set(AccountUid uid, const uint8_t *code);       // nullptr: no code for this profile; written at once (false: SD error)

    // the drawing (PS2 look: lights in the fog, no box). Dots: n filled; `at[i]` when dot i was filled (its pop); shake px;
    // state 0 typing, 1 right (they glow), 2 wrong (pink)
    void DrawDots(Ui &ui, float cx, float cy, int n, const float *at, float shake, int state, float a, float now);
    // the key / touch pad centred on (cx, cy); `lit` = the symbol pressed last and when (it flashes)
    void DrawPad(Ui &ui, float cx, float cy, float a, int lit, float litAt, float now);
    int  PadHit(float cx, float cy, float x, float y);   // the symbol under the finger, -1 = none

}
