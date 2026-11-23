# Brume — code et outils tiers

| Quoi | Licence | Où | Rôle |
|---|---|---|---|
| **three.js** r160 (jsDelivr, version figée) | MIT | `tools/bake-assets.html`, `tools/render-sounds.html` (PC seulement) | lire les modèles 3D de la maquette, calculer la lumière de la pièce (RoomEnvironment) ; le shader des consoles reprend la formule MeshStandardMaterial / ACES de three.js |
| **meshoptimizer** 0.21.0 (Arseny Kapoulkine, jsDelivr) | MIT | `tools/bake-assets.html` (PC seulement) | alléger les consoles (simplification) et ranger les triangles pour le cache du GPU |
| **deko3d / neko3d**, **qlaunch-ext**, **libnx** | voir leurs dépôts (zlib / GPL-2 / ISC) | menu console | moteur graphique, chargeur du menu HOME, bibliothèque système |
| Bibliothèques de qlaunch-ext dans `sdmc:/lib/` : **FFmpeg** (compilé avec `--enable-gpl`), **SDL 3**, **nlohmann/json**, **miniini**, **xxHash**, **libnx-dlink** | GPL-2.0+ / zlib / MIT / Apache-2.0 / BSD-2 / Unlicense (textes dans le paquet : `brume/docs/LICENCES/`) | `sdmc:/lib/*.dnro` | chargées par le loader (copiées depuis la build de sLaunch) |
| **stb_image** v2.30 (Sean Barrett), copie figée prise dans `qlaunch-ext/menu/src/third_party/` | domaine public ou MIT (au choix) | `src/third_party/stb_image.h`, compilé par `src/Jpeg.cpp` (JPEG seulement, sans TLS) | décoder les icônes JPEG des jeux Switch (boîtes v31) et les jaquettes GameTDB (v77) |
| **QR Code generator library** v1.8.0 (Project Nayuki, github.com/nayuki/QR-Code-generator, fichiers `c/qrcodegen.c` et `c/qrcodegen.h`, copiés sans changement) | MIT (texte : `src/third_party/qrcodegen-LICENSE.txt` et en-tête des fichiers) | `src/third_party/qrcodegen.c/.h`, compilé dans `src/Qr.cpp` (version 10 au plus, sans allocation, ses `assert` coupés) | les QR codes de Brume (v95) : le salon vocal Discord, la connexion à Nextendo depuis un téléphone |
| **Nextendo Network** (nextendo.network) : seulement son **API publique** (OAuth 2.0 PKCE, `/api/friends`, `/api/oauth/userinfo`, `/api/avatar`, `/api/game-icon`), comme client tiers, avec l'appli « Brume » de Yass créée sur leur portail | leur code est sous PolyForm Shield (pas libre) : **rien n'est copié** ; avatars et icônes jamais dans le dépôt | `src/Social.cpp` (v95), réponses lues défensivement ; images gardées sur la carte (`sdmc:/brume/cache/amis/`) | la page Amis : amis, demandes, présence, notifications |
| **sys-clk** 2.0.1 (retronx-team/sys-clk : seulement la forme publique de son IPC « sys:clk » — commandes 2 et 8, structure du contexte de `sysclk/clock_manager.h`, `board.h`) | Beer-Ware (Revision 42) | `overlay/source/main.cpp` (ligne « Performance » du volet HOME) | lire les fréquences et poser / retirer un réglage (fréquences officielles de Nintendo) ; aucun fichier de sys-clk dans le dépôt, le module est installé à part sur la carte |
| **libretro-thumbnails** (jaquettes avant, `Named_Boxarts`) | images officielles des éditeurs : **jamais dans le dépôt** | téléchargées par la console elle-même depuis la v75 (`src/Covers.cpp`, raw.githubusercontent.com, une requête à la fois, cache des trouvées et des introuvables), ou par `tools/art-builder/` sur le PC ; rangées dans `sdmc:/brume/jaquettes/` | jaquettes des boîtes 3D (v31) |
| **GameTDB** (gametdb.com : la liste publique des titres `switchtdb.txt`, et les jaquettes `art.gametdb.com/switch/coverM/<région>/<ID>.jpg`) | images officielles des éditeurs, base de données de la communauté GameTDB : **jamais dans le dépôt** | téléchargées par la console elle-même depuis la v77 (`src/Covers.cpp`, HTTPS, une requête à la fois avec une pause, liste des titres gardée 14 jours dans `sdmc:/brume/jaquettes/switch/gametdb-*.txt`, introuvables notées) ; rangées dans `sdmc:/brume/jaquettes/switch/<ID du titre>.btex` | jaquettes des jeux Switch installés (le jeu est retrouvé par son nom dans la liste de GameTDB) |
| **zlib** 1.3.1 (Jean-loup Gailly, Mark Adler) : celle que le chargeur qlaunch-ext exporte ; seulement son en-tête au moment de compiler (paquet devkitPro `switch-zlib`) | zlib License | `src/Covers.cpp` (`inflate` en flux) | décoder les PNG des jaquettes au fur et à mesure qu'elles arrivent, sans jamais garder l'image entière en mémoire (v75) |
| **Polices de l'interface** : Outfit, Lexend Exa, Lexend Giga | SIL Open Font License 1.1 (`assets/fonts/OFL-Outfit.txt`, `OFL-Lexend.txt`) | `assets/fonts/`, pré-rendues par `tools/fontbake.cpp` (le menu) et par `overlay/tools/bakefont.py` (Brume Overlay : Outfit, intégrée au programme depuis la v90, plus de FreeType ni de libjpeg-turbo) | tous les textes, l'heure de l'écran verrouillé |
| **Police du logo** (v1.0.0) : Zen Dots (The Dots Project Authors), dépôt officiel github.com/google/fonts | SIL Open Font License 1.1 (`assets/fonts/OFL-zendots.txt`) | `assets/fonts/`, vectorisée dans les SVG de `docs/logo/` par `tools/logo/make_logo.py` | le logo et la bannière de BrumeNX |
| **Polices de l'heure** (v44) : Michroma, Big Shoulders Display, Instrument Serif, Orbitron, Major Mono Display, Monoton (dépôt officiel github.com/google/fonts) | SIL Open Font License 1.1 (`assets/fonts/OFL-*.txt`) | `assets/fonts/`, pré-rendues par `tools/fontbake.cpp` dans des fichiers `sdmc:/brume/polices/<nom>-<échelle>.bfnt` (chiffres et deux-points), chargées seulement quand on les choisit | Réglages > Brume > Police de l'heure |
| **hbloader** (switchbrew nx-hbloader, ISC) modifié par **sLaunch** (GPL-3.0) | ISC / GPL-3.0 | `sdmc:/brume/bin/hbloader_app/` (copié depuis la build de sLaunch) | lancer un `.nro` en mode application (homebrews, émulateurs, ports) |
| **qlaunch-ext** (Horizon-OC, GPL-2.0) + correctifs de **sLaunch** (GPL-3.0) + les nôtres (`scripts/build-loader.sh`) ; source exacte : `sources/qlaunch-ext/loader-brumenx.patch` sur le commit `d7306be` (`sources/README.md`) | GPL-2.0 / GPL-3.0 | `atmosphere/contents/0100000000001000/exefs/`, `sdmc:/lib/*.dnro`, `sdmc:/qlaunch-ext/bin/` | le loader qui remplace le menu HOME |

