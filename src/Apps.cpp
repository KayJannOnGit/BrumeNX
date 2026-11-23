// Brume - v35/v36: the Apps page. See Apps.hpp.
// The .nro format (switchbrew « NRO »): header at 0x10 ('NRO0', u32 size at 0x18); right after `size`, the assets:
// 'ASET', u32 version, then {u64 offset, u64 size} for the icon (JPEG), the NACP, the RomFS - offsets from 'ASET'.
// NACP: 16 x { name[0x200], author[0x100] } (French = 3, American English = 0), display version at 0x3060.
// Everything read here is checked (sizes, bounds): a broken file is skipped, never a crash.
#include "Apps.hpp"
#include "Lang.hpp"
#include "Games.hpp"
#include "Wheel.hpp"
#include "Mesh3d.hpp"
#include "Ease.hpp"
#include "Audio.hpp"
#include "Busy.hpp"
#include "Haptics.hpp"
#include "Worker.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

namespace brume {

    bool DecodeJpegSquare(const void *data, int bytes, uint8_t *out, int size);   // Jpeg.cpp

    void (*Apps::s_log)(const char *) = nullptr;

    namespace {
        uint8_t s_nacp[0x3070];
        uint8_t s_icon[0x40000];                       // an icon (JPEG, ~20-100 KB)
        uint8_t s_rgba[256 * 256 * 4];
        // v90: the worker's job - the list (Scan), or one icon: read, decoded, in BC1 (in s_rgba itself: EncodeBC1 can write
        // over the picture it reads), the frame only sends it
        enum { kJobScan, kJobIcon };
        struct Job { int kind; char path[256]; uint32_t off, size; int ok; };
        Job    s_job = {};
        Worker s_work;
        // the ring (world units, like the consoles' wheel: RING 5, its centre at z -5): the front cube at z ~.8
        constexpr float kR = 5.0f, kZc = -4.2f, kStep = 0.36f, kSize = 1.3f, kY = 0.55f, kRingW = 6.0f;
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        bool EndsNro(const char *s) { const size_t n = strlen(s); return n > 4 && strcasecmp(s + n - 4, ".nro") == 0; }
        void CopyText(char *dst, size_t cap, const uint8_t *src, size_t max) {   // a NACP string: may lack its end
            size_t n = 0;
            while (n < max && n + 1 < cap && src[n]) { dst[n] = (char)src[n]; n++; }
            dst[n] = 0;
        }
    }

    bool Apps::Visible(const Wheel &w) const { return w.Active() && fabsf(w.PageAnim() - kPage) < 1.0f; }

    bool Apps::Read(const char *path, App &a) {
        memset(&a, 0, sizeof a);
        if (snprintf(a.path, sizeof a.path, "%s", path) >= (int)sizeof a.path) return false;
        // the name by default: the file's, without « .nro »
        const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
        snprintf(a.name, sizeof a.name, "%s", base);
        if (char *dot = strrchr(a.name, '.')) *dot = 0;
        a.noIcon = true;
        FILE *f = fopen(path, "rb");
        if (!f) return false;
        uint8_t hdr[0x80];
        bool ok = fread(hdr, 1, sizeof hdr, f) == sizeof hdr && memcmp(hdr + 0x10, "NRO0", 4) == 0;
        uint32_t size = 0;
        if (ok) memcpy(&size, hdr + 0x18, 4);
        fseek(f, 0, SEEK_END);
        const long total = ftell(f);
        ok = ok && size >= 0x80 && (long)size < total;
        uint8_t aset[0x38];
        if (ok && fseek(f, (long)size, SEEK_SET) == 0 && fread(aset, 1, sizeof aset, f) == sizeof aset && memcmp(aset, "ASET", 4) == 0) {
            uint64_t io, is, no, ns;
            memcpy(&io, aset + 0x08, 8); memcpy(&is, aset + 0x10, 8); memcpy(&no, aset + 0x18, 8); memcpy(&ns, aset + 0x20, 8);
            if (ns >= sizeof s_nacp && no < (uint64_t)total && size + no + sizeof s_nacp <= (uint64_t)total &&
                fseek(f, (long)(size + no), SEEK_SET) == 0 && fread(s_nacp, 1, sizeof s_nacp, f) == sizeof s_nacp) {
                const int langs[2] = { 3, 0 };                      // French, then American English, then the first one there
                int pick = -1;
                for (int l : langs) if (s_nacp[l * 0x300]) { pick = l; break; }
                for (int l = 0; l < 16 && pick < 0; l++) if (s_nacp[l * 0x300]) pick = l;
                if (pick >= 0) {
                    CopyText(a.name, sizeof a.name, s_nacp + pick * 0x300, 0x200);
                    CopyText(a.author, sizeof a.author, s_nacp + pick * 0x300 + 0x200, 0x100);
                }
                CopyText(a.version, sizeof a.version, s_nacp + 0x3060, 0x10);
            }
            if (is > 64 && is <= sizeof s_icon && size + io + is <= (uint64_t)total) {
                a.iconOff = (uint32_t)(size + io); a.iconSize = (uint32_t)is; a.noIcon = false;
            }
        }
        fclose(f);
        return ok;
    }

