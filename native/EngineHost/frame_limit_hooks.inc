/* The iOS display-rate request at Halo's time update (frame_limit.h).
 * Included by overrides.c. The entry is routed here only where engine_hooks.h
 * lists it (ENGINE_IOS_HOOK_ADDRESSES); elsewhere nothing calls the hook and
 * nothing configures it. All guest access is on the engine thread. */
#include "frame_limit.h"
#include <stdatomic.h>
#include <time.h>

#ifndef HALO_FRAME_LIMIT_NOW_NS
/* The same clock QueryPerformanceCounter returns (shims_kernel32.c). */
#define HALO_FRAME_LIMIT_NOW_NS() host_monotonic_ns()
static void frame_limit_sleep_until(uint64_t deadline) {
    for (int tries = 0; tries < 8; tries++) {
        uint64_t now = host_monotonic_ns();
        if (now >= deadline) return;
        uint64_t left = deadline - now;
        struct timespec t = { (time_t)(left / 1000000000ull), (long)(left % 1000000000ull) };
        nanosleep(&t, NULL);
    }
}
#define HALO_FRAME_LIMIT_SLEEP_UNTIL_NS(deadline) frame_limit_sleep_until(deadline)
#endif

static _Atomic int frame_limit_enabled;
static int frame_limit_nested;
static _Atomic uint64_t frame_limit_counts[4 + HALO_FRAME_LIMIT_WHY_COUNT];   /* calls, lifted, waits, wait ns, reasons */
void host_frame_idle_wait(uint64_t start, uint64_t end);   /* shims_kernel32.c */

void host_frame_limit_configure(int lift_throttle) {
    atomic_store_explicit(&frame_limit_enabled, lift_throttle ? 1 : 0, memory_order_relaxed);
}
void host_frame_limit_stats(HaloFrameLimitStats *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->calls = atomic_load_explicit(&frame_limit_counts[0], memory_order_relaxed);
    out->lifted = atomic_load_explicit(&frame_limit_counts[1], memory_order_relaxed);
    out->floor_waits = atomic_load_explicit(&frame_limit_counts[2], memory_order_relaxed);
    out->floor_wait_ns = atomic_load_explicit(&frame_limit_counts[3], memory_order_relaxed);
    for (int i = 0; i < HALO_FRAME_LIMIT_WHY_COUNT; i++)
        out->reasons[i] = atomic_load_explicit(&frame_limit_counts[4 + i], memory_order_relaxed);
    out->enabled = atomic_load_explicit(&frame_limit_enabled, memory_order_relaxed);
}
static double frame_limit_double(uint32_t address) { double v; memcpy(&v, GPTR(address), sizeof v); return v; }
static int64_t frame_limit_int64(uint32_t address) { int64_t v; memcpy(&v, GPTR(address), sizeof v); return v; }

/* 1 when it ran the original itself (throttle lifted for the call); 0 to let
 * the dispatcher run the original unchanged (after the floor guard, when the
 * throttle was already off). */
static int host_frame_limit_dispatch(EngineCPU *cpu, uint32_t address) {
    if (address != 0x004C9F90u || frame_limit_nested) return 0;   /* HALO_FRAME_LIMIT_ENTRY */
    atomic_fetch_add_explicit(&frame_limit_counts[0], 1, memory_order_relaxed);
    const int enabled = atomic_load_explicit(&frame_limit_enabled, memory_order_relaxed);
    const uint8_t throttle = enabled ? G8(HALO_FRAME_LIMIT_THROTTLE) : 0;
    const uint32_t timedemo = enabled ? G32(HALO_FRAME_LIMIT_TIMEDEMO) : 0;
    const uint32_t cinematic_state = enabled ? G32(HALO_FRAME_LIMIT_CINEMATIC) : 0;
    const int cinematic = cinematic_state && G8(cinematic_state + 9u);
    HaloFrameLimitConstants k = { 0, 0, 0, 0 };
    if (enabled && !timedemo && !cinematic) {
        k.cap = frame_limit_double(HALO_FRAME_LIMIT_CAP); k.floor = frame_limit_double(HALO_FRAME_LIMIT_FLOOR);
        k.ceiling = frame_limit_double(HALO_FRAME_LIMIT_CEILING); k.freq = frame_limit_int64(HALO_FRAME_LIMIT_QPC_FREQ);
    }
    int why = HALO_FRAME_LIMIT_WHY_DISABLED;
    int lift = halo_frame_limit_decide(enabled, throttle, timedemo, cinematic, &k, &why);
    atomic_fetch_add_explicit(&frame_limit_counts[4 + why], 1, memory_order_relaxed);
    if (lift == HALO_FRAME_LIMIT_ORIGINAL) return 0;
    /* Never let an unthrottled frame be counted as the dt floor, whether the
     * throttle is lifted here or was already off. */
    uint64_t now = HALO_FRAME_LIMIT_NOW_NS();
    uint64_t deadline = halo_frame_limit_floor_deadline(frame_limit_int64(HALO_FRAME_LIMIT_LAST_QPC), (int64_t)now, k.floor);
    if (deadline) {
        HALO_FRAME_LIMIT_SLEEP_UNTIL_NS(deadline);
        uint64_t end = HALO_FRAME_LIMIT_NOW_NS();
        host_frame_idle_wait(now, end);
        atomic_fetch_add_explicit(&frame_limit_counts[2], 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&frame_limit_counts[3], end - now, memory_order_relaxed);
    }
    if (lift == HALO_FRAME_LIMIT_GUARD) return 0;   /* the saved byte is already 0: the original as it is */
    S8(HALO_FRAME_LIMIT_THROTTLE, 0);
    frame_limit_nested = 1;
    engine_dispatch(cpu, HALO_FRAME_LIMIT_ENTRY);
    frame_limit_nested = 0;
    /* The original never writes the byte; if anything did, keep its value. */
    if (!G8(HALO_FRAME_LIMIT_THROTTLE)) S8(HALO_FRAME_LIMIT_THROTTLE, throttle);
    atomic_fetch_add_explicit(&frame_limit_counts[1], 1, memory_order_relaxed);
    return 1;
}
