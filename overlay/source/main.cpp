// Brume Overlay - the HOLD-HOME panel, the HOLD-POWER menu, the volume and the notifications, in Brume's own look. GPL-3, see brume/LICENSE.
//
// Replaces the Switch's overlay applet (overlayDisp, 010000000000100C): the panel shown when HOME is held, the volume
// banner, the power menu (Power held) and the notifications (screenshot, video, low battery).
//
// SAFETY FIRST (Yass: « on garantit la stabilité et la sécurité, Power est vraiment important »):
//  - the power menu uses the system's OWN sequences (appletStartSleepSequence / Reboot / Shutdown, available to the
//    overlay applet on 15.0.0+); if one is refused, the same request goes to the HOME menu (« SAMS » message, which
//    the qlaunch-ext loader carries out). Holding Power ~12 s always forces the console off (hardware);
//  - the power action runs FIRST when chosen (before the input is given back, before any sound);
//  - the power menu needs nothing that can fail at run time: its text is baked into the program (no font file, no
//    FreeType), its picture is drawn into a buffer that is part of the program (no allocation); without the cached plate
//    or the fog it draws a plain plate;
//  - it gives the input back every time it closes, never keeps it;
//  - a start counter (sdmc:/brume/overlay/demarrages.txt): 3 starts in a row that did not reach 30 s of stable running
//    = it removes itself (renames its folder) and the original overlay comes back at the next start; the same at once
//    if the screen cannot be opened;
//  - nothing runs while nothing is shown: it waits on events (zero cost in game), draws only what changed.
//
// v90 (Yass: « ressemble plus à l'OS, allège-le pour les 60 FPS, il est en low quality ») - drawn like the menu itself:
// the plates are Ui::Surface (night mist, rounded), the words are the menu's (Outfit, baked at build time), the chosen
// line in its halo, the ring of six lights (Busy::Orbs). Two display layers:
//  - the UI layer, 560 x 720 at the screen's own resolution (sharp), placed where it is needed (right: HOME panel,
//    centre: power menu, top left: volume / notifications) - only the rectangles that changed are copied into it;
//  - the fog layer, 320 x 180 enlarged by the display hardware (free): the dim veil over the game and the menu's fog.
// Triple-buffered (with two buffers the layer waited one more screen refresh: the « 33 ms » of the old journal).
// Built twice: the real overlay (exefs main + main.npdm) and a PREVIEW .nro (a normal app: L = power menu, R = HOME
// panel, ZL = volume banner, ZR = a notification, + = quit; the power actions are only shown).
//
// v98 (Yass: « on change les options par des plus pertinentes et vraiment utiles in game ») - the HOME panel becomes the
// « centre de contrôle » of the game being played:
//  - header: the time and date, the console's battery (charging or not), the controllers' batteries (4 lights each, hid);
//  - title: the game's name (ns), under it how long it has been running (pdm: its last launch); « Centre de contrôle »
//    without a game;
//  - rows: Volume (audctl, ◀▶, A = mute), Luminosité (◀▶, A = auto: the old « Luminosité auto. » row folded in),
//    Performance (sys-clk, unchanged), Mode avion (unchanged), Notes du jeu (sdmc:/brume/notes/<title id>.txt, read only,
//    shown in the panel), and a hidden « Musique » row left for later (no network here);
//  - a live line at the bottom: CPU / GPU / RAM clocks, SoC / PCB temperatures, the game's memory (sys-clk, else clkrst + ts;
//    svcGetSystemInfo);
//  - removed: « Mise en veille » (Power pressed briefly does it, and it stays in the power menu), « Luminosité auto. » (now A on
//    Luminosité). No screenshot row: the Capture button already does it in every game, and the overlay cannot save into the
//    Album itself (the HOME menu's loader has no capture message; caps:ss would need a 3.6 MB picture in our small heap).
// Everything is read when the panel opens and once a second while it is open - never while it is closed; a source that
// fails only hides its line. The power menu, the safety and the display layers are untouched.
#include <switch.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>                  // (memalign: the sound buffers)
#include <sys/stat.h>

#include "glyphs.inc"                // Outfit (SIL OFL 1.1, brume/assets/fonts/OFL-Outfit.txt), baked by tools/bakefont.py

