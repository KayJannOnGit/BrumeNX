// Brume - v82: Réglages > Son > Audio Bluetooth, Brume's own page (Yass: « les Réglages doivent vraiment marcher », and total
// independence from Nintendo's menu, which holds the Switch's own Bluetooth audio page). Like the Switch's: the paired
// headphones / speakers, connect or disconnect one, search the ones nearby and pair one, forget a pairing.
// - The console's side is btm:sys (libnx btmsys*, FW 13.0.0+): opened when the page opens, closed when it leaves (Settings
//   follows its column every frame); nothing at all otherwise - so nothing while a game runs.
// - The search runs only while asked, 30 s at most; it is a task of the task centre (« recherche… »), the devices it finds
//   arrive one by one, each with a crystal note in the chord. A connection is followed until it is made (or not: a popup).
// - Pairing = connecting a device found by the search (btm:sys has no separate « add »: ConnectAudioDevice pairs it).
// - The page's rows are drawn by Settings (the same column as every other level: a list floating in the fog, the chosen row
//   in the band of light); this module gives their words and draws the little lights (the search's waves, a device's).
// - Everything the console answers is checked (counts bounded, names cut to valid UTF-8, empty addresses skipped).
// - The PC preview (uisim, BRUME_UISIM): made-up devices instead of btm:sys, so the page can be seen.
#pragma once
#include "Ui.hpp"

namespace brume::btaudio {

    constexpr int kRowsMax = 16;          // a settings column shows 16 rows at most: the search + 15 devices
    struct Row { const char *ico, *name, *desc, *value; };   // name: drawn as it is (a device's); the others: French, translated when drawn

    void Open(float now);                 // the page opens: btm:sys, its two events, the lists (a failure: a popup, a row that says it)
    void Close(float now);                // it leaves: the search stops, the events and the service close
    bool IsOpen();
    void Update(float now);               // once per frame while open: the events, the search, a connection under way
    int  Rows();                          // 1 (the search) + the devices
    Row  RowAt(int i);
    int  Track(int sel);                  // the chosen row follows its device when the list changes (and stays in it)
    void PressA(int row, float now);      // search / stop, connect / disconnect, pair
    void PressX(float now);               // forget the chosen device (a popup first)
    const char *HintA(int row);           // A's word for this row
    bool CanForget(int row);
    void DrawRow(Ui &ui, int row, float x, float y, float a, float now);   // under the row's icon: the search's waves, a device's light

} // namespace brume::btaudio
