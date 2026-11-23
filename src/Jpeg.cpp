// Brume - the Switch games' icons are JPEG (the title's control data, given by the loader): decoded here with
// stb_image (public domain / MIT, see THIRDPARTY.md). JPEG only, from memory, and NO thread-local storage
// (the qlaunch-ext loader has no TLS: a thread_local variable crashes the menu).
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "Shelf.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ASSERT(x) ((void)0)
#define STBI_MAX_DIMENSIONS 2048
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include "third_party/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace brume {

    namespace {
        // v90 (perf, memory) - the rows of a JPEG one by one (RGBA), never the whole picture in memory: stb_image's own
        // decoder and its own colour conversion (load_jpeg_image above, the same code with ONE output row instead of the
        // whole output) - the same pixels. A capture (1280 x 720) needed ~5.5 MB at once (planes 1.4 MB + output 3.7 MB +
        // the file): with ~4 MB free in the heap it failed every time (« photo nette illisible »). Now ~1.9 MB.
        // begin(user, w, h): false = stop (a size we don't want); row(user, y, rgba): false = stop.
        bool JpegRows(const void *data, int bytes, bool (*begin)(void *, int, int), bool (*row)(void *, int, const uint8_t *), void *user) {
            if (!data || bytes < 64 || bytes > (8 << 20)) return false;
            stbi__context s;
            stbi__start_mem(&s, (const stbi_uc *)data, bytes);
            stbi__jpeg *z = (stbi__jpeg *)stbi__malloc(sizeof(stbi__jpeg));
            if (!z) return false;
            memset(z, 0, sizeof(stbi__jpeg));
            z->s = &s;
            stbi__setup_jpeg(z);
            z->s->img_n = 0;                                   // (stbi__cleanup_jpeg safe)
            bool ok = false;
            stbi_uc *line = nullptr;
            do {
                if (!stbi__decode_jpeg_image(z)) break;
                const int n = 4;                               // RGBA, as stbi_load_from_memory(..., 4)
                const int is_rgb = z->s->img_n == 3 && (z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif));
                const int decode_n = z->s->img_n;              // (n >= 3: every component)
                if (decode_n <= 0 || decode_n > 4) break;
                const int W = (int)z->s->img_x, H = (int)z->s->img_y;
                if (W < 4 || H < 4) break;
                if (begin && !begin(user, W, H)) break;
                stbi__resample res_comp[4];
                bool lines = true;
                for (int k = 0; k < decode_n && lines; ++k) {
                    stbi__resample *r = &res_comp[k];
                    z->img_comp[k].linebuf = (stbi_uc *)stbi__malloc(z->s->img_x + 3);
                    if (!z->img_comp[k].linebuf) { lines = false; break; }
                    r->hs      = z->img_h_max / z->img_comp[k].h;
                    r->vs      = z->img_v_max / z->img_comp[k].v;
                    r->ystep   = r->vs >> 1;
                    r->w_lores = (z->s->img_x + r->hs - 1) / r->hs;
                    r->ypos    = 0;
                    r->line0   = r->line1 = z->img_comp[k].data;
                    if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
                    else if (r->hs == 1 && r->vs == 2) r->resample = stbi__resample_row_v_2;
                    else if (r->hs == 2 && r->vs == 1) r->resample = stbi__resample_row_h_2;
                    else if (r->hs == 2 && r->vs == 2) r->resample = z->resample_row_hv_2_kernel;
                    else                               r->resample = stbi__resample_row_generic;
                }
                if (!lines) break;
                line = (stbi_uc *)stbi__malloc((size_t)n * z->s->img_x);
                if (!line) break;
                stbi_uc *coutput[4] = { NULL, NULL, NULL, NULL };
                bool all = true;
                for (unsigned j = 0; j < z->s->img_y && all; ++j) {
                    stbi_uc *out = line;
                    for (int k = 0; k < decode_n; ++k) {
                        stbi__resample *r = &res_comp[k];
                        const int y_bot = r->ystep >= (r->vs >> 1);
                        coutput[k] = r->resample(z->img_comp[k].linebuf, y_bot ? r->line1 : r->line0, y_bot ? r->line0 : r->line1, r->w_lores, r->hs);
                        if (++r->ystep >= r->vs) {
                            r->ystep = 0;
                            r->line0 = r->line1;
                            if (++r->ypos < z->img_comp[k].y) r->line1 += z->img_comp[k].w2;
                        }
                    }
                    stbi_uc *y = coutput[0];
                    if (z->s->img_n == 3) {
                        if (is_rgb) {
                            for (unsigned i = 0; i < z->s->img_x; ++i) { out[0] = y[i]; out[1] = coutput[1][i]; out[2] = coutput[2][i]; out[3] = 255; out += n; }
                        } else z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
                    } else if (z->s->img_n == 4) {
                        if (z->app14_color_transform == 0) {              // CMYK
                            for (unsigned i = 0; i < z->s->img_x; ++i) {
                                const stbi_uc m = coutput[3][i];
                                out[0] = stbi__blinn_8x8(coutput[0][i], m); out[1] = stbi__blinn_8x8(coutput[1][i], m);
                                out[2] = stbi__blinn_8x8(coutput[2][i], m); out[3] = 255; out += n;
                            }
                        } else if (z->app14_color_transform == 2) {       // YCCK
                            z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
                            for (unsigned i = 0; i < z->s->img_x; ++i) {
                                const stbi_uc m = coutput[3][i];
                                out[0] = stbi__blinn_8x8(255 - out[0], m); out[1] = stbi__blinn_8x8(255 - out[1], m);
                                out[2] = stbi__blinn_8x8(255 - out[2], m); out += n;
                            }
                        } else z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2], z->s->img_x, n);
                    } else {
                        for (unsigned i = 0; i < z->s->img_x; ++i) { out[0] = out[1] = out[2] = y[i]; out[3] = 255; out += n; }
                    }
                    all = row(user, (int)j, line);
                }
                ok = all;
            } while (false);
            if (line) STBI_FREE(line);
            stbi__cleanup_jpeg(z);
            STBI_FREE(z);
            return ok;
        }

        // the bilinear resample of DecodeJpegTo, fed row by row: the same arithmetic as the whole-picture version it
        // replaces (an output row is made as soon as its two source rows are there; two source rows kept at a time)
        struct Resize {
            uint8_t *out = nullptr; int ow = 0, oh = 0, w = 0, h = 0, next = 0;
            uint8_t *ring = nullptr;                          // source rows j & 1
        };
        bool ResizeBegin(void *u, int w, int h) {
            Resize &r = *(Resize *)u;
            r.w = w; r.h = h; r.next = 0;
            r.ring = (uint8_t *)malloc((size_t)w * 4 * 2);
            return r.ring != nullptr;
        }
        bool ResizeRow(void *u, int j, const uint8_t *src) {
            Resize &r = *(Resize *)u;
            memcpy(r.ring + (size_t)(j & 1) * r.w * 4, src, (size_t)r.w * 4);
            const int w = r.w, h = r.h, ow = r.ow, oh = r.oh;
            while (r.next < oh) {
                const int y = r.next;
                const float fy = ((float)y + 0.5f) * h / oh - 0.5f;
                int y0 = (int)fy; if (y0 < 0) y0 = 0; if (y0 > h - 1) y0 = h - 1;
                const int y1 = y0 + 1 < h ? y0 + 1 : y0;
                if (y1 > j) break;                            // (its second row is not decoded yet)
                const float ty = fy - y0 < 0 ? 0 : (fy - y0 > 1 ? 1 : fy - y0);
                const uint8_t *row0 = r.ring + (size_t)(y0 & 1) * w * 4, *row1 = r.ring + (size_t)(y1 & 1) * w * 4;
                for (int x = 0; x < ow; x++) {
                    const float fx = ((float)x + 0.5f) * w / ow - 0.5f;
                    int x0 = (int)fx; if (x0 < 0) x0 = 0; if (x0 > w - 1) x0 = w - 1;
                    const int x1 = x0 + 1 < w ? x0 + 1 : x0;
                    const float tx = fx - x0 < 0 ? 0 : (fx - x0 > 1 ? 1 : fx - x0);
                    const uint8_t *a = row0 + x0 * 4, *b = row0 + x1 * 4;
                    const uint8_t *c = row1 + x0 * 4, *d = row1 + x1 * 4;
                    uint8_t *o = r.out + (y * ow + x) * 4;
                    for (int k = 0; k < 4; k++) {
                        const float top = a[k] + (b[k] - a[k]) * tx, bot = c[k] + (d[k] - c[k]) * tx;
                        o[k] = (uint8_t)(top + (bot - top) * ty + 0.5f);
                    }
                }
                r.next++;
            }
            return true;
        }

        // v90: the album's sharp photo, straight into BC1 four rows at a time (no 3.7 MB picture in between)
        struct ToBc1 { int ew, eh; uint8_t *bc; uint8_t *strip; };
        bool Bc1Begin(void *u, int w, int h) {
            ToBc1 &t = *(ToBc1 *)u;
            if (w != t.ew || h != t.eh || (w & 3) || (h & 3)) return false;
            t.strip = (uint8_t *)malloc((size_t)w * 4 * 4);
            return t.strip != nullptr;
        }
        bool Bc1Row(void *u, int j, const uint8_t *src) {
            ToBc1 &t = *(ToBc1 *)u;
            memcpy(t.strip + (size_t)(j & 3) * t.ew * 4, src, (size_t)t.ew * 4);
            if ((j & 3) == 3) Shelf::EncodeBC1(t.strip, t.ew, 4, t.bc + (size_t)(j / 4) * (t.ew / 4) * 8);
            return true;
        }
    }

    // JPEG -> out (ow x oh RGBA, resampled bilinear). false if the data is not a usable image.
    bool DecodeJpegTo(const void *data, int bytes, uint8_t *out, int ow, int oh) {
        if (!data || bytes < 64 || bytes > (8 << 20) || !out || ow < 4 || oh < 4) return false;
        Resize r;
        r.out = out; r.ow = ow; r.oh = oh;
        const bool ok = JpegRows(data, bytes, ResizeBegin, ResizeRow, &r) && r.next == oh;
        free(r.ring);
        return ok;
    }
    bool DecodeJpegSquare(const void *data, int bytes, uint8_t *out, int size) { return DecodeJpegTo(data, bytes, out, size, size); }
    // v90: a w x h JPEG (exactly that size) -> BC1 (w/4 x h/4 blocks, 8 bytes each) into bc. false if unusable / another size.
    bool DecodeJpegBC1(const void *data, int bytes, int w, int h, uint8_t *bc) {
        if (!bc || w < 4 || h < 4) return false;
        ToBc1 t = { w, h, bc, nullptr };
        const bool ok = JpegRows(data, bytes, Bc1Begin, Bc1Row, &t);
        free(t.strip);
        return ok;
    }

} // namespace brume