    void Apps::Scan() {
        int &m_n = m_scanN;                              // v90: the worker fills m_scanN (m_n is the main thread's, 0 meanwhile)
        m_n = 0;
        DIR *d = opendir("sdmc:/switch");
        if (!d) return;
        while (struct dirent *e = readdir(d)) {
            if (m_n >= kMax) break;
            if (e->d_name[0] == '.') continue;
            char p[256];
            if (snprintf(p, sizeof p, "sdmc:/switch/%s", e->d_name) >= (int)sizeof p) continue;
            if (EndsNro(p)) { if (Read(p, m_a[m_n])) m_n++; continue; }
            if (DIR *sub = opendir(p)) {                           // one folder down (sdmc:/switch/<app>/<app>.nro)
                int here = 0;
                while (struct dirent *s = readdir(sub)) {
                    if (m_n >= kMax || here >= 4) break;
                    if (s->d_name[0] == '.' || !EndsNro(s->d_name)) continue;
                    char q[256];
                    if (snprintf(q, sizeof q, "%s/%s", p, s->d_name) >= (int)sizeof q) continue;
                    if (Read(q, m_a[m_n])) { m_n++; here++; }
                }
                closedir(sub);
            }
        }
        closedir(d);
        if (m_n < kMax && Read("sdmc:/hbmenu.nro", m_a[m_n])) m_n++;   // v42.2: the homebrew menu itself (at the card's root)
        for (int i = 0; i < m_n; i++)                                   // v58: our Brume Launcher is a tool, not an app
            if (!strcasecmp(m_a[i].name, "Brume Launcher") || !strcasecmp(m_a[i].name, "Brume Lanceur")) { m_a[i] = m_a[--m_n]; i--; }
        // alphabetical (insertion sort, few items)
        for (int a = 1; a < m_n; a++)
            for (int b = a; b > 0 && strcasecmp(m_a[b].name, m_a[b - 1].name) < 0; b--) { const App t = m_a[b]; m_a[b] = m_a[b - 1]; m_a[b - 1] = t; }
    }

    void Apps::Select(int i, float now) {
        if (i == m_sel) return;
        const int dir = i > m_sel ? 1 : -1;
        m_sel = i; m_moveAt = now;
        audio::Play(audio::Deplacement, dir * 0.3f, 1.0f);
        audio::PlaySweep(audio::Whoosh, dir * 0.5f, -dir * 0.4f, 0.7f, 0.35f);
        haptic::Tick(0.6f);
    }

