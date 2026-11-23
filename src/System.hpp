// Brume - the system pages behind the top bar's tabs, and the power panel.
//   Album, Manettes: the loader's qext_launch_applet (0 album, 1 controllers)
//   Réglages:        the System Settings library applet (la::Start(AppletId_LibraryAppletSet, 1), as sLaunch does)
//   Profil (« Yass »): MyPage, « show my profile » for the console's profile
//   Alimentation:    our own small glass panel - Veille / Redémarrer / Éteindre
#pragma once
#include "Ui.hpp"
#include "Assets.hpp"
#include "Shelf.hpp"

namespace brume {

    class Wheel;

    class System {
    public:
        // tab (System's own numbers, not the wheel's pages): 0 Jeux, 2 Album, 3 Manettes, 4 Réglages, 5 Alimentation, -1 profile
        // (v92: 1, the old Musique placeholder, is gone).
        // Returns true when the tab is handled here (Jeux is the wheel itself: false).
        bool Open(int tab, float now);

        bool PowerOpen() const { return m_power; }
        void PowerMove(int step, float now);
        void PowerConfirm(float now);
        void PowerClose(float now);
        void Restart(float now);                          // v78: a popup's « Redémarrer » (the same fade to black, then the restart)

        void Draw(Ui &ui, const Assets &a, float now);
        // v33 (Yass): the popup's own life - the lights go down while it is open, the chosen action happens after a
        // short fade to black; a tap on an icon chooses it, a tap outside closes
        void Update(float now, float dt);
        void Tap(float x, float y, float now);
        bool Busy() const { return m_doAt > 0; }

        static void (*s_log)(const char *);
        void Status(float now, const char *fmt, ...) __attribute__((format(printf, 3, 4)));   // the toast under the bar

    private:
        bool  m_power = false;
        int   m_sel = 0;
        float m_powerAt = -100, m_closeAt = -100, m_moveAt = -100;
        float m_doAt = -1, m_dim = 0;                     // v33: when the action happens; the light going down
        float m_selF = 0, m_selV = 0;                     // v78: where the chosen row's light is (it glides)
        int   m_do = 0;
        char  m_status[128] = {};
        float m_statusAt = -100;
    };

} // namespace brume
