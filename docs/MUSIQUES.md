# Tes musiques dans Brume (« Mes musiques »)

Brume peut jouer **tes propres musiques** à la place de sa nappe d'ambiance.

## Le choix

**Réglages → Son → Musique d'ambiance** :

- **La nappe de Brume** : la nappe d'origine, avec ses accords et ses chœurs (par défaut) ;
- **Mes musiques** : tes morceaux, dans le désordre, avec un fondu d'environ 2 s entre deux morceaux ;
- **Désactivée** : pas de musique (les sons de l'interface restent).

**Réglages → Son → Morceau suivant** passe au morceau suivant, en fondu rapide. Ça ne marche qu'avec « Mes musiques ».

Tes musiques suivent l'écran verrouillé comme la nappe : silence dans le noir, puis un peu plus fort à chaque appui,
et tout le volume une fois la console ouverte. Elles s'arrêtent avant un jeu ou une appli, et reprennent au retour
(sur un autre morceau).

## Le format

- **Ogg Vorbis** (`.ogg`), stéréo ou mono, de 8 à 48 kHz. Le mieux : **48 kHz stéréo, environ 112 kb/s**, soit
  environ 1 Mo par minute.
- Un fichier = un morceau. **Le nom du fichier** (sans `.ogg`) est le nom affiché quand tu passes au morceau suivant.
- 64 morceaux au plus. Un fichier abîmé est sauté et noté dans `brume/perf.log`, sans planter le menu.

## Où les mettre

Sur la carte SD : **`sdmc:/brume/musiques/`**. Copie tes `.ogg` dans ce dossier.

## Convertir tes fichiers (sur le PC)

L'outil `tools/music-convert.sh` (ffmpeg, dans WSL) convertit un dossier de FLAC, WAV ou MP3 au bon format. Il règle
aussi le même volume pour tous les morceaux (-20 LUFS) et enlève le numéro de piste du nom :

```bash
sh tools/music-convert.sh <dossier source> <dossier de sortie>
```

Copie ensuite le dossier de sortie dans `sdmc:/brume/musiques/`.

## Mémoire et coût

Un morceau n'est **jamais chargé en entier**. Brume le lit petit à petit pendant qu'il joue : environ 250 ms d'avance.
Pendant que « Mes musiques » joue, ça prend environ 300 Ko de mémoire (environ 450 Ko pendant un fondu) :
- le décodeur, environ 150 Ko par morceau ;
- 96 Ko de tampons audio ;
- 48 Ko pour le fil de lecture.

Tout est rendu dès que tu repasses sur la nappe ou que tu désactives la musique. Le décodage coûte environ 1 % d'un
cœur du processeur, d'après une estimation faite sur le PC.

Tes musiques restent **chez toi**. Elles ne vont jamais dans le dépôt git, comme les sons perso et les logos.

## La page Musique (v76) : un vrai lecteur

La page **Musique** (deuxième page après Apps, avec L / R) est le lecteur de « Mes musiques » :

- **à gauche**, la liste de tes morceaux ; **à droite**, celui qui joue, en grand (le titre et l'artiste écrits dans le
  fichier s'il y en a, sinon le nom du fichier), le temps écoulé et une fine ligne de lumière ;
- **derrière**, un visualiseur qui suit vraiment la musique : **Brume** (la brume respire avec les basses), **Orbes** (un
  anneau de lumières autour du titre), **Spectre** (des colonnes de lumière douces et leur reflet), **Aurore** (des rubans
  de lumière lents) ;
- **A** lecture / pause · **◀ ▶** morceau précédent / suivant · **▲ ▼** la liste (A joue celui choisi) · **X** l'ordre
  (au hasard, en boucle, dans l'ordre) · **Y** le visualiseur · au doigt : toucher le titre = pause, glisser à droite =
  morceau suivant / précédent.

Écouter depuis cette page met « Musique d'ambiance » sur « Mes musiques ». La musique continue quand tu quittes la page.
Le spectre n'est calculé que pendant que la page est à l'écran (rien sinon, et rien pendant un jeu).

Pour mettre tes musiques au bon format (Ogg Vorbis) : `sh tools/music-convert.sh <dossier source> <dossier de sortie>`
sur le PC, puis copie la sortie dans `brume/musiques/` sur la carte SD.
