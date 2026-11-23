// Brume - the settings page (see Settings.hpp). Values and layout from the mockup v26-v27 (« carrousel » display).
#include "Settings.hpp"
#include "Lang.hpp"
#include "Ease.hpp"
#include "Audio.hpp"
#include "Haptics.hpp"
#include "Lock.hpp"
#include "Profiles.hpp"
#include "Ntp.hpp"
#include "Covers.hpp"
#include "Popup.hpp"
#include "Theme.hpp"
#include "BtAudio.hpp"
#include "Spotify.hpp"
#include "Clock.hpp"
#include "DatePages.hpp"
#include "Notify.hpp"
#include "Journal.hpp"
#include "Games.hpp"
#include "Pin.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <switch.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>

extern "C" {
    Result qext_launch_applet(int kind);   // 0 album, 1 controllers, 2 Mii edit
    int    qext_refresh_titles(void);      // v63: « Gérer les logiciels »
    int    qext_title_count(void);
    u64    qext_title_id(int index);
    int    qext_title_name(int index, char *out, unsigned cap);
    int    qext_title_is_virtual(u64 tid);
    int    qext_game_running(void);
    struct BrumeSwkbdOptions { const char *initialText, *headerText; unsigned minLength, maxLength; int blurBackground; };
    int qext_swkbd_show_ex(const BrumeSwkbdOptions *options, char *out, unsigned cap);
}

namespace brume {

    void (*Settings::s_toast)(float, const char *) = nullptr;
    void (*Settings::s_open)(int, float) = nullptr;
    float Settings::s_ambient[3] = { 0.22f, 0.42f, 0.95f }, Settings::s_ambientTarget[3] = { 0.22f, 0.42f, 0.95f };

    namespace {
        constexpr float PI = 3.14159265f;
        // ---- the data: every Switch setting, sorted our way (mockup XCATS). Constant-initialised (no constructor
        // runs in a loader module): plain aggregates, the macros only count the children / options.
        enum Kind : uint8_t { kFolder, kToggle, kChoice, kSlider, kInfo, kAction };
        struct Item {
            const char *ico, *name, *desc;
            Kind k;
            int v;
            const char *const *opt; int nopt;   // choice
            const char *info;                   // info: the value shown
            Item *kids; int nkids;              // folder
        };
        #define NOF(a) ((int)(sizeof(a) / sizeof((a)[0])))
        #define FO(i, n, kids, d) { i, n, d, kFolder, 0, nullptr, 0, nullptr, kids, NOF(kids) }
        #define TO(i, n, v, d)    { i, n, d, kToggle, v, nullptr, 0, nullptr, nullptr, 0 }
        #define CH(i, n, o, v, d) { i, n, d, kChoice, v, o, NOF(o), nullptr, nullptr, 0 }
        #define SL(i, n, v, d)    { i, n, d, kSlider, v, nullptr, 0, nullptr, nullptr, 0 }
        #define IN(i, n, val, d)  { i, n, d, kInfo, 0, nullptr, 0, val, nullptr, 0 }
        #define AC(i, n, d)       { i, n, d, kAction, 0, nullptr, 0, nullptr, nullptr, 0 }
        const char *const N0 = nullptr;

        // ---- v43 (Yass: « faut faire marcher les paramètres ») - every row does something real now: it reads the console's
        // own value when its category opens (Refresh) and writes it when it changes (WriteItem), on the emuMMC only (the
        // system settings services; the sysNAND is never touched). What has no native equivalent opens the Nintendo page.
        // v70.7: there is NO Nintendo settings page on a retail Switch 1 (the « set » applet is not installed: System Settings
        // live inside Nintendo's HOME menu, which Brume replaces). A row opens a Switch screen of its own when there is one
        // (Internet, amiibo, album, controllers, users, Mii); the others say « Pas encore dans Brume », until we build them.
        // Rows that only pretended (themes, layouts, fonts...) are gone until they exist.
        // v85 (Yass: « supprimer les trucs des settings qui servent à rien dans le contexte emuMMC ») - the emuMMC is cut from
        // Nintendo (atmosphere/hosts/emummc.txt, blank_prodinfo_emummc=1): the rows that needed Nintendo's servers or tools are
        // gone (system update, auto-update, download / news notifications, error reports, Nintendo Account, parental controls,
        // data repair, the blank serial number); the rest got a real use (the notifications' sound, Brume's journal, the way
        // back to the original menu, JKSV for the saves)
        char s_userName[40] = "Profil", s_devNick[72] = "", s_iWifi[48] = "", s_iState[40] = "", s_iIp[24] = "", s_iNand[40] = "",
             s_iSdFree[40] = "", s_iZone[40] = "", s_iFw[32] = "", s_iAms[24] = "", s_iModel[40] = "",
             s_iSd[24] = "", s_codeTxt[64] = "Pas encore choisi";
        bool s_codeSet = false;                                    // v44: a code was chosen (sdmc:/brume/reglages.ini codeseq=)
        // v87: the code is the active profile's (Pin.hpp) - never shown, only whether there is one
        bool HasCode() { uint8_t t[pin::kLen]; return pin::Get(Profiles::Active(), t); }
        void CodeText() {
            if (HasCode()) snprintf(s_codeTxt, sizeof s_codeTxt, lang::T("Choisi pour %s"), Profiles::ActiveName());
            else snprintf(s_codeTxt, sizeof s_codeTxt, lang::T("Aucun pour %s"), Profiles::ActiveName());
        }
        constexpr const char *kNin = "Bientôt dans Brume";   // v70.7: was « Page Nintendo » (that page cannot open)

        Item kUser[] = { AC("user", "Profil", "Ta page de profil, tes amis, ton activité"), AC("pen", "Modifier le pseudo", N0),
                         AC("mii", "Modifier l'icône", N0), AC("trash", "Supprimer l'utilisateur", kNin) };   // (v85: no Nintendo Account)
        Item kComptes[] = { FO("user", s_userName, kUser, "Le profil qui joue"), AC("userAdd", "Ajouter un utilisateur", N0),
                            AC("mii", "Créer ou modifier un Mii", N0),   // (v85: no parental controls - Nintendo's app and servers)
                            TO("users", "Choisir le profil à chaque jeu", 0, "La liste des profils au lancement d'un jeu Switch") };
        Item &AskUserItem() { return kComptes[3]; }
        const char *const oRes[] = { "Automatique", "1080p", "720p", "480p" }, *const oRgb[] = { "Automatique", "Limitée", "Complète" };
        Item kTv[] = { CH("tv", "Résolution TV", oRes, 0, "Prise en compte sur la TV"), CH("rgb", "Plage RGB", oRgb, 0, N0),
                       TO("power", "Synchroniser l'alimentation avec la TV", 1, "HDMI-CEC") };
        Item kEcran[] = { SL("sun", "Luminosité", 70, "L'écran de la console"), TO("sunAuto", "Luminosité automatique", 1, "Selon la lumière de la pièce"),
                          FO("tv", "Sortie TV", kTv, "Résolution, plage RGB, HDMI-CEC"), TO("lock", "Écran de verrouillage", 1, "L'heure et les 3 appuis sur A"),
                          AC("zoom", "Accessibilité", "Bientôt dans Brume : zoom, contraste, audio mono") };
        const char *const oTvSnd[] = { "Automatique", "Stéréo", "Mono", "Surround" };   // v57: Automatique (the Switch's default)
        // v82: Audio Bluetooth is Brume's own page (BtAudio.cpp): its rows (the search, then the devices) are filled from it every
        // frame while it is open (Settings::Update), the folder's count too; one row, the search, before it opens
        Item kBt[btaudio::kRowsMax] = { AC("radar", "Rechercher un appareil", "Le casque ou l'enceinte en mode appairage") };
        Item kSon[] = { SL("speaker", "Volume", 60, "La sortie active : haut-parleur, casque ou TV"), CH("tv", "Son de la TV", oTvSnd, 0, N0),
                        { "bt", "Audio Bluetooth", "Casque ou enceinte sans fil", kFolder, 0, nullptr, 0, nullptr, kBt, 1 },   // v82
                        TO("note", "Sons de l'interface", 1, "Les sons de Brume"),
                        TO("bell", "Son des notifications", 1, "Le petit son des notifications de Brume"),   // v85 (was Système > Notifications)
                        TO("wave", "Musique d'ambiance", 1, "La nappe de Brume"),   // (v92: « Mes musiques » removed - the nappe, on or off)
                        TO("vibrate", "Vibration HD", 1, "Chaque geste dans le menu, tout en douceur") };
                        // (v101: the two Spotify rows are out until Spotify comes back - spotify::kEnabled, Spotify.hpp)
        Item kPlane[] = { TO("plane", "Mode avion", 0, "Coupe le Wi-Fi et le NFC, les manettes restent"), TO("wifi", "Wi-Fi", 1, N0),
                          TO("nfc", "NFC", 1, "Les amiibo") };
        Item kReseau[] = { IN("wifi", "Connexion", s_iWifi, N0), IN("pulse", "État de la connexion", s_iState, N0), IN("chip", "Adresse IP", s_iIp, N0),
                           AC("globe", "Paramètres Internet", "Choisir ou ajouter un réseau"), FO("plane", "Mode avion", kPlane, N0),
                           TO("users", "Joueurs en ligne (Nextendo)", 1, "Combien jouent en ligne, sous chaque jeu Switch"),   // (v85: no error reports)
                           AC("wave", "Vocal Discord", "Le QR code de ton salon, pour parler depuis ton téléphone") };   // v95 (social::DiscordOpen)
        Item &NextendoItem() { return kReseau[5]; }
        // v84: « Tester » = Brume's own test screen (Pads), « Calibrer » = why Brume does not write it (a popup), « assignation »
        // = the Switch's own screen (the controller applet in its key remapping mode, System::Open(-4))
        Item kManettes[] = { AC("joycons", "Changer la prise en main ou l'ordre", N0), AC("plusCircle", "Associer une nouvelle manette", N0),
                             AC("buttons", "Tester les manettes", "Sticks, boutons, gâchettes, gyroscope"),
                             AC("stick", "Calibrer les sticks", "Un stick qui dérive : que faire"),
                             AC("swap", "Changer l'assignation des boutons", "L'écran de la Switch"),
                             TO("vibrate", "Vibration des manettes", 1, N0), TO("cable", "Communication filaire de la manette Pro", 0, "Par câble USB"),
                             AC("unlink", "Déconnecter les manettes", "Les manettes sans fil"), AC("amiibo", "amiibo", "Surnom et propriétaire d'un amiibo") };
        const char *const oWhere[] = { "Carte SD", "Console" };
        Item kCaps[] = { CH("sd", "Emplacement", oWhere, 0, "Où vont les nouvelles captures"), AC("copy", "Copier ou supprimer", "Dans l'album de la Switch") };
        Item kDonnees[] = { IN("pie", "Console", s_iNand, N0), IN("sd", "Carte SD", s_iSdFree, N0),
                            AC("grid", "Gérer les logiciels", "La place de chaque jeu, archiver, supprimer"),   // v67: the Cover Flow
                            AC("save", "Données de sauvegarde", "Sauvegarder ou restaurer avec JKSV"),   // v85: JKSV, if it is on the card
                            FO("camera", "Captures et vidéos", kCaps, N0),   // (v85: no « Corriger les données » - Nintendo's tool)
                            AC("download", "Télécharger les jaquettes", "PS2, PSP, PS1, GameCube, N64, GBA : par Internet"),   // v75
                            TO("cloud", "Jaquettes automatiques", 1, "Celles qui manquent arrivent toutes seules") };
        Item &CoversAutoItem() { return kDonnees[6]; }
        const char *const oSleepH[] = { "1 min", "3 min", "5 min", "10 min", "30 min", "Jamais" }, *const oSleepTv[] = { "1 h", "2 h", "3 h", "6 h", "12 h", "Jamais" };
        Item kVeille[] = { CH("handheld", "Veille auto. (portable)", oSleepH, 3, N0), CH("tv", "Veille auto. (TV)", oSleepTv, 0, N0),
                           TO("play", "Suspendre pendant les vidéos", 1, "Ne pas se mettre en veille pendant un film"),
                           TO("plug", "Réveiller au débranchement du chargeur", 0, N0) };
        const char *const oLang[] = { "Français", "English", "Español", "Deutsch", "Italiano", "Japonais" }, *const oRegion[] = { "Europe", "Amérique", "Japon", "Australie" };
        // v70.8: Nintendo's « sync by Internet » cannot work here (the emuMMC is cut from Nintendo's servers, its time ones
        // too): Brume asks a public time server itself (Ntp.cpp) - at boot, or now
        // v83: and Brume's own pages for the rest (DatePages.cpp): the date and time by hand, the time zone, the 24 h format
        Item kDate[] = { TO("sync", "Heure automatique (Internet)", 1, "Brume règle l'heure par Internet au démarrage"),
                         AC("clock", "Mettre à l'heure maintenant", "Demande l'heure à Internet"),
                         AC("pen", "Régler la date et l'heure", "À la main : le jour, l'heure, les minutes"), AC("globe", "Fuseau horaire", s_iZone),
                         TO("gauge", "Format 24 h", 1, "Sinon 9:41 PM, sur l'écran verrouillé et en haut") };
        Item &NtpItem() { return kDate[0]; }
        Item &H24Item() { return kDate[4]; }
        // v85: no system update, no auto-update, no serial number (blank here), no Notifications folder (its sound: Son; the
        // downloads and the news came from Nintendo); « Historique des erreurs » = Brume's journal (Journal.cpp); « Paramètres
        // Nintendo » = the way back to the original menu (the same safe exit as L + R + Moins)
        Item kSysteme[] = { AC("tag", "Surnom de la console", s_devNick), CH("lang", "Langue", oLang, 0, "Celle de la console (Brume la suit)"),
                            CH("pin", "Région", oRegion, 0, N0), FO("clock", "Date et heure", kDate, N0),
                            AC("warn", "Journal de Brume", "Les dernières erreurs, pour signaler un bug"),
                            AC("lifebuoy", "Revenir au menu d'origine", "Quitter Brume : la console redémarre sur l'ancien menu") };
        // v29 (Yass): the ambient colour - the fog's light and the tint of the scene's lights. Blue by default.
        const char *const oAmbient[] = { "Bleu nuit", "Indigo", "Violet", "Rose", "Rouge", "Ambre", "Or", "Vert", "Émeraude",
                                         "Turquoise", "Cyan", "Argent", "Blanc lunaire", "Aurore dorée (secret)" };   // v44: the 14th = the golden cube's
        const float kAmbientRgb[14][3] = { { 0.22f, 0.42f, 0.95f }, { 0.32f, 0.30f, 0.95f }, { 0.52f, 0.26f, 0.90f }, { 0.85f, 0.30f, 0.65f },
                                           { 0.90f, 0.22f, 0.25f }, { 0.95f, 0.55f, 0.18f }, { 0.85f, 0.70f, 0.25f }, { 0.25f, 0.80f, 0.40f },
                                           { 0.15f, 0.75f, 0.55f }, { 0.15f, 0.70f, 0.80f }, { 0.20f, 0.75f, 0.95f }, { 0.60f, 0.65f, 0.75f },
                                           { 0.80f, 0.82f, 0.90f }, { 0.95f, 0.66f, 0.28f } };
        bool s_secret = false;                                     // v44: the golden cube was caught once (reglages.ini secret=1)
        Item kPerso[] = { IN("folder", "Dossier", "sdmc:/brume/perso/", N0), IN("sync", "Prise en compte", "Au démarrage", N0) };
        const char *const oAccent[] = { "Bleu glacier", "Violet", "Argent" };
        const char *const oLayout[] = { "XMB", "Carrousel" };
        const char *const oClock[] = { "Lexend Giga", "Michroma", "Big Shoulders", "Instrument Serif", "Orbitron", "Major Mono", "Monoton" };
        const float kAccentRgb[3][3] = { { 0.549f, 0.627f, 1.0f }, { 0.70f, 0.52f, 1.0f }, { 0.80f, 0.82f, 0.88f } };
        // v47 - the themes: a whole mood at once (the fog's colour, the accent, how much light), the colours stay editable.
        // v81: and a whole LOOK (Theme.cpp: the fog's character, the lights, the cubes, the interface...); « Y2K Chrome »
        // became « Chrome » (Yass), same place (theme=2 in reglages.ini)
        const char *const oTheme[] = { "Brume", "Nuit profonde", "Chrome" };
        struct ThemePreset { int ambient, accent; float light; };
        const ThemePreset kThemes[3] = { { 0, 0, 1.0f }, { 1, 1, 0.62f }, { 11, 2, 1.0f } };
        const char *const oUnlock[] = { "Battement de cœur", "Aurore" };   // v59
        const char *const oBrumeLang[] = { "Automatique", "Français", "English" };   // v64: automatic = the console's language
        const char *const oGlass[] = { "PS3", "Classique" };   // v73: the icons' liquid glass (ui_fsh mode 29) or the v28 glass (mode 22)
        Item kBrume[] = { CH("palette", "Thème", oTheme, 0, "Une ambiance complète : lumière et couleurs"),
                          CH("layout", "Affichage des réglages", oLayout, 1, "XMB comme la PS3, ou carrousel 3D"),   // v70.6: here, no Y any more
                          CH("palette", "Couleur d'ambiance", oAmbient, 0, "La lumière de la brume et de la scène"),
                          CH("drop", "Couleur d'accent", oAccent, 0, "La surbrillance de l'interface"),
                          CH("font", "Police de l'heure", oClock, 0, "L'heure de l'écran verrouillé"),
                          TO("keypad", "Code de verrouillage", 0, "4 touches de la manette, comme la PS4 (le profil actif)"), AC("keypad", "Choisir le code", s_codeTxt),
                          TO("cube", "Cube doré", 1, "Le secret de la roue"),
                          FO("note", "Sons perso", kPerso, "Tes propres sons à la place des nôtres"),
                          TO("wave", "Vague XMB", 0, "Une nappe de lumière qui ondule derrière la brume"),
                          TO("clock", "Ciel selon l'heure", 0, "Aube rose, jour bleu, soir violet, nuit presque noire"),
                          TO("battery", "La batterie, c'est la brume", 0, "Batterie faible : la brume s'assombrit ; en charge, elle respire"),
                          CH("note", "Déverrouillage", oUnlock, 0, "Le son et la lumière du 3e appui (on l'entend en le choisissant)"),
                          CH("lang", "Langue de Brume", oBrumeLang, 0, "Le menu en français ou en anglais"),
                          TO("power", "Overlay Brume (essai)", 0, "Volet HOME, volume et menu Power à nous (au prochain démarrage)"),
                          CH("sparkle", "Verre des icônes", oGlass, 0, "Verre liquide façon PS3, ou le verre d'avant") };   // v73 (the 16th row: the most a column shows)
        Item kInfos[] = { IN("cube", "Brume", "v85", N0), IN("chip", "Système", s_iFw, N0), IN("sparkle", "Atmosphère", s_iAms, N0),
                          IN("handheld", "Console", s_iModel, N0), IN("sd", "Carte SD", s_iSd, N0) };
        struct Cat { const char *id, *name; Item *items; int n; };
        Cat kCat[10] = { { "comptes", "Comptes", kComptes, NOF(kComptes) }, { "ecran", "Écran", kEcran, NOF(kEcran) },
                         { "son", "Son", kSon, NOF(kSon) }, { "reseau", "Réseau", kReseau, NOF(kReseau) },
                         { "manettes", "Manettes", kManettes, NOF(kManettes) }, { "donnees", "Données", kDonnees, NOF(kDonnees) },
                         { "veille", "Veille", kVeille, NOF(kVeille) }, { "systeme", "Système", kSysteme, NOF(kSysteme) },
                         { "brume", "Brume", kBrume, NOF(kBrume) }, { "infos", "À propos", kInfos, NOF(kInfos) } };

