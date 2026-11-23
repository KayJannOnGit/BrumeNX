// Brume - v82: Réglages > Son > Audio Bluetooth. See BtAudio.hpp.
#include "BtAudio.hpp"
#include "Audio.hpp"
#include "Ease.hpp"
#include "Haptics.hpp"
#include "Lang.hpp"
#include "Notify.hpp"
#include "Popup.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <switch.h>

namespace brume::btaudio {

    namespace {
        constexpr int kDevMax = kRowsMax - 1;            // the search row + 15 devices
        constexpr int kFoundMax = 15;                    // the most GetDiscoveredAudioDevice gives (libnx: « the max is 15 »)
        constexpr float kSearchLen = 30.0f;              // the search stops by itself after 30 s
        constexpr float kConnectLen = 25.0f, kOffLen = 8.0f;
        constexpr float kPollFound = 0.5f, kPollBusy = 0.25f, kPollLists = 2.0f, kPing = 2.4f;
        enum St : uint8_t { kConnected = 0, kPaired = 1, kFound = 2 };
        struct Dev { uint8_t addr[6]; char name[128]; uint8_t st; bool speaker; };
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        bool Same(const uint8_t *a, const uint8_t *b) { return !memcmp(a, b, 6); }
        bool Empty(const uint8_t *a) { static const uint8_t z[6] = {}; return !memcmp(a, z, 6); }

