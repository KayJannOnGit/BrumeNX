// Brume - v96: the profile page. See MyPage.hpp.
#include "MyPage.hpp"
#include "Profiles.hpp"
#include "Social.hpp"
#include "Settings.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Ease.hpp"
#include "Busy.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include "Worker.hpp"
#include <switch.h>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
    int    qext_game_running(void);
    int    qext_title_count(void);
    u64    qext_title_id(int index);
    int    qext_title_name(int index, char *out, unsigned cap);
    int    qext_title_is_virtual(u64 tid);
}

namespace brume::mypage {

    bool s_avatarsOk = false;
    void (*s_switchPanel)(float) = nullptr;
    void (*s_friends)(float) = nullptr;

    namespace {
        constexpr const char *kFile = "sdmc:/brume/profil.ini";
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

        // ---- the page ----
        bool  s_open = false, s_imgNextendo = false, s_users = false;
        float s_k = 0, s_kv = 0, s_openAt = -100, s_moveAt = -100, s_selY = 0, s_selV = 0;
        int   s_sel = 0, s_userSel = 0;
        enum Row { kRUser, kRImage, kRStatus, kRNext, kRSwitch };
        int   s_rows[5], s_nrows = 0;
        struct Hit { int i; float x0, y0, x1, y1; };
        Hit   s_hit[12]; int s_nhit = 0;
        constexpr float kListX0 = 112, kListX1 = 548, kListTop = 352, kRowH = 54;

        // ---- the play time (pdm:qry, read-only), on a background thread ----
        constexpr int kMaxT = 256;
        struct Played { u64 tid; u32 min; };
        Worker s_work;
        u64    s_tids[kMaxT]; int s_nt = 0;             // (the job's input, made on the main thread)
        AccountUid s_uid = {};
        struct Stats { Played top[3]; int ntop, played; u64 total, lastTid, lastTs; bool ok; };
        Stats  s_job = {}, s_st = {};                    // the job's result / the one shown
        int    s_state = 0;                              // 0 nothing yet, 1 reading, 2 ready
        float  s_statAt = -100;
        AccountUid s_statUid = {};
        char   s_topName[3][72], s_lastName[72];