    void Apps::Move(int step, float now) {
        if (!m_n || m_launch >= 0) return;
        const int to = m_sel + step;
        if (to < 0 || to >= m_n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
        Select(to, now);
    }

    void Apps::RingDragBegin() { if (!m_n || m_launch >= 0) return; m_drag = true; m_dragFrom = m_cur; m_curV = 0; }
    void Apps::RingDragTo(float now, float steps) {
        if (!m_drag) return;
        m_cur = fmaxf(-0.4f, fminf(m_n - 0.6f, m_dragFrom + steps));   // the finger holds the ring
        const int t = (int)lroundf(fmaxf(0.0f, fminf((float)(m_n - 1), m_cur)));
        if (t != m_sel) Select(t, now);
    }
    void Apps::RingDragEnd(float now, float v) {
        if (!m_drag) return;
        m_drag = false;
        m_curV = v;                                       // the ring keeps the finger's speed...
        int t = (int)FlickTarget(m_cur, v, kRingW);      // ...and settles on the next cube (or a few, thrown hard)
        t = t < 0 ? 0 : (t >= m_n ? m_n - 1 : t);
        Select(t, now);
    }

    void Apps::PressA(float now) {
        if (!m_n || m_launch >= 0) return;
        m_launch = m_sel; m_launchAt = now; m_started = false; m_status[0] = 0;
        audio::Play(audio::Tick2400);
        audio::Bell(audio::HarmonyNote(0, 72, 84), 0.05f, true, 0.05f);
        // v80 (Yass: « le son se coupe d'un coup »): the soundscape (and the bell's tail) fades out under the entry sound - the
        // cube comes at us: the dive - and the homebrew starts once it has gone into silence (Update: kEntryLen)
        audio::Depart(0.3f);
        audio::EntrySound(true, 0.0f);
        haptic::Swell(0.8f, 0.6f);
    }

    bool Apps::PressB(float now) {
        (void)now;
        return m_launch < 0;
    }

    void Apps::Tap(float x, float y, float now) {
        for (int k = 0; k < m_nhit; k++) {
            const Hit &h = m_hit[k];
            if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) {
                if (h.i == m_sel) PressA(now);
                else Select(h.i, now);
                return;
            }
        }
    }

    void Apps::Done() {
        if (m_launch >= 0 && m_backAt < 0) { m_backAt = 0; m_settle.on = false; }   // the light comes back (Update: v80, once the frames are steady; a 2nd onOpen: once)
    }

    void Apps::Update(const Wheel &w, float now, float dt) {
        // v73.3 (Yass: a fast trip across the pages froze ~3 times on the way): read only when THIS page is where the
        // slide goes and it is nearly there - a page crossed on the way to another one reads nothing
        Collect();
        if (Visible(w) && !m_listed && !m_scanning && m_iconFor < 0 && s_work.Idle() && w.Page() == kPage && fabsf(w.PageAnim() - kPage) < 0.35f) {
            m_n = 0;                                      // v90: the list is read by the worker (the orbs turn meanwhile)
            m_scanning = true;
            s_job.kind = kJobScan;
            if (s_work.Start(Work, this, 0x10000)) Collect();   // (no thread: read at once, as before)
            else m_scanning = false;
        }
        if (m_scanning && !Visible(w) && s_work.Idle()) m_scanning = false;
        if (!Visible(w) && m_listed && m_launch < 0) m_listed = false;   // read again next time
        if (m_iconFor >= 0 && s_work.Idle() && (!m_listed || m_iconGen != m_gen)) { m_iconReady = false; m_iconFor = -1; }   // (an old list's icon)
        if (!m_drag) Glide(m_cur, m_curV, (float)m_sel, dt, kRingW);
        if (m_backAt == 0) {                            // v80: back from the homebrew - black until the frames are steady (Settle),
            if (!m_settle.on) m_settle.Begin(now);      // then the light, the exit sound, the soundscape fading back in
            if (m_settle.Step(now, dt)) { m_backAt = now; audio::Arrive(1.3f); audio::ExitSound(0.0f); }
        }
        if (m_launch >= 0 && !m_started && m_backAt < 0 && now - m_launchAt > audio::kEntryLen) {   // black and silent: start it
            m_started = true;
            const u64 fw = Games::ForwarderFor(m_a[m_launch].name);   // v51: its real forwarder (made by sphaira) first
            const bool ok = fw ? Games::LaunchTitle(fw, m_status, sizeof m_status) : Games::LaunchNro(m_a[m_launch].path, nullptr, m_status, sizeof m_status);
            if (!ok) { m_statusAt = now; m_backAt = now; audio::Arrive(0.8f); }
        }
        if (m_backAt > 0 && now - m_backAt > Pace(0.8f)) { m_launch = -1; m_backAt = -100; m_started = false; }
        if (m_launch >= 0 && m_started && m_backAt < 0 && now - m_launchAt > 8.0f) { m_backAt = now; audio::Arrive(0.8f); }   // nothing happened
    }

