// Brume - the console wheel, ported value for value from docs/M1/maquette-brume.html (v22/v23):
// consoles arriving one by one (FALL_*), the camera glide (camCurve), the carousel (ease-out 0.6 s),
// the wind it blows on what floats, the lights tinted by the console, the sounds, and the interface
// (top bar, name banner with logo, button hints) with the mockup's CSS transitions.
#pragma once
#include "Gpu.hpp"
#include "Ui.hpp"
#include "Assets.hpp"
#include "Mesh3d.hpp"
#include "Consoles.hpp"

namespace brume {

    class Wheel {
    public:
        void Unlock(float now, bool fromFinale);   // the lock screen opened: the consoles arrive
        void Relock(float now);                    // B: back to the lock screen
        void Rotate(float now, int step);
        void PressA(float now);
        bool Active() const { return m_on; }
        void Update(float now, float dt, float hour);
        void DrawMeshes(Gpu &gpu, const Assets &a, const Target &haze, float now);
        void DrawUi(Ui &ui, const Assets &a, float now, const char *timeText);
        void DrawLabels(Ui &ui, float now);         // v93: the names of the blocks of glass (the consoles without a model yet)
        int   Sel() const;
        // The top bar's tabs (v92: Jeux, Apps, Album, Manettes, Réglages, Alimentation - the Musique page is gone; v95: Amis
        // before Réglages; -1 = the profile). Not focused: the page we are on is on.
        static constexpr int kTabs = 7;
        void  SetBar(bool focused, int tab) { m_barFocus = focused; m_tabOn = tab; }
        bool  BarFocused() const { return m_barFocus; }
        int   TabOn() const { return m_tabOn; }
        static inline int s_count[consoles::kMax] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // v66: the real number of games of each console (-1 = not known yet)
        static inline int s_bench = 0;   // test bench: 0 normal, 1 no console drawn, 2 one triangle per part
        static inline float s_cover = 0; // v96: the profile page over the wheel (0..1, Entry)
        static inline int s_debug = 0;   // test views (ZR): 0 normal, 1 texture, 2 room light, 3 lamps, 4/5 back faces culled (CCW / CW), 6-9 see mesh_fsh
        float WindDrift() const { return m_windDrift; }
        float WindLift() const { return m_windLift; }
        float WindVel() const { return m_windVel; }
        float Spin() const { return m_spin; }            // the carousel's speed (consoles per second, + = A turning right)
        // v28 - the pages (L / R, mockup v24): 0 Jeux (the wheel), 1 Apps, 2 Album, 3 Manettes, 4 Amis (v95), 5 Réglages.
        // The new page slides in from the side we go to (ease-out .7 s), the consoles leave to the left, the wind carries
        // what floats, the top bar's pill follows. Each page's number: Apps::kPage, Album::kPage, Pads::kPage, social::kPage,
        // Settings::kPage.
        static constexpr int kPages = 6;
        static constexpr int kPageApps = 1, kPageAlbum = 2, kPagePads = 3, kPageFriends = 4, kPageSettings = 5;   // (Entry checks they match)
        void  SetPage(float now, int p);
        // v79 (Yass: « Gérer les logiciels : marche mais fastidieux ») - a far page in ONE move, without crossing the pages in
        // between: what is on screen shrinks / fades away (WarpK 1 -> 0, `out` seconds), the page changes unseen, one short step
        // from its place, and glides in the last bit while everything comes back (WarpK 0 -> 1). The consoles follow WarpK;
        // the other pages read it (Settings). Any other page change cancels it.
        float Warp(float now, int p, float out);         // (out: before Pace(); returns when it lands)
        float WarpK() const { return m_warpK; }
        // v79: straight to console i, no turn, no jingle (used while the wheel is off screen)
        void  Snap(int i);
        // v90 (Yass: « les consoles du carrousel s'affichent uniquement si l'émulateur associé est installé ») - which consoles
        // stand on the wheel (Emus.cpp: bit i = console i; the Switch always). The ring closes up around them: each console
        // glides to its new place, the spacing too. fresh = the new ones: they wait until the wheel is calm and on screen,
        // then arrive one by one (from above, a crystal note in the chord, then their notification: s_arrived). A removed
        // one fades away where it stands. instant = at once, no staging (boot, the wheel not on screen).
        // The ring keeps its 8 places: the consoles stand side by side on the first ones; 6 or 7 wrap over the empty places,
        // fewer have two ends (a soft knock there). v93: more than 8 (up to 15, Consoles.hpp): the ring has as many places, the
        // spacing stays the validated one (45 degrees) and the ones at the back fade into the fog (never two on one place).
        void  SetPresent(uint16_t mask, uint16_t fresh, float now, bool instant);
        static inline void (*s_arrived)(int console, float now) = nullptr;
        int   Page() const { return m_page; }
        float PageAway() const { return m_pg < 1.0f ? m_pg : 1.0f; }   // 0 = on the wheel, 1 = elsewhere
        float PageAnim() const { return m_pg; }                         // the animated page (0 .. kPages - 1)
        void  CameraAt(float now, float *eye, float *at) const { Camera(now, eye, at); }
        void  SetStick(float x, float y) { m_stk[0] = x; m_stk[1] = y; }   // right stick: tilt the chosen console (-1..1)
        const float *StickS() const { return m_stkS; }                    // v94: the same, glided (the Manettes page's Switch turns with it)
        void  SetTouchTilt(float x, float y) { m_touchTilt[0] = x; m_touchTilt[1] = y; }   // v36: the finger turns it (games screen)
        void  SetPtr(float x, float y) { m_ptr[0] = x; m_ptr[1] = y; }     // v29: gyroscope parallax (mockup ptr.sx / sy)
        // v29 - the top bar under the finger: 0..5 = a tab, -1 = the profile, -2 = nothing there
        int   TabAt(float x, float y) const;
        // v29.1 - the finger moves the carousel / the pages ITSELF (to the millimetre), then it settles on the nearest
        // console / page like a grid (never stopped in the void, no momentum that sends it far)
        void  DragBegin(float now);
        void  DragTo(float now, float steps);           // steps: consoles since the finger went down (+ = to the next)
        void  DragEnd(float now, float v = 0);         // v36: v = the finger's speed (consoles per second): it is thrown
        void  PageDragBegin();
        void  PageDragTo(float pages);                  // pages since the finger went down (+ = to the right)
        void  PageDragEnd(float now);
        // v30 - the games (Yass's vision): the chosen console glides to the LEFT of the screen (the others fade away)
        // while its games come out on its right; launching: a portable dives into its screen (the camera goes in),
        // a home console spins - and the screen goes black (Games draws the black).
        void  SetFocus(bool on) { m_focusOn = on; }
        void  SnapFocus() { m_focus = m_focusOn ? 1.0f : 0.0f; m_focusV = 0; }   // v79: already there (a Warp: no glide seen from the wheel)
        void  StartDive(float now, bool portable) { m_diveT0 = now; m_divePortable = portable; }
        void  EndDive() { m_diveT0 = -100; }
        void  Nudge(float now) { m_nudgeT0 = now; }      // v33: the disc / cartridge seats: the console gives a little
        // v31: the lights of this frame's consoles (the game boxes are lit the same way, Games::DrawBoxes)
        static inline m3::MeshUbo s_lights{};
        static inline m3::M4 s_selModel{};             // v32: the chosen console's model matrix this frame (its slot)
        // v89 - the console holding a game that still runs (Games, each frame; -1: none) and its slot (model space, kSlot):
        // a soft light breathes there on the wheel, and « EN COURS » under its name. Where it was seen (the last frame drawn)
        static inline int   s_held = -1;
        static inline float s_heldSlot[3] = {};
        static inline float s_heldSx = 0, s_heldSy = 0, s_heldR = 0, s_heldK = 0;
        // v31: where the chosen console glides when its games are open (Games aims the launched box at it)
        static constexpr float kFocusX = -2.45f, kFocusY = 0.05f, kFocusZ = 0.25f, kFocusYaw = 0.55f;
        // The consoles as spheres, in the water cubes' camera space (Entry.cpp: x right, y up, z forward, focal 900 px):
        // centre xyz + radius. For the cubes to bump into them instead of passing through.
        int   Spheres(float (*out)[4], int max) const;
        // v28 - the stars (the mockup's dust): points orbiting the carousel, carried by its wind; some pass right
        // in front of the camera (big soft dots). front = only those nearer than the centre console (drawn over the
        // consoles), else the others (drawn before them). light = the lock screen's light (1 on the wheel).
        int   Stars(float now, float light, UiVtx *out, int maxVtx, bool front) const;
        static void (*s_probeLog)(const char *);
        static bool s_probeAll, s_probeQuiet;   // diagnosis: cut and check after every 3D draw (normal path)

