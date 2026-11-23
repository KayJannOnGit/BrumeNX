// Brume - v95: the « Amis » page (Nextendo) and the Discord voice shortcut. See Social.hpp.
#include "Social.hpp"
#include "Wheel.hpp"
#include "Nextendo.hpp"
#include "Notify.hpp"
#include "Popup.hpp"
#include "Games.hpp"
#include "Shelf.hpp"
#include "Qr.hpp"
#include "Lang.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Ease.hpp"
#include "Busy.hpp"
#include "Settings.hpp"
#include "Worker.hpp"
#include "Covers.hpp"
#include "Spotify.hpp"
#include <cstdarg>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
    int    qext_game_running(void);
    int    qext_title_count(void);
    u64    qext_title_id(int index);
    int    qext_title_name(int index, char *out, unsigned cap);
    int    qext_title_is_virtual(u64 tid);
    int    qext_title_icon_size(int index);
    int    qext_title_icon(int index, void *out, unsigned cap);
    u64    qext_suspended_title(void);
    Result qext_resume_game(void);
    struct BrumeSwkbdOptions { const char *initialText, *headerText; unsigned minLength, maxLength; int blurBackground; };
    int    qext_swkbd_show_ex(const BrumeSwkbdOptions *options, char *out, unsigned cap);
}
namespace la {                             // the loader's library applet helpers (exported C++ symbols)
    Result Start(AppletId id, s32 version, const void *a, size_t na);
    bool   IsActive();
    Result Terminate();
}

namespace brume {

    bool DecodeJpegSquare(const void *data, int bytes, uint8_t *out, int size);   // Jpeg.cpp

    namespace social {

    void (*s_log)(const char *) = nullptr;
    bool s_barNextendo = false;                          // v96 (MyPage: Image du profil = Nextendo)

    namespace {
        constexpr const char *kIni = "sdmc:/brume/nextendo.ini", *kTokFile = "sdmc:/brume/nextendo.json";
        constexpr const char *kDiscordFile = "sdmc:/brume/discord.txt", *kCacheDir = "sdmc:/brume/cache/amis";
        constexpr int kPort = 8080;
        constexpr const char *kRedirect = "http://127.0.0.1:8080/callback";
        constexpr const char *kScopes = "identity friends presence profil";   // lang:log v96: + profil (only GET / POST /api/appear-offline is used)
        constexpr int kMaxF = 64, kMaxR = 16;
        constexpr float kEvery = 120.0f;                 // one look at the friends every 2 minutes (menu on screen only)
        constexpr int kBuf = 0x18000;                    // one answer at most (96 KB, only while a job runs)
        constexpr float kServeFor = 300.0f;              // the sign-in server: 5 minutes at most
        // the strip: 1024 x 128 BC1 - 14 x 2 places of 64 px, then a 128 x 128 square for a QR code
        constexpr int kW = 1024, kH = 128, kPx = 64, kCols = 14, kSlots = 28, kQrX = 896;
        constexpr uint32_t kStripBytes = (kW / 4) * (kH / 4) * 8, kPicBytes = (kPx / 4) * (kPx / 4) * 8;

        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
        void Log(const char *fmt, ...) {
            if (!s_log) return;
            char m[200]; va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
            s_log(m);
        }

        struct Friend { u64 pid, tid; char name[40], code[24], game[64], detail[64]; u32 av; uint8_t status; bool fav; };

