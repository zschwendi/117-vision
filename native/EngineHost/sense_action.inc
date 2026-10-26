/* Original retail1.10 action priority. Verified005659C0/005625B0 use BYTE
 * unit+320 current zoom, +321 desired; FF means no zoom. Melee starts in
 *00569A20 (unit+289=1 or4); local00559D39 event4 maps through004927C0's
 *verified492830 table to FP animation class10.493059 stores class at+0C. */
static pthread_mutex_t sense_action_lock=PTHREAD_MUTEX_INITIALIZER;
static HaloSenseWeaponActionStatus sense_action_state,sense_scope_frame_state;
static uint64_t sense_action_ns,sense_scope_frame_ns,sense_action_counter;
static int sense_scope_nested;
/* First-person animation class (FP cache +0x0C, written by 00492E60) to the
 * action that owns the rig. Verified on c9acf0c4...: 00492880 maps class to
 * the graph's animation slot through its table at 00492924 (10->13 melee,
 * 13->7 reload-empty, 14->8 reload-full, 15->23 enter, 16->24 exit-empty,
 * 17->25 exit-full, 18->11 put-away, 19->10 ready), whose names the retail
 * first-person graphs carry. Emitters: put-away 004C28F0 (004940F0 event
 * 0x0B, 004927C0 -> 18), ready 004C2840 (event 0x0C -> 19), reload events
 * 9/10 (-> 13/14, or 13/15 for the loop reload at 004941B5..00494276). The
 * class stays set for the original animation's own duration; 00492D20 moves
 * it on when that animation ends. */
static int sense_fp_class_action(int animation_class) {
    if(animation_class==10)return HALO_SENSE_ACTION_MELEE;
    if(animation_class>=13&&animation_class<=17)return HALO_SENSE_ACTION_RELOAD;
    if(animation_class==18||animation_class==19)return HALO_SENSE_ACTION_SWAP;
    return HALO_SENSE_ACTION_NONE;
}
/* The original animation owns the whole viewmodel rig (no tracked hands). */
static int sense_action_native(int action) {
    return action==HALO_SENSE_ACTION_MELEE||action==HALO_SENSE_ACTION_RELOAD||action==HALO_SENSE_ACTION_SWAP;
}
/* Reload and swap belong to the primary viewmodel only: the dual offhand,
 * its own original update and its reticle stay live meanwhile. */
static int sense_action_offhand_live(int action) {
    return action==HALO_SENSE_ACTION_NONE||action==HALO_SENSE_ACTION_RELOAD||action==HALO_SENSE_ACTION_SWAP;
}
static int sense_physical_reload_owned(uint32_t datum,uint32_t weapon); /* sense_physical_hooks.inc */
static int sense_action_for_unit(uint32_t unit,uint32_t datum,uint32_t weapon,int *zoom) {
    if(zoom)*zoom=-1;
    if(!unit||sense_object(datum,0x322,0)!=unit)return HALO_SENSE_ACTION_NONE;
    uint32_t cache=G32(0x006B2D98u);
    int fp=sense_range(cache,0x28)&&G8(cache)&&G32(cache+4)==datum&&G32(cache+8)==weapon?
        sense_fp_class_action((int16_t)G16(cache+0xC)):HALO_SENSE_ACTION_NONE;
    if((G8(unit+0x289)>0&&G8(unit+0x289)<=4)||fp==HALO_SENSE_ACTION_MELEE)return HALO_SENSE_ACTION_MELEE;
    /* A reload the physical magazine committed keeps its own pinned,
     * tracked presentation (sense_physical_hooks.inc); the original reload
     * still runs its timer and transfer. */
    if(fp==HALO_SENSE_ACTION_RELOAD&&sense_physical_reload_owned(datum,weapon))fp=HALO_SENSE_ACTION_NONE;
    if(fp!=HALO_SENSE_ACTION_NONE)return fp;
    int level=(int8_t)G8(unit+0x320);
    if(level>=0&&level<=3){if(zoom)*zoom=level;return HALO_SENSE_ACTION_SCOPE;}
    return HALO_SENSE_ACTION_NONE;
}
static int sense_world_head(uint32_t unit,uint32_t datum,HaloSenseTransform *out,float eye[3]) {
    HaloSenseHeadPose head;uint64_t now=host_monotonic_ns();
    if(sense_camera_unit!=datum||now<sense_camera_ns||now-sense_camera_ns>UINT64_C(250000000)||
       !halo_sense_head_read(&head))return 0;
    float f[3],u[3];sense_rotate(&sense_camera,head.forward,f);sense_rotate(&sense_camera,head.up,u);
    if(!sense_basis(f,u,out))return 0;
    memcpy(eye,GPTR(unit+0x5C),12);for(unsigned k=0;k<3;k++)eye[k]+=sense_camera_from_unit[k];
    memcpy(out->p,eye,12);return sense_finite3(eye);
}
/* The current-slot weapon while another slot is desired: the put-away half
 * of an original switch. 0056D6E0 calls put-away 004C28F0 on +0x2F2 while
 * +0x2F4 already names the new slot, and only then copies +0x2F4 into +0x2F2
 * and calls ready 004C2840. Only a native animation (the put-away itself, or
 * a melee/reload interrupting it) may use this identity. */
