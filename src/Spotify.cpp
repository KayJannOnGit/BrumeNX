// Brume - v98: Spotify. See Spotify.hpp and docs/SPOTIFY.md.
#include "Spotify.hpp"
#include "Audio.hpp"
#include "Busy.hpp"
#include "Covers.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Nextendo.hpp"
#include "Shelf.hpp"
#include "Social.hpp"
#include "Worker.hpp"
// the service's ABI (one copy, the service's own: commands, the 216-byte status, the browse pages)
#include "../musique/switify/phase2/common/include/switify/ipc_protocol.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>

extern "C" {
    int qext_game_running(void);
    struct BrumeSwkbdOptions { const char *initialText, *headerText; unsigned minLength, maxLength; int blurBackground; };
    int qext_swkbd_show_ex(const BrumeSwkbdOptions *options, char *out, unsigned cap);
}

namespace brume {
    bool DecodeJpegTo(const void *data, int bytes, uint8_t *out, int ow, int oh);   // Jpeg.cpp
}

namespace brume::spotify {

    void (*s_log)(const char *) = nullptr;

    namespace {
        constexpr u64 kTid = BRUME_MUSIQUE_PROGRAM_ID;
        const char *const kNsp = "sdmc:/atmosphere/contents/420000000042524D/exefs.nsp";
        const char *const kIni = "sdmc:/brume/spotify/brume.ini";
        const char *const kReadyFile = "sdmc:/brume/spotify/background-ready.log";
        const char *const kArtFile = "sdmc:/brume/spotify/current-artwork-url";
        constexpr int kItemsMax = 32;                     // the service sends 32 browse items at most
        constexpr int kCover = 128;                       // the cover: 128 x 128 px, BC1 (8 KB)
        constexpr uint32_t kCoverBc = (kCover / 4) * (kCover / 4) * 8;

        enum Phase { kOff, kMissing, kStarting, kStartFail, kPairing, kConnecting, kReady, kRefused };

        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
        void Log(const char *fmt, ...) {
            if (!s_log) return;
            char m[160];
            va_list ap; va_start(ap, fmt); vsnprintf(m, sizeof m, fmt, ap); va_end(ap);
            s_log(m);
        }
        // a text from the service: at most cap - 1 bytes of src (which may lack its NUL), cut to valid UTF-8, no control chars
        void Clean(char *dst, int cap, const char *src, int srcCap) {
            int n = 0, i = 0;
            while (i < srcCap && src[i] && n < cap - 1) {
                const unsigned char c = (unsigned char)src[i];
                int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
                if (!len) { i++; continue; }                       // (a stray byte: dropped)
                if (i + len > srcCap || n + len > cap - 1) break;  // (cut in the middle of a character: stop before it)
                bool ok = true;
                for (int k = 1; k < len; k++) ok = ok && ((unsigned char)src[i + k] & 0xC0) == 0x80;
                if (!ok) { i++; continue; }
                if (len == 1 && c < 0x20) { dst[n++] = ' '; i++; continue; }
                memcpy(dst + n, src + i, (size_t)len); n += len; i += len;
            }
            dst[n] = 0;
        }

