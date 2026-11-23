// Brume - v49: Nextendo's players online. See Nextendo.hpp.
#include "Nextendo.hpp"
#include "Notify.hpp"
#include "Settings.hpp"
#include "Spotify.hpp"
#include "Ntp.hpp"
#include "Covers.hpp"
#include "Social.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>

extern "C" int qext_game_running(void);

namespace brume::nextendo {

    void (*s_log)(const char *) = nullptr;

    // one HTTPS request through the console's TLS service (v75: shared with the covers' downloader, Covers.cpp); the answer
    // goes to sink as it arrives, headers included (sink < 0: stop reading). verify = check the server's certificate;
    // firstTick (v71): when the answer's first bytes arrived (the Date header's moment). -1: no answer (why says why).
    int Https(const char *host, const char *path, bool verify, const char *accept, int (*sink)(void *, const char *, int), void *user,
              char *why, int whyCap, const char *method, u64 *firstTick, const char *headers, const char *body) {
        addrinfo hints; memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo *ai = nullptr;
        if (getaddrinfo(host, "443", &hints, &ai) != 0 || !ai) { snprintf(why, (size_t)whyCap, "DNS"); return -1; }
        const int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) { freeaddrinfo(ai); snprintf(why, (size_t)whyCap, "socket"); return -1; }
        timeval tv = { 6, 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        const bool con = connect(fd, ai->ai_addr, ai->ai_addrlen) == 0;
        freeaddrinfo(ai);
        if (!con) { close(fd); snprintf(why, (size_t)whyCap, "connexion"); return -1; }
        if (qext_game_running()) { close(fd); snprintf(why, (size_t)whyCap, "jeu lance"); return -1; }   // v56  - lang:log (never drawn)
        int got = -1;
        SslContext ctx; SslConnection c;
        if (R_SUCCEEDED(sslCreateContext(&ctx, SslVersion_Auto))) {
            if (R_SUCCEEDED(sslContextCreateConnection(&ctx, &c))) {
                const int outfd = socketSslConnectionSetSocketDescriptor(&c, fd);
                bool ok = outfd >= 0 || errno == ENOENT;
                ok = ok && R_SUCCEEDED(sslConnectionSetHostName(&c, host, strlen(host)));
                if (ok && !verify) {
                    sslConnectionSetOption(&c, SslOptionType_SkipDefaultVerify, true);
                    sslConnectionSetVerifyOption(&c, 0);
                }
                sslConnectionSetIoTimeout(&c, 8000);
                Result hr = ok ? sslConnectionDoHandshake(&c, nullptr, nullptr, nullptr, 0) : MAKERESULT(Module_Libnx, LibnxError_IoError);
                if (R_SUCCEEDED(hr)) {
                    // v95: the request may carry header lines (a token) and a body: sized for them, freed right after
                    const size_t cap = 512 + strlen(path) + strlen(host) + (headers ? strlen(headers) : 0) + (body ? strlen(body) : 0);
                    char *req = (char *)malloc(cap);
                    const int rl = !req ? -1 : snprintf(req, cap, "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Brume\r\nAccept: %s\r\n%sConnection: close\r\n\r\n%s",
                                            method ? method : "GET", path, host, accept ? accept : "*/*", headers ? headers : "", body ? body : "");
                    u32 w = 0;
                    const bool sent = rl > 0 && rl < (int)cap && R_SUCCEEDED(sslConnectionWrite(&c, req, (u32)rl, &w)) && w == (u32)rl;
                    if (req) { memset(req, 0, cap); free(req); }   // (it may have held a token)
                    if (sent) {
                        char chunk[4096];
                        int n = 0;
                        for (;;) {
                            u32 r = 0;
                            if (R_FAILED(sslConnectionRead(&c, chunk, sizeof chunk, &r)) || r == 0) break;
                            if (!n && firstTick) *firstTick = armGetSystemTick();
                            n += (int)r;
                            if (sink(user, chunk, (int)r) < 0) break;
                        }
                        got = n;
                    } else snprintf(why, (size_t)whyCap, "envoi");
                } else snprintf(why, (size_t)whyCap, "TLS 0x%X", hr);
                if (outfd >= 0) close(outfd);
                sslConnectionClose(&c);
            }
            sslContextClose(&ctx);
        } else snprintf(why, (size_t)whyCap, "ssl");
        close(fd);
        return got;
    }

