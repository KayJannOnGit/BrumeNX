// fontbake - pre-renders the menu's text into glyph atlases at build time (on the
// PC, with FreeType), so the console only samples a texture: no font code on the
// Switch, exact weights of the variable fonts the mockup uses.
//
// Output: a C++ header with, for each render scale (1 = 1280x720, 1.5 = 1920x1080):
// one R8 atlas + a glyph table per style. A style = font file + weight + CSS px size
// (+ an optional Gaussian blur, stored at a quarter resolution: used for glows).
//
// Build (WSL): g++ -O2 -std=c++17 tools/fontbake.cpp -o fontbake $(pkg-config --cflags --libs freetype2)
// Run:         fontbake OUT.h FONTDIR [CLOCKDIR]
//
// v45 - the clock's other fonts, loaded on demand (memory: the menu is at 90 / 96 MB): the two clock styles (Clock,
// ClockGlow) own a fixed band at the top of each atlas (kClockRegionH rows). For every other clock font, CLOCKDIR gets
// <id>-<scale>.bfnt = that band baked with this font (shrunk a little if it would not fit) + its two glyph tables; the
// console uploads the chosen one over the band (zero memory more). 'BFNT', u32 1, u32 W, u32 H, 2 x { u32 count, f32
// ascent, descent, gscale }, the glyphs (u32 cp, u16 x, y, w, h, f32 bx, by, adv), then W x H bytes.
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

struct Style {
    const char *name, *file;
    int weight;            // variable-font weight (100..900)
    float px;              // CSS px at scale 1
    float blur;            // CSS blur std-dev in px (0 = sharp)
    const char *chars;     // UTF-8
};

// Upper case for the lock screen texts (CSS text-transform: uppercase), digits, punctuation.
#define UPPER " ABCDEFGHIJKLMNOPQRSTUVWXYZÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ0123456789,.'-:·"
#define MIXED " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzàâäçéèêëîïôöùûüÿœæÀÂÄÇÉÈÊËÎÏÔÖÙÛÜ0123456789,.;:!?'\"()-+/·↑↓←→"

#define TITLE_EXTRA "&[]#%_*~=@<>$—–’‘“”…™®©°ºª¿¡ñÑßøØåÅ«»"   // (v89: « » - « Fermer « … » ? », the notifications)

// Values from docs/M1/maquette-brume.html (#hud --cf/--cw/--cs, .greet, .date, .lockbtn .a, .hint).
static const Style kStyles[] = {
    { "Clock",     "LexendGiga-wght.ttf", 100, 150.0f, 0.0f,  "0123456789:" },
    { "ClockGlow", "LexendGiga-wght.ttf", 100, 150.0f, 11.0f, "0123456789:" },   // drop-shadow 0 0 22px
    { "Greet",     "LexendExa-wght.ttf",  300, 15.0f,  0.0f,  UPPER },
    { "Date",      "LexendExa-wght.ttf",  200, 16.0f,  0.0f,  UPPER },
    { "Button",    "Outfit-wght.ttf",     500, 20.0f,  0.0f,  "ABXYLR" },
    { "Hint",      "Outfit-wght.ttf",     300, 15.0f,  0.0f,  MIXED },
    { "TopBar",    "Outfit-wght.ttf",     300, 22.0f,  0.0f,  MIXED },          // the date and time in the top bar (#topTime; v28: +25 %, 22px)
    // The games screen (titles come from file names and installed games: wider punctuation).
    { "GameTitle", "Outfit-wght.ttf",     500, 30.0f,  0.0f,  MIXED TITLE_EXTRA },
    { "GameItem",  "Outfit-wght.ttf",     300, 21.0f,  0.0f,  MIXED TITLE_EXTRA },
    { "GameInfo",  "LexendExa-wght.ttf",  200, 13.0f,  0.0f,  UPPER "/()" },
    { "BtnSmall",  "Outfit-wght.ttf",     600, 13.5f,  0.0f,  "ABXYLRZ+-" },          // .hints .btn-g: font 600 13.5px (v28: +25 %)
    { "TabLabel",  "Outfit-wght.ttf",     400, 18.0f,  0.0f,  MIXED },                // .tab: font 400 18px Outfit (v28: +25 %)
    { "HintBig",   "Outfit-wght.ttf",     300, 19.0f,  0.0f,  MIXED TITLE_EXTRA },    // .hints / #toast: 300 19px (v28: +25 %)
    { "PageTitle", "Outfit-wght.ttf",     200, 40.0f,  0.0f,  MIXED },                // .pcard .pt: 200 40px (the pages, v28)
    // the settings page (mockup v26-v27: .xi.on .xit 27px, .xid 16px, .xiv 19px / .xo 20px)
    { "SetSel",    "Outfit-wght.ttf",     300, 27.0f,  0.0f,  MIXED TITLE_EXTRA },
    { "SetDesc",   "Outfit-wght.ttf",     300, 16.0f,  0.0f,  MIXED TITLE_EXTRA },
    { "SetOpt",    "Outfit-wght.ttf",     300, 20.0f,  0.0f,  MIXED TITLE_EXTRA },
};
static const float kScales[] = { 1.0f, 1.5f };

