// Brume - v83: the console's clock, written by the menu (Nintendo's Date & Time page lives inside the HOME menu Brume
// replaces). One place for every write, shared by the clock set by Internet (Ntp.cpp) and Brume's own pages
// (DatePages.cpp: Réglages > Système > Date et heure). Writes go to the emuMMC only (the system's time services).
//  - The user clock (what the console shows) and the device's time zone are written through time:a, the service of
//    Nintendo's own Date & Time page: the loader's time:s may write the network clock only (0x274 = permission denied).
//  - The 12 h / 24 h format is Brume's own (the Switch has no such setting): the lock screen and the top bar follow it.
#pragma once
#include <switch.h>

namespace brume::sysclock {

    // the user clock: automatic correction off (in the time service now, and in the settings for the next boot), then the
    // time written; good = it reads back within 2 s of what was written. Each step's code, for perf.log
    struct Write { Result svc = 0, autoOff = 0, clock = 0, set = 0; bool good = false; };
    Write SetUserClock(u64 posix);

    // the device's time zone (a name of the time service's list, « Europe/Paris »): live = the time service took it at once
    // (time:a: the shown clock follows now); else only the setting was written (it applies at the next boot). The code of
    // the step that failed (0: written)
    Result SetZone(const char *location, bool *live);

    // the local time now (the user clock, the device's zone); false = unknown (the fields are zeroed)
    bool Now(TimeCalendarTime &cal, TimeCalendarAdditionalInfo &info, u64 *posix = nullptr);
    int  DaysIn(int year, int month);                    // 28..31
    int  WeekDay(int year, int month, int day);          // 0 Sunday .. 6 Saturday

    // the hour as Brume writes it: « 21:42 » (24 h) or « 9:42 PM » (12 h)
    inline bool s_h24 = true;
    void HourText(char *out, int cap, int hour, int minute);
    bool DefaultH24();                                   // no choice saved yet: 12 h for American English, else 24 h

    inline void (*s_log)(const char *) = nullptr;       // perf.log (Entry)

} // namespace brume::sysclock
