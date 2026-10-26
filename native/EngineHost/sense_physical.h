/* Optional physical Sense presentation/interaction features. Every feature
 * is default off and independent; disabling one restores build118 behavior.
 * Status is read-only and timestamped; no guest pointers cross threads. */
#ifndef HALO_SENSE_PHYSICAL_H
#define HALO_SENSE_PHYSICAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Connected first-person arms: cosmetic shoulder-elbow-wrist solve on the
 * original animated rig. Never changes the gun, reticle, scope or shot. */
void halo_sense_set_connected_arms(int enabled);
int halo_sense_connected_arms(void);

enum {
    HALO_SENSE_ARM_OFF = 0,         /* setting off or no tracked rig this frame */
    HALO_SENSE_ARM_CONNECTED = 1,   /* solved with authored segment lengths */
    HALO_SENSE_ARM_OUT_OF_REACH = 2,/* existing collapsed forearm retained */
    HALO_SENSE_ARM_UNKNOWN_RIG = 3, /* names/parents not the verified chain */
    HALO_SENSE_ARM_DEGENERATE = 4,  /* non-finite/zero segment or pole */
    HALO_SENSE_ARM_HIDDEN = 5,      /* hand intentionally hidden (dual/mirrored) */
    HALO_SENSE_ARM_BODY = 6         /* the tracked body draws the arms instead */
};
/* Physical magazine: the weapon's own magazine subtree follows the left
 * grip (L2) out of the well, is hidden when discarded, a new one is taken
 * away from the well, and inserting it requests one original reload. */
void halo_sense_set_physical_magazine(int enabled);
int halo_sense_physical_magazine(void);
/* Optional charging phase (requires the physical magazine): after inserting
 * a new magazine, pull the charging handle (assault rifle, sniper rifle) or
 * slide (pistol) back with L2 and release it; only then is the single
 * original reload requested. Weapons without that verified node reload on
 * insertion as before. */
void halo_sense_set_physical_charge(int enabled);
int halo_sense_physical_charge(void);
enum { /* magazine_support */
    HALO_SENSE_MAGAZINE_OFF = 0, HALO_SENSE_MAGAZINE_READY = 1, HALO_SENSE_MAGAZINE_UNSUPPORTED = 2,
    HALO_SENSE_MAGAZINE_FULL = 3, HALO_SENSE_MAGAZINE_ENGINE_BUSY = 4, HALO_SENSE_MAGAZINE_BLOCKED = 5
};
enum { /* magazine_stage (sense_magazine.h) */
    HALO_SENSE_MAGAZINE_SEATED = 0, HALO_SENSE_MAGAZINE_GRIPPED = 1, HALO_SENSE_MAGAZINE_OUT = 2,
    HALO_SENSE_MAGAZINE_DISCARDED = 3, HALO_SENSE_MAGAZINE_NEW = 4, HALO_SENSE_MAGAZINE_COMMITTED = 5,
    HALO_SENSE_MAGAZINE_INSERTED = 6, HALO_SENSE_MAGAZINE_CHARGE_GRIPPED = 7, HALO_SENSE_MAGAZINE_CHARGE_PULLED = 8
};
enum { /* magazine_result */
    HALO_SENSE_MAGAZINE_RESULT_NONE = 0, HALO_SENSE_MAGAZINE_CONFIRMED = 1, HALO_SENSE_MAGAZINE_REFUSED = 2,
    HALO_SENSE_MAGAZINE_BUSY = 3, HALO_SENSE_MAGAZINE_CANCELLED = 4, HALO_SENSE_MAGAZINE_NO_RESERVE = 5
};
typedef struct {
    int arms_enabled, arm[2];       /* [0] left, [1] right */
    float shoulder_shift_m[2];      /* cosmetic reach translation near full extension */
    int magazine_enabled, magazine_support, magazine_stage, magazine_result;
    int charge_enabled, charge_supported; /* charging phase setting; this weapon has a verified handle/slide */
} HaloSensePhysicalStatus;
/* Returns 1 when the status is fresh (<=250 ms) for an active Sense session. */
int host_sense_physical_status(HaloSensePhysicalStatus *out);

/* Physical pickup: point the free left hand at the weapon the original game
 * has selected for its pickup prompt, close L2 and pull toward the body.
 * The weapon's own model is drawn flying to the hand (render only; the world
 * object is not moved); holding L2 as it arrives catches it and requests one
 * original exchange (X). target is metres from the engine eye in the neutral
 * panorama basis (+X forward, +Y left, +Z up) for a hint marker. */
void halo_sense_set_physical_pickup(int enabled);
int halo_sense_physical_pickup(void);
enum { /* reason */
    HALO_SENSE_PICKUP_OFF = 0, HALO_SENSE_PICKUP_READY = 1, HALO_SENSE_PICKUP_NONE_SELECTED = 2,
    HALO_SENSE_PICKUP_NOT_CAMPAIGN = 3, HALO_SENSE_PICKUP_NO_HAND = 4, HALO_SENSE_PICKUP_BLOCKED = 5
};
enum { /* stage */
    HALO_SENSE_PICKUP_IDLE = 0, HALO_SENSE_PICKUP_AIMED = 1, HALO_SENSE_PICKUP_LOCKED = 2,
    HALO_SENSE_PICKUP_COMMITTED = 3, /* caught: drawn in the hand until the original pickup */
    HALO_SENSE_PICKUP_FLYING = 4     /* drawn flying to the hand (render only) */
};
enum { /* result */
    HALO_SENSE_PICKUP_RESULT_NONE = 0, HALO_SENSE_PICKUP_TAKEN = 1, HALO_SENSE_PICKUP_REFUSED = 2,
    HALO_SENSE_PICKUP_BUSY = 3, HALO_SENSE_PICKUP_CANCELLED = 4
};
typedef struct {
    int enabled, reason, stage, result, target_valid;
    float target[3];
} HaloSensePickupStatus;
int host_sense_pickup_status(HaloSensePickupStatus *out);

/* Commit transport for completed physical interactions (engine thread).
 * Delivered through the existing tick-stable gesture/DirectInput bridge as
 * one original key tick: R for a confirmed magazine insertion, X for a caught
 * engine-selected weapon. Own sequence space per Sense session (a busy
 * sequence is spent, never retried late); requires the same session and
 * input generation as the interaction and fresh Sense gameplay context.
 * owner/target (unit+weapon for reload, player+object for pickup) are
 * revalidated by the registered engine check at enqueue and at every
 * DirectInput delivery poll, as are live conflicting controls (L1 grenade,
 * R1 melee, Triangle swap, R3 zoom, Square, Create/Menu/Home; R2 for
 * reload). Raw L2 and locomotion are allowed. Independent of the gesture
 * toggle. halo_sense_physical_cancel drops a matching pending commit. */
enum {
    HALO_SENSE_GESTURE_PHYSICAL_RELOAD = 5,
    HALO_SENSE_GESTURE_PHYSICAL_PICKUP = 6
};
int halo_sense_physical_enqueue(uint64_t session, uint64_t generation, uint64_t sequence, int action,
                                uint32_t owner, uint32_t target);
void halo_sense_physical_cancel(int action);
int halo_sense_physical_pending(int action);
void halo_sense_physical_set_delivery_check(int (*check)(int action, uint32_t owner, uint32_t target));

#ifdef __cplusplus
}
#endif
#endif
