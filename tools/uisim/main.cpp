// uisim - runs the REAL menu (every src/*.cpp but Gpu.cpp) on the PC, frame by frame at 60 FPS, with a scripted
// controller, and writes the frames the script asks for (see GpuSim.cpp, replayed by view.html).
//   uisim <script> <out dir>      (run in a folder that holds a fake card: ./sdmc:/brume/...)
// Script lines:  <seconds> press A|B|X|Y|L|R|ZL|ZR|Plus|Minus|Up|Down|Left|Right   (held 2 frames)
//                <seconds> touch <x> <y>                                          (held 2 frames)
//                <seconds> hold A|B|... <duration>                                (v84: held that many seconds)
//                <seconds> shot <name>
//                <seconds> demo taches | fin   (v77: fake tasks and notifications, to see the task centre without a network)
//                <seconds> demo erreur | redemarrer | jeu | sansjeu   (v78: an error popup, the restart question, a game suspended)
//                <seconds> demo titre | retour | lent   (v80: one Switch game installed, the menu comes back (onOpen), the next 4
//                                                        frames take 70 ms each - like the first frames back from a game)
//                <seconds> demo home   (v89: HOME during the game: it stays suspended, the menu comes back; « retour » = it ended)
//                <seconds> demo theme <0..2> | heure <h> | batterie <pct> | charge <0|1> | ciel <0|1> | brumebat <0|1>
//                          (v81: a theme chosen as in Réglages; the hour and the battery forced; the two options on / off)
//                <seconds> demo h24 <0|1>   (v83: the clock's format, as Réglages > Système > Date et heure > Format 24 h)
//                <seconds> demo veille   (v86: onPowerButton - the console sleeps, then wakes on the lock screen)
//                <seconds> demo verrou 1 | pin <4 symbols 0..9>   (v87: the lock screen's flow from power-on (the old scripts keep
//                          the 3 quick presses), a code for the active profile (in memory only: the fake card is not touched)
//                <seconds> pose <sdmc:/path> | enleve <sdmc:/path>   (v90: an empty file put on / taken off the fake card, folders
//                          included - an emulator installed or removed; then « demo retour » and the menu looks again)
//                <seconds> demo rail <0..3> | paire <0|1> | pro <0|1>   (v92: the Joy-Con on the rails, the pair off them, a Pro Controller)
//                <seconds> demo stickx | sticky <-1..1> | couleurs <0|1>   (v94: the right stick held; the Joy-Con's colours)
//                <seconds> demo amis <0..6>   (v95: the Amis page without a network - 0 signed out, 1 signed in (canned friends),
//                          2 the phone's QR, 3 no client_id, 4 the next look brings changes (notifications), 5 waiting for the
//                          console's browser, 6 a refused sign-in); « demo discord 0 | 1 »: the Discord QR, no link / a link
#include <switch.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>
#include <sys/stat.h>
#include <string>
#include "Notify.hpp"
#include "Popup.hpp"
#include "Settings.hpp"
#include "Theme.hpp"
#include "Clock.hpp"
#include "Pin.hpp"
#include "Profiles.hpp"
#include "Social.hpp"
#include "Spotify.hpp"

