// Brume - the console wheel (see Wheel.hpp). Every number below comes from docs/M1/maquette-brume.html;
// the mockup's names are kept in the comments so each line can be checked against it.
#include "Wheel.hpp"
#include "Consoles.hpp"
#include "Lang.hpp"
#include "Haptics.hpp"
#include "Settings.hpp"
#include "Profiles.hpp"
#include "Ease.hpp"
#include "Theme.hpp"
#include <cstddef>
#include "Audio.hpp"
#include "Ui.hpp"
#include "Pads.hpp"
#include "Charge.hpp"   // v96: the battery in the top bar
#include "MyPage.hpp"   // v96: the top bar's picture (Nextendo or Switch)
#include "Spotify.hpp"  // v98: its line in the top bar (the battery steps aside)
#include "ui_layout.h"     // generated from the browser's layout of the mockup's CSS (tools/ui2h.py)
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume {

    namespace {
        // CONSOLES (tint, chord, sub) - same order as the mockup and as the baked models
        struct ConsoleDef { uint32_t tint; int chord[4]; int sub; };
        const ConsoleDef kC[8] = {   // (by voice: Consoles.hpp)
            // v28: the chords of « the very first time » (Yass): D maj7, A maj7, F maj7, G sus2, E maj7/B, Bb maj7,
            // C maj9, Eb add9. The change between two of them is the angelic transition (audio::SetChord, glide 4).
            { 0xc0506a, {50, 57, 61, 66}, 38 }, { 0x3550c8, {45, 52, 56, 61}, 33 }, { 0x5a3cc0, {41, 48, 52, 57}, 41 },
            { 0x2f8a62, {43, 50, 55, 57}, 43 }, { 0x6a7494, {47, 52, 56, 63}, 40 }, { 0x3a62c4, {46, 53, 57, 62}, 34 },
            { 0x5e48b0, {48, 55, 59, 62}, 36 }, { 0xa0603a, {51, 55, 58, 65}, 39 } };
        constexpr int   N = 8;                       // the ring's validated places (45 degrees apart)
        constexpr int   NC = consoles::kCount;       // v93: the consoles (Consoles.hpp)
        constexpr float RING = 5.0f, RZ = -5.0f, PI = 3.14159265f, kStep = 2 * PI / N;
        // v93: the chord of a console = its voice's (the recorded pads); its light = its own (the table)
        const ConsoleDef &CD(int i) { return kC[consoles::Valid(i) ? consoles::kDefs[i].voice & 7 : 0]; }
        int Voice(int i) { return consoles::Valid(i) ? consoles::kDefs[i].voice & 7 : 0; }
        // v28 - how each console stands (Yass, after seeing the wheel on the console): the home consoles were too big,
        // the Switch should lean back like the PSP (measured on the baked models: the PSP's screen faces 48 deg up),
        // the home consoles lean a little towards us to show their top and logo, the PS2 stands up (more iconic).
        //   size (x the carousel scale), tilt (rad, + = top towards us), upright (rad about the front axis)
        struct Look { float size, tilt, upright; };
        const Look kLook[consoles::kMax] = {
            { 1.00f, -0.84f, 0 },          // Switch: leans back like the PSP
            { 0.78f,  0.15f, PI / 2 },     // PS2: standing
            { 0.78f,  0.35f, 0 },          // GameCube
            { 0.85f,  0.35f, 0 },          // N64
            { 0.85f,  0.35f, 0 },          // PS1
            { 1.00f,  0.00f, 0 },          // PSP: its angle is perfect
            { 1.00f,  0.00f, 0 },          // GBA
            { 1.00f,  0.00f, 0 },          // native ports
            // v93: the blocks of glass (no model yet): leaning a little towards us, like the home consoles
            { 0.80f, 0.22f, 0 }, { 0.80f, 0.22f, 0 }, { 0.80f, 0.22f, 0 }, { 0.80f, 0.22f, 0 },
            { 0.80f, 0.22f, 0 }, { 0.80f, 0.22f, 0 }, { 0.80f, 0.22f, 0 } };
        // v28 - the stars (the mockup's dust: DUST = 160, same layout), plus their own slow orbit around the carousel
        constexpr int kStars = 160;
        struct Star { float a0, r, y, ph, cp, w; };
        Star g_star[kStars];
        bool g_starInit = false;
        void InitStars() {
            uint32_t s = 0x9e3779b9u;
            auto R = [&]() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) * (1.0f / 16777216.0f); };
            for (int i = 0; i < kStars; i++) {
                Star &q = g_star[i];
                q.a0 = R() * 2 * PI; q.r = 2 + R() * 11; q.y = -2 + R() * 6; q.ph = R() * 6.28f; q.cp = 0.4f + R() * 0.8f;
                q.w = 0.012f + R() * 0.03f;   // rad/s: one turn in 3 to 9 minutes, each at its own pace
            }
            g_starInit = true;
        }
        float Smooth(float a, float b, float x) { const float t = x <= a ? 0 : x >= b ? 1 : (x - a) / (b - a); return t * t * (3 - 2 * t); }
        constexpr float FALL_START = 0.2f, FALL_GAP = 0.13f, FALL_DUR = 0.85f, FALL_H = 3.2f, CAM_DUR = 1.8f;
        constexpr float CAM_T1 = 0.55f, CAM_TAU = 0.55f, CAM_V = 1.0f / (CAM_T1 / 2 + CAM_TAU);
        float camCurve(float t) {
            if (t <= 0) return 0;
            if (t < CAM_T1) return CAM_V * (t / 2 - CAM_T1 / (2 * PI) * sinf(PI * t / CAM_T1));
            return fminf(1.0f, CAM_V * (CAM_T1 / 2 + CAM_TAU * (1 - expf(-(t - CAM_T1) / CAM_TAU))));
        }
        float ease(float x) { return 1 - powf(1 - x, 3); }
        float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
        // CSS cubic-bezier(x1, y1, x2, y2) at x
        float Bezier(float x1, float y1, float x2, float y2, float x) {
            if (x <= 0) return 0;
            if (x >= 1) return 1;
            float t = x;
            for (int i = 0; i < 8; i++) {
                const float cx = 3 * x1 * t * (1 - t) * (1 - t) + 3 * x2 * t * t * (1 - t) + t * t * t - x;
                const float dx = 3 * x1 * (1 - t) * (1 - 3 * t) + 3 * x2 * t * (2 - 3 * t) + 3 * t * t;
                if (fabsf(dx) < 1e-5f) break;
                t = clamp01(t - cx / dx);
            }
            return 3 * y1 * t * (1 - t) * (1 - t) + 3 * y2 * t * t * (1 - t) + t * t * t;
        }
        float SrgbToLin(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
        void Lin(uint32_t hex, float *o) { o[0] = SrgbToLin(((hex >> 16) & 255) / 255.0f); o[1] = SrgbToLin(((hex >> 8) & 255) / 255.0f); o[2] = SrgbToLin((hex & 255) / 255.0f); }
        void Lerp3(float *a, const float *b, float k) { for (int i = 0; i < 3; i++) a[i] += (b[i] - a[i]) * k; }
        // v29: a light colour of the scene, moved to the chosen ambient colour (the default one gives back the mockup's)
        void Amb(uint32_t hex, float *o) {
            static const float kDef[3] = { 0.22f, 0.42f, 0.95f };
            Lin(hex, o);
            for (int i = 0; i < 3; i++) o[i] *= powf(fmaxf(0.02f, Settings::s_ambient[i]) / kDef[i], 2.2f);
        }

        // ---- matrices (column-major, like GLSL) ----
        struct M4 { float m[16]; };
        M4 Mul(const M4 &a, const M4 &b) { M4 r; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k]; r.m[c * 4 + rr] = s; } return r; }
        M4 Ident() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
        M4 Translate(float x, float y, float z) { M4 r = Ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
        M4 Scale(float s) { M4 r = Ident(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
        M4 RotX(float a) { M4 r = Ident(); r.m[5] = cosf(a); r.m[6] = sinf(a); r.m[9] = -sinf(a); r.m[10] = cosf(a); return r; }
        M4 RotY(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[2] = -sinf(a); r.m[8] = sinf(a); r.m[10] = cosf(a); return r; }
        M4 RotZ(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[1] = sinf(a); r.m[4] = -sinf(a); r.m[5] = cosf(a); return r; }
        void Xform(const M4 &m, const float *p, float *o) {   // point
            for (int r = 0; r < 3; r++) o[r] = m.m[r] * p[0] + m.m[4 + r] * p[1] + m.m[8 + r] * p[2] + m.m[12 + r];
        }
        // PerspectiveCamera(38, aspect): depth 0..1 (deko3d), right-handed, looking down -z
        M4 Persp(float fovDeg, float aspect, float n, float f) {
            M4 r{}; const float t = 1.0f / tanf(fovDeg * PI / 360.0f);
            r.m[0] = t / aspect; r.m[5] = t; r.m[10] = f / (n - f); r.m[11] = -1; r.m[14] = n * f / (n - f);
            return r;
        }
        M4 LookAt(const float *e, const float *c) {   // camera.lookAt, up (0, 1, 0)
            float z[3] = { e[0] - c[0], e[1] - c[1], e[2] - c[2] }; float l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
            for (float &v : z) v /= l;
            float x[3] = { z[2], 0, -z[0] }; l = sqrtf(x[0] * x[0] + x[2] * x[2]); x[0] /= l; x[2] /= l;   // up x z
            float y[3] = { z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0] };
            M4 r = Ident();
            for (int i = 0; i < 3; i++) { r.m[i * 4 + 0] = x[i]; r.m[i * 4 + 1] = y[i]; r.m[i * 4 + 2] = z[i]; }
            r.m[12] = -(x[0] * e[0] + x[1] * e[1] + x[2] * e[2]);
            r.m[13] = -(y[0] * e[0] + y[1] * e[1] + y[2] * e[2]);
            r.m[14] = -(z[0] * e[0] + z[1] * e[1] + z[2] * e[2]);
            return r;
        }

        struct alignas(16) MeshUbo {            // shaders/mesh_vsh.glsl (std140)
            float mvp[16], model[16], base[4], mat[4], tint[4], emi[4], cam[4];
            float keyDir[4], keyCol[4], rimDir[4], rimCol[4], driftPos[4], driftCol[4], sky[4], ground[4], sh[9][4];
            float joyL[4], joyR[4];             // v40: the Switch's Joy-Con in the real controllers' colours (w: the palette cell, < 0 none)
        };
        static_assert(sizeof(MeshUbo) <= 0x200, "MeshUbo must fit the mesh UBO (Gpu.cpp kMeshUboSize)");
        const char *const kGroup[4] = { "top", "name", "hintsL", "hintsR" };
    }

    static void Harmonize(int i);
    // v90: the ring keeps its 8 places (the spacing Yass validated); the consoles standing take the first m_M ones, in order
    static int ModS(int x, int S) { return ((x % S) + S) % S; }
    int Wheel::AtSlot(int slot) const { const int k = ModS(slot, m_S); return k < m_M ? m_ring[k] : -1; }
    int Wheel::Sel() const { const int c = AtSlot((int)lroundf(m_target)); return c >= 0 ? c : m_ring[0]; }
    float Wheel::Slots() const { return (float)m_S; }
    // v93: a ring of more than 8: the spacing stays 45 degrees, the ones past the back fade into the fog
    float Wheel::Vis(float rel) const { return m_S <= N ? 1.0f : 1.0f - Smooth(3.45f, 4.15f, fabsf(rel)); }
    // the place a step goes to (+1 / -1): a full ring wraps over its empty places (6 or 7 consoles), a short one stops at its
    // ends (returns `t` itself)
    int Wheel::StepFrom(int t, int step) const {
        const int S = m_S, k = ModS(t, S), M = m_M;
        if (step > 0) { if (k + 1 < M) return t + 1; return Wraps() ? t + (S - k) : t; }
        if (k > 0) return t - 1;
        return Wraps() ? t - (S - (M - 1)) : t;
    }
    // the nearest place a console stands on (the finger, a throw): an empty place goes to the end it is nearer to
    int Wheel::ValidNear(float x, int dir) const {
        const int S = m_S, r = (int)lroundf(x), k = ModS(r, S), M = m_M;
        if (k < M) return r;
        if (!Wraps()) return x < (float)(M - 1) * 0.5f ? 0 : M - 1;   // (a short ring stays on its first turn)
        const int back = r - (k - (M - 1)), fwd = r + (S - k);
        if (dir > 0) return fwd;
        if (dir < 0) return back;
        return x - back <= fwd - x ? back : fwd;
    }
    float Wheel::Rel(int i) const {
        const float S = Slots(), d = m_rp[i] - m_cur;
        return fmodf(fmodf(d, S) + S + S / 2.0f, S) - S / 2.0f;
    }

    // ---- v90 - only the consoles that have an emulator (see Wheel.hpp) ----
    void Wheel::SetPresent(uint16_t mask, uint16_t fresh, float now, bool instant) {
        mask |= 1;                                        // the Switch, always
        mask &= (uint16_t)((1u << NC) - 1);
        m_want = mask;
        m_fresh = (uint16_t)((m_fresh | fresh) & mask & ~1u);
        if (instant) Relayout((uint16_t)(mask & ~m_fresh), now, true);
    }
    // the ring closes up (or opens) around `mask`: the front console stays at the front (the next one if it leaves), every
    // other one glides from where it stands to its new place
    void Wheel::Relayout(uint16_t mask, float now, bool instant) {
        mask |= 1;
        const int sel = Sel();
        float rel[NC];
        for (int i = 0; i < NC; i++) rel[i] = Rel(i);
        int ring[NC], M = 0;
        for (int o = 0; o < NC; o++) if (mask & (1u << consoles::kOrder[o])) ring[M++] = consoles::kOrder[o];   // (v93: the ring's order)
        int ps = 0;
        for (int o = 0; o < NC; o++) if (consoles::kOrder[o] == sel) ps = o;
        int idx = -1;
        for (int k = 0; k < NC && idx < 0; k++)           // the chosen one, else the next one standing
            for (int r = 0; r < M; r++) if (ring[r] == consoles::kOrder[(ps + k) % NC]) { idx = r; break; }
        if (idx < 0) idx = 0;
        const float off = m_cur - m_target;
        m_M = M; m_mask = mask; m_S = M > N ? M : N;      // (v93: more than 8 consoles: as many places)
        for (int r = 0; r < consoles::kMax; r++) m_ring[r] = r < M ? ring[r] : 0;
        m_target = (float)idx; m_cur = m_target + off; m_tweenTo = m_target; m_tweenFrom = m_cur;
        const float S1 = (float)m_S;
        for (int i = 0; i < NC; i++) {
            int r = -1;
            for (int k = 0; k < M; k++) if (ring[k] == i) r = k;
            const float here = m_cur + rel[i];
            if (r >= 0) {
                const float tgt = r + S1 * roundf((here - r) / S1);   // (its place on the ring, the nearest way round)
                m_rpT[i] = tgt;
                if (instant || m_pres[i] < 0.01f) { m_rp[i] = tgt; m_rpV[i] = 0; } else m_rp[i] = here;
                m_presT[i] = 1;
                if (instant) { m_pres[i] = 1; m_presV[i] = 0; }
            } else {                                      // it leaves: it fades where it stands
                m_rp[i] = m_rpT[i] = here; m_rpV[i] = 0;
                m_presT[i] = 0;
                if (instant) { m_pres[i] = 0; m_presV[i] = 0; }
            }
        }
        const int ns = m_ring[idx];
        if (ns != sel) {                                  // another console at the front: its name, its chord
            m_shownName = ns; m_nameSwapAt = -1; m_nameFadeFrom = instant ? 1.0f : 0.0f; m_nameFadeTo = 1; m_nameFadeT0 = now;
            m_lastJingle = ns;
            if (m_on && ns != m_chord) {
                int dd = CD(ns).sub - CD(m_chord).sub; while (dd > 6) dd -= 12; while (dd < -6) dd += 12;
                m_chord = ns;
                audio::SetChord(Voice(ns), 4.0f, (float)dd);
                Harmonize(ns);
            }
        }
    }
    void Wheel::PresenceTick(float now, float dt) {
        uint16_t want = (uint16_t)(m_want | 1);
        if (s_held >= 0 && s_held < NC) want |= (uint16_t)(1u << s_held);   // (a game still running in it: it stays)
        const uint16_t next = (uint16_t)((m_mask & want) | (want & ~m_fresh));
        const bool unseen = !m_on || PageAway() > 0.99f;
        const bool still = !m_moving && !m_drag && !m_focusOn && m_focus < 0.02f && m_diveT0 < 0 && m_warpTo < 0;
        if (next != m_mask && (unseen || still)) Relayout(next, now, unseen);
        // a new one arrives once the wheel is calm and on screen (the intro over), one after the other
        const uint16_t arrive = (uint16_t)(m_fresh & want & ~m_mask);
        if (arrive && !unseen && still && m_uiShown && now > m_settleAt && PageAway() < 0.01f && now >= m_nextArrive) {
            int i = NC;                                   // (v93: in the ring's order)
            for (int o = 0; o < NC && i == NC; o++) if (arrive & (1u << consoles::kOrder[o])) i = consoles::kOrder[o];
            if (i < NC) {
                m_fresh &= (uint16_t)~(1u << i);
                Relayout((uint16_t)(m_mask | (1u << i)), now, false);
                m_pres[i] = 0; m_presV[i] = 0;
                m_arriveT0[i] = now; m_arrLanded[i] = false;
                m_nextArrive = now + 1.5f;
                const float pan = fmaxf(-1.0f, fminf(1.0f, sinf(Rel(i) * kStep) * RING * 1.2f / 6)) * 0.7f;
                audio::Crystal(pan, 0.5f, audio::HarmonyNote(2, 79, 91));   // a crystal note of the chord now
                audio::Bell(audio::HarmonyNote(0, 79, 91), 0.03f, true, 0.22f);
                haptic::Tick(0.4f);
                // the wheel turns to it while it comes down: it lands at the front, its name under it (else a far place
                // on the ring would hide it)
                int ri = 0;
                for (int r = 0; r < m_M; r++) if (m_ring[r] == i) ri = r;
                int to = ri;
                if (Wraps()) { int d = ModS(ri - ModS((int)lroundf(m_target), m_S), m_S); if (d > m_S / 2) d -= m_S; to = (int)lroundf(m_target) + d; }
                if ((float)to != m_target) Turn(now, to, false);
                if (s_arrived) s_arrived(i, now);
            }
        }
        for (int i = 0; i < NC; i++) {
            Glide(m_rp[i], m_rpV[i], m_rpT[i], dt, 3.5f);
            Glide(m_pres[i], m_presV[i], m_presT[i], dt, m_presT[i] > 0.5f ? 2.6f : 4.5f);
            if (m_arriveT0[i] > 0) {
                const float age = now - m_arriveT0[i];
                if (!m_arrLanded[i] && age > Pace(1.7f) * 0.6f) {   // it sets down
                    m_arrLanded[i] = true;
                    audio::Thock(fmaxf(-1.0f, fminf(1.0f, sinf(Rel(i) * kStep) * RING * 1.2f / 6)) * 0.7f, 0.95f, 0.06f);
                    haptic::Tick(0.5f);
                }
                if (age > 4.0f) m_arriveT0[i] = -100;
            }
        }
    }

    // v29.2: the key (the console's root vs D, folded into +- 6) and the harmony (its chord) for every « ding »
    static void Harmonize(int i) {
        int d = CD(i).sub - 38; while (d > 6) d -= 12; while (d < -6) d += 12;
        audio::SetKey(d);
        audio::SetHarmony(CD(i).chord);
    }

    int Wheel::TabAt(float x, float y) const {
        if (fabsf(y - m_tabCy) > 30) return -2;
        for (int k = 0; k < kTabs; k++) if (x >= m_tabX[k] - 6 && x <= m_tabX[k] + m_tabW[k] + 6) return k;
        const Sprite *none = nullptr; (void)none;
        if (x >= 34 && x <= 300) return -1;               // the profile « Yass » (left of the bar)
        return -2;
    }

    // camera: front of the carousel, breathing (the finger / gyroscope will come later: ptr = 0)
    void Wheel::Camera(float now, float *eye, float *at) const {
        const float k = m_camT;
        eye[0] = m_ptr[0] * 0.8f; eye[1] = 2.5f + (1.25f - 2.5f) * k + sinf(now * 0.13f) * 0.05f - m_ptr[1] * 0.4f; eye[2] = 12.8f + (7.6f - 12.8f) * k;
        at[0] = m_ptr[0] * 0.22f; at[1] = 0.1f + (0.3f - 0.1f) * k; at[2] = -4 + 2 * k;   // v29: the gyroscope moves the camera (parallax)
    }

    // The consoles as spheres in the cubes' camera space. The cubes' camera (Entry.cpp) is the wheel's camera
    // with a 900 px focal instead of 1045 (fov 38 at 720 px): x and y are scaled so both land on the same pixels.
    int Wheel::Spheres(float (*out)[4], int max) const {
        if (!m_on) return 0;
        float eye[3], at[3];
        Camera(m_t, eye, at);
        const M4 view = LookAt(eye, at);
        const float kf = (360.0f / tanf(19.0f * PI / 180.0f)) / 900.0f;
        int n = 0;
        for (int i = 0; i < NC && n < max; i++) {
            const Slot &s = m_slot[i];
            if (s.appear < 0.5f || m_pres[i] < 0.5f) continue;   // (v90: a console not on the ring)
            const float d = Rel(i);
            if (Vis(d) < 0.5f) continue;                  // (v93: faded at the back of a long ring)
            const float ang = d * kStep, f01 = (cosf(ang) + 1) / 2;
            float p[3] = { sinf(ang) * RING * 1.2f - PageAway() * 16, 0.75f + sinf(m_t * 0.7f + i * 1.3f) * 0.08f + (1 - f01) * 0.2f, RZ + cosf(ang) * RING };
            const float fe = fminf(1.0f, fmaxf(0.0f, m_focus));
            float sc = (0.55f + 0.7f * powf(f01, 6)) * kLook[i].size * m_warpK;   // (v79: a Warp shrinks them)
            if (i == Sel()) { p[0] += (kFocusX - p[0]) * fe; p[1] += kFocusY * fe; p[2] += (kFocusZ - p[2]) * fe; sc *= 1 - 0.07f * fe; }
            else { sc *= 1 - fe; if (sc < 0.05f) continue; }
            float v[3];
            Xform(view, p, v);
            out[n][0] = v[0] * kf; out[n][1] = v[1] * kf; out[n][2] = -v[2]; out[n][3] = 0.85f * sc * kf;   // models: longest side 2
            n++;
        }
        return n;
    }

    // The mockup's dust (Points, additive, gl_PointSize = (1.5 + 2 fract(ph * 3.7)) * 10 / depth, a soft gaussian),
    // drawn as interface quads (ui_fsh mode 21). The wind carries them the same way as the consoles (v28: the
    // mockup had it backwards), and each one also orbits slowly at its own pace.
    int Wheel::Stars(float now, float light, UiVtx *out, int maxVtx, bool front) const {
        if (!g_starInit) InitStars();
        const float amt0 = (0.3f + 0.4f * m_wake) * (1 - 0.3f * m_night) * fminf(1.0f, fmaxf(0.0f, light));
        if (amt0 < 0.003f) return 0;
        // v81 - the theme: how many stars / comets show (Nuit profonde: few), and Chrome: silver glints, chrome comets
        const float chr = theme::Chrome(), amt = amt0 * theme::StarsK(), camt = amt0 * theme::CometsK();
        const float sr = 0.5225f + (0.86f - 0.5225f) * chr, sg = 0.589f + (0.90f - 0.589f) * chr, sb = 0.9025f + (1.0f - 0.9025f) * chr;
        float eye[3], at[3];
        Camera(now, eye, at);
        const M4 view = LookAt(eye, at);
        const float f = 1.0f / tanf(19.0f * PI / 180.0f), aspect = 16.0f / 9.0f;
        static const float kCx[6] = { -1, 1, 1, -1, 1, -1 }, kCy[6] = { -1, -1, 1, -1, 1, 1 };
        int n = 0;
        for (int i = 0; i < kStars && n + 6 <= maxVtx; i++) {
            const Star &q = g_star[i];
            const float al = q.a0 - q.w * now - m_windDrift * q.cp;
            const float p[3] = { sinf(al) * q.r + sinf(now * 0.11f + q.ph) * 0.5f,
                                 q.y + sinf(now * 0.07f + q.ph * 1.7f) * 0.3f + m_windLift * q.cp * 0.4f,
                                 RZ + cosf(al) * q.r + cosf(now * 0.09f + q.ph) * 0.5f };
            float v[3];
            Xform(view, p, v);
            const float depth = -v[2];
            if (depth < 0.25f || (depth < 7.2f) != front) continue;
            const float sx = 640 + f / aspect * v[0] / depth * 640, sy = 360 - f * v[1] / depth * 360;
            const float fr = q.ph * 3.7f - floorf(q.ph * 3.7f);
            const float h = fminf(150.0f, (1.5f + 2.0f * fr) * 10.0f / depth) * 0.5f;
            if (sx + h < 0 || sx - h > 1280 || sy + h < 0 || sy - h > 720) continue;
            const float tw = 0.5f + 0.5f * sinf(now * 0.8f + q.ph * 4);
            const float tw4 = tw * tw * tw * tw;                 // (v81 Chrome: a glint - dark, then a sharp flash)
            const float a = ((0.35f + 0.65f * tw * tw) + ((0.10f + 1.3f * tw4 * tw4) - (0.35f + 0.65f * tw * tw)) * chr) * amt * Smooth(0.25f, 1.0f, depth);
            for (int c = 0; c < 6; c++) {
                UiVtx &o = out[n++];
                o.x = sx + kCx[c] * h; o.y = sy + kCy[c] * h; o.u = kCx[c]; o.v = kCy[c];
                o.r = sr; o.g = sg; o.b = sb; o.a = a;
                o.mode = 21; o.p1 = o.p2 = o.p3 = 0;
            }
        }
        // v29 - « a few small coloured streaks of light wandering » (the vision, the PS2 boot of the moodboard): three
        // comets slowly orbiting the carousel, each with a fading trail of 14 dots sampled back in time
        static const float kCom[3][7] = {   // radius x, radius z, height, speed (rad/s), phase, colour r g b packed below
            { 7.5f, 6.0f, 1.2f, 0.32f, 0.0f, 0, 0 }, { 9.0f, 7.0f, -0.6f, -0.26f, 2.2f, 0, 0 }, { 6.0f, 8.5f, 2.4f, 0.21f, 4.1f, 0, 0 } };
        static const float kComCol0[3][3] = { { 0.95f, 0.38f, 0.42f }, { 0.38f, 0.92f, 0.52f }, { 0.86f, 0.38f, 0.88f } };
        static const float kComChr[3][3] = { { 0.70f, 0.92f, 1.0f }, { 0.94f, 0.95f, 1.0f }, { 0.80f, 0.78f, 1.0f } };   // v81 Chrome
        float kComCol[3][3];
        for (int i = 0; i < 3; i++) for (int c = 0; c < 3; c++) kComCol[i][c] = kComCol0[i][c] + (kComChr[i][c] - kComCol0[i][c]) * chr;
        // v70 (the PS2 look, as Brume Overlay): a ~2 s trail that ages into blue (red fades first, then green: f^3, f^2, f, the
        // PS2's clock), drawn as stretched soft glows from one point to the next - a continuous streak, not a row of dots
        constexpr int kPts = 28;                                // every 0.075 s
        for (int cmt = 0; cmt < 3; cmt++) {
            const float *c = kCom[cmt];
            float sx[kPts], sy[kPts], dp[kPts];
            bool ok[kPts];
            for (int k = 0; k < kPts; k++) {
                const float tt = now - k * 0.075f, an = c[4] + c[3] * tt - m_windDrift * 0.8f;
                const float p[3] = { sinf(an) * c[0], 0.6f + c[2] + sinf(tt * 0.37f + c[4]) * 0.9f, RZ + cosf(an) * c[1] };
                float v[3];
                Xform(view, p, v);
                dp[k] = -v[2];
                ok[k] = dp[k] >= 0.4f && (dp[k] < 7.2f) == front;
                sx[k] = ok[k] ? 640 + f / aspect * v[0] / dp[k] * 640 : 0; sy[k] = ok[k] ? 360 - f * v[1] / dp[k] * 360 : 0;
            }
            for (int k = 0; k < kPts && n + 6 <= maxVtx; k++) {
                if (!ok[k] || (k > 0 && !ok[k - 1])) continue;
                const float fk = 1.0f - k / (float)kPts, h = fminf(60.0f, (k == 0 ? 3.2f : 2.2f) * 10.0f / dp[k]) * 0.5f * (k == 0 ? 1.0f : 0.45f + 0.55f * fk);
                float mx = sx[k], my = sy[k], ux = 1, uy = 0, L = h;    // the head: a round glow; the trail: from the point before
                if (k > 0) {
                    const float dx = sx[k] - sx[k - 1], dy = sy[k] - sy[k - 1], len = sqrtf(dx * dx + dy * dy);
                    if (len > 0.01f) { ux = dx / len; uy = dy / len; }
                    mx = (sx[k] + sx[k - 1]) * 0.5f; my = (sy[k] + sy[k - 1]) * 0.5f; L = len * 0.5f + h;
                }
                if (mx + L < 0 || mx - L > 1280 || my + L < 0 || my - L > 720) continue;
                const float a = camt * (k == 0 ? 0.9f : 0.75f * fk) * Smooth(0.4f, 1.2f, dp[k]);
                const float cr = kComCol[cmt][0] * (k == 0 ? 1.0f : fk * fk * fk), cg = kComCol[cmt][1] * (k == 0 ? 1.0f : fk * fk), cb = kComCol[cmt][2] * (k == 0 ? 1.0f : fk);
                const float cbb = fmaxf(cb, (k == 0 ? 0.0f : 0.55f * fk));   // (the old trail ends in blue, even for a red comet)
                for (int q = 0; q < 6; q++) {
                    UiVtx &o = out[n++];
                    o.x = mx + ux * L * kCx[q] - uy * h * kCy[q]; o.y = my + uy * L * kCx[q] + ux * h * kCy[q]; o.u = kCx[q]; o.v = kCy[q];
                    o.r = cr; o.g = cg; o.b = cbb; o.a = a;
                    o.mode = 21; o.p1 = k > 0 ? 1.0f : 0.0f; o.p2 = k > 0 ? fminf(1.0f, 2.0f * h / L) : 0.0f; o.p3 = 0;
                }
            }
            // v81 - Chrome: the comet leaves chrome sparkles - little four-pointed glints that stay where it passed, flash
            // and fade (~1.3 s); each from a past moment of its path (a fixed place in the room, never sliding along)
            if (chr > 0.01f && camt > 0.003f) {
                constexpr float kStep = 0.16f;
                const float base = floorf(now / kStep);
                for (int j = 1; j <= 8 && n + 12 <= maxVtx; j++) {
                    const float tj = (base - j) * kStep, age = now - tj;
                    float hs = sinf(tj * 12.9898f + cmt * 78.233f) * 43758.5453f; hs -= floorf(hs);
                    float hs2 = sinf(tj * 39.346f + cmt * 11.135f) * 24634.6345f; hs2 -= floorf(hs2);
                    const float an = c[4] + c[3] * tj - m_windDrift * 0.8f;
                    const float q[3] = { sinf(an) * c[0] + (hs - 0.5f) * 0.6f, 0.6f + c[2] + sinf(tj * 0.37f + c[4]) * 0.9f + (hs2 - 0.5f) * 0.5f,
                                         RZ + cosf(an) * c[1] };
                    float v[3];
                    Xform(view, q, v);
                    const float dq = -v[2];
                    if (dq < 0.4f || (dq < 7.2f) != front) continue;
                    const float qx = 640 + f / aspect * v[0] / dq * 640, qy = 360 - f * v[1] / dq * 360;
                    const float life = 1.0f - age / (9 * kStep);
                    if (life <= 0) continue;
                    const float flash = 0.5f + 0.5f * sinf(age * 17.0f + hs * 6.2831853f);
                    const float a = camt * chr * life * life * (0.25f + 0.75f * flash * flash) * 1.1f * Smooth(0.4f, 1.2f, dq);
                    const float L = fminf(26.0f, (4.0f + 4.0f * hs2) * 10.0f / dq), w = fmaxf(0.9f, L * 0.13f);
                    if (qx + L < 0 || qx - L > 1280 || qy + L < 0 || qy - L > 720) continue;
                    for (int arm = 0; arm < 2; arm++)          // the cross: one arm across, one up
                        for (int qd = 0; qd < 6; qd++) {
                            UiVtx &o = out[n++];
                            const float ax = arm == 0 ? L : w, ay = arm == 0 ? w : L;
                            o.x = qx + ax * kCx[qd]; o.y = qy + ay * kCy[qd];
                            o.u = arm == 0 ? kCx[qd] : kCy[qd]; o.v = arm == 0 ? kCy[qd] : kCx[qd];
                            o.r = 0.86f; o.g = 0.93f; o.b = 1.0f; o.a = a;
                            o.mode = 21; o.p1 = 1.0f; o.p2 = 0.9f; o.p3 = 0;
                        }
                }
            }
        }
        // v81 - Chrome: a thin chrome ring around the chosen console, slightly tilted, turning slowly; a glint runs along it
        // and its side towards the light shines. Its near half is drawn in front of the console, the far half behind.
        // It stays at the front place (where the chosen one stands) and takes the size of the console arriving there: the
        // wheel turns through it, it never jumps from one console to the next.
        if (chr > 0.01f && m_on && PageAway() < 0.999f) {
            const float fe = fminf(1.0f, fmaxf(0.0f, m_focus));
            const float fc = m_cur - floorf(m_cur);
            const int r0 = (int)floorf(m_cur), j0 = AtSlot(r0), j1 = AtSlot(r0 + 1);   // (v90: an empty place: the ring fades there)
            const int i0 = j0 >= 0 ? j0 : (j1 >= 0 ? j1 : 0), i1 = j1 >= 0 ? j1 : i0;
            const float app = (j0 >= 0 && j1 >= 0) ? fminf(m_slot[i0].appear * m_pres[i0], m_slot[i1].appear * m_pres[i1]) : 0.0f;
            const float ringA = chr * fminf(1.0f, fmaxf(0.0f, light)) * (1 - PageAway()) * (1 - fe) * fminf(1.0f, fmaxf(0.0f, app * 2 - 1));
            if (ringA > 0.003f) {
                const float cx = -PageAway() * 16, cy = 0.75f + 0.06f * sinf(now * 0.5f), cz = RZ + RING;
                const float R = 1.32f * 1.25f * (kLook[i0].size + (kLook[i1].size - kLook[i0].size) * fc);
                const float tx = 0.30f + 0.05f * sinf(now * 0.31f), tz = 0.12f * sinf(now * 0.23f), spin = now * 0.22f;
                const float ctx = cosf(tx), stx = sinf(tx), ctz = cosf(tz), stz = sinf(tz);
                constexpr int kSeg = 56;
                float px[kSeg + 1], py[kSeg + 1], pd[kSeg + 1], pzw[kSeg + 1];
                bool okp[kSeg + 1];
                for (int k = 0; k <= kSeg; k++) {
                    const float th = k * (2 * PI / kSeg) + spin;
                    const float x = R * cosf(th), y = 0, z = R * sinf(th);
                    const float y1 = y * ctx - z * stx, z1 = y * stx + z * ctx;          // tilt: its front dips
                    const float x2 = x * ctz - y1 * stz, y2 = x * stz + y1 * ctz;        // and rocks a little
                    const float q[3] = { cx + x2, cy + y2, cz + z1 };
                    float v[3];
                    Xform(view, q, v);
                    pd[k] = -v[2]; pzw[k] = z1;
                    okp[k] = pd[k] > 0.4f;
                    px[k] = okp[k] ? 640 + f / aspect * v[0] / pd[k] * 640 : 0; py[k] = okp[k] ? 360 - f * v[1] / pd[k] * 360 : 0;
                }
                for (int k = 0; k < kSeg && n + 6 <= maxVtx; k++) {
                    if (!okp[k] || !okp[k + 1] || ((pzw[k] + pzw[k + 1] > 0) != front)) continue;
                    const float th = (k + 0.5f) * (2 * PI / kSeg) + spin;
                    const float run = 0.5f + 0.5f * cosf(th - spin - now * 0.9f), side = 0.5f + 0.5f * cosf(th - 2.3f);
                    const float run8 = run * run * run * run * run * run * run * run, side4 = side * side * side * side;
                    const float bright = 0.20f + 1.1f * run8 * run8 * run8 + 0.45f * side4;
                    const float dx = px[k + 1] - px[k], dy = py[k + 1] - py[k], len = sqrtf(dx * dx + dy * dy);
                    if (len < 0.01f) continue;
                    const float ux = dx / len, uy = dy / len, h = fmaxf(1.0f, 2.6f * 7.6f / pd[k]), Lh = len * 0.5f + h;
                    const float mx = (px[k] + px[k + 1]) * 0.5f, my = (py[k] + py[k + 1]) * 0.5f;
                    for (int qd = 0; qd < 6; qd++) {
                        UiVtx &o = out[n++];
                        o.x = mx + ux * Lh * kCx[qd] - uy * h * kCy[qd]; o.y = my + uy * Lh * kCx[qd] + ux * h * kCy[qd];
                        o.u = kCx[qd]; o.v = kCy[qd];
                        o.r = 0.80f; o.g = 0.87f; o.b = 0.97f; o.a = ringA * bright * 0.55f;
                        o.mode = 21; o.p1 = 1.0f; o.p2 = fminf(1.0f, 2.0f * h / Lh); o.p3 = 0;
                    }
                }
            }
        }
        return n;
    }

    // .enter{transition:transform 1.1s cubic-bezier(.2,1.25,.35,1), opacity .8s ease}
    float Wheel::GroupT(const Trans &t, float now) const { return t.from + (t.to - t.from) * Bezier(0.2f, 1.25f, 0.35f, 1.0f, (now - t.t0) / 1.1f); }
    float Wheel::GroupO(const Trans &t, float now) const { return t.from + (t.to - t.from) * Bezier(0.25f, 0.1f, 0.25f, 1.0f, (now - t.t0) / 0.8f); }

    // unlock(): setChord(CONSOLES[sel], 2.5); the consoles fall one by one, from the back to the front, the
    // selected one last; the camera leaves while the last one lands; the interface comes when it is ~90 % there.
    void Wheel::Unlock(float now, bool fromFinale) {
        if (m_on) return;
        m_on = true;
        const int sel = Sel();
        m_chord = sel;
        audio::SetChord(Voice(sel), 2.5f);
        Harmonize(sel);
        const float lead = fromFinale ? 0.4f : 0.0f;
        // v90: only the consoles on the ring fall (the others make no sound); the places count on the ring
        const int M = m_M, S = m_S, t0 = ModS((int)lroundf(m_target), S);
        int order[NC]; float key[NC] = {};
        for (int r = 0; r < M; r++) {
            int dist, side;
            if (Wraps()) { const int d = ModS(r - t0, S); dist = d < S - d ? d : S - d; side = d <= S / 2 ? 1 : -1; }
            else { dist = r > t0 ? r - t0 : t0 - r; side = r >= t0 ? 1 : -1; }
            order[r] = m_ring[r]; key[m_ring[r]] = -dist * 10.0f + (side < 0 ? 0.0f : 1.0f);   // sort((a,b)=>b.dist-a.dist||a.side-b.side)
        }
        for (int a = 1; a < M; a++) for (int b = a; b > 0 && key[order[b]] < key[order[b - 1]]; b--) { const int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t; }
        for (Slot &s : m_slot) { s.fallStart = -1; s.landedAt = 0; s.popped = true; s.hero = false; }
        for (int idx = 0; idx < M; idx++) {
            Slot &s = m_slot[order[idx]];
            s.fallStart = now + lead + FALL_START + idx * FALL_GAP; s.landedAt = 0; s.popped = false;
            s.hero = idx == M - 1; s.fallIdx = idx;
        }
        m_lastLand = lead + FALL_START + (M - 1) * FALL_GAP + FALL_DUR;
        m_camMoveT0 = -1; m_showAt = now + m_lastLand - FALL_DUR * 0.25f;   // at(lastLand - FALL_DUR*1000*.25, ...)
        m_uiShown = false; m_lastJingle = -1; m_settleAt = 1e9;
        m_shownName = sel;
    }

    void Wheel::ShowUi(float now) {                 // showUI(): INTRO, 110 ms apart, then lockIn()
        if (m_uiShown) return;
        m_uiShown = true;
        for (int i = 0; i < 4; i++) m_groupShowAt[i] = now + i * 0.11f;
        const ConsoleDef &c = CD(m_chord);
        audio::SoftLow(c.sub + 12, 0.17f, 1.1f);    // lockIn(): softLow + thock + the chord, up
        audio::Thock(0, 0.75f, 0.16f);
        for (int i = 0; i < 4; i++) audio::Bell(c.chord[i % 4] + 24, 0.03f, true, 0.04f + i * 0.11f);   // v38.1: the v12 intro (soft, wet)
        m_settleAt = now + 1.2f;
    }

    // relock(): the interface goes, the consoles fade, the camera goes back (the lock screen does the rest)
    void Wheel::Relock(float now) {
        if (!m_on) return;
        m_on = false;
        for (Slot &s : m_slot) { s.fallStart = -1; s.landedAt = 0; }
        m_camMoveT0 = -1; m_showAt = -1; m_uiShown = true;
        for (int i = 0; i < 4; i++) { m_groupShowAt[i] = -1; m_group[i].Set(now, 0, GroupT(m_group[i], now)); }
        audio::Play(audio::Tick1700, 0, 1.0f);       // pressSound(0, .08, 1700)
        m_page = 0; m_pageFrom = 0; m_pg = 0;        // relock: back to the games page (mockup v24)
        m_warpTo = -1; m_warpInT0 = -100; m_warpK = 1;   // v79
    }

    // setPage(p) (mockup v24): the swish from the side we go to, the soft movement note, the gust
    void Wheel::SetPage(float now, int p) {
        if (!m_on) return;
        if (p < 0 || p >= kPages) { audio::Thock(p < 0 ? -0.6f : 0.6f, 0.8f, 0.06f); return; }
        if (m_warpTo >= 0) { m_warpTo = -1; m_warpFrom = m_warpK; m_warpInT0 = now; }   // v79: a Warp on its way out: it stops, all comes back
        if (p == m_page) return;
        const int dir = p > m_page ? 1 : -1;
        m_pageFrom = m_pg; m_page = p; m_pageT0 = now;
        audio::Play(dir > 0 ? audio::PageR : audio::PageL);   // v29: a tuned breath across the scene + a glass harp (Yass: a new sound)
        haptic::Thump(0.6f);
        m_windVel += dir * 2.4f;                     // the stars
        m_windKick += dir * 1.6f;                    // the cubes
    }

    // v79 - Warp (see Wheel.hpp): out (WarpK falls to 0 in `out` s), the page changes one short step from its place (unseen:
    // the pages in between are never drawn), then it glides in while WarpK rises again (Update)
    float Wheel::Warp(float now, int p, float out) {
        if (!m_on || p < 0 || p >= kPages) return now;
        if (p == m_page && m_warpTo < 0) return now;
        m_warpTo = p; m_warpT0 = now; m_warpOut = Pace(out > 0.05f ? out : 0.05f); m_warpFrom = m_warpK;
        m_pageDrag = false;
        return now + m_warpOut;
    }
    void Wheel::Snap(int i) {
        if (i < 0 || i >= NC) return;
        int ri = -1;                                     // v90: its place on the ring (not there: nothing to do)
        for (int r = 0; r < m_M; r++) if (m_ring[r] == i) ri = r;
        if (ri < 0) return;
        if (Wraps()) {
            int d = ModS(ri - ModS((int)lroundf(m_target), m_S), m_S);
            if (d > m_S / 2) d -= m_S;
            m_target += (float)d;
        } else m_target = (float)ri;
        m_cur = m_tweenTo = m_target; m_curV = 0; m_tweenT0 = -1; m_moving = false;
        m_shownName = i; m_nameSwapAt = -1; m_nameFadeFrom = m_nameFadeTo = 1;
        m_lastJingle = i;                                // (no tune: nobody saw it turn)
        if (i != m_chord) {                              // the soundscape slides to its chord, as a turn would
            int dd = CD(i).sub - CD(m_chord).sub; while (dd > 6) dd -= 12; while (dd < -6) dd += 12;
            m_chord = i;
            audio::SetChord(Voice(i), 4.0f, (float)dd);
            Harmonize(i);
        }
    }

    // rotate(step): ease-out 0.6 s, the whoosh from the side we turn to, the soft movement note,
    // setChord(CONSOLES[i], 4), the name fades out and back in with the new one after 300 ms
    void Wheel::Rotate(float now, int step) {
        if (!m_on) return;
        // v90: the next console standing (over the empty places); a short ring has two ends - a soft knock, like the last page
        const int t = (int)lroundf(m_target), to = StepFrom(t, step > 0 ? 1 : -1);
        if (to == t) { audio::Thock(step < 0 ? -0.6f : 0.6f, 0.8f, 0.06f); haptic::Tick(0.4f); return; }
        Turn(now, to, true);
    }
    // the turn itself (Rotate; v90: also a new console's arrival - the wheel brings it to the front, quietly: its own sounds)
    void Wheel::Turn(float now, int to, bool sounds) {
        const int dir = (float)to > m_target ? 1 : -1;
        m_target = (float)to;
        m_moving = true;
        m_tweenFrom = m_cur; m_tweenTo = m_target; m_tweenT0 = now;
        const int i = Sel();
        if (sounds) {
            audio::PlaySweep(audio::Whoosh, dir * 0.7f, -dir * 0.6f, 1.3f);
            int m = CD(i).chord[3] + 12; while (m > 79) m -= 12; while (m < 72) m += 12;   // navSound
            audio::Nav(m, dir * 0.25f);
            haptic::Tick(0.8f);
        }
        {   // v29.1: the soundscape slides from the old root to the new one (folded into +- 6 semitones)
            int d = CD(i).sub - CD(m_chord).sub; while (d > 6) d -= 12; while (d < -6) d += 12;
            m_chord = i;
            audio::SetChord(Voice(i), 4.0f, (float)d);
            Harmonize(i);
        }
        m_nameFadeFrom = 1; m_nameFadeTo = 0; m_nameFadeT0 = now;   // .fade (opacity 0, blur 4px), .45s
        m_nameSwapAt = now + 0.3f; m_nameSwapTo = i;
        m_settleAt = now + 1.3f;
    }

    void Wheel::DragBegin(float now) {
        if (!m_on) return;
        m_drag = true; m_dragFrom = m_cur; m_tweenT0 = -1; m_moving = true; m_curV = 0;
        (void)now;
    }
    void Wheel::DragTo(float now, float steps) {
        if (!m_drag) return;
        m_cur = m_dragFrom + steps;
        if (!Wraps()) {                                  // v90: a short ring's ends hold the finger back
            const float hi = (float)(m_M - 1);
            if (m_cur < 0) m_cur *= 0.3f; else if (m_cur > hi) m_cur = hi + (m_cur - hi) * 0.3f;
        }
        const float tgt = (float)ValidNear(m_cur, 0);   // (v90: never an empty place)
        if (tgt != m_target) {                                   // the finger brings the next console to the middle
            const int dir = tgt > m_target ? 1 : -1;
            m_target = tgt;
            const int i = Sel();
            int m = CD(i).chord[3] + 12; while (m > 79) m -= 12; while (m < 72) m += 12;   // navSound
            audio::Nav(m, dir * 0.25f);
            haptic::Tick(0.6f);
            m_nameFadeFrom = 1; m_nameFadeTo = 0; m_nameFadeT0 = now; m_nameSwapAt = now + 0.3f; m_nameSwapTo = i;
        }
    }
    void Wheel::DragEnd(float now, float v) {
        if (!m_drag) return;
        m_drag = false;
        // v36 (Yass): throw it - it keeps the finger's speed and settles on the next console (a few, thrown hard)
        const float before = m_target;
        m_target = FlickTarget(m_cur, v, 6.0f);
        if (AtSlot((int)m_target) < 0 || (!Wraps() && (m_target < 0 || m_target > m_M - 1)))   // (v90: never an empty place)
            m_target = (float)(Wraps() ? ValidNear(m_target, v > 0 ? 1 : -1) : (m_target < 0 ? 0 : m_M - 1));
        m_curV = v;
        m_tweenFrom = m_cur; m_tweenTo = m_target; m_tweenT0 = now; m_moving = true;
        if (m_target != before) {
            const int i = Sel();
            m_nameFadeFrom = 1; m_nameFadeTo = 0; m_nameFadeT0 = now; m_nameSwapAt = now + 0.3f; m_nameSwapTo = i;
            haptic::Tick(0.6f);
        }
        const int i = Sel();
        if (i != m_chord) {                                      // the chord slides only once, when the finger lets go
            int d = CD(i).sub - CD(m_chord).sub; while (d > 6) d -= 12; while (d < -6) d += 12;
            m_chord = i;
            audio::SetChord(Voice(i), 4.0f, (float)d);
            Harmonize(i);
        }
        m_settleAt = now + 1.0f;
    }
    void Wheel::PageDragBegin() { if (!m_on || m_warpTo >= 0) return; m_pageDrag = true; m_pageDragFrom = m_pg; m_pgV = 0; }   // (v79: not during a Warp)
    void Wheel::PageDragTo(float pages) {
        if (!m_pageDrag) return;
        m_pg = fmaxf(0.0f, fminf((float)(kPages - 1), m_pageDragFrom + pages));
    }
    void Wheel::PageDragEnd(float now) {
        if (!m_pageDrag) return;
        m_pageDrag = false;
        const int p = (int)lroundf(m_pg);
        m_pageFrom = m_pg; m_pageT0 = now;
        if (p != m_page) {
            const int dir = p > m_page ? 1 : -1;
            m_page = p;
            audio::Play(dir > 0 ? audio::PageR : audio::PageL);
            haptic::Thump(0.5f);
            m_windVel += dir * 2.4f; m_windKick += dir * 1.6f;
        }
    }

    // openConsole() (v22; the games screen comes next): the console jumps, a breath, the glass tick, two notes
    void Wheel::PressA(float now) {
        if (!m_on) return;
        haptic::Thump(0.8f);                           // v31: no jump any more - the console glides (Games::Open)
        const ConsoleDef &c = CD(m_chord);
        audio::Play(audio::Dive);
        audio::Play(audio::Tick2400);
        audio::SoftLow(c.sub + 12, 0.13f, 0.6f);
        audio::Bell(c.chord[0] + 12, 0.07f, false, 0.03f);
        audio::Bell(c.chord[2] + 12, 0.04f, true, 0.1f);
        (void)now;
    }

    void Wheel::Update(float now, float dt, float hour) {
        m_t = now;
        // camera: wide view while they fall, then the glide; back when locked
        if (m_on) {
            if (m_showAt > 0 && now >= m_showAt && m_camMoveT0 < 0) {
                m_camMoveT0 = now;
                // v38.2 (Yass: « le pshhht fait mal aux oreilles »): no more swish of air when the camera glides - only the deep note
                audio::SoftLow(CD(m_chord).sub + 12, 0.09f, CAM_DUR);
            }
            if (m_camMoveT0 >= 0) { m_camT = camCurve(now - m_camMoveT0); if (m_camT >= 0.9f && !m_uiShown) ShowUi(now); }
        } else m_camT += (0 - m_camT) * fminf(1, dt * 1.4f);
        for (int i = 0; i < 4; i++) if (m_groupShowAt[i] > 0 && now >= m_groupShowAt[i]) { m_groupShowAt[i] = -1; m_group[i].Set(now, 1, GroupT(m_group[i], now)); }
        // the carousel
        const float prev = m_cur;
        if (m_tweenT0 >= 0) {   // v35: the glide (it speeds up, then brakes for long; pressing again keeps its speed)
            Glide(m_cur, m_curV, m_tweenTo, dt, 6.0f);
            if (fabsf(m_cur - m_tweenTo) < 0.0015f && fabsf(m_curV) < 0.01f) { m_cur = m_tweenTo; m_curV = 0; m_tweenT0 = -1; m_moving = false; }
        }
        const float spin = (m_cur - prev) / fmaxf(dt, 0.001f);
        // the pages (v28): ease-out .7 s; a page change blows a gust the same way (mockup: windVel += dir * 2.4)
        // v79 - a Warp: out, then the page changes one short step from its place (the pages in between are never crossed), in
        if (m_warpTo >= 0) {
            m_warpK = m_warpFrom * (1 - EaseGlide((now - m_warpT0) / m_warpOut));
            if (now >= m_warpT0 + m_warpOut) {
                const int from = m_page, p = m_warpTo;
                m_warpTo = -1; m_warpK = 0; m_warpFrom = 0; m_warpInT0 = now;
                m_page = p; m_pg = (float)p + (from > p ? 0.12f : (from < p ? -0.12f : 0.0f)); m_pgV = 0; m_pageFrom = m_pg; m_pageT0 = now;
                m_focus = m_focusOn ? 1.0f : 0.0f; m_focusV = 0;   // (the console is where it should be: no glide from elsewhere)
                const int dir = p > from ? 1 : -1;
                audio::Play(dir > 0 ? audio::PageR : audio::PageL, 0, 0.7f);
                m_windVel += dir * 1.6f; m_windKick += dir * 1.0f;
            }
        } else if (m_warpInT0 > -50) {
            m_warpK = m_warpFrom + (1 - m_warpFrom) * EaseGlide((now - m_warpInT0) / Pace(0.55f));
            if (now - m_warpInT0 >= Pace(0.55f)) { m_warpK = 1; m_warpInT0 = -100; }
        } else m_warpK = 1;
        if (!m_pageDrag) Glide(m_pg, m_pgV, (float)m_page, dt, 5.5f);   // v35: the pages glide too
        m_windKick *= expf(-dt * 3.0f);
        {   // v34 - the glide to the left: ease out expo over .75 s (Yass: the same motion everywhere); its speed leans
            // the console into the move, through a soft filter (no jerk at the start)
            Glide(m_focus, m_focusV, m_focusOn ? 1.0f : 0.0f, dt, 5.0f);   // v35: the glide (accelerates, then brakes long)
        }
        m_spin = spin + m_windKick;
        for (int k = 0; k < 2; k++) Glide(m_stkS[k], m_stkV[k], m_stk[k] + m_touchTilt[k], dt, 6.0f);   // v36: the stick + the finger, glided
        // the wind: the rotation carries what floats, then everything drifts back to its own path
        m_windVel += spin * dt * 1.6f; m_windVel *= expf(-dt * 1.8f); m_windDrift += m_windVel * dt; m_windDrift *= expf(-dt * 0.35f);
        m_windLift += (fminf(1, fabsf(spin) * 0.6f) - m_windLift) * fminf(1, dt * 2);
        const int sel = Sel();
        if (m_on && !m_moving && now > m_settleAt && m_lastJingle != sel) {      // jingle(i): the placeholder banner tune
            m_lastJingle = sel;
            audio::Duck(0.5f, 0.5f);
            static const int pat[7] = { 0, 1, 2, 3, 2, 1, 3 };
            static const float times[7] = { 0, 0.3f, 0.6f, 0.9f, 1.5f, 1.8f, 2.1f };
            for (int j = 0; j < 7; j++) audio::Bell(CD(sel).chord[pat[j]] + 12 + (j == 3 ? 12 : 0), 0.045f, true, 0.3f + times[j]);
            m_duckBackAt = now + 3.5f;
        }
        if (m_duckBackAt > 0 && now >= m_duckBackAt) { m_duckBackAt = -1; audio::Duck(1, 1.4f); }
        if (m_nameSwapAt > 0 && now >= m_nameSwapAt) { m_nameSwapAt = -1; m_shownName = m_nameSwapTo; m_nameFadeFrom = 0; m_nameFadeTo = 1; m_nameFadeT0 = now; }
        // colours: very dark; the console only tints the diffuse light a little
        float tmp[3], blue[3]; if (m_on) Lin(consoles::kDefs[sel].tint, tmp); else Amb(0x1c2f9c, tmp); Amb(0x1c2f9c, blue);   // v29: the ambient colour
        Lerp3(tmp, blue, 0.75f); Lerp3(m_tint, tmp, fminf(1, dt * 0.8f));
        m_wake = m_on ? 1.0f : m_wake;                                   // (the lock screen drives it while locked)
        m_night = hour < 5 || hour > 22 ? 1 : hour < 7 ? (7 - hour) / 2 : hour > 20 ? (hour - 20) / 2 : 0;
        PresenceTick(now, dt);                                           // v90: the consoles that come and go
        // the slots
        for (int i = 0; i < NC; i++) {
            Slot &s = m_slot[i];
            if (m_on && s.fallStart > 0) {
                const float fa = clamp01((now - s.fallStart) / FALL_DUR);
                s.appear = fa > 0 ? 1 : 0;
                const float d = Rel(i);
                const float x = sinf(d * kStep) * RING * 1.2f, px = fmaxf(-1, fminf(1, x / 6));
                if (fa > 0 && !s.popped) {                                // popNote(fallIdx, pan, hero)
                    s.popped = true;
                    // v38.1 (Yass: « reprends le son de la toute première maquette »): v12 - each console appears with a very
                    // soft bell of the chord, far away in the reverb (no more dry plinks rising up to C7)
                    audio::Bell(CD(m_chord).chord[s.fallIdx % 4] + 19, 0.02f, true, 0.0f);
                    (void)px;
                }
                if (fa >= 0.9f && !s.landedAt) {                          // the console sets down
                    s.landedAt = now;
                    audio::Thock(px * 0.7f, s.hero ? 0.85f : 0.95f + (s.fallIdx % 3) * 0.05f, s.hero ? 0.11f : 0.06f);
                    if (s.hero) haptic::Thump(0.7f); else haptic::Tick(0.5f);
                    if (s.hero) audio::SoftLow(CD(m_chord).sub + 12, 0.1f, 0.9f);
                }
            } else s.appear = m_on ? fminf(1, s.appear + dt / 1.2f) : fmaxf(0, s.appear - dt * 1.8f);
            if (s.jump > 0) s.jump = fmaxf(0, s.jump - dt * 1.8f);
        }
    }

    void Wheel::DrawMeshes(Gpu &gpu, const Assets &a, const Target &haze, float now) {
        s_heldK = 0;                                  // v89: (set again below if the held console is drawn)
        for (auto &l : m_lbl) l[3] = 0;               // v93: (the blocks drawn this frame set their name again)
        bool any = false;
        for (const Slot &s : m_slot) any = any || s.appear > 0.001f;
        if (!any || PageAway() > 0.999f) return;      // on another page the consoles are off screen: nothing to draw
        const float awayX = PageAway() * 16;          // v28: they leave to the left (mockup: x -= pageAway * 16)
        const float t = now;
        float eye[3], at[3];
        Camera(now, eye, at);
        const M4 vp = Mul(Persp(38, (float)gpu.Width() / gpu.Height(), 0.3f, 300), LookAt(eye, at));
        // the lights of the mockup's scene
        MeshUbo u{};
        float c[3];
        auto dir = [](float *o, float x, float y, float z) { const float l = sqrtf(x * x + y * y + z * z); o[0] = x / l; o[1] = y / l; o[2] = z / l; };
        dir(u.keyDir, 2, 5, 6); Lin(0xc8d2ff, c); for (int i = 0; i < 3; i++) u.keyCol[i] = c[i] * 0.75f;
        dir(u.rimDir, -1, 3, -7); float rim[3]; Amb(0x3c4fd8, rim); float rc[3] = { m_tint[0], m_tint[1], m_tint[2] }; Lerp3(rc, rim, 0.9f);   // Yass: consoles too red (mockup .6)
        for (int i = 0; i < 3; i++) u.rimCol[i] = rc[i] * 1.4f;
        const float la = t * 0.18f;
        u.driftPos[0] = sinf(la) * 4; u.driftPos[1] = 1.8f + sinf(la * 1.6f) * 0.7f; u.driftPos[2] = 2 + cosf(la) * 1.2f;
        float dc[3] = { m_tint[0], m_tint[1], m_tint[2] }, d8[3]; Amb(0x8a9cff, d8); Lerp3(dc, d8, 0.9f);
        const float di = (1.3f + 2.6f * m_wake) * (1 - 0.3f * m_night) * (0.5f + 0.5f * Settings::s_themeLightCur);   // v29: 35 % softer; v47: the theme
        for (int i = 0; i < 3; i++) u.driftCol[i] = dc[i] * di;
        Lin(0x5b6cc8, c); for (int i = 0; i < 3; i++) u.sky[i] = c[i] * 0.28f;
        Lin(0x020308, c); for (int i = 0; i < 3; i++) u.ground[i] = c[i] * 0.28f;
        u.cam[0] = eye[0]; u.cam[1] = eye[1]; u.cam[2] = eye[2]; u.cam[3] = 0.78f;   // toneMappingExposure
        for (int k = 0; k < 9; k++) for (int c2 = 0; c2 < 3; c2++) u.sh[k][c2] = a.envSH[k][c2];
        u.sky[3] = (float)(s_debug == 4 || s_debug == 5 ? 0 : s_debug);
        if (Settings::s_powerDim > 0.001f) {                // v33: the power popup - the lamps go down, warmer
            const float pd = Settings::s_powerDim;
            for (int i = 0; i < 3; i++) { u.keyCol[i] *= 1 - 0.5f * pd; u.driftCol[i] *= 1 - 0.55f * pd; u.sky[i] *= 1 - 0.4f * pd; u.rimCol[i] *= 1 - 0.45f * pd; }
        }
        static_assert(sizeof(MeshUbo) == sizeof(m3::MeshUbo), "the boxes read the same block");
        memcpy(&s_lights, &u, sizeof u); s_lights.sky[3] = 0;

        // One-time probe (the first frame with consoles): every step is sent alone and checked, so if the GPU
        // complains, sdmc:/brume/perf.log says exactly where. Then it draws normally.
        // each console is probed the first time it is drawn (opaque and see-through passes)
        static bool probed[2][consoles::kMax] = {};
        char tag[96];
        gpu.BeginMesh();
        // nearest console first: the depth test then skips the hidden pixels of the ones behind (early-Z)
        int order[NC];
        float score[NC];
        for (int i = 0; i < NC; i++) {
            const float d = Rel(i);
            order[i] = i; score[i] = cosf(d * kStep) + (Vis(d) < 0.01f ? -4.0f : 0.0f);

        }
        for (int i = 1; i < NC; i++) for (int j = i; j > 0 && score[order[j]] > score[order[j - 1]]; j--) { const int x = order[j]; order[j] = order[j - 1]; order[j - 1] = x; }
        int lastState = -1, lastTex = -1;
        for (int pass = 0; pass < 2; pass++) {          // opaque first, then the see-through parts
            for (int oi = 0; oi < NC; oi++) {
                const int i = pass == 0 ? order[oi] : order[NC - 1 - oi];   // see-through parts: back to front
                const Slot &s = m_slot[i];
                const Model &mdl = consoles::HasModel(i) ? a.model[i] : a.block;   // v93: no model yet: the block of glass
                const float ap = clamp01(s.appear), pr = clamp01(m_pres[i]) * Vis(Rel(i));   // (v90: pr = on the ring; v93: + the back of a long ring)
                if (ap <= 0.001f || pr <= 0.001f || !mdl.count) continue;
                float fallY = 0, grow = 1;
                if (m_on && s.fallStart > 0) {
                    const float fa = clamp01((now - s.fallStart) / FALL_DUR), eo = 1 - powf(1 - fa, 4);
                    fallY = FALL_H * (1 - eo); grow = 0.82f + 0.18f * eo;
                }
                const float e = ((m_on && s.fallStart > 0) ? (ap > 0 ? 1.0f : 0.0f) : (ap > 0 ? ease(ap) : 0.0f)) * grow;
                const float d = Rel(i);
                const float ang = d * kStep, front = cosf(ang), f01 = (front + 1) / 2, focus = fmaxf(0, 1 - fabsf(d));
                const float bob = sinf(t * 0.7f + i * 1.3f) * 0.08f;
                float y = 0.75f + bob + fallY + (1 - f01) * 0.2f;
                float arrive = 1;                              // v90: a new console comes down from above, turning softly into place
                if (m_arriveT0[i] > 0) { arrive = EaseGlide((now - m_arriveT0[i]) / Pace(1.7f)); y += (1 - arrive) * 2.6f; }
                if (s.jump > 0) y += sinf((1 - s.jump) * PI) * 0.4f;
                const Look &L = kLook[i];
                const float sc = fmaxf(0.0001f, e * pr * (0.55f + 0.7f * powf(f01, 6)) * L.size * m_warpK);   // (v79: a Warp shrinks them away and back)
                // the chosen one sways, and follows the right stick (it turns to look where the stick points)
                const float fe = fminf(1.0f, fmaxf(0.0f, m_focus));   // v31: the games' glide (a spring, see Update)
                const bool chosen = i == Sel();
                // (focused on its games: it sways much less, its front stays towards the middle)
                // v33: the media seats - a soft give, like a hand pushing a disc in (a damped little tilt)
                const float nd = now - m_nudgeT0, nudge = chosen && nd >= 0 && nd < 1.4f ? 0.05f * sinf(nd * 9.0f) * expf(-nd * 4.5f) : 0.0f;
                const float tx = L.tilt - focus * m_stkS[1] * 0.6f + nudge,
                            ty = focus * (sinf(t * 0.35f) * 0.3f * (chosen ? 1 - 0.75f * fe : 1.0f) + m_stkS[0] * 1.3f);
                // v30 - the games: the chosen console glides to the left and a little nearer, the others fade away;
                // the launch: a portable comes at the camera (we dive into its screen), a home console spins
                // v31: to the left but fully in the frame (Yass: it went past the left edge), a little further, a little
                // smaller, and turned so its front faces the middle of the screen; it leans into the move (spring speed)
                float px = sinf(ang) * RING * 1.2f - awayX, py = y, pz = RZ + cosf(ang) * RING, scF = sc, spin = (1 - arrive) * 1.4f, yawF = 0, lean = 0;
                if (chosen) {
                    px += (kFocusX - px) * fe; py += kFocusY * fe; pz += (kFocusZ - pz) * fe; scF *= 1 - 0.07f * fe;
                    yawF = kFocusYaw * fe; lean = fmaxf(-0.12f, fminf(0.12f, m_focusV * 0.035f));
                    if (m_diveT0 > 0) {
                        const float dp = clamp01((now - m_diveT0) / Pace(0.8f));
                        if (m_divePortable) { const float ex = EaseOutExpo(dp); px += (0.0f - px) * ex; pz += 6.0f * ex; py += (1.25f - py) * ex; scF *= 1 + 0.6f * ex; }
                        else {                     // v33 (Yass): a home console turns smoothly, no jump
                            const float sm = EaseOutExpo(dp);
                            spin = sm * PI * 1.1f; pz += 1.0f * sm;
                        }
                    }
                } else scF *= 1 - fe;
                if (scF < 0.002f) continue;
                const M4 model = Mul(Mul(Mul(Mul(Translate(px, py, pz), RotZ(lean)), RotY(ang * 0.85f + spin + yawF)), Scale(scF)),
                                     Mul(Mul(RotX(tx), RotY(ty)), RotZ(L.upright)));
                memcpy(u.model, model.m, 64);
                memcpy(u.mvp, Mul(vp, model).m, 64);
                if (chosen) memcpy(s_selModel.m, model.m, 64);
                if (pass == 0 && i == s_held) {           // v89: its slot, on screen (the soft light, DrawUi)
                    float p[3], q[3], sx, sy, qx, qy;
                    const float side[3] = { s_heldSlot[0] + 0.35f, s_heldSlot[1], s_heldSlot[2] };
                    m3::M4 mm, vv; memcpy(mm.m, model.m, 64); memcpy(vv.m, vp.m, 64);   // (the same layout as ours)
                    m3::Xform(mm, s_heldSlot, p); m3::Xform(mm, side, q);
                    if (m3::Project(vv, p, sx, sy) && m3::Project(vv, q, qx, qy)) {
                        s_heldSx = sx; s_heldSy = sy; s_heldR = fmaxf(10.0f, fminf(90.0f, hypotf(qx - sx, qy - sy)));
                        s_heldK = ap * fmaxf(0.0f, 1.0f - fabsf(d) / 1.6f) * (1 - fe) * m_warpK;   // the front ones only, never on the games screen
                    }
                }
                // v40: the Switch wears the real Joy-Con's colours (from the controllers, Pads)
                const bool joy = i == 0 && Settings::s_joyKnown && a.joyCell[0] > 0 && a.joyCell[1] > 0;
                for (int k = 0; k < 3; k++) { u.joyL[k] = Settings::s_joyL[k]; u.joyR[k] = Settings::s_joyR[k]; }
                u.joyL[3] = joy ? a.joyCell[0] : -1.0f; u.joyR[3] = joy ? a.joyCell[1] : -1.0f;
                const bool probe = s_probeAll; probed[pass][i] = true;
                bool whole = true;                     // the whole block once per console, then only the material per part
                // detail level: the centre console and its two neighbours (big on screen) in full, the others light
                const bool lod1 = fabsf(d) > 1.6f;
                for (int p = 0; p < mdl.count && s_bench != 1; p++) {
                    const MeshPart &q = mdl.parts[p];
                    const bool blend = q.base[3] < 0.999f && q.glass < 0.5f;
                    if ((pass == 1) != blend) continue;
                    memcpy(u.base, q.base, 16);
                    u.mat[0] = q.metal; u.mat[1] = q.rough; u.mat[2] = q.env;
                    u.mat[3] = (float)((q.tex >= 0 ? 1 : 0) | (q.glass > 0.5f && q.base[3] >= 0.999f ? 2 : 0) | (q.emiTex >= 0 ? 4 : 0));
                    u.tint[0] = q.tint[0]; u.tint[1] = q.tint[1]; u.tint[2] = q.tint[2]; u.tint[3] = 0.3f / 0.8f;   // thickness / attenuationDistance
                    u.emi[0] = q.emi[0]; u.emi[1] = q.emi[1]; u.emi[2] = q.emi[2]; u.emi[3] = q.dbl;
                    // states and textures only when they change (back faces culled: CCW = front, checked on the console; view 4: none)
                    const int st = (s_debug != 4 && q.dbl < 0.5f ? 1 : 0) | (blend ? 2 : 0) | (s_debug == 5 ? 4 : 0);
                    if (st != lastState) { gpu.SetMeshState(st & 1, st & 2, st & 4); lastState = st; }
                    if (whole) { gpu.SetMeshUniforms(&u, sizeof u); whole = false; }
                    else gpu.SetMeshUniforms(&u, 64, (uint32_t)offsetof(MeshUbo, base));   // base, mat, tint, emi
                    const Target *ts[4] = { q.tex >= 0 ? &a.tex[q.tex] : &a.env, &a.env, &haze, q.emiTex >= 0 ? &a.tex[q.emiTex] : &a.env };
                    const int tk = (q.tex & 0xFF) | ((q.emiTex & 0xFF) << 8);
                    if (tk != lastTex) { gpu.BindTextures(ts, 4, true); lastTex = tk; }
                    gpu.DrawMesh(q.vtx, q.vtxBytes, lod1 ? q.idx1 : q.idx, s_bench == 2 ? 3u : (lod1 ? q.idx1Count : q.idxCount));
                    if (probe) {
                        snprintf(tag, sizeof tag, "console %d partie %d (tex %d, %u sommets, %u indices, verre %d)", i, p, q.tex,
                                 q.vtxBytes / 32, q.idxCount, (int)q.glass);
                        if (!gpu.Probe(tag)) return;          // the normal path, just cut and checked (no re-binding)
                    }
                }
                if (pass == 0 && i < 8) gpu.MarkConsole(i);
                if (pass == 0 && !consoles::HasModel(i)) {     // v93: where its name floats, in front of its block
                    m3::M4 mm, vv; memcpy(mm.m, model.m, 64); memcpy(vv.m, vp.m, 64);
                    const float c0[3] = { 0, -0.05f, 0.34f }, c1[3] = { 0.9f, -0.05f, 0.34f };
                    float p0[3], p1[3], sx, sy, qx, qy;
                    m3::Xform(mm, c0, p0); m3::Xform(mm, c1, p1);
                    if (m3::Project(vv, p0, sx, sy) && m3::Project(vv, p1, qx, qy)) {
                        m_lbl[i][0] = sx; m_lbl[i][1] = sy; m_lbl[i][2] = hypotf(qx - sx, qy - sy);
                        m_lbl[i][3] = ap * pr * m_warpK * (0.25f + 0.75f * powf(f01, 3)) * (scF > 0.002f ? 1.0f : 0.0f);
                    }
                }
                if (probe && s_probeLog && !s_probeQuiet) { snprintf(tag, sizeof tag, "sonde : console %d (passe %d) OK", i, pass); s_probeLog(tag); }
            }
        }
    }

    // v93 - the blocks of glass' names (the consoles without a model yet): floating in front of each block, in our font, at its
    // size on screen, softly lit like the words on the fog; the front ones clearly, the far ones faintly (also on the games
    // screen, where the chosen block stands on the left)
    void Wheel::DrawLabels(Ui &ui, float now) {
        (void)now;
        for (int i = 8; i < NC; i++) {
            const float la = m_lbl[i][3] * (1 - PageAway());
            if (la < 0.01f || !consoles::kDefs[i].label[0]) continue;
            const char *t = consoles::kDefs[i].label;
            const float w = ui.Width(fonts::kPageTitle, t, 4.0f);
            if (w < 1) continue;
            const float z = fminf(fminf(1.3f * m_lbl[i][2] / w, m_lbl[i][2] / 75.0f), 1.2f);
            if (z < 0.08f) continue;
            const float x = m_lbl[i][0], y = m_lbl[i][1];
            ui.Light(x - w * z * 0.75f, y - 34 * z, x + w * z * 0.75f, y + 34 * z, ps2::A(ps2::kHalo, 0.10f * la), true);
            ui.SetXform(z, x, y, x, y);
            ui.TextCentered(fonts::kPageTitle, t, x, y - 22.0f, 4.0f, Hex(0xe4eaff, 0.82f * la));
            ui.SetZoom(1.0f);
        }
    }

    void (*Wheel::s_probeLog)(const char *) = nullptr;
    bool Wheel::s_probeAll = false, Wheel::s_probeQuiet = false;

    void Wheel::DrawUi(Ui &ui, const Assets &a, float now, const char *timeText) {
        float T[4], O[4];
        for (int i = 0; i < 4; i++) { T[i] = GroupT(m_group[i], now); O[i] = clamp01(GroupO(m_group[i], now)); }
        // v79: back after a while unseen (the games screen hid it): the bar and the rest fade in instead of popping
        if (m_lastUi < 0 || now - m_lastUi > 0.15f) m_uiBackAt = now;
        { const float back = EaseGlide((now - m_uiBackAt) / Pace(0.4f)); for (int i = 0; i < 4; i++) O[i] *= back; }
        const int at = 0;   // one interface sheet (720p) for both modes: memory
        // the name: .fade (opacity 0, filter blur(4px)), transition .45s ease
        const float nf = m_nameFadeFrom + (m_nameFadeTo - m_nameFadeFrom) * Bezier(0.25f, 0.1f, 0.25f, 1.0f, (now - m_nameFadeT0) / 0.45f);
        // the top bar. v70 (the PS2 look, Yass: « on voit trop les arêtes »): no glass bar with its edges any more - a soft dark
        // veil that fades out on every side, so that the words read on the fog
        if (O[0] > 0.002f) {
            const float *b = layout::kTopBar, dy = -90 * (1 - T[0]), w = b[2] + 120, h = b[3] + 44;
            ui.Shape(b[0] - 60, b[1] + dy - 22, b[0] - 60 + w, b[1] + dy - 22 + h, 0, 0, w, h, Rgba{1, 1, 1, 0.85f * O[0]}, 23, w, h);
        }
        // v28: on another page, the name banner and the wheel's hints fade out (mockup: opacity 1 - away)
        const float away = PageAway();
        for (int g = 1; g < 4; g++) O[g] *= (1 - away) * (1 - s_cover);   // (v96: the profile page over it: the banner and the hints step aside)
        const int plateOf = consoles::HasModel(m_shownName) ? m_shownName : consoles::kPorts;   // v93: no banner baked: the ports' place, the name live
        const float *pl = layout::kPlate[plateOf];
        const float pdy = 40 * (1 - T[1]), psc = 0.96f + 0.04f * T[1], pcx = pl[0] + pl[2] / 2, pcy = pl[1] + pl[3] / 2;
        auto plateX = [&](float x) { return pcx + (x - pcx) * psc; };
        auto plateY = [&](float y) { return pcy + (y - pcy) * psc + pdy; };
        if (O[1] > 0.002f) {
            // v70.3 (the PS2 look, Yass: « appliquer le style partout »): no glass banner with its edges any more - a soft dark
            // veil that fades out on every side, and a faint halo of light behind the logo / the name (they float on the fog)
            const float x0 = plateX(pl[0]), x1 = plateX(pl[0] + pl[2]), y0 = plateY(pl[1]), y1 = plateY(pl[1] + pl[3]);
            ui.Veil(x0 - 90, y0 - 30, x1 + 90, y1 + 36, 0.9f * O[1]);
            ui.Light(x0 - 40, y0 - 8, x1 + 40, y1 + 8, ps2::A(ps2::kHalo, 0.11f * O[1] * nf), true);
        }
        // ---- the top bar's tabs, laid out like the mockup's CSS (v28, +25 %: .tabs gap 12px; .tab padding 0 15px,
        // height 48px, radius 24px, icon 25px, gap 10px; the label shows only on the tab that is .on). Background and
        // colour fade in .5s (CSS transition), the layout follows the « on » tab at once (display: none -> inline).
        static const char *const kTabLbl[kTabs] = { "Jeux", "Apps", "Album", "Manettes", "Amis", "R\xc3\xa9glages", "Alimentation" };   // v36: Apps next to Jeux (v92: no Musique; v95: Amis)
        const float tdt = m_lastUi < 0 ? 0.0f : fminf(0.1f, fmaxf(0.0f, now - m_lastUi));
        m_lastUi = now;
        const int onIdx = m_barFocus ? m_tabOn : m_page;   // v28: the pill follows the page
        for (int k = 0; k <= kTabs; k++) {
            const float target = (k == 0 ? onIdx == -1 : onIdx == k - 1) ? 1.0f : 0.0f;
            m_tabAmt[k] += (target - m_tabAmt[k]) * (1 - expf(-tdt * 8.0f));
        }
        const float *tb = layout::kTopBar, tdy = -90 * (1 - T[0]), tcy = tb[1] + tb[3] / 2 + tdy;
        float tw[kTabs], ttotal = 0;
        for (int k = 0; k < kTabs; k++) {
            tw[k] = 30 + 25 + m_tabAmt[k + 1] * (10 + ui.Width(fonts::kTabLabel, kTabLbl[k], 0));   // v29: opens / closes smoothly
            ttotal += tw[k] + (k ? 12 : 0);
        }
        float tx[kTabs];
        { float x = tb[0] + tb[2] / 2 - ttotal / 2; for (int k = 0; k < kTabs; k++) { tx[k] = x; x += tw[k] + 12; } }
        // v29 (Yass: a fluid move between the tabs): one pill that slides to the chosen tab
        for (int k = 0; k < kTabs; k++) { m_tabX[k] = tx[k]; m_tabW[k] = tw[k]; }
        m_tabCy = tcy;
        if (onIdx >= 0) {
            if (m_pillX < 0) { m_pillX = tx[onIdx]; m_pillW = tw[onIdx]; }
            Glide(m_pillX, m_pillVX, tx[onIdx], tdt, 7.0f); Glide(m_pillW, m_pillVW, tw[onIdx], tdt, 7.0f);   // v36: glided
        }
        if (O[0] > 0.002f) {
            // v70 (the PS2 look): no pill - the chosen tab sits in a soft light of the accent colour (it still glides)
            auto TabLight = [&](float x0, float x1, float k) {
                const Rgba ac = Settings::Accent(1.0f);
                ui.Light(x0 - 16, tcy - 30, x1 + 16, tcy + 30, Rgba{ ac.r, ac.g, ac.b, 0.36f * k }, true);
                ui.Light(x0 - 34, tcy - 20, x1 + 34, tcy + 20, Rgba{ ac.r, ac.g, ac.b, 0.14f * k });
            };
            if (onIdx >= 0) TabLight(m_pillX, m_pillX + m_pillW, O[0]);
            const Sprite *cube = a.Find("cube"), *who = a.Find("who");
            if (cube && who && m_tabAmt[0] > 0.002f) {     // the profile: the same light around « ◇ Yass » (v46: the real one)
                const float x0 = cube->x + 14 - 15, x1 = who->x + 14 + ui.Width(fonts::kTabLabel, Profiles::ActiveName(), 0) + 15;
                m_profX0 = x0; m_profX1 = x1;
                TabLight(x0, x1, O[0] * m_tabAmt[0]);
            }
        }
        // the baked sprites, group by group
        char nameGroup[12]; snprintf(nameGroup, sizeof nameGroup, "name%d", plateOf);
        for (int i = 0; i < a.nsprites; i++) {
            const Sprite &s = a.sprites[i];
            if (!strncmp(s.name, "tab", 3)) continue;       // the tabs are drawn live, below
            if (!strcmp(s.name, "who")) continue;           // v46: the profile's name is drawn live (the active one)
            if (!strcmp(s.name, "cube") && Profiles::ActiveSlot() >= 0) continue;   // v46: its avatar instead of ◇
            int g = -1;
            for (int k = 0; k < 4; k++) if (!strcmp(s.group, kGroup[k])) g = k;
            if (!strcmp(s.group, nameGroup)) g = 1;
            if (g < 0 || O[g] <= 0.002f) continue;
            float x0 = s.x, y0 = s.y, x1 = s.x + s.w, y1 = s.y + s.h, alpha = O[g], blur = 0;
            if (g == 0) { y0 += -90 * (1 - T[0]); y1 += -90 * (1 - T[0]); }
            else if (g == 1) { x0 = plateX(x0); x1 = plateX(x1); y0 = plateY(y0); y1 = plateY(y1); alpha *= nf; blur = 4 * (1 - nf); }
            else { y0 += 90 * (1 - T[g]); y1 += 90 * (1 - T[g]); }
            if (alpha <= 0.002f) continue;
            if (!strncmp(s.name, "count", 5)) continue;     // v65: the mockup's sample line (« exemple · 24 jeux »): not real, not shown
            if (!strcmp(s.name, "name7")) {                 // v65: « Ports natifs » / « Native ports », live (the mockup's .name: Outfit 200 40px)
                ui.TextCentered(fonts::kPageTitle, consoles::Valid(m_shownName) ? consoles::kDefs[m_shownName].name : "", (x0 + x1) * 0.5f, (y0 + y1) * 0.5f - 22.0f, 1.5f, Hex(0xeef1ff, alpha));   // (v93: the new consoles too)
                continue;
            }
            if ((g == 2 || g == 3) && strstr(s.name, "lbl")) {   // v65: the hints' words live (« Ouvrir » / « Open »), like the pages
                static const char *const kL[3] = { "Ouvrir", "Verrouiller", "Attraper" }, *const kR[2] = { "Pages", "Tourner" };
                const int k = s.name[strlen(s.name) - 1] - '0';
                const char *fr = g == 2 ? (k >= 0 && k < 3 ? kL[k] : nullptr) : (k >= 0 && k < 2 ? kR[k] : nullptr);
                if (fr) {
                    float px = x0 + 14;                              // (the bake's margin around each image)
                    for (const char *q = lang::T(fr); *q;) px += ui.Glyph(fonts::kHintBig, NextCodepoint(q), px, (y0 + y1) * 0.5f - 9.5f, ps2::A(ps2::kWords, 0.9f * alpha), 0, 0, 0, 0);
                    continue;
                }
            }
            if ((g == 2 || g == 3) && strstr(s.name, "btn")) {   // v70 (the PS2 look): the buttons' letters in colour, no glass chip
                const int k = s.name[strlen(s.name) - 1] - '0';  // (mockup: A Ouvrir, B Verrouiller, Y Attraper | L R Pages, ◀ ▶ Tourner)
                const float cy = (y0 + y1) * 0.5f, bx = x0 + 14;
                if (g == 3 && k == 1) {
                    ui.Arrow(bx + 6, cy, 5.5f, 0, ps2::A(ps2::kChoice, alpha)); ui.Arrow(bx + 21, cy, 5.5f, 1, ps2::A(ps2::kChoice, alpha));
                    continue;
                }
                const char *t = g == 3 ? "LR" : (k == 1 ? "B" : (k == 2 ? "Y" : "A"));
                const Rgba c = g == 2 && k == 1 ? ps2::kPink : (g == 2 && k == 2 ? ps2::kGreen : ps2::kChoice);
                float px = bx;
                for (const char *q = t; *q;) { px += ui.Glyph(fonts::kButton, NextCodepoint(q), px, cy - 10, ps2::A(c, alpha), 0, 0, 0, 0); if (*q) px += 7; }
                continue;
            }
            ui.Shape(x0, y0, x1, y1, s.uv[at][0], s.uv[at][1], s.uv[at][2], s.uv[at][3], Rgba{1, 1, 1, alpha}, 20, blur);
        }
        // v89 - a game still runs in that console: a soft light breathes at its slot (slowly, like a sleeping console's
        // lamp), the colour of the ambiance - never an edge, only near the front of the carousel
        if (s_held >= 0 && s_heldK > 0.01f) {
            const float br = 0.62f + 0.38f * sinf(now * 1.55f), k = s_heldK * br, R = s_heldR;
            const Rgba ac = Settings::Accent(1.0f);
            ui.Light(s_heldSx - R * 1.6f, s_heldSy - R * 0.9f, s_heldSx + R * 1.6f, s_heldSy + R * 0.9f, Rgba{ ac.r, ac.g, ac.b, 0.28f * k }, true);
            ui.Light(s_heldSx - R * 0.45f, s_heldSy - R * 0.3f, s_heldSx + R * 0.45f, s_heldSy + R * 0.3f, ps2::A(ps2::kChosen, 0.22f * k), true);
        }
        // v66 - the real number of games under the console's name (where the mockup had a sample line): it rolls up from 0
        // when the console changes (or from the old number when it changes), brighter while it rolls
        {
            const int n = s_count[m_shownName];
            if (m_shownName != m_cntConsole || n != m_cntTarget) {
                m_cntFrom = m_shownName != m_cntConsole || m_cntTarget < 0 ? 0.0f : (float)m_cntTarget;
                m_cntConsole = m_shownName; m_cntTarget = n; m_cntT0 = now;
            }
            const float alpha = O[1] * nf;
            if (n >= 0 && alpha > 0.002f) {
                const float u = fminf(1.0f, fmaxf(0.0f, (now - m_cntT0) / 0.9f)), e = u >= 1 ? 1.0f : 1.0f - powf(2.0f, -10.0f * u);
                const int shown = (int)lroundf(m_cntFrom + (n - m_cntFrom) * e);
                char t[48], up[64];
                if (n == 0) snprintf(t, sizeof t, "%s", lang::T("Aucun jeu"));
                else if (shown == 1 && n == 1) snprintf(t, sizeof t, "%s", lang::T("1 jeu"));
                else snprintf(t, sizeof t, lang::T("%d jeux"), shown);
                Upper(t, up, sizeof up);
                const float glow = 1.0f - e;                                  // brighter while it rolls
                const Rgba col = { (0x8f + (0xe8 - 0x8f) * glow) / 255.0f, (0x9a + (0xee - 0x9a) * glow) / 255.0f, (0xc2 + (0xff - 0xc2) * glow) / 255.0f, 0.9f * alpha };
                const float cx = plateX(pl[0] + pl[2] / 2), cy = plateY(599.0f);
                if (s_held == m_shownName) {                                  // v89: « 12 JEUX  ·  • EN COURS » (its little light breathes)
                    const char *sep = "   \xc2\xb7   ", *run = lang::T("EN COURS");
                    const float ls = 3.0f, wUp = ui.Width(fonts::kGameInfo, up, ls), wSep = ui.Width(fonts::kGameInfo, sep, ls);
                    const float wRun = ui.Width(fonts::kGameInfo, run, ls) + 18, total = wUp + wSep + wRun;
                    float px = cx - total * 0.5f;
                    ui.TextCentered(fonts::kGameInfo, up, px + wUp * 0.5f, cy, ls, col); px += wUp;
                    ui.TextCentered(fonts::kGameInfo, sep, px + wSep * 0.5f, cy, ls, ps2::A(ps2::kFaint, 0.7f * alpha)); px += wSep;
                    const float dx = px + 6, dy = cy + 9, br = 0.75f + 0.25f * sinf(now * 1.55f);
                    ui.Light(dx - 10, dy - 10, dx + 10, dy + 10, ps2::A(ps2::kChoice, 0.7f * alpha * br), true);
                    ui.Shape(dx - 3, dy - 3, dx + 3, dy + 3, 0, 0, 6, 6, ps2::A(ps2::kChoice, alpha), 24, 6, 6, 3);
                    ui.TextCentered(fonts::kGameInfo, run, px + 18 + (wRun - 18) * 0.5f, cy, ls, ps2::A(ps2::kChoice, 0.95f * alpha));
                } else ui.TextCentered(fonts::kGameInfo, up, cx, cy, 3.0f, col);
            }
        }
        // v46 - the active profile: its avatar (or ◇) and its real nickname, where the mockup's « ◇ Yass » was baked
        if (O[0] > 0.002f) {
            const Sprite *cube = a.Find("cube"), *who = a.Find("who");
            if (cube && who) {
                const float ty = -90 * (1 - T[0]);
                if (mypage::BarAvatar(ui, cube->x + cube->w * 0.5f, cube->y + cube->h * 0.5f + ty, 15, O[0])) {}   // v96: the Nextendo picture (the profile chose it)
                else if (Profiles::ActiveSlot() >= 0)
                    Profiles::DrawAvatar(ui, Profiles::ActiveSlot(), cube->x + cube->w * 0.5f, cube->y + cube->h * 0.5f + ty, 15, O[0]);
                float px = who->x + 14;
                for (const char *p = Profiles::ActiveName(); *p;) px += ui.Glyph(fonts::kTabLabel, NextCodepoint(p), px, tcy - 9.0f, Hex(0xe4e9ff, O[0]), 0, 0, 0, 0);
            }
        }
        // tab icons (off #8c95b8 / on #e4e9ff, crossfaded) and the « on » tab's label
        if (O[0] > 0.002f) {
            char nm[16];
            for (int k = 0; k < kTabs; k++) {
                const float on = m_tabAmt[k + 1], ix = tx[k] + 15, iy = tcy - 12.5f;
                const int spr = k == 0 ? 0 : (k == 1 ? -1 : (k == kPageFriends ? -2 : (k < kPageFriends ? k : k - 1)));   // (the baked icons, in
                                                                 // the mockup's order - Jeux, Musique, Album, Manettes, Réglages, Alimentation:
                                                                 // Apps and Amis (v95) have none, Musique is skipped)
                if (spr == -2) {                                 // v95 - Amis: two small heads side by side, filled (off #8c95b8 / on #e4e9ff)
                    const float cr = (0x8c + (0xe4 - 0x8c) * on) / 255.0f, cg = (0x95 + (0xe9 - 0x95) * on) / 255.0f, cb = (0xb8 + (0xff - 0xb8) * on) / 255.0f;
                    for (int q = 0; q < 2; q++) {
                        const float hx = ix + 7 + q * 11, hy = iy + 7 - q * 1.5f, sh = q ? 1.0f : 0.78f;
                        ui.Shape(hx - 4.5f, hy - 4.5f, hx + 4.5f, hy + 4.5f, 0, 0, 9, 9, Rgba{ cr, cg, cb, O[0] * sh }, 24, 9, 9, 4.5f);
                        ui.Shape(hx - 7, hy + 6, hx + 7, hy + 16, 0, 0, 14, 10, Rgba{ cr, cg, cb, O[0] * sh }, 24, 14, 10, 5);
                    }
                }
                if (spr < 0) {                                   // v35 - Apps: four small rounded squares, off #8c95b8 / on #e4e9ff
                    const float cr = (0x8c + (0xe4 - 0x8c) * on) / 255.0f, cg = (0x95 + (0xe9 - 0x95) * on) / 255.0f, cb = (0xb8 + (0xff - 0xb8) * on) / 255.0f;
                    for (int q = 0; q < 4; q++) {
                        const float sx = ix + 1 + (q & 1) * 13, sy = iy + 1 + (q >> 1) * 13;
                        ui.Shape(sx, sy, sx + 10, sy + 10, 0, 0, 10, 10, Rgba{ cr, cg, cb, O[0] }, 24, 10, 10, 3);
                    }
                }
                for (int v = 0; v < 2 && spr >= 0; v++) {
                    snprintf(nm, sizeof nm, v ? "tabon%d" : "tabicon%d", spr);
                    const Sprite *s = a.Find(nm);
                    const float al = O[0] * (v ? on : 1 - on);
                    if (!s || al <= 0.002f) continue;
                    ui.Shape(ix - 14, iy - 14, ix - 14 + s->w, iy - 14 + s->h, s->uv[at][0], s->uv[at][1], s->uv[at][2], s->uv[at][3],
                             Rgba{1, 1, 1, al}, 20, 0);
                }
                if (m_tabAmt[k + 1] > 0.01f) {                    // v29: the label fades with its tab
                    const float lx = ix + 25 + 10;
                    float px = lx;
                    for (const char *p = lang::T(kTabLbl[k]); *p;) px += ui.Glyph(fonts::kTabLabel, NextCodepoint(p), px, tcy - 9.0f,
                                                                          ps2::A(ps2::kChosen, O[0] * on * on), 0, 0, 0, 0);
                }
            }
        }
        // the date and time (#topTime): right-aligned, Outfit 300 22px (v28), letter-spacing .5px, #aab3d4
        if (O[0] > 0.002f && timeText) {
            const float w = ui.Width(fonts::kTopBar, timeText, layout::kTimeLetterSpacing);
            const float top = layout::kTimeBaseline - ui.Baseline(fonts::kTopBar, 0) - 90 * (1 - T[0]);
            Rgba col = Hex(0xaab3d4, O[0]);
            ui.TextCentered(fonts::kTopBar, timeText, layout::kTimeRight - w / 2, top, layout::kTimeLetterSpacing, col);
            charge::DrawBar(ui, now, layout::kTimeRight - w - spotify::BarRoom() * 1.2f, tcy, O[0]);   // v96: the battery, left of the date and time (and of Spotify's four bars, v98)
        }
        // ---- v28: the pages' cards (mockup .pcard: 520 px glass card, centred, translate(-50%,-46%); the page slides
        // translateX(d * 520px) scale(1 - .06 |d|), opacity 1 - 1.15 |d|) - Apps, Album, Manettes, Réglages
        if (m_on) {
            // (no card left here - Apps, Album, Manettes, Réglages draw their own)
            // the pages' button hints (mockup setPageHints): left « (A Ouvrir) B Jeux », right « L R Pages »
            const float ph = m_page >= kPageApps && m_page <= kPagePads ? fminf(1.0f, away * 2) * (1 - s_cover) : 0.0f;   // (Réglages draws its own)
            if (ph > 0.002f) {
                const float hc = 720 - 28 - 13.5f;
                float hx = 46;
                // v70 (the PS2 look): the button's letter in colour (no glass ring), then its word
                auto Hint = [&](char btn, const char *label) {
                    const Rgba bc = btn == 'B' ? ps2::kPink : (btn == 'Y' ? ps2::kGreen : ps2::kChoice);
                    hx += ui.Glyph(fonts::kButton, (uint32_t)btn, hx, hc - 10, ps2::A(bc, ph), 0, 0, 0, 0) + 10;
                    for (const char *q = lang::T(label); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * ph), 0, 0, 0, 0) + 0.5f;
                    hx += 30;
                };
                if (m_page == kPageAlbum) Hint('A', "Ouvrir");
                if (m_page == kPagePads) { if (!Pads::s_none) Hint('A', "Vibrer"); Hint('Y', "Appairer"); }
                if (m_page == kPageApps) Hint('A', "Lancer");
                {   // right: « L R  Pages », live, at the right edge
                    const float lw = ui.Advance(fonts::kButton, 'L') + 7 + ui.Advance(fonts::kButton, 'R');
                    float rx = 1280 - 46 - (lw + 10 + ui.Width(fonts::kHintBig, "Pages", 0.5f));
                    rx += ui.Glyph(fonts::kButton, 'L', rx, hc - 10, ps2::A(ps2::kChoice, ph), 0, 0, 0, 0) + 7;
                    rx += ui.Glyph(fonts::kButton, 'R', rx, hc - 10, ps2::A(ps2::kChoice, ph), 0, 0, 0, 0) + 10;
                    for (const char *q = lang::T("Pages"); *q;) rx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), rx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * ph), 0, 0, 0, 0) + 0.5f;
                }
            }
        }
        if (s_debug > 0) {
            static const char *const kView[10] = { "", "vue 1 : texture seule", "vue 2 : lumiere de la piece seule",   // lang:log (never drawn)
                                                   "vue 3 : lampes seules", "vue 4 : sans tri des faces", "vue 5 : faces cachees (B)",   // lang:log (never drawn)
                                                   "vue 6 : reflet brillant seul", "vue 7 : lumiere diffuse de la piece seule",   // lang:log (never drawn)
                                                   "vue 8 : lueur seule", "vue 9 : tout, sans le filtre de couleur" };   // lang:log (never drawn)
            ui.TextCentered(fonts::kTopBar, kView[s_debug], 640, 92, 0, Hex(0xffffff, 1));
        }
    }

} // namespace brume
