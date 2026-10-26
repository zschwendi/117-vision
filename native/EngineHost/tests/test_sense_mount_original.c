/* Seat entry/exit through the original retail instructions: seat entry
 * 00566970, the biped exit request 0056C470 (exit animation) and completion
 * 0056C640, the unit update 005590A0 with its inlined ejection (0055993F),
 * held-weapon selection 0056D6A0/0056DBA0/0056D6E0/0056DA00, occupant scan
 * 0056CE30, first-person rig 004D6880 and shot ray 005658F0. Leaf object
 * graph, animation and effect routines are stubs (listed below); attach and
 * detach model only the parent field those leaves own. The fixture never
 * writes a seat, parent or held slot itself. No device, game process, save,
 * inventory or damage is fabricated. */
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
#include "sub_00566970.c"
#include "sub_005590A0.c"
#include "sub_0056D6A0.c"
#include "sub_0056DBA0.c"
#include "sub_0056D6E0.c"
#include "sub_0056DA00.c"
#include "sub_0056CE30.c"
#include "sub_0056C470.c"
#include "sub_0056C640.c"
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
#include "../sense_physical.h"
uint8_t *engine_flat_base;uint32_t engine_trace_lo,engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu){(void)cpu;}
void host_log(const char *f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a);}
static int active=1,fresh=1,panorama_nested,panorama_fp_cache_nested,app_blocked,scope_active;
static uint64_t input_generation=1;
bool hostgc_poll(HostGCSnapshot *out){memset(out,0,sizeof *out);return false;}
static uint64_t now=1000000000;
uint64_t host_monotonic_ns(void){return now;}
int halo_sense_active(void){return active;}
int halo_sense_requested(void){return 1;}
int halo_sense_dual_requested(void){return 0;}
void halo_sense_set_scope_active(int value){scope_active=value;}
/* Controller connection 1, as every published frame/head pose here (controller_generation 1). */
void halo_sense_status(HaloSenseStatus *s){memset(s,0,sizeof *s);s->session=1;s->generation=input_generation;s->active=active;s->requested=1;s->gameplay_supported=1;
    s->controller_generation=s->published_controller_generation=1;}
int halo_sense_head_read(HaloSenseHeadPose *h){memset(h,0,sizeof *h);h->forward[0]=1;h->up[2]=1;h->generation=h->controller_generation=1;return 1;}
int halo_sense_app_input_blocked(void){return app_blocked;}
int halo_sense_scope_input(HaloSenseScopeInput *i){memset(i,0,sizeof *i);return 0;}
int halo_sense_dual_input(HaloSenseDualInput *p){memset(p,0,sizeof *p);p->session=1;p->generation=input_generation;p->fresh=fresh;p->armed[0]=p->armed[1]=1;return !app_blocked&&fresh;}
int host_sense_reticle_art_begin(unsigned h,uint32_t w,const HaloSenseReticleFrame*t,float x,float y){(void)h;(void)w;(void)t;(void)x;(void)y;return 0;}
int host_sense_reticle_art_end(int complete){(void)complete;return 1;}
static HaloSenseReticleFrame reticles;static unsigned reticle_clears;
void halo_sense_reticle_publish(const HaloSenseReticleFrame *p){if(p)reticles=*p;else{memset(&reticles,0,sizeof reticles);reticle_clears++;}}
int halo_sense_reticle_read(HaloSenseReticleFrame *p){*p=reticles;return fresh&&(p->hands[0].valid||p->hands[1].valid);}
void halo_sense_observe_controller(const HostGCSnapshot *p){(void)p;}
static int gameplay_supported=-1;
void halo_sense_set_gameplay_supported(int s){gameplay_supported=s;}
int halo_sense_gameplay_supported(void){return gameplay_supported>0;}
/* Input side of sense_mount.h: every identity change reaches it. */
static int native_controls=-1;static uint64_t native_generation;static unsigned native_publications,input_changes;
void halo_sense_set_native_controls(int n,uint64_t g){
    if(n!=native_controls||g!=native_generation){input_changes++;input_generation++;}
    native_controls=n;native_generation=g;native_publications++;
}
int halo_sense_weapon_pose(unsigned hand,float p[3],float f[3],float u[3]){
    if(!fresh)return 0;float pos[3]={.5f,hand==1?-.25f:.25f,-.3f},dir[3]={1,0,0},up[3]={0,0,1};
    memcpy(p,pos,12);memcpy(f,dir,12);memcpy(u,up,12);return 1;
}
int halo_sense_read(HaloSenseFrame *frame){memset(frame,0,sizeof *frame);if(!fresh||app_blocked)return 0;
    frame->generation=frame->controller_generation=1;
    frame->left.valid=halo_sense_weapon_pose(0,frame->left.position,frame->left.forward,frame->left.up);
    frame->right.valid=halo_sense_weapon_pose(1,frame->right.position,frame->right.forward,frame->right.up);return 1;}
/* Bridge acceptance belongs to the grenade fixture (test_sense_weapon_original.c);
 * no throw start is dispatched here, so it is never consulted. */