        // ================================================================ the console's side
        namespace hw {
#ifndef BRUME_UISIM
            // btm:sys (FW 13.0.0+). Its two events say « the connected ones changed » and « the paired ones changed »
            Event s_conn, s_pair;
            bool s_connOk = false, s_pairOk = false;
            BtdrvAddress Addr(const uint8_t *a) { BtdrvAddress x; memcpy(x.address, a, 6); return x; }
            Result Init(float) {
                if (!hosversionAtLeast(13, 0, 0)) return MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
                const Result rc = btmsysInitialize();
                if (R_FAILED(rc)) return rc;
                s_connOk = R_SUCCEEDED(btmsysAcquireAudioDeviceConnectionEvent(&s_conn));
                s_pairOk = R_SUCCEEDED(btmsysAcquirePairedAudioDeviceInfoChangedEvent(&s_pair));
                return 0;
            }
            void Exit() {
                if (s_connOk) eventClose(&s_conn);
                if (s_pairOk) eventClose(&s_pair);
                s_connOk = s_pairOk = false;
                btmsysExit();
            }
            void Tick(float) {}
            bool ConnChanged() { return s_connOk && R_SUCCEEDED(eventWait(&s_conn, 0)); }   // (autoclear: once per change)
            bool PairChanged() { return s_pairOk && R_SUCCEEDED(eventWait(&s_pair, 0)); }
            Result Paired(BtmAudioDevice *o, s32 max, s32 *n) { return btmsysGetPairedAudioDevices(o, max, n); }
            Result Connected(BtmAudioDevice *o, s32 max, s32 *n) { return btmsysGetConnectedAudioDevices(o, max, n); }
            Result Found(BtmAudioDevice *o, s32 max, s32 *n) { return btmsysGetDiscoveredAudioDevice(o, max, n); }
            Result StartSearch() { return btmsysStartAudioDeviceDiscovery(); }
            void   StopSearch() { btmsysStopAudioDeviceDiscovery(); }
            bool   Searching() { bool b = false; return R_FAILED(btmsysIsDiscoveryingAudioDevice(&b)) || b; }   // (no answer: the timeout decides)
            Result Connect(const uint8_t *a) { return btmsysConnectAudioDevice(Addr(a)); }   // a device found by the search: paired too
            bool   Connecting() { bool b = false; return R_SUCCEEDED(btmsysIsConnectingAudioDevice(&b)) && b; }
            Result Disconnect(const uint8_t *a) { return btmsysDisconnectAudioDevice(Addr(a)); }
            Result Forget(const uint8_t *a) { return btmsysRemoveAudioDevicePairing(Addr(a)); }
            bool   RadioOn() { bool b = true; return R_FAILED(btmsysGetRadioOnOff(&b)) || b; }
#else
            // the PC preview (uisim): two paired devices (one connected), a search that finds three more, a connection that takes
            // 1.4 s; « MAJOR IV » does not answer the first time (the error popup can be seen)
            struct Fake { uint8_t a[6]; const char *name; bool paired, connected; float foundAt; };
            Fake s_f[5] = { { { 0x38, 0x18, 0x4C, 0x10, 0xA2, 0x01 }, "WH-1000XM4", true, true, -1 },
                            { { 0xF4, 0x4E, 0xFD, 0x20, 0xB3, 0x02 }, "JBL Flip 5", true, false, 2.4f },
                            { { 0xAC, 0x90, 0x85, 0x30, 0xC4, 0x03 }, "AirPods Pro", false, false, 1.6f },
                            { { 0x2C, 0x41, 0xA1, 0x40, 0xD5, 0x04 }, "MAJOR IV", false, false, 3.2f },
                            { { 0x00, 0x0C, 0x8A, 0x50, 0xE6, 0x05 }, "SoundLink Mini", false, false, 5.0f } };
            float s_now = 0, s_searchT0 = -1, s_opT0 = 0;
            int s_op = 0, s_opI = -1, s_refused = 0;     // op: 1 connecting, 2 disconnecting
            bool s_ev = false, s_stopped = true;
            int Find(const uint8_t *a) { for (int i = 0; i < 5; i++) if (Same(s_f[i].a, a)) return i; return -1; }
            Result Init(float now) { s_now = now; return 0; }
            void Exit() { s_op = 0; s_stopped = true; }
            void Tick(float now) {
                s_now = now;
                if (s_op && now - s_opT0 >= (s_op == 1 ? 1.4f : 0.5f)) {
                    Fake &f = s_f[s_opI];
                    if (s_op == 2) f.connected = false;
                    else if (strcmp(f.name, "MAJOR IV") || s_refused++) { for (Fake &o : s_f) o.connected = false; f.connected = f.paired = true; }
                    s_op = 0; s_ev = true;
                }
            }
            bool ConnChanged() { const bool e = s_ev; s_ev = false; return e; }
            bool PairChanged() { return false; }
            Result List(BtmAudioDevice *o, s32 max, s32 *n, int which) {
                *n = 0;
                for (const Fake &f : s_f) {
                    const bool in = which == 0 ? f.paired : which == 1 ? f.connected
                                  : (!f.paired && f.foundAt >= 0 && s_searchT0 >= 0 && s_now - s_searchT0 >= f.foundAt);
                    if (!in || *n >= max) continue;
                    memcpy(o[*n].addr.address, f.a, 6);
                    snprintf(o[*n].name, sizeof o[*n].name, "%s", f.name);
                    (*n)++;
                }
                return 0;
            }
            Result Paired(BtmAudioDevice *o, s32 max, s32 *n) { return List(o, max, n, 0); }
            Result Connected(BtmAudioDevice *o, s32 max, s32 *n) { return List(o, max, n, 1); }
            Result Found(BtmAudioDevice *o, s32 max, s32 *n) { return List(o, max, n, 2); }
            Result StartSearch() { s_searchT0 = s_now; s_stopped = false; return 0; }
            void   StopSearch() { s_stopped = true; }
            bool   Searching() { return !s_stopped && s_now - s_searchT0 < 14.0f; }
            Result Connect(const uint8_t *a) { const int i = Find(a); if (i < 0) return MAKERESULT(Module_Libnx, LibnxError_NotFound); s_op = 1; s_opI = i; s_opT0 = s_now; return 0; }
            bool   Connecting() { return s_op == 1; }
            Result Disconnect(const uint8_t *a) { const int i = Find(a); if (i < 0) return MAKERESULT(Module_Libnx, LibnxError_NotFound); s_op = 2; s_opI = i; s_opT0 = s_now; return 0; }
            Result Forget(const uint8_t *a) {
                const int i = Find(a);
                if (i < 0) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
                s_f[i].paired = s_f[i].connected = false; s_ev = true;
                if (s_f[i].foundAt < 0) s_f[i].foundAt = 2.0f;   // (a new search finds it again)
                return 0;
            }
            bool   RadioOn() { return true; }
#endif
        }

