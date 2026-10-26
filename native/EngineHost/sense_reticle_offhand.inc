/* Reuse each real weapon's original crosshair state and artwork. The second
 * invocation cannot change the actual selected inventory or primary HUD's
 * mutable timing record. Only the exact retail crosshair entry is wrapped. */
static int sense_reticle_pair_nested;
static int sense_reticle_pair_dispatch(EngineCPU *cpu,uint32_t address) {
    if(address!=0x004B2CF0u||sense_reticle_pair_nested||G32(cpu->gpr[4])!=0x004B1F45u)return 0;
    uint32_t datum,primary,unit=sense_local_unit(&datum,&primary);
    if(unit&&!sense_action_offhand_live(sense_action_for_unit(unit,datum,primary,NULL)))return 0;
    uint32_t p=unit?sense_object(primary,0x340,2):0,data=p?sense_tag(G32(p),0x490):0;
    uint32_t globals=G32(0x0087A478u),players=G32(0x0087A480u);
    if(!unit||!data||cpu->gpr[0]!=G32(data+0x48C)||!sense_range(globals,8)||!sense_range(players,0x38))return 0;
    uint32_t row=G32(players+0x34)+(G32(globals+4)&65535u)*0x200u;
    if(cpu->gpr[1]!=row||!sense_range(row,0x200))return 0;
    uint32_t caller=G32(cpu->gpr[4]);unsigned oldhand=sense_reticle_art_hand;
    uint32_t oldweapon=sense_reticle_art_weapon;
    sense_reticle_pair_nested=1;sense_reticle_art_hand=1;sense_reticle_art_weapon=primary;
    engine_dispatch(cpu,address);
    sense_reticle_art_hand=oldhand;sense_reticle_art_weapon=oldweapon;
    if(cpu->pc!=caller){sense_reticle_pair_nested=0;return 1;}
    HaloSenseReticleFrame targets;uint32_t pair_unit,pair_primary,secondary;int node;
    if(!halo_sense_dual_requested()||sense_grenade_hold_owns_left()||!halo_sense_reticle_read(&targets)||!targets.hands[0].valid||
       !dual_inventory(&pair_unit,&pair_primary,&secondary,&node)||pair_unit!=datum||pair_primary!=primary||
       !sense_secondary_ready||secondary!=sense_secondary_weapon||!sense_muzzle_ready(0,datum,secondary))goto finished;
    uint32_t w=sense_object(secondary,0x340,2),wd=w?sense_tag(G32(w),0x4F8):0;
    uint32_t hud=wd?G32(wd+0x48C):UINT32_MAX;
    int32_t magazines=wd?(int32_t)G32(wd+0x4F0):-1;
    /* Original4C29D0 writes12 bytes per magazine after a12-byte header. */
    if(!wd||magazines<0||magazines>2||!sense_tag(hud,0x88))goto finished;
    int index=(int16_t)G16(row+2);uint32_t state=G32(0x00719430u);
    if(index<0||index>=4||!sense_range(state,0x28u+(unsigned)(index+1)*0x50u))goto finished;
    state+=0x28u+(unsigned)index*0x50u;
    uint32_t sp=cpu->gpr[4];if(sp<0x10020u)goto finished;
    static uint32_t scratch;if(!scratch)scratch=guest_alloc(0x100);
    if(!sense_range(scratch,0x100))goto finished;
    unsigned char hud_before[0x50],stack_before[16];
    memcpy(hud_before,GPTR(state),sizeof hud_before);memcpy(stack_before,GPTR(sp-16),sizeof stack_before);
    memset(GPTR(scratch),0,0x100);
    EngineCPU q=*cpu;q.gpr[0]=secondary;host_call_guest(&q,0x004C29D0u,1,&scratch,0);
    q.gpr[0]=hud;q.gpr[1]=row;sense_reticle_art_hand=0;sense_reticle_art_weapon=secondary;
    host_call_guest(&q,0x004B2CF0u,1,&scratch,0);
    sense_reticle_art_hand=oldhand;sense_reticle_art_weapon=oldweapon;
    memcpy(GPTR(state),hud_before,sizeof hud_before);memcpy(GPTR(sp-16),stack_before,sizeof stack_before);
    cpu->instruction_count=q.instruction_count;
finished:
    sense_reticle_pair_nested=0;return 1;
}
