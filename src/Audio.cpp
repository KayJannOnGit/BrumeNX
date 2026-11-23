// Brume - sound playback with audren (see Audio.hpp).
//
// Memory: all the samples live in ONE block (the renderer's memory pool), read once from the SD
// at boot (~18 MB: lock screen 32 kHz stereo, soundscape loops 12 kHz stereo, wheel kit 16 kHz mono).
// Parsing is defensive: a missing or damaged file only silences that sound.
#include "Audio.hpp"
#include <switch.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

extern "C" int qext_game_running(void);

namespace brume::audio {

    namespace {
        // audren: num_voices is ALSO the number of channels, and a stereo voice takes 2. 10 soundscape loops
        // (20 channels) + 22 one-shot slots of up to 2 channels = 64. A finished voice keeps its channels until
        // it is re-initialised, so one-shots reuse a fixed ring of slots (audrvVoiceInit frees the old sound).
        // (v92: « Mes musiques » removed - its 2 stereo voices went back to the one-shots: 22 slots again)
        constexpr int kVoices = 64, kPads = 10, kShot0 = kPads, kShots = (kVoices - 2 * kPads) / 2, kShotEnd = kShot0 + kShots;
        // v28 - the « angelic » transition between two consoles (mockup setChord + angelSwell): the old chord
        // fades out fast, the new one comes back slowly after a short silence, while the choir sings it.
        constexpr float kOutTc = 0.12f, kInTc = 0.6f, kInDelay = 0.4f;
        constexpr int kClips = kSoundCount + kPads;
        constexpr u32 kMaxClip = 16u << 20;               // no sound file is bigger than 16 MB
        const char *const kNames[kClips] = {
            "wake", "press1", "press2", "bigbang", "sigh", "chime70", "chime81", "chime86",   // v28: lock chord Bb maj7
            "bellw60", "bellw74", "bells60", "bells74", "plink70", "plink84", "plinkh70", "plinkh84",
            "nav76", "whoosh", "thock", "tick2400", "tick1700", "low06", "low09", "low11", "low18", "swishcam", "dive", "deplacement",
            "swell0", "swell1", "swell2", "swell3", "swell4", "swell5", "swell6", "swell7",
            "crystal", "page_r", "page_l", "unlock12",
            "pop_open", "pop_close", "pop_move", "pop_ok", "pop_armed", "pop_thud", "pop_error",   // v78 (optional)
            "entree_plongee", "entree_salon", "entree_monte", "entree_descend", "sortie", "sortie_ton",   // v80 (optional)
            "pad_lock1", "pad_lock2", "pad_c0", "pad_c1", "pad_c2", "pad_c3", "pad_c4", "pad_c5", "pad_c6", "pad_c7" };
        // Remark from Yass (2026-11-15): the sigh (light going back into the button) was too quiet -> +8 dB
        bool g_custom[kClips] = {};                       // loaded from sdmc:/brume/perso/
        float Gain(int s) {
            if (s == Sigh) return 2.5f;
            if (s == Deplacement && !g_custom[s]) return 1.6f;   // tok: Yass, a bit louder (+4 dB)
            if (s >= Swell0 && s <= Swell7 && !g_custom[s]) return 1.5f;   // the choir: heard in the gap between two chords
            return 1.0f;
        }

        const AudioRendererConfig kCfg = {
            .output_rate = AudioRendererOutputRate_48kHz, .num_voices = kVoices, .num_effects = 0,
            .num_sinks = 1, .num_mix_objs = 1, .num_mix_buffers = 2 };

        struct Clip { u32 off = 0, bytes = 0, frames = 0, rate = 0, ch = 0; };
        struct Voice { bool used = false, sweep = false, fade = false; int ch = 2, snd = -1; float vol = 1, from = 0, to = 0, dur = 0, t = 0, started = 0; };   // v80: fade = it goes with Depart
        float        g_clock = 0;                         // seconds since Init (for the voice ages)
        int          g_keySemis = 0, g_harm[4] = { 50, 57, 61, 66 };
        float        g_keyPitch = 1.0f;
        bool Keyed(int s) { return s == Wake || s == Press1 || s == Press2 || s == BigBang || s == Unlock12 || s == Sigh || s == PageR || s == PageL || s == Deplacement ||
                                   s == EntreeMonte || s == EntreeDescend || s == SortieTon; }   // v80: the launch's tones (recorded on D)
        struct Pending { int s; float t, pan, vol, pitch; };
        Clip         g_clip[kClips];
        Voice        g_v[kVoices];
        Pending      g_pend[48];
        int          g_npend = 0;