        // ================================================================ the page's state
        bool s_open = false, s_ok = false, s_radio = true;
        Result s_initRc = 0;
        Dev s_dev[kDevMax]; int s_n = 0;                 // the rows' devices (their names stay after Close: the column fades out)
        Dev s_fd[kFoundMax]; int s_nfd = 0;              // found by the search (kept until the page leaves or a new search)
        uint8_t s_conn[8][6]; int s_nconn = 0;           // the connected ones as last seen (who came, who left)
        uint32_t s_gen = 1;                              // +1 when the list changes (Track)
        BtmAudioDevice s_buf[kFoundMax];                 // the console's answers (main thread, one list at a time)
        Dev s_nd[kDevMax];                               // (Rebuild's new list)
        float s_last = -1, s_listAt = 0;
        bool s_search = false;
        float s_searchT0 = -100, s_pollAt = 0, s_pingAt = 0, s_wave = 0, s_waveV = 0;
        int s_found = 0, s_ping = 0, s_task = -1;
        void EndTask(float now, bool ok, const char *why = nullptr) { tasks::End(s_task, now, ok, why); s_task = -1; }   // v86: never end a stale id
        enum Op { kNone = 0, kConnect, kDisconnect, kSwitchOff };   // kSwitchOff: the connected one leaves, then s_next comes
        int s_op = kNone, s_idle = 0;
        float s_opT0 = 0, s_opPoll = 0;
        Dev s_opDev, s_next, s_ask, s_askFrom;           // the device an action / a popup is about
        uint8_t s_quiet[6] = {}; float s_quietUntil = -1;   // just forgotten: its « déconnecté » is not said on top
        int s_trackRow = 0; uint32_t s_trackGen = 0; uint8_t s_trackAddr[6] = {}; bool s_trackHas = false;
        char s_searchVal[48] = "";
        float s_led[kRowsMax] = {}, s_ledV[kRowsMax] = {};

        // a name from the console: 0xF9 bytes at most, maybe not terminated, maybe not UTF-8. Kept: valid UTF-8, no control
        // character, 28 letters at most (then « … »), no spaces around; nothing left: « Appareil AB:CD »
        void CleanName(const char *src, int srcCap, const uint8_t *addr, char *out, int cap) {
            int o = 0, i = 0, letters = 0;
            bool cut = false;
            while (i < srcCap && src[i]) {
                const uint8_t c = (uint8_t)src[i];
                const int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
                bool good = len > 0 && i + len <= srcCap && !(len == 2 && c < 0xC2);
                for (int k = 1; good && k < len; k++) good = ((uint8_t)src[i + k] & 0xC0) == 0x80;
                if (letters >= 28 || o + (good ? len : 1) + 4 > cap) { cut = true; break; }
                if (!good) { out[o++] = '?'; i++; }
                else if (len == 1 && (c < 0x20 || c == 0x7F)) { out[o++] = ' '; i++; }
                else { memcpy(out + o, src + i, (size_t)len); o += len; i += len; }
                letters++;
            }
            while (o > 0 && out[o - 1] == ' ') o--;
            int s = 0;
            while (s < o && out[s] == ' ') s++;
            if (s) { memmove(out, out + s, (size_t)(o - s)); o -= s; }
            if (cut && o > 0) { memcpy(out + o, "\xe2\x80\xa6", 3); o += 3; }
            out[o] = 0;
            if (!o) snprintf(out, (size_t)cap, lang::T("Appareil %02X:%02X"), addr[4], addr[5]);
        }
        // the icon and the word (« Enceinte » / « Casque »): a speaker by its name, else headphones
        bool LooksSpeaker(const char *n) {
            static const char *const kW[] = { "speaker", "enceinte", "soundlink", "sonos", "soundbar", "boom", "flip", "charge",
                                              "xtreme", "pulse", "stanmore", "acton", "emberton", "lautsprecher", "altavoz", "partybox" };
            char low[128];
            int k = 0;
            for (; n[k] && k < 127; k++) low[k] = (n[k] >= 'A' && n[k] <= 'Z') ? (char)(n[k] + 32) : n[k];
            low[k] = 0;
            for (const char *w : kW) if (strstr(low, w)) return true;
            return false;
        }
        void Fill(Dev &d, const BtmAudioDevice &b, uint8_t st) {
            memcpy(d.addr, b.addr.address, 6);
            CleanName(b.name, (int)sizeof b.name, d.addr, d.name, (int)sizeof d.name);
            d.st = st; d.speaker = LooksSpeaker(d.name);
        }
        int FindDev(const Dev *list, int n, const uint8_t *a) { for (int i = 0; i < n; i++) if (Same(list[i].addr, a)) return i; return -1; }
        bool IsConnected(const uint8_t *a) { for (int i = 0; i < s_nconn; i++) if (Same(s_conn[i], a)) return true; return false; }
        bool Busy(const Dev &d) { return s_op != kNone && (Same(d.addr, s_opDev.addr) || (s_op == kSwitchOff && Same(d.addr, s_next.addr))); }
        bool Leaving(const Dev &d) { return s_op == kDisconnect || (s_op == kSwitchOff && Same(d.addr, s_opDev.addr)); }