        struct Trans {                              // a CSS transition between 0 (class « out-* ») and 1
            float from = 0, to = 0, t0 = -100;
            void Set(float now, float target, float cur) { from = cur; to = target; t0 = now; }
        };
    private:

        void  Camera(float now, float *eye, float *at) const;   // the mockup's camera (wide view .. carousel view)
        float GroupT(const Trans &t, float now) const;   // transform progress (may overshoot)
        float GroupO(const Trans &t, float now) const;   // opacity
        void  ShowUi(float now);

        bool  m_on = false, m_uiShown = true, m_moving = false;
        float m_cur = 0, m_target = 0, m_tweenFrom = 0, m_tweenTo = 0, m_tweenT0 = -1;
        float m_camT = 0, m_camMoveT0 = -1, m_settleAt = 1e9, m_showAt = -1, m_lastLand = 0;
        int   m_lastJingle = -1, m_chord = 0;
        float m_windDrift = 0, m_windVel = 0, m_windLift = 0, m_spin = 0;
        float m_stk[2] = { 0, 0 }, m_stkS[2] = { 0, 0 };   // right stick, raw and smoothed
        int   m_page = 0;
        float m_ptr[2] = { 0, 0 };                         // v29: gyroscope parallax
        bool  m_drag = false, m_pageDrag = false;          // v29.1: the finger holds the carousel / the pages
        bool  m_focusOn = false, m_divePortable = true;    // v30: the games
        float m_focus = 0, m_diveT0 = -100;
        float m_focusV = 0;                                // the glide's speed (the lean)
        float m_focusFrom = 0, m_focusTgt = 0, m_focusT0 = -100;
        float m_curV = 0, m_pgV = 0;                       // v35: the carousel's and the pages' glides (their speeds)
        float m_touchTilt[2] = {}, m_stkV[2] = {}, m_pillVX = 0, m_pillVW = 0;   // v36
        float m_nudgeT0 = -100;                            // v33
        float m_dragFrom = 0, m_pageDragFrom = 0;
        float m_pillX = -1, m_pillW = 0, m_tabX[kTabs] = {}, m_tabW[kTabs] = {}, m_tabCy = 52, m_profX0 = 0, m_profX1 = 0;
        float m_pageFrom = 0, m_pageT0 = -100, m_pg = 0, m_windKick = 0;   // m_pg: the animated page; m_windKick: a page change's gust
        int   m_warpTo = -1;                               // v79: the page a Warp goes to (-1: none on its way out)
        float m_warpT0 = -100, m_warpOut = 0.3f, m_warpInT0 = -100, m_warpFrom = 1, m_warpK = 1;
        float m_tint[3] = { 0.0116f, 0.0284f, 0.3325f };   // tintNow (linear), starts at BLUE
        float m_wake = 0, m_night = 0, m_t = 0;
        struct Slot { float appear = 0, fallStart = -1, landedAt = 0, jump = 0; int fallIdx = 0; bool hero = false, popped = false; } m_slot[consoles::kMax];
        Trans m_group[4];                          // topBar, namePlate, bottom, bottomR (INTRO order)
        float m_groupShowAt[4] = { -1, -1, -1, -1 };
        int   m_shownName = 0;                     // the name banner shows this console
        float m_nameFadeFrom = 1, m_nameFadeTo = 1, m_nameFadeT0 = -100, m_nameSwapAt = -1;
        int   m_cntConsole = -1, m_cntTarget = -1;    // v66: the count under the name rolls up when it changes
        float m_cntT0 = -100, m_cntFrom = 0;
        int   m_nameSwapTo = 0;
        float m_duckBackAt = -1;
        bool  m_barFocus = false;
        int   m_tabOn = 0;
        float m_tabAmt[kTabs + 1] = { 0, 1, 0, 0, 0, 0, 0, 0 };   // [0] = profile, [1 + i] = tab i (transition .5s)
        float m_lastUi = -1, m_uiBackAt = -100;          // (v79: when the interface came back on screen)
        // v90 - the consoles on the ring (SetPresent): m_ring = the ones standing, in order (m_target counts in its places);
        // each console's place on the ring (m_rp, in places, glides to m_rpT), its presence (m_pres, 0..1)
        // (v93: 16 bits, Consoles.hpp; the first 8 stand as before until the first scan says otherwise)
        uint16_t m_mask = 0xFF, m_want = 0xFF, m_fresh = 0;
        int   m_ring[consoles::kMax] = { 0, 1, 2, 3, 4, 5, 6, 7 }, m_M = 8, m_S = 8;   // m_S: the ring's places (8, or more consoles)
        float m_rp[consoles::kMax] = { 0, 1, 2, 3, 4, 5, 6, 7 }, m_rpT[consoles::kMax] = { 0, 1, 2, 3, 4, 5, 6, 7 }, m_rpV[consoles::kMax] = {};
        float m_pres[consoles::kMax] = { 1, 1, 1, 1, 1, 1, 1, 1 }, m_presT[consoles::kMax] = { 1, 1, 1, 1, 1, 1, 1, 1 }, m_presV[consoles::kMax] = {};
        float m_nextArrive = 0;
        float m_arriveT0[consoles::kMax] = { -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100, -100 };
        bool  m_arrLanded[consoles::kMax] = {};
        // v93: the blocks of glass' names (the consoles without a model), where they stand on screen this frame (DrawMeshes)
        float m_lbl[consoles::kMax][4] = {};             // x, y, size (px), alpha
        float Slots() const;                             // the ring's places (8: the spacing stays the validated one; v93: more consoles, more places)
        float Vis(float rel) const;                      // v93: 1, or fading at the back of a ring of more than 8
        float Rel(int i) const;                          // console i's place from the front one (places, folded on the ring)
        bool  Wraps() const { return m_M >= 6; }         // 6 or 7 consoles: it wraps over the empty places; fewer: two ends
        int   AtSlot(int slot) const;                    // the console standing on that place, -1 empty
        int   StepFrom(int t, int step) const;           // the next place with a console (t itself: an end)
        int   ValidNear(float x, int dir) const;         // the nearest place with a console
        void  Relayout(uint16_t mask, float now, bool instant);
        void  Turn(float now, int to, bool sounds);      // the carousel goes to that place (Rotate; v90: an arrival)
        void  PresenceTick(float now, float dt);
    };

} // namespace brume
