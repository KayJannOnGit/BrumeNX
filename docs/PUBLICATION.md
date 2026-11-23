# Avant de publier BrumeNX sur GitHub

Rien n'est publié tant que Yass ne l'a pas décidé.

- **La liste complète du jour J** (préparer, publier, texte de la release, brouillon Reddit, questions ouvertes) :
  **[RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md)**.
- **Le paquet** : `scripts/package.sh <version>` (le zip à copier à la racine de la SD + les installeurs
  `scripts/install/`, qui vérifient chaque fichier et ne suppriment rien).
- **Le dépôt public** : `scripts/snapshot-public.sh <dossier neuf>` fabrique un dépôt git **neuf, à un seul commit**,
  sans l'historique privé (qui contient d'anciens chemins du PC avec le vrai nom de Yass), après avoir tout scanné.
- **Les licences** : `THIRDPARTY.md` (section « Release publique ») et `sources/README.md` (sources exactes des binaires
  tiers du zip : loader qlaunch-ext, bibliothèques, hbloader).

Ce qui ne part jamais (exclu du dépôt et du paquet, vérifié par les deux scripts) : logos officiels, jaquettes
(`sdmc:/brume/jaquettes`), sons perso (`brume/perso`), musiques, jeux, BIOS, clés, `.env`, réglages, journaux,
identifiants Spotify / Nextendo.
