// Brume - v34/v37: the Album page (Yass: « qui affiche réellement la galerie explorable »; v37: « plutôt laggy, surtout
// quand tu mets une image en plein écran... petite refonte, il doit être plaisant »).
// Captures from the console's album (the loader's qext_album_* = the official capture service caps:a, SD and the
// emuMMC's NAND), newest first, in a COVER FLOW of glass prints: the chosen one big in the middle, the others turned on
// each side, a reflection on the floor. ◀ ▶ or the finger (throw it: it settles on the next one). The chosen photo
// turns SHARP by itself after a moment: its full picture (1280 x 720) is decoded on a worker thread and sent to the GPU in
// slices, one per frame (no freeze), then it fades in over the thumbnail. A = full screen (the print grows to fill the
// screen; ◀ ▶ slide to the previous / next one there too), B = back. A video shows a play sign; A plays it in the
// Switch's album (for now).
#pragma once
#include "Gpu.hpp"
#include "Ui.hpp"
#include "Assets.hpp"
#include "Shelf.hpp"

namespace brume {

    class Wheel;

    class Album {
    public:
        static constexpr int kPage = 2;                  // the wheel's pages (v95): 0 Jeux, 1 Apps, 2 Album, 3 Manettes, 4 Amis, 5 Réglages
        bool Init(Gpu &gpu, uint32_t fullDescriptor);
        bool Visible(const Wheel &w) const;              // the page is (partly) on screen
        void Move(int step, float now);
        void PressA(float now);
        bool PressB(float now);                          // true: nothing was open (back to the games)
        bool Viewing() const { return m_view; }
        void Tap(float x, float y, float now);
        void DragBegin();
        void DragTo(float now, float steps);             // steps: photos since the finger went down (+ = older)
        void DragEnd(float now, float v);
        void Update(Gpu &gpu, const Wheel &w, float now, float dt);   // before Gpu::Begin
        static void Shutdown();                                        // v68: waits for its worker (before the menu is unloaded)
        void Stream(Gpu &gpu, Shelf &shelf, const Wheel &w);         // after Gpu::Begin: a thumbnail or a slice of the photo
        void DrawCards(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w, Shelf &shelf);
        void DrawUi(Ui &ui, float now, const Wheel &w);
        static void (*s_log)(const char *);

    private:
        static constexpr int kMax = 256;
        int  Key(int i) const { return (1 << 20) + m_gen * 512 + i; }
        void Info(int i);
        void Select(int i, float now);
        void StartJob(int i, float now);
        bool StartList();                                // v90: the list, read by the worker (it took 220-240 ms in a frame)
        void Collect(const Wheel &w);                    // v90: the worker's result, once it is over

        int   m_n = 0, m_sel = 0, m_gen = 0;
        bool  m_listed = false;
        float m_cur = 0, m_curV = 0, m_moveAt = -100, m_now = 0;   // the flow (a float index gliding to m_sel)
        bool  m_drag = false;
        float m_dragFrom = 0;
        bool  m_view = false;                            // full screen
        float m_viewK = 0, m_viewV = 0;                  // 0 the flow .. 1 full screen (glided)
        // the sharp photo: decoded on a worker thread, sent in slices, then faded in
        Target   m_full;
        bool     m_fullOk = false;
        uint32_t m_fullDesc = 0;
        int   m_fullFor = -1, m_jobFor = -1, m_upRow = -1;
        float m_sharpAt = -100, m_dropAt = -100;
        bool  m_noThumb[kMax] = {}, m_movie[kMax] = {};
        bool  m_fullBad[kMax] = {};                      // v90: its sharp photo could not be made: not asked again until the next list
        bool  m_listing = false;                         // v90: the worker reads the list
        int   m_thumbFor = -1, m_thumbGen = -1;          // v90: the thumbnail the worker makes (its capture, its list)
        bool  m_thumbReady = false, m_thumbOk = false;   // ... it is ready (or could not be made)
        int   m_infoFor = -1;
        char  m_game[96] = {}, m_date[64] = {};
        struct Hit { int i; float x0, y0, x1, y1; };
        Hit   m_hit[16];
        int   m_nhit = 0;
    };

} // namespace brume
