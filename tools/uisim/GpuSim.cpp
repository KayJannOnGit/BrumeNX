// uisim - the Gpu class on the PC. Nothing is drawn here: while a frame is captured, every command is written to a
// file (out/<name>.bin) that view.html replays with the real shaders (WebGL 2). Textures given with their pixels are
// written once (out/tex<descriptor>.bin). The 3D is not replayed (consoles, boxes), except the settings carousel's glass
// pebbles (v73: icon_fsh, with their uniforms, vertices and indices, so that their glass can be judged here too).
#include "../../src/Gpu.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
    extern FILE *g_simOut;                 // main.cpp: open while a frame is captured
    extern const char *g_simDir;           // where the files go
}

double g_simFill[40];                     // (perf) covered pixels per ui_fsh mode, this frame (main.cpp prints them on a shot)
int    g_simQuads[40];
int    g_simMeshes = 0, g_simMeshIdx = 0, g_simProgs = 0;   // (perf) 3D draws and their indices, program changes, this frame
namespace {
    enum : uint32_t { kBindTarget = 1, kBindScreen, kUseProgram, kUniforms, kTextures, kFullscreen, kUi, kBeginUi,
                      kCubeQuads, kScissor, kBeginMesh, kMesh, kBeginOverlay, kEndOverlay, kMeshState };
    void Rec(uint32_t type, uint32_t a, uint32_t b, const void *data, uint32_t bytes) {
        if (!g_simOut) return;
        const uint32_t h[4] = { type, a, b, bytes };
        fwrite(h, 4, 4, g_simOut);
        if (bytes) fwrite(data, 1, bytes, g_simOut);
    }
    void DumpTex(uint32_t id, int w, int h, int fmt, const void *px, uint32_t bytes) {
        if (!px || !bytes) return;
        char path[512];
        snprintf(path, sizeof path, "%s/tex%u.bin", g_simDir, id);
        if (FILE *f = fopen(path, "wb")) {
            const uint32_t hd[4] = { (uint32_t)w, (uint32_t)h, (uint32_t)fmt, bytes };
            fwrite(hd, 4, 4, f); fwrite(px, 1, bytes, f); fclose(f);
        }
    }
    struct SimBlock { void *cpu; uint32_t size; };
    const void *s_progFs[16];
    int s_progs = 0;
    int s_fmt[256];                       // the format of each placed texture (StreamTexture gives only its data)
    uint32_t s_prog = 0;                  // v73: the program in use (4 = icon_fsh, the pebbles: their meshes are written)
    uint8_t s_meshUbo[0x200];             // v73: the Mesh block as the shaders see it (SetMeshUniforms may update a part)
}

extern "C" {
    void *dkMemBlockGetCpuAddr(DkMemBlock mb) { return mb ? ((SimBlock *)mb)->cpu : nullptr; }
    DkGpuAddr dkMemBlockGetGpuAddr(DkMemBlock mb) { return mb ? (DkGpuAddr)(uintptr_t)((SimBlock *)mb)->cpu : 0; }
    uint32_t dkMemBlockGetSize(DkMemBlock mb) { return mb ? ((SimBlock *)mb)->size : 0; }
}