        // one of the console's lists into s_buf: -1 if it did not answer, else how many (bounded: never more than asked)
        int Fetch(int which, int max) {
            if (max > kFoundMax) max = kFoundMax;
            memset(s_buf, 0, sizeof s_buf);
            s32 n = 0;
            const Result rc = which == 0 ? hw::Paired(s_buf, max, &n) : which == 1 ? hw::Connected(s_buf, max, &n) : hw::Found(s_buf, max, &n);
            if (R_FAILED(rc)) return -1;
            return n < 0 ? 0 : (n > max ? max : n);
        }

        // someone came / left (a headset switched on reconnects by itself: it is said too). Its words, then a few notes in the chord
        void Said(float now, const Dev &d, bool on) {
            if (!on && s_quietUntil > now && Same(d.addr, s_quiet)) return;
            char m[200];
            if (on) snprintf(m, sizeof m, d.speaker ? lang::T("Enceinte connectée : %s") : lang::T("Casque connecté : %s"), d.name);
            else snprintf(m, sizeof m, d.speaker ? lang::T("Enceinte déconnectée : %s") : lang::T("Casque déconnecté : %s"), d.name);
            notify::Post(now, m, on ? notify::kDone : notify::kInfo);
            if (on) { for (int k = 0; k < 3; k++) audio::Bell(audio::HarmonyNote(k, 74, 88), 0.026f, true, 0.14f + k * 0.08f); haptic::Tap(0.5f); }
            else { audio::Bell(audio::HarmonyNote(3, 67, 79), 0.022f, true, 0.12f); audio::Bell(audio::HarmonyNote(1, 60, 72), 0.02f, true, 0.22f); }
        }

        // the rows again: the connected one, the paired ones, then the ones the search found. notify: say who came / left
        void Rebuild(float now, bool notify) {
            int n = 0, nconn = 0;
            uint8_t conn[8][6] = {};
            const int c = Fetch(1, 8);
            for (int i = 0; i < c && n < kDevMax; i++) {
                const uint8_t *a = s_buf[i].addr.address;
                if (Empty(a) || FindDev(s_nd, n, a) >= 0) continue;
                Fill(s_nd[n++], s_buf[i], kConnected);
                if (nconn < 8) memcpy(conn[nconn++], a, 6);
            }
            const int p = Fetch(0, 10);
            for (int i = 0; i < p && n < kDevMax; i++) {
                const uint8_t *a = s_buf[i].addr.address;
                if (Empty(a) || FindDev(s_nd, n, a) >= 0) continue;
                Fill(s_nd[n++], s_buf[i], kPaired);
            }
            for (int i = 0; i < s_nfd && n < kDevMax; i++)
                if (FindDev(s_nd, n, s_fd[i].addr) < 0) { s_nd[n] = s_fd[i]; s_nd[n].st = kFound; n++; }
            if (c >= 0) {                                // (no answer: nothing is said, the old list stays the reference)
                if (notify) {
                    for (int i = 0; i < nconn; i++) {
                        bool was = false;
                        for (int k = 0; k < s_nconn; k++) was = was || Same(conn[i], s_conn[k]);
                        const int at = FindDev(s_nd, n, conn[i]);
                        if (!was && at >= 0) Said(now, s_nd[at], true);
                    }
                    for (int k = 0; k < s_nconn; k++) {
                        bool is = false;
                        for (int i = 0; i < nconn; i++) is = is || Same(conn[i], s_conn[k]);
                        const int at = FindDev(s_dev, s_n, s_conn[k]);
                        if (!is && at >= 0) Said(now, s_dev[at], false);
                    }
                }
                memcpy(s_conn, conn, sizeof conn); s_nconn = nconn;
            }
            bool changed = n != s_n;
            for (int i = 0; i < n && !changed; i++)
                changed = !Same(s_nd[i].addr, s_dev[i].addr) || s_nd[i].st != s_dev[i].st || strcmp(s_nd[i].name, s_dev[i].name);
            if (changed) { memcpy(s_dev, s_nd, sizeof(Dev) * (size_t)n); s_n = n; s_gen++; }
        }

