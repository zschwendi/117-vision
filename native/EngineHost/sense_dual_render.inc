/* Independent real inventory viewmodel. Its original FP cache is private;
 * selected inventory slots and the primary cache are never overwritten. */
static uint32_t sense_dual_cache;
static int sense_dual_nested, sense_mirror_scope;
static uint32_t sense_dual_animation_tick=UINT32_MAX;
static uint32_t sense_dual_primary=UINT32_MAX;
static uint64_t sense_dual_session,sense_dual_generation;
static int sense_offhand_claimed,sense_offhand_allow;
static uint32_t sense_offhand_unit,sense_offhand_weapon,sense_offhand_previous_cache;
static unsigned char sense_primary_cache_before[0x1EA0];
static int sense_offhand_cache_owned;
static uint32_t dual_inventory(uint32_t *,uint32_t *,uint32_t *,int *);
static int host_dual_primary_allowed(uint32_t,uint32_t);
static void sense_publish_reticles(EngineCPU *);
static void sense_dual_reset_model(void);
int host_sense_weapon_mirrored_draw(void){return sense_mirror_scope;}
void host_sense_offhand_end(void){
    if(sense_offhand_cache_owned){
        S32(0x006B2D98u,sense_offhand_previous_cache);
        if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")&&sense_range(sense_offhand_previous_cache,0x1EA0)&&
           memcmp(sense_primary_cache_before,GPTR(sense_offhand_previous_cache),0x1EA0))
            host_log("[sense-dual-cache] ERROR primary cache changed during offhand update");
    }
    if(sense_offhand_cache_owned&&(!sense_range(sense_dual_cache,0x1EA0)||
       !G8(sense_dual_cache)||G32(sense_dual_cache+4)!=sense_offhand_unit||G32(sense_dual_cache+8)!=sense_offhand_weapon)){
        sense_dual_reset_model();halo_sense_reticle_publish(NULL);
    }
    sense_offhand_cache_owned=0;sense_offhand_claimed=sense_offhand_allow=0;
}
void host_sense_offhand_allow_fire(int allowed){sense_offhand_allow=sense_offhand_claimed&&allowed&&!sense_grenade_hold_owns_left();}
int host_sense_offhand_begin(uint32_t datum,uint32_t weapon) {
    host_sense_offhand_end();uint32_t local,primary,unit=sense_local_unit(&local,&primary);
    uint32_t body=sense_object(weapon,0x340,2);HaloSenseTransform hand;
    if(!unit||!sense_action_offhand_live(sense_action_for_unit(unit,local,primary,NULL))||local!=datum||primary==weapon||!body||G32(body+0x11C)!=UINT32_MAX||
       !sense_secondary_ready||sense_secondary_unit!=datum||sense_secondary_weapon!=weapon||
       !sense_muzzle_ready(0,datum,weapon)||!sense_world_weapon(0,unit,datum,weapon,&hand))return 0;
    int found=0;for(unsigned i=0;i<4;i++)if(G32(unit+0x2F8+4*i)==weapon)found++;
    if(found!=1)return 0;
    sense_offhand_unit=datum;sense_offhand_weapon=weapon;sense_offhand_claimed=1;
    sense_offhand_previous_cache=G32(0x006B2D98u);
    if(!sense_range(sense_offhand_previous_cache,0x1EA0)){host_sense_offhand_end();return 0;}
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE"))memcpy(sense_primary_cache_before,GPTR(sense_offhand_previous_cache),0x1EA0);
    S32(0x006B2D98u,sense_dual_cache);sense_offhand_cache_owned=1;return 1;
}
static void sense_dual_reset_model(void) {
    sense_secondary_ready=0;sense_muzzles[0].valid=0;
    sense_secondary_unit=sense_secondary_weapon=sense_dual_primary=UINT32_MAX;
    sense_dual_session=sense_dual_generation=0;sense_dual_animation_tick=UINT32_MAX;
    if(sense_dual_cache)S8(sense_dual_cache,0);
}
static int sense_dual_initialize(EngineCPU *cpu,uint32_t datum,uint32_t weapon) {
    uint32_t body=sense_object(weapon,0x340,2),data=body?sense_tag(G32(body),0x47C):0;
    uint32_t graph=data?sense_tag(G32(data+0x478),0x80):0;
    uint32_t globals=G32(0x00746FA0u);
    if(!graph||!sense_range(globals,0x184)||!sense_tag(G32(data+0x468),0xC0))return 0;
    int32_t type_count=(int32_t)G32(graph+0x48),animation_count=(int32_t)G32(graph+0x74);
    if(type_count<=0||type_count>64||!sense_range(G32(graph+0x4C),(size_t)type_count*0x18)||
       animation_count<=0||animation_count>4096)return 0;
    uint32_t info=G32(globals+0x180);
    if(!sense_range(info,0x10)||!sense_tag(G32(info+0xC),0xC0))return 0;
    if(!sense_dual_cache)sense_dual_cache=guest_alloc(0x1EA0);
    if(!sense_range(sense_dual_cache,0x1EA0))return 0;
    uint32_t c=sense_dual_cache;memset(GPTR(c),0,0x1EA0);
    S32(c+4,datum);S32(c+8,weapon);
    for(unsigned o=0;o<5;o++){static const unsigned offsets[]={0xC,0x14,0x16,0x1A,0x20};S16(c+offsets[o],UINT16_MAX);}
    S32(c+0x1E98,UINT32_MAX);S16(c+0x1E9C,UINT16_MAX);
    /* Original493C60's optional idle animation selection. */
    if(G32(graph+0x48)>0&&sense_range(G32(graph+0x4C),0x18)){
        uint32_t type=G32(graph+0x4C),list=G32(type+0x14);
        if(G32(type+0x10)>4&&sense_range(list,10)){
            unsigned index=G16(list+8);uint32_t animations=G32(graph+0x78);
            if(index<G32(graph+0x74)&&sense_range(animations+(uint64_t)index*0xB4,0xB4)&&(int16_t)G16(animations+index*0xB4+0x22)>=9)S16(c+0x14,index);
        }
    }
    EngineCPU q=*cpu;uint32_t saved=G32(0x006B2D98u);S32(0x006B2D98u,c);
    q.gpr[0]=G32(data+0x478);q.gpr[1]=G32(info+0xC);uint32_t arg=c+0x1E10;
    unsigned arms=host_call_guest(&q,0x00493F00u,1,&arg,0)&255u;S8(c+0x1E0E,arms);
    q.gpr[0]=G32(data+0x478);q.gpr[1]=G32(data+0x468);arg=c+0x1D8E;
    unsigned gun=host_call_guest(&q,0x00493F00u,1,&arg,0)&255u;S8(c+0x1D8C,gun);
    if(arms&&gun){q.gpr[0]=0;uint32_t args[2]={0,1};host_call_guest(&q,0x00492E60u,2,args,0);S8(c,1);}
    S32(0x006B2D98u,saved);cpu->instruction_count=q.instruction_count;
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE"))host_log("[sense-dual-init] unit=%08x weapon=%08x arms=%u gun=%u",datum,weapon,arms,gun);
    return arms&&gun;
}
static int sense_dual_prepare(EngineCPU *cpu) {
    HaloSenseDualInput input={0};uint32_t datum,primary,weapon;int node;
    if(!halo_sense_dual_requested()){
        sense_status(-1,HALO_SENSE_DUAL_OFF);sense_dual_reset_model();return 0;}
    if(!halo_sense_dual_input(&input)||!input.gameplay_supported){
        sense_status(-1,HALO_SENSE_DUAL_WAITING_TRACKING);sense_dual_reset_model();return 0;}
    if(!dual_inventory(&datum,&primary,&weapon,&node)){sense_dual_reset_model();return 0;}
    uint32_t unit=sense_local_unit(&datum,&primary);
    if(!unit||!sense_action_offhand_live(sense_action_for_unit(unit,datum,primary,NULL))){sense_dual_reset_model();return 0;}
    if(sense_secondary_weapon!=weapon||sense_secondary_unit!=datum||sense_dual_primary!=primary||
       sense_dual_session!=input.session||sense_dual_generation!=input.generation||
       !sense_range(sense_dual_cache,0x1EA0)||!G8(sense_dual_cache)||
       G32(sense_dual_cache+4)!=datum||G32(sense_dual_cache+8)!=weapon){
        sense_dual_reset_model();if(!sense_dual_initialize(cpu,datum,weapon))return 0;
        sense_secondary_weapon=weapon;sense_secondary_unit=datum;sense_dual_primary=primary;
        sense_dual_session=input.session;sense_dual_generation=input.generation;
    }
    return 1;
}
static void sense_dual_tick(EngineCPU *cpu) {
    if(!sense_dual_prepare(cpu))return;
    uint32_t time=G32(0x006F1D6Cu);if(!sense_range(time,16))return;
    uint32_t tick=G32(time+12);if(tick==sense_dual_animation_tick)return;
    sense_dual_animation_tick=tick;
    EngineCPU q=*cpu;uint32_t old=G32(0x006B2D98u);S32(0x006B2D98u,sense_dual_cache);
    uint32_t view=0;host_call_guest(&q,0x00493150u,1,&view,0);
    S32(0x006B2D98u,old);cpu->instruction_count=q.instruction_count;
}
static void sense_dual_build(EngineCPU *cpu) {
    sense_secondary_ready=0;
    if(!sense_dual_prepare(cpu))return;
    EngineCPU q=*cpu;uint32_t old=G32(0x006B2D98u);S32(0x006B2D98u,sense_dual_cache);
    sense_model_hand=0;q.gpr[0]=0;host_call_guest(&q,0x00493740u,0,NULL,0);sense_model_hand=1;
    S32(0x006B2D98u,old);cpu->instruction_count=q.instruction_count;
    sense_secondary_ready=sense_muzzle_ready(0,sense_secondary_unit,sense_secondary_weapon);
    sense_status(-1,sense_secondary_ready?HALO_SENSE_DUAL_READY:HALO_SENSE_DUAL_PREPARING_MODEL);
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){
        static unsigned frames;if(++frames%25==0){uint32_t a=sense_object(sense_dual_primary,0x340,2),b=sense_object(sense_secondary_weapon,0x340,2);host_log("[sense-dual-ammo] tick=%u primary=%08x loaded=%u reserve=%u offhand=%08x loaded=%u reserve=%u parent=%08x",sense_dual_animation_tick,sense_dual_primary,G16(a+0x2B8),G16(a+0x2B6),sense_secondary_weapon,G16(b+0x2B8),G16(b+0x2B6),G32(b+0x11C));}
    }
}
static int sense_dual_render_dispatch(EngineCPU *cpu,uint32_t address) {
    if(sense_dual_nested)return 0;
    if(address==0x004923D0u){
        uint32_t caller=G32(cpu->gpr[4]);sense_dual_nested=1;engine_dispatch(cpu,address);
        if(cpu->pc==caller)sense_dual_tick(cpu);sense_dual_nested=0;return 1;
    }
    if(address==0x00492430u&&panorama_fp_cache_nested&&G32(cpu->gpr[4])==0x0050C09Cu){
        uint32_t caller=G32(cpu->gpr[4]);sense_dual_nested=1;engine_dispatch(cpu,address);
        if(cpu->pc==caller){sense_dual_build(cpu);sense_publish_reticles(cpu);}sense_dual_nested=0;return 1;
    }
    if(address==0x004924B0u&&sense_secondary_ready&&G16(0x007C3108u)==0&&!sense_grenade_hold_owns_left()){
        uint32_t caller=G32(cpu->gpr[4]);sense_dual_nested=1;engine_dispatch(cpu,address);
        if(cpu->pc==caller&&sense_dual_prepare(cpu)&&sense_muzzle_ready(0,sense_secondary_unit,sense_secondary_weapon)){
            EngineCPU q=*cpu;uint32_t old=G32(0x006B2D98u);S32(0x006B2D98u,sense_dual_cache);
            sense_mirror_scope=1;host_call_guest(&q,0x004924B0u,0,NULL,0);sense_mirror_scope=0;
            S32(0x006B2D98u,old);cpu->instruction_count=q.instruction_count;
        }
        sense_dual_nested=0;return 1;
    }
    return 0;
}
