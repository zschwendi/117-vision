#ifndef HALO_SENSE_POSE_H
#define HALO_SENSE_POSE_H
#include <stdint.h>
#include "gamecontroller.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Coordinates relative to the original panorama centre camera, NOT the
 * current head rotation: +X forward, +Y left, +Z up. Position is the grip
 * minus the head in metres. The native weapon hook applies the camera's
 * world basis and Halo's metres-to-world-units scale exactly once. */
typedef struct {
    float position[3], forward[3], up[3];
    float trigger;
    int valid;
} HaloSenseHandPose;
typedef struct {
    HaloSenseHandPose left, right;
    float head_yaw; /* radians, positive looking right */
    uint64_t generation;
    uint64_t controller_generation; /* connection that produced these tracked poses */
    /* The head itself in the same panorama coordinates (metres). Hand room
     * position = head_position + hand.position, so physical intent can be
     * measured without the head's own translation. 0 valid: not published. */
    float head_position[3];
    int head_position_valid;
} HaloSenseFrame;

/* A head sample remains available when an accessory is outside tracking.
 * Directions use the same neutral panorama basis as hand poses. Ownership
 * and its 150ms lifetime are independent of normal two-hand validity. */
typedef struct {
    float forward[3], up[3];
    uint64_t generation, controller_generation;
    /* Pitch-free movement heading (radians, positive looking right), the
     * same value as HaloSenseFrame.head_yaw. Scoped movement uses it instead
     * of forward, which includes virtual pitch and can pass vertical. Scope
     * aim and presentation keep forward/up. 0 valid: scoped movement stops. */
    float movement_yaw;
    int movement_yaw_valid;
} HaloSenseHeadPose;
void halo_sense_publish_head(uint64_t session, const HaloSenseHeadPose *pose);
int halo_sense_head_read(HaloSenseHeadPose *out);
/* The engine owns scope state. Entry/exit invalidates held firing inputs. */
void halo_sense_set_scope_active(int active);
typedef struct {
    uint64_t session, generation;
    HaloSenseHeadPose head;
    float trigger;
    int armed;
} HaloSenseScopeInput;
int halo_sense_scope_input(HaloSenseScopeInput *out);
/* One coherent left-hand release sample, in the same neutral basis as
 * HaloSenseFrame. Velocity is metres/second, differentiated from measured
 * (unpredicted) grip positions in the physical origin space and rotated by
 * this same publication's panoramaFromOrigin exactly once, so virtual
 * turning/pitch alone never creates velocity. The hand pose is this same
 * publication's predicted presentation pose (what is displayed); the velocity
 * window ends at the latest measured anchor, at most 75 ms earlier. A release
 * commits only with motion_valid; a valid still hand drops the grenade. */
typedef struct {
    HaloSenseHandPose hand;
    float velocity[3];
    uint64_t generation, controller_generation;
    int motion_valid;
} HaloSenseGrenadeSample;
void halo_sense_publish_grenade(uint64_t session,const HaloSenseGrenadeSample *sample);
/* Only actual DirectInput mouse/joystick reads may begin this one-tick pulse. */
int halo_sense_grenade_pulse(uint32_t clock_identity,uint32_t tick,int gameplay);
uint64_t halo_sense_grenade_event_epoch(void);
/* A committed L1 release: the sample that was live when L1 rose, plus its
 * age on the bridge clock (CLOCK_MONOTONIC) when accepted. Only this
 * duration crosses clocks; the engine subtracts it from its own clock read
 * before the call, so the reconstructed sample time is never later than the
 * real one and no later render camera is chosen. */
typedef struct {
    HaloSenseGrenadeSample sample;
    uint64_t epoch, session, age_ns;
} HaloSenseGrenadeRelease;
/* Engine, at the original's accepted throw start (0056E080 returned 1 and
 * set the throw state) for the local player, with the original game clock
 * ([0x006F1D6C]) and its tick (+0x0C). 1: the delivered Sense release,
 * consumed and bound to that throw. -1: the delivered DI pulse came from a
 * Sense release that can no longer be honoured (tracking, ownership, session
 * or menu loss): the caller cancels the accepted throw, never head aim.
 * 0: no Sense pulse is live (ordinary input, original unchanged). A delivery
 * is live only on its own clock, from its tick until the next DI poll of a
 * later tick or any ordinary-controller DI read, so a pulse the original did
 * not accept can never be attributed to a later or ordinary throw. */
int halo_sense_grenade_accept(uint32_t clock, uint32_t tick, HaloSenseGrenadeRelease *out);
/* Every DI read whose input comes from an ordinary (non-Sense) controller. */
void halo_sense_grenade_ordinary_read(void);
/* Read only: L1 is held as the armed throw clutch (the hold whose release
 * commits a HaloSenseGrenadeRelease) with fresh head/pad ownership and a
 * fresh left sample. Never spends, delivers or changes throw state. The
 * engine draws the selected grenade in the left hand from it, once per
 * outer epoch, only under the same session and input generation. */
