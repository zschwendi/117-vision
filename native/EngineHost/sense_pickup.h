/* Physical pickup stages (pure logic). The target is only ever the weapon
 * the original game itself selected for the local player's pickup prompt
 * (player action result swap/add-weapon with its salted object datum).
 *
 * Point the free left hand at it, close L2 to lock, pull the hand toward the
 * body: the weapon's own model is then drawn flying to the hand (render
 * only; the world object never moves). Still holding L2 when it arrives
 * catches it: exactly one original exchange (X) tick is requested and the
 * weapon stays drawn in the hand while it is pending. If the original
 * pickup gives it to the unit, that is reported (it is then the unit's, not
 * drawn by us). Releasing L2, losing the engine selection, a conflicting
 * control or a refusal cancels the pending X and draws it on the ground
 * again in the same frame. The pull is the hand's displacement in the
 * room (head position added back), so head motion alone never pulls. No
 * input smoothing. */
#ifndef HALO_SENSE_PICKUP_H
#define HALO_SENSE_PICKUP_H
#include <stdint.h>
#include <string.h>
#include <math.h>

enum {
    SENSE_PICKUP_IDLE = 0,      /* no eligible engine-selected weapon, or not aimed */
    SENSE_PICKUP_AIMED = 1,     /* left hand points at the engine's selected weapon */
    SENSE_PICKUP_LOCKED = 2,    /* L2 closed while aimed */
    SENSE_PICKUP_COMMITTED = 3, /* caught: one original X requested, drawn in hand */
    SENSE_PICKUP_FLYING = 4     /* pulled: drawn flying toward the hand */
};
enum {
    SENSE_PICKUP_RESULT_NONE = 0, SENSE_PICKUP_RESULT_TAKEN = 1, /* object now owned by the local unit */
    SENSE_PICKUP_RESULT_REFUSED = 2, SENSE_PICKUP_RESULT_BUSY = 3, SENSE_PICKUP_RESULT_CANCELLED = 4
};
enum { SENSE_PICKUP_DRAW_GROUND = 0, SENSE_PICKUP_DRAW_FLYING = 1, SENSE_PICKUP_DRAW_HELD = 2 };
#define SENSE_PICKUP_AIM_ENTER .940f  /* cos 20 degrees */
#define SENSE_PICKUP_AIM_EXIT .819f   /* cos 35 degrees while aimed */
#define SENSE_PICKUP_REACH_M 3.f      /* hand to object */
#define SENSE_PICKUP_PULL_M .15f      /* room-frame hand displacement toward the body */
#define SENSE_PICKUP_PRESS .60f
#define SENSE_PICKUP_RELEASE .25f
#define SENSE_PICKUP_CONFIRM_NS UINT64_C(750000000)

typedef struct {
    int eligible;        /* campaign, on foot, engine action result 6/7, unparented, no conflicting control */
    int taken;           /* the target object's parent is now the local unit */
    int left_busy;       /* the left hand holds a magazine */
    int pull_valid;      /* room-frame hand displacement is measurable this frame */
    int pending;         /* the transport still holds the caught X (not consumed/dropped) */
    uint32_t object;     /* engine-selected object datum (with salt) */
    float grip;          /* raw L2 */
    float aim;           /* cos between left-hand forward and hand->object */
    float distance;      /* metres, hand to object */
    float pull;          /* metres the hand moved toward the body since lock */
    uint64_t now;
} SensePickupInput;
typedef struct {
    int stage, closed, result;
    uint32_t object;
    uint64_t sequence, committed_ns, fly_start_ns, fly_ns;
} SensePickupState;
typedef struct { int commit, lock, draw, cancel_pending; uint32_t cancelled_object; float progress; } SensePickupOutput;

