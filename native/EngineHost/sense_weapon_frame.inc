/* The single authoritative accepted primary weapon frame.
 *
 * The outer render boundary (0050BEA0, never a panorama face, eye or the
 * scope's nested render) begins an epoch. The first primary consumer in that
 * epoch -- scope camera, viewmodel, reticle or fire -- admits ONE frame: a raw
 * left/right pair read under an identical before/after Sense lease, the
 * two-hand support decision, grip continuity and scoped smoothing. Every later
 * consumer in the epoch, including shots fired by game ticks before the next
 * render, receives exactly that frame. Support eligibility (and so any
 * physical "left hand busy" state) is evaluated only here; a change later in
 * the epoch takes effect at the next admission.
 *
 * One admission attempt per epoch, success or failure. A frame that later
 * stops matching the live session, input generation, unit, weapon, camera or
 * the 150 ms age fails closed for the rest of the epoch: no consumer falls
 * back to the head or recomputes a second pose. The offhand never uses it.
 * Included by sense_weapon_hooks.inc after sense_solve_support. */
#include "sense_weapon_continuity.inc"
typedef struct {
    uint64_t epoch, epoch_ns, session, generation;
    uint32_t unit, weapon;
    int supported;
    HaloSenseTransform primary, left, visual_left;
} SenseAcceptedWeaponFrame;
#define SENSE_ACCEPTED_WEAPON_FRAME_DEFINED 1
typedef struct { HaloSenseDualInput input; HaloSenseTransform left, right; } SenseLeasedPair;
static uint64_t sense_frame_epoch,sense_frame_epoch_ns;
static struct {
    SenseAcceptedWeaponFrame frame;
    HaloSenseDualInput input;        /* admission lease, for the viewmodel's foregrip capture */
    HaloSenseTransform raw_right;    /* unfiltered right controller of the same pair */
    uint64_t attempted;              /* epoch of the single admission attempt */
    int accepted;
} sense_accepted;
#define SENSE_FRAME_MAX_AGE_NS UINT64_C(150000000)
static void sense_accepted_frame_begin(void){
    if(++sense_frame_epoch==0)++sense_frame_epoch;
    sense_frame_epoch_ns=host_monotonic_ns();
}
static uint64_t sense_accepted_frame_epoch(void){return sense_frame_epoch;}
/* Both hands from one publication, bracketed by the same session and input
 * generation: a recenter/configuration change around the read fails closed. */
static int sense_leased_pair(uint32_t unit,uint32_t datum,SenseLeasedPair *out){
    HaloSenseDualInput after;HaloSenseFrame frame;
    memset(out,0,sizeof *out);
    if(!halo_sense_dual_input(&out->input)||!halo_sense_read(&frame)||!halo_sense_dual_input(&after)||
       after.session!=out->input.session||after.generation!=out->input.generation)return 0;
    return sense_world_frame_hand(&frame,0,unit,datum,&out->left)&&sense_world_frame_hand(&frame,1,unit,datum,&out->right);
}
static int sense_frame_owner(uint32_t datum,uint32_t weapon){
    uint32_t local,selected;uint64_t now=host_monotonic_ns();
    return sense_frame_epoch&&now>=sense_frame_epoch_ns&&now-sense_frame_epoch_ns<=SENSE_FRAME_MAX_AGE_NS&&
        sense_camera_unit==datum&&sense_local_unit(&local,&selected)&&local==datum&&selected==weapon;
}
static int sense_frame_live(uint32_t datum,uint32_t weapon){
    const SenseAcceptedWeaponFrame *f=&sense_accepted.frame;HaloSenseDualInput input;
    return sense_accepted.accepted&&sense_accepted.attempted==sense_frame_epoch&&f->epoch==sense_frame_epoch&&
        f->unit==datum&&f->weapon==weapon&&sense_frame_owner(datum,weapon)&&
        halo_sense_dual_input(&input)&&input.session==f->session&&input.generation==f->generation;
}
static int sense_frame_attempted(void){return sense_frame_epoch&&sense_accepted.attempted==sense_frame_epoch;}
static void sense_frame_fail(void){sense_accepted.attempted=sense_frame_epoch;sense_accepted.accepted=0;}
/* Seeding consumers only: an invalid observation latches the epoch closed,
 * so a unit/weapon/menu/lease state restored later in the same epoch can
 * never resurrect the old frame. The next epoch admits afresh. */
