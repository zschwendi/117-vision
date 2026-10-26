#ifndef ENGINE_VISION_BRIDGE_H
#define ENGINE_VISION_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "panorama.h"
#include "sense_reticle_art.h"
#include "mobile_graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENGINEVISION_IDLE = 0,
    ENGINEVISION_STARTING = 1,
    ENGINEVISION_RUNNING = 2,
    ENGINEVISION_STOPPED = 3,
    ENGINEVISION_FAILED = 4
};

typedef struct {
    uint64_t sequence;
    int32_t width;
    int32_t height;
    size_t byte_count;
    /* Completed original scoped render belonging to these exact BGRA bytes.
     * Zero for ordinary, panorama, incomplete or failed publications. Never
     * infer this from a later live action-state read. */
    HaloSenseWeaponActionStatus scope;
} EngineVisionFrameInfo;

typedef struct { uint64_t sequence, hash; uint32_t sample_count, nonblack_count, differing_count; } EngineFrameDigest;
void enginevision_frame_digest(EngineFrameDigest *out);

/* Starts the one process-wide engine instance. The root must contain halo.exe.
 * Returns zero only after the required worker thread has been created. */
int enginevision_start(const char *game_root);
/* Register controller discovery on the app's main thread before game startup. */
void enginevision_prepare_controller(void);
void enginevision_request_stop(void);
int enginevision_runtime_state(void);
int enginevision_exit_code(void);
void enginevision_copy_status(char *destination, size_t capacity);

/* The info/copy split is race-safe: copy returns false if capacity is smaller
 * than the newest complete frame. Callers retry after reading fresh info. */
bool enginevision_frame_info(EngineVisionFrameInfo *out);
bool enginevision_copy_latest_frame(void *destination, size_t capacity,
                                    EngineVisionFrameInfo *out);

typedef struct {
    uint64_t sequence;
    int32_t width, height;
    size_t byte_count;
    /* Indexed by layer, not by bearing: the zenith and nadir views carry
     * their own projection and sit at layers 5 and 6. */
    float projection_x[HALO_PANORAMA_LAYERS], projection_y[HALO_PANORAMA_LAYERS];
    uint64_t source_epoch, flat_sequence, scene_epoch;
    uint64_t layer_epoch[HALO_PANORAMA_LAYERS];
    /* 0: ordinary flat/menu; 1: complete panorama; 2: incomplete world.
     * These fields are returned even when info/copy returns false. */
    uint32_t status, failure_reason;
    /* 1 when right-eye layers are present, so each eye samples its own views. */
    uint32_t stereo;
    float viewport_u_min[HALO_PANORAMA_LAYERS], viewport_v_min[HALO_PANORAMA_LAYERS], viewport_u_max[HALO_PANORAMA_LAYERS], viewport_v_max[HALO_PANORAMA_LAYERS];
    /* The engine camera each layer was drawn with (position, forward, up;
     * zero when unknown) and the first epoch after the last camera cut, as
     * panorama.h describes them. Carried with the images exactly like
     * layer_epoch, so the presenter can turn an older layer to meet the
     * newest centre. */
    float layer_pose[HALO_PANORAMA_LAYERS][HALO_PANORAMA_POSE_FLOATS];
    uint64_t cut_epoch;
} EngineVisionPanoramaInfo;
/* Copy one coherent publication atomically. Retained ring/cap images can
 * precede source_epoch; layer_epoch identifies each image's actual tick. */
bool enginevision_panorama_info(EngineVisionPanoramaInfo *out);
bool enginevision_copy_panorama(void *destination, size_t capacity, EngineVisionPanoramaInfo *out);

/* Zero-copy panorama: the engine blits its eight views and HUD into
 * runtime-owned Metal textures. `latest` lends the newest complete slot (one
 * lease); release it from the completion handler of the command buffer that
 * sampled it. byte_count is 0 for these frames; textures are unretained
 * id<MTLTexture> that stay valid while leased. */
typedef struct {
    int32_t slot;
    EngineVisionPanoramaInfo info;
    void *textures[HALO_PANORAMA_LAYERS];
    HaloSenseReticleArtFrame reticle_art;
    void *reticle_textures[2];
} EngineVisionPanoramaGPUSnapshot;
bool enginevision_panorama_gpu_enabled(void);
bool enginevision_panorama_gpu_latest(EngineVisionPanoramaGPUSnapshot *out);
void enginevision_panorama_gpu_release(int32_t slot);
/* What the pool has been doing, for the diagnostics report. A frame the
 * compositor cannot lease looks identical to a frame the engine never made
 * unless these say which it was: rotation on the headset showed black with
 * the host reporting COMPLETE, and nothing on the device could say whether
 * a slot ever became ready. */
