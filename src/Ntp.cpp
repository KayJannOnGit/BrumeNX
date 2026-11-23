// Brume - the console's clock, from the Date of Nextendo's HTTPS answer. See Ntp.hpp.
#include "Ntp.hpp"
#include "Clock.hpp"
#include "Lang.hpp"
#include "Nextendo.hpp"
#include "Settings.hpp"
#include <cstdio>

extern "C" int qext_game_running(void);

namespace brume::ntp {

    void (*s_log)(const char *) = nullptr;
    void (*s_toast)(float, const char *) = nullptr;

    namespace {
        bool s_bootDone = false;                         // set once this boot (or the option is off)
        bool s_asked = false;                            // the row asked: always say the result
        void Say(float now, const char *msg) { if (s_toast) s_toast(now, msg); }
        bool Online() {
            NifmInternetConnectionType t; u32 st = 0; NifmInternetConnectionStatus cs;
            return R_SUCCEEDED(nifmGetInternetConnectionStatus(&t, &st, &cs)) && cs == NifmInternetConnectionStatus_Connected;
        }
    }

    bool Wanted() { return s_asked || (Settings::s_ntpAuto && !s_bootDone); }
    bool Syncing() { return Wanted() && nextendo::Busy(); }

    void SyncNow(float now) {
        if (qext_game_running()) { Say(now, "Pas pendant un jeu"); return; }
        if (!Online()) { Say(now, "Pas de r\xc3\xa9seau"); return; }
        s_asked = true;
        nextendo::RequestNow();                          // (if a fetch is already on its way, its answer will do)
        Say(now, "Je demande l'heure \xc3\xa0 Internet\xe2\x80\xa6");
    }

    void NoDate(const char *why, float now) {
        if (!Wanted()) return;
        char m[160];
        if (s_log) { snprintf(m, sizeof m, "heure : pas de date (%s)", why && why[0] ? why : "?"); s_log(m); }   // lang:log (never drawn)
        if (s_asked) { Say(now, "L'heure n'a pas pu \xc3\xaatre r\xc3\xa9gl\xc3\xa9" "e (pas de r\xc3\xa9ponse d'Internet)"); s_asked = false; }
    }

    void FromDate(long long secs, u64 tick, float now) {
        if (!Wanted() || secs < 1735689600LL || secs > 4102444800LL) return;   // (2025 .. 2100: else it is not a real date)
        const bool asked = s_asked;
        s_asked = false;
        // a Date is a whole second: its middle, plus the time since it arrived
        const u64 ts = (u64)(secs + (long long)((double)(armGetSystemTick() - tick) / (double)armGetSystemTickFreq() + 0.5));
        u64 before = 0;
        const bool had = R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &before));
        // v73.2 (perf.log on the console: time:s may write the network clock but not the user clock, 0x274 = permission
        // denied): the user clock is written through time:a, automatic correction off - v83: Clock.cpp, shared with the
        // pages of Réglages > Système > Date et heure
        const sysclock::Write w = sysclock::SetUserClock(ts);
        const bool good = w.good;
        u64 after = ts;
        if (good) timeGetCurrentTime(TimeType_UserSystemClock, &after);
        const long long moved = had ? (long long)ts - (long long)before : 0;
        char m[160];
        if (s_log) { snprintf(m, sizeof m, "heure : %s par Internet (%+lld s) time:a=0x%x correction=0x%x horloge=0x%x ecrite=0x%x", good ? "reglee" : "PAS reglee", moved, w.svc, w.autoOff, w.clock, w.set); s_log(m); }   // lang:log (never drawn)
        if (good) s_bootDone = true;
        if (!good) { if (asked) Say(now, "L'heure n'a pas pu \xc3\xaatre r\xc3\xa9gl\xc3\xa9" "e"); return; }
        if (!asked && moved > -60 && moved < 60) return;   // (the boot's setting stays silent unless the clock really moved)
        TimeCalendarTime cal; TimeCalendarAdditionalInfo info;
        if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(after, &cal, &info))) {
            char hm[16]; sysclock::HourText(hm, sizeof hm, cal.hour, cal.minute);   // v83: 24 h or 12 h
            snprintf(m, sizeof m, lang::T("Heure r\xc3\xa9gl\xc3\xa9" "e : %s"), hm);
            Say(now, m);
        } else Say(now, "Heure r\xc3\xa9gl\xc3\xa9" "e");
    }

} // namespace brume::ntp
