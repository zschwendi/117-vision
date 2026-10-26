/* Included under sense_lock's bridge ownership. Scope firing requires the
 * strict hand pair; the independent head keeps movement and L1 fallback safe. */
static int sense_head_fresh_locked(uint64_t now) {
    int valid = !sense_app_blocked && sense_enabled && halo_sense_requested() && sense_head_valid &&
        sense_session && now && sense_head_ns && now >= sense_head_ns &&
        now-sense_head_ns <= UINT64_C(150000000) && sense_head.controller_generation &&
        sense_head.controller_generation == sense_controller_generation;
    if (!valid) {
        if (sense_head_valid) sense_input_changed_locked();
        sense_head_valid = 0; sense_scope_armed = 0; sense_throw_cancel_locked();
    }
    return valid;
}
static int sense_scope_fresh_locked(uint64_t now) {
    int valid = sense_scope_active && sense_head_fresh_locked(now) && sense_fresh_locked(now) && sense_gameplay_supported &&
        sense_pad.connected && sense_pad.spatial && sense_pad.source_generation == sense_controller_generation &&
        sense_pad_ns && now >= sense_pad_ns && now-sense_pad_ns <= UINT64_C(150000000) &&
        !sense_pad.buttons[HOSTGC_BTN_MENU];
    if (!valid) sense_scope_armed = 0;
    return valid;
}
static int sense_grenade_fresh_locked(uint64_t now) {
    int valid=sense_head_fresh_locked(now)&&sense_gameplay_supported&&sense_pad.connected&&sense_pad.spatial&&
        sense_pad.source_generation==sense_controller_generation&&sense_pad_ns&&now>=sense_pad_ns&&
        now-sense_pad_ns<=UINT64_C(150000000)&&!sense_pad.buttons[HOSTGC_BTN_MENU];
    if(!valid)sense_grenade_armed=0;
    return valid;
}
void halo_sense_publish_head(uint64_t session, const HaloSenseHeadPose *pose) {
    uint64_t now = sense_now_ns();
    pthread_mutex_lock(&sense_lock);
    if (!session || session != sense_session) { pthread_mutex_unlock(&sense_lock); return; }
    HaloSenseHandPose validation = {.valid=1};
    if (pose) { memcpy(validation.forward,pose->forward,12); memcpy(validation.up,pose->up,12); }
    if (sense_app_blocked || !sense_enabled || !pose || !now || !pose->controller_generation ||
        pose->controller_generation != sense_controller_generation || !sense_hand_valid(&validation)) {
        if (sense_head_valid || sense_scope_armed) sense_input_changed_locked();
        sense_head_valid = sense_scope_armed = 0; sense_throw_cancel_locked();
        sense_head.movement_yaw = 0; sense_head.movement_yaw_valid = 0;
    } else {
        if (sense_head_valid && (now < sense_head_ns || now-sense_head_ns > UINT64_C(150000000) ||
            sense_head.generation != pose->generation)) { sense_input_changed_locked(); sense_throw_cancel_locked(); }
        sense_head=*pose; sense_head_ns=now; sense_head_valid=1;
        /* A bad heading never borrows forward; it only stops scoped movement. */
        float yaw = sense_head.movement_yaw;
        int heading = pose->movement_yaw_valid && isfinite(yaw) && fabsf(yaw) <= 6.2832f;
        sense_head.movement_yaw = heading ? remainderf(yaw, 6.28318531f) : 0; sense_head.movement_yaw_valid = heading;
        if (sense_scope_fresh_locked(now) && isfinite(sense_pad.rt) && sense_pad.rt >= 0 && sense_pad.rt < .25f &&
            !sense_pad.buttons[HOSTGC_BTN_RTRIGGER]) sense_scope_armed=1;
        if (sense_grenade_fresh_locked(now) && !sense_pad.buttons[HOSTGC_BTN_LSHOULDER]) sense_grenade_armed=1;
    }
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_head_read(HaloSenseHeadPose *out) {
    pthread_mutex_lock(&sense_lock);
    int valid=sense_head_fresh_locked(sense_now_ns());
    if(out){if(valid)*out=sense_head;else memset(out,0,sizeof *out);}
    pthread_mutex_unlock(&sense_lock);return valid;
}
void halo_sense_set_scope_active(int active) {
    pthread_mutex_lock(&sense_lock);
    active=!!active && sense_session && sense_enabled && halo_sense_requested();
    if(active!=sense_scope_active){sense_input_changed_locked();sense_scope_active=active;}
    pthread_mutex_unlock(&sense_lock);
}
int halo_sense_scope_input(HaloSenseScopeInput *out) {
    if(!out)return 0;
    pthread_mutex_lock(&sense_lock);
    int valid=sense_scope_fresh_locked(sense_now_ns());
    memset(out,0,sizeof *out);out->session=sense_session;out->generation=sense_input_generation;
    if(valid){out->head=sense_head;out->trigger=sense_pad.rt;out->armed=sense_scope_armed;}
    pthread_mutex_unlock(&sense_lock);return valid;
}

#include "sense_throw_input.inc"
