# Installer Brume

Brume remplace le menu HOME de la Switch par un menu à nous : une brume bleue qui respire, des cubes d'eau, une roue
de consoles en 3D, les jeux dans de vraies boîtes, et des sons et vibrations à chaque geste.

> ⚠️ Brume est un projet perso, non officiel, sans lien avec Nintendo ni Sony. Il ne contient aucun jeu, aucun BIOS,
> aucune clé, aucune jaquette officielle. Tu l'utilises à tes risques (comme tout ce qui touche au CFW).

## Ce qu'il faut avant

- Une Switch avec **Atmosphère** (testé : Switch V1, Atmosphère 1.12.x, firmware 22.5.0, sur **emuMMC**).
- Une carte SD en **FAT32** (exFAT déconseillé).
- Un PC pour copier les fichiers (ou la Switch en **UMS** avec Hekate : *USB Tools → SD Card*).

## Installer (3 minutes)

1. **Fais une sauvegarde** du dossier `atmosphere/contents/0100000000001000/` de ta carte SD s'il existe
   (c'est là qu'un autre menu, comme sLaunch, est peut-être installé).
2. **Copie tout le contenu du zip** (`atmosphere/`, `brume/`, `lib/`, `qlaunch-ext/`, `switch/`…) **à la racine de ta
   carte SD** (accepte de fusionner les dossiers).
3. **Démarre la Switch** normalement (Hekate → Atmosphère). Brume s'ouvre sur l'écran verrouillé : appuie **3 fois sur
   n'importe quel bouton** (ou touche l'écran 3 fois).

C'est tout. Les réglages de Brume se trouvent ensuite dans **R → … → Réglages → Brume**.

## Revenir en arrière (si quelque chose ne va pas)

