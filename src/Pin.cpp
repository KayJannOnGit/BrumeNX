// Brume - v87: the lock code, like the PS4's. See Pin.hpp.
#include "Pin.hpp"
#include "Lock.hpp"
#include "Settings.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume::pin {

    namespace {
        constexpr const char *kFile = "sdmc:/brume/codes.txt";
        constexpr int kMaxCodes = 16;
        struct Entry { u64 a, b; bool has; uint8_t c[kLen]; };
        Entry s_e[kMaxCodes];
        int   s_n = 0;
        bool  s_loaded = false;
        const char *const kNames[kSyms] = { "haut", "droite", "bas", "gauche", "L", "R", "ZL", "ZR", "X", "Y" };
        // the key's places (relative to its centre): ▲ ▶ ▼ ◀ on the left, L R / ZL ZR in the middle, X and Y on the right
        const float kPos[kSyms][2] = { { -128, -27 }, { -101, 0 }, { -128, 27 }, { -155, 0 },
                                       { -30, -18 }, { 30, -18 }, { -30, 20 }, { 30, 20 },
                                       { 128, -27 }, { 101, 0 } };
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        float EaseOutDots(float t) { const float u = Clamp01((t - 0.3f) / 0.7f); return 1 - (1 - u) * (1 - u); }
        int Find(AccountUid u) { for (int i = 0; i < s_n; i++) if (s_e[i].a == u.uid[0] && s_e[i].b == u.uid[1]) return i; return -1; }
        bool Hex16(const char *s, u64 &out) {
            out = 0;
            for (int i = 0; i < 16; i++) {
                const char c = s[i]; int v;
                if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else return false;
                out = (out << 4) | (u64)v;
            }
            return true;
        }
        bool Write() {
#ifdef BRUME_UISIM
            return true;                                 // (the PC preview keeps the codes in memory: the fake card stays as it is)
#endif
            FILE *f = fopen(kFile, "w");
            if (!f) return false;
            fprintf(f, "# Brume - les codes de verrouillage (un par profil). Supprimer ce fichier = plus aucun code.\n");   // lang:log (never drawn)
            for (int i = 0; i < s_n; i++) {
                fprintf(f, "%016llX%016llX ", (unsigned long long)s_e[i].a, (unsigned long long)s_e[i].b);
                if (s_e[i].has) fprintf(f, "%d%d%d%d\n", s_e[i].c[0], s_e[i].c[1], s_e[i].c[2], s_e[i].c[3]);
                else fprintf(f, "-\n");
            }
            const bool ok = !ferror(f);
            fclose(f);
            return ok;
        }
    }

    int FromButtons(u64 down) {
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) return 0;
        if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) return 1;
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) return 2;
        if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) return 3;
        if (down & HidNpadButton_L) return 4;
        if (down & HidNpadButton_R) return 5;
        if (down & HidNpadButton_ZL) return 6;
        if (down & HidNpadButton_ZR) return 7;
        if (down & HidNpadButton_X) return 8;
        if (down & HidNpadButton_Y) return 9;
        return -1;
    }
    const char *Name(int sym) { return sym >= 0 && sym < kSyms ? kNames[sym] : ""; }

    void Load() {
        s_n = 0; s_loaded = true;
#ifdef BRUME_UISIM
        return;
#endif
        FILE *f = fopen(kFile, "r");
        if (!f) return;
        char line[128];
        while (s_n < kMaxCodes && fgets(line, sizeof line, f)) {
            if (strlen(line) < 34 || line[32] != ' ') continue;     // (a comment, a broken line: skipped)
            Entry e = {};
            if (!Hex16(line, e.a) || !Hex16(line + 16, e.b) || (!e.a && !e.b)) continue;
            const char *c = line + 33;
            if (c[0] == '-') e.has = false;
            else {
                bool good = true;
                for (int k = 0; k < kLen; k++) good = good && c[k] >= '0' && c[k] <= '9';
                if (!good) continue;
                for (int k = 0; k < kLen; k++) e.c[k] = (uint8_t)(c[k] - '0');
                e.has = true;
            }
            bool dup = false;
            for (int i = 0; i < s_n; i++) dup = dup || (s_e[i].a == e.a && s_e[i].b == e.b);
            if (!dup) s_e[s_n++] = e;
        }
        fclose(f);
    }

    bool Own(AccountUid uid) { if (!s_loaded) Load(); return Find(uid) >= 0; }
    bool Get(AccountUid uid, uint8_t out[kLen]) {
        if (!s_loaded) Load();
        const int i = Find(uid);
        if (i >= 0) { if (!s_e[i].has) return false; memcpy(out, s_e[i].c, kLen); return true; }
        if (!Lock::s_codeOn) return false;               // the old global code (v44, reglages.ini codeseq=): every profile without its own
        for (int k = 0; k < kLen; k++) out[k] = Lock::s_code[k] < kSyms ? Lock::s_code[k] : 0;
        return true;
    }
    bool Set(AccountUid uid, const uint8_t *code) {
        if (!s_loaded) Load();
        int i = Find(uid);
        if (i < 0) {
            if (s_n >= kMaxCodes) return false;
            i = s_n++;
            s_e[i].a = uid.uid[0]; s_e[i].b = uid.uid[1];
        }
        s_e[i].has = code != nullptr;
        if (code) for (int k = 0; k < kLen; k++) s_e[i].c[k] = code[k] < kSyms ? code[k] : 0;
        return Write();
    }

    void DrawDots(Ui &ui, float cx, float cy, int n, const float *at, float shake, int state, float a, float now) {
        if (a <= 0.003f) return;
        for (int i = 0; i < kLen; i++) {
            const float x = cx + (i - 1.5f) * 40 + shake, y = cy;
            float s = 1;
            const float kt = at ? (now - at[i]) / 0.42f : 1.0f;     // the pop: .5 -> 1.4 -> 1 (it fills with light)
            if (i < n && kt >= 0 && kt < 1) s = kt < 0.3f ? 0.5f + 0.9f * (kt / 0.3f) : 1.4f - 0.4f * EaseOutDots(kt);
            const float r = 7.0f * s;
            if (state == 2) {                                        // wrong: pink, dimmer
                ui.Light(x - 20, y - 20, x + 20, y + 20, ps2::A(ps2::kPink, 0.30f * a), true);
                ui.Shape(x - r, y - r, x + r, y + r, 0, 0, 2 * r, 2 * r, ps2::A(ps2::kPink, 0.85f * a), 24, 2 * r, 2 * r, r);
            } else if (i < n || state == 1) {                        // filled: a soft light, near-white (right: brighter, wider)
                const float g = state == 1 ? 30.0f : 22.0f;
                ui.Light(x - g * s, y - g * s, x + g * s, y + g * s, Settings::Accent((state == 1 ? 1.0f : 0.75f) * a), true);
                ui.Shape(x - r, y - r, x + r, y + r, 0, 0, 2 * r, 2 * r, ps2::A(ps2::kChosen, a), 24, 2 * r, 2 * r, r);
            } else {                                                 // empty: a faint ring of light
                ui.Shape(x - 7, y - 7, x + 7, y + 7, -7, -7, 7, 7, ps2::A(ps2::kChoice, 0.42f * a * (0.85f + 0.15f * sinf(now * 2.2f + i))), 16, 1.5f);
            }
        }
    }

    void DrawPad(Ui &ui, float cx, float cy, float a, int lit, float litAt, float now) {
        if (a <= 0.003f) return;
        const float since = now - litAt;
        // A and B's places, faint: the diamond of the right buttons (they confirm / go back, they are not in the code)
        ui.Shape(cx + 155 - 3, cy - 3, cx + 155 + 3, cy + 3, 0, 0, 6, 6, ps2::A(ps2::kFaint, 0.35f * a), 24, 6, 6, 3);
        ui.Shape(cx + 128 - 3, cy + 27 - 3, cx + 128 + 3, cy + 27 + 3, 0, 0, 6, 6, ps2::A(ps2::kFaint, 0.35f * a), 24, 6, 6, 3);
        for (int s = 0; s < kSyms; s++) {
            const float x = cx + kPos[s][0], y = cy + kPos[s][1];
            const float hot = s == lit && since >= 0 && since < 0.5f ? 1 - since / 0.5f : 0.0f;
            const float k = 0.62f + 0.38f * hot, pop = 1 + 0.22f * hot * hot;
            ui.Light(x - 19 * pop, y - 19 * pop, x + 19 * pop, y + 19 * pop, ps2::A(ps2::kHalo, (0.16f + 0.5f * hot) * a), true);
            const Rgba c = ps2::A(ps2::Mix(ps2::kChoice, ps2::kChosen, hot), k * a);
            if (s < 4) {                                             // the directions: small solid triangles (Ui::Arrow: 0 left, 1 right, 2 up, 3 down)
                static const int kDir[4] = { 2, 1, 3, 0 };
                ui.Arrow(x, y, 6.5f * pop, kDir[s], c);
            } else if (s == 6 || s == 7) {                           // ZL, ZR: the small letters (the big font has no Z)
                const char *t = s == 6 ? "ZL" : "ZR";
                ui.TextCentered(fonts::kBtnSmall, t, x, y - 7, 0.5f, c);
            } else {
                const uint32_t L = (uint32_t)(s == 4 ? 'L' : s == 5 ? 'R' : s == 8 ? 'X' : 'Y');
                const float w = ui.Advance(fonts::kButton, L);
                ui.Glyph(fonts::kButton, L, x - w * 0.5f, y - 10, s == 9 ? ps2::A(ps2::Mix(ps2::kGreen, ps2::kChosen, hot), k * a) : c, 0, 0, 0, 0);
            }
        }
    }

    int PadHit(float cx, float cy, float x, float y) {
        int best = -1; float bd = 24 * 24;
        for (int s = 0; s < kSyms; s++) {
            const float dx = x - (cx + kPos[s][0]), dy = y - (cy + kPos[s][1]), d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = s; }
        }
        return best;
    }

}
