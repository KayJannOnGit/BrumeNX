// Brume - the lock screen, ported value for value from docs/M1/maquette-brume.html
// (v20-v22 + Yass's 2026-11-15 remark: the last press is a « big bang », no shattering).
//
// v87 (Yass: « une sélection de profil avant l'unlock. Les 3 clics c'est uniquement s'il n'y a pas de code, comportement
// vraiment comme la PS4 ; allumage stylé avec un son d'allumage ; absolument tout doit être animé ») - the flow, like the PS4:
//   1. kBoot    (power-on only): Brume's emblem - the six lights come one by one around a ring in the dark, each a note of
//               D major, the ring turns, « Brume » is written in light, then the ring opens into the fog (~2.9 s; any button
//               skips it)
//   2. kPrompt  the dark screen: the clock, the glass button, « Appuie sur un bouton » (after sleep, the flow starts here)
//   3. kWho     « Qui joue ? »: the profiles' avatars come out of the button like dominoes, a gentle carousel in a dim light
//               (◀ ▶ or the finger, A chooses, B back); skipped when there is only one profile
//   4. kPin     that profile has a code (Pin.hpp, per profile): four presses among ten buttons, dots that fill with light, a
//               note of the chord each; wrong = an elegant shake and a low sound; 5 wrong = a calm pause (the orbs turn)
//      kPresses no code: the three presses of before (heartbeat, the light by steps); with one profile the first button
//               already counts as the first press
//   5. the big bang (stronger: the six lights burst out of it) and the consoles arrive.
// Black at first: only the clock and the A button. Each press a blast of light from the button; behind the front the
// scene keeps more light (0 -> 50 % -> 80 % -> 100 %), texts are pushed by the shock wave and spring back.
// Stop on the way: after 4 s (or B) the light withdraws into the button.
#pragma once
#include "Ui.hpp"
#include <switch.h>

namespace brume {

    struct Blast {
        float t0, ox, oy;
        int   type;            // 1 lights up, -1 withdraws (sleep), 2 flash only
        float R0, R1, dur, amp, to;
        bool  chime;
        uint32_t hit;          // bits 0-2: texts (date, clock, the line under it), bits 3-5: cubes
    };
    struct BlastState { float p, R, a, to; };

    class Lock {
    public:
        static constexpr float kOx = 640.0f, kOy = 546.0f;    // the button's centre
        static constexpr float kFullR = 900.0f, kEdge = 200.0f, kFrontW = 70.0f;
        static constexpr int   kMaxBlasts = 4;
        enum Step { kBoot = 0, kPrompt = 1, kWho = 2, kPin = 3, kPresses = 4, kSteps = 5 };

        void Boot(float now);                  // v87: power-on - the emblem first
        void Reset(float now);                 // locked, asleep (after sleep): the dark screen
        void Skip(float now);                  // v43: no lock screen (Réglages > Écran): the light comes up, the consoles arrive
        void Input(float now, u64 down);       // v87: every button of the frame (Entry, while the wheel is not there)
        void Tap(float x, float y, float now); // v87: the finger
        void PressA(float now);
        void Press(float now, char letter) { m_letter = letter; PressA(now); }   // v29: any button (or a tap: letter 0 = keep)
        void PressB(float now);
        void Update(float now, float dt);
        void Draw(Ui &ui, float now, int hour, int minute, int wday, int day, int month);

        // v44 - the old global code (reglages.ini code= / codeseq=): v87 it is the fallback of the profiles without their own
        static inline bool    s_codeOn = false;
        static inline uint8_t s_code[4] = { 0, 0, 2, 1 };
        static inline bool    s_avatarsOk = false;          // v87: the avatars' strip is on the GPU (Entry, every frame)
        static inline float   s_tilt[2] = { 0, 0 };         // v87: the console's tilt (Entry's gyroscope): a faint parallax
#ifdef BRUME_UISIM
        static inline bool    s_simFlow = false;             // uisim: the old scripts unlock with 3 presses; « demo verrou 1 » = the v87 flow
#endif
        bool  CodeMode() const { return m_step == kPin; }
        Step  StepNow() const { return m_step; }
        bool  Unlocked() const { return m_unlocked; }
        float Wake() const { return m_wake; }
        float Flash() const { return m_flash; }
        float LightBase() const { return m_lightBase; }
        int   BlastCount() const { return m_nb; }
        Blast &BlastAt(int i) { return m_b[i]; }
        const BlastState &StateAt(int i) const { return m_bs[i]; }
        float LightAt(float x, float y) const;  // the shader's light, on the CPU (texts' opacity)

    private:
        void Add(Blast b);
        void WakeUp(float now);
        void GoSleep(float now);
        void Finale(float now);
        bool Sleeping() const;
        void Go(Step s, float now);            // v87: to a step (its elements glide in, the others out)
        void Dim(float now, float to, float ox, float oy, float amp);   // a soft wave of light to `to`
        void Leave(float now);                 // the prompt was pressed: who plays, the code, or the presses
        void Chosen(float now);                // a profile was chosen: its code, or the presses
        void PinKey(float now, int sym);
        void PinBack(float now);
        void CheckCode(float now);
        void DrawBoot(Ui &ui, float now);
        void DrawWho(Ui &ui, float now, float k);
        void DrawPin(Ui &ui, float now, float k);
        void DrawStatus(Ui &ui, float now, float a);
        void DrawHints(Ui &ui, float a, const char *aWord, const char *bWord);

        Step  m_step = kPrompt, m_from = kPrompt;
        float m_stepAt = -100, m_k[kSteps] = {}, m_kv[kSteps] = {};
        // the boot
        float m_bootAt = -100, m_bootEnd = -100;
        int   m_bootNote = 0;
        // who plays
        int   m_who = 0, m_whoN = 0;
        float m_whoCur = 0, m_whoV = 0, m_whoMoveAt = -100, m_chooseAt = -100;
        // the code
        int     m_codeN = 0, m_lit = -1;
        uint8_t m_codeIn[4] = {}, m_want[4] = {};
        float   m_checkAt = -1, m_badAt = -1, m_okAt = -1, m_litAt = -100, m_keyAt[4] = { -100, -100, -100, -100 };
        static inline int   s_fails = 0;               // (kept across sleep while the menu runs: the pause cannot be slept away)
        static inline float s_coolUntil = -1;
        float m_coolSaid = -1;

        // state (names follow the mockup)
        bool  m_awake = false, m_unlocked = false, m_gone = false, m_wakeSnd = false;
        int   m_presses = 0, m_sleepFrom = 0;
        float m_lastInput = 0, m_lightBase = 0, m_wake = 0.45f, m_flash = 0;
        float m_flashFade = 0.6f, m_echoAt[2] = { -1, -1 };   // v59: Aurore (longer rays, two echo waves)
        char  m_letter = 'A';           // v29: the letter in the glass button (the button that was pressed)
        float m_awakeAt = -100, m_goneAt = -100, m_pressAt = -100, m_relockSleepAt = -1;
        float m_lastBeat = -100, m_beatAmp = 0, m_beatGap = 0.17f;
        float m_segOn[3] = { 0, 0, 0 };
        float m_hintOp = 0;
        Blast m_b[kMaxBlasts];
        BlastState m_bs[kMaxBlasts];
        int   m_nb = 0;
        // shock-wave springs for the texts: the date (y 178), the clock (y 280), the line and the step under it (y 420)
        float m_shP[3] = { 0, 0, 0 }, m_shV[3] = { 0, 0, 0 };
    };

    float CubicBezier(float x1, float y1, float x2, float y2, float t);

} // namespace brume
