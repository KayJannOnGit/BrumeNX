// Brume - v49: Nextendo (Yass, 2026-11-16: « faut aussi intégrer Nextendo », the community's online network).
// For now: how many players are online in each game (the public counts nextendo.network's own pages show:
// https://nextendo.network/api/online-counts and /service-catalog/v1/online-counts, {"counts":{"<title id>":n,...}}),
// shown under a Switch game's box (« 13 EN LIGNE »).
// The network rules (CLAUDE.md section 7): HTTPS through the console's own TLS service (ssl:, the peer's certificate is
// checked; only if the console's store refuses it, the public counts are read without that check - nothing is sent but
// the request), nothing about Yass sent anywhere, a worker thread, once every 5 minutes and only while the menu is on
// screen (the loader does not run the menu while a game is in front), small socket buffers (~40 KB, freed after).
// Nextendo's code is PolyForm Shield (not free): nothing of it is copied, only its public data is read.
// Réglages > Réseau > « Joueurs en ligne (Nextendo) » switches it off.
#pragma once
#include <switch.h>

namespace brume {

    namespace nextendo {
        void Update(float now, bool menuOnScreen);   // starts a fetch when it is time, collects the result (main thread)
        int  Players(u64 titleId);                    // players online now (-1: unknown)
        void Shutdown();                              // v68: waits for the worker (before the menu is unloaded)
        // v72.1 - the clock (Ntp.hpp) takes the « Date: » of these answers: a fetch also runs when the clock wants it (even
        // with the counts switched off), and this asks for one at once (« Mettre à l'heure maintenant »)
        void RequestNow();
        bool Busy();                                  // v75: a fetch is on its way (the covers' downloader waits: one at a time)
        // v75 - one HTTPS request through the console's TLS service, the answer handed to sink as it arrives (headers
        // included; sink returns < 0 to stop). Sockets and ssl must be initialised by the caller's thread. -1: no answer.
        int Https(const char *host, const char *path, bool verify, const char *accept, int (*sink)(void *, const char *, int), void *user,
                  char *why, int whyCap, const char *method = "GET", u64 *firstTick = nullptr,
                  const char *headers = nullptr, const char *body = nullptr);   // v95: extra header lines (each ends with \r\n), a body
        // v95 (Social.cpp, the friends) - one call to nextendo.network, the certificate ALWAYS checked (a token never goes
        // anywhere unchecked): the whole answer into buf (cap bytes, bounded), then its HTTP status (-1: no answer, why says
        // why), *body / *bodyLen its body (dechunked, NUL-terminated), *retryAfter the seconds a 429 asks to wait (0: none).
        // Sockets and ssl must be initialised by the caller's thread.
        int Call(const char *method, const char *path, const char *headers, const char *body, char *buf, int cap,
                 char **bodyOut, int *bodyLen, int *retryAfter, char *why, int whyCap);
        extern void (*s_log)(const char *);
    }

} // namespace brume
