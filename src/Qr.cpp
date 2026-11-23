// Brume - v95: QR codes. See Qr.hpp.
#include "Qr.hpp"
#include <cstring>

#ifndef NDEBUG
#define NDEBUG 1                         // (its asserts would import __assert_func, which the loader does not export)
#endif
extern "C" {
#include "third_party/qrcodegen.c"
}

namespace brume::qr {

    int Make(const char *text, uint8_t *modules, int cap) {
        if (!text || !modules || strlen(text) > 160) return 0;
        static_assert(kMaxSide == 10 * 4 + 17, "version 10");
        uint8_t code[qrcodegen_BUFFER_LEN_FOR_VERSION(10)], temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
        if (!qrcodegen_encodeText(text, temp, code, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) return 0;
        const int n = qrcodegen_getSize(code);
        if (n <= 0 || n > kMaxSide || n * n > cap) return 0;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) modules[y * n + x] = qrcodegen_getModule(code, x, y) ? 1 : 0;
        return n;
    }

}
