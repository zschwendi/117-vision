/* Only the original weapon-crosshair callsite is redirected. Centre-anchored
 * damage flashes, shields and other HUD elements retain their normal target. */
#include "sense_reticle_art.h"
static unsigned sense_reticle_art_hand=1;
static uint32_t sense_reticle_art_weapon=UINT32_MAX;
static int sense_reticle_art_nested;
static int sense_reticle_capture(EngineCPU *cpu) {
    if(sense_reticle_art_nested||G32(cpu->gpr[4])!=0x004B33C6u)return 0;
    uint32_t datum,weapon,unit=sense_local_unit(&datum,&weapon);HaloSenseReticleFrame targets;
    int action=unit?sense_action_for_unit(unit,datum,weapon,NULL):HALO_SENSE_ACTION_NONE;
    if(action!=HALO_SENSE_ACTION_NONE&&!sense_action_offhand_live(action)){
        if(sense_reticle_art_hand==0){ret_to_caller(cpu);return 1;}return 0;
    }
    /* The primary gun is in its original reload/swap animation: no primary
     * crosshair (never the centred HUD one); the offhand's stays. */
    if(action!=HALO_SENSE_ACTION_NONE&&sense_reticle_art_hand==1){ret_to_caller(cpu);return 1;}
    if(!unit||!halo_sense_reticle_read(&targets)){
        /* Missing target/tracking must hide the relocated crosshair, never
         * alternate between its world depth and an unrelated central HUD. */
        ret_to_caller(cpu);return 1;
    }
    if(sense_reticle_art_weapon!=UINT32_MAX)weapon=sense_reticle_art_weapon;
    unsigned hand=sense_reticle_art_hand;
    float ax=320.f-(int16_t)G16(0x007C3142u),ay=240.f-(int16_t)G16(0x007C3140u);
    if(!host_sense_reticle_art_begin(hand,weapon,&targets,ax,ay)){
        /* An epoch/weapon mismatch, missing hand or unavailable capture
         * target must not move the crosshair back to the central HUD. */
        ret_to_caller(cpu);return 1;
    }
    uint32_t caller=G32(cpu->gpr[4]);
    sense_reticle_art_nested=1;engine_dispatch(cpu,0x004ACAD0u);sense_reticle_art_nested=0;
    host_sense_reticle_art_end(cpu->pc==caller);
    return 1;
}
