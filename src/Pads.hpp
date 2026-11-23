// Brume - v39: the Manettes page (Yass: « onglet manettes d'abord, fais une pépite ; la console connaît la couleur des
// manettes »). Replaces the Switch's controllers page for everything but pairing.
//  - the chosen controller drawn big, in ITS REAL COLOURS (body and buttons, left and right apart for a pair of
//    Joy-Con): a pair, a single Joy-Con, a Pro Controller, the console in handheld mode, any other pad;
//  - it is ALIVE: its buttons light up and its sticks move with the real ones, as you press them;
//  - the room takes its colour: the fog and the lights glide towards it (a pair: the mix of both);
//  - its player lights, its battery (each Joy-Con), charging or not;
//  - A: it vibrates (a soft heartbeat) and glows; Y: pair a new one (the Switch's own screen); ◀ ▶: the next one;
//  - anywhere in the menu, a controller that connects SAYS HELLO: the room pulses in its colour, a soft chord, its name
//    under the bar, and it answers with a little vibration (IDEES #12).
#pragma once
#include "Ui.hpp"
#include "Gpu.hpp"
#include "Assets.hpp"
#include <switch.h>

namespace brume {

    class Wheel;

    class Pads {
    public:
        static constexpr int kPage = 3;                  // v92: 3 (the Musique page is gone)
        bool Visible(const Wheel &w) const;
        void Update(const Wheel &w, float now, float dt);   // watches the controllers (live), hello / goodbye, the room's colour
        // v92 (Yass: « les manettes détectées en temps réel, comme le menu de la Switch ») - the menu's buttons: EVERY controller
        // (players 1 to 8 and the console's rails), a Joy-Con alone on its rail included (libnx's padUpdate only read player 1
        // + the console, and drops a console npad not flagged « connected »): whoever holds a controller drives the menu, and
        // attaching / detaching the Joy-Con never takes the control away. Replaces padUpdate (the same PadState).
        static void Poll(PadState &pad);
        void Move(int step, float now);
        void PressA(float now);                              // the chosen one vibrates and glows
        void PressY(float now);                              // pair one: the Switch's controllers screen
        void Tap(float x, float y, float now);
        void DrawUi(Ui &ui, float now, const Wheel &w);
        // v40 (Yass: « un modèle 3D de la Switch avec les couleurs des manettes, qui se combinent à la console comme IRL »):
        // Joy-Con chosen -> the 3D Switch in their colours; detached Joy-Con slide up off their rails and float beside it,
        // attached ones slide back on (a click); its buttons light the Joy-Con that pressed them. v94: the menu's right stick
        // turns it exactly as it turns the chosen console on the wheel (Wheel::StickS: the same dead zone, the same glide);
        // with no controller at all it is still there, its Joy-Con asleep beside it
        bool ModelVisible(const Wheel &w) const;
        void DrawModel(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w);
        static void (*s_pair)(float now);                    // (System: the controllers applet)
        // v94 (Yass: « faut une animation à gauche et à droite de l'écran quand le joycon est connecté… ultra stylé, aux couleurs
        // programmées sur le joycon ») - anywhere in the menu: a Joy-Con that slides onto its rail lights its screen edge (a
        // light runs up the edge in its body colour, a second one in its buttons' colour, a soft bloom, a few motes drifting
        // in), the left one on the left, the right one on the right; one that connects wirelessly does it too, softer. ~1.3 s,
        // nothing drawn when idle (Entry: over everything but the notifications)
        void DrawRail(Ui &ui, float now);
        // v84 (Réglages > Manettes > « Tester les manettes ») - Brume's own test screen, in the PS2 look, over the settings
        // (they shrink away into the fog): both sticks in their circles of light (the live position, a fading trail, the
        // menu's dead zone, their rest measured during a short « ne touche plus » countdown: ok / léger écart / dérive
        // possible), every button lighting up with ITS note of the chord and a tap of the motor on its side, L / R / ZL / ZR
        // as gauges (a GameCube controller's analog triggers fill), the gyroscope as a plate of light points that tilts with
        // the controller. The controller tested = the last one that pressed a button. Hold B to leave (a press only lights B:
        // it is being tested), hold X to measure again; a touch on those hints does it at once. Nothing runs once it is
        // closed (its sensor and the motors stopped); nothing is ever written (no calibration).
        bool  TestOpen() const { return m_t.open; }
        void  TestBegin(float now);
        void  TestEnd(float now);                            // (B held, a touch on its hint, HOME)
        void  TestTap(float x, float y, float now);
        void  TestStop();                                    // before sleep: closed at once, motors and sensor off
        static void Shutdown();                              // v99: the files' thread (manettes.log / .ini) ends
        void  DrawTest(Ui &ui, float now);
        static inline u64 s_held = 0;                        // the menu's buttons this frame (Entry): B held, and the PC preview's script
        static inline bool s_none = false;                   // v94: no controller at all (the page hides « A Vibrer »)

