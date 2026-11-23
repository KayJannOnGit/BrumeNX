// Brume - v64/v65: the languages (Yass: « la commu est majoritairement anglophone »).
//
// Brume is written in French; every text the menu draws goes through T() when it is drawn (Ui::Width and
// Ui::TextCentered do it themselves, the glyph-by-glyph loops call it): when Brume speaks English, the French text is
// found in a table (LangTable.inc, from tools/lang/en.py) and its English one is drawn instead. Texts built with
// numbers translate their format (T("%d réglages")); dates and greetings are written for each language.
//
// v65 (Yass: « ça doit marcher universellement, dans tous les cas »): one path for everything - no extra image file, no
// restart (changing the language redraws everything at once), and the BUILD checks that every French text of the menu
// has its English and goes through T() (tools/lang/build.py --check fails the build otherwise).
// Cost: French = nothing (one test); English = one hash + one comparison per drawn text (measured: 18 ns on a PC,
// ~0.2 us on the Switch's CPU, ~150 texts at most on screen = ~0.03 ms of the 16.6 ms frame). Memory: 2 KB.
#pragma once

namespace brume::lang {

    // 0 = automatic (the console's language: French -> French, anything else -> English), 1 = French, 2 = English
    void Init(int choice);
    bool En();
    const char *T(const char *fr);                       // the text to draw (fr itself when French or not in the table)

}
