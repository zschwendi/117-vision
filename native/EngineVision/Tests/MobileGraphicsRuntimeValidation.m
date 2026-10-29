/* The production iPad/iPhone launch-graphics functions in EngineVisionRuntime.m
 * (prepare_launch and its refusals, the plan reads, presenter rate and the
 * producer cadence and throttle lift it moves, flat-present pacing),
 * compiled for the host with the window, screen and saved choices stubbed.
 * Each scenario runs in its own process because a launch fixes the plan once
 * per process. No game, UIKit, simulator or device is involved. */
#define ENGINEVISION_MOBILE_GRAPHICS_TEST 1
/* The pacing scenarios run the production pacer on a fixture clock: Mach
 * ticks move only when the scenario does engine work or the pacer waits, a
 * CLOCK_UPTIME_RAW reading is the ticks through the timebase rounded down as
 * the OS converts them, and mach_wait_until wakes on its deadline tick (one
 * tick later for a deadline already passed) plus any oversleep the scenario
 * injects. A live scheduler may hold the thread past a whole period anywhere,
 * which the cadence rightly counts late or restarts instead of holding.
 * Injecting such a delay reproduces the CI assertion failure; the remote
 * runner's exact scheduling trace is unknown. Other scenarios and clocks are
 * real. System declarations come first so only production calls are renamed. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <time.h>
#include <assert.h>
static int pace_mock;                                /* set only in a pacing scenario's process */
static mach_timebase_info_data_t pace_timebase;
static uint64_t pace_ticks, pace_oversleep_ticks;    /* the oversleep is spent by the next wait */
static unsigned pace_timebase_calls, pace_wait_calls;
static uint64_t pace_deadline[64];
static uint64_t pace_ns(uint64_t ticks) { return (uint64_t)((__uint128_t)ticks * pace_timebase.numer / pace_timebase.denom); }
static uint64_t pace_clock(clockid_t clock) {
    return pace_mock && clock == CLOCK_UPTIME_RAW ? pace_ns(pace_ticks) : clock_gettime_nsec_np(clock);
}
static kern_return_t pace_timebase_info(mach_timebase_info_t info) {
    if (!pace_mock) return mach_timebase_info(info);
    pace_timebase_calls++; *info = pace_timebase;
    return KERN_SUCCESS;
}
static kern_return_t pace_wait_until(uint64_t deadline) {
    if (!pace_mock) return mach_wait_until(deadline);
    if (pace_wait_calls < sizeof pace_deadline / sizeof *pace_deadline) pace_deadline[pace_wait_calls] = deadline;
    pace_wait_calls++;
    pace_ticks = (deadline > pace_ticks ? deadline : pace_ticks + 1) + pace_oversleep_ticks;
    pace_oversleep_ticks = 0;
    return KERN_SUCCESS;
}
#define clock_gettime_nsec_np pace_clock
#define mach_timebase_info pace_timebase_info
#define mach_wait_until pace_wait_until
#include "../Sources/EngineVisionRuntime.m"
#undef mach_wait_until
#undef mach_timebase_info
#undef clock_gettime_nsec_np
#include <sys/wait.h>

int host_trace_imports, host_quit_requested, host_core_telemetry;
uint8_t *engine_flat_base;
uint64_t host_present_sleep_ns;
_Thread_local uint64_t host_thread_wait_ns, host_thread_waits;
uint64_t host_yield_spin_ns;
const char *mr_last_commit_error(void) { return "fixture"; }
int host_sense_scope_frame(HaloSenseWeaponActionStatus *out) { memset(out, 0, sizeof *out); return 0; }
int host_panorama_frame(HaloPanoramaInfo *out, const void *layers[HALO_PANORAMA_LAYERS]) {
    memset(out, 0, sizeof *out); out->status = HALO_PANORAMA_FLAT;
    for (unsigned i = 0; i < HALO_PANORAMA_LAYERS; i++) layers[i] = NULL;
    return 0;
}
uint64_t host_pass_profile_total_ns(void) { return 0; }
uint64_t host_frame_pacer_present(const HaloFramePacerDisplay *d) { (void)d; return 0; }
void host_sense_reticle_art_frame(HaloSenseReticleArtFrame *out) { memset(out, 0, sizeof *out); }
void host_log(const char *fmt, ...) { (void)fmt; }
int mr_finish_dropped_frame(void) { return 0; }
int mr_commit_async(void (*callback)(void *, int), void *context) { callback(context, 0); return 0; }
/* The zero-copy panorama sink is off on iPad/iPhone (HALO_PANORAMA_GPU unset);
 * reaching any of these would mean the flat path was not the one tested. */
void halo_graphics_upscale(HaloSceneUpscale *out) { (void)out; abort(); }
void host_panorama_set_gpu_sink(const HaloPanoramaGPUSink *sink) { (void)sink; abort(); }
void host_sense_reticle_art_targets(void *left, void *right) { (void)left; (void)right; abort(); }
unsigned long mr_scene_destination_usage(int mode) { (void)mode; abort(); }
void *mr_shared_device(void) { abort(); }
/* Measured memory and the time-update hook live in the renderer and
 * overrides.c; these record what the runtime asks of them. */