    namespace {
        constexpr const char *kHost = "nextendo.network";
        constexpr int kMaxTitles = 600, kBody = 0x10000;
        constexpr float kEvery = 300.0f;                 // seconds between two fetches
        // the table the menu reads (main thread) and the one the worker fills
        u64 s_id[kMaxTitles], s_nid[kMaxTitles];
        u16 s_n[kMaxTitles], s_nn[kMaxTitles];
        int s_count = 0, s_ncount = 0;
        int s_state = 0;                                 // 0 idle, 1 working, 2 done (atomics)
        Thread s_thread;
        bool  s_threadOn = false;
        float s_lastAt = -1e9f;
        int   s_task = -1;                               // v77: its line in the task centre
        char  s_why[96];
        // the Date of the answer (worker -> main thread when the fetch is done): the console's clock (Ntp.hpp)
        long long s_ndate = 0;
        u64 s_ndateTick = 0;
        bool s_force = false;                            // v72.1: a fetch now (the clock's row)

        // v71 - the HTTP « Date: » header (RFC 7231 IMF-fixdate: « Sun, 16 Nov 2026 21:42:07 GMT »), defensively: only in the
        // headers, bounded, every field checked; false if it is missing or odd
        bool ParseDate(const char *b, int len, long long *secs) {
            int hend = len;
            for (int i = 0; i + 3 < len; i++) if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') { hend = i; break; }
            for (int i = 0; i + 7 < hend; i++) {
                if (b[i] != '\n') continue;
                const char *h = b + i + 1;
                if ((h[0] | 0x20) != 'd' || (h[1] | 0x20) != 'a' || (h[2] | 0x20) != 't' || (h[3] | 0x20) != 'e' || h[4] != ':') continue;
                char v[48] = {};
                int k = 0;
                for (const char *q = h + 5; q < b + hend && *q != '\r' && *q != '\n' && k < (int)sizeof v - 1; q++) v[k++] = *q;
                int day = 0, year = 0, hh = 0, mm = 0, ss = 0; char mon[4] = {};
                if (sscanf(v, " %*3[A-Za-z], %2d %3[A-Za-z] %4d %2d:%2d:%2d", &day, mon, &year, &hh, &mm, &ss) != 6) return false;
                static const char kMon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
                int m = -1;
                for (int j = 0; j < 12; j++) if (!strncmp(mon, kMon + 3 * j, 3)) { m = j + 1; break; }
                if (m < 0 || day < 1 || day > 31 || year < 2025 || year > 2099 || hh > 23 || mm > 59 || ss > 60) return false;
                // days since 1970-01-01 (the civil calendar, proleptic Gregorian)
                const int y = year - (m <= 2), era = y / 400, yoe = y - era * 400;
                const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
                const long long days = (long long)era * 146097 + doe - 719468;
                *secs = days * 86400 + hh * 3600 + mm * 60 + ss;
                return true;
            }
            return false;
        }