int halo_sense_grenade_accept(uint32_t clock,uint32_t tick,HaloSenseGrenadeRelease *out){(void)clock;(void)tick;(void)out;assert(!"acceptance is not exercised by the mount fixture");return 0;}
int halo_sense_grenade_hold(HaloSenseGrenadeHold *out){if(out)memset(out,0,sizeof *out);return 0;} /* no armed hold in this fixture */
uint32_t guest_alloc(uint32_t size){assert(size==0x100);return 0x60000;}
uint32_t host_call_guest(EngineCPU *cpu,uint32_t fn,int n,const uint32_t *args,int pops){
    (void)cpu;assert(fn==0x00505880&&n==5&&pops==0&&args[0]==0x1000E9);return 0; /* clear line of fire */
}
static void ret_to_caller(EngineCPU *cpu){cpu->pc=engine_pop(cpu,4);}
static void sub_0056E280_unrouted(EngineCPU *cpu){(void)cpu;assert(!"0056E280 is not routed through engine_hooks.h");}
static unsigned physical_reload_cancels;
int halo_sense_physical_enqueue(uint64_t s,uint64_t g,uint64_t q,int a,uint32_t o,uint32_t t){(void)s;(void)g;(void)q;(void)a;(void)o;(void)t;return 1;}
int halo_sense_physical_pending(int a){(void)a;return 0;}
void halo_sense_physical_cancel(int a){if(a==HALO_SENSE_GESTURE_PHYSICAL_RELOAD)physical_reload_cancels++;}
void halo_sense_physical_set_delivery_check(int (*c)(int,uint32_t,uint32_t)){(void)c;}
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
int host_sense_body_active(void){return 0;}
#include "../sense_weapon_hooks.inc"
/* The shared seat cancellation must come from the production grenade module
 * through the real producer (sense_mount.inc); never a fixture definition or
 * the standalone grenade mount gate. */
#ifndef HALO_SENSE_GRENADE_CANCEL_MOUNT
#error "production sense_grenade.inc must provide HALO_SENSE_GRENADE_CANCEL_MOUNT"
#endif
#ifdef HALO_SENSE_GRENADE_LOCAL_MOUNT
#error "the mount fixture exercises the producer-bound grenade gate"
#endif
#include "../dual_wield.inc"

/* Object table (0x8603B0): 0 biped 23450000, 1 rifle 34560001, 3 turret
 * vehicle 56780003, 4 turret weapon 67890004, 5 respawned biped 789A0005. */
#define UNIT 0x15000u
#define RIFLE 0x16000u
#define VEHICLE 0x18000u
#define TURRET_GUN 0x19000u
#define UNIT_ID 0x23450000u
#define RIFLE_ID 0x34560001u
#define VEHICLE_ID 0x56780003u
#define TURRET_GUN_ID 0x67890004u
#define RESPAWN 0x1A000u
#define RESPAWN_ID 0x789A0005u
static uint32_t body_of(uint32_t datum){
    uint32_t e=G32(G32(0x008603B0u)+0x34)+(datum&65535u)*12u;
    return datum!=UINT32_MAX&&(datum&65535u)<8&&G16(e)==(datum>>16)?G32(e+8):0;
}
/* Leaf routines below the original seat paths. Each is cdecl (checked:
 * every one returns with a bare ret). */
static int seat_keeps_weapon,exit_blocked;static unsigned attaches,detaches,unexpected;
static float seat_eye[3]={10,25,31.2f};
static void leaf(EngineCPU *cpu,uint32_t address){
    switch(address){
    case 0x00566840u: cpu->gpr[0]=1;break;                   /* seat enterable */
    case 0x004C28F0u: cpu->gpr[0]=1;break;                   /* held weapon may be put away */
    case 0x005651E0u: {                                      /* animation lookup: the turret seat has no weapon animations */
        uint32_t unit=body_of(UNIT_ID);
        cpu->gpr[0]=unit&&G16(unit+0x2F0)!=UINT16_MAX?(uint32_t)seat_keeps_weapon:1;break;}
    case 0x004F6610u: { uint32_t b=body_of(ARG(0));assert(b);S32(b+0x11C,UINT32_MAX);detaches++;cpu->gpr[0]=0;break;}
    case 0x004F6180u: { uint32_t b=body_of(cpu->gpr[6]);assert(b);S32(b+0x11C,ARG(0));attaches++;break;}
    case 0x004F6EC0u: cpu->gpr[0]=body_of(cpu->gpr[1]);break; /* object get-and-verify */
    case 0x00568F80u: memcpy(GPTR(cpu->gpr[7]),seat_eye,12);break; /* original unit eye: the seat camera marker */
    case 0x004F6AA0u: { float v[3]={2,3,4};memcpy(GPTR(cpu->gpr[6]),v,12);break;}
    case 0x00558EB0u: case 0x0055E2D0u: case 0x004F6900u: case 0x004F6080u: case 0x004CC010u: case 0x004F51C0u:
    case 0x0056DFD0u: case 0x005659C0u: case 0x004F82B0u: case 0x004F9A20u: case 0x004BCFC0u: case 0x004F5DE0u:
    case 0x004F50F0u: case 0x004C24D0u: case 0x0056C2F0u: case 0x004F5C30u: case 0x004C2840u: case 0x00565420u:
    case 0x0055ADD0u: case 0x00566910u: case 0x004F6B70u: case 0x004D6280u: case 0x004F6C10u: case 0x0042D340u:
    case 0x00565F90u: case 0x004D0680u: case 0x0042C370u: case 0x0056C370u: case 0x0056EBD0u: case 0x0056AB10u:
        cpu->gpr[0]=0;break;
    case 0x00565C60u: cpu->gpr[0]=exit_blocked;break;        /* 0056C4FE: unit+0x298 state check; nonzero refuses the exit request */
    default: fprintf(stderr,"unlisted leaf %08x from %08x\n",address,G32(cpu->gpr[4]));unexpected++;assert(!"unexpected leaf");
    }
    ret_to_caller(cpu);
}
#define LEAF(a) void sub_##a(EngineCPU *cpu){leaf(cpu,0x##a##u);}
LEAF(00401990) LEAF(0042C370) LEAF(0042D340) LEAF(00492730) LEAF(004BCFC0) LEAF(004C24D0) LEAF(004C2840) LEAF(004C28F0)
LEAF(004C2EE0) LEAF(004C2F80) LEAF(004C4B50) LEAF(004CC010) LEAF(004D0680) LEAF(004D6280) LEAF(004E6F20) LEAF(004F50F0)
LEAF(004F51C0) LEAF(004F5C30) LEAF(004F5DE0) LEAF(004F6080) LEAF(004F6180) LEAF(004F6610) LEAF(004F6900) LEAF(004F6AA0)
LEAF(004F6B70) LEAF(004F6C10) LEAF(004F6EC0) LEAF(004F82B0) LEAF(004F9A20) LEAF(00558EB0) LEAF(0055ADD0) LEAF(0055B7C0)
LEAF(0055CFD0) LEAF(0055E190) LEAF(0055E2D0) LEAF(0055E4A0) LEAF(0055E840) LEAF(0055E940) LEAF(0055EB90) LEAF(0055EC20)
LEAF(0055ECF0) LEAF(00560410) LEAF(00560800) LEAF(005651E0) LEAF(00565420) LEAF(005659C0) LEAF(00565C60) LEAF(00565E00)
LEAF(00565F90) LEAF(00566840) LEAF(00566910) LEAF(00568F80) LEAF(00569970) LEAF(0056AB10) LEAF(0056C2F0) LEAF(0056C370)
LEAF(0056DFD0) LEAF(0056EBD0) LEAF(0056F550)