uint64_t mr_target_memory(uint32_t *targets) { if (targets) *targets = 3; return 123456789ull; }
void host_page_stats(uint64_t out[3]) { out[0] = 987654321ull; out[1] = out[2] = 0; }
static int limit_configured = -1, limit_configure_calls;
void host_frame_limit_configure(int lift) { limit_configured = lift; limit_configure_calls++; }
void host_frame_limit_stats(HaloFrameLimitStats *out) { memset(out, 0, sizeof *out); out->lifted = 7; out->enabled = limit_configured; }
/* enginevision_start's worker: the engine itself is not part of this test. */
static _Atomic int host_runs;
int host_run(const char *exe, const char *root) { (void)exe; (void)root; atomic_fetch_add(&host_runs, 1); return 0; }

/* Stubbed window, screen and saved choices. */
static int64_t view_w, view_h, panel_w, panel_h; static int32_t display_fps;
static NSString *stored_resolution, *stored_rate;
static void mobile_measure(MobileMeasure *m) {
    m->view_w = view_w; m->view_h = view_h; m->panel_w = panel_w; m->panel_h = panel_h; m->fps = display_fps;
}
static NSString *mobile_stored(NSString *key) {
    if ([key isEqualToString:MOBILE_RESOLUTION_KEY]) return stored_resolution;
    if ([key isEqualToString:MOBILE_RATE_KEY]) return stored_rate;
    return nil;
}
/* setenv, with injectable failure: fail_setenv_errno is the errno a failing
 * call reports (-1 reports none); fail_setenv_count how many calls fail. */
static int fail_setenv_errno, fail_setenv_count, setenv_calls;
static int mobile_setenv(const char *name, const char *value, int overwrite) {
    setenv_calls++;
    if (fail_setenv_count > 0) { fail_setenv_count--; errno = fail_setenv_errno > 0 ? fail_setenv_errno : 0; return -1; }
    return setenv(name, value, overwrite);
}
static uint64_t idle_wait_ns, idle_last_start, idle_last_end; static unsigned idle_waits;
void host_frame_idle_wait(uint64_t start, uint64_t end) {
    assert(end >= start); idle_wait_ns += end - start; idle_waits++; idle_last_start = start; idle_last_end = end;
}

/* 13-inch iPad Pro, full-screen landscape window; UIKit's nativeBounds is portrait. */
static void ipad13(void) { view_w = 2752; view_h = 2064; panel_w = 2064; panel_h = 2752; display_fps = 120; }
static const uint64_t FRAME13 = 2752ull * 2064ull * 4ull, ARENAS = 3ull * 32 * 1024 * 1024;
static uint64_t gpu(uint64_t f) { return f + f / 16; }

static mach_timebase_info_data_t pace_request;   /* the fixture clock for the next child; 0/0 the real one */
static void run(const char *name, void (*scenario)(void)) {
    fflush(stdout);
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        unsetenv("HALO_CMDLINE_EXTRA"); unsetenv("HALO_VIDMODE");
        /* A fresh process, so the pacer's cached timebase is this one's. Hours
         * to days of uptime, off the 125/3 nanosecond grid. */
        if (pace_request.denom) { pace_mock = 1; pace_timebase = pace_request; pace_ticks = 5000000000003ull; }
        @autoreleasepool { scenario(); }
        _exit(0);
    }
    int status = 0; assert(waitpid(child, &status, 0) == child);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) { fprintf(stderr, "FAIL: %s (status 0x%x)\n", name, status); exit(1); }
    printf("pass: %s\n", name);
}
static void run_paced(const char *name, void (*scenario)(void), uint32_t numer, uint32_t denom) {
    char label[160]; snprintf(label, sizeof label, "%s (timebase %u/%u)", name, numer, denom);
    pace_request = (mach_timebase_info_data_t){ numer, denom };
    run(label, scenario);
    pace_request = (mach_timebase_info_data_t){ 0, 0 };
}
static void status_says(const char *text) {
    char status[512]; enginevision_copy_status(status, sizeof status);
    assert(strstr(status, "Graphics settings not applied") && strstr(status, text));
}
static void refused(int expected, const char *why) {
    EngineMobileGraphicsPlan plan, launched;
    const char *before = getenv("HALO_CMDLINE_EXTRA"); char saved[600] = ""; int had = before != NULL;
    if (had) snprintf(saved, sizeof saved, "%s", before);
    assert(enginevision_mobile_prepare_launch(&plan) == expected && !plan.launched);
    assert(!enginevision_mobile_graphics_launched(&launched));
    const char *after = getenv("HALO_CMDLINE_EXTRA");
    assert(had ? after && !strcmp(after, saved) : after == NULL);   /* environment untouched */
    assert(limit_configure_calls == 0);
    status_says(why);
    /* The engine is never started on a refused plan, and stays startable. */
    assert(enginevision_start("/nonexistent-game-root") == expected);
    usleep(20000); assert(atomic_load(&host_runs) == 0 && enginevision_runtime_state() == ENGINEVISION_IDLE);
}

