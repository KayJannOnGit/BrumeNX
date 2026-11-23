// uisim - the few newlib lock types libnx's headers name (only their size matters on the PC: nothing waits here)
#pragma once
#include <stdint.h>
typedef uint32_t _LOCK_T;
typedef struct { _LOCK_T lock; uint32_t thread_tag; uint32_t counter; } _LOCK_RECURSIVE_T;
typedef uint32_t _COND_T;
