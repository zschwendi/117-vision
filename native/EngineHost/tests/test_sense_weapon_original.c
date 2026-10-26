/* Exercises the original lifted skeleton and shot-ray helper instructions.
 * No physical device, game process, save, inventory or damage is fabricated. */
#include "../host.h"
#include "engine_flags.h"
#include "engine_hooks.h"
#include "engine_functions.h"
#include <assert.h>
#include <stdarg.h>
#include "engine_registers.h"
#include "sub_004CB970.c"
#include "sub_004CBAD0.c"
#include "sub_004CC0D0.c"
#include "sub_004D6880.c"
#include "sub_005658F0.c"
#include "sub_004F5350.c"
#include "sub_004BEF80.c"
/* Grenade release: the original detach-to-flight path runs as lifted. */
#include "sub_0056E440.c"
#include "sub_004F7B70.c"
#include "sub_004F6900.c"
#include "sub_004CBDE0.c"
#include "sub_004052C0.c"
#include "sub_00401990.c"
#include "sub_00401050.c"
/* Original detach (004F6610 -> 004F5DE0 -> 004F6EC0/004F8FE0) and delete
 * (004F5BD0 -> 004F5AA0, 004F59D0) run as lifted. */
#include "sub_004F6610.c"
#include "sub_004F5DE0.c"
#include "sub_004F6EC0.c"
#include "sub_004F8FE0.c"
#include "sub_004F5BD0.c"
#include "sub_004F5AA0.c"
#include "sub_004F59D0.c"
/* Original throw start 0056E080 with its small self-contained callees. */
#include "sub_0056E080.c"
#include "sub_004018E0.c"
#include "sub_004C2F30.c"
#include "sub_005704D0.c"
#include "sub_004726F0.c"
/* Viewmodel render and first-person animation class transitions, as lifted:
 * 004924B0 (gun and arms 004D6FC0 submissions, 00493EA0 node remap), the
 * event->class table 004927C0, class->graph slot 00492880, class set
 * 00492E60 (with its 004930B0 blend) and animation-end transition 00492D20;
 * the unanimated-model node pose 004D7610/004D7690. */
#include "sub_004924B0.c"
#include "sub_00493EA0.c"
#include "sub_004927C0.c"
#include "sub_00492880.c"
#include "sub_00492E60.c"
#include "sub_004930B0.c"
#include "sub_00492D20.c"
#include "sub_004D7610.c"
#include "sub_004D7690.c"
#include "sub_004D6FC0.c"
#define sub_00568F80 sub_00568F80_original
#include "sub_00568F80.c"
#undef sub_00568F80
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
uint8_t *engine_flat_base;uint32_t engine_trace_lo,engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu){(void)cpu;}
void host_log(const char *f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a);}
static int active=1,fresh=1,dual_enabled,panorama_nested,panorama_fp_cache_nested;
static uint64_t input_generation=1;
static int support_grip,support_blocked,app_blocked,custom_positions;static float grip_positions[2][3];
static int head_fresh=1,scope_active,scope_armed=1,scope_escape,scope_draws,scope_change_epoch;
static uint64_t scope_elapsed;
static float head_forward[3]={1,0,0},head_up[3]={0,0,1},scope_camera[9],scope_camera2[9],scope_fov;
static unsigned char scope_interface[0xAC];
bool hostgc_poll(HostGCSnapshot *out){memset(out,0,sizeof *out);return false;}
static uint64_t now=1000000000;
uint64_t host_monotonic_ns(void){return now;}
int halo_sense_active(void){return active;}
int halo_sense_requested(void){const char *e=getenv("HALO_SENSE_WEAPON");return !e||strcmp(e,"0");}
int halo_sense_dual_requested(void){return dual_enabled;}
void halo_sense_set_scope_active(int value){scope_active=value;}
static uint64_t status_session=1;
void halo_sense_status(HaloSenseStatus *s){memset(s,0,sizeof *s);s->session=status_session;s->generation=input_generation;s->active=active;s->requested=halo_sense_requested();s->gameplay_supported=1;}
int halo_sense_head_read(HaloSenseHeadPose *h){memset(h,0,sizeof *h);memcpy(h->forward,head_forward,12);memcpy(h->up,head_up,12);h->generation=h->controller_generation=1;return head_fresh;}
int halo_sense_app_input_blocked(void){return app_blocked;}
int halo_sense_scope_input(HaloSenseScopeInput *i){memset(i,0,sizeof *i);i->session=1;i->generation=input_generation;i->armed=scope_armed;halo_sense_head_read(&i->head);return !app_blocked&&scope_active&&head_fresh&&fresh;}
int halo_sense_dual_input(HaloSenseDualInput *p){memset(p,0,sizeof *p);p->session=1;p->generation=input_generation;p->fresh=fresh;p->gameplay_supported=1;p->support_grip=support_grip;p->support_blocked=support_blocked;p->armed[0]=p->armed[1]=1;return !app_blocked&&fresh;}
static int art_available,art_captured_draws;
int host_sense_reticle_art_begin(unsigned h,uint32_t w,const HaloSenseReticleFrame*t,float x,float y){(void)x;(void)y;return art_available&&h<2&&t->hands[h].valid&&t->hands[h].weapon==w;}
int host_sense_reticle_art_end(int complete){assert(complete);return 1;}
static HaloSenseReticleFrame reticles;
void halo_sense_reticle_publish(const HaloSenseReticleFrame *p){if(p)reticles=*p;else memset(&reticles,0,sizeof reticles);}
int halo_sense_reticle_read(HaloSenseReticleFrame *p){*p=reticles;return fresh&&(p->hands[0].valid||p->hands[1].valid);}

void halo_sense_observe_controller(const HostGCSnapshot *p){(void)p;}
void halo_sense_set_gameplay_supported(int s){(void)s;}
/* Every real producer observation (sense_mount.inc) publishes here. The check
 * needs the grenade module's in-flight flags, so it is defined after the hooks. */
static void mount_publication_check(void);
static unsigned mount_publications,grenade_entry_publications;static int grenade_entry;
void halo_sense_set_native_controls(int n,uint64_t g){(void)n;(void)g;mount_publication_check();}
int halo_sense_gameplay_supported(void){return 1;}
static float aim_angle,aim_roll;
int halo_sense_weapon_pose(unsigned hand,float p[3],float f[3],float u[3]){
    if(!fresh)return 0;
    float pos[3]={.5f,hand==1?-.25f:.25f,-.3f},dir[3]={cosf(aim_angle),sinf(aim_angle),0},up[3]={sinf(aim_angle)*sinf(aim_roll),-cosf(aim_angle)*sinf(aim_roll),cosf(aim_roll)};
    if(custom_positions)memcpy(pos,grip_positions[hand],12);
    memcpy(p,pos,12);memcpy(f,dir,12);memcpy(u,up,12);return 1;
}
int halo_sense_read(HaloSenseFrame *frame){memset(frame,0,sizeof *frame);if(!fresh||app_blocked)return 0;
    frame->generation=frame->controller_generation=1;
    frame->left.valid=halo_sense_weapon_pose(0,frame->left.position,frame->left.forward,frame->left.up);
    frame->right.valid=halo_sense_weapon_pose(1,frame->right.position,frame->right.forward,frame->right.up);return frame->left.valid&&frame->right.valid;
}
/* Bridge acceptance (covered by test_sense_throw.c): 1 this tick's release,
 * 0 ordinary, -1 this tick's pulse unhonourable. Its age is measured on the
 * bridge clock at entry; grenade_accept_delay is time passing before return. */
static HaloSenseGrenadeRelease grenade_claim;static int grenade_claim_result;static uint64_t grenade_accept_delay,grenade_sample_time;
static unsigned grenade_accepts;
int halo_sense_grenade_accept(uint32_t clock,uint32_t tick,HaloSenseGrenadeRelease *out){int r=grenade_claim_result;grenade_claim_result=0;grenade_accepts++;
    assert(clock==0x64000u&&tick==G32(0x6400Cu)); /* the original game clock and tick (00470BF0 +0x0C) */
    memset(out,0,sizeof *out);if(r>0){*out=grenade_claim;if(grenade_sample_time)out->age_ns=now-grenade_sample_time;}
    now+=grenade_accept_delay;return r;}
/* The bridge's armed-hold read (its state machine is covered by
 * test_sense_throw.c): held under this fixture's session and generation. */
static int hold_held;static uint64_t hold_generation_skew;
int halo_sense_grenade_hold(HaloSenseGrenadeHold *h){memset(h,0,sizeof *h);h->session=1;h->generation=input_generation+hold_generation_skew;
    h->controller_generation=1;h->held=hold_held&&fresh&&!app_blocked;return h->held;}
static int collision_hit;static unsigned mirrored_passes;static int hold_nested_probe;
uint32_t guest_alloc(uint32_t size){assert(size==0x100);return 0x60000;}
static unsigned grenade_cleanup_detaches,grenade_cleanup_deletes;static uint32_t grenade_current=0x45670002u,grenade_body=0x17000;
/* dual_wield.inc's original calls (attach 004F6440, control 004C2990, update
 * 004C1530, detach 004F6610 and the holster leaves), recorded with their
 * attach/detach parent effect on the offhand object at 0x17000. */
typedef struct { uint32_t fn,arg0,eax; } DualCall;
static DualCall dual_calls[64];static unsigned dual_call_count;static int dual_recording;
uint32_t host_call_guest(EngineCPU *cpu,uint32_t fn,int n,const uint32_t *args,int pops){
    if(dual_recording&&(fn==0x004F6440u||fn==0x004C2990u||fn==0x004C1530u||fn==0x004F6610u||fn==0x004F5DE0u||
                        fn==0x004F50F0u||fn==0x004F9A20u||fn==0x004BCFC0u)){
        assert(!pops&&dual_call_count<64);dual_calls[dual_call_count++]=(DualCall){fn,n>0?args[0]:0,cpu->gpr[0]};
        if(fn==0x004F6440u)S32(0x1711C,args[0]);
        if(fn==0x004F6610u)S32(0x1711C,UINT32_MAX);
        return 0;
    }
    if(fn==0x004924B0u){ /* the mirrored offhand viewmodel pass */
        assert(!n&&!pops);mirrored_passes++;return 0;
    }
    if(fn==0x004D6FC0u){ /* the held grenade's extra original submission */
        assert(n==11&&!pops);uint32_t sp=cpu->gpr[4],pc=cpu->pc;
        for(int i=n-1;i>=0;i--)engine_push(cpu,args[i],4);engine_push(cpu,0xDEADC0DEu,4);
        engine_dispatch(cpu,fn);assert(cpu->pc==0xDEADC0DEu);cpu->gpr[4]=sp;cpu->pc=pc;return cpu->gpr[0];}
    if(fn==0x004F5350u){assert(n==1&&!pops);uint32_t sp=cpu->gpr[4],pc=cpu->pc;engine_push(cpu,args[0],4);engine_push(cpu,0xDEADC0DEu,4);engine_dispatch(cpu,fn);assert(cpu->pc==0xDEADC0DEu);cpu->gpr[4]=sp;cpu->pc=pc;return cpu->gpr[0];}
    if(fn==0x004F6610u||fn==0x004F5BD0u){ /* grenade cleanup: original detach (cdecl) and delete (EAX) */
        assert(n==(fn==0x004F6610u?1:0)&&!pops);uint32_t sp=cpu->gpr[4],pc=cpu->pc;
        if(n)engine_push(cpu,args[0],4);engine_push(cpu,0xDEADC0DEu,4);
        cpu->pc=fn; /* entered at its entry, as host_call_guest does */
        if(fn==0x004F6610u){grenade_cleanup_detaches++;sub_004F6610(cpu);}else{assert(cpu->gpr[0]==grenade_current);grenade_cleanup_deletes++;sub_004F5BD0(cpu);}
        assert(cpu->pc==0xDEADC0DEu);cpu->gpr[4]=sp;cpu->pc=pc;return cpu->gpr[0];}
    assert(fn==0x00505880&&n==5&&pops==0&&args[0]==0x1000E9&&args[3]==0x23450000);return collision_hit;
}
static void ret_to_caller(EngineCPU *cpu){cpu->pc=engine_pop(cpu,4);}
/* Physical commit transport lives in halo_settings.c; its options stay off here. */
int halo_sense_physical_enqueue(uint64_t s,uint64_t g,uint64_t q,int a,uint32_t o,uint32_t t){(void)s;(void)g;(void)q;(void)a;(void)o;(void)t;assert(!"physical options are off in this fixture");return 0;}
int halo_sense_physical_pending(int a){(void)a;return 0;}
void halo_sense_physical_cancel(int a){(void)a;}
void halo_sense_physical_set_delivery_check(int (*c)(int,uint32_t,uint32_t)){(void)c;}
/* Accepted-shot/charge haptics, the cache contract and scoped smoothing are
 * covered by test_sense_weapon_continuity.c; defaults here. */
int hostgc_play_spatial_haptic(const HostGCSpatialHaptic *e){(void)e;return 1;}
void hostgc_cancel_spatial_haptics(int kind){(void)kind;}
uint64_t hostgc_source_generation(void){return 1;}
uint64_t halo_sense_set_weapon_haptic_owner(int o,uint64_t s,uint64_t g){(void)o;(void)s;(void)g;return 0;}
int halo_sense_grip_continuity(void){return 1;}
int halo_sense_scope_smoothing(void){return 0;}
int halo_sense_weapon_haptics(void){return 1;}
int halo_haptics_enabled(void){return 1;}
void halo_haptics_weapon_owner_renew(void){}
void halo_haptics_weapon_owner_release(void){}
void halo_haptics_weapon_shot_begin(void){}
void halo_haptics_weapon_shot_end(int accepted){(void)accepted;}
float halo_settings_haptics_strength(void){return 1;}
/* Body rendering is disabled in the weapon/physical fixtures; its original-engine path has a separate fixture. */
int host_sense_body_active(void){return 0;}
#include "../sense_weapon_hooks.inc"
/* Which grenade mount gate compiled. The runner's second variant defines
 * HALO_SENSE_GRENADE_LOCAL_MOUNT, selecting sense_grenade.inc's standalone
 * parent/seat gate; HALO_SENSE_MOUNT_H is defined either way, because the real
 * turret producer (sense_mount.inc) is always part of sense_weapon_hooks.inc.
 * No producer function is mocked or redefined here. */
#ifdef HALO_SENSE_GRENADE_LOCAL_MOUNT
#define GRENADE_PRODUCER_GATE 0
#else
#define GRENADE_PRODUCER_GATE 1
#endif
#ifndef HALO_SENSE_GRENADE_CANCEL_MOUNT
#error "production sense_grenade.inc must provide HALO_SENSE_GRENADE_CANCEL_MOUNT"
#endif
static void mount_publication_check(void){
    assert(!sense_grenade_in_accept&&!sense_grenade_in_create&&!sense_grenade_in_release); /* never inside an original grenade action */
    mount_publications++;if(grenade_entry)grenade_entry_publications++;
}
/* A real producer-visible seat cycle completed between observations: the
 * original exit stamp (last vehicle +0x32C, exit tick +0x330) moved. */
static void mount_exit_stamp(void){S32(0x1532C,0x56780003u);S32(0x15330,G32(0x15330)+1);}
#include "../dual_wield.inc"
/* The outer render boundary: every model build and shot below is its own
 * epoch, as between game frames. */
