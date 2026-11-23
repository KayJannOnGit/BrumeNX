// Brume - v39: the Manettes page. See Pads.hpp.
// Colours: hidGetNpadControllerColorSingle / Split give the body (« main ») and the buttons (« sub ») as RGBA8 (red in
// the low byte). Battery: hidGetNpadPowerInfoSingle / Split (level 0..4). Live state: hidGetNpadStates<style> (shared
// memory: no request to the system, cheap). Vibration: the controller's own vibration devices, a soft heartbeat.
#include "Pads.hpp"
#include "Lang.hpp"
#include "Wheel.hpp"
#include "Settings.hpp"
#include "Ease.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Mesh3d.hpp"
#include "Notify.hpp"
#include "Worker.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace brume {

    void (*Pads::s_pair)(float) = nullptr;

    namespace {
        const HidNpadIdType kIds[9] = { HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4, HidNpadIdType_No5,
                                        HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8, HidNpadIdType_Handheld };
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        // a controller colour (RGBA8, red in the low byte) as an interface colour; k darkens, w mixes with white
        Rgba Col(u32 c, float a, float k = 1.0f, float w = 0.0f) {
            float r = (c & 0xFF) / 255.0f, g = ((c >> 8) & 0xFF) / 255.0f, b = ((c >> 16) & 0xFF) / 255.0f;
            r = (r * k) * (1 - w) + w; g = (g * k) * (1 - w) + w; b = (b * k) * (1 - w) + w;
            return Rgba{ r, g, b, a };
        }
        void RR(Ui &ui, float x0, float y0, float x1, float y1, float r, Rgba c) {
            const float w = x1 - x0, h = y1 - y0;
            if (w <= 0 || h <= 0 || c.a <= 0.002f) return;
            ui.Shape(x0, y0, x1, y1, 0, 0, w, h, c, 24, w, h, fminf(r, fminf(w, h) * 0.5f));
        }
        // v41.1 - which Joy-Con a pad really has. The handheld npad's attribute bits do NOT say it (Yass: the 3D Switch had
        // its Joy-Con always off): the system's own answers do. First the HOME menu's footer icon type (what Nintendo's
        // menu draws), then the device type (HandheldLeft / Right = on the rails), then the attribute bits; connected and
        // nothing said = both.
        void Sides(HidNpadIdType id, u32 attr, bool handheld, bool pair, bool &l, bool &r) {
            l = r = false;
            const HidAppletFooterUiType ft = hidGetAppletFooterUiTypes(id);
            const u32 dt = hidGetNpadDeviceType(id);
            if (handheld && ft >= HidAppletFooterUiType_HandheldNone && ft <= HidAppletFooterUiType_HandheldJoyConLeftJoyConRight) {
                l = ft == HidAppletFooterUiType_HandheldJoyConLeftOnly || ft == HidAppletFooterUiType_HandheldJoyConLeftJoyConRight;
                r = ft == HidAppletFooterUiType_HandheldJoyConRightOnly || ft == HidAppletFooterUiType_HandheldJoyConLeftJoyConRight;
                return;
            }
            if (pair && ft >= HidAppletFooterUiType_JoyDual && ft <= HidAppletFooterUiType_JoyDualRightOnly) {
                l = ft != HidAppletFooterUiType_JoyDualRightOnly;
                r = ft != HidAppletFooterUiType_JoyDualLeftOnly;
                return;
            }
            if (handheld) {
                l = (dt & (HidDeviceTypeBits_HandheldLeft | HidDeviceTypeBits_HandheldLarkHvcLeft | HidDeviceTypeBits_HandheldLarkNesLeft)) != 0;
                r = (dt & (HidDeviceTypeBits_HandheldRight | HidDeviceTypeBits_HandheldLarkHvcRight | HidDeviceTypeBits_HandheldLarkNesRight)) != 0;
            } else if (pair) {
                l = (dt & (HidDeviceTypeBits_JoyLeft | HidDeviceTypeBits_LarkHvcLeft | HidDeviceTypeBits_LarkNesLeft)) != 0;
                r = (dt & (HidDeviceTypeBits_JoyRight | HidDeviceTypeBits_LarkHvcRight | HidDeviceTypeBits_LarkNesRight)) != 0;
            }
            if (!l && !r) {
                l = (attr & (HidNpadAttribute_IsLeftConnected | HidNpadAttribute_IsLeftWired)) != 0;
                r = (attr & (HidNpadAttribute_IsRightConnected | HidNpadAttribute_IsRightWired)) != 0;
            }
            if (!l && !r && (attr & HidNpadAttribute_IsConnected)) l = r = true;
        }
        // a small log of what the system says about the controllers (sdmc:/brume/manettes.log), only when it changes,
        // written 2 s later (never during the click), 300 lines at most
        char  s_padLog[2048];
        int   s_padLogLen = 0, s_padLogLines = 0;
        float s_padLogAt = -1;
        char  s_padLast[256];
        // v99 (perf): the files are written by a short thread (Worker.hpp), never in the frame - perf.log: « lent :
        // Pads::Update 25.7 ms » (the log appended to on the SD card). One file at a time; busy: tried again next frame.
        struct FileJob { char path[48]; char buf[2048]; int len; bool append; };
        FileJob s_fj;
        Worker  s_fw;
        void WriteJob(void *) {
            if (FILE *fp = fopen(s_fj.path, s_fj.append ? "a" : "w")) { fwrite(s_fj.buf, 1, (size_t)s_fj.len, fp); fclose(fp); }
        }
        bool WriteLater(const char *path, const char *data, int len, bool append) {
            s_fw.Done();
            if (!s_fw.Idle() || len < 0 || len > (int)sizeof s_fj.buf) return false;
            snprintf(s_fj.path, sizeof s_fj.path, "%s", path);
            memcpy(s_fj.buf, data, (size_t)len); s_fj.len = len; s_fj.append = append;
            return s_fw.Start(WriteJob, nullptr, 0x8000);
        }
        void Dot(Ui &ui, float cx, float cy, float r, Rgba c) { RR(ui, cx - r, cy - r, cx + r, cy + r, r, c); }
        void Glow(Ui &ui, float cx, float cy, float rx, float ry, Rgba c) { if (c.a > 0.002f) ui.Shape(cx - rx, cy - ry, cx + rx, cy + ry, -1, -1, 1, 1, c, 21); }
    }

    bool Pads::Visible(const Wheel &w) const { return w.Active() && fabsf(w.PageAnim() - kPage) < 1.0f; }

    bool Pads::Read(HidNpadIdType id, u32 style, HidNpadCommonState &st) {
        memset(&st, 0, sizeof st);
        size_t n = 0;
        if (style & HidNpadStyleTag_NpadHandheld)      n = hidGetNpadStatesHandheld(id, &st, 1);
        else if (style & HidNpadStyleTag_NpadJoyDual)  n = hidGetNpadStatesJoyDual(id, &st, 1);
        else if (style & HidNpadStyleTag_NpadFullKey)  n = hidGetNpadStatesFullKey(id, &st, 1);
        else if (style & HidNpadStyleTag_NpadJoyLeft)  n = hidGetNpadStatesJoyLeft(id, &st, 1);
        else if (style & HidNpadStyleTag_NpadJoyRight) n = hidGetNpadStatesJoyRight(id, &st, 1);
        else if (style & HidNpadStyleTag_NpadGc)       n = hidGetNpadStatesFullKey(id, &st, 1);
        // v41: a pair (or the console) with only ONE Joy-Con left is still there (Yass: one off and everything vanished)
        return n > 0 && ((st.attributes & (HidNpadAttribute_IsConnected | HidNpadAttribute_IsLeftConnected | HidNpadAttribute_IsRightConnected))
                         || hidGetNpadDeviceType(id) != 0);   // v41.1: or the system says a device is there
    }

    void Pads::Name(const Pad &p, char *out, int cap) const {
        switch (p.kind) {
            // v92: as the Switch names them in French (G = gauche, D = droit); English: (L) / (R)
            case kPair:     snprintf(out, cap, "%s", lang::T(p.hasL && p.hasR ? "Joy-Con (G+D)" : (p.hasL ? "Joy-Con (G)" : "Joy-Con (D)"))); break;
            case kLeft:     snprintf(out, cap, "%s", lang::T("Joy-Con (G)")); break;
            case kRight:    snprintf(out, cap, "%s", lang::T("Joy-Con (D)")); break;
            case kPro:      snprintf(out, cap, lang::T("Manette Pro")); break;
            case kHandheld: snprintf(out, cap, "%s", p.hasL && p.hasR ? lang::T("Mode portable") : lang::T(p.hasL ? "Console + Joy-Con (G)" : (p.hasR ? "Console + Joy-Con (D)" : "Console"))); break;
            case kGc:       snprintf(out, cap, lang::T("Manette GameCube")); break;
            default:        snprintf(out, cap, lang::T("Manette")); break;
        }
    }

    // which controllers are there: their kind, colours, battery; a new one says hello
    void Pads::Scan(float now, bool hello) {
        Pad found[9];
        int n = 0;
        u32 mask = 0;
        char sig[256]; int sl = 0;
        sig[0] = 0;
        for (int k = 0; k < 9; k++) {
            const HidNpadIdType id = kIds[k];
            const u32 style = hidGetNpadStyleSet(id);
            if (!style) continue;
            HidNpadCommonState st;
            if (!Read(id, style, st)) continue;
            Pad &p = found[n];
            memset(&p, 0, sizeof p);
            p.id = id; p.style = style;
            p.kind = (style & HidNpadStyleTag_NpadHandheld) ? kHandheld : (style & HidNpadStyleTag_NpadJoyDual) ? kPair :
                     (style & HidNpadStyleTag_NpadFullKey) ? kPro : (style & HidNpadStyleTag_NpadJoyLeft) ? kLeft :
                     (style & HidNpadStyleTag_NpadJoyRight) ? kRight : (style & HidNpadStyleTag_NpadGc) ? kGc : kOther;
            Sides(id, (u32)st.attributes, p.kind == kHandheld, p.kind == kPair, p.hasL, p.hasR);
            if (sl < (int)sizeof sig - 64)
                sl += snprintf(sig + sl, sizeof sig - sl, " [%s style=%x attr=%x type=%x footer=%d L%d R%d]",
                               id == HidNpadIdType_Handheld ? "console" : "J", (unsigned)style, (unsigned)st.attributes,
                               (unsigned)hidGetNpadDeviceType(id), (int)hidGetAppletFooterUiTypes(id), p.hasL, p.hasR);
            const bool split = p.kind == kPair || p.kind == kHandheld;
            bool colOk;
            if (split) {
                colOk = R_SUCCEEDED(hidGetNpadControllerColorSplit(id, &p.l, &p.r));
                if (!colOk) { p.l.main = p.r.main = 0x828282; p.l.sub = p.r.sub = 0x0f0f0f; }
                hidGetNpadPowerInfoSplit(id, &p.pl, &p.pr);
            } else {
                colOk = R_SUCCEEDED(hidGetNpadControllerColorSingle(id, &p.l));
                if (!colOk) { p.l.main = p.kind == kGc ? 0xc84f5a : 0x323232; p.l.sub = 0x1e1e1e; }
                p.r = p.l;
                hidGetNpadPowerInfoSingle(id, &p.pl);
                p.pr = p.pl;
            }
            mask |= 1u << k;
            if (p.kind == kPair || p.kind == kHandheld || p.kind == kLeft || p.kind == kRight) {   // v40: the Switch wears them
                // v94: remembered only from a side that is really there (the console answers something for an empty rail, a
                // pair for its missing half: never that Joy-Con's colour - the grey Yass saw), and a missing side wears the last
                // colours seen of it (MemApply gives them to the 3D Switch)
                const bool hasL = p.kind == kLeft || (split && p.hasL), hasR = p.kind == kRight || (split && p.hasR);
                const HidNpadControllerColor &cr = split ? p.r : p.l;
                if (colOk && hasL && (p.l.main | p.l.sub) & 0xFFFFFF) MemNote(0, p.l.main, p.l.sub, now);
                if (colOk && hasR && (cr.main | cr.sub) & 0xFFFFFF) MemNote(1, cr.main, cr.sub, now);
                if (split) {
                    if ((!hasL || !colOk) && m_memOk[0]) { p.l.main = m_mem[0][0]; p.l.sub = m_mem[0][1]; }
                    if ((!hasR || !colOk) && m_memOk[1]) { p.r.main = m_mem[1][0]; p.r.sub = m_mem[1][1]; }
                }
            }
            m_ghost[k] = p;                               // v92: (its card keeps this look while it fades away)
            n++;
        }
        // v92: the new list; the one shown on the page stays the same controller (found by its player), what changed is said
        Pad old[9];
        const int oldN = m_n;
        memcpy(old, m_pad, sizeof(Pad) * oldN);
        const bool hadSel = m_n > 0;
        const HidNpadIdType selId = hadSel ? m_pad[m_sel < m_n ? m_sel : 0].id : HidNpadIdType_No1;
        memcpy(m_pad, found, sizeof(Pad) * n);
        m_n = n; m_mask = mask;
        if (hadSel) {
            int to = -1;
            for (int i = 0; i < m_n; i++) if (m_pad[i].id == selId) to = i;
            if (to >= 0) m_sel = to;
            else { m_sel = m_sel < m_n ? m_sel : (m_n ? m_n - 1 : 0); m_moveAt = now; }   // (it left: the next one arrives, softly)
        }
        if (hello) Announce(old, oldN, now);
        if (strcmp(sig, s_padLast) != 0 && s_padLogLines < 300) {   // what the system says changed: noted (written 2 s later)
            memcpy(s_padLast, sig, sizeof s_padLast);
            const int k = snprintf(s_padLog + s_padLogLen, sizeof s_padLog - s_padLogLen, "%7.2f s :%s\n", now, sig[0] ? sig : " (aucune manette)");   // lang:log (never drawn)
            if (k > 0 && s_padLogLen + k < (int)sizeof s_padLog) { s_padLogLen += k; s_padLogLines++; }
            s_padLogAt = now + 2.0f;
        }
        if (s_padLogLen && s_padLogAt >= 0 && now >= s_padLogAt) {
            if (WriteLater("sdmc:/brume/manettes.log", s_padLog, s_padLogLen, true)) { s_padLogLen = 0; s_padLogAt = -1; }   // (v99: off the frame)
        }
        if (m_sel >= m_n) m_sel = m_n ? m_n - 1 : 0;
    }

    // ---------------------------------------------------------------- v92: live detection, the menu's buttons
    namespace {
        int  NpadIndex(HidNpadIdType id) { return id == HidNpadIdType_Handheld ? 8 : ((int)id & 7); }
    }

    // what the system says about the 9 npads, in one number: read from the shared memory (no request to the system), a few
    // microseconds - so a Joy-Con that slides on or off, a controller that wakes up, is seen the very frame it happens
    u32 Pads::Signature() const {
        u32 h = 2166136261u;
        auto mix = [&h](u32 v) { h = (h ^ v) * 16777619u; };
        for (int k = 0; k < 9; k++) {
            const HidNpadIdType id = kIds[k];
            const u32 style = hidGetNpadStyleSet(id);
            mix(style);
            if (!style) continue;
            mix(hidGetNpadDeviceType(id));
            mix((u32)hidGetAppletFooterUiTypes(id));
            HidNpadCommonState st;
            mix(Read(id, style, st) ? (u32)st.attributes : 0xFFFFFFFFu);
        }
        return h;
    }

    // ---------------------------------------------------------------- v94: the Joy-Con's colours, remembered
    namespace {
        constexpr const char *kMemPath = "sdmc:/brume/manettes.ini";
        constexpr u32 kNeutral = 0x828282;                 // (the grey Joy-Con: only when a side was never seen)
        u32 Rgb(u32 c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }   // RGBA8 (red low) <-> RRGGBB (red high)
        void Lin(u32 c, float *o) { for (int q = 0; q < 3; q++) o[q] = powf(((c >> (8 * q)) & 0xFF) / 255.0f, 2.2f); }
        // « gauche=RRGGBB RRGGBB » -> body, buttons (RGBA8); false on anything else (a damaged file is only ignored)
        bool ParseCol(const char *v, u32 &body, u32 &btn) {
            u32 c[2];
            for (int i = 0; i < 2; i++) {
                while (*v == ' ' || *v == '\t') v++;
                u32 x = 0; int d = 0;
                for (; d < 6; d++, v++) {
                    const char ch = *v;
                    const int h = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
                    if (h < 0) return false;
                    x = x << 4 | (u32)h;
                }
                if (*v && *v != ' ' && *v != '\t' && *v != '\r' && *v != '\n') return false;
                c[i] = Rgb(x);
            }
            body = c[0]; btn = c[1];
            return true;
        }
    }

    void Pads::MemLoad() {
        m_memLoaded = true;
        FILE *fp = fopen(kMemPath, "r");
        if (!fp) return;
        char line[96];
        for (int n = 0; n < 16 && fgets(line, sizeof line, fp); n++) {
            const int s = strncmp(line, "gauche=", 7) == 0 ? 0 : strncmp(line, "droite=", 7) == 0 ? 1 : -1;
            u32 body, btn;
            if (s >= 0 && ParseCol(line + 7, body, btn)) { m_mem[s][0] = body; m_mem[s][1] = btn; m_memOk[s] = true; }
        }
        fclose(fp);
        MemApply();
    }

    void Pads::MemSave() {                               // (v99: written by the files' thread, never in the frame)
        char b[256];
        int n = snprintf(b, sizeof b, "# Brume : les derni\xc3\xa8res couleurs vues des Joy-Con (corps, boutons), en RRGGBB\n");   // lang:log (never drawn)
        for (int s = 0; s < 2; s++)
            if (m_memOk[s] && n > 0 && n < (int)sizeof b) n += snprintf(b + n, sizeof b - n, "%s=%06X %06X\n", s ? "droite" : "gauche", (unsigned)Rgb(m_mem[s][0]), (unsigned)Rgb(m_mem[s][1]));
        if (n <= 0 || n >= (int)sizeof b) { m_memSaveAt = -1; return; }
        if (WriteLater(kMemPath, b, n, false)) m_memSaveAt = -1;   // (busy: again next frame)
    }
    void Pads::Shutdown() { s_fw.Wait(); }               // v99: the files' thread ends with the menu

    void Pads::MemNote(int s, u32 body, u32 btn, float now) {
        body &= 0xFFFFFF; btn &= 0xFFFFFF;
        if (m_memOk[s] && m_mem[s][0] == body && m_mem[s][1] == btn) return;
        m_mem[s][0] = body; m_mem[s][1] = btn; m_memOk[s] = true;
        m_memSaveAt = now + 2.0f;                          // (written 2 s later: never during the click)
        MemApply();
    }

    void Pads::MemApply() {
        Lin(m_memOk[0] ? m_mem[0][0] : kNeutral, Settings::s_joyL);
        Lin(m_memOk[1] ? m_mem[1][0] : kNeutral, Settings::s_joyR);
        Settings::s_joyKnown = m_memOk[0] || m_memOk[1];   // (nothing ever seen: the model's own colours)
    }

    // ---------------------------------------------------------------- v94: the rail's light
    void Pads::Rail(int s, float now, float k, bool voice) {
        m_railAt[s] = now; m_railK[s] = k;
        m_railCol[s][0] = m_memOk[s] ? m_mem[s][0] : kNeutral; m_railCol[s][1] = m_memOk[s] ? m_mem[s][1] : 0xFFFFFF;
        static float lastVoice = -100;
        if (voice && now - lastVoice > 0.25f) {            // (nothing else said it: the light's own voice, a soft rising shimmer)
            lastVoice = now;
            for (int q = 0; q < 3; q++) audio::Bell(audio::HarmonyNote(q + 1, 74 + 3 * q, 86 + 3 * q), 0.016f - 0.003f * q, true, 0.06f + 0.09f * q);
        }
    }

    namespace {
        // a controller colour made into light: as bright as light goes, a little white in it; a near-black one takes the accent
        Rgba LightOf(u32 c, float w) {
            float r = (c & 0xFF) / 255.0f, g = ((c >> 8) & 0xFF) / 255.0f, b = ((c >> 16) & 0xFF) / 255.0f;
            const float m = fmaxf(r, fmaxf(g, b));
            if (m < 0.16f) { const Rgba a = Settings::Accent(1.0f); r = a.r; g = a.g; b = a.b; }
            else { r /= m; g /= m; b /= m; }
            return Rgba{ r + (1 - r) * w, g + (1 - g) * w, b + (1 - b) * w, 1 };
        }
        Rgba WithA(Rgba c, float a) { c.a = a; return c; }
    }

    // the light along one screen edge: a run up the edge in the body's colour (its head hot and white, its tail in the colour),
    // a thinner one in the buttons' colour just after, the edge left glowing behind them, a soft bloom, motes drifting inwards
    void Pads::DrawRail(Ui &ui, float now) {
        constexpr float kDur = 1.5f, kRun = 0.7f;
        for (int s = 0; s < 2; s++) {
            const float t = now - m_railAt[s];
            if (t < 0 || t >= kDur) continue;
            const float p = t / kDur, K = m_railK[s];
            const float ex = s ? 1280.0f : 0.0f, dir = s ? -1.0f : 1.0f;
            const Rgba cb = LightOf(m_railCol[s][0], 0.15f), cs = LightOf(m_railCol[s][1], 0.25f);
            auto band = [&](float x0, float x1, float y0, float y1, Rgba c, bool glow) {   // (x0..x1 measured from the edge, inwards)
                const float a = ex + dir * x0, b = ex + dir * x1;
                ui.Light(fminf(a, b), y0, fmaxf(a, b), y1, c, glow);
            };
            // the bloom: it grows from the edge as the light climbs, then fades
            const float bloom = Clamp01((p - 0.08f) / 0.3f) * powf(1 - p, 1.5f);
            band(-260, 200 + 90 * EaseGlide(p), 360 - 470, 360 + 470, WithA(cb, 0.46f * K * bloom), true);
            band(-160, 110 + 60 * EaseGlide(p), 360 - 280, 360 + 280, WithA(cs, 0.20f * K * bloom), true);
            // two runs: the body's colour, then the buttons' (thinner, a little after)
            for (int r = 0; r < 2; r++) {
                const float d = r ? 0.12f : 0.0f, q = Clamp01((p - d) / kRun);
                if (q <= 0 || q >= 1) continue;
                const float y = 790 - 880 * EaseGlide(q);                    // from below the screen to above it
                const float hk = Clamp01(q * 8) * (1 - Clamp01((q - 0.72f) / 0.28f));
                const Rgba c = r ? cs : cb;
                const float wd = r ? 0.6f : 1.0f;
                ui.Streak(ex + dir * 3, y, ex + dir * 3, fminf(760.0f, y + 380 * wd), 12 * wd, WithA(c, 0.60f * K * hk));  // its tail
                ui.Streak(ex + dir * 2, y, ex + dir * 2, fminf(760.0f, y + 150 * wd), 4 * wd, WithA(ps2::kChosen, 0.70f * K * hk));
                band(-90 * wd, 110 * wd, y - 150 * wd, y + 150 * wd, WithA(c, 0.55f * K * hk), true);                        // the head's light
                band(-16, 22 * wd, y - 40 * wd, y + 40 * wd, WithA(ps2::Mix(c, ps2::kChosen, 0.6f), 0.85f * K * hk), true);   // its hot core
                if (!r) band(-10, 34, y, 760, WithA(c, 0.26f * K * hk), false);                                              // the edge lit behind it
            }
            // the edge keeps a breath of light, then lets it go
            const float keep = Clamp01((p - 0.3f) / 0.2f) * (1 - Clamp01((p - 0.55f) / 0.45f));
            band(-12, 30, -20, 740, WithA(cb, 0.28f * K * keep), false);
            // motes: each leaves the edge when the light passes it, drifts in and up, and fades
            for (int i = 0; i < 12; i++) {
                const float ti = 0.04f + i * 0.036f, life = (p - ti) / 0.55f;
                if (life <= 0 || life >= 1) continue;
                const float h1 = (float)((i * 37) % 11) / 10.0f, h2 = (float)((i * 53) % 7) / 6.0f;
                const float y0 = 790 - 880 * EaseGlide(Clamp01(ti / kRun));
                const float e = EaseGlide(life), x = 8 + (30 + 110 * h1) * e, y = y0 - (20 + 60 * h2) * e;
                const float rr = 3 + 4 * h2, a = (1 - life) * Clamp01(life * 6) * K;
                band(x - rr * 3, x + rr * 3, y - rr * 3, y + rr * 3, WithA(i & 1 ? cs : cb, 0.40f * a), true);
                band(x - rr, x + rr, y - rr, y + rr, WithA(ps2::kChosen, 0.55f * a), true);
            }
        }
    }

    // one gesture, one message (PS4 notifications), a soft sound in the chord: the Joy-Con put on / taken off the rails, a
    // controller that comes (it says hello: the room pulses in its colour, it vibrates) or leaves (said in Update: a Joy-Con a
    // moment later, in case it is only sliding onto a rail)
    void Pads::Announce(const Pad *old, int oldN, float now) {
        auto IsJoy = [](Kind k) { return k == kPair || k == kLeft || k == kRight; };   // (Joy-Con off the rails)
        auto find = [](const Pad *a, int n, HidNpadIdType id) -> const Pad * { for (int i = 0; i < n; i++) if (a[i].id == id) return &a[i]; return nullptr; };
        bool r0[2] = { false, false }, r1[2] = { false, false };
        if (const Pad *h = find(old, oldN, HidNpadIdType_Handheld)) { r0[0] = h->hasL; r0[1] = h->hasR; }
        if (const Pad *h = find(m_pad, m_n, HidNpadIdType_Handheld)) { r1[0] = h->hasL; r1[1] = h->hasR; }
        const int on = (r1[0] && !r0[0] ? 1 : 0) | (r1[1] && !r0[1] ? 2 : 0), off = (r0[0] && !r1[0] ? 1 : 0) | (r0[1] && !r1[1] ? 2 : 0);
        if (on) {                                          // on the rails: the console takes them (handheld)
            m_attachAt = now;
            for (int s = 0; s < 2; s++) if (on >> s & 1) Rail(s, now, 1.0f, false);   // v94: its edge lights up (its click says it)
            for (int k = 0; k < 8; k++) m_offAt[k] = 0;   // (the pair that just went: it is here, on the console)
            notify::Post(now, on == 3 ? "Joy-Con (G+D) fix\xc3\xa9s \xc3\xa0 la console" : on == 1 ? "Joy-Con (G) fix\xc3\xa9 \xc3\xa0 la console"
                                                                                      : "Joy-Con (D) fix\xc3\xa9 \xc3\xa0 la console", notify::kDone, false);
            if (!m_pageOn) {                               // (the page's 3D Switch clicks by itself)
                audio::Thock(on == 1 ? -0.4f : on == 2 ? 0.4f : 0.0f, 0.85f, 0.05f);
                audio::Bell(audio::HarmonyNote(0, 55, 67), 0.026f, true, 0.03f);
                audio::Bell(audio::HarmonyNote(2, 62, 74), 0.020f, true, 0.12f);
                haptic::Tick(0.5f);
            }
        }
        if (off) {                                         // off the rails
            m_detachAt = now;
            notify::Post(now, off == 3 ? "Joy-Con (G+D) d\xc3\xa9tach\xc3\xa9s de la console" : off == 1 ? "Joy-Con (G) d\xc3\xa9tach\xc3\xa9 de la console"
                                                                                         : "Joy-Con (D) d\xc3\xa9tach\xc3\xa9 de la console", notify::kInfo, false);
            if (!m_pageOn) {
                audio::Thock(off == 1 ? -0.4f : off == 2 ? 0.4f : 0.0f, 0.95f, 0.04f);
                audio::Bell(audio::HarmonyNote(1, 67, 79), 0.022f, true, 0.02f);
                haptic::Tick(0.4f);
            }
        }
        // who left (first: a controller whose kind changed on the same player - a pair that lost a side - is said by nobody)
        for (int i = 0; i < oldN; i++) {
            const Pad &p = old[i];
            if (p.kind == kHandheld) continue;
            const Pad *is = find(m_pad, m_n, p.id);
            if (is && is->kind == p.kind) continue;
            const int k = NpadIndex(p.id);
            if (IsJoy(p.kind) && now - m_attachAt < 1.5f) continue;   // it slid onto a rail (already said)
            Name(p, m_offName[k], sizeof m_offName[k]);
            m_offAt[k] = now + (IsJoy(p.kind) ? 0.8f : 0.001f);
        }
        // who came
        for (int i = 0; i < m_n; i++) {
            const Pad &p = m_pad[i];
            if (p.kind == kHandheld) continue;
            const Pad *was = find(old, oldN, p.id);
            if (was && was->kind == p.kind) continue;
            const int k = NpadIndex(p.id);
            if (m_offAt[k] > 0) { m_offAt[k] = 0; continue; }       // it left and came back before it was said (or changed kind)
            m_bornAt[k] = now;
            if (IsJoy(p.kind) && now - m_detachAt < 4.0f) continue; // the Joy-Con that just left the rails (already said)
            m_helloAt = now;                                         // hello: the room pulses in its colour, a chord, it vibrates
            const u32 c1 = p.l.main, c2 = p.r.main;
            m_helloCol[0] = ((c1 & 0xFF) + (c2 & 0xFF)) / 510.0f; m_helloCol[1] = (((c1 >> 8) & 0xFF) + ((c2 >> 8) & 0xFF)) / 510.0f;
            m_helloCol[2] = (((c1 >> 16) & 0xFF) + ((c2 >> 16) & 0xFF)) / 510.0f;
            char nm[48], msg[96];
            Name(p, nm, sizeof nm);
            snprintf(msg, sizeof msg, lang::T("Manette connect\xc3\xa9" "e : %s"), nm);
            notify::Post(now, msg, notify::kDone, false);
            audio::Bell(audio::HarmonyNote(0, 67, 79), 0.04f, true, 0.0f);
            audio::Bell(audio::HarmonyNote(2, 67, 79), 0.032f, true, 0.13f);
            audio::Bell(audio::HarmonyNote(1, 74, 86), 0.024f, true, 0.27f);
            Vibrate(p, now);
            if (m_sel != i) { m_sel = i; m_moveAt = now; }          // (on the page, the new one is shown)
        }
        // v94: a Joy-Con that connects wirelessly (a new one, the one just taken off its rail, one waking up, the missing half of
        // a pair) lights its edge too, softer; its voice only when no hello said it
        bool w0[2] = { false, false }, w1[2] = { false, false };
        auto wireless = [](const Pad *a, int n, bool *o) {
            for (int i = 0; i < n; i++) {
                if (a[i].kind == kPair) { o[0] |= a[i].hasL; o[1] |= a[i].hasR; }
                else if (a[i].kind == kLeft) o[0] = true;
                else if (a[i].kind == kRight) o[1] = true;
            }
        };
        wireless(old, oldN, w0); wireless(m_pad, m_n, w1);
        for (int s = 0; s < 2; s++) if (w1[s] && !w0[s] && !r1[s]) Rail(s, now, 0.7f, m_helloAt != now);
    }

    void Pads::Poll(PadState &pad) {
        pad.buttons_old = pad.buttons_cur;
        pad.buttons_cur = 0;
        pad.active_id_mask = 0; pad.active_handheld = false;
        pad.style_set = 0; pad.attributes = 0;
        pad.sticks[0] = pad.sticks[1] = HidAnalogStickState{ 0, 0 };
        pad.gc_triggers[0] = pad.gc_triggers[1] = 0;
        int mag[2] = { 0, 0 };
        for (int k = 0; k < 9; k++) {
            const HidNpadIdType id = kIds[k];
            const u32 style = hidGetNpadStyleSet(id);
            if (!style) continue;
            HidNpadCommonState st;
            if (!Read(id, style, st)) continue;            // (a console with ONE Joy-Con on its rail counts: Read says so)
            if (id == HidNpadIdType_Handheld) pad.active_handheld = true;
            else pad.active_id_mask |= (u8)(1u << k);
            pad.style_set |= style;
            pad.attributes |= (u32)st.attributes;
            pad.buttons_cur |= st.buttons;
            for (int s = 0; s < 2; s++) {                  // the stick pushed the furthest wins (two hands, two controllers)
                const HidAnalogStickState &a = s ? st.analog_stick_r : st.analog_stick_l;
                const int m = (a.x < 0 ? -a.x : a.x) + (a.y < 0 ? -a.y : a.y);
                if (m > mag[s]) { mag[s] = m; pad.sticks[s] = a; }
            }
        }
    }

    void Pads::Vibrate(const Pad &p, float now) {
        m_vibAt = now;
        if (!haptic::Enabled()) { m_vibOn = false; return; }
        const bool two = p.kind == kPair || p.kind == kHandheld;
        const u32 tag = p.kind == kPair ? HidNpadStyleTag_NpadJoyDual : p.kind == kHandheld ? HidNpadStyleTag_NpadHandheld :
                        p.kind == kLeft ? HidNpadStyleTag_NpadJoyLeft : p.kind == kRight ? HidNpadStyleTag_NpadJoyRight : HidNpadStyleTag_NpadFullKey;
        m_nvh = two ? 2 : 1;
        m_vibOn = R_SUCCEEDED(hidInitializeVibrationDevices(m_vh, m_nvh, p.id, (HidNpadStyleTag)tag));
    }

    void Pads::Move(int step, float now) {
        if (!m_n) return;
        const int to = m_sel + step;
        if (to < 0 || to >= m_n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
        m_sel = to; m_moveAt = now;
        audio::Play(audio::Deplacement, step * 0.3f, 1.0f);
        haptic::Tick(0.6f);
    }

    void Pads::PressA(float now) {
        if (!m_n) return;
        Vibrate(m_pad[m_sel], now);
        audio::Bell(audio::HarmonyNote(0, 67, 79), 0.035f, true, 0.0f);
        audio::Bell(audio::HarmonyNote(2, 67, 79), 0.028f, true, 0.12f);
        audio::Thock(0.0f, 0.8f, 0.05f);
    }

    void Pads::PressY(float now) {
        audio::Play(audio::Tick2400);
        if (s_pair) s_pair(now);
    }

    void Pads::Tap(float x, float y, float now) {
        // the selector at the top (several controllers), else the controller itself
        if (m_n > 1 && y >= 92 && y <= 148) {               // v92: where each card is right now (they glide when one comes / goes)
            for (int i = 0; i < m_n; i++) {
                const int k = NpadIndex(m_pad[i].id);
                if (m_cardW[k] > 1 && x >= m_cardX[k] && x <= m_cardX[k] + m_cardW[k]) { if (i != m_sel) Move(i - m_sel, now); return; }
            }
        }
        if (fabsf(x - 640) < 330 && y > 160 && y < 520) PressA(now);
    }

    void Pads::Update(const Wheel &w, float now, float dt) {
        m_now = now;
        if (!m_memLoaded) MemLoad();                     // v94: the Joy-Con's colours, from the very first frame (the lock screen)
        if (m_memSaveAt >= 0 && now >= m_memSaveAt) MemSave();
        s_none = m_n == 0;
        if (!w.Active()) {                               // locked: no hello, and the room's colour goes back to normal
            if (m_t.open) TestStop();                    // v84: (never over the lock screen)
            Settings::s_cover = 0;
            m_first = true;
            for (float &t : m_offAt) t = 0;              // (v92: nothing said late about before the lock)
            Glide(m_tintW, m_tintV, 0.0f, dt, 4.0f);
            Settings::s_tintW = m_tintW;
            return;
        }
        // v92: the fingerprint every frame (a few microseconds), the full look when it changes - attaching is seen at once -
        // and twice a second anyway (the battery, the charge)
        {
            const bool first = m_first;
            const u32 sig = Signature();
            if (first || sig != m_sig || now >= m_pollAt) {
                m_sig = sig; m_pollAt = now + 0.5f;
                m_pageOn = ModelVisible(w);
                Scan(now, !first); m_first = false;
            }
            for (int k = 0; k < 9; k++) {                // each card's spring (the selector): it grows in, it fades out
                const float to = (m_mask >> k) & 1 ? 1.0f : 0.0f;
                if (first) { m_show[k] = to; m_showV[k] = 0; }
                else Glide(m_show[k], m_showV[k], to, dt, 7.0f);
                if (m_offAt[k] > 0 && now >= m_offAt[k]) {   // a controller that left, said now (a soft fall in the chord)
                    m_offAt[k] = 0;
                    char msg[96];
                    snprintf(msg, sizeof msg, lang::T("Manette d\xc3\xa9" "connect\xc3\xa9" "e : %s"), m_offName[k]);
                    notify::Post(now, msg, notify::kInfo, false);
                    audio::Bell(audio::HarmonyNote(3, 67, 79), 0.022f, true, 0.0f);
                    audio::Bell(audio::HarmonyNote(1, 60, 72), 0.020f, true, 0.11f);
                    haptic::Tick(0.3f);
                }
            }
        }
        {   // each Joy-Con on the console's rail or not (the handheld npad's two sides)
            bool on[2] = { false, false };
            for (int i = 0; i < m_n; i++) if (m_pad[i].kind == kHandheld) { on[0] = m_pad[i].hasL; on[1] = m_pad[i].hasR; }
            for (int s = 0; s < 2; s++) m_attach[s] = on[s];
            // v94 - and each one there at all (on its rail or wirelessly): one that is neither for 3 s falls asleep on the page;
            // coming back (detected again, or slid onto its rail) it wakes up
            bool here[2] = { on[0], on[1] };
            for (int i = 0; i < m_n; i++) {
                const Pad &p = m_pad[i];
                if (p.kind == kPair) { here[0] |= p.hasL; here[1] |= p.hasR; }
                else if (p.kind == kLeft) here[0] = true;
                else if (p.kind == kRight) here[1] = true;
            }
            for (int s = 0; s < 2; s++) {
                if (!m_hereInit) { m_here[s] = here[s]; m_goneAt[s] = here[s] ? -100 : now - 100; m_lost[s] = here[s] ? 0.0f : 1.0f; m_lostV[s] = 0; continue; }
                if (here[s] && !m_here[s] && m_lost[s] > 0.3f) m_wakeAt[s] = now;
                if (!here[s] && m_here[s]) m_goneAt[s] = now;
                m_here[s] = here[s];
                Glide(m_lost[s], m_lostV[s], !here[s] && now - m_goneAt[s] > 3.0f ? 1.0f : 0.0f, dt, here[s] ? 4.0f : 1.6f);   // (falls asleep slowly, wakes quickly)
            }
            m_hereInit = true;
        }
        // v84: the test screen (only while it is open; its cover glides the settings away and back)
        Settings::s_cover = m_t.open ? EaseGlide((now - m_t.openAt) / Pace(0.4f)) : 1 - EaseGlide((now - m_t.closeAt - Pace(0.12f)) / Pace(0.5f));
        if (m_t.open) TestUpdate(now, dt);
        // the chosen one, live (its buttons light up, its sticks move)
        if (m_n && Visible(w)) {
            const Pad &p = m_pad[m_sel];
            HidNpadCommonState st;
            if (Read(p.id, p.style, st)) {
                m_prevBtn = m_btn; m_btn = st.buttons;
                for (int b = 0; b < 32; b++) if ((m_btn >> b & 1) && !(m_prevBtn >> b & 1)) m_btnAt[b] = now;
                const float t[4] = { st.analog_stick_l.x / 32767.0f, st.analog_stick_l.y / 32767.0f, st.analog_stick_r.x / 32767.0f, st.analog_stick_r.y / 32767.0f };
                for (int k = 0; k < 4; k++) m_stk[k] += (fmaxf(-1.0f, fminf(1.0f, t[k])) - m_stk[k]) * (1 - expf(-dt * 30.0f));
            }
        }
        // its vibration: a soft heartbeat (two beats), then quiet
        if (m_vibOn) {
            const float t = now - m_vibAt;
            const float amp = t < 0.07f ? 0.42f : t < 0.18f ? 0.0f : t < 0.25f ? 0.26f : 0.0f;
            HidVibrationValue v[2];
            for (int k = 0; k < 2; k++) { v[k].amp_low = amp; v[k].freq_low = 160.0f; v[k].amp_high = amp * 0.6f; v[k].freq_high = 320.0f; }
            hidSendVibrationValues(m_vh, v, m_nvh);
            if (t >= 0.27f) m_vibOn = false;
        }
        // the room takes the controller's colour (on this page; for a moment when one says hello)
        const float al = Visible(w) ? Clamp01(1 - fabsf(w.PageAnim() - kPage) * 1.15f) : 0.0f;
        const float hp = now - m_helloAt < 4.0f ? expf(-(now - m_helloAt) * 1.2f) : 0.0f;
        Glide(m_tintW, m_tintV, fmaxf(al * 0.8f, hp * 0.6f), dt, 4.0f);
        float c[3] = { m_helloCol[0], m_helloCol[1], m_helloCol[2] };
        if (m_n && al >= hp) {
            const u32 c1 = m_pad[m_sel].l.main, c2 = m_pad[m_sel].r.main;
            c[0] = ((c1 & 0xFF) + (c2 & 0xFF)) / 510.0f; c[1] = (((c1 >> 8) & 0xFF) + ((c2 >> 8) & 0xFF)) / 510.0f; c[2] = (((c1 >> 16) & 0xFF) + ((c2 >> 16) & 0xFF)) / 510.0f;
        }
        float m = 0;
        for (int k = 0; k < 3; k++) { c[k] = powf(fmaxf(c[k], 0.0f), 2.2f); m = fmaxf(m, c[k]); }
        if (m < 0.04f) { c[0] = 0.22f; c[1] = 0.42f; c[2] = 0.95f; }             // (a black controller: the usual blue)
        else for (float &v : c) v *= 0.95f / m;                                     // as bright as the usual light
        for (int k = 0; k < 3; k++) Settings::s_tint[k] += (c[k] - Settings::s_tint[k]) * (1 - expf(-dt * 3.0f));
        Settings::s_tintW = m_tintW;
    }

    // ---------------------------------------------------------------- the drawings
    namespace {
        struct Live { u64 btn; const float *stk; const float *at; float now; };
        float Pop(const Live &L, int bit) { const float t = L.now - L.at[bit]; return t >= 0 && t < 0.5f ? expf(-t * 9.0f) : 0.0f; }
        bool On(const Live &L, int bit) { return (L.btn >> bit) & 1; }
        // a round button: lit and a little bigger when pressed
        void Btn(Ui &ui, float cx, float cy, float r, u32 col, float a, const Live &L, int bit, u32 glow) {
            const bool on = bit >= 0 && On(L, bit);
            const float p = bit >= 0 ? Pop(L, bit) : 0.0f;
            if (on || p > 0.01f) Glow(ui, cx, cy, r * 3.2f, r * 3.2f, Col(glow, (0.25f + 0.35f * p) * a, 1.0f, 0.35f));
            Dot(ui, cx, cy, r * (1 + 0.16f * p) + 1.2f, Rgba{ 0, 0, 0, 0.30f * a });
            Dot(ui, cx, cy, r * (1 + 0.16f * p), Col(col, a, 1.0f, on ? 0.55f : 0.0f));
            Dot(ui, cx - r * 0.25f, cy - r * 0.3f, r * 0.45f, Rgba{ 1, 1, 1, 0.08f * a });
        }
        void Stick(Ui &ui, float cx, float cy, float r, float sx, float sy, float a, bool on) {
            Dot(ui, cx, cy, r * 1.32f, Rgba{ 0.06f, 0.065f, 0.08f, a });
            Dot(ui, cx, cy, r * 1.18f, Rgba{ 0.10f, 0.11f, 0.13f, a });
            const float x = cx + sx * r * 0.42f, y = cy - sy * r * 0.42f;
            Dot(ui, x, y + 2, r, Rgba{ 0, 0, 0, 0.35f * a });
            Dot(ui, x, y, r, Rgba{ on ? 0.32f : 0.20f, on ? 0.34f : 0.21f, on ? 0.40f : 0.24f, a });
            Dot(ui, x, y, r * 0.72f, Rgba{ 0.15f, 0.16f, 0.19f, a });
            Dot(ui, x - r * 0.3f, y - r * 0.35f, r * 0.35f, Rgba{ 1, 1, 1, 0.07f * a });
        }
        void Shoulder(Ui &ui, float x0, float x1, float y, u32 body, float a, bool on) {
            RR(ui, x0, y - 7, x1, y + 6, 6, on ? Col(body, a, 1.0f, 0.6f) : Col(body, a, 0.62f));
        }
        // a Joy-Con, standing; right = the right one (mirrored)
        void Joy(Ui &ui, bool right, float x, float y, float w, float h, u32 body, u32 btn, float a, const Live &L) {
            auto fx = [&](float f) { return x + w * (right ? 1 - f : f); };
            Shoulder(ui, fx(right ? 0.85f : 0.15f) - (right ? 0 : 0), fx(right ? 0.15f : 0.85f), y + 2, body, a,
                     On(L, right ? 7 : 6) || On(L, right ? 9 : 8));
            RR(ui, x - 2, y + 6, x + w + 2, y + h + 8, w * 0.5f, Rgba{ 0, 0, 0, 0.35f * a });          // its shadow
            RR(ui, x, y, x + w, y + h, w * 0.5f, Col(body, a));                                          // rounded outside
            if (right) RR(ui, x, y, x + w * 0.5f, y + h, w * 0.14f, Col(body, a));                       // squarer inside
            else RR(ui, x + w * 0.5f, y, x + w, y + h, w * 0.14f, Col(body, a));
            RR(ui, x + 4, y + 4, x + w - 4, y + h * 0.32f, w * 0.42f, Rgba{ 1, 1, 1, 0.09f * a });       // light on top
            RR(ui, x + 4, y + h * 0.70f, x + w - 4, y + h - 4, w * 0.42f, Rgba{ 0, 0, 0, 0.10f * a });  // shade below
            if (right) RR(ui, x, y + h * 0.05f, x + 5, y + h * 0.95f, 2, Rgba{ 0, 0, 0, 0.30f * a });   // the rail
            else RR(ui, x + w - 5, y + h * 0.05f, x + w, y + h * 0.95f, 2, Rgba{ 0, 0, 0, 0.30f * a });
            const float r = w * 0.105f, off = w * 0.215f;
            if (!right) {
                Stick(ui, fx(0.5f), y + h * 0.27f, w * 0.19f, L.stk[0], L.stk[1], a, On(L, 4));
                const float bx = fx(0.5f), by = y + h * 0.56f;
                Btn(ui, bx, by - off, r, btn, a, L, 13, body); Btn(ui, bx, by + off, r, btn, a, L, 15, body);
                Btn(ui, bx - off, by, r, btn, a, L, 12, body); Btn(ui, bx + off, by, r, btn, a, L, 14, body);
                RR(ui, fx(0.70f), y + h * 0.075f, fx(0.88f), y + h * 0.075f + 5, 2, On(L, 11) ? Col(btn, a, 1, 0.6f) : Col(btn, a));   // minus
                RR(ui, fx(0.56f), y + h * 0.72f, fx(0.56f) + w * 0.16f, y + h * 0.72f + w * 0.16f, 3, Rgba{ 0.12f, 0.12f, 0.14f, a });   // capture
            } else {
                const float bx = fx(0.5f), by = y + h * 0.27f;
                Btn(ui, bx, by - off, r, btn, a, L, 2, body); Btn(ui, bx, by + off, r, btn, a, L, 1, body);      // X, B
                Btn(ui, bx - off, by, r, btn, a, L, 3, body); Btn(ui, bx + off, by, r, btn, a, L, 0, body);      // Y, A
                Stick(ui, fx(0.5f), y + h * 0.56f, w * 0.19f, L.stk[2], L.stk[3], a, On(L, 5));
                const float px = fx(0.79f), py = y + h * 0.085f;                                                  // plus
                const Rgba pc = On(L, 10) ? Col(btn, a, 1, 0.6f) : Col(btn, a);
                RR(ui, px - 9, py - 2.5f, px + 9, py + 2.5f, 2, pc); RR(ui, px - 2.5f, py - 9, px + 2.5f, py + 9, 2, pc);
                Dot(ui, fx(0.62f), y + h * 0.74f, w * 0.11f, Rgba{ 0.12f, 0.12f, 0.14f, a });                     // home
                Dot(ui, fx(0.62f), y + h * 0.74f, w * 0.07f, Rgba{ 0.20f, 0.21f, 0.24f, a });
            }
        }
        // the Pro Controller (or any other full controller)
        void Pro(Ui &ui, float x, float y, float w, float h, u32 body, u32 btn, float a, const Live &L) {
            Shoulder(ui, x + w * 0.12f, x + w * 0.34f, y + h * 0.10f, body, a, On(L, 6) || On(L, 8));
            Shoulder(ui, x + w * 0.66f, x + w * 0.88f, y + h * 0.10f, body, a, On(L, 7) || On(L, 9));
            const Rgba B = Col(body, a);
            Dot(ui, x + w * 0.23f, y + h * 0.72f + 6, w * 0.165f, Rgba{ 0, 0, 0, 0.35f * a });
            Dot(ui, x + w * 0.77f, y + h * 0.72f + 6, w * 0.165f, Rgba{ 0, 0, 0, 0.35f * a });
            Dot(ui, x + w * 0.23f, y + h * 0.70f, w * 0.16f, Col(body, a, 0.93f));                         // grips
            Dot(ui, x + w * 0.77f, y + h * 0.70f, w * 0.16f, Col(body, a, 0.93f));
            RR(ui, x + w * 0.06f, y + h * 0.10f, x + w * 0.94f, y + h * 0.66f, h * 0.30f, B);                 // body
            RR(ui, x + w * 0.24f, y + h * 0.45f, x + w * 0.76f, y + h * 0.74f, h * 0.12f, B);                 // the bridge
            RR(ui, x + w * 0.10f, y + h * 0.13f, x + w * 0.90f, y + h * 0.33f, h * 0.20f, Rgba{ 1, 1, 1, 0.08f * a });
            Stick(ui, x + w * 0.27f, y + h * 0.33f, w * 0.06f, L.stk[0], L.stk[1], a, On(L, 4));
            Stick(ui, x + w * 0.62f, y + h * 0.56f, w * 0.06f, L.stk[2], L.stk[3], a, On(L, 5));
            const float r = w * 0.032f, o = w * 0.055f, dx = x + w * 0.38f, dy = y + h * 0.56f, ax = x + w * 0.73f, ay = y + h * 0.33f;
            Btn(ui, dx, dy - o, r, btn, a, L, 13, body); Btn(ui, dx, dy + o, r, btn, a, L, 15, body);
            Btn(ui, dx - o, dy, r, btn, a, L, 12, body); Btn(ui, dx + o, dy, r, btn, a, L, 14, body);
            Btn(ui, ax, ay - o, r * 1.15f, btn, a, L, 2, body); Btn(ui, ax, ay + o, r * 1.15f, btn, a, L, 1, body);
            Btn(ui, ax - o, ay, r * 1.15f, btn, a, L, 3, body); Btn(ui, ax + o, ay, r * 1.15f, btn, a, L, 0, body);
            RR(ui, x + w * 0.39f, y + h * 0.22f, x + w * 0.44f, y + h * 0.22f + 5, 2, On(L, 11) ? Col(btn, a, 1, 0.6f) : Col(btn, a));
            const float px = x + w * 0.585f, py = y + h * 0.22f + 2.5f;
            const Rgba pc = On(L, 10) ? Col(btn, a, 1, 0.6f) : Col(btn, a);
            RR(ui, px - 8, py - 2.5f, px + 8, py + 2.5f, 2, pc); RR(ui, px - 2.5f, py - 8, px + 2.5f, py + 8, 2, pc);
            Dot(ui, x + w * 0.56f, y + h * 0.42f, w * 0.022f, Rgba{ 0.12f, 0.12f, 0.14f, a });
            RR(ui, x + w * 0.425f, y + h * 0.40f, x + w * 0.455f, y + h * 0.40f + w * 0.03f, 2, Rgba{ 0.12f, 0.12f, 0.14f, a });
        }
    }

    void Pads::DrawUi(Ui &ui, float now, const Wheel &w) {
        if (!Visible(w)) return;
        const float al = Clamp01(1 - fabsf(w.PageAnim() - kPage) * 1.15f);
        if (al <= 0.003f) return;
        const float dxp = (kPage - w.PageAnim()) * 520;
        // v94: a Joy-Con asleep beside the 3D Switch (not detected): its words float in the fog under it, breathing with it
        if (ModelVisible(w))
            for (int s = 0; s < 2; s++) {
                const float lo = Clamp01(m_lost[s]) * Clamp01(m_det[s]);
                if (lo <= 0.01f || !m_joyScrOk[s]) continue;
                const float br = 0.82f + 0.18f * sinf(now * 1.5f + s * 2.1f), la = al * lo * br;
                const float x = fminf(1180.0f, fmaxf(100.0f, m_joyScr[s][0])), y = fminf(470.0f, fmaxf(160.0f, m_joyScr[s][1])) + 10;
                const char *t = s ? lang::T("Joy-Con (D) en veille") : lang::T("Joy-Con (G) en veille");
                const float tw = ui.Width(fonts::kSetDesc, t, 0.5f);
                ui.Veil(x - tw / 2 - 70, y - 22, x + tw / 2 + 70, y + 56, 0.55f * la);
                ui.TextCentered(fonts::kSetDesc, t, x, y, 0.5f, ps2::A(ps2::kChoice, la));
                ui.TextCentered(fonts::kHint, lang::T("Appuie sur un bouton pour le r\xc3\xa9veiller"), x, y + 24, 0.3f, ps2::A(ps2::kFaint, 0.8f * la));
            }
        if (!m_n) {                                       // v94: the console stays (DrawModel), its Joy-Con asleep beside it
            const char *t = lang::T("Aucune manette connect\xc3\xa9" "e.");
            const float tw = ui.Width(fonts::kSetSel, t, 0.5f);
            ui.Veil(640 + dxp - tw / 2 - 180, 512, 640 + dxp + tw / 2 + 180, 636, 0.8f * al);
            ui.TextCentered(fonts::kSetSel, t, 640 + dxp, 538, 0.5f, ps2::A(ps2::kWords, al));
            ui.TextCentered(fonts::kHint, lang::T("Y : appairer une manette"), 640 + dxp, 584, 0.5f, ps2::A(ps2::kFaint, 0.9f * al));
            return;
        }
        const Pad &p = m_pad[m_sel];
        const Live L = { m_btn, m_stk, m_btnAt, now };
        const float in = EaseGlide((now - m_moveAt) / Pace(0.5f));          // the chosen one arrives (◀ ▶)
        const float a = al * (0.3f + 0.7f * in);
        const float vib = now - m_vibAt < 0.45f ? sinf((now - m_vibAt) * 70.0f) * 5.0f * expf(-(now - m_vibAt) * 7.0f) : 0.0f;
        const float cx = 640 + dxp + vib, cy = 330 + sinf(now * 1.1f) * 4.0f + (1 - in) * 14;
        const float pulse = fmaxf(now - m_vibAt < 1.2f ? expf(-(now - m_vibAt) * 3.0f) : 0.0f, now - m_helloAt < 2.0f ? expf(-(now - m_helloAt) * 2.0f) : 0.0f);
        const bool joy3d = p.kind == kPair || p.kind == kHandheld || p.kind == kLeft || p.kind == kRight;
        // its colours glow behind it, softly (the 2D drawings; the 3D Switch is lit by the room)
        const float ga = (0.22f + 0.30f * pulse) * al * (joy3d ? 0.0f : 1.0f);
        if (p.kind == kPair || p.kind == kHandheld) {
            Glow(ui, cx - 170, cy + 10, 330, 300, Col(p.l.main, ga, 1.0f, 0.15f));
            Glow(ui, cx + 170, cy + 10, 330, 300, Col(p.r.main, ga, 1.0f, 0.15f));
        } else Glow(ui, cx, cy + 10, 420, 320, Col(p.l.main, ga, 1.0f, 0.15f));
        // the drawing, in its real colours, alive
        if (!joy3d) switch (p.kind) {
            case kPair: {
                const float jw = 118, jh = 330, gap = 46;
                Joy(ui, false, cx - gap / 2 - jw, cy - jh / 2, jw, jh, p.l.main, p.l.sub, a, L);
                Joy(ui, true, cx + gap / 2, cy - jh / 2, jw, jh, p.r.main, p.r.sub, a, L);
                break;
            }
            case kLeft:  Joy(ui, false, cx - 59, cy - 165, 118, 330, p.l.main, p.l.sub, a, L); break;
            case kRight: Joy(ui, true, cx - 59, cy - 165, 118, 330, p.l.main, p.l.sub, a, L); break;
            case kHandheld: {
                const float h = 250, jw = h * 0.355f, wc = h * 1.70f, x0 = cx - (wc + 2 * jw) / 2, y0 = cy - h / 2;
                RR(ui, x0 + jw - 4, y0 + 6, x0 + jw + wc + 4, y0 + h + 8, 14, Rgba{ 0, 0, 0, 0.35f * a });
                RR(ui, x0 + jw - 3, y0, x0 + jw + wc + 3, y0 + h, 10, Rgba{ 0.15f, 0.16f, 0.18f, a });           // the console
                RR(ui, x0 + jw + wc * 0.05f, y0 + h * 0.07f, x0 + jw + wc * 0.95f, y0 + h * 0.93f, 6, Rgba{ 0.02f, 0.025f, 0.035f, a });
                Glow(ui, x0 + jw + wc * 0.5f, y0 + h * 0.55f, wc * 0.38f, h * 0.36f, Rgba{ Settings::s_ambient[0] * 0.5f, Settings::s_ambient[1] * 0.5f, Settings::s_ambient[2] * 0.5f, 0.5f * a });   // a little fog in its screen
                RR(ui, x0 + jw + wc * 0.05f, y0 + h * 0.07f, x0 + jw + wc * 0.95f, y0 + h * 0.20f, 6, Rgba{ 1, 1, 1, 0.04f * a });
                Joy(ui, false, x0, y0, jw, h, p.l.main, p.l.sub, a, L);
                Joy(ui, true, x0 + jw + wc, y0, jw, h, p.r.main, p.r.sub, a, L);
                break;
            }
            default: Pro(ui, cx - 220, cy - 150, 440, 300, p.l.main, p.l.sub, a, L); break;
        }
        // its name, its player lights, its battery
        char nm[48];
        Name(p, nm, sizeof nm);
        {   // v70.3 (the PS2 look): the name near-white in a halo of light, on a soft veil (no glass)
            const float tw = ui.Width(fonts::kSetSel, nm, 0.5f);
            ui.Veil(640 + dxp - tw / 2 - 180, 512, 640 + dxp + tw / 2 + 180, 636, 0.8f * al);
            ui.Light(640 + dxp - tw / 2 - 50, 536, 640 + dxp + tw / 2 + 50, 592, ps2::A(ps2::kHalo, 0.30f * al), true);
            ui.TextCentered(fonts::kSetSel, nm, 640 + dxp, 538, 0.5f, ps2::A(ps2::kChosen, al));
        }
        float x = 640 + dxp;
        char who[24];
        if (p.kind == kHandheld) snprintf(who, sizeof who, lang::T("Sur la console"));
        else snprintf(who, sizeof who, lang::T("Joueur %d"), (int)p.id + 1);
        const bool split = p.kind == kPair || p.kind == kHandheld;
        const float rowW = ui.Width(fonts::kGameInfo, who, 2) + 14 + (p.kind == kHandheld ? 0 : 4 * 12 + 18) + (split ? 2 * 92 : 92);
        x -= rowW / 2;
        const float ry = 588;
        x += ui.TextCentered(fonts::kGameInfo, who, x + ui.Width(fonts::kGameInfo, who, 2) / 2, ry - 7, 2, ps2::A(ps2::kChoice, al)) * 0 + ui.Width(fonts::kGameInfo, who, 2) + 14;
        if (p.kind != kHandheld) {                                        // the 4 player lights (the real ones are green)
            const int lit = ((int)p.id) & 3;
            for (int k = 0; k < 4; k++) Dot(ui, x + 4 + k * 12, ry, 3.5f, k == lit ? Rgba{ 0.62f, 1.0f, 0.70f, al } : Rgba{ 0.22f, 0.25f, 0.34f, al });
            x += 4 * 12 + 18;
        }
        auto Batt = [&](const char *side, const HidPowerInfo &pi) {      // a glass gauge of 4 cells (+ « en charge »)
            float bx = x;
            if (side[0]) { bx += ui.TextCentered(fonts::kGameInfo, side, bx + 6, ry - 7, 0, ps2::A(ps2::kFaint, al)) * 0 + 16; }
            const int lv = (int)(pi.battery_level > 4 ? 4 : pi.battery_level);
            const bool low = lv <= 1 && !pi.is_charging;
            for (int k = 0; k < 4; k++) {
                const bool f = k < lv;
                const float ch = pi.is_charging && k == lv && lv < 4 ? 0.5f + 0.5f * sinf(now * 4.0f) : 0.0f;
                RR(ui, bx + k * 15, ry - 5, bx + k * 15 + 12, ry + 5, 3, f ? (low ? Rgba{ 1.0f, 0.70f, 0.32f, al } : Rgba{ 0.80f, 0.88f, 1.0f, al })
                                                                         : Rgba{ 0.80f, 0.88f, 1.0f, (0.14f + 0.5f * ch) * al });
            }
            x += 92;
        };
        if (split) { Batt("L", p.pl); Batt("R", p.pr); } else Batt("", p.pl);
        if (p.pl.is_charging || p.pr.is_charging)
            ui.TextCentered(fonts::kGameInfo, "en charge", 640 + dxp, ry + 18, 2, ps2::A(ps2::kGreen, 0.8f * al));
        // several controllers: the selector at the top (their two colours, their player)
        // v92: live - a controller that connects GROWS IN (its place opens, the others glide aside, a soft light of its colour),
        // one that leaves FADES OUT and its place closes (each card on its own spring, Ease.hpp)
        float sum = 0, tot = 0;
        const float pw = 118, gap = 10;
        for (int k = 0; k < 9; k++) if (m_show[k] > 0.003f) { sum += m_show[k]; tot += (pw + gap) * m_show[k]; }
        const float sa = Clamp01((sum - 1.0f) * 2.0f);        // (one controller left: no selector)
        for (float &w : m_cardW) w = 0;
        if (sa > 0.003f) {
            const int selK = NpadIndex(m_pad[m_sel].id);
            float x = 640 + dxp - (tot - gap) / 2;
            for (int k = 0; k < 9; k++) {
                const float s = m_show[k];
                if (s <= 0.003f) continue;
                const Pad &q = m_ghost[k];
                const float slot = (pw + gap) * s, px = x + (slot - gap - pw) / 2, py = 104 - (1 - s) * 12;
                const float ca = al * sa * EaseGlide(s);
                m_cardX[k] = x; m_cardW[k] = slot - gap;
                x += slot;
                if (k == selK && ((m_mask >> k) & 1)) {        // v70 (the PS2 look): a soft light, no pill
                    const Rgba ac = Settings::Accent(1.0f);
                    ui.Light(px - 10, py - 8, px + pw + 10, py + 44, Rgba{ ac.r, ac.g, ac.b, 0.34f * ca }, true);
                }
                const float born = now - m_bornAt[k] < 2.0f ? expf(-(now - m_bornAt[k]) * 2.2f) : 0.0f;
                if (born > 0.01f) Glow(ui, px + 29, py + 18, 70, 46, Col(q.l.main, 0.5f * born * ca, 1.0f, 0.3f));   // (the new one: its colour, softly)
                Dot(ui, px + 22, py + 18, 7 * (0.6f + 0.4f * s), Col(q.l.main, ca)); Dot(ui, px + 36, py + 18, 7 * (0.6f + 0.4f * s), Col(q.r.main, ca));
                char t[16];
                if (q.kind == kHandheld) snprintf(t, sizeof t, "Console"); else snprintf(t, sizeof t, lang::T("J%d"), (int)q.id + 1);
                ui.TextCentered(fonts::kGameInfo, t, px + 80, py + 10, 1, Hex(k == selK ? 0xf2f5ff : 0x8f9ac2, ca));
            }
        }
    }


    // ---------------------------------------------------------------- v40: the 3D Switch
    bool Pads::ModelVisible(const Wheel &w) const {
        if (!Visible(w)) return false;
        if (!m_n) return true;                            // v94: no controller at all: the console is still there, its Joy-Con asleep
        const Kind k = m_pad[m_sel].kind;
        return k == kPair || k == kHandheld || k == kLeft || k == kRight;
    }

    void Pads::DrawModel(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w) {
        if (!ModelVisible(w) || !a.model[0].count) return;
        using namespace m3;
        float eye[3], at[3];
        w.CameraAt(now, eye, at);
        const M4 vp = Mul(Persp(38, (float)gpu.Width() / gpu.Height(), 0.3f, 300), LookAt(eye, at));
        // v41 - where the Joy-Con are, as in real life: on the console's rail if the console has that side, else in the hands
        // (floating beside it). They move on ONE smooth curve (up along the rail, then out, never in steps); coming back,
        // they glide down onto the rail and CLICK: a round woody knock that resonates in the chord, a vibration, and the
        // console gives a little under it.
        static float last = -1;
        const bool fresh = last < 0 || now - last > 0.5f;   // (the page was not on screen: show the state as it is, no sound)
        const float dt = fresh ? 0.0f : fminf(0.05f, fmaxf(0.0f, now - last));
        last = now;
        for (int s = 0; s < 2; s++) {
            const float to = m_attach[s] ? 0.0f : 1.0f;
            const float pan = s ? 0.45f : -0.45f;
            if (fresh) { m_detTo[s] = m_det[s] = to; m_detV[s] = 0; }
            if (to != m_detTo[s]) {
                if (to > m_detTo[s]) {                    // off the rail: a soft release
                    audio::Thock(pan, 0.95f, 0.045f);
                    audio::Bell(audio::HarmonyNote(s ? 2 : 0, 55, 67), 0.03f, true, 0.02f);
                    haptic::Tick(0.5f);
                }
                m_detTo[s] = to;
            }
            const float before = m_det[s];
            Glide(m_det[s], m_detV[s], m_detTo[s], dt, 5.0f);
            if (m_detTo[s] == 0 && before > 0.035f && m_det[s] <= 0.035f && now - m_clickAt[s] > 0.4f) {   // home on its rail: CLICK
                m_clickAt[s] = now; m_nudgeAt = now;
                audio::Thock(pan, 1.15f, 0.11f);                                   // the click itself: round, woody
                audio::Thock(pan, 0.70f, 0.07f);                                   // its body, lower
                audio::Bell(audio::HarmonyNote(0, 50, 62), 0.05f, true, 0.01f);    // and it resonates, low, in the chord
                audio::Bell(audio::HarmonyNote(2, 62, 74), 0.03f, true, 0.06f);
                audio::SoftLow(audio::HarmonyNote(0, 38, 49), 0.12f, 0.6f);
                haptic::Tap(0.8f); haptic::Thump(0.6f);
            }
        }
        // the console, big in the middle; it arrives with the page. v94 (Yass: « avec le stick R droit on puisse tourner la switch
        // exactement comme dans le menu jeux, smooth »): the menu's right stick (any controller), glided by the wheel (Wheel:
        // the same dead zone, the same spring, the same angles: it turns up to 1.3 rad, it tilts up to 0.6 rad)
        const float away = (kPage - w.PageAnim()) * 16.0f;
        const float in = EaseGlide((now - m_moveAt) / Pace(0.55f));
        const float *stk = w.StickS();
        const float nd = now - m_nudgeAt, nudge = nd >= 0 && nd < 1.2f ? 0.035f * sinf(nd * 16.0f) * expf(-nd * 6.0f) : 0.0f;   // it gives a little
        const float yaw = sinf(now * 0.4f) * 0.10f, pitch = -0.32f + nudge - stk[1] * 0.6f, turn = stk[0] * 1.3f;
        const float sc = 1.45f * (0.92f + 0.08f * in);
        auto baseAt = [&](float tk) { return Mul(Mul(Mul(Mul(Translate(away, 0.80f + sinf(now * 1.1f) * 0.02f, 2.3f), RotY(yaw)), RotX(pitch)), RotY(turn * tk)), Scale3(sc, sc, sc)); };
        const M4 base = baseAt(1.0f);
        float wake[2];
        auto joyM = [&](int s) {                          // a Joy-Con on one smooth curve: up along its rail, curving out to the side
            const float e = fminf(1.0f, fmaxf(0.0f, m_det[s])), sg = s ? 1.0f : -1.0f;
            const float t = e * e * (3 - 2 * e);              // (smooth at both ends)
            // a quadratic Bezier: on the rail (0, 0) -> up (0, 1.0) -> beside the console (0.62, 0.40)
            const float x = 2 * t * (1 - t) * 0.0f + t * t * 0.62f, y = 2 * t * (1 - t) * 1.0f + t * t * 0.40f;
            // v94: asleep (not detected): it drifts further out and back, lower, leaning, and floats slower and wider; waking up
            // it gives a little hop
            const float lo = fminf(1.0f, fmaxf(0.0f, m_lost[s])) * t, ls = lo * lo * (3 - 2 * lo);
            const float wt = now - m_wakeAt[s];
            wake[s] = wt >= 0 && wt < 1.4f ? expf(-wt * 3.2f) * Clamp01(wt * 14) : 0.0f;
            const float hop = wt >= 0 && wt < 0.9f ? sinf(wt * 3.4907f) * expf(-wt * 2.5f) * 0.10f : 0.0f;
            const float bob = sinf(now * (1.3f - 0.65f * ls) + s * 1.7f) * (0.025f + 0.02f * ls) * t;   // floating in the hands
            // (in the hands, it follows the console's turn only in part: turned far, it would swing into the camera)
            return Mul(t > 0.001f ? baseAt(1 - 0.65f * t) : base, Mul(Translate(sg * (x + 0.20f * ls), y + bob - 0.10f * ls + hop * t, 0.10f * t - 0.30f * ls),
                                 RotZ(-sg * (0.16f * t + 0.22f * ls))));
        };
        const M4 mL = joyM(0), mR = joyM(1);
        {   // v94: where each floating Joy-Con is on screen (its words float under it: DrawUi)
            const M4 vpm[2] = { Mul(vp, mL), Mul(vp, mR) };
            for (int s = 0; s < 2; s++) {
                const float c[3] = { s ? 0.86f : -0.86f, -0.46f, 0.0f };
                m_joyScrOk[s] = Project(vpm[s], c, m_joyScr[s][0], m_joyScr[s][1]);
            }
        }
        // a Joy-Con lights up a little when one of its buttons is pressed; v94: and flashes its colour when it wakes up
        const Live L = { m_btn, m_stk, m_btnAt, now };
        const int bitsL[] = { 4, 6, 8, 11, 12, 13, 14, 15, 24, 25 }, bitsR[] = { 0, 1, 2, 3, 5, 7, 9, 10, 26, 27 };
        float glow[2] = { 0, 0 };
        for (int b : bitsL) glow[0] = fmaxf(glow[0], On(L, b) ? 0.6f : Pop(L, b));
        for (int b : bitsR) glow[1] = fmaxf(glow[1], On(L, b) ? 0.6f : Pop(L, b));
        float dim[2];
        for (int s = 0; s < 2; s++) {
            const float lo = Clamp01(m_lost[s]) * Clamp01(m_det[s]);
            glow[s] = fmaxf(glow[s] * (1 - lo), 2.2f * wake[s]);
            dim[s] = 1 - lo * (0.55f + 0.12f * sinf(now * 1.5f + s * 2.1f));   // dimmed, breathing slowly (~4 s)
        }
        if (!gpu.MeshDrawn()) gpu.BeginMesh();
        MeshUbo u = Wheel::s_lights;
        u.cam[0] = eye[0]; u.cam[1] = eye[1]; u.cam[2] = eye[2];
        const bool joyOk = Settings::s_joyKnown && a.joyCell[0] > 0 && a.joyCell[1] > 0;
        for (int k = 0; k < 3; k++) { u.joyL[k] = Settings::s_joyL[k]; u.joyR[k] = Settings::s_joyR[k]; }
        u.joyL[3] = joyOk ? a.joyCell[0] : -1.0f; u.joyR[3] = joyOk ? a.joyCell[1] : -1.0f;
        const Model &md = a.model[0];
        auto draw = [&](const M4 &m, const MeshPart &q, DkGpuAddr idx, uint32_t count, float g, const float *gc, float dk = 1.0f) {
            if (!count) return;
            memcpy(u.model, m.m, 64);
            memcpy(u.mvp, Mul(vp, m).m, 64);
            memcpy(u.base, q.base, 16);
            for (int k = 0; k < 3; k++) { u.base[k] *= dk; u.joyL[k] = Settings::s_joyL[k] * dk; u.joyR[k] = Settings::s_joyR[k] * dk; }   // (v94: asleep, dimmed)
            u.mat[0] = q.metal; u.mat[1] = q.rough; u.mat[2] = q.env;
            u.mat[3] = (float)((q.tex >= 0 ? 1 : 0) | (q.glass > 0.5f && q.base[3] >= 0.999f ? 2 : 0) | (q.emiTex >= 0 ? 4 : 0));
            u.tint[0] = q.tint[0]; u.tint[1] = q.tint[1]; u.tint[2] = q.tint[2]; u.tint[3] = 0.3f / 0.8f;
            for (int k = 0; k < 3; k++) u.emi[k] = q.emi[k] + (gc ? gc[k] * g * 0.35f : 0.0f);
            u.emi[3] = q.dbl;
            gpu.SetMeshUniforms(&u, sizeof u);
            const Target *ts[4] = { q.tex >= 0 ? &a.tex[q.tex] : &a.env, &a.env, &haze, q.emiTex >= 0 ? &a.tex[q.emiTex] : &a.env };
            gpu.BindTextures(ts, 4, true);
            gpu.DrawMesh(q.vtx, q.vtxBytes, idx, count);
        };
        for (int pass = 0; pass < 2; pass++)              // opaque first, then the see-through parts
            for (int pi = 0; pi < md.count; pi++) {
                const MeshPart &q = md.parts[pi];
                const bool blend = q.base[3] < 0.999f && q.glass < 0.5f;
                if ((pass == 1) != blend) continue;
                gpu.SetMeshState(q.dbl < 0.5f, blend, false);
                if (q.splitR > q.splitL || q.splitL > 0) {   // the piece with the Joy-Con: three draws
                    draw(base, q, q.idx, q.splitL, 0, nullptr);
                    draw(mL, q, q.idx + (DkGpuAddr)q.splitL * 4, q.splitR - q.splitL, glow[0], Settings::s_joyL, dim[0]);
                    draw(mR, q, q.idx + (DkGpuAddr)q.splitR * 4, q.idxCount - q.splitR, glow[1], Settings::s_joyR, dim[1]);
                } else draw(base, q, q.idx, q.idxCount, 0, nullptr);
            }
        gpu.SetMeshState(true, false, false);
    }


    // ---------------------------------------------------------------- v84: « Tester les manettes » (see Pads.hpp)
    namespace {
        // what the test lights: each button's bit, and the side of the controller it is on (0 left, 1 right: its motor answers)
        enum { sA, sB, sX, sY, sUp, sRight, sDown, sLeft, sPlus, sMinus, sL3, sR3, sL, sR, sZL, sZR, sLSL, sLSR, sRSL, sRSR };
        constexpr int kSlotBit[20]  = { 0, 1, 2, 3, 13, 14, 15, 12, 10, 11, 4, 5, 6, 7, 8, 9, 24, 25, 26, 27 };
        constexpr int kSlotSide[20] = { 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1 };
        constexpr u64 kTestMask = 0x0F00FFFFull;            // those buttons (not the sticks' pseudo-buttons)
        constexpr float kSX[2] = { 300, 980 }, kSY = 352, kSR = 112;   // the sticks' circles
        constexpr float kDead = 0.15f;                      // the menu's own dead zone (Entry: the right stick)
        constexpr float kHold = 0.75f;                      // hold B (leave) / X (measure again) this long
        int IdIndex(HidNpadIdType id) { return id == HidNpadIdType_Handheld ? 8 : ((int)id & 7); }
        void Num(char *out, int cap, float v, bool sign) {  // « +0,62 » (French: a comma)
            snprintf(out, cap, sign ? "%+.2f" : "%.2f", v);
            if (!lang::En()) for (char *p = out; *p; p++) if (*p == '.') *p = ',';
        }
        // the gyroscope's turn, as quaternions (x, y, z, w)
        void QMul(const float *a, const float *b, float *o) {
            o[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
            o[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
            o[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
            o[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        }
        void QNorm(float *q) {
            const float n = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (n < 1e-6f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
            for (int i = 0; i < 4; i++) q[i] /= n;
        }
        // nn::hid's DirectionState: its rows are the controller's axes in the world, so the turn (controller -> world) is its
        // transpose: R[i][j] = m[j][i]
        void QFromDir(const float m[3][3], float *q) {
            const float r00 = m[0][0], r11 = m[1][1], r22 = m[2][2];
            const float r21 = m[1][2], r12 = m[2][1], r02 = m[2][0], r20 = m[0][2], r10 = m[0][1], r01 = m[1][0];
            const float tr = r00 + r11 + r22;
            if (tr > 0) { const float s = sqrtf(tr + 1) * 2; q[3] = 0.25f * s; q[0] = (r21 - r12) / s; q[1] = (r02 - r20) / s; q[2] = (r10 - r01) / s; }
            else if (r00 > r11 && r00 > r22) { const float s = sqrtf(1 + r00 - r11 - r22) * 2; q[3] = (r21 - r12) / s; q[0] = 0.25f * s; q[1] = (r01 + r10) / s; q[2] = (r02 + r20) / s; }
            else if (r11 > r22) { const float s = sqrtf(1 + r11 - r00 - r22) * 2; q[3] = (r02 - r20) / s; q[0] = (r01 + r10) / s; q[1] = 0.25f * s; q[2] = (r12 + r21) / s; }
            else { const float s = sqrtf(1 + r22 - r00 - r11) * 2; q[3] = (r10 - r01) / s; q[0] = (r02 + r20) / s; q[1] = (r12 + r21) / s; q[2] = 0.25f * s; }
            QNorm(q);
        }
        void QRotate(const float *q, const float *p, float *o) {
            const float x = q[0], y = q[1], z = q[2], w = q[3];
            o[0] = (1 - 2 * (y * y + z * z)) * p[0] + 2 * (x * y - z * w) * p[1] + 2 * (x * z + y * w) * p[2];
            o[1] = 2 * (x * y + z * w) * p[0] + (1 - 2 * (x * x + z * z)) * p[1] + 2 * (y * z - x * w) * p[2];
            o[2] = 2 * (x * z - y * w) * p[0] + 2 * (y * z + x * w) * p[1] + (1 - 2 * (x * x + y * y)) * p[2];
        }
#ifdef BRUME_UISIM
        // the PC preview has no controller (HID is a stub there): a made-up Pro Controller - its sticks at rest during the
        // countdown (the right one drifts a little), then they draw circles, every button in turn, a chord, and it tilts
        void Fake(float t, HidNpadCommonState &st, float *q) {
            memset(&st, 0, sizeof st);
            float lx = 0.02f + 0.006f * sinf(t * 23), ly = -0.015f, rx = 0.17f + 0.008f * sinf(t * 31), ry = 0.12f;
            if (t > 3.6f) {
                const float u = t - 3.6f;
                lx = 0.86f * cosf(u * 2.6f); ly = 0.86f * sinf(u * 2.6f);
                rx = 0.9f * sinf(u * 1.9f); ry = 0.55f * sinf(u * 3.8f);
            }
            st.analog_stick_l.x = (s32)(lx * 32767); st.analog_stick_l.y = (s32)(ly * 32767);
            st.analog_stick_r.x = (s32)(rx * 32767); st.analog_stick_r.y = (s32)(ry * 32767);
            static const int seq[16] = { sA, sB, sX, sY, sUp, sRight, sDown, sLeft, sL, sZL, sR, sZR, sPlus, sMinus, sL3, sR3 };
            if (t > 3.8f) { const float u = (t - 3.8f) / 0.32f; const int i = (int)u; if (i < 16 && u - i < 0.55f) st.buttons |= 1ull << kSlotBit[seq[i]]; }
            if (t > 9.2f && t < 10.4f) st.buttons |= (1ull << 0) | (1ull << 3) | (1ull << 9);      // a chord: A + Y + ZR
            const float roll = 0.42f * sinf(t * 0.8f), pitch = 0.32f * sinf(t * 0.55f + 0.6f);
            const float qx[4] = { sinf(pitch / 2), 0, 0, cosf(pitch / 2) }, qy[4] = { 0, sinf(roll / 2), 0, cosf(roll / 2) };
            QMul(qy, qx, q);
        }
#endif
    }

    void Pads::TestBegin(float now) {
        if (m_t.open) return;
        m_t = Test{};
        Test &t = m_t;
        t.open = true; t.openAt = now;
        for (float &a : t.at) a = -100;
        for (auto &r : t.rest) r[0] = r[1] = -1;
        // which one: the one whose A is down (it just opened this screen), else the one of the Manettes page, else the first
        int pick = -1;
#ifdef BRUME_UISIM
        pick = 0;
#else
        for (int i = 0; i < m_n; i++) {
            HidNpadCommonState st;
            if (!Read(m_pad[i].id, m_pad[i].style, st)) continue;
            const int k = IdIndex(m_pad[i].id);
            t.seen[k] = st.buttons & kTestMask;
            if (pick < 0 && (st.buttons & HidNpadButton_A)) pick = k;
        }
        if (pick < 0 && m_n) pick = IdIndex(m_pad[m_sel < m_n ? m_sel : 0].id);
#endif
        if (pick >= 0) TestSelect(pick, now);
        t.switchAt = -100;                                  // (the first one is not announced)
        audio::PlaySweep(audio::Whoosh, 0.3f, -0.3f, 0.5f, 0.45f);
        audio::Bell(audio::HarmonyNote(0, 67, 79), 0.03f, true, 0.05f);
        audio::Bell(audio::HarmonyNote(2, 74, 86), 0.024f, true, 0.17f);
    }

    // its motors quiet, its sensor stopped (unless the menu's own gyroscope uses it: the console, Joy-Con pair n° 1)
    void Pads::TestQuiet() {
        Test &t = m_t;
        if (t.vibOn && t.nvh) {
            HidVibrationValue v[2];
            for (HidVibrationValue &x : v) { x.amp_low = x.amp_high = 0; x.freq_low = 160; x.freq_high = 320; }
            hidSendVibrationValues(t.vh, v, t.nvh);
        }
        t.vibOn = false; t.nvh = 0; t.vibZero = true;
        if (t.sixOn && t.sixMine) hidStopSixAxisSensor(t.six);
        t.sixOn = t.sixMine = t.sixSeen = t.qInit = false;
    }

    void Pads::TestEnd(float now) {
        if (!m_t.open) return;
        TestQuiet();
        m_t.open = false; m_t.closeAt = now;
        audio::Play(audio::Tick1700, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, -0.3f, 0.3f, 0.45f, 0.4f);
    }

    void Pads::TestStop() {
        if (m_t.open) TestQuiet();
        m_t.open = false; m_t.closeAt = -100;               // (gone at once)
        Settings::s_cover = 0;
    }

    bool Pads::TestRead(HidNpadCommonState &st, float now) {
        Test &t = m_t;
        t.trig[0] = t.trig[1] = -1;
#ifdef BRUME_UISIM
        Fake(now - t.openAt, st, t.qr);
        t.sixSeen = true;
        return t.cur >= 0;
#else
        (void)now;
        if (t.cur < 0 || !Read(t.pad.id, t.pad.style, st)) return false;
        if (t.pad.style & HidNpadStyleTag_NpadGc) {        // a GameCube controller: its L / R are analog
            HidNpadGcState g;
            if (hidGetNpadStatesGc(t.pad.id, &g, 1) > 0) {
                t.trig[0] = fminf(1.0f, g.trigger_l / 32767.0f);
                t.trig[1] = fminf(1.0f, g.trigger_r / 32767.0f);
            }
        }
        return true;
#endif
    }

    void Pads::TestSelect(int k, float now) {
        Test &t = m_t;
        TestQuiet();
        t.cur = k; t.switchAt = now;
#ifdef BRUME_UISIM
        memset(&t.pad, 0, sizeof t.pad);
        t.pad.id = HidNpadIdType_No1; t.pad.style = HidNpadStyleTag_NpadFullKey; t.pad.kind = kPro;
        t.pad.l.main = t.pad.r.main = 0x2a2a2e; t.pad.l.sub = t.pad.r.sub = 0x1e1e1e; t.pad.hasL = t.pad.hasR = true;
        t.sixOn = true;
#else
        bool found = false;
        for (int i = 0; i < m_n; i++) if (IdIndex(m_pad[i].id) == k) { t.pad = m_pad[i]; found = true; }
        if (!found) { t.cur = -1; return; }
        const u32 sty = t.pad.style;
        const HidNpadStyleTag tag = (sty & HidNpadStyleTag_NpadHandheld) ? HidNpadStyleTag_NpadHandheld :
                                    (sty & HidNpadStyleTag_NpadJoyDual) ? HidNpadStyleTag_NpadJoyDual :
                                    (sty & HidNpadStyleTag_NpadFullKey) ? HidNpadStyleTag_NpadFullKey :
                                    (sty & HidNpadStyleTag_NpadJoyLeft) ? HidNpadStyleTag_NpadJoyLeft :
                                    (sty & HidNpadStyleTag_NpadJoyRight) ? HidNpadStyleTag_NpadJoyRight : (HidNpadStyleTag)0;
        if (tag) {
            // its motors (two: left and right; a single Joy-Con: one) and its gyroscope (a pair: the right Joy-Con's)
            const int nv = tag == HidNpadStyleTag_NpadJoyLeft || tag == HidNpadStyleTag_NpadJoyRight ? 1 : 2;
            if (R_SUCCEEDED(hidInitializeVibrationDevices(t.vh, nv, t.pad.id, tag))) { t.vibOn = true; t.nvh = nv; }
            HidSixAxisSensorHandle h[2];
            const int ns = tag == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
            if (R_SUCCEEDED(hidGetSixAxisSensorHandles(h, ns, t.pad.id, tag))) {
                t.six = ns == 2 && t.pad.hasR ? h[1] : h[0];
                if (R_SUCCEEDED(hidStartSixAxisSensor(t.six))) {
                    t.sixOn = true;
                    t.sixMine = !(t.pad.id == HidNpadIdType_Handheld || (t.pad.id == HidNpadIdType_No1 && tag == HidNpadStyleTag_NpadJoyDual));
                }
            }
        }
#endif
        HidNpadCommonState st;
        if (TestRead(st, now)) {                            // (what is already down does not ring)
            t.btn = t.prev = st.buttons;
            const float r[4] = { st.analog_stick_l.x / 32767.0f, st.analog_stick_l.y / 32767.0f, st.analog_stick_r.x / 32767.0f, st.analog_stick_r.y / 32767.0f };
            for (int q = 0; q < 4; q++) t.raw[q] = t.stk[q] = fmaxf(-1.05f, fminf(1.05f, r[q]));
        }
        for (int s = 0; s < 2; s++) for (int j = 0; j < kTrail; j++) { t.trail[s][j][0] = t.stk[2 * s]; t.trail[s][j][1] = t.stk[2 * s + 1]; }
        for (int i = 0; i < kSlots; i++) { t.sink[i] = t.sinkV[i] = 0; }
        if (t.rest[k][0] < 0 && t.rest[k][1] < 0) TestMeasure(now);   // its rest, measured once
    }

    // the « ne touche plus les sticks » moment: 3 s, the last 2 measured (the average position is the rest)
    void Pads::TestMeasure(float now) {
        Test &t = m_t;
        if (t.cur < 0) return;
        t.measAt = now; t.doneAt = -100; t.tick = -1; t.cnt = 0;
        for (int s = 0; s < 2; s++) for (int a = 0; a < 2; a++) { t.sum[s][a] = 0; t.lo[s][a] = 9; t.hi[s][a] = -9; }
        t.rest[t.cur][0] = t.rest[t.cur][1] = -1;
    }

    // a button goes in: its note of the chord (each one has its own: the controller is an instrument), and its side's motor
    void Pads::TestPress(int slot, float now) {
        Test &t = m_t;
        t.at[slot] = now;
        const int side = kSlotSide[slot];
        const float pan = side ? 0.45f : -0.45f;
        switch (slot) {
            case sA: case sB: case sX: case sY: audio::Bell(audio::HarmonyNote(slot - sA, 74, 86), 0.034f, true); break;
            case sUp: case sRight: case sDown: case sLeft: audio::Bell(audio::HarmonyNote(slot - sUp, 62, 74), 0.032f, true); break;
            case sPlus: audio::Bell(audio::HarmonyNote(2, 86, 98), 0.022f, true); break;
            case sMinus: audio::Bell(audio::HarmonyNote(0, 86, 98), 0.022f, true); break;
            case sL3: case sR3: audio::Crystal(pan, 0.7f, audio::HarmonyNote(slot == sL3 ? 0 : 2, 79, 91)); break;
            case sL: case sR: audio::Thock(pan, 0.95f, 0.05f); audio::Bell(audio::HarmonyNote(slot == sL ? 0 : 2, 55, 67), 0.03f, true, 0.01f); break;
            case sZL: case sZR: audio::Thock(pan, 0.7f, 0.06f); audio::SoftLow(audio::HarmonyNote(slot == sZL ? 0 : 1, 38, 50), 0.10f, 0.6f); break;
            default: audio::Bell(audio::HarmonyNote(slot - sLSL, 79, 91), 0.024f, true); break;   // SL / SR
        }
        if (!t.vibOn || !t.nvh || !haptic::Enabled()) return;
        const int m = t.nvh == 1 ? 0 : side;
        const bool low = slot >= sL;                        // the shoulders: a soft low knock; the rest: a crisp tick
        t.pulseAt[m] = now; t.pulseDur[m] = low ? 0.045f : 0.024f;
        t.pulseLo[m] = low ? 0.30f : 0.06f; t.pulseHi[m] = low ? 0.10f : 0.30f;
    }

    void Pads::TestUpdate(float now, float dt) {
        Test &t = m_t;
        auto has = [&](int s) { return !(t.pad.kind == kLeft && s == 1) && !(t.pad.kind == kRight && s == 0); };
#ifndef BRUME_UISIM
        // the controller tested = the last one that pressed a button (another one takes over; it is announced)
        for (int i = 0; i < m_n; i++) {
            HidNpadCommonState st;
            if (!Read(m_pad[i].id, m_pad[i].style, st)) continue;
            const int k = IdIndex(m_pad[i].id);
            const u64 b = st.buttons & kTestMask, fresh = b & ~t.seen[k];
            t.seen[k] = b;
            if (fresh && k != t.cur) {
                TestSelect(k, now);
                t.prev &= ~fresh;                           // (the press that chose it rings)
                char nm[48], msg[96];
                Name(t.pad, nm, sizeof nm);
                snprintf(msg, sizeof msg, lang::T("On teste : %s"), nm);
                notify::Post(now, msg);
            }
        }
        {   // the one tested is still there? (its battery, its Joy-Con: refreshed)
            bool still = false;
            for (int i = 0; i < m_n; i++) if (IdIndex(m_pad[i].id) == t.cur) { t.pad = m_pad[i]; still = true; }
            if (t.cur >= 0 && !still) { TestQuiet(); t.cur = -1; }
            if (t.cur < 0 && m_n) TestSelect(IdIndex(m_pad[0].id), now);
        }
#endif
        HidNpadCommonState st;
        const bool ok = t.cur >= 0 && TestRead(st, now);
        u64 b = ok ? st.buttons : 0;
#ifdef BRUME_UISIM
        b |= s_held;                                        // (the preview's script)
#else
        if (t.cur < 0) b |= s_held & HidNpadButton_B;       // (no controller to read: the menu's B still leaves)
#endif
        t.prev = t.btn; t.btn = b;
        for (int i = 0; i < kSlots; i++) {                  // each button: it rings when it goes in, it sinks while held
            const bool on = (b >> kSlotBit[i]) & 1;
            if (on && !((t.prev >> kSlotBit[i]) & 1)) TestPress(i, now);
            Glide(t.sink[i], t.sinkV[i], on ? 1.0f : 0.0f, dt, 26.0f);
        }
        if (ok) {
            const float r[4] = { st.analog_stick_l.x / 32767.0f, st.analog_stick_l.y / 32767.0f, st.analog_stick_r.x / 32767.0f, st.analog_stick_r.y / 32767.0f };
            for (int q = 0; q < 4; q++) {
                t.raw[q] = fmaxf(-1.2f, fminf(1.2f, r[q]));
                t.stk[q] += (fmaxf(-1.05f, fminf(1.05f, r[q])) - t.stk[q]) * (1 - expf(-dt * 40.0f));
            }
        }
        t.trailAt += dt;                                    // the trail: 18 points, 45 a second (0.4 s of the stick's way)
        if (t.trailAt >= 1.0f / 45.0f) {
            t.trailAt = 0;
            for (int s = 0; s < 2; s++) { float *p = t.trail[s][t.head[s]]; p[0] = t.stk[2 * s]; p[1] = t.stk[2 * s + 1]; t.head[s] = (t.head[s] + 1) % kTrail; }
        }
        // the rest: a note on each second of the countdown; moved during it -> again; then the verdict
        if (t.cur >= 0 && t.measAt > 0 && t.doneAt < t.measAt) {
            const float e = now - t.measAt;
            const int tk = (int)e;
            if (tk != t.tick && tk < 3) { t.tick = tk; audio::Bell(audio::HarmonyNote(tk, 79, 91), 0.022f, true); }
            if (e >= 1.0f && e < 3.0f && ok) {
                bool moved = false;
                for (int s = 0; s < 2; s++) for (int a = 0; a < 2; a++) {
                    const float v = t.raw[2 * s + a];
                    t.sum[s][a] += v; t.lo[s][a] = fminf(t.lo[s][a], v); t.hi[s][a] = fmaxf(t.hi[s][a], v);
                    if (has(s) && t.hi[s][a] - t.lo[s][a] > 0.35f) moved = true;
                }
                t.cnt++;
                if (moved) { t.againAt = now; TestMeasure(now); audio::Play(audio::ThockSnd, 0, 0.5f); }
            } else if (e >= 3.0f) {
                bool drift = false, any = false;
                for (int s = 0; s < 2; s++) {
                    if (!has(s) || !t.cnt) continue;
                    const float mx = (float)(t.sum[s][0] / t.cnt), my = (float)(t.sum[s][1] / t.cnt);
                    t.rest[t.cur][s] = sqrtf(mx * mx + my * my);
                    any = true;
                    if (t.rest[t.cur][s] >= kDead) drift = true;
                }
                t.doneAt = now;
                if (!any) {}
                else if (!drift) {                          // all well: the chord rises
                    audio::Bell(audio::HarmonyNote(0, 74, 86), 0.034f, true);
                    audio::Bell(audio::HarmonyNote(2, 79, 91), 0.028f, true, 0.12f);
                    audio::Bell(audio::HarmonyNote(1, 86, 98), 0.02f, true, 0.24f);
                } else {                                    // a stick drifts: a low, soft note (never an alarm)
                    audio::SoftLow(audio::HarmonyNote(0, 38, 50), 0.10f, 0.9f);
                    audio::Bell(audio::HarmonyNote(3, 67, 79), 0.026f, true, 0.08f);
                }
            }
        }
        // the holds: B leaves (a short press only lights it - and says to hold it), X measures again
        if (b & HidNpadButton_B) { t.holdB += dt; if (t.holdB >= kHold) { TestEnd(now); return; } }
        else { if (t.holdB > 0 && t.holdB < 0.35f) t.bTapAt = now; t.holdB = 0; }
        if (b & HidNpadButton_X) {
            if (t.holdX >= 0) { t.holdX += dt; if (t.holdX >= kHold && t.cur >= 0) { t.holdX = -1; audio::Play(audio::Tick2400, 0, 1.0f); TestMeasure(now); } }
        } else t.holdX = 0;
#ifndef BRUME_UISIM
        // the gyroscope: the controller's turn against a reference that follows it slowly (held still, the plate settles flat)
        if (t.sixOn) {
            HidSixAxisSensorState s6;
            if (hidGetSixAxisSensorStates(t.six, &s6, 1) > 0 && (s6.attributes & HidSixAxisSensorAttribute_IsConnected)) {
                float q[4];
                QFromDir(s6.direction.direction, q);
                if (!t.qInit) { memcpy(t.q0, q, sizeof q); t.qInit = true; }
                if (t.q0[0] * q[0] + t.q0[1] * q[1] + t.q0[2] * q[2] + t.q0[3] * q[3] < 0) for (float &c : q) c = -c;
                const float kq = 1 - expf(-dt / 3.0f);
                for (int i = 0; i < 4; i++) t.q0[i] += (q[i] - t.q0[i]) * kq;
                QNorm(t.q0);
                const float c0[4] = { -t.q0[0], -t.q0[1], -t.q0[2], t.q0[3] };
                QMul(c0, q, t.qr);
                t.sixSeen = true;
            }
        }
#endif
        // its motors: one short pulse per side, then quiet (one zero sent, then no traffic)
        if (t.vibOn && t.nvh) {
            HidVibrationValue v[2];
            bool any = false;
            for (int s = 0; s < 2; s++) {
                const float e = now - t.pulseAt[s];
                const bool live = e >= 0 && e < t.pulseDur[s] && haptic::Enabled();
                v[s].amp_low = live ? t.pulseLo[s] : 0; v[s].freq_low = 160; v[s].amp_high = live ? t.pulseHi[s] : 0; v[s].freq_high = 320;
                any = any || live;
            }
            if (any || !t.vibZero) { hidSendVibrationValues(t.vh, v, t.nvh); t.vibZero = !any; }
        }
    }

    void Pads::TestTap(float x, float y, float now) {
        if (!m_t.open) return;
        const float hc = 720 - 28 - 13.5f;
        if (fabsf(y - hc) > 28) return;
        if (x >= m_t.hint[0][0] - 14 && x <= m_t.hint[0][1] + 14) { TestEnd(now); return; }
        if (m_t.cur >= 0 && x >= m_t.hint[1][0] - 14 && x <= m_t.hint[1][1] + 14) { audio::Play(audio::Tick2400, 0, 1.0f); TestMeasure(now); }
    }

    void Pads::DrawTest(Ui &ui, float now) {
        Test &t = m_t;
        const float k = t.open ? EaseGlide((now - t.openAt) / Pace(0.6f)) : 1 - EaseGlide((now - t.closeAt) / Pace(0.4f));
        if (k <= 0.003f) return;
        // each part comes in its turn (after the settings have shrunk away); leaving, everything goes together
        auto In = [&](float delay) { return t.open ? EaseGlide((now - t.openAt - delay) / Pace(0.55f)) : k; };
        auto has = [&](int s) { return !(t.pad.kind == kLeft && s == 1) && !(t.pad.kind == kRight && s == 0); };
        // the title: yellow, in its warm glow (the PS2 look)
        const float aT = In(0.12f);
        {
            const float tw = ui.Width(fonts::kPageTitle, lang::T("Tester les manettes"), 1.5f);
            ui.Light(640 - tw / 2 - 70, 84, 640 + tw / 2 + 70, 160, ps2::A(ps2::kTitleGlow, 0.13f * aT), true);
            ui.TextCentered(fonts::kPageTitle, "Tester les manettes", 640, 96 + (1 - aT) * 12, 1.5f, ps2::A(ps2::kTitle, aT));
        }
        const float mt = now - t.measAt;
        const bool measuring = t.cur >= 0 && t.measAt > 0 && t.doneAt < t.measAt;
        if (t.cur < 0) {
            ui.TextCentered(fonts::kGameItem, "Aucune manette connectée.", 640, 330, 0, ps2::A(ps2::kWords, 0.9f * In(0.25f)));
        } else {
            {   // which controller: its two colours, its name and player (a new one: it crossfades in)
                const float aN = In(0.2f) * (0.2f + 0.8f * EaseGlide((now - t.switchAt) / Pace(0.45f)));
                char nm[48], up[64], who[32], whoU[40], line[112];
                Name(t.pad, nm, sizeof nm);
                Upper(nm, up, sizeof up);
                if (t.pad.kind == kHandheld) snprintf(who, sizeof who, "%s", lang::T("Sur la console"));
                else snprintf(who, sizeof who, lang::T("Joueur %d"), (int)t.pad.id + 1);
                Upper(who, whoU, sizeof whoU);
                snprintf(line, sizeof line, "%s  \xc2\xb7  %s", up, whoU);
                const float lw = ui.Width(fonts::kGameInfo, line, 2);
                Dot(ui, 640 - lw / 2 - 30, 161, 5.5f, Col(t.pad.l.main, aN, 1.0f, 0.15f));
                Dot(ui, 640 - lw / 2 - 16, 161, 5.5f, Col(t.pad.r.main, aN, 1.0f, 0.15f));
                ui.TextCentered(fonts::kGameInfo, line, 640, 154.5f, 2, ps2::A(ps2::kChoice, aN));
            }
            {   // the countdown's words (moved: say it, softly), fading out once measured
                const bool again = measuring && now - t.againAt < 1.6f;
                const float sa = measuring ? EaseGlide(mt / Pace(0.4f)) : (t.doneAt > 0 ? 1 - EaseGlide((now - t.doneAt) / Pace(0.5f)) : 0.0f);
                if (sa > 0.003f)
                    ui.TextCentered(fonts::kHintBig, again ? "Tu as bougé un stick : on recommence" : "Lâche les sticks : on mesure leur repos",
                                    640, 188, 0.3f, ps2::A(again ? ps2::kPink : ps2::kWords, 0.9f * sa * In(0.25f)));
            }
            // ---- the two sticks
            for (int s = 0; s < 2; s++) {
                const float aS = In(0.22f + 0.10f * s) * (has(s) ? 1.0f : 0.35f);
                if (aS <= 0.003f) continue;
                const float cx = kSX[s], cy = kSY, R = kSR;
                const float rest = t.rest[t.cur][s];
                const int verdict = rest < 0 ? -1 : rest < 0.08f ? 0 : rest < kDead ? 1 : 2;
                const Rgba vc = verdict == 0 ? ps2::kGreen : verdict == 1 ? ps2::kTitle : verdict == 2 ? ps2::kPink : ps2::kChoice;
                const float vk = verdict < 0 ? 0.0f : 0.5f * EaseGlide((now - t.doneAt) / Pace(0.8f));   // its ring takes the verdict's colour
                // a soft cloud behind it, its two axes
                ui.Light(cx - R * 1.4f, cy - R * 1.4f, cx + R * 1.4f, cy + R * 1.4f, ps2::A(ps2::kHalo, 0.11f * aS), true);
                ui.Light(cx - R, cy - 2.5f, cx + R, cy + 2.5f, ps2::A(ps2::kChoice, 0.18f * aS));
                ui.Light(cx - 2.5f, cy - R, cx + 2.5f, cy + R, ps2::A(ps2::kChoice, 0.18f * aS));
                // its edge: a ring of light points that come round one by one; the countdown lights them in turn
                const float lit = measuring ? Clamp01((mt - 1.0f) / 2.0f) : 0.0f;
                const Rgba rc = ps2::Mix(ps2::kChoice, vc, vk);
                for (int i = 0; i < 36; i++) {
                    const float f = i / 36.0f, ang = f * 6.2831853f - 1.5707963f;
                    const float ai = Clamp01((aS - f * 0.45f) * 2.2f);
                    if (ai <= 0.01f) continue;
                    const bool on = measuring && f < lit;
                    const float r = on ? 7.0f : 5.0f;
                    Glow(ui, cx + cosf(ang) * R, cy + sinf(ang) * R, r, r, ps2::A(on ? ps2::kChosen : rc, (on ? 0.95f : 0.6f) * ai));
                }
                // the menu's dead zone: a small ring, faint
                for (int i = 0; i < 10; i++) {
                    const float ang = i * 0.62831853f;
                    Glow(ui, cx + cosf(ang) * R * kDead, cy + sinf(ang) * R * kDead, 3.4f, 3.4f, ps2::A(ps2::kFaint, 0.6f * aS));
                }
                if (measuring && has(s) && mt < 3.0f) {    // 3, 2, 1 - each second fades
                    char dg[12]; snprintf(dg, sizeof dg, "%d", 3 - (int)mt);
                    const float fr = mt - (int)mt;
                    ui.TextCentered(fonts::kPageTitle, dg, cx, cy - R * 0.5f - 22, 0, ps2::A(ps2::kTitle, aS * (1 - 0.75f * fr) * Clamp01(mt * 5 + 0.2f)));
                }
                if (has(s)) {
                    // the trail (the stick's last 0.4 s), the line from the centre, the live position (it sinks when clicked)
                    for (int j = kTrail - 1; j >= 1; j--) {
                        const float *p = t.trail[s][(t.head[s] - 1 - j + 2 * kTrail) % kTrail];
                        const float f = 1 - j / (float)kTrail, r = 4 + 6 * f;
                        Glow(ui, cx + p[0] * R, cy - p[1] * R, r, r, ps2::A(ps2::kHalo, 0.5f * f * f * aS));
                    }
                    const float px = cx + t.stk[2 * s] * R, py = cy - t.stk[2 * s + 1] * R;
                    for (int j = 1; j <= 4; j++) Glow(ui, cx + (px - cx) * j / 5.0f, cy + (py - cy) * j / 5.0f, 3.0f, 3.0f, ps2::A(ps2::kChoice, 0.4f * aS));
                    const int sl = s ? sR3 : sL3;
                    const float sk = t.sink[sl], rp = now - t.at[sl];
                    if (rp >= 0 && rp < 0.6f) {               // clicked: a wave of light from it
                        const float e = rp / 0.6f, rr = 14 + 70 * EaseGlide(e);
                        ui.Light(px - rr, py - rr, px + rr, py + rr, ps2::A(ps2::kHalo, 0.55f * (1 - e) * aS), true);
                    }
                    const Rgba ac = Settings::Accent(1.0f);
                    ui.Light(px - 34, py - 34, px + 34, py + 34, Rgba{ ac.r, ac.g, ac.b, (0.35f + 0.45f * sk) * aS }, true);
                    Dot(ui, px, py, 7.5f * (1 - 0.22f * sk), ps2::A(ps2::kChosen, aS));
                }
                // its words: which one, where it is, its rest
                ui.TextCentered(fonts::kGameInfo, s ? "STICK DROIT" : "STICK GAUCHE", cx, cy + R + 22, 2, ps2::A(ps2::kFaint, aS));
                if (has(s)) {
                    char v1[16], v2[16], val[48];
                    Num(v1, sizeof v1, t.raw[2 * s], true); Num(v2, sizeof v2, t.raw[2 * s + 1], true);
                    snprintf(val, sizeof val, "x %s     y %s", v1, v2);
                    ui.TextCentered(fonts::kHint, val, cx, cy + R + 44, 0.3f, ps2::A(ps2::kWords, 0.85f * aS));
                    if (verdict >= 0) {
                        char n[16], vl[80];
                        Num(n, sizeof n, rest, false);
                        const char *fmt = verdict == 0 ? lang::T("au repos : %s → ok") : verdict == 1 ? lang::T("au repos : %s → léger écart")
                                                                                       : lang::T("au repos : %s → dérive possible");
                        snprintf(vl, sizeof vl, fmt, n);
                        ui.TextCentered(fonts::kHint, vl, cx, cy + R + 67, 0.3f, ps2::A(vc, aS * EaseGlide((now - t.doneAt) / Pace(0.5f))));
                    } else if (measuring)
                        ui.TextCentered(fonts::kHint, "mesure en cours", cx, cy + R + 67, 0.3f, ps2::A(ps2::kFaint, aS * (0.6f + 0.3f * sinf(now * 5))));
                } else ui.TextCentered(fonts::kHint, "pas de stick", cx, cy + R + 44, 0.3f, ps2::A(ps2::kFaint, aS));
            }
            // ---- the buttons, in the middle: they light up, sink in, a wave of light leaves each press
            const float aB = In(0.32f);
            auto Halo = [&](int slot, float x, float y, float r) {
                const float sk = t.sink[slot], e = now - t.at[slot];
                if (sk > 0.01f) ui.Light(x - r, y - r, x + r, y + r, ps2::A(ps2::kHalo, 0.6f * sk * aB), true);
                if (e >= 0 && e < 0.55f) {
                    const float q = e / 0.55f, rr = r * (0.6f + 1.7f * EaseGlide(q));
                    ui.Light(x - rr, y - rr, x + rr, y + rr, ps2::A(ps2::kChoice, 0.3f * (1 - q) * aB), true);
                }
            };
            {   // L / R / ZL / ZR: gauges that fill with light as they go in (a GameCube controller's L / R: as far as pressed)
                struct Sh { int slot; float x; const char *lbl; int trig; };
                const Sh sh[4] = { { sZL, 488, "ZL", -1 }, { sL, 564, "L", 0 }, { sR, 716, "R", 1 }, { sZR, 792, "ZR", -1 } };
                const float y = 230;
                for (const Sh &g : sh) {
                    float f = t.sink[g.slot];
                    if (g.trig >= 0 && t.trig[g.trig] >= 0) f = fmaxf(f, t.trig[g.trig]);
                    Halo(g.slot, g.x, y, 32);
                    ui.Light(g.x - 32, y - 6, g.x + 32, y + 6, ps2::A(ps2::kChoice, 0.3f * aB));
                    if (f > 0.01f) ui.Light(g.x - 32 * f, y - 9, g.x + 32 * f, y + 9, ps2::A(ps2::kChosen, 0.85f * f * aB));
                    ui.TextCentered(fonts::kGameItem, g.lbl, g.x, y - 38 + 3 * t.sink[g.slot], 1, ps2::Line(f, aB));
                }
            }
            {   // - and +
                const struct { int slot; float x; uint32_t c; } pm[2] = { { sMinus, 606, '-' }, { sPlus, 674, '+' } };
                for (const auto &b : pm) {
                    const float y = 276, sk = t.sink[b.slot], adv = ui.Advance(fonts::kGameItem, b.c);
                    Halo(b.slot, b.x, y, 22);
                    ui.SetXform(1.5f * (1 - 0.2f * sk), b.x, y, b.x, y);
                    ui.Glyph(fonts::kGameItem, b.c, b.x - adv / 2, y - 10.5f, ps2::Line(sk, aB), 0, 0, 0, 0);
                    ui.SetZoom(1.0f);
                }
            }
            {   // the directions: small triangles of light
                const float dx = 562, dy = 348, o = 34;
                const struct { int slot; float x, y; int dir; } dp[4] = { { sUp, dx, dy - o, 2 }, { sRight, dx + o, dy, 1 }, { sDown, dx, dy + o, 3 }, { sLeft, dx - o, dy, 0 } };
                for (const auto &d : dp) {
                    const float sk = t.sink[d.slot];
                    Halo(d.slot, d.x, d.y, 28);
                    ui.Arrow(d.x, d.y, 12.0f * (1 - 0.2f * sk), d.dir, ps2::Line(sk, aB));
                }
            }
            {   // A B X Y: their letters in their colours, near-white when pressed
                const float fx = 718, fy = 348, o = 34;
                const struct { int slot; float x, y; uint32_t c; Rgba col; } fb[4] = {
                    { sX, fx, fy - o, 'X', ps2::kChoice }, { sA, fx + o, fy, 'A', ps2::kChoice }, { sB, fx, fy + o, 'B', ps2::kPink }, { sY, fx - o, fy, 'Y', ps2::kGreen } };
                for (const auto &b : fb) {
                    const float sk = t.sink[b.slot], adv = ui.Advance(fonts::kButton, b.c);
                    Halo(b.slot, b.x, b.y, 28);
                    ui.SetXform(1.35f * (1 - 0.18f * sk), b.x, b.y, b.x, b.y);
                    ui.Glyph(fonts::kButton, b.c, b.x - adv / 2, b.y - 10, ps2::A(ps2::Mix(b.col, ps2::kChosen, sk), aB), 0, 0, 0, 0);
                    ui.SetZoom(1.0f);
                }
            }
            if (t.pad.kind == kPair || t.pad.kind == kLeft || t.pad.kind == kRight) {   // a Joy-Con's SL / SR (on its rail)
                const struct { int slot; float x; const char *l; bool left; } ss[4] = {
                    { sLSL, 538, "SL", true }, { sLSR, 590, "SR", true }, { sRSL, 690, "SL", false }, { sRSR, 742, "SR", false } };
                for (const auto &q : ss) {
                    if ((q.left && t.pad.kind == kRight) || (!q.left && t.pad.kind == kLeft)) continue;
                    Halo(q.slot, q.x, 424, 20);
                    ui.TextCentered(fonts::kHint, q.l, q.x, 416 + 2 * t.sink[q.slot], 1, ps2::Line(t.sink[q.slot], aB));
                }
            }
            // ---- the gyroscope: a plate of light points, turned like the controller (seen from a little above)
            {
                const float aG = In(0.42f) * (t.sixOn ? 1.0f : 0.45f);
                const float gx = 640, gy = 506, S = 58, sE = 0.82f, cE = 0.57f, D = 210;   // (elevation ~55°)
                const float *q = t.sixSeen ? t.qr : nullptr;
                const float one[4] = { 0, 0, 0, 1 };
                for (int j = 0; j < 6; j++) for (int i = 0; i < 6; i++) {
                    const float p[3] = { (-1 + i * 0.4f) * S, (-1 + j * 0.4f) * S, 0 };
                    float r[3];
                    QRotate(q ? q : one, p, r);
                    const float up = r[1] * sE + r[2] * cE, dep = r[1] * cE - r[2] * sE, pr = D / fmaxf(40.0f, D + dep);
                    const float a = (0.3f + 0.55f * Clamp01((pr - 0.7f) * 1.6f)) * aG * (j == 5 ? 1.35f : 1.0f), rad = (j == 5 ? 5.4f : 4.6f) * pr;
                    Glow(ui, gx + r[0] * pr, gy - up * pr, rad, rad, ps2::A(j == 5 ? ps2::kTitle : ps2::kChoice, a));   // (the far edge, yellow: its top)
                }
                ui.Light(gx - 36, gy - 26, gx + 36, gy + 26, ps2::A(ps2::kHalo, 0.18f * aG), true);
                ui.TextCentered(fonts::kGameInfo, t.sixOn ? "GYROSCOPE" : "PAS DE GYROSCOPE", gx, gy + 80, 2, ps2::A(ps2::kFaint, aG));
            }
        }
        // ---- the hints (the button's letter in colour, then its words): hold B to leave, hold X to measure again
        const float aH = In(0.45f);
        const float hc = 720 - 28 - 13.5f;
        float hx = 46;
        auto Hint = [&](char btn, const char *label, float hold, float flash, float *span) {
            const Rgba bc = btn == 'B' ? ps2::kPink : ps2::kChoice;
            span[0] = hx;
            if (flash > 0.01f) ui.Light(hx - 24, hc - 22, hx + 300, hc + 22, ps2::A(ps2::kHalo, 0.32f * flash * aH), true);
            hx += ui.Glyph(fonts::kButton, (uint32_t)btn, hx, hc - 10, ps2::A(bc, aH), 0, 0, 0, 0) + 10;
            const float tx = hx;
            const Rgba wc = ps2::Mix(ps2::kWords, ps2::kChosen, flash);
            for (const char *p = lang::T(label); *p;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(p), hx, hc - 9.5f, ps2::A(wc, 0.9f * aH), 0, 0, 0, 0) + 0.5f;
            if (hold > 0.01f) RR(ui, tx, hc + 13, tx + (hx - tx) * fminf(1.0f, hold), hc + 15.5f, 1.2f, ps2::A(ps2::kChosen, 0.9f * aH));   // the hold fills a line of light
            span[1] = hx;
            hx += 30;
        };
        const float eb = now - t.bTapAt, flashB = eb >= 0 && eb < 2.0f ? expf(-eb * 2.0f) * (0.75f + 0.25f * cosf(eb * 14.0f)) : 0.0f;
        Hint('B', "Maintenir : quitter", t.holdB / kHold, flashB, t.hint[0]);
        if (t.cur >= 0) Hint('X', "Maintenir : mesurer à nouveau", t.holdX > 0 ? t.holdX / kHold : 0.0f, 0.0f, t.hint[1]);
        else t.hint[1][0] = t.hint[1][1] = -1000;
    }

} // namespace brume
