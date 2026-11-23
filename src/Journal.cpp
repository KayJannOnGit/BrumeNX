// Brume - v85: Réglages > Système > Journal de Brume. See Journal.hpp.
#include "Journal.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace brume::journal {

    namespace {
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        constexpr const char *kBrumeLog = "sdmc:/brume/perf.log", *kOverlayLog = "sdmc:/brume/overlay/journal.txt";
        constexpr const char *kStart = "=== Brume : demarrage ===";   // (Entry writes it at each start)
        constexpr int kMax = 60, kMaxOverlay = 12, kText = 164;       // lines kept (the overlay's at most 12 of them), their size
        constexpr int kTailBrume = 32 * 1024, kTailOverlay = 8 * 1024; // the end of each file only (perf.log grows: never all of it)
        // a line: its text, how many times it came (the same one again is counted, not repeated), the start it came from
        // (0 this one, 1 the one before...; -1 unknown: the overlay's), its source (0 Brume, 1 the overlay), error or warning
        struct Line { char text[kText]; uint16_t count; int8_t boot; uint8_t src, err; };
        Line *s_l = nullptr;
        int   s_n = 0, s_nb = 0;              // all the lines / Brume's (first in s_l, the overlay's after them)
        bool  s_any = false;                  // a journal was found on the card
        bool  s_open = false, s_drawn = false;
        float s_now = 0, s_show = 0, s_showV = 0, s_flash = 0;
        int   s_sel = 0, s_notch = 0, s_dragFrom = 0;
        float s_pos = 0, s_vel = 0, s_bump = 0, s_bumpV = 0, s_gap = 29, s_gapV = 0, s_gapTo = 29;
        bool  s_quiet = true;
        int   s_heldDir = 0; float s_heldSince = 0, s_nextRep = 0, s_tickAt = -100;
        bool  s_xmb = false;

        // ---- the sounds: every one on a note of the chord sounding now (as Date et heure's pages) ----
        float At(int midi, int ref) { return exp2f((midi - ref) / 12.0f); }
        void Tick(int k, float pan) {
            if (s_now - s_tickAt < 0.032f) return;
            s_tickAt = s_now;
            const int note = audio::HarmonyNote(k & 3, 76, 88);
            if (audio::Has(audio::PopMove)) audio::Play(audio::PopMove, pan, 0.42f, At(note, 74));
            else audio::Play(audio::Deplacement, pan, 0.55f);
            haptic::Tick(0.35f);
        }
        void SndEnd(float pan) { audio::Thock(pan, 0.8f, 0.05f); haptic::Tick(0.3f); }
        void SndOpen() {
            audio::PlaySweep(audio::Whoosh, 0.35f, -0.15f, 0.42f, 0.45f);
            audio::Bell(audio::HarmonyNote(2, 76, 88), 0.012f, true, 0.07f);
        }
        void SndBack() { audio::Play(audio::Tick1700, 0, 1.0f); audio::PlaySweep(audio::Whoosh, -0.15f, 0.35f, 0.38f, 0.32f); }
        void SndOk() {
            if (audio::Has(audio::PopOk)) {
                audio::Play(audio::PopOk, -0.1f, 1.0f, At(audio::HarmonyNote(0, 69, 80), 74));
                audio::Play(audio::PopOk, 0.1f, 0.6f, At(audio::HarmonyNote(2, 74, 85), 74), 0.07f);
            } else { audio::Play(audio::Tick2400, 0, 1.0f); audio::Bell(audio::HarmonyNote(0, 74, 86), 0.02f, true); }
            haptic::Tap(0.6f);
        }

        // ---- reading the journals (defensive: bounded, cleaned, never trusted) ----
        char Low(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
        bool Has(const char *s, const char *w) {              // a word inside (any case; the logs are written without accents)
            for (; *s; s++) {
                int i = 0;
                while (w[i] && s[i] && Low(s[i]) == w[i]) i++;
                if (!w[i]) return true;
            }
            return false;
        }
        bool BadCode(const char *s) {                          // « rc=0x2a8 », « (0x4b59) »: a result that is not 0
            for (const char *p = s; p[0] && p[1]; p++) {
                if (p[0] != '0' || Low(p[1]) != 'x') continue;
                if (p > s && p[-1] != '=' && p[-1] != '(' && p[-1] != ' ') continue;   // (« 1280x720 » is not a code)
                bool hex = false, nz = false;
                for (const char *q = p + 2; (*q >= '0' && *q <= '9') || (Low(*q) >= 'a' && Low(*q) <= 'f'); q++) { hex = true; nz = nz || *q != '0'; }
                if (hex && nz) return true;
            }
            return false;
        }
        int Severity(const char *s) {                          // 2 an error, 1 a warning, 0 an ordinary line
            if (s[0] == 't' && s[1] == '=') return 0;          // (the frame times, every 2 s)
            static const char *const kErr[] = { "erreur", "error", "echec", "impossible", "crash", "plant", "securite" };
            static const char *const kWarn[] = { "illisible", "introuvable", "refus", "indisponible", "incomplet", "pas de place",   // lang:log (the logs' words, never drawn)
                                                 "pas de memoire", "manque", "absente", "pas reglee", "bloque" };   // lang:log
            for (const char *w : kErr) if (Has(s, w)) return 2;
            for (const char *w : kWarn) if (Has(s, w)) return 1;
            return BadCode(s) ? 1 : 0;
        }
        // the line as printable text: ASCII, or whole UTF-8 letters; a control becomes a space, a broken byte « ? »; cut to fit
        void Clean(const char *s, int len, char *out, int cap) {
            int o = 0, i = 0;
            while (i < len && o < cap - 8) {
                const unsigned char c = (unsigned char)s[i];
                if (c >= 0x20 && c < 0x7F) { out[o++] = (char)c; i++; continue; }
                if (c < 0x20 || c == 0x7F) { out[o++] = ' '; i++; continue; }
                const int n = c >= 0xC2 && c <= 0xDF ? 2 : (c >= 0xE0 && c <= 0xEF ? 3 : (c >= 0xF0 && c <= 0xF4 ? 4 : 0));
                bool ok = n > 0 && i + n <= len;
                for (int k = 1; ok && k < n; k++) ok = ((unsigned char)s[i + k] & 0xC0) == 0x80;
                if (ok) { memcpy(out + o, s + i, (size_t)n); o += n; i += n; }
                else { out[o++] = '?'; i++; }
            }
            while (o > 0 && out[o - 1] == ' ') o--;
            if (i < len && o > 0) { memcpy(out + o, "\xe2\x80\xa6", 3); o += 3; }   // (cut: « … »)
            out[o] = 0;
            int lead = 0;
            while (out[lead] == ' ') lead++;
            if (lead) memmove(out, out + lead, (size_t)(o - lead + 1));
        }
        // the last bytes of a file (-1: no file); a NUL inside becomes an end of line (never a cut string)
        int Tail(const char *path, char *buf, int cap, bool &cut) {
            cut = false;
            FILE *f = fopen(path, "rb");
            if (!f) return -1;
            long size = 0;
            if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
            if (size < 0) size = 0;
            const long from = size > cap - 1 ? size - (cap - 1) : 0;
            if (fseek(f, from, SEEK_SET) != 0) { fclose(f); return 0; }
            cut = from > 0;
            const size_t n = fread(buf, 1, (size_t)(cap - 1), f);
            fclose(f);
            for (size_t k = 0; k < n; k++) if (!buf[k]) buf[k] = '\n';
            buf[n] = 0;
            return (int)n;
        }
        // a line found: counted again if it is already there (it becomes the newest), else added (full: its source's oldest goes)
        void Add(const char *t, bool err, int src, int boot, int base, int most) {
            for (int i = base; i < s_n; i++) {
                if (strcmp(s_l[i].text, t)) continue;
                Line l = s_l[i];
                if (l.count < 9999) l.count++;
                l.boot = (int8_t)(boot < -1 ? -1 : (boot > 99 ? 99 : boot)); l.err = l.err || err;
                memmove(&s_l[i], &s_l[i + 1], sizeof(Line) * (size_t)(s_n - 1 - i));
                s_l[s_n - 1] = l;
                return;
            }
            if (s_n - base >= most) { memmove(&s_l[base], &s_l[base + 1], sizeof(Line) * (size_t)(s_n - 1 - base)); s_n--; }
            Line &l = s_l[s_n++];
            snprintf(l.text, sizeof l.text, "%s", t);
            l.count = 1; l.boot = (int8_t)(boot < -1 ? -1 : (boot > 99 ? 99 : boot)); l.src = (uint8_t)src; l.err = err;
        }
        void Parse(const char *buf, int len, bool cut, int src, int base, int most) {
            const char *p = buf, *end = buf + len;
            int starts = 0, seen = 0;                          // Brume's starts in this end of the file (the newest = this one)
            if (src == 0) for (const char *q = buf; (q = strstr(q, kStart)); q += 1) starts++;
            if (cut) { const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p)); p = nl ? nl + 1 : end; }   // (only its end)
            while (p < end) {
                const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p)), *e = nl ? nl : end;
                if (src == 0 && e - p >= 25 && !strncmp(p, kStart, 25)) seen++;
                else if (e > p) {
                    char t[kText];
                    Clean(p, (int)(e - p), t, sizeof t);
                    const int sev = t[0] ? Severity(t) : 0;
                    if (sev) Add(t, sev == 2, src, src == 0 ? starts - seen : -1, base, most);
                }
                p = nl ? nl + 1 : end;
            }
        }
        void Load() {
            s_n = s_nb = 0; s_any = false;
            if (s_flush) s_flush();                            // (Brume's lines still in memory: written first)
            if (!s_l) s_l = (Line *)malloc(sizeof(Line) * kMax);
            char *buf = s_l ? (char *)malloc(kTailBrume + 1) : nullptr;
            if (!buf) return;                                  // (no memory: the page says there is nothing to show)
            bool cut = false;
            int len = Tail(kBrumeLog, buf, kTailBrume + 1, cut);
            if (len >= 0) { s_any = true; Parse(buf, len, cut, 0, 0, kMax - kMaxOverlay); }
            s_nb = s_n;
            len = Tail(kOverlayLog, buf, kTailOverlay + 1, cut);
            if (len >= 0) { s_any = true; Parse(buf, len, cut, 1, s_nb, kMaxOverlay); }
            free(buf);
        }
        int At(int i) { return i < s_nb ? s_nb - 1 - i : s_n - 1 - (i - s_nb); }   // shown: Brume's newest first, then the overlay's
        void Free() { free(s_l); s_l = nullptr; s_n = s_nb = 0; }

        // ---- drawing helpers ----
        float TextLeft(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c, float ls = 0) {
            const float x0 = x;
            for (const char *q = s; *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0) + ls;
            return x - x0;
        }
        float W(Ui &ui, fonts::Style st, const char *s) { float w = 0; for (const char *q = s; *q;) w += ui.Advance(st, NextCodepoint(q)); return w; }
        void Fit(Ui &ui, fonts::Style st, const char *src, float maxW, char *out, int cap) {   // cut to fit, with « … »
            char tmp[kText + 8];
            snprintf(tmp, sizeof tmp, "%s", src);                // (src may be out itself)
            snprintf(out, (size_t)cap, "%s", tmp);
            int n = (int)strlen(out);
            while (n > 1 && W(ui, st, out) > maxW) {
                do n--; while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80);
                snprintf(out + n, (size_t)(cap - n), "\xe2\x80\xa6");
            }
        }
        // the text cut into lines that fit maxW (whole words; a word too long is cut), at most `most` lines; how many
        int Wrap(Ui &ui, fonts::Style st, const char *s, float maxW, char (*out)[kText + 8], int most) {
            int n = 0;
            out[0][0] = 0;
            while (*s && n < most) {
                while (*s == ' ') s++;
                if (!*s) break;
                const char *w = s;
                while (*s && *s != ' ') s++;
                char cand[kText + 8];
                const int len = (int)strlen(out[n]);
                snprintf(cand, sizeof cand, "%s%s%.*s", out[n], len ? " " : "", (int)(s - w), w);
                if (W(ui, st, cand) <= maxW) { snprintf(out[n], kText + 8, "%s", cand); continue; }
                if (len) {                                     // the next line
                    if (n + 1 >= most) { Fit(ui, st, cand, maxW, out[n], kText + 8); return n + 1; }
                    n++;
                }
                snprintf(out[n], kText + 8, "%.*s", (int)(s - w), w);
                if (W(ui, st, out[n]) > maxW) Fit(ui, st, out[n], maxW, out[n], kText + 8);   // (one word wider than the line)
            }
            return out[0][0] ? n + 1 : 0;
        }
        void TitleLeft(Ui &ui, const char *t, float x, float a) {   // yellow, in its warm glow (as Date et heure's pages)
            const float tw = ui.Width(fonts::kSetSel, t, 1.0f);
            ui.Light(x - 60, 98, x + tw + 60, 152, ps2::A(ps2::kTitleGlow, 0.12f * a), true);
            ui.TextCentered(fonts::kSetSel, t, x + tw / 2, 110, 1.0f, ps2::A(ps2::kTitle, a));
        }
        constexpr float kY0 = 292, kRow = 42, kUp = 10;     // the chosen line's middle; a row; the space above the chosen one
        float RowY(float off) { return kY0 + off * kRow + (off < 0 ? kUp * fmaxf(off, -1.0f) : s_gap * fminf(off, 1.0f)); }
        void Select(int s) {
            if (s_n <= 0) return;
            s = s < 0 ? 0 : (s >= s_n ? s_n - 1 : s);
            if (s == s_sel) return;
            s_sel = s; s_quiet = false;
        }
        void Step(int d) {
            const int s = s_sel + d;
            if (s < 0 || s >= s_n) {
                if (s_sel != (d < 0 ? 0 : s_n - 1)) Select(s);   // (a page from near the end: to the end)
                else { SndEnd(0); s_bumpV += (d < 0 ? -1 : 1) * 230.0f; }
                return;
            }
            Select(s);
        }
        void Meta(const Line &l, char *out, int cap) {        // « Brume · ce démarrage · 12 fois »
            char when[64] = "", times[32] = "";
            if (l.src == 1) snprintf(when, sizeof when, "%s", lang::T("Overlay Brume"));
            else if (l.boot == 0) snprintf(when, sizeof when, "%s", lang::T("Brume \xc2\xb7 ce d\xc3\xa9marrage"));
            else if (l.boot == 1) snprintf(when, sizeof when, "%s", lang::T("Brume \xc2\xb7 d\xc3\xa9marrage pr\xc3\xa9" "c\xc3\xa9" "dent"));
            else snprintf(when, sizeof when, lang::T("Brume \xc2\xb7 il y a %d d\xc3\xa9marrages"), (int)l.boot);
            if (l.count > 1) snprintf(times, sizeof times, lang::T(" \xc2\xb7 %d fois"), (int)l.count);
            snprintf(out, (size_t)cap, "%s%s%s", l.err ? lang::T("Erreur \xc2\xb7 ") : lang::T("Alerte \xc2\xb7 "), when, times);
        }
    }

    void Show(float now) {
        (void)now;
        Load();
        s_sel = 0; s_pos = -3.0f; s_vel = 0; s_notch = (int)lroundf(s_pos); s_quiet = true;   // the lines roll in from above
        s_bump = s_bumpV = 0; s_gap = s_gapTo = 29; s_gapV = 0; s_flash = 0;
        s_open = s_drawn = true; s_heldDir = 0;
        SndOpen();
    }

    bool Open() { return s_open; }
    float Shown() { return Clamp01(s_show); }

    void Move(int dc, int dr, float now) {
        if (!s_open) return;
        if (dr) Step(dr);
        if (dc) { if (s_n) { Step(dc * 6); audio::PlaySweep(audio::Whoosh, -dc * 0.3f, dc * 0.3f, 0.3f, 0.22f); } else SndEnd(dc * 0.4f); }
        s_heldDir = dr; s_heldSince = now; s_nextRep = now + 0.38f;
    }

    void Held(int dr, float now) {
        if (!s_open || !dr) { s_heldDir = 0; return; }
        if (dr != s_heldDir) { s_heldDir = dr; s_heldSince = now; s_nextRep = now + 0.38f; return; }
        if (now < s_nextRep) return;
        const float held = now - s_heldSince;
        s_nextRep = now + (held > 1.6f ? 0.05f : (held > 0.9f ? 0.07f : 0.1f));
        Step(dr);
    }

    void PressA(float now) {                                   // read again: what happened since it opened
        (void)now;
        if (!s_open) return;
        Load();
        if (s_sel >= s_n) s_sel = s_n ? s_n - 1 : 0;
        s_flash = 1;
        SndOk();
    }

    void PressB(float now) { (void)now; if (!s_open) return; s_open = false; s_heldDir = 0; SndBack(); }

    void Tap(float x, float y, float now) {
        if (!s_open) return;
        if (!s_xmb && x < 470) { PressB(now); return; }      // (the carousel's icon, on the left: back to the list)
        int best = -1; float bd = 24;
        for (int j = (int)floorf(s_pos) - 3; j <= (int)floorf(s_pos) + 9; j++) {
            if (j < 0 || j >= s_n) continue;
            const float d = fabsf(RowY((float)j - s_pos) - y);
            if (d < bd) { bd = d; best = j; }
        }
        if (best >= 0 && best != s_sel) Select(best);
    }

    void DragBegin() { s_dragFrom = s_sel; }
    void DragTo(float rows) { if (s_open) Select(s_dragFrom + (int)lroundf(rows * 60.0f / kRow)); }

    void Update(float now, float dt) {
        s_now = now;
        Glide(s_show, s_showV, s_open ? 1.0f : 0.0f, dt, s_open ? 7.0f : 9.0f);
        if (!s_open && s_show < 0.003f) {
            s_show = 0; s_showV = 0;
            if (s_drawn) { s_drawn = false; Free(); }           // gone: its memory too
            return;
        }
        s_flash *= expf(-dt * 3.2f);
        Glide(s_pos, s_vel, (float)s_sel, dt, 9.0f);
        const int n = (int)lroundf(s_pos);
        if (n != s_notch) { if (!s_quiet && n >= 0 && n < s_n) Tick(n, 0.25f); s_notch = n; }
        Glide(s_bump, s_bumpV, 0.0f, dt, 15.0f);
        Glide(s_gap, s_gapV, s_gapTo, dt, 12.0f);
    }

    void Draw(Ui &ui, float now, float alpha, float slide, bool xmb) {
        (void)now;
        if (!s_drawn || s_show <= 0.003f) return;
        const float k = Clamp01(s_show), a = alpha * k * k * (3 - 2 * k);
        s_xmb = xmb;
        if (a <= 0.003f) return;
        const float cx = (xmb ? 640.0f : 860.0f) + slide + 46.0f * (1 - k);   // (it comes in from the right, and goes back there)
        const float x0 = cx - 300, x1 = fminf(cx + 380, 1236.0f), w = x1 - x0;
        TitleLeft(ui, "Journal de Brume", x0 - 2, a);
        char b[200], up[220];
        if (s_n) snprintf(b, sizeof b, s_n > 1 ? lang::T("%d alertes \xc2\xb7 la plus r\xc3\xa9" "cente en haut") : lang::T("%d alerte"), s_n);
        else snprintf(b, sizeof b, "%s", lang::T("Erreurs et alertes de Brume"));
        Upper(b, up, sizeof up);
        TextLeft(ui, fonts::kDate, up, x0, 152, ps2::A(ps2::kWords, 0.8f * a), 4);
        if (!s_n) {                                            // nothing to show: say it, plainly
            const float mx = (x0 + x1) * 0.5f;
            ui.Light(mx - 200, 296, mx + 200, 360, ps2::A(ps2::kHalo, 0.22f * a), true);
            ui.TextCentered(fonts::kSetSel, s_any ? "Aucune erreur" : "Pas encore de journal", mx, 300, 0.5f, ps2::A(ps2::kChosen, a));
            ui.TextCentered(fonts::kSetDesc, s_any ? "Brume n'a rien not\xc3\xa9 d'inqui\xc3\xa9tant. Tant mieux !"
                                                   : "Il arrive dans brume/perf.log au prochain d\xc3\xa9marrage.", mx, 344, 0.3f, ps2::A(ps2::kFaint, a));
            return;
        }
        char lines[3][kText + 8], c[kText + 8];
        {   // the chosen one's whole text: its lines decide the space under it (it glides)
            const int nl = Wrap(ui, fonts::kSetOpt, s_l[At(s_sel < s_n ? s_sel : 0)].text, w, lines, 3);
            s_gapTo = 25.0f * (nl < 1 ? 1 : nl) + 4;
        }
        const int base = (int)floorf(s_pos);
        for (int j = base - 3; j <= base + 10; j++) {
            if (j < 0 || j >= s_n) continue;
            const float off = (float)j - s_pos, y = RowY(off) + s_bump, kk = Clamp01(1 - fabsf(off));
            const float o = a * (off < 0 ? Clamp01(1 - (-off - 1.0f) / 0.55f) : 1.0f) * Clamp01((650 - y) / 60);   // (one line above: the title stays clear)
            if (o <= 0.003f) continue;
            const Line &l = s_l[At(j)];
            {   // its mark: a small light, pink for an error, gold for a warning
                const Rgba mc = l.err ? ps2::kPink : ps2::kTitle;
                const float dx = x0 - 22, dy = y - 1;
                ui.Shape(dx - 9, dy - 9, dx + 9, dy + 9, -1, -1, 1, 1, ps2::A(mc, (0.35f + 0.4f * kk) * o), 21);
                ui.Shape(dx - 2.5f, dy - 2.5f, dx + 2.5f, dy + 2.5f, 0, 0, 5, 5, ps2::A(mc, o), 24, 5, 5, 2.5f);
            }
            if (kk < 0.99f) {                                  // the others: one line, cut to fit
                Fit(ui, fonts::kGameItem, l.text, w, c, sizeof c);
                TextLeft(ui, fonts::kGameItem, c, x0, y - 14, ps2::A(ps2::kChoice, 0.82f * o * (1 - kk)));
            }
            if (kk > 0.01f) {                                  // the chosen one: all of it, near-white in its halo, then where it came from
                const int nl = Wrap(ui, fonts::kSetOpt, l.text, w, lines, 3);
                float tw = 0;
                for (int i = 0; i < nl; i++) tw = fmaxf(tw, W(ui, fonts::kSetOpt, lines[i]));
                ui.Chosen(x0 - 70, x1 + 30, y - 2 + (nl - 1) * 12.5f, x0, tw, kk * o * (1 + 0.6f * s_flash));
                if (nl > 1) ui.Light(x0 - 30, y - 24, x0 + tw + 30, y + (nl - 1) * 25 + 20, ps2::A(ps2::kHalo, 0.18f * kk * o), true);
                for (int i = 0; i < nl; i++) TextLeft(ui, fonts::kSetOpt, lines[i], x0, y - 13 + i * 25, ps2::A(ps2::kChosen, 0.96f * o * kk));
                Meta(l, b, sizeof b);
                TextLeft(ui, fonts::kSetDesc, b, x0, y - 13 + nl * 25 + 3, ps2::A(l.err ? ps2::Mix(ps2::kFaint, ps2::kPink, 0.45f) : ps2::kFaint, o * kk), 0.3f);
            }
        }
        if (s_n > 6) {                                         // more above / below: small arrows (they fade at the ends)
            ui.Arrow(x0 - 22, kY0 - 2.2f * kRow - kUp, 4.5f, 2, ps2::A(ps2::kChoice, 0.6f * a * Clamp01(s_pos / 2.0f)));
            ui.Arrow(x0 - 22, 640, 4.5f, 3, ps2::A(ps2::kChoice, 0.6f * a * Clamp01((s_n - 1 - s_pos) / 3.0f)));
        }
    }

    void Hints(const char *&aL, const char *&bL, const char **chip, const char **lbl, int &nr) {
        nr = 0;
        aL = "Relire"; bL = "Retour";
        chip[nr] = "chipLeRi"; lbl[nr++] = "Défiler";
        chip[nr] = "chipUpDo"; lbl[nr++] = "Alertes";
    }

} // namespace brume::journal
