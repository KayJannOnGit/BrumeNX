// Brume - v81: the themes, the sky by the hour, the battery as the fog (see Theme.hpp).
#include "Theme.hpp"
#include "Settings.hpp"
#include "Ui.hpp"
#include "Notify.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include <switch.h>
#include <cmath>

#ifdef BRUME_UISIM
// uisim (tools/uisim/main.cpp, « demo heure / batterie / charge »): the hour and the battery forced, -1 = the real ones
extern "C" { float g_simHour = -1; int g_simBat = -1, g_simCharge = -1; }
#endif

namespace brume::theme {

    namespace {
        struct Look {
            float chrome;      // 0..1 the liquid chrome (haze_fsh), chrome cubes / pebbles / ring / sparkles
            float dens;        // the fog's density (+ thicker)
            float fogSpd;      // the fog's pace (1)
            float curl;        // the fog's curl (- smoother, bigger clouds)
            float lights;      // drifting lights (3)
            float lightSpd;    // their pace (1)
            float lightAmt;    // their strength (+)
            float sparkle;     // the dust's sparkle (+)
            float dark;        // the room's darkness (0..1: the background and the edges)
            float cubeRefl;    // the cubes' reflections (+)
            float stars, comets;   // their amount (1)
            Rgba title, glow, choice, chosen, halo, faint, words;   // the interface (ps2:: palette)
        };
        // Brume = the values of v80 (Ui.hpp's palette, every shader number at 0)
        const Look kLook[kCount] = {
            { 0, 0, 1, 0, 3, 1, 0, 0, 0, 0, 1, 1,
              { 0.95f, 0.86f, 0.40f, 1 }, { 0.95f, 0.75f, 0.25f, 1 }, { 0.60f, 0.74f, 1.0f, 1 }, { 0.94f, 0.96f, 1.0f, 1 },
              { 0.32f, 0.52f, 1.0f, 1 }, { 0.52f, 0.60f, 0.84f, 1 }, { 0.80f, 0.84f, 0.94f, 1 } },
            // Nuit profonde: a thin, slow, smooth fog, a light and a half, almost no dust, the room nearly black, dim stars
            { 0, -0.14f, 0.45f, -0.45f, 1.5f, 0.5f, -0.35f, -0.75f, 1, -0.35f, 0.40f, 0.30f,
              { 0.80f, 0.73f, 0.46f, 1 }, { 0.70f, 0.55f, 0.28f, 1 }, { 0.50f, 0.58f, 0.86f, 1 }, { 0.86f, 0.88f, 0.97f, 1 },
              { 0.30f, 0.30f, 0.82f, 1 }, { 0.42f, 0.46f, 0.68f, 1 }, { 0.68f, 0.72f, 0.84f, 1 } },
            // Chrome: the liquid chrome takes the fog's place; five quicker lights reflected in it, sparkles, polished cubes,
            // silver titles with a cyan glow
            { 1, 0, 1, 0, 5, 1.3f, 0.5f, 1.2f, 0.25f, 1, 1.25f, 1.15f,
              { 0.90f, 0.93f, 0.98f, 1 }, { 0.55f, 0.85f, 1.0f, 1 }, { 0.72f, 0.82f, 0.94f, 1 }, { 0.97f, 0.98f, 1.0f, 1 },
              { 0.50f, 0.80f, 1.0f, 1 }, { 0.58f, 0.64f, 0.75f, 1 }, { 0.84f, 0.87f, 0.93f, 1 } } };

        int   s_theme = 0;
        float s_w[kCount] = { 1, 0, 0 }, s_wv[kCount] = { 0, 0, 0 };   // each theme's weight (springs: they always sum to 1)
        Look  s_cur = kLook[0];
        float s_fogT = 0, s_lightT = 0, s_lastNow = -1;   // the fog's and the lights' clocks (their pace may change: never a jump)
        float s_pressAt = -100;
        // the sky
        float s_skyK = 0, s_skyLight = 1, s_skyDens = 0;
        int   s_phase = -1;
        bool  s_skyWas = false, s_skyInit = false;
        // the battery
        float s_lowK = 0, s_chargeK = 0, s_askedAt = -100;
        u32   s_pct = 100;
        bool  s_charging = false, s_batWas = false, s_batInit = false, s_lowSaid = false, s_chargeSaid = false;