#ifndef PREVIEW
extern "C" {
    u32 __nx_applet_type = AppletType_OverlayApplet;
    // a small heap of its own (the overlay shares the applets' memory with the HOME menu): 14 MB (was 28)
    // (UI layer 3 x 1.7 MB, fog layer 3 x 0.33 MB, two cached plates 2 x 1.6 MB, the small plate, the fog, the sounds)
    #define HEAP_SIZE (14 * 1024 * 1024)
    static char g_heap[HEAP_SIZE] __attribute__((aligned(0x1000)));
    extern char *fake_heap_start, *fake_heap_end;
    void __libnx_initheap(void) { fake_heap_start = g_heap; fake_heap_end = g_heap + HEAP_SIZE; }
    // v93 (journal on the console: « calque ui plein ecran : ECHEC (memoire ?) ») - libnx takes 8 MB of the heap for the
    // GPU driver's transfer memory (an application's default); the overlay never sends GPU commands (it draws with the CPU
    // into nvmap buffers): 256 KB is plenty, and the full-screen layer's two 1280x720 buffers (2 x 3.9 MB) now fit
    u32 __nx_nv_transfermem_size = 0x40000;
    extern u64 __nx_vi_layer_id;                           // (libnx: the layer viCreateLayer opens; see MakeLayer)
    u32 __nx_fs_num_sessions = 1;
    void __appInit(void) {
        if (R_FAILED(smInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
        appletInitialize();          // (required: the screen and the messages)
        hidInitialize();
        hidsysInitialize();
        fsInitialize();
        fsdevMountSdmc();
    }
    void __appExit(void) {
        fsdevUnmountAll(); fsExit(); hidsysExit(); hidExit(); appletExit(); smExit();
    }
}
#endif

namespace {

    // ======================= the log and the safety =======================
    constexpr const char *kDir = "sdmc:/brume/overlay";
    constexpr const char *kLog = "sdmc:/brume/overlay/journal.txt";
    constexpr const char *kStarts = "sdmc:/brume/overlay/demarrages.txt";
    constexpr const char *kOverride = "sdmc:/atmosphere/contents/010000000000100C";
    int g_logLines = 0;
    bool g_logStarted = false, g_logDefer = false;              // v90: while a screen is shown, the lines wait in memory (no card
    char g_logBuf[4096];                                        // access during a frame); written once it has closed
    size_t g_logLen = 0;
    void LogFlush() {
        if (!g_logLen) return;
        if (FILE *f = fopen(kLog, g_logStarted ? "a" : "w")) { fwrite(g_logBuf, 1, g_logLen, f); fclose(f); g_logStarted = true; }
        g_logLen = 0;
    }
    void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    void Log(const char *fmt, ...) {
        if (g_logLines > 400) return;                          // (bounded)
        g_logLines++;
        char line[1024];
        va_list a; va_start(a, fmt); int n = vsnprintf(line, sizeof line - 1, fmt, a); va_end(a);
        if (n < 0) return;
        if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
        line[n++] = '\n';
        if (g_logLen + (size_t)n > sizeof g_logBuf) LogFlush();   // (full: written now)
        if ((size_t)n <= sizeof g_logBuf) { memcpy(g_logBuf + g_logLen, line, (size_t)n); g_logLen += (size_t)n; }
        if (!g_logDefer) LogFlush();
    }
    void GiveBack(const char *why) {                           // the original overlay comes back at the next start
#ifndef PREVIEW
        char off[96];
        bool moved = false;
        for (int k = 0; k < 10 && !moved; k++) { snprintf(off, sizeof off, k ? "%s_off%d" : "%s_off", kOverride, k); moved = rename(kOverride, off) == 0; }
        Log("SECURITE : %s -> overlay retire (%s), l'original revient au prochain demarrage", why, moved ? "ok" : "ECHEC");
#else
        (void)why;
#endif
    }
    void SafetyAtStart() {
        mkdir("sdmc:/brume", 0777); mkdir(kDir, 0777);
        int n = 0;
        if (FILE *f = fopen(kStarts, "r")) { if (fscanf(f, "%d", &n) != 1) n = 0; fclose(f); }
#ifndef PREVIEW
        if (n >= 3) { GiveBack("3 demarrages sans 30 s de stabilite"); n = 0; }
#endif
        if (FILE *f = fopen(kStarts, "w")) { fprintf(f, "%d\n", n + 1); fclose(f); }
    }
    void SafetyStable() { if (FILE *f = fopen(kStarts, "w")) { fputs("0\n", f); fclose(f); } }

    // ======================= Brume's settings (sdmc:/brume/reglages.ini): language, ambient colour, theme =======================
    bool g_en = false;
    float g_amb[3] = { 0.22f, 0.42f, 0.95f };
    float g_themeLight = 1.0f;                                  // Réglages > Brume > Thème: its light (1 Brume, .62 Nuit profonde, 1.22 Y2K)
    void ReadBrume() {
        static const float kAmbient[14][3] = { { 0.22f, 0.42f, 0.95f }, { 0.32f, 0.30f, 0.95f }, { 0.52f, 0.26f, 0.90f }, { 0.85f, 0.30f, 0.65f },
                                               { 0.90f, 0.22f, 0.25f }, { 0.95f, 0.55f, 0.18f }, { 0.85f, 0.70f, 0.25f }, { 0.25f, 0.80f, 0.40f },
                                               { 0.15f, 0.75f, 0.55f }, { 0.15f, 0.70f, 0.80f }, { 0.20f, 0.75f, 0.95f }, { 0.60f, 0.65f, 0.75f },
                                               { 0.80f, 0.82f, 0.90f }, { 0.95f, 0.66f, 0.28f } };
        int lang = 0, amb = 0, theme = 0;
        if (FILE *f = fopen("sdmc:/brume/reglages.ini", "r")) {
            char line[96];
            while (fgets(line, sizeof line, f)) {
                int v;
                if (sscanf(line, "langue=%d", &v) == 1) lang = v;
                else if (sscanf(line, "ambiance=%d", &v) == 1 && v >= 0 && v < 14) amb = v;
                else if (sscanf(line, "theme=%d", &v) == 1 && v >= 0 && v < 3) theme = v;
            }
            fclose(f);
        }
        memcpy(g_amb, kAmbient[amb], sizeof g_amb);
        static const float kThemeLight[3] = { 1.0f, 0.62f, 1.22f };   // (the menu's ThemePreset lights)
        g_themeLight = kThemeLight[theme];
        g_en = lang == 2;
        if (lang == 0 && R_SUCCEEDED(setInitialize())) {
            u64 code = 0; SetLanguage l = SetLanguage_FR;
            if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &l))) g_en = !(l == SetLanguage_FR || l == SetLanguage_FRCA);
            setExit();
        }
    }
    const char *L(const char *fr, const char *en) { return g_en ? en : fr; }

    // ======================= time, easing (brume/src/Ease.hpp: ONE motion everywhere) =======================
    float Now() { return (float)((double)armGetSystemTick() / (double)armGetSystemTickFreq()); }
    float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
    constexpr float kPace = 1.35f;
    float Pace(float s) { return s / kPace; }
    void Glide(float &x, float &v, float target, float dt, float omega) {   // a critically damped spring (the menu's)
        omega *= kPace;
        const float h = dt < 0 ? 0 : (dt > 0.1f ? 0.1f : dt);
        const float d = x - target, c = v + omega * d, e = expf(-omega * h);
        x = target + (d + c * h) * e;
        v = (c - omega * (d + c * h)) * e;
    }
    float EaseGlide(float p) {
        if (p <= 0) return 0;
        if (p >= 1) return 1;
        const float a = 7.0f * p;
        return (1.0f - (1.0f + a) * expf(-a)) / (1.0f - 8.0f * expf(-7.0f));
    }
    void Approach(float &v, float target, float dt, float speed) { v += (target - v) * (1 - expf(-dt * speed)); }

    // ======================= system state (each service optional) =======================
    bool g_lbl = false, g_nifm = false, g_setsys = false, g_psm = false, g_time = false;
    int Battery() { u32 p = 0; return g_psm && R_SUCCEEDED(psmGetBatteryChargePercentage(&p)) ? (int)p : -1; }
    bool Charging() { PsmChargerType t = PsmChargerType_Unconnected; return g_psm && R_SUCCEEDED(psmGetChargerType(&t)) && t != PsmChargerType_Unconnected; }

    // ======================= the power actions (the system's own sequences, then the HOME menu as a second path) =======================
    void SamsToHomeMenu(u32 msg) {                             // the message the qlaunch-ext loader carries out (3 sleep, 5 off, 6 reboot)
        AppletStorage s;
        struct { u32 magic, unk, msg, unk2; } h = { 0x534D4153, 1, msg, 0 };
        if (R_FAILED(appletCreateStorage(&s, sizeof h))) return;
        if (R_SUCCEEDED(appletStorageWrite(&s, 0, &h, sizeof h))) appletPushToGeneralChannel(&s);
        else appletStorageClose(&s);
    }
    // v101 (console, 2026-11-23: the menu frozen, « Redémarrer » did nothing twice - the overlay's own sequence is refused
    // (0x1159: the system applet's call) and the message went to a HOME menu that could not carry it out). Now, if the console
    // is still on 6 s after asking the HOME menu, the overlay restarts / powers off by itself through spsm (the system's own
    // clean shutdown: every module told, the SD card closed properly), then bpc as the very last way. The power menu no longer
    // depends on the HOME menu being alive. (Sleep needs the HOME menu: nothing safe to do without it, only written down.)
    int   g_powerWant = -1;                                     // the action waiting for the HOME menu (-1: none)
    u64   g_powerAt = 0;                                        // (the tick it was asked: g_t is defined further down)
    void PowerAction(int what) {                               // 0 sleep, 1 restart, 2 power off
#ifdef PREVIEW
        Log("apercu : action %d (rien n'est fait)", what);
        (void)SamsToHomeMenu;
#else
        Result rc = what == 0 ? appletStartSleepSequence(true) : (what == 1 ? appletStartRebootSequence() : appletStartShutdownSequence());
        if (R_FAILED(rc)) { SamsToHomeMenu(what == 0 ? 3 : (what == 1 ? 6 : 5)); g_powerWant = what; g_powerAt = armGetSystemTick(); }
        Log("alimentation : %s rc=0x%x", what == 0 ? "veille" : (what == 1 ? "redemarrer" : "eteindre"), rc);   // (after: the action first)
#endif
    }
    void PowerWatch() {
#ifndef PREVIEW
        if (g_powerWant < 0 || armTicksToNs(armGetSystemTick() - g_powerAt) < 6000000000ULL) return;   // (6 s: a normal restart through the HOME menu is long over by then)
        const int what = g_powerWant;
        g_powerWant = -1;
        if (what == 0) { Log("alimentation : le menu HOME n'a pas mis la console en veille (6 s)"); return; }
        Log("alimentation : le menu HOME ne repond pas (6 s) - l'overlay %s la console lui-meme", what == 1 ? "redemarre" : "eteint");
        LogFlush();
        Result rc = spsmInitialize();
        if (R_SUCCEEDED(rc)) { rc = spsmShutdown(what == 1); spsmExit(); }
        Log("alimentation : spsm rc=0x%x", rc);
        if (R_FAILED(rc)) {
            rc = bpcInitialize();
            if (R_SUCCEEDED(rc)) { rc = what == 1 ? bpcRebootSystem() : bpcShutdownSystem(); bpcExit(); }
            Log("alimentation : bpc rc=0x%x", rc);
        }
        LogFlush();
#endif
    }

    // ======================= the sounds (v74.4: soft and low, in Brume's key, D major) =======================
    // Made here at the first opening (no file), played by a tiny mixer into audout: while something sounds, the queue gets
    // small buffers (10.7 ms) of the voices mixed. Nothing runs when nothing sounds (audout stopped). Without audout the overlay
    // stays silent (logged). No sound for the volume (the game runs).
    enum Snd { kSndOpen, kSndClose, kSndMove, kSndAdjust, kSndConfirm, kSndCount };
    constexpr int kAudRate = 48000, kAudFrames = 512, kAudBufBytes = 0x1000;
    struct SndPcm { s16 *pcm = nullptr; int frames = 0; };
    SndPcm g_snd[kSndCount];
    struct AudBuf { AudioOutBuffer ob; s16 *pcm; bool queued; };
    AudBuf g_ab[4];
    struct Voice { int snd = -1, pos = 0; float vol = 0; };
    Voice g_voice[4];
    bool g_audTried = false, g_audOk = false, g_audOn = false;
    float g_t = 0, g_dt = 0;
    float g_audQuietAt = 0;                                     // when the last voice ended
    void SynthSound(int k, SndPcm &o) {
        struct Part { float hz, l, r; };
        Part parts[5]; int np = 0;
        float len = 0.3f, atk = 0.01f, dec = 8, gain = 0.1f, noise = 0, natk = 0.05f, ndec = 10, thump = 0;
        switch (k) {
            case kSndOpen:    len = 0.62f; atk = 0.05f; dec = 5.0f; gain = 0.15f; noise = 0.35f; natk = 0.12f; ndec = 7;   // a breath and a warm D
                              parts[np++] = { 293.66f, 0.9f, 0.6f }; parts[np++] = { 440.00f, 0.6f, 0.9f }; parts[np++] = { 587.33f, 0.28f, 0.28f }; break;
            case kSndClose:   len = 0.38f; atk = 0.012f; dec = 9.0f; gain = 0.12f; noise = 0.25f; natk = 0.02f; ndec = 14;   // lower, shorter
                              parts[np++] = { 220.00f, 0.8f, 0.8f }; parts[np++] = { 293.66f, 0.55f, 0.55f }; break;
            case kSndMove:    len = 0.13f; atk = 0.003f; dec = 38; gain = 0.085f;                 // a soft « tok »
                              parts[np++] = { 369.99f, 0.8f, 0.8f }; parts[np++] = { 185.00f, 0.45f, 0.45f }; break;
            case kSndAdjust:  len = 0.11f; atk = 0.003f; dec = 44; gain = 0.075f;
                              parts[np++] = { 293.66f, 0.8f, 0.8f }; parts[np++] = { 146.83f, 0.45f, 0.45f }; break;
            default:          len = 0.66f; atk = 0.006f; dec = 4.6f; gain = 0.14f; thump = 0.6f;   // confirm: a low chord, a soft knock
                              parts[np++] = { 146.83f, 0.7f, 0.7f }; parts[np++] = { 220.00f, 0.6f, 0.6f }; parts[np++] = { 293.66f, 0.45f, 0.45f }; parts[np++] = { 369.99f, 0.35f, 0.35f }; break;
        }
        o.frames = (int)(len * kAudRate);
        o.pcm = (s16 *)malloc((size_t)o.frames * 4);
        if (!o.pcm) { o.frames = 0; return; }
        float lp = 0; u32 rn = 0x1234567u + (u32)k * 977u;
        const float a = 1 - expf(-6.2831853f * 700.0f / kAudRate);   // the breath: noise through a soft low-pass
        for (int i = 0; i < o.frames; i++) {
            const float t = (float)i / kAudRate;
            const float env = (t < atk ? t / atk : 1.0f) * expf(-(t > atk ? t - atk : 0.0f) * dec);
            const float tail = Clamp01((len - t) / 0.025f);       // (no click at its end)
            float Lv = 0, Rv = 0;
            for (int q = 0; q < np; q++) { const float v = sinf(6.2831853f * parts[q].hz * t); Lv += v * parts[q].l; Rv += v * parts[q].r; }
            Lv *= env; Rv *= env;
            if (noise > 0) {
                rn = rn * 1664525u + 1013904223u;
                lp += a * (((rn >> 9) & 0x7FFF) / 16384.0f - 1.0f - lp);
                const float ne = (t < natk ? t / natk : 1.0f) * expf(-(t > natk ? t - natk : 0.0f) * ndec) * noise * 2.2f;
                Lv += lp * ne; Rv += lp * ne;
            }
            if (thump > 0) { const float th = sinf(6.2831853f * 72.0f * t) * expf(-t * 26) * thump; Lv += th; Rv += th; }
            const float gL = Lv * gain * tail * 32767, gR = Rv * gain * tail * 32767;
            o.pcm[2 * i] = (s16)fmaxf(-32767.0f, fminf(32767.0f, gL)); o.pcm[2 * i + 1] = (s16)fmaxf(-32767.0f, fminf(32767.0f, gR));
        }
    }
    void AudioInit() {                                          // (at the first sound: nothing before anyone opens something)
        if (g_audTried) return;
        g_audTried = true;
        Result rc = audoutInitialize();
        if (R_FAILED(rc)) { Log("son : audout indisponible (0x%x) - sans son", rc); return; }
        if (audoutGetSampleRate() != (u32)kAudRate || audoutGetChannelCount() != 2) {
            Log("son : sortie %u Hz %u canaux - sans son", audoutGetSampleRate(), audoutGetChannelCount()); audoutExit(); return;
        }
        for (AudBuf &b : g_ab) {
            b.pcm = (s16 *)memalign(0x1000, kAudBufBytes); b.queued = false;
            if (!b.pcm) { Log("son : pas de memoire - sans son"); audoutExit(); return; }
        }
        for (int k = 0; k < kSndCount; k++) SynthSound(k, g_snd[k]);
        g_audOk = true;
        Log("son : ok");
    }
    void AudioPump() {                                          // keep the queue fed while something sounds; stop when all is quiet
        if (!g_audOn) return;
        bool any = false;
        for (const Voice &v : g_voice) any = any || v.snd >= 0;
        if (any) g_audQuietAt = g_t;
        for (AudBuf &b : g_ab) if (b.queued) { bool in = true; if (R_SUCCEEDED(audoutContainsAudioOutBuffer(&b.ob, &in)) && !in) b.queued = false; }
        if (!any && g_t - g_audQuietAt > 0.3f) {                // (the last buffers have played out)
            audoutStopAudioOut(); g_audOn = false;
            return;
        }
        for (AudBuf &b : g_ab) {
            if (b.queued) continue;
            for (int i = 0; i < kAudFrames; i++) {
                float Lv = 0, Rv = 0;
                for (Voice &v : g_voice) {
                    if (v.snd < 0) continue;
                    const SndPcm &sp = g_snd[v.snd];
                    if (v.pos >= sp.frames) { v.snd = -1; continue; }
                    Lv += sp.pcm[2 * v.pos] * v.vol; Rv += sp.pcm[2 * v.pos + 1] * v.vol; v.pos++;
                }
                b.pcm[2 * i] = (s16)fmaxf(-32767.0f, fminf(32767.0f, Lv)); b.pcm[2 * i + 1] = (s16)fmaxf(-32767.0f, fminf(32767.0f, Rv));
            }
            armDCacheFlush(b.pcm, kAudBufBytes);
            b.ob.next = nullptr; b.ob.buffer = b.pcm; b.ob.buffer_size = kAudBufBytes; b.ob.data_size = kAudFrames * 4; b.ob.data_offset = 0;
            b.queued = R_SUCCEEDED(audoutAppendAudioOutBuffer(&b.ob));
        }
    }
    void Play(int k, float vol = 1.0f) {
        AudioInit();
        if (!g_audOk || !g_snd[k].pcm) return;
        Voice *slot = &g_voice[0];                              // a free voice, else the one furthest along
        for (Voice &v : g_voice) { if (v.snd < 0) { slot = &v; break; } if (v.pos > slot->pos) slot = &v; }
        slot->snd = k; slot->pos = 0; slot->vol = vol;
        if (!g_audOn) { if (R_FAILED(audoutStartAudioOut())) return; g_audOn = true; }
        g_audQuietAt = g_t;
        AudioPump();
    }

    // ======================= v74.4 - « Performance »: sys-clk (CPU / GPU / RAM clocks, Nintendo's official ones only) =======================
    // sys-clk (github.com/retronx-team/sys-clk, « Beer-Ware » licence) answers on its IPC service « sys:clk ». Its context and its
    // commands (GetCurrentContext = 2, SetOverride = 8) are written here from its public header (sysclk/ipc.h, clock_manager.h,
    // board.h of 2.0.1). An override holds for every program until « Auto » takes it off: then sys-clk's own profiles
    // (/config/sys-clk/config.ini) apply again. Opened with the HOME panel, closed with it (never kept: its manager app may need it).
    struct ClkCtx { u8 enabled; u64 app; u32 profile; u32 freqs[3], realFreqs[3], overrideFreqs[3], temps[3]; s32 power[2]; u32 ramLoad[2]; };
    static_assert(sizeof(ClkCtx) == 88, "the sys-clk 2.0.1 context");
    Service g_clk;
    bool g_clkOk = false;
    ClkCtx g_clkCtx = {};
    float g_clkAt = -100;
    int g_perf = 0;                                              // 0 Auto, 1 Économie, 2 Boost, 3 something else (set elsewhere)
    const char *PerfName(int m) {
        static const char *fr[4] = { "Auto", "\xc3\x89" "conomie", "Boost", "Perso" }, *en[4] = { "Auto", "Saver", "Boost", "Custom" };
        return g_en ? en[m & 3] : fr[m & 3];
    }
    bool ClkOpen() {
        if (g_clkOk) return true;
#ifdef BRUME_SIM
        g_clkOk = true;                                         // (the simulator: a made-up sys-clk)
#else
        g_clkOk = R_SUCCEEDED(smGetService(&g_clk, "sys:clk"));
#endif
        return g_clkOk;
    }
    void ClkClose() {
#ifndef BRUME_SIM
        if (g_clkOk) serviceClose(&g_clk);
#endif
        g_clkOk = false;
    }
    bool ClkRead() {
        if (!g_clkOk) return false;
#ifdef BRUME_SIM
        g_clkCtx.profile = 0; g_clkCtx.realFreqs[0] = 1020000000u; g_clkCtx.realFreqs[1] = 460800000u; g_clkCtx.realFreqs[2] = 1600000000u;
        g_clkCtx.temps[0] = 46400; g_clkCtx.temps[1] = 39800;
        return true;
#else
        ClkCtx c = {};
        if (R_FAILED(serviceDispatchOut(&g_clk, 2, c))) return false;
        g_clkCtx = c;
        return true;
#endif
    }
    int ClkMode() {
        const u32 *o = g_clkCtx.overrideFreqs;
        if (!o[0] && !o[1] && !o[2]) return 0;
        if (o[0] == 1785000000u) return 2;
        if (o[0] == 1020000000u && o[1] == 307200000u) return 1;
        return 3;
    }
    void ClkSet(int mode) {                                     // 0 off (the profiles), 1 Économie, 2 Boost
        const bool docked = g_clkCtx.profile == 4;              // (SysClkProfile_Docked)
        u32 hz[3] = { 0, 0, 0 };
        if (mode == 1) { hz[0] = 1020000000u; hz[1] = 307200000u; hz[2] = 1331200000u; }
        else if (mode == 2) { hz[0] = 1785000000u; hz[1] = docked ? 768000000u : 460800000u; hz[2] = 1600000000u; }
        Log("performance : %s", PerfName(mode));
#ifndef BRUME_SIM
        for (u32 m = 0; m < 3; m++) { const struct { u32 module, hz; } a = { m, hz[m] }; serviceDispatchIn(&g_clk, 8, a); }
#else
        for (int m = 0; m < 3; m++) g_clkCtx.overrideFreqs[m] = hz[m];
#endif
    }

    // ======================= v98 - the « centre de contrôle »: what the HOME panel reads (ONLY while it is open) =======================
    // Each source is opened when the panel opens and closed when it closes (never kept: the system and the HOME menu may need
    // it), read once a second at most while it is open, never while it is closed. Each one is optional: a call that fails
    // hides its line and nothing else.

    // -- the volume (audctl): the active output's own volume, 0..15 like the system's (the TV's volume is the TV's: no row) --
    bool g_audc = false, g_tvOut = false, g_mute = false;
    AudioTarget g_audTarget = AudioTarget_Speaker;
    s32 g_volMin = 0, g_volMax = 15, g_volume = 0;
    float g_volumeShown = 0;                                    // (the lights, in steps above the minimum: they glide)
    bool AudcRead() {
        s32 v = 0;
        if (!g_audc || R_FAILED(audctlGetTargetVolume(&v, g_audTarget))) return false;
        g_volume = v < g_volMin ? g_volMin : (v > g_volMax ? g_volMax : v);
        bool m = false;
        g_mute = R_SUCCEEDED(audctlIsTargetMute(&m, g_audTarget)) && m;
        return true;
    }
    void AudcOpen() {
        if (g_audc || R_FAILED(audctlInitialize())) return;
        g_audc = true;
        AudioTarget t = AudioTarget_Invalid;
        if (R_FAILED(audctlGetActiveOutputTarget(&t))) t = AudioTarget_Invalid;
        if (t == AudioTarget_Invalid && (R_FAILED(audctlGetDefaultTarget(&t)) || t == AudioTarget_Invalid)) t = AudioTarget_Speaker;
        g_audTarget = t; g_tvOut = t == AudioTarget_Tv;
        s32 mn = 0, mx = 15;
        if (R_FAILED(audctlGetTargetVolumeMin(&mn))) mn = 0;
        if (R_FAILED(audctlGetTargetVolumeMax(&mx))) mx = 15;
        if (mx <= mn || mx - mn > 40) { mn = 0; mx = 15; }        // (defensive: the lights are made for a few steps)
        g_volMin = mn; g_volMax = mx;
        if (!AudcRead()) { audctlExit(); g_audc = false; return; }
        g_volumeShown = g_mute ? 0.0f : (float)(g_volume - g_volMin);
    }
    void AudcClose() { if (g_audc) audctlExit(); g_audc = false; }
    void AudcSet(int v) {                                       // a press on ◀▶: one step (it also takes the mute off)
        if (!g_audc) return;
        v = v < g_volMin ? g_volMin : (v > g_volMax ? g_volMax : v);
        if (g_mute && R_SUCCEEDED(audctlSetTargetMute(g_audTarget, false))) g_mute = false;
        if (R_SUCCEEDED(audctlSetTargetVolume(g_audTarget, v))) g_volume = v;
    }
    void AudcMute() { if (g_audc && R_SUCCEEDED(audctlSetTargetMute(g_audTarget, !g_mute))) g_mute = !g_mute; }

    // -- the live system line: clocks (sys-clk, else clkrst, read only), temperatures (sys-clk, else ts), the game's memory --
    bool g_ts = false, g_tsS[2] = { false, false }, g_clkr = false, g_clkrS[3] = { false, false, false };
    TsSession g_tsSess[2];
    ClkrstSession g_clkrSess[3];
    char g_mon1[96] = "", g_mon2[112] = "";                      // the two lines, made once a second (never in a frame)
    void MonOpen() {                                            // (only without sys-clk: it already gives all of it)
        if (!g_clkr && R_SUCCEEDED(clkrstInitialize())) {
            g_clkr = true;
            const PcvModuleId m[3] = { PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC };
            for (int i = 0; i < 3; i++) g_clkrS[i] = R_SUCCEEDED(clkrstOpenSession(&g_clkrSess[i], m[i], 3));
        }
        if (!g_ts && hosversionAtLeast(10, 0, 0) && R_SUCCEEDED(tsInitialize())) {
            g_ts = true;
            const u32 dc[2] = { TsDeviceCode_LocationExternal, TsDeviceCode_LocationInternal };   // the SoC, the board
            for (int i = 0; i < 2; i++) g_tsS[i] = R_SUCCEEDED(tsOpenSession(&g_tsSess[i], dc[i]));
        }
    }
    void MonClose() {
        for (int i = 0; i < 2; i++) if (g_tsS[i]) { tsSessionClose(&g_tsSess[i]); g_tsS[i] = false; }
        if (g_ts) { tsExit(); g_ts = false; }
        for (int i = 0; i < 3; i++) if (g_clkrS[i]) { clkrstCloseSession(&g_clkrSess[i]); g_clkrS[i] = false; }
        if (g_clkr) { clkrstExit(); g_clkr = false; }
    }
    void MonRead() {
        int mhz[3] = { -1, -1, -1 }, deg[2] = { -999, -999 };
        if (g_clkOk) {                                          // sys-clk's context (read just before): the real clocks, its sensors
            for (int i = 0; i < 3; i++) if (g_clkCtx.realFreqs[i] > 0 && g_clkCtx.realFreqs[i] < 4000000000u) mhz[i] = (int)(g_clkCtx.realFreqs[i] / 1000000u);
            for (int i = 0; i < 2; i++) if (g_clkCtx.temps[i] > 0 && g_clkCtx.temps[i] < 150000u) deg[i] = (int)((g_clkCtx.temps[i] + 500) / 1000);
        } else {
            for (int i = 0; i < 3; i++) { u32 hz = 0; if (g_clkrS[i] && R_SUCCEEDED(clkrstGetClockRate(&g_clkrSess[i], &hz)) && hz) mhz[i] = (int)(hz / 1000000u); }
            for (int i = 0; i < 2; i++) { float c = 0; if (g_tsS[i] && R_SUCCEEDED(tsSessionGetTemperature(&g_tsSess[i], &c)) && c > -40 && c < 150) deg[i] = (int)lroundf(c); }
        }
        u64 tot = 0, used = 0;                                  // the Application pool (the game's memory)
        const bool mem = R_SUCCEEDED(svcGetSystemInfo(&tot, 0, INVALID_HANDLE, 0)) && R_SUCCEEDED(svcGetSystemInfo(&used, 1, INVALID_HANDLE, 0)) && tot > 0 && used <= tot;
        static const char *kClk[3] = { "CPU", "GPU", "RAM" };
        static const char *kSep = "  \xc2\xb7  ";
        size_t n = 0; g_mon1[0] = 0;
        for (int i = 0; i < 3; i++) if (mhz[i] > 0 && n < sizeof g_mon1) n += snprintf(g_mon1 + n, sizeof g_mon1 - n, "%s%s %d", n ? kSep : "", kClk[i], mhz[i]);
        if (n && n < sizeof g_mon1) snprintf(g_mon1 + n, sizeof g_mon1 - n, " MHz");
        n = 0; g_mon2[0] = 0;
        if (deg[0] > -999 && n < sizeof g_mon2) n += snprintf(g_mon2 + n, sizeof g_mon2 - n, "SoC %d \xc2\xb0" "C", deg[0]);
        if (deg[1] > -999 && n < sizeof g_mon2) n += snprintf(g_mon2 + n, sizeof g_mon2 - n, "%sPCB %d \xc2\xb0" "C", n ? kSep : "", deg[1]);
        if (mem && n < sizeof g_mon2) {
            char a[16], b[16];
            snprintf(a, sizeof a, "%.1f", (double)used / 1073741824.0); snprintf(b, sizeof b, "%.1f", (double)tot / 1073741824.0);
            if (!g_en) { for (char *c = a; *c; c++) if (*c == '.') *c = ','; for (char *c = b; *c; c++) if (*c == '.') *c = ','; }
            n += snprintf(g_mon2 + n, sizeof g_mon2 - n, "%s%s %s / %s %s", n ? kSep : "", L("M\xc3\xa9moire", "Memory"), a, b, L("Go", "GB"));
        }
    }

    // -- the controllers' batteries (hid's shared memory: no request at all), three at most --
    struct PadInfo { const char *name; int n; int lv[2]; bool chg[2]; };
    PadInfo g_pads[3]; int g_nPads = 0;
    void PadsRead() {
        g_nPads = 0;
        static const HidNpadIdType kIds[9] = { HidNpadIdType_Handheld, HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
                                               HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8 };
        auto Lv = [](const HidPowerInfo &p) { return p.battery_level > 4 ? 4 : (int)p.battery_level; };
        for (HidNpadIdType id : kIds) {
            if (g_nPads >= 3) break;
            const u32 st = hidGetNpadStyleSet(id);
            if (!st) continue;
            PadInfo p = {};
            HidPowerInfo a = {}, b = {};
            if (st & (HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual)) {
                hidGetNpadPowerInfoSplit(id, &a, &b);
                p.name = "Joy-Con"; p.n = 2; p.lv[0] = Lv(a); p.lv[1] = Lv(b); p.chg[0] = a.is_charging; p.chg[1] = b.is_charging;
            } else if (st & (HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight)) {
                hidGetNpadPowerInfoSplit(id, &a, &b);
                const bool left = st & HidNpadStyleTag_NpadJoyLeft;
                p.name = left ? L("Joy-Con G", "Joy-Con L") : L("Joy-Con D", "Joy-Con R"); p.n = 1;
                p.lv[0] = Lv(left ? a : b); p.chg[0] = left ? a.is_charging : b.is_charging;
            } else {
                hidGetNpadPowerInfoSingle(id, &a);
                p.name = (st & HidNpadStyleTag_NpadFullKey) ? "Pro" : L("Manette", "Pad"); p.n = 1; p.lv[0] = Lv(a); p.chg[0] = a.is_charging;
            }
            g_pads[g_nPads++] = p;
        }
    }

    // -- the game being played: its process (pm), its name (ns), when it was launched (pdm) - asked once per game --
    struct GameNow { u64 pid, app, launch; bool known, launchOk; char name[96]; } g_game = {};
    char g_session[64] = "";                                    // « En jeu depuis 1 h 20 » (made once a second)
    bool LaunchTime(u64 app, u64 &posix) {                      // its last « launch » in the play log, from the end (256 entries at most)
        if (R_FAILED(pdmqryInitialize())) return false;
        static PdmPlayEvent ev[32];
        bool found = false;
        s32 total = 0, first = 0, last = 0;
        if (R_SUCCEEDED(pdmqryGetAvailablePlayEventRange(&total, &first, &last)) && total > 0 && last >= first) {
            for (int b = 0; b < 8 && !found; b++) {
                s32 from = last - 31 - b * 32;
                if (from + 31 < first) break;
                if (from < first) from = first;
                s32 n = 0;
                if (R_FAILED(pdmqryQueryPlayEvent(from, ev, 32, &n)) || n <= 0) break;
                if (n > 32) n = 32;
                for (int i = n - 1; i >= 0 && !found; i--) {
                    const PdmPlayEvent &e = ev[i];
                    if (e.play_event_type != PdmPlayEventType_Applet || e.event_data.applet.applet_id != AppletId_application ||
                        e.event_data.applet.event_type != PdmAppletEventType_Launch) continue;
                    const u64 id = ((u64)e.event_data.applet.program_id[0] << 32) | e.event_data.applet.program_id[1];   // (stored swapped)
                    if ((id & ~0xFFFull) != (app & ~0xFFFull)) continue;
                    posix = e.timestamp_user; found = posix > 0;
                }
                if (from == first) break;
            }
        }
        pdmqryExit();
        return found;
    }
    void GameRead() {
        u64 pid = 0;
        bool ok = false;
        if (R_SUCCEEDED(pmdmntInitialize())) { ok = R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid)); pmdmntExit(); }
        if (!ok && R_SUCCEEDED(pmshellInitialize())) { ok = R_SUCCEEDED(pmshellGetApplicationProcessIdForShell(&pid)); pmshellExit(); }
        if (!ok || !pid) { g_game = {}; return; }               // (no game: the HOME menu alone)
        if (g_game.known && g_game.pid == pid) return;          // (the same game: already known)
        u64 prog = 0;
        ok = false;
        if (R_SUCCEEDED(pminfoInitialize())) { ok = R_SUCCEEDED(pminfoGetProgramId(&prog, pid)); pminfoExit(); }
        g_game = {};
        if (!ok || !prog) return;
        g_game.pid = pid; g_game.app = prog & ~0xFull; g_game.known = true;   // (a game's other programs: the same title)
        if (R_SUCCEEDED(nsInitialize())) {                      // its name, in the console's language (0x24000 bytes, given back at once)
            NsApplicationControlData *cd = (NsApplicationControlData *)malloc(sizeof(NsApplicationControlData));
            u64 sz = 0;
            if (cd && (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, g_game.app, cd, sizeof *cd, &sz)) ||
                       R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, g_game.app, cd, sizeof *cd, &sz))) && sz >= sizeof cd->nacp) {
                NacpLanguageEntry *le = nullptr;
                if (R_SUCCEEDED(nacpGetLanguageEntry(&cd->nacp, &le)) && le) {
                    le->name[sizeof le->name - 1] = 0;
                    size_t o = 0;
                    for (const char *s = le->name; *s && o < sizeof g_game.name - 1; s++) if ((u8)*s >= 0x20) g_game.name[o++] = *s;   // (no control characters)
                    g_game.name[o] = 0;
                    while (o > 0 && ((u8)g_game.name[o - 1] & 0xC0) == 0x80) o--;   // (never half a character at the end)
                    if (o > 0 && (u8)g_game.name[o - 1] >= 0xC0) g_game.name[--o] = 0;
                }
            }
            free(cd);
            nsExit();
        }
        g_game.launchOk = LaunchTime(g_game.app, g_game.launch);
    }
    void SessionText() {
        g_session[0] = 0;
        u64 now = 0;
        if (!g_game.launchOk || !g_time || R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &now)) || now < g_game.launch) return;
        const u64 m = (now - g_game.launch) / 60;
        if (m > 7 * 24 * 60) return;                            // (the clock was changed: no nonsense)
        if (m < 1) snprintf(g_session, sizeof g_session, "%s", L("Lanc\xc3\xa9 \xc3\xa0 l'instant", "Just started"));
        else if (m < 60) snprintf(g_session, sizeof g_session, g_en ? "Playing for %u min" : "En jeu depuis %u min", (unsigned)m);
        else snprintf(g_session, sizeof g_session, g_en ? "Playing for %u h %02u" : "En jeu depuis %u h %02u", (unsigned)(m / 60), (unsigned)(m % 60));
    }

    // ======================= FUTURE: the « Musique » row (Spotify: now playing + play / pause / next) =======================
    // Left here for the task that will plug Spotify in (docs/RECHERCHE-INTEGRATIONS.md §3.5). Hidden: kMusicRow is false and
    // nothing fills g_music. RULES for whoever fills it: no network in this program (the data comes from elsewhere - a small
    // file or an IPC fed by the Spotify engine), read only while the panel is open (MusicPoll, called with the rest once a
    // second), every failure = available false (the row disappears). A = play / pause, ▶ = next, ◀ = previous.
    constexpr bool kMusicRow = false;
    struct MusicNow { bool available, playing; char title[64], artist[48]; } g_music = {};
    void MusicPoll() {}                                         // TODO(musique): fill g_music (and its title / artist, UTF-8)
    void MusicCommand(int cmd) { (void)cmd; }                   // TODO(musique): 0 play / pause, 1 next, 2 previous

    // ======================= pixels: premultiplied RGBA (R | G << 8 | B << 16 | A << 24, the screen's order) =======================
    // Premultiplied: « over » is d = s + d (1 - sa), no division; a light is added with alpha 0 (the menu's ui_fsh mode 27).
    struct Col { float r, g, b, a; };
    inline int C255(float v) { return v <= 0 ? 0 : (v >= 255 ? 255 : (int)(v + 0.5f)); }
    inline u32 Pack4(int r, int g, int b, int a) { return (u32)r | ((u32)g << 8) | ((u32)b << 16) | ((u32)a << 24); }
    inline u32 Pre(Col c, float a) {                            // c at opacity a, premultiplied
        const float k = Clamp01(c.a * a) * 255.0f;
        return Pack4(C255(c.r * k), C255(c.g * k), C255(c.b * k), C255(k));
    }
    inline u32 AddCol(Col c, float a) { const float k = c.a * a * 255.0f; return Pack4(C255(c.r * k), C255(c.g * k), C255(c.b * k), 0); }
    inline u32 SatAdd(u32 a, u32 b) {                          // a + b, each byte stopping at 255
        const u32 s = (a & 0x7F7F7F7Fu) + (b & 0x7F7F7F7Fu), sum = s ^ ((a ^ b) & 0x80808080u);
        const u32 carry = ((a & b) | ((a | b) & s)) & 0x80808080u;
        return sum | ((carry >> 7) * 0xFFu);
    }
    inline u32 Scale(u32 c, u32 m) {                            // each byte x m / 256 (m 0..256)
        const u32 rb = (((c & 0x00FF00FFu) * m) >> 8) & 0x00FF00FFu;
        const u32 ag = (((c >> 8) & 0x00FF00FFu) * m) & 0xFF00FF00u;
        return rb | ag;
    }
    inline u32 Over(u32 d, u32 s) {                             // s over d (both premultiplied)
        const u32 ia = 255 - (s >> 24);
        if (ia == 0) return s;
        u32 rb = (d & 0x00FF00FFu) * ia + 0x00800080u; rb = ((rb + ((rb >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu;
        u32 ag = ((d >> 8) & 0x00FF00FFu) * ia + 0x00800080u; ag = (ag + ((ag >> 8) & 0x00FF00FFu)) & 0xFF00FF00u;
        return SatAdd(s, rb | ag);
    }
    inline u32 Avg(u32 a, u32 b) { return (a & b) + (((a ^ b) & 0xFEFEFEFEu) >> 1); }   // (a + b) / 2, each byte

    // ======================= the UI layer's picture: 560 x 720, part of the program (it can never be missing) =======================
    constexpr int UW = 560, UH = 720;
    u32 g_canvas[UW * UH] __attribute__((aligned(64)));
    struct Rect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool Empty() const { return x1 <= x0 || y1 <= y0; }
    };
    inline Rect R(int x0, int y0, int x1, int y1) { Rect r; r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; return r; }
    inline Rect Union(Rect a, Rect b) { if (a.Empty()) return b; if (b.Empty()) return a; return R(a.x0 < b.x0 ? a.x0 : b.x0, a.y0 < b.y0 ? a.y0 : b.y0, a.x1 > b.x1 ? a.x1 : b.x1, a.y1 > b.y1 ? a.y1 : b.y1); }
    inline Rect Inter(Rect a, Rect b) { return R(a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1); }
    const Rect kAll = { 0, 0, UW, UH };
    Rect g_clip = { 0, 0, UW, UH };                            // every drawing stays inside it (what changed this frame)
    inline Rect ClipBox(float x0, float y0, float x1, float y1) { return Inter(g_clip, R((int)floorf(x0), (int)floorf(y0), (int)ceilf(x1), (int)ceilf(y1))); }
    void ClearRect(Rect r) { r = Inter(r, kAll); for (int y = r.y0; y < r.y1; y++) memset(g_canvas + y * UW + r.x0, 0, (size_t)(r.x1 - r.x0) * 4); }

    // ---- a soft light ADDED, no edge at all (Ui::Light, ui_fsh mode 27): a band that fades on every side, or a round glow ----
    void Light(float x0, float y0, float x1, float y1, Col c, float a, bool round) {
        if (a <= 0.002f || x1 - x0 < 1 || y1 - y0 < 1) return;
        const Rect b = ClipBox(x0, y0, x1, y1);
        if (b.Empty()) return;
        const u32 add = AddCol(c, a);
        if (!add) return;
        const float iw = 1.0f / (x1 - x0), ih = 1.0f / (y1 - y0);
        int col[UW];
        for (int x = b.x0; x < b.x1; x++) {
            const float q = Clamp01((x + 0.5f - x0) * iw);
            if (round) { const float c2 = q * 2 - 1; col[x] = (int)(c2 * c2 * 65536.0f); }
            else col[x] = (int)(sinf(3.14159265f * q) * 256.0f);
        }
        for (int y = b.y0; y < b.y1; y++) {
            const float q = Clamp01((y + 0.5f - y0) * ih);
            u32 *row = g_canvas + y * UW;
            if (round) {
                const float c2 = q * 2 - 1;
                const int ry = (int)(c2 * c2 * 65536.0f);
                if (ry >= 65536) continue;
                for (int x = b.x0; x < b.x1; x++) {             // (no branch: 4 pixels at a time with NEON)
                    const int s = 65536 - col[x] - ry;
                    const u32 t = (u32)(s > 0 ? s : 0) >> 8, k = (t * t) >> 8;
                    row[x] = SatAdd(row[x], Scale(add, k));
                }
            } else {
                const float vy = 1 - fabsf(q * 2 - 1);
                const int rk = (int)(vy * vy * 256.0f);
                if (rk <= 0) continue;
                for (int x = b.x0; x < b.x1; x++) row[x] = SatAdd(row[x], Scale(add, (u32)(col[x] * rk) >> 8));
            }
        }
    }
    // ---- a solid disc (anti-aliased), over ----
    void Dot(float cx, float cy, float r, Col c, float a) {
        const Rect b = ClipBox(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1);
        if (b.Empty()) return;
        const u32 s = Pre(c, a);
        if (!s) return;
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
                if (cov <= 0) continue;
                u32 &d = g_canvas[y * UW + x];
                d = Over(d, cov >= 1 ? s : Scale(s, (u32)(cov * 256)));
            }
    }
    // ---- a streak of light from a to b, soft across (half width h), added (Ui::Streak, the trails) ----
    void Streak(float ax, float ay, float bx, float by, float h, Col c, float a) {
        if (a <= 0.002f || h <= 0.05f) return;
        const Rect b = ClipBox(fminf(ax, bx) - h - 1, fminf(ay, by) - h - 1, fmaxf(ax, bx) + h + 1, fmaxf(ay, by) + h + 1);
        if (b.Empty()) return;
        const u32 add = AddCol(c, a);
        const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, il2 = l2 > 1e-4f ? 1 / l2 : 0, ih = 1 / h;
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                const float px = x + 0.5f - ax, py = y + 0.5f - ay, t = Clamp01((px * dx + py * dy) * il2);
                const float ex = px - t * dx, ey = py - t * dy, k = 1 - sqrtf(ex * ex + ey * ey) * ih;
                if (k <= 0) continue;
                u32 &d = g_canvas[y * UW + x];
                d = SatAdd(d, Scale(add, (u32)(k * k * 256)));
            }
    }
    // ---- a small solid triangle (a value's arrows, the hints): 4 samples a pixel, over ----
    void Tri(float ax, float ay, float bx, float by, float cx, float cy, Col c, float a) {
        const Rect b = ClipBox(fminf(ax, fminf(bx, cx)), fminf(ay, fminf(by, cy)), fmaxf(ax, fmaxf(bx, cx)), fmaxf(ay, fmaxf(by, cy)));
        if (b.Empty()) return;
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (fabsf(area) < 0.01f) return;
        const float sg = area > 0 ? 1.0f : -1.0f;
        const u32 s = Pre(c, a);
        for (int y = b.y0; y < b.y1; y++)
            for (int x = b.x0; x < b.x1; x++) {
                int in = 0;
                for (int q = 0; q < 4; q++) {
                    const float px = x + 0.25f + 0.5f * (q & 1), py = y + 0.25f + 0.5f * (q >> 1);
                    const float e0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * sg, e1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) * sg,
                                e2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) * sg;
                    in += e0 >= 0 && e1 >= 0 && e2 >= 0;
                }
                if (in) { u32 &d = g_canvas[y * UW + x]; d = Over(d, in == 4 ? s : Scale(s, (u32)in * 64)); }
            }
    }
    // ---- a thin bar (the brightness): its height may be fractional, anti-aliased top and bottom, over ----
    void Bar(float x0, float y0, float x1, float y1, Col c, float a) {
        const Rect b = ClipBox(x0, y0, x1, y1);
        if (b.Empty()) return;
        const u32 s = Pre(c, a);
        for (int y = b.y0; y < b.y1; y++) {
            const float cov = Clamp01(fminf(y + 1.0f, y1) - fmaxf((float)y, y0));
            if (cov <= 0) continue;
            const u32 sv = cov >= 1 ? s : Scale(s, (u32)(cov * 256));
            u32 *row = g_canvas + y * UW;
            for (int x = b.x0; x < b.x1; x++) {
                const float cx = Clamp01(fminf(x + 1.0f, x1) - fmaxf((float)x, x0));
                row[x] = Over(row[x], cx >= 1 ? sv : Scale(sv, (u32)(cx * 256)));
            }
        }
    }

    // ---- text: the menu's Outfit, baked (tools/bakefont.py) - each glyph an alpha mask, copied at whole pixels ----
    enum Face { kTitle = 0, kItem = 1, kValue = 2, kSmall = 3, kHint = 4, kButton = 5 };
    u16 g_gidx[kBFaces][0x180];                                 // codepoint -> glyph (0xFFFF: not baked), Latin-1 and Œ œ
    void GlyphsInit() {
        memset(g_gidx, 0xFF, sizeof g_gidx);
        for (int f = 0; f < kBFaces; f++)
            for (int i = 0; i < kBFace[f].count; i++) { const BGlyph &g = kBGlyph[kBFace[f].first + i]; if (g.cp < 0x180) g_gidx[f][g.cp] = (u16)(kBFace[f].first + i); }
    }
    const BGlyph *FindGlyph(int f, u32 cp) {
        if (cp < 0x180) { const u16 i = g_gidx[f][cp]; return i == 0xFFFF ? nullptr : &kBGlyph[i]; }
        for (int i = 0; i < kBFace[f].count; i++) if (kBGlyph[kBFace[f].first + i].cp == cp) return &kBGlyph[kBFace[f].first + i];
        return nullptr;
    }
    u32 NextCp(const char *&p) {
        const u8 c = (u8)*p++;
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0 && *p) return ((c & 0x1F) << 6) | ((u8)*p++ & 0x3F);
        if ((c & 0xF0) == 0xE0 && p[0] && p[1]) { u32 v = ((c & 0x0F) << 12) | (((u8)p[0] & 0x3F) << 6) | ((u8)p[1] & 0x3F); p += 2; return v; }
        return '?';
    }
    float TextW(int f, const char *s, float ls = 0) {
        float w = 0;
        while (*s) { const BGlyph *g = FindGlyph(f, NextCp(s)); if (g) w += g->adv / 64.0f + ls; }
        return w;
    }
    void Text(int f, const char *s, float x, float baseline, Col c, float a, float ls = 0) {
        const u32 cs = Pre(c, a);
        if (!cs) return;
        const int bl = (int)lroundf(baseline);
        if (bl - kBFace[f].asc - 2 >= g_clip.y1 || bl + kBFace[f].desc + 2 <= g_clip.y0) return;   // (outside what is drawn)
        float pen = x;
        while (*s) {
            const BGlyph *g = FindGlyph(f, NextCp(s));
            if (!g) continue;
            const int gx = (int)floorf(pen + 0.5f) + g->bx, gy = bl - g->by;
            pen += g->adv / 64.0f + ls;
            const Rect b = Inter(g_clip, R(gx, gy, gx + g->w, gy + g->h));
            if (b.Empty()) continue;
            for (int y = b.y0; y < b.y1; y++) {
                const u8 *m = kBAtlas + (g->y + y - gy) * kBAtlasW + g->x - gx;
                u32 *row = g_canvas + y * UW;
                for (int xx = b.x0; xx < b.x1; xx++) {
                    const u32 v = m[xx];
                    if (!v) continue;
                    row[xx] = Over(row[xx], v == 255 ? cs : Scale(cs, v + (v >> 7)));
                }
            }
        }
    }
    void TextC(int f, const char *s, float cx, float baseline, Col c, float a, float ls = 0) { Text(f, s, cx - TextW(f, s, ls) * 0.5f, baseline, c, a, ls); }
    void TextR(int f, const char *s, float rx, float baseline, Col c, float a, float ls = 0) { Text(f, s, rx - TextW(f, s, ls), baseline, c, a, ls); }
    // v98: a text cut to fit maxw, with « … » (worked out once, when what it says changes: never in a frame)
    void Fit(int f, const char *s, float maxw, float ls, char *o, size_t on) {
        if (!on) return;
        size_t n = strlen(s);
        if (n > on - 1) { n = on - 1; while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--; }   // (bounded, at a character's start)
        memcpy(o, s, n); o[n] = 0;
        if (TextW(f, o, ls) <= maxw) return;
        const char *ell = "\xe2\x80\xa6";
        const float ew = TextW(f, ell, ls);
        while (n > 0) {
            do n--; while (n > 0 && ((u8)o[n] & 0xC0) == 0x80);   // (back to the start of a character)
            o[n] = 0;
            if (TextW(f, o, ls) + ew <= maxw) break;
        }
        while (n > 0 && o[n - 1] == ' ') o[--n] = 0;
        if (n + 4 <= on) memcpy(o + n, ell, 4);
    }

    // ======================= v98 - « Notes du jeu »: sdmc:/brume/notes/<title id>.txt, read only, shown in the panel =======================
    // Read when the panel opens (4 KB at most, the rest ignored); cleaned (bad UTF-8, control characters, emoji dropped; tabs as
    // spaces), then cut into lines that fit the panel. Nothing in it is ever run or trusted: it is only drawn.
    constexpr int kNoteMax = 4096, kNoteLines = 96, kNoteVis = 12;
    char g_noteWrap[kNoteMax + kNoteLines + 8];
    const char *g_noteLine[kNoteLines];
    int g_noteN = 0, g_noteTop = 0;
    bool g_notesView = false;
    char g_notePeek[96] = "";                                   // its first line, under the row
    void NotesClean(char *s, size_t n) {                        // in place; s[n] = 0 after
        size_t o = 0, i = 0;
        if (n >= 3 && (u8)s[0] == 0xEF && (u8)s[1] == 0xBB && (u8)s[2] == 0xBF) i = 3;   // (the UTF-8 mark)
        while (i < n) {
            const u8 c = (u8)s[i];
            if (c == '\n') { s[o++] = '\n'; i++; continue; }
            if (c == '\t') { s[o++] = ' '; i++; continue; }
            if (c < 0x20 || c == 0x7F) { i++; continue; }
            if (c < 0x80) { s[o++] = (char)c; i++; continue; }
            const int len = (c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 0));
            bool good = len > 0 && i + len <= n;
            for (int k = 1; good && k < len; k++) good = ((u8)s[i + k] & 0xC0) == 0x80;
            if (good && len < 4) { for (int k = 0; k < len; k++) s[o++] = s[i + k]; }   // (4 bytes: emoji, never in the font)
            i += good ? (size_t)len : 1;
        }
        s[o] = 0;
    }
    void ReadNotes(u64 app) {
        g_noteN = 0; g_noteTop = 0; g_notePeek[0] = 0;
        if (!app) return;
        static char raw[kNoteMax + 1];
        char path[64];
#ifdef BRUME_SIM
        snprintf(path, sizeof path, "notes_demo.txt");          // (the simulator: its own demo notes)
#else
        snprintf(path, sizeof path, "sdmc:/brume/notes/%016llX.txt", (unsigned long long)app);
#endif
        FILE *f = fopen(path, "rb");
        if (!f) return;
        const size_t n = fread(raw, 1, kNoteMax, f);
        fclose(f);
        NotesClean(raw, n);
        const float W = 420;                                    // (the panel's text width)
        char *out = g_noteWrap, *end = g_noteWrap + sizeof g_noteWrap - 1;
        auto Emit = [&](const char *s, size_t len) {
            if (g_noteN >= kNoteLines || out + len + 1 > end) return;
            memcpy(out, s, len); out[len] = 0;
            g_noteLine[g_noteN++] = out; out += len + 1;
        };
        auto WidthN = [](const char *s, size_t len) { char tmp[kNoteMax + 1]; memcpy(tmp, s, len); tmp[len] = 0; return TextW(kHint, tmp, 0.2f); };
        {   // the preview under the row: its first paragraph, cut with « … »
            const char *s = raw;
            while (*s == '\n' || *s == ' ') s++;
            char para[160]; size_t k = 0;
            while (s[k] && s[k] != '\n' && k < sizeof para - 1) { para[k] = s[k]; k++; }
            while (k > 0 && ((u8)para[k - 1] & 0xC0) == 0x80) k--;   // (never half a character)
            if (k > 0 && (u8)para[k - 1] >= 0xC0) k--;
            para[k] = 0;
            Fit(kSmall, para, 420, 0.4f, g_notePeek, sizeof g_notePeek);
        }
        const char *p = raw;
        while (*p && g_noteN < kNoteLines) {
            const char *e = strchr(p, '\n');
            if (!e) e = p + strlen(p);
            const char *q = p;
            if (q == e) Emit("", 0);                            // (an empty line stays: paragraphs)
            while (q < e && g_noteN < kNoteLines) {
                while (q < e && *q == ' ') q++;
                if (q >= e) break;
                const char *best = nullptr, *r = q;
                while (r < e) {                                 // as many whole words as fit
                    const char *w = r;
                    while (w < e && *w != ' ') w++;
                    if (WidthN(q, (size_t)(w - q)) > W) break;
                    best = w; r = w;
                    while (r < e && *r == ' ') r++;
                }
                if (!best) {                                    // a word wider than the panel: cut between its characters
                    const char *c = q;
                    while (c < e) { const char *c2 = c; NextCp(c2); if (c2 > e) c2 = e; if (c > q && WidthN(q, (size_t)(c2 - q)) > W) break; c = c2; }
                    best = c > q ? c : e;
                }
                Emit(q, (size_t)(best - q));
                q = best;
            }
            p = *e ? e + 1 : e;
        }
        while (g_noteN > 0 && !g_noteLine[g_noteN - 1][0]) g_noteN--;   // (no empty lines at the end)
    }

    // ======================= the PS2 look's colours (brume/src/Ui.hpp, namespace ps2) =======================
    const Col kTitleC = { 0.95f, 0.86f, 0.40f, 1 }, kTitleGlow = { 0.95f, 0.75f, 0.25f, 1 }, kChoice = { 0.60f, 0.74f, 1.0f, 1 },
              kChosen = { 0.94f, 0.96f, 1.0f, 1 }, kHalo = { 0.32f, 0.52f, 1.0f, 1 }, kFaint = { 0.52f, 0.60f, 0.84f, 1 },
              kWords = { 0.80f, 0.84f, 0.94f, 1 }, kPink = { 1.0f, 0.45f, 0.62f, 1 };
    inline Col Mix(Col a, Col b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t }; }
    inline Col Line(float k) { return Mix(kChoice, kChosen, k); }          // a choice: light blue -> near-white as it gets chosen
    inline float LineA(float k) { return 0.82f + 0.14f * k; }
    void Chosen(float x0, float x1, float cy, float tx, float tw, float k) {   // Ui::Chosen: the chosen line's band and halo
        if (k <= 0.004f) return;
        Light(x0, cy - 38, x1, cy + 38, kHalo, 0.26f * k, false);
        Light(tx - 46, cy - 30, tx + tw + 46, cy + 30, kHalo, 0.42f * k, true);
        Light(tx - 14, cy - 16, tx + tw + 14, cy + 16, { 0.55f, 0.72f, 1.0f, 1 }, 0.22f * k, true);
    }
    // the hints, bottom of the plate as everywhere in the menu: the button's letter in colour, then its word
    float Hint(float x, float baseline, char btn, const char *label, float a) {
        const char b[2] = { btn, 0 };
        Text(kButton, b, x, baseline, btn == 'B' ? kPink : kChoice, a);
        x += TextW(kButton, b) + 10;
        Text(kHint, label, x, baseline, kWords, 0.9f * a, 0.5f);
        return x + TextW(kHint, label, 0.5f) + 30;
    }
    float HintW(char btn, const char *label) { const char b[2] = { btn, 0 }; return TextW(kButton, b) + 10 + TextW(kHint, label, 0.5f) + 30; }

    // ======================= the ring of six lights (brume/src/Busy.cpp, Orbs: the PS2's emblem) =======================
    void OrbAt(int k, float t, float cx, float cy, float rad, float &x, float &y) {
        const float ang = t * 1.6f + k * 1.0471976f, dx = 0.50f * sinf(t * 0.23f), dy = 0.50f * sinf(t * 0.17f + 1.3f);
        x = cx + sinf(ang + dx) * rad; y = cy - cosf(ang + dy) * rad * 0.92f;
    }
    void Orbs(float cx, float cy, float rad, float a, float t) {
        if (a <= 0.003f) return;
        const float sc = fmaxf(0.55f, fminf(1.6f, rad / 22.0f));
        for (int k = 5; k >= 0; k--) {                           // (the leading one last: on top)
            const float lit = 0.25f + 0.75f * powf(0.62f, (float)k);
            float hx, hy;
            OrbAt(k, t, cx, cy, rad, hx, hy);
            const float g = 15.0f * sc;
            Light(hx - g, hy - g, hx + g, hy + g, kHalo, 0.55f * lit * a, true);
            float lx = hx, ly = hy;
            for (int i = 1; i <= 6; i++) {                       // the trail, back in time; it ages into blue (red fades first)
                float x, y;
                OrbAt(k, t - i * 0.1f, cx, cy, rad, x, y);
                const float f = 1.0f - (i - 0.5f) / 7.0f;
                Streak(lx, ly, x, y, 1.9f * sc * (0.6f + 0.4f * f), { 0.8f * f * f * f, 0.9f * f * f, 1.0f * f, 1 }, 1.5f * lit * a * (0.35f + 0.65f * f));
                lx = x; ly = y;
            }
            Dot(hx, hy, 2.3f * sc, Mix(kChosen, kHalo, 0.15f), (0.35f + 0.65f * lit) * a);
        }
    }

    // ======================= the plates: Ui::Surface (ui_fsh mode 30), night mist with rounded (squircle) corners =======================
    // Worked out ONCE into a picture (at start, again only if the ambient colour changes): each frame only copies it.
    inline float SqBox(float px, float py, float w, float h, float r) {
        const float qx = fabsf(px - w * 0.5f) - (w * 0.5f - r), qy = fabsf(py - h * 0.5f) - (h * 0.5f - r);
        const float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0, m2x = mx * mx, m2y = my * my;
        return sqrtf(sqrtf(m2x * m2x + m2y * m2y)) + fminf(fmaxf(qx, qy), 0.0f) - r;
    }
    inline float Smoothstep(float a, float b, float x) { const float t = Clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); }
    float g_expT[1025];                                         // exp(x) for x in [-16, 0], 64 steps per unit (the plates' soft edges)
    void ExpInit() { for (int i = 0; i <= 1024; i++) g_expT[i] = expf(-16.0f + i / 64.0f); }
    inline float FastExp(float x) {                             // (x <= 0; linear between the steps: error < 1e-4)
        if (x <= -16.0f) return 0.0f;
        if (x >= 0.0f) return 1.0f;
        const float f = (x + 16.0f) * 64.0f;
        const int i = (int)f;
        return g_expT[i] + (g_expT[i + 1] - g_expT[i]) * (f - i);
    }
    u32 Hash(u32 x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
    // into buf (bw x bh, premultiplied), rows ry0..ry1 only (cleared first), the plate x0..x1, y0..y1 (its shadow and halo reach
    // 48 px further, 62 below)
    void SurfaceTo(u32 *buf, int bw, int bh, float x0, float y0, float x1, float y1, Col tint, float r, int ry0, int ry1) {
        if (ry0 < 0) ry0 = 0;
        if (ry1 > bh) ry1 = bh;
        if (ry1 > ry0) memset(buf + (size_t)ry0 * bw, 0, (size_t)(ry1 - ry0) * bw * 4);
        const float w = x1 - x0, h = y1 - y0;
        r = fminf(r, 0.5f * fminf(w, h));
        const int ix0 = (int)fmaxf(0, floorf(x0 - 48)), ix1 = (int)fminf((float)bw, ceilf(x1 + 48)), iy0 = (int)fmaxf(0, floorf(y0 - 48)), iy1 = (int)fminf((float)bh, ceilf(y1 + 62));
        static float gx[1024];                                  // the cloud high in it, across (separable)
        for (int x = ix0; x < ix1 && x - ix0 < 1024; x++) { const float fx = Clamp01((x + 0.5f - x0) / w); gx[x - ix0] = expf(-(fx - 0.5f) * (fx - 0.5f) * 2.25f); }
        const float bx0 = x0 - 48, bx1 = x1 + 48, by0 = y0 - 48, by1 = y1 + 62;   // (the shader's quad: its halo fades out before its edge)
        for (int y = iy0 > ry0 ? iy0 : ry0; y < iy1 && y < ry1; y++) {
            const float py = y + 0.5f - y0, fy = Clamp01(py / h);
            const float gy = expf(-(fy + 0.05f) * (fy + 0.05f) * 6.76f);
            const float base[3] = { 0.050f + (0.010f - 0.050f) * fy, 0.066f + (0.013f - 0.066f) * fy, 0.150f + (0.040f - 0.150f) * fy };
            const bool rowIn = py >= 40 && py <= h - 40;
            u32 *row = buf + y * bw;
            for (int x = ix0; x < ix1; x++) {
                const float px = x + 0.5f - x0;
                float o[4];
                if (rowIn && px >= 40 && px <= w - 40) {          // deep inside: the gradient and the cloud only (the edges' terms are ~0)
                    const float lit = 0.11f * gx[x - ix0] * gy;
                    for (int c = 0; c < 3; c++) o[c] = (base[c] + (&tint.r)[c] * lit) * 0.94f;
                    o[3] = 0.94f;
                } else {
                    const float d = SqBox(px, py, w, h, r), inside = 1 - Smoothstep(-1, 1, d);
                    if (inside < 0.004f) {                       // around it: the shadow (14 px lower, wide) and the halo (added)
                        const float ax = x + 0.5f, ay = y + 0.5f;
                        const float edge = Smoothstep(0, 16, fminf(fminf(ax - bx0, bx1 - ax), fminf(ay - by0, by1 - ay)));
                        const float sh = 0.55f * (1 - Smoothstep(-16, 40, SqBox(px, py - 14, w, h, r))) * edge;
                        const float hk = 0.13f * FastExp(-d / 20.0f) * edge;
                        o[0] = tint.r * hk; o[1] = tint.g * hk; o[2] = tint.b * hk; o[3] = sh;
                    } else {
                        const float fx = Clamp01(px / w), e = d < 0 ? -d : 0, side = sinf(3.14159265f * fx);
                        const float top = 0.32f * FastExp(-e * e / 3.5f) * (1 - Smoothstep(0, 0.32f, fy)) * side * side;
                        const float lit = 0.11f * gx[x - ix0] * gy + 0.045f * FastExp(-e / 6.0f) + 0.55f * top;
                        const float sh = 1 - 0.30f * FastExp(-e / 10.0f) * Smoothstep(0.55f, 1, fy);
                        for (int c = 0; c < 3; c++) {
                            const float v = (base[c] + (&tint.r)[c] * lit + 0.45f * top) * sh;
                            o[c] = (&tint.r)[c] * 0.13f + (v * 0.94f - (&tint.r)[c] * 0.13f) * inside;
                        }
                        o[3] = 0.42f + (0.94f - 0.42f) * inside;
                    }
                }
                const float gr = ((Hash((u32)(x * 7919 + y * 104729)) & 255) / 255.0f - 0.5f) * 0.008f;   // a grain: no banding
                const u32 s = Pack4(C255((o[0] + gr) * 255), C255((o[1] + gr) * 255), C255((o[2] + gr) * 255), C255(o[3] * 255));
                row[x] = Over(row[x], s);
            }
        }
    }
    // the plates' places (in the UI layer's picture)
    constexpr float kHX0 = 36, kHY0 = 24, kHX1 = 536, kHY1 = 696, kHR = 38;      // the HOME panel (the layer at the screen's right)
    constexpr float kPX0 = 40, kPY0 = 136, kPX1 = 520, kPY1 = 584, kPR = 44;     // the power menu (the layer at the centre)
    constexpr int kSW = 380, kSH = 56, kSM = 48, kSMB = 62;                         // the small plate (volume, notifications)
    constexpr int kSBW = kSW + 2 * kSM, kSBH = kSH + kSM + kSMB;
    u32 *g_plateHome = nullptr, *g_platePower = nullptr, *g_plateSmall = nullptr;
    u32 g_plateKey = 0;
    Col Tint() { return { g_amb[0], g_amb[1], g_amb[2], 1 }; }
    u32 TintKey() { u32 h = 7; for (float f : g_amb) h = h * 31 + (u32)(f * 1000); return h; }
    // Made at start in one go; when the ambient colour changes (Brume's settings), made again a few rows at a time while
    // nothing is shown, so that the message loop is never held up for long (a screen opened meanwhile uses them as they are).
    int g_pbPlate = 3, g_pbRow = 0;                             // the plate being made (3: none) and its next row
    u32 g_pbKey = 0;
    Col g_pbTint = { 0, 0, 0, 1 };
    u64 g_pbStart = 0;
    void PlatesBegin() {
        static bool expOk = false;
        if (!expOk) { ExpInit(); expOk = true; }
        const size_t big = (size_t)UW * UH * 4;
        if (!g_plateHome) g_plateHome = (u32 *)malloc(big);
        if (!g_platePower) g_platePower = (u32 *)malloc(big);
        if (!g_plateSmall) g_plateSmall = (u32 *)malloc((size_t)kSBW * kSBH * 4);
        g_pbPlate = 0; g_pbRow = 0; g_pbKey = TintKey(); g_pbTint = Tint(); g_pbStart = armGetSystemTick();
    }
    bool PlatesStep(int rows) {                                 // true: all made
        while (rows > 0 && g_pbPlate < 3) {
            u32 *buf = g_pbPlate == 0 ? g_plateHome : (g_pbPlate == 1 ? g_platePower : g_plateSmall);
            const int bw = g_pbPlate == 2 ? kSBW : UW, bh = g_pbPlate == 2 ? kSBH : UH;
            const int n = rows < bh - g_pbRow ? rows : bh - g_pbRow;
            if (buf) {
                if (g_pbPlate == 0) SurfaceTo(buf, bw, bh, kHX0, kHY0, kHX1, kHY1, g_pbTint, kHR, g_pbRow, g_pbRow + n);
                else if (g_pbPlate == 1) SurfaceTo(buf, bw, bh, kPX0, kPY0, kPX1, kPY1, g_pbTint, kPR, g_pbRow, g_pbRow + n);
                else SurfaceTo(buf, bw, bh, kSM, kSM, kSM + kSW, kSM + kSH, g_pbTint, 26, g_pbRow, g_pbRow + n);
            }
            rows -= n; g_pbRow += n;
            if (g_pbRow >= bh) { g_pbPlate++; g_pbRow = 0; }
        }
        if (g_pbPlate < 3) return false;
        g_plateKey = g_pbKey;
        Log("plaques : %s%s%s (%.1f ms en tout)", g_plateHome ? "volet " : "VOLET-ABSENT ", g_platePower ? "alimentation " : "ALIMENTATION-ABSENTE ",
            g_plateSmall ? "petite" : "PETITE-ABSENTE", (double)(armGetSystemTick() - g_pbStart) * 1000.0 / (double)armGetSystemTickFreq());
        return true;
    }
    void BuildPlates() { PlatesBegin(); while (!PlatesStep(1 << 20)) {} }   // (at start, in one go: nobody is looking)
    bool PlatesPending() { return g_pbPlate < 3 || g_plateKey != TintKey(); }
    // a big plate: copied (moved by ox, oy; faded by k) into what is drawn; without its picture, a plain dark rounded plate
    void PlainPlate(float x0, float y0, float x1, float y1, float rad, float k) {   // (the safe drawing: integer, no table)
        const u32 s = Pre({ 0.030f, 0.040f, 0.100f, 1 }, 0.94f * k);
        const Rect b = ClipBox(x0, y0, x1, y1);
        for (int y = b.y0; y < b.y1; y++) {
            const float py = y + 0.5f, dy = py < y0 + rad ? y0 + rad - py : (py > y1 - rad ? py - (y1 - rad) : 0);
            const float inset = dy > 0 ? rad - sqrtf(fmaxf(0.0f, rad * rad - dy * dy)) : 0;
            const int xa = (int)fmaxf((float)b.x0, ceilf(x0 + inset)), xb = (int)fminf((float)b.x1, floorf(x1 - inset));
            u32 *row = g_canvas + y * UW;
            for (int x = xa; x < xb; x++) row[x] = Over(row[x], s);
        }
    }
    // (it writes EVERY pixel of what is drawn: nothing to clear first)
    void BigPlate(const u32 *pic, int ox, int oy, float k, float x0, float y0, float x1, float y1, float rad) {
        if (!pic) { ClearRect(g_clip); PlainPlate(x0 + ox, y0 + oy, x1 + ox, y1 + oy, rad, k); return; }
        const u32 m = (u32)(Clamp01(k) * 256);
        for (int y = g_clip.y0; y < g_clip.y1; y++) {
            const int sy = y - oy;
            u32 *row = g_canvas + y * UW;
            if (sy < 0 || sy >= UH || m == 0) { memset(row + g_clip.x0, 0, (size_t)(g_clip.x1 - g_clip.x0) * 4); continue; }
            const int xa = g_clip.x0 > ox ? g_clip.x0 : ox, xb = g_clip.x1 < UW + ox ? g_clip.x1 : UW + ox;
            if (xa > g_clip.x0) memset(row + g_clip.x0, 0, (size_t)((xa < g_clip.x1 ? xa : g_clip.x1) - g_clip.x0) * 4);
            if (xb <= xa) continue;
            const u32 *src = pic + sy * UW - ox;
            if (m >= 256) memcpy(row + xa, src + xa, (size_t)(xb - xa) * 4);
            else for (int x = xa; x < xb; x++) row[x] = Scale(src[x], m);
        }
    }
    // the small plate at (x0, y0), plate width w: its ends as they are, its middle stretched (a « 9-slice » across), faded by a
    void SmallPlate(float fx0, float fy0, float w, float a) {
        const int x0 = (int)lroundf(fx0), y0 = (int)lroundf(fy0), iw = (int)lroundf(w);
        if (!g_plateSmall) { PlainPlate((float)x0, (float)y0, (float)(x0 + iw), (float)(y0 + kSH), 26, a); return; }
        const int tw = iw + 2 * kSM, cap = kSM + 110;
        const Rect b = Inter(g_clip, R(x0 - kSM, y0 - kSM, x0 - kSM + tw, y0 - kSM + kSBH));
        const u32 m = (u32)(Clamp01(a) * 256);
        if (b.Empty() || !m) return;
        u16 map[UW];                                            // the picture's column for each column drawn (once)
        for (int x = b.x0; x < b.x1; x++) {
            const int u = x - (x0 - kSM);
            int su;
            if (u < cap) su = u;
            else if (u >= tw - cap) su = u - tw + kSBW;
            else su = tw - 2 * cap > 0 ? cap + (u - cap) * (kSBW - 2 * cap) / (tw - 2 * cap) : cap;
            map[x] = (u16)(su < 0 ? 0 : (su >= kSBW ? kSBW - 1 : su));
        }
        for (int y = b.y0; y < b.y1; y++) {
            const u32 *src = g_plateSmall + (y - (y0 - kSM)) * kSBW;
            u32 *row = g_canvas + y * UW;
            for (int x = b.x0; x < b.x1; x++) {
                const u32 v = src[map[x]];
                if (v) row[x] = Over(row[x], m >= 256 ? v : Scale(v, m));
            }
        }
    }

    // ======================= copying into the display's buffer: its « block linear » layout (Tegra X1) =======================
    // As libnx's framebufferEnd does (read from its code): GOBs of 64 bytes x 8 rows, 16 GOBs high per block (128 rows),
    // the blocks one after the other along a row. Only the rectangle that changed is copied, 16 bytes at a time.
    [[maybe_unused]] void SwizzleRect(u8 *dst, u32 stride, const u32 *src, int srcW, Rect r, int dx = 0) {   // (dx: where the picture starts in the buffer, pixels, a multiple of 4)
        const int xb0 = (r.x0 & ~3) * 4, xb1 = ((r.x1 + 3) & ~3) * 4, db = dx * 4;
        for (int y = r.y0; y < r.y1; y++) {
            const u8 *s = (const u8 *)(src + (size_t)y * srcW);
            const u32 rowb = (u32)(y >> 7) * stride * 128 + (u32)((y >> 3) & 15) * 512 + (u32)((y >> 1) & 3) * 64 + (u32)(y & 1) * 16;
            for (int x = xb0; x < xb1 && x + db < (int)stride; x += 16) {
                const int xd = x + db;
                const u32 off = rowb + (u32)(xd >> 6) * 8192 + (u32)((xd >> 5) & 1) * 256 + (u32)((xd >> 4) & 1) * 32;
                memcpy(dst + off, s + x, 16);
            }
        }
    }

    // ======================= the fog layer: 320 x 180, enlarged by the display (the dim veil and the menu's fog) =======================
    // The fog is the HOME menu's own (brume/shaders/haze_fsh.glsl), ported to the CPU (v74.3): its fbm made once into a
    // seamless texture, 160 x 90 texels, a twelfth of the rows each frame (it drifts slowly), enlarged twice, then by the display.
    constexpr int FW = 320, FH = 180, BW = 160, BH = 90;
    u32 *g_fog = nullptr, *g_fogOut = nullptr;                  // the fog (BW x BH, opaque light), the layer's picture (FW x FH, premultiplied)
    u8 *g_gain = nullptr, *g_spot = nullptr, *g_vig = nullptr, *g_fogA = nullptr;   // the cloud's body, its backlight, the rim; the veil's opacity
    u16 *g_fbm = nullptr;
    bool g_fogOk = false;
    float g_at = 0, g_speed = 1, g_lastInput = -100;            // the atmosphere's own clock: it slows to a stop when nobody touches
    constexpr float kFreeze = 20.0f;                            // s without a touch -> everything slows down and stops (nothing drawn)
    float g_expLut[64];
    float g_hcx = 640, g_hcy = 330, g_hrx = 640, g_hry = 330;   // the cloud (native screen pixels)
    void MakeFbm() {                                            // 5 octaves of value noise (8 .. 128 cells), seamless, ~1 at most
        g_fbm = (u16 *)malloc(256 * 256 * 2);
        float *acc = (float *)calloc(256 * 256, 4);
        if (!g_fbm || !acc) { free(acc); free(g_fbm); g_fbm = nullptr; return; }
        float amp = 0.5f;
        for (int o = 0, cells = 8; o < 5; o++, cells *= 2, amp *= 0.5f) {
            const float step = 256.0f / cells;
            auto Lv = [&](int i, int j) { return (Hash((u32)((i & (cells - 1)) + (j & (cells - 1)) * 977 + o * 7919) * 2654435761u + 0x51A7u) & 0xFFFF) / 65535.0f; };
            for (int y = 0; y < 256; y++) {
                const float fy = y / step; const int iy = (int)fy; float ty = fy - iy; ty = ty * ty * (3 - 2 * ty);
                for (int x = 0; x < 256; x++) {
                    const float fx = x / step; const int ix = (int)fx; float tx = fx - ix; tx = tx * tx * (3 - 2 * tx);
                    const float a = Lv(ix, iy) + (Lv(ix + 1, iy) - Lv(ix, iy)) * tx, b = Lv(ix, iy + 1) + (Lv(ix + 1, iy + 1) - Lv(ix, iy + 1)) * tx;
                    acc[y * 256 + x] += amp * (a + (b - a) * ty);
                }
            }
        }
        for (int i = 0; i < 256 * 256; i++) g_fbm[i] = (u16)(fminf(acc[i], 0.9999f) * 65535);
        free(acc);
    }
    inline float Fbm(float qx, float qy) {                      // the menu's Fbm(q): one noise unit = 32 texels, seamless (integer bilinear)
        const int X = (int)(qx * 8192.0f), Y = (int)(qy * 8192.0f);
        const int ix = (X >> 8) & 255, iy = (Y >> 8) & 255, jx = (ix + 1) & 255, jy = (iy + 1) & 255, tx = X & 255, ty = Y & 255;
        const u16 *r0 = g_fbm + iy * 256, *r1 = g_fbm + jy * 256;
        const int a = (r0[ix] * (256 - tx) + r0[jx] * tx) >> 8, b = (r1[ix] * (256 - tx) + r1[jx] * tx) >> 8;
        return (float)(a * (256 - ty) + b * ty) * (1.0f / (65535.0f * 256.0f));
    }
    inline float FogLayer(float qx, float qy, float t, float sp) {   // the menu's Fog(): warped fbm (wisps and curls, not blobs)
        const float wx = Fbm(qx, qy + t * 0.05f * sp), wy = Fbm(qx + 5.2f - t * 0.04f * sp, qy + 1.3f);
        return Fbm(qx + 1.9f * wx + t * 0.02f * sp, qy + 1.9f * wy - t * 0.012f * sp);
    }
    void FogInit() {                                            // ~0.5 MB from the heap; without it: no veil (the plates still show)
        g_fog = (u32 *)malloc((size_t)BW * BH * 4); g_fogOut = (u32 *)malloc((size_t)FW * FH * 4);
        g_gain = (u8 *)malloc(BW * BH); g_spot = (u8 *)malloc(BW * BH); g_vig = (u8 *)malloc(BW * BH); g_fogA = (u8 *)malloc(FW * FH);
        MakeFbm();
        for (int i = 0; i < 64; i++) g_expLut[i] = expf(-(i + 0.5f) / 10.5f);
        g_fogOk = g_fog && g_fogOut && g_gain && g_spot && g_vig && g_fogA && g_fbm;
        if (!g_fogOk) { Log("brume : pas de memoire (sans voile)"); return; }
        memset(g_fog, 0, (size_t)BW * BH * 4); memset(g_fogOut, 0, (size_t)FW * FH * 4);
    }
    void NebulaRows(int ya, int yb) {                           // the menu's haze_fsh, texel by texel (160 x 90: 8 screen pixels each)
        if (!g_fbm) return;
        const float t = g_at, breath = 0.85f + 0.15f * sinf(t * 0.42f), S = 0.84f, TL = g_themeLight;   // (S: the menu once unlocked)
        const float A[3] = { g_amb[0], g_amb[1], g_amb[2] };
        const float fc[3] = { A[0] * 0.227f * S, A[1] * 0.262f * S, A[2] * 0.316f * S };
        float mx[3], my[3], mi[3], mc[3][3];                    // the 3 soft lights drifting through (never a beam)
        const float sx = g_hrx / 640.0f;
        for (int k = 0; k < 3; k++) {
            mx[k] = g_hcx + 430 * sx * sinf(t * (0.045f + 0.017f * k) + k * 2.1f);
            my[k] = g_hcy + 170 * sinf(t * (0.063f + 0.013f * k) + k * 4.0f);
            const float r = (110 + 30 * k) * (0.5f + 0.5f * sx); mi[k] = 1 / (r * r);
            for (int c = 0; c < 3; c++) {
                const float rot1 = A[(c + 2) % 3], rot2 = A[(c + 1) % 3];   // A.zxy, A.yzx
                mc[k][c] = k == 0 ? A[c] * (c == 1 ? 0.81f : 1.0f) : (k == 1 ? A[c] + (rot1 - A[c]) * 0.35f : A[c] + (rot2 - A[c]) * 0.35f);
            }
        }
        for (int y = ya; y < yb; y++) {
            const float py = (y + 0.5f) * 8, g = Smoothstep(0, 720, py);
            const float base[3] = { 0.006f + 0.014f * g, 0.008f + 0.002f * g, 0.020f + 0.020f * g };
            u32 *o = g_fog + y * BW;
            const u8 *bd = g_gain + y * BW, *gw = g_spot + y * BW, *vg = g_vig + y * BW;
            for (int x = 0; x < BW; x++) {
                const float px = (x + 0.5f) * 8;
                const float far = FogLayer(px * 0.0026f, py * 0.0026f, t, 1.0f), near = FogLayer(px * 0.0052f + 3.7f, py * 0.0052f + 9.1f, t, 2.2f);
                const float body = bd[x] * (1 / 255.0f), glow = gw[x] * (1 / 255.0f) * breath;
                const float dens = Smoothstep(0.30f, 0.85f, far * (0.55f + 0.75f * body)) * body;
                const float total = fminf(dens + Smoothstep(0.45f, 0.95f, near) * body * 0.6f, 1.0f);
                const float k = glow * (0.35f + 0.65f * total) * (1 - 0.45f * dens) * S * 0.40f + glow * Smoothstep(0.15f, 0.0f, fabsf(dens - 0.35f)) * 0.09f * S;
                float col[3] = { base[0] + fc[0] * total + A[0] * k, base[1] + fc[1] * total + A[1] * k, base[2] + fc[2] * total + A[2] * k };
                const float lk = (0.03f + 0.05f * total) * S;
                for (int m = 0; m < 3; m++) {
                    const float ddx = px - mx[m], ddy = py - my[m], d2 = (ddx * ddx + ddy * ddy) * mi[m];
                    if (d2 >= 6) continue;
                    const float e = g_expLut[(int)(d2 * 10.5f)] * lk;
                    col[0] += mc[m][0] * e; col[1] += mc[m][1] * e; col[2] += mc[m][2] * e;
                }
                const float vk = TL * 255 * vg[x] * (1 / 255.0f);
                o[x] = Pack4(C255(col[0] * vk), C255(col[1] * vk), C255(col[2] * vk), 0);
            }
        }
    }
    void FogShape(bool quick) {                                 // per screen: the cloud's body, backlight, rim; the veil's opacity
        if (!g_fogOk) return;
        g_hcx = quick ? 720 + (kHX0 + kHX1) * 0.5f : 640.0f; g_hcy = quick ? 360.0f : 330.0f; g_hrx = quick ? 420.0f : 640.0f; g_hry = 330;
        for (int y = 0; y < BH; y++)
            for (int x = 0; x < BW; x++) {
                const float px = (x + 0.5f) * 8, py = (y + 0.5f) * 8, dx = (px - g_hcx) / g_hrx, dy = (py - g_hcy) / g_hry;
                g_gain[y * BW + x] = (u8)(expf(-(dx * dx * 1.1f + dy * dy * 2.6f)) * 255 + 0.5f);
                g_spot[y * BW + x] = (u8)(expf(-(dx * dx + dy * dy) * 3.2f) * 255 + 0.5f);
                const float vy = (py - 360) / 360.0f;
                g_vig[y * BW + x] = (u8)((1 - 0.40f * Smoothstep(0.45f, 1.25f, sqrtf(dx * dx + vy * vy))) * 255);
            }
        for (int y = 0; y < FH; y++)                            // the veil: the game dimmed, darker toward the plate (it melts into the fog)
            for (int x = 0; x < FW; x++) {
                const float px = (x + 0.5f) * 4, py = (y + 0.5f) * 4;
                float a;
                if (quick) a = 0.56f + 0.36f * Smoothstep(560, 800, px);   // (the game stays readable on the left)
                else { const float dx = (px - 640) / 640.0f, dy = (py - 340) / 360.0f; a = 0.80f + 0.16f * expf(-(dx * dx + dy * dy) * 2.2f); }   // (the popups' backdrop: the scene melts into the fog)
                g_fogA[y * FW + x] = (u8)(a * 255 + 0.5f);
            }
        NebulaRows(0, BH);
    }
    int g_fogPart = 0;
    void FogStep() { NebulaRows(g_fogPart * BH / 12, (g_fogPart + 1) * BH / 12); g_fogPart = (g_fogPart + 1) % 12; }
    void FogCompose(float p) {                                  // the fog enlarged twice, its veil, faded by p -> the layer's picture
        const u32 pm = (u32)(Clamp01(p) * 256);
        for (int y = 0; y < FH; y++) {
            const u32 *r0 = g_fog + (y >> 1) * BW, *r1 = g_fog + ((y >> 1) + 1 < BH ? (y >> 1) + 1 : BH - 1) * BW;
            u32 line[BW + 1];
            for (int x = 0; x < BW; x++) line[x] = (y & 1) ? Avg(r0[x], r1[x]) : r0[x];
            line[BW] = line[BW - 1];
            u32 *o = g_fogOut + y * FW;
            const u8 *va = g_fogA + y * FW;
            for (int x = 0; x < FW; x++) {
                const u32 c = (x & 1) ? Avg(line[x >> 1], line[(x >> 1) + 1]) : line[x >> 1];
                const u32 a = (va[x] * pm) >> 8;
                o[x] = (pm >= 256 ? c : Scale(c, pm)) | (a << 24);
            }
        }
    }

    // ======================= the screens and their animation =======================
    enum Mode { kIdle, kPower, kQuick };
    Mode g_mode = kIdle, g_shown = kIdle;                      // g_shown stays while it leaves (the closing animation)
    int g_sel = 0;
    float g_openAt = -100, g_closeAt = -100;
    float g_bright = 0.7f, g_brightShown = 0.7f, g_knob[2] = { 0, 0 };
    constexpr int kSelN = 8;
    float g_selK[kSelN] = { 1, 0, 0, 0, 0, 0, 0, 0 };          // how chosen each line is (its colour)
    float g_hy = 0, g_hyv = 0, g_hx = 0, g_hxv = 0, g_hw = 0, g_hwv = 0;   // the chosen line's light: it glides (the menu's spring)
    bool g_hSnap = true;
    float g_pressAt = -100; int g_pressRow = -1;               // a press: the word lights up
    bool g_autoBright = false, g_plane = false;
    struct Toast { char text[96]; float at; bool on; } g_toast = {};
    // v96 (Yass: « l'animation du son refait l'apparition à chaque fois qu'on clique ») - born: when it appeared (its slide
    // in, once); at: the last press (it stays 1.6 s after it); fade: its opacity, which comes back softly when a press
    // catches it fading out (never a pop, never the entrance again)
    bool g_vsyncOk = false, g_vsTick = false;                  // v97: paced by the refresh; the time between two frames
    int g_gapN = 0, g_gapLate = 0; float g_gapSum = 0, g_gapMax = 0, g_prevFrame = -1;
    struct VolBanner { int step; bool muted; float at, shown; bool on; float born, fade; } g_vol = {};
    PadState g_pad;
    constexpr float kOpen = 0.45f, kCloseT = 0.22f;

    // v98: the HOME panel's rows - only those whose service answered when it opened (the order: the most used while playing first)
    enum RowKind { kRowVolume, kRowBright, kRowPerf, kRowPlane, kRowNotes, kRowMusic };
    int g_rows[kSelN], g_nRows = 0;
    char g_title[128] = "";                                     // the game's name cut to the panel (or « Centre de contrôle »)
    int QuickRows() { return g_nRows; }
    const char *QuickTitle() { return g_en ? "Control center" : "Centre de contr\xc3\xb4le"; }
    const char *NotesLabel() { return L("Notes du jeu", "Game notes"); }
    const char *QuickItem(int i) {
        if (i < 0 || i >= g_nRows) return "";
        switch (g_rows[i]) {
            case kRowVolume: return "Volume";
            case kRowBright: return L("Luminosit\xc3\xa9", "Brightness");
            case kRowPerf:   return "Performance";
            case kRowPlane:  return L("Mode avion", "Airplane mode");
            case kRowNotes:  return NotesLabel();
            default:         return L("Musique", "Music");
        }
    }
    void BuildRows() {
        const bool docked = appletGetOperationMode() == AppletOperationMode_Console || g_tvOut;   // (the TV: no screen to light, its own volume)
        g_nRows = 0;
        if (g_audc && !g_tvOut) g_rows[g_nRows++] = kRowVolume;
        if (g_lbl && !docked) g_rows[g_nRows++] = kRowBright;
        if (g_clkOk) g_rows[g_nRows++] = kRowPerf;
        if (g_nifm && g_setsys) g_rows[g_nRows++] = kRowPlane;
        if (g_noteN > 0) g_rows[g_nRows++] = kRowNotes;
        if (kMusicRow && g_music.available) g_rows[g_nRows++] = kRowMusic;
    }
    void QuickTick() {                                          // once a second while the panel is open (never when closed)
        if (g_clkOk) ClkRead();                                 // (a failed read keeps the last clocks)
        MonRead(); PadsRead(); SessionText(); MusicPoll();
    }
    void QuickClose() { ClkClose(); AudcClose(); MonClose(); }  // (everything the panel opened, given back)
    void ReadQuick() {
        if (g_lbl) { float f = 0.7f; if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&f))) g_bright = f; bool b = false; if (R_SUCCEEDED(lblIsAutoBrightnessControlEnabled(&b))) g_autoBright = b; }
        bool w = true, n = true;
        if (g_nifm) nifmIsWirelessCommunicationEnabled(&w);
        if (g_setsys) setsysGetNfcEnableFlag(&n);
        g_plane = !w && !n;
        g_brightShown = g_bright; g_knob[0] = g_autoBright; g_knob[1] = g_plane;
        if (ClkOpen() && ClkRead()) g_perf = ClkMode();         // v74.4: sys-clk (its row only if it answers)
        else ClkClose();
        AudcOpen();                                             // v98: the volume, the live line, the game, its notes
        if (!g_clkOk) MonOpen();
        GameRead();
        ReadNotes(g_game.known ? g_game.app : 0);
        Fit(kTitle, g_game.known && g_game.name[0] ? g_game.name : QuickTitle(), kHX1 - kHX0 - 80, 0.6f, g_title, sizeof g_title);
        QuickTick(); g_clkAt = g_t;
        g_notesView = false;
        BuildRows();
        if (g_sel >= QuickRows()) g_sel = 0;
    }
    void TakeInput(bool on) {                                  // the overlay takes the buttons while it is shown, and gives them back
#ifndef PREVIEW
        if (on) appletBeginToWatchShortHomeButtonMessage();
        else {
            appletEndToWatchShortHomeButtonMessage();
            hidsysExit(); hidsysInitialize(); hidsysEnableAppletToGetInput(true);   // (as Layoff: the input really comes back)
        }
#else
        (void)on;
#endif
    }
    void Open(Mode m) {
        if (g_mode == m) return;
        g_logDefer = true;                                      // (no card access while it opens)
        if (g_mode == kIdle) TakeInput(true);
        if (g_mode == kQuick) QuickClose();                     // (the HOME panel gives way to the power menu: all it opened, given back)
        g_mode = g_shown = m; g_sel = 0; g_openAt = g_t; g_closeAt = -100;
        for (int i = 0; i < kSelN; i++) g_selK[i] = i == 0 ? 1.0f : 0.0f;
        g_hSnap = true; g_pressRow = -1;
        ReadBrume();                                            // (the colours / language chosen in Brume since)
        if (m == kQuick) ReadQuick();
        g_speed = 1; g_lastInput = g_t;
        FogShape(m == kQuick);
        Play(kSndOpen);
        Log("ouvert : %s", m == kPower ? "alimentation" : "volet HOME");
    }
    void Close(bool quiet = false) {
        if (g_mode == kIdle) return;
        if (!quiet) Play(kSndClose);
        if (g_mode == kQuick && g_lbl) lblSaveCurrentSetting();
        if (g_mode == kQuick) QuickClose();                     // (v98: sys-clk, audctl, ts, clkrst - never kept while closed)
        g_mode = kIdle; g_closeAt = g_t;                        // (g_shown leaves with its animation)
        TakeInput(false);                                       // the buttons go back at once
    }
    void ShowToast(const char *t) { snprintf(g_toast.text, sizeof g_toast.text, "%s", t); g_toast.at = g_t; g_toast.on = true; }
    // how much of the open screen is there: 0 -> 1 when it opens (the menu's curve), 1 -> 0 when it closes
    float Presence() {
        if (g_mode != kIdle) return EaseGlide((g_t - g_openAt) / kOpen);
        const float k = 1 - Clamp01((g_t - g_closeAt) / kCloseT);
        if (k <= 0) g_shown = kIdle;
        return k * k;
    }

    // ---- the words ----
    const char *PowerItem(int i) { static const char *fr[4] = { "Mode veille", "Red\xc3\xa9marrer", "\xc3\x89teindre", "Annuler" }, *en[4] = { "Sleep mode", "Restart", "Power off", "Cancel" }; return g_en ? en[i] : fr[i]; }
    const char *PowerTitle() { return g_en ? "Power" : "Alimentation"; }

    // ---- the layout (in the UI layer's picture) ----
    constexpr float kRingY = 196, kRingR = 24, kPTitleY = 264, kPItemY = 332, kPItemGap = 54, kPHintY = 552;   // power
    // v98 HOME panel: the time and battery, the controllers, the game's name, its session, the rows, the live line, the hints
    constexpr float kHHeadY = 66, kHPadY = 94, kHTitleY = 146, kHSubY = 174, kHRowY = 238, kHRowGap = 66, kHMonY = 616, kHMonGap = 22, kHHintY = 668;
    constexpr float kHL = kHX0 + 40, kHRt = kHX1 - 40;                                                        // its margins
    const Rect kRingBox = { (int)(280 - kRingR - 30), (int)(kRingY - kRingR - 30), (int)(280 + kRingR + 30), (int)(kRingY + kRingR + 30) };
    const Rect kPRowsBox = { 0, (int)(kPItemY - 60), UW, (int)(kPItemY + 3 * kPItemGap + 44) };   // (full width: a halo never left behind)
    const Rect kHHeadBox = { 0, 38, UW, 104 };
    const Rect kHTitleBox = { 0, 106, UW, 186 };
    const Rect kHMonBox = { 0, (int)(kHMonY - 20), UW, (int)(kHMonY + kHMonGap + 8) };
    const Rect kHHintBox = { 0, (int)(kHHintY - 24), UW, (int)(kHHintY + 12) };
    const Rect kHRowsBox = { 0, (int)(kHRowY - 50), UW, (int)(kHMonY - 22) };   // (up to 6 rows; the chosen one's halo stays inside)

    // the header of the HOME panel: the clock and the date, the console's battery; the volume or a notification over the clock
    int g_battPct = -1; bool g_battChg = false;                 // (read with the clock, once a second)
    void ClockText(char *d, size_t dn, char *t, size_t tn) {   // (asked to the system at most once a second)
        static char cd[24], ct[48];
        static float at = -100;
        static bool en = false;
        if (g_t - at >= 1.0f || g_t < at || en != g_en) {
            at = g_t; en = g_en;
            cd[0] = ct[0] = 0;
            u64 ts = 0; TimeCalendarTime cal = {}; TimeCalendarAdditionalInfo info;
            if (g_time && R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &ts)) && R_SUCCEEDED(timeToCalendarTimeWithMyRule(ts, &cal, &info))) {
                if (g_en) snprintf(cd, sizeof cd, "%04d/%02d/%02d", cal.year, cal.month, cal.day); else snprintf(cd, sizeof cd, "%02d/%02d/%04d", cal.day, cal.month, cal.year);
                snprintf(ct, sizeof ct, "%02d:%02d", cal.hour, cal.minute);
            }
            g_battPct = Battery(); g_battChg = g_battPct >= 0 && Charging();
        }
        snprintf(d, dn, "%s", cd); snprintf(t, tn, "%s", ct);
    }
    float VolAlpha() {
        return EaseGlide((g_t - g_vol.born) / Pace(0.35f)) * g_vol.fade;
    }
    float ToastAlpha() {
        const float t = g_t - g_toast.at, in = EaseGlide(t / Pace(0.4f)), out = t < 3.0f ? 1.0f : 1 - Clamp01((t - 3.0f) / 0.3f);
        return in * out;
    }
    void LightsRow(float x, float cy, float a, float step, float shown, int count) {   // a row of lights: the lit ones near-white in their glow
        for (int s = 0; s < count; s++) {
            const float lit = Clamp01(shown - s), px = x + s * step;
            if (lit > 0.01f) Light(px - 10, cy - 10, px + 10, cy + 10, kHalo, 0.55f * lit * a, true);
            Dot(px, cy, 2.4f + 0.7f * lit, Mix({ 0.45f, 0.52f, 0.75f, 0.45f }, { 0.88f, 0.93f, 1.0f, 1 }, lit), a);
        }
    }
    void VolumeLights(float x, float cy, float a, float step = 16.0f) { LightsRow(x, cy, a, step, g_vol.shown, 15); }   // (the banner: fifteen)
    void HomeHeader(float ox, float p) {
        char d[24], t[48];
        ClockText(d, sizeof d, t, sizeof t);
        const float va = g_vol.on ? VolAlpha() : 0, ta = g_toast.on ? ToastAlpha() : 0, over = fmaxf(va, ta);
        if (over < 0.999f) {                                    // v98: the clock, near-white, then the date
            Text(kValue, t, kHL + ox, kHHeadY, kChosen, 0.92f * p * (1 - over), 0.4f);
            Text(kSmall, d, kHL + ox + TextW(kValue, t, 0.4f) + 14, kHHeadY, kWords, 0.70f * p * (1 - over), 0.6f);
        }
        if (va > 0.001f) {
            Text(kSmall, g_vol.muted ? L("Muet", "Muted") : "Volume", kHL + ox, kHHeadY, kChoice, 0.95f * p * va, 0.6f);
            VolumeLights(kHL + ox + 84, kHHeadY - 6, p * va, 12.5f);
        } else if (ta > 0.001f) {
            Light(kHL + ox - 14, kHHeadY - 20, kHL + ox + 14, kHHeadY + 8, kHalo, 0.6f * p * ta, true);
            Dot(kHL + ox, kHHeadY - 6, 2.6f, kChosen, p * ta);
            Text(kSmall, g_toast.text, kHL + ox + 14, kHHeadY, kChosen, p * ta, 0.3f);
        }
        if (g_battPct >= 0) {                                   // the console's battery, on the right (pink when low and not charging)
            char b[48];
            if (g_battChg) snprintf(b, sizeof b, g_en ? "Charging  \xc2\xb7  %d %%" : "En charge  \xc2\xb7  %d %%", g_battPct);
            else snprintf(b, sizeof b, "%d %%", g_battPct);
            TextR(kSmall, b, kHRt + ox, kHHeadY, g_battPct <= 15 && !g_battChg ? kPink : kWords, 0.85f * p, 0.6f);
        }
        float x = kHL + ox;                                     // the controllers: each name, then four lights per Joy-Con / pad
        for (int i = 0; i < g_nPads; i++) {                    // (lit = charge left; yellow while charging, pink when nearly empty)
            const PadInfo &pd = g_pads[i];
            Text(kSmall, pd.name, x, kHPadY, kFaint, 0.9f * p, 0.4f);
            x += TextW(kSmall, pd.name, 0.4f) + 12;
            for (int s = 0; s < pd.n; s++) {
                const Col on = pd.chg[s] ? kTitleC : (pd.lv[s] <= 1 ? kPink : kChosen);
                for (int k = 0; k < 4; k++) {
                    const bool lit = k < pd.lv[s];
                    if (lit) Light(x - 7, kHPadY - 13, x + 7, kHPadY + 1, pd.chg[s] ? kTitleGlow : kHalo, 0.40f * p, true);
                    Dot(x, kHPadY - 6, lit ? 2.5f : 1.9f, lit ? on : Col{ 0.45f, 0.52f, 0.75f, 0.45f }, p);
                    x += 9;
                }
                x += 7;
            }
            x += 16;
        }
    }

    // ---- the power menu: a plate in the middle (the menu's popups), the ring of lights, the title, the four choices ----
    void DrawPower(float p) {
        const int oy = (int)lroundf(14 * (1 - p));             // it rises into place as it appears (the popup's)
        BigPlate(g_platePower, 0, oy, p, kPX0, kPY0, kPX1, kPY1, kPR);
        const float cx = (kPX0 + kPX1) * 0.5f, age = g_t - g_openAt;
        Orbs(cx, kRingY + oy, kRingR, p, g_at);
        const float cT = p * EaseGlide((age - 0.03f) / Pace(0.32f));
        const float tw = TextW(kTitle, PowerTitle(), 1.0f);
        Light(cx - tw * 0.5f - 60, kPTitleY - 34 + oy, cx + tw * 0.5f + 60, kPTitleY + 14 + oy, kTitleGlow, 0.14f * cT, true);
        TextC(kTitle, PowerTitle(), cx, kPTitleY + oy, kTitleC, cT, 1.0f);
        Chosen(kPX0 + 16, kPX1 - 16, g_hy + oy, g_hx - g_hw * 0.5f, g_hw, p * EaseGlide((age - 0.08f) / Pace(0.36f)));
        if (g_pressRow >= 0 && g_t - g_pressAt < 0.45f) {      // a press: the word lights up
            const float fl = 1 - Clamp01((g_t - g_pressAt) / 0.45f), y = kPItemY + g_pressRow * kPItemGap - 9 + oy, w = TextW(kItem, PowerItem(g_pressRow));
            Light(cx - w * 0.5f - 40, y - 30, cx + w * 0.5f + 40, y + 30, kChosen, 0.50f * fl * fl * p, true);
        }
        for (int i = 0; i < 4; i++) {                           // the words appear in turn (they do not move: they come with the light)
            const float in = p * EaseGlide((age - 0.08f - 0.05f * i) / Pace(0.36f)), k = g_selK[i];
            TextC(kItem, PowerItem(i), cx, kPItemY + i * kPItemGap + oy, Line(k), in * LineA(k));
        }
        const float cB = p * EaseGlide((age - 0.2f) / Pace(0.40f));
        const char *ok = L("Valider", "Confirm"), *back = L("Retour", "Back");
        float hx = cx - (HintW('A', ok) + HintW('B', back) - 30) * 0.5f;
        hx = Hint(hx, kPHintY + oy, 'A', ok, cB); Hint(hx, kPHintY + oy, 'B', back, cB);
    }
    // ---- the HOME panel: a plate on the right that slides in (Yass: « vraiment un volet, pas un plein écran »), the game dimmed ----
    void DrawQuick(float p) {
        const int ox = (int)lroundf((UW - kHX0 + 8) * (1 - p));   // (it slides in from the right - the menu's curve - and back out)
        BigPlate(g_plateHome, ox, 0, p, kHX0, kHY0, kHX1, kHY1, kHR);
        const float age = g_t - g_openAt, lx = kHL + ox, vr = kHRt + ox;
        HomeHeader((float)ox, p);
        // v98: the title is the game being played (its name cut to the panel), its session under it
        const float cT = p * EaseGlide((age - 0.05f) / Pace(0.32f));
        const float tw = TextW(kTitle, g_title, 0.6f);
        Light(lx - 50, kHTitleY - 34, lx + tw + 60, kHTitleY + 14, kTitleGlow, 0.14f * cT, true);
        Text(kTitle, g_title, lx, kHTitleY, kTitleC, cT, 0.6f);
        if (g_session[0]) Text(kSmall, g_session, lx, kHSubY, kWords, 0.78f * cT, 0.5f);
        const int nr = QuickRows();
        const float cR = p * EaseGlide((age - 0.10f) / Pace(0.36f));
        Chosen(kHX0 + 14 + ox, kHX1 - 14 + ox, g_hy, g_hx - g_hw * 0.5f + ox, g_hw, nr > 0 ? cR : 0.0f);
        if (g_notesView) {                                      // the game's notes, in place of the rows (▲▼ to read on, B to go back)
            Text(kItem, NotesLabel(), lx, kHRowY, Line(1), cR * LineA(1));
            for (int j = 0; j < kNoteVis && g_noteTop + j < g_noteN; j++)
                Text(kHint, g_noteLine[g_noteTop + j], lx, kHRowY + 42 + j * 27, kWords, 0.92f * cR, 0.2f);
            if (g_noteTop > 0) Tri(vr - 6, kHRowY - 15, vr - 12, kHRowY - 5, vr, kHRowY - 5, kChoice, cR);
            if (g_noteTop + kNoteVis < g_noteN) { const float y = kHRowY + 42 + (kNoteVis - 1) * 27 + 10; Tri(vr - 6, y + 8, vr - 12, y - 2, vr, y - 2, kChoice, cR); }
        } else {
            if (g_pressRow >= 0 && g_t - g_pressAt < 0.45f) {   // a press: the word lights up
                const float fl = 1 - Clamp01((g_t - g_pressAt) / 0.45f), y = kHRowY + g_pressRow * kHRowGap - 9, w = TextW(kItem, QuickItem(g_pressRow));
                Light(lx - 40, y - 30, lx + w + 40, y + 30, kChosen, 0.50f * fl * fl * p, true);
            }
            if (nr == 0) Text(kHint, L("Rien \xc3\xa0 r\xc3\xa9gler ici", "Nothing to adjust here"), lx, kHRowY, kFaint, 0.85f * cR, 0.4f);
            for (int i = 0; i < nr; i++) {
                const int kind = g_rows[i];
                const float in = p * EaseGlide((age - 0.10f - 0.05f * i) / Pace(0.36f)), k = g_selK[i], y = kHRowY + i * kHRowGap;
                Text(kItem, QuickItem(i), lx, y, Line(k), in * LineA(k));
                char v[64]; v[0] = 0;                           // its value, on the right
                float lit = 1.0f, vx = vr;
                bool arrows = false;
                if (kind == kRowVolume) {                       // the volume: its lights (as the banner), « Muet » when muted
                    arrows = true;
                    if (g_mute) snprintf(v, sizeof v, "%s", L("Muet", "Muted"));
                    else {
                        const int cnt = g_volMax - g_volMin;
                        const float step = cnt > 15 ? 135.0f / cnt : 9.0f, x0 = vr - 3 - (cnt - 1) * step;
                        LightsRow(x0, y - 7, in, step, g_volumeShown, cnt);
                        vx = x0 - 9;
                    }
                } else if (kind == kRowBright) {                // the brightness: its value, « Auto » before it; a fine line under it
                    arrows = true;
                    snprintf(v, sizeof v, g_autoBright ? "Auto  \xc2\xb7  %d %%" : "%d %%", (int)lroundf(g_brightShown * 100));
                    const float by = y + 16, xe = lx + (vr - lx) * g_brightShown;
                    Bar(lx, by, vr, by + 1.4f, { 0.6f, 0.75f, 1.0f, 1 }, 0.16f * in);
                    Bar(lx, by, xe, by + 1.4f, kChoice, 0.85f * in);
                    Light(xe - 16, by - 9, xe + 16, by + 10, kHalo, 0.6f * in, true);
                    Dot(xe, by + 0.7f, 2.2f, kChosen, in);
                } else if (kind == kRowPerf) {                  // sys-clk's mode (the clocks are on the live line)
                    arrows = true;
                    snprintf(v, sizeof v, "%s", PerfName(g_perf));
                } else if (kind == kRowPlane) {
                    snprintf(v, sizeof v, "%s", g_plane ? (g_en ? "On" : "Activ\xc3\xa9") : (g_en ? "Off" : "D\xc3\xa9sactiv\xc3\xa9"));
                    lit = g_knob[1];
                } else if (kind == kRowNotes) {                 // its first line, small, under it
                    Text(kSmall, g_notePeek, lx, y + 25, kFaint, 0.9f * in, 0.4f);
                } else if (kind == kRowMusic) {                 // (FUTURE, hidden: what plays, under it)
                    char m[128];
                    snprintf(m, sizeof m, "%s  \xc2\xb7  %s", g_music.title, g_music.artist);
                    Text(kSmall, m, lx, y + 25, kFaint, 0.9f * in, 0.4f);
                    snprintf(v, sizeof v, "%s", g_music.playing ? L("Lecture", "Playing") : L("Pause", "Paused"));
                    arrows = true;
                }
                if (v[0]) {
                    const float vw = TextW(kValue, v);
                    vx = vr - vw;
                    Text(kValue, v, vx, y, Mix(kFaint, kChosen, 0.35f + 0.65f * lit), in * 0.95f);
                }
                if (arrows && k > 0.01f) {                      // the arrows of an adjustable value
                    Tri(vx - 24, y - 8, vx - 15, y - 14, vx - 15, y - 2, kChoice, k * in);
                    Tri(vr + 22, y - 8, vr + 13, y - 14, vr + 13, y - 2, kChoice, k * in);
                }
            }
        }
        // the live line: clocks, temperatures, the game's memory (made once a second; nothing when nothing answered)
        const float cM = p * EaseGlide((age - 0.16f) / Pace(0.40f));
        if (g_mon1[0]) Text(kSmall, g_mon1, lx, kHMonY, kFaint, 0.85f * cM, 0.4f);
        if (g_mon2[0]) Text(kSmall, g_mon2, lx, g_mon1[0] ? kHMonY + kHMonGap : kHMonY, kFaint, 0.85f * cM, 0.4f);
        const float cB = p * EaseGlide((age - 0.2f) / Pace(0.40f));
        float hx = lx;
        if (g_notesView) {                                      // « ▲▼  Défiler    B  Retour »
            Tri(hx + 5, kHHintY - 13, hx, kHHintY - 3, hx + 10, kHHintY - 3, kChoice, cB);
            Tri(hx + 20, kHHintY - 2, hx + 15, kHHintY - 12, hx + 25, kHHintY - 12, kChoice, cB);
            Text(kHint, L("D\xc3\xa9" "filer", "Scroll"), hx + 35, kHHintY, kWords, 0.9f * cB, 0.5f);
            hx += 35 + TextW(kHint, L("D\xc3\xa9" "filer", "Scroll"), 0.5f) + 30;
        } else if (nr > 0) {                                    // « A  Valider    ◀▶  Régler    B  Retour »
            hx = Hint(hx, kHHintY, 'A', L("Valider", "Confirm"), cB);
            Tri(hx, kHHintY - 7, hx + 8, kHHintY - 12, hx + 8, kHHintY - 2, kChoice, cB);
            Tri(hx + 21, kHHintY - 7, hx + 13, kHHintY - 12, hx + 13, kHHintY - 2, kChoice, cB);
            Text(kHint, L("R\xc3\xa9gler", "Adjust"), hx + 31, kHHintY, kWords, 0.9f * cB, 0.5f);
            hx += 31 + TextW(kHint, L("R\xc3\xa9gler", "Adjust"), 0.5f) + 30;
        }
        Hint(hx, kHHintY, 'B', L("Retour", "Back"), cB);
    }
    // ---- over the game (nothing open): the volume and the notifications, top left, on small plates (the menu's notifications) ----
    constexpr float kVX = 36, kVY = 36, kVW = kSW;
    float ToastY() { return g_vol.on ? kVY + kSH + 14 : kVY; }
    float ToastW() { return fminf(UW - kVX - 24.0f, fmaxf(260.0f, TextW(kHint, g_toast.text, 0.3f) + 90)); }
    Rect VolBox() { return R((int)kVX - kSM, (int)kVY - kSM, (int)(kVX + kVW + kSM + 40), (int)(kVY + kSH + kSMB)); }
    Rect ToastBox() { return R(0, (int)(kVY - kSM), UW, (int)(kVY + 2 * kSH + 14 + kSMB)); }
    void DrawVolume() {
        const float a = VolAlpha(), x0 = kVX - 30 * (1 - EaseGlide((g_t - g_vol.born) / Pace(0.35f))), cy = kVY + kSH * 0.5f;
        if (a <= 0.002f) return;
        SmallPlate(x0, kVY, kVW, a);
        Text(kSmall, g_vol.muted ? L("Muet", "Muted") : "Volume", x0 + 26, cy + 6, kChoice, 0.95f * a, 0.6f);
        VolumeLights(x0 + 124, cy, a);
    }
    void DrawToast() {
        const float a = ToastAlpha(), x0 = kVX - 30 * (1 - EaseGlide((g_t - g_toast.at) / Pace(0.4f))), y0 = ToastY(), cy = y0 + kSH * 0.5f;
        if (a <= 0.002f) return;
        SmallPlate(x0, y0, ToastW(), a);
        Light(x0 + 28 - 22, cy - 22, x0 + 28 + 22, cy + 22, kHalo, 0.55f * a, true);
        Dot(x0 + 28, cy, 3.0f, kChosen, a);
        Text(kHint, g_toast.text, x0 + 52, cy + 6.5f, kWords, 0.95f * a, 0.3f);
    }

    // ======================= what changed this frame (only that is drawn and copied) =======================
    // Each part of a screen has a key made of what it shows; a key that changes = that part is drawn again.
    u32 Fnv(const void *p, size_t n, u32 h = 2166136261u) { const u8 *b = (const u8 *)p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u; return h; }
    inline int Q(float v, float s = 1000.0f) { return (int)lroundf(v * s); }
    u32 g_kRows = 0, g_kHead = 0, g_kRing = 0, g_kVol = 0, g_kToast = 0, g_kTitle = 0, g_kMon = 0, g_kHint = 0;
    int g_uPos = -1, g_uWant = 0;                               // where the UI layer is (0 top left, 1 centre, 2 right)
    bool g_uBlank = true;                                       // what the layer shows is empty (it may move)
    constexpr float kEntrance = 0.6f;                           // the words have all appeared (DrawPower / DrawQuick)
    int g_lastOx = 0;                                           // where the HOME panel was in the last frame drawn
    const Rect kPowerArea = { 0, (int)(kPY0 - 48), UW, (int)(kPY1 + 62 + 16) };
    int g_forceFull = 0;                                        // frames still to draw whole (a screen comes, the layer moved)
    u32 RowsKey() {
        int k[40]; int n = 0;
        k[n++] = Q(g_hy, 4); k[n++] = Q(g_hx, 4); k[n++] = Q(g_hw, 4);
        for (int i = 0; i < kSelN; i++) k[n++] = Q(g_selK[i], 200);
        k[n++] = Q(g_brightShown, 1000); k[n++] = Q(g_knob[0], 200); k[n++] = Q(g_knob[1], 200); k[n++] = g_perf; k[n++] = g_autoBright; k[n++] = g_plane;
        k[n++] = Q(g_volumeShown, 100); k[n++] = g_mute; k[n++] = g_notesView; k[n++] = g_noteTop; k[n++] = g_music.playing;   // v98
        k[n++] = g_pressRow >= 0 && g_t - g_pressAt < 0.46f ? Q(g_t - g_pressAt, 60) : -1; k[n++] = QuickRows(); k[n++] = g_en;
        return Fnv(k, sizeof(int) * n);
    }
    u32 HeadKey() {
        char d[24], t[48];
        ClockText(d, sizeof d, t, sizeof t);
        u32 h = Fnv(d, strlen(d)); h = Fnv(t, strlen(t), h);
        const int k[6] = { g_vol.on ? Q(VolAlpha(), 100) : -1, g_vol.on ? Q(g_vol.shown, 50) : -1, g_toast.on ? Q(ToastAlpha(), 100) : -1, g_vol.muted, g_battPct, g_battChg };
        h = Fnv(k, sizeof k, h);
        for (int i = 0; i < g_nPads; i++) { const int pk[5] = { g_pads[i].n, g_pads[i].lv[0], g_pads[i].lv[1], g_pads[i].chg[0], g_pads[i].chg[1] }; h = Fnv(pk, sizeof pk, h); }
        return Fnv(&g_nPads, sizeof g_nPads, h);
    }
    // v98: the title and its session, the live line, the hints (each drawn again only when what it says changes)
    u32 TitleKey() { u32 h = Fnv(g_title, strlen(g_title)); return Fnv(g_session, strlen(g_session), h); }
    u32 MonKey() { u32 h = Fnv(g_mon1, strlen(g_mon1)); return Fnv(g_mon2, strlen(g_mon2), h); }
    u32 HintKey() { const int k[3] = { g_notesView, QuickRows() > 0, g_en }; return Fnv(k, sizeof k); }

    // ======================= the display layers =======================