        bool Same(const AccountUid &a, const AccountUid &b) { return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1]; }
        void Scan(void *) {
            Stats &r = s_job;
            memset(&r, 0, sizeof r);
#ifdef BRUME_UISIM
            // the PC preview: a made-up play log (no pdm there)
            static const Played kFake[3] = { { 0x0100000000010000ULL, 7340 }, { 0x01006A800016E000ULL, 4125 }, { 0x01003BC0000A0000ULL, 2210 } };
            memcpy(r.top, kFake, sizeof kFake); r.ntop = 3; r.played = 37; r.total = 12890;
            r.lastTid = kFake[0].tid; r.lastTs = 1795400000ULL - 86400; r.ok = true;
#else
            if (R_FAILED(pdmqryInitialize())) return;
            for (int i = 0; i < s_nt; i++) {
                PdmPlayStatistics st;
                memset(&st, 0, sizeof st);
                if (R_FAILED(pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(s_tids[i], s_uid, false, &st))) continue;
                const u64 min = st.playtime / 60000000000ULL;
                if (!st.total_launches && !min) continue;
                if (min > 24ULL * 60 * 365 * 30) continue;   // (nonsense: more than 30 years of play)
                r.played++; r.total += min;
                const Played p = { s_tids[i], (u32)min };
                int k = r.ntop < 3 ? r.ntop++ : 3;
                if (k == 3) { if (r.top[2].min >= p.min) k = -1; else k = 2; }
                if (k >= 0) { r.top[k] = p; while (k > 0 && r.top[k - 1].min < r.top[k].min) { const Played t = r.top[k]; r.top[k] = r.top[k - 1]; r.top[k - 1] = t; k--; } }
                if (st.last_timestamp_user > r.lastTs && st.last_timestamp_user < 4102444800ULL) { r.lastTs = st.last_timestamp_user; r.lastTid = s_tids[i]; }
            }
            pdmqryExit();
            r.ok = true;
#endif
        }
        // a game's name (installed: the console's; the preview: a few known ones)
        void GameName(u64 tid, char *out, int cap) {
            out[0] = 0;
            for (int j = 0, n = qext_title_count(); j < n; j++)
                if (qext_title_id(j) == tid) { if (qext_title_name(j, out, (unsigned)cap) > 0) return; break; }
#ifdef BRUME_UISIM
            if (tid == 0x0100000000010000ULL) snprintf(out, cap, "Mario Kart 8 Deluxe");
            else if (tid == 0x01006A800016E000ULL) snprintf(out, cap, "Super Smash Bros. Ultimate");
            else if (tid == 0x01003BC0000A0000ULL) snprintf(out, cap, "Splatoon 2");
            if (out[0]) return;
#endif
            snprintf(out, cap, "%s", lang::T("Un jeu supprimé"));
        }
        void StartScan(float now) {
            if (!s_work.Idle() || qext_game_running()) return;
            s_uid = Profiles::Active();
            s_nt = 0;
            for (int j = 0, n = qext_title_count(); j < n && s_nt < kMaxT; j++) {
                const u64 tid = qext_title_id(j);
                if (tid && !qext_title_is_virtual(tid)) s_tids[s_nt++] = tid;
            }
            s_state = 1; s_statAt = now; s_statUid = s_uid;
            s_work.Start(Scan, nullptr, 0x4000);
        }
        // a length of play: « 214 h », « 3 h 20 », « 45 min »
        void Hours(u64 min, char *out, int cap) {
            if (min >= 600) snprintf(out, cap, "%llu h", (unsigned long long)(min / 60));
            else if (min >= 60) snprintf(out, cap, "%llu h %02llu", (unsigned long long)(min / 60), (unsigned long long)(min % 60));
            else if (min >= 1) snprintf(out, cap, "%llu min", (unsigned long long)min);
            else snprintf(out, cap, "%s", lang::T("moins d'une minute"));
        }
        // the day of a moment (the console's time zone), counted from a fixed day
        long DayOf(u64 posix) {
            TimeCalendarTime c; TimeCalendarAdditionalInfo info;
            if (R_FAILED(timeToCalendarTimeWithMyRule(posix, &c, &info))) return (long)(posix / 86400);
            const int y = c.year - (c.month <= 2), era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
            const int doy = (153 * (c.month + (c.month > 2 ? -3 : 9)) + 2) / 5 + c.day - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return (long)era * 146097 + doe;
        }
        void When(u64 ts, char *out, int cap) {
            u64 now = 0;
#ifdef BRUME_UISIM
            now = 1795400000ULL;
#else
            if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &now))) now = 0;
