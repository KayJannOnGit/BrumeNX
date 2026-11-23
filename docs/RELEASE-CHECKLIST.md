# Le jour où Yass dit « publie » — la liste exacte

> Rien de tout ça n'a été fait. Rien n'est publié, aucun dépôt GitHub n'existe, aucun remote n'est configuré.
> Chaque étape ci-dessous se fait **seulement** quand Yass l'a décidé, et dans l'ordre.

## 0. Avant : les décisions de Yass (voir « Questions ouvertes » en bas)

Ne rien publier tant que les questions **bloquantes** (B) n'ont pas de réponse.

## 1. Préparer (sur le PC, rien ne sort)

1. Fusionner la branche `publication` dans `master` (ou la rebaser) une fois relue.
2. Mettre le numéro de version (ex. `v1.0.0`) là où il apparaît : titre de la release, `scripts/package.sh <version>`.
3. Ajouter les **captures d'écran** dans `docs/apercus/` et remplacer le tableau « *(screenshot: …)* » du `README.md`.
   Les meilleures : prises sur la vraie console (bouton Capture → Album → copier sur le PC). Idées dans le commentaire
   du README. **Pas de jaquette officielle, pas de vrai nom de profil, pas d'avatar perso** sur les captures.
4. Compiler (WSL) : `scripts/build.sh`, `overlay/build.sh`, `musique/build.sh` (et `scripts/build-loader.sh` si le loader a changé).
5. Faire le paquet : `sh scripts/package.sh v1.0.0` → `dist/brumenx-v1.0.0.zip`, `install-brumenx.sh/.ps1/.bat`,
   `brumenx-v1.0.0.sha256`. Le script s'arrête tout seul sur un fichier inattendu, un logo, un réglage ou un chemin perso.
6. **Tester le zip sur une carte « propre »** (Atmosphère seul, sans sLaunch) : installation avec l'installeur, premier
   démarrage, L + R + − (retour au menu Nintendo), renommer `0100000000001000` en `_off` (retour au menu Nintendo),
   remettre. Puis une mise à jour par-dessus une installation existante (rien ne doit être perdu).