extern "C" {
    extern float g_simHour;                            // v81: Theme.cpp (« demo heure / batterie / charge »)
    extern int g_simBat, g_simCharge;
    static int s_running = 0;                          // v78: « demo jeu » - a game is suspended (the close-it popup)
    int qext_game_running(void) { return s_running; }
    // v89 - the loader's application, as qlaunch-ext does it (loader/src/qext.cpp): a launch that goes well runs and has the
    // screen; « demo home » suspends it (onOpen); resume gives it the screen back; terminate closes it - and the loader, at its
    // next turn, sees « the game exited » and calls onOpen (s_exited)
    static int s_fg = 0, s_exited = 0;
    static u64 s_sus = 0;
    int qext_game_has_foreground(void) { return s_running && s_fg; }
    u64 qext_suspended_title(void) { return s_running ? s_sus : 0; }
    Result qext_launch_title(u64 tid) { s_exited = 0; s_running = 1; s_fg = 1; s_sus = tid; return 0; }   // (closed then started in the same turn: no « exited »)
    Result qext_launch_title_as(u64 tid, AccountUid) { return qext_launch_title(tid); }
    // v95 - the console's keyboard: a friend code typed (the Amis page's « Ajouter un ami »)
    struct BrumeSwkbdOptions { const char *initialText, *headerText; unsigned minLength, maxLength; int blurBackground; };
    int qext_swkbd_show_ex(const BrumeSwkbdOptions *, char *out, unsigned cap) { if (!cap) return 0; snprintf(out, cap, "SW-4821-0937-1166"); return (int)strlen(out); }
    Result qext_resume_game(void) { if (!s_running) return 1; s_fg = 1; return 0; }
    Result qext_terminate_game(void) { if (s_running) { s_running = 0; s_fg = 0; s_sus = 0; s_exited = 1; } return 0; }
    FILE *g_simOut = nullptr;
    const char *g_simDir = ".";
    char *fake_heap_start = nullptr, *fake_heap_end = nullptr;
    int onBoot(NWindow *win);
    int loop(void);
    void onOpen(void);
    void onPowerButton(void);                          // v86: « demo veille »
    void sim_lock_boot(void);                          // v87 (Entry.cpp)

    static uint64_t s_frame = 0, s_ticks = 0;
    static int s_slow = 0;                             // v80: frames still to make slow (« demo lent »)
    unsigned long long sim_tick(void) { return s_ticks; }   // 19.2 MHz: 320000 a frame (60 FPS), 1344000 a slow one (70 ms)
    // v80: « demo titre » - one Switch game installed (its launch « succeeds »: the qext_launch_* stubs return 0)
    static int s_title = 0;
    int qext_title_count(void) { return s_title; }
    u64 qext_title_id(int i) { return s_title && i == 0 ? 0x0100000000010000ULL : 0; }
    int qext_title_name(int i, char *out, unsigned cap) {
        if (!s_title || i != 0 || !cap) return 0;
        snprintf(out, cap, "Jeu Switch (essai)");
        return (int)strlen(out);
    }

    static uint64_t s_buttons = 0;
    static int s_touch = 0, s_tx = 0, s_ty = 0;
    void padUpdate(PadState *pad) { pad->buttons_old = pad->buttons_cur; pad->buttons_cur = s_buttons; pad->style_set = HidNpadStyleTag_NpadHandheld; }
    // v92 - a made-up HID (the menu reads every controller itself now, Pads::Poll): the console with its two Joy-Con on the
    // rails at the start. « demo rail <0..3> »: which Joy-Con are on the rails (bit 0 the left one, bit 1 the right one); the
    // ones taken off connect by themselves 1.2 s later, as a pair on player 1 (« demo paire 0 »: they stay off - asleep);
    // « demo pro 1 | 0 »: a Pro Controller on player 2 connects / leaves. The script's buttons come from every controller there.
    static int s_rail = 3, s_pairOk = 1, s_pro = 0;
    static uint64_t s_railFrame = 0;
    static int SimPairSides() { return (s_pairOk && s_rail != 3 && s_frame >= s_railFrame + 72) ? (~s_rail & 3) : 0; }
    // v94: « demo stickx | sticky <-1..1> »: the right stick held there (every controller); « demo couleurs 0 | 1 »: the Joy-Con's
    // colours (neon blue / red, or pink / green); a side that is not there answers the grey of the real console (what Yass saw)
    static float s_simStk[2] = { 0, 0 };
    static int s_colSet = 0;
    u32 hidGetNpadStyleSet(HidNpadIdType id) {
        if (id == HidNpadIdType_Handheld) return s_rail ? HidNpadStyleTag_NpadHandheld : 0;
        if (id == HidNpadIdType_No1) return SimPairSides() ? HidNpadStyleTag_NpadJoyDual : 0;
        if (id == HidNpadIdType_No2) return s_pro ? HidNpadStyleTag_NpadFullKey : 0;
        return 0;
    }
    static size_t SimState(HidNpadIdType id, u32 style, HidNpadCommonState *st, size_t count) {
        if (!count) return 0;
        memset(st, 0, sizeof *st);
        if (!(hidGetNpadStyleSet(id) & style)) return 1;    // (a real npad answers even when nobody is there: not connected)
        st->buttons = s_buttons;
        st->attributes = HidNpadAttribute_IsConnected;      // (the console's npad says no more, as on the real one - journal v41.1)
        if (id == HidNpadIdType_No1) st->attributes |= ((SimPairSides() & 1) ? HidNpadAttribute_IsLeftConnected : 0) | ((SimPairSides() & 2) ? HidNpadAttribute_IsRightConnected : 0);
        st->analog_stick_r.x = (s32)(s_simStk[0] * 32767); st->analog_stick_r.y = (s32)(s_simStk[1] * 32767);   // v94: « demo stickx / sticky »
        return 1;
    }
    size_t hidGetNpadStatesHandheld(HidNpadIdType id, HidNpadHandheldState *s, size_t n) { return SimState(id, HidNpadStyleTag_NpadHandheld, s, n); }
    size_t hidGetNpadStatesJoyDual(HidNpadIdType id, HidNpadJoyDualState *s, size_t n) { return SimState(id, HidNpadStyleTag_NpadJoyDual, s, n); }
    size_t hidGetNpadStatesFullKey(HidNpadIdType id, HidNpadFullKeyState *s, size_t n) { return SimState(id, HidNpadStyleTag_NpadFullKey, s, n); }
    size_t hidGetNpadStatesJoyLeft(HidNpadIdType id, HidNpadJoyLeftState *s, size_t n) { return SimState(id, HidNpadStyleTag_NpadJoyLeft, s, n); }
    size_t hidGetNpadStatesJoyRight(HidNpadIdType id, HidNpadJoyRightState *s, size_t n) { return SimState(id, HidNpadStyleTag_NpadJoyRight, s, n); }
    u32 hidGetNpadDeviceType(HidNpadIdType id) {
        if (!hidGetNpadStyleSet(id)) return 0;
        if (id == HidNpadIdType_Handheld) return ((s_rail & 1) ? HidDeviceTypeBits_HandheldLeft : 0) | ((s_rail & 2) ? HidDeviceTypeBits_HandheldRight : 0);
        if (id == HidNpadIdType_No1) return ((SimPairSides() & 1) ? HidDeviceTypeBits_JoyLeft : 0) | ((SimPairSides() & 2) ? HidDeviceTypeBits_JoyRight : 0);
        return HidDeviceTypeBits_FullKey;
    }
    HidAppletFooterUiType hidGetAppletFooterUiTypes(HidNpadIdType id) {
        if (!hidGetNpadStyleSet(id)) return HidAppletFooterUiType_None;
        if (id == HidNpadIdType_Handheld) return s_rail == 3 ? HidAppletFooterUiType_HandheldJoyConLeftJoyConRight : s_rail == 1 ? HidAppletFooterUiType_HandheldJoyConLeftOnly : HidAppletFooterUiType_HandheldJoyConRightOnly;
        if (id == HidNpadIdType_No1) return SimPairSides() == 3 ? HidAppletFooterUiType_JoyDual : SimPairSides() == 1 ? HidAppletFooterUiType_JoyDualLeftOnly : HidAppletFooterUiType_JoyDualRightOnly;
        return HidAppletFooterUiType_SwitchProController;
    }
    Result hidGetNpadControllerColorSplit(HidNpadIdType id, HidNpadControllerColor *l, HidNpadControllerColor *r) {
        if (s_colSet) { l->main = 0x7832FF; l->sub = 0x281E1E; r->main = 0x1EDC1E; r->sub = 0x1E281E; }   // neon pink / neon green
        else { l->main = 0xE6B90A; l->sub = 0x1E1E1E; r->main = 0x283CFF; r->sub = 0x1E1E1E; }          // neon blue / neon red (red in the low byte)
        const int sides = id == HidNpadIdType_Handheld ? s_rail : SimPairSides();
        if (!(sides & 1)) { l->main = 0x828282; l->sub = 0x0F0F0F; }
        if (!(sides & 2)) { r->main = 0x828282; r->sub = 0x0F0F0F; }
        return 0;
    }
    Result hidGetNpadControllerColorSingle(HidNpadIdType, HidNpadControllerColor *c) { c->main = 0x2D2D2D; c->sub = 0xE6E6E6; return 0; }
    void hidGetNpadPowerInfoSplit(HidNpadIdType id, HidPowerInfo *l, HidPowerInfo *r) {
        memset(l, 0, sizeof *l); memset(r, 0, sizeof *r);
        l->battery_level = 3; r->battery_level = 4;
        l->is_charging = r->is_charging = id == HidNpadIdType_Handheld;
    }
    void hidGetNpadPowerInfoSingle(HidNpadIdType, HidPowerInfo *p) { memset(p, 0, sizeof *p); p->battery_level = 2; }
    // 4 made-up profiles (names only, no avatar), for the Profiles panel and the profile's pill
    static const char *const kNick[4] = { "Nova", "Mika", "Sora","Invit\xc3\xa9" };
    static u64 s_profUid = 0;
    Result accountListAllUsers(AccountUid *uids, s32 max, s32 *total) {
        const s32 n = max < 4 ? max : 4;
        for (s32 i = 0; i < n; i++) { uids[i].uid[0] = (u64)i + 1; uids[i].uid[1] = 0x53494D55; }
        for (s32 i = n; i < max; i++) uids[i].uid[0] = uids[i].uid[1] = 0;
        *total = n;
        return 0;
    }
    Result accountGetLastOpenedUser(AccountUid *uid) { uid->uid[0] = 1; uid->uid[1] = 0x53494D55; return 0; }
    Result accountGetProfile(AccountProfile *out, AccountUid uid) { memset(out, 0, sizeof *out); s_profUid = uid.uid[0]; return 0; }
    Result accountProfileGet(AccountProfile *, AccountUserData *, AccountProfileBase *base) {
        if (base && s_profUid >= 1 && s_profUid <= 4) { memset(base, 0, sizeof *base); snprintf(base->nickname, sizeof base->nickname, "%s", kNick[s_profUid - 1]); }
        return 0;
    }
    void accountProfileClose(AccountProfile *) {}
    // v83 - the clock (Réglages > Système > Date et heure): a user clock that runs with the frames (Tue 17 Nov 2026, 21:42 in
    // Paris at the start), a few places of the time service's list (one bad entry: it must be left out), their offsets
    static long long s_simClock = 1794948120LL;        // 2026-11-17 20:42:00 UTC
    static uint64_t s_simClockFrame = 0;
    static const struct { const char *n; int off; } kSimZones[] = {
        { "Africa/Abidjan", 0 }, { "Africa/Cairo", 7200 }, { "Africa/Johannesburg", 7200 }, { "Africa/Lagos", 3600 }, { "Africa/Nairobi", 10800 },
        { "America/Argentina/Buenos_Aires", -10800 }, { "America/Chicago", -21600 }, { "America/Denver", -25200 }, { "America/Los_Angeles", -28800 },
        { "America/Mexico_City", -21600 }, { "America/Montreal", -18000 }, { "America/New_York", -18000 }, { "America/Sao_Paulo", -10800 },
        { "America/St_Johns", -12600 }, { "America/Toronto", -18000 }, { "America/Vancouver", -28800 },
        { "Asia/Bangkok", 25200 }, { "Asia/Dubai", 14400 }, { "Asia/Ho_Chi_Minh", 25200 }, { "Asia/Hong_Kong", 28800 }, { "Asia/Kolkata", 19800 },
        { "Asia/Seoul", 32400 }, { "Asia/Shanghai", 28800 }, { "Asia/Singapore", 28800 }, { "Asia/Tokyo", 32400 },
        { "\x01\x02bad", 0 },                          // (not a place: left out)
        { "Atlantic/Azores", -3600 }, { "Atlantic/Reykjavik", 0 }, { "Australia/Perth", 28800 }, { "Australia/Sydney", 39600 },
        { "Cuba", -18000 }, { "Etc/GMT+5", -18000 }, { "Etc/UTC", 0 },
        { "Europe/Amsterdam", 3600 }, { "Europe/Athens", 7200 }, { "Europe/Berlin", 3600 }, { "Europe/Brussels", 3600 }, { "Europe/Dublin", 0 },
        { "Europe/Istanbul", 10800 }, { "Europe/Lisbon", 0 }, { "Europe/London", 0 }, { "Europe/Madrid", 3600 }, { "Europe/Moscow", 10800 },
        { "Europe/Paris", 3600 }, { "Europe/Rome", 3600 }, { "Europe/Stockholm", 3600 }, { "Europe/Zurich", 3600 },
        { "Indian/Maldives", 18000 }, { "Indian/Reunion", 14400 }, { "Japan", 32400 },
        { "Pacific/Auckland", 46800 }, { "Pacific/Honolulu", -36000 }, { "Pacific/Tahiti", -36000 }, { "UTC", 0 } };
    static int s_simZone = 43;                          // Europe/Paris
    static int SimZoneOff(const char *n) {
        for (auto &z : kSimZones) if (!strncmp(z.n, n, 0x24)) return z.off;
        return 0;
    }
    static void SimCal(u64 ts, int off, TimeCalendarTime *c, TimeCalendarAdditionalInfo *info) {
        const time_t t = (time_t)((long long)ts + off);
        struct tm g; gmtime_r(&t, &g);
        if (c) { c->year = (u16)(g.tm_year + 1900); c->month = (u8)(g.tm_mon + 1); c->day = (u8)g.tm_mday; c->hour = (u8)g.tm_hour; c->minute = (u8)g.tm_min; c->second = (u8)g.tm_sec; c->pad = 0; }
        if (info) { memset(info, 0, sizeof *info); info->wday = (u32)g.tm_wday; info->yday = (u32)g.tm_yday; info->offset = off; snprintf(info->timezoneName, sizeof info->timezoneName, "SIM"); }
    }
    static long long SimNow() { return s_simClock + (long long)((s_frame - s_simClockFrame) / 60); }
    void sim_set_user_clock(unsigned long long posix) { s_simClock = (long long)posix; s_simClockFrame = s_frame; }
    void sim_set_zone(const char *name) { for (int i = 0; i < (int)(sizeof kSimZones / sizeof kSimZones[0]); i++) if (!strncmp(kSimZones[i].n, name, 0x24)) s_simZone = i; }
    Result timeGetCurrentTime(TimeType, u64 *ts) { *ts = (u64)SimNow(); return 0; }
    Result timeToCalendarTimeWithMyRule(u64 ts, TimeCalendarTime *c, TimeCalendarAdditionalInfo *info) { SimCal(ts, kSimZones[s_simZone].off, c, info); return 0; }
    Result timeToPosixTimeWithMyRule(const TimeCalendarTime *c, u64 *list, s32 n, s32 *count) {
        struct tm g = {}; g.tm_year = c->year - 1900; g.tm_mon = c->month - 1; g.tm_mday = c->day; g.tm_hour = c->hour; g.tm_min = c->minute; g.tm_sec = c->second;
        if (n >= 1) list[0] = (u64)((long long)timegm(&g) - kSimZones[s_simZone].off);
        *count = n >= 1 ? 1 : 0;
        return 0;
    }
    Result timeGetTotalLocationNameCount(s32 *n) { *n = (s32)(sizeof kSimZones / sizeof kSimZones[0]); return 0; }
    Result timeLoadLocationNameList(s32 index, TimeLocationName *arr, s32 max, s32 *count) {
        const s32 total = (s32)(sizeof kSimZones / sizeof kSimZones[0]);
        s32 k = 0;
        for (; k < max && index + k < total; k++) {
            memset(&arr[k], 0, sizeof arr[k]);
            if (kSimZones[index + k].n[0] == '\x01') memset(arr[k].name, 0x7F, sizeof arr[k].name);   // (unterminated garbage)
            else snprintf(arr[k].name, sizeof arr[k].name, "%s", kSimZones[index + k].n);
        }
        *count = k;
        return 0;
    }
    Result timeGetDeviceLocationName(TimeLocationName *n) { memset(n, 0, sizeof *n); snprintf(n->name, sizeof n->name, "%s", kSimZones[s_simZone].n); return 0; }
    Result timeLoadTimeZoneRule(const TimeLocationName *n, TimeZoneRule *r) { const int off = SimZoneOff(n->name); memcpy(r->data, &off, sizeof off); return 0; }
    Result timeToCalendarTime(const TimeZoneRule *r, u64 ts, TimeCalendarTime *c, TimeCalendarAdditionalInfo *info) { int off; memcpy(&off, r->data, sizeof off); SimCal(ts, off, c, info); return 0; }
    size_t hidGetTouchScreenStates(HidTouchScreenState *states, size_t count) {
        if (!count) return 0;
        memset(states, 0, sizeof *states);
        states->count = s_touch ? 1 : 0;
        states->touches[0].x = (u32)s_tx; states->touches[0].y = (u32)s_ty;
        return 1;
    }
}