namespace brume {
    bool Gpu::Init(NWindow *) { m_w = 1280; m_h = 720; m_docked = false; return true; }
    void Gpu::Shutdown() {}
    void Gpu::CheckMode() {}
    bool Gpu::MakeTarget(Target &t, int w, int h, uint32_t id) { t.w = w; t.h = h; t.descriptorId = id; return true; }
    bool Gpu::MakeTexture(Target &t, int w, int h, DkImageFormat fmt, uint32_t bpp, const void *px, uint32_t id) {
        t.w = w; t.h = h; t.descriptorId = id;
        if (id < 256) s_fmt[id] = (int)fmt;
        DumpTex(id, w, h, (int)fmt, px, (uint32_t)w * (uint32_t)h * bpp);
        return true;
    }
    bool Gpu::MakeTextureRaw(Target &t, int w, int h, DkImageFormat fmt, const void *data, uint32_t bytes, uint32_t id, uint32_t) {
        t.w = w; t.h = h; t.descriptorId = id;
        if (id < 256) s_fmt[id] = (int)fmt;
        DumpTex(id, w, h, (int)fmt, data, bytes);
        return true;
    }
    uint32_t Gpu::ImageBytes(int w, int h, DkImageFormat, uint32_t, uint32_t *align) const { if (align) *align = 512; return (uint32_t)(w * h * 4); }
    uint32_t Gpu::ReserveImage(uint32_t bytes, uint32_t) { const uint32_t o = m_imgUsed; m_imgUsed += bytes; return o; }
    bool Gpu::PlaceTexture(Target &t, uint32_t, int w, int h, DkImageFormat fmt, uint32_t, uint32_t id) {
        t.w = w; t.h = h; t.descriptorId = id;
        if (id < 256) s_fmt[id] = (int)fmt;
        return true;
    }
    bool Gpu::StreamTexture(const Target &t, const void *data, uint32_t bytes, uint32_t) {
        DumpTex(t.descriptorId, t.w, t.h, t.descriptorId < 256 ? s_fmt[t.descriptorId] : 0, data, bytes);
        return true;
    }
    bool Gpu::StreamRows(const Target &, const void *, uint32_t, uint32_t) { return true; }
    bool Gpu::UploadNow(const Target &t, const void *data, uint32_t bytes, uint32_t) {
        DumpTex(t.descriptorId, t.w, t.h, t.descriptorId < 256 ? s_fmt[t.descriptorId] : 0, data, bytes);
        return true;
    }
    bool Gpu::UploadRowsNow(const Target &, const void *, uint32_t, uint32_t) { return true; }
    DkMemBlock Gpu::MakeBuffer(uint32_t size) {
        SimBlock *b = (SimBlock *)calloc(1, sizeof(SimBlock));
        b->cpu = calloc(1, size ? size : 1); b->size = size;
        return (DkMemBlock)b;
    }
    bool Gpu::MakeProgram(Program &p, const void *, uint32_t, const void *fs, uint32_t) {
        // the programs are made in a fixed order in onBoot: 0 haze, 1 glass, 2 ui, 3 mesh, 4 icon, 5 box, 6 cubequad,
        // then (v74) 7 verre, the settings icons' glass (Assets::Load), 8 glassbg, 9 glasscube, (v99) 10 uilite, 11 uisurf
        if (s_progs < 16) s_progFs[s_progs] = fs;
        memset(&p.vs, 0, sizeof p.vs);
        uint32_t idx = (uint32_t)s_progs++;
        memcpy(&p.fs, &idx, sizeof idx);   // (the shader object only carries its number here)
        p.ok = true;
        return true;
    }
    DkCmdBuf Gpu::Begin() { m_meshDrawn = false; return nullptr; }
    void Gpu::BindScreen() { Rec(kBindScreen, 0, 0, nullptr, 0); }
    void Gpu::BindTarget(Target &t) { const uint32_t wh[2] = { (uint32_t)t.w, (uint32_t)t.h }; Rec(kBindTarget, t.descriptorId, 0, wh, 8); }
    void Gpu::SetScissor(int x, int y, int w, int h) { const int32_t r[4] = { x, y, w, h }; Rec(kScissor, 0, 0, r, 16); }
    void Gpu::UseProgram(const Program &p) { g_simProgs++; uint32_t idx; memcpy(&idx, &p.fs, sizeof idx); s_prog = idx; m_prog = &p; Rec(kUseProgram, idx, 0, nullptr, 0); }
    void Gpu::SetUniforms(const void *data, uint32_t size) { Rec(kUniforms, 0, 0, data, size); }
    void Gpu::BindTexture(const Target &t) { const uint32_t id = t.descriptorId; Rec(kTextures, 1, 0, &id, 4); }
    void Gpu::BindTextures(const Target *const *ts, int n, bool repeat) {
        uint32_t ids[8] = {};
        for (int i = 0; i < n && i < 8; i++) ids[i] = ts[i] ? ts[i]->descriptorId : 0;
        Rec(kTextures, (uint32_t)n, repeat ? 1 : 0, ids, (uint32_t)(4 * (n < 8 ? n : 8)));
    }
    void Gpu::Barrier() {}
    void Gpu::DrawFullscreen() { Rec(kFullscreen, 0, 0, nullptr, 0); }
    void Gpu::DrawCubeQuads() { Rec(kCubeQuads, 0, 0, nullptr, 0); }
    void Gpu::BeginUi() { Rec(kBeginUi, 0, 0, nullptr, 0); }
    void Gpu::DrawUi(const UiVtx *v, uint32_t count) {
        if (!count) return;
        Rec(kUi, count, 0, v, count * (uint32_t)sizeof(UiVtx));
        // (perf) the pixels each interface quad covers, by mode: what the GPU will shade (the console's « interface » time)
        for (uint32_t i = 0; i + 5 < count; i += 6) {
            float x0 = v[i].x, y0 = v[i].y, x1 = v[i + 2].x, y1 = v[i + 2].y;
            if (x0 > x1) { const float t = x0; x0 = x1; x1 = t; }
            if (y0 > y1) { const float t = y0; y0 = y1; y1 = t; }
            x0 = x0 < 0 ? 0 : x0; y0 = y0 < 0 ? 0 : y0; x1 = x1 > 1280 ? 1280 : x1; y1 = y1 > 720 ? 720 : y1;
            const int m = (int)(v[i].mode + 0.5f);
            if (x1 > x0 && y1 > y0 && m >= 0 && m < 40) { g_simFill[m] += (double)(x1 - x0) * (y1 - y0); g_simQuads[m]++; }
        }
    }
    void Gpu::BeginMesh() { m_meshDrawn = true; Rec(kBeginMesh, 0, 0, nullptr, 0); }
    void Gpu::SetMeshUniforms(const void *data, uint32_t size, uint32_t offset) {
        // (as Gpu.cpp: `size` bytes taken at data + offset, written at offset)
        if (offset < sizeof s_meshUbo) memcpy(s_meshUbo + offset, (const uint8_t *)data + offset, size < sizeof s_meshUbo - offset ? size : sizeof s_meshUbo - offset);
    }
    void Gpu::SetMeshState(bool cullBack, bool blend, bool frontCW) { Rec(kMeshState, (cullBack ? 1u : 0u) | (frontCW ? 2u : 0u), blend ? 1 : 0, nullptr, 0); }
    // v73: a pebble (icon_fsh) is written whole - its Mesh block, then its vertices (32 bytes each) and indices (u32);
    // the other meshes (consoles, boxes) only leave a mark (view.html does not draw them)
    void Gpu::DrawMesh(DkGpuAddr vtx, uint32_t vtxBytes, DkGpuAddr idx, uint32_t idxCount) {
        g_simMeshes++; g_simMeshIdx += (int)idxCount;
        // (v89: the consoles (3, mesh_fsh) and the boxes (5, box_fsh) too - to see a disc go in and out of its console)
        if ((s_prog != 3 && s_prog != 4 && s_prog != 5) || !vtx || !idx || !g_simOut) { Rec(kMesh, 0, 0, nullptr, 0); return; }
        const uint32_t ib = idxCount * 4, bytes = (uint32_t)sizeof s_meshUbo + vtxBytes + ib;
        const uint32_t h[4] = { kMesh, vtxBytes, idxCount, bytes };
        fwrite(h, 4, 4, g_simOut);
        fwrite(s_meshUbo, 1, sizeof s_meshUbo, g_simOut);
        fwrite((const void *)(uintptr_t)vtx, 1, vtxBytes, g_simOut);   // (in the sim a GPU address is the CPU pointer)
        fwrite((const void *)(uintptr_t)idx, 1, ib, g_simOut);
    }
    void Gpu::BeginOverlay() { Rec(kBeginOverlay, 0, 0, nullptr, 0); }
    void Gpu::EndOverlay() { Rec(kEndOverlay, 0, 0, nullptr, 0); }
    bool Gpu::Probe(const char *) { return false; }
    void Gpu::End() {}
    void Gpu::Mark(int) {}
    void Gpu::MarkConsole(int) {}
} // namespace brume
