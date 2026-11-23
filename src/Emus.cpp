// Brume - v90: the emulators found by themselves; v93: every console of Consoles.hpp, RetroArch's cores, Tico's modules.
// See Emus.hpp.
#include "Emus.hpp"
#include "Consoles.hpp"
#include "Notify.hpp"
#include "Lang.hpp"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

extern "C" int qext_game_running(void);

namespace brume::emus {

    namespace {
        using consoles::kDefs;
        constexpr int kN = consoles::kCount;
        constexpr const char *kKnownFile = "sdmc:/brume/emulateurs-detectes.txt";
        constexpr const char *kEmuIni    = "sdmc:/brume/emulateurs.ini";

        void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
        void Log(const char *fmt, ...) {
            if (!s_log) return;
            char buf[300];
            va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
            s_log(buf);
        }

        // a name the forgiving way: lower case, letters and digits only (« NetherSX2_nx » -> « nethersx2nx »)
        void Norm(const char *in, char *out, int cap) {
            int n = 0;
            for (const unsigned char *p = (const unsigned char *)in; *p && n < cap - 1; p++)
                if (isalnum(*p)) out[n++] = (char)tolower(*p);
            out[n] = 0;
        }
        bool EndsWith(const char *s, const char *end) {
            const size_t n = strlen(s), m = strlen(end);
            return n >= m && strcasecmp(s + n - m, end) == 0;
        }
        void Cpy(char *d, size_t cap, const char *s) {             // a bounded copy (always ends)
            size_t n = 0;
            while (s[n] && n + 1 < cap) { d[n] = s[n]; n++; }
            if (cap) d[n] = 0;
        }
        bool IsRa(const char *norm) { return strstr(norm, "retroarch") || strstr(norm, "libretro"); }
        // the console whose games' folder is called that (normalised), -1 none
        int ByAlias(const char *norm, bool raOnly) {
            if (!norm[0]) return -1;
            for (int c = 0; c < kN; c++) {
                if (!consoles::Emulated(c) || (raOnly && !kDefs[c].ra)) continue;
                for (const char *a : kDefs[c].alias) if (a && !strcmp(a, norm)) return c;
            }
            return -1;
        }

        // ---- what the scan found, per console ----
        struct Pick {
            char path[160]; char name[32]; int prio;
            char tmpl[96];          // a Tico module's « launch » ("" : "<nro>" "<rom>")
            char slugs[40];         // its slugs for this console ("gb,gbc")
            char sysdir[32];        // its « system_dir » ({system})
        };
        Pick s_scan[kN];                  // the worker's result
        volatile bool s_scanOk = false;   // complete (no game started during it)
        Pick s_cur[kN];                   // main thread: what the menu uses
        uint16_t s_mask = 1;              // main thread: the consoles the wheel shows

        // what was found last time (sdmc:/brume/emulateurs-detectes.txt)
        bool     s_first = true;          // no file yet: this is the first scan (nothing is « new »)
        uint16_t s_knownMask = 1, s_hints = 0;
        char     s_knownName[kN][32];
        char     s_announce[kN][32];      // the name each arriving console announces (« » : none)
        char     s_said[8][32];           // the names already announced in this batch (RetroArch: once for several consoles)
        int      s_nsaid = 0;