        void Add(u64 id, int n, bool onlyNew) {
            for (int i = 0; i < s_ncount; i++) if (s_nid[i] == id) { if (!onlyNew && n > s_nn[i]) s_nn[i] = (u16)n; return; }
            if (s_ncount < kMaxTitles) { s_nid[s_ncount] = id; s_nn[s_ncount] = (u16)(n < 0 ? 0 : (n > 65535 ? 65535 : n)); s_ncount++; }
        }
        // {"counts":{"0100...":12,...}} - defensive: bounded, nothing trusted
        void Parse(const char *b, int len, bool onlyNew) {
            const char *end = b + len;
            const char *p = strstr(b, "\"counts\"");
            if (!p) return;
            p = (const char *)memchr(p, '{', end - p);
            if (!p) return;
            const char *close = (const char *)memchr(p, '}', end - p);
            if (!close) return;
            for (p++; p < close;) {
                const char *q0 = (const char *)memchr(p, '"', close - p);
                if (!q0) break;
                const char *q1 = (const char *)memchr(q0 + 1, '"', close - q0 - 1);
                if (!q1 || q1 - q0 - 1 > 16) break;
                char hex[17] = {};
                memcpy(hex, q0 + 1, q1 - q0 - 1);
                const char *colon = (const char *)memchr(q1, ':', close - q1);
                if (!colon) break;
                char *ne = nullptr;
                const long n = strtol(colon + 1, &ne, 10);
                const u64 id = strtoull(hex, nullptr, 16);
                if (id && ne != colon + 1) Add(id, (int)n, onlyNew);
                p = ne && ne > colon ? ne : colon + 1;
            }
        }
        // the body of a chunked HTTP/1.1 answer, in place; returns its length
        int Dechunk(char *b, int len) {
            int out = 0, i = 0;
            while (i < len) {
                char *ne = nullptr;
                const long sz = strtol(b + i, &ne, 16);
                if (!ne || ne == b + i || sz < 0) break;
                char *crlf = strstr(ne, "\r\n");
                if (!crlf) break;
                i = (int)(crlf - b) + 2;
                if (sz == 0 || sz > len - i) break;          // v56 (review): no overflow
                memmove(b + out, b + i, (size_t)sz);
                out += (int)sz; i += (int)sz + 2;
            }
            b[out] = 0;
            return out;
        }
        // Nextendo's request: the whole answer into buf (headers included)
        struct Into { char *buf; int cap, n; };
        int IntoSink(void *u, const char *p, int n) {
            Into &d = *(Into *)u;
            const int k = n < d.cap - 1 - d.n ? n : d.cap - 1 - d.n;
            if (k > 0) { memcpy(d.buf + d.n, p, (size_t)k); d.n += k; }
            return d.n < d.cap - 1 ? 0 : -1;                 // (full: stop reading)
        }
        int Get(const char *path, char *buf, int cap, bool verify, const char *method = "GET", u64 *firstTick = nullptr) {
            Into d = { buf, cap, 0 };
            const int r = Https(kHost, path, verify, "application/json", IntoSink, &d, s_why, (int)sizeof s_why, method, firstTick);
            buf[d.n] = 0;
            return r < 0 ? -1 : d.n;
        }
        // the answer: 200, then its body (chunked or not)
        int Body(char *buf, int len, char **body) {
            if (len < 12 || strncmp(buf, "HTTP/1.", 7) || strncmp(buf + 9, "200", 3)) return -1;
            char *h = strstr(buf, "\r\n\r\n");
            if (!h) return -1;
            *h = 0;
            const bool chunked = strstr(buf, "chunked") != nullptr;
            char *b = h + 4;
            int bl = len - (int)(b - buf);
            if (chunked) bl = Dechunk(b, bl);
            *body = b;
            return bl;
        }

        void Work(void *) {
            s_ncount = 0;
            s_why[0] = 0;
            SocketInitConfig cfg = *socketGetDefaultInitConfig();   // small buffers (the menu's memory is short)
            cfg.tcp_tx_buf_size = 0x2000; cfg.tcp_rx_buf_size = 0x8000; cfg.tcp_tx_buf_max_size = 0; cfg.tcp_rx_buf_max_size = 0;
            cfg.udp_tx_buf_size = 0x800; cfg.udp_rx_buf_size = 0x800; cfg.sb_efficiency = 1; cfg.num_bsd_sessions = 1;
            char *buf = (char *)malloc(kBody);
            bool verified = true;
            if (buf && R_SUCCEEDED(socketInitialize(&cfg))) {
                if (R_SUCCEEDED(sslInitialize(1))) {
                    static const char *const kPaths[2] = { "/api/online-counts", "/service-catalog/v1/online-counts" };
                    for (int k = 0; k < 2; k++) {
                        if (qext_game_running()) break;          // v56 (review): a game started: no more network
                        u64 tick = 0;
                        int n = Get(kPaths[k], buf, kBody, true, "GET", &tick);
                        if (n < 0 && strncmp(s_why, "TLS", 3) == 0) { verified = false; n = Get(kPaths[k], buf, kBody, false, "GET", &tick); }
                        long long d = 0;                         // v71: its Date (before Body cuts the headers off)
                        if (n > 0 && ParseDate(buf, n, &d)) { s_ndate = d; s_ndateTick = tick; }
                        char *body = nullptr;
                        const int bl = n > 0 ? Body(buf, n, &body) : -1;
                        if (bl > 0) Parse(body, bl, k == 1);
                    }
                    sslExit();
                } else snprintf(s_why, sizeof s_why, "ssl indisponible");
                socketExit();
            } else if (!s_why[0]) snprintf(s_why, sizeof s_why, "reseau indisponible");
            free(buf);
            if (!verified && !s_why[0]) snprintf(s_why, sizeof s_why, "certificat non verifie");
            __atomic_store_n(&s_state, 2, __ATOMIC_RELEASE);
        }
    }