static void boundary(void){EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20004,0x50000);S32(0x20008,1);assert(!host_sense_weapon_dispatch(&c,0x0050BEA0));}
/* Completes a 200 ms grip latch/release ease in fixture time. */
static void settle(void){now+=HALO_SENSE_GRIP_EASE_NS;boundary();}
static unsigned fires,rays;static float observed_origin[3],observed_forward[3],observed_velocity;
static unsigned grenades;static int grenade_escape;static float grenade_direction[3];
static int escape_ray;
void sub_004F6AA0(EngineCPU *cpu){float velocity[3]={2,3,4};memcpy(GPTR(cpu->gpr[6]),velocity,12);ret_to_caller(cpu);}
void sub_004F5C30(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004C0180(EngineCPU *cpu){ret_to_caller(cpu);}
static int grenade_release_running;
/* Original 004D6FC0 submissions run as lifted (its LOD selection, default
 * records and palette x bind loop through 004CC0D0). Recorded at entry: the
 * model (EAX), palette (ECX), return address and the 11 cdecl arguments; at
 * its rasterizer sink 004D72A0: the model data, level, flags, the context's
 * object (ebp-0D8), permutation/colour records (ebp-058/-054) and the
 * transformed palette (ebp-0DDC). The original reuses its argument slots. */
typedef struct { uint32_t model,palette,ret,args[11]; float nodes[4][13];
    int submitted; uint32_t data,level,flags,object,permutation,colours; float drawn[4][13]; } FpDraw;
static FpDraw fp_draws[8];static unsigned fp_draw_count;static int fp_draw_escape;static void (*fp_draw_during)(void);
static FpDraw *fp_draw_current;
static void fp_draw_record(EngineCPU *cpu){
    uint32_t sp=cpu->gpr[4];FpDraw *d=&fp_draws[fp_draw_count<8?fp_draw_count:7];fp_draw_count++;memset(d,0,sizeof *d);
    d->model=cpu->gpr[0];d->palette=cpu->gpr[1];d->ret=G32(sp);for(unsigned i=0;i<11;i++)d->args[i]=G32(sp+4+4*i);
    memcpy(d->nodes,GPTR(cpu->gpr[1]),sizeof d->nodes);fp_draw_current=d;
}
void sub_004D72A0(EngineCPU *cpu){
    FpDraw *d=fp_draw_current;assert(d&&!d->submitted);uint32_t ctx=ARG(2);
    d->submitted=1;d->data=ARG(0);d->level=ARG(3);d->flags=ARG(5);d->object=G32(ctx-4);
    d->permutation=G32(ctx+0x7C);d->colours=G32(ctx+0x80);memcpy(d->drawn,GPTR(G32(ctx)),sizeof d->drawn);
    if(d->ret==0x004926DAu&&fp_draw_during){void (*during)(void)=fp_draw_during;fp_draw_during=NULL;during();}
    if(d->ret==0x004926DAu&&fp_draw_escape){cpu->pc=0xDEAD;return;} /* the original arms draw escapes */
    ret_to_caller(cpu);
}
void sub_00518B40(EngineCPU *cpu){(void)cpu;assert(!"viewmodel draws never take the shadow path");}
void sub_00526F50(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_0052B530(EngineCPU *cpu){ret_to_caller(cpu);} /* non-shadow draws end through it (004D7288) */
/* 004924B0's lighting query (stubbed: one 0x74-byte record) and 00492E60's
 * sound stop (never reached: +0x1E98 stays none). */
void sub_0050EA00(EngineCPU *cpu){cpu->gpr[0]=0x5A000;ret_to_caller(cpu);}
void sub_00549EE0(EngineCPU *cpu){(void)cpu;assert(!"first-person sound stop is not reached");}
/* Original unit eye: only the grenade release may use it; the tracked shot ray must not. */
void sub_00568F80(EngineCPU *cpu){assert(grenade_release_running&&"Body eye projection must be disabled for the tracked ray");sub_00568F80_original(cpu);}
/* Stubbed boundaries of the grenade path (documented in the grenade section):
 * creation 0056E280 (marker lookup/object_new/attach), detach 004F6610,
 * collision 00505880 and post-hit 004F8310. Branches the on-foot player path
 * never takes must stay unreachable. */
static unsigned grenade_creates,grenade_deletes,grenade_sweeps;static int grenade_sweep_hit;
static float sweep_origin[3],sweep_delta[3];
void sub_00505880(EngineCPU *cpu){
    uint32_t sp=cpu->gpr[4];assert(G32(sp+4)==0x1000E9u&&G32(sp+16)==UINT32_MAX);
    memcpy(sweep_origin,GPTR(G32(sp+8)),12);memcpy(sweep_delta,GPTR(G32(sp+12)),12);grenade_sweeps++;
    uint32_t result=G32(sp+20);S16(result+0x10,grenade_sweep_hit?0:UINT16_MAX);
    if(grenade_sweep_hit){float hit[3];for(unsigned k=0;k<3;k++)hit[k]=sweep_origin[k]+sweep_delta[k]*.5f;memcpy(GPTR(result+0x18),hit,12);}
    cpu->gpr[0]=grenade_sweep_hit?1:0;ret_to_caller(cpu);}
void sub_004F8310(EngineCPU *cpu){ret_to_caller(cpu);}
/* Bounded leaf stubs below the lifted detach/delete: map/cluster
 * reconnection (004F5C30 above, 00552020), the deletion render/cluster
 * leaves 004F9A20/004F9B00 (counted: one per original 004F59D0), and the
 * network-deletion message leaves of 004F5AA0 (encode returns 0: nothing is
 * sent, as in a local campaign). */
void sub_00552020(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004F9A20(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004F9B00(EngineCPU *cpu){assert(cpu->gpr[7]==grenade_current);grenade_deletes++;ret_to_caller(cpu);}
void sub_004F05E0(EngineCPU *cpu){cpu->gpr[0]=UINT32_MAX;ret_to_caller(cpu);}
void sub_004EC940(EngineCPU *cpu){cpu->gpr[0]=0;ret_to_caller(cpu);}
void sub_004E9D40(EngineCPU *cpu){ret_to_caller(cpu);}
/* Bounded leaf stubs below the real 0056E080: the unit animation set 00565F90
 * (cdecl unit, animation; its effect here is the original's own writes of
 * +0x2A3 animation state and +0xD0/+0xD2 index/frame, and AL; refusable),
 * the first-person animation 00492730, weapon lowering 004C4B50 and the
 * throw effect 004507A0 (never reached: no effect tag in the fixture). */
static unsigned grenade_animation_starts,grenade_fp_starts;static int grenade_animation_refuses;
void sub_00565F90(EngineCPU *cpu){uint32_t sp=cpu->gpr[4],datum=G32(sp+4),animation=G32(sp+8);
    assert(datum==0x23450000u&&animation==0x21);grenade_animation_starts++;
    if(!grenade_animation_refuses){S8(0x152A3,0x21);S16(0x150D0,0);S16(0x150D2,0);}
    cpu->gpr[0]=(cpu->gpr[0]&~255u)|(grenade_animation_refuses?0u:1u);ret_to_caller(cpu);}
void sub_00492730(EngineCPU *cpu){assert(G32(cpu->gpr[4]+4)==0x11);grenade_fp_starts++;ret_to_caller(cpu);}
void sub_004C4B50(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004507A0(EngineCPU *cpu){(void)cpu;assert(!"no throw effect tag in this fixture");}
void sub_004F6080(EngineCPU *cpu){(void)cpu;assert(!"seated eye path");}
void sub_00410A60(EngineCPU *cpu){(void)cpu;assert(!"parented release path");}
void sub_004C0B10(EngineCPU *cpu){(void)cpu;assert(!"network release path");}
void sub_004E1A80(EngineCPU *cpu){(void)cpu;assert(!"network release path");}
void sub_004F4560(EngineCPU *cpu){(void)cpu;assert(!"network release path");}
void sub_004F5C10(EngineCPU *cpu){(void)cpu;assert(!"network release path");}
void engine_dispatch(EngineCPU *cpu,uint32_t address){
    cpu->pc=address;if(host_sense_weapon_dispatch(cpu,address))return;
    if(address==0x0056E080u){sub_0056E080(cpu);return;} /* the real original start */
    if(address==0x0056E280u){
        /* Creation stub with the original's effects: one debit, attach, state 2. */
        /* A fresh projectile object: slot 2, or slot 3 once slot 2 is
         * original-delete-marked (deferred free not modelled). Attached to the
         * unit's node 0 (identity at the origin, so local == world) as the
         * unit's first child, like 004F6440. */
        uint32_t unit=0x15000,count=unit + 0x31E + G8(unit+0x31C);grenade_creates++;S8(count,(uint8_t)(G8(count)-1));
        int second=(G8(0x14018+2)&8)!=0;grenade_current=second?0x56780003u:0x45670002u;grenade_body=second?0x18000:0x17000;
        uint32_t g=grenade_body;memset(GPTR(g),0,0x1F4);S16(g+0xB4,5);S16(g+0x9C,0);S32(g+0x114,UINT32_MAX);S32(g+0x118,UINT32_MAX);
        S32(g+0x11C,0x23450000);S8(g+0x120,0);float one=1;memcpy(GPTR(g+0x74),&one,4);memcpy(GPTR(g+0x88),&one,4);
        S32(unit+0x118,grenade_current);S32(unit+0x294,grenade_current);S8(unit+0x28D,2);
        float hand[3]={10.1f,20.2f,30.4f};memcpy(GPTR(g+0x5C),hand,12);
        ret_to_caller(cpu);return;}
    if(address==0x004F5350u){sub_004F5350(cpu);return;}
    if(address==0x004BEF80u){sub_004BEF80(cpu);return;}
    if(address==0x0056E440u){grenades++;memcpy(grenade_direction,GPTR(0x1523C),12);
        grenade_release_running=1;sub_0056E440(cpu);grenade_release_running=0;
        if(grenade_escape)cpu->pc=0xDEAD;return;}
    if(address==0x004ACAD0u){assert(sense_reticle_art_nested);art_captured_draws++;ret_to_caller(cpu);return;}
    if(address==0x0050BEA0u){if(!host_sense_scope_rendering()&&host_sense_scope_dispatch(cpu,address))return;
        scope_draws++;memcpy(scope_camera,GPTR(ARG(0)+4),36);memcpy(scope_camera2,GPTR(ARG(0)+0x58),36);memcpy(&scope_fov,GPTR(ARG(0)+4+0x28),4);
        if((ARG(1)&65535u)>1)memcpy(scope_interface,GPTR(ARG(0)+0xAC),sizeof scope_interface);
        now+=scope_elapsed;if(scope_change_epoch)input_generation++;
        if(scope_escape)cpu->pc=0xDEAD;else ret_to_caller(cpu);return;}
    if(address==0x004D6880u){sub_004D6880(cpu);return;}
    if(address==0x004CB970u){sub_004CB970(cpu);return;}
    if(address==0x004CBAD0u){sub_004CBAD0(cpu);return;}
    if(address==0x004CC0D0u){sub_004CC0D0(cpu);return;}
    if(address==0x005658F0u){if(escape_ray){cpu->pc=0xDEAD;return;}sub_005658F0(cpu);return;}
    if(address==0x004F6AA0u){sub_004F6AA0(cpu);return;}
    if(address==0x004D6FC0u){fp_draw_record(cpu);sub_004D6FC0(cpu);return;}
    if(address==0x004924B0u){fp_draw_count=0;sub_004924B0(cpu);return;}
    if(address!=0x004C3F10u)fprintf(stderr,"unexpected fixture call %08x\n",address);
    assert(address==0x004C3F10u);fires++;
    EngineCPU child=*cpu;child.gpr[4]=0x23000;S32(0x23000,0x004C4EE2);S32(0x23004,0x23450000);
    S32(0x23008,0x23200);S32(0x2300C,1);S32(0x23010,1);child.gpr[0]=0x23100;child.gpr[3]=0x23120;
    float garbage[3]={77,88,99};memcpy(GPTR(0x23100),garbage,12);memcpy(GPTR(0x23120),garbage,12);
    engine_dispatch(&child,0x005658F0);assert(G32(0x2300C)==1&&G32(0x23010)==1);
    if(!escape_ray){assert(child.pc==0x004C4EE2);
        /* Retail004C4C40 applies tag offsets after005658F0; this boundary must
         * restore both origin and direction before original dispersion. */
        float shifted[3]={19,-27,43};memcpy(GPTR(0x23100),shifted,12);memcpy(GPTR(0x23120),shifted,12);
        child.gpr[4]=0x23400;S32(0x23400,0x004C500F);S32(0x23404,0x23100);S32(0x23408,0x23120);
        engine_dispatch(&child,0x004593B0u);assert(child.pc==0x004C500F&&child.gpr[0]==UINT32_MAX);
        memcpy(observed_origin,GPTR(0x23100),12);memcpy(observed_forward,GPTR(0x23120),12);memcpy(&observed_velocity,GPTR(0x23200),4);rays++;}
    ret_to_caller(cpu);
}
static void putf(uint32_t p,float f){memcpy(GPTR(p),&f,4);}
static void near(float actual,float wanted){assert(fabsf(actual-wanted)<.00005f);}
static void setup(void){
    memset(engine_flat_base,0,0x900000);sense_muzzle.valid=0;fresh=active=1;aim_angle=aim_roll=0;fires=rays=grenades=0;escape_ray=grenade_escape=support_blocked=app_blocked=0;
    S32(0x696664,0x004CC0D0);
    putf(0x672AC4,1);putf(0x672BE4,2);
    S32(0x87A478,0x10000);S32(0x87A480,0x11000);S32(0x10004,0x12340000);
    S16(0x11020,1);S16(0x11022,0x200);S32(0x11034,0x12000);S16(0x12000,0x1234);S32(0x12034,0x23450000);
    S32(0x8603B0,0x13000);S16(0x13020,2);S16(0x13022,12);S32(0x13034,0x14000);
    S16(0x14000,0x2345);S16(0x14006,0x550);S32(0x14008,0x15000);S16(0x150B4,0);putf(0x150E0,1);
    S16(0x1400C,0x3456);S16(0x14012,0x340);S32(0x14014,0x16000);S16(0x160B4,2);S32(0x1611C,0x23450000);
    S32(0x1511C,UINT32_MAX); /* retail on-foot biped: no parent (cleared by exit 0056AB50) */
    S16(0x152F0,0xFFFF);S16(0x152F2,0);S16(0x152F4,0);S32(0x152F8,0x34560001);
    S8(0x15320,0xFF);S8(0x15321,0xFF);head_fresh=scope_armed=1;scope_active=scope_escape=scope_draws=0;
    head_forward[0]=1;head_forward[1]=head_forward[2]=0;head_up[0]=head_up[1]=0;head_up[2]=1;
    float origin[3]={10,20,30};memcpy(GPTR(0x1505C),origin,12);
    S32(0x87BC14,0x30000);S32(0x3000C,0x45670000);S32(0x30014,0x31000);S32(0x31068,5);S32(0x3106C,0x32000);
    S32(0x16000,0x45670001);S32(0x3002C,0x45670001);S32(0x30034,0x33000);S32(0x33468,0x45670002);
    S32(0x3004C,0x45670002);S32(0x30054,0x34000);S32(0x340AC,1);S32(0x340B0,0x35000);
    memcpy(GPTR(0x35000),"primary trigger",16);S32(0x35034,1);S32(0x35038,0x36000);putf(0x36004,.2f);S8(0x36002,2);putf(0x3601C,1);S32(0x340B8,5);S32(0x340BC,0x37000);
    const char *names[]={"root","r wrist","gun","barrel","l wrist"};int parent[]={-1,0,1,2,0},child[]={1,2,3,-1,-1},sibling[]={4,-1,-1,-1,-1};
    for(unsigned i=0;i<5;i++){uint32_t b=0x32000+i*64;memcpy(GPTR(b),names[i],strlen(names[i])+1);S16(b+0x20,child[i]);S16(b+0x22,sibling[i]);S16(b+0x24,parent[i]);memcpy(GPTR(0x37000+i*0x9C),names[i],strlen(names[i])+1);
        uint32_t q=0x40000+i*32;putf(q+12,1);putf(q+28,1);putf(q+16,i==0?0:i==1||i==4?.3f:.1f);putf(q+20,i==1?-.2f:i==4?.2f:0);putf(q+24,i==1||i==4?-.2f:0);}
    float cam[9]={10,20,30.6f,1,0,0,0,0,1};memcpy(GPTR(0x50004),cam,36);
    EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20004,0x50000);S32(0x20008,1);assert(!host_sense_weapon_dispatch(&c,0x0050BEA0));assert(sense_camera_unit==0x23450000);
}
static void model(void){
    boundary();
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;c.gpr[1]=0x50004;
    S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);S32(0x2000C,0x50010);S32(0x20010,0x5001C);
    engine_dispatch(&c,0x004D6880);assert(c.pc==0x00493C49&&c.gpr[4]==0x20004);
}
/* A further viewmodel build inside the current epoch (panorama views/eyes). */
static void model_build(void){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;c.gpr[1]=0x50004;
    S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);S32(0x2000C,0x50010);S32(0x20010,0x5001C);
    engine_dispatch(&c,0x004D6880);assert(c.pc==0x00493C49&&c.gpr[4]==0x20004);
}
static void fire(void){boundary();EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,0x34560001);S32(0x20008,0);engine_dispatch(&c,0x004C3F10);assert(c.pc==0x401234&&c.gpr[4]==0x20004);}
/* Grenade fixture world: the verified original constants the release reads
 * (0x672ACC=1/30, 0x672D3C=pi/2, 0x672B84=1/65536, 0x672BD8=1e-4) and the
 * verified stock speed: characters\\cyborg\\cyborg unit tag +0x2C0 = 10.0
 * Halo units/second in the user's own a10/a30/b30/c10/d40 maps (with eye
 * heights +0x400/+0x404 = 0.62/0.35). Spawn offsets are fixture values. */
static float grenade_stock=10.f;
static uint64_t grenade_epoch_counter;
static void grenade_world(void){
    setup();S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x1F4);S32(0x14020,0x17000);S16(0x170B4,5);S32(0x1711C,UINT32_MAX);S16(0x1709C,0);
    S32(0x6B7AF4,0x62000);S16(0x6B7AF8,1);putf(0x62008,1);putf(0x672B84,1.f/65536.f);putf(0x672D3C,1.5707964f);putf(0x672ACC,1.f/30.f);
    double epsilon=1e-4;memcpy(GPTR(0x672BD8),&epsilon,8);
    S32(0x1511C,UINT32_MAX);S32(0x151F4,UINT32_MAX);S32(0x15218,0);S32(0x15294,UINT32_MAX);S8(0x1528D,0);S8(0x1531C,0);S8(0x1531E,2);
    float walk[3]={.05f,0,0};memcpy(GPTR(0x15068),walk,12);
    putf(0x312C0,grenade_stock);putf(0x31400,.62f);putf(0x31404,.35f);
    S32(0x746FA0,0x63000);S32(0x63174,0x63200);putf(0x63268,.2f);putf(0x6326C,.1f);putf(0x63270,-.05f);
    S32(0x696720,0x61000);putf(0x61008,1);
    float aim[3]={1,0,0};memcpy(GPTR(0x1523C),aim,12);memset(GPTR(0x17068),0,0x30);
    /* Second projectile slot, the unit's child list and its node 0 palette
     * (identity at the origin) for the original detach. */
    S16(0x13020,4);S16(0x14024,0x5678);S16(0x1402A,0x1F4);S32(0x1402C,0x18000);S32(0x15118,UINT32_MAX);
    S16(0x151F2,0x400);putf(0x15400,1);putf(0x15404,1);putf(0x15414,1);putf(0x15424,1);
    grenade_creates=grenade_deletes=grenade_sweeps=grenades=grenade_accepts=grenade_cleanup_detaches=grenade_cleanup_deletes=0;
    grenade_sweep_hit=collision_hit=0;grenade_accept_delay=grenade_sample_time=0;grenade_claim_result=0;active=1;status_session=1;
    grenade_current=0x45670002u;grenade_body=0x17000;S32(0x6F1D6C,0x64000);S32(0x6400C,77);
    /* For the real 0056E080: no special animation (+0x2A3=-1, so its 0x17..0x29
     * jump table is not read), the unit-tag animation graph (+0x44) whose
     * animation 0 has its throw keyframe at frame 5, both grenade types'
     * globals entries without effect tags, an idle weapon, the controls array. */
    S8(0x152A3,0xFF);S32(0x31044,0x45670003);S32(0x3006C,0x45670003);S32(0x30074,0x65000);S32(0x65078,0x65100);S16(0x65134,5);
    S32(0x6312C,0x63400);S32(0x63410,UINT32_MAX);S32(0x63454,UINT32_MAX);S8(0x1531F,2);S8(0x16238,0);S32(0x6B145C,0x66000);
    grenade_animation_starts=grenade_fp_starts=0;grenade_animation_refuses=0;
}
static void grenade_unit(float x,float y,float z){float p[3]={x,y,z};memcpy(GPTR(0x1505C),p,12);}
/* One outer render whose camera is at the unit eye (+0.62) with forward f and up u. */
static void grenade_render(const float f[3],const float u[3]){
    float cam[9];memcpy(cam,GPTR(0x1505C),12);cam[2]+=.62f;memcpy(cam+3,f,12);memcpy(cam+6,u,12);memcpy(GPTR(0x50004),cam,36);boundary();
}
static void grenade_publish(int result,const float hand[3],const float velocity[3],uint64_t age){
    now+=2000000; /* accepted after its sample's render */
    memset(&grenade_claim,0,sizeof grenade_claim);
    grenade_claim.sample.hand.valid=1;memcpy(grenade_claim.sample.hand.position,hand,12);
    grenade_claim.sample.hand.forward[1]=1;grenade_claim.sample.hand.up[2]=1; /* points left: neither head nor motion */
    memcpy(grenade_claim.sample.velocity,velocity,12);grenade_claim.sample.motion_valid=1;
    grenade_claim.sample.generation=grenade_claim.sample.controller_generation=1;
    grenade_claim.epoch=++grenade_epoch_counter;grenade_claim.session=1;grenade_claim.age_ns=age;grenade_claim_result=result;
}
/* Original state-0 call: EDI unit, one cdecl argument; returns AL. Entered as
 * the retail caller does (005625B0 ENGINE_DIRECT): only engine_hooks.h
 * routing reaches the hook; a direct call would assert. */
