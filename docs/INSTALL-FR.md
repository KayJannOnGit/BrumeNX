# Brume bêta — installer

Brume remplace le menu HOME de la Switch par un menu à nous : une brume bleue qui respire, des cubes d'eau, une roue
de consoles en 3D, tes jeux dans de vraies boîtes, un son et une vibration à chaque geste.

> ⚠️ Projet perso, non officiel, sans lien avec Nintendo ni Sony. Aucun jeu, BIOS, clé ni jaquette officielle dedans ; les logos des consoles (Wikimedia Commons, domaine public) sont des marques de leurs propriétaires.
> C'est une **bêta** : des choses vont casser. Tu l'utilises à tes risques, comme tout ce qui touche au CFW.

## 1. Ce qu'il faut

- Une Switch avec **Atmosphère** sur une **emuMMC** (testé : Switch V1 « Erista », Hekate 6.5.4, Atmosphère 1.12.2,
  firmware 22.5.0). Les autres modèles (V2, Lite, OLED) et firmwares : **pas encore testés**, dis-nous ce que ça donne.
- Une carte SD en **FAT32**, et un PC (ou Hekate → *USB Tools* → *SD Card* pour voir la carte sur le PC).
- Facultatif : au moins **un jeu Switch installé** (il en faut un pour lancer les émulateurs, voir le guide complet).

> 🛑 **N'utilise jamais Brume sur ta sysNAND** (la mémoire de la console, celle avec laquelle tu vas en ligne).
> Brume remplace le menu HOME et **écrit de vrais réglages système** (veille, TV, langue…). Seulement sur une emuMMC.
> Garde ton emuMMC coupée des serveurs de Nintendo (blocage DNS d'Atmosphère, `atmosphere/hosts/emummc.txt`).

## 2. Installer (5 minutes)

1. Éteins la console et mets la carte SD dans ton PC.
2. **Sauvegarde** ces dossiers s'ils existent sur la carte (un autre menu, comme sLaunch, y est peut-être) :
   `atmosphere/contents/0100000000001000/`, `qlaunch-ext/` et `lib/`.
3. Dézippe `brumenx-….zip` et copie **tout son contenu à la racine de la carte SD** (ou laisse faire l'installeur : `install-brumenx.sh` sur Mac / Linux, `install-brumenx.bat` sur Windows ; il vérifie chaque fichier et ne supprime rien).
   Accepte de *fusionner les dossiers* / *remplacer les fichiers*.
4. Remets la carte, démarre Atmosphère **sur ton emuMMC** depuis Hekate.
5. Brume s'ouvre sur l'écran verrouillé : **appuie 3 fois sur n'importe quel bouton** (ou touche l'écran 3 fois).

Brume suit la langue de la console (français ou anglais). Pour la forcer : **R** jusqu'à la page **Réglages** →
**Brume** → **Langue de Brume**.

## 3. Jouer

**Jeux Switch** : ils sont sur la roue tout de suite et se lancent avec **A**. Leurs jaquettes se téléchargent toutes
seules dès que la console est en Wi-Fi (Réglages > Données > « Jaquettes automatiques », allumé de base).

**Autres consoles** (rien à régler, Brume trouve tout au démarrage) :
1. Mets l'**émulateur** sur la carte, n'importe où dans `switch/` (ex. `switch/NetherSX2/NetherSX2.nro`), ou RetroArch
   avec ses cœurs dans `retroarch/cores/`. Reconnus : NetherSX2 / AetherSX2 (PS2), Nezumiiruka / Dolphin (GameCube),
   PPSSPP (PSP), DraStic / melonDS (DS), et les cœurs RetroArch (N64, PS1, GBA, SNES, NES, Mega Drive, Game Boy,
   Master System, Game Gear).
2. Mets **tes propres jeux** dans `roms/<console>/` : `roms/ps2`, `roms/gc`, `roms/n64`, `roms/psx`, `roms/psp`,
   `roms/gba`, `roms/nds`, `roms/snes`, `roms/nes`, `roms/megadrive`, `roms/gb`, `roms/mastersystem`, `roms/gamegear`.
3. Les émulateurs PS1 et PS2 demandent **ton propre BIOS** (voir le guide de chaque émulateur). Brume ne fournit
   jamais de jeu, de BIOS ni de clé.
4. Redémarre la console : chaque console qui a des jeux apparaît sur la roue.

Brume lance les émulateurs avec toute la mémoire d'un vrai jeu en empruntant un instant un de tes jeux Switch installés
(automatique : il faut au moins un jeu Switch installé). En option, une seule fois : ouvre
`switch/brume-launcher/brume-launcher.nro` dans sphaira et fais-en un **forwarder** nommé « Brume Launcher » ; Brume
l'utilise alors et n'emprunte plus rien.

## 4. Revenir à ton ancien menu

Prends la première solution qui marche pour toi :

1. **Depuis Brume** : maintiens **L + R + −** pendant **3 secondes**. La console redémarre
   — sur sLaunch s'il est installé, sinon sur le menu HOME de Nintendo. (Ça coupe aussi l'overlay Brume.)
2. **Depuis le PC, seulement si sLaunch est installé** (le fichier `qlaunch-ext/menus/menu.dnro` existe) :
   supprime `qlaunch-ext/config/menu.ini`. Au prochain démarrage, c'est sLaunch.
   ⚠️ Sans sLaunch, **ne fais pas ça** : le loader ne trouverait aucun menu et l'écran resterait noir. Prends la solution 3.
3. **Depuis le PC, marche toujours (même écran noir)** : renomme le dossier
   `atmosphere/contents/0100000000001000` en `0100000000001000_off`. Le menu Nintendo revient.
   Si tu avais allumé l'overlay Brume, renomme aussi `atmosphere/contents/010000000000100C` en `010000000000100C_off`.

Pour remettre Brume : renomme le dossier dans l'autre sens (enlève `_off`), ou recopie `qlaunch-ext/config/menu.ini` du zip.

**Désinstaller** : supprime `atmosphere/contents/0100000000001000/`, `qlaunch-ext/`, `lib/`, `brume/` et
`switch/brume-launcher/`, puis remets ta sauvegarde de l'étape 2.

## 5. Signaler un bug

1. Note **ce que tu as fait, ce qui s'est passé, et l'heure**.
2. Éteins la console, mets la carte dans le PC et copie ces fichiers (ceux qui existent) :
   - `brume/perf.log` — le journal de Brume (il grandit à chaque démarrage : la fin du fichier = le plus récent) ;
   - `qlaunch-ext-log.txt` (à la racine de la carte) — le journal du loader ;
   - les fichiers les plus récents de `atmosphere/crash_reports/`, `atmosphere/fatal_reports/` et
     `atmosphere/fatal_errors/` (plantages, écrans d'erreur) ;
   - `brume/overlay/journal.txt` — seulement si tu as allumé l'overlay Brume ;
   - `brume/reglages.ini` — tes réglages de Brume (enlève d'abord la ligne `codeseq=` si tu as mis un code).
3. Remplis le modèle à la fin de `BETA-CHECKLIST.md`, ajoute une photo ou une vidéo de l'écran si tu peux, et poste-le
   là où tu as eu la bêta.

Les journaux peuvent contenir le nom de ton profil et les noms de tes fichiers de jeux : jette un œil avant de les envoyer.

Pour aller plus loin (émulateurs, ports natifs, jaquettes, tes propres sons) : `GUIDE-COMPLET-FR.md`.
