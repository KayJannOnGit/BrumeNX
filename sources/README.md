# Sources of the third-party binaries shipped in the BrumeNX release

The release zip contains a few programs that are **not built from this repository's own code**. The GPL asks that
whoever distributes them also gives their exact source. This folder says where each one comes from and holds the
changes made to it, so anyone can rebuild the same files.

| File in the zip | What it is | Licence | Exact source |
|---|---|---|---|
| `atmosphere/contents/0100000000001000/exefs/main` (+ `main.npdm`) | the **qlaunch-ext loader** (replaces the HOME menu, loads `brume.dnro`) | GPL-2.0 (qlaunch-ext) + GPL-3.0 (sLaunch's patch, see below) | [Horizon-OC/qlaunch-ext](https://github.com/Horizon-OC/qlaunch-ext) commit `d7306be` (2026-11-05) + `qlaunch-ext/loader-brumenx.patch` (`git apply` at the root of qlaunch-ext) |
| `qlaunch-ext/bin/nroloader.nsp` | qlaunch-ext's NRO loader | GPL-2.0 | same commit, `plugins/` (the patch above only changes its Makefile's include path) |
| `lib/sdl3.dnro` | SDL 3 as a qlaunch-ext module | zlib | qlaunch-ext's `extern/SDL-dlink` at `92edf49` + `qlaunch-ext/sdl-slaunch.patch` (from sLaunch) |
| `lib/neko3d.dnro` | deko3d / neko3d (fincs) | zlib | inside `extern/SDL-dlink/extern/neko3d` (same commit) |
| `lib/ffmpeg.dnro` | FFmpeg, configured by `build-ffmpeg.sh` (with `--enable-gpl`) | GPL-2.0-or-later | qlaunch-ext's `extern/FFmpeg-dlink` at `a86b496` (n7.1-dev-2772), configure line in `build-ffmpeg.sh` + the patch above (adds `--enable-protocol=file`) |
| `lib/json.dnro`, `lib/miniini.dnro`, `lib/xxhash.dnro` | nlohmann/json, miniini, xxHash as modules | MIT / Apache-2.0 / BSD-2-Clause | qlaunch-ext's `lib/*-module` and `extern/` (same commit) |
| (inside every module) | libnx-dlink | Unlicense | qlaunch-ext's `extern/libnx-dlink` at `9daa454` |
| `brume/bin/hbloader_app/main` | nx-hbloader as modified by sLaunch (starts a `.nro` as an application) | ISC + GPL-3.0 | [etonedemid/slaunch](https://github.com/etonedemid/slaunch) commit `1c86996` (2026-11-15), `projects/hbloader/` (unchanged) |
| `atmosphere/contents/420000000042524D/exefs.nsp` | **Brume Musique** (Spotify background service) | GPL-3.0-or-later | this repository: `musique/` (Switify's engine, changes in `musique/switify/MODIFICATIONS-BRUME.txt`) |

Build chain: devkitPro (devkitA64, libnx **master** — the loader uses NACP fields the packaged libnx 4.12 lacks),
`dekotools` for neko3d's headers. The order and commands are those of sLaunch's `scripts/build-qext.sh` (commit
above) followed by this repository's `scripts/build-loader.sh`.

## What the loader patch contains

`qlaunch-ext/loader-brumenx.patch` = sLaunch's `scripts/qext/loader.patch` (by etonedemid, GPL-3.0: a separate arena
for big allocations, `arena_core.h`, `qext.cpp` fixes, the extra menu imports `QEXT_EXTRA_UNDEF`, `--enable-protocol=file`
for FFmpeg) **plus** BrumeNX's own change made by `scripts/build-loader.sh` (v42.4: the loader no longer keeps the
`avm` service open, so installers such as sphaira do not fail with 2021-0003).

## Licence note (open question, see docs/RELEASE-CHECKLIST.md)

qlaunch-ext's files say « Licensed under the GPLv2 » without « or any later version », and sLaunch's patch to it is
published under the GPL-3.0. BrumeNX (GPL-3.0) is a module loaded into the same process. Whether these can be combined
depends on the qlaunch-ext authors' intent (they publish the module API so that other people write menus). This is the
same situation as sLaunch's own releases; it is written down here so that it is clear, and the question to the authors
is listed in the release checklist.