static void grenade_unrouted(EngineCPU *cpu){(void)cpu;assert(!"0056E080 is not routed through engine_hooks.h");}
static int grenade_accept(void){
    assert(engine_hooked(0x0056E080u)&&engine_hooked(0x0056E280u)&&engine_hooked(0x0056E440u)&&engine_hooked(0x004BEF80u));
    EngineCPU c={.fp_control=0x37F};c.gpr[7]=0x23450000;c.gpr[4]=0x25000;S32(0x25000,0x401234);S32(0x25004,0);
    c.pc=0x401230;grenade_entry=1;ENGINE_DIRECT(&c,0x0056E080u,grenade_unrouted);grenade_entry=0;assert(c.pc==0x401234&&c.gpr[4]==0x25004);return (int)(c.gpr[0]&255u);
}
static void grenade_spawn(void){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x25000;S32(0x25000,0x401234);S32(0x25004,0x23450000);
    grenade_entry=1;engine_dispatch(&c,0x0056E280u);grenade_entry=0;assert(c.pc==0x401234&&c.gpr[4]==0x25004);
}
/* Back to idle as the end of the original throw animation leaves it. */
static void grenade_idle(void){S8(0x1528D,0);S8(0x152A3,0xFF);}
/* Acceptance (when idle) then the frame-2 creation call. */
static void grenade_create(void){if(!G8(0x1528D))grenade_accept();grenade_spawn();}
static void grenade_release(int interrupted){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x25000;S32(0x25000,0x401234);S32(0x25004,0x23450000);S32(0x25008,(uint32_t)interrupted);
    grenade_entry=1;engine_dispatch(&c,0x0056E440u);grenade_entry=0;assert(c.pc==0x401234&&c.gpr[4]==0x25004);
}
static void grenade_final(float position[3],float velocity[3]){memcpy(position,GPTR(grenade_body+0x5C),12);memcpy(velocity,GPTR(grenade_body+0x68),12);}
static float grenade_length(const float v[3]){return sqrtf(sense_dot(v,v));}
/* Independent of the hook's helpers: world = f*x + (u x f)*y + u*z. */
static void grenade_basis(const float f[3],const float u[3],const float v[3],float out[3]){
    float l[3]={u[1]*f[2]-u[2]*f[1],u[2]*f[0]-u[0]*f[2],u[0]*f[1]-u[1]*f[0]};
    for(unsigned k=0;k<3;k++)out[k]=f[k]*v[0]+l[k]*v[1]+u[k]*v[2];
}

/* ---- Native reload/swap animation authority and the held grenade ---------
 * Verified retail tables (c9acf0c4...), loaded into the fixture image so the
 * lifted jump tables dispatch exactly as in the executable. */
static const uint32_t table_492830[18]={0x4927cf,0x4927d5,0x4927db,0x4927e1,0x4927e7,0x4927ed,0x4927f3,0x492829,0x492829,
    0x4927f9,0x4927ff,0x492805,0x49280b,0x492829,0x492811,0x492817,0x492823,0x49281d};
static const uint32_t table_492924[24]={0x492893,0x4928f6,0x4928fc,0x492896,0x49289c,0x4928a2,0x4928a8,0x4928ae,0x4928b4,0x4928ba,
    0x4928c0,0x4928c6,0x4928cc,0x4928d2,0x4928d8,0x492902,0x492908,0x49290e,0x4928de,0x4928e4,0x4928ea,0x4928f0,0x492914,0x49291a};
static const uint32_t table_49306c[4]={0x492eed,0x492ede,0x492ed1,0x492f30},table_49308c[3]={0x492f98,0x492fb5,0x492fbc};
static const uint8_t table_49307c[14]={0,0,0,0,3,1,1,3,3,3,3,3,3,2},table_493098[17]={0,2,2,1,1,1,1,0,2,2,2,2,2,2,2,2,0};
static const uint32_t table_492e30[6]={0x492d4d,0x492d5b,0x492db7,0x492d73,0x492d6d,0x492e2b};
static const uint8_t table_492e48[24]={0,1,1,5,5,0,0,0,0,0,0,0,0,2,2,3,0,0,4,0,0,1,0,1};
/* The graph animation slot names (retail first-person graphs, e.g. weapons\\assault rifle\\fp\\fp). */
static const char *fp_slot_names[28]={"idle","posing","fire-1","moving","overlays","light-off","light-on","reload-empty","reload-full",
    "overheated","ready","put-away","overcharged","melee","fire-2","overcharged-jitter","throw-grenade","ammunition","misfire-1",
    "misfire-2","throw-overheated","overheating","overheating-again","enter","exit-empty","exit-full","o-h-exit","o-h-s-enter"};
#define FP_CACHE 0x58000u
static void fp_tables(void){
    for(unsigned i=0;i<18;i++)S32(0x492830+4*i,table_492830[i]);for(unsigned i=0;i<24;i++)S32(0x492924+4*i,table_492924[i]);
    for(unsigned i=0;i<4;i++)S32(0x49306C+4*i,table_49306c[i]);for(unsigned i=0;i<3;i++)S32(0x49308C+4*i,table_49308c[i]);
    for(unsigned i=0;i<14;i++)S8(0x49307C+i,table_49307c[i]);for(unsigned i=0;i<17;i++)S8(0x493098+i,table_493098[i]);
    for(unsigned i=0;i<6;i++)S32(0x492E30+4*i,table_492e30[i]);for(unsigned i=0;i<24;i++)S8(0x492E48+i,table_492e48[i]);
}
static uint32_t lifted(void (*fn)(EngineCPU*),uint32_t pc,EngineCPU *c){
    uint32_t sp=c->gpr[4];engine_push(c,0xDEADC0DEu,4);c->pc=pc;fn(c);assert(c->pc==0xDEADC0DEu&&c->gpr[4]==sp);return c->gpr[0];
}
/* 004927C0: original event -> animation class. */
static int fp_event_class(int event){EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;c.gpr[0]=(uint32_t)event;return (int16_t)lifted(sub_004927C0,0x004927C0,&c);}
/* 00492880: class -> graph animation slot. */
static int fp_class_slot(int cls){EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;c.gpr[0]=(uint32_t)cls;return (int)lifted(sub_00492880,0x00492880,&c);}
/* 00492E60(class, local player 0, 1): the original's own class change. */
static void fp_set_class(int cls){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;engine_push(&c,1,4);engine_push(&c,0,4);c.gpr[0]=(uint32_t)cls;
    lifted(sub_00492E60,0x00492E60,&c);c.gpr[4]+=8;
}
/* 00492D20 (EDX local player 0): the original's transition when the current animation ends. */
static void fp_animation_end(void){EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;c.gpr[2]=0;lifted(sub_00492D20,0x00492D20,&c);}
static int fp_class(void){return (int16_t)G16(FP_CACHE+0xC);}
/* First-person world: the weapon tag's graph (+0x478) with one weapon type
 * whose 28 slots all name an animation; cache owned by the unit's weapon. */
static void fp_world(void){
    setup();fp_tables();
    S32(0x33478,0x45670000);S16(0x334E2,0);S8(0x1622C,0);
    S32(0x31048,1);S32(0x3104C,0x5D000);S32(0x5D010,28);S32(0x5D014,0x5D100);for(unsigned k=0;k<28;k++)S16(0x5D100+2*k,(uint16_t)k);
    S32(0x31074,28);S32(0x31078,0x5D200);
    S32(0x006B2D98,FP_CACHE);S8(FP_CACHE,1);S32(FP_CACHE+4,0x23450000);S32(FP_CACHE+8,0x34560001);
    S32(FP_CACHE+0x1E98,UINT32_MAX);S16(FP_CACHE+0x1E9C,UINT16_MAX);S8(FP_CACHE+0x1D8C,1);S8(FP_CACHE+0x1E0E,1);
    fp_set_class(0);assert(fp_class()==0);
    /* 004924B0's globals: local player view 0, the first-person arms model (globals +0x180 -> +0x0C). */
    S16(0x007C3108,0);S32(0x746FA0,0x63000);S32(0x63180,0x5B000);S32(0x5B00C,0x45670002);S16(0x6893E8,0xFFFF); /* no global LOD override */
    hold_held=0;hold_generation_skew=0;fp_draw_escape=0;
}
/* The original viewmodel render 004924B0 as 0050BFB0 calls it (no arguments). */
static void fp_render(void){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2C000;S32(0x2C000,0x00401234);c.pc=0x004924B0u;fp_draw_count=0;
    sub_004924B0(&c);assert(fp_draw_escape?c.pc==0xDEAD:(c.pc==0x00401234&&c.gpr[4]==0x2C004));
}
static void fire_weapon(uint32_t weapon){boundary();EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,weapon);S32(0x20008,0);engine_dispatch(&c,0x004C3F10);assert(c.pc==0x401234&&c.gpr[4]==0x20004);}
static int action_now(void){HaloSenseWeaponActionStatus a;boundary();return host_sense_weapon_action_status(&a)?a.action:-1;}
/* The original animated rig (Sense not rendering) for comparison. */
static void original_rig(unsigned char *nodes,unsigned count){active=0;model();memcpy(nodes,GPTR(0x41000),count*52u);active=1;}
/* Every node of the native rig is the original's, rotated about the eye by
 * the head yaw only (head +Y forward: x' = -y, y' = x about the eye). */
static void assert_native_rig(const unsigned char *original,unsigned count,int turned){
    for(unsigned i=0;i<count;i++){
        float before[13],after[13];memcpy(before,original+i*52,52);memcpy(after,GPTR(0x41000+i*52),52);
        if(!turned){for(unsigned k=0;k<13;k++)near(after[k],before[k]);continue;}
        near(after[0],before[0]);for(unsigned c=0;c<3;c++){near(after[1+c*3],-before[2+c*3]);near(after[2+c*3],before[1+c*3]);near(after[3+c*3],before[3+c*3]);}
        near(after[10],10-(before[11]-20));near(after[11],20+(before[10]-10));near(after[12],before[12]);
    }
}
/* Graph extension: the left hand's four knuckles under 'l wrist' (bone 4),
 * 8.5 cm along the fingers, index on the thumb (-Y) side of a palm-down left hand. */
static void fp_finger_rig(void){
    const char *names[4]={"frame l index low","frame l middlelow","frame l ring low","frame l pinky low"};
    const float lateral[4]={-.03f,-.01f,.01f,.03f};
    S32(0x31068,9);S16(0x32000+4*64+0x20,5);
    for(unsigned i=0;i<4;i++){uint32_t b=0x32000+(5+i)*64;memset(GPTR(b),0,64);memcpy(GPTR(b),names[i],strlen(names[i])+1);
        S16(b+0x20,UINT16_MAX);S16(b+0x22,i<3?(uint16_t)(6+i):UINT16_MAX);S16(b+0x24,4);
        uint32_t q=0x40000+(5+i)*32;memset(GPTR(q),0,32);putf(q+12,1);putf(q+28,1);putf(q+16,.085f/3.048f);putf(q+20,lateral[i]/3.048f);}
}
/* Two grenade types in the globals block, each a projectile definition
 * (object type 5) whose +0x34 model has one node and cutoffs 0/10/25/50/100,
 * with the retail default orientations (frag rotated, plasma identity). */
