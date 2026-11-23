// Brume - v34/v37: the Album page. See Album.hpp.
#include "Album.hpp"
#include "Lang.hpp"
#include "Wheel.hpp"
#include "Mesh3d.hpp"
#include "Ease.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Busy.hpp"
#include "Worker.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {   // the loader's album (qlaunch-ext loader/src/album.cpp: caps:a, SD + NAND, newest first, 256 at most)
    int    qext_album_refresh(void);
    int    qext_album_count(void);
    int    qext_album_thumb(int index, void *out, unsigned cap);
    int    qext_album_image_size(int index);
    int    qext_album_image(int index, void *out, unsigned cap);
    int    qext_album_fileid(int index, CapsAlbumFileId *out);
    int    qext_album_is_movie(int index);
    int    qext_title_count(void);
    u64    qext_title_id(int index);
    int    qext_title_name(int index, char *out, unsigned cap);
    Result qext_launch_applet(int kind);   // 0 = the Switch's album
}

namespace brume {

    bool     DecodeJpegTo(const void *data, int bytes, uint8_t *out, int ow, int oh);   // Jpeg.cpp
    bool     DecodeJpegBC1(const void *data, int bytes, int w, int h, uint8_t *bc);

    void (*Album::s_log)(const char *) = nullptr;

    namespace {
        uint8_t s_thumbJpg[0x40000];                   // a thumbnail as the album gives it (JPEG, 320 x 180)
        uint8_t s_thumbRgba[320 * 180 * 4];
        // the sharp photo's job (the worker thread): the album file -> JPEG decode -> BC1, into s_bc
        constexpr int kFW = 1280, kFH = 720;
        uint8_t s_bc[(kFW / 4) * (kFH / 4) * 8];
        // v90 (perf.log: the page's list took 220-240 ms in a frame, each thumbnail 10-30 ms): the album's worker does all
        // of the album's work, one job at a time (the loader's album is never asked twice at once, as before): the list,
        // a thumbnail (read, decoded, in BC1), the sharp photo (decoded straight into BC1: see Jpeg.cpp)
        enum { kJobPhoto, kJobList, kJobThumb };
        constexpr int kListMax = 256;                  // (= Album::kMax, the loader's 256 at most)
        struct Job { int kind, index, ok, n; bool movie[kListMax]; };
        Job    s_job = {};
        Worker s_work;
        void Work(void *arg) {
            Job *j = (Job *)arg;
            j->ok = 0;
            if (j->kind == kJobList) {
                qext_album_refresh();
                int n = qext_album_count();
                if (n < 0) n = 0;
                if (n > kListMax) n = kListMax;
                for (int i = 0; i < n; i++) j->movie[i] = qext_album_is_movie(i) > 0;
                j->n = n; j->ok = 1;
            } else if (j->kind == kJobThumb) {
                const int sz = qext_album_thumb(j->index, s_thumbJpg, sizeof s_thumbJpg);
                if (sz > 0 && DecodeJpegTo(s_thumbJpg, sz, s_thumbRgba, 320, 180)) { Shelf::EncodeBC1(s_thumbRgba, 320, 180, s_thumbRgba); j->ok = 1; }   // (v90: in BC1 over itself: EncodeBC1 writes behind what it reads)
            } else {
                const int size = qext_album_image_size(j->index);
                if (size >= 64 && size <= (8 << 20)) {
                    if (uint8_t *jpg = (uint8_t *)malloc((size_t)size)) {
                        const int got = qext_album_image(j->index, jpg, (unsigned)size);
                        j->ok = got > 0 && DecodeJpegBC1(jpg, got, kFW, kFH, s_bc);   // (captures are 1280 x 720)
                        free(jpg);
                    }
                }
            }
        }
        // the flow (world units, the wheel's camera): the chosen print at z 2, the others turned on each side and set
        // back; the full screen: the print grows until it fills ~90 % of the screen, facing the camera
        constexpr float kW = 2.8f, kH = kW * 9.0f / 16.0f, kThick = 0.04f;
        constexpr float kGapC = 1.35f, kStep = 0.45f, kYaw = 0.85f, kZc = 2.0f, kBack = 1.2f, kY = 0.62f, kFloor = -0.20f;
        constexpr float kFlowW = 6.0f;                  // the flow's glide (and its throw)
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        const char *const kMonth[12] = { "janvier", "f\xc3\xa9vrier", "mars", "avril", "mai", "juin", "juillet",   // lang:fr (French only, the English is beside it)
                                         "ao\xc3\xbbt", "septembre", "octobre", "novembre", "d\xc3\xa9" "cembre" };   // lang:fr (French only, the English is beside it)
    }