/* Simulated retail 004C3F10 body around the original 005658F0 and the
 * final pre-spread boundary 004593B0 (as test_sense_weapon_original.c). */
static unsigned fires,rays,crosshairs,entries,grenade_creations;static int prespread_override,escape_entry;
static float shot_origin[3],shot_forward[3],muzzle_in[3];static uint32_t shot_unit=UNIT_ID;
void engine_dispatch(EngineCPU *cpu,uint32_t address){
    cpu->pc=address;if(host_sense_weapon_dispatch(cpu,address))return;
    switch(address){
    case 0x00566970u: entries++;if(escape_entry){cpu->pc=0xDEADu;return;} sub_00566970(cpu);return;
    case 0x005590A0u: sub_005590A0(cpu);return;
    case 0x0056C470u: sub_0056C470(cpu);return;
    case 0x0056C640u: sub_0056C640(cpu);return;
    case 0x004D6880u: sub_004D6880(cpu);return;
    case 0x004CB970u: sub_004CB970(cpu);return;
    case 0x004CBAD0u: sub_004CBAD0(cpu);return;
    case 0x004CC0D0u: sub_004CC0D0(cpu);return;
    case 0x004F5350u: sub_004F5350(cpu);return;
    case 0x005658F0u: sub_005658F0(cpu);return;
    case 0x004593B0u: cpu->gpr[0]=0;ret_to_caller(cpu);return; /* original continues: no tracked replacement */
    case 0x0056E280u: grenade_creations++;ret_to_caller(cpu);return;  /* original grenade creation reached (no state 2: binding ends) */
    case 0x004ACAD0u: crosshairs++;ret_to_caller(cpu);return;  /* original crosshair drawn */
    case 0x004C3F10u: {
        fires++;EngineCPU child=*cpu;child.gpr[4]=0x23000;S32(0x23000,0x004C4EE2);S32(0x23004,shot_unit);
        S32(0x23008,0x23200);S32(0x2300C,1);S32(0x23010,1);child.gpr[0]=0x23100;child.gpr[3]=0x23120;
        memcpy(GPTR(0x23100),muzzle_in,12);float garbage[3]={77,88,99};memcpy(GPTR(0x23120),garbage,12);
        engine_dispatch(&child,0x005658F0);assert(child.pc==0x004C4EE2&&G32(0x2300C)==1&&G32(0x23010)==1);
        child.gpr[4]=0x23400;S32(0x23400,0x004C500F);S32(0x23404,0x23100);S32(0x23408,0x23120);
        engine_dispatch(&child,0x004593B0u);assert(child.pc==0x004C500F);prespread_override=child.gpr[0]==UINT32_MAX;
        memcpy(shot_origin,GPTR(0x23100),12);memcpy(shot_forward,GPTR(0x23120),12);rays++;
        ret_to_caller(cpu);return;}
    default: fprintf(stderr,"unexpected dispatch %08x\n",address);assert(!"unexpected dispatch");
    }
}
static void putf(uint32_t p,float f){memcpy(GPTR(p),&f,4);}
static void near(float a,float b){assert(fabsf(a-b)<.00005f);}
static void object(unsigned index,uint16_t salt,uint32_t body,uint16_t size,uint16_t kind,uint32_t tag){
    uint32_t e=0x14000+index*12;S16(e,salt);S16(e+6,size);S32(e+8,body);S16(body+0xB4,kind);S32(body,tag);
    S32(body+0x11C,UINT32_MAX);S32(body+0x118,UINT32_MAX);S32(body+0x114,UINT32_MAX);
}
static void tag(unsigned index,uint32_t data){S32(0x30000+index*32+0xC,0x45670000u+index);S32(0x30000+index*32+0x14,data);}
static void biped(uint32_t b){
    putf(b+0xE0,1);S16(b+0x2F0,0xFFFF);S16(b+0x2F2,0);S16(b+0x2F4,0);S32(b+0x2F8,RIFLE_ID);
    S32(b+0x2FC,UINT32_MAX);S32(b+0x300,UINT32_MAX);S32(b+0x304,UINT32_MAX);S32(b+0x1F4,UINT32_MAX);
    S8(b+0x320,0xFF);S8(b+0x321,0xFF);S16(b+0x1F2,0x400);putf(b+0x400,1);putf(b+0x404,1);putf(b+0x414,1);putf(b+0x424,1);
    float origin[3]={10,20,30};memcpy(GPTR(b+0x5C),origin,12);
}
static void setup(void){
    memset(engine_flat_base,0,0x900000);
    S32(0x696664,0x004CC0D0);putf(0x672AC4,1);putf(0x672BE4,2);
    S32(0x6F1D6C,0x64000);S32(0x6400C,500);                     /* game time, tick 500 */
    S32(0x87A478,0x10000);S32(0x87A480,0x11000);S32(0x10004,0x12340000);
    S16(0x11020,1);S16(0x11022,0x200);S32(0x11034,0x12000);S16(0x12000,0x1234);S32(0x12034,UNIT_ID);
    S32(0x8603B0,0x13000);S16(0x13020,6);S16(0x13022,12);S32(0x13034,0x14000);
    S32(0x87BC14,0x30000);
    tag(0,0x31000);tag(1,0x33000);tag(2,0x34000);tag(3,0x3A000);tag(4,0x3C000);tag(5,0x3D000);tag(6,0x3F000);
    object(0,0x2345,UNIT,0x550,0,0x45670004);biped(UNIT);
    object(1,0x3456,RIFLE,0x340,2,0x45670001);S32(RIFLE+0x11C,UNIT_ID);
    object(3,0x5678,VEHICLE,0x600,1,0x45670003);
    object(4,0x6789,TURRET_GUN,0x340,2,0x45670001);S32(TURRET_GUN+0x11C,VEHICLE_ID);
    /* Unit tags: biped 4 and vehicle 3 share graph 5 (empty mode block) and model 6. */
    S32(0x3C044,0x45670005);S32(0x3C034,0x45670006);S32(0x3A044,0x45670005);S32(0x3A034,0x45670006);
    S32(0x3D010,0x3E000);S32(0x3F0BC,0x3F100);
    S32(0x3E040,9);S32(0x3E044,0x3E100);S16(0x3E10E,UINT16_MAX);S16(0x3E110,3); /* mode 0: an exit animation, no entry animation */
    S32(0x3A2E4,1);S32(0x3A2E8,0x3B000);S32(0x3B000,8);memcpy(GPTR(0x3B004),"gunner",7);memcpy(GPTR(0x3B024),"gunner",7);
    /* First-person rig of the rifle, as test_sense_weapon_original.c. */
    S32(0x31068,5);S32(0x3106C,0x32000);S32(0x33468,0x45670002);S32(0x340AC,1);S32(0x340B0,0x35000);
    memcpy(GPTR(0x35000),"primary trigger",16);S32(0x35034,1);S32(0x35038,0x36000);putf(0x36004,.2f);S8(0x36002,2);putf(0x3601C,1);S32(0x340B8,5);S32(0x340BC,0x37000);
    const char *names[]={"root","r wrist","gun","barrel","l wrist"};int parent[]={-1,0,1,2,0},child[]={1,2,3,-1,-1},sibling[]={4,-1,-1,-1,-1};
    for(unsigned i=0;i<5;i++){uint32_t b=0x32000+i*64;memcpy(GPTR(b),names[i],strlen(names[i])+1);S16(b+0x20,child[i]);S16(b+0x22,sibling[i]);S16(b+0x24,parent[i]);memcpy(GPTR(0x37000+i*0x9C),names[i],strlen(names[i])+1);
        uint32_t q=0x40000+i*32;putf(q+12,1);putf(q+28,1);putf(q+16,i==0?0:i==1||i==4?.3f:.1f);putf(q+20,i==1?-.2f:i==4?.2f:0);putf(q+24,i==1||i==4?-.2f:0);}
    float cam[9]={10,20,30.6f,1,0,0,0,0,1};memcpy(GPTR(0x50004),cam,36);
    /* Native seat aim (unit+0x23C), deliberately unlike the tracked hand. */
    float aim[3]={0,1,0};memcpy(GPTR(UNIT+0x23C),aim,12);
}
static void boundary(void){EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20004,0x50000);S32(0x20008,1);assert(!host_sense_weapon_dispatch(&c,0x0050BEA0));}
static void tick(void){EngineCPU c={0};c.gpr[4]=0x20000;assert(!host_sense_weapon_dispatch(&c,0x004C6E80));}
static void model(void){
    boundary();EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;c.gpr[1]=0x50004;
    S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);S32(0x2000C,0x50010);S32(0x20010,0x5001C);
    engine_dispatch(&c,0x004D6880);assert(c.pc==0x00493C49&&c.gpr[4]==0x20004);
}
static int fire(uint32_t weapon){
    unsigned before=fires;EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,weapon);S32(0x20008,0);
    engine_dispatch(&c,0x004C3F10);assert(c.pc==0x401234&&c.gpr[4]==0x20004);return fires!=before;
}
static int crosshair(void){
    unsigned before=crosshairs;EngineCPU c={0};c.gpr[4]=0x22000;S32(0x22000,0x004B33C6);S32(0x2200C,0x38000);S16(0x38000,3);
    engine_dispatch(&c,0x004ACAD0);assert(c.pc==0x004B33C6);return crosshairs!=before;
}
/* Original seat entry: eax = unit, stack (vehicle, seat). Returns al. */
static int enter(int seat){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x21000;S32(0x21000,0x00569DEFu);S32(0x21004,VEHICLE_ID);S32(0x21008,(uint32_t)seat);
    c.gpr[0]=UNIT_ID;engine_dispatch(&c,0x00566970u);
    if(escape_entry){assert(c.pc==0xDEADu);return -1;}
    assert(c.pc==0x00569DEFu&&c.gpr[4]==0x21004);return c.gpr[0]&255u;
}
/* Original biped exit request: edi = unit, al = flag. Starts the exit
 * animation and returns al=1 with the biped still seated, or 0. */