// v45 - the clock's other fonts (all SIL OFL, assets/fonts/OFL-*.txt), in the order of Réglages > Brume > Police de l'heure
struct ClockAlt { const char *id, *file; int weight; };
static const ClockAlt kClockAlts[] = {
    { "michroma", "Michroma-Regular.ttf", 400 }, { "bigshoulders", "BigShouldersDisplay-wght.ttf", 200 },
    { "instrument", "InstrumentSerif-Regular.ttf", 400 }, { "orbitron", "Orbitron-wght.ttf", 400 },
    { "majormono", "MajorMonoDisplay-Regular.ttf", 400 }, { "monoton", "Monoton-Regular.ttf", 400 },
};

struct Glyph { uint32_t cp; int x, y, w, h; float bx, by, adv; };   // atlas px; bearings/advance in target px
struct Baked { std::vector<Glyph> glyphs; float ascent, descent, gscale; };

static std::vector<uint32_t> Utf8(const char *s) {
    std::vector<uint32_t> out;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xF0)      { c = (c & 7) << 18;  c |= (*p++ & 63) << 12; c |= (*p++ & 63) << 6; c |= *p++ & 63; }
        else if (c >= 0xE0) { c = (c & 15) << 12; c |= (*p++ & 63) << 6;  c |= *p++ & 63; }
        else if (c >= 0xC0) { c = (c & 31) << 6;  c |= *p++ & 63; }
        out.push_back(c);
    }
    return out;
}

static void Blur(std::vector<float> &img, int w, int h, float sigma) {
    const int r = (int)std::ceil(sigma * 3.0f);
    std::vector<float> k(2 * r + 1), tmp(img.size());
    float sum = 0;
    for (int i = -r; i <= r; i++) { k[i + r] = std::exp(-0.5f * i * i / (sigma * sigma)); sum += k[i + r]; }
    for (float &v : k) v /= sum;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float a = 0; for (int i = -r; i <= r; i++) { int xx = x + i; if (xx >= 0 && xx < w) a += img[y * w + xx] * k[i + r]; }
        tmp[y * w + x] = a; }
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float a = 0; for (int i = -r; i <= r; i++) { int yy = y + i; if (yy >= 0 && yy < h) a += tmp[yy * w + x] * k[i + r]; }
        img[y * w + x] = a; }
}

