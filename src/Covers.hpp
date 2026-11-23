// Brume - v75: the game covers, downloaded by the console itself (Yass, 2026-11-17: « vrai download direct depuis la
// console des jaquettes, les joueurs veulent pas traîner sur le PC »). For the emulated consoles (sdmc:/roms/<console>/),
// the front cover of libretro-thumbnails (« Named_Boxarts », the ROM's Redump / No-Intro name) is fetched over HTTPS
// (the console's TLS service, Nextendo.hpp's Https), decoded as it arrives (PNG, zlib streaming: never the whole image in
// memory) straight into the texture the menu reads: sdmc:/brume/jaquettes/<console>/<rom name>.btex - the same file the PC
// tool (tools/art-builder) makes. The rules of CLAUDE.md section 5 stay: one request at a time with a pause, never the same
// image twice (a cover found is kept; a cover missing is noted in introuvables.txt and not asked again), resumes where it
// stopped, nothing while a game runs, nothing about the player sent anywhere, never at the same time as Nextendo.
// Réglages > Données: « Télécharger les jaquettes » (now) and « Jaquettes automatiques » (the missing ones, in the
// background, once per start, when the menu is on screen and the Wi-Fi is up).
#pragma once
#include <switch.h>

namespace brume {

    namespace covers {
        void Update(float now, bool menuOnScreen);   // main thread, every frame: starts the background run, says the progress
        void DownloadNow(float now);                 // the row (a toast says how it goes)
        bool Busy();                                 // a run is on its way (Nextendo waits: the network is shared)
        int  Generation();                           // goes up each time a cover is written (Games reloads the missing ones)
        void Shutdown();                             // stops the run, waits for it (before the menu is unloaded)
        extern void (*s_log)(const char *);
        extern void (*s_toast)(float now, const char *msg);
    }

} // namespace brume
