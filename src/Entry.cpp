// Brume - entry points the qlaunch-ext loader resolves with dlsym, the scene
// (the PS2-boot fog with water cubes) and the lock screen on top of it.
//
// The loader owns the system side: HOME, sleep, game exit, and it does not
// call loop() while a game is in the foreground (zero cost in game).
#include "Gpu.hpp"
#include "Lang.hpp"
#include "Ui.hpp"
#include "Lock.hpp"
#include "Audio.hpp"
#include "Assets.hpp"
#include "Wheel.hpp"
#include "Games.hpp"
#include "Emus.hpp"
#include "Album.hpp"
#include "Apps.hpp"
#include "Pads.hpp"
#include "System.hpp"
#include "Settings.hpp"
#include "Profiles.hpp"
#include "Nextendo.hpp"
#include "Covers.hpp"
#include "Social.hpp"
#include "Notify.hpp"
#include "Popup.hpp"
#include "Ntp.hpp"
#include "Clock.hpp"
#include "DatePages.hpp"
#include "Journal.hpp"
#include "Haptics.hpp"
#include "Theme.hpp"
#include "BtAudio.hpp"
#include "Spotify.hpp"
#include "Charge.hpp"
#include "MyPage.hpp"   // v96: the profile page
#include "Pin.hpp"
#include "Worker.hpp"
#include "shaders.h"       // generated: dksh blobs
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <malloc.h>
#include <cstring>
#include <sys/stat.h>

using namespace brume;

// v92 - the pages (the Musique page is gone; v95: Amis): 0 Jeux, 1 Apps, 2 Album, 3 Manettes, 4 Amis, 5 Réglages - every module agrees
static_assert(Apps::kPage == Wheel::kPageApps && Album::kPage == Wheel::kPageAlbum && Pads::kPage == Wheel::kPagePads && social::kPage == Wheel::kPageFriends && Settings::kPage == Wheel::kPageSettings && Wheel::kPageSettings == Wheel::kPages - 1 && Wheel::kTabs == Wheel::kPages + 1, "the pages");

namespace {
    constexpr const char *kDir      = "sdmc:/brume";
    constexpr const char *kPerfLog  = "sdmc:/brume/perf.log";
    constexpr const char *kTrialTag = "sdmc:/brume/essai.txt";
    constexpr const char *kMenuIni  = "sdmc:/qlaunch-ext/config/menu.ini";
    constexpr int kHazeW = 384, kHazeH = 216;

    struct alignas(16) SceneUbo {      // matches shaders/common.glsl (std140)
        float screen[4];
        float haze[4];
        float cube[6][4];               // v29: up to 6 water cubes
        float rot[6][4];
        float cubeFx[6][4];
        float light[4];
        float origin[4];
        float b[4][4];
        float bo[4][4];
        float ambient[4];               // v29: the ambient colour (Réglages > Brume)
        float glass[4];                 // v73: the settings icons' liquid glass: light direction (screen), lens, presence
    };
    static_assert(sizeof(SceneUbo) <= 0x200, "the Scene block has 512 bytes (Gpu.cpp kUboSize): it is full");

    Gpu     g_gpu;
    Target  g_haze, g_atlas[2];
    Program g_hazeProg, g_glassProg, g_uiProg, g_meshProg, g_iconProg, g_cubeQuadProg, g_boxProg;
    Program g_uiLiteProg, g_uiSurfProg;   // v99 (perf): the interface's simple modes (uilite_fsh), its surfaces (uisurf_fsh)
    Program g_glassBgProg, g_glassCubeProg;   // v90: the glass pass in two (the room, then the cubes in their squares)
    bool    g_glassSplit = false;
    Assets  g_assets;
    Album   g_album;                     // v34: the Album page
    Apps    g_apps;                      // v35: the Apps page (homebrews)
    Pads    g_pads;                      // v39: the Manettes page (and the hello of a controller that connects)
    Wheel   g_wheel;
    Games   g_games;
    System  g_system;
    Settings g_settings;               // v28: the settings page (carousel of glass icons)
    Profiles g_profiles;               // v46: the profiles (the bar's pill, the panel)
    int     g_probeFrame = 0;          // diagnosis (see the probes in Frame)
    Ui      g_ui;
    Lock    g_lock;
    bool    g_ready = false;
    u64     g_t0 = 0;                   // boot tick
    u32     g_seed = 0;                 // cube layout, new every boot
    PadState g_pad;

    // ---- perf log: first two minutes only (no SD wear), one line every 2 s ----
    u64   g_lastTick = 0, g_winStart = 0;
    float g_cpuSum = 0, g_cpuMax = 0, g_gpuSum = 0, g_gpuMax = 0, g_frameSum = 0, g_frameMax = 0, g_stSum[Gpu::kMarks + 1] = {};
    int   g_n = 0, g_lines = 0, g_slowFrames = 0;
    int   g_fogThirds = 0;                // v90: images whose fog was drawn by thirds (the calm ones), in the 2 s
    u64   g_loopEnd = 0;                // v90: when the last image ended (the time outside Brume)

    float Seconds(u64 ticks) { return (float)((double)ticks / (double)armGetSystemTickFreq()); }
    // v60 (review) - the menu's own clock: it only runs while the menu is drawn (not during a game, not asleep). The old
    // « seconds since boot » float lost precision with the uptime (16 ms steps after ~36 h, 31 ms after ~3 days: the
    // animations would have stuttered); this one would need 36 h of menu on screen.
    double g_menuTime = 0;
    float  g_manageAt = -1;                                   // v67: « Gérer les logiciels » asked from the settings
    bool   g_manageBack = false;                              // v79: ... and B goes straight back there (one press)
    u64    g_clockTick = 0;                                   // the last frame's tick (0 = the menu just came back)
    float  g_calmAt = -1;                                     // v80: back from a game / an app, the way back's staging until then
    float  MenuNow() { return (float)g_menuTime; }