namespace {
    struct Ev { int frame; std::string what, arg; int x, y; float val; };
    uint64_t Button(const std::string &b) {
        static const struct { const char *n; uint64_t v; } k[] = {
            { "A", HidNpadButton_A }, { "B", HidNpadButton_B }, { "X", HidNpadButton_X }, { "Y", HidNpadButton_Y },
            { "L", HidNpadButton_L }, { "R", HidNpadButton_R }, { "ZL", HidNpadButton_ZL }, { "ZR", HidNpadButton_ZR },
            { "Plus", HidNpadButton_Plus }, { "Minus", HidNpadButton_Minus }, { "Up", HidNpadButton_Up },
            { "Down", HidNpadButton_Down }, { "Left", HidNpadButton_Left }, { "Right", HidNpadButton_Right } };
        for (auto &e : k) if (b == e.n) return e.v;
        fprintf(stderr, "bouton inconnu : %s\n", b.c_str());
        return 0;
    }
}

extern double g_simFill[40];
extern int g_simQuads[40], g_simMeshes, g_simMeshIdx, g_simProgs;
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uisim <script> <out dir>\n"); return 1; }
    g_simDir = argv[2];
    std::vector<Ev> evs;
    int last = 0;
    if (FILE *f = fopen(argv[1], "r")) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
            float t = 0, val = 0; char w[32] = {}, a[64] = {};
            const int n = sscanf(line, "%f %31s %63s %f", &t, w, a, &val);
            if (n < 2) continue;
            Ev e{ (int)(t * 60.0f + 0.5f), w, a, 0, 0, val };
            if (e.what == "touch") { e.x = atoi(a); e.y = (int)val; }
            evs.push_back(e);
            if (e.frame > last) last = e.frame;
        }
        fclose(f);
    } else { fprintf(stderr, "pas de script %s\n", argv[1]); return 1; }

    if (onBoot(nullptr) != 0) { fprintf(stderr, "onBoot a echoue\n"); return 1; }
    std::string index = "[";
    for (s_frame = 1; (int)s_frame <= last + 2; s_frame++) {
        const int f = (int)s_frame;
        s_buttons = 0; s_touch = 0;
        std::string shot;
        for (auto &e : evs) {
            if (e.what == "press" && f >= e.frame && f < e.frame + 2) s_buttons |= Button(e.arg);
            if (e.what == "hold" && f >= e.frame && f < e.frame + (int)(e.val * 60.0f + 0.5f)) s_buttons |= Button(e.arg);   // v84
            if (e.what == "touch" && f >= e.frame && f < e.frame + 2) { s_touch = 1; s_tx = e.x; s_ty = e.y; }
            if (e.what == "shot" && f == e.frame) shot = e.arg;
            if (e.what == "demo" && f == e.frame) {          // v77: the task centre and the notifications, without a network
                const float t = f / 60.0f;
                static int a = -1, b = -1, n = -1;
                if (e.arg == "taches") {
                    a = brume::tasks::Begin(t, "Jaquettes");
                    brume::tasks::Progress(a, t, 3, 7, "SSX 3 (USA)");
                    b = brume::tasks::Begin(t, "Joueurs en ligne (Nextendo)");
                    brume::tasks::End(b, t, false, "pas de r\xc3\xa9ponse d'Internet");
                    n = brume::notify::Open(t, "Jaquettes : 3 / 7", brume::notify::kDownload);
                    brume::notify::Set(n, t, "Jaquettes : 3 / 7", 3.0f / 7.0f);
                    brume::notify::Post(t, "Heure r\xc3\xa9gl\xc3\xa9" "e : 21:42");
                } else if (e.arg == "fin") {
                    brume::tasks::End(a, t, true);
                    brume::notify::Close(n, t, "Jaquettes : 4 nouvelle(s)", brume::notify::kDone);
                } else if (e.arg == "erreur") {             // v78: the popups that need the console to happen
                    brume::popup::Error(t, "Le jeu n'a pas pu d\xc3\xa9marrer", "La console a refus\xc3\xa9 de le lancer.", 0x6C10);
                } else if (e.arg == "redemarrer") {
                    brume::popup::Spec s;
                    s.kind = brume::popup::kAsk; s.title = "Red\xc3\xa9marrer maintenant ?";
                    s.text = "L'overlay Brume arrivera au prochain d\xc3\xa9marrage. Secours : L + R + Moins pendant 3 s.";
                    s.yes = "Red\xc3\xa9marrer"; s.no = "Plus tard";
                    brume::popup::Show(t, s);
                } else if (e.arg == "jeu") s_running = 1;  // a game is suspended (HOME during it)
                else if (e.arg == "sansjeu") s_running = 0;
                else if (e.arg == "titre") s_title = 1;    // v80
                else if (e.arg == "retour") { s_running = 0; s_fg = 0; onOpen(); }   // (v89: the game ended - by itself, or closed elsewhere)
                else if (e.arg == "home") { s_fg = 0; onOpen(); }               // v89: HOME during the game - it stays suspended
                else if (e.arg == "veille") onPowerButton();   // v86 (revue): the console sleeps (and wakes at once)
                else if (e.arg == "lent") s_slow = 4;
                else if (e.arg == "rail") { const int r = (int)e.val & 3; if (r != s_rail && (s_rail == 3 || SimPairSides() == 0)) s_railFrame = s_frame; s_rail = r; }   // v92
                else if (e.arg == "paire") s_pairOk = e.val > 0.5f;
                else if (e.arg == "pro") s_pro = e.val > 0.5f;
                else if (e.arg == "stickx") s_simStk[0] = e.val;       // v94
                else if (e.arg == "sticky") s_simStk[1] = e.val;
                else if (e.arg == "couleurs") s_colSet = (int)e.val;
                else if (e.arg == "amis" || e.arg == "discord" || e.arg == "profil") brume::social::SimDemo(e.arg.c_str(), e.val, t);   // v95 (v96: profil)
                else if (e.arg == "theme") brume::Settings::ApplyTheme((int)e.val);   // v81
                else if (e.arg == "heure") g_simHour = e.val;
                else if (e.arg == "batterie") g_simBat = (int)e.val;
                else if (e.arg == "charge") g_simCharge = (int)e.val;
                else if (e.arg == "ciel") { brume::Settings::s_sky = e.val > 0.5f; if (!brume::Settings::s_sky) brume::Settings::ApplyTheme(brume::theme::Current()); }
                else if (e.arg == "brumebat") brume::Settings::s_battery = e.val > 0.5f;
                else if (e.arg == "h24") brume::sysclock::s_h24 = e.val > 0.5f;   // v83
                else if (e.arg == "verrou") sim_lock_boot();   // v87
                else if (e.arg == "spotify") brume::spotify::SimSet((int)e.val);   // v98: 0 off, 1 pairing, 2 playing, 3 paused, 4 refused, 5 not installed, 6 starting
                else if (e.arg == "pin") {                   // v87: « demo pin 0213 » (the 4 symbols are the number's digits)
                    const int v = (int)e.val; uint8_t c[4] = { (uint8_t)(v / 1000 % 10), (uint8_t)(v / 100 % 10), (uint8_t)(v / 10 % 10), (uint8_t)(v % 10) };
                    brume::pin::Set(brume::Profiles::Active(), c);
                }
            }
            // v90: « <s> pose sdmc:/switch/x/y.nro » puts an (empty) file on the fake card, folders included; « <s> enleve <path> »
            // takes it away (the emulators found by themselves: then « demo retour », the menu looks at the card again)
            if ((e.what == "pose" || e.what == "enleve") && f == e.frame && e.arg.rfind("sdmc:/", 0) == 0 && e.arg.find("..") == std::string::npos) {
                if (e.what == "pose") {
                    for (size_t k = 6; (k = e.arg.find('/', k)) != std::string::npos; k++) mkdir(e.arg.substr(0, k).c_str(), 0777);
                    if (FILE *nf = fopen(e.arg.c_str(), "wb")) fclose(nf);
                } else remove(e.arg.c_str());
            }
        }
        if (!shot.empty()) {
            const std::string p = std::string(g_simDir) + "/" + shot + ".bin";
            g_simOut = fopen(p.c_str(), "wb");
            if (index.size() > 1) index += ",";
            index += "\"" + shot + "\"";
        }
        s_ticks += s_slow > 0 ? 1344000ULL : 320000ULL;   // (v80: the frame's length; « lent »: 70 ms)
        if (s_slow > 0) s_slow--;
        if (s_exited) { s_exited = 0; onOpen(); }         // v89: the loader saw the game exit (after a terminate)
        memset(g_simFill, 0, sizeof g_simFill); memset(g_simQuads, 0, sizeof g_simQuads); g_simMeshes = g_simMeshIdx = g_simProgs = 0;
        loop();
        if (g_simOut) {                                   // (perf) the interface's fill (full screens of pixels), by mode
            double tot = 0; for (double x : g_simFill) tot += x;
            printf("  remplissage %s : %.2f ecrans", shot.c_str(), tot / (1280.0 * 720.0));
            for (int m = 0; m < 40; m++) if (g_simFill[m] > 1280.0 * 720.0 * 0.02) printf(" | m%d %.2f (%d)", m, g_simFill[m] / (1280.0 * 720.0), g_simQuads[m]);
            printf(" | 3D %d dessins, %d indices | %d programmes\n", g_simMeshes, g_simMeshIdx, g_simProgs);
        }
        if (getenv("UISIM_AMB") && (f % 60) == 0) printf("t=%.0f ambient %.2f %.2f %.2f target %.2f %.2f %.2f\n", f / 60.0, brume::Settings::s_ambient[0], brume::Settings::s_ambient[1], brume::Settings::s_ambient[2], brume::Settings::s_ambientTarget[0], brume::Settings::s_ambientTarget[1], brume::Settings::s_ambientTarget[2]);
        if (g_simOut) { fclose(g_simOut); g_simOut = nullptr; printf("image : %s (t = %.2f s)\n", shot.c_str(), f / 60.0); }
    }
    index += "]";
    if (FILE *f = fopen((std::string(g_simDir) + "/index.json").c_str(), "w")) { fputs(index.c_str(), f); fclose(f); }
    return 0;
}
