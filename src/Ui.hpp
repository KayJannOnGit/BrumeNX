// Brume - interface batching: text from the pre-baked atlases (tools/fontbake.cpp)
// laid out like the mockup's CSS, and shape quads for ui_fsh.glsl.
#pragma once
#include "Gpu.hpp"
#include "fonts.h"     // generated

namespace brume {

    struct Rgba { float r, g, b, a; };
    Rgba Hex(uint32_t rgb, float a = 1.0f);

    class Ui {
    public:
        // scale: 1 at 1280x720, 1.5 at 1920x1080 (picks the matching atlas).
        void Begin(float scale);
        // v45 - the clock's font loaded on demand: replace / restore the two clock styles of atlas si (0 = 1x, 1 = 1.5x)
        static void SetClockFaces(int si, const fonts::Face &clock, const fonts::Face &glow);
        static void ResetClockFaces(int si);
        void Flush(Gpu &gpu);                  // draws everything queued (one call; v74: a few more around the glass icons)
        // v100 (perf): an opaque black quad over the whole screen was queued this frame (a launch's black): what is under it
        // need not be drawn - Flush starts at it, and the scene can be left out (Entry)
        bool Blackout() const { return m_blackAt >= 0; }
        void SetGlassProgram(const Program *p) { m_glass = p && p->ok ? p : nullptr; }   // v74: mode 29 (verre_fsh)
        // v99 (perf): the simple modes (text, lights, veils, plain rects, stars, arrows) in their own small program
        // (shaders/uilite_fsh.glsl, the same maths): ui_fsh cost ~7 ms per full screen of pixels on the console
        void SetLiteProgram(const Program *p) { m_lite = p && p->ok ? p : nullptr; }
        // v99: the surfaces and the popups' backdrop (modes 30, 25: shaders/uisurf_fsh.glsl, the same maths)
        void SetSurfProgram(const Program *p) { m_surf = p && p->ok ? p : nullptr; }
        static bool LiteMode(float mode) {
            const int m = (int)(mode + 0.5f);
            return m == 0 || m == 3 || m == 21 || m == 23 || m == 24 || m == 27 || m == 28;
        }

        // Width of a line as CSS lays it out: advances + letter-spacing after each char.
        float Width(fonts::Style s, const char *utf8, float letterSpacing) const;
        // A centred line in a box starting at `top` whose height is the font size
        // (CSS line-height:1): the baseline sits where a browser puts it.
        // mode/p1..p3 go to ui_fsh.glsl. Returns the left edge used.
        float TextCentered(fonts::Style s, const char *utf8, float cx, float top, float letterSpacing,
                           Rgba color, float mode = 0, float p1 = 0, float p2 = 0, float p3 = 0);
        // One glyph at pen x (left of its advance box), line box top; flipY mirrors it
        // around y = mirror (reflections). Returns the advance in menu px.
        float Glyph(fonts::Style s, uint32_t cp, float penX, float top, Rgba color,
                    float mode, float p1, float p2, float p3, bool flipY = false, float mirror = 0);
        float Advance(fonts::Style s, uint32_t cp) const;
        float Baseline(fonts::Style s, float top) const;

        // A shape quad (local px coords go in uv: see ui_fsh modes 10-13).
        void Shape(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                   Rgba color, float mode, float p1 = 0, float p2 = 0, float p3 = 0);

        // v70 - the PS2 look (docs/moodboard, the same as Brume Overlay): soft light added on the fog, never an edge.
        // A band (fades out on every side) or a round glow (glow = true), in the box x0..x1, y0..y1.
        void Light(float x0, float y0, float x1, float y1, Rgba c, bool glow = false) {
            if (c.a > 0.002f && x1 > x0 && y1 > y0) Shape(x0, y0, x1, y1, 0, 0, x1 - x0, y1 - y0, c, 27, x1 - x0, y1 - y0, glow ? 1.0f : 0.0f);
        }
        // a small solid triangle centred on (cx, cy), half size h: dir 0 left, 1 right, 2 up, 3 down
        void Arrow(float cx, float cy, float h, int dir, Rgba c) {
            if (c.a > 0.002f && h > 0.5f) Shape(cx - h, cy - h, cx + h, cy + h, -1, -1, 1, 1, c, 28, (float)dir, 1.0f / h);
        }
        // v87 - a streak of light from (ax, ay) to (bx, by), soft across (half width h), added (the comets' trails, mode 21):
        // the busy orbs' trails (Busy.hpp)
        void Streak(float ax, float ay, float bx, float by, float h, Rgba c);
        // the chosen line's light: a soft band across x0..x1 and a halo behind its words (tx .. tx + tw), k = how chosen
        void Chosen(float x0, float x1, float cy, float tx, float tw, float k);
        // v70.3 - a soft dark veil (no edge, it fades out on every side) so that words read on the fog, opacity a (1 = the
        // usual, up to ~1.6 = darker); v70.5: fade = the width of its soft edge in px (0: a share of its size)
        void Veil(float x0, float y0, float x1, float y1, float a, float fade = 0) {
            if (a > 0.002f && x1 > x0 && y1 > y0) Shape(x0, y0, x1, y1, 0, 0, x1 - x0, y1 - y0, Rgba{ 1, 1, 1, a }, 23, x1 - x0, y1 - y0, fade);
        }
        // v70.3 - a page with nothing to show, in the PS2 look: the title in yellow in its warm glow, a line, a faint hint
        void Empty(float cx, const char *title, const char *line, const char *hint, float a);
        // v78 (Yass: « on voit pas leur fond… c'est une brume opaque mais en général elle est carrée… pas de Liquid Glass ») - a
        // « surface » (ui_fsh mode 30), the plate behind a panel, a notification or a popup: dense night mist with rounded
        // (squircle) corners and a soft edge, the light of its tint along the top edge and around it, a shadow below. Never glass.
        // x0..y1 = the plate (its shadow and halo reach ~50 px further); a = how much it shows; tint = its light (s_tint: the
        // ambient colour, set every frame by Entry; red: a deletion, pink: an error); r = the corners' radius.
        void Surface(float x0, float y0, float x1, float y1, float a, Rgba tint, float r = 38);   // v78.1 (Yass: « arrondir un peu plus les coins »): 24 -> 38
        void Surface(float x0, float y0, float x1, float y1, float a) { Surface(x0, y0, x1, y1, a, s_tint); }
        static inline Rgba s_tint{ 0.22f, 0.42f, 0.95f, 1 };
        // Everything queued afterwards is scaled by z around the screen centre (640, 360).
        void SetZoom(float z) { SetXform(z, 640, 360, 640, 360); }
        // v78: ... scaled by z around (cx, cy), which lands on (ox, oy) - a popup that grows, a button pressed in
        void SetXform(float z, float cx, float cy, float ox, float oy) { m_zoom = z; m_zc[0] = cx; m_zc[1] = cy; m_zo[0] = ox; m_zo[1] = oy; }
        const fonts::Atlas &AtlasData() const { return *m_atlas; }
        float Scale() const { return m_scale; }

