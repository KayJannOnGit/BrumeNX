// Brume - v31: the game boxes' model and covers. See Shelf.hpp.
#include "Shelf.hpp"
#include "Worker.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" {
    int qext_title_icon_size(int index);
    int qext_title_icon(int index, void *out, unsigned cap);
}

namespace brume {

    bool DecodeJpegSquare(const void *data, int bytes, uint8_t *out, int size);   // Jpeg.cpp

    namespace {
        // the places hold any of these (BC1, 3 mip levels): portrait / landscape covers, square ones, Switch icons
        constexpr int kLayouts[3][2] = { { 256, 512 }, { 512, 256 }, { 256, 256 } };
        constexpr uint32_t kLevels = 3;
        uint8_t s_buf[Gpu::kStreamMax];          // one cover's data, as it goes to the GPU
        uint8_t s_jpg[0x30000];                  // a Switch icon as the loader gives it (JPEG, ~20-120 KB)
        uint8_t s_rgba[256 * 256 * 4];           // ... decoded
        Worker  s_lw;                            // v90: the load's worker (see Load)
        bool    s_ljReady = false;               // ... its load is done (main thread), not uploaded yet
        bool    s_lwIdle() { return s_lw.Idle() && !s_ljReady; }

        uint16_t To565(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }
        void Expand(uint16_t c, int *o) { o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31; }

        // BC1 (opaque): per 4 x 4 block, the darkest and the brightest pixels as the two ends, each pixel the nearest
        // of the 4 colours between them - the same simple encoder as tools/art-builder (good enough for an icon)
    }
    uint8_t *Shelf::EncodeBC1(const uint8_t *rgba, int w, int h, uint8_t *out) {
            for (int by = 0; by < h; by += 4)
                for (int bx = 0; bx < w; bx += 4) {
                    int px[16][3], lo = 0, hi = 0, lmin = 1 << 30, lmax = -1;
                    for (int i = 0; i < 16; i++) {
                        const uint8_t *p = rgba + ((by + i / 4) * w + bx + i % 4) * 4;
                        px[i][0] = p[0]; px[i][1] = p[1]; px[i][2] = p[2];
                        const int l = p[0] * 3 + p[1] * 6 + p[2];
                        if (l < lmin) { lmin = l; lo = i; }
                        if (l > lmax) { lmax = l; hi = i; }
                    }
                    uint16_t c0 = To565(px[hi][0], px[hi][1], px[hi][2]), c1 = To565(px[lo][0], px[lo][1], px[lo][2]);
                    uint32_t idx = 0;
                    if (c0 < c1) { const uint16_t t = c0; c0 = c1; c1 = t; }
                    if (c0 != c1) {
                        int e[4][3];
                        Expand(c0, e[0]); Expand(c1, e[1]);
                        for (int k = 0; k < 3; k++) { e[2][k] = (2 * e[0][k] + e[1][k]) / 3; e[3][k] = (e[0][k] + 2 * e[1][k]) / 3; }
                        for (int i = 0; i < 16; i++) {
                            int best = 0, bd = 1 << 30;
                            for (int k = 0; k < 4; k++) {
                                const int dr = px[i][0] - e[k][0], dg = px[i][1] - e[k][1], db = px[i][2] - e[k][2];
                                const int d = dr * dr + dg * dg + db * db;
                                if (d < bd) { bd = d; best = k; }
                            }
                            idx |= (uint32_t)best << (2 * i);
                        }
                    }
                    out[0] = (uint8_t)c0; out[1] = (uint8_t)(c0 >> 8); out[2] = (uint8_t)c1; out[3] = (uint8_t)(c1 >> 8);
                    out[4] = (uint8_t)idx; out[5] = (uint8_t)(idx >> 8); out[6] = (uint8_t)(idx >> 16); out[7] = (uint8_t)(idx >> 24);
                    out += 8;
                }
            return out;
    }
    namespace {
        void Half(uint8_t *rgba, int w, int h) {   // in place: w x h -> w/2 x h/2 (2 x 2 average)
            for (int y = 0; y < h / 2; y++)
                for (int x = 0; x < w / 2; x++)
                    for (int k = 0; k < 4; k++) {
                        const int s = rgba[((2 * y) * w + 2 * x) * 4 + k] + rgba[((2 * y) * w + 2 * x + 1) * 4 + k]
                                    + rgba[((2 * y + 1) * w + 2 * x) * 4 + k] + rgba[((2 * y + 1) * w + 2 * x + 1) * 4 + k];
                        rgba[(y * (w / 2) + x) * 4 + k] = (uint8_t)((s + 2) / 4);
                    }
        }
        void Face(float *v, int &n, const float *nrm, const float *r, const float *up, int face) {
            // 4 corners TL, BL, BR, TR (counter-clockwise seen from outside: r x up = normal)
            const float cu[4] = { -0.5f, -0.5f, 0.5f, 0.5f }, cv[4] = { 0.5f, -0.5f, -0.5f, 0.5f };
            const float tu[4] = { 0, 0, 1, 1 }, tv[4] = { 0, 1, 1, 0 };
            for (int c = 0; c < 4; c++) {
                float *o = v + n * 8;
                for (int k = 0; k < 3; k++) o[k] = nrm[k] * 0.5f + r[k] * cu[c] + up[k] * cv[c];
                o[3] = nrm[0]; o[4] = nrm[1]; o[5] = nrm[2];
                o[6] = tu[c] + 2.0f * face;            // the face's number travels in u (box_fsh: floor(u / 2))
                o[7] = tv[c];
                n++;
            }
        }
    }

