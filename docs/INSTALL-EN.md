# Brume beta — install guide

Brume replaces the Switch HOME menu with our own: a blue fog that breathes, water cubes, a 3D wheel of consoles,
your games in real boxes, a sound and a vibration for every gesture.

> ⚠️ Unofficial fan project, no link to Nintendo or Sony. No game, BIOS, key or official cover inside; the consoles' logos (Wikimedia Commons, public domain) are trademarks of their owners.
> This is a **beta**: things will break. You use it at your own risk, like anything CFW.

## 1. What you need

- A Switch running **Atmosphère** on an **emuMMC** (tested: Switch V1 "Erista", Hekate 6.5.4, Atmosphère 1.12.2,
  firmware 22.5.0). Other models (V2, Lite, OLED) and firmwares: **not tested yet**, tell us how it goes.
- An SD card in **FAT32**, and a PC (or Hekate → *USB Tools* → *SD Card* to see the card on the PC).
- Optional: at least **one Switch game installed** (needed to launch emulators, see the full guide).

> 🛑 **Never use Brume on your sysNAND** (the console's own memory, the one you go online with).
> Brume replaces the HOME menu and **writes real system settings** (sleep, TV, language…). Only on an emuMMC.
> Keep your emuMMC cut off from Nintendo's servers (Atmosphère's DNS blocking, `atmosphere/hosts/emummc.txt`).

## 2. Install (5 minutes)

1. Turn the console off and put the SD card in your PC.
2. **Back up** these folders if they exist on the card (another menu such as sLaunch lives there):
   `atmosphere/contents/0100000000001000/`, `qlaunch-ext/` and `lib/`.
3. Unzip `brumenx-….zip` and copy **everything inside it to the root of the SD card** (or let the installer do it: `install-brumenx.sh` on macOS / Linux, `install-brumenx.bat` on Windows; it checks every file and deletes nothing).
   Say yes to *merge folders* / *replace files*.
4. Put the card back, start Atmosphère **on your emuMMC** from Hekate.
5. Brume opens on its lock screen: **press any button 3 times** (or tap the screen 3 times).

Brume follows the console's language (French or English). To force it: **R** to the **Settings** page →
**Brume** → **Brume's language**.

## 3. Play

**Switch games**: they are on the wheel right away and launch with **A**. Their covers download by themselves as soon
as the console is online (Settings > Data > automatic covers, on by default).

**Other consoles** (nothing to configure, Brume finds everything at boot):
1. Put the **emulator** on the card, anywhere in `switch/` (e.g. `switch/NetherSX2/NetherSX2.nro`), or RetroArch with its
   cores in `retroarch/cores/`. Recognised: NetherSX2 / AetherSX2 (PS2), Nezumiiruka / Dolphin (GameCube),
   PPSSPP (PSP), DraStic / melonDS (DS), and the RetroArch cores (N64, PS1, GBA, SNES, NES, Mega Drive, Game Boy,
   Master System, Game Gear).
2. Put **your own games** in `roms/<console>/`: `roms/ps2`, `roms/gc`, `roms/n64`, `roms/psx`, `roms/psp`, `roms/gba`,
   `roms/nds`, `roms/snes`, `roms/nes`, `roms/megadrive`, `roms/gb`, `roms/mastersystem`, `roms/gamegear`.
3. PS1 and PS2 emulators need **your own BIOS** (see each emulator's guide). Brume never ships games, BIOS or keys.
4. Restart the console: each console with games appears on the wheel.

Brume starts emulators with all the memory of a real game by briefly borrowing one of your installed Switch games
(automatic: at least one Switch game must be installed). Optional, once: open `switch/brume-launcher/brume-launcher.nro`
in sphaira and make it a **forwarder** named « Brume Launcher »; Brume then uses it and borrows nothing.

## 4. Go back to your old menu

Pick the first one that works for you:

1. **From Brume**: hold **L + R + −** for **3 seconds**. The console reboots
   — on sLaunch if it is installed, otherwise on the Nintendo HOME menu. (This also turns Brume Overlay off.)
2. **From the PC, only if sLaunch is installed** (the file `qlaunch-ext/menus/menu.dnro` exists):
   delete `qlaunch-ext/config/menu.ini`. The next boot opens sLaunch.
   ⚠️ Without sLaunch, **don't**: the loader would find no menu and the screen stays black. Use option 3.
3. **From the PC, always works (even on a black screen)**: rename the folder
   `atmosphere/contents/0100000000001000` to `0100000000001000_off`. The Nintendo menu comes back.
   If you had turned Brume Overlay on, rename `atmosphere/contents/010000000000100C` to `010000000000100C_off` too.

To bring Brume back: rename the folder back (remove `_off`), or copy `qlaunch-ext/config/menu.ini` from the zip again.

**Uninstall**: delete `atmosphere/contents/0100000000001000/`, `qlaunch-ext/`, `lib/`, `brume/` and
`switch/brume-launcher/`, then put your backup from step 2 back.

## 5. Report a bug

1. Note **what you did, what happened, and the time**.
2. Turn the console off, put the card in the PC and copy these files (those that exist):
   - `brume/perf.log` — Brume's log (it grows at every boot: the end of the file is the latest);
   - `qlaunch-ext-log.txt` (at the root of the card) — the loader's log;
   - the newest files in `atmosphere/crash_reports/`, `atmosphere/fatal_reports/` and `atmosphere/fatal_errors/`
     (crashes, error screens);
   - `brume/overlay/journal.txt` — only if you turned Brume Overlay on;
   - `brume/reglages.ini` — your Brume settings (delete the `codeseq=` line first if you set a lock code).
3. Fill in the template at the end of `BETA-CHECKLIST.md`, add a photo or video of the screen if you can, and post it
   where you got the beta.

The logs can contain your profile name and the names of your game files: have a look before sending them.

More (emulators, native ports, covers, your own sounds): `GUIDE-COMPLET-FR.md` (French).
