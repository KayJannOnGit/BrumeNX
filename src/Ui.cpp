// Brume - interface batching. See Ui.hpp.
#include "Ui.hpp"
#include "Lang.hpp"
#include <cmath>
#include <cstring>

namespace brume {

    Rgba Hex(uint32_t rgb, float a) {
        return { ((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a };
    }

    void Ui::Chosen(float x0, float x1, float cy, float tx, float tw, float k) {   // v70 (the PS2 look)
        if (k <= 0.004f) return;
        Light(x0, cy - 38, x1, cy + 38, ps2::A(ps2::kHalo, 0.26f * k));                    // the band: no edge, it fades out
        Light(tx - 46, cy - 30, tx + tw + 46, cy + 30, ps2::A(ps2::kHalo, 0.42f * k), true);   // the halo behind the words
        Light(tx - 14, cy - 16, tx + tw + 14, cy + 16, ps2::A({ 0.55f, 0.72f, 1.0f, 1 }, 0.22f * k), true);
    }

    void Ui::Surface(float x0, float y0, float x1, float y1, float a, Rgba tint, float r) {   // v78 (ui_fsh mode 30)
        if (a <= 0.003f || x1 - x0 < 2 || y1 - y0 < 2) return;
        const float w = x1 - x0, h = y1 - y0, m = 48, mb = 62;     // (room for the halo, and the shadow 14 px lower)
        r = fminf(r, 0.5f * fminf(w, h));
        Shape(x0 - m, y0 - m, x1 + m, y1 + mb, -m, -m, w + m, h + mb, Rgba{ tint.r, tint.g, tint.b, fminf(a, 1.0f) }, 30, w, h, r);
    }

    void Ui::Empty(float cx, const char *title, const char *line, const char *hint, float a) {   // v70.3 (the PS2 look)
        if (a <= 0.003f) return;
        const float tw = Width(fonts::kPageTitle, title, 1.5f);
        Light(cx - tw / 2 - 60, 244, cx + tw / 2 + 60, 320, ps2::A(ps2::kTitleGlow, 0.13f * a), true);
        TextCentered(fonts::kPageTitle, title, cx, 250, 1.5f, ps2::A(ps2::kTitle, a));
        if (line) TextCentered(fonts::kGameItem, line, cx, 330, 0, ps2::A(ps2::kWords, 0.9f * a));
        if (hint) TextCentered(fonts::kHint, hint, cx, 370, 0.5f, ps2::A(ps2::kFaint, 0.9f * a));
    }

    uint32_t NextCodepoint(const char *&s) {
        const unsigned char *p = (const unsigned char *)s;
        uint32_t c = *p++;
        if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
        else if (c >= 0xE0 && p[0] && p[1])   { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
        else if (c >= 0xC0 && p[0])           { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
        s = (const char *)p;
        return c;
    }

    static int PutUtf8(uint32_t c, char *o) {
        if (c < 0x80)  { o[0] = (char)c; return 1; }
        if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
        o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3;
    }

    void Upper(const char *in, char *out, int cap) {
        int n = 0;
        while (*in && n < cap - 4) {
            uint32_t c = NextCodepoint(in);
            if (c >= 'a' && c <= 'z') c -= 32;
            else if ((c >= 0xE0 && c <= 0xFE) && c != 0xF7) c -= 32;   // à..þ -> À..Þ
            else if (c == 0xFF) c = 0x178;                             // ÿ -> Ÿ
            else if (c == 0x153) c = 0x152;                            // œ -> Œ
            n += PutUtf8(c, out + n);
        }
        out[n] = 0;
    }

    namespace {
        fonts::Atlas s_live[2];                          // v45: copies of the baked atlases' tables (filled at first use, no constructor)
        bool s_liveOk = false;
        void LiveInit() { if (s_liveOk) return; s_live[0] = fonts::kAtlas0; s_live[1] = fonts::kAtlas1; s_liveOk = true; }
    }
    void Ui::SetClockFaces(int si, const fonts::Face &clock, const fonts::Face &glow) {
        LiveInit();
        if (si < 0 || si > 1) return;
        s_live[si].faces[fonts::kClock] = clock; s_live[si].faces[fonts::kClockGlow] = glow;
    }
    void Ui::ResetClockFaces(int si) {
        LiveInit();
        if (si < 0 || si > 1) return;
        const fonts::Atlas &k = si ? fonts::kAtlas1 : fonts::kAtlas0;
        s_live[si].faces[fonts::kClock] = k.faces[fonts::kClock]; s_live[si].faces[fonts::kClockGlow] = k.faces[fonts::kClockGlow];
    }

    void Ui::Begin(float scale) {
        m_n = 0; m_runs = 0; m_blackAt = -1;
        m_scale = scale;
        LiveInit();
        m_atlas = scale > 1.2f ? &s_live[1] : &s_live[0];
    }

    void Ui::Flush(Gpu &gpu) {
        const Program *ui = gpu.CurrentProgram();
        if (!m_runs || !ui) { const int b = m_blackAt > 0 ? m_blackAt : 0; gpu.DrawUi(m_v + b, (uint32_t)(m_n - b)); }
        else {                                   // v74: the runs, in order (v99: ui, glass or the simple modes' program)
            for (int r = 0; r < m_runs; r++) {
                int from = m_runAt[r];
                const int to = r + 1 < m_runs ? m_runAt[r + 1] : m_n;
                if (from < m_blackAt) from = m_blackAt;          // v100: what an opaque black quad covers is not drawn
                if (to <= from) continue;
                const int k = m_runCls[r];
                const Program *p = k == 1 ? m_glass : (k == 2 ? m_lite : (k == 3 ? m_surf : ui));
                if (!p) continue;
                if (gpu.CurrentProgram() != p) gpu.UseProgram(*p);
                gpu.DrawUi(m_v + from, (uint32_t)(to - from));
            }
            if (gpu.CurrentProgram() != ui) gpu.UseProgram(*ui);   // (left as the caller bound it)
        }
        m_n = 0; m_runs = 0; m_blackAt = -1;
    }

    // v99: the run a quad goes in. A simple quad can go in an ui_fsh run too (ui_fsh draws every mode but the glass): when
    // runs are short, the last ones are kept for ui_fsh and the glass, so that nothing is ever drawn by the wrong program
    bool Ui::Run(int need) {
        if (need == 1 && !m_glass) return false;            // (no glass program: nothing, never drawn by ui_fsh)
        if ((need == 2 && !m_lite) || (need == 3 && !m_surf)) need = 0;
        const int cur = m_runs > 0 ? m_runCls[m_runs - 1] : -1;
        if (cur == need || (need >= 2 && cur == 0 && m_runs >= kMaxRuns - 2)) return true;
        if (m_runs >= (need >= 2 ? kMaxRuns - 2 : kMaxRuns)) return false;
        m_runAt[m_runs] = m_n; m_runCls[m_runs] = (uint8_t)need; m_runs++;
        return true;
    }

    const fonts::Glyph *Ui::Find(fonts::Style s, uint32_t cp) const {
        const fonts::Face &f = m_atlas->faces[s];
        for (int i = 0; i < f.count; i++) if (f.glyphs[i].cp == cp) return &f.glyphs[i];
        return nullptr;
    }

    float Ui::Advance(fonts::Style s, uint32_t cp) const {
        const fonts::Glyph *g = Find(s, cp);
        return g ? g->adv / m_scale : 0.0f;
    }

    float Ui::Baseline(fonts::Style s, float top) const {
        const fonts::Face &f = m_atlas->faces[s];
        // CSS line-height:1 -> line box = font size; content area (ascent+descent) centred in it.
        const float size = (f.ascent + f.descent) / m_scale;            // ≈ font size * (asc+desc)/em
        const float em = fonts::kStylePx(s);
        return top + (em - size) * 0.5f + f.ascent / m_scale;
    }

    float Ui::Width(fonts::Style s, const char *utf8, float ls) const {
        utf8 = lang::T(utf8);                                    // v64: what is drawn is what is measured
        float w = 0;
        while (*utf8) w += Advance(s, NextCodepoint(utf8)) + ls;
        return w;
    }

    void Ui::Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                  Rgba c, float mode, float p1, float p2, float p3) {
        if (m_n + 6 > kMax) return;
        // v74: a glass icon (mode 29) has its own program; v99: the simple modes too (uilite_fsh)
        const int mi = (int)(mode + 0.5f);
        if (!Run(mi == 29 ? 1 : (LiteMode(mode) ? 2 : (mi == 30 || mi == 25 ? 3 : 0)))) return;
        if (m_zoom != 1.0f || m_zc[0] != m_zo[0] || m_zc[1] != m_zo[1]) {   // (v78: around any point, landing anywhere)
            x0 = m_zo[0] + (x0 - m_zc[0]) * m_zoom; x1 = m_zo[0] + (x1 - m_zc[0]) * m_zoom;
            y0 = m_zo[1] + (y0 - m_zc[1]) * m_zoom; y1 = m_zo[1] + (y1 - m_zc[1]) * m_zoom;
        }
        if (mi == 24 && c.a >= 0.999f && c.r <= 0.0f && c.g <= 0.0f && c.b <= 0.0f && p3 <= 0.0f &&   // v100: an opaque black
            x0 <= 0.0f && y0 <= 0.0f && x1 >= 1280.0f && y1 >= 720.0f) m_blackAt = m_n;             // full-screen quad (Blackout)
        const UiVtx a = { x0, y0, u0, v0, c.r, c.g, c.b, c.a, mode, p1, p2, p3 };
        const UiVtx b = { x1, y0, u1, v0, c.r, c.g, c.b, c.a, mode, p1, p2, p3 };
        const UiVtx d = { x1, y1, u1, v1, c.r, c.g, c.b, c.a, mode, p1, p2, p3 };
        const UiVtx e = { x0, y1, u0, v1, c.r, c.g, c.b, c.a, mode, p1, p2, p3 };
        m_v[m_n++] = a; m_v[m_n++] = b; m_v[m_n++] = d;
        m_v[m_n++] = a; m_v[m_n++] = d; m_v[m_n++] = e;
    }

    float Ui::Glyph(fonts::Style s, uint32_t cp, float penX, float top, Rgba color,
                    float mode, float p1, float p2, float p3, bool flipY, float mirror) {
        const fonts::Glyph *g = Find(s, cp);
        if (!g) return 0.0f;
        const fonts::Face &f = m_atlas->faces[s];
        const float base = Baseline(s, top);
        const float k = 1.0f / (f.gscale * m_scale);                   // atlas px -> menu px
        float x0 = penX + g->bx / m_scale, y0 = base - g->by / m_scale;
        float x1 = x0 + g->w * k, y1 = y0 + g->h * k;
        const float aw = (float)m_atlas->w, ah = (float)m_atlas->h;
        float u0 = g->x / aw, v0 = g->y / ah, u1 = (g->x + g->w) / aw, v1 = (g->y + g->h) / ah;
        if (flipY) { const float ny0 = 2 * mirror - y1, ny1 = 2 * mirror - y0; y0 = ny0; y1 = ny1; const float t = v0; v0 = v1; v1 = t; }
        Quad(x0, y0, x1, y1, u0, v0, u1, v1, color, mode, p1, p2, p3);
        return g->adv / m_scale;
    }

    float Ui::TextCentered(fonts::Style s, const char *utf8, float cx, float top, float ls,
                           Rgba color, float mode, float p1, float p2, float p3) {
        utf8 = lang::T(utf8);                                    // v64: the English text when Brume speaks English
        float w = 0;
        for (const char *q = utf8; *q;) w += Advance(s, NextCodepoint(q)) + ls;
        const float left = cx - w * 0.5f;
        float pen = left;
        while (*utf8) pen += Glyph(s, NextCodepoint(utf8), pen, top, color, mode, p1, p2, p3) + ls;
        return left;
    }

    void Ui::Shape(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                   Rgba color, float mode, float p1, float p2, float p3) {
        Quad(x0, y0, x1, y1, u0, v0, u1, v1, color, mode, p1, p2, p3);
    }

    // v87 - a turned quad (Quad only knows upright ones): the same run and zoom rules, mode 21 « a piece of trail »
    void Ui::Streak(float ax, float ay, float bx, float by, float h, Rgba c) {
        if (c.a <= 0.002f || h <= 0.05f || m_n + 6 > kMax) return;
        if (!Run(2)) return;                                     // (mode 21: the simple modes' program, v99)
        const float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        const float ux = len > 0.01f ? dx / len : 1.0f, uy = len > 0.01f ? dy / len : 0.0f;
        const float L = len * 0.5f + h, mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f, p2 = fminf(1.0f, 2.0f * h / L);
        static const float kCx[4] = { -1, 1, 1, -1 }, kCy[4] = { -1, -1, 1, 1 };
        UiVtx q[4];
        for (int k = 0; k < 4; k++) {
            float x = mx + ux * L * kCx[k] - uy * h * kCy[k], y = my + uy * L * kCx[k] + ux * h * kCy[k];
            if (m_zoom != 1.0f || m_zc[0] != m_zo[0] || m_zc[1] != m_zo[1]) { x = m_zo[0] + (x - m_zc[0]) * m_zoom; y = m_zo[1] + (y - m_zc[1]) * m_zoom; }
            q[k] = { x, y, kCx[k], kCy[k], c.r, c.g, c.b, c.a, 21, 1.0f, p2, 0 };
        }
        m_v[m_n++] = q[0]; m_v[m_n++] = q[1]; m_v[m_n++] = q[2];
        m_v[m_n++] = q[0]; m_v[m_n++] = q[2]; m_v[m_n++] = q[3];
    }

} // namespace brume