    namespace { bool s_wait = false; }                    // v87: the chosen cube has no icon yet (its orbs)
    void Apps::Stream(Gpu &gpu, Shelf &shelf, const Wheel &w) {
        s_wait = Visible(w) && w.Page() == kPage && m_launch < 0 && (!m_listed || (m_n && !m_a[m_sel].noIcon && shelf.Slots() && !shelf.Has(Key(m_sel))));
        if (!Visible(w) || !m_listed || !m_n || !shelf.Slots()) return;
        shelf.NextFrame();
        int best = -1, bestD = 1 << 30;
        for (int i = m_sel - 6; i <= m_sel + 6; i++) {    // the cubes in sight, nearest first
            if (i < 0 || i >= m_n || m_a[i].noIcon || shelf.Has(Key(i))) continue;
            const int d = abs(i - m_sel);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best < 0) return;
        // v90: read, decoded, compressed by the worker; the frame only sends it (this frame without a thread, as before)
        if (!m_iconReady && m_iconFor < 0) {
            if (!s_work.Idle()) return;
            const App &a = m_a[best];
            s_job.kind = kJobIcon; s_job.off = a.iconOff; s_job.size = a.iconSize;
            snprintf(s_job.path, sizeof s_job.path, "%s", a.path);
            m_iconFor = best; m_iconGen = m_gen;
            if (!s_work.Start(Work, this, 0x10000)) { m_iconFor = -1; return; }
            Collect();
        }
        if (!m_iconReady) return;
        const int i = m_iconFor;
        if (m_iconGen != m_gen || i < 0 || i >= m_n) { m_iconReady = false; m_iconFor = -1; return; }
        if (!m_iconOk) { m_a[i].noIcon = true; m_iconReady = false; m_iconFor = -1; return; }
        const int r = shelf.LoadBC1(gpu, Key(i), s_rgba, 256, 256, 1.0f);
        if (r < 0) return;                                // (no place free yet / the frame's upload is taken: next frame)
        if (r == 0) m_a[i].noIcon = true;
        m_iconReady = false; m_iconFor = -1;
    }

    void Apps::Shutdown() { s_work.Wait(); }

    void Apps::Work(void *self) {
        Apps &me = *(Apps *)self;
        Job &j = s_job;
        j.ok = 0;
        if (j.kind == kJobScan) { me.Scan(); j.ok = 1; return; }
        bool ok = false;
        if (j.size > 64 && j.size <= sizeof s_icon)
            if (FILE *f = fopen(j.path, "rb")) {
                ok = fseek(f, (long)j.off, SEEK_SET) == 0 && fread(s_icon, 1, j.size, f) == j.size;
                fclose(f);
            }
        if (ok && DecodeJpegSquare(s_icon, (int)j.size, s_rgba, 256)) { Shelf::EncodeBC1(s_rgba, 256, 256, s_rgba); j.ok = 1; }
    }

    // v90: the worker's job is over: the list (published at once), or an icon (Stream sends it)
    void Apps::Collect() {
        if (!s_work.Done()) return;
        if (s_job.kind == kJobScan) {
            if (!m_scanning) return;                      // (the page was left meanwhile: read again next time)
            m_scanning = false;
            m_n = m_scanN;
            if (m_sel >= m_n) m_sel = m_n ? m_n - 1 : 0;
            m_cur = (float)m_sel; m_curV = 0;
            m_gen = (m_gen + 1) % 1000;
            m_listed = true;
            if (s_log) { char b[64]; snprintf(b, sizeof b, "apps : %d homebrew(s)", m_n); s_log(b); }
        } else { m_iconOk = s_job.ok != 0; m_iconReady = true; }
    }