static void native_launch_writes_vidmode_once(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = @"display";
    EngineMobileGraphicsPlan plan;
    assert(!enginevision_mobile_graphics_launched(&plan) && plan.render_width == 0);
    assert(enginevision_mobile_graphics_plan(&plan) && !plan.launched);
    assert(plan.render_width == 2752 && plan.render_height == 2064 && plan.refresh == 120 && plan.lifts_throttle);
    assert(plan.native_width == 2752 && plan.view_width == 2752 && plan.view_height == 2064);
    assert(plan.display_width == 2752 && plan.display_height == 2064);
    assert(plan.memory_steady_bytes == 6 * FRAME13 + 9 * gpu(FRAME13) + ARENAS && plan.memory_peak_bytes == 10 * FRAME13 + 10 * gpu(FRAME13) + ARENAS);
    /* Before launch the presenter follows the saved choice and its screen. */
    assert(enginevision_mobile_presenter_fps(120) == 120 && enginevision_mobile_presenter_fps(60) == 60);
    assert(getenv("HALO_CMDLINE_EXTRA") == NULL && limit_configure_calls == 0);
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.launched && !plan.external_vidmode);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 2752,2064,60"));
    assert(limit_configured == 1 && plan.effective_refresh == 120);
    assert(plan.renderer_target_bytes == 123456789ull && plan.renderer_targets == 3 && plan.guest_live_bytes == 987654321ull);
    assert(plan.limiter_lifted == 7);
    /* Fixed for the process: a changed preference and a second call change nothing. */
    stored_resolution = @"1280x960"; stored_rate = @"30";
    assert(enginevision_mobile_prepare_launch(&plan) == EALREADY && plan.render_width == 2752 && plan.refresh == 120);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 2752,2064,60"));
    assert(enginevision_mobile_presenter_fps(0) == 120);
    /* The next-launch read reflects the new saved choice, the launched one does not. */
    assert(enginevision_mobile_graphics_plan(&plan) && plan.render_width == 1280 && plan.refresh == 30 && !plan.launched && !plan.lifts_throttle);
    assert(enginevision_mobile_graphics_launched(&plan) && plan.render_width == 2752 && plan.launched);
}

static void window_orientation_drives_native(void) {
    /* A portrait Stage Manager window on the same panel (coordinator's repro). */
    ipad13(); view_w = 900; view_h = 1200; stored_resolution = @"native"; stored_rate = @"display";
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0);
    assert(plan.render_width == 896 && plan.render_height == 1195 && plan.view_width == 900 && plan.view_height == 1200);
    assert(plan.render_width <= plan.view_width && plan.render_height <= plan.view_height);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 896,1195,60"));
    /* Drawables are estimated at the whole panel, which the window can grow to. */
    assert(plan.memory_steady_bytes == 6ull * 896 * 1195 * 4 + 6 * gpu(896ull * 1195 * 4) + 3 * gpu(FRAME13) + ARENAS);
}

static void admitted_existing_vidmode_is_kept(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = @"display";
    /* The current EnginePadRuntime.m sets its preset first. */
    setenv("HALO_CMDLINE_EXTRA", "-vidmode 1600,1200,60", 1);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 1600,1200,60"));
    assert(plan.external_vidmode && plan.size_source == HALO_MOBILE_SIZE_EXTERNAL);
    assert(plan.render_width == 1600 && plan.render_height == 1200 && plan.refresh == 120 && limit_configured == 1);
}

static void unsafe_existing_vidmode_is_refused(void) {
    /* Coordinator's repro, and the other unsafe or ambiguous forms. */
    ipad13(); stored_resolution = @"native"; stored_rate = @"display";
    setenv("HALO_CMDLINE_EXTRA", "-console -vidmode 9000,9000,60", 1);
    refused(EINVAL, "HALO_CMDLINE_EXTRA");
}
static void repeated_existing_vidmode_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-vidmode 1600,1200,60 -vidmode 2048,1536,60", 1);
    refused(EINVAL, "repeated");
}
static void malformed_existing_vidmode_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-console -vidmode", 1);
    refused(EINVAL, "malformed");
}

static void other_extras_are_kept(void) {
    ipad13(); stored_resolution = @"2048x1536"; stored_rate = @"60";
    setenv("HALO_CMDLINE_EXTRA", "-console", 1);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.refresh == 60 && limit_configured == 1);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-console -vidmode 2048,1536,60"));
}

static void too_long_command_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    char extra[490]; memset(extra, 'a', sizeof extra - 1); extra[0] = '-'; extra[sizeof extra - 1] = 0;
    setenv("HALO_CMDLINE_EXTRA", extra, 1);
    refused(E2BIG, "cut short");
}
static void too_long_extra_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    char extra[520]; memset(extra, 'b', sizeof extra - 1); extra[0] = '-'; extra[sizeof extra - 1] = 0;
    setenv("HALO_CMDLINE_EXTRA", extra, 1);
    refused(E2BIG, "too long");
}

