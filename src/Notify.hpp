// Brume - v77: the notifications and the task centre (Yass, 2026-11-17: « Il nous faut un endroit sur l'écran qui indique si
// une tâche est en cours discrètement. Cliquer dessus révèle la tâche en cours. On ne laisse jamais l'utilisateur sans savoir si
// sa tâche est vraiment bloquée ou juste lente. Travaille bien les animations de notifs pour que le mouvement soit le même que
// celles sur ps4 »).
// - notify: messages that slide in from the top-left edge (a quick ease-out, like the PS4's), stay, then slide back out
//   (ease-in); several stack and push each other softly (the menu's spring). A « sticky » one is updated in place (a download:
//   « Jaquettes : 3 / 7 ») and leaves with its last word. The PS2 look: a soft dark veil, a small light, no glass card.
// - tasks: what runs in the background (covers, Nextendo...): a small mark of orbiting lights in the top bar while something
//   runs (nothing when idle); touching it (or ZR) opens a small panel: each task's name, progress, current item, a clear state
//   (en cours / en attente du réseau / bloqué : pourquoi / terminé) and the time since its last answer - slow, or stuck.
// Main thread only (the workers hand their numbers over with atomics; their owner calls these).
#pragma once
#include "Ui.hpp"

namespace brume {

    namespace notify {
        enum Icon { kInfo = 0, kDownload = 1, kDone = 2, kError = 3 };
        void Post(float now, const char *text, int icon = kInfo, bool chime = true);   // a one-shot message (~4 s); v87: chime false = its owner plays its own sound
        int  Open(float now, const char *text, int icon = kDownload); // stays until Close (its id; -1: none free)
        void Set(int id, float now, const char *text, float progress = -1);   // updated in place (progress 0..1, -1: none)
        void Close(int id, float now, const char *text, int icon = kDone);    // its last word, then it leaves (~3 s)
        void Draw(Ui &ui, float now, float alpha);
        void Avoid(float x);                                         // v78: the stack glides to the right of x (0: back to its place)
        // v87 - the lock screen: while it is there the messages wait (their time does not run, nothing is drawn); it shows how
        // many wait, and at the unlock they slide in one after the other
        void Wait(bool on);
        int  Waiting();
        inline bool s_sound = true;                                  // v85: Réglages > Son > Son des notifications (their chime; the HD tick stays)
    }

    namespace tasks {
        enum State { kRunning = 0, kWaitNet = 1, kBlocked = 2, kDone = 3 };
        int  Begin(float now, const char *name);                     // a task starts (the same name: the same slot)
        void Progress(int id, float now, int done, int total, const char *item);
        void Alive(int id, float now);                               // an answer came (bytes): « dernière réponse il y a… »
        void SetState(int id, float now, State s, const char *why = nullptr);
        void End(int id, float now, bool ok, const char *why = nullptr);   // done (kept in the panel a few seconds) / blocked
        bool Any();                                                  // something is running (or just ended: still shown)
        void Update(float now);
        // the top bar's mark (nothing when idle) and the panel
        void DrawMark(Ui &ui, float now, float alpha, float x);     // x: just left of the date and time (they are right-aligned)
        bool MarkHit(float x, float y);
        void Toggle(float now);
        bool PanelOpen();
        void Close(float now);
        bool PanelTap(float x, float y, float now);                  // true: the touch was the panel's (or closed it)
        void DrawPanel(Ui &ui, float now);
    }

} // namespace brume
