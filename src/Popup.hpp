// Brume - v78: the popups (Yass, 2026-11-17: « mettre en place des pop-ups là où c'est pertinent. Par exemple la double
// confirmation de suppression, double confirmée avec popup et bouton oui qui attend avant de s'allumer en rouge et être
// cliquable… crée des sons spécifiques pour ces popups, et anime absolument tout »). Where and why: docs/POPUPS.md.
// One at a time, modal: while it is open it takes every button and every touch (Entry). The scene melts into the blurred fog
// behind it, its plate (Ui::Surface: night mist, never glass) grows to its size with the menu's spring, the words appear in
// turn, the light of the focus glides from one answer to the other, a press sinks the word in and lights it up, then the
// popup sinks back into the mist and only then its answer happens (so an answer may open another popup).
//   kInfo    one or two answers, the action chosen first (nothing is lost)
//   kAsk     two answers, the safe one chosen first (« Plus tard », « Annuler »): a restart, closing a game, archiving
//   kDanger  a deletion: « Annuler » chosen first; the action waits ~1.5 s (a line of light fills under it from the centre),
//            then turns red and only then can be chosen; its own sounds (it arms; the deletion: a soft low thud)
//   kError   one answer (OK), a pink light, the error code written like the Switch's (2XXX-YYYY)
// Every sound is played on a note of the chord sounding now (« tout est dans l'accord »).
#pragma once
#include "Ui.hpp"

namespace brume::popup {

    enum Kind { kInfo = 0, kAsk = 1, kDanger = 2, kError = 3 };
    using Fn = void (*)(void *ctx, float now);
    struct Spec {
        int kind = kInfo;
        const char *title = "";          // French (translated when drawn), yellow
        const char *subject = nullptr;   // a name, near-white, under the title (a game's) - drawn as it is
        const char *text = "";           // French, or already built in the right language (lang::T before snprintf); 3 lines max
        const char *note = nullptr;      // a small faint line under it (an error code)
        const char *yes = "OK";          // the action, on the right (alone: in the middle)
        const char *no = nullptr;        // the other answer, on the left; nullptr = a single answer
        Fn onYes = nullptr, onNo = nullptr;   // called once the popup has gone (B = onNo; a single answer: B = onYes)
        void *ctx = nullptr;
    };

    void Show(float now, const Spec &s);
    void Error(float now, const char *title, const char *text, unsigned rc);   // kError, with the code when rc != 0
    bool Open();                         // it takes the input (from Show until its answer)
    float Shown();                       // 0..1 how much it is on screen (what is behind it can step back)
    void Move(int dir, float now);       // ◀ ▶ (and ▲ ▼): the other answer
    void PressA(float now);
    void PressB(float now);
    bool Tap(float x, float y, float now);   // true: the touch was the popup's (an answer, or outside it = B)
    void CloseNow();                     // silently, no answer (before sleep)
    void Draw(Ui &ui, float now);        // last of all (above the panels; the notifications stay above it, like the PS4)

} // namespace brume::popup