        // ================================================================ the service (real, or the PC preview's)
        bool s_want = false, s_installed = false, s_conn = false, s_failed = false, s_booted = false;
        float s_now = 0;                                  // the last frame's time (SetOn from the settings, which have none)
        float s_bootAt = 1e9f, s_launchAt = -100, s_nextTry = 0, s_pollAt = 0, s_stAt = 0;
        int   s_launches = 0;
        u64   s_pid = 0;
        SwitifyIpcStatus s_st;                            // the last status (checked), and when it came
        bool  s_stOk = false;
        char  s_title[72] = "", s_artist[56] = "", s_album[48] = "";

#ifndef BRUME_UISIM
        Service s_svc;
        bool s_dmnt = false;
        bool Installed() { struct stat st; return stat(kNsp, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0; }
        bool Running(u64 *pid) {
            if (!s_dmnt) s_dmnt = R_SUCCEEDED(pmdmntInitialize());
            if (!s_dmnt) return false;
            u64 p = 0;
            const Result rc = pmdmntGetProcessId(&p, kTid);
            *pid = R_SUCCEEDED(rc) ? p : 0;
            return R_SUCCEEDED(rc) && p != 0;
        }
        Result Launch() {
            remove(kReadyFile);                               // (its « pass » line is written again once it answers)
            Result rc = pmshellInitialize();
            if (R_FAILED(rc)) return rc;
            NcmProgramLocation loc = {};
            loc.program_id = kTid; loc.storageID = NcmStorageId_None;
            u64 pid = 0;
            rc = pmshellLaunchProgram(0, &loc, &pid);
            pmshellExit();
            return rc;
        }
        void Terminate() {
            if (R_SUCCEEDED(pmshellInitialize())) { pmshellTerminateProgram(kTid); pmshellExit(); }
        }
        // the service has registered « brm:spot » and answered its own ping: the line « pass-... <pid> » of THIS process.
        // (sm would make us WAIT for a name not registered yet: we never ask for it before that line is there)
        bool Answers(u64 pid) {
            FILE *f = fopen(kReadyFile, "r");
            if (!f) return false;
            char line[96] = "";
            const bool got = fgets(line, sizeof line, f) != nullptr;
            fclose(f);
            unsigned long long p = 0;
            return got && !strncmp(line, "pass", 4) && sscanf(line, "%*s %llx", &p) == 1 && p == pid;
        }
        bool Lost(Result rc) { return R_FAILED(rc) && R_MODULE(rc) != 421 && R_MODULE(rc) != Module_Libnx; }   // 421: refused, not gone
        // (v101: called by the link's thread only - see namespace link)
        void CloseSvc() { serviceClose(&s_svc); }
        bool ConnectSvc(u32 *badVer) {
            *badVer = 0;
            if (R_FAILED(smGetService(&s_svc, SWITIFY_IPC_SERVICE_NAME))) return false;
            u32 ver = 0;
            if (R_FAILED(serviceDispatchOut(&s_svc, SwitifyIpcCommand_GetApiVersion, ver)) || (ver >> 16) != SWITIFY_IPC_API_VERSION_MAJOR) {
                *badVer = ver ? ver : 1;
                serviceClose(&s_svc);
                return false;
            }
            return true;
        }
        Result IpcStatus(SwitifyIpcStatus &st) { memset(&st, 0, sizeof st); return serviceDispatchOut(&s_svc, SwitifyIpcCommand_GetStatus, st); }
        Result IpcCmd(u32 cmd) { return serviceDispatch(&s_svc, cmd); }
        Result IpcCmdU32(u32 cmd, u32 v) { return serviceDispatchIn(&s_svc, cmd, v); }
        Result IpcHeader(SwitifyIpcBrowseHeader &h) { memset(&h, 0, sizeof h); return serviceDispatchOut(&s_svc, SwitifyIpcCommand_GetBrowseHeader, h); }
        Result IpcItem(u32 i, SwitifyIpcBrowseItem &it) { memset(&it, 0, sizeof it); return serviceDispatchInOut(&s_svc, SwitifyIpcCommand_GetBrowseItem, i, it); }
        Result IpcBrowse(u32 cmd, u64 gen, u32 index, const char *query) {
            SwitifyIpcBrowseRequest r = {};
            r.generation = gen; r.index = index;
            if (query) snprintf(r.query, sizeof r.query, "%s", query);
            return serviceDispatchIn(&s_svc, cmd, r);
        }
        bool ArtUrl(char *out, int cap) {                // the cover's address the service wrote (only i.scdn.co, hex only)
            out[0] = 0;
            FILE *f = fopen(kArtFile, "r");
            if (!f) return false;
            char u[128] = "";
            const bool got = fgets(u, sizeof u, f) != nullptr;
            fclose(f);
            if (!got) return false;
            const char *pre = "https://i.scdn.co/image/";
            const int pl = (int)strlen(pre);
            if (strncmp(u, pre, (size_t)pl)) return false;
            int n = pl;
            while (u[n] && ((u[n] >= '0' && u[n] <= '9') || (u[n] >= 'a' && u[n] <= 'f'))) n++;
            if (n - pl < 16 || n - pl > 64 || n >= cap) return false;
            memcpy(out, u, (size_t)n); out[n] = 0;
            return true;
        }
#else
        // ---- the PC preview: a made-up service with the same answers (« demo spotify <n> » in a uisim script)
        int  s_simMode = 0;
        bool s_simRun = false;
        float s_simNow = 0, s_simPosAt = 0;
        u32  s_simPos = 0, s_simVol = 600, s_simTrack = 0;
        bool s_simPlay = false, s_simShuffle = false;
        struct SimTrack { const char *t, *a, *al; u32 ms; };
        const SimTrack kSimTracks[] = { { "Midnight City", "M83", "Hurry Up, We're Dreaming", 243000 },
                                        { "Teardrop", "Massive Attack", "Mezzanine", 330000 },
                                        { "Avril 14th", "Aphex Twin", "Drukqs", 125000 },
                                        { "Nightcall", "Kavinsky", "OutRun", 258000 },
                                        { "Svefn-g-englar", "Sigur R\xc3\xb3s", "\xc3\x81g\xc3\xa6tis byrjun", 604000 } };
        constexpr int kSimN = (int)(sizeof kSimTracks / sizeof kSimTracks[0]);
        SwitifyIpcBrowseHeader s_simHdr;
        SwitifyIpcBrowseItem s_simItems[kItemsMax];
        u64 s_simGen = 0;
        bool Installed() { return s_simMode != 5; }
        bool Running(u64 *pid) { *pid = s_simRun ? 0x77 : 0; return s_simRun; }
        Result Launch() { s_simRun = true; return 0; }
        void Terminate() { s_simRun = false; }
        bool Answers(u64) { return s_simMode != 6; }
        bool Lost(Result rc) { return R_FAILED(rc) && R_MODULE(rc) != 421; }
        void CloseSvc() {}
        bool ConnectSvc(u32 *badVer) { *badVer = 0; return s_simRun; }
        u32 SimPos() { return s_simPos + (s_simPlay ? (u32)((s_simNow - s_simPosAt) * 1000.0f) : 0); }
        Result IpcStatus(SwitifyIpcStatus &st) {
            memset(&st, 0, sizeof st);
            st.struct_size = SWITIFY_IPC_STATUS_SIZE; st.abi_version = SWITIFY_IPC_STATUS_ABI_VERSION;
            st.service_state = s_simMode == 1 ? SwitifyServiceState_Pairing : s_simMode == 4 ? SwitifyServiceState_Error : SwitifyServiceState_Ready;
            st.volume_permille = s_simVol;
            st.flags = SwitifyStatusFlag_HasCredentials | (s_simShuffle ? SwitifyStatusFlag_Shuffle : 0);
            if (s_simMode == 4) snprintf(st.album, sizeof st.album, "Spotify refused the audio key");
            if (s_simMode == 2 || s_simMode == 3) {
                const SimTrack &t = kSimTracks[s_simTrack % kSimN];
                st.flags |= SwitifyStatusFlag_Connected | SwitifyStatusFlag_HasTrack | SwitifyStatusFlag_CanSeek;
                st.playback_state = s_simPlay ? SwitifyPlaybackState_Playing : SwitifyPlaybackState_Paused;
                u32 p = SimPos(); if (p > t.ms) p = t.ms;
                st.position_ms = p; st.duration_ms = t.ms; st.queue_index = s_simTrack; st.queue_count = kSimN;
                snprintf(st.title, sizeof st.title, "%s", t.t); snprintf(st.artist, sizeof st.artist, "%s", t.a); snprintf(st.album, sizeof st.album, "%s", t.al);
            }
            return 0;
        }
        void SimJump(int d) { s_simTrack = (u32)((int)s_simTrack + d + kSimN) % kSimN; s_simPos = 0; s_simPosAt = s_simNow; }
        Result IpcCmd(u32 cmd) {
            if (s_simMode != 2 && s_simMode != 3) return MAKERESULT(421, SwitifyControlResult_BackendUnavailable);
            if (cmd == SwitifyIpcCommand_PlayPause) { s_simPos = SimPos(); s_simPosAt = s_simNow; s_simPlay = !s_simPlay; s_simMode = s_simPlay ? 2 : 3; }
            if (cmd == SwitifyIpcCommand_Next) SimJump(1);
            if (cmd == SwitifyIpcCommand_Previous) SimJump(-1);
            return 0;
        }
        Result IpcCmdU32(u32 cmd, u32 v) {
            if (cmd == SwitifyIpcCommand_SetVolume) s_simVol = v > 1000 ? 1000 : v;
            if (cmd == SwitifyIpcCommand_SetShuffle) s_simShuffle = v != 0;
            return 0;
        }
        Result IpcHeader(SwitifyIpcBrowseHeader &h) { h = s_simHdr; return 0; }
        Result IpcItem(u32 i, SwitifyIpcBrowseItem &it) {
            if (i >= s_simHdr.item_count || i >= (u32)kItemsMax) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
            it = s_simItems[i]; return 0;
        }
        void SimItem(int i, u32 kind, const char *name, const char *sub, const char *detail) {
            SwitifyIpcBrowseItem &it = s_simItems[i];
            memset(&it, 0, sizeof it);
            it.struct_size = SWITIFY_IPC_BROWSE_ITEM_SIZE; it.abi_version = SWITIFY_IPC_BROWSE_ABI_VERSION;
            it.generation = s_simGen; it.kind = kind; it.index = (u32)i;
            snprintf(it.name, sizeof it.name, "%s", name); snprintf(it.subtitle, sizeof it.subtitle, "%s", sub);
            snprintf(it.detail, sizeof it.detail, "%s", detail);
        }
        void SimPage(const char *title, u32 depth) {
            s_simGen++;
            memset(&s_simHdr, 0, sizeof s_simHdr);
            s_simHdr.struct_size = SWITIFY_IPC_BROWSE_HEADER_SIZE; s_simHdr.abi_version = SWITIFY_IPC_BROWSE_ABI_VERSION;
            s_simHdr.generation = s_simGen; s_simHdr.state = SwitifyBrowseState_Ready; s_simHdr.depth = depth;
            snprintf(s_simHdr.title, sizeof s_simHdr.title, "%s", title);
        }
        Result IpcBrowse(u32 cmd, u64, u32 index, const char *) {
            int n = 0;
            if (cmd == SwitifyIpcCommand_BrowseLibrary || cmd == SwitifyIpcCommand_BrowseBack) {
                SimPage("Your Library", 0);
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Liked Songs", "Playlist", "312 items");
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Night Fog", "Playlist \xc2\xb7 Yass", "48 items");
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Chill PS2", "Playlist \xc2\xb7 Yass", "27 items");
                SimItem(n++, SwitifyBrowseItemKind_Album, "Random Access Memories", "Daft Punk", "13 items");
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Lo-fi beats", "Spotify", "100 items");
                SimItem(n++, SwitifyBrowseItemKind_Artist, "Kavinsky", "Artist", "");
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Sunday Jazz", "Playlist", "64 items");
                SimItem(n++, SwitifyBrowseItemKind_Album, "Mezzanine", "Massive Attack", "11 items");
                SimItem(n++, SwitifyBrowseItemKind_Playlist, "Synthwave", "Spotify", "80 items");
            } else if (cmd == SwitifyIpcCommand_BrowseOpen && s_simHdr.depth == 0 && index < s_simHdr.item_count) {
                SimPage(s_simItems[index].name, 1);
                for (int k = 0; k < kSimN; k++) SimItem(n++, SwitifyBrowseItemKind_Track, kSimTracks[k].t, kSimTracks[k].a, kSimTracks[k].al);
            } else if (cmd == SwitifyIpcCommand_BrowseOpen && index < s_simHdr.item_count && s_simItems[index].kind == SwitifyBrowseItemKind_Track) {
                s_simTrack = index % kSimN; s_simPos = 0; s_simPosAt = s_simNow; s_simPlay = true; s_simMode = 2;
                return 0;
            } else return 0;
            s_simHdr.item_count = (u32)n;
            return 0;
        }
        bool ArtUrl(char *out, int cap) {
            if (s_simMode != 2 && s_simMode != 3) { out[0] = 0; return false; }
            snprintf(out, (size_t)cap, "sim:%u", s_simTrack % kSimN); return true;
        }
#endif

        // ================================================================ the cover (a worker: download, decode, BC1)
        Worker s_work;
        struct CoverJob { char url[128]; bool ok; char why[64]; };
        CoverJob s_job;
        uint8_t s_bc[kCoverBc];                           // the finished cover, waiting for Stream
        char  s_coverUrl[128] = "", s_wantUrl[128] = "";
        bool  s_coverReady = false, s_coverShown = false, s_texOk = false;
        Target s_tex;
        float s_coverAt = -100, s_coverTry = 0;

#ifndef BRUME_UISIM
        struct Sink { uint8_t *buf; int cap, n, headN, status; bool body, over; char head[1024]; };
        int SinkFn(void *u, const char *p, int n) {     // the headers, then the body (no chunked answers from i.scdn.co: refused)
            Sink &s = *(Sink *)u;
            if (s.body) {
                if (s.n + n > s.cap) { s.over = true; return -1; }
                memcpy(s.buf + s.n, p, (size_t)n); s.n += n; return 0;
            }
            const int k = n < (int)sizeof s.head - 1 - s.headN ? n : (int)sizeof s.head - 1 - s.headN;
            memcpy(s.head + s.headN, p, (size_t)k); s.headN += k; s.head[s.headN] = 0;
            char *e = strstr(s.head, "\r\n\r\n");
            if (!e) { if (s.headN >= (int)sizeof s.head - 1) { s.over = true; return -1; } return 0; }
            if (!strncmp(s.head, "HTTP/1.", 7) && s.headN > 12) s.status = atoi(s.head + 9);
            if (s.status != 200) return -1;
            for (char *q = s.head; q < e; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q - 'A' + 'a');
            *e = 0;
            if (strstr(s.head, "transfer-encoding: chunked")) { s.over = true; return -1; }
            s.body = true;
            const int used = (int)(e + 4 - s.head) - (s.headN - k);
            if (used < n) {
                if (n - used > s.cap) { s.over = true; return -1; }
                memcpy(s.buf, p + used, (size_t)(n - used)); s.n = n - used;
            }
            return 0;
        }
        void CoverRun(void *arg) {
            CoverJob &j = *(CoverJob *)arg;
            j.ok = false; j.why[0] = 0;
            // the 300 px version of an album cover (« ab67616d » + size + hash), else the address as it is
            char path[128];
            snprintf(path, sizeof path, "%s", j.url + strlen("https://i.scdn.co"));
            if (!strncmp(path, "/image/ab67616d", 15) && strlen(path) == 7 + 40) memcpy(path + 15, "00001e02", 8);
            Sink *s = (Sink *)calloc(1, sizeof(Sink));
            uint8_t *rgba = (uint8_t *)malloc((size_t)kCover * kCover * 4);
            if (s) { s->cap = 160 * 1024; s->buf = (uint8_t *)malloc((size_t)s->cap); }
            if (!s || !s->buf || !rgba) { snprintf(j.why, sizeof j.why, "memoire"); }
            else {
                SocketInitConfig cfg = *socketGetDefaultInitConfig();   // small buffers (the menu's memory is short)
                cfg.tcp_tx_buf_size = 0x2000; cfg.tcp_rx_buf_size = 0x8000; cfg.tcp_tx_buf_max_size = 0; cfg.tcp_rx_buf_max_size = 0;
                cfg.udp_tx_buf_size = 0x800; cfg.udp_rx_buf_size = 0x800; cfg.sb_efficiency = 1; cfg.num_bsd_sessions = 1;
                if (R_SUCCEEDED(socketInitialize(&cfg))) {
                    if (R_SUCCEEDED(sslInitialize(1))) {
                        char why[64] = "";
                        const int r = nextendo::Https("i.scdn.co", path, true, "image/jpeg", SinkFn, s, why, (int)sizeof why);
                        if (r >= 0 && s->status == 200 && s->n > 0 && !s->over)
                            j.ok = DecodeJpegTo(s->buf, s->n, rgba, kCover, kCover) && Shelf::EncodeBC1(rgba, kCover, kCover, s_bc) != nullptr;
                        if (!j.ok) snprintf(j.why, sizeof j.why, "%s (HTTP %d, %d o)", why[0] ? why : "image", s->status, s->n);
                        sslExit();
                    } else snprintf(j.why, sizeof j.why, "ssl indisponible");
                    socketExit();
                } else snprintf(j.why, sizeof j.why, "reseau indisponible");
            }
            if (s) free(s->buf);
            free(s); free(rgba);
        }
#else
        void CoverRun(void *arg) {                        // the preview: a made-up cover (a soft gradient, one per track)
            CoverJob &j = *(CoverJob *)arg;
            uint8_t *rgba = (uint8_t *)malloc((size_t)kCover * kCover * 4);
            j.ok = false;
            if (!rgba) return;
            const int t = atoi(j.url + 4);
            static const float kA[5][3] = { { 0.95f, 0.35f, 0.55f }, { 0.45f, 0.75f, 0.95f }, { 0.55f, 0.75f, 0.85f }, { 0.85f, 0.25f, 0.65f }, { 0.75f, 0.85f, 0.95f } };
            static const float kB[5][3] = { { 0.15f, 0.10f, 0.40f }, { 0.02f, 0.02f, 0.06f }, { 0.10f, 0.15f, 0.20f }, { 0.08f, 0.02f, 0.20f }, { 0.25f, 0.35f, 0.50f } };
            for (int y = 0; y < kCover; y++)
                for (int x = 0; x < kCover; x++) {
                    const float u = x / (float)kCover, v = y / (float)kCover, d = sqrtf((u - 0.35f) * (u - 0.35f) + (v - 0.3f) * (v - 0.3f));
                    const float k = Clamp01(1.0f - d * 1.4f) * (0.85f + 0.15f * sinf(u * 9.0f + v * 4.0f + t));
                    uint8_t *p = rgba + (y * kCover + x) * 4;
                    for (int c = 0; c < 3; c++) p[c] = (uint8_t)(255.0f * (kB[t % 5][c] + (kA[t % 5][c] - kB[t % 5][c]) * k));
                    p[3] = 255;
                }
            j.ok = Shelf::EncodeBC1(rgba, kCover, kCover, s_bc) != nullptr;
            free(rgba);
        }
#endif

        // ================================================================ v101: the link - every call to the service on its own thread
        // On the console (v100) the menu froze with the Spotify panel open: a call to the service (made on the frame) never came
        // back. Now the frame never calls the service: it leaves its requests here and reads what the thread brought back. A
        // call that hangs is seen (Stuck: 3 s) and shown as « ne répond pas »; « Réessayer » stops the service, which frees it.
        namespace link {
            enum { kPlain, kU32, kBrowse };
            struct Cmd { int kind; u32 cmd, v, index; u64 gen; char query[SWITIFY_IPC_BROWSE_QUERY_SIZE]; };
            constexpr int kQ = 8;
            // asked by the frame (under the lock)
            bool s_wantConn = false, s_wantClose = false, s_askSt = false, s_askBr = false;
            Cmd  s_q[kQ];
            int  s_qn = 0;
            // brought back by the thread (under the lock)
            bool s_up = false;
            int  s_upSeq = 0, s_badSeq = 0, s_stSeq = 0, s_pageSeq = 0;
            u32  s_badVer = 0;
            char s_lost[24] = "";
            SwitifyIpcStatus s_st;
            SwitifyIpcBrowseHeader s_hdr;
            SwitifyIpcBrowseItem s_it[kItemsMax];
            int  s_itN = -1;                              // -1: this page brought only its header (no new items)
            u64  s_callAt = 0;                            // (atomic) when the call running began (0: none)
            // the thread's own
            bool t_conn = false;
            u64  t_hdrGen = ~0ULL, t_itGen = ~0ULL, t_nextConn = 0;
            u32  t_hdrState = ~0u;
            SwitifyIpcBrowseItem t_items[kItemsMax];
#ifndef BRUME_UISIM
            Mutex  s_m;
            UEvent s_ev;
            Thread s_thr;
            bool   s_thread = false;
            int    s_stop = 0;
            void Lock() { mutexLock(&s_m); }
            void Unlock() { mutexUnlock(&s_m); }
            void Wake() { if (s_thread) ueventSignal(&s_ev); }
#else
            void Lock() {}
            void Unlock() {}
            void Wake() {}
#endif
            template <typename F> Result Call(F f) {
                __atomic_store_n(&s_callAt, armGetSystemTick() | 1, __ATOMIC_RELEASE);
                const Result rc = f();
                __atomic_store_n(&s_callAt, 0, __ATOMIC_RELEASE);
                return rc;
            }
            bool Stuck() {
                const u64 at = __atomic_load_n(&s_callAt, __ATOMIC_ACQUIRE);
                const u64 t = armGetSystemTick();
                return at && t > at && armTicksToNs(t - at) > 3000000000ULL;
            }
            void Drop(const char *why) {                  // (thread) the link is gone
                if (t_conn) CloseSvc();
                t_conn = false; t_hdrGen = ~0ULL; t_itGen = ~0ULL; t_hdrState = ~0u;
                Lock(); s_up = false; if (why) snprintf(s_lost, sizeof s_lost, "%s", why); Unlock();
            }
            void Step() {                                 // (thread) one round: the requests, then what was asked for
                Lock();
                const bool want = s_wantConn, close = s_wantClose;
                bool st = s_askSt, br = s_askBr;
                s_wantClose = false; s_askSt = false; s_askBr = false;
                Cmd q[kQ];
                const int qn = s_qn;
                if (qn > 0) memcpy(q, s_q, sizeof(Cmd) * (size_t)qn);
                s_qn = 0;
                Unlock();
                if (close && t_conn) Drop(nullptr);
                if (!t_conn) {
                    if (!want || close || armGetSystemTick() < t_nextConn) return;
                    u32 bad = 0; bool ok = false;
                    Call([&] { ok = ConnectSvc(&bad); return (Result)0; });
                    if (!ok) {
                        t_nextConn = armGetSystemTick() + armNsToTicks(500000000ULL);
                        if (bad) { Lock(); s_badVer = bad; s_badSeq++; Unlock(); }
                        return;
                    }
                    t_conn = true; st = true;
                    Lock(); s_up = true; s_upSeq++; Unlock();
                }
                for (int i = 0; i < qn && t_conn; i++) {
                    const Cmd &c = q[i];
                    const Result rc = Call([&] {
                        return c.kind == kPlain ? IpcCmd(c.cmd) : c.kind == kU32 ? IpcCmdU32(c.cmd, c.v)
                                                : IpcBrowse(c.cmd, c.gen, c.index, c.query[0] ? c.query : nullptr); });
                    if (Lost(rc)) { Drop(c.kind == kBrowse ? "page" : "commande"); return; }
                    st = true;
                    if (c.kind == kBrowse) { br = true; t_itGen = ~0ULL; }
                }
                if (st && t_conn) {
                    SwitifyIpcStatus s;
                    const Result rc = Call([&] { return IpcStatus(s); });
                    if (R_FAILED(rc)) { if (Lost(rc)) { Drop("statut"); return; } }
                    else if (s.struct_size != SWITIFY_IPC_STATUS_SIZE || s.abi_version != SWITIFY_IPC_STATUS_ABI_VERSION) { Drop("statut inconnu"); return; }
                    else { Lock(); s_st = s; s_stSeq++; Unlock(); }
                }
                if (br && t_conn) {                       // the page: its header, then its items when a new one is ready
                    SwitifyIpcBrowseHeader h;
                    const Result rc = Call([&] { return IpcHeader(h); });
                    if (R_FAILED(rc)) { if (Lost(rc)) Drop("page"); return; }
                    if (h.struct_size != SWITIFY_IPC_BROWSE_HEADER_SIZE || h.abi_version != SWITIFY_IPC_BROWSE_ABI_VERSION) return;
                    if (h.item_count > (u32)kItemsMax) h.item_count = kItemsMax;
                    const bool fresh = h.generation != t_hdrGen || h.state != t_hdrState;
                    t_hdrGen = h.generation; t_hdrState = h.state;
                    int n = -1;
                    if (h.state == SwitifyBrowseState_Ready && (fresh || t_itGen != h.generation)) {
                        n = 0;
                        for (u32 i = 0; i < h.item_count; i++) {
                            SwitifyIpcBrowseItem it;
                            const Result ri = Call([&] { return IpcItem(i, it); });
                            if (R_FAILED(ri)) { if (Lost(ri)) { Drop("page"); return; } break; }
                            if (it.struct_size != SWITIFY_IPC_BROWSE_ITEM_SIZE || it.abi_version != SWITIFY_IPC_BROWSE_ABI_VERSION || it.generation != h.generation) break;
                            Clean(it.name, sizeof it.name, it.name, (int)sizeof it.name);
                            Clean(it.subtitle, sizeof it.subtitle, it.subtitle, (int)sizeof it.subtitle);
                            Clean(it.detail, sizeof it.detail, it.detail, (int)sizeof it.detail);
                            it.index = i;
                            t_items[n++] = it;
                        }
                        t_itGen = h.generation;
                    }
                    Lock();                               // (the header and its items together: never a new title over old rows)
                    s_hdr = h;
                    s_itN = n;
                    if (n > 0) memcpy(s_it, t_items, sizeof(t_items[0]) * (size_t)n);
                    s_pageSeq++;
                    Unlock();
                }
            }
#ifndef BRUME_UISIM
            void Run(void *) {
                while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
                    waitSingle(waiterForUEvent(&s_ev), 50000000ULL);   // a request, or every 50 ms
                    if (__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) break;
                    Step();
                }
                if (t_conn) { CloseSvc(); t_conn = false; }
            }
            void Start() {
                if (s_thread) return;
                mutexInit(&s_m);
                ueventCreate(&s_ev, true);
                __atomic_store_n(&s_stop, 0, __ATOMIC_RELEASE);
                s_thread = R_SUCCEEDED(threadCreate(&s_thr, Run, nullptr, nullptr, 0x8000, 0x2E, -2));
                if (s_thread && R_FAILED(threadStart(&s_thr))) { threadClose(&s_thr); s_thread = false; }
                if (!s_thread) Log("spotify : le fil du service n'a pas pu demarrer (appels sur l'image)");
            }
            void Stop() {                                 // the menu closes: a call that hangs is freed by stopping the service
                if (!s_thread) return;
                __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
                ueventSignal(&s_ev);
                if (R_FAILED(waitSingle(waiterForThread(&s_thr), 1500000000ULL))) {
                    Terminate();
                    if (R_FAILED(waitSingle(waiterForThread(&s_thr), 1500000000ULL))) { s_thread = false; return; }   // (left: never wait forever)
                }
                threadClose(&s_thr);
                s_thread = false;
            }
            bool Threaded() { return s_thread; }
#else
            void Start() {}
            void Stop() {}
            bool Threaded() { return false; }
#endif
            // (frame) the requests
            void AskConnect(bool on) { Lock(); s_wantConn = on; if (!on) s_wantClose = true; Unlock(); Wake(); }
            void AskStatus() { Lock(); s_askSt = true; Unlock(); Wake(); }
            void AskBrowse() { Lock(); s_askBr = true; Unlock(); Wake(); }
            bool Push(const Cmd &c) { Lock(); const bool ok = s_qn < kQ; if (ok) s_q[s_qn++] = c; Unlock(); Wake(); return ok; }
        } // namespace link

