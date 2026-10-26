/* Independent 6-DoF weapon presentation and firing for the verified retail
 * Halo PC 1.10 guest. This implementation preserves the original skeleton,
 * trigger, spread, damage and projectile routines. It does not rotate the
 * camera, emulate a mouse, relocate the player or write save data.
 *
 * References: LivingFray/HaloCEVR 18603218f96e9c31f887c069c1a112fe1edb943a
 * documents the same fire/skeleton boundaries. No implementation is copied.
 * ABI and offsets below were checked against the user's verified executable
 * c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545.
 * Kept opt-in until the physical tracked-pose acceptance test is complete. */
#include "sense_pose.h"
#include "sense_weapon.h"
#include <stdatomic.h>
#include <pthread.h>
static int sense_fire_nested, sense_ray_nested, sense_model_nested;
static uint32_t sense_firing_unit, sense_firing_weapon;
static HaloSenseTransform sense_firing_ray;
static HaloSenseTransform sense_camera;
static float sense_camera_from_unit[3];
static uint32_t sense_camera_unit=UINT32_MAX;
static uint64_t sense_camera_ns;
typedef struct { uint32_t weapon,unit; float muzzle[3]; int valid; uint64_t timestamp,session,generation; } SenseMuzzle;
static SenseMuzzle sense_muzzles[2];
/* Captured from the original animated rifle at grab time. Keeping the
 * foregrip in barrel space avoids stretching or steering by reload motion. */
static struct {
    uint32_t unit,weapon; uint64_t session,generation;
    float foregrip[3],left_basis[3][3]; int valid,engaged;
} sense_support;
static uint32_t dual_inventory(uint32_t *,uint32_t *,uint32_t *,int *);
static _Atomic unsigned sense_status_support,sense_status_dual;
static _Atomic uint64_t sense_status_support_ns,sense_status_dual_ns;
void host_sense_weapon_status(HaloSenseWeaponStatus *out) {
    if(!out)return;memset(out,0,sizeof *out);
    uint64_t now=host_monotonic_ns();if(!halo_sense_active())return;
    uint64_t stamp=atomic_load(&sense_status_support_ns);
    if(now>=stamp&&now-stamp<=UINT64_C(250000000))out->support=(int)atomic_load(&sense_status_support);
    stamp=atomic_load(&sense_status_dual_ns);
    if(now>=stamp&&now-stamp<=UINT64_C(250000000))out->dual=(int)atomic_load(&sense_status_dual);
}
static void sense_status(int support,int dual) {
    uint64_t now=host_monotonic_ns();
    if(support>=0){atomic_store(&sense_status_support,(unsigned)support);atomic_store(&sense_status_support_ns,now);}
    if(dual>=0){atomic_store(&sense_status_dual,(unsigned)dual);atomic_store(&sense_status_dual_ns,now);}
}

static unsigned sense_model_hand=1;
static uint32_t sense_secondary_weapon=UINT32_MAX,sense_secondary_unit=UINT32_MAX;
static int sense_secondary_ready;
#define sense_muzzle sense_muzzles[sense_model_hand]
int host_sense_weapon_rendering(void) {
    if(!halo_sense_requested())return 0;
    HostGCSnapshot pad={0};
    /* A selected ordinary pad is authoritative immediately, even before the
     * compositor observes the handover. A lost Sense pair is not a handover. */
    if(hostgc_poll(&pad))halo_sense_observe_controller(&pad);
    if(pad.connected&&!pad.spatial)return 0;
    return halo_sense_active();
}
static int sense_range(uint64_t p,size_t n) {
    return p>=0x10000u && p<=UINT32_MAX && n<=UINT64_C(0x100000000)-p;
}
static uint32_t sense_object(uint32_t datum,size_t n,unsigned kind) {
    uint32_t table=G32(0x008603B0u);
    if(datum==UINT32_MAX || !sense_range(table,0x38))return 0;
    unsigned count=G16(table+0x20),index=datum&65535u;uint32_t entries=G32(table+0x34);
    if(!count||count>4096||index>=count||G16(table+0x22)!=12||!sense_range(entries,count*12u))return 0;
    uint32_t e=entries+index*12u,body=G32(e+8);
    if(!G16(e)||G16(e)!=(datum>>16)||G16(e+6)<n||!sense_range(body,n)||G16(body+0xB4)!=kind)return 0;
    return body;
}
static uint32_t sense_local_body(uint32_t *datum) {
    /* Menus, cutscenes/input-disabled and remote
     * objects cannot become pose targets. */
    if(G8(0x00718FC9u))return 0;
    uint32_t globals=G32(0x0087A478u),array=G32(0x0087A480u);
    if(!sense_range(globals,0x12)||G8(globals+0x11)||!sense_range(array,0x38))return 0;
    uint32_t player=G32(globals+4),rows=G32(array+0x34);unsigned count=G16(array+0x20),i=player&65535u;
    if(player==UINT32_MAX||!count||count>16||i>=count||G16(array+0x22)!=0x200||!sense_range(rows,count*0x200u))return 0;
    uint32_t row=rows+i*0x200u;if(G16(row)!=(player>>16))return 0;
    *datum=G32(row+0x34);uint32_t unit=sense_object(*datum,0x308,0);
    float health=0;if(unit)memcpy(&health,GPTR(unit+0xE0),4);
    return isfinite(health)&&health>0.f?unit:0;
}
static uint32_t sense_local_unit_any(uint32_t *datum,uint32_t *weapon) {
    uint32_t unit=sense_local_body(datum);if(!unit)return 0;
    int slot=(int16_t)G16(unit+0x2F2);
    if(slot<0||slot>=4||G16(unit+0x2F4)!=(unsigned)slot)return 0;
    *weapon=G32(unit+0x2F8+4u*(unsigned)slot);uint32_t w=sense_object(*weapon,0x340,2);
    return w&&G32(w+0x11C)==*datum?unit:0;
}
static uint32_t sense_local_unit(uint32_t *datum,uint32_t *weapon) {
    uint32_t unit=sense_local_unit_any(datum,weapon);
    return unit&&G16(0x00719720u)==0&&G16(unit+0x2F0)==UINT16_MAX&&G32(unit+0x11C)==UINT32_MAX?unit:0;
}
static uint32_t sense_tag(uint32_t datum,size_t minimum) {
    uint32_t tags=G32(0x0087BC14u);
    if(datum==UINT32_MAX||!sense_range(tags,0x20u*((datum&65535u)+1)))return 0;
    uint32_t row=tags+(datum&65535u)*32u,data=G32(row+0x14);
    return G32(row+0xC)==datum&&sense_range(data,minimum)?data:0;
}
static int sense_weapon_path(uint32_t weapon,char path[80]) {
    uint32_t w=sense_object(weapon,0x340,2),tags=G32(0x0087BC14u);
    if(!w||!sense_tag(G32(w),0x46C))return 0;
    uint32_t name=G32(tags+(G32(w)&65535u)*32u+0x10);
    if(!sense_range(name,80))return 0;
    memcpy(path,GPTR(name),80);path[79]=0;return 1;
}
/* Keep dual eligibility distinct: kind3 uses a two-hand rig but must never
 * silently become a second dual-wield weapon. */
