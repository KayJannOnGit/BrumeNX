// Brume - v75: the covers, downloaded by the console itself. See Covers.hpp.
// The way the PC tool (tools/art-builder/art_builder.py) does it, written again for the console: same sources, same names,
// same texture ('BTEX', u32 w, h, levels, aspect x 1000, then BC1 levels; 256 x 512 / 512 x 256 / 256 x 256, 3 levels).
#include "Covers.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include "Nextendo.hpp"
#include "Social.hpp"
#include "Settings.hpp"
#include "Shelf.hpp"
#include "Emus.hpp"
#include "Consoles.hpp"
#include "Spotify.hpp"
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <new>
#include <strings.h>
#include <sys/stat.h>

extern "C" {
    int qext_game_running(void);
    int qext_title_count(void);
    u64 qext_title_id(int index);
    int qext_title_original_name(int index, char *out, unsigned cap);   // v93: the game's own name (a rename does not count)
}
namespace brume { bool DecodeJpegTo(const void *data, int bytes, uint8_t *out, int ow, int oh); }

namespace brume::covers {

    void (*s_log)(const char *) = nullptr;
    void (*s_toast)(float, const char *) = nullptr;

    namespace {
        struct Sys { const char *folder, *repo; };
        // sdmc:/roms/<folder> -> its libretro-thumbnails repository (github.com/libretro-thumbnails)
        // v93: every console of the table that has a libretro-thumbnails repository
        struct SysList { Sys s[consoles::kMax]; int n; };
        constexpr SysList MakeSys() {
            SysList l{};
            for (int i = 0; i < consoles::kCount; i++)
                if (consoles::Emulated(i) && consoles::kDefs[i].thumbs) l.s[l.n++] = Sys{ consoles::kDefs[i].key, consoles::kDefs[i].thumbs };
            return l;
        }
        constexpr SysList kSysList = MakeSys();
        const char *const kExt[] = { ".iso", ".chd", ".cso", ".bin", ".cue", ".pbp", ".gba", ".z64", ".n64", ".v64", ".rvz", ".gcm", ".ciso", ".gcz",
                                     ".zip", ".m3u", ".nds", ".sfc", ".smc", ".fig", ".swc", ".nes", ".fds", ".unf", ".unif", ".md", ".gen",
                                     ".smd", ".gb", ".gbc", ".sgb", ".sms", ".gg" };   // (v90: as Games.cpp lists them; v93: Consoles.hpp)
        constexpr const char *kHost = "raw.githubusercontent.com";
        constexpr int kPauseSteps = 20;              // 20 x 100 ms = 2 s between two requests (sites have blocked Yass before)
        constexpr int kMaxSide = 4096;               // a cover bigger than that is not a cover
        constexpr int kMissMax = 32 * 1024;          // introuvables.txt read back (bounded)

        int   s_state = 0;                           // 0 idle, 1 working, 2 done (atomics)
        int   s_quit = 0;
        Thread s_thread;
        bool  s_threadOn = false;
        bool  s_manual = false, s_wantManual = false, s_bootDone = false;
        float s_nextTry = 25.0f;                     // the first background run: after Nextendo and the clock had their turn
        int   s_total = 0, s_done = 0, s_got = 0;    // the worker writes them, the menu reads them (atomics)
        int   s_shownDone = -1;
        int   s_gen = 0;
        bool  s_netFail = false, s_noVerify = false, s_noVerifyTdb = false;
        char  s_why[64];
        // v77 - the task centre and the notification (main thread), what the worker is on (double buffer + atomic), and when
        // it last heard back (system ticks, atomic)
        int   s_task = -1, s_note = -1, s_shownTotal = -1;
        char  s_item[2][120];
        int   s_itemSeq = 0;
        u64   s_alive = 0, s_aliveSeen = 0;
        // v77 - the Switch games (GameTDB, by name: its own ids are not the title ids), snapshot by the menu before a run
        constexpr int kSwMax = 256;
        struct Sw { u64 tid; char name[96]; };
        Sw  *s_sw = nullptr;
        int  s_swN = 0;

        int  Ld(const int &x) { return __atomic_load_n(&x, __ATOMIC_ACQUIRE); }
        void Item(const char *name) {                    // (worker) what it is on now: the task centre shows it
            const int k = (Ld(s_itemSeq) + 1) & 1;
            snprintf(s_item[k], sizeof s_item[k], "%s", name);
            __atomic_store_n(&s_itemSeq, Ld(s_itemSeq) + 1, __ATOMIC_RELEASE);
        }
        void Alive() { __atomic_store_n(&s_alive, armGetSystemTick(), __ATOMIC_RELEASE); }
        void St(int &x, int v) { __atomic_store_n(&x, v, __ATOMIC_RELEASE); }
        bool Stop() { return Ld(s_quit) || qext_game_running(); }
        void Say(float now, const char *msg) { notify::Post(now, msg); }   // v77: a notification (Notify.hpp)
        bool Online() {
            NifmInternetConnectionType t; u32 st = 0; NifmInternetConnectionStatus cs;
            return R_SUCCEEDED(nifmGetInternetConnectionStatus(&t, &st, &cs)) && cs == NifmInternetConnectionStatus_Connected;
        }
        bool Exists(const char *p) { struct stat s; return stat(p, &s) == 0; }

        // ---- the names (art_builder.py: clean, libretro_name, candidates) ----
        void Clean(const char *in, char *out, int cap) {    // « [PS2](2001) SSX Tricky » -> « SSX Tricky »
            const char *p = in;
            while (*p == '[' || *p == '(') {
                const char *c = strchr(p, *p == '[' ? ']' : ')');
                if (!c) break;
                p = c + 1;
                while (*p == ' ') p++;
            }
            snprintf(out, (size_t)cap, "%s", p);
            for (int n = (int)strlen(out); n > 0 && out[n - 1] == ' '; n--) out[n - 1] = 0;
        }
        void Libretro(char *s) { for (; *s; s++) if (strchr("&*/:`<>?\\|\"", *s)) *s = '_'; }   // what libretro replaces in file names
        void UrlEncode(const char *in, char *out, int cap) {
            static const char kHex[] = "0123456789ABCDEF";
            int n = 0;
            for (const unsigned char *p = (const unsigned char *)in; *p && n < cap - 4; p++) {
                if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || strchr("-_.~", *p)) out[n++] = (char)*p;
                else { out[n++] = '%'; out[n++] = kHex[*p >> 4]; out[n++] = kHex[*p & 15]; }
            }
            out[n] = 0;
        }

