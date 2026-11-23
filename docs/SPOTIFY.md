# Spotify dans Brume (v98, 2026-11-23)

## Pour Yass (en simple)

**Ce que ça fait.** Tu écoutes ton Spotify sur la Switch, **même pendant un jeu**. Dans Brume :

- en haut à droite, à gauche de l'heure, **quatre petites barres** dansent quand une musique joue ; juste sous la date,
  le **titre et l'artiste** en petit ;
- **ZL** (ou un appui sur ces barres) ouvre le **panneau Spotify** : la pochette, le titre, l'artiste, l'album, où en est
  le morceau, les boutons **précédent / lecture-pause / suivant**, et le **volume** ;
- en dessous, tu choisis ta musique : **Bibliothèque** (tes playlists, albums, artistes), **Accueil**, **Rechercher**
  (le clavier s'ouvre), **À suivre**, et **Éteindre Spotify** ;
- dans le panneau : **A** choisir, **B** retour (ou fermer), **X** pause / lecture, **◀ ▶** morceau précédent / suivant,
  **− +** volume, **Y** aléatoire, **ZL** fermer ;
- **Réglages > Son > Spotify** l'allume ou l'éteint ; **Réglages > Son > Lecteur Spotify** ouvre le panneau.
- Quand Spotify joue, **la nappe de Brume se tait toute seule** (et revient quand tu mets pause).

**La toute première fois** (une seule fois) : le panneau l'explique à l'écran.

1. la Switch et ton téléphone sur le **même Wi-Fi** ;
2. ouvre **Spotify sur ton téléphone** ;
3. touche **Appareils**, puis choisis **BrumeNX** ;
4. c'est tout : la Switch s'en souvient (le fichier reste sur ta carte SD, jamais ailleurs).

Il faut **Spotify Premium** (tu l'as). Si tu avais déjà connecté Switify, Brume reprend ce compte tout seul.

**Ce qui tourne, et quand.** La musique est jouée par un petit programme à part, **« Brume Musique »**, qui continue
quand Brume dort (pendant un jeu). Il **ne démarre que quand tu allumes Spotify**, et **s'arrête quand tu l'éteins** :
éteint, **rien ne tourne**, zéro coût. Il n'est **jamais** lancé au démarrage de la console par Atmosphère (pas de
`boot2.flag`) : c'est Brume qui le lance, seulement si tu as laissé Spotify allumé.

C'est une **exception** à deux règles du projet, que tu as acceptée le 2026-11-23 :
- « zéro réseau en jeu » : **seulement pour la musique** (le menu, lui, ne fait toujours rien pendant un jeu) ;
- « pas de nouveau sysmodule » : Brume Musique est un **programme d'arrière-plan** (comme un sysmodule), mais **à la
  demande** et seulement quand Spotify est allumé.

**Ce que tu dois installer.** Rien de plus que le paquet Brume : il contient maintenant le dossier
`atmosphere/contents/420000000042524D/` (le lecteur). Copie le paquet sur la carte comme d'habitude, **redémarre une fois**
(pour qu'Atmosphère voie le nouveau programme), puis allume Spotify dans Brume.

**Si ça ne marche pas** :
- « Le lecteur n'a pas pu démarrer » : la console manque de mémoire système (trop de modules ou d'overlays en même
  temps) : enlève ceux dont tu n'as pas besoin, redémarre, **Réessayer** ;
- « Spotify refuse la lecture » : Spotify refuse parfois la lecture à certains comptes Premium récents (connu chez
  Switify, côté Spotify : on ne peut rien y faire) ;
- tu ne vois pas **BrumeNX** sur le téléphone : autorise « réseau local » pour l'appli Spotify (iPhone : Réglages >
  Spotify > Réseau local), même Wi-Fi, pas un Wi-Fi invité.
- Pour tout couper : Réglages > Son > Spotify sur « non ». En secours, supprimer le dossier
  `atmosphere/contents/420000000042524D` de la carte.