typedef struct {
    uint64_t published, dropped, publish_failed, carry_copies, carry_failed, publish_superseded;
    int32_t latest_slot;
    int32_t slot_state[3], slot_leases[3];
    char last_error[128];
} EngineVisionPanoramaGPUStats;
void enginevision_panorama_gpu_stats(EngineVisionPanoramaGPUStats *out);
/* Where the engine thread's time goes, cumulative for the run. The Mac
 * profiler has always had this; the headset never did, and the headset is
 * where the engine runs at four frames a second while the Mac runs at
 * sixteen, so this is the number that says why. */
typedef struct {
    uint64_t passes, pass_ns, readback_ns, readback_calls, draws, vertex_ns, submit_ns, upload_ns;
    /* The game's own Sleep(n) calls and time, and its Sleep(0) yields. */
    uint64_t sleep_calls, sleep_ns, yield_calls;
    /* Controller pulses fired from voice onsets (the player's own weapon). */
    uint64_t haptic_onsets;
    /* The bearing budget: extra passes per two frames, and the engine's busy
     * time per frame (the period less the frame limiter's spin) it steers on. */
    uint32_t panorama_extra_half;
    /* The budget's heavy tier: 0 normal, 3 lightest (panorama_budget.h). */
    uint32_t panorama_tier;
    float panorama_busy_seconds;
    /* The gaze pointer: frames its servo ran with a menu up, and the
     * engine's cursor as it last read it (interface pixels, 640x480). */
    uint64_t pointer_frames;
    int32_t pointer_x, pointer_y;
    /* CPU time the engine thread has run, cumulative. Against wall time it
     * says whether a slow headset frame is the engine computing or the
     * engine waiting for a core; the Mac runs the same tick three times
     * faster, and nothing measured on the headset said which. */
    uint64_t engine_cpu_ns;
    /* Which CPU the engine thread was on at each presented frame, by CPU
     * number, and how many performance and efficiency cores there are
     * (hw.perflevel0/1). The engine runs ticks and passes about 3.4x slower
     * on the headset than on the Mac, which is the gap between the two
     * kinds of core; this says which kind it gets. */
    uint32_t engine_cpu_frames[16];
    uint32_t performance_cores, efficiency_cores;
    /* Metal pipelines built on first use, and the engine-thread time spent. */
    uint64_t program_compiles, program_compile_ns;
    /* Times the audio watchdog found the output queue stalled and rebuilt it. */
    uint32_t audio_rebuilds;
    /* Pipelines built ahead of their draws (mr_pipeline_stats): draws that
     * waited for a background build and the time; background pipeline and
     * function builds and their time; first draws that found a background
     * pipeline ready; binary-archive hits; functions the engine thread
     * compiled itself. */
    uint64_t program_waits, program_wait_ns;
    uint64_t program_background_pipelines, program_background_functions, program_background_ns;
    uint64_t program_prewarm_hits, program_archive_hits, program_engine_functions;
} EngineVisionDrawProfile;
void enginevision_draw_profile(EngineVisionDrawProfile *out);
/* 1 while the engine has a menu up (the pointer's scope). */
int enginevision_menu_active(void);
/* 1 only for a genuine in-game menu (pausing widget or non-campaign UI root);
 * 0 for the front-end shell, so title menus at startup never qualify. */
int enginevision_pause_menu_active(void);
/* Core residency, clock counters, waits and game ticks. Cumulative for the
 * run, nanoseconds unless named otherwise; scheduling fields are snapshots.
 * API and snapshot availability are explicit: zero is not evidence of idle. */