static const float frag_q[4]={-.065263f,-.113039f,-.495722f,.858616f},frag_t[3]={-.005f,.0075f,.00900036f};
static const float plasma_q[4]={0,0,0,1},plasma_t[3]={4.74e-6f,0,0};
static void tag_row(unsigned index,uint32_t datum,uint32_t data){S32(0x30000+index*32+0xC,datum);S32(0x30000+index*32+0x14,data);}
static void hold_world(void){
    fp_world();
    S32(0x63128,2);S32(0x6312C,0x63400);S32(0x63440,0x45670008u);S32(0x63484,0x45670009u);
    tag_row(8,0x45670008u,0x5C000);tag_row(9,0x45670009u,0x5C100);tag_row(10,0x4567000Au,0x5C200);tag_row(11,0x4567000Bu,0x5C400);
    S16(0x5C000,5);S32(0x5C034,0x4567000Au);S16(0x5C100,5);S32(0x5C134,0x4567000Bu);
    const uint32_t models[2]={0x5C200,0x5C400},nodes[2]={0x5C600,0x5C700};const float *q[2]={frag_q,plasma_q},*t[2]={frag_t,plasma_t};
    for(unsigned m=0;m<2;m++){
        const float cut[5]={0,10,25,50,100};memcpy(GPTR(models[m]+8),cut,20);S32(models[m]+0xB8,1);S32(models[m]+0xBC,nodes[m]);
        memcpy(GPTR(nodes[m]),"frame grenade",14);S16(nodes[m]+0x20,UINT16_MAX);S16(nodes[m]+0x22,UINT16_MAX);S16(nodes[m]+0x24,UINT16_MAX);
        memcpy(GPTR(nodes[m]+0x28),t[m],12);memcpy(GPTR(nodes[m]+0x34),q[m],16);
        /* A non-trivial bind matrix at +0x68, which 004D7071..004D70A7 applies. */
        const float bind[2][13]={{1,1,0,0,0,1,0,0,0,1,.01f,-.02f,.03f},{1,0,1,0,-1,0,0,0,0,1,0,0,.01f}};memcpy(GPTR(nodes[m]+0x68),bind[m],52);
    }
    S8(0x1531C,0);S8(0x1531E,2);S8(0x1531F,1);S8(0x1528D,0);
}
/* Expected node 0 from the original pose builder: 004D7610 copies the
 * model's default orientations, 004D7690 composes them under the root. */
static void original_grenade_palette(uint32_t model_data,const HaloSenseTransform *root,float out[13]){
    float position[3],forward[3],up[3];memcpy(position,root->p,12);memcpy(forward,root->c[0],12);memcpy(up,root->c[2],12);
    memcpy(GPTR(0x5E000),position,12);memcpy(GPTR(0x5E010),forward,12);memcpy(GPTR(0x5E020),up,12);memset(GPTR(0x5E100),0,0x100);
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;c.gpr[6]=model_data;engine_push(&c,0x5E100,4);lifted(sub_004D7610,0x004D7610,&c);c.gpr[4]+=4;
    engine_push(&c,0x5E020,4);engine_push(&c,0x5E100,4);engine_push(&c,0x5E300,4);engine_push(&c,model_data,4);
    c.gpr[0]=0x5E000;c.gpr[1]=0x5E010;lifted(sub_004D7690,0x004D7690,&c);c.gpr[4]+=16;
    memcpy(out,GPTR(0x5E300),52);
}
static void native_action_checks(void){
    /* 004927C0/00492880 as lifted: the emitters' events reach exactly the
     * classes classified, and each class names that graph animation. */
    fp_world();
    assert(fp_event_class(0x0B)==18&&fp_event_class(0x0C)==19&&fp_event_class(9)==13&&fp_event_class(10)==14&&fp_event_class(4)==10);
    for(int cls=0;cls<24;cls++){
        int slot=fp_class_slot(cls);assert(slot>=0&&slot<28);const char *name=fp_slot_names[slot];int action=sense_fp_class_action(cls);
        int reload=!strcmp(name,"reload-empty")||!strcmp(name,"reload-full")||!strcmp(name,"enter")||!strcmp(name,"exit-empty")||!strcmp(name,"exit-full");
        int swap=!strcmp(name,"ready")||!strcmp(name,"put-away");
        assert(action==(!strcmp(name,"melee")?HALO_SENSE_ACTION_MELEE:reload?HALO_SENSE_ACTION_RELOAD:swap?HALO_SENSE_ACTION_SWAP:HALO_SENSE_ACTION_NONE));
    }
    unsigned char original[5*52];original_rig(original,5);
    model();assert(sense_muzzle.valid&&action_now()==HALO_SENSE_ACTION_NONE);
    /* Reload-full started by the original's own class change: the whole
     * original rig owns the view for as long as the class is held (its own
     * frame advance, simulated here, never ends it), then 00492D20 returns
     * to idle and the very next build restores both tracked hands. */
    unsigned shots=fires;
    fp_set_class(fp_event_class(10));assert(fp_class()==14&&G16(FP_CACHE+0x16)==8&&G16(FP_CACHE+0x18)==0);
    assert(action_now()==HALO_SENSE_ACTION_RELOAD);
    assert(sense_muzzle.valid);fire();assert(fires==shots); /* same tick as the class change: the tracked muzzle is still fresh */
    for(unsigned frame=0;frame<87;frame++){ /* retail AR reload-full: 87 frames */
        S16(FP_CACHE+0x18,(uint16_t)frame);now+=33333333;model();
        assert(!sense_muzzle.valid&&fp_class()==14);assert_native_rig(original,5,0);
        if(frame%29==0){fire();assert(fires==shots);EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(!reticles.hands[1].valid);}
    }
    head_forward[0]=0;head_forward[1]=1;model();assert_native_rig(original,5,1);head_forward[0]=1;head_forward[1]=0;
    fp_animation_end();assert(fp_class()==0&&action_now()==HALO_SENSE_ACTION_NONE);
    model();assert(sense_muzzle.valid);fire();assert(fires==shots+1);
    HaloSenseTransform wrist;assert(sense_node(0x41000+4*52,&wrist));near(wrist.p[0],10+.5f/3.048f);near(wrist.p[1],20+.25f/3.048f);
    /* Reload-empty (event 9) and the loop reload: enter (15) -> 00492D20 ->
     * exit-full (17) -> idle, all native; the second weapon type (+0x4E2 1). */
    fp_set_class(fp_event_class(9));assert(fp_class()==13);model();assert(!sense_muzzle.valid&&action_now()==HALO_SENSE_ACTION_RELOAD);
    fp_animation_end();assert(fp_class()==0);model();assert(sense_muzzle.valid);
    S16(0x334E2,1);S16(FP_CACHE+0x1E94,2);S8(FP_CACHE+0x1E90,0);fp_set_class(15);assert(fp_class()==15);
    model();assert(!sense_muzzle.valid&&action_now()==HALO_SENSE_ACTION_RELOAD);
    fp_animation_end();assert(fp_class()==17);model();assert(!sense_muzzle.valid&&action_now()==HALO_SENSE_ACTION_RELOAD);
    fp_animation_end();assert(fp_class()==0);model();assert(sense_muzzle.valid);S16(0x334E2,0);
    /* Interruption: melee during a reload is still native (melee wins);
     * loss of the cache identity (another weapon's cache) is not ours. */
    fp_set_class(14);S8(0x15289,1);assert(action_now()==HALO_SENSE_ACTION_MELEE);S8(0x15289,0);
    S32(FP_CACHE+8,0x45670002u);model();assert(sense_muzzle.valid);S32(FP_CACHE+8,0x34560001u);
    fp_animation_end();assert(fp_class()==0);
    /* Swap: put-away of the current slot while the next slot is desired
     * (0056D6E0's order), 00492D20 holds put-away, the switch then readies
     * the new weapon (ready 19) and its end restores tracking on that weapon. */
    S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,0x23450000);S32(0x152FC,0x45670002);
    S16(0x152F4,1);model();assert(sense_muzzle.valid==0); /* desired != current: no tracked rig */
    fp_set_class(fp_event_class(0x0B));assert(fp_class()==18&&action_now()==HALO_SENSE_ACTION_SWAP);
    shots=fires;model();assert(!sense_muzzle.valid);assert_native_rig(original,5,0);
    head_forward[0]=0;head_forward[1]=1;model();assert_native_rig(original,5,1);head_forward[0]=1;head_forward[1]=0;
    fire();assert(fires==shots);
    fp_animation_end();assert(fp_class()==18);model();assert(!sense_muzzle.valid&&action_now()==HALO_SENSE_ACTION_SWAP);
    /* A melee interrupting the pending switch is native too (not the head-locked original rig). */
    S8(0x15289,1);assert(action_now()==HALO_SENSE_ACTION_MELEE);head_forward[0]=0;head_forward[1]=1;model();
    assert(!sense_muzzle.valid);assert_native_rig(original,5,1);head_forward[0]=1;head_forward[1]=0;S8(0x15289,0);
    S16(0x152F2,1);S32(FP_CACHE+8,0x45670002u);fp_set_class(fp_event_class(0x0C));assert(fp_class()==19&&action_now()==HALO_SENSE_ACTION_SWAP);
    model();assert(!sense_muzzle.valid);fire_weapon(0x45670002u);assert(fires==shots);
    /* A zoom level present during ready does not hand the rig to the scope
     * early; once ready ends the scope owns it (and its shot rules). */
    S8(0x15320,0);assert(action_now()==HALO_SENSE_ACTION_SWAP&&!scope_active);model();assert(!sense_muzzle.valid);
    fire_weapon(0x45670002u);assert(fires==shots);
    fp_animation_end();assert(fp_class()==0&&action_now()==HALO_SENSE_ACTION_SCOPE&&scope_active);S8(0x15320,0xFF);
    assert(action_now()==HALO_SENSE_ACTION_NONE);
    model();assert(sense_muzzle.valid&&sense_muzzle.weapon==0x45670002u);fire_weapon(0x45670002u);assert(fires==shots+1);
    /* Dual: a primary reload is the primary's alone. The holstered offhand
     * keeps its model, its original update (host_sense_offhand_begin) and its
     * reticle; the primary's animated left arm collapses to one point. */
    fp_world();S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);S32(0x152FC,0x45670002);S16(0x151F2,0x400);
    dual_enabled=1;sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;
    sense_model_hand=0;model();assert(sense_muzzles[0].valid);sense_model_hand=1;sense_secondary_ready=1;
    fp_set_class(14);model_build();assert(!sense_muzzle.valid);
    assert(G32(0x41000+4*52)==0&&G32(0x41000+52)!=0&&G32(0x41000+104)!=0); /* left hand collapsed, right hand and gun drawn */
    for(unsigned k=0;k<3;k++){float a,b;memcpy(&a,GPTR(0x41000+4*52+40+4*k),4);memcpy(&b,GPTR(0x41000+40+4*k),4);(void)b;assert(isfinite(a));}
    {EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(reticles.hands[0].valid&&!reticles.hands[1].valid);}
    S8(0x15289,1);assert(!host_sense_offhand_begin(0x23450000,0x45670002));S8(0x15289,0); /* melee still owns both hands */
    fp_animation_end();model_build();assert(sense_muzzle.valid&&G32(0x41000+4*52)==0); /* tracked again; left hidden by the dual rig as before */
    fp_set_class(14);model_build();
    /* (The fixture has no private offhand FP cache, so end() then resets the offhand model.) */
    assert(host_sense_offhand_begin(0x23450000,0x45670002));host_sense_offhand_end();fp_animation_end();
    dual_enabled=0;sense_secondary_ready=0;sense_dual_reset_model();
    puts("PASS: native reload/swap: lifted 004927C0/00492880 events and slots, 00492E60/00492D20 class lifetime, full original rig head-oriented, no shot/reticle, tracked hands restored on the next build");
}
/* The original 004CC0D0 product node x bind, as 004D7071 forms it. */
static void bind_product(const float node[13],uint32_t bind,float out[13]){
    memcpy(GPTR(0x5E400),node,52);EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2E000;
    engine_push(&c,0x5E500,4);engine_push(&c,bind,4);engine_push(&c,0x5E400,4);lifted(sub_004CC0D0,0x004CC0D0,&c);c.gpr[4]+=12;
    memcpy(out,GPTR(0x5E500),52);
}
static void hold_draw_checks(const FpDraw *arms,const FpDraw *grenade,uint32_t model,const float palette[13]){
    assert(arms->ret==0x004926DAu&&arms->model==0x45670002u&&arms->args[8]==0x34560001u&&arms->args[10]==8&&arms->args[5]==0x007C3114u);
    assert(arms->submitted&&arms->permutation==0x151B8u&&arms->colours==0x15134u&&arms->object==0x34560001u);
    assert(grenade->ret==0xDEADC0DEu&&grenade->model==model);
    /* What the original 004D6FC0 itself did with the injected submission. */
    uint32_t data=model==0x4567000Au?0x5C200u:0x5C400u,bind=G32(data+0xBC)+0x68;
    assert(grenade->submitted&&grenade->data==data&&grenade->level==4&&grenade->flags==8&&grenade->object==0x34560001u&&
           grenade->permutation==0x006B7F60u&&grenade->colours==0x006B7F08u); /* finest LOD, engine default records */
    float drawn[13];bind_product(palette,bind,drawn);for(unsigned k=0;k<13;k++)near(grenade->drawn[0][k],drawn[k]);
    float lod;memcpy(&lod,&grenade->args[0],4);near(lod,100);
    assert(!grenade->args[2]&&!grenade->args[3]);
    for(unsigned i=1;i<11;i++)if(i!=2&&i!=3)assert(grenade->args[i]==arms->args[i]); /* entry arguments, not the reused slots */
    for(unsigned k=0;k<13;k++)near(grenade->nodes[0][k],palette[k]);
}
static void grenade_hold_checks(void){
    hold_world();unsigned char before[0x550],weapon_before[0x340];
    /* Fallback rig (no knuckle nodes): the tracked left grip plus the fixed offset. */
    hold_held=1;memcpy(before,GPTR(0x15000),sizeof before);memcpy(weapon_before,GPTR(0x16000),sizeof weapon_before);
    model();assert(sense_hold.active&&!sense_hold.palm&&sense_hold.type==0&&sense_hold.model==0x4567000Au&&sense_hold.projectile==0x45670008u);
    HaloSenseTransform root;float f[3]={1,0,0},u[3]={0,0,1};assert(sense_basis(f,u,&root));
    root.p[0]=10+(.5f+.06f)/3.048f;root.p[1]=20+(.25f-.02f)/3.048f;root.p[2]=30.6f+(-.3f-.03f)/3.048f;
    float expected[13];original_grenade_palette(0x5C200,&root,expected);
    for(unsigned k=0;k<13;k++)near(sense_hold.palette[0][k],expected[k]);
    fp_render();assert(fp_draw_count==3&&fp_draws[0].ret==0x0049266Fu&&fp_draws[0].model==0x45670002u);
    hold_draw_checks(&fp_draws[1],&fp_draws[2],0x4567000Au,expected);
    fp_render();assert(fp_draw_count==3); /* every view of the epoch */
    assert(!memcmp(before,GPTR(0x15000),sizeof before)&&!memcmp(weapon_before,GPTR(0x16000),sizeof weapon_before)); /* nothing spent, attached or moved */
    /* The rendered palm of the left hand with knuckles: palm side of a palm-down left hand. */
    fp_finger_rig();model();assert(sense_hold.active&&sense_hold.palm);
    HaloSenseTransform wrist;assert(sense_node(0x41000+4*52,&wrist));
    root.p[0]=wrist.p[0]+.55f*.085f/3.048f;root.p[1]=wrist.p[1];root.p[2]=wrist.p[2]-.035f/3.048f;
    original_grenade_palette(0x5C200,&root,expected);for(unsigned k=0;k<13;k++)near(sense_hold.palette[0][k],expected[k]);
    fp_render();assert(fp_draw_count==3);hold_draw_checks(&fp_draws[1],&fp_draws[2],0x4567000Au,expected);
    /* It follows the tracked left hand only. */
    custom_positions=1;float grips[2][3]={{.6f,.3f,-.2f},{.5f,-.25f,-.3f}};memcpy(grip_positions,grips,sizeof grips);model();
    near(sense_hold.palette[0][10],expected[10]+.1f/3.048f);near(sense_hold.palette[0][11],expected[11]+.05f/3.048f);near(sense_hold.palette[0][12],expected[12]+.1f/3.048f);
    grip_positions[1][0]=.7f;model();near(sense_hold.palette[0][10],expected[10]+.1f/3.048f);custom_positions=0;model();
    /* Decided once per epoch (a further build of the epoch keeps it), but
     * every draw re-qualifies live: a release later in the epoch hides it for
     * the rest of that epoch, even if L1 is pressed again; the next epoch
     * decides afresh. */
    model_build();assert(sense_hold.active);fp_render();assert(fp_draw_count==3); /* another view's build of the same epoch */
    hold_held=0;model_build();assert(sense_hold.active);fp_render();assert(fp_draw_count==2);
    hold_held=1;fp_render();assert(fp_draw_count==2);
    hold_held=0;model();assert(!sense_hold.active);fp_render();assert(fp_draw_count==2);
    hold_held=1;model_build();assert(!sense_hold.active);model();assert(sense_hold.active);hold_held=0;model();
    /* Plasma (type 1) is its own model and default pose; empty counts draw nothing. */
    hold_held=1;S8(0x1531C,1);model();assert(sense_hold.active&&sense_hold.model==0x4567000Bu&&sense_hold.type==1);
    original_grenade_palette(0x5C400,&root,expected);for(unsigned k=0;k<13;k++)near(sense_hold.palette[0][k],expected[k]);
    fp_render();assert(fp_draw_count==3);hold_draw_checks(&fp_draws[1],&fp_draws[2],0x4567000Bu,expected);
    {   /* The arms' own pixel size (0) would select the coarsest level (0) of this model. */
        FpDraw saved=fp_draws[1];EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2D000;c.gpr[0]=0x4567000Bu;c.gpr[1]=0x60000;
        uint32_t args[11];memcpy(args,saved.args,sizeof args);args[0]=0;fp_draw_count=0;hold_nested_probe=1;
        host_call_guest(&c,0x004D6FC0u,11,args,0);hold_nested_probe=0;assert(fp_draw_count==1&&fp_draws[0].submitted&&fp_draws[0].level==0);
    }
    S8(0x1531F,0);model();assert(!sense_hold.active);fp_render();assert(fp_draw_count==2);S8(0x1531F,1);
    S8(0x1531C,0xFF);model();assert(!sense_hold.active);S8(0x1531C,0);
    /* A malformed globals block advertising four types: type 2 would read the
     * zoom byte +0x320 as a count. Rejected; types 0/1 unaffected. */
    S32(0x63128,4);S32(0x634C8,0x45670008u);S8(0x15320,0x7F);S8(0x1531C,2);model();assert(!sense_hold.active);
    S8(0x1531C,0);model();assert(sense_hold.active);S8(0x15320,0xFF);S32(0x63128,2);
    S8(0x1531E,0);model();assert(!sense_hold.active);S8(0x1531E,2);
    /* During the original throw while the real grenade exists (1, 2) none; after its release (3) the next hold shows. */
    for(unsigned state=1;state<4;state++){S8(0x1528D,(uint8_t)state);model();assert(sense_hold.active==(state==3));}S8(0x1528D,0);
    /* Ownership and context: another input generation, stale tracking, app
     * ownership, menu, seat, a native reload, the dual offhand in the hand,
     * the mirrored pass, an escaped original arms draw, and an accepted frame
     * lost after the decision (fail closed for the rest of that epoch). */
    hold_generation_skew=1;model();assert(!sense_hold.active);hold_generation_skew=0;
    fresh=0;model();fp_render();assert(fp_draw_count==2);fresh=1;
    app_blocked=1;model();assert(!sense_hold.active);app_blocked=0;
    S8(0x00718FC9,1);model();fp_render();assert(fp_draw_count==2&&!sense_hold.active);S8(0x00718FC9,0);
    S16(0x152F0,0);model();fp_render();assert(fp_draw_count==2);S16(0x152F0,UINT16_MAX);
    fp_set_class(14);model();assert(!sense_hold.active);fp_render();assert(fp_draw_count==2);fp_animation_end();
    sense_secondary_ready=1;model();assert(sense_hold.active&&sense_hold.dual&&G32(0x41000+4*52)!=0);sense_secondary_ready=0;model();
    model();assert(sense_hold.active);sense_mirror_scope=1;fp_render();assert(fp_draw_count==2);sense_mirror_scope=0;
    fp_draw_escape=1;fp_render();assert(fp_draw_count==2);fp_draw_escape=0;
    model();input_generation++;fp_render();assert(fp_draw_count==2);input_generation--;fp_render();assert(fp_draw_count==2);
    model();fp_render();assert(fp_draw_count==3);
    hold_held=0;puts("PASS: held grenade: selected frag/plasma projectile model through one extra original 004D6FC0 after the lifted 004924B0 arms draw, entry arguments kept, node pose equal to lifted 004D7610/004D7690, rendered left palm or tracked grip, once per epoch, empty/throwing/ownership/menu/seat/native/dual/mirror/escape fail closed, no unit or weapon byte changed");
}