        // ---- the name written in an .nro (its NACP), read once per file (the cache: path + size) ----
        struct NacpCache { char path[128]; long size; char name[40]; };
        NacpCache s_nacp[32];
        int s_nacpNext = 0;
        uint8_t *s_nacpBuf = nullptr;     // 0x3000 bytes, only during a scan (the worker's only)
        bool ReadNacpName(const char *path, long size, char *out, int cap) {
            for (const NacpCache &c : s_nacp) if (c.size == size && !strcmp(c.path, path)) { snprintf(out, cap, "%s", c.name); return out[0] != 0; }
            out[0] = 0;
            if (strlen(path) >= sizeof s_nacp[0].path) return false;
            if (!s_nacpBuf && !(s_nacpBuf = (uint8_t *)malloc(0x3000))) return false;
            FILE *f = fopen(path, "rb");
            if (f) {
                uint8_t hdr[0x80], aset[0x38];
                uint32_t nsize = 0;
                if (fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr + 0x10, "NRO0", 4) == 0) {
                    memcpy(&nsize, hdr + 0x18, 4);
                    if (nsize >= 0x80 && (long)nsize < size && fseek(f, (long)nsize, SEEK_SET) == 0 && fread(aset, 1, sizeof aset, f) == sizeof aset
                        && memcmp(aset, "ASET", 4) == 0) {
                        uint64_t no = 0, ns = 0;
                        memcpy(&no, aset + 0x18, 8); memcpy(&ns, aset + 0x20, 8);
                        if (ns >= 0x3000 && no < 0x10000000ull && fseek(f, (long)(nsize + no), SEEK_SET) == 0
                            && fread(s_nacpBuf, 1, 0x3000, f) == 0x3000) {
                            int pick = s_nacpBuf[0] ? 0 : -1;               // American English, else the first one there
                            for (int l = 1; l < 16 && pick < 0; l++) if (s_nacpBuf[l * 0x300]) pick = l;
                            if (pick >= 0) {
                                int n = 0;
                                while (n < 0x1ff && n + 1 < cap && s_nacpBuf[pick * 0x300 + n]) { out[n] = (char)s_nacpBuf[pick * 0x300 + n]; n++; }
                                out[n] = 0;
                            }
                        }
                    }
                }
                fclose(f);
            }
            NacpCache &c = s_nacp[s_nacpNext];                       // remembered either way (nothing there: read once)
            s_nacpNext = (s_nacpNext + 1) % (int)(sizeof s_nacp / sizeof s_nacp[0]);
            snprintf(c.path, sizeof c.path, "%s", path); c.size = size;
            snprintf(c.name, sizeof c.name, "%s", out);
            return out[0] != 0;
        }

        // every console a (normalised) name plays (an emulator can play several: Genesis Plus GX), `got` called for each;
        // returns how many
        template <class F> int Match(const char *norm, F got) {
            int n = 0;
            for (int c = 0; c < kN; c++) {
                if (!consoles::Emulated(c)) continue;
                for (const consoles::Emu &e : kDefs[c].emu) {
                    if (!e.pat) break;
                    if (strstr(norm, e.pat)) { got(c, (int)e.prio, e.name); n++; break; }   // (the first one of its list: the longer ones come first)
                }
            }
            return n;
        }
        // a RetroArch core copied on its own and named after its console (« switch/retroarch-n64/retroarch-n64.nro »,
        // « RetroArch_SNES », « retroarchgba »): a word of its name or of its folder's is one of the console's folder names
        int MatchToken(const char *raw) {
            char w[64];
            int n = 0;
            for (const char *p = raw;; p++) {
                if (*p && isalnum((unsigned char)*p)) { if (n < (int)sizeof w - 1) w[n++] = (char)tolower((unsigned char)*p); continue; }
                w[n] = 0;
                if (n) {
                    const char *t = w;
                    if (!strncmp(t, "retroarch", 9)) t += 9; else if (!strncmp(t, "libretro", 8)) t += 8;
                    const int c = ByAlias(t, true);
                    if (c >= 0) return c;
                }
                n = 0;
                if (!*p) break;
            }
            char nb[96], st[96];                                     // « retroarch-game-boy »: the whole name without « retroarch »
            Norm(raw, nb, sizeof nb);
            int k = 0;
            for (const char *p = nb; *p;) {
                if (!strncmp(p, "retroarch", 9)) { p += 9; continue; }
                if (!strncmp(p, "libretro", 8)) { p += 8; continue; }
                if (!strncmp(p, "libnx", 5)) { p += 5; continue; }
                st[k++] = *p++;
            }
            st[k] = 0;
            return ByAlias(st, true);
        }
        void Offer(int console, const char *path, const char *name, int prio, const char *tmpl = "", const char *slugs = "", const char *sysdir = "") {
            if (!consoles::Emulated(console) || strlen(path) >= sizeof s_scan[0].path || strchr(path, '"')) return;
            Pick &p = s_scan[console];
            if (p.path[0] && p.prio >= prio) return;                // (a tie: the first one found - sdmc:/switch/ before the cores)
            snprintf(p.path, sizeof p.path, "%s", path);
            snprintf(p.name, sizeof p.name, "%s", name);
            p.prio = prio;
            if (strlen(tmpl) >= sizeof p.tmpl) tmpl = "";            // (too long for us: the plain way)
            snprintf(p.tmpl, sizeof p.tmpl, "%s", tmpl);
            snprintf(p.slugs, sizeof p.slugs, "%s", slugs);
            snprintf(p.sysdir, sizeof p.sysdir, "%s", sysdir);
        }
        // one .nro of sdmc:/switch/ (dir: its folder's name, "" at the top)
        void Consider(const char *path, const char *file, const char *dir) {
            char base[96], nb[96], nd[64];
            snprintf(base, sizeof base, "%s", file);
            if (char *dot = strrchr(base, '.')) *dot = 0;
            Norm(base, nb, sizeof nb); Norm(dir, nd, sizeof nd);
            const bool ra = IsRa(nb);
            if (Match(nb, [&](int c, int prio, const char *name) { Offer(c, path, ra ? "RetroArch" : name, prio); })) return;
            if (ra || IsRa(nd)) {
                int c = MatchToken(base);
                if (c < 0) c = MatchToken(dir);
                if (c >= 0) { Offer(c, path, "RetroArch", 105); return; }   // a core copied on its own: Yass's way, tested
            }
            if (ra) return;                                          // RetroArch itself: never relaunched with a game (it crashes, FW 22.5)
            struct stat st;
            if (stat(path, &st) != 0 || st.st_size < 0x1000) return;
            char title[40], nt[40];
            if (!ReadNacpName(path, (long)st.st_size, title, sizeof title)) return;
            Norm(title, nt, sizeof nt);
            Match(nt, [&](int c, int prio, const char *name) { Offer(c, path, name, prio - 1); });   // renamed: found by the name inside it
        }