// One style into an atlas (W wide, Hmax tall) from the pen position; false = it does not fit.
static bool BakeStyle(FT_Library lib, const char *dir, const char *file, int weight, float pxCss, float blur, const char *chars,
                      float scale, std::vector<uint8_t> &atlas, int W, int Hmax, int &penX, int &penY, int &rowH, int &usedH, Baked &b) {
    FT_Face face;
    std::string path = std::string(dir) + "/" + file;
    if (FT_New_Face(lib, path.c_str(), 0, &face)) { fprintf(stderr, "font %s\n", path.c_str()); exit(1); }
    FT_Fixed coord = (FT_Fixed)weight << 16;
    FT_Set_Var_Design_Coordinates(face, 1, &coord);
    const float px = pxCss * scale;
    FT_Set_Char_Size(face, 0, (FT_F26Dot6)std::lround(px * 64.0f), 72, 72);
    // A blurred style is stored at a quarter resolution (it is smooth anyway).
    const float gscale = blur > 0 ? 0.25f : 1.0f;
    const float sigma = blur * scale * gscale;
    const int pad = blur > 0 ? (int)std::ceil(sigma * 3.0f) + 1 : 1;
    b.glyphs.clear();
    b.ascent  = (float)face->ascender  * px / face->units_per_EM;
    b.descent = (float)-face->descender * px / face->units_per_EM;
    b.gscale  = gscale;
    bool ok = true;
    for (uint32_t cp : Utf8(chars)) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_NO_HINTING)) continue;
        FT_GlyphSlot g = face->glyph;
        const int bw = g->bitmap.width, bh = g->bitmap.rows;
        // glyph bitmap -> float, downscaled for blurred styles, padded, blurred
        const int sw = (int)std::ceil(bw * gscale) + 2 * pad, sh = (int)std::ceil(bh * gscale) + 2 * pad;
        std::vector<float> img((size_t)std::max(1, sw * sh), 0.0f);
        for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) {
            float v = g->bitmap.buffer[y * g->bitmap.pitch + x] / 255.0f;
            int dx = pad + (int)(x * gscale), dy = pad + (int)(y * gscale);
            img[dy * sw + dx] += v * gscale * gscale;
        }
        if (sigma > 0) Blur(img, sw, sh, sigma);
        if (penX + sw + 1 > W) { penX = 1; penY += rowH + 1; rowH = 0; }
        if (penY + sh + 1 > Hmax) { ok = false; break; }
        for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++)
            atlas[(size_t)(penY + y) * W + penX + x] = (uint8_t)std::lround(std::min(1.0f, img[y * sw + x]) * 255.0f);
        Glyph gl;
        gl.cp = cp; gl.x = penX; gl.y = penY; gl.w = sw; gl.h = sh;
        gl.bx = (float)g->bitmap_left - pad / gscale;         // target px
        gl.by = (float)g->bitmap_top + pad / gscale;
        gl.adv = (float)g->advance.x / 64.0f;
        b.glyphs.push_back(gl);
        penX += sw + 1; rowH = std::max(rowH, sh); usedH = std::max(usedH, penY + sh + 1);
    }
    FT_Done_Face(face);
    return ok;
}

