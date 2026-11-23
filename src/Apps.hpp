// Brume - v35/v36: the Apps page (Yass: « un onglet apps, qui forward les nro de homebrew »; v36: « juste à côté de la
// page des jeux », « un agencement plus stylé que des icônes carrées en grille »).
// Every homebrew of sdmc:/switch/ (a .nro there, or one folder down), with its real name, author, version and icon (read
// from the .nro's own assets: NACP + JPEG icon), as GLASS CUBES on a ring - like the consoles' wheel: the chosen one in
// front, the others going round into the fog; its name on a glass banner. ◀ ▶ or the finger turn it (throw it: it
// settles on the next one). A = the cube comes at us, the screen goes black, the homebrew starts in APPLICATION mode
// (all the memory) through the donor game - the same path as the emulators (Games::LaunchNro), cleaned up afterwards.
#pragma once
#include "Gpu.hpp"
#include "Ui.hpp"
#include "Assets.hpp"
#include "Shelf.hpp"
#include "Ease.hpp"

namespace brume {

    class Wheel;

    class Apps {
    public:
        static constexpr int kPage = 1;                  // v36: right after Jeux
        bool Visible(const Wheel &w) const;
        void Move(int step, float now);
        void PressA(float now);
        bool PressB(float now);                          // true: nothing to close (back to the games)
        void Tap(float x, float y, float now);
        void RingDragBegin();
        void RingDragTo(float now, float steps);         // steps: cubes since the finger went down (+ = to the next)
        void RingDragEnd(float now, float v);            // v: the finger's speed (cubes per second)
        bool Launching() const { return m_launch >= 0; }
        void Sleep() { if (m_launch >= 0 && !m_started) { m_launch = -1; m_launchAt = -100; m_backAt = -100; } }   // v86: Power before it started: it never starts behind the lock screen
        void Done();                                     // back in the menu (a homebrew exited)
        void Update(const Wheel &w, float now, float dt);
        void Stream(Gpu &gpu, Shelf &shelf, const Wheel &w);
        void DrawCards(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w, Shelf &shelf);
        void DrawUi(Ui &ui, float now, const Wheel &w);
        static void (*s_log)(const char *);
        static void Shutdown();                          // v90: waits for its worker (before the menu is unloaded)

    private:
        static constexpr int kMax = 160;
        struct App { char path[256], name[80], author[48], version[16]; uint32_t iconOff, iconSize; bool noIcon; };
        void Scan();                                     // (v90: on the worker - it took 155-167 ms in a frame)
        static void Work(void *self);                    // v90: the worker's job (the list, or one icon)
        void Collect();
        bool Read(const char *path, App &a);
        void Select(int i, float now);
        int  Key(int i) const { return (2 << 20) + m_gen * 256 + i; }

        App   m_a[kMax];
        int   m_n = 0, m_sel = 0, m_gen = 0;
        bool  m_listed = false;
        bool  m_scanning = false;                        // v90: the worker reads sdmc:/switch (m_n stays 0 meanwhile)
        int   m_scanN = 0;                               // ... what it found
        int   m_iconFor = -1, m_iconGen = -1;            // v90: the icon the worker reads (its app, its list)
        bool  m_iconReady = false, m_iconOk = false;
        float m_cur = 0, m_curV = 0, m_moveAt = -100;    // the ring (a float index, gliding to m_sel)
        bool  m_drag = false;
        float m_dragFrom = 0;
        int   m_launch = -1;                             // the cube coming at us (then the homebrew starts)
        float m_launchAt = -100, m_backAt = -100;
        bool  m_started = false;
        Settle m_settle;                                 // v80: back from the homebrew, black until the frames are steady
        char  m_status[160] = {};
        float m_statusAt = -100;
        struct Hit { int i; float x0, y0, x1, y1; };
        Hit   m_hit[16];
        int   m_nhit = 0;
    };

} // namespace brume