#ifndef PREVIEW
    ViDisplay g_disp;
    // v97 (journal v96: every frame ~18 ms, just over a refresh: a skipped one now and then) - the frames are paced by the
    // screen's own refresh (vi's vsync event) instead of a timer: drawn right after it, shown at the next one
    Event g_vsync;
    ViLayer g_layU, g_layF;
    NWindow g_winU, g_winF;
#endif
#ifndef PREVIEW
    Framebuffer g_fbU, g_fbF;
#endif
    bool g_fbOk = false, g_fbFOk = false;
    // v92 (journal on the console, FW 22.5: « calque ui : ECHEC (etape 7, 0x872) », « position refusee 0x872 ») - this console
    // refuses a UI layer smaller than the screen, and refuses to move it. Then the UI layer is the whole screen (as the old
    // overlay that showed), its picture 1280 x 720 (same sharpness), and the panel is copied at its place inside it.
    [[maybe_unused]] bool g_full = false;
    [[maybe_unused]] int UiX() { return g_uPos == 2 ? 1280 - UW : (g_uPos == 1 ? 640 - UW / 2 : 0); }
    u32 g_frameNo = 0;
    [[maybe_unused]] u32 g_slotFrame[3] = { 0, 0, 0 };
    Rect g_hist[8]; u32 g_histNo[8] = {};                       // what changed, per frame (a slot shown 2 frames ago needs both)
    void SetUPos(int pos) {                                     // the UI layer: top left / centre / right (layer pixels: 1920 x 1080)
        g_uPos = pos;
#ifndef PREVIEW
        if (g_full) {                                           // (nothing to move: both buffers emptied, the panel is drawn at its new place)
            if (g_fbOk) { memset(g_fbU.buf, 0, (size_t)g_fbU.fb_size * g_fbU.num_fbs); armDCacheFlush(g_fbU.buf, (size_t)g_fbU.fb_size * g_fbU.num_fbs); }
            return;
        }
        const float x = pos == 2 ? (1280 - UW) * 1.5f : (pos == 1 ? (640 - UW / 2) * 1.5f : 0.0f);
        const Result rc = viSetLayerPosition(&g_layU, x, 0.0f);
        if (R_FAILED(rc)) Log("calque : position refusee 0x%x", rc);
#endif
    }
    // per-frame timing (the journal gets a summary when the screen closes: never a write while it is shown)
    constexpr int kStatN = 120;
    float g_stDraw[kStatN], g_stCopy[kStatN];
    int g_stN = 0, g_stFrames = 0, g_stArea = 0;
    double g_stSumDraw = 0, g_stSumCopy = 0, g_stMaxDraw = 0, g_stSumFog = 0, g_stSumWait = 0;
    double g_frameWait = 0;                                     // (this frame: the wait for a free display buffer, not counted as work)
    float g_stFirst = -1, g_stLast = -1;
    inline double Ms(u64 a, u64 b) { return (double)(b - a) * 1000.0 / (double)armGetSystemTickFreq(); }

    // what this buffer needs: everything that changed since it was last filled (3 buffers: what the 2 frames before it changed
    // too); a buffer never filled, or filled too long ago, gets everything. (Checked by the PC simulator with random buffer orders.)