static int sense_weapon_kind(uint32_t weapon) {
    char path[80];if(!sense_weapon_path(weapon,path))return 0;
    if(!strcmp(path,"weapons\\assault rifle\\assault rifle"))return 1;
    if(!strcmp(path,"weapons\\pistol\\pistol"))return 2;
    static const char *rifles[]={"weapons\\sniper rifle\\sniper rifle","weapons\\shotgun\\shotgun",
        "weapons\\plasma rifle\\plasma rifle","weapons\\rocket launcher\\rocket launcher","weapons\\flamethrower\\flamethrower"};
    for(unsigned i=0;i<sizeof rifles/sizeof *rifles;i++)if(!strcmp(path,rifles[i]))return 3;
    return 0;
}
static int sense_finite3(const float v[3]) { return isfinite(v[0])&&isfinite(v[1])&&isfinite(v[2]); }
static int sense_world_frame_hand(const HaloSenseFrame *frame,unsigned hand,uint32_t unit,uint32_t datum,HaloSenseTransform *out) {
    const HaloSenseHandPose *pose=hand?&frame->right:&frame->left;
    const float *p=pose->position,*f=pose->forward,*u=pose->up;uint64_t now=host_monotonic_ns();
    if(sense_camera_unit!=datum||now<sense_camera_ns||now-sense_camera_ns>UINT64_C(250000000)||
       !pose->valid||!sense_finite3(p)||sense_dot(p,p)>9.f)return 0;
    float wf[3],wu[3],offset[3],position[3];
    sense_rotate(&sense_camera,f,wf);sense_rotate(&sense_camera,u,wu);
    if(!sense_basis(wf,wu,out))return 0;
    sense_rotate(&sense_camera,p,offset);memcpy(position,GPTR(unit+0x5C),12);
    if(!sense_finite3(position))return 0;
    for(unsigned k=0;k<3;k++)out->p[k]=position[k]+sense_camera_from_unit[k]+offset[k]/3.048f;
    return sense_finite3(out->p);
}
static int sense_world_pair(uint32_t unit,uint32_t datum,HaloSenseTransform *left,HaloSenseTransform *right) {
    HaloSenseFrame frame;if(!halo_sense_read(&frame))return 0;
    return sense_world_frame_hand(&frame,0,unit,datum,left)&&sense_world_frame_hand(&frame,1,unit,datum,right);
}
static int sense_world_hand(unsigned hand,uint32_t unit,uint32_t datum,HaloSenseTransform *out) {
    HaloSenseFrame frame;return halo_sense_read(&frame)&&sense_world_frame_hand(&frame,hand,unit,datum,out);
}
static int sense_physical_left_busy(void); /* sense_physical_hooks.inc: latched per outer epoch; left hand holds a magazine or pickup */
static int sense_support_allowed(uint32_t datum,uint32_t weapon,const HaloSenseDualInput *input) {
    int kind=sense_weapon_kind(weapon);
    if((kind!=1&&kind!=3)||input->support_blocked||sense_physical_left_busy())return 0;
    uint32_t pair_unit,primary,secondary;int node;
    return !halo_sense_dual_requested()||!dual_inventory(&pair_unit,&primary,&secondary,&node)||pair_unit!=datum||primary!=weapon;
}
static int sense_solve_support(uint32_t datum,uint32_t weapon,const HaloSenseDualInput *input,
                               HaloSenseTransform *right,const HaloSenseTransform *left) {
    if(!sense_support.valid||sense_support.unit!=datum||sense_support.weapon!=weapon||sense_support.session!=input->session){
        sense_support.engaged=0;return 0;
    }
    if(sense_support.generation!=input->generation){sense_support.engaged=0;sense_support.generation=input->generation;}
    if(!sense_support_allowed(datum,weapon,input)||!sense_support_span(right,left,sense_support.engaged)){
        sense_support.engaged=0;return 0;
    }
    HaloSenseTransform solved;if(!sense_supported_pose(right,left,sense_support.foregrip,&solved)){sense_support.engaged=0;return 0;}
    *right=solved;sense_support.engaged=1;return 1;
}
#include "sense_weapon_frame.inc"
/* Each hand is its epoch's accepted frame (sense_weapon_frame.inc), never a
 * fresh read: hand1 the primary frame, hand0 the separate offhand frame. */
