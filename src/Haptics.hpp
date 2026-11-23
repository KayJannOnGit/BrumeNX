// Brume - the Joy-Cons' HD rumble used like Apple's Taptic Engine (v29, Yass): tiny crisp « clicks » and soft
// « thumps », never strong (it would be annoying), on the handheld rails and on a detached pair of Joy-Cons.
// Off with Réglages > Son > Vibration HD (on by default). Nothing runs while a game is in front (the loader does not
// call the menu then) and Stop() silences the motors before a game starts or the console sleeps.
#pragma once

namespace brume::haptic {

    void Init();
    void SetEnabled(bool on);
    bool Enabled();
    void Tap(float strength = 1.0f);      // a crisp click: high band, ~18 ms (a cube touched, a choice confirmed)
    void Tick(float strength = 1.0f);     // the lightest click: moving in a list, a tab
    void Thump(float strength = 1.0f);    // a soft low knock: a console landing, opening something, a page
    void Swell(float strength, float seconds);   // a slow low rise and fall (the big bang)
    // v55 (IDEES #7, « vibration matière »: with the eyes closed you know what you touch)
    void Glass(float strength = 1.0f);    // a water cube / glass: the shortest, highest « tic »
    void Plastic(float strength = 1.0f);  // a game case: a duller, rounder « clac »
    void Purr(float strength, float hz);  // a disc spinning: a soft continuous purr (call it every frame while it lasts)
    void Update(float dt);                // once per frame: sends the current values, ends the pulses
    void Stop();                          // motors off now

}