typedef struct {
    /* The engine thread's frames, split at each Present. Frames over a
     * second (loads, stalls) are counted apart and kept out of the split. */
    uint64_t frames, wall_ns, pass_ns, idle_ns, wait_ns, waits, sleep_ns, other_ns;
    uint64_t cpu_ns, off_core_ns, unaccounted_ns, outside_pass_ns;
    uint32_t snapshot_available;
    /* Game ticks from Halo's own counter, and frames and non-pass time by
     * ticks run (0, 1, 2, 3, 4+). */
    uint64_t ticks, tick_frames[5], tick_other_ns[5], tick_resyncs, tick_mismatch_frames;
    uint64_t tick_unknown_frames, counts_frames;
    uint64_t stall_outside_pass_ns, stall_tick_unknown_frames;
    uint32_t max_ticks, counts_available;
    uint64_t stall_frames, stall_ns, stall_ticks, presents;
    /* thread_info(THREAD_EXTENDED_INFO) on the engine thread, now. */
    uint32_t thread_info_available;
    int32_t thread_info_result, thread_run_state, thread_flags, thread_max_priority;
    int32_t thread_policy, thread_priority, thread_base_priority, thread_cpu_usage;
    uint64_t thread_run_ns;
    /* The whole process: proc_pid_rusage(RUSAGE_INFO_V6), and cluster
     * switches from task_info(TASK_POWER_INFO_V2). Runnable time includes
     * running time, so runnable less CPU is the time spent ready with no
     * core. QoS order: default, maintenance, background, utility, legacy,
     * user-initiated, user-interactive; it is the QoS the kernel applied. */
    uint32_t process_available, process_pset_switches_available;
    int32_t process_rusage_result, process_timebase_result;
    uint64_t process_cpu_ns, process_performance_ns, process_runnable_ns;
    uint64_t process_cycles, process_instructions, process_performance_cycles, process_performance_instructions;
    uint64_t process_qos_ns[7];
    uint64_t process_energy_nj, process_performance_energy_nj, process_disk_read_bytes, process_pageins;
    uint64_t process_interrupt_wakeups, process_idle_wakeups, process_pset_switches;
} EngineVisionCoreTelemetry;
/* False, with out zeroed, while the telemetry is off (HALO_CORE_TELEMETRY=0)
 * or the engine has not started: the report then leaves its groups out. */
bool enginevision_core_telemetry(EngineVisionCoreTelemetry *out);
/* Normalized menu target (0,0 top left), panel hit, and pointer action:
 * -1 cancels pending input, 0 previews a target, 2 queues one left-click.
 * Gaze comes from a system interaction; taps remain queued until the engine
 * cursor reaches their target. This is not a Boolean held-button API. */
void enginevision_pointer(float u, float v, int on_panel, int action);

/* iPad/iPhone launch graphics (mobile_graphics.h has the policy, and
 * frame_limit.h the time-update hook). Sizes are pixels; refresh is frames a
 * second requested of the display and of the engine, not a measured rate.
 * Call on the main thread; off it the window reads as unknown and the plan
 * falls back safely. */
typedef struct {
    int32_t display_width, display_height;   /* whole panel, long edge first; 0 unknown */
    int32_t view_width, view_height;         /* the game's window as oriented now; 0 unknown */
    int32_t display_max_fps;                 /* the current screen's maximum; 0 unknown */
    int32_t native_width, native_height;     /* largest 4:3 game viewport in the window; 0 unknown */
    int32_t render_width, render_height;     /* engine back buffer requested */
    int32_t refresh;                         /* launch rate request */
    uint32_t size_source, flags;             /* HALO_MOBILE_SIZE_*, HALO_MOBILE_FLAG_* */
    int32_t launched;                        /* 1: fixed for this process by a launch */
    /* An admitted -vidmode was already in HALO_CMDLINE_EXTRA and was kept. */
    int32_t external_vidmode;
    /* The rate in force now (the launch request, lowered to the presenter's
     * screen when that supports less) and whether it lifts Halo's saved
     * 30 fps throttle at the time update (cinematics are never lifted). */
    int32_t effective_refresh, lifts_throttle;
    /* The presenter's current screen maximum once it reported one (else the
     * launch measurement, display_max_fps, which is never updated). */
    int32_t current_display_max_fps;
    /* Estimates of frame-sized buffers (HaloMobileMemory): steady, a same-size
     * live Reset overlap, and any Reset into this size from a mode at the
     * admission bound. Not a process memory ceiling or proof of headroom. */
    uint64_t memory_steady_bytes, memory_peak_bytes, memory_guest_bytes;
    uint64_t memory_peak_worst_bytes;
    /* Engine back buffer of the newest frame and its steady estimate; 0 before one. */
    int32_t current_width, current_height;
    uint64_t memory_current_bytes;
    /* Measured after launch: every live renderer target's colour and depth
     * attachments (Halo's extra targets included), and live guest pages. */
    uint64_t renderer_target_bytes, guest_live_bytes;
    uint32_t renderer_targets;
    /* Engine flat presents held to the rate in force, cumulative. */
    uint64_t paced_frames, paced_waits, paced_wait_ns, paced_late, paced_restarts;
    /* Time updates run with the throttle lifted; kept for a cinematic;
     * declined because the executable's constants were unexpected; waits so
     * a frame was not shorter than Halo's dt floor. */
    uint64_t limiter_lifted, limiter_cinematic, limiter_declined, limiter_floor_waits;
} EngineMobileGraphicsPlan;
/* The plan the saved choices give in this window now: what the next launch
 * would fix (launched = 0). False only for a NULL argument. */