    bool Album::Init(Gpu &gpu, uint32_t fullDescriptor) {
        uint32_t align = 1;
        const uint32_t bytes = gpu.ImageBytes(kFW, kFH, DkImageFormat_RGB_BC1_sRGB, 1, &align);
        if (fullDescriptor >= 64) return false;
        const uint32_t off = gpu.ReserveImage(bytes, align);
        if (off == UINT32_MAX) return false;
        m_fullDesc = fullDescriptor;
        m_fullOk = gpu.PlaceTexture(m_full, off, kFW, kFH, DkImageFormat_RGB_BC1_sRGB, 1, fullDescriptor);   // once: every capture fits
        return m_fullOk;
    }

    bool Album::Visible(const Wheel &w) const { return w.Active() && fabsf(w.PageAnim() - kPage) < 1.0f; }

    void Album::Select(int i, float now) {
        if (i == m_sel) return;
        const int dir = i > m_sel ? 1 : -1;
        m_sel = i; m_moveAt = now;
        audio::Play(audio::Deplacement, dir * 0.3f, 1.0f);
        haptic::Tick(0.6f);
    }

    void Album::Move(int step, float now) {
        if (!m_n) return;
        const int to = m_sel + step;
        if (to < 0 || to >= m_n) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
        Select(to, now);
    }

    void Album::DragBegin() { if (!m_n) return; m_drag = true; m_dragFrom = m_cur; m_curV = 0; }
    void Album::DragTo(float now, float steps) {
        if (!m_drag) return;
        m_cur = fmaxf(-0.4f, fminf(m_n - 0.6f, m_dragFrom + steps));
        const int t = (int)lroundf(fmaxf(0.0f, fminf((float)(m_n - 1), m_cur)));
        if (t != m_sel) Select(t, now);
    }
    void Album::DragEnd(float now, float v) {
        if (!m_drag) return;
        m_drag = false;
        m_curV = v;
        int t = (int)FlickTarget(m_cur, v, kFlowW, 6.0f);
        t = t < 0 ? 0 : (t >= m_n ? m_n - 1 : t);
        Select(t, now);
    }

    void Album::PressA(float now) {
        (void)now;
        if (!m_n || m_view) return;
        if (m_movie[m_sel]) {                           // the videos play in the Switch's album (for now)
            audio::Play(audio::Tick2400);
            haptic::Tap(0.7f);
            const Result rc = qext_launch_applet(0);
            if (s_log) { char b[64]; snprintf(b, sizeof b, "album : video -> album Switch rc=0x%x", rc); s_log(b); }
            return;
        }
        m_view = true;
        audio::Play(audio::Tick2400);
        audio::PlaySweep(audio::Whoosh, 0.0f, 0.0f, 0.5f, 0.4f);
        audio::Bell(audio::HarmonyNote(2, 72, 84), 0.05f, true, 0.05f);
        haptic::Thump(0.6f);
    }

    bool Album::PressB(float now) {
        (void)now;
        if (!m_view) return true;
        m_view = false;
        audio::Play(audio::Tick1700, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, 0.0f, 0.0f, 0.45f, 0.3f);
        haptic::Tick(0.5f);
        return false;
    }

    void Album::Tap(float x, float y, float now) {
        if (m_view) { PressB(now); return; }
        for (int k = 0; k < m_nhit; k++) {
            const Hit &h = m_hit[k];
            if (x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1) {
                if (h.i == m_sel) PressA(now); else Select(h.i, now);
                return;
            }
        }
    }

