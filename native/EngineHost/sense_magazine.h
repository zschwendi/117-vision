/* Physical magazine stages for a verified first-person rig (pure logic).
 *
 * The rendered magazine is the weapon's own 'frame magazine' graph node and
 * its descendants. Stages only choose where that subtree is drawn: seated
 * (original animation), in the tracked left hand, or hidden (discarded).
 * The single engine commit is one original reload request after a NEW
 * magazine is inserted; the original game then applies every ammo/reserve
 * gate and performs the transfer. Re-seating the same magazine never
 * commits. Any identity change, engine magazine activity, firing, melee,
 * scope, dual mode or tracking loss cancels to the seated original pose.
 *
 * Optional charging phase (weapons with a verified charging handle/slide
 * node): the inserted magazine stays seated without a commit until L2 closes
 * on the handle, pulls it back by the weapon's travel and releases it; the
 * handle subtree is drawn at the pulled offset meanwhile. Only that release
 * requests the single original reload. PC1.10 has no separate chambering
 * transaction for these magazines, so the original reload's own timer and
 * ammo transfer then run unchanged.
 * No input smoothing: thresholds use separate press/release levels. */
#ifndef HALO_SENSE_MAGAZINE_H
#define HALO_SENSE_MAGAZINE_H
#include <stdint.h>
#include <string.h>
#include <math.h>

enum {
    SENSE_MAG_IDLE = 0,      /* seated, original animation */
    SENSE_MAG_GRIPPED = 1,   /* L2 closed on the seated magazine: in hand */
    SENSE_MAG_OUT = 2,       /* pulled clear of the well: in hand */
    SENSE_MAG_DISCARDED = 3, /* released away from the well: well empty */
    SENSE_MAG_NEW = 4,       /* fresh magazine fetched away from the well */
    SENSE_MAG_COMMITTED = 5, /* inserted; one original reload requested */
    SENSE_MAG_INSERTED = 6,  /* charging: new magazine seated, not yet charged */
    SENSE_MAG_CHARGE_GRIPPED = 7, /* L2 closed on the charging handle/slide */
    SENSE_MAG_CHARGE_PULLED = 8   /* pulled back by its travel: release commits */
};
enum { SENSE_MAG_DRAW_SEATED = 0, SENSE_MAG_DRAW_IN_HAND = 1, SENSE_MAG_DRAW_HIDDEN = 2 };
enum {
    SENSE_MAG_RESULT_NONE = 0, SENSE_MAG_RESULT_CONFIRMED = 1, /* engine began its reload */
    SENSE_MAG_RESULT_REFUSED = 2, SENSE_MAG_RESULT_BUSY = 3, SENSE_MAG_RESULT_CANCELLED = 4,
    SENSE_MAG_RESULT_NO_RESERVE = 5
};
#define SENSE_MAG_GRAB_M .09f       /* hand to seated magazine to grab */
#define SENSE_MAG_PULL_M .12f       /* clear of the well */
#define SENSE_MAG_FETCH_M .25f      /* away from the well to take a new one */
#define SENSE_MAG_INSERT_M .05f     /* new magazine at the well */
#define SENSE_MAG_ALIGN_COS .766f   /* within 40 degrees of the seated axis */
#define SENSE_MAG_PRESS .60f
#define SENSE_MAG_RELEASE .25f
#define SENSE_MAG_CONFIRM_NS UINT64_C(750000000)
#define SENSE_MAG_HANDLE_GRAB_M .07f /* hand to charging handle/slide to grab */
#define SENSE_MAG_PULLED_SHARE .8f   /* of the travel to count as fully pulled */
#define SENSE_MAG_RETURN_SHARE .5f   /* falling back below this needs a new full pull */