    private:
        const fonts::Glyph *Find(fonts::Style s, uint32_t cp) const;
        void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                  Rgba c, float mode, float p1, float p2, float p3);

        static constexpr int kMax = 6 * 1024;
        UiVtx m_v[kMax];
        int   m_n = 0;
        // v74 - the settings icons' liquid glass (mode 29) has its own program (shaders/verre_fsh.glsl): inside ui_fsh it
        // tripled the registers of every interface pixel. The queue is cut into runs where the program changes, drawn in
        // order (Flush); without that program (or too many runs), mode 29 quads are dropped, never drawn wrong.
        // v99: three programs now - 0 ui_fsh, 1 the glass (verre_fsh), 2 the simple modes (uilite_fsh); each run has its own
        static constexpr int kMaxRuns = 128;
        int   m_runAt[kMaxRuns];                 // where each run starts (in vertices)
        uint8_t m_runCls[kMaxRuns];              // and its program (0 ui, 1 glass, 2 lite, 3 surfaces)
        int   m_runs = 0;
        int   m_blackAt = -1;                    // v100: where the last opaque full-screen black quad starts (vertices), -1: none
        bool  Run(int need);                     // the run for the next quad (false: it cannot be drawn - no glass program, no room)
        const Program *m_glass = nullptr, *m_lite = nullptr, *m_surf = nullptr;
        const fonts::Atlas *m_atlas = &fonts::kAtlas0;
        float m_scale = 1.0f;
        float m_zoom = 1.0f, m_zc[2] = { 640, 360 }, m_zo[2] = { 640, 360 };
    };

    // v70 - the PS2 look's colours (Brume Overlay, the PS2 / PlayStation BB menus): titles in yellow, the choices in light
    // blue, the chosen one near-white (never pure white) with a soft halo, the rest faint; the buttons' letters in colour.
    namespace ps2 {
        // v81: the theme tints these (Theme.cpp glides them every frame: Nuit profonde dimmer, Chrome silver); Brume's here
        inline Rgba kTitle{ 0.95f, 0.86f, 0.40f, 1 }, kTitleGlow{ 0.95f, 0.75f, 0.25f, 1 }, kChoice{ 0.60f, 0.74f, 1.0f, 1 },
                    kChosen{ 0.94f, 0.96f, 1.0f, 1 }, kHalo{ 0.32f, 0.52f, 1.0f, 1 }, kFaint{ 0.52f, 0.60f, 0.84f, 1 },
                    kWords{ 0.80f, 0.84f, 0.94f, 1 };
        constexpr Rgba kPink{ 1.0f, 0.45f, 0.62f, 1 }, kGreen{ 0.45f, 0.92f, 0.62f, 1 },
                       kRed{ 1.0f, 0.36f, 0.38f, 1 };   // v78: a deletion's « Oui », once it may be chosen (a warm red, never harsh)
        inline Rgba A(Rgba c, float a) { return Rgba{ c.r, c.g, c.b, c.a * a }; }
        inline Rgba Mix(Rgba a, Rgba b, float t) { return Rgba{ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
        // a choice's colour: light blue -> near-white as it gets chosen (k 0..1), its alpha a
        inline Rgba Line(float k, float a) { return A(Mix(kChoice, kChosen, k), a * (0.82f + 0.14f * k)); }
    }

    // UTF-8 decoding and French upper case (CSS text-transform: uppercase).
    uint32_t NextCodepoint(const char *&p);
    void Upper(const char *in, char *out, int cap);

} // namespace brume