    // the game and the date of a capture (the game from the installed titles, the date from the capture itself)
    void Album::Info(int i) {
        m_infoFor = i;
        m_game[0] = m_date[0] = 0;
        CapsAlbumFileId fid;
        if (qext_album_fileid(i, &fid) != 0) return;
        const CapsAlbumFileDateTime &d = fid.datetime;
        static const char *const kMonthEn[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                                  "September", "October", "November", "December" };
        if (lang::En()) snprintf(m_date, sizeof m_date, "%s %d, %d  \xc2\xb7  %02d:%02d", d.month >= 1 && d.month <= 12 ? kMonthEn[d.month - 1] : "",
                                 d.day, d.year, d.hour, d.minute);   // v64
        else snprintf(m_date, sizeof m_date, "%d %s %d  \xc2\xb7  %02d:%02d", d.day, d.month >= 1 && d.month <= 12 ? kMonth[d.month - 1] : "",
                      d.year, d.hour, d.minute);
        const int n = qext_title_count();
        for (int t = 0; t < n; t++)
            if (qext_title_id(t) == fid.application_id) { qext_title_name(t, m_game, sizeof m_game); break; }
        if (!m_game[0]) snprintf(m_game, sizeof m_game, "%s", m_movie[i] ? lang::T("Vid\xc3\xa9o") : lang::T("Capture"));
    }

    // the sharp photo of capture i, on the worker thread (the menu keeps its 60 FPS); without a thread: here, once
    void Album::StartJob(int i, float now) {
        if (!s_work.Idle()) return;
        m_jobFor = i; m_upRow = -1;
        m_fullFor = -1; m_dropAt = now;                 // (the old sharp photo is no longer drawn: its memory gets reused)
        s_job.kind = kJobPhoto; s_job.index = i;
        s_work.Start(Work, &s_job, 0x20000);
    }

    // v90: the list, on the worker (the page shows its orbs meanwhile, as it did while the list was not read)
    bool Album::StartList() {
        if (!s_work.Idle() || m_thumbFor >= 0) return false;
        s_job.kind = kJobList;
        m_listing = true;
        return s_work.Start(Work, &s_job, 0x10000);
    }

    // v90: a job of the worker is over - its result, once (the thumbnail waits for Stream: it needs the frame)
    void Album::Collect(const Wheel &w) {
        if (!s_work.Done()) return;
        if (s_job.kind == kJobList) {
            m_listing = false;
            if (!Visible(w) || m_listed) return;          // (the page was left meanwhile: read again next time)
            m_n = s_job.n;
            for (int i = 0; i < m_n; i++) { m_noThumb[i] = false; m_fullBad[i] = false; m_movie[i] = s_job.movie[i]; }
            if (m_sel >= m_n) m_sel = m_n ? m_n - 1 : 0;
            m_cur = (float)m_sel; m_curV = 0;
            m_gen = (m_gen + 1) % 1000;                 // new keys: the old thumbnails can't be mistaken for these
            m_listed = true; m_infoFor = -1; m_fullFor = -1;
            if (s_log) { char b[64]; snprintf(b, sizeof b, "album : %d capture(s)", m_n); s_log(b); }
        } else if (s_job.kind == kJobThumb) {
            m_thumbOk = s_job.ok != 0;
            m_thumbReady = true;                        // (Stream sends it, or notes « no thumbnail »)
        } else {
            if (s_job.ok && m_jobFor >= 0 && m_listed) m_upRow = 0;
            else {
                // v90: not asked again for this capture (it was asked again every frame: 21 times in 2 s in perf.log)
                if (!s_job.ok && m_listed && m_jobFor >= 0 && m_jobFor < m_n) m_fullBad[m_jobFor] = true;
                m_jobFor = -1;
                if (s_log && !s_job.ok) s_log("album : photo nette illisible (miniature gardee)");
            }
        }
    }

    void Album::Shutdown() { s_work.Wait(); }