        // ================================================================ the menu's own mDNS announcer (v99.1, the fallback)
        // On the console (v99), the background service could not start its mDNS responder: the phone never saw BrumeNX. The
        // pairing itself (the zeroconf HTTP server on TCP 7864, the account's decryption) stays in the service; only the
        // ANNOUNCE moves here when the service says it failed (sdmc:/brume/spotify/mdns-etat.txt: « echec ... », or no file
        // after 6 s of « Première connexion »): the menu is a full applet with the network, it answers the phones' mDNS
        // questions for _spotify-connect._tcp.local (PTR, SRV -> port 7864, TXT, A = the console's address), and says it
        // again every few seconds. Only while that screen is on and the menu on screen; it holds the network (Busy: the
        // covers, Nextendo and the friends wait), and says goodbye (TTL 0) when it stops.
        namespace mdns {
            constexpr uint16_t kZeroconfPort = 7864;          // the service's login server (spotify_engine.cpp ZEROCONF_PORT)
            Thread s_thr;
            bool   s_thread = false;
            int    s_stop = 0, s_state = 0;                 // (atomics) state: 0 off, 1 starting, 2 announcing, 3 failed
            char   s_msg[8][112];                            // the thread's log lines, read (and logged) by the main thread
            int    s_msgW = 0, s_msgR = 0;
            float  s_pairSince = -1, s_checkAt = 0, s_retryAt = 0;
            bool   s_serviceOk = false, s_serviceFailed = false;

