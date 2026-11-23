// Brume - v87: the battery and the charger, lovingly (Yass: « des petits détails cools »). When the charger is plugged in or
// out: a short soft notification (« En charge · 64 % » / « Sur batterie · 63 % »), a brief light from the bottom of the
// screen (where the cable goes in: it rises when the power comes, it sinks back when it goes), a few notes of the chord and a
// haptic tick. Also: the battery full while charging, and low (20 %, then 10 %) on battery - once each, an amber light.
// Read from psm while the menu is on screen (Frame is not called while a game is in front: zero cost then): the charger
// twice a second (the light must come when the cable goes in), the level every 3 s (v96) and at once when the charger
// changes; the first reading is the state at start (nothing said). The lock screen shows the level (Pct, Charging).
// v96 (Yass: « un meilleur indicateur de batterie dans la barre d'en haut ») - the top bar's battery, left of the date and
// time: five small lights that fill (the last one partly) and the level written beside them; charging, a light flows along
// them into the last one; full and charging, « Chargée » in green; low (15 % or less, on battery) they turn pink and a soft
// halo breathes. A touch on it says the details (the level, charging or not, the charger: fast or slow USB).
#pragma once
#include "Ui.hpp"

namespace brume::charge {

    void  Update(float now, bool psmOk, bool locked);   // once per frame
    void  Draw(Ui &ui, float now);                      // the light from the bottom (over everything but the notifications)
    bool  Known();                                      // a reading came
    int   Pct();                                        // 0..100
    bool  Charging();
    float PulseK(float now);                            // 0..1 the light's strength now (the lock screen's text follows it)
    // v96 - the top bar's battery: drawn right-aligned on `right` (its vertical centre cy), alpha a; returns the room it takes
    // (its gap to the date included; 0 while the level is not known)
    float DrawBar(Ui &ui, float now, float right, float cy, float a);
    float BarWidth();                                   // the room the last DrawBar took (the task centre's mark goes left of it)
    bool  BarHit(float x, float y);                     // a touch on it
    void  Tap(float now);                               // the details, in a notification
    int   Charger();                                    // 0 none, 1 fast (enough power), 2 slow (USB), 3 not supported

}