#ifdef BRUME_SIM
    int g_simSlotErrors = 0;
#endif
    [[maybe_unused]] Rect SlotRect(s32 slot, Rect dirty) {
        Rect r = dirty;
        const u32 last = slot >= 0 && slot < 3 ? g_slotFrame[slot] : 0;
        if (!last || g_frameNo - last > 7) r = kAll;
        else for (u32 f = last + 1; f < g_frameNo; f++) { if (g_histNo[f & 7] == f) r = Union(r, g_hist[f & 7]); else { r = kAll; break; } }
        if (slot >= 0 && slot < 3) g_slotFrame[slot] = g_frameNo;
        return Inter(r, kAll);
    }
#ifndef PREVIEW
    // libnx's framebufferBegin / End, without their abort: a buffer the display refuses only skips a frame (never a crash)
    int g_dqFails = 0;
    s32 Dequeue(Framebuffer &fb, NWindow &win, u8 *&buf) {
        s32 slot = -1;
        const u64 w0 = armGetSystemTick();
        const Result rc = nwindowDequeueBuffer(&win, &slot, nullptr);
        g_frameWait += Ms(w0, armGetSystemTick());
        if (R_FAILED(rc) || slot < 0 || (u32)slot >= fb.num_fbs) {
            if (g_dqFails++ < 3) Log("ecran : tampon refuse 0x%x (image sautee)", rc);
            return -1;
        }
        buf = (u8 *)fb.buf + (size_t)slot * fb.fb_size;
        return slot;
    }
    void Queue(Framebuffer &fb, NWindow &win, u8 *buf, s32 slot) {
        armDCacheFlush(buf, fb.fb_size);
        const Result rc = nwindowQueueBuffer(&win, slot, nullptr);
        if (R_FAILED(rc) && g_dqFails++ < 3) Log("ecran : affichage refuse 0x%x", rc);
    }