        // ---- Tico's modules (sdmc:/tico/modules/<id>/module.json): read only, defensively ----
        // JSON with // and /* */ comments: the comments go (outside the strings), then every "key": "string" pair in order
        void StripComments(char *s) {
            char *w = s;
            bool str = false;
            for (char *r = s; *r;) {
                if (str) {
                    if (*r == '\\' && r[1]) { *w++ = *r++; *w++ = *r++; continue; }
                    if (*r == '"') str = false;
                    *w++ = *r++;
                } else if (r[0] == '/' && r[1] == '/') { while (*r && *r != '\n') r++; }
                else if (r[0] == '/' && r[1] == '*') { r += 2; while (*r && !(r[0] == '*' && r[1] == '/')) r++; if (*r) r += 2; }
                else { if (*r == '"') str = true; *w++ = *r++; }
            }
            *w = 0;
        }
        const char *ReadStr(const char *p, char *out, int cap) {   // p on the opening quote; returns past the closing one
            int n = 0;
            for (p++; *p && *p != '"'; p++) {
                char ch = *p;
                if (ch == '\\' && p[1]) {
                    p++;
                    ch = *p == 'n' || *p == 't' || *p == 'r' ? ' ' : *p;
                    if (*p == 'u') { for (int k = 0; k < 4 && p[1]; k++) p++; ch = '?'; }
                }
                if (n < cap - 1) out[n++] = ch;
            }
            out[n] = 0;
            return *p ? p + 1 : p;
        }
        template <class F> void Pairs(const char *s, F f) {
            char key[24], val[128];
            for (const char *p = s; *p;) {
                if (*p != '"') { p++; continue; }
                p = ReadStr(p, key, sizeof key);
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p != ':') continue;                             // (it was a value)
                p++;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p != '"') continue;
                p = ReadStr(p, val, sizeof val);
                f(key, val);
            }
        }
        bool SafeName(const char *s) { return s[0] && s[0] != '.' && !strchr(s, '/') && !strchr(s, '\\') && !strchr(s, '"') && strlen(s) < 64; }
        char *s_jsonBuf = nullptr;        // 16 KB, only during a scan
        void ScanTico() {
            DIR *d = opendir("sdmc:/tico/modules");
            if (!d) return;
            int seen = 0;
            while (struct dirent *e = readdir(d)) {
                if (qext_game_running() || ++seen > 64) break;
                if (!SafeName(e->d_name) || strlen(e->d_name) > 48) continue;
                char dir[96], p[160];
                snprintf(dir, sizeof dir, "sdmc:/tico/modules/%s", e->d_name);
                char nro[64] = "", tmpl[128] = "", sysdir[32] = "", slugs[6][24] = {};
                int ns = 0;
                snprintf(p, sizeof p, "%s/module.json", dir);
                if (FILE *f = fopen(p, "rb")) {
                    if (!s_jsonBuf) s_jsonBuf = (char *)malloc(16384);
                    if (s_jsonBuf) {
                        const size_t r = fread(s_jsonBuf, 1, 16383, f);   // (a bigger file: its start only - the keys we read come first)
                        s_jsonBuf[r] = 0;
                        StripComments(s_jsonBuf);
                        Pairs(s_jsonBuf, [&](const char *k, const char *v) {
                            if (!strcmp(k, "nro") && !nro[0]) Cpy(nro, sizeof nro, v);
                            else if (!strcmp(k, "launch") && !tmpl[0]) Cpy(tmpl, sizeof tmpl, v);
                            else if (!strcmp(k, "system_dir") && !sysdir[0] && SafeName(v)) Cpy(sysdir, sizeof sysdir, v);
                            else if (!strcmp(k, "slug") && ns < 6) Cpy(slugs[ns++], sizeof slugs[0], v);
                        });
                    }
                    fclose(f);
                }
                if (!SafeName(nro) || !EndsWith(nro, ".nro")) {       // no module.json (or no « nro » in it): its first .nro
                    nro[0] = 0;
                    if (DIR *s = opendir(dir)) {
                        int k = 0;
                        while (struct dirent *f2 = readdir(s)) {
                            if (++k > 32) break;
                            if (EndsWith(f2->d_name, ".nro") && SafeName(f2->d_name)) { snprintf(nro, sizeof nro, "%s", f2->d_name); break; }
                        }
                        closedir(s);
                    }
                    if (!nro[0]) continue;
                }
                if (snprintf(p, sizeof p, "%s/%s", dir, nro) >= (int)sizeof p) continue;
                struct stat st;
                if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) continue;
                char nb[96];
                { char base[64]; snprintf(base, sizeof base, "%s", nro); if (char *dot = strrchr(base, '.')) *dot = 0; Norm(base, nb, sizeof nb); }
                // its consoles: the slugs it declares (each one's folder name), else what its name plays
                int prioOf[kN] = {}; const char *nameOf[kN] = {};
                Match(nb, [&](int c, int prio, const char *name) { prioOf[c] = prio; nameOf[c] = name; });
                bool any = false;
                for (int c = 0; c < kN; c++) {
                    if (!consoles::Emulated(c)) continue;
                    char sl[40] = "";
                    for (int k = 0; k < ns; k++) {
                        char nsl[24];
                        Norm(slugs[k], nsl, sizeof nsl);
                        if (ByAlias(nsl, false) != c || !SafeName(slugs[k])) continue;
                        const size_t l = strlen(sl);
                        if (l) Cpy(sl + l, sizeof sl - l, ",");
                        Cpy(sl + strlen(sl), sizeof sl - strlen(sl), slugs[k]);
                    }
                    if (!sl[0] && (ns || !prioOf[c])) continue;     // (declared consoles: only those)
                    if (!sl[0]) snprintf(sl, sizeof sl, "%s", kDefs[c].alias[0]);
                    Offer(c, p, nameOf[c] ? nameOf[c] : "Tico", prioOf[c] ? prioOf[c] - 2 : 70, tmpl, sl, sysdir);
                    any = true;
                }
                if (any) Log("emulateurs : module Tico %s", e->d_name);
            }
            closedir(d);
        }

        void ReadIni(char (*out)[300]) {                             // emulateurs.ini: the user's own choice (read only here)
            for (int k = 0; k < kN; k++) out[k][0] = 0;
            FILE *f = fopen(kEmuIni, "r");
            if (!f) return;
            char line[360];
            for (int lines = 0; lines < 200 && fgets(line, sizeof line, f); lines++) {
                if (line[0] == '#') continue;
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                for (char *p = eq + 1; *p; p++) if (*p == '\r' || *p == '\n') *p = 0;
                for (int k = 0; k < kN; k++) if (consoles::Emulated(k) && !strcasecmp(line, kDefs[k].key)) snprintf(out[k], 300, "%s", eq + 1);
            }
            fclose(f);
        }

        // ---- the remembered file ----
        void LoadKnown() {
            s_first = true; s_knownMask = 1; s_hints = 0;
            for (auto &n : s_knownName) n[0] = 0;
            FILE *f = fopen(kKnownFile, "r");
            if (!f) return;
            s_first = false;
            char line[256];
            for (int lines = 0; lines < 64 && fgets(line, sizeof line, f); lines++) {
                if (line[0] == '#') continue;
                for (char *p = line; *p; p++) if (*p == '\r' || *p == '\n') *p = 0;
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = 0;
                const char *v = eq + 1;
                constexpr uint16_t all = (uint16_t)((1u << kN) - 1);
                if (!strcmp(line, "roue")) s_knownMask = (uint16_t)((strtoul(v, nullptr, 16) & all) | 1);   // (v90's 2 digits read the same)
                else if (!strcmp(line, "astuces")) s_hints = (uint16_t)(strtoul(v, nullptr, 16) & all);
                else for (int k = 0; k < kN; k++) if (consoles::Emulated(k) && !strcmp(line, kDefs[k].key)) {
                    const char *bar = strchr(v, '|');
                    const int len = bar ? (int)(bar - v) : (int)strlen(v);
                    snprintf(s_knownName[k], sizeof s_knownName[k], "%.*s", len < 31 ? len : 31, v);
                }
            }
            fclose(f);
        }
        void SaveKnown() {
            FILE *f = fopen(kKnownFile, "w");
            if (!f) { Log("emulateurs : impossible d'ecrire %s", kKnownFile); return; }
            fputs("# Brume - ce que Brume a trouve sur la carte SD (refait tout seul a chaque demarrage, inutile de le modifier).\n"
                  "# Pour choisir toi-meme un emulateur : brume/emulateurs.ini\n", f);
            fprintf(f, "roue=%04X\nastuces=%04X\n", s_mask, s_hints);
            for (int k = 0; k < kN; k++) if (consoles::Emulated(k) && s_cur[k].path[0]) fprintf(f, "%s=%s|%s\n", kDefs[k].key, s_cur[k].name, s_cur[k].path);
            fclose(f);
        }
        void Release() { free(s_nacpBuf); s_nacpBuf = nullptr; free(s_jsonBuf); s_jsonBuf = nullptr; }

        // the folders of `dir` named like console c's games (bounded)
        int AddDirs(const char *dir, const consoles::Def &def, char (*out)[96], int n, int max) {
            DIR *d = opendir(dir);
            if (!d) return n;
            int seen = 0;
            while (struct dirent *e = readdir(d)) {
                if (++seen > 512 || n >= max) break;                 // (bounded: a strange card never holds the menu)
                if (e->d_name[0] == '.' || strlen(e->d_name) > 60) continue;
                char nn[96];
                Norm(e->d_name, nn, sizeof nn);
                bool ok = false;
                for (const char *a : def.alias) if (a && !strcmp(nn, a)) ok = true;
                if (!ok) continue;
                snprintf(out[n], 96, "%s/%s", dir, e->d_name);
                struct stat st;
                if (stat(out[n], &st) == 0 && S_ISDIR(st.st_mode)) n++;
            }
            closedir(d);
            return n;
        }
    }

    int RomDirs(const char *key, char (*out)[96], int max) {
        const consoles::Def *def = nullptr;
        for (int c = 0; c < kN; c++) if (consoles::Emulated(c) && !strcmp(kDefs[c].key, key)) def = &kDefs[c];
        if (!def || max <= 0) return 0;
        const int n = AddDirs("sdmc:/roms", *def, out, 0, max);
        return AddDirs("sdmc:/tico/roms", *def, out, n, max);    // (Tico's own folder: read only)
    }

    void Scan() {
        s_scanOk = false;
        for (Pick &p : s_scan) { p.path[0] = 0; p.name[0] = 0; p.prio = 0; p.tmpl[0] = 0; p.slugs[0] = 0; p.sysdir[0] = 0; }
        // 1. sdmc:/switch/*.nro and sdmc:/switch/<folder>/*.nro
        if (DIR *d = opendir("sdmc:/switch")) {
            int seen = 0;
            while (struct dirent *e = readdir(d)) {
                if (qext_game_running()) { closedir(d); Release(); return; }
                if (++seen > 400) break;
                if (e->d_name[0] == '.' || strlen(e->d_name) > 60) continue;
                char p[160];
                snprintf(p, sizeof p, "sdmc:/switch/%s", e->d_name);
                if (EndsWith(e->d_name, ".nro")) { Consider(p, e->d_name, ""); continue; }
                DIR *sub = opendir(p);
                if (!sub) continue;
                int n = 0;
                while (struct dirent *s = readdir(sub)) {
                    if (++n > 64) break;                            // (a data folder with thousands of files: not an emulator's)
                    if (s->d_name[0] == '.' || !EndsWith(s->d_name, ".nro") || strlen(s->d_name) > 80) continue;
                    char q[160];
                    if (snprintf(q, sizeof q, "%s/%s", p, s->d_name) >= (int)sizeof q) continue;
                    Consider(q, s->d_name, e->d_name);
                }
                closedir(sub);
            }
            closedir(d);
        }
        // 2. RetroArch's cores (each one is a whole RetroArch with its core: started with the game, never relaunched)
        if (DIR *d = opendir("sdmc:/retroarch/cores")) {
            int seen = 0;
            while (struct dirent *e = readdir(d)) {
                if (qext_game_running()) { closedir(d); Release(); return; }
                if (++seen > 600) break;
                if (!EndsWith(e->d_name, "_libretro_libnx.nro") || strlen(e->d_name) > 80) continue;
                char nb[96], p[160];
                Norm(e->d_name, nb, sizeof nb);
                snprintf(p, sizeof p, "sdmc:/retroarch/cores/%s", e->d_name);
                Match(nb, [&](int c, int prio, const char *) { Offer(c, p, "RetroArch", prio - 3); });   // (a copy of its own, or a standalone emulator, first)
            }
            closedir(d);
        }
        // 3. Tico's emulators
        if (qext_game_running()) { Release(); return; }
        ScanTico();
        // 4. emulateurs.ini: what the user wrote wins (the first one there of his list)
        static char ini[kN][300];
        ReadIni(ini);
        for (int k = 0; k < kN; k++) {
            if (!consoles::Emulated(k)) continue;
            for (char *tok = ini[k]; tok && *tok;) {
                char *semi = strchr(tok, ';');
                if (semi) *semi = 0;
                while (*tok == ' ') tok++;
                struct stat st;
                if (*tok && !strchr(tok, '"') && stat(tok, &st) == 0 && !S_ISDIR(st.st_mode)) {
                    const char *file = strrchr(tok, '/');
                    char base[96], nb[96];
                    Cpy(base, sizeof base, file ? file + 1 : tok);
                    Norm(base, nb, sizeof nb);
                    const char *name = nullptr;
                    Match(nb, [&](int c, int, const char *nm) { if (c == k || !name) name = nm; });
                    if (IsRa(nb)) name = "RetroArch";
                    if (!name) { if (char *dot = strrchr(base, '.')) *dot = 0; name = base; }
                    Offer(k, tok, name, 1000);
                    break;
                }
                tok = semi ? semi + 1 : nullptr;
            }
        }
        Release();
        s_scanOk = !qext_game_running();
    }

    void Boot(float now) {
        LoadKnown();
        s_mask = s_first ? 1 : s_knownMask;
        for (int k = 0; k < kN; k++) {                               // the names as last time (Path() only after a scan)
            s_cur[k] = Pick{}; Cpy(s_cur[k].name, sizeof s_cur[k].name, s_knownName[k]);
        }
        for (auto &a : s_announce) a[0] = 0;
        if (s_present) s_present(s_mask, 0, now, true);
        Log("emulateurs : la roue comme la derniere fois (%04X)%s", s_mask, s_first ? ", premiere fois" : "");
    }

    void Commit(float now, const int *counts) {
        if (!s_scanOk) return;                                       // a game started during the scan: the next one will do
        uint16_t mask = 1;
        for (int k = 0; k < kN; k++) {
            if (!consoles::Emulated(k)) continue;
            s_cur[k] = s_scan[k];
            const uint16_t b = (uint16_t)(1u << k);
            if (!s_cur[k].path[0]) continue;
            if (!kDefs[k].needGames || counts[k] > 0) mask |= b;
            else if (counts[k] < 0) mask |= s_mask & b;              // (not counted this time: as it was)
        }
        constexpr uint16_t kPortsBit = 1u << consoles::kPorts;
        if (counts[consoles::kPorts] > 0) mask |= kPortsBit;
        else if (counts[consoles::kPorts] < 0) mask |= s_mask & kPortsBit;
        const uint16_t before = s_first ? mask : s_knownMask;
        const uint16_t fresh = (uint16_t)(mask & ~before & ~1u);
        bool changed = s_first || mask != s_knownMask;
        s_nsaid = 0;
        for (int k = 1; k < kN; k++) {
            if (!(fresh & (1u << k))) continue;
            snprintf(s_announce[k], sizeof s_announce[k], "%s", k == consoles::kPorts ? "" : s_cur[k].name);
        }
        // a console still there whose emulator changed for a new one (mGBA installed beside RetroArch's core...)
        for (int k = 0; k < kN; k++) {
            if (!consoles::Emulated(k)) continue;
            if (!(mask & before & (1u << k)) || s_first || !s_knownName[k][0] || !strcmp(s_knownName[k], s_cur[k].name)) continue;
            bool said = false;
            for (int j = 1; j < k; j++) said = said || (consoles::Emulated(j) && (mask & before & (1u << j)) && strcmp(s_knownName[j], s_cur[j].name) && !strcmp(s_cur[j].name, s_cur[k].name));
            if (!said) { char m[96]; snprintf(m, sizeof m, lang::T("Nouvel \xc3\xa9mulateur d\xc3\xa9tect\xc3\xa9 : %s"), s_cur[k].name); notify::Post(now, m); }
            changed = true;
        }
        for (int k = 0; k < kN; k++) if (consoles::Emulated(k) && strcmp(s_knownName[k], s_cur[k].name)) { snprintf(s_knownName[k], sizeof s_knownName[k], "%s", s_cur[k].name); changed = true; }
        // games without their emulator: said once (discreetly), until an emulator comes for them
        for (int k = 0; k < kN; k++) {
            if (!consoles::Emulated(k)) continue;
            const uint16_t b = (uint16_t)(1u << k);
            if (s_cur[k].path[0]) { if (s_hints & b) { s_hints &= (uint16_t)~b; changed = true; } continue; }
            if (counts[k] <= 0 || (s_hints & b)) continue;
            char m[140];
            snprintf(m, sizeof m, lang::T("Des jeux %s trouv\xc3\xa9s : installe %s pour y jouer"), kDefs[k].name, kDefs[k].advice);
            notify::Post(now, m);
            s_hints |= b; changed = true;
        }
        s_mask = mask; s_knownMask = mask; s_first = false;
        if (s_present) s_present(mask, fresh, now, false);
        if (changed) SaveKnown();
        for (int k = 0; k < kN; k++) if (consoles::Emulated(k) && (s_cur[k].path[0] || counts[k] > 0))
            Log("emulateurs : %s -> %s%s%s", kDefs[k].key, s_cur[k].path[0] ? s_cur[k].path : "(aucun)", s_cur[k].tmpl[0] ? " (Tico) " : "", s_cur[k].tmpl);
        Log("emulateurs : roue %04X, nouveaux %04X", mask, fresh);
    }

    const char *Path(int console) { return consoles::Emulated(console) && s_cur[console].path[0] ? s_cur[console].path : nullptr; }
    const char *Name(int console) { return consoles::Emulated(console) ? s_cur[console].name : ""; }

    bool Argv(int console, const char *nro, const char *rom, const char *title, char *out, int cap) {
        if (!nro || cap < 8 || strchr(nro, '"') || (rom && strchr(rom, '"'))) return false;
        const Pick *p = consoles::Emulated(console) ? &s_cur[console] : nullptr;
        if (!rom || !rom[0]) return snprintf(out, cap, "\"%s\"", nro) < cap;
        if (!p || !p->tmpl[0] || strcmp(p->path, nro)) return snprintf(out, cap, "\"%s\" \"%s\"", nro, rom) < cap;
        // a Tico module: its own « launch » template, every argument quoted ({saves} / {states}: Tico's per-user folders,
        // sealed in its session - left out, the emulator then uses its own default folders)
        char tl[64] = "", slug[24] = "", ext[16] = "", tmp[300];
        int t = 0;
        for (const char *q = title ? title : ""; *q && t < (int)sizeof tl - 1; q++) if (*q != '"') tl[t++] = *q;
        tl[t] = 0;
        if (const char *dot = strrchr(rom, '.')) Norm(dot + 1, ext, sizeof ext);
        {   // the slug: the one named like the game's extension (gbc), else the first one
            for (const char *s = p->slugs; *s;) {
                const char *e = strchr(s, ',');
                const int len = e ? (int)(e - s) : (int)strlen(s);
                char one[24], ns[24];
                snprintf(one, sizeof one, "%.*s", len < 23 ? len : 23, s);
                Norm(one, ns, sizeof ns);
                if (!slug[0] || !strcmp(ns, ext)) snprintf(slug, sizeof slug, "%s", one);
                s = e ? e + 1 : s + len;
            }
        }
        int n = 0;
        out[0] = 0;
        bool first = true;
        for (const char *q = p->tmpl; *q;) {
            while (*q == ' ') q++;
            if (!*q) break;
            char tok[96]; int k = 0;
            if (*q == '"') { q++; while (*q && *q != '"') { if (k < (int)sizeof tok - 1) tok[k++] = *q; q++; } if (*q) q++; }
            else while (*q && *q != ' ') { if (k < (int)sizeof tok - 1) tok[k++] = *q; q++; }
            tok[k] = 0;
            if (strstr(tok, "{saves}") || strstr(tok, "{states}")) continue;
            int m = 0;
            for (const char *s = tok; *s && m < (int)sizeof tmp - 1;) {
                const char *v = nullptr; int skip = 0; char folder[96];
                if (!strncmp(s, "{nro}", 5)) { v = nro; skip = 5; }
                else if (!strncmp(s, "{rom}", 5)) { v = rom; skip = 5; }
                else if (!strncmp(s, "{title}", 7)) { v = tl; skip = 7; }
                else if (!strncmp(s, "{slug}", 6)) { v = slug; skip = 6; }
                else if (!strncmp(s, "{roms}", 6)) { snprintf(folder, sizeof folder, "sdmc:/tico/roms/%s/", slug); v = folder; skip = 6; }
                else if (!strncmp(s, "{system}", 8)) { snprintf(folder, sizeof folder, "sdmc:/tico/system/%s/", p->sysdir); v = folder; skip = 8; }
                if (v) { while (*v && m < (int)sizeof tmp - 1) tmp[m++] = *v++; s += skip; }
                else tmp[m++] = *s++;
            }
            tmp[m] = 0;
            if (first && strcmp(tmp, nro)) {                         // argv[0] is always the .nro itself
                const int w = snprintf(out + n, cap - n, "\"%s\"", nro);
                if (w < 0 || w >= cap - n) return false;
                n += w;
            }
            first = false;
            const int w = snprintf(out + n, cap - n, "%s\"%s\"", n ? " " : "", tmp);
            if (w < 0 || w >= cap - n) return false;
            n += w;
        }
        if (first) return snprintf(out, cap, "\"%s\" \"%s\"", nro, rom) < cap;
        return true;
    }

    void Arrived(int console, float now) {
        if (console < 1 || console >= kN) return;
        char m[96];
        if (console == consoles::kPorts) snprintf(m, sizeof m, "%s", lang::T("Ports natifs ajout\xc3\xa9s \xc3\xa0 la roue"));
        else {
            const char *name = s_announce[console][0] ? s_announce[console] : s_cur[console].name;
            if (!name[0]) return;
            for (int i = 0; i < s_nsaid; i++) if (!strcmp(s_said[i], name)) return;   // RetroArch: once, for its consoles
            if (s_nsaid < 8) snprintf(s_said[s_nsaid++], sizeof s_said[0], "%s", name);
            snprintf(m, sizeof m, lang::T("Nouvel \xc3\xa9mulateur d\xc3\xa9tect\xc3\xa9 : %s"), name);
        }
        s_announce[console][0] = 0;
        notify::Post(now, m, notify::kDone);
    }
}