static uint32_t sense_switching_unit(uint32_t *datum,uint32_t *weapon) {
    uint32_t unit=sense_local_body(datum);if(!unit)return 0;
    int slot=(int16_t)G16(unit+0x2F2);if(slot<0||slot>=4)return 0;
    *weapon=G32(unit+0x2F8+4u*(unsigned)slot);uint32_t w=sense_object(*weapon,0x340,2);
    if(!w||G32(w+0x11C)!=*datum||G16(0x00719720u)!=0||G16(unit+0x2F0)!=UINT16_MAX||G32(unit+0x11C)!=UINT32_MAX)return 0;
    return sense_action_native(sense_action_for_unit(unit,*datum,*weapon,NULL))?unit:0;
}
static void sense_action_update(void) {
    HaloSenseWeaponActionStatus next={.zoom_level=-1,.weapon=UINT32_MAX};
    uint32_t datum,weapon,unit=sense_local_unit(&datum,&weapon);
    if(!unit)unit=sense_switching_unit(&datum,&weapon);
    if(unit){next.weapon=weapon;next.action=sense_action_for_unit(unit,datum,weapon,&next.zoom_level);}
    halo_sense_set_scope_active(next.action==HALO_SENSE_ACTION_SCOPE);
    HaloSenseStatus status;halo_sense_status(&status);
    next.session=status.session;next.generation=status.generation;
    pthread_mutex_lock(&sense_action_lock);
    if(next.session!=sense_action_state.session||next.generation!=sense_action_state.generation||
       next.weapon!=sense_action_state.weapon||next.action!=sense_action_state.action||next.zoom_level!=sense_action_state.zoom_level){
        if(++sense_action_counter==0)++sense_action_counter;
    }
    next.mode_generation=sense_action_counter;sense_action_state=next;sense_action_ns=host_monotonic_ns();
    pthread_mutex_unlock(&sense_action_lock);
}
int host_sense_weapon_action_status(HaloSenseWeaponActionStatus *out) {
    if(!out)return 0;HaloSenseStatus status;halo_sense_status(&status);uint64_t now=host_monotonic_ns();
    pthread_mutex_lock(&sense_action_lock);
    int valid=!halo_sense_app_input_blocked()&&status.active&&status.requested&&status.gameplay_supported&&status.session==sense_action_state.session&&
        status.generation==sense_action_state.generation&&sense_action_ns&&now>=sense_action_ns&&now-sense_action_ns<=UINT64_C(250000000);
    if(valid)*out=sense_action_state;else memset(out,0,sizeof *out);
    pthread_mutex_unlock(&sense_action_lock);return valid;
}
static void sense_scope_clear_frame(void) {
    pthread_mutex_lock(&sense_action_lock);memset(&sense_scope_frame_state,0,sizeof sense_scope_frame_state);
    sense_scope_frame_ns=0;pthread_mutex_unlock(&sense_action_lock);
}
int host_sense_scope_rendering(void){return sense_scope_nested;}
int host_sense_scope_frame(HaloSenseWeaponActionStatus *out) {
    if(!out)return 0;HaloSenseWeaponActionStatus live;
    if(!host_sense_weapon_action_status(&live)){memset(out,0,sizeof *out);return 0;}
    pthread_mutex_lock(&sense_action_lock);uint64_t now=host_monotonic_ns();
    int valid=live.action==HALO_SENSE_ACTION_SCOPE&&sense_scope_frame_ns&&now>=sense_scope_frame_ns&&
        now-sense_scope_frame_ns<=UINT64_C(250000000)&&live.mode_generation==sense_scope_frame_state.mode_generation&&
        live.session==sense_scope_frame_state.session&&live.generation==sense_scope_frame_state.generation;
    if(valid)*out=sense_scope_frame_state;else memset(out,0,sizeof *out);
    pthread_mutex_unlock(&sense_action_lock);return valid;
}
int host_sense_scope_dispatch(EngineCPU *cpu,uint32_t address) {
    if(address!=0x0050BEA0u||sense_scope_nested)return 0;
    HaloSenseWeaponActionStatus action;
    if(!host_sense_weapon_action_status(&action)||action.action!=HALO_SENSE_ACTION_SCOPE)return 0;
    uint32_t datum,weapon,unit=sense_local_unit(&datum,&weapon),renderer=ARG(0),caller=G32(cpu->gpr[4]);
    unsigned count=ARG(1)&65535u;HaloSenseTransform gun;
    if(!unit||weapon!=action.weapon||count<1||count>4||!sense_range(renderer,count*0xACu)||G8(renderer+2)||
       !sense_world_weapon(1,unit,datum,weapon,&gun)){ret_to_caller(cpu);return 1;}
    /* Retail single-player normally supplies its player record followed by
     * the interface record (player=-1, interface byte set). Keep the latter
     * and the original count: it draws fades/subtitles over the scope. */
    for(unsigned i=1;i<count;i++){
        uint32_t p=renderer+i*0xACu;
        if(G16(p)!=UINT16_MAX||!G8(p+2)){ret_to_caller(cpu);return 1;}
    }
    unsigned char saved[0xAC];memcpy(saved,GPTR(renderer),sizeof saved);
    for(unsigned i=0;i<2;i++){
        uint32_t p=renderer+(i?0x58u:4u);
        memcpy(GPTR(p),gun.p,12);memcpy(GPTR(p+12),gun.c[0],12);memcpy(GPTR(p+24),gun.c[2],12);
    }
    sense_scope_nested=1;engine_dispatch(cpu,address);sense_scope_nested=0;
    memcpy(GPTR(renderer),saved,sizeof saved);
    if(cpu->pc==caller){
        /* A slow original render can exceed the pre-render status lifetime.
         * Qualify its completed result against live ownership, not that old
         * timestamp. A source/pose loss increments the input generation. */
        HaloSenseStatus live;HaloSenseFrame live_hands;int zoom=-1;
        halo_sense_status(&live);
        if(live.active&&live.requested&&live.gameplay_supported&&live.session==action.session&&
           live.generation==action.generation&&halo_sense_read(&live_hands)&&
           sense_action_for_unit(unit,datum,weapon,&zoom)==HALO_SENSE_ACTION_SCOPE&&zoom==action.zoom_level){
            uint64_t completed=host_monotonic_ns();
            pthread_mutex_lock(&sense_action_lock);sense_scope_frame_state=action;sense_scope_frame_ns=completed;
            sense_action_ns=completed;
            pthread_mutex_unlock(&sense_action_lock);
            sense_camera_ns=completed;
        }
    }
    return 1;
}
