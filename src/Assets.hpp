// Brume - what the wheel needs, baked on the PC from the mockup (tools/bake-assets.html) and read from the SD:
//   sdmc:/brume/modeles/<console>.bmdl  the consoles (vertices + BC1 textures + material), processed like the mockup
//   sdmc:/brume/modeles/env.bin         the « room » light of the mockup (RoomEnvironment), 6 roughness levels
//   sdmc:/brume/ui.bin                  the wheel's interface (bar, banners, hints) laid out by the browser
// Every file is read defensively: a damaged or missing one only hides what it holds.
#pragma once
#include "Gpu.hpp"

namespace brume {

    struct MeshPart {
        DkGpuAddr vtx = 0, idx = 0;
        uint32_t  vtxBytes = 0, idxCount = 0;
        DkGpuAddr idx1 = 0;                // LOD 1 indices (lighter, same vertices): consoles at the back
        uint32_t  idx1Count = 0;
        int       tex = -1;                // index into Assets::tex
        int       emiTex = -1;             // emissive map (three: emissive x emissiveMap), index into Assets::tex
        float     base[4], metal, rough, env, glass, tint[3], emi[3], dbl;
        uint32_t  splitL = 0, splitR = 0;  // v40 (the Switch): indices [0, splitL) the console, [splitL, splitR) the left Joy-Con, then the right one
    };
    struct Model { MeshPart parts[24]; int count = 0; };

    struct Sprite { char name[24], group[12]; float x, y, w, h; float uv[2][4]; };   // menu px; uv per scale
    // v28 - the settings carousel's 3D icons (sdmc:/brume/modeles/icones.bin, 'BICN'): glass pebbles, same vertex layout
    struct IconMesh { DkGpuAddr vtx = 0, idx = 0; uint32_t vtxBytes = 0, idxCount = 0; };

    class Assets {
    public:
        // descriptor ids: 3, 4 = interface (720p, 1080p), 5 = light, 6.. = consoles' textures
        bool Load(Gpu &gpu, char *err, int errCap);
        Model   model[8];
        Model   block;              // v93: the consoles without a model yet (Consoles.hpp, index 8..): a rounded block of glass, made here
        Target  tex[56];
        int     ntex = 0;
        Target  env, ui[2];
        bool    envOk = false, uiOk = false;
        bool    uiSdf = false;      // v73: the settings icons carry their distance to the edge (blue): the PS3 liquid glass can be drawn
        Program glassProg;          // v74: their glass (ui_vsh + verre_fsh), drawn by Ui::Flush in its own runs
        float   envSH[9][3] = {};   // env level 5 (roughness 1) as spherical harmonics, for the diffuse room light
        float   joyCell[2] = { -1, -1 };   // v40: the Switch's Joy-Con body colours in its palette (u of the cell centre), recoloured
        Sprite *sprites = nullptr;
        int     nsprites = 0;
        const Sprite *Find(const char *name) const;
        IconMesh icon[10];
        int      nicons = 0;
    private:
        bool LoadModels(Gpu &gpu, char *err, int errCap);
        bool LoadEnv(Gpu &gpu);
        bool LoadUi(Gpu &gpu);
        bool LoadIcons(Gpu &gpu);
        DkMemBlock m_geo = nullptr, m_iconGeo = nullptr;
    };

} // namespace brume