static inline void sense_pickup_reset(SensePickupState *s, int result) {
    uint64_t sequence = s->sequence; int closed = s->closed;
    memset(s, 0, sizeof *s); s->sequence = sequence; s->closed = closed; s->result = result; s->object = UINT32_MAX;
}
/* About a third of a second, longer for farther weapons (0.3..0.7 s). */
static inline uint64_t sense_pickup_flight_ns(float metres) {
    float seconds = .25f + (isfinite(metres) ? metres : 3.f)/8.f;
    seconds = seconds < .3f ? .3f : seconds > .7f ? .7f : seconds;
    return (uint64_t)(seconds*1e9f);
}
static inline SensePickupOutput sense_pickup_step(SensePickupState *s, const SensePickupInput *in) {
    SensePickupOutput out = {0, 0, SENSE_PICKUP_DRAW_GROUND, 0, UINT32_MAX, 0};
    int was_closed = s->closed;
    if (!isfinite(in->grip) || !isfinite(in->aim) || !isfinite(in->distance) || !isfinite(in->pull)) {
        sense_pickup_reset(s, SENSE_PICKUP_RESULT_CANCELLED); s->closed = 0; return out;
    }
    if (in->grip >= SENSE_PICKUP_PRESS) s->closed = 1; else if (in->grip <= SENSE_PICKUP_RELEASE) s->closed = 0;
    int press = s->closed && !was_closed;
    if (s->stage == SENSE_PICKUP_COMMITTED) {
        /* Linearization: the original taking it wins; otherwise a released
         * hand, a lost engine selection or a conflicting control cancels the
         * pending X and the weapon is on the ground again in this frame. */
        if (in->taken) { sense_pickup_reset(s, SENSE_PICKUP_RESULT_TAKEN); return out; }
        if (!s->closed || !in->eligible || in->object != s->object) {
            uint32_t object = s->object; sense_pickup_reset(s, SENSE_PICKUP_RESULT_CANCELLED);
            out.cancel_pending = 1; out.cancelled_object = object; return out;
        }
        /* Delivered or dropped without the unit owning it: the original
         * declined (or a priority control took the moment). */
        if (!in->pending) { sense_pickup_reset(s, SENSE_PICKUP_RESULT_REFUSED); return out; }
        if (in->now < s->committed_ns || in->now - s->committed_ns > SENSE_PICKUP_CONFIRM_NS) {
            uint32_t object = s->object; sense_pickup_reset(s, SENSE_PICKUP_RESULT_REFUSED);
            out.cancel_pending = 1; out.cancelled_object = object; return out;
        }
        out.draw = SENSE_PICKUP_DRAW_HELD; out.progress = 1; return out;
    }
    int same = in->eligible && !in->left_busy && in->object == s->object;
    int aimed = in->distance <= SENSE_PICKUP_REACH_M &&
        in->aim >= (s->stage == SENSE_PICKUP_IDLE ? SENSE_PICKUP_AIM_ENTER : SENSE_PICKUP_AIM_EXIT);
    switch (s->stage) {
    case SENSE_PICKUP_IDLE:
        if (in->eligible && !in->left_busy && aimed && !s->closed) {
            int result = s->result; sense_pickup_reset(s, result);
            s->stage = SENSE_PICKUP_AIMED; s->object = in->object;
        }
        return out;
    case SENSE_PICKUP_AIMED:
        if (!same || !aimed) { int result = s->result; sense_pickup_reset(s, result); return out; }
        if (press && in->pull_valid) { s->stage = SENSE_PICKUP_LOCKED; s->result = SENSE_PICKUP_RESULT_NONE; out.lock = 1; }
        return out;
    case SENSE_PICKUP_LOCKED:
        if (!same || !s->closed || !in->pull_valid) { sense_pickup_reset(s, SENSE_PICKUP_RESULT_CANCELLED); return out; }
        /* The lock holds while the hand pulls back even if it stops pointing. */
        if (in->pull >= SENSE_PICKUP_PULL_M) {
            s->stage = SENSE_PICKUP_FLYING; s->fly_start_ns = in->now; s->fly_ns = sense_pickup_flight_ns(in->distance);
            out.draw = SENSE_PICKUP_DRAW_FLYING;
        }
        return out;
    case SENSE_PICKUP_FLYING: {
        if (!same || !s->closed) { sense_pickup_reset(s, SENSE_PICKUP_RESULT_CANCELLED); return out; }
        uint64_t elapsed = in->now >= s->fly_start_ns ? in->now - s->fly_start_ns : 0;
        float t = s->fly_ns ? (float)((double)elapsed/(double)s->fly_ns) : 1.f;
        if (t >= 1.f) { out.commit = 1; out.draw = SENSE_PICKUP_DRAW_HELD; out.progress = 1; return out; }
        out.draw = SENSE_PICKUP_DRAW_FLYING; out.progress = t; return out;
    }
    }
    sense_pickup_reset(s, SENSE_PICKUP_RESULT_CANCELLED); return out;
}
/* The catch either becomes one original X, or (transport busy/refused)
 * the weapon returns to the ground and a new lock is required. */
static inline void sense_pickup_committed(SensePickupState *s, int accepted, uint64_t now) {
    if (accepted) { s->stage = SENSE_PICKUP_COMMITTED; s->committed_ns = now; s->result = SENSE_PICKUP_RESULT_NONE; }
    else sense_pickup_reset(s, SENSE_PICKUP_RESULT_BUSY);
}
#endif
