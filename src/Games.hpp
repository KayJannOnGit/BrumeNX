// Brume - the games of a console, MINIMAL BUT REALLY LAUNCHING (journal 2026-11-15, « prochaine étape »):
//   A on a console  -> its games (Switch: the installed titles from the loader; others: sdmc:/roms/<console>/)
//   A on a game     -> it starts: Switch natively, the others in their emulator (validated in
//                      docs/VERIFICATION-EMULATEURS.md) through a « donor » game, like sLaunch does
//   B               -> back to the wheel; when a game exits, the donor is cleaned up (onOpen)
// The staging (console sliding left, games coming out like dominoes, disc into the console) comes on top
// of this later, following Yass's vision in CLAUDE.md.
#pragma once
#include "Ui.hpp"
#include "Notify.hpp"
#include "Shelf.hpp"
#include "Assets.hpp"
#include "Ease.hpp"
#include <cmath>
#include <cstdio>

namespace brume {

    class Wheel;

    class Games {
    public:
        static constexpr int kMaxGames = 256;

        void Open(int console, float now);   // console = wheel index (mockup CONSOLES order)
        void Close(float now);
        bool Active() const { return m_open; }
        bool Visible(float now) const { return m_open || now - m_closeAt < 0.5f || m_diveAt > 0 || m_launchT0 > 0 || m_returnT0 > 0; }
        // v31 - the games as real 3D boxes beside the console (a leaning « library »), with the official covers
        bool InitShelf(Gpu &gpu, uint32_t firstDescriptor) { return m_shelf.Init(gpu, firstDescriptor); }
        int  ShelfSlots() const { return m_shelf.Slots(); }
        void Stream(Gpu &gpu);                       // right after Gpu::Begin: loads one missing cover
        void DrawBoxes(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &wheel);   // box_fsh bound
        int  BoxAt(float x, float y) const;          // the box under the finger (frontmost), -1 none
        void Tap(int i, float now) { if (i == m_sel) PressA(now); else Move(i - m_sel, now); }
        void Move(int step, float now);
        void PressA(float now);
        // v67 - « Gérer » (− on a game, or Réglages > Données > Gérer les logiciels), for EVERY console: the same Cover Flow
        // sorted by size, each game's real size, the space left; A archive (Switch), X delete (any game), Y sort, B back
        void Manage(bool on, float now);
        bool Managing() const { return m_manage; }
        void PressX(float now);
        void PressY(float now);
        void PressMinus(float now) { Manage(!m_manage, now); }
        bool PressB(float now);                      // true = handled here (manage: leaves it / cancels)
        void Update(float now, float dt);
        void Draw(Ui &ui, float now);
        void Reset() { ScanWait(); m_open = false; m_closeAt = -100; m_launching = false; m_pending = false; m_diveAt = -100; m_launchT0 = m_returnT0 = -100;
                       m_unfocusAt = -1; m_returnPending = false; m_settle.on = false; if (s_diveEnd) s_diveEnd(); if (s_focus) s_focus(false);
                       m_diveOutAt = -100;   // after sleep (v86 revue: the way back's « black held » (now + 1000) must not survive a sleep: black screen ~17 min)
                       m_mode = kLaunch; m_fromResume = false; m_ejectT0 = m_quietAt = -100; m_closeStep = 0; m_launchAfter = false; m_heldIdx = -1; }
                       // (v89: a closing not started yet never happens; the game stays suspended, its console keeps its disc)
        // back from a game: v32 - the disc / cartridge comes out of the console and goes back into its box; v89 - unless the game
        // is still running (HOME during it): then the disc stays in the console and the box goes back to its place, open
        void Done();
        bool Diving() const { return m_pending; }
        // v89 - the console holding a game that still runs (suspended with HOME), -1: none (the wheel shows it softly)
        static int HeldConsole();
        // v30: the finger - a drag moves along the cards (one card = 205 px), a tap on the chosen one launches it
        // v36: the finger holds the Cover Flow (to the millimetre), and when it lets go with speed, it is thrown: it
        // settles on the next box (a few, thrown hard)
        void DragBegin();
        void DragTo(float cards, float now);
        void DragEnd(float now, float v);
        // v30: the wheel's part of the staging (Entry links them to Wheel::SetFocus / StartDive / EndDive)
        static void (*s_focus)(bool on);
        static void (*s_dive)(float now, bool portable);
        static void (*s_diveEnd)();
        static void (*s_nudge)(float now);           // v33: Wheel::Nudge

