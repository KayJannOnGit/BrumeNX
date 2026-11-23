// uisim - libnx's crc.h uses the AArch64 CRC instructions; the menu never computes a CRC, so on the PC they return 0
#pragma once
#include <stdint.h>
static inline uint32_t __crc32b(uint32_t a, uint8_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32h(uint32_t a, uint16_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32w(uint32_t a, uint32_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32d(uint32_t a, uint64_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32cb(uint32_t a, uint8_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32ch(uint32_t a, uint16_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32cw(uint32_t a, uint32_t b) { (void)a; (void)b; return 0; }
static inline uint32_t __crc32cd(uint32_t a, uint64_t b) { (void)a; (void)b; return 0; }
