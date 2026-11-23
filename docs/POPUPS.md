# Les pop-ups de Brume (v78)

> Demande de Yass (2026-11-17) : « les menus avec fond, ou même les notifs, on voit pas leur fond… c'est une brume opaque mais
> en général elle est carrée… J'veux pas non plus un fond Liquid Glass… mettre en place des pop-ups là où c'est pertinent…
> la double confirmation de suppression… bouton oui qui attend avant de s'allumer en rouge… crée des sons spécifiques…
> anime absolument tout. »

## 1. Ce que font les consoles (recherche rapide)

### Quand une console ouvre une pop-up

| Situation | Switch | PS4 / PS5 | PS3 (XMB) |
|---|---|---|---|
| **Supprimer un logiciel / des données** | « Supprimer le logiciel » → confirmation « Supprimer » (Gestion des données) | Options → Supprimer → **OK** (PS5 : Stockage → sélection → Supprimer → confirmer) | Triangle → Supprimer → « Oui / Non » |
| **Archiver** | confirmation « Archiver » (les sauvegardes restent) | — | — |
| **Lancer un jeu alors qu'un autre est suspendu** | « Fermer le logiciel ? Ce qui n'est pas sauvegardé sera perdu » → **Fermer** | « L'application va être fermée » → OK | (un seul jeu à la fois) |
| **Réglage qui demande un redémarrage** (langue, région…) | message « La console va redémarrer » | « Redémarrer maintenant ? » | « Le système va redémarrer » |
| **Erreur** (lancement, réseau, page système) | écran d'erreur avec un **code 2XXX-YYYY** et OK | fenêtre avec **code CE-XXXXX-X** et OK | code 8001XXXX |
| **Supprimer un utilisateur** | double confirmation (on coche « supprimer », puis on confirme) | confirmation + avertissement | confirmation |
| **Alimentation** | menu de l'appui long (Veille / Options d'alimentation → Éteindre / Redémarrer), sans confirmation de plus | Éteindre / Redémarrer directement | idem |

Le loader de qlaunch-ext le dit lui-même dans son code : avant de fermer un jeu pour en lancer un autre, *« asking is for the menu »*
(c'est au menu de demander). Son menu d'exemple ouvre justement une pop-up « fermer le jeu » quand on choisit un autre jeu,
et **reprend** le jeu (sans le fermer) quand on rechoisit celui qui est suspendu. Brume ne faisait ni l'un ni l'autre.

### Comment elles s'animent

- **Fond** : tout l'écran derrière s'assombrit (PS4, Switch) ; la PS5 floute en plus ce qu'il y a derrière.
- **Entrée** : la fenêtre apparaît en fondu avec un léger agrandissement (≈ 92 → 100 %), en 150-300 ms ; **sortie** plus rapide.
- **Focus** : une surbrillance qui glisse d'un bouton à l'autre, un son court à chaque déplacement.
- **Boutons** : un ou deux, rarement plus ; « Annuler » à gauche, l'action à droite (PS4, Switch).

### Les actions destructrices (règles communes)

- Le **bouton sûr est sélectionné par défaut** (« Annuler ») ; une action destructrice n'est jamais le choix par défaut
  (règles d'interface de Microsoft / Xbox : *le bouton par défaut doit être la réponse la plus sûre*).
- **B / Échap annule toujours**, et annuler ne déclenche jamais l'action.
- Pour ce qu'on ne peut **pas défaire**, on ajoute de la friction : **maintenir** le bouton (« hold to confirm »,
  ≈ 1 s, 1,5 à 2 s pour l'irréversible) ou un bouton qui **ne s'active qu'après un délai**. La progression doit se voir.
- Le texte dit **ce qui sera perdu** et **ce qui reste** (« Ses sauvegardes restent »).

Sources :
- Nintendo, fermer un logiciel : https://en-americas-support.nintendo.com/app/answers/detail/a_id/22450/~/how-to-start%2Fclose-a-game-or-software-application
  et https://nintendo.com/au/support/articles/how-to-start-close-a-game-or-software-application
- Supprimer un jeu sur PS5 (guides) : https://www.tomsguide.com/how-to/how-to-delete-games-on-ps5 ,
  https://www.androidcentral.com/how-delete-games-your-ps5
- Xbox, dialogues (bouton par défaut / bouton d'annulation) : https://devdocs.xbox.com/reference/system/xgameui/functions/xgameuishowmessagedialogasync
- Xbox Accessibility Guideline 115 (erreurs et actions destructrices) : https://learn.microsoft.com/en-ie/gaming/accessibility/xbox-accessibility-guidelines/115
- Microsoft, dialogues (bouton par défaut = le plus sûr) : https://learn.microsoft.com/windows/apps/design/controls/dialogs-and-flyouts/dialogs
- « Hold to confirm » et délais : https://www.smashingmagazine.com/2024/09/how-manage-dangerous-actions-user-interfaces ,
  https://motion.dev/tutorials/js-hold-to-confirm
- Code de référence lu (pas copié, GPL-2) : `qlaunch-ext/menu/src/widgets/Prompt.cpp` et `menu/src/core/App.cpp` (fermer / reprendre un jeu).

## 2. Où Brume ouvre une pop-up (v78)

| Où | Type | Ce qui se passe |
|---|---|---|
| **Gérer → X (supprimer)** | destructrice | « Annuler » choisi d'abord ; « Supprimer » est **éteint**, une ligne de lumière se remplit dessous depuis le centre (1,5 s), puis il **s'allume en rouge** (son « armé », lueur rouge, petite vibration) et seulement là il peut être choisi. A dessus avant : il tremble, son sourd. Confirmé : un « boum » doux et grave. |
| **Gérer → A (archiver)** | question | « Annuler » choisi d'abord (rien n'est perdu pour de bon : les sauvegardes restent). |
| **Lancer un jeu alors qu'un autre est suspendu** (v89 : aussi **X** sur sa boîte) | destructrice | « Fermer « son nom » ? », « Ce qui n'est pas sauvegardé sera perdu. » ; « Non » d'abord, « Oui » attend puis s'allume en rouge. Oui : le jeu se ferme, **sa boîte vient chercher son disque**, se referme, retourne à sa place, puis l'autre jeu se lance normalement (une autre console : le retour du disque s'entend seulement). **Rechoisir le jeu suspendu le reprend** (`qext_resume_game`) : pas d'animation de boîte, le disque est déjà dedans, juste la plongée. Tant qu'il tourne, **son disque reste dans sa console et sa boîte reste ouverte** (« EN COURS » ; sur la roue, une lumière douce à la fente de la console). |
| **Un jeu qui ne démarre pas** | erreur | « Le jeu n'a pas pu démarrer » + la raison + le **code d'erreur façon Switch** (2XXX-YYYY). Avant : une ligne sous les boîtes que personne ne voyait. |
| **Jeu archivé / fichiers d'un port manquants** | info | une pop-up qui dit pourquoi et quoi faire. (Correction au passage : choisir un jeu archivé **fermait le jeu suspendu** ; plus maintenant.) |
| **Réglages : overlay Brume, langue, région** | question | « Redémarrer maintenant ? » ; « Plus tard » d'abord ; « Redémarrer » = le fondu au noir habituel puis le redémarrage. |
| **Une page système qui ne s'ouvre pas** (Album, Manettes, Internet…) | erreur | avec son code. |

Pas de pop-up pour : l'alimentation (les consoles n'en mettent pas, notre petit panneau suffit), les messages
d'information (ce sont des notifications), un utilisateur à supprimer (Brume ne le fait pas : c'est la page Nintendo).
Candidats pour plus tard : le choix des options dans les Réglages (le petit menu à droite pourrait avoir la même surface),
« Télécharger les jaquettes manquantes ? » (réseau), réinitialiser des réglages.