        // ---- a PNG, decoded as it arrives (zlib streaming), scaled straight to the texture's size (box filter) ----
        struct Png {
            int st = 0, sigN = 0, hdrN = 0; u8 hdr[8] = {}; u32 left = 0, type = 0;
            u8 ihdr[13] = {}; int ihdrN = 0;
            int W = 0, H = 0, depth = 0, ctype = 0, chans = 0, bpp = 1, rowBytes = 0;
            u8 pal[256 * 3] = {}; int palN = 0, palPos = 0;
            z_stream z; bool zOn = false;
            u8 *row = nullptr, *prev = nullptr; int rowPos = 0, y = 0;
            u8 *rgb = nullptr;                            // one source row, 8-bit RGB
            int w = 0, h = 0; u8 *out = nullptr;          // the texture, RGBA
            u32 *hrow = nullptr, *acc = nullptr; int accN = 0, dy = 0;
            int *xs = nullptr, *xe = nullptr;
            bool bad = false, done = false, started = false;
            u8 zbuf[8192];
        };
        void* ZAlloc(void *, unsigned items, unsigned size) { return calloc(items, size); }
        void  ZFree(void *, void *p) { free(p); }
        void PngFree(Png &p) {
            if (p.zOn) { inflateEnd(&p.z); p.zOn = false; }
            free(p.row); free(p.prev); free(p.rgb); free(p.out); free(p.hrow); free(p.acc); free(p.xs); free(p.xe);
            p.row = p.prev = p.rgb = p.out = nullptr; p.hrow = p.acc = nullptr; p.xs = p.xe = nullptr;
        }
        bool PngBegin(Png &p) {                          // IHDR read: check it, choose the texture, allocate
            const u8 *d = p.ihdr;
            p.W = (int)((u32)d[0] << 24 | (u32)d[1] << 16 | (u32)d[2] << 8 | d[3]);
            p.H = (int)((u32)d[4] << 24 | (u32)d[5] << 16 | (u32)d[6] << 8 | d[7]);
            p.depth = d[8]; p.ctype = d[9];
            if (p.W < 16 || p.H < 16 || p.W > kMaxSide || p.H > kMaxSide || d[10] != 0 || d[11] != 0 || d[12] != 0) return false;   // (interlaced: no)
            switch (p.ctype) {
                case 0: p.chans = 1; break; case 2: p.chans = 3; break; case 3: p.chans = 1; break;
                case 4: p.chans = 2; break; case 6: p.chans = 4; break; default: return false;
            }
            const bool okDepth = p.depth == 8 || (p.depth == 16 && p.ctype != 3) || ((p.depth == 1 || p.depth == 2 || p.depth == 4) && (p.ctype == 0 || p.ctype == 3));
            if (!okDepth) return false;
            p.rowBytes = (p.W * p.chans * p.depth + 7) / 8;
            p.bpp = (p.chans * p.depth + 7) / 8; if (p.bpp < 1) p.bpp = 1;
            const float aspect = (float)p.W / (float)p.H;
            p.w = aspect > 1.2f ? 512 : 256; p.h = aspect > 1.2f ? 256 : (aspect < 0.83f ? 512 : 256);
            p.row = (u8 *)calloc(1, (size_t)p.rowBytes + 1); p.prev = (u8 *)calloc(1, (size_t)p.rowBytes + 1);
            p.rgb = (u8 *)malloc((size_t)p.W * 3); p.out = (u8 *)malloc((size_t)p.w * p.h * 4);
            p.hrow = (u32 *)malloc((size_t)p.w * 3 * sizeof(u32)); p.acc = (u32 *)calloc((size_t)p.w * 3, sizeof(u32));
            p.xs = (int *)malloc((size_t)p.w * sizeof(int)); p.xe = (int *)malloc((size_t)p.w * sizeof(int));
            if (!p.row || !p.prev || !p.rgb || !p.out || !p.hrow || !p.acc || !p.xs || !p.xe) return false;
            for (int x = 0; x < p.w; x++) {
                p.xs[x] = (int)((long long)x * p.W / p.w);
                p.xe[x] = (int)((long long)(x + 1) * p.W / p.w); if (p.xe[x] <= p.xs[x]) p.xe[x] = p.xs[x] + 1;
            }
            memset(&p.z, 0, sizeof p.z);
            p.z.zalloc = ZAlloc; p.z.zfree = ZFree;
            if (inflateInit(&p.z) != Z_OK) return false;
            p.zOn = true; p.started = true;
            return true;
        }
        void Unfilter(Png &p, u8 *r, const u8 *q, int f) {
            const int n = p.rowBytes, b = p.bpp;
            switch (f) {
                case 0: break;
                case 1: for (int i = b; i < n; i++) r[i] = (u8)(r[i] + r[i - b]); break;
                case 2: for (int i = 0; i < n; i++) r[i] = (u8)(r[i] + q[i]); break;
                case 3: for (int i = 0; i < n; i++) r[i] = (u8)(r[i] + (((i >= b ? r[i - b] : 0) + q[i]) >> 1)); break;
                case 4:
                    for (int i = 0; i < n; i++) {
                        const int a = i >= b ? r[i - b] : 0, c = i >= b ? q[i - b] : 0, bb = q[i];
                        const int pp = a + bb - c, pa = abs(pp - a), pb = abs(pp - bb), pc = abs(pp - c);
                        r[i] = (u8)(r[i] + (pa <= pb && pa <= pc ? a : (pb <= pc ? bb : c)));
                    }
                    break;
                default: p.bad = true;
            }
        }
        void ToRgb(Png &p, const u8 *r) {                // one row -> 8-bit RGB (alpha: over black)
            for (int x = 0; x < p.W; x++) {
                int R, G, B, A = 255;
                if (p.depth < 8) {
                    const int bit = x * p.depth, v = (r[bit >> 3] >> (8 - p.depth - (bit & 7))) & ((1 << p.depth) - 1);
                    if (p.ctype == 3) { const int k = v < p.palN ? v : 0; R = p.pal[k * 3]; G = p.pal[k * 3 + 1]; B = p.pal[k * 3 + 2]; }
                    else R = G = B = v * 255 / ((1 << p.depth) - 1);
                } else {
                    const int s = p.depth / 8;                // bytes per sample (16 bits: the high byte)
                    const u8 *px = r + (size_t)x * p.chans * s;
                    switch (p.ctype) {
                        case 0: R = G = B = px[0]; break;
                        case 2: R = px[0]; G = px[s]; B = px[2 * s]; break;
                        case 3: { const int k = px[0] < p.palN ? px[0] : 0; R = p.pal[k * 3]; G = p.pal[k * 3 + 1]; B = p.pal[k * 3 + 2]; break; }
                        case 4: R = G = B = px[0]; A = px[s]; break;
                        default: R = px[0]; G = px[s]; B = px[2 * s]; A = px[3 * s]; break;
                    }
                }
                if (A != 255) { R = R * A / 255; G = G * A / 255; B = B * A / 255; }
                p.rgb[x * 3] = (u8)R; p.rgb[x * 3 + 1] = (u8)G; p.rgb[x * 3 + 2] = (u8)B;
            }
        }
        void EmitRow(Png &p, int dyRow) {                // the vertical sum -> one texture row
            const int n = p.accN > 0 ? p.accN : 1;
            u8 *o = p.out + (size_t)dyRow * p.w * 4;
            for (int x = 0; x < p.w; x++) {
                o[x * 4] = (u8)(p.acc[x * 3] / n); o[x * 4 + 1] = (u8)(p.acc[x * 3 + 1] / n); o[x * 4 + 2] = (u8)(p.acc[x * 3 + 2] / n); o[x * 4 + 3] = 255;
            }
            memset(p.acc, 0, (size_t)p.w * 3 * sizeof(u32)); p.accN = 0;
        }
        void SourceRow(Png &p) {                         // a whole source row is in: unfilter, colour, scale
            Unfilter(p, p.row + 1, p.prev + 1, p.row[0]);
            if (p.bad) return;
            ToRgb(p, p.row + 1);
            for (int x = 0; x < p.w; x++) {              // horizontal: the average of its source span
                u32 r = 0, g = 0, b = 0;
                for (int s = p.xs[x]; s < p.xe[x]; s++) { r += p.rgb[s * 3]; g += p.rgb[s * 3 + 1]; b += p.rgb[s * 3 + 2]; }
                const u32 c = (u32)(p.xe[x] - p.xs[x]);
                p.hrow[x * 3] = r / c; p.hrow[x * 3 + 1] = g / c; p.hrow[x * 3 + 2] = b / c;
            }
            const int y = p.y;                           // vertical: the texture rows whose source span holds this row
            while (p.dy < p.h) {
                const int ys = (int)((long long)p.dy * p.H / p.h);
                int ye = (int)((long long)(p.dy + 1) * p.H / p.h); if (ye <= ys) ye = ys + 1;
                if (y < ys) break;
                for (int k = 0; k < p.w * 3; k++) p.acc[k] += p.hrow[k];
                p.accN++;
                if (y >= ye - 1) { EmitRow(p, p.dy); p.dy++; continue; }   // (enlarging: the next texture row may use it too)
                break;
            }
            u8 *t = p.prev; p.prev = p.row; p.row = t;
            p.y++;
        }
        void Rows(Png &p, const u8 *d, int n) {
            for (int i = 0; i < n && !p.bad && p.y < p.H; ) {
                const int need = p.rowBytes + 1 - p.rowPos, k = n - i < need ? n - i : need;
                memcpy(p.row + p.rowPos, d + i, (size_t)k);
                p.rowPos += k; i += k;
                if (p.rowPos == p.rowBytes + 1) { p.rowPos = 0; SourceRow(p); }
            }
        }
        void Idat(Png &p, const u8 *d, int n) {
            if (!p.zOn) { p.bad = true; return; }
            p.z.next_in = (Bytef *)d; p.z.avail_in = (uInt)n;
            int r;
            do {
                p.z.next_out = p.zbuf; p.z.avail_out = sizeof p.zbuf;
                r = inflate(&p.z, Z_NO_FLUSH);
                if (r == Z_NEED_DICT || r == Z_DATA_ERROR || r == Z_MEM_ERROR || r == Z_STREAM_ERROR) { p.bad = true; return; }
                Rows(p, p.zbuf, (int)(sizeof p.zbuf - p.z.avail_out));
            } while (p.z.avail_out == 0 && r != Z_STREAM_END && !p.bad);
        }
        void PngFeed(Png &p, const u8 *d, int n) {      // the bytes of the file, as they come
            static const u8 kSig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
            for (int i = 0; i < n && !p.bad && !p.done; ) {
                if (p.st == 0) {                         // the signature
                    if (d[i] != kSig[p.sigN]) { p.bad = true; return; }
                    i++; if (++p.sigN == 8) p.st = 1;
                } else if (p.st == 1) {                  // a chunk's length and type
                    p.hdr[p.hdrN++] = d[i++];
                    if (p.hdrN == 8) {
                        p.left = (u32)p.hdr[0] << 24 | (u32)p.hdr[1] << 16 | (u32)p.hdr[2] << 8 | p.hdr[3];
                        p.type = (u32)p.hdr[4] << 24 | (u32)p.hdr[5] << 16 | (u32)p.hdr[6] << 8 | p.hdr[7];
                        p.hdrN = 0; p.st = 2;
                        if (p.left > 0x4000000) { p.bad = true; return; }
                        if (p.type == 0x49454E44) { p.done = true; return; }   // IEND
                        if (p.type == 0x49444154 && !p.started) { p.bad = true; return; }   // IDAT before IHDR
                        if (p.left == 0) p.st = 3, p.left = 4;
                    }
                } else if (p.st == 2) {                  // its data
                    const int k = (int)((u32)(n - i) < p.left ? (u32)(n - i) : p.left);
                    if (p.type == 0x49484452) {          // IHDR
                        for (int j = 0; j < k && p.ihdrN < 13; j++) p.ihdr[p.ihdrN++] = d[i + j];
                        if (p.ihdrN == 13 && !p.started && !PngBegin(p)) { p.bad = true; return; }
                    } else if (p.type == 0x504C5445) {   // PLTE (byte by byte: it can be cut between two network reads)
                        for (int j = 0; j < k; j++) {
                            if (p.palPos < 768) p.pal[p.palPos] = d[i + j];
                            p.palPos++;
                            if (p.palPos % 3 == 0 && p.palPos <= 768) p.palN = p.palPos / 3;
                        }
                    } else if (p.type == 0x49444154) {   // IDAT
                        Idat(p, d + i, k);
                    }
                    i += k; p.left -= (u32)k;
                    if (p.left == 0) { p.st = 3; p.left = 4; }
                } else {                                  // its CRC (not checked: TLS already protects the bytes)
                    const int k = (int)((u32)(n - i) < p.left ? (u32)(n - i) : p.left);
                    i += k; p.left -= (u32)k;
                    if (p.left == 0) p.st = 1;
                }
            }
        }