/* Dual wielding through the real dual_wield.inc update (original calls
 * recorded): the offhand pistol holstered in slot 1 with its own private
 * first-person cache, as sense_dual_initialize leaves it. */
#define DUAL_CACHE 0x59000u
static void dual_world(void){
    S32(0x30030,0x39000);memset(GPTR(0x39000),0,80);strcpy(GPTR(0x39000),"weapons\\pistol\\pistol");
    S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);S32(0x152FC,0x45670002);S16(0x151F2,0x400);S8(0x16120,0);
    S32(0x6F1D6C,0x64000);S32(0x6400C,100);
    dual_enabled=1;sense_dual_cache=DUAL_CACHE;memset(GPTR(DUAL_CACHE),0,0x40);S8(DUAL_CACHE,1);S32(DUAL_CACHE+4,0x23450000);S32(DUAL_CACHE+8,0x45670002);
    sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;sense_dual_primary=0x34560001;
    sense_dual_session=1;sense_dual_generation=input_generation;
}
/* The offhand's own viewmodel build, on its private cache as sense_dual_build does. */
static void offhand_build(void){
    uint32_t primary_cache=G32(0x006B2D98);S32(0x006B2D98,DUAL_CACHE);sense_model_hand=0;model_build();sense_model_hand=1;S32(0x006B2D98,primary_cache);
    sense_secondary_ready=sense_muzzle_ready(0,0x23450000,0x45670002);
}
/* One original tick: 004C1530 for the primary with this dual input. Returns
 * the offhand's 004C2990 control flags (2 fire, 8 reload), or -1 when the
 * offhand's own original update did not run. */
static int dual_tick(float left_trigger,int reload){
    S32(0x6400C,G32(0x6400C)+1);dual_call_count=0;dual_recording=1;
    HaloDualWieldInput in={0};in.enabled=1;in.spatial=1;in.fresh=1;in.gameplay_supported=1;in.owner.session=1;in.owner.generation=input_generation;
    in.trigger[0]=left_trigger;in.reload[0]=in.reload[1]=reload;
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x2B000;S32(0x2B000,0x401234);S32(0x2B004,0x34560001);
    assert(host_dual_wield_update(&c,0x004C1530u,&in)==0&&c.gpr[4]==0x2B000);dual_recording=0;
    int flags=-1,updated=0;
    for(unsigned i=0;i<dual_call_count;i++){
        if(dual_calls[i].fn==0x004C2990u){assert(dual_calls[i].eax==0x45670002u);flags=(int)dual_calls[i].arg0;}
        if(dual_calls[i].fn==0x004C1530u){assert(dual_calls[i].arg0==0x45670002u);updated=1;}
    }
    assert(!dual_state.fault&&G32(0x1711C)==UINT32_MAX); /* holstered again after each update */
    return updated?flags:-1;
}
static void dual_native_and_hold_checks(void){
    /* Primary reload is its own: the offhand still fires, reloads and runs
     * its original update; its own reload keeps its tracked muzzle; both
     * finish and recover. */
    hold_world();dual_world();model();offhand_build();model();assert(sense_secondary_ready&&sense_muzzle.valid);
    assert(dual_tick(0,0)==0&&dual_tick(1,0)==2); /* armed by release, then fires */
    fp_set_class(14);model();offhand_build();assert(!sense_muzzle.valid&&sense_secondary_ready);
    assert(dual_tick(1,0)==2&&dual_tick(0,1)==8);  /* offhand fire and its own reload request during the primary reload */
    S16(DUAL_CACHE+0xC,14);offhand_build();assert(sense_secondary_ready&&sense_muzzles[0].valid); /* offhand reloading too: still tracked */
    assert(dual_tick(0,0)==0);
    fp_animation_end();S16(DUAL_CACHE+0xC,0);model();offhand_build();assert(sense_muzzle.valid&&sense_secondary_ready);
    assert(dual_tick(1,0)==2);fire();assert(fires&&rays);
    /* The armed grenade takes the offhand's hand: the primary rig shows the
     * tracked left hand with the grenade, the mirrored offhand gun and its
     * reticle are not drawn, the offhand neither fires nor reloads but its
     * own original update runs; after the release it re-arms only after L2
     * is released, and its model/fire return. */
    unsigned shots=fires;
    /* L1 already held when the offhand first becomes ready in the same
     * epoch: the admitted grenade still owns the left hand. */
    sense_secondary_ready=0;hold_held=1;model();assert(sense_hold.active&&!sense_hold.dual&&G32(0x41000+4*52)!=0);
    offhand_build();assert(sense_secondary_ready&&sense_grenade_hold_owns_left());
    model_build();assert(G32(0x41000+4*52)!=0); /* later builds of the epoch keep the tracked hand */
    mirrored_passes=0;{EngineCPU v={.fp_control=0x37F};v.gpr[4]=0x2C000;S32(0x2C000,0x00401234);assert(sense_dual_render_dispatch(&v,0x004924B0u)==0);}
    assert(mirrored_passes==0);
    {EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(!reticles.hands[0].valid);}
    assert(dual_tick(1,0)==0);
    hold_held=0;model();assert(!sense_grenade_hold_owns_left()&&G32(0x41000+4*52)==0);
    assert(dual_tick(1,0)==0&&dual_tick(0,0)==0&&dual_tick(1,0)==2); /* re-armed by an L2 release */
    shots=fires;
    hold_held=1;model();assert(sense_hold.active&&sense_hold.dual&&sense_grenade_hold_owns_left());
    assert(G32(0x41000+4*52)!=0);fp_render();assert(fp_draw_count==3&&fp_draws[2].model==0x4567000Au&&fp_draws[2].submitted);
    {EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(!reticles.hands[0].valid&&reticles.hands[1].valid);}
    mirrored_passes=0;{EngineCPU v={.fp_control=0x37F};v.gpr[4]=0x2C000;S32(0x2C000,0x00401234);assert(sense_dual_render_dispatch(&v,0x004924B0u)==0);}
    assert(mirrored_passes==0); /* declined: the original primary pass alone */
    offhand_build();assert(sense_secondary_ready);
    assert(dual_tick(1,0)==0&&dual_tick(1,1)==0); /* held L2 and Square: nothing requested, update still runs */
    fire();assert(fires==shots+1); /* the primary still fires */
    hold_held=0;model();assert(!sense_hold.active&&!sense_grenade_hold_owns_left()&&G32(0x41000+4*52)==0);
    mirrored_passes=0;{EngineCPU v={.fp_control=0x37F};v.gpr[4]=0x2C000;S32(0x2C000,0x00401234);assert(sense_dual_render_dispatch(&v,0x004924B0u)==1);}
    assert(mirrored_passes==1);
    {EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(reticles.hands[0].valid);}
    assert(dual_tick(1,0)==0&&dual_tick(0,0)==0&&dual_tick(1,0)==2); /* re-armed only by an L2 release */
    dual_enabled=0;sense_secondary_ready=0;sense_dual_reset_model();sense_dual_cache=0;
    puts("PASS: dual: primary reload/swap leaves the offhand's own original update, fire, reload and tracked muzzle live; an armed grenade takes the left hand (tracked hand + grenade, no mirrored gun/reticle, no offhand fire/reload, update still runs), re-armed by an L2 release afterwards");
}

/* Changes made while the original arms draw runs (before the injected
 * submission): each must fail closed for the rest of that epoch. */
