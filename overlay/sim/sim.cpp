// Runs Brume Overlay's own drawing code on the PC: the screens as the display would show them (over a stand-in game,
// raw 1280x720 RGBA for render.py) and the cost of every frame (drawing + the copy into the display's layout).
// The Switch's CPU core (Cortex-A57 at 1 GHz) is roughly 8-10 times slower than a PC core on this kind of code.
#define PREVIEW
#define BRUME_SIM                                             // (a made-up sys-clk: the « Performance » row shows)
#include "../source/main.cpp"
#include <cstdio>
#include <vector>
#include <algorithm>

static u32 g_shot[1280 * 720];
static std::vector<u32> g_gamePic;
static void Step() {                                         // one frame at 60 images a second (time, the atmosphere's clock)
    g_t += 1.0f / 60; g_dt = 1.0f / 60;
    if (g_shown != kIdle) {
        const bool idle = g_t - g_lastInput >= kFreeze;
        Approach(g_speed, idle ? 0.0f : 1.0f, g_dt, 1.4f);
        if (idle && g_speed < 0.002f) g_speed = 0;
        g_at += g_dt * g_speed;
    }
}
struct Phase { const char *name; std::vector<double> ms; std::vector<int> px; };
static double Frame(Phase *ph) {
    Step();
    const int a0 = g_stArea;
    const auto t0 = std::chrono::steady_clock::now();
    const bool drew = Render();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (ph && drew) { ph->ms.push_back(ms); ph->px.push_back(g_stArea - a0); }
    return ms;
}
static void Save(const char *name) {
    PreviewCompose(g_shot, 1280);
    char path[64]; snprintf(path, sizeof path, "%s.rgba", name);
    FILE *f = fopen(path, "wb"); fwrite(g_shot, 4, 1280 * 720, f); fclose(f);
}
static void Report(const Phase &p) {
    if (p.ms.empty()) { printf("%-38s : rien dessine\n", p.name); return; }
    std::vector<double> s = p.ms; std::sort(s.begin(), s.end());
    double sum = 0; for (double v : s) sum += v;
    long px = 0; for (int v : p.px) px += v;
    printf("%-38s : %3zu images, %.3f ms en moyenne, max %.3f, mediane %.3f (PC) ; %6ld px copies par image\n",
           p.name, s.size(), sum / s.size(), s.back(), s[s.size() / 2], px / (long)p.px.size());
}