static int sense_frame_use(uint32_t datum,uint32_t weapon){
    if(sense_frame_live(datum,weapon))return 1;
    sense_frame_fail();return 0;
}
/* READ ONLY (physical/body lanes): never seeds, advances or writes, so it
 * cannot itself record a transient invalid observation; a substantive
 * consumer (model, reticle, scope, shot) that sees it latches the epoch. */
static int sense_accepted_weapon_frame(uint32_t datum,uint32_t weapon,SenseAcceptedWeaponFrame *out){
    if(!out)return 0;
    if(sense_frame_live(datum,weapon)){*out=sense_accepted.frame;return 1;}
    memset(out,0,sizeof *out);return 0;
}
/* Seeding consumers. pair: the viewmodel's already leased raw pair (its
 * foregrip capture precedes admission), else NULL to lease one here.
 * eligible: the viewmodel's rig-qualified support eligibility; other
 * consumers pass 1 and sense_solve_support applies sense_support_allowed. */
static const SenseAcceptedWeaponFrame *sense_primary_frame(uint32_t unit,uint32_t datum,uint32_t weapon,
                                                           const SenseLeasedPair *pair,int eligible){
    if(!sense_frame_epoch)return NULL;
    if(sense_frame_attempted())return sense_frame_use(datum,weapon)?&sense_accepted.frame:NULL;
    sense_frame_fail();
    SenseLeasedPair leased;
    if(!pair){if(!sense_leased_pair(unit,datum,&leased)){sense_support.engaged=0;return NULL;}pair=&leased;}
    if(!sense_frame_owner(datum,weapon))return NULL;
    HaloSenseTransform right=pair->right,two_hand,primary;
    int supported=eligible&&sense_solve_support(datum,weapon,&pair->input,&right,&pair->left);
    const HaloSenseTransform *other=supported?&pair->right:NULL;
    if(!supported&&sense_support.valid&&sense_support.unit==datum&&sense_support.weapon==weapon&&
       sense_support.session==pair->input.session&&sense_supported_pose(&pair->right,&pair->left,sense_support.foregrip,&two_hand))
        other=&two_hand;
    sense_accept_orientation(unit,datum,weapon,&pair->input,sense_frame_epoch,sense_frame_epoch_ns,supported,&right,other,&primary);
    SenseAcceptedWeaponFrame *f=&sense_accepted.frame;
    memset(f,0,sizeof *f);
    f->epoch=sense_frame_epoch;f->epoch_ns=sense_frame_epoch_ns;f->session=pair->input.session;f->generation=pair->input.generation;
    f->unit=datum;f->weapon=weapon;f->supported=supported;f->primary=primary;f->left=pair->left;f->visual_left=pair->left;
    if(supported){
        /* The rendered support wrist, frozen with the pose it belongs to. */
        sense_point(&primary,sense_support.foregrip,f->visual_left.p);
        for(unsigned k=0;k<3;k++)sense_rotate(&primary,sense_support.left_basis[k],f->visual_left.c[k]);
    }
    sense_accepted.input=pair->input;sense_accepted.raw_right=pair->right;sense_accepted.accepted=1;
    return f;
}
/* The offhand's own immutable epoch pose: the raw left controller only, from
 * its own leased publication, keyed by the offhand weapon. No support,
 * continuity or scope filtering, and no state shared with the primary frame.
 * The mirrored offhand model, its reticle and its actual shot all use this
 * transform; shots still need the original muzzle/ownership gates. It does
 * not require the secondary model to be latched ready, so the first offhand
 * model build of an epoch can admit it. One attempt per epoch; fails closed. */
typedef struct {
    uint64_t epoch, epoch_ns, session, generation;
    uint32_t unit, weapon;
    HaloSenseTransform left;
} SenseAcceptedOffhandFrame;
static struct { SenseAcceptedOffhandFrame frame; uint64_t attempted; int accepted; } sense_offhand_accepted;
/* A non-selected weapon held in exactly one inventory slot of the local
 * on-foot campaign player whose camera this epoch sampled, whose salted
 * object's parent (+0x11C) is either that player (attached by the dual
 * update for its actual shot) or none (the holstered inventory state that
 * dual_inventory and host_sense_offhand_begin require between shots). Any
 * other parent -- dropped, taken by another unit, attached elsewhere -- fails.
 * Requiring the player as parent at all times would reject the verified
 * holstered state the mirrored model and reticle are drawn in. */
