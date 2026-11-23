// Brume - our own HOME menu for qlaunch-ext (sLaunch « Yass Edition », written from zero).
// Gpu: a small, explicit deko3d wrapper. No SDL in the drawing path.
//
// Rules this is built around (see CLAUDE.md, section 3 bis):
//  - render at 1280x720 handheld and 1920x1080 docked only; the VI layer is
//    FitToLayer, so the compositor scales our buffers to the screen;
//  - two buffers (memory: the menu must fit in qlaunch-ext's 62 MB heap);
//  - real GPU time measured with timestamps, not only vsync-rounded FPS.
#pragma once
#include <switch.h>
#include <deko3d.h>
#include <cstdint>

namespace brume {

    constexpr int kFrames = 3;          // swapchain images / frames in flight (3: a late image no longer costs a whole vsync)

    struct Target {                     // an offscreen colour target we can sample
        DkImage     image;
        DkImageView view;
        int         w = 0, h = 0;
        uint32_t    descriptorId = 0;   // slot in the image descriptor set
    };

    struct UiVtx {                      // matches shaders/ui_vsh.glsl
        float x, y;                     // menu px (1280x720 space)
        float u, v;                     // atlas uv, or local px for shapes
        float r, g, b, a;               // straight colour
        float mode, p1, p2, p3;         // see ui_fsh.glsl
    };

    struct Program {
        DkShader vs, fs;
        bool     ok = false;
    };

    class Gpu {
    public:
        // deko3d's error messages go here (written to sdmc:/brume/perf.log before anything else happens)
        static inline void (*s_log)(const char *msg) = nullptr;
        bool Init(NWindow *win);        // false: nothing usable, menu must report failure
        void Shutdown();

        // Docked / handheld changed: rebuild the swapchain at the new size.
        void CheckMode();
        void WaitIdle() { if (m_queue) dkQueueWaitIdle(m_queue); }
        int  Width() const  { return m_w; }
        int  Height() const { return m_h; }
        bool Docked() const { return m_docked; }

        bool MakeTarget(Target &t, int w, int h, uint32_t descriptorId);
        bool MakeTexture(Target &t, int w, int h, DkImageFormat fmt, uint32_t bytesPerPixel,
                         const void *pixels, uint32_t descriptorId);
        // Same, any format (BC1...): `bytes` = size of the linear data.
        bool MakeTextureRaw(Target &t, int w, int h, DkImageFormat fmt, const void *data, uint32_t bytes, uint32_t descriptorId, uint32_t levels = 1);
        // v31 - textures that change while the menu runs (the game covers): a fixed place in the image pool, reserved
        // once (big enough for every layout it will hold); Place = an image of that size / format there + its
        // descriptor (only for a place no frame in flight still reads); Stream = the upload, RECORDED IN THE FRAME
        // (no stall): call it right after Begin(), before any draw, once per frame at most (<= kStreamMax bytes).
        static constexpr uint32_t kStreamMax = 0x18000;
        uint32_t ImageBytes(int w, int h, DkImageFormat fmt, uint32_t levels, uint32_t *align) const;
        uint32_t ReserveImage(uint32_t bytes, uint32_t align);            // UINT32_MAX: the pool is full
        bool PlaceTexture(Target &t, uint32_t off, int w, int h, DkImageFormat fmt, uint32_t levels, uint32_t descriptorId);
        bool StreamTexture(const Target &t, const void *data, uint32_t bytes, uint32_t levels);
        // v37: rows [y, y + rows) of a BC1 texture's first level (rows: multiple of 4), the same way (one per frame)
        bool StreamRows(const Target &t, const void *data, uint32_t y, uint32_t rows);
        // v34: a big texture (the album's full photo): waits for the GPU, then copies - only OUTSIDE a frame (before Begin)
        bool UploadNow(const Target &t, const void *data, uint32_t bytes, uint32_t levels);
        bool UploadRowsNow(const Target &t, const void *data, uint32_t y, uint32_t rows);   // v45: R8 rows, outside a frame
        // A CPU-written, GPU-read buffer (vertices / indices of the 3D models). nullptr if no memory.
        DkMemBlock MakeBuffer(uint32_t size);
        bool MakeProgram(Program &p, const void *vs, uint32_t vsSize, const void *fs, uint32_t fsSize);

