// Brume - loading the wheel's baked assets (see Assets.hpp). Formats written by tools/bake-assets.html:
//  BMD2: 'BMD2', u32 ntex, u32 nparts; ntex x { u32 w, h, levels; BC1 sRGB mip levels, each (w/4)*(h/4)*8, halving };
//        nparts x { u32 vtxCount, idxCount; i32 tex; 18 floats (rgba, metal, rough, env, glass, tint3, emi3,
//        doubleSided, clearcoat, iridescence, 0); vtxCount x 8 floats (pos, normal, uv); idxCount x u32 }
//  BMD3: same, each part header has a 4th u32 = idx1Count, and idx1Count x u32 (LOD 1: lighter, same vertices)
//        follow the LOD 0 indices. LOD 1 is drawn for the consoles at the back (small on screen).
//  BENV: 'BENV', u32 w, h, levels; floats RGBA w*h*levels
//  BUIA: 'BUIA', u32 n; n x { name[24], group[12], f32 x, y, w, h }; 2 x { u32 W, H; n x u32 rect[4]; RGBA W*H }
#include <cmath>
#include "Assets.hpp"
#include "shaders.h"       // generated: v74 the settings icons' glass program (only k_ui_vsh and k_verre_fsh are used here)
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace brume {

    namespace {
        const char *const kModels[8] = { "switch", "ps2", "gamecube", "n64", "ps1", "psp", "gba", "ports" };
        constexpr uint32_t kMaxTexSide = 2048, kMaxVtx = 1u << 20, kMaxIdx = 3u << 20;
        constexpr uint64_t kBlockVtx = 6 * 36, kBlockIdx = 6 * 25 * 6;   // v93: the block of glass (6 faces of 6 x 6 points)

        struct File {
            FILE *fp = nullptr; long size = 0;
            explicit File(const char *path) { fp = fopen(path, "rb"); if (fp) { fseek(fp, 0, SEEK_END); size = ftell(fp); fseek(fp, 0, SEEK_SET); } }
            ~File() { if (fp) fclose(fp); }
            bool Read(void *d, size_t n) { return fp && fread(d, 1, n, fp) == n; }
            bool Skip(long n) { return fp && n >= 0 && ftell(fp) + n <= size && !fseek(fp, n, SEEK_CUR); }
            long Left() const { return fp ? size - ftell(fp) : 0; }
        };
    }

    const Sprite *Assets::Find(const char *name) const {
        for (int i = 0; i < nsprites; i++) if (!strncmp(sprites[i].name, name, sizeof sprites[i].name)) return &sprites[i];
        return nullptr;
    }

    bool Assets::Load(Gpu &gpu, char *err, int errCap) {
        err[0] = 0;
        // v74 - the settings icons' liquid glass, its own program (made here at boot, before any frame: shaders/verre_fsh.glsl)
        if (!gpu.MakeProgram(glassProg, k_ui_vsh, sizeof(k_ui_vsh), k_verre_fsh, sizeof(k_verre_fsh))) glassProg.ok = false;
        envOk = LoadEnv(gpu);
        uiOk = LoadUi(gpu);
        LoadIcons(gpu);                                  // optional: without it, the settings page has no 3D carousel
        const bool ok = LoadModels(gpu, err, errCap);
        if (!envOk && !err[0]) snprintf(err, errCap, "lumiere env.bin illisible");
        if (!uiOk && !err[0]) snprintf(err, errCap, "interface ui.bin illisible");
        return ok && envOk && uiOk;
    }

    // 'BICN', u32 n (<= 10); n x { u32 vtx, idx; vtx x 8 floats; idx x u32 }. Every index is checked.
    bool Assets::LoadIcons(Gpu &gpu) {
        File f("sdmc:/brume/modeles/icones.bin");
        uint32_t h[2];
        if (!f.Read(h, 8) || memcmp(h, "BICN", 4) || h[1] == 0 || h[1] > 10 || f.size > (8 << 20)) return false;
        m_iconGeo = gpu.MakeBuffer((uint32_t)f.size + 0x10000);
        if (!m_iconGeo) return false;
        uint8_t *cpu = (uint8_t *)dkMemBlockGetCpuAddr(m_iconGeo);
        const DkGpuAddr gpuBase = dkMemBlockGetGpuAddr(m_iconGeo);
        uint64_t used = 0;
        for (uint32_t i = 0; i < h[1]; i++) {
            uint32_t c[2];
            if (!f.Read(c, 8) || !c[0] || !c[1] || c[0] > 200000 || c[1] > 600000 || c[1] % 3) { nicons = 0; return false; }
            const uint64_t vb = (uint64_t)c[0] * 32, ib = (uint64_t)c[1] * 4, vbA = (vb + 255) / 256 * 256, ibA = (ib + 255) / 256 * 256;
            if (used + vbA + ibA > (uint64_t)f.size + 0x10000 || !f.Read(cpu + used, vb) || !f.Read(cpu + used + vbA, ib)) { nicons = 0; return false; }
            const uint32_t *idx = (const uint32_t *)(cpu + used + vbA);
            for (uint32_t k = 0; k < c[1]; k++) if (idx[k] >= c[0]) { nicons = 0; return false; }
            IconMesh &m = icon[nicons++];
            m.vtx = gpuBase + used; m.vtxBytes = (uint32_t)vb; m.idx = gpuBase + used + vbA; m.idxCount = c[1];
            used += vbA + ibA;
        }
        return true;
    }

    bool Assets::LoadEnv(Gpu &gpu) {
        File f("sdmc:/brume/modeles/env.bin");
        uint32_t h[4];
        if (!f.Read(h, 16) || memcmp(h, "BENV", 4) || h[1] == 0 || h[1] > 256 || h[2] == 0 || h[3] == 0 || h[3] > 8) return false;
        const uint32_t bytes = h[1] * h[2] * h[3] * 16;
        if ((long)bytes != f.Left()) return false;
        void *px = malloc(bytes);
        if (!px) return false;
        // half floats: RGBA32F filtered linearly came out pink on the console; RGBA16F is always filterable
        bool ok = f.Read(px, bytes);
        if (ok) {   // 9 spherical harmonics of the last level (roughness 1), with the same mapping as EnvAt (mesh_fsh.glsl)
            const float *e = (const float *)px; const uint32_t W = h[1], H = h[2], lvl = h[3] - 1;
            const float PI = 3.14159265f;
            for (uint32_t r = 0; r < H; r++) {
                const float lat = ((r + 0.5f) / H - 0.5f) * PI, cl = cosf(lat), y = sinf(lat);
                const float dw = (2 * PI / W) * (PI / H) * cl;
                for (uint32_t c = 0; c < W; c++) {
                    const float phi = ((c + 0.5f) / W - 0.5f) * 2 * PI, x = cl * cosf(phi), z = cl * sinf(phi);
                    const float Y[9] = { 0.282095f, 0.488603f * y, 0.488603f * z, 0.488603f * x, 1.092548f * x * y, 1.092548f * y * z,
                                         0.315392f * (3 * z * z - 1), 1.092548f * x * z, 0.546274f * (x * x - y * y) };
                    const float *p = e + ((size_t)(lvl * H + r) * W + c) * 4;
                    for (int k = 0; k < 9; k++) for (int ch = 0; ch < 3; ch++) envSH[k][ch] += p[ch] * Y[k] * dw;
                }
            }
        }
        if (ok) {
            const float *src = (const float *)px; __fp16 *dst = (__fp16 *)px;   // in place: dst trails src
            const uint32_t n = bytes / 4;
            for (uint32_t i = 0; i < n; i++) { float v = src[i]; dst[i] = (__fp16)(v > 60000.0f ? 60000.0f : v < 0.0f ? 0.0f : v); }
            ok = gpu.MakeTextureRaw(env, (int)h[1], (int)(h[2] * h[3]), DkImageFormat_RGBA16_Float, px, bytes / 2, 5);
        }
        free(px);
        return ok;
    }

    bool Assets::LoadUi(Gpu &gpu) {
        File f("sdmc:/brume/ui.bin");
        uint32_t h[2];
        if (!f.Read(h, 8) || memcmp(h, "BUI", 3) || h[1] == 0 || h[1] > 512) return false;
        nsprites = (int)h[1];
        sprites = (Sprite *)calloc((size_t)nsprites, sizeof(Sprite));
        if (!sprites) return false;
        for (int i = 0; i < nsprites; i++) {
            Sprite &s = sprites[i];
            if (!f.Read(s.name, 24) || !f.Read(s.group, 12) || !f.Read(&s.x, 16)) return false;
            s.name[23] = s.group[11] = 0;
        }
        for (int a = 0; a < 2; a++) {           // only the 720p sheet is kept (memory); the 1080p one is skipped
            uint32_t wh[2];
            if (!f.Read(wh, 8) || wh[0] == 0 || wh[0] > 4096 || wh[1] == 0 || wh[1] > 4096) return false;
            for (int i = 0; i < nsprites; i++) {
                uint32_t r[4];
                if (!f.Read(r, 16)) return false;
                Sprite &s = sprites[i];
                s.uv[a][0] = (float)r[0] / wh[0]; s.uv[a][1] = (float)r[1] / wh[1];
                s.uv[a][2] = (float)(r[0] + r[2]) / wh[0]; s.uv[a][3] = (float)(r[1] + r[3]) / wh[1];
            }
            const uint32_t bytes = wh[0] * wh[1] * 4;
            if (f.Left() < (long)bytes) return false;
            if (a == 1) { for (int i = 0; i < nsprites; i++) memcpy(sprites[i].uv[1], sprites[i].uv[0], sizeof sprites[i].uv[0]); break; }
            void *px = malloc(bytes);
            if (!px) return false;
            const bool ok = f.Read(px, bytes) && gpu.MakeTexture(ui[a], (int)wh[0], (int)wh[1], DkImageFormat_RGBA8_Unorm, 4, px, 3 + a);
            // v73 - does the sheet hold the icons' thickness (the blue of the « ico_* » sprites: tools/ui_sdf.py)? An older
            // ui.bin has none: the settings keep the v28 glass (ui_fsh 22) instead of drawing nothing
            uiSdf = false;
            if (ok)
                for (int i = 0; i < nsprites && !uiSdf; i++) {
                    if (strncmp(sprites[i].name, "ico_", 4)) continue;
                    const uint32_t x0 = (uint32_t)(sprites[i].uv[0][0] * wh[0] + 0.5f), y0 = (uint32_t)(sprites[i].uv[0][1] * wh[1] + 0.5f);
                    const uint32_t x1 = (uint32_t)(sprites[i].uv[0][2] * wh[0] + 0.5f), y1 = (uint32_t)(sprites[i].uv[0][3] * wh[1] + 0.5f);
                    for (uint32_t y = y0; y < y1 && y < wh[1] && !uiSdf; y++)
                        for (uint32_t x = x0; x < x1 && x < wh[0]; x++)
                            if (((const uint8_t *)px)[((size_t)y * wh[0] + x) * 4 + 2] > 128) { uiSdf = true; break; }   // (inside: b > .5)
                    break;                                       // the first icon decides
                }
            free(px);
            if (!ok) return false;
        }
        return true;
    }

    // a console texture: w, h (multiples of 4), mip levels (each half the previous, never under 4 px)
    static bool TexOk(const uint32_t *wh) {
        if (!wh[0] || !wh[1] || wh[0] > kMaxTexSide || wh[1] > kMaxTexSide || (wh[0] & 3) || (wh[1] & 3) || !wh[2] || wh[2] > 12) return false;
        return (wh[0] >> (wh[2] - 1)) >= 4 && (wh[1] >> (wh[2] - 1)) >= 4 && (wh[2] == 1 || (!(wh[0] & (wh[0] - 1)) && !(wh[1] & (wh[1] - 1))));
    }
    static uint32_t TexBytes(const uint32_t *wh) {
        uint32_t n = 0;
        for (uint32_t l = 0; l < wh[2]; l++) n += ((wh[0] >> l) / 4) * ((wh[1] >> l) / 4) * 8;
        return n;
    }

    bool Assets::LoadModels(Gpu &gpu, char *err, int errCap) {
        char path[80];
        // pass 1: check every file and add up the geometry, so it lives in one GPU buffer
        uint64_t geoTotal = 0;
        for (int m = 0; m < 8; m++) {
            snprintf(path, sizeof path, "sdmc:/brume/modeles/%s.bmdl", kModels[m]);
            File f(path);
            uint32_t h[3];
            if (!f.Read(h, 12) || (memcmp(h, "BMD2", 4) && memcmp(h, "BMD3", 4)) || h[1] > 16 || h[2] > 24) { snprintf(err, errCap, "console illisible : %s", kModels[m]); continue; }
            const bool v3 = !memcmp(h, "BMD3", 4);
            bool ok = true;
            for (uint32_t t = 0; t < h[1] && ok; t++) {
                uint32_t wh[3];
                ok = f.Read(wh, 12) && TexOk(wh) && f.Skip((long)TexBytes(wh));
            }
            for (uint32_t p = 0; p < h[2] && ok; p++) {
                uint32_t c[4] = { 0, 0, 0, 0 };
                ok = f.Read(c, v3 ? 16 : 12) && c[0] && c[1] && c[0] <= kMaxVtx && c[1] <= kMaxIdx && c[1] % 3 == 0 &&
                     c[3] <= kMaxIdx && c[3] % 3 == 0 && f.Skip(18 * 4) && f.Skip((long)c[0] * 32 + (long)c[1] * 4 + (long)c[3] * 4);
                if (ok) geoTotal += ((uint64_t)c[0] * 32 + 255) / 256 * 256 + ((uint64_t)c[1] * 4 + 255) / 256 * 256 +
                                    ((uint64_t)c[3] * 4 + 255) / 256 * 256;
            }
            if (!ok) snprintf(err, errCap, "console abimee : %s", kModels[m]);
        }
        if (!geoTotal || geoTotal > (48u << 20)) return false;
        geoTotal += kBlockVtx * 32 + kBlockIdx * 4 + 512;      // v93: the block of glass (made below, a few KB)
        m_geo = gpu.MakeBuffer((uint32_t)geoTotal + 0x10000);   // + 64 KB: the GPU prefetches past the last buffer
        if (!m_geo) { snprintf(err, errCap, "pas de memoire pour les consoles"); return false; }   // lang:log (never drawn)
        uint8_t *cpu = (uint8_t *)dkMemBlockGetCpuAddr(m_geo);
        const DkGpuAddr gpuBase = dkMemBlockGetGpuAddr(m_geo);
        uint64_t used = 0;
        // pass 2: fill
        for (int m = 0; m < 8; m++) {
            snprintf(path, sizeof path, "sdmc:/brume/modeles/%s.bmdl", kModels[m]);
            File f(path);
            uint32_t h[3];
            if (!f.Read(h, 12) || (memcmp(h, "BMD2", 4) && memcmp(h, "BMD3", 4)) || h[1] > 16 || h[2] > 24) continue;
            const bool v3 = !memcmp(h, "BMD3", 4);
            const int texBase = ntex;
            bool ok = true;
            for (uint32_t t = 0; t < h[1] && ok; t++) {
                uint32_t wh[3];
                ok = f.Read(wh, 12) && TexOk(wh);
                if (!ok) break;
                const uint32_t bytes = TexBytes(wh);
                void *px = malloc(bytes);
                ok = px && f.Read(px, bytes) && ntex < 56 &&
                     gpu.MakeTextureRaw(tex[ntex], (int)wh[0], (int)wh[1], DkImageFormat_RGB_BC1_sRGB, px, bytes, 6 + (uint32_t)ntex, wh[2]);
                free(px);
                if (ok) ntex++;
            }
            Model &md = model[m];
            for (uint32_t p = 0; p < h[2] && ok; p++) {
                uint32_t c[4] = { 0, 0, 0, 0 }; float fl[18];
                ok = f.Read(c, v3 ? 16 : 12) && c[0] && c[1] && c[0] <= kMaxVtx && c[1] <= kMaxIdx && c[1] % 3 == 0 &&
                     c[3] <= kMaxIdx && c[3] % 3 == 0 && f.Read(fl, sizeof fl);
                const uint64_t vb = (uint64_t)c[0] * 32, ib = (uint64_t)c[1] * 4, ib1 = (uint64_t)c[3] * 4;
                const uint64_t vbA = (vb + 255) / 256 * 256, ibA = (ib + 255) / 256 * 256, ib1A = (ib1 + 255) / 256 * 256;
                ok = ok && used + vbA + ibA + ib1A <= geoTotal && f.Read(cpu + used, vb) && f.Read(cpu + used + vbA, ib) &&
                     (!ib1 || f.Read(cpu + used + vbA + ibA, ib1));
                if (!ok) break;
                const uint32_t *idx = (const uint32_t *)(cpu + used + vbA);       // every index must point at a vertex
                for (uint32_t i = 0; i < c[1] && ok; i++) ok = idx[i] < c[0];
                const uint32_t *idx1 = (const uint32_t *)(cpu + used + vbA + ibA);
                for (uint32_t i = 0; i < c[3] && ok; i++) ok = idx1[i] < c[0];
                if (!ok) break;
                MeshPart &q = md.parts[md.count++];
                q.vtx = gpuBase + used; q.vtxBytes = (uint32_t)vb; q.idx = gpuBase + used + vbA; q.idxCount = c[1];
                if (c[3]) { q.idx1 = gpuBase + used + vbA + ibA; q.idx1Count = c[3]; }   // LOD 1
                else      { q.idx1 = q.idx; q.idx1Count = c[1]; }                       // BMD2: one level only
                const int32_t t = (int32_t)c[2];
                q.tex = (t >= 0 && t < (int32_t)h[1]) ? texBase + t : -1;
                memcpy(q.base, fl, 16); q.metal = fl[4]; q.rough = fl[5]; q.env = fl[6]; q.glass = fl[7];
                memcpy(q.tint, fl + 8, 12); memcpy(q.emi, fl + 11, 12); q.dbl = fl[14];
                const int et = (int)fl[16];
                q.emiTex = (et >= 0 && et < (int)h[1]) ? texBase + et : -1;
                if (m == 0 && p == 0) {   // v40 - the Switch: its Joy-Con apart (they can slide off their rails) + their colour cells
                    const float *vv = (const float *)(cpu + used);
                    uint32_t *ix = (uint32_t *)(cpu + used + vbA);
                    const uint32_t nt = c[1] / 3;
                    auto side = [&](uint32_t t) {
                        const float cx = (vv[ix[t * 3] * 8] + vv[ix[t * 3 + 1] * 8] + vv[ix[t * 3 + 2] * 8]) / 3.0f;
                        return cx < -0.715f ? 1 : (cx > 0.715f ? 2 : 0);
                    };
                    auto swapTri = [&](uint32_t a2, uint32_t b2) { for (int k = 0; k < 3; k++) { const uint32_t t = ix[a2 * 3 + k]; ix[a2 * 3 + k] = ix[b2 * 3 + k]; ix[b2 * 3 + k] = t; } };
                    uint32_t lo = 0, mid = 0, hi = nt;    // [0, lo) the console, [lo, mid) left, [hi, nt) right
                    while (mid < hi) {
                        const int s = side(mid);
                        if (s == 0) { swapTri(mid, lo); lo++; mid++; } else if (s == 1) mid++; else { hi--; swapTri(mid, hi); }
                    }
                    q.splitL = lo * 3; q.splitR = hi * 3;
                    static float area[2][128];
                    memset(area, 0, sizeof area);
                    for (uint32_t t = lo; t < nt; t++) {   // each Joy-Con's colour cell covering the most surface: its body
                        const int s = t < hi ? 0 : 1;
                        const float *A = vv + ix[t * 3] * 8, *Bv = vv + ix[t * 3 + 1] * 8, *C = vv + ix[t * 3 + 2] * 8;
                        const float e1[3] = { Bv[0] - A[0], Bv[1] - A[1], Bv[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
                        const float cr[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                        const float ar = sqrtf(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
                        const int cell = (int)((A[6] + Bv[6] + C[6]) / 3.0f * 128.0f);
                        if (cell >= 0 && cell < 128 && (A[7] + Bv[7] + C[7]) / 3.0f < 0.25f) area[s][cell] += ar;
                    }
                    for (int s = 0; s < 2; s++) {
                        int best = -1; float ba = 0;
                        for (int k = 0; k < 128; k++) if (area[s][k] > ba) { ba = area[s][k]; best = k; }
                        joyCell[s] = best >= 0 ? best / 128.0f : -1.0f;   // v41: the vertices' own u (the cell's edge, not its middle)
                    }
                }
                used += vbA + ibA + ib1A;
            }
            if (!ok) { md.count = 0; snprintf(err, errCap, "console abimee : %s", kModels[m]); }
        }
        // v93 - the block of glass of the consoles without a model: a rounded box (a subdivided box whose points are pushed onto
        // the rounded shape: flat faces, round edges and corners), its normals outward, triangles counter-clockwise seen from
        // outside. Drawn like the « Ports natifs » glass cards: the fog seen through it, the room's reflections on it.
        used = (used + 255) / 256 * 256;
        if (used + kBlockVtx * 32 + kBlockIdx * 4 + 256 <= geoTotal) {
            float *v = (float *)(cpu + used);
            const uint64_t ioff = (used + kBlockVtx * 32 + 255) / 256 * 256;
            uint32_t *ix = (uint32_t *)(cpu + ioff);
            const float H[3] = { 0.90f, 0.62f, 0.30f }, R = 0.13f;
            static const int kAx[6][3] = { { 0, 1, 2 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 1, 0 }, { 2, 0, 1 }, { 0, 2, 1 } };   // t1, t2, normal axis
            static const float kSg[6] = { 1, -1, 1, -1, 1, -1 };
            uint32_t nv = 0, ni = 0;
            for (int f = 0; f < 6; f++) {
                const int a1 = kAx[f][0], a2 = kAx[f][1], an = kAx[f][2];
                auto G = [&](int ax, int k) {             // the grid along an axis: the edge rounding, the flat middle, the other edge
                    static const float t[6] = { -1.0f, -0.5f, 0.0f, 0.0f, 0.5f, 1.0f };
                    return k < 3 ? -H[ax] + R * (1 + t[k]) : H[ax] - R * (1 - t[k]);
                };
                const uint32_t base = nv;
                for (int j = 0; j < 6; j++) for (int i = 0; i < 6; i++) {
                    float q[3];
                    q[a1] = G(a1, i); q[a2] = G(a2, j); q[an] = kSg[f] * H[an];
                    float c[3], d[3], l = 0;
                    for (int k = 0; k < 3; k++) { const float lim = H[k] - R; c[k] = q[k] < -lim ? -lim : (q[k] > lim ? lim : q[k]); d[k] = q[k] - c[k]; l += d[k] * d[k]; }
                    l = sqrtf(l > 1e-12f ? l : 1e-12f);
                    float *o = v + nv * 8;
                    for (int k = 0; k < 3; k++) { o[3 + k] = d[k] / l; o[k] = c[k] + R * o[3 + k]; }
                    o[6] = 0; o[7] = 0;
                    nv++;
                }
                for (int j = 0; j < 5; j++) for (int i = 0; i < 5; i++) {
                    const uint32_t p0 = base + j * 6 + i, p1 = p0 + 1, p2 = p0 + 7, p3 = p0 + 6;
                    ix[ni++] = p0; ix[ni++] = p1; ix[ni++] = p2; ix[ni++] = p0; ix[ni++] = p2; ix[ni++] = p3;   // (t1 x t2 = the outward normal: CCW)
                }
            }
            MeshPart &q = block.parts[0];
            q.vtx = gpuBase + used; q.vtxBytes = nv * 32; q.idx = q.idx1 = gpuBase + ioff; q.idxCount = q.idx1Count = ni;
            q.tex = q.emiTex = -1;
            q.base[0] = 0.62f; q.base[1] = 0.68f; q.base[2] = 0.86f; q.base[3] = 1.0f;
            q.metal = 0; q.rough = 0.10f; q.env = 1.0f; q.glass = 1.0f;
            q.tint[0] = 0.40f; q.tint[1] = 0.45f; q.tint[2] = 0.62f;   // (darker than the fog behind: the consoles stay a little dark)
            q.emi[0] = q.emi[1] = q.emi[2] = 0; q.dbl = 0;
            block.count = 1;
            used = ioff + ni * 4;
        }
        return true;
    }

} // namespace brume