static int sense_world_weapon(unsigned hand,uint32_t unit,uint32_t datum,uint32_t weapon,HaloSenseTransform *out) {
    if(hand==1){
        const SenseAcceptedWeaponFrame *frame=sense_primary_frame(unit,datum,weapon,NULL,1);
        if(!frame)return 0;
        *out=frame->primary;return 1;
    }
    const SenseAcceptedOffhandFrame *offhand=sense_offhand_frame(unit,datum,weapon);
    if(!offhand)return 0;
    *out=offhand->left;return 1;
}
#include "sense_weapon_feedback.inc"
#include "sense_action.inc"
#include "sense_grenade.inc"
static int sense_node(uint32_t address,HaloSenseTransform *out) {
    float scale;memcpy(&scale,GPTR(address),4);
    memcpy(out->c,GPTR(address+4),36);memcpy(out->p,GPTR(address+40),12);
    return isfinite(scale)&&fabsf(scale)>.00001f&&sense_finite3(out->p)&&sense_basis(out->c[0],out->c[2],out);
}
static int sense_descendant(unsigned i,int ancestor,const int16_t *parents,unsigned count) {
    for(unsigned steps=0;steps<count&&i<count;steps++) {
        if((int)i==ancestor)return 1;
        if(parents[i]<0)return 0;i=(unsigned)parents[i];
    }
    return 0;
}
static void sense_apply_node(uint32_t address,const HaloSenseTransform *delta) {
    float v[3];for(unsigned c=0;c<3;c++){memcpy(v,GPTR(address+4+c*12u),12);sense_rotate(delta,v,v);memcpy(GPTR(address+4+c*12u),v,12);}
    memcpy(v,GPTR(address+40),12);sense_point(delta,v,v);memcpy(GPTR(address+40),v,12);
}
#include "sense_physical_hooks.inc"
#include "sense_grenade_hold.inc"
/* Original004D7850 reads instance.node at +2 and remaps model nodes to
 * animation nodes; model node names are the first 32 bytes of each 0x9C row
 * (004D7690). Resolve names explicitly rather than assuming the gun node. */
