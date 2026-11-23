// Brume - v64/v65: the languages. See Lang.hpp.
#include "Lang.hpp"
#include <switch.h>
#include <cstdint>
#include <cstring>

namespace brume::lang {

    namespace {
        struct Pair { const char *fr, *en; };
        const Pair kPairs[] = {
#include "LangTable.inc"
        };
        constexpr int kN = (int)(sizeof kPairs / sizeof kPairs[0]);
        constexpr int kSlots = 2048;                     // open addressing, under half full: 1-2 probes (v83 merge: 537 texts, 4 KB)
        static_assert(kN < kSlots / 2, "the language table needs more slots");
        uint16_t g_slot[kSlots];                         // 0 = empty, else 1 + the pair's index
        bool g_en = false, g_built = false;

        uint32_t Hash(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }
    }

    void Init(int choice) {
        bool en = choice == 2;
        if (choice == 0 && R_SUCCEEDED(setInitialize())) {   // the console's language
            u64 code = 0; SetLanguage l = SetLanguage_FR;
            if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &l))) en = !(l == SetLanguage_FR || l == SetLanguage_FRCA);
            setExit();
        }
        g_en = en;
        if (g_built) return;
        g_built = true;
        for (int i = 0; i < kN; i++) {
            uint32_t s = Hash(kPairs[i].fr) & (kSlots - 1);
            while (g_slot[s]) s = (s + 1) & (kSlots - 1);
            g_slot[s] = (uint16_t)(i + 1);
        }
    }

    bool En() { return g_en; }

    const char *T(const char *fr) {
        if (!g_en || !fr || !fr[0]) return fr;
        for (uint32_t s = Hash(fr) & (kSlots - 1); g_slot[s]; s = (s + 1) & (kSlots - 1))
            if (!strcmp(kPairs[g_slot[s] - 1].fr, fr)) return kPairs[g_slot[s] - 1].en;
        return fr;
    }

}
