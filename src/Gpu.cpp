// Brume - deko3d wrapper. See Gpu.hpp for the rules it follows.
#include "Gpu.hpp"
#include <cstdio>
#include <cstring>

namespace brume {

    namespace {
        constexpr uint32_t kCmdSize   = 0x10000;   // command memory per frame in flight
        constexpr uint32_t kCodeSize  = 0x40000;   // all shaders
        // targets + textures: haze 330 KB, font atlases 750 KB, interface atlas 1.8 MB, consoles (BC1) ~3 MB, light 200 KB
        // (the menu has 62 MB in all: keep this tight)
        constexpr uint32_t kImgSize   = 0xE00000;   // 14 MB: textures + their mipmaps (v28: + the settings icons in the interface sheet)
        constexpr uint32_t kMaxImages = 64;         // image descriptors
        constexpr uint32_t kVtxSize   = 0x80000;   // interface vertices per frame in flight (~10900)
        // m_dataMem layout
        constexpr uint32_t kUboOff     = 0x000, kUboSize = 0x200;
        constexpr uint32_t kImgDescOff = 0x200;                    // kMaxImages * 0x20 = 0x800
        constexpr uint32_t kSmpDescOff = 0xA00;                    // 2 samplers: clamped, repeating
        static_assert(kFrames <= 3, "reports + marks must fit before the mesh UBO (0xB00)");
        constexpr uint32_t kReportOff  = 0xA40;                    // kFrames * 2 reports * 16 bytes
        // (0xAA0 - 0xB00: free since v90 - the stage marks moved to 0xE80)
        constexpr uint32_t kMeshUboOff = 0xB00, kMeshUboSize = 0x200;   // the 3D models' parameters (binding 1)
        constexpr uint32_t kConsOff    = 0xD00;                    // kFrames * 8 console marks * 16 bytes (up to 0xE80)
        constexpr uint32_t kMarkOff    = 0xE80;                    // v90: kFrames * 4 stage marks * 16 bytes (up to 0xF40)
        static_assert(kMarkOff + kFrames * Gpu::kMarks * 16 <= 0x1000, "the stage marks fit in the data block");
        constexpr uint32_t kDataSize   = 0x1000;

        uint32_t Align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

        // name != nullptr: the block's GPU range goes to perf.log (to match a « GPU page fault » address)
        DkMemBlock MakeBlock(DkDevice dev, uint32_t size, uint32_t flags, const char *name = nullptr) {
            DkMemBlockMaker mk;
            dkMemBlockMakerDefaults(&mk, dev, Align(size, DK_MEMBLOCK_ALIGNMENT));
            mk.flags = flags;
            DkMemBlock b = dkMemBlockCreate(&mk);
            if (b && name && Gpu::s_log) {
                char line[96];
                const unsigned long long a = dkMemBlockGetGpuAddr(b);
                snprintf(line, sizeof line, "bloc %s : %llx - %llx", name, a, a + Align(size, DK_MEMBLOCK_ALIGNMENT));
                Gpu::s_log(line);
            }
            return b;
        }
    }

    static void DebugCb(void *, const char *context, DkResult result, const char *message) {
        char line[256];
        snprintf(line, sizeof line, "%s (%d) : %s", context ? context : "?", (int)result, message ? message : "");
        if (Gpu::s_log) Gpu::s_log(line);
    }

    bool Gpu::Init(NWindow *win) {
        m_win = win;
        DkDeviceMaker dm;
        dkDeviceMakerDefaults(&dm);             // origin upper-left, Y up (see fullscreen_vsh)
        dm.cbDebug = DebugCb;
        m_dev = dkDeviceCreate(&dm);
        if (!m_dev) return false;

        m_cmdMem  = MakeBlock(m_dev, kCmdSize * kFrames, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, "commandes");
        m_dataMem = MakeBlock(m_dev, kDataSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, "donnees");
        m_codeMem = MakeBlock(m_dev, kCodeSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, "shaders");
        m_imgMem  = MakeBlock(m_dev, kImgSize, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, "images");
        m_vtxMem  = MakeBlock(m_dev, kVtxSize * kFrames, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, "sommets-ui");
        if (!m_cmdMem || !m_dataMem || !m_codeMem || !m_imgMem || !m_vtxMem) return false;
        m_streamMem = MakeBlock(m_dev, kStreamMax * kFrames, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, "envois");   // v31 (optional)
        memset(dkMemBlockGetCpuAddr(m_dataMem), 0, kDataSize);

        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, m_dev);
        m_cmd = dkCmdBufCreate(&cm);
        DkQueueMaker qm;
        dkQueueMakerDefaults(&qm, m_dev);
        qm.flags = DkQueueFlags_Graphics;
        m_queue = dkQueueCreate(&qm);
        if (!m_cmd || !m_queue) return false;

