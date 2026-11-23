// Brume - v93 (Yass: « Brume doit tout détecter ») - THE table of the wheel's consoles: one line per console, read by the
// detection (Emus.cpp), the wheel (Wheel.cpp), the games (Games.cpp) and the covers (Covers.cpp). All of it is constant data
// (read-only memory, nothing allocated). The first 8 keep their index, order and look (the baked models, ui.bin's name
// banners, the recorded chords); the new ones come after them (index 8..) and stand on the ring between the GBA and the
// native ports (kOrder). They have no 3D model yet: the wheel shows a block of glass with their name (Wheel.cpp).
//   alias: the names people give the games' folder (normalised: lower case, letters and digits only), in sdmc:/roms/ and in
//          sdmc:/tico/roms/ (Tico's slugs: nes, snes, gb, gbc, genesis, master-system, game-gear, psx, gc, nds...)
//   emu:   the emulators that play it, by a piece of their normalised file name (or the name written in the .nro): the
//          best one found wins (prio); a RetroArch core (sdmc:/retroarch/cores/<core>_libretro_libnx.nro) is matched the same way
//   voice: the recorded chord / choir it sings (Audio's 8 pads; Wheel's chords). Neighbours on the ring differ.
//   needGames: its emulator is shared with another console (Genesis Plus GX plays the Mega Drive AND the Master System
//          and the Game Gear): it only stands on the wheel when it also has games
#pragma once
#include <cstdint>

namespace brume::consoles {

    constexpr int kMax = 16;                 // the masks are 16 bits (bit i = console i)
    constexpr int kSwitch = 0, kPorts = 7;
    enum : int { kDS = 8, kSNES, kNES, kMD, kGB, kSMS, kGG, kCount };
    static_assert(kCount <= kMax, "16 consoles at most (uint16_t masks)");

    struct Emu { const char *pat; int16_t prio; const char *name; };
    struct Def {
        const char *key;         // Brume's own name: emulateurs.ini, sdmc:/brume/jaquettes/<key>/, the known file
        const char *name;        // shown (the name banner, the notifications)
        const char *label;       // the provisional block's short name ("" : a real model)
        const char *alias[8];
        const char *ext[8];
        Emu         emu[6];
        const char *advice;      // what to install (games without their emulator)
        const char *thumbs;      // its libretro-thumbnails repository (covers), nullptr none
        uint32_t    tint;        // its light (very little of it reaches the scene)
        uint8_t     voice;       // 0..7
        uint8_t     box;         // the case of its games: 0 DVD, 1 jewel, 2 UMD, 3 Switch, 4 cardboard
        bool        ra;          // a renamed RetroArch copy may be named after it (« switch/retroarch-snes/ »)
        bool        portable;    // the launch dives into its screen (else: a home console spins)
        bool        needGames;
    };