**Ce qui ne marche pas (encore).** Choisir la musique **depuis le téléphone** pendant qu'elle joue sur la Switch
(« télécommande Spotify Connect ») : le moteur repris de Switify ne le fait plus, parce que les serveurs de Spotify ont
fermé l'ancien canal qu'il utilisait. Le téléphone sert donc **seulement à la première connexion** ; on choisit la musique
dans le panneau de Brume. NXpotify (licence MIT) a réécrit ce canal moderne : c'est la piste pour l'ajouter plus tard.

---

## Partie technique

### Recherche (2026-11-23)

| Projet | Licence | Moteur | Arrière-plan | Télécommande depuis le téléphone |
|---|---|---|---|---|
| **Switify** (DomazinUS / HiroshiYamauchi), NRO 0.7.1 + overlay/service 0.5.0, dernier commit `e4b8661` (2026-08-20) | appli + moteur **GPL-3.0-or-later**, overlay Ultrahand **GPL-2.0-only**, serveur IPC Beer-Ware | **cspot** modifié (GPL-3.0-or-later) + bell | **oui** : programme `0x4200000000535754`, pool Applet, lancé à la demande par l'overlay via `pm:shell` | **non** : `SpircHandler(ctx, false)`, l'ancien canal SPIRC est fermé par les serveurs de Spotify ; le téléphone ne sert qu'à l'appairage zeroconf (et seulement dans le NRO) |
| **NXpotify** (etonedemid) 2.0.0 | **MIT** | maison (AP, Shannon, TLS 1.3, **dealer / Connect State API v1**, zeroconf) ≈ 8 300 lignes | non (appli plein écran) | **oui** (Connect State) |
| sys-tune streaming | GPL-2 | — | oui | pilotage par l'API web |
| librespot | MIT | Rust | — | oui (référence) |