static int sense_offhand_owner(uint32_t datum,uint32_t weapon){
    uint32_t local,unit=sense_local_body(&local),body=sense_object(weapon,0x340,2);uint64_t now=host_monotonic_ns();
    if(!unit||local!=datum||!sense_frame_epoch||now<sense_frame_epoch_ns||now-sense_frame_epoch_ns>SENSE_FRAME_MAX_AGE_NS||
       sense_camera_unit!=datum||G16(0x00719720u)!=0||G16(unit+0x2F0)!=UINT16_MAX||!body)return 0;
    uint32_t parent=G32(body+0x11C);
    if(parent!=datum&&parent!=UINT32_MAX)return 0;
    int slot=(int16_t)G16(unit+0x2F2);unsigned held=0;
    for(unsigned i=0;i<4;i++)if(G32(unit+0x2F8+4*i)==weapon){if((int)i==slot)return 0;held++;}
    return held==1;
}
static int sense_offhand_live(uint32_t datum,uint32_t weapon){
    const SenseAcceptedOffhandFrame *f=&sense_offhand_accepted.frame;HaloSenseDualInput input;
    return sense_offhand_accepted.accepted&&sense_offhand_accepted.attempted==sense_frame_epoch&&f->epoch==sense_frame_epoch&&
        f->unit==datum&&f->weapon==weapon&&sense_offhand_owner(datum,weapon)&&
        halo_sense_dual_input(&input)&&input.session==f->session&&input.generation==f->generation;
}
/* Cosmetic consumers may inspect an already accepted offhand only. Never
 * seed/advance the independent offhand cache or latch a failed observation. */
static int sense_accepted_offhand_frame(uint32_t datum,uint32_t weapon,SenseAcceptedOffhandFrame *out){
    if(!out)return 0;
    if(sense_offhand_live(datum,weapon)){*out=sense_offhand_accepted.frame;return 1;}
    memset(out,0,sizeof *out);return 0;
}
static const SenseAcceptedOffhandFrame *sense_offhand_frame(uint32_t unit,uint32_t datum,uint32_t weapon){
    if(!sense_frame_epoch)return NULL;
    if(sense_offhand_accepted.attempted==sense_frame_epoch){
        const SenseAcceptedOffhandFrame *f=&sense_offhand_accepted.frame;
        if(f->unit!=datum||f->weapon!=weapon)return NULL; /* another key: not an observation of this frame */
        if(sense_offhand_live(datum,weapon))return f;
        sense_offhand_accepted.accepted=0;return NULL;    /* latched closed for this epoch */
    }
    sense_offhand_accepted.attempted=sense_frame_epoch;sense_offhand_accepted.accepted=0;
    SenseLeasedPair pair;
    if(!sense_offhand_owner(datum,weapon)||!sense_leased_pair(unit,datum,&pair))return NULL;
    SenseAcceptedOffhandFrame *f=&sense_offhand_accepted.frame;
    *f=(SenseAcceptedOffhandFrame){.epoch=sense_frame_epoch,.epoch_ns=sense_frame_epoch_ns,.session=pair.input.session,
        .generation=pair.input.generation,.unit=datum,.weapon=weapon,.left=pair.left};
    sense_offhand_accepted.accepted=1;
    return f;
}
/* Substantive hook entries (fire, viewmodel, main-loop tick, reticle
 * publication) call this whatever path they then take, so a menu, death,
 * weapon/owner change or lease change that makes them return early still
 * latches each accepted frame closed against its own identity. */
static void sense_frame_audit(void){
    if(sense_frame_attempted()&&sense_accepted.accepted)(void)sense_frame_use(sense_accepted.frame.unit,sense_accepted.frame.weapon);
    if(sense_frame_epoch&&sense_offhand_accepted.attempted==sense_frame_epoch&&sense_offhand_accepted.accepted&&
       !sense_offhand_live(sense_offhand_accepted.frame.unit,sense_offhand_accepted.frame.weapon))sense_offhand_accepted.accepted=0;
}