static int sense_muzzle_marker(uint32_t weapon,uint32_t bones,unsigned bone_count,
                               HaloSenseTransform *out,int *bone) {
    uint32_t w=sense_object(weapon,0x340,2),data=w?sense_tag(G32(w),0x46C):0;
    uint32_t model=data?sense_tag(G32(data+0x468),0xC0):0;if(!model)return 0;
    uint32_t count=G32(model+0xAC),markers=G32(model+0xB0),nodes=G32(model+0xBC),node_count=G32(model+0xB8);
    if(count>128||!sense_range(markers,count*64u)||!node_count||node_count>64||!sense_range(nodes,node_count*0x9Cu))return 0;
    for(unsigned i=0;i<count;i++){
        uint32_t marker=markers+i*64u;char name[33]={0};memcpy(name,GPTR(marker),32);
        if(strcmp(name,"primary trigger"))continue;
        uint32_t n=G32(marker+0x34),instance=G32(marker+0x38);
        /* Multiple region/permutation-specific muzzles require the engine's
         * active-permutation mapping, so do not guess which instance to use. */
        if(n!=1||!sense_range(instance,32)||G8(instance+2)>=node_count)return 0;
        char model_name[33]={0};memcpy(model_name,GPTR(nodes+G8(instance+2)*0x9Cu),32);*bone=-1;
        for(unsigned j=0;j<bone_count;j++){
            char graph_name[33]={0};memcpy(graph_name,GPTR(bones+j*64u),32);
            if(!strcmp(model_name,graph_name)){if(*bone!=-1)return 0;*bone=(int)j;}
        }
        if(*bone<0)return 0;
        float q[4];memcpy(q,GPTR(instance+16),16);memcpy(out->p,GPTR(instance+4),12);
        float n2=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
        if(!isfinite(n2)||n2<1e-8f||!sense_finite3(out->p)||sense_dot(out->p,out->p)>=16.f)return 0;
        /* Halo quaternion convention, verified against original004CBAD0. */
        float t=2.f/n2,x=q[0],y=q[1],z=q[2],wq=q[3];
        float c[3][3]={{1-t*(y*y+z*z),t*(x*y-wq*z),t*(x*z+wq*y)},
                       {t*(x*y+wq*z),1-t*(x*x+z*z),t*(y*z-wq*x)},
                       {t*(x*z-wq*y),t*(y*z+wq*x),1-t*(x*x+y*y)}};
        memcpy(out->c,c,sizeof c);return 1;
    }
    return 0;
}
static int sense_muzzle_ready(unsigned hand,uint32_t unit,uint32_t weapon) {
    HaloSenseDualInput input;SenseMuzzle *m=&sense_muzzles[hand];uint64_t now=host_monotonic_ns();
    return halo_sense_dual_input(&input)&&m->valid&&m->unit==unit&&m->weapon==weapon&&
        m->session==input.session&&m->generation==input.generation&&now>=m->timestamp&&now-m->timestamp<UINT64_C(150000000);
}
static int sense_update_model(EngineCPU *cpu,uint32_t address) {
    if(sense_model_nested||G32(cpu->gpr[4])!=0x00493C49u)return 0;
    sense_muzzle.valid=0; /* Invalidate even if this frame has an unknown rig. */
    uint32_t datum,weapon,unit=sense_local_unit(&datum,&weapon);
    /* The put-away half of an original switch is drawn natively below. */
    int switching=!unit&&sense_model_hand==1&&(unit=sense_switching_unit(&datum,&weapon))!=0;
    if(!unit)return 0;
    if(!sense_model_hand){if(datum!=sense_secondary_unit)return 0;weapon=sense_secondary_weapon;}
    uint32_t graph=sense_tag(cpu->gpr[0],0x70),output=ARG(0);
    unsigned count=graph?G32(graph+0x68):0;uint32_t bones=graph?G32(graph+0x6C):0;
    if(!count||count>64||!sense_range(bones,count*64u)||!sense_range(output,count*52u))return 0;
    int right=-1,left=-1,gun=-1;int16_t parents[64];
    for(unsigned i=0;i<count;i++){
        char name[33]={0};memcpy(name,GPTR(bones+i*64u),32);parents[i]=(int16_t)G16(bones+i*64u+0x24);
        if(parents[i]>= (int)count)return 0;
        if(strstr(name,"r wrist"))right=(int)i;else if(strstr(name,"l wrist"))left=(int)i;
        else if(gun<0&&(strstr(name,"gun")||strstr(name,"body")))gun=(int)i;
    }
    uint32_t caller=G32(cpu->gpr[4]);sense_model_nested=1;engine_dispatch(cpu,address);sense_model_nested=0;
    sense_muzzle.valid=0;
    if(cpu->pc!=caller)return 1;
    int action=sense_action_for_unit(unit,datum,weapon,NULL);
    /* The mirrored offhand keeps its tracked rig except for melee. */
    int native=sense_model_hand==1?sense_action_native(action):action==HALO_SENSE_ACTION_MELEE;
    if(native){
        /* Melee, reload and put-away/ready: keep every original animated
         * node, including shoulders, the gun swing and the magazine hand, for
         * exactly as long as the original plays that class. Only rotate the
         * entire rig into the current view; per-wrist anchoring would cancel
         * the animation's moving pivot. The first build after the original
         * leaves the class restores the tracked hands. */
        sense_support.engaged=0;sense_physical_interrupt();
        if(sense_model_hand==1)sense_grenade_hold_clear();
        HaloSenseTransform head;float eye[3];
        if(sense_world_head(unit,datum,&head,eye)){
            HaloSenseTransform delta=sense_delta(&sense_camera,&head,eye,eye);
            for(unsigned i=0;i<count;i++)sense_apply_node(output+i*52u,&delta);
        }
        /* Dual: the live offhand occupies the left hand, so the primary's
         * animated left arm collapses to one point (its shoulder, or its
         * wrist when that chain also carries the gun). */
        if(sense_model_hand==1&&action!=HALO_SENSE_ACTION_MELEE&&sense_secondary_ready&&left>=0){
            int upper=sense_arm_named(bones,count,"frame l upperarm");
            int top=upper>=0&&sense_descendant((unsigned)left,upper,parents,count)?upper:left;
            if((right>=0&&sense_descendant((unsigned)right,top,parents,count))||(gun>=0&&sense_descendant((unsigned)gun,top,parents,count)))top=left;
            float at[3];memcpy(at,GPTR(output+(unsigned)top*52u+40),12);
            for(unsigned i=0;i<count;i++)if(sense_descendant(i,top,parents,count)){S32(output+i*52u,0);memcpy(GPTR(output+i*52u+40),at,12);}
        }
        return 1;
    }
    if(switching)return 1; /* (unreachable) never a tracked rig for a slot being put away */
    HaloSenseTransform rh={0},lh={0},oldr,oldl,oldbarrel,marker,marker_node;int marker_bone=-1;
    /* The primary build uses this epoch's leased pair: the admitted frame's
     * when another consumer came first, else one it admits below. */
    SenseLeasedPair leased;memset(&leased,0,sizeof leased);
    int pair,admitted=sense_model_hand==1&&sense_frame_attempted();
    if(sense_model_hand!=1){
        const SenseAcceptedOffhandFrame *offhand=sense_offhand_frame(unit,datum,weapon);
        pair=offhand!=NULL;if(pair)lh=rh=offhand->left;
    }
    else if(admitted){
        pair=sense_frame_use(datum,weapon);
        if(pair){lh=sense_accepted.frame.left;rh=sense_accepted.raw_right;leased.input=sense_accepted.input;}
    }else{pair=sense_leased_pair(unit,datum,&leased);lh=leased.left;rh=leased.right;if(!pair)sense_frame_fail();}
    /* Raw left grip of this same accepted/leased pair, before any foregrip
     * constraint: the physical magazine attaches to the real hand. */
    HaloSenseTransform raw_lh=lh;
    int rv=right>=0&&gun>=0&&pair&&
        sense_node(output+(unsigned)right*52u,&oldr)&&sense_muzzle_marker(weapon,bones,count,&marker,&marker_bone)&&
        (sense_descendant((unsigned)marker_bone,gun,parents,count)||sense_descendant((unsigned)marker_bone,right,parents,count))&&
        sense_node(output+(unsigned)marker_bone*52u,&marker_node);
    if(rv){for(unsigned k=0;k<3;k++)sense_rotate(&marker_node,marker.c[k],oldbarrel.c[k]);}
    int lv=left>=0&&pair&&sense_node(output+(unsigned)left*52u,&oldl);
    HaloSenseDualInput support_input={0};int supported=0,waiting_pose=0;
    int kind=sense_weapon_kind(weapon),rifle=rv&&sense_model_hand==1&&(kind==1||kind==3);
    if(sense_model_hand==1){
        int framed=rifle&&lv;if(framed)support_input=leased.input;
        int eligible=framed&&sense_support_allowed(datum,weapon,&support_input);
        int grabbed=eligible&&sense_support_span(&rh,&lh,sense_support.engaged);
        if(!eligible)sense_support.engaged=0;
        if(sense_support.unit!=datum||sense_support.weapon!=weapon||sense_support.session!=support_input.session)sense_support.valid=0;
        if(eligible&&!sense_support.valid){
            sense_support.valid=0;
            /* Original492E60 stores animation class at FPcache+0xC; class0
             * is the default idle selected by493C60 and restored by493150.
             * A first grab during reload/equip must wait, not turn its
             * animated magazine-hand position into a permanent foregrip. */
            uint32_t cache=G32(0x006B2D98u),body=sense_object(weapon,0x340,2);
            waiting_pose=!sense_range(cache,0x28)||!G8(cache)||G32(cache+4)!=datum||
                G32(cache+8)!=weapon||G16(cache+0xC)!=0||!body||G16(body+0x2B0)!=0;
            if(!waiting_pose){
                float offset[3],candidate[3];for(unsigned k=0;k<3;k++)offset[k]=oldl.p[k]-oldr.p[k];
                sense_unrotate(&oldbarrel,offset,candidate);
                float metres2=sense_dot(candidate,candidate)*3.048f*3.048f;
                if(isfinite(metres2)&&metres2>=.08f*.08f&&metres2<=1.1f*1.1f){
                    memcpy(sense_support.foregrip,candidate,12);
                    for(unsigned k=0;k<3;k++)sense_unrotate(&oldbarrel,oldl.c[k],sense_support.left_basis[k]);
                    sense_support.unit=datum;sense_support.weapon=weapon;sense_support.session=support_input.session;
                    sense_support.generation=support_input.generation;sense_support.valid=1;
                }
            }
        }
        const SenseAcceptedWeaponFrame *accepted=pair?sense_primary_frame(unit,datum,weapon,admitted?NULL:&leased,eligible):NULL;
        if(!accepted)rv=lv=0; /* fail closed: nothing drawn or armed from a second pose */
        else{rh=accepted->primary;supported=accepted->supported;raw_lh=accepted->left;if(supported)lh=accepted->visual_left;}
        sense_status(supported?HALO_SENSE_SUPPORT_HELD:waiting_pose?HALO_SENSE_SUPPORT_WAITING_POSE:grabbed?HALO_SENSE_SUPPORT_INVALID_SPAN:
                     rifle?HALO_SENSE_SUPPORT_READY:HALO_SENSE_SUPPORT_NONE,-1);
    }
    /* Optional connected arms: qualify the chain on the original animation
     * before any tracked delta (sense_physical_hooks.inc). */
    SenseArmCapture arms;sense_arms_capture(&arms,output,bones,count,parents,left,right);
    HaloSenseTransform rd={0},ld={0};
    if(rv)rd=sense_delta(&oldbarrel,&rh,oldr.p,rh.p);
    if(lv)ld=sense_delta(&oldl,&lh,oldl.p,lh.p);
    for(unsigned i=0;i<count;i++){
        uint32_t node=output+i*52u;
        if(rv&&(sense_descendant(i,right,parents,count)||sense_descendant(i,gun,parents,count)))sense_apply_node(node,&rd);
        else if(lv&&sense_descendant(i,left,parents,count))sense_apply_node(node,&ld);
        else S32(node,0); /* Hide camera-attached forearms; no stretched shoulder. */
    }
    /* Zero-scale forearms need their collapse point at the tracked wrist.
     * Leaving their old camera-space translation creates long skin triangles
     * between the hand and its collapsed parent. */
    const int wrists[2]={left,right},valid[2]={lv,rv};
    for(unsigned h=0;h<2;h++)if(valid[h]&&wrists[h]>=0&&parents[wrists[h]]>=0){
        uint32_t wrist=output+(unsigned)wrists[h]*52u,parent=output+(unsigned)parents[wrists[h]]*52u;
        memcpy(GPTR(parent+4),GPTR(wrist+4),48);S32(parent,0);
    }
    /* The armed grenade (once per epoch) may take the dual offhand's hand. */
    if(sense_model_hand==1)sense_grenade_hold_frame(output,bones,count,parents,left,lv,!supported&&sense_secondary_ready,unit,datum,weapon,&raw_lh);
    int left_hidden=lv&&(!sense_model_hand||(!supported&&sense_secondary_ready&&!sense_grenade_hold_owns_left()));
    if(left_hidden){
        for(unsigned i=0;i<count;i++)if(sense_descendant(i,left,parents,count)||(int)i==parents[left]){
            S32(output+i*52u,0);memcpy(GPTR(output+i*52u+40),lh.p,12);
        }
    }
    /* Arms solve only on the primary, unreflected rig; the mirrored offhand
     * pass and a hidden left hand keep the existing collapse. */
    if(sense_model_hand==1){const int visible[2]={lv&&!left_hidden,rv};sense_arms_connect(&arms,output,visible,datum,weapon,graph);
        sense_magazine_frame(output,bones,count,parents,gun,marker_bone,unit,datum,weapon,&raw_lh,pair&&lv&&!left_hidden,rv);
        sense_pickup_epoch_update(unit,datum,weapon); /* once per epoch, after this epoch's admission */}
    /* Reflect the complete secondary rig in the hand's vertical/forward
     * plane. The mirrored right-grip geometry becomes a left grip, including
     * the weapon; its draw scope reverses triangle winding. */
    if(rv&&!sense_model_hand){
        for(unsigned i=0;i<count;i++){
            uint32_t node=output+i*52u;
            for(unsigned c=0;c<3;c++){
                float v[3];memcpy(v,GPTR(node+4+12*c),12);
                float d=2.f*sense_dot(v,rh.c[1]);for(unsigned k=0;k<3;k++)v[k]-=d*rh.c[1][k];
                memcpy(GPTR(node+4+12*c),v,12);
            }
            float p[3],offset[3];memcpy(p,GPTR(node+40),12);
            for(unsigned k=0;k<3;k++)offset[k]=p[k]-rh.p[k];
            float d=2.f*sense_dot(offset,rh.c[1]);for(unsigned k=0;k<3;k++)p[k]-=d*rh.c[1][k];
            memcpy(GPTR(node+40),p,12);
        }
    }
    if(rv){
        HaloSenseTransform moved;float muzzle[3],relative[3];
        if(sense_node(output+(unsigned)marker_bone*52u,&moved)){
            /* Keep the actual reflected affine basis instead of rebuilding
             * a right-handed basis for marker position. */
            uint32_t matrix=output+(unsigned)marker_bone*52u;float scale;
            memcpy(&scale,GPTR(matrix),4);memcpy(moved.c,GPTR(matrix+4),36);
            float local[3]={marker.p[0]*scale,marker.p[1]*scale,marker.p[2]*scale};
            sense_point(&moved,local,muzzle);for(unsigned k=0;k<3;k++)relative[k]=muzzle[k]-rh.p[k];
            sense_unrotate(&rh,relative,sense_muzzle.muzzle);sense_muzzle.weapon=weapon;sense_muzzle.unit=datum;
            HaloSenseDualInput input;if(halo_sense_dual_input(&input)){
                sense_muzzle.session=input.session;sense_muzzle.generation=input.generation;
                sense_muzzle.timestamp=host_monotonic_ns();sense_muzzle.valid=1;
            }
        }
    }
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){
        static unsigned logs;if(logs++<12)host_log("[sense-model] hand=%u weapon=%08x count=%u right=%d left=%d gun=%d rightValid=%d leftValid=%d muzzle=%d",sense_model_hand,weapon,count,right,left,gun,rv,lv,sense_muzzle.valid);
    }
    return 1;
}
/* The source projectile safety sweep004F7B70 uses00505880 with flags1000E9
 * from the player's eye to its spawn point. Run that same collision query
 * before consuming ammo. A tracked muzzle behind geometry blocks the trigger
 * rather than launching a projectile through a wall or relocating the unit. */
