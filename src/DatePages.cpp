// Brume - v83: Réglages > Système > Date et heure, Brume's own pages. See DatePages.hpp.
#include "DatePages.hpp"
#include "Audio.hpp"
#include "Clock.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include "Popup.hpp"
#include "Settings.hpp"
#include <switch.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace brume::datepages {

    namespace {
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        enum Page { kNone = 0, kPicker = 1, kZones = 2 };
        int   s_page = kNone, s_drawn = kNone;    // the page that takes the input / the one drawn (it stays while it fades out)
        float s_now = 0, s_show = 0, s_showV = 0, s_openAt = -100;
        float s_flash = 0;                        // A: the band of light flares, then fades
        float s_pop = 0, s_popV = 0;              // A: the chosen values swell a little and come back (a spring)
        float s_leaveAt = -1;                     // A taken: the page leaves then (the light first)
        Done  s_onSet = nullptr;
        bool  s_xmb = false; float s_cx = 860;    // the last layout (the finger)
        int   s_heldDir = 0; float s_heldSince = 0, s_nextRep = 0;
        float s_tickAt = -100;

        // ---- the sounds: every one on a note of the chord sounding now ----
        float At(int midi, int ref) { return exp2f((midi - ref) / 12.0f); }
        void Tick(int k, float pan) {             // a notch: a short tick (note k of the chord) and the lightest HD click
            if (s_now - s_tickAt < 0.032f) return;   // (a fast turn: one every 32 ms at most, never a buzz)
            s_tickAt = s_now;
            const int note = audio::HarmonyNote(k & 3, 76, 88);
            if (audio::Has(audio::PopMove)) audio::Play(audio::PopMove, pan, 0.42f, At(note, 74));
            else audio::Play(audio::Deplacement, pan, 0.55f);
            haptic::Tick(0.35f);
        }
        void SndFocus(float pan) { audio::Play(audio::Deplacement, pan, 0.8f); haptic::Tick(0.5f); }
        void SndEnd(float pan) { audio::Thock(pan, 0.8f, 0.05f); haptic::Tick(0.3f); }
        void SndOpen() {
            audio::PlaySweep(audio::Whoosh, 0.35f, -0.15f, 0.42f, 0.45f);
            audio::Bell(audio::HarmonyNote(2, 76, 88), 0.012f, true, 0.07f);
        }
        void SndBack() { audio::Play(audio::Tick1700, 0, 1.0f); audio::PlaySweep(audio::Whoosh, -0.15f, 0.35f, 0.38f, 0.32f); }
        void SndOk() {                            // two notes of the chord, rising (the popups' OK)
            if (audio::Has(audio::PopOk)) {
                audio::Play(audio::PopOk, -0.1f, 1.0f, At(audio::HarmonyNote(0, 69, 80), 74));
                audio::Play(audio::PopOk, 0.1f, 0.6f, At(audio::HarmonyNote(2, 74, 85), 74), 0.07f);
            } else { audio::Play(audio::Tick2400, 0, 1.0f); audio::Bell(audio::HarmonyNote(0, 74, 86), 0.02f, true); }
            audio::Bell(audio::HarmonyNote(1, 81, 93), 0.008f, true, 0.14f);   // and a glint, high and quiet
            haptic::Tap(0.8f);
        }
        void Title(Ui &ui, const char *t, float cx, float a) {   // yellow, in its warm glow (the settings' names)
            const float tw = ui.Width(fonts::kSetSel, t, 1.0f);
            ui.Light(cx - tw / 2 - 60, 98, cx + tw / 2 + 60, 152, ps2::A(ps2::kTitleGlow, 0.12f * a), true);
            ui.TextCentered(fonts::kSetSel, t, cx, 110, 1.0f, ps2::A(ps2::kTitle, a));
        }
        void TitleLeft(Ui &ui, const char *t, float x, float a) {   // the same, starting at x (a list's page)
            const float tw = ui.Width(fonts::kSetSel, t, 1.0f);
            ui.Light(x - 60, 98, x + tw + 60, 152, ps2::A(ps2::kTitleGlow, 0.12f * a), true);
            ui.TextCentered(fonts::kSetSel, t, x + tw / 2, 110, 1.0f, ps2::A(ps2::kTitle, a));
        }
        void Leave(float now, bool quiet) {
            if (s_page == kNone) return;
            s_page = kNone; s_leaveAt = -1; s_heldDir = 0;
            if (!quiet) SndBack();
            (void)now;
        }
        long long DaysFromCivil(int y, int m, int d) {   // days since 1970-01-01 (Howard Hinnant's algorithm)
            y -= m <= 2;
            const long long era = (y >= 0 ? y : y - 399) / 400;
            const unsigned yoe = (unsigned)(y - era * 400);
            const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
            const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + (long long)doe - 719468;
        }

        // ======== the picker: five wheels ========
        constexpr int kCols = 5, kYearLo = 2000, kYearHi = 2099;
        enum { cDay, cMonth, cYear, cHour, cMin };
        constexpr float kStep = 0.42f, kRowH = 68.0f, kR = kRowH / kStep;   // the drum: a notch = 0.42 rad (68 px in the middle)
        constexpr float kWheelY = 396;                                      // the middle line
        const char *const kLabel[kCols] = { "Jour", "Mois", "Ann\xc3\xa9" "e", "Heure", "Minute" };
        const char *const kMonFr[12] = { "Janvier", "F\xc3\xa9vrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Ao\xc3\xbbt", "Septembre", "Octobre", "Novembre", "D\xc3\xa9" "cembre" };   // lang:fr (French only, the English is beside it)
        const char *const kMonEn[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const char *const kDayFr[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };   // lang:fr (French only, the English is beside it)
        const char *const kDayEn[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        // one wheel: t = its notch (unbounded on the wheels that turn round: the value is t folded into lo..hi), v = the value,
        // pos = where it is drawn (it glides to t), a = how much it is there (they come one after the other)
        struct Whl { int t = 0, v = 0, notch = 0; float pos = 0, vel = 0, a = 0, av = 0, kick = 0, kickV = 0; bool quiet = true; };
        Whl   s_w[kCols];
        int   s_focus = cHour, s_dragCol = -1, s_dragFrom = 0;
        float s_focusX = cHour, s_focusV = 0;
        bool  s_dirty = false;
        struct Lay { float x[kCols], w[kCols], total; bool en, h24, ok; };
        Lay   s_lay = {};

        bool Wraps(int c) { return c != cYear; }
        int  Lo(int c) { return c == cDay || c == cMonth ? 1 : (c == cYear ? kYearLo : 0); }
        int  Hi(int c) {
            switch (c) {
                case cDay: return sysclock::DaysIn(s_w[cYear].v, s_w[cMonth].v);
                case cMonth: return 12;
                case cYear: return kYearHi;
                case cHour: return 23;
                default: return 59;
            }
        }
        int Val(int c, int t) {
            const int lo = Lo(c), hi = Hi(c), n = hi - lo + 1;
            if (!Wraps(c)) return t < lo ? lo : (t > hi ? hi : t);
            return lo + (((t - lo) % n) + n) % n;
        }
        void ValText(int c, int v, char *out, int cap) {
            switch (c) {
                case cMonth: snprintf(out, (size_t)cap, "%s", v >= 1 && v <= 12 ? (lang::En() ? kMonEn[v - 1] : kMonFr[v - 1]) : "?"); break;
                case cHour:
                    if (sysclock::s_h24) snprintf(out, (size_t)cap, "%02d", v);
                    else snprintf(out, (size_t)cap, "%d %s", v % 12 ? v % 12 : 12, v < 12 ? "AM" : "PM");
                    break;
                case cMin: snprintf(out, (size_t)cap, "%02d", v); break;
                default: snprintf(out, (size_t)cap, "%d", v); break;
            }
        }
        // the same picture, its notch brought back next to its value (a wheel turned round many times)
        void Fold(Whl &w, int c) {
            if (!Wraps(c)) return;
            const int norm = w.t - w.v;
            w.t -= norm; w.pos -= (float)norm; w.notch -= norm;
            (void)c;
        }
        // the month or the year changed: the day stays, or becomes the month's last one (the wheel turns back to it)
        void FixDay() {
            Whl &w = s_w[cDay];
            Fold(w, cDay);
            const int days = sysclock::DaysIn(s_w[cYear].v, s_w[cMonth].v);
            if (w.v > days) { w.t = days; w.v = days; w.quiet = false; }
        }
        bool Step(int c, int d) {                 // d: +1 = the next value (it comes from above)
            if (c < 0 || c >= kCols || !d) return false;
            Whl &w = s_w[c];
            if (!Wraps(c)) {
                const int nt = w.t + d;
                if (nt < Lo(c) || nt > Hi(c)) { SndEnd(s_lay.ok ? s_lay.x[c] / 900.0f : 0); w.kickV += d * 90.0f; return false; }
                w.t = nt;
            } else w.t += d;
            w.v = Val(c, w.t); w.quiet = false; s_dirty = true;
            w.kickV += d * 160.0f;
            if (c == cMonth || c == cYear) FixDay();
            if (Wraps(c) && (w.t - w.v > 400 || w.v - w.t > 400)) Fold(w, c);
            return true;
        }
        void PickerLayout(Ui &ui) {
            if (s_lay.ok && s_lay.en == lang::En() && s_lay.h24 == sysclock::s_h24) return;
            char b[32];
            float tw[kCols] = {};
            tw[cDay] = ui.Width(fonts::kPageTitle, "00", 0);
            for (int m = 1; m <= 12; m++) { ValText(cMonth, m, b, sizeof b); tw[cMonth] = fmaxf(tw[cMonth], ui.Width(fonts::kPageTitle, b, 0)); }
            tw[cYear] = ui.Width(fonts::kPageTitle, "0000", 0);
            for (int h = 0; h < 24; h++) { ValText(cHour, h, b, sizeof b); tw[cHour] = fmaxf(tw[cHour], ui.Width(fonts::kPageTitle, b, 0)); }
            tw[cMin] = ui.Width(fonts::kPageTitle, "00", 0);
            const float gap[kCols - 1] = { 14, 14, 58, 30 };
            float x = 0;
            for (int c = 0; c < kCols; c++) {
                s_lay.w[c] = tw[c] + 30;
                s_lay.x[c] = x + s_lay.w[c] * 0.5f;
                x += s_lay.w[c] + (c < kCols - 1 ? gap[c] : 0);
            }
            s_lay.total = x;
            for (int c = 0; c < kCols; c++) s_lay.x[c] -= x * 0.5f;   // (centred on 0: the page's middle is added when drawn)
            s_lay.en = lang::En(); s_lay.h24 = sysclock::s_h24; s_lay.ok = true;
        }
        float ColX(float k) {                      // a gliding wheel index -> its middle (relative to the page's middle)
            const int a = (int)floorf(k);
            if (a < 0) return s_lay.x[0];
            if (a >= kCols - 1) return s_lay.x[kCols - 1];
            return s_lay.x[a] + (s_lay.x[a + 1] - s_lay.x[a]) * (k - a);
        }
        void Preview(char *out, int cap) {        // « mardi 17 novembre 2026 · 21:42 »: what will be written
            const int y = s_w[cYear].v, m = s_w[cMonth].v, d = s_w[cDay].v, wd = sysclock::WeekDay(y, m, d);
            char hm[16];
            sysclock::HourText(hm, sizeof hm, s_w[cHour].v, s_w[cMin].v);
            if (m < 1 || m > 12) { snprintf(out, (size_t)cap, "%s", hm); return; }
            if (lang::En()) snprintf(out, (size_t)cap, "%s, %s %d, %d \xc2\xb7 %s", kDayEn[wd], kMonEn[m - 1], d, y, hm);
            else snprintf(out, (size_t)cap, "%s %d %s %d \xc2\xb7 %s", kDayFr[wd], d, kMonFr[m - 1], y, hm);
        }
        void Confirm(float now) {
            if (s_leaveAt > 0) return;
            if (!s_dirty) { SndOk(); s_flash = 1; s_popV += 7; s_leaveAt = now + Pace(0.3f); return; }   // (nothing changed: nothing written)
            const int y = s_w[cYear].v, mo = s_w[cMonth].v, d = s_w[cDay].v, h = s_w[cHour].v, mi = s_w[cMin].v;
            TimeCalendarTime cal;
            memset(&cal, 0, sizeof cal);
            cal.year = (u16)y; cal.month = (u8)mo; cal.day = (u8)d; cal.hour = (u8)h; cal.minute = (u8)mi; cal.second = 0;
            u64 list[2] = { 0, 0 }; s32 cnt = 0;
            u64 posix = 0;
            if (R_SUCCEEDED(timeToPosixTimeWithMyRule(&cal, list, 2, &cnt)) && cnt >= 1 && list[0]) posix = list[0];
            else {                                // a time the zone skips (summer time), or no rule: today's offset
                TimeCalendarTime c2; TimeCalendarAdditionalInfo inf;
                sysclock::Now(c2, inf);
                posix = (u64)(DaysFromCivil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL - inf.offset);
            }
            const sysclock::Write w = sysclock::SetUserClock(posix);
            if (sysclock::s_log) {
                char m[160];
                snprintf(m, sizeof m, "heure : %s a la main (%04d-%02d-%02d %02d:%02d) time:a=0x%x correction=0x%x horloge=0x%x ecrite=0x%x", w.good ? "reglee" : "PAS reglee", y, mo, d, h, mi, w.svc, w.autoOff, w.clock, w.set);   // lang:log (never drawn)
                sysclock::s_log(m);
            }
            if (!w.good) {
                const Result rc = R_FAILED(w.svc) ? w.svc : (R_FAILED(w.clock) ? w.clock : w.set);
                popup::Error(now, "L'heure n'a pas pu changer", "La console a refus\xc3\xa9 de r\xc3\xa9gler son horloge.", R_FAILED(rc) ? (unsigned)rc : 0u);
                return;                           // (the page stays: try again, or B)
            }
            SndOk(); s_flash = 1; s_popV += 7;
            char hm[16], m[96];
            sysclock::HourText(hm, sizeof hm, h, mi);
            snprintf(m, sizeof m, lang::T("Heure r\xc3\xa9gl\xc3\xa9" "e : %s"), hm);
            notify::Post(now, m, notify::kDone);
            if (s_onSet) s_onSet(now);
            s_leaveAt = now + Pace(0.34f);
        }
        void PickerUpdate(float now, float dt) {
            for (int c = 0; c < kCols; c++) {
                Whl &w = s_w[c];
                if (now >= s_openAt + Pace(0.06f) * c) {   // their turn: one after the other, from the left
                    Glide(w.pos, w.vel, (float)w.t, dt, 9.0f);
                    Glide(w.a, w.av, 1.0f, dt, 8.0f);
                }
                const int n = (int)lroundf(w.pos);
                if (n != w.notch) { if (!w.quiet) Tick(Val(c, n), s_lay.ok ? (s_lay.x[c]) / 900.0f : 0); w.notch = n; }
                Glide(w.kick, w.kickV, 0.0f, dt, 14.0f);
            }
            Glide(s_focusX, s_focusV, (float)s_focus, dt, 11.0f);
        }
        void PickerDraw(Ui &ui, float now, float a, float cx) {
            PickerLayout(ui);
            Title(ui, "R\xc3\xa9gler la date et l'heure", cx, a);
            char b[96], up[128];
            Preview(b, sizeof b);
            Upper(b, up, sizeof up);
            ui.TextCentered(fonts::kDate, up, cx, 152, 4, ps2::A(ps2::kWords, 0.8f * a));
            const float cy = kWheelY, L0 = cx + s_lay.x[0] - s_lay.w[0] / 2, L1 = cx + s_lay.x[kCols - 1] + s_lay.w[kCols - 1] / 2;
            // the middle line: one soft band across the wheels (it flares when A is taken)
            ui.Light(L0 - 80, cy - 38, L1 + 80, cy + 38, ps2::A(ps2::kHalo, (0.15f + 0.25f * s_flash) * a));
            // the chosen wheel: its halo glides from wheel to wheel, its width follows the value's
            const float fx = cx + ColX(s_focusX);
            float fw;
            {
                const int k0 = s_focusX <= 0 ? 0 : (s_focusX >= kCols - 1 ? kCols - 1 : (int)floorf(s_focusX)), k1 = k0 + 1 < kCols ? k0 + 1 : k0;
                const float fr = s_focusX - k0;
                ValText(k0, s_w[k0].v, b, sizeof b); const float w0 = ui.Width(fonts::kPageTitle, b, 0);
                ValText(k1, s_w[k1].v, b, sizeof b); const float w1 = ui.Width(fonts::kPageTitle, b, 0);
                fw = w0 + (w1 - w0) * fr;
            }
            ui.Light(fx - fw / 2 - 44, cy - 34, fx + fw / 2 + 44, cy + 34, ps2::A(ps2::kHalo, (0.40f + 0.2f * s_flash) * a), true);
            ui.Light(fx - fw / 2 - 14, cy - 18, fx + fw / 2 + 14, cy + 18, ps2::A({ 0.55f, 0.72f, 1.0f, 1 }, 0.2f * a), true);
            {   // its arrows: the side it turns to gives a little
                const float k = s_w[s_focus].kick * 0.03f;
                ui.Arrow(fx, cy - 33 - fmaxf(0.0f, k), 4.5f, 2, ps2::A(ps2::kChoice, 0.85f * a));
                ui.Arrow(fx, cy + 33 + fmaxf(0.0f, -k), 4.5f, 3, ps2::A(ps2::kChoice, 0.85f * a));
            }
            // the wheels' names, above
            for (int c = 0; c < kCols; c++) {
                const float f = Clamp01(1 - fabsf(c - s_focusX)), la = a * Clamp01(s_w[c].a);
                if (la <= 0.003f) continue;
                Upper(lang::T(kLabel[c]), up, sizeof up);
                ui.TextCentered(fonts::kGameInfo, up, cx + s_lay.x[c], cy - 172, 3, ps2::Mix(ps2::A(ps2::kFaint, 0.85f * la), ps2::A(ps2::kTitle, la), f));
            }
            // the wheels: a drum each, the values smaller and fainter away from the middle line
            for (int c = 0; c < kCols; c++) {
                const Whl &w = s_w[c];
                const float wa = a * Clamp01(w.a);
                if (wa <= 0.003f) continue;
                const float x = cx + s_lay.x[c], f = Clamp01(1 - fabsf(c - s_focusX));
                const float yIn = 22.0f * (1 - Clamp01(w.a)) - w.kick * 0.04f;
                const int base = (int)floorf(w.pos), lo = Lo(c), hi = Hi(c);
                for (int j = base - 3; j <= base + 4; j++) {
                    if (!Wraps(c) && (j < lo || j > hi)) continue;
                    const float off = (float)j - w.pos, th = off * kStep;
                    if (fabsf(th) > 1.3f) continue;
                    const float ct = cosf(th), k = Clamp01(1 - fabsf(off));
                    const float y = cy - sinf(th) * kR + yIn;
                    const float sc = (0.5f + 0.5f * ct) * (1 + 0.06f * s_pop * k);
                    const float al = wa * powf(ct, 2.2f) * (0.72f + 0.28f * k) * Clamp01((1.3f - fabsf(th)) / 0.4f);   // (they fade in and out at the drum's edges)
                    if (al <= 0.003f) continue;
                    ValText(c, Val(c, j), b, sizeof b);
                    ui.SetXform(sc, x, y, x, y);
                    ui.TextCentered(fonts::kPageTitle, b, x, y - 21, 0, ps2::Line(k * (0.7f + 0.3f * f), al));
                }
            }
            ui.SetXform(1, 640, 360, 640, 360);
            {   // the colon between the hour and the minute breathes, like the lock screen's
                const float x = cx + (s_lay.x[cHour] + s_lay.w[cHour] / 2 + s_lay.x[cMin] - s_lay.w[cMin] / 2) * 0.5f;
                const float ca = a * Clamp01(fminf(s_w[cHour].a, s_w[cMin].a)) * (0.55f + 0.3f * (0.5f + 0.5f * sinf(now * 2.4f)));
                ui.TextCentered(fonts::kPageTitle, ":", x, cy - 23, 0, ps2::A(ps2::kChosen, ca));
            }
        }

        // ======== the time zones: the time service's list ========
        constexpr int kMaxZones = 1024;           // (the Switch has ~600: 36 bytes each)
        constexpr float kListY = 404, kZRow = 46, kZGap = 26;
        TimeLocationName *s_names = nullptr;     // the valid ones only
        u16  *s_order = nullptr;                  // sorted: by region, the names without a region at the end
        TimeZoneRule *s_rule = nullptr;           // the chosen place's rule (16 KB, for its offset)
        int   s_nz = 0, s_sel = 0, s_curZ = -1, s_zFrom = 0, s_znotch = 0;
        float s_zpos = 0, s_zvel = 0, s_zbump = 0, s_zbumpV = 0;
        bool  s_zquiet = true;
        int   s_ruleFor = -1; float s_selAt = -100; bool s_known = false; s32 s_offset = 0; float s_infoK = 0, s_infoV = 0;
        u64   s_posix = 0;                        // the user clock now (Update)
        int   s_reg = -1, s_regOld = -1, s_regDir = 1; float s_regAt = -100, s_regNudge = 0, s_regNudgeV = 0, s_regMid = 0;

        void Release() {
            free(s_names); s_names = nullptr;
            free(s_order); s_order = nullptr;
            free(s_rule); s_rule = nullptr;
            s_nz = 0; s_sel = 0; s_curZ = -1; s_ruleFor = -1; s_known = false; s_reg = s_regOld = -1;
        }
        const char *Name(int i) { return s_nz > 0 && i >= 0 && i < s_nz ? s_names[s_order[i]].name : ""; }
        bool ValidName(const char *s, int cap) {  // a place's name: letters, digits, / _ - + only, ended inside its box
            int i = 0;
            for (; i < cap && s[i]; i++) {
                const char c = s[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) return false;
            }
            return i > 0 && i < cap && s[0] != '/' && s[i - 1] != '/';
        }
        int Cmp(const void *pa, const void *pb) {
            const char *x = s_names[*(const u16 *)pa].name, *y = s_names[*(const u16 *)pb].name;
            const bool sx = strchr(x, '/') != nullptr, sy = strchr(y, '/') != nullptr;
            if (sx != sy) return sx ? -1 : 1;
            return strcmp(x, y);
        }
        int RegionLen(int i) { const char *n = Name(i), *s = strchr(n, '/'); return s ? (int)(s - n) : 0; }
        bool SameRegion(int i, int j) {
            const int a = RegionLen(i), b = RegionLen(j);
            return a == b && (a == 0 || !strncmp(Name(i), Name(j), (size_t)a));
        }
        int RegionStart(int i) { while (i > 0 && SameRegion(i - 1, i)) i--; return i; }
        const char *RegionLabel(int i, char *buf, int cap) {
            static const char *const kReg[][2] = {
                { "Africa", "Afrique" }, { "America", "Am\xc3\xa9rique" }, { "Antarctica", "Antarctique" }, { "Arctic", "Arctique" },
                { "Asia", "Asie" }, { "Atlantic", "Atlantique" }, { "Australia", "Australie" }, { "Brazil", "Br\xc3\xa9sil" },
                { "Chile", "Chili" }, { "Etc", "D\xc3\xa9" "calages UTC" }, { "Indian", "Oc\xc3\xa9" "an Indien" }, { "Mexico", "Mexique" },
                { "Pacific", "Pacifique" }, { "US", "\xc3\x89tats-Unis" } };
            const int n = RegionLen(i);
            if (n <= 0) return "Autres";
            for (const auto &r : kReg) if ((int)strlen(r[0]) == n && !strncmp(Name(i), r[0], (size_t)n)) return r[1];
            snprintf(buf, (size_t)cap, "%.*s", n < 40 ? n : 40, Name(i));
            return buf;
        }
        void City(int i, char *out, int cap) {    // « America/Argentina/Buenos_Aires » -> « Argentina · Buenos Aires »
            const char *n = Name(i), *s = strchr(n, '/');
            const char *p = s ? s + 1 : n;
            int o = 0;
            for (; *p && o < cap - 4; p++) {
                if (*p == '/') { if (o + 4 < cap - 1) { memcpy(out + o, " \xc2\xb7 ", 4); o += 4; } }
                else out[o++] = *p == '_' ? ' ' : *p;
            }
            out[o < cap ? o : cap - 1] = 0;
        }
        void UtcText(char *out, int cap, s32 off) {
            if (!off) { snprintf(out, (size_t)cap, "UTC"); return; }
            const s32 a = off < 0 ? -off : off;
            if ((a % 3600) / 60) snprintf(out, (size_t)cap, "UTC%c%d:%02d", off < 0 ? '-' : '+', (int)(a / 3600), (int)((a % 3600) / 60));
            else snprintf(out, (size_t)cap, "UTC%c%d", off < 0 ? '-' : '+', (int)(a / 3600));
        }
        bool LoadZones() {
            Release();
            s32 total = 0;
            if (R_FAILED(timeGetTotalLocationNameCount(&total)) || total <= 0) return false;
            if (total > kMaxZones) total = kMaxZones;
            s_names = (TimeLocationName *)malloc(sizeof(TimeLocationName) * (size_t)total);
            if (!s_names) return false;
            memset(s_names, 0, sizeof(TimeLocationName) * (size_t)total);
            s32 have = 0;
            while (have < total) {                // (in pieces: never more than the box asks)
                s32 got = 0;
                const s32 want = total - have < 128 ? total - have : 128;
                if (R_FAILED(timeLoadLocationNameList(have, s_names + have, want, &got)) || got <= 0) break;
                have += got > want ? want : got;
            }
            int n = 0;                            // the valid ones, packed (a bad entry is left out, never drawn or sent)
            for (s32 i = 0; i < have; i++) {
                if (!ValidName(s_names[i].name, (int)sizeof s_names[i].name)) continue;
                if (n != i) memcpy(&s_names[n], &s_names[i], sizeof s_names[i]);
                n++;
            }
            if (n <= 0) { Release(); return false; }
            s_order = (u16 *)malloc(sizeof(u16) * (size_t)n);
            if (!s_order) { Release(); return false; }
            for (int i = 0; i < n; i++) s_order[i] = (u16)i;
            qsort(s_order, (size_t)n, sizeof(u16), Cmp);
            s_nz = n;
            s_rule = (TimeZoneRule *)malloc(sizeof(TimeZoneRule));   // (optional: without it, no offset under the chosen one)
            TimeLocationName cur;
            memset(&cur, 0, sizeof cur);
            s_curZ = -1;
            if (R_SUCCEEDED(timeGetDeviceLocationName(&cur))) {
                cur.name[sizeof cur.name - 1] = 0;
                for (int i = 0; i < n; i++) if (!strcmp(Name(i), cur.name)) { s_curZ = i; break; }
            }
            return true;
        }
        void ZoneSelect(int s, float now) {
            if (s_nz <= 0) return;
            s = s < 0 ? 0 : (s >= s_nz ? s_nz - 1 : s);
            if (s == s_sel) return;
            s_sel = s; s_selAt = now; s_zquiet = false;
        }
        void ZoneStep(int d, float now) {
            const int s = s_sel + d;
            if (s < 0 || s >= s_nz) { SndEnd(0); s_zbumpV += d * 230.0f; return; }
            ZoneSelect(s, now);
        }
        void RegionJump(int d, float now) {
            if (s_nz <= 0) return;
            const int start = RegionStart(s_sel);
            int s;
            if (d > 0) { s = start; while (s < s_nz && SameRegion(s, start)) s++; }
            else s = start > 0 ? RegionStart(start - 1) : -1;
            s_regNudgeV += d * 260.0f;
            if (s < 0 || s >= s_nz) { SndEnd(d * 0.4f); return; }
            ZoneSelect(s, now);
            audio::PlaySweep(audio::Whoosh, -d * 0.4f, d * 0.4f, 0.3f, 0.25f);
            SndFocus(d * 0.3f);
        }
        void ChooseZone(float now) {
            if (s_leaveAt > 0 || s_nz <= 0) return;
            if (s_sel == s_curZ) { SndOk(); s_flash = 1; s_popV += 7; s_leaveAt = now + Pace(0.3f); return; }
            bool live = false;
            const Result rc = sysclock::SetZone(Name(s_sel), &live);
            if (R_FAILED(rc)) { popup::Error(now, "Le fuseau horaire n'a pas pu changer", "La console a refus\xc3\xa9 ce fuseau.", (unsigned)rc); return; }
            SndOk(); s_flash = 1; s_popV += 7;
            if (live) {
                char city[72], m[128];
                City(s_sel, city, sizeof city);
                s_curZ = s_sel;
                if (s_known && s_ruleFor == s_sel) {
                    char u[16]; UtcText(u, sizeof u, s_offset);
                    snprintf(m, sizeof m, lang::T("Fuseau horaire : %s (%s)"), city, u);
                } else snprintf(m, sizeof m, lang::T("Fuseau horaire : %s"), city);
                notify::Post(now, m, notify::kDone);
            } else {                              // only the setting took it: it comes at the next boot - now, or later?
                popup::Spec s;
                s.kind = popup::kAsk; s.title = "Red\xc3\xa9marrer maintenant ?";
                s.text = "Le nouveau fuseau sera pris en compte au prochain d\xc3\xa9marrage.";
                s.yes = "Red\xc3\xa9marrer"; s.no = "Plus tard";
                s.onYes = [](void *, float t) { if (Settings::s_restart) Settings::s_restart(t); };
                popup::Show(now, s);
            }
            if (s_onSet) s_onSet(now);
            s_leaveAt = now + Pace(0.34f);
        }
        float RowY(float off) { return kListY + off * kZRow + kZGap * (off < -1 ? -1.0f : (off > 1 ? 1.0f : off)); }
        void ZonesUpdate(float now, float dt) {
            Glide(s_zpos, s_zvel, (float)s_sel, dt, 9.0f);
            const int n = (int)lroundf(s_zpos);
            if (n != s_znotch) { if (!s_zquiet) Tick(n, 0.25f); s_znotch = n; }
            Glide(s_zbump, s_zbumpV, 0.0f, dt, 15.0f);
            Glide(s_regNudge, s_regNudgeV, 0.0f, dt, 14.0f);
            timeGetCurrentTime(TimeType_UserSystemClock, &s_posix);
            // the chosen place's offset, once it has settled (one rule read per place, not per frame)
            if (s_nz > 0 && s_rule && s_sel != s_ruleFor && now - s_selAt > 0.16f) {
                TimeLocationName ln;
                memset(&ln, 0, sizeof ln);
                snprintf(ln.name, sizeof ln.name, "%s", Name(s_sel));
                TimeCalendarTime cal; TimeCalendarAdditionalInfo info;
                memset(&info, 0, sizeof info);
                s_known = R_SUCCEEDED(timeLoadTimeZoneRule(&ln, s_rule)) && R_SUCCEEDED(timeToCalendarTime(s_rule, s_posix, &cal, &info))
                          && info.offset > -16 * 3600 && info.offset < 16 * 3600;
                s_offset = s_known ? info.offset : 0;
                s_ruleFor = s_sel;
            }
            Glide(s_infoK, s_infoV, s_known && s_ruleFor == s_sel ? 1.0f : 0.0f, dt, 10.0f);
            const int r = s_nz > 0 ? RegionStart(s_sel) : -1;
            if (r != s_reg) { s_regOld = s_reg; s_regDir = r > s_reg ? 1 : -1; s_reg = r; s_regAt = now; }
        }
        void ZonesDraw(Ui &ui, float now, float a, float cx) {
            const float tx0 = cx - 170;           // the list's words start there; the title and the region line up with them
            TitleLeft(ui, "Fuseau horaire", tx0 - 2, a);
            if (s_nz <= 0) return;
            char b[96], c2[96];
            {   // the region: ◀ name ▶, the old one leaves on one side while the new one comes from the other (the width glides)
                const float e = EaseGlide((now - s_regAt) / Pace(0.36f)), hy = 152;
                float wn = 0, wo = 0;
                if (s_reg >= 0) wn = ui.Width(fonts::kSetOpt, RegionLabel(s_reg, b, sizeof b), 0.5f);
                if (s_regOld >= 0) wo = ui.Width(fonts::kSetOpt, RegionLabel(s_regOld, b, sizeof b), 0.5f);
                const float w = e >= 1 || s_regOld < 0 ? wn : wo + (wn - wo) * e, x0 = tx0 + 18;
                s_regMid = x0 + w / 2;
                for (int k = 0; k < 2; k++) {
                    const int r = k ? s_regOld : s_reg;
                    if (r < 0 || (k && e >= 1)) continue;
                    const char *t = RegionLabel(r, b, sizeof b);
                    const float al = k ? 1 - e : e, dy = (k ? -s_regDir : s_regDir) * 12.0f * (k ? e : 1 - e);
                    ui.TextCentered(fonts::kSetOpt, t, x0 + ui.Width(fonts::kSetOpt, t, 0.5f) / 2, hy + dy, 0.5f, ps2::A(ps2::kChosen, 0.92f * a * al));
                }
                const float ay = hy + 11, nl = fminf(s_regNudge, 0.0f) * 0.03f, nr = fmaxf(s_regNudge, 0.0f) * 0.03f;
                ui.Arrow(tx0 + 4 + nl, ay, 5, 0, ps2::A(ps2::kChoice, 0.8f * a));
                ui.Arrow(x0 + w + 16 + nr, ay, 5, 1, ps2::A(ps2::kChoice, 0.8f * a));
            }
            const int base = (int)floorf(s_zpos);
            for (int j = base - 6; j <= base + 7; j++) {
                if (j < 0 || j >= s_nz) continue;
                const float off = (float)j - s_zpos;
                if (fabsf(off) > 4.4f) continue;
                const float y = RowY(off) + s_zbump, k = Clamp01(1 - fabsf(off));
                const float o = a * Clamp01(1.0f - (fabsf(off) - 0.7f) / 3.7f) * (SameRegion(j, s_sel) ? 1.0f : 0.6f);
                if (o <= 0.003f) continue;
                City(j, c2, sizeof c2);
                const char *t = c2;
                const float tx = tx0 + 16 * k;
                if (k > 0.01f) {
                    float tw = 0;
                    for (const char *q = t; *q;) tw += ui.Advance(fonts::kSetSel, NextCodepoint(q));
                    ui.Chosen(tx0 - 70, cx + 420, y - 6, tx, tw, k * o * (1 + 0.6f * s_flash));
                }
                if (k < 0.99f) { float px = tx; for (const char *q = t; *q;) px += ui.Glyph(fonts::kGameItem, NextCodepoint(q), px, y - 14, ps2::A(ps2::kChoice, 0.82f * o * (1 - k)), 0, 0, 0, 0); }
                if (k > 0.01f) {
                    const float s = 1 + 0.05f * s_pop * k;
                    ui.SetXform(s, tx, y - 11, tx, y - 11);
                    float px = tx;
                    for (const char *q = t; *q;) px += ui.Glyph(fonts::kSetSel, NextCodepoint(q), px, y - 25, ps2::A(ps2::kChosen, 0.96f * o * k), 0, 0, 0, 0);
                    ui.SetXform(1, 640, 360, 640, 360);
                    const float ia = j == s_sel ? o * k * Clamp01(s_infoK) : 0.0f;   // its offset and the time there (once its rule is read)
                    if (ia > 0.003f) {
                        char u[16], hm[16];
                        UtcText(u, sizeof u, s_offset);
                        const long long lt = (long long)s_posix + s_offset;
                        const int mins = (int)(((lt / 60) % 1440 + 1440) % 1440);
                        sysclock::HourText(hm, sizeof hm, mins / 60, mins % 60);
                        if (j == s_curZ) snprintf(b, sizeof b, lang::T("Fuseau actuel \xc2\xb7 %s \xc2\xb7 %s"), u, hm);
                        else snprintf(b, sizeof b, "%s \xc2\xb7 %s", u, hm);
                        float dx = tx;
                        for (const char *q = b; *q;) dx += ui.Glyph(fonts::kSetDesc, NextCodepoint(q), dx, y + 11, ps2::A(ps2::kFaint, ia), 0, 0, 0, 0) + 0.3f;
                    }
                }
                if (j == s_curZ) {                // the current one: the accent's small light, wherever it is in the list
                    const float dx = tx - 24, dy = y - 3 - 8 * k;
                    ui.Shape(dx - 10, dy - 10, dx + 10, dy + 10, -1, -1, 1, 1, Settings::Accent(0.9f * o, 0.5f), 21);
                    ui.Shape(dx - 2.5f, dy - 2.5f, dx + 2.5f, dy + 2.5f, 0, 0, 5, 5, ps2::A(ps2::kChosen, o), 24, 5, 5, 2.5f);
                }
            }
            if (s_nz > 9) {                       // more above / below: small arrows (they fade at the ends)
                ui.Arrow(tx0 - 24, RowY(-4.2f), 4.5f, 2, ps2::A(ps2::kChoice, 0.6f * a * Clamp01(s_zpos / 3.0f)));
                ui.Arrow(tx0 - 24, RowY(4.2f), 4.5f, 3, ps2::A(ps2::kChoice, 0.6f * a * Clamp01((s_nz - 1 - s_zpos) / 3.0f)));
            }
        }
    }

    // ======== the pages ========
    void OpenPicker(float now, Done onSet) {
        TimeCalendarTime cal; TimeCalendarAdditionalInfo info;
        if (!sysclock::Now(cal, info)) { cal.year = 2026; cal.month = 1; cal.day = 1; cal.hour = 12; cal.minute = 0; }
        const int v[kCols] = { cal.day, cal.month, cal.year < kYearLo ? kYearLo : (cal.year > kYearHi ? kYearHi : cal.year), cal.hour, cal.minute };
        for (int c = 0; c < kCols; c++) {
            Whl &w = s_w[c];
            w = Whl{};
            w.t = w.v = v[c];
        }
        const int order[kCols] = { cMonth, cYear, cDay, cHour, cMin };   // (the day after the month and the year: its length)
        for (int c : order) {                     // (each value inside its wheel, whatever the clock said)
            Whl &w = s_w[c];
            w.t = w.v = Val(c, w.t);
            w.pos = (float)w.t - 2.6f; w.notch = (int)lroundf(w.pos);   // they roll in from below, silently
        }
        s_focus = cHour; s_focusX = (float)cHour; s_focusV = 0; s_dirty = false; s_dragCol = -1;
        s_page = s_drawn = kPicker; s_openAt = now; s_onSet = onSet; s_leaveAt = -1; s_flash = 0; s_pop = s_popV = 0;
        Release();                                // (the zones' memory, if they had not gone yet)
        SndOpen();
    }

    void OpenZones(float now, Done onSet) {
        if (!LoadZones()) {
            audio::Thock(0, 0.6f, 0.2f);
            notify::Post(now, "La liste des fuseaux n'a pas pu se charger", notify::kError);
            return;
        }
        s_sel = s_curZ >= 0 ? s_curZ : 0;
        s_zpos = (float)s_sel + 4.0f; s_zvel = 0; s_znotch = (int)lroundf(s_zpos); s_zquiet = true;   // the list rolls to it
        s_zbump = s_zbumpV = 0; s_selAt = now; s_infoK = s_infoV = 0; s_reg = s_regOld = -1; s_regAt = now - 10;
        s_page = s_drawn = kZones; s_openAt = now; s_onSet = onSet; s_leaveAt = -1; s_flash = 0; s_pop = s_popV = 0;
        SndOpen();
    }

    bool Open() { return s_page != kNone; }
    float Shown() { return Clamp01(s_show); }

    void Move(int dc, int dr, float now) {
        if (s_page == kNone || s_leaveAt > 0) return;
        if (s_page == kPicker) {
            if (dc) {
                const int f = s_focus + dc;
                if (f < 0 || f >= kCols) { SndEnd(dc * 0.5f); s_w[s_focus].kickV += dc * 60.0f; }
                else { s_focus = f; SndFocus(s_lay.ok ? s_lay.x[f] / 900.0f : 0); }
            }
            if (dr) Step(s_focus, -dr);           // ▲ = the next value (it comes from above)
        } else {
            if (dc) RegionJump(dc, now);
            if (dr) ZoneStep(dr, now);
        }
        s_heldDir = dr; s_heldSince = now; s_nextRep = now + 0.38f;   // (a held ▲ ▼ repeats after a moment: Held)
    }

    void Held(int dr, float now) {
        if (s_page == kNone || s_leaveAt > 0 || !dr) { s_heldDir = 0; return; }
        if (dr != s_heldDir) { s_heldDir = dr; s_heldSince = now; s_nextRep = now + 0.38f; return; }
        if (now < s_nextRep) return;
        const float held = now - s_heldSince;    // quicker the longer it is held
        s_nextRep = now + (held > 1.6f ? 0.035f : (held > 0.9f ? 0.06f : 0.09f));
        if (s_page == kPicker) Step(s_focus, -dr);
        else ZoneStep(dr * (held > 2.4f ? 3 : 1), now);
    }

    void PressA(float now) {
        if (s_page == kPicker) Confirm(now);
        else if (s_page == kZones) ChooseZone(now);
    }

    void PressB(float now) { if (s_leaveAt < 0) Leave(now, false); }

    void Tap(float x, float y, float now) {
        if (s_page == kNone || s_leaveAt > 0) return;
        if (!s_xmb && x < 470) { PressB(now); return; }   // (the carousel's icon, on the left: back to the list)
        if (s_page == kPicker) {
            if (!s_lay.ok) return;
            for (int c = 0; c < kCols; c++) {
                if (fabsf(x - (s_cx + s_lay.x[c])) > s_lay.w[c] * 0.5f + 7) continue;
                if (c != s_focus) { s_focus = c; SndFocus(s_lay.x[c] / 900.0f); return; }
                const float dy = y - kWheelY;
                if (fabsf(dy) < kRowH * 0.5f) { Confirm(now); return; }   // the chosen value: A
                const int n = (int)lroundf(fabsf(dy) / kRowH) < 1 ? 1 : (int)lroundf(fabsf(dy) / kRowH);
                for (int k = 0; k < n && k < 3; k++) if (!Step(c, dy < 0 ? 1 : -1)) break;   // above = the next ones
                return;
            }
            return;
        }
        if (s_nz <= 0) return;
        if (fabsf(y - 163) < 24) { RegionJump(x < s_regMid ? -1 : 1, now); return; }
        int best = -1; float bd = 27;
        for (int j = (int)floorf(s_zpos) - 5; j <= (int)floorf(s_zpos) + 6; j++) {
            if (j < 0 || j >= s_nz) continue;
            const float d = fabsf(RowY((float)j - s_zpos) - 6 - y);
            if (d < bd) { bd = d; best = j; }
        }
        if (best < 0) return;
        if (best == s_sel) ChooseZone(now);
        else { ZoneSelect(best, now); SndFocus(0.2f); }
    }

    void DragBegin(float x, float y) {
        (void)y;
        if (s_page == kPicker && s_lay.ok) {
            int best = s_focus; float bd = 1e9f;
            for (int c = 0; c < kCols; c++) { const float d = fabsf(x - (s_cx + s_lay.x[c])); if (d < bd) { bd = d; best = c; } }
            if (best != s_focus) { s_focus = best; SndFocus(s_lay.x[best] / 900.0f); }
            s_dragCol = best; s_dragFrom = s_w[best].t;
        } else if (s_page == kZones) s_zFrom = s_sel;
    }

    void DragTo(float rows) {
        if (s_page == kNone || s_leaveAt > 0) return;
        if (s_page == kPicker) {
            if (s_dragCol < 0) return;
            const int target = s_dragFrom - (int)lroundf(rows * 60.0f / kRowH);   // the finger up: the values below come up
            Whl &w = s_w[s_dragCol];
            for (int g = 0; g < 200 && w.t != target; g++) if (!Step(s_dragCol, target > w.t ? 1 : -1)) break;
        } else ZoneSelect(s_zFrom + (int)lroundf(rows * 60.0f / kZRow), s_now);
    }

    void Update(float now, float dt) {
        s_now = now;
        if (s_leaveAt > 0 && now >= s_leaveAt) Leave(now, true);
        Glide(s_show, s_showV, s_page != kNone ? 1.0f : 0.0f, dt, s_page != kNone ? 7.0f : 9.0f);
        if (s_page == kNone && s_show < 0.003f) {
            s_show = 0; s_showV = 0;
            if (s_drawn != kNone) { s_drawn = kNone; Release(); }
            return;
        }
        s_flash *= expf(-dt * 3.2f);
        Glide(s_pop, s_popV, 0.0f, dt, 9.0f);
        if (s_drawn == kPicker) PickerUpdate(now, dt);
        else if (s_drawn == kZones) ZonesUpdate(now, dt);
    }

    void Draw(Ui &ui, float now, float alpha, float slide, bool xmb) {
        if (s_drawn == kNone || s_show <= 0.003f) return;
        const float k = Clamp01(s_show), a = alpha * k * k * (3 - 2 * k);
        s_xmb = xmb;
        s_cx = (xmb ? 640.0f : 860.0f) + slide;
        if (a <= 0.003f) return;
        const float cx = s_cx + 46.0f * (1 - k);  // (it comes in from the right, and goes back there)
        if (s_drawn == kPicker) PickerDraw(ui, now, a, cx);
        else ZonesDraw(ui, now, a, cx);
    }

    void Hints(const char *&aL, const char *&bL, const char **chip, const char **lbl, int &nr) {
        nr = 0;
        aL = s_page == kZones ? "Choisir" : "Valider"; bL = "Annuler";
        chip[nr] = "chipLeRi"; lbl[nr++] = s_page == kZones ? "R\xc3\xa9gions" : "Choisir";
        chip[nr] = "chipUpDo"; lbl[nr++] = s_page == kZones ? "Fuseaux" : "R\xc3\xa9gler";
    }

} // namespace brume::datepages
