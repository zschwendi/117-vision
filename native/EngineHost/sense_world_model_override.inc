/* The one wrapper of world-model submission for the tracked body and the
 * physical pickup. Include after every available prepare helper:
 *   HALO_SENSE_HAVE_BODY_MODEL_PREPARE   sense_body_model_prepare (local biped)
 *   HALO_SENSE_HAVE_PICKUP_MODEL_PREPARE sense_pickup_model_prepare (ground weapon)
 * Their salted identities are disjoint; the body is asked first. Either lane
 * builds alone. A single guard means the original runs exactly once per
 * submission and is never re-overridden from inside itself. ECX and the
 * optional centre argument are restored on every return, including an
 * escaped PC. See sense_world_model_override.h for the ABI. */
#include "sense_world_model_override.h"
static int sense_world_model_nested;
static int host_sense_world_model_dispatch(EngineCPU *cpu,uint32_t address) {
    if(address!=0x004D6FC0u||sense_world_model_nested)return 0;
    uint32_t stack=cpu->gpr[4],caller=G32(stack);
    if(caller!=0x0050F049u&&caller!=0x0050F0BDu)return 0;
    SenseWorldModelOverride change={0};int outcome=SENSE_WORLD_MODEL_NONE;
#ifdef HALO_SENSE_HAVE_BODY_MODEL_PREPARE
    outcome=sense_body_model_prepare(cpu,&change);
#endif
#ifdef HALO_SENSE_HAVE_PICKUP_MODEL_PREPARE
    if(outcome==SENSE_WORLD_MODEL_NONE){memset(&change,0,sizeof change);outcome=sense_pickup_model_prepare(cpu,&change);}
#endif
    if(outcome==SENSE_WORLD_MODEL_NONE)return 0;
    if(outcome!=SENSE_WORLD_MODEL_APPLY||!change.palette){ret_to_caller(cpu);return 1;}
    uint32_t nodes=cpu->gpr[1],center=G32(stack+0x18);
    cpu->gpr[1]=change.palette;if(change.center)S32(stack+0x18,change.center);
    sense_world_model_nested=1;engine_dispatch(cpu,address);sense_world_model_nested=0;
    if(change.center)S32(stack+0x18,center);
    /* Both admitted retail callers overwrite CL before testing it and rebuild
     * full ECX before any full-register read (0050F0C4/0050F0E5/0050F10F).
     * Restore the injected register input even if the original left a derived
     * scratch address (palette+52), including an escaped/aborted original. */
    cpu->gpr[1]=nodes;
    return 1;
}