        // ---- the search
        void EndSearch(float now, bool quiet) {
            if (!s_search) return;
            hw::StopSearch();
            s_search = false;
            EndTask(now, true);
            if (quiet) return;
            if (!s_found) notify::Post(now, "Aucun appareil trouvé : mets-le en mode appairage, tout près de la console");
            audio::Bell(audio::HarmonyNote(2, 67, 79), 0.022f, true);
        }
        void StartSearch(float now) {
            if (!s_radio) s_radio = hw::RadioOn();
            if (!s_radio) { popup::Error(now, "Bluetooth coupé", "Le Bluetooth de la console est coupé (mode avion ?).", 0); return; }
            const Result rc = hw::StartSearch();
            if (R_FAILED(rc)) { popup::Error(now, "Recherche impossible", "Le Bluetooth de la console n'a pas répondu.", rc); return; }
            s_search = true; s_searchT0 = now; s_pollAt = now + kPollFound; s_pingAt = now + 0.2f; s_found = 0; s_ping = 0;
            s_nfd = 0;
            Rebuild(now, false);
            s_task = tasks::Begin(now, "Audio Bluetooth");
            tasks::Progress(s_task, now, 0, 0, lang::T("recherche…"));
            audio::PlaySweep(audio::Whoosh, -0.4f, 0.4f, 0.6f, 0.3f);
            audio::Bell(audio::HarmonyNote(0, 74, 86), 0.026f, true);
            haptic::Thump(0.4f);
        }
        void PollFound(float now) {
            const int n = Fetch(2, kFoundMax);
            int added = 0;
            for (int i = 0; i < n && s_nfd < kFoundMax; i++) {
                const uint8_t *a = s_buf[i].addr.address;
                if (Empty(a) || FindDev(s_fd, s_nfd, a) >= 0) continue;
                Fill(s_fd[s_nfd++], s_buf[i], kFound);
                const int k = FindDev(s_dev, s_n, a);
                if (k >= 0 && s_dev[k].st != kFound) continue;   // (already paired: already in the list)
                // a new one arrives: a crystal note of the chord (the next one each time), on the list's side
                audio::Crystal(0.35f, 0.32f, audio::HarmonyNote(s_found % 4, 84, 96));
                haptic::Tick(0.4f);
                s_found++; added++;
            }
            if (!added) return;
            tasks::Alive(s_task, now);
            char item[96];
            snprintf(item, sizeof item, lang::T("recherche… %d trouvé(s)"), s_found);
            tasks::Progress(s_task, now, s_found, 0, item);
            Rebuild(now, false);
        }

