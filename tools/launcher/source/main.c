// Brume Launcher - Brume's own forwarder (GPL-3, see brume/LICENSE).
//
// Made once into a forwarder with sphaira (a real installed title: its icon, its ID, all the memory). Brume then
// replaces, for one launch, the two files of that forwarder's romfs that say which .nro to start (Atmosphère's
// LayeredFS: atmosphere/contents/<id>/romfs/nextNroPath + nextArgv) and starts it: any homebrew, emulator (with
// its game) or port runs in application mode without borrowing a game. Started on its own, it only says what it is.
#include <switch.h>
#include <stdio.h>

int main(int argc, char **argv) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("\n\n   \x1b[36mBrume Launcher\x1b[0m\n\n");
    printf("   Ce titre sert a Brume pour lancer tes homebrews, tes emulateurs\n");
    printf("   (avec leur jeu) et tes ports avec toute la memoire,\n");
    printf("   sans emprunter de jeu Switch.\n\n");
    printf("   Rien a faire ici : lance tes apps depuis Brume.\n\n");
    printf("   Appuie sur + pour quitter.\n");
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
}