#endif
    void PresentU(Rect dirty) {                                 // copy what changed (since that buffer was last filled) and show it
        g_frameNo++;
        g_hist[g_frameNo & 7] = dirty; g_histNo[g_frameNo & 7] = g_frameNo;
#ifndef PREVIEW
        if (!g_fbOk) return;
        u8 *buf = nullptr;
        const s32 slot = Dequeue(g_fbU, g_winU, buf);
        if (slot < 0) return;                                  // (refused: this frame is skipped, the next one catches up)
        const Rect r = SlotRect(slot, dirty);
        if (!r.Empty()) SwizzleRect(buf, g_fbU.stride, g_canvas, UW, r, g_full ? UiX() : 0);
        Queue(g_fbU, g_winU, buf, slot);
        g_stArea += (r.x1 - r.x0) * (r.y1 - r.y0);
#elif defined(BRUME_SIM)
        // (the PC: three stand-in buffers handed out in a random order, as the display might; each one, once filled, must be
        // exactly the picture - SlotRect checked; and the same block-linear copy, to time it)
        static u8 fake[UW * 4 * 768];
        static u32 slots[3][UW * UH];
        static u32 rng = 12345;
        rng = rng * 1103515245u + 12345u;
        const s32 slot = (s32)((rng >> 16) % 3);
        const Rect r = SlotRect(slot, dirty);
        if (!r.Empty()) SwizzleRect(fake, UW * 4, g_canvas, UW, r);
        for (int y = r.y0; y < r.y1; y++) memcpy(slots[slot] + y * UW + r.x0, g_canvas + y * UW + r.x0, (size_t)(r.x1 - r.x0) * 4);
        if (memcmp(slots[slot], g_canvas, sizeof g_canvas) != 0) g_simSlotErrors++;
        g_stArea += (r.x1 - r.x0) * (r.y1 - r.y0);
#endif
    }
    void PresentF() {
#ifndef PREVIEW
        if (!g_fbFOk) return;
        u8 *buf = nullptr;
        const s32 slot = Dequeue(g_fbF, g_winF, buf);
        if (slot < 0) return;
        SwizzleRect(buf, g_fbF.stride, g_fogOut, FW, R(0, 0, FW, FH));
        Queue(g_fbF, g_winF, buf, slot);
#elif defined(BRUME_SIM)
        static u8 fake[FW * 4 * 256];
        SwizzleRect(fake, FW * 4, g_fogOut, FW, R(0, 0, FW, FH));
#endif
    }