static void unsafe_override_is_refused(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = nil;
    setenv("HALO_VIDMODE", "4095,3071,60", 1);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_graphics_plan(&plan) && (plan.flags & HALO_MOBILE_FLAG_OVERRIDE_REJECTED) && plan.render_width == 2752);
    refused(EINVAL, "HALO_VIDMODE");
}
static void safe_override_wins(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = nil;
    setenv("HALO_VIDMODE", "1024,768,75", 1);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.size_source == HALO_MOBILE_SIZE_OVERRIDE);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 1024,768,75"));
}
static void override_copied_by_old_caller_is_kept(void) {
    /* The old EnginePadRuntime.m block copies HALO_VIDMODE into the extras. */
    ipad13(); stored_resolution = @"native";
    setenv("HALO_VIDMODE", "1024,768,75", 1); setenv("HALO_CMDLINE_EXTRA", "-vidmode 1024,768,75", 1);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.external_vidmode && plan.render_width == 1024);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 1024,768,75"));
}
static void disagreeing_override_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_VIDMODE", "1024,768,75", 1); setenv("HALO_CMDLINE_EXTRA", "-vidmode 1600,1200,60", 1);
    refused(EINVAL, "different sizes");
}

static void unknown_window_falls_back(void) {
    view_w = view_h = panel_w = panel_h = 0; display_fps = 0; stored_resolution = @"native"; stored_rate = @"display";
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0);
    assert(plan.render_width == 1600 && plan.render_height == 1200 && plan.refresh == 60);
    assert(plan.size_source == HALO_MOBILE_SIZE_NATIVE_FALLBACK && (plan.flags & HALO_MOBILE_FLAG_DISPLAY_UNKNOWN));
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-vidmode 1600,1200,60"));
}

static void low_rate_display_is_respected(void) {
    ipad13(); display_fps = 24; stored_resolution = nil; stored_rate = @"display";
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.refresh == 24 && !plan.lifts_throttle);
    assert(limit_configured == 0 && mobile_cadence.period_ns == 1000000000ull / 24);
    assert(enginevision_mobile_presenter_fps(24) == 24);
}

static void thirty_keeps_halo_throttle(void) {
    ipad13(); stored_resolution = nil; stored_rate = @"30";
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.refresh == 30 && !plan.lifts_throttle && limit_configured == 0);
}

static void present(void) { static const uint8_t pixels[16] = {0}; metalwin_present(pixels, 2, 2); }

/* ---- Paced presents on the fixture clock ------------------------------------
 * Each step does work_ns of engine work, then presents; a held present's wait
 * oversleeps by oversleep_ns. A held present is expected released at frame
 * anchor's arrival plus periods cadence periods. */
enum { PACE_FIRST, PACE_HELD, PACE_LATE, PACE_RESTART };
static const char *const pace_outcome_name[] = { "first", "held", "late", "restart" };
typedef struct { uint64_t work_ns, oversleep_ns; int expect, anchor; unsigned periods; } PaceStep;
typedef struct { uint64_t arrive, release, end; unsigned first_wait, waits; int expect, outcome; } PaceSeen;
static PaceSeen pace_seen[16]; static int pace_seen_count;
static uint64_t pace_tick_at(uint64_t ns) {   /* the first tick reading ns or later */
    return (uint64_t)(((__uint128_t)ns * pace_timebase.denom + pace_timebase.numer - 1) / pace_timebase.numer);
}
static uint64_t pace_now(void) { return pace_ns(pace_ticks); }
static void pace_dump(const char *check, int line) {
    const HaloMobileCadence *c = &mobile_cadence; uint64_t t0 = pace_seen_count ? pace_seen[0].arrive : 0;
    fprintf(stderr, "pacing check failed (line %d): %s\n  timebase %u/%u period %llu ns; cadence frames %llu waits %llu "
                    "wait %llu ns late %llu restarts %llu; idle waits %u (%llu ns); timebase queries %u; mach_wait_until calls %u\n",
            line, check, pace_timebase.numer, pace_timebase.denom, (unsigned long long)c->period_ns,
            (unsigned long long)c->frames, (unsigned long long)c->waits, (unsigned long long)c->wait_ns,
            (unsigned long long)c->late, (unsigned long long)c->restarts, idle_waits, (unsigned long long)idle_wait_ns,
            pace_timebase_calls, pace_wait_calls);
    for (int i = 0; i < pace_seen_count; i++) {
        const PaceSeen *s = &pace_seen[i];
        fprintf(stderr, "  frame %d: arrive +%llu, expect %s release +%llu, saw %s, %u waits (first deadline %llu ticks), end +%llu ns\n",
                i, (unsigned long long)(s->arrive - t0), pace_outcome_name[s->expect], (unsigned long long)(s->release - t0),
                pace_outcome_name[s->outcome], s->waits,
                (unsigned long long)(s->waits && s->first_wait < 64 ? pace_deadline[s->first_wait] : 0), (unsigned long long)(s->end - t0));
    }
}
#define PACE_CHECK(condition) do { if (!(condition)) { pace_dump(#condition, __LINE__); abort(); } } while (0)

