/* Independent original inventory weapon updates. Included after
 * sense_weapon_hooks.inc so the model and input guards share ownership.
 *
 * Qualified primitives: private DualWieldAudit revision10, original Halo PC
 * 1.10 SHA c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545.
 * The two real inventory objects retain independent ammo/heat/reload state.
 * No weapon clone, ammo copy, player relocation or selected-slot overwrite.
 *
 * Integration requires an explicit user opt-in; fresh paired Sense input;
 * L2 release-before-fire on every session/source/inventory transition;
 * reserving L2 for the actual offhand while L1 retains grenades; and
 * routing the actual left model through host_sense_offhand_begin/end.
 */
#include "dual_input.h"
typedef HaloDualInputSample HaloDualWieldInput;
static HaloDualInputGate dual_input_gate;
/* Contracts supplied by the independently verified left-model path. */
int host_sense_offhand_begin(uint32_t unit, uint32_t weapon);
void host_sense_offhand_end(void);
void host_sense_offhand_allow_fire(int allowed);

static struct {
    uint32_t unit, primary, offhand, tick;
    uint64_t generation;
    int has_tick, nested, fault;
} dual_state;

static int dual_requested(void) {
    return halo_sense_dual_requested() && host_sense_weapon_rendering();
}
/* Selection is also usable by the left renderer before the first shot.
 * Prototype qualification currently covers the campaign's existing pistol
 * and assault rifle only. Expanding this requires each model/tag's proof. */
static int dual_supported_weapon(uint32_t weapon) {
    int kind=sense_weapon_kind(weapon);return kind==1||kind==2;
}
static uint32_t dual_inventory(uint32_t *unit_id,uint32_t *primary_id,uint32_t *offhand_id,int *node) {
    uint32_t unit=sense_local_unit(unit_id,primary_id);
    if(!unit){sense_status(-1,HALO_SENSE_DUAL_NO_PLAYER);return 0;}
    if(!dual_supported_weapon(*primary_id)){sense_status(-1,HALO_SENSE_DUAL_UNSUPPORTED_PRIMARY);return 0;}
    uint32_t primary=sense_object(*primary_id,0x340,2);
    int selected=(int16_t)G16(unit+0x2f2);
    *offhand_id=UINT32_MAX;int other=0,supported_other=0;
    for(unsigned slot=0;slot<4;slot++)if(slot!=(unsigned)selected) {
        uint32_t id=G32(unit+0x2f8+slot*4u),w=sense_object(id,0x340,2);
        if(w&&id!=*primary_id){other++;if(dual_supported_weapon(id))supported_other++;}
        if(w&&id!=*primary_id&&G32(w+0x11c)==UINT32_MAX&&dual_supported_weapon(id)) {
            /* Multiple eligible offhands require an explicit choice. */
            if(*offhand_id!=UINT32_MAX){sense_status(-1,HALO_SENSE_DUAL_AMBIGUOUS);return 0;}
            *offhand_id=id;
        }
    }
    if(*offhand_id==UINT32_MAX){sense_status(-1,!other?HALO_SENSE_DUAL_NO_SECOND_WEAPON:
        !supported_other?HALO_SENSE_DUAL_UNSUPPORTED_SECONDARY:HALO_SENSE_DUAL_SECONDARY_BUSY);return 0;}
    *node=(int8_t)G8(primary+0x120);
    uint32_t table=G32(0x008603B0u),entries=G32(table+0x34);
    unsigned size=G16(entries+(*unit_id&65535u)*12u+6u);
    int offset=(int16_t)G16(unit+0x1f2);
    if(*node<0||*node>=64||offset<0x1f4||(unsigned)(offset+(*node+1)*0x34)>size){
        sense_status(-1,HALO_SENSE_DUAL_INVALID_ATTACHMENT);return 0;}
    sense_status(-1,sense_secondary_ready&&sense_secondary_unit==*unit_id&&sense_secondary_weapon==*offhand_id?
        HALO_SENSE_DUAL_READY:HALO_SENSE_DUAL_PREPARING_MODEL);
    return unit;
}
static int host_dual_primary_allowed(uint32_t unit,uint32_t primary) {
    return dual_input_gate.owns&&dual_input_gate.owner.unit==unit&&dual_input_gate.owner.primary==primary&&dual_input_gate.armed[1];
}
static void dual_reset(void) {
    /* No persistent attachment is retained between calls. A fault cannot be
     * cleared just by tracking flicker; disabling the feature resets it. */
    dual_state.has_tick=0;memset(&dual_input_gate,0,sizeof dual_input_gate);
    dual_state.unit=dual_state.primary=dual_state.offhand=UINT32_MAX;
    dual_state.generation=0;
}
/* Called at original004C1530 entry after a fresh gameplay input capture.
 * Returns zero because the primary's original update always continues.
 * The actual dispatch list must include004C1530 before compiling chunks. */
