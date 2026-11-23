// Brume - v87: the battery and the charger. See Charge.hpp.
#include "Charge.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include <switch.h>
#include <cmath>
#include <cstdio>

extern "C" int qext_game_running(void);
#ifdef BRUME_UISIM
extern "C" { extern int g_simBat, g_simCharge; }       // Theme.cpp (uisim: « demo batterie / charge »)
#endif

namespace brume::charge {

    namespace {
        enum Kind { kPlug = 1, kUnplug = 2, kFull = 3, kLow = 4 };
        bool  s_known = false, s_charging = false, s_full = false, s_low20 = false, s_low10 = false, s_locked = false;
        int   s_pct = 100, s_kind = 0, s_type = 0;
        float s_readAt = -100, s_at = -100, s_pctAt = -100;
        float s_barW = 0, s_barX0 = 0, s_barX1 = 0, s_barCy = 0;   // v96: the top bar's battery (where it was drawn)
        float s_shown = -1, s_shownV = 0;                // its fill, glided (a change of level slides, never jumps)
        char  s_text[64] = "";
        constexpr float kDur = 2.6f;
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        Rgba Col(int kind) {
            if (kind == kPlug) return ps2::Mix(ps2::kGreen, ps2::kHalo, 0.35f);
            if (kind == kFull) return ps2::kGreen;
            if (kind == kLow) return Rgba{ 1.0f, 0.62f, 0.22f, 1 };
            return ps2::kHalo;
        }
        // a few notes of the chord sounding now: in (rising), out (falling), full (high and airy), low (a soft low one)
        void Sound(int kind) {
            if (!notify::s_sound) return;                // (Réglages > Son > Son des notifications)
            if (kind == kPlug) for (int k = 0; k < 4; k++) audio::Bell(audio::HarmonyNote(k, 62 + 4 * k, 74 + 4 * k), 0.032f - 0.004f * k, true, 0.07f * k);
            else if (kind == kUnplug) { audio::Bell(audio::HarmonyNote(3, 74, 86), 0.026f, true); audio::Bell(audio::HarmonyNote(1, 62, 74), 0.024f, true, 0.11f); }
            else if (kind == kFull) for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k + 1, 79, 93), 0.02f, true, 0.09f * k);
            else { audio::SoftLow(audio::HarmonyNote(0, 38, 50), 0.05f, 0.9f); audio::Bell(audio::HarmonyNote(0, 62, 74), 0.02f, true, 0.05f); }
        }
        void Event(float now, int kind) {
            s_kind = kind; s_at = now;
            if (kind == kPlug && s_type == 2) snprintf(s_text, sizeof s_text, lang::T("Charge lente (USB)  \xc2\xb7  %d %%"), s_pct);   // v96: the charger
            else if (kind == kPlug && s_type == 3) snprintf(s_text, sizeof s_text, lang::T("Chargeur non reconnu  \xc2\xb7  %d %%"), s_pct);
            else if (kind == kPlug) snprintf(s_text, sizeof s_text, lang::T("En charge  \xc2\xb7  %d %%"), s_pct);
            else if (kind == kUnplug) snprintf(s_text, sizeof s_text, lang::T("Sur batterie  \xc2\xb7  %d %%"), s_pct);
            else if (kind == kFull) snprintf(s_text, sizeof s_text, "%s", lang::T("Batterie charg\xc3\xa9" "e"));
            else snprintf(s_text, sizeof s_text, lang::T("Batterie faible  \xc2\xb7  %d %%"), s_pct);
            Sound(kind);
            if (kind == kPlug || kind == kFull) haptic::Tick(0.35f); else if (kind == kUnplug) haptic::Tick(0.25f); else haptic::Thump(0.3f);
            // the notification (on the lock screen the light says it, with its words: no stale note after the unlock)
            if (!s_locked) notify::Post(now, s_text, kind == kLow ? notify::kError : (kind == kUnplug ? notify::kInfo : notify::kDone), false);
        }
    }

    void Update(float now, bool psmOk, bool locked) {
        s_locked = locked;
        if (now - s_readAt < 0.5f && now >= s_readAt) return;
        s_readAt = now;
        bool known = false, ch = s_charging;
        int pct = s_pct;
#ifdef BRUME_UISIM
        if (g_simBat >= 0) { known = true; pct = g_simBat; ch = g_simCharge > 0; s_type = g_simCharge > 0 ? (g_simCharge > 3 ? 1 : g_simCharge) : 0; }
        else
#endif
        if (psmOk && !qext_game_running()) {
            // v96: the charger twice a second (one small call), the level every 3 s - or at once when the charger changes
            PsmChargerType t = PsmChargerType_Unconnected;
            if (R_SUCCEEDED(psmGetChargerType(&t))) {
                const int type = (int)t >= 0 && (int)t <= 3 ? (int)t : 3;
                const bool plugChanged = !s_known || (type != 0) != s_charging || type != s_type;
                s_type = type; ch = type != 0;
                u32 p = (u32)s_pct;
                if (plugChanged || now - s_pctAt >= 3.0f || now < s_pctAt) {
                    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&p))) { s_pctAt = now; known = true; pct = (int)(p > 100 ? 100 : p); }
                } else { known = s_known; pct = s_pct; }
            }
        }
        if (!known) return;
        const bool first = !s_known;
        s_pct = pct;
        if (first) { s_known = true; s_charging = ch; s_full = ch && pct >= 100; s_low20 = pct <= 20; s_low10 = pct <= 10; return; }   // (the state at start: nothing said)
        if (ch != s_charging) {
            s_charging = ch;
            Event(now, ch ? kPlug : kUnplug);
            if (ch) { s_low20 = s_low10 = false; s_full = pct >= 100; }
            return;
        }
        if (ch && !s_full && pct >= 100) { s_full = true; Event(now, kFull); }
        if (!ch && pct > 25) s_low20 = s_low10 = false;
        if (!ch && pct <= 10 && !s_low10) { s_low10 = s_low20 = true; Event(now, kLow); }
        else if (!ch && pct <= 20 && !s_low20) { s_low20 = true; Event(now, kLow); }
    }

    // ---- v96: the top bar's battery ----
    float DrawBar(Ui &ui, float now, float right, float cy, float a) {
        static float s_last = -1;
        const float dt = s_last < 0 ? 0.0f : fminf(0.1f, fmaxf(0.0f, now - s_last));
        s_last = now;
        if (!s_known || a <= 0.003f) { s_barW = 0; return 0; }
        const int pct = s_pct;
        const bool ch = s_charging, full = ch && pct >= 100, low = !ch && pct <= 15;
        if (s_shown < 0) s_shown = pct / 100.0f;
        Glide(s_shown, s_shownV, pct / 100.0f, dt, 5.0f);
        // between the tabs and the date (the bar has little room): a small upright gauge of five cells of light - lit from the
        // bottom up to the level, the last one partly - and the level written beside it, on the date's baseline
        char t[24];
        if (full) snprintf(t, sizeof t, "%s", lang::T("Charg\xc3\xa9" "e"));
        else snprintf(t, sizeof t, "%d %%", pct);
        constexpr float kCw = 11.0f, kCh = 3.4f, kGap = 1.7f, kR = 1.5f, gap = 20, kTx = 9;
        const float ch5 = 5 * kCh + 4 * kGap, tw = ui.Width(fonts::kHintBig, t, 0.5f);
        const float tx1 = right - gap, tx0 = tx1 - tw, gx1 = tx0 - kTx, gx0 = gx1 - kCw;
        const float top = cy + 7.0f - ui.Baseline(fonts::kHintBig, 0) - 1.0f;   // (cy 52: the date's baseline is at 60)
        const float gcy = cy + 1.0f, gy1 = gcy + ch5 * 0.5f, gy0 = gy1 - ch5;     // the gauge: its bottom, its top
        const Rgba base = low ? ps2::kPink : (ch ? ps2::Mix(ps2::kGreen, ps2::kChoice, full ? 0.15f : 0.4f) : ps2::kChoice);
        const Rgba words = low ? ps2::Mix(ps2::kPink, ps2::kChosen, 0.25f) : (full ? ps2::Mix(ps2::kGreen, ps2::kChosen, 0.35f) : (ch ? ps2::Mix(Hex(0xaab3d4), ps2::kGreen, 0.45f) : Hex(0xaab3d4)));
        const float breathe = full ? 0.80f + 0.20f * sinf(now * 1.3f) : 1.0f;
        if (low) {                                       // low: a soft pink halo breathes behind it (no flashing)
            const float br = 0.5f + 0.5f * sinf(now * 2.2f);
            ui.Light(gx0 - 22, cy - 24, tx1 + 16, cy + 24, ps2::A(ps2::kPink, (0.10f + 0.16f * br) * a), true);
        }
        ui.TextCentered(fonts::kHintBig, t, (tx0 + tx1) * 0.5f, top, 0.5f, ps2::A(words, 0.95f * a));
        // charging: a light rises through the cells up to the last lit one, again and again (1.8 s)
        float flowY = 1e9f, flowK = 0;
        if (ch && !full) {
            const float p = fmodf(now, 1.8f) / 1.8f, e = p < 0.5f ? 2 * p * p : 1 - 2 * (1 - p) * (1 - p);
            const float end = gy1 - ch5 * fmaxf(0.1f, s_shown);
            flowY = gy1 + 5 - (gy1 + 5 - end) * e;
            flowK = sinf(3.14159f * fminf(1.0f, p * 1.15f));
            ui.Light(gx0 - 9, flowY - 9, gx1 + 9, flowY + 9, ps2::A(ps2::Mix(base, ps2::kChosen, 0.4f), 0.55f * flowK * a), true);
        }
        const float fillTop = gy1 - ch5 * s_shown;
        if (s_shown > 0.01f) ui.Light(gx0 - 10, fillTop - 8, gx1 + 10, gy1 + 8, ps2::A(base, 0.26f * a * breathe), true);   // the lit part's glow
        for (int i = 0; i < 5; i++) {                    // (cell 0 at the bottom)
            const float y1 = gy1 - i * (kCh + kGap), y0 = y1 - kCh, f = fminf(1.0f, fmaxf(0.0f, s_shown * 5 - i));
            ui.Shape(gx0, y0, gx1, y1, 0, 0, kCw, kCh, ps2::A(ps2::kFaint, 0.28f * a), 24, kCw, kCh, kR);   // the empty cell
            if (f > 0.02f) {
                const float my = (y0 + y1) * 0.5f, boost = flowK > 0 ? expf(-((my - flowY) * (my - flowY)) / 18.0f) * flowK : 0.0f;
                const Rgba c = ps2::Mix(ps2::Mix(base, ps2::kChosen, 0.35f), ps2::kChosen, 0.6f * boost);
                const float w = fmaxf(kCh, kCw * f);           // the last one: partly (from the left)
                ui.Shape(gx0, y0, gx0 + w, y1, 0, 0, w, kCh, ps2::A(c, a * breathe * (0.6f + 0.4f * f)), 24, w, kCh, fminf(kR, w * 0.5f));
            }
        }
        (void)gy0;
        s_barX0 = gx0 - 10; s_barX1 = tx1 + 8; s_barCy = cy;
        s_barW = right - (gx0 - 4);
        return s_barW;
    }
    float BarWidth() { return s_barW; }
    bool  BarHit(float x, float y) { return s_barW > 0 && x >= s_barX0 && x <= s_barX1 && fabsf(y - s_barCy) < 26; }
    int   Charger() { return s_charging ? s_type : 0; }
    void  Tap(float now) {
        if (!s_known) return;
        char m[96];
        const char *how = !s_charging ? lang::T("sur batterie") : s_type == 2 ? lang::T("charge lente (USB)") :
                          s_type == 3 ? lang::T("chargeur non reconnu") : s_pct >= 100 ? lang::T("charg\xc3\xa9" "e") : lang::T("charge rapide");
        snprintf(m, sizeof m, lang::T("Batterie %d %%  \xc2\xb7  %s"), s_pct, how);
        notify::Post(now, m, s_charging ? notify::kDone : (s_pct <= 15 ? notify::kError : notify::kInfo), false);
        audio::Play(audio::Tick2400, 0.6f, 0.8f);
        haptic::Tick(0.4f);
    }

    bool  Known() { return s_known; }
    int   Pct() { return s_pct; }
    bool  Charging() { return s_charging; }
    float PulseK(float now) {
        const float p = (now - s_at) / kDur;
        if (p < 0 || p >= 1) return 0.0f;
        return Clamp01(p * 10.0f) * powf(1 - p, 1.3f);
    }

    void Draw(Ui &ui, float now) {
        const float p = (now - s_at) / kDur;
        if (p < 0 || p >= 1 || !s_kind) return;
        const float e = EaseGlide(p), k = Clamp01(p * 10.0f) * powf(1 - p, 1.3f);
        const Rgba c = Col(s_kind);
        // rising (the power comes in): a bloom grows up from the bottom edge, its front climbing; sinking (it goes): the
        // light that was there falls back into the edge
        const float g = s_kind == kUnplug ? 1 - e : e;
        const float rx = 260 + 720 * g, ry = 70 + 230 * g, front = 720 - 250 * g;
        ui.Light(640 - rx, 735 - ry, 640 + rx, 735 + ry, ps2::A(c, 0.50f * k), true);
        ui.Light(-80, front - 26, 1360, front + 26, ps2::A(c, 0.22f * k));
        ui.Light(640 - 120, 700, 640 + 120, 760, ps2::A(ps2::Mix(c, ps2::kChosen, 0.5f), 0.45f * k), true);   // where the cable goes in
        if (s_locked && s_text[0]) {                     // the lock screen: its words, small, above the edge
            const float t = now - s_at, ta = Clamp01(t / 0.35f) * (t < 1.8f ? 1.0f : Clamp01(1 - (t - 1.8f) / 0.8f));
            if (ta > 0.003f) ui.TextCentered(fonts::kSetDesc, s_text, 640, 674, 1, ps2::A(ps2::Mix(c, ps2::kChosen, 0.4f), 0.9f * ta));   // (mixed case: the « % » is only in this font)
        }
    }

}