        AudioDriver  g_drv;                               // (v100: declared before the pump thread that sends it)
        // v100 (perf): the renderer's update (audrvUpdate: a request to the audio service) on a thread of its own. perf.log:
        // « lent : audio::Update 216.9 ms » on the lock screen - the service answers late around sleep / wake, and the frame
        // waited for it. The frame now only changes the voices (in memory) and asks the thread to send them; while a send
        // is stuck, the frame's per-frame changes wait for the next frame and a new sound waits in the queue (g_pend) -
        // the frame never waits. The thread lives only while the menu runs (it ends 0.3 s after the last frame: nothing
        // runs while a game plays). The lock: 0 free, 1 the frame (recursive: g_depth), 2 the thread - atomics only.
        int          g_lock = 0, g_depth = 0;
        u32          g_kick = 0;
        int          g_thrState = 0;                      // 0 none, 1 running, 2 ended (to be closed by the frame)
        bool         g_quit = false;
        Thread       g_thr;
        bool TryDrv() {
            if (g_depth > 0) { g_depth++; return true; }
            int z = 0;
            if (!__atomic_compare_exchange_n(&g_lock, &z, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return false;
            g_depth = 1;
            return true;
        }
        void LockDrv() { while (!TryDrv()) svcSleepThread(100000); }   // (rare: Hush, Arrive, Silence... while a send is stuck)
        void UnlockDrv() { if (g_depth > 0 && --g_depth == 0) __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE); }
        struct Drv { Drv() { LockDrv(); } ~Drv() { UnlockDrv(); } };
        void Pump(void *) {
            u32 seen = __atomic_load_n(&g_kick, __ATOMIC_ACQUIRE);
            u64 last = armGetSystemTick();
            const u64 idle = armGetSystemTickFreq() * 3 / 10;
            while (!__atomic_load_n(&g_quit, __ATOMIC_ACQUIRE)) {
                const u32 k = __atomic_load_n(&g_kick, __ATOMIC_ACQUIRE);
                if (k != seen) {
                    int z = 0;
                    if (__atomic_compare_exchange_n(&g_lock, &z, 2, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
                        seen = k;
                        audrvUpdate(&g_drv);
                        __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE);
                        last = armGetSystemTick();
                    }
                } else if (armGetSystemTick() - last > idle) break;   // (the menu stopped: a game, sleep)
                svcSleepThread(2000000);
            }
            __atomic_store_n(&g_thrState, 2, __ATOMIC_RELEASE);
        }
        void Send() {                                     // (the frame: hold no lock here)
            if (__atomic_load_n(&g_thrState, __ATOMIC_ACQUIRE) == 2) { threadWaitForExit(&g_thr); threadClose(&g_thr); g_thrState = 0; }
            if (g_thrState == 0) {
                __atomic_store_n(&g_quit, false, __ATOMIC_RELEASE);
                if (R_SUCCEEDED(threadCreate(&g_thr, Pump, nullptr, nullptr, 0x4000, 0x2B, -2))) {
                    if (R_SUCCEEDED(threadStart(&g_thr))) g_thrState = 1;
                    else threadClose(&g_thr);
                }
            }
            if (g_thrState == 1) { __atomic_add_fetch(&g_kick, 1, __ATOMIC_RELEASE); return; }
            if (TryDrv()) { audrvUpdate(&g_drv); UnlockDrv(); }   // (no thread: at once, as before)
        }
        void StopPump() {
            if (g_thrState == 0) return;
            __atomic_store_n(&g_quit, true, __ATOMIC_RELEASE);
            threadWaitForExit(&g_thr); threadClose(&g_thr); g_thrState = 0;
        }
        AudioDriverWaveBuf g_wb[kVoices];
        u8          *g_pool = nullptr;
        size_t       g_poolSize = 0;
        bool         g_on = false, g_rendererUp = false, g_drvUp = false;
        int          g_next = 0, g_stage = 0, g_chord = 0;
        float        g_padVol[kPads] = {}, g_padTarget[kPads] = {}, g_padTc[kPads] = {}, g_padDelay[kPads] = {};
        float        g_padPitch[kPads] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, g_padPitchTo[kPads] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, g_pitchTc = 1.33f;
        float        g_duck = 1, g_duckTarget = 1, g_duckTc = 0.5f;
        float        g_sfxGain = 1, g_musicGain = 1;           // v43: Réglages > Son
        float        g_yield = 1, g_yieldTo = 1;              // v98: 0 while Spotify plays (Yield)
        char         g_err[160] = "";
        // v80 - a launch / the way back: g_away scales the soundscape (nappe, choirs) and the voices marked « fade »
        // (playing at Depart, or started after it - all but the entry sound); hushed: the game is starting, not a sound
        float        g_away = 1, g_awayTo = 1, g_awayTc = 0.3f, g_hushT = 0, g_arriveIn = -1;
        bool         g_departing = false, g_hushed = false;
        u64          g_beat0 = 0;                              // the last Update (a long gap = the menu was away)

        u32 Rd32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
        u16 Rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

        // WAV header: PCM 16 bits, mono or stereo, 8..48 kHz -> data offset / size.
        bool ParseWav(FILE *fp, long fileSize, u32 &dataOff, u32 &dataBytes, u32 &rate, u32 &ch) {
            u8 h[12];
            if (fread(h, 1, 12, fp) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
            bool fmtOk = false; long pos = 12;
            while (pos + 8 <= fileSize) {
                u8 c[8];
                if (fseek(fp, pos, SEEK_SET) || fread(c, 1, 8, fp) != 8) return false;
                const u32 len = Rd32(c + 4);
                if (!memcmp(c, "fmt ", 4)) {
                    u8 f[16];
                    if (len < 16 || fread(f, 1, 16, fp) != 16) return false;
                    rate = Rd32(f + 4); ch = Rd16(f + 2);
                    fmtOk = Rd16(f) == 1 && (ch == 1 || ch == 2) && Rd16(f + 14) == 16 && rate >= 8000 && rate <= 48000;
                } else if (!memcmp(c, "data", 4)) {
                    if (!fmtOk || len == 0 || len > kMaxClip || pos + 8 + (long)len > fileSize) return false;
                    dataOff = (u32)(pos + 8); dataBytes = len & ~(2 * ch - 1);
                    return true;
                }
                pos += 8 + (long)len + (len & 1);
            }
            return false;
        }

        void Fail(const char *what, const char *name) { snprintf(g_err, sizeof g_err, "%s %s", what, name ? name : ""); }

        void SetPan(int v, float pan) {
            Drv g;
            pan = pan > 1 ? 1 : pan < -1 ? -1 : pan;
            if (g_v[v].ch == 1) {
                // mono: equal power; x sqrt 2 so that pan 0 gives the recorded level in each ear (the kit was
                // recorded in mono: a panned source came out x0.707, an unpanned one at full level)
                const float x = (pan + 1) * 0.7853982f;
                audrvVoiceSetMixFactor(&g_drv, v, cosf(x) * 1.4142136f, 0, 0);
                audrvVoiceSetMixFactor(&g_drv, v, sinf(x) * 1.4142136f, 0, 1);
                return;
            }
            const float a = fabsf(pan) * 1.5707963f, c = cosf(a), s = sinf(a);   // Web Audio StereoPanner, stereo input
            const float ll = pan >= 0 ? c : 1, lr = pan >= 0 ? s : 0, rr = pan >= 0 ? 1 : c, rl = pan >= 0 ? 0 : s;
            audrvVoiceSetMixFactor(&g_drv, v, ll, 0, 0); audrvVoiceSetMixFactor(&g_drv, v, lr, 0, 1);
            audrvVoiceSetMixFactor(&g_drv, v, rl, 1, 0); audrvVoiceSetMixFactor(&g_drv, v, rr, 1, 1);
        }

        // v59 - the unlock has two versions: ONE place in the pool, as big as the larger, refilled when the choice changes
        const char *const kUnlockFile[2] = { "unlock12", "unlock_aurore" };
        int g_unlockStyle = 0;
        u32 g_unlockCap = 0;                              // bytes reserved for the unlock clip
        bool WavInfo(const char *path, u32 &off, u32 &bytes, u32 &rate, u32 &ch) {
            FILE *fp = fopen(path, "rb");
            if (!fp) return false;
            fseek(fp, 0, SEEK_END); const long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
            const bool ok = sz > 44 && ParseWav(fp, sz, off, bytes, rate, ch);
            fclose(fp);
            return ok;
        }

        bool StartVoice(int v, const Clip &k, bool loop, float vol, float pan, float pitch) {
            if (!k.frames) return false;
            Drv g;
            if (!audrvVoiceInit(&g_drv, v, (int)k.ch, PcmFormat_Int16, (int)k.rate)) return false;   // drops the old one itself
            g_v[v].used = true; g_v[v].ch = (int)k.ch; g_v[v].sweep = false; g_v[v].vol = vol;
            audrvVoiceSetDestinationMix(&g_drv, v, AUDREN_FINAL_MIX_ID);
            SetPan(v, pan);
            audrvVoiceSetVolume(&g_drv, v, vol);
            audrvVoiceSetPitch(&g_drv, v, pitch);
            AudioDriverWaveBuf &wb = g_wb[v];
            memset(&wb, 0, sizeof wb);
            wb.data_raw = g_pool + k.off; wb.size = k.bytes;
            wb.start_sample_offset = 0; wb.end_sample_offset = (s32)k.frames; wb.is_looping = loop;
            audrvVoiceAddWaveBuf(&g_drv, v, &wb);
            audrvVoiceStart(&g_drv, v);
            return true;
        }

        // v29 (Yass: « some sounds seem broken at the unlock »): more than 22 sounds start at once there (big bang,
        // 8 consoles' plinks and landings, the cubes' notes, the banner tune...) and the old ring cut voices that were
        // still playing. Now: a finished voice first; if none, the oldest one, never a protected sound (the big bang,
        // the choirs, the lock screen's long sounds) unless everything is protected.
        bool Protected(int s) { return s == BigBang || s == Unlock12 || s == Wake || s == Press1 || s == Press2 || s == Sigh || (s >= Swell0 && s <= Swell7); }   // v70.8: + Unlock12 (Aurore was cut right after the unlock: the consoles' sounds took its voice)
        int PickShot() {
            Drv g;
            int best = -1; float bestAge = -1;
            for (int k = 0; k < kShots; k++) {
                const int v = kShot0 + (g_next + k) % kShots;
                if (!g_v[v].used || !audrvVoiceIsPlaying(&g_drv, v)) { g_next = (v - kShot0 + 1) % kShots; return v; }
                const float age = g_clock - g_v[v].started - (Protected(g_v[v].snd) ? 1000.0f : 0.0f);
                if (age > bestAge) { bestAge = age; best = v; }
            }
            return best;
        }
        int PlayNow(int s, float pan, float vol, float pitch, bool keep = false) {   // keep (v80): not faded by Depart (the entry sound)
            if (!g_on || g_hushed || s < 0 || s >= kSoundCount || !g_clip[s].frames) return -1;
            if (!TryDrv()) {                              // v100: a send is stuck - the sound waits in the queue (next frame)
                if (g_npend < (int)(sizeof g_pend / sizeof g_pend[0])) g_pend[g_npend++] = { s, 0.0f, pan, vol, pitch };
                return -1;
            }
            struct Un { ~Un() { UnlockDrv(); } } un;
            const int v = PickShot();
            if (v < 0) return -1;
            if (g_v[v].used) audrvVoiceStop(&g_drv, v);   // (never audrvVoiceDrop an unused voice: crash 2168-0002)
            if (Keyed(s) && !g_custom[s]) pitch *= g_keyPitch;      // v29.2: in the console's key
            const float on = s >= Swell0 && s <= Swell7 ? g_musicGain * g_yieldTo : g_sfxGain;   // v43: the choirs belong to the nappe
            if (on <= 0.0f) return -1;
            if (!StartVoice(v, g_clip[s], false, vol * Gain(s) * on, pan, pitch)) return -1;
            g_v[v].snd = s; g_v[v].started = g_clock; g_v[v].fade = g_departing && !keep;
            return v;
        }

        float Semis(float st) { return exp2f(st / 12.0f); }
        void RetargetPads(float tc) {
            for (int p = 0; p < kPads; p++) g_padTarget[p] = 0;
            if (g_stage == 1) g_padTarget[0] = 1;
            else if (g_stage == 2) g_padTarget[1] = 1;
            else if (g_stage == 3) g_padTarget[2 + (g_chord & 7)] = 1;
            for (int p = 0; p < kPads; p++) { g_padTc[p] = tc; g_padDelay[p] = 0; }
        }

    }

    const char *Error() { return g_err; }

    bool Init() {
        static const char *kDir = "sdmc:/brume/sons/";
        char path[96];
        // Yass's own sounds win: sdmc:/brume/perso/<name>.wav (WAV 16-bit, mono or stereo, any rate) replaces ours
        auto pathOf = [&](int i) {
            snprintf(path, sizeof path, "sdmc:/brume/perso/%s.wav", kNames[i]);
            FILE *c = fopen(path, "rb");
            if (c) { fclose(c); g_custom[i] = true; return; }
            const char *nm = kNames[i];
            if (i == Unlock12) {                       // v59: the chosen version (if its file is there)
                snprintf(path, sizeof path, "%s%s.wav", kDir, kUnlockFile[g_unlockStyle & 1]);
                if (FILE *a = fopen(path, "rb")) { fclose(a); nm = kUnlockFile[g_unlockStyle & 1]; }
            }
            snprintf(path, sizeof path, "%s%s.wav", kDir, nm);
        };
        size_t total = 0;
        u32 dOff[kClips] = {};
        for (int i = 0; i < kClips; i++) {          // 1. sizes, so the whole pool is allocated once
            pathOf(i);
            FILE *fp = fopen(path, "rb");
            if (!fp) { if (i < PopOpen || i >= kSoundCount) Fail("son manquant :", kNames[i]); continue; }   // (v78: the popups' are optional; v80: the launch's too)
            fseek(fp, 0, SEEK_END); const long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
            u32 off = 0, bytes = 0, rate = 0, ch = 0;
            if (sz > 44 && ParseWav(fp, sz, off, bytes, rate, ch)) {
                g_clip[i].bytes = bytes; g_clip[i].frames = bytes / (2 * ch); g_clip[i].rate = rate; g_clip[i].ch = ch;
                g_clip[i].off = (u32)total; dOff[i] = off;
                u32 cap = bytes;
                if (i == Unlock12 && !g_custom[i])       // v59: room for the larger of the two versions
                    for (const char *u : kUnlockFile) {
                        char q[96]; snprintf(q, sizeof q, "%s%s.wav", kDir, u);
                        u32 o2, b2, r2, c2;
                        if (WavInfo(q, o2, b2, r2, c2) && b2 > cap && b2 <= kMaxClip) cap = b2;
                    }
                if (i == Unlock12) g_unlockCap = cap;
                total += (cap + 0xFFF) & ~(size_t)0xFFF;     // each clip starts on a 4 KB boundary
            } else Fail("son illisible :", kNames[i]);
            fclose(fp);
        }
        if (!total) { Fail("aucun son dans", kDir); return false; }
        g_poolSize = total;
        g_pool = (u8 *)memalign(0x1000, g_poolSize);
        if (!g_pool) { Fail("pas assez de memoire pour les sons", nullptr); return false; }
        memset(g_pool, 0, g_poolSize);
        for (int i = 0; i < kClips; i++) {          // 2. the samples, straight into the pool
            if (!g_clip[i].bytes) continue;
            pathOf(i);
            FILE *fp = fopen(path, "rb");
            bool ok = fp && !fseek(fp, dOff[i], SEEK_SET) && fread(g_pool + g_clip[i].off, 1, g_clip[i].bytes, fp) == g_clip[i].bytes;
            if (fp) fclose(fp);
            if (!ok) { g_clip[i] = Clip{}; Fail("lecture ratee :", kNames[i]); }
        }
        armDCacheFlush(g_pool, g_poolSize);
        // 3. the renderer
        if (R_FAILED(audrenInitialize(&kCfg))) { Fail("audren indisponible", nullptr); Shutdown(); return false; }
        g_rendererUp = true;
        if (R_FAILED(audrvCreate(&g_drv, &kCfg, 2))) { Fail("audrv indisponible", nullptr); Shutdown(); return false; }
        g_drvUp = true;
        const int mp = audrvMemPoolAdd(&g_drv, g_pool, g_poolSize);
        if (mp < 0 || !audrvMemPoolAttach(&g_drv, mp)) { Fail("memoire audio refusee", nullptr); Shutdown(); return false; }
        static const u8 kSink[2] = {0, 1};
        if (audrvDeviceSinkAdd(&g_drv, AUDREN_DEFAULT_DEVICE_NAME, 2, kSink) < 0) { Fail("sortie audio refusee", nullptr); Shutdown(); return false; }
        audrvUpdate(&g_drv);
        audrenStartAudioRenderer();
        // the soundscape loops run all the time, in sync (same length and rate), silent: a change = a crossfade
        for (int p = 0; p < kPads; p++) StartVoice(p, g_clip[kSoundCount + p], true, 0.0f, 0.0f, 1.0f);
        audrvUpdate(&g_drv);
        g_on = true;
        return true;
    }

    void Shutdown() {
        StopPump();                                      // v100
        if (g_drvUp) {
            for (int v = 0; v < kVoices; v++) if (g_v[v].used) { audrvVoiceStop(&g_drv, v); g_v[v].used = false; }
            audrvUpdate(&g_drv);
            audrvClose(&g_drv); g_drvUp = false;
        }
        if (g_rendererUp) { audrenStopAudioRenderer(); audrenExit(); g_rendererUp = false; }
        free(g_pool); g_pool = nullptr; g_on = false;
    }

    void Play(Sound s, float pan, float vol, float pitch, float delay) {
        if (g_hushed) return;                            // v80: a game is starting
        if (delay > 0.0f) {
            if (g_npend < (int)(sizeof g_pend / sizeof g_pend[0])) g_pend[g_npend++] = { (int)s, delay, pan, vol, pitch };
            return;
        }
        PlayNow(s, pan, vol, pitch);
    }

    bool Has(Sound s) { return g_on && s >= 0 && s < kSoundCount && g_clip[s].frames; }

    void PlaySweep(Sound s, float panFrom, float panTo, float dur, float vol) {
        const int v = PlayNow(s, panFrom, vol, 1.0f);
        if (v < 0) return;
        g_v[v].sweep = true; g_v[v].from = panFrom; g_v[v].to = panTo; g_v[v].dur = dur; g_v[v].t = 0;
    }

    // recorded: bell(60|74, .045, 3 / 3.3, 0, wetBus|sfxBus)
    void Bell(int midi, float vel, bool wet, float delay) {
        const int ref = midi < 67 ? 60 : 74;
        const Sound s = wet ? (ref == 60 ? BellW60 : BellW74) : (ref == 60 ? BellS60 : BellS74);
        Play(s, 0.0f, vel / 0.045f, Semis((float)(midi - ref)), delay);
    }
    // recorded: popNote at 70 and 84, normal and « hero » (its own levels)
    void PopNote(int midi, float pan, bool hero) {
        const int ref = midi < 77 ? 70 : 84;
        Play(hero ? (ref == 70 ? PlinkH70 : PlinkH84) : (ref == 70 ? Plink70 : Plink84), pan, 1.0f, Semis((float)(midi - ref)));
    }
    // navSound: the chord's note + our « tok » (selectTick); a deplacement.wav from Yass replaces both (mockup: playCustom)
    void Nav(int midi, float pan) {
        if (g_custom[Deplacement]) { Play(Deplacement, pan); return; }
        Play(Nav76, pan, 1.0f, Semis((float)(midi - 76)));         // recorded at 76
        Play(Deplacement, pan);
    }
    // v29: a cube touched (crystalTouch, recorded at 1318 Hz): stronger = louder, pitch = playback speed (big cubes lower)
    void Crystal(float pan, float strength, int midi) {
        const float s = strength < 0 ? 0 : strength > 1 ? 1 : strength;
        Play(CrystalSnd, pan, 0.5f + 1.3f * s, Semis((float)(midi - 84)));   // recorded at 84 (C6)
    }
    void SetKey(int semis) { g_keySemis = semis; g_keyPitch = exp2f(semis / 12.0f); }
    int  KeySemis() { return g_keySemis; }
    void SetHarmony(const int *n) { for (int i = 0; i < 4; i++) g_harm[i] = n[i]; }
    int  HarmonyNote(int k, int lo, int hi) { int m = g_harm[k & 3]; while (m < lo) m += 12; while (m > hi) m -= 12; return m; }
    void Thock(float pan, float pitch, float vel) { Play(ThockSnd, pan, vel / 0.22f, pitch); }   // recorded thock(0,1,.22)
    void SoftLow(int midi, float vel, float dur) {                                               // recorded softLow(50, .1, dur)
        const Sound s = dur < 0.75f ? Low06 : dur < 1.0f ? Low09 : dur < 1.4f ? Low11 : Low18;
        Play(s, 0.0f, vel / 0.1f, Semis((float)(midi - 50)));
    }

    // mockup: setMusicStage(n) -> setTargetAtTime(..., n ? 1.1 : 0.5); down here: 1.5 s, because the
    // mockup's 8 s reverb keeps ringing while its voices stop, and here the reverb is inside the loops
    void SetMusic(int stage) {
        g_stage = stage;
        RetargetPads(stage ? 1.1f : 1.5f);
        if (g_on && (stage == 1 || stage == 2))                 // v29.2: the lock screen's loops in the console's key
            for (int p = 0; p < 2; p++) { Drv g; g_padPitch[p] = g_padPitchTo[p] = g_keyPitch; audrvVoiceSetPitch(&g_drv, p, g_keyPitch); }
    }
    // mockup: setChord(ch, glide). From one console to another (v28, « angelic »): the old chord goes out fast,
    // the new one waits kInDelay in the silence then comes back slowly, and the console's choir sings it (angelSwell).
    // Anything else (unlock, same console): setTargetAtTime(freq, t, glide / 3) as before.
    // v29.1 (Yass: « the chord moving, like at the start of the mockup ») - the mockup glides every voice to the new
    // chord (setTargetAtTime(freq, t, glide / 3)). The loops here are recorded chords, so: during the crossfade the
    // old loop glides in pitch towards the new root while the new one arrives from the old root and settles - the
    // whole soundscape is heard sliding to the new chord. The choir stays, softer.
    void SetChord(int console, float glide, float semis) {
        const int old = g_chord; g_chord = console;
        if (g_stage != 3) return;
        if (old == console || glide < 3.5f) { RetargetPads(glide / 3.0f); return; }   // unlock (2.5): as before
        const int op = 2 + (old & 7), np = 2 + (console & 7);
        const float tc = glide / 3.0f, r = exp2f(semis / 12.0f);
        for (int p = 0; p < kPads; p++) { g_padTarget[p] = 0; g_padTc[p] = tc; g_padDelay[p] = 0; }
        g_padTarget[np] = 1;
        g_padPitchTo[op] = r;                                     // the old chord slides towards the new root...
        if (g_padVol[np] < 0.05f) g_padPitch[np] = 1.0f / r;      // ...the new one comes from the old root
        g_padPitchTo[np] = 1.0f;
        g_pitchTc = tc;
        PlayNow(Swell0 + (console & 7), 0.0f, 0.55f, 1.0f);
    }
    void Duck(float level, float tc) { g_duckTarget = level; g_duckTc = tc; }
    void SetSfxOn(bool on) { g_sfxGain = on ? 1.0f : 0.0f; }
    void SetMusicOn(bool on) { g_musicGain = on ? 1.0f : 0.0f; }   // v43 (v92: the nappe only again, « Mes musiques » removed)
    void Yield(bool on) { g_yieldTo = on ? 0.0f : 1.0f; }   // v98 (Spotify.cpp, every frame)

    // ---- v80: a launch and the way back ----
    void Depart(float tc) {
        g_departing = true; g_awayTo = 0; g_awayTc = tc > 0.02f ? tc : 0.02f;
        for (int v = kShot0; v < kShotEnd; v++) if (g_v[v].used) g_v[v].fade = true;   // the tails of what plays now go too
    }
    void EntrySound(bool portable, float pan) {
        const Sound air = portable ? EntreePlongee : EntreeSalon, tone = portable ? EntreeMonte : EntreeDescend;
        if (!Has(air)) { PlayNow(Dive, pan, 2.2f, 1.0f); return; }   // (no files: the old dive sound, fading with the rest)
        const int v = PlayNow(air, pan, 1.0f, 1.0f, true);
        if (v >= 0 && portable) {                        // the dive comes to the middle of the screen
            g_v[v].sweep = true; g_v[v].from = pan; g_v[v].to = 0.0f; g_v[v].dur = 0.35f; g_v[v].t = 0;
        }
        PlayNow(tone, pan * 0.5f, 1.0f, 1.0f, true);     // (in the console's key: Keyed)
    }
    void Hush() {
        g_hushed = true; g_departing = false; g_hushT = 0; g_arriveIn = -1;
        g_npend = 0;
        g_away = g_awayTo = 0;
        if (!g_on) return;
        Drv g;                                           // v100
        for (int p = 0; p < kPads; p++) { audrvVoiceSetVolume(&g_drv, p, 0.0f); audrvVoiceSetPaused(&g_drv, p, true); }   // (together: back in sync)
        for (int v = kShot0; v < kShotEnd; v++)
            if (g_v[v].used) { audrvVoiceSetVolume(&g_drv, v, 0.0f); audrvVoiceStop(&g_drv, v); g_v[v].sweep = g_v[v].fade = false; }
        audrvUpdate(&g_drv);
    }
    void Arrive(float tc) {
        const bool was = g_hushed;
        g_hushed = false; g_departing = false; g_arriveIn = -1;
        g_awayTo = 1; g_awayTc = tc > 0.02f ? tc : 0.02f;
        if (!g_on) return;
        for (int v = kShot0; v < kShotEnd; v++) g_v[v].fade = false;   // (a launch that failed early: its faded tails stay quiet)
        if (was) { Drv g; for (int p = 0; p < kPads; p++) audrvVoiceSetPaused(&g_drv, p, false); audrvUpdate(&g_drv); }
    }
    void ExitSound(float pan) {
        if (!Has(Sortie)) return;
        PlayNow(Sortie, pan, 1.0f, 1.0f);
        PlayNow(SortieTon, pan * 0.5f, 1.0f, 1.0f);      // (in the console's key: Keyed)
    }

    void SetUnlock(int style) {
        style &= 1;
        if (style == g_unlockStyle && g_on) return;
        g_unlockStyle = style;                           // before Init: Init loads this one
        if (!g_on || !g_pool || g_custom[Unlock12] || !g_unlockCap) return;
        char path[96];
        snprintf(path, sizeof path, "sdmc:/brume/sons/%s.wav", kUnlockFile[style]);
        u32 off = 0, bytes = 0, rate = 0, ch = 0;
        if (!WavInfo(path, off, bytes, rate, ch) || bytes > g_unlockCap || !ch) return;
        Drv g;                                           // v100
        for (int v = kShot0; v < kVoices; v++)           // nothing may be reading it
            if (g_v[v].used && g_v[v].snd == Unlock12) { audrvVoiceStop(&g_drv, v); g_v[v].snd = -1; }
        for (int k = 0; k < g_npend; k++) if (g_pend[k].s == Unlock12) g_pend[k].s = -1;
        audrvUpdate(&g_drv);
        Clip &c = g_clip[Unlock12];
        FILE *fp = fopen(path, "rb");
        const bool ok = fp && !fseek(fp, off, SEEK_SET) && fread(g_pool + c.off, 1, bytes, fp) == bytes;
        if (fp) fclose(fp);
        if (!ok) { c.frames = 0; return; }               // (silent rather than noise)
        c.bytes = bytes; c.frames = bytes / (2 * ch); c.rate = rate; c.ch = ch;
        armDCacheFlush(g_pool + c.off, bytes);
    }

    void Silence() {
        g_npend = 0;
        for (int p = 0; p < kPads; p++) g_padTarget[p] = g_padVol[p] = 0;
        const bool hushed = g_hushed;                    // v80: asleep during a launch / a game: the wake starts afresh (the lock
        g_hushed = g_departing = false; g_arriveIn = -1; g_away = g_awayTo = 1;   // screen's sounds are not held back)
        if (!g_on) return;
        Drv g;                                           // v100
        for (int v = kShot0; v < kShotEnd; v++) g_v[v].fade = false;
        if (hushed) for (int p = 0; p < kPads; p++) audrvVoiceSetPaused(&g_drv, p, false);   // (silent: their volume is 0)
        for (int v = 0; v < kVoices; v++) if (g_v[v].used) audrvVoiceSetVolume(&g_drv, v, 0.0f);
        audrvUpdate(&g_drv);
    }

    void Update(float dt) {
        if (!g_on) return;
        if (!TryDrv()) return;                           // v100: a send is stuck (sleep / wake): this frame's changes wait, never the frame
        g_clock += dt;
        {   // v80 - hushed (a game is starting): back in the menu, Games / Apps say it (Arrive) when the light comes back; else it
            // comes back by itself 0.6 s later - and after 15 s of menu without the game taking the screen: never a silent menu
            const u64 tk = armGetSystemTick();
            const bool away = g_beat0 && tk - g_beat0 > armGetSystemTickFreq() * 6 / 10;
            g_beat0 = tk;
            if (g_hushed) {
                g_hushT += dt;
                if (away && g_arriveIn < 0) g_arriveIn = 0.6f;
                else if (g_arriveIn >= 0 && (g_arriveIn -= dt) < 0) Arrive(1.3f);
                if (g_hushed && g_hushT > 15.0f) Arrive(1.3f);
            }
            g_away += (g_awayTo - g_away) * (1.0f - expf(-dt / g_awayTc));
        }
        for (int i = 0; i < g_npend;) {                 // delayed notes (melodies, staggered bells)
            g_pend[i].t -= dt;
            if (g_pend[i].t <= 0) { const Pending p = g_pend[i]; g_pend[i] = g_pend[--g_npend]; PlayNow(p.s, p.pan, p.vol, p.pitch); }
            else i++;
        }
        g_duck += (g_duckTarget - g_duck) * (1.0f - expf(-dt / g_duckTc));
        g_yield += (g_yieldTo - g_yield) * (1.0f - expf(-dt / 0.5f));   // v98: Spotify plays - the nappe fades out (and back)
        for (int p = 0; p < kPads; p++) {
            float target = g_padTarget[p], tc = g_padTc[p] > 0.01f ? g_padTc[p] : 1.1f;
            if (g_padDelay[p] > 0) { g_padDelay[p] -= dt; target = 0; tc = kOutTc; }   // the silence before the new chord
            g_padVol[p] += (target - g_padVol[p]) * (1.0f - expf(-dt / tc));
            {   // v29.1: the glide (and back to 1 once silent, ready for the next time)
                const float before = g_padPitch[p];
                if (g_padVol[p] < 0.01f && g_padTarget[p] == 0) { g_padPitch[p] = 1.0f; g_padPitchTo[p] = 1.0f; }
                else g_padPitch[p] += (g_padPitchTo[p] - g_padPitch[p]) * (1.0f - expf(-dt / g_pitchTc));
                if (fabsf(g_padPitch[p] - before) > 1e-5f) audrvVoiceSetPitch(&g_drv, p, g_padPitch[p]);
            }
            audrvVoiceSetVolume(&g_drv, p, g_padVol[p] * g_duck * g_musicGain * g_yield * g_away);
        }
        for (int v = kShot0; v < kVoices; v++) {        // sounds crossing the stage (whoosh: linear pan ramp)
            Voice &x = g_v[v];
            if (!x.used) continue;
            if (x.fade) audrvVoiceSetVolume(&g_drv, v, x.vol * g_away);   // v80: going with Depart
            if (!x.sweep) continue;
            x.t += dt;
            const float f = x.dur > 0 ? fminf(1.0f, x.t / x.dur) : 1.0f;
            SetPan(v, x.from + (x.to - x.from) * f);
            if (f >= 1.0f) x.sweep = false;
        }
        UnlockDrv();
        Send();                                          // v100: the renderer's update, on its thread
    }

}