    bool Shelf::Init(Gpu &gpu, uint32_t firstDescriptor) {
        // the box: faces 0 front (+z), 1 back, 2 spine (-x, the left side seen from the front), 3 right side, 4 top, 5 bottom
        m_mesh = gpu.MakeBuffer(0x8000);
        if (!m_mesh) return false;
        float *v = (float *)dkMemBlockGetCpuAddr(m_mesh);
        int n = 0;
        const float F[6][3][3] = {
            { { 0, 0, 1 },  { 1, 0, 0 },  { 0, 1, 0 } },
            { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { -1, 0, 0 }, { 0, 0, 1 },  { 0, 1, 0 } },
            { { 1, 0, 0 },  { 0, 0, -1 }, { 0, 1, 0 } },
            { { 0, 1, 0 },  { 1, 0, 0 },  { 0, 0, -1 } },
            { { 0, -1, 0 }, { 1, 0, 0 },  { 0, 0, 1 } },
        };
        for (int f = 0; f < 6; f++) Face(v, n, F[f][0], F[f][1], F[f][2], f);
        uint32_t *ix = (uint32_t *)((uint8_t *)v + kIdxOff);
        for (uint32_t f = 0; f < 6; f++) {
            const uint32_t b = f * 4;
            ix[f * 6 + 0] = b; ix[f * 6 + 1] = b + 1; ix[f * 6 + 2] = b + 2;
            ix[f * 6 + 3] = b; ix[f * 6 + 4] = b + 2; ix[f * 6 + 5] = b + 3;
        }
        // v32 - the disc: 8 rings of kDiscSeg vertices (label outer / inner, data outer / inner, outer edge top / bottom,
        // inner edge top / bottom), counter-clockwise seen from outside (checked by hand: see the Face() rule)
        {
            static_assert(kDiscVtxOff + kDiscVtxBytes <= kDiscIdxOff && kDiscIdxOff + kDiscIdxCount * 4 <= 0x8000, "disc layout");
            constexpr int S = kDiscSeg;
            constexpr float RO = 0.5f, RI = 0.075f, T = 0.005f;
            float *dv = (float *)((uint8_t *)dkMemBlockGetCpuAddr(m_mesh) + kDiscVtxOff);
            auto put = [&](int ring, int k, float r, float y, float nx, float ny, float nz, int face) {
                const float a = 2.0f * 3.14159265f * k / S, x = cosf(a) * r, z = sinf(a) * r;
                float *o = dv + (ring * S + k) * 8;
                o[0] = x; o[1] = y; o[2] = z; o[3] = nx; o[4] = ny; o[5] = nz;
                o[6] = (face == 6 ? 0.5f : x + 0.5f) + 2.0f * face; o[7] = face == 6 ? 0.5f : z + 0.5f;
            };
            for (int k = 0; k < S; k++) {
                const float a = 2.0f * 3.14159265f * k / S, c = cosf(a), s = sinf(a);
                put(0, k, RO, T, 0, 1, 0, 4);  put(1, k, RI, T, 0, 1, 0, 4);
                put(2, k, RO, -T, 0, -1, 0, 5); put(3, k, RI, -T, 0, -1, 0, 5);
                put(4, k, RO, T, c, 0, s, 6);   put(5, k, RO, -T, c, 0, s, 6);
                put(6, k, RI, T, -c, 0, -s, 6); put(7, k, RI, -T, -c, 0, -s, 6);
            }
            uint32_t *di = (uint32_t *)((uint8_t *)dkMemBlockGetCpuAddr(m_mesh) + kDiscIdxOff);
            int n = 0;
            auto V = [&](int ring, int k) { return (uint32_t)(ring * S + (k % S)); };
            auto tri = [&](uint32_t a, uint32_t b, uint32_t c) { di[n++] = a; di[n++] = b; di[n++] = c; };
            for (int k = 0; k < S; k++) {
                tri(V(0, k), V(1, k), V(0, k + 1)); tri(V(0, k + 1), V(1, k), V(1, k + 1));   // label (+y)
                tri(V(2, k), V(2, k + 1), V(3, k)); tri(V(2, k + 1), V(3, k + 1), V(3, k));   // data side (-y)
                tri(V(4, k), V(4, k + 1), V(5, k)); tri(V(4, k + 1), V(5, k + 1), V(5, k));   // outer edge
                tri(V(6, k), V(7, k), V(6, k + 1)); tri(V(6, k + 1), V(7, k), V(7, k + 1));   // inner edge (facing the hole)
            }
        }
        // the cover places: each big enough for the largest layout
        uint32_t bytes = 0, align = 1;
        for (const auto &l : kLayouts) {
            uint32_t a = 1;
            const uint32_t b = gpu.ImageBytes(l[0], l[1], DkImageFormat_RGB_BC1_sRGB, kLevels, &a);
            if (b > bytes) bytes = b;
            if (a > align) align = a;
        }
        bytes = (bytes + align - 1) & ~(align - 1);
        m_firstDesc = firstDescriptor;
        m_nslot = 0;
        while (m_nslot < kMaxSlots && m_firstDesc + (uint32_t)m_nslot < 64) {
            const uint32_t off = gpu.ReserveImage(bytes, align);
            if (off == UINT32_MAX) break;
            m_slot[m_nslot].off = off;
            m_slot[m_nslot].key = -1;
            m_nslot++;
        }
        return true;
    }