        float Mixf(const float *v, int stride) {          // the blend of one number of the three looks
            float s = 0;
            for (int i = 0; i < kCount; i++) s += s_w[i] * v[i * stride];
            return s;
        }
        Rgba MixRgba(Rgba Look::*m) {
            Rgba o{ 0, 0, 0, 1 };
            for (int i = 0; i < kCount; i++) { const Rgba &c = kLook[i].*m; o.r += s_w[i] * c.r; o.g += s_w[i] * c.g; o.b += s_w[i] * c.b; }
            return o;
        }
        void Blend() {
            const int st = sizeof(Look) / sizeof(float);
            static_assert(sizeof(Look) % sizeof(float) == 0, "Look = floats only");
            float *out = (float *)&s_cur;
            const float *in = (const float *)&kLook[0];
            for (int k = 0; k < 12; k++) out[k] = Mixf(in + k, st);   // the 12 numbers before the palette
            s_cur.title = MixRgba(&Look::title); s_cur.glow = MixRgba(&Look::glow); s_cur.choice = MixRgba(&Look::choice);
            s_cur.chosen = MixRgba(&Look::chosen); s_cur.halo = MixRgba(&Look::halo); s_cur.faint = MixRgba(&Look::faint);
            s_cur.words = MixRgba(&Look::words);
        }
        int Phase(float h) { return h >= 5.0f && h < 8.0f ? 0 : h >= 8.0f && h < 17.5f ? 1 : h >= 17.5f && h < 21.5f ? 2 : 3; }
        const char *PhaseText(int p) {
            switch (p) {
                case 0: return "Ciel selon l'heure : l'aube";
                case 1: return "Ciel selon l'heure : le jour";
                case 2: return "Ciel selon l'heure : le soir";
                default: return "Ciel selon l'heure : la nuit";
            }
        }
        // Chrome's arrival: a soft metallic shimmer, high notes of the chord playing (never a new sound file)
        void Shimmer() {
            for (int k = 0; k < 4; k++) audio::Bell(audio::HarmonyNote(k, 86, 100), 0.016f - 0.002f * k, true, 0.06f + 0.075f * k);
        }
    }

    void Set(int v) {
        if (v < 0 || v >= kCount) return;
        if (v == kChrome && s_theme != kChrome) Shimmer();
        s_theme = v;
    }
    void Snap(int v) {
        if (v < 0 || v >= kCount) v = 0;
        s_theme = v;
        for (int i = 0; i < kCount; i++) { s_w[i] = i == v ? 1.0f : 0.0f; s_wv[i] = 0; }
        Blend();
    }
    int   Current() { return s_theme; }
    float Chrome() { return s_cur.chrome; }
    float StarsK() { return s_cur.stars; }
    float CometsK() { return s_cur.comets; }
    void  PressA(float now) { if (s_cur.chrome > 0.01f) s_pressAt = now; }

    float Hour(float h) {
#ifdef BRUME_UISIM
        if (g_simHour >= 0) return g_simHour;
#endif
        return h;
    }