        Item &ThemeItem() { return kBrume[0]; }
        Item &AmbientItem() { return kBrume[2]; }
        Item &VibrationItem() { return kSon[6]; }   // (v85: kSon[4] = the notifications' sound)
        Item &UiSoundsItem() { return kSon[3]; }
        Item &NotifSoundItem() { return kSon[4]; }   // v85: Brume's notifications (Notify.cpp)
        Item &MusicItem() { return kSon[5]; }
        Item &AccentItem() { return kBrume[3]; }
        Item &ClockItem() { return kBrume[4]; }
        Item &CodeItem() { return kBrume[5]; }
        Item &GoldItem() { return kBrume[7]; }
        Item &LayoutItem() { return kBrume[1]; }   // v70.6: next to Thème
        Item &WaveItem() { return kBrume[9]; }
        Item &SkyItem() { return kBrume[10]; }
        Item &BatteryItem() { return kBrume[11]; }
        Item &UnlockItem() { return kBrume[12]; }
        Item &BrumeLangItem() { return kBrume[13]; }
        Item &GlassItem() { return kBrume[15]; }   // v73
        static_assert(NOF(kBrume) <= 16, "a column shows 16 rows at most (Settings::m_row)");
        // v69 - Brume Overlay (test): its files (sdmc:/brume/overlay/exefs/) in place of the Nintendo overlay's, or not
        constexpr const char *kOvlSrc = "sdmc:/brume/overlay/exefs", *kOvlDir = "sdmc:/atmosphere/contents/010000000000100C";
        bool OverlayOn() { struct stat st; char p[96]; snprintf(p, sizeof p, "%s/exefs/main", kOvlDir); return stat(p, &st) == 0; }
        bool CopyOne(const char *from, const char *to) {
            FILE *a = fopen(from, "rb");
            if (!a) return false;
            FILE *b = fopen(to, "wb");
            if (!b) { fclose(a); return false; }
            char *buf = (char *)malloc(65536);
            bool ok = buf != nullptr;
            size_t n;
            while (ok && (n = fread(buf, 1, 65536, a)) > 0) ok = fwrite(buf, 1, n, b) == n;
            free(buf); fclose(a);
            ok = fclose(b) == 0 && ok;
            if (!ok) remove(to);
            return ok;
        }
        Result OverlaySet(bool on) {
            char d[96], f[128], s[128];
            snprintf(d, sizeof d, "%s/exefs", kOvlDir);
            if (!on) {
                snprintf(f, sizeof f, "%s/main", d); remove(f);
                snprintf(f, sizeof f, "%s/main.npdm", d); remove(f);
                rmdir(d); rmdir(kOvlDir);                      // (only if empty: never someone else's files)
                return OverlayOn() ? MAKERESULT(Module_Libnx, LibnxError_IoError) : 0;
            }
            struct stat st;
            snprintf(s, sizeof s, "%s/main", kOvlSrc);
            if (stat(s, &st) != 0) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
            mkdir("sdmc:/atmosphere/contents", 0777); mkdir(kOvlDir, 0777); mkdir(d, 0777);
            snprintf(f, sizeof f, "%s/main.npdm", d); snprintf(s, sizeof s, "%s/main.npdm", kOvlSrc);
            bool ok = CopyOne(s, f);                           // the npdm first, then main: main present = complete
            snprintf(f, sizeof f, "%s/main", d); snprintf(s, sizeof s, "%s/main", kOvlSrc);
            ok = ok && CopyOne(s, f);
            if (FILE *c = fopen("sdmc:/brume/overlay/demarrages.txt", "w")) { fputs("0\n", c); fclose(c); }   // a fresh start for its safety counter
            if (!ok) { OverlaySet(false); return MAKERESULT(Module_Libnx, LibnxError_IoError); }
            return 0;
        }
        // v53 - the sky's colour through the day: v81, Theme.cpp (theme::SkyAt, with the fog's light and density)