    void Album::Update(Gpu &gpu, const Wheel &w, float now, float dt) {
        (void)gpu;
        m_now = now;
        Collect(w);
        // v73.3 (Yass: a fast trip across the pages froze ~3 times on the way): read only when THIS page is where the
        // slide goes and it is nearly there - a page crossed on the way to another one reads nothing
        if (Visible(w) && !m_listed && !m_listing && w.Page() == kPage && fabsf(w.PageAnim() - kPage) < 0.35f)
            if (StartList()) Collect(w);                 // (no thread: read at once, as before)
        if (!Visible(w) && m_listed) { m_listed = false; m_view = false; }   // read again next time
        if (m_thumbFor >= 0 && s_work.Idle() && (!m_listed || m_thumbGen != m_gen)) { m_thumbReady = false; m_thumbFor = -1; }   // (v90: a thumbnail of an old list)
        if (!m_drag) Glide(m_cur, m_curV, (float)m_sel, dt, kFlowW);
        Glide(m_viewK, m_viewV, m_view ? 1.0f : 0.0f, dt, 5.5f);
        if (m_listed && m_n && m_infoFor != m_sel) Info(m_sel);
        // the chosen photo settled for a moment: make it sharp
        if (m_listed && m_n && m_fullOk && s_work.Idle() && m_thumbFor < 0 && m_upRow < 0 && m_fullFor != m_sel && !m_movie[m_sel] &&
            !m_fullBad[m_sel] && now - m_moveAt > 0.35f && fabsf(m_cur - m_sel) < 0.08f && !m_drag)
            StartJob(m_sel, now);
    }

    namespace { bool s_wait = false; }                    // v87: the chosen print has no picture yet (its orbs)
    void Album::Stream(Gpu &gpu, Shelf &shelf, const Wheel &w) {
        s_wait = Visible(w) && w.Page() == kPage && (!m_listed || (m_n && !m_noThumb[m_sel] && !shelf.Has(Key(m_sel))));
        if (!Visible(w) || !m_listed || !m_n) return;
        shelf.NextFrame();
        // 1. the sharp photo's slices (144 rows a frame: 5 frames), once the old one has left the frames in flight
        if (m_upRow >= 0) {
            if (m_now - m_dropAt < 0.08f) return;
            const uint32_t rows = 144, row = (uint32_t)m_upRow;
            if (gpu.StreamRows(m_full, s_bc + (row / 4) * (kFW / 4) * 8, row, rows)) {
                m_upRow += (int)rows;
                if (m_upRow >= kFH) { m_upRow = -1; m_fullFor = m_jobFor; m_jobFor = -1; m_sharpAt = m_now; }
            }
            return;
        }
        // 2. a thumbnail: the prints in sight, nearest first - and only if a place is free (no work for nothing). v90: read,
        // decoded and compressed by the worker; the frame only sends it (the next frame after it is ready, or this one)
        if (!m_thumbReady && m_thumbFor < 0) {
            if (!s_work.Idle() || !shelf.CanLoad()) return;   // (one album request at a time)
            int best = -1, bestD = 1 << 30;
            for (int i = m_sel - 6; i <= m_sel + 6; i++) {
                if (i < 0 || i >= m_n || m_noThumb[i] || shelf.Has(Key(i))) continue;
                const int d = abs(i - m_sel);
                if (d < bestD) { bestD = d; best = i; }
            }
            if (best < 0) return;
            s_job.kind = kJobThumb; s_job.index = best;
            m_thumbFor = best; m_thumbGen = m_gen;
            if (!s_work.Start(Work, &s_job, 0x10000)) { m_thumbFor = -1; return; }
            Collect(w);                                  // (no thread: it is ready now, as before)
        }
        if (!m_thumbReady) return;
        const int i = m_thumbFor;
        if (m_thumbGen != m_gen || i < 0 || i >= m_n) { m_thumbReady = false; m_thumbFor = -1; return; }   // (another list since)
        if (!m_thumbOk) { m_noThumb[i] = true; m_thumbReady = false; m_thumbFor = -1; return; }
        const int r = shelf.LoadBC1(gpu, Key(i), s_thumbRgba, 320, 180, 16.0f / 9.0f);
        if (r < 0) return;                                // (no place free yet / the frame's upload is taken: next frame)
        if (r == 0) m_noThumb[i] = true;
        m_thumbReady = false; m_thumbFor = -1;
    }