    // Lines are kept in memory and written in one go (writing to the SD inside a
    // frame causes the very hitch we are trying to measure).
    char g_log[16384];
    size_t g_logLen = 0;
    // v90 (perf.log: an append to perf.log on the card took 20-35 ms - the hitch after every « [roue] » line, and each
    // « jeux : jaquette » line was flushed inside the frame): the lines are written by a short thread (Worker.hpp), the
    // frame only copies them. FlushLogNow = written before it returns (sleep, shutdown, a GPU error, the journal).
    char   g_logOut[16384];
    size_t g_logOutLen = 0;
    Worker g_logWork;
    void WriteLogOut(void *) {
        if (FILE *fp = fopen(kPerfLog, "a")) { fwrite(g_logOut, 1, g_logOutLen, fp); fclose(fp); }
    }
    void FlushLog() {
        g_logWork.Done();                                // (the last write is over: its thread is closed)
        if (!g_logLen || !g_logWork.Idle()) return;      // (still writing: these lines go with the next flush)
        memcpy(g_logOut, g_log, g_logLen);
        g_logOutLen = g_logLen;
        g_logLen = 0;
        g_logWork.Start(WriteLogOut, nullptr, 0x8000);
    }
    void FlushLogNow() {
        g_logWork.Wait();                                // (the lines on their way first: the order stays)
        if (!g_logLen) return;
        if (FILE *fp = fopen(kPerfLog, "a")) { fwrite(g_log, 1, g_logLen, fp); fclose(fp); }
        g_logLen = 0;
    }
    void LogLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
    void LogLine(const char *fmt, ...) {
        if (g_logLen > sizeof(g_log) - 256) FlushLog();
        if (g_logLen > sizeof(g_log) - 256) FlushLogNow();   // (v90: the thread is still writing and the buffer is full: rare)
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(g_log + g_logLen, sizeof(g_log) - g_logLen - 1, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        g_logLen += (size_t)n < sizeof(g_log) - g_logLen - 1 ? (size_t)n : sizeof(g_log) - g_logLen - 2;
        g_log[g_logLen++] = '\n';
    }

    // v90 - what took long on the main thread: each step of a frame is timed (two reads of the tick), and one that took more
    // than 12 ms is written to perf.log (« lent : <step> <ms> ») - at most one line per step every 2 s, 300 a boot
    constexpr int kSteps = 48;
    float g_slowAt[kSteps];
    int   g_slowLines = 0;
    void StepEnd(int id, const char *name, u64 t0, float now) {
        const float ms = Seconds(armGetSystemTick() - t0) * 1000.0f;
        if (ms < 12.0f || g_slowLines >= 300 || id < 0 || id >= kSteps) return;
        if (g_slowAt[id] > 0 && now - g_slowAt[id] < 2.0f && now >= g_slowAt[id]) return;
        g_slowAt[id] = now > 0 ? now : 0.001f;
        g_slowLines++;
        LogLine("lent : %s %.1f ms (t=%.1fs)", name, ms, now);
    }
    // v101 - the watchdog: a small thread that looks every 0.5 s whether the frame came back. If one frame lasts more than 3 s
    // (the menu frozen), it writes down WHERE (the step running) in sdmc:/brume/blocage.txt, once per freeze. It never touches the
    // frame's own log (the frame may be stuck in it): its own file, its own line. Costs nothing: a sleep and two atomic reads.
    const char *g_wdWhere = "";                 // (atomic) the step running now
    u64   g_wdIn = 0;                           // (atomic) when the frame began (0: Brume is not in a frame)
    Thread g_wdThr;
    inline void WdAt(const char *w) { __atomic_store_n(&g_wdWhere, w, __ATOMIC_RELEASE); }
    bool  g_wdOn = false;
    int   g_wdStop = 0;
    void WatchdogRun(void *) {
        bool said = false;
        while (!__atomic_load_n(&g_wdStop, __ATOMIC_ACQUIRE)) {
            svcSleepThread(500000000ULL);
            const u64 in = __atomic_load_n(&g_wdIn, __ATOMIC_ACQUIRE), t = armGetSystemTick();
            const bool stuck = in && t > in && armTicksToNs(t - in) > 3000000000ULL;
            if (stuck && !said) {
                said = true;
                const char *where = __atomic_load_n(&g_wdWhere, __ATOMIC_ACQUIRE);
                if (FILE *f = fopen("sdmc:/brume/blocage.txt", "a")) {
                    fprintf(f, "menu bloque depuis %.1f s, dans : %s\n", armTicksToNs(t - in) / 1e9, where ? where : "?");
                    fclose(f);
                }
            }
            if (!stuck) said = false;
        }
    }
    void WatchdogStart() {
        if (g_wdOn) return;
        __atomic_store_n(&g_wdStop, 0, __ATOMIC_RELEASE);
        g_wdOn = R_SUCCEEDED(threadCreate(&g_wdThr, WatchdogRun, nullptr, nullptr, 0x2000, 0x3B, -2));
        if (g_wdOn && R_FAILED(threadStart(&g_wdThr))) { threadClose(&g_wdThr); g_wdOn = false; }
    }
    void WatchdogStop() {
        if (!g_wdOn) return;
        __atomic_store_n(&g_wdStop, 1, __ATOMIC_RELEASE);
        threadWaitForExit(&g_wdThr); threadClose(&g_wdThr); g_wdOn = false;   // (<= 0.5 s: its sleep)
    }
#define STEP(id, name, ...) do { WdAt(name); const u64 t0_ = armGetSystemTick(); __VA_ARGS__; StepEnd(id, name, t0_, now); WdAt("entre deux etapes"); } while (0)

    void LogClocks() {
        if (R_FAILED(clkrstInitialize())) { LogLine("horloges : service clkrst inaccessible"); return; }
        const struct { PcvModuleId id; const char *name; } kClk[] = {
            { PcvModuleId_GPU, "GPU" }, { PcvModuleId_CpuBus, "CPU" }, { PcvModuleId_EMC, "RAM" } };
        for (const auto &c : kClk) {
            ClkrstSession s; u32 hz = 0;
            Result rc = clkrstOpenSession(&s, c.id, 3);
            if (R_SUCCEEDED(rc)) { rc = clkrstGetClockRate(&s, &hz); clkrstCloseSession(&s); }
            if (R_SUCCEEDED(rc)) LogLine("horloge %s : %u MHz", c.name, hz / 1000000u);
            else                 LogLine("horloge %s : illisible (0x%x)", c.name, rc);
        }
        clkrstExit();
    }

    // The detailed log (every 10 s in perf.log): heap, process memory, GPU image pool, clocks, temperature, battery.
    bool g_psmOk = false, g_tsOk = false;
    extern "C" { extern char *fake_heap_start, *fake_heap_end; }
    size_t g_heapPeak = 0;
    float g_consSum[8] = {}; int g_consN = 0;
    // v99 (perf): the system's numbers are asked by a short thread (Worker.hpp), never in the frame - perf.log: every 10 s
    // one image of ~21 ms (« pire 20.5-21.3 », 1 late image in each « [roue] » window): the clock, temperature and memory
    // services opened, asked and closed inside the frame. The frame only writes the answer down when it is there.
    struct SysInfo { size_t used, arena; u64 pUsed, pTotal; u32 hz[3]; bool clk; float soc, pcb; u32 bat; };
    SysInfo g_sys;
    Worker  g_sysWork;
    char    g_sysWhen[16];
    void SysJob(void *) {
        SysInfo &i = g_sys;
        const struct mallinfo mi = mallinfo();
        i.used = (size_t)mi.uordblks; i.arena = (size_t)mi.arena;
        i.pUsed = i.pTotal = 0;
        svcGetInfo(&i.pUsed, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&i.pTotal, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        i.clk = false; i.hz[0] = i.hz[1] = i.hz[2] = 0;
        if (R_SUCCEEDED(clkrstInitialize())) {
            const PcvModuleId ids[3] = { PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC };
            for (int k = 0; k < 3; k++) { ClkrstSession s; if (R_SUCCEEDED(clkrstOpenSession(&s, ids[k], 3))) { clkrstGetClockRate(&s, &i.hz[k]); clkrstCloseSession(&s); } }
            clkrstExit();
            i.clk = true;
        }
        i.soc = i.pcb = -1000;
        if (g_tsOk) {
            TsSession s;
            if (R_SUCCEEDED(tsOpenSession(&s, TsDeviceCode_LocationInternal))) { tsSessionGetTemperature(&s, &i.soc); tsSessionClose(&s); }
            if (R_SUCCEEDED(tsOpenSession(&s, TsDeviceCode_LocationExternal))) { tsSessionGetTemperature(&s, &i.pcb); tsSessionClose(&s); }
        }
        i.bat = 0; if (g_psmOk) psmGetBatteryChargePercentage(&i.bat);
    }
    void LogSystemTake() {                               // (every frame: the answer, once it is there)
        if (!g_sysWork.Done()) return;
        const SysInfo &i = g_sys;
        const char *when = g_sysWhen;
        const size_t heapTotal = (size_t)((char *)fake_heap_end - (char *)fake_heap_start);
        if (i.used > g_heapPeak) g_heapPeak = i.used;
        const float MB = 1024.0f * 1024.0f;
        LogLine("[%s] tas : %.1f Mo utilises / %.1f Mo (pic %.1f, reserve %.1f) | processus : %.1f / %.1f Mo | images GPU : %.1f / 14 Mo",
                when, i.used / MB, heapTotal / MB, g_heapPeak / MB, i.arena / MB, i.pUsed / MB, i.pTotal / MB, g_gpu.ImgUsed() / MB);
        char clk[96] = "horloges ?";
        if (i.clk) snprintf(clk, sizeof clk, "CPU %u / GPU %u / RAM %u MHz", i.hz[0] / 1000000u, i.hz[1] / 1000000u, i.hz[2] / 1000000u);
        char tmp[48] = "temperature ?";
        if (g_tsOk) snprintf(tmp, sizeof tmp, "puce %.1f C, carte %.1f C", i.soc, i.pcb);   // lang:log (never drawn)
        LogLine("[%s] %s | %s | batterie %u %%", when, clk, tmp, i.bat);
        if (g_consN) {
            const float k = 1.0f / g_consN;
            LogLine("[%s] GPU par console (ms) : switch %.2f ps2 %.2f gc %.2f n64 %.2f ps1 %.2f psp %.2f gba %.2f ports %.2f", when,
                    g_consSum[0] * k, g_consSum[1] * k, g_consSum[2] * k, g_consSum[3] * k, g_consSum[4] * k, g_consSum[5] * k, g_consSum[6] * k, g_consSum[7] * k);
            for (float &v : g_consSum) v = 0;
            g_consN = 0;
        }
        FlushLog();
    }
    void LogSystem(const char *when) {
        if (!g_sysWork.Idle()) return;                   // (the last one still asking: this one is skipped)
        snprintf(g_sysWhen, sizeof g_sysWhen, "%s", when);
        g_sysWork.Start(SysJob, nullptr, 0x4000);
        LogSystemTake();                                 // (no thread could be made: it was asked at once)
    }

    float Rnd(u32 &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s & 0xFFFFFF) / 16777216.0f; }

    // v29 (Yass): the cubes, FULLY random at each boot - how many (3 to 6), how near or far (from just in front of
    // the camera to deep in the fog), how big; each on its own slow and wide path at its own pace, carried by the
    // carousel's wind the same way as the consoles, bumping SOFTLY into the consoles, the settings icons and each other
    // (springs and capped speeds: never a jolt). The finger can tap them (TapCubes). Camera space: x right, y up,
    // z forward, focal 900 px (glass_fsh); the wheel's camera has a 1045 px focal: kF scales x / y between the two.
    constexpr int   kMaxCubes = 6;
    constexpr float kCubeMinZ = 9.0f;     // v38 (Yass): the cubes stay BEHIND the carousels and the interface, always
    constexpr float kF = (360.0f / 0.344327613f) / 900.0f;      // tan(19 deg) = 0.3443
    struct Cube { float x, y, z, sz, ph, ax, ay, az, drag, kback, ampX, ampY, ampZ, fx, fy, fz, rotSpd; };
    struct CubeShock { float p[3], v[3], hitT, swirl, extraAngle, phi, w, bumpT; u32 touching, touchingNow; float loose; };   // contacts (bits)
    // v37 (Yass): a hard shake sends the cubes flying everywhere; the finger can grab a cube and throw it. A loose cube
    // (thrown, shaken) has a soft spring: it flies far, bumps the others (they ring), then glides home.
    int   g_grab = -1;                      // the cube under the finger
    float g_grabX = 0, g_grabY = 0;         // where the finger is (menu px)
    float g_shakeAt = -100;
    CubeShock g_shock[kMaxCubes] = {};
    Cube  g_cube[kMaxCubes];
    int   g_ncubes = 0;
    float g_cubeScr[kMaxCubes][3];     // screen x, y, radius (menu px): for the finger
    // v44 - the golden cube (Réglages > Brume, mockup setGold / catchGold): after 20 s on the wheel one cube turns to gold
    // and glows; Y or a touch catches it -> a little fanfare of bells, it spins, and a secret ambient colour unlocks
    bool  g_goldOn = false, g_goldDone = false;
    float g_goldK = 0, g_wheelSince = -1;
    int   GoldCube() { return g_ncubes > 2 ? 2 : 0; }
    float g_ptr[2] = { 0, 0 };         // parallax from the gyroscope / finger (-1 .. 1), smoothed (v29)
    bool  g_cubeOverlay = false;       // v29.1: some cube is in front of a console this frame (second glass pass)

    void MakeCubes() {
        u32 s = g_seed ^ 0x5bd1e995u;
        g_ncubes = 3 + (int)(Rnd(s) * 3.0f);                             // v29.1: 3 to 5 (FPS)
        if (g_ncubes > kMaxCubes) g_ncubes = kMaxCubes;
        for (int i = 0; i < g_ncubes; i++) {
            Cube &c = g_cube[i];
            for (int tries = 0; tries < 30; tries++) {
                // depth: one near the camera, one deep in the fog, the others anywhere (the eye gets a range to read)
                const float r = Rnd(s);
                c.z = i == 0 ? 9.5f + r * 2.0f : i == 1 ? 15.0f + r * 7.0f : 10.0f + r * 11.0f;   // v38: all behind the carousels (>= 9.5)
                const float hw = c.z * 640.0f / 900.0f, hh = c.z * 360.0f / 900.0f;
                c.x = (Rnd(s) * 2 - 1) * hw * 0.9f; c.y = (Rnd(s) * 2 - 1) * hh * 0.75f;
                const float sx = c.x / c.z * 900, sy = c.y / c.z * 900;
                if (fabsf(sx) < (c.z < 7 ? 340.0f : 240.0f) && sy > -240 && sy < 170) continue;   // never over the centre console
                bool ok = true;
                for (int j = 0; j < i && ok; j++) {
                    const float dx = (c.x / c.z - g_cube[j].x / g_cube[j].z) * 900, dy = (c.y / c.z - g_cube[j].y / g_cube[j].z) * 900;
                    ok = dx * dx + dy * dy > 150.0f * 150.0f;                // not on top of each other on screen
                }
                if (ok) break;
            }
            c.sz = fminf(0.32f + Rnd(s) * 0.45f, 0.075f * c.z);           // v29.2: at most ~115 px on screen (each cube pixel costs: FPS)
            c.ph = Rnd(s) * 6.2831853f;
            c.ax = Rnd(s) - 0.5f; c.ay = Rnd(s) - 0.5f; c.az = Rnd(s) - 0.5f;
            const float al = sqrtf(c.ax * c.ax + c.ay * c.ay + c.az * c.az) + 1e-4f;
            c.ax /= al; c.ay /= al; c.az /= al;
            c.drag = 0.3f + Rnd(s) * 0.6f; c.kback = 0.5f + Rnd(s) * 1.0f;
            const float amp = 0.30f + 0.09f * c.z;                       // wide slow paths (similar on screen near or far)
            c.ampX = amp * (0.6f + Rnd(s) * 0.8f); c.ampY = amp * (0.35f + Rnd(s) * 0.45f); c.ampZ = amp * (0.3f + Rnd(s) * 0.7f);
            c.fx = 0.05f + Rnd(s) * 0.07f; c.fy = 0.04f + Rnd(s) * 0.06f; c.fz = 0.03f + Rnd(s) * 0.05f;   // one loop in 1 to 3 min
            c.rotSpd = 0.06f + Rnd(s) * 0.14f;
        }
    }

    // v29: a cube rings like crystal when something touches it (pitch from its size: the big ones lower)
    void CubeChime(int i, float strength, float pan) {
        // v29.2 (Yass: the touch must fit the chord playing): a note of the chord sounding now; big cubes lower
        static int turn = 0;
        const bool big = g_cube[i].sz > 0.36f;
        audio::Crystal(pan, strength, audio::HarmonyNote(i + turn++, big ? 72 : 79, big ? 84 : 91));
        haptic::Glass(strength);                         // v55: glass under the finger
    }

    void FillScene(SceneUbo &u, float now, float dt, int w, int h) {
        memset(&u, 0, sizeof(u));
        u.screen[0] = (float)w; u.screen[1] = (float)h; u.screen[2] = now;
        u.haze[0] = 330.0f;            // cloud height (menu px)
        u.haze[1] = 0.30f + 6 * 0.09f; // strength (same as sLaunch « Haze » 6)
        u.haze[2] = now * 0.07f;       // light angle on the glass
        u.haze[3] = Settings::s_waveCur;   // v52: the XMB wave (Réglages > Brume)
        {   // v73 - the light on the icons' glass: from the top left, it drifts slowly (a turn of ~90 s, like the glints of
            // the cubes) and follows the console's tilt (the gyroscope, already smoothed): as if the light stayed in the room
            const float da = 0.22f * sinf(now * (6.2831853f / 90.0f));
            float lx = -0.55f * cosf(da) + 0.83f * sinf(da), ly = -0.55f * sinf(da) - 0.83f * cosf(da);
            lx -= 0.45f * g_ptr[0]; ly += 0.45f * g_ptr[1];
            const float l = sqrtf(lx * lx + ly * ly);
            u.glass[0] = l > 1e-4f ? lx / l : -0.6f; u.glass[1] = l > 1e-4f ? ly / l : -0.8f;
            u.glass[2] = 1.0f;                                   // the lens
            u.glass[3] = Settings::s_glassVis;                    // the glass appears with the page (Apple: the lens grows, no fade)
        }
        Settings::Ambient(u.ambient);  // v29: the ambient colour
        if (Settings::s_tintW > 0.001f)        // v39: the room takes the controller's colour (the Manettes page, a hello)
            for (int c = 0; c < 3; c++) u.ambient[c] += (Settings::s_tint[c] - u.ambient[c]) * Settings::s_tintW;
        // v53 (IDEES #14), v81: the battery is the fog, the sky by the hour, and the theme's whole look (Theme.cpp): it fills
        // the free halves of the block (uBO zw, uCubeFx w - see common.glsl)
        theme::Frame(now, dt, u.ambient, u.haze, u.bo, u.cubeFx, g_psmOk, g_wheel.WindDrift());
        if (Settings::s_powerDim > 0.001f) {   // v34: the power popup - the light goes down (it stays blue)
            const float pd = Settings::s_powerDim;
            for (int c = 0; c < 3; c++) u.ambient[c] *= 1 - 0.55f * pd;
        }
        g_cubeOverlay = false;
        if (!g_ncubes) MakeCubes();
        float sph[18][4]; int sphOwner[18];
        int nsph = g_wheel.Spheres(sph, 8);                    // the consoles, for the bumps (v28)
        for (int j = 0; j < nsph; j++) sphOwner[j] = -1;
        nsph += g_settings.Spheres(sph + nsph, sphOwner + nsph, 18 - nsph, now, g_wheel);   // + the settings icons
        const float omega = g_wheel.Spin() * (2 * 3.14159265f / 8);   // the carousel, rad/s
        float P[kMaxCubes][3];
        // 1. where each cube wants to be: its path, the wind, its shock offset, the parallax
        for (int i = 0; i < g_ncubes; i++) {
            const Cube &c = g_cube[i];
            CubeShock &k = g_shock[i];
            float cx = c.x + c.ampX * sinf(now * c.fx + c.ph), cy = c.y + c.ampY * sinf(now * c.fy + c.ph * 1.7f), cz = c.z + c.ampZ * sinf(now * c.fz + c.ph * 0.6f);
            cz = fmaxf(kCubeMinZ, cz);                                   // v38: never in front of the carousels
            {   // the wind: carried around the carousel's centre the same way as the consoles, by its own amount, then
                // back at its own pace (damped spring); the arc is capped (6 units) so the near ones don't fly off
                k.w += (omega * c.drag - k.w) * (1.0f - expf(-dt * 2.0f));
                k.w -= k.phi * c.kback * dt;
                k.phi += k.w * dt;
                const float zc = 12.6f, rx = cx, rz = zc - cz, r = sqrtf(rx * rx + rz * rz) + 1e-3f;
                const float a = k.phi * fminf(1.0f, 6.0f / r);
                cx = rx * cosf(a) - rz * sinf(a); cz = zc - (rz * cosf(a) + rx * sinf(a));
                cy += g_wheel.WindLift() * 0.3f * c.drag;
            }
            if (i == g_grab) {                                           // v37: held by the finger, at its own depth
                const float z = fmaxf(kCubeMinZ, cz + k.p[2]);
                const float wx = (g_grabX - 640) / 900.0f * z, wy = (360 - g_grabY) / 900.0f * z;
                const float nx = wx - (cx - g_ptr[0] * 0.8f * kF), ny = wy - (cy + g_ptr[1] * 0.4f * kF);
                if (dt > 0) { k.v[0] += ((nx - k.p[0]) / dt - k.v[0]) * 0.35f; k.v[1] += ((ny - k.p[1]) / dt - k.v[1]) * 0.35f; }
                k.p[0] += (nx - k.p[0]) * 0.55f; k.p[1] += (ny - k.p[1]) * 0.55f;   // (a soft follow: never a jump)
            }
            P[i][0] = cx + k.p[0] - g_ptr[0] * 0.8f * kF;               // parallax: the camera leans with the console
            P[i][1] = cy + k.p[1] + g_ptr[1] * 0.4f * kF;
            P[i][2] = fmaxf(kCubeMinZ, cz + k.p[2]);                     // (thrown, shaken: still behind)
        }
        // 2. soft bumps: consoles, settings icons, and the cubes between themselves (springs, never a jump)
        for (int i = 0; i < g_ncubes; i++) g_shock[i].touchingNow = 0;
        auto Bump = [&](int i, float nx, float ny, float nz, float over, float pan, u32 bit) {
            CubeShock &k = g_shock[i];
            const bool began = !(k.touching & bit);                      // v29.1: a new contact (not one that goes on)
            k.touchingNow |= bit;
            const float push = fminf(over, 0.25f);
            k.p[0] += nx * push * 0.5f; k.p[1] += ny * push * 0.5f; k.p[2] += nz * push * 0.5f;      // half of the way now
            const float vn = k.v[0] * nx + k.v[1] * ny + k.v[2] * nz;
            const float add = fminf(1.2f, over * 6.0f) - (vn < 0 ? vn * 1.2f : 0.0f);
            k.v[0] += nx * add; k.v[1] += ny * add; k.v[2] += nz * add;
            k.swirl = fminf(1.6f, k.swirl + over * 3.0f);
            const float spd = sqrtf(k.v[0] * k.v[0] + k.v[1] * k.v[1] + k.v[2] * k.v[2]);
            const float cap = k.loose > 0.1f ? 6.0f : 2.5f;
            if (spd > cap) { k.v[0] *= cap / spd; k.v[1] *= cap / spd; k.v[2] *= cap / spd; }
            if (began && now - k.bumpT > (k.loose > 0.1f ? 0.22f : 0.9f) && over > 0.02f) { k.bumpT = now; CubeChime(i, fminf(1.0f, over * 5.0f + 0.25f), pan); }
        };
        for (int i = 0; i < g_ncubes; i++) {
            const float sz = g_cube[i].sz;
            for (int j = 0; j < nsph; j++) {
                float dx = P[i][0] - sph[j][0], dy = P[i][1] - sph[j][1], dz = P[i][2] - sph[j][2];
                const float dd = sqrtf(dx * dx + dy * dy + dz * dz), rr = sz * 1.3f + sph[j][3];
                if (dd < rr * 1.5f && dd >= rr && dd > 1e-3f) {           // v29.1: a soft field before the contact (no resting on it)
                    const float f = (1 - (dd - rr) / (rr * 0.5f)) * 0.9f * dt / dd;
                    g_shock[i].v[0] += dx * f; g_shock[i].v[1] += dy * f; g_shock[i].v[2] += dz * f;
                }
                if (dd >= rr || dd < 1e-3f) continue;
                dx /= dd; dy /= dd; dz /= dd;
                Bump(i, dx, dy, dz, rr - dd, fmaxf(-1, fminf(1, P[i][0] / P[i][2] * 1.4f)), 1u << j);
                if (sphOwner[j] >= 0) g_settings.Kick(sphOwner[j], (dx >= 0 ? -1.0f : 1.0f) * fminf(1.5f, (rr - dd) * 8.0f));   // the icon wobbles
            }
            for (int j = i + 1; j < g_ncubes; j++) {
                float dx = P[i][0] - P[j][0], dy = P[i][1] - P[j][1], dz = P[i][2] - P[j][2];
                const float dd = sqrtf(dx * dx + dy * dy + dz * dz), rr = (sz + g_cube[j].sz) * 1.25f;
                if (dd >= rr || dd < 1e-3f) continue;
                dx /= dd; dy /= dd; dz /= dd;
                const float pan = fmaxf(-1, fminf(1, P[i][0] / P[i][2] * 1.4f));
                Bump(i, dx, dy, dz, (rr - dd) * 0.5f, pan, 1u << (20 + j)); Bump(j, -dx, -dy, -dz, (rr - dd) * 0.5f, pan, 1u << (20 + i));
            }
        }
        for (int i = 0; i < g_ncubes; i++) g_shock[i].touching = g_shock[i].touchingNow;
        // 3. the light fronts, the springs, the uniforms
        for (int i = 0; i < g_ncubes; i++) {
            const Cube &c = g_cube[i];
            CubeShock &k = g_shock[i];
            const float cx = P[i][0], cy = P[i][1], cz = P[i][2], sz = c.sz;
            const float hx = 640 + cx / cz * 900, hy = 360 - cy / cz * 900;
            for (int b = 0; b < g_lock.BlastCount(); b++) {
                Blast &bl = g_lock.BlastAt(b);
                if (bl.type == 2 || (bl.hit & (8u << i))) continue;
                const float d = hypotf(hx - bl.ox, hy - bl.oy), R = g_lock.StateAt(b).R;
                if (bl.type == 1 ? R >= d : R <= d) {
                    if (bl.chime && bl.type == 1) {   // mockup: the cube rings when the light touches it (n-th cube: ch[(2+n)%4]+24)
                        const int n = __builtin_popcount((bl.hit >> 3) & 63u);
                        audio::Crystal((hx / 640.0f - 1.0f) * 0.8f, 0.55f, audio::HarmonyNote(2 + n, 74, 90));   // v29.2: a note of the chord now
                    }
                    bl.hit |= 8u << i;
                    float dx = hx - bl.ox, dy = hy - bl.oy, L = hypotf(dx, dy); if (L < 1) L = 1;
                    const float depth = fminf(1.6f, sqrtf(cx * cx + cy * cy + cz * cz) / 9.0f);
                    const float f = 2.4f * bl.amp / 1.6f * depth * (bl.type == 1 ? 1.0f : -0.4f);   // softer than before (Yass: no jolts)
                    k.v[0] += dx / L * f; k.v[1] += -dy / L * f;
                    k.swirl += 0.6f * bl.amp;
                    if (bl.type == 1) k.hitT = now;
                }
            }
            if (i != g_grab) {
                const float stiff = 18.0f * (1 - 0.93f * k.loose), damp = 3.2f * (1 - 0.55f * k.loose);
                const float pl = sqrtf(k.p[0] * k.p[0] + k.p[1] * k.p[1] + k.p[2] * k.p[2]);
                const float wall = pl > 4.5f ? (pl - 4.5f) * 6.0f / pl : 0.0f;   // a soft wall: they never leave for good
                for (int q = 0; q < 3; q++) { k.v[q] += k.p[q] * (-(stiff + wall) * dt); k.v[q] *= fmaxf(0.0f, 1 - damp * dt); k.p[q] += k.v[q] * dt; }
                k.loose = fmaxf(0.0f, k.loose - dt * 0.3f);
            }
            k.swirl = fmaxf(0.0f, k.swirl - dt * 0.6f);
            k.extraAngle += k.swirl * dt;
            const float ht = now - k.hitT;
            const float bump = k.hitT <= 0 || ht < 0 ? 0 : (ht < 0.08f ? ht / 0.08f : expf(-(ht - 0.08f) / 0.4f));
            const float a = (now * c.rotSpd + c.ph + k.extraAngle) * 0.5f, sa = sinf(a);
            const float gold = i == GoldCube() ? g_goldK : 0.0f;
            u.cube[i][0] = cx; u.cube[i][1] = cy; u.cube[i][2] = cz; u.cube[i][3] = sz * (1 + 0.06f * gold * sinf(now * 3.0f));
            u.cubeFx[i][2] = gold;
            u.rot[i][0] = c.ax * sa; u.rot[i][1] = c.ay * sa; u.rot[i][2] = c.az * sa; u.rot[i][3] = cosf(a);
            u.cubeFx[i][0] = bump;
            g_cubeScr[i][0] = hx; g_cubeScr[i][1] = hy; g_cubeScr[i][2] = sz * 1.5f / cz * 900;
            bool front = false;                                          // v29.1: only these get the second glass pass
            for (int j = 0; j < nsph && !front; j++) {
                if (sph[j][2] < 0.3f) continue;
                const float sx = 640 + sph[j][0] / sph[j][2] * 900, sy = 360 - sph[j][1] / sph[j][2] * 900, sr = sph[j][3] * 1.2f / sph[j][2] * 900;
                const float d = hypotf(hx - sx, hy - sy), cr = sz * 1.8f / cz * 900;
                front = d < sr + cr && cz < sph[j][2] - sph[j][3] * 0.3f;      // v29.2: really in front of it (pass 2 only then)
            }
            u.cubeFx[i][1] = front ? 1.0f : 0.0f;
            g_cubeOverlay = g_cubeOverlay || front;
        }
        // the lock screen's light
        u.light[0] = g_lock.LightBase(); u.light[1] = Lock::kEdge; u.light[2] = Lock::kFrontW; u.light[3] = Lock::kFullR;
        u.origin[0] = Lock::kOx; u.origin[1] = Lock::kOy; u.origin[2] = g_lock.Flash(); u.origin[3] = g_lock.Wake();
        for (int b = 0; b < g_lock.BlastCount() && b < 4; b++) {
            const Blast &bl = g_lock.BlastAt(b); const BlastState &st = g_lock.StateAt(b);
            u.b[b][0] = st.R; u.b[b][1] = st.to; u.b[b][2] = st.a; u.b[b][3] = (float)bl.type;
            u.bo[b][0] = bl.ox; u.bo[b][1] = bl.oy;
        }
    }

    // Local time for the clock and the date (the loader started the time service).
    void LocalTime(int &hour, int &minute, int &wday, int &day, int &month) {
        // v90: two requests to the time service every frame for a clock that shows minutes: asked twice a second at most
        static u64 s_at = 0; static int s_v[5] = { 0, 0, -1, 0, 0 };
        const u64 tk = armGetSystemTick();
        if (s_at && tk >= s_at && tk - s_at < armGetSystemTickFreq() / 2) {
            hour = s_v[0]; minute = s_v[1]; wday = s_v[2]; day = s_v[3]; month = s_v[4];
            return;
        }
        s_at = tk;
        struct Keep { int &h, &m, &w, &d, &mo; ~Keep() { s_v[0] = h; s_v[1] = m; s_v[2] = w; s_v[3] = d; s_v[4] = mo; } } keep{ hour, minute, wday, day, month };
        hour = minute = day = month = 0; wday = -1;
        u64 ts = 0;
        TimeCalendarTime cal; TimeCalendarAdditionalInfo info;
        if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &ts))) return;   // v73.1: the user clock (what the console shows; with « via Internet » it follows the network clock)
        if (R_FAILED(timeToCalendarTimeWithMyRule(ts, &cal, &info))) return;
        hour = cal.hour; minute = cal.minute; day = cal.day; month = cal.month; wday = (int)info.wday;
    }

    // Emergency exit while Brume cannot launch games yet: hold L + R + Minus for
    // 3 s -> menu.ini is removed and the console reboots on sLaunch (still on the SD).
    u64 g_escapeSince = 0;
    // v85: the exit's files, shared by the emergency exit and Réglages > Système > « Revenir au menu d'origine » (true: done)
    bool LeaveFiles(const char *who) {
        // v48: with sLaunch on the card, back to it (menu.ini set aside); on a fresh install (Brume only), back to the
        // Nintendo HOME menu: the loader's folder is renamed (atmosphere/contents/0100000000001000 -> ..._off)
        struct stat st;
        bool done = true;
        if (stat("sdmc:/qlaunch-ext/menus/menu.dnro", &st) == 0) {
            LogLine("%s : retour a sLaunch", who);
            // v85: kept as menu.ini.off (coming back = renaming it, said by the settings' popup); removed if it cannot move
            char off[64]; snprintf(off, sizeof off, "%s.off", kMenuIni);
            remove(off);
            if (rename(kMenuIni, off) != 0) remove(kMenuIni);
            done = stat(kMenuIni, &st) != 0;
            LogLine("%s : menu.ini %s", who, done ? "mis de cote (menu.ini.off)" : "IMPOSSIBLE a retirer");
        } else {
            LogLine("%s : retour au menu Nintendo", who);
            bool moved = rename("sdmc:/atmosphere/contents/0100000000001000", "sdmc:/atmosphere/contents/0100000000001000_off") == 0;
            for (int k = 2; k < 10 && !moved; k++) {         // v56 (review): ..._off already there -> ..._off2, _off3...
                char to[80]; snprintf(to, sizeof to, "sdmc:/atmosphere/contents/0100000000001000_off%d", k);
                moved = rename("sdmc:/atmosphere/contents/0100000000001000", to) == 0;
            }
            done = moved;
            LogLine("%s : dossier du loader %s", who, moved ? "renomme" : "IMPOSSIBLE a renommer");
        }
        {   // v69: Brume Overlay (test) off too - the Nintendo overlay comes back with everything else
            bool off = stat("sdmc:/atmosphere/contents/010000000000100C", &st) != 0;
            for (int k = 0; k < 10 && !off; k++) {
                char to[80]; snprintf(to, sizeof to, k ? "sdmc:/atmosphere/contents/010000000000100C_off%d" : "sdmc:/atmosphere/contents/010000000000100C_off", k);
                off = rename("sdmc:/atmosphere/contents/010000000000100C", to) == 0;
            }
            LogLine("%s : overlay Brume %s", who, off ? "retire (ou absent)" : "IMPOSSIBLE a retirer");
        }
        FlushLogNow();
        return done;
    }
    void EmergencyExit(u64 held, u64 now) {
        const u64 combo = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Minus;
        if ((held & combo) != combo) { g_escapeSince = 0; return; }
        if (!g_escapeSince) { g_escapeSince = now; return; }
        if (Seconds(now - g_escapeSince) < 3.0f) return;
        LeaveFiles("sortie de secours");   // lang:log (the log line's tag)
        g_gpu.WaitIdle();
        appletStartRebootSequence();
        g_escapeSince = 0;
    }

    // ---- v29: the toy (Yass: « we use everything the console has ») - the gyroscope and the touch screen ----
    // Gyroscope: the console's tilt read from gravity (acceleration, in G), minus its slow average: however it is held,
    // at rest the view comes back to the middle; tilting it moves the camera a little (parallax: the near cubes slide
    // more than the far ones - the eye reads the depth).
    HidSixAxisSensorHandle g_six[3];
    bool  g_sixOk = false, g_accInit = false;
    float g_accAvg[2] = { 0, 0 };
    void ToyInit() {
        const bool h = R_SUCCEEDED(hidGetSixAxisSensorHandles(&g_six[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld));
        const bool d = R_SUCCEEDED(hidGetSixAxisSensorHandles(&g_six[1], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
        if (h) hidStartSixAxisSensor(g_six[0]);
        if (d) { hidStartSixAxisSensor(g_six[1]); hidStartSixAxisSensor(g_six[2]); }
        g_sixOk = h || d;
        hidInitializeTouchScreen();
    }
    void CubesBurst(float strength, const float *dir, float now);
    void Gyro(float dt, float now) {
        float tx = 0, ty = 0;
        if (g_sixOk) {
            HidSixAxisSensorState st = {};
            const bool handheld = (padGetStyleSet(&g_pad) & HidNpadStyleTag_NpadHandheld) != 0;
            if (hidGetSixAxisSensorStates(g_six[handheld ? 0 : 2], &st, 1) > 0) {
                const float ax = st.acceleration.x, az = st.acceleration.z;
                if (!g_accInit) { g_accAvg[0] = ax; g_accAvg[1] = az; g_accInit = true; }
                const float k = 1.0f - expf(-dt / 2.5f);                  // the slow average (2.5 s)
                g_accAvg[0] += (ax - g_accAvg[0]) * k; g_accAvg[1] += (az - g_accAvg[1]) * k;
                auto Dead = [](float v) {                                // v29.1: nothing under ~8 deg, then smooth
                    const float a = fabsf(v), d = 0.14f;
                    return a <= d ? 0.0f : (v < 0 ? -1.0f : 1.0f) * fminf(1.0f, (a - d) * 2.6f);
                };
                tx = Dead(ax - g_accAvg[0]);
                ty = Dead(az - g_accAvg[1]);
                // v37: a hard shake (the acceleration jumps by more than ~1.2 G against its slow average): the cubes fly
                static float avg[3] = { 0, 0, 0 };
                static bool init = false;
                const float a3[3] = { st.acceleration.x, st.acceleration.y, st.acceleration.z };
                if (!init) { for (int q = 0; q < 3; q++) avg[q] = a3[q]; init = true; }
                float hp[3], mag = 0;
                for (int q = 0; q < 3; q++) { hp[q] = a3[q] - avg[q]; mag += hp[q] * hp[q]; avg[q] += (a3[q] - avg[q]) * (1.0f - expf(-dt / 0.6f)); }
                mag = sqrtf(mag);
                if (mag > 1.2f && now - g_shakeAt > 0.8f && g_wheel.Active()) {
                    g_shakeAt = now;
                    const float d[3] = { -hp[0] / mag, -hp[2] / mag, hp[1] / mag };   // the cubes lag behind the move
                    CubesBurst(fminf(1.0f, 0.45f + (mag - 1.2f) / 1.5f), d, now);
                }
            }
        }
        const float k = 1.0f - expf(-dt * 2.2f);                         // v29.1: a slower, calmer follow
        g_ptr[0] += (tx - g_ptr[0]) * k; g_ptr[1] += (ty - g_ptr[1]) * k;
        g_wheel.SetPtr(g_ptr[0], g_ptr[1]);
    }

    // v37 - the burst: every cube thrown its own way (and a little against the shake), spinning, lit; a shower of crystal
    // notes of the chord, a breath, a low thump, and the motors
    void CubesBurst(float strength, const float *dir, float now) {
        u32 r = (u32)(now * 997.0f) | 1u;
        for (int i = 0; i < g_ncubes; i++) {
            CubeShock &k = g_shock[i];
            float d[3] = { Rnd(r) * 2 - 1, Rnd(r) * 2 - 1, (Rnd(r) * 2 - 1) * 0.6f };
            for (int q = 0; q < 3; q++) d[q] = d[q] * 0.75f + dir[q] * 0.5f;
            const float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-4f, sp = (3.5f + 4.0f * strength) * (0.7f + 0.6f * Rnd(r));
            for (int q = 0; q < 3; q++) k.v[q] += d[q] / l * sp;
            k.loose = 1.0f; k.swirl = fminf(3.0f, k.swirl + 2.0f + strength); k.hitT = now; k.bumpT = now - 1;
        }
        for (int n = 0; n < 4; n++) audio::Bell(audio::HarmonyNote(n, 79, 96), 0.05f + 0.02f * strength, true, n * 0.06f + Rnd(r) * 0.03f);
        audio::Crystal((Rnd(r) * 2 - 1) * 0.6f, 0.7f, audio::HarmonyNote(0, 84, 96));
        audio::PlaySweep(audio::Whoosh, -0.6f, 0.6f, 0.7f, 0.5f + 0.3f * strength);
        audio::SoftLow(audio::HarmonyNote(0, 36, 47), 0.10f + 0.06f * strength, 0.6f);
        haptic::Thump(0.9f);
        haptic::Swell(0.6f * strength, 0.5f);
    }
    void CatchGold(float now) {
        if (!g_goldOn) return;
        g_goldDone = true; g_goldOn = false;
        CubeShock &k = g_shock[GoldCube()];
        k.swirl = 3.0f; k.hitT = now;
        for (int n = 0; n < 8; n++)                      // the little fanfare: the chord twice, an octave apart
            audio::Bell(audio::HarmonyNote(n & 3, n > 3 ? 86 : 74, n > 3 ? 100 : 88), 0.05f, true, n * 0.08f);
        haptic::Tap(0.9f); haptic::Swell(0.5f, 0.7f);
        Settings::UnlockSecret(now);
    }
    // v37 - a cube taken / thrown by the finger
    void GrabCube(int i, float x, float y) {
        if (i == GoldCube() && g_goldOn) { CatchGold(MenuNow()); }
        g_grab = i; g_grabX = x; g_grabY = y;
        CubeChime(i, 0.5f, fmaxf(-1.0f, fminf(1.0f, (x - 640) / 640.0f)));
        haptic::Tap(0.6f);
    }
    void ThrowCube(float vxPx, float vyPx, float now) {
        if (g_grab < 0) return;
        const int i = g_grab;
        g_grab = -1;
        CubeShock &k = g_shock[i];
        const float z = fmaxf(2.5f, g_cube[i].z + k.p[2]);
        float vx = vxPx / 900.0f * z, vy = -vyPx / 900.0f * z;
        const float sp = sqrtf(vx * vx + vy * vy);
        if (sp > 9.0f) { vx *= 9.0f / sp; vy *= 9.0f / sp; }
        k.v[0] = vx; k.v[1] = vy; k.v[2] += 0.6f;
        k.loose = sp > 0.6f ? 1.0f : 0.4f; k.swirl = fminf(3.0f, k.swirl + sp * 0.4f); k.bumpT = now - 1;
        if (sp > 0.6f) {
            const float pan = fmaxf(-1.0f, fminf(1.0f, (g_grabX - 640) / 640.0f));
            audio::PlaySweep(audio::Whoosh, pan, fmaxf(-1.0f, fminf(1.0f, pan + (vx > 0 ? 0.8f : -0.8f))), 0.5f, fminf(0.8f, 0.3f + sp * 0.06f));
            audio::Crystal(pan, 0.5f, audio::HarmonyNote(2, 79, 91));
            haptic::Thump(0.5f);
        }
    }
    // The finger rings a cube: pushed into the scene, it spins a little, lights up, rings like crystal, a click
    void TapCube(int i, float now) {
        CubeShock &k = g_shock[i];
        u32 r = (u32)(now * 1000.0f) | 1u;
        const float rx = Rnd(r) - 0.5f, ry = Rnd(r) - 0.5f;
        k.v[0] += rx * 1.2f; k.v[1] += ry * 1.2f; k.v[2] += 1.4f;
        k.swirl = fminf(2.0f, k.swirl + 1.2f); k.hitT = now; k.bumpT = now;
        CubeChime(i, 1.0f, fmaxf(-1.0f, fminf(1.0f, (g_cubeScr[i][0] - 640) / 640.0f)));
    }
    int CubeAt(float x, float y) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < g_ncubes; i++) {
            const float dx = x - g_cubeScr[i][0], dy = y - g_cubeScr[i][1], d = dx * dx + dy * dy, r = fmaxf(36.0f, g_cubeScr[i][2]);
            if (d < r * r && d < bd) { bd = d; best = i; }
        }
        return best;
    }
    // A tap (finger down and up without moving): the lock screen, a cube, the top bar, then the page
    void TouchTap(float x, float y, float now) {
        if (popup::Tap(x, y, now)) return;               // v78: a popup first (an answer; outside it = B)
        if (g_pads.TestOpen()) { g_pads.TestTap(x, y, now); return; }   // v84: the controllers' test (its hints)
        if (social::DiscordShown()) { social::DiscordClose(now); return; }   // v95: the Discord QR - a touch closes it
        if (!g_wheel.Active()) { if (!g_lock.Unlocked()) g_lock.Tap(x, y, now); return; }   // v87: the lock screen's steps (who plays, the code)
        if (g_system.PowerOpen()) { g_system.Tap(x, y, now); return; }   // v37: the power cubes first
        if (tasks::PanelTap(x, y, now)) return;          // v77: the task centre (open: its touches; outside: it closes)
        if (tasks::MarkHit(x, y)) { tasks::Toggle(now); return; }
        if (spotify::PanelTap(x, y, now)) return;        // v98: the Spotify panel (open: its touches; outside: it closes)
        if (spotify::BarHit(x, y)) { haptic::Tap(0.6f); spotify::OpenPanel(now); return; }
        const int c = CubeAt(x, y);
        if (c >= 0 && c == GoldCube() && g_goldOn) { CatchGold(now); TapCube(c, now); return; }   // v44
        if (c >= 0) { TapCube(c, now); return; }
        if (g_games.Active()) {                          // v31: a tap on a box chooses it (on the chosen one: launch), on the console: back
            const int b = g_games.BoxAt(x, y);
            if (b >= 0) g_games.Tap(b, now);
            else if (x < 380) g_games.Close(now);
            return;
        }
        if (g_system.PowerOpen()) { g_system.Tap(x, y, now); return; }   // v33: an icon = choose it, outside = close
        if (g_profiles.Open()) { g_profiles.Tap(x, y, now); return; }    // v46
        if (mypage::Open()) { mypage::Tap(x, y, now); return; }          // v96: the profile page
        if (charge::BarHit(x, y)) { charge::Tap(now); return; }          // v96: the battery - its details
        const int tab = g_wheel.TabAt(x, y);
        if (tab >= 0 && tab < Wheel::kPages) { g_wheel.SetPage(now, tab); audio::Play(audio::Tick2400, 0, 1.0f); haptic::Tick(0.8f); return; }
        if (tab == -1) { haptic::Tap(0.8f); mypage::Show(now); return; }   // v96: the profile page (v46: the profiles' panel)
        if (tab == Wheel::kTabs - 1) { haptic::Tap(0.8f); g_system.Open(5, now); return; }   // (5 = System's power)
        switch (g_wheel.Page()) {
            case 0:                                                    // the wheel: the middle opens, the sides turn it
                if (fabsf(x - 640) < 170 && y > 180 && y < 560) { g_wheel.PressA(now); g_games.Open(g_wheel.Sel(), now); }
                else g_wheel.Rotate(now, x < 640 ? -1 : 1);
                break;
            case Settings::kPage:
                g_settings.Tap(x, y, now);                     // v40: a tap outside no longer goes back to the games
                break;
            case Album::kPage:                                         // v34: a card = choose / open it
                g_album.Tap(x, y, now);
                break;
            case Apps::kPage:                                          // v35: a tile = choose / start it
                g_apps.Tap(x, y, now);
                break;
            case Pads::kPage:                                          // v39: the controller = it vibrates; the selector
                g_pads.Tap(x, y, now);
                break;
            case social::kPage:                                        // v95: a friend / a choice = choose it (the chosen one: A)
                social::Tap(x, y, now);
                break;
        }
    }
    // v29.1 (Yass) - the finger holds what it touches and moves it to the millimetre; when it lets go, it settles on the
    // grid (the nearest console / category / page / setting): never stopped in the void, no momentum that sends it far.
    //   mode 1: the wheel (480 px = one console), 2: the settings ring (430 px), 3: the settings list (60 px = one row),
    //   4: the pages (520 px = one page); 0: taps only (the top bar, a cube, a panel)
    //   v36: 5 the games' boxes (130 px = one box), 6 the chosen console in the games screen (the finger turns it),
    //   7 the apps' ring (300 px = one cube); a carousel let go with speed is thrown (it settles on the next element)
    struct TouchState { bool down = false, drag = false; int mode = 0; float x0 = 0, y0 = 0, x = 0, y = 0, t0 = 0, lx = 0, ly = 0, lt = 0, vx = 0, vy = 0; };
    TouchState g_tc;
    int TouchMode(float x, float y) {
        if (popup::Shown() > 0.02f || g_pads.TestOpen()) return 0;   // v78: a popup takes the finger (taps only); v84: the test too
        if (g_wheel.Active() && !g_system.PowerOpen() && y >= 92 && CubeAt(x, y) >= 0) return 8;   // v37: a cube: grab it
        if (g_wheel.Active() && g_games.Active() && y >= 92 && CubeAt(x, y) < 0) return x >= 380 ? 5 : 6;   // v31: the boxes; v36: the console
        if (!g_wheel.Active() || g_system.PowerOpen() || g_games.Active() || y < 92 || CubeAt(x, y) >= 0) return 0;
        switch (g_wheel.Page()) {
            case 0: return 1;
            case Apps::kPage: return g_apps.Launching() ? 0 : 7;
            case Album::kPage: return g_album.Viewing() ? 0 : 9;   // v37: the flow under the finger
            case Settings::kPage: return g_settings.Open() ? (x >= 470 ? 3 : 0) : 2;
            default: return 4;
        }
    }
    void Touch(float now) {
        HidTouchScreenState st = {};
        const bool has = hidGetTouchScreenStates(&st, 1) > 0 && st.count > 0;
        if (has) {
            const float x = (float)st.touches[0].x, y = (float)st.touches[0].y;
            if (!g_tc.down) { g_tc.down = true; g_tc.drag = false; g_tc.x0 = g_tc.x = g_tc.lx = x; g_tc.y0 = g_tc.y = g_tc.ly = y; g_tc.t0 = g_tc.lt = now; g_tc.vx = g_tc.vy = 0; g_tc.mode = TouchMode(x, y); return; }
            g_tc.x = x; g_tc.y = y;
            if (now - g_tc.lt > 0.004f) {                  // v36: the finger's speed (px/s, smoothed): for the throws
                const float inst = (x - g_tc.lx) / (now - g_tc.lt), insty = (y - g_tc.ly) / (now - g_tc.lt);
                g_tc.vx += (inst - g_tc.vx) * 0.45f; g_tc.vy += (insty - g_tc.vy) * 0.45f; g_tc.lx = x; g_tc.ly = y; g_tc.lt = now;
            }
            const float dx = x - g_tc.x0, dy = y - g_tc.y0;
            if (!g_tc.drag && g_tc.mode && hypotf(dx, dy) > 12) {
                g_tc.drag = true;
                if (g_tc.mode == 1) g_wheel.DragBegin(now);
                else if (g_tc.mode == 2) g_settings.RingDragBegin();
                else if (g_tc.mode == 3) g_settings.ListDragBegin(g_tc.x0, g_tc.y0);   // (v83: which wheel of the date and time)
                else if (g_tc.mode == 5) g_games.DragBegin();
                else if (g_tc.mode == 6) {}
                else if (g_tc.mode == 7) g_apps.RingDragBegin();
                else if (g_tc.mode == 8) { const int c = CubeAt(g_tc.x0, g_tc.y0); if (c >= 0) GrabCube(c, x, y); }
                else if (g_tc.mode == 9) g_album.DragBegin();
                else g_wheel.PageDragBegin();
            }
            if (g_tc.drag) {
                if (g_tc.mode == 1) g_wheel.DragTo(now, -dx / 480.0f);
                else if (g_tc.mode == 2) g_settings.RingDragTo(now, -dx / 430.0f);
                else if (g_tc.mode == 3) g_settings.ListDragTo(-dy / 60.0f);
                else if (g_tc.mode == 5) g_games.DragTo(-dx / 130.0f, now);   // v31: one box per 130 px
                else if (g_tc.mode == 6) g_wheel.SetTouchTilt(fmaxf(-2.4f, fminf(2.4f, dx / 150.0f)), fmaxf(-1.2f, fminf(1.2f, -dy / 220.0f)));   // v36.2: the side under the finger follows it (right = it turns right; down = its top comes to us)
                else if (g_tc.mode == 7) g_apps.RingDragTo(now, -dx / 300.0f);
                else if (g_tc.mode == 8) { g_grabX = x; g_grabY = y; }
                else if (g_tc.mode == 9) g_album.DragTo(now, -dx / 260.0f);
                else g_wheel.PageDragTo(-dx / 520.0f);
            }
        } else if (g_tc.down) {
            g_tc.down = false;
            if (g_tc.drag) {
                const float vx = now - g_tc.lt > 0.08f ? 0.0f : g_tc.vx;   // (a finger that stopped before letting go: no throw)
                if (g_tc.mode == 1) g_wheel.DragEnd(now, -vx / 480.0f);
                else if (g_tc.mode == 2) g_settings.RingDragEnd(now, -vx / 430.0f);
                else if (g_tc.mode == 4) g_wheel.PageDragEnd(now);
                else if (g_tc.mode == 5) g_games.DragEnd(now, -vx / 130.0f);
                else if (g_tc.mode == 6) g_wheel.SetTouchTilt(0, 0);      // let go: the console glides back
                else if (g_tc.mode == 7) g_apps.RingDragEnd(now, -vx / 300.0f);
                else if (g_tc.mode == 8) ThrowCube(vx, now - g_tc.lt > 0.08f ? 0.0f : g_tc.vy, now);
                else if (g_tc.mode == 9) g_album.DragEnd(now, -vx / 260.0f);
            } else if (now - g_tc.t0 < 0.5f && hypotf(g_tc.x - g_tc.x0, g_tc.y - g_tc.y0) <= 12) TouchTap(g_tc.x0, g_tc.y0, now);
        }
    }

    // v45 - the clock's font (Réglages > Brume > Police de l'heure): Lexend Giga lives in the atlas; the others are
    // sdmc:/brume/polices/<id>-<scale>.bfnt, uploaded over the clock's band of each atlas - nothing more stays in memory
    const char *const kClockIds[7] = { nullptr, "michroma", "bigshoulders", "instrument", "orbitron", "majormono", "monoton" };
    fonts::Glyph g_clockGlyphs[2][2][16];
    bool LoadClockBand(int idx, int si) {
        const fonts::Atlas &k = si ? fonts::kAtlas1 : fonts::kAtlas0;
        const uint32_t H = (uint32_t)fonts::kClockRegionH[si];
        char path[64];
        snprintf(path, sizeof path, "sdmc:/brume/polices/%s-%d.bfnt", kClockIds[idx], si);
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        bool ok = false;
        uint8_t *px = nullptr;
        do {
            char magic[4]; uint32_t ver = 0, w = 0, h = 0;
            if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "BFNT", 4) || fread(&ver, 4, 1, f) != 1 || fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1) break;
            if (ver != 1 || w != (uint32_t)k.w || h != H) break;
            uint32_t cnt[2]; float met[2][3];
            bool hd = true;
            for (int s = 0; s < 2 && hd; s++) hd = fread(&cnt[s], 4, 1, f) == 1 && fread(met[s], 4, 3, f) == 3 && cnt[s] >= 1 && cnt[s] <= 16;
            if (!hd) break;
            bool gl = true;
            for (int s = 0; s < 2 && gl; s++)
                for (uint32_t i = 0; i < cnt[s] && gl; i++) {
                    fonts::Glyph &g = g_clockGlyphs[si][s][i];
                    uint16_t r[4]; float m[3];
                    gl = fread(&g.cp, 4, 1, f) == 1 && fread(r, 2, 4, f) == 4 && fread(m, 4, 3, f) == 3 && r[0] + r[2] <= w && r[1] + r[3] <= h;
                    g.x = r[0]; g.y = r[1]; g.w = r[2]; g.h = r[3]; g.bx = m[0]; g.by = m[1]; g.adv = m[2];
                }
            if (!gl) break;
            px = (uint8_t *)malloc((size_t)w * h);
            if (!px || fread(px, 1, (size_t)w * h, f) != (size_t)w * h) break;
            if (!g_gpu.UploadRowsNow(g_atlas[si], px, 0, h)) break;
            const fonts::Face c = { g_clockGlyphs[si][0], (int)cnt[0], met[0][0], met[0][1], met[0][2] };
            const fonts::Face gw = { g_clockGlyphs[si][1], (int)cnt[1], met[1][0], met[1][1], met[1][2] };
            Ui::SetClockFaces(si, c, gw);
            ok = true;
        } while (false);
        free(px);
        fclose(f);
        return ok;
    }
    bool ApplyClockFont(int idx) {
        bool ok = idx > 0 && idx < 7;
        for (int si = 0; si < 2 && ok; si++) ok = LoadClockBand(idx, si);
        if (!ok) {                                       // Lexend Giga (also: a missing / damaged file puts it back)
            for (int si = 0; si < 2; si++) {
                Ui::ResetClockFaces(si);
                const fonts::Atlas &k = si ? fonts::kAtlas1 : fonts::kAtlas0;
                g_gpu.UploadRowsNow(g_atlas[si], k.pixels, 0, (uint32_t)fonts::kClockRegionH[si]);
            }
        }
        LogLine("heure : police %d %s", idx, ok ? "chargee" : (idx ? "introuvable (Lexend Giga)" : "Lexend Giga"));
        return ok || idx == 0;
    }

    void Frame() {
        if (Settings::s_clockFontDirty) {                // v45: before anything is recorded for this frame
            Settings::s_clockFontDirty = false;
            if (!ApplyClockFont(Settings::s_clockFont)) {
                Settings::SetClockFont(0);
                g_system.Status(MenuNow(), "Cette police n'est pas sur la carte SD (brume/polices/)");
            }
        }
        const u64 start = armGetSystemTick();
        // v90: time spent outside Brume between two images (the loader), written down when long (not after a game / sleep)
        if (g_lastTick && g_loopEnd && start > g_loopEnd && Seconds(start - g_loopEnd) > 0.025f && g_slowLines < 300) {
            g_slowLines++;
            LogLine("lent : hors de Brume (le loader) %.1f ms (t=%.1fs)", Seconds(start - g_loopEnd) * 1000.0f, MenuNow());
        }
        if (g_clockTick && start > g_clockTick) {
            const double step = (double)(start - g_clockTick) / (double)armGetSystemTickFreq();
            double inc = step < 0.25 ? step : 1.0 / 60.0;        // v68 (review): a blocking page (profile picker, keyboard...) = a pause
            if (MenuNow() < g_calmAt && step > 1.0 / 30.0) inc = 1.0 / 30.0;   // v80: a slow frame of the way back slows the staging, never jumps it
            g_menuTime += inc;
        } else g_menuTime += 1.0 / 60.0;                       // back from a game or from sleep: the clock goes on from there
        g_clockTick = start;
        const float now = MenuNow();
        static float last = 0;
        const float dt = fminf(0.05f, fmaxf(0.0f, now - last));
        last = now;
        static int modeCheck = 0;
        if (++modeCheck >= 30) { modeCheck = 0; g_gpu.CheckMode(); }

        Pads::Poll(g_pad);                               // v92: every controller (players 1-8, the console), live (Pads.hpp)
        u64 down = padGetButtonsDown(&g_pad);
        const u64 held = padGetButtons(&g_pad);
        EmergencyExit(held, start);
        STEP(26, "Input", Gyro(dt, now); Touch(now));          // v29: the toy - the tilt, the finger
        if ((down & HidNpadButton_A) && g_wheel.Active() && g_wheel.Page() == 0) theme::PressA(now);   // v81: Chrome - the liquid ripples (the consoles)
        if (popup::Open()) {                             // v78: a popup takes every button (nothing behind it moves)
            if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_Up | HidNpadButton_StickLUp)) popup::Move(-1, now);
            if (down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_Down | HidNpadButton_StickLDown)) popup::Move(1, now);
            if (down & HidNpadButton_A) popup::PressA(now);
            if (down & HidNpadButton_B) popup::PressB(now);
            down = 0;
        }
        if (g_settings.CodeOpen() && g_wheel.Active()) { g_settings.CodeInput(now, down); down = 0; }   // v87: the lock code's panel takes every button
        if (social::DiscordShown()) {                    // v95: the Discord QR takes every button (A / B close it)
            if (down & (HidNpadButton_A | HidNpadButton_B)) social::DiscordClose(now);
            down = 0;
        }
        if (social::Launching()) down &= ~(u64)(HidNpadButton_A | HidNpadButton_L | HidNpadButton_R);   // v95: « Rejoindre » runs
        Pads::s_held = held;                             // v84: the controllers' test reads each controller itself (every button
        if (g_pads.TestOpen()) down = 0;                 // is under test: nothing else moves; hold B to leave)
        if (g_wheel.Active() && !popup::Open()) {        // v98: Spotify - ZL opens / closes its panel; open, it takes every button
            if (spotify::PanelOpen()) { spotify::Input(down, now); down = 0; }
            else if ((down & HidNpadButton_ZL) && !(held & HidNpadButton_ZR) && !g_settings.CodeOpen()) { tasks::Close(now); spotify::OpenPanel(now); down &= ~(u64)HidNpadButton_ZL; }
        }
        if (g_wheel.Active()) {                          // v77: the task centre - ZR opens / closes it, B closes it
            if ((down & HidNpadButton_ZR) && !(held & HidNpadButton_ZL)) { tasks::Toggle(now); down &= ~(u64)HidNpadButton_ZR; }
            if (tasks::PanelOpen() && (down & HidNpadButton_B)) { tasks::Close(now); down &= ~(u64)HidNpadButton_B; }
        }
        if (g_wheel.Active() && g_games.Active()) {   // the games of the chosen console
            if (down & HidNpadButton_A) g_games.PressA(now);
            if (down & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_Left | HidNpadButton_StickLLeft)) g_games.Move(-1, now);
            if (down & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_Right | HidNpadButton_StickLRight)) g_games.Move(1, now);   // v30: the cards go sideways
            if (down & HidNpadButton_X) g_games.PressX(now);            // v67: « Gérer »
            if (down & HidNpadButton_Y) g_games.PressY(now);
            if (down & HidNpadButton_Minus) g_games.PressMinus(now);
            if (down & HidNpadButton_B) {
                if (g_manageBack && g_games.Managing()) {   // v79: « Gérer » came from the settings - B goes straight back there
                    g_games.Close(now);                     // (the boxes leave, the Switch shrinks away, the settings come back)
                    if (!g_games.Active()) { g_manageBack = false; g_wheel.Warp(now, Settings::kPage, 0.42f); }
                } else if (!g_games.PressB(now)) g_games.Close(now);
            }
        } else if (g_wheel.Active() && g_pads.TestOpen()) {      // v84: the controllers' test (Pads reads the controllers)
            g_wheel.SetStick(0, 0);
        } else if (g_wheel.Active() && mypage::Open()) {         // v96: the profile page (▲ ▼ choose, ◀ ▶ a value, A, B, Y)
            if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) mypage::Move(-1, now);
            if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) mypage::Move(1, now);
            if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) mypage::Side(-1, now);
            if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) mypage::Side(1, now);
            if (down & HidNpadButton_A) mypage::PressA(now);
            if (down & HidNpadButton_B) mypage::PressB(now);
            if (down & HidNpadButton_Y) mypage::PressY(now);
        } else if (g_wheel.Active() && g_profiles.Open()) {      // v46: the profiles' panel
            if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) g_profiles.Move(-1, now);
            if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) g_profiles.Move(1, now);
            if (down & HidNpadButton_A) STEP(40, "Profiles::PressA", g_profiles.PressA(now));   // v99: timed (a Switch screen blocks the frame)
            if (down & HidNpadButton_X) g_profiles.PressX(now);
            if (down & HidNpadButton_Y) STEP(41, "Profiles::PressY", g_profiles.PressY(now));
            if (down & HidNpadButton_Minus) STEP(42, "Profiles::PressMinus", g_profiles.PressMinus(now));
            if (down & HidNpadButton_B) g_profiles.Close(now);
        } else if (g_wheel.Active() && g_system.PowerOpen()) {   // Alimentation: Veille / Redémarrer / Éteindre
            if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_Up | HidNpadButton_StickLUp)) g_system.PowerMove(-1, now);
            if (down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_Down | HidNpadButton_StickLDown)) g_system.PowerMove(1, now);
            if (down & HidNpadButton_A) g_system.PowerConfirm(now);
            if (down & HidNpadButton_B) g_system.PowerClose(now);
        } else if (g_wheel.Active() && g_wheel.BarFocused()) {   // the top bar: profile + 6 tabs (5 pages + Alimentation)
            int t = g_wheel.TabOn();
            if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) {
                if (t > -1) { t--; audio::Play(audio::Deplacement, 0, 1.0f); } else audio::Play(audio::ThockSnd, 0, 0.5f);
            }
            if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) {
                if (t < Wheel::kTabs - 1) { t++; audio::Play(audio::Deplacement, 0, 1.0f); } else audio::Play(audio::ThockSnd, 0, 0.5f);
            }
            g_wheel.SetBar(true, t);
            if (down & HidNpadButton_A) {
                if (t >= 0 && t < Wheel::kPages) {          // v28: Jeux .. Réglages are pages now (the bar = the pages, mockup v24)
                    g_wheel.SetBar(false, 0); g_wheel.SetPage(now, t); audio::Play(audio::Tick2400, 0, 1.0f);
                } else if (t == -1) { g_wheel.SetBar(false, 0); mypage::Show(now); }   // v96: the profile page (v46: the panel)
                else g_system.Open(t == Wheel::kTabs - 1 ? 5 : t, now);   // Alimentation (our panel)
            }
            else if (down & (HidNpadButton_B | HidNpadButton_Down | HidNpadButton_StickLDown)) {
                g_wheel.SetBar(false, 0); audio::Play(audio::Tick1700, 0, 1.0f);
            }
        } else if (g_wheel.Active()) {
            {   // v28 - right stick: the chosen console turns to look where the stick points (Yass: see it from every side)
                const HidAnalogStickState r = padGetStickPos(&g_pad, 1);
                float sx = r.x / 32767.0f, sy = r.y / 32767.0f;
                const float len = sqrtf(sx * sx + sy * sy);
                if (len < 0.15f) sx = sy = 0;                       // dead zone
                else { const float k = (len - 0.15f) / (0.85f * len); sx *= k; sy *= k; }
                g_wheel.SetStick(fmaxf(-1, fminf(1, sx)), fmaxf(-1, fminf(1, sy)));
            }
            const bool settingsList = g_wheel.Page() == Settings::kPage && g_settings.Open();   // the list uses Up / Down itself
            if (down & HidNpadButton_Plus) { haptic::Tap(0.8f); g_system.Open(5, now); }   // v29: + = Alimentation (Yass: no more Up into the bar, L / R only)
            // v28 - L / R: the pages (mockup v24)
            if (down & HidNpadButton_L) g_wheel.SetPage(now, g_wheel.Page() - 1);
            if (down & HidNpadButton_R) g_wheel.SetPage(now, g_wheel.Page() + 1);
            if (g_wheel.Page() == 0) {                       // the wheel
                if (down & HidNpadButton_A) { g_wheel.PressA(now); STEP(43, "Games::Open", g_games.Open(g_wheel.Sel(), now)); }   // (v99: timed)
                if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) g_wheel.Rotate(now, -1);
                if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) g_wheel.Rotate(now, 1);
                if (down & HidNpadButton_B) { g_wheel.Relock(now); g_lock.PressB(now); }
                if (down & HidNpadButton_Y) CatchGold(now);     // v44: the golden cube
                if ((down & HidNpadButton_ZR) && (held & HidNpadButton_ZL)) { Wheel::s_debug = (Wheel::s_debug + 1) % 10; LogLine("vue de test : %d", Wheel::s_debug); }   // (v77: ZL + ZR; ZR alone = the tasks)
            } else if (g_wheel.Page() == Settings::kPage) {  // v28: the settings page (carousel of glass icons; v92: page 4)
                const int dc = (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) ? -1 : (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) ? 1 : 0;
                const int dr = settingsList ? ((down & (HidNpadButton_Up | HidNpadButton_StickLUp)) ? -1 : (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : 0) : 0;
                if (dc || dr) g_settings.Move(dc, dr, now);
                if (down & HidNpadButton_A) g_settings.PressA(now);
                if (down & HidNpadButton_B) g_settings.PressB(now);   // v40 (Yass): B closes what is open, never goes back to the games
                if (down & HidNpadButton_X) btaudio::PressX(now);      // v82: Audio Bluetooth - forget the chosen device (only there)
                if (datepages::Open() && !popup::Open())     // v83: Date et heure's pages - a held ▲ ▼ repeats (quicker and quicker)
                    datepages::Held((held & (HidNpadButton_Up | HidNpadButton_StickLUp)) ? -1 : (held & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : 0, now);
                if (journal::Open() && !popup::Open())       // v85: Brume's journal - the same
                    journal::Held((held & (HidNpadButton_Up | HidNpadButton_StickLUp)) ? -1 : (held & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : 0, now);
                // (v70.6: no Y here any more - the display is chosen in Réglages > Brume)
            } else if (g_wheel.Page() == social::kPage) {    // v95: the friends (▲ ▼ choose, A, X refuse a request, Y look again)
                const int dy = (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) ? -1 : (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : 0;
                if (dy) social::Move(dy, now);
                if (down & HidNpadButton_A) social::PressA(now);
                if (down & HidNpadButton_B) social::PressB(now);
                if (down & HidNpadButton_X) social::PressX(now);
                if (down & HidNpadButton_Y) social::PressY(now);
            } else if (g_wheel.Page() == Pads::kPage) {      // v39: the controllers (A = it vibrates, Y = pair one)
                const int dx = (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) ? -1 : (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) ? 1 : 0;
                if (dx) g_pads.Move(dx, now);
                if (down & HidNpadButton_A) g_pads.PressA(now);
                if (down & HidNpadButton_Y) g_pads.PressY(now);
            } else if (g_wheel.Page() == Apps::kPage) {      // v35: the homebrews (A = start it)
                const int dx = (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) ? -1 : (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) ? 1 : 0;
                if (dx) g_apps.Move(dx, now);
                if (down & HidNpadButton_A) g_apps.PressA(now);
                if (down & HidNpadButton_B) g_apps.PressB(now);
            } else if (g_wheel.Page() == Album::kPage) {     // v34: the album (a grid of captures, A = full screen)
                const int dx = (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) ? -1 : (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) ? 1 : 0;
                const int dy = (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) ? -1 : (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : 0;
                if (dx) g_album.Move(dx, now);
                else if (dy && !g_album.Viewing()) g_album.Move(dy * 8, now);   // ▲ ▼: eight at a time
                if (down & HidNpadButton_A) g_album.PressA(now);
                if (down & HidNpadButton_B) g_album.PressB(now);
            }
        } else {
            g_wheel.SetStick(0, 0);
            // v29 (Yass): like the Switch, ANY button opens it (the glass button shows the letter pressed); B = sleep.
            // v87: the flow like the PS4 (power-on, the prompt, who plays, the code or the three presses): Lock decides
            STEP(44, "Lock::Input", g_lock.Input(now, down));   // (v99: timed - « Qui joue ? » may open the Switch's picker)
        }
        {   // test bench (temporary): which part of the consoles' 7 ms is fixed? logged in perf.log
            static float wheelAt = -1; static int lastPhase = -1;
            if (g_wheel.Active() && wheelAt < 0) wheelAt = now;
            if (!g_wheel.Active()) wheelAt = -1;
            const float bt = wheelAt < 0 ? -1 : now - wheelAt - 4.0f;
            const int phase = 0 && bt >= 0 && bt < 36.0f ? ((int)(bt / 6.0f)) % 3 : 0;   // bench off (done: triangles were the cost)
            Wheel::s_bench = phase;
            if (phase != lastPhase) { LogLine("banc d'essai : %s", phase == 0 ? "normal" : phase == 1 ? "aucune console" : "1 triangle par morceau"); lastPhase = phase; }
        }
        Lock::s_avatarsOk = g_profiles.Avatars() != nullptr;     // v87: « Qui joue ? » shows the real avatars
        Lock::s_tilt[0] = g_ptr[0]; Lock::s_tilt[1] = g_ptr[1];
        STEP(1, "Lock::Update", g_lock.Update(now, dt));
        if (g_lock.Unlocked() && !g_wheel.Active()) g_wheel.Unlock(now, true);   // the big bang: the consoles arrive
        notify::Wait(!g_wheel.Active());                         // v87: the messages wait behind the lock screen (it counts them)
        STEP(2, "charge::Update", charge::Update(now, g_psmOk, !g_wheel.Active()));         // v87: the charger plugged in / out, the battery full / low
        if (!g_wheel.Active()) {                         // v29.2: the lock screen's harmony, in the chosen console's key
            const int s = audio::KeySemis();
            const int lockChord[4] = { 46 + s, 53 + s, 57 + s, 62 + s }, resolve[4] = { 50 + s, 57 + s, 62 + s, 66 + s };
            audio::SetHarmony(g_lock.Unlocked() ? resolve : lockChord);
        }
        STEP(4, "audio::Update", audio::Update(dt));
        STEP(5, "haptic::Update", haptic::Update(dt));

        SceneUbo u;
        STEP(6, "FillScene", FillScene(u, now, dt, g_gpu.Width(), g_gpu.Height()));
        int hour, minute, wday, day, month;
        LocalTime(hour, minute, wday, day, month);
        const float hourNow = theme::Hour((float)hour + minute / 60.0f);   // (v81: uisim may force it)
        Settings::s_hour = hourNow;                       // v53: the sky follows the hour (Réglages > Brume)
        STEP(7, "Wheel::Update", g_wheel.Update(now, dt, hourNow));
        STEP(8, "Games::Update", g_games.Update(now, dt));
        STEP(9, "Settings::Update", g_settings.Update(now, dt));
        STEP(10, "Profiles::Update", g_profiles.Update(now, dt));
        if (!g_wheel.Active() || g_games.Active()) mypage::CloseNow();   // v96: never over the lock screen or the games
        mypage::s_avatarsOk = g_profiles.Avatars() != nullptr;
        mypage::Update(now, dt);
        Wheel::s_cover = mypage::Amount();
        const bool calm = now >= g_calmAt;               // v80: nothing starts in the background during the way back (it stuttered)
        STEP(11, "nextendo::Update", nextendo::Update(now, g_wheel.Active() && calm)); // v49: players online (every 5 min, only while the menu is on screen)
        STEP(12, "covers::Update", covers::Update(now, g_wheel.Active() && calm));   // v75: the missing covers, downloaded by the console
        STEP(13, "tasks::Update", tasks::Update(now));                              // v77: the task centre
        STEP(27, "spotify::Update", spotify::Update(now, dt, g_wheel.Active()));   // v98: Spotify (the service, its status, the cover)
        {   // v44 - the golden cube appears after 20 s on the wheel (once a session), leaves with the lock screen
            if (g_wheel.Active()) { if (g_wheelSince < 0) g_wheelSince = now; } else { g_wheelSince = -1; g_goldOn = false; }
            if (!Settings::s_goldCube) g_goldOn = false;
            if (Settings::s_goldCube && g_wheel.Active() && !g_goldOn && !g_goldDone && g_ncubes > 0 && g_wheelSince >= 0 && now - g_wheelSince > 20.0f) {
                g_goldOn = true;
                audio::Bell(audio::HarmonyNote(2, 86, 98), 0.035f, true);
                g_system.Status(now, "Un cube brille quelque part dans la brume... (Y)");
            }
            g_goldK += ((g_goldOn ? 1.0f : 0.0f) - g_goldK) * (1 - expf(-dt * 3.0f));
        }
        STEP(14, "System::Update", g_system.Update(now, dt));                       // v33: the power popup (its light, its delayed action)
        STEP(15, "Album::Update", g_album.Update(g_gpu, g_wheel, now, dt));        // v34: the album (reads the list when the page arrives)
        STEP(16, "Apps::Update", g_apps.Update(g_wheel, now, dt));                // v35: the homebrews (read when the page arrives; the launch)
        STEP(17, "Pads::Update", g_pads.Update(g_wheel, now, dt));                // v39: the controllers (their colours, battery, hello, the room's colour)
        STEP(18, "social::Update", social::Update(g_wheel, now, dt, g_wheel.Active() && calm));   // v95: the friends (every 2 min, only while the menu is on screen)
        STEP(19, "Games::CountsTick", Games::CountsTick(now));                         // v66 (v90: and the emulators found)
        if (g_manageAt > 0 && now >= g_manageAt) {      // v67: Réglages > Données > Gérer les logiciels
            g_manageAt = -1;
            if (g_wheel.Active() && g_wheel.Page() == 0) {   // v79: the Warp just landed - the Switch is already at its place
                if (!g_games.Active()) { g_games.Open(g_wheel.Sel(), now); g_wheel.SnapFocus(); }
                g_games.Manage(true, now);
            } else g_manageBack = false;
        }
        if (g_manageBack && g_manageAt < 0 && (!g_games.Active() || !g_games.Managing())) g_manageBack = false;   // (− left « Gérer »: B is the games' again)
        // #topTime: the mockup's « sam. 15 novembre · 21:24 »
        static const char *const kDay[7] = { "dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam." };   // lang:fr (French only, the English is beside it)
        // v96: the month short (« mar. 17 nov. ») - the top bar has to hold the battery and Spotify's line beside the date
        static const char *const kMonth[12] = { "janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.",   // lang:fr (French only, the English is beside it)
                                                "août", "sept.", "oct.", "nov.", "dé" "c." };   // lang:fr (French only, the English is beside it)
        char timeText[64];
        static const char *const kDayEn[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        static const char *const kMonthEn[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        char hm[16];
        sysclock::HourText(hm, sizeof hm, hour, minute);    // v83: 24 h or 12 h (Réglages > Système > Date et heure)
        if (lang::En())                                      // v64
            snprintf(timeText, sizeof timeText, "%s, %s %d \xc2\xb7 %s", wday >= 0 && wday < 7 ? kDayEn[wday] : "",
                     month >= 1 && month <= 12 ? kMonthEn[month - 1] : "", day, hm);
        else snprintf(timeText, sizeof timeText, "%s %d %s \xc2\xb7 %s", wday >= 0 && wday < 7 ? kDay[wday] : "",
                 day, month >= 1 && month <= 12 ? kMonth[month - 1] : "", hm);
        const u64 tDraw = armGetSystemTick();             // v90: the interface's vertices (timed, see StepEnd)
        g_ui.Begin(g_gpu.Docked() ? 1.5f : 1.0f);
        Ui::s_tint = Rgba{ Settings::s_ambient[0], Settings::s_ambient[1], Settings::s_ambient[2], 1 };   // v78: the surfaces' light
        g_wheel.DrawLabels(g_ui, now);                   // v93: the blocks of glass' names (under everything)
        g_lock.Draw(g_ui, now, hour, minute, wday, day, month);
        if (!g_games.Visible(now)) g_wheel.DrawUi(g_ui, g_assets, now, timeText);
        g_settings.DrawUi(g_ui, g_assets, now, g_wheel);
        g_settings.DrawCode(g_ui, now);                  // v87: the lock code's panel (over the page)
        g_profiles.DrawUi(g_ui, now);                    // v46: the profiles' panel (above the pages)
        g_games.Draw(g_ui, now);
        g_album.DrawUi(g_ui, now, g_wheel);
        g_apps.DrawUi(g_ui, now, g_wheel);
        g_pads.DrawUi(g_ui, now, g_wheel);
        social::DrawUi(g_ui, now, g_wheel);              // v95: the Amis page
        mypage::Draw(g_ui, now);                         // v96: the profile page (over the page that was there)
        g_pads.DrawTest(g_ui, now);                      // v84: the controllers' test (over the settings)
        g_system.Draw(g_ui, g_assets, now);
        social::DrawDiscord(g_ui, now);                  // v95: the Discord QR (over any page)
        {   // v77: the task centre (its mark in the top bar, its panel) and the notifications, above everything
            static float s_barK = 0, s_noteK = 0;
            const bool bar = g_wheel.Active() && !g_games.Visible(now);
            s_barK += ((bar ? 1.0f : 0.0f) - s_barK) * (1 - expf(-dt * 8.0f));
            s_noteK += ((g_wheel.Active() ? 1.0f : 0.0f) - s_noteK) * (1 - expf(-dt * 8.0f));
            const float timeL = 1215.0f - g_ui.Width(fonts::kTopBar, timeText, 0.5f);   // (layout::kTimeRight: the date and time end there)
            spotify::DrawBar(g_ui, timeL - 13.0f, 52.0f, s_barK, now);   // v98: what Spotify plays, then the task centre left of it
            // the task centre, left of the battery (v96), of Spotify's line (v98) and of the date and time
            tasks::DrawMark(g_ui, now, s_barK, timeL - 30.0f - spotify::BarRoom() * 1.2f - charge::BarWidth());
            charge::Draw(g_ui, now);                     // v87: the light from the bottom (the charger)
            g_pads.DrawRail(g_ui, now);                  // v94: the light along an edge (a Joy-Con slid onto its rail / connected)
            tasks::DrawPanel(g_ui, now);
            spotify::DrawPanel(g_ui, now);              // v98: the Spotify panel
            popup::Draw(g_ui, now);                      // v78: the popup (above the panels; the notifications stay above it)
            notify::Avoid(g_profiles.Open() ? 450.0f : (mypage::Open() ? 540.0f : 0.0f));   // v78: the profiles' panel is where they come: they step aside (v96: the profile page's name too)
            notify::Draw(g_ui, now, s_noteK);
        }
        StepEnd(22, "Draw (interface)", tDraw, now);

        g_gpu.Begin();
        STEP(21, "Stream",
        if (g_profiles.NeedsStream()) g_profiles.Stream(g_gpu);   // v46: the avatars' strip, when it changed (rare)
        else if (social::NeedsStream()) social::Stream(g_gpu);   // v95: the friends' strip (a picture or a QR code arrived)
        else if (spotify::NeedsStream()) spotify::Stream(g_gpu);   // v98: the Spotify cover (8 KB, when a new one is ready)
        else if (g_games.Visible(now)) g_games.Stream(g_gpu);   // v31: one game cover per frame, uploaded inside the frame
        else if (g_album.Visible(g_wheel)) g_album.Stream(g_gpu, g_games.Covers(), g_wheel);   // v34: or one album thumbnail
        else g_apps.Stream(g_gpu, g_games.Covers(), g_wheel));   // v35: or one homebrew icon (the same places)
        Shelf::Reap();                                   // v90: a cover's load finished while nobody asks: its thread is closed
        // v100 (perf): the screen is black (a launch's dive, the way back held black, « Rejoindre »...): an opaque black quad
        // covers it all (Ui::Blackout) - nothing under it is drawn (the fog, the glass, the stars, the 3D), the interface only
        // from that quad on. perf.log: the Switch's dive into its screen filled the frame with the console's shader under the
        // black (GPU up to 33 ms on the games screen). The picture is the same: black.
        const bool blackout = g_ui.Blackout();
        static UiVtx s_stars[2][2112];                 // 160 stars + 3 comets of 28 glows (v70) + v81 Chrome: their sparkles, the ring
        int nStarF = 0;
        if (!blackout) {
        // 1. the fog, small - one image in two: it drifts slowly, nobody sees 30 updates a second instead
        //    of 60, and it halves its GPU cost (perf: 56-59 FPS with GPU 16 ms before). The target keeps
        //    the last fog in between; the frame's fragment barrier (Begin) protects it as before.
        static unsigned s_fogFrame = 0;
        {   // v90: how far the lock screen's blasts can push a pixel (glass_fsh LightAt: the air bends): the cubes' squares grow
            // by that much (cubebox_vsh) - uCubeFx[5].w, a free half of the block (common.glsl uThD.y)
            float push = 0.0f;
            for (int b = 0; b < 4; b++) if (u.b[b][3] != 0.0f) push += 0.8578f * fabsf(u.b[b][2]) * u.light[2] * 0.22f;
            u.cubeFx[5][3] = push;
        }
        g_gpu.SetUniforms(&u, sizeof(u));
        {   // v38: one half of the fog each frame (top, then bottom): each half is still redrawn 30 times a second, as
            // before, but the cost is cut in two and the same every frame (before: a heavy frame, then a light one).
            // v90 (perf): while nothing changes the fog's look quickly (its strength, colours, theme, the
            // lock screen's steps), one THIRD each frame (each row 20 times a second: it drifts ~1 px in that time); any quick
            // change and it is halves again for half a second (as before: no band ever lags behind a flash)
            // what haze_fsh reads besides the time (common.glsl: uThA = bo[0].zw bo[1].zw, uThB = bo[2].zw bo[3].zw, uThC.x =
            // cubeFx[0].w; the theme's two clocks, bo[1].z and bo[2].w, only set the drift's pace: left out)
            constexpr int kSig = 16;
            static float s_sig[kSig] = {}; static int s_quick = 60;
            const float S = u.haze[1] * (0.25f + 0.35f * u.origin[3] + 0.5f * u.origin[2]);
            const float sig[kSig] = { S, u.haze[0], u.haze[3], u.light[0], u.ambient[0], u.ambient[1], u.ambient[2], u.ambient[3],
                                      u.bo[0][2], u.bo[0][3], u.bo[1][3], u.bo[2][2], u.bo[3][2], u.bo[3][3], u.cubeFx[0][3],
                                      (u.cubeFx[2][3] + u.cubeFx[3][3]) * u.bo[0][2] };   // (the wind, A's ripple: Chrome only)
            bool quick = false;
            for (int i = 0; i < kSig; i++) { quick = quick || fabsf(sig[i] - s_sig[i]) > 0.0015f * (1.0f + fabsf(sig[i])); s_sig[i] = sig[i]; }
            if (quick) s_quick = 30; else if (s_quick > 0) s_quick--;
            const int parts = s_quick > 0 ? 2 : 3, rows = (kHazeH + parts - 1) / parts;
            if (parts == 3) g_fogThirds++;
            const int part = (int)(s_fogFrame++ % (unsigned)parts);
            g_gpu.BindTarget(g_haze);
            g_gpu.SetScissor(0, part * rows, kHazeW, part == parts - 1 ? kHazeH - part * rows : rows);   // (216 rows: 2 x 108 or 3 x 72)
            g_gpu.UseProgram(g_hazeProg);
            g_gpu.DrawFullscreen();
            g_gpu.Barrier();
        }
        g_gpu.Mark(0);
        // 2. full screen: fog upscaled + water cubes + the lock screen's light. v90 (perf): in two passes - the room over the
        // whole screen with the cheap part of the shader, then the cubes with the whole shader only in the square around each
        // one (grown by what the blasts can push) - the same pixels (glass_fsh, cubebox_vsh)
        const bool split = g_glassSplit;
        g_gpu.BindScreen();
        g_gpu.UseProgram(split ? g_glassBgProg : g_glassProg);
        g_gpu.BindTexture(g_haze);
        g_gpu.DrawFullscreen();
        if (split) { g_gpu.UseProgram(g_glassCubeProg); g_gpu.DrawCubeQuads(); }
        // 2b. v28 - the stars behind the centre console (the consoles will cover them); the nearer ones come after
        const float starLight = g_wheel.Active() ? 1.0f : g_lock.LightBase();
        const int nStarB = g_wheel.Stars(now, starLight, s_stars[0], 2112, false);
        nStarF = g_wheel.Stars(now, starLight, s_stars[1], 2112, true);
        if (nStarB) {
            g_gpu.BeginUi();
            g_gpu.UseProgram(g_uiProg);
            const int d = g_gpu.Docked() ? 1 : 0;
            const Target *ts[6] = { &g_atlas[d], g_assets.uiOk ? &g_assets.ui[0] : &g_atlas[d], &g_haze,
                                    g_profiles.Avatars() ? g_profiles.Avatars() : &g_atlas[d],   // v46: the avatars
                                    social::Strip() ? social::Strip() : &g_atlas[d],          // v95: the friends' pictures
                                    spotify::Cover() ? spotify::Cover() : &g_atlas[d] };          // v98: the Spotify cover (binding 5)
            g_gpu.BindTextures(ts, 6);
            if (g_uiLiteProg.ok) g_gpu.UseProgram(g_uiLiteProg);   // v99: the stars are mode 21 only (the simple modes' program)
            g_gpu.DrawUi(s_stars[0], (uint32_t)nStarB);
        }
        g_gpu.Mark(1);                                   // (v90: mark 0 = the fog, 1 = the glass and the stars behind)
        // 3. the consoles (3D)
        Wheel::s_probeAll = false;
        Wheel::s_probeQuiet = g_probeFrame % 30 != 0;
        if (Wheel::s_probeAll) { char tag[48]; snprintf(tag, sizeof tag, "image %d : brume + cubes", g_probeFrame); g_gpu.Probe(tag); }
        g_gpu.UseProgram(g_meshProg);
        STEP(24, "Wheel::DrawMeshes", g_wheel.DrawMeshes(g_gpu, g_assets, g_haze, now));
        g_gpu.Mark(2);
        if (g_games.Visible(now)) {                        // v31: the game boxes, in the consoles' scene (same light, same depth)
            g_gpu.UseProgram(g_boxProg);
            g_games.DrawBoxes(g_gpu, g_assets, g_haze, now, g_wheel);
        }
        if (g_pads.ModelVisible(g_wheel)) {                // v40: the 3D Switch as it really is (the Manettes page)
            g_gpu.UseProgram(g_meshProg);
            g_pads.DrawModel(g_gpu, g_assets, g_haze, now, g_wheel);
        }
        if (g_album.Visible(g_wheel)) {                    // v34: the album's glass cards
            g_gpu.UseProgram(g_boxProg);
            g_album.DrawCards(g_gpu, g_assets, g_haze, now, g_wheel, g_games.Covers());
        }
        if (g_apps.Visible(g_wheel)) {                     // v35: the homebrews' glass tiles
            g_gpu.UseProgram(g_boxProg);
            g_apps.DrawCards(g_gpu, g_assets, g_haze, now, g_wheel, g_games.Covers());
        }
        if (g_settings.Visible(g_wheel)) {               // v28: the settings carousel's glass pebbles (the cubes' water)
            g_gpu.UseProgram(g_iconProg);
            g_settings.DrawMeshes(g_gpu, g_assets, g_haze, now, g_wheel);
        }
        if (false && g_gpu.MeshDrawn() && g_cubeOverlay) {   // v38: never needed now (the cubes are always behind) - v29: the cubes in front of a console stay in front (v29.1: only
            SceneUbo u2 = u; u2.screen[3] = 1.0f;         // those, and only a small square around each: it cost 7 ms full screen)
            g_gpu.BeginOverlay();
            g_gpu.UseProgram(g_cubeQuadProg);   // cubequad_vsh + glassfront_fsh
            g_gpu.SetUniforms(&u2, sizeof(u2));
            g_gpu.BindTexture(g_haze);
            g_gpu.DrawCubeQuads();
            g_gpu.EndOverlay();
        }
        g_gpu.Mark(3);                                   // (v90: 2 = the consoles, 3 = the other 3D)
        }                                                // (v100: the scene, unless the screen is black)
        // 4. the interface: text atlas, the wheel's baked interface, the fog (behind the glass panels)
        g_gpu.BeginUi();
        g_gpu.UseProgram(g_uiProg);
        {
            const int d = g_gpu.Docked() ? 1 : 0;
            const Target *ts[6] = { &g_atlas[d], g_assets.uiOk ? &g_assets.ui[0] : &g_atlas[d], &g_haze,
                                    g_profiles.Avatars() ? g_profiles.Avatars() : &g_atlas[d],   // v46: the avatars
                                    social::Strip() ? social::Strip() : &g_atlas[d],          // v95: the friends' pictures
                                    spotify::Cover() ? spotify::Cover() : &g_atlas[d] };          // v98: the Spotify cover (binding 5)
            g_gpu.BindTextures(ts, 6);
        }
        if (nStarF) {                                    // the stars in front of the consoles (some pass right by the camera)
            if (g_uiLiteProg.ok) g_gpu.UseProgram(g_uiLiteProg);   // v99
            g_gpu.DrawUi(s_stars[1], (uint32_t)nStarF);
            g_gpu.UseProgram(g_uiProg);                  // (Ui::Flush takes the bound program as ui_fsh)
        }
        STEP(23, "Ui::Flush", g_ui.Flush(g_gpu));
        {   // probe: the first 4 s of the wheel are checked to the end (interface included); log every 30 images
            if (false && g_wheel.Active() && g_probeFrame < 240) {
                char tag[48]; snprintf(tag, sizeof tag, "image %d de la roue (interface)", g_probeFrame);   // lang:log (never drawn)
                if (g_gpu.Probe(tag) && g_probeFrame % 30 == 0) { LogLine("sonde : %s OK", tag); FlushLog(); }
                g_probeFrame++;
            }
        }
        const u64 cpuEnd = armGetSystemTick();
        g_gpu.End();

        // ---- measurements ----
        const float cpu = Seconds(cpuEnd - start - g_gpu.AcquireTicks()) * 1000.0f, gpu = g_gpu.GpuMs();
        if (g_lastTick) {
            const float fr = Seconds(start - g_lastTick) * 1000.0f;
            g_frameSum += fr; if (fr > g_frameMax) g_frameMax = fr;
            g_cpuSum += cpu;  if (cpu > g_cpuMax) g_cpuMax = cpu;
            g_gpuSum += gpu;  if (gpu > g_gpuMax) g_gpuMax = gpu;
            for (int k = 0; k <= Gpu::kMarks; k++) g_stSum[k] += g_gpu.StageMs(k);
            if (fr > 20.0f) g_slowFrames++;              // v90: how many images were late (over 20 ms) in the 2 s
            for (int k = 0; k < 8; k++) g_consSum[k] += g_gpu.ConsoleMs(k);
            g_consN++;
            g_n++;
        }
        g_lastTick = start;
        static float sysAt = 10.0f;
        if (now >= sysAt && g_lines < 400) { sysAt = now + 10.0f; STEP(20, "LogSystem", LogSystem(g_wheel.Active() ? "roue" : "verrou")); }
        LogSystemTake();                                 // v99: the system's numbers, asked off the frame
        if (g_lines < 400 && g_n > 0 && Seconds(start - g_winStart) >= 2.0f) {
            LogLine("t=%5.1fs  %dx%d %s  image %5.2f ms (pire %5.2f, %4.1f FPS)  GPU %5.2f ms (pire %5.2f)  CPU %5.2f ms (pire %5.2f) | brume %.1f (fond %.1f, verre %.1f) consoles %.1f (+3D %.1f) interface %.1f (console %d, page %d%s, %d lentes, brume au tiers %d %%)",
                    now, g_gpu.Width(), g_gpu.Height(), g_gpu.Docked() ? "dock" : "portable",
                    g_frameSum / g_n, g_frameMax, 1000.0f * g_n / g_frameSum,
                    g_gpuSum / g_n, g_gpuMax, g_cpuSum / g_n, g_cpuMax,
                    (g_stSum[0] + g_stSum[1]) / g_n, g_stSum[0] / g_n, g_stSum[1] / g_n, g_stSum[2] / g_n, g_stSum[3] / g_n, g_stSum[4] / g_n,
                    g_wheel.Active() ? g_wheel.Sel() : -1, g_wheel.Active() ? g_wheel.Page() : -1, g_games.Visible(now) ? " jeux" : "", g_slowFrames, g_n ? g_fogThirds * 100 / g_n : 0);
            g_lines++;
            if (g_lines == 5 || g_lines == 15 || g_lines == 30 || g_lines == 60) FlushLog();   // Restart gives no callback
            g_winStart = start;
            g_n = 0; g_cpuSum = g_cpuMax = g_gpuSum = g_gpuMax = g_frameSum = g_frameMax = 0;
            for (float &s : g_stSum) s = 0;
            g_slowFrames = 0; g_fogThirds = 0;
        }
        g_logWork.Done();                                // v90: the log's writer finished: its thread is closed (its stack freed)
        g_loopEnd = armGetSystemTick();
    }
}

// ---- the loader's interface ------------------------------------------------
extern "C" {

__attribute__((visibility("default"))) int onBoot(NWindow *win) {
    mkdir(kDir, 0777);
    // Trial mode (only if sdmc:/brume/essai.txt exists): the menu.ini that selected
    // us is removed straight away, so the next reboot is back on sLaunch.
    struct stat st;
    if (stat(kTrialTag, &st) == 0) remove(kMenuIni);
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    // v92: the console counts with ONE Joy-Con on its rail (the default « Dual » asks for both: one taken off could silence
    // the other), and the menu listens to every controller (whoever holds one drives it, as on the Switch). Both only
    // concern the menu (its own applet resource): a game sets its own.
    hidSetNpadHandheldActivationMode(HidNpadHandheldActivationMode_Single);
    padInitializeAny(&g_pad);

    g_t0 = g_winStart = armGetSystemTick();
    g_seed = (u32)(g_t0 ^ (g_t0 >> 32)) | 1u;
    LogLine("=== Brume : demarrage ===");
    Gpu::s_log = [](const char *m) { LogLine("deko3d : %s", m); FlushLogNow(); };   // (v90: at once - a GPU error may be the last thing)
    Wheel::s_probeLog = [](const char *m) { LogLine("%s", m); FlushLogNow(); };
    Games::s_log = [](const char *m) { LogLine("%s", m); FlushLog(); };
    System::s_log = [](const char *m) { LogLine("%s", m); FlushLogNow(); };   // (v90: at once - a restart / power off follows)
    Settings::s_toast = [](float t, const char *m) { g_system.Status(t, "%s", m); };
    Games::s_focus = [](bool on) { g_wheel.SetFocus(on); };                  // v30: the staging of the games
    Games::s_dive = [](float t, bool portable) { g_wheel.StartDive(t, portable); };
    Games::s_diveEnd = []() { g_wheel.EndDive(); };
    Games::s_nudge = [](float t) { g_wheel.Nudge(t); };                     // v33: the media seats
    haptic::Init();
    ToyInit();                         // v29: the gyroscope, the touch screen
    Settings::Load();                  // v29: the ambient colour, the vibrations (sdmc:/brume/reglages.ini)
    Games::CleanUp();   // a launch interrupted by a crash or a power cut left its redirect
    LogClocks();

    if (!g_gpu.Init(win)) { LogLine("ERREUR : init deko3d"); FlushLogNow(); return -1; }
    if (!g_gpu.MakeTarget(g_haze, kHazeW, kHazeH, 0)) { LogLine("ERREUR : cible brume"); FlushLogNow(); return -1; }
    if (!g_gpu.MakeTexture(g_atlas[0], fonts::kAtlas0.w, fonts::kAtlas0.h, DkImageFormat_R8_Unorm, 1, fonts::kAtlas0.pixels, 1) ||
        !g_gpu.MakeTexture(g_atlas[1], fonts::kAtlas1.w, fonts::kAtlas1.h, DkImageFormat_R8_Unorm, 1, fonts::kAtlas1.pixels, 2)) {
        LogLine("ERREUR : polices"); FlushLogNow(); return -1;
    }
    if (Settings::s_clockFont && !ApplyClockFont(Settings::s_clockFont)) Settings::SetClockFont(0);   // v45
    if (!g_gpu.MakeProgram(g_hazeProg, k_fullscreen_vsh, sizeof(k_fullscreen_vsh), k_haze_fsh, sizeof(k_haze_fsh)) ||
        !g_gpu.MakeProgram(g_glassProg, k_fullscreen_vsh, sizeof(k_fullscreen_vsh), k_glass_fsh, sizeof(k_glass_fsh)) ||
        !g_gpu.MakeProgram(g_uiProg, k_ui_vsh, sizeof(k_ui_vsh), k_ui_fsh, sizeof(k_ui_fsh)) ||
        !g_gpu.MakeProgram(g_meshProg, k_mesh_vsh, sizeof(k_mesh_vsh), k_mesh_fsh, sizeof(k_mesh_fsh)) ||
        !g_gpu.MakeProgram(g_iconProg, k_mesh_vsh, sizeof(k_mesh_vsh), k_icon_fsh, sizeof(k_icon_fsh)) ||
        !g_gpu.MakeProgram(g_boxProg, k_mesh_vsh, sizeof(k_mesh_vsh), k_box_fsh, sizeof(k_box_fsh)) ||
        !g_gpu.MakeProgram(g_cubeQuadProg, k_cubequad_vsh, sizeof(k_cubequad_vsh), k_glassfront_fsh, sizeof(k_glassfront_fsh))) {
        LogLine("ERREUR : shaders"); FlushLogNow(); return -1;
    }
    if (audio::Init()) LogLine("son : pret%s%s", audio::Error()[0] ? " (" : "", audio::Error()[0] ? "manque un fichier)" : "");
    else LogLine("son : coupe (%s)", audio::Error());
    {
        char err[96];
        const u64 a0 = armGetSystemTick();
        const bool ok = g_assets.Load(g_gpu, err, sizeof err);
        LogLine("roue : %s%s (%d textures, %.0f ms)", ok && !err[0] ? "prete" : "incomplete - ", err, g_assets.ntex,
                Seconds(armGetSystemTick() - a0) * 1000.0f);
    }
    // v90 (perf): the glass pass in two - made here, after the settings' glass (Assets::Load): uisim numbers the programs
    g_glassSplit = g_gpu.MakeProgram(g_glassBgProg, k_fullscreen_vsh, sizeof(k_fullscreen_vsh), k_glassbg_fsh, sizeof(k_glassbg_fsh)) &&
                   g_gpu.MakeProgram(g_glassCubeProg, k_cubebox_vsh, sizeof(k_cubebox_vsh), k_glasscube_fsh, sizeof(k_glasscube_fsh));
    if (!g_glassSplit) LogLine("verre : une seule passe (programmes du verre en deux introuvables)");
    // v99 (perf): the interface's simple modes in a small program of their own (made last: uisim numbers it 10)
    if (g_gpu.MakeProgram(g_uiLiteProg, k_ui_vsh, sizeof(k_ui_vsh), k_uilite_fsh, sizeof(k_uilite_fsh))) g_ui.SetLiteProgram(&g_uiLiteProg);
    else { g_uiLiteProg.ok = false; LogLine("interface : programme simple introuvable (tout par ui_fsh)"); }
    if (g_gpu.MakeProgram(g_uiSurfProg, k_ui_vsh, sizeof(k_ui_vsh), k_uisurf_fsh, sizeof(k_uisurf_fsh))) g_ui.SetSurfProgram(&g_uiSurfProg);   // (uisim: 11)
    else g_uiSurfProg.ok = false;
    // v31: the game boxes (their model, and places for the covers after the consoles' textures)
    if (g_games.InitShelf(g_gpu, 6 + (uint32_t)g_assets.ntex))
        LogLine("jeux : boites pretes, %d places de jaquettes (descripteurs %d+), images GPU %.1f Mo", g_games.ShelfSlots(),
                6 + g_assets.ntex, g_gpu.ImgUsed() / (1024.0f * 1024.0f));
    else LogLine("jeux : boites indisponibles (memoire)");
    Album::s_log = [](const char *m) { LogLine("%s", m); };
    Apps::s_log = [](const char *m) { LogLine("%s", m); };
    Pads::s_pair = [](float t) { g_system.Open(3, t); };                       // the Switch's controllers screen (pairing, order)
    Settings::s_padTest = [](float t) { g_pads.TestBegin(t); };                // v84: Réglages > Manettes > Tester les manettes
    if (!g_album.Init(g_gpu, 6 + (uint32_t)g_assets.ntex + (uint32_t)g_games.ShelfSlots())) LogLine("album : pas de place pour la photo entiere");
    Profiles::s_toast = [](float t, const char *m) { g_system.Status(t, "%s", m); };   // v46
    Profiles::s_log = [](const char *m) { LogLine("%s", m); };
    nextendo::s_log = [](const char *m) { LogLine("%s", m); };   // v49
    covers::s_log = [](const char *m) { LogLine("%s", m); FlushLog(); };   // v75
    covers::s_toast = [](float t, const char *m) { g_system.Status(t, "%s", m); };
    ntp::s_log = [](const char *m) { LogLine("%s", m); FlushLog(); };   // v70.8
    sysclock::s_log = [](const char *m) { LogLine("%s", m); FlushLog(); };   // v83: the clock and the zone written by hand
    ntp::s_toast = [](float t, const char *m) { g_system.Status(t, "%s", m); };
    if (!g_profiles.Init(g_gpu, 7 + (uint32_t)g_assets.ntex + (uint32_t)g_games.ShelfSlots())) LogLine("profils : pas de place pour les avatars");
    g_profiles.Load();
    social::s_log = [](const char *m) { LogLine("%s", m); };   // v95: the friends' strip (after the avatars' one)
    if (!social::Init(g_gpu, 8 + (uint32_t)g_assets.ntex + (uint32_t)g_games.ShelfSlots())) LogLine("amis : pas de place pour les avatars");
    Settings::s_discord = [](float t) { social::DiscordOpen(t); };   // v95: Réglages > Réseau > Vocal Discord
    spotify::s_log = [](const char *m) { LogLine("%s", m); };   // v98: Spotify (Spotify.hpp)
    if (!spotify::InitCover(g_gpu, 9 + (uint32_t)g_assets.ntex + (uint32_t)g_games.ShelfSlots())) LogLine("spotify : pas de place pour la pochette");
    spotify::Boot(0.0f);
    pin::Load();                                  // v87: the lock codes, per profile (brume/codes.txt)
    emus::s_log = [](const char *m) { LogLine("%s", m); };   // v90: the emulators found by themselves - the wheel as last time, at once
    emus::s_present = [](uint16_t m, uint16_t f, float t, bool inst) { g_wheel.SetPresent(m, f, t, inst); };
    Wheel::s_arrived = [](int c, float t) { emus::Arrived(c, t); };
    emus::Boot(0.0f);
    g_lock.Boot(0.0f);                            // v87: power-on - Brume's emblem first, then the dark screen
    if (!Settings::s_lockOn) g_lock.Skip(0.0f);   // v43: the lock screen switched off in the settings
    Settings::s_open = [](int tab, float t) { g_system.Open(tab, t); };
    Settings::s_restart = [](float t) { g_system.Restart(t); };   // v78: a popup's « Redémarrer »
    Settings::s_leave = [](float t) {                              // v85: « Revenir au menu d'origine » - the emergency exit's files, then the restart
        if (LeaveFiles("reglages")) g_system.Restart(t);
        else popup::Error(t, "Brume n'a pas pu s'effacer", "La carte SD n'a pas voulu renommer ses fichiers. Rien n'a changé.", 0);
    };
    journal::s_flush = [] { FlushLogNow(); };                       // v85: Réglages > Système > Journal de Brume reads perf.log
    mypage::Load();                                   // v96: the profile page (its choice: sdmc:/brume/profil.ini)
    mypage::s_switchPanel = [](float t) { g_profiles.OpenPanel(t); };
    mypage::s_friends = [](float t) { g_wheel.Warp(t, social::kPage, 0.42f); };
    Settings::s_profilesChanged = [] { g_profiles.LoadSoon(); };   // (v99: off the frame)
    // v67: the games page, the Switch, then « Gérer ». v79 (Yass: « marche mais fastidieux » - 5 pages crossed, the wheel turning,
    // a wait, then 2 B and 5 R to come back): one move - the settings fade, the page jumps (Wheel::Warp), the Switch is
    // already chosen and already at its place, its games come in sorted by size; B brings the settings back the same way
    Settings::s_manage = [](float t) {
        if (g_wheel.Page() != Settings::kPage || g_manageAt > 0) return;
        g_wheel.Snap(0);                             // (the wheel is off screen: no turn to watch)
        g_manageAt = g_wheel.Warp(t, 0, 0.38f) + 0.001f;   // (the frame the Warp lands)
        g_manageBack = true;
    };   // v57 (review): a nickname / icon changed from the settings   // v43: the settings' rows that open a Switch page
    g_psmOk = R_SUCCEEDED(psmInitialize());   // battery (a failure only hides it from the log)
    g_tsOk = R_SUCCEEDED(tsInitialize());     // temperatures
    LogSystem("demarrage");
    WatchdogStart();                                 // v101
    LogLine("pret : %dx%d (%s)", g_gpu.Width(), g_gpu.Height(), g_gpu.Docked() ? "dock" : "portable");
    g_ready = true;
    Games::RefreshCounts();                       // v66: the real number of games under each console
    FlushLog();
    return 0;
}

#ifdef BRUME_UISIM
void sim_lock_boot(void) { Lock::s_simFlow = true; g_lock.Boot(MenuNow()); }   // uisim « demo verrou 1 »: the v87 flow from power-on
#endif

__attribute__((visibility("default"))) int loop(void) {
    __atomic_store_n(&g_wdIn, armGetSystemTick() | 1, __ATOMIC_RELEASE);
    WdAt("loop()");
    if (g_ready) Frame();
    __atomic_store_n(&g_wdIn, 0, __ATOMIC_RELEASE);
    return 0;   // non-zero would end the loader's loop
}

// Back in the menu (a game or applet exited, HOME from a game): the donor's redirect goes.
// v80: the way back's staging plays first - the games are counted again (a thread walking the SD card), Nextendo and the
// covers looked at, only once it is over (they made it stutter)
__attribute__((visibility("default"))) void onOpen(void) {
    __atomic_store_n(&g_wdIn, armGetSystemTick() | 1, __ATOMIC_RELEASE);   // v101: the watchdog looks here too
    WdAt("onOpen()");
    g_lastTick = 0; g_clockTick = 0; g_calmAt = MenuNow() + 3.5f; Games::CleanUp(); g_games.Done(); g_apps.Done(); g_settings.Done(); social::Done(); Games::RefreshCounts(3.5f);
    __atomic_store_n(&g_wdIn, 0, __ATOMIC_RELEASE);
}
__attribute__((visibility("default"))) void onReload(void) {}
// v84: HOME while the controllers' test is open leaves it (HOME itself cannot be tested: the system keeps it)
__attribute__((visibility("default"))) void onHomeButton(void) { if (g_ready && g_pads.TestOpen()) g_pads.TestEnd(MenuNow()); }

// Before sleep: the GPU must be idle before vi and nvservices power down; the
// console wakes up on the lock screen again.
__attribute__((visibility("default"))) void onPowerButton(void) {
    haptic::Stop();
    if (g_ready) {
        g_gpu.WaitIdle();
        audio::Silence();
        g_lock.Reset(MenuNow());
        if (!Settings::s_lockOn) g_lock.Skip(MenuNow());
        g_wheel = Wheel{};   // bug (Yass): after sleep the wheel stayed on top of the lock screen, both usable
        g_games.Reset();
        g_apps.Sleep();                              // v86 (review): a homebrew chosen just before Power never starts behind the lock screen
        Settings::Sleep();
        g_profiles.CloseNow();                       // v56 (review): not over the lock screen after sleep
        mypage::CloseNow();                          // v96
        popup::CloseNow();                           // v78: nor a popup (no answer: nothing happens)
        btaudio::Close(MenuNow());                   // v82: no Bluetooth search left running in sleep
        spotify::ClosePanel(MenuNow());              // v98: not over the lock screen after sleep (the music itself is the service's)
        g_pads.TestStop();                           // v84: nor the controllers' test (its motors and sensor off)
        social::Sleep();                             // v95: the sign-in server stops, the Discord QR closes
    }
    FlushLogNow();
    g_lastTick = 0;
    g_clockTick = 0;                                 // v60: the menu's clock does not count the sleep
}

__attribute__((visibility("default"))) void onShutdown(void) {
    WatchdogStop();                                  // v101
    haptic::Stop();
    Games::Shutdown();                               // v68 (review): no background thread outlives the menu
    covers::Shutdown();                              // v75 (before Nextendo's: both use the network)
    social::Shutdown();                              // v95: the friends' worker (the sign-in server stops)
    Profiles::Shutdown();                            // v99: the users' background read
    Pads::Shutdown();                                // v99: the controllers' files (written off the frame)
    g_sysWork.Wait();                                // v99: the system's numbers (perf.log)
    nextendo::Shutdown();
    Album::Shutdown();
    Apps::Shutdown();                                // v90: the workers (Apps, the covers' loads) end first
    Shelf::Shutdown();
    if (g_ready) btaudio::Close(MenuNow());          // v82: btm:sys closed with the menu
    spotify::Shutdown();                             // v98: the cover's worker ends, the session closes (the music plays on)
    FlushLogNow();
    if (g_tsOk) { tsExit(); g_tsOk = false; }
    if (g_psmOk) { psmExit(); g_psmOk = false; }
    if (!g_ready) return;
    g_ready = false;
    audio::Shutdown();
    g_gpu.Shutdown();
}

} // extern "C"
