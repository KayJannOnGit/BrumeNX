// Brume - v83: Réglages > Système > Date et heure, Brume's own pages (Nintendo's Date & Time page lives inside the HOME menu
// Brume replaces, it cannot open). They come over the settings list (it steps back to the left and fades), in the PS2 look:
// words floating in the fog, the title in yellow, the chosen value near-white in its halo, a soft band of light.
//  - « Régler la date et l'heure »: five wheels (jour, mois, année, heure, minute) that turn like real ones - each notch a
//    small tick on a note of the chord sounding now and a light HD click; ◀ ▶ the wheel, ▲ ▼ (held: it repeats) or the finger
//    turns it; A writes the time (time:a, Clock.cpp), B leaves. « Heure automatique » goes off when a time is chosen by hand.
//  - « Fuseau horaire »: the time service's list of places, sorted, by region (◀ ▶ the previous / next region, ▲ ▼ the
//    places); the chosen one says its UTC offset and the time there; the current one has the accent's dot. A sets it: the
//    shown clock follows at once.
// Memory: only while a page is there (the places ~22 KB, one zone rule 16 KB), freed once it has gone. Defensive: a bad
// entry of the list (not a place name) is left out, never drawn, never sent.
#pragma once
#include "Ui.hpp"

namespace brume::datepages {

    using Done = void (*)(float now);
    void OpenPicker(float now, Done onSet);     // onSet: after a time was written by hand (Settings: « Heure automatique » off)
    void OpenZones(float now, Done onSet);      // onSet: after the zone changed (Settings: the row reads its new name)
    bool Open();                                // a page takes the settings page's input
    float Shown();                              // 0..1 how much it is there (the list behind steps back)
    void Move(int dc, int dr, float now);       // dr -1 = up, +1 = down (as Settings::Move)
    void Held(int dr, float now);               // the direction held now (0: none): ▲ ▼ repeat
    void PressA(float now);
    void PressB(float now);
    void Tap(float x, float y, float now);      // (outside the page, on the left: B)
    void DragBegin(float x, float y);
    void DragTo(float rows);                    // the finger moved up by `rows` rows of 60 px (Settings::ListDragTo)
    void Update(float now, float dt);
    void Draw(Ui &ui, float now, float alpha, float slide, bool xmb);   // alpha: the settings page's, slide: its slide in
    void Hints(const char *&aL, const char *&bL, const char **chip, const char **lbl, int &nr);   // the page's hints

} // namespace brume::datepages