        // Back in the menu (a game exited, or boot): remove the donor's redirect.
        static void CleanUp();
        // v35: an .nro started in application mode through the donor (emulators, the Apps page); false = `status` says why
        static bool LaunchNro(const char *nro, const char *arg, char *status, int cap, const char *fullArgv = nullptr);   // v93: fullArgv = the whole argv (a Tico template)
        // v51 - the real forwarders made by sphaira (installed titles 05xxxxxxxxxxx000, the homebrew's own name and icon,
        // all the memory, no borrowed game): the one of this homebrew (by its name), 0 = none; and starting it
        static u64  ForwarderFor(const char *name);
        static bool LaunchTitle(u64 tid, char *status, int cap);
        static bool LaunchVia(u64 tid, const char *nro, const char *argv, char *status, int cap);   // v58: a forwarder told what to start
        static bool IsForwarder(u64 tid) { return (tid >> 56) == 0x05; }
        static void RefreshCounts(float delay = 0);   // v66: counts every console's games again (boot, back from a game): Switch now, the others in the background
                                                      // (v80: `delay` seconds later - back from a game: after the way back, never during it)
        static void CountsTick(float now);   // v66: once per frame (joins the background count when it is done); v90: then the emulators found (Emus.hpp)
        static void Shutdown();        // v68: the background threads end before the menu is unloaded
        static void (*s_log)(const char *);

    private:
        struct Game {
            char title[96];
            char path[300];   // emulated: sdmc:/roms/... ; Switch: empty
            u64  tid;         // Switch title id (0 for roms)
            bool archived;    // Switch: listed but its data is gone (dimmed, cannot start)
            int  idx;         // Switch: the loader's title index (its icon); -1 for roms
            bool noCover;     // v31: asked once, nothing found (no .btex / no icon)
            bool noData;      // v42: a native port whose game files are not on the SD yet (it says where they go)
            char sys[24];     // v61: a port's original system (« Xbox », « PC »...), shown under its box (Reddit request)
            u64  size, part[3];   // v67: bytes (Switch: game, update, add-on content; others: part[0] = all of it)
            uint8_t where;        // v67: 1 console, 2 SD card, 3 both
            volatile bool sized;  // v67: measured (by the background thread)
            float shift;          // v77: where it was, in places (it glides back to 0: a sort, a deletion)
        };
        void Scan();                                  // (v99: into m_g[0..m_scanN), on the list's thread; Publish hands it over)
        void Publish();
        void ScanWait();                              // v99: a read still running ends (its list is dropped)
        int   m_scanN = 0;
        bool  m_scanning = false;
    public:
        void ScanNow() { Scan(); }                   // (the list's thread)
    private:
        void Launch(const Game &g, float now);
        // v67 - « Gérer »
        bool  m_manage = false, m_resort = false;
        unsigned m_gen = 0;                                 // v68: which list (a deletion that ends after a newer list leaves it alone)
        int   m_sortBy = 0, m_askKind = 0, m_askIdx = -1, m_busy = 0, m_goneIdx = -1, m_flashIdx = -1;
        float m_goneAt = -100, m_flashAt = -100;            // (v78: the question is a popup now - m_askKind: while it is open)
        double m_gUsed[2] = {}, m_gTot[2] = {}, m_gShown[2] = {};    // console, SD card: bytes used / total; the bars glide
        static void MgWork(void *self);
        bool  StartJob(int kind, int idx);
        void  StopJob();
        void  JobDone(float now);
        void  Sort(float now);
        void  Ask(int kind, float now);
        // v78: the popup's answers (Popup.hpp): yes = the deletion / archiving starts; no = nothing happens
        static void AskYes(void *self, float now);
        static void AskNo(void *self, float now);
        // v78: a game is running (HOME pressed during it) and another one is chosen: « close it? » first, like the Switch
        static void CloseYes(void *self, float now);
        bool  AskProfile();               // v89: the Switch's user picker when asked for (false: cancelled)
        void  StartStaging(float now, bool asked = false);   // the box opens, the media goes into the console, the dive (was PressA's end)
        // v89 (Yass: « quand tu sors d'un jeu en cours d'exécution, le cd doit rester dans la console et la boîte reste ouverte. Si
        // après tu choisis un autre jeu et le lances, il y a la popup pour fermer et le cd sort pour se ranger, et puis l'autre jeu
        // se lance normalement avec l'animation de sa boîte »)
        enum { kLaunch = 0, kKeep = 1, kResume = 2, kEject = 3 };   // what the staging plays
        int   m_mode = kLaunch;
        bool  m_fromResume = false;       // this game came back by a resume: its box never left its place
        float m_ejectT0 = -100;           // the disc comes out of the console back into its box (closed here, or ended by itself)
        float m_quietAt = -100;           // the game of ANOTHER console closed: its disc's way home, heard (that console is not on screen)
        int   m_closeStep = 0;            // after « Oui »: 1 the profile (if asked), 2 the game closes (the next frame)
        bool  m_closeAfter = false, m_launchAfter = false;   // then the chosen game starts (B on the way home: it does not)
        int   m_heldIdx = -1;             // the held game's box in this list (-1: not here)
        void  AskClose(float now, bool launchAfter);
        void  Resume(float now);          // the same game again: no box, the dive, then it comes back where it was
        void  StartEject(float now);
        void  Hold(const Game &g);        // a launch went well: this game is in its console now
        int   FindHeld() const;
        float HeldLid(float kk) const;    // how open the held box stands at its place (kk: its place from the chosen one)
        void  HomeSounds(float r);        // the media's way home (the return's sounds, bits 16-22)
        static bool HeldAlive();
        void  LaunchFailed(float now, unsigned rc);   // v78: a popup says why (the error code like the Switch's)
        void  Gauges();
        bool  Busy() const { return m_busy || m_goneAt > 0; }   // v67.1: a job, or a game leaving the list
        void  Say(float now, const char *msg) { notify::Post(now, msg); }   // v77: a notification (Notify.hpp)