#endif
            const long d = now ? DayOf(now) - DayOf(ts) : 99;
            if (d <= 0) snprintf(out, cap, "%s", lang::T("aujourd'hui"));
            else if (d == 1) snprintf(out, cap, "%s", lang::T("hier"));
            else if (d < 30) snprintf(out, cap, lang::T("il y a %ld jours"), d);
            else {
                TimeCalendarTime c; TimeCalendarAdditionalInfo info;
                if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(ts, &c, &info))) snprintf(out, cap, lang::T("le %d/%02d/%d"), c.day, c.month, c.year);
                else snprintf(out, cap, "%s", lang::T("il y a longtemps"));
            }
        }
        // a name cut to `letters` letters (never in the middle of one), an ellipsis after
        void Cut(const char *in, char *out, int cap, int letters) {
            int n = 0, l = 0;
            while (in[n] && l < letters && n + 4 < cap) { n++; while (((unsigned char)in[n] & 0xC0) == 0x80) n++; l++; }
            memcpy(out, in, (size_t)n); out[n] = 0;
            if (in[n] && n + 4 < cap) snprintf(out + n, (size_t)(cap - n), "\xe2\x80\xa6");
        }
        float Text(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c, float ls = 0.0f) {
            const float x0 = x;
            for (const char *q = lang::T(s); *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0) + ls;
            return x - x0;
        }
        void Save() {
            if (FILE *f = fopen(kFile, "w")) { fprintf(f, "# Brume - ton profil\nimage=%s\n", s_imgNextendo ? "nextendo" : "switch"); fclose(f); }
        }
        void BuildRows(const social::Me &m) {
            s_nrows = 0;
            s_rows[s_nrows++] = kRUser;
            if (m.in) { s_rows[s_nrows++] = kRImage; s_rows[s_nrows++] = kRStatus; }
            s_rows[s_nrows++] = kRNext;
            s_rows[s_nrows++] = kRSwitch;
            if (s_sel >= s_nrows) s_sel = s_nrows - 1;
        }
        int Lines() { return s_users ? Profiles::Count() : s_nrows; }
        int Sel() { return s_users ? s_userSel : s_sel; }
        void Chosen(float now) { s_moveAt = now; audio::Play(audio::Deplacement, -0.4f, 1.0f); haptic::Tick(0.5f); }
    }

    void Load() {
        s_imgNextendo = false;
        char b[256] = {};
        if (FILE *f = fopen(kFile, "r")) {
            const size_t n = fread(b, 1, sizeof b - 1, f);
            fclose(f);
            b[n] = 0;
            for (char *line = b; line && *line;) {
                char *nl = strchr(line, '\n');
                if (nl) *nl = 0;
                while (*line == ' ' || *line == '\t') line++;
                if (!strncmp(line, "image", 5)) {
                    const char *v = line + 5;
                    while (*v == ' ' || *v == '\t' || *v == '=') v++;
                    s_imgNextendo = !strncasecmp(v, "nextendo", 8);
                }
                line = nl ? nl + 1 : nullptr;
            }
        }
        social::s_barNextendo = s_imgNextendo;
    }

    bool Open() { return s_open; }
    float Amount() { return Clamp01(s_k); }
    void Show(float now) {
        if (s_open) return;
        s_open = true; s_openAt = now; s_users = false; s_sel = 0;
        s_selY = kListTop; s_selV = 0;
        social::ProfileShown(true, now);
        AccountUid u = Profiles::Active();
        if (s_state == 0 || !Same(u, s_statUid) || now - s_statAt > 60.0f) StartScan(now);
        audio::Play(audio::Tick2400, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, -0.5f, -0.1f, 0.5f, 0.35f);
        audio::Bell(audio::HarmonyNote(1, 72, 84), 0.03f, true, 0.06f);
        haptic::Thump(0.5f);
    }
    void Close(float now) {
        if (!s_open) return;
        (void)now;
        s_open = false; s_users = false;
        social::ProfileShown(false, now);
        audio::Play(audio::Tick1700, 0, 1.0f);
        haptic::Tick(0.5f);
    }
    void CloseNow() { if (s_open) { s_open = false; s_users = false; social::ProfileShown(false, 0); } s_k = 0; s_kv = 0; }

    void Update(float now, float dt) {
        Glide(s_k, s_kv, s_open ? 1.0f : 0.0f, dt, 7.0f);
        social::s_barNextendo = s_imgNextendo;
        if (s_work.Done()) {
            s_st = s_job; s_state = 2;
            for (int i = 0; i < s_st.ntop; i++) GameName(s_st.top[i].tid, s_topName[i], sizeof s_topName[i]);
            s_lastName[0] = 0;
            if (s_st.lastTid) GameName(s_st.lastTid, s_lastName, sizeof s_lastName);
        }
        if (!s_open && s_k < 0.003f) return;
        const social::Me m = social::GetMe();
        BuildRows(m);
        if (s_users && s_userSel >= Profiles::Count()) s_userSel = Profiles::Count() - 1;
        Glide(s_selY, s_selV, kListTop + Sel() * kRowH, dt, 9.0f);
        // another user chosen elsewhere (the Profils panel): the play time follows
        if (s_open && s_state == 2 && !Same(Profiles::Active(), s_statUid) && s_work.Idle()) StartScan(now);
    }

    void Move(int dy, float now) {
        if (!s_open) return;
        const int n = Lines(), to = Sel() + dy;
        if (to < 0 || to >= n) { audio::Thock(dy < 0 ? -0.4f : 0.4f, 0.9f, 0.05f); return; }
        (s_users ? s_userSel : s_sel) = to;
        Chosen(now);
    }
    void Side(int dx, float now) {
        if (!s_open || s_users || s_sel >= s_nrows) return;
        const social::Me m = social::GetMe();
        const int r = s_rows[s_sel];
        if (r == kRImage) {
            const bool want = dx > 0;
            if (want == s_imgNextendo) { audio::Thock(dx < 0 ? -0.4f : 0.4f, 0.9f, 0.05f); return; }
            s_imgNextendo = want; social::s_barNextendo = want; Save();
            audio::Play(audio::Deplacement, dx * 0.4f, 1.0f);
            audio::Bell(audio::HarmonyNote(want ? 2 : 0, 72, 86), 0.03f, true);
            haptic::Tick(0.6f);
        } else if (r == kRStatus) {
            if (m.canHide == 0) { audio::Thock(0, 0.8f, 0.06f); notify::Post(now, lang::T("Reconnecte-toi à Nextendo pour changer ton statut"), notify::kError); return; }
            const bool hide = dx > 0;
            if ((m.hidden == 1) == hide) { audio::Thock(dx < 0 ? -0.4f : 0.4f, 0.9f, 0.05f); return; }
            social::SetHidden(hide, now);
            audio::Play(audio::Deplacement, dx * 0.4f, 1.0f);
            audio::Bell(audio::HarmonyNote(hide ? 0 : 2, 67, 81), 0.03f, true);
            haptic::Tick(0.6f);
        }
    }
    void PressA(float now) {
        if (!s_open) return;
        if (s_users) {                                   // who plays: this one
            if (s_userSel < 0 || s_userSel >= Profiles::Count()) return;
            char msg[96];
            if (s_userSel != Profiles::ActiveIndex()) {
                Profiles::Choose(s_userSel);
                for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k, 72, 86), 0.04f - k * 0.008f, true, k * 0.08f);
                haptic::Tap(0.7f);
                snprintf(msg, sizeof msg, lang::T("C'est %s qui joue maintenant"), Profiles::NameAt(s_userSel));
                notify::Post(now, msg, notify::kDone, false);
                StartScan(now);
            } else { audio::Play(audio::Tick2400, 0, 0.8f); }
            s_users = false; s_moveAt = now;
            return;
        }
        if (s_sel >= s_nrows) return;
        const social::Me m = social::GetMe();
        switch (s_rows[s_sel]) {
            case kRUser:
                if (Profiles::Count() <= 0) { audio::Thock(0, 0.8f, 0.06f); return; }
                s_users = true; s_userSel = Profiles::ActiveIndex() >= 0 ? Profiles::ActiveIndex() : 0;
                s_selY = kListTop + s_userSel * kRowH; s_moveAt = now;
                audio::Play(audio::Tick2400, 0, 1.0f); haptic::Tick(0.6f);
                return;
            case kRImage: Side(s_imgNextendo ? -1 : 1, now); return;
            case kRStatus: Side(m.hidden == 1 ? -1 : 1, now); return;
            case kRNext:
                audio::Play(audio::Tick2400, 0, 1.0f);
                if (m.in) { social::SignOut(now); haptic::Tick(0.6f); }
                else { CloseNow(); if (s_friends) s_friends(now); }   // the Amis page: its sign-in (the console's browser or a phone)
                return;
            case kRSwitch:
                audio::Play(audio::Tick2400, 0, 1.0f);
                CloseNow();
                if (s_switchPanel) s_switchPanel(now);
                return;
        }
    }
    void PressB(float now) {
        if (s_users) { s_users = false; s_moveAt = now; audio::Play(audio::Tick1700, 0, 0.9f); haptic::Tick(0.4f); return; }
        Close(now);
    }
    void PressY(float now) {
        if (!s_open) return;
        social::RefreshMe(now);
        if (now - s_statAt > 3.0f) StartScan(now);
        audio::Play(audio::Tick2400, 0, 0.7f);
    }
    void Tap(float x, float y, float now) {
        if (!s_open) return;
        if (y < 92) { Close(now); return; }              // the top bar (the profile itself): closes, as B
        for (int k = 0; k < s_nhit; k++) {
            const Hit &h = s_hit[k];
            if (x < h.x0 || x > h.x1 || y < h.y0 || y > h.y1) continue;
            if (h.i == Sel()) PressA(now);
            else { (s_users ? s_userSel : s_sel) = h.i; Chosen(now); }
            return;
        }
    }

    bool BarAvatar(Ui &ui, float cx, float cy, float r, float a) {
        if (!s_imgNextendo) return false;
        return social::DrawMeAvatar(ui, cx, cy, r, a);
    }

    void Draw(Ui &ui, float now) {
        s_nhit = 0;
        const float al = Clamp01(s_k);
        if (al <= 0.01f) return;
        const social::Me m = social::GetMe();
        const float rise = (1 - al) * 22;
        // the scene steps back (the consoles, the page that was there): darker, the fog still breathes through
        ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0.004f, 0.016f, 0.85f * al }, 24, 1280, 720, 0);   // (one full-screen plate, light program: no veil over it)
        // ---- who: the avatar big, the name, what the friends see ----
        const float acx = 200, acy = 204 + rise, ar = 80;
        const Rgba ac = Settings::Accent(1.0f);
        const float br = 0.85f + 0.15f * sinf(now * 1.1f);
        ui.Light(acx - 190, acy - 160, acx + 190, acy + 160, Rgba{ ac.r, ac.g, ac.b, 0.30f * al * br }, true);
        ui.Shape(acx - ar - 34, acy - ar - 34, acx + ar + 34, acy + ar + 34, -1, -1, 1, 1, Settings::Accent(0.75f * al), 21);   // its halo
        const int slot = Profiles::ActiveSlot();
        bool drawn = s_imgNextendo && m.in && social::DrawMeAvatar(ui, acx, acy, ar, al);
        if (!drawn && slot >= 0 && s_avatarsOk) { Profiles::DrawAvatar(ui, slot, acx, acy, ar, al); drawn = true; }
        if (!drawn) {                                    // no picture: the first letter in a soft disc of the accent
            ui.Shape(acx - ar, acy - ar, acx + ar, acy + ar, 0, 0, 2 * ar, 2 * ar, Settings::Accent(0.32f * al), 24, 2 * ar, 2 * ar, ar);
            char ini[8] = {};
            const char *nm = Profiles::ActiveName(), *q = nm;
            NextCodepoint(q);
            const int n = (int)(q - nm);
            if (n > 0 && n < 5) { memcpy(ini, nm, (size_t)n); ui.TextCentered(fonts::kPageTitle, ini, acx, acy - 22, 0, ps2::A(ps2::kChosen, 0.95f * al)); }
        }
        const float nx = 326;
        char name[64]; Cut(Profiles::ActiveName(), name, sizeof name, 18);
        Text(ui, fonts::kPageTitle, name, nx, 138 + rise, ps2::A(ps2::kChosen, al), 1.5f);
        {   // what the friends see: a small light and its words
            const char *st = nullptr; char buf[128]; Rgba c = ps2::kFaint; bool dot = true;
            if (!m.in) { st = lang::T("Profil de la console"); dot = false; }
            else if (m.hidden == 1) st = lang::T("Invisible pour tes amis");
            else if (m.status == 2) {
                char g[72]; Cut(m.game[0] ? m.game : lang::T("un jeu"), g, sizeof g, 30);
                snprintf(buf, sizeof buf, lang::T("Joue à %s"), g); st = buf; c = ps2::kGreen;
            } else if (m.status == 1) { st = lang::T("En ligne"); c = ps2::kChoice; }
            else if (m.status == 0) st = lang::T("Hors ligne pour tes amis");
            const float sy = 202 + rise;
            if (st) {
                float tx = nx + 2;
                if (dot) {
                    const float dx = nx + 8, dy = sy + 11, b2 = c.r == ps2::kFaint.r ? 1.0f : 0.75f + 0.25f * sinf(now * 1.55f);
                    ui.Light(dx - 13, dy - 13, dx + 13, dy + 13, ps2::A(c, 0.6f * al * b2), true);
                    ui.Shape(dx - 4, dy - 4, dx + 4, dy + 4, 0, 0, 8, 8, ps2::A(ps2::Mix(c, ps2::kChosen, 0.3f), al), 24, 8, 8, 4);
                    tx = nx + 26;
                }
                Text(ui, fonts::kHintBig, st, tx, sy, ps2::A(c.r == ps2::kFaint.r ? ps2::kWords : ps2::Mix(c, ps2::kChosen, 0.25f), 0.95f * al), 0.4f);
            } else if (m.busy) busy::Orbs(ui, nx + 14, sy + 11, 9, al, now);
            char line[96];
            if (m.in && m.user[0]) snprintf(line, sizeof line, lang::T("Nextendo  \xc2\xb7  @%s"), m.user);
            else if (m.in) snprintf(line, sizeof line, "%s", lang::T("Nextendo"));
            else snprintf(line, sizeof line, "%s", lang::T("Pas connecté à Nextendo"));
            Text(ui, fonts::kHintBig, line, nx + 2, 236 + rise, ps2::A(ps2::kFaint, 0.9f * al), 0.4f);
        }
        // ---- the friend code, big, to give ----
        {
            const float cx = 990, ty = 136 + rise;
            if (m.in) {
                char lbl[48]; Upper(lang::T("Ton code ami"), lbl, sizeof lbl);
                ui.TextCentered(fonts::kGameInfo, lbl, cx, ty, 3.0f, ps2::A(ps2::kTitle, 0.9f * al));
                if (m.code[0]) {
                    const float cw = ui.Width(fonts::kGameTitle, m.code, 2.5f);
                    ui.Light(cx - cw / 2 - 60, ty + 14, cx + cw / 2 + 60, ty + 70, ps2::A(ps2::kHalo, 0.26f * al), true);
                    ui.TextCentered(fonts::kGameTitle, m.code, cx, ty + 26, 2.5f, ps2::A(ps2::kChosen, al));
                    ui.TextCentered(fonts::kHint, "Tes amis l'entrent dans Amis, puis Ajouter un ami.", cx, ty + 74, 0.4f, ps2::A(ps2::kFaint, 0.9f * al));
                } else busy::Orbs(ui, cx, ty + 44, 12, al, now);
            } else {
                char lbl[48]; Upper(lang::T("Code ami"), lbl, sizeof lbl);
                ui.TextCentered(fonts::kGameInfo, lbl, cx, ty, 3.0f, ps2::A(ps2::kTitle, 0.6f * al));
                ui.TextCentered(fonts::kHintBig, m.noClient ? "Nextendo n'est pas encore branché sur cette console." : "Connecte-toi à Nextendo pour avoir ton code ami.",
                                cx, ty + 34, 0.3f, ps2::A(ps2::kFaint, 0.9f * al));
            }
        }
        // ---- the choices (left) ----
        {
            const float x0 = kListX0, x1 = kListX1, ty = 296 + rise;
            const char *title = s_users ? "Qui joue ?" : "Profil";
            const float tw = ui.Width(fonts::kSetSel, lang::T(title), 0.3f);
            ui.Light(x0 - 24, ty - 10, x0 + tw + 34, ty + 40, ps2::A(ps2::kTitleGlow, 0.14f * al), true);
            Text(ui, fonts::kSetSel, title, x0, ty, ps2::A(ps2::kTitle, al), 0.3f);
            const int n = Lines(), sel = Sel();
            const float kSel = 0.5f + 0.5f * EaseGlide((now - s_moveAt) / Pace(0.35f));
            const char *desc = nullptr; char descBuf[128];
            for (int i = 0; i < n; i++) {
                const float cy = kListTop + i * kRowH + rise + 14;
                const bool on = i == sel;
                const float chosenY = s_selY + rise + 14;
                if (on) {
                    float lw = 0;
                    if (s_users) lw = ui.Width(fonts::kSetOpt, Profiles::NameAt(i), 0.3f) + 46;
                    ui.Chosen(x0 - 22, x1 + 22, chosenY, x0 + (s_users ? 0 : 0), lw > 0 ? lw : 200, kSel * al);
                }
                const Rgba lc = ps2::Line(on ? 1.0f : 0.0f, al);
                if (s_users) {
                    const int us = Profiles::SlotAt(i);
                    if (us >= 0 && s_avatarsOk) Profiles::DrawAvatar(ui, us, x0 + 16, cy, 17, al);
                    else ui.Shape(x0 - 1, cy - 17, x0 + 33, cy + 17, 0, 0, 34, 34, Settings::Accent(0.3f * al), 24, 34, 34, 17);
                    Text(ui, fonts::kSetOpt, Profiles::NameAt(i), x0 + 46, cy - 12, lc, 0.3f);
                    if (i == Profiles::ActiveIndex()) {
                        const float w = ui.Width(fonts::kHint, lang::T("joue"), 2);
                        ui.TextCentered(fonts::kHint, "joue", x1 - w / 2, cy - 9, 2, ps2::A(ps2::kChoice, 0.9f * al));
                    }
                    if (s_nhit < 12) s_hit[s_nhit++] = { i, x0 - 22, cy - 26, x1 + 22, cy + 26 };
                    continue;
                }
                const int r = s_rows[i];
                const char *label = "", *value = nullptr; bool arrows = false; Rgba vc = ps2::A(ps2::kChoice, 0.9f * al);
                switch (r) {
                    case kRUser: {
                        label = "Changer de profil";
                        static char cnt[32];
                        const int c = Profiles::Count();
                        if (c == 1) snprintf(cnt, sizeof cnt, "%s", lang::T("1 profil")); else snprintf(cnt, sizeof cnt, lang::T("%d profils"), c);
                        value = cnt; vc = ps2::A(ps2::kFaint, 0.85f * al);
                        if (on) desc = "Choisis qui joue : les jeux démarrent avec ce profil.";
                        break;
                    }
                    case kRImage:
                        label = "Image du profil"; value = s_imgNextendo ? "Nextendo" : "Switch"; arrows = true;
                        if (on) desc = "En haut à gauche et sur cette page.";
                        break;
                    case kRStatus:
                        label = "Statut pour tes amis";
                        if (m.canHide == 0) { value = "Reconnecte-toi"; vc = ps2::A(ps2::kFaint, 0.85f * al); if (on) desc = "Ta connexion date d'avant ce réglage : reconnecte-toi à Nextendo pour l'autoriser."; }
                        else { value = m.hidden == 1 ? "Invisible" : (m.hidden == 0 ? "Visible" : "\xe2\x80\xa6"); arrows = m.hidden >= 0;
                               if (on) desc = m.hidden == 1 ? "Tu apparais hors ligne pour tes amis." : "Tes amis Nextendo voient quand tu es en ligne et à quoi tu joues."; }
                        break;
                    case kRNext:
                        label = m.in ? "Se déconnecter de Nextendo" : "Se connecter à Nextendo";
                        if (on) desc = m.in ? "Brume oublie ta connexion sur cette console. Tes amis restent sur ton compte."
                                            : (m.noClient ? "La page Amis explique comment brancher Nextendo." : "Sur la page Amis : avec le navigateur de la console ou ton téléphone.");
                        break;
                    case kRSwitch:
                        label = "Gérer sur la Switch";
                        if (on) desc = "Icône, pseudo, nouvel utilisateur : les écrans de la Switch.";
                        break;
                }
                Text(ui, fonts::kSetOpt, label, x0, cy - 12, lc, 0.3f);
                if (value) {
                    const float vw = ui.Width(fonts::kSetOpt, lang::T(value), 0.3f), vx = x1 - (arrows ? 18 : 0) - vw;
                    ui.TextCentered(fonts::kSetOpt, value, vx + vw / 2, cy - 12, 0.3f, on ? ps2::A(ps2::kChosen, al) : vc);
                    if (arrows && on) {
                        ui.Arrow(vx - 14, cy, 5.0f, 0, ps2::A(ps2::kChoice, 0.9f * al));
                        ui.Arrow(x1 - 6, cy, 5.0f, 1, ps2::A(ps2::kChoice, 0.9f * al));
                    }
                }
                if (s_nhit < 12) s_hit[s_nhit++] = { i, x0 - 22, cy - 26, x1 + 22, cy + 26 };
            }
            if (s_users) desc = "Les jeux démarrent avec le profil choisi.";
            if (desc) {
                snprintf(descBuf, sizeof descBuf, "%s", lang::T(desc));
                float y = kListTop + n * kRowH + rise + 8;
                // (two lines at most, cut at a space)
                const char *t = descBuf; char line[128];
                for (int l = 0; l < 2 && *t; l++) {
                    int best = 0, k = 0;
                    while (t[k] && k < (int)sizeof line - 1) {
                        while (t[k] && t[k] != ' ' && k < (int)sizeof line - 1) k++;
                        memcpy(line, t, (size_t)k); line[k] = 0;
                        if (ui.Width(fonts::kHint, line, 0.4f) > x1 - x0 + 20 && best > 0) break;
                        best = k;
                        if (!t[k]) break;
                        k++;
                    }
                    if (!best) best = k;
                    memcpy(line, t, (size_t)best); line[best] = 0;
                    float px = x0;
                    for (const char *q = line; *q;) px += ui.Glyph(fonts::kHint, NextCodepoint(q), px, y, ps2::A(ps2::kFaint, 0.9f * al), 0, 0, 0, 0) + 0.4f;
                    y += 22; t += best;
                    while (*t == ' ') t++;
                }
            }
        }
        // ---- the play time (right), from the console's own play log ----
        {
            const float x0 = 652, x1 = 1172, ty = 296 + rise;
            const char *title = "Temps de jeu";
            const float tw = ui.Width(fonts::kSetSel, lang::T(title), 0.3f);
            ui.Light(x0 - 24, ty - 10, x0 + tw + 34, ty + 40, ps2::A(ps2::kTitleGlow, 0.14f * al), true);
            Text(ui, fonts::kSetSel, title, x0, ty, ps2::A(ps2::kTitle, al), 0.3f);
            if (s_state == 1 && s_st.played == 0) {
                busy::Orbs(ui, x0 + 20, ty + 82, 11, al, now);
                Text(ui, fonts::kHintBig, "Brume lit le journal de jeu de la console…", x0 + 46, ty + 71, ps2::A(ps2::kFaint, 0.9f * al), 0.3f);
            } else if (s_state == 2 && !s_st.ok) {
                Text(ui, fonts::kHintBig, "Le journal de jeu de la console ne répond pas.", x0, ty + 66, ps2::A(ps2::kFaint, 0.9f * al), 0.3f);
            } else if (s_state == 2 && s_st.played == 0) {
                Text(ui, fonts::kHintBig, "Aucun jeu lancé avec ce profil pour l'instant.", x0, ty + 66, ps2::A(ps2::kFaint, 0.9f * al), 0.3f);
            } else if (s_state >= 1) {
                char h[32]; Hours(s_st.total, h, sizeof h);
                const float hw = Text(ui, fonts::kPageTitle, h, x0, ty + 50, ps2::A(ps2::kChosen, al), 1.5f);
                char sub[64];
                if (s_st.played == 1) snprintf(sub, sizeof sub, "%s", lang::T("sur 1 jeu"));
                else snprintf(sub, sizeof sub, lang::T("sur %d jeux"), s_st.played);
                Text(ui, fonts::kHintBig, sub, x0 + hw + 16, ty + 68, ps2::A(ps2::kFaint, 0.95f * al), 0.4f);
                char lbl[48]; Upper(lang::T("Les plus joués"), lbl, sizeof lbl);
                Text(ui, fonts::kGameInfo, lbl, x0, ty + 116, ps2::A(ps2::kFaint, 0.9f * al), 3.0f);
                const float maxMin = s_st.ntop ? (float)(s_st.top[0].min ? s_st.top[0].min : 1) : 1.0f;
                for (int i = 0; i < s_st.ntop; i++) {
                    const float cy = ty + 160 + i * 52;
                    const float icx = x0 + 20;
                    if (!social::DrawGameIcon(ui, s_st.top[i].tid, icx, cy, 20, al))
                        ui.Light(icx - 20, cy - 20, icx + 20, cy + 20, ps2::A(ps2::kHalo, 0.3f * al), true);
                    char nm[72]; Cut(s_topName[i], nm, sizeof nm, 28);
                    Text(ui, fonts::kSetOpt, nm, x0 + 54, cy - 18, ps2::A(i == 0 ? ps2::kChosen : ps2::kChoice, 0.95f * al), 0.2f);
                    char t[32]; Hours(s_st.top[i].min, t, sizeof t);
                    const float w = ui.Width(fonts::kHintBig, t, 0.4f);
                    ui.TextCentered(fonts::kHintBig, t, x1 - w / 2, cy - 16, 0.4f, ps2::A(ps2::kWords, 0.9f * al));
                    const float f = s_st.top[i].min / maxMin, bx1 = x0 + 54 + (x1 - x0 - 54) * f;   // how much, as a soft light
                    ui.Light(x0 + 50, cy + 7, bx1 + 6, cy + 17, ps2::A(ps2::Mix(ac, ps2::kHalo, 0.4f), (0.30f - 0.06f * i) * al));
                }
                if (s_st.lastTid && s_lastName[0]) {
                    char w[48]; When(s_st.lastTs, w, sizeof w);
                    char nm[72]; Cut(s_lastName, nm, sizeof nm, 30);
                    char line[160]; snprintf(line, sizeof line, lang::T("Dernier joué : %s, %s"), nm, w);
                    Text(ui, fonts::kHintBig, line, x0, ty + 160 + 3 * 52 + 6, ps2::A(ps2::kFaint, 0.95f * al), 0.3f);
                }
            }
        }
        // ---- the hints, as everywhere: the button's letter in colour, then its word ----
        {
            const float hc = 720 - 28 - 13.5f;
            float hx = 46;
            auto Hint = [&](char btn, const char *label) {
                const Rgba bc = btn == 'B' ? ps2::kPink : (btn == 'Y' ? ps2::kGreen : ps2::kChoice);
                hx += ui.Glyph(fonts::kButton, (uint32_t)btn, hx, hc - 10, ps2::A(bc, al), 0, 0, 0, 0) + 10;
                for (const char *q = lang::T(label); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * al), 0, 0, 0, 0) + 0.5f;
                hx += 30;
            };
            const int r = !s_users && s_sel < s_nrows ? s_rows[s_sel] : -1;
            if (s_users) { Hint('A', "Il joue"); Hint('B', "Retour"); }
            else {
                Hint('A', r == kRImage || r == kRStatus ? "Changer" : "Choisir");
                Hint('B', "Fermer");
                Hint('Y', "Actualiser");
            }
        }
    }

} // namespace brume::mypage