            void Say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
            void Say(const char *fmt, ...) {
                const int w = __atomic_load_n(&s_msgW, __ATOMIC_ACQUIRE);
                if (w - __atomic_load_n(&s_msgR, __ATOMIC_ACQUIRE) >= 8) return;
                va_list ap; va_start(ap, fmt); vsnprintf(s_msg[w & 7], sizeof s_msg[0], fmt, ap); va_end(ap);
                __atomic_store_n(&s_msgW, w + 1, __ATOMIC_RELEASE);
            }
            void Drain() {
                while (__atomic_load_n(&s_msgR, __ATOMIC_ACQUIRE) < __atomic_load_n(&s_msgW, __ATOMIC_ACQUIRE)) {
                    const int r = s_msgR;
                    Log("spotify : mdns (menu) : %s", s_msg[r & 7]);
                    __atomic_store_n(&s_msgR, r + 1, __ATOMIC_RELEASE);
                }
            }
#ifndef BRUME_UISIM
            // ---- the DNS message, by hand (RFC 1035 / 6762 / 6763), names written whole (no compression)
            int PutName(uint8_t *p, int at, int cap, const char *dotted) {
                for (const char *s = dotted; *s;) {
                    const char *e = strchr(s, '.');
                    const int n = e ? (int)(e - s) : (int)strlen(s);
                    if (n < 1 || n > 63 || at + 1 + n >= cap) return -1;
                    p[at++] = (uint8_t)n; memcpy(p + at, s, (size_t)n); at += n;
                    s += n; if (*s == '.') s++;
                }
                if (at >= cap) return -1;
                p[at++] = 0;
                return at;
            }
            int Put16(uint8_t *p, int at, int cap, unsigned v) { if (at < 0 || at + 2 > cap) return -1; p[at] = (uint8_t)(v >> 8); p[at + 1] = (uint8_t)v; return at + 2; }
            int Put32(uint8_t *p, int at, int cap, uint32_t v) { at = Put16(p, at, cap, v >> 16); return Put16(p, at, cap, v & 0xFFFF); }
            // one record: name, type, class (+ cache-flush), ttl, then its data written by fill (returns the end, -1 = full)
            template <typename F> int Rec(uint8_t *p, int at, int cap, const char *name, unsigned type, bool flush, uint32_t ttl, F fill) {
                if (at < 0 || (at = PutName(p, at, cap, name)) < 0 || (at = Put16(p, at, cap, type)) < 0 || (at = Put16(p, at, cap, flush ? 0x8001 : 1)) < 0 ||
                    (at = Put32(p, at, cap, ttl)) < 0 || at + 2 > cap) return -1;
                const int len = at; at += 2;
                if ((at = fill(at)) < 0) return -1;
                Put16(p, len, cap, (unsigned)(at - len - 2));
                return at;
            }
            const char *const kType = "_spotify-connect._tcp.local", *const kInst = "BrumeNX._spotify-connect._tcp.local", *const kHost = "brumenx.local";
            // the answer: PTR, SRV, TXT, A (legacy unicast: the question copied and the asker's id); ttl 0 = goodbye
            int Answer(uint8_t *p, int cap, uint16_t id, uint32_t ip, const uint8_t *q, int qlen, bool goodbye) {
                const uint32_t t1 = goodbye ? 0 : 4500, t2 = goodbye ? 0 : 120;
                int at = 0;
                at = Put16(p, at, cap, id); at = Put16(p, at, cap, 0x8400); at = Put16(p, at, cap, q ? 1 : 0);
                at = Put16(p, at, cap, 4); at = Put16(p, at, cap, 0); at = Put16(p, at, cap, 0);
                if (at < 0) return -1;
                if (q) { if (at + qlen > cap) return -1; memcpy(p + at, q, (size_t)qlen); at += qlen; }
                at = Rec(p, at, cap, kType, 12, false, t1, [&](int a) { return PutName(p, a, cap, kInst); });
                at = Rec(p, at, cap, kInst, 33, true, t2, [&](int a) {
                    a = Put16(p, a, cap, 0); a = Put16(p, a, cap, 0); a = Put16(p, a, cap, kZeroconfPort);
                    return a < 0 ? -1 : PutName(p, a, cap, kHost); });
                at = Rec(p, at, cap, kInst, 16, true, t1, [&](int a) {
                    static const char *const kTxt[3] = { "VERSION=1.0", "CPath=/spotify_info", "Stack=SP" };
                    for (const char *s : kTxt) { const int n = (int)strlen(s); if (a + 1 + n > cap) return -1; p[a++] = (uint8_t)n; memcpy(p + a, s, (size_t)n); a += n; }
                    return a; });
                at = Rec(p, at, cap, kHost, 1, true, t2, [&](int a) { if (a + 4 > cap) return -1; memcpy(p + a, &ip, 4); return a + 4; });
                return at;
            }
            // a name in a received message (pointers followed, 16 jumps at most, everything bounded), lower case, dotted
            bool ReadName(const uint8_t *p, int n, int &at, char *out, int cap) {
                int o = 0, jumps = 0, pos = at; bool jumped = false;
                while (true) {
                    if (pos < 0 || pos >= n) return false;
                    const uint8_t l = p[pos];
                    if (l == 0) { pos++; break; }
                    if ((l & 0xC0) == 0xC0) {
                        if (pos + 1 >= n || ++jumps > 16) return false;
                        const int to = ((l & 0x3F) << 8) | p[pos + 1];
                        if (!jumped) at = pos + 2;
                        jumped = true; pos = to; continue;
                    }
                    if ((l & 0xC0) || pos + 1 + l > n || o + l + 2 > cap) return false;
                    if (o) out[o++] = '.';
                    for (int k = 0; k < l; k++) { const char c = (char)p[pos + 1 + k]; out[o++] = c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }
                    pos += 1 + l;
                }
                out[o] = 0;
                if (!jumped) at = pos;
                return true;
            }
            // is it a question about us? (its matching question is copied into q for a legacy unicast answer, when whole)
            bool AboutUs(const uint8_t *p, int n, uint8_t *q, int &qlen) {
                qlen = 0;
                if (n < 12 || (p[2] & 0x80)) return false;            // too short, or an answer
                const int qd = (p[4] << 8) | p[5];
                int at = 12;
                for (int i = 0; i < qd && i < 16; i++) {
                    const int start = at;
                    char name[256];
                    if (!ReadName(p, n, at, name, sizeof name) || at + 4 > n) return false;
                    at += 4;
                    if (!strcmp(name, "_spotify-connect._tcp.local") || !strcmp(name, "brumenx._spotify-connect._tcp.local") ||
                        !strcmp(name, "brumenx.local") || !strcmp(name, "_services._dns-sd._udp.local")) {
                        bool whole = at - start <= 300;
                        for (int k = start; k < at - 4 && whole;) { const uint8_t l = p[k]; if (!l) break; if (l & 0xC0) whole = false; k += 1 + l; }
                        if (whole) { qlen = at - start; memcpy(q, p + start, (size_t)qlen); }
                        return true;
                    }
                }
                return false;
            }
            void Run(void *) {
                SocketInitConfig cfg = *socketGetDefaultInitConfig();   // small buffers, UDP on
                cfg.tcp_tx_buf_size = 0x800; cfg.tcp_rx_buf_size = 0x800; cfg.tcp_tx_buf_max_size = 0; cfg.tcp_rx_buf_max_size = 0;
                cfg.udp_tx_buf_size = 0x2400; cfg.udp_rx_buf_size = 0x2400; cfg.sb_efficiency = 1; cfg.num_bsd_sessions = 2;
                const Result rs = socketInitialize(&cfg);
                if (R_FAILED(rs)) { Say("socketInitialize 0x%X", rs); __atomic_store_n(&s_state, 3, __ATOMIC_RELEASE); return; }
                uint32_t ip = 0;
                if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) { u32 a = 0; if (R_SUCCEEDED(nifmGetCurrentIpAddress(&a))) ip = a; nifmExit(); }
                const int fd = ip ? socket(AF_INET, SOCK_DGRAM, 0) : -1;
                bool ok = fd >= 0;
                if (!ip) Say("no address (nifm)");
                else if (fd < 0) Say("UDP socket refused (errno %d)", errno);
                if (ok) {
                    int on = 1; unsigned char ttl = 255;
                    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0) Say("SO_REUSEADDR refused (errno %d)", errno);
                    struct sockaddr_in a; memset(&a, 0, sizeof a);
                    a.sin_family = AF_INET; a.sin_port = htons(5353); a.sin_addr.s_addr = htonl(INADDR_ANY);
                    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0) { Say("bind UDP 5353 refused (errno %d)", errno); ok = false; }
                    struct ip_mreq m; memset(&m, 0, sizeof m);
                    m.imr_multiaddr.s_addr = inet_addr("224.0.0.251"); m.imr_interface.s_addr = ip;
                    if (ok && setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) < 0) {
                        Say("IP_ADD_MEMBERSHIP on the address refused (errno %d), trying any interface", errno);
                        m.imr_interface.s_addr = htonl(INADDR_ANY);
                        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) < 0) { Say("IP_ADD_MEMBERSHIP refused (errno %d)", errno); ok = false; }
                    }
                    if (ok && setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ip, sizeof ip) < 0) Say("IP_MULTICAST_IF refused (errno %d), going on", errno);
                    if (ok && setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl) < 0) Say("IP_MULTICAST_TTL refused (errno %d), going on", errno);
                }
                if (!ok) { if (fd >= 0) close(fd); socketExit(); __atomic_store_n(&s_state, 3, __ATOMIC_RELEASE); return; }
                Say("BrumeNX announced by the menu (%u.%u.%u.%u, login on TCP %u)", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24, kZeroconfPort);
                __atomic_store_n(&s_state, 2, __ATOMIC_RELEASE);
                struct sockaddr_in group; memset(&group, 0, sizeof group);
                group.sin_family = AF_INET; group.sin_port = htons(5353); group.sin_addr.s_addr = inet_addr("224.0.0.251");
                static uint8_t in[1500], out[1500], q[300];
                u64 nextAnnounce = armGetSystemTick();
                int announced = 0, answers = 0;
                while (!__atomic_load_n(&s_stop, __ATOMIC_ACQUIRE)) {
                    const u64 now = armGetSystemTick();
                    if (now >= nextAnnounce) {                // unsolicited: at once, after 1 s, 2 s, then every 10 s
                        const int len = Answer(out, sizeof out, 0, ip, nullptr, 0, false);
                        if (len > 0) sendto(fd, out, (size_t)len, 0, (struct sockaddr *)&group, sizeof group);
                        announced++;
                        nextAnnounce = now + armNsToTicks(announced < 3 ? 1000000000ULL : 10000000000ULL);
                    }
                    struct pollfd pf = { fd, POLLIN, 0 };
                    if (poll(&pf, 1, 250) <= 0 || !(pf.revents & POLLIN)) continue;
                    struct sockaddr_in from; socklen_t fl = sizeof from;
                    const ssize_t n = recvfrom(fd, in, sizeof in, 0, (struct sockaddr *)&from, &fl);
                    int qlen = 0;
                    if (n <= 0 || !AboutUs(in, (int)n, q, qlen)) continue;
                    const bool legacy = ntohs(from.sin_port) != 5353;   // a one-shot resolver: answer it directly, with its id
                    const int len = Answer(out, sizeof out, legacy ? (uint16_t)((in[0] << 8) | in[1]) : 0, ip, legacy && qlen ? q : nullptr, qlen, false);
                    if (len > 0) {
                        if (legacy) sendto(fd, out, (size_t)len, 0, (struct sockaddr *)&from, sizeof from);
                        sendto(fd, out, (size_t)len, 0, (struct sockaddr *)&group, sizeof group);
                    }
                    if (++answers == 1) Say("first question from a phone, answered");
                }
                const int len = Answer(out, sizeof out, 0, ip, nullptr, 0, true);   // goodbye: the phones forget BrumeNX at once
                if (len > 0) sendto(fd, out, (size_t)len, 0, (struct sockaddr *)&group, sizeof group);
                close(fd);
                socketExit();
                Say("announce stopped (%d answers)", answers);
                __atomic_store_n(&s_state, 0, __ATOMIC_RELEASE);
            }
            void Start() {
                if (s_thread) return;
                __atomic_store_n(&s_stop, 0, __ATOMIC_RELEASE);
                __atomic_store_n(&s_state, 1, __ATOMIC_RELEASE);
                s_thread = R_SUCCEEDED(threadCreate(&s_thr, Run, nullptr, nullptr, 0x8000, 0x2C, -2));
                if (s_thread && R_FAILED(threadStart(&s_thr))) { threadClose(&s_thr); s_thread = false; }
                if (!s_thread) { __atomic_store_n(&s_state, 3, __ATOMIC_RELEASE); Log("spotify : mdns (menu) : le fil n'a pas pu demarrer"); }
            }
            // v101: the frame never waits for the thread - asked to stop, it is collected later (Reap, each image), once ended
            bool s_stopping = false;
            void Reap() {
                if (!s_stopping) return;
                if (R_FAILED(waitSingle(waiterForThread(&s_thr), 0))) return;   // (still in its last poll: next image)
                threadClose(&s_thr); s_thread = false; s_stopping = false;
                Drain();
            }
            void Stop() {
                if (!s_thread || s_stopping) return;
                __atomic_store_n(&s_stop, 1, __ATOMIC_RELEASE);
                s_stopping = true;
                Reap();
            }
            void StopWait() {                             // (the menu closes: 1.5 s at most, never forever)
                Stop();
                if (s_stopping && R_SUCCEEDED(waitSingle(waiterForThread(&s_thr), 1500000000ULL))) Reap();
            }
#else
            void Start() { __atomic_store_n(&s_state, 2, __ATOMIC_RELEASE); s_thread = true; }   // (the preview: no network)
            void Stop() { __atomic_store_n(&s_state, 0, __ATOMIC_RELEASE); s_thread = false; }
            void StopWait() { Stop(); }
            void Reap() {}