static int sense_muzzle_clear(EngineCPU *cpu,uint32_t unit,uint32_t datum,const float muzzle[3]) {
    static uint32_t scratch;
    if(!scratch)scratch=guest_alloc(0x100);
    if(!sense_range(scratch,0x100))return 0;
    float eye[3],delta[3];memcpy(eye,GPTR(unit+0x5C),12);
    for(unsigned k=0;k<3;k++){eye[k]+=sense_camera_from_unit[k];delta[k]=muzzle[k]-eye[k];}
    if(!sense_finite3(eye)||!sense_finite3(delta)||sense_dot(delta,delta)>16.f)return 0;
    memcpy(GPTR(scratch),eye,12);memcpy(GPTR(scratch+16),delta,12);memset(GPTR(scratch+32),0,0x80);
    EngineCPU check=*cpu;uint32_t args[5]={0x001000E9u,scratch,scratch+16,datum,scratch+32};
    uint32_t hit=host_call_guest(&check,0x00505880u,5,args,0);
    cpu->instruction_count=check.instruction_count;
    if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){
        static unsigned logs;if(logs++<128)host_log("[sense-clearance] hit=%u eye=%.6f,%.6f,%.6f muzzle=%.6f,%.6f,%.6f",hit&255u,eye[0],eye[1],eye[2],muzzle[0],muzzle[1],muzzle[2]);
    }
    return (hit&255u)==0;
}
static void sense_publish_reticles(EngineCPU *cpu) {
    sense_frame_audit();
    HaloSenseDualInput input;HaloSenseReticleFrame frame={0};uint32_t datum,weapon,unit=sense_local_unit(&datum,&weapon);
    /* During a primary reload/swap its muzzle is not ready; the offhand's stays. */
    if(!unit||!sense_action_offhand_live(sense_action_for_unit(unit,datum,weapon,NULL))||!halo_sense_dual_input(&input)){
        halo_sense_reticle_publish(NULL);return;}
    frame.session=input.session;frame.generation=input.generation;
    static uint32_t scratch;if(!scratch)scratch=guest_alloc(0x100);
    if(!sense_range(scratch,0x100)){halo_sense_reticle_publish(NULL);return;}
    float eye[3];memcpy(eye,GPTR(unit+0x5C),12);for(unsigned k=0;k<3;k++)eye[k]+=sense_camera_from_unit[k];
    for(unsigned h=0;h<2;h++){
        uint32_t id=h?weapon:sense_secondary_weapon;HaloSenseTransform ray;
        if((!h&&(!halo_sense_dual_requested()||sense_grenade_hold_owns_left()))||!sense_muzzle_ready(h,datum,id)||!sense_world_weapon(h,unit,datum,id,&ray))continue;
        float muzzle[3];sense_point(&ray,sense_muzzles[h].muzzle,muzzle);memcpy(ray.p,muzzle,12);
        if(!sense_muzzle_clear(cpu,unit,datum,ray.p))continue;
        float delta[3];for(unsigned k=0;k<3;k++)delta[k]=ray.c[0][k]*50.f;
        memcpy(GPTR(scratch),ray.p,12);memcpy(GPTR(scratch+16),delta,12);memset(GPTR(scratch+32),0,0x80);
        EngineCPU q=*cpu;uint32_t args[5]={0x001000E9u,scratch,scratch+16,datum,scratch+32};
        unsigned hit=host_call_guest(&q,0x00505880u,5,args,0)&255u;cpu->instruction_count=q.instruction_count;
        float point[3];if(hit)memcpy(point,GPTR(scratch+32+0x18),12);
        else for(unsigned k=0;k<3;k++)point[k]=ray.p[k]+delta[k];
        if(!sense_finite3(point))continue;
        float origin[3],target[3];for(unsigned k=0;k<3;k++){origin[k]=(ray.p[k]-eye[k])*3.048f;target[k]=(point[k]-eye[k])*3.048f;}
        HaloSenseReticle *r=&frame.hands[h];sense_unrotate(&sense_camera,origin,r->origin);
        sense_unrotate(&sense_camera,ray.c[0],r->direction);sense_unrotate(&sense_camera,target,r->point);r->valid=1;r->hit=hit!=0;r->weapon=id;
        if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){static unsigned n;if(n++<24)host_log("[sense-reticle] hand=%u hit=%u origin=%.5f,%.5f,%.5f direction=%.5f,%.5f,%.5f point=%.5f,%.5f,%.5f",h,hit,r->origin[0],r->origin[1],r->origin[2],r->direction[0],r->direction[1],r->direction[2],r->point[0],r->point[1],r->point[2]);}
    }
    halo_sense_reticle_publish(&frame);
}
#include "sense_reticle_capture.inc"
#include "sense_dual_render.inc"
#include "sense_reticle_offhand.inc"
#include "sense_mount.inc"
static int host_sense_weapon_dispatch(EngineCPU *cpu,uint32_t address) {
    /* The retail player-fire path applies weapon tag offsets and body/view
     * auto-aim AFTER 005658F0. Restore the already sampled gun line at that
     * final pre-spread boundary. Original dispersion, velocity, projectile
     * ownership and physics continue unchanged; NPC/ordinary-pad calls do not.
     * A shot accepted by the outer hook owns its ray through this call. */
    if(address==0x004593B0u&&sense_fire_nested&&G32(cpu->gpr[4])==0x004C500Fu){
        uint32_t origin=ARG(0),forward=ARG(1);
        if(sense_range(origin,12)&&sense_range(forward,12)){
            memcpy(GPTR(origin),sense_firing_ray.p,12);
            memcpy(GPTR(forward),sense_firing_ray.c[0],12);
            sense_shot_projectile();
            cpu->gpr[0]=UINT32_MAX;ret_to_caller(cpu);return 1;
        }
    }
    /* Original seat entry, the only entry writer, while Sense owns the
     * weapon path: observe the live identity before it (an exit completed
     * since the last poll, even in this game tick) and after it (the entry),
     * so every seated->on-foot->seated cycle is distinct. Arguments, return
     * and an escaped return are the original's; an ordinary pad, or Sense
     * off, runs the original alone with no mount mutation. */
    if(address==0x00566970u&&!sense_mount_nested&&host_sense_weapon_rendering()){
        (void)sense_mount_observe();
        sense_mount_nested=1;engine_dispatch(cpu,address);sense_mount_nested=0;
        (void)sense_mount_observe();return 1;
    }
    if(sense_grenade_hold_dispatch(cpu,address))return 1;
    if(address!=0x0056E080u&&address!=0x0056E280u&&address!=0x004BEF80u&&address!=0x0056E440u&&address!=0x004B2CF0u&&address!=0x0050BEA0u&&address!=0x004D6880u&&address!=0x005658F0u&&address!=0x004C3F10u&&
       address!=0x004923D0u&&address!=0x00492430u&&address!=0x004924B0u&&address!=0x004ACAD0u&&address!=0x004C6E80u&&address!=0x00494010u)return 0;
    if(address==0x0050BEA0u&&!panorama_nested&&!sense_scope_nested)sense_scope_clear_frame();
    if(address==0x004C3F10u&&halo_sense_app_input_blocked()){
        uint32_t datum,local=sense_local_body(&datum),weapon=ARG(0),body=sense_object(weapon,0x340,2);
        if(local){
            int owned=body&&G32(body+0x11C)==datum;
            for(unsigned slot=0;slot<4;slot++)if(G32(local+0x2F8+slot*4)==weapon)owned=1;
            if(body&&G32(local+0x11C)!=UINT32_MAX&&G32(body+0x11C)==G32(local+0x11C))owned=1; /* the seat's own weapon */
            if(owned){ret_to_caller(cpu);return 1;}
        }
    }
    /* A claimed physical release is honoured or cancelled even if Sense lost
     * rendering ownership after it; ordinary input claims nothing. */
    if(address==0x004BEF80u||address==0x0056E080u||address==0x0056E280u||address==0x0056E440u)return sense_grenade_dispatch(cpu,address);
    if(address==0x004C3F10u||address==0x004D6880u||address==0x004C6E80u)sense_frame_audit();
    if(address==0x004C6E80u)sense_haptics_tick();
    if(!host_sense_weapon_rendering()){
        /* An extra left-art invocation must never leak a centre HUD bitmap
         * if tracking/controller ownership changes mid-render. */
        if(address==0x004ACAD0u&&sense_reticle_art_hand==0&&!sense_reticle_art_nested){ret_to_caller(cpu);return 1;}
        return 0;
    }
    if(sense_reticle_pair_dispatch(cpu,address))return 1;
    if(address==0x00494010u&&sense_offhand_claimed&&sense_offhand_cache_owned&&ARG(0)==sense_offhand_weapon&&
       G32(0x006B2D98u)==sense_dual_cache&&sense_range(sense_dual_cache,0x1EA0)&&
       G32(sense_dual_cache+4)==sense_offhand_unit&&G32(sense_dual_cache+8)==sense_offhand_weapon){
        cpu->gpr[0]=0;ret_to_caller(cpu);return 1;
    }
    if(address==0x004C6E80u){
        (void)sense_mount_observe(); /* seat/death/ownership boundary before this tick's input and fire */
        uint32_t datum;if(!sense_local_body(&datum)){
            sense_camera_unit=UINT32_MAX;sense_muzzles[1].valid=0;sense_dual_reset_model();
            halo_sense_set_gameplay_supported(0);halo_sense_reticle_publish(NULL);
        }
        return 0;
    }
    /* Seated: the original crosshair is the native seat aim; never capture it. */
    if(address==0x004ACAD0u&&sense_mount_observe()==HALO_SENSE_MOUNT_SEATED)return 0;
    if(address==0x004ACAD0u&&sense_reticle_capture(cpu))return 1;
    if(sense_dual_render_dispatch(cpu,address))return 1;
    if(address==0x0050BEA0u&&!panorama_nested&&!sense_scope_nested){
        uint32_t datum,unit=sense_local_body(&datum),renderer=ARG(0);
        (void)sense_mount_observe();
        if(unit&&!sense_mount_on_foot(datum))unit=0;
        sense_camera_unit=UINT32_MAX;sense_accepted_frame_begin();
        sense_physical_frame_begin(); /* physical latches for this whole outer epoch, before any admission */
        halo_sense_set_gameplay_supported(unit!=0);
        if(unit&&(ARG(1)&65535u)>0&&sense_range(renderer,0xAC)&&!G8(renderer+2)){
            float pose[9],base[3];memcpy(pose,GPTR(renderer+4),36);memcpy(base,GPTR(unit+0x5C),12);
            if(sense_basis(pose+3,pose+6,&sense_camera)&&sense_finite3(pose)&&sense_finite3(base)){
                for(unsigned k=0;k<3;k++)sense_camera_from_unit[k]=pose[k]-base[k];
                if(sense_dot(sense_camera_from_unit,sense_camera_from_unit)<4.f){sense_camera_unit=datum;sense_camera_ns=host_monotonic_ns();}
            }
        }
        sense_grenade_render_record();
        sense_action_update();
        sense_pickup_outer_expire(unit&&sense_camera_unit==datum?unit:0,datum); /* no progress before admission */
        return 0;
    }
    if(address==0x004D6880u)return sense_update_model(cpu,address);
    /* Original005658F0 writes caller-owned origin/forward and computes the
     * inherited velocity along that ray. Its two flags normally replace them
     * with the body's eye line. Supply tracked values and clear those flags
     * only for this call. Restore the argument bytes even on escaped return.
     * Player aim/position and camera globals are never changed. */
    if(address==0x005658F0u&&sense_fire_nested&&!sense_ray_nested&&G32(cpu->gpr[4])==0x004C4EE2u&&ARG(0)==sense_firing_unit){
        uint32_t origin=cpu->gpr[0],forward=cpu->gpr[3],stack=cpu->gpr[4];
        if(!sense_range(origin,12)||!sense_range(forward,12)||!sense_range(stack,20))return 0;
        uint32_t flags[2]={ARG(2),ARG(3)};
        memcpy(GPTR(origin),sense_firing_ray.p,12);memcpy(GPTR(forward),sense_firing_ray.c[0],12);
        S32(stack+12,0);S32(stack+16,0);
        sense_ray_nested=1;engine_dispatch(cpu,address);sense_ray_nested=0;
        S32(stack+12,flags[0]);S32(stack+16,flags[1]);
        if(HOST_ENV("HALO_SENSE_WEAPON_TRACE")){
            static unsigned logs;if(logs++<128)host_log("[sense-ray] weapon=%08x origin=%.7f,%.7f,%.7f forward=%.7f,%.7f,%.7f returned=%d",sense_firing_weapon,sense_firing_ray.p[0],sense_firing_ray.p[1],sense_firing_ray.p[2],sense_firing_ray.c[0][0],sense_firing_ray.c[0][1],sense_firing_ray.c[0][2],cpu->pc==0x004C4EE2u);
        }
        return 1;
    }
    if(address!=0x004C3F10u||sense_fire_nested)return 0;
    /* Seated: the original seat aims and fires (turret weapon or a seat that
     * keeps the held weapon). A boundary since the tick is observed here. */
    if(sense_mount_observe()==HALO_SENSE_MOUNT_SEATED)return 0;
    uint32_t datum,weapon,unit=sense_local_unit_any(&datum,&weapon);
    unsigned firing_hand=1;
    if(sense_offhand_claimed&&ARG(0)==sense_offhand_weapon){
        if(!unit||datum!=sense_offhand_unit||!sense_offhand_allow){ret_to_caller(cpu);return 1;}
        weapon=sense_offhand_weapon;firing_hand=0;
        uint32_t offhand=sense_object(weapon,0x340,2);
        if(!offhand||G32(offhand+0x11C)!=datum){ret_to_caller(cpu);return 1;}
    }else if(!unit||ARG(0)!=weapon){
        /* During a selected/desired-slot transition a queued local shot must
         * wait for the new tracked model, never fall back to the head ray. */
        uint32_t local_id,local=sense_local_body(&local_id),firing=ARG(0);
        if(local){
            uint32_t body=sense_object(firing,0x340,2);int owned=body&&G32(body+0x11C)==local_id;
            for(unsigned slot=0;slot<4;slot++)if(G32(local+0x2F8+slot*4)==firing)owned=1;
            if(owned){ret_to_caller(cpu);return 1;}
        }
        return 0;
    }
    if(G16(0x00719720u)!=0||G16(unit+0x2F0)!=UINT16_MAX){ret_to_caller(cpu);return 1;}
    int action=sense_action_for_unit(unit,datum,weapon,NULL);
    /* The gun is wherever the original melee/reload/swap animation puts it. */
    if(action==HALO_SENSE_ACTION_MELEE||(firing_hand==1&&sense_action_native(action))){ret_to_caller(cpu);return 1;}
    if(firing_hand==1&&sense_physical_fire_blocked()){ret_to_caller(cpu);return 1;} /* drawn without its magazine */
    if(action==HALO_SENSE_ACTION_SCOPE){
        HaloSenseScopeInput input;HaloSenseTransform weapon_pose;
        if(!firing_hand||!halo_sense_scope_input(&input)||!input.armed||
           !sense_world_weapon(1,unit,datum,weapon,&weapon_pose)||
           !sense_muzzle_clear(cpu,unit,datum,weapon_pose.p)){ret_to_caller(cpu);return 1;}
        /* Scope camera and shot use the same physical gun sight line. */
        sense_firing_ray=weapon_pose;
        sense_firing_unit=datum;sense_firing_weapon=weapon;sense_fire_nested=1;
        HaloSenseDualInput shot_input;
        if(halo_sense_dual_input(&shot_input))sense_shot_begin(cpu,1,datum,weapon,&shot_input);
        engine_dispatch(cpu,address);sense_fire_nested=0;sense_shot_end(cpu);return 1;
    }
    SenseMuzzle *muzzle=&sense_muzzles[firing_hand];
    HaloSenseDualInput firing_input;
    if(!halo_sense_dual_input(&firing_input)||!firing_input.armed[firing_hand]||
       (firing_hand&&halo_sense_dual_requested()&&!host_dual_primary_allowed(datum,weapon))){ret_to_caller(cpu);return 1;}
    HaloSenseTransform hand;
    if(G16(0x00719720u)!=0||G16(unit+0x2F0)!=UINT16_MAX||
       !sense_muzzle_ready(firing_hand,datum,weapon)||!sense_world_weapon(firing_hand,unit,datum,weapon,&hand)){
        ret_to_caller(cpu);return 1; /* Never silently fall back to head aiming. */
    }
    sense_firing_ray=hand;sense_point(&hand,muzzle->muzzle,sense_firing_ray.p);
    if(!sense_muzzle_clear(cpu,unit,datum,sense_firing_ray.p)){ret_to_caller(cpu);return 1;}
    sense_firing_unit=datum;sense_firing_weapon=weapon;sense_fire_nested=1;
    sense_shot_begin(cpu,firing_hand,datum,weapon,&firing_input);
    engine_dispatch(cpu,address);sense_fire_nested=0;sense_shot_end(cpu);return 1;
}
