// Brume - v95: the « Amis » page (Yass, 2026-11-22: Nextendo « pour rejoindre les gens, voir les infos », and a Discord voice
// shortcut). Research: docs/RECHERCHE-INTEGRATIONS.md 3.4 and 3.6; Yass's steps: docs/NEXTENDO-APP.md.
//
// NEXTENDO (nextendo.network, the community's online network; its code is PolyForm Shield: nothing of it is copied, Brume is
// only a client of its public API, the one its developer portal opens to third-party applications):
//  - sign-in: OAuth 2.0 with PKCE (a public application: no secret), scopes « identity friends presence ». Nextendo has no
//    device-code flow (checked 2026-11-23: not in the docs, /api/oauth/device_authorization answers 404), so Brume runs a tiny
//    web server on the console (port 8080, only while the sign-in screen waits, 5 minutes at most) and the redirect is the
//    loopback address Nextendo accepts for public applications, http://127.0.0.1:8080/callback. Two ways to reach it:
//      1. on the console: the Switch's own browser (the WifiWebAuth applet - the one that works outside a game; the « Web »
//         applet needs a game's whitelist), started by the loader (la::Start) so the menu goes on; Nextendo sends the
//         browser to 127.0.0.1:8080, Brume's server takes the code, exchanges it and closes the browser by itself;
//      2. with a phone (the way out if 1 fails): a QR code opens a page served by the console on the Wi-Fi; the phone signs in,
//         lands on a 127.0.0.1 page that does not open (normal), and its address is pasted back into Brume's page.
//    The client_id is Yass's own application on the portal: sdmc:/brume/nextendo.ini (client_id=nxc_...). The tokens are
//    kept in sdmc:/brume/nextendo.json (never in a log); the access token lasts 1 h and is renewed with the refresh token.
//  - the page: each friend (avatar, name, offline / online / playing + the game's icon and detail), the requests received
//    (A accept, X refuse), « Ajouter un ami » by friend code (the console's keyboard), « Rejoindre » = the friend's game when
//    it is installed (as the games screen starts a Switch game), else « Tu n'as pas ce jeu ».
//  - notifications like the PS4's (Notify.hpp): « Léo est en ligne », « Léo joue à Mario Kart 8 Deluxe », a new request.
//    One look every 2 minutes, only while the menu is on screen (never during a game: zero cost), 429 + Retry-After obeyed,
//    one request at a time (the covers and the online counts wait for it, it waits for them).
//  - memory: the avatars and icons are 64 px, BC1, in ONE small strip texture (1024 x 128: 28 places + a QR code); its CPU
//    copy (64 KB) only lives while the page (or the QR) is shown; each picture is kept on the card once (2 KB,
//    sdmc:/brume/cache/amis/) so it is never downloaded twice. Every answer is read defensively (bounded, nothing trusted).
// DISCORD: voice on the Switch is not possible (end-to-end encryption DAVE since March 2026, no client allowed, no
//  microphone): « Vocal Discord » shows a big QR code of Yass's server invite (sdmc:/brume/discord.txt); his phone opens it.
//  From the Amis page and from Réglages > Réseau; drawn over any page.
#pragma once
#include "Ui.hpp"
#include "Gpu.hpp"
#include <switch.h>

namespace brume {

    class Wheel;

    namespace social {
        constexpr int kPage = 4;                         // the wheel's pages (v95): 0 Jeux, 1 Apps, 2 Album, 3 Manettes, 4 Amis, 5 Réglages
        bool Init(Gpu &gpu, uint32_t descriptor);        // the strip texture (false: initials instead of the pictures)
        const Target *Strip();                           // the strip, for the interface's 5th texture (nullptr: not there)
        bool Visible(const Wheel &w);
        void Update(const Wheel &w, float now, float dt, bool menuOnScreen);
        bool NeedsStream();
        void Stream(Gpu &gpu);                           // after Gpu::Begin: the strip, when it changed
        // the page's buttons (Entry: only while the page is in front and nothing else is open)
        void Move(int dir, float now);                   // ▲ ▼
        void PressA(float now);
        bool PressB(float now);                          // true: it closed something (the phone's QR, a waiting sign-in)
        void PressX(float now);                          // refuse a request
        void PressY(float now);                          // look again now
        void Tap(float x, float y, float now);
        void DrawUi(Ui &ui, float now, const Wheel &w);
        bool Launching();                                // « Rejoindre »: the screen goes black, the game starts
        void Done();                                     // back in the menu (onOpen): the light comes back after a launch
        // the Discord voice shortcut (any page; it takes the buttons while it is open: B closes)
        void DiscordOpen(float now);
        bool DiscordShown();                             // it takes the input
        void DiscordClose(float now);
        void DrawDiscord(Ui &ui, float now);
        bool NetBusy();                                  // a network job runs (the covers and the counts wait)
        // v96 - the profile page (MyPage.cpp) reads the Nextendo account through these. GET /api/me (identity: the account and
        // its presence), GET / POST /api/appear-offline (scope « profil »: invisible to friends; wiki.nextendo.network/developers).
        struct Me {
            bool in, noClient, busy;                     // signed in / no client_id / a network job runs
            const char *name, *user, *code;              // display name, @username, friend code ("" = not known yet)
            int  status;                                 // -1 not known, 0 offline, 1 online, 2 playing (what the friends see)
            u64  tid; const char *game;                  // the game they see (status 2)
            int  hidden;                                 // -1 not known, 0 visible, 1 invisible (appear offline)
            int  canHide;                                // -1 not known, 0 the token lacks « profil » (sign in again), 1 yes
        };
        Me   GetMe();
        void ProfileShown(bool on, float now);           // the page is open: the strip lives, a fresh look at the account
        void RefreshMe(float now);                       // Y on the profile page: look again now
        void SetHidden(bool hide, float now);            // invisible / visible to friends (POST /api/appear-offline)
        void SignOut(float now);                         // forget the sign-in (as the Amis page's « Se déconnecter »)
        bool DrawMeAvatar(Ui &ui, float cx, float cy, float r, float a);   // the Nextendo picture (128 px); false: not there (yet)
        bool DrawGameIcon(Ui &ui, u64 tid, float cx, float cy, float r, float a);   // an installed game's icon (the strip)
        extern bool s_barNextendo;                       // the profile's choice: the Nextendo picture in the top bar too
        void Sleep();                                    // before sleep: the sign-in server stops, the QR closes
        void Shutdown();                                 // the worker ends (before the menu is unloaded)
        extern void (*s_log)(const char *);
#ifdef BRUME_UISIM
        void SimDemo(const char *what, float val, float now);   // tools/uisim: canned answers (no network there)
                                                                // (v96: « demo profil 0..4 »: out / in / playing / invisible / no client_id)
#endif
    }

} // namespace brume
