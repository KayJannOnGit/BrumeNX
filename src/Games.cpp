// Brume - the games of a console, minimal but really launching. See Games.hpp.
//
// Launching a homebrew emulator with a game, as sLaunch does it (slaunch/projects/sMenu/source/sl/menu/qext/
// QextBackend.cpp, LaunchHomebrewApp, GPL-3, read and rewritten here): an installed game is borrowed (the
// « donor »), its code is redirected to hbloader_app for this one launch (atmosphere/contents/<donor>/exefs),
// and hbloader reads which .nro to start and with which argv from sdmc:/slaunch/hbtarget.txt (our hbloader
// fork, projects/hbloader). The emulator then runs in APPLICATION mode (all the memory: PS2 / GameCube need it).
// As soon as the menu is back, the redirect is removed, so the donor boots as itself again.
#include "Games.hpp"
#include "Covers.hpp"
#include "Lang.hpp"
#include "Wheel.hpp"
#include "Mesh3d.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Profiles.hpp"
#include "Nextendo.hpp"
#include "Settings.hpp"
#include "Popup.hpp"
#include "Busy.hpp"
#include "Emus.hpp"
#include "Consoles.hpp"
#include "Worker.hpp"
#include <cstring>
#include "Audio.hpp"
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {   // the loader's exports (qlaunch-ext loader + sLaunch's loader.patch)
    int    qext_refresh_titles(void);
    int    qext_title_count(void);
    u64    qext_title_id(int index);
    int    qext_title_name(int index, char *out, unsigned cap);
    int    qext_title_is_virtual(u64 tid);
    Result qext_launch_title(u64 tid);
    Result qext_launch_title_as(u64 tid, AccountUid uid);
    Result qext_terminate_game(void);
    int    qext_game_running(void);
    Result qext_resume_game(void);           // v78: the suspended game comes back (as the Switch does when you choose it again)
    u64    qext_suspended_title(void);       // v78: which one is suspended (0: none)
    int    qext_game_has_foreground(void);
}

namespace brume {

    void (*Games::s_log)(const char *) = nullptr;

    namespace {
        // One entry per console of the wheel (mockup CONSOLES order): name, roms folder, emulator, extensions.
        // Emulators and their launch method: docs/VERIFICATION-EMULATEURS.md (RetroArch: always the renamed
        // core with the game, never RetroArch relaunching itself - it crashes on exit on FW 22.5).
        // v93: built from THE table (Consoles.hpp): name, games' folder (Brume's key), extensions
        struct Console { const char *name, *folder; const char *ext[8]; };
        struct ConsoleList { Console c[consoles::kMax]; };
        constexpr ConsoleList MakeConsoles() {
            ConsoleList l{};
            for (int i = 0; i < consoles::kCount; i++) {
                l.c[i].name = consoles::kDefs[i].name;
                l.c[i].folder = consoles::Emulated(i) ? consoles::kDefs[i].key : nullptr;
                for (int k = 0; k < 8; k++) l.c[i].ext[k] = consoles::kDefs[i].ext[k];
            }
            return l;
        }
        constexpr ConsoleList kConsoleList = MakeConsoles();
        constexpr const Console *kConsoles = kConsoleList.c;
        // ---- v54: the emulators, configurable (sdmc:/brume/emulateurs.ini: « ps2=<nro>;<another nro> », one console a
        // line, the first one that exists wins), then our built-in list (the second ones are other known emulators) ----
        constexpr const char *kEmuIni = "sdmc:/brume/emulateurs.ini";
        const char *const kEmuDefaults[consoles::kMax][3] = {   // (v93: the new consoles: only what Brume finds)
            { nullptr, nullptr, nullptr },
            { "sdmc:/switch/NetherSX2.nro", "sdmc:/switch/nethersx2/NetherSX2.nro", nullptr },
            { "sdmc:/switch/nezumiiruka/nezumiiruka.nro", "sdmc:/switch/dolphin-nx/dolphin.nro", nullptr },
            { "sdmc:/switch/retroarch-n64/retroarch-n64.nro", nullptr, nullptr },
            { "sdmc:/switch/retroarch-ps1/retroarch-ps1.nro", nullptr, nullptr },
            { "sdmc:/switch/ppsspp-nx/PPSSPP.nro", "sdmc:/switch/ppsspp/PPSSPP.nro", nullptr },
            { "sdmc:/switch/retroarch-gba/retroarch-gba.nro", "sdmc:/switch/mgba/mgba.nro", "sdmc:/switch/mGBA/mgba.nro" },
            { nullptr, nullptr, nullptr },
        };
        char s_emuIni[consoles::kMax][300];                          // what emulateurs.ini says for each console ("" = nothing)
        char s_emuChosen[300];
        void EmuIniLoad() {
            for (auto &e : s_emuIni) e[0] = 0;
            FILE *f = fopen(kEmuIni, "r");
            if (!f) {                                   // first time: our template, so it can be edited by hand
                if (FILE *w = fopen(kEmuIni, "w")) {
                    fputs("# Brume - les emulateurs de chaque console (le premier qui existe sur la carte gagne).\n"
                          "# Une ligne par console : <console>=<chemin du .nro>;<un autre .nro>   (# = commentaire)\n"
                          "# Rien a faire d'habitude : Brume trouve tout seul les emulateurs installes dans switch/ (v90).\n"
                          "# Une ligne ici impose ce choix-la.\n"
                          "#ps2=sdmc:/switch/NetherSX2.nro\n#gamecube=sdmc:/switch/nezumiiruka/nezumiiruka.nro;sdmc:/switch/dolphin-nx/dolphin.nro\n"
                          "#n64=sdmc:/switch/retroarch-n64/retroarch-n64.nro\n#ps1=sdmc:/switch/retroarch-ps1/retroarch-ps1.nro\n"
                          "#psp=sdmc:/switch/ppsspp-nx/PPSSPP.nro\n#gba=sdmc:/switch/retroarch-gba/retroarch-gba.nro;sdmc:/switch/mgba/mgba.nro\n"
                          "# Aussi : ds, snes, nes, megadrive, gb, mastersystem, gamegear\n", w);
                    fclose(w);
                }
                return;
            }
            char line[360];
            while (fgets(line, sizeof line, f)) {
                if (line[0] == '#') continue;
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                for (char *p = eq + 1; *p; p++) if (*p == '\r' || *p == '\n') *p = 0;
                for (int k = 0; k < consoles::kCount; k++) if (consoles::Emulated(k) && !strcasecmp(line, consoles::kDefs[k].key)) snprintf(s_emuIni[k], sizeof s_emuIni[k], "%s", eq + 1);
            }
            fclose(f);
        }
        const char *EmulatorFor(int console) {
            if (!consoles::Emulated(console)) return nullptr;
            char tmp[300];
            snprintf(tmp, sizeof tmp, "%s", s_emuIni[console]);
            for (char *tok = tmp; tok && *tok;) {           // the user's list first
                char *semi = strchr(tok, ';');
                if (semi) *semi = 0;
                while (*tok == ' ') tok++;
                if (*tok && !strchr(tok, '"')) {
                    struct stat st;
                    if (stat(tok, &st) == 0) { snprintf(s_emuChosen, sizeof s_emuChosen, "%s", tok); return s_emuChosen; }
                }
                tok = semi ? semi + 1 : nullptr;
            }
            if (const char *found = emus::Path(console)) {  // v90: then the one Brume found by itself (Emus.cpp)
                struct stat st;
                if (stat(found, &st) == 0) { snprintf(s_emuChosen, sizeof s_emuChosen, "%s", found); return s_emuChosen; }
            }
            for (const char *d : kEmuDefaults[console]) {   // then ours
                struct stat st;
                if (d && stat(d, &st) == 0) return d;
            }
            return nullptr;
        }

        constexpr const char *kHbTarget   = "sdmc:/slaunch/hbtarget.txt";      // read by our hbloader fork
        constexpr const char *kDonorArmed = "sdmc:/brume/donneur-arme.txt";    // a redirect to remove
        // v58 - a forwarder told what to start: Atmosphère's LayeredFS replaces the two files of its romfs that say which
        // .nro to load and with which argv (nextNroPath / nextArgv, the layout of sphaira's forwarders, owo.cpp, GPL-3)
        constexpr const char *kFwArmed    = "sdmc:/brume/forwarder-arme.txt";  // the forwarder whose romfs is redirected
        constexpr const char *kLanceur    = "Brume Launcher";                  // our own forwarder (tools/launcher, made once with sphaira;
                                                                               // Yass: « Brume Launcher » whatever the language)
        constexpr const char *kLanceurOld = "Brume Lanceur";                   // (its first name, v58: a forwarder made then still works)
        void FwDir(u64 tid, char *out, int cap, const char *sub) { snprintf(out, cap, "sdmc:/atmosphere/contents/%016llX%s", (unsigned long long)tid, sub); }
        bool Gone(const char *p) { struct stat st; return remove(p) == 0 || stat(p, &st) != 0; }   // removed, or not there
        constexpr const char *kDonorFile  = "sdmc:/brume/donneur.txt";         // the borrowed game (hex id)
        // hbloader_app: our own copy first, sLaunch's otherwise.
        constexpr const char *kHblDirs[2] = { "sdmc:/brume/bin/hbloader_app", "sdmc:/slaunch/bin/hbloader_app" };

        // v89 - the game whose disc / cartridge is in its console: launched from here and still running (suspended with HOME).
        // It outlives the games screen and sleep; only the loader knows whether it still runs (asked once a frame, Update), and
        // any other launch that goes well (the Apps page, JKSV...) ends it (the loader closes the running one first)
        int  s_heldCon = -1;                       // its console (wheel index), -1: none
        u64  s_heldGame = 0, s_heldTid = 0;        // its Switch title id (0: an emulated game, a port) / what the loader runs for it
        char s_heldPath[300], s_heldTitle[96];     // (an emulated game, a port: its file - its name for the popup)
        unsigned s_launchGen = 0, s_heldGen = 0;   // every launch that went well counts one
        bool s_heldLive = false;                   // this frame: it still runs

        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }

        // ---- v42: the native ports (the wheel's « Ports natifs ») ----
        // A port = an .nro in sdmc:/switch/<folder>/ when the folder is listed in sdmc:/brume/ports.txt (one folder name per
        // line, # = a comment) or has a game folder of its own (« game », « game-<region> », the mcdub3-nx way). Its name is
        // its NACP's; its data: « mcdub3-nx-eur.nro » -> « game-eur » (or « game »). When one version has its data, the
        // versions without are hidden; when none has, all are shown and A says where the files go.
        constexpr const char *kPortsList = "sdmc:/brume/ports.txt";
        bool EndsNro(const char *s) { const size_t n = strlen(s); return n > 4 && strcasecmp(s + n - 4, ".nro") == 0; }
        bool IsDir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
        bool DirHasFiles(const char *dir) {
            DIR *d = opendir(dir);
            if (!d) return false;
            bool any = false;
            while (struct dirent *e = readdir(d)) if (e->d_name[0] != '.') { any = true; break; }
            closedir(d);
            return any;
        }
        bool Listed(const char *list, const char *name) {          // ports.txt: one folder name per line
            const size_t n = strlen(name);
            for (const char *p = list; *p; ) {
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                const char *e = p;
                while (*e && *e != '\n' && *e != '\r') e++;
                const char *t = e;
                for (const char *q = p; q < e; q++) if (*q == '=') { t = q; break; }   // v61: « <folder> = <system> »
                while (t > p && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '/')) t--;
                if (*p != '#' && (size_t)(t - p) == n && strncasecmp(p, name, n) == 0) return true;
                p = e;
            }
            return false;
        }
        // v61 (Reddit: « add Xbox, Xbox 360, Wii, PC... for the games ported to Switch ») - a port's original system:
        // what ports.txt says after « = », else the ports we know (by the start of their folder's name)
        struct PortSys { const char *prefix, *sys; };
        const PortSys kPortSys[] = {
            { "mcdub3", "Xbox" }, { "nfsmw", "Xbox 360" }, { "nfscx", "Xbox" }, { "nfsu2x", "Xbox" },
            { "sm64", "Nintendo 64" }, { "soh", "Nintendo 64" }, { "shipwright", "Nintendo 64" }, { "2ship", "Nintendo 64" },
            { "starship", "Nintendo 64" }, { "zelda3", "Super Nintendo" }, { "smw", "Super Nintendo" },
            { "re3", "PC" }, { "revc", "PC" }, { "srb2", "PC" }, { "sonic3air", "Mega Drive" } };
        void PortSystem(const char *list, const char *folder, char *out, int cap) {
            out[0] = 0;
            const size_t n = strlen(folder);
            for (const char *p = list; *p; ) {
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                const char *e = p;
                while (*e && *e != '\n' && *e != '\r') e++;
                const char *eq = nullptr;
                for (const char *q = p; q < e; q++) if (*q == '=') { eq = q; break; }
                if (*p != '#' && eq) {
                    const char *t = eq;
                    while (t > p && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '/')) t--;
                    if ((size_t)(t - p) == n && strncasecmp(p, folder, n) == 0) {
                        const char *v = eq + 1;
                        while (v < e && (*v == ' ' || *v == '\t')) v++;
                        const char *w = e;
                        while (w > v && (w[-1] == ' ' || w[-1] == '\t')) w--;
                        const int len = (int)(w - v) < cap - 1 ? (int)(w - v) : cap - 1;
                        if (len > 0) { memcpy(out, v, len); out[len] = 0; return; }
                    }
                }
                p = e;
            }
            for (const PortSys &k : kPortSys)
                if (strncasecmp(folder, k.prefix, strlen(k.prefix)) == 0) { snprintf(out, cap, "%s", k.sys); return; }
        }

        // its NACP name (French, then American English, then the first one there); false = none
        bool NroTitle(const char *path, char *out, int cap) {
            static uint8_t nacp[0x3000];
            FILE *f = fopen(path, "rb");
            if (!f) return false;
            uint8_t hdr[0x80], aset[0x38];
            bool ok = false;
            uint32_t size = 0;
            if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr + 0x10, "NRO0", 4) == 0) {
                memcpy(&size, hdr + 0x18, 4);
                if (size >= 0x80 && fseek(f, (long)size, SEEK_SET) == 0 && fread(aset, 1, sizeof aset, f) == sizeof aset && memcmp(aset, "ASET", 4) == 0) {
                    uint64_t no = 0, ns = 0;
                    memcpy(&no, aset + 0x18, 8); memcpy(&ns, aset + 0x20, 8);
                    ok = ns >= sizeof nacp && no < 0x10000000ull && fseek(f, (long)(size + no), SEEK_SET) == 0 && fread(nacp, 1, sizeof nacp, f) == sizeof nacp;
                }
            }
            fclose(f);
            if (!ok) return false;
            int pick = nacp[3 * 0x300] ? 3 : (nacp[0] ? 0 : -1);
            for (int l = 0; l < 16 && pick < 0; l++) if (nacp[l * 0x300]) pick = l;
            if (pick < 0) return false;
            int n = 0;
            while (n < 0x1ff && n + 1 < cap && nacp[pick * 0x300 + n]) { out[n] = (char)nacp[pick * 0x300 + n]; n++; }
            out[n] = 0;
            return n > 0;
        }
        // « Midnight Club 3: DUB Edition (Europe) » -> « Midnight Club 3: DUB Edition » (the region tags at the end)
        void StripTags(char *s) {
            for (;;) {
                int n = (int)strlen(s);
                while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
                if (n < 3 || s[n - 1] != ')') return;
                char *o = strrchr(s, '(');
                if (!o || o == s) return;
                *o = 0;
            }
        }
        // the folder its data goes to: « <dir>/mcdub3-nx-eur.nro » -> « <dir>/game-eur », else « <dir>/game »; "" = none
        void DataDir(const char *nro, char *out, int cap) {
            out[0] = 0;
            char dir[300];
            snprintf(dir, sizeof dir, "%s", nro);
            char *slash = strrchr(dir, '/');
            if (!slash) return;
            *slash = 0;
            const char *file = slash + 1;
            const char *dash = strrchr(file, '-'), *dot = strrchr(file, '.');
            if (dash && dot && dot > dash + 1 && dot - dash < 12) {
                char p[340];
                snprintf(p, sizeof p, "%s/game-%.*s", dir, (int)(dot - dash - 1), dash + 1);
                if (IsDir(p)) { snprintf(out, cap, "%s", p); return; }
            }
            static const char *const kNames[2] = { "game", "game_root" };
            for (const char *name : kNames) {   // v42.1: « game_root » = nfsmw-nx (Need for Speed: Most Wanted)
                char p[340];
                snprintf(p, sizeof p, "%s/%s", dir, name);
                if (IsDir(p)) { snprintf(out, cap, "%s", p); return; }
            }
        }

        // v32: the case of each console's games (0 DVD, 1 jewel, 2 UMD, 3 Switch, 4 cardboard - kBox below)
        int kBoxKind(int console) { return consoles::Valid(console) ? consoles::kDefs[console].box : 0; }   // v93: the table
        // v33: the media's journey (box -> console) lasts kJourney s and starts when the case is open
        constexpr float kJourney = 2.4f;                    // v35: the flight + the slide into the slot (slower)
        float JourneyStart(int console) { return kBoxKind(console) == 4 ? 0.6f : 1.0f; }
        constexpr float kEjectLead = 0.55f;                 // v89: the box comes for its disc (it rises from its place) before it leaves the console
        constexpr float kHeldOpen = 1.95f;                  // v89: the held box's lid at its place, when chosen (radians, as the staging opens it)
        bool HeldSlot(int console, float *out);             // v89: that console's slot (its model's space), for the wheel's light - below
        float EaseOut3(float p) { p = Clamp01(p); return 1 - powf(1 - p, 3); }
        bool  Exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

        void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
        void Log(const char *fmt, ...) {
            if (!Games::s_log) return;
            char buf[400];
            va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
            Games::s_log(buf);
        }

        bool CopyFile(const char *from, const char *to) {
            FILE *in = fopen(from, "rb");
            if (!in) return false;
            FILE *out = fopen(to, "wb");
            if (!out) { fclose(in); return false; }
            static char buf[0x8000];
            bool ok = true;
            size_t n;
            while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
            fclose(in);
            if (fclose(out) != 0) ok = false;
            return ok;
        }

        void DonorDir(u64 donor, char *out, size_t cap, const char *sub) {
            snprintf(out, cap, "sdmc:/atmosphere/contents/%016llX%s", (unsigned long long)donor, sub);
        }

        // « [PS2](2001) SSX Tricky.iso » -> « SSX Tricky » ; « Sonic Adventure 2 - Battle (USA) (En,Ja).iso »
        // -> « Sonic Adventure 2 - Battle »: no extension, no (...) / [...] tags, single spaces.
        void CleanTitle(const char *file, char *out, int cap) {
            char tmp[300];
            snprintf(tmp, sizeof tmp, "%s", file);
            if (char *dot = strrchr(tmp, '.')) *dot = 0;
            int n = 0, depth = 0;
            bool space = true;
            for (const char *p = tmp; *p && n < cap - 1; p++) {
                if (*p == '(' || *p == '[') { depth++; continue; }
                if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
                if (depth) continue;
                if (*p == '_' || *p == ' ') { if (!space) { out[n++] = ' '; space = true; } continue; }
                out[n++] = *p; space = false;
            }
            while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '-')) n--;
            out[n] = 0;
            if (!out[0]) snprintf(out, cap, "%s", tmp);   // nothing left: keep the file name
        }

        bool HasExt(const char *name, const Console &c) {
            const char *dot = strrchr(name, '.');
            if (!dot) return false;
            for (const char *e : c.ext) {
                if (!e) break;
                if (strcasecmp(dot, e) == 0) return true;
            }
            return false;
        }

        // ---- v66: how many games each console has - the SAME rules as Games::Scan (what the boxes will show), but only
        // the folders are listed (no file is opened). Switch on the main thread (the loader's list), the others in a
        // small background thread at boot and when the menu comes back; a game starting stops it.
        int CountEmulated(const Console &c) {
            char dirs[4][96];                                       // v90: every folder people name that way (ps2, PS2, playstation2...; v93: + Tico's)
            const int nd = emus::RomDirs(c.folder, dirs, 4);
            int n = 0;
            for (int k = 0; k < nd; k++) {
                const char *dir = dirs[k];
                DIR *d = opendir(dir);
                if (!d) continue;
                while (struct dirent *e = readdir(d)) {
                    if (n >= Games::kMaxGames || qext_game_running()) break;   // v68
                    if (e->d_name[0] == '.' || !HasExt(e->d_name, c) || strchr(e->d_name, '"')) continue;
                    if (strlen(dir) + 1 + strlen(e->d_name) >= 300) continue;           // (Scan: must fit its path)
                    n++;
                }
                closedir(d);
            }
            return n;
        }
        int CountPorts() {
            static char list[2048];
            list[0] = 0;
            if (FILE *fp = fopen(kPortsList, "r")) { const size_t r = fread(list, 1, sizeof list - 1, fp); list[r] = 0; fclose(fp); }
            DIR *d = opendir("sdmc:/switch");
            if (!d) return 0;
            int total = 0;
            while (struct dirent *e = readdir(d)) {
                if (total >= Games::kMaxGames || qext_game_running()) break;
                if (e->d_name[0] == '.') continue;
                char dir[160];
                if (snprintf(dir, sizeof dir, "sdmc:/switch/%s", e->d_name) >= (int)sizeof dir) continue;
                DIR *sub = opendir(dir);
                if (!sub) continue;
                char nro[4][96]; int nn = 0; bool gameDir = false;
                while (struct dirent *s = readdir(sub)) {
                    if (s->d_name[0] == '.') continue;
                    if (EndsNro(s->d_name)) { if (nn < 4 && strlen(s->d_name) < sizeof nro[0] && !strchr(s->d_name, '"')) snprintf(nro[nn++], sizeof nro[0], "%s", s->d_name); }
                    else if (strncasecmp(s->d_name, "game", 4) == 0 && (s->d_name[4] == 0 || s->d_name[4] == '-' || s->d_name[4] == '_')) gameDir = true;
                }
                closedir(sub);
                if (!nn || !(gameDir || Listed(list, e->d_name))) continue;
                int nready = 0;
                for (int k = 0; k < nn; k++) {
                    char p[300], data[300];
                    snprintf(p, sizeof p, "%s/%s", dir, nro[k]);
                    DataDir(p, data, sizeof data);
                    nready += !data[0] || DirHasFiles(data);
                }
                total += nready ? nready : nn;                          // (Scan: the ready versions, else all of them)
            }
            closedir(d);
            return total;
        }
        // ---- v67: « Gérer » - the sizes and the removal, the same for every console. Switch: the system (ns); emulated: the
        // file (with its tracks for a .cue / .gdi / .m3u); a port: its .nro and its game folder. Only under sdmc:/roms/ and
        // sdmc:/switch/<folder>/ (checked), in a background thread; a game starting stops the measuring.
        void SizeStr(u64 b, char *out, int cap) {
            if (b >= 1000000000ull) snprintf(out, cap, lang::T("%.1f Go"), (double)b / 1e9);
            else snprintf(out, cap, lang::T("%.0f Mo"), (double)b / 1e6);
            if (!lang::En()) for (char *p = out; *p; p++) if (*p == '.') *p = ',';
        }
        u64 FileSize(const char *p) { struct stat st; return stat(p, &st) == 0 && !S_ISDIR(st.st_mode) ? (u64)st.st_size : 0; }
        volatile bool s_mgAbort = false;
        u64 TreeSize(const char *dir, int depth, int &budget) {
            if (depth > 12 || budget <= 0 || s_mgAbort || qext_game_running()) return 0;
            DIR *d = opendir(dir);
            if (!d) return 0;
            u64 total = 0;
            char p[512];
            while (struct dirent *e = readdir(d)) {
                if (--budget <= 0 || s_mgAbort || qext_game_running()) break;   // v68: checked at every entry
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
                struct stat st;
                if (stat(p, &st) != 0) continue;
                total += S_ISDIR(st.st_mode) ? TreeSize(p, depth + 1, budget) : (u64)st.st_size;
            }
            closedir(d);
            return total;
        }
        bool TreeRemove(const char *dir, int depth) {
            if (depth > 12) return false;
            DIR *d = opendir(dir);
            if (!d) return false;
            bool ok = true;
            char p[512];
            while (struct dirent *e = readdir(d)) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) { ok = false; continue; }
                struct stat st;
                if (stat(p, &st) != 0) continue;
                ok = (S_ISDIR(st.st_mode) ? TreeRemove(p, depth + 1) : remove(p) == 0) && ok;
            }
            closedir(d);
            return rmdir(dir) == 0 && ok;
        }
        // where Brume may delete: under root, at least `depth` folders down (1 = inside one of its folders), no « .. »
        bool SafeUnder(const char *p, const char *root, int depth) {
            const size_t n = strlen(root);
            if (strncmp(p, root, n) || strstr(p, "/..") || strstr(p, "/./")) return false;
            int slashes = 0;
            for (const char *q = p + n; *q; q++) slashes += *q == '/';
            return slashes >= depth;
        }
        // a .cue / .gdi / .m3u lists other files (its tracks, its discs): exactly those, READ from it (v67.1: never « the files
        // whose name starts the same way », which could be another version of the game). Only plain names in the same
        // folder (no « / », no « .. »); an .m3u's discs bring their own tracks (one level).
        bool MultiFile(const char *name) { const char *d = strrchr(name, '.'); return d && (!strcasecmp(d, ".cue") || !strcasecmp(d, ".gdi") || !strcasecmp(d, ".m3u")); }
        int ListedFiles(const char *path, char (*out)[160], int cap, int n) {   // adds the files `path` lists to out (no duplicates)
            const char *slash = strrchr(path, '/'), *dot = strrchr(path, '.');
            if (!slash || !dot) return n;
            FILE *f = fopen(path, "rb");
            if (!f) return n;
            constexpr size_t kBuf = 16384;                    // (only while it is read: no memory kept)
            char *buf = (char *)malloc(kBuf);
            if (!buf) { fclose(f); return n; }
            const size_t r = fread(buf, 1, kBuf - 1, f);
            fclose(f);
            buf[r] = 0;
            const bool cue = !strcasecmp(dot, ".cue"), gdi = !strcasecmp(dot, ".gdi");
            int line = 0;
            for (char *p = buf; *p && n < cap; line++) {
                char *e = p;
                while (*e && *e != '\n' && *e != '\r') e++;
                const char save = *e; *e = 0;
                char name[160]; name[0] = 0;
                char *q = p;
                while (*q == ' ' || *q == '\t') q++;
                if (cue) {                                // FILE "Game (Track 1).bin" BINARY
                    if (!strncasecmp(q, "FILE", 4) && (q[4] == ' ' || q[4] == '\t')) {
                        q += 5; while (*q == ' ' || *q == '\t') q++;
                        if (*q == '"') { char *z = strchr(q + 1, '"'); if (z) snprintf(name, sizeof name, "%.*s", (int)(z - q - 1), q + 1); }
                        else { char *z = strrchr(q, ' '); if (z && z > q) snprintf(name, sizeof name, "%.*s", (int)(z - q), q); }
                    }
                } else if (gdi) {                         // (first line: the count) N LBA TYPE SIZE "name" OFFSET
                    if (line > 0 && *q) {
                        int tok = 0;
                        while (*q && tok < 4) { while (*q && *q != ' ' && *q != '\t') q++; while (*q == ' ' || *q == '\t') q++; tok++; }
                        if (*q == '"') { char *z = strchr(q + 1, '"'); if (z) snprintf(name, sizeof name, "%.*s", (int)(z - q - 1), q + 1); }
                        else { char *z = q; while (*z && *z != ' ' && *z != '\t') z++; snprintf(name, sizeof name, "%.*s", (int)(z - q), q); }
                    }
                } else if (*q && *q != '#') snprintf(name, sizeof name, "%s", q);   // .m3u: one disc a line
                *e = save;
                p = *e ? e + 1 : e;
                if (!name[0] || strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) continue;
                bool dup = false;
                for (int k = 0; k < n; k++) dup = dup || !strcmp(out[k], name);
                if (dup) continue;
                snprintf(out[n++], 160, "%s", name);
            }
            free(buf);
            return n;
        }
        u64 PartsDo(const char *path, bool remove_) {          // the file and the files it lists: their size, or remove them (0 = ok)
            u64 total = FileSize(path);
            const char *slash = strrchr(path, '/');
            if (slash && MultiFile(slash + 1)) {
                char (*files)[160] = (char (*)[160])malloc(64 * 160);   // (only while it is done)
                if (!files) return remove_ ? 1 : total;
                char dir[300], p[512];
                snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
                int n = ListedFiles(path, files, 64, 0);
                const int direct = n;
                for (int k = 0; k < direct; k++)          // an .m3u's discs: their tracks too
                    if (MultiFile(files[k]) && snprintf(p, sizeof p, "%s/%s", dir, files[k]) < (int)sizeof p) n = ListedFiles(p, files, 64, n);
                for (int k = 0; k < n; k++) {
                    if (snprintf(p, sizeof p, "%s/%s", dir, files[k]) >= (int)sizeof p || !strcmp(p, path)) continue;
                    if (remove_) remove(p); else total += FileSize(p);
                }
                free(files);
            }
            if (remove_) return remove(path) == 0 ? 0 : 1;
            return total;
        }
        void SwitchSize(u64 tid, u64 &g, u64 &u, u64 &a, uint8_t &where) {   // ns ApplicationOccupiedSize: 4 x { storage, game, update, aoc }
            NsApplicationOccupiedSize o;
            memset(&o, 0, sizeof o);
            g = u = a = 0; where = 0;
            if (R_FAILED(nsCalculateApplicationOccupiedSize(tid, &o))) return;
            for (int k = 0; k < 4; k++) {
                const u8 *e = o.unk_x0 + k * 0x20;
                u64 x, y, z;
                memcpy(&x, e + 8, 8); memcpy(&y, e + 16, 8); memcpy(&z, e + 24, 8);
                if (x > (1ull << 42) || y > (1ull << 42) || z > (1ull << 42) || !(x | y | z)) continue;
                g += x; u += y; a += z;
                if (e[0] == NcmStorageId_BuiltInUser) where |= 1;
                if (e[0] == NcmStorageId_SdCard) where |= 2;
            }
        }
        enum { kJobSizes = 1, kJobArchive = 2, kJobDelete = 3 };
        Thread s_mgThread;
        volatile bool s_mgRunning = false, s_mgDone = false;
        int s_mgKind = 0, s_mgIdx = -1;
        unsigned s_mgGen = 0;                               // v68: the list the job belongs to (a newer list ignores its end)
        u64 s_mgTid = 0;                                    // v67.1: the game of an archive / delete job, copied when it starts
        char s_mgPath[300] = {};
        Result s_mgRc = 0;
        u64 s_mgFreed = 0;

        Thread s_cntThread;
        volatile bool s_cntRunning = false, s_cntDone = false;
        bool s_cntInline = false;                           // v90: no thread could start - it was done right there (the PC preview)
        int s_cntResult[consoles::kMax];
        void CountWork(void *) {
            for (int k = 1; k < consoles::kCount; k++) {
                if (qext_game_running()) { s_cntResult[k] = -1; continue; }
                s_cntResult[k] = k == consoles::kPorts ? CountPorts() : (kConsoles[k].folder ? CountEmulated(kConsoles[k]) : 0);
            }
            emus::Scan();                                           // v90: the emulators installed (folder listings, a name once per .nro)
            s_cntDone = true;
        }

        // The game to borrow: the one used last time if it still has no override, else the first
        // installed (non-virtual) game whose code is not already redirected by something else.
        u64 PickDonor() {
            u64 id = 0;
            if (FILE *fp = fopen(kDonorFile, "r")) {
                char line[32] = {};
                if (fgets(line, sizeof line, fp)) id = strtoull(line, nullptr, 16);
                fclose(fp);
            }
            char dir[128];
            if (qext_title_count() <= 0) qext_refresh_titles();   // v42.3: Apps right after boot - the list was never read yet
            const int n = qext_title_count();
            bool installed = false;
            for (int i = 0; i < n && id; i++) if (qext_title_id(i) == id) installed = true;
            if (id && installed) { DonorDir(id, dir, sizeof dir, "/exefs"); if (!Exists(dir)) return id; }
            for (int i = 0; i < n; i++) {
                const u64 t = qext_title_id(i);
                if (!t || qext_title_is_virtual(t) || Games::IsForwarder(t)) continue;   // v51: never a forwarder as donor
                DonorDir(t, dir, sizeof dir, "/exefs");
                if (Exists(dir)) continue;
                if (FILE *fp = fopen(kDonorFile, "w")) { fprintf(fp, "%016llX\n", (unsigned long long)t); fclose(fp); }
                return t;
            }
            return 0;
        }

        // The profile the game runs as. A game opens ITS user's save at boot: with no valid user it
        // aborts (crash 2002-6400, first console test). Brume never opens a user session, so « last
        // opened » can be empty: then the first profile of the console. (A picker for several
        // profiles can come later, like sLaunch's PickUser.)
        AccountUid s_launchUid = {};                 // v46: picked for this launch (Réglages > Comptes > choisir à chaque jeu)
        AccountUid Player() {
            if (accountUidIsValid(&s_launchUid)) return s_launchUid;
            AccountUid uid = Profiles::Active();         // v46: the active profile (the bar's pill)
            if (accountUidIsValid(&uid)) return uid;
            if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid)) return uid;
            AccountUid all[ACC_USER_LIST_SIZE];
            s32 n = 0;
            if (R_SUCCEEDED(accountListAllUsers(all, ACC_USER_LIST_SIZE, &n)))
                for (int i = 0; i < n; i++) if (accountUidIsValid(&all[i])) return all[i];
            return uid;
        }

        // The donor's own boot modules must not load: its rtld would try to link hbloader as a part
        // of the game (crash 2168-0002 in nnrtld, first console test). Atmosphère's loader skips any
        // NSO with a <name>.stub file in the override folder; sdk is refused without rtld, so all go.
        const char *const kStubs[] = { "rtld", "sdk", "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4",
                                       "subsdk5", "subsdk6", "subsdk7", "subsdk8", "subsdk9" };
    }

    namespace { u64 s_cntAt = 0; }                                  // v80: when the deferred count starts (ticks), 0: none
    void Games::RefreshCounts(float delay) {
        int n = 0;                                                  // Switch: the loader's list, as Scan reads it
        if (qext_title_count() <= 0) qext_refresh_titles();         // v68 (review): at boot it was never read yet (it said « aucun jeu »)
        const int t = qext_title_count();
        for (int i = 0; i < t && n < kMaxGames; i++) { const u64 id = qext_title_id(i); if (id && !IsForwarder(id)) n++; }
        Wheel::s_count[0] = n;
        // v80: back from a game the card is read later - a thread walking the SD card's folders during the way back's
        // staging took the file system from under it (one of its stutters)
        if (delay > 0) { s_cntAt = armGetSystemTick() + (u64)(delay * (float)armGetSystemTickFreq()); return; }
        s_cntAt = 0;
        if (s_cntRunning) return;                                   // (the one running will do)
        s_cntDone = false;
        if (R_SUCCEEDED(threadCreate(&s_cntThread, CountWork, nullptr, nullptr, 0x8000, 0x3B, -2))) {
            if (R_SUCCEEDED(threadStart(&s_cntThread))) s_cntRunning = true;
            else threadClose(&s_cntThread);
        }
        if (!s_cntRunning) { CountWork(nullptr); s_cntInline = true; }   // v90: else right here (a few folder listings), never not at all
    }
    void Games::CountsTick(float now) {
        if (s_cntAt && armGetSystemTick() >= s_cntAt) RefreshCounts();   // v80: the deferred one (only while the menu runs)
        if (s_cntRunning) {
            if (!s_cntDone) return;
            threadWaitForExit(&s_cntThread); threadClose(&s_cntThread);
            s_cntRunning = false;
        } else if (!s_cntInline) return;
        s_cntInline = false;
        for (int k = 1; k < consoles::kCount; k++) if (s_cntResult[k] >= 0) Wheel::s_count[k] = s_cntResult[k];
        emus::Commit(now, s_cntResult);                     // v90: the wheel shows the consoles that have an emulator
    }

    void Games::CleanUp() {
        if (FILE *fa = fopen(kFwArmed, "r")) {                  // v58: the forwarder's romfs, as it was
            char line[32] = {};
            const u64 tid = fgets(line, sizeof line, fa) ? strtoull(line, nullptr, 16) : 0;
            fclose(fa);
            bool ok = true;
            if (IsForwarder(tid)) {
                char p[128];
                FwDir(tid, p, sizeof p, "/romfs/nextArgv");     ok = Gone(p) && ok;
                FwDir(tid, p, sizeof p, "/romfs/nextNroPath");  ok = Gone(p) && ok;
                FwDir(tid, p, sizeof p, "/romfs");              rmdir(p);   // only if empty: never someone else's files
                FwDir(tid, p, sizeof p, "");                    rmdir(p);
                Log("forwarder : %016llX remis comme avant%s", (unsigned long long)tid, ok ? "" : " (A REESSAYER)");
            }
            if (ok) remove(kFwArmed);                            // else: tried again at the next return / boot
        }
        FILE *fp = fopen(kDonorArmed, "r");
        if (!fp) return;
        char line[32] = {};
        const u64 donor = fgets(line, sizeof line, fp) ? strtoull(line, nullptr, 16) : 0;
        fclose(fp);
        if (donor) {
            char p[128];
            DonorDir(donor, p, sizeof p, "/exefs/main");      remove(p);
            DonorDir(donor, p, sizeof p, "/exefs/main.npdm"); remove(p);
            for (const char *s : kStubs) {
                char sub[40]; snprintf(sub, sizeof sub, "/exefs/%s.stub", s);
                DonorDir(donor, p, sizeof p, sub); remove(p);
            }
            DonorDir(donor, p, sizeof p, "/exefs");           rmdir(p);
            DonorDir(donor, p, sizeof p, "");                 rmdir(p);
            Log("jeux : donneur %016llX nettoye", (unsigned long long)donor);
        }
        remove(kDonorArmed);
        remove(kHbTarget);
    }

    // v99 (perf): the list is read by a background thread (Worker.hpp) - perf.log: A on « Ports natifs » took 53 ms in one
    // frame (every folder of switch/, each .nro's name). It fills m_g[0..m_scanN) while the frame still sees m_n = 0 (the
    // console glides to the left meanwhile, ~0.4 s: the list is there long before the first box); Publish hands it over.
    // Nothing here may touch what the frame uses (no Log, no m_n). The Switch's list (the loader's, in memory) stays at once.
    void Games::Scan() {
        m_scanN = 0;
        EmuIniLoad();                                         // v54
        const Console &c = kConsoles[m_console];
        if (m_console == 0) {                                   // Switch: installed titles, from the loader
            qext_refresh_titles();
            const int n = qext_title_count();
            for (int i = 0; i < n && m_scanN < kMaxGames; i++) {
                Game &g = m_g[m_scanN];
                g.tid = qext_title_id(i);
                if (!g.tid || IsForwarder(g.tid)) continue;   // v51: a forwarder is an app / a port, not a Switch game
                g.path[0] = 0; g.idx = i; g.noCover = false; g.sys[0] = 0; g.size = 0; g.part[0] = g.part[1] = g.part[2] = 0; g.where = 0; g.sized = false; g.noData = false;
                if (qext_title_name(i, g.title, sizeof g.title) <= 0 || !g.title[0])
                    snprintf(g.title, sizeof g.title, "%016llX", (unsigned long long)g.tid);
                m_scanN++;
            }
            // Archived games (listed, but their data is gone): NS view flags, measured on a test console
            // (2026-11-15): the installed ones are 0x00401F17, the archived ones (0x813, 0x1847, 0x817
            // seen) lack bit 0x100.
            // They stay in the list (like the real HOME menu), dimmed, after the installed ones.
            static NsApplicationView views[kMaxGames];
            static u64 ids[kMaxGames];
            for (int i = 0; i < m_scanN; i++) { ids[i] = m_g[i].tid; m_g[i].archived = false; }
            if (m_scanN > 0 && R_SUCCEEDED(nsGetApplicationView(views, ids, m_scanN)))
                for (int i = 0; i < m_scanN; i++) {
                    const u32 fl = *(const u32 *)((const u8 *)&views[i] + 0xC);
                    m_g[i].archived = !(fl & 0x100);
                }

        } else if (m_console == 7) {                            // v42: the native ports
            static char list[2048];
            list[0] = 0;
            if (FILE *fp = fopen(kPortsList, "r")) { const size_t r = fread(list, 1, sizeof list - 1, fp); list[r] = 0; fclose(fp); }
            if (DIR *d = opendir("sdmc:/switch")) {
                while (struct dirent *e = readdir(d)) {
                    if (m_scanN >= kMaxGames) break;
                    if (e->d_name[0] == '.') continue;
                    char dir[160];
                    if (snprintf(dir, sizeof dir, "sdmc:/switch/%s", e->d_name) >= (int)sizeof dir) continue;
                    DIR *sub = opendir(dir);
                    if (!sub) continue;
                    char nro[4][96];
                    int nn = 0;
                    bool gameDir = false;
                    while (struct dirent *s = readdir(sub)) {
                        if (s->d_name[0] == '.') continue;
                        if (EndsNro(s->d_name)) { if (nn < 4 && strlen(s->d_name) < sizeof nro[0] && !strchr(s->d_name, '"')) snprintf(nro[nn++], sizeof nro[0], "%s", s->d_name); }
                        else if (strncasecmp(s->d_name, "game", 4) == 0 && (s->d_name[4] == 0 || s->d_name[4] == '-' || s->d_name[4] == '_')) gameDir = true;
                    }
                    closedir(sub);
                    if (!nn || !(gameDir || Listed(list, e->d_name))) continue;
                    bool ready[4];
                    int nready = 0;
                    for (int k = 0; k < nn; k++) {
                        char p[300], data[300];
                        snprintf(p, sizeof p, "%s/%s", dir, nro[k]);
                        DataDir(p, data, sizeof data);
                        ready[k] = !data[0] || DirHasFiles(data);
                        nready += ready[k];
                    }
                    const int shown = nready ? nready : nn;
                    for (int k = 0; k < nn && m_scanN < kMaxGames; k++) {
                        if (nready && !ready[k]) continue;
                        Game &g = m_g[m_scanN];
                        if (snprintf(g.path, sizeof g.path, "%s/%s", dir, nro[k]) >= (int)sizeof g.path) continue;
                        if (!NroTitle(g.path, g.title, sizeof g.title)) CleanTitle(nro[k], g.title, sizeof g.title);
                        else if (shown == 1) StripTags(g.title);       // one version: no « (Europe) »
                        g.tid = 0; g.archived = false; g.idx = -1; g.noCover = false; g.sys[0] = 0; g.size = 0; g.part[0] = g.part[1] = g.part[2] = 0; g.where = 0; g.sized = false; g.noData = !ready[k];
                        PortSystem(list, e->d_name, g.sys, sizeof g.sys);   // v61
                        m_scanN++;
                    }
                }
                closedir(d);
            }
        } else if (c.folder) {                                  // emulated: sdmc:/roms/<console>/
            char dirs[4][96];                                   // v90: every folder people name that way (ps2, PS2, playstation2...; v93: + Tico's)
            const int nd = emus::RomDirs(c.folder, dirs, 4);
            for (int dk = 0; dk < nd; dk++) {
                const char *dir = dirs[dk];
                DIR *d = opendir(dir);
                if (!d) continue;
                while (struct dirent *e = readdir(d)) {
                    if (m_scanN >= kMaxGames) break;
                    if (e->d_name[0] == '.' || !HasExt(e->d_name, c)) continue;
                    Game &g = m_g[m_scanN];
                    const int len = snprintf(g.path, sizeof g.path, "%s/%s", dir, e->d_name);
                    if (len <= 0 || len >= (int)sizeof g.path || strchr(e->d_name, '"')) continue;   // must fit, quotable
                    CleanTitle(e->d_name, g.title, sizeof g.title);
                    g.tid = 0; g.archived = false; g.idx = -1; g.noCover = false; g.sys[0] = 0; g.size = 0; g.part[0] = g.part[1] = g.part[2] = 0; g.where = 0; g.sized = false; g.noData = false;
                    m_scanN++;
                }
                closedir(d);
            }
        }
        // installed first, then alphabetical (insertion sort, few items)
        auto before = [](const Game &x, const Game &y) {
            if (x.archived != y.archived) return !x.archived;
            return strcasecmp(x.title, y.title) < 0;
        };
        for (int a = 1; a < m_scanN; a++)
            for (int b = a; b > 0 && before(m_g[b], m_g[b - 1]); b--) {
                Game t = m_g[b]; m_g[b] = m_g[b - 1]; m_g[b - 1] = t;
            }
    }

    void Games::Publish() {
        m_scanning = false;
        m_n = m_scanN;
        Log("jeux : %s, %d jeu(x)", kConsoles[m_console].name, m_n);
        Wheel::s_count[m_console] = m_n;                              // v66
        for (int i = 0; i < m_n; i++) m_g[i].shift = 0;   // v77
        {   // v89: the game whose disc is in this console comes first in view (as the Switch shows its suspended game first)
            const int h = FindHeld();
            if (h >= 0) { m_sel = h; m_scroll = m_scrollFrom = (float)h; m_heldIdx = h; }
        }
    }
    namespace {
        Worker s_scanWork;                                  // v99: the list's background read (one at a time)
        void ScanJob(void *self) { ((Games *)self)->ScanNow(); }
    }
    void Games::ScanWait() { if (!s_scanWork.Idle()) s_scanWork.Wait(); m_scanning = false; }

    void (*Games::s_focus)(bool) = nullptr;
    void (*Games::s_dive)(float, bool) = nullptr;
    void (*Games::s_diveEnd)() = nullptr;
    void (*Games::s_nudge)(float) = nullptr;

    // v30 (Yass's vision): the console glides to the left, its games come out on its right like dominoes
    void Games::Open(int console, float now) {
        m_console = consoles::Valid(console) ? console : 0;
        m_open = true; m_openAt = now; m_sel = 0; m_scroll = 0; m_scrollV = 0; m_scrollFrom = 0; m_scrollT0 = -100; m_status[0] = 0; m_launching = false; m_pending = false;
        m_domino = 0; m_diveAt = -100;
        m_mode = kLaunch; m_fromResume = false; m_ejectT0 = m_quietAt = -100; m_closeStep = 0; m_launchAfter = false; m_heldIdx = -1;   // v89
        StopJob();                                        // v67: (a measuring of the other list stops first)
        m_manage = false; m_resort = false; m_askKind = 0; m_busy = 0; m_goneIdx = m_flashIdx = -1; m_goneAt = m_flashAt = -100;
        m_shelf.Forget();                                 // v31: another list of games
        m_gen++;                                          // v68
        // (the wheel's A already played the dive) v99: the list in the background (the Switch's: at once, it is in memory)
        ScanWait();                                       // (a list still being read for another console: dropped)
        m_n = 0; m_scanning = true;
        if (m_console == 0 || !s_scanWork.Start(ScanJob, this, 0x8000)) { Scan(); Publish(); }
        else if (s_scanWork.Done()) Publish();            // (no thread could be made: it was read at once)
        if (s_focus) s_focus(true);
        audio::PlaySweep(audio::Whoosh, 0.3f, -0.7f, 0.8f, 0.6f);   // the console glides to the left
        haptic::Thump(0.6f);
    }

    void Games::Close(float now) {
        if (!m_open || m_pending || m_returnT0 > 0 || Busy()) return;   // (v67: not while a game is being deleted)
        if (m_ejectT0 > 0 || m_quietAt > 0 || m_closeStep) return;      // (v89: a disc on its way home - B: PressB)
        m_manage = false; m_askKind = 0;
        if (s_mgRunning && s_mgKind == kJobSizes) StopJob();   // v68 (review): the measuring stops with the list
        m_open = false; m_closeAt = now;
        m_unfocusAt = now + Pace(0.36f);                  // v32 (Yass): the boxes leave first, THEN the console comes back
        audio::Play(audio::Tick1700, 0, 1.0f);
    }

    void Games::DragBegin() { if (!m_n || m_pending || m_returnT0 > 0 || m_ejectT0 > 0 || m_quietAt > 0) return; m_drag = true; m_dragScroll = m_scroll; m_scrollV = 0; }
    void Games::DragTo(float cards, float now) {
        if (!m_drag) return;
        m_scroll = fmaxf(-0.4f, fminf(m_n - 0.6f, m_dragScroll + cards));
        const int t = (int)lroundf(fmaxf(0.0f, fminf((float)(m_n - 1), m_scroll)));
        if (t != m_sel) { m_sel = t; m_moveAt = now; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Plastic(0.6f); }   // v55: a case
    }
    void Games::DragEnd(float now, float v) {
        if (!m_drag) return;
        m_drag = false;
        m_scrollV = v;
        int t = (int)FlickTarget(m_scroll, v, 6.0f);
        t = t < 0 ? 0 : (t >= m_n ? m_n - 1 : t);
        if (t != m_sel) { m_sel = t; m_moveAt = now; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Plastic(0.6f); }   // v55: a case
    }
    void Games::Move(int step, float now) {
        if (!m_open || !m_n || m_pending || m_askKind) return;   // (v78: the question is a popup, it takes the buttons)
        if (m_ejectT0 > 0 || m_quietAt > 0 || m_closeStep) return;   // (v89: the chosen one waits for the disc to be home)
        const int to = m_sel + step;
        if (to < 0 || to >= m_n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }   // end of the list
        m_sel = to; m_moveAt = now; m_askKind = 0;
        m_scrollFrom = m_scroll; m_scrollT0 = now;          // v34: the glide restarts from where it is
        audio::Play(audio::Deplacement, 0, 1.0f);
        haptic::Plastic(0.6f);                            // v55: a case
    }

    // A: a portable dives into its screen, a home console spins, the screen goes black - then the game starts
    void Games::PressA(float now) {
        if (!m_open || m_launching || !m_n) return;
        if (m_pending) {                                  // A again during the staging: straight to the console
            if (m_launchT0 > 0 && m_diveAt < 0 && (now - m_launchT0) * kPace < LaunchIn()) m_launchT0 = now - LaunchIn() / kPace;
            return;
        }
        if (m_returnT0 > 0 || m_askKind || m_ejectT0 > 0 || m_quietAt > 0 || m_closeStep) return;
        if (m_manage) { if (m_console == 0) Ask(kJobArchive, now); else audio::Play(audio::ThockSnd, 0, 0.5f); return; }   // v67
        if (m_g[m_sel].archived || m_g[m_sel].noData) { Launch(m_g[m_sel], now); return; }   // (it only says why it cannot start)
        // v89 - its disc is already in the console (still running): it comes back where it was - no box, just the dive. (Ended by
        // itself, its disc not home yet: the same, but it starts again from the beginning - Launch sees it is not running.)
        if (m_sel == m_heldIdx) {
            if (HeldAlive()) { Resume(now); return; }
            if (!qext_game_running()) { if (AskProfile()) Resume(now); return; }
        }
        // v78 - a game is suspended (HOME during it) and ANOTHER one is chosen: « close it? » first, like the Switch (the loader:
        // « asking is for the menu »). v89: « Oui » closes it, its disc goes home into its box, then this one's normal staging
        if (qext_game_running()) { AskClose(now, true); return; }
        StartStaging(now);
    }
    // v89 - « Fermer « <son nom> » ? »: a deletion's popup (« Non » chosen first, « Oui » waits, then turns red) - what is not saved is lost
    void Games::AskClose(float now, bool launchAfter) {
        static char title[96];
        char name[96] = "";
        if (HeldAlive()) snprintf(name, sizeof name, "%s", s_heldTitle);
        else {                                            // a game Brume did not start (or no longer knows): its name, when listed
            const u64 sus = qext_suspended_title();
            for (int j = 0, t = qext_title_count(); j < t && sus; j++)
                if (qext_title_id(j) == sus) { qext_title_name(j, name, sizeof name); break; }
        }
        {   // a long name is cut (the title stays on one line): 26 letters at most, never in the middle of one
            int n = 0, letters = 0;
            while (name[n] && letters < 26) { n++; while (((unsigned char)name[n] & 0xC0) == 0x80) n++; letters++; }
            if (name[n] && n + 4 < (int)sizeof name) snprintf(name + n, sizeof name - n, "\xe2\x80\xa6");
        }
        const char *generic = "Fermer le jeu en cours ?";
        if (name[0]) snprintf(title, sizeof title, lang::T("Fermer \xc2\xab %s \xc2\xbb ?"), name);
        popup::Spec s;
        s.kind = popup::kDanger; s.title = name[0] ? title : generic;
        s.text = launchAfter ? "Il sera ferm\xc3\xa9 pour lancer celui-ci. Ce qui n'est pas sauvegard\xc3\xa9 sera perdu."
                             : "Ce qui n'est pas sauvegard\xc3\xa9 sera perdu.";
        s.yes = "Oui"; s.no = "Non"; s.onYes = CloseYes; s.ctx = this;
        m_closeAfter = launchAfter;
        popup::Show(now, s);
    }
    // « Oui » (called once the popup has gone, from its drawing): the rest happens in Update - the profile, then the game closes on
    // the next frame (closing it takes the console a moment: the picture holds still, the menu's clock pauses meanwhile)
    void Games::CloseYes(void *self, float now) {
        Games &g = *(Games *)self;
        if (!g.m_open || g.m_pending || g.m_launching || !g.m_n || g.m_closeStep || g.m_ejectT0 > 0 || g.m_quietAt > 0) return;
        g.m_launchAfter = g.m_closeAfter;
        g.m_closeStep = 1;
        (void)now;
    }
    bool Games::AskProfile() {
        memset(&s_launchUid, 0, sizeof s_launchUid);
        if (m_console == 0 && Settings::s_askUser && Profiles::Count() > 1) {   // v46: the Switch's picker first; cancelled = no launch
            s_launchUid = Profiles::Ask();                // (v68: the time spent picking is a pause of the menu's clock - Entry)
            if (!accountUidIsValid(&s_launchUid)) { audio::Play(audio::Tick1700, 0, 1.0f); return false; }
        }
        return true;
    }
    // v89 - the same game again: its disc is in the console, its box stays open at its place; the others fall back, the dive (a
    // portable: into its screen; a home console turns), the black, and it comes back where it was (Launch: qext_resume_game)
    void Games::Resume(float now) {
        m_mode = kResume; m_fromResume = true;
        m_pending = true; m_pendingAt = 1e9f; m_diveAt = -100;
        m_launchT0 = now; m_stage = m_sel; m_evt = 0;
        audio::Play(audio::Tick2400);
        audio::Bell(audio::HarmonyNote(0, 72, 84), 0.035f, true);
        haptic::Tick(0.7f);
    }
    void Games::StartStaging(float now, bool asked) {
        if (!asked && !AskProfile()) return;
        // v32: the box comes to the front and opens, the disc / cartridge goes into the console (Update), then the dive
        m_mode = kLaunch; m_fromResume = false;
        m_pending = true; m_pendingAt = 1e9f; m_diveAt = -100;
        m_launchT0 = now; m_stage = m_sel; m_evt = 0;
        audio::PlaySweep(audio::Whoosh, 0.2f, -0.1f, 0.5f, 0.55f);
        audio::Play(audio::Tick2400);
        haptic::Tick(0.7f);
    }

    void Games::Launch(const Game &g, float now) {
        m_statusAt = now;
        const u64 t0 = armGetSystemTick();                       // v100: how long the console took (perf.log: 120-135 ms, under the black)
        audio::Play(audio::Tick2400);
        // (v78: nothing is closed before we know this one can start - an archived game used to close the suspended one)
        if (g.archived) {                                        // v78: a popup says it (it was a line under the boxes)
            popup::Spec s;
            s.kind = popup::kInfo; s.title = "Ce jeu est archiv\xc3\xa9"; s.subject = g.title;
            s.text = "Ses donn\xc3\xa9" "es ne sont plus sur la console : il faut le r\xc3\xa9installer pour y rejouer. Ses sauvegardes sont gard\xc3\xa9" "es.";
            popup::Show(now, s);
            return;
        }
        // v78 / v89 - its disc is still in the console (suspended with HOME): it comes back where it was, like the Switch - for
        // every kind of game (an emulated one or a port too: its forwarder / donor is what the loader holds)
        if (m_heldIdx >= 0 && &g == &m_g[m_heldIdx] && HeldAlive() && !qext_game_has_foreground()) {
            haptic::Stop(); audio::Hush();
            const Result rr = qext_resume_game();
            Log("jeux : reprise %s rc=0x%x", g.title, rr);
            if (R_SUCCEEDED(rr)) { m_launching = true; snprintf(m_status, sizeof m_status, lang::T("Lancement…")); return; }
            qext_terminate_game();                        // it would not come back: closed, then started again (below)
            s_heldCon = -1;
        }
        if (g.tid) {                                             // Switch: natively, as the last user
            CleanUp();
            haptic::Stop();                               // v29: motors off before the game takes the screen
            audio::Hush();                                // v80: every sound stops, the nappe paused
            if (qext_game_running()) qext_terminate_game();      // one game at a time (v78: the popup asked first - PressA)
            const AccountUid who = Player();
            Result rc = qext_launch_title_as(g.tid, who);
            if (R_FAILED(rc)) rc = qext_launch_title(g.tid);
            Log("jeux : lancement Switch %016llX rc=0x%x (profil %s, %.0f ms)", (unsigned long long)g.tid, rc,
                accountUidIsValid(&who) ? "ok" : "AUCUN", (double)armTicksToNs(armGetSystemTick() - t0) / 1e6);
            if (R_FAILED(rc)) { snprintf(m_status, sizeof m_status, "%s", lang::T("La console a refus\xc3\xa9 de le lancer.")); LaunchFailed(now, rc); return; }
            s_launchGen++;
            m_launching = true; Hold(g);                  // v89: its cartridge is in the Switch now
            snprintf(m_status, sizeof m_status, lang::T("Lancement…"));
            return;
        }
        if (m_console == 7) {                                    // v42: a native port, alone, in application mode
            if (g.noData) {
                char data[300], m[400];
                DataDir(g.path, data, sizeof data);
                const char *show = strncmp(data, "sdmc:/", 6) == 0 ? data + 6 : data;
                snprintf(m, sizeof m, lang::T("Copie-les sur la carte SD, dans  %s/"), show);
                popup::Spec s;                            // v78: a popup (it was a line under the boxes)
                s.kind = popup::kInfo; s.title = "Il manque les fichiers du jeu"; s.subject = g.title; s.text = m;
                popup::Show(now, s);
                return;
            }
            const u64 fw = ForwarderFor(g.title);         // v51: its real forwarder first
            if (fw ? !LaunchTitle(fw, m_status, sizeof m_status) : !LaunchNro(g.path, nullptr, m_status, sizeof m_status)) { LaunchFailed(now, 0); return; }
            m_launching = true; Hold(g);                  // v89
            snprintf(m_status, sizeof m_status, lang::T("Lancement…"));
            return;
        }
        const char *emu = EmulatorFor(m_console);        // v54: emulateurs.ini, then our list
        if (!emu) { snprintf(m_status, sizeof m_status, lang::T("\xc3\x89mulateur introuvable (voir brume/emulateurs.ini)")); LaunchFailed(now, 0); return; }
        char av[512];                                    // v93: "<nro>" "<game>", or a Tico module's own template
        if (!emus::Argv(m_console, emu, g.path, g.title, av, sizeof av)) { snprintf(m_status, sizeof m_status, "%s", lang::T("Chemin trop long")); LaunchFailed(now, 0); return; }
        if (!LaunchNro(emu, g.path, m_status, sizeof m_status, av)) { LaunchFailed(now, 0); return; }
        m_launching = true; Hold(g);                      // v89: its disc is in its console now
        snprintf(m_status, sizeof m_status, lang::T("Lancement…"));
    }
    // ---- v89 - the game still running in its console ----
    void Games::Hold(const Game &g) {
        s_heldCon = m_console; s_heldGame = g.tid; s_heldGen = s_launchGen;
        snprintf(s_heldPath, sizeof s_heldPath, "%s", g.path);
        snprintf(s_heldTitle, sizeof s_heldTitle, "%s", g.title);
        s_heldTid = qext_suspended_title();               // what the loader runs: the game itself, its forwarder or the donor (0: unknown)
        Log("jeux : %s est dans sa console (%016llX)", g.title, (unsigned long long)s_heldTid);
    }
    bool Games::HeldAlive() {
        if (s_heldCon < 0 || s_heldGen != s_launchGen || !qext_game_running()) return false;
        const u64 t = qext_suspended_title();
        return !t || !s_heldTid || t == s_heldTid;
    }
    int Games::HeldConsole() { return s_heldLive ? s_heldCon : -1; }
    int Games::FindHeld() const {
        if (s_heldCon < 0 || s_heldCon != m_console) return -1;
        auto same = [](const Game &g) { return s_heldGame ? g.tid == s_heldGame : (s_heldPath[0] && !strcmp(g.path, s_heldPath)); };
        if (m_heldIdx >= 0 && m_heldIdx < m_n && same(m_g[m_heldIdx])) return m_heldIdx;   // (still at its place: the usual case)
        for (int i = 0; i < m_n; i++) if (same(m_g[i])) return i;
        return -1;
    }
    void Games::Done() {
        // v89: not back from a game - our own closing (the loader then says « the game exited »): the way home is already ours
        if (m_pending || m_closeStep || m_ejectT0 > 0 || m_quietAt > 0) return;
        const bool was = m_launchT0 > 0 || m_launching;
        m_launching = false; m_pending = false; m_diveAt = -100; m_status[0] = 0;   // (v80: no « Lancement… » left)
        if (s_diveEnd) { s_diveEnd(); }
        if (was && m_open) {
            m_returnPending = true;
            m_mode = HeldAlive() ? kKeep : kLaunch;       // v89: still running - its disc stays in the console, the box goes back open
            if (m_mode == kLaunch) { m_fromResume = false; s_heldCon = -1; }   // (it ended: its disc comes home with the way back)
        } else { m_launchT0 = -100; }
    }
    // v78 - a launch that failed: a popup says why (m_status: the reason, already in the right language), like the Switch's error
    // screen, instead of a line under the boxes that went by unseen
    void Games::LaunchFailed(float now, unsigned rc) {
        audio::Arrive(0.8f);                             // v80: the soundscape comes back (and the popup is heard)
        static char why[160];
        snprintf(why, sizeof why, "%s", m_status);
        m_status[0] = 0;
        popup::Error(now, "Le jeu n'a pas pu d\xc3\xa9marrer", why, rc);
    }

    // v51 - a name, compared the forgiving way: case, spaces and the region tags at the end don't count
    namespace {
        void Norm(const char *in, char *out, int cap) {
            int n = 0;
            for (const char *p = in; *p && n < cap - 1; p++) {
                const unsigned char c = (unsigned char)*p;
                if (c == ' ' || c == '_' || c == '-' || c == ':' || c == '.') continue;
                out[n++] = (char)(c < 0x80 ? tolower(c) : c);
            }
            out[n] = 0;
            for (;;) {                                   // trailing « (…) » groups (StripTags may have taken them off one side)
                char *o = strrchr(out, '(');
                if (!o || o == out || out[n - 1] != ')') break;
                *o = 0; n = (int)(o - out);
            }
        }
    }
    u64 Games::ForwarderFor(const char *name) {
        if (!name || !name[0]) return 0;
        if (qext_title_count() <= 0) qext_refresh_titles();
        char want[96], got[96], nm[128];
        u64 found = 0;
        Norm(name, want, sizeof want);
        const int n = qext_title_count();
        for (int i = 0; i < n; i++) {
            const u64 t = qext_title_id(i);
            if (!IsForwarder(t)) continue;
            if (qext_title_name(i, nm, sizeof nm) <= 0) continue;
            Norm(nm, got, sizeof got);
            if (!strcmp(want, got)) {
                if (found) return 0;                     // v56 (review): ambiguous (e.g. several « RetroArch »): not this way
                found = t;
            }
        }
        return found;
    }
    // v58 - a forwarder starts another .nro (with its argv): its romfs files are replaced for this one launch
    bool Games::LaunchVia(u64 tid, const char *nro, const char *argv, char *status, int cap) {
        if (!IsForwarder(tid) || !nro || !argv) return false;    // never a real game
        const char *rel = strncmp(nro, "sdmc:", 5) == 0 ? nro + 5 : nro;   // its hbloader opens it on the SD card's own file system
        if (strlen(rel) >= 0x300 || strlen(argv) >= 2000) { snprintf(status, cap, lang::T("Chemin trop long")); return false; }
        if (qext_game_running()) qext_terminate_game();
        CleanUp();
        char p[128];
        FwDir(tid, p, sizeof p, "/romfs/nextArgv");
        struct stat st;
        bool ours = false;                                       // our own leftover (the game was still closing): ours to rewrite
        if (FILE *fa = fopen(kFwArmed, "r")) { char line[32] = {}; ours = fgets(line, sizeof line, fa) && strtoull(line, nullptr, 16) == tid; fclose(fa); }
        if (!ours && stat(p, &st) == 0) { Log("forwarder : %016llX a deja un romfs a lui, pas touche", (unsigned long long)tid); return false; }
        if (!ours && stat(kFwArmed, &st) == 0) { Log("forwarder : un autre est encore redirige, pas touche"); return false; }   // v68 (review)
        if (FILE *fa = fopen(kFwArmed, "w")) { fprintf(fa, "%016llX\n", (unsigned long long)tid); fclose(fa); }   // first: a crash is cleaned up
        else { snprintf(status, cap, lang::T("Impossible d'\xc3\xa9" "crire sur la carte SD")); return false; }
        mkdir("sdmc:/atmosphere/contents", 0777);
        FwDir(tid, p, sizeof p, "");       mkdir(p, 0777);
        FwDir(tid, p, sizeof p, "/romfs"); mkdir(p, 0777);
        bool ok = true;
        const char *const name[2] = { "/romfs/nextArgv", "/romfs/nextNroPath" };   // in this order (the forwarder takes the 1st, then its sibling)
        const char *const data[2] = { argv, rel };
        for (int k = 0; k < 2; k++) {
            FwDir(tid, p, sizeof p, name[k]);
            FILE *f = fopen(p, "wb");
            if (!f) { ok = false; break; }
            ok = fwrite(data[k], 1, strlen(data[k]) + 1, f) == strlen(data[k]) + 1 && ok;   // with its 0 (the forwarder does not add one)
            fclose(f);
        }
        haptic::Stop(); audio::Hush();                   // (v80: everything silent and stopped - it had faded out: Depart)
        Result rc = ok ? qext_launch_title_as(tid, Player()) : MAKERESULT(Module_Libnx, LibnxError_IoError);
        if (ok && R_FAILED(rc)) rc = qext_launch_title(tid);
        Log("forwarder : %s via %016llX rc=0x%x", argv, (unsigned long long)tid, rc);
        if (R_FAILED(rc)) { CleanUp(); return false; }
        s_launchGen++;                                   // v89: (the game held in its console, if any, was closed by it)
        return true;
    }

    bool Games::LaunchTitle(u64 tid, char *status, int cap) {
        if (qext_game_running()) qext_terminate_game();
        CleanUp();
        haptic::Stop(); audio::Hush();                   // (v80: everything silent and stopped - it had faded out: Depart)
        Result rc = qext_launch_title_as(tid, Player());
        if (R_FAILED(rc)) rc = qext_launch_title(tid);
        Log("forwarder : %016llX rc=0x%x", (unsigned long long)tid, rc);
        if (R_FAILED(rc)) { snprintf(status, cap, lang::T("Le forwarder n'a pas pu d\xc3\xa9marrer (erreur 0x%X)"), rc); return false; }
        s_launchGen++;                                   // v89
        return true;
    }

    // v35 - any .nro in APPLICATION mode (all the memory) through the donor: the emulators (with their game) and the
    // Apps page (alone). false + `status` = why it could not.
    bool Games::LaunchNro(const char *nro, const char *arg, char *status, int cap, const char *fullArgv) {
        if (!nro || !Exists(nro)) { snprintf(status, cap, lang::T("Homebrew introuvable sur la carte SD")); return false; }
        if (qext_game_running()) qext_terminate_game();          // one application at a time
        {   // v58 - the real forwarders first: no game borrowed, nothing written in a game's folder
            char av[512], nm[128];
            const int n = fullArgv ? snprintf(av, sizeof av, "%s", fullArgv)
                        : arg && arg[0] ? snprintf(av, sizeof av, "\"%s\" \"%s\"", nro, arg) : snprintf(av, sizeof av, "\"%s\"", nro);
            if (n > 0 && n < (int)sizeof av && !strchr(nro, '"')) {
                u64 fw = NroTitle(nro, nm, sizeof nm) ? ForwarderFor(nm) : 0;
                if (fw && !(arg && arg[0])) return LaunchTitle(fw, status, cap);     // alone: its forwarder, as it is
                if (!fw) fw = ForwarderFor(kLanceur);                                // anything: our Brume Launcher
                if (!fw) fw = ForwarderFor(kLanceurOld);
                if (fw && LaunchVia(fw, nro, av, status, cap)) return true;
                if (fw) Log("forwarder : echec, on passe par le donneur");
            }
        }
        const char *hbl = nullptr;
        for (const char *d : kHblDirs) { char p[96]; snprintf(p, sizeof p, "%s/main", d); if (Exists(p)) { hbl = d; break; } }
        if (!hbl) { snprintf(status, cap, lang::T("Chargeur de homebrew introuvable (hbloader_app)")); return false; }
        CleanUp();
        const u64 donor = PickDonor();
        if (!donor) { snprintf(status, cap, lang::T("Il faut au moins un jeu Switch installé pour lancer un homebrew")); return false; }
        // hbtarget.txt: the .nro, then its argv - "<nro>" ["<game>"] (512 characters max, like hbmenu)
        char argv[512];
        const int an = fullArgv ? snprintf(argv, sizeof argv, "%s", fullArgv)
                     : arg && arg[0] ? snprintf(argv, sizeof argv, "\"%s\" \"%s\"", nro, arg) : snprintf(argv, sizeof argv, "\"%s\"", nro);
        if (an <= 0 || an >= (int)sizeof argv || strchr(nro, '"')) { snprintf(status, cap, lang::T("Chemin trop long")); return false; }
        mkdir("sdmc:/slaunch", 0777);                    // v101: our hbloader reads sdmc:/slaunch/hbtarget.txt - a card without sLaunch has no such folder
        FILE *fp = fopen(kHbTarget, "w");
        if (!fp) { snprintf(status, cap, lang::T("Impossible d'écrire sur la carte SD")); return false; }
        fprintf(fp, "%s\n%s\n", nro, argv);
        fclose(fp);
        // written first, so a crash during the copy is still cleaned up
        if ((fp = fopen(kDonorArmed, "w"))) { fprintf(fp, "%016llX\n", (unsigned long long)donor); fclose(fp); }
        char p[128], from[128];
        mkdir("sdmc:/atmosphere/contents", 0777);
        DonorDir(donor, p, sizeof p, ""); mkdir(p, 0777);
        DonorDir(donor, p, sizeof p, "/exefs"); mkdir(p, 0777);
        DonorDir(donor, p, sizeof p, "/exefs/main");      snprintf(from, sizeof from, "%s/main", hbl);
        bool ok = CopyFile(from, p);
        DonorDir(donor, p, sizeof p, "/exefs/main.npdm"); snprintf(from, sizeof from, "%s/main.npdm", hbl);
        ok = ok && CopyFile(from, p);
        for (const char *s : kStubs) {
            char sub[40]; snprintf(sub, sizeof sub, "/exefs/%s.stub", s);
            DonorDir(donor, p, sizeof p, sub);
            if (FILE *st = fopen(p, "wb")) fclose(st); else ok = false;
        }
        haptic::Stop(); audio::Hush();                   // (v80: everything silent and stopped - it had faded out: Depart)
        Result rc = ok ? qext_launch_title_as(donor, Player()) : MAKERESULT(Module_Libnx, LibnxError_IoError);
        Log("homebrew : %s via %016llX rc=0x%x", argv, (unsigned long long)donor, rc);
        if (R_FAILED(rc)) {
            CleanUp();
            snprintf(status, cap, lang::T("Le homebrew n'a pas pu démarrer (erreur 0x%X)"), rc);
            return false;
        }
        s_launchGen++;                                   // v89
        return true;
    }

    // ======================= v67 - « Gérer » =======================
    bool Games::StartJob(int kind, int idx) {
        if (s_mgRunning) return false;
        s_mgKind = kind; s_mgIdx = idx; s_mgDone = false; s_mgAbort = false; s_mgRc = 0; s_mgFreed = 0; s_mgGen = m_gen;
        if (kind != kJobSizes) {                          // v67.1: the job never reads the list again (it may move meanwhile)
            if (idx < 0 || idx >= m_n) return false;
            s_mgTid = m_g[idx].tid; s_mgFreed = m_g[idx].size;
            snprintf(s_mgPath, sizeof s_mgPath, "%s", m_g[idx].path);
        }
        if (R_FAILED(threadCreate(&s_mgThread, MgWork, this, nullptr, 0x10000, 0x3B, -2))) return false;
        if (R_FAILED(threadStart(&s_mgThread))) { threadClose(&s_mgThread); return false; }
        s_mgRunning = true;
        return true;
    }
    void Games::StopJob() {                               // a measuring stops at once (v68: a deletion goes on by itself - it only
        if (!s_mgRunning || s_mgKind != kJobSizes) return; // works on its copy - and its end no longer touches a newer list)
        s_mgAbort = true;
        threadWaitForExit(&s_mgThread); threadClose(&s_mgThread);
        s_mgRunning = false; m_busy = 0;
    }
    void Games::Shutdown() {                              // v68 (review): no thread may outlive the menu (its code is unloaded)
        s_scanWork.Wait();                                // v99: the list's read
        s_mgAbort = true;
        if (s_mgRunning) { threadWaitForExit(&s_mgThread); threadClose(&s_mgThread); s_mgRunning = false; }
        if (s_cntRunning) { threadWaitForExit(&s_cntThread); threadClose(&s_cntThread); s_cntRunning = false; }
    }
    void Games::MgWork(void *self) {
        Games &G = *(Games *)self;
        if (s_mgKind == kJobSizes) {
            const bool ns = G.m_console == 0 && R_SUCCEEDED(nsInitialize());
            for (int i = 0; i < G.m_n && !s_mgAbort && !qext_game_running(); i++) {
                Game &g = G.m_g[i];
                if (g.sized) continue;
                u64 p0 = 0, p1 = 0, p2 = 0; uint8_t where = 2;
                if (G.m_console == 0) { if (ns && !g.archived) SwitchSize(g.tid, p0, p1, p2, where); }
                else if (G.m_console == 7) {
                    char data[300]; int budget = 200000;
                    DataDir(g.path, data, sizeof data);
                    p0 = FileSize(g.path) + (data[0] && IsDir(data) ? TreeSize(data, 0, budget) : 0);
                } else p0 = PartsDo(g.path, false);
                if (s_mgAbort) break;
                g.part[0] = p0; g.part[1] = p1; g.part[2] = p2; g.where = where; g.size = p0 + p1 + p2; g.sized = true;
            }
            if (ns) nsExit();
        } else {
            struct { u64 tid; const char *path; } g = { s_mgTid, s_mgPath };   // (the copy made when the job started)
            if (G.m_console == 0) {
                Result rc = nsInitialize();
                if (R_SUCCEEDED(rc)) { rc = s_mgKind == kJobArchive ? nsDeleteApplicationEntity(g.tid) : nsDeleteApplicationCompletely(g.tid); nsExit(); }
                s_mgRc = rc;
            } else if (G.m_console == 7) {                // a port: its game folder, then its .nro (and the folder, if empty)
                char data[300], dir[300];
                DataDir(g.path, data, sizeof data);
                const char *slash = strrchr(g.path, '/');
                if (slash) snprintf(dir, sizeof dir, "%.*s", (int)(slash - g.path), g.path); else dir[0] = 0;
                bool shared = false;                      // v68 (review): another version of this folder uses the same data folder
                if (data[0] && dir[0]) if (DIR *d = opendir(dir)) {
                    char other[512], od[300];
                    while (struct dirent *e = readdir(d)) {
                        if (!EndsNro(e->d_name) || snprintf(other, sizeof other, "%s/%s", dir, e->d_name) >= (int)sizeof other || !strcmp(other, g.path)) continue;
                        DataDir(other, od, sizeof od);
                        if (!strcmp(od, data)) { shared = true; break; }
                    }
                    closedir(d);
                }
                bool ok = SafeUnder(g.path, "sdmc:/switch/", 1);
                if (ok && !shared && data[0] && IsDir(data) && SafeUnder(data, "sdmc:/switch/", 1)) ok = TreeRemove(data, 0);   // the data first:
                if (ok) ok = remove(g.path) == 0;          // if it fails, the .nro stays and a second try finds everything again
                if (dir[0]) rmdir(dir);
                s_mgRc = ok ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
            } else {
                s_mgRc = SafeUnder(g.path, "sdmc:/roms/", 1) && PartsDo(g.path, true) == 0 ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
            }
        }
        s_mgDone = true;
    }
    void Games::Gauges() {                                // the space of the console and the SD card (asked now, not per frame)
        if (R_FAILED(nsInitialize())) return;
        const NcmStorageId st[2] = { NcmStorageId_BuiltInUser, NcmStorageId_SdCard };
        for (int k = 0; k < 2; k++) {
            s64 f = 0, t = 0;
            if (R_SUCCEEDED(nsGetFreeSpaceSize(st[k], &f)) && R_SUCCEEDED(nsGetTotalSpaceSize(st[k], &t)) && t > 0) { m_gTot[k] = (double)t; m_gUsed[k] = (double)(t - f); }
        }
        nsExit();
    }
    // the order: « Gérer » = the biggest first (or by name), the archived ones last; else the usual (installed, then by name)
    void Games::Sort(float now) {
        if (!m_n) return;
        if (s_mgRunning) { m_resort = true; return; }     // (never while the thread reads the list)
        static int ord[kMaxGames], newOf[kMaxGames];
        for (int i = 0; i < m_n; i++) ord[i] = i;
        auto before = [&](int x, int y) {
            const Game &a = m_g[x], &b = m_g[y];
            if (a.archived != b.archived) return !a.archived;
            if (m_manage && m_sortBy == 0 && a.size != b.size) return a.size > b.size;
            return strcasecmp(a.title, b.title) < 0;
        };
        for (int a = 1; a < m_n; a++)                     // (indices only; the games move once, below)
            for (int b = a; b > 0 && before(ord[b], ord[b - 1]); b--) { const int t = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = t; }
        bool same = true;
        for (int i = 0; i < m_n; i++) { newOf[ord[i]] = i; same = same && ord[i] == i; }
        if (same) return;
        static Game tmp;                                  // apply the permutation, cycle by cycle (each game moves once)
        static bool done[kMaxGames];
        for (int i = 0; i < m_n; i++) done[i] = false;
        for (int i = 0; i < m_n; i++) {
            if (done[i] || ord[i] == i) { done[i] = true; continue; }
            tmp = m_g[i];
            int j = i;
            while (true) { const int k = ord[j]; done[j] = true; if (k == i) { m_g[j] = tmp; break; } m_g[j] = m_g[k]; j = k; }
        }
        m_shelf.Remap(newOf, m_n);
        // v77 (Yass: « de vraies animations »): each box glides from where it was to its new place, the chosen one stays
        const int oldSel = m_sel, newSel = newOf[m_sel];
        for (int i = 0; i < m_n; i++) m_g[newOf[i]].shift = (float)((i - newOf[i]) - (oldSel - newSel)) + m_g[newOf[i]].shift;
        m_sel = newSel; m_scroll = (float)m_sel; m_scrollFrom = m_scroll; m_scrollT0 = -100; m_scrollV = 0;
        m_askKind = 0;
        (void)now;
    }
    void Games::Manage(bool on, float now) {
        if (!m_open || m_pending || m_returnT0 > 0 || on == m_manage || Busy()) return;
        if (m_ejectT0 > 0 || m_quietAt > 0 || m_closeStep) return;   // (v89)
        m_manage = on; m_askKind = 0; m_sortBy = 0;
        if (!on && s_mgRunning && s_mgKind == kJobSizes) { StopJob(); m_resort = false; }   // v68 (review): leaving « Gérer » stops the measuring
        audio::Play(audio::Tick2400, 0, 1.0f); audio::PlaySweep(audio::Whoosh, 0.3f, -0.3f, 0.45f, 0.4f); haptic::Tap(0.6f);
        if (on) {
            Gauges();
            for (int k = 0; k < 2; k++) m_gShown[k] = 0;  // the bars fill up
            bool all = true;
            for (int i = 0; i < m_n; i++) all = all && m_g[i].sized;
            if (!all && StartJob(kJobSizes, -1)) { m_resort = true; return; }   // sorted when the sizes are known
        }
        Sort(now);
    }
    void Games::PressX(float now) {
        if (!m_manage) {                                  // v89 - on the box of the game still running: « Fermer « … » ? » (like the Switch's X)
            if (m_open && m_n && m_sel == m_heldIdx && HeldAlive() && !m_pending && !m_launching && m_returnT0 < 0 && m_ejectT0 < 0 &&
                m_quietAt < 0 && !m_closeStep && !Busy())
                AskClose(now, false);
            return;
        }
        if (m_askKind) return;                            // (v77: X never confirms - v78: the popup takes the buttons anyway)
        Ask(kJobDelete, now);
    }
    void Games::PressY(float now) {
        if (!m_manage || !m_n || Busy()) return;
        m_sortBy ^= 1;
        audio::Play(audio::Deplacement, 0, 1.0f); haptic::Plastic(0.6f);
        Sort(now);
    }
    bool Games::PressB(float now) {
        if (m_ejectT0 > 0 || m_quietAt > 0 || m_closeStep) {   // v89 - a disc going home: B = the chosen game will not start after it
            if (m_launchAfter) { m_launchAfter = false; audio::Play(audio::Tick1700, 0, 0.8f); haptic::Tick(0.4f); }
            return true;
        }
        if (!m_manage) return false;
        if (Busy() || m_askKind) return true;
        Manage(false, now);
        return true;
    }
    // A (archive) / X (delete): v77 (Yass deleted archived games by mistake: « une vraie double vérif »). v78 (Yass: « double
    // confirmée avec popup et bouton oui qui attend avant de s'allumer en rouge et être cliquable ») - a popup: « Annuler »
    // chosen first; « Supprimer » waits ~1.5 s (a line of light fills under it) before it turns red and can be chosen; B, a touch
    // outside it or « Annuler »: nothing happens. Archiving loses nothing for good (the saves stay): a plain question.
    void Games::Ask(int kind, float now) {
        if (!m_n || Busy() || m_askKind) return;
        Game &g = m_g[m_sel];
        if (kind == kJobArchive && g.archived) { Say(now, lang::T("Ce logiciel est d\xc3\xa9j\xc3\xa0 archiv\xc3\xa9")); return; }
        if (qext_game_running()) { Say(now, lang::T("Ferme d'abord le jeu en cours (HOME, puis X sur sa bo\xc3\xaete)")); return; }
        if (s_mgRunning) { Say(now, lang::T("Calcul des tailles\xe2\x80\xa6")); return; }
        m_askKind = kind; m_askIdx = m_sel;
        const bool del = kind == kJobDelete;
        popup::Spec s;
        s.kind = del ? popup::kDanger : popup::kAsk;
        s.title = del ? "Supprimer ce jeu ?" : "Archiver ce jeu ?";
        s.subject = g.title;
        s.text = del ? (m_console == 0 ? "Il sera effac\xc3\xa9 de la console. Ses sauvegardes restent."
                                       : "Son fichier sera effac\xc3\xa9 de la carte SD. Ses sauvegardes restent.")
                     : "Ses sauvegardes restent. Pour rejouer, il faudra le r\xc3\xa9installer.";
        s.yes = del ? "Supprimer" : "Archiver"; s.no = "Annuler";
        s.onYes = AskYes; s.onNo = AskNo; s.ctx = this;
        popup::Show(now, s);
    }
    void Games::AskYes(void *self, float now) {
        Games &G = *(Games *)self;
        const int kind = G.m_askKind;
        G.m_askKind = 0;
        if (!kind || G.m_askIdx < 0 || G.m_askIdx >= G.m_n || G.Busy()) return;
        if (!G.StartJob(kind, G.m_askIdx)) { audio::Play(audio::ThockSnd, 0, 0.6f); return; }
        G.m_busy = kind;
        (void)now;                                        // (its sound and its light were the popup's: Popup.cpp)
    }
    void Games::AskNo(void *self, float now) { ((Games *)self)->m_askKind = 0; (void)now; }
    void Games::JobDone(float now) {
        const int kind = s_mgKind;
        int i = s_mgIdx;
        m_busy = 0;
        if (s_mgGen != m_gen) i = -1;                     // v68: another list since: only the message (the list is already new)
        if (kind == kJobSizes) { if (m_resort) { m_resort = false; Sort(now); } return; }
        char m[200], freed[24];
        if (R_FAILED(s_mgRc)) { snprintf(m, sizeof m, lang::T("Impossible (erreur 0x%X)"), s_mgRc); Say(now, m); audio::Play(audio::ThockSnd, 0, 0.6f); return; }
        if (i < 0 || i >= m_n) { Gauges(); return; }
        SizeStr(s_mgFreed, freed, sizeof freed);
        if (m_console == 0) {                             // v67.1: the system's list changed - each game finds its place in it again
            qext_refresh_titles();
            const int t = qext_title_count();
            for (int k = 0; k < m_n; k++)
                for (int j = 0; j < t; j++) if (qext_title_id(j) == m_g[k].tid) { m_g[k].idx = j; break; }
        }
        Game &g = m_g[i];
        if (kind == kJobArchive) {
            snprintf(m, sizeof m, lang::T("\xc2\xab %s \xc2\xbb archiv\xc3\xa9 : %s lib\xc3\xa9r\xc3\xa9s"), g.title, freed);
            g.archived = true; g.size = 0; g.part[0] = g.part[1] = g.part[2] = 0;
            m_flashIdx = i; m_flashAt = now;              // it lights up, then goes grey
            audio::Bell(audio::HarmonyNote(0, 62, 74), 0.04f, true);
        } else {
            snprintf(m, sizeof m, lang::T("\xc2\xab %s \xc2\xbb supprim\xc3\xa9 : %s lib\xc3\xa9r\xc3\xa9s"), g.title, freed);
            m_goneIdx = i; m_goneAt = now;                // it falls back and fades, then leaves the list (Update)
            audio::PlaySweep(audio::Whoosh, 0.0f, 0.4f, 0.6f, 0.45f); haptic::Plastic(0.8f);
        }
        Say(now, m);
        Gauges();
    }

    void Games::Update(float now, float dt) {
        if (m_scanning && s_scanWork.Done()) Publish();   // v99: the list read in the background is there
        if (s_mgRunning && s_mgDone) { threadWaitForExit(&s_mgThread); threadClose(&s_mgThread); s_mgRunning = false; JobDone(now); }   // v67
        if (m_goneAt > 0 && now - m_goneAt > 0.6f && m_goneIdx >= 0 && m_goneIdx < m_n) {   // v67: the deleted game leaves the list
            static int newOf[kMaxGames];
            const int gi = m_goneIdx;
            for (int i = 0; i < m_n; i++) newOf[i] = i < gi ? i : (i == gi ? -1 : i - 1);
            for (int i = gi; i + 1 < m_n; i++) m_g[i] = m_g[i + 1];
            m_n--;
            for (int i = gi; i < m_n; i++) m_g[i].shift += 1.0f;   // v77: they were one place further: they glide into the gap
            m_shelf.Remap(newOf, m_n + 1);
            if (m_sel > gi || m_sel >= m_n) m_sel = m_sel > 0 ? m_sel - 1 : 0;
            m_scrollFrom = m_scroll; m_scrollT0 = now;     // the others close the gap (the glide)
            m_goneIdx = -1; m_goneAt = -100;
            Wheel::s_count[m_console] = m_n;
        }
        for (int k = 0; k < 2; k++) m_gShown[k] += (m_gUsed[k] - m_gShown[k]) * (1 - expf(-dt * 4.0f));
        {   // v77: the boxes glide back to their places (sorts, deletions) (v78: the question is a popup: no time limit)
            const float f = expf(-dt * 6.5f);
            for (int i = 0; i < m_n; i++) { m_g[i].shift *= f; if (fabsf(m_g[i].shift) < 0.002f) m_g[i].shift = 0; }
        }
        if (!m_open && m_unfocusAt > 0 && now >= m_unfocusAt) {   // v32: the boxes are down: the console goes back
            m_unfocusAt = -1;
            if (s_focus) s_focus(false);
            audio::PlaySweep(audio::Whoosh, -0.7f, 0.3f, 0.8f, 0.5f);
        }
        // v89 - the game held in its console: still running? (asked once a frame - the wheel shows it, the boxes follow)
        s_heldLive = HeldAlive();
        m_heldIdx = m_open ? FindHeld() : -1;
        {
            int held = -1; float slot[3] = {};
            if (s_heldLive && HeldSlot(s_heldCon, slot)) held = s_heldCon;
            Wheel::s_held = held;
            for (int k = 0; k < 3; k++) Wheel::s_heldSlot[k] = slot[k];
        }
        if (m_closeStep == 1) {                                     // « Oui »: the profile first (cancelled: nothing closed, nothing started)
            m_closeStep = m_launchAfter && !AskProfile() ? 0 : 2;
            if (!m_closeStep) m_launchAfter = false;
        } else if (m_closeStep == 2 && popup::Shown() < 0.01f) {  // then it closes, once the popup has fully gone (the close holds the picture still)
            m_closeStep = 0;
            const bool ours = HeldAlive();
            const int idx = ours && s_heldCon == m_console ? m_heldIdx : -1;
            Log("jeux : fermeture du jeu en cours (%s)", ours ? s_heldTitle : "pas lance par Brume");
            haptic::Stop();
            const u64 t0 = armGetSystemTick();
            qext_terminate_game();                                  // (blocking: the console closes it - the menu's clock pauses)
            Log("jeux : jeu ferme en %.0f ms", (double)armTicksToNs(armGetSystemTick() - t0) / 1e6);   // v100 (perf.log)
            CleanUp();                                              // (its donor / forwarder as it was - onOpen does it again, harmless)
            if (ours) s_heldCon = -1;
            s_heldLive = false; Wheel::s_held = -1;
            if (idx >= 0) { m_stage = idx; StartEject(now); }       // its box is here: it comes for its disc, closes, goes back
            else if (ours) { m_quietAt = now; m_evt = 0; }          // another console's game: its disc's way home, heard
            else if (m_launchAfter) { m_launchAfter = false; if (m_open && m_n) StartStaging(now, true); }
        }
        // v89 - it ended by itself, or was closed elsewhere (a crash, the overlay, another launch): when its screen is on and still,
        // its disc comes back into its box (never a disc left in a console with nothing running)
        if (s_heldCon >= 0 && !s_heldLive && m_open && s_heldCon == m_console && !m_pending && !m_launching && m_returnT0 < 0 &&
            !m_returnPending && !m_settle.on && m_ejectT0 < 0 && m_quietAt < 0 && !m_closeStep && !popup::Open() && !m_drag && !Busy() &&
            now - m_openAt > Pace(1.2f)) {
            if (m_heldIdx >= 0) { m_stage = m_heldIdx; m_launchAfter = false; StartEject(now); }
            s_heldCon = -1;
        }
        // v33 - the staging of a launch: a sound for every moment, then the dive once the media has settled in the console
        // (v89: a resume - its disc is already in: straight to the dive, silently until then)
        if (m_pending && m_launchT0 > 0) {
            const float t = (now - m_launchT0) * kPace, tJ = JourneyStart(m_console), tIn = LaunchIn();
            const bool card = kBoxKind(m_console) == 4, staged = m_mode != kResume;
            auto once = [&](int bit, float at) { if (staged && t >= at && !(m_evt & (1u << bit))) { m_evt |= 1u << bit; return true; } return false; };
            auto pitchOf = [](int midi, int ref) { return powf(2.0f, (midi - ref) / 12.0f); };
            if (once(0, 0.0f)) audio::Play((audio::Sound)(audio::Swell0 + consoles::kDefs[m_console].voice), 0.1f, 0.30f);      // the choir breathes in
            if (!card && once(1, 0.46f)) {                                         // the case unclips...
                audio::Thock(0.0f, 1.8f, 0.06f); audio::Play(audio::Tick1700, 0.0f, 0.7f); haptic::Tap(0.55f);
            }
            if (!card && once(2, 0.55f)) audio::PlaySweep(audio::Whoosh, 0.05f, -0.15f, 0.45f, 0.22f);   // ...swings open
            if (!card && once(3, 1.10f)) { audio::Thock(-0.15f, 1.3f, 0.035f); haptic::Tick(0.4f); }    // and settles
            if (once(4, tJ + 0.05f)) {                                             // the disc / cartridge lifts: an arpeggio
                for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k, 72, 86), 0.05f - k * 0.008f, true, k * 0.09f);
                audio::Crystal(0.0f, 0.35f, audio::HarmonyNote(3, 84, 96));
                haptic::Tick(0.5f);
            }
            if (once(5, tJ + 0.45f)) audio::PlaySweep(audio::Whoosh, 0.1f, -0.85f, 1.1f, 0.65f);       // the flight
            if (staged && !card && t > tJ && t < tJ + 1.95f) {                      // v55: the disc spinning, felt (IDEES #7)
                const float u = (t - tJ) / 1.95f;
                haptic::Purr(0.6f * sinf(3.14159265f * u), 90.0f + 110.0f * u);
            }
            if (once(6, tJ + 0.75f)) {                                             // sparkles on the way
                audio::Play(audio::PlinkH84, -0.4f, 0.35f, pitchOf(audio::HarmonyNote(0, 84, 96), 84));
                audio::Play(audio::PlinkH84, -0.55f, 0.28f, pitchOf(audio::HarmonyNote(2, 84, 96), 84), 0.11f);
            }
            if (once(7, tJ + 1.05f)) audio::Play(audio::SwishCam, -0.7f, 0.30f, 0.85f);                // it lines up with the slot
            if (once(8, tJ + 1.95f)) {                                             // it seats: a deep, round « clunk »
                audio::Thock(-0.7f, 0.72f, 0.10f);
                audio::SoftLow(audio::HarmonyNote(0, 36, 47), 0.17f, 0.9f);
                audio::Play(audio::Low06, -0.6f, 0.45f);
                audio::Bell(audio::HarmonyNote(0, 60, 71), 0.05f, true, 0.06f);
                haptic::Thump(0.9f);
                if (s_nudge) s_nudge(now);
            }
            if (m_diveAt < 0 && (t >= tIn || (!staged && t >= 0.12f))) {          // then the dive (v30), the black, the game
                const bool portable = consoles::kDefs[m_console].portable;   // Switch, PSP, GBA (v93: DS, Game Boy, Game Gear)
                // v80 (Yass: « le son se coupe d'un coup »): the soundscape and the staging's tails fade out under the entry
                // sound (a portable: the dive into its screen; a home console: it turns, then black), and the game starts
                // once it has gone into silence - the black holds that long
                m_diveAt = now; m_pendingAt = now + audio::kEntryLen;
                if (s_dive) s_dive(now, portable);
                audio::Depart(0.3f);
                audio::EntrySound(portable, -0.4f);                                // (the console stands on the left)
                haptic::Swell(0.9f, 0.6f);
            }
        }
        if (m_pending && m_diveAt > 0 && now >= m_pendingAt) {      // the dive is over: the game
            m_pending = false;
            Launch(m_g[m_stage], now);
            if (!m_launching) {                                     // it failed: light back, the disc comes back to its box
                m_diveAt = -100; m_diveOutAt = now; if (s_diveEnd) s_diveEnd();
                audio::Arrive(0.8f);                                // v80: the soundscape comes back
                m_mode = kLaunch; m_fromResume = false;             // (v89: a resume that failed: its game is closed now)
                StartReturn(now);
            }
        }
        // v80 (Yass: « l'animation de sortie est propre mais saccade un peu »): back from the game the first frames are slow
        // (the console is still closing it) - the staging used to start at once and jumped. Now it waits, black, until the
        // frames are steady (Settle), then the light comes back with the exit sound and the soundscape fades back in.
        if (m_returnPending) {
            m_returnPending = false;
            StartReturn(now + 1000.0f); m_diveOutAt = m_returnT0;   // (held: the media in the console, the screen black)
            m_settle.Begin(now);
        }
        if (m_settle.Step(now, dt)) {
            StartReturn(now); m_diveOutAt = now;
            audio::Arrive(1.3f);
            audio::ExitSound(-0.3f);
        }
        if (m_returnT0 > 0 && m_mode == kKeep) {                    // v89 - still running: the disc stays, the box goes back to its place, open
            const float r = (now - m_returnT0) * kPace;
            auto once = [&](int bit, float at) { if (r >= at && !(m_evt & (1u << bit))) { m_evt |= 1u << bit; return true; } return false; };
            if (!m_fromResume && once(16, 0.10f)) audio::PlaySweep(audio::Whoosh, 0.0f, 0.3f, 0.6f, 0.25f);   // back on the shelf
            if (once(17, m_fromResume ? 0.25f : 0.95f)) {                         // it stands there, open, its disc still in the console
                audio::Thock(0.25f, 1.45f, 0.035f);
                audio::Bell(audio::HarmonyNote(2, 67, 81), 0.03f, true, 0.04f);
                haptic::Tick(0.35f);
            }
            if (r > (m_fromResume ? 0.6f : 1.4f)) { m_returnT0 = -100; m_launchT0 = -100; m_mode = kLaunch; }
        } else if (m_returnT0 > 0) {                                // v33 - back from the game: the staging backwards, with its sounds
            const float r = (now - m_returnT0) * kPace;   // v36: the whole staging, livelier
            HomeSounds(r);
            if (r > 3.50f) { m_returnT0 = -100; m_launchT0 = -100; m_mode = kLaunch; m_fromResume = false; }
        }
        if (m_ejectT0 > 0) {                                        // v89 - the disc comes out of the console and goes home (closed / ended)
            const float r = (now - m_ejectT0) * kPace;
            HomeSounds(r - kEjectLead);
            if (r - kEjectLead > 3.2f) {                            // its box is nearly back at its place: then the chosen game, normally
                m_ejectT0 = -100; m_mode = kLaunch; m_fromResume = false;
                if (m_launchAfter) { m_launchAfter = false; if (m_open && m_n && !popup::Open()) StartStaging(now, true); }
            }
        }
        if (m_quietAt > 0) {                                        // v89 - another console's disc going home: only heard (its console is away)
            const float q = (now - m_quietAt) * kPace;
            auto once = [&](int bit, float at) { if (q >= at && !(m_evt & (1u << bit))) { m_evt |= 1u << bit; return true; } return false; };
            if (once(24, 0.0f)) {                                                  // it unseats, far away on the left
                audio::Play(audio::Tick2400, -0.8f, 0.5f); audio::Thock(-0.8f, 0.95f, 0.05f);
                audio::SoftLow(audio::HarmonyNote(0, 43, 54), 0.10f, 0.6f);
                haptic::Tap(0.45f);
            }
            if (once(25, 0.25f)) audio::PlaySweep(audio::Whoosh, -0.9f, -0.4f, 0.9f, 0.4f);
            if (once(26, 0.60f)) {                                                 // home
                for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(2 - k, 67, 81), 0.035f - k * 0.007f, true, k * 0.10f);
                haptic::Tick(0.4f);
            }
            if (once(27, 0.95f)) { audio::Thock(-0.6f, 1.55f, 0.05f); audio::Play(audio::Tick1700, -0.6f, 0.5f); }   // clac
            if (q > 1.25f) {
                m_quietAt = -100;
                if (m_launchAfter) { m_launchAfter = false; if (m_open && m_n && !popup::Open()) StartStaging(now, true); }
            }
        }
        // the dominoes: one « clack » per card as it lands (the first 6), a note of the chord, rising
        if (m_open) {
            while (m_domino < 6 && m_domino < m_n && now >= m_openAt + Pace(0.28f + m_domino * 0.10f + 0.30f)) {
                const float x = 560 + m_domino * 80.0f;
                audio::PopNote(audio::HarmonyNote(m_domino, 76, 91), fmaxf(-1, fminf(1, (x - 640) / 640.0f)) * 0.7f, m_domino == 0);
                audio::Thock((x - 640) / 900.0f, 1.25f + m_domino * 0.04f, 0.04f);
                haptic::Tick(0.35f);
                m_domino++;
            }
        }
        // v34 - the Cover Flow glides with the same motion as the carousel (ease out expo, .6 s)
        if (!m_drag) Glide(m_scroll, m_scrollV, (float)m_sel, dt, 6.0f);   // v35: the Cover Flow glides (keeps its speed)
        if (m_launching && now - m_statusAt > 6.0f) { m_launching = false; }   // nothing happened: allow again
    }

    void Games::Draw(Ui &ui, float now) {
        // v30 (Yass's vision): the games, on the right of the console (which glided to the left), as glass cards in a
        // carousel; they came out one by one like dominoes (stand up with a small bounce). The chosen one big, the next
        // ones smaller behind it, the previous ones slide away under the console.
        float k = m_open ? EaseOut3((now - m_openAt) / 0.5f) : 1 - EaseOut3((now - m_closeAt) / 0.3f);
        if (k <= 0.003f && m_diveAt < 0 && now - m_diveOutAt > 0.5f) return;
        const Console &c = kConsoles[m_console];
        const float X = 700, Y = 110, W = 520, H = 500;   // (kept for the status line below)
        if (!m_n && !m_scanning && k > 0.003f) {         // (v99: not while the list is still being read)
            const char *l1 = m_console == 7 ? "Aucun port natif trouvé." : "Aucun jeu trouvé.";
            char l2[96];
            if (c.folder) snprintf(l2, sizeof l2, lang::T("Dépose tes jeux dans  roms/%s/  sur la carte SD"), c.folder);
            else if (m_console == 7) snprintf(l2, sizeof l2, lang::T("Range chaque port dans  switch/<son dossier>/  (liste : brume/ports.txt)"));
            else l2[0] = 0;
            ui.TextCentered(fonts::kGameItem, l1, 860, 330, 0, Hex(0xdfe5f7, 0.9f * k));
            if (l2[0]) ui.TextCentered(fonts::kHint, l2, 860, 368, 0.5f, Hex(0x7d87ad, 0.9f * k));
        }
        // v31: the boxes are 3D (DrawBoxes); here, under the chosen one, its title, then the console and where we are
        if (m_n && m_diveAt < 0 && m_launchT0 < 0 && m_returnT0 < 0 && m_ejectT0 < 0) {   // (v89: nor while a disc goes home)
            const Game &g = m_g[m_sel];
            const float a = k * Clamp01((now - m_openAt - 0.45f) / 0.35f) * (0.25f + 0.75f * Clamp01((now - m_moveAt) / 0.25f)) * (1.0f - 0.88f * popup::Shown());   // (v77: it steps back behind the question - v78: behind any popup)
            if (a > 0.003f) {
                // v77 (Yass: « ces petits textes sont trop subtils et tous dans la même police »): a real hierarchy - the title big
                // and clear in Outfit (the consoles' font) in a soft halo, then a small line in capitals, on a dark veil
                const bool big = !m_manage && ui.Width(fonts::kPageTitle, g.title, 1.0f) <= 640;
                const fonts::Style st = big ? fonts::kPageTitle : (ui.Width(fonts::kSetSel, g.title, 0) <= 600 ? fonts::kSetSel : fonts::kGameItem);
                const float tw = ui.Width(st, g.title, big ? 1.0f : 0), half = tw * 0.5f;
                float cx = m_selSx;
                if (cx - half < 330) cx = 330 + half;
                if (cx + half > 1240) cx = 1240 - half;
                const bool dim = g.archived || g.noData;
                if (!m_manage) {
                    ui.Veil(cx - half - 130, m_selSy + 8, cx + half + 130, m_selSy + 136, 1.25f * a, 34);
                    ui.Light(cx - half - 50, m_selSy + 22, cx + half + 50, m_selSy + 84, ps2::A(ps2::kHalo, (dim ? 0.10f : 0.24f) * a), true);
                }
                ui.TextCentered(st, g.title, cx, m_selSy + 30, big ? 1.0f : 0, dim ? ps2::A(ps2::kFaint, a) : ps2::A(ps2::kChosen, a));
                char up[128], info[192];
                if (m_manage) {                                 // v67: « Gérer » - its size, what it is made of, where it is
                    char big[48], s1[24], s2[24], s3[24], leg[160];
                    const bool busy = m_busy && m_askIdx == m_sel;
                    const char *busyTxt = m_busy == kJobArchive ? lang::T("Archivage\xe2\x80\xa6") : lang::T("Suppression\xe2\x80\xa6");
                    if (busy) snprintf(big, sizeof big, "%s", busyTxt);
                    else if (g.archived) snprintf(big, sizeof big, "%s", lang::T("Archiv\xc3\xa9"));
                    else if (!g.sized) snprintf(big, sizeof big, "\xe2\x80\xa6");
                    else SizeStr(g.size, big, sizeof big);
                    const float pulse = busy ? 0.55f + 0.45f * sinf(now * 6.0f) : 1.0f;
                    ui.TextCentered(fonts::kPageTitle, big, cx, m_selSy + 30 + 40, 1.5f, Hex(0xeef1ff, a * pulse));
                    const float bw = 360, bx = cx - bw / 2, by = m_selSy + 30 + 40 + 52;
                    ui.Shape(bx, by, bx + bw, by + 8, 0, 0, bw, 8, Rgba{ 0.78f, 0.83f, 0.96f, 0.10f * a }, 24, bw, 8, 4);
                    if (g.sized && g.size > 0 && !g.archived) {
                        static const uint32_t kSeg[3] = { 0x8ca0ff, 0xc8b6ff, 0x6fe0d0 };
                        float x = bx;
                        for (int s = 0; s < 3; s++) {
                            const float w = bw * (float)((double)g.part[s] / (double)g.size);
                            if (w < 0.5f) continue;
                            ui.Shape(x, by, x + w, by + 8, 0, 0, w, 8, Hex(kSeg[s], 0.95f * a), 24, w, 8, 4);
                            x += w;
                        }
                    }
                    if (m_console == 0 && g.sized && !g.archived) {
                        SizeStr(g.part[0], s1, sizeof s1); SizeStr(g.part[1], s2, sizeof s2); SizeStr(g.part[2], s3, sizeof s3);
                        snprintf(leg, sizeof leg, lang::T("Jeu %s   \xc2\xb7   Mise \xc3\xa0 jour %s   \xc2\xb7   Contenu %s"), s1, s2, s3);
                        ui.TextCentered(fonts::kHint, leg, cx, by + 18, 0.3f, Hex(0x9aa6d0, a));
                    } else if (g.archived) ui.TextCentered(fonts::kHint, "R\xc3\xa9installe-le depuis le PC pour rejouer", cx, by + 18, 0.3f, Hex(0x9aa6d0, a));
                    const char *where = g.where == 3 ? "Console et carte SD" : (g.where == 1 ? "Console" : "Carte SD");
                    snprintf(up, sizeof up, "%s", lang::T(where));
                    char upw[96]; Upper(up, upw, sizeof upw);
                    snprintf(info, sizeof info, "%s   \xc2\xb7   %d / %d", upw, m_sel + 1, m_n);
                    ui.TextCentered(fonts::kGameInfo, info, cx, by + 44, 3, Hex(0x8f9ac2, 0.85f * a));
                } else {
                if (m_console == 7 && g.sys[0]) {                 // v61: « PORT XBOX » (its original system)
                    char sys[64]; snprintf(sys, sizeof sys, "Port %s", g.sys);
                    Upper(sys, up, sizeof up);
                } else Upper(lang::T(c.name), up, sizeof up);
                (void)info;
                // v77: the line in pieces, each with its own colour - the console (faint), the place (soft white), and what
                // matters in its colour: « 3 EN LIGNE » green with its little light, « ARCHIVÉ » / missing files in pink
                char pos[32], flag[64] = "";
                snprintf(pos, sizeof pos, "%d / %d", m_sel + 1, m_n);
                Rgba flagCol = ps2::kGreen;
                const int players = g.tid ? nextendo::Players(g.tid) : -1;
                if (g.archived) { snprintf(flag, sizeof flag, "%s", lang::T("ARCHIV\xc3\x89")); flagCol = ps2::kPink; }
                else if (g.noData) { snprintf(flag, sizeof flag, "%s", lang::T("FICHIERS DU JEU MANQUANTS")); flagCol = ps2::kPink; }
                else if (m_sel == m_heldIdx && s_heldLive) { snprintf(flag, sizeof flag, "%s", lang::T("EN COURS")); flagCol = ps2::kChoice; }   // v89: its disc is in the console
                else if (players > 0) snprintf(flag, sizeof flag, lang::T("%d EN LIGNE"), players);
                const char *sep = "   \xc2\xb7   ";
                const float ls = 3, wUp = ui.Width(fonts::kGameInfo, up, ls), wSep = ui.Width(fonts::kGameInfo, sep, ls);
                const float wPos = ui.Width(fonts::kGameInfo, pos, ls), wFlag = flag[0] ? ui.Width(fonts::kGameInfo, flag, ls) + 18 : 0;
                const float total = wUp + wSep + wPos + (flag[0] ? wSep + wFlag : 0);
                const float ly = m_selSy + 30 + (big ? 54 : 42);
                float px = cx - total * 0.5f;
                ui.TextCentered(fonts::kGameInfo, up, px + wUp * 0.5f, ly, ls, ps2::A(ps2::kFaint, 0.95f * a)); px += wUp;
                ui.TextCentered(fonts::kGameInfo, sep, px + wSep * 0.5f, ly, ls, ps2::A(ps2::kFaint, 0.7f * a)); px += wSep;
                ui.TextCentered(fonts::kGameInfo, pos, px + wPos * 0.5f, ly, ls, ps2::A(ps2::kWords, 0.9f * a)); px += wPos;
                if (flag[0]) {
                    ui.TextCentered(fonts::kGameInfo, sep, px + wSep * 0.5f, ly, ls, ps2::A(ps2::kFaint, 0.7f * a)); px += wSep;
                    const float dx = px + 6, dy = ly + 9;   // its little light
                    ui.Light(dx - 10, dy - 10, dx + 10, dy + 10, ps2::A(flagCol, 0.7f * a * (0.75f + 0.25f * sinf(now * 3.0f))), true);
                    ui.Shape(dx - 3, dy - 3, dx + 3, dy + 3, 0, 0, 6, 6, ps2::A(flagCol, a), 24, 6, 6, 3);
                    ui.TextCentered(fonts::kGameInfo, flag, px + 18 + (wFlag - 18) * 0.5f, ly, ls, ps2::A(flagCol, a));
                }
                }
            }
        }
        {   // v87 - covers on their way (Covers.hpp): the orbiting lights turn on the chosen box while it still has none
            static busy::Fade s_cov;
            const bool waiting = covers::Busy() && m_n && m_g[m_sel].noCover && m_diveAt < 0 && m_launchT0 < 0 && m_returnT0 < 0 && m_ejectT0 < 0 && !m_manage;
            const float ck = s_cov.Step(waiting, now, 0.3f) * k * (1.0f - popup::Shown());
            for (int h = 0; h < m_nhit && ck > 0.003f; h++)
                if (m_hit[h].i == m_sel) {
                    const float cx = (m_hit[h].x0 + m_hit[h].x1) * 0.5f, cy = (m_hit[h].y0 + m_hit[h].y1) * 0.5f;
                    busy::Orbs(ui, cx, cy, fminf(34.0f, (m_hit[h].x1 - m_hit[h].x0) * 0.28f), ck, now);
                    break;
                }
        }
        // v67 - « Gérer »: the space of the console and the SD card (top right, the bars glide), the measuring, the question
        if (m_manage && k > 0.003f) {
            const char *const lbl[2] = { "Console", "Carte SD" };
            static const uint32_t kCol[2] = { 0xb48cff, 0x6fa8ff };
            for (int s = 0; s < 2; s++) {
                if (m_gTot[s] <= 0) continue;
                const float x0 = 1234 - 2 * 230 + s * 240, y0 = 96;
                char used[24], tot[24], v[64];
                SizeStr((u64)m_gShown[s], used, sizeof used); SizeStr((u64)m_gTot[s], tot, sizeof tot);
                snprintf(v, sizeof v, "%s / %s", used, tot);
                float px = x0;
                for (const char *q = lang::T(lbl[s]); *q;) px += ui.Glyph(fonts::kHint, NextCodepoint(q), px, y0, Hex(0x8f9ac2, k), 0, 0, 0, 0);
                const float vw = ui.Width(fonts::kHint, v, 0);
                float vx = x0 + 220 - vw;
                for (const char *q = v; *q;) vx += ui.Glyph(fonts::kHint, NextCodepoint(q), vx, y0, Hex(0xeef1ff, k), 0, 0, 0, 0);
                ui.Shape(x0, y0 + 24, x0 + 220, y0 + 30, 0, 0, 220, 6, Rgba{ 0.78f, 0.83f, 0.96f, 0.12f * k }, 24, 220, 6, 3);
                const float f = (float)fmin(1.0, m_gShown[s] / m_gTot[s]);
                if (f > 0.005f) ui.Shape(x0, y0 + 24, x0 + 220 * f, y0 + 30, 0, 0, 220 * f, 6, Hex(kCol[s], k), 24, 220 * f, 6, 3);
            }
            if (s_mgRunning && s_mgKind == kJobSizes)
                ui.TextCentered(fonts::kHint, "Calcul des tailles\xe2\x80\xa6", 1234 - 230, 140, 0.3f, Hex(0x8f9ac2, k * (0.55f + 0.45f * sinf(now * 4.0f))));
        }
        // the dive: the screen goes black (and comes back if the launch failed)
        const float blk = m_diveAt > 0 ? Clamp01((now - m_diveAt - Pace(0.2f)) / Pace(0.55f)) : Clamp01(1 - (now - m_diveOutAt) / Pace(0.6f));
        if (blk > 0.003f) ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0, 0, blk }, 24, 1280, 720, 0);
        // status (launching, errors) under the panel
        if (m_status[0]) {
            const float a = Clamp01(1 - (now - m_statusAt - 5.0f) / 0.6f) * k;
            if (a > 0.003f) ui.TextCentered(fonts::kHintBig, m_status, 760, 632, 0.5f, Hex(0xc9d3f5, a));
            (void)X; (void)Y; (void)W; (void)H;
        }
        // button hints, bottom left (actions) like the mockup (v28, +25 %: .hints bottom 28px, left 46px, gap 26px,
        // font 300 19px; .btn-g 27px, font 600 13.5px, margin-right 9px): « A Lancer   B Retour »
        float hx = 46;
        const float hc = 720 - 28 - 13.5f;                                 // the row's centre line
        const float kh = k * (1.0f - popup::Shown()) * (1.0f - blk);        // (v78: a popup's own hints take their place; v80: the black too)
        auto Hint = [&](const char *btn, const char *label) {   // v70 (the PS2 look): the button's letter in colour, no glass ring
            const uint32_t b = (uint32_t)btn[0];
            const Rgba bc = b == 'B' ? ps2::kPink : (b == 'Y' ? ps2::kGreen : ps2::kChoice);
            if (strchr("ABXYLR", (int)b)) hx += ui.Glyph(fonts::kButton, b, hx, hc - 10, ps2::A(bc, kh), 0, 0, 0, 0);
            else hx += ui.Glyph(fonts::kBtnSmall, b, hx, hc - 6.75f, ps2::A(bc, kh), 0, 0, 0, 0);
            hx += 10;
            for (const char *p = lang::T(label); *p;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(p), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * kh), 0, 0, 0, 0) + 0.5f;
            hx += 30;
        };
        if (m_manage) {                                   // v67
            if (m_console == 0 && m_n && !m_g[m_sel].archived) Hint("A", "Archiver");
            if (m_n) Hint("X", "Supprimer");
            if (m_n) Hint("Y", m_sortBy ? "Trier : nom" : "Trier : taille");
            Hint("B", "Retour");
        } else {
            const bool held = m_n && m_sel == m_heldIdx && s_heldLive;   // v89: its disc is in the console - A resumes it, X closes it
            if (m_n) Hint("A", held ? "Reprendre" : "Lancer");
            if (held) Hint("X", "Fermer");
            if (m_n) Hint("-", "G\xc3\xa9rer");
            Hint("B", "Retour");
        }
    }


    // ======================= v31 - the boxes =======================
    // Yass: « comme dans une bibliothèque, sauf qu'au lieu de voir juste la tranche, on voit les jeux penchés dans le
    // même sens que la console mais pas trop, et derrière une boîte on voit une partie de celle derrière ». So: the
    // boxes stand on an invisible shelf to the right of the console, all turned the same way as it (front towards the
    // middle, a bit less), each one set back and to the right of the previous: about half of every cover shows behind
    // the one in front. The chosen one comes forward and turns a little more towards us. They come out standing up one
    // after the other (dominoes), fall back when we leave, and the launched one flies into the console.
    namespace {
        // real sizes (mm): height, depth, default front proportions (the cover's own win), the case's plastic,
        // the kind box_fsh.glsl dresses (0 DVD case, 1 jewel case, 2 UMD case, 3 Switch case, 4 cardboard), the band
        struct BoxLook { float h, d, aspect; uint32_t plastic; int kind; float band; uint32_t bandCol; };
        const BoxLook kBox[consoles::kMax] = {
            { 170, 11, 0.618f, 0x9c1020, 3, 0.075f, 0xd8101c },   // Switch: red case, red band on top
            { 190, 14, 0.705f, 0x0e1119, 0, 0, 0 },               // PlayStation 2: DVD case
            { 190, 14, 0.705f, 0x101013, 0, 0, 0 },               // GameCube
            { 155, 38, 1.400f, 0x26262c, 4, 0, 0 },               // Nintendo 64: cardboard box
            { 125, 10, 1.000f, 0x5d6475, 1, 0, 0 },               // PlayStation: jewel case
            { 183, 14, 0.570f, 0x151821, 2, 0, 0 },               // PSP: UMD case
            { 145, 32, 1.000f, 0x26262c, 4, 0, 0 },               // Game Boy Advance: cardboard box
            { 190, 14, 0.705f, 0x0e1119, 0, 0, 0 },               // ports
            // v93: the new consoles (real sizes, mm)
            { 125, 14, 0.880f, 0x14161c, 0, 0, 0 },               // Nintendo DS: its small case
            { 130, 32, 1.380f, 0x26262c, 4, 0, 0 },               // Super Nintendo: cardboard box (PAL / Japan, landscape)
            { 178, 30, 0.720f, 0x26262c, 4, 0, 0 },               // NES: cardboard box
            { 186, 22, 0.700f, 0x101114, 0, 0, 0 },               // Mega Drive: its plastic case
            { 128, 26, 0.950f, 0x26262c, 4, 0, 0 },               // Game Boy: cardboard box
            { 186, 22, 0.700f, 0x101114, 0, 0, 0 },               // Master System: plastic case
            { 170, 22, 0.700f, 0x101114, 0, 0, 0 },               // Game Gear: plastic case
        };
        // v32 - what is inside: 0 a disc (the disc model), 1 a cartridge, 2 a UMD (the box model); size in mm (disc:
        // diameter), its plastic, the disc's data side (0 silver, 1 black PS1, 2 blue PS2)
        struct MediaLook { int type; float mm[3]; uint32_t plastic; int variant; };
        const MediaLook kMedia[consoles::kMax] = {
            { 1, { 21, 31, 3.3f }, 0xd2d5dc, 0 },        // Switch: a game card, its label = the icon
            { 0, { 120, 1.2f, 1.2f }, 0, 2 },            // PlayStation 2: a DVD
            { 0, { 80, 1.2f, 1.2f }, 0, 0 },             // GameCube: a mini disc
            { 1, { 116, 75, 19 }, 0x7c7f88, 0 },         // Nintendo 64: a cartridge
            { 0, { 120, 1.2f, 1.2f }, 0, 1 },            // PlayStation: a black CD
            { 2, { 64, 65, 4.2f }, 0x15171c, 0 },        // PSP: a UMD
            { 1, { 57, 35, 8 }, 0x34363e, 0 },           // Game Boy Advance: a cartridge
            { 0, { 120, 1.2f, 1.2f }, 0, 0 },            // ports
            { 1, { 35, 33, 4 }, 0x2a2c33, 0 },           // Nintendo DS: a game card
            { 1, { 108, 80, 18 }, 0x8a8d95, 0 },         // Super Nintendo: a cartridge
            { 1, { 120, 105, 20 }, 0x6e7078, 0 },        // NES: a cartridge
            { 1, { 110, 85, 20 }, 0x1a1a1e, 0 },         // Mega Drive: a cartridge
            { 1, { 57, 65, 8 }, 0x8f9196, 0 },           // Game Boy: a cartridge
            { 1, { 100, 70, 18 }, 0x1a1a1e, 0 },         // Master System: a cartridge
            { 1, { 68, 54, 10 }, 0x1a1a1e, 0 },          // Game Gear: a cartridge
        };
        // where it goes, in the console model's own space (longest side 2, tools/bake-assets.html; found from the models'
        // shapes - to adjust on the console): the slot, where it comes from (added), how far it goes in, its turn there
        // (around x), its size there (model units per mm)
        struct SlotLook { float pos[3], from[3], in[3], rotX, upm; float via[3]; bool hasVia, stay; };
        const SlotLook kSlot[consoles::kMax] = {
            { { -0.45f, 0.43f, 0.00f }, { 0, 0.50f, 0 },     { 0, -0.40f, 0 },   0.0f,     0.0084f,  {}, false, false },   // Switch: card slot, top left
            { { -0.20f, 0.08f, 0.63f }, { 0, 0, 0.75f },     { 0, 0, -1.00f },   0.0f,     0.0066f,  {}, false, false },   // PS2: the tray, in front
            // GameCube (v33: measured on the model - its disc bay is a recess of radius .40 at y .39, centred z .24): the
            // disc lands IN the bay and stays there, seen through the open top
            { {  0.00f, 0.395f, 0.24f }, { 0, 0.70f, 0 },    { 0, 0, 0 },        0.0f,     0.00975f, {}, false, true },
            // N64 (v33: the model already has a cartridge, .42 wide at y .17, z .31): ours, the same size, goes in there
            { {  0.00f, 0.17f, 0.31f }, { 0, 0.75f, 0 },     { 0, -0.35f, 0 },   0.0f,     0.0036f,  {}, false, false },
            { {  0.00f, 0.22f, 0.00f }, { 0, 0.75f, 0 },     { 0, -0.35f, 0 },   0.0f,     0.0074f,  {}, false, false },   // PS1: under the lid
            // PSP (v33: its back is .16 behind the centre along (0, -.71, -.71)): the UMD flies OVER the console, comes down
            // behind it and settles flat into the door (it never crosses the console)
            { {  0.00f, -0.12f, -0.12f }, { 0, -0.39f, -0.39f }, { 0, 0.011f, 0.011f }, 2.356f, 0.0118f, { 0, 1.5f, -1.0f }, true, true },
            { {  0.00f, -0.62f, 0.80f }, { 0, 0, 0.75f },    { 0, 0, -0.45f },  -1.5708f,  0.0156f,  {}, false, false },   // GBA SP: front edge of the base
            { {  0.00f, 0.00f, 0.60f }, { 0, 0, 0.75f },     { 0, 0, -0.90f },   0.0f,     0.0066f,  {}, false, false },   // ports
            // v93: the new consoles stand as blocks of glass for now (Wheel.cpp): into the middle of their front face
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0066f,  {}, false, false },   // DS
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0060f,  {}, false, false },   // Super Nintendo
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0060f,  {}, false, false },   // NES
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0060f,  {}, false, false },   // Mega Drive
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0080f,  {}, false, false },   // Game Boy
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0060f,  {}, false, false },   // Master System
            { {  0.00f, 0.05f, 0.30f }, { 0, 0, 0.75f },     { 0, 0, -0.45f },   0.0f,     0.0080f,  {}, false, false },   // Game Gear
        };
        bool HeldSlot(int console, float *out) {       // v89: the slot's mouth (the wheel's soft light goes there)
            if (!consoles::Valid(console)) return false;
            for (int k = 0; k < 3; k++) out[k] = kSlot[console].pos[k];   // (its mouth: inside, the light would be hidden)
            return true;
        }
        constexpr float kMM = 0.0084f;                 // world units per millimetre (a PS2 box: 1.6 tall)
        constexpr float kFloor = -0.05f;               // the shelf (the boxes' bottom)
        constexpr float kYaw = 0.40f, kYawSel = 0.16f; // turned like the console, not too much; the chosen one less
        float Smooth(float p) { p = Clamp01(p); return p * p * (3 - 2 * p); }
        // between two placements: position (+ an arc), turned axes (re-squared), sizes (geometric)
        m3::M4 Blend(const m3::M4 &a, const m3::M4 &b, float t, const float *lift) {
            float L[3], d[3][3];
            for (int c = 0; c < 3; c++) {
                float da[3], db[3], la = 0, lb = 0;
                for (int k = 0; k < 3; k++) { da[k] = a.m[c * 4 + k]; db[k] = b.m[c * 4 + k]; la += da[k] * da[k]; lb += db[k] * db[k]; }
                la = sqrtf(la); lb = sqrtf(lb);
                for (int k = 0; k < 3; k++) d[c][k] = da[k] / fmaxf(la, 1e-6f) * (1 - t) + db[k] / fmaxf(lb, 1e-6f) * t;
                L[c] = la > 1e-6f && lb > 1e-6f ? expf(logf(la) * (1 - t) + logf(lb) * t) : la * (1 - t) + lb * t;
            }
            auto norm = [](float *v) { const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-6f) for (int k = 0; k < 3; k++) v[k] /= l; return l > 1e-6f; };
            float x[3] = { d[0][0], d[0][1], d[0][2] }, y[3] = { d[1][0], d[1][1], d[1][2] }, z[3];
            if (!norm(x)) { x[0] = 1; x[1] = 0; x[2] = 0; }
            const float xy = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
            for (int k = 0; k < 3; k++) y[k] -= x[k] * xy;
            if (!norm(y)) { y[0] = 0; y[1] = 1; y[2] = 0; }
            z[0] = x[1] * y[2] - x[2] * y[1]; z[1] = x[2] * y[0] - x[0] * y[2]; z[2] = x[0] * y[1] - x[1] * y[0];
            if (z[0] * d[2][0] + z[1] * d[2][1] + z[2] * d[2][2] < 0) for (float &v : z) v = -v;
            m3::M4 r = m3::Ident();
            for (int k = 0; k < 3; k++) { r.m[k] = x[k] * L[0]; r.m[4 + k] = y[k] * L[1]; r.m[8 + k] = z[k] * L[2]; }
            const float arc = 4 * t * (1 - t);
            for (int k = 0; k < 3; k++) r.m[12 + k] = a.m[12 + k] * (1 - t) + b.m[12 + k] * t + (lift ? lift[k] * arc : 0);
            return r;
        }
        void LinHex(uint32_t hex, float *o) {
            for (int k = 0; k < 3; k++) {
                const float c = ((hex >> (16 - 8 * k)) & 255) / 255.0f;
                o[k] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
            }
        }
    }

    // one cover per frame: the chosen game first, then the next ones, then the two before
    void Games::Stream(Gpu &gpu) {
        m_shelf.NextFrame();
        {   // v75: a cover just downloaded by the console (Covers.hpp): the boxes still without one look again
            static int s_coverGen = 0;
            const int g = covers::Generation();
            if (g != s_coverGen) {
                s_coverGen = g;
                for (int i = 0; i < m_n; i++) m_g[i].noCover = false;
                if (m_console == 0 && m_open) m_shelf.Forget();   // v77: a Switch game's icon gives its place to its new box
            }
        }
        if (!m_open || !m_n || !m_shelf.Slots()) return;
        const Console &c = kConsoles[m_console];
        for (int dd = 0; dd < 9; dd++)
            for (int sg = 0; sg < (dd ? 2 : 1); sg++) {
                if (sg && dd > 2) continue;
                const int i = m_sel + (sg ? -dd : dd);
                if (i < 0 || i >= m_n || m_g[i].noCover || m_shelf.Has(i)) continue;
                char path[400];
                path[0] = 0;
                if (m_g[i].tid)                          // v77: a Switch game: GameTDB's box (Covers.cpp), else its icon
                    snprintf(path, sizeof path, "sdmc:/brume/jaquettes/switch/%016llX.btex", (unsigned long long)m_g[i].tid);
                if (!m_g[i].tid && c.folder) {           // sdmc:/roms/ps2/x.iso -> sdmc:/brume/jaquettes/ps2/x.btex
                    const char *file = strrchr(m_g[i].path, '/');
                    file = file ? file + 1 : m_g[i].path;
                    char base[300];
                    snprintf(base, sizeof base, "%s", file);
                    if (char *dot = strrchr(base, '.')) *dot = 0;
                    const int n = snprintf(path, sizeof path, "sdmc:/brume/jaquettes/%s/%s.btex", c.folder, base);
                    if (n <= 0 || n >= (int)sizeof path) { m_g[i].noCover = true; return; }
                }
                const char *icon = nullptr;
                if (m_console == 7) {                    // v42: sdmc:/switch/x/y.nro -> sdmc:/brume/jaquettes/ports/y.btex, else its icon
                    const char *file = strrchr(m_g[i].path, '/');
                    file = file ? file + 1 : m_g[i].path;
                    char base[200];
                    snprintf(base, sizeof base, "%s", file);
                    if (char *dot = strrchr(base, '.')) *dot = 0;
                    snprintf(path, sizeof path, "sdmc:/brume/jaquettes/ports/%s.btex", base);
                    icon = m_g[i].path;
                }
                const int r = m_shelf.Load(gpu, i, path, m_g[i].tid ? m_g[i].idx : -1, icon);
                if (r == 0) m_g[i].noCover = true;
                if (r == 1) Log("jeux : jaquette %d (%s)", i, m_g[i].title);
                return;                                  // one per frame (r == -1: no free place yet, next frame)
            }
    }

    int Games::BoxAt(float x, float y) const {
        for (int k = 0; k < m_nhit; k++) {
            const Hit &h = m_hit[k];
            if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) return h.i;
        }
        return -1;
    }

    // ---- v33: the staging's timeline (Yass: « trop mécanique » -> one continuous journey, overlapping phases) ----
    float Games::LaunchIn() const { return JourneyStart(m_console) + 2.25f; }   // (the media has settled by then)
    void Games::StartReturn(float now) {
        m_returnT0 = now; m_launchT0 = -100; m_evt = 0;
        // the other boxes stand up again at the end (dominoes) - v89: still running, only the box goes back: they stand up sooner
        const float stand = m_mode == kKeep ? (m_fromResume ? 0.15f : 0.75f) : 2.6f;
        m_openAt = now + Pace(stand) - Pace(0.28f); m_domino = 0;
    }
    // v89 - the disc goes home: its box comes for it (it rises from its place to the front), the disc leaves the console and
    // flies into it, the lid closes, the box goes back to its place - the way back from a game, with the box's own way first
    void Games::StartEject(float now) {
        m_ejectT0 = now; m_evt = 0; m_mode = kEject; m_fromResume = false;
        audio::PlaySweep(audio::Whoosh, 0.2f, -0.1f, 0.5f, 0.45f);   // the box comes to the front
        audio::Bell(audio::HarmonyNote(1, 69, 81), 0.03f, true, 0.05f);
        haptic::Tick(0.5f);
    }
    // the media's way home - a click, a soft low, the flight back, sparkles, the bells of the chord, the lid, back on the shelf
    void Games::HomeSounds(float r) {
        const bool card = kBoxKind(m_console) == 4;
        auto once = [&](int bit, float at) { if (r >= at && !(m_evt & (1u << bit))) { m_evt |= 1u << bit; return true; } return false; };
        auto pitchOf = [](int midi, int ref) { return powf(2.0f, (midi - ref) / 12.0f); };
        if (once(16, 0.0f)) {                                                  // it unseats: a click, a soft low
            audio::Play(audio::Tick2400, -0.7f, 0.7f); audio::Thock(-0.7f, 0.95f, 0.07f);
            audio::SoftLow(audio::HarmonyNote(0, 43, 54), 0.12f, 0.6f);
            if (m_ejectT0 > 0 || !audio::Has(audio::SortieTon)) audio::Play((audio::Sound)(audio::Swell0 + consoles::kDefs[m_console].voice), -0.3f, 0.25f);   // (v80: back from a game the exit sound's tone sings instead)
            haptic::Tap(0.6f);
        }
        if (once(17, 0.20f)) audio::PlaySweep(audio::Whoosh, -0.85f, 0.1f, 1.1f, 0.6f);           // the flight back
        if (once(18, 0.60f)) {                                                 // sparkles, coming down
            audio::Play(audio::PlinkH84, -0.3f, 0.30f, pitchOf(audio::HarmonyNote(2, 84, 96), 84));
            audio::Play(audio::PlinkH84, -0.1f, 0.25f, pitchOf(audio::HarmonyNote(0, 84, 96), 84), 0.12f);
        }
        if (once(19, 1.60f)) {                                                 // home: it settles in its box
            for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(2 - k, 67, 81), 0.045f - k * 0.008f, true, k * 0.10f);
            haptic::Tick(0.5f);
        }
        if (!card && once(20, 1.95f)) audio::PlaySweep(audio::Whoosh, -0.15f, 0.05f, 0.45f, 0.2f);   // the lid swings
        if (!card && once(21, 2.40f)) { audio::Thock(0.0f, 1.55f, 0.07f); audio::Play(audio::Tick1700, 0.0f, 0.7f); haptic::Tap(0.55f); }   // clac
        if (once(22, 2.45f)) audio::PlaySweep(audio::Whoosh, 0.0f, 0.3f, 0.6f, 0.25f);            // back on the shelf
    }
    // v89 - the held box at its place: open (empty). Chosen, or among the ones passed (on the left: the boxes behind it are further
    // left, its lid swings out in front of them), wide open; among the next ones only ajar - the box in front of it stands right
    // over its hinge (its lid must never go through it). Cardboard boxes have no lid: their cartridge just is not there.
    float Games::HeldLid(float kk) const {
        if (kBoxKind(m_console) == 4) return 0;
        const float r = Clamp01(kk), sr = r * r * (3 - 2 * r), l = Clamp01(-kk);
        return kHeldOpen * (1.0f - 0.90f * sr) * (1.0f - 0.25f * l);
    }
    // front: the chosen box from its shelf (0) to the front (1); lid: how open (radians); media: the journey of the disc /
    // cartridge (0 in its box .. 1 in the console, already eased: slow out of the box, fast in the air, slowing down into
    // the slot); spin: the disc's turn (no speed at both ends)
    void Games::Stage(float now, float &front, float &lid, float &media, float &spin) const {
        front = lid = media = spin = 0;
        const bool card = kBoxKind(m_console) == 4;
        const float open = card ? 0.0f : 1.95f;
        // v34 (Yass): ease out expo for everything - the disc pops out of its case (it brakes), then flies to the console
        // and slides in, slower and slower (the second expo overlaps the first: no stop in between)
        if (m_launchT0 > 0 && m_mode == kResume) {        // v89: the same game again - its box stays at its place, open; its disc is in
            lid = open; media = 1;
        } else if (m_launchT0 > 0) {
            const float t = (now - m_launchT0) * kPace, tJ = JourneyStart(m_console);
            front = EaseGlide(t / 1.0f);
            lid = open * EaseGlide((t - 0.45f) / 0.85f);
            media = 0.18f * EaseGlide((t - tJ) / 0.8f) + 0.82f * EaseGlide((t - tJ - 0.45f) / kJourney);
        } else if (m_returnT0 > 0 && m_mode == kKeep) {   // v89: still running - the disc stays in the console, the box goes back open
            const float r = (now - m_returnT0) * kPace;
            lid = open; media = 1;
            front = m_fromResume ? 0.0f : 1 - EaseGlide((r - 0.10f) / 1.0f);
        } else if (m_returnT0 > 0) {
            const float r = (now - m_returnT0) * kPace;   // v36: the whole staging, livelier
            media = 1 - 0.82f * EaseGlide(r / kJourney) - 0.18f * EaseGlide((r - 1.4f) / 0.8f);
            lid = open * (1 - EaseGlide((r - 1.95f) / 0.85f));
            front = 1 - EaseGlide((r - 2.45f) / 1.0f);
        } else if (m_ejectT0 > 0) {                       // v89: the box comes for its disc, the way back, the box goes back
            const float r = (now - m_ejectT0) * kPace, re = r - kEjectLead;
            media = 1 - 0.82f * EaseGlide(re / kJourney) - 0.18f * EaseGlide((re - 1.4f) / 0.8f);
            lid = open * (1 - EaseGlide((re - 1.95f) / 0.85f));
            front = fminf(EaseGlide(r / 0.9f), 1 - EaseGlide((re - 2.45f) / 1.0f));
        }
        spin = media * 2.2f * 6.2831853f;
    }

    void Games::DrawBoxes(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &wheel) {
        m_nhit = 0;
        if (!m_shelf.Ready() || !m_n || !Visible(now)) return;
        using namespace m3;
        const BoxLook &L = kBox[m_console];
        float eye[3], at[3];
        wheel.CameraAt(now, eye, at);
        const M4 vp = Mul(Persp(38, (float)gpu.Width() / gpu.Height(), 0.3f, 300), LookAt(eye, at));
        // v32 - Cover Flow (Yass): the chosen box in the middle, a free space on each side of it, then the others in a
        // tight stack - to the right the next ones, to the LEFT the ones we passed (still seen) - all turned the same way
        // as the console. All the boxes are parallel and each step goes back at least a box's thickness, and the chosen
        // one keeps a box's width of free space around it: moving along, two boxes can never go through each other.
        const float W0 = fminf(2.1f, L.h * L.aspect * kMM), D0 = L.d * kMM;
        const float gap = 1.10f * W0, stepR = 0.42f * W0, stepL = 0.30f * W0, vSel = 0.55f, vStep = fmaxf(0.30f, D0 + 0.08f);
        const float ca = cosf(kYaw), sa = sinf(kYaw);
        // v77 (Yass: « ça colle » à la console): the chosen one stands further right (x 1.05 instead of .75), and the ones we
        // passed fade out sooner, before they reach the console
        const float baseX = 1.05f - vSel * sa, baseZ = 1.0f - vSel * ca;
        float front, lid, media, spin;
        Stage(now, front, lid, media, spin);
        const bool staging = m_launchT0 > 0 || m_returnT0 > 0 || m_ejectT0 > 0;
        const float fe = front;
        // v89: the held game's box (its disc in the console) - open at its place; staged (kept / resumed / ejected): its lid goes
        // from that opening to the staging's as it comes to the front (openness q: the staging's own, 0 once it has closed)
        const bool heldStage = staging && (m_mode == kKeep || m_mode == kResume || m_mode == kEject);
        const float openK = kBoxKind(m_console) == 4 ? 0.0f : 1.95f, q = openK > 0 ? Clamp01(lid / openK) : 0.0f;
        struct Box { int i; M4 frame; float w, h, d, sel, light, dist; const Target *tex; float aspect; bool staged; float lid; };
        Box list[16];
        int nb = 0;
        for (int i = 0; i < m_n && nb < 16; i++) {
            const float kk = i - m_scroll + m_g[i].shift;   // (v77: + where it was, gliding back: sorts and deletions move)
            const bool staged = staging && i == m_stage;
            if ((kk < -3.2f || kk > 7.5f) && !staged) continue;   // (v89: the staged one may come from far: the held box, for its disc)
            // standing up (dominoes) / falling back (leaving, or another one is launched)
            const int j = i - (m_sel < i ? m_sel : i);
            float pitch = 0, slide = 0;
            if (!staged) {
                if (m_open && m_launchT0 < 0) {
                    const float p = Clamp01((now - m_openAt - Pace(0.28f) - (j > 12 ? 12 : j) * Pace(0.10f)) / Pace(0.75f));
                    if (p <= 0) continue;
                    pitch = -1.42f * (1 - EaseOutExpo(p));        // v34: ease out expo
                    slide = -0.35f * (1 - EaseOutExpo(p));
                } else {
                    const float t0 = m_launchT0 > 0 ? m_launchT0 : m_closeAt;
                    const float order = m_launchT0 > 0 ? 0 : fmaxf(0.0f, fminf(10.0f, 7.0f - kk));   // the far ones first
                    const float q = Clamp01((now - t0 - order * Pace(0.022f)) / Pace(0.42f));
                    if (q >= 1) continue;
                    pitch = -1.42f * EaseOutExpo(q);
                }
            }
            // its place in the Cover Flow
            const float ak = fabsf(kk), sg = kk < 0 ? -1.0f : 1.0f;
            const float u = ak <= 1 ? kk * gap : sg * (gap + (ak - 1) * (kk < 0 ? stepL : stepR));
            const float v = vSel * fmaxf(0.0f, 1 - ak) - vStep * ak;
            float x = baseX + ca * u + sa * v + slide, z = baseZ - sa * u + ca * v;
            float sc = kk < -1.7f ? Clamp01((kk + 2.5f) / 0.8f) : 1.0f;   // the oldest ones fade out before the console
            if (staged) sc += (1 - sc) * fe;              // (v89: one from far is whole by the time it is in front)
            if (sc < 0.02f) continue;
            const float s = Clamp01(1 - ak);
            float lidB = staged ? lid : 0.0f;             // v89: how open it stands
            if (staged && heldStage) lidB = q * (HeldLid(kk) * (1 - fe) + openK * fe);
            else if (!staged && i == m_heldIdx) lidB = HeldLid(kk);
            float y = kFloor + 0.03f * s + sinf(now * 1.3f) * 0.010f * s, yaw = kYaw;
            // the size: the console's real height, the cover's own proportions (no distortion)
            float aspect = L.aspect;
            const Target *tex = m_shelf.Cover(i, &aspect);
            const float coverAspect = aspect;
            if (m_console == 0) aspect = L.aspect;       // Switch: the case's shape (its icon is square; v93: a real box is fitted in by the shader)
            float h = L.h * kMM, w = h * aspect;
            const float d = D0;
            if (w > 2.1f) { h *= 2.1f / w; w = 2.1f; }
            if (staged) {                                // the staged box: to the front, facing us, a little towards the console
                x += (0.30f - x) * fe; y += (kFloor + 0.20f - y) * fe; z += (2.65f - z) * fe; yaw += (-0.30f - yaw) * fe;
                if (m_diveAt > 0) sc *= 1 - EaseGlide((now - m_diveAt) / Pace(0.6f));   // the dive: it fades away
                if (sc < 0.02f) continue;
            }
            if (i == m_goneIdx && m_goneAt > 0) {         // v67: deleted - it tips back, drops and fades
                const float q = Clamp01((now - m_goneAt) / 0.55f);
                y -= 0.45f * q * q; pitch += 0.9f * q; sc *= 1 - q;
                if (sc < 0.02f) continue;
            }
            const M4 frame = Mul(Mul(Mul(Translate(x, y, z), RotY(yaw)), RotX(pitch)), Mul(Scale3(sc, sc, sc), Translate(0, h * 0.5f, 0)));
            const float flash = i == m_flashIdx && m_flashAt > 0 ? 1.0f + 1.4f * (1 - Clamp01((now - m_flashAt) / 0.7f)) : 1.0f;   // v67: archived
            const float light = flash * (m_g[i].archived ? 0.45f : 1.0f) * (kk > 0 ? 1 - fminf(kk, 6.0f) * 0.07f : 1 + fmaxf(kk, -3.0f) * 0.07f);
            const float cy = y + h * 0.5f * sc, dx = x - eye[0], dy = cy - eye[1], dz = z - eye[2];
            list[nb++] = { i, frame, w, h, d, staged ? 0.0f : s, light, dx * dx + dy * dy + dz * dz, tex, coverAspect, staged, lidB };
            (void)pitch;
        }
        if (!nb) return;
        for (int p = 1; p < nb; p++)                     // front to back (the depth test skips what is hidden)
            for (int q = p; q > 0 && list[q].dist < list[q - 1].dist; q--) { const Box t = list[q]; list[q] = list[q - 1]; list[q - 1] = t; }

        if (!gpu.MeshDrawn()) gpu.BeginMesh();
        MeshUbo u = Wheel::s_lights;
        u.cam[0] = eye[0]; u.cam[1] = eye[1]; u.cam[2] = eye[2]; u.cam[3] = 1.0f;
        float plastic[3], bandCol[3];
        LinHex(L.plastic, plastic); LinHex(L.bandCol, bandCol);
        auto draw = [&](const M4 &model, const Target *tex, int part, float sel, float light, float w, float h, float d, bool disc) {
            memcpy(u.model, model.m, 64);
            memcpy(u.mvp, Mul(vp, model).m, 64);
            gpu.SetMeshUniforms(&u, sizeof u);
            const Target *ts[4] = { tex ? tex : &a.env, &a.env, &haze, &a.env };
            gpu.BindTextures(ts, 4, false);
            if (disc) gpu.DrawMesh(m_shelf.DiscVtx(), Shelf::kDiscVtxBytes, m_shelf.DiscIdx(), Shelf::kDiscIdxCount);
            else gpu.DrawMesh(m_shelf.Vtx(), Shelf::kVtxBytes, m_shelf.Idx(), Shelf::kIdxCount);
            (void)part; (void)sel; (void)light; (void)w; (void)h; (void)d;
        };
        auto boxUniforms = [&](const Box &b, int part, float lightW) {
            memcpy(u.base, plastic, 12); u.base[3] = 1.0f + part;
            // v93: a Switch game's real box (a portrait .btex from GameTDB, not its square icon): 2 + its proportions, the
            // shader fits it whole on the front (box_fsh.glsl: boxArt)
            u.mat[0] = !b.tex ? 0.0f : (L.kind == 3 && b.aspect > 0.3f && b.aspect < 0.9f ? 2.0f + b.aspect : 1.0f);
            u.mat[1] = (float)L.kind; u.mat[2] = b.sel; u.mat[3] = lightW;
            u.tint[0] = L.band; u.tint[1] = b.w; u.tint[2] = b.h; u.tint[3] = b.d;
            memcpy(u.emi, bandCol, 12); u.emi[3] = kFloor;
        };
        gpu.SetMeshState(true, false, false);
        const Box *stagedBox = nullptr;
        for (int k = 0; k < nb; k++) {                   // 1. the boxes (the staged one may be open: tray + lid)
            const Box &b = list[k];
            if (b.staged) stagedBox = &b;
            if (b.lid > 0.002f) {                         // open: the tray, and the lid on its hinge (v89: the held box too)
                boxUniforms(b, 1, b.light);
                draw(Mul(b.frame, Mul(Translate(0, 0, -0.1f * b.d), Scale3(b.w, b.h, 0.8f * b.d))), b.tex, 1, 0, 0, 0, 0, 0, false);
                boxUniforms(b, 2, b.light);
                const M4 lidM = Mul(Mul(Mul(b.frame, Translate(-b.w * 0.5f, 0, b.d * 0.5f)), RotY(-b.lid)),
                                    Mul(Translate(b.w * 0.5f, 0, -0.1f * b.d), Scale3(b.w, b.h, 0.2f * b.d)));
                draw(lidM, b.tex, 2, 0, 0, 0, 0, 0, false);
            } else {
                boxUniforms(b, 0, b.light);
                draw(Mul(b.frame, Scale3(b.w, b.h, b.d)), b.tex, 0, 0, 0, 0, 0, 0, false);
            }
            if (!b.staged && m_nhit < 16 && b.sel >= 0) {    // where it is on screen (the finger, the title)
                float o[3], t[3], sx0, sy0, sx1, sy1;
                const float bot[3] = { 0, -b.h * 0.5f, 0 }, top[3] = { 0, b.h * 0.5f, 0 };
                Xform(b.frame, bot, o); Xform(b.frame, top, t);
                if (Project(vp, o, sx0, sy0) && Project(vp, t, sx1, sy1) && fabsf(sy0 - sy1) > 20) {
                    const float half = fabsf(sy0 - sy1) * (b.w / b.h) * cosf(kYaw) * 0.5f;
                    m_hit[m_nhit++] = { b.i, sx0 - half, sy1, sx0 + half, sy0 };
                    if (b.i == m_sel && !staging) { m_selSx = sx0; m_selSy = sy0; }
                }
            }
        }
        // 2. v32 - the disc / cartridge of the staged game: in its box, rising, flying to the console's slot, going in
        // v89: the held game's (still running) rests in its console - seen where the console shows it (GameCube, PSP)
        Box heldMedia;
        const bool heldRest = !stagedBox && m_heldIdx >= 0 && kSlot[m_console].stay;
        if (heldRest) {
            float asp = L.aspect;
            heldMedia.tex = m_shelf.Cover(m_heldIdx, &asp); heldMedia.aspect = asp;
            heldMedia.frame = Ident(); heldMedia.w = heldMedia.h = heldMedia.d = 1;
            media = 1; lid = 0; spin = 2.2f * 6.2831853f;   // (where the launch left it)
        }
        if ((stagedBox || heldRest) && (media > 0.0f || lid > 0.3f) && (media < 0.999f || kSlot[m_console].stay)) {
            const Box &b = stagedBox ? *stagedBox : heldMedia;
            const MediaLook &ML = kMedia[m_console];
            const SlotLook &SL = kSlot[m_console];
            const bool disc = ML.type == 0;
            const float mw = ML.mm[0] * kMM, mh = (disc ? ML.mm[0] : ML.mm[1]) * kMM, md = (disc ? ML.mm[0] : ML.mm[2]) * kMM;
            const M4 sizeBox = disc ? Scale3(mw, mw, mw) : Scale3(mw, mh, md);
            const M4 turnBox = disc ? RotX(kPi * 0.5f) : Ident();      // a disc lies in the case (its face towards us)
            M4 inBox, risen;
            if (kBoxKind(m_console) == 4) {                            // cardboard: it comes out of the top
                inBox = Mul(b.frame, Mul(Translate(0, 0, 0), Mul(turnBox, sizeBox)));
                risen = Mul(b.frame, Mul(Translate(0, b.h * 0.5f + mh * 0.5f + 0.25f, 0.15f), Mul(turnBox, sizeBox)));
            } else {
                const float zin = 0.3f * b.d + (disc ? 0.004f : md * 0.5f);
                inBox = Mul(b.frame, Mul(Translate(0, 0, zin), Mul(turnBox, sizeBox)));
                risen = Mul(b.frame, Mul(Translate(0, 0.05f, zin + 0.55f), Mul(turnBox, sizeBox)));
            }
            const float upm = SL.upm;
            const M4 sizeCon = disc ? Scale3(ML.mm[0] * upm, ML.mm[0] * upm, ML.mm[0] * upm) : Scale3(ML.mm[0] * upm, ML.mm[1] * upm, ML.mm[2] * upm);
            const M4 &CM = Wheel::s_selModel;
            const M4 approach = Mul(CM, Mul(Translate(SL.pos[0] + SL.from[0], SL.pos[1] + SL.from[1], SL.pos[2] + SL.from[2]), Mul(RotX(SL.rotX), sizeCon)));
            const M4 inside = Mul(CM, Mul(Translate(SL.pos[0] + SL.in[0], SL.pos[1] + SL.in[1], SL.pos[2] + SL.in[2]), Mul(RotX(SL.rotX), sizeCon)));
            // v33: ONE smooth journey (Yass: « trop mécanique ») - a spline through the box, risen, a point above (or
            // behind, for the PSP), the slot's mouth and the inside; the turn and the size change all along the way
            constexpr float kT[5] = { 0.0f, 0.18f, 0.50f, 0.78f, 1.0f };
            float K[5][3];
            for (int k = 0; k < 3; k++) { K[0][k] = inBox.m[12 + k]; K[1][k] = risen.m[12 + k]; K[3][k] = approach.m[12 + k]; K[4][k] = inside.m[12 + k]; }
            if (SL.hasVia) { float o[3]; Xform(CM, SL.via, o); for (int k = 0; k < 3; k++) K[2][k] = o[k]; }
            else { const float lift[3] = { 0, 0.9f, 0.35f }; for (int k = 0; k < 3; k++) K[2][k] = (K[1][k] + K[3][k]) * 0.5f + lift[k]; }
            float tan[5][3];
            for (int k = 0; k < 3; k++) {
                tan[0][k] = (K[1][k] - K[0][k]) / (kT[1] - kT[0]);
                for (int q = 1; q < 4; q++) tan[q][k] = (K[q + 1][k] - K[q - 1][k]) / (kT[q + 1] - kT[q - 1]);
                tan[4][k] = (K[4][k] - K[3][k]) / (kT[4] - kT[3]);
            }
            const float uu = fminf(1.0f, fmaxf(0.0f, media));
            int sgI = 0;
            while (sgI < 3 && uu > kT[sgI + 1]) sgI++;
            const float dT = kT[sgI + 1] - kT[sgI], s1 = (uu - kT[sgI]) / dT, s2 = s1 * s1, s3 = s2 * s1;
            const float h00 = 2 * s3 - 3 * s2 + 1, h10 = s3 - 2 * s2 + s1, h01 = -2 * s3 + 3 * s2, h11 = s3 - s2;
            M4 mm;
            if (uu <= kT[1]) mm = Blend(inBox, risen, Smooth(uu / kT[1]), nullptr);
            else if (uu < kT[3]) mm = Blend(risen, approach, Smooth((uu - kT[1]) / (kT[3] - kT[1])), nullptr);
            else mm = Blend(approach, inside, 1.0f, nullptr);
            for (int k = 0; k < 3; k++)
                mm.m[12 + k] = h00 * K[sgI][k] + h10 * dT * tan[sgI][k] + h01 * K[sgI + 1][k] + h11 * dT * tan[sgI + 1][k];
            if (disc) mm = Mul(mm, RotY(spin));                        // the disc turns on itself
            memcpy(u.base, ML.plastic ? plastic : plastic, 12);
            LinHex(ML.plastic, u.base); u.base[3] = 1;
            u.mat[0] = b.tex ? 1.0f : 0.0f; u.mat[1] = disc ? 5.0f : (ML.type == 2 ? 7.0f : 6.0f); u.mat[2] = (float)ML.variant; u.mat[3] = 1;
            u.tint[0] = b.aspect; u.tint[1] = mw; u.tint[2] = mh; u.tint[3] = md;
            u.emi[0] = mm.m[12]; u.emi[1] = mm.m[13]; u.emi[2] = mm.m[14]; u.emi[3] = kFloor;
            gpu.SetMeshState(!disc, false, false);                     // (a disc: both sides, it is thin)
            draw(mm, b.tex, 0, 0, 0, 0, 0, 0, disc);
            gpu.SetMeshState(true, false, false);
        }
        // 3. the reflections on the shelf (see-through, the mirrored copy turns the other way)
        gpu.SetMeshState(true, true, true);
        for (int k = nb - 1; k >= 0; k--) {
            const Box &b = list[k];
            if (b.staged && front > 0.02f) continue;
            const M4 model = Mul(Mul(Translate(0, 2 * kFloor, 0), Scale3(1, -1, 1)), Mul(b.frame, Scale3(b.w, b.h, b.d)));
            if (model.m[13] < kFloor - 0.6f * b.h) {}   // (always drawn: the shader fades it below the floor)
            boxUniforms(b, 0, -0.32f * b.light);
            draw(model, b.tex, 0, 0, 0, 0, 0, 0, false);
        }
        gpu.SetMeshState(true, false, false);
    }

} // namespace brume