static void pace_run(const PaceStep *steps, int count) {
    const uint64_t period = mobile_cadence.period_ns, numer = pace_timebase.numer, denom = pace_timebase.denom;
    PACE_CHECK(pace_mock && period && pace_seen_count + count <= 16);
    for (int i = 0; i < count; i++) {
        const PaceStep *step = &steps[i]; PaceSeen *seen = &pace_seen[pace_seen_count++];
        PACE_CHECK(step->expect == PACE_HELD ? step->anchor < pace_seen_count - 1 : !step->oversleep_ns);
        if (step->work_ns) pace_ticks = pace_tick_at(pace_now() + step->work_ns);
        const uint64_t oversleep = step->oversleep_ns ? pace_tick_at(step->oversleep_ns) : 0;
        const HaloMobileCadence before = mobile_cadence; const unsigned idle_before = idle_waits;
        *seen = (PaceSeen){ .arrive = pace_now(), .first_wait = pace_wait_calls, .expect = step->expect };
        seen->release = step->expect == PACE_HELD ? pace_seen[step->anchor].arrive + step->periods * period : seen->arrive;
        pace_oversleep_ticks = oversleep;
        present();
        pace_oversleep_ticks = 0;
        seen->end = pace_now(); seen->waits = pace_wait_calls - seen->first_wait;
        seen->outcome = mobile_cadence.waits != before.waits ? PACE_HELD : mobile_cadence.late != before.late ? PACE_LATE
                      : mobile_cadence.restarts != before.restarts ? PACE_RESTART : PACE_FIRST;
        PACE_CHECK(seen->outcome == step->expect && mobile_cadence.frames == before.frames + 1);
        if (step->expect != PACE_HELD) {
            /* Released at once: no wait, no idle time, no time passes. */
            PACE_CHECK(!seen->waits && idle_waits == idle_before && seen->end == seen->arrive);
            PACE_CHECK(mobile_cadence.wait_ns == before.wait_ns);
            continue;
        }
        const uint64_t release = seen->release, deadline = pace_deadline[seen->first_wait];
        PACE_CHECK(seen->waits >= 1 && seen->first_wait + seen->waits <= 64);
        PACE_CHECK(mobile_cadence.wait_ns == before.wait_ns + (release - seen->arrive));
        /* The production conversion: the last Mach tick at or before the release. */
        PACE_CHECK((__uint128_t)deadline * numer <= (__uint128_t)release * denom &&
                   (__uint128_t)release * denom < (__uint128_t)(deadline + 1) * numer);
        for (unsigned w = 1; w < seen->waits; w++) PACE_CHECK(pace_deadline[seen->first_wait + w] == deadline);
        /* Woken on that tick, the hold is over if it reads the release, else it
         * waits once more and wakes on the first tick that does. An oversleep
         * lands past the release: one wait, ending that much late. */
        PACE_CHECK(seen->waits == (oversleep || pace_ns(deadline) >= release ? 1u : 2u));
        PACE_CHECK(seen->end >= release && seen->end == (oversleep ? pace_ns(deadline + oversleep) : pace_ns(pace_tick_at(release))));
        /* The whole hold, oversleep included, is the frame's idle time. */
        PACE_CHECK(idle_waits == idle_before + 1 && idle_last_start == seen->arrive && idle_last_end == seen->end);
    }
}

static void presenter_screen_moves_the_producer(void) {
    ipad13(); stored_resolution = nil; stored_rate = @"display";
    assert(enginevision_mobile_prepare_launch(NULL) == 0);
    assert(mobile_cadence.period_ns == 1000000000ull / 120 && limit_configured == 1);
    /* The view moves to a 60 Hz external display: presenter and producer both 60. */
    assert(enginevision_mobile_presenter_fps(60) == 60);
    assert(mobile_cadence.period_ns == 1000000000ull / 60 && limit_configured == 1);
    /* Really produced at 60: four back-to-back presents, three held a 60 Hz period each. */
    const PaceStep at60[] = { { 0, 0, PACE_FIRST, 0, 0 }, { 0, 0, PACE_HELD, 0, 1 }, { 0, 0, PACE_HELD, 0, 2 }, { 0, 0, PACE_HELD, 0, 3 } };
    pace_run(at60, 4);
    PACE_CHECK(pace_seen[3].end - pace_seen[0].arrive >= 3 * (1000000000ull / 60));
    /* A 24 Hz screen: below Halo's own 30, so the throttle is no longer lifted. */
    int calls = limit_configure_calls;
    assert(enginevision_mobile_presenter_fps(24) == 24 && limit_configured == 0 && limit_configure_calls == calls + 1);
    assert(mobile_cadence.period_ns == 1000000000ull / 24);
    /* The live change starts a new grid at the new period, not counted as a
     * restart: the next present goes at once, the one after is held 24 Hz. */
    const PaceStep at24[] = { { 1000000, 0, PACE_FIRST, 0, 0 }, { 1000000, 0, PACE_HELD, 4, 1 } };
    pace_run(at24, 2);
    PACE_CHECK(mobile_cadence.frames == 6 && mobile_cadence.waits == 4 && !mobile_cadence.late && !mobile_cadence.restarts);
    PACE_CHECK(pace_timebase_calls == 1);
    /* An unknown screen keeps the rate in force without reconfiguring. */
    assert(enginevision_mobile_presenter_fps(0) == 24 && limit_configure_calls == calls + 1);
    /* The same screen again: no change, no reconfiguration. */
    assert(enginevision_mobile_presenter_fps(24) == 24 && limit_configure_calls == calls + 1);
    /* Back on the 120 Hz panel; Display maximum follows a faster screen too. */
    assert(enginevision_mobile_presenter_fps(120) == 120 && limit_configured == 1 && mobile_cadence.period_ns == 1000000000ull / 120);
    assert(enginevision_mobile_presenter_fps(240) == 240 && mobile_cadence.period_ns == 1000000000ull / 240);
    EngineMobileGraphicsPlan plan; assert(enginevision_mobile_graphics_launched(&plan));
    assert(plan.refresh == 120 && plan.effective_refresh == 240 && plan.render_width == 1600);   /* back buffer never moved */
    assert(plan.display_max_fps == 120 && plan.current_display_max_fps == 240);                  /* launch vs current screen */
    assert(enginevision_mobile_presenter_fps(60) == 60 && enginevision_mobile_graphics_launched(&plan) && plan.effective_refresh == 60 && plan.refresh == 120);
}