        // ---- what each row is bound to ----
        enum Bind : uint8_t { B_NONE, B_BRIGHT, B_BRIGHT_AUTO, B_TV_RES, B_TV_RGB, B_TV_CEC, B_LOCK, B_VOLUME, B_TV_SOUND, B_UI_SOUNDS, B_MUSIC,
                              B_VIBRATION, B_PLANE, B_WIFI, B_NFC, B_PAD_VIB, B_PRO_WIRED, B_ALBUM, B_SLEEP_H, B_SLEEP_TV, B_SLEEP_MEDIA,
                              B_WAKE_PLUG, B_LANG, B_REGION, B_CLOCK_SYNC, B_NOTIF_SOUND, B_AMBIENT,
                              I_USER, I_WIFI, I_STATE, I_IP, I_NAND, I_SDFREE, I_ZONE, I_FW, I_AMS, I_MODEL, I_SD,
                              A_PROFILE, A_EDIT_NICK, A_EDIT_ICON, A_ADD_USER, A_MII, A_NINTENDO, A_CONTROLLERS, A_DISCONNECT, A_NICKNAME,
                              B_LOCKCODE, A_SETCODE, B_GOLD, B_ACCENT, B_CLOCKFONT, B_ASKUSER, B_THEME, B_NEXTENDO, B_LAYOUT, B_WAVE,
                              B_SKY, B_BATTERY, B_UNLOCK, A_MANAGE, B_BRUMELANG, B_OVERLAY, A_NETCONNECT, A_AMIIBO, A_ALBUM,
                              B_NTP, A_CLOCK_NOW, B_GLASS, A_COVERS, B_COVERS_AUTO,
                              A_PADTEST, A_CALIBRATE, A_REMAP,   // v84: Réglages > Manettes
                              A_SET_TIME, A_ZONE, B_H24,   // v83
                              A_JOURNAL, A_LEAVE, A_SAVES,     // v85: Brume's journal, back to the original menu, JKSV
                              A_DISCORD,                     // v95: the Discord voice QR (Social.cpp)
                              B_SPOTIFY, A_SPOTIFY };          // v98: Spotify (Spotify.cpp)
        struct BindRow { const Item *it; Bind b; };
        const BindRow kBind[] = {
            { &kComptes[0], I_USER }, { &kUser[0], A_PROFILE }, { &kUser[1], A_EDIT_NICK }, { &kUser[2], A_EDIT_ICON }, { &kUser[3], A_NINTENDO },
            { &kComptes[1], A_ADD_USER }, { &kComptes[2], A_MII }, { &kComptes[3], B_ASKUSER },   // (v85: no Nintendo Account, no parental controls)
            { &kEcran[0], B_BRIGHT }, { &kEcran[1], B_BRIGHT_AUTO }, { &kTv[0], B_TV_RES }, { &kTv[1], B_TV_RGB }, { &kTv[2], B_TV_CEC },
            { &kEcran[3], B_LOCK }, { &kEcran[4], A_NINTENDO },
            { &kSon[0], B_VOLUME }, { &kSon[1], B_TV_SOUND }, { &kSon[3], B_UI_SOUNDS }, { &kSon[4], B_NOTIF_SOUND }, { &kSon[5], B_MUSIC },   // (v82: kSon[2], a page now)
            { &kSon[6], B_VIBRATION },   // (v101: no Spotify rows for now)   // (v85: the notifications' sound at 4; v92: no more « Morceau » rows)
            { &kReseau[0], I_WIFI }, { &kReseau[1], I_STATE }, { &kReseau[2], I_IP }, { &kReseau[3], A_NETCONNECT }, { &kPlane[0], B_PLANE },
            { &kPlane[1], B_WIFI }, { &kPlane[2], B_NFC }, { &kReseau[5], B_NEXTENDO }, { &kReseau[6], A_DISCORD },   // (v85: no error reports; v95: Discord)
            { &kManettes[0], A_CONTROLLERS }, { &kManettes[1], A_CONTROLLERS }, { &kManettes[2], A_PADTEST }, { &kManettes[3], A_CALIBRATE },
            { &kManettes[4], A_REMAP }, { &kManettes[5], B_PAD_VIB }, { &kManettes[6], B_PRO_WIRED }, { &kManettes[7], A_DISCONNECT },
            { &kManettes[8], A_AMIIBO },   // v84
            { &kDonnees[0], I_NAND }, { &kDonnees[1], I_SDFREE }, { &kDonnees[2], A_MANAGE }, { &kDonnees[3], A_SAVES }, { &kCaps[0], B_ALBUM },
            { &kCaps[1], A_ALBUM }, { &kDonnees[5], A_COVERS }, { &kDonnees[6], B_COVERS_AUTO },   // v75 (v85: JKSV at 3; no data repair)
            { &kVeille[0], B_SLEEP_H }, { &kVeille[1], B_SLEEP_TV }, { &kVeille[2], B_SLEEP_MEDIA }, { &kVeille[3], B_WAKE_PLUG },
            { &kSysteme[0], A_NICKNAME }, { &kSysteme[1], B_LANG }, { &kSysteme[2], B_REGION },
            { &kDate[0], B_NTP }, { &kDate[1], A_CLOCK_NOW }, { &kDate[2], A_SET_TIME }, { &kDate[3], A_ZONE }, { &kDate[4], B_H24 },   // v83
            { &kSysteme[4], A_JOURNAL }, { &kSysteme[5], A_LEAVE },   // v85
            { &kBrume[0], B_THEME }, { &kBrume[1], B_LAYOUT }, { &kBrume[2], B_AMBIENT }, { &kBrume[3], B_ACCENT }, { &kBrume[4], B_CLOCKFONT },
            { &kBrume[5], B_LOCKCODE }, { &kBrume[6], A_SETCODE }, { &kBrume[7], B_GOLD }, { &kBrume[9], B_WAVE }, { &kBrume[10], B_SKY }, { &kBrume[11], B_BATTERY }, { &kBrume[12], B_UNLOCK }, { &kBrume[13], B_BRUMELANG }, { &kBrume[14], B_OVERLAY }, { &kBrume[15], B_GLASS }, { &kInfos[1], I_FW }, { &kInfos[2], I_AMS }, { &kInfos[3], I_MODEL }, { &kInfos[4], I_SD } };
        Bind BindOf(const Item &it) {
            for (const BindRow &r : kBind) if (r.it == &it) return r.b;
            return B_NONE;
        }
        // ---- reading and writing the console's settings (every call is checked; a failure leaves the row as it was) ----
        AccountUid PlayerUid() {   // as for the games: v46 the active profile, else the last opened, else the first one
            AccountUid uid = Profiles::Active();
            if (accountUidIsValid(&uid)) return uid;
            if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid)) return uid;
            AccountUid all[ACC_USER_LIST_SIZE]; s32 n = 0;
            if (R_SUCCEEDED(accountListAllUsers(all, ACC_USER_LIST_SIZE, &n)))
                for (int i = 0; i < n; i++) if (accountUidIsValid(&all[i])) return all[i];
            return uid;
        }
        void Copy(char *dst, int cap, const char *src, int max) { int n = 0; while (n < max && n + 1 < cap && src[n]) { dst[n] = src[n]; n++; } dst[n] = 0; }
        void Space(char *out, int cap, NcmStorageId id) {   // « 12,3 Go libres »
            s64 f = 0, t = 0;
            if (R_FAILED(nsGetFreeSpaceSize(id, &f)) || R_FAILED(nsGetTotalSpaceSize(id, &t)) || t <= 0) { snprintf(out, cap, "?"); return; }
            snprintf(out, cap, lang::T("%.1f Go libres sur %.0f"), f / 1e9, t / 1e9);
            for (char *p = out; *p; p++) if (*p == '.') { *p = ','; break; }
        }
        bool GetSleep(SetSysSleepSettings &s) { memset(&s, 0, sizeof s); return R_SUCCEEDED(setsysGetSleepSettings(&s)); }
        bool GetTv(SetSysTvSettings &t) { memset(&t, 0, sizeof t); return R_SUCCEEDED(setsysGetTvSettings(&t)); }
        const SetLanguage kLangs[6] = { SetLanguage_FR, SetLanguage_ENGB, SetLanguage_ES, SetLanguage_DE, SetLanguage_IT, SetLanguage_JA };
        const SetRegion kRegions[4] = { SetRegion_EUR, SetRegion_USA, SetRegion_JPN, SetRegion_AUS };
        // the active audio output and its volume (0..100); audctl is opened only for the call
        constexpr int kHeadCap = 75;                       // v57 (review): the headphones' safe level
        bool s_loudOk = false, s_volCapped = false;        // the warning was seen (until the slider closes) / it just stopped there
        bool Volume(int *getPct, const int *setPct, bool *capped = nullptr) {
            if (R_FAILED(audctlInitialize())) return false;
            AudioTarget tg = AudioTarget_Speaker;
            if (R_FAILED(audctlGetActiveOutputTarget(&tg)) || tg == AudioTarget_Invalid) tg = AudioTarget_Speaker;
            s32 mx = 15, v = 0;
            audctlGetTargetVolumeMax(&mx);
            if (mx <= 0) mx = 15;
            bool ok;
            int want = setPct ? *setPct : 0;
            if (setPct && capped && tg == AudioTarget_Headphone && want > kHeadCap) { want = kHeadCap; *capped = true; }
            if (setPct) ok = R_SUCCEEDED(audctlSetTargetVolume(tg, (s32)lroundf(want * mx / 100.0f)));
            else { ok = R_SUCCEEDED(audctlGetTargetVolume(&v, tg)); if (ok && getPct) *getPct = (int)lroundf(v * 100.0f / mx); }
            audctlExit();
            return ok;
        }

        void ReadItem(Item &it) {
            switch (BindOf(it)) {
                case B_BRIGHT: { float f = 0; if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&f))) it.v = (int)lroundf(fmaxf(0.0f, fminf(1.0f, f)) * 100); break; }
                case B_BRIGHT_AUTO: { bool b = false; if (R_SUCCEEDED(lblIsAutoBrightnessControlEnabled(&b))) it.v = b; break; }
                case B_TV_RES: { SetSysTvSettings t; if (GetTv(t) && t.tv_resolution >= 0 && t.tv_resolution <= 3) it.v = t.tv_resolution; break; }
                case B_TV_RGB: { SetSysTvSettings t; if (GetTv(t)) it.v = t.rgb_range == 2 ? 1 : (t.rgb_range == 1 ? 2 : 0); break; }
                case B_TV_CEC: { SetSysTvSettings t; if (GetTv(t)) it.v = (t.flags >> 2) & 1; break; }   // TvFlag AllowsCec = bit 2
                case B_LOCK: { bool b = true; if (R_SUCCEEDED(setsysGetLockScreenFlag(&b))) { it.v = b; Settings::s_lockOn = b; } break; }
                case B_OVERLAY: it.v = OverlayOn() ? 1 : 0; break;   // v69
                case B_SPOTIFY: it.v = spotify::Wanted() ? 1 : 0; break;   // v98
                case B_VOLUME: { int p = 0; if (Volume(&p, nullptr)) it.v = p < 0 ? 0 : (p > 100 ? 100 : p); break; }
                case B_TV_SOUND: {
                    if (R_FAILED(audctlInitialize())) break;
                    AudioOutputMode m = AudioOutputMode_Pcm2ch;
                    if (R_SUCCEEDED(audctlGetOutputModeSetting(&m, AudioTarget_Tv))) it.v = m == AudioOutputMode_Pcm1ch ? 2 : (m == AudioOutputMode_Pcm6ch ? 3 : (m == AudioOutputMode_Pcm2ch ? 1 : 0));
                    audctlExit();
                    break;
                }
                case B_PLANE: {
                    bool w = true, n = true;
                    nifmIsWirelessCommunicationEnabled(&w); setsysGetNfcEnableFlag(&n);
                    it.v = !w && !n;
                    break;
                }
                case B_WIFI: { bool b = true; if (R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&b))) it.v = b; break; }
                case B_NFC: { bool b = true; if (R_SUCCEEDED(setsysGetNfcEnableFlag(&b))) it.v = b; break; }
                case B_PAD_VIB: { float f = 1; if (R_SUCCEEDED(setsysGetVibrationMasterVolume(&f))) it.v = f > 0.01f; break; }
                case B_PRO_WIRED: { bool b = false; if (R_SUCCEEDED(setsysGetUsbFullKeyEnableFlag(&b))) it.v = b; break; }
                case B_ALBUM: { SetSysPrimaryAlbumStorage s; if (R_SUCCEEDED(setsysGetPrimaryAlbumStorage(&s))) it.v = s == SetSysPrimaryAlbumStorage_SdCard ? 0 : 1; break; }
                case B_SLEEP_H: { SetSysSleepSettings s; if (GetSleep(s) && s.handheld_sleep_plan >= 0 && s.handheld_sleep_plan <= 5) it.v = s.handheld_sleep_plan; break; }
                case B_SLEEP_TV: { SetSysSleepSettings s; if (GetSleep(s) && s.console_sleep_plan >= 0 && s.console_sleep_plan <= 5) it.v = s.console_sleep_plan; break; }
                case B_SLEEP_MEDIA: { SetSysSleepSettings s; if (GetSleep(s)) it.v = !(s.flags & 1); break; }      // SleepFlag SleepsWhilePlayingMedia = bit 0
                case B_WAKE_PLUG: { SetSysSleepSettings s; if (GetSleep(s)) it.v = (s.flags >> 1) & 1; break; }    // WakesAtPowerStateChange = bit 1
                case B_LANG: {
                    u64 code = 0; SetLanguage l = SetLanguage_FR;
                    if (R_SUCCEEDED(setGetLanguageCode(&code)) && R_SUCCEEDED(setMakeLanguage(code, &l))) {
                        if (l == SetLanguage_FRCA) l = SetLanguage_FR;
                        if (l == SetLanguage_ENUS) l = SetLanguage_ENGB;
                        if (l == SetLanguage_ES419) l = SetLanguage_ES;
                        for (int k = 0; k < 6; k++) if (kLangs[k] == l) it.v = k;
                    }
                    break;
                }
                case B_REGION: { SetRegion r; if (R_SUCCEEDED(setGetRegionCode(&r))) for (int k = 0; k < 4; k++) if (kRegions[k] == r) it.v = k; break; }
                case B_CLOCK_SYNC: { bool b = true; if (R_SUCCEEDED(setsysIsUserSystemClockAutomaticCorrectionEnabled(&b))) it.v = b; break; }
                case I_USER: {
                    AccountProfile p; AccountProfileBase base;
                    const AccountUid uid = PlayerUid();
                    if (accountUidIsValid(&uid) && R_SUCCEEDED(accountGetProfile(&p, uid))) {
                        memset(&base, 0, sizeof base);
                        if (R_SUCCEEDED(accountProfileGet(&p, nullptr, &base)) && base.nickname[0]) Copy(s_userName, sizeof s_userName, base.nickname, sizeof base.nickname);
                        accountProfileClose(&p);
                    }
                    break;
                }
                case I_WIFI: {
                    static NifmNetworkProfileData p;
                    memset(&p, 0, sizeof p);
                    if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(&p))) {
                        if (p.wireless_setting_data.ssid_len && p.wireless_setting_data.ssid[0])
                            Copy(s_iWifi, sizeof s_iWifi, p.wireless_setting_data.ssid, p.wireless_setting_data.ssid_len < 0x20 ? p.wireless_setting_data.ssid_len : 0x20);
                        else Copy(s_iWifi, sizeof s_iWifi, p.network_name, sizeof p.network_name);
                    } else snprintf(s_iWifi, sizeof s_iWifi, lang::T("Aucun réseau"));
                    if (!s_iWifi[0]) snprintf(s_iWifi, sizeof s_iWifi, lang::T("Aucun réseau"));
                    break;
                }
                case I_STATE: {
                    NifmInternetConnectionType t; u32 strength = 0; NifmInternetConnectionStatus st;
                    if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&t, &strength, &st)) && st == NifmInternetConnectionStatus_Connected)
                        snprintf(s_iState, sizeof s_iState, t == NifmInternetConnectionType_WiFi ? lang::T("Connecté · signal %u/3") : lang::T("Connecté (câble)"), strength);
                    else snprintf(s_iState, sizeof s_iState, lang::T("Non connecté"));
                    break;
                }
                case I_IP: {
                    u32 ip = 0;
                    if (R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) && ip) snprintf(s_iIp, sizeof s_iIp, "%u.%u.%u.%u", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24);
                    else snprintf(s_iIp, sizeof s_iIp, lang::T("Aucune"));
                    break;
                }
                case I_NAND: Space(s_iNand, sizeof s_iNand, NcmStorageId_BuiltInUser); break;
                case I_SDFREE: Space(s_iSdFree, sizeof s_iSdFree, NcmStorageId_SdCard); break;
                case I_ZONE: case A_ZONE: {                      // (v83: the row opens the list; its line = the zone)
                    TimeLocationName n; memset(&n, 0, sizeof n);
                    if (R_SUCCEEDED(timeGetDeviceLocationName(&n))) {
                        Copy(s_iZone, sizeof s_iZone, n.name, sizeof n.name);
                        for (char *q = s_iZone; *q; q++) if (*q == '_') *q = ' ';
                    }
                    break;
                }
                case A_NICKNAME: { static SetSysDeviceNickName d; memset(&d, 0, sizeof d); if (R_SUCCEEDED(setsysGetDeviceNickname(&d))) Copy(s_devNick, sizeof s_devNick, d.nickname, sizeof d.nickname); break; }
                case I_FW: {
                    static SetSysFirmwareVersion fw; memset(&fw, 0, sizeof fw);
                    if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) Copy(s_iFw, sizeof s_iFw, fw.display_version, sizeof fw.display_version);
                    break;
                }
                case I_AMS: {
                    u64 v = 0;
                    if (R_SUCCEEDED(splInitialize())) {           // (opened only for this one question)
                        if (R_SUCCEEDED(splGetConfig((SplConfigItem)65000, &v)))   // Atmosphère's ExosphereApiVersion
                            snprintf(s_iAms, sizeof s_iAms, "%u.%u.%u", (unsigned)(v >> 56) & 255, (unsigned)(v >> 48) & 255, (unsigned)(v >> 40) & 255);
                        splExit();
                    }
                    if (!s_iAms[0]) snprintf(s_iAms, sizeof s_iAms, "?");
                    break;
                }
                case I_MODEL: {
                    SetSysProductModel m = SetSysProductModel_Invalid;
                    setsysGetProductModel(&m);
                    snprintf(s_iModel, sizeof s_iModel, "%s", m == SetSysProductModel_Nx ? "Switch V1 (Erista)" : m == SetSysProductModel_Iowa ? "Switch V2 (Mariko)" :
                             m == SetSysProductModel_Hoag ? "Switch Lite" : m == SetSysProductModel_Aula ? "Switch OLED" : "Switch");
                    break;
                }
                case I_SD: {
                    struct statvfs st;
                    if (statvfs("sdmc:/", &st) == 0) snprintf(s_iSd, sizeof s_iSd, lang::T("%.0f Go"), (double)st.f_blocks * st.f_frsize / 1e9);
                    break;
                }
                default: break;
            }
        }
        // live = applied while the slider moves (brightness, volume)
        bool Live(const Item &it) { const Bind b = BindOf(it); return b == B_BRIGHT || b == B_VOLUME; }
        // v70.5 (Yass: « plus smooth, pas d'un coup »): the backlight glides to the new brightness in ~0.3 s (one small step per
        // frame, applied to the backlight), and the setting is saved once, at the end - not at every step of the slider
        float s_brightCur = -1, s_brightTo = -1;
        bool  s_brightGlide = false;
        Result BrightTo(float to) {
            if (s_brightCur < 0) { float f = 0; if (R_FAILED(lblGetCurrentBrightnessSetting(&f))) f = to; s_brightCur = fmaxf(0.0f, fminf(1.0f, f)); }
            s_brightTo = fmaxf(0.0f, fminf(1.0f, to)); s_brightGlide = true;
            return 0;
        }
        void BrightGlide(float dt) {
            if (!s_brightGlide) return;
            s_brightCur += (s_brightTo - s_brightCur) * (1 - expf(-dt * 11.0f));
            const bool done = fabsf(s_brightTo - s_brightCur) < 0.004f;
            if (done) s_brightCur = s_brightTo;
            if (R_FAILED(lblSetCurrentBrightnessSetting(s_brightCur))) { s_brightGlide = false; return; }
            lblApplyCurrentBrightnessSettingToBacklight();
            if (done) { s_brightGlide = false; s_brightCur = -1; lblSaveCurrentSetting(); }   // v57 (review): always kept (and read again next time)
        }
        Result WriteItem(Item &it) {
            const int v = it.v;
            switch (BindOf(it)) {
                case B_BRIGHT: return BrightTo(v / 100.0f);   // v70.5: it glides (BrightGlide, every frame)
                case B_BRIGHT_AUTO: { Result rc = v ? lblEnableAutoBrightnessControl() : lblDisableAutoBrightnessControl(); if (R_SUCCEEDED(rc)) lblSaveCurrentSetting(); return rc; }
                case B_TV_RES: case B_TV_RGB: case B_TV_CEC: {
                    SetSysTvSettings t;
                    if (!GetTv(t)) return MAKERESULT(Module_Libnx, LibnxError_IoError);
                    const Bind b = BindOf(it);
                    if (b == B_TV_RES) t.tv_resolution = v;
                    if (b == B_TV_RGB) t.rgb_range = v == 1 ? 2 : (v == 2 ? 1 : 0);
                    if (b == B_TV_CEC) t.flags = v ? (t.flags | 4u) : (t.flags & ~4u);
                    return setsysSetTvSettings(&t);
                }
                case B_LOCK: { Result rc = setsysSetLockScreenFlag(v != 0); if (R_SUCCEEDED(rc)) Settings::s_lockOn = v != 0; return rc; }
                case B_VOLUME: {
                    bool capped = false;
                    const bool ok = Volume(nullptr, &v, s_loudOk ? nullptr : &capped);
                    if (capped) { it.v = kHeadCap; s_volCapped = true; }
                    return ok ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
                }
                case B_TV_SOUND: {
                    Result rc = audctlInitialize();
                    if (R_FAILED(rc)) return rc;
                    rc = audctlSetOutputModeSetting(AudioTarget_Tv, v == 2 ? AudioOutputMode_Pcm1ch : (v == 3 ? AudioOutputMode_Pcm6ch : (v == 1 ? AudioOutputMode_Pcm2ch : AudioOutputMode_PcmAuto)));
                    audctlExit();
                    return rc;
                }
                case B_UI_SOUNDS: audio::SetSfxOn(v != 0); Settings::Save(); return 0;
                case B_NOTIF_SOUND: notify::s_sound = v != 0; Settings::Save(); return 0;   // v85: Brume's own notifications (Notify.cpp)
                case B_MUSIC: audio::SetMusicOn(v != 0); Settings::Save(); return 0;   // the nappe, on or off (v92: « Mes musiques » removed)
                case B_LOCKCODE: return 0;                   // (v87: the code lives in brume/codes.txt, per profile: PressA asks for it)
                case B_GOLD: Settings::s_goldCube = v != 0; Settings::Save(); return 0;
                case B_CLOCKFONT: Settings::s_clockFont = v; Settings::s_clockFontDirty = true; Settings::Save(); return 0;
                case B_ASKUSER: Settings::s_askUser = v != 0; Settings::Save(); return 0;
                case B_NEXTENDO: Settings::s_nextendo = v != 0; Settings::Save(); return 0;
                case B_SPOTIFY: spotify::SetOn(v != 0, -1.0f); return 0;   // v98: its own file (brume/spotify/brume.ini); on = the panel opens
                case B_NTP: Settings::s_ntpAuto = v != 0; Settings::Save(); return 0;   // v70.8
                case B_H24: sysclock::s_h24 = v != 0; Settings::Save(); return 0;      // v83: the lock screen and the top bar follow
                case B_COVERS_AUTO: Settings::s_coversAuto = v != 0; Settings::Save(); return 0;   // v75
                case B_WAVE: Settings::s_wave = v != 0; Settings::Save(); return 0;
                case B_SKY:                                  // v53: off = back to the chosen ambient colour
                    Settings::s_sky = v != 0;
                    if (!v) for (int c = 0; c < 3; c++) Settings::s_ambientTarget[c] = kAmbientRgb[AmbientItem().v][c];
                    Settings::Save(); return 0;
                case B_BATTERY: Settings::s_battery = v != 0; Settings::Save(); return 0;
                case B_GLASS: Settings::s_glassPs3 = v == 0; Settings::Save(); return 0;   // v73
                case B_BRUMELANG: lang::Init(v); Settings::Save(); return 0;   // v64 (v65: at once, everywhere)
                case B_OVERLAY: return OverlaySet(v != 0);   // v69 (the console's own folder: read back when the category opens)
                case B_UNLOCK:                               // v59: the version is loaded now, then heard
                    Settings::s_unlockStyle = v & 1; audio::SetUnlock(v & 1); Settings::Save();
                    audio::Play(audio::Unlock12, 0.0f, 0.8f);
                    return 0;
                case B_THEME: Settings::ApplyTheme(v); return 0;   // v47 (v81: the whole look, Theme.cpp)
                case B_ACCENT: if (v >= 0 && v < 3) for (int c = 0; c < 3; c++) Settings::s_accentTarget[c] = kAccentRgb[v][c]; Settings::Save(); return 0;   // (v81: it glides)
                case B_PLANE: {
                    bool w0 = true;
                    nifmIsWirelessCommunicationEnabled(&w0);
                    Result rc = nifmSetWirelessCommunicationEnabled(!v);
                    if (R_SUCCEEDED(rc)) {
                        rc = setsysSetNfcEnableFlag(!v);
                        if (R_FAILED(rc)) nifmSetWirelessCommunicationEnabled(w0);   // v57 (review): never Wi-Fi off alone
                    }
                    if (R_SUCCEEDED(rc)) { kPlane[1].v = !v; kPlane[2].v = !v; }
                    return rc;
                }
                case B_WIFI: { Result rc = nifmSetWirelessCommunicationEnabled(v != 0); if (R_SUCCEEDED(rc)) kPlane[0].v = !kPlane[1].v && !kPlane[2].v; return rc; }
                case B_NFC: { Result rc = setsysSetNfcEnableFlag(v != 0); if (R_SUCCEEDED(rc)) kPlane[0].v = !kPlane[1].v && !kPlane[2].v; return rc; }
                case B_PAD_VIB: return setsysSetVibrationMasterVolume(v ? 1.0f : 0.0f);
                case B_PRO_WIRED: {
                    Result rc = setsysSetUsbFullKeyEnableFlag(v != 0);
                    if (R_SUCCEEDED(rc) && R_SUCCEEDED(hidsysInitialize())) { hidsysEnableUsbFullKeyController(v != 0); hidsysExit(); }   // v57: right now
                    return rc;
                }
                case B_ALBUM: return setsysSetPrimaryAlbumStorage(v == 0 ? SetSysPrimaryAlbumStorage_SdCard : SetSysPrimaryAlbumStorage_Nand);
                case B_SLEEP_H: case B_SLEEP_TV: case B_SLEEP_MEDIA: case B_WAKE_PLUG: {
                    SetSysSleepSettings s;
                    if (!GetSleep(s)) return MAKERESULT(Module_Libnx, LibnxError_IoError);
                    const Bind b = BindOf(it);
                    if (b == B_SLEEP_H) s.handheld_sleep_plan = v;
                    if (b == B_SLEEP_TV) s.console_sleep_plan = v;
                    if (b == B_SLEEP_MEDIA) s.flags = v ? (s.flags & ~1u) : (s.flags | 1u);
                    if (b == B_WAKE_PLUG) s.flags = v ? (s.flags | 2u) : (s.flags & ~2u);
                    return setsysSetSleepSettings(&s);
                }
                case B_LANG: {
                    if (v < 0 || v >= 6) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    u64 code = 0;
                    Result rc = setMakeLanguageCode(kLangs[v], &code);
                    return R_SUCCEEDED(rc) ? setsysSetLanguageCode(code) : rc;
                }
                case B_REGION: return v >= 0 && v < 4 ? setsysSetRegionCode(kRegions[v]) : MAKERESULT(Module_Libnx, LibnxError_BadInput);
                case B_CLOCK_SYNC: return setsysSetUserSystemClockAutomaticCorrectionEnabled(v != 0);
                default: return 0;
            }
        }
        void ReadTree(Item *items, int n, int depth) {
            for (int i = 0; i < n; i++) {
                ReadItem(items[i]);
                if (items[i].k == kFolder && depth < 3) ReadTree(items[i].kids, items[i].nkids, depth + 1);
            }
        }
        constexpr const char *kIni = "sdmc:/brume/reglages.ini";

        // ---- the mockup's numbers (v26 CSS / v27 SR) ----
        constexpr float CAR_X = 560, SEL_Y = 329, IT_S = 44, IT_GAP = 60, VAL_R = 1205, RING = 5.0f, RZ = -5.0f;
        constexpr float ROW_Y = 205, SEL_X = 330, CAT_GAP = 156, CAT_S = 62, COL_GAP = 280;   // v50: the XMB (mockup v26)

        // ---- small matrix kit (column-major), same conventions as Wheel.cpp ----
        struct M4 { float m[16]; };
        M4 Mul(const M4 &a, const M4 &b) { M4 r; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k]; r.m[c * 4 + rr] = s; } return r; }
        M4 Ident() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
        M4 Translate(float x, float y, float z) { M4 r = Ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
        M4 Scale(float s) { M4 r = Ident(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
        M4 RotX(float a) { M4 r = Ident(); r.m[5] = cosf(a); r.m[6] = sinf(a); r.m[9] = -sinf(a); r.m[10] = cosf(a); return r; }
        M4 RotY(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[2] = -sinf(a); r.m[8] = sinf(a); r.m[10] = cosf(a); return r; }
        M4 RotZ(float a) { M4 r = Ident(); r.m[0] = cosf(a); r.m[1] = sinf(a); r.m[4] = -sinf(a); r.m[5] = cosf(a); return r; }
        M4 Persp(float fovDeg, float aspect, float n, float f) {
            M4 r{}; const float t = 1.0f / tanf(fovDeg * PI / 360.0f);
            r.m[0] = t / aspect; r.m[5] = t; r.m[10] = f / (n - f); r.m[11] = -1; r.m[14] = n * f / (n - f);
            return r;
        }
        void Basis(const float *e, const float *c, float *x, float *y, float *z) {   // camera.lookAt: z = back, x = right, y = up
            z[0] = e[0] - c[0]; z[1] = e[1] - c[1]; z[2] = e[2] - c[2];
            float l = sqrtf(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]); for (int i = 0; i < 3; i++) z[i] /= l;
            x[0] = z[2]; x[1] = 0; x[2] = -z[0]; l = sqrtf(x[0] * x[0] + x[2] * x[2]); x[0] /= l; x[2] /= l;
            y[0] = z[1] * x[2] - z[2] * x[1]; y[1] = z[2] * x[0] - z[0] * x[2]; y[2] = z[0] * x[1] - z[1] * x[0];
        }
        M4 LookAt(const float *e, const float *c) {
            float x[3], y[3], z[3]; Basis(e, c, x, y, z);
            M4 r = Ident();
            for (int i = 0; i < 3; i++) { r.m[i * 4 + 0] = x[i]; r.m[i * 4 + 1] = y[i]; r.m[i * 4 + 2] = z[i]; }
            r.m[12] = -(x[0] * e[0] + x[1] * e[1] + x[2] * e[2]);
            r.m[13] = -(y[0] * e[0] + y[1] * e[1] + y[2] * e[2]);
            r.m[14] = -(z[0] * e[0] + z[1] * e[1] + z[2] * e[2]);
            return r;
        }
        struct alignas(16) MeshUbo {            // shaders/mesh_vsh.glsl + icon_fsh.glsl (std140)
            float mvp[16], model[16], base[4], mat[4], tint[4], emi[4], cam[4];
            float keyDir[4], keyCol[4], rimDir[4], rimCol[4], driftPos[4], driftCol[4], sky[4], ground[4], sh[9][4];
        };
        float Clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
        float Wrap(float d, int n) { return fmodf(fmodf(d, (float)n) + n + n / 2.0f, (float)n) - n / 2.0f; }

        // the value shown on the right (mockup valText)
        const char *Value(const Item &it, char *buf, int cap) {
            switch (it.k) {
                case kToggle: return it.v ? "Activé" : "Désactivé";
                case kChoice: { const char *o = it.opt[it.v >= 0 && it.v < it.nopt ? it.v : 0]; return o ? o : ""; }   // (v79: never out of the list)
                case kSlider: snprintf(buf, cap, "%d %%", it.v); return buf;
                case kInfo:   return it.info ? it.info : "";
                case kFolder: return ">";
                case kAction: return it.info ? it.info : "";   // v82: an action may show a state (Audio Bluetooth: « Connecté »)
                default:      return "";
            }
        }
        // the hint chips (baked « .hints .btn-g » sprites) + their label, like the mockup's .hints
        // v70 - the hints as the PS2 writes them (no more glass buttons): the button's letter, or its arrows, in colour,
        // then its word - « A Valider   B Retour   ◀ ▶ Régler »
        struct HintRow {
            Ui &ui; const Assets &a; float alpha;
            static constexpr float kHc = 720 - 28 - 13.5f;
            float Mark(float x, const char *chip, bool draw) const {   // the mark's width (drawn if draw)
                if (!strcmp(chip, "chipLeRi") || !strcmp(chip, "chipUpDo")) {
                    const bool lr = chip[4] == 'L';
                    if (draw) { ui.Arrow(x + 5, kHc, 5.5f, lr ? 0 : 2, ps2::A(ps2::kChoice, alpha)); ui.Arrow(x + 20, kHc, 5.5f, lr ? 1 : 3, ps2::A(ps2::kChoice, alpha)); }
                    return 26;
                }
                const char *t = "A";
                Rgba c = ps2::kChoice;
                if (!strcmp(chip, "chipB")) { t = "B"; c = ps2::kPink; }
                else if (!strcmp(chip, "chipY")) { t = "Y"; c = ps2::kGreen; }
                else if (!strcmp(chip, "chipX")) t = "X";
                else if (!strcmp(chip, "chipLR")) t = "LR";
                float w = 0;
                for (const char *q = t; *q;) {
                    const uint32_t cp = NextCodepoint(q);
                    w += draw ? ui.Glyph(fonts::kButton, cp, x + w, kHc - 10, ps2::A(c, alpha), 0, 0, 0, 0) : ui.Advance(fonts::kButton, cp);
                    if (*q) w += 7;                              // (L  R)
                }
                return w;
            }
            float Width(const char *chip, const char *label) const { return Mark(0, chip, false) + 10 + ui.Width(fonts::kHintBig, label, 0.5f); }
            float Skip(float x, const char *chip, const char *label) const {   // v79: where Draw would end, drawing nothing
                x += Mark(x, chip, false) + 10;
                for (const char *q = lang::T(label); *q;) x += ui.Advance(fonts::kHintBig, NextCodepoint(q)) + 0.5f;
                return x + 30;
            }
            float Draw(float x, const char *chip, const char *label) const {
                x += Mark(x, chip, true) + 10;
                for (const char *q = lang::T(label); *q;) x += ui.Glyph(fonts::kHintBig, NextCodepoint(q), x, kHc - 9.5f, ps2::A(ps2::kWords, 0.9f * alpha), 0, 0, 0, 0) + 0.5f;
                return x + 30;
            }
        };
    }

    namespace {
        // v83 - a time chosen by hand: « Heure automatique » goes off (at the next boot it would bring Internet's back), and says so
        void ClockByHand(float now) {
            if (!NtpItem().v) return;
            NtpItem().v = 0; Settings::s_ntpAuto = false; Settings::Save();
            notify::Post(now, "Heure automatique d\xc3\xa9sactiv\xc3\xa9" "e : ton heure reste", notify::kInfo);
        }
        void ZoneChanged(float) { ReadItem(kDate[3]); }   // v83: the row's line says the new zone

        // v85 - Données > Données de sauvegarde: JKSV (the homebrew that saves and restores the games' saves), started the way the
        // Apps page starts a homebrew (Games::LaunchNro: its own forwarder, else Brume Launcher, else the donor game): the entry
        // sound, the screen goes black, it starts once it is silent; back in the menu, the light comes back (Done)
        const char *const kJksv[] = { "sdmc:/switch/JKSV.nro", "sdmc:/switch/JKSV/JKSV.nro" };
        const char *JksvPath() { struct stat st; for (const char *p : kJksv) if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) return p; return nullptr; }
        const char *s_appPath = nullptr;
        float s_appAt = -100, s_appBackAt = -100;              // started (black) / the light coming back (0: back, waiting for steady frames)
        bool  s_appStarted = false;
        Settle s_appSettle;
        bool AppBusy() { return s_appAt > 0; }
    }

    void Settings::Sleep() { if (s_appAt > 0 && !s_appStarted) { s_appAt = -100; s_appBackAt = -100; } }   // v86: Power before JKSV started: it never starts behind the lock screen
    void Settings::Done() { if (s_appAt > 0 && s_appBackAt < 0) { s_appBackAt = 0; s_appSettle.on = false; } }

    // v29 - the real settings kept on the SD (only our own file, sdmc:/brume/reglages.ini: key=value lines)
    void Settings::Load() {
        bool h24Seen = false;                             // v83: no choice saved yet: the console's language decides
        FILE *f = fopen(kIni, "rb");
        if (f) {
            char line[96];
            while (fgets(line, sizeof line, f)) {
                int v = 0;
                if (sscanf(line, "ambiance=%d", &v) == 1 && v >= 0 && v < NOF(oAmbient)) AmbientItem().v = v;
                else if (sscanf(line, "vibrations=%d", &v) == 1) VibrationItem().v = v ? 1 : 0;
                else if (sscanf(line, "sons=%d", &v) == 1) UiSoundsItem().v = v ? 1 : 0;
                else if (sscanf(line, "musique=%d", &v) == 1) MusicItem().v = v ? 1 : 0;   // (v92: « musiques= », « morceau= » are gone: ignored)
                else if (sscanf(line, "code=%d", &v) == 1) CodeItem().v = v ? 1 : 0;
                else if (sscanf(line, "cube=%d", &v) == 1) GoldItem().v = v ? 1 : 0;
                else if (sscanf(line, "accent=%d", &v) == 1 && v >= 0 && v < NOF(oAccent)) AccentItem().v = v;
                else if (sscanf(line, "police=%d", &v) == 1 && v >= 0 && v < NOF(oClock)) ClockItem().v = v;
                else if (sscanf(line, "choisir=%d", &v) == 1) AskUserItem().v = v ? 1 : 0;
                else if (sscanf(line, "theme=%d", &v) == 1 && v >= 0 && v < NOF(oTheme)) ThemeItem().v = v;
                else if (sscanf(line, "nextendo=%d", &v) == 1) NextendoItem().v = v ? 1 : 0;
                else if (sscanf(line, "notifson=%d", &v) == 1) NotifSoundItem().v = v ? 1 : 0;   // v85 (absent: on)
                else if (sscanf(line, "heure=%d", &v) == 1) NtpItem().v = v ? 1 : 0;   // v70.8
                else if (sscanf(line, "h24=%d", &v) == 1) { H24Item().v = v ? 1 : 0; h24Seen = true; }   // v83
                else if (sscanf(line, "jaquettes=%d", &v) == 1) CoversAutoItem().v = v ? 1 : 0;   // v75
                else if (sscanf(line, "affichage=%d", &v) == 1) LayoutItem().v = v ? 1 : 0;
                else if (sscanf(line, "vague=%d", &v) == 1) WaveItem().v = v ? 1 : 0;
                else if (sscanf(line, "ciel=%d", &v) == 1) SkyItem().v = v ? 1 : 0;
                else if (sscanf(line, "batterie=%d", &v) == 1) BatteryItem().v = v ? 1 : 0;
                else if (sscanf(line, "deverrouillage=%d", &v) == 1) UnlockItem().v = v ? 1 : 0;
                else if (sscanf(line, "langue=%d", &v) == 1) BrumeLangItem().v = v >= 0 && v <= 2 ? v : 0;
                else if (sscanf(line, "verre=%d", &v) == 1) GlassItem().v = v ? 1 : 0;   // v73
                else if (sscanf(line, "secret=%d", &v) == 1) s_secret = v != 0;
                else if (!strncmp(line, "codeseq=", 8)) {
                    bool good = true;
                    for (int k = 0; k < 4; k++) good = good && line[8 + k] >= '0' && line[8 + k] <= '9';   // (v87: 10 symbols; 0..3 = the old directions)
                    if (good) { for (int k = 0; k < 4; k++) Lock::s_code[k] = (uint8_t)(line[8 + k] - '0'); s_codeSet = true; }
                }
            }
            fclose(f);
        }
        if (AmbientItem().v >= (s_secret ? NOF(oAmbient) : NOF(oAmbient) - 1)) AmbientItem().v = 0;
        for (int c = 0; c < 3; c++) s_ambient[c] = s_ambientTarget[c] = kAmbientRgb[AmbientItem().v][c];
        haptic::SetEnabled(VibrationItem().v != 0);
        audio::SetSfxOn(UiSoundsItem().v != 0);
        audio::SetMusicOn(MusicItem().v != 0);
        if (!s_codeSet) CodeItem().v = 0;
        s_goldCube = GoldItem().v != 0;
        s_clockFont = ClockItem().v;
        s_askUser = AskUserItem().v != 0;
        s_nextendo = NextendoItem().v != 0;
        notify::s_sound = NotifSoundItem().v != 0;    // v85
        s_ntpAuto = NtpItem().v != 0;
        if (!h24Seen) H24Item().v = sysclock::DefaultH24() ? 1 : 0;
        sysclock::s_h24 = H24Item().v != 0;              // v83
        s_coversAuto = CoversAutoItem().v != 0;          // v75
        s_xmb = LayoutItem().v == 0;
        s_wave = WaveItem().v != 0; s_waveCur = s_wave ? 1.0f : 0.0f;
        s_sky = SkyItem().v != 0; s_battery = BatteryItem().v != 0;
        s_glassPs3 = GlassItem().v == 0;                 // v73
        s_unlockStyle = UnlockItem().v; audio::SetUnlock(s_unlockStyle);   // (before audio::Init: only remembered)
        lang::Init(BrumeLangItem().v);                   // v64: before anything is drawn (and before the baked labels load)
        s_themeLight = s_themeLightCur = kThemes[ThemeItem().v].light;
        for (int c = 0; c < 3; c++) s_accent[c] = s_accentTarget[c] = kAccentRgb[AccentItem().v][c];
        theme::Snap(ThemeItem().v);                      // v81: the look, at once
        AmbientItem().nopt = s_secret ? NOF(oAmbient) : NOF(oAmbient) - 1;   // the secret colour, once found
        if (AmbientItem().v >= AmbientItem().nopt) AmbientItem().v = 0;
        Lock::s_codeOn = CodeItem().v != 0;
        CodeText();
        bool lock = true;                                          // v43: the console's own lock screen setting
        if (R_SUCCEEDED(setsysGetLockScreenFlag(&lock))) s_lockOn = lock;
        {   // v85: the error reports to Nintendo have no row any more - never left on (an older Brume could switch them on)
            SetSysErrorReportSharePermission p = SetSysErrorReportSharePermission_NotConfirmed;
            if (R_SUCCEEDED(setsysGetErrorReportSharePermission(&p)) && p == SetSysErrorReportSharePermission_Granted)
                setsysSetErrorReportSharePermission(SetSysErrorReportSharePermission_Denied);
        }
    }
    void Settings::Save() {
        FILE *f = fopen(kIni, "wb");
        if (!f) return;
        fprintf(f, "ambiance=%d\nvibrations=%d\nsons=%d\nmusique=%d\ncode=%d\ncube=%d\nsecret=%d\naccent=%d\npolice=%d\nchoisir=%d\ntheme=%d\nnextendo=%d\naffichage=%d\nvague=%d\nciel=%d\nbatterie=%d\ndeverrouillage=%d\nlangue=%d\nheure=%d\nverre=%d\n", AmbientItem().v, VibrationItem().v, UiSoundsItem().v, MusicItem().v ? 1 : 0, CodeItem().v,
                GoldItem().v, s_secret ? 1 : 0, AccentItem().v, ClockItem().v, AskUserItem().v, ThemeItem().v, NextendoItem().v, LayoutItem().v, WaveItem().v,
                SkyItem().v, BatteryItem().v, UnlockItem().v, BrumeLangItem().v, NtpItem().v, GlassItem().v);
        if (s_codeSet) fprintf(f, "codeseq=%d%d%d%d\n", Lock::s_code[0] % 10, Lock::s_code[1] % 10, Lock::s_code[2] % 10, Lock::s_code[3] % 10);
        fprintf(f, "jaquettes=%d\n", CoversAutoItem().v);   // v75
        fprintf(f, "notifson=%d\n", NotifSoundItem().v);   // v85
        fprintf(f, "h24=%d\n", H24Item().v);           // v83
        fclose(f);
    }
    void Settings::SetClockFont(int v) { ClockItem().v = v; s_clockFont = v; Save(); }
    // v47 - a theme's preset: the ambient colour and the accent follow, the light glides; v81: and its whole look (Theme.cpp)
    void Settings::ApplyTheme(int v) {
        if (v < 0 || v >= NOF(oTheme)) return;
        ThemeItem().v = v;
        const ThemePreset &t = kThemes[v];
        if (t.ambient < AmbientItem().nopt) { AmbientItem().v = t.ambient; if (!s_sky) for (int c = 0; c < 3; c++) s_ambientTarget[c] = kAmbientRgb[t.ambient][c]; }
        AccentItem().v = t.accent;
        for (int c = 0; c < 3; c++) s_accentTarget[c] = kAccentRgb[t.accent][c];
        s_themeLight = t.light;
        theme::Set(v);
        Save();
    }
    // v44 - the golden cube caught: its colour joins the ambient colours (kept for good)
    void Settings::UnlockSecret(float now) {
        const bool first = !s_secret;
        s_secret = true;
        AmbientItem().nopt = NOF(oAmbient);
        Save();
        if (s_toast) s_toast(now, first ? "Secret trouv\xc3\xa9 : le cube dor\xc3\xa9 ! Nouvelle couleur : R\xc3\xa9glages > Brume > Couleur d'ambiance"
                                          : "Le cube dor\xc3\xa9, encore lui !");
    }
    void Settings::Ambient(float *out) { out[0] = s_ambient[0]; out[1] = s_ambient[1]; out[2] = s_ambient[2]; out[3] = s_themeLightCur; }   // w: v47 the theme's light

    // v43 - the category's real values, read when it opens (a few quick calls to the system services)
    void Settings::Refresh() { ReadTree(kCat[m_cat].items, kCat[m_cat].n, 0); }

    // v43 - a row that opens something: our own action, a Switch applet, or the Nintendo page for what we don't do yet
    void Settings::Act(int bind, const char *name, float now) {
        audio::Play(audio::Tick2400, 0, 1.0f);
        haptic::Tap(0.6f);
        const AccountUid uid = PlayerUid();
        switch ((Bind)bind) {
            case A_PROFILE: if (s_open) s_open(-1, now); return;
            case A_CONTROLLERS: if (s_open) s_open(3, now); return;
            case A_PADTEST: if (s_padTest) s_padTest(now); return;   // v84: Brume's own test screen (Pads::TestBegin)
            case A_REMAP: if (s_open) s_open(-4, now); return;       // v84: the Switch's button mapping screen (controller applet)
            case A_CALIBRATE: {                                      // v84: calibrating writes the controller's calibration: never here
                popup::Spec s;                                       // (the test chosen first: it measures the drift)
                s.kind = popup::kInfo; s.title = "Calibrer les sticks";
                s.text = "La calibration est un outil du menu Nintendo, que Brume remplace. Brume ne réécrit jamais ces réglages (trop risqué). Le test mesure déjà la dérive de tes sticks.";
                s.yes = "Tester les manettes"; s.no = "Fermer";
                s.onYes = [](void *, float t) { if (s_padTest) s_padTest(t); };
                popup::Show(now, s);
                return;
            }
            case A_NETCONNECT: if (s_open) s_open(-2, now); return;   // v70.7: the Switch's own Internet screen
            case A_AMIIBO: if (s_open) s_open(-3, now); return;       // v70.7: amiibo settings (nickname and owner)
            case A_ALBUM: if (s_open) s_open(2, now); return;         // v70.7: copy or delete in the Switch's album
            case A_MII: { const Result rc = qext_launch_applet(2); if (R_FAILED(rc)) { char m[96]; snprintf(m, sizeof m, lang::T("Mii : erreur 0x%X"), rc); Toast(now, m); } return; }
            case A_EDIT_NICK: case A_EDIT_ICON: case A_ADD_USER: {
                haptic::Stop();
                appletUnlockForeground();
                Result rc = bind == A_ADD_USER ? pselShowUserCreator() : (bind == A_EDIT_NICK ? pselShowUserNicknameEditor(uid) : pselShowUserIconEditor(uid));
                if (R_FAILED(rc)) { char m[96]; snprintf(m, sizeof m, lang::T("Cette page n'a pas pu s'ouvrir (0x%X)"), rc); Toast(now, m); }
                Refresh();
                if (s_profilesChanged) s_profilesChanged();   // v57 (review): the bar's avatar and nickname
                return;
            }
            case A_NICKNAME: {
                haptic::Stop();
                char out[0x80] = {};
                BrumeSwkbdOptions o = { s_devNick, "Surnom de la console", 1, 32, 1 };
                appletUnlockForeground();
                if (qext_swkbd_show_ex(&o, out, sizeof out) > 0) {
                    static SetSysDeviceNickName d;
                    memset(&d, 0, sizeof d);
                    snprintf(d.nickname, sizeof d.nickname, "%s", out);
                    const Result rc = setsysSetDeviceNickname(&d);
                    if (R_SUCCEEDED(rc)) { snprintf(s_devNick, sizeof s_devNick, "%s", out); audio::Bell(audio::HarmonyNote(0, 72, 84), 0.04f, true); }
                    else { char m[96]; snprintf(m, sizeof m, lang::T("Le surnom n'a pas pu changer (0x%X)"), rc); Toast(now, m); }
                }
                return;
            }
            case A_SETCODE: StartCode(now); return;
            case A_MANAGE:                                          // v67 (v79: straight there - Wheel::Warp, Entry)
                if (s_manage) { audio::PlaySweep(audio::Whoosh, 0.3f, -0.6f, 0.45f, 0.5f); s_manage(now); }
                return;
            case A_CLOCK_NOW: ntp::SyncNow(now); return;           // v70.8: the clock set by Internet, now
            case A_SET_TIME: datepages::OpenPicker(now, ClockByHand); return;   // v83: Brume's own pages (DatePages.cpp)
            case A_ZONE: datepages::OpenZones(now, ZoneChanged); return;
            case A_COVERS: covers::DownloadNow(now); return;       // v75: the covers, downloaded by the console
            case A_JOURNAL: journal::Show(now); return;          // v85: Brume's journal (Journal.cpp)
            case A_DISCORD: if (s_discord) s_discord(now); return;   // v95: the Discord voice QR (over the settings)
            case A_SPOTIFY: spotify::OpenPanel(now); return;     // v98: the Spotify panel (the same as ZL)
            case A_LEAVE: {                                      // v85: the emergency exit, asked from here (a deletion's popup)
                popup::Spec s;
                s.kind = popup::kDanger; s.title = "Revenir au menu d'origine ?";
                s.text = "Brume s'efface et la console red\xc3\xa9marre sur l'ancien menu. Pour revenir : renomme menu.ini.off en menu.ini (dossier qlaunch-ext/config) sur la carte SD.";
                {
                    struct stat st;                              // (Brume alone on the card: the Nintendo menu comes back - its own words)
                    if (stat("sdmc:/qlaunch-ext/menus/menu.dnro", &st) != 0)
                        s.text = "Brume s'efface et la console red\xc3\xa9marre sur le menu Nintendo. Pour revenir : renomme 0100000000001000_off en 0100000000001000 (dossier atmosphere/contents) sur la carte SD.";
                }
                s.yes = "Quitter Brume"; s.no = "Annuler";
                s.onYes = [](void *, float t) { if (s_leave) s_leave(t); };
                popup::Show(now, s);
                return;
            }
            case A_SAVES: {                                      // v85: JKSV, if it is on the card - else, say how to get it
                const char *p = JksvPath();
                if (!p) {
                    popup::Spec s;
                    s.kind = popup::kInfo; s.title = "JKSV n'est pas sur ta carte";
                    s.text = "JKSV sauvegarde et restaure les sauvegardes de tes jeux. Installe-le sur la carte SD (switch/JKSV.nro), avec sphaira par exemple.";
                    s.yes = "OK";
                    popup::Show(now, s);
                    return;
                }
                if (AppBusy()) return;
                s_appPath = p; s_appAt = now; s_appBackAt = -100; s_appStarted = false;
                audio::Bell(audio::HarmonyNote(0, 72, 84), 0.05f, true, 0.05f);
                audio::Depart(0.3f);                             // (as the Apps page: the soundscape fades under the entry sound)
                audio::EntrySound(true, 0.0f);
                haptic::Swell(0.8f, 0.6f);
                return;
            }
            case A_DISCONNECT: {
                const HidNpadIdType ids[8] = { HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4,
                                               HidNpadIdType_No5, HidNpadIdType_No6, HidNpadIdType_No7, HidNpadIdType_No8 };
                for (HidNpadIdType id : ids) hidDisconnectNpad(id);
                Toast(now, "Manettes sans fil d\xc3\xa9" "connect\xc3\xa9" "es : un bouton pour les reconnecter");
                return;
            }
            default:                                         // v70.7: no Nintendo page to open (see above): say it, softly
                (void)name;
                audio::Play(audio::ThockSnd, 0, 0.5f);
                m_refuseAt = now;                            // v79: the row shakes its head (a soft « no »)
                Toast(now, "Pas encore dans Brume");
                return;
        }
    }

    int Settings::Count(int depth) const {
        const Item *items = kCat[m_cat].items; int n = kCat[m_cat].n;
        for (int d = 0; d < depth; d++) { const Item &f = items[m_path[d]]; items = f.kids; n = f.nkids; }
        return n;
    }
    static Item *Column(int cat, const int *path, int depth, int &n) {
        Item *items = kCat[cat].items; n = kCat[cat].n;
        for (int d = 0; d < depth; d++) { Item &f = items[path[d]]; items = f.kids; n = f.nkids; }
        return items;
    }
    // v82 - is this column Audio Bluetooth's page? (BtAudio.cpp: open while it is the current column and the page is on screen)
    static bool BtColumn(int cat, const int *path, int depth) {
        if (depth < 1 || depth > 2) return false;
        int n; const Item *up = Column(cat, path, depth - 1, n);
        return path[depth - 1] >= 0 && path[depth - 1] < n && up[path[depth - 1]].kids == kBt;
    }
    static bool s_btHere = false;                        // the settings page is the one on screen (DrawUi, every frame)

    // a new column (category change, folder): rows start 18 px lower and faded (mockup: translateY(18px), fade).
    // v79: they come one after the other (m_colT0: the stagger, Update), and the deeper columns of the old one are gone
    void Settings::ResetCol(int depth, bool fromBelow) {
        for (int i = 0; i < 16; i++) m_row[depth][i] = Row{ 0, 0, 0, 1, 0, 0, 0, 0, -9999, 0, -100, -1, 0 };
        for (int i = 0; i < 16; i++) m_row[depth][i].y = -1e4f;
        m_colX[depth] = fromBelow ? 0 : 140; m_colA[depth] = 0; m_colXV[depth] = m_colAV[depth] = 0;
        m_colT[depth] = 1; m_colTV[depth] = 0; m_colT0[depth] = m_t;
        for (int d = depth + 1; d < 3; d++) { m_colA[d] = 0; m_colAV[d] = 0; }
    }

    void Settings::Turn(int dc, float now) {
        m_target += (float)dc; m_tweenFrom = m_cur; m_tweenT0 = now;
        m_cat = ((int)lroundf(m_target) % kCats + kCats) % kCats;
        audio::PlaySweep(audio::Whoosh, dc * 0.7f, -dc * 0.6f, 1.3f);      // whoosh(dc)
        haptic::Tick(0.8f);
        audio::Nav(76, dc * 0.25f);                                          // navSound
        m_nameFadeT0 = now; m_nameSwapAt = now + 0.26f;                     // the name fades out and back (xCarName)
        m_depth = 0; m_sel[0] = 0; m_opt.on = false;
        ResetCol(0, true);
    }
    void Settings::Enter(float now) {
        int n; Item *col = Column(m_cat, m_path, m_depth, n);
        if (m_depth >= 2 || col[m_sel[m_depth]].k != kFolder) return;
        m_path[m_depth] = m_sel[m_depth]; m_depth++; m_sel[m_depth] = 0;
        ResetCol(m_depth, false);
        audio::Play(audio::Tick2400, 0, 1.0f);
        audio::PlaySweep(audio::Whoosh, -0.3f, 0.4f, 0.35f, 0.5f);
        (void)now;
    }
    void Settings::Back(float now) {
        if (!m_depth) return;
        m_depth--;
        audio::Play(audio::Tick1700, 0, 1.0f);
        (void)now;
    }
    void Settings::OpenOpt(float now) {
        int n; Item *col = Column(m_cat, m_path, m_depth, n);
        Item &it = col[m_sel[m_depth]];
        m_opt.on = true; m_opt.at = now; m_opt.slider = it.k == kSlider; m_opt.old = it.v;
        s_loudOk = false; s_volCapped = false;                   // v57: the headphones' warning comes back each time
        m_opt.sel = it.k == kToggle ? (it.v ? 0 : 1) : it.v;
        m_optScroll = OptFirst(it.k == kToggle ? 2 : it.nopt, m_opt.sel); m_optScrollV = 0;   // v70.9: opens already on it
        // v79: another item, or the panel had gone - its options start from nothing (then come in one by one, Update)
        if (m_optIt != &it || m_optK < 0.05f) { for (int k = 0; k < kOptMax; k++) m_oe[k] = m_oev[k] = 0; m_optK = m_optKV = 0; }
        m_optIt = &it;
        m_optHi = m_optDot = (float)m_opt.sel; m_optHiV = m_optDotV = 0;
        m_sl = (float)it.v; m_slV = 0; m_slNudge = m_slNudgeV = 0;
        audio::PlaySweep(audio::Whoosh, -0.2f, 0.5f, 0.35f, 0.4f);
    }
    void Settings::CloseOpt(float now, bool chosen) {
        m_opt.on = false; m_opt.at = now;               // (they fade out; a value chosen: first the dot glides to it, ~0.16 s)
        m_optHold = chosen ? now + Pace(0.22f) : now;
    }

    // v44 - choosing the lock code. v87 (Yass: « un vrai PIN comme sur la PS4 ») - like the PS4's passcode screen: the current
    // code first if there is one, then the new one, then once more to confirm (they must match); four presses among ten
    // buttons (Pin.hpp), each one a note of the chord; B erases, then cancels. Per profile (the active one).
    void Settings::StartCode(float now, int purpose) {
        const bool has = pin::Get(Profiles::Active(), m_rec.cur);
        if (purpose == 1 && !has) return;
        m_rec.on = true; m_rec.n = 0; m_rec.at = m_rec.stepAt = now; m_rec.purpose = purpose; m_rec.step = has ? 0 : 1;
        m_rec.doneAt = m_rec.badAt = m_rec.checkAt = -1; m_rec.lit = -1;
        audio::Bell(audio::HarmonyNote(3, 74, 88), 0.03f, true);
        audio::PlaySweep(audio::Whoosh, -0.2f, 0.4f, 0.4f, 0.4f);
        haptic::Tap(0.5f);
    }
    void Settings::CodeInput(float now, u64 down) {
        if (!m_rec.on) return;
        const int s = pin::FromButtons(down);
        if (s >= 0) CodeKey(s, now);
        else if (down & HidNpadButton_B) PressB(now);
    }
    void Settings::CodeCheck(float now) {
        bool same = true;
        if (m_rec.step == 0) {                                   // the current one
            for (int k = 0; k < 4; k++) same = same && m_rec.dir[k] == m_rec.cur[k];
            if (!same) { m_rec.badAt = now; m_rec.bad = "Ce n'est pas le bon code"; }
            else if (m_rec.purpose == 1) {                       // removing it: done
                char m[128];
                if (pin::Set(Profiles::Active(), nullptr)) { snprintf(m, sizeof m, lang::T("Code retir\xc3\xa9 pour %s"), Profiles::ActiveName()); notify::Post(now, m, notify::kDone); m_rec.doneAt = now; }
                else { m_rec.on = false; popup::Error(now, "Le code n'a pas pu changer", "La carte SD n'a pas voulu \xc3\xa9" "crire brume/codes.txt.", 0); }
                return;
            } else { m_rec.step = 1; m_rec.n = 0; m_rec.stepAt = now; audio::Bell(audio::HarmonyNote(2, 74, 86), 0.03f, true); }
        } else if (m_rec.step == 1) {                            // the new one: once more
            for (int k = 0; k < 4; k++) m_rec.first[k] = m_rec.dir[k];
            m_rec.step = 2; m_rec.n = 0; m_rec.stepAt = now;
            audio::Bell(audio::HarmonyNote(1, 74, 86), 0.03f, true);
        } else {                                                 // the confirmation
            for (int k = 0; k < 4; k++) same = same && m_rec.dir[k] == m_rec.first[k];
            if (!same) { m_rec.badAt = now; m_rec.bad = "Les deux codes ne sont pas pareils"; return; }
            if (!pin::Set(Profiles::Active(), m_rec.first)) { m_rec.on = false; popup::Error(now, "Le code n'a pas pu changer", "La carte SD n'a pas voulu \xc3\xa9" "crire brume/codes.txt.", 0); return; }
            for (int k = 0; k < 4; k++) audio::Bell(audio::HarmonyNote(k, 74, 88), 0.04f, true, 0.1f + k * 0.06f);
            haptic::Tap(0.8f);
            char m[128]; snprintf(m, sizeof m, lang::T("Code enregistr\xc3\xa9 pour %s"), Profiles::ActiveName());
            notify::Post(now, m, notify::kDone, false);
            m_rec.doneAt = now;
        }
    }
    void Settings::CodeKey(int sym, float now) {
        if (m_rec.n >= 4 || m_rec.doneAt > 0 || m_rec.badAt > 0 || m_rec.checkAt > 0 || sym < 0 || sym >= pin::kSyms) return;
        const int i = m_rec.n;
        m_rec.dir[m_rec.n++] = (uint8_t)sym; m_rec.keyAt[i] = now; m_rec.lit = sym; m_rec.litAt = now;
        audio::Bell(audio::HarmonyNote(i, 74, 86), 0.045f, true);
        audio::Crystal((i - 1.5f) * 0.25f, 0.35f, audio::HarmonyNote(i, 86, 98));
        haptic::Tick(0.6f);
        if (m_rec.n == 4) m_rec.checkAt = now + 0.25f;
    }

    void Settings::VolWarn(float now) {                          // v57 (review): stopped at 75 % on the headphones - once
        if (!s_volCapped) return;
        s_volCapped = false; s_loudOk = true;
        audio::Thock(0, 0.8f, 0.05f); haptic::Tap(0.5f);
        Toast(now, "Casque : volume prot\xc3\xa9g\xc3\xa9 \xc3\xa0 75 %. Pousse encore pour monter plus haut");
    }

    // v70.9 (Yass: « la liste est trop longue et dépasse de l'écran »): at most kOptVis options are shown; the list scrolls
    // (it glides) to keep the chosen one in view, the rows at its edges fade, small arrows say there is more
    static constexpr int kOptVis = 7;
    float Settings::OptFirst(int no, int sel) {
        if (no <= kOptVis) return 0;
        int f = sel - kOptVis / 2;
        if (f < 0) f = 0;
        if (f > no - kOptVis) f = no - kOptVis;
        return (float)f;
    }
    float Settings::OptTop(int no, float selY) const {
        const int vis = no < kOptVis ? no : kOptVis;
        // (v79: from the gliding light, not the chosen index: like the level before, the chosen option stays level with its
        // row and the list GLIDES under the light - it used to jump 48 px at every press)
        return fmaxf(96.0f, fminf(720.0f - 74 - vis * 48 - 10, selY - ((m_optHi - m_optScroll + 0.5f) * 48 + 10)));
    }

    void Settings::Move(int dc, int dr, float now) {
        if (AppBusy()) return;                                   // v85: JKSV is starting
        if (datepages::Open()) { datepages::Move(dc, dr, now); return; }   // v83: Date et heure's own pages
        if (journal::Open()) { journal::Move(dc, dr, now); return; }       // v85: Brume's journal
        if (m_rec.on) {                                          // v44: the code being chosen
            if (m_rec.doneAt < 0) CodeKey(dr < 0 ? 0 : (dc > 0 ? 1 : (dr > 0 ? 2 : 3)), now);
            return;
        }
        int n; Item *col = Column(m_cat, m_path, m_depth, n);
        if (m_opt.on) {
            Item &it = col[m_sel[m_depth]];
            if (m_opt.slider && dc) {
                const int nv = it.v + dc * 5 < 0 ? 0 : it.v + dc * 5 > 100 ? 100 : it.v + dc * 5;
                m_slNudgeV += dc * (nv != it.v ? 320.0f : 180.0f);   // v79: the arrow pressed gives (at the end: the number bumps)
                if (nv != it.v) { it.v = nv; audio::Play(audio::Deplacement, dc * 0.3f, 1.0f); if (Live(it)) WriteItem(it); VolWarn(now); } else audio::Thock(dc * 0.5f, 0.8f, 0.05f);
            }
            if (!m_opt.slider && dr) {
                const int no = it.k == kToggle ? 2 : it.nopt, s = m_opt.sel + dr;
                if (s < 0 || s >= no) { audio::Thock(0, 0.8f, 0.05f); Bump(dr); } else { m_opt.sel = s; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.6f); }
            }
            return;
        }
        if (!m_open) { if (dc) Turn(dc, now); else if (dr) audio::Thock(0, 0.8f, 0.05f); return; }
        if (dc) {
            if (m_depth == 0 && s_xmb) {                 // v50: the XMB's row of categories
                const int c = m_cat + dc;
                if (c < 0 || c >= kCats) { audio::Thock(dc * 0.6f, 0.8f, 0.06f); return; }
                // v70.6 (Yass: « pas d'animation sur l'horizontale »): no jump any more - the row glides to it with the ring's
                // spring (m_cur follows m_target in Update), and the new column comes in softly from that side
                m_cat = c; m_shownCat = c; m_target = (float)c; m_tweenFrom = m_cur; m_tweenT0 = now;
                Refresh();                               // v56 (review): the console's real values for this category
                audio::Nav(76, dc * 0.25f);
                audio::PlaySweep(audio::Whoosh, dc * 0.4f, -dc * 0.4f, 0.4f, 0.3f);
                haptic::Tick(0.7f);
                m_sel[0] = 0; m_opt.on = false; ResetCol(0, true);
                m_colX[0] = dc * 60.0f;                  // v70.6: from 60 px on that side (and 18 px lower, fading in: the rows)
                return;
            }
            if (m_depth == 0) { Turn(dc, now); return; }
            if (dc < 0) { Back(now); return; }
            if (col[m_sel[m_depth]].k == kFolder) { Enter(now); return; }
            audio::Thock(dc * 0.5f, 0.8f, 0.05f); return;
        }
        if (dr) {
            const int s = m_sel[m_depth] + dr;
            if (s < 0 || s >= n) { audio::Thock(0, 0.8f, 0.05f); Bump(dr); return; }   // v79: the list gives a little
            m_sel[m_depth] = s; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.7f);
        }
    }

    void Settings::PressA(float now) {
        if (AppBusy()) return;                                   // v85
        if (datepages::Open()) { datepages::PressA(now); return; }   // v83
        if (journal::Open()) { journal::PressA(now); return; }       // v85
        if (m_rec.on) return;                                    // (the code takes directions only)
        m_flash = 1.0f;                                          // v73: the chosen icon's glass lights up from inside, then fades
        int n; Item *col = Column(m_cat, m_path, m_depth, n);
        if (m_opt.on) {
            Item &it = col[m_sel[m_depth]];
            audio::Play(audio::Tick2400, 0, 0.8f); haptic::Tap(0.8f);
            if (!m_opt.slider) {
                const int before = it.v;
                if (it.k == kToggle) it.v = m_opt.sel == 0 ? 1 : 0; else it.v = m_opt.sel;
                audio::Bell(74 + (m_opt.sel % 4) * 3, 0.03f, true);
                if (&it == &AmbientItem()) {
                    // v70.9 (Yass: « ct bien ciel en fonction de l'heure le problème »): the sky would paint over the chosen colour
                    // at every frame - choosing a colour switches it off, and says so
                    if (s_sky) { s_sky = false; SkyItem().v = 0; Toast(now, "Ciel selon l'heure d\xc3\xa9sactiv\xc3\xa9 : ta couleur reste"); }
                    for (int c = 0; c < 3; c++) s_ambientTarget[c] = kAmbientRgb[it.v][c];
                    Save();
                }
                if (&it == &VibrationItem()) { haptic::SetEnabled(it.v != 0); if (it.v) haptic::Tap(1.0f); Save(); }
                if (it.v != before) {                            // v43: the real setting
                    const Result rc = WriteItem(it);
                    const Bind b = BindOf(it);
                    char m[128];
                    if (R_FAILED(rc)) { it.v = before; snprintf(m, sizeof m, lang::T("Ce r\xc3\xa9glage n'a pas pu changer (0x%X)"), rc); Toast(now, m); }
                    else if (b == B_LOCKCODE) {                  // v87: on = choose it (none yet); off = remove it (the current one first)
                        const bool want = it.v != 0;
                        it.v = HasCode() ? 1 : 0;
                        if (want && !it.v) StartCode(now, 0);
                        else if (!want && it.v) {
                            static char txt[200];
                            snprintf(txt, sizeof txt, lang::T("Le code de %s ne sera plus demand\xc3\xa9. Tape-le une derni\xc3\xa8re fois pour confirmer."), Profiles::ActiveName());
                            popup::Spec s;
                            s.kind = popup::kAsk; s.title = "Retirer le code ?"; s.text = txt; s.yes = "Retirer"; s.no = "Annuler";
                            s.ctx = this; s.onYes = [](void *c, float t) { ((Settings *)c)->StartCode(t, 1); };
                            popup::Show(now, s);
                        }
                    }
                    else if (b == B_LAYOUT) { it.v = before; m_dipTo = m_opt.sel == 0 ? 1 : 0; m_dipAt = now; CloseOpt(now, false); return; }   // v50 (v79: it dips, Update swaps)
                    else if (b == B_TV_RES || b == B_TV_RGB || b == B_TV_CEC) Toast(now, "Pris en compte sur la TV (au prochain branchement si besoin)");
                    else if (b == B_OVERLAY || b == B_LANG || b == B_REGION) {   // v78: it needs a restart - a popup: now, or later?
                        popup::Spec s;                           // (« Plus tard » chosen first: a restart is never an accident)
                        s.kind = popup::kAsk; s.title = "Red\xc3\xa9marrer maintenant ?";
                        s.text = b == B_LANG ? "La console parlera sa nouvelle langue au prochain d\xc3\xa9marrage."
                               : b == B_REGION ? "La nouvelle r\xc3\xa9gion sera prise en compte au prochain d\xc3\xa9marrage."
                               : it.v ? "L'overlay Brume arrivera au prochain d\xc3\xa9marrage. Secours : L + R + Moins pendant 3 s."
                                      : "L'overlay de Nintendo reviendra au prochain d\xc3\xa9marrage.";
                        s.yes = "Red\xc3\xa9marrer"; s.no = "Plus tard";
                        s.onYes = [](void *, float t) { if (s_restart) s_restart(t); };
                        popup::Show(now, s);
                    }
                    else if (b == B_BRUMELANG) {                 // v65: the texts built once are rebuilt in the new language
                        CodeText(); Refresh();
                        if (s_profilesChanged) s_profilesChanged();
                        Toast(now, "Brume parle fran\xc3\xa7" "ais");   // (« Brume speaks English » when drawn in English)
                    }
                    else if (b == B_CLOCK_SYNC || b == B_SLEEP_H || b == B_SLEEP_TV || b == B_SLEEP_MEDIA || b == B_WAKE_PLUG
                             || b == B_NFC || b == B_PAD_VIB)
                        Toast(now, "Enregistr\xc3\xa9 : effet garanti au prochain d\xc3\xa9marrage");
                }
            } else if (BindOf(it) == B_BRIGHT && !s_brightGlide) lblSaveCurrentSetting();   // v43: kept (v70.5: else at the glide's end)
            CloseOpt(now, !m_opt.slider); return;
        }
        if (!m_open) {                                           // open the category: the ring slides to the left
            m_open = true; m_depth = 0; m_sel[0] = 0; ResetCol(0, true);
            Refresh();                                           // v43: the console's real values
            audio::Play(audio::Tick2400, 0, 1.0f);
            audio::PlaySweep(audio::Whoosh, 0.4f, -0.6f, 0.5f, 0.6f);
            return;
        }
        if (BtColumn(m_cat, m_path, m_depth)) { btaudio::PressA(m_sel[m_depth], now); return; }   // v82: Audio Bluetooth's rows
        Item &it = col[m_sel[m_depth]];
        if (it.k == kFolder) { Enter(now); return; }
        if (it.k == kToggle || it.k == kChoice || it.k == kSlider) { audio::Play(audio::Tick2400, 0, 1.0f); OpenOpt(now); return; }
        if (it.k == kAction) { Act(BindOf(it), it.name, now); return; }   // v43
        audio::Thock(0, 0.8f, 0.05f);
    }

    bool Settings::PressB(float now) {
        if (AppBusy()) return false;                             // v85
        if (datepages::Open()) { datepages::PressB(now); return false; }   // v83
        if (journal::Open()) { journal::PressB(now); return false; }       // v85
        if (m_rec.on) {                                          // v44: cancel choosing the code (v87: B erases the last one first)
            if (m_rec.n > 0 && m_rec.checkAt < 0 && m_rec.badAt < 0 && m_rec.doneAt < 0) { m_rec.n--; m_rec.keyAt[m_rec.n] = -100; audio::Play(audio::Tick1700, 0, 0.7f); haptic::Tick(0.3f); return false; }
            if (m_rec.doneAt < 0) m_rec.on = false;
            audio::Play(audio::Tick1700, 0, 1.0f);
            return false;
        }
        if (m_opt.on) {
            int n; Item *col = Column(m_cat, m_path, m_depth, n);
            if (m_opt.slider) { Item &it = col[m_sel[m_depth]]; it.v = m_opt.old; s_loudOk = true; if (Live(it)) WriteItem(it); }   // v57: the revert is never capped
            CloseOpt(now, false); audio::Play(audio::Tick1700, 0, 1.0f); return false;
        }
        if (m_depth) { Back(now); return false; }
        if (s_xmb) { audio::Thock(0, 0.8f, 0.05f); return false; }   // v50
        if (m_open) { m_open = false; audio::Play(audio::Tick1700, 0, 1.0f); audio::PlaySweep(audio::Whoosh, -0.4f, 0.6f, 0.5f, 0.5f); return false; }
        return true;
    }

    // v87 - the code's panel: a surface in the middle (the page steps back behind a veil), its title in yellow, who it is for,
    // the four dots, the key of the ten buttons (also the touch pad), the hints. Everything glides in and out.
    namespace { constexpr float kCodePadY = 444; }
    void Settings::DrawCode(Ui &ui, float now) {
        const float k = m_rec.k < 0 ? 0.0f : (m_rec.k > 1 ? 1.0f : m_rec.k);
        if (k <= 0.003f) return;
        ui.Veil(-40, -40, 1320, 760, 1.1f * k, 120);
        const float e = EaseGlide(k), x0 = 640 - 270, x1 = 640 + 270, y0 = 150 + 18 * (1 - e), y1 = 566 + 18 * (1 - e);
        ui.SetXform(0.94f + 0.06f * e, 640, 375, 640, 375);
        ui.Surface(x0, y0, x1, y1, k);
        static const char *const kTitle[3] = { "Ton code actuel", "Ton nouveau code", "Encore une fois" };
        const char *title = m_rec.purpose == 1 && m_rec.step == 0 ? "Retirer le code" : kTitle[m_rec.step < 0 || m_rec.step > 2 ? 1 : m_rec.step];
        const float sa = k * EaseGlide((now - m_rec.stepAt) / Pace(0.35f));          // (each step's words come in)
        const float tw = ui.Width(fonts::kSetSel, title, 0.5f);
        ui.Light(640 - tw / 2 - 50, y0 + 22, 640 + tw / 2 + 50, y0 + 72, ps2::A(ps2::kTitleGlow, 0.15f * k), true);
        ui.TextCentered(fonts::kSetSel, title, 640, y0 + 30 + 6 * (1 - sa), 0.5f, ps2::A(ps2::kTitle, fmaxf(sa, 0.3f * k)));
        char who[96]; snprintf(who, sizeof who, lang::T("Pour %s  \xc2\xb7  4 touches de la manette"), Profiles::ActiveName());
        ui.TextCentered(fonts::kHint, who, 640, y0 + 74, 0.5f, ps2::A(ps2::kFaint, 0.95f * k));
        float shake = 0;
        if (m_rec.badAt > 0) { const float t = (now - m_rec.badAt) / 0.5f; if (t < 1) shake = -16.0f * (1 - t) * (1 - t) * sinf(t * 3.14159f * 5.0f); }
        const int state = m_rec.doneAt > 0 ? 1 : (m_rec.badAt > 0 ? 2 : 0);
        pin::DrawDots(ui, 640, y0 + 150, state == 2 ? 4 : m_rec.n, m_rec.keyAt, shake, state, k, now);
        const char *line = state == 2 ? m_rec.bad : (state == 1 ? (m_rec.purpose == 1 ? "Code retir\xc3\xa9" : "C'est not\xc3\xa9") : (m_rec.step == 2 ? "Retape-le pour \xc3\xaatre s\xc3\xbbr" : "Choisis 4 touches parmi les 10"));
        ui.TextCentered(fonts::kHint, line, 640 + shake * 0.4f, y0 + 178, 0.5f, ps2::A(state == 2 ? ps2::kPink : (state == 1 ? ps2::kGreen : ps2::kWords), 0.9f * k));
        pin::DrawPad(ui, 640, kCodePadY + 18 * (1 - e), k, m_rec.lit, m_rec.litAt, now);
        {   // the hints, inside the plate: « B Effacer / Annuler »
            const char *bw = lang::T(m_rec.n > 0 ? "Effacer" : "Annuler");
            const float w = ui.Advance(fonts::kButton, 'B') + 10 + ui.Width(fonts::kHintBig, bw, 0.5f);
            float hx = 640 - w / 2;
            const float hc = y1 - 34;
            hx += ui.Glyph(fonts::kButton, 'B', hx, hc - 10, ps2::A(ps2::kPink, k), 0, 0, 0, 0) + 10;
            for (const char *q = bw; *q;) hx += ui.Glyph(fonts::kHintBig, NextCodepoint(q), hx, hc - 9.5f, ps2::A(ps2::kWords, 0.9f * k), 0, 0, 0, 0) + 0.5f;
        }
        ui.SetZoom(1.0f);
    }

    bool Settings::Tap(float x, float y, float now) {
        if (AppBusy()) return true;                              // v85
        if (m_rec.on) {                                          // v87: the code's panel - the key is a touch pad
            const int s = pin::PadHit(640, kCodePadY, x, y);
            if (s >= 0) CodeKey(s, now);
            else if (x < 640 - 290 || x > 640 + 290 || y < 140 || y > 590) PressB(now);   // outside: like B
            return true;
        }
        if (datepages::Open()) { datepages::Tap(x, y, now); return true; }   // v83
        if (journal::Open()) { journal::Tap(x, y, now); return true; }       // v85
        int n; Item *col = Column(m_cat, m_path, m_depth, n);
        if (m_opt.on) {                                          // the options panel: a row = choose it and confirm
            Item &it = col[m_sel[m_depth]];
            if (!m_opt.slider && x >= 944 && x <= 944 + 290) {
                const int no = it.k == kToggle ? 2 : it.nopt;
                const float top = OptTop(no, SEL_Y);
                const int r = (int)floorf((y - top - 10) / 48 + m_optScroll);   // v70.9: the list may be scrolled
                if (r >= 0 && r < no && r >= (int)floorf(m_optScroll) && r < (int)floorf(m_optScroll) + kOptVis) { m_opt.sel = r; PressA(now); return true; }
            }
            if (m_opt.slider && x >= 944 && x <= 944 + 290) {   // the slider: tap along the bar
                it.v = (int)fmaxf(0.0f, fminf(100.0f, roundf((x - 944 - 26) / (290 - 52) * 20) * 5));
                if (Live(it)) WriteItem(it);
                VolWarn(now);
                audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.7f); return true;
            }
            return PressB(now) ? false : true;
        }
        if (!m_open) {                                           // the ring: the middle opens, the sides turn it
            if (fabsf(x - 640) < 200 && y > 160 && y < 600) PressA(now);
            else Move(x < 640 ? -1 : 1, 0, now);
            return true;
        }
        if (s_xmb && m_depth == 0 && fabsf(y - ROW_Y) < 50) {   // v50: a category of the XMB's row
            const int c = m_cat + (int)lroundf((x - SEL_X) / CAT_GAP);
            if (c != m_cat && c >= 0 && c < kCats) Move(c > m_cat ? 1 : -1, 0, now);
            return true;
        }
        if (!s_xmb && x < 470) return PressB(now) ? false : true;          // the icon on the left: close / back
        for (int i = 0; i < n && i < 16; i++) {                  // a row: the chosen one opens, another one gets chosen
            const Row &r = m_row[m_depth][i];
            if (r.o < 0.1f || fabsf(y - r.y) > (i == m_sel[m_depth] ? 40.0f : 28.0f)) continue;
            if (i == m_sel[m_depth]) PressA(now);
            else { m_sel[m_depth] = i; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.7f); }
            return true;
        }
        return true;
    }
    void Settings::Swipe(int steps, int rows, float now) {
        if (m_opt.on || datepages::Open() || journal::Open() || AppBusy()) return;
        if (steps && (!m_open || m_depth == 0)) { for (int k = 0; k < (steps < 0 ? -steps : steps) && k < 4; k++) Move(steps > 0 ? 1 : -1, 0, now); return; }
        if (rows && m_open) { int n; Column(m_cat, m_path, m_depth, n);
            const int s = m_sel[m_depth] + rows < 0 ? 0 : m_sel[m_depth] + rows >= n ? n - 1 : m_sel[m_depth] + rows;
            if (s != m_sel[m_depth]) { m_sel[m_depth] = s; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.7f); } }
    }

    void Settings::RingDragBegin() { if (m_open || m_opt.on) return; m_ringDrag = true; m_ringFrom = m_cur; m_tweenT0 = -1; m_curV = 0; }
    void Settings::RingDragTo(float now, float steps) {
        if (!m_ringDrag) return;
        m_cur = m_ringFrom + steps;
        const float tgt = roundf(m_cur);
        if (tgt != m_target) {
            const int dc = tgt > m_target ? 1 : -1;
            m_target = tgt;
            m_cat = ((int)lroundf(m_target) % kCats + kCats) % kCats;
            audio::Nav(76, dc * 0.25f); haptic::Tick(0.6f);
            m_nameFadeT0 = now; m_nameSwapAt = now + 0.26f;
            m_depth = 0; m_sel[0] = 0; ResetCol(0, true);
        }
    }
    void Settings::RingDragEnd(float now, float v) {
        if (!m_ringDrag) return;
        m_ringDrag = false;
        // v36: thrown - it keeps the finger's speed and settles on the next category
        const float before = m_target;
        m_target = FlickTarget(m_cur, v, 6.0f); m_curV = v; m_tweenFrom = m_cur; m_tweenT0 = now;
        // v44.1 (Yass: thrown fast, the name no longer matched the icon in front): the category = where it will settle
        const int cat = ((int)lroundf(m_target) % kCats + kCats) % kCats;
        if (cat != m_cat) {
            m_cat = cat;
            m_nameFadeT0 = now; m_nameSwapAt = now + 0.26f;
            m_depth = 0; m_sel[0] = 0; ResetCol(0, true);
        }
        if (m_target != before) { audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.6f); }
    }
    void Settings::ListDragBegin(float x, float y) {   // (v83: x, y for the date and time pages: which wheel)
        if (datepages::Open()) { datepages::DragBegin(x, y); return; }
        if (journal::Open()) { journal::DragBegin(); return; }   // v85
        m_listFrom = m_sel[m_depth];
    }
    void Settings::ListDragTo(float rows) {
        if (datepages::Open()) { datepages::DragTo(rows); return; }   // v83
        if (journal::Open()) { journal::DragTo(rows); return; }       // v85
        if (!m_open || m_opt.on) return;
        int n; Column(m_cat, m_path, m_depth, n);
        int s = m_listFrom + (int)lroundf(rows);
        s = s < 0 ? 0 : s >= n ? n - 1 : s;
        if (s != m_sel[m_depth]) { m_sel[m_depth] = s; audio::Play(audio::Deplacement, 0, 1.0f); haptic::Tick(0.6f); }
    }

    // v50 - the two displays (mockup xSetMode): the XMB of the PS3 (categories in a row, settings in a column) or the 3D
    // carousel; the choice is kept (reglages.ini affichage=). v70.6 (Yass): no Y shortcut any more - Réglages > Brume >
    // Affichage des réglages
    void Settings::SetLayout(bool xmb, float now) {
        if (s_xmb == xmb) return;
        s_xmb = xmb;
        LayoutItem().v = xmb ? 0 : 1;
        Save();
        m_opt.on = false; m_depth = 0; m_sel[0] = 0;
        m_open = xmb;                                    // (the XMB has no closed ring)
        if (xmb) Refresh();                              // v56 (review)
        m_target = m_cur = (float)m_cat; m_tweenT0 = -1; m_curV = 0;
        ResetCol(0, true);
        m_nameFadeT0 = now; m_shownCat = m_cat;
        audio::PlaySweep(audio::Whoosh, -0.4f, 0.4f, 0.5f, 0.4f);
        Toast(now, xmb ? "Affichage des r\xc3\xa9glages : XMB" : "Affichage des r\xc3\xa9glages : carrousel");
    }

    void Settings::Update(float now, float dt) {
        m_t = now;
        {   // v82 - Audio Bluetooth: open while its column is the current one and the page is on screen; its rows follow it
            const bool inBt = s_btHere && BtColumn(m_cat, m_path, m_depth);
            if (!inBt && btaudio::IsOpen()) btaudio::Close(now);
            if (inBt) {
                if (!btaudio::IsOpen()) btaudio::Open(now);
                btaudio::Update(now);
                const int n = btaudio::Rows() < btaudio::kRowsMax ? btaudio::Rows() : btaudio::kRowsMax;
                for (int i = 0; i < n; i++) {
                    const btaudio::Row r = btaudio::RowAt(i);
                    kBt[i] = Item{ r.ico, r.name, r.desc, kAction, 0, nullptr, 0, r.value, nullptr, 0 };
                }
                for (int i = n; i < btaudio::kRowsMax; i++) m_row[m_depth][i].y = -1e4f;   // (a row gone: one there later arrives anew)
                kSon[2].nkids = n;
                m_sel[m_depth] = btaudio::Track(m_sel[m_depth]);
            }
        }
        if (m_opt.on && !m_opt.slider) {                   // v70.9: the long lists of options scroll (glide) to the chosen one
            int n; Item *col = Column(m_cat, m_path, m_depth, n);
            const Item &it = col[m_sel[m_depth]];
            Glide(m_optScroll, m_optScrollV, OptFirst(it.k == kToggle ? 2 : it.nopt, m_opt.sel), dt, 12.0f);
        }
        BrightGlide(dt);                                   // v70.5: the brightness glides
        datepages::Update(now, dt);                        // v83: Date et heure's own pages
        journal::Update(now, dt);                          // v85: Brume's journal
        if (s_appAt > 0) {                                 // v85: JKSV - black and silent: start it; back: the light (as Apps::Update)
            if (s_appBackAt == 0) {
                if (!s_appSettle.on) s_appSettle.Begin(now);
                if (s_appSettle.Step(now, dt)) { s_appBackAt = now; audio::Arrive(1.3f); audio::ExitSound(0.0f); }
            }
            if (!s_appStarted && s_appBackAt < 0 && now - s_appAt > audio::kEntryLen) {
                s_appStarted = true;
                char st[160] = "";
                if (!s_appPath || !Games::LaunchNro(s_appPath, nullptr, st, sizeof st)) {
                    s_appBackAt = now; audio::Arrive(0.8f);
                    popup::Error(now, "JKSV n'a pas pu d\xc3\xa9marrer", st[0] ? st : lang::T("La console a refus\xc3\xa9 de le lancer."), 0);
                }
            }
            if (s_appBackAt > 0 && now - s_appBackAt > Pace(0.8f)) { s_appAt = -100; s_appBackAt = -100; s_appStarted = false; }
            else if (s_appStarted && s_appBackAt < 0 && now - s_appAt > 8.0f) { s_appBackAt = now; audio::Arrive(0.8f); }   // nothing happened
        }
        if (s_xmb && !m_open) { m_open = true; Refresh(); }   // v50: the XMB is always open (v56: with the real values)
        if (m_rec.on && m_rec.doneAt > 0 && now - m_rec.doneAt > 0.9f) { m_rec.on = false; m_rec.doneAt = -1; }   // v44: shown, then closed
        if (m_rec.on && m_rec.checkAt > 0 && now >= m_rec.checkAt) {                                  // v87: the four are in
            m_rec.checkAt = -1; CodeCheck(now);
            if (m_rec.badAt > 0) { audio::Thock(0.0f, 0.55f, 0.14f); audio::SoftLow(audio::HarmonyNote(0, 36, 48), 0.06f, 0.6f); haptic::Thump(0.7f); }
        }
        if (m_rec.on && m_rec.badAt > 0 && now - m_rec.badAt > 0.7f) {                               // wrong: shaken, then again
            m_rec.badAt = -1; m_rec.n = 0;
            if (m_rec.step == 2) { m_rec.step = 1; m_rec.stepAt = now; }
        }
        CodeItem().v = HasCode() ? 1 : 0;                        // v87: the row says the active profile's code (per profile)
        CodeText();
        Glide(m_rec.k, m_rec.v, m_rec.on ? 1.0f : 0.0f, dt, 6.0f);
        if (s_sky) theme::SkyAt(s_hour, s_ambientTarget);                                                           // v53: the sky follows the hour
        for (int c = 0; c < 3; c++) s_ambient[c] += (s_ambientTarget[c] - s_ambient[c]) * (1 - expf(-dt * 1.5f));   // the colour glides
        s_themeLightCur += (s_themeLight - s_themeLightCur) * (1 - expf(-dt * 1.2f));                             // v47: the light too
        // v52: the wave fades in / out. v73: with the PS3 glass, it comes discreetly while the settings page is there (the
        // reference: icons of liquid glass shining ON the XMB wave), even when the option is off; the option keeps it everywhere
        const float waveTo = s_wave ? 1.0f : (s_glassPs3 ? 0.5f * s_glassVis : 0.0f);
        s_waveCur += (waveTo - s_waveCur) * (1 - expf(-dt * 1.0f));
        m_flash *= expf(-dt * 3.0f);                                                                                // v73: A's light fades
        if (m_tweenT0 >= 0) {                                    // v36: turning glides (like the wheel)
            Glide(m_cur, m_curV, m_target, dt, 6.0f);
            if (fabsf(m_cur - m_target) < 0.0015f && fabsf(m_curV) < 0.01f) { m_cur = m_target; m_curV = 0; m_tweenT0 = -1; }
        }
        Glide(m_openAmt, m_openV, m_open ? 1.0f : 0.0f, dt, 6.0f);   // v36: glided
        if (m_nameSwapAt > 0 && now >= m_nameSwapAt) { m_nameSwapAt = -1; m_shownCat = m_cat; }
        // v79 - the layout change dips: everything leaves, the swap (unseen), everything comes back (Dip)
        if (m_dipTo >= 0 && now >= m_dipAt + Pace(0.28f)) { SetLayout(m_dipTo != 0, now); m_dipTo = -1; m_dipInAt = now; }
        // the rows: CSS transition .3s cubic-bezier(.25,.85,.3,1). v79: the spring of Ease.hpp instead of the exponential (no
        // hard start), and a new column's rows come one after the other (the chosen one first, then the next ones)
        constexpr float kRowW = 14.0f, kStagger = 0.05f;
        Glide(m_depthCur, m_depthV, (float)m_depth, dt, 8.0f);
        Glide(m_bump, m_bumpV, 0.0f, dt, 15.0f);
        for (int d = 0; d <= m_depth && d < 3; d++) {
            int n; Item *col = Column(m_cat, m_path, d, n);
            const bool cur = d == m_depth;
            Glide(m_colX[d], m_colXV[d], cur ? 0.0f : (s_xmb ? -COL_GAP * (m_depth - d) : -140.0f), dt, kRowW);
            Glide(m_colA[d], m_colAV[d], cur ? 1.0f : (s_xmb ? 0.55f : 0.0f), dt, cur || s_xmb ? kRowW : 22.0f);   // (a parent of the carousel goes quickly: less overlap)
            Glide(m_colT[d], m_colTV[d], cur ? 1.0f : 0.0f, dt, kRowW);   // (a parent column keeps its icons, its words fade)
            const int s = m_sel[d];
            for (int i = 0; i < n && i < 16; i++) {
                Row &r = m_row[d][i];
                float y, o;
                if (i == s) { y = SEL_Y; o = 1; }
                else if (i > s) { y = SEL_Y + 78 + (i - s - 1) * IT_GAP; o = y > 640 ? 0 : (y > 585 ? 0.4f : 0.85f); }
                else if (s_xmb) { y = ROW_Y - 84 - (s - 1 - i) * IT_GAP; o = i == s - 1 ? 0.55f : 0.0f; }   // v50 (mockup placeCol)
                else { y = SEL_Y - 78 - (s - 1 - i) * IT_GAP; o = i == s - 1 ? 0.5f : (i == s - 2 ? 0.2f : 0.0f); }
                if (r.y < -1000) { r.y = y + 18; r.vy = 0; r.o = 0; r.vo = 0; }   // a new column: from 18 px lower
                if (now < m_colT0[d] + Pace(kStagger) * fminf(fabsf((float)(i - s)), 7.0f)) { r.y = y + 18; r.vy = 0; }   // its turn has not come
                else { Glide(r.y, r.vy, y, dt, kRowW); Glide(r.o, r.vo, o, dt, kRowW); }
                Glide(r.a, r.va, i == s ? 1.0f : 0.0f, dt, kRowW); Glide(r.s, r.vs, i == s ? 1.4f : 1.0f, dt, kRowW);
                // v79: the value it shows - a change crossfades (DrawUi), a slider's number counts to the new one
                const Item &it = col[i];
                if (it.k == kToggle || it.k == kChoice) {
                    if (r.shownV == -9999) r.shownV = it.v;
                    else if (it.v != r.shownV) { r.oldV = r.shownV; r.shownV = it.v; r.vAt = fmaxf(now, m_optHold); }   // (it crossfades once the options leave)
                } else if (it.k == kSlider) {
                    if (r.sv < 0) { r.sv = (float)it.v; r.svv = 0; }
                    Glide(r.sv, r.svv, (float)it.v, dt, 9.0f);
                }
            }
        }
        for (int d = m_depth + 1; d < 3; d++) {             // a column left (B): it glides back to the right and fades (drawn until gone)
            Glide(m_colA[d], m_colAV[d], 0.0f, dt, 22.0f); Glide(m_colX[d], m_colXV[d], 140.0f, dt, kRowW);
            if (m_colA[d] < 0) { m_colA[d] = 0; m_colAV[d] = 0; }
        }
        // v79 - the options: in one by one (the chosen one's neighbours first), out together; the light and the dot glide
        {
            const bool hold = !m_opt.on && now < m_optHold;   // (a value chosen: they stay until the dot is there)
            Glide(m_optK, m_optKV, m_opt.on || hold ? 1.0f : 0.0f, dt, m_opt.on ? 9.0f : 11.0f);
            if (m_optK < 0) { m_optK = 0; m_optKV = 0; }
            if (m_optIt) {
                const Item &it = *(const Item *)m_optIt;
                const int no = it.k == kToggle ? 2 : (it.nopt < kOptMax ? it.nopt : kOptMax);
                const int first = (int)floorf(m_optScroll);
                for (int q = 0; q < no; q++) {
                    const float rel = fminf(fmaxf((float)(q - first), 0.0f), (float)kOptVis);
                    const bool in = (m_opt.on && now >= m_opt.at + Pace(0.06f) * rel) || hold;
                    Glide(m_oe[q], m_oev[q], in ? 1.0f : 0.0f, dt, in ? 11.0f : 13.0f);
                }
                const int cur = it.k == kToggle ? (it.v ? 0 : 1) : it.v;
                Glide(m_optHi, m_optHiV, (float)m_opt.sel, dt, 13.0f);
                Glide(m_optDot, m_optDotV, (float)cur, dt, 14.0f);
                if (it.k == kSlider) Glide(m_sl, m_slV, (float)it.v, dt, 10.0f);
                Glide(m_slNudge, m_slNudgeV, 0.0f, dt, 14.0f);
                if (!m_opt.on && m_optK < 0.002f) m_optIt = nullptr;   // gone
            }
        }
        // the icons: their own angle (the mockup v27 fix: no more spinning top when the ring turns), the bumps
        for (int i = 0; i < kCats; i++) {
            Icon &c = m_ic[i];
            const float focus = fmaxf(0.0f, 1 - fabsf(Wrap(i - m_cur, kCats)));
            const float sway = sinf(now * 0.5f) * 0.42f, TAU = 2 * PI;
            if (focus < 0.001f) c.k = roundf((c.spin - sway) / TAU);
            const float nearA = sway + TAU * c.k;
            c.spin += dt * 0.35f * (1 - focus);
            c.spin += (nearA - c.spin) * focus * (1 - expf(-dt * 4));
            c.kick += -c.wob * 28 * dt; c.kick *= expf(-dt * 3.5f); c.wob += c.kick * dt;
        }
    }

    // the icon i's model matrix (mockup updateSetRing); scale out = its scale
    // the ring (mockup updateSetRing): arrives from the right with the page, slides left when open, the others shrink
    static M4 IconModel(int i, float cur, float openAmt, float pg, float now, float &scale) {
        const float op = openAmt * openAmt * (3 - 2 * openAmt);
        const float d = Wrap(i - cur, Settings::kCats), ang = d * (2 * PI / Settings::kCats), f01 = (cosf(ang) + 1) / 2, focus = fmaxf(0.0f, 1 - fabsf(d));
        const float offX = ((float)Settings::kPage - pg) * 16 - op * 2.85f;
        const float x = sinf(ang) * RING * 1.2f * (1 - 0.3f * op) + offX, y = 0.75f + sinf(now * 0.7f + i * 1.3f) * 0.08f + (1 - f01) * 0.2f, z = RZ + cosf(ang) * RING;
        scale = fmaxf(0.0001f, (0.55f + 0.7f * powf(f01, 6)) * (1 - 0.97f * op * (1 - focus)));
        return Mul(Mul(Translate(x, y, z), RotY(ang * 0.85f)), Scale(scale));
    }

    void Settings::DrawMeshes(Gpu &gpu, const Assets &a, const Target &haze, float now, const Wheel &w) {
        if (!Visible(w) || a.nicons < kCats || s_xmb) return;   // v50: the XMB has no 3D ring
        float eye[3], at[3], X[3], Y[3], Z[3];
        w.CameraAt(now, eye, at);
        Basis(eye, at, X, Y, Z);
        const M4 vp = Mul(Persp(38, (float)gpu.Width() / gpu.Height(), 0.3f, 300), LookAt(eye, at));
        MeshUbo u{};
        for (int k = 0; k < 3; k++) { u.keyDir[k] = X[k]; u.keyCol[k] = Y[k]; u.rimDir[k] = -Z[k]; u.cam[k] = eye[k]; }
        gpu.BeginMesh();
        gpu.SetMeshState(true, false, true);                     // v74: back faces culled (the pebbles are closed, wound clockwise: checked)
        const Target *ts[4] = { &a.env, &a.env, &haze, &a.env };
        gpu.BindTextures(ts, 4, true);
        const float kk = w.WarpK() * Dip(now);                   // v79: the pebbles shrink away (a jump, a layout change) and come back
        for (int i = 0; i < kCats; i++) {
            const Icon &c = m_ic[i];
            const float focus = fmaxf(0.0f, 1 - fabsf(Wrap(i - m_cur, kCats)));
            float sc;
            const M4 base = Mul(IconModel(i, m_cur, m_openAmt, w.PageAnim(), now, sc), Scale(fmaxf(0.0001f, kk)));
            sc *= kk;
            const float sway = sinf(now * 0.5f) * 0.42f, nearA = sway + 2 * PI * c.k;
            const float ry = c.spin + (nearA - c.spin) * focus;
            const float rx = focus * (sinf(now * 0.37f) * 0.06f) + c.wob * 0.25f;
            const M4 model = Mul(base, Mul(Mul(RotX(rx), RotY(ry)), RotZ(c.wob * 0.35f)));
            if (sc < 0.02f) continue;
            memcpy(u.model, model.m, 64);
            memcpy(u.mvp, Mul(vp, model).m, 64);
            // v73 (icon_fsh, the PS3 glass): how chosen (+ A's light on the chosen one), a cube's bump flashes in it, the category open
            u.emi[0] = focus; u.emi[1] = fminf(1.0f, fabsf(c.kick) * 0.6f + m_flash * focus); u.emi[2] = m_openAmt; u.emi[3] = 0;
            gpu.SetMeshUniforms(&u, sizeof u);
            const IconMesh &m = a.icon[i];
            gpu.DrawMesh(m.vtx, m.vtxBytes, m.idx, m.idxCount);
        }
    }

    int Settings::Spheres(float (*out)[4], int *owner, int max, float now, const Wheel &w) const {
        if (!Visible(w) || s_xmb) return 0;
        float eye[3], at[3];
        w.CameraAt(now, eye, at);
        const M4 view = LookAt(eye, at);
        const float kf = (360.0f / tanf(19.0f * PI / 180.0f)) / 900.0f;
        int n = 0;
        const float kk = w.WarpK() * Dip(now);                   // v79
        for (int i = 0; i < kCats && n < max; i++) {
            float sc;
            const M4 m = Mul(view, IconModel(i, m_cur, m_openAmt, w.PageAnim(), now, sc));
            sc *= kk;
            if (sc < 0.05f) continue;
            out[n][0] = m.m[12] * kf; out[n][1] = m.m[13] * kf; out[n][2] = -m.m[14]; out[n][3] = 0.62f * sc * kf;
            owner[n] = i; n++;
        }
        return n;
    }

    float Settings::Dip(float now) const {              // v79: the layout change - out, the swap (Update), then back in
        const float c = 1.0f - s_cover;                  // v84: the controllers' test over the page - it shrinks away, then comes back
        if (m_dipTo >= 0) return (1 - EaseGlide((now - m_dipAt) / Pace(0.28f))) * c;
        return EaseGlide((now - m_dipInAt) / Pace(0.5f)) * c;
    }

    void Settings::DrawUi(Ui &ui, const Assets &a, float now, const Wheel &w) {
        s_glassVis = 0;
        s_btHere = w.Active() && w.Page() == kPage;              // v82: Audio Bluetooth closes when the page leaves
        if (!w.Active()) return;
        // v79 - the page left in one jump (Wheel::Warp: « Gérer les logiciels »): when it comes back, its rows come in one by one
        if (w.WarpK() < 0.02f) m_away = true;
        else if (m_away && w.Page() == kPage) { m_away = false; ResetCol(m_depth, true); }
        // the page's opacity (kPage); v79: x the jump's fade and the layout's dip
        const float pg = w.PageAnim(), d4 = (float)kPage - pg, vis = Clamp01(1 - fabsf(d4) * 1.15f) * w.WarpK() * Dip(now);
        if (vis <= 0.002f) return;
        s_glassVis = vis * vis * (3 - 2 * vis);                 // v73: uGlass.w (the next frame: the lens and the rims grow with the page)
        // v73 - one glass icon: the PS3's liquid glass (mode 29: the sheet's blue holds the shape's distance to its edge, tools/
        // ui_sdf.py), else the v28 glass (mode 22: the option « Verre des icônes : Classique », or an old ui.bin without it).
        // f = how chosen (0..1, it glides), ph = the icon's own beat (its glints), o22 = mode 22's opacity as it was
        // v74: the PS3 glass is its own program (shaders/verre_fsh.glsl): its icons are queued and drawn together (glassFlush,
        // after the row of categories and after the rows), so the program changes twice a frame, not once per icon
        struct GlassQ { const Sprite *s; float x0, y0, x1, y1; Rgba c; float f; };
        GlassQ gq[64]; int ngq = 0;
        ui.SetGlassProgram(&a.glassProg);
        const bool ps3 = s_glassPs3 && a.uiSdf && a.glassProg.ok;
        auto glassIcon = [&](const Sprite *s, float x0, float y0, float x1, float y1, float o, float f, float ph, float o22) {
            if (ps3) { if (ngq < 64) gq[ngq++] = { s, x0, y0, x1, y1, Rgba{ (x1 - x0) / fmaxf(s->w, 1.0f), m_flash * f, ph, o * (0.80f + 0.20f * f) }, f }; }
            else
                ui.Shape(x0, y0, x1, y1, s->uv[0][0], s->uv[0][1], s->uv[0][2], s->uv[0][3], Rgba{1, 1, 1, o22}, 22, s->uv[0][1], s->uv[0][3], f > 0.5f ? 4.0f : 3.0f);
        };
        auto glassFlush = [&]() {
            for (int q = 0; q < ngq; q++) {
                const GlassQ &g = gq[q];
                ui.Shape(g.x0, g.y0, g.x1, g.y1, g.s->uv[0][0], g.s->uv[0][1], g.s->uv[0][2], g.s->uv[0][3], g.c, 29, g.s->uv[0][1], g.s->uv[0][3], g.f);
            }
            ngq = 0;
        };
        const float slide = d4 * 520;                                                       // the page slides in (CSS)
        const float op = m_openAmt * m_openAmt * (3 - 2 * m_openAmt);
        char vb[32];
        // ---- v50: the XMB's row of categories (mockup xLayout): the chosen one big with its name under it, the
        // ones before fade to the left, the ones after dim to the right; inside a folder the row slides away
        if (s_xmb) {
            // v79: going into a folder, the row slides away and dims WITH the depth (it glided), no jump any more
            const float deepK = Clamp01(m_depthCur), deep01 = deepK * deepK * (3 - 2 * deepK);
            for (int i = 0; i < kCats; i++) {
                // v70.6: drawn from the gliding position m_cur (not the chosen index), so that everything moves on
                // left / right: the places, the chosen one's size and light, its name (the old one fades out, the new one in)
                const float kf = i - m_cur, f = fmaxf(0.0f, 1 - fabsf(kf));   // f: how chosen (1 = in its place)
                float o;
                if (kf < -1) o = fmaxf(0.0f, 0.55f + kf * 0.2f);
                else if (kf < 0) o = 0.35f + 0.65f * (kf + 1);
                else if (kf < 1) o = 1 - 0.38f * kf;
                else o = fmaxf(0.2f, 0.7f - kf * 0.08f);
                o += (0.35f * f - o) * deep01;
                o *= vis * (1 - Clamp01(fmaxf(datepages::Shown(), journal::Shown()) * 1.7f));   // (v83: gone while a page of Date et heure is there; v85: the journal)
                if (o <= 0.003f) continue;
                const float cx = SEL_X + kf * CAT_GAP + 26 * fminf(1.0f, fmaxf(0.0f, kf)) - COL_GAP * m_depthCur + slide, cy = ROW_Y;
                char nm[24]; snprintf(nm, sizeof nm, "ico_%s", kCat[i].id);
                if (const Sprite *s = a.Find(nm)) {             // v70: no dark shadow, discreet edges; the chosen one glows
                    const float half = CAT_S * 0.5f * (1 + 0.3f * f), pad = 3 * half * 2 / 56;
                    const float x0 = cx - half - pad, y0 = cy - half - pad, x1 = cx + half + pad, y1 = cy + half + pad;
                    if (f > 0.01f) ui.Light(x0 - 40, y0 - 34, x1 + 40, y1 + 34, ps2::A(ps2::kHalo, 0.30f * o * f), true);
                    glassIcon(s, x0, y0, x1, y1, o, f, fmodf(i * 0.618f, 1.0f), o * (0.7f + 0.25f * f));
                }
                if (deep01 < 0.99f && f > 0.5f) ui.TextCentered(fonts::kGameItem, kCat[i].name, cx, cy + 52, 0.5f, ps2::A(ps2::kTitle, o * (f - 0.5f) * 2 * (1 - deep01)));
            }
            glassFlush();                                        // v74: the categories' glass, together
        }
        // ---- the category's name (#xCarName): bottom centre, follows the icon to the left when open. v70 (the PS2 look): no
        // glass banner any more - the title floats in the fog, in yellow, with a faint warm glow; its count under it, faint
        if (!s_xmb) {
            const float ph = 94;
            const float cx = 640 + (248 - 640) * op + slide, top = 720 - 92 - ph + (-58) * op;
            const float y0 = top;
            const float nf = m_nameSwapAt > 0 ? Clamp01(1 - (now - m_nameFadeT0) / 0.26f) : Clamp01((now - m_nameFadeT0 - 0.26f) / 0.3f);
            const Cat &c = kCat[m_shownCat];
            const float tw = ui.Width(fonts::kPageTitle, c.name, 1.5f);
            ui.Light(cx - tw / 2 - 60, y0 - 6, cx + tw / 2 + 60, y0 + 70, ps2::A(ps2::kTitleGlow, 0.13f * vis * nf), true);
            ui.TextCentered(fonts::kPageTitle, c.name, cx, y0 + 12, 1.5f, ps2::A(ps2::kTitle, vis * nf));
            snprintf(vb, sizeof vb, lang::T("%d r\xc3\xa9glages"), c.n);
            char up[48]; Upper(vb, up, sizeof up);
            ui.TextCentered(fonts::kGameInfo, up, cx, y0 + 12 + 44 + 2, 4, ps2::A(ps2::kFaint, 0.9f * vis * nf));
        }
        // ---- the list (open): rows (icon, title, description, value), the options. v70 (the PS2 look): the frosted dark band
        // becomes a soft band of light behind the chosen line, with a halo behind its words (Ui::Chosen); the words float.
        // v79: every level the same - a new column's rows come one by one, a column left (B) glides away to the right while it
        // fades (it used to vanish), a parent column's words fade (XMB), the value crossfades when it changes, a slider's number
        // counts, the rows step back while the options are open, a list's end gives a little, a refused action shakes its head
        const float sub = fmaxf(datepages::Shown(), journal::Shown());   // v83: a page of Date et heure over the list: it steps back to the left (v85: or the journal)
        const float la = vis * op * (1 - Clamp01(sub * 1.7f));   // (gone before the page is half there: no clutter)
        if (la > 0.002f) {
            const float bx = s_xmb ? SEL_X - 60 : 470;   // v50
            const float optK = Clamp01(m_optK);
            for (int d = 0; d <= m_depth + 1 && d < 3; d++) {
                const float ca = m_colA[d] * la;
                if (ca <= 0.002f) continue;
                if (d > m_depth) {                               // v79: the column being left - still the folder it showed?
                    int pn; const Item *pc = Column(m_cat, m_path, d - 1, pn);
                    if (m_path[d - 1] < 0 || m_path[d - 1] >= pn || pc[m_path[d - 1]].k != kFolder) continue;
                }
                int n; Item *col = Column(m_cat, m_path, d, n);
                const float x = (s_xmb ? SEL_X : CAR_X) + m_colX[d] + slide - 90.0f * sub;   // v50 (v83: steps back)
                const float tk = Clamp01(m_colT[d]);             // its words (a parent column keeps only its icons)
                const bool live = d == m_depth;
                const bool btCol = BtColumn(m_cat, m_path, d);   // v82: its lights (the search's waves, a device's)
                const float by = live ? m_bump * (1 - optK) : 0.0f;
                for (int i = 0; i < n && i < 16; i++) {
                    const Row &r = m_row[d][i];
                    const bool chosen = live && i == m_sel[d];
                    const float o = r.o * ca * (live && !chosen ? 1 - 0.45f * optK : 1.0f);
                    if (o <= 0.003f || r.y < -1000) continue;
                    const Item &it = col[i];
                    const float ry = r.y + by;
                    float shx = 0;                               // the « no »: a short shake, softly started, quickly gone
                    if (chosen) {
                        const float st = now - m_refuseAt;
                        if (st >= 0 && st < 0.6f) shx = 7.0f * sinf(st * 2 * PI * 6.0f) * expf(-st * 7.0f) * (1 - expf(-st * 40.0f));
                    }
                    const float tl = x + (IT_S / 2 + 16) + (IT_S * 0.7f - IT_S / 2) * r.a + shx;
                    // v70: the chosen line's light (a soft band, a halo behind its words) - drawn first, under everything
                    if (r.a > 0.01f && tk > 0.01f) {
                        float tw = 0;
                        for (const char *q = lang::T(it.name); *q;) tw += ui.Advance(fonts::kSetSel, NextCodepoint(q));
                        ui.Chosen(bx + slide, 1280 + slide, ry - 6, tl, tw, r.a * o * tk);
                    }
                    if (btCol) btaudio::DrawRow(ui, i, x + shx * 0.5f, ry, o, now);   // v82 (under its glass icon)
                    // the glass icon (v70: no dark shadow, its edges discreet, the others quieter)
                    char nm[24]; snprintf(nm, sizeof nm, "ico_%s", it.ico);
                    if (const Sprite *s = a.Find(nm)) {
                        const float half = (IT_S / 2) * r.s, pad = 3 * r.s * IT_S / 56, ix = x + shx * 0.5f;
                        const float x0 = ix - half - pad, y0 = ry - half - pad, x1 = ix + half + pad, y1 = ry + half + pad;
                        glassIcon(s, x0, y0, x1, y1, o, r.a, fmodf(i * 0.618f + d * 0.31f, 1.0f), o * (0.62f + 0.33f * r.a));
                    }
                    const float ot = o * tk;
                    if (ot <= 0.003f) continue;                  // v50: a parent column keeps only its icon (XMB) - v79: its words fade
                    // title: 21 px light blue -> 27 px near-white (crossfade of the two sizes)
                    if (r.a < 0.99f) {
                        float px = tl;
                        for (const char *q = lang::T(it.name); *q;) px += ui.Glyph(fonts::kGameItem, NextCodepoint(q), px, ry - 14, ps2::A(ps2::kChoice, 0.82f * ot * (1 - r.a)), 0, 0, 0, 0);
                    }
                    if (r.a > 0.01f) {
                        float px = tl;
                        for (const char *q = lang::T(it.name); *q;) px += ui.Glyph(fonts::kSetSel, NextCodepoint(q), px, ry - 25, ps2::A(ps2::kChosen, 0.96f * ot * r.a), 0, 0, 0, 0);
                        if (it.desc) {
                            float dx = tl;
                            for (const char *q = lang::T(it.desc); *q;) dx += ui.Glyph(fonts::kSetDesc, NextCodepoint(q), dx, ry + 11, ps2::A(ps2::kFaint, ot * r.a), 0, 0, 0, 0) + 0.3f;
                        }
                    }
                    // the value, right-aligned, written as text (hidden while the options are open: v79 it fades); a slider
                    // that is chosen shows its arrows « ◂ 62 % ▸ ». v79: its colour and place follow how chosen the row is (no
                    // jump half way), a new value crossfades with the old one (the old one rises away, the new one comes up)
                    const char *val = Value(it, vb, sizeof vb);
                    if (it.k == kSlider) { snprintf(vb, sizeof vb, "%d %%", (int)lroundf(r.sv < 0 ? (float)it.v : r.sv)); val = vb; }
                    const float va = ot * (1 - Clamp01(optK * 1.8f));   // (gone before the options are half in: no overlap)
                    if (val[0] && va > 0.003f) {
                        const float ac0 = Clamp01((r.a - 0.25f) * 2), ac = ac0 * ac0 * (3 - 2 * ac0);
                        const float vy = ry - 13 - 5 * ac, vw = ui.Width(fonts::kSetOpt, val, 0);
                        const Rgba vc = ps2::Mix(ps2::A(ps2::kFaint, 0.85f), ps2::A(ps2::kChosen, 0.94f), ac);
                        const float ce = (it.k == kToggle || it.k == kChoice) ? EaseGlide((now - r.vAt) / Pace(0.42f)) : 1.0f;
                        if (ce < 1) {
                            Item old = it; old.v = r.oldV;
                            char ob[32]; const char *ov = Value(old, ob, sizeof ob);
                            const float ow = ui.Width(fonts::kSetOpt, ov, 0);
                            ui.TextCentered(fonts::kSetOpt, ov, VAL_R + slide - ow / 2, vy - 9 * ce, 0, ps2::A(vc, va * (1 - ce)));
                        }
                        ui.TextCentered(fonts::kSetOpt, val, VAL_R + slide - vw / 2, vy + 9 * (1 - ce), 0, ps2::A(vc, va * ce));
                        if (it.k == kSlider && ac > 0.01f) {
                            const float ay = vy + 11;
                            ui.Arrow(VAL_R + slide - vw - 16, ay, 5, 0, ps2::A(ps2::kChoice, va * ac));
                            ui.Arrow(VAL_R + slide + 16, ay, 5, 1, ps2::A(ps2::kChoice, va * ac));
                        }
                    }
                }
            }
            glassFlush();                                        // v74: the rows' glass, together (over their bands, before the options)
            // the options panel (#xOpt, carousel display: left 944, width 290). v79 (Yass: « les mêmes animations que le niveau
            // juste avant »): the options come in one by one from the right, the light glides from one to the next (the words
            // brighten as it passes), the current value's dot glides to the new one when chosen, they leave softly; the slider's
            // number counts, its line of light glides, its arrows give when pressed
            if (m_optIt && optK > 0.002f) {
                const Item &it = *(const Item *)m_optIt;
                const float pa = la * optK, ox = 944 + slide, pw = 290;
                // v70 (the PS2 look): no glass panel - the options float on the right of the list, in the fog
                if (it.k == kSlider) {
                    const float sx = ox + 30 * (1 - optK);
                    const float ph = 112, top = fmaxf(96.0f, fminf(560.0f, SEL_Y - 56));
                    const float shown = fmaxf(0.0f, fminf(100.0f, m_sl));
                    snprintf(vb, sizeof vb, "%d %%", (int)lroundf(shown));
                    const float vw = ui.Width(fonts::kPageTitle, vb, 0), vy = top + 16 + 20, nx = m_slNudge * 0.22f;
                    ui.Light(sx + pw / 2 - vw / 2 - 50 + nx, vy - 34, sx + pw / 2 + vw / 2 + 50 + nx, vy + 34, ps2::A(ps2::kHalo, 0.36f * pa), true);
                    ui.TextCentered(fonts::kPageTitle, vb, sx + pw / 2 + nx, top + 16, 0, ps2::A(ps2::kChosen, 0.96f * pa));
                    ui.Arrow(sx + pw / 2 - vw / 2 - 22 + fminf(m_slNudge, 0.0f), vy, 6, 0, ps2::A(ps2::kChoice, pa));
                    ui.Arrow(sx + pw / 2 + vw / 2 + 22 + fmaxf(m_slNudge, 0.0f), vy, 6, 1, ps2::A(ps2::kChoice, pa));
                    // a fine line of light: how much (no track box, no knob: the light itself) - v79: it glides
                    const float lx = sx + 26, lw = pw - 52, ly = top + ph - 26;
                    ui.Shape(lx, ly, lx + lw, ly + 1.5f, 0, 0, lw, 1.5f, ps2::A(ps2::kChoice, 0.18f * pa), 24, lw, 1.5f, 0.75f);
                    const float fw = lw * shown / 100.0f;
                    if (fw > 1) ui.Shape(lx, ly, lx + fw, ly + 1.5f, 0, 0, fw, 1.5f, ps2::A(ps2::kChoice, 0.85f * pa), 24, fw, 1.5f, 0.75f);
                    ui.Light(lx + fw - 22, ly - 12, lx + fw + 22, ly + 13, ps2::A(ps2::kHalo, (0.7f + 0.25f * Clamp01(fabsf(m_slV) * 0.02f)) * pa), true);
                } else {
                    static const char *const kOnOff[2] = { "Activé", "Désactivé" };
                    const int no = it.k == kToggle ? 2 : (it.nopt < kOptMax ? it.nopt : kOptMax);
                    const int vis = no < kOptVis ? no : kOptVis;
                    const float top = OptTop(no, SEL_Y) + m_bump * optK, sc = m_optScroll;
                    auto inWin = [&](float k) { const float rel = k - sc; return Clamp01(rel + 1.0f) * Clamp01((float)vis - rel); };
                    auto rowE = [&](float k) { const int q = (int)lroundf(k); return q >= 0 && q < no ? Clamp01(m_oe[q]) : 0.0f; };
                    if (no > kOptVis) {                          // v70.9: more above / below - small arrows (they fade at the ends)
                        const float up = Clamp01(sc), dn = Clamp01((float)(no - vis) - sc);
                        ui.Arrow(ox + pw / 2, top - 4, 5, 2, ps2::A(ps2::kChoice, 0.7f * pa * up));
                        ui.Arrow(ox + pw / 2, top + 10 + vis * 48 + 8, 5, 3, ps2::A(ps2::kChoice, 0.7f * pa * dn));
                    }
                    auto optText = [&](int k) -> const char * { return k < 0 || k >= no ? nullptr : (it.k == kToggle ? kOnOff[k] : it.opt[k]); };
                    auto optW = [&](int k) {
                        float tw = 0;
                        if (const char *t = optText(k)) for (const char *q = lang::T(t); *q;) tw += ui.Advance(fonts::kSetOpt, NextCodepoint(q));
                        return tw;
                    };
                    {   // the light: ONE band and ONE halo that glide from option to option (the halo takes the words' width on its way)
                        const float hy = top + 10 + (m_optHi - sc) * 48 + 24, ha = pa * inWin(m_optHi) * rowE(m_optHi) * (1 + 0.5f * m_flash);
                        const int k0 = (int)floorf(m_optHi);
                        const float fr = m_optHi - k0, hw = optW(k0) + (optW(k0 + 1) - optW(k0)) * fr;
                        const float hx = ox + 26 * (1 - rowE(m_optHi)) + 6 + 24 + 8 + 13;
                        ui.Light(ox - 20, hy - 26, 1280 + slide, hy + 26, ps2::A(ps2::kHalo, 0.24f * ha));
                        ui.Light(hx - 30, hy - 22, hx + hw + 30, hy + 22, ps2::A(ps2::kHalo, 0.36f * ha), true);
                    }
                    for (int k = 0; k < no; k++) {
                        const float ea = inWin((float)k), e = Clamp01(m_oe[k]);
                        if (ea * e <= 0.003f) continue;
                        const float oa = la * ea * e;            // (the rows leaving the window fade out; each one enters on its turn)
                        const float c = Clamp01(1 - fabsf(k - m_optHi));   // how much of the light is on it
                        const char *t = optText(k);
                        if (!t) continue;
                        const float rx = ox + 26 * (1 - e) + 6 * c;   // from the right as it enters; the light pushes it a little
                        const float dy = top + 10 + (k - sc) * 48 + 24, tx0 = rx + 24 + 8 + 13;
                        float tx = tx0;
                        for (const char *q = lang::T(t); *q;) tx += ui.Glyph(fonts::kSetOpt, NextCodepoint(q), tx, dy - 10, ps2::Line(c, oa * (0.6f + 0.4f * optK)), 0, 0, 0, 0);
                    }
                    {                                            // the current one: a small light, not a radio button - it glides
                        const float de = rowE(m_optDot);         // to the new value when one is chosen (and swells a little on its way)
                        const float da = la * inWin(m_optDot) * de * (0.4f + 0.6f * optK);
                        const float dx = ox + 24 + 4 + 26 * (1 - de), dy = top + 10 + (m_optDot - sc) * 48 + 24;
                        const float g = 1 + 0.35f * Clamp01(fabsf(m_optDotV) * 0.25f);
                        ui.Shape(dx - 10 * g, dy - 10 * g, dx + 10 * g, dy + 10 * g, -1, -1, 1, 1, Accent(0.9f * da, 0.5f), 21);
                        ui.Shape(dx - 2.5f, dy - 2.5f, dx + 2.5f, dy + 2.5f, 0, 0, 5, 5, ps2::A(ps2::kChosen, da), 24, 5, 5, 2.5f);
                    }
                }
            }
        }
        datepages::Draw(ui, now, vis * op, slide, s_xmb);   // v83: Date et heure's own pages (over the list, which stepped back)
        journal::Draw(ui, now, vis * op, slide, s_xmb);     // v85: Brume's journal (the same place)
        // ---- the hints (mockup setPageHints, carousel display): left = actions, right = navigation
        // v79: when they change (a folder, the options...), the words that change crossfade - the ones that stay, stay; and they
        // come with the page (a short fade as it settles, no switch at half way)
        {
            Hints H;
            {
                int n; Item *col = Column(m_cat, m_path, m_depth, n);
                const Item &it = col[m_sel[m_depth] < n ? m_sel[m_depth] : 0];
                if (datepages::Open()) datepages::Hints(H.aL, H.bL, H.chip, H.lbl, H.nr);   // v83
                else if (journal::Open()) journal::Hints(H.aL, H.bL, H.chip, H.lbl, H.nr);   // v85
                else if (m_opt.on) { H.aL = "Valider"; H.bL = "Annuler"; }
                else if (!m_open) { H.aL = "Ouvrir"; H.bL = nullptr; }   // v40: B no longer goes back to the games
                else { H.aL = it.k == kFolder ? "Ouvrir" : "Choisir"; H.bL = m_depth ? "Retour" : (s_xmb ? nullptr : "Fermer"); }
                if (datepages::Open() || journal::Open()) {}
                else if (m_opt.on) { H.chip[H.nr] = m_opt.slider ? "chipLeRi" : "chipUpDo"; H.lbl[H.nr++] = m_opt.slider ? "Régler" : "Choisir"; }
                else {
                    H.chip[H.nr] = "chipLR"; H.lbl[H.nr++] = "Pages";
                    if (!m_open) { H.chip[H.nr] = "chipLeRi"; H.lbl[H.nr++] = "Tourner"; }   // (v70.6: no « Y Affichage »)
                    else { if (!m_depth) { H.chip[H.nr] = "chipLeRi"; H.lbl[H.nr++] = "Catégories"; } H.chip[H.nr] = "chipUpDo"; H.lbl[H.nr++] = "Réglages"; }
                }
                if (!m_opt.on && m_open && BtColumn(m_cat, m_path, m_depth)) {   // v82: Audio Bluetooth's own words
                    H.aL = btaudio::HintA(m_sel[m_depth]);
                    H.nr = 0; H.chip[H.nr] = "chipLR"; H.lbl[H.nr++] = "Pages";
                    if (btaudio::CanForget(m_sel[m_depth])) { H.chip[H.nr] = "chipX"; H.lbl[H.nr++] = "Oublier"; }
                    H.chip[H.nr] = "chipUpDo"; H.lbl[H.nr++] = "Appareils";
                }
            }
            auto same = [](const char *x, const char *y) { return x == y || (x && y && !strcmp(x, y)); };
            auto sameR = [&](const Hints &p, const Hints &q) {
                if (p.nr != q.nr) return false;
                for (int k = 0; k < p.nr; k++) if (!same(p.chip[k], q.chip[k]) || !same(p.lbl[k], q.lbl[k])) return false;
                return true;
            };
            if (!same(H.aL, m_hint[0].aL) || !same(H.bL, m_hint[0].bL) || !sameR(H, m_hint[0])) { m_hint[1] = m_hint[0]; m_hint[0] = H; m_hintAt = now; }
            const float pe = Clamp01((pg - 4.5f) / 0.3f), ha = vis * pe * pe * (3 - 2 * pe);
            if (ha > 0.002f) {
                const Hints &N = m_hint[0], &O = m_hint[1];
                // one after the other (the old words go, then the new ones come): never two lines of hints on top of each other
                const float t = (now - m_hintAt) / Pace(0.36f), e = t >= 1 ? 1.0f : (t < 0.5f ? 0.0f : EaseGlide(t * 2 - 1));
                const float eo = t >= 1 ? 0.0f : 1 - EaseGlide(fminf(1.0f, t * 2));   // (the old ones' alpha)
                auto left = [&](const Hints &q, float al, bool aToo, bool bToo) {
                    if (!q.aL || al <= 0.002f) return;
                    HintRow h{ ui, a, al };
                    const float x = aToo ? h.Draw(46, "chipA", q.aL) : h.Skip(46, "chipA", q.aL);
                    if (q.bL && bToo) h.Draw(x, "chipB", q.bL);
                };
                auto right = [&](const Hints &q, float al) {
                    if (al <= 0.002f || !q.nr) return;
                    HintRow h{ ui, a, al };
                    float tw = 0;
                    for (int k = 0; k < q.nr; k++) tw += h.Width(q.chip[k], q.lbl[k]) + (k ? 30 : 0);
                    float rx = 1280 - 46 - tw;
                    for (int k = 0; k < q.nr; k++) rx = h.Draw(rx, q.chip[k], q.lbl[k]);
                };
                if (e >= 1 || (same(N.aL, O.aL) && same(N.bL, O.bL))) left(N, ha, true, true);
                else if (same(N.aL, O.aL)) { left(N, ha, true, false); left(N, ha * e, false, true); left(O, ha * eo, false, true); }
                else { left(N, ha * e, true, true); left(O, ha * eo, true, true); }
                if (e >= 1 || sameR(N, O)) right(N, ha);
                else { right(N, ha * e); right(O, ha * eo); }
            }
        }
        if (s_appAt > 0) {                                       // v85: JKSV starting - the screen goes black (and comes back, as Apps)
            const float blk = s_appBackAt > 0 ? 1 - Clamp01((now - s_appBackAt) / Pace(0.7f))
                            : (s_appBackAt == 0 ? 1.0f : Clamp01((now - s_appAt - Pace(0.3f)) / Pace(0.5f)));
            if (blk > 0.003f) ui.Shape(0, 0, 1280, 720, 0, 0, 1280, 720, Rgba{ 0, 0, 0, blk }, 24, 1280, 720, 0);
        }
    }

} // namespace brume
