// uisim - the real libnx headers on the PC. x86-64 and AArch64 share the same LP64 layout, so the structs are the
// same; only the AArch64 inline assembly is removed, and the clock is driven by the preview (see main.cpp).
#pragma once
// the C library first, untouched (its own __asm__ redirects must stay)
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#ifdef __cplusplus
#include <atomic>
#endif

#define __asm__(...)
#define __volatile__
#define armGetSystemTick armGetSystemTick_libnx
#define armGetSystemTickFreq armGetSystemTickFreq_libnx
#include_next <switch.h>
#undef armGetSystemTick
#undef armGetSystemTickFreq
#undef __asm__
#undef __volatile__

#ifdef __cplusplus
extern "C" {
#endif
unsigned long long sim_tick(void);
#ifdef __cplusplus
}
#endif
static inline u64 armGetSystemTick(void) { return sim_tick(); }
static inline u64 armGetSystemTickFreq(void) { return 19200000ULL; }
#if defined(__x86_64__)
typedef _Float16 __fp16;               // AArch64 half floats (the env map)
#endif