        // ---- one frame ----
        DkCmdBuf Begin();               // waits for the frame slot, opens the command buffer
        void BindScreen();              // render into the swapchain image
        void BindTarget(Target &t);
        void SetScissor(int x, int y, int w, int h);   // v38: draw only into this rectangle (the fog, half per frame)
        void UseProgram(const Program &p);
        const Program *CurrentProgram() const { return m_prog; }   // v74: what UseProgram bound last (Ui::Flush switches and comes back)
        void SetUniforms(const void *data, uint32_t size);
        void BindTexture(const Target &t);
        // Several textures at bindings 0..n-1; repeat = sampler 1 (wrapping uv) for binding 0.
        void BindTextures(const Target *const *ts, int n, bool repeat = false);
        void Barrier();                 // finish writing targets before sampling them
        void DrawFullscreen();
        void DrawCubeQuads();           // v29.1: 6 squares, one per water cube (shaders/cubequad_vsh.glsl)
        void BeginUi();                 // premultiplied blending + interface vertex layout
        void DrawUi(const UiVtx *v, uint32_t count);   // triangles
        // ---- 3D models (the consoles) ----
        void BeginMesh();               // screen + depth (cleared), depth test, 3 attributes (pos, normal, uv)
        void SetMeshUniforms(const void *data, uint32_t size, uint32_t offset = 0);   // offset: update only a part of the block   // binding 1 (vertex + fragment)
        void SetMeshState(bool cullBack, bool blend, bool frontCW = false);            // blend = straight alpha, no depth write
        void DrawMesh(DkGpuAddr vtx, uint32_t vtxBytes, DkGpuAddr idx, uint32_t idxCount);
        // v29 - a full-screen pass over the 3D (the water cubes in front of the consoles): screen + the depth of the
        // consoles, depth test LESS without writing, depth bounds [0, 1) (the background is skipped), premultiplied
        // blending. Only if something 3D was drawn this frame (MeshDrawn); EndOverlay before the interface.
        bool MeshDrawn() const { return m_meshDrawn; }
        void BeginOverlay();
        void EndOverlay();
        // Diagnosis: send what is queued so far, wait, and tell whether the GPU hit an error
        // (writes `tag` to the log if it did). Slow: only for a one-time probe.
        bool Probe(const char *tag);
        void End();                     // submit + present

        // Measured GPU time of the last completed frame, ms (0 until known).
        float GpuMs() const { return m_gpuMs; }
        static constexpr int kMarks = 4;         // v90: 4 marks = 5 stages: the fog, the glass (+ stars behind), the consoles, the other 3D, the interface
        void  Mark(int k);                       // stage timestamps (0 .. kMarks - 1): GPU time per stage, see StageMs
        float StageMs(int k) const { return m_stageMs[k]; }
        void  MarkConsole(int i);                 // after console i's opaque parts: its GPU time, see ConsoleMs
        float ConsoleMs(int i) const { return m_consMs[i]; }   // 0 = fog + cubes, 1 = consoles, 2 = interface
        uint32_t ImgUsed() const { return m_imgUsed; }        // the image pool (textures, targets), bytes
        // How long Begin() waited for a free swapchain image (vsync), in system ticks.
        u64 AcquireTicks() const { return m_acquireTicks; }

    private:
        bool CreateSwapchain();
        void DestroySwapchain();
        uint32_t AllocImage(uint64_t size, uint32_t align);
        uint32_t AllocCode(const void *data, uint32_t size);

        NWindow    *m_win = nullptr;
        DkDevice    m_dev = nullptr;
        DkQueue     m_queue = nullptr;
        DkMemBlock  m_cmdMem = nullptr, m_dataMem = nullptr, m_codeMem = nullptr;
        DkMemBlock  m_fbMem = nullptr, m_imgMem = nullptr, m_vtxMem = nullptr, m_depthMem = nullptr;
        DkMemBlock  m_streamMem = nullptr;    // v31: kStreamMax per frame in flight (the covers' uploads)
        bool        m_streamed = false;       // v34: this frame's staging area is taken
        DkImage     m_depth;
        DkImageView m_depthView;
        uint32_t    m_vtxUsed = 0;
        DkCmdBuf    m_cmd = nullptr;
        DkFence     m_fence[kFrames] = {};
        bool        m_fenceUsed[kFrames] = {};
        DkImage     m_fb[kFrames];
        DkImageView m_fbView[kFrames];
        DkSwapchain m_swapchain = nullptr;
        int         m_w = 1280, m_h = 720;
        bool        m_docked = false;
        bool        m_meshDrawn = false;      // v29: something 3D was drawn this frame (the depth buffer is valid)
        const Program *m_prog = nullptr;      // v74: the program bound last
        int         m_slot = -1;        // acquired swapchain image
        int         m_frame = 0;        // frame-in-flight index
        uint32_t    m_codeUsed = 0, m_imgUsed = 0;
        float       m_gpuMs = 0.0f;
        float       m_stageMs[kMarks + 1] = {};
        float       m_consMs[8] = {};
        u64         m_acquireTicks = 0;
    };

} // namespace brume