#endif
            bool Running() { return s_thread; }
            int  State() { return __atomic_load_n(&s_state, __ATOMIC_ACQUIRE); }
            // what the service wrote about its own announcer (v99.1 service; an older one writes nothing)
            void ReadServiceState() {
                s_serviceOk = s_serviceFailed = false;
                FILE *f = fopen("sdmc:/brume/spotify/mdns-etat.txt", "r");
                if (!f) return;
                char line[64] = "";
                if (fgets(line, sizeof line, f)) { s_serviceOk = !strncmp(line, "ok", 2); s_serviceFailed = !strncmp(line, "echec", 5); }
                fclose(f);
            }
        } // namespace mdns

        // ================================================================ the browse pages and the panel's state
        SwitifyIpcBrowseHeader s_hdr;
        SwitifyIpcBrowseItem s_items[kItemsMax];
        int   s_nItems = 0;
        u64   s_itemsGen = ~0ULL;
        u64   s_selGen = ~0ULL;                           // the page the choice belongs to (a new page: back to its first row)
        bool  s_browsing = false;                         // false: the panel's own first list (Bibliothèque, Accueil...)
        int   s_rootFrom = 0;                             // which first-list row opened the page (its name, at depth 0)
        float s_hdrAt = 0;
        bool  s_open = false;
        float s_openAt = -100, s_closeAt = -100;
        int   s_sel = 0, s_top = 0;
        float s_selY = 0, s_selV = 0, s_barK = 0, s_barV = 0, s_room = 0, s_roomV = 0, s_bars[4] = {}, s_barsV[4] = {};
        float s_flash[4] = { -100, -100, -100, -100 };    // the buttons' small light when pressed: previous, play, next, volume
        float s_barX = -1000, s_barY = 52, s_barW = 0;
        char  s_lastKey[128] = "";                        // the track (title + artist): a new one = a new cover

        // the panel's first list
        struct RootRow { const char *name, *desc; };
        const RootRow kRoot[] = { { "Biblioth\xc3\xa8que", "Tes playlists, tes albums, tes artistes" },
                                  { "Accueil", "Reprendre, \xc3\xa9" "cout\xc3\xa9s r\xc3\xa9" "cemment, pour toi" },
                                  { "Rechercher", "Un titre, un album, un artiste" },
                                  { "\xc3\x80 suivre", "La file de lecture" },
                                  { "\xc3\x89teindre Spotify", "La musique s'arr\xc3\xaate, plus rien ne tourne" } };
        constexpr int kRootN = (int)(sizeof kRoot / sizeof kRoot[0]);

        Phase PhaseNow() {
            if (!s_want) return kOff;
            if (!s_installed) return kMissing;
            if (link::Stuck()) return kStartFail;         // (v101: a call that hangs: « Réessayer » stops the service)
            if (!s_conn || !s_stOk) return s_failed ? kStartFail : kStarting;
            switch (s_st.service_state) {
                case SwitifyServiceState_Ready: return kReady;
                case SwitifyServiceState_Pairing: case SwitifyServiceState_WaitingForCredentials: return kPairing;
                case SwitifyServiceState_Error: return kRefused;
                default: return kConnecting;
            }
        }
        bool HasTrack() { return PhaseNow() == kReady && (s_st.flags & SwitifyStatusFlag_HasTrack) && s_title[0]; }
        bool Playing() { return HasTrack() && s_st.playback_state == SwitifyPlaybackState_Playing; }
        u32 PositionNow(float now) {
            if (!s_stOk) return 0;
            u32 p = s_st.position_ms;
            if (s_st.playback_state == SwitifyPlaybackState_Playing) p += (u32)(fmaxf(0.0f, now - s_stAt) * 1000.0f);
            return s_st.duration_ms && p > s_st.duration_ms ? s_st.duration_ms : p;
        }

        void SaveIni() {
            mkdir("sdmc:/brume", 0777); mkdir("sdmc:/brume/spotify", 0777);
            if (FILE *f = fopen(kIni, "w")) { fprintf(f, "allume=%d\n", s_want ? 1 : 0); fclose(f); }
        }
        bool s_linkWant = false;                          // (v101) the frame wants the link up (s_conn = up and wanted)
        int  s_upSeen = 0, s_badSeen = 0, s_stSeen = 0, s_pageSeen = 0;
        bool s_stuckSaid = false;
        void Disconnect(const char *why) {
            if (s_conn) Log("spotify : liaison perdue (%s)", why);
            s_conn = false; s_linkWant = false; s_stOk = false;
            link::AskConnect(false);
        }
        void AskLink() { s_linkWant = true; link::AskConnect(true); }
        void Poll(float) { link::AskStatus(); }           // (v101: asked; the thread brings the status back, Sync reads it)
        void PollBrowse() { link::AskBrowse(); }
        bool Send(u32 cmd) {
            if (!s_conn || link::Stuck()) return false;
            link::Cmd c = {}; c.kind = link::kPlain; c.cmd = cmd;
            return link::Push(c);
        }
        bool SendU32(u32 cmd, u32 v) {
            if (!s_conn || link::Stuck()) return false;
            link::Cmd c = {}; c.kind = link::kU32; c.cmd = cmd; c.v = v;
            return link::Push(c);
        }
        bool Browse(u32 cmd, u64 gen, u32 index, const char *query = nullptr) {
            if (!s_conn || link::Stuck()) return false;
            link::Cmd c = {}; c.kind = link::kBrowse; c.cmd = cmd; c.gen = gen; c.index = index;
            if (query) snprintf(c.query, sizeof c.query, "%s", query);
            s_hdrAt = 0; s_itemsGen = ~0ULL;
            return link::Push(c);
        }
        // (frame, each image) what the thread brought back: the link's state, the status, the page
        void Sync(float now) {
            if (!link::Threaded()) link::Step();          // (no thread - the PC preview, or it could not start: here)
            SwitifyIpcStatus st;
            SwitifyIpcBrowseHeader h;
            bool newSt = false, newPage = false;
            int n = -1;
            link::Lock();
            const bool up = link::s_up;
            const int upSeq = link::s_upSeq;
            char lost[24];
            snprintf(lost, sizeof lost, "%s", link::s_lost); link::s_lost[0] = 0;
            const bool bad = link::s_badSeq != s_badSeen; const u32 badVer = link::s_badVer; s_badSeen = link::s_badSeq;
            if (link::s_stSeq != s_stSeen) { st = link::s_st; s_stSeen = link::s_stSeq; newSt = true; }
            if (link::s_pageSeq != s_pageSeen) {
                h = link::s_hdr; n = link::s_itN; s_pageSeen = link::s_pageSeq; newPage = true;
                if (n > 0) memcpy(s_items, link::s_it, sizeof(s_items[0]) * (size_t)n);
            }
            link::Unlock();
            if (bad) Log("spotify : version du service refusee (0x%X)", badVer);
            if (upSeq != s_upSeen) { s_upSeen = upSeq; if (up && s_linkWant) { s_failed = false; Log("spotify : service relie"); } }
            if (lost[0] && s_conn) Log("spotify : liaison perdue (%s)", lost);
            const bool conn = up && s_linkWant;
            if (s_conn && !conn) s_stOk = false;
            s_conn = conn;
            if (newSt && s_conn) {                        // the status (checked), its texts cleaned
                if (st.volume_permille > 1000) st.volume_permille = 1000;
                if (st.duration_ms > 24u * 3600u * 1000u) st.duration_ms = 0;
                if (st.position_ms > st.duration_ms && st.duration_ms) st.position_ms = st.duration_ms;
                s_st = st; s_stOk = true; s_stAt = now;
                Clean(s_title, sizeof s_title, st.title, (int)sizeof st.title);
                Clean(s_artist, sizeof s_artist, st.artist, (int)sizeof st.artist);
                Clean(s_album, sizeof s_album, st.album, (int)sizeof st.album);
            }
            if (newPage && s_conn) {
                s_hdr = h;
                if (n >= 0) {
                    if (s_selGen != h.generation) { s_sel = 0; s_top = 0; s_selGen = h.generation; }
                    s_nItems = n; s_itemsGen = h.generation;
                    if (s_sel >= n) s_sel = n ? n - 1 : 0;
                }
            }
            const bool stuck = link::Stuck();
            if (stuck && !s_stuckSaid) Log("spotify : le service ne repond plus (un appel attend depuis 3 s) - le menu continue");
            s_stuckSaid = stuck;
        }

        int Rows() {
            const Phase p = PhaseNow();
            if (p == kOff || p == kStartFail || p == kRefused) return 1;   // Allumer / Réessayer
            if (p != kReady) return 0;
            return s_browsing ? s_nItems : kRootN;
        }

        // ---- drawing helpers
        float Text(Ui &ui, fonts::Style st, const char *s, float x, float top, Rgba c) {
            for (const char *q = s; *q;) x += ui.Glyph(st, NextCodepoint(q), x, top, c, 0, 0, 0, 0);
            return x;
        }
        const char *Fit(Ui &ui, fonts::Style st, const char *s, float w, char *out, int cap) {
            // (always written into out, which it returns: the callers draw out)
            if (ui.Width(st, s, 0) <= w && (int)strlen(s) < cap) { snprintf(out, (size_t)cap, "%s", s); return out; }
            int n = (int)strlen(s);
            if (n > cap - 4) n = cap - 4;
            while (n > 0) {
                while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
                memcpy(out, s, (size_t)n); memcpy(out + n, "...", 4);
                if (ui.Width(st, out, 0) <= w) return out;
                n--;
            }
            out[0] = 0;
            return out;
        }
        void Clock(char *out, int cap, u32 ms) { const u32 s = ms / 1000; snprintf(out, (size_t)cap, "%u:%02u", s / 60, s % 60); }
        void Bar(Ui &ui, float x, float y, float w, float h, Rgba c) {   // a small rounded bar (mode 24)
            if (w > 0.2f && h > 0.2f) ui.Shape(x, y, x + w, y + h, 0, 0, w, h, c, 24, w, h, fminf(w, h) * 0.5f);
        }
        void Line(Ui &ui, float x, float y, float w, float f, Rgba c, float a) {   // a fine line of light, filled up to f
            ui.Shape(x, y, x + w, y + 1.5f, 0, 0, w, 1.5f, ps2::A(ps2::kChoice, 0.16f * a), 24, w, 1.5f, 0.75f);
            f = Clamp01(f);
            if (f > 0.002f) ui.Shape(x, y, x + w * f, y + 1.5f, 0, 0, w * f, 1.5f, ps2::A(c, 0.85f * a), 24, w * f, 1.5f, 0.75f);
        }

        // the panel's place: right of the screen, under the top bar (it grows out of the top bar's line)
        constexpr float kPW = 560, kPX = 1244 - kPW, kPY = 94, kPH = 596;
        constexpr float kListY = 290, kRowH = 42;         // (from the panel's top)
        constexpr int   kVisible = 5;
        float ListTop() { return kPY + kListY + 30; }

        void Move(int d, float now) {
            const int n = Rows();
            if (n <= 1) { if (d) audio::Play(audio::ThockSnd, 0, 0.5f); return; }
            const int to = s_sel + d;
            if (to < 0 || to >= n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
            s_sel = to;
            if (s_sel < s_top) s_top = s_sel;
            if (s_sel >= s_top + kVisible) s_top = s_sel - kVisible + 1;
            audio::Play(audio::Deplacement, 0, 1.0f);
            haptic::Tick(0.4f);
            (void)now;
        }
        void Search(float now) {
            haptic::Stop();
            char out[SWITIFY_IPC_BROWSE_QUERY_SIZE] = {};
            BrumeSwkbdOptions o = { "", lang::T("Rechercher sur Spotify"), 1, SWITIFY_IPC_BROWSE_QUERY_SIZE - 1, 1 };
            appletUnlockForeground();
            if (qext_swkbd_show_ex(&o, out, sizeof out) > 0 && out[0]) {
                out[sizeof out - 1] = 0;
                s_rootFrom = 2;
                s_browsing = true;
                Browse(SwitifyIpcCommand_BrowseSearch, 0, 0, out);
            }
            (void)now;
        }
        void PressA(float now) {
            const Phase p = PhaseNow();
            audio::Play(audio::Tick2400, 0, 1.0f);
            haptic::Tap(0.6f);
            if (p == kOff) { SetOn(true, now); return; }
            if (p == kStartFail || p == kRefused) {       // try again: the service starts afresh
                Disconnect("nouvel essai");
                Terminate();
                s_failed = false; s_launches = 0; s_launchAt = -100; s_nextTry = now + 0.8f;
                return;
            }
            if (p != kReady) return;
            if (!s_browsing) {
                switch (s_sel) {
                    case 0: s_rootFrom = 0; s_browsing = true; Browse(SwitifyIpcCommand_BrowseLibrary, 0, 0); break;
                    case 1: s_rootFrom = 1; s_browsing = true; Browse(SwitifyIpcCommand_BrowseHome, 0, 0); break;
                    case 2: Search(now); break;
                    case 3: s_rootFrom = 3; s_browsing = true; Browse(SwitifyIpcCommand_BrowseQueue, 0, 0); break;
                    default: SetOn(false, now); break;
                }
                return;
            }
            if (s_hdr.state == SwitifyBrowseState_Ready && s_sel >= 0 && s_sel < s_nItems)
                Browse(SwitifyIpcCommand_BrowseOpen, s_hdr.generation, s_items[s_sel].index);   // a track plays, the rest opens
        }
        bool PressB(float now) {                          // false: nothing to go back to (the panel closes)
            if (!s_browsing || PhaseNow() != kReady) return false;
            audio::Play(audio::Tick1700, 0, 1.0f);
            if (s_hdr.depth > 0 && s_hdr.state != SwitifyBrowseState_Loading) Browse(SwitifyIpcCommand_BrowseBack, s_hdr.generation, 0);
            else { s_browsing = false; s_sel = 0; s_top = 0; }
            (void)now;
            return true;
        }
        void Volume(int d, float now) {
            if (!HasTrack() && PhaseNow() != kReady) return;
            int v = (int)s_st.volume_permille + d * 50;
            v = v < 0 ? 0 : (v > 1000 ? 1000 : v);
            v = (v + 25) / 50 * 50;
            if (SendU32(SwitifyIpcCommand_SetVolume, (u32)v)) { s_st.volume_permille = (u32)v; s_flash[3] = now; }
            audio::Play(audio::Deplacement, d < 0 ? -0.2f : 0.2f, 0.6f);
            haptic::Tick(0.4f);
        }
    } // namespace

    // ================================================================ public
    void Boot(float now) {
        if (!kEnabled) { s_want = false; Log("spotify : desactive dans cette version"); return; }   // (v101: s_booted stays false)
        s_installed = Installed();
        s_want = false;
        if (FILE *f = fopen(kIni, "r")) {
            char line[64];
            while (fgets(line, sizeof line, f)) { int v = 0; if (sscanf(line, "allume=%d", &v) == 1) s_want = v != 0; }
            fclose(f);
        }
        s_bootAt = now + 3.0f;                            // (the service starts once the menu has settled: no stutter at boot)
        s_booted = true;
        Log("spotify : %s, service %s", s_want ? "allume" : "eteint", s_installed ? "installe" : "absent");
    }

    bool Wanted() { return s_want; }

    void SetOn(bool on, float now) {
        if (!kEnabled) return;
        if (now < 0) now = s_now;
        if (on == s_want) return;
        s_want = on;
        s_installed = Installed();
        SaveIni();
        if (on) { s_failed = false; s_launches = 0; s_launchAt = -100; s_nextTry = now; s_bootAt = now; OpenPanel(now); }
        else {                                            // off: the service stops, nothing at all runs any more
            Disconnect("eteint");
            u64 pid = 0;
            if (Running(&pid)) Terminate();
            s_browsing = false; s_sel = 0; s_top = 0;
            s_title[0] = 0; s_coverShown = false; s_coverUrl[0] = 0; s_lastKey[0] = 0;
            audio::Yield(false);
            mdns::Stop();
        }
        Log("spotify : %s", on ? "allume" : "eteint");
    }

    bool Busy() { return s_work.Busy() || s_work.Finished() || mdns::Running(); }   // (v99.1: the announce holds the network too)

    void Update(float now, float dt, bool menuOnScreen) {
        s_now = now;
#ifdef BRUME_UISIM
        s_simNow = now;
#endif
        if (!s_booted) return;
        if (s_want) link::Start();                        // (v101: the link's thread, made once, the first time it is wanted)
        Sync(now);
        // ---- the service: started when wanted, connected once it answers, stopped when switched off
        if (s_want && s_installed && now >= s_bootAt && now >= s_nextTry) {
            s_nextTry = now + 0.5f;
            u64 pid = 0;
            const bool run = Running(&pid);
            if (!run) {
                if (s_conn) Disconnect("service arrete");
                const bool waited = now - s_launchAt > 12.0f;
                if (!s_failed && (s_launchAt < 0 || waited)) {
                    if (s_launches >= 2) { s_failed = true; Log("spotify : le service ne demarre pas (memoire systeme ?)"); }
                    else {
                        const Result rc = Launch();
                        s_launches++; s_launchAt = now;
                        Log("spotify : demarrage du service (essai %d) : 0x%X", s_launches, rc);
                        if (R_FAILED(rc)) { s_launchAt = now - 9.0f; }   // (a refusal: tried again in 3 s, twice at most)
                    }
                }
            } else {
                s_pid = pid;
                if (!s_conn) {
                    if (Answers(pid)) AskLink();          // (v101: the thread connects; Sync logs « service relie »)
                    else if (s_launchAt > 0 && now - s_launchAt > 20.0f && !s_failed) { s_failed = true; Log("spotify : le service ne repond pas"); }
                }
            }
        }
        // ---- the status, 4 times a second while the menu is on screen (the bar, the panel)
        if (s_conn && menuOnScreen && now >= s_pollAt) {
            s_pollAt = now + (s_open ? 0.25f : 0.5f);
            Poll(now);
        }
        if (s_conn && s_open && s_browsing && now >= s_hdrAt) {
            s_hdrAt = now + (s_hdr.state == SwitifyBrowseState_Loading ? 0.12f : 0.4f);
            PollBrowse();
        }
        {   // ---- v99.1: the first connection - the menu announces BrumeNX itself when the service could not (see namespace mdns)
            const bool pairing = PhaseNow() == kPairing && s_open && menuOnScreen;
            if (pairing) {
                if (mdns::s_pairSince < 0) { mdns::s_pairSince = now; mdns::s_checkAt = 0; }
                if (now >= mdns::s_checkAt) { mdns::s_checkAt = now + 2.0f; mdns::ReadServiceState(); }
                const bool need = mdns::s_serviceFailed || (!mdns::s_serviceOk && now - mdns::s_pairSince > 6.0f);
                if (need && !mdns::Running() && now >= mdns::s_retryAt && s_work.Idle() && !covers::Busy() && !nextendo::Busy() && !social::NetBusy()) {
                    Log("spotify : le service ne s'annonce pas (%s) : le menu annonce BrumeNX", mdns::s_serviceFailed ? "echec" : "pas de nouvelles");
                    mdns::Start();
                }
                if (mdns::Running() && mdns::State() == 3) { mdns::Stop(); mdns::s_retryAt = now + 10.0f; }   // (failed: again in 10 s)
                if (mdns::Running() && mdns::s_serviceOk) mdns::Stop();   // (v101: the service announces itself now - never two)
            } else {
                mdns::s_pairSince = -1;
                if (mdns::Running()) mdns::Stop();
            }
            mdns::Reap();
            mdns::Drain();
        }
        audio::Yield(Playing());                          // the nappe steps aside while Spotify plays
        // ---- the cover: only while the panel is open, one download per track
        const float panelK = s_open ? 1.0f : 0.0f;
        if (HasTrack()) {
            char key[128];
            snprintf(key, sizeof key, "%s\x1f%s", s_title, s_artist);
            if (strcmp(key, s_lastKey)) { snprintf(s_lastKey, sizeof s_lastKey, "%s", key); s_wantUrl[0] = 0; s_coverTry = now + 0.4f; }
        }
        if (s_work.Done()) {
            if (s_job.ok) { snprintf(s_coverUrl, sizeof s_coverUrl, "%s", s_job.url); s_coverReady = true; }
            else { Log("spotify : pochette introuvable (%s)", s_job.why); snprintf(s_coverUrl, sizeof s_coverUrl, "%s", s_job.url); }
        }
        if (panelK > 0.5f && HasTrack() && menuOnScreen && now >= s_coverTry && s_work.Idle() && !qext_game_running()) {
            s_coverTry = now + 1.0f;
            char url[128];
            if (ArtUrl(url, sizeof url) && strcmp(url, s_coverUrl) && !covers::Busy() && !nextendo::Busy() && !social::NetBusy()) {
                snprintf(s_job.url, sizeof s_job.url, "%s", url);
                s_work.Start(CoverRun, &s_job, 0x8000);
            }
        }
        // ---- the motion
        Glide(s_barK, s_barV, HasTrack() ? 1.0f : 0.0f, dt, 7.0f);
        const bool play = Playing();
        for (int i = 0; i < 4; i++) {                     // four small bars that dance while it plays, low while paused
            const float t = play ? 0.35f + 0.65f * Clamp01(0.5f + 0.5f * sinf(now * (5.1f + i * 1.3f) + i * 1.9f) * sinf(now * (2.3f + i * 0.7f) + i)) : 0.12f;
            Glide(s_bars[i], s_barsV[i], t, dt, play ? 18.0f : 6.0f);
        }
        Glide(s_room, s_roomV, s_barK > 0.02f ? 30.0f : 0.0f, dt, 7.0f);   // (the four bars; the title is on its own line)
        const int n = Rows();
        if (s_sel >= n) s_sel = n ? n - 1 : 0;
        Glide(s_selY, s_selV, (float)(s_sel - s_top), dt, 11.0f);
    }

    void Shutdown() {
        mdns::StopWait();
        link::Stop();
        s_work.Wait();
#ifndef BRUME_UISIM
        if (s_dmnt) { pmdmntExit(); s_dmnt = false; }
#endif
    }

    // ---- the panel
    void OpenPanel(float now) {
        if (s_open || !kEnabled) return;
        s_open = true; s_openAt = now;
        if (Rows() && s_sel >= Rows()) s_sel = 0;
        s_pollAt = 0; s_hdrAt = 0;
        audio::Play(audio::Tick2400, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, 0.5f, 0.1f, 0.35f, 0.3f);
        haptic::Tick(0.5f);
    }
    void ClosePanel(float now) {
        if (!s_open) return;
        mdns::Stop();                                     // (the announce lives with the « Première connexion » screen)
        s_open = false; s_closeAt = now;
        audio::Play(audio::Tick1700, 0, 0.8f);
        haptic::Tick(0.4f);
    }
    void TogglePanel(float now) { if (s_open) ClosePanel(now); else OpenPanel(now); }
    bool PanelOpen() { return s_open; }

    void Input(u64 down, float now) {
        if (!s_open || !down) return;
        if (down & HidNpadButton_ZL) { ClosePanel(now); return; }
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) Move(-1, now);
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) Move(1, now);
        if (down & HidNpadButton_A) PressA(now);
        if (down & HidNpadButton_B) { if (!PressB(now)) ClosePanel(now); return; }
        if (!HasTrack()) return;
        if (down & HidNpadButton_X) { Send(SwitifyIpcCommand_PlayPause); s_flash[1] = now; haptic::Tap(0.7f); audio::Play(audio::Tick2400, 0, 0.7f); }
        if (down & (HidNpadButton_Left | HidNpadButton_StickLLeft)) { Send(SwitifyIpcCommand_Previous); s_flash[0] = now; haptic::Tick(0.6f); audio::Play(audio::Deplacement, -0.3f, 0.8f); }
        if (down & (HidNpadButton_Right | HidNpadButton_StickLRight)) { Send(SwitifyIpcCommand_Next); s_flash[2] = now; haptic::Tick(0.6f); audio::Play(audio::Deplacement, 0.3f, 0.8f); }
        if (down & HidNpadButton_Minus) Volume(-1, now);
        if (down & HidNpadButton_Plus) Volume(1, now);
        if (down & HidNpadButton_Y) SendU32(SwitifyIpcCommand_SetShuffle, (s_st.flags & SwitifyStatusFlag_Shuffle) ? 0u : 1u);
    }

    bool PanelTap(float x, float y, float now) {
        if (!s_open) return false;
        if (x < kPX - 20 || x > kPX + kPW + 20 || y < kPY || y > kPY + kPH) { ClosePanel(now); return true; }
        if (PhaseNow() != kReady) {                       // the one row of these states (Allumer / Réessayer)
            if (Rows() && y >= kPY + kPH - 120 && y < kPY + kPH - 120 + kRowH) PressA(now);
            return true;
        }
        const float ly = ListTop();
        if (y >= ly && y < ly + kVisible * kRowH) {      // a row: chosen; touched again: A
            const int r = s_top + (int)((y - ly) / kRowH);
            if (r >= 0 && r < Rows()) { if (r == s_sel) PressA(now); else { s_sel = r; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.4f); } }
            return true;
        }
        if (HasTrack() && y > kPY + 222 && y < kPY + 262) {   // the buttons' row
            const float cx = kPX + 26 + 64;
            if (fabsf(x - (cx - 50)) < 22) Input(HidNpadButton_Left, now);
            else if (fabsf(x - cx) < 24) Input(HidNpadButton_X, now);
            else if (fabsf(x - (cx + 50)) < 22) Input(HidNpadButton_Right, now);
            else if (x > kPX + 220) {                       // the volume line: set where touched
                const float f = Clamp01((x - (kPX + 268)) / 170.0f);
                const u32 v = (u32)(f * 20.0f + 0.5f) * 50;
                if (SendU32(SwitifyIpcCommand_SetVolume, v)) { s_st.volume_permille = v; s_flash[3] = now; haptic::Tick(0.4f); }
            }
        }
        return true;
    }

    // ---- the top bar
    float BarRoom() { return s_room; }
    bool BarHit(float x, float y) {                  // the four bars, or the title line under the date
        if (s_barK < 0.5f) return false;
        return (x > s_barX - 34 && x < s_barX + 12 && fabsf(y - s_barY) < 24) || (x > 1215.0f - s_barW - 10 && x < 1225.0f && y > s_barY + 14 && y < s_barY + 44);
    }
    void DrawBar(Ui &ui, float xRight, float cy, float alpha, float now) {
        s_barX = xRight; s_barY = cy;
        const float a = alpha * Clamp01(s_barK);
        if (a <= 0.003f) { if (s_barK < 0.01f) s_barW = 0; return; }
        const bool play = Playing();
        // the title (and the artist), faint, on a line just under the date and time, right-aligned with it (layout::kTimeRight =
        // 1215): the top bar itself has no room left between the tabs and the date
        char t[160], fit[180];
        if (s_artist[0]) snprintf(t, sizeof t, "%s  \xc2\xb7  %s", s_title, s_artist); else snprintf(t, sizeof t, "%s", s_title);
        const char *s = Fit(ui, fonts::kHint, t, 330, fit, sizeof fit);
        const float w = ui.Width(fonts::kHint, s, 0.5f), x0 = xRight - 22;
        s_barW = w;
        ui.Light(x0 - 14, cy - 18, xRight + 14, cy + 18, ps2::A(ps2::kHalo, (play ? 0.24f : 0.12f) * a * (0.8f + 0.2f * s_bars[0])), true);
        for (int i = 0; i < 4; i++) {
            const float h = 3.0f + 13.0f * s_bars[i], bx = x0 + i * 5.8f;
            Bar(ui, bx, cy + 8 - h, 3.2f, h, ps2::A(ps2::Mix(ps2::kChoice, ps2::kChosen, 0.3f + 0.5f * s_bars[i]), (play ? 0.9f : 0.55f) * a));
        }
        ui.TextCentered(fonts::kHint, s, 1215.0f - w * 0.5f, cy + 20, 0.5f, ps2::A(play ? ps2::kWords : ps2::kFaint, 0.85f * a));
        (void)now;
    }

    // ---- the cover
    bool InitCover(Gpu &gpu, uint32_t descriptor) {
        uint32_t align = 1;
        const uint32_t bytes = gpu.ImageBytes(kCover, kCover, DkImageFormat_RGB_BC1, 1, &align);
        const uint32_t off = gpu.ReserveImage(bytes, align);
        if (off == UINT32_MAX || descriptor >= 64) return false;
        s_texOk = gpu.PlaceTexture(s_tex, off, kCover, kCover, DkImageFormat_RGB_BC1, 1, descriptor);
        return s_texOk;
    }
    bool NeedsStream() { return s_coverReady && s_texOk; }
    void Stream(Gpu &gpu) {
        if (!NeedsStream()) return;
        if (gpu.StreamTexture(s_tex, s_bc, kCoverBc, 1)) { s_coverReady = false; s_coverShown = true; s_coverAt = 0; }
    }
    const Target *Cover() { return s_texOk && s_coverShown ? &s_tex : nullptr; }

    void SimSet(int state) {
#ifdef BRUME_UISIM
        s_simMode = state;
        s_booted = true; s_bootAt = 0;
        s_installed = state != 5;
        s_want = state != 0;
        s_simPlay = state == 2;
        s_simPosAt = s_simNow; s_simPos = 71000;
        s_conn = false; s_stOk = false; s_browsing = false; s_sel = 0; s_top = 0;   // (each state seen afresh)
        s_linkWant = false; link::AskConnect(false);
        if (state == 0) s_simRun = false;
        else { s_simRun = state != 5; s_failed = false; s_nextTry = 0; s_launchAt = s_simNow; }
#else
        (void)state;
#endif
    }

    // ================================================================ the panel's drawing
    void DrawPanel(Ui &ui, float now) {
        const float k = s_open ? EaseGlide((now - s_openAt) / Pace(0.42f)) : 1 - EaseGlide((now - s_closeAt) / Pace(0.30f));
        if (k <= 0.003f) return;
        const float Y = kPY - (1 - k) * 16, X = kPX;
        const Phase p = PhaseNow();
        ui.SetXform(0.94f + 0.06f * k, s_barX - 20, s_barY, s_barX - 20, s_barY);
        ui.Surface(X - 4, Y - 4, X + kPW + 4, Y + kPH + 2, k);
        // the title in yellow in its warm glow, and the state, faint, at the right
        const float tw = ui.Width(fonts::kSetSel, "Spotify", 0);
        ui.Light(X + 8, Y + 4, X + 28 + tw + 20, Y + 50, ps2::A(ps2::kTitleGlow, 0.15f * k), true);
        Text(ui, fonts::kSetSel, "Spotify", X + 24, Y + 12, ps2::A(ps2::kTitle, k));
        {
            const char *st = p == kOff ? "\xc3\xa9teint" : p == kMissing ? "non install\xc3\xa9" : p == kStarting ? "d\xc3\xa9marrage" :
                             p == kStartFail ? "bloqu\xc3\xa9" : p == kPairing ? "premi\xc3\xa8re connexion" : p == kConnecting ? "connexion" :
                             p == kRefused ? "refus\xc3\xa9" : Playing() ? "en lecture" : HasTrack() ? "en pause" : "pr\xc3\xaat";
            const Rgba c = p == kStartFail || p == kRefused ? ps2::kPink : (Playing() ? ps2::kGreen : ps2::kFaint);
            const float w = ui.Width(fonts::kHint, st, 1.0f);
            ui.TextCentered(fonts::kHint, st, X + kPW - 26 - w * 0.5f, Y + 24, 1.0f, ps2::A(c, 0.9f * k));
        }
        const float cx0 = X + 26, tx = X + 26 + kCover + 24, tw2 = kPW - (tx - X) - 26;
        char fit[200], line[200];
        // ---- the states before the music: a few words floating in the fog
        auto Para = [&](float y, const char *const *lines, int n, Rgba c, fonts::Style st) {
            for (int i = 0; i < n; i++) { Fit(ui, st, lang::T(lines[i]), kPW - 52, fit, sizeof fit); Text(ui, st, fit, cx0, y + i * 28, ps2::A(c, k)); }
        };
        if (p != kReady) {
            float y = Y + 76;
            if (p == kOff) {
                Text(ui, fonts::kSetOpt, lang::T("Ta musique Spotify, m\xc3\xaame en jeu"), cx0, y, ps2::A(ps2::kChosen, k));
                static const char *const L[] = { "Choisis tes playlists ici : la Switch joue la musique,",
                                                 "et elle continue quand tu lances un jeu.",
                                                 "Il faut un compte Spotify Premium.",
                                                 "\xc3\x89teint, rien ne tourne : z\xc3\xa9ro co\xc3\xbbt pour la console." };
                Para(y + 42, L, 4, ps2::kWords, fonts::kHintBig);
            } else if (p == kMissing) {
                Text(ui, fonts::kSetOpt, lang::T("Le lecteur de musique n'est pas install\xc3\xa9"), cx0, y, ps2::A(ps2::kChosen, k));
                static const char *const L[] = { "Le paquet Brume contient un dossier atmosphere :",
                                                 "copie-le \xc3\xa0 la racine de la carte SD,",
                                                 "puis red\xc3\xa9marre la console." };
                Para(y + 42, L, 3, ps2::kWords, fonts::kHintBig);
            } else if (p == kStarting || p == kConnecting) {
                busy::Orbs(ui, cx0 + 16, y + 14, 11.0f, k, now);
                Text(ui, fonts::kSetOpt, lang::T(p == kStarting ? "D\xc3\xa9marrage du lecteur..." : "Connexion \xc3\xa0 Spotify..."), cx0 + 44, y, ps2::A(ps2::kChosen, k));
                if (p == kStarting && s_launchAt > 0 && now - s_launchAt > 8.0f) {
                    static const char *const L[] = { "\xc3\x87" "a prend du temps : la console manque peut-\xc3\xaatre de m\xc3\xa9moire." };
                    Para(y + 42, L, 1, ps2::kFaint, fonts::kHintBig);
                }
            } else if (p == kStartFail) {
                Text(ui, fonts::kSetOpt, lang::T("Le lecteur n'a pas pu d\xc3\xa9marrer"), cx0, y, ps2::A(ps2::kPink, k));
                static const char *const L[] = { "La console manque s\xc3\xbbrement de m\xc3\xa9moire syst\xc3\xa8me.",
                                                 "Enl\xc3\xa8ve les overlays ou modules syst\xc3\xa8me en trop,",
                                                 "red\xc3\xa9marre, puis r\xc3\xa9" "essaie." };
                Para(y + 42, L, 3, ps2::kWords, fonts::kHintBig);
            } else if (p == kRefused) {
                Text(ui, fonts::kSetOpt, lang::T("Spotify refuse la lecture"), cx0, y, ps2::A(ps2::kPink, k));
                static const char *const L[] = { "V\xc3\xa9rifie ta connexion et ton abonnement Premium.",
                                                 "Spotify bloque parfois les comptes Premium r\xc3\xa9" "cents." };
                Para(y + 42, L, 2, ps2::kWords, fonts::kHintBig);
                if (s_album[0]) { Fit(ui, fonts::kHint, s_album, kPW - 52, fit, sizeof fit); Text(ui, fonts::kHint, fit, cx0, y + 42 + 66, ps2::A(ps2::kFaint, 0.9f * k)); }
            } else if (p == kPairing) {                   // the first time: the phone, step by step
                Text(ui, fonts::kSetOpt, lang::T("Premi\xc3\xa8re connexion"), cx0, y, ps2::A(ps2::kChosen, k));
                static const char *const S[] = { "Mets la Switch et ton t\xc3\xa9l\xc3\xa9phone sur le m\xc3\xaame Wi-Fi",
                                                 "Ouvre Spotify sur ton t\xc3\xa9l\xc3\xa9phone",
                                                 "Touche Appareils, puis choisis",
                                                 "C'est tout : la Switch s'en souviendra" };
                for (int i = 0; i < 4; i++) {
                    const float ry = y + 50 + i * 54;
                    char num[4]; snprintf(num, sizeof num, "%d", i + 1);
                    ui.Light(cx0 - 6, ry - 6, cx0 + 30, ry + 30, ps2::A(ps2::kHalo, 0.25f * k), true);
                    ui.TextCentered(fonts::kSetOpt, num, cx0 + 12, ry, 0, ps2::A(ps2::kTitle, k));
                    Fit(ui, fonts::kHintBig, lang::T(S[i]), kPW - 100, fit, sizeof fit);
                    const float ex = Text(ui, fonts::kHintBig, fit, cx0 + 40, ry + 1, ps2::A(ps2::kWords, k));
                    if (i == 2) {                         // « BrumeNX », bright, breathing in its halo
                        const float bx = ex + 12, bw = ui.Width(fonts::kSetOpt, "BrumeNX", 0), br = 0.75f + 0.25f * sinf(now * 2.6f);
                        ui.Light(bx - 18, ry - 12, bx + bw + 18, ry + 36, ps2::A(ps2::kHalo, 0.45f * br * k), true);
                        Text(ui, fonts::kSetOpt, "BrumeNX", bx, ry - 1, ps2::A(ps2::kChosen, k));
                    }
                }
                static const char *const L[] = { "Il faut un compte Spotify Premium.",
                                                 "Tu ne vois pas BrumeNX ? Autorise le r\xc3\xa9seau local pour Spotify." };
                Para(y + 50 + 4 * 54 + 8, L, 2, ps2::kFaint, fonts::kHint);
                if (mdns::State() == 2) Text(ui, fonts::kHint, lang::T("BrumeNX est annoncé sur ton Wi-Fi par la console"), cx0, y + 50 + 4 * 54 + 8 + 62, ps2::A(ps2::kGreen, 0.9f * k));
                else if (mdns::State() == 3) Text(ui, fonts::kHint, lang::T("La console n'arrive pas à s'annoncer sur le Wi-Fi : nouvel essai..."), cx0, y + 50 + 4 * 54 + 8 + 62, ps2::A(ps2::kPink, 0.9f * k));
                busy::Orbs(ui, X + kPW - 40, Y + kPH - 44, 9.0f, 0.8f * k, now);
            }
            // the one row of these states (Allumer / Réessayer), chosen, near the bottom
            if (p == kOff || p == kStartFail || p == kRefused) {
                const char *row = p == kOff ? "Allumer Spotify" : "R\xc3\xa9" "essayer";
                const float ry = Y + kPH - 120, rw = ui.Width(fonts::kSetOpt, row, 0);
                ui.Chosen(X - 6, X + kPW + 6, ry + kRowH * 0.5f, cx0, rw, k);
                ui.TextCentered(fonts::kSetOpt, row, cx0 + rw * 0.5f, ry + 10, 0, ps2::Line(1.0f, k));
            }
        } else {
            // ---- what plays: the cover, the title, the artist, the album, where it is
            const float cy = Y + 70;
            const Target *cov = Cover();
            if (HasTrack() && cov) {
                ui.Light(cx0 - 22, cy - 18, cx0 + kCover + 22, cy + kCover + 26, ps2::A(ps2::kHalo, 0.22f * k), true);
                ui.Shape(cx0, cy, cx0 + kCover, cy + kCover, 0, 0, 1, 1, Rgba{ 0.86f, 0.86f, 0.88f, k }, 32, 0.10f);   // (a bit darker: never dazzling)
            } else {                                      // no cover (yet): a soft square of light with a note of music
                ui.Shape(cx0, cy, cx0 + kCover, cy + kCover, 0, 0, kCover, kCover, ps2::A(ps2::kHalo, 0.10f * k), 24, kCover, kCover, 14);
                ui.Light(cx0 + 20, cy + 20, cx0 + kCover - 20, cy + kCover - 20, ps2::A(ps2::kHalo, 0.18f * k), true);
                Bar(ui, cx0 + 70, cy + 40, 4, 44, ps2::A(ps2::kChoice, 0.55f * k));
                ui.Shape(cx0 + 50, cy + 74, cx0 + 74, cy + 92, 0, 0, 24, 18, ps2::A(ps2::kChoice, 0.55f * k), 24, 24, 18, 9);
            }
            if (HasTrack()) {
                Fit(ui, fonts::kSetOpt, s_title, tw2, fit, sizeof fit);
                Text(ui, fonts::kSetOpt, fit, tx, cy + 4, ps2::A(ps2::kChosen, k));
                Fit(ui, fonts::kHintBig, s_artist, tw2, fit, sizeof fit);
                Text(ui, fonts::kHintBig, fit, tx, cy + 36, ps2::A(ps2::kWords, k));
                Fit(ui, fonts::kHint, s_album, tw2, fit, sizeof fit);
                Text(ui, fonts::kHint, fit, tx, cy + 64, ps2::A(ps2::kFaint, 0.9f * k));
                const u32 pos = PositionNow(now), dur = s_st.duration_ms;
                Line(ui, tx, cy + 100, tw2, dur ? (float)pos / (float)dur : 0.0f, Playing() ? ps2::kChoice : ps2::kFaint, k);
                char a[16], b[16]; Clock(a, sizeof a, pos); Clock(b, sizeof b, dur);
                Text(ui, fonts::kHint, a, tx, cy + 108, ps2::A(ps2::kFaint, 0.85f * k));
                ui.TextCentered(fonts::kHint, b, tx + tw2 - ui.Width(fonts::kHint, b, 0) * 0.5f, cy + 108, 0, ps2::A(ps2::kFaint, 0.85f * k));
                if (s_st.flags & SwitifyStatusFlag_Shuffle) {
                    const float sw = ui.Width(fonts::kHint, "al\xc3\xa9" "atoire", 0.5f);
                    ui.TextCentered(fonts::kHint, "al\xc3\xa9" "atoire", tx + tw2 * 0.5f, cy + 108, 0.5f, ps2::A(ps2::kGreen, 0.85f * k));
                    (void)sw;
                }
                // the buttons: previous, play / pause, next (drawn, with a small light when pressed), then the volume
                const float by = Y + 242, bx = cx0 + 64;
                auto Flash = [&](int i, float x) {
                    const float f = Clamp01(1.0f - (now - s_flash[i]) / Pace(0.35f));
                    ui.Light(x - 26, by - 26, x + 26, by + 26, ps2::A(ps2::kHalo, (0.10f + 0.45f * f) * k), true);
                };
                const Rgba ic = ps2::A(ps2::kChosen, 0.92f * k);
                Flash(0, bx - 50); Bar(ui, bx - 59, by - 8, 3, 16, ic); ui.Arrow(bx - 47, by, 8, 0, ic);
                Flash(1, bx);
                if (Playing()) { Bar(ui, bx - 7, by - 10, 5, 20, ic); Bar(ui, bx + 2, by - 10, 5, 20, ic); }
                else ui.Arrow(bx + 2, by, 11, 1, ic);
                Flash(2, bx + 50); ui.Arrow(bx + 47, by, 8, 1, ic); Bar(ui, bx + 56, by - 8, 3, 16, ic);
                {   // the volume: a fine line of light and its number
                    const float vx = X + 268, vw = 170, f = s_st.volume_permille / 1000.0f;
                    const float fl = Clamp01(1.0f - (now - s_flash[3]) / Pace(0.5f));
                    Text(ui, fonts::kHint, lang::T("Volume"), X + 196, by - 9, ps2::A(ps2::kFaint, 0.9f * k));
                    Line(ui, vx, by, vw, f, ps2::Mix(ps2::kChoice, ps2::kChosen, fl), k);
                    ui.Light(vx + vw * f - 10, by - 10, vx + vw * f + 10, by + 10, ps2::A(ps2::kHalo, (0.35f + 0.4f * fl) * k), true);
                    snprintf(line, sizeof line, "%u", (s_st.volume_permille + 5) / 10);   // (no « % » in the hints' font)
                    Text(ui, fonts::kHint, line, vx + vw + 14, by - 9, ps2::A(ps2::kWords, 0.9f * k));
                }
            } else {
                Text(ui, fonts::kSetOpt, lang::T("Rien en lecture"), tx, cy + 22, ps2::A(ps2::kChosen, k));
                Fit(ui, fonts::kHintBig, lang::T("Choisis une playlist ci-dessous"), tw2, fit, sizeof fit);
                Text(ui, fonts::kHintBig, fit, tx, cy + 56, ps2::A(ps2::kWords, k));
            }
            // ---- the list: the panel's own first list, or a page of Spotify
            const float hy = Y + kListY;
            ui.Shape(cx0, hy - 8, X + kPW - 26, hy - 6.5f, 0, 0, kPW - 52, 1.5f, ps2::A(ps2::kChoice, 0.12f * k), 24, kPW - 52, 1.5f, 0.75f);
            // (the service names its first pages in English: at depth 0, the name of the row that opened it)
            const bool own = !s_browsing || s_hdr.depth == 0;
            const char *head = !s_browsing ? "Choisir la musique" : own ? kRoot[s_rootFrom].name : s_hdr.title[0] ? s_hdr.title : "Spotify";
            char hc[72]; Clean(hc, sizeof hc, head, (int)strlen(head) + 1);
            Fit(ui, fonts::kHint, own ? lang::T(hc) : hc, kPW - 52, fit, sizeof fit);
            Text(ui, fonts::kHint, fit, cx0, hy, ps2::A(ps2::kTitle, 0.9f * k));
            const float ly = ListTop();
            if (s_browsing && s_hdr.state == SwitifyBrowseState_Loading) {
                busy::Orbs(ui, cx0 + 16, ly + 22, 10.0f, k, now);
                Text(ui, fonts::kHintBig, lang::T("Chargement..."), cx0 + 42, ly + 10, ps2::A(ps2::kWords, k));
            } else if (s_browsing && s_hdr.state == SwitifyBrowseState_Error) {
                Text(ui, fonts::kHintBig, lang::T("Spotify n'a pas r\xc3\xa9pondu"), cx0, ly + 10, ps2::A(ps2::kPink, k));
                char m[64]; Clean(m, sizeof m, s_hdr.message, (int)sizeof s_hdr.message);
                Fit(ui, fonts::kHint, m, kPW - 52, fit, sizeof fit);
                Text(ui, fonts::kHint, fit, cx0, ly + 40, ps2::A(ps2::kFaint, 0.9f * k));
            } else if (s_browsing && s_nItems == 0) {
                Text(ui, fonts::kHintBig, lang::T("Rien ici"), cx0, ly + 10, ps2::A(ps2::kFaint, k));
            } else {
                const int n = Rows();
                ui.Chosen(X - 6, X + kPW + 6, ly + (s_selY + 0.5f) * kRowH, cx0, 200, k);
                for (int r = s_top; r < n && r < s_top + kVisible; r++) {
                    const float ry = ly + (r - s_top) * kRowH;
                    const float ck = Clamp01(1.0f - fabsf(s_selY - (float)(r - s_top)));
                    const char *name, *sub;
                    char nm[96];
                    if (!s_browsing) { name = lang::T(kRoot[r].name); sub = lang::T(kRoot[r].desc); }
                    else {
                        const SwitifyIpcBrowseItem &it = s_items[r];
                        name = it.name; sub = it.kind == SwitifyBrowseItemKind_Track ? it.subtitle : it.detail[0] ? it.detail : it.subtitle;
                        if (it.kind == SwitifyBrowseItemKind_Section && !strncmp(it.name, "Next ", 5)) name = lang::T("Suite");
                        else if (it.kind == SwitifyBrowseItemKind_Section && !strncmp(it.name, "Previous ", 9)) name = lang::T("Pr\xc3\xa9" "c\xc3\xa9" "dents");
                    }
                    const bool section = s_browsing && s_items[r].kind == SwitifyBrowseItemKind_Section;
                    Fit(ui, fonts::kSetOpt, name, 300, nm, sizeof nm);
                    const float ex = Text(ui, fonts::kSetOpt, nm, cx0, ry + 10, section ? ps2::A(ps2::Mix(ps2::kTitle, ps2::kChosen, ck * 0.5f), k) : ps2::Line(ck, k));
                    if (sub && sub[0]) {
                        const float room = X + kPW - 26 - (ex + 16);
                        if (room > 40) { Fit(ui, fonts::kHint, sub, room, fit, sizeof fit); Text(ui, fonts::kHint, fit, ex + 16, ry + 14, ps2::A(ps2::kFaint, (0.6f + 0.3f * ck) * k)); }
                    }
                }
                if (s_top > 0) ui.Arrow(X + kPW - 30, ly - 6, 5, 2, ps2::A(ps2::kFaint, 0.8f * k));
                if (s_top + kVisible < n) ui.Arrow(X + kPW - 30, ly + kVisible * kRowH + 2, 5, 3, ps2::A(ps2::kFaint, 0.8f * k));
            }
        }
        // ---- the hints, as everywhere: the button's letter in colour, then its word
        {
            const char *L[6]; const char *W[6]; int n = 0;
            const bool ready = p == kReady;
            if (Rows() > 0) { L[n] = "A"; W[n++] = p == kOff ? "Allumer" : (p == kStartFail || p == kRefused) ? "R\xc3\xa9" "essayer" : "Choisir"; }
            L[n] = "B"; W[n++] = ready && s_browsing ? "Retour" : "Fermer";
            if (ready && HasTrack()) { L[n] = "X"; W[n++] = Playing() ? "Pause" : "Lecture"; L[n] = "- +"; W[n++] = "Volume"; L[n] = "Y"; W[n++] = "Al\xc3\xa9" "atoire"; }
            float total = 0;
            for (int i = 0; i < n; i++) total += ui.Width(fonts::kBtnSmall, L[i], 0) + 8 + ui.Width(fonts::kHint, W[i], 0) + (i + 1 < n ? 24 : 0);
            float hx = X + kPW / 2 - total / 2;
            const float hy = Y + kPH - 32;
            for (int i = 0; i < n; i++) {
                const Rgba bc = L[i][0] == 'B' ? ps2::kPink : L[i][0] == 'Y' ? ps2::kGreen : ps2::kChoice;
                hx = Text(ui, fonts::kBtnSmall, L[i], hx, hy - 1, ps2::A(bc, k)) + 8;
                hx = Text(ui, fonts::kHint, lang::T(W[i]), hx, hy, ps2::A(ps2::kWords, 0.9f * k)) + 24;
            }
            if (ready && HasTrack()) {                    // ◀ ▶ : the track (two small arrows), under the hints
                const char *w = lang::T("Morceau pr\xc3\xa9" "c\xc3\xa9" "dent / suivant");
                const float ww = ui.Width(fonts::kHint, w, 0), ax = X + kPW / 2 - (ww + 30) / 2;
                ui.Arrow(ax + 4, Y + kPH - 52 + 9, 5, 0, ps2::A(ps2::kChoice, k)); ui.Arrow(ax + 16, Y + kPH - 52 + 9, 5, 1, ps2::A(ps2::kChoice, k));
                Text(ui, fonts::kHint, w, ax + 30, Y + kPH - 52, ps2::A(ps2::kFaint, 0.8f * k));
            }
        }
        ui.SetZoom(1.0f);
    }

} // namespace brume::spotify
