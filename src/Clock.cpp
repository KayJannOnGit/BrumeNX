// Brume - v83: the console's clock, written by the menu. See Clock.hpp.
#include "Clock.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef BRUME_UISIM
extern "C" { void sim_set_user_clock(unsigned long long posix); void sim_set_zone(const char *name); }   // tools/uisim/main.cpp
#endif

namespace brume::sysclock {

    namespace {
        void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
        void Log(const char *fmt, ...) {
            if (!s_log) return;
            char m[200];
            va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
            s_log(m);
        }
    }

    Write SetUserClock(u64 posix) {
        Write w;
        w.set = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
#ifdef BRUME_UISIM
        sim_set_user_clock(posix); w.svc = w.autoOff = w.clock = w.set = 0;
#else
        // (v73.2, perf.log on the console: time:s may write the network clock but not the user clock, 0x274 = permission
        // denied) the service of Nintendo's own Date & Time page. Its automatic correction goes off first (it would bring
        // back the network clock, which this emuMMC never reaches): in the time service now (cmd 101), in the settings below
        Service ta;
        if (R_SUCCEEDED(w.svc = smGetService(&ta, "time:a"))) {
            const u8 off = 0;
            w.autoOff = serviceDispatchIn(&ta, 101, off);              // SetStandardUserSystemClockAutomaticCorrectionEnabled
            Service clk;
            if (R_SUCCEEDED(w.clock = serviceDispatch(&ta, 0, .out_num_objects = 1, .out_objects = &clk))) {   // GetStandardUserSystemClock
                w.set = serviceDispatchIn(&clk, 1, posix);             // ISystemClock::SetCurrentTime
                serviceClose(&clk);
            }
            serviceClose(&ta);
        }
        setsysSetUserSystemClockAutomaticCorrectionEnabled(false);
#endif
        u64 after = 0;
        w.good = R_SUCCEEDED(w.set) && R_SUCCEEDED(timeGetCurrentTime(TimeType_UserSystemClock, &after))
                 && (after > posix ? after - posix : posix - after) <= 2;
        return w;
    }

    Result SetZone(const char *location, bool *live) {
        if (live) *live = false;
        TimeLocationName n;
        memset(&n, 0, sizeof n);
        if (!location || !location[0]) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        snprintf(n.name, sizeof n.name, "%s", location);
#ifdef BRUME_UISIM
        sim_set_zone(n.name);
        if (live) *live = true;
        return 0;
#else
        // time:a's ITimeZoneService (IStaticService cmd 3), cmd 1 SetDeviceLocationName: the rule of that place at once (the
        // shown clock follows), and the time service keeps it in the settings. The settings are written here too (the same
        // value): if the time service refused, the zone still comes at the next boot
        Result rs = 0, rz = MAKERESULT(Module_Libnx, LibnxError_NotInitialized), rn = rz;
        Service ta;
        if (R_SUCCEEDED(rs = smGetService(&ta, "time:a"))) {
            Service tz;
            if (R_SUCCEEDED(rz = serviceDispatch(&ta, 3, .out_num_objects = 1, .out_objects = &tz))) {   // GetTimeZoneService
                rn = serviceDispatchIn(&tz, 1, n);                     // ITimeZoneService::SetDeviceLocationName
                serviceClose(&tz);
            }
            serviceClose(&ta);
        }
        const Result rset = setsysSetDeviceTimeZoneLocationName(&n);
        TimeLocationName back;
        memset(&back, 0, sizeof back);
        const bool now = R_SUCCEEDED(rn) && R_SUCCEEDED(timeGetDeviceLocationName(&back)) && !strncmp(back.name, n.name, sizeof n.name);
        if (live) *live = now;
        Log("fuseau : %s %s time:a=0x%x service=0x%x ecrit=0x%x reglage=0x%x", n.name, now ? "pris tout de suite" : "PAS pris tout de suite", rs, rz, rn, rset);   // lang:log (never drawn)
        if (now) return 0;
        return R_FAILED(rset) ? rset : 0;
#endif
    }

    bool Now(TimeCalendarTime &cal, TimeCalendarAdditionalInfo &info, u64 *posix) {
        memset(&cal, 0, sizeof cal); memset(&info, 0, sizeof info);
        u64 ts = 0;
        if (posix) *posix = 0;
        if (R_FAILED(timeGetCurrentTime(TimeType_UserSystemClock, &ts))) return false;
        if (R_FAILED(timeToCalendarTimeWithMyRule(ts, &cal, &info))) { memset(&cal, 0, sizeof cal); memset(&info, 0, sizeof info); return false; }
        if (posix) *posix = ts;
        return true;
    }

    int DaysIn(int y, int m) {
        static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        if (m < 1 || m > 12) return 31;
        if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
        return kDays[m - 1];
    }

    int WeekDay(int y, int m, int d) {                   // (Sakamoto's method)
        static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
        if (m < 1 || m > 12) return 0;
        if (m < 3) y -= 1;
        return ((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 7) % 7;
    }

    void HourText(char *out, int cap, int hour, int minute) {
        if (!out || cap <= 0) return;
        hour = hour < 0 ? 0 : (hour > 23 ? 23 : hour);
        minute = minute < 0 ? 0 : (minute > 59 ? 59 : minute);
        if (s_h24) snprintf(out, (size_t)cap, "%02d:%02d", hour, minute);
        else snprintf(out, (size_t)cap, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
    }

    bool DefaultH24() {
        u64 code = 0; SetLanguage l = SetLanguage_FR;
        if (R_SUCCEEDED(setGetLanguageCode(&code)) && R_SUCCEEDED(setMakeLanguage(code, &l))) return l != SetLanguage_ENUS;
        return true;
    }

} // namespace brume::sysclock