        // ======================= JSON, read defensively (bounded, never trusted, never recursive past 24) =======================
        struct Js { const char *p = nullptr, *e = nullptr; bool Ok() const { return p && e && p < e; } };
        const char *Ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }
        const char *Skip(const char *p, const char *e, int depth = 0) {   // the end of the value at p (nullptr: malformed)
            p = Ws(p, e);
            if (p >= e || depth > 24) return nullptr;
            if (*p == '"') { for (p++; p < e; p++) { if (*p == '\\') { p++; continue; } if (*p == '"') return p + 1; } return nullptr; }
            if (*p == '{' || *p == '[') {
                const char close = *p == '{' ? '}' : ']';
                p++;
                for (int n = 0; n < 100000; n++) {
                    p = Ws(p, e);
                    if (p >= e) return nullptr;
                    if (*p == close) return p + 1;
                    if (close == '}') {
                        if (*p != '"') return nullptr;
                        p = Skip(p, e, depth + 1); if (!p) return nullptr;
                        p = Ws(p, e); if (p >= e || *p != ':') return nullptr;
                        p++;
                    }
                    p = Skip(p, e, depth + 1); if (!p) return nullptr;
                    p = Ws(p, e);
                    if (p < e && *p == ',') p++;
                    else if (p < e && *p != close) return nullptr;
                }
                return nullptr;
            }
            const char *q = p;
            while (q < e && *q != ',' && *q != '}' && *q != ']' && *q != ' ' && *q != '\n' && *q != '\r' && *q != '\t') q++;
            return q > p ? q : nullptr;
        }
        Js Root(const char *b, int n) { Js j; if (!b || n <= 0) return j; const char *e = b + n; const char *p = Ws(b, e); const char *q = Skip(p, e); if (q) { j.p = p; j.e = q; } return j; }
        Js Get(const Js &o, const char *key) {
            Js none;
            if (!o.Ok()) return none;
            const char *p = Ws(o.p, o.e);
            if (p >= o.e || *p != '{') return none;
            p++;
            const size_t kl = strlen(key);
            for (int n = 0; n < 4096; n++) {
                p = Ws(p, o.e);
                if (p >= o.e || *p != '"') return none;
                const char *ks = p + 1, *ke = Skip(p, o.e);
                if (!ke) return none;
                const bool match = (size_t)(ke - 1 - ks) == kl && !memcmp(ks, key, kl);
                p = Ws(ke, o.e);
                if (p >= o.e || *p != ':') return none;
                p = Ws(p + 1, o.e);
                const char *ve = Skip(p, o.e);
                if (!ve) return none;
                if (match) { Js v; v.p = p; v.e = ve; return v; }
                p = Ws(ve, o.e);
                if (p < o.e && *p == ',') p++;
            }
            return none;
        }
        bool Next(const Js &a, const char *&cur, Js &v) {   // an array's elements, one by one (cur = nullptr to start)
            if (!a.Ok()) return false;
            if (!cur) { cur = Ws(a.p, a.e); if (cur >= a.e || *cur != '[') return false; cur++; }
            cur = Ws(cur, a.e);
            if (cur >= a.e || *cur == ']') return false;
            const char *ve = Skip(cur, a.e);
            if (!ve || ve > a.e) return false;
            v.p = cur; v.e = ve;
            cur = Ws(ve, a.e);
            if (cur < a.e && *cur == ',') cur++;
            return true;
        }
        void PutU8(uint32_t c, char *o, int &k, int cap) {
            char t[4]; int n;
            if (c < 0x80) { t[0] = (char)c; n = 1; }
            else if (c < 0x800) { t[0] = (char)(0xC0 | (c >> 6)); t[1] = (char)(0x80 | (c & 63)); n = 2; }
            else if (c < 0x10000) { t[0] = (char)(0xE0 | (c >> 12)); t[1] = (char)(0x80 | ((c >> 6) & 63)); t[2] = (char)(0x80 | (c & 63)); n = 3; }
            else { t[0] = (char)(0xF0 | (c >> 18)); t[1] = (char)(0x80 | ((c >> 12) & 63)); t[2] = (char)(0x80 | ((c >> 6) & 63)); t[3] = (char)(0x80 | (c & 63)); n = 4; }
            if (k + n < cap) { memcpy(o + k, t, (size_t)n); k += n; }
        }
        // a string (unescaped, control characters dropped) or a bare number / literal as it is; false: nothing there
        bool Str(const Js &v, char *out, int cap) {
            if (cap <= 0) return false;
            out[0] = 0;
            if (!v.Ok()) return false;
            int k = 0;
            if (*v.p != '"') {
                if (*v.p == '{' || *v.p == '[' || !strncmp(v.p, "null", 4)) return false;
                for (const char *p = v.p; p < v.e && k + 1 < cap; p++) out[k++] = *p;
                out[k] = 0;
                return k > 0;
            }
            for (const char *p = v.p + 1; p < v.e - 1 && k + 1 < cap; p++) {
                unsigned char c = (unsigned char)*p;
                if (c == '\\' && p + 1 < v.e - 1) {
                    const char n = *++p;
                    if (n == 'u' && p + 4 < v.e - 1) {
                        char hx[5] = { p[1], p[2], p[3], p[4], 0 };
                        uint32_t cp = (uint32_t)strtoul(hx, nullptr, 16);
                        p += 4;
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 6 < v.e - 1 && p[1] == '\\' && p[2] == 'u') {
                            char lo[5] = { p[3], p[4], p[5], p[6], 0 };
                            const uint32_t l = (uint32_t)strtoul(lo, nullptr, 16);
                            if (l >= 0xDC00 && l < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (l - 0xDC00); p += 6; }
                        }
                        if (cp >= 0x20 && !(cp >= 0xD800 && cp < 0xE000)) PutU8(cp, out, k, cap);
                    } else if (n == 'n' || n == 't' || n == 'r') { if (k + 1 < cap) out[k++] = ' '; }
                    else if (n == '"' || n == '\\' || n == '/') { if (k + 1 < cap) out[k++] = n; }
                    continue;
                }
                if (c < 0x20) continue;
                out[k++] = (char)c;
            }
            while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80 && k + 1 >= cap) k--;   // (never a cut letter at the end)
            out[k] = 0;
            return true;
        }
        long long Num(const Js &v) { char t[32]; if (!Str(v, t, sizeof t)) return 0; return strtoll(t, nullptr, 10); }
        u64 Hex(const Js &v) { char t[32]; if (!Str(v, t, sizeof t)) return 0; return strtoull(t, nullptr, 16); }
        bool Bool(const Js &v) { return v.Ok() && *v.p == 't'; }
        u32 Fnv(const char *s) { u32 h = 2166136261u; for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; } return h; }

        // a name / text cut to `letters` letters, never in the middle of one (and an ellipsis)
        void Cut(const char *in, char *out, int cap, int letters) {
            int n = 0, l = 0;
            while (in[n] && l < letters && n + 4 < cap) { n++; while (((unsigned char)in[n] & 0xC0) == 0x80) n++; l++; }
            memcpy(out, in, (size_t)n); out[n] = 0;
            if (in[n] && n + 4 < cap) snprintf(out + n, (size_t)(cap - n), "\xe2\x80\xa6");
        }

        // ======================= the shared state =======================
        enum Auth { kUnknown, kNoClient, kOut, kIn };
        enum JobKind { kJobNone, kJobLogin, kJobList, kJobAction, kJobImage, kJobLogout, kJobMe, kJobHide };   // (v96: the profile page)
        enum Act { kAdd = 1, kAccept, kDecline };
        int  s_auth = kUnknown;
        char s_client[80] = {};
        char s_access[2048] = {}, s_refresh[1024] = {};  // the tokens: written by the worker during a job only (and at load)
        Friend s_f[kMaxF], s_rq[kMaxR];
        int  s_nf = 0, s_nrq = 0;
        char s_meName[40] = {}, s_meCode[24] = {};
        // v96 - the profile page: the account as Nextendo shows it, my presence, invisible or not, my picture (128 px)
        char s_meUser[40] = {}, s_meGame[64] = {};
        u64  s_mePid = 0, s_meTid = 0;
        int  s_meStatus = -1, s_hidden = -1, s_canHide = -1;
        float s_meAt = -1e9f, s_wantMeAt = -1, s_meImgAt = 0;
        bool s_profile = false, s_wantMe = false, s_meBigOk = false, s_meBigTried = false, s_meFresh = false;
        uint8_t s_meBig[128 / 4 * 128 / 4 * 8];         // its BC1 (8 KB), kept: the square of the strip gets it back after a QR
        int  s_sq = 0, s_texSq = 0;                      // what the strip's 128 px square holds: 0 nothing, 1 a QR, 2 my picture (mirror / GPU)
        bool s_haveList = false, s_firstList = true;
        float s_listAt = -1e9f, s_retryUntil = 0, s_nextImgAt = 0, s_wantListAt = -1;
        int  s_task = -1;
        // the sign-in (main thread writes before the job starts; the worker reads; s_stop / s_phase are atomics)
        char s_verifier[64] = {}, s_state[40] = {}, s_authUrl[720] = {}, s_phoneTok[12] = {}, s_ip[20] = {}, s_phoneUrl[64] = {};
        int  s_stop = 0, s_phase = 0;                    // phase: 0 waiting, 1 a code came, 2 exchanging, -1 the port is taken
        int  s_login = 0;                                // 0 none, 1 waiting (console browser), 2 waiting (the phone's QR shown)
        float s_loginAt = -100;
        bool s_browserMine = false;                      // the browser was started by us (closed by us once signed in)
        char s_loginWhy[128] = {};
        float s_loginWhyAt = -100;
        // the job
        struct Job {                                     // (plain data: zero at load, no constructor to run)
            int kind;
            int act; u64 pid; char code[40];             // an action
            int imgKind; u64 imgId; u32 imgVer; int imgIndex;   // a picture (1 avatar, 2 game icon)
            int http, retry; char why[96], msg[128];
            bool ok, out, list, me, denied;
            Friend f[kMaxF]; int nf; Friend rq[kMaxR]; int nrq;
            char meName[40], meCode[24];
            char meUser[40], meGame[64]; u64 mePid, meTid; int meStatus, hidden, canHide; bool wantHide, fresh;   // v96 (kJobMe / kJobHide)
            uint8_t big[128 / 4 * 128 / 4 * 8];          // v96: my picture, 128 px (imgKind 3)
            uint8_t bc[kPicBytes]; int img;              // 1 the picture, 0 none (never ask again), -1 not now
        };
        Job s_job;
        Worker s_work;
        // the strip (CPU copy only while needed) and its places
        Target s_tex;
        bool s_texOk = false, s_dirty = false;
        uint8_t *s_mirror = nullptr;
        struct Slot { uint8_t kind; u64 id; u32 ver; u32 used; };
        Slot s_slot[kSlots];
        u32  s_frame = 1;
        struct Miss { uint8_t kind; u64 id; u32 ver; };
        Miss s_miss[32]; int s_nmiss = 0, s_missAt = 0;
        char s_qrFor[200] = {};                          // the text the QR square holds now
        int  s_qrPx = 0;                                 // its size in the strip (px, square, centred in the 128 px)
        // the page
        enum Row { kRAdd, kRDiscord, kRReq, kRFriend, kRLogout };
        struct RowT { uint8_t kind; int8_t idx; };
        RowT s_rows[kMaxF + kMaxR + 4]; int s_nrows = 0, s_sel = 0, s_loginSel = 0;
        float s_selY = 0, s_selV = 0, s_scroll = 0, s_scrollV = 0, s_moveAt = -100, s_pageK = 0;
        float s_rowY[kMaxF + kMaxR + 4];
        struct Hit { int i; float x0, y0, x1, y1; }; Hit s_hit[16]; int s_nhit = 0;
        char s_status[160] = {}; float s_statusAt = -100;
        // « Rejoindre »
        u64  s_launchTid = 0; float s_launchAt = -100, s_backAt = -100; bool s_started = false; Settle s_settle;
        // Discord
        bool s_dOpen = false; float s_dAt = -100, s_dCloseAt = -100; char s_dLink[176] = {};

        void Status(float now, const char *m) { snprintf(s_status, sizeof s_status, "%s", m); s_statusAt = now; }

        // ======================= files =======================
        bool ReadFile(const char *path, char *buf, int cap, int *n) {
            *n = 0;
            FILE *f = fopen(path, "rb");
            if (!f) return false;
            const size_t r = fread(buf, 1, (size_t)cap - 1, f);
            fclose(f);
            buf[r] = 0; *n = (int)r;
            return true;
        }
        bool CleanId(const char *v, char *out, int cap) {   // a client id: nxc_ and letters, digits, - and _ only
            int k = 0;
            for (; *v && *v != '\r' && *v != '\n' && *v != ' ' && *v != '\t'; v++) {
                const char c = *v;
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
                if (k + 1 >= cap) return false;
                out[k++] = c;
            }
            out[k] = 0;
            return k >= 6;
        }
        void LoadConfig() {
            s_client[0] = 0;
            char b[1024]; int n = 0;
            if (ReadFile(kIni, b, sizeof b, &n)) {
                for (char *line = b; line && *line;) {
                    char *nl = strchr(line, '\n');
                    if (nl) *nl = 0;
                    while (*line == ' ' || *line == '\t') line++;
                    if (!strncmp(line, "client_id", 9)) {
                        char *v = line + 9;
                        while (*v == ' ' || *v == '\t') v++;
                        if (*v == '=') { v++; while (*v == ' ' || *v == '\t') v++; if (!CleanId(v, s_client, sizeof s_client)) s_client[0] = 0; }
                    }
                    line = nl ? nl + 1 : nullptr;
                }
            }
            s_access[0] = s_refresh[0] = 0;
            char *t = (char *)malloc(4096);
            if (t && ReadFile(kTokFile, t, 4096, &n) && n > 0) {
                const Js r = Root(t, n);
                Str(Get(r, "access_token"), s_access, sizeof s_access);
                Str(Get(r, "refresh_token"), s_refresh, sizeof s_refresh);
            }
            if (t) { memset(t, 0, 4096); free(t); }
            s_auth = !s_client[0] ? kNoClient : (s_refresh[0] ? kIn : kOut);
            Log("amis : %s, %s", s_client[0] ? "client_id ok" : "pas de client_id (brume/nextendo.ini)", s_refresh[0] ? "connecte" : "pas connecte");   // lang:log (never drawn)
        }
        bool SaveTokens() {                              // (worker) - never logged
            FILE *f = fopen(kTokFile, "wb");
            if (!f) return false;
            fprintf(f, "{\"access_token\":\"%s\",\"refresh_token\":\"%s\"}\n", s_access, s_refresh);
            fclose(f);
            return true;
        }
        void ForgetTokens() { memset(s_access, 0, sizeof s_access); memset(s_refresh, 0, sizeof s_refresh); remove(kTokFile); }
        // Discord: the first line of discord.txt, only an invite of discord.gg / discord.com (anything else is refused)
        void LoadDiscord() {
            s_dLink[0] = 0;
            char b[512]; int n = 0;
            if (!ReadFile(kDiscordFile, b, sizeof b, &n)) return;
            char *p = b;
            if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   // (a BOM)
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            char l[160]; int k = 0;
            for (; *p && *p != '\r' && *p != '\n' && *p != ' ' && *p != '\t' && k + 1 < (int)sizeof l; p++) {
                const char c = *p;
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_./:", c))) return;
                l[k++] = c;
            }
            l[k] = 0;
            const char *q = l;
            if (!strncasecmp(q, "https://", 8)) q += 8; else if (!strncasecmp(q, "http://", 7)) q += 7;
            if (!strncasecmp(q, "www.", 4)) q += 4;
            const bool ok = (!strncasecmp(q, "discord.gg/", 11) && q[11]) || (!strncasecmp(q, "discord.com/invite/", 19) && q[19]) ||
                            (!strncasecmp(q, "discordapp.com/invite/", 22) && q[22]);
            if (ok) snprintf(s_dLink, sizeof s_dLink, "https://%s", q);
        }

        // ======================= the network (worker only) =======================
        bool NetUp() {
            SocketInitConfig cfg = *socketGetDefaultInitConfig();   // small buffers, like Nextendo.cpp (the menu's memory is short)
            cfg.tcp_tx_buf_size = 0x2000; cfg.tcp_rx_buf_size = 0x8000; cfg.tcp_tx_buf_max_size = 0; cfg.tcp_rx_buf_max_size = 0;
            cfg.udp_tx_buf_size = 0x800; cfg.udp_rx_buf_size = 0x800; cfg.sb_efficiency = 1; cfg.num_bsd_sessions = 2;
            if (R_FAILED(socketInitialize(&cfg))) return false;
            if (R_FAILED(sslInitialize(1))) { socketExit(); return false; }
            return true;
        }
        void NetDown() { sslExit(); socketExit(); }
        void UrlEnc(const char *in, char *out, int cap) {
            static const char kHexD[] = "0123456789ABCDEF";
            int k = 0;
            for (; *in && k + 4 < cap; in++) {
                const unsigned char c = (unsigned char)*in;
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out[k++] = (char)c;
                else { out[k++] = '%'; out[k++] = kHexD[c >> 4]; out[k++] = kHexD[c & 15]; }
            }
            out[k] = 0;
        }
        void UrlDec(char *s) {                           // in place
            char *o = s;
            for (; *s; s++) {
                if (*s == '+') *o++ = ' ';
                else if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; char *e = nullptr; const long v = strtol(h, &e, 16); if (e == h + 2) { *o++ = (char)v; s += 2; } else *o++ = *s; }
                else *o++ = *s;
            }
            *o = 0;
        }
        // a parameter of a query string (« a=1&b=2 »), decoded; false: not there
        bool Param(const char *q, const char *name, char *out, int cap) {
            const size_t nl = strlen(name);
            for (const char *p = q; p && *p;) {
                if (!strncmp(p, name, nl) && p[nl] == '=') {
                    const char *v = p + nl + 1; int k = 0;
                    while (*v && *v != '&' && *v != ' ' && *v != '#' && *v != '\r' && *v != '\n' && k + 1 < cap) out[k++] = *v++;
                    out[k] = 0; UrlDec(out);
                    return true;
                }
                p = strpbrk(p, "&?");
                if (p) p++;
            }
            return false;
        }
        // the token endpoint (a form): access + refresh kept and saved; false: refused (s_job.why / s_job.out)
        bool TokenPost(const char *form, char *buf) {
            char hd[160]; snprintf(hd, sizeof hd, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n", (int)strlen(form));
            char *body = nullptr; int bl = 0, ra = 0;
            const int st = nextendo::Call("POST", "/api/oauth/token", hd, form, buf, kBuf, &body, &bl, &ra, s_job.why, sizeof s_job.why);
            s_job.http = st;
            if (st == 429) { s_job.retry = ra ? ra : 60; snprintf(s_job.why, sizeof s_job.why, "429"); return false; }
            if (st != 200 || !body) {
                if (st == 400 || st == 401) {
                    char e[48] = {}; Str(Get(Root(body, bl), "error"), e, sizeof e);
                    if (!strcmp(e, "invalid_grant")) s_job.out = true;   // (the refresh token is gone: signed out)
                    snprintf(s_job.why, sizeof s_job.why, "%s", e[0] ? e : "refus");
                } else if (st > 0) snprintf(s_job.why, sizeof s_job.why, "HTTP %d", st);
                return false;
            }
            const Js r = Root(body, bl);
            char a[sizeof s_access] = {}, rf[sizeof s_refresh] = {};
            if (!Str(Get(r, "access_token"), a, sizeof a) || !a[0]) { snprintf(s_job.why, sizeof s_job.why, "reponse"); return false; }
            Str(Get(r, "refresh_token"), rf, sizeof rf);
            memcpy(s_access, a, sizeof s_access);
            if (rf[0]) memcpy(s_refresh, rf, sizeof s_refresh);   // (each refresh gives a new one: the old one is dropped)
            memset(a, 0, sizeof a); memset(rf, 0, sizeof rf);
            SaveTokens();
            return true;
        }
        bool Renew(char *buf) {
            if (!s_refresh[0]) { s_job.out = true; return false; }
            char *form = (char *)malloc(1600);
            if (!form) return false;
            char rt[1100], cid[100];
            UrlEnc(s_refresh, rt, sizeof rt); UrlEnc(s_client, cid, sizeof cid);
            snprintf(form, 1600, "grant_type=refresh_token&client_id=%s&refresh_token=%s", cid, rt);
            const bool ok = TokenPost(form, buf);
            memset(form, 0, 1600); memset(rt, 0, sizeof rt); free(form);
            return ok;
        }
        // an API call with the token (renewed once on 401); the HTTP status, -1 no answer
        int Api(const char *method, const char *path, const char *json, char *buf, char **body, int *bl) {
            for (int tries = 0; tries < 2; tries++) {
                if (qext_game_running()) { snprintf(s_job.why, sizeof s_job.why, "jeu lance"); return -1; }   // lang:log (never drawn)
                char *hd = (char *)malloc(2400);
                if (!hd) return -1;
                if (json) snprintf(hd, 2400, "Authorization: Bearer %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\n", s_access, (int)strlen(json));
                else snprintf(hd, 2400, "Authorization: Bearer %s\r\n", s_access);
                int ra = 0;
                const int st = nextendo::Call(method, path, hd, json, buf, kBuf, body, bl, &ra, s_job.why, sizeof s_job.why);
                memset(hd, 0, 2400); free(hd);
                s_job.http = st;
                if (st == 429) { s_job.retry = ra ? ra : 60; snprintf(s_job.why, sizeof s_job.why, "429"); return st; }
                if (st == 401 && tries == 0) { if (!Renew(buf)) return 401; continue; }
                return st;
            }
            return -1;
        }
        void ParseFriend(const Js &o, Friend &f) {
            memset(&f, 0, sizeof f);
            f.pid = (u64)Num(Get(o, "pid"));
            if (!Str(Get(o, "name"), f.name, sizeof f.name) || !f.name[0]) Str(Get(o, "username"), f.name, sizeof f.name);
            if (!f.name[0]) snprintf(f.name, sizeof f.name, "PID %llu", (unsigned long long)f.pid);
            Str(Get(o, "friend_code"), f.code, sizeof f.code);
            char av[160] = {}; Str(Get(o, "avatar"), av, sizeof av); f.av = Fnv(av);
            f.fav = Bool(Get(o, "favorite"));
            const Js p = Get(o, "presence");
            if (p.Ok() && *p.p == '{') {
                const long long st = Num(Get(p, "status"));
                f.status = (uint8_t)(st <= 0 ? 0 : (st >= 2 ? 2 : 1));
                f.tid = Hex(Get(p, "app_id"));
                Str(Get(p, "jeu"), f.game, sizeof f.game);
                Str(Get(p, "app_detail"), f.detail, sizeof f.detail);
                if (f.status == 1 && f.tid && f.game[0]) f.status = 2;   // (a game named: playing it)
            }
        }
        int Rank(const Friend &f) { return (f.status == 2 ? 0 : f.status == 1 ? 2 : 4) + (f.fav ? 0 : 1); }
        // {friends:[...], requests:[...], sent:[...]} -> the job's lists, sorted (playing, online, offline; favourites first)
        bool ParseList(const char *b, int n, Job &j) {
            const Js r = Root(b, n);
            const Js fl = Get(r, "friends"), rl = Get(r, "requests");
            if (!fl.Ok() || *fl.p != '[') return false;
            j.nf = 0; j.nrq = 0;
            const char *cur = nullptr; Js v;
            while (j.nf < kMaxF && Next(fl, cur, v)) if (*v.p == '{') { ParseFriend(v, j.f[j.nf]); if (j.f[j.nf].pid) j.nf++; }
            cur = nullptr;
            while (rl.Ok() && j.nrq < kMaxR && Next(rl, cur, v)) if (*v.p == '{') { ParseFriend(v, j.rq[j.nrq]); if (j.rq[j.nrq].pid) j.nrq++; }
            for (int i = 1; i < j.nf; i++) {             // (insertion sort: a few dozen at most)
                Friend t = j.f[i]; int k = i - 1;
                while (k >= 0 && (Rank(j.f[k]) > Rank(t) || (Rank(j.f[k]) == Rank(t) && strcasecmp(j.f[k].name, t.name) > 0))) { j.f[k + 1] = j.f[k]; k--; }
                j.f[k + 1] = t;
            }
            return true;
        }
        bool ParseMe(const char *b, int n, Job &j) {
            const Js r = Root(b, n);
            if (!r.Ok()) return false;
            if (!Str(Get(r, "display_name"), j.meName, sizeof j.meName) || !j.meName[0]) Str(Get(r, "username"), j.meName, sizeof j.meName);
            Str(Get(r, "friend_code"), j.meCode, sizeof j.meCode);
            Str(Get(r, "username"), j.meUser, sizeof j.meUser);   // v96
            j.mePid = (u64)Num(Get(r, "pid"));
            char sc[96] = {};                            // v96: the scopes granted - « profil » opens the invisible mode
            if (Str(Get(r, "scope"), sc, sizeof sc)) {
                bool has = false;                        // (a whole word: separated by spaces, commas or +)
                for (const char *t = strstr(sc, "profil"); t && !has; t = strstr(t + 1, "profil"))
                    has = (t == sc || strchr(" ,+", t[-1])) && (!t[6] || strchr(" ,+", t[6]));
                j.canHide = has ? 1 : 0;
            }
            return j.meName[0] != 0;
        }
        // v96 - GET /api/me: {account: {pid, username, name, friend_code, ...}, presence: {status, app_id, jeu, app_detail}}
        bool ParseAccount(const char *b, int n, Job &j) {
            const Js r = Root(b, n), a = Get(r, "account");
            if (!a.Ok() || *a.p != '{') return false;
            if (!Str(Get(a, "name"), j.meName, sizeof j.meName) || !j.meName[0]) Str(Get(a, "username"), j.meName, sizeof j.meName);
            Str(Get(a, "username"), j.meUser, sizeof j.meUser);
            Str(Get(a, "friend_code"), j.meCode, sizeof j.meCode);
            j.mePid = (u64)Num(Get(a, "pid"));
            const Js p = Get(r, "presence");
            j.meStatus = 0; j.meTid = 0; j.meGame[0] = 0;
            if (p.Ok() && *p.p == '{') {
                const long long st = Num(Get(p, "status"));
                j.meStatus = st <= 0 ? 0 : (st >= 2 ? 2 : 1);
                j.meTid = Hex(Get(p, "app_id"));
                Str(Get(p, "jeu"), j.meGame, sizeof j.meGame);
                if (j.meStatus == 1 && j.meTid && j.meGame[0]) j.meStatus = 2;
            }
            return j.meName[0] != 0;
        }
        void MeJob(char *buf) {
            char *body = nullptr; int bl = 0;
            int st = Api("GET", "/api/me", nullptr, buf, &body, &bl);
            if (st == 200 && body) s_job.me = ParseAccount(body, bl, s_job);
            if (st == 401) { s_job.out = true; return; }
            if (st == 429 || st < 0) return;
            st = Api("GET", "/api/appear-offline", nullptr, buf, &body, &bl);
            if (st == 200 && body) {
                const Js v = Get(Root(body, bl), "appear_offline");
                if (v.Ok()) { s_job.hidden = Bool(v) ? 1 : 0; s_job.canHide = 1; }
            } else if (st == 401 || st == 403) { s_job.canHide = 0; s_job.out = false; }   // (no « profil » on this token: not signed out)
            s_job.ok = s_job.me;
        }
        void HideJob(char *buf) {
            char *body = nullptr; int bl = 0; char js[40];
            snprintf(js, sizeof js, "{\"appear_offline\":%s}", s_job.wantHide ? "true" : "false");
            const int st = Api("POST", "/api/appear-offline", js, buf, &body, &bl);
            s_job.ok = st == 200;
            if (st == 200) { s_job.hidden = s_job.wantHide ? 1 : 0; s_job.canHide = 1; }
            else if (st == 401 || st == 403) { s_job.canHide = 0; s_job.out = false; snprintf(s_job.msg, sizeof s_job.msg, "%s", lang::T("Reconnecte-toi à Nextendo pour changer ton statut")); }
            else if (st != 429) snprintf(s_job.msg, sizeof s_job.msg, "%s", lang::T("Nextendo n'a pas pu changer ton statut"));
        }
        void FetchList(char *buf) {
            char *body = nullptr; int bl = 0;
            if (!s_meName[0] && !s_job.me) {
                const int st = Api("GET", "/api/oauth/userinfo", nullptr, buf, &body, &bl);
                if (st == 200 && body) s_job.me = ParseMe(body, bl, s_job);
                if (st == 429 || s_job.out) return;
            }
            const int st = Api("GET", "/api/friends?images=0", nullptr, buf, &body, &bl);
            if (st == 200 && body) s_job.list = ParseList(body, bl, s_job);
            else if (st == 401) s_job.out = true;
            if (st == 200 && !s_job.list) snprintf(s_job.why, sizeof s_job.why, "reponse illisible");   // lang:log (never drawn)
        }

        // ---- the sign-in server (worker): http://127.0.0.1:8080/callback (the console's browser) and /k/<token> (a phone) ----
        void Send(int fd, const char *s, int n) { while (n > 0) { const ssize_t w = send(fd, s, (size_t)n, 0); if (w <= 0) break; s += w; n -= (int)w; } }
        void Page(int fd, int code, const char *html) {
            char h[160];
            const int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %d\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                                   code, code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request"), (int)strlen(html));
            Send(fd, h, n); Send(fd, html, (int)strlen(html));
        }
        constexpr const char *kCss = "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Brume</title>"
            "<style>body{background:#03050d;color:#cfd8f5;font:17px/1.55 system-ui,sans-serif;margin:0;padding:30px 22px;max-width:560px}"
            "h1{color:#f2db66;font-weight:300;letter-spacing:.06em}a.b,button{display:block;width:100%;box-sizing:border-box;margin:16px 0;padding:15px;border:0;"
            "border-radius:16px;background:#2747c9;color:#fff;font-size:17px;text-align:center;text-decoration:none}textarea{width:100%;box-sizing:border-box;"
            "height:120px;border-radius:14px;border:1px solid #273566;background:#0a1028;color:#fff;padding:10px;font-size:15px}.f{color:#7f8cb8;font-size:14px}</style>"
            "</head><body><h1>Brume</h1>";
        void Simple(int fd, int code, const char *text) {
            char *h = (char *)malloc(2048);
            if (!h) return;
            snprintf(h, 2048, "%s<p>%s</p></body></html>", kCss, text);
            Page(fd, code, h);
            free(h);
        }
        void PhonePage(int fd) {
            char *h = (char *)malloc(6144), *href = (char *)malloc(1100);
            if (h && href) {
                int k = 0;                              // the link, & written &amp;
                for (const char *p = s_authUrl; *p && k + 6 < 1100; p++) { if (*p == '&') { memcpy(href + k, "&amp;", 5); k += 5; } else href[k++] = *p; }
                href[k] = 0;
                snprintf(h, 6144, "%s<p>%s</p><a class=b href=\"%s\">%s</a><p>%s</p><form method=post><textarea name=u placeholder='http://127.0.0.1:8080/callback?code=...'></textarea>"
                         "<button>%s</button></form><p class=f>%s</p></body></html>", kCss,
                         lang::T("1. Touche le bouton ci-dessous, connecte-toi à Nextendo et accepte."), href, lang::T("Se connecter à Nextendo"),
                         lang::T("2. Ton téléphone arrive ensuite sur une page qui ne s'ouvre pas (127.0.0.1) : c'est normal. Copie toute son adresse et colle-la ici :"),
                         lang::T("Envoyer à la console"),
                         lang::T("Cette page vient de ta console (même Wi-Fi). Elle s'arrête toute seule dans 5 minutes."));
                Page(fd, 200, h);
            }
            free(h); free(href);
        }
        // one connection: true when the code arrived (s_job.code holds it)
        bool Serve(int fd) {
            timeval tv = { 3, 0 };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            char *req = (char *)malloc(4097);
            if (!req) return false;
            int n = 0, need = -1;
            while (n < 4096) {
                const ssize_t r = recv(fd, req + n, (size_t)(4096 - n), 0);
                if (r <= 0) break;
                n += (int)r; req[n] = 0;
                char *h = strstr(req, "\r\n\r\n");
                if (h && need < 0) {
                    need = (int)(h + 4 - req);
                    for (char *l = strstr(req, "\r\n"); l && l < h; l = strstr(l + 2, "\r\n"))
                        if (!strncasecmp(l + 2, "content-length:", 15)) { const long c = strtol(l + 17, nullptr, 10); if (c > 0 && c < 3000) need += (int)c; }
                }
                if (need >= 0 && n >= need) break;
            }
            req[n] = 0;
            bool got = false;
            char path[600] = {};
            const bool post = !strncmp(req, "POST ", 5);
            const char *sp = strchr(req, ' ');
            if (sp) { int k = 0; for (const char *p = sp + 1; *p && *p != ' ' && k + 1 < (int)sizeof path; p++) path[k++] = *p; path[k] = 0; }
            char kpath[24]; snprintf(kpath, sizeof kpath, "/k/%s", s_phoneTok);
            const char *q = strchr(path, '?');
            char code[sizeof s_job.code] = {}, state[48] = {}, err[48] = {};
            auto Take = [&](const char *qs) -> bool {    // a query with code + state (ours), or an error
                if (Param(qs, "error", err, sizeof err)) { s_job.denied = true; snprintf(s_job.why, sizeof s_job.why, "%s", err); Simple(fd, 200, lang::T("Connexion annulée. Tu peux revenir sur Brume.")); return true; }   // (v96: its reason kept)
                if (Param(qs, "code", code, sizeof code) && Param(qs, "state", state, sizeof state) && code[0] && !strcmp(state, s_state)) {
                    snprintf(s_job.code, sizeof s_job.code, "%s", code);
                    Simple(fd, 200, lang::T("C'est bon ! Retourne sur Brume, la connexion se termine."));
                    return true;
                }
                return false;
            };
            if (!post && !strncmp(path, "/callback", 9) && q) {
                got = Take(q + 1);
                if (!got) Simple(fd, 400, lang::T("Ce lien n'est pas le bon. Recommence depuis Brume."));
            } else if (s_phoneTok[0] && !strncmp(path, kpath, strlen(kpath)) && (path[strlen(kpath)] == 0 || path[strlen(kpath)] == '?')) {
                if (post) {
                    char *b = strstr(req, "\r\n\r\n");
                    char pasted[1200] = {};
                    if (b && Param(b + 4, "u", pasted, sizeof pasted)) {
                        const char *pq = strchr(pasted, '?');
                        got = pq && Take(pq + 1);
                    }
                    if (!got) Simple(fd, 400, lang::T("Je ne trouve pas le code dans cette adresse. Copie bien toute l'adresse de la page 127.0.0.1."));
                } else PhonePage(fd);
            } else Page(fd, 404, "<!doctype html><title>404</title>");
            memset(req, 0, 4097); free(req);
            return got;
        }
        void Exchange(char *buf) {
            char *form = (char *)malloc(1400);
            if (!form) return;
            char c[200], v[100], r[100], cid[100];
            UrlEnc(s_job.code, c, sizeof c); UrlEnc(s_verifier, v, sizeof v); UrlEnc(kRedirect, r, sizeof r); UrlEnc(s_client, cid, sizeof cid);
            snprintf(form, 1400, "grant_type=authorization_code&client_id=%s&code=%s&code_verifier=%s&redirect_uri=%s", cid, c, v, r);
            s_job.ok = TokenPost(form, buf);
            memset(form, 0, 1400); free(form);
        }
        void LoginJob(char *buf) {
            const int srv = socket(AF_INET, SOCK_STREAM, 0);
            if (srv < 0) { snprintf(s_job.why, sizeof s_job.why, "socket"); return; }
            int one = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            sockaddr_in a; memset(&a, 0, sizeof a);
            a.sin_family = AF_INET; a.sin_port = __builtin_bswap16((uint16_t)kPort); a.sin_addr.s_addr = 0;   // (network order; any address)
            if (bind(srv, (sockaddr *)&a, sizeof a) != 0 || listen(srv, 4) != 0) {
                close(srv); __atomic_store_n(&s_phase, -1, __ATOMIC_RELEASE);
                snprintf(s_job.why, sizeof s_job.why, "port %d", kPort);
                return;
            }
            const u64 t0 = armGetSystemTick(), limit = armNsToTicks((u64)(kServeFor * 1e9f));
            bool got = false;
            while (!got && !__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE) && armGetSystemTick() - t0 < limit && !qext_game_running()) {
                pollfd p = { srv, POLLIN, 0 };
                if (poll(&p, 1, 250) <= 0 || !(p.revents & POLLIN)) continue;
                const int c = accept(srv, nullptr, nullptr);
                if (c < 0) continue;
                got = Serve(c);
                close(c);
                if (s_job.denied) break;
            }
            close(srv);
            if (!got || s_job.denied) { if (!s_job.denied && !s_job.why[0]) snprintf(s_job.why, sizeof s_job.why, "delai"); return; }   // lang:log (never drawn)
            __atomic_store_n(&s_phase, 2, __ATOMIC_RELEASE);
            Exchange(buf);
            memset(s_job.code, 0, sizeof s_job.code);
            if (s_job.ok) FetchList(buf);
        }
        void ImageJob(char *buf) {
            // v96: imgKind 3 = my own picture, 128 px (the profile page), into s_job.big; the others 64 px into s_job.bc
            const bool big = s_job.imgKind == 3;
            const int px = big ? 128 : kPx;
            const size_t bytes = big ? sizeof s_job.big : (size_t)kPicBytes;
            uint8_t *out = big ? s_job.big : s_job.bc;
            char path[96];
            if (s_job.imgKind == 1) snprintf(path, sizeof path, "%s/a%llu_%08x.bc1", kCacheDir, (unsigned long long)s_job.imgId, (unsigned)s_job.imgVer);
            else if (big) snprintf(path, sizeof path, "%s/m%llu.bc1", kCacheDir, (unsigned long long)s_job.imgId);
            else snprintf(path, sizeof path, "%s/g%016llx.bc1", kCacheDir, (unsigned long long)s_job.imgId);
            auto Cached = [&]() -> bool {
                FILE *f = fopen(path, "rb");
                if (!f) return false;
                const size_t r = fread(out, 1, bytes, f);
                fclose(f);
                return r == bytes;
            };
            // kept on the card: never downloaded twice (my picture: once per start-up, it may have changed - the card's copy
            // is only the way out without a network)
            if (!(big && s_job.fresh) && Cached()) { s_job.img = 1; return; }
            uint8_t *rgba = (uint8_t *)malloc((size_t)px * px * 4);
            if (!rgba) { s_job.img = -1; return; }
            bool ok = false;
            if (s_job.imgKind == 2 && s_job.imgIndex >= 0) {   // an installed game: its own icon (no network)
                const int sz = qext_title_icon_size(s_job.imgIndex);
                const int got = sz > 0 && sz <= kBuf ? qext_title_icon(s_job.imgIndex, buf, kBuf) : 0;
                ok = got > 0 && DecodeJpegSquare(buf, got, rgba, px);
            }
#ifndef BRUME_UISIM
            if (!ok) {
                char url[80];
                if (s_job.imgKind != 2) snprintf(url, sizeof url, "/api/avatar?pid=%llu", (unsigned long long)s_job.imgId);
                else snprintf(url, sizeof url, "/api/game-icon/%016llx.jpg", (unsigned long long)s_job.imgId);
                if (NetUp()) {
                    char *body = nullptr; int bl = 0, ra = 0;
                    const int st = nextendo::Call("GET", url, nullptr, nullptr, buf, kBuf, &body, &bl, &ra, s_job.why, sizeof s_job.why);
                    s_job.http = st;
                    if (st == 200 && body && bl > 0) ok = DecodeJpegSquare(body, bl, rgba, px);
                    else if (st == 429) { s_job.retry = ra ? ra : 60; s_job.img = -1; }
                    else if (st == 404) s_job.img = 0;
                    else s_job.img = -1;
                    NetDown();
                } else s_job.img = -1;
            }
#else
            if (!ok) {                                   // the PC preview: a made-up picture (soft colours, a highlight)
                const u32 h = (u32)(s_job.imgId * 2654435761u) ^ s_job.imgVer;
                const float hue = (h % 360) / 360.0f, half = px * 0.5f;
                for (int y = 0; y < px; y++) for (int x = 0; x < px; x++) {
                    const float fx = (x - half) / half, fy = (y - half) / half, d = sqrtf(fx * fx + fy * fy);
                    const float t = hue * 6.2832f + (s_job.imgKind == 2 ? fx * 2.0f : fy * 1.4f);
                    float r = 0.5f + 0.4f * cosf(t), g = 0.5f + 0.4f * cosf(t - 2.1f), b = 0.55f + 0.4f * cosf(t + 2.1f);
                    const float face = s_job.imgKind != 2 ? (1 - Clamp01((sqrtf(fx * fx + (fy + 0.15f) * (fy + 0.15f)) - 0.38f) * 8)) * 0.35f : 0.0f;
                    const float sh = 0.75f + 0.25f * (1 - d) + face;
                    uint8_t *o = rgba + (y * px + x) * 4;
                    o[0] = (uint8_t)(fminf(1, r * sh) * 255); o[1] = (uint8_t)(fminf(1, g * sh) * 255); o[2] = (uint8_t)(fminf(1, b * sh) * 255); o[3] = 255;
                }
                ok = true;
            }
#endif
            if (ok) {
                Shelf::EncodeBC1(rgba, px, px, out);
                s_job.img = 1;
                mkdir("sdmc:/brume/cache", 0777); mkdir(kCacheDir, 0777);
                if (FILE *f = fopen(path, "wb")) { fwrite(out, 1, bytes, f); fclose(f); }
            } else if (big && Cached()) s_job.img = 1;   // (no network: the card's copy)
            else if (s_job.img > 0) s_job.img = -1;
            free(rgba);
        }
        void ActionJob(char *buf) {
            char *body = nullptr; int bl = 0; char js[96];
            const char *path = "/api/friends";
            if (s_job.act == kAdd) {
                char c[40]; int k = 0;                   // (only what a friend code can hold)
                for (const char *p = s_job.code; *p && k + 1 < (int)sizeof c; p++) if ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '-') c[k++] = *p;
                c[k] = 0;
                snprintf(js, sizeof js, "{\"friend_code\":\"%s\"}", c);
            } else {
                path = s_job.act == kAccept ? "/api/friends/accept" : "/api/friends/decline";
                snprintf(js, sizeof js, "{\"pid\":%llu}", (unsigned long long)s_job.pid);
            }
            const int st = Api("POST", path, js, buf, &body, &bl);
            s_job.ok = st == 200;
            if (s_job.act == kAdd) {
                if (st == 200) {
                    Friend f; ParseFriend(Get(Root(body, bl), "friend"), f);
                    const bool already = Bool(Get(Root(body, bl), "already"));
                    snprintf(s_job.msg, sizeof s_job.msg, already ? lang::T("Déjà demandé à %s") : lang::T("Demande envoyée à %s"), f.name[0] && f.pid ? f.name : lang::T("ton ami"));
                }
                else if (st == 404) snprintf(s_job.msg, sizeof s_job.msg, "%s", lang::T("Ce code ne correspond à personne"));
                else if (st == 400) snprintf(s_job.msg, sizeof s_job.msg, "%s", lang::T("C'est ton propre code"));
                else if (st == 409) snprintf(s_job.msg, sizeof s_job.msg, "%s", lang::T("Une liste d'amis est pleine (300 au plus)"));
            }
            if (st > 0 && st != 429 && !s_job.out) FetchList(buf);
        }
        void LogoutJob(char *buf) {
            char *form = (char *)malloc(1400);
            if (form && s_refresh[0]) {
                char rt[1100], cid[100];
                UrlEnc(s_refresh, rt, sizeof rt); UrlEnc(s_client, cid, sizeof cid);
                snprintf(form, 1400, "token=%s&client_id=%s", rt, cid);
                char hd[160]; snprintf(hd, sizeof hd, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: %d\r\n", (int)strlen(form));
                char *body = nullptr; int bl = 0, ra = 0;
                s_job.http = nextendo::Call("POST", "/api/oauth/revoke", hd, form, buf, kBuf, &body, &bl, &ra, s_job.why, sizeof s_job.why);
                memset(rt, 0, sizeof rt); memset(form, 0, 1400);
            }
            free(form);
            s_job.ok = true;
        }

#ifdef BRUME_UISIM
        // ---- the PC preview: canned answers, read by the same parser ----
        const char *kSimList =
            "{\"friends\":[{\"pid\":1801,\"username\":\"leo_kart\",\"name\":\"L\\u00e9o\",\"friend_code\":\"SW-2214-8830-1290\",\"avatar\":\"a1\",\"favorite\":true,"   // lang:log (the preview's canned answer, never drawn as it is)
            "\"presence\":{\"status\":2,\"app_id\":\"0100000000010000\",\"jeu\":\"Mario Kart 8 Deluxe\",\"app_detail\":\"Course en ligne \\u00b7 150cc\"}},"
            "{\"pid\":1802,\"username\":\"ines\",\"name\":\"In\\u00e8s\",\"friend_code\":\"SW-9031-1157-0042\",\"avatar\":\"b2\",\"presence\":{\"status\":1}},"
            "{\"pid\":1803,\"username\":\"samsplat\",\"name\":\"Sam\",\"friend_code\":\"SW-5520-3318-7781\",\"avatar\":\"c3\","
            "\"presence\":{\"status\":2,\"app_id\":\"01003BC0000A0000\",\"jeu\":\"Splatoon 2\",\"app_detail\":\"Guerre de territoire\"}},"
            "{\"pid\":1804,\"username\":\"noa\",\"name\":\"Noa\",\"friend_code\":\"SW-1200-4471-9930\",\"avatar\":\"d4\",\"presence\":{\"status\":0}},"
            "{\"pid\":1805,\"username\":\"maxou\",\"name\":\"Max\",\"friend_code\":\"SW-7700-2210-5561\",\"avatar\":\"\",\"presence\":{\"status\":0}},"
            "{\"pid\":1806,\"username\":\"broken\",\"presence\":\"???\"},{\"pid\":\"x\"},"
            "{\"pid\":1807,\"username\":\"ana\",\"name\":\"Ana\",\"friend_code\":\"SW-3399-0012-4417\",\"avatar\":\"e5\",\"presence\":{\"status\":0}}],"
            "\"requests\":[{\"pid\":1900,\"username\":\"lucas\",\"name\":\"Lucas\",\"friend_code\":\"SW-6612-0098-3345\",\"avatar\":\"f6\"}],\"sent\":[]}";
        const char *kSimList2 =   // 2 minutes later: Inès starts a game, Noa comes online, a new request
            "{\"friends\":[{\"pid\":1801,\"name\":\"L\\u00e9o\",\"friend_code\":\"SW-2214-8830-1290\",\"avatar\":\"a1\",\"favorite\":true,"   // lang:log (the preview's canned answer)
            "\"presence\":{\"status\":2,\"app_id\":\"0100000000010000\",\"jeu\":\"Mario Kart 8 Deluxe\",\"app_detail\":\"Course en ligne \\u00b7 150cc\"}},"
            "{\"pid\":1802,\"name\":\"In\\u00e8s\",\"avatar\":\"b2\",\"presence\":{\"status\":2,\"app_id\":\"01006A800016E000\",\"jeu\":\"Super Smash Bros. Ultimate\",\"app_detail\":\"Arène\"}},"
            "{\"pid\":1803,\"name\":\"Sam\",\"avatar\":\"c3\",\"presence\":{\"status\":2,\"app_id\":\"01003BC0000A0000\",\"jeu\":\"Splatoon 2\"}},"
            "{\"pid\":1804,\"name\":\"Noa\",\"avatar\":\"d4\",\"presence\":{\"status\":1}},{\"pid\":1805,\"name\":\"Max\",\"presence\":{\"status\":0}}],"
            "\"requests\":[{\"pid\":1900,\"name\":\"Lucas\",\"avatar\":\"f6\"},{\"pid\":1901,\"name\":\"Zo\\u00e9\",\"avatar\":\"g7\"}]}";
        int s_simList = 1, s_simMe = 1, s_simHidden = 0;
        void SimJob() {
            switch (s_job.kind) {
                case kJobLogin: case kJobList: case kJobAction: {
                    const char *me = "{\"scope\":\"identity friends presence profil\",\"pid\":1700,\"username\":\"yass\",\"display_name\":\"Yass\",\"friend_code\":\"SW-4821-0937-1166\"}";   // lang:log (the preview's canned answer)
                    s_job.me = ParseMe(me, (int)strlen(me), s_job);
                    const char *l = s_simList == 2 ? kSimList2 : kSimList;
                    s_job.list = ParseList(l, (int)strlen(l), s_job);
                    s_job.ok = true;
                    if (s_job.kind == kJobLogin) { snprintf(s_access, sizeof s_access, "sim"); snprintf(s_refresh, sizeof s_refresh, "sim"); }
                    if (s_job.kind == kJobAction && s_job.act == kAdd) snprintf(s_job.msg, sizeof s_job.msg, lang::T("Demande envoyée à %s"), "Zo\xc3\xa9");
                    break;
                }
                case kJobImage: { char b[16]; ImageJob(b); break; }
                case kJobLogout: s_job.ok = true; break;
                case kJobMe: {                           // v96: the account, its presence (s_simMe), invisible (s_simHidden)
                    char me[400];
                    snprintf(me, sizeof me, "{\"account\":{\"pid\":1700,\"username\":\"yass\",\"name\":\"Yass\",\"friend_code\":\"SW-4821-0937-1166\"},\"presence\":%s}",   // lang:log (the preview's canned answer)
                             s_simMe == 2 ? "{\"status\":2,\"app_id\":\"0100000000010000\",\"jeu\":\"Mario Kart 8 Deluxe\"}" : (s_simMe == 1 ? "{\"status\":1}" : "{\"status\":0}"));   // lang:log
                    s_job.me = ParseAccount(me, (int)strlen(me), s_job);
                    s_job.hidden = s_simHidden; s_job.canHide = 1; s_job.ok = true;
                    break;
                }
                case kJobHide: s_job.hidden = s_job.wantHide ? 1 : 0; s_simHidden = s_job.hidden; s_job.canHide = 1; s_job.ok = true; break;
            }
        }
#endif
        void Run(void *) {
#ifdef BRUME_UISIM
            SimJob();
            return;
#else
            if (s_job.kind == kJobImage) { char *b = (char *)malloc(kBuf); if (b) { ImageJob(b); free(b); } else s_job.img = -1; return; }
            char *buf = (char *)malloc(kBuf);
            if (!buf) { snprintf(s_job.why, sizeof s_job.why, "memoire"); return; }
            if (!NetUp()) { snprintf(s_job.why, sizeof s_job.why, "reseau indisponible"); free(buf); return; }   // lang:log (never drawn)
            switch (s_job.kind) {
                case kJobLogin:  LoginJob(buf); break;
                case kJobList:   FetchList(buf); break;
                case kJobAction: ActionJob(buf); break;
                case kJobLogout: LogoutJob(buf); break;
                case kJobMe:     MeJob(buf); break;          // v96
                case kJobHide:   HideJob(buf); break;
            }
            NetDown();
            memset(buf, 0, kBuf); free(buf);
#endif
        }

        // ======================= main thread =======================
        bool NetOk() {
            if (qext_game_running() || nextendo::Busy() || covers::Busy() || spotify::Busy()) return false;   // (v98: nor during a Spotify cover)
#ifndef BRUME_UISIM
            NifmInternetConnectionType t; u32 st = 0; NifmInternetConnectionStatus cs;
            if (R_FAILED(nifmGetInternetConnectionStatus(&t, &st, &cs)) || cs != NifmInternetConnectionStatus_Connected) return false;
#endif
            return true;
        }
        bool Start(int kind, float now) {                // (the caller set the job's inputs: act, pid, code, img*)
            if (!s_work.Idle()) return false;
            Job &j = s_job;
            j.kind = kind; j.http = j.retry = 0; j.why[0] = j.msg[0] = 0; j.ok = j.out = j.list = j.me = j.denied = false;
            j.nf = j.nrq = 0; j.meName[0] = j.meCode[0] = 0; j.img = 0;
            j.meUser[0] = j.meGame[0] = 0; j.mePid = j.meTid = 0; j.meStatus = j.hidden = j.canHide = -1;   // v96 (wantHide / fresh: set by the caller)
            if (kind != kJobAction && kind != kJobLogin) j.code[0] = 0;
            if (kind != kJobImage && s_task < 0) s_task = tasks::Begin(now, "Amis (Nextendo)");
            return s_work.Start(Run, nullptr, 0x14000);
        }
        bool Installed(u64 tid, int *index = nullptr) {
            if (!tid) return false;
            for (int j = 0, t = qext_title_count(); j < t; j++)
                if (qext_title_id(j) == tid) { if (qext_title_is_virtual(tid)) return false; if (index) *index = j; return true; }
            return false;
        }
        const char *GameName(const Friend &f, char *tmp, int cap) {
            if (f.game[0]) return f.game;
            int idx = -1;
            if (Installed(f.tid, &idx) && qext_title_name(idx, tmp, (unsigned)cap) > 0) return tmp;
            return lang::T("un jeu");
        }
        const Friend *Find(const Friend *a, int n, u64 pid) { for (int i = 0; i < n; i++) if (a[i].pid == pid) return &a[i]; return nullptr; }
        // a new list: what changed is said (not the first one after boot / sign-in), then it becomes the page's
        void TakeList(float now) {
            if (!s_firstList) {
                int said = 0;
                for (int i = 0; i < s_job.nf && said < 3; i++) {
                    const Friend &f = s_job.f[i];
                    const Friend *o = Find(s_f, s_nf, f.pid);
                    if (!o) continue;
                    char m[160], g[96], nm[48];
                    Cut(f.name, nm, sizeof nm, 16);
                    if (f.status == 2 && (o->status != 2 || o->tid != f.tid)) { char gc[96]; Cut(GameName(f, g, sizeof g), gc, sizeof gc, 30); snprintf(m, sizeof m, lang::T("%s joue à %s"), nm, gc); notify::Post(now, m); said++; }
                    else if (o->status == 0 && f.status == 1) { snprintf(m, sizeof m, lang::T("%s est en ligne"), nm); notify::Post(now, m); said++; }
                }
                for (int i = 0; i < s_job.nrq && said < 4; i++)
                    if (!Find(s_rq, s_nrq, s_job.rq[i].pid)) {
                        char m[160], nm[48]; Cut(s_job.rq[i].name, nm, sizeof nm, 16);
                        snprintf(m, sizeof m, lang::T("%s veut être ton ami"), nm); notify::Post(now, m); said++;
                    }
            }
            s_firstList = false;
            memcpy(s_f, s_job.f, sizeof s_f); s_nf = s_job.nf;
            memcpy(s_rq, s_job.rq, sizeof s_rq); s_nrq = s_job.nrq;
            s_haveList = true;
        }
        void BuildRows() {
            RowT keep = s_nrows && s_sel < s_nrows ? s_rows[s_sel] : RowT{ kRAdd, -1 };
            const u64 keepPid = keep.kind == kRFriend && keep.idx >= 0 ? s_f[keep.idx].pid : (keep.kind == kRReq && keep.idx >= 0 ? s_rq[keep.idx].pid : 0);
            s_nrows = 0;
            s_rows[s_nrows++] = { kRAdd, -1 };
            s_rows[s_nrows++] = { kRDiscord, -1 };
            for (int i = 0; i < s_nrq; i++) s_rows[s_nrows++] = { kRReq, (int8_t)i };
            for (int i = 0; i < s_nf; i++) s_rows[s_nrows++] = { kRFriend, (int8_t)i };
            s_rows[s_nrows++] = { kRLogout, -1 };
            int sel = -1;
            for (int i = 0; i < s_nrows && sel < 0; i++) {
                const RowT &r = s_rows[i];
                if (r.kind != keep.kind) continue;
                if (r.kind == kRFriend) { if (s_f[r.idx].pid == keepPid) sel = i; }
                else if (r.kind == kRReq) { if (s_rq[r.idx].pid == keepPid) sel = i; }
                else sel = i;
            }
            if (sel < 0) sel = s_sel < s_nrows ? s_sel : s_nrows - 1;
            s_sel = sel < 0 ? 0 : sel;
            // the rows' places: a section's name takes a little room above its first row
            float y = 0;
            for (int i = 0; i < s_nrows; i++) {
                const bool head = (s_rows[i].kind == kRReq && (i == 0 || s_rows[i - 1].kind != kRReq)) ||
                                  (s_rows[i].kind == kRFriend && (i == 0 || s_rows[i - 1].kind != kRFriend)) || s_rows[i].kind == kRLogout;
                if (head && i > 0) y += 34;
                s_rowY[i] = y;
                y += 58;
            }
        }
        // ---- the strip ----
        // (v96: the profile page too, and once to put my picture in the top bar when the profile chose it)
        bool StripWanted(bool page) { return s_texOk && (page || s_dOpen || s_login == 2 || s_profile || (s_barNextendo && s_meBigOk && s_texSq != 2)); }
        void MirrorOn() {
            if (s_mirror || !s_texOk) return;
            s_mirror = (uint8_t *)calloc(1, kStripBytes);
            memset(s_slot, 0, sizeof s_slot);
            s_qrFor[0] = 0; s_qrPx = 0; s_sq = 0;
        }
        void MirrorOff() { if (s_mirror && !s_dirty) { free(s_mirror); s_mirror = nullptr; memset(s_slot, 0, sizeof s_slot); s_qrFor[0] = 0; } }
        int SlotOf(int kind, u64 id, u32 ver) {
            for (int i = 0; i < kSlots; i++) if (s_slot[i].kind == kind && s_slot[i].id == id && s_slot[i].ver == ver) return i;
            return -1;
        }
        bool Missing(int kind, u64 id, u32 ver) { for (int i = 0; i < s_nmiss; i++) if (s_miss[i].kind == kind && s_miss[i].id == id && s_miss[i].ver == ver) return true; return false; }
        void Put(int kind, u64 id, u32 ver, const uint8_t *bc) {
            if (!s_mirror) return;
            int best = 0; u32 oldest = 0xFFFFFFFF;
            for (int i = 0; i < kSlots; i++) {
                if (!s_slot[i].kind) { best = i; break; }
                if (s_slot[i].used < oldest) { oldest = s_slot[i].used; best = i; }
            }
            const int x0 = (best % kCols) * kPx, y0 = (best / kCols) * kPx;
            for (int by = 0; by < kPx / 4; by++)
                memcpy(s_mirror + ((size_t)(y0 / 4 + by) * (kW / 4) + (size_t)(x0 / 4)) * 8, bc + (size_t)by * (kPx / 4) * 8, (kPx / 4) * 8);
            s_slot[best].kind = (uint8_t)kind; s_slot[best].id = id; s_slot[best].ver = ver; s_slot[best].used = s_frame;
            s_dirty = true;
        }
        void SetQr(const char *text) {
            if (!s_mirror || !strcmp(text, s_qrFor)) return;
            static uint8_t mods[qr::kMaxSide * qr::kMaxSide];
            const int n = qr::Make(text, mods, (int)sizeof mods);
            snprintf(s_qrFor, sizeof s_qrFor, "%s", text);
            s_qrPx = 0;
            if (n <= 0) return;
            const int m = 128 / (n + 8) < 1 ? 1 : 128 / (n + 8), px = m * (n + 8), off = (128 - px) / 2;
            uint8_t *rgba = (uint8_t *)malloc(128 * 128 * 4), *bc = (uint8_t *)malloc(32 * 32 * 8);
            if (rgba && bc) {
                for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
                    const int qx = (x - off) / m - 4, qy = (y - off) / m - 4;
                    const bool dark = x >= off && y >= off && qx >= 0 && qy >= 0 && qx < n && qy < n && mods[qy * n + qx];
                    uint8_t *o = rgba + (y * 128 + x) * 4;
                    o[0] = o[1] = o[2] = dark ? 0 : 255; o[3] = 255;
                }
                Shelf::EncodeBC1(rgba, 128, 128, bc);
                for (int by = 0; by < 32; by++) memcpy(s_mirror + ((size_t)by * (kW / 4) + kQrX / 4) * 8, bc + (size_t)by * 32 * 8, 32 * 8);
                s_qrPx = px; s_dirty = true; s_sq = 1;
            }
            free(rgba); free(bc);
        }
        // v96 - my picture (128 px) into the square, when no QR code needs it
        void PutMe() {
            if (!s_mirror || !s_meBigOk || s_sq == 2) return;
            for (int by = 0; by < 32; by++) memcpy(s_mirror + ((size_t)by * (kW / 4) + kQrX / 4) * 8, s_meBig + (size_t)by * 32 * 8, 32 * 8);
            s_qrFor[0] = 0; s_qrPx = 0; s_sq = 2; s_dirty = true;
        }
        // a picture: drawn from the strip when it is there, else asked for (the page's Update picks the first one missing)
        struct Want { uint8_t kind; u64 id; u32 ver; int index; };
        Want s_want[12]; int s_nwant = 0;
        void Ask(int kind, u64 id, u32 ver) {
            if (!id || s_nwant >= 12 || Missing(kind, id, ver)) return;
            for (int i = 0; i < s_nwant; i++) if (s_want[i].kind == kind && s_want[i].id == id && s_want[i].ver == ver) return;
            int idx = -1;
            if (kind == 2) Installed(id, &idx);
            s_want[s_nwant++] = { (uint8_t)kind, id, ver, idx };
        }
        void Initial(Ui &ui, const char *name, float cx, float cy, float r, float a) {
            ui.Light(cx - r * 1.6f, cy - r * 1.6f, cx + r * 1.6f, cy + r * 1.6f, ps2::A(ps2::kHalo, 0.30f * a), true);
            ui.Shape(cx - r, cy - r, cx + r, cy + r, 0, 0, 2 * r, 2 * r, Settings::Accent(0.32f * a), 24, 2 * r, 2 * r, r);
            char ini[8] = {};
            const char *q = name; const uint32_t cp = NextCodepoint(q);
            const int n = (int)(q - name);
            if (n > 0 && n < 5) { memcpy(ini, name, (size_t)n); ui.TextCentered(r > 30 ? fonts::kGameTitle : fonts::kSetOpt, ini, cx, cy - (r > 30 ? 16 : 11), 0, ps2::A(ps2::kChosen, 0.9f * a)); }
            (void)cp;
        }
        void Avatar(Ui &ui, const Friend &f, float cx, float cy, float r, float a) {
            const int s = s_mirror ? SlotOf(1, f.pid, f.av) : -1;
            if (s < 0) { Ask(1, f.pid, f.av); Initial(ui, f.name, cx, cy, r, a); return; }
            s_slot[s].used = s_frame;
            const float u0 = (float)((s % kCols) * kPx) / kW, v0 = (float)((s / kCols) * kPx) / kH;
            const float u1 = u0 + (float)kPx / kW, v1 = v0 + (float)kPx / kH;
            ui.Shape(cx - r, cy - r, cx + r, cy + r, u0, v0, u1, v1, Rgba{ 0.86f, 0.88f, 0.96f, a }, 31, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, (u1 - u0) * 0.5f);
        }
        bool Icon(Ui &ui, u64 tid, float cx, float cy, float r, float a) {
            const int s = s_mirror ? SlotOf(2, tid, 0) : -1;
            if (s < 0) { Ask(2, tid, 0); return false; }
            s_slot[s].used = s_frame;
            const float u0 = (float)((s % kCols) * kPx) / kW, v0 = (float)((s / kCols) * kPx) / kH;
            const float u1 = u0 + (float)kPx / kW, v1 = v0 + (float)kPx / kH;
            ui.Shape(cx - r, cy - r, cx + r, cy + r, u0, v0, u1, v1, Rgba{ 0.9f, 0.92f, 0.98f, a }, 31, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, 1.0f + (u1 - u0) * 0.5f);
            return true;
        }
        void DrawQr(Ui &ui, float cx, float cy, float size, float a) {
            if (!s_qrPx || a <= 0.003f) return;
            const float off = (128 - s_qrPx) * 0.5f;
            const float u0 = (kQrX + off) / kW, v0 = off / kH, u1 = (kQrX + off + s_qrPx) / kW, v1 = (off + s_qrPx) / kH;
            const float h = size * 0.5f;
            ui.Light(cx - h - 120, cy - h - 120, cx + h + 120, cy + h + 120, ps2::A(ps2::kHalo, 0.34f * a), true);   // its light in the fog
            ui.Shape(cx - h, cy - h, cx + h, cy + h, u0, v0, u1, v1, Rgba{ 0.90f, 0.93f, 1.0f, a }, 31, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, -(u1 - u0) * 0.5f);
        }
        // text, left-aligned (translated); returns its width
        float Text(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c, float ls = 0.0f) {
            const float x0 = x;
            for (const char *q = lang::T(s); *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0) + ls;
            return x - x0;
        }
        // a few words that wrap inside `width` (at most `lines` lines), centred on cx; returns the y after them
        float Wrap(Ui &ui, fonts::Style st, const char *s, float cx, float top, float width, float lh, Rgba c, int lines = 3, bool left = false) {
            const char *t = lang::T(s);
            char line[200];
            for (int l = 0; l < lines && *t; l++) {
                int best = 0, k = 0;
                while (t[k] && k < (int)sizeof line - 1) {
                    while (t[k] && t[k] != ' ' && k < (int)sizeof line - 1) k++;
                    memcpy(line, t, (size_t)k); line[k] = 0;
                    if (ui.Width(st, line, 0.3f) > width && best > 0) break;
                    best = k;
                    if (!t[k]) break;
                    k++;
                }
                if (best == 0) best = k;
                memcpy(line, t, (size_t)best); line[best] = 0;
                if (left) Text(ui, st, line, cx, top, c, 0.3f);
                else ui.TextCentered(st, line, cx, top, 0.3f, c);
                top += lh;
                t += best;
                while (*t == ' ') t++;
            }
            return top;
        }
        void Hints(Ui &ui, float a, const char *const *items, int n) {   // the PS2 look: the button's letter in colour, then its word
            const float hc = 720 - 28 - 13.5f;
            float hx = 46;
            for (int i = 0; i + 1 < n; i += 2) {
                const char btn = items[i][0];
                const Rgba bc = btn == 'B' ? ps2::kPink : (btn == 'Y' ? ps2::kGreen : ps2::kChoice);
                hx += ui.Glyph(fonts::kButton, (uint32_t)btn, hx, hc - 10, ps2::A(bc, a), 0, 0, 0, 0) + 10;
                for (const char *q = lang::T(items[i + 1]); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * a), 0, 0, 0, 0) + 0.5f;
                hx += 30;
            }
            const float lw = ui.Advance(fonts::kButton, 'L') + 7 + ui.Advance(fonts::kButton, 'R');
            float rx = 1280 - 46 - (lw + 10 + ui.Width(fonts::kHintBig, "Pages", 0.5f));
            rx += ui.Glyph(fonts::kButton, 'L', rx, hc - 10, ps2::A(ps2::kChoice, a), 0, 0, 0, 0) + 7;
            rx += ui.Glyph(fonts::kButton, 'R', rx, hc - 10, ps2::A(ps2::kChoice, a), 0, 0, 0, 0) + 10;
            for (const char *q = lang::T("Pages"); *q;) rx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), rx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * a), 0, 0, 0, 0) + 0.5f;
        }

        // ---- the sign-in ----
        void B64Url(const uint8_t *in, int n, char *out, int cap) {
            static const char k[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
            int o = 0;
            for (int i = 0; i < n && o + 5 < cap; i += 3) {
                const u32 v = (u32)in[i] << 16 | (i + 1 < n ? (u32)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
                out[o++] = k[(v >> 18) & 63]; out[o++] = k[(v >> 12) & 63];
                if (i + 1 < n) out[o++] = k[(v >> 6) & 63];
                if (i + 2 < n) out[o++] = k[v & 63];
            }
            out[o] = 0;
        }
        void Prepare() {                                 // PKCE (S256), the state, the link, the phone's address
            uint8_t r[32], h[32];
            randomGet(r, sizeof r);
            B64Url(r, 32, s_verifier, sizeof s_verifier);
            sha256CalculateHash(h, s_verifier, strlen(s_verifier));
            char chal[64]; B64Url(h, 32, chal, sizeof chal);
            randomGet(r, 12);
            for (int i = 0; i < 12; i++) snprintf(s_state + i * 2, 3, "%02x", r[i]);
            randomGet(r, 5);
            for (int i = 0; i < 5; i++) snprintf(s_phoneTok + i * 2, 3, "%02x", r[i]);
            char cid[100], red[100], sc[80];
            UrlEnc(s_client, cid, sizeof cid); UrlEnc(kRedirect, red, sizeof red); UrlEnc(kScopes, sc, sizeof sc);
            snprintf(s_authUrl, sizeof s_authUrl, "https://nextendo.network/api/oauth/authorize?client_id=%s&response_type=code&redirect_uri=%s"
                     "&code_challenge=%s&code_challenge_method=S256&scope=%s&state=%s", cid, red, chal, sc, s_state);
            u32 ip = 0;
            s_ip[0] = 0;
#ifdef BRUME_UISIM
            ip = 0x2A01A8C0;                             // (the preview: 192.168.1.42)
#else
            if (R_FAILED(nifmGetCurrentIpAddress(&ip))) ip = 0;
#endif
            if (ip) snprintf(s_ip, sizeof s_ip, "%u.%u.%u.%u", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24);
            snprintf(s_phoneUrl, sizeof s_phoneUrl, "http://%s:%d/k/%s", s_ip[0] ? s_ip : "?", kPort, s_phoneTok);
        }
        void LoginWhy(float now, const char *m) { snprintf(s_loginWhy, sizeof s_loginWhy, "%s", m); s_loginWhyAt = now; }
        void BeginLogin(float now, bool phone) {
            if (s_login) { if (phone && s_ip[0]) { s_login = 2; audio::Play(audio::Tick2400); } else if (phone) audio::Thock(0, 0.8f, 0.06f); return; }
            if (!s_work.Idle()) { Status(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
            if (qext_game_running()) { LoginWhy(now, lang::T("Ferme le jeu en cours pour te connecter")); audio::Thock(0, 0.8f, 0.06f); return; }
            if (!NetOk()) { LoginWhy(now, lang::T("Pas de connexion à Internet")); audio::Thock(0, 0.8f, 0.06f); return; }
            Prepare();
            if (phone && !s_ip[0]) { LoginWhy(now, lang::T("La console n'a pas d'adresse sur le Wi-Fi")); audio::Thock(0, 0.8f, 0.06f); return; }
            __atomic_store_n(&s_stop, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&s_phase, 0, __ATOMIC_RELEASE);
            s_loginWhy[0] = 0;
            if (!Start(kJobLogin, now)) { LoginWhy(now, lang::T("La connexion n'a pas pu démarrer")); return; }
            s_login = phone ? 2 : 1; s_loginAt = now; s_browserMine = false;
            audio::Play(audio::Tick2400);
            audio::Bell(audio::HarmonyNote(1, 72, 84), 0.04f, true);
            haptic::Tap(0.7f);
            if (!phone) {                                // the console's browser, held by the loader (the menu goes on)
                static WebWifiConfig cfg;
                Uuid none; memset(&none, 0, sizeof none);
                webWifiCreate(&cfg, nullptr, s_authUrl, none, 0);
                const Result rc = la::Start(AppletId_LibraryAppletWifiWebAuth, 0, &cfg.arg, sizeof cfg.arg);
                Log("amis : navigateur rc=0x%x", rc);   // lang:log (never drawn)
                if (R_FAILED(rc)) { s_login = 2; LoginWhy(now, lang::T("Le navigateur de la console n'a pas démarré : essaie avec ton téléphone")); }
                else s_browserMine = true;
            }
        }
        void CancelLogin(float now) {
            if (!s_login) return;
            __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
            s_login = 0;
            audio::Play(audio::Tick1700);
            (void)now;
        }

        // ---- « Rejoindre » ----
        void LaunchNow(float now) {
            s_launchAt = now; s_started = false; s_backAt = -100;
            audio::Play(audio::Tick2400);
            audio::Bell(audio::HarmonyNote(0, 72, 84), 0.05f, true, 0.05f);
            audio::Depart(0.3f);
            audio::EntrySound(true, 0.0f);
            haptic::Swell(0.8f, 0.6f);
        }
        void CloseYes(void *, float now) { LaunchNow(now); }
        void Join(const Friend &f, float now) {
            int idx = -1;
            if (!Installed(f.tid, &idx)) { Status(now, lang::T("Tu n'as pas ce jeu")); audio::Thock(0, 0.8f, 0.06f); return; }
            s_launchTid = f.tid;
            if (qext_game_running()) {
                if (qext_suspended_title() == f.tid) {   // that game is already open: it comes back
                    const Result rr = qext_resume_game();
                    if (R_SUCCEEDED(rr)) { audio::Play(audio::Tick2400); return; }
                }
                popup::Spec s;
                s.kind = popup::kDanger; s.title = "Fermer le jeu en cours ?";
                s.text = "Il sera fermé pour lancer celui de ton ami. Ce qui n'est pas sauvegardé sera perdu.";
                s.yes = "Oui"; s.no = "Non"; s.onYes = CloseYes;
                popup::Show(now, s);
                return;
            }
            LaunchNow(now);
        }
    }

    // ============================================================================================================================

    bool Init(Gpu &gpu, uint32_t descriptor) {
        uint32_t align = 1;
        const uint32_t bytes = gpu.ImageBytes(kW, kH, DkImageFormat_RGB_BC1, 1, &align);
        if (descriptor >= 64) return false;
        const uint32_t off = gpu.ReserveImage(bytes, align);
        if (off == UINT32_MAX) return false;
        s_texOk = gpu.PlaceTexture(s_tex, off, kW, kH, DkImageFormat_RGB_BC1, 1, descriptor);
        return s_texOk;
    }
    const Target *Strip() { return s_texOk ? &s_tex : nullptr; }
    bool Visible(const Wheel &w) { return w.Active() && fabsf(w.PageAnim() - kPage) < 1.0f; }
    bool NeedsStream() { return s_dirty && s_mirror && s_texOk; }
    void Stream(Gpu &gpu) {
        if (!NeedsStream()) return;
        if (gpu.StreamTexture(s_tex, s_mirror, kStripBytes, 1)) { s_dirty = false; s_texSq = s_sq; }   // (v96: what the square holds on the GPU now)
    }
    bool NetBusy() { return !s_work.Idle(); }

    // ---- v96: the profile page's side (MyPage.cpp) ----
    Me GetMe() {
        if (s_auth == kUnknown) LoadConfig();
        Me m;
        m.in = s_auth == kIn; m.noClient = s_auth == kNoClient;
        m.busy = !s_work.Idle() && (s_job.kind == kJobMe || s_job.kind == kJobHide || s_job.kind == kJobList || s_job.kind == kJobLogout);
        m.name = s_meName; m.user = s_meUser; m.code = s_meCode;
        m.status = s_meStatus; m.tid = s_meTid; m.game = s_meGame;
        m.hidden = s_hidden; m.canHide = s_canHide;
        return m;
    }
    void ProfileShown(bool on, float now) {
        if (on && !s_profile) {
            if (s_auth == kIn && now - s_meAt > 20.0f) s_wantMeAt = now;
            if (!s_meFresh) s_meBigTried = false;        // (once per start-up, my picture from Nextendo itself)
        }
        s_profile = on;
    }
    void RefreshMe(float now) { if (s_auth == kIn && now - s_meAt > 4.0f) s_wantMeAt = now; }
    void SetHidden(bool hide, float now) {
        if (s_auth != kIn) return;
        if (!s_work.Idle()) { notify::Post(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
        if (!NetOk()) { notify::Post(now, lang::T("Pas de connexion à Internet"), notify::kError); audio::Thock(0, 0.8f, 0.06f); return; }
        s_job.wantHide = hide;
        if (Start(kJobHide, now)) { s_hidden = hide ? 1 : 0; }   // (shown at once; the answer corrects it if it fails)
    }
    void SignOut(float now) {
        if (s_auth != kIn) return;
        if (!s_work.Idle()) { notify::Post(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
        if (!NetOk() || !Start(kJobLogout, now)) {       // (no network: forgotten here all the same)
            ForgetTokens(); s_auth = s_client[0] ? kOut : kNoClient; s_nf = s_nrq = 0; s_meName[0] = s_meCode[0] = 0; s_nrows = 0; s_sel = 0; s_haveList = false;
            s_meUser[0] = s_meGame[0] = 0; s_mePid = s_meTid = 0; s_meStatus = s_hidden = s_canHide = -1; s_meBigOk = s_meBigTried = false;
        }
    }
    bool DrawMeAvatar(Ui &ui, float cx, float cy, float r, float a) {
        s_wantMe = true;
        if (s_auth != kIn || !s_meBigOk || !s_texOk || s_texSq != 2 || (s_mirror && s_sq != 2)) return false;
        const float u0 = (float)kQrX / kW, v0 = 0.0f, u1 = (float)(kQrX + 128) / kW, v1 = 1.0f;
        ui.Shape(cx - r, cy - r, cx + r, cy + r, u0, v0, u1, v1, Rgba{ 0.93f, 0.94f, 0.98f, a }, 31, (u0 + u1) * 0.5f, (v0 + v1) * 0.5f, (u1 - u0) * 0.5f);
        return true;
    }
    bool DrawGameIcon(Ui &ui, u64 tid, float cx, float cy, float r, float a) { return Icon(ui, tid, cx, cy, r, a); }
    bool Launching() { return s_launchAt > 0 || s_backAt > 0; }
    void Done() { if (s_launchAt > 0 && s_backAt < 0) { s_backAt = 0; s_settle.on = false; } }

    void Update(const Wheel &w, float now, float dt, bool menuOnScreen) {
        s_frame++;
        if (s_auth == kUnknown) LoadConfig();
        const bool page = Visible(w);
        // the job's result
        if (s_work.Done()) {
            Job &j = s_job;
            if (j.retry > 0) s_retryUntil = now + (float)(j.retry > 900 ? 900 : j.retry);
            if (j.kind == kJobImage && j.imgKind == 3) {   // v96: my picture (128 px), kept for the square
                if (j.img == 1 && j.imgId == s_mePid) { memcpy(s_meBig, j.big, sizeof s_meBig); s_meBigOk = true; if (s_sq == 2) s_sq = 0; if (j.fresh && j.http == 200) s_meFresh = true; }
                if (j.img >= 0) s_meBigTried = true;     // (a failure: again in a minute, never in a loop)
                s_meImgAt = now + (j.img < 0 ? 60.0f : 0.4f); s_nextImgAt = now + 0.4f;
            } else if (j.kind == kJobImage) {
                if (j.img == 1) Put(j.imgKind, j.imgId, j.imgVer, j.bc);
                else if (j.img == 0) { s_miss[s_missAt] = { (uint8_t)j.imgKind, j.imgId, j.imgVer }; s_missAt = (s_missAt + 1) % 32; if (s_nmiss < 32) s_nmiss++; }
                s_nextImgAt = now + (j.img < 0 ? 5.0f : (j.http > 0 ? 0.4f : 0.02f));   // (one request at a time, a pause between; none: later)
            } else {
                if (j.me) { snprintf(s_meName, sizeof s_meName, "%s", j.meName); snprintf(s_meCode, sizeof s_meCode, "%s", j.meCode); }
                if (j.me && j.meUser[0]) snprintf(s_meUser, sizeof s_meUser, "%s", j.meUser);   // v96: the profile page
                if (j.me && j.mePid) { if (s_mePid && s_mePid != j.mePid) { s_meBigOk = false; s_meBigTried = false; } s_mePid = j.mePid; }
                if (j.canHide >= 0) s_canHide = j.canHide;
                if (j.hidden >= 0) s_hidden = j.hidden;
                if (j.kind == kJobMe) { s_meAt = now; if (j.me) { s_meStatus = j.meStatus; s_meTid = j.meTid; snprintf(s_meGame, sizeof s_meGame, "%s", j.meGame); } }
                if (j.list) { TakeList(now); s_listAt = now; BuildRows(); }
                else if (j.kind == kJobMe && !j.ok) s_meAt = now - 30.0f;   // (no answer: the profile page looks again in 30 s)
                else if (j.kind != kJobLogout && j.kind != kJobMe && j.kind != kJobHide) s_listAt = now - kEvery + 60.0f;   // (no answer: again in a minute)
                if (j.msg[0]) { notify::Post(now, j.msg, j.ok ? notify::kDone : notify::kError); }
                if (j.kind == kJobLogin) {
                    const bool wasLogin = s_login != 0;
                    s_login = 0;
                    if (s_browserMine && la::IsActive()) la::Terminate();   // the browser closes by itself
                    s_browserMine = false;
                    if (j.ok) {
                        s_auth = kIn; s_firstList = true;
                        if (j.list) s_firstList = false;
                        char m[96]; snprintf(m, sizeof m, lang::T("Connecté à Nextendo : %s"), s_meName[0] ? s_meName : "Nextendo");
                        notify::Post(now, m, notify::kDone);
                        audio::Bell(audio::HarmonyNote(0, 76, 88), 0.05f, true); audio::Bell(audio::HarmonyNote(2, 76, 88), 0.04f, true, 0.12f);
                        haptic::Tap(0.8f);
                    } else if (wasLogin && !__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
                        if (j.denied && !strcmp(j.why, "invalid_scope")) LoginWhy(now, lang::T("Ton appli Nextendo refuse la permission « profil » : coche-la sur le portail (docs/NEXTENDO-APP.md)"));   // v96
                        else if (j.denied) LoginWhy(now, lang::T("Connexion annulée sur Nextendo"));
                        else if (__atomic_load_n(&s_phase, __ATOMIC_ACQUIRE) < 0) LoginWhy(now, lang::T("Le port 8080 de la console est déjà pris"));
                        else if (j.http == 400) LoginWhy(now, lang::T("Nextendo a refusé la connexion : vérifie le client_id et l'adresse de retour"));
                        else if (!strcmp(j.why, "delai")) LoginWhy(now, lang::T("Le temps est écoulé (5 minutes)"));
                        else { char m[128]; snprintf(m, sizeof m, lang::T("La connexion n'a pas abouti (%s)"), j.why); LoginWhy(now, m); }
                        audio::Thock(0, 0.8f, 0.06f);
                    }
                    Log("amis : connexion %s (%s, HTTP %d)", j.ok ? "ok" : "ratee", j.why, j.http);   // lang:log (never drawn)
                }
                if (j.kind == kJobLogout || j.out) {
                    ForgetTokens();
                    s_auth = s_client[0] ? kOut : kNoClient;
                    s_nf = s_nrq = 0; s_meName[0] = s_meCode[0] = 0; s_haveList = false; s_firstList = true; s_sel = 0; s_nrows = 0;
                    s_meUser[0] = s_meGame[0] = 0; s_mePid = s_meTid = 0; s_meStatus = s_hidden = s_canHide = -1; s_meAt = -1e9f;   // v96
                    s_meBigOk = s_meBigTried = false;
                    if (j.out && j.kind != kJobLogout) notify::Post(now, lang::T("Nextendo : reconnecte-toi (page Amis)"), notify::kError);
                }
                if (s_task >= 0) { tasks::End(s_task, now, j.ok || j.list, j.ok || j.list ? nullptr : j.why); s_task = -1; }
                if (j.kind != kJobLogin && j.why[0] && !j.list && !j.ok) Log("amis : %s (HTTP %d)", j.why, j.http);   // lang:log (never drawn)
            }
        }
        // the strip's copy: only while it can be seen
        if (StripWanted(page)) MirrorOn(); else MirrorOff();
        if (s_mirror) {
            if (s_dOpen && s_dLink[0]) SetQr(s_dLink);
            else if (s_login == 2) SetQr(s_phoneUrl);
            else if (s_meBigOk && s_auth == kIn) PutMe();   // v96: my picture comes back into the square
        }
        // the page arrives: a fresh look if the last one is old
        const bool front = page && w.Page() == kPage;
        if (front && s_pageK < 0.5f && s_auth == kIn && now - s_listAt > 20.0f) s_wantListAt = now;
        s_pageK += ((front ? 1.0f : 0.0f) - s_pageK) * (1 - expf(-dt * 6.0f));
        // the next job (one at a time; nothing while a game runs, nothing behind the lock screen)
        if (s_work.Idle() && menuOnScreen && !qext_game_running() && now >= s_retryUntil && s_launchAt < 0) {
            if (s_auth == kIn && (now - s_listAt > kEvery || (s_wantListAt > 0 && now - s_wantListAt >= 0)) && NetOk()) {
                s_wantListAt = -1;
                Start(kJobList, now);
            } else if (s_auth == kIn && s_wantMeAt > 0 && now >= s_wantMeAt && NetOk()) {   // v96: the profile page's look at the account
                s_wantMeAt = -1;
                Start(kJobMe, now);
            } else if (s_auth == kIn && s_mePid && !s_meBigTried && (s_wantMe || s_barNextendo) && now >= s_meImgAt && now >= s_nextImgAt && !nextendo::Busy() && !covers::Busy() && !spotify::Busy()) {
                s_job.imgKind = 3; s_job.imgId = s_mePid; s_job.imgVer = 0; s_job.imgIndex = -1;
                s_job.fresh = s_wantMe && NetOk();               // (the page open: once per start-up from Nextendo; else the card's copy)
                Start(kJobImage, now);
            } else if (s_mirror && s_nwant > 0 && now >= s_nextImgAt && (page || s_dOpen || s_profile) && !nextendo::Busy() && !covers::Busy() && !spotify::Busy()) {
                const Want &q = s_want[0];
                if (SlotOf(q.kind, q.id, q.ver) < 0) {
                    s_job.imgKind = q.kind; s_job.imgId = q.id; s_job.imgVer = q.ver; s_job.imgIndex = q.index;
                    Start(kJobImage, now);
                }
            }
        }
        s_nwant = 0;                                     // (asked again by this frame's drawing)
        s_wantMe = false;                                // v96 (the same: the profile page asks for my picture while it draws it)
        if (s_profile && s_auth == kIn && now - s_meAt > 60.0f && s_wantMeAt < 0) s_wantMeAt = now;   // (the page open: a look a minute)
        // the selection's light glides
        if (s_nrows) {
            const float target = s_rowY[s_sel < s_nrows ? s_sel : 0];
            Glide(s_selY, s_selV, target, dt, 9.0f);
            const float view = 400.0f;                   // the list's height on screen
            const float want = fminf(fmaxf(0.0f, target - view * 0.42f), fmaxf(0.0f, s_rowY[s_nrows - 1] - view + 60));
            Glide(s_scroll, s_scrollV, want, dt, 6.5f);
        }
        // « Rejoindre »: black and silent, then the game; back: black until the frames are steady, then the light
        if (s_launchAt > 0 && !s_started && s_backAt < 0 && now - s_launchAt > audio::kEntryLen) {
            s_started = true;
            if (!Games::LaunchTitle(s_launchTid, s_status, sizeof s_status)) { s_statusAt = now; s_backAt = now; audio::Arrive(0.8f); }
        }
        if (s_backAt == 0) {
            if (!s_settle.on) s_settle.Begin(now);
            if (s_settle.Step(now, dt)) { s_backAt = now; audio::Arrive(1.3f); audio::ExitSound(0.0f); }
        }
        if (s_backAt > 0 && now - s_backAt > Pace(0.8f)) { s_launchAt = -100; s_backAt = -100; s_started = false; }
        if (s_launchAt > 0 && s_started && s_backAt < 0 && now - s_launchAt > 8.0f) { s_backAt = now; audio::Arrive(0.8f); }
        // the sign-in: a code came / the exchange runs
        if (s_login && __atomic_load_n(&s_phase, __ATOMIC_ACQUIRE) == 2 && s_browserMine && la::IsActive()) { la::Terminate(); s_browserMine = false; }
        if (s_login == 1 && s_browserMine && !la::IsActive()) { s_browserMine = false; }   // (closed by hand: the page offers the phone)
    }

    // ---- the buttons ----
    void Move(int dir, float now) {
        if (s_launchAt > 0 || s_login) return;
        if (s_auth == kIn) {
            if (!s_nrows) return;
            const int n = s_sel + dir;
            if (n < 0 || n >= s_nrows) { audio::Thock(dir < 0 ? -0.4f : 0.4f, 0.9f, 0.05f); return; }
            s_sel = n; s_moveAt = now;
        } else {
            const int count = s_auth == kNoClient ? 1 : 3;
            const int n = s_loginSel + dir;
            if (n < 0 || n >= count) { audio::Thock(dir < 0 ? -0.4f : 0.4f, 0.9f, 0.05f); return; }
            s_loginSel = n; s_moveAt = now;
        }
        audio::Play(audio::Deplacement, 0, 1.0f);
        haptic::Tick(0.5f);
    }
    void PressA(float now) {
        if (s_launchAt > 0) return;
        if (s_auth != kIn) {
            if (s_login) return;
            const int c = s_auth == kNoClient ? 2 : s_loginSel;
            if (c == 0) BeginLogin(now, false);
            else if (c == 1) BeginLogin(now, true);
            else DiscordOpen(now);
            return;
        }
        if (!s_nrows) return;
        const RowT r = s_rows[s_sel];
        switch (r.kind) {
            case kRAdd: {
                if (!s_work.Idle()) { Status(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
                char out[40] = {};
                BrumeSwkbdOptions o = { "", lang::T("Code ami de ton ami"), 4, 24, 1 };
                audio::Play(audio::Tick2400);
                if (qext_swkbd_show_ex(&o, out, sizeof out) > 0 && out[0]) {
                    snprintf(s_job.code, sizeof s_job.code, "%s", out);
                    s_job.act = kAdd;
                    if (!NetOk() || !Start(kJobAction, now)) { Status(now, lang::T("Pas de connexion à Internet")); audio::Thock(0, 0.8f, 0.06f); }
                }
                return;
            }
            case kRDiscord: DiscordOpen(now); return;
            case kRReq: {
                if (!s_work.Idle() || !NetOk()) { Status(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
                s_job.act = kAccept; s_job.pid = s_rq[r.idx].pid;
                if (Start(kJobAction, now)) { audio::Bell(audio::HarmonyNote(0, 76, 88), 0.05f, true); haptic::Tap(0.7f); }
                return;
            }
            case kRFriend: {
                const Friend &f = s_f[r.idx];
                if (f.status == 2 && f.tid) Join(f, now);
                else { audio::Thock(0, 0.9f, 0.05f); Status(now, f.status ? lang::T("Ton ami ne joue à rien pour l'instant") : lang::T("Ton ami est hors ligne")); }
                return;
            }
            case kRLogout: {
                if (!s_work.Idle()) { Status(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
                audio::Play(audio::Tick1700);
                if (!NetOk() || !Start(kJobLogout, now)) {   // (no network: forgotten here all the same)
                    ForgetTokens(); s_auth = s_client[0] ? kOut : kNoClient; s_nf = s_nrq = 0; s_meName[0] = 0; s_nrows = 0; s_sel = 0; s_haveList = false;
                }
                return;
            }
        }
    }
    bool PressB(float now) {
        if (s_login) { CancelLogin(now); return true; }
        return false;
    }
    void PressX(float now) {
        if (s_auth != kIn || !s_nrows || s_rows[s_sel].kind != kRReq) return;
        if (!s_work.Idle() || !NetOk()) { Status(now, lang::T("Un instant, Brume parle déjà à Nextendo…")); return; }
        s_job.act = kDecline; s_job.pid = s_rq[s_rows[s_sel].idx].pid;
        if (Start(kJobAction, now)) { audio::Play(audio::Tick1700); haptic::Tick(0.6f); }
    }
    void PressY(float now) {
        if (s_auth != kIn) { if (s_auth == kOut) BeginLogin(now, true); return; }   // (waiting for the browser: the phone instead)
        if (now - s_listAt < 5.0f) return;
        s_wantListAt = now;
        audio::Play(audio::Tick2400, 0, 0.7f);
    }
    void Tap(float x, float y, float now) {
        for (int k = 0; k < s_nhit; k++) {
            const Hit &h = s_hit[k];
            if (x < h.x0 || x > h.x1 || y < h.y0 || y > h.y1) continue;
            if (s_auth == kIn) { if (h.i == s_sel) PressA(now); else { s_sel = h.i; s_moveAt = now; audio::Play(audio::Deplacement); } }
            else { if (h.i == s_loginSel) PressA(now); else { s_loginSel = h.i; s_moveAt = now; audio::Play(audio::Deplacement); } }
            return;
        }
    }

    // ---- Discord ----
    void DiscordOpen(float now) {
        LoadDiscord();
        s_dOpen = true; s_dAt = now; s_dCloseAt = -100;
        audio::Play(audio::Tick2400);
        audio::PlaySweep(audio::Whoosh, -0.3f, 0.3f, 0.5f, 0.35f);
        audio::Bell(audio::HarmonyNote(2, 72, 84), 0.04f, true, 0.1f);
        haptic::Thump(0.5f);
    }
    bool DiscordShown() { return s_dOpen; }
    namespace {
        float DiscordK(float now) {                      // how much the Discord QR is on screen (0..1)
            return s_dOpen ? EaseGlide((now - s_dAt) / Pace(0.55f)) : (s_dCloseAt < 0 ? 0.0f : 1 - EaseGlide((now - s_dCloseAt) / Pace(0.4f)));
        }
    }
    void DiscordClose(float now) {
        if (!s_dOpen) return;
        s_dOpen = false; s_dCloseAt = now;
        audio::Play(audio::Tick1700);
        haptic::Tick(0.5f);
    }
    void DrawDiscord(Ui &ui, float now) {
        const float in = DiscordK(now);
        if (in <= 0.003f) return;
        ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0.004f, 0.015f, 0.80f * in }, 24, 1280, 720, 0);   // the scene steps back
        ui.Veil(140, 60, 1140, 680, 1.2f * in);
        const float rise = (1 - in) * 24;
        const char *title = "Vocal Discord";
        const float tw = ui.Width(fonts::kPageTitle, title, 1.5f);
        ui.Light(640 - tw / 2 - 70, 88 + rise, 640 + tw / 2 + 70, 168 + rise, ps2::A(ps2::kTitleGlow, 0.14f * in), true);
        ui.TextCentered(fonts::kPageTitle, title, 640, 100 + rise, 1.5f, ps2::A(ps2::kTitle, in));
        if (s_dLink[0] && s_qrPx) {
            const float qa = EaseGlide((now - s_dAt - 0.12f) / Pace(0.6f)) * in;
            const float br = 1.0f + 0.03f * sinf(now * 1.3f);
            DrawQr(ui, 640, 352 + rise, 290 * (0.94f + 0.06f * qa), qa * br > 1 ? 1 : qa);
            ui.TextCentered(fonts::kGameItem, "Scanne-le avec ton téléphone : ton salon s'ouvre dans Discord.", 640, 532 + rise, 0.3f, ps2::A(ps2::kWords, 0.92f * in));
            const char *l = s_dLink + 8;             // (without https://)
            ui.TextCentered(fonts::kHint, l, 640, 568 + rise, 0.6f, ps2::A(ps2::kChoice, 0.85f * in));
        } else if (s_dLink[0]) {
            ui.TextCentered(fonts::kGameItem, "Ce lien est trop long pour un QR code.", 640, 320 + rise, 0.3f, ps2::A(ps2::kWords, in));
        } else {
            ui.TextCentered(fonts::kGameItem, "Aucun lien d'invitation pour l'instant.", 640, 300 + rise, 0.3f, ps2::A(ps2::kWords, in));
            Wrap(ui, fonts::kHintBig, "Mets ton lien (discord.gg/...) dans brume/discord.txt sur la carte SD : il apparaîtra ici en QR code.",
                 640, 346 + rise, 760, 30, ps2::A(ps2::kFaint, 0.95f * in), 2);
        }
        const float hc = 720 - 28 - 13.5f;
        float hx = 46;
        hx += ui.Glyph(fonts::kButton, 'B', hx, hc - 10, ps2::A(ps2::kPink, in), 0, 0, 0, 0) + 10;
        for (const char *q = lang::T("Fermer"); *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * in), 0, 0, 0, 0) + 0.5f;
    }

    // ---- the page ----
    void DrawUi(Ui &ui, float now, const Wheel &w) {
        s_nhit = 0;
        if (!Visible(w)) return;
        const float al = Clamp01(1 - fabsf(w.PageAnim() - kPage) * 1.15f);
        if (al <= 0.003f) return;
        const float dx = (kPage - w.PageAnim()) * 520;
        const float hal = al * (1 - DiscordK(now));       // (the hints step aside under the Discord QR)
        const bool busyNet = !s_work.Idle() && s_job.kind != kJobImage;
        static busy::Fade s_busyFade;
        const float bk = s_busyFade.Step(busyNet && s_auth == kIn, now, 0.4f);
        if (s_auth == kIn) {
            // ---- the title, who I am ----
            const float tx = 112 + dx;
            const float ttw = ui.Width(fonts::kPageTitle, "Amis", 1.5f);
            ui.Light(tx - 60, 92, tx + ttw + 60, 168, ps2::A(ps2::kTitleGlow, 0.13f * al), true);
            Text(ui, fonts::kPageTitle, "Amis", tx, 100, ps2::A(ps2::kTitle, al), 1.5f);
            int online = 0; for (int i = 0; i < s_nf; i++) online += s_f[i].status > 0;
            char who[128];
            if (s_meName[0]) snprintf(who, sizeof who, lang::T("%s · %d en ligne"), s_meName, online);
            else snprintf(who, sizeof who, lang::T("%d en ligne"), online);
            Text(ui, fonts::kHintBig, who, tx + 2, 154, ps2::A(ps2::kFaint, 0.95f * al), 0.4f);
            if (bk > 0.003f) busy::Orbs(ui, tx + ttw + 34, 122, 10, bk * al, now);
            // ---- the list (floating words in the fog) ----
            const float top = 214, bottom = 640, x0 = 104 + dx, x1 = 600 + dx;
            for (int i = 0; i < s_nrows; i++) {
                const float cy = top + s_rowY[i] - s_scroll + 22;
                if (cy < top - 70 || cy > bottom + 40) continue;
                const float edge = Clamp01((cy - (top - 6)) / 34.0f) * Clamp01((bottom + 20 - cy) / 60.0f);
                const float a = al * edge;
                if (a <= 0.003f) continue;
                const RowT &r = s_rows[i];
                const bool head = (r.kind == kRReq && (i == 0 || s_rows[i - 1].kind != kRReq)) || (r.kind == kRFriend && (i == 0 || s_rows[i - 1].kind != kRFriend));
                if (head) {
                    char h[64];
                    if (r.kind == kRReq) snprintf(h, sizeof h, lang::T("Demandes reçues (%d)"), s_nrq);
                    else snprintf(h, sizeof h, lang::T("Tes amis (%d)"), s_nf);
                    Text(ui, fonts::kSetDesc, h, x0 + 6, cy - 50, ps2::A(ps2::kTitle, 0.85f * a), 1.2f);
                }
                const float k = Clamp01(1 - fabsf((top + s_selY - s_scroll + 22) - cy) / 58.0f);
                const bool sel = i == s_sel;
                const float nx = x0 + 70;
                const char *name = "", *sub = nullptr;
                char nm[48] = {}, subBuf[120] = {};
                Rgba subC = ps2::A(ps2::kFaint, 0.9f * a);
                switch (r.kind) {
                    case kRAdd: name = "Ajouter un ami"; sub = "Par son code ami"; break;
                    case kRDiscord: name = "Vocal Discord"; sub = "Le QR code de ton salon"; break;
                    case kRLogout: name = "Se déconnecter"; sub = "De Nextendo, sur cette console"; break;
                    case kRReq: Cut(s_rq[r.idx].name, nm, sizeof nm, 20); name = nm; sub = "Veut être ton ami"; subC = ps2::A(ps2::kTitle, 0.8f * a); break;
                    case kRFriend: {
                        const Friend &f = s_f[r.idx];
                        Cut(f.name, nm, sizeof nm, 20); name = nm;
                        if (f.status == 2) { char g[96]; Cut(GameName(f, g, sizeof g), subBuf, sizeof subBuf, 30); sub = subBuf; subC = ps2::A(ps2::kGreen, 0.85f * a); }
                        else if (f.status == 1) { sub = "En ligne"; subC = ps2::A(ps2::kChoice, 0.9f * a); }
                        else sub = "Hors ligne";
                        break;
                    }
                }
                const float nw = ui.Width(fonts::kSetOpt, name, 0.3f);
                if (k > 0.004f) ui.Chosen(x0 - 30, x1 + 40, cy, nx, nw, k * a);
                // its picture: an avatar, or a soft light for the actions (« + », a wave)
                const float ax = x0 + 26;
                if (r.kind == kRFriend || r.kind == kRReq) {
                    const Friend &f = r.kind == kRFriend ? s_f[r.idx] : s_rq[r.idx];
                    Avatar(ui, f, ax, cy, 21, a * (0.7f + 0.3f * k));
                    if (r.kind == kRFriend && f.status) {   // a little light: online (blue), playing (green) - never an icon
                        const Rgba c = f.status == 2 ? ps2::kGreen : ps2::kChoice;
                        ui.Light(ax + 6, cy + 6, ax + 30, cy + 30, ps2::A(c, 0.9f * a), true);
                        ui.Light(ax + 13, cy + 13, ax + 23, cy + 23, ps2::A(ps2::kChosen, 0.7f * a), true);
                    }
                } else {
                    ui.Light(ax - 24, cy - 24, ax + 24, cy + 24, ps2::A(r.kind == kRLogout ? ps2::kPink : ps2::kHalo, (0.25f + 0.25f * k) * a), true);
                    const char *g = r.kind == kRAdd ? "+" : (r.kind == kRDiscord ? "\xe2\x97\x8e" : "\xe2\x80\xb9");
                    if (r.kind == kRDiscord) {       // (three rings of light: the voice)
                        for (int q = 0; q < 3; q++) { const float rr = 6 + q * 6 + 2 * sinf(now * 2 - q); ui.Light(ax - rr, cy - rr, ax + rr, cy + rr, ps2::A(ps2::kChoice, (0.32f - q * 0.08f) * a), true); }
                    } else ui.TextCentered(fonts::kSetSel, g, ax, cy - 17, 0, ps2::Line(k, a));
                }
                Text(ui, fonts::kSetOpt, name, nx, cy - 22, ps2::Line(sel ? fmaxf(k, 0.6f) : k, a), 0.3f);
                if (sub) Text(ui, fonts::kHint, sub, nx, cy + 4, subC, 0.3f);
                if (s_nhit < 16) s_hit[s_nhit++] = { i, x0 - 20, cy - 29, x1, cy + 29 };
            }
            // ---- the chosen one, on the right ----
            const float cx = 930 + dx;
            const float in = EaseGlide((now - s_moveAt) / Pace(0.45f));
            const float da = al * (0.25f + 0.75f * in), dy = (1 - in) * 10;
            const char *hints[8]; int nh = 0;
            if (s_nrows) {
                const RowT &r = s_rows[s_sel];
                if (r.kind == kRFriend || r.kind == kRReq) {
                    const Friend &f = r.kind == kRFriend ? s_f[r.idx] : s_rq[r.idx];
                    ui.Light(cx - 190, 150 + dy, cx + 190, 380 + dy, ps2::A(f.status == 2 ? ps2::kGreen : ps2::kHalo, (f.status ? 0.20f : 0.12f) * da), true);
                    Avatar(ui, f, cx, 262 + dy, 66, da);
                    char nm[64]; Cut(f.name, nm, sizeof nm, 22);
                    ui.TextCentered(fonts::kGameTitle, nm, cx, 348 + dy, 0.5f, ps2::A(ps2::kChosen, da));
                    if (r.kind == kRReq) {
                        ui.TextCentered(fonts::kGameItem, "Veut être ton ami", cx, 394 + dy, 0.3f, ps2::A(ps2::kTitle, 0.9f * da));
                        hints[nh++] = "A"; hints[nh++] = "Accepter"; hints[nh++] = "X"; hints[nh++] = "Refuser";
                    } else if (f.status == 2) {
                        ui.TextCentered(fonts::kGameItem, "Joue en ce moment", cx, 394 + dy, 0.3f, ps2::A(ps2::kGreen, 0.9f * da));
                        char g[96], gc[96]; Cut(GameName(f, g, sizeof g), gc, sizeof gc, 30);
                        const float gw = ui.Width(fonts::kSetOpt, gc, 0.3f), gx = cx - (gw + 58) / 2;
                        const bool icon = f.tid && Icon(ui, f.tid, gx + 22, 458 + dy, 22, da);
                        if (!icon) ui.Light(gx, 436 + dy, gx + 44, 480 + dy, ps2::A(ps2::kGreen, 0.35f * da), true);
                        Text(ui, fonts::kSetOpt, gc, gx + 58, 446 + dy, ps2::A(ps2::kWords, da), 0.3f);
                        if (f.detail[0]) { char d[96]; Cut(f.detail, d, sizeof d, 40); ui.TextCentered(fonts::kHintBig, d, cx, 492 + dy, 0.3f, ps2::A(ps2::kFaint, 0.95f * da)); }
                        if (Installed(f.tid)) { hints[nh++] = "A"; hints[nh++] = "Rejoindre"; }
                        else ui.TextCentered(fonts::kHintBig, "Tu n'as pas ce jeu", cx, 530 + dy, 0.3f, ps2::A(ps2::kPink, 0.85f * da));
                    } else ui.TextCentered(fonts::kGameItem, f.status ? "En ligne" : "Hors ligne", cx, 394 + dy, 0.3f, ps2::A(f.status ? ps2::kChoice : ps2::kFaint, 0.9f * da));
                    if (f.code[0]) {
                        char c[64]; snprintf(c, sizeof c, lang::T("Code ami : %s"), f.code);
                        ui.TextCentered(fonts::kHint, c, cx, 580 + dy, 0.6f, ps2::A(ps2::kFaint, 0.8f * da));
                    }
                } else if (r.kind == kRAdd) {
                    ui.TextCentered(fonts::kGameTitle, "Ajouter un ami", cx, 214 + dy, 0.5f, ps2::A(ps2::kChosen, da));
                    Wrap(ui, fonts::kGameItem, "Demande son code ami à ton ami, puis tape-le avec le clavier.", cx, 268 + dy, 470, 30, ps2::A(ps2::kWords, 0.9f * da), 2);
                    if (s_meCode[0]) {
                        ui.TextCentered(fonts::kSetDesc, "Ton code ami, à lui donner", cx, 380 + dy, 1.0f, ps2::A(ps2::kTitle, 0.85f * da));
                        const float cw = ui.Width(fonts::kGameTitle, s_meCode, 1.5f);
                        ui.Light(cx - cw / 2 - 50, 400 + dy, cx + cw / 2 + 50, 460 + dy, ps2::A(ps2::kHalo, 0.25f * da), true);
                        ui.TextCentered(fonts::kGameTitle, s_meCode, cx, 412 + dy, 1.5f, ps2::A(ps2::kChoice, da));
                    }
                    hints[nh++] = "A"; hints[nh++] = "Ajouter";
                } else if (r.kind == kRDiscord) {
                    ui.TextCentered(fonts::kGameTitle, "Vocal Discord", cx, 214 + dy, 0.5f, ps2::A(ps2::kChosen, da));
                    Wrap(ui, fonts::kGameItem, "La Switch ne peut pas faire le vocal Discord : un QR code ouvre ton salon sur ton téléphone, et vous parlez de là.",
                         cx, 268 + dy, 470, 30, ps2::A(ps2::kWords, 0.9f * da), 3);
                    hints[nh++] = "A"; hints[nh++] = "Ouvrir";
                } else {
                    ui.TextCentered(fonts::kGameTitle, "Se déconnecter", cx, 214 + dy, 0.5f, ps2::A(ps2::kChosen, da));
                    Wrap(ui, fonts::kGameItem, "Brume oublie ta connexion Nextendo sur cette console. Tes amis restent sur ton compte.",
                         cx, 268 + dy, 470, 30, ps2::A(ps2::kWords, 0.9f * da), 3);
                    hints[nh++] = "A"; hints[nh++] = "Se déconnecter";
                }
            }
            if (!s_haveList) ui.TextCentered(fonts::kHintBig, busyNet ? "Brume demande tes amis à Nextendo…" : "Pas encore de nouvelles de Nextendo.", 352 + dx, 600, 0.3f, ps2::A(ps2::kFaint, 0.9f * al));
            hints[nh++] = "Y"; hints[nh++] = "Actualiser";
            Hints(ui, hal, hints, nh);
        } else {
            // ---- not signed in: the title, why, the choices ----
            const float cx = 640 + dx;
            if (s_login == 2) {                          // the phone: the steps on the left, the QR on the right
                const float lx = 116 + dx;
                const float ttw = ui.Width(fonts::kPageTitle, "Avec ton téléphone", 1.5f);
                ui.Light(lx - 60, 108, lx + ttw + 60, 184, ps2::A(ps2::kTitleGlow, 0.13f * al), true);
                Text(ui, fonts::kPageTitle, "Avec ton téléphone", lx, 116, ps2::A(ps2::kTitle, al), 1.5f);
                static const char *const kSteps[3] = { "Scanne le code (ton téléphone sur le même Wi-Fi que la console).",
                                                       "Touche « Se connecter à Nextendo », connecte-toi et accepte.",
                                                       "Ton téléphone finit sur une page 127.0.0.1 qui ne s'ouvre pas : copie son adresse et colle-la dans la page Brume." };
                float y = 206;
                for (int i = 0; i < 3; i++) {
                    char n[4]; snprintf(n, sizeof n, "%d", i + 1);
                    ui.Light(lx - 4, y - 8, lx + 40, y + 36, ps2::A(ps2::kHalo, 0.28f * al), true);
                    ui.TextCentered(fonts::kSetSel, n, lx + 18, y - 2, 0, ps2::A(ps2::kChosen, al));
                    y = Wrap(ui, fonts::kGameItem, kSteps[i], lx + 54, y + 2, 520, 30, ps2::A(ps2::kWords, 0.92f * al), 3, true) + 22;
                }
                const int ph = __atomic_load_n(&s_phase, __ATOMIC_ACQUIRE);
                if (ph == 2) { busy::Orbs(ui, lx + 22, y + 20, 12, al, now); Text(ui, fonts::kHintBig, "Connexion…", lx + 54, y + 9, ps2::A(ps2::kChoice, al), 0.3f); }
                else Text(ui, fonts::kHint, "Le code marche 5 minutes.", lx, y + 6, ps2::A(ps2::kFaint, 0.85f * al), 0.4f);
                DrawQr(ui, 935 + dx, 372, 300, al * (1.0f - 0.0f));
                ui.TextCentered(fonts::kHint, s_phoneUrl, 935 + dx, 540, 0.6f, ps2::A(ps2::kChoice, 0.8f * al));
                const char *h[] = { "B", "Annuler" };
                Hints(ui, hal, h, 2);
            } else if (s_login == 1) {                   // the console's browser (open, or closed by hand)
                const int ph = __atomic_load_n(&s_phase, __ATOMIC_ACQUIRE);
                busy::Orbs(ui, cx, 270, 26, al, now);
                ui.TextCentered(fonts::kGameItem, ph >= 2 ? "Connexion…" : "En attente de Nextendo…", cx, 330, 0.3f, ps2::A(ps2::kWords, al));
                if (ph < 2) Wrap(ui, fonts::kHintBig, "Le navigateur s'est fermé sans rien faire ? Essaie avec ton téléphone (Y).", cx, 372, 700, 28, ps2::A(ps2::kFaint, 0.95f * al), 2);
                const char *h[] = { "Y", "Avec mon téléphone", "B", "Annuler" };
                Hints(ui, hal, h, 4);
            } else {
                const float ttw = ui.Width(fonts::kPageTitle, "Amis", 1.5f);
                ui.Light(cx - ttw / 2 - 60, 150, cx + ttw / 2 + 60, 226, ps2::A(ps2::kTitleGlow, 0.13f * al), true);
                ui.TextCentered(fonts::kPageTitle, "Amis", cx, 156, 1.5f, ps2::A(ps2::kTitle, al));
                static const char *const kChoice[3] = { "Se connecter sur la console", "Avec mon téléphone", "Vocal Discord" };
                static const char *const kDesc[3] = { "Le navigateur de la Switch s'ouvre sur Nextendo, puis se ferme tout seul.",
                                                      "Un QR code : tu te connectes sur ton téléphone (même Wi-Fi).",
                                                      "Le QR code de ton salon vocal, pour ton téléphone." };
                int first = 0, count = 3;
                float y;
                if (s_auth == kNoClient) {
                    ui.TextCentered(fonts::kGameItem, "Il manque l'identifiant de ton appli Nextendo.", cx, 236, 0.3f, ps2::A(ps2::kWords, 0.92f * al));
                    Wrap(ui, fonts::kHintBig, "Crée l'appli « Brume » sur nextendo.network/developers, puis mets client_id=nxc_... dans brume/nextendo.ini sur la carte SD (voir docs/NEXTENDO-APP.md).",
                         cx, 274, 760, 28, ps2::A(ps2::kFaint, 0.95f * al), 3);
                    first = 2; count = 1; y = 400;
                } else {
                    Wrap(ui, fonts::kGameItem, "Connecte ton compte Nextendo : tes amis, ce qu'ils jouent, et les rejoindre.", cx, 236, 760, 30, ps2::A(ps2::kWords, 0.92f * al), 2);
                    y = 322;
                }
                for (int c = 0; c < count; c++) {
                    const int idx = first + c;
                    const bool sel = (s_auth == kNoClient) || c == s_loginSel;
                    const float k = sel ? EaseGlide((now - s_moveAt) / Pace(0.35f)) * 0.5f + 0.5f : 0.0f;
                    const float wdt = ui.Width(fonts::kSetSel, kChoice[idx], 0.3f);
                    if (sel) ui.Chosen(cx - 330, cx + 330, y + 15, cx - wdt / 2, wdt, k * al);
                    ui.TextCentered(fonts::kSetSel, kChoice[idx], cx, y, 0.3f, ps2::Line(sel ? 1.0f : 0.0f, al));
                    if (s_nhit < 16) s_hit[s_nhit++] = { c, cx - 330, y - 14, cx + 330, y + 44 };
                    y += 58;
                }
                const int d = s_auth == kNoClient ? 2 : s_loginSel;
                ui.TextCentered(fonts::kHint, kDesc[d], cx, y + 10, 0.4f, ps2::A(ps2::kFaint, 0.9f * al));
                if (s_loginWhy[0] && now - s_loginWhyAt < 12.0f) {
                    const float wa = Clamp01((12.0f - (now - s_loginWhyAt)) / 0.6f) * al;
                    Wrap(ui, fonts::kHintBig, s_loginWhy, cx, y + 52, 760, 28, ps2::A(ps2::kPink, 0.95f * wa), 2);
                }
                const char *h[] = { "A", "Choisir" };
                Hints(ui, hal, h, 2);
            }
        }
        // a short message (an action that cannot happen now)
        if (s_status[0] && now - s_statusAt < 4.0f) {
            const float sa = Clamp01((4.0f - (now - s_statusAt)) / 0.5f) * al;
            const float sw = ui.Width(fonts::kHintBig, s_status, 0.4f);
            ui.Veil(640 - sw / 2 - 90, 618, 640 + sw / 2 + 90, 668, 0.7f * sa);
            ui.TextCentered(fonts::kHintBig, s_status, 640, 632, 0.4f, ps2::A(ps2::kWords, sa));
        }
        // « Rejoindre »: the screen goes black (and comes back if it could not start)
        if (s_launchAt > 0 || s_backAt > 0) {
            const float blk = s_backAt > 0 ? 1 - Clamp01((now - s_backAt) / Pace(0.7f)) : (s_backAt == 0 ? 1.0f : Clamp01((now - s_launchAt - Pace(0.3f)) / Pace(0.5f)));
            if (blk > 0.003f) ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0, 0, blk }, 24, 1280, 720, 0);
        }
    }

    void Sleep() {
        __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
        s_login = 0; s_dOpen = false; s_dCloseAt = -100;
        if (s_launchAt > 0 && !s_started) { s_launchAt = -100; s_backAt = -100; }
    }
    void Shutdown() {
        __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
        s_work.Wait();
        free(s_mirror); s_mirror = nullptr;
    }

#ifdef BRUME_UISIM
    void SimDemo(const char *what, float val, float now) {
        if (s_auth == kUnknown) LoadConfig();
        if (!strcmp(what, "amis")) {
            const int v = (int)val;
            snprintf(s_client, sizeof s_client, "nxc_apercu");
            s_login = 0;
            if (v == 0) { s_auth = kOut; s_nf = s_nrq = 0; s_nrows = 0; s_haveList = false; }
            else if (v == 1) { s_auth = kIn; s_simList = 1; s_firstList = true; Start(kJobList, now); }
            else if (v == 2) { s_auth = kOut; Prepare(); s_login = 2; }
            else if (v == 3) { s_auth = kNoClient; s_client[0] = 0; }
            else if (v == 4) { s_simList = 2; s_listAt = -1e9f; }   // (the next look: changes -> notifications)
            else if (v == 5) { s_auth = kOut; Prepare(); s_login = 1; }
            else if (v == 6) { s_auth = kOut; LoginWhy(now, lang::T("Nextendo a refusé la connexion : vérifie le client_id et l'adresse de retour")); }
        } else if (!strcmp(what, "profil")) {           // v96: the profile page - 0 out, 1 in (online), 2 playing, 3 invisible, 4 no client_id
            const int v = (int)val;
            snprintf(s_client, sizeof s_client, "nxc_apercu");
            s_login = 0;
            if (v == 0) { s_auth = kOut; s_meName[0] = s_meCode[0] = s_meUser[0] = 0; s_mePid = 0; s_meStatus = s_hidden = s_canHide = -1; s_meBigOk = false; }
            else if (v == 4) { s_auth = kNoClient; s_client[0] = 0; }
            else {
                s_auth = kIn; s_simMe = v == 2 ? 2 : 1; s_simHidden = v == 3 ? 1 : 0;
                if (s_simList == 1 && !s_haveList) Start(kJobList, now);
                else { s_meAt = -1e9f; s_wantMeAt = now; }
            }
        } else if (!strcmp(what, "discord")) {
            DiscordOpen(now);
            if (val > 0.5f) snprintf(s_dLink, sizeof s_dLink, "https://discord.gg/brume42");
        }
    }
#endif

    } // namespace social

} // namespace brume
