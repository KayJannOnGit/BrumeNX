// Brume - v90: the emulators found by themselves (Yass, 2026-11-19: « Les consoles du carrousel s'affichent uniquement si
// l'émulateur associé est installé. [...] Il lance juste sa Switch après le transfert des ROMs et des émulateurs, et la console
// s'incruste dans la roue, avec une petite notif « Nouvel émulateur détecté » »).
// v93 (Yass: « Brume doit tout détecter »): the consoles come from ONE table (Consoles.hpp, 15 consoles, masks of 16 bits).
// - the scan (in Games' count thread, at boot and back from a game / homebrew), bounded everywhere:
//   1. every .nro of sdmc:/switch/ and of its folders (one level), recognised by their file name, else by the name written
//      inside the .nro (its NACP: a renamed file still works); a RetroArch copy named after its console (« retroarch-snes »);
//   2. RetroArch's cores (sdmc:/retroarch/cores/<core>_libretro_libnx.nro: each one is a whole RetroArch with its core,
//      started as "<core.nro>" "<game>" - never through retroarch_switch.nro, its relaunch crashes on FW 22.5);
//   3. Tico's emulators (sdmc:/tico/modules/<id>/module.json - read only, never written: its « nro », its « launch »
//      template and its consoles' slugs; started with that template, several arguments);
//   4. sdmc:/brume/emulateurs.ini (written by hand) still comes first.
//   The best one per console wins.
// - the wheel shows a console only when it has an emulator (the Switch always, « Ports natifs » when there are ports; a console
//   whose emulator is shared - Master System, Game Gear - only with games); games without their emulator: a discreet hint, once.
// - what was found is remembered (sdmc:/brume/emulateurs-detectes.txt): a NEW emulator arrives on the wheel with its own
//   little staging (Wheel::SetPresent) and the notification; a removed one leaves quietly.
// - the games' folders: sdmc:/roms/<a name people use> and sdmc:/tico/roms/<Tico's slug> (Consoles.hpp: alias).
#pragma once
#include <cstdint>

namespace brume::emus {

    // the folders of a console's games (`key` = Brume's own name: ps2, gamecube, snes...): every folder of sdmc:/roms/ and of
    // sdmc:/tico/roms/ that people name that way. Any thread (it only lists those two folders). Returns how many (paths in out).
    int  RomDirs(const char *key, char (*out)[96], int max);

    // ---- worker side (Games' count thread, or inline when no thread could start) ----
    void Scan();                                   // fills the result (no allocation kept; stops when a game starts)

    // ---- main thread ----
    void Boot(float now);                          // what was found last time: the wheel as it was, at once
    void Commit(float now, const int *counts);     // the scan is done: the wheel, the notifications, the file (counts: per console, -1 unknown; consoles::kMax of them)
    const char *Path(int console);                 // the emulator found for this console, nullptr: none
    const char *Name(int console);                 // its name (« NetherSX2 », « RetroArch »...), "" none
    // the whole argv to start `rom` with `nro` ("<nro>" "<rom>", or a Tico module's own template when `nro` is that module's):
    // false = too long
    bool Argv(int console, const char *nro, const char *rom, const char *title, char *out, int cap);
    void Arrived(int console, float now);          // the wheel: that console just arrived (its notification)
    inline void (*s_present)(uint16_t mask, uint16_t fresh, float now, bool instant) = nullptr;   // Entry: Wheel::SetPresent
    inline void (*s_log)(const char *) = nullptr;
}
