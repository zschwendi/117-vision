/* Included by sense_pose.inc. All state below is protected by sense_lock.
 * Actions remain original game inputs; this code never changes inventory,
 * ammo, pickup range or the game's selected interaction target. */
#include "sense_physical.h"
#define SENSE_GESTURE_TTL_NS UINT64_C(250000000)
static _Atomic int sense_gestures_enabled = 1;
static struct {
    HaloSenseGesturePulse pending, active;
    uint64_t sequence_session, last_sequence, accepted, delivered, dropped, delivery_generation;
    uint64_t last_natural_ns, physical_session, physical_last_sequence;
    uint32_t pending_owner, pending_target, active_owner, active_target; /* physical identity */
    uint32_t clock, last_tick, active_tick, release_tick;
    int have_clock, release, last_action;
} sense_gesture;

static void sense_gesture_clear_actions_locked(void) {
    if (sense_gesture.pending.action) ++sense_gesture.dropped;
    memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending);
    memset(&sense_gesture.active, 0, sizeof sense_gesture.active);
    sense_gesture.release = 0;
}
static void sense_gesture_reset_locked(void) {
    sense_gesture_clear_actions_locked();
    sense_gesture.have_clock = 0;
    if (++sense_gesture.delivery_generation == 0) ++sense_gesture.delivery_generation;
}
static int sense_gesture_physical_action(int action) {
    return action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD || action == HALO_SENSE_GESTURE_PHYSICAL_PICKUP;
}
/* Engine-thread owner/target validator, registered by the weapon hooks. It
 * only reads guest memory (never takes sense_lock). Absent: fail closed. */
static int (*_Atomic sense_physical_check)(int action, uint32_t owner, uint32_t target);
void halo_sense_physical_set_delivery_check(int (*check)(int, uint32_t, uint32_t)) { atomic_store(&sense_physical_check, check); }
/* Live controls that own the moment of delivery. A physical commit yields
 * to grenade (L1), melee (R1), swap (Triangle), zoom (R3), legacy Square
 * use/reload/exchange, Create/Menu/Home; a reload also yields to R2, and a
 * pickup requires L2 still held. Locomotion stays allowed. */
static int sense_gesture_physical_conflict_locked(int action) {
    static const unsigned owners[] = {HOSTGC_BTN_LSHOULDER, HOSTGC_BTN_RSHOULDER, HOSTGC_BTN_Y,
        HOSTGC_BTN_RTHUMB, HOSTGC_BTN_X, HOSTGC_BTN_OPTIONS, HOSTGC_BTN_MENU, HOSTGC_BTN_HOME};
    for (unsigned i = 0; i < sizeof owners / sizeof *owners; ++i) if (sense_pad.buttons[owners[i]]) return 1;
    /* A catch is only a catch while the hand is still closed; a completed
     * magazine insertion may already have released L2. */
    if (action == HALO_SENSE_GESTURE_PHYSICAL_PICKUP &&
        !(sense_pad.buttons[HOSTGC_BTN_LTRIGGER] || (isfinite(sense_pad.lt) && sense_pad.lt > .25f))) return 1;
    return action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD &&
        (sense_pad.buttons[HOSTGC_BTN_RTRIGGER] || !isfinite(sense_pad.rt) || sense_pad.rt > .25f);
}
/* Delivery context shared by user gestures and physical commits. The user
 * gesture toggle gates only Swift gestures (sense_gesture_context_locked). */