## Modèles 3D des consoles

Allégés et convertis en `.bmdl` (`sdmc:/brume/modeles/`) par `tools/bake-assets.html`. Licences des auteurs
(Sketchfab), avec les phrases de crédit demandées :

- This work is based on "Nintendo Switch" (https://sketchfab.com/3d-models/nintendo-switch-b94e6a6a8c564fee81c2d794da6c5712) by gabrieljuan0322 (https://sketchfab.com/gabrieljuan0322) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)
- This work is based on "PlayStation 2" (https://sketchfab.com/3d-models/playstation-2-8c992e9f5c86496ca7bd7dfe109d3cc2) by Smoggybeard (https://sketchfab.com/Smoggybeard) licensed under CC-BY-NC-SA-4.0 (http://creativecommons.org/licenses/by-nc-sa/4.0/) — la version allégée garde cette licence (pas d'usage commercial, partage dans les mêmes conditions)
- This work is based on "GameCube console" (https://sketchfab.com/3d-models/gamecube-console-187a584e3bff46c2aa1bd8a701211fb4) by Jeffrey fan 2000 (https://sketchfab.com/8723516) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)
- This work is based on "Nintendo 64" (https://sketchfab.com/3d-models/nintendo-64-0a6b8eb8097e4365a34b1af7b6eac408) by DevonCarlson (https://sketchfab.com/devoncarlson) licensed under CC-BY-NC-4.0 (http://creativecommons.org/licenses/by-nc/4.0/) — pas d'usage commercial
- This work is based on "Playstation 1" (https://sketchfab.com/3d-models/playstation-1-96bed8283f2940d7bb4b1866ab0637a2) by ecjhr0awipso (https://sketchfab.com/ecjhr0awipso) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)
- This work is based on "Sony PSP" (https://sketchfab.com/3d-models/sony-psp-dca89d10ec304d0cab76837750df7761) by Ilya Ostrovsky (https://sketchfab.com/strov) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)
- This work is based on "Gameboy Advance SP" (https://sketchfab.com/3d-models/gameboy-advance-sp-18c9688eaa8f4903a940c105242df15f) by thegraphicsgeek (https://sketchfab.com/thegraphicsgeek) licensed under CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)

## Logos des consoles

Fichiers de Wikimedia Commons (domaine public / CC0, liste et sources : `assets/logos/SOURCES.md`), intégrés à
`sdmc:/brume/ui.bin` pour le bandeau du nom des consoles. Ce sont des marques de leurs propriétaires (Nintendo, Sony),
utilisées seulement pour désigner les consoles ; Brume n'a aucun lien avec ces sociétés.

Depuis la v1.0.0 (décision de Yass), ils sont dans le dépôt (`assets/logos/`) et dans le paquet. `LOGOS=0 sh scripts/package.sh`
fait un paquet sans eux (`tools/ui_sans_logos.py` : PS2, N64, PSP et GBA ont alors leur nom écrit en Outfit).

## Verre liquide des icônes des Réglages (v73) : des idées, aucun code repris

Le verre de `shaders/verre_fsh.glsl` (mode 29, son propre programme depuis la v74), les couches PS3 de `shaders/icon_fsh.glsl`, la carte de distances de
`tools/ui_sdf.py` et de `tools/bake-assets.html` (`sdfIcons`) sont **écrits par nous**, à partir de descriptions publiques
(détails et liens : `reports/Icônes verre liquide XMB PS3.md` du projet). Aucune ligne de code, aucune image, aucun fichier
de Sony (RCO, GIM, thèmes, firmware) n'est utilisé.

| Idée | Source (licence) | Où |
|---|---|---|
| Transformée de distance exacte (enveloppe de paraboles) | article de Felzenszwalb et Huttenlocher, « Distance Transforms of Sampled Functions » (2012) : un algorithme publié, réécrit par nous | `tools/ui_sdf.py`, `tools/bake-assets.html` |
| Profil « squircle » du biseau, verre d'indice 1,5 | billet de kube.io sur le Liquid Glass (code non publié : idée seulement) | `verre_fsh.glsl` |
| Lentille en quart de cercle, lobe de lumière sur le bord | Kyant0/AndroidLiquidGlass (Apache-2.0) : principe seulement, aucune ligne reprise | `verre_fsh.glsl` |
| Reflet du côté opposé à la lumière | iyinchao/liquid-glass-studio (MIT), whynotmake-it/flutter_liquid_glass « FakeGlass » (Apache-2.0) : principe seulement | `verre_fsh.glsl`, `icon_fsh.glsl` |
| Bord clair d'un côté, plus faible de l'autre ; corps translucide | effet « Plastic » du menu XMB de RetroArch (GPL-3.0) : principe seulement | `verre_fsh.glsl` |
| Lumière qui suit le gyroscope, verre « éclairé de l'intérieur », verre qui apparaît par sa lentille | Apple, WWDC25 sessions 219 et 220 (descriptions publiques) | `Entry.cpp` (`uGlass`), `verre_fsh.glsl`, `icon_fsh.glsl` |
| Approximation de Fresnel | Christophe Schlick (1994), formule mathématique | `verre_fsh.glsl`, `icon_fsh.glsl` |

## Spotify : le lecteur « Brume Musique » (v98)

Un programme d'arrière-plan à part (`musique/`, programme `0x420000000042524D`), compilé par `musique/build.sh`. Le menu
lui-même n'en reprend que l'en-tête de son protocole (`ipc_protocol.h`, inclus tel quel par `src/Spotify.cpp`).
Liste exacte des changements : `musique/switify/MODIFICATIONS-BRUME.txt`. Explications : `docs/SPOTIFY.md`.

| Composant | Licence | Où | Pourquoi |
|---|---|---|---|
| **Switify** (HiroshiYamauchi, github.com/DomazinUS/Switify), commit `e4b8661` (2026-08-20), moteur d'arrière-plan 0.5.0 : `phase2/common`, `phase2/sysmodule`, `app/src/spotify_engine.*`, `app/src/app_settings.*`, `targets/switch` | GPL-3.0-or-later (`musique/switify/LICENSE`) | `musique/switify/` (copie modifiée) | le moteur Spotify d'arrière-plan qui joue même pendant les jeux, son service IPC |
| **Switify overlay** (Ultrahand) | GPL-2.0-only | **non repris** (incompatible avec la GPL-3 de Brume) | — |
| **cspot** (feelfreelinux et alufers), copie modifiée par Switify | GPL-3.0-or-later | `musique/switify/vendor/cspot/` | le protocole Spotify (connexion, Spotify Connect pour l'appairage, lecture) |
| **bell** (dans cspot) et ses bibliothèques : nanopb (zlib), Tremor (BSD-3), fmt (MIT), nlohmann/json (MIT), cJSON (MIT), mdnssvc (BSD-3, Darell Tan) | voir chaque `LICENSE` à côté des sources | `musique/switify/vendor/cspot/bell/` | Vorbis, protobuf, JSON, mDNS (appairage) |
| Serveur IPC compact (auteurs de sys-clk, repris par sys-tune puis Switify) | Beer-Ware rév. 42 (notice gardée) | `musique/switify/phase2/sysmodule/source/ipc_server.*` | le service `brm:spot` |
| mbedTLS, libcurl, zlib, libnx | portlibs devkitPro (Apache-2.0, curl, zlib, ISC) | liés au moment de la compilation | TLS, HTTP |

Les codecs inutilisés (AAC, MP3, ALAC, Opus) et le serveur web de bell ne sont pas copiés (jamais compilés : Vorbis seul).
Les identifiants Spotify restent sur la carte (`sdmc:/brume/spotify/credentials.json`), jamais dans le dépôt.

## Release publique (BrumeNX) : ce qui est où, et les points de licence

**Dans le dépôt git** : le code de Brume (GPL-3.0-or-later), ses shaders et outils, les polices OFL (avec leurs textes),
`musique/` (Switify modifié, GPL-3.0-or-later, et les licences de chaque bibliothèque à côté de ses sources), `src/third_party/`
(stb_image : domaine public ou MIT ; qrcodegen : MIT), et `sources/` (les modifications du loader qlaunch-ext et de SDL,
pour la source exacte des binaires de la release).

**Seulement dans le zip de la release** (fichiers produits, pas dans git) : les sons (`brume/sons/*.wav`, **créés par nous** :
synthétisés par `tools/render-sounds.html` depuis les fonctions de la maquette et par `tools/synth_*.py`, aucun
échantillon extérieur), les modèles 3D (`brume/modeles/*.bmdl` : allégés depuis les modèles Sketchfab crédités plus haut,
avec `meshoptimizer`, textures cuites, convertis par `tools/bake-assets.html` ; **chaque modèle garde la licence de son
auteur**, et la PS2 allégée reste en CC-BY-NC-SA-4.0), `ui.bin` (avec les logos des consoles, voir plus haut), les polices
de l'heure pré-rendues (`*.bfnt`, OFL), le loader et les bibliothèques de qlaunch-ext, le hbloader de sLaunch, Brume Musique.
Leurs licences sont copiées dans le zip (`brume/docs/LICENCES/`) et leurs sources exactes sont décrites dans
`sources/README.md`.

**Jamais publiés** : les jaquettes, les sons perso (`sdmc:/brume/perso`), les jeux, BIOS, clés,
réglages, journaux, identifiants (Spotify, Nextendo).

### Points ouverts (à trancher avant de publier, voir `docs/RELEASE-CHECKLIST.md`)

- **qlaunch-ext est « Licensed under the GPLv2 »** (en-têtes de ses fichiers, `LICENSE` = GPL-2.0, sans « or any later
  version »). Brume (GPL-3.0) est un module `.dnro` chargé **dans le même processus** que ce loader, et le loader distribué
  contient le correctif de sLaunch (GPL-3.0). Au sens strict de la FSF, GPL-2.0-only et GPL-3.0 ne se combinent pas dans un
  même programme. qlaunch-ext publie son API de modules précisément pour que d'autres écrivent des menus, et sLaunch est
  distribué exactement de la même façon ; mais ce n'est pas écrit noir sur blanc. **À demander aux auteurs de qlaunch-ext**
  (Horizon-OC) : « GPLv2 ou toute version ultérieure ? » ou une exception pour les menus / plugins chargés par le loader.
- **Polices de l'heure à nom réservé** (Orbitron, Monoton : « Reserved Font Name » dans leur OFL) : nos `.bfnt` sont des
  rendus en image de 11 caractères, pas des fichiers de police modifiés ; on garde le nom d'origine pour désigner la
  police qu'on a rendue. Si un doute vient, les renommer dans les Réglages suffit.
- **Brume Musique** contient, comme Switify en amont, les identifiants **publics** des applications officielles de Spotify
  (`WEB_CLIENT_ID`… dans `musique/switify/app/src/spotify_engine.cpp`) : ce ne sont pas des secrets (ils sont dans le code
  public de Switify, de librespot, etc.), mais un client Spotify non officiel reste **contraire aux conditions d'utilisation
  de Spotify** (risque pour le compte de l'utilisateur, faible mais réel). Le service est livré endormi et ne s'allume que
  si l'utilisateur active Spotify.
- **Modèles N64 (CC-BY-NC) et PS2 (CC-BY-NC-SA)** : pas d'usage commercial. Pas de vente, pas de contenu payant ;
  des dons « pour soutenir » restent un risque à éviter tant que ces deux modèles sont dans le zip.