typedef struct {
    uint64_t session, generation, controller_generation;
    int held;
} HaloSenseGrenadeHold;
int halo_sense_grenade_hold(HaloSenseGrenadeHold *out);

/* Native launcher control ownership; callback never calls GameController. */
void halo_sense_app_input_changed(int blocked);
int halo_sense_app_input_blocked(void);

/* Mode 0 is physical only, 1 smooth stick, 2 point-to-turn plus smooth stick;
 * 30/45 retain optional snap sizes. Settings choose the persisted default.
 * This never changes ordinary gamepad controls. */
void halo_sense_set_turn_mode(int mode);
int halo_sense_turn_mode(void);
void halo_sense_set_turn_speed(float degrees_per_second);
float halo_sense_turn_speed(void);
void halo_sense_set_turn_boundary(float normalized_screen_x);
float halo_sense_turn_boundary(void);
enum {
    HALO_SENSE_GESTURE_NONE = 0,
    HALO_SENSE_GESTURE_RELOAD = 1, /* original R: current/right weapon only */
    HALO_SENSE_GESTURE_SWAP = 2,   /* original Tab */
    HALO_SENSE_GESTURE_PICKUP = 3, /* original X: game chooses eligible pickup */
    HALO_SENSE_GESTURE_MELEE = 4   /* original F: game animation/range/damage */
};
void halo_sense_set_gestures_enabled(int enabled);
int halo_sense_gestures_enabled(void);
/* One commit per deliberate gesture. Sequence must increase within this
 * renderer session. Generation is HaloSenseStatus.generation, not the pose's
 * compositor generation. Reload/pickup require Create; shoulder swap and melee are buttonless. */
int halo_sense_gesture_enqueue(uint64_t session, uint64_t controller_generation,
                              uint64_t generation, uint64_t sequence, int action);
/* Native DirectInput transport. A pulse is stable for one original simulation
 * tick, regardless of USER32/state/buffer/peek poll count. Event metadata lets
 * buffered key-downs be discarded after an ownership/tracking discontinuity. */
typedef struct {
    uint64_t session, controller_generation, generation, sequence, created_ns, delivery_generation;
    int action;
} HaloSenseGesturePulse;
int halo_sense_gesture_keyboard_pulse(uint32_t clock_identity, uint32_t tick,
                                     int gameplay, HaloSenseGesturePulse *out);
int halo_sense_gesture_event_valid(const HaloSenseGesturePulse *event);
void halo_sense_gesture_cancel(void);
/* The persisted user setting is authoritative. An explicit diagnostic
 * HALO_SENSE_WEAPON=0 disables tracking; an absent launch variable does not. */
int halo_sense_requested(void);
void halo_sense_set_enabled(int enabled);
int halo_sense_enabled(void);
void halo_sense_set_dual_enabled(int enabled);
int halo_sense_dual_enabled(void);
int halo_sense_dual_requested(void);
/* Persisted Sense weapon feel. Grip continuity (default on) eases a support
 * latch/release over 200 ms; scoped aim smoothing (default off) filters the
 * accepted weapon orientation while zoomed. Changing either invalidates held
 * input and every accepted pose (input generation). Off is exactly raw. */
void halo_sense_set_grip_continuity(int enabled);
int halo_sense_grip_continuity(void);
void halo_sense_set_scope_smoothing(int enabled);
int halo_sense_scope_smoothing(void);
/* Tracked weapon haptics (default on): semantic shot/charge feedback on the
 * Sense controllers. The master haptics strength stays authoritative. Off
 * revokes the owner lease at once: queued semantic events are dropped and
 * the audio-derived path resumes immediately for the player's own weapon. */
void halo_sense_set_weapon_haptics(int enabled);
int halo_sense_weapon_haptics(void);
/* Engine thread, once per tick: whether the tracked path owns the local
 * campaign player's weapon feedback under this session/generation. Returns
 * the owner lease serial (0 when not owned); it changes whenever ownership
 * is re-established, so an event queued under an older lease never plays. */
uint64_t halo_sense_set_weapon_haptic_owner(int owned, uint64_t session, uint64_t generation);
/* Any thread, immediately before a semantic pulse plays: live session,
 * generation, owner lease, fresh tracking, gameplay, no native UI, toggle on
 * and master strength > 0. */
