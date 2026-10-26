/* Halo's original time update 004C9F90 (lifted, read-only generated source)
 * with the iOS/visionOS frame-rate hook (frame_limit_hooks.inc), on a fake QPC/Sleep
 * clock. Shows what the original does with the saved throttle on and off,
 * then that the hook with the throttle saved on produces exactly the
 * original's own unthrottled result (all guest memory, CPU and x87 state,
 * clock) with the saved byte restored; that cinematics, timedemo, a disabled
 * request and unexpected constants run the original unchanged; and that a dt
 * floor can never make game time run fast. No game files are needed: the
 * executable's constants are written by the fixture. SKIP without the
 * generated sources. */
#if !__has_include("sub_004C9F90.c")
#include <stdio.h>
int main(void) { puts("SKIP: generated sub_004C9F90.c is not on the include path"); return 0; }
#else
#include "host.h"
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
#include "sub_004C9F90.c"
#include "sub_00639230.c"
#include "sub_0062DE80.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

uint8_t *engine_flat_base;
enum { MEMORY_SIZE = 0x800000, STACK = 0x10000, RETURN_PC = 0x12345678,
       QPC_THUNK = 0x00700010, SLEEP_THUNK = 0x00700020, QPF_THUNK = 0x00700030,
       CINEMATIC_STATE = 0x00100000, PROFILE = 0x00200000 };
static uint64_t fake_now, idle_ns;
static unsigned sleeps_long, sleeps_zero;
static const uint64_t SPIN_NS = 250000;   /* what one Sleep(0) costs on the fake clock */

#define HALO_FRAME_LIMIT_NOW_NS() fake_now
#define HALO_FRAME_LIMIT_SLEEP_UNTIL_NS(t) do { if ((t) > fake_now) fake_now = (t); } while (0)
void host_frame_idle_wait(uint64_t start, uint64_t end) { assert(end >= start); idle_ns += end - start; }
#include "../frame_limit_hooks.inc"

void engine_dispatch(EngineCPU *cpu, uint32_t address) {
    if (address == 0x004C9F90u) {   /* as the production dispatcher: hooks first, then the original */
        if (host_frame_limit_dispatch(cpu, address)) return;
        cpu->pc = address; sub_004C9F90(cpu); return;
    }
    if (address == QPC_THUNK) { S64(ARG(0), fake_now); RET_STDCALL(1, 1); }
    if (address == QPF_THUNK) { S64(ARG(0), 1000000000ull); RET_STDCALL(1, 1); }
    if (address == SLEEP_THUNK) {
        uint32_t ms = ARG(0);
        if (ms) { sleeps_long++; fake_now += (uint64_t)ms * 1000000ull; } else { sleeps_zero++; fake_now += SPIN_NS; }
        RET_STDCALL(0, 1);
    }
    fprintf(stderr, "unexpected call %08x\n", address); abort();
}

typedef struct {
    uint8_t throttle, cinematic; uint32_t timedemo; uint16_t mode;
    double cap, floor, ceiling; int64_t freq;
} Setup;
static const Setup normal = { 1, 0, 0, 1, 1.0 / 30.0, 0.0, 1.0 / 15.0, 1000000000ll };

static void put_double(uint32_t a, double v) { memcpy(GPTR(a), &v, sizeof v); }
static float dt(void) { float v; memcpy(&v, GPTR(0x0071971Cu), sizeof v); return v; }
static void setup(const Setup *s, uint64_t last, uint64_t now) {
    memset(engine_flat_base, 0, MEMORY_SIZE);
    S32(0x0063A0ACu, QPC_THUNK); S32(0x0063A29Cu, SLEEP_THUNK); S32(0x0063A0C0u, QPF_THUNK);
    S8(0x006894BAu, s->throttle); S32(0x007196D8u, s->timedemo); S16(0x00719720u, s->mode);
    S32(0x006F187Cu, CINEMATIC_STATE); S8(CINEMATIC_STATE + 9u, s->cinematic);
    put_double(0x00672D98u, s->cap); put_double(0x00672D90u, 0.012);
    put_double(0x00672C08u, s->floor); put_double(0x00672AF8u, s->ceiling);
    put_double(0x00672D80u, 0.1); put_double(0x00672D88u, 0.05);
    S64(0x006AC8F8u, (uint64_t)s->freq); S64(0x00719700u, last);
    /* A saved profile with the throttle chosen; nothing may write it. */
    for (uint32_t i = 0; i < 0x1000; i++) S8(PROFILE + i, (uint8_t)(i * 7u + 3u));
    S8(PROFILE + 0xA6Fu, 2);
    fake_now = now; sleeps_long = sleeps_zero = 0;
}
static EngineCPU call(void) {
    EngineCPU cpu = { 0 }; cpu.gpr[4] = STACK; cpu.pc = 0x004C9F90u; cpu.fp_control = 0x027F; cpu.flags = 0x202;
    S32(STACK, RETURN_PC); cpu.gpr[4] -= 0; /* esp points at the return address */
    engine_dispatch(&cpu, 0x004C9F90u);
    assert(cpu.pc == RETURN_PC && cpu.gpr[4] == STACK + 4);
    return cpu;
}
static int same_cpu(const EngineCPU *a, const EngineCPU *b) {
    return !memcmp(a->gpr, b->gpr, sizeof a->gpr) && a->flags == b->flags && a->pc == b->pc &&
           !memcmp(a->fp_reg, b->fp_reg, sizeof a->fp_reg) && a->fp_valid == b->fp_valid && a->fp_top == b->fp_top &&
           a->fp_control == b->fp_control && a->fp_status == b->fp_status;
}