        // One sampler: bilinear, clamped (the haze is upscaled with it).
        DkSampler s;
        dkSamplerDefaults(&s);
        s.minFilter = DkFilter_Linear;
        s.magFilter = DkFilter_Linear;
        s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_ClampToEdge;
        auto *smp = (DkSamplerDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kSmpDescOff);
        dkSamplerDescriptorInitialize(smp, &s);
        s.wrapMode[0] = s.wrapMode[1] = s.wrapMode[2] = DkWrapMode_Repeat;   // 1: the consoles' textures
        s.mipFilter = DkMipFilter_Linear;                                    //    with their mipmaps
        dkSamplerDescriptorInitialize(smp + 1, &s);

        m_docked = appletGetOperationMode() == AppletOperationMode_Console;
        return CreateSwapchain();
    }

    bool Gpu::CreateSwapchain() {
        m_w = m_docked ? 1920 : 1280;
        m_h = m_docked ? 1080 : 720;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
        lm.format = DkImageFormat_RGBA8_Unorm;
        lm.dimensions[0] = (uint32_t)m_w;
        lm.dimensions[1] = (uint32_t)m_h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        const uint32_t align = dkImageLayoutGetAlignment(&layout);
        const uint32_t size  = Align((uint32_t)dkImageLayoutGetSize(&layout), align);
        m_fbMem = MakeBlock(m_dev, size * kFrames, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, "ecran");
        if (!m_fbMem) return false;
        const DkImage *imgs[kFrames];
        for (int i = 0; i < kFrames; i++) {
            dkImageInitialize(&m_fb[i], &layout, m_fbMem, size * (uint32_t)i);
            dkImageViewDefaults(&m_fbView[i], &m_fb[i]);
            imgs[i] = &m_fb[i];
        }
        // depth for the 3D models (16 bits: the scene spans ~20 units, near plane 0.3)
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.flags = DkImageFlags_UsageRender;
        lm.format = DkImageFormat_Z16;
        lm.dimensions[0] = (uint32_t)m_w; lm.dimensions[1] = (uint32_t)m_h;
        DkImageLayout dl;
        dkImageLayoutInitialize(&dl, &lm);
        m_depthMem = MakeBlock(m_dev, Align((uint32_t)dkImageLayoutGetSize(&dl), dkImageLayoutGetAlignment(&dl)), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, "profondeur");
        if (!m_depthMem) return false;
        dkImageInitialize(&m_depth, &dl, m_depthMem, 0);
        dkImageViewDefaults(&m_depthView, &m_depth);
        nwindowSetDimensions(m_win, (u32)m_w, (u32)m_h);
        DkSwapchainMaker sm;
        dkSwapchainMakerDefaults(&sm, m_dev, m_win, imgs, kFrames);
        m_swapchain = dkSwapchainCreate(&sm);
        return m_swapchain != nullptr;
    }

    void Gpu::DestroySwapchain() {
        if (m_queue) dkQueueWaitIdle(m_queue);
        if (m_swapchain) { dkSwapchainDestroy(m_swapchain); m_swapchain = nullptr; }
        if (m_fbMem) { dkMemBlockDestroy(m_fbMem); m_fbMem = nullptr; }
        if (m_depthMem) { dkMemBlockDestroy(m_depthMem); m_depthMem = nullptr; }
    }

    void Gpu::CheckMode() {
        const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
        if (docked == m_docked) return;
        DestroySwapchain();
        m_docked = docked;
        CreateSwapchain();
    }

    void Gpu::Shutdown() {
        DestroySwapchain();
        if (m_queue)   { dkQueueDestroy(m_queue); m_queue = nullptr; }
        if (m_cmd)     { dkCmdBufDestroy(m_cmd); m_cmd = nullptr; }
        if (m_imgMem)  { dkMemBlockDestroy(m_imgMem); m_imgMem = nullptr; }
        if (m_streamMem) { dkMemBlockDestroy(m_streamMem); m_streamMem = nullptr; }
        if (m_vtxMem)  { dkMemBlockDestroy(m_vtxMem); m_vtxMem = nullptr; }
        if (m_codeMem) { dkMemBlockDestroy(m_codeMem); m_codeMem = nullptr; }
        if (m_dataMem) { dkMemBlockDestroy(m_dataMem); m_dataMem = nullptr; }
        if (m_cmdMem)  { dkMemBlockDestroy(m_cmdMem); m_cmdMem = nullptr; }
        if (m_dev)     { dkDeviceDestroy(m_dev); m_dev = nullptr; }
    }

    uint32_t Gpu::AllocImage(uint64_t size, uint32_t align) {
        const uint32_t off = Align(m_imgUsed, align);
        if (off + size > kImgSize) return UINT32_MAX;
        m_imgUsed = off + (uint32_t)size;
        if (s_log) { char line[64]; snprintf(line, sizeof line, "  image +%x (%x octets)", off, (unsigned)size); s_log(line); }
        return off;
    }

    bool Gpu::MakeTarget(Target &t, int w, int h, uint32_t descriptorId) {
        if (descriptorId >= kMaxImages) return false;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.flags = DkImageFlags_UsageRender | DkImageFlags_HwCompression;
        lm.format = DkImageFormat_RGBA8_Unorm;
        lm.dimensions[0] = (uint32_t)w;
        lm.dimensions[1] = (uint32_t)h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        const uint32_t off = AllocImage(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout));
        if (off == UINT32_MAX) return false;
        dkImageInitialize(&t.image, &layout, m_imgMem, off);
        dkImageViewDefaults(&t.view, &t.image);
        t.w = w; t.h = h; t.descriptorId = descriptorId;
        auto *desc = (DkImageDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kImgDescOff) + descriptorId;
        dkImageDescriptorInitialize(desc, &t.view, false, false);
        return true;
    }

    // A sampled texture filled from CPU memory (font atlases): staging copy, one
    // command list, wait. Only at boot.
    bool Gpu::MakeTexture(Target &t, int w, int h, DkImageFormat fmt, uint32_t bytesPerPixel,
                          const void *pixels, uint32_t descriptorId) {
        if (descriptorId >= kMaxImages) return false;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.format = fmt;
        lm.dimensions[0] = (uint32_t)w;
        lm.dimensions[1] = (uint32_t)h;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        const uint32_t off = AllocImage(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout));
        if (off == UINT32_MAX) return false;
        dkImageInitialize(&t.image, &layout, m_imgMem, off);
        dkImageViewDefaults(&t.view, &t.image);
        t.w = w; t.h = h; t.descriptorId = descriptorId;
        auto *desc = (DkImageDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kImgDescOff) + descriptorId;
        dkImageDescriptorInitialize(desc, &t.view, false, false);

        const uint32_t bytes = (uint32_t)w * (uint32_t)h * bytesPerPixel;
        DkMemBlock staging = MakeBlock(m_dev, bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!staging) return false;
        memcpy(dkMemBlockGetCpuAddr(staging), pixels, bytes);
        dkCmdBufClear(m_cmd);
        dkCmdBufAddMemory(m_cmd, m_cmdMem, 0, kCmdSize);
        DkCopyBuf src = { dkMemBlockGetGpuAddr(staging), 0, 0 };
        DkImageRect rect = { 0, 0, 0, (uint32_t)w, (uint32_t)h, 1 };
        dkCmdBufCopyBufferToImage(m_cmd, &src, &t.view, &rect, 0);
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueWaitIdle(m_queue);
        dkMemBlockDestroy(staging);
        return true;
    }

    bool Gpu::MakeTextureRaw(Target &t, int w, int h, DkImageFormat fmt, const void *data, uint32_t bytes, uint32_t descriptorId, uint32_t levels) {
        if (descriptorId >= kMaxImages || !data || !bytes || !levels) return false;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.format = fmt;
        lm.dimensions[0] = (uint32_t)w;
        lm.dimensions[1] = (uint32_t)h;
        lm.mipLevels = levels;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        const uint32_t off = AllocImage(dkImageLayoutGetSize(&layout), dkImageLayoutGetAlignment(&layout));
        if (off == UINT32_MAX) return false;
        dkImageInitialize(&t.image, &layout, m_imgMem, off);
        dkImageViewDefaults(&t.view, &t.image);
        t.w = w; t.h = h; t.descriptorId = descriptorId;
        auto *desc = (DkImageDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kImgDescOff) + descriptorId;
        dkImageDescriptorInitialize(desc, &t.view, false, false);
        DkMemBlock staging = MakeBlock(m_dev, bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!staging) return false;
        memcpy(dkMemBlockGetCpuAddr(staging), data, bytes);
        dkCmdBufClear(m_cmd);
        dkCmdBufAddMemory(m_cmd, m_cmdMem, 0, kCmdSize);
        // each mip level follows the previous one in `data` (BC1: 8 bytes per 4x4 block; other formats: 1 level)
        uint64_t off2 = 0;
        for (uint32_t l = 0; l < levels; l++) {
            const uint32_t lw = (uint32_t)w >> l, lh = (uint32_t)h >> l;
            DkImageView lv = t.view; lv.mipLevelOffset = (uint8_t)l; lv.mipLevelCount = 1;
            DkCopyBuf src = { dkMemBlockGetGpuAddr(staging) + off2, 0, 0 };
            DkImageRect rect = { 0, 0, 0, lw, lh, 1 };
            dkCmdBufCopyBufferToImage(m_cmd, &src, &lv, &rect, 0);
            off2 += levels > 1 ? (uint64_t)(lw / 4) * (lh / 4) * 8 : bytes;
        }
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueWaitIdle(m_queue);
        dkMemBlockDestroy(staging);
        return true;
    }

    // ---- v31: streamed textures (the game covers) ----
    uint32_t Gpu::ImageBytes(int w, int h, DkImageFormat fmt, uint32_t levels, uint32_t *align) const {
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.format = fmt;
        lm.dimensions[0] = (uint32_t)w;
        lm.dimensions[1] = (uint32_t)h;
        lm.mipLevels = levels;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        if (align) *align = dkImageLayoutGetAlignment(&layout);
        return (uint32_t)dkImageLayoutGetSize(&layout);
    }

    uint32_t Gpu::ReserveImage(uint32_t bytes, uint32_t align) { return AllocImage(bytes, align); }

    bool Gpu::PlaceTexture(Target &t, uint32_t off, int w, int h, DkImageFormat fmt, uint32_t levels, uint32_t descriptorId) {
        if (descriptorId >= kMaxImages || off == UINT32_MAX) return false;
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, m_dev);
        lm.format = fmt;
        lm.dimensions[0] = (uint32_t)w;
        lm.dimensions[1] = (uint32_t)h;
        lm.mipLevels = levels;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        if (off % dkImageLayoutGetAlignment(&layout)) return false;
        dkImageInitialize(&t.image, &layout, m_imgMem, off);
        dkImageViewDefaults(&t.view, &t.image);
        t.w = w; t.h = h; t.descriptorId = descriptorId;
        auto *desc = (DkImageDescriptor *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kImgDescOff) + descriptorId;
        dkImageDescriptorInitialize(desc, &t.view, false, false);
        return true;
    }

    // BC1 levels one after the other in `data` (8 bytes per 4 x 4 block), copied into this frame's staging area
    // (the frame's fence, waited in Begin, guarantees the GPU is done with it), then one barrier before any draw.
    bool Gpu::StreamTexture(const Target &t, const void *data, uint32_t bytes, uint32_t levels) {
        if (!m_streamMem || !data || !bytes || bytes > kStreamMax || !levels || m_slot < 0 || m_streamed) return false;
        m_streamed = true;
        uint8_t *dst = (uint8_t *)dkMemBlockGetCpuAddr(m_streamMem) + kStreamMax * (uint32_t)m_frame;
        memcpy(dst, data, bytes);
        const DkGpuAddr base = dkMemBlockGetGpuAddr(m_streamMem) + kStreamMax * (uint32_t)m_frame;
        uint64_t off = 0;
        for (uint32_t l = 0; l < levels; l++) {
            const uint32_t lw = (uint32_t)t.w >> l, lh = (uint32_t)t.h >> l;
            const uint64_t lb = (uint64_t)(lw / 4) * (lh / 4) * 8;
            if (off + lb > bytes) return false;
            DkImageView lv = t.view; lv.mipLevelOffset = (uint8_t)l; lv.mipLevelCount = 1;
            DkCopyBuf src = { base + off, 0, 0 };
            DkImageRect rect = { 0, 0, 0, lw, lh, 1 };
            dkCmdBufCopyBufferToImage(m_cmd, &src, &lv, &rect, 0);
            off += lb;
        }
        dkCmdBufBarrier(m_cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        return true;
    }

    bool Gpu::StreamRows(const Target &t, const void *data, uint32_t y, uint32_t rows) {
        if (!m_streamMem || !data || !rows || (rows & 3) || (y & 3) || y + rows > (uint32_t)t.h || m_slot < 0 || m_streamed) return false;
        const uint32_t bytes = ((uint32_t)t.w / 4) * (rows / 4) * 8;
        if (bytes > kStreamMax) return false;
        m_streamed = true;
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(m_streamMem) + kStreamMax * (uint32_t)m_frame, data, bytes);
        DkCopyBuf src = { dkMemBlockGetGpuAddr(m_streamMem) + kStreamMax * (uint32_t)m_frame, 0, 0 };
        DkImageView lv = t.view; lv.mipLevelOffset = 0; lv.mipLevelCount = 1;
        DkImageRect rect = { 0, y, 0, (uint32_t)t.w, rows, 1 };
        dkCmdBufCopyBufferToImage(m_cmd, &src, &lv, &rect, 0);
        dkCmdBufBarrier(m_cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        return true;
    }

    bool Gpu::UploadNow(const Target &t, const void *data, uint32_t bytes, uint32_t levels) {
        if (!data || !bytes || !levels) return false;
        dkQueueWaitIdle(m_queue);                        // nothing in flight any more: the command memory is free
        DkMemBlock staging = MakeBlock(m_dev, bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!staging) return false;
        memcpy(dkMemBlockGetCpuAddr(staging), data, bytes);
        dkCmdBufClear(m_cmd);
        dkCmdBufAddMemory(m_cmd, m_cmdMem, 0, kCmdSize);
        uint64_t off = 0;
        for (uint32_t l = 0; l < levels; l++) {
            const uint32_t lw = (uint32_t)t.w >> l, lh = (uint32_t)t.h >> l;
            DkImageView lv = t.view; lv.mipLevelOffset = (uint8_t)l; lv.mipLevelCount = 1;
            DkCopyBuf src = { dkMemBlockGetGpuAddr(staging) + off, 0, 0 };
            DkImageRect rect = { 0, 0, 0, lw, lh, 1 };
            dkCmdBufCopyBufferToImage(m_cmd, &src, &lv, &rect, 0);
            off += (uint64_t)(lw / 4) * (lh / 4) * 8;
        }
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueWaitIdle(m_queue);
        dkMemBlockDestroy(staging);
        return true;
    }

    // v45 - rows y .. y+rows of an R8 image (the clock's band of the font atlas), outside a frame (before Begin)
    bool Gpu::UploadRowsNow(const Target &t, const void *data, uint32_t y, uint32_t rows) {
        if (!data || !rows || y + rows > (uint32_t)t.h) return false;
        const uint32_t bytes = (uint32_t)t.w * rows;
        dkQueueWaitIdle(m_queue);
        DkMemBlock staging = MakeBlock(m_dev, bytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!staging) return false;
        memcpy(dkMemBlockGetCpuAddr(staging), data, bytes);
        dkCmdBufClear(m_cmd);
        dkCmdBufAddMemory(m_cmd, m_cmdMem, 0, kCmdSize);
        DkImageView lv = t.view; lv.mipLevelOffset = 0; lv.mipLevelCount = 1;
        DkCopyBuf src = { dkMemBlockGetGpuAddr(staging), 0, 0 };
        DkImageRect rect = { 0, y, 0, (uint32_t)t.w, rows, 1 };
        dkCmdBufCopyBufferToImage(m_cmd, &src, &lv, &rect, 0);
        dkCmdBufBarrier(m_cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueWaitIdle(m_queue);
        dkMemBlockDestroy(staging);
        return true;
    }

    DkMemBlock Gpu::MakeBuffer(uint32_t size) {
        return MakeBlock(m_dev, size, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, "geometrie");
    }

    uint32_t Gpu::AllocCode(const void *data, uint32_t size) {
        const uint32_t off = Align(m_codeUsed, DK_SHADER_CODE_ALIGNMENT);
        if (off + size + DK_SHADER_CODE_UNUSABLE_SIZE > kCodeSize) return UINT32_MAX;
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(m_codeMem) + off, data, size);
        m_codeUsed = off + size;
        return off;
    }

    bool Gpu::MakeProgram(Program &p, const void *vs, uint32_t vsSize, const void *fs, uint32_t fsSize) {
        const uint32_t vo = AllocCode(vs, vsSize), fo = AllocCode(fs, fsSize);
        if (vo == UINT32_MAX || fo == UINT32_MAX) return false;
        DkShaderMaker sm;
        dkShaderMakerDefaults(&sm, m_codeMem, vo);
        dkShaderInitialize(&p.vs, &sm);
        dkShaderMakerDefaults(&sm, m_codeMem, fo);
        dkShaderInitialize(&p.fs, &sm);
        p.ok = true;
        return true;
    }

    DkCmdBuf Gpu::Begin() {
        const int f = m_frame;
        const u64 w0 = armGetSystemTick();
        if (m_fenceUsed[f]) {
            dkFenceWait(&m_fence[f], -1);
            // The two timestamps this slot wrote last time: {counter, timestamp} each.
            const uint64_t *r = (const uint64_t *)((const uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kReportOff + f * 32);
            const uint64_t a = r[1] ? r[1] : r[0], b = r[3] ? r[3] : r[2];
            if (b > a) m_gpuMs = (float)dkTimestampToNs(b - a) / 1e6f;
            uint64_t *mk = (uint64_t *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kMarkOff + f * kMarks * 16);
            uint64_t prev = a;
            for (int k = 0; k <= kMarks; k++) {          // v90: 5 stages (a mark not written this image: its stage is 0)
                const uint64_t e = k < kMarks ? (mk[k * 2 + 1] ? mk[k * 2 + 1] : mk[k * 2]) : b;
                m_stageMs[k] = e > prev && e <= b ? (float)dkTimestampToNs(e - prev) / 1e6f : 0.0f;
                if (e > prev && e <= b) prev = e;
            }
            // per console: its mark minus the latest earlier mark of this image (start = the fog's end, mark 0)
            uint64_t *cm = (uint64_t *)((uint8_t *)dkMemBlockGetCpuAddr(m_dataMem) + kConsOff + f * 128);
            const uint64_t m0 = mk[3] ? mk[3] : mk[2];   // (the consoles start after mark 1: the fog and the glass)
            uint64_t ts[8];
            for (int i = 0; i < 8; i++) { ts[i] = cm[i * 2 + 1] ? cm[i * 2 + 1] : cm[i * 2]; m_consMs[i] = 0; }
            for (int i = 0; i < 8; i++) {
                if (ts[i] <= m0 || ts[i] > b) continue;   // not drawn in this image
                uint64_t p = m0;
                for (int j = 0; j < 8; j++) if (j != i && ts[j] > p && ts[j] < ts[i] && ts[j] <= b) p = ts[j];
                m_consMs[i] = (float)dkTimestampToNs(ts[i] - p) / 1e6f;
            }
            for (int i = 0; i < 16; i++) cm[i] = 0;
            for (int i = 0; i < kMarks * 2; i++) mk[i] = 0;
        }
        const u64 fenceTicks = armGetSystemTick() - w0;
        dkCmdBufClear(m_cmd);
        dkCmdBufAddMemory(m_cmd, m_cmdMem, kCmdSize * (uint32_t)f, kCmdSize);
        m_vtxUsed = 0;
        m_meshDrawn = false;
        m_streamed = false;
        const u64 a0 = armGetSystemTick();
        m_slot = dkQueueAcquireImage(m_queue, m_swapchain);
        m_acquireTicks = armGetSystemTick() - a0 + fenceTicks;   // all the waiting, not CPU work

        const DkGpuAddr data = dkMemBlockGetGpuAddr(m_dataMem);
        // The previous image may still be reading the fog target (glass panels, cubes) while this one is about to
        // overwrite it: once the GPU is busy all the time (the wheel's consoles), the two overlap and the GPU faults
        // (compressed target written while sampled). Wait for the previous fragments first; cheap, once per image.
        dkCmdBufBarrier(m_cmd, DkBarrier_Fragments, DkInvalidateFlags_Descriptors | DkInvalidateFlags_Image);
        dkCmdBufBindImageDescriptorSet(m_cmd, data + kImgDescOff, kMaxImages);
        dkCmdBufBindSamplerDescriptorSet(m_cmd, data + kSmpDescOff, 2);
        dkCmdBufBindUniformBuffer(m_cmd, DkStage_Vertex, 0, data + kUboOff, kUboSize);
        dkCmdBufBindUniformBuffer(m_cmd, DkStage_Fragment, 0, data + kUboOff, kUboSize);
        dkCmdBufBindUniformBuffer(m_cmd, DkStage_Vertex, 1, data + kMeshUboOff, kMeshUboSize);
        dkCmdBufBindUniformBuffer(m_cmd, DkStage_Fragment, 1, data + kMeshUboOff, kMeshUboSize);

        DkRasterizerState rs;  dkRasterizerStateDefaults(&rs);  rs.cullMode = DkFace_None;
        DkColorState cs;       dkColorStateDefaults(&cs);
        DkColorWriteState cw;  dkColorWriteStateDefaults(&cw);
        DkDepthStencilState ds; dkDepthStencilStateDefaults(&ds);
        ds.depthTestEnable = false; ds.depthWriteEnable = false;
        dkCmdBufBindRasterizerState(m_cmd, &rs);
        dkCmdBufBindColorState(m_cmd, &cs);
        dkCmdBufBindColorWriteState(m_cmd, &cw);
        dkCmdBufBindDepthStencilState(m_cmd, &ds);
        dkCmdBufBindVtxAttribState(m_cmd, nullptr, 0);
        dkCmdBufBindVtxBufferState(m_cmd, nullptr, 0);

        dkCmdBufReportCounter(m_cmd, DkCounter_Timestamp, data + kReportOff + f * 32);
        return m_cmd;
    }

    static void Viewport(DkCmdBuf c, int w, int h) {
        DkViewport v = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        DkScissor  s = { 0, 0, (uint32_t)w, (uint32_t)h };
        dkCmdBufSetViewports(c, 0, &v, 1);
        dkCmdBufSetScissors(c, 0, &s, 1);
    }

    void Gpu::BindScreen() {
        const DkImageView *rt = &m_fbView[m_slot];
        dkCmdBufBindRenderTargets(m_cmd, &rt, 1, nullptr);
        Viewport(m_cmd, m_w, m_h);
    }

    void Gpu::BindTarget(Target &t) {
        const DkImageView *rt = &t.view;
        dkCmdBufBindRenderTargets(m_cmd, &rt, 1, nullptr);
        Viewport(m_cmd, t.w, t.h);
    }

    void Gpu::SetScissor(int x, int y, int w, int h) {
        DkScissor s = { (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h };
        dkCmdBufSetScissors(m_cmd, 0, &s, 1);
    }

    void Gpu::UseProgram(const Program &p) {
        m_prog = &p;
        const DkShader *sh[2] = { &p.vs, &p.fs };
        dkCmdBufBindShaders(m_cmd, DkStageFlag_GraphicsMask, sh, 2);
    }

    void Gpu::SetUniforms(const void *data, uint32_t size) {
        dkCmdBufPushConstants(m_cmd, dkMemBlockGetGpuAddr(m_dataMem) + kUboOff, kUboSize, 0, size, data);
    }

    void Gpu::BindTexture(const Target &t) {
        const DkResHandle h = dkMakeTextureHandle(t.descriptorId, 0);
        dkCmdBufBindTextures(m_cmd, DkStage_Fragment, 0, &h, 1);
    }

    void Gpu::BindTextures(const Target *const *ts, int n, bool repeat) {
        DkResHandle h[8];
        if (n > 8) n = 8;
        for (int i = 0; i < n; i++) h[i] = dkMakeTextureHandle(ts[i]->descriptorId, repeat && (i == 0 || i == 3) ? 1 : 0);
        dkCmdBufBindTextures(m_cmd, DkStage_Fragment, 0, h, (uint32_t)n);
    }

    void Gpu::Barrier() { dkCmdBufBarrier(m_cmd, DkBarrier_Fragments, DkInvalidateFlags_Image); }

    // Interface: premultiplied blending, 4 attributes (pos, uv, colour, params).
    void Gpu::BeginUi() {
        DkDepthStencilState ds; dkDepthStencilStateDefaults(&ds);
        ds.depthTestEnable = false; ds.depthWriteEnable = false;
        dkCmdBufBindDepthStencilState(m_cmd, &ds);
        DkRasterizerState rs; dkRasterizerStateDefaults(&rs); rs.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(m_cmd, &rs);
        BindScreen();
        DkColorState cs; dkColorStateDefaults(&cs);
        dkColorStateSetBlendEnable(&cs, 0, true);
        dkCmdBufBindColorState(m_cmd, &cs);
        DkBlendState bl; dkBlendStateDefaults(&bl);
        dkBlendStateSetOps(&bl, DkBlendOp_Add, DkBlendOp_Add);
        dkBlendStateSetFactors(&bl, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
        dkCmdBufBindBlendStates(m_cmd, 0, &bl, 1);
        DkVtxAttribState attr[4];
        memset(attr, 0, sizeof(attr));
        const uint32_t offs[4] = { 0, 8, 16, 32 };
        const DkVtxAttribSize sizes[4] = { DkVtxAttribSize_2x32, DkVtxAttribSize_2x32, DkVtxAttribSize_4x32, DkVtxAttribSize_4x32 };
        for (int i = 0; i < 4; i++) {
            attr[i].bufferId = 0; attr[i].isFixed = 0; attr[i].offset = offs[i];
            attr[i].size = sizes[i]; attr[i].type = DkVtxAttribType_Float; attr[i].isBgra = 0;
        }
        dkCmdBufBindVtxAttribState(m_cmd, attr, 4);
        DkVtxBufferState vb = { (uint32_t)sizeof(UiVtx), 0 };
        dkCmdBufBindVtxBufferState(m_cmd, &vb, 1);
    }

    void Gpu::DrawUi(const UiVtx *v, uint32_t count) {
        if (!count) return;
        const uint32_t bytes = count * (uint32_t)sizeof(UiVtx);
        if (m_vtxUsed + bytes > kVtxSize) return;   // full: drop (never expected)
        const uint32_t off = kVtxSize * (uint32_t)m_frame + m_vtxUsed;
        memcpy((uint8_t *)dkMemBlockGetCpuAddr(m_vtxMem) + off, v, bytes);
        DkBufExtents ext = { dkMemBlockGetGpuAddr(m_vtxMem) + off, bytes };
        dkCmdBufBindVtxBuffers(m_cmd, 0, &ext, 1);
        dkCmdBufDraw(m_cmd, DkPrimitive_Triangles, count, 1, 0, 0);
        m_vtxUsed += (bytes + 255) & ~255u;
    }

    // 3D models: screen + depth, depth test LESS (depth 0..1, cleared to 1), back faces culled,
    // vertices = pos(3 floats), normal(3 floats), uv(2 floats) = 32 bytes.
    void Gpu::BeginMesh() {
        const DkImageView *rt = &m_fbView[m_slot];
        dkCmdBufBindRenderTargets(m_cmd, &rt, 1, &m_depthView);
        Viewport(m_cmd, m_w, m_h);
        dkCmdBufClearDepthStencil(m_cmd, true, 1.0f, 0, 0);
        m_meshDrawn = true;
        DkVtxAttribState attr[3];
        memset(attr, 0, sizeof(attr));
        const uint32_t offs[3] = { 0, 12, 24 };
        const DkVtxAttribSize sizes[3] = { DkVtxAttribSize_3x32, DkVtxAttribSize_3x32, DkVtxAttribSize_2x32 };
        for (int i = 0; i < 3; i++) {
            attr[i].bufferId = 0; attr[i].isFixed = 0; attr[i].offset = offs[i];
            attr[i].size = sizes[i]; attr[i].type = DkVtxAttribType_Float; attr[i].isBgra = 0;
        }
        dkCmdBufBindVtxAttribState(m_cmd, attr, 3);
        DkVtxBufferState vb = { 32, 0 };
        dkCmdBufBindVtxBufferState(m_cmd, &vb, 1);
        SetMeshState(true, false);
    }

    void Gpu::SetMeshState(bool cullBack, bool blend, bool frontCW) {
        DkRasterizerState rs; dkRasterizerStateDefaults(&rs);
        rs.cullMode = cullBack ? DkFace_Back : DkFace_None;
        rs.frontFace = frontCW ? DkFrontFace_CW : DkFrontFace_CCW;
        dkCmdBufBindRasterizerState(m_cmd, &rs);
        DkDepthStencilState ds; dkDepthStencilStateDefaults(&ds);
        ds.depthTestEnable = true; ds.depthWriteEnable = !blend; ds.depthCompareOp = DkCompareOp_Less;
        dkCmdBufBindDepthStencilState(m_cmd, &ds);
        DkColorState cs; dkColorStateDefaults(&cs);
        dkColorStateSetBlendEnable(&cs, 0, blend);
        dkCmdBufBindColorState(m_cmd, &cs);
        if (blend) {
            DkBlendState bl; dkBlendStateDefaults(&bl);
            dkBlendStateSetOps(&bl, DkBlendOp_Add, DkBlendOp_Add);
            dkBlendStateSetFactors(&bl, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
            dkCmdBufBindBlendStates(m_cmd, 0, &bl, 1);
        }
    }

    // v29 - the water cubes in front of the consoles (glass_fsh, uScreen.w = 1)
    void Gpu::BeginOverlay() {
        const DkImageView *rt = &m_fbView[m_slot];
        dkCmdBufBindRenderTargets(m_cmd, &rt, 1, &m_depthView);
        Viewport(m_cmd, m_w, m_h);
        DkRasterizerState rs; dkRasterizerStateDefaults(&rs); rs.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(m_cmd, &rs);
        DkDepthStencilState ds; dkDepthStencilStateDefaults(&ds);
        ds.depthTestEnable = true; ds.depthWriteEnable = false; ds.depthCompareOp = DkCompareOp_Less;
        dkCmdBufBindDepthStencilState(m_cmd, &ds);
        dkCmdBufSetDepthBounds(m_cmd, true, 0.0f, 0.99998f);   // the background (cleared to 1) is skipped
        DkColorState cs; dkColorStateDefaults(&cs);
        dkColorStateSetBlendEnable(&cs, 0, true);
        dkCmdBufBindColorState(m_cmd, &cs);
        DkBlendState bl; dkBlendStateDefaults(&bl);
        dkBlendStateSetOps(&bl, DkBlendOp_Add, DkBlendOp_Add);
        dkBlendStateSetFactors(&bl, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha);
        dkCmdBufBindBlendStates(m_cmd, 0, &bl, 1);
    }
    void Gpu::EndOverlay() { dkCmdBufSetDepthBounds(m_cmd, false, 0.0f, 1.0f); }

    void Gpu::SetMeshUniforms(const void *data, uint32_t size, uint32_t offset) {
        dkCmdBufPushConstants(m_cmd, dkMemBlockGetGpuAddr(m_dataMem) + kMeshUboOff, kMeshUboSize, offset, size, (const uint8_t *)data + offset);
    }

    void Gpu::DrawMesh(DkGpuAddr vtx, uint32_t vtxBytes, DkGpuAddr idx, uint32_t idxCount) {
        DkBufExtents ext = { vtx, vtxBytes };
        dkCmdBufBindVtxBuffers(m_cmd, 0, &ext, 1);
        dkCmdBufBindIdxBuffer(m_cmd, DkIdxFormat_Uint32, idx);
        dkCmdBufDrawIndexed(m_cmd, DkPrimitive_Triangles, idxCount, 1, 0, 0, 0);
    }

    extern "C" bool dkQueueIsInErrorState(DkQueue obj);   // exported by deko3d (neko3d.def), not in its header

    bool Gpu::Probe(const char *tag) {
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueWaitIdle(m_queue);
        if (!dkQueueIsInErrorState(m_queue)) return true;
        char line[160];
        snprintf(line, sizeof line, "ERREUR GPU apres : %s", tag);   // lang:log (never drawn)
        if (s_log) s_log(line);
        return false;
    }

    void Gpu::MarkConsole(int i) {
        if (i < 0 || i > 7) return;
        dkCmdBufReportCounter(m_cmd, DkCounter_Timestamp, dkMemBlockGetGpuAddr(m_dataMem) + kConsOff + m_frame * 128 + i * 16);
    }

    void Gpu::Mark(int k) {
        if (k < 0 || k >= kMarks) return;
        dkCmdBufReportCounter(m_cmd, DkCounter_Timestamp, dkMemBlockGetGpuAddr(m_dataMem) + kMarkOff + m_frame * kMarks * 16 + k * 16);
    }

    void Gpu::DrawFullscreen() { dkCmdBufDraw(m_cmd, DkPrimitive_Triangles, 3, 1, 0, 0); }
    void Gpu::DrawCubeQuads() { dkCmdBufDraw(m_cmd, DkPrimitive_Triangles, 6, 6, 0, 0); }   // v29.1: one square per cube (cubequad_vsh)

    void Gpu::End() {
        const int f = m_frame;
        dkCmdBufReportCounter(m_cmd, DkCounter_Timestamp, dkMemBlockGetGpuAddr(m_dataMem) + kReportOff + f * 32 + 16);
        dkQueueSubmitCommands(m_queue, dkCmdBufFinishList(m_cmd));
        dkQueueSignalFence(m_queue, &m_fence[f], false);
        dkQueuePresentImage(m_queue, m_swapchain, m_slot);
        m_fenceUsed[f] = true;
        m_frame = (m_frame + 1) % kFrames;
    }

} // namespace brume
