/* Executes the original lifted render path for the experimental tracked body:
 * 0050E930/0050EB70 object loops -> 0050EBA0 render_object (incl. its shadow
 * gate) -> 0050EE20 render_object_list -> 0050EA50/00445AC0 first-person test
 * -> 004D6FC0 render_model whose own palette loop runs 004CC0D0. Only engine
 * sinks (rasterizer submit, lighting, LOD, shadow begin/end) are stubbed.
 * Synthetic guarded memory; no game files, device, save or input.
 * BODY_TEST_API=1 mocks the weapon module's fixed accepted-frame API; 0 builds
 * the standalone seam. BODY_TEST_PICKUP=1 adds a synthetic pickup helper to
 * prove the shared wrapper's disjoint routing. "--witness" bypasses the body
 * hooks and exits 3 when the body is (correctly) still suppressed. */
#include "../host.h"
#include "engine_flags.h"
#include "engine_hooks.h"
#include "engine_functions.h"
#include <assert.h>
#include <stdarg.h>
#include "engine_registers.h"
#include "sub_004CC0D0.c"
#include "sub_00445AC0.c"
#include "sub_0050EA50.c"
#include "sub_0050EE20.c"
#include "sub_0050EBA0.c"
#include "sub_0050EB70.c"
#include "sub_0050E930.c"
#include "sub_004D6FC0.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#include "../sense_pose.h"
#include "../sense_weapon.h"
#include "../sense_reticle_art.h"
#ifndef BODY_TEST_API
#define BODY_TEST_API 1
#endif
#ifndef BODY_TEST_PICKUP
#define BODY_TEST_PICKUP 1
#endif
uint8_t *engine_flat_base;uint32_t engine_trace_lo,engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu){(void)cpu;}
void host_log(const char *f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a);}
static int active=1,fresh=1,panorama_nested,panorama_fp_cache_nested,app_blocked,pad_mode,dual_mode;
static uint64_t input_generation=1,input_session=1;
bool hostgc_poll(HostGCSnapshot *out){memset(out,0,sizeof *out);if(pad_mode){out->connected=1;out->spatial=pad_mode==2;}return pad_mode!=0;}
static uint64_t now=1000000000;
uint64_t host_monotonic_ns(void){return now;}
int halo_sense_active(void){return active;}
int halo_sense_requested(void){return 1;}
int halo_sense_dual_requested(void){return dual_mode;}
void halo_sense_set_scope_active(int value){(void)value;}
void halo_sense_status(HaloSenseStatus *s){memset(s,0,sizeof *s);s->session=input_session;s->generation=input_generation;s->active=active;s->requested=1;s->gameplay_supported=1;}
int halo_sense_head_read(HaloSenseHeadPose *h){memset(h,0,sizeof *h);h->forward[0]=1;h->up[2]=1;return 1;}
int halo_sense_app_input_blocked(void){return app_blocked;}
int halo_sense_scope_input(HaloSenseScopeInput *i){memset(i,0,sizeof *i);return 0;}
int halo_sense_dual_input(HaloSenseDualInput *p){memset(p,0,sizeof *p);p->session=input_session;p->generation=input_generation;p->fresh=fresh;return !app_blocked&&fresh;}
int host_sense_reticle_art_begin(unsigned h,uint32_t w,const HaloSenseReticleFrame*t,float x,float y){(void)h;(void)w;(void)t;(void)x;(void)y;return 0;}
int host_sense_reticle_art_end(int complete){(void)complete;return 1;}
void halo_sense_reticle_publish(const HaloSenseReticleFrame *p){(void)p;}
int halo_sense_reticle_read(HaloSenseReticleFrame *p){memset(p,0,sizeof *p);return 0;}
void halo_sense_observe_controller(const HostGCSnapshot *p){(void)p;}
void halo_sense_set_gameplay_supported(int s){(void)s;}
void halo_sense_set_native_controls(int n,uint64_t g){(void)n;(void)g;}
int halo_sense_gameplay_supported(void){return 1;}
int halo_sense_weapon_pose(unsigned hand,float p[3],float f[3],float u[3]){
    if(!fresh)return 0;float pos[3]={.5f,hand==1?-.25f:.25f,-.3f},dir[3]={1,0,0},up[3]={0,0,1};
    memcpy(p,pos,12);memcpy(f,dir,12);memcpy(u,up,12);return 1;
}
int halo_sense_read(HaloSenseFrame *frame){memset(frame,0,sizeof *frame);if(!fresh||app_blocked)return 0;
    frame->generation=frame->controller_generation=1;
    frame->left.valid=halo_sense_weapon_pose(0,frame->left.position,frame->left.forward,frame->left.up);
    frame->right.valid=halo_sense_weapon_pose(1,frame->right.position,frame->right.forward,frame->right.up);return 1;
}
int halo_sense_grenade_accept(uint32_t c,uint32_t t,HaloSenseGrenadeRelease *out){(void)c;(void)t;if(out)memset(out,0,sizeof *out);return 0;}
int halo_sense_grenade_hold(HaloSenseGrenadeHold *out){if(out)memset(out,0,sizeof *out);return 0;} /* no armed hold in this fixture */
static uint32_t heap=0x70000;
uint32_t guest_alloc(uint32_t size){if(size==0x100)return 0x60000;uint32_t p=heap;heap+=(size+0xFFFu)&~0xFFFu;memset(GPTR(p),0,size);return p;}
uint32_t host_call_guest(EngineCPU *cpu,uint32_t fn,int n,const uint32_t *args,int pops){(void)cpu;(void)fn;(void)n;(void)args;(void)pops;return 0;}
static void ret_to_caller(EngineCPU *cpu){cpu->pc=engine_pop(cpu,4);}

/* Merged weapon-lane boundaries this body fixture never drives: the haptics
 * main-loop tick (004C6E80) and primary-frame seeding (viewmodel/fire/
 * reticle/scope). The body only reads the accepted frame, so these must not run. */
