# Brume beta — testers' checklist

Tick what works, report what doesn't (template below). One bug = one report. Thank you! 💙

## Checklist

- [ ] **1. First boot** — Brume starts on its lock screen; 3 presses (any button, or 3 taps) unlock it, with sound.
- [ ] **2. The wheel** — ◀ ▶ (or the stick, or a swipe) turns the consoles; each name shows; it stays smooth
      (note any stutter or freeze, and where).
- [ ] **3. A Switch game** — **A** on *Switch* → the boxes; launch a game, play a minute, press **HOME**: Brume comes back
      quickly; go back into the game; then launch another game (the first one must close cleanly).
- [ ] **4. Sleep** — press **Power** in Brume, wake the console up; do it again while a game runs.
- [ ] **5. Pages** — **L / R** go through Games, Apps, Album, Controllers, Settings; **+** opens Power
      (does *Restart* work?).
- [ ] **6. Settings** — change the theme and the ambient colour (**Settings → Brume**); are they still there after a reboot?
- [ ] **7. Docked** (if you have a dock) — Brume on the TV, controllers work, back to handheld.
- [ ] **8. Optional** — an emulator (expected paths in `GUIDE-COMPLET-FR.md`), the covers download
      (**Settings → Data**), the Brume Launcher forwarder (made with sphaira).
- [ ] **9. Emergency exit** — hold **L + R + −** for 3 s once: you are back on your old menu; then bring Brume back
      (`INSTALL-EN.md`, part 3).

**Please don't (yet):**
- turn on **Brume Overlay (test)** (Settings → Brume): it replaces the HOME panel, the volume bar **and the Power menu**;
  only on our request;
- change the **region** or delete software from Brume unless you mean it: Brume's settings change the console for real
  (on your emuMMC);
- use Brume on a **sysNAND**, ever.

## Bug report template

Copy, fill in, post it where you got the beta, with the files from `INSTALL-EN.md` part 4 (`brume/perf.log` first).

```
Brume version : beta-___ (brume/docs/VERSION.txt)
Console       : V1 / V2 (Mariko) / Lite / OLED      Handheld / Docked
Firmware      : ___      Atmosphère : ___      Hekate : ___
emuMMC        : partition / files                  sLaunch installed too : yes / no
Other things on the card (sys-clk, Tesla/Ultrahand, MissionControl, sys-patch...) : ___
Brume Overlay on : yes / no         Brume language : FR / EN

What I did (steps):
1.
2.
3.
What happened :
What I expected :
How often     : every time / sometimes / once        Time it happened : __:__
Files attached: perf.log / qlaunch-ext-log.txt / crash or fatal report / photo / video
```

---

## Version française (courte)

- [ ] **1.** Brume démarre sur l'écran verrouillé ; 3 appuis (ou 3 touchers) l'ouvrent, avec le son.
- [ ] **2.** ◀ ▶ (stick ou glisser) fait tourner les consoles, le nom s'affiche, c'est fluide (note les saccades).
- [ ] **3.** **A** sur *Switch* → les boîtes ; lance un jeu, **HOME** → Brume revient vite ; retourne au jeu ; puis lance un autre jeu (le premier doit se fermer proprement).
- [ ] **4.** **Power** dans Brume puis réveil ; pareil pendant un jeu.
- [ ] **5.** **L / R** : Jeux, Apps, Album, Manettes, Réglages ; **+** ouvre Alimentation (*Redémarrer* marche ?).
- [ ] **6.** Change le thème et la couleur d'ambiance (**Réglages → Brume**) ; toujours là après un redémarrage ?
- [ ] **7.** Sur la TV (dock) : l'image, les manettes, le retour en portable.
- [ ] **8.** Facultatif : un émulateur, les jaquettes (**Réglages → Données**), le forwarder du Brume Launcher.
- [ ] **9.** **L + R + −** 3 s : retour à l'ancien menu, puis remets Brume (`INSTALL-FR.md`, partie 3).

**Pas encore :** l'**overlay Brume (essai)**, changer la **région** ou supprimer des logiciels sans le vouloir
(les réglages de Brume agissent pour de vrai sur ton emuMMC), et **jamais** sur une sysNAND.
Pour un bug : le modèle ci-dessus (en français si tu préfères) + les fichiers de `INSTALL-FR.md`, partie 4.
