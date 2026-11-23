// Brume - HD rumble as soft haptic clicks (see Haptics.hpp).
// The Joy-Con's linear actuator has two bands: low (~160 Hz) and high (~320 Hz). A very short burst on the high band
// at low amplitude feels like a « click » under the finger; a short low burst feels like a soft « knock ». Everything
// stays at or under 0.32 amplitude: Yass wants nothing strong.
#include "Haptics.hpp"
#include <switch.h>
#include <cmath>

namespace brume::haptic {

    namespace {
        constexpr float kMax = 0.32f;                 // never stronger than this
        HidVibrationDeviceHandle g_hand[2], g_dual[2];
        bool  g_ok = false, g_on = true, g_sent0 = true;
        // up to 4 overlapping pulses, mixed each frame
        struct Pulse { float t = 0, dur = 0, lo = 0, hi = 0, flo = 160, fhi = 320; bool swell = false; };
        Pulse g_p[4];

        void Add(const Pulse &p) {
            if (!g_ok || !g_on) return;
            int k = 0;
            for (int i = 1; i < 4; i++) if (g_p[i].t >= g_p[i].dur || (g_p[k].t < g_p[k].dur && g_p[i].t > g_p[k].t)) k = i;
            g_p[k] = p;
        }
        void Send(float lo, float hi, float flo, float fhi) {
            HidVibrationValue v[2];
            for (int i = 0; i < 2; i++) { v[i].amp_low = lo; v[i].freq_low = flo; v[i].amp_high = hi; v[i].freq_high = fhi; }
            hidSendVibrationValues(g_hand, v, 2);
            hidSendVibrationValues(g_dual, v, 2);
        }
    }

    void Init() {
        g_ok = R_SUCCEEDED(hidInitializeVibrationDevices(g_hand, 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
        const bool dual = R_SUCCEEDED(hidInitializeVibrationDevices(g_dual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
        g_ok = g_ok || dual;
    }
    void SetEnabled(bool on) { g_on = on; if (!on) Stop(); }
    bool Enabled() { return g_on; }

    void Tap(float s)   { Pulse p; p.dur = 0.018f; p.hi = kMax * 0.75f * fminf(1.0f, s); p.lo = kMax * 0.15f * fminf(1.0f, s); p.fhi = 320; Add(p); }
    void Tick(float s)  { Pulse p; p.dur = 0.012f; p.hi = kMax * 0.45f * fminf(1.0f, s); p.fhi = 300; Add(p); }
    void Thump(float s) { Pulse p; p.dur = 0.035f; p.lo = kMax * 0.8f * fminf(1.0f, s); p.flo = 150; p.hi = kMax * 0.15f * fminf(1.0f, s); Add(p); }
    void Glass(float s)   { Pulse p; p.dur = 0.010f; p.hi = kMax * 0.65f * fminf(1.0f, s); p.fhi = 520; Add(p); }
    void Plastic(float s) { Pulse p; p.dur = 0.022f; p.lo = kMax * 0.45f * fminf(1.0f, s); p.flo = 210; p.hi = kMax * 0.30f * fminf(1.0f, s); p.fhi = 250; Add(p); }
    void Purr(float s, float hz) {                     // renewed every frame: one slot, low band, its pitch follows the spin
        if (!g_ok || !g_on) return;
        Pulse p; p.dur = 0.05f; p.lo = kMax * 0.35f * fminf(1.0f, s); p.flo = fmaxf(60.0f, fminf(240.0f, hz));
        for (Pulse &q : g_p) if (q.t < q.dur && q.lo > 0 && q.hi == 0 && !q.swell && q.dur == 0.05f) { q = p; return; }
        Add(p);
    }
    void Swell(float s, float sec) { Pulse p; p.dur = sec; p.lo = kMax * fminf(1.0f, s); p.flo = 120; p.swell = true; Add(p); }

    void Update(float dt) {
        if (!g_ok) return;
        float lo = 0, hi = 0, flo = 160, fhi = 320;
        for (Pulse &p : g_p) {
            if (p.t >= p.dur) continue;
            const float k = p.swell ? sinf(3.14159265f * p.t / p.dur) : 1.0f;   // a swell rises and falls
            if (p.lo * k > lo) { lo = p.lo * k; flo = p.flo; }
            if (p.hi * k > hi) { hi = p.hi * k; fhi = p.fhi; }
            p.t += dt;
        }
        if (!g_on) { lo = hi = 0; }
        lo = fminf(lo, kMax); hi = fminf(hi, kMax);
        const bool zero = lo <= 0.001f && hi <= 0.001f;
        if (zero && g_sent0) return;                // nothing to say: no traffic
        Send(lo, hi, flo, fhi);
        g_sent0 = zero;
    }

    void Stop() {
        for (Pulse &p : g_p) p.t = p.dur;
        if (g_ok) Send(0, 0, 160, 320);
        g_sent0 = true;
    }

}
