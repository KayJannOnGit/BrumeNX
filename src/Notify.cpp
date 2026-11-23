// Brume - v77: the notifications and the task centre. See Notify.hpp.
#include "Notify.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Busy.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume {

    namespace {
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        float EaseInCubic(float p) { p = Clamp01(p); return p * p * p; }
        // left-aligned text (translated), its width
        float Text(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c) {
            const float x0 = x;
            for (const char *q = s; *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0);
            return x - x0;
        }
        // the text cut to fit maxW (with « … »), translated first
        void Fit(Ui &ui, fonts::Style st, const char *src, float maxW, char *out, int cap) {
            snprintf(out, (size_t)cap, "%s", lang::T(src));
            if (ui.Width(st, out, 0) <= maxW) return;
            int n = (int)strlen(out);
            while (n > 1) {
                do n--; while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);   // (never in the middle of a letter)
                char tmp[256];
                snprintf(tmp, sizeof tmp, "%.*s\xe2\x80\xa6", n, out);
                if (ui.Width(st, tmp, 0) <= maxW || n <= 1) { snprintf(out, (size_t)cap, "%.*s", cap - 1, tmp); return; }
            }
        }
    }

    // ================================================================ notifications
    namespace notify {
        namespace {
            constexpr int kMax = 5;
            constexpr float kIn = 0.42f, kOut = 0.32f, kHold = 4.2f, kAfterClose = 3.0f;
            constexpr float kX = 36, kY = 100, kH = 54, kStep = 70;
            struct Note {
                bool on = false, sticky = false;
                int icon = 0;
                char text[160] = "";
                float in = -100, out = -1, holdUntil = 0, prog = -1, y = kY, vy = 0;
            };
            Note s_n[kMax];
            float s_last = -1;
            float s_ox = 0, s_oxV = 0, s_oxTo = 0;       // v78: the stack steps aside (right) while a panel is open on the left
            bool s_wait = false;                         // v87: the lock screen holds them
            void Chime(int icon) {
                if (!s_sound) {}                                 // v85: « Son des notifications » off (Réglages > Son)
                else if (icon == kError) audio::Bell(audio::HarmonyNote(0, 50, 62), 0.030f, true);
                else audio::Bell(audio::HarmonyNote(icon == kDone ? 2 : 0, 74, 86), 0.022f, true);
                haptic::Tick(0.3f);
            }
            int Slot(float now) {
                for (int i = 0; i < kMax; i++) if (!s_n[i].on) return i;
                int best = -1; float oldest = 1e9f;      // full: the oldest one that is not staying leaves now
                for (int i = 0; i < kMax; i++) if (!s_n[i].sticky && s_n[i].in < oldest) { oldest = s_n[i].in; best = i; }
                if (best >= 0) s_n[best].on = false;
                (void)now;
                return best;
            }
            int Start(float now, const char *text, int icon, bool sticky, bool chime = true) {
                const int i = Slot(now);
                if (i < 0) return -1;
                Note &n = s_n[i];
                n = Note();
                n.on = true; n.sticky = sticky; n.icon = icon; n.in = now; n.out = -1;
                n.holdUntil = sticky ? 1e9f : now + kHold;
                snprintf(n.text, sizeof n.text, "%s", lang::T(text));
                n.y = kY - 10;                           // (it arrives at the top; the others are pushed down, softly)
                if (chime && !s_wait) Chime(icon);
                return i;
            }
        }

        void Post(float now, const char *text, int icon, bool chime) { if (text && text[0]) Start(now, text, icon, false, chime); }
        void Wait(bool on) { s_wait = on; }
        int  Waiting() { int n = 0; for (const Note &e : s_n) n += e.on && e.out < 0; return n; }
        int  Open(float now, const char *text, int icon) { return Start(now, text ? text : "", icon, true); }
        void Set(int id, float now, const char *text, float progress) {
            if (id < 0 || id >= kMax || !s_n[id].on) return;
            (void)now;
            if (text) snprintf(s_n[id].text, sizeof s_n[id].text, "%s", lang::T(text));
            s_n[id].prog = progress;
        }
        void Close(int id, float now, const char *text, int icon) {
            if (id < 0 || id >= kMax || !s_n[id].on) return;
            Note &n = s_n[id];
            if (text) snprintf(n.text, sizeof n.text, "%s", lang::T(text));
            if (icon != n.icon) Chime(icon);
            n.icon = icon; n.sticky = false; n.prog = -1; n.holdUntil = now + kAfterClose;
        }

        void Avoid(float x) { s_oxTo = x > 0 ? x : 0; }
        void Draw(Ui &ui, float now, float alpha) {
            const float dt = s_last < 0 ? 0.016f : fminf(0.05f, fmaxf(0.0f, now - s_last));
            s_last = now;
            if (s_wait) {                                // v87: the lock screen - their time waits; at the unlock, one after the other
                int k = 0;
                for (Note &e : s_n) if (e.on && e.out < 0) { e.in = now + 0.9f + 0.25f * k++; if (!e.sticky) e.holdUntil = e.in + kHold; e.y = kY - 10; }
                return;
            }
            Glide(s_ox, s_oxV, s_oxTo, dt, 10.0f);       // v78: aside while a panel is open on the left (the profiles)
            // the order: the newest at the top (PS4), each one's place glides (the menu's spring)
            int order[kMax], n = 0;
            for (int i = 0; i < kMax; i++) {
                Note &e = s_n[i];
                if (!e.on) continue;
                if (e.out < 0 && now >= e.holdUntil) e.out = now;                  // its time is up: it slides out
                if (e.out >= 0 && now - e.out > kOut) { e.on = false; continue; }
                order[n++] = i;
            }
            for (int a = 1; a < n; a++)
                for (int b = a; b > 0 && s_n[order[b]].in > s_n[order[b - 1]].in; b--) { const int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t; }
            int rank = 0;
            for (int k = 0; k < n; k++) {
                Note &e = s_n[order[k]];
                const float target = kY + (e.out >= 0 ? rank - 0.0f : (float)rank) * kStep;
                Glide(e.y, e.vy, target, dt, 16.0f);
                if (e.out < 0) rank++;
                if (alpha <= 0.003f) continue;
                const float tw = ui.Width(fonts::kHintBig, e.text, 0.3f);
                const float w = fminf(660.0f, fmaxf(300.0f, tw + 100));
                // the motion: in from the left edge (fast, then it settles), out to the left (it gathers speed)
                if (now < e.in) continue;                // (v87: still waiting for its turn after the unlock)
                const float pin = EaseOutExpo((now - e.in) / kIn), pout = e.out >= 0 ? EaseInCubic((now - e.out) / kOut) : 0.0f;
                const float off = -(w + 80) * ((1 - pin) + pout);
                const float a = alpha * Clamp01(pin * 1.6f) * (1 - pout);
                if (a <= 0.003f) continue;
                const float x0 = kX + s_ox + off, y0 = e.y;
                // v78 (Yass: « même les notifs, on voit pas leur fond ») - its surface (night mist, rounded; Ui::Surface), lit
                // by its own colour: the ambient one, the green of « done », the pink of a problem
                const Rgba tint = e.icon == kDone ? ps2::Mix(Ui::s_tint, ps2::kGreen, 0.6f) : (e.icon == kError ? ps2::Mix(Ui::s_tint, ps2::kPink, 0.7f) : Ui::s_tint);
                ui.Surface(x0 - 6, y0 - 2, x0 + w + 6, y0 + kH + 2, a, tint, 26);   // v78.1: rounder (18 -> 26)
                // its light: a soft dot (blue, the green of « done », the pink of a problem); a download's arrow breathes
                const float cx = x0 + 30, cy = y0 + kH * 0.5f;
                const Rgba col = e.icon == kDone ? ps2::kGreen : (e.icon == kError ? ps2::kPink : ps2::kChoice);
                ui.Light(cx - 22, cy - 22, cx + 22, cy + 22, ps2::A(col, 0.55f * a), true);
                if (e.icon == kDownload) {
                    const float bob = sinf(now * 5.0f) * 2.5f;
                    ui.Arrow(cx, cy + bob, 6, 3, ps2::A(ps2::kChosen, a));
                    ui.Shape(cx - 7, cy + 9, cx + 7, cy + 10.5f, 0, 0, 14, 1.5f, ps2::A(ps2::kChosen, 0.8f * a), 24, 14, 1.5f, 0.75f);
                } else ui.Shape(cx - 4, cy - 4, cx + 4, cy + 4, 0, 0, 8, 8, ps2::A(ps2::kChosen, a), 24, 8, 8, 4);
                char fit[200];
                Fit(ui, fonts::kHintBig, e.text, w - 80, fit, sizeof fit);
                Text(ui, fonts::kHintBig, fit, x0 + 58, cy - 9.5f, ps2::A(ps2::kWords, 0.95f * a));
                if (e.prog >= 0) {                     // a fine line of light: how far
                    const float lx = x0 + 58, lw = w - 80, ly = y0 + kH - 8, f = Clamp01(e.prog);
                    ui.Shape(lx, ly, lx + lw, ly + 1.5f, 0, 0, lw, 1.5f, ps2::A(ps2::kChoice, 0.16f * a), 24, lw, 1.5f, 0.75f);
                    if (f > 0.002f) ui.Shape(lx, ly, lx + lw * f, ly + 1.5f, 0, 0, lw * f, 1.5f, ps2::A(ps2::kChoice, 0.85f * a), 24, lw * f, 1.5f, 0.75f);
                    ui.Light(lx + lw * f - 16, ly - 10, lx + lw * f + 16, ly + 11, ps2::A(ps2::kHalo, 0.6f * a), true);
                }
            }
        }
    } // namespace notify

    // ================================================================ tasks
    namespace tasks {
        namespace {
            constexpr int kMax = 6;
            constexpr float kKeepDone = 8.0f, kKeepBlocked = 25.0f;
            constexpr float kMarkY = 52;                           // in the top bar, left of the date and time
            constexpr float kPW = 440, kPX = 1244 - kPW, kPY = 94, kRowH = 96, kHead = 56;
            struct Task {
                bool on = false;
                int state = kRunning, done = 0, total = 0;
                char name[48] = "", item[120] = "", why[96] = "";
                float started = 0, alive = 0, ended = -1;
            };
            Task s_t[kMax];
            bool s_open = false;
            float s_markX = 1000;
            float s_openAt = -100, s_closeAt = -100, s_markK = 0, s_markV = 0, s_last = -1;
            float s_orbT = 0, s_spin = 1, s_spinV = 0;   // v87: the orbs' own clock (they slow down to rest when the work is done)
            bool Valid(int id) { return id >= 0 && id < kMax && s_t[id].on; }
        }

        int Begin(float now, const char *name) {
            int id = -1;
            for (int i = 0; i < kMax && id < 0; i++) if (s_t[i].on && !strcmp(s_t[i].name, name)) id = i;
            for (int i = 0; i < kMax && id < 0; i++) if (!s_t[i].on) id = i;
            if (id < 0) {                                // full: the oldest finished one gives its place
                float oldest = 1e9f;
                for (int i = 0; i < kMax; i++) if (s_t[i].ended >= 0 && s_t[i].ended < oldest) { oldest = s_t[i].ended; id = i; }
            }
            if (id < 0) return -1;
            Task &t = s_t[id];
            t = Task();
            t.on = true; t.state = kRunning; t.started = t.alive = now;
            snprintf(t.name, sizeof t.name, "%s", name);
            return id;
        }
        void Progress(int id, float now, int done, int total, const char *item) {
            if (!Valid(id)) return;
            Task &t = s_t[id];
            if (done != t.done || total != t.total) t.alive = now;
            t.done = done; t.total = total;
            if (item) snprintf(t.item, sizeof t.item, "%s", item);
            if (t.state == kWaitNet && done > 0) t.state = kRunning;
        }
        void Alive(int id, float now) { if (Valid(id)) s_t[id].alive = now; }
        void SetState(int id, float now, State s, const char *why) {
            if (!Valid(id)) return;
            s_t[id].state = s; s_t[id].alive = now;
            snprintf(s_t[id].why, sizeof s_t[id].why, "%s", why ? why : "");
        }
        void End(int id, float now, bool ok, const char *why) {
            if (!Valid(id)) return;
            Task &t = s_t[id];
            t.state = ok ? kDone : kBlocked; t.ended = now;
            snprintf(t.why, sizeof t.why, "%s", why ? why : "");
        }
        bool Any() { for (const Task &t : s_t) if (t.on) return true; return false; }
        void Update(float now) {
            for (Task &t : s_t)
                if (t.on && t.ended >= 0 && now - t.ended > (t.state == kBlocked ? kKeepBlocked : kKeepDone)) t.on = false;
            const float dt = s_last < 0 ? 0.016f : fminf(0.05f, fmaxf(0.0f, now - s_last));
            s_last = now;
            Glide(s_markK, s_markV, Any() ? 1.0f : 0.0f, dt, 8.0f);
            bool running = false;
            for (const Task &t : s_t) running = running || (t.on && t.ended < 0);
            Glide(s_spin, s_spinV, running ? 1.0f : 0.12f, dt, 3.0f);
            s_orbT += dt * s_spin;
        }

        // ---- the mark: v87 - the overlay's ring of six orbiting lights (Busy.hpp), the state's colour; done: they slow to rest
        void DrawMark(Ui &ui, float now, float alpha, float x) {
            s_markX = x;
            const float kMarkX = x;
            const float k = Clamp01(s_markK) * alpha;
            if (k <= 0.003f) return;
            bool running = false, blocked = false;
            for (const Task &t : s_t) if (t.on) { running = running || t.ended < 0; blocked = blocked || t.state == kBlocked; }
            const Rgba col = blocked ? ps2::kPink : (running ? ps2::kChoice : ps2::kGreen);
            ui.Light(kMarkX - 22, kMarkY - 22, kMarkX + 22, kMarkY + 22, ps2::A(col, 0.26f * k * (0.8f + 0.2f * sinf(now * 2.4f))), true);
            busy::Orbs(ui, kMarkX, kMarkY, 11.0f, k, s_orbT, col);
        }
        bool MarkHit(float x, float y) { return s_markK > 0.3f && fabsf(x - s_markX) < 26 && fabsf(y - kMarkY) < 26; }
        void Toggle(float now) {
            s_open = !s_open;
            if (s_open) { s_openAt = now; audio::PlaySweep(audio::Whoosh, 0.5f, 0.1f, 0.35f, 0.3f); haptic::Tick(0.5f); }
            else { s_closeAt = now; audio::Play(audio::Tick1700, 0, 0.8f); }
        }
        bool PanelOpen() { return s_open; }
        void Close(float now) { if (s_open) Toggle(now); }
        bool PanelTap(float x, float y, float now) {
            if (!s_open) return false;
            int n = 0; for (const Task &t : s_t) n += t.on;
            const float ph = kHead + (n ? n : 1) * kRowH + 16;
            if (x >= kPX - 20 && x <= kPX + kPW + 20 && y >= kPY && y <= kPY + ph) return true;   // inside: nothing more to do
            Close(now);
            return true;
        }

        void DrawPanel(Ui &ui, float now) {
            const float k = s_open ? EaseGlide((now - s_openAt) / Pace(0.42f)) : 1 - EaseGlide((now - s_closeAt) / Pace(0.30f));
            if (k <= 0.003f) return;
            int n = 0; for (const Task &t : s_t) n += t.on;
            const float ph = kHead + (n ? n : 1) * kRowH + 16, Y = kPY - (1 - k) * 16;
            // v78 - its surface (night mist, rounded; Ui::Surface), and it grows out of its mark in the top bar
            ui.SetXform(0.94f + 0.06f * k, s_markX, kMarkY, s_markX, kMarkY);
            ui.Surface(kPX - 4, Y - 4, kPX + kPW + 4, Y + ph + 2, k);
            const float tw = ui.Width(fonts::kSetSel, "T\xc3\xa2" "ches", 0);
            ui.Light(kPX + 8, Y + 4, kPX + 28 + tw + 20, Y + 50, ps2::A(ps2::kTitleGlow, 0.15f * k), true);
            Text(ui, fonts::kSetSel, lang::T("T\xc3\xa2" "ches"), kPX + 24, Y + 12, ps2::A(ps2::kTitle, k));
            if (!n) { Text(ui, fonts::kHintBig, lang::T("Aucune t\xc3\xa2" "che en cours"), kPX + 24, Y + kHead + 26, ps2::A(ps2::kFaint, k)); ui.SetZoom(1.0f); return; }
            int row = 0;
            for (const Task &t : s_t) {
                if (!t.on) continue;
                const float y = Y + kHead + row * kRowH;
                row++;
                // its name, then its state (the colour says it: blue running, faint waiting, pink stuck, green done)
                char line[200], fit[220];
                Text(ui, fonts::kSetOpt, lang::T(t.name), kPX + 24, y + 4, ps2::A(ps2::kChosen, k));
                Rgba sc = ps2::kChoice;
                if (t.state == kDone) { snprintf(line, sizeof line, "%s", lang::T("Termin\xc3\xa9")); sc = ps2::kGreen; }
                else if (t.state == kBlocked) {
                    snprintf(line, sizeof line, lang::T("Bloqu\xc3\xa9 : %s"), t.why[0] ? lang::T(t.why) : lang::T("erreur inconnue"));
                    sc = ps2::kPink;
                } else if (t.state == kWaitNet) { snprintf(line, sizeof line, "%s", lang::T("En attente du r\xc3\xa9seau")); sc = ps2::kFaint; }
                else if (t.total > 0) snprintf(line, sizeof line, lang::T("En cours   \xc2\xb7   %d / %d"), t.done, t.total);
                else snprintf(line, sizeof line, "%s", lang::T("En cours"));
                Fit(ui, fonts::kHintBig, line, kPW - 48, fit, sizeof fit);
                Text(ui, fonts::kHintBig, fit, kPX + 24, y + 36, ps2::A(sc, k));
                // what it is doing now, and when it last heard back (slow, or stuck: never a doubt)
                if (t.ended < 0) {
                    const int since = (int)(now - t.alive);
                    Rgba lc = ps2::kFaint;
                    if (since >= 60) { snprintf(line, sizeof line, lang::T("semble bloqu\xc3\xa9 : aucune r\xc3\xa9ponse depuis %d s"), since); lc = ps2::kPink; }
                    else if (since >= 15) { snprintf(line, sizeof line, lang::T("aucune r\xc3\xa9ponse depuis %d s : r\xc3\xa9seau lent ?"), since); lc = Rgba{ 1.0f, 0.74f, 0.42f, 1 }; }
                    else snprintf(line, sizeof line, lang::T("derni\xc3\xa8re r\xc3\xa9ponse il y a %d s"), since);
                    if (t.item[0]) {
                        char both[300]; snprintf(both, sizeof both, "%s   \xc2\xb7   %s", t.item, line);
                        Fit(ui, fonts::kHint, both, kPW - 48, fit, sizeof fit);
                    } else Fit(ui, fonts::kHint, line, kPW - 48, fit, sizeof fit);
                    Text(ui, fonts::kHint, fit, kPX + 24, y + 64, ps2::A(lc, 0.9f * k));
                    if (t.total > 0) {                    // a fine line of light
                        const float lx = kPX + 24, lw = kPW - 48, ly = y + kRowH - 10, f = Clamp01((float)t.done / (float)t.total);
                        ui.Shape(lx, ly, lx + lw, ly + 1.5f, 0, 0, lw, 1.5f, ps2::A(ps2::kChoice, 0.16f * k), 24, lw, 1.5f, 0.75f);
                        if (f > 0.002f) ui.Shape(lx, ly, lx + lw * f, ly + 1.5f, 0, 0, lw * f, 1.5f, ps2::A(sc, 0.85f * k), 24, lw * f, 1.5f, 0.75f);
                    }
                }
            }
            ui.SetZoom(1.0f);
        }
    } // namespace tasks

} // namespace brume