typedef struct {
    int eligible;          /* supported rig + engine magazine idle, not full, same identity, no melee/scope/dual */
    int reserve;           /* engine reserve rounds > 0 */
    int engine_reloading;  /* engine magazine[0] state == reloading for this weapon */
    int firing;            /* R2 pressed */
    float grip;            /* raw L2, 0..1 */
    float to_well;         /* metres, tracked left grip to seated magazine */
    float align;           /* cos between held magazine axis and seated axis */
    int charge;            /* a charging phase is required (setting on, verified node) */
    float to_handle;       /* metres, tracked left grip to the handle/slide */
    float pull;            /* metres the gripped hand moved back relative to the weapon (its own frame) */
    float travel;          /* metres of handle/slide travel for this weapon */
    uint64_t now;
} SenseMagInput;
typedef struct {
    int stage, closed, result, withdraw;
    uint64_t sequence, committed_ns;
} SenseMagState;
typedef struct { int draw, capture_grip, commit, capture_handle; float handle; } SenseMagOutput;

static inline void sense_mag_reset(SenseMagState *s, int result) {
    uint64_t sequence = s->sequence; int closed = s->closed;
    memset(s, 0, sizeof *s); s->sequence = sequence; s->closed = closed; s->result = result;
}
/* commit_accepted is called back only when output.commit was set. */
static inline SenseMagOutput sense_mag_step(SenseMagState *s, const SenseMagInput *in) {
    SenseMagOutput out = {SENSE_MAG_DRAW_SEATED, 0, 0, 0, 0};
    int was_closed = s->closed;
    if (!isfinite(in->grip) || !isfinite(in->to_well) || !isfinite(in->align) ||
        (in->charge && (!isfinite(in->to_handle) || !isfinite(in->pull) || !(in->travel > 0)))) { sense_mag_reset(s, SENSE_MAG_RESULT_CANCELLED); s->closed = 0; return out; }
    if (in->grip >= SENSE_MAG_PRESS) s->closed = 1; else if (in->grip <= SENSE_MAG_RELEASE) s->closed = 0;
    int press = s->closed && !was_closed, release = !s->closed && was_closed;
    if (s->stage == SENSE_MAG_COMMITTED) {
        /* The original reload owns the magazine now; confirm or time out. */
        if (in->engine_reloading) { sense_mag_reset(s, SENSE_MAG_RESULT_CONFIRMED); return out; }
        if (in->now < s->committed_ns || in->now - s->committed_ns > SENSE_MAG_CONFIRM_NS) sense_mag_reset(s, SENSE_MAG_RESULT_REFUSED);
        return out;
    }
    if (s->stage != SENSE_MAG_IDLE && (!in->eligible || in->firing || in->engine_reloading)) {
        sense_mag_reset(s, SENSE_MAG_RESULT_CANCELLED); return out;
    }
    switch (s->stage) {
    case SENSE_MAG_IDLE:
        if (in->eligible && !in->firing && press && in->to_well <= SENSE_MAG_GRAB_M) {
            s->stage = SENSE_MAG_GRIPPED; s->result = SENSE_MAG_RESULT_NONE; out.capture_grip = 1;
            out.draw = SENSE_MAG_DRAW_IN_HAND;
        }
        return out;
    case SENSE_MAG_GRIPPED:
        if (!s->closed) { s->stage = SENSE_MAG_IDLE; return out; } /* let go at the well: still seated */
        if (in->to_well > SENSE_MAG_PULL_M) s->stage = SENSE_MAG_OUT;
        out.draw = SENSE_MAG_DRAW_IN_HAND; return out;
    case SENSE_MAG_OUT:
        if (release) {
            /* Back at the well it is the same magazine: re-seat, no commit. */
            if (in->to_well <= SENSE_MAG_PULL_M) { s->stage = SENSE_MAG_IDLE; return out; }
            s->stage = SENSE_MAG_DISCARDED; out.draw = SENSE_MAG_DRAW_HIDDEN; return out;
        }
        out.draw = SENSE_MAG_DRAW_IN_HAND; return out;
    case SENSE_MAG_DISCARDED:
        if (press && in->to_well >= SENSE_MAG_FETCH_M) {
            if (!in->reserve) { s->result = SENSE_MAG_RESULT_NO_RESERVE; out.draw = SENSE_MAG_DRAW_HIDDEN; return out; }
            s->stage = SENSE_MAG_NEW; s->result = SENSE_MAG_RESULT_NONE; out.draw = SENSE_MAG_DRAW_IN_HAND; return out;
        }
        out.draw = SENSE_MAG_DRAW_HIDDEN; return out;
    case SENSE_MAG_NEW:
        if (!s->closed) { s->stage = SENSE_MAG_DISCARDED; out.draw = SENSE_MAG_DRAW_HIDDEN; return out; }
        if (s->withdraw && in->to_well > 2*SENSE_MAG_INSERT_M) s->withdraw = 0;
        if (!s->withdraw && in->to_well <= SENSE_MAG_INSERT_M && in->align >= SENSE_MAG_ALIGN_COS && in->reserve) {
            if (in->charge) { s->stage = SENSE_MAG_INSERTED; return out; } /* seated, not charged: no commit */
            out.commit = 1; out.draw = SENSE_MAG_DRAW_SEATED; return out;
        }
        out.draw = SENSE_MAG_DRAW_IN_HAND; return out;
    case SENSE_MAG_INSERTED:
        if (!in->charge) { sense_mag_reset(s, SENSE_MAG_RESULT_CANCELLED); return out; }
        if (press && in->to_handle <= SENSE_MAG_HANDLE_GRAB_M) { s->stage = SENSE_MAG_CHARGE_GRIPPED; out.capture_handle = 1; }
        return out;
    case SENSE_MAG_CHARGE_GRIPPED:
    case SENSE_MAG_CHARGE_PULLED: {
        if (!in->charge) { sense_mag_reset(s, SENSE_MAG_RESULT_CANCELLED); return out; }
        float offset = in->pull < 0 ? 0 : in->pull > in->travel ? in->travel : in->pull;
        /* This epoch's pull decides first (hysteresis: full pull at 80% of
         * travel, lost below 50%), then the release: a handle let go while
         * still pulled past the return share charges (one original reload);
         * one pushed back forward before letting go, or never fully pulled,
         * springs back with nothing committed. A stale PULLED stage from an
         * earlier epoch can therefore never commit on its own. */
        if (s->stage == SENSE_MAG_CHARGE_GRIPPED && in->pull >= SENSE_MAG_PULLED_SHARE*in->travel) s->stage = SENSE_MAG_CHARGE_PULLED;
        else if (s->stage == SENSE_MAG_CHARGE_PULLED && in->pull < SENSE_MAG_RETURN_SHARE*in->travel) s->stage = SENSE_MAG_CHARGE_GRIPPED;
        if (!s->closed) {
            if (s->stage == SENSE_MAG_CHARGE_PULLED) { out.commit = 1; return out; }
            s->stage = SENSE_MAG_INSERTED; return out;
        }
        out.handle = offset; return out;
    }
    }
    sense_mag_reset(s, SENSE_MAG_RESULT_CANCELLED); return out;
}
/* Caller result for an output.commit request. Accepted: wait for the
 * engine. Refused by the transport: the new magazine stays in hand. */
static inline void sense_mag_committed(SenseMagState *s, int accepted, uint64_t now) {
    if (accepted) { s->stage = SENSE_MAG_COMMITTED; s->committed_ns = now; s->result = SENSE_MAG_RESULT_NONE; }
    else if (s->stage == SENSE_MAG_CHARGE_PULLED || s->stage == SENSE_MAG_CHARGE_GRIPPED) {
        s->stage = SENSE_MAG_INSERTED; s->result = SENSE_MAG_RESULT_BUSY; /* charge again */
    } else { s->result = SENSE_MAG_RESULT_BUSY; s->withdraw = 1; } /* retry needs a new insertion */
}
#endif