static void display_maximum_follows_a_faster_screen(void) {
    /* Coordinator's display-upgrade repro: launched on 60 Hz, a 120 Hz screen attaches. */
    ipad13(); display_fps = 60; stored_resolution = @"native"; stored_rate = @"display";
    assert(enginevision_mobile_prepare_launch(NULL) == 0 && mobile_cadence.period_ns == 1000000000ull / 60);
    assert(enginevision_mobile_presenter_fps(120) == 120 && mobile_cadence.period_ns == 1000000000ull / 120 && limit_configured == 1);
    EngineMobileGraphicsPlan plan; assert(enginevision_mobile_graphics_launched(&plan));
    assert(plan.effective_refresh == 120 && plan.refresh == 60 && plan.render_width == 2752);
}
static void unknown_launch_display_follows_the_screen(void) {
    ipad13(); display_fps = 0; stored_resolution = nil; stored_rate = @"display";
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.refresh == 60);
    assert(enginevision_mobile_presenter_fps(120) == 120 && mobile_cadence.period_ns == 1000000000ull / 120);
}
static void explicit_caps_stay_ceilings(void) {
    ipad13(); display_fps = 60; stored_resolution = nil; stored_rate = @"60";
    assert(enginevision_mobile_prepare_launch(NULL) == 0);
    assert(enginevision_mobile_presenter_fps(120) == 60 && mobile_cadence.period_ns == 1000000000ull / 60 && limit_configured == 1);
    assert(enginevision_mobile_presenter_fps(48) == 48 && mobile_cadence.period_ns == 1000000000ull / 48);
    assert(enginevision_mobile_presenter_fps(240) == 60);
}
static void thirty_cap_stays_thirty(void) {
    ipad13(); stored_resolution = nil; stored_rate = @"30";
    assert(enginevision_mobile_prepare_launch(NULL) == 0 && limit_configured == 0);
    assert(enginevision_mobile_presenter_fps(240) == 30 && limit_configured == 0 && mobile_cadence.period_ns == 1000000000ull / 30);
}

static void setenv_failure_commits_nothing_and_retries(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = @"display";
    setenv("HALO_CMDLINE_EXTRA", "-console", 1);
    fail_setenv_errno = ENOMEM; fail_setenv_count = 1;
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_prepare_launch(&plan) == ENOMEM && !plan.launched && setenv_calls == 1);
    assert(!enginevision_mobile_graphics_launched(&plan) && limit_configure_calls == 0 && mobile_cadence.period_ns == 0);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-console"));
    status_says("could not be written");
    /* enginevision_start refuses on the same failure and never starts. */
    fail_setenv_count = 1;
    assert(enginevision_start("/nonexistent-game-root") == ENOMEM);
    usleep(20000); assert(atomic_load(&host_runs) == 0 && enginevision_runtime_state() == ENGINEVISION_IDLE);
    /* A failure that reports no errno is still an error. */
    fail_setenv_errno = -1; fail_setenv_count = 1;
    assert(enginevision_mobile_prepare_launch(&plan) == ENOMEM && !enginevision_mobile_graphics_launched(&plan));
    /* Retry once setenv works: committed exactly once, coherent. */
    assert(enginevision_mobile_prepare_launch(&plan) == 0 && plan.launched);
    assert(!strcmp(getenv("HALO_CMDLINE_EXTRA"), "-console -vidmode 2752,2064,60"));
    assert(limit_configure_calls == 1 && limit_configured == 1 && mobile_cadence.period_ns == 1000000000ull / 120);
    assert(enginevision_mobile_prepare_launch(&plan) == EALREADY && setenv_calls == 4);
}