    void Album::DrawCards(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w, Shelf &shelf) {
        m_nhit = 0;
        if (!Visible(w) || !m_listed || !m_n || !shelf.Ready()) return;
        using namespace m3;
        float eye[3], at[3];
        w.CameraAt(now, eye, at);
        const float aspect = (float)gpu.Width() / gpu.Height();
        const M4 vp = Mul(Persp(38, aspect, 0.3f, 300), LookAt(eye, at));
        const float away = (kPage - w.PageAnim()) * 16.0f;   // the page slides like the consoles do
        const float vk = Clamp01(m_viewK);
        const float wf = 2.0f * (7.6f - kZc) * 0.34433f * aspect * 0.90f;   // full screen: 90 % of the width at z = kZc
        if (!gpu.MeshDrawn()) gpu.BeginMesh();
        MeshUbo ub = Wheel::s_lights;
        ub.cam[0] = eye[0]; ub.cam[1] = eye[1]; ub.cam[2] = eye[2]; ub.cam[3] = 1.0f;
        ub.base[0] = 0.02f; ub.base[1] = 0.025f; ub.base[2] = 0.05f; ub.base[3] = 1;
        struct Card { int i; M4 m; float k, light; };
        Card cards[13];
        int nc = 0;
        for (int i = (int)floorf(m_cur) - 6; i <= (int)ceilf(m_cur) + 6 && nc < 13; i++) {
            if (i < 0 || i >= m_n) continue;
            const float k = i - m_cur, ak = fabsf(k), sg = k < 0 ? -1.0f : 1.0f;
            if (ak > 5.5f) continue;
            // the flow
            float x, z, yaw;
            if (ak <= 1) { x = k * (kW * 0.5f + kGapC); yaw = -sg * kYaw * ak; z = kZc - kBack * ak; }
            else { x = sg * (kW * 0.5f + kGapC + (ak - 1) * kStep); yaw = -sg * kYaw; z = kZc - kBack - (ak - 1) * 0.06f; }
            float y = kY, s = 1, pitch = 0;
            // full screen: side by side, facing the camera, the chosen one filling the screen
            if (vk > 0.001f) {
                const float fx = k * (wf + 0.4f);
                x += (fx - x) * vk; yaw *= 1 - vk; z += (kZc - z) * vk; y += (0.70f - y) * vk; pitch = 0.098f * vk;
                s += (wf / kW - s) * vk;
                if (ak > 1.3f && vk > 0.5f) continue;
            }
            const M4 m = Mul(Mul(Mul(Translate(x + away, y, z), RotY(yaw)), RotX(pitch)), Scale3(kW * s, kH * s, kThick));
            const float light = 1.0f - fminf(ak, 4.0f) * 0.09f;
            cards[nc++] = { i, m, k, light };
        }
        for (int p = 1; p < nc; p++)                     // nearest first (the chosen one is in front)
            for (int q = p; q > 0 && fabsf(cards[q].k) < fabsf(cards[q - 1].k); q--) { const Card t = cards[q]; cards[q] = cards[q - 1]; cards[q - 1] = t; }
        for (int pass = 0; pass < 2; pass++) {           // the prints, then their reflection on the floor
            if (pass == 1 && vk > 0.6f) break;
            gpu.SetMeshState(true, pass == 1, pass == 1);
            for (int c = 0; c < nc; c++) {
                const Card &cd = pass == 0 ? cards[c] : cards[nc - 1 - c];
                const M4 m = pass == 0 ? cd.m : Mul(Mul(Translate(0, 2 * kFloor, 0), Scale3(1, -1, 1)), cd.m);
                const Target *tex = shelf.Cover(Key(cd.i), nullptr);
                const bool sharp = cd.i == m_fullFor;
                const float mix = sharp ? EaseGlide((now - m_sharpAt) / Pace(0.4f)) : 0.0f;
                memcpy(ub.model, m.m, 64);
                memcpy(ub.mvp, Mul(vp, m).m, 64);
                ub.mat[0] = tex ? 1.0f : 0.0f; ub.mat[1] = 8.0f; ub.mat[2] = m_movie[cd.i] ? 1.0f : 0.0f;
                ub.mat[3] = pass == 0 ? cd.light : -0.30f * cd.light * (1 - vk);
                ub.tint[0] = mix; ub.tint[1] = kW; ub.tint[2] = kH; ub.tint[3] = kThick;
                ub.emi[0] = kFloor; ub.emi[1] = 0; ub.emi[2] = 0; ub.emi[3] = fmaxf(0.0f, 1 - fabsf(cd.k));
                gpu.SetMeshUniforms(&ub, sizeof ub);
                const Target *ts[4] = { tex ? tex : &a.env, &a.env, &haze, sharp && m_fullOk ? &m_full : &a.env };
                gpu.BindTextures(ts, 4, false);
                gpu.DrawMesh(shelf.Vtx(), Shelf::kVtxBytes, shelf.Idx(), Shelf::kIdxCount);
                if (pass == 0 && m_nhit < 16 && vk < 0.01f) {   // where it is on screen (the finger)
                    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                    bool vis = true;
                    for (int q = 0; q < 4 && vis; q++) {
                        const float lp[3] = { (q & 1) ? 0.5f : -0.5f, (q & 2) ? 0.5f : -0.5f, 0.5f };
                        float wp[3], sx, sy;
                        Xform(cd.m, lp, wp);
                        vis = Project(vp, wp, sx, sy);
                        x0 = fminf(x0, sx); x1 = fmaxf(x1, sx); y0 = fminf(y0, sy); y1 = fmaxf(y1, sy);
                    }
                    if (vis) m_hit[m_nhit++] = { cd.i, x0, y0, x1, y1 };
                }
            }
        }
        gpu.SetMeshState(true, false, false);
    }

