/* Hold/release ownership is separate from ordinary hand firing. A committed
 * release survives lowered/occluded hands, but never a session, source,
 * recenter, UI, menu or gameplay transition. All state is under sense_lock. */
static struct {
    HaloSenseGrenadeSample live,release;
    uint64_t live_ns,release_ns,release_sample_ns,session,event_epoch;
    uint32_t clock,last_tick,active_tick;
    int held,pending,active,delivered,have_clock;
} sense_throw;
static uint64_t sense_throw_epoch_counter;
/* The DI pulse that reached the original game this tick. It outlives any
 * cancellation of sense_throw until the next original tick's poll, so the
 * original's acceptance (0056E080) in this tick is always attributed: to the
 * release, or to a cancellation. It never survives into a later tick, so an
 * unaccepted pulse (no grenades, busy) cannot attach to any later throw. */
static struct { uint64_t epoch; uint32_t clock,tick; int live; } sense_throw_delivery;
static int sense_throw_owner_locked(uint64_t now) {
    return sense_throw.release_ns && now>=sense_throw.release_ns &&
        now-sense_throw.release_ns<=UINT64_C(1000000000) &&
        sense_throw.session==sense_session && sense_grenade_fresh_locked(now) &&
        sense_throw.release.controller_generation==sense_controller_generation &&
        sense_throw.release.generation==sense_head.generation;
}
static void sense_throw_cancel_locked(void) { memset(&sense_throw,0,sizeof sense_throw);sense_grenade_armed=0; }
static void sense_throw_input_changed_locked(void) {
    /* Fresh-head ownership below distinguishes hand occlusion from an actual
     * control transfer; do not discard an already committed throw here. */
    if(sense_app_blocked||!sense_enabled||!sense_session||!sense_gameplay_supported||
       (sense_throw.release_ns&&(sense_throw.session!=sense_session||sense_throw.release.controller_generation!=sense_controller_generation))||
       (sense_throw.live_ns&&sense_throw.live.controller_generation!=sense_controller_generation)){
        sense_throw_cancel_locked();
    }
}
static void sense_throw_observe_locked(uint64_t now) {
    if(!sense_grenade_fresh_locked(now)) { sense_throw_cancel_locked();return; }
    int pressed=sense_pad.buttons[HOSTGC_BTN_LSHOULDER];
    if(!pressed){
        /* Without measured release motion (occluded/stale anchor, tracking
         * re-prime) nothing commits: no grenade, never a pointing/head guess.
         * A committed or delivered, unaccepted release is never overwritten;
         * after a cancellation a new release may commit (and be delivered). */
        if(sense_throw.held && sense_throw.live_ns && now>=sense_throw.live_ns &&
           now-sense_throw.live_ns<=UINT64_C(150000000) && sense_throw.live.motion_valid &&
           sense_throw.live.controller_generation==sense_controller_generation &&
           sense_throw.live.generation==sense_head.generation &&
           !sense_throw.pending&&!sense_throw.active&&!sense_throw.delivered){
            sense_throw.release=sense_throw.live;sense_throw.release_ns=now;sense_throw.release_sample_ns=sense_throw.live_ns;
            sense_throw.session=sense_session;sense_throw.pending=1;sense_throw.delivered=0;
            if(++sense_throw_epoch_counter==0)++sense_throw_epoch_counter;sense_throw.event_epoch=sense_throw_epoch_counter;
        }
        sense_throw.held=0;sense_grenade_armed=1;
    }else if(sense_grenade_armed){sense_throw.held=1;sense_grenade_armed=0;}
}
void halo_sense_publish_grenade(uint64_t session,const HaloSenseGrenadeSample *sample){
    uint64_t now=sense_now_ns();pthread_mutex_lock(&sense_lock);
    if(session&&session==sense_session){
        int valid=sample&&sense_grenade_fresh_locked(now)&&sense_hand_valid(&sample->hand)&&
            sample->controller_generation==sense_controller_generation&&sample->generation==sense_head.generation;
        if(valid&&sample->motion_valid)for(unsigned i=0;i<3;i++)if(!isfinite(sample->velocity[i])||fabsf(sample->velocity[i])>20.f)valid=0;
        if(valid){
            if(sense_throw.live_ns&&(sense_throw.live.generation!=sample->generation||sense_throw.live.controller_generation!=sample->controller_generation))sense_throw_cancel_locked();
            sense_throw.live=*sample;sense_throw.live_ns=now;
        }
        else {sense_throw.live_ns=0;sense_throw.held=0;sense_grenade_armed=0;}
    }
    pthread_mutex_unlock(&sense_lock);
}
/* An original tick ended without acceptance: the pulse is spent. */
static void sense_throw_discard_locked(void) {
    if(sense_throw.delivered){sense_throw.delivered=0;sense_throw.release_ns=0;}
    sense_throw_delivery.live=0;
}
int halo_sense_grenade_pulse(uint32_t clock,uint32_t tick,int gameplay){
    uint64_t now=sense_now_ns();pthread_mutex_lock(&sense_lock);int down=0;
    if(sense_throw_delivery.live&&(!gameplay||clock!=sense_throw_delivery.clock||tick!=sense_throw_delivery.tick))sense_throw_discard_locked();
    if(!gameplay||!clock||(sense_throw.have_clock&&(clock!=sense_throw.clock||tick<sense_throw.last_tick))){
        sense_throw.pending=sense_throw.active=sense_throw.delivered=0;sense_throw.release_ns=0;
        sense_throw.have_clock=0;sense_throw.held=0;sense_grenade_armed=0;
        goto done;
    }
    sense_throw.clock=clock;sense_throw.last_tick=tick;sense_throw.have_clock=1;
    if(!sense_throw.release_ns)goto done; /* held L1 is never a down pulse */
    if(!sense_throw_owner_locked(now)){
        sense_throw.pending=sense_throw.active=sense_throw.delivered=0;sense_throw.release_ns=0;
        goto done;
    }
    if(sense_throw.active&&tick!=sense_throw.active_tick){sense_throw.active=0;goto done;}
    if(sense_throw.pending){
        sense_throw.pending=0;sense_throw.active=1;sense_throw.active_tick=tick;sense_throw.delivered=1;
        sense_throw_delivery.epoch=sense_throw.event_epoch;sense_throw_delivery.clock=clock;
        sense_throw_delivery.tick=tick;sense_throw_delivery.live=1;
    }
    down=sense_throw.active&&tick==sense_throw.active_tick;
done:pthread_mutex_unlock(&sense_lock);return down;
}
void halo_sense_grenade_ordinary_read(void){
    pthread_mutex_lock(&sense_lock);sense_throw_discard_locked();pthread_mutex_unlock(&sense_lock);
}
int halo_sense_grenade_accept(uint32_t clock,uint32_t tick,HaloSenseGrenadeRelease *out){
    pthread_mutex_lock(&sense_lock);uint64_t now=sense_now_ns();int result=0;
    if(out)memset(out,0,sizeof *out);
    /* Only on the delivery's own game clock, in its tick or a later tick of
     * the same frame (00470BF0 advances +0x0C after each tick; no DI poll
     * between them). Anything else is stale: spent, never attributed. */
    if(sense_throw_delivery.live&&(!clock||clock!=sense_throw_delivery.clock||tick<sense_throw_delivery.tick))sense_throw_discard_locked();
    if(sense_throw_delivery.live){
        result=-1;
        if(sense_throw.delivered&&sense_throw.event_epoch==sense_throw_delivery.epoch&&
           sense_throw_owner_locked(now)&&now>=sense_throw.release_sample_ns){
            if(out){out->sample=sense_throw.release;out->epoch=sense_throw.event_epoch;out->session=sense_throw.session;
                out->age_ns=now-sense_throw.release_sample_ns;}
            result=1;
        }
        /* Consumed by this acceptance either way; the rest of this tick's
         * stable pulse stays down, and nothing is left for a later throw. */
        sense_throw.delivered=0;sense_throw_delivery.live=0;
    }
    pthread_mutex_unlock(&sense_lock);return result;
}

int halo_sense_grenade_hold(HaloSenseGrenadeHold *out){
    pthread_mutex_lock(&sense_lock);uint64_t now=sense_now_ns();
    int held=sense_throw.held&&sense_grenade_fresh_locked(now)&&sense_fresh_locked(now)&&
        sense_pad.buttons[HOSTGC_BTN_LSHOULDER]&&sense_throw.live_ns&&now>=sense_throw.live_ns&&
        now-sense_throw.live_ns<=UINT64_C(150000000)&&
        sense_throw.live.controller_generation==sense_controller_generation&&
        sense_throw.live.generation==sense_head.generation;
    if(out){memset(out,0,sizeof *out);out->session=sense_session;out->generation=sense_input_generation;
        out->controller_generation=sense_controller_generation;out->held=held;}
    pthread_mutex_unlock(&sense_lock);return held;
}
uint64_t halo_sense_grenade_event_epoch(void){
    pthread_mutex_lock(&sense_lock);uint64_t epoch=sense_throw_owner_locked(sense_now_ns())?sense_throw.event_epoch:0;
    pthread_mutex_unlock(&sense_lock);return epoch;
}