    // the ring of glass cubes: like the consoles' wheel (seen from the front, the chosen one in the middle, the others going
    // round into the fog behind); each cube bobs at its own pace, the chosen one turns slowly to show its glass
    void Apps::DrawCards(Gpu &gpu, const Assets &as, const Target &haze, float now, const Wheel &w, Shelf &shelf) {
        m_nhit = 0;
        if (!Visible(w) || !m_listed || !m_n || !shelf.Ready()) return;
        using namespace m3;
        float eye[3], at[3];
        w.CameraAt(now, eye, at);
        const float aspect = (float)gpu.Width() / gpu.Height();
        const M4 vp = Mul(Persp(38, aspect, 0.3f, 300), LookAt(eye, at));
        const float away = (kPage - w.PageAnim()) * 16.0f;   // the page slides like the consoles do (x -= pageAway * 16)
        const float fly = m_launch >= 0 ? (m_backAt > 0 ? 1 - EaseGlide((now - m_backAt) / Pace(0.7f)) : EaseGlide((now - m_launchAt) / Pace(0.9f))) : 0.0f;
        if (!gpu.MeshDrawn()) gpu.BeginMesh();
        MeshUbo ub = Wheel::s_lights;
        ub.cam[0] = eye[0]; ub.cam[1] = eye[1]; ub.cam[2] = eye[2]; ub.cam[3] = 1.0f;
        ub.base[0] = 0.02f; ub.base[1] = 0.025f; ub.base[2] = 0.05f; ub.base[3] = 1;
        gpu.SetMeshState(true, false, false);
        // nearest first (the depth test skips what is hidden behind)
        int order[16], no = 0;
        for (int i = (int)floorf(m_cur) - 7; i <= (int)ceilf(m_cur) + 7 && no < 16; i++) if (i >= 0 && i < m_n) order[no++] = i;
        for (int p = 1; p < no; p++)
            for (int q = p; q > 0 && fabsf(order[q] - m_cur) < fabsf(order[q - 1] - m_cur); q--) { const int t = order[q]; order[q] = order[q - 1]; order[q - 1] = t; }
        for (int k = 0; k < no; k++) {
            const int i = order[k];
            const float d = i - m_cur, ang = d * kStep;
            const float vis = Clamp01((2.1f - fabsf(ang)) / 0.45f);   // the far ones melt into the fog behind
            if (vis <= 0.003f) continue;
            const float focus = fmaxf(0.0f, 1 - fabsf(d)), f01 = (cosf(ang) + 1) * 0.5f;
            float x = sinf(ang) * kR + away, z = kZc + cosf(ang) * kR;
            float y = kY + sinf(now * 0.7f + i * 1.3f) * 0.06f + (1 - f01) * 0.35f;
            float s = kSize * (0.82f + 0.18f * focus) * (0.25f + 0.75f * vis);
            float yaw = ang * 0.85f + focus * sinf(now * 0.5f + 0.6f) * 0.24f, pitch = -0.10f + focus * sinf(now * 0.37f) * 0.05f;
            float light = 1.0f;
            if (i == m_launch) {                            // launching: the cube comes at us
                x += (0.0f - x) * fly; z += 4.2f * fly; y += (1.05f - y) * fly; yaw *= 1 - fly; pitch *= 1 - fly; s *= 1 + 0.25f * fly;
            } else if (fly > 0) light = 1 - 0.8f * fly;
            const M4 m = Mul(Mul(Mul(Translate(x, y, z), RotY(yaw)), RotX(pitch)), Scale3(s, s, s * 0.92f));
            const Target *tex = shelf.Cover(Key(i), nullptr);
            memcpy(ub.model, m.m, 64);
            memcpy(ub.mvp, Mul(vp, m).m, 64);
            ub.mat[0] = tex ? 1.0f : 0.0f; ub.mat[1] = 9.0f; ub.mat[2] = 0; ub.mat[3] = light;
            ub.tint[0] = 1.0f; ub.tint[1] = s; ub.tint[2] = s; ub.tint[3] = s * 0.92f;
            ub.emi[0] = x; ub.emi[1] = y; ub.emi[2] = z; ub.emi[3] = focus;
            gpu.SetMeshUniforms(&ub, sizeof ub);
            const Target *ts[4] = { tex ? tex : &as.env, &as.env, &haze, &as.env };
            gpu.BindTextures(ts, 4, false);
            gpu.DrawMesh(shelf.Vtx(), Shelf::kVtxBytes, shelf.Idx(), Shelf::kIdxCount);
            if (m_nhit < 16 && fly < 0.01f && vis > 0.5f) {      // where it is on screen (the finger)
                float sx, sy, sx2, sy2;
                const float c[3] = { x, y, z }, top[3] = { x, y + s * 0.5f, z };
                if (Project(vp, c, sx, sy) && Project(vp, top, sx2, sy2)) {
                    const float hh = fabsf(sy - sy2);
                    m_hit[m_nhit++] = { i, sx - hh, sy - hh, sx + hh, sy + hh };
                }
            }
        }
    }

