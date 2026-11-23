// Brume - v95: QR codes (the Discord voice shortcut, the Nextendo sign-in from a phone).
// Project Nayuki's QR Code generator (MIT, src/third_party/qrcodegen.c, unchanged; THIRDPARTY.md), compiled here as C++.
// Small codes only (version 10 at most: 57 x 57 modules), error correction M (or better when it still fits), no allocation.
#pragma once
#include <cstdint>

namespace brume::qr {

    constexpr int kMaxSide = 57;
    // text -> its modules, one byte each (1 = dark), row by row; returns the side (modules), 0 if it does not fit
    int Make(const char *text, uint8_t *modules, int cap);

}
