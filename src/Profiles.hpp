// Brume - v46: the profiles (Yass, 2026-11-16: « faut aussi faire les profils », one of the big jobs).
// The console's users, with their real avatar and nickname: the top bar's pill shows the active one (instead of the
// mockup's baked « Yass »); A on it opens the Profiles panel (a glass panel under the bar):
//   ▲ ▼ choose · A = this one plays (the games start as them) · X = their profile page (the Switch's MyPage)
//   Y = change the icon · − = change the nickname · the last row adds a user (the Switch's own screens) · B = close.
// The active profile is kept in sdmc:/brume/profil.txt; if it no longer exists: the last opened, else the first one.
// Avatars: the account service's JPEG (256 px), decoded once into a strip of 8 x 128 px (BC1, 64 KB, one GPU place).
#pragma once
#include "Gpu.hpp"
#include "Ui.hpp"
#include <switch.h>

namespace brume {

    class Profiles {
    public:
        static constexpr int kMax = 8, kSlot = 128;
        bool Init(Gpu &gpu, uint32_t descriptor);       // the avatars' strip (false: names only)
        void Load();                                     // the users, their names and avatars (decoded now, uploaded by Stream)
        void LoadSoon();                                 // v99: the same in the background (the frame takes it in Update)
        static void Shutdown();                          // v99: the background read ends (the menu closes)
        struct UserCopy { AccountUid uid; char nick[40]; bool icon; };
        bool NeedsStream() const { return m_dirty && m_ok; }
        void Stream(Gpu &gpu);                           // after Gpu::Begin: the strip, when it changed
        const Target *Avatars() const { return m_ok && m_uploaded ? &m_tex : nullptr; }

        bool Open() const { return m_open; }
        void OpenPanel(float now);
        void Close(float now);
        void CloseNow() { m_open = false; m_k = 0; m_kv = 0; }   // v56: silently (before sleep)
        void Move(int step, float now);
        void PressA(float now);
        void PressX(float now);                          // the profile page
        void PressY(float now);                          // the icon
        void PressMinus(float now);                      // the nickname
        void Tap(float x, float y, float now);
        void Update(float now, float dt);
        void DrawUi(Ui &ui, float now);

        // the active profile (the games start as them): the chosen one if it still exists, else last opened / first
        static AccountUid Active();
        static int  Count() { return s_n; }
        static const char *ActiveName();
        static int  ActiveSlot();                        // its avatar's place in the strip, -1 = none
        // v87 - the lock screen's « Qui joue ? »: each user, and the one chosen there becomes the one who plays (saved)
        static int  ActiveIndex() { return s_active; }
        static AccountUid UidAt(int i);
        static const char *NameAt(int i);
        static int  SlotAt(int i);                       // its avatar's place, -1 = none
        static void Choose(int i);
        static AccountUid Ask();                         // the Switch's user picker (invalid = cancelled)
        // an avatar as a soft-edged circle (ui_fsh mode 26), for the bar too
        static void DrawAvatar(Ui &ui, int slot, float cx, float cy, float r, float alpha);
        static void (*s_toast)(float now, const char *msg);
        static void (*s_log)(const char *msg);

    private:
        struct User { AccountUid uid; char nick[40]; bool icon; };
        static User s_user[kMax];
        static int  s_n, s_active;
        static void SaveActive();
        void Take(bool first);                           // v99: what the read found becomes the list
        void Refresh(float now);                         // after a Switch screen (editor, creator): read again
        Target   m_tex;
        bool     m_ok = false, m_dirty = false, m_uploaded = false;
        bool     m_open = false;
        int      m_sel = 0;
        float    m_k = 0, m_kv = 0, m_openAt = -100, m_moveAt = -100, m_selY = 0, m_selV = 0;
    };

} // namespace brume