    void Shelf::Forget() { for (int i = 0; i < m_nslot; i++) m_slot[i].key = -1; m_gen++; m_nmiss = m_missAt = 0; }   // (v90: + the worker's load, the missing ones)
    void Shelf::Remap(const int *newOf, int n) {
        m_gen++; m_nmiss = m_missAt = 0;                 // v90: a load on the worker is for the old order: dropped (asked again)
        for (int i = 0; i < m_nslot; i++) { const int k = m_slot[i].key; if (k >= 0) m_slot[i].key = k < n ? newOf[k] : -1; }
    }

    bool Shelf::Has(int key) const {
        for (int i = 0; i < m_nslot; i++) if (m_slot[i].key == key) return true;
        return false;
    }

    const Target *Shelf::Cover(int key, float *aspect) {
        for (int i = 0; i < m_nslot; i++)
            if (m_slot[i].key == key) {
                m_slot[i].used = m_frame;
                if (aspect) *aspect = m_slot[i].aspect;
                return &m_slot[i].t;
            }
        return nullptr;
    }

    // a place no frame still in flight can be reading (drawn more than kFrames + 1 frames ago): free ones first,
    // then the least recently drawn
    int Shelf::FreeSlot() const {
        int best = -1;
        for (int i = 0; i < m_nslot; i++) {
            const Slot &s = m_slot[i];
            if (s.used + kFrames + 2 > m_frame) continue;
            if (s.key < 0) return i;
            if (best < 0 || s.used < m_slot[best].used) best = i;
        }
        return best;
    }

    int Shelf::LoadRGBA(Gpu &gpu, int key, const uint8_t *rgba, int w, int h, float aspect) {
        if (!rgba || w < 4 || h < 4 || (w & 3) || (h & 3) || (uint32_t)(w / 4) * (h / 4) * 8 > sizeof s_buf) return 0;
        if (!s_lwIdle()) return -1;                     // (v90: s_buf belongs to the worker's load until it is uploaded)
        const int si = FreeSlot();
        if (si < 0) return -1;
        Slot &s = m_slot[si];
        const uint32_t bytes = (uint32_t)(EncodeBC1(rgba, w, h, s_buf) - s_buf);
        if (!gpu.PlaceTexture(s.t, s.off, w, h, DkImageFormat_RGB_BC1_sRGB, 1, m_firstDesc + (uint32_t)si)) return 0;
        if (!gpu.StreamTexture(s.t, s_buf, bytes, 1)) { s.key = -1; return -1; }
        s.key = key; s.aspect = aspect; s.used = m_frame;
        return 1;
    }