static int host_dual_wield_update(EngineCPU *cpu,uint32_t address,const HaloDualWieldInput *input) {
    if(address!=0x004C1530u||dual_state.nested)return 0;
    if(!input||!input->enabled||!dual_requested()) {
        dual_reset();dual_state.fault=0;return 0;
    }
    if(!input->fresh||!input->spatial||!input->owner.session||!halo_sense_gameplay_supported()) {
        dual_input_gate.armed[0]=dual_input_gate.armed[1]=0;return 0;
    }
    uint32_t unit_id,primary_id,offhand_id=UINT32_MAX;int node=0;
    uint32_t unit=sense_local_unit(&unit_id,&primary_id);
    if(!unit){dual_reset();return 0;}
    uint32_t pair_unit,pair_primary;int pair=dual_inventory(&pair_unit,&pair_primary,&offhand_id,&node)!=0;
    if(!pair)offhand_id=UINT32_MAX;
    if(ARG(0)!=primary_id)return 0;
    uint32_t time=G32(0x006f1d6cu);
    if(!sense_range(time,20))return 0;
    uint32_t tick=G32(time+12);
    if(dual_state.unit!=unit_id||dual_state.primary!=primary_id||dual_state.offhand!=offhand_id||
       dual_state.generation!=input->owner.generation) {
        dual_reset();dual_state.unit=unit_id;dual_state.primary=primary_id;dual_state.offhand=offhand_id;
        dual_state.generation=input->owner.generation;
    }
    if(dual_state.fault||(dual_state.has_tick&&dual_state.tick==tick))return 0;
    dual_state.has_tick=1;dual_state.tick=tick;
    /* Readiness may disappear between input and update. Freeze the entire
     * second update if the real model/pose is absent: controls=0 alone cannot
     * prevent a queued burst from entering the original fire routine. */
    HaloDualInputSample sample=*input;
    sample.owner.unit=unit_id;sample.owner.primary=primary_id;sample.owner.offhand=offhand_id;
    sample.gameplay_supported=1;
    sample.model_ready[1]=sense_muzzle_ready(1,unit_id,primary_id);
    int offhand_ready=pair&&host_sense_offhand_begin(unit_id,offhand_id);
    /* An armed grenade holds the offhand's hand: no fire or reload request,
     * and L2 must be released again before it fires (dual_input.h). Its own
     * original update below still runs, so its timers keep progressing. */
    sample.model_ready[0]=offhand_ready&&!sense_grenade_hold_owns_left();
    const HaloDualInputResult controls=dual_input_update(&dual_input_gate,&sample);
    if(!offhand_ready) {host_sense_offhand_end();return 0;}
    host_sense_offhand_allow_fire(controls.fire[0]);
    uint32_t sp=cpu->gpr[4];
    if(sp<0x10010u){host_sense_offhand_end();dual_input_gate.armed[0]=0;return 0;}
    const EngineCPU original=*cpu;unsigned char stack[16];memcpy(stack,GPTR(sp-16),16);
    dual_state.nested=1;
    uint32_t attach_args[3]={unit_id,offhand_id,(uint32_t)node};
    host_call_guest(cpu,0x004F6440u,3,attach_args,0);
    uint32_t offhand=sense_object(offhand_id,0x340,2);
    if(offhand&&G32(offhand+0x11c)==unit_id) {
        uint32_t flags=0,analog=0;
        if(controls.reload[0])flags=8;
        else if(controls.fire[0]){flags=2;analog=0x3f800000u;}
        cpu->gpr[0]=offhand_id;uint32_t control[2]={flags,analog};
        host_call_guest(cpu,0x004C2990u,2,control,0);
        uint32_t update_args[1]={offhand_id};
        host_call_guest(cpu,0x004C1530u,1,update_args,0);
        offhand=sense_object(offhand_id,0x340,2);
        if(offhand&&G32(offhand+0x11c)==unit_id)host_call_guest(cpu,0x004F6610u,1,update_args,0);
        offhand=sense_object(offhand_id,0x340,2);
        if(offhand&&G32(offhand+0x11c)==UINT32_MAX){
            /* Match original0056D77B–0056D804 holstered inventory state.
             * Detach alone spatially inserts a pickup; the player's normal
             * ammo collector can then consume its own offhand weapon. */
            cpu->gpr[0]=offhand_id;host_call_guest(cpu,0x004F5DE0u,0,NULL,0);
            cpu->gpr[0]=offhand_id;host_call_guest(cpu,0x004F50F0u,0,NULL,0);
            offhand=sense_object(offhand_id,0x340,2);
            if(offhand){
                uint32_t data=sense_tag(G32(offhand),0x38);
                if(data&&G32(data+0x34)!=UINT32_MAX&&!(G32(offhand+0x10)&1u)){
                    cpu->gpr[0]=offhand_id;uint32_t hide[2]={1,0};
                    host_call_guest(cpu,0x004F9A20u,2,hide,0);
                }
                offhand=sense_object(offhand_id,0x340,2);
                if(!offhand){dual_state.fault=1;goto dual_restore;}
                uint32_t entry=G32(G32(0x008603B0u)+0x34)+(offhand_id&65535u)*12u;
                S32(offhand+0x10,G32(offhand+0x10)|1u);S8(entry+2,G8(entry+2)&0xFDu);
                cpu->gpr[1]=offhand_id;cpu->gpr[2]=unit_id;
                host_call_guest(cpu,0x004BCFC0u,0,NULL,0);
            }
        }
        offhand=sense_object(offhand_id,0x340,2);
        if(!offhand||G32(offhand+0x11c)!=UINT32_MAX)dual_state.fault=1;
    } else dual_state.fault=1;
dual_restore:
    host_sense_offhand_end();
    const uint64_t instructions=cpu->instruction_count;
    memcpy(GPTR(sp-16),stack,16);*cpu=original;cpu->instruction_count=instructions;
    dual_state.nested=0;
    if(dual_state.fault)host_log("[sense-dual] stopped after attachment/restoration failure");
    return 0;
}