    void Apps::DrawUi(Ui &ui, float now, const Wheel &w) {
        if (!Visible(w)) return;
        const float al = Clamp01(1 - fabsf(w.PageAnim() - kPage) * 1.15f);
        const float dx = (kPage - w.PageAnim()) * 520;
        if (al > 0.003f) {
            if (m_listed && !m_n) {
                ui.Empty(640 + dx, "Apps", "Aucun homebrew trouv\xc3\xa9.", "D\xc3\xa9pose tes .nro dans  switch/  sur la carte SD.", al);
            } else if (m_n && m_launch < 0) {
                // the chosen one's name, then its author, version, place. v70.3 (the PS2 look): no glass banner any more - a soft
                // dark veil without edges, the name near-white in a halo of light, the details in faint blue
                const App &a = m_a[m_sel];
                const float a2 = al * (0.3f + 0.7f * EaseGlide((now - m_moveAt) / Pace(0.45f)));
                char info[160];
                if (a.author[0] && a.version[0]) snprintf(info, sizeof info, "%s   \xc2\xb7   v%s   \xc2\xb7   %d / %d", a.author, a.version, m_sel + 1, m_n);
                else if (a.author[0]) snprintf(info, sizeof info, "%s   \xc2\xb7   %d / %d", a.author, m_sel + 1, m_n);
                else snprintf(info, sizeof info, "%d / %d", m_sel + 1, m_n);
                const float tw = ui.Width(fonts::kSetSel, a.name, 0.5f), bw = fmaxf(fmaxf(300.0f, tw + 80), ui.Width(fonts::kGameInfo, info, 2) + 80);
                const float cx = 640 + dx, y0 = 538;
                ui.Veil(cx - bw / 2 - 70, y0 - 26, cx + bw / 2 + 70, y0 + 118, 0.85f * al);
                ui.Light(cx - tw / 2 - 50, y0 - 2, cx + tw / 2 + 50, y0 + 54, ps2::A(ps2::kHalo, 0.34f * a2), true);
                ui.TextCentered(fonts::kSetSel, a.name, cx, y0 + 16, 0.5f, ps2::A(ps2::kChosen, a2));
                ui.TextCentered(fonts::kGameInfo, info, cx, y0 + 56, 2, ps2::A(ps2::kFaint, 0.95f * a2));
            }
        }
        {   // v87 - still loading (the list, the chosen cube's icon): the orbiting lights, inside its glass
            static busy::Fade s_load;
            const float lk = s_load.Step(s_wait, now, 0.25f) * al;
            if (lk > 0.003f) {
                float cx = 640 + dx, cy = 320;
                for (int h = 0; h < m_nhit; h++) if (m_hit[h].i == m_sel) { cx = (m_hit[h].x0 + m_hit[h].x1) * 0.5f; cy = (m_hit[h].y0 + m_hit[h].y1) * 0.5f; break; }
                busy::Orbs(ui, cx, cy, 30, lk, now);
            }
        }
        // launching: the screen goes black (and comes back if it could not start, with why)
        if (m_launch >= 0) {
            const float blk = m_backAt > 0 ? 1 - Clamp01((now - m_backAt) / Pace(0.7f)) : Clamp01((now - m_launchAt - Pace(0.3f)) / Pace(0.5f));
            if (blk > 0.003f) ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0, 0, blk }, 24, 1280, 720, 0);
        }
        if (m_status[0]) {
            const float sa = Clamp01(1 - (now - m_statusAt - 5.0f) / 0.6f);
            if (sa > 0.003f) ui.TextCentered(fonts::kHintBig, m_status, 640, 500, 0.5f, Hex(0xc9d3f5, sa));
        }
    }

} // namespace brume