uint64_t halo_sense_set_weapon_haptic_owner(int owned,uint64_t session,uint64_t generation){
    (void)owned;(void)session;(void)generation;assert(!"weapon haptics tick is unexercised by the body fixture");return 0;
}
int halo_sense_grip_continuity(void){assert(!"primary-frame seeding is unexercised by the body fixture");return 0;}
int halo_sense_scope_smoothing(void){assert(!"primary-frame seeding is unexercised by the body fixture");return 0;}
/* Weapon shot/haptic feedback (sense_weapon_feedback.inc): fire and tick only. */
#include "../haptics.h"
#include "../halo_settings.h"
#define BODY_UNEXERCISED_FEEDBACK assert(!"weapon shot/haptic feedback is unexercised by the body fixture")
int halo_sense_weapon_haptics(void){BODY_UNEXERCISED_FEEDBACK;return 0;}
int halo_haptics_enabled(void){BODY_UNEXERCISED_FEEDBACK;return 0;}
float halo_settings_haptics_strength(void){BODY_UNEXERCISED_FEEDBACK;return 0;}
void halo_haptics_weapon_owner_renew(void){BODY_UNEXERCISED_FEEDBACK;}
void halo_haptics_weapon_owner_release(void){BODY_UNEXERCISED_FEEDBACK;}
void halo_haptics_weapon_shot_begin(void){BODY_UNEXERCISED_FEEDBACK;}
void halo_haptics_weapon_shot_end(int accepted){(void)accepted;BODY_UNEXERCISED_FEEDBACK;}
int hostgc_play_spatial_haptic(const HostGCSpatialHaptic *event){(void)event;BODY_UNEXERCISED_FEEDBACK;return 0;}
void hostgc_cancel_spatial_haptics(int kind){(void)kind;BODY_UNEXERCISED_FEEDBACK;}
uint64_t hostgc_source_generation(void){BODY_UNEXERCISED_FEEDBACK;return 0;}
#if __has_include("../sense_physical.h")
/* Physical lane transport, default off in this fixture: none of it may run. */
#include "../sense_physical.h"
int halo_sense_physical_enqueue(uint64_t session,uint64_t generation,uint64_t sequence,int action,uint32_t owner,uint32_t target){
    (void)session;(void)generation;(void)sequence;(void)action;(void)owner;(void)target;
    assert(!"physical transport is unexercised: physical options are off");return 0;
}
int halo_sense_physical_pending(int action){(void)action;return 0;}
void halo_sense_physical_cancel(int action){(void)action;}
void halo_sense_physical_set_delivery_check(int (*check)(int,uint32_t,uint32_t)){(void)check;}
#endif
/* The merged weapon module defines the real accepted-frame cache under the
 * same names the body consumes. Import it namespaced, plus any imported real
 * pickup helper, so the body sees this fixture's mocked API (or, for
 * BODY_TEST_API=0, its own standalone seam) and the synthetic pickup lane. */
#define SenseAcceptedWeaponFrame NativeSenseAcceptedWeaponFrame
#define sense_accepted_weapon_frame native_sense_accepted_weapon_frame
#define sense_accepted_frame_epoch native_sense_accepted_frame_epoch
#define sense_pickup_model_prepare native_sense_pickup_model_prepare
#include "../sense_weapon_hooks.inc"
#include "../dual_wield.inc"
#undef SenseAcceptedWeaponFrame
#undef sense_accepted_weapon_frame
#undef sense_accepted_frame_epoch
#undef SENSE_ACCEPTED_WEAPON_FRAME_DEFINED
#if BODY_TEST_API
/* The weapon module's fixed read-only API (cache-api-ready.json), mocked. */
#define SENSE_ACCEPTED_WEAPON_FRAME_DEFINED 1
typedef struct {
    uint64_t epoch, epoch_ns, session, generation;
    uint32_t unit, weapon;
    int supported;
    HaloSenseTransform primary, left, visual_left;
} SenseAcceptedWeaponFrame;
static uint64_t mock_epoch;static int mock_accept,mock_admitted,mock_drift;static unsigned mock_reads;
static SenseAcceptedWeaponFrame mock_frame;
static uint64_t sense_accepted_frame_epoch(void){return mock_epoch;}
static int sense_accepted_weapon_frame(uint32_t datum,uint32_t weapon,SenseAcceptedWeaponFrame *out){
    mock_reads++;
    if(mock_admitted&&mock_frame.epoch==mock_epoch&&mock_frame.unit==datum&&mock_frame.weapon==weapon&&
       mock_frame.session==input_session&&mock_frame.generation==input_generation){
        *out=mock_frame;if(mock_drift){out->primary.p[0]+=.01f*(float)mock_reads;} /* a misbehaving provider */
        return 1;
    }
    memset(out,0,sizeof *out);return 0;
}
#endif
#include "../sense_body_hooks.inc"
#undef sense_pickup_model_prepare
#undef HALO_SENSE_HAVE_PICKUP_MODEL_PREPARE
#if BODY_TEST_PICKUP
/* Synthetic second lane: one salted ground weapon, scratch pointers only. */
static int pickup_enabled;static unsigned pickup_calls,pickup_body_calls;
#define HALO_SENSE_HAVE_PICKUP_MODEL_PREPARE 1
static int sense_pickup_model_prepare(EngineCPU *cpu,SenseWorldModelOverride *out){
    pickup_calls++;if(ARG(8)==0x23450000u)pickup_body_calls++;
    if(!pickup_enabled||ARG(8)!=0x89AB0005u)return SENSE_WORLD_MODEL_NONE;
    out->palette=0x7E000;out->center=0x7E800;return SENSE_WORLD_MODEL_APPLY;
}
#endif
#include "../sense_world_model_override.inc"

#define UNIT 0x23450000u
#define UNIT_BODY 0x15000u
#define UNIT_NODES 0x15600u
#define MARINE 0x67890003u
#define GROUND 0x89AB0005u
#define NODE_COUNT 16u
static const char *node_names[NODE_COUNT]={"bip01 pelvis","bip01 spine","bip01 spine1","bip01 neck","bip01 head",
    "bip01 l clavicle","bip01 l upperarm","bip01 l forearm","bip01 l hand","bip01 l finger0",
    "bip01 r clavicle","bip01 r upperarm","bip01 r forearm","bip01 r hand","bip01 l thigh","bip01 r thigh"};
static const int node_parent[NODE_COUNT]={-1,0,1,2,3,3,5,6,7,8,3,10,11,12,0,0};
static const float node_pos[NODE_COUNT][3]={{10,20,30.30f},{10,20,30.40f},{10,20,30.48f},{10,20,30.55f},{10,20,30.58f},
    {10,20.03f,30.53f},{10,20.06f,30.53f},{10,20.07f,30.43f},{10.08f,20.07f,30.40f},{10.10f,20.07f,30.40f},
    {10,19.97f,30.53f},{10,19.94f,30.53f},{10,19.93f,30.43f},{10.08f,19.93f,30.40f},{10,20.03f,30.28f},{10,19.97f,30.28f}};