    namespace {
        // v42 - a .nro's own icon (a native port with no cover yet): 'ASET' after the NRO, {offset, size} of the JPEG
        int NroIcon(const char *path, uint8_t *out, int cap) {
            FILE *f = fopen(path, "rb");
            if (!f) return 0;
            uint8_t hdr[0x80], aset[0x38];
            int got = 0;
            uint32_t size = 0;
            if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr + 0x10, "NRO0", 4) == 0) {
                memcpy(&size, hdr + 0x18, 4);
                if (size >= 0x80 && fseek(f, (long)size, SEEK_SET) == 0 && fread(aset, 1, sizeof aset, f) == sizeof aset && memcmp(aset, "ASET", 4) == 0) {
                    uint64_t io = 0, is = 0;
                    memcpy(&io, aset + 0x08, 8); memcpy(&is, aset + 0x10, 8);
                    if (is > 64 && is <= (uint64_t)cap && io < 0x10000000ull && fseek(f, (long)(size + io), SEEK_SET) == 0 &&
                        fread(out, 1, (size_t)is, f) == (size_t)is) got = (int)is;
                }
            }
            fclose(f);
            return got;
        }
    }

    // ---- v90 (perf): the load's heavy part on a worker ----
    namespace {
        // one load at a time (s_buf / s_jpg / s_rgba are its own until it is uploaded); in: what to load, out: the result
        struct LoadJob {
            const Shelf *owner; unsigned gen; int key;
            char path[400], nro[400]; int titleIndex; int jpgBytes;       // in (jpgBytes > 0: the loader's icon is in s_jpg)
            int r; int w, h; uint32_t levels, bytes; float aspect;         // out: r 1 loaded, 0 none, 2 needs the loader's icon
        };
        LoadJob s_lj;

        void LoadWork(void *arg) {
            LoadJob &j = *(LoadJob *)arg;
            j.r = 0;
            FILE *f = j.path[0] ? fopen(j.path, "rb") : nullptr;
            if (j.path[0] && !f && !j.nro[0] && j.titleIndex < 0) return;   // (v77: a Switch game: its icon)
            if (f) {
                // 'BTEX', u32 w, h, levels, aspect x 1000, then the BC1 levels (tools/art-builder/art_builder.py)
                uint32_t hd[5] = {};
                const bool okHd = fread(hd, 1, sizeof hd, f) == sizeof hd && memcmp(hd, "BTEX", 4) == 0;
                const int w = (int)hd[1], h = (int)hd[2];
                const uint32_t levels = hd[3];
                uint32_t bytes = 0;
                bool ok = okHd && levels >= 1 && levels <= kLevels;
                bool known = false;
                for (const auto &l : kLayouts) known = known || (l[0] == w && l[1] == h);
                ok = ok && known;
                for (uint32_t l = 0; ok && l < levels; l++) bytes += (uint32_t)((w >> l) / 4) * (uint32_t)((h >> l) / 4) * 8;
                ok = ok && bytes > 0 && bytes <= sizeof s_buf && fread(s_buf, 1, bytes, f) == bytes;
                fclose(f);
                if (!ok) return;
                j.w = w; j.h = h; j.levels = levels; j.bytes = bytes;
                j.aspect = hd[4] >= 250 && hd[4] <= 4000 ? hd[4] / 1000.0f : (float)w / h;
                j.r = 1;
                return;
            }
            int got = 0;
            if (j.jpgBytes > 0) got = j.jpgBytes;            // the loader's icon (read by the main thread: its buffer is the loader's)
            else if (j.nro[0]) got = NroIcon(j.nro, s_jpg, sizeof s_jpg);
            else if (j.titleIndex >= 0) { j.r = 2; return; }   // -> the main thread asks the loader, then a second job decodes it
            else return;
            if (got <= 0 || got > (int)sizeof s_jpg || !DecodeJpegSquare(s_jpg, got, s_rgba, 256)) return;
            uint8_t *o = s_buf;
            int lw = 256;
            for (uint32_t l = 0; l < kLevels; l++) {
                if (l) { Half(s_rgba, lw, lw); lw /= 2; }
                o = Shelf::EncodeBC1(s_rgba, lw, lw, o);
            }
            j.w = j.h = 256; j.levels = kLevels; j.bytes = (uint32_t)(o - s_buf); j.aspect = 1;
            j.r = 1;
        }
    }

    bool Shelf::Missing(int key) const {
        for (int i = 0; i < m_nmiss; i++) if (m_miss[i] == key) return true;
        return false;
    }
    void Shelf::AddMissing(int key) {
        if (Missing(key)) return;
        m_miss[m_missAt] = key;
        m_missAt = (m_missAt + 1) % 16;
        if (m_nmiss < 16) m_nmiss++;
    }

    int Shelf::Collect(Gpu &gpu, int key) {
        if (s_lw.Done()) s_ljReady = true;
        if (!s_ljReady) return s_lw.Busy() && s_lj.owner == this && s_lj.key == key ? -1 : -2;
        LoadJob &j = s_lj;
        if (j.owner != this || j.gen != m_gen) { s_ljReady = false; return -2; }   // (an old list's: dropped)
        const bool mine = j.key == key;
        if (j.r == 2) {                                  // a Switch game without its box: the loader's icon, asked here
            s_ljReady = false;
            const int size = qext_title_icon_size(j.titleIndex);
            const int got = size > 0 && size <= (int)sizeof s_jpg ? qext_title_icon(j.titleIndex, s_jpg, sizeof s_jpg) : 0;
            if (got <= 0 || got > (int)sizeof s_jpg) { AddMissing(j.key); return mine ? 0 : -2; }
            j.path[0] = 0; j.nro[0] = 0; j.jpgBytes = got;
            s_lw.Start(LoadWork, &j, 0x10000);
            if (!s_lw.Done()) return mine ? -1 : -2;     // (on its thread: a later frame)
            s_ljReady = true;                            // (no thread: decoded already)
        }
        if (j.r != 1) { s_ljReady = false; AddMissing(j.key); return mine ? 0 : -2; }
        const int si = FreeSlot();
        if (si < 0) return mine ? -1 : -2;               // (kept: no place free yet)
        Slot &s = m_slot[si];
        if (!gpu.PlaceTexture(s.t, s.off, j.w, j.h, DkImageFormat_RGB_BC1_sRGB, j.levels, m_firstDesc + (uint32_t)si)) {
            s_ljReady = false; AddMissing(j.key); return mine ? 0 : -2;
        }
        if (!gpu.StreamTexture(s.t, s_buf, j.bytes, j.levels)) { s.key = -1; return mine ? -1 : -2; }   // (kept: the next frame)
        s.key = j.key; s.aspect = j.aspect; s.used = m_frame;
        s_ljReady = false;
        return mine ? 1 : -2;
    }

    int Shelf::Load(Gpu &gpu, int key, const char *btexPath, int titleIndex, const char *nroIcon) {
        int r = Collect(gpu, key);                       // 1. what the worker finished (for this key, or another one)
        if (r != -2) return r;
        if (Missing(key)) return 0;
        if (Has(key)) return 1;                          // (the result of an earlier ask, just uploaded)
        if (!s_lw.Idle() || s_ljReady || FreeSlot() < 0) return -1;
        LoadJob &j = s_lj;                               // 2. a new load, on the worker
        j.owner = this; j.gen = m_gen; j.key = key; j.titleIndex = titleIndex; j.jpgBytes = 0;
        if (snprintf(j.path, sizeof j.path, "%s", btexPath ? btexPath : "") >= (int)sizeof j.path) j.path[0] = 0;
        if (snprintf(j.nro, sizeof j.nro, "%s", nroIcon ? nroIcon : "") >= (int)sizeof j.nro) j.nro[0] = 0;
        if (!s_lw.Start(LoadWork, &j, 0x10000)) return -1;
        r = Collect(gpu, key);                           // (no thread: it is done already - the old behaviour, same frame)
        return r == -2 ? -1 : r;
    }

    int Shelf::LoadBC1(Gpu &gpu, int key, const uint8_t *bc, int w, int h, float aspect) {
        if (!bc || w < 4 || h < 4 || (w & 3) || (h & 3) || (uint32_t)(w / 4) * (h / 4) * 8 > Gpu::kStreamMax) return 0;
        const int si = FreeSlot();
        if (si < 0) return -1;
        Slot &s = m_slot[si];
        if (!gpu.PlaceTexture(s.t, s.off, w, h, DkImageFormat_RGB_BC1_sRGB, 1, m_firstDesc + (uint32_t)si)) return 0;
        if (!gpu.StreamTexture(s.t, bc, (uint32_t)(w / 4) * (h / 4) * 8, 1)) { s.key = -1; return -1; }
        s.key = key; s.aspect = aspect; s.used = m_frame;
        return 1;
    }

    void Shelf::Shutdown() { s_lw.Wait(); s_ljReady = false; }
    void Shelf::Reap() { if (s_lw.Done()) s_ljReady = true; }   // (the result stays for the next Load)

} // namespace brume