    void Album::DrawUi(Ui &ui, float now, const Wheel &w) {
        if (!Visible(w)) return;
        const float al = Clamp01(1 - fabsf(w.PageAnim() - kPage) * 1.15f) * (1 - Clamp01(m_viewK * 2));
        if (al <= 0.003f) return;
        const float dx = (kPage - w.PageAnim()) * 520;
        {   // v87 - still loading (the list, the chosen print's picture): the orbiting lights, on its card
            static busy::Fade s_load;
            const float lk = s_load.Step(s_wait && !m_view, now, 0.25f) * al;
            if (lk > 0.003f) {
                float cx = 640 + dx, cy = 330;
                for (int h = 0; h < m_nhit; h++) if (m_hit[h].i == m_sel) { cx = (m_hit[h].x0 + m_hit[h].x1) * 0.5f; cy = (m_hit[h].y0 + m_hit[h].y1) * 0.5f; break; }
                busy::Orbs(ui, cx, cy, 30, lk, now);
            }
        }
        if (m_listed && !m_n) {
            ui.Empty(640 + dx, "Album", "Aucune capture pour l'instant.", "Appuie sur le bouton Capture pendant un jeu : elle appara\xc3\xaet ici.", al);
            return;
        }
        if (!m_n || m_infoFor != m_sel) return;
        // under the flow: the date, then the game and where we are
        const float a2 = al * (0.3f + 0.7f * EaseGlide((now - m_moveAt) / Pace(0.45f)));
        // v70.3 (the PS2 look): the date near-white in a halo of light, on a soft veil; the game and the place in faint blue
        char info[160];
        snprintf(info, sizeof info, "%s   \xc2\xb7   %d / %d", m_game, m_sel + 1, m_n);
        const float tw = ui.Width(fonts::kSetSel, m_date, 0.5f), bw = fmaxf(tw, ui.Width(fonts::kGameInfo, info, 2));
        ui.Veil(640 + dx - bw / 2 - 140, 562, 640 + dx + bw / 2 + 140, 676, 0.8f * al);
        ui.Light(640 + dx - tw / 2 - 50, 584, 640 + dx + tw / 2 + 50, 640, ps2::A(ps2::kHalo, 0.30f * a2), true);
        ui.TextCentered(fonts::kSetSel, m_date, 640 + dx, 586, 0.5f, ps2::A(ps2::kChosen, a2));
        ui.TextCentered(fonts::kGameInfo, info, 640 + dx, 626, 2, ps2::A(ps2::kFaint, 0.95f * a2));
    }

} // namespace brume
