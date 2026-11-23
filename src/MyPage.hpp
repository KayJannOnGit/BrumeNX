// Brume - v96: the profile page (Yass, 2026-11-23: « un vrai menu de profil qui remplace celui de Nintendo par un truc génial
// qui marche avec Nextendo »). The top bar's profile (its avatar and name, top left) opens it: a whole page in the fog, over
// the page that was there (the scene steps back). Not a new L / R page: the profile is the person, not a place - it opens
// from their own picture, wherever they are (as the PS4 / PS5 do), and the ring of pages stays the console's places.
//
//  - who: the avatar big (the Switch user's, or the Nextendo one: « Image du profil »), the Switch nickname, what the friends
//    see (online / playing / invisible / offline) and the Nextendo name; the friend code big, to give to friends.
//    (No QR code for adding a friend: Nextendo has no such link - checked on wiki.nextendo.network/developers, 2026-11-23.)
//  - play time, from the console's own play log (pdm:qry, read-only, the active user): the total, the three most played
//    games, the last one played. Read on a background thread when the page opens (never while a game runs).
//  - the choices: Changer de profil (who plays: the console's users, inline), Image du profil (Switch / Nextendo),
//    Statut pour tes amis (Visible / Invisible: POST /api/appear-offline, scope « profil »), Se connecter à Nextendo (the
//    Amis page's sign-in) / Se déconnecter, Gérer sur la Switch (the Profils panel: the Switch's own screens).
// Nothing is ever written to the console's accounts (read-only); Brume's own choice goes in sdmc:/brume/profil.ini.
#pragma once
#include "Ui.hpp"

namespace brume::mypage {

    void Load();                                         // sdmc:/brume/profil.ini (at start)
    bool Open();
    float Amount();                                      // how much it is on screen (0..1)
    void Show(float now);
    void Close(float now);
    void CloseNow();                                     // silently (sleep, the lock screen)
    void Update(float now, float dt);
    void Move(int dy, float now);                        // ▲ ▼
    void Side(int dx, float now);                        // ◀ ▶ (a value)
    void PressA(float now);
    void PressB(float now);
    void PressY(float now);                              // look again (the account, the play time)
    void Tap(float x, float y, float now);
    void Draw(Ui &ui, float now);
    // the top bar's picture when the profile chose the Nextendo one (false: draw the Switch's)
    bool BarAvatar(Ui &ui, float cx, float cy, float r, float a);
    extern bool s_avatarsOk;                             // the Switch avatars' strip is there (Entry)
    extern void (*s_switchPanel)(float now);             // Entry: the Profils panel (the Switch's own screens)
    extern void (*s_friends)(float now);                 // Entry: to the Amis page (its sign-in screen)

}
