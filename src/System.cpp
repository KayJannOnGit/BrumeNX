// Brume - system pages and power panel. See System.hpp.
#include "System.hpp"
#include "Lang.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Settings.hpp"
#include "Ease.hpp"
#include "Wheel.hpp"
#include "Mesh3d.hpp"
#include "Profiles.hpp"
#include "Notify.hpp"
#include "Popup.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" {
    Result qext_launch_applet(int kind);   // 0 album, 1 controllers, 2 Mii edit
    Result qext_sleep(void);
}
namespace la {                             // the loader's library applet helpers (exported C++ symbols)
    Result Start(AppletId id, s32 version);
    Result Start(AppletId id, s32 version, const void *a, size_t na);
    Result Start(AppletId id, s32 version, const void *a, size_t na, const void *b, size_t nb);   // v84: two storages
}

namespace brume {

    void (*System::s_log)(const char *) = nullptr;

    namespace {
        float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        AccountUid Player() {   // as for the games: v46 the active profile, else last opened, else the first one
            AccountUid uid = Profiles::Active();
            if (accountUidIsValid(&uid)) return uid;
            if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid)) return uid;
            AccountUid all[ACC_USER_LIST_SIZE]; s32 n = 0;
            if (R_SUCCEEDED(accountListAllUsers(all, ACC_USER_LIST_SIZE, &n)))
                for (int i = 0; i < n; i++) if (accountUidIsValid(&all[i])) return all[i];
            return uid;
        }
        const char *const kPowerRows[3] = { "Veille", "Red\xc3\xa9marrer", "\xc3\x89teindre" };
        const char *const kPowerIco[3] = { "ico_veille", "ico_sync", "ico_power" };
        // v38 (Yass: « remets l'ancien, le tout petit truc à côté du bouton shutdown ») - the small glass panel under the bar
        constexpr float kPW = 300, kRowH = 56, kPH = 3 * kRowH + 24, kPX = 1244 - kPW, kPY = 92;
    }

    void System::Status(float now, const char *fmt, ...) {
        va_list ap; va_start(ap, fmt); vsnprintf(m_status, sizeof m_status, fmt, ap); va_end(ap);
        m_statusAt = now;
        notify::Post(now, m_status);                    // v77: a notification (PS4-like motion) instead of a line under the bar
    }

    bool System::Open(int tab, float now) {
        Result rc = 0;
        const char *what = "";
        if (tab >= 2 || tab < 0) haptic::Stop();                          // v29: motors off before a Switch page / the power
        switch (tab) {
            case 0: return false;                                         // Jeux: the wheel
            case 2: what = "album";      rc = qext_launch_applet(0); break;
            case 3: what = "manettes";   rc = qext_launch_applet(1); break;
            case 4:                                                       // v70.7: the « set » applet does not exist on a retail
                audio::Play(audio::ThockSnd, 0, 0.5f);                    // Switch 1 (System Settings are inside Nintendo's HOME
                Status(now, "Pas encore dans Brume");                     // menu, which Brume replaces): nothing to open
                return true;
            case -2: {                                                    // v70.7: the Switch's Internet screen (choose / add a network)
                what = "internet";
                const u32 caller = 1;                                     // (1 = called from the HOME menu, as uLaunch / HomeNX do)
                rc = la::Start(AppletId_LibraryAppletNetConnect, 1, &caller, sizeof caller);
                break;
            }
            case -3: {                                                    // v70.7: amiibo settings (nickname and owner), as libnx's nfp_la
                what = "amiibo";
                static NfpLaStartParamForAmiiboSettings p;
                memset(&p, 0, sizeof p);
                p.type = NfpLaStartParamTypeForAmiiboSettings_NicknameAndOwnerSettings;
                p.flags = 0x1;
                rc = la::Start(AppletId_LibraryAppletCabinet, 1, &p, sizeof p);
                break;
            }
            case -4: {                                                    // v84: Réglages > Manettes > « Changer l'assignation des boutons »
                // the Switch's own button mapping screen: the controller applet in its key remapping mode (11.0.0+), as Nintendo's
                // System Settings open it - the same two storages as libnx's hidLaShowControllerKeyRemappingForSystem (the
                // private arg, then the 16-byte KeyRemappingArg, all zero by default), but through the loader like the
                // controllers page (la::Start: the loader holds the applet, the menu goes on)
                what = "assignation";
                if (hosversionBefore(11, 0, 0)) { rc = MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer); break; }
                static HidLaControllerSupportArgPrivate priv;
                static HidLaControllerKeyRemappingArg arg;
                memset(&priv, 0, sizeof priv);
                priv.private_size = sizeof priv;
                priv.arg_size = sizeof(HidLaControllerSupportArg);                   // (8.0.0+ size)
                priv.flag0 = 0; priv.flag1 = 1;
                priv.mode = HidLaControllerSupportMode_ShowControllerKeyRemappingForSystem;
                priv.controller_support_caller = HidLaControllerSupportCaller_System;
                u32 styles = 0;
                HidNpadJoyHoldType hold = HidNpadJoyHoldType_Horizontal;
                if (R_FAILED(hidGetSupportedNpadStyleSet(&styles))) styles = 0;
                if (R_FAILED(hidGetNpadJoyHoldType(&hold))) hold = HidNpadJoyHoldType_Horizontal;
                priv.npad_style_set = styles;
                priv.npad_joy_hold_type = hold;
                hidLaCreateControllerKeyRemappingArg(&arg);
                rc = la::Start(AppletId_LibraryAppletController, 0x8, &priv, sizeof priv, &arg, sizeof arg);   // (version 8: 11.0.0+)
                break;
            }
            case 5:
                if (m_doAt > 0) return true;
                m_power = true; m_sel = 0; m_selF = 0; m_selV = 0; m_powerAt = now; m_moveAt = now;
                audio::Play(audio::Tick2400);
                audio::PlaySweep(audio::Whoosh, 0.3f, 0.0f, 0.6f, 0.3f);
                haptic::Tick(0.6f);
                return true;
            case -1: {
                what = "profil";
                FriendsLaArg arg;
                memset(&arg, 0, sizeof arg);
                arg.hdr.type = FriendsLaArgType_ShowMyProfile;
                arg.hdr.uid = Player();
                rc = la::Start(AppletId_LibraryAppletMyPage, 0x10000, &arg, sizeof arg);
                break;
            }
            default: return false;
        }
        audio::Play(audio::Tick2400);
        if (s_log) { char b[96]; snprintf(b, sizeof b, "systeme : %s rc=0x%x", what, rc); s_log(b); }
        if (rc == MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer))      // v84: the button mapping screen needs the system 11.0.0
            popup::Error(now, "Cette page n'a pas pu s'ouvrir", "Il faut au moins la version 11.0.0 du syst\xc3\xa8me.", 0);
        else if (R_FAILED(rc)) popup::Error(now, "Cette page n'a pas pu s'ouvrir", "La console a refus\xc3\xa9 de l'ouvrir.", (unsigned)rc);   // v78
        return true;
    }

    void System::PowerMove(int step, float now) {
        if (!m_power || m_doAt > 0) return;
        const int to = m_sel + step;
        if (to < 0 || to > 2) { audio::Play(audio::ThockSnd, 0, 0.5f); return; }
        m_sel = to; m_moveAt = now;
        audio::Play(audio::Deplacement, (to - 1) * 0.4f, 1.0f);
        haptic::Tick(0.6f);
    }

    void System::PowerClose(float now) {
        if (!m_power) return;
        m_power = false; m_closeAt = now;
        m_doAt = -1;                                    // v33: B during the fade to black cancels it
        audio::Play(audio::Tick1700, 0, 1.0f);
        haptic::Tick(0.5f);
    }

    // v33: the choice glows, the screen fades to black with the « sigh » (the light going back into the button), then it happens
    void System::PowerConfirm(float now) {
        if (!m_power || m_doAt > 0) return;
        audio::Play(audio::Tick2400);
        audio::Play(audio::Sigh, 0, 1.0f);
        haptic::Thump(0.8f);
        m_do = m_sel; m_doAt = now + 0.55f;
    }
    void System::Restart(float now) {
        if (m_doAt > 0) return;
        audio::Play(audio::Sigh, 0, 1.0f);
        haptic::Thump(0.8f);
        m_do = 1; m_doAt = now + 0.55f;
    }
    void System::Update(float now, float dt) {
        const float target = m_doAt > 0 ? 1.0f : 0.0f;   // v38: the light dims only while it fades to black
        m_dim += (target - m_dim) * (1 - expf(-dt * 5.0f));
        if (!m_power) { m_selF = (float)m_sel; m_selV = 0; } else Glide(m_selF, m_selV, (float)m_sel, dt, 14.0f);   // v78
        Settings::s_powerDim = m_dim;
        if (m_doAt > 0 && now >= m_doAt) {
            const int s = m_do;
            m_doAt = -1; m_power = false; m_closeAt = now;
            if (s_log) { char b[64]; snprintf(b, sizeof b, "systeme : alimentation %d", s); s_log(b); }
            haptic::Stop();                             // v29: motors off first
            audio::Duck(1.0f, 0.1f);
            if (s == 0) qext_sleep();                   // the loader puts the console to sleep (menu told first)
            else if (s == 1) appletStartRebootSequence();
            else appletStartShutdownSequence();
        }
    }
    void System::Tap(float x, float y, float now) {
        if (!m_power || m_doAt > 0) return;
        if (x >= kPX && x <= kPX + kPW && y >= kPY + 12 && y < kPY + 12 + 3 * kRowH) {
            const int i = (int)((y - kPY - 12) / kRowH);
            if (i != m_sel) { m_sel = i; m_moveAt = now; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.6f); }
            else PowerConfirm(now);                     // (a first touch chooses, a second one confirms: no accident)
            return;
        }
        PowerClose(now);
    }
    void System::Draw(Ui &ui, const Assets &a, float now) {
        // (v77: the status line under the top bar became a notification - Notify.hpp)
        // v38 - the small panel under the bar, by the power button (Yass: « le tout petit truc à côté du bouton shutdown »); it
        // slides down. v70 (the PS2 look, the same as Brume Overlay's power menu): no glass panel, no pill - a soft dark veil, the
        // words float in light blue, the chosen one near-white in a band of light with its halo (it grows on the new choice)
        // v78 (Yass: « on voit pas leur fond… carrée ») - its own surface (night mist, rounded, Ui::Surface), and it grows out of
        // the power button; the chosen row's light glides from one row to the next (Update: the menu's spring)
        {
            const float k = m_power ? EaseGlide((now - m_powerAt) / Pace(0.5f)) : 1 - EaseGlide((now - m_closeAt) / Pace(0.35f));
            if (k > 0.003f) {
                const float Y = kPY + (1 - k) * -14;
                ui.SetXform(0.94f + 0.06f * k, 1236, kPY - 30, 1236, kPY - 30);
                ui.Surface(kPX - 22, Y - 2, kPX + kPW + 8, Y + kPH + 2, k);
                const float pop = EaseGlide((now - m_moveAt) / Pace(0.3f));
                {
                    const char *row = lang::T(kPowerRows[m_sel]);
                    ui.Chosen(kPX - 12, kPX + kPW + 2, Y + 12 + m_selF * kRowH + 27, kPX + 32, ui.Width(fonts::kGameItem, row, 0), k * (0.45f + 0.55f * pop));
                }
                for (int i = 0; i < 3; i++) {
                    const float y = Y + 12 + i * kRowH;
                    const char *row = lang::T(kPowerRows[i]);
                    float px = kPX + 32;
                    const float on = Clamp01(1 - fabsf(m_selF - i));                  // (the colour follows the light)
                    const Rgba col = ps2::Line(on, k);
                    for (const char *p = row; *p;) px += ui.Glyph(fonts::kGameItem, NextCodepoint(p), px, y + 17, col, 0, 0, 0, 0);
                }
                ui.SetZoom(1.0f);
            }
        }
        // confirming: everything fades to black (the console goes to sleep / restarts / turns off right after)
        if (m_doAt > 0) {
            const float b = Clamp01(1 - (m_doAt - now) / 0.5f);
            ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0, 0, b }, 24, 1280, 720, 0);
        }
    }
} // namespace brume
