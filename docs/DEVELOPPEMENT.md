# Brume — notes de développement

(L'ancien README, gardé ici : le README public est maintenant `README.md`.)


Module **qlaunch-ext** (`brume.dnro`) qui dessine directement avec **deko3d** (neko3d), sans SDL.
Le loader qlaunch-ext garde la partie système (HOME, veille, retour de jeu, rien n'est appelé quand un jeu tourne).

- `src/Gpu.*` : petit moteur deko3d (720p en portable, 1080p en dock, 2 images, chrono GPU).
- `src/Entry.cpp` : les 7 fonctions appelées par le loader + la première scène (brume + cubes de verre).
- `shaders/` : GLSL 460, compilés par `uam` (`common.glsl` = bloc d'uniformes partagé).
- Compiler (WSL) : `wsl -d archlinux -u root -- sh <le dépôt vu depuis WSL, ex. /mnt/c/...>/scripts/build.sh`
  → `builds/brume/brume.dnro` (vérifie que tous les imports existent dans le loader et `sdmc:/lib`).

## Installer / revenir en arrière (sur la SD)

- Copier `brume.dnro` dans `sdmc:/qlaunch-ext/menus/`.
- `sdmc:/qlaunch-ext/config/menu.ini` : `[menu]` puis `path=sdmc:/qlaunch-ext/menus/brume.dnro`.
- **Mode essai** : si `sdmc:/brume/essai.txt` existe, Brume supprime `menu.ini` dès son démarrage → au prochain
  redémarrage, sLaunch revient tout seul.
- **Secours** : supprimer `sdmc:/qlaunch-ext/config/menu.ini` → sLaunch (`menu.dnro`) redémarre.
- Mesures : `sdmc:/brume/perf.log` (fréquences, temps d'image, temps GPU et CPU, les 2 premières minutes).

## Ajouts (2026-11-17)

- `scripts/build-loader.sh` : recompile le loader qlaunch-ext avec nos corrections (v42.4 : `avm` n'est plus gardé
  ouvert) et vérifie qu'aucune fonction exportée aux menus n'est perdue. Sorties : `../builds/loader/`.
- `scripts/package.sh [version]` : le paquet d'installation `../dist/brume-<version>/` (+ `.zip`).
- Les fichiers de l'heure (polices à la demande) : `$(BUILD)/polices/*.bfnt` → `sdmc:/brume/polices/`.
- Profil actif : `sdmc:/brume/profil.txt` ; réglages de Brume : `sdmc:/brume/reglages.ini`.