#ifdef PREVIEW
    // the preview app (and the PC simulator): no layers - both pictures are put together over a backdrop, as the display would
    u32 *g_pvBack = nullptr;                                    // a stand-in for the game (1280 x 720, opaque), or nothing: dark blue
    void PreviewCompose(u32 *dst, u32 dstride) {
        for (int y = 0; y < 720; y++) {
            u32 *o = dst + (size_t)y * dstride;
            for (int x = 0; x < 1280; x++) o[x] = g_pvBack ? g_pvBack[y * 1280 + x] : Pack4(20 + y / 12, 28 + y / 14, 52 + y / 10, 255);
            if (g_fogOk && g_shown != kIdle) {                  // the fog layer, enlarged 4 times (bilinear, as the display)
                const float fy = (y + 0.5f) / 4 - 0.5f; const int j0 = (int)fmaxf(0, floorf(fy)), j1 = j0 + 1 < FH ? j0 + 1 : FH - 1; const u32 wy = (u32)((fy - floorf(fy)) * 256) & 255;
                for (int x = 0; x < 1280; x++) {
                    const float fx = (x + 0.5f) / 4 - 0.5f; const int i0 = (int)fmaxf(0, floorf(fx)), i1 = i0 + 1 < FW ? i0 + 1 : FW - 1; const u32 wx = (u32)((fx - floorf(fx)) * 256) & 255;
                    const u32 *r0 = g_fogOut + j0 * FW, *r1 = g_fogOut + j1 * FW;
                    const u32 a = Avg(r0[i0], r0[i0]), b = r0[i1], c = r1[i0], d = r1[i1];
                    const u32 top = Scale(a, 256 - wx) + Scale(b, wx), bot = Scale(c, 256 - wx) + Scale(d, wx);
                    o[x] = Over(o[x], Scale(top, 256 - wy) + Scale(bot, wy));
                }
            }
            const int ux = g_uPos == 2 ? 1280 - UW : (g_uPos == 1 ? 640 - UW / 2 : 0);
            const u32 *u = g_canvas + y * UW;
            for (int x = 0; x < UW; x++) if (u[x]) o[ux + x] = Over(o[ux + x], u[x]);
            for (int x = 0; x < 1280; x++) o[x] |= 0xFF000000u;
        }
    }