int halo_sense_weapon_haptics_admit(uint64_t session, uint64_t generation, uint64_t lease);
/* Snapshot of native input before stock-game mappings consume L2. Hand0 is
 * left; hand1 is right. Epoch changes on ownership/configuration/tracking
 * discontinuity; hardware poll sequence is never an ownership identifier.
 * The weapon layer supplies actual inventory and model readiness and must
 * additionally require armed[hand] at the final original firing boundary. */
typedef struct {
    uint64_t session, generation;
    int enabled, spatial, fresh, gameplay_supported;
    float trigger[2];
    int reload[2], armed[2];
    int support_grip; /* legacy raw L1 grenade state, before stock mappings */
    int support_blocked; /* deliberate free-hand action or launcher ownership */
} HaloSenseDualInput;
int halo_sense_dual_input(HaloSenseDualInput *out);
typedef struct {
    uint64_t session, generation, published_age_ns;
    uint64_t controller_generation, published_controller_generation;
    int requested, enabled, dual_requested, active, valid;
    int gameplay_supported, armed[2], support_grip;
    int gestures_enabled, gesture_clutch, gesture_last_action;
    uint64_t gesture_accepted, gesture_delivered, gesture_dropped;
} HaloSenseStatus;
void halo_sense_status(HaloSenseStatus *out);
/* Session is the immersive renderer's lifetime, independent of controller
 * selection. A native input poll can therefore claim a newly connected Sense
 * pair before the next compositor frame without allowing stock head firing. */
uint64_t halo_sense_begin_session(void);
void halo_sense_end_session(uint64_t session);
int halo_sense_session_active(void);
void halo_sense_observe_controller(const HostGCSnapshot *snapshot);
void halo_sense_observe_controller_for_session(uint64_t session, const HostGCSnapshot *snapshot);
/* Called synchronously by native controller connection notifications. */
void halo_sense_controller_changed(uint64_t generation);
void halo_sense_set_active(int active);
int halo_sense_active(void);
void halo_sense_set_gameplay_supported(int supported);
int halo_sense_gameplay_supported(void);
/* NULL invalidates tracking while retaining session ownership. Publication
 * stamps CLOCK_MONOTONIC internally; ARKit timestamps never cross clocks. */
void halo_sense_publish(uint64_t session, const HaloSenseFrame *frame);
int halo_sense_read(HaloSenseFrame *out);
int halo_sense_weapon_pose(unsigned hand, float position[3], float forward[3], float up[3]);
/* Apply only to gameplay snapshots, never menu input. Fresh Sense tracking
 * uses head-directed movement and consumes stick look. Lost tracking freezes
 * gameplay, preserving the pause button. Unsupported gameplay with fresh
 * tracking also preserves Use for tutorial acknowledgement / vehicle exit.
 * R2 must be released after entry,
 * source replacement or tracking loss before it can fire. */
void halo_sense_filter_gamepad(HostGCSnapshot *snapshot);

/* Which branch halo_sense_filter_gamepad would take for a Sense pair right
 * now, for native prompt hints (input_prompt_label.h). Read only: unlike the
 * filter's freshness helpers it never invalidates, disarms or cancels
 * anything, and it never changes what any control does.
 *   PASSTHROUGH  tracking off or no session: the pad is used unchanged
 *   SEATED       native seat controls: L1 and L2 withheld
 *   SCOPE        scoped with fresh tracking: L1, L2 and the right stick withheld
 *   SCOPE_HEAD   scoped after hand loss: Menu, R3 and R1 only
 *   TRACKED      on foot with fresh tracking: L1, L2 and the right stick
 *                withheld; Quick gestures may own centered Create
 *   USE_ONLY     fresh tracking, unsupported gameplay: Use (Square) and Menu
 *   MENU_ONLY    lost tracking: Menu only
 * gestures_own_create: TRACKED with Quick gestures on and their delivery
 * context live, so centered Create is the gesture clutch and Create + R3 is
 * the scoreboard (sense_pose.inc filter, sense_gesture.inc).
 * grenade_clutch: holding L1, a throwing motion and release requests the
 * original grenade throw (sense_throw_input.inc): head/pad ownership is fresh
 * and gameplay is supported, so the release pulse can be delivered. */
enum {
    HALO_SENSE_PROMPT_NONE = 0, HALO_SENSE_PROMPT_PASSTHROUGH, HALO_SENSE_PROMPT_SEATED,
    HALO_SENSE_PROMPT_SCOPE, HALO_SENSE_PROMPT_SCOPE_HEAD, HALO_SENSE_PROMPT_TRACKED,
    HALO_SENSE_PROMPT_USE_ONLY, HALO_SENSE_PROMPT_MENU_ONLY
};
typedef struct {
    int context, gestures_own_create, grenade_clutch;
    uint64_t controller_generation;
} HaloSensePromptContext;
int halo_sense_prompt_context(HaloSensePromptContext *out);

#ifdef __cplusplus
}
#endif
#endif