    private:
        enum Kind { kPair, kLeft, kRight, kPro, kHandheld, kGc, kOther };
        struct Pad { HidNpadIdType id; u32 style; Kind kind; HidNpadControllerColor l, r; HidPowerInfo pl, pr; bool hasL, hasR; };
        void  Scan(float now, bool hello);
        static bool Read(HidNpadIdType id, u32 style, HidNpadCommonState &st);
        // v92 - live detection: a cheap fingerprint of the 9 npads every frame (shared memory: style, device, footer, sides),
        // the full look (colours, battery) only when it changes or twice a second; what changed is announced (notify): a
        // controller connected / disconnected, Joy-Con put on / taken off the rails - one message for one gesture (the pair
        // that leaves the rails is not « connected » too, the one that slides onto them is not « disconnected »)
        u32   Signature() const;
        void  Announce(const Pad *old, int oldN, float now);
        u32   m_sig = 0;
        float m_detachAt = -100, m_attachAt = -100;
        float m_offAt[9] = {};                               // a Joy-Con that left: « déconnecté » said a moment later (unless it slid onto a rail)
        char  m_offName[9][40] = {};
        bool  m_pageOn = false;                              // (the page shows the 3D Switch: it plays its own click / release)
        // the selector's cards: each npad's own spring (it appears, it fades away), its last look kept while it fades
        float m_show[9] = {}, m_showV[9] = {}, m_bornAt[9] = { -100, -100, -100, -100, -100, -100, -100, -100, -100 };
        float m_cardX[9] = {}, m_cardW[9] = {};
        Pad   m_ghost[9] = {};
        bool  m_attach[2] = {};                             // v41: each Joy-Con on the console's rail (the handheld npad's sides)
        float m_clickAt[2] = { -100, -100 }, m_nudgeAt = -100;
        void  Vibrate(const Pad &p, float now);
        void  Name(const Pad &p, char *out, int cap) const;

        // v94 (Yass: « les manettes restent de la bonne couleur qu'importe si elles sont pas détectées ») - the last real
        // colours seen of each Joy-Con (0 left, 1 right; body, buttons), kept in sdmc:/brume/manettes.ini (written 2 s after
        // they change, read once at start): the 3D Switch always wears them, even before a Joy-Con is seen
        u32   m_mem[2][2] = {};
        bool  m_memOk[2] = {}, m_memLoaded = false;
        float m_memSaveAt = -1;
        void  MemLoad();
        void  MemSave();
        void  MemNote(int s, u32 body, u32 btn, float now);
        void  MemApply();                                    // (Settings::s_joyL / R, linear, for the 3D Switch on every page)
        // v94 (Yass: « si après détachement elles sont pas détectées après un moment faut le montrer ; le physique doit devenir
        // virtuel ») - each Joy-Con on its rail or connected wirelessly; one that is neither for 3 s is ASLEEP on the page: it
        // drifts further, dims and breathes slowly, its words float under it; it wakes with a flash of its colour
        bool  m_here[2] = {}, m_hereInit = false;
        float m_goneAt[2] = { -100, -100 }, m_lost[2] = {}, m_lostV[2] = {}, m_wakeAt[2] = { -100, -100 };
        float m_joyScr[2][2] = {};                           // where each floating Joy-Con is on screen (DrawModel -> DrawUi)
        bool  m_joyScrOk[2] = {};
        // v94 - the rail's light (see DrawRail): when it started, how strong, and the colours it runs in
        float m_railAt[2] = { -100, -100 }, m_railK[2] = {};
        u32   m_railCol[2][2] = {};
        void  Rail(int s, float now, float k, bool voice);

