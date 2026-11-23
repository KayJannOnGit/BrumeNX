// Brume - v78: the popups. See Popup.hpp.
#include "Popup.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume::popup {

    namespace {
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        constexpr float kArmDelay = 0.30f, kArmTime = 1.5f;   // a deletion's « Oui »: it waits, then fills for 1.5 s
        constexpr float kPressHold = 0.16f;                   // the word sinks in, then the popup goes
        constexpr int kLines = 3;
        struct State {
            bool open = false, two = false, armed = false, snap = false;
            int kind = kInfo, focus = 1, pressed = -1;
            char title[96] = "", subject[128] = "", text[400] = "", note[96] = "", yes[40] = "", no[40] = "";
            Fn onYes = nullptr, onNo = nullptr;
            void *ctx = nullptr;
            float openAt = -100, show = 0, showV = 0, arm = 0, armedAt = -100, nopeAt = -100, last = -1;
            float fx = 640, fv = 0, fw = 100, fwv = 0, moveAt = -100, pressAt = -100;
            float px0 = 0, py0 = 0, px1 = 0, py1 = 0, bx[2] = { 0, 0 }, bw[2] = { 0, 0 }, by = 0;   // the last layout (touch)
        };
        State S;

        // ---- the sounds: each one ONE note recorded at a reference pitch, played on a note of the chord sounding now
        float At(int midi, int ref) { return exp2f((midi - ref) / 12.0f); }
        void SndOpen(int kind) {
            if (kind == kError) {
                if (audio::Has(audio::PopError)) audio::Play(audio::PopError, 0, 1.0f, At(audio::HarmonyNote(0, 45, 56), 50));
                else audio::Thock(0, 0.6f, 0.2f);
                return;
            }
            if (audio::Has(audio::PopOpen)) audio::Play(audio::PopOpen, 0, 1.0f, At(audio::HarmonyNote(0, 69, 80), 74));
            else audio::PlaySweep(audio::Whoosh, -0.2f, 0.2f, 0.45f, 0.35f);
            audio::Bell(audio::HarmonyNote(kind == kDanger ? 1 : 2, 76, 88), 0.010f, true, 0.09f);   // and a second note of the chord
        }
        void SndClose() {
            if (audio::Has(audio::PopClose)) audio::Play(audio::PopClose, 0, 1.0f, At(audio::HarmonyNote(0, 57, 68), 62));
            else audio::Play(audio::Tick1700, 0, 1.0f);
        }
        void SndMove(int to) {
            if (audio::Has(audio::PopMove)) audio::Play(audio::PopMove, to ? 0.25f : -0.25f, 1.0f, At(audio::HarmonyNote(to ? 2 : 0, 69, 80), 74));
            else audio::Play(audio::Deplacement, to ? 0.3f : -0.3f, 1.0f);
        }
        void SndOk() {
            if (audio::Has(audio::PopOk)) {                   // two notes of the chord, rising
                audio::Play(audio::PopOk, -0.1f, 1.0f, At(audio::HarmonyNote(0, 69, 80), 74));
                audio::Play(audio::PopOk, 0.1f, 0.6f, At(audio::HarmonyNote(2, 74, 85), 74), 0.07f);
            } else { audio::Play(audio::Tick2400, 0, 1.0f); audio::Bell(audio::HarmonyNote(0, 74, 86), 0.02f, true); }
        }
        void SndArmed() {
            if (audio::Has(audio::PopArmed)) audio::Play(audio::PopArmed, 0.2f, 1.0f, At(audio::HarmonyNote(0, 57, 68), 62));
            else audio::Bell(audio::HarmonyNote(0, 62, 74), 0.022f, true);
            audio::Bell(audio::HarmonyNote(3, 81, 93), 0.008f, true, 0.05f);   // a glint, high and quiet
        }
        void SndThud() {
            if (audio::Has(audio::PopThud)) audio::Play(audio::PopThud, 0, 1.0f, At(audio::HarmonyNote(0, 33, 44), 38));
            else { audio::Thock(0, 0.55f, 0.30f); audio::SoftLow(audio::HarmonyNote(0, 38, 49), 0.12f, 0.9f); }
        }

        // the message cut into lines that fit maxW (words kept whole); returns how many
        int Wrap(Ui &ui, fonts::Style st, const char *s, float maxW, char (*out)[200], int maxLines) {
            int n = 0;
            out[0][0] = 0;
            bool cut = false;
            while (*s) {
                if (*s == '\n') { s++; if (n + 1 >= maxLines) { cut = true; break; } out[++n][0] = 0; continue; }
                if (*s == ' ') { s++; continue; }
                const char *w = s;
                while (*s && *s != ' ' && *s != '\n') s++;
                char cand[200];
                const int len = (int)strlen(out[n]);
                snprintf(cand, sizeof cand, "%s%s%.*s", out[n], len ? " " : "", (int)(s - w), w);
                if (len && ui.Width(st, cand, 0) > maxW) {
                    if (n + 1 >= maxLines) { cut = true; break; }
                    snprintf(out[++n], 200, "%.*s", (int)(s - w), w);
                } else snprintf(out[n], 200, "%s", cand);
            }
            if (cut) { const size_t l = strlen(out[n]); if (l + 4 < 200) memcpy(out[n] + l, "\xe2\x80\xa6", 4); }
            while (n > 0 && !out[n][0]) n--;
            return out[0][0] ? n + 1 : 0;
        }
        // a name cut to fit (with « … »)
        void Fit(Ui &ui, fonts::Style st, const char *src, float maxW, char *out, int cap) {
            snprintf(out, (size_t)cap, "%s", src);
            int n = (int)strlen(out);
            while (n > 1 && ui.Width(st, out, 0) > maxW) {
                do n--; while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);   // (never in the middle of a letter)
                snprintf(out + n, (size_t)(cap - n), "\xe2\x80\xa6");
            }
        }

        void Finish(float now) {                   // the popup goes; then its answer (which may open another popup)
            const int p = S.pressed;
            S.open = false;
            const Fn f = p == 1 ? S.onYes : S.onNo;
            if (f) f(S.ctx, now);
        }
    }

    void Show(float now, const Spec &s) {
        S.kind = s.kind;
        snprintf(S.title, sizeof S.title, "%s", s.title ? s.title : "");
        snprintf(S.subject, sizeof S.subject, "%s", s.subject ? s.subject : "");
        snprintf(S.text, sizeof S.text, "%s", s.text ? s.text : "");
        snprintf(S.note, sizeof S.note, "%s", s.note ? s.note : "");
        snprintf(S.yes, sizeof S.yes, "%s", s.yes && s.yes[0] ? s.yes : "OK");
        snprintf(S.no, sizeof S.no, "%s", s.no ? s.no : "");
        S.two = S.no[0] != 0;
        S.focus = S.two && (s.kind == kAsk || s.kind == kDanger) ? 0 : 1;   // the safe answer first, when something is lost
        S.onYes = s.onYes; S.onNo = s.onNo; S.ctx = s.ctx;
        S.open = true; S.pressed = -1; S.openAt = now; S.moveAt = now;
        S.armed = s.kind != kDanger; S.arm = S.armed ? 1.0f : 0.0f; S.armedAt = -100; S.nopeAt = -100;
        S.snap = true;                             // (the focus' light starts on its answer)
        tasks::Close(now);                         // the task panel steps aside
        SndOpen(s.kind);
        haptic::Thump(s.kind == kError ? 0.5f : 0.35f);
    }

    void Error(float now, const char *title, const char *text, unsigned rc) {
        Spec s;
        s.kind = kError; s.title = title; s.text = text; s.yes = "OK";
        char note[96];
        if (rc) {   // like the Switch: 2000 + the module, then the description
            snprintf(note, sizeof note, lang::T("Code d'erreur : %04u-%04u (0x%X)"), 2000u + (rc & 0x1FF), (rc >> 9) & 0x1FFF, rc);
            s.note = note;
        }
        Show(now, s);
    }

    bool Open() { return S.open; }
    float Shown() { return Clamp01(S.show); }

    void Move(int dir, float now) {
        if (!S.open || S.pressed >= 0 || !dir) return;
        const int to = dir > 0 ? 1 : 0;
        if (!S.two || to == S.focus) { audio::Thock(dir * 0.4f, 0.8f, 0.04f); return; }
        S.focus = to; S.moveAt = now;
        SndMove(to);
        haptic::Tick(0.5f);
    }

    void PressA(float now) {
        if (!S.open || S.pressed >= 0 || now - S.openAt < 0.12f) return;   // (a double press never answers by accident)
        if (S.focus == 1 && !S.armed) {            // a deletion that is not ready: « not yet » (it shakes, a dull sound)
            S.nopeAt = now;
            audio::Thock(0, 0.7f, 0.06f);
            haptic::Tick(0.4f);
            return;
        }
        S.pressed = S.focus; S.pressAt = now;
        if (S.pressed == 1) {
            if (S.kind == kDanger) { SndThud(); haptic::Thump(0.8f); }
            else { SndOk(); haptic::Tap(0.7f); }
        } else { SndClose(); haptic::Tick(0.5f); }
    }

    void PressB(float now) {
        if (!S.open || S.pressed >= 0) return;
        S.pressed = S.two ? 0 : 1; S.pressAt = now;   // (a single answer: B = OK)
        if (S.focus != S.pressed) { S.focus = S.pressed; S.moveAt = now; }
        SndClose();
        haptic::Tick(0.5f);
    }

    bool Tap(float x, float y, float now) {
        if (!S.open) return S.show > 0.05f;        // leaving: the touch is still its own (nothing under it reacts)
        if (S.pressed >= 0) return true;
        for (int i = S.two ? 0 : 1; i < 2; i++)
            if (fabsf(x - S.bx[i]) < fmaxf(80.0f, S.bw[i] * 0.5f + 36.0f) && fabsf(y - (S.by + 13.0f)) < 34.0f) {
                if (S.focus != i) { S.focus = i; S.moveAt = now; SndMove(i); haptic::Tick(0.5f); }
                PressA(now);
                return true;
            }
        if ((x < S.px0 || x > S.px1 || y < S.py0 || y > S.py1) && now - S.openAt > 0.35f) PressB(now);   // outside it: B
        return true;
    }

    void CloseNow() { S.open = false; S.pressed = -1; S.show = 0; S.showV = 0; }

    void Draw(Ui &ui, float now) {
        const float dt = S.last < 0 ? 0.016f : fminf(0.05f, fmaxf(0.0f, now - S.last));
        S.last = now;
        if (S.open && S.pressed >= 0 && now - S.pressAt >= kPressHold) Finish(now);
        Glide(S.show, S.showV, S.open ? 1.0f : 0.0f, dt, S.open ? 8.0f : 11.0f);   // in: the menu's spring; out: a little quicker
        if (!S.open && S.show < 0.004f) { S.show = 0; S.showV = 0; return; }
        const float k = Clamp01(S.show), age = now - S.openAt;
        // a deletion's « Oui »: it waits, fills for 1.5 s, then arms (red, its sound, a glint)
        if (S.open && !S.armed) {
            S.arm = Clamp01((now - S.openAt - kArmDelay) / kArmTime);
            if (S.arm >= 1.0f) { S.armed = true; S.armedAt = now; SndArmed(); haptic::Tap(0.55f); }
        }
        const bool danger = S.kind == kDanger;
        const float armK = S.kind != kDanger ? 1.0f : (S.armed ? EaseGlide((now - S.armedAt) / Pace(0.4f)) : 0.0f);

        // ---- the layout (menu px, before the popup's scale)
        char lines[kLines][200];
        const int nl = Wrap(ui, fonts::kHintBig, lang::T(S.text), 620, lines, kLines);
        float wide = ui.Width(fonts::kSetSel, S.title, 0);
        for (int i = 0; i < nl; i++) wide = fmaxf(wide, ui.Width(fonts::kHintBig, lines[i], 0.2f));
        char subj[160] = "";
        if (S.subject[0]) { Fit(ui, fonts::kHintBig, S.subject, 620, subj, sizeof subj); wide = fmaxf(wide, ui.Width(fonts::kHintBig, subj, 0.3f)); }
        const char *lab[2] = { S.no, S.yes };
        float ow[2] = { S.two ? ui.Width(fonts::kSetOpt, S.no, 0) : 0.0f, ui.Width(fonts::kSetOpt, S.yes, 0) };
        const float gap = fmaxf(118.0f, fmaxf(ow[0], ow[1]) * 0.5f + 64.0f);
        wide = fmaxf(wide, S.two ? 2 * gap + fmaxf(ow[0], ow[1]) : ow[1]);
        const float W = fminf(760.0f, fmaxf(520.0f, wide + 120.0f));
        const float H = 34 + 34 + (subj[0] ? 34 : 0) + 12 + nl * 27 + (S.note[0] ? 28 : 0) + 30 + 26 + 36;
        const float x0 = 640 - W * 0.5f, y0 = 352 - H * 0.5f;
        float y = y0 + 34;
        const float yT = y; y += 34;
        const float yS = y; if (subj[0]) y += 34;
        y += 12;
        const float yM = y; y += nl * 27;
        const float yN = y; if (S.note[0]) y += 28;
        y += 30;
        const float yB = y, cy = yB + 13;
        S.px0 = x0; S.py0 = y0; S.px1 = x0 + W; S.py1 = y0 + H; S.by = yB;
        S.bx[0] = 640 - gap; S.bx[1] = S.two ? 640 + gap : 640; S.bw[0] = ow[0]; S.bw[1] = ow[1];
        if (S.snap) { S.snap = false; S.fx = S.bx[S.focus]; S.fv = 0; S.fw = ow[S.focus]; S.fwv = 0; }
        Glide(S.fx, S.fv, S.bx[S.focus], dt, 13.0f);            // the focus' light glides to its answer
        Glide(S.fw, S.fwv, ow[S.focus], dt, 13.0f);

        // ---- 1. behind it: the scene melts into the blurred fog, darker (the v33 backdrop, ui_fsh mode 25)
        ui.SetZoom(1.0f);
        ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0.60f, 0.66f, 0.84f, 0.97f * k }, 25);
        // ---- 2. the popup grows from the mist (and sinks back into it), with the menu's spring
        const float s = 0.92f + 0.08f * k, oy = 360 + 14 * (1 - k);
        ui.SetXform(s, 640, 360, 640, oy);
        const Rgba tint = danger ? ps2::Mix(Ui::s_tint, ps2::kRed, 0.35f + 0.65f * armK) : S.kind == kError ? ps2::kPink : Ui::s_tint;
        ui.Surface(x0, y0, x0 + W, y0 + H, k, tint, 44);   // v78.1: rounder (28 -> 44)
        // the words appear in turn (they do not move: they come with the light)
        const float cT = k * EaseGlide((age - 0.03f) / Pace(0.32f)), cM = k * EaseGlide((age - 0.09f) / Pace(0.36f)),
                    cB = k * EaseGlide((age - 0.15f) / Pace(0.40f));
        {
            const float tw = ui.Width(fonts::kSetSel, S.title, 0);
            ui.Light(640 - tw * 0.5f - 60, yT - 8, 640 + tw * 0.5f + 60, yT + 40, ps2::A(ps2::kTitleGlow, 0.14f * cT), true);
            ui.TextCentered(fonts::kSetSel, S.title, 640, yT, 0, ps2::A(ps2::kTitle, cT));
        }
        if (subj[0]) ui.TextCentered(fonts::kHintBig, subj, 640, yS + 2, 0.3f, ps2::A(ps2::kChosen, cM));
        for (int i = 0; i < nl; i++) ui.TextCentered(fonts::kHintBig, lines[i], 640, yM + i * 27, 0.2f, ps2::A(ps2::kWords, 0.92f * cM));
        if (S.note[0]) ui.TextCentered(fonts::kHint, S.note, 640, yN + 6, 0.4f, ps2::A(ps2::kFaint, 0.9f * cM));

        // ---- 3. the answers: the focus' light behind the chosen one (it glides), the chosen word near-white
        {
            const bool onDanger = S.focus == 1 && danger;
            const Rgba hc = onDanger ? ps2::Mix(ps2::kFaint, ps2::kRed, armK) : ps2::kHalo;
            const float hk = cB * (onDanger ? 0.45f + 0.55f * armK : 1.0f);
            ui.Light(S.fx - S.fw * 0.5f - 72, cy - 17, S.fx + S.fw * 0.5f + 72, cy + 17, ps2::A(hc, 0.20f * hk));
            ui.Light(S.fx - S.fw * 0.5f - 46, cy - 28, S.fx + S.fw * 0.5f + 46, cy + 28, ps2::A(hc, 0.40f * hk), true);
            const float pop = 1 - EaseGlide((now - S.moveAt) / Pace(0.35f));   // the new choice brightens, then settles
            if (pop > 0.004f) {
                const float bx = S.bx[S.focus], bw = ow[S.focus];
                ui.Light(bx - bw * 0.5f - 30, cy - 20, bx + bw * 0.5f + 30, cy + 20, ps2::A(onDanger ? hc : ps2::kChosen, 0.22f * pop * hk), true);
            }
        }
        for (int i = S.two ? 0 : 1; i < 2; i++) {
            const bool on = S.focus == i;
            Rgba col;
            if (i == 1 && danger) col = S.armed ? ps2::Mix(ps2::A(ps2::kFaint, 0.55f), on ? ps2::Mix(ps2::kRed, ps2::kChosen, 0.30f) : ps2::kRed, armK)
                                                : ps2::A(ps2::kFaint, 0.55f);   // (not ready: faint, it cannot be chosen yet)
            else col = on ? ps2::kChosen : ps2::A(ps2::kChoice, 0.78f);
            // pressed: the word sinks in (a little smaller) and lights up; « not yet »: it shakes
            float p = 1, dx = 0;
            if (S.pressed == i) p = 1 - 0.09f * sinf(3.14159265f * Clamp01((now - S.pressAt) / 0.24f));
            if (i == 1 && now - S.nopeAt < 0.5f) { const float t = now - S.nopeAt; dx = sinf(t * 44.0f) * 7.0f * expf(-t * 7.0f); }
            const float bx = S.bx[i] + dx;
            ui.SetXform(s * p, bx, cy, 640 + s * (bx - 640), oy + s * (cy - 360));
            ui.TextCentered(fonts::kSetOpt, lab[i], bx, yB, 0, ps2::A(col, cB));
            ui.SetXform(s, 640, 360, 640, oy);
            if (S.pressed == i) {
                const float fl = 1 - Clamp01((now - S.pressAt) / 0.45f);
                const Rgba fc = i == 1 && danger ? ps2::kRed : ps2::kChosen;
                ui.Light(bx - ow[i] * 0.5f - 40, cy - 30, bx + ow[i] * 0.5f + 40, cy + 30, ps2::A(fc, 0.50f * fl * fl * k), true);
            }
        }
        if (danger) {                              // the line of light under « Oui » fills from the centre, then turns red
            const float lw = ow[1] + 44, lx = S.bx[1], ly = yB + 34, f = S.arm * S.arm * (3 - 2 * S.arm), half = lw * 0.5f * f;
            ui.Shape(lx - lw * 0.5f, ly, lx + lw * 0.5f, ly + 2, 0, 0, lw, 2, ps2::A(ps2::kChoice, 0.14f * cB), 24, lw, 2, 1);
            if (half > 0.5f) {
                const Rgba fc = ps2::Mix(ps2::kChoice, ps2::kRed, f * f);
                ui.Shape(lx - half, ly, lx + half, ly + 2, 0, 0, 2 * half, 2, ps2::A(fc, (0.75f + 0.2f * armK) * cB), 24, 2 * half, 2, 1);
                if (!S.armed)
                    for (int sgn = -1; sgn <= 1; sgn += 2)
                        ui.Light(lx + sgn * half - 14, ly - 11, lx + sgn * half + 14, ly + 13, ps2::A(ps2::Mix(ps2::kHalo, ps2::kRed, f), 0.65f * cB), true);
            }
            if (S.armed) {                         // it arms: a bloom of red light around the word, then a soft glow stays
                const float b = 1 - Clamp01((now - S.armedAt) / 0.8f);
                ui.Light(lx - ow[1] * 0.5f - 80, cy - 46, lx + ow[1] * 0.5f + 80, cy + 50, ps2::A(ps2::kRed, (0.55f * b * b + 0.10f) * cB), true);
            }
        }
        ui.SetZoom(1.0f);

        // ---- 4. the hints, bottom left as everywhere (the button's letter in colour, then its word)
        {
            float hx = 46;
            const float hc = 720 - 28 - 13.5f, a = cB;
            auto Hint = [&](char b, const char *label) {
                const Rgba bc = b == 'B' ? ps2::kPink : ps2::kChoice;
                hx += ui.Glyph(fonts::kButton, (uint32_t)b, hx, hc - 10, ps2::A(bc, a), 0, 0, 0, 0) + 10;
                for (const char *q = lang::T(label); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * a), 0, 0, 0, 0) + 0.5f;
                hx += 30;
            };
            Hint('A', S.two ? "Choisir" : "OK");
            if (S.two) Hint('B', S.no);
        }
    }

} // namespace brume::popup
