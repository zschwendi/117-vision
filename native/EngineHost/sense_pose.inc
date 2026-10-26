/* Included by halo_settings.c so every existing host target gets the same
 * bridge. A mutex publishes the whole frame coherently; individual atomic
 * floats could combine one controller's old origin with a new direction. */
#include "sense_pose.h"
#include "sense_weapon.h"
#include "sense_mount.h"
#include <time.h>
static pthread_mutex_t sense_lock = PTHREAD_MUTEX_INITIALIZER;
static HaloSenseFrame sense_frame;
static HaloSenseHeadPose sense_head;
static uint64_t sense_head_ns;
static int sense_head_valid, sense_scope_active, sense_scope_armed, sense_grenade_armed, sense_app_blocked;
static uint64_t sense_published_ns;
static int sense_enabled, sense_valid, sense_fire_armed[2], sense_gameplay_supported;
static uint64_t sense_session, sense_session_counter;
static uint64_t sense_input_generation, sense_pad_ns;
static uint64_t sense_controller_generation;
static HostGCSnapshot sense_pad;
static HaloSenseReticleFrame sense_reticles;
static uint64_t sense_reticles_ns;
static _Atomic int sense_turn_mode;
static _Atomic float sense_turn_speed = 90.f;
static _Atomic float sense_turn_boundary = .72f;
static _Atomic int sense_user_enabled = 1;
static _Atomic int sense_dual_enabled;
static void sense_gesture_reset_locked(void);
static void sense_throw_input_changed_locked(void);
static void sense_throw_cancel_locked(void);
static void sense_throw_observe_locked(uint64_t now);
static int sense_gesture_clutch_locked(uint64_t now);
static int sense_gesture_scoreboard_locked(uint64_t now);
static void sense_gesture_status_locked(HaloSenseStatus *out, uint64_t now);
static void sense_native_revoke_locked(void);
static void sense_input_changed_locked(void) {
    if (++sense_input_generation == 0) ++sense_input_generation;
    sense_fire_armed[0] = sense_fire_armed[1] = 0;
    sense_scope_armed = 0;
    sense_grenade_armed = 0;
    sense_throw_input_changed_locked();
    memset(&sense_reticles, 0, sizeof sense_reticles); sense_reticles_ns = 0;
    sense_gesture_reset_locked();
}
static void sense_invalidate_locked(void) {
    if (sense_valid || sense_fire_armed[0] || sense_fire_armed[1]) sense_input_changed_locked();
    sense_valid = 0;
}
static uint64_t sense_now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
static int sense_hand_valid(const HaloSenseHandPose *hand) {
    if (!hand->valid || !isfinite(hand->trigger) || hand->trigger < 0 || hand->trigger > 1) return 0;
    float length = 0, up_length = 0, dot = 0, reach = 0;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(hand->position[i]) || !isfinite(hand->forward[i]) || !isfinite(hand->up[i])) return 0;
        length += hand->forward[i] * hand->forward[i];
        up_length += hand->up[i] * hand->up[i];
        dot += hand->forward[i] * hand->up[i];
        reach += hand->position[i] * hand->position[i];
    }
    return fabsf(length - 1) < .01f && fabsf(up_length - 1) < .01f && fabsf(dot) < .01f && reach <= 16.f;
}
static int sense_fresh_locked(uint64_t now) {
    if (sense_app_blocked || !sense_enabled || !halo_sense_requested() || !sense_valid || !now || now < sense_published_ns || now - sense_published_ns > UINT64_C(150000000)) {
        sense_invalidate_locked(); return 0;
    }
    return 1;
}
#include "sense_head.inc"
void halo_sense_app_input_changed(int blocked) {
    pthread_mutex_lock(&sense_lock);blocked=!!blocked;
    if(sense_app_blocked!=blocked){sense_app_blocked=blocked;sense_valid=sense_head_valid=0;sense_input_changed_locked();}
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_app_input_blocked(void) {
    pthread_mutex_lock(&sense_lock);int blocked=sense_app_blocked;pthread_mutex_unlock(&sense_lock);return blocked;
}
void halo_sense_set_turn_mode(int mode) { atomic_store(&sense_turn_mode, mode == 1 || mode == 2 || mode == 30 || mode == 45 ? mode : 0); }
int halo_sense_turn_mode(void) { return atomic_load(&sense_turn_mode); }
void halo_sense_set_turn_speed(float speed) {
    atomic_store(&sense_turn_speed, isfinite(speed) ? fmaxf(30.f, fminf(120.f, speed)) : 90.f);
}
float halo_sense_turn_speed(void) { return atomic_load(&sense_turn_speed); }
void halo_sense_set_turn_boundary(float boundary) {
    atomic_store(&sense_turn_boundary, isfinite(boundary) ? fmaxf(.50f, fminf(.90f, boundary)) : .72f);
}
float halo_sense_turn_boundary(void) { return atomic_load(&sense_turn_boundary); }
int halo_sense_requested(void) {
    const char *requested = getenv("HALO_SENSE_WEAPON");
    return atomic_load(&sense_user_enabled) && (!requested || strcmp(requested, "0") != 0);
}
void halo_sense_set_enabled(int enabled) {
    atomic_store(&sense_user_enabled, !!enabled);
    if (!enabled) halo_sense_set_active(0);
}
int halo_sense_enabled(void) { return atomic_load(&sense_user_enabled); }
void halo_sense_set_dual_enabled(int enabled) {
    enabled = !!enabled;
    pthread_mutex_lock(&sense_lock);
    if (atomic_exchange(&sense_dual_enabled, enabled) != enabled) sense_input_changed_locked();
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_dual_enabled(void) { return atomic_load(&sense_dual_enabled); }
int halo_sense_dual_requested(void) { return halo_sense_requested() && halo_sense_dual_enabled(); }
static _Atomic int sense_grip_continuity_enabled = 1, sense_scope_smoothing_enabled, sense_weapon_haptics_enabled = 1;
static void sense_set_weapon_feel(_Atomic int *field, int enabled) {
    enabled = !!enabled;
    pthread_mutex_lock(&sense_lock);
    if (atomic_exchange(field, enabled) != enabled) sense_input_changed_locked();
    pthread_mutex_unlock(&sense_lock);
}
void halo_sense_set_grip_continuity(int enabled) { sense_set_weapon_feel(&sense_grip_continuity_enabled, enabled); }
int halo_sense_grip_continuity(void) { return atomic_load(&sense_grip_continuity_enabled); }
void halo_sense_set_scope_smoothing(int enabled) { sense_set_weapon_feel(&sense_scope_smoothing_enabled, enabled); }
int halo_sense_scope_smoothing(void) { return atomic_load(&sense_scope_smoothing_enabled); }
/* Haptic ownership never changes the input generation: toggling feedback
 * must not disarm a held trigger. It revokes the lease instead. */
static int sense_haptic_owned;
static uint64_t sense_haptic_session, sense_haptic_generation, sense_haptic_lease, sense_haptic_lease_counter, sense_haptic_owner_ns;
void halo_sense_set_weapon_haptics(int enabled) {
    pthread_mutex_lock(&sense_lock);
    atomic_store(&sense_weapon_haptics_enabled, !!enabled);
    if (!enabled) { sense_haptic_owned = 0; sense_haptic_lease = 0; }
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_weapon_haptics(void) { return atomic_load(&sense_weapon_haptics_enabled); }
uint64_t halo_sense_set_weapon_haptic_owner(int owned, uint64_t session, uint64_t generation) {
    pthread_mutex_lock(&sense_lock);
    owned = owned && session && session == sense_session && generation == sense_input_generation &&
        atomic_load(&sense_weapon_haptics_enabled);
    if (!owned) { sense_haptic_owned = 0; sense_haptic_lease = 0; }
    else {
        if (!sense_haptic_owned || sense_haptic_session != session || sense_haptic_generation != generation) {
            if (++sense_haptic_lease_counter == 0) ++sense_haptic_lease_counter;
            sense_haptic_lease = sense_haptic_lease_counter;
        }
        sense_haptic_owned = 1; sense_haptic_session = session; sense_haptic_generation = generation;
        sense_haptic_owner_ns = sense_now_ns();
    }
    uint64_t lease = sense_haptic_lease;
    pthread_mutex_unlock(&sense_lock); return lease;
}
int halo_sense_weapon_haptics_admit(uint64_t session, uint64_t generation, uint64_t lease) {
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    int valid = lease && session && atomic_load(&sense_weapon_haptics_enabled) && sense_haptic_owned &&
        lease == sense_haptic_lease && session == sense_session && session == sense_haptic_session &&
        generation == sense_input_generation && generation == sense_haptic_generation &&
        now >= sense_haptic_owner_ns && now - sense_haptic_owner_ns <= UINT64_C(250000000) &&
        !sense_app_blocked && sense_gameplay_supported && sense_fresh_locked(now);
    pthread_mutex_unlock(&sense_lock);
    return valid && halo_settings_haptics_strength() > 0.f;
}
uint64_t halo_sense_begin_session(void) {
    pthread_mutex_lock(&sense_lock);
    if (++sense_session_counter == 0) ++sense_session_counter;
    sense_session = sense_session_counter;
    sense_enabled = 0; sense_valid = 0; sense_head_valid = sense_scope_active = 0; sense_input_changed_locked();
    memset(&sense_pad, 0, sizeof sense_pad); sense_pad_ns = 0;
    sense_gameplay_supported = 0;
    uint64_t session = sense_session;
    pthread_mutex_unlock(&sense_lock); return session;
}
void halo_sense_end_session(uint64_t session) {
    pthread_mutex_lock(&sense_lock);
    if (session && session == sense_session) {
        sense_session = 0;
        sense_enabled = 0; sense_valid = 0; sense_head_valid = sense_scope_active = 0; sense_input_changed_locked();
        memset(&sense_pad, 0, sizeof sense_pad); sense_pad_ns = 0;
        sense_gameplay_supported = 0;
    }
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_session_active(void) {
    pthread_mutex_lock(&sense_lock); int value = sense_session != 0; pthread_mutex_unlock(&sense_lock); return value;
}
static void sense_set_active_locked(int active) {
    active = active && sense_session;
    if (sense_enabled != !!active) { sense_input_changed_locked(); sense_valid = 0; }
    if (!active) { sense_invalidate_locked(); sense_head_valid = sense_scope_active = 0; sense_throw_cancel_locked(); sense_native_revoke_locked(); }
    sense_enabled = !!active;
}
static void sense_controller_changed_locked(uint64_t generation) {
    if (generation && generation != sense_controller_generation) {
        sense_controller_generation = generation;
        sense_valid = 0; sense_head_valid = sense_scope_active = 0; sense_input_changed_locked();
        memset(&sense_pad, 0, sizeof sense_pad); sense_pad_ns = 0;
    }
}
void halo_sense_controller_changed(uint64_t generation) {
    pthread_mutex_lock(&sense_lock);
    /* A late notification must not roll ownership back after a newer poll. */
    if (generation > sense_controller_generation) sense_controller_changed_locked(generation);
    pthread_mutex_unlock(&sense_lock);
}
void halo_sense_set_active(int active) {
    active = active && halo_sense_requested();
    pthread_mutex_lock(&sense_lock);
    sense_set_active_locked(active);
    pthread_mutex_unlock(&sense_lock);
}
void halo_sense_observe_controller(const HostGCSnapshot *pad) {
    if (!pad || !pad->connected) return;
    int active = pad->spatial && halo_sense_requested();
    pthread_mutex_lock(&sense_lock);
    if (pad->source_generation && pad->source_generation < sense_controller_generation) {
        pthread_mutex_unlock(&sense_lock); return;
    }
    sense_controller_changed_locked(pad->source_generation);
    sense_set_active_locked(active);
    if (sense_session) { sense_pad = *pad; sense_pad_ns = sense_now_ns(); sense_throw_observe_locked(sense_pad_ns); }
    pthread_mutex_unlock(&sense_lock);
}
void halo_sense_observe_controller_for_session(uint64_t session, const HostGCSnapshot *pad) {
    if (!pad || !pad->connected) return;
    int active = pad->spatial && halo_sense_requested();
    pthread_mutex_lock(&sense_lock);
    if (session && session == sense_session) {
        if (!pad->source_generation || pad->source_generation >= sense_controller_generation) {
            sense_controller_changed_locked(pad->source_generation);
            sense_set_active_locked(active); sense_pad = *pad; sense_pad_ns = sense_now_ns(); sense_throw_observe_locked(sense_pad_ns);
        }
    }
    pthread_mutex_unlock(&sense_lock);
}
void halo_sense_set_gameplay_supported(int supported) {
    pthread_mutex_lock(&sense_lock);
    if (sense_gameplay_supported != !!supported) sense_input_changed_locked();
    if (!supported) { sense_fire_armed[0] = sense_fire_armed[1] = 0; sense_throw_cancel_locked(); }
    sense_gameplay_supported = !!supported;
    pthread_mutex_unlock(&sense_lock);
}
/* Native seat controls (sense_mount.h). A publication is a lease held by the
 * Sense owner that received it: the session and controller connection, and
 * revoked whenever Sense stops owning input (switched off, ordinary pad,
 * session end) even on the same connection. Ownership changes fail closed
 * until the engine publishes again; hand tracking, pose and setting changes
 * do not touch the committed seat presentation. R2 arms once released in the
 * current input generation, which every identity change advances. */
static int sense_native;
static uint64_t sense_native_mount_generation, sense_native_session, sense_native_controller;
static uint64_t sense_native_armed_generation, sense_seat_transitions;
static int sense_native_live_locked(void) {
    return sense_native && sense_enabled && sense_session && sense_native_session == sense_session &&
        sense_native_controller == sense_controller_generation;
}
static void sense_native_revoke_locked(void) { sense_native_session = 0; } /* never moves the seat token */
void halo_sense_set_native_controls(int native, uint64_t mount_generation) {
    pthread_mutex_lock(&sense_lock); native = !!native;
    /* Every identity change (seat, death, respawn, ownership) ends held
     * input; only seat boundaries move the presentation token. */
    if (native != sense_native || mount_generation != sense_native_mount_generation) {
        if (native != sense_native || native) { if (++sense_seat_transitions == 0) ++sense_seat_transitions; }
        sense_input_changed_locked(); sense_throw_cancel_locked();
    }
    sense_native = native; sense_native_mount_generation = mount_generation;
    sense_native_session = sense_session; sense_native_controller = sense_controller_generation;
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_native_controls(void) {
    pthread_mutex_lock(&sense_lock); int value = sense_native_live_locked(); pthread_mutex_unlock(&sense_lock); return value;
}
uint64_t halo_sense_seat_transition(void) {
    pthread_mutex_lock(&sense_lock); uint64_t value = sense_seat_transitions; pthread_mutex_unlock(&sense_lock); return value;
}
void halo_sense_seat_snapshot(HaloSenseSeatSnapshot *out) {
    if (!out) return;
    pthread_mutex_lock(&sense_lock); out->native = sense_native_live_locked(); out->transition = sense_seat_transitions; pthread_mutex_unlock(&sense_lock);
}
int halo_sense_gameplay_supported(void) {
    pthread_mutex_lock(&sense_lock); int value = sense_gameplay_supported; pthread_mutex_unlock(&sense_lock); return value;
}
int halo_sense_active(void) {
    pthread_mutex_lock(&sense_lock); int value = sense_enabled; pthread_mutex_unlock(&sense_lock); return value;
}
void halo_sense_publish(uint64_t session, const HaloSenseFrame *frame) {
    uint64_t now = sense_now_ns();
    pthread_mutex_lock(&sense_lock);
    if (!session || session != sense_session) { pthread_mutex_unlock(&sense_lock); return; }
    int prior_fresh = sense_fresh_locked(now);
    if (sense_app_blocked || !sense_enabled || !frame || !frame->controller_generation || frame->controller_generation != sense_controller_generation ||
        !isfinite(frame->head_yaw) || !sense_hand_valid(&frame->left) || !sense_hand_valid(&frame->right)) {
        sense_invalidate_locked();
    } else {
        if (!prior_fresh || sense_frame.generation != frame->generation) sense_input_changed_locked();
        if (frame->left.trigger < .25f) sense_fire_armed[0] = 1;
        if (frame->right.trigger < .25f) sense_fire_armed[1] = 1;
        sense_frame = *frame; sense_published_ns = now; sense_valid = now != 0;
        /* Optional head position never invalidates the hands; it is kept
         * only when finite and within the same 4 m bound as the hands. */
        if (sense_frame.head_position_valid) {
            const float *h = sense_frame.head_position;
            if (!isfinite(h[0]) || !isfinite(h[1]) || !isfinite(h[2]) || h[0]*h[0]+h[1]*h[1]+h[2]*h[2] > 16.f) {
                memset(sense_frame.head_position, 0, sizeof sense_frame.head_position); sense_frame.head_position_valid = 0;
            }
        }
    }
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_read(HaloSenseFrame *out) {
    pthread_mutex_lock(&sense_lock);
    int valid = sense_fresh_locked(sense_now_ns());
    if (out) { if (valid) *out = sense_frame; else memset(out, 0, sizeof *out); }
    pthread_mutex_unlock(&sense_lock); return valid;
}
int halo_sense_weapon_pose(unsigned hand, float position[3], float forward[3], float up[3]) {
    HaloSenseFrame frame;
    if (hand > 1 || !position || !forward || !up || !halo_sense_read(&frame)) return 0;
    const HaloSenseHandPose *pose = hand == 0 ? &frame.left : &frame.right;
    memcpy(position, pose->position, sizeof pose->position);
    memcpy(forward, pose->forward, sizeof pose->forward);
    memcpy(up, pose->up, sizeof pose->up); return 1;
}
int halo_sense_dual_input(HaloSenseDualInput *out) {
    if (!out) return 0;
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    int fresh = sense_fresh_locked(now) && sense_pad.connected && sense_pad.spatial &&
        sense_pad_ns && now >= sense_pad_ns && now - sense_pad_ns <= UINT64_C(150000000);
    if (!fresh && (sense_fire_armed[0] || sense_fire_armed[1])) sense_input_changed_locked();
    memset(out, 0, sizeof *out);
    out->session = sense_session; out->generation = sense_input_generation;
    out->enabled = halo_sense_dual_requested(); out->spatial = sense_enabled;
    out->fresh = fresh; out->gameplay_supported = sense_gameplay_supported;
    if (fresh) {
        out->trigger[0] = sense_pad.lt; out->trigger[1] = sense_pad.rt;
        /* Square requests independent original reloads for both inventory
         * weapons in dual mode; its existing primary Use/Reload stays intact. */
        out->reload[0] = out->reload[1] = sense_pad.buttons[HOSTGC_BTN_X];
        out->armed[0] = sense_fire_armed[0]; out->armed[1] = sense_fire_armed[1];
        out->support_grip = sense_pad.buttons[HOSTGC_BTN_LSHOULDER];
        out->support_blocked = sense_app_blocked || sense_pad.buttons[HOSTGC_BTN_LSHOULDER] ||
            sense_pad.buttons[HOSTGC_BTN_OPTIONS] || sense_pad.buttons[HOSTGC_BTN_X] || sense_pad.buttons[HOSTGC_BTN_Y] ||
            sense_pad.buttons[HOSTGC_BTN_HOME] || sense_pad.dpad_up || sense_pad.dpad_down || sense_pad.dpad_left || sense_pad.dpad_right;
    }
    pthread_mutex_unlock(&sense_lock); return fresh;
}
void halo_sense_status(HaloSenseStatus *out) {
    if (!out) return;
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    int fresh = sense_fresh_locked(now);
    memset(out, 0, sizeof *out);
    out->session = sense_session; out->generation = sense_input_generation;
    out->published_age_ns = sense_published_ns && now >= sense_published_ns ? now - sense_published_ns : UINT64_MAX;
    out->controller_generation = sense_controller_generation;
    out->published_controller_generation = sense_frame.controller_generation;
    out->requested = halo_sense_requested(); out->enabled = halo_sense_enabled();
    out->dual_requested = halo_sense_dual_requested(); out->active = sense_enabled;
    out->valid = fresh; out->gameplay_supported = sense_gameplay_supported;
    out->armed[0] = sense_fire_armed[0]; out->armed[1] = sense_fire_armed[1];
    out->support_grip = fresh && sense_pad.connected && sense_pad.spatial && sense_pad_ns &&
        now >= sense_pad_ns && now - sense_pad_ns <= UINT64_C(150000000) &&
        sense_pad.buttons[HOSTGC_BTN_LSHOULDER];
    sense_gesture_status_locked(out, now);
    pthread_mutex_unlock(&sense_lock);
}
static int sense_reticle_valid(const HaloSenseReticle *r) {
    if (!r->valid) return 0;
    float direction_length = 0, origin_length = 0;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(r->origin[i]) || !isfinite(r->direction[i]) || !isfinite(r->point[i])) return 0;
        direction_length += r->direction[i] * r->direction[i];
        origin_length += r->origin[i] * r->origin[i];
    }
    return fabsf(direction_length - 1) < .01f && origin_length <= 16.f;
}
void halo_sense_reticle_publish(const HaloSenseReticleFrame *frame) {
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    if (!frame) { memset(&sense_reticles, 0, sizeof sense_reticles); sense_reticles_ns = 0; }
    else if (frame->session && frame->session == sense_session && frame->generation == sense_input_generation &&
             sense_fresh_locked(now) && sense_gameplay_supported) {
        sense_reticles = *frame;
        for (unsigned hand = 0; hand < 2; ++hand)
            if (!sense_reticle_valid(&frame->hands[hand])) memset(&sense_reticles.hands[hand], 0, sizeof sense_reticles.hands[hand]);
        sense_reticles_ns = now;
    }
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_reticle_read(HaloSenseReticleFrame *frame) {
    if (!frame) return 0;
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    int fresh = sense_fresh_locked(now) && sense_gameplay_supported && sense_reticles_ns &&
        now >= sense_reticles_ns && now - sense_reticles_ns <= UINT64_C(150000000) &&
        sense_reticles.session == sense_session && sense_reticles.generation == sense_input_generation;
    if (fresh) *frame = sense_reticles; else memset(frame, 0, sizeof *frame);
    int valid = fresh && (frame->hands[0].valid || frame->hands[1].valid);
    pthread_mutex_unlock(&sense_lock); return valid;
}
void halo_sense_filter_gamepad(HostGCSnapshot *pad) {
    if (!pad || !pad->connected) return;
    halo_sense_observe_controller(pad);
    if (!pad->spatial) {
        /* Explicit conventional-pad ownership is different from missing or
         * stale Sense tracking. Stock input must not inherit a Sense gate. */
        return;
    }
    pthread_mutex_lock(&sense_lock);
    if (!sense_enabled) { pthread_mutex_unlock(&sense_lock); return; }
    if (sense_native_live_locked() && !sense_app_blocked) {
        /* Seated: an ordinary pad for the original seat. Sticks aim and move
         * unrotated; tracking is not needed. L1/L2 stay withheld as on foot. */
        bool held = (isfinite(pad->rt) && pad->rt >= .25f) || pad->buttons[HOSTGC_BTN_RTRIGGER];
        if (!held) sense_native_armed_generation = sense_input_generation;
        if (sense_native_armed_generation != sense_input_generation) { pad->rt = 0; pad->buttons[HOSTGC_BTN_RTRIGGER] = false; }
        pad->buttons[HOSTGC_BTN_LSHOULDER] = false;
        pad->lt = 0; pad->buttons[HOSTGC_BTN_LTRIGGER] = false;
        pthread_mutex_unlock(&sense_lock); return;
    }
    int head_controls=sense_grenade_fresh_locked(sense_now_ns());

    if (sense_scope_active) {
        /* Scope uses the gun pose. Fresh head/controller input still permits
         * zoom exit and the original grenade/melee buttons after hand loss. */
        uint64_t now = sense_now_ns();
        if (!sense_scope_fresh_locked(now)) {
            bool menu = pad->buttons[HOSTGC_BTN_MENU];
            bool zoom=head_controls&&pad->buttons[HOSTGC_BTN_RTHUMB],melee=head_controls&&pad->buttons[HOSTGC_BTN_RSHOULDER];
            uint64_t sequence = pad->sequence, generation = pad->source_generation;
            memset(pad, 0, sizeof *pad); pad->connected = pad->spatial = true;
            pad->sequence = sequence; pad->source_generation = generation;
            pad->buttons[HOSTGC_BTN_MENU] = menu;
            pad->buttons[HOSTGC_BTN_RTHUMB]=zoom;pad->buttons[HOSTGC_BTN_RSHOULDER]=melee;
        } else {
            float x = isfinite(pad->lx) ? pad->lx : 0, y = isfinite(pad->ly) ? pad->ly : 0;
            /* Pitch-free heading: the tilted gaze in forward[] reverses
             * movement once physical plus virtual pitch passes vertical. */
            float yaw = sense_head.movement_yaw_valid ? sense_head.movement_yaw : 0;
            float c = cosf(yaw), s = sinf(yaw);
            if (!sense_head.movement_yaw_valid) x = y = 0;
            pad->lx = fmaxf(-1, fminf(1, x*c+y*s)); pad->ly = fmaxf(-1, fminf(1, y*c-x*s));
            pad->rx = pad->ry = 0;
            if (!sense_scope_armed) { pad->rt = 0; pad->buttons[HOSTGC_BTN_RTRIGGER] = false; }
        }
        pad->buttons[HOSTGC_BTN_LSHOULDER]=false;
        pad->lt = 0; pad->buttons[HOSTGC_BTN_LTRIGGER] = false;
        pthread_mutex_unlock(&sense_lock); return;
    }
    int valid = sense_fresh_locked(sense_now_ns());
    if (!valid || !sense_gameplay_supported) {
        bool menu = pad->buttons[HOSTGC_BTN_MENU];
        /* Fresh Use still acknowledges the unarmed tutorial and exits a
         * vehicle. Lost tracking retains only Pause, never an action. */
        bool use = valid && pad->buttons[HOSTGC_BTN_X];
        uint64_t sequence = pad->sequence, source_generation = pad->source_generation;
        memset(pad, 0, sizeof *pad);
        pad->connected = pad->spatial = true; pad->sequence = sequence;
        pad->source_generation = source_generation;
        pad->buttons[HOSTGC_BTN_MENU] = menu;
        pad->buttons[HOSTGC_BTN_X] = use;
        pad->buttons[HOSTGC_BTN_LSHOULDER]=false;
    } else {
        float x = isfinite(pad->lx) ? pad->lx : 0, y = isfinite(pad->ly) ? pad->ly : 0;
        float c = cosf(sense_frame.head_yaw), s = sinf(sense_frame.head_yaw);
        pad->lx = fmaxf(-1, fminf(1, x * c + y * s));
        pad->ly = fmaxf(-1, fminf(1, y * c - x * s));
        pad->rx = pad->ry = 0;
        if (!sense_fire_armed[1]) { pad->rt = 0; pad->buttons[HOSTGC_BTN_RTRIGGER] = false; }
        /* Raw L1 is a throw clutch. Only the DI release pulse may request
         * the original grenade action; ordinary gamepads bypass this filter. */
        pad->buttons[HOSTGC_BTN_LSHOULDER] = false;
        /* Centered Create is a deliberate gesture clutch. Create+R3 retains
         * the scoreboard, consuming only that chord's old R3 zoom action.
         * Directional shortcuts and every ordinary pad remain unchanged. */
        uint64_t now = sense_now_ns();
        if (sense_gesture_scoreboard_locked(now)) pad->buttons[HOSTGC_BTN_RTHUMB] = false;
        else if (sense_gesture_clutch_locked(now)) pad->buttons[HOSTGC_BTN_OPTIONS] = false;
    }
    /* L2 belongs only to the offhand. Never also reach old gamepad zoom. */
    pad->lt = 0; pad->buttons[HOSTGC_BTN_LTRIGGER] = false;
    pthread_mutex_unlock(&sense_lock);
}
#include "sense_gesture.inc"