    // v53, v81 (Yass: « trop discrets ») - the sky through the day, clearly: a pink dawn turning apricot, a real day blue, a
    // violet evening, a near-black night. The fog follows too: thick morning mist, a clear bright day, a thin dim night.
    void SkyAt(float h, float *rgb, float *light, float *dens) {
        struct K { float h, c[3], l, d; };
        static const K k[] = { { 0.0f, { 0.05f, 0.06f, 0.20f }, 0.55f, -0.12f }, { 4.5f, { 0.05f, 0.06f, 0.20f }, 0.55f, -0.12f },
                               { 6.0f, { 0.98f, 0.36f, 0.55f }, 0.95f, 0.28f }, { 7.3f, { 1.00f, 0.55f, 0.30f }, 1.05f, 0.22f },
                               { 9.0f, { 0.24f, 0.52f, 1.00f }, 1.15f, 0.0f }, { 17.0f, { 0.24f, 0.52f, 1.00f }, 1.15f, 0.0f },
                               { 19.0f, { 0.62f, 0.26f, 0.95f }, 0.92f, 0.10f }, { 21.5f, { 0.05f, 0.06f, 0.20f }, 0.55f, -0.12f },
                               { 24.0f, { 0.05f, 0.06f, 0.20f }, 0.55f, -0.12f } };
        if (h < 0) h = 0;
        if (h > 24) h = 24;
        for (int i = 0; i + 1 < (int)(sizeof k / sizeof k[0]); i++)
            if (h <= k[i + 1].h) {
                float f = (h - k[i].h) / (k[i + 1].h - k[i].h);
                f = f * f * (3 - 2 * f);
                for (int c = 0; c < 3; c++) rgb[c] = k[i].c[c] + (k[i + 1].c[c] - k[i].c[c]) * f;
                if (light) *light = k[i].l + (k[i + 1].l - k[i].l) * f;
                if (dens) *dens = k[i].d + (k[i + 1].d - k[i].d) * f;
                return;
            }
        for (int c = 0; c < 3; c++) rgb[c] = k[0].c[c];
        if (light) *light = k[0].l;
        if (dens) *dens = k[0].d;
    }