Switify en détail (lu dans le code) :
- **Service** : `phase2/sysmodule` (CMake + toolchain Switch), process sans applet (`AppletType_None`), cœur 3, **pool
  Applet** (`pool_partition: 1`, pour ne pas prendre la mémoire système dont l'émulateur N64 NSO a besoin), tas fixe
  **4 Mo**, image ≈ 5,5 Mo, 2 sessions BSD TCP seulement, `audout` ouvert au premier son, Vorbis seul.
- **Démarrage** : pas de `boot2.flag` ; un client lance `pmshellLaunchProgram(0, {tid, NcmStorageId_None})`, l'arrête par
  `pmshellTerminateProgram`. Le service écrit une ligne « pass… <pid> » dans un fichier quand son nom est enregistré et
  qu'il répond à son propre ping.
- **Protocole** : service CMIF nommé (`switify`), ABI C figée (`phase2/common/include/switify/ipc_protocol.h`) :
  version 1.2, statut de 216 octets (état du service, lecture, position, durée, volume ‰, file, titre / artiste / album),
  commandes lecture / pause / suivant / précédent / volume / seek / aléatoire / répétition / qualité, et navigation
  (Accueil, Bibliothèque, Recherche, Ouvrir, Retour, Jouer, Playlist courante, File) par « générations » de pages de
  32 éléments au plus. L'adresse de la pochette est écrite dans un fichier (`current-artwork-url`).
- **Problèmes connus** : comptes Premium récents refusés (« audio key », côté Spotify) ; service bloqué en « Starting »
  faute de mémoire système (consoles chargées en sysmodules / overlays) ; l'appli et le service jouent deux flux s'ils
  tournent ensemble.

### Le choix : (b) notre propre service, construit à partir des parties GPL-3 de Switify

Comparé à (a) « piloter le service de Switify installé à côté » :

- **(a) obligeait à ouvrir l'appli Switify** (en mode titre, en maintenant R) pour l'appairage : le service de Switify
  refuse d'appairer lui-même. Étape manuelle = contraire à « universel et léger » (le cousin).
- **(b)** = le **même moteur éprouvé** (mêmes réglages mémoire, mêmes correctifs), avec trois changements : il **appaire
  lui-même** (nom **BrumeNX**), il a **son propre identifiant** (pas de conflit si Switify est installé), ses fichiers dans
  `sdmc:/brume/spotify/`. Rien à installer à part, il est dans le paquet Brume. Le protocole est **inchangé** (même ABI) :
  le menu inclut l'en-tête du service tel quel.
- Licences : Brume est GPL-3 ; Switify (appli + moteur) et cspot sont GPL-3.0-or-later : on peut les reprendre en les
  créditant. **L'overlay de Switify (GPL-2.0-only) n'est pas repris.**

### Les fichiers

| Où | Quoi |
|---|---|
| `musique/switify/` | la copie modifiée (code repris, licences, notices) ; changements listés dans `MODIFICATIONS-BRUME.txt` |
| `musique/switify/phase2/sysmodule/brume-musique.json` | le NPDM : programme `0x420000000042524D`, service `brm:spot`, pool Applet, cœur 3 |
| `musique/build.sh` | compile le service (WSL archlinux) → `builds/brume/musique/atmosphere/contents/420000000042524D/exefs.nsp` |
| `scripts/package.sh` | ajoute ce dossier au paquet s'il a été compilé |
| `src/Spotify.cpp/.hpp` | le menu : démarrer / relier / arrêter le service, le statut, le panneau, la ligne de la barre, la pochette |
| `shaders/ui_fsh.glsl` | `binding = 5` (`uPicture`) et le mode 32 (une image en carré arrondi) pour la pochette ; le 4 et le 31 sont ceux de la page Amis |
| `src/Audio.cpp` | `audio::Yield` : la nappe s'efface pendant que Spotify joue |
| `src/Settings.cpp` | Réglages > Son > « Spotify » (oui / non) et « Lecteur Spotify » |
| `src/Covers.cpp`, `src/Nextendo.cpp` | attendent la fin du téléchargement d'une pochette (une seule connexion à la fois) |

Changements du service (voir `MODIFICATIONS-BRUME.txt`) : nom / identifiant / dossier ; `setInteractiveAuthentication(true)`
et `DEVICE_NAME = "BrumeNX"` ; tant qu'aucun compte n'est enregistré, le service démarre avec un profil réseau qui permet
le mDNS (tampons UDP, 4 sessions BSD), sinon le profil léger d'origine (TCP seul, 2 sessions) ; il copie une fois
`sdmc:/config/Switify/credentials.json` s'il existe. Compilé ici : **0 avertissement, `exefs.nsp` = 1 700 147 octets**.

### Le menu (src/Spotify.cpp)

- **Démarrage** : `sdmc:/brume/spotify/brume.ini` (`allume=1`) ; si allumé, le service est lancé 3 s après le démarrage
  du menu (pas d'à-coup). Deux essais au plus, puis « Le lecteur n'a pas pu démarrer » (+ Réessayer).
- **Relier sans bloquer** : `sm` fait **attendre** un client qui demande un nom pas encore enregistré. Le menu ne demande
  donc `brm:spot` qu'après avoir vu, dans `background-ready.log`, la ligne « pass » écrite **par le process en cours**
  (son pid, via `pmdmntGetProcessId`). Version d'ABI vérifiée (majeure 1), taille / version du statut vérifiées.
- **Statut** : 4 fois par seconde panneau ouvert, 2 fois sinon, **seulement quand le menu est à l'écran** (le loader
  n'appelle pas le menu pendant un jeu). Textes recopiés en UTF-8 valide, bornés ; durée et volume bornés. Une erreur de
  session = déconnexion propre, reconnexion au tour suivant ; une commande refusée (module 421) ne coupe rien.
- **Pochette** : seulement panneau ouvert, une par morceau, uniquement `https://i.scdn.co/image/<hex>` (lu dans
  `current-artwork-url`, vérifié), version 300 px demandée, téléchargée par le TLS de la console
  (`nextendo::Https`), réponse ≤ 160 Ko, décodée en 128 × 128, compressée en BC1 (**8 Ko** sur le GPU, réservés au
  démarrage) ; tout ça dans un fil de travail (`Worker`), mémoire temporaire ≈ 230 Ko libérée aussitôt.
- **Arrêt** : « Éteindre Spotify » ou le réglage → `pmshellTerminateProgram` ; à la sortie du menu, la session se ferme,
  la musique continue.
- **Aperçu PC** (uisim) : un faux service (`demo spotify <n>` : 0 éteint, 1 première connexion, 2 en lecture, 3 en pause,
  4 refusé, 5 non installé, 6 démarrage), script `tools/uisim/scripts/spotify.txt`.

### L'API pour Brume Overlay (le volet HOME)

Rien à inventer : **Brume Overlay parle directement au service**, comme le menu (le service accepte 4 sessions).

1. **Spotify allumé ?** Lire `sdmc:/brume/spotify/brume.ini` (`allume=1`). Éteint : ne rien afficher, ne rien lancer.
2. **Le service tourne ?** `pmdmntGetProcessId(&pid, 0x420000000042524D)`. Ne **jamais** le lancer depuis l'overlay
   (c'est le menu qui décide) ; s'il ne tourne pas, afficher « Spotify : ouvre le menu ».
3. **Prêt ?** Première ligne de `sdmc:/brume/spotify/background-ready.log` = `pass… <pid en hex>` du même pid ; seulement
   alors `smGetService(&s, "brm:spot")` (sinon `sm` bloque l'overlay — **danger pour le menu Power**, à faire hors du fil
   principal de toute façon).
4. **Protocole** : `musique/switify/phase2/common/include/switify/ipc_protocol.h` (à inclure tel quel). Utile pour le volet :
   `GetApiVersion` (0, `u32`, majeure = 1), `GetStatus` (2 → `SwitifyIpcStatus`, 216 octets : vérifier `struct_size` et
   `abi_version`), `PlayPause` (10), `Next` (13), `Previous` (14), `SetVolume` (15, `u32` en ‰). `service_state` :
   0 démarrage, 1 sans compte, 2 appairage, 3 connexion, 4 prêt, 5 erreur ; `playback_state` : 0 arrêt, 1 chargement,
   2 lecture, 3 pause ; `flags` bit 6 = un morceau est chargé.
5. **Pochette** (facultatif) : `sdmc:/brume/spotify/current-artwork-url` (une ligne, n'accepter que
   `https://i.scdn.co/image/` + hexadécimal).

### Mémoire et coût

- **Menu** : code ≈ 35 Ko ; données fixes ≈ 17 Ko (8 Ko de pochette en attente, 32 éléments de page de 216 octets) ;
  GPU : 8 Ko ; temporaire pendant un téléchargement ≈ 230 Ko. Rien chargé au démarrage du menu.
- **Service** : ≈ 5,5 Mo dans le **pool Applet** (pas dans le tas de 96 Mo du menu, pas dans le pool système), 0 quand
  Spotify est éteint. Pendant un jeu : décodage Vorbis + flux réseau sur le cœur 3 (celui du système), comme Switify.

### Risques

- **Spotify peut casser les lecteurs non officiels** à tout moment (protocole non documenté ; leurs règles interdisent de
  contourner leurs technologies). Pas de vague de bannissements connue. Si ça casse : on suit les corrections de Switify /
  cspot.
- **Comptes Premium refusés** (« audio key ») : côté Spotify, impossible à corriger chez nous.
- **Mémoire système** : sur une console chargée (sys-patch, sys-clk, nx-ovlloader, overlays), le service peut ne pas
  démarrer (comme Switify). Le panneau le dit et propose de réessayer.
- **Appairage** : voir la section suivante (v99.1).

### L'appairage (v99.1, après le test de la v99 sur la console)

**Le test** : le service démarrait, son serveur de connexion aussi, mais `background.log` disait seulement
`E MDNSService.cpp:97: cannot start mDNS listener for BrumeNX` : le téléphone ne pouvait pas voir BrumeNX.

**La cause (lue dans le code, pas encore confirmée sur la console)** :
- `mdnsd_start()` a rendu `NULL` sans rien dire : sur Switch, le petit répondeur mDNS (mdnssvc) **n'écrivait aucune
  erreur** (son journal était coupé exprès), donc impossible de savoir quelle étape avait échoué.
- L'adresse de la console venait de `gethostid()` de libnx, qui rend `INADDR_LOOPBACK` **dans l'ordre de l'hôte**
  (0x7F000001) quand l'appel à nifm échoue, alors que bell ne reconnaissait que l'ordre réseau. Dans un programme
  d'arrière-plan sans applet, un échec de `nifm:u` passait donc inaperçu : le service continuait avec « 1.0.0.127 », et
  `IP_MULTICAST_IF` (une étape alors fatale) échouait → pas de répondeur. C'est la cause la plus probable : tout le
  reste du code est le même que dans l'appli Switify, où l'appairage marche.
- Le profil réseau d'appairage était bien appliqué : `socketInitialize` est appelé dans `__appInit` avec ce profil (la
  ligne « Compatibility profile … 2 BSD sessions » était un texte fixe, trompeur). La ligne écrit maintenant les vraies
  valeurs et la réponse de `socketInitialize`.

**La correction, dans le service** (`vendor/cspot/bell/main/platform/nx/MDNSService.cpp`, `external/mdnssvc/mdnsd.c`) :
- l'adresse est cherchée par `nifm:s` (fait pour les programmes système), puis `nifm:u`, puis par une prise UDP
  « connectée » à une adresse publique (rien n'est envoyé) dont on lit l'adresse locale ; les deux formes de 127.x sont
  refusées ; le journal dit l'adresse trouvée et comment ;
- chaque étape du répondeur écrit son échec dans `background.log`, avec errno (un crochet vers le journal du service,
  qui est protégé par un verrou) ; seules les étapes indispensables sont fatales (prise, `bind` UDP 5353, entrée dans le
  groupe 224.0.0.251 — essayée sur l'adresse de la console, puis sur toutes les interfaces) ; `IP_MULTICAST_IF`, TTL et
  boucle ne bloquent plus ; la prise n'est plus perdue en cas d'échec (elle gardait le port 5353) ;
- le serveur de connexion écrit chaque requête reçue (`zeroconf: GET /spotify_info …`, `zeroconf: the phone sent its
  account`) ;
- le service écrit `sdmc:/brume/spotify/mdns-etat.txt` : `ok BrumeNX` ou `echec adresse` / `echec responder`.

**Le secours, dans le menu** (`src/Spotify.cpp`, `namespace mdns`) : si le service écrit `echec…` (ou n'écrit rien
pendant 6 s, cas de l'ancien service v99), **le menu annonce BrumeNX lui-même** tant que l'écran « Première connexion »
est ouvert. Le menu est une applet complète avec le réseau ; il répond aux questions mDNS des téléphones pour
`_spotify-connect._tcp.local` (PTR, SRV → port **7864**, TXT `VERSION=1.0`, `CPath=/spotify_info`, `Stack=SP`, A = l'adresse
de la console), et répète l'annonce (tout de suite, à 1 s, 2 s, puis toutes les 10 s). Le téléphone se connecte ensuite
**au serveur du service** (TCP 7864), qui reçoit et déchiffre le compte comme avant, et l'enregistre. Rien à faire pour
l'utilisateur ; l'écran dit en vert « BrumeNX est annoncé sur ton Wi-Fi par la console ». L'annonce s'arrête (avec un
« au revoir », TTL 0) dès que l'écran se ferme ou que le compte est arrivé ; pendant ce temps les jaquettes, Nextendo et les
amis attendent (une seule connexion à la fois). Journal : lignes `spotify : mdns (menu) : …` dans `brume/perf.log`.

**Pas encore vérifié** : tout cela sur la console. À lire après le test : `background.log` (lignes `MDNS`/`mdns:` et
`zeroconf:`), `mdns-etat.txt`, et `perf.log` (`spotify : …`).

### Ce qui reste à faire

1. **Tester sur la Switch** : copier `builds/brume/musique/atmosphere/...` et le menu, redémarrer, allumer Spotify,
   appairer avec BrumeNX, choisir une playlist, lancer un jeu (la musique continue ?), revenir (le panneau suit ?),
   éteindre (le process disparaît ?). Lire `sdmc:/brume/spotify/background.log` et `brume/perf.log` si besoin.
2. Brume Overlay : les contrôles dans le volet HOME (API ci-dessus) — autre tâche.
3. **Télécommande depuis le téléphone** (Connect State) : greffer le « dealer » de NXpotify (MIT) dans le service.
4. Quelques titres de pages du service sont en anglais (sections de l'Accueil, « Next songs ») : à traduire dans le
   service si besoin. Les caractères absents des polices de Brume (ex. « ó ») ne s'affichent pas.