static void quoted_or_control_extras_are_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-console \"-vidmode 9000,9000,60\"", 1);
    refused(EINVAL, "quotes or control characters");
}
static void unbalanced_quote_is_refused(void) {
    /* An appended -vidmode would land inside the quoted argument. */
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-path \"C:\\Halo maps", 1);
    refused(EINVAL, "quotes or control characters");
}
static void control_character_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-console\x01-vidmode 9000,9000", 1);
    refused(EINVAL, "quotes or control characters");
}
static void embedded_vidmode_is_refused(void) {
    ipad13(); stored_resolution = @"native";
    setenv("HALO_CMDLINE_EXTRA", "-console x-vidmode 9000,9000,60", 1);
    refused(EINVAL, "embedded");
}

static void fast_presents_are_held_to_the_request(void) {
    view_w = 2752; view_h = 2064; panel_w = 2064; panel_h = 2752; display_fps = 100; stored_resolution = @"1280x960"; stored_rate = @"display";
    assert(enginevision_mobile_prepare_launch(NULL) == 0 && mobile_cadence.period_ns == 10000000ull);
    /* Seven presents 3 ms of work apart at 100 fps: the first starts the grid,
     * the other six are each held to the next 10 ms line, 7 ms each. */
    PaceStep steps[7] = { { 3000000, 0, PACE_FIRST, 0, 0 } };
    for (int i = 1; i < 7; i++) steps[i] = (PaceStep){ 3000000, 0, PACE_HELD, 0, (unsigned)i };
    pace_run(steps, 7);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_graphics_launched(&plan));
    PACE_CHECK(plan.paced_frames == 7 && plan.paced_waits == 6 && idle_waits == 6);
    PACE_CHECK(!plan.paced_late && !plan.paced_restarts && pace_timebase_calls == 1);
    uint64_t held = 0; for (int i = 1; i < 7; i++) held += pace_seen[i].end - pace_seen[i].arrive;
    PACE_CHECK(pace_seen[6].end - pace_seen[0].arrive >= 60000000ull && idle_wait_ns == held);
    /* Exact on the unit timebase; on the fractional ones a release between
     * ticks is woken a tick early and waited for once more. */
    if (pace_timebase.numer == pace_timebase.denom)
        PACE_CHECK(plan.paced_wait_ns == 42000000ull && idle_wait_ns == 42000000ull && pace_wait_calls == 6);
    else PACE_CHECK(pace_wait_calls > 6);
    assert(plan.current_width == 2 && plan.current_height == 2 && plan.memory_current_bytes > 0);
    EngineVisionFrameInfo info; assert(enginevision_frame_info(&info) && info.sequence == 7);
}

static void late_presents_recover(void) {
    view_w = 2752; view_h = 2064; panel_w = 2064; panel_h = 2752; display_fps = 100; stored_resolution = @"1280x960"; stored_rate = @"display";
    assert(enginevision_mobile_prepare_launch(NULL) == 0 && mobile_cadence.period_ns == 10000000ull);
    /* 3 ms of work per frame at 100 fps, with three injected oversleeps:
     * 4 ms fits in the next frame's slack, which is held 3 ms to the grid;
     * 9 ms makes the next frame 2 ms late, sent at once with the grid kept;
     * 20 ms makes it more than a period late, which restarts the grid there. */
    const uint64_t W = 3000000ull;
    const PaceStep steps[] = {
        { W, 0, PACE_FIRST, 0, 0 },
        { W, 4000000, PACE_HELD, 0, 1 }, { W, 0, PACE_HELD, 0, 2 },
        { W, 9000000, PACE_HELD, 0, 3 }, { W, 0, PACE_LATE, 0, 0 }, { W, 0, PACE_HELD, 0, 5 },
        { W, 20000000, PACE_HELD, 0, 6 }, { W, 0, PACE_RESTART, 0, 0 }, { W, 0, PACE_HELD, 7, 1 }, { W, 0, PACE_HELD, 7, 2 },
    };
    pace_run(steps, 10);
    EngineMobileGraphicsPlan plan; assert(enginevision_mobile_graphics_launched(&plan));
    PACE_CHECK(plan.paced_frames == 10 && plan.paced_waits == 7 && idle_waits == 7);
    PACE_CHECK(plan.paced_late == 1 && plan.paced_restarts == 1 && pace_timebase_calls == 1);
    /* Holds of 7, 3, 7, 5, 7, 7 and 7 ms; idle adds the 4, 9 and 20 ms overslept. */
    if (pace_timebase.numer == pace_timebase.denom)
        PACE_CHECK(plan.paced_wait_ns == 43000000ull && idle_wait_ns == 76000000ull && pace_wait_calls == 7);
    EngineVisionFrameInfo info; assert(enginevision_frame_info(&info) && info.sequence == 10);
}

static void slow_presents_are_never_held(void) {
    ipad13(); stored_resolution = nil; stored_rate = @"display";
    assert(enginevision_mobile_prepare_launch(NULL) == 0);
    /* Halo's own limiter at 30 fps under a 120 fps request: every frame is
     * more than a period late, so each restarts the grid and none waits. */
    PaceStep steps[5] = { { 0, 0, PACE_FIRST, 0, 0 } };
    for (int i = 1; i < 5; i++) steps[i] = (PaceStep){ 1000000000ull / 30, 0, PACE_RESTART, 0, 0 };
    pace_run(steps, 5);
    EngineMobileGraphicsPlan plan; assert(enginevision_mobile_graphics_launched(&plan));
    assert(plan.paced_frames == 5 && plan.paced_waits == 0 && idle_waits == 0);
    PACE_CHECK(plan.paced_restarts == 4 && !plan.paced_late && !pace_wait_calls && !pace_timebase_calls);
}