        // ---- a connection, a disconnection, a switch (one audio device at a time)
        void ConnectFailed(float now, Result rc) {
            s_op = kNone;
            EndTask(now, false, "l'appareil n'a pas répondu");
            popup::Error(now, "Connexion impossible", "Allume l'appareil et mets-le en mode appairage, tout près de la console, puis réessaie.", rc);
        }
        void Connect(const Dev &d, float now) {
            EndSearch(now, true);                        // (the search and a connection share the radio)
            const Result rc = hw::Connect(d.addr);
            if (R_FAILED(rc)) { ConnectFailed(now, rc); return; }
            s_op = kConnect; s_opDev = d; s_opT0 = now; s_opPoll = now + kPollBusy; s_idle = 0;
            s_task = tasks::Begin(now, "Audio Bluetooth");
            char item[160];
            snprintf(item, sizeof item, lang::T("connexion : %s"), d.name);
            tasks::Progress(s_task, now, 0, 0, item);
            audio::Bell(audio::HarmonyNote(1, 74, 86), 0.022f, true, 0.06f);
        }
        void OffFailed(float now, Result rc) {
            s_op = kNone;
            popup::Error(now, "Déconnexion impossible", "La console n'a pas pu le déconnecter.", rc);
        }
        void Disconnect(const Dev &d, float now) {
            const Result rc = hw::Disconnect(d.addr);
            if (R_FAILED(rc)) { OffFailed(now, rc); return; }
            s_op = kDisconnect; s_opDev = d; s_opT0 = now; s_opPoll = now + kPollBusy;
            audio::Bell(audio::HarmonyNote(3, 67, 79), 0.018f, true);
        }
        void DoSwitch(float now) {
            if (!s_open || !s_ok || s_op != kNone) return;
            const Result rc = hw::Disconnect(s_askFrom.addr);
            if (R_FAILED(rc)) { OffFailed(now, rc); return; }
            s_op = kSwitchOff; s_opDev = s_askFrom; s_next = s_ask; s_opT0 = now; s_opPoll = now + kPollBusy;
        }
        void AskSwitch(const Dev &from, const Dev &to, float now) {
            s_askFrom = from; s_ask = to;
            char txt[220];
            snprintf(txt, sizeof txt, lang::T("%s sera déconnecté : un seul appareil audio à la fois."), from.name);
            popup::Spec s;
            s.kind = popup::kAsk; s.title = "Changer d'appareil ?"; s.subject = to.name; s.text = txt;
            s.yes = "Connecter"; s.no = "Annuler";
            s.onYes = [](void *, float t) { DoSwitch(t); };
            popup::Show(now, s);
        }
        void DoForget(float now) {
            if (!s_open || !s_ok || s_op != kNone) return;
            if (IsConnected(s_ask.addr)) hw::Disconnect(s_ask.addr);   // (forgetting a connected one lets it go first)
            const Result rc = hw::Forget(s_ask.addr);
            if (R_FAILED(rc)) { popup::Error(now, "L'appareil n'a pas pu être oublié", "La console a refusé d'effacer son appairage.", rc); return; }
            for (int i = 0; i < s_nfd; i++) if (Same(s_fd[i].addr, s_ask.addr)) { s_fd[i] = s_fd[--s_nfd]; break; }
            memcpy(s_quiet, s_ask.addr, 6); s_quietUntil = now + 6.0f;
            char m[200];
            snprintf(m, sizeof m, lang::T("Appareil oublié : %s"), s_ask.name);
            notify::Post(now, m, notify::kDone);
            Rebuild(now, true);
        }
        // followed 4 times a second: done, failed, or the next step (a switch: the old one gone, the new one now)
        void StepOp(float now) {
            const float t = now - s_opT0;
            if (s_op == kConnect) {
                if (IsConnected(s_opDev.addr)) { s_op = kNone; EndTask(now, true); return; }   // (Rebuild said it)
                // no longer connecting for a whole second (after a moment to start), or too long: a last look, then the answer
                s_idle = t > 1.0f && !hw::Connecting() ? s_idle + 1 : 0;
                if (s_idle >= 4 || t > kConnectLen) {
                    Rebuild(now, true);
                    if (IsConnected(s_opDev.addr)) { s_op = kNone; EndTask(now, true); }
                    else ConnectFailed(now, 0);
                }
                return;
            }
            if (!IsConnected(s_opDev.addr)) {            // the one leaving is gone
                if (s_op == kSwitchOff) { s_op = kNone; const Dev next = s_next; Connect(next, now); }
                else s_op = kNone;
                return;
            }
            if (t > kOffLen) OffFailed(now, 0);
            else if (t > 0.5f) Rebuild(now, true);       // (in case the event was missed)
        }
    }