        bool  m_open = false;
        int   m_console = 0, m_n = 0, m_sel = 0;
        float m_openAt = -100, m_closeAt = -100, m_scroll = 0, m_moveAt = -100;
        char  m_status[160] = {};
        float m_statusAt = -100;
        bool  m_launching = false, m_pending = false;      // v30: the dive plays, then the game starts
        float m_diveAt = -100, m_pendingAt = 0, m_diveOutAt = -100;
        int   m_domino = 0;                                 // cards that have played their « clack »
        int   m_dragFrom = 0;
        bool  m_drag = false;
        float m_dragScroll = 0;
        Shelf m_shelf;                                       // v31: the boxes' model and covers
        float m_selSx = 560, m_selSy = 470;                  // where the chosen box stands on screen (its title goes under)
        struct Hit { int i; float x0, y0, x1, y1; };
        Hit   m_hit[16];
        int   m_nhit = 0;
        // v32 - the staging: A = the box comes to the front and opens, the disc / cartridge rises, flies into the console's
        // slot (then the dive and the game); back from the game = the same, backwards
        float m_launchT0 = -100, m_returnT0 = -100, m_unfocusAt = -1;
        int   m_stage = 0;                                  // the staged game
        unsigned m_evt = 0;                                 // the staging's sounds already played (bits)
        bool  m_returnPending = false;
        Settle m_settle;                                    // v80: back from the game, black until the frames are steady
        void  StartReturn(float now);
        float LaunchIn() const;                             // when the media is in the console (s from launch)
        void  Stage(float now, float &front, float &lid, float &media, float &spin) const;
        float m_scrollV = 0, m_scrollFrom = 0, m_scrollT0 = -100;   // v34: the Cover Flow glides (ease out expo)
    public:
        Shelf &Covers() { return m_shelf; }                 // v34: the album's thumbnails use the same places
    private:
        Game  m_g[kMaxGames];
    };

} // namespace brume