static void during_release(void){hold_held=0;}
static void during_app(void){app_blocked=1;}
static void during_seat(void){S16(0x152F0,0);}
static void during_empty(void){S8(0x1531E,0);}
static void during_throw(void){S8(0x1528D,1);}
static void during_generation(void){input_generation++;}
static void during_menu(void){S8(0x00718FC9,1);}
static void during_type(void){S8(0x1531C,1);}
static void during_restore(void){hold_held=1;app_blocked=0;S16(0x152F0,UINT16_MAX);S8(0x1531E,2);S8(0x1528D,0);S8(0x00718FC9,0);S8(0x1531C,0);}
static void grenade_hold_boundary_checks(void){
    hold_world();hold_held=1;
    void (*changes[])(void)={during_release,during_app,during_seat,during_empty,during_throw,during_generation,during_menu,during_type};
    for(unsigned i=0;i<sizeof changes/sizeof *changes;i++){
        uint64_t generation=input_generation;model();assert(sense_hold.active);
        fp_draw_during=changes[i];fp_render();assert(fp_draw_count==2&&!fp_draw_during); /* original gun and arms only */
        during_restore();input_generation=generation;fp_render();assert(fp_draw_count==2); /* the rest of the epoch too */
        model();fp_render();assert(fp_draw_count==3);
    }
    /* Restoration: the arms call entered exactly as 004924B0 makes it ends
     * with the same CPU and every byte at or above ESP identical whether or
     * not the grenade is injected (only free stack below ESP and the private
     * palette differ); unit, weapon and grenade tag bytes never change. */
    model();assert(sense_hold.active);
    const uint32_t sp=0x2D000,args[11]={0,0,0x151B8,0x15134,0x5A000,0x007C3114,0,0x2D100,0x34560001,0,8};
    S32(sp,0x004926DAu);for(unsigned i=0;i<11;i++)S32(sp+4+4*i,args[i]);
    EngineCPU base={.fp_control=0x37F};base.gpr[0]=0x45670002;base.gpr[1]=0x2D200;base.gpr[2]=0x11111111;base.gpr[3]=0x22222222;
    base.gpr[4]=sp;base.gpr[5]=0x33333333;base.gpr[6]=0x44444444;base.gpr[7]=0x55555555;base.flags=0x246;
    unsigned char *before=malloc(0x900000),*injected=malloc(0x900000);assert(before&&injected);memcpy(before,engine_flat_base,0x900000);
    EngineCPU a=base;unsigned draws=sense_hold_draws;fp_draw_count=0;engine_dispatch(&a,0x004D6FC0u);
    assert(fp_draw_count==2&&sense_hold_draws==draws+1&&a.pc==0x004926DAu&&a.gpr[4]==sp+4);
    memcpy(injected,engine_flat_base,0x900000);memcpy(engine_flat_base,before,0x900000);
    sense_hold.active=0;EngineCPU b=base;fp_draw_count=0;engine_dispatch(&b,0x004D6FC0u);sense_hold.active=1;assert(fp_draw_count==1);
    assert(!memcmp(a.gpr,b.gpr,sizeof a.gpr)&&a.flags==b.flags&&a.pc==b.pc&&a.fp_top==b.fp_top&&a.fp_valid==b.fp_valid&&
           a.fp_control==b.fp_control&&a.fp_status==b.fp_status&&!memcmp(a.fp_reg,b.fp_reg,sizeof a.fp_reg));
    for(uint32_t addr=0;addr<0x900000;addr++){
        if((addr>=sp-0x3000&&addr<a.gpr[4])||(addr>=0x60000&&addr<0x60100))continue; /* free stack below the final ESP */
        if(injected[addr]!=engine_flat_base[addr]){fprintf(stderr,"byte %06x differs\n",addr);assert(0);}
    }
    assert(!memcmp(before+0x15000,engine_flat_base+0x15000,0x550)&&!memcmp(before+0x16000,engine_flat_base+0x16000,0x340)&&
           !memcmp(before+0x5C000,engine_flat_base+0x5C000,0x800)&&!memcmp(before+0x63000,engine_flat_base+0x63000,0x500));
    free(before);free(injected);hold_held=0;
    puts("PASS: held grenade draw re-qualified after the original arms draw (release, app, seat, empty, throw, lease, menu, type change fail closed for the epoch); CPU, stack at/above ESP, unit/weapon/grenade tag bytes identical with and without the injected submission");
}
int main(void){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);setenv("HALO_SENSE_WEAPON","1",1);setenv("HALO_SENSE_WEAPON_TRACE","1",1);setup();
    /* Marker quaternion math must match the actual retail conversion, including its handedness. */
    float q[4]={.2f,-.3f,.5f,.8f};memcpy(GPTR(0x36010),q,16);
    EngineCPU qc={.fp_control=0x37F};qc.gpr[4]=0x20000;qc.gpr[1]=0x36010;qc.gpr[2]=0x38000;S32(0x20000,0x401234);engine_dispatch(&qc,0x004CBAD0);
    HaloSenseTransform marker;int marker_bone;assert(sense_muzzle_marker(0x34560001,0x32000,5,&marker,&marker_bone)&&marker_bone==2);
    for(unsigned c=0;c<3;c++)for(unsigned r=0;r<3;r++){float original;memcpy(&original,GPTR(0x38004+(c*3+r)*4),4);near(marker.c[c][r],original);}
    float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);
    unsigned char unit[0x550],cam[36];memcpy(unit,GPTR(0x15000),sizeof unit);memcpy(cam,GPTR(0x50004),36);
    model();assert(sense_muzzle.valid);HaloSenseTransform right,gun;assert(sense_node(0x41000+52,&right)&&sense_node(0x41000+104,&gun));
    near(right.p[0],10+.5f/3.048f);near(right.p[1],20-.25f/3.048f);near(right.p[2],30.6f-.3f/3.048f);near(gun.c[0][0],1);
    fire();assert(fires==1&&rays==1);near(observed_forward[0],1);near(observed_forward[1],0);near(observed_velocity,2);near(observed_origin[0],right.p[0]+.3f);
    aim_angle=1.5707963268f;model();fire();assert(fires==2&&rays==2);near(observed_forward[0],0);near(observed_forward[1],1);near(observed_velocity,3);
    near(observed_origin[0],right.p[0]);near(observed_origin[1],right.p[1]+.3f);
    assert(!memcmp(unit,GPTR(0x15000),sizeof unit)&&!memcmp(cam,GPTR(0x50004),36));
    aim_roll=1.5707963268f;model();assert(sense_node(0x41000+104,&gun));near(gun.c[2][0],1);near(gun.c[2][1],0);near(gun.c[2][2],0);
    aim_roll=0;model();
    /* A marker attached to barrel node3 must include that extra bone offset. */
    S8(0x36002,3);model();fire();assert(fires==3);near(observed_origin[1],right.p[1]+.4f);
    S8(0x36002,2);model();
    /* Unknown graph invalidates the previous same-weapon cache immediately. */
    S32(0x31068,0);EngineCPU invalid={0};invalid.gpr[4]=0x20000;invalid.gpr[0]=0x45670000;S32(0x20000,0x00493C49);assert(!sense_update_model(&invalid,0x004D6880));assert(!sense_muzzle.valid);S32(0x31068,5);model();
    fires=2; fresh=0;fire();assert(fires==2);model();assert(!sense_muzzle.valid);for(unsigned i=0;i<5;i++)assert(G32(0x41000+i*52)==0);
    fresh=1;model();now+=300000000;fire();assert(fires==2);now-=300000000;
    S16(0x152F4,1);fire();assert(fires==2);S16(0x152F4,0);
    collision_hit=1;fire();assert(fires==2);collision_hit=0;
    /* Published targets are actual barrel points relative to the engine eye,
     * then converted to metres, without changing the neutral camera. */
    EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);
    assert(reticles.hands[1].valid&&!reticles.hands[0].valid&&reticles.hands[1].weapon==0x34560001);
    near(reticles.hands[1].origin[0],.5f);near(reticles.hands[1].origin[1],-.25f+.3f*3.048f);
    near(reticles.hands[1].direction[1],1);
    near(reticles.hands[1].point[1]-reticles.hands[1].origin[1],50.f*3.048f);
    S32(0x22000,0x401234);S32(0x2200C,0x38000);S16(0x38000,4);
    assert(!host_sense_weapon_dispatch(&rc,0x004ACAD0));
    rc.gpr[4]=0x22000;S16(0x38000,3);assert(!host_sense_weapon_dispatch(&rc,0x004ACAD0));
    collision_hit=1;sense_publish_reticles(&rc);assert(!reticles.hands[1].valid);collision_hit=0;
    /* A different inventory gun uses the left reflected grip, retaining its
     * own marker and cache identity. Original rig math still executes. */
    S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);S32(0x152FC,0x45670002);
    sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;sense_model_hand=0;dual_enabled=1;
    model();assert(sense_muzzles[0].valid&&sense_muzzles[0].weapon==0x45670002);
    float raw[9];memcpy(raw,GPTR(0x41000+104+4),36);
    float cross[3];sense_cross(raw,raw+3,cross);near(sense_dot(cross,raw+6),-1);
    sense_model_hand=1;model();sense_publish_reticles(&rc);assert(reticles.hands[0].valid&&reticles.hands[1].valid);
    assert(fabsf(reticles.hands[0].origin[0]-reticles.hands[1].origin[0])>.01f||fabsf(reticles.hands[0].origin[1]-reticles.hands[1].origin[1])>.01f);
    input_generation++;fire();assert(fires==2);input_generation--;dual_enabled=0;

    escape_ray=1;fire();assert(fires==3&&!sense_fire_nested&&!sense_ray_nested);assert(G32(0x2300C)==1&&G32(0x23010)==1);
    setenv("HALO_SENSE_WEAPON","0",1);assert(!host_sense_weapon_rendering());setenv("HALO_SENSE_WEAPON","1",1);active=0;assert(!host_sense_weapon_rendering());
    /* Two-hand AR support uses the original foregrip, fixed metric scale and
     * one shared gun/ray/reticle transform; left motion is not camera motion. */
    setup();support_grip=0;custom_positions=1;dual_enabled=0;sense_support.valid=0;
    S32(0x30030,0x39000);memcpy(GPTR(0x39000),"weapons\\assault rifle\\assault rifle",36);
    putf(0x40000+4*32+16,.4f);putf(0x40000+4*32+20,-.2f);
    float positions[2][3]={{.85f,-.25f,-.15f},{.5f,-.25f,-.3f}};memcpy(grip_positions,positions,sizeof positions);
    memcpy(unit,GPTR(0x15000),sizeof unit);memcpy(cam,GPTR(0x50004),36);
    S32(0x006B2D98,0x58000);S8(0x58000,1);S32(0x58004,0x23450000);S32(0x58008,0x34560001);
    /* First grab during reload/equip waits until idle; never captures the
     * magazine hand. A bad original span is retryable without holding any button. */
    HaloSenseWeaponStatus status;S16(0x5800C,9);model();assert(!sense_support.valid);
    host_sense_weapon_status(&status);assert(status.support==HALO_SENSE_SUPPORT_WAITING_POSE);
    S16(0x5800C,0);S16(0x162B0,1);model();assert(!sense_support.valid);S16(0x162B0,0);
    putf(0x40000+4*32+16,2);model();assert(!sense_support.valid);putf(0x40000+4*32+16,.4f);
    model();assert(sense_muzzle.valid&&sense_support.valid);host_sense_weapon_status(&status);assert(status.support==HALO_SENSE_SUPPORT_HELD);
    assert(sense_grip.active);settle();model(); /* latch frame showed the one-hand line; eased now */
    HaloSenseTransform left,barrel;assert(sense_node(0x41000+4*52,&left)&&sense_node(0x41000+104,&barrel)&&sense_node(0x41000+52,&right));
    float direction[3]={.35f,0,.15f};assert(sense_normalize(direction));
    near(barrel.c[0][0],direction[0]);near(barrel.c[0][2],direction[2]);
    float span[3];for(unsigned k=0;k<3;k++)span[k]=left.p[k]-right.p[k];near(sense_dot(span,span),.01f);
    fire();assert(fires==1);near(observed_forward[0],direction[0]);near(observed_forward[2],direction[2]);
    sense_publish_reticles(&rc);near(reticles.hands[1].direction[2],direction[2]);
    float muzzle_near[3];memcpy(muzzle_near,observed_origin,12);
    /* Double controller spacing: identical rifle scale, wrist, muzzle and
     * constrained foregrip. Reload wrist animation cannot steer the rifle. */
    grip_positions[0][0]=1.2f;grip_positions[0][2]=0;
    putf(0x40000+4*32+16,.7f);putf(0x40000+4*32+24,.1f);
    model();fire();assert(fires==2);for(unsigned k=0;k<3;k++)near(observed_origin[k],muzzle_near[k]);
    assert(sense_node(0x41000+4*52,&left)&&sense_node(0x41000+52,&right));
    for(unsigned k=0;k<3;k++)span[k]=left.p[k]-right.p[k];near(sense_dot(span,span),.01f);
    /* Dip the front hand: barrel and real shot dip while camera stays fixed. */
    grip_positions[0][0]=.85f;grip_positions[0][2]=-.45f;model();fire();assert(fires==3);
    near(observed_forward[2],-direction[2]);
    assert(!memcmp(unit,GPTR(0x15000),sizeof unit)&&!memcmp(cam,GPTR(0x50004),36));
    support_blocked=1;model();settle();model();fire();near(observed_forward[2],0);assert(sense_support.valid&&!sense_support.engaged);
    support_blocked=0;dual_enabled=1;model();settle();HaloSenseTransform pref_gun;assert(sense_world_weapon(1,0x15000,0x23450000,0x34560001,&pref_gun));near(pref_gun.c[0][2],-direction[2]);assert(sense_support.engaged); /* no eligible pair: preference alone cannot disable support */
    dual_enabled=0;
    const char *rifles[]={"weapons\\sniper rifle\\sniper rifle","weapons\\shotgun\\shotgun","weapons\\plasma rifle\\plasma rifle","weapons\\rocket launcher\\rocket launcher","weapons\\flamethrower\\flamethrower"};
    for(unsigned i=0;i<5;i++){memset(GPTR(0x39000),0,80);strcpy(GPTR(0x39000),rifles[i]);assert(sense_weapon_kind(0x34560001)==3&&!dual_supported_weapon(0x34560001));model();fire();near(observed_forward[2],-direction[2]);assert(sense_support.engaged);}
    input_generation++;model();fire();near(observed_forward[2],-direction[2]);assert(sense_support.engaged); /* scope/recenter epoch retains original foregrip geometry */
    /* A supported rifle keeps exactly the same barrel axis through zoom.
     * The real camera and actual shot share that axis and sight origin. */
    S8(0x15320,0);input_generation++;sense_action_update();model();fire();
    near(observed_forward[2],-direction[2]);assert(sense_support.engaged);
    memcpy(GPTR(0x50058),GPTR(0x50004),0x54);putf(0x5002C,.12f);putf(0x50080,.12f);
    EngineCPU supported_scope={0};supported_scope.gpr[4]=0x24000;S32(0x24000,0x401234);S32(0x24004,0x50000);S32(0x24008,1);
    engine_dispatch(&supported_scope,0x0050BEA0);
    for(unsigned k=0;k<3;k++){near(scope_camera[k],observed_origin[k]);near(scope_camera[k+3],observed_forward[k]);}
    S8(0x15320,0xFF);sense_action_update();
    support_grip=1;memset(GPTR(0x39000),0,80);memcpy(GPTR(0x39000),"weapons\\pistol\\pistol",22);model();settle();model();fire();near(observed_forward[2],0);assert(!sense_support.valid);
    /* Coincident hands and invalid spans never produce NaN/scale changes. */
    HaloSenseTransform test_right={0},test_left={0},solved;float f[3]={1,0,0},up[3]={0,0,1};
    assert(sense_basis(f,up,&test_right));test_left=test_right;float fg[3]={.1f,0,0};
    test_left.p[0]=.3f/3.048f;assert(sense_support_span(&test_right,&test_left,0));
    test_left.p[0]=.15f/3.048f;assert(!sense_support_span(&test_right,&test_left,0)&&sense_support_span(&test_right,&test_left,1));
    test_left.p[0]=.95f/3.048f;assert(!sense_support_span(&test_right,&test_left,0)&&sense_support_span(&test_right,&test_left,1));
    test_left.p[0]=1.1f/3.048f;assert(!sense_support_span(&test_right,&test_left,1));
    test_left.p[0]=-.4f/3.048f;assert(!sense_support_span(&test_right,&test_left,0)&&!sense_support_span(&test_right,&test_left,1));
    test_left.p[0]=NAN;assert(!sense_support_span(&test_right,&test_left,1));test_left.p[0]=0;
    assert(!sense_supported_pose(&test_right,&test_left,fg,&solved));test_left.p[2]=.15f;
    assert(sense_supported_pose(&test_right,&test_left,fg,&solved));near(solved.c[0][2],1);
    /* Authored foregrip lateral/vertical offsets must not tilt the aiming ray. */
    test_left.p[0]=.18f;test_left.p[1]=.03f;test_left.p[2]=.07f;
    float biased_foregrip[3]={.12f,.04f,-.03f},physical_span[3];memcpy(physical_span,test_left.p,12);assert(sense_normalize(physical_span));
    assert(sense_supported_pose(&test_right,&test_left,biased_foregrip,&solved));
    for(unsigned k=0;k<3;k++)near(solved.c[0][k],physical_span[k]);
    now+=300000000;host_sense_weapon_status(&status);assert(!status.support);now-=300000000;
    custom_positions=support_grip=0;
    /* Missing left gun is an explicit inventory reason, never success.
     * A genuine second supported object is required, with matching salt,
     * holstered parent and a validated attachment node. */
    uint32_t uid,pid,oid;int attach;dual_enabled=1;
    assert(!dual_inventory(&uid,&pid,&oid,&attach));host_sense_weapon_status(&status);
    assert(status.dual==HALO_SENSE_DUAL_NO_SECOND_WEAPON);
    S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);S32(0x152FC,0x45670002);
    assert(!dual_inventory(&uid,&pid,&oid,&attach));host_sense_weapon_status(&status);assert(status.dual==HALO_SENSE_DUAL_INVALID_ATTACHMENT);
    S16(0x151F2,0x400);assert(dual_inventory(&uid,&pid,&oid,&attach)==0x15000&&oid==0x45670002);
    S32(0x1711C,0x23450000);assert(!dual_inventory(&uid,&pid,&oid,&attach));host_sense_weapon_status(&status);assert(status.dual==HALO_SENSE_DUAL_SECONDARY_BUSY);
    S32(0x1711C,UINT32_MAX);S16(0x14018,0x5555);assert(!dual_inventory(&uid,&pid,&oid,&attach));host_sense_weapon_status(&status);assert(status.dual==HALO_SENSE_DUAL_NO_SECOND_WEAPON);
    dual_enabled=0;

    /* The original animated skeleton wins over tracked anchoring during
     * melee; every node/scale survives and the entire rig follows the head. */
    setup();active=0;model();unsigned char original_nodes[5*52];memcpy(original_nodes,GPTR(0x41000),sizeof original_nodes);
    active=1;S8(0x15289,1);model();assert(!memcmp(original_nodes,GPTR(0x41000),sizeof original_nodes)&&!sense_muzzle.valid);
    head_forward[0]=0;head_forward[1]=1;aim_angle=-1.5707963268f;model();
    for(unsigned i=0;i<5;i++){
        float before[13],after[13];memcpy(before,original_nodes+i*52,52);memcpy(after,GPTR(0x41000+i*52),52);
        near(after[0],before[0]);for(unsigned c=0;c<3;c++){near(after[1+c*3],-before[2+c*3]);near(after[2+c*3],before[1+c*3]);near(after[3+c*3],before[3+c*3]);}
        near(after[10],10-(before[11]-20));near(after[11],20+(before[10]-10));near(after[12],before[12]);
    }
    fire();assert(fires==0);sense_publish_reticles(&rc);assert(!reticles.hands[1].valid);
    S8(0x15289,0);S32(0x006B2D98,0x58000);S8(0x58000,1);S32(0x58004,0x23450000);S32(0x58008,0x34560001);S16(0x5800C,10);
    model();assert(!sense_muzzle.valid);S32(0x58008,0x45670002);model();assert(sense_muzzle.valid); /* unrelated cache is not melee */
    S32(0x58008,0x34560001);S16(0x5800C,0);model();assert(sense_muzzle.valid);
    /* A missing live target consumes its draw rather than flickering back to
     * an unrelated centre-HUD reticle. Scope preserves the original HUD. */
    sense_publish_reticles(&rc);art_available=1;rc.gpr[4]=0x22000;S32(0x22000,0x004B33C6);
    assert(host_sense_weapon_dispatch(&rc,0x004ACAD0)&&art_captured_draws==1);
    for(unsigned invalid=0;invalid<4;invalid++){
        memset(&reticles,0,sizeof reticles);reticles.hands[0].valid=invalid!=0;
        reticles.hands[1].valid=invalid>=2;reticles.hands[1].weapon=invalid==2?0xBAD:0x34560001;
        art_available=invalid!=3;rc.gpr[4]=0x22000;S32(0x22000,0x004B33C6);
        assert(host_sense_weapon_dispatch(&rc,0x004ACAD0)&&rc.pc==0x004B33C6&&art_captured_draws==1);
    }
    S8(0x15320,0);S8(0x15321,0xFF);sense_action_update();assert(scope_active);
    rc.gpr[4]=0x22000;S32(0x22000,0x004B33C6);assert(!host_sense_weapon_dispatch(&rc,0x004ACAD0));
    /* Scope requires a fresh gun; it never falls back to head-directed shots.
     * The actual original projectile helper retains velocity projection. */
    fresh=0;aim_angle=0;fire();assert(fires==0);fresh=1;fire();assert(fires==1);near(observed_forward[0],1);near(observed_forward[1],0);
    near(observed_origin[0],10+.5f/3.048f);near(observed_origin[1],20-.25f/3.048f);near(observed_origin[2],30.6f-.3f/3.048f);near(observed_velocity,2);
    scope_armed=0;fire();assert(fires==1);scope_armed=1;head_fresh=0;fire();assert(fires==1);head_fresh=1;
    S16(0x00719720,1);fire();assert(fires==1);S16(0x00719720,0);
    S16(0x152F0,0);fire();assert(fires==1);S16(0x152F0,UINT16_MAX);
    /* Original narrow-FOV scope render uses both weapon-oriented frusta, then
     * restores every source byte. Head motion cannot turn a stationary scoped gun. */
    memcpy(GPTR(0x50058),GPTR(0x50004),0x54);putf(0x5002C,.12f);putf(0x50080,.12f);
    unsigned char renderer[0xAC];memcpy(renderer,GPTR(0x50000),sizeof renderer);
    EngineCPU sc={0};sc.gpr[4]=0x24000;S32(0x24000,0x401234);S32(0x24004,0x50000);S32(0x24008,1);
    engine_dispatch(&sc,0x0050BEA0);HaloSenseWeaponActionStatus scope_marker;
    assert(sc.pc==0x401234&&scope_draws==1&&host_sense_scope_frame(&scope_marker)&&scope_marker.action==HALO_SENSE_ACTION_SCOPE&&scope_marker.zoom_level==0);
    near(scope_camera[3],1);near(scope_camera[4],0);near(scope_camera2[3],1);near(scope_camera2[4],0);near(scope_fov,.12f);near(sense_camera.c[0][0],1);
    for(unsigned k=0;k<3;k++)near(scope_camera[k],observed_origin[k]);
    head_forward[0]=-1;head_forward[1]=0;sc.gpr[4]=0x24000;engine_dispatch(&sc,0x0050BEA0);fire();assert(fires==2);near(scope_camera[3],1);near(scope_camera[4],0);near(observed_forward[0],1);near(observed_forward[1],0);
    app_blocked=1;HaloSenseWeaponActionStatus blocked;assert(!host_sense_weapon_action_status(&blocked));fire();assert(fires==2);app_blocked=0;
    assert(!memcmp(renderer,GPTR(0x50000),sizeof renderer));uint64_t scope_mode=scope_marker.mode_generation;
    scope_escape=1;sc.gpr[4]=0x24000;engine_dispatch(&sc,0x0050BEA0);
    assert(sc.pc==0xDEAD&&!host_sense_scope_frame(&scope_marker)&&!memcmp(renderer,GPTR(0x50000),sizeof renderer));scope_escape=0;
    S8(0x15320,1);sc.gpr[4]=0x24000;engine_dispatch(&sc,0x0050BEA0);assert(host_sense_scope_frame(&scope_marker)&&scope_marker.mode_generation!=scope_mode&&scope_marker.zoom_level==1);
    /* Original player+interface count is preserved, only player frusta
     * change, and a slow render is stamped at completion. */
    memset(GPTR(0x500AC),0xA7,0xAC);S16(0x500AC,UINT16_MAX);S8(0x500AE,1);
    unsigned char interface[0xAC];memcpy(interface,GPTR(0x500AC),sizeof interface);
    S32(0x24008,2);sc.gpr[4]=0x24000;scope_elapsed=UINT64_C(400000000);
    engine_dispatch(&sc,0x0050BEA0);assert(host_sense_scope_frame(&scope_marker));
    assert(!memcmp(renderer,GPTR(0x50000),sizeof renderer)&&!memcmp(interface,scope_interface,sizeof interface)&&!memcmp(interface,GPTR(0x500AC),sizeof interface));
    scope_elapsed=0;scope_change_epoch=1;sc.gpr[4]=0x24000;engine_dispatch(&sc,0x0050BEA0);
    assert(!host_sense_scope_frame(&scope_marker));scope_change_epoch=0;
    S8(0x500AE,0);unsigned before_draws=scope_draws;sc.gpr[4]=0x24000;engine_dispatch(&sc,0x0050BEA0);
    assert(scope_draws==before_draws&&!host_sense_scope_frame(&scope_marker));S8(0x500AE,1);
    S32(0x24008,1);
    S8(0x15320,0xFF);sense_action_update();assert(!scope_active&&!host_sense_scope_frame(&scope_marker));
    fresh=1;model();assert(sense_muzzle.valid);fire();assert(fires==3);near(observed_forward[0],1);

    /* Grenade throw through the original 0056E440 -> 004BEF80 -> 00568F80 ->
     * 004F7B70 instructions, the original detach 004F6610 (004F5DE0,
     * 004F6EC0, 004F8FE0, 004CB970, 004CC0D0) and delete 004F5BD0 (004F5AA0,
     * 004F59D0) and the original throw start 0056E080 (004C2F30, 004F6EC0,
     * 004018E0, 005704D0, 004726F0). Stubbed boundaries: creation 0056E280
     * (its verified debit/attach/state effects are reproduced), collision
     * 00505880 (deterministic hit or clear), and leaf functions 004F8310,
     * 004F5C30, 00552020, 004F9A20, 004F9B00, 004F05E0, 004EC940, 004E9D40,
     * 00565F90, 00492730, 004C4B50, 004507A0. 0056E080 is entered through
     * ENGINE_DIRECT/engine_hooks.h. */
    const float level_f[3]={1,0,0},level_u[3]={0,0,1};
    const float cy=cosf(.35f),sy=sinf(.35f),cp=cosf(.5f),sp=sinf(.5f);
    const float pitch_f[3]={cp*cy,cp*sy,sp},pitch_u[3]={-sp*cy,-sp*sy,cp}; /* yawed and pitched panorama camera */
    const float turned_f[3]={0,1,0},stock=grenade_stock/30.f;
    float hand[3]={.45f,.3f,-.25f},measured[3]={4,1,1.5f},position[3],velocity[3],expected_origin[3],expected_velocity[3],offset[3];
    float fast[3]={20,20,12}; /* 30.66 m/s */
    float measured_dir[3];memcpy(measured_dir,measured,12);sense_normalize(measured_dir);
    const float measured_strength=sqrtf(sense_dot(measured,measured))/5.f;
    /* 1. Hand differs from head and character aim; the unit walks and the
     * camera turns/pitches between sample, acceptance, creation and keyframe.
     * Clear path: final origin = frozen hand; final velocity = measured
     * direction at (|v|/5 m/s) of the original stock speed. The original
     * detach unlinked the grenade from the unit. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(pitch_f,pitch_u);
    uint64_t sampled=now+5000000;
    now+=8000000;grenade_unit(11,20.5f,30);grenade_render(turned_f,level_u);
    now+=8000000;grenade_unit(11.4f,20.7f,30);grenade_render(turned_f,level_u);
    grenade_publish(1,hand,measured,now+2000000-sampled);assert(grenade_accept()==1&&grenade_accepts==1&&sense_grenade_release.bound&&!sense_grenade_release.poisoned);
    { /* The binding's mount generation comes from the compiled gate. */
