// Brume - v46: the profiles. See Profiles.hpp.
#include "Profiles.hpp"
#include "Lang.hpp"
#include "Shelf.hpp"
#include "Ease.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Settings.hpp"
#include "Worker.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace la {                             // the loader's library applet helpers (exported C++ symbols)
    Result Start(AppletId id, s32 version, const void *a, size_t na);
}

namespace brume {

    bool DecodeJpegSquare(const void *data, int bytes, uint8_t *out, int size);   // Jpeg.cpp

    void (*Profiles::s_toast)(float, const char *) = nullptr;
    void (*Profiles::s_log)(const char *) = nullptr;
    Profiles::User Profiles::s_user[Profiles::kMax];
    int Profiles::s_n = 0, Profiles::s_active = -1;

    namespace {
        constexpr const char *kFile = "sdmc:/brume/profil.txt";
        constexpr int kW = Profiles::kSlot * Profiles::kMax, kH = Profiles::kSlot;   // the strip: 1024 x 128
        constexpr uint32_t kBcBytes = (kW / 4) * (kH / 4) * 8, kJpgMax = 0x20000;
        uint8_t *s_bc = nullptr;                     // its BC1 (64 KB): allocated by Load, freed once uploaded (no memory kept)
        // the panel (under the bar's profile pill)
        constexpr float kPX = 46, kPY = 96, kPW = 420, kRowH = 64, kHead = 60, kFoot = 46;
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        bool Same(const AccountUid &a, const AccountUid &b) { return a.uid[0] == b.uid[0] && a.uid[1] == b.uid[1]; }
        AccountUid Fallback() {                      // last opened, else the console's first profile
            AccountUid uid = {};
            if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid)) return uid;
            AccountUid all[ACC_USER_LIST_SIZE]; s32 n = 0;
            if (R_SUCCEEDED(accountListAllUsers(all, ACC_USER_LIST_SIZE, &n)))
                for (int i = 0; i < n && i < ACC_USER_LIST_SIZE; i++) if (accountUidIsValid(&all[i])) return all[i];
            return uid;
        }
        void Text(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c) {
            for (const char *q = lang::T(s); *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0);
        }
    }

    bool Profiles::Init(Gpu &gpu, uint32_t descriptor) {
        uint32_t align = 1;
        const uint32_t bytes = gpu.ImageBytes(kW, kH, DkImageFormat_RGB_BC1, 1, &align);
        const uint32_t off = gpu.ReserveImage(bytes, align);
        if (off == UINT32_MAX || descriptor >= 64) return false;
        m_ok = gpu.PlaceTexture(m_tex, off, kW, kH, DkImageFormat_RGB_BC1, 1, descriptor);
        return m_ok;
    }

    // v99 (perf): the users read off the frame. perf.log: opening the panel took 45 ms in one frame (4 avatars: the account
    // service's JPEGs decoded, compressed to BC1), and again after each Switch screen (icon, nickname, new user). Now it is
    // a background job (Worker.hpp) that fills its own copy; the frame takes it at once when it is over (Update): until then
    // the panel shows the users as they were (the same, almost always). At boot it is still done at once (no frame yet).
    namespace {
        struct Gathered {
            Profiles::UserCopy user[Profiles::kMax];
            int n = 0;
            uint8_t *bc = nullptr;                   // its BC1 strip (nullptr: no avatars), handed to the frame
            AccountUid want = {};                    // the active one written in our file (invalid: none)
        };
        Gathered s_got;
        Worker s_work;
        bool s_again = false, s_wantStrip = false;   // (a load asked while one runs: one more after it)
        void Gather(void *) {
            Gathered &g = s_got;
            g.n = 0; g.want = AccountUid{};
            AccountUid all[ACC_USER_LIST_SIZE]; s32 n = 0;
            if (R_FAILED(accountListAllUsers(all, ACC_USER_LIST_SIZE, &n)) || n < 0) n = 0;
            // v56 (review): no 512 KB strip any more - each avatar is compressed straight into its place of the BC1 strip
            g.bc = s_wantStrip ? (uint8_t *)calloc(1, kBcBytes) : nullptr;
            uint8_t *jpg = g.bc ? (uint8_t *)malloc(kJpgMax) : nullptr, *rgba = g.bc ? (uint8_t *)malloc((size_t)Profiles::kSlot * Profiles::kSlot * 4) : nullptr;
            uint8_t *one = g.bc ? (uint8_t *)malloc((Profiles::kSlot / 4) * (Profiles::kSlot / 4) * 8) : nullptr;
            const bool strip = jpg && rgba && one;
            for (int i = 0; i < n && i < ACC_USER_LIST_SIZE && g.n < Profiles::kMax; i++) {
                if (!accountUidIsValid(&all[i])) continue;
                Profiles::UserCopy &u = g.user[g.n];
                u.uid = all[i]; u.icon = false;
                snprintf(u.nick, sizeof u.nick, lang::T("Profil %d"), g.n + 1);
                AccountProfile p;
                if (R_SUCCEEDED(accountGetProfile(&p, u.uid))) {
                    AccountProfileBase base;
                    memset(&base, 0, sizeof base);
                    if (R_SUCCEEDED(accountProfileGet(&p, nullptr, &base)) && base.nickname[0]) {
                        int k = 0;
                        while (k < (int)sizeof base.nickname && k + 1 < (int)sizeof u.nick && base.nickname[k]) { u.nick[k] = base.nickname[k]; k++; }
                        u.nick[k] = 0;
                    }
                    u32 sz = 0, got = 0;
                    if (strip && R_SUCCEEDED(accountProfileGetImageSize(&p, &sz)) && sz > 0 && sz <= kJpgMax &&
                        R_SUCCEEDED(accountProfileLoadImage(&p, jpg, kJpgMax, &got)) && got > 0 && got <= kJpgMax &&
                        DecodeJpegSquare(jpg, (int)got, rgba, Profiles::kSlot)) {
                        Shelf::EncodeBC1(rgba, Profiles::kSlot, Profiles::kSlot, one);   // 32 x 32 blocks, then each row of blocks into the strip
                        for (int by = 0; by < Profiles::kSlot / 4; by++)
                            memcpy(g.bc + ((size_t)by * (kW / 4) + (size_t)g.n * (Profiles::kSlot / 4)) * 8, one + (size_t)by * (Profiles::kSlot / 4) * 8, (Profiles::kSlot / 4) * 8);
                        u.icon = true;
                    }
                    accountProfileClose(&p);
                }
                g.n++;
            }
            free(jpg); free(rgba); free(one);
            if (!strip) { free(g.bc); g.bc = nullptr; }
            if (FILE *f = fopen(kFile, "r")) {           // the active one written in our file
                unsigned long long a = 0, b = 0;
                if (fscanf(f, "%16llx%16llx", &a, &b) == 2) { g.want.uid[0] = a; g.want.uid[1] = b; }
                fclose(f);
            }
        }
    }

    void Profiles::Take(bool first) {
        Gathered &g = s_got;
        // the one who plays: the one chosen here if still there (our file may be older than a choice made meanwhile),
        // else our file, else the last opened, else the first
        const AccountUid keep = !first && s_active >= 0 && s_active < s_n ? s_user[s_active].uid : AccountUid{};
        s_n = g.n;
        for (int i = 0; i < s_n; i++) { s_user[i].uid = g.user[i].uid; s_user[i].icon = g.user[i].icon; memcpy(s_user[i].nick, g.user[i].nick, sizeof s_user[i].nick); }
        if (g.bc) { free(s_bc); s_bc = g.bc; g.bc = nullptr; m_dirty = true; }
        s_active = s_n ? 0 : -1;
        bool found = false;
        for (int i = 0; i < s_n && accountUidIsValid(&keep); i++) if (Same(s_user[i].uid, keep)) { s_active = i; found = true; }
        if (!found) {
            AccountUid want = g.want;
            if (!accountUidIsValid(&want)) want = Fallback();
            for (int i = 0; i < s_n; i++) if (Same(s_user[i].uid, want)) s_active = i;
        }
        if (m_sel >= s_n + 1) m_sel = s_n;
        if (s_log) { char m[96]; snprintf(m, sizeof m, "profils : %d utilisateur(s), actif = %s", s_n, ActiveName()); s_log(m); }
    }

    void Profiles::Load() {                          // at once (boot)
        if (!s_work.Idle()) { s_work.Wait(); free(s_got.bc); s_got.bc = nullptr; }
        s_wantStrip = m_ok;
        Gather(nullptr);
        Take(true);
    }

    void Profiles::LoadSoon() {                      // v99: in the background (the panel, after a Switch screen)
        if (!s_work.Idle()) { s_again = true; return; }
        s_wantStrip = m_ok;
        s_work.Start(Gather, nullptr, 0x8000);
    }

    void Profiles::Shutdown() {                      // (the menu closes: the job ends, its result is dropped)
        s_work.Wait();
        free(s_got.bc); s_got.bc = nullptr;
    }

    void Profiles::Stream(Gpu &gpu) {
        if (!m_dirty || !m_ok || !s_bc) return;
        if (gpu.StreamTexture(m_tex, s_bc, kBcBytes, 1)) { m_dirty = false; m_uploaded = true; free(s_bc); s_bc = nullptr; }
    }

    void Profiles::SaveActive() {
        if (s_active < 0 || s_active >= s_n) return;
        if (FILE *f = fopen(kFile, "w")) {
            fprintf(f, "%016llX%016llX\n", (unsigned long long)s_user[s_active].uid.uid[0], (unsigned long long)s_user[s_active].uid.uid[1]);
            fclose(f);
        }
    }

    AccountUid Profiles::Active() { return s_active >= 0 && s_active < s_n ? s_user[s_active].uid : Fallback(); }
    const char *Profiles::ActiveName() { return s_active >= 0 && s_active < s_n ? s_user[s_active].nick : "Profil"; }
    int Profiles::ActiveSlot() { return s_active >= 0 && s_active < s_n && s_user[s_active].icon ? s_active : -1; }
    AccountUid Profiles::UidAt(int i) { return i >= 0 && i < s_n ? s_user[i].uid : Fallback(); }
    const char *Profiles::NameAt(int i) { return i >= 0 && i < s_n ? s_user[i].nick : ""; }
    int Profiles::SlotAt(int i) { return i >= 0 && i < s_n && s_user[i].icon ? i : -1; }
    void Profiles::Choose(int i) { if (i >= 0 && i < s_n && i != s_active) { s_active = i; SaveActive(); } }

    AccountUid Profiles::Ask() {
        PselUserSelectionSettings st;
        memset(&st, 0, sizeof st);
        AccountUid out = {};
        haptic::Stop();
        appletUnlockForeground();
        if (R_FAILED(pselShowUserSelector(&out, &st))) memset(&out, 0, sizeof out);
        return out;
    }

    void Profiles::DrawAvatar(Ui &ui, int slot, float cx, float cy, float r, float alpha) {
        if (slot < 0 || slot >= kMax || alpha <= 0.002f) return;
        const float u0 = (float)slot / kMax, u1 = (float)(slot + 1) / kMax;
        ui.Shape(cx - r, cy - r, cx + r, cy + r, u0, 0, u1, 1, Rgba{ 1, 1, 1, alpha }, 26, (u0 + u1) * 0.5f, (u1 - u0) * 0.5f, 0);
    }

    // ---- the panel ----
    void Profiles::OpenPanel(float now) {
        if (m_open) return;
        LoadSoon();                                  // (fresh: someone may have changed an icon in a game; v99: off the frame)
        m_open = true; m_openAt = now;
        m_sel = s_active >= 0 ? s_active : 0;
        m_selY = kPY + kHead + m_sel * kRowH; m_selV = 0;
        audio::Play(audio::Tick2400, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, -0.5f, -0.2f, 0.5f, 0.35f);
        haptic::Tick(0.6f);
    }
    void Profiles::Close(float now) {
        if (!m_open) return;
        (void)now;
        m_open = false;
        audio::Play(audio::Tick1700, 0, 1.0f);
        haptic::Tick(0.5f);
    }
    void Profiles::Move(int step, float now) {
        if (!m_open) return;
        const int to = m_sel + step;
        if (to < 0 || to > s_n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
        m_sel = to; m_moveAt = now;
        audio::Play(audio::Deplacement, -0.6f, 1.0f);
        haptic::Tick(0.6f);
    }
    void Profiles::Refresh(float now) { (void)now; LoadSoon(); }   // v99: off the frame

    void Profiles::PressA(float now) {
        if (!m_open) return;
        if (m_sel >= s_n) {                          // « Ajouter un utilisateur »: the Switch's own screen
            audio::Play(audio::Tick2400, 0, 1.0f);
            haptic::Stop();
            appletUnlockForeground();
            const Result rc = pselShowUserCreator();
            if (R_FAILED(rc) && s_toast) { char m[96]; snprintf(m, sizeof m, lang::T("La cr\xc3\xa9" "ation n'a pas pu s'ouvrir (0x%X)"), rc); s_toast(now, m); }
            Refresh(now);
            return;
        }
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Tap(0.7f);
        char m[96];
        if (m_sel != s_active) {
            s_active = m_sel;
            SaveActive();
            for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k, 72, 86), 0.04f - k * 0.008f, true, k * 0.08f);
            snprintf(m, sizeof m, lang::T("C'est %s qui joue maintenant"), s_user[s_active].nick);
        } else snprintf(m, sizeof m, lang::T("%s joue d\xc3\xa9j\xc3\xa0"), s_user[s_active].nick);
        if (s_toast) s_toast(now, m);
    }
    void Profiles::PressX(float now) {                // the Switch's profile page (MyPage) of the chosen one
        if (!m_open || m_sel >= s_n) return;
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Stop();
        FriendsLaArg arg;
        memset(&arg, 0, sizeof arg);
        arg.hdr.type = FriendsLaArgType_ShowMyProfile;
        arg.hdr.uid = s_user[m_sel].uid;
        const Result rc = la::Start(AppletId_LibraryAppletMyPage, 0x10000, &arg, sizeof arg);
        if (R_FAILED(rc) && s_toast) { char m[96]; snprintf(m, sizeof m, lang::T("Le profil n'a pas pu s'ouvrir (0x%X)"), rc); s_toast(now, m); }
    }
    void Profiles::PressY(float now) {                // the icon
        if (!m_open || m_sel >= s_n) return;
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Stop();
        appletUnlockForeground();
        pselShowUserIconEditor(s_user[m_sel].uid);
        Refresh(now);
    }
    void Profiles::PressMinus(float now) {            // the nickname
        if (!m_open || m_sel >= s_n) return;
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Stop();
        appletUnlockForeground();
        pselShowUserNicknameEditor(s_user[m_sel].uid);
        Refresh(now);
    }
    void Profiles::Tap(float x, float y, float now) {
        if (!m_open) return;
        const float top = kPY + kHead;
        if (x < kPX || x > kPX + kPW || y < kPY || y > top + (s_n + 1) * kRowH + kFoot) { Close(now); return; }
        const int r = (int)floorf((y - top) / kRowH);
        if (r < 0 || r > s_n) return;
        if (r == m_sel) PressA(now);
        else { m_sel = r; m_moveAt = now; audio::Play(audio::Deplacement, -0.6f, 1.0f); haptic::Tick(0.6f); }
    }

    void Profiles::Update(float now, float dt) {
        (void)now;
        if (s_work.Done()) { Take(false); if (s_again) { s_again = false; LoadSoon(); } }   // v99: the users read in the background
        Glide(m_k, m_kv, m_open ? 1.0f : 0.0f, dt, 7.0f);
        Glide(m_selY, m_selV, kPY + kHead + m_sel * kRowH, dt, 9.0f);
    }

    void Profiles::DrawUi(Ui &ui, float now) {
        const float k = Clamp01(m_k);
        if (k <= 0.003f) return;
        (void)now;
        const int rows = s_n + 1;
        const float dy = -14 * (1 - k), top = kPY + dy, ph = kHead + rows * kRowH + kFoot, x0 = kPX;
        // v70 (the PS2 look, as Brume Overlay): no glass panel - the title in yellow, the names floating in light blue, the
        // chosen one near-white in a band of light with its halo that glides. v70.5 (Yass: « pas super lisible »): the whole
        // left of the screen goes dark behind it (the consoles and the bright fog no longer shine through the names)
        // v78 (Yass: « on voit pas leur fond… carrée ») - its own surface (night mist, rounded; Ui::Surface) instead of the veil
        // over the whole left of the screen, and it grows out of the profile's pill (top left)
        ui.SetXform(0.94f + 0.06f * k, 90, 46, 90, 46);
        ui.Surface(x0 - 6, top - 4, x0 + kPW + 6, top + ph + 2, k);
        const float tw = ui.Width(fonts::kSetSel, "Profils", 0);
        ui.Light(x0 + 6, top + 4, x0 + 26 + tw + 20, top + 52, ps2::A(ps2::kTitleGlow, 0.16f * k), true);
        Text(ui, fonts::kSetSel, "Profils", x0 + 26, top + 14, ps2::A(ps2::kTitle, k));
        char cnt[32]; snprintf(cnt, sizeof cnt, "%d", s_n);   // how many, right after the title (faint)
        Text(ui, fonts::kGameInfo, cnt, x0 + 26 + tw + 14, top + 26, ps2::A(ps2::kFaint, 0.85f * k));
        // the chosen row: the band of light glides with it, the halo behind its name
        const float sy = m_selY + dy;
        {
            const char *nm = m_sel < s_n ? s_user[m_sel].nick : lang::T("Ajouter un utilisateur");
            ui.Chosen(x0 - 10, x0 + kPW + 10, sy + kRowH * 0.5f, x0 + 26 + 44 + 18, ui.Width(fonts::kSetOpt, nm, 0), k);
        }
        for (int i = 0; i < rows; i++) {
            const float ry = top + kHead + i * kRowH, cy = ry + kRowH * 0.5f;
            const float cx = x0 + 26 + 22;
            const bool sel = i == m_sel;
            if (i < s_n) {
                const User &u = s_user[i];
                if (i == s_active) ui.Shape(cx - 34, cy - 34, cx + 34, cy + 34, -1, -1, 1, 1, Settings::Accent(0.8f * k), 21);   // the one who plays: a halo
                if (u.icon && Avatars()) DrawAvatar(ui, i, cx, cy, 22, k);
                else {
                    ui.Shape(cx - 22, cy - 22, cx + 22, cy + 22, 0, 0, 44, 44, Settings::Accent(0.30f * k), 24, 44, 44, 22);
                    char ini[8] = { 0 };
                    const char *q = u.nick; const uint32_t cp = NextCodepoint(q);
                    if (cp < 0x80) { ini[0] = (char)cp; ui.TextCentered(fonts::kSetOpt, ini, cx, cy - 12, 0, Hex(0xffffff, k)); }
                }
                Text(ui, fonts::kSetOpt, u.nick, cx + 22 + 18, cy - 12, ps2::Line(sel ? 1.0f : 0.0f, k));
                if (i == s_active) ui.TextCentered(fonts::kHint, "joue", x0 + kPW - 44, cy - 9, 2, ps2::A(ps2::kChoice, 0.9f * k));
            } else {                                 // add a user: a « + » in a soft light (no ring)
                ui.Light(cx - 26, cy - 26, cx + 26, cy + 26, ps2::A(ps2::kHalo, 0.35f * k), true);
                ui.TextCentered(fonts::kSetSel, "+", cx, cy - 18, 0, ps2::Line(sel ? 1.0f : 0.0f, k));
                Text(ui, fonts::kSetOpt, "Ajouter un utilisateur", cx + 22 + 18, cy - 12, ps2::Line(sel ? 1.0f : 0.0f, k));
            }
        }
        // the hints, as everywhere (v70): the button's letter in colour, then its word. v70.5: the « − » was missing (that
        // character is not in the hints' font: the small buttons' « - » now)
        {
            static const char kL[4] = { 'A', 'X', 'Y', '-' };
            static const char *const kW[4] = { "Choisir", "Profil", "Ic\xc3\xb4ne", "Pseudo" };
            const int n = m_sel < s_n ? 4 : 1;
            const char *w0 = m_sel < s_n ? kW[0] : "Cr\xc3\xa9" "er un utilisateur";
            float total = 0;
            for (int i = 0; i < n; i++) total += ui.Advance(fonts::kBtnSmall, (uint32_t)kL[i]) + 8 + ui.Width(fonts::kHint, i ? kW[i] : w0, 0) + (i + 1 < n ? 26 : 0);
            float hx = x0 + kPW / 2 - total / 2;
            const float hy = top + ph - 32;
            for (int i = 0; i < n; i++) {
                const Rgba bc = kL[i] == 'Y' ? ps2::kGreen : ps2::kChoice;
                hx += ui.Glyph(fonts::kBtnSmall, (uint32_t)kL[i], hx, hy - 1, ps2::A(bc, k), 0, 0, 0, 0) + 8;
                for (const char *q = lang::T(i ? kW[i] : w0); *q;) hx += ui.Glyph(fonts::kHint, NextCodepoint(q), hx, hy, ps2::A(ps2::kWords, 0.9f * k), 0, 0, 0, 0);
                hx += 26;
            }
        }
        ui.SetZoom(1.0f);
    }

} // namespace brume