enum { HEAD=4,L_UPPER=6,L_FORE=7,L_HAND=8,L_FINGER=9,R_UPPER=11,R_FORE=12,R_HAND=13,L_THIGH=14 };

/* Records: every original render_model entry and every rasterizer submit. */
typedef struct { uint32_t object,flags,count,ecx,center,stack; float m[64][13]; } Draw;
static Draw draws[64];static unsigned ndraws,originals,fp_draws,body_active_calls,body_active_true;
static uint32_t entry_ecx[64],entry_center[64],entry_object[64],entry_stack[64];
static int bypass_body,escape_submit,escape_predicate,mutate_during_lod,call_body_active,body_active_first;
static unsigned lod_calls,wrapped;static uint32_t mock_weapon;static int window_mode,freeze_epoch,between_passes,restore_fresh_before_shadow;
static float right_target[3],left_target[3];
/* The unit's second LOD query is 0050EE20's, after its 0050EA50 admission and
 * before 004D6FC0: a change there exercises the submission-time checks. */
void sub_0050F740(EngineCPU *cpu){
    if(++lod_calls==2&&mutate_during_lod>0){
        if(mutate_during_lod==1)S16(UNIT_BODY+0x1F2,0x640);      /* node array moved under the list */
        if(mutate_during_lod==2)input_generation++;              /* lease changed mid-epoch */
        if(mutate_during_lod==3)fresh=0;                         /* tracking lost before submission */
        mutate_during_lod=-mutate_during_lod;
    }
    engine_fp_push(cpu,500.0);ret_to_caller(cpu);
}
void sub_0050EA00(EngineCPU *cpu){cpu->gpr[0]=0x5A000;ret_to_caller(cpu);}
void sub_0050F830(EngineCPU *cpu){cpu->gpr[0]=1;ret_to_caller(cpu);}
void sub_0050F980(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004FFC60(EngineCPU *cpu){cpu->gpr[0]=0;ret_to_caller(cpu);}
void sub_004F4410(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004FFCA0(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_00518B40(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_00526F50(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_0052B530(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_0050EAC0(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004924B0(EngineCPU *cpu){fp_draws++;ret_to_caller(cpu);}
/* Rasterizer sink: [esp+4] model data, +0Ch &context (ebp-0D4h: palette
 * pointer; ebp-0D8h: object, stored at 004D712F), +18h flags. */
void sub_004D72A0(EngineCPU *cpu){
    assert(ndraws<64);Draw *d=&draws[ndraws++];uint32_t model=ARG(0),palette=G32(ARG(2));
    d->object=G32(ARG(2)-4);d->flags=ARG(5);d->count=G32(model+0xB8);assert(d->count<=64);
    assert(originals&&d->object==entry_object[originals-1]); /* the submitting 004D6FC0 entry's own object */
    memcpy(d->m,GPTR(palette),d->count*52u);
    if(escape_submit){escape_submit=0;cpu->pc=0xDEAD;return;}
    ret_to_caller(cpu);
}
static void fp_rig(unsigned pass);
static void render_world(unsigned passes);
void engine_dispatch(EngineCPU *cpu,uint32_t address){
    cpu->pc=address;
    if(host_sense_weapon_dispatch(cpu,address))return;
    if(!bypass_body){
        if(host_sense_body_dispatch(cpu,address))return;
        /* The wrapper's temporary inputs are back the moment it returns,
         * on normal, dropped and escaped paths alike. */
        uint32_t sp=cpu->gpr[4],slot=G32(sp+0x18),nodes=cpu->gpr[1];
        if(host_sense_world_model_dispatch(cpu,address)){
            assert(G32(sp+0x18)==slot&&cpu->gpr[1]==nodes&&!sense_world_model_nested);wrapped++;return;
        }
    }
    switch(address){
    case 0x004CC0D0u:sub_004CC0D0(cpu);return;
    case 0x0050E930u:sub_0050E930(cpu);return;
    case 0x0050EB70u:sub_0050EB70(cpu);return;
    case 0x004924B0u:sub_004924B0(cpu);return; /* first-person weapon draw (panorama hook address) */
    case 0x0050F740u:sub_0050F740(cpu);return;
    case 0x0050EA50u:if(escape_predicate){escape_predicate=0;cpu->pc=0xDEAD;return;}sub_0050EA50(cpu);return;
    case 0x004D6FC0u:
        assert(originals<64);entry_ecx[originals]=cpu->gpr[1];entry_center[originals]=G32(cpu->gpr[4]+0x18);
        entry_object[originals]=G32(cpu->gpr[4]+0x24);entry_stack[originals]=cpu->gpr[4];originals++;
        sub_004D6FC0(cpu);return;
    default:fprintf(stderr,"unexpected fixture call %08x\n",address);assert(0);
    }
}
static uint32_t call_fn(uint32_t fn,uint32_t eax,uint32_t edi){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x1F0000;c.gpr[0]=eax;c.gpr[7]=edi;
    engine_push(&c,0xDEADC0DEu,4);engine_dispatch(&c,fn);return c.pc;
}
static void putf(uint32_t p,float f){memcpy(GPTR(p),&f,4);}
static void near(float a,float b){if(!(fabsf(a-b)<.0002f)){fprintf(stderr,"near %f %f\n",a,b);assert(0);}}
static void identity_node(uint32_t m,const float p[3]){putf(m,1);putf(m+4,1);putf(m+20,1);putf(m+36,1);memcpy(GPTR(m+40),p,12);}
static void object(unsigned index,uint32_t datum,uint32_t body,unsigned size,unsigned type,uint32_t definition,
                   uint32_t parent,uint32_t child,uint32_t sibling,unsigned offset,unsigned nodes,float x) {
    uint32_t e=0x14000+index*12u;S16(e,(uint16_t)(datum>>16));S16(e+6,(uint16_t)size);S32(e+8,body);
    S32(body,definition);S32(body+0x10,0);S16(body+0xB4,(uint16_t)type);S32(body+0x114,sibling);S32(body+0x118,child);
    S32(body+0x11C,parent);S32(body+0x16C,UINT32_MAX);S16(body+0x1F2,(uint16_t)offset);putf(body+0xAC,.5f);
    float c[3]={x,20,30.3f};memcpy(GPTR(body+0xA0),c,12);
    for(unsigned i=0;i<nodes;i++){float p[3]={node_pos[i][0]+x-10,node_pos[i][1],node_pos[i][2]};identity_node(body+offset+i*52u,p);}
}
static void tag(unsigned index,uint32_t datum,uint32_t data){S32(0x30000+index*32u+0xC,datum);S32(0x30000+index*32u+0x14,data);}
static void model_tag(uint32_t data,uint32_t meta,unsigned count,int biped){
    S32(data+0xB8,count);S32(data+0xBC,meta);
    for(unsigned i=0;i<count;i++){
        uint32_t row=meta+i*0x9Cu;float zero[3]={0,0,0};
        if(biped){memcpy(GPTR(row),node_names[i],strlen(node_names[i])+1);S16(row+0x24,(uint16_t)(int16_t)node_parent[i]);}
        else S16(row+0x24,0xFFFF);
        identity_node(row+0x68,zero);
    }
}
static void setup(void){
    memset(engine_flat_base,0,0x900000);
    active=fresh=1;app_blocked=pad_mode=0;input_generation=input_session=1;panorama_nested=0;sense_scope_nested=0;
    S32(0x696664,0x004CC0D0);
    putf(0x672AC0,0);putf(0x672AC4,1);putf(0x672AC8,10);putf(0x673248,.2f);putf(0x673018,.01f);putf(0x673244,1);
    putf(0x672C88,.3f);putf(0x672C8C,.59f);putf(0x672C90,.11f);putf(0x672C94,.3f);S16(0x6893E8,0xFFFF);
    for(unsigned k=0;k<3;k++)putf(0x5A068+4*k,.1f);
    /* Players, director first person (00445AC0), render local player 0. */
    S32(0x87A478,0x10000);S32(0x87A480,0x11000);S32(0x10004,0x12340000);
    S16(0x11020,1);S16(0x11022,0x200);S32(0x11034,0x12000);S16(0x12000,0x1234);S32(0x12034,UNIT);
    S16(0x7C3108,0);S32(0x6AC560+8,0x00446D60);putf(0x6AC560+4,0);
    /* Objects: unit + carried weapon/grenade, marine + its weapon, ground weapon. */
    S32(0x8603B0,0x13000);S16(0x13020,6);S16(0x13022,12);S32(0x13034,0x14000);
    S32(0x87BC14,0x30000);
    tag(1,0x45670001,0x33000);tag(3,0x45670003,0x3A000);tag(4,0x45670004,0x3B000);tag(5,0x45670005,0x3D000);
    tag(6,0x45670006,0x3F000);tag(7,0x45670007,0x3E000); /* the marine's own copies */
    S32(0x33034,0x45670005);S32(0x3309C,UINT32_MAX);S32(0x3A034,0x45670004);S32(0x3A09C,UINT32_MAX);
    S32(0x3E034,0x45670006);S32(0x3E09C,UINT32_MAX);
    model_tag(0x3B000,0x3C000,NODE_COUNT,1);model_tag(0x3D000,0x3D100,1,0);model_tag(0x3F000,0x3F100,NODE_COUNT,1);
    object(0,UNIT,UNIT_BODY,0x1000,0,0x45670003,UINT32_MAX,0x34560001,UINT32_MAX,0x600,NODE_COUNT,10);
    object(1,0x34560001,0x16000,0x340,2,0x45670001,UNIT,UINT32_MAX,0x56780002,0x380,1,10);
    object(2,0x56780002,0x18000,0x400,2,0x45670001,UNIT,UINT32_MAX,UINT32_MAX,0x380,1,10);
    object(3,MARINE,0x19000,0x1000,0,0x45670007,UINT32_MAX,0x789A0004,UINT32_MAX,0x600,NODE_COUNT,12);
    object(4,0x789A0004,0x1B000,0x400,2,0x45670001,MARINE,UINT32_MAX,UINT32_MAX,0x380,1,12);
    object(5,GROUND,0x1C000,0x400,2,0x45670001,UINT32_MAX,UINT32_MAX,UINT32_MAX,0x380,1,14);
    putf(UNIT_BODY+0xE0,1);float origin[3]={10,20,30};memcpy(GPTR(UNIT_BODY+0x5C),origin,12);
    S16(UNIT_BODY+0x2F0,0xFFFF);S16(UNIT_BODY+0x2F2,0);S16(UNIT_BODY+0x2F4,0);S32(UNIT_BODY+0x2F8,0x34560001);
    S8(UNIT_BODY+0x320,0xFF);S8(UNIT_BODY+0x321,0xFF);S32(0x1611C,UNIT);
    /* Rendered list (0050E930/0050EB70): unit, marine, ground weapon. */
    S16(0x6B8DC0,3);S32(0x6B8DC4,UNIT);S32(0x6B8DC8,MARINE);S32(0x6B8DCC,GROUND);
    /* Outer render camera at the eye, facing +X (renderer +4 position/forward/up). */
    float cam[9]={10,20,30.6f,1,0,0,0,0,1};memcpy(GPTR(0x50004),cam,36);
    /* Synthetic pickup scratch: its one node lifted 0.5, its own centre. */
    float lifted[3]={14,20,31},centre[3]={14,20,31};identity_node(0x7E000,lifted);memcpy(GPTR(0x7E800),centre,12);
    float r[3]={10+.5f/3.048f,20-.25f/3.048f,30.6f-.3f/3.048f},l[3]={10+.5f/3.048f,20+.25f/3.048f,30.6f-.3f/3.048f};
    memcpy(right_target,r,12);memcpy(left_target,l,12);
    halo_sense_set_body_enabled(1);call_body_active=0;mutate_during_lod=0;mock_weapon=0;window_mode=0;dual_mode=0;between_passes=0;restore_fresh_before_shadow=0;
#if BODY_TEST_API
    mock_accept=1;mock_drift=0;
#endif
#if BODY_TEST_PICKUP
    pickup_enabled=0;pickup_calls=pickup_body_calls=0;
#endif
}
/* Original order: 00492430 first-person rig (weapon admission, then any
 * connected-arm postpass) precedes 0050E930 world objects, then shadows. */
static void fp_rig(unsigned pass){
#if BODY_TEST_API
    if(call_body_active&&body_active_first&&pass==0){body_active_calls++;body_active_true+=host_sense_body_active();}
    if(pass==0&&mock_accept){
        HaloSenseTransform r={0},l={0};float f[3]={1,0,0},u[3]={0,0,1};sense_basis(f,u,&r);l=r;
        memcpy(r.p,right_target,12);memcpy(l.p,left_target,12);
        mock_frame=(SenseAcceptedWeaponFrame){.epoch=mock_epoch,.epoch_ns=now,.session=input_session,.generation=input_generation,
            .unit=UNIT,.weapon=mock_weapon?mock_weapon:G32(UNIT_BODY+0x2F8+4u*G16(UNIT_BODY+0x2F2)),.primary=r,.left=l,.visual_left=l};
        mock_admitted=1;
    }
#endif
    if(call_body_active&&!body_active_first){body_active_calls++;body_active_true+=host_sense_body_active();}
    (void)pass;
}
static void render_world(unsigned passes){
    for(unsigned pass=0;pass<passes;pass++){
        if(pass==1){
#if BODY_TEST_API
            if(between_passes==1)mock_admitted=0;                  /* accepted frame went stale */
#endif
            if(between_passes==2)fresh=0;                          /* raw tracking lost */
            if(between_passes==3)now+=UINT64_C(200000000);         /* render stalled between eyes */
            if(between_passes==4)fresh=0;                          /* transient failures, restored at pass 2 */
            if(between_passes==5)input_generation++;
            if(between_passes==6)app_blocked=1;
        }
        if(pass==2){
            if(between_passes==4)fresh=1;
            if(between_passes==5)input_generation--;
            if(between_passes==6)app_blocked=0;
        }
        fp_rig(pass);
        call_fn(0x0050E930u,0,0);
        if(restore_fresh_before_shadow)fresh=1;
        uint32_t data=0x5C000;memset(GPTR(data),0,16);S8(data+8,1);S8(data+9,1);
        call_fn(0x0050EB70u,data,0);
    }
}
/* One outer 0050BEA0: the epoch boundary, camera sample, then the passes. */
static void frame(unsigned passes){
    ndraws=originals=fp_draws=0;
#if BODY_TEST_API
    if(!freeze_epoch){mock_epoch++;mock_admitted=0;}mock_reads=0;
#endif
    lod_calls=0;
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,0x50000);S32(0x20008,1);
    c.pc=0x0050BEA0u;
    if(!host_sense_weapon_dispatch(&c,0x0050BEA0u)&&(bypass_body||!host_sense_body_dispatch(&c,0x0050BEA0u))){
        panorama_nested=!window_mode;render_world(passes);panorama_nested=0;
    }
}
static unsigned draws_of(uint32_t object,int shadow){
    unsigned n=0;for(unsigned i=0;i<ndraws;i++)if(draws[i].object==object&&((draws[i].flags&2u)!=0)==shadow)n++;return n;
}
static const Draw *draw_of(uint32_t object,int shadow,unsigned nth){
    for(unsigned i=0;i<ndraws;i++)if(draws[i].object==object&&((draws[i].flags&2u)!=0)==shadow&&!nth--)return &draws[i];
    return NULL;
}
static int dump_draws;
static void dump(void){
    fprintf(stderr,"originals=%u draws=%u fp=%u\n",originals,ndraws,fp_draws);
    for(unsigned i=0;i<ndraws;i++)fprintf(stderr,"  draw %08x flags=%x count=%u\n",draws[i].object,draws[i].flags,draws[i].count);
    for(unsigned i=0;i<originals;i++)fprintf(stderr,"  original %08x ecx=%08x\n",entry_object[i],entry_ecx[i]);
}
static void expect_original(void){
    if(dump_draws)dump();
    /* The stock result: no local body, nothing it carries, others intact. */
    assert(draws_of(UNIT,0)==0&&draws_of(UNIT,1)==0&&draws_of(0x34560001,0)==0&&draws_of(0x56780002,0)==0);
    assert(draws_of(MARINE,0)==1&&draws_of(0x789A0004,0)==1&&draws_of(GROUND,0)==1);
    assert(draws_of(MARINE,1)==1&&draws_of(0x789A0004,1)==1&&draws_of(GROUND,1)==1);
}
static float dist(const float *a,const float *b){float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return sqrtf(sense_dot(d,d));}
static unsigned char unit_before[0x1000],others_before[0x7000]; /* every object body; 0x20000 is fixture stack */
static void snapshot_world(void){memcpy(unit_before,GPTR(UNIT_BODY),sizeof unit_before);memcpy(others_before,GPTR(0x16000),sizeof others_before);}
static void expect_world_unchanged(void){assert(!memcmp(unit_before,GPTR(UNIT_BODY),sizeof unit_before));assert(!memcmp(others_before,GPTR(0x16000),sizeof others_before));}

int main(int argc,char **argv){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);
    setup();dump_draws=argc>1&&!strcmp(argv[1],"--dump");
    if(argc>1&&!strcmp(argv[1],"--witness")){
        /* Same enabled scenario with the body hooks bypassed: original code
         * must suppress the body; the receipt records this expected failure. */
        bypass_body=1;frame(1);expect_original();
        int drawn=draws_of(UNIT,0)>0;printf("WITNESS: body drawn without hooks=%d\n",drawn);free(engine_flat_base);return drawn?0:3;
    }
    /* Stock behaviour with the setting off: actual 0050EA50 returns 1 for the
     * local first-person unit and 0050EE20/0050EBA0 skip it and its weapon. */
    halo_sense_set_body_enabled(0);snapshot_world();frame(1);expect_original();assert(fp_draws==1);expect_world_unchanged();
    assert(originals==6);
    /* Unknown/non-render caller of 0050EA50 is untouched. */
    {EngineCPU q={0};q.gpr[4]=0x21000;S32(0x21000,0x00401234);q.gpr[6]=UNIT;halo_sense_set_body_enabled(1);
     assert(!host_sense_body_dispatch(&q,0x0050EA50u));}

    /* Enabled, valid: one body per pass, eye palette and full shadow palette. */
    setup();snapshot_world();frame(1);
    assert(draws_of(UNIT,0)==1&&draws_of(UNIT,1)==1);
    assert(draws_of(0x34560001,0)==0&&draws_of(0x56780002,0)==0&&draws_of(0x34560001,1)==0&&draws_of(0x56780002,1)==0);
    assert(draws_of(MARINE,0)==1&&draws_of(0x789A0004,0)==1&&draws_of(GROUND,0)==1&&draws_of(MARINE,1)==1&&draws_of(0x789A0004,1)==1);
    assert(originals==8&&fp_draws==1);expect_world_unchanged();
    for(unsigned i=0;i<originals;i++){
        if(entry_object[i]==UNIT)assert(entry_ecx[i]==sense_body_eye||entry_ecx[i]==sense_body_shadow);
        else assert(entry_ecx[i]==G32(0x14000+(entry_object[i]&65535u)*12u+8)+0x380u||entry_ecx[i]==G32(0x14000+(entry_object[i]&65535u)*12u+8)+0x600u);
        assert(entry_center[i]==G32(0x14000+(entry_object[i]&65535u)*12u+8)+0xA0u); /* body never moves its centre */
    }
    const Draw *eye=draw_of(UNIT,0,0),*shadow=draw_of(UNIT,1,0);assert(eye&&shadow&&eye->count==NODE_COUNT);
    float original[NODE_COUNT][13];memcpy(original,GPTR(UNIT_NODES),sizeof original);
    /* Head hidden in the eye palette; full head in the shadow. */
    near(eye->m[HEAD][0],0);for(unsigned k=0;k<3;k++)near(eye->m[HEAD][10+k],original[HEAD][10+k]);
    assert(!memcmp(shadow->m[HEAD],original[HEAD],52));
    /* Torso and legs keep the original animation. */
    for(unsigned i=0;i<4;i++){assert(!memcmp(eye->m[i],original[i],52));assert(!memcmp(shadow->m[i],original[i],52));}
    assert(!memcmp(eye->m[L_THIGH],original[L_THIGH],52)&&!memcmp(shadow->m[15],original[15],52));
#if BODY_TEST_API
    /* Arms reach the accepted first-person wrists with their own lengths. */
    assert(sense_body.arms&&sense_body.state==HALO_SENSE_BODY_ACTIVE);
    const int up[2]={L_UPPER,R_UPPER},fo[2]={L_FORE,R_FORE},ha[2]={L_HAND,R_HAND};const float *target[2]={left_target,right_target};
    for(unsigned s=0;s<2;s++){
        float l1=dist(original[fo[s]]+10,original[up[s]]+10),l2=dist(original[ha[s]]+10,original[fo[s]]+10);
        near(dist(shadow->m[fo[s]]+10,shadow->m[up[s]]+10),l1);near(dist(shadow->m[ha[s]]+10,shadow->m[fo[s]]+10),l2);
        for(unsigned k=0;k<3;k++){near(shadow->m[ha[s]][10+k],target[s][k]);near(shadow->m[up[s]][10+k],original[up[s]][10+k]);}
        assert(shadow->m[fo[s]][12]<shadow->m[up[s]][12]); /* elbow below the shoulder */
        near(shadow->m[ha[s]][0],1);near(eye->m[ha[s]][0],0);  /* FP hands own the eye view */
        for(unsigned k=0;k<3;k++)near(eye->m[ha[s]][10+k],target[s][k]);
        assert(!memcmp(eye->m[up[s]],shadow->m[up[s]],52)&&!memcmp(eye->m[fo[s]],shadow->m[fo[s]],52));
        for(unsigned c=0;c<3;c++)near(sense_dot(shadow->m[fo[s]]+1+3*c,shadow->m[fo[s]]+1+3*c),1);
    }
    near(eye->m[L_FINGER][0],0);near(shadow->m[L_FINGER][0],1);
    near(dist(shadow->m[L_FINGER]+10,shadow->m[L_HAND]+10),dist(original[L_FINGER]+10,original[L_HAND]+10));
#else
    assert(!sense_body.arms&&sense_body.state==HALO_SENSE_BODY_ARMS_WAITING);
#endif
    for(unsigned i=0;i<NODE_COUNT;i++)for(unsigned k=0;k<13;k++)assert(isfinite(eye->m[i][k])&&isfinite(shadow->m[i][k]));

    /* Every eye/bearing of one epoch: identical palettes and admission, even
     * if the provider drifted or the controllers moved after the latch. */
    setup();
#if BODY_TEST_API
    mock_drift=1;
#endif
    frame(3);assert(draws_of(UNIT,0)==3&&draws_of(UNIT,1)==3);
    for(unsigned n=1;n<3;n++){assert(!memcmp(draw_of(UNIT,0,n)->m,draw_of(UNIT,0,0)->m,NODE_COUNT*52u));
                              assert(!memcmp(draw_of(UNIT,1,n)->m,draw_of(UNIT,1,0)->m,NODE_COUNT*52u));}
#if BODY_TEST_API
    mock_drift=0;
    /* A later epoch takes the new accepted pose. */
    right_target[0]-=.02f;frame(1);near(draw_of(UNIT,1,0)->m[R_HAND][10],right_target[0]);
#endif
#if BODY_TEST_API
    /* First frame / missing accepted pose: admitted, arms hidden at the
     * shoulder in the eye view, shadow keeps the original rig, sticky. */
    setup();mock_accept=0;call_body_active=1;body_active_first=0;body_active_calls=body_active_true=0;frame(2);
    assert(draws_of(UNIT,0)==2&&!sense_body.arms&&body_active_calls==2&&body_active_true==0);
    eye=draw_of(UNIT,0,1);shadow=draw_of(UNIT,1,1);
    for(unsigned i=0;i<NODE_COUNT;i++)if(i!=HEAD&&!(i>=L_UPPER&&i<=L_FINGER)&&!(i>=R_UPPER&&i<=R_HAND))assert(!memcmp(eye->m[i],original[i],52));
    for(unsigned i=L_UPPER;i<=L_FINGER;i++){near(eye->m[i][0],0);for(unsigned k=0;k<3;k++)near(eye->m[i][10+k],original[L_UPPER][10+k]);}
    assert(!memcmp(shadow->m,original,sizeof original));
    /* An arm postpass asked before this epoch's admission latches the absent
     * outcome for every eye; the next epoch admits. */
    mock_accept=1;body_active_first=1;body_active_calls=body_active_true=0;frame(2);
    assert(body_active_calls==1&&body_active_true==0&&!sense_body.arms&&draws_of(UNIT,0)==2&&mock_reads==1);
    assert(!memcmp(draw_of(UNIT,0,0)->m,draw_of(UNIT,0,1)->m,NODE_COUNT*52u));
    body_active_first=0;body_active_calls=body_active_true=0;frame(2);
    assert(body_active_calls==2&&body_active_true==2&&sense_body.arms);
    call_body_active=0;
    /* Weapon transition: the accepted frame belongs to another weapon. */
    setup();S32(UNIT_BODY+0x2FC,0x56780002);S16(UNIT_BODY+0x2F2,1);S16(UNIT_BODY+0x2F4,1);
    mock_weapon=0x34560001;frame(1);assert(draws_of(UNIT,0)==1&&!sense_body.arms&&sense_body.state==HALO_SENSE_BODY_ARMS_WAITING);
    mock_weapon=0;frame(1);assert(draws_of(UNIT,0)==1&&sense_body.arms);
    /* After a successful latch, a stale accepted frame, lost raw tracking or
     * a stalled render fails closed for the rest of the epoch (never a
     * recomputed pose); the arm postpass is no longer suppressed. */
    for(int stale=1;stale<=3;stale++){
        setup();between_passes=stale;call_body_active=1;body_active_first=0;body_active_calls=body_active_true=0;frame(3);
        assert(draws_of(UNIT,0)==1&&draws_of(UNIT,1)==1&&body_active_calls==3&&body_active_true==1);
        assert(draws_of(MARINE,0)==3&&draws_of(0x34560001,0)==0);
    }
    setup();mutate_during_lod=3;frame(1);assert(draws_of(UNIT,0)==0&&draws_of(UNIT,1)==0&&mutate_during_lod<0);
    /* Sticky body invalidation: a transient raw-tracking, lease-generation or
     * app-menu failure restored later in the SAME epoch never resurrects the
     * cached body or arm suppression; the next epoch recovers. */
    for(int transient=4;transient<=6;transient++){
        setup();between_passes=transient;call_body_active=1;body_active_first=0;body_active_calls=body_active_true=0;
        snapshot_world();frame(3);
        assert(draws_of(UNIT,0)==1&&draws_of(UNIT,1)==1&&body_active_calls==3&&body_active_true==1);
        assert(draws_of(MARINE,0)==3&&draws_of(0x34560001,0)==0&&sense_body.invalidated);
        HaloSenseBodyStatus sticky;host_sense_body_status(&sticky);assert(sticky.state==HALO_SENSE_BODY_UNAVAILABLE);
        expect_world_unchanged();
        between_passes=0;body_active_calls=body_active_true=0;frame(2);
        assert(draws_of(UNIT,0)==2&&body_active_true==2&&!sense_body.invalidated);
    }
    call_body_active=0;
    /* Submission-time loss (safe DROP) restored before the shadow pass: the
     * shadow stays closed this epoch; the next epoch draws both. */
    setup();mutate_during_lod=3;restore_fresh_before_shadow=1;frame(1);
    assert(draws_of(UNIT,0)==0&&draws_of(UNIT,1)==0&&fresh&&sense_body.invalidated);
    restore_fresh_before_shadow=0;frame(1);assert(draws_of(UNIT,0)==1&&draws_of(UNIT,1)==1);
    /* Dual wielding mode: body and legs, arms always left to the first-person
     * hands (eye arms hidden, authored shadow arms), identical every pass. */
    setup();dual_mode=1;call_body_active=1;body_active_first=0;body_active_calls=body_active_true=0;frame(2);
    assert(draws_of(UNIT,0)==2&&!sense_body.arms&&body_active_true==0&&sense_body.state==HALO_SENSE_BODY_ARMS_WAITING);
    assert(!memcmp(draw_of(UNIT,0,0)->m,draw_of(UNIT,0,1)->m,NODE_COUNT*52u));
    assert(!memcmp(draw_of(UNIT,1,0)->m,original,sizeof original));
    near(draw_of(UNIT,0,0)->m[R_UPPER][0],0);near(draw_of(UNIT,0,0)->m[L_UPPER][0],0);
    call_body_active=0;
    /* Melee keeps the original animation precedence: no cosmetic arm solve. */
    setup();S8(UNIT_BODY+0x289,1);frame(1);assert(draws_of(UNIT,0)==1&&!sense_body.arms);
    /* An unreachable wrist is clamped inside reach, finite, lengths kept. */
    setup();right_target[0]=10.55f;frame(1);shadow=draw_of(UNIT,1,0);assert(shadow&&sense_body.arms);
    float l1=dist(original[R_FORE]+10,original[R_UPPER]+10),l2=dist(original[R_HAND]+10,original[R_FORE]+10);
    near(dist(shadow->m[R_FORE]+10,shadow->m[R_UPPER]+10),l1);near(dist(shadow->m[R_HAND]+10,shadow->m[R_FORE]+10),l2);
    near(dist(shadow->m[R_HAND]+10,original[R_UPPER]+10),(l1+l2)*.999f);
    /* A non-finite or far accepted wrist is refused, not drawn as a reach. */
    setup();right_target[1]=NAN;frame(1);assert(draws_of(UNIT,0)==1&&!sense_body.arms);
    setup();right_target[2]=40;frame(1);assert(draws_of(UNIT,0)==1&&!sense_body.arms);
#endif

    /* Gates: each leaves the complete original result and world bytes. */
    for(unsigned gate=0;gate<17;gate++){
        setup();snapshot_world();
        switch(gate){
        case 0:halo_sense_set_body_enabled(0);break;            /* default/off */
        case 1:active=0;break;                                   /* no immersive Sense */
        case 2:pad_mode=1;break;                                 /* ordinary gamepad */
        case 3:window_mode=1;break;                              /* flat window, trackers live */
        case 4:S16(0x00719720,1);break;                          /* multiplayer */
        case 5:S16(UNIT_BODY+0x2F0,0);break;                     /* vehicle seat */
        case 6:S32(UNIT_BODY+0x11C,0x67890003);break;            /* parented */
        case 7:fresh=0;break;                                    /* no poses */
        case 8:S16(0x14000,0x9999);break;                        /* invalid salt */
        case 9:S16(UNIT_BODY+0x1F2,0xF00);break;                 /* node range */
        case 10:S32(0x3B0B8,65);break;                           /* node count */
        case 11:memcpy(GPTR(0x3C000+HEAD*0x9C),"bip01 helmet",13);break; /* unknown rig */
        case 12:S16(0x3C000+2*0x9C+0x24,4);S16(0x3C000+4*0x9C+0x24,2);break; /* parent cycle */
        case 13:putf(UNIT_BODY+0xE0,0);break;                    /* dead */
        case 14:app_blocked=1;break;                             /* app menu */
        case 15:S8(0x00718FC9,1);break;                          /* original shell/menu/cutscene flag */
        case 16:S8(0x10011,1);break;                             /* players globals +11h input-disabled */
        }
        snapshot_world();
        frame(1);window_mode=0;
        if(sense_body_current()||draws_of(UNIT,0))fprintf(stderr,"gate %u admitted state=%d\n",gate,sense_body.state);
        expect_original();assert(!sense_body_current());expect_world_unchanged();
    }
    /* The real weapon module only advances its epoch while tracked rendering
     * runs. A frozen epoch after a gamepad handover, or an old latch, never
     * reuses the cached body or reports connected-arm suppression. */
#if BODY_TEST_API
    setup();frame(1);assert(draws_of(UNIT,0)==1);
    pad_mode=1;freeze_epoch=1;frame(1);expect_original();panorama_nested=1;assert(!host_sense_body_active());panorama_nested=0;
    pad_mode=0;now+=UINT64_C(300000000);frame(1);expect_original();freeze_epoch=0;frame(1);assert(draws_of(UNIT,0)==1);
#endif
    /* Scope's narrow original render and mirrors never get the body. */
    setup();sense_scope_nested=1;frame(1);sense_scope_nested=0;expect_original();
    setup();S8(0x7C3138,1);frame(1);S8(0x7C3138,0);
    assert(draws_of(UNIT,0)==1&&draws_of(0x34560001,0)==1&&entry_ecx[0]==UNIT_NODES); /* stock mirror: original nodes and weapon */
    /* Third-person director: the original says no, nothing changes. */
    setup();S32(0x6AC560+8,0x00447370);frame(1);
    assert(draws_of(UNIT,0)==1&&draws_of(0x34560001,0)==1);for(unsigned i=0;i<originals;i++)assert(entry_ecx[i]!=sense_body_eye&&entry_ecx[i]!=sense_body_shadow);

    /* Mid-epoch live changes fail closed at submission (DROP: no original). */
    setup();snapshot_world();mutate_during_lod=1;frame(1);
    assert(draws_of(UNIT,0)==0&&mutate_during_lod<0);
    for(unsigned i=0;i<originals;i++)assert(entry_object[i]!=UNIT||entry_ecx[i]!=UNIT_NODES); /* never the raw head */
    S16(UNIT_BODY+0x1F2,0x600);expect_world_unchanged();
    setup();mutate_during_lod=2;frame(1);assert(draws_of(UNIT,0)==0&&draws_of(UNIT,1)==0);
    /* Recycled slot mid-epoch: a new salt never receives the cached palette. */
    setup();frame(1);assert(draws_of(UNIT,0)==1);
    S16(0x14000,0x2346);S32(0x12034,0x23460000);
    {EngineCPU q={0};q.gpr[4]=0x21000;S32(0x21000,0x0050F049);S32(0x21024,0x23460000);q.gpr[1]=UNIT_NODES;q.gpr[0]=0x45670004;
     panorama_nested=1;sense_body_pending=0x23460000;SenseWorldModelOverride o;
     assert(sense_body_model_prepare(&q,&o)==SENSE_WORLD_MODEL_DROP&&!o.palette);panorama_nested=0;}

    /* Prepare helper writes nothing but its output and pending marker. */
    setup();frame(1);panorama_nested=1;
    {EngineCPU q={.fp_control=0x37F};q.gpr[4]=0x21000;S32(0x21000,0x0050F049);S32(0x21024,UNIT);S32(0x2102C,4);
     q.gpr[1]=UNIT_NODES;q.gpr[0]=0x45670004;sense_body_pending=UNIT;EngineCPU copy=q;unsigned char stack[0x40];
     memcpy(stack,GPTR(0x21000),sizeof stack);snapshot_world();SenseWorldModelOverride o;
     assert(sense_body_model_prepare(&q,&o)==SENSE_WORLD_MODEL_APPLY&&o.palette==sense_body_eye&&!o.center);
     assert(!memcmp(&q,&copy,sizeof q)&&!memcmp(stack,GPTR(0x21000),sizeof stack)&&sense_body_pending==UINT32_MAX);
     expect_world_unchanged();
     assert(sense_body_model_prepare(&q,&o)==SENSE_WORLD_MODEL_NONE); /* claimed once */
    }
    panorama_nested=0;
    /* Escaped original submit and escaped predicate: arguments restored,
     * guards clear, world unchanged, next epoch draws normally. */
    setup();snapshot_world();escape_submit=1;
    frame(1);
    assert(!sense_world_model_nested&&!sense_body_predicate_nested&&originals>=1&&!escape_submit);
    expect_world_unchanged();
    escape_predicate=1;frame(1);assert(!sense_body_predicate_nested&&sense_body_pending==UINT32_MAX);
    frame(1);assert(draws_of(UNIT,0)==1&&draws_of(UNIT,1)==1);expect_world_unchanged();

#if BODY_TEST_PICKUP
    /* Shared wrapper: body and synthetic ground-weapon lanes are disjoint;
     * exactly one original per submission; centre and ECX restored. */
    setup();pickup_enabled=1;snapshot_world();frame(1);
    assert(originals==8&&pickup_body_calls==0&&draws_of(UNIT,0)==1&&draws_of(GROUND,0)==1&&draws_of(GROUND,1)==1);
    unsigned ground_originals=0;
    for(unsigned i=0;i<originals;i++)if(entry_object[i]==GROUND){
        ground_originals++;assert(entry_ecx[i]==0x7E000u&&entry_center[i]==0x7E800u); /* during the one original */
    }
    assert(ground_originals==2);
    near(draw_of(GROUND,0,0)->m[0][12],31);near(draw_of(MARINE,0,0)->m[0][12],30.30f);
    expect_world_unchanged();
    /* Dropped body: zero originals for it; pickup unaffected. */
    setup();pickup_enabled=1;mutate_during_lod=1;frame(1);
    assert(draws_of(UNIT,0)==0&&draws_of(GROUND,0)==1&&pickup_body_calls==0);
    S16(UNIT_BODY+0x1F2,0x600);
#endif
    /* Status and settings bridge. */
    setup();frame(1);HaloSenseBodyStatus status;host_sense_body_status(&status);
    assert(status.state==(BODY_TEST_API?HALO_SENSE_BODY_ACTIVE:HALO_SENSE_BODY_ARMS_WAITING));
    halo_sense_set_body_enabled(0);host_sense_body_status(&status);assert(status.state==HALO_SENSE_BODY_OFF&&!halo_sense_body_enabled());
    free(engine_flat_base);
    printf("PASS: original 0050E930/0050EB70/0050EBA0/0050EE20/0050EA50/00445AC0/004D6FC0/004CC0D0 path; body+shadow admission (api=%d pickup=%d); "
           "head/hand/carried-weapon suppression; once-per-epoch eye/bearing consistency; length-preserving clamped arms; "
           "gates, mid-epoch fail-closed DROP, escape restoration, world bytes unchanged\n",BODY_TEST_API,BODY_TEST_PICKUP);
    return 0;
}
