/* Accepted-shot and charge haptics (sense_weapon_haptics.h). A shot counts
 * only when the original fire routine reached its final pre-spread boundary
 * 004593B0 from 004C500F with the tracked ray, and the outer 004C3F10 call
 * then returned normally: one event per accepted call however many
 * projectiles it made, nothing for a blocked, escaped, charging or empty
 * trigger. The trigger level and the audio are never the witness. Events carry
 * the Sense owner lease; the controller queue rechecks it before each player
 * starts. Included by sense_weapon_hooks.inc after sense_world_weapon. */
#include "sense_weapon_haptics.h"
#include "sense_weapon_charge.h"
#include "haptics.h"
#include "halo_settings.h"
static struct {
    uint32_t unit,weapon,caller;uint64_t session,generation;
    unsigned hand,projectiles;int armed,supported;
} sense_shot;
static struct { uint64_t session,generation,lease;uint32_t unit;int owned; } sense_haptic_owner;
static HaloWeaponChargeHaptic sense_charge_haptic;
/* Master enable, master strength and the "Tracked weapon haptics" toggle. */
static float sense_haptic_strength(void){
    float strength=halo_settings_haptics_strength();
    return halo_haptics_enabled()&&halo_sense_weapon_haptics()&&isfinite(strength)&&strength>0.f?strength:0.f;
}
static void sense_shot_begin(EngineCPU *cpu,unsigned hand,uint32_t datum,uint32_t weapon,const HaloSenseDualInput *input) {
    sense_shot.armed=1;sense_shot.caller=G32(cpu->gpr[4]);sense_shot.projectiles=0;
    sense_shot.unit=datum;sense_shot.weapon=weapon;sense_shot.hand=hand;
    sense_shot.session=input->session;sense_shot.generation=input->generation;
    /* The support state accepted with the very pose this shot uses. */
    SenseAcceptedWeaponFrame frame;
    sense_shot.supported=hand==1&&sense_accepted_weapon_frame(datum,weapon,&frame)&&frame.supported;
    halo_haptics_weapon_shot_begin();
}
static void sense_shot_projectile(void){if(sense_shot.armed)sense_shot.projectiles++;}
/* Requalifies the local campaign player, identity and Sense ownership after
 * the original call, then hands one leased event to the controller queue. */
static void sense_shot_end(EngineCPU *cpu) {
    if(!sense_shot.armed)return;
    sense_shot.armed=0;
    uint32_t datum,weapon,unit=sense_local_unit_any(&datum,&weapon);HaloSenseDualInput input;
    int accepted=cpu->pc==sense_shot.caller&&sense_shot.projectiles&&unit&&datum==sense_shot.unit&&
       sense_object(sense_shot.weapon,0x340,2)&&G16(0x00719720u)==0&&halo_sense_dual_input(&input)&&
       input.session==sense_shot.session&&input.generation==sense_shot.generation&&input.gameplay_supported&&
       sense_haptic_owner.owned&&sense_haptic_owner.session==input.session&&sense_haptic_owner.generation==input.generation&&
       sense_haptic_owner.unit==datum;
    halo_haptics_weapon_shot_end(accepted);
    float strength=sense_haptic_strength();
    if(!accepted||strength<=0.f)return;
    char path[80];HostGCSpatialHaptic event;
    unsigned profile=halo_weapon_haptic_profile(sense_weapon_path(sense_shot.weapon,path)?path:NULL);
    if(halo_weapon_haptic_event(profile,sense_shot.hand,sense_shot.supported,strength,hostgc_source_generation(),&event)){
        event.session=input.session;event.generation=input.generation;event.lease=sense_haptic_owner.lease;
        hostgc_play_spatial_haptic(&event);
    }
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){
        static unsigned logs;if(logs++<64)host_log("[sense-haptic] hand=%u weapon=%08x profile=%u projectiles=%u supported=%d",sense_shot.hand,sense_shot.weapon,profile,sense_shot.projectiles,sense_shot.supported);
    }
}
/* Charge of the local player's own primary weapon (sense_weapon_charge.h):
 * validated salted object, owner and definition tag, then the original math. */
static int sense_weapon_charge(uint32_t datum,uint32_t weapon,int *state,float *fraction) {
    *state=0;*fraction=0;
    uint32_t w=sense_object(weapon,0x340,2),data=w?sense_tag(G32(w),0x504):0;
    return data&&G32(w+0x11C)==datum&&sense_weapon_charge_read(w,data,state,fraction);
}
/* Once per main-loop tick, before any ownership early-out. While the tracked
 * path owns the local campaign player's weapon the audio fire witness stands
 * down; losing that (menu, death, toggle, session/generation/player change)
 * releases the audio path and cancels queued pulses at once. Charge pulses
 * follow the primary weapon's real charge state. */
static void sense_haptics_tick(void) {
    HaloSenseDualInput input;uint32_t datum=0,weapon=0;
    int owned=host_sense_weapon_rendering()&&halo_sense_dual_input(&input)&&input.gameplay_supported&&
        sense_local_unit(&datum,&weapon)&&sense_haptic_strength()>0.f;
    uint64_t lease=halo_sense_set_weapon_haptic_owner(owned,owned?input.session:0,owned?input.generation:0);
    owned=owned&&lease;
    if(sense_haptic_owner.owned&&(!owned||input.session!=sense_haptic_owner.session||
       input.generation!=sense_haptic_owner.generation||datum!=sense_haptic_owner.unit||lease!=sense_haptic_owner.lease))
        hostgc_cancel_spatial_haptics(HOSTGC_SPATIAL_ALL);
    if(!owned&&sense_haptic_owner.owned)halo_haptics_weapon_owner_release();
    sense_haptic_owner.owned=owned;
    if(!owned){memset(&sense_charge_haptic,0,sizeof sense_charge_haptic);return;}
    sense_haptic_owner.session=input.session;sense_haptic_owner.generation=input.generation;
    sense_haptic_owner.unit=datum;sense_haptic_owner.lease=lease;halo_haptics_weapon_owner_renew();
    int state;float fraction,intensity,sharpness;sense_weapon_charge(datum,weapon,&state,&fraction);
    int step=halo_weapon_charge_step(&sense_charge_haptic,input.session,input.generation,lease,datum,weapon,
                                     state,fraction,host_monotonic_ns(),&intensity,&sharpness);
    if(step==HALO_WEAPON_CHARGE_CANCEL){
        hostgc_cancel_spatial_haptics(HOSTGC_SPATIAL_CHARGE);
        /* A different weapon/lease may already be charging: begin it now. */
        step=halo_weapon_charge_step(&sense_charge_haptic,input.session,input.generation,lease,datum,weapon,
                                     state,fraction,host_monotonic_ns(),&intensity,&sharpness);
    }
    if(step==HALO_WEAPON_CHARGE_PULSE){
        float strength=sense_haptic_strength();
        HostGCSpatialHaptic event={.intensity={0,fminf(1.f,intensity*strength)},.sharpness={0,sharpness},
            .source_generation=hostgc_source_generation(),.session=input.session,.generation=input.generation,
            .lease=lease,.kind=HOSTGC_SPATIAL_CHARGE};
        hostgc_play_spatial_haptic(&event);
    }
}
