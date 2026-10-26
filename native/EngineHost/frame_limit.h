#ifndef HALO_FRAME_LIMIT_H
#define HALO_FRAME_LIMIT_H
/* Halo's 30 fps limiter and the iOS display-rate request.
 *
 * 004C9F90 is Halo's per-frame time update, called once from the main loop
 * (004C7610). It reads QueryPerformanceCounter, and while bl is set it waits
 * (Sleep(10), then Sleep(0)) until 1/30 s ([0x672D98]) has passed since the
 * previous update. bl = !timedemo && (framerate_throttle || cinematic):
 *   0x007196D8   timedemo: never throttled, fixed 1/30 s steps
 *   0x006894BA   framerate_throttle, copied from the player profile's
 *                +0xA6F == 2 (00495580, 004BB7E0) and written back by Video
 *                Setup (004BB970), so it is saved with the profile
 *   [0x006F187C]+9  the cinematic flag: Halo's deliberate cutscene cadence
 * Then, the same with or without the wait, it measures the frame's elapsed
 * seconds, clamps it to [floor 0x672C08, ceiling 0x672AF8] (and in mode 0 to
 * menu ceilings), stores it as the frame's dt (0x71971C) and the sample as
 * the previous one (0x719700). The simulation's 30 Hz ticks come from that dt
 * elsewhere; the throttle adds only the wait.
 *
 * Above 30 fps the iOS host calls that same original with the throttle byte
 * cleared for the one call and restored as soon as it returns: exactly
 * Halo's own unthrottled time update, with the saved setting unchanged for
 * every other reader (Video Setup, the profile write, 0053B000). It does not
 * lift a cinematic or timedemo, and it declines whenever the executable's
 * own constants do not look like the limiter described here.
 *
 * Unthrottled, a frame shorter than the dt floor would be counted as the
 * floor and game time would run fast. That holds whenever the iOS request
 * is above 30 fps, whether the throttle was lifted here or the player's
 * profile already had it off. So while the request is enabled, every time
 * update outside a cinematic or timedemo reads the floor from the loaded
 * executable and, before the original runs, the host waits until at least
 * that long has passed since Halo's previous sample: dt is always the real
 * elapsed time. With the throttle already off the original then runs as it
 * is, the saved byte untouched.
 */
#include <math.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HALO_FRAME_LIMIT_ENTRY      0x004C9F90u
#define HALO_FRAME_LIMIT_THROTTLE   0x006894BAu
#define HALO_FRAME_LIMIT_TIMEDEMO   0x007196D8u
#define HALO_FRAME_LIMIT_CINEMATIC  0x006F187Cu   /* pointer; flag at +9 */
#define HALO_FRAME_LIMIT_LAST_QPC   0x00719700u   /* int64 */
#define HALO_FRAME_LIMIT_QPC_FREQ   0x006AC8F8u   /* int64 */
#define HALO_FRAME_LIMIT_CAP        0x00672D98u   /* double, 1/30 s */
#define HALO_FRAME_LIMIT_FLOOR      0x00672C08u   /* double */
#define HALO_FRAME_LIMIT_CEILING    0x00672AF8u   /* double */
/* The host's QueryPerformanceFrequency (shims_kernel32.c). */
#define HALO_FRAME_LIMIT_HOST_FREQ  1000000000ll

typedef struct { double cap, floor, ceiling; int64_t freq; } HaloFrameLimitConstants;

/* What one call decided. */
enum {
    HALO_FRAME_LIMIT_ORIGINAL = 0,      /* run the original as it is, no host action */
    HALO_FRAME_LIMIT_LIFT = 1,          /* guard the dt floor, run the original with the throttle lifted */
    HALO_FRAME_LIMIT_GUARD = 2          /* throttle already off: guard the dt floor, run the original as it is */
};
enum {
    HALO_FRAME_LIMIT_WHY_DISABLED = 0, HALO_FRAME_LIMIT_WHY_LIFTED, HALO_FRAME_LIMIT_WHY_UNTHROTTLED,
    HALO_FRAME_LIMIT_WHY_TIMEDEMO, HALO_FRAME_LIMIT_WHY_CINEMATIC, HALO_FRAME_LIMIT_WHY_CONSTANTS,
    HALO_FRAME_LIMIT_WHY_COUNT
};

/* The constants the original measures with, as this description expects:
 * a cap near 1/30 s, a non-negative floor below it, a ceiling above it, and
 * the host's 1 GHz QPC. Anything else is a different executable or data. */
static inline int halo_frame_limit_constants_usable(const HaloFrameLimitConstants *k) {
    return k && isfinite(k->cap) && k->cap > 0.03 && k->cap < 0.037 &&
           isfinite(k->floor) && k->floor >= 0.0 && k->floor < k->cap &&
           isfinite(k->ceiling) && k->ceiling >= k->cap && k->ceiling <= 1.0 &&
           k->freq == HALO_FRAME_LIMIT_HOST_FREQ;
}

/* What to do for this call, and why. Timedemo and cinematics always run the
 * original with no host action; so does a request that is not enabled, or
 * constants that do not describe this limiter (then no floor is trusted). */
static inline int halo_frame_limit_decide(int enabled, uint8_t throttle, uint32_t timedemo, int cinematic,
                                          const HaloFrameLimitConstants *k, int *why) {
    int reason = !enabled ? HALO_FRAME_LIMIT_WHY_DISABLED :
                 timedemo ? HALO_FRAME_LIMIT_WHY_TIMEDEMO :
                 cinematic ? HALO_FRAME_LIMIT_WHY_CINEMATIC :
                 !halo_frame_limit_constants_usable(k) ? HALO_FRAME_LIMIT_WHY_CONSTANTS :
                 throttle ? HALO_FRAME_LIMIT_WHY_LIFTED : HALO_FRAME_LIMIT_WHY_UNTHROTTLED;
    if (why) *why = reason;
    return reason == HALO_FRAME_LIMIT_WHY_LIFTED ? HALO_FRAME_LIMIT_LIFT :
           reason == HALO_FRAME_LIMIT_WHY_UNTHROTTLED ? HALO_FRAME_LIMIT_GUARD : HALO_FRAME_LIMIT_ORIGINAL;
}

/* The earliest QPC (host ns) at which the original may sample so that its
 * elapsed time is not below the floor; 0 when no wait is needed. */
static inline uint64_t halo_frame_limit_floor_deadline(int64_t last_qpc, int64_t now_qpc, double floor) {
    if (last_qpc <= 0 || now_qpc < last_qpc || !(floor > 0.0)) return 0;
    uint64_t floor_ns = (uint64_t)ceil(floor * 1e9);
    uint64_t deadline = (uint64_t)last_qpc + floor_ns;
    return (uint64_t)now_qpc < deadline ? deadline : 0;
}

typedef struct {
    uint64_t calls, lifted, floor_waits, floor_wait_ns;   /* lifted: LIFT calls; reasons[UNTHROTTLED]: GUARD calls */
    uint64_t reasons[HALO_FRAME_LIMIT_WHY_COUNT];
    int32_t enabled;
} HaloFrameLimitStats;

/* Implemented in frame_limit_hooks.inc (overrides.c). configure() is called
 * by the iOS runtime on the main thread; the hook runs on the engine thread. */
void host_frame_limit_configure(int lift_throttle);
void host_frame_limit_stats(HaloFrameLimitStats *out);

#ifdef __cplusplus
}
#endif
#endif
