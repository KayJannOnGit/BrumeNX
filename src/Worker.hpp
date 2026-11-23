// Brume - v90 (perf: « 60 FPS constants, partout »): ONE background job at a time, off the frame.
// The SD card, a JPEG decode, a BC1 encode take 5 to 250 ms: done inside a frame they were the hitches of perf.log
// (the Apps page 155-167 ms, the Album 220-240 ms, a game cover 30-75 ms). A job runs on its own thread (made for
// the job, closed when its result is collected: nothing stays alive while a game runs - zero cost in game), at a
// lower priority than the menu (it only uses the CPU the frame leaves). If the thread cannot be made (no memory,
// the PC preview), the job simply runs at once, inline: the old behaviour, never a lost job.
// Rules for a job: it touches only its own data until it is collected (Done), never the GPU, never audio.
#pragma once
#include <switch.h>

namespace brume {

    struct Worker {
        Thread t;
        bool   thread = false;              // a thread was made for the current job (to close at Done)
        int    state = 0;                   // 0 idle, 1 running, 2 done (atomics: the job writes 2 when it ends)
        void (*fn)(void *) = nullptr;
        void  *arg = nullptr;

        static void Run(void *w) {
            Worker *me = (Worker *)w;
            me->fn(me->arg);
            __atomic_store_n(&me->state, 2, __ATOMIC_RELEASE);
        }
        bool Idle() const { return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 0; }
        bool Busy() const { return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 1; }
        bool Finished() const { return __atomic_load_n(&state, __ATOMIC_ACQUIRE) == 2; }
        // false: a job is still there (running, or done and not collected yet)
        bool Start(void (*f)(void *), void *a, size_t stack = 0x10000) {
            if (!Idle()) return false;
            fn = f; arg = a;
            __atomic_store_n(&state, 1, __ATOMIC_RELEASE);
            thread = R_SUCCEEDED(threadCreate(&t, Run, this, nullptr, stack, 0x30, -2));   // (a low priority: never in the way of the frame or the sound)
            if (thread && R_FAILED(threadStart(&t))) { threadClose(&t); thread = false; }
            if (!thread) Run(this);           // no thread: at once (the result is there when Start returns)
            return true;
        }
        // true once per job, when it is over: the thread is closed, the worker is free again
        bool Done() {
            if (!Finished()) return false;
            if (thread) { threadWaitForExit(&t); threadClose(&t); thread = false; }
            __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
            return true;
        }
        void Wait() {                         // (shutdown, sleep: the job ends, its result is dropped)
            if (thread) { threadWaitForExit(&t); threadClose(&t); thread = false; }
            __atomic_store_n(&state, 0, __ATOMIC_RELEASE);
        }
    };

} // namespace brume