static void invalid_frames_are_not_paced(void) {
    ipad13(); stored_resolution = nil; stored_rate = nil;
    assert(enginevision_mobile_prepare_launch(NULL) == 0);
    metalwin_present(NULL, 2, 2); metalwin_present((const uint8_t[16]){0}, 9000, 2); metalwin_present((const uint8_t[16]){0}, 0, 2);
    EngineMobileGraphicsPlan plan; assert(enginevision_mobile_graphics_launched(&plan) && plan.paced_frames == 0);
    PACE_CHECK(!pace_wait_calls && !idle_waits);
    EngineVisionFrameInfo info; assert(!enginevision_frame_info(&info));
}

static void start_fixes_the_plan_before_the_engine_thread(void) {
    ipad13(); stored_resolution = @"native"; stored_rate = @"60";
    assert(enginevision_start("/nonexistent-game-root") == 0);
    EngineMobileGraphicsPlan plan;
    assert(enginevision_mobile_graphics_launched(&plan) && plan.render_width == 2752 && plan.refresh == 60);
    assert(strstr(host_cmdline_buf, " -vidmode 2752,2064,60"));
    for (int i = 0; i < 200 && !atomic_load(&host_runs); i++) usleep(5000);
    assert(atomic_load(&host_runs) == 1);
    assert(enginevision_mobile_presenter_fps(120) == 60 && limit_configured == 1);
}

int main(void) {
    run("native launch writes -vidmode once", native_launch_writes_vidmode_once);
    run("the window as oriented drives Native", window_orientation_drives_native);
    run("an admitted existing -vidmode is kept", admitted_existing_vidmode_is_kept);
    run("an unsafe existing -vidmode is refused", unsafe_existing_vidmode_is_refused);
    run("a repeated -vidmode is refused", repeated_existing_vidmode_is_refused);
    run("a malformed -vidmode is refused", malformed_existing_vidmode_is_refused);
    run("other command-line extras are kept", other_extras_are_kept);
    run("a command line that would be cut short is refused", too_long_command_is_refused);
    run("extras too long for the mode are refused", too_long_extra_is_refused);
    run("an unsafe HALO_VIDMODE is refused", unsafe_override_is_refused);
    run("a safe HALO_VIDMODE wins", safe_override_wins);
    run("HALO_VIDMODE copied by the old caller is kept", override_copied_by_old_caller_is_kept);
    run("HALO_VIDMODE and extras disagreeing is refused", disagreeing_override_is_refused);
    run("an unknown window falls back to 1600x1200 at 60", unknown_window_falls_back);
    run("a 24 Hz display requests 24 and keeps Halo's throttle", low_rate_display_is_respected);
    run("a 30 fps choice keeps Halo's throttle", thirty_keeps_halo_throttle);
    /* Pacing on the fixture clock: a unit, the arm64 and a timebase where
     * neither periods nor releases fall on ticks. */
    run_paced("the presenter's screen moves producer cadence and throttle lift", presenter_screen_moves_the_producer, 125, 3);
    run_paced("fast flat presents are held to the request", fast_presents_are_held_to_the_request, 1, 1);
    run_paced("fast flat presents are held to the request", fast_presents_are_held_to_the_request, 125, 3);
    run_paced("fast flat presents are held to the request", fast_presents_are_held_to_the_request, 7, 3);
    run_paced("late and overslept presents recover the grid", late_presents_recover, 1, 1);
    run_paced("late and overslept presents recover the grid", late_presents_recover, 125, 3);
    run_paced("late and overslept presents recover the grid", late_presents_recover, 7, 3);
    run_paced("slow flat presents are never held", slow_presents_are_never_held, 125, 3);
    run_paced("invalid frames are not paced or published", invalid_frames_are_not_paced, 125, 3);
    run("enginevision_start fixes the plan first", start_fixes_the_plan_before_the_engine_thread);
    run("Display maximum follows a faster screen", display_maximum_follows_a_faster_screen);
    run("an unknown launch display follows the screen", unknown_launch_display_follows_the_screen);
    run("60 stays a ceiling on any screen", explicit_caps_stay_ceilings);
    run("30 stays 30 and keeps Halo's throttle", thirty_cap_stays_thirty);
    run("a setenv failure commits nothing and a retry succeeds", setenv_failure_commits_nothing_and_retries);
    run("quoted extras are refused", quoted_or_control_extras_are_refused);
    run("an unbalanced quote is refused", unbalanced_quote_is_refused);
    run("a control character is refused", control_character_is_refused);
    run("an embedded -vidmode is refused", embedded_vidmode_is_refused);
    puts("mobile graphics runtime: launch plan, refusals, environment, presenter rate, producer cadence, throttle lift and present pacing pass");
    return 0;
}