    // v95 - see Nextendo.hpp. Defensive: the status line and the headers are read only inside what came, bounded.
    int Call(const char *method, const char *path, const char *headers, const char *body, char *buf, int cap,
             char **bodyOut, int *bodyLen, int *retryAfter, char *why, int whyCap) {
        *bodyOut = nullptr; *bodyLen = 0; *retryAfter = 0;
        if (!buf || cap < 64) return -1;
        Into d = { buf, cap, 0 };
        const char *accept = "application/json";
        const int r = Https(kHost, path, true, accept, IntoSink, &d, why, whyCap, method, nullptr, headers, body);
        buf[d.n] = 0;
        if (r < 0 || d.n < 12 || strncmp(buf, "HTTP/1.", 7)) return -1;
        const int status = atoi(buf + 9);
        char *h = strstr(buf, "\r\n\r\n");
        if (!h) return status > 0 ? status : -1;
        *h = 0;
        bool chunked = false;
        for (char *line = strstr(buf, "\r\n"); line && line < h; line = strstr(line + 2, "\r\n")) {
            const char *v = line + 2;
            if (!strncasecmp(v, "retry-after:", 12)) { const long s = strtol(v + 12, nullptr, 10); *retryAfter = s < 1 ? 1 : (s > 3600 ? 3600 : (int)s); }
            if (!strncasecmp(v, "transfer-encoding:", 18) && strstr(v, "chunked") && strstr(v, "chunked") < h) chunked = true;
        }
        char *b = h + 4;
        int bl = d.n - (int)(b - buf);
        if (bl < 0) bl = 0;
        if (chunked) bl = Dechunk(b, bl); else b[bl] = 0;
        *bodyOut = b; *bodyLen = bl;
        return status;
    }

    void Shutdown() { if (s_threadOn) { threadWaitForExit(&s_thread); threadClose(&s_thread); s_threadOn = false; } }
    void RequestNow() { s_force = true; }
    bool Busy() { return __atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != 0; }

    void Update(float now, bool menuOnScreen) {
        if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) == 2) {        // the worker is done: its table becomes ours
            if (s_threadOn) { threadWaitForExit(&s_thread); threadClose(&s_thread); s_threadOn = false; }
            if (s_task >= 0) { tasks::End(s_task, now, s_ncount > 0 || !s_why[0], s_ncount > 0 ? nullptr : s_why); s_task = -1; }   // v77
            if (s_ncount > 0 && Settings::s_nextendo) {   // (v72.1: a fetch made only for the clock shows no counts)
                memcpy(s_id, s_nid, sizeof(u64) * s_ncount); memcpy(s_n, s_nn, sizeof(u16) * s_ncount);
                s_count = s_ncount;
            }
            if (s_ndate > 0) ntp::FromDate(s_ndate, s_ndateTick, now);   // v72.1: the console's clock, from this answer
            else ntp::NoDate(s_why, now);
            s_ndate = 0;
            if (s_log) {
                int on = 0; for (int i = 0; i < s_count; i++) on += s_n[i] > 0;
                char m[160]; snprintf(m, sizeof m, "nextendo : %d jeux, %d avec des joueurs%s%s", s_ncount, on, s_why[0] ? " - " : "", s_why);   // lang:log (never drawn)
                s_log(m);
            }
            __atomic_store_n(&s_state, 0, __ATOMIC_RELEASE);
        }
        const bool clock = ntp::Wanted();                // v72.1: the clock waits for a Date (the lock screen too)
        if (!Settings::s_nextendo && !clock) { s_force = false; return; }
        if (!s_force && ((!menuOnScreen && !clock) || now - s_lastAt < kEvery)) return;
        if (__atomic_load_n(&s_state, __ATOMIC_ACQUIRE) != 0 || qext_game_running() || covers::Busy() || social::NetBusy() || spotify::Busy()) return;   // (v75: one at a time; v95: the friends too; v98: the Spotify cover)
        s_force = false;
        NifmInternetConnectionType t; u32 st = 0; NifmInternetConnectionStatus cs;
        if (R_FAILED(nifmGetInternetConnectionStatus(&t, &st, &cs)) || cs != NifmInternetConnectionStatus_Connected) { s_lastAt = now - kEvery + 30.0f; return; }
        s_lastAt = now;
        s_task = tasks::Begin(now, "Joueurs en ligne (Nextendo)");   // v77: the task centre
        __atomic_store_n(&s_state, 1, __ATOMIC_RELEASE);
        s_threadOn = R_SUCCEEDED(threadCreate(&s_thread, Work, nullptr, nullptr, 0x10000, 0x2E, -2));
        if (s_threadOn && R_FAILED(threadStart(&s_thread))) { threadClose(&s_thread); s_threadOn = false; }
        if (!s_threadOn) __atomic_store_n(&s_state, 0, __ATOMIC_RELEASE);
    }

    int Players(u64 titleId) {
        for (int i = 0; i < s_count; i++) if (s_id[i] == titleId) return s_n[i];
        return -1;
    }

} // namespace brume::nextendo
