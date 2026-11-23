// Brume - v85: Réglages > Système > « Journal de Brume » (it replaces « Historique des erreurs », a Nintendo page that cannot
// open here). Brume's own page, over the settings list like Date et heure's (DatePages.cpp), in the PS2 look: the title in
// yellow, the lines floating in the fog, the chosen one near-white in its halo with its whole text under it.
// What it shows: the latest error and warning lines of sdmc:/brume/perf.log (Brume's own log) and of
// sdmc:/brume/overlay/journal.txt (Brume Overlay's), the newest first, the same line repeated counted once (« 12 fois »),
// with the start it came from (« ce démarrage », « démarrage précédent »...). For the testers' bug reports.
// Defensive: only the last bytes of each file are read (bounded), every line is cut to a fixed size, any byte that is not
// printable text becomes « ? »; a missing or unreadable file only means fewer lines. Memory: ~11 KB while the page is there,
// freed once it has gone. Nothing runs while it is closed.
#pragma once
#include "Ui.hpp"

namespace brume::journal {

    void Show(float now);                       // opens the page (reads the files now)
    bool Open();                                // the page takes the settings page's input
    float Shown();                              // 0..1 how much it is there (the list behind steps back)
    void Move(int dc, int dr, float now);       // ▲ ▼ a line, ◀ ▶ a page of lines
    void Held(int dr, float now);               // the direction held now (0: none): ▲ ▼ repeat
    void PressA(float now);                     // read the files again (new lines since it opened)
    void PressB(float now);
    void Tap(float x, float y, float now);      // (outside the page, on the left: B)
    void DragBegin();
    void DragTo(float rows);                    // the finger moved up by `rows` rows of 60 px (Settings::ListDragTo)
    void Update(float now, float dt);
    void Draw(Ui &ui, float now, float alpha, float slide, bool xmb);   // alpha: the settings page's, slide: its slide in
    void Hints(const char *&aL, const char *&bL, const char **chip, const char **lbl, int &nr);
    inline void (*s_flush)() = nullptr;         // Entry: writes Brume's log lines still in memory (so the page sees them)

} // namespace brume::journal