    void Frame(float now, float dt, float *ambient, float *haze, float (*bo)[4], float (*cubeFx)[4], bool psmOk, float wind) {
        // 1. the look glides (each theme's weight is a spring: a crossfade of ~2.5 s, no hard start)
        for (int i = 0; i < kCount; i++) Glide(s_w[i], s_wv[i], i == s_theme ? 1.0f : 0.0f, dt, 1.9f);
        Blend();
        if (s_lastNow < 0) { s_fogT = s_lightT = now; }
        s_lastNow = now;
        s_fogT += dt * s_cur.fogSpd; s_lightT += dt * s_cur.lightSpd;
        // 2. the interface's colours and the accent (it glides too)
        ps2::kTitle = s_cur.title; ps2::kTitleGlow = s_cur.glow; ps2::kChoice = s_cur.choice; ps2::kChosen = s_cur.chosen;
        ps2::kHalo = s_cur.halo; ps2::kFaint = s_cur.faint; ps2::kWords = s_cur.words;
        for (int c = 0; c < 3; c++) Settings::s_accent[c] += (Settings::s_accentTarget[c] - Settings::s_accent[c]) * (1 - expf(-dt * 1.6f));

        // 3. the sky: its light and density by the hour (its colour is the ambient's target: Settings::Update); a short
        //    notification when it is switched on and when the sky turns (dawn, day, evening, night)
        {
            float l = 1, d = 0, rgb[3];
            SkyAt(Settings::s_hour, rgb, &l, &d);
            const int ph = Phase(Settings::s_hour);
            static int frames = 0;                               // (the first frames: the hour is not read yet - silent)
            if (Settings::s_sky && s_skyInit && (!s_skyWas || ph != s_phase)) notify::Post(now, PhaseText(ph));
            if (frames < 4) frames++;
            s_phase = ph; s_skyWas = Settings::s_sky; s_skyInit = frames > 3;
            s_skyK += ((Settings::s_sky ? 1.0f : 0.0f) - s_skyK) * (1 - expf(-dt * 1.2f));
            s_skyLight = 1 + (l - 1) * s_skyK; s_skyDens = d * s_skyK;
        }
        // 4. the battery is the fog: low (< 25 %) = thicker, darker, amber; charging = a slow breath (4.5 s), well visible
        float batDens = 0, breath = 0;
        {
            bool known = psmOk;
#ifdef BRUME_UISIM
            if (g_simBat >= 0) { known = true; s_pct = (u32)g_simBat; s_charging = g_simCharge > 0; }
#endif
            if (known && Settings::s_battery && (now - s_askedAt > 10.0f || now < s_askedAt)) {
                s_askedAt = now;
#ifdef BRUME_UISIM
                if (g_simBat < 0)
#endif
                {
                    PsmChargerType ch = PsmChargerType_Unconnected;
                    if (R_FAILED(psmGetBatteryChargePercentage(&s_pct))) s_pct = 100;
                    if (R_SUCCEEDED(psmGetChargerType(&ch))) s_charging = ch != PsmChargerType_Unconnected;
                }
            }
            const bool on = known && Settings::s_battery;
            const float low = !on ? 0.0f : (s_pct >= 25 ? 0.0f : (s_pct <= 5 ? 1.0f : (25 - (float)s_pct) / 20.0f));
            const bool lowNow = on && s_pct < 25, chNow = on && s_charging;
            if (s_batInit) {                                                     // what kicks in (switched on, or it happens)
                if (lowNow && !chNow && (!s_lowSaid || !s_batWas)) notify::Post(now, "Batterie faible : la brume s'\xc3\xa9paissit");
                if (chNow && !s_batWas) notify::Post(now, "En charge : la brume respire");   // (v87: plugged in later: Charge.cpp says it)
            }
            s_lowSaid = lowNow && !chNow; s_chargeSaid = chNow; s_batWas = on; s_batInit = true;
            s_lowK += (low - s_lowK) * (1 - expf(-dt * 0.8f));
            s_chargeK += ((chNow ? 1.0f : 0.0f) - s_chargeK) * (1 - expf(-dt * 0.8f));
            static const float kAmber[3] = { 1.0f, 0.50f, 0.10f };
            for (int c = 0; c < 3; c++) ambient[c] = (ambient[c] + (kAmber[c] - ambient[c]) * 0.85f * s_lowK) * (1 - 0.40f * s_lowK);
            haze[1] *= 1 + 0.45f * s_lowK * (1 - 0.8f * s_cur.chrome);         // (the liquid chrome does not get « thicker »: darker only)
            batDens = 0.38f * s_lowK;
            breath = s_chargeK * (0.5f - 0.5f * cosf(now * (6.2831853f / 4.5f)));   // in, out: 4.5 s
            haze[1] *= 1 + 0.38f * breath;
        }
        // the fog's light: the theme's (Settings, uAmbient.w), the sky's, the breath's
        ambient[3] = (ambient[3] > 0.01f ? ambient[3] : 1.0f) * s_skyLight * (1 + 0.20f * breath);

        // 5. the shaders' numbers (common.glsl uThA..uThD): all 0 = Brume
        bo[0][2] = s_cur.chrome < 0.002f ? 0.0f : (s_cur.chrome > 0.998f ? 1.0f : s_cur.chrome);   // (the spring's tail: done)
        bo[0][3] = s_cur.dens + s_skyDens + batDens;
        bo[1][2] = s_fogT - now;                                 bo[1][3] = s_cur.curl;
        bo[2][2] = s_cur.lights - 3.0f;                          bo[2][3] = s_lightT - now;
        bo[3][2] = s_cur.lightAmt;                               bo[3][3] = s_cur.sparkle;
        cubeFx[0][3] = s_cur.dark;                               cubeFx[1][3] = s_cur.cubeRefl;
        cubeFx[2][3] = s_cur.chrome > 0.001f ? wind : 0.0f;
        const float age = now - s_pressAt;
        cubeFx[3][3] = s_cur.chrome > 0.001f && age > 0 && age < 6.0f ? age : 0.0f;
        cubeFx[4][3] = breath;                                   cubeFx[5][3] = 0;
    }

} // namespace brume::theme