#endif

    // ======================= one frame =======================
    // Decides what changed, draws only that into the UI picture, copies it to the layer; steps the fog. Returns true if drawn.
    bool g_fogShown = false;
    bool Render() {
        const u64 t0 = armGetSystemTick();
        const float p = Presence();
        const Mode shown = g_shown;
        // where the UI layer must be; it moves only while it shows nothing (one empty frame first if needed)
        g_uWant = shown == kQuick ? 2 : (shown == kPower ? 1 : 0);
        Rect dirty;
        bool drewU = false;
        if (g_uWant != g_uPos) {
            if (!g_uBlank) { ClearRect(kAll); dirty = kAll; g_uBlank = true; drewU = true; }
            else { SetUPos(g_uWant); g_forceFull = 3; }
        }
        if (!drewU) {
            // the glides of the chosen line (the menu's spring), the knobs
            if (shown != kIdle) {
                const int n = shown == kQuick ? QuickRows() : 4;
                for (int i = 0; i < kSelN; i++) Approach(g_selK[i], i == g_sel && i < n ? 1.0f : 0.0f, g_dt, 12);
                float ty, tx, tw;
                if (shown == kQuick) {                          // (v98: in the notes, the light rests on their title)
                    ty = kHRowY + (g_notesView ? 0 : g_sel) * kHRowGap - 9; tw = TextW(kItem, g_notesView ? NotesLabel() : QuickItem(g_sel)); tx = kHL + tw * 0.5f;
                }
                else { ty = kPItemY + g_sel * kPItemGap - 9; tw = TextW(kItem, PowerItem(g_sel)); tx = (kPX0 + kPX1) * 0.5f; }
                if (g_hSnap) { g_hy = ty; g_hx = tx; g_hw = tw; g_hyv = g_hxv = g_hwv = 0; g_hSnap = false; }
                Glide(g_hy, g_hyv, ty, g_dt, 13.0f); Glide(g_hx, g_hxv, tx, g_dt, 13.0f); Glide(g_hw, g_hwv, tw, g_dt, 13.0f);
                if (shown == kQuick) {
                    Approach(g_brightShown, g_bright, g_dt, 14);
                    Approach(g_knob[0], g_autoBright ? 1.0f : 0.0f, g_dt, 16);
                    Approach(g_knob[1], g_plane ? 1.0f : 0.0f, g_dt, 16);
                    Approach(g_volumeShown, g_mute ? 0.0f : (float)(g_volume - g_volMin), g_dt, 14);
                    if (g_t - g_clkAt > 1.0f) { QuickTick(); g_clkAt = g_t; }   // (v98: the clocks, the live line, the pads, once a second)
                }
            }
            if (g_vol.on) {
                Approach(g_vol.shown, g_vol.muted ? 0.0f : (float)g_vol.step, g_dt, 18);
                const float tgt = g_t - g_vol.at < 1.6f ? 1.0f : 0.0f;
                g_vol.fade = tgt > g_vol.fade ? fminf(tgt, g_vol.fade + g_dt / 0.2f) : fmaxf(tgt, g_vol.fade - g_dt / 0.3f);
                if (g_vol.fade <= 0 && g_t - g_vol.at > 1) g_vol.on = false;
            }
            if (g_toast.on && ToastAlpha() <= 0 && g_t - g_toast.at > 1) g_toast.on = false;
            // what changed: the whole plate while it comes or goes and its words appear (0.6 s), then only the parts that move
            const bool full = g_forceFull > 0 || (shown != kIdle && (p < 0.999f || g_t - g_openAt < kEntrance));
            if (g_forceFull > 0) g_forceFull--;
            Rect whole = shown == kPower && g_forceFull <= 0 ? kPowerArea : kAll;   // (the power plate, its shadow and halo)
            if (shown == kQuick) {                              // (the panel sliding: only from its left edge, now or last frame)
                const int ox = (int)lroundf((UW - kHX0 + 8) * (1 - p)), from = (ox < g_lastOx ? ox : g_lastOx) + (int)kHX0 - 56;
                if (g_forceFull <= 0 && from > 0) whole = R(from, 0, UW, UH);
                g_lastOx = ox;
            }
            if (shown != kIdle) {
                const u32 kr = RowsKey(), kh = shown == kQuick ? HeadKey() : 0, kg = (u32)Q(g_at, 240);
                const u32 kt = shown == kQuick ? TitleKey() : 0, km = shown == kQuick ? MonKey() : 0, ki = shown == kQuick ? HintKey() : 0;
                if (full) dirty = whole;
                else {
                    if (kr != g_kRows) dirty = Union(dirty, shown == kQuick ? kHRowsBox : kPRowsBox);
                    if (shown == kQuick && kh != g_kHead) dirty = Union(dirty, kHHeadBox);
                    if (shown == kQuick && kt != g_kTitle) dirty = Union(dirty, kHTitleBox);
                    if (shown == kQuick && km != g_kMon) dirty = Union(dirty, kHMonBox);
                    if (shown == kQuick && ki != g_kHint) dirty = Union(dirty, kHHintBox);
                    if (shown == kPower && kg != g_kRing) dirty = Union(dirty, kRingBox);
                }
                g_kRows = kr; g_kHead = kh; g_kRing = kg; g_kTitle = kt; g_kMon = km; g_kHint = ki;
            } else if (full) dirty = kAll;
            else {                                              // over the game: only when the volume / the notification change
                const u32 kv = g_vol.on ? Fnv(&g_vol.step, sizeof g_vol.step, (u32)(Q(VolAlpha(), 200) * 977 + Q(g_vol.shown, 50) * 31 + g_vol.muted)) : 0;
                const u32 kt = g_toast.on ? Fnv(g_toast.text, strlen(g_toast.text), (u32)(Q(ToastAlpha(), 200) * 977 + Q(ToastY(), 1))) : 0;
                if (kv != g_kVol) dirty = Union(dirty, VolBox());
                if (kt != g_kToast) dirty = Union(dirty, ToastBox());
                g_kVol = kv; g_kToast = kt;
            }
            dirty = Inter(dirty, kAll);
            if (!dirty.Empty()) {
                g_clip = dirty;
                if (shown == kIdle) ClearRect(dirty);           // (a screen's plate writes every pixel itself)
                if (shown == kQuick) DrawQuick(p);
                else if (shown == kPower) DrawPower(p);
                else { if (g_vol.on) DrawVolume(); if (g_toast.on) DrawToast(); }
                g_clip = kAll;
                drewU = true;
                g_uBlank = shown == kIdle && !g_vol.on && !g_toast.on;
            }
        }
        const u64 t1 = armGetSystemTick();
        // the fog layer: alive while a screen is shown (it slows to a stop with the atmosphere's clock); one empty frame at the end
        bool drewF = false;
        if (g_fogOk && g_fbFOk) {
            static u32 tick = 0;
            tick++;
            if (shown != kIdle) {                               // (once it is there, 30 times a second: it drifts so slowly)
                const bool due = p < 0.999f || !g_fogShown || (g_speed > 0 && (tick & 1));
                if (due) { if (g_speed > 0) FogStep(); FogCompose(p); drewF = true; g_fogShown = true; }
            } else if (g_fogShown) { memset(g_fogOut, 0, (size_t)FW * FH * 4); drewF = true; g_fogShown = false; }
        }
#ifdef PREVIEW
        if (!g_fbFOk) {                                         // (the preview app: the fog is put together with the rest)
            if (g_fogOk && shown != kIdle && g_speed > 0) { FogStep(); FogCompose(p); }
            else if (g_fogOk && shown != kIdle && p < 0.999f) FogCompose(p);
        }
#endif
        const u64 t2 = armGetSystemTick();
        g_frameWait = 0;
        if (drewU) PresentU(dirty);
        if (drewF) PresentF();
        const u64 t3 = armGetSystemTick();
        if ((drewU || drewF) && shown != kIdle) {               // the timing of an open screen
            const float dr = (float)Ms(t0, t1), fo = (float)Ms(t1, t2), cp = (float)(Ms(t2, t3) - g_frameWait);
            if (g_stN < kStatN) { g_stDraw[g_stN] = dr + fo; g_stCopy[g_stN] = cp; g_stN++; }
            g_stFrames++; g_stSumDraw += dr + fo; g_stSumFog += fo; g_stSumCopy += cp; g_stSumWait += g_frameWait;
            if (dr + fo > g_stMaxDraw) g_stMaxDraw = dr + fo;
            if (g_stFirst < 0) g_stFirst = g_t;
            g_stLast = g_t;
        }
        return drewU || drewF;
    }
    void LogStats(const char *what) {                           // when a screen has closed: one summary, the first frames' times
        if (!g_stFrames) return;
        const float span = g_stLast - g_stFirst;
        Log("%s : %d images en %.2f s, dessin %.2f ms en moyenne (brume %.2f, max %.2f), copie vers l'ecran %.2f ms, attente d'un tampon libre %.2f ms (hors travail), %.0f px copies par image",
            what, g_stFrames, span, g_stSumDraw / g_stFrames, g_stSumFog / g_stFrames, g_stMaxDraw,
            g_stSumCopy / g_stFrames, g_stSumWait / g_stFrames, (double)g_stArea / g_stFrames);
        char line[900]; size_t n = 0;
        n += snprintf(line + n, sizeof line - n, "  dessin+copie par image (ms) :");
        for (int i = 0; i < g_stN && n < sizeof line - 12; i++) n += snprintf(line + n, sizeof line - n, " %.1f", g_stDraw[i] + g_stCopy[i]);
        Log("%s", line);
        if (g_gapN) Log("  entre deux images : %.1f ms en moyenne, max %.1f, %d ratees sur %d (vsync %s)", g_gapSum / g_gapN, g_gapMax, g_gapLate, g_gapN, g_vsyncOk ? "oui" : "non");
        g_gapN = g_gapLate = 0; g_gapSum = g_gapMax = 0; g_prevFrame = -1;
        g_stN = g_stFrames = g_stArea = 0; g_stSumDraw = g_stSumCopy = g_stMaxDraw = g_stSumFog = g_stSumWait = 0; g_stFirst = g_stLast = -1;
    }

    // nothing moves on screen: no need to wake up each frame (only the clock, once a second)
    bool Animating() { return g_shown != kIdle || g_vol.on || g_toast.on || !g_uBlank || g_fogShown; }
    bool Settled() {
        if (g_vol.on || g_toast.on) return false;
        if (g_mode == kIdle) return g_shown == kIdle && g_uBlank && !g_fogShown;
        if (g_speed > 0) return false;                          // (alive)
        if (g_t - g_openAt < 1.0f) return false;
        if (g_pressRow >= 0 && g_t - g_pressAt < 0.5f) return false;
        for (int i = 0; i < kSelN; i++) if (fabsf(g_selK[i] - (i == g_sel ? 1.0f : 0.0f)) > 0.002f) return false;
        if (fabsf(g_hyv) > 0.05f || fabsf(g_hwv) > 0.05f || fabsf(g_hxv) > 0.05f) return false;
        if (g_mode == kQuick && (fabsf(g_brightShown - g_bright) > 0.001f || fabsf(g_knob[0] - (g_autoBright ? 1.0f : 0.0f)) > 0.002f ||
                                 fabsf(g_knob[1] - (g_plane ? 1.0f : 0.0f)) > 0.002f ||
                                 fabsf(g_volumeShown - (g_mute ? 0.0f : (float)(g_volume - g_volMin))) > 0.002f)) return false;
        return true;
    }

    // ======================= the input of the open screens =======================
#ifdef PREVIEW
    u64 g_pvDown = 0;                                          // the preview's loop reads the pad once per frame (see main)