## 3. La nouvelle « surface » (au lieu du voile carré)

`Ui::Surface` (shader `ui_fsh` mode 30) : une **plaque de brume nocturne**, pas du verre.
- coins en **superellipse** (squircle) : jamais un carré dur, plus doux qu'un arrondi classique ; bord net mais doux (2 px) ;
- dégradé **bleu nuit → presque noir**, la brume floutée de derrière (`uHazeT`, déjà calculée) respire un peu au travers ;
- un **nuage de la couleur d'ambiance** en haut, une **lumière douce sur le bord du haut** (qui s'efface sur les côtés) ;
- une **ombre** sous la plaque et un **halo** de lumière autour (la couleur d'ambiance ; rouge pour une suppression armée,
  rose pour une erreur, vert pour une notification « terminé ») ; un grain très fin contre les bandes du dégradé.

Utilisée par : les notifications, le panneau des tâches, le panneau d'alimentation, le panneau des profils, les pop-ups.
Les panneaux **grandissent depuis leur bouton** (alimentation, marque des tâches, pastille du profil) ; la lumière du
choix **glisse** d'une ligne à l'autre ; les notifications **s'écartent** quand le panneau des profils est ouvert.

## 4. Les sons (originaux, `tools/synth_popups.py`)

Chaque fichier est **une seule note** enregistrée à une hauteur de référence : la pop-up la rejoue **sur une note de
l'accord du moment** (`audio::HarmonyNote`) → tout reste dans l'accord. Fichiers à copier dans `sdmc:/brume/sons/`
(`scripts/package.sh` les prend avec les autres) :

| Fichier | Quand | Note de réf. |
|---|---|---|
| `pop_open.wav` | la pop-up arrive (souffle + note de verre, + une 2e note de l'accord) | Ré5 |
| `pop_close.wav` | B / Annuler / elle repart | Ré4 |
| `pop_move.wav` | le focus change de bouton (note 1 ou 3 de l'accord) | Ré5 |
| `pop_ok.wav` | OK / l'action (joué deux fois : un intervalle qui monte) | Ré5 |
| `pop_armed.wav` | « Supprimer » s'allume en rouge | Ré4 |
| `pop_thud.wav` | la suppression : un « boum » doux et grave | Ré2 |
| `pop_error.wav` | une erreur : « doum-doum » doux | Ré3 |

Ils sont **optionnels** : s'ils manquent, Brume rejoue ses anciens sons (aucun message « son manquant »). Total ≈ 200 Ko
de mémoire (16 kHz, 12 kHz pour les graves).

## 5. Coût

- `ui_fsh` : 32 registres (avant : 25). Sur le Tegra X1 les registres sont alloués par 8 : 25 → 32 de toute façon, donc
  **aucune perte d'occupation** pour les autres pixels de l'interface. ⚠️ Ne pas dépasser 32 en retouchant le mode 30.
- Une pop-up (≈ 350 000 pixels de plaque + marges, ≈ 120 instructions par pixel, 1 lecture de texture) + le fond flouté
  plein écran (mode 25) : **≈ 0,6 à 1 ms de GPU** en portable (307 MHz), seulement pendant qu'elle est à l'écran.
- Panneaux : ≈ 0,2 à 0,4 ms tant qu'ils sont ouverts ; une notification ≈ 0,1 à 0,15 ms.
- Mémoire : aucune texture ajoutée ; ≈ 200 Ko de sons ; le code de la pop-up ≈ 1 Ko de données.
- À **mesurer sur la console** (perf.log) : rien n'a encore été testé sur la Switch.
