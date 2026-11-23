// Brume - the settings page (v28, ported from docs/M1/maquette-brume.html v26-v27, the « carrousel » display):
// the 10 categories as 3D glass pebbles (the cubes' water, shaders/icon_fsh.glsl) on a ring like the consoles;
// ◀ ▶ turns it (whoosh, wind), the front one sways and the others spin slowly, each at its own angle;
// A: the ring slides to the left, the others shrink away, the category's settings arrive on the right
// (neutral glass icons, the chosen one fixed under a frosted dark band, values on the right); folders open a new
// column; choices / switches open a list on the right, sliders adjust with ◀ ▶. B goes back.
// v43 (Yass: « faut faire marcher les paramètres ») - the rows are the REAL settings: read when a category opens, written
// when changed (system settings services, emuMMC only); what we don't do natively opens the Nintendo page.
#pragma once
#include "Gpu.hpp"
#include "Ui.hpp"
#include "Assets.hpp"
#include "Wheel.hpp"

namespace brume {

    class Settings {
    public:
        static constexpr int kPage = 5;                  // the wheel's pages (v95): 0 Jeux, 1 Apps, 2 Album, 3 Manettes, 4 Amis, 5 Réglages
        static constexpr int kCats = 10;
        bool Visible(const Wheel &w) const { return w.Active() && fabsf(w.PageAnim() - (float)kPage) < 0.98f; }
        bool Open() const { return m_open; }
        void Move(int dc, int dr, float now);
        void PressA(float now);
        bool PressB(float now);               // true = leave the page (back to the games)
        // v87 - choosing the lock code (PS4-like: the current one first, the new one, again to confirm): it takes the buttons
        bool CodeOpen() const { return m_rec.on; }
        void CodeInput(float now, u64 down);
        void DrawCode(Ui &ui, float now);     // over everything of the page (Entry, after DrawUi)
        // v29 - the finger: a tap (menu px) and a swipe (+1 = to the left: the next one). Returns false if B was
        // asked at the top (leave the page).
        bool Tap(float x, float y, float now);
        void Swipe(int steps, int rows, float now);
        // v29.1 - the finger holds the ring (closed) or scrolls the list (open), then it settles on the grid
        void RingDragBegin();
        void RingDragTo(float now, float steps);
        void RingDragEnd(float now, float v = 0);
        void ListDragBegin(float x = 0, float y = 0);   // (v83: where the finger went down - the date and time pages' wheels)
        void ListDragTo(float rows);
        void Update(float now, float dt);
        void DrawMeshes(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w);
        void DrawUi(Ui &ui, const Assets &a, float now, const Wheel &w);
        // the icons as spheres in the water cubes' camera space (like Wheel::Spheres), owner = icon index
        int  Spheres(float (*out)[4], int *owner, int max, float now, const Wheel &w) const;
        void Kick(int icon, float k) { if (icon >= 0 && icon < kCats) m_ic[icon].kick += k; }
        static void (*s_toast)(float now, const char *msg);
        static void (*s_open)(int tab, float now);
        static inline void (*s_profilesChanged)() = nullptr;
        static inline void (*s_restart)(float now) = nullptr;   // v78: « Redémarrer » in a popup (System::Restart: the fade, then the restart)
        static inline void (*s_leave)(float now) = nullptr;    // v85: « Revenir au menu d'origine » (Entry: the emergency exit's files, then the restart)
        void Done();                                           // v85: back in the menu (JKSV, launched from Données, exited)
        static inline void (*s_manage)(float now) = nullptr;   // v67: « Gérer les logiciels » (the games' Cover Flow, sorted by size)   // v57 (review): a nickname / icon / user changed from here -> the bar's pill reloads          // v43: System::Open (-1 profile, 3 controllers, 4 Nintendo settings)
        static inline bool s_lockOn = true;
        static inline bool s_goldCube = true;
        static inline bool s_askUser = false;
        static inline bool s_nextendo = true;
        static inline bool s_coversAuto = true;       // v75: the missing covers downloaded by the console (Réglages > Données)
        static inline bool s_ntpAuto = true;          // v70.8: the clock set by Internet at boot (Réglages > Système > Date et heure)
        static inline bool s_xmb = false;
        static inline bool s_wave = false;                   // v52: the XMB wave behind the fog
        static inline bool s_glassPs3 = true;                // v73: the icons' liquid glass of the PS3 (ui_fsh 29), else the v28 glass (22)
        static inline float s_glassVis = 0;                  // v73: how much the settings page is there (uGlass.w: the glass appears)
        static inline int  s_unlockStyle = 0;                // v59: 0 « Battement de cœur » (v38.2), 1 « Aurore »
        static inline bool s_sky = false, s_battery = false; // v53: the sky follows the hour (IDEES #13) / the battery is the fog (#14)
        static inline float s_hour = 12.0f;                  // v53: the local time (Entry, every frame)
        static inline float s_waveCur = 0;                    // v50: the XMB display (PS3) instead of the 3D carousel (Y)                // v49: Réglages > Réseau > joueurs en ligne (Nextendo)
        static inline float s_themeLight = 1.0f, s_themeLightCur = 1.0f;   // v47: the theme's light (1 Brume, .62 Nuit profonde, 1 Chrome)
        static void ApplyTheme(int v);                       // v81: Réglages > Brume > Thème (its colours, its light, its look: Theme.cpp)                // v46: Réglages > Comptes > choisir le profil à chaque jeu
        static inline int  s_clockFont = 0;                  // v44: the lock screen clock's font (0 Lexend Giga .. 6)
        static inline bool s_clockFontDirty = false;         // v45: to apply before the next frame (Entry)
        static void SetClockFont(int v);                     // v45: (Entry, when a font could not load: back to 0)
        // v44 - the accent colour (Réglages > Brume): the highlights of the interface (the bar's pill, the chosen option...)
        static inline float s_accent[3] = { 0.549f, 0.627f, 1.0f };
        static inline float s_accentTarget[3] = { 0.549f, 0.627f, 1.0f };   // v81: the accent glides to it (Theme::Frame)
        static Rgba Accent(float a, float white = 0.0f) {
            return Rgba{ s_accent[0] + (1 - s_accent[0]) * white, s_accent[1] + (1 - s_accent[1]) * white, s_accent[2] + (1 - s_accent[2]) * white, a };
        }                // v44: the golden cube may appear (Réglages > Brume)
        static void UnlockSecret(float now);                 // v44: the golden cube caught                  // v43: the console's lock screen setting (off: straight to the consoles)
        // v29 - the real settings (sdmc:/brume/reglages.ini): the ambient colour, the menu's vibrations
        static void Sleep();                                 // v86: (onPowerButton) a homebrew not started yet is forgotten
        static void Load();
        void VolWarn(float now);                             // v57: the headphones' volume warning (after a live slider write)
        static void Save();
        static void Ambient(float *rgba);     // the current ambient colour (it glides when changed)
        static float s_ambient[3], s_ambientTarget[3];
        static inline float s_powerDim = 0;          // v33: the power popup is open (0..1): the light goes down
        static inline float s_cover = 0;             // v84: the controllers' test covers the page (0..1, Pads): the page shrinks away
        static inline void (*s_padTest)(float now) = nullptr;   // v84: Réglages > Manettes > « Tester les manettes » (Pads::TestBegin)
        static inline void (*s_discord)(float now) = nullptr;   // v95: Réglages > Réseau > « Vocal Discord » (social::DiscordOpen)
        static inline float s_tint[3] = { 0.22f, 0.42f, 0.95f }, s_tintW = 0;   // v39: the room takes a controller's colour (Pads)
        static inline float s_joyL[3] = {}, s_joyR[3] = {};                         // v40: the real Joy-Con colours (linear), for the 3D Switch
        static inline bool  s_joyKnown = false;