- **Depuis la console** : maintiens **L + R + −** pendant 3 secondes. La console redémarre sur le menu Nintendo
  (ou sur sLaunch s'il est installé).
- **Depuis le PC** (écran noir au démarrage) : renomme le dossier `atmosphere/contents/0100000000001000` en
  `0100000000001000_off`. Le menu Nintendo revient. Pour remettre Brume, renomme-le dans l'autre sens.

## Ajouter des jeux (facultatif)

### Jeux Switch
Rien à faire : les jeux installés apparaissent dans la case **Switch** de la roue.

### Émulateurs (à installer toi-même, chacun a sa licence)

**Rien à configurer (v90)** : copie le `.nro` de l'émulateur dans `switch/` (ou `switch/<un dossier>/`) et tes jeux (tes
propres sauvegardes) dans `roms/<console>/`, puis redémarre la console (ou reviens d'un homebrew). Brume trouve l'émulateur
tout seul, par le nom du fichier ou par le nom écrit dans le `.nro` (un fichier renommé marche aussi) : la console **arrive
dans la roue** (la roue tourne vers elle) et une notification dit « Nouvel émulateur détecté ». **La roue ne montre que les
consoles qui ont un émulateur** (la Switch toujours, « Ports natifs » s'il y a des ports). Des jeux sans leur émulateur ?
Une notification le dit une fois (« Des jeux PS2 trouvés : installe NetherSX2 pour y jouer »). Un émulateur retiré : sa
console quitte la roue sans bruit.

| Console | Dossier des jeux (noms reconnus) | Émulateurs reconnus (dans l'ordre de préférence) | État |
|---|---|---|---|
| PlayStation 2 | `roms/ps2/`, `PS2`, `playstation2` (`.iso`, `.chd`, `.cso`, `.bin`) | NetherSX2 (NetherSX2_nx), AetherSX2, PCSX2 | ✅ marche (selon les jeux) |
| GameCube | `roms/gamecube/`, `gc`, `ngc` (`.iso`, `.rvz`, `.gcz`…) | Nezumiiruka, Dolphin (dolphin-nx) | ✅ marche (selon les jeux) |
| PSP | `roms/psp/` (`.iso`, `.cso`, `.pbp`) | PPSSPP (PPSSPP-nx) | ✅ fluide |
| Nintendo 64 | `roms/n64/`, `nintendo64` (`.z64`, `.n64`, `.v64`, `.zip`) | RetroArch : Mupen64Plus-Next, ParaLLEl N64, Mupen64Plus | ⚠️ expérimental |
| PlayStation | `roms/ps1/`, `psx`, `playstation` (`.chd`, `.cue`, `.pbp`, `.m3u`) | RetroArch : PCSX ReARMed, SwanStation, DuckStation, Beetle PSX | ⚠️ expérimental |
| Game Boy Advance | `roms/gba/`, `gameboyadvance` (`.gba`, `.zip`) | mGBA, RetroArch : mGBA, VBA Next, VBA-M, gpSP, Beetle GBA | ⚠️ expérimental |

**RetroArch** : installe-le normalement (`switch/retroarch_switch.nro` + le dossier `retroarch/` avec ses cœurs). Brume
lance directement le bon cœur (`retroarch/cores/<cœur>_libretro_libnx.nro`) avec le jeu, jamais RetroArch lui-même (il
plante en changeant de cœur sur le FW 22.5). Un cœur copié tout seul et nommé d'après sa console
(`switch/retroarch-n64/retroarch-n64.nro`, la méthode testée) passe en premier.

Ce que Brume a trouvé est noté dans `sdmc:/brume/emulateurs-detectes.txt` (refait à chaque démarrage, inutile d'y toucher).

**Une seule fois, recommandé : le Brume Launcher.** Dans **sphaira**, va sur `switch/brume-launcher/brume-launcher.nro`
et crée son **forwarder**. Ensuite Brume lance tes homebrews, émulateurs et ports par ce titre-là (toute la mémoire,
aucun jeu emprunté). Sans lui, Brume emprunte un jeu installé, comme avant.

Tu veux imposer un autre émulateur que celui que Brume choisit ? Écris son chemin dans `sdmc:/brume/emulateurs.ini`
(Brume crée ce fichier au premier lancement, avec des exemples : `gamecube=sdmc:/switch/dolphin-nx/dolphin.nro`,
plusieurs chemins séparés par `;`, le premier qui existe gagne) : il passe avant la détection.

Les jeux sont lancés **en mode application** (toute la mémoire) : il faut **au moins un jeu Switch installé** sur la
console (Brume « l'emprunte » le temps du lancement, puis remet tout en place).

### Ports natifs (des jeux PC / Xbox recompilés pour la Switch)
Mets le `.nro` d'un port dans `switch/<son dossier>/`. S'il a un sous-dossier `game`, `game-xxx` ou `game_root`, il
apparaît tout seul dans la case **Ports natifs** ; sinon, écris le nom de son dossier dans `brume/ports.txt`.
La plupart des ports demandent **les fichiers de ton propre jeu** (voir leur page).

### Jaquettes (facultatif, sur le PC)
Depuis la v75, **la console télécharge elle-même les jaquettes** de tes jeux (PS2, PSP, PS1, GameCube, N64, GBA) quand le
Wi-Fi est là : Réglages > Données > « Télécharger les jaquettes », ou toutes seules (« Jaquettes automatiques »).
Les noms de fichiers Redump / No-Intro (ex. `SSX 3 (USA).iso`) donnent les meilleurs résultats ; une jaquette introuvable est
notée dans `sdmc:/brume/jaquettes/<console>/introuvables.txt` (efface ce fichier pour qu'elle soit cherchée de nouveau).
Depuis la v77, les **jeux Switch installés** ont aussi leur vraie jaquette, prise sur **GameTDB** (gametdb.com) : le jeu est
retrouvé par son nom (celui de la console, puis son nom anglais : v93), la jaquette est rangée dans
`sdmc:/brume/jaquettes/switch/` et remplit toute la face du boîtier rouge (sans elle, la boîte garde l'icône du jeu). Les jeux
Switch introuvables sont notés dans `switch/introuvables-v93.txt`.
Un téléchargement en cours se voit en haut à droite (les petites lumières qui tournent) : touche-les, ou appuie sur ZR, pour
voir où il en est (en cours, en attente du réseau, bloqué et pourquoi, et depuis combien de temps rien n'est arrivé).

Elles peuvent aussi se préparer sur le PC avec `tools/art-builder/art_builder.py` (Python 3 + ImageMagick) :
il lit tes jeux sur la carte SD, télécharge une jaquette par jeu **une requête à la fois**, et fabrique des fichiers
`.btex` à copier dans `sdmc:/brume/jaquettes/<console>/`.

## Personnaliser

- **Réglages → Brume** : thème (Brume, Nuit profonde, Chrome), couleur d'ambiance, couleur d'accent, police de
  l'heure, code de verrouillage, cube doré (un secret…).
- **Tes propres sons** : dépose des fichiers `.wav` (16 bits) dans `sdmc:/brume/perso/` avec le même nom que ceux de
  `sdmc:/brume/sons/` ; ils remplacent les nôtres au prochain démarrage.

## Désinstaller

Supprime `atmosphere/contents/0100000000001000/`, `qlaunch-ext/`, `lib/`, `brume/` et `switch/brume-launcher/` de la carte SD
(et remets ta sauvegarde de l'étape 1 si tu en avais une).
