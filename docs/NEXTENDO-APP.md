# Brancher Brume sur Nextendo (et le vocal Discord)

Pour que la page **Amis** de Brume marche, Nextendo doit connaître Brume. C'est **ton** appli, créée avec **ton** compte,
sur leur portail des développeurs. Ça se fait une fois, en 5 minutes, sur ton Mac (ou ton téléphone).

Brume ne garde jamais ton mot de passe : tu te connectes sur la page de Nextendo, et Nextendo donne à Brume une « clé »
(un jeton) qu'il range dans `brume/nextendo.json` sur la carte SD. Ce fichier, ne le partage avec personne.

---

## Étape 1 : ton compte Nextendo prêt

1. Va sur **https://nextendo.network** et connecte-toi.
2. Dans ton compte, vérifie que ton **e-mail est confirmé** et que ton **compte Discord est lié**.
   Sans ces deux choses, le portail refuse de créer une appli (erreur `dev_conditions`).

## Étape 2 : créer l'appli « Brume »

1. Va sur **https://nextendo.network/developers** (connecté).
2. Clique sur le bouton qui crée une nouvelle application.
   (Je n'ai pas pu voir cette page sans compte : son nom exact peut être un peu différent.)
3. Remplis comme ça :

   | Champ | Ce que tu mets |
   |---|---|
   | **Nom** | `Brume` |
   | **Description** | `Menu HOME perso pour Switch : voir ses amis Nextendo et les rejoindre.` |
   | **Type** | **Publique** (public, PKCE, *sans* secret). Surtout **pas** « confidentielle ». |
   | **Adresse de retour** (Redirect URI) | `http://127.0.0.1:8080/callback` |
   | **Permissions** (scopes) | coche **identity**, **friends**, **presence** et **profil** (rien d'autre) |

4. Valide. Le portail te montre le **Client ID** : il commence par `nxc_`. Copie-le.
   (Une appli publique n'a pas de « secret » : c'est normal.)

## Étape 3 : le Client ID sur la carte SD

Branche la Switch sur ton Mac (Hekate → USB Tools → SD Card), puis crée ce fichier :

**`brume/nextendo.ini`** (dans le dossier `brume` à la racine de la carte), avec **tout** ce contenu :

```ini
# Brume - mon appli Nextendo (nextendo.network/developers)
client_id=nxc_COLLE_TON_CLIENT_ID_ICI
```

Remplace `nxc_COLLE_TON_CLIENT_ID_ICI` par ton vrai Client ID (sans espace).

## Étape 4 (en option) : le vocal Discord

La Switch ne peut pas faire le vocal Discord (Discord chiffre tous les appels depuis mars 2026 et n'autorise pas les
applis non officielles, et la console n'a pas de micro). Brume affiche donc un **gros QR code** de ton salon : tu le
scannes avec ton téléphone, et vous parlez depuis le téléphone.

1. Dans Discord, sur ton serveur : **Inviter des gens** → **Modifier le lien d'invitation** → **Expire après : Jamais** →
   copie le lien (`https://discord.gg/...`).
2. Crée ce fichier sur la carte : **`brume/discord.txt`**, avec **tout** ce contenu (une seule ligne) :

```
https://discord.gg/TON_LIEN
```

Brume accepte seulement les liens `discord.gg/...` ou `discord.com/invite/...`.

## Étape 5 : se connecter sur la console

1. Démarre la Switch, déverrouille, puis appuie sur **R** jusqu'à la page **Amis**.
2. Choisis **Se connecter sur la console** (A). Le navigateur de la Switch s'ouvre sur Nextendo.
3. Connecte-toi et **accepte** l'appli Brume. Le navigateur doit se fermer tout seul, puis tu vois
   « Connecté à Nextendo » et tes amis.

**Si le navigateur ne se ferme pas, affiche une erreur, ou ne s'ouvre pas** : ferme-le, puis choisis
**Avec mon téléphone** (ou appuie sur **Y**). Un QR code s'affiche :

1. Scanne-le avec ton téléphone (**le même Wi-Fi** que la console).
2. Sur la page qui s'ouvre, touche **Se connecter à Nextendo**, connecte-toi et accepte.
3. Ton téléphone arrive sur une page qui **ne s'ouvre pas** (son adresse commence par `http://127.0.0.1:8080/callback?code=`) :
   c'est normal. **Copie toute l'adresse** de cette page, reviens sur la page Brume (bouton retour), **colle-la** dans la case
   et touche **Envoyer à la console**.

## Sur la page Amis

- **▲ ▼** : choisir. **A** : l'action affichée en bas (Rejoindre, Accepter, Ajouter…). **X** : refuser une demande.
  **Y** : actualiser.
- **Ajouter un ami** : tape son code ami avec le clavier de la console. Ton propre code est affiché à droite.
- **Rejoindre** : si ton ami joue à un jeu que tu as, Brume le lance (ensuite tu le rejoins dans le jeu).
  Sinon : « Tu n'as pas ce jeu ».
- Brume regarde tes amis **toutes les 2 minutes**, seulement quand le menu est à l'écran (jamais pendant un jeu), et
  te prévient en haut à gauche : « Léo est en ligne », « Léo joue à Mario Kart 8 Deluxe », « Zoé veut être ton ami ».
- **Se déconnecter** (tout en bas de la liste) : Brume oublie ta connexion sur la console.


## Sur la page Profil (ton avatar, en haut à gauche)

Touche ton avatar en haut à gauche (ou monte dans la barre avec ▲ et choisis-le) : ta page **Profil** s'ouvre par-dessus
tout le reste.

- En grand : ton avatar, ton pseudo, ce que tes amis voient (**En ligne**, **Joue à…**, **Invisible**, **Hors ligne**) et
  ton **code ami** (à donner à tes amis : ils le tapent dans **Amis** → **Ajouter un ami**).
- **Temps de jeu** : lu dans le journal de jeu de la console (le profil qui joue) : le total, tes 3 jeux les plus joués,
  le dernier joué.
- **Changer de profil** : qui joue (les jeux démarrent avec ce profil).
- **Image du profil** : **Switch** ou **Nextendo** (◀ ▶). Avec « Nextendo », ta photo Nextendo s'affiche aussi en haut à gauche.
- **Statut pour tes amis** : **Visible** ou **Invisible** (◀ ▶). Ça demande la permission **profil** : si tu t'étais connecté
  avant (sans elle), Brume écrit « Reconnecte-toi » : déconnecte-toi puis reconnecte-toi.
- **Se connecter / Se déconnecter de Nextendo**, et **Gérer sur la Switch** (icône, pseudo, nouvel utilisateur : les écrans
  de la Switch).
- Ton choix d'image est rangé dans `brume/profil.ini` sur la carte. Brume ne modifie jamais les comptes de la console.

(Pas de QR code « ajouter en ami » : Nextendo n'a pas de lien pour ça, vérifié dans leur doc le 2026-11-23.)

## Si ça coince

| Ce que tu vois | Ce qu'il faut faire |
|---|---|
| « Il manque l'identifiant de ton appli Nextendo » | `brume/nextendo.ini` absent ou mal écrit (étape 3). |
| « Nextendo a refusé la connexion : vérifie le client_id et l'adresse de retour » | Le Client ID est faux, l'appli n'est pas **publique**, ou l'adresse de retour n'est pas exactement `http://127.0.0.1:8080/callback`. |
| « Le temps est écoulé (5 minutes) » | Recommence : le petit serveur de la console s'arrête au bout de 5 minutes. |
| « Le port 8080 de la console est déjà pris » | Un autre programme (homebrew réseau) utilise ce port : redémarre la console. |
| « Nextendo : reconnecte-toi (page Amis) » | Ta connexion a expiré (30 jours sans utiliser Brume) ou tu as retiré l'accès sur ton compte : reconnecte-toi. |
| « Ton appli Nextendo refuse la permission « profil » » | Sur le portail, coche aussi **profil** dans les permissions de ton appli (étape 2), puis reconnecte-toi. |

Pour tout effacer : supprime `brume/nextendo.json` sur la carte (ou « Se déconnecter »), et retire l'appli dans ton compte
Nextendo si tu veux.

---

## Pour info (technique)

- Nextendo n'a **pas** de connexion « code appareil » (vérifié le 2026-11-23 : rien dans leur doc, l'adresse
  `/api/oauth/device_authorization` répond 404). Brume utilise donc OAuth 2.0 **PKCE** avec l'adresse de retour
  `http://127.0.0.1:8080/callback`, que Nextendo accepte pour les applis publiques ; un tout petit serveur web tourne sur la
  console pendant la connexion (5 minutes au plus).
- Le navigateur utilisé est celui des portails Wi-Fi de la Switch (« WifiWebAuth ») : le navigateur « normal » ne s'ouvre
  qu'avec un jeu. **Pas encore testé sur la console** : il faut vérifier qu'il s'ouvre depuis le menu HOME et que la page
  `127.0.0.1:8080` de Brume lui répond. Sinon, le téléphone prend le relais.
- Jetons : l'accès dure 1 heure, il est renouvelé tout seul avec le jeton de rafraîchissement (30 jours). Rien de ça n'est
  écrit dans les journaux.
- La permission **profil** (ajoutée pour la page Profil) ne sert qu'à `GET` / `POST /api/appear-offline` (Invisible /
  Visible). Brume ne touche ni à ton pseudo, ni à ta photo, ni à ta bannière Nextendo.
- Brume ne touche à rien de Prelude, du DNS, des certificats ni de `atmosphere/hosts` : le blocage des serveurs Nintendo ne
  bouge pas.
