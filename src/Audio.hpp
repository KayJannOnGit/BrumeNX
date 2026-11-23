// Brume - the sound. Every sound is RECORDED from the mockup on the PC
// (tools/render-sounds.html: the mockup's own sound functions, same reverb),
// copied to sdmc:/brume/sons/, and played back here with the audio renderer (audren).
// Notes that the mockup plays at many pitches (bells, the consoles' « plink », the
// movement sound...) are recorded once at a reference pitch and replayed faster or
// slower: a bell into a linear reverb is exactly the sum of its notes.
#pragma once

namespace brume::audio {

    enum Sound {
        Wake, Press1, Press2, BigBang, Sigh, Chime70, Chime81, Chime86,           // lock screen (v28: Bb maj7 notes)
        BellW60, BellW74, BellS60, BellS74, Plink70, Plink84, PlinkH70, PlinkH84,  // the wheel's kit
        Nav76, Whoosh, ThockSnd, Tick2400, Tick1700, Low06, Low09, Low11, Low18, SwishCam, Dive, Deplacement,
        Swell0, Swell1, Swell2, Swell3, Swell4, Swell5, Swell6, Swell7,             // v28: the choir of each console's chord
        CrystalSnd, PageR, PageL,                                                   // v29: a cube touched, the pages
        Unlock12,                                                                   // v38.1: the unlock of the mockup v12 (hold A)
        // v78: the popups (tools/synth_popups.py, our own, each ONE note recorded at a reference pitch so the popup plays it
        // on a note of the chord sounding now): it opens, closes, the focus moves, OK, a deletion's « Oui » arms, the
        // deletion itself (a soft low thud), an error. Optional: a missing file = the older sounds instead (Has)
        PopOpen, PopClose, PopMove, PopOk, PopArmed, PopThud, PopError,
        // v80: a launch and the way back (tools/synth_entree.py, our own; optional: missing = the old dive sound / nothing).
        // The air (no pitch) is played as it is, in time with the picture; the tones are recorded on D, played in the key
        EntreePlongee, EntreeSalon, EntreeMonte, EntreeDescend, Sortie, SortieTon,
        kSoundCount };

    bool Init();                         // false = silent menu (missing files, no renderer); see Error()
    const char *Error();
    void Shutdown();

    // pan -1 (left) .. 1 (right) like the mockup's StereoPanner; pitch = playback speed; delay in seconds
    void Play(Sound s, float pan = 0.0f, float vol = 1.0f, float pitch = 1.0f, float delay = 0.0f);
    void PlaySweep(Sound s, float panFrom, float panTo, float dur, float vol = 1.0f);   // a sound crossing the stage
    bool Has(Sound s);                   // v78: its file was loaded (the popups' sounds are optional)

    // The mockup's sound functions, same arguments:
    void Bell(int midi, float vel, bool wet, float delay = 0.0f);   // bell(m, vel, ~3 s, delay, wet ? wetBus : sfxBus)
    void PopNote(int midi, float pan, bool hero);                   // popNote: a console arrives
    void Nav(int midi, float pan);                                  // navSound (midi already folded into 72..79)
    void Thock(float pan, float pitch, float vel);                  // thock
    void Crystal(float pan, float strength, int midi);              // v29.2: a cube touched, singing that note (crystalTouch)
    // v29.2 (Yass: « harmonise every sound ») - the key: the chosen console's root vs D (-6..+6 semitones). The lock
    // screen (its loops, presses, big bang, sigh), the « tok » and the page sound are played in it, so the unlock always
    // resolves on the console's chord. The harmony: the 4 notes sounding now; every « ding » takes one of them.
    void SetKey(int semis);
    int  KeySemis();
    void SetHarmony(const int *notes4);
    int  HarmonyNote(int k, int lo, int hi);                        // note k (0..3) of the chord, moved into [lo, hi]
    void SoftLow(int midi, float vel, float dur);                   // softLow (dur .6 / .9 / 1.1 / 1.8)

    // The soundscape (mockup: setMusicStage + setChord): 0 = silence in the dark, 1 / 2 = the lock
    // screen's first levels, 3 = the whole soundscape on the selected console's chord.
    void SetMusic(int stage);
    void SetChord(int console, float glide, float semis = 0);   // setChord(CONSOLES[i], glide); glide 4 = turning: the chord slides (semis = root move)
    void Duck(float level, float tc);           // duck(level, tc): the soundscape only
    void Silence();                             // before sleep: everything off at once
    void SetSfxOn(bool on);                     // v43 (Réglages): the interface sounds
    void SetMusicOn(bool on);                   // v43 (Réglages): the soundscape and the choirs
    void Yield(bool on);                        // v98: the nappe (and its choirs) steps aside while Spotify plays (~1.5 s glide)
    // v80 (Yass: « le son se coupe d'un coup → un vrai son d'entrée et de sortie, très stylé, pour tous les logiciels et jeux »)
    // A launch: Depart() as the screen starts to go (everything playing, and every sound from now on, fades out into silence
    // with the soundscape - but the entry sound), EntrySound(), then the game is started kEntryLen later, once it is silent:
    // Hush() just before (everything stopped, the nappe paused, not a sound until Arrive: zero cost in a
    // game). Back in the menu: Arrive() + ExitSound() when the light comes back (without Arrive: by itself after 0.6 s).
    constexpr float kEntryLen = 1.25f;          // the entry sounds have gone into silence by then (tools/synth_entree.py)
    void Depart(float tc = 0.3f);
    void EntrySound(bool portable, float pan);  // portable: the dive into its screen; else a home console turns, then black
    void Hush();
    void Arrive(float tc = 1.3f);               // the soundscape comes back (tc), the nappe goes on where it was
    void ExitSound(float pan);                  // the way back (a breath drawn in, the light blooming in the chord)
    void SetUnlock(int style);                  // v59 (Réglages > Brume > Déverrouillage): 0 heartbeat (unlock12), 1 Aurore
    void Update(float dt);                      // once per frame: delays, ramps, sweeps, renderer update

}