    // ================================================================ the page
    void Open(float now) {
        if (s_open) return;
        s_open = true; s_last = -1; s_n = 0; s_nfd = 0; s_nconn = 0; s_search = false; s_op = kNone; s_wave = s_waveV = 0;
        s_trackHas = false; s_gen++;
        for (int i = 0; i < kRowsMax; i++) s_led[i] = s_ledV[i] = 0;
        const Result rc = hw::Init(now);
        s_ok = R_SUCCEEDED(rc); s_initRc = rc;
        if (!s_ok) {
            popup::Error(now, "Bluetooth indisponible", rc == MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer) ? "Il faut le système 13.0.0 ou plus récent."
                                                                                                              : "Le service Bluetooth de la console n'a pas répondu.", rc);
            return;
        }
        s_radio = hw::RadioOn();
        Rebuild(now, false);
        for (int i = 0; i < s_n && i + 1 < kRowsMax; i++) if (s_dev[i].st == kConnected) s_led[i + 1] = 1;   // (already lit)
        s_listAt = now + kPollLists;
    }

    void Close(float now) {
        if (!s_open) return;
        if (s_search) { hw::StopSearch(); s_search = false; EndTask(now, true); }
        if (s_op == kConnect || s_op == kSwitchOff) EndTask(now, true);   // (the console finishes it alone; no longer watched)
        s_op = kNone;
        if (s_ok) hw::Exit();
        s_open = s_ok = false; s_n = 0; s_nfd = 0; s_nconn = 0; s_wave = 0; s_gen++;
    }

    bool IsOpen() { return s_open; }

    void Update(float now) {
        if (!s_open) return;
        const float dt = s_last < 0 ? 0.016f : fminf(0.05f, fmaxf(0.0f, now - s_last));
        s_last = now;
        Glide(s_wave, s_waveV, s_search ? 1.0f : 0.0f, dt, 5.0f);
        for (int i = 0; i < kRowsMax; i++) {
            const bool lit = i >= 1 && i - 1 < s_n && s_dev[i - 1].st == kConnected && !Busy(s_dev[i - 1]);
            Glide(s_led[i], s_ledV[i], lit ? 1.0f : 0.0f, dt, 8.0f);
        }
        if (!s_ok) return;
        hw::Tick(now);
        bool again = hw::ConnChanged();
        again = hw::PairChanged() || again;
        if (now >= s_listAt) again = true;               // (every 2 s anyway: an event may be missed)
        if (again) { s_listAt = now + kPollLists; Rebuild(now, true); }
        if (s_search) {
            if (now >= s_pollAt) {
                s_pollAt = now + kPollFound;
                PollFound(now);
                if (now - s_searchT0 > 1.5f && !hw::Searching()) EndSearch(now, false);   // it ended by itself
            }
            if (s_search && now - s_searchT0 >= kSearchLen) EndSearch(now, false);
            if (s_search && now >= s_pingAt) {           // the sonar: a soft, high note of the chord, from one side to the other
                s_pingAt = now + kPing;
                audio::Bell(audio::HarmonyNote(s_ping % 4, 79, 91), 0.010f, true);
                s_ping++;
            }
        }
        if (s_op != kNone && now >= s_opPoll) { s_opPoll = now + kPollBusy; StepOp(now); }
    }

    int Rows() { return 1 + s_n; }

    Row RowAt(int i) {
        if (i <= 0) {
            if (!s_ok) return { "radar", "Rechercher un appareil", "Le Bluetooth de la console ne répond pas", "Indisponible" };
            if (!s_radio) return { "radar", "Rechercher un appareil", "Le Bluetooth de la console est coupé", "Bluetooth coupé" };
            if (s_search) {
                const int left = (int)ceilf(fmaxf(0.0f, kSearchLen - (s_last - s_searchT0)));
                snprintf(s_searchVal, sizeof s_searchVal, lang::T("Recherche… %d s"), left);
                return { "radar", "Rechercher un appareil", "Recherche des appareils proches… A pour arrêter", s_searchVal };
            }
            return { "radar", "Rechercher un appareil", s_n ? "Le casque ou l'enceinte en mode appairage" : "Aucun appareil appairé : mets le tien en mode appairage", "" };
        }
        if (i - 1 >= s_n) return { "headphones", "", nullptr, "" };
        const Dev &d = s_dev[i - 1];
        const char *ico = d.speaker ? "speaker" : "headphones";
        if (Busy(d)) return { ico, d.name, Leaving(d) ? "Déconnexion en cours" : "Connexion en cours, patiente quelques secondes", Leaving(d) ? "Déconnexion…" : "Connexion…" };
        if (d.st == kConnected) return { ico, d.name, "2 manettes sans fil au plus pendant la connexion", "Connecté" };
        if (d.st == kPaired) return { ico, d.name, "A pour le connecter, X pour l'oublier", "Appairé" };
        return { ico, d.name, "A pour l'appairer et le connecter", "Trouvé" };
    }

    int Track(int sel) {
        const int rows = Rows();
        if (s_trackGen != s_gen) {                       // the list changed: the chosen row goes where its device went
            s_trackGen = s_gen;
            if (s_trackHas) for (int i = 0; i < s_n; i++) if (Same(s_dev[i].addr, s_trackAddr)) { sel = i + 1; break; }
        }
        sel = sel < 0 ? 0 : (sel >= rows ? rows - 1 : sel);
        s_trackRow = sel;
        s_trackHas = sel > 0 && sel - 1 < s_n;
        if (s_trackHas) memcpy(s_trackAddr, s_dev[sel - 1].addr, 6);
        return sel;
    }

    void PressA(int row, float now) {
        if (!s_open) return;
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Tap(0.6f);
        if (!s_ok) {
            popup::Error(now, "Bluetooth indisponible", "Le service Bluetooth de la console n'a pas répondu.", (unsigned)s_initRc);
            return;
        }
        if (s_op != kNone) { audio::Thock(0, 0.8f, 0.05f); notify::Post(now, "Patiente : une connexion est en cours"); return; }
        if (row <= 0) { if (s_search) EndSearch(now, false); else StartSearch(now); return; }
        if (row - 1 >= s_n) return;
        const Dev d = s_dev[row - 1];                    // (a copy: the list may change under it)
        if (d.st == kConnected) { Disconnect(d, now); return; }
        for (int i = 0; i < s_n; i++)                    // one audio device at a time: another one is connected - ask first
            if (s_dev[i].st == kConnected && !Same(s_dev[i].addr, d.addr)) { AskSwitch(s_dev[i], d, now); return; }
        Connect(d, now);
    }

    bool CanForget(int row) { return s_open && s_ok && s_op == kNone && row >= 1 && row - 1 < s_n && s_dev[row - 1].st != kFound; }

    void PressX(float now) {
        if (!s_open) return;
        if (s_op != kNone) { audio::Thock(0, 0.8f, 0.05f); notify::Post(now, "Patiente : une connexion est en cours"); return; }
        if (!CanForget(s_trackRow)) { audio::Thock(0, 0.8f, 0.05f); return; }
        s_ask = s_dev[s_trackRow - 1];
        audio::Play(audio::Tick2400, 0, 0.8f);
        haptic::Tap(0.5f);
        popup::Spec s;
        s.kind = popup::kDanger; s.title = "Oublier cet appareil ?"; s.subject = s_ask.name;
        s.text = "Pour t'en resservir, il faudra l'appairer à nouveau.";
        s.yes = "Oublier"; s.no = "Annuler";
        s.onYes = [](void *, float t) { DoForget(t); };
        popup::Show(now, s);
    }

    const char *HintA(int row) {
        if (s_op != kNone) return "Patiente";            // (a connection under way: A waits)
        if (row <= 0) return !s_ok ? "Choisir" : (s_search ? "Arrêter" : "Rechercher");
        if (row - 1 >= s_n) return "Choisir";
        const uint8_t st = s_dev[row - 1].st;
        return st == kConnected ? "Déconnecter" : (st == kPaired ? "Connecter" : "Appairer");
    }

    void DrawRow(Ui &ui, int row, float x, float y, float a, float now) {
        if (!s_open || a <= 0.003f) return;
        if (row == 0) {                                  // the search: rings of light leave the radar, one per ping; a halo breathes
            const float w = Clamp01(s_wave);
            if (w <= 0.003f) return;
            ui.Light(x - 58, y - 58, x + 58, y + 58, ps2::A(ps2::kHalo, 0.30f * a * w * (0.75f + 0.25f * sinf(now * 2.6f))), true);
            for (int k = 0; k < 2; k++) {                // (a soft wave of light, and its faint edge: never a hard line)
                const float p = fmodf(fmaxf(0.0f, now - s_searchT0) / kPing + k * 0.5f, 1.0f);
                const float r = 22 + 70 * EaseGlide(p), al = a * w * (1 - p) * (1 - p);
                if (al <= 0.003f) continue;
                ui.Light(x - r * 1.25f, y - r * 1.25f, x + r * 1.25f, y + r * 1.25f, ps2::A(ps2::kHalo, 0.16f * al), true);
                ui.Shape(x - r - 2, y - r - 2, x + r + 2, y + r + 2, -r - 2, -r - 2, r + 2, r + 2, ps2::A(ps2::kChoice, 0.30f * al), 16, r);
            }
            return;
        }
        if (row - 1 >= s_n) return;
        const Dev &d = s_dev[row - 1];
        if (Busy(d)) {                                   // a connection under way: the icon's light breathes
            const float b = 0.5f + 0.5f * sinf((now - s_opT0) * 4.2f);
            ui.Light(x - 46, y - 46, x + 46, y + 46, ps2::A(ps2::kHalo, (0.16f + 0.24f * b) * a), true);
        }
        const float l = row < kRowsMax ? Clamp01(s_led[row]) : 0.0f;
        if (l > 0.003f) {                                // connected: a small green light at the icon's corner (it comes softly)
            const float cx = x + 24, cy = y + 19, s = 3.0f * (0.6f + 0.4f * l);
            ui.Light(cx - 15, cy - 15, cx + 15, cy + 15, ps2::A(ps2::kGreen, 0.5f * a * l), true);
            ui.Shape(cx - s, cy - s, cx + s, cy + s, 0, 0, 2 * s, 2 * s, ps2::A(ps2::kGreen, a * l), 24, 2 * s, 2 * s, s);
        }
    }

} // namespace brume::btaudio