bool enginevision_mobile_graphics_plan(EngineMobileGraphicsPlan *out);
/* The plan this process fixed, with the newest frame size, measured memory
 * and pacing counters. False (out zeroed) before a launch fixed one. */
bool enginevision_mobile_graphics_launched(EngineMobileGraphicsPlan *out);
/* Fix the plan for this process before enginevision_start (main thread).
 *   0         fixed and applied: HALO_CMDLINE_EXTRA holds exactly one admitted
 *             -vidmode (appended as "-vidmode W,H,60", or an admitted one that
 *             was already there, kept), and the rate is set.
 *   EALREADY  an earlier call already fixed it; nothing changes.
 *   EINVAL    refused: HALO_VIDMODE, or a -vidmode already in
 *             HALO_CMDLINE_EXTRA, is outside 640 x 480 ... 2880 x 2160,
 *             malformed, repeated or embedded in another token; the two
 *             disagree; or HALO_CMDLINE_EXTRA contains a double quote or a
 *             control character (it could hide or swallow a -vidmode).
 *   E2BIG     refused: the command line would not fit, so the mode could
 *             be cut off.
 *   other     refused: writing HALO_CMDLINE_EXTRA failed; the errno setenv
 *             reported (ENOMEM if it reported none).
 * On refusal nothing is fixed (no plan, cadence or time-update change), the
 * environment is left as setenv left it on failure (unchanged), the runtime
 * status says why, and out (if given) holds the refused plan with its flags.
 * A later call re-measures and may succeed. The caller must not start the
 * engine on a refusal; enginevision_start refuses with the same code. */
int enginevision_mobile_prepare_launch(EngineMobileGraphicsPlan *out);
/* preferredFramesPerSecond for the presenter on a screen whose maximum is
 * screen_max_fps (0 unknown). Before launch: the saved choice on that screen.
 * After launch: the launch's rate choice re-resolved on a known screen
 * (Display maximum follows that screen's maximum up or down; 60 and 30 stay
 * ceilings; never above the screen), and the engine's present cadence and
 * time-update hook move with it. An unknown screen keeps the rate in force.
 * Resolution never changes. Main thread. */
int32_t enginevision_mobile_presenter_fps(int32_t screen_max_fps);

/* visionOS: whether this launch lifts Halo's saved 30 fps limiter at its time
 * update (HaloVisionFrameRate, frame_limit.h), and the hook's cumulative
 * counts: calls; run with the throttle lifted; already unthrottled (floor
 * guard only); kept for a cinematic or timedemo; declined for unexpected
 * executable constants; waits so dt was not below Halo's floor. A request
 * and its counters, not a measured frame rate. visionOS builds only. */
typedef struct {
    int32_t lifts_throttle;
    uint64_t calls, lifted, unthrottled, cinematic, timedemo, declined;
    uint64_t floor_waits, floor_wait_ns;
} EngineVisionFrameLimitInfo;
bool enginevision_frame_limit_info(EngineVisionFrameLimitInfo *out);

/* Existing d3d9.c links against this macOS-named interface. EngineVision owns
 * this platform replacement; it creates no UIKit/AppKit objects. */
int metalwin_init(int width, int height, const char *title);
void metalwin_present(const void *bgra, int width, int height);
void metalwin_present_gpu(int slot, int width, int height);
void metalwin_present_dropped(int width, int height);
int metalwin_should_close(void);
void metalwin_poll(void);

#ifdef __cplusplus
}
#endif
#endif