typedef struct { EngineCPU cpu; uint8_t *memory; uint64_t now; unsigned long_sleeps; } Result;
static Result run(const Setup *s, uint64_t last, uint64_t now, int enabled) {
    host_frame_limit_configure(enabled);
    setup(s, last, now);
    Result r; r.cpu = call(); r.now = fake_now; r.long_sleeps = sleeps_long;
    r.memory = malloc(MEMORY_SIZE); assert(r.memory); memcpy(r.memory, engine_flat_base, MEMORY_SIZE);
    return r;
}
static int same(const Result *a, const Result *b) {
    return same_cpu(&a->cpu, &b->cpu) && a->now == b->now && !memcmp(a->memory, b->memory, MEMORY_SIZE);
}
static float result_dt(const Result *r) { float v; memcpy(&v, r->memory + 0x0071971Cu, sizeof v); return v; }
static uint64_t reason(int why) { HaloFrameLimitStats s; host_frame_limit_stats(&s); return s.reasons[why]; }

int main(void) {
    engine_flat_base = calloc(1, MEMORY_SIZE); assert(engine_flat_base);
    const uint64_t T = 5000000000ull, work = 5000000ull;   /* the frame took 5 ms */
    unsigned checked = 0;

    /* The original, as lifted: saved throttle on waits to 1/30 s and sleeps
     * meanwhile; off measures the 5 ms frame. */
    Setup throttled = normal, free_running = normal; free_running.throttle = 0;
    Result capped = run(&throttled, T, T + work, 0);
    assert(capped.now - T >= 33333333ull && result_dt(&capped) >= (float)(1.0 / 30.0) - 1e-6f && capped.long_sleeps > 0);
    Result original_free = run(&free_running, T, T + work, 0);
    assert(fabsf(result_dt(&original_free) - 0.005f) < 1e-6f && original_free.long_sleeps == 0);

    /* Every game mode and several frame lengths: the hook with the throttle
     * saved on equals the original with it off, except the saved byte, which
     * is back to what it was. */
    for (uint16_t mode = 0; mode <= 2; mode++) for (int f = 0; f < 5; f++) {
        const uint64_t lengths[] = { 1000000, 5000000, 16000000, 40000000, 200000000 };
        for (uint8_t saved = 1; saved <= 2; saved++) {
            Setup on = normal, off = normal; on.mode = off.mode = mode; on.throttle = saved; off.throttle = 0;
            Result want = run(&off, T, T + lengths[f], 0);
            uint64_t lifted_before = reason(HALO_FRAME_LIMIT_WHY_LIFTED);
            Result got = run(&on, T, T + lengths[f], 1);
            assert(reason(HALO_FRAME_LIMIT_WHY_LIFTED) == lifted_before + 1);
            assert(got.memory[0x006894BAu] == saved && got.memory[PROFILE + 0xA6Fu] == 2);
            want.memory[0x006894BAu] = saved;
            assert(same(&want, &got));
            free(want.memory); free(got.memory); checked++;
        }
    }

    /* Unchanged original: a cinematic keeps Halo's cadence, timedemo keeps
     * its fixed step, a 30 fps request does not lift, and constants that do
     * not describe this limiter are never trusted. */
    struct { Setup s; int why; } kept[] = {
        { { 1, 1, 0, 1, 1.0 / 30.0, 0.0, 1.0 / 15.0, 1000000000ll }, HALO_FRAME_LIMIT_WHY_CINEMATIC },
        { { 1, 0, 1, 1, 1.0 / 30.0, 0.0, 1.0 / 15.0, 1000000000ll }, HALO_FRAME_LIMIT_WHY_TIMEDEMO },
        { { 1, 0, 0, 1, 0.05, 0.0, 1.0 / 15.0, 1000000000ll }, HALO_FRAME_LIMIT_WHY_CONSTANTS },
        { { 1, 0, 0, 1, 1.0 / 30.0, 1.0 / 30.0, 1.0 / 15.0, 1000000000ll }, HALO_FRAME_LIMIT_WHY_CONSTANTS },
        { { 1, 0, 0, 1, 1.0 / 30.0, NAN, 1.0 / 15.0, 1000000000ll }, HALO_FRAME_LIMIT_WHY_CONSTANTS },
        { { 1, 0, 0, 1, 1.0 / 30.0, 0.0, 0.01, 1000000000ll }, HALO_FRAME_LIMIT_WHY_CONSTANTS },
        { { 1, 0, 0, 1, 1.0 / 30.0, 0.0, 1.0 / 15.0, 10000000ll }, HALO_FRAME_LIMIT_WHY_CONSTANTS },
    };
    for (unsigned i = 0; i < sizeof kept / sizeof kept[0]; i++) {
        Result want = run(&kept[i].s, T, T + work, 0);
        uint64_t before = reason(kept[i].why);
        Result got = run(&kept[i].s, T, T + work, 1);
        assert(reason(kept[i].why) == before + 1 && same(&want, &got));
        if (kept[i].why == HALO_FRAME_LIMIT_WHY_CINEMATIC) assert(got.now - T >= 33333333ull && result_dt(&got) >= (float)(1.0 / 30.0) - 1e-6f);
        free(want.memory); free(got.memory); checked++;
    }
    /* Enabled with the saved throttle already off: the original runs as it
     * is (no byte change) and, with no floor to guard, is byte-identical to
     * the plain original in every mode, cinematic and timedemo included. */
    for (uint16_t mode = 0; mode <= 2; mode++) for (int variant = 0; variant < 3; variant++) {
        Setup off = normal; off.throttle = 0; off.mode = mode;
        if (variant == 1) off.cinematic = 1;
        if (variant == 2) off.timedemo = 1;
        int why = variant == 1 ? HALO_FRAME_LIMIT_WHY_CINEMATIC : variant == 2 ? HALO_FRAME_LIMIT_WHY_TIMEDEMO : HALO_FRAME_LIMIT_WHY_UNTHROTTLED;
        Result want = run(&off, T, T + work, 0);
        uint64_t before = reason(why), lifted = reason(HALO_FRAME_LIMIT_WHY_LIFTED);
        Result got = run(&off, T, T + work, 1);
        assert(reason(why) == before + 1 && reason(HALO_FRAME_LIMIT_WHY_LIFTED) == lifted && same(&want, &got));
        assert(got.memory[0x006894BAu] == 0);
        free(want.memory); free(got.memory); checked++;
    }
    {   /* Disabled (a 30 fps request, Original on visionOS, or the Mac): the original, throttled. */
        Result want = run(&throttled, T, T + work, 0), got = run(&throttled, T, T + work, 0);
        assert(same(&want, &got) && got.now - T >= 33333333ull);
        free(want.memory); free(got.memory); checked++;
    }

    /* A dt floor longer than the frame. Halo's unthrottled original would
     * count every short frame as the floor and run game time fast. With the
     * request enabled, each update waits to the floor and dt is the real
     * interval: the throttle lifted by the hook (case 1), and the profile's
     * throttle already off (case 2: the 120 Hz request's 8.33 ms period, as
     * coordinator-unthrottled-floor-probe.c, and 2 ms work). */
    Setup floored = normal; floored.floor = 1.0 / 60.0;
    Setup floored_free = floored; floored_free.throttle = 0;
    for (int lifted = 0; lifted <= 3; lifted++) {
        host_frame_limit_configure(lifted != 0);
        setup(lifted == 1 ? &floored : &floored_free, 0, T);
        idle_ns = 0;
        double game = 0.0; uint64_t first = 0;
        for (int i = 0; i < 120; i++) {
            fake_now += lifted == 2 ? 8333333ull : 2000000ull;   /* the 120 Hz period, or 2 ms of engine work */
            call();
            if (i == 0) first = G64(0x00719700u); else game += dt();
            if (i) assert(dt() >= (float)(1.0 / 60.0) - 1e-6f);
        }
        double real = (double)(G64(0x00719700u) - first) * 1e-9;
        if (!lifted) assert(game > 1.9 && real < 0.4);              /* the hazard, in the original */
        else {
            assert(fabs(game - real) < 1e-4 && idle_ns > 0);         /* game time is real time */
            assert(G8(0x006894BAu) == (lifted == 1 ? 1 : 0));   /* the saved byte, exactly */
        }
        checked++;
    }
    HaloFrameLimitStats stats; host_frame_limit_stats(&stats);
    assert(stats.floor_waits >= 3 * 119 && stats.lifted > 0 && stats.calls > stats.lifted && stats.reasons[HALO_FRAME_LIMIT_WHY_UNTHROTTLED] >= 2 * 120);
#if ENGINE_IOS_HOOKS
    assert(engine_hooked(0x004C9F90u));
#else
    assert(!engine_hooked(0x004C9F90u));   /* macOS: the direct call, unchanged */
#endif
    free(engine_flat_base);
    printf("PASS frame limit on Halo's original 004C9F90: saved throttle enforces 1/30 s, lifted hook == original unthrottled "
           "(memory, CPU, x87, clock) with the saved byte restored in %u cases; cinematic, timedemo, disabled and unexpected "
           "constants unchanged; dt floor never accelerates game time, with the throttle lifted or already off; routed=%d\n", checked, ENGINE_IOS_HOOKS);
    return 0;
}
#endif
