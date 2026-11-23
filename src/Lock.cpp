// Brume - the lock screen. Every constant comes from docs/M1/maquette-brume.html
// (ORIGIN, MAXR, FULLR, GROW, SHRINK, EDGE, FRONT_W, LIGHT, SLEEP_AFTER, SHOCK_HUD,
// the CSS of .lk / .clock / .refl / .date / .lockbtn / .hint). v87: the flow like the PS4 (see Lock.hpp) and a new layout:
// the status at the top (the messages waiting, the battery), the date above the clock (no greeting: Yass, « c'est nul »),
// then the step (the button, who plays, the code).
#include "Lock.hpp"
#include "Lang.hpp"
#include "Profiles.hpp"
#include "Haptics.hpp"
#include "Audio.hpp"
#include "Settings.hpp"
#include "Clock.hpp"
#include "Ease.hpp"
#include "Busy.hpp"
#include "Pin.hpp"
#include "Notify.hpp"
#include "Charge.hpp"
#include "Ntp.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume {

    namespace {
        constexpr float kMaxR = 1100.0f, kGrow = 2.0f, kShrink = 1.3f;
        constexpr float kLight[4] = { 0.0f, 0.5f, 0.8f, 1.0f };
        constexpr float kSleepAfter = 4.0f;
        constexpr float kShockHud = 300.0f;
        constexpr float kHudY[3] = { 178.0f, 280.0f, 420.0f };
        // v87 - the layout of the steps (1280 x 720)
        constexpr float kBootLen = 2.9f, kEmX = 640, kEmY = 300;   // the emblem
        constexpr float kWhoY = 528, kWhoStep = 150;               // the avatars' row
        constexpr float kPinAvY = 446, kDotsY = 534, kPadY = 624;  // the code
        constexpr float kDim = 0.3f;                               // the light of « who plays » and of the code
        const int kBootNotes[6] = { 62, 66, 69, 74, 78, 81 };     // D F# A D F# A: one per light of the emblem

        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        float Smooth(float a, float b, float x) { const float t = Clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); }
        float EaseOutC(float p) { return 1 - powf(1 - p, 3); }
        float Ease(float t)    { return CubicBezier(0.25f, 0.1f, 0.25f, 1.0f, Clamp01(t)); }   // CSS « ease »
        float EaseOut(float t) { return CubicBezier(0.0f, 0.0f, 0.58f, 1.0f, Clamp01(t)); }    // CSS « ease-out »
        // CSS transition towards a target: exponential approach, ~reaches it in `dur`.
        void Approach(float &v, float target, float dt, float dur) { v += (target - v) * (1 - expf(-dt * 4.0f / dur)); }

        const char *kDays[7]    = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };
        const char *kMonths[12] = { "janvier", "février", "mars", "avril", "mai", "juin", "juillet",   // lang:fr (French only, the English is beside it)
                                    "août", "septembre", "octobre", "novembre", "décembre" };   // lang:fr (French only, the English is beside it)
        constexpr u64 kAnyBtn = HidNpadButton_A | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL |
                                HidNpadButton_ZR | HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_Left | HidNpadButton_Right |
                                HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_StickL | HidNpadButton_StickR;
        char Letter(u64 down) {
            return (down & HidNpadButton_A) ? 'A' : (down & HidNpadButton_X) ? 'X' : (down & HidNpadButton_Y) ? 'Y'
                 : (down & (HidNpadButton_L | HidNpadButton_ZL)) ? 'L' : (down & (HidNpadButton_R | HidNpadButton_ZR)) ? 'R' : 'A';
        }
    }

    float CubicBezier(float x1, float y1, float x2, float y2, float t) {
        // Solve x(s) = t for s (Newton, then bisection fallback), return y(s).
        auto bx = [&](float s) { const float u = 1 - s; return 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; };
        auto by = [&](float s) { const float u = 1 - s; return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s; };
        float s = t;
        for (int i = 0; i < 6; i++) {
            const float x = bx(s) - t;
            const float u = 1 - s;
            const float dx = 3 * u * u * x1 + 6 * u * s * (x2 - x1) + 3 * s * s * (1 - x2);
            if (fabsf(dx) < 1e-5f) break;
            s -= x / dx;
            s = Clamp01(s);
        }
        float lo = 0, hi = 1;
        for (int i = 0; i < 12 && fabsf(bx(s) - t) > 1e-4f; i++) { s = (lo + hi) * 0.5f; if (bx(s) < t) lo = s; else hi = s; }
        return by(s);
    }

    void Lock::Reset(float now) {
        m_awake = false; m_unlocked = false; m_gone = false; m_wakeSnd = false;
        audio::SetMusic(0);                                             // silence in the dark
        m_presses = 0; m_sleepFrom = 0; m_lastInput = now; m_lightBase = 0; m_flash = 0;
        m_flashFade = 0.6f; m_echoAt[0] = m_echoAt[1] = -1;
        m_nb = 0; m_relockSleepAt = -1; m_hintOp = 0;
        m_codeN = 0; m_checkAt = m_badAt = m_okAt = -1; m_chooseAt = -100;
        for (int i = 0; i < 3; i++) { m_segOn[i] = 0; m_shP[i] = m_shV[i] = 0; }
        m_step = m_from = kPrompt; m_stepAt = now;
        for (int i = 0; i < kSteps; i++) { m_k[i] = i == kPrompt ? 1.0f : 0.0f; m_kv[i] = 0; }
        WakeUp(now + 0.3f);   // the console shows the lock screen right away (no browser audio gate here)
    }

    // v87 - power-on: the emblem, then the dark screen (the clock comes once the ring has opened)
    void Lock::Boot(float now) {
        Reset(now);
#ifdef BRUME_UISIM
        if (!s_simFlow) return;
#endif
        m_step = m_from = kBoot; m_stepAt = m_bootAt = now; m_bootNote = 0; m_bootEnd = -100;
        for (int i = 0; i < kSteps; i++) m_k[i] = 0;
        m_awake = true; m_awakeAt = now + kBootLen; m_wakeSnd = true;   // (the boot's chime instead of the wake sound)
    }

    void Lock::WakeUp(float now) {
        if (m_awake) return;
        m_awake = true; m_lastInput = now; m_awakeAt = now;
    }

    void Lock::Add(Blast b) {
        if (m_nb == kMaxBlasts) {                  // the oldest leaves; its light stays acquired
            if (m_b[0].type != 2) m_lightBase = m_b[0].to;
            for (int i = 1; i < m_nb; i++) m_b[i - 1] = m_b[i];
            m_nb--;
        }
        m_b[m_nb++] = b;
    }

    static Blast MakeBlast(float now, float to, float amp) {
        Blast b;
        b.t0 = now; b.ox = Lock::kOx; b.oy = Lock::kOy; b.type = 1; b.R0 = 0; b.R1 = kMaxR;
        b.dur = kGrow; b.amp = amp; b.to = to; b.chime = false; b.hit = 0;
        return b;
    }

    bool Lock::Sleeping() const {
        for (int i = 0; i < m_nb; i++) if (m_b[i].type == -1) return true;
        return false;
    }

    void Lock::Go(Step s, float now) { m_from = m_step; m_step = s; m_stepAt = now; m_lastInput = now; }

    void Lock::Dim(float now, float to, float ox, float oy, float amp) {   // a soft wave of light from (ox, oy) to `to`
        Blast b = MakeBlast(now, to, amp);
        b.ox = ox; b.oy = oy; b.dur = 1.6f;
        if (to < m_lightBase) { b.type = -1; b.R0 = kFullR; b.dur = kShrink; }
        Add(b);
    }

    // ---- v87: the flow ----
    void Lock::Leave(float now) {                                       // the prompt was pressed
        m_lastInput = now;
        m_whoN = Profiles::Count();
#ifdef BRUME_UISIM
        if (!s_simFlow) m_whoN = 1;
#endif
        if (m_whoN > 1) {                                               // who plays: the avatars come out of the button
            m_who = Profiles::ActiveIndex() >= 0 && Profiles::ActiveIndex() < m_whoN ? Profiles::ActiveIndex() : 0;
            m_whoCur = (float)m_who; m_whoV = 0;
            Go(kWho, now);
            Dim(now, kDim, kOx, kOy, 0.9f);
            audio::SetMusic(1);
            for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k, 69, 81), 0.03f - 0.006f * k, true, 0.06f * k);
            haptic::Thump(0.45f);
            m_pressAt = now;
            return;
        }
        Chosen(now);
    }

    void Lock::Chosen(float now) {                                      // this profile plays: its code, or the three presses
        if (m_whoN > 1) Profiles::Choose(m_who);
        m_chooseAt = now;
        m_codeN = 0; m_checkAt = m_badAt = m_okAt = -1;
        if (pin::Get(Profiles::Active(), m_want)) {
            Go(kPin, now);
            if (m_lightBase < kDim - 0.01f) Dim(now, kDim, kOx, kPinAvY, 0.7f);
            audio::SetMusic(1);
            audio::Bell(audio::HarmonyNote(3, 74, 88), 0.03f, true);   // chime(LOCK_CHORD.chord[3] + 24)
            haptic::Tap(0.6f);
            return;
        }
        if (m_whoN > 1) {                                               // the button comes back for the three presses
            Go(kPresses, now);
            m_presses = 0;
            audio::Bell(audio::HarmonyNote(0, 62, 74), 0.03f, true);
            haptic::Tap(0.6f);
            return;
        }
        Go(kPresses, now);                                              // one profile, no code: the first button was the first press
        PressA(now);
    }

    void Lock::Input(float now, u64 down) {
        if (m_unlocked || !down) return;
        switch (m_step) {
            case kBoot:                                                 // any button: straight to the dark screen
                if (down & (kAnyBtn | HidNpadButton_B)) { m_bootNote = 99; Go(kPrompt, now); m_awakeAt = now; m_bootEnd = now; }
                return;
            case kPrompt:
                if (down & kAnyBtn) { m_letter = Letter(down); Leave(now); }
                else if (down & HidNpadButton_B) PressB(now);
                return;
            case kWho: {
                m_lastInput = now;
                const int d = (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) ? -1 : (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) ? 1 : 0;
                if (d) {
                    const int to = m_who + d;
                    if (to < 0 || to >= m_whoN) { audio::Thock(d * 0.4f, 0.8f, 0.05f); m_whoV += d * -2.0f; }
                    else { m_who = to; m_whoMoveAt = now; audio::Nav(audio::HarmonyNote(to & 3, 72, 79), d * 0.3f); haptic::Tick(0.5f); }
                }
                if (down & HidNpadButton_A) { audio::Play(audio::Tick2400, 0, 0.9f); Chosen(now); }
                else if (down & HidNpadButton_B) { audio::Play(audio::Tick1700, 0, 1.0f); GoSleep(now); }
                return;
            }
            case kPin: {
                const int s = pin::FromButtons(down);
                if (s >= 0) PinKey(now, s);
                else if (down & HidNpadButton_B) PinBack(now);
                return;
            }
            case kPresses:
                if (down & kAnyBtn) Press(now, Letter(down));
                if (down & HidNpadButton_B) PressB(now);
                return;
            default: return;
        }
    }

    void Lock::Tap(float x, float y, float now) {
        if (m_unlocked) return;
        switch (m_step) {
            case kBoot: Input(now, HidNpadButton_A); return;
            case kPrompt: m_letter = 'A'; Leave(now); return;
            case kWho: {
                const int i = (int)floorf((x - 640) / kWhoStep + m_whoCur + 0.5f);
                if (fabsf(y - kWhoY) > 80 || i < 0 || i >= m_whoN) return;
                m_lastInput = now;
                if (i == m_who) { audio::Play(audio::Tick2400, 0, 0.9f); Chosen(now); }
                else { m_who = i; m_whoMoveAt = now; audio::Nav(audio::HarmonyNote(i & 3, 72, 79), (x - 640) / 640.0f); haptic::Tick(0.5f); }
                return;
            }
            case kPin: {
                if (fabsf(y - kPinAvY) < 40 && fabsf(x - 640) < 160) { m_codeN = 0; PinBack(now); return; }   // the avatar: back
                const int s = pin::PadHit(640, kPadY, x, y);
                if (s >= 0) PinKey(now, s);
                return;
            }
            case kPresses: Press(now, 0); return;
            default: return;
        }
    }

    void Lock::PinKey(float now, int sym) {
        m_lastInput = now;
        if (s_coolUntil > now) { audio::Thock(0, 0.7f, 0.04f); return; }   // (the pause: nothing to type)
        if (m_codeN >= 4 || m_badAt > 0 || m_okAt > 0 || m_checkAt > 0) return;
        const int i = m_codeN;
        m_codeIn[m_codeN++] = (uint8_t)sym;
        m_keyAt[i] = now; m_lit = sym; m_litAt = now;
        Blast b = MakeBlast(now, m_lightBase, 0.5f);                    // a small flash leaves the dot (type 2: light only)
        b.type = 2; b.ox = kOx + (i - 1.5f) * 40.0f; b.oy = kDotsY; b.R1 = 320.0f; b.dur = 1.1f;
        Add(b);
        const float pan = (i - 1.5f) * 0.25f;
        audio::Bell(audio::HarmonyNote(i, 74, 86), 0.045f, true);       // chime(ch[i] + 24) + dropSound(ch[i] + 36)
        audio::Crystal(pan, 0.35f, audio::HarmonyNote(i, 86, 98));
        haptic::Tick(0.5f);
        if (m_codeN == 4) m_checkAt = now + 0.28f;
    }

    void Lock::PinBack(float now) {
        m_lastInput = now;
        if (m_codeN > 0 && m_checkAt < 0 && m_okAt < 0 && m_badAt < 0) {   // erase the last one
            m_codeN--; m_keyAt[m_codeN] = -100;
            audio::Play(audio::Tick1700, 0, 0.7f); haptic::Tick(0.3f);
            return;
        }
        if (m_okAt > 0) return;
        audio::Play(audio::Tick1700, 0, 1.0f);
        m_codeN = 0; m_checkAt = m_badAt = -1;
        if (m_whoN > 1) Go(kWho, now);                                  // the step before: who plays
        else GoSleep(now);
    }

    void Lock::CheckCode(float now) {
        if (m_step != kPin) return;
        bool ok = true;
        for (int k = 0; k < 4; k++) ok = ok && m_codeIn[k] == m_want[k];
        if (ok) {                                                       // the dots glow, four bells, then the big bang
            m_okAt = now; s_fails = 0;
            Blast b = MakeBlast(now, m_lightBase, 1.4f); b.type = 2; b.oy = kDotsY;
            Add(b);
            for (int k = 0; k < 4; k++) audio::Bell(audio::HarmonyNote(k, 74, 88), 0.04f, true, k * 0.06f);
            haptic::Tap(0.8f);
        } else {                                                        // the dots shake, pink, with a dull sound
            m_badAt = now; s_fails++;
            audio::Thock(0.0f, 0.55f, 0.14f);
            audio::SoftLow(audio::HarmonyNote(0, 36, 48), 0.06f, 0.6f);
            haptic::Thump(0.7f);
            if (s_fails % 5 == 0) {                                     // a calm pause: 30 s, then 60, at most 2 min (never locked for good)
                const float wait = fminf(120.0f, 30.0f * (float)(s_fails / 5));
                s_coolUntil = now + wait;
                audio::SoftLow(audio::HarmonyNote(0, 36, 48), 0.05f, 1.8f);
            }
        }
    }

    // The last press (Yass, 2026-11-15): a big bang - one huge blast of light, a
    // strong shock wave, the flash; no cracks, no shards. Then the entry follows.
    void Lock::Skip(float now) {
        WakeUp(now);
        m_presses = 3; m_unlocked = true; m_gone = true; m_goneAt = now - 1.0f;   // (the clock is already gone)
        m_step = kPresses;
        audio::SetMusic(3);
        Add(MakeBlast(now, 1.0f, 1.0f));                                // the light comes up, softly
    }

    void Lock::PressA(float now) {
        if (m_unlocked) return;
        m_lastInput = now;
        if (!m_awake) { WakeUp(now); return; }
        if (m_presses >= 3) return;
        m_pressAt = now;                                                // pressAnim
        if (Sleeping() && m_presses == 0) {                             // resume where the light was
            m_presses = m_sleepFrom > 1 ? m_sleepFrom : 1;
            Add(MakeBlast(now, kLight[m_presses], 1.2f));
            audio::SetMusic(m_presses); audio::Play(m_presses == 1 ? audio::Press1 : audio::Press2);   // mockup: pressChord(presses-1)
            return;
        }
        m_presses++;
        m_lastBeat = now; m_beatAmp = 0.65f + 0.15f * m_presses; m_beatGap = 0.17f;   // the colon beats with the heart
        audio::SetMusic(m_presses);
        if (m_presses == 3) { Finale(now); return; }
        audio::Play(m_presses == 1 ? audio::Press1 : audio::Press2, 0.0f, 1.0f, 1.0f);   // heartbeat + chord note
        haptic::Thump(0.5f + 0.2f * m_presses);                         // v29: a soft heartbeat under the finger
        Blast b = MakeBlast(now, kLight[m_presses], 1.6f);
        b.chime = true;
        Add(b);
    }

    void Lock::Finale(float now) {
        Blast b = MakeBlast(now, 1.0f, 3.8f);                           // v28: stronger (Yass: the big bang lacked scale)
        b.dur = 2.4f;
        if (m_step == kPin) b.oy = kDotsY;                              // (v87: from the code's dots)
        Add(b);
        m_flash = 0.85f;                                                 // + the rays and the core (glass_fsh, v28)
        haptic::Thump(1.0f); haptic::Swell(0.8f, 1.6f);                  // v29: a knock, then a slow wave
        if (Settings::s_unlockStyle == 1) {                              // v59 « Aurore »: the bang goes on blooming
            m_flashFade = 0.38f;                                         // the rays run longer (~2.2 s)
            m_echoAt[0] = now + 0.45f; m_echoAt[1] = now + 1.05f;        // with the chord opening: two more waves
            haptic::Swell(1.0f, 2.6f);
        }
        audio::SetMusic(3);
        audio::Play(audio::Unlock12);  // v38.1 (Yass): the unlock of the mockup v12 - the deep boom, the air, the bells in the reverb
        for (int k = 0; k < 6; k++) audio::Bell(audio::HarmonyNote(k & 3, 86, 100), 0.012f, true, 0.35f + 0.07f * k);   // v87: the six lights flying out, each a glint
        m_gone = true; m_goneAt = now + 0.06f;
        m_unlocked = true;
    }

    void Lock::GoSleep(float now) {
        if (m_unlocked) return;
        m_codeN = 0; m_checkAt = m_badAt = m_okAt = -1;
        m_sleepFrom = m_presses ? (m_presses < 2 ? m_presses : 2) : (m_sleepFrom ? m_sleepFrom : 2);
        m_presses = 0; m_lastInput = now;
        audio::SetMusic(0); audio::Play(audio::Sigh);                  // the light withdraws, the soundscape fades out
        Blast b = MakeBlast(now, 0.0f, 0.6f);
        b.type = -1; b.R0 = kFullR; b.dur = kShrink;
        Add(b);
        Go(kPrompt, now);                                               // v87: back to the start (the button in the dark)
        m_sleepFrom = 0;                                                // (from the prompt, the presses start again from the first)
    }

    void Lock::PressB(float now) {
        if (m_unlocked) {                                               // relock
            m_unlocked = false; m_gone = false;
            m_presses = 2;
            m_relockSleepAt = now + 0.35f;
            return;
        }
        if (m_step == kPresses && m_presses == 0 && m_whoN > 1 && !Sleeping()) {   // v87: the step before - who plays
            audio::Play(audio::Tick1700, 0, 1.0f);
            Go(kWho, now);
            return;
        }
        bool lit = m_presses > 0 || m_lightBase > 0;
        for (int i = 0; i < m_nb; i++) if (m_b[i].type == 1) lit = true;
        if (lit && !Sleeping()) GoSleep(now);
    }

    float Lock::LightAt(float hx, float hy) const {
        float lv = m_lightBase, fl = 0;
        for (int i = 0; i < m_nb; i++) {
            const Blast &b = m_b[i]; const BlastState &s = m_bs[i];
            const float d = hypotf(hx - b.ox, hy - b.oy), ins = 1 - Smooth(s.R - kEdge, s.R, d);
            if (b.type == 1) lv += (s.to - lv) * ins; else if (b.type == -1) lv = s.to + (lv - s.to) * ins;
            const float x = (d - s.R) / (d < s.R ? kFrontW * 2.6f : kFrontW);
            fl += s.a * expf(-x * x);
        }
        const float fromBtn = 1 - 0.5f * (1 - (lv < 1 ? lv : 1)) * Smooth(0.12f, 1, hypotf(hx - kOx, hy - kOy) / kFullR);
        const float v = lv * fromBtn + fl * 1.2f;
        return v > 0 ? v : 0;
    }

    void Lock::Update(float now, float dt) {
        if (m_awake && !m_wakeSnd && now >= m_awakeAt) { m_wakeSnd = true; audio::Play(audio::Wake); }   // mockup: wakeUp()
        // v87 - the boot: the emblem's notes as its lights come (an original chime in D, from the menu's own bells)
        if (m_step == kBoot) {
            const float t = now - m_bootAt;
            if (m_bootNote == 0 && t >= 0.25f) { audio::SoftLow(38, 0.07f, 1.8f); m_bootNote = 1; }
            for (int k = 0; k < 6; k++)
                if (m_bootNote == 1 + k && t >= 0.35f + 0.17f * k) { audio::Bell(kBootNotes[k], 0.032f - 0.002f * k, true); haptic::Tick(0.12f); m_bootNote++; }
            if (m_bootNote == 7 && t >= 1.4f) { audio::Bell(86, 0.022f, true); audio::Bell(90, 0.016f, true, 0.09f); audio::SoftLow(50, 0.045f, 1.1f); m_bootNote++; }
            if (m_bootNote == 8 && t >= 2.15f) {                           // the ring opens into the fog
                audio::PlaySweep(audio::Whoosh, -0.25f, 0.25f, 0.7f, 0.22f);
                audio::Bell(74, 0.02f, true);
                haptic::Thump(0.25f);
                Blast b = MakeBlast(now, 0.0f, 0.9f);
                b.type = 2; b.ox = kEmX; b.oy = kEmY; b.R1 = 760; b.dur = 1.3f;
                Add(b);
                m_bootNote++;
            }
            if (t >= kBootLen) { Go(kPrompt, now); m_bootEnd = now; m_lastInput = now; }
        }
        for (int i = 0; i < kSteps; i++) Glide(m_k[i], m_kv[i], m_step == i ? 1.0f : 0.0f, dt, m_step == i ? 5.0f : 7.0f);
        Glide(m_whoCur, m_whoV, (float)m_who, dt, 8.0f);
        // finished blasts become the acquired light
        while (m_nb && now - m_b[0].t0 >= m_b[0].dur) {
            if (m_b[0].type != 2) m_lightBase = m_b[0].to;
            for (int i = 1; i < m_nb; i++) m_b[i - 1] = m_b[i];
            m_nb--;
        }
        if (m_relockSleepAt > 0 && now >= m_relockSleepAt) { m_relockSleepAt = -1; GoSleep(now); }
        if (s_coolUntil > now && m_step == kPin) m_lastInput = now;   // (the pause does not count as idle)
        const float idle = now - m_lastInput;
        if (!m_unlocked && m_step == kPresses && m_presses > 0 && m_presses < 3 && idle > kSleepAfter) GoSleep(now);
        if (!m_unlocked && m_step == kPresses && m_presses == 0 && m_lightBase > 0.01f && idle > 12.0f) GoSleep(now);
        if (!m_unlocked && m_step == kWho && idle > 15.0f) GoSleep(now);
        if (!m_unlocked && m_step == kPin && idle > 20.0f) GoSleep(now);
        // the code's timeline (check 280 ms after the 4th key; wrong: back to empty after 650 ms; right: 380 ms of glow, then
        // the big bang); the pause's end: a soft note
        if (m_checkAt > 0 && now >= m_checkAt) { m_checkAt = -1; CheckCode(now); }
        if (m_badAt > 0 && now - m_badAt > 0.65f) { m_badAt = -1; m_codeN = 0; }
        if (m_okAt > 0 && now - m_okAt > 0.38f) { m_okAt = -1; Finale(now); }
        if (s_coolUntil > 0 && now >= s_coolUntil) { s_coolUntil = -1; if (m_step == kPin) { audio::Bell(audio::HarmonyNote(2, 74, 86), 0.03f, true); haptic::Tick(0.4f); } }
        const bool hintOn = m_awake && !m_unlocked && ((m_step == kPrompt && now - m_awakeAt > 1.2f && !Sleeping()) ||
                                                      (m_step == kPresses && m_presses == 0 && idle > 2.5f));
        Approach(m_hintOp, hintOn ? 0.6f : 0.0f, dt, 1.2f);

        for (int i = 0; i < m_nb; i++) {                               // blastAt
            const Blast &b = m_b[i]; BlastState &s = m_bs[i];
            s.p = (now - b.t0) / b.dur; if (s.p > 1) s.p = 1; if (s.p < 0) s.p = 0;
            if (b.type > 0) {
                s.R = b.R0 + (b.R1 - b.R0) * EaseOutC(s.p);
                s.a = b.amp * powf(1 - s.p, 1.4f) * (s.p * 16 < 1 ? s.p * 16 : 1);
                s.to = b.to * (1 + 0.4f * (1 - s.p) * (1 - s.p));
            } else {
                s.R = b.R0 * (1 - s.p * s.p);
                s.a = b.amp * (s.p * 5 < 1 ? s.p * 5 : 1) * (1 - s.p * s.p * s.p);
                s.to = b.to;
            }
        }
        // shock wave on the texts: pushed outward as the front passes, then a spring
        for (int i = 0; i < m_nb; i++) {
            Blast &b = m_b[i];
            if (b.type == 2) continue;
            for (int h = 0; h < 3; h++) {
                if (b.hit & (1u << h)) continue;
                const float dy = kHudY[h] - b.oy, d = fabsf(dy);
                if (b.type == 1 ? m_bs[i].R >= d : m_bs[i].R <= d) {
                    b.hit |= 1u << h;
                    m_shV[h] += (dy < 0 ? -1.0f : (dy > 0 ? 1.0f : -1.0f)) * (b.type == 1 ? 1.0f : -0.45f) * kShockHud * b.amp / 1.6f;
                }
            }
        }
        for (int h = 0; h < 3; h++) { m_shV[h] += (-140.0f * m_shP[h] - 10.0f * m_shV[h]) * dt; m_shP[h] += m_shV[h] * dt; }

        static const float kWake[4] = { 0.45f, 0.7f, 0.88f, 1.0f };
        const float target = m_unlocked ? 1.0f : (m_step == kWho || m_step == kPin ? 0.62f : kWake[m_presses]);
        m_wake += (target - m_wake) * (dt * 1.6f < 1 ? dt * 1.6f : 1);
        m_flash -= dt * m_flashFade; if (m_flash < 0) m_flash = 0;
        for (int k = 0; k < 2; k++)                                      // v59: Aurore's echo waves (light stays full)
            if (m_echoAt[k] > 0 && now >= m_echoAt[k]) {
                m_echoAt[k] = -1;
                Blast e = MakeBlast(now, 1.0f, k ? 1.6f : 2.6f);
                e.dur = k ? 2.6f : 2.2f;
                Add(e);
                haptic::Thump(k ? 0.35f : 0.55f);
            }
        for (int i = 0; i < 3; i++) Approach(m_segOn[i], (m_unlocked ? 3 : m_presses) > i ? 1.0f : 0.0f, dt, 0.6f);
    }

    // ================================================================ drawing
    // v87 - the emblem (power-on): the six lights of Brume's ring, one by one, each its note; « Brume » written in light; the ring opens
    void Lock::DrawBoot(Ui &ui, float now) {
        const float kb = Clamp01(m_k[kBoot]) * (m_step == kBoot ? 1.0f : Clamp01(1 - (now - m_bootEnd) / 0.5f));
        if (kb <= 0.003f) return;
        const float t = now - m_bootAt;
        const float open = EaseGlide((t - 2.15f) / Pace(0.9f)), fade = 1 - EaseGlide((t - 2.2f) / Pace(0.7f));
        const float rad = 26 + 22 * EaseGlide((t - 0.3f) / Pace(1.4f)) + 360 * open;
        const float spin = t + 0.9f * EaseGlide((t - 1.3f) / Pace(1.0f));          // (it gathers a little speed before it opens)
        const float a = kb * fade;
        ui.Light(kEmX - 170, kEmY - 130, kEmX + 170, kEmY + 130, ps2::A(ps2::kHalo, 0.16f * a * Clamp01((t - 0.3f) / 1.2f)), true);
        for (int k = 5; k >= 0; k--) {
            const float ta = 0.35f + 0.17f * k, e = EaseGlide((t - ta) / Pace(0.5f));
            if (e <= 0.003f) continue;
            busy::Orb(ui, k, kEmX, kEmY, rad, a * e, spin, ps2::kHalo);
            const float pop = Clamp01((t - ta) / 0.12f) * (1 - Clamp01((t - ta - 0.12f) / 0.5f));   // a small bloom as it comes
            if (pop > 0.003f) { float x, y; busy::OrbAt(k, spin, kEmX, kEmY, rad, x, y); ui.Light(x - 30, y - 30, x + 30, y + 30, ps2::A(ps2::kChosen, 0.45f * pop * a), true); }
        }
        // « Brume », letter by letter (each comes out of the fog), a soft light under it
        static const char *const kWord = "Brume";
        const float ls = 16, wordW = ui.Width(fonts::kPageTitle, kWord, ls), top = kEmY + 84;
        const float wa = a * Clamp01((t - 1.3f) / 0.5f);
        if (wa > 0.003f) ui.Light(kEmX - wordW / 2 - 70, top - 8, kEmX + wordW / 2 + 70, top + 52, ps2::A(ps2::kHalo, 0.22f * wa), true);
        float pen = kEmX - wordW / 2;
        for (int i = 0; kWord[i]; i++) {
            const float la = a * EaseGlide((t - 1.35f - 0.07f * i) / Pace(0.6f));
            const uint32_t cp = (uint32_t)kWord[i];
            if (la > 0.003f) ui.Glyph(fonts::kPageTitle, cp, pen, top + 6 * (1 - la), ps2::A(ps2::kChosen, la), 0, 0, 0, 0);
            pen += ui.Advance(fonts::kPageTitle, cp) + ls;
        }
    }

    // the status at the top: the messages that wait (left), the battery (right) - faint in the dark, clearer with the light
    void Lock::DrawStatus(Ui &ui, float now, float a) {
        if (a <= 0.003f) return;
        const int n = notify::Waiting();
        if (n > 0) {
            char t[48];
            if (n == 1) snprintf(t, sizeof t, "1 NOTIFICATION");
            else snprintf(t, sizeof t, "%d NOTIFICATIONS", n);
            const float x = 52, y = 40;
            ui.Light(x - 14, y - 14, x + 14, y + 14, ps2::A(ps2::kHalo, 0.5f * a * (0.75f + 0.25f * sinf(now * 2.2f))), true);
            ui.Shape(x - 3.5f, y - 3.5f, x + 3.5f, y + 3.5f, 0, 0, 7, 7, ps2::A(ps2::kChosen, a), 24, 7, 7, 3.5f);
            const float w = ui.Width(fonts::kGameInfo, t, 3);
            ui.TextCentered(fonts::kGameInfo, t, x + 16 + w * 0.5f, y - 7, 3, ps2::A(ps2::kWords, 0.85f * a));
        }
        if (charge::Known()) {                                           // five small lights and the level
            const int pct = charge::Pct();
            const bool ch = charge::Charging(), low = !ch && pct <= 20;
            char t[16]; snprintf(t, sizeof t, "%d %%", pct);
            const float w = ui.Width(fonts::kSetDesc, t, 1), right = 1228, y = 40;
            const Rgba col = ch ? ps2::kGreen : (low ? Rgba{ 1.0f, 0.62f, 0.22f, 1 } : ps2::kChoice);
            ui.TextCentered(fonts::kSetDesc, t, right - w * 0.5f, y - 8, 1, ps2::A(ps2::kWords, 0.85f * a));
            const int lit = (pct + 19) / 20;
            for (int i = 0; i < 5; i++) {
                const float x = right - w - 18 - (4 - i) * 11, on = i < lit ? 1.0f : 0.0f;
                const float br = ch && i == lit - 1 ? 0.55f + 0.45f * sinf(now * 3.0f) : 1.0f;   // charging: the last one breathes
                if (on > 0) {
                    ui.Light(x - 9, y - 9, x + 9, y + 9, ps2::A(col, 0.35f * a * br), true);
                    ui.Shape(x - 2.6f, y - 2.6f, x + 2.6f, y + 2.6f, 0, 0, 5.2f, 5.2f, ps2::A(ps2::Mix(col, ps2::kChosen, 0.35f), a * br), 24, 5.2f, 5.2f, 2.6f);
                } else ui.Shape(x - 2.2f, y - 2.2f, x + 2.2f, y + 2.2f, 0, 0, 4.4f, 4.4f, ps2::A(ps2::kFaint, 0.35f * a), 24, 4.4f, 4.4f, 2.2f);
            }
        }
    }

    void Lock::DrawHints(Ui &ui, float a, const char *aWord, const char *bWord) {   // as everywhere (v70): the letter in colour, its word
        if (a <= 0.003f) return;
        const float hc = 720 - 28 - 13.5f;
        float hx = 46;
        auto Hint = [&](char btn, const char *label) {
            const Rgba bc = btn == 'B' ? ps2::kPink : ps2::kChoice;
            hx += ui.Glyph(fonts::kButton, (uint32_t)btn, hx, hc - 10, ps2::A(bc, a), 0, 0, 0, 0) + 10;
            for (const char *q = lang::T(label); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * a), 0, 0, 0, 0) + 0.5f;
            hx += 30;
        };
        if (aWord) Hint('A', aWord);
        if (bWord) Hint('B', bWord);
    }

    // « Qui joue ? » - the avatars came out of the button one after the other (dominoes), the chosen one in the middle, lit
    void Lock::DrawWho(Ui &ui, float now, float k) {
        if (k <= 0.003f || m_whoN <= 0) return;
        const bool leaving = m_step != kWho;
        const float tt = ui.Width(fonts::kSetSel, "Qui joue ?", 1.0f);
        ui.Light(640 - tt / 2 - 60, 424, 640 + tt / 2 + 60, 470, ps2::A(ps2::kTitleGlow, 0.13f * k), true);
        ui.TextCentered(fonts::kSetSel, "Qui joue ?", 640, 430 + 8 * (1 - k), 1.0f, ps2::A(ps2::kTitle, k));
        for (int i = 0; i < m_whoN; i++) {
            const float d = i - m_whoCur, f = fmaxf(0.0f, 1 - fabsf(d));
            const float vis = Clamp01(1 - (fabsf(d) - 2.3f) / 0.8f);
            if (vis <= 0.003f) continue;
            const float e = leaving ? 1.0f : EaseGlide((now - m_stepAt - 0.06f * fabsf((float)(i - m_who))) / Pace(0.6f));
            float x = 640 + (640 + d * kWhoStep - 640) * e, y = kOy + (kWhoY - kOy) * e;
            float r = (30 + 12 * f) * (0.5f + 0.5f * e);
            float al = k * vis * e * (0.42f + 0.58f * f);
            if (leaving && i == m_who && m_step == kPin) {               // the chosen one rises to its place above the code
                const float g = 1 - k;
                y += (kPinAvY - y) * g; r += (24 - r) * g; al = fmaxf(al, 0.0f);
            } else if (leaving) al *= k * k;                              // the others sink back into the fog
            if (al <= 0.003f) continue;
            if (i == m_who) {                                            // the chosen one: its light, a ring of the accent
                const float pulse = 1 + 0.06f * sinf((now - m_whoMoveAt) * 3.0f) * expf(-(now - m_whoMoveAt) * 1.5f);
                r *= pulse;
                ui.Light(x - r * 2.2f, y - r * 2.2f, x + r * 2.2f, y + r * 2.2f, ps2::A(ps2::kHalo, 0.42f * al), true);
                ui.Shape(x - r * 1.5f, y - r * 1.5f, x + r * 1.5f, y + r * 1.5f, -1, -1, 1, 1, Settings::Accent(0.55f * al), 21);
            }
            const int slot = Profiles::SlotAt(i);
            if (slot >= 0 && s_avatarsOk) Profiles::DrawAvatar(ui, slot, x, y, r, al);
            else {                                                       // no avatar: its initial in a soft disc of the accent
                ui.Shape(x - r, y - r, x + r, y + r, 0, 0, 2 * r, 2 * r, Settings::Accent(0.32f * al), 24, 2 * r, 2 * r, r);
                char ini[8] = { 0 };
                const char *q = Profiles::NameAt(i); const uint32_t cp = NextCodepoint(q);
                if (cp > 0 && cp < 0x80) { ini[0] = (char)cp; ui.TextCentered(fonts::kSetSel, ini, x, y - 14, 0, ps2::A(ps2::kChosen, al)); }
            }
            const char *nm = Profiles::NameAt(i);
            if (i == m_who) ui.TextCentered(fonts::kSetOpt, nm, x, y + r + 14, 0, ps2::A(ps2::kChosen, al));
            else ui.TextCentered(fonts::kHint, nm, x, y + r + 16, 0.5f, ps2::A(ps2::kFaint, 0.9f * al));
        }
        DrawHints(ui, k * (leaving ? k : 1.0f), "Choisir", "Retour");
    }

    // the code: who it is, four dots that fill with light, the key of the ten buttons (also the touch pad)
    void Lock::DrawPin(Ui &ui, float now, float k) {
        if (k <= 0.003f) return;
        const int me = Profiles::ActiveIndex();
        {   // the avatar (it came up from « Qui joue ? »; alone: it fades in) and the name
            const float r = 24, x = 640, y = kPinAvY;
            const float al = m_from == kWho ? (m_k[kWho] > 0.05f ? Clamp01((k - 0.6f) / 0.4f) : k) : k;
            const int slot = Profiles::SlotAt(me);
            ui.Light(x - 60, y - 50, x + 60, y + 50, ps2::A(ps2::kHalo, 0.3f * k), true);
            if (slot >= 0 && s_avatarsOk) Profiles::DrawAvatar(ui, slot, x, y, r, al);
            else if (al > 0.003f) {
                ui.Shape(x - r, y - r, x + r, y + r, 0, 0, 2 * r, 2 * r, Settings::Accent(0.32f * al), 24, 2 * r, 2 * r, r);
                char ini[8] = { 0 };
                const char *q = Profiles::ActiveName(); const uint32_t cp = NextCodepoint(q);
                if (cp > 0 && cp < 0x80) { ini[0] = (char)cp; ui.TextCentered(fonts::kSetOpt, ini, x, y - 11, 0, ps2::A(ps2::kChosen, al)); }
            }
        }
        const float rise = 10 * (1 - k);
        float shake = 0;
        if (m_badAt > 0) { const float t = (now - m_badAt) / 0.5f; if (t < 1) shake = -16.0f * (1 - t) * (1 - t) * sinf(t * 3.14159f * 5.0f); }
        const bool cool = s_coolUntil > now;
        if (cool) {                                                       // the calm pause: the orbs turn where the dots were
            const float ca = k * Clamp01((now - (s_coolUntil - fminf(120.0f, 30.0f * (float)(s_fails / 5)))) / 0.6f);
            busy::Orbs(ui, 640, kDotsY + rise, 20, ca, now, ps2::kHalo);
            char t[96]; snprintf(t, sizeof t, lang::T("Trop d'essais  \xc2\xb7  r\xc3\xa9" "essaie dans %d s"), (int)ceilf(s_coolUntil - now));
            ui.TextCentered(fonts::kHint, t, 640, kDotsY + 34 + rise, 0.5f, ps2::A(ps2::kFaint, 0.95f * k));
        } else {
            const int state = m_okAt > 0 ? 1 : (m_badAt > 0 ? 2 : 0);
            pin::DrawDots(ui, 640, kDotsY + rise, state == 2 ? 4 : m_codeN, m_keyAt, shake, state, k, now);
            const char *line = state == 2 ? "Ce n'est pas le bon code" : (state == 1 ? "Bienvenue" : "Entre ton code");
            ui.TextCentered(fonts::kHint, line, 640 + shake * 0.4f, kDotsY + 26 + rise, 0.5f,
                            ps2::A(state == 2 ? ps2::kPink : (state == 1 ? ps2::kChosen : ps2::kFaint), 0.95f * k));
        }
        pin::DrawPad(ui, 640, kPadY + rise * 1.5f, k * (cool ? 0.35f : 1.0f), m_lit, m_litAt, now);
        DrawHints(ui, k, nullptr, m_codeN > 0 && m_codeN < 4 ? "Effacer" : "Retour");
    }

    void Lock::Draw(Ui &ui, float now, int hour, int minute, int wday, int day, int month) {
        // v87 - the unlock's burst: the six lights fly out of the big bang in a spiral, with their trails (the emblem's goodbye)
        if (m_unlocked && m_goneAt > 0) {
            const float t = now - (m_goneAt - 0.06f);
            if (t >= 0 && t < 1.5f) {
                const float oy = m_step == kPin ? kDotsY : kOy, fa = powf(1 - t / 1.5f, 1.4f) * Clamp01(t / 0.08f);
                auto P = [&](int k, float tt, float &x, float &y) {
                    const float e = EaseGlide(tt / Pace(1.3f)), ang = k * 1.0471976f + tt * 2.4f, rr = 20 + 620 * e;
                    x = kOx + cosf(ang) * rr * 1.2f; y = oy + sinf(ang) * rr * 0.72f;
                };
                for (int k = 0; k < 6; k++) {
                    float hx, hy; P(k, t, hx, hy);
                    float lx = hx, ly = hy;
                    for (int i = 1; i <= 6; i++) {
                        float x, y; P(k, fmaxf(0.0f, t - i * 0.05f), x, y);
                        ui.Streak(lx, ly, x, y, 2.6f, Rgba{ 0.8f * powf(1 - i / 7.0f, 3), 0.9f * powf(1 - i / 7.0f, 2), 1.0f * (1 - i / 7.0f), 1.6f * fa });
                        lx = x; ly = y;
                    }
                    ui.Light(hx - 26, hy - 26, hx + 26, hy + 26, ps2::A(ps2::kHalo, 0.6f * fa), true);
                    ui.Shape(hx - 3, hy - 3, hx + 3, hy + 3, 0, 0, 6, 6, ps2::A(ps2::kChosen, fa), 24, 6, 6, 3);
                }
            }
        }
        // .lk.gone: opacity .45 s ease, scale 1.04 .6 s cubic-bezier(.5,0,.2,1)
        float lkOp = 1, lkScale = 1;
        if (m_gone) {
            const float g = now - m_goneAt;
            if (g > 0) { lkOp = 1 - Ease(g / 0.45f); lkScale = 1 + 0.04f * CubicBezier(0.5f, 0, 0.2f, 1, Clamp01(g / 0.6f)); }
            if (lkOp <= 0.001f) return;
        }
        DrawBoot(ui, now);
        ui.SetZoom(lkScale);   // the 4 % zoom of the fade-out, around the screen centre
        const float sw = now - m_awakeAt;                 // since awake
        const float clockK = m_awake && sw > 0 ? Ease(sw / 1.4f) : 0;   // opacity 1.4 s ease, blur 16 px -> 0
        const float px = s_tilt[0] * 5.0f, py = -s_tilt[1] * 3.0f;     // v87: a faint parallax when the console tilts
        char buf[96], up[128];

        // v87 - the status at the top
        DrawStatus(ui, now, lkOp * clockK * fmaxf(0.45f, fminf(1.0f, LightAt(640, 40) * 1.3f)));

        // light sweep across the digits: 8 s cycle, 55 % moving (cubic-bezier(.5,0,.5,1)), 1.6 -> -0.6
        float cyc = fmodf(now, 8.0f) / 8.0f, P = -0.6f;
        if (cyc < 0.55f) P = 1.6f + (-0.6f - 1.6f) * CubicBezier(0.5f, 0, 0.5f, 1, cyc / 0.55f);
        const float sweepX = -2304.0f * P + 1792.0f;

        // the date, above the clock (v87: where the greeting was - no greeting any more): « mardi 17 novembre », upper case,
        // faint in the dark, clear with the light
        if (wday >= 0 && wday < 7 && month >= 1 && month <= 12) {
            static const char *const kDaysEn[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
            static const char *const kMonthsEn[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                                       "September", "October", "November", "December" };
            if (lang::En()) snprintf(buf, sizeof(buf), "%s, %s %d", kDaysEn[wday], kMonthsEn[month - 1], day);   // v64
            else snprintf(buf, sizeof(buf), "%s %d %s", kDays[wday], day, kMonths[month - 1]);
            Upper(buf, up, sizeof(up));
            const float dateOp = clockK * (0.42f + 0.58f * fminf(1.0f, LightAt(640, 178))) * lkOp;
            if (dateOp > 0.003f) ui.TextCentered(fonts::kDate, up, 640 + px * 0.6f, 170 + m_shP[0] + py * 0.6f, 7, Hex(0x8f9ac2, dateOp));
        }

        // the clock: HH : MM  (colon in its own span, padding 0 .04em, letter-spacing -2px)
        const float cTop = 205 + m_shP[1] + py;
        const float ls = -2.0f, pad = 6.0f;
        // v83: in 12 h (Réglages > Système > Date et heure > Format 24 h off) « 9:41 », its AM / PM small beside it
        const bool h12 = !sysclock::s_h24;
        const int hShown = h12 ? (hour % 12 ? hour % 12 : 12) : (hour < 0 ? 0 : hour % 24);
        uint32_t cps[5]; int ng = 0;
        if (!h12 || hShown >= 10) cps[ng++] = (uint32_t)('0' + hShown / 10);
        cps[ng++] = (uint32_t)('0' + hShown % 10);
        const int colonAt = ng;
        cps[ng++] = ':';
        cps[ng++] = (uint32_t)('0' + (minute / 10) % 10); cps[ng++] = (uint32_t)('0' + minute % 10);
        float w = 0;
        for (int i = 0; i < ng; i++) w += ui.Advance(fonts::kClock, cps[i]) + ls + (i == colonAt ? 2 * pad : 0);
        const float env0 = (now - m_lastBeat) * 1000.0f, env1 = env0 - m_beatGap * 1000.0f;
        auto Env = [](float x) { return x < 0 ? 0.0f : (x < 55 ? x / 55 : expf(-(x - 55) / 120)); };
        const float pulse = (Env(env0) + 0.6f * Env(env1)) * m_beatAmp;
        const float colonP = fminf(1.0f, fmaxf(pulse, 0.45f * (0.5f + 0.5f * sinf(now * 2.4f))));
        const float clockOp = clockK * lkOp;
        const float reflOp = fminf(1.0f, 0.16f * LightAt(640, 390)) * lkOp;
        const float mirror = 713.0f + m_shP[1] * 0.6f;   // translateY(cs*1.02 + shC*.6) scaleY(-1)
        const float reflTop = 2 * mirror - (cTop + 150);
        // three passes over the same layout: glow (drop-shadow), reflection, digits
        for (int pass = 0; pass < 3; pass++) {
            if (pass == 0 && clockOp < 0.003f) continue;
            if (pass == 1 && reflOp < 0.003f) continue;
            if (pass == 2 && clockOp < 0.003f) continue;
            float pen = 640 + px - w * 0.5f;
            for (int i = 0; i < ng; i++) {
                const bool colon = i == colonAt;
                const uint32_t cp = cps[i];
                if (colon) pen += pad;
                if (pass == 0) {
                    const float a = 0.35f * clockOp * (colon ? (0.45f + 0.55f * colonP) : 1);
                    ui.Glyph(fonts::kClockGlow, cp, pen, cTop, Hex(0x506ef0, a), 3, 0, 0, 0);
                    // while waking up the digits come out of a blur(16px): a soft copy fades away
                    if (clockK < 1) ui.Glyph(fonts::kClockGlow, cp, pen, cTop, Hex(0xd4defe, (1 - clockK) * clockOp * 1.6f), 3, 0, 0, 0);
                } else if (pass == 1) {
                    ui.Glyph(fonts::kClock, cp, pen, cTop, Rgba{ 1.5f * ui.Scale(), 0, 0, 1 }, 4, reflTop, sweepX, reflOp, true, mirror);
                } else {
                    if (colon) ui.Glyph(fonts::kClock, cp, pen, cTop, Rgba{ 1, 1, 1, 1 }, 2, cTop, 0, clockOp * clockK * (0.45f + 0.55f * colonP));
                    else       ui.Glyph(fonts::kClock, cp, pen, cTop, Rgba{ 1, 1, 1, 1 }, 1, cTop, sweepX, clockOp * clockK);
                }
                pen += ui.Advance(fonts::kClock, cp) + ls;
                if (colon) pen += pad;
            }
        }
        if (h12 && clockOp * clockK > 0.003f) {           // v83: AM / PM beside the digits, small, like the date
            const char *ap = hour < 12 ? "AM" : "PM";
            const float aw = ui.Width(fonts::kDate, ap, 4);
            ui.TextCentered(fonts::kDate, ap, 640 + px + w * 0.5f + 16 + aw * 0.5f, cTop + 34, 4, Hex(0xb4c0e6, 0.9f * clockOp * clockK));
        }
        // v87 - the clock asking Internet for the time (at start): the orbs, small, beside the minutes
        {
            static busy::Fade s_sync;
            const float sk = s_sync.Step(ntp::Syncing(), now, 0.6f) * clockOp;
            if (sk > 0.003f) busy::Orbs(ui, 640 + px + w * 0.5f + 34, cTop + 112, 9, sk, now);
        }

        // the glass button (the prompt and the three presses): appears .35 s after waking (opacity 1.2 s ease, scale .7 -> 1,
        // 1.3 s cubic-bezier(.2,1,.3,1)); v87: it sinks back when another step takes its place
        const float bk = Clamp01(m_k[kPrompt] + m_k[kPresses]);
        const float bt = sw - 0.35f;
        const float btnOp = (m_awake ? Ease(bt / 1.2f) : 0) * lkOp * bk;
        if (btnOp > 0.003f) {
            const float bs = (0.7f + 0.3f * CubicBezier(0.2f, 1, 0.3f, 1, Clamp01(bt / 1.3f))) * (0.82f + 0.18f * bk);
            const float cx = 640, cy = 546;
            // press: A scales 1 -> .84 (14 %) -> 1.07 (52 %) -> 1 in 640 ms, cubic-bezier(.2,.8,.3,1)
            float aS = 1;
            const float pt = (now - m_pressAt) / 0.64f;
            if (pt >= 0 && pt < 1) {
                const float e = CubicBezier(0.2f, 0.8f, 0.3f, 1, pt);
                aS = e < 0.14f ? 1 - 0.16f * e / 0.14f : (e < 0.52f ? 0.84f + 0.23f * (e - 0.14f) / 0.38f : 1.07f - 0.07f * (e - 0.52f) / 0.48f);
            }
            // glow: opacity 0 -> 1 (18 %) -> 0, scale .7 -> 1 -> 2.1, 950 ms ease-out
            const float gt = (now - m_pressAt) / 0.95f;
            if (gt >= 0 && gt < 1) {
                const float e = EaseOut(gt);
                const float gop = e < 0.18f ? e / 0.18f : 1 - (e - 0.18f) / 0.82f;
                const float gsc = e < 0.18f ? 0.7f + 0.3f * e / 0.18f : 1 + 1.1f * (e - 0.18f) / 0.82f;
                const float R = 46 * gsc * bs;
                ui.Shape(cx - R, cy - R, cx + R, cy + R, -46 * gsc, -46 * gsc, 46 * gsc, 46 * gsc, Rgba{ 1, 1, 1, gop * lkOp }, 11, 46 * gsc);
            }
            const float S = bs * aS;
            ui.Shape(cx - 70 * S, cy - 60 * S, cx + 70 * S, cy + 80 * S, -70, -60, 70, 80, Rgba{ 1, 1, 1, btnOp }, 13);
            ui.Shape(cx - 29 * S, cy - 29 * S, cx + 29 * S, cy + 29 * S, -29, -29, 29, 29, Rgba{ 1, 1, 1, btnOp }, 12);
            ui.Shape(cx - 50 * bs, cy - 50 * bs, cx + 50 * bs, cy + 50 * bs, 0, 0, 100, 100, Rgba{ 1, 1, 1, btnOp }, 10,
                     m_segOn[0], m_segOn[1], m_segOn[2]);
            // « A »: Outfit 500 20 px #e4e9ff, centred in the circle
            const char L = m_letter ? m_letter : 'A';
            const float aw = ui.Advance(fonts::kButton, (uint32_t)L);
            ui.Glyph(fonts::kButton, (uint32_t)L, cx - aw * 0.5f * S, cy - 10 * S, Hex(0xe4e9ff, btnOp), 0, 0, 0, 0);
        }

        // hint: Outfit 300 15 px, letter-spacing 3 px, #7d87ad, .6 when shown. v87: « Appuie sur un bouton » (the prompt, like the
        // PS4's « press the PS button »), « Appuie 3 fois... » (the presses)
        if (m_hintOp > 0.003f) {
            const float br = m_step == kPrompt ? 0.75f + 0.25f * sinf((now - m_awakeAt) * 1.8f) : 1.0f;   // (it breathes)
            const char *h = m_step == kPrompt ? "Appuie sur un bouton" : "Appuie 3 fois sur un bouton, ou touche l'\xc3\xa9" "cran";
            ui.TextCentered(fonts::kHint, h, 640, 664, 3, Hex(0x7d87ad, m_hintOp * lkOp * br * bk));
        }
        DrawWho(ui, now, Clamp01(m_k[kWho]) * lkOp);
        DrawPin(ui, now, Clamp01(m_k[kPin]) * lkOp);
        ui.SetZoom(1.0f);
    }

} // namespace brume
