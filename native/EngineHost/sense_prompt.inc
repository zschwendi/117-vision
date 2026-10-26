/* halo_sense_prompt_context (sense_pose.h). Included at the end of
 * sense_pose.inc. The predicates restate sense_fresh_locked,
 * sense_head_fresh_locked, sense_scope_fresh_locked, sense_grenade_fresh_locked
 * and sense_gesture_transport_context_locked without their invalidation side
 * effects, and follow halo_sense_filter_gamepad's branch order exactly. Keep
 * them in step with those helpers. */
static int sense_prompt_pad_fresh_locked(uint64_t now) {
    return sense_pad.connected && sense_pad.spatial && sense_pad.source_generation == sense_controller_generation &&
        sense_pad_ns && now >= sense_pad_ns && now - sense_pad_ns <= UINT64_C(150000000) &&
        !sense_pad.buttons[HOSTGC_BTN_MENU];
}
static int sense_prompt_fresh_locked(uint64_t now) {
    return !sense_app_blocked && sense_enabled && halo_sense_requested() && sense_valid && now &&
        now >= sense_published_ns && now - sense_published_ns <= UINT64_C(150000000);
}
static int sense_prompt_head_fresh_locked(uint64_t now) {
    return !sense_app_blocked && sense_enabled && halo_sense_requested() && sense_head_valid &&
        sense_session && now && sense_head_ns && now >= sense_head_ns &&
        now - sense_head_ns <= UINT64_C(150000000) && sense_head.controller_generation &&
        sense_head.controller_generation == sense_controller_generation;
}
static int sense_prompt_grenade_fresh_locked(uint64_t now) {
    return sense_prompt_head_fresh_locked(now) && sense_gameplay_supported && sense_prompt_pad_fresh_locked(now);
}
int halo_sense_prompt_context(HaloSensePromptContext *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    pthread_mutex_lock(&sense_lock);
    uint64_t now = sense_now_ns();
    out->controller_generation = sense_controller_generation;
    if (!sense_enabled) out->context = HALO_SENSE_PROMPT_PASSTHROUGH;
    else if (sense_native_live_locked() && !sense_app_blocked) out->context = HALO_SENSE_PROMPT_SEATED;
    else {
        int head_controls = sense_prompt_grenade_fresh_locked(now);
        int fresh = sense_prompt_fresh_locked(now);
        if (sense_scope_active) {
            int scope = head_controls && fresh && sense_gameplay_supported;   /* sense_scope_fresh_locked */
            out->context = scope ? HALO_SENSE_PROMPT_SCOPE : head_controls ? HALO_SENSE_PROMPT_SCOPE_HEAD
                                                                           : HALO_SENSE_PROMPT_MENU_ONLY;
        } else if (!fresh || !sense_gameplay_supported) {
            out->context = fresh ? HALO_SENSE_PROMPT_USE_ONLY : HALO_SENSE_PROMPT_MENU_ONLY;
        } else {
            out->context = HALO_SENSE_PROMPT_TRACKED;
            out->gestures_own_create = atomic_load(&sense_gestures_enabled) && sense_session &&
                sense_prompt_pad_fresh_locked(now) && sense_controller_generation &&
                sense_frame.controller_generation == sense_controller_generation;
        }
        out->grenade_clutch = head_controls && out->context != HALO_SENSE_PROMPT_USE_ONLY &&
                              out->context != HALO_SENSE_PROMPT_MENU_ONLY;
    }
    pthread_mutex_unlock(&sense_lock);
    return out->context;
}