#if GRENADE_PRODUCER_GATE
      HaloSenseMountIdentity id;assert(host_sense_mount_identity(&id)&&id.mode==HALO_SENSE_MOUNT_ON_FOOT&&sense_grenade_release.mount==id.generation&&grenade_entry_publications>0);
#else
      assert(sense_grenade_release.mount==sense_grenade_mount_generation&&!grenade_entry_publications);
#endif
    }
    now+=33000000;grenade_unit(11.6f,20.8f,30);grenade_render(turned_f,level_u);
    grenade_spawn();assert(grenade_creates==1&&G8(0x1531E)==1&&G8(0x1528D)==2&&sense_grenade_release.armed&&G32(0x15118)==0x45670002u);
    grenade_basis(pitch_f,pitch_u,hand,offset);
    for(unsigned k=0;k<3;k++)expected_origin[k]=(k==0?10.f:k==1?20.f:30.62f)+offset[k]/3.048f;
    grenade_basis(pitch_f,pitch_u,measured,expected_velocity); /* raw m/s in world axes */
    for(unsigned k=0;k<3;k++)near(sense_grenade_release.velocity[k],expected_velocity[k]/3.048f/30.f); /* raw m/s -> units/tick */
    near(sense_grenade_release.strength,measured_strength);
    now+=30000000;grenade_unit(12,21,30);grenade_render(turned_f,level_u);
    float before_aim[3];memcpy(before_aim,GPTR(0x1523C),12);
    grenade_release(0);grenade_final(position,velocity);
    assert(grenades==1&&grenade_sweeps==1&&!grenade_deletes&&grenade_creates==1&&G8(0x1531E)==1);
    assert(G32(0x15118)==UINT32_MAX&&G32(0x1711C)==UINT32_MAX&&G32(0x17114)==UINT32_MAX&&G8(0x17120)==0xFF); /* original unlink */
    float frame_dir[3];memcpy(frame_dir,expected_velocity,12);sense_normalize(frame_dir);
    for(unsigned k=0;k<3;k++){near(position[k],expected_origin[k]);near(velocity[k],frame_dir[k]*measured_strength*stock);}
    near(sweep_origin[0],12);near(sweep_origin[1],21);near(sweep_origin[2],30.62f); /* original unit eye at the keyframe */
    for(unsigned k=0;k<3;k++)near(sweep_delta[k],expected_origin[k]-sweep_origin[k]);
    assert(G8(0x1528D)==3&&G32(0x15294)==UINT32_MAX&&!memcmp(before_aim,GPTR(0x1523C),12)&&!sense_grenade_release.bound);
    float spin[3];memcpy(spin,GPTR(0x1708C),12);assert(spin[2]>0); /* original spin derived from the injected delta */
    assert(sense_dot(velocity,before_aim)/grenade_length(velocity)<.9f);
    float later[3];grenade_basis(turned_f,level_u,hand,later);
    float drift[3]={12+later[0]/3.048f-expected_origin[0],21+later[1]/3.048f-expected_origin[1],30.62f+later[2]/3.048f-expected_origin[2]};
    assert(grenade_length(drift)>1.f);
    printf("grenade mapping: origin %.5f,%.5f,%.5f velocity %.6f,%.6f,%.6f units/tick (measured %.3f m/s -> %.1f%% of stock); later-camera drift avoided %.4f units\n",
        position[0],position[1],position[2],velocity[0],velocity[1],velocity[2],grenade_length(measured),100.f*grenade_length(velocity)/stock,grenade_length(drift));
    /* 1b. Clock anchor: the engine clock is read before the bridge measures
     * the sample age; time passing inside the bridge call (crossing the next
     * render) must not move the sample onto that later camera. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);uint64_t first=now;
    now+=3000000;grenade_render(turned_f,level_u);
    grenade_publish(1,hand,measured,0);grenade_sample_time=first+2000000;grenade_accept_delay=4000000;
    assert(grenade_accept()==1&&!sense_grenade_release.poisoned);grenade_accept_delay=0;
    grenade_basis(level_f,level_u,measured,expected_velocity);
    for(unsigned k=0;k<3;k++)near(sense_grenade_release.velocity[k],expected_velocity[k]/(3.048f*30.f));
    near(sense_grenade_release.origin[0],10+hand[0]/3.048f);near(sense_grenade_release.origin[1],20+hand[1]/3.048f);
    grenade_spawn();grenade_release(0);assert(grenade_sweeps==1);
    /* 2. Launch strength (user choice "Halo-like range, controlled by my
     * throw"): clamp(m/s / 5, 0, 1) of the verified stock 10 units/s. */
    const float table_mps[5]={1,3,5,8,30.66f},table_expect[5]={.0666667f,.2f,.3333333f,.3333333f,.3333333f};
    for(unsigned i=0;i<5;i++){
        float forward[3]={table_mps[i],0,0};if(i==4)memcpy(forward,fast,12);
        grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
        grenade_publish(1,hand,forward,1000000);grenade_create();grenade_release(0);grenade_final(position,velocity);
        near(grenade_length(velocity),table_expect[i]);
        float dir[3];memcpy(dir,forward,12);sense_normalize(dir);for(unsigned k=0;k<3;k++)near(velocity[k],dir[k]*table_expect[i]);
        printf("grenade strength: %.2f m/s measured -> %.7f units/tick = %.4f units/s (raw %.7f units/tick), %.1f%% of stock %.7f units/tick\n",
            table_mps[i],grenade_length(velocity),grenade_length(velocity)*30.f,table_mps[i]/3.048f/30.f,100.f*grenade_length(velocity)/stock,stock);
    }
    /* 3. A valid still hand drops from the hand: zero velocity even though the
     * original detach gave it the walking unit's velocity. */
    float still[3]={0,0,0};grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,still,1000000);grenade_create();grenade_release(0);grenade_final(position,velocity);
    for(unsigned k=0;k<3;k++){near(velocity[k],0);near(position[k],(k==0?10.f:k==1?20.f:30.62f)+hand[k]/3.048f);}
    /* 4. Interrupted release (arg1=1, progress 3/6): the original's lower
     * interrupted blend .5*stock + .5*random(.02,.0467) is the cap, scaled by
     * strength, along the measured direction. */
    float fast_dir[3];memcpy(fast_dir,fast,12);sense_normalize(fast_dir);
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,fast,1000000);grenade_create();S16(0x1528E,3);S16(0x15290,6);grenade_release(1);grenade_final(position,velocity);
    float blend=grenade_length(velocity);assert(blend>=.5f*stock+.5f*.02f-.0001f&&blend<=.5f*stock+.5f*.0467f+.0001f);
    for(unsigned k=0;k<3;k++)near(velocity[k]/blend,fast_dir[k]);
    float two[3]={2,0,0};grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,two,1000000);grenade_create();S16(0x1528E,3);S16(0x15290,6);grenade_release(1);grenade_final(position,velocity);
    assert(velocity[0]>=.4f*(.5f*stock+.5f*.02f)-.0001f&&velocity[0]<=.4f*(.5f*stock+.5f*.0467f)+.0001f);near(velocity[1],0);near(velocity[2],0);
    /* 5. Obstructed eye->hand path: the original final sweep clamps to its
     * collision point; velocity stays the frozen measured result. An invalid
     * location is deleted by the original (real 004F5BD0 chain). */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();grenade_sweep_hit=1;grenade_release(0);grenade_final(position,velocity);
    float frozen_hand[3]={10+hand[0]/3.048f,20+hand[1]/3.048f,30.62f+hand[2]/3.048f};
    for(unsigned k=0;k<3;k++){near(sweep_delta[k],frozen_hand[k]-sweep_origin[k]);near(position[k],sweep_origin[k]+sweep_delta[k]*.5f);
        near(velocity[k],measured_dir[k]*measured_strength*stock);}
    assert(!grenade_deletes);
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();S16(0x1709C,UINT16_MAX);grenade_release(0);
    assert(grenade_sweeps==1&&grenade_deletes==1&&(G8(0x1401A)&8)&&(G8(0x17010)&1)&&!grenade_cleanup_deletes);
    /* 6. The delivered pulse was unhonourable when the original accepted it:
     * creation cancels (state 3, nothing spent, no object); a later throw works. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_claim_result=-1;grenade_create();assert(!grenade_creates&&G8(0x1528D)==3&&G8(0x1531E)==2&&G32(0x15294)==UINT32_MAX);
    grenade_release(0);assert(grenades==1&&!grenade_sweeps);
    grenade_idle();now+=10000000;grenade_render(level_f,level_u);grenade_publish(1,hand,measured,1000000);grenade_create();
    assert(grenade_creates==1&&G8(0x1528D)==2);grenade_release(0);assert(grenade_sweeps==1&&G8(0x1531E)==1);
    /* 7. Unmappable at acceptance (no render at/before the sample, invalid
     * motion, out-of-range hand, newest render >250ms older), or no longer
     * honourable at creation (UI, death, controller handover, turret seat
     * cancel, new Sense session): creation cancels and spends nothing. */
    for(unsigned bad=0;bad<9;bad++){
        grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
        float far_hand[3]={4,0,0};
        grenade_publish(1,bad==2?far_hand:hand,measured,bad==0?UINT64_C(400000000):1000000);
        if(bad==1)grenade_claim.sample.motion_valid=0;
        if(bad==3){grenade_claim.age_ns=0;now+=UINT64_C(300000000);}
        assert(grenade_accept()==1&&sense_grenade_release.bound);
        if(bad==4)app_blocked=1;
        if(bad==5)putf(0x150E0,0);
        if(bad==6)active=0;
        if(bad==7){assert(!sense_grenade_release.poisoned);sense_grenade_cancel_for_mount();sense_grenade_cancel_for_mount();assert(sense_grenade_release.bound&&sense_grenade_release.poisoned);}
        if(bad==8)status_session=2;
        grenade_spawn();app_blocked=0;putf(0x150E0,1);active=1;status_session=1;
        assert(!grenade_creates&&G8(0x1528D)==3&&G8(0x1531E)==2&&G32(0x15294)==UINT32_MAX&&!sense_grenade_release.bound);
    }
    /* 8. Mount identity between sample and creation: seated at creation, a
     * render-observed seat cycle, death and revival, or a producer-observed
     * seat cycle (refreshed at the 0056E280 entry) all cancel, spending nothing. */
    for(unsigned change=0;change<4;change++){
        grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);uint64_t sample_at=now;
        if(change==1){S16(0x152F0,0);now+=8000000;grenade_render(level_f,level_u);S16(0x152F0,UINT16_MAX);now+=8000000;grenade_render(level_f,level_u);}
        if(change==2){putf(0x150E0,0);now+=8000000;grenade_render(level_f,level_u);putf(0x150E0,1);now+=8000000;grenade_render(level_f,level_u);}
        grenade_publish(1,hand,measured,now+2000000-sample_at);
        if(change==0){assert(grenade_accept()==1);S16(0x152F0,0);}
