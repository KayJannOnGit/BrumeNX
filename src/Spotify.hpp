// Brume - v98: Spotify (Yass, 2026-11-23: he has Spotify Premium and accepts music streaming during games - an exception to
// « zéro réseau en jeu », for the music only). See docs/SPOTIFY.md.
// - The music plays in « Brume Musique », a small background program (Switify's GPL-3 engine, cspot inside, rebuilt for
//   Brume: musique/, program 0x420000000042524D, service « brm:spot »). It keeps playing while a game runs. It is started
//   only when Spotify is switched on (Réglages > Son > Spotify, or the panel) and stopped when it is switched off: nothing
//   at all runs while Spotify is off (zero cost when unused).
// - The first time, the phone pairs it (Spotify > Appareils > BrumeNX, same Wi-Fi, Premium); the account is kept on the
//   card (sdmc:/brume/spotify/credentials.json, never in git, never in a log).
// - Brume shows what plays and controls it: a small line in the top bar (four little bars and the title), and a panel
//   (ZL, or a touch on that line): the cover, the title, the artist, play / pause / next / previous, the volume, and the
//   music to choose (Bibliothèque, Accueil, Rechercher, À suivre) - Spotify's servers no longer let the phone drive this
//   kind of player, so the choosing happens here.
// - Everything the service answers is checked (sizes, versions, text cut to valid UTF-8); the cover is fetched only while
//   the panel is open, small (128 px, 8 KB on the GPU), from i.scdn.co only.
// - The nappe steps aside while Spotify plays (audio::Yield).
// - The PC preview (uisim, BRUME_UISIM): a made-up service (« demo spotify <n> »), so every state can be seen.
#pragma once
#include "Ui.hpp"
#include <switch.h>

namespace brume::spotify {

    // v101 (Yass, 2026-11-23): Spotify is OFF for the first public version - the pairing did not work on the console and it
    // comes back in a later update. Off: Boot does nothing (the service is never started, nothing runs, no panel, no top-bar
    // line, no Réglages rows). The code stays, ready: true here brings it all back.
    constexpr bool kEnabled = false;

    void Boot(float now);                        // reads sdmc:/brume/spotify/brume.ini; Spotify left on = the service starts
    void Update(float now, float dt, bool menuOnScreen);   // once per frame (main thread): the service, the panel, the cover
    void Shutdown();                             // the menu leaves (sleep, unload): the session closes, the music plays on
    bool Wanted();                               // switched on
    void SetOn(bool on, float now);              // on: the service starts (and the panel shows the pairing if needed)
    bool Busy();                                 // the cover's download holds the network (the covers / Nextendo wait)

    // the panel
    void OpenPanel(float now);
    void ClosePanel(float now);
    void TogglePanel(float now);
    bool PanelOpen();
    void Input(u64 down, float now);             // while it is open: every button is its own
    bool PanelTap(float x, float y, float now);  // true: the touch was the panel's (or closed it)
    void DrawPanel(Ui &ui, float now);

    // the top bar's line (only while something is loaded: playing or paused)
    float BarRoom();                             // the width it takes left of the date and time (glides)
    void  DrawBar(Ui &ui, float xRight, float cy, float alpha, float now);
    bool  BarHit(float x, float y);

    // the cover (binding 5 of ui_fsh, mode 32)
    bool InitCover(Gpu &gpu, uint32_t descriptor);
    bool NeedsStream();
    void Stream(Gpu &gpu);                       // after Gpu::Begin, once per frame at most (8 KB)
    const Target *Cover();

    void SimSet(int state);                      // uisim: 0 off, 1 pairing, 2 playing, 3 paused, 4 refused, 5 not installed, 6 starting
    extern void (*s_log)(const char *);

} // namespace brume::spotify