int main() {
    // the stand-in game (render.py game -> game.rgba)
    if (FILE *f = fopen("game.rgba", "rb")) { g_gamePic.resize(1280 * 720); if (fread(g_gamePic.data(), 4, 1280 * 720, f) == 1280 * 720) g_pvBack = g_gamePic.data(); fclose(f); }
    GlyphsInit();
    FogInit();
    g_fbFOk = true;                                           // (the fog layer's path, copied into a stand-in buffer)
    g_lbl = g_nifm = g_setsys = g_psm = g_time = true;
    {
        auto b0 = std::chrono::steady_clock::now();
        BuildPlates();
        printf("plaques (une fois, au demarrage) : %.1f ms sur le PC\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count());
    }
    g_t = 100;
    ClearRect(kAll);

    // 1. the power menu: its opening, then at rest (only the ring of lights moves), a move down
    Phase pOpen{ "alimentation : ouverture (0.75 s)" }, pRest{ "alimentation : au repos (l'anneau)" }, pMove{ "alimentation : deplacement du choix" };
    Open(kPower);
    for (int k = 0; k < 45; k++) { Frame(&pOpen); if (k == 5) Save("power_anim"); }
    for (int k = 0; k < 120; k++) Frame(&pRest);
    g_sel = 1; g_lastInput = g_t;
    for (int k = 0; k < 40; k++) Frame(&pMove);
    Save("power");
    Close(true);
    for (int k = 0; k < 30; k++) Frame(nullptr);

    // 2. the HOME panel (v98, the « centre de contrôle »): it slides in, at rest, then real presses: the volume up, the
    //    brightness down, the performance, the game's notes opened and read on
    if (FILE *f = fopen("notes_demo.txt", "wb")) {             // (the demo notes: what a player might keep for a game)
        fputs("Boss du chapitre 3 : rester derri\xc3\xa8re les piliers, il charge toujours deux fois avant de s'arr\xc3\xaanter.\n"
              "Code du coffre de la tour : 4-1-7-3\n\n"
              "Astuce : maintenir ZR pendant le saut = double saut plus long (pas expliqu\xc3\xa9 dans le jeu).\n"
              "Cheat menu : \xe2\x86\x91 \xe2\x86\x91 \xe2\x86\x93 \xe2\x86\x93 \xe2\x86\x90 \xe2\x86\x92 \xe2\x86\x90 \xe2\x86\x92 B A\n"
              "\t# Collectibles : 12/40 trouv\xc3\xa9s (zone 2 [nord] et zone 5 <grotte>)\n"
              "Sauvegarde avant la porte rouge, sinon tout recommencer.\n"
              "Un_mot_tres_long_sans_aucun_espace_pour_verifier_que_la_coupure_entre_les_lettres_marche_bien_jusqu_au_bout\n"
              "Ligne 9\nLigne 10\nLigne 11\nLigne 12\nLigne 13\nLigne 14 (la derni\xc3\xa8re)\n", f);
        fclose(f);
    }
    auto Press = [](u64 b) { g_pvDown = b; Input(); g_pvDown = 0; };
    Phase qOpen{ "volet HOME : ouverture (0.75 s)" }, qRest{ "volet HOME : au repos (brume)" }, qMove{ "volet HOME : deplacement + reglages" };
    Open(kQuick);
    for (int k = 0; k < 45; k++) { Frame(&qOpen); if (k == 7) Save("quick_anim"); }
    for (int k = 0; k < 120; k++) Frame(&qRest);
    Press(HidNpadButton_AnyRight); for (int k = 0; k < 8; k++) Frame(&qMove);       // the volume: 9 -> 11
    Press(HidNpadButton_AnyRight); for (int k = 0; k < 40; k++) Frame(&qMove);
    Save("quick");
    Press(HidNpadButton_AnyDown); for (int k = 0; k < 10; k++) Frame(&qMove);        // the brightness: down twice
    Press(HidNpadButton_AnyLeft); for (int k = 0; k < 8; k++) Frame(&qMove);
    Press(HidNpadButton_AnyLeft); for (int k = 0; k < 40; k++) Frame(&qMove);
    Save("quick_bright");
    Press(HidNpadButton_AnyDown); for (int k = 0; k < 10; k++) Frame(&qMove);        // the performance: Boost
    Press(HidNpadButton_AnyRight); Press(HidNpadButton_AnyRight); for (int k = 0; k < 70; k++) Frame(&qMove);
    Save("quick_perf");
    Press(HidNpadButton_AnyDown); Press(HidNpadButton_AnyDown); for (int k = 0; k < 30; k++) Frame(&qMove);   // the notes, opened, read on
    Press(HidNpadButton_A); for (int k = 0; k < 20; k++) Frame(&qMove);
    Press(HidNpadButton_AnyDown); Press(HidNpadButton_AnyDown); for (int k = 0; k < 40; k++) Frame(&qMove);
    Save("quick_notes");
    Press(HidNpadButton_B); for (int k = 0; k < 40; k++) Frame(&qMove);              // back to the rows
    OnVolume(9);
    for (int k = 0; k < 20; k++) Frame(nullptr);
    Save("quick_volume");
    // frozen: nobody touches for 20 s -> nothing drawn
    Phase qFrozen{ "volet HOME : 20 s sans toucher (fige)" };
    for (int k = 0; k < 60 * 23; k++) Frame(k > 60 * 22 ? &qFrozen : nullptr);
    Close(true);
    for (int k = 0; k < 30; k++) Frame(nullptr);

    // 3. over the game: the volume and a notification
    Phase vol{ "volume + notification (par-dessus le jeu)" };
    OnVolume(9);
    for (int k = 0; k < 10; k++) Frame(&vol);
    ShowToast("Capture d'\xc3\xa9" "cran enregistr\xc3\xa9" "e");
    for (int k = 0; k < 30; k++) Frame(&vol);
    Save("volume");
    for (int k = 0; k < 300; k++) Frame(&vol);

    // 4. the states: HOME panel, then Power held while it is open, closed, then the volume - nothing may be left behind
    {
        auto Outside = [](Rect keep) { int n = 0; for (int y = 0; y < UH; y++) for (int x = 0; x < UW; x++) if (g_canvas[y * UW + x] && !(x >= keep.x0 && x < keep.x1 && y >= keep.y0 && y < keep.y1)) n++; return n; };
        for (int k = 0; k < 400; k++) Frame(nullptr);         // (the volume and the notification gone)
        Open(kQuick);
        for (int k = 0; k < 40; k++) Frame(nullptr);
        Open(kPower);
        for (int k = 0; k < 60; k++) Frame(nullptr);
        const int a = Outside(kPowerArea), pos1 = g_uPos;
        Close(true);
        for (int k = 0; k < 30; k++) Frame(nullptr);
        const int b = Outside(R(0, 0, 0, 0));
        OnVolume(5);
        for (int k = 0; k < 20; k++) Frame(nullptr);
        const int c = Outside(VolBox()), pos0 = g_uPos;
        printf("etats : volet -> alimentation : %d px hors de la plaque (0 attendu), calque au centre %s ; ferme : %d px (0) ; volume : %d px hors de sa place (0), calque en haut a gauche %s\n",
               a, pos1 == 1 ? "oui" : "NON", b, c, pos0 == 0 ? "oui" : "NON");
        printf("tampons de l'ecran (3, ordre au hasard) : %d image(s) fausse(s) sur %u (0 attendu)\n", g_simSlotErrors, g_frameNo);
    }

    // the copy into the display's layout, alone: the whole UI picture, the fog picture
    {
        static u8 fake[UW * 4 * 768];
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 200; k++) SwizzleRect(fake, UW * 4, g_canvas, UW, kAll);
        printf("copie de toute l'image UI (560x720) : %.3f ms (PC)\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 200);
    }
    {   // the parts of a whole frame (the power menu open)
        Open(kPower); g_t += 2; g_at += 2;
        auto T = [](const char *what, auto fn) {
            for (int k = 0; k < 20; k++) fn();
            auto s = std::chrono::steady_clock::now();
            for (int k = 0; k < 200; k++) fn();
            printf("  dont %-36s : %.3f ms (PC)\n", what, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count() / 200);
        };
        T("effacer toute l'image", [] { ClearRect(kAll); });
        T("plaque copiee (k = 1)", [] { BigPlate(g_platePower, 0, 0, 1.0f, kPX0, kPY0, kPX1, kPY1, kPR); });
        T("plaque copiee et fondue (k = 0.5)", [] { BigPlate(g_platePower, 0, 0, 0.5f, kPX0, kPY0, kPX1, kPY1, kPR); });
        T("plaque simple (sans image)", [] { PlainPlate(kPX0, kPY0, kPX1, kPY1, kPR, 1.0f); });
        T("alimentation entiere (p = 1)", [] { DrawPower(1.0f); });
        T("anneau de lumieres", [] { Orbs(280, kRingY, kRingR, 1.0f, g_at); });
        T("halo du choix", [] { Chosen(kPX0 + 16, kPX1 - 16, 370, 220, 120, 1.0f); });
        T("textes (titre + 4 choix + aides)", [] { TextC(kTitle, PowerTitle(), 280, kPTitleY, kTitleC, 1, 1); for (int i = 0; i < 4; i++) TextC(kItem, PowerItem(i), 280, kPItemY + i * kPItemGap, kChoice, 1); Hint(200, kPHintY, 'A', "Valider", 1); });
        T("brume : un douzieme des lignes", [] { FogStep(); });
        T("brume : image du calque 320x180", [] { FogCompose(1.0f); });
        T("volet HOME entier (p = 1)", [] { DrawQuick(1.0f); });
        Close(true);
    }
    printf("\n");
    for (const Phase *p : { &pOpen, &pRest, &pMove, &qOpen, &qRest, &qMove, &qFrozen, &vol }) Report(*p);
    printf("\nmemoire : image UI %u Ko (dans le programme), plaques %u Ko, brume %u Ko, glyphes %u Ko (dans le programme)\n",
           (unsigned)(sizeof g_canvas / 1024), (unsigned)((2 * UW * UH + kSBW * kSBH) * 4 / 1024),
           (unsigned)((BW * BH * 4 + FW * FH * 4 + 3 * BW * BH + FW * FH + 256 * 256 * 2) / 1024), (unsigned)(sizeof kBAtlas / 1024));
    return 0;
}