#if GRENADE_PRODUCER_GATE
        /* Real producer: the refresh at the 0056E280 entry sees the moved exit stamp. */
        HaloSenseMountIdentity before_cycle;assert(host_sense_mount_identity(&before_cycle));
        if(change==3){assert(grenade_accept()==1&&!sense_grenade_release.poisoned);mount_exit_stamp();}
#else
        if(change==3){assert(grenade_accept()==1&&!sense_grenade_release.poisoned);
            S16(0x152F0,0);now+=8000000;grenade_render(level_f,level_u);S16(0x152F0,UINT16_MAX);}
#endif
        grenade_create();
        assert(!grenade_creates&&G8(0x1528D)==3&&G8(0x1531E)==2);S16(0x152F0,UINT16_MAX);
#if GRENADE_PRODUCER_GATE
        if(change==3){HaloSenseMountIdentity after_cycle;assert(host_sense_mount_identity(&after_cycle)&&after_cycle.generation>before_cycle.generation);}
#endif
    }
    /* 9. After creation (one debit, attached): seat, death, controller
     * handover, turret seat cancel (twice), new Sense session or a
     * producer-observed seat cycle cancels with the original detach and
     * delete: unlinked, delete-marked, released state, no sweep/flight, the
     * single debit stands. A repeated keyframe or cancel does nothing more,
     * and the next ordinary throw is an untouched new original transaction. */
    for(unsigned change=0;change<6;change++){
#if !GRENADE_PRODUCER_GATE
        if(change==5)continue; /* the local gate cannot see a cycle completed between observations */
#endif
        grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
        grenade_publish(1,hand,measured,1000000);grenade_create();assert(grenade_creates==1&&G8(0x1531E)==1&&sense_grenade_release.armed);
        if(change==0)S16(0x152F0,0);
        if(change==1)putf(0x150E0,0);
        if(change==2)active=0;
        if(change==3){sense_grenade_cancel_for_mount();sense_grenade_cancel_for_mount();}
        if(change==4)status_session=2;
#if GRENADE_PRODUCER_GATE
        if(change==5)mount_exit_stamp(); /* seen by the producer refresh at the 0056E440 entry */
#endif
        grenade_release(0);
        assert(!grenades&&!grenade_sweeps&&grenade_cleanup_detaches==1&&grenade_cleanup_deletes==1&&grenade_deletes==1); /* original release never entered */
        assert(G32(0x15118)==UINT32_MAX&&G32(0x1711C)==UINT32_MAX&&(G8(0x1401A)&8)&&(G8(0x17010)&1)&&sense_object(0x45670002u,0x1F4,5)); /* deferred free */
        assert(G8(0x1528D)==3&&G32(0x15294)==UINT32_MAX&&G8(0x1531E)==1&&!memcmp(GPTR(0x15068),GPTR(0x17068),12)&&!sense_grenade_release.bound); /* only the detach's copied unit velocity: no launch */
        grenade_release(0);sense_grenade_cancel_for_mount();
        assert(grenades==1&&!grenade_sweeps&&grenade_deletes==1&&grenade_cleanup_deletes==1); /* original sees state 3 */
        S16(0x152F0,UINT16_MAX);putf(0x150E0,1);active=1;status_session=1;
        grenade_idle();float pad_aim[3]={0,1,0};memcpy(GPTR(0x1523C),pad_aim,12);
        grenade_create();assert(grenade_creates==2&&grenade_current==0x56780003u&&!sense_grenade_release.bound&&G8(0x1531E)==0);
        grenade_release(0);grenade_final(position,velocity);near(velocity[0],0);near(velocity[1],stock);near(velocity[2],0);
        assert(grenade_sweeps==1&&grenade_deletes==1&&(G8(0x1401A)&8));
    }
    /* 9b. Already delete-marked by the original (e.g. the unit's own delete
     * recursed into it): the cancel clears the unit's pointer/state without a
     * second cleanup. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();S8(0x1401A,(uint8_t)(G8(0x1401A)|8));
    grenade_release(0);assert(!grenades&&!grenade_cleanup_detaches&&!grenade_cleanup_deletes&&G32(0x15294)==UINT32_MAX&&G8(0x1528D)==3);
    /* 10. Fail closed: a non-finite frozen strength cannot reach the original
     * 004BEF80: no velocity is added (never stock power) and the original
     * delete removes it after the original release. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();sense_grenade_release.strength=NAN;
    grenade_release(0);grenade_final(position,velocity);
    assert(grenades==1&&grenade_cleanup_deletes==1&&grenade_deletes==1&&(G8(0x1401A)&8)&&G8(0x1528D)==3&&!sense_grenade_release.bound);
    near(velocity[0],.05f);near(velocity[1],0);near(velocity[2],0); /* only the original detach's carried velocity */
    /* 11. Ordinary input binds nothing: original creation and release at the
     * character aim at stock speed; a repeated keyframe launches nothing. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    float pad_aim[3]={0,1,0};memcpy(GPTR(0x1523C),pad_aim,12);
    grenade_create();assert(grenade_accepts==1&&grenade_creates==1&&!sense_grenade_release.bound);grenade_release(0);grenade_final(position,velocity);
    near(velocity[0],0);near(velocity[1],stock);near(velocity[2],0);
    for(unsigned k=0;k<3;k++)near(sweep_delta[k],position[k]-sweep_origin[k]);
    assert(fabsf(position[0]-frozen_hand[0])>.05f);
    grenade_release(0);assert(grenade_sweeps==1&&G8(0x1531E)==1);
    /* 12. The original's gate rejects a Sense pulse (no grenades): the bridge
     * is not consulted and nothing binds; the following ordinary throw is
     * untouched (the bridge side of this handover: test_sense_throw.c). */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);S8(0x1531E,0);
    grenade_publish(1,hand,measured,1000000);assert(grenade_accept()==0&&!grenade_accepts&&!sense_grenade_release.bound&&G8(0x1528D)==0);
    grenade_claim_result=0;S8(0x1531E,2);memcpy(GPTR(0x1523C),pad_aim,12);
    grenade_create();assert(grenade_accepts==1&&grenade_creates==1&&!sense_grenade_release.bound);
    grenade_release(0);grenade_final(position,velocity);near(velocity[1],stock);
    /* 13. Repeated callbacks keep the first accepted token: a duplicate start
     * during the bound throw is refused before the original runs; a second creation call while
     * created is refused (no second debit, first grenade kept); an uncreated
     * binding is superseded by a newer acceptance; a created one is never
     * stranded (original cleanup, no throw). */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);assert(grenade_accept()==1);uint64_t first_epoch=sense_grenade_release.epoch;
    grenade_publish(1,hand,still,1000000);assert(grenade_accept()==0&&grenade_accepts==1&&sense_grenade_release.epoch==first_epoch);
    grenade_spawn();assert(grenade_creates==1&&sense_grenade_release.armed);
    grenade_spawn();assert(grenade_creates==1&&G8(0x1531E)==1&&G32(0x15294)==0x45670002u&&sense_grenade_release.armed&&sense_grenade_release.epoch==first_epoch);
    grenade_claim_result=0;grenade_release(0);grenade_final(position,velocity);
    for(unsigned k=0;k<3;k++)near(velocity[k],measured_dir[k]*measured_strength*stock); /* still the first token */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);assert(grenade_accept()==1&&sense_grenade_release.bound);
    grenade_idle();grenade_claim_result=0;assert(grenade_accept()==1&&!sense_grenade_release.bound); /* ordinary acceptance supersedes an uncreated binding */
    grenade_spawn();assert(grenade_creates==1&&!sense_grenade_release.armed);
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();assert(sense_grenade_release.armed);
    grenade_idle();grenade_claim_result=0;assert(grenade_accept()==1&&!sense_grenade_release.bound);
    assert(grenade_cleanup_deletes==1&&(G8(0x1401A)&8)&&G32(0x15118)==UINT32_MAX&&G32(0x15294)==UINT32_MAX&&G8(0x1531E)==1);
    /* 14. Ownership never transfers to another projectile: if the unit now
     * references a different grenade, the binding ends and that grenade is the
     * original's; the bound one is not touched. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);grenade_create();assert(sense_grenade_release.armed);
    S16(0x1402A,0x1F4);S16(0x180B4,5);S32(0x15294,0x56780003u);
    grenade_release(0);assert(grenades==1&&!sense_grenade_release.bound&&!grenade_cleanup_deletes&&!(G8(0x1401A)&8));
    { HaloSenseMountIdentity id;assert(host_sense_mount_identity(&id)&&id.generation>1);
#if GRENADE_PRODUCER_GATE
      /* Admission refreshed the real producer at grenade entries (outside every original action). */
      assert(grenade_entry_publications>0);
      printf("grenade mount gate: real sense_mount.inc producer (grenade-entry refresh publications %u of %u, identity generation %llu)\n",
          grenade_entry_publications,mount_publications,(unsigned long long)id.generation);
#else
      /* Standalone gate: grenade admission never refreshes the producer; the
       * separately compiled turret producer still publishes at its own hooks. */
      assert(!grenade_entry_publications&&mount_publications>0);
      printf("grenade mount gate: standalone parent/seat fallback (local generation %llu; turret producer still compiled, %u publications, none at grenade entries)\n",
          (unsigned long long)sense_grenade_mount_generation,mount_publications);
#endif
    }
    /* 15. The real 0056E080: acceptance and rejection are its own; the
     * one-start-per-throw rule is the retail caller's (005625B0 switch at
     * 005630FE, case 0 at 00563116), not this function's. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    assert(grenade_accept()==1&&G8(0x1528D)==1&&G16(0x1528E)==0&&G16(0x15290)==6&&G8(0x152A3)==0x21&&grenade_animation_starts==1&&grenade_fp_starts==1);
    S8(0x152A3,0xFF);assert(G8(0x1528D)==1&&grenade_accept()==1&&grenade_animation_starts==2); /* state 1 left as is: no state test inside the function, an unbound direct re-entry restarts it (+0x2A3 reset: its 0x17..0x29 table branch is not modelled) */
    grenade_idle();S8(0x16238,5);assert(grenade_accept()==0&&G8(0x1528D)==0&&grenade_animation_starts==2); /* weapon busy (real 004C2F30) */
    S8(0x16238,0);S8(0x1531E,0);assert(grenade_accept()==0&&G8(0x1528D)==0); /* none left */
    S8(0x1531E,2);S8(0x1531C,0xFF);assert(grenade_accept()==0&&G8(0x1528D)==0);S8(0x1531C,0); /* no selected type */
    grenade_animation_refuses=1;assert(grenade_accept()==0&&G8(0x1528D)==0);grenade_animation_refuses=0; /* animation refused */
    assert(grenade_accepts==2&&!sense_grenade_release.bound); /* only the two acceptances consulted the bridge; rejections never do */
    /* Owned binding: a duplicate start in state 1 or 2 is refused before the
     * original (no restart, no new token); first epoch, projectile and debit stand. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);
    grenade_publish(1,hand,measured,1000000);assert(grenade_accept()==1);first_epoch=sense_grenade_release.epoch;
    grenade_publish(1,hand,fast,1000000);assert(grenade_accept()==0&&grenade_accepts==1&&grenade_animation_starts==1&&G8(0x1528D)==1&&sense_grenade_release.epoch==first_epoch);
    grenade_spawn();assert(sense_grenade_release.armed&&G8(0x1528D)==2);
    assert(grenade_accept()==0&&grenade_accepts==1&&grenade_animation_starts==1&&G8(0x1528D)==2&&G32(0x15294)==0x45670002u&&G8(0x1531E)==1);
    grenade_claim_result=0;grenade_release(0);grenade_final(position,velocity);
    for(unsigned k=0;k<3;k++)near(velocity[k],measured_dir[k]*measured_strength*stock);
    /* Both grenade types: the plasma count is debited once, frag untouched. */
    grenade_world();now+=10000000;grenade_unit(10,20,30);grenade_render(level_f,level_u);S8(0x1531C,1);
    grenade_publish(1,hand,measured,1000000);grenade_create();assert(G8(0x1531F)==1&&G8(0x1531E)==2&&sense_grenade_release.armed);
    grenade_release(0);grenade_final(position,velocity);for(unsigned k=0;k<3;k++)near(velocity[k],measured_dir[k]*measured_strength*stock);
    native_action_checks();grenade_hold_checks();grenade_hold_boundary_checks();dual_native_and_hold_checks();
    free(engine_flat_base);puts("PASS: original skeleton/ray instructions; tracked/support/dual regression; complete melee rig retained and head-oriented; scoped weapon ray and both original narrow-FOV frusta independent of head turn; grenade bound at original acceptance, frozen at the sample render (origin/basis/measured velocity, engine-clock anchor) through original 0056E440/004BEF80/004F7B70, strength clamp(m/s/5,0,1) of the original desired speed, cancellation before creation (nothing spent) and after creation through the original detach/delete, fail-closed injection, repeated callbacks keep the first token, ordinary input unchanged; interface/arguments restored; slow render and epoch loss; reticle capture failures never leak centered HUD.");
}