        // ---- the texture ----
        void Half(u8 *rgba, int w, int h) {              // in place: w x h -> w/2 x h/2
            const int nw = w / 2, nh = h / 2;
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++)
                    for (int c = 0; c < 4; c++) {
                        const u8 *a = rgba + ((size_t)(2 * y) * w + 2 * x) * 4 + c, *b = a + (size_t)w * 4;
                        rgba[((size_t)y * nw + x) * 4 + c] = (u8)((a[0] + a[4] + b[0] + b[4] + 2) / 4);
                    }
        }
        bool WriteBtexRGBA(u8 *rgba, int w, int h, int aspect, const char *dst) {   // (rgba is halved in place for the mips)
            const uint32_t bytes = (uint32_t)(w * h / 2 + w * h / 8 + w * h / 32);
            u8 *bc = (u8 *)malloc(bytes);
            if (!bc) return false;
            u8 *o = bc;
            int lw = w, lh = h;
            for (int l = 0; l < 3; l++) {
                if (l) { Half(rgba, lw, lh); lw /= 2; lh /= 2; }
                o = Shelf::EncodeBC1(rgba, lw, lh, o);
            }
            if (aspect < 250) aspect = 250;
            if (aspect > 4000) aspect = 4000;
            uint32_t hd[5];
            memcpy(&hd[0], "BTEX", 4);
            hd[1] = (uint32_t)w; hd[2] = (uint32_t)h; hd[3] = 3; hd[4] = (uint32_t)aspect;
            char tmp[440];
            if (snprintf(tmp, sizeof tmp, "%.420s.tmp", dst) >= (int)sizeof tmp) { free(bc); return false; }
            FILE *f = fopen(tmp, "wb");
            bool ok = f && fwrite(hd, 1, sizeof hd, f) == sizeof hd && fwrite(bc, 1, (size_t)(o - bc), f) == (size_t)(o - bc);
            if (f) ok = fclose(f) == 0 && ok;
            free(bc);
            if (ok) { remove(dst); ok = rename(tmp, dst) == 0; }
            if (!ok) remove(tmp);
            return ok;
        }
        bool WriteBtex(Png &p, const char *dst) { return WriteBtexRGBA(p.out, p.w, p.h, (int)((long long)p.W * 1000 / p.H), dst); }

        // ---- one request: the HTTP answer (status, chunked or not), its body into the PNG decoder ----
        struct Job {
            char head[4096]; int headN = 0; bool inBody = false; int status = 0; bool chunked = false;
            int cst = 0; long chunkLeft = 0; char csz[24]; int cszN = 0;
            long total = 0; bool stop = false;
            Png png;
        };
        void Body(Job &j, const u8 *d, int n) {
            if (!j.chunked) { PngFeed(j.png, d, n); return; }
            for (int i = 0; i < n && !j.png.bad && !j.png.done; ) {
                if (j.cst == 0) {                        // a chunk's size line
                    const char c = (char)d[i++];
                    if (c == '\n') {
                        j.csz[j.cszN] = 0; j.cszN = 0;
                        j.chunkLeft = strtol(j.csz, nullptr, 16);
                        if (j.chunkLeft <= 0) { j.png.done = j.png.done || j.png.y > 0; return; }
                        j.cst = 1;
                    } else if (j.cszN < (int)sizeof j.csz - 1) j.csz[j.cszN++] = c;
                } else if (j.cst == 1) {                 // its data
                    const int k = (long)(n - i) < j.chunkLeft ? n - i : (int)j.chunkLeft;
                    PngFeed(j.png, d + i, k);
                    i += k; j.chunkLeft -= k;
                    if (j.chunkLeft == 0) { j.cst = 2; j.chunkLeft = 2; }
                } else {                                  // the CRLF after it
                    i++; if (--j.chunkLeft == 0) j.cst = 0;
                }
            }
        }
        int Sink(void *u, const char *p, int n) {
            Job &j = *(Job *)u;
            if (Stop()) { j.stop = true; return -1; }
            Alive();
            j.total += n;
            if (j.total > (16L << 20)) { j.png.bad = true; return -1; }
            if (j.inBody) { Body(j, (const u8 *)p, n); return j.png.bad || j.png.done ? -1 : 0; }
            const int k = n < (int)sizeof j.head - 1 - j.headN ? n : (int)sizeof j.head - 1 - j.headN;
            memcpy(j.head + j.headN, p, (size_t)k);
            j.headN += k; j.head[j.headN] = 0;
            char *e = strstr(j.head, "\r\n\r\n");
            if (!e) { if (j.headN >= (int)sizeof j.head - 1) { j.png.bad = true; return -1; } return 0; }
            if (!strncmp(j.head, "HTTP/1.", 7) && j.headN > 12) j.status = atoi(j.head + 9);
            if (j.status != 200) return -1;
            for (char *q = j.head; q < e; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q - 'A' + 'a');
            *e = 0;
            j.chunked = strstr(j.head, "transfer-encoding: chunked") != nullptr;
            j.inBody = true;
            const int used = (int)(e + 4 - j.head) - (j.headN - k);   // (the header's bytes taken from this read)
            if (used < n) Body(j, (const u8 *)p + used, n - used);
            return j.png.bad || j.png.done ? -1 : 0;
        }
        enum Result_ { kGot, kMissing, kNet, kStopped, kSkip };   // v88: kSkip = not now (an image that could not be decoded: maybe memory), never written as missing
        Result_ Fetch(const Sys &s, const char *name, const char *dst) {
            char enc[600], path[800];
            UrlEncode(name, enc, sizeof enc);
            snprintf(path, sizeof path, "/libretro-thumbnails/%s/master/Named_Boxarts/%s.png", s.repo, enc);
            Job *j = (Job *)malloc(sizeof(Job));
            if (!j) return kNet;
            Result_ res = kNet;
            for (int attempt = 0; attempt < 2; attempt++) {
                new (j) Job();
                const int r = nextendo::Https(kHost, path, !s_noVerify, "image/png", Sink, j, s_why, (int)sizeof s_why);
                if (r < 0 && !s_noVerify && !strncmp(s_why, "TLS", 3)) {   // the console's store refuses the certificate: the
                    s_noVerify = true;                                    // public image without the check (nothing is sent)
                    PngFree(j->png);
                    continue;
                }
                if (j->stop || Stop()) res = kStopped;
                else if (r < 0) res = kNet;
                else if (j->status == 404) res = kMissing;
                else if (j->status != 200) { res = kNet; snprintf(s_why, sizeof s_why, "HTTP %d", j->status); }
                else if (j->png.bad || !j->png.started) res = kMissing;   // not a PNG (or a broken one): noted as missing
                else if (j->png.y < j->png.H) { res = kNet; snprintf(s_why, sizeof s_why, "image coupee"); }   // (cut on the way: later)
                else {
                    while (j->png.dy < j->png.h) {          // (the last rows: rounding)
                        if (j->png.dy > 0 && j->png.accN == 0)
                            memcpy(j->png.out + (size_t)j->png.dy * j->png.w * 4, j->png.out + (size_t)(j->png.dy - 1) * j->png.w * 4, (size_t)j->png.w * 4);
                        else EmitRow(j->png, j->png.dy);
                        j->png.dy++;
                    }
                    res = WriteBtex(j->png, dst) ? kGot : kNet;
                    if (res == kNet) snprintf(s_why, sizeof s_why, "ecriture");
                }
                PngFree(j->png);
                break;
            }
            j->~Job();
            free(j);
            return res;
        }


        // ---- v77: GameTDB, the Switch games' boxes (Yass: « avec GameDB oublie pas, download en direct ») ----
        // GameTDB knows Switch games by its own five-letter ids (from the product code), not by title id: the link is the name,
        // matched strictly (letters and digits only, accents dropped) against its title lists (French, then English), kept on
        // the card 14 days. Then its medium front cover (coverM, ~352 x 570 JPEG): French box first, then the English, the
        // American, the Japanese one. Same rules as the rest: one request at a time with the pause, misses noted.
        constexpr const char *kTdbHost = "www.gametdb.com", *kArtHost = "art.gametdb.com";
        constexpr const char *kTdbIdx[2] = { "sdmc:/brume/jaquettes/switch/gametdb-fr.txt", "sdmc:/brume/jaquettes/switch/gametdb-en.txt" };
        constexpr const char *kTdbLang[2] = { "FR", "EN" };
        constexpr const char *kTdbRegions[4] = { "FR", "EN", "US", "JA" };
        constexpr int kJpgMax = 512 * 1024;
        constexpr const char *kSwMiss = "introuvables-v93.txt";
        bool s_tdbReady = false;

        // an HTTP answer's body (chunked or not) into a file or a memory buffer
        struct Blob {
            char head[4096]; int headN = 0; bool inBody = false; int status = 0; bool chunked = false;
            int cst = 0; long chunkLeft = 0; char csz[24]; int cszN = 0;
            FILE *f = nullptr; u8 *mem = nullptr; int memN = 0, memCap = 0; bool over = false; long total = 0;
        };
        void BlobPut(Blob &b, const u8 *d, int n) {
            if (n <= 0) return;
            if (b.f) { if (fwrite(d, 1, (size_t)n, b.f) != (size_t)n) b.over = true; return; }
            if (!b.mem || b.memN + n > b.memCap) { b.over = true; return; }
            memcpy(b.mem + b.memN, d, (size_t)n); b.memN += n;
        }
        void BlobBody(Blob &b, const u8 *d, int n) {
            if (!b.chunked) { BlobPut(b, d, n); return; }
            for (int i = 0; i < n && !b.over; ) {
                if (b.cst == 0) {
                    const char c = (char)d[i++];
                    if (c == '\n') {
                        b.csz[b.cszN] = 0; b.cszN = 0;
                        b.chunkLeft = strtol(b.csz, nullptr, 16);
                        if (b.chunkLeft <= 0) { b.cst = 3; return; }
                        b.cst = 1;
                    } else if (b.cszN < (int)sizeof b.csz - 1) b.csz[b.cszN++] = c;
                } else if (b.cst == 1) {
                    const int k = (long)(n - i) < b.chunkLeft ? n - i : (int)b.chunkLeft;
                    BlobPut(b, d + i, k);
                    i += k; b.chunkLeft -= k;
                    if (b.chunkLeft == 0) { b.cst = 2; b.chunkLeft = 2; }
                } else if (b.cst == 2) { i++; if (--b.chunkLeft == 0) b.cst = 0; }
                else return;
            }
        }
        int BlobSink(void *u, const char *p, int n) {
            Blob &b = *(Blob *)u;
            if (Stop()) return -1;
            Alive();
            b.total += n;
            if (b.total > (8L << 20)) { b.over = true; return -1; }
            if (b.inBody) { BlobBody(b, (const u8 *)p, n); return b.over ? -1 : 0; }
            const int k = n < (int)sizeof b.head - 1 - b.headN ? n : (int)sizeof b.head - 1 - b.headN;
            memcpy(b.head + b.headN, p, (size_t)k);
            b.headN += k; b.head[b.headN] = 0;
            char *e = strstr(b.head, "\r\n\r\n");
            if (!e) { if (b.headN >= (int)sizeof b.head - 1) { b.over = true; return -1; } return 0; }
            if (!strncmp(b.head, "HTTP/1.", 7) && b.headN > 12) b.status = atoi(b.head + 9);
            if (b.status != 200) return -1;
            for (char *q = b.head; q < e; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q - 'A' + 'a');
            *e = 0;
            b.chunked = strstr(b.head, "transfer-encoding: chunked") != nullptr;
            b.inBody = true;
            const int used = (int)(e + 4 - b.head) - (b.headN - k);
            if (used < n) BlobBody(b, (const u8 *)p + used, n - used);
            return b.over ? -1 : 0;
        }
        // one request (the TLS check dropped once if the console's store refuses the host: public data, nothing is sent)
        int Get(const char *host, const char *path, const char *accept, Blob &b) {
            for (int attempt = 0; attempt < 2; attempt++) {
                const int r = nextendo::Https(host, path, !s_noVerifyTdb, accept, BlobSink, &b, s_why, (int)sizeof s_why);
                if (r < 0 && !s_noVerifyTdb && !strncmp(s_why, "TLS", 3) && b.total == 0) { s_noVerifyTdb = true; continue; }
                return r < 0 && b.status == 0 ? -1 : b.status;
            }
            return -1;
        }
        void Pause(int &asked) {                         // one request at a time, with a pause
            if (asked++) for (int k = 0; k < kPauseSteps && !Stop(); k++) svcSleepThread(100000000LL);
        }
        // a title, compared the GameTDB way: letters and digits only, lower case, accents dropped (« Pokémon » = « pokemon »)
        void Norm(const char *in, char *out, int cap) {
            static const char kLatin[33] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";   // U+00C0..U+00DF and U+00E0..U+00FF
            int n = 0;
            for (const unsigned char *p = (const unsigned char *)in; *p && n < cap - 1; ) {
                unsigned c = *p++;
                if (c == 0xC3 && (*p & 0xC0) == 0x80) {
                    const unsigned d = (unsigned)(*p++) | 0x40;
                    const char m = kLatin[(d >= 0xE0 ? d - 0xE0 : d - 0xC0) & 31];
                    if (m != '/') out[n++] = m;
                    continue;
                }
                if (c >= 0x80) { while ((*p & 0xC0) == 0x80) p++; continue; }   // other symbols (™, ®...): dropped
                if (c >= 'A' && c <= 'Z') c += 32;
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = (char)c;
            }
            out[n] = 0;
        }
        // the title lists, fresh enough (14 days) or fetched again
        void EnsureIndex(int &asked) {
            mkdir("sdmc:/brume/jaquettes/switch", 0777);
            for (int l = 0; l < 2 && !Stop(); l++) {
                struct stat st;
                const time_t t = time(nullptr);
                if (stat(kTdbIdx[l], &st) == 0 && st.st_size > 1000 && t > st.st_mtime && t - st.st_mtime < 14 * 86400) continue;
                Pause(asked);
                char tmp[96], path[64];
                snprintf(tmp, sizeof tmp, "%s.tmp", kTdbIdx[l]);
                snprintf(path, sizeof path, "/switchtdb.txt?LANG=%s", kTdbLang[l]);
                Item("GameTDB");
                Blob *b = new (std::nothrow) Blob();
                if (!b) return;
                b->f = fopen(tmp, "wb");
                const int st2 = b->f ? Get(kTdbHost, path, "text/plain", *b) : -1;
                bool ok = b->f && st2 == 200 && !b->over;
                if (b->f) ok = fclose(b->f) == 0 && ok;
                delete b;
                if (ok) { remove(kTdbIdx[l]); ok = rename(tmp, kTdbIdx[l]) == 0; }
                if (!ok) remove(tmp);
                if (st2 < 0) { s_netFail = true; return; }
            }
        }
        // v93 - a title's main part, before « : » or « - » (« Minecraft Dungeons: Hero Edition » -> « Minecraft Dungeons »)
        void Head(const char *in, char *out, int cap) {
            int n = 0;
            for (const char *p = in; *p && n < cap - 1; p++) {
                if (*p == ':') break;
                if (*p == ' ' && p[1] == '-' && p[2] == ' ') break;
                if (*p == ' ' && !strncmp(p + 1, "\xe2\x80\x93 ", 4)) break;   // (« – »)
                out[n++] = *p;
            }
            out[n] = 0;
        }
        // the GameTDB ids whose title is this name (at most 3). loose (v93, only when the strict way found nothing): the
        // main parts may match instead (« Minecraft Dungeons » = « Minecraft Dungeons: Hero Edition »), 8 letters at least
        int FindTdb(const char *name, char ids[3][8], bool loose = false) {
            char want[128], wantHead[128], have[128], haveHead[128], line[400];
            Norm(name, want, sizeof want);
            Head(name, line, sizeof line); Norm(line, wantHead, sizeof wantHead);
            if (!want[0]) return 0;
            const bool longWant = strlen(want) >= 8, longHead = strlen(wantHead) >= 8;
            if (loose && !longWant && !longHead) return 0;
            int n = 0;
            for (int l = 0; l < 2 && n < 3; l++) {
                FILE *f = fopen(kTdbIdx[l], "rb");
                if (!f) continue;
                while (n < 3 && fgets(line, sizeof line, f)) {
                    char *eq = strstr(line, " = ");
                    if (!eq || eq - line < 4 || eq - line > 6) continue;     // (also the header)
                    *eq = 0;
                    char *title = eq + 3;
                    for (char *q = title; *q; q++) if (*q == '\r' || *q == '\n') { *q = 0; break; }
                    Norm(title, have, sizeof have);
                    bool hit = !strcmp(have, want);
                    if (loose && !hit) {
                        char h[400];
                        Head(title, h, sizeof h); Norm(h, haveHead, sizeof haveHead);
                        hit = (longWant && !strcmp(haveHead, want)) || (longHead && !strcmp(have, wantHead));
                    }
                    if (!hit) continue;
                    bool dup = false;
                    for (int k = 0; k < n; k++) dup = dup || !strcmp(ids[k], line);
                    if (!dup) snprintf(ids[n++], 8, "%.7s", line);
                }
                fclose(f);
            }
            return n;
        }
        // v93 - the game's English name, from its control data (a French console gives « Pokémon Écarlate », GameTDB's lists
        // say « Pokémon Scarlet »). FW 21+ may keep the names compressed (TitlesDataFormat 1, at 0x3215: raw DEFLATE of 32
        // entries of 0x300 bytes): only the first two are inflated (American, British English). false: none.
        bool EnglishName(u64 tid, char *out, int cap) {
            out[0] = 0;
            constexpr size_t kCtl = sizeof(NsApplicationControlData);   // (0x24000: the NACP + the icon, freed right away)
            u8 *ctl = (u8 *)malloc(kCtl);
            if (!ctl) return false;
            u64 got = 0;
            bool ok = false;
            if (R_SUCCEEDED(nsInitialize())) {
                ok = R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, (NsApplicationControlData *)ctl, kCtl, &got))
                     && got >= sizeof(NacpStruct);
                nsExit();
            }
            u8 ent[0x600];
            if (ok) {
                const u8 fmt = ctl[0x3215];
                if (fmt == 0) memcpy(ent, ctl, sizeof ent);
                else if (fmt == 1) {
                    const unsigned cs = (unsigned)ctl[0] | (unsigned)ctl[1] << 8;
                    z_stream z;
                    memset(&z, 0, sizeof z);
                    z.zalloc = ZAlloc; z.zfree = ZFree;
                    ok = cs > 0 && cs <= 0x2FFE && inflateInit2(&z, -15) == Z_OK;
                    if (ok) {
                        z.next_in = ctl + 2; z.avail_in = cs; z.next_out = ent; z.avail_out = sizeof ent;
                        const int r = inflate(&z, Z_SYNC_FLUSH);
                        ok = (r == Z_OK || r == Z_STREAM_END || r == Z_BUF_ERROR) && z.total_out >= 0x300;
                        if (z.total_out < sizeof ent) memset(ent + z.total_out, 0, sizeof ent - z.total_out);
                        inflateEnd(&z);
                    }
                } else ok = false;
            }
            free(ctl);
            if (!ok) return false;
            const int pick = ent[0] ? 0 : (ent[0x300] ? 1 : -1);
            if (pick < 0) return false;
            int n = 0;
            while (n < 0x1ff && n + 1 < cap && ent[pick * 0x300 + n]) { out[n] = (char)ent[pick * 0x300 + n]; n++; }
            out[n] = 0;
            return n > 0;
        }
        Result_ FetchSwitch(const Sw &g, const char *dst, int &asked) {
            char ids[3][8], en[96];
            int n = FindTdb(g.name, ids);                // its name as the console shows it, then its English name (v93),
            const bool hasEn = !n && EnglishName(g.tid, en, sizeof en) && strcmp(en, g.name);
            if (!n && hasEn) n = FindTdb(en, ids);
            if (!n) n = FindTdb(g.name, ids, true);       // then their main parts (« … : Hero Edition »)
            if (!n && hasEn) n = FindTdb(en, ids, true);
            if (!n) return kMissing;
            u8 *jpg = (u8 *)malloc(kJpgMax);
            u8 *rgba = (u8 *)malloc(256 * 512 * 4);
            Blob *b = new (std::nothrow) Blob();
            Result_ res = kMissing;
            bool undecoded = false;                      // v88 (review): a decode that failed may be a lack of memory: not « introuvable » for good
            if (!jpg || !rgba || !b) res = kSkip;
            for (int i = 0; i < n && res == kMissing; i++)
                for (int r = 0; r < 4 && res == kMissing; r++) {
                    Pause(asked);
                    if (Stop()) { res = kStopped; break; }
                    char path[64];
                    snprintf(path, sizeof path, "/switch/coverM/%s/%s.jpg", kTdbRegions[r], ids[i]);
                    *b = Blob();
                    b->mem = jpg; b->memCap = kJpgMax;
                    const int st = Get(kArtHost, path, "image/jpeg", *b);
                    if (Stop()) { res = kStopped; break; }
                    if (st < 0) { res = kNet; break; }
                    if (st != 200 || b->over || b->memN < 64) continue;   // (404: that region has no box)
                    if (!DecodeJpegTo(jpg, b->memN, rgba, 256, 512)) { undecoded = true; continue; }
                    res = WriteBtexRGBA(rgba, 256, 512, 618, dst) ? kGot : kNet;   // (a Switch box: 352 x 570)
                    if (res == kNet) snprintf(s_why, sizeof s_why, "ecriture");
                }
            delete b; free(rgba); free(jpg);
            if (res == kMissing && undecoded) res = kSkip;
            return res;
        }

        // ---- the run ----
        bool IsRom(const char *n) {
            const char *dot = strrchr(n, '.');
            if (!dot || n[0] == '.') return false;
            for (const char *e : kExt) if (!strcasecmp(dot, e)) return true;
            return false;
        }
        // v93: the Switch games have their own list (introuvables-v93.txt): the names are matched better now, the games
        // noted before are asked once more
        char *LoadMiss(const char *folder, const char *file = "introuvables.txt") {   // one name a line, « \n » first (for the search)
            char path[160];
            snprintf(path, sizeof path, "sdmc:/brume/jaquettes/%s/%s", folder, file);
            char *b = (char *)calloc(1, kMissMax + 2);
            if (!b) return nullptr;
            b[0] = '\n';
            if (FILE *f = fopen(path, "rb")) { const size_t n = fread(b + 1, 1, kMissMax, f); b[1 + n] = 0; fclose(f); }
            return b;
        }
        bool InMiss(const char *miss, const char *base) {
            if (!miss) return false;
            char key[310];
            snprintf(key, sizeof key, "\n%s\n", base);
            return strstr(miss, key) != nullptr;
        }
        void AddMiss(const char *folder, const char *base, const char *file = "introuvables.txt") {
            char path[160];
            snprintf(path, sizeof path, "sdmc:/brume/jaquettes/%s/%s", folder, file);
            if (FILE *f = fopen(path, "ab")) { fprintf(f, "%s\n", base); fclose(f); }
        }
        bool Base(const char *file, char *base, int cap) {
            if (snprintf(base, (size_t)cap, "%s", file) >= cap) return false;
            if (char *dot = strrchr(base, '.')) *dot = 0;
            return base[0] != 0;
        }
        // each rom of each console that has neither a cover nor a « missing » note; count = true: only count them
        int Walk(bool count) {
            int n = 0, asked = 0;
            mkdir("sdmc:/brume/jaquettes", 0777);
            for (int si = 0; si < kSysList.n; si++) {
              const Sys &s = kSysList.s[si];
              char dirs[4][96];                          // v90: every folder people name that way (ps2, PS2, playstation2...; v93: + Tico's)
              const int nd = emus::RomDirs(s.folder, dirs, 4);
              for (int dk = 0; dk < nd; dk++) {
                DIR *d = opendir(dirs[dk]);
                if (!d) continue;
                char jdir[96];
                snprintf(jdir, sizeof jdir, "sdmc:/brume/jaquettes/%s", s.folder);
                mkdir(jdir, 0777);
                char *miss = LoadMiss(s.folder);
                while (struct dirent *e = readdir(d)) {
                    if (Stop() || (!count && s_netFail)) break;
                    if (!IsRom(e->d_name)) continue;
                    char base[300], dst[420];
                    if (!Base(e->d_name, base, sizeof base)) continue;
                    if (snprintf(dst, sizeof dst, "%s/%s.btex", jdir, base) >= (int)sizeof dst) continue;
                    if (Exists(dst) || InMiss(miss, base)) continue;
                    n++;
                    if (count) continue;
                    // its names, the libretro way: the file's own name, then (no region in it) with the usual regions
                    char clean[300], cand[4][320];
                    Clean(base, clean, sizeof clean);
                    int nc = 0;
                    snprintf(cand[nc++], sizeof cand[0], "%s", clean);
                    if (!strchr(clean, '(')) {
                        snprintf(cand[nc++], sizeof cand[0], "%s (USA)", clean);
                        snprintf(cand[nc++], sizeof cand[0], "%s (Europe)", clean);
                        snprintf(cand[nc++], sizeof cand[0], "%s (USA, Europe)", clean);
                    }
                    bool got = false, stopped = false;
                    Item(clean);
                    for (int c = 0; c < nc && !got && !stopped; c++) {
                        Libretro(cand[c]);
                        if (asked++) {                   // one request at a time, with a pause
                            for (int k = 0; k < kPauseSteps && !Stop(); k++) svcSleepThread(100000000LL);
                        }
                        if (Stop()) { stopped = true; break; }
                        const char *dot = strrchr(e->d_name, '.');           // v93: a Game Boy Color game: its own repository
                        const Sys gbc = { s.folder, "Nintendo_-_Game_Boy_Color" };
                        const Result_ r = Fetch(dot && !strcasecmp(dot, ".gbc") ? gbc : s, cand[c], dst);
                        if (r == kGot) got = true;
                        else if (r == kStopped) stopped = true;
                        else if (r == kNet) { s_netFail = true; stopped = true; }
                    }
                    if (got) { St(s_got, Ld(s_got) + 1); St(s_gen, Ld(s_gen) + 1); }
                    else if (!stopped) {                  // asked everywhere: missing (never asked again; delete the file to retry)
                        AddMiss(s.folder, base);
                        if (miss) { const size_t l = strlen(miss); if (l + strlen(base) + 2 < (size_t)kMissMax) { strcat(miss, base); strcat(miss, "\n"); } }
                    }
                    if (stopped) break;
                    St(s_done, Ld(s_done) + 1);
                }
                free(miss);
                closedir(d);
                if (Stop() || (!count && s_netFail)) break;
              }
              if (Stop() || (!count && s_netFail)) break;
            }
            // v77 - the Switch games: GameTDB's box, by name (sdmc:/brume/jaquettes/switch/<title id>.btex)
            if (s_sw && s_swN > 0 && !Stop() && !(!count && s_netFail)) {
                const char *jdir = "sdmc:/brume/jaquettes/switch";
                mkdir(jdir, 0777);
                char *miss = LoadMiss("switch", kSwMiss);
                for (int i = 0; i < s_swN; i++) {
                    if (Stop() || (!count && s_netFail)) break;
                    char key[20], dst[96];
                    snprintf(key, sizeof key, "%016llX", (unsigned long long)s_sw[i].tid);
                    snprintf(dst, sizeof dst, "%s/%s.btex", jdir, key);
                    if (Exists(dst) || InMiss(miss, key)) continue;
                    n++;
                    if (count) continue;
                    if (!s_tdbReady) { EnsureIndex(asked); s_tdbReady = true; if (s_netFail) break; }
                    Item(s_sw[i].name);
                    const Result_ r = FetchSwitch(s_sw[i], dst, asked);
                    if (r == kGot) { St(s_got, Ld(s_got) + 1); St(s_gen, Ld(s_gen) + 1); }
                    else if (r == kMissing) {
                        AddMiss("switch", key, kSwMiss);
                        if (miss) { const size_t l = strlen(miss); if (l + strlen(key) + 2 < (size_t)kMissMax) { strcat(miss, key); strcat(miss, "\n"); } }
                    } else if (r == kNet) { s_netFail = true; break; }
                    else if (r != kSkip) break;                 // (kSkip: asked again next run)
                    St(s_done, Ld(s_done) + 1);
                }
                free(miss);
            }
            return n;
        }

        void Work(void *) {
            SocketInitConfig cfg = *socketGetDefaultInitConfig();   // small buffers (the menu's memory is short)
            cfg.tcp_tx_buf_size = 0x2000; cfg.tcp_rx_buf_size = 0x8000; cfg.tcp_tx_buf_max_size = 0; cfg.tcp_rx_buf_max_size = 0;
            cfg.udp_tx_buf_size = 0x800; cfg.udp_rx_buf_size = 0x800; cfg.sb_efficiency = 1; cfg.num_bsd_sessions = 1;
            St(s_total, Walk(true));
            if (Ld(s_total) > 0 && !Stop()) {
                if (R_SUCCEEDED(socketInitialize(&cfg))) {
                    if (R_SUCCEEDED(sslInitialize(1))) { Walk(false); sslExit(); }
                    else { s_netFail = true; snprintf(s_why, sizeof s_why, "ssl indisponible"); }
                    socketExit();
                } else { s_netFail = true; snprintf(s_why, sizeof s_why, "reseau indisponible"); }
            }
            St(s_state, 2);
        }

        bool Start(bool manual) {
            if (Ld(s_state) != 0 || s_threadOn || qext_game_running() || nextendo::Busy() || social::NetBusy() || spotify::Busy()) return false;   // (v95: the friends too; v98: the Spotify cover)
            if (!s_sw) s_sw = (Sw *)malloc(sizeof(Sw) * kSwMax);   // v77 (~26 KB, kept): the Switch games, read here (the loader's calls)
            s_swN = 0;
            if (s_sw) {
                const int tc = qext_title_count();
                for (int i = 0; i < tc && s_swN < kSwMax; i++) {
                    Sw &w = s_sw[s_swN];
                    w.tid = qext_title_id(i); w.name[0] = 0;
                    if (!w.tid) continue;
                    qext_title_original_name(i, w.name, sizeof w.name);
                    w.name[sizeof w.name - 1] = 0;
                    if (w.name[0]) s_swN++;
                }
            }
            s_tdbReady = false; s_shownTotal = -1; s_aliveSeen = 0;
            __atomic_store_n(&s_itemSeq, 0, __ATOMIC_RELEASE);
            s_manual = manual; s_netFail = false; s_why[0] = 0; s_shownDone = -1;
            St(s_total, 0); St(s_done, 0); St(s_got, 0); St(s_quit, 0);
            St(s_state, 1);
            s_threadOn = R_SUCCEEDED(threadCreate(&s_thread, Work, nullptr, nullptr, 0x10000, 0x2E, -2));
            if (s_threadOn && R_FAILED(threadStart(&s_thread))) { threadClose(&s_thread); s_threadOn = false; }
            if (!s_threadOn) { St(s_state, 0); return false; }
            return true;
        }
    }

    bool Busy() { return Ld(s_state) != 0; }
    int  Generation() { return Ld(s_gen); }

    void Shutdown() {
        St(s_quit, 1);
        if (s_threadOn) { threadWaitForExit(&s_thread); threadClose(&s_thread); s_threadOn = false; }
        St(s_state, 0);
    }

    void DownloadNow(float now) {
        if (qext_game_running()) return;
        if (Busy()) { s_manual = true; Say(now, "Jaquettes : d\xc3\xa9j\xc3\xa0 en cours"); return; }
        if (!Online()) { Say(now, "Pas de r\xc3\xa9seau"); return; }
        Say(now, "Jaquettes : je cherche\xe2\x80\xa6");
        if (!Start(true)) s_wantManual = true;           // (Nextendo is using the network: right after it)
    }

    void Update(float now, bool menuOnScreen) {
        char m[128];
        if (Ld(s_state) == 2) {                          // the run is over
            if (s_threadOn) { threadWaitForExit(&s_thread); threadClose(&s_thread); s_threadOn = false; }
            const int got = Ld(s_got), total = Ld(s_total), done = Ld(s_done);
            if (s_log) {
                snprintf(m, sizeof m, "jaquettes : %d trouvee(s), %d / %d cherchee(s)%s%s", got, done, total, s_why[0] ? " - " : "", s_why);   // lang:log (never drawn)
                s_log(m);
            }
            // v77: the last word takes the place of the download's notification (or is posted if the row asked)
            const char *word = nullptr;
            int icon = notify::kDone;
            if (s_netFail) { word = "Jaquettes : pas de r\xc3\xa9ponse d'Internet"; icon = notify::kError; }
            else if (got) { snprintf(m, sizeof m, lang::T("Jaquettes : %d nouvelle(s)"), got); word = m; }
            else if (total) word = "Jaquettes : aucune trouv\xc3\xa9" "e pour ces jeux";
            else if (s_manual) word = "Jaquettes : il ne manque rien";
            if (s_note >= 0) notify::Close(s_note, now, word ? word : "Jaquettes : termin\xc3\xa9", icon);
            else if (word && (s_manual || got)) notify::Post(now, word, icon);
            s_note = -1;
            if (s_task >= 0) {
                tasks::End(s_task, now, !s_netFail, s_netFail ? (s_why[0] ? s_why : "pas de r\xc3\xa9ponse d'Internet") : nullptr);
                s_task = -1;
            }
            if (!s_netFail && !Ld(s_quit)) s_bootDone = true;
            else s_nextTry = now + 120.0f;               // (no answer: the background tries again in 2 minutes)
            s_manual = false;
            St(s_state, 0);
        }
        if (Ld(s_state) == 1) {                          // v77: the task centre, and a notification that counts in place
            const int d = Ld(s_done), t = Ld(s_total);
            if (s_task < 0) s_task = tasks::Begin(now, "Jaquettes");
            const int seq = Ld(s_itemSeq);
            tasks::Progress(s_task, now, d, t, seq ? s_item[seq & 1] : nullptr);
            const u64 a = __atomic_load_n(&s_alive, __ATOMIC_ACQUIRE);
            if (a != s_aliveSeen) { s_aliveSeen = a; tasks::Alive(s_task, now); }
            if (t > 0 && (d != s_shownDone || t != s_shownTotal)) {
                s_shownDone = d; s_shownTotal = t;
                snprintf(m, sizeof m, lang::T("Jaquettes : %d / %d"), d, t);
                if (s_note < 0) s_note = notify::Open(now, m, notify::kDownload);
                notify::Set(s_note, now, m, (float)d / (float)t);
            }
            return;
        }
        if (qext_game_running()) return;
        if (s_wantManual) { if (Start(true)) s_wantManual = false; return; }
        if (!Settings::s_coversAuto || s_bootDone || !menuOnScreen || now < s_nextTry || nextendo::Busy() || social::NetBusy() || spotify::Busy()) return;
        if (!Online()) { s_nextTry = now + 30.0f; return; }
        if (!Start(false)) s_nextTry = now + 10.0f;
    }

} // namespace brume::covers
