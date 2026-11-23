// Brume - the console's clock, set by Internet. The emuMMC is cut from Nintendo (its time servers too) and Nintendo's
// Date & Time page lives in the HOME menu Brume replaces, so the menu sets the clock itself.
// v72.1 (Yass: « faut faire marcher un seul truc ») - ONE way, no fallback: the « Date: » header of the HTTPS answer the
// menu already gets from Nextendo (on his console UDP / NTP could not even be sent; that HTTPS request works). Once per boot
// when the network is up (Réglages > Système > Date et heure > Heure automatique), or on demand (« Mettre à l'heure
// maintenant »). The clock written is the user clock (what the console shows) through time:a, automatic correction off.
#pragma once
#include <switch.h>

namespace brume {

    namespace ntp {
        void SyncNow(float now);                              // the row: ask now (a toast says the result)
        bool Wanted();                                        // the clock is waiting for a Date (Nextendo fetches for it)
        bool Syncing();                                       // v87: ... and the fetch is on its way (the lock screen's orbs)
        void FromDate(long long secs, u64 tick, float now);   // main thread: Nextendo's answer had this Date (UTC, at tick)
        void NoDate(const char *why, float now);              // main thread: Nextendo's fetch brought no Date
        extern void (*s_log)(const char *);
        extern void (*s_toast)(float now, const char *msg);
    }

} // namespace brume