#endif
    void Input() {
#ifdef PREVIEW
        const u64 d = g_pvDown;                                 // (a second padUpdate here ate every press: B never closed the menu)
#else
        padUpdate(&g_pad);
        const u64 d = padGetButtonsDown(&g_pad);
#endif
        if (g_mode == kIdle || g_t - g_openAt < 0.25f) return;  // (the press that opened it does not act)
        if (d) g_lastInput = g_t;                               // (a touch: the atmosphere lives again)
        if (g_mode == kPower && (d & HidNpadButton_A)) {        // FIRST, and with nothing before it: the power action
            const int s = g_sel;
            if (s < 3) PowerAction(s);
            Close(s < 3);                                       // (the confirmation's own sound, not the closing one)
            if (s < 3) Play(kSndConfirm);
            return;
        }
        if (g_mode == kQuick && g_notesView) {                  // v98: reading the game's notes (only the HOME panel gets here)
            if (d & HidNpadButton_B) { g_notesView = false; Play(kSndMove); return; }
            if ((d & HidNpadButton_AnyUp) && g_noteTop > 0) { g_noteTop--; Play(kSndAdjust); }
            if ((d & HidNpadButton_AnyDown) && g_noteTop + kNoteVis < g_noteN) { g_noteTop++; Play(kSndAdjust); }
            return;
        }
        const int n = g_mode == kQuick ? QuickRows() : 4;
        if (n > 0 && (d & HidNpadButton_AnyUp)) { g_sel = (g_sel + n - 1) % n; Play(kSndMove); }
        if (n > 0 && (d & HidNpadButton_AnyDown)) { g_sel = (g_sel + 1) % n; Play(kSndMove); }
        if (d & HidNpadButton_B) { Close(); return; }
        if (g_mode == kPower || n <= 0 || g_sel >= n) return;
        // v98: the HOME panel's rows (◀▶ adjust, A toggles / opens)
        const int dir = (d & HidNpadButton_AnyRight) ? 1 : ((d & HidNpadButton_AnyLeft) ? -1 : 0);
        const bool a = d & HidNpadButton_A;
        auto Pressed = [] { Play(kSndConfirm, 0.55f); g_pressRow = g_sel; g_pressAt = g_t; };
        switch (g_rows[g_sel]) {
            case kRowVolume:                                    // a step (and the mute off) / A: mute
                if (dir) { AudcSet(g_volume + dir); Play(kSndAdjust); }
                else if (a) { AudcMute(); Pressed(); }
                break;
            case kRowBright:                                    // a tenth / A: automatic brightness (the old « Luminosité auto. » row)
                if (dir) {
                    g_bright = fmaxf(0.0f, fminf(1.0f, g_bright + dir * 0.1f));
                    Play(kSndAdjust);
                    if (R_SUCCEEDED(lblSetCurrentBrightnessSetting(g_bright))) lblApplyCurrentBrightnessSettingToBacklight();
                } else if (a) {
                    g_autoBright = !g_autoBright;
                    if (R_SUCCEEDED(g_autoBright ? lblEnableAutoBrightnessControl() : lblDisableAutoBrightnessControl())) lblSaveCurrentSetting();
                    Pressed();
                }
                break;
            case kRowPerf:                                      // v74.4: sys-clk
                if (dir || a) {
                    const int cur = g_perf > 2 ? 0 : g_perf;
                    g_perf = dir < 0 ? (cur + 2) % 3 : (cur + 1) % 3;
                    ClkSet(g_perf); ClkRead(); g_clkAt = g_t - 2;   // (the live line follows at once)
                    Play(kSndAdjust);
                }
                break;
            case kRowPlane:                                     // airplane mode: Wi-Fi and NFC together, or nothing changes
                if (a) {
                    Pressed();
                    const bool on = !g_plane;
                    bool w0 = true; nifmIsWirelessCommunicationEnabled(&w0);
                    if (R_SUCCEEDED(nifmSetWirelessCommunicationEnabled(!on))) {
                        if (R_SUCCEEDED(setsysSetNfcEnableFlag(!on))) g_plane = on;
                        else nifmSetWirelessCommunicationEnabled(w0);
                    }
                }
                break;
            case kRowNotes:
                if (a) { g_notesView = true; g_noteTop = 0; Play(kSndConfirm, 0.55f); }
                break;
            case kRowMusic:                                     // (FUTURE, hidden)
                if (a) { MusicCommand(0); Pressed(); }
                else if (dir) { MusicCommand(dir > 0 ? 1 : 2); Play(kSndAdjust); }
                break;
            default: break;
        }
    }

    // ======================= the system's notifications (ovln: volume, battery, screenshot, video) =======================
#ifndef PREVIEW
    Service g_ovln, g_rcv;
    Event g_ovlnEvent;
    bool g_ovlnOk = false;
#endif
    void OvlnInit() {
#ifndef PREVIEW
        if (R_FAILED(smGetService(&g_ovln, "ovln:rcv"))) { Log("ovln : service indisponible"); return; }
        if (R_FAILED(serviceDispatch(&g_ovln, 0, .out_num_objects = 1, .out_objects = &g_rcv))) { serviceClose(&g_ovln); Log("ovln : OpenReceiver refuse"); return; }
        struct { char name[0x10]; } src = {};
        strcpy(src.name, "overlay");
        if (R_FAILED(serviceDispatchIn(&g_rcv, 0, src))) Log("ovln : AddSource refuse");
        Handle h = INVALID_HANDLE;
        Result rc = serviceDispatch(&g_rcv, 2, .out_handle_attrs = { SfOutHandleAttr_HipcCopy }, .out_handles = &h);
        if (R_FAILED(rc)) rc = serviceDispatch(&g_rcv, 2, .in_send_pid = true, .out_handle_attrs = { SfOutHandleAttr_HipcCopy }, .out_handles = &h);
        if (R_FAILED(rc)) { Log("ovln : evenement refuse 0x%x", rc); return; }
        eventLoadRemote(&g_ovlnEvent, h, true);
        g_ovlnOk = true;
#endif
    }
    void OnVolume(u16 v) { g_vol.step = v < 256 ? (int)(v > 15 ? 15 : v) : 0; g_vol.muted = v >= 256 || v == 0; g_vol.at = g_t; if (!g_vol.on) { g_vol.shown = (float)g_vol.step; g_vol.born = g_t; g_vol.fade = 1; } g_vol.on = true; if (g_mode == kQuick && g_audc) AudcRead(); }   // (v98: the panel's volume row follows the buttons)
#ifndef PREVIEW
    void OvlnReceive() {
#ifndef PREVIEW
        struct { u32 tag, size; u8 data[0x78]; } m;
        for (int k = 0; k < 8; k++) {                          // everything waiting
            memset(&m, 0, sizeof m);
            if (R_FAILED(serviceDispatchOut(&g_rcv, 3, m))) return;
            u16 v; memcpy(&v, m.data, 2);
            if (m.tag != 1000 && m.tag != 1004) Log("ovln : tag %u taille %u valeur %u", m.tag, m.size, v);   // (the volume: not logged each time)
            switch (m.tag) {
                case 1000: case 1004: OnVolume(v); break;   // (1004 on FW 22.5: the journal on the console)
                case 3002: ShowToast(L("Batterie faible", "Battery low")); break;
                case 8004: ShowToast(L("Capture d'\xc3\xa9" "cran enregistr\xc3\xa9" "e", "Screenshot saved")); break;
                case 8003: ShowToast(L("La capture n'a pas pu \xc3\xaatre enregistr\xc3\xa9" "e", "The screenshot could not be saved")); break;
                case 8100: ShowToast(L("Vid\xc3\xa9o enregistr\xc3\xa9" "e", "Video saved")); break;
                case 8102: ShowToast(L("La vid\xc3\xa9o n'a pas pu \xc3\xaatre enregistr\xc3\xa9" "e", "The video could not be saved")); break;
                default: break;                                 // (logged: the journal will tell what else comes)
            }
            if (m.tag == 0 && m.size == 0) return;
        }
#endif
    }
#endif

#ifndef PREVIEW
    // v74.2 (journal on the console: « ecran : ECHEC ») - an overlay applet has no ready-made window (libnx's default one
    // is an application's): it makes its own layers, the highest ones, in 1920x1080 screen space, shown on the screen only
    // (not in the screenshot stack). AddToLayerStack is vi's manager command 6000 (switchbrew « Display services »).
    bool MakeLayer(const char *name, ViLayer &lay, NWindow &win, s32 z, float w, float h, float x, float y) {
        const u64 aruid = appletGetAppletResourceUserId();     // (0 if the applet has none)
        auto stack = [](u64 id, u32 st) { const struct { u32 st; u32 pad; u64 id; } in = { st, 0, id }; return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in); };
        int step = 3;
        // v95 - THE bug behind « size refused 0x872 » and « every buffer refused »: libnx's viCreateLayer ignores the id we
        // pass (it clears the ViLayer and takes the global __nx_vi_layer_id; 0 -> it asks am for the applet's OWN managed
        // layer, the one am keeps hidden and sized itself). libtesla / Ultrahand create theirs INTO __nx_vi_layer_id: same
        // here. Owner 0 first (ours alone), our applet's id if vi refuses to create or open it that way
        auto open = [&](u64 owner) -> Result {
            u64 id = 0;
            Result rc = viCreateManagedLayer(&g_disp, (ViLayerFlags)0, owner, &id);
            if (R_FAILED(rc)) { Log("calque %s : creation refusee 0x%x (%s)", name, rc, owner ? "notre aruid" : "aruid 0"); return rc; }
            __nx_vi_layer_id = id;
            rc = viCreateLayer(&g_disp, &lay);
            __nx_vi_layer_id = 0;
            Log("calque %s : id %llu (%s), ouverture 0x%x%s", name, (unsigned long long)id, owner ? "notre aruid" : "aruid 0", rc,
                R_SUCCEEDED(rc) && lay.layer_id != id ? " - AUTRE id !" : "");
            if (R_FAILED(rc)) { ViLayer tmp{}; tmp.layer_id = id; viDestroyManagedLayer(&tmp); }
            return rc;
        };
        Result r = open(0);
        if (R_FAILED(r) && aruid) r = open(aruid);
        if (R_SUCCEEDED(r)) step = 4;
        if (R_SUCCEEDED(r)) { step = 5; r = viSetLayerScalingMode(&lay, ViScalingMode_FitToLayer); }
        if (R_SUCCEEDED(r)) viSetLayerZ(&lay, z);
        if (R_SUCCEEDED(r)) { step = 6; r = stack(lay.layer_id, ViLayerStack_Default); }
        if (R_SUCCEEDED(r)) { stack(lay.layer_id, ViLayerStack_Lcd); step = 7; r = viSetLayerSize(&lay, (s32)w, (s32)h); }
        if (R_SUCCEEDED(r)) { step = 8; r = viSetLayerPosition(&lay, x, y); }
        if (R_SUCCEEDED(r)) { step = 9; r = nwindowCreateFromLayer(&win, &lay); }
        Log("calque %s : %s (etape %d, 0x%x, z %d)", name, R_SUCCEEDED(r) ? "ok" : "ECHEC", step, r, z);
        if (R_FAILED(r) && step >= 4) {                         // (v92: a layer half made is given back, so another can be tried)
            const u64 id = lay.layer_id;
            viCloseLayer(&lay);
            ViLayer tmp{}; tmp.layer_id = id; viDestroyManagedLayer(&tmp);
        }
        return R_SUCCEEDED(r);
    }
#endif

}

#ifndef BRUME_SIM
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    SafetyAtStart();
    Log("Brume Overlay v101 (le menu Power ne depend plus du menu HOME) demarre");
    ReadBrume();
    GlyphsInit();
#ifdef PREVIEW
    hidInitialize();
#endif
    g_lbl = R_SUCCEEDED(lblInitialize());
    g_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_System));
    g_setsys = R_SUCCEEDED(setsysInitialize());
    g_psm = R_SUCCEEDED(psmInitialize());
    g_time = R_SUCCEEDED(timeInitialize());
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&g_pad);
#ifdef PREVIEW
    Framebuffer pvFb;
    NWindow *pvWin = nwindowGetDefault();
    g_fbOk = pvWin && R_SUCCEEDED(framebufferCreate(&pvFb, pvWin, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2));
    if (g_fbOk) framebufferMakeLinear(&pvFb);
#else
    // the UI layer FIRST (the power menu needs only it): the highest; then the fog layer just under it (optional)
    {
        bool okDisp = R_SUCCEEDED(viInitialize(ViServiceType_Manager)) && R_SUCCEEDED(viOpenDefaultDisplay(&g_disp));
        s32 zmax = 0, zmin = 0;
        if (okDisp && (R_FAILED(viGetZOrderCountMax(&g_disp, &zmax)) || zmax <= 0)) zmax = 0;
        if (okDisp) g_vsyncOk = R_SUCCEEDED(viGetDisplayVsyncEvent(&g_disp, &g_vsync));
        if (okDisp) viGetZOrderCountMin(&g_disp, &zmin);
        if (okDisp && MakeLayer("ui", g_layU, g_winU, zmax, UW * 1.5f, UH * 1.5f, 0, 0)) {
            g_fbOk = R_SUCCEEDED(framebufferCreate(&g_fbU, &g_winU, UW, UH, PIXEL_FORMAT_RGBA_8888, 3));
            if (!g_fbOk) g_fbOk = R_SUCCEEDED(framebufferCreate(&g_fbU, &g_winU, UW, UH, PIXEL_FORMAT_RGBA_8888, 2));
            g_uPos = 0;
            // v98.1 (journal v97: 3 buffers yet ~15 ms waiting for one each frame, 26-35 ms between frames) - swap interval 0:
            // the screen takes the newest frame, we never wait for it to show the previous ones
            if (g_fbOk) Log("intervalle 0 : 0x%x", nwindowSetSwapInterval(&g_winU, 0));
            Log("tampons ui : %u", g_fbOk ? (unsigned)g_fbU.num_fbs : 0u);
        }
        if (okDisp && !g_fbOk && MakeLayer("ui plein ecran", g_layU, g_winU, zmax, 1920, 1080, 0, 0)) {
            const Result fr = framebufferCreate(&g_fbU, &g_winU, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2);
            g_fbOk = R_SUCCEEDED(fr);
            if (!g_fbOk) Log("tampons plein ecran : 0x%x", fr);
            g_full = g_fbOk; g_uPos = 0;
            if (g_fbOk) { memset(g_fbU.buf, 0, (size_t)g_fbU.fb_size * g_fbU.num_fbs); armDCacheFlush(g_fbU.buf, (size_t)g_fbU.fb_size * g_fbU.num_fbs); }
            Log("calque ui plein ecran : %s", g_fbOk ? "ok (1280x720, 2 tampons)" : "ECHEC (memoire ?)");
        }
        // v96 (journal: every frame that also showed the fog waited one more screen refresh: 36 ms instead of 18, the menus
        // at 25-30 images/s) - the full-screen fog layer is off: the plates alone, at 60 images/s
        constexpr bool kFogLayer = false;
        if (g_fbOk && kFogLayer) {
            FogInit();
            if (g_fogOk && MakeLayer("brume", g_layF, g_winF, zmax > zmin ? zmax - 1 : zmax, 1920, 1080, 0, 0))
                g_fbFOk = R_SUCCEEDED(framebufferCreate(&g_fbF, &g_winF, FW, FH, PIXEL_FORMAT_RGBA_8888, 3));
            Log("calque brume : %s", g_fbFOk ? "ok" : "absent (sans voile)");
        }
    }
#endif
    Log("ecran : %s", g_fbOk ? "ok" : "ECHEC");
    if (!g_fbOk) GiveBack("l'ecran ne s'ouvre pas");             // (the power menu could not be shown: never keep that)
#ifdef PREVIEW
    FogInit();
#endif
    OvlnInit();
    BuildPlates();                                              // (now, while nobody is looking)
    {
        struct mallinfo mi = mallinfo();
        Log("memoire : %u Ko utilises sur %u Ko du tas", (unsigned)(mi.uordblks / 1024), (unsigned)((size_t)mi.arena / 1024));
    }
    g_t = Now();
    ClearRect(kAll);
    PresentU(kAll);                                             // one transparent frame
#ifndef PREVIEW
    SamsToHomeMenu(17);                                         // v100: the menu is never left held by an overlay that stopped
#endif
    bool stable = false, half = false;
    float lastDraw = -100;
    const float start = g_t;
    float last = g_t;
#ifdef PREVIEW
    Open(kPower);
#endif
    while (true) {
        // wait: for a message, a notification - or, while something moves, until the next frame is due (60/s: one every
        // 16.7 ms counted from the last one, drawing time included; 30/s if this console cannot keep up)
        const float period = half ? 1 / 30.0f : 1 / 60.0f, left = lastDraw + period - Now();
        const u64 frameWait = (u64)(fminf(period, fmaxf(0.001f, left)) * 1e9f);
        const bool anim = Animating(), settled = Settled();
#ifndef PREVIEW
        s32 idx = -1;
        u64 timeout = g_audOn ? (frameWait < 6000000ULL ? frameWait : 6000000ULL) : (anim && !settled ? frameWait :
                      (PlatesPending() ? 2000000ULL : (anim || !stable ? 1000000000ULL : UINT64_MAX)));
        if (g_powerWant >= 0 && timeout > 250000000ULL) timeout = 250000000ULL;   // v101: the power watch looks 4 times a second
        const bool vsPaced = g_vsyncOk && anim && !settled;
        if (vsPaced) {                                          // (moving: woken by the refresh; the sounds still every 6 ms)
            const u64 vt = g_audOn ? 6000000ULL : 100000000ULL;
            if (g_ovlnOk) waitMulti(&idx, vt, waiterForEvent(appletGetMessageEvent()), waiterForEvent(&g_ovlnEvent), waiterForEvent(&g_vsync));
            else waitMulti(&idx, vt, waiterForEvent(appletGetMessageEvent()), waiterForEvent(&g_vsync));
            if (idx == (g_ovlnOk ? 2 : 1)) g_vsTick = true;
        }
        else if (g_ovlnOk) waitMulti(&idx, timeout, waiterForEvent(appletGetMessageEvent()), waiterForEvent(&g_ovlnEvent));
        else waitMulti(&idx, timeout, waiterForEvent(appletGetMessageEvent()));
#else
        (void)settled; (void)anim;
        svcSleepThread(g_audOn && frameWait > 6000000ULL ? 6000000ULL : frameWait);
        if (!appletMainLoop()) break;
#endif
        g_t = Now(); g_dt = fminf(0.05f, g_t - last); last = g_t;
        if (g_shown != kIdle) {                                 // the atmosphere's clock: slows to a stop after kFreeze s without a touch
            const bool idle = g_t - g_lastInput >= kFreeze;
            Approach(g_speed, idle ? 0.0f : 1.0f, g_dt, 1.4f);
            if (idle && g_speed < 0.002f) g_speed = 0;
            g_at += g_dt * g_speed;
        }
#ifdef PREVIEW
        padUpdate(&g_pad);
        const u64 pd = padGetButtonsDown(&g_pad);
        g_pvDown = pd;
        if (pd & HidNpadButton_Plus) break;
        if (g_mode == kIdle) {
            if (pd & HidNpadButton_L) Open(kPower);
            if (pd & HidNpadButton_R) Open(kQuick);
            if (pd & HidNpadButton_ZL) { OnVolume((u16)((g_vol.step + 3) % 16)); }
            if (pd & HidNpadButton_ZR) ShowToast(L("Capture d'\xc3\xa9" "cran enregistr\xc3\xa9" "e", "Screenshot saved"));
        }
#endif
        if (!stable && g_t - start > 30.0f) { stable = true; SafetyStable(); Log("stable (30 s)"); }
#ifndef PREVIEW
        u32 msg = 0;
        while (R_SUCCEEDED(appletGetMessage(&msg))) {
            Log("message %u", msg);
            switch (msg) {
                case 20: Close(); break;                       // HOME (short) while shown: close
                case 21: Open(kQuick); break;                  // HOME held: the panel
                case 23: case 24: Open(kPower); break;         // Power held (middle / long press): the power menu
                default: appletProcessMessage(msg); break;
            }
        }
        if (g_ovlnOk && idx == 1) OvlnReceive();
#endif
        if (g_mode != kIdle) Input();
        PowerWatch();                                           // v101
#ifndef PREVIEW
        {   // v100 (perf, journal: the panel at ~40 FPS over the menu, 60 over a game) - the HOME menu is told when the HOME panel
            // is up (« SAMS » 16 shown / 17 hidden, the messages the qlaunch-ext loader already knows): the loader then stops
            // calling the menu (no full scene drawn behind the panel) and lets it go on once it is closed. Only a message pushed
            // after the frame's input: the power menu sends nothing of its own (opened from the panel, the menu simply stays
            // still until it closes); nothing waits for an answer.
            static bool s_menuHeld = false;
            const bool want = g_mode == kQuick || (s_menuHeld && g_mode == kPower);
            if (want != s_menuHeld) { s_menuHeld = want; SamsToHomeMenu(want ? 16 : 17); }
        }
#endif
        AudioPump();                                            // v74.4: the sounds
        const Mode before = g_shown;
        bool paceOk = true;
#ifndef PREVIEW
        paceOk = !vsPaced || g_vsTick;                          // (paced by the refresh: draw only when it came)
#endif
        if (paceOk && Animating() && (!Settled() || g_t - lastDraw > 1.0f)) {   // (at rest: the clock once a second; Render draws only what changed)
            if (g_shown != kIdle && g_prevFrame >= 0 && g_t - g_prevFrame < 0.5f) {   // (the time between two frames: 16.7 ms = perfect)
                const float gap = (g_t - g_prevFrame) * 1000;
                g_gapN++; g_gapSum += gap; if (gap > g_gapMax) g_gapMax = gap; if (gap > 20) g_gapLate++;
            }
            g_prevFrame = g_t; g_vsTick = false;
            Render();
            lastDraw = g_t;
#ifdef PREVIEW
            if (g_fbOk) { u32 st = 0; u32 *px = (u32 *)framebufferBegin(&pvFb, &st); PreviewCompose(px, st / 4); framebufferEnd(&pvFb); }
#endif
            if (!half && g_stFrames > 30 && g_stSumDraw / g_stFrames > 13.0) { half = true; Log("dessin trop lent (%.1f ms en moyenne) : 30 images/s", g_stSumDraw / g_stFrames); }
        }
        if (before != kIdle && g_shown == kIdle) { LogStats(before == kPower ? "alimentation ferme" : "volet HOME ferme"); half = false; }
        if (PlatesPending() && !Animating()) {                  // (the ambient colour changed in Brume: made again while nothing is
            if (g_pbPlate >= 3) PlatesBegin();                  //  shown, 24 rows a turn - never when a screen opens)
            PlatesStep(24);
        }
        g_logDefer = g_shown != kIdle;                          // (while a screen is shown: the journal waits in memory)
        if (!g_logDefer) LogFlush();
    }
#ifdef PREVIEW
    if (g_fbOk) framebufferClose(&pvFb);
#endif
    return 0;
}
#endif