    inline constexpr Def kDefs[kCount] = {
        { "switch", "Switch", "", {}, {}, {}, "", nullptr, 0xc0506a, 0, 3, false, true, false },
        { "ps2", "PlayStation 2", "", { "ps2", "playstation2", "sonyplaystation2" }, { ".iso", ".chd", ".cso", ".bin" },
          { { "nethersx2", 100, "NetherSX2" }, { "aethersx2", 90, "AetherSX2" }, { "pcsx2", 80, "PCSX2" } },
          "NetherSX2", "Sony_-_PlayStation_2", 0x3550c8, 1, 0, false, false, false },
        { "gamecube", "GameCube", "", { "gamecube", "gc", "ngc", "nintendogamecube" }, { ".iso", ".gcm", ".rvz", ".ciso", ".gcz" },
          { { "nezumiiruka", 100, "Nezumiiruka" }, { "nezumi", 98, "Nezumiiruka" }, { "dolphin", 90, "Dolphin" } },
          "Nezumiiruka", "Nintendo_-_GameCube", 0x5a3cc0, 2, 0, true, false, false },
        { "n64", "Nintendo 64", "", { "n64", "nintendo64" }, { ".z64", ".n64", ".v64", ".zip" },
          { { "mupen64plusnext", 100, "Mupen64Plus-Next" }, { "parallaln64", 90, "ParaLLEl N64" }, { "mupen64", 85, "Mupen64Plus" } },
          "RetroArch", "Nintendo_-_Nintendo_64", 0x2f8a62, 3, 4, true, false, false },
        { "ps1", "PlayStation", "", { "ps1", "psx", "playstation", "playstation1", "sonyplaystation", "psone" }, { ".chd", ".cue", ".pbp", ".m3u" },
          { { "pcsxrearmed", 100, "PCSX ReARMed" }, { "swanstation", 95, "SwanStation" }, { "duckstation", 92, "DuckStation" },
            { "mednafenpsx", 90, "Beetle PSX" }, { "beetlepsx", 90, "Beetle PSX" } },
          "RetroArch", "Sony_-_PlayStation", 0x6a7494, 4, 1, true, false, false },
        { "psp", "PSP", "", { "psp", "playstationportable", "sonyplaystationportable" }, { ".iso", ".cso", ".pbp" },
          { { "ppsspp", 100, "PPSSPP" } },
          "PPSSPP", "Sony_-_PlayStation_Portable", 0x3a62c4, 5, 2, true, true, false },
        { "gba", "Game Boy Advance", "", { "gba", "gameboyadvance", "nintendogameboyadvance" }, { ".gba", ".zip" },
          { { "mgba", 100, "mGBA" }, { "vbanext", 90, "VBA Next" }, { "vbam", 88, "VBA-M" }, { "gpsp", 85, "gpSP" }, { "mednafengba", 80, "Beetle GBA" } },
          "RetroArch", "Nintendo_-_Game_Boy_Advance", 0x5e48b0, 6, 4, true, true, false },
        { "ports", "Ports natifs", "", {}, {}, {}, "", nullptr, 0xa0603a, 7, 0, false, false, false },
        // ---- v93: the new ones (no 3D model yet) ----
        { "ds", "Nintendo DS", "DS", { "nds", "ds", "nintendods" }, { ".nds", ".zip" },
          { { "drastic", 100, "DraStic" }, { "melonds", 90, "melonDS" }, { "desmume", 85, "DeSmuME" } },
          "DraStic", "Nintendo_-_Nintendo_DS", 0x4a5cc8, 2, 0, true, true, false },
        { "snes", "Super Nintendo", "SNES", { "snes", "sfc", "supernintendo", "superfamicom", "supernes", "nintendosnes" }, { ".sfc", ".smc", ".zip", ".fig", ".swc" },
          { { "snes9x", 100, "Snes9x" }, { "bsnes", 90, "bsnes" }, { "mesens", 85, "Mesen-S" } },
          "RetroArch", "Nintendo_-_Super_Nintendo_Entertainment_System", 0x6650b8, 3, 4, true, false, false },
        { "nes", "NES", "NES", { "nes", "famicom", "fc", "fds", "nintendones", "nintendoentertainmentsystem" }, { ".nes", ".fds", ".unf", ".unif", ".zip" },
          { { "nestopia", 100, "Nestopia" }, { "fceumm", 98, "FCEUmm" }, { "mesenlibretro", 95, "Mesen" }, { "quicknes", 90, "QuickNES" } },
          "RetroArch", "Nintendo_-_Nintendo_Entertainment_System", 0x7a7e98, 5, 4, true, false, false },
        { "megadrive", "Mega Drive", "MEGA DRIVE", { "megadrive", "genesis", "md", "segagenesis", "segamegadrive", "gen" }, { ".md", ".gen", ".smd", ".bin", ".zip" },
          { { "genesisplusgx", 100, "Genesis Plus GX" }, { "picodrive", 95, "PicoDrive" }, { "clownmdemu", 90, "clownmdemu" }, { "blastem", 88, "BlastEm" } },
          "RetroArch", "Sega_-_Mega_Drive_-_Genesis", 0x3448b0, 1, 0, true, false, false },
        { "gb", "Game Boy", "GAME BOY", { "gb", "gbc", "gameboy", "gameboycolor", "nintendogameboy", "nintendogameboycolor" }, { ".gb", ".gbc", ".sgb", ".zip" },
          { { "gambatte", 100, "Gambatte" }, { "sameboy", 98, "SameBoy" }, { "gearboy", 95, "Gearboy" }, { "tgbdual", 85, "TGB Dual" } },
          "RetroArch", "Nintendo_-_Game_Boy", 0x58689c, 4, 4, true, true, false },
        { "mastersystem", "Master System", "MASTER SYSTEM", { "mastersystem", "sms", "segamastersystem", "markiii" }, { ".sms", ".zip" },
          { { "genesisplusgx", 100, "Genesis Plus GX" }, { "gearsystem", 98, "Gearsystem" }, { "smsplus", 95, "SMS Plus" }, { "picodrive", 90, "PicoDrive" } },
          "RetroArch", "Sega_-_Master_System_-_Mark_III", 0x4058b4, 6, 0, true, false, true },
        { "gamegear", "Game Gear", "GAME GEAR", { "gamegear", "gg", "segagamegear" }, { ".gg", ".zip" },
          { { "genesisplusgx", 100, "Genesis Plus GX" }, { "gearsystem", 98, "Gearsystem" }, { "smsplus", 95, "SMS Plus" } },
          "RetroArch", "Sega_-_Game_Gear", 0x3c4c9c, 0, 0, true, true, true },
    };
    // the ring, from the Switch round to the native ports (the first 8 keep their order)
    inline constexpr uint8_t kOrder[kCount] = { 0, 1, 2, 3, 4, 5, 6, kDS, kSNES, kNES, kMD, kSMS, kGG, kGB, kPorts };

    constexpr bool Valid(int i) { return i >= 0 && i < kCount; }
    constexpr bool Emulated(int i) { return i > 0 && i < kCount && i != kPorts; }   // a console played by an emulator
    constexpr bool HasModel(int i) { return i >= 0 && i < 8; }
}
