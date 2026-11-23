// Brume - v31: what the game boxes need on the GPU.
//  - one box model (a unit cube whose faces carry their number in the uv: shaders/box_fsh.glsl dresses each face:
//    the cover in front, a spine made from the cover's edge, the case's plastic around);
//  - the covers: a few fixed places in the image pool (as many as fit, ~20), filled while the menu runs - one cover
//    per frame, recorded in the frame (no stall), the least recently drawn place is reused (an LRU cache).
//    A cover is either a .btex made on the PC by tools/art-builder (sdmc:/brume/jaquettes/<console>/<rom>.btex,
//    BC1 + mipmaps + the scan's real proportions) or, for a Switch game, its icon (JPEG from the loader, decoded
//    and compressed here).
#pragma once
#include "Gpu.hpp"

namespace brume {

    class Shelf {
    public:
        static constexpr int kMaxSlots = 20;
        bool Init(Gpu &gpu, uint32_t firstDescriptor);   // false: no box can be drawn (logged by the caller)
        int  Slots() const { return m_nslot; }
        void Forget();
        void Remap(const int *newOf, int n);             // v67: the list was sorted again: key k becomes newOf[k] (-1 = gone)                                   // another list of games: every place can be reused
        void NextFrame() { m_frame++; }
        bool Has(int key) const;
        bool CanLoad() const { return FreeSlot() >= 0; }   // v37: a place can take a new picture now
        // the cover of `key` if it is loaded (and marks it drawn this frame), else nullptr
        const Target *Cover(int key, float *aspect);
        // Load the cover of `key` NOW (once per frame, right after Gpu::Begin): a .btex file, or the icon of the
        // loader's title `titleIndex` (Switch). 1 = loaded, 0 = this game has no cover (don't ask again),
        // -1 = not now (no place free yet).
        int  Load(Gpu &gpu, int key, const char *btexPath, int titleIndex, const char *nroIcon = nullptr);   // v42: or a .nro's icon
        // v90 (perf): the heavy part of Load (the SD read, the JPEG decode, the BC1 encode: 10-75 ms in a frame in perf.log)
        // runs on a worker; Load starts it and returns -1 (« not now »), a later Load uploads it - for that key or another
        // one (asked meanwhile: it gets its place all the same; a missing one is remembered and answered 0 when asked).
        // v34: an RGBA picture (w, h multiples of 4, BC1 <= Gpu::kStreamMax) into a place: the album's thumbnails
        int  LoadRGBA(Gpu &gpu, int key, const uint8_t *rgba, int w, int h, float aspect);
        // v90: a picture already in BC1 (one level, w x h, made by a worker) into a place; same answers as Load
        int  LoadBC1(Gpu &gpu, int key, const uint8_t *bc, int w, int h, float aspect);
        static void Shutdown();                          // v90: waits for the worker (before the menu is unloaded)
        static void Reap();                              // v90: every frame - a finished load's thread is closed (its stack freed)
        static uint8_t *EncodeBC1(const uint8_t *rgba, int w, int h, uint8_t *out);
        bool Ready() const { return m_mesh != nullptr; }
        DkGpuAddr Vtx() const { return dkMemBlockGetGpuAddr(m_mesh); }
        DkGpuAddr Idx() const { return dkMemBlockGetGpuAddr(m_mesh) + kIdxOff; }
        static constexpr uint32_t kVtxBytes = 24 * 32, kIdxOff = 768, kIdxCount = 36;
        // v32: a disc (diameter 1, 1 % thick, a hole in the middle): faces 4 label (+y), 5 data side (-y), 6 edges
        static constexpr int      kDiscSeg = 48;
        static constexpr uint32_t kDiscVtxOff = 0x400, kDiscVtxBytes = 8 * kDiscSeg * 32, kDiscIdxOff = 0x3400, kDiscIdxCount = 4 * kDiscSeg * 6;
        DkGpuAddr DiscVtx() const { return dkMemBlockGetGpuAddr(m_mesh) + kDiscVtxOff; }
        DkGpuAddr DiscIdx() const { return dkMemBlockGetGpuAddr(m_mesh) + kDiscIdxOff; }

    private:
        struct Slot { int key = -1; uint32_t used = 0, off = UINT32_MAX; float aspect = 1; Target t; };
        int  FreeSlot() const;
        int  Collect(Gpu &gpu, int key);                 // v90: the worker's result: 1 / 0 / -1 for `key`, -2 = not about `key`
        bool Missing(int key) const;
        void AddMissing(int key);
        Slot       m_slot[kMaxSlots];
        int        m_nslot = 0;
        uint32_t   m_frame = 100, m_firstDesc = 0;
        DkMemBlock m_mesh = nullptr;
        unsigned   m_gen = 0;                            // v90: Forget / Remap - a load still on the worker is for an old list
        int        m_miss[16] = {}, m_nmiss = 0, m_missAt = 0;   // v90: keys the worker found without a cover (a ring)
    };

} // namespace brume