        Pad   m_pad[9];
        int   m_n = 0, m_sel = 0;
        u32   m_mask = 0;
        bool  m_first = true;
        float m_pollAt = 0, m_moveAt = -100, m_helloAt = -100, m_vibAt = -100, m_now = 0;
        float m_helloCol[3] = { 0.3f, 0.5f, 1.0f };
        // the chosen one, live
        u64   m_btn = 0, m_prevBtn = 0;
        float m_stk[4] = {}, m_btnAt[32] = {};
        // its vibration (a soft heartbeat: two beats)
        HidVibrationDeviceHandle m_vh[2];
        int   m_nvh = 0;
        bool  m_vibOn = false;
        // the room's colour
        float m_tintW = 0, m_tintV = 0;
        float m_det[2] = {}, m_detV[2] = {}, m_detTo[2] = {};   // v40: each Joy-Con off the console (0 on .. 1 off)
        // v84 - the test screen
        static constexpr int kSlots = 20, kTrail = 18;
        struct Test {
            bool  open = false;
            float openAt = -100, closeAt = -100;
            int   cur = -1;                                 // the controller tested: its index in the 9 npads (-1: none)
            Pad   pad{};
            u64   btn = 0, prev = 0;
            float at[kSlots] = {}, sink[kSlots] = {}, sinkV[kSlots] = {};   // each button: pressed at, how far in (a spring)
            float stk[4] = {}, raw[4] = {};                 // the sticks (drawn: a little smoothed; raw: as read)
            float trail[2][kTrail][2] = {}; int head[2] = {}; float trailAt = 0;
            float trig[2] = {};                             // a GameCube controller's analog L / R (0..1), else -1
            // the rest: the countdown (3 s; the last 2 measured), then a result per npad and stick (-1: not measured)
            float measAt = -100, doneAt = -100, againAt = -100;
            double sum[2][2] = {}; float lo[2][2] = {}, hi[2][2] = {}; int cnt = 0;
            float rest[9][2] = {};
            int   tick = -1;
            // the gyroscope: its handle, a reference that slowly follows (the plate settles flat again), the relative turn
            HidSixAxisSensorHandle six{}; bool sixOn = false, sixMine = false, sixSeen = false;
            float q0[4] = { 0, 0, 0, 1 }, qr[4] = { 0, 0, 0, 1 }; bool qInit = false;
            // its motors: one short pulse per side (0 left, 1 right)
            HidVibrationDeviceHandle vh[2]; int nvh = 0; bool vibOn = false, vibZero = true;
            float pulseAt[2] = { -100, -100 }, pulseDur[2] = {}, pulseLo[2] = {}, pulseHi[2] = {};
            // the holds (B leaves, X measures again) and the hint that says so
            float holdB = 0, holdX = 0, bTapAt = -100, switchAt = -100;
            float hint[2][2] = {};                          // where the two hints are drawn (x from .. to): a touch there
            u64   seen[9] = {};                             // each npad's buttons last frame (a fresh press takes over the test)
        } m_t;
        void  TestQuiet();
        void  TestUpdate(float now, float dt);
        void  TestSelect(int k, float now);
        void  TestMeasure(float now);
        void  TestPress(int slot, float now);
        bool  TestRead(HidNpadCommonState &st, float now);
    };

} // namespace brume