static int sense_gesture_transport_context_locked(uint64_t now) {
    return sense_session &&
        sense_fresh_locked(now) && sense_gameplay_supported &&
        sense_pad.connected && sense_pad.spatial && sense_controller_generation &&
        !sense_pad.buttons[HOSTGC_BTN_MENU] &&
        sense_pad.source_generation == sense_controller_generation &&
        sense_frame.controller_generation == sense_controller_generation &&
        sense_pad_ns && now >= sense_pad_ns && now - sense_pad_ns <= UINT64_C(150000000);
}
static int sense_gesture_context_locked(uint64_t now) {
    return atomic_load(&sense_gestures_enabled) && sense_gesture_transport_context_locked(now);
}
static int sense_gesture_safe_controls_locked(uint64_t now, int allow_locomotion) {
    if (!sense_gesture_context_locked(now) ||
        sense_pad.buttons[HOSTGC_BTN_LSHOULDER] || sense_pad.buttons[HOSTGC_BTN_LTRIGGER] ||
        sense_pad.buttons[HOSTGC_BTN_RTRIGGER] || !isfinite(sense_pad.lt) || !isfinite(sense_pad.rt) ||
        sense_pad.lt < 0 || sense_pad.rt < 0 || sense_pad.lt >= .25f || sense_pad.rt >= .25f) return 0;
    static const unsigned blockers[] = {HOSTGC_BTN_A, HOSTGC_BTN_B, HOSTGC_BTN_X, HOSTGC_BTN_Y,
        HOSTGC_BTN_RSHOULDER, HOSTGC_BTN_LTHUMB, HOSTGC_BTN_HOME};
    for (unsigned i = 0; i < sizeof blockers / sizeof *blockers; ++i)
        if (sense_pad.buttons[blockers[i]]) return 0;
    float lx = sense_pad.spatial_raw_lx, ly = sense_pad.spatial_raw_ly;
    float rx = sense_pad.rx, ry = sense_pad.ry;
    return isfinite(lx) && isfinite(ly) && isfinite(rx) && isfinite(ry) &&
        (allow_locomotion ? fabsf(lx) <= 1.f && fabsf(ly) <= 1.f : lx * lx + ly * ly < .04f) &&
        rx * rx + ry * ry < .04f;
}
static int sense_gesture_centered_create_locked(uint64_t now) {
    return sense_gesture_safe_controls_locked(now, 0) && sense_pad.buttons[HOSTGC_BTN_OPTIONS];
}
static int sense_gesture_clutch_locked(uint64_t now) {
    return sense_gesture_centered_create_locked(now) && !sense_pad.buttons[HOSTGC_BTN_RTHUMB];
}
static int sense_gesture_scoreboard_locked(uint64_t now) {
    return sense_gesture_centered_create_locked(now) && sense_pad.buttons[HOSTGC_BTN_RTHUMB];
}
static int sense_gesture_event_locked(const HaloSenseGesturePulse *event, uint64_t now) {
    if (event && sense_gesture_physical_action(event->action))
        return event->session == sense_session && event->controller_generation == sense_controller_generation &&
            event->generation == sense_input_generation && event->sequence &&
            event->delivery_generation == sense_gesture.delivery_generation &&
            event->created_ns && now >= event->created_ns && now - event->created_ns <= SENSE_GESTURE_TTL_NS &&
            sense_gesture_transport_context_locked(now) && !sense_gesture_physical_conflict_locked(event->action);
    return event && event->action >= HALO_SENSE_GESTURE_RELOAD && event->action <= HALO_SENSE_GESTURE_MELEE &&
        event->session == sense_session && event->controller_generation == sense_controller_generation &&
        event->generation == sense_input_generation && event->sequence &&
        event->delivery_generation == sense_gesture.delivery_generation &&
        event->created_ns && now >= event->created_ns && now - event->created_ns <= SENSE_GESTURE_TTL_NS &&
        sense_gesture_context_locked(now) &&
        ((event->action != HALO_SENSE_GESTURE_MELEE && event->action != HALO_SENSE_GESTURE_SWAP) ||
          (sense_gesture_safe_controls_locked(now, !sense_pad.buttons[HOSTGC_BTN_OPTIONS]) && !sense_pad.buttons[HOSTGC_BTN_RTHUMB]));
}
void halo_sense_set_gestures_enabled(int enabled) {
    pthread_mutex_lock(&sense_lock);
    if (atomic_exchange(&sense_gestures_enabled, !!enabled) != !!enabled) sense_gesture_reset_locked();
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_gestures_enabled(void) { return atomic_load(&sense_gestures_enabled); }
void halo_sense_gesture_cancel(void) {
    pthread_mutex_lock(&sense_lock); sense_gesture_reset_locked(); pthread_mutex_unlock(&sense_lock);
}
int halo_sense_gesture_enqueue(uint64_t session, uint64_t controller_generation,
                              uint64_t generation, uint64_t sequence, int action) {
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    int accepted = 0;
    int natural = action == HALO_SENSE_GESTURE_MELEE ||
        (action == HALO_SENSE_GESTURE_SWAP && !sense_pad.buttons[HOSTGC_BTN_OPTIONS]);
    int safe = sense_gesture_safe_controls_locked(now, !sense_pad.buttons[HOSTGC_BTN_OPTIONS]) && !sense_pad.buttons[HOSTGC_BTN_RTHUMB];
    int permitted = natural ? safe && !sense_pad.buttons[HOSTGC_BTN_OPTIONS] : sense_gesture_clutch_locked(now);
    if (session && session == sense_session && controller_generation == sense_controller_generation &&
        generation == sense_input_generation && sequence &&
        action >= HALO_SENSE_GESTURE_RELOAD && action <= HALO_SENSE_GESTURE_MELEE && permitted) {
        if (sense_gesture.sequence_session != session) {
            sense_gesture.sequence_session = session; sense_gesture.last_sequence = 0; sense_gesture.last_natural_ns = 0;
        }
        if (sense_gesture.pending.action && !sense_gesture_event_locked(&sense_gesture.pending, now)) {
            ++sense_gesture.dropped; memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending);
        }
        if (sense_gesture.active.action && !sense_gesture_event_locked(&sense_gesture.active, now))
            memset(&sense_gesture.active, 0, sizeof sense_gesture.active);
        if (sequence > sense_gesture.last_sequence) {
            /* Record even a busy commit so a caller retry cannot turn one
             * deliberate gesture into a later action on another weapon. */
            sense_gesture.last_sequence = sequence;
            if (!sense_gesture.pending.action && !sense_gesture.active.action &&
                (!natural || !sense_gesture.last_natural_ns ||
                 (now >= sense_gesture.last_natural_ns && now - sense_gesture.last_natural_ns >= UINT64_C(700000000)))) {
                if (natural) sense_gesture.last_natural_ns = now;
                sense_gesture.pending = (HaloSenseGesturePulse){.session=session,
                    .controller_generation=controller_generation, .generation=generation,
                    .sequence=sequence, .created_ns=now,
                    .delivery_generation=sense_gesture.delivery_generation, .action=action};
                ++sense_gesture.accepted; sense_gesture.last_action = action; accepted = 1;
            } else ++sense_gesture.dropped;
        }
    }
    pthread_mutex_unlock(&sense_lock); return accepted;
}
int halo_sense_physical_enqueue(uint64_t session, uint64_t generation, uint64_t sequence, int action,
                                uint32_t owner, uint32_t target) {
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns(); int accepted = 0;
    int (*check)(int, uint32_t, uint32_t) = atomic_load(&sense_physical_check);
    if (session && session == sense_session && generation == sense_input_generation && sequence &&
        sense_gesture_physical_action(action) && sense_gesture_transport_context_locked(now) &&
        !sense_gesture_physical_conflict_locked(action) && owner != UINT32_MAX && target != UINT32_MAX) {
        if (sense_gesture.physical_session != session) {
            sense_gesture.physical_session = session; sense_gesture.physical_last_sequence = 0;
        }
        if (sense_gesture.pending.action && !sense_gesture_event_locked(&sense_gesture.pending, now)) {
            ++sense_gesture.dropped; memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending);
        }
        if (sense_gesture.active.action && !sense_gesture_event_locked(&sense_gesture.active, now))
            memset(&sense_gesture.active, 0, sizeof sense_gesture.active);
        /* A sequence is spent even if busy: one interaction, one commit. */
        if (sequence > sense_gesture.physical_last_sequence) {
            sense_gesture.physical_last_sequence = sequence;
            if (!sense_gesture.pending.action && !sense_gesture.active.action && check && check(action, owner, target)) {
                sense_gesture.pending = (HaloSenseGesturePulse){.session=session,
                    .controller_generation=sense_controller_generation, .generation=generation,
                    .sequence=sequence, .created_ns=now,
                    .delivery_generation=sense_gesture.delivery_generation, .action=action};
                sense_gesture.pending_owner = owner; sense_gesture.pending_target = target;
                ++sense_gesture.accepted; sense_gesture.last_action = action; accepted = 1;
            } else ++sense_gesture.dropped;
        }
    }
    pthread_mutex_unlock(&sense_lock); return accepted;
}
/* Whether a physical commit of this action is still queued or being
 * delivered (not yet consumed, dropped or cancelled). */