7. Construire le **dépôt public propre** (un seul commit, sans l'historique privé) :
   ```sh
   AUTHOR_NAME="<pseudo>" AUTHOR_EMAIL="<id>+<pseudo>@users.noreply.github.com" \
     sh scripts/snapshot-public.sh ../brumenx-public master v1.0.0
   ```
   Le script refuse un dossier qui existe déjà et s'arrête sur tout chemin perso, secret, fichier de jeu / BIOS / clé,
   son, modèle 3D ou image non prévue. Relire `../brumenx-public` (au moins `git show --stat`).

## 2. Publier (seulement après le « publie » de Yass)

1. Sur github.com (compte de Yass, connecté par lui-même) : **New repository** → nom `BrumeNX`, **Public**,
   **sans** README / licence / .gitignore (ils sont déjà dans le snapshot).
2. Dans `../brumenx-public` (et nulle part ailleurs : **jamais** depuis le dépôt privé, son historique contient des
   chemins avec le vrai nom de Yass) :
   ```sh
   git remote add origin https://github.com/<compte>/BrumeNX.git
   git push -u origin main
   git tag v1.0.0 && git push origin v1.0.0
   ```
3. **Releases** → *Draft a new release* → tag `v1.0.0`, titre « BrumeNX v1.0.0 », texte ci-dessous, et joindre :
   `brumenx-v1.0.0.zip`, `install-brumenx.sh`, `install-brumenx.ps1`, `install-brumenx.bat`, `brumenx-v1.0.0.sha256`.
   (GitHub ajoute tout seul le code source du tag : c'est la source de BrumeNX ; `sources/README.md` dit où est celle
   des binaires tiers.) Cocher « Set as a pre-release » si Yass préfère une bêta publique.
4. Dans les réglages du dépôt : **Issues** activées (pour les bugs), description courte, topics `nintendo-switch`,
   `homebrew`, `atmosphere`, `qlaunch`, `custom-home-menu`.
5. Poster sur Reddit (texte ci-dessous), en répondant d'abord à ceux qui l'ont demandé.

### Texte de la release (anglais)

```
BrumeNX v1.0.0 — first public release

A custom HOME menu for the Nintendo Switch, inspired by the PS2 boot screen: blue fog, water cubes, a 3D console
wheel, your games in real 3D boxes, a sound and an HD rumble for every gesture. Replaces qlaunch through qlaunch-ext.

Install: run install-brumenx.sh (macOS/Linux) or install-brumenx.bat (Windows) next to the zip, or copy everything
in the zip to the root of your SD card. emuMMC only. Tested: Erista (V1), Atmosphère 1.12.2, firmware 22.5.0.
Rescue: hold L + R + Minus for 3 s, or rename atmosphere/contents/0100000000001000 to 0100000000001000_off.

No games, BIOS, keys, logos or official covers included. Not affiliated with Nintendo or Sony. GPL-3.0.
SHA-256 of every file: brumenx-v1.0.0.sha256
```

### Brouillon du post Reddit (r/SwitchHacks ou r/SwitchPirates selon les règles du sous-forum : vérifier avant)

```
Title: [Release] BrumeNX — a PS2-boot-inspired custom HOME menu for the Switch (qlaunch-ext)

A few of you asked for it, so here it is: BrumeNX, a full replacement for the Switch HOME menu that I've been
building, inspired by the PS2 boot screen.

- Lock screen in the dark, three presses = three waves of light, then a big bang
- 3D console wheel (Switch, PS2, GameCube, N64, PS1, PSP, GBA, native ports), emulators detected automatically
- Games in real 3D boxes (Cover Flow); on launch the disc flies into its console
- Apps, Album, Controllers (live Joy-Con colours), Settings wired to the real system settings (carousel or XMB)
- Every gesture has its own sound, always in key, and HD rumble; 60 FPS on an Erista; zero cost while a game runs

Install: one zip + an installer for Windows/macOS/Linux that checks every file and deletes nothing.
emuMMC only. Tested on Erista, Atmosphère 1.12.2, FW 22.5.0 — Mariko/Lite/OLED reports very welcome.
Rescue: hold L + R + Minus for 3 s.

Built on the qlaunch-ext loader (Horizon-OC), with fixes from sLaunch (etonedemid). GPL-3.0.
No games, BIOS, keys, logos or official covers included.

GitHub: <link>   Screenshots/video: <link>
Bug reports: GitHub issues (template in BETA-CHECKLIST.md, with brume/perf.log).
```

## 3. Après

- Garder `dist/brumenx-v1.0.0.zip` et le commit du snapshot (pour pouvoir redonner la source exacte).
- Les versions suivantes : refaire le paquet, puis dans `../brumenx-public` copier le nouvel état du dépôt privé
  (ou refaire un snapshot dans un dossier neuf et pousser son contenu comme un nouveau commit), tag, release.

---

## Questions ouvertes pour Yass

**(B) = bloquant avant de publier.**

1. **(B) Le nom affiché** : le dépôt, les commits et `LICENSE` disent « Yass ». Garder ce pseudo, un autre pseudo,
   ou ton nom ? (Ton vrai nom n'est **nulle part** dans le snapshot ; il est seulement dans l'historique privé, qui ne
   sera jamais poussé.) Et quel compte GitHub publie (pour l'adresse `noreply` des commits) ?
2. **(B) La licence du loader qlaunch-ext** : ses fichiers disent « GPLv2 » sans « ou version ultérieure », alors que
   Brume (et le correctif de sLaunch) sont en GPL-3. sLaunch publie déjà exactement le même montage, donc c'est la
   pratique courante, mais le plus propre est de **demander aux auteurs de qlaunch-ext** (issue sur leur GitHub) si
   c'est « GPLv2 ou plus » ou s'ils autorisent les menus en GPL-3. Publier avant leur réponse, ou attendre ?
3. **Spotify (Brume Musique) dans la première version ?** Il marche, mais c'est un client Spotify non officiel (contre
   leurs conditions, petit risque pour le compte de l'utilisateur) et ses identifiants viennent de Switify. Options :
   le garder (endormi par défaut, comme maintenant), ou le sortir du zip pour la v1.
4. **Nextendo** : pour l'instant chaque utilisateur doit créer sa propre « appli » sur nextendo.network (fastidieux pour
   ton cousin). Un `client_id` d'appli publique OAuth n'est pas un secret : on pourrait livrer celui de ton appli
   « Brume » dans le zip (si les conditions de Nextendo le permettent). D'accord ? Sinon, on garde l'étape manuelle.
5. **Overlay Brume** : livré **prêt mais éteint** (on l'allume dans Réglages > Brume). Le garder comme ça, ou le retirer
   de la v1 (il remplace aussi le menu Power, ta priorité n°1) ?
6. **Les modèles 3D N64 et PS2** sont « pas d'usage commercial ». OK tant que BrumeNX est gratuit. Si un jour tu veux
   des dons (Ko-fi…), il faudra d'abord remplacer ces deux modèles.
7. **La maquette et les outils qui fabriquent les modèles / sons / `ui.bin`** (`tools/bake-assets.html`,
   `tools/render-sounds.html`) lisent la maquette `docs/M1/maquette-brume.html` du projet privé. Les fichiers produits
   sont dans le zip ; mais pour que quelqu'un puisse **les refaire** depuis le dépôt public, il faudrait y ajouter la
   maquette (sans les logos ni les modèles d'origine). La publier aussi, ou non ?
8. **La version** : `v1.0.0` ou une « bêta publique » (`v0.9.0`, pre-release) ? (Tu avais dit « la V1 viendra avec
   plus que ça ».)
9. **Le nom** : « BrumeNX » partout (dépôt, zip, Spotify le montre déjà) mais le dossier sur la carte reste `brume/`
   (le changer casserait les installations des testeurs). D'accord ?
10. **Où poster** : quel sous-reddit, et avec quelle vidéo / captures.