    private:
        void  Toast(float now, const char *msg) { if (s_toast) s_toast(now, msg); }
        void  Refresh();
        void  SetLayout(bool xmb, float now);                // v50: XMB <-> carousel
        void  StartCode(float now, int purpose = 0);         // v44: choosing the lock code (v87: 0 set / change, 1 remove)
        void  CodeKey(int sym, float now);                   // v87: a symbol of Pin.hpp (0..9)
        void  CodeCheck(float now);
        struct { bool on = false; int n = 0, step = 1, purpose = 0, lit = -1; uint8_t dir[4] = {}, first[4] = {}, cur[4] = {};
                 float at = -100, doneAt = -1, badAt = -1, checkAt = -1, stepAt = -100, litAt = -100, k = 0, v = 0,
                 keyAt[4] = { -100, -100, -100, -100 }; const char *bad = ""; } m_rec;
        void  Act(int bind, const char *name, float now);
        void  ResetCol(int depth, bool fromBelow);
        void  Turn(int dc, float now);
        void  Enter(float now);
        void  Back(float now);
        void  OpenOpt(float now);
        void  CloseOpt(float now, bool chosen);              // v79: the options leave (chosen: the dot glides to the new value first)
        void  Bump(int dir) { m_bumpV += dir * 230.0f; }     // v79: the end of a list - it gives a little and comes back (a spring)
        int   Count(int depth) const;
        struct Icon { float spin = 0, k = 0, wob = 0, kick = 0; };
        // animated (v79: every one glides with Ease.hpp's spring, no hard start): centre y, opacity, « chosen » amount, icon
        // scale, and their speeds; the value shown (it crossfades when it changes) and a slider's number (it counts to it)
        struct Row  { float y = 0, o = 0, a = 0, s = 1, vy = 0, vo = 0, va = 0, vs = 0; int shownV = -9999, oldV = 0; float vAt = -100, sv = -1, svv = 0; };
        int   m_cat = 0, m_depth = 0, m_sel[3] = { 0, 0, 0 }, m_path[3] = { 0, 0, 0 };
        bool  m_open = false;
        float m_cur = 0, m_target = 0, m_tweenFrom = 0, m_tweenT0 = -1, m_openAmt = 0, m_t = 0, m_ringFrom = 0;
        float m_curV = 0, m_openV = 0;                     // v36: glided
        bool  m_ringDrag = false;
        int   m_listFrom = 0;
        float m_colX[3] = { 0, 0, 0 }, m_colA[3] = { 0, 0, 0 };
        float m_colXV[3] = {}, m_colAV[3] = {}, m_colT[3] = { 1, 1, 1 }, m_colTV[3] = {}, m_colT0[3] = { -100, -100, -100 };   // v79: glided; T = its words (XMB parents); T0 = born (the stagger)
        float m_depthCur = 0, m_depthV = 0;                // v79: the depth, glided (the XMB's row of categories slides away with it)
        float m_bump = 0, m_bumpV = 0;                     // v79: a list's end gives a little (pixels, a spring)
        float m_refuseAt = -100;                           // v79: « Pas encore dans Brume » - the chosen row shakes its head
        Row   m_row[3][16];
        Icon  m_ic[kCats];
        float m_nameFadeT0 = -100, m_nameSwapAt = -1; int m_shownCat = 0;
        struct { bool on = false, slider = false; int sel = 0, old = 0; float at = -100; } m_opt;
        float m_optScroll = 0, m_optScrollV = 0;   // v70.9: a long list of options scrolls (the first row shown, it glides)
        // v79 (Yass: « le choix de couleur faut qu'il ait les mêmes animations que le niveau juste avant ») - the options enter one
        // by one (from the right, fading in), their light glides from one to the next, the current value's dot glides to the new
        // one, they leave softly; the slider's number counts and its line of light glides; its arrows give when pressed
        static constexpr int kOptMax = 68;
        const void *m_optIt = nullptr;             // the item whose options are shown (kept while they fade out)
        float m_optK = 0, m_optKV = 0, m_optHold = -100;   // (hold: a value chosen - they stay until the dot is there)
        float m_oe[kOptMax] = {}, m_oev[kOptMax] = {};
        float m_optHi = 0, m_optHiV = 0, m_optDot = 0, m_optDotV = 0;
        float m_sl = 0, m_slV = 0, m_slNudge = 0, m_slNudgeV = 0;
        // v79 - the hints crossfade when they change; the layout change dips (out, the swap, in); the page comes back in one jump
        struct Hints { const char *aL = nullptr, *bL = nullptr, *chip[4] = {}, *lbl[4] = {}; int nr = 0; };
        Hints m_hint[2];                           // [0] shown now, [1] the one fading out
        float m_hintAt = -100;
        int   m_dipTo = -1;                        // the layout to switch to (-1: none)
        float m_dipAt = -100, m_dipInAt = -100;
        bool  m_away = false;                      // the page went away in one jump (Wheel::Warp): its rows come back one by one
        float Dip(float now) const;                // 1 = all there, 0 = gone
        float m_flash = 0;                         // v73: A lights the chosen icon's glass from inside, then it fades (0..1)
        static float OptFirst(int no, int sel);    // v70.9: the first row to show so that the chosen one stays in view
        float OptTop(int no, float selY) const;    // v70.9: where the list's window starts (on screen, never past the bottom)
    };

} // namespace brume