static int exit_request(void){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x21000;S32(0x21000,0x0056C40Fu);c.gpr[7]=UNIT_ID;c.gpr[0]=0;
    engine_dispatch(&c,0x0056C470u);assert(c.pc==0x0056C40Fu&&c.gpr[4]==0x21004);return c.gpr[0]&255u;
}
/* Original exit completion (as 0056C470 calls it: unit, 1, flag, 1). */
static void exit_complete(void){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x21000;S32(0x21000,0x0056C4F0u);S32(0x21004,UNIT_ID);S32(0x21008,1);S32(0x2100C,0);S32(0x21010,1);
    S32(0x6400C,G32(0x6400C)+1); /* a later game tick */
    engine_dispatch(&c,0x0056C640u);assert(c.pc==0x0056C4F0u&&c.gpr[4]==0x21004);
}
/* Original unit update of the biped: its inlined seat exit 0055993F runs
 * when its vehicle gates pass: [006893CC] set, vehicle +0x88 below [00672AC0] (fcomp/test ah,5), vehicle +0x10 bit 2. */
static void unit_update_tick(uint32_t datum,int next_tick){
    S8(0x006893CCu,1);putf(VEHICLE+0x88,-1);S32(VEHICLE+0x10,G32(VEHICLE+0x10)|2u);
    if(next_tick)S32(0x6400C,G32(0x6400C)+1);
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x21000;S32(0x21000,0x004E7123u);S32(0x21004,datum);
    engine_dispatch(&c,0x005590A0u);assert(c.pc==0x004E7123u&&c.gpr[4]==0x21004);
}
static void unit_update(uint32_t datum){unit_update_tick(datum,1);}
static HaloSenseMountIdentity identity(void){HaloSenseMountIdentity id;assert(host_sense_mount_identity(&id));return id;}
static HaloSenseMountIdentity refreshed(void){(void)host_sense_mount_refresh();return identity();}
/* A throw the original accepted on a Sense release: unit+0x28D state 1 (set
 * by 0056E080 at 0056E17C), nothing debited or created yet. The binding is
 * the production struct, keyed to the real current producer identity,
 * Sense session/controller, unit and an epoch. Creation is not exercised. */