static void Put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void PutF(FILE *f, float v) { fwrite(&v, 4, 1, f); }
static void Put16(FILE *f, int v) { const uint16_t u = (uint16_t)v; fwrite(&u, 2, 1, f); }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: fontbake OUT.h FONTDIR\n"); return 1; }
    FT_Library lib;
    if (FT_Init_FreeType(&lib)) return 1;
    FILE *out = fopen(argv[1], "w");
    if (!out) return 1;
    fprintf(out, "// Generated by tools/fontbake.cpp - do not edit.\n#pragma once\n#include <cstdint>\n\n");
    fprintf(out, "namespace brume::fonts {\n\nenum Style {");
    for (size_t s = 0; s < sizeof(kStyles) / sizeof(kStyles[0]); s++) fprintf(out, " k%s = %zu,", kStyles[s].name, s);
    fprintf(out, " kStyleCount = %zu };\n\n", sizeof(kStyles) / sizeof(kStyles[0]));
    fprintf(out, "struct Glyph { uint32_t cp; uint16_t x, y, w, h; float bx, by, adv; };\n");
    fprintf(out, "struct Face { const Glyph *glyphs; int count; float ascent, descent, gscale; };\n");
    fprintf(out, "struct Atlas { int w, h; const uint8_t *pixels; Face faces[kStyleCount]; };\n\n");
    fprintf(out, "// CSS font size of each style (menu px at scale 1).\ninline float kStylePx(int s) { static const float v[] = {");
    for (const Style &st : kStyles) fprintf(out, " %.1ff,", st.px);
    fprintf(out, " }; return v[s]; }\n\n");

    int regionHs[2] = { 0, 0 };
    for (int si = 0; si < 2; si++) {
        const float scale = kScales[si];
        const int W = si == 0 ? 1024 : 2048;
        std::vector<uint8_t> atlas((size_t)W * 4096, 0);
        int penX = 1, penY = 1, rowH = 0, usedH = 0;
        std::vector<Baked> baked;
        int regionH = 0;
        for (size_t si2 = 0; si2 < sizeof(kStyles) / sizeof(kStyles[0]); si2++) {
            const Style &st = kStyles[si2];
            Baked b;
            if (!BakeStyle(lib, argv[2], st.file, st.weight, st.px, st.blur, st.chars, scale, atlas, W, 4096, penX, penY, rowH, usedH, b)) {
                fprintf(stderr, "atlas full\n"); return 1;
            }
            baked.push_back(b);
            if (si2 == 1) {                                  // v45: the clock band ends here (whole rows, a multiple of 4)
                regionH = (usedH + 3) & ~3;
                penX = 1; penY = regionH; rowH = 0;
            }
        }
        regionHs[si] = regionH;
        const int H = (usedH + 3) & ~3;
        fprintf(out, "// ---- scale %.1f : %dx%d ----\n", scale, W, H);
        for (size_t s = 0; s < baked.size(); s++) {
            fprintf(out, "static const Glyph kGlyphs%d_%zu[] = {\n", si, s);
            for (const Glyph &g : baked[s].glyphs)
                fprintf(out, "    { 0x%X, %d, %d, %d, %d, %.3ff, %.3ff, %.3ff },\n", g.cp, g.x, g.y, g.w, g.h, g.bx, g.by, g.adv);
            fprintf(out, "};\n");
        }
        fprintf(out, "alignas(256) static const uint8_t kPixels%d[] = {\n", si);
        for (size_t i = 0; i < (size_t)W * H; i++) fprintf(out, "%u,%s", atlas[i], (i % 32) == 31 ? "\n" : "");
        fprintf(out, "};\n");
        fprintf(out, "static const Atlas kAtlas%d = { %d, %d, kPixels%d, {\n", si, W, H, si);
        for (size_t s = 0; s < baked.size(); s++)
            fprintf(out, "    { kGlyphs%d_%zu, %zu, %.3ff, %.3ff, %.3ff },\n", si, s, baked[s].glyphs.size(), baked[s].ascent, baked[s].descent, baked[s].gscale);
        fprintf(out, "} };\n\n");
        fprintf(stderr, "scale %.1f: atlas %dx%d (bande de l'heure : %d lignes)\n", scale, W, H, regionH);
        for (const ClockAlt &ca : kClockAlts) {
            if (argc < 4) break;
            std::vector<uint8_t> band((size_t)W * regionH, 0);
            Baked bc, bg;
            float f = 1.0f;
            for (int tries = 0; tries < 16; tries++, f *= 0.96f) {
                std::fill(band.begin(), band.end(), 0);
                int px2 = 1, py2 = 1, rh2 = 0, uh2 = 0;
                if (BakeStyle(lib, argv[2], ca.file, ca.weight, 150.0f * f, 0.0f, "0123456789:", scale, band, W, regionH, px2, py2, rh2, uh2, bc) &&
                    BakeStyle(lib, argv[2], ca.file, ca.weight, 150.0f * f, 11.0f * f, "0123456789:", scale, band, W, regionH, px2, py2, rh2, uh2, bg)) break;
                bc.glyphs.clear();
            }
            if (bc.glyphs.empty()) { fprintf(stderr, "police %s : ne rentre pas\n", ca.id); continue; }
            std::string fn = std::string(argv[3]) + "/" + ca.id + "-" + (si ? "1" : "0") + ".bfnt";
            FILE *bf = fopen(fn.c_str(), "wb");
            if (!bf) { fprintf(stderr, "ecriture %s\n", fn.c_str()); return 1; }
            fwrite("BFNT", 1, 4, bf); Put32(bf, 1); Put32(bf, (uint32_t)W); Put32(bf, (uint32_t)regionH);
            for (const Baked *bk : { &bc, &bg }) { Put32(bf, (uint32_t)bk->glyphs.size()); PutF(bf, bk->ascent); PutF(bf, bk->descent); PutF(bf, bk->gscale); }
            for (const Baked *bk : { &bc, &bg })
                for (const Glyph &g : bk->glyphs) { Put32(bf, g.cp); Put16(bf, g.x); Put16(bf, g.y); Put16(bf, g.w); Put16(bf, g.h); PutF(bf, g.bx); PutF(bf, g.by); PutF(bf, g.adv); }
            fwrite(band.data(), 1, band.size(), bf);
            fclose(bf);
            fprintf(stderr, "  police %s (echelle %.1f) : taille %.0f %%\n", ca.id, scale, f * 100);
        }
    }
    fprintf(out, "// v45: the clock's band at the top of each atlas (rows), for the fonts loaded on demand\n");
    fprintf(out, "constexpr int kClockRegionH[2] = { %d, %d };\n\n", regionHs[0], regionHs[1]);
    fprintf(out, "} // namespace brume::fonts\n");
    fclose(out);
    FT_Done_FreeType(lib);
    return 0;
}
