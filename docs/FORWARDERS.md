# Les vrais forwarders — où on en est, et la suite

**But (Yass)** : chaque app, émulateur et port lancé par **son propre titre installé** (icône, ID, toute la mémoire),
au lieu du « donneur » (emprunter un jeu installé en redirigeant son exefs dans `atmosphere/contents/`).

## Fait (v51)
- Les **forwarders créés avec sphaira** (titres `05xxxxxxxxxxx000`) sont reconnus :
  - ils **ne se mélangent plus aux jeux Switch** (case Switch de la roue) ;
  - dans **Apps** et **Ports natifs**, si un homebrew a son forwarder (même nom, sans tenir compte de la casse, des
    espaces ni des « (Europe) » de fin), Brume **lance le forwarder** (vrai titre, toute la mémoire, aucun jeu emprunté) ;
  - un forwarder n'est jamais pris comme « donneur ».
- Pour en créer un : sphaira → onglet des homebrews → le `.nro` → « Installer un forwarder » (libellé exact à vérifier).

## Fait (v58) : le « Brume Launcher » et les émulateurs avec leur jeu
Un forwarder de sphaira lit **deux fichiers de son romfs** : `nextNroPath` (le `.nro` à lancer) et `nextArgv` (ses
arguments). Atmosphère sait remplacer les fichiers du romfs d'un titre (LayeredFS : `atmosphere/contents/<id>/romfs/`).
Juste avant un lancement, Brume écrit donc ces deux fichiers pour **ce** forwarder, le lance, puis les efface au retour
(et au démarrage suivant si la console s'est éteinte entre-temps : `sdmc:/brume/forwarder-arme.txt`).

Ordre essayé pour chaque homebrew, émulateur ou port :
1. **son propre forwarder** (même nom que le `.nro`). Seul : lancé tel quel. Émulateur + jeu : le jeu est écrit dans son romfs ;
2. sinon le **Brume Launcher** (`sdmc:/switch/brume-launcher/brume-launcher.nro`, transformé une fois en forwarder avec sphaira) :
   il lance n'importe quoi, sans emprunter de jeu ;
3. sinon le **donneur** (comme avant).

Sécurités : seulement sur un titre forwarder (`05…`), jamais sur un vrai jeu ; si le forwarder a déjà son propre dossier
`romfs` dans `atmosphere/contents/`, Brume n'y touche pas ; les dossiers ne sont supprimés que s'ils sont vides.

**À vérifier sur la console** : que le chargeur des forwarders de sphaira lit bien nos fichiers (il prend le 1er fichier du
romfs puis son voisin : Brume les écrit dans le même ordre que sphaira).

## La suite
- **Brume fabrique les forwarders lui-même** (comme `owo.cpp` de sphaira, GPL-3 : NCA programme / contrôle / méta
  construites sur la console, en-tête chiffré AES-XTS avec la clé d'en-tête, installation par ncm + enregistrement ns).
  Gros morceau, à faire avec la console sous la main pour tester. Avec le Launcher, ce n'est plus urgent : il suffit
  d'un forwarder par app qu'on veut voir avec **son** icône dans le menu Nintendo.