static void grenade_bound(uint64_t epoch){
    S8(UNIT+0x28D,1);S16(UNIT+0x28E,0);S32(UNIT+0x294,UINT32_MAX);S8(UNIT+0x31C,0);S8(UNIT+0x31E,2);
    HaloSenseStatus status;halo_sense_status(&status);HaloSenseMountIdentity id; /* pure read: no extra mount observation */
    assert(host_sense_mount_identity(&id)&&id.mode==HALO_SENSE_MOUNT_ON_FOOT&&id.unit==UNIT_ID&&status.controller_generation==1);
    sense_grenade_clear();sense_grenade_release.bound=1;sense_grenade_release.unit=UNIT_ID;sense_grenade_release.epoch=epoch;
    sense_grenade_release.session=status.session;sense_grenade_release.controller=status.controller_generation;
    sense_grenade_release.mount=id.generation;
    assert(sense_grenade_honourable(UNIT_ID,UNIT)); /* otherwise honourable before any boundary */
}
/* Original frame-2 creation call 0056E280(unit), through engine_hooks.h. */
static void grenade_create(void){
    assert(engine_hooked(0x0056E280u));EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x25000;S32(0x25000,0x00568123u);S32(0x25004,UNIT_ID);
    c.pc=0x00568120u;ENGINE_DIRECT(&c,0x0056E280u,sub_0056E280_unrouted);assert(c.pc==0x00568123u&&c.gpr[4]==0x25004);
}
static float hand_origin[3];
static void onfoot_shot(void){
    model();assert(sense_muzzle.valid);assert(fire(RIFLE_ID));assert(prespread_override);
    near(shot_forward[0],1);near(shot_forward[1],0);for(unsigned k=0;k<3;k++)near(shot_origin[k],hand_origin[k]);
    EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(reticles.hands[1].valid&&reticles.hands[1].weapon==RIFLE_ID);
}
int main(void){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);setup();
    /* build119 on-foot baseline: tracked hand ray through the original rig/shot. */
    boundary();HaloSenseMountIdentity foot=identity();assert(foot.mode==HALO_SENSE_MOUNT_ON_FOOT&&foot.unit==UNIT_ID&&foot.seat==-1);
    assert(gameplay_supported==1&&native_controls==0);
    model();assert(sense_muzzle.valid&&fire(RIFLE_ID)&&prespread_override);memcpy(hand_origin,shot_origin,12);
    near(shot_forward[0],1);near(hand_origin[0],10+.5f/3.048f+.3f);
    onfoot_shot();assert(!crosshair()); /* tracked reticle replaces the centre crosshair on foot */

    /* Pre-entry leases that must not survive any seat boundary. */
    model();assert(sense_muzzle.valid&&sense_frame_live(UNIT_ID,RIFLE_ID));
    grenade_bound(7);const uint64_t bound_mount=sense_grenade_release.mount;
    sense_mag.state.stage=SENSE_MAG_COMMITTED;sense_mag.have_grip=1;sense_mag_left_owned=sense_mag_drawn_removed=1;
    sense_mag_drawn_epoch=sense_accepted_frame_epoch();sense_support.valid=1;sense_support.unit=UNIT_ID;sense_support.weapon=RIFLE_ID;
    assert(sense_physical_fire_blocked());
    unsigned cancels=physical_reload_cancels,changes=input_changes;uint64_t g0=foot.generation;

    /* R5: a whole entry+exit with no render, tick or fire in between. */
    assert(enter(0)==1);
    HaloSenseMountIdentity in=identity();
    assert(in.mode==HALO_SENSE_MOUNT_SEATED&&in.generation==g0+1&&in.vehicle==VEHICLE_ID&&in.seat==0&&native_controls==1);
    /* Original entry: seat/parent written, held rifle put away for this seat. */
    assert(G16(UNIT+0x2F0)==0&&G32(UNIT+0x11C)==VEHICLE_ID&&(int16_t)G16(UNIT+0x2F2)==-1&&(int16_t)G16(UNIT+0x2F4)==-1);
    assert(G32(RIFLE+0x11C)==UINT32_MAX&&(G32(RIFLE+0x10)&1u)&&detaches==1);
    /* The producer's seat boundary only poisons the grenade binding through the
     * production helper: identity, ownership and uncreated state are kept. */
    assert(sense_grenade_release.bound&&sense_grenade_release.poisoned&&!sense_grenade_release.armed&&sense_grenade_release.projectile==UINT32_MAX);
    assert(sense_grenade_release.unit==UNIT_ID&&sense_grenade_release.epoch==7&&sense_grenade_release.mount==bound_mount&&sense_grenade_release.session==1&&sense_grenade_release.controller==1);
    assert(G8(UNIT+0x28D)==1&&G32(UNIT+0x294)==UINT32_MAX&&G8(UNIT+0x31E)==2);
    assert(sense_mag.state.stage==SENSE_MAG_IDLE&&sense_mag.state.result==SENSE_MAG_RESULT_CANCELLED);
    assert(physical_reload_cancels==cancels+1&&!sense_mag_left_owned&&!sense_mag_drawn_removed&&!sense_physical_fire_blocked());
    assert(!sense_muzzles[1].valid&&!sense_support.valid&&!sense_frame_live(UNIT_ID,RIFLE_ID)&&!sense_accepted.accepted);
    exit_complete();
    assert(identity().generation==g0+1&&identity().mode==HALO_SENSE_MOUNT_SEATED); /* identity is a pure read */
    HaloSenseMountIdentity out=refreshed();
    assert(out.mode==HALO_SENSE_MOUNT_ON_FOOT&&out.generation==g0+2&&native_controls==0&&input_changes==changes+2);
    /* Original exit restored: seat/parent clear, rifle re-selected and attached to the biped. */
    assert((int16_t)G16(UNIT+0x2F0)==-1&&G32(UNIT+0x11C)==UINT32_MAX);
    assert(G16(UNIT+0x2F2)==0&&G16(UNIT+0x2F4)==0&&G32(RIFLE+0x11C)==UNIT_ID&&!(G32(RIFLE+0x10)&1u)&&G32(UNIT+0x308)==501);
    assert(G32(UNIT+0x32C)==VEHICLE_ID&&G32(UNIT+0x330)==501);   /* original exit stamp */
    /* Back on foot after the original exit, the old binding is still poisoned:
     * its matching creation call cancels with the original creation-failure
     * state, no creation, debit, object or cleanup; only then does it end. */
    assert(sense_grenade_release.bound&&sense_grenade_release.poisoned&&sense_grenade_release.epoch==7&&!sense_grenade_honourable(UNIT_ID,UNIT));
    grenade_create();
    assert(G8(UNIT+0x28D)==3&&G32(UNIT+0x294)==UINT32_MAX&&G8(UNIT+0x31E)==2&&!grenade_creations&&!sense_grenade_release.bound);
    S8(UNIT+0x28D,0);
    /* Control: a fresh binding with no boundary reaches the original creation. */
    grenade_bound(8);grenade_create();
    assert(grenade_creations==1&&G8(UNIT+0x28D)==1&&!sense_grenade_release.bound); /* no state 2: production ends the binding */
    S8(UNIT+0x28D,0);
    /* The very next tracked epoch admits a fresh hand ray from that state. */
    onfoot_shot();

    /* Seated: native seat controls, original crosshair and original shot. */
    grenade_bound(10);const uint64_t seated_bound_mount=sense_grenade_release.mount;
    assert(enter(0)==1);tick();boundary();
    HaloSenseMountIdentity seated=identity();assert(seated.mode==HALO_SENSE_MOUNT_SEATED&&seated.generation==g0+3);
    /* A throw bound on foot is poisoned by the entry and its matching creation
     * call cancels while still seated under native controls. */
    assert(sense_grenade_release.bound&&sense_grenade_release.poisoned&&sense_grenade_release.epoch==10&&sense_grenade_release.mount==seated_bound_mount);
    assert(sense_grenade_release.unit==UNIT_ID&&sense_grenade_release.session==1&&sense_grenade_release.controller==1&&!sense_grenade_release.armed&&sense_grenade_release.projectile==UINT32_MAX);
    { unsigned made=grenade_creations;grenade_create();
      assert(identity().mode==HALO_SENSE_MOUNT_SEATED&&identity().generation==seated.generation&&native_controls==1);
      assert(G8(UNIT+0x28D)==3&&G8(UNIT+0x31E)==2&&G32(UNIT+0x294)==UINT32_MAX&&grenade_creations==made&&!sense_grenade_release.bound);
      S8(UNIT+0x28D,0); }                                    /* fixture idle grenade state for the native firing below */
    assert(gameplay_supported==0&&native_controls==1&&native_generation==seated.generation&&!scope_active);
    model();assert(!sense_muzzle.valid);                    /* no tracked transform of the seat's view */
    assert(crosshair());                                    /* original crosshair: native aim */
    EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(!reticles.hands[0].valid&&!reticles.hands[1].valid);
    float m[3]={seat_eye[0]+.1f,seat_eye[1]+2,seat_eye[2]-.05f};memcpy(muzzle_in,m,12);
    assert(fire(TURRET_GUN_ID)&&!prespread_override);
    near(shot_forward[0],0);near(shot_forward[1],1);near(shot_forward[2],0); /* native unit aim, not the hand */
    near(shot_origin[0],seat_eye[0]);near(shot_origin[1],seat_eye[1]+2);near(shot_origin[2],seat_eye[2]); /* original eye-line projection */
    app_blocked=1;assert(!fire(TURRET_GUN_ID));app_blocked=0; /* launcher ownership blocks the seat's own weapon */
    /* Exit request: the original starts the exit animation; the biped is
     * still seated, so native control, crosshair and seat fire remain. */
    S32(0x3E040,0);assert(exit_request()==0);S32(0x3E040,9);  /* no exit animation: refused, still seated */
    exit_blocked=1;assert(exit_request()==0&&G8(UNIT+0x2A3)!=0x1B);exit_blocked=0; /* 00565C60 refuses: still seated */
    assert(identity().generation==g0+3&&identity().mode==HALO_SENSE_MOUNT_SEATED);
    assert(exit_request()==1&&G8(UNIT+0x2A3)==0x1B&&G16(UNIT+0x2F0)==0&&G32(UNIT+0x11C)==VEHICLE_ID);
    assert(identity().generation==g0+3&&native_controls==1&&crosshair()&&fire(TURRET_GUN_ID)&&!prespread_override);
    /* Completion in this same frame: the next gate sees on foot, and with no
     * tracked admission yet nothing fires from the head or the turret. */
    exit_complete();assert(!fire(RIFLE_ID)&&identity().generation==g0+4&&native_controls==0); /* the fire gate observes */
    assert(!crosshair()); /* tracked reticle owns the crosshair again; none until admission */
    tick();boundary();assert(identity().generation==g0+4&&gameplay_supported==1);
    onfoot_shot();assert(!crosshair());
    for(unsigned cycle=0;cycle<3;cycle++){
        uint64_t g=identity().generation;
        assert(enter(0)==1);boundary();assert(identity().generation==g+1&&fire(TURRET_GUN_ID)&&!prespread_override); /* seat fire stays original */
        if(cycle&1){assert(exit_request()==1);exit_complete();}else unit_update(UNIT_ID); /* completion or ejection */
        boundary();assert(identity().generation==g+2);onfoot_shot();
    }
    /* Two complete entry/ejection cycles and a third entry in ONE game tick
     * with no render, tick, fire or refresh: the exit stamp repeats, yet the
     * live observation before and after each original entry makes every
     * boundary distinct. */
    { grenade_bound(9);uint64_t g=identity().generation;unsigned e=entries;
      assert(enter(0)==1&&identity().generation==g+1);unit_update_tick(UNIT_ID,1);uint32_t stamp=G32(UNIT+0x330);
      assert(enter(0)==1&&identity().generation==g+3);unit_update_tick(UNIT_ID,0);
      assert(G32(UNIT+0x330)==stamp&&G32(UNIT+0x32C)==VEHICLE_ID); /* same tick: stamp alone cannot tell */
      assert(enter(0)==1);HaloSenseMountIdentity id=identity();
      assert(id.mode==HALO_SENSE_MOUNT_SEATED&&id.generation==g+5&&entries==e+3&&native_controls==1);
      unit_update_tick(UNIT_ID,0);assert(refreshed().generation==g+6);
      /* A throw bound before those cycles stays poisoned and never creates. */
      assert(sense_grenade_release.bound&&sense_grenade_release.poisoned&&sense_grenade_release.epoch==9);
      unsigned made=grenade_creations;grenade_create();
      assert(G8(UNIT+0x28D)==3&&G32(UNIT+0x294)==UINT32_MAX&&grenade_creations==made&&!sense_grenade_release.bound);S8(UNIT+0x28D,0);
      boundary();onfoot_shot(); }
    /* An ordinary pad (or Sense off) owns the weapon path: the original entry
     * and exit run once each with no mount, input or seat-token mutation;
     * Sense taking over later observes the change exactly once. */
    { uint64_t g=identity().generation;unsigned e=entries,pubs=native_publications,changes0=input_changes;
      active=0;assert(enter(0)==1&&entries==e+1&&G16(UNIT+0x2F0)==0);
      assert(host_sense_mount_refresh()==HALO_SENSE_MOUNT_ON_FOOT&&identity().generation==g); /* refresh mutates nothing */
      assert(exit_request()==1);exit_complete();assert(G16(UNIT+0x2F0)==0xFFFF&&G32(RIFLE+0x11C)==UNIT_ID);
      assert(enter(0)==1&&entries==e+2);
      assert(native_publications==pubs&&input_changes==changes0&&identity().generation==g);
      active=1;tick();assert(identity().generation==g+1&&identity().mode==HALO_SENSE_MOUNT_SEATED&&native_controls==1);
      exit_complete();boundary();assert(identity().generation==g+2);onfoot_shot(); }
    /* Escaped original entry return is preserved; the witness still observes. */
    { uint64_t g=identity().generation;escape_entry=1;assert(enter(0)==-1);escape_entry=0;assert(identity().generation==g); }
    /* A refresh inside nothing in flight is idempotent. */
    { uint64_t g=identity().generation;for(int i=0;i<3;i++)assert(host_sense_mount_refresh()==HALO_SENSE_MOUNT_ON_FOOT);assert(identity().generation==g); }

    /* Death, respawn and player-unit ownership are identity boundaries too. */
    uint64_t g=identity().generation;changes=input_changes;
    model();assert(sense_muzzle.valid);putf(UNIT+0xE0,0);tick();
    assert(identity().mode==HALO_SENSE_MOUNT_NONE&&identity().generation==g+1&&input_changes==changes+1&&!sense_muzzles[1].valid);
    (void)fire(RIFLE_ID);assert(!prespread_override); /* no tracked ray survives death */
    object(5,0x789A,RESPAWN,0x550,0,0x45670004);biped(RESPAWN);S32(RIFLE+0x11C,RESPAWN_ID);
    float raised[3]={10,20,30};memcpy(GPTR(RESPAWN+0x5C),raised,12);S32(0x12034,RESPAWN_ID);
    tick();assert(identity().unit==RESPAWN_ID&&identity().mode==HALO_SENSE_MOUNT_ON_FOOT&&identity().generation==g+2);
    shot_unit=RESPAWN_ID;boundary();EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;c.gpr[1]=0x50004;
    S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);S32(0x2000C,0x50010);S32(0x20010,0x5001C);
    engine_dispatch(&c,0x004D6880);assert(sense_muzzle.valid&&sense_muzzle.unit==RESPAWN_ID&&fire(RIFLE_ID)&&prespread_override);
    S32(0x12034,UNIT_ID);putf(UNIT+0xE0,1);tick();assert(identity().unit==UNIT_ID&&identity().generation==g+3); /* another unit owned */
    /* A unit object too small for the exit stamp (+0x330) is never read. */
    { uint64_t g=identity().generation;S16(0x14006,0x330);tick();assert(identity().mode==HALO_SENSE_MOUNT_NONE&&identity().generation==g+1);
      S16(0x14006,0x550);tick();assert(identity().mode==HALO_SENSE_MOUNT_ON_FOOT&&identity().generation==g+2); }
    /* Pausing (menu/cutscene gate of sense_local_body) is not a boundary. */
    g=identity().generation;S8(0x00718FC9u,1);tick();boundary();assert(identity().generation==g);S8(0x00718FC9u,0);
    assert(native_publications>0);
    free(engine_flat_base);
    printf("PASS: original seat entry 00566970, biped exit request 0056C470 (animation, still seated; refused without animation) and completion 0056C640, ejection 0055993F (weapon put away / re-attached with current==desired slot by 0056D6E0, exit stamp); live identity before/after each original entry (two complete same-tick cycles with a repeated exit stamp stay distinct); read-only identity vs explicit refresh; ordinary-pad entry/exit run once with no mutation, later Sense takeover observes once; entry+exit with no intervening poll advance the mount generation twice and cancel accepted pose, muzzle, support, committed magazine + queued commit and input; a bound uncreated grenade throw is only poisoned by the production helper, stays poisoned through the original exit and two same-tick cycles, and its 0056E280 cancels (state 3, no creation/debit/object) and also while seated under native controls, while a fresh uninterrupted bound control reaches creation; seated native crosshair and original 005658F0 shot from the seat eye along native aim; post-exit tracked hand ray restored; repeated cycles, escaped entry, death, respawn and ownership change; truncated unit object refused; pause is not a boundary; every leaf reached is listed (%u unlisted)\n",unexpected);
    return 0;
}