int halo_sense_physical_pending(int action) {
    pthread_mutex_lock(&sense_lock);
    int pending = sense_gesture_physical_action(action) &&
        (sense_gesture.pending.action == action || sense_gesture.active.action == action);
    pthread_mutex_unlock(&sense_lock); return pending;
}
/* Settings disable: drop only a matching pending/active physical commit. */
void halo_sense_physical_cancel(int action) {
    pthread_mutex_lock(&sense_lock);
    if (sense_gesture_physical_action(action)) {
        if (sense_gesture.pending.action == action) { ++sense_gesture.dropped; memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending); }
        if (sense_gesture.active.action == action) {
            memset(&sense_gesture.active, 0, sizeof sense_gesture.active);
            sense_gesture.release = 1; sense_gesture.release_tick = sense_gesture.last_tick;
        }
    }
    pthread_mutex_unlock(&sense_lock);
}
/* Delivery boundary: the physical identity must still be the engine's. */
static int sense_gesture_physical_owned_locked(const HaloSenseGesturePulse *event, uint32_t owner, uint32_t target) {
    if (!sense_gesture_physical_action(event->action)) return 1;
    int (*check)(int, uint32_t, uint32_t) = atomic_load(&sense_physical_check);
    return check && check(event->action, owner, target);
}
int halo_sense_gesture_keyboard_pulse(uint32_t clock_identity, uint32_t tick,
                                     int gameplay, HaloSenseGesturePulse *out) {
    pthread_mutex_lock(&sense_lock);
    if (out) memset(out, 0, sizeof *out);
    uint64_t now = sense_now_ns(); int action = HALO_SENSE_GESTURE_NONE;
    if (!gameplay || !clock_identity || !sense_gesture_transport_context_locked(now)) {
        sense_gesture_reset_locked(); goto done;
    }
    if (sense_gesture.have_clock && (clock_identity != sense_gesture.clock || tick < sense_gesture.last_tick)) {
        sense_gesture_reset_locked();
    }
    sense_gesture.have_clock = 1; sense_gesture.clock = clock_identity; sense_gesture.last_tick = tick;
    if (sense_gesture.pending.action && (!sense_gesture_event_locked(&sense_gesture.pending, now) ||
        !sense_gesture_physical_owned_locked(&sense_gesture.pending, sense_gesture.pending_owner, sense_gesture.pending_target))) {
        ++sense_gesture.dropped; memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending);
    }
    if (sense_gesture.active.action) {
        if (!sense_gesture_event_locked(&sense_gesture.active, now) || tick != sense_gesture.active_tick ||
            !sense_gesture_physical_owned_locked(&sense_gesture.active, sense_gesture.active_owner, sense_gesture.active_target)) {
            memset(&sense_gesture.active, 0, sizeof sense_gesture.active);
            sense_gesture.release = 1; sense_gesture.release_tick = tick;
        }
    }
    if (sense_gesture.release) {
        if (sense_gesture.release_tick == tick) goto done;
        sense_gesture.release = 0;
    }
    if (!sense_gesture.active.action && sense_gesture.pending.action) {
        sense_gesture.active = sense_gesture.pending;
        sense_gesture.active_owner = sense_gesture.pending_owner; sense_gesture.active_target = sense_gesture.pending_target;
        memset(&sense_gesture.pending, 0, sizeof sense_gesture.pending);
        sense_gesture.active_tick = tick; ++sense_gesture.delivered;
    }
    if (sense_gesture.active.action) {
        action = sense_gesture.active.action;
        if (out) *out = sense_gesture.active;
    }
done:
    pthread_mutex_unlock(&sense_lock); return action;
}
int halo_sense_gesture_event_valid(const HaloSenseGesturePulse *event) {
    pthread_mutex_lock(&sense_lock);
    int valid = sense_gesture_event_locked(event, sense_now_ns());
    pthread_mutex_unlock(&sense_lock); return valid;
}
static void sense_gesture_status_locked(HaloSenseStatus *out, uint64_t now) {
    out->gestures_enabled = atomic_load(&sense_gestures_enabled);
    out->gesture_clutch = sense_gesture_clutch_locked(now);
    out->gesture_last_action = sense_gesture.last_action;
    out->gesture_accepted = sense_gesture.accepted;
    out->gesture_delivered = sense_gesture.delivered;
    out->gesture_dropped = sense_gesture.dropped;
}
