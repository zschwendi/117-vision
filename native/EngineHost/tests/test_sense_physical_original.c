/* Connected-arm and physical-interaction regression on the original lifted
 * 004D6880 skeleton, 004D6FC0 world-model and shot-ray instructions, through
 * the accepted weapon frame. Harness mirrors test_sense_weapon_original.c;
 * no device, game process, save, inventory, ammo or damage is fabricated. */
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
/* This fixture tests the physical layer with the optional body disabled.
 * Embedding probes provide their own body adapter after including us. */
#if __INCLUDE_LEVEL__ == 0
int host_sense_body_active(void){return 0;}
#endif
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
void halo_sense_status(HaloSenseStatus *s){memset(s,0,sizeof *s);s->session=1;s->generation=input_generation;s->active=active;s->requested=halo_sense_requested();s->gameplay_supported=1;}
int halo_sense_head_read(HaloSenseHeadPose *h){memset(h,0,sizeof *h);memcpy(h->forward,head_forward,12);memcpy(h->up,head_up,12);h->generation=h->controller_generation=1;return head_fresh;}
int halo_sense_app_input_blocked(void){return app_blocked;}
int halo_sense_scope_input(HaloSenseScopeInput *i){memset(i,0,sizeof *i);i->session=1;i->generation=input_generation;i->armed=scope_armed;halo_sense_head_read(&i->head);return !app_blocked&&scope_active&&head_fresh&&fresh;}
static float left_trigger,right_trigger;
static uint64_t input_session=1; /* varied only by render-qualification tests */
int halo_sense_dual_input(HaloSenseDualInput *p){memset(p,0,sizeof *p);p->session=input_session;p->generation=input_generation;p->fresh=fresh;p->support_grip=support_grip;p->support_blocked=support_blocked;p->armed[0]=p->armed[1]=1;p->trigger[0]=left_trigger;p->trigger[1]=right_trigger;return !app_blocked&&fresh;}
static int art_available,art_captured_draws;
int host_sense_reticle_art_begin(unsigned h,uint32_t w,const HaloSenseReticleFrame*t,float x,float y){(void)x;(void)y;return art_available&&h<2&&t->hands[h].valid&&t->hands[h].weapon==w;}
int host_sense_reticle_art_end(int complete){assert(complete);return 1;}
static HaloSenseReticleFrame reticles;
void halo_sense_reticle_publish(const HaloSenseReticleFrame *p){if(p)reticles=*p;else memset(&reticles,0,sizeof reticles);}
int halo_sense_reticle_read(HaloSenseReticleFrame *p){*p=reticles;return fresh&&(p->hands[0].valid||p->hands[1].valid);}

void halo_sense_observe_controller(const HostGCSnapshot *p){(void)p;}
void halo_sense_set_gameplay_supported(int s){(void)s;}
void halo_sense_set_native_controls(int n,uint64_t g){(void)n;(void)g;}
int halo_sense_gameplay_supported(void){return 1;}
static float aim_angle,aim_roll,left_roll;
int halo_sense_weapon_pose(unsigned hand,float p[3],float f[3],float u[3]){
    if(!fresh)return 0;
    float roll=aim_roll+(hand==0?left_roll:0);
    float pos[3]={.5f,hand==1?-.25f:.25f,-.3f},dir[3]={cosf(aim_angle),sinf(aim_angle),0},up[3]={sinf(aim_angle)*sinf(roll),-cosf(aim_angle)*sinf(roll),cosf(roll)};
    if(custom_positions)memcpy(pos,grip_positions[hand],12);
    memcpy(p,pos,12);memcpy(f,dir,12);memcpy(u,up,12);return 1;
}
static float head_pos[3];static int head_pos_valid=1;
int halo_sense_read(HaloSenseFrame *frame){memset(frame,0,sizeof *frame);if(!fresh||app_blocked)return 0;
    frame->generation=frame->controller_generation=1;memcpy(frame->head_position,head_pos,12);frame->head_position_valid=head_pos_valid;
    frame->left.valid=halo_sense_weapon_pose(0,frame->left.position,frame->left.forward,frame->left.up);
    frame->right.valid=halo_sense_weapon_pose(1,frame->right.position,frame->right.forward,frame->right.up);return frame->left.valid&&frame->right.valid;
}
/* Grenade release semantics are covered by test_sense_weapon_original.c. */
int halo_sense_grenade_accept(uint32_t c,uint32_t t,HaloSenseGrenadeRelease *out){(void)c;(void)t;if(out)memset(out,0,sizeof *out);return 0;}
int halo_sense_grenade_hold(HaloSenseGrenadeHold *out){if(out)memset(out,0,sizeof *out);return 0;} /* no armed hold in this fixture */
static int collision_hit;
static uint32_t guest_next=0x60000;
uint32_t guest_alloc(uint32_t size){uint32_t p=guest_next;guest_next+=(size+15u)&~15u;assert(guest_next<0x70000);return p;}
uint32_t host_call_guest(EngineCPU *cpu,uint32_t fn,int n,const uint32_t *args,int pops){
    if(fn==0x004F5350u){assert(n==1&&!pops);uint32_t sp=cpu->gpr[4],pc=cpu->pc;engine_push(cpu,args[0],4);engine_push(cpu,0xDEADC0DEu,4);engine_dispatch(cpu,fn);assert(cpu->pc==0xDEADC0DEu);cpu->gpr[4]=sp;cpu->pc=pc;return cpu->gpr[0];}
    assert(fn==0x00505880&&n==5&&pops==0&&args[0]==0x1000E9&&args[3]==0x23450000);return collision_hit;
}
static void ret_to_caller(EngineCPU *cpu){cpu->pc=engine_pop(cpu,4);}
static unsigned physical_commits,physical_accept=1;static int physical_last_action;static uint64_t physical_last_sequence;
static uint32_t physical_last_owner,physical_last_target;static unsigned physical_cancels;static int (*physical_check)(int,uint32_t,uint32_t);
int halo_sense_physical_enqueue(uint64_t s,uint64_t g,uint64_t q,int a,uint32_t o,uint32_t t){assert(s==1&&g==input_generation&&q>physical_last_sequence);physical_last_sequence=q;physical_commits++;physical_last_action=a;physical_last_owner=o;physical_last_target=t;return (int)physical_accept;}
static int physical_pending=1;
int halo_sense_physical_pending(int a){(void)a;return physical_pending;}
void halo_sense_physical_cancel(int a){(void)a;physical_cancels++;}
void halo_sense_physical_set_delivery_check(int (*c)(int,uint32_t,uint32_t)){physical_check=c;}
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
#include "../sense_weapon_hooks.inc"
#include "../dual_wield.inc"
#include "../sense_world_model_override.inc" /* the shared wrapper itself, after every helper */
/* The outer render boundary: every model build and shot below is its own
 * epoch, as between game frames. */
static void boundary(void){EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20004,0x50000);S32(0x20008,1);assert(!host_sense_weapon_dispatch(&c,0x0050BEA0));}
/* Completes a 200 ms grip latch/release ease in fixture time. */
static void settle(void){now+=HALO_SENSE_GRIP_EASE_NS;boundary();}
static unsigned fires,rays;static float observed_origin[3],observed_forward[3],observed_velocity;
static unsigned grenades;static int grenade_escape;static float grenade_direction[3];
static int escape_ray;
void sub_004F6AA0(EngineCPU *cpu){float velocity[3]={2,3,4};memcpy(GPTR(cpu->gpr[6]),velocity,12);ret_to_caller(cpu);}
void sub_004F5DE0(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004F5C30(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004C0180(EngineCPU *cpu){ret_to_caller(cpu);}
/* Original 004D6FC0's draw callees: stubs; 004D72A0 records the palette
 * the original passes to drawing (render record at its third argument). */
static unsigned char drawn_palette[64*52];static unsigned drawn_count,draws_seen;static int model_ecx_mode;static uint32_t model_ecx_value;
void sub_00518B40(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_00526F50(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_0052B530(EngineCPU *cpu){ret_to_caller(cpu);}
void sub_004D72A0(EngineCPU *cpu){uint32_t record=G32(cpu->gpr[4]+12);drawn_count=G16(record+4);assert(drawn_count<=64);
    memcpy(drawn_palette,GPTR(G32(record)),drawn_count*52);draws_seen++;ret_to_caller(cpu);}
void sub_00568F80(EngineCPU *cpu){(void)cpu;assert(!"Body eye projection must be disabled for the tracked ray");}
void engine_dispatch(EngineCPU *cpu,uint32_t address){
    cpu->pc=address;if(host_sense_weapon_dispatch(cpu,address))return;
    if(address==0x0056E280u){ret_to_caller(cpu);return;}
    if(address==0x004F5350u){sub_004F5350(cpu);return;}
    if(address==0x004BEF80u){sub_004BEF80(cpu);return;}
    if(address==0x0056E440u){grenades++;memcpy(grenade_direction,GPTR(0x1523C),12);
        if(sense_grenade_in_release){EngineCPU release=*cpu;release.gpr[4]=0x26000;S32(0x26000,0x0056E75A);release.gpr[0]=0x45670002;release.gpr[2]=0x26100;
            float delta[3];for(unsigned k=0;k<3;k++)delta[k]=grenade_direction[k]*.5f;memcpy(GPTR(0x26100),delta,12);engine_dispatch(&release,0x004BEF80u);assert(release.pc==0x0056E75Au);}
        if(grenade_escape)cpu->pc=0xDEAD;else ret_to_caller(cpu);return;}
    if(address==0x004ACAD0u){assert(sense_reticle_art_nested);art_captured_draws++;ret_to_caller(cpu);return;}
    if(address==0x0050BEA0u){if(!host_sense_scope_rendering()&&host_sense_scope_dispatch(cpu,address))return;
        scope_draws++;memcpy(scope_camera,GPTR(ARG(0)+4),36);memcpy(scope_camera2,GPTR(ARG(0)+0x58),36);memcpy(&scope_fov,GPTR(ARG(0)+4+0x28),4);
        if((ARG(1)&65535u)>1)memcpy(scope_interface,GPTR(ARG(0)+0xAC),sizeof scope_interface);
        now+=scope_elapsed;if(scope_change_epoch)input_generation++;
        if(scope_escape)cpu->pc=0xDEAD;else ret_to_caller(cpu);return;}
    if(address==0x004D6880u){sub_004D6880(cpu);return;}
    if(address==0x004D6FC0u){if(host_sense_world_model_dispatch(cpu,address))return;sub_004D6FC0(cpu);
        if(model_ecx_mode==1)cpu->gpr[1]=model_ecx_value;if(model_ecx_mode==2){cpu->gpr[1]=0x12345678;cpu->pc=0xDEADu;}return;}
    if(address==0x004CB970u){sub_004CB970(cpu);return;}
    if(address==0x004CBAD0u){sub_004CBAD0(cpu);return;}
    if(address==0x004CC0D0u){sub_004CC0D0(cpu);return;}
    if(address==0x005658F0u){if(escape_ray){cpu->pc=0xDEAD;return;}sub_005658F0(cpu);return;}
    if(address==0x004F6AA0u){sub_004F6AA0(cpu);return;}
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
static void fire(void){boundary();EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,0x34560001);S32(0x20008,0);engine_dispatch(&c,0x004C3F10);assert(c.pc==0x401234&&c.gpr[4]==0x20004);}
/* Another first-person build in the SAME outer epoch (another view/eye). */
static void model_in_epoch(void){EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;c.gpr[1]=0x50004;S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);S32(0x2000C,0x50010);S32(0x20010,0x5001C);engine_dispatch(&c,0x004D6880);assert(c.pc==0x00493C49);}
static void fire_in_epoch(void){EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,0x34560001);S32(0x20008,0);engine_dispatch(&c,0x004C3F10);assert(c.pc==0x401234&&c.gpr[4]==0x20004);}
/* Ten-node rig with the verified CE chain names: frame bone24 root,
 * l/r upperarm -> forearm -> wriste, frame gun under r wriste, and a
 * frame magazine under the gun. Halo units; identity local rotations. */
enum { ROOT, LUP, RUP, LFORE, RFORE, LWRIST, RWRIST, GUN, MAG, MAGROUNDS, OPHANDLE, NODES };
static const char *rig_names[NODES]={"frame bone24","frame l upperarm","frame r upperarm","frame l forearm",
    "frame r forearm","frame l wriste","frame r wriste","frame gun","frame magazine","frame rounds","frame ophandle"};
static const int rig_parent[NODES]={-1,ROOT,ROOT,LUP,RUP,LFORE,RFORE,RWRIST,GUN,MAG,GUN};
static float rig_local[NODES][3]={{0,0,0},{-.02f,.06f,-.06f},{-.02f,-.06f,-.06f},{.07f,0,-.0714143f},{.07f,0,-.0714143f},
    {.1f,0,0},{.1f,0,0},{.02f,0,.01f},{.03f,0,-.03f},{0,0,.01f},{-.06f,0,.03f}};
static void rig_write(void){
    int first_child[NODES],next_sibling[NODES];for(unsigned i=0;i<NODES;i++)first_child[i]=next_sibling[i]=-1;
    for(int i=NODES-1;i>0;i--){next_sibling[i]=first_child[rig_parent[i]];first_child[rig_parent[i]]=i;}
    memset(GPTR(0x32000),0,NODES*64);memset(GPTR(0x37000),0,NODES*0x9C);memset(GPTR(0x40000),0,NODES*32);
    for(unsigned i=0;i<NODES;i++){uint32_t b=0x32000+i*64;memcpy(GPTR(b),rig_names[i],strlen(rig_names[i])+1);
        S16(b+0x20,first_child[i]);S16(b+0x22,next_sibling[i]);S16(b+0x24,rig_parent[i]);
        memcpy(GPTR(0x37000+i*0x9C),rig_names[i],strlen(rig_names[i])+1);
        uint32_t q=0x40000+i*32;putf(q+12,1);putf(q+28,1);for(unsigned k=0;k<3;k++)putf(q+16+4*k,rig_local[i][k]);}
    S32(0x31068,NODES);S32(0x340B8,NODES);S8(0x36002,GUN);
}
static void rig_setup(void){setup();rig_write();}
static unsigned char off_nodes[NODES*52];static float off_origin[3],off_forward[3];static HaloSenseReticleFrame off_reticles;
static SenseArmNode node_at(unsigned i){SenseArmNode n;memcpy(&n,GPTR(0x41000+i*52),52);return n;}
static float dist(const float a[3],const float b[3]){float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return sqrtf(sense_dot(d,d));}
/* Disabled (build118) frame: nodes, actual final shot and reticle. */
static void capture_off(void){
    halo_sense_set_connected_arms(0);model();memcpy(off_nodes,GPTR(0x41000),sizeof off_nodes);
    unsigned before=fires;fire();assert(fires==before+1&&rays);memcpy(off_origin,observed_origin,12);memcpy(off_forward,observed_forward,12);
    EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);off_reticles=reticles;
}
/* Everything the gun, reticle, scope and shot read is byte-identical. */
static void assert_weapon_identical(void){
    for(unsigned i=0;i<NODES;i++)if(i!=LUP&&i!=RUP&&i!=LFORE&&i!=RFORE)assert(!memcmp(off_nodes+i*52,GPTR(0x41000+i*52),52));
    unsigned before=fires;fire();assert(fires==before+1);
    assert(!memcmp(off_origin,observed_origin,12)&&!memcmp(off_forward,observed_forward,12));
    EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);assert(!memcmp(&off_reticles,&reticles,sizeof reticles));
}
static void assert_identical_to_off(void){assert(!memcmp(off_nodes,GPTR(0x41000),sizeof off_nodes));}
static SenseArmNode arm_node(float x,float y,float z){SenseArmNode n={.scale=1,.c={{1,0,0},{0,1,0},{0,0,1}},.p={x,y,z}};return n;}
static float basis_delta(const SenseArmNode *a,const SenseArmNode *b){float m=0;for(unsigned c=0;c<3;c++){float d[3]={a->c[c][0]-b->c[c][0],a->c[c][1]-b->c[c][1],a->c[c][2]-b->c[c][2]};float l=sqrtf(sense_dot(d,d));if(l>m)m=l;}return m;}
/* Adjacent-frame sweep: every pair of consecutive solved frames must move
 * elbow/shoulder/bases at most `bound` times the wrist motion (no flips),
 * with the forearm always ending at the wrist and lengths fixed. */
typedef struct{int held,valid;SenseArmNode u,f,t;unsigned solved,transitions;float worst;}Sweep;
static void sweep_step(Sweep *sw,const SenseArmNode *up,const SenseArmNode *fo,const SenseArmNode *wr,const SenseArmNode *target,float bound){
    float down[3]={0,0,-1},shift=0;SenseArmNode ou,of;memset(&ou,0x7F,52);memset(&of,0x7F,52);SenseArmNode su=ou,sf=of;
    int r=sense_arm_solve_held(up,fo,wr,target,down,sw->held,&ou,&of,&shift);
    if(r!=SENSE_ARM_SOLVED){assert(!memcmp(&ou,&su,52)&&!memcmp(&of,&sf,52));if(sw->held)sw->transitions++;sw->held=0;sw->valid=0;return;}
    if(!sw->held&&sw->valid==0&&sw->solved)sw->transitions++;
    float a=dist(up->p,fo->p),c=dist(fo->p,wr->p);
    assert(fabsf(dist(ou.p,of.p)-a)<2e-4f*a&&fabsf(dist(of.p,target->p)-c)<2e-4f*c);
    assert(fabsf(shift)<=SENSE_ARM_SHIFT_EXIT*(a+c)+1e-6f);
    /* Angular target motion near the shoulder turns the whole arm by
     * step/d; scale the bound by reach/d. A flip is a jump of ~2h, i.e. a
     * ratio in the hundreds for these 1e-4..1e-3 steps. */
    bound*=fmaxf(1.f,(a+c)/dist(up->p,target->p));
    if(sw->valid){float step=dist(sw->t.p,target->p)+basis_delta(&sw->t,target)*c;
        float moved=dist(sw->f.p,of.p),root=dist(sw->u.p,ou.p),turn=basis_delta(&sw->f,&of)*c+basis_delta(&sw->u,&ou)*a;
        float worst=fmaxf(moved,fmaxf(root,turn))/fmaxf(step,1e-7f);if(worst>sw->worst)sw->worst=worst;
        if(!(moved<=bound*step+1e-5f&&root<=bound*step+1e-5f&&turn<=bound*step+1e-5f)){
            float l0[3]={wr->p[0]-up->p[0],wr->p[1]-up->p[1],wr->p[2]-up->p[2]},ln[3]={target->p[0]-up->p[0],target->p[1]-up->p[1],target->p[2]-up->p[2]};sense_normalize(l0);sense_normalize(ln);
            fprintf(stderr,"discontinuity step=%g elbow=%g root=%g turn=%g d=%g cos=%g upturn=%g foreturn=%g\n",step,moved,root,turn,dist(up->p,target->p),sense_dot(l0,ln),basis_delta(&sw->u,&ou)*a,basis_delta(&sw->f,&of)*c);assert(0);}}
    sw->held=1;sw->valid=1;sw->solved++;sw->u=ou;sw->f=of;sw->t=*target;
}
/* Magazine fixtures: assault rifle tag path, magazine block with
 * rounds_loaded_maximum, weapon object magazine[0] at +0x2B0. */
static void mag_fixture(unsigned state,unsigned reserve,unsigned loaded){
    S32(0x30030,0x39000);memset(GPTR(0x39000),0,80);strcpy(GPTR(0x39000),"weapons\\assault rifle\\assault rifle");
    S32(0x33000+0x4F0,1);S32(0x33000+0x4F4,0x3A000);S16(0x3A000+0xA,60);S16(0x3A000+8,600);
    S16(0x162B0,state);S16(0x162B6,reserve);S16(0x162B8,loaded);
}
static float right_hand[3]={.5f,-.2f,-.3f};
/* Place the tracked left grip at a world point (neutral camera basis). */
static void left_at(const float world[3],float dx,float dy,float dz){
    float p[3]={(world[0]-10)*3.048f+dx,(world[1]-20)*3.048f+dy,(world[2]-30.6f)*3.048f+dz};
    memcpy(grip_positions[0],p,12);memcpy(grip_positions[1],right_hand,12);
}
static unsigned char gun_ref[52],wrist_ref[52];static float shot_ref[6];
static void weapon_unchanged(void){
    assert(!memcmp(gun_ref,GPTR(0x41000+GUN*52),52)&&!memcmp(wrist_ref,GPTR(0x41000+RWRIST*52),52));
    unsigned before=fires;fire_in_epoch();assert(fires==before+1);
    assert(!memcmp(shot_ref,observed_origin,12)&&!memcmp(shot_ref+3,observed_forward,12));
}
static void advance(uint64_t ns){now+=ns;EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20004,0x50000);S32(0x20008,1);assert(!host_sense_weapon_dispatch(&c,0x0050BEA0));}
/* Magazine drawn out of the gun: gun/wrist unchanged, original trigger refused. */
static void weapon_blocked(void){
    assert(!memcmp(gun_ref,GPTR(0x41000+GUN*52),52)&&!memcmp(wrist_ref,GPTR(0x41000+RWRIST*52),52));
    assert(sense_physical_fire_blocked());unsigned before=fires;fire_in_epoch();assert(fires==before);
}
static HaloSensePhysicalStatus mstatus(void){HaloSensePhysicalStatus st;assert(host_sense_physical_status(&st));return st;}
static void magazine_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);custom_positions=1;mag_fixture(0,120,10);
    left_trigger=right_trigger=0;physical_commits=0;physical_accept=1;
    float far_left[3]={.45f,.3f,-.35f};memcpy(grip_positions[0],far_left,12);memcpy(grip_positions[1],right_hand,12);
    halo_sense_set_physical_magazine(0);model();assert(mstatus().magazine_support==HALO_SENSE_MAGAZINE_OFF);
    unsigned char off[NODES*52];memcpy(off,GPTR(0x41000),sizeof off);
    memcpy(gun_ref,GPTR(0x41000+GUN*52),52);memcpy(wrist_ref,GPTR(0x41000+RWRIST*52),52);
    fire_in_epoch();memcpy(shot_ref,observed_origin,12);memcpy(shot_ref+3,observed_forward,12);
    SenseArmNode seat=node_at(MAG),rounds=node_at(MAGROUNDS);
    /* Enabled and ready: nothing changes until the hand closes on it. */
    halo_sense_set_physical_magazine(1);model();HaloSensePhysicalStatus st=mstatus();
    assert(st.magazine_enabled&&st.magazine_support==HALO_SENSE_MAGAZINE_READY&&st.magazine_stage==HALO_SENSE_MAGAZINE_SEATED);
    assert(!memcmp(off,GPTR(0x41000),sizeof off));weapon_unchanged();
    /* L2 away from the magazine: no grab. */
    left_trigger=1;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_SEATED);left_trigger=0;model();
    /* Grab at the magazine (4 cm off), pull 20 cm: only the magazine
     * subtree follows the left grip, rigidly. */
    left_at(seat.p,.04f,0,0);model();left_trigger=1;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_GRIPPED);
    /* Decided during this build: drawn seated, support/fire authority unchanged. */
    assert(!sense_physical_left_busy()&&!sense_physical_fire_blocked());
    assert(!memcmp(off+MAG*52,GPTR(0x41000+MAG*52),52));weapon_unchanged();
    model();assert(sense_physical_left_busy()&&sense_physical_fire_blocked());weapon_blocked();
    left_at(seat.p,.04f,.05f,-.20f);model();st=mstatus();assert(st.magazine_stage==HALO_SENSE_MAGAZINE_OUT);
    SenseArmNode held=node_at(MAG),held_rounds=node_at(MAGROUNDS);
    near(held.p[0],seat.p[0]);near(held.p[1],seat.p[1]+.05f/3.048f);near(held.p[2],seat.p[2]-.20f/3.048f);
    near(held_rounds.p[2]-held.p[2],rounds.p[2]-seat.p[2]);assert(held.scale==seat.scale);
    for(unsigned i=0;i<NODES;i++)if(i!=MAG&&i!=MAGROUNDS&&i!=LWRIST)assert(!memcmp(off+i*52,GPTR(0x41000+i*52),52)||i==LUP||i==LFORE);
    weapon_blocked();
    /* Discard: released away from the well, the well is empty. */
    left_trigger=0;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_DISCARDED);
    assert(G32(0x41000+MAG*52)==0&&G32(0x41000+MAGROUNDS*52)==0);weapon_blocked();
    /* L2 near the well is not a fetch; 30 cm away it takes a new one. */
    left_at(seat.p,.04f,0,-.10f);model();left_trigger=1;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_DISCARDED);
    left_trigger=0;left_at(seat.p,.04f,.1f,-.30f);model();left_trigger=1;model();
    assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_NEW);assert(node_at(MAG).scale==seat.scale);
    /* Inserting at a 60 degree roll is not aligned: no commit, still held. */
    left_roll=1.0472f;left_at(seat.p,.04f,0,0);model();
    assert(physical_commits==0&&mstatus().magazine_stage==HALO_SENSE_MAGAZINE_NEW);weapon_blocked();
    left_roll=0;model();
    assert(physical_commits==1&&physical_last_action==HALO_SENSE_GESTURE_PHYSICAL_RELOAD);
    assert(physical_last_owner==0x23450000&&physical_last_target==0x34560001);
    /* Commit build: still one-handed (latched) but drawn seated, so the
     * original trigger is allowed; the next build releases the hand. */
    st=mstatus();assert(st.magazine_stage==HALO_SENSE_MAGAZINE_COMMITTED&&sense_physical_left_busy()&&!sense_physical_fire_blocked());
    assert(!memcmp(off+MAG*52,GPTR(0x41000+MAG*52),52));weapon_unchanged();
    model();assert(!sense_physical_left_busy());
    /* Further frames never request again; the original reload confirms. */
    model();model();assert(physical_commits==1);
    S16(0x162B0,1);model();st=mstatus();assert(st.magazine_result==HALO_SENSE_MAGAZINE_CONFIRMED&&st.magazine_stage==HALO_SENSE_MAGAZINE_SEATED);
    /* The original reload animation moves the magazine 10 cm down; the
     * physically inserted magazine stays seated on the gun meanwhile. */
    {float saved_mag[3];memcpy(saved_mag,rig_local[MAG],12);rig_local[MAG][2]-=.1f/3.048f;rig_write();
     /* The original reload-full class (14) is playing: a physically started
     * reload keeps its pinned, tracked presentation, not the native takeover. */
     S32(0x006B2D98,0x58000);S8(0x58000,1);S32(0x58004,0x23450000);S32(0x58008,0x34560001);S16(0x5800C,14);model();
     SenseArmNode pinned=node_at(MAG);for(unsigned k=0;k<3;k++)near(pinned.p[k],seat.p[k]);weapon_unchanged();
     uint32_t pd,pw,pu=sense_local_unit(&pd,&pw);assert(pu&&sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_NONE);
     S32(0x006B2D98,0); /* no idle cache here: it would let the rifle's two-hand foregrip latch */
     S16(0x162B0,0);model();near(node_at(MAG).p[2],seat.p[2]-.1f/3.048f); /* idle: original animation again */
     S16(0x162B0,1);model();near(node_at(MAG).p[2],seat.p[2]-.1f/3.048f); /* no pin without a new physical insertion */
     S32(0x006B2D98,0x58000);assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD); /* an ordinary reload is native */
     /* The exception is bound to its own commit: this unit, weapon, Sense
      * session and input generation. Another owner's, weapon's or lease's
      * pinned/committed state never keeps a different reload tracked. */
     {int pin=sense_mag.pinned,stage=sense_mag.state.stage;uint32_t mu=sense_mag.unit,mw=sense_mag.weapon;uint64_t ms=sense_mag.session,mg=sense_mag.generation;
      assert(sense_mag_feature&&mu==pd&&mw==pw);
      sense_mag.pinned=1;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_NONE);
      sense_mag.weapon=0xBEEF0001u;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD);sense_mag.weapon=mw;
      sense_mag.unit=0xBEEF0000u;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD);sense_mag.unit=mu;
      sense_mag.session=ms+1;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD);sense_mag.session=ms;
      sense_mag.generation=mg+1;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD);sense_mag.generation=mg;
      sense_mag.pinned=0;sense_mag.state.stage=SENSE_MAG_COMMITTED;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_NONE);
      sense_mag_feature=0;assert(sense_action_for_unit(pu,pd,pw,NULL)==HALO_SENSE_ACTION_RELOAD);sense_mag_feature=1; /* option off: native */
      sense_mag.pinned=pin;sense_mag.state.stage=stage;}
     S16(0x5800C,0);S32(0x006B2D98,0);
     memcpy(rig_local[MAG],saved_mag,12);rig_write();}
    assert(G16(0x162B6)==120&&G16(0x162B8)==10); /* ammo is the engine's: never written here */
    assert(mstatus().magazine_support==HALO_SENSE_MAGAZINE_ENGINE_BUSY);
    /* While the original reload runs nothing can start. */
    left_trigger=0;model();left_at(seat.p,.04f,0,0);left_trigger=1;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_SEATED);
    S16(0x162B0,0);left_trigger=0;model();
    /* Engine never starts: refused after the confirmation window. */
    #define FULL_CYCLE() do{left_at(seat.p,.04f,0,0);left_trigger=0;model();left_trigger=1;model();left_at(seat.p,.04f,0,-.2f);model();left_trigger=0;model(); \
        left_at(seat.p,.04f,.1f,-.3f);model();left_trigger=1;model();left_at(seat.p,.04f,0,0);model();}while(0)
    FULL_CYCLE();assert(physical_commits==2&&mstatus().magazine_stage==HALO_SENSE_MAGAZINE_COMMITTED);
    advance(800000000);model();st=mstatus();assert(st.magazine_result==HALO_SENSE_MAGAZINE_REFUSED&&st.magazine_stage==HALO_SENSE_MAGAZINE_SEATED);
    /* Transport busy: magazine stays in hand; retry needs a new insertion. */
    physical_accept=0;FULL_CYCLE();assert(physical_commits==3&&mstatus().magazine_result==HALO_SENSE_MAGAZINE_BUSY&&mstatus().magazine_stage==HALO_SENSE_MAGAZINE_NEW);
    model();model();assert(physical_commits==3);
    physical_accept=1;left_at(seat.p,.04f,0,-.15f);model();left_at(seat.p,.04f,0,0);model();assert(physical_commits==4&&mstatus().magazine_stage==HALO_SENSE_MAGAZINE_COMMITTED);
    S16(0x162B0,1);model();S16(0x162B0,0);left_trigger=0;model();
    /* Re-seating the same magazine is not a reload. */
    left_at(seat.p,.04f,0,0);model();left_trigger=1;model();left_at(seat.p,.04f,0,-.2f);model();left_at(seat.p,.04f,0,-.05f);model();
    left_trigger=0;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_SEATED&&physical_commits==4);
    assert(!memcmp(off+MAG*52,GPTR(0x41000+MAG*52),52));
    /* Cancels: firing, input generation (recenter/tracking), weapon change,
     * melee, dual mode, tracking loss; each restores the seated original. */
    for(int reason=0;reason<6;reason++){
        left_trigger=0;left_at(seat.p,.04f,0,0);model();left_trigger=1;model();left_at(seat.p,.04f,0,-.2f);model();
        assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_OUT);
        if(reason==0)right_trigger=1;else if(reason==1)input_generation++;else if(reason==2)S16(0x162B0,3);
        else if(reason==3)S8(0x15289,1);else if(reason==4)dual_enabled=1;else fresh=0;
        model();
        if(reason!=3&&reason!=5){st=mstatus();assert(st.magazine_stage==HALO_SENSE_MAGAZINE_SEATED);if(reason!=1)assert(st.magazine_result==HALO_SENSE_MAGAZINE_CANCELLED);}
        assert(physical_commits==4);
        right_trigger=0;S16(0x162B0,0);S8(0x15289,0);dual_enabled=0;fresh=1;left_trigger=0;model();
        assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_SEATED&&!sense_physical_left_busy());
        if(reason!=1)assert(!memcmp(off+MAG*52,GPTR(0x41000+MAG*52),52));
    }
    /* Full magazine or empty reserve cannot start; unsupported weapons and
     * rigs without a model 'frame magazine' report unsupported. */
    mag_fixture(0,120,60);left_at(seat.p,.04f,0,0);model();left_trigger=1;model();
    st=mstatus();assert(st.magazine_support==HALO_SENSE_MAGAZINE_FULL&&st.magazine_stage==HALO_SENSE_MAGAZINE_SEATED);left_trigger=0;model();
    mag_fixture(0,0,10);left_trigger=1;model();assert(mstatus().magazine_stage==HALO_SENSE_MAGAZINE_SEATED);left_trigger=0;model();
    mag_fixture(0,120,10);strcpy(GPTR(0x39000),"weapons\\plasma rifle\\plasma rifle");model();assert(mstatus().magazine_support==HALO_SENSE_MAGAZINE_UNSUPPORTED);
    mag_fixture(0,120,10);strcpy(GPTR(0x37000+MAG*0x9C),"frame mag");model();assert(mstatus().magazine_support==HALO_SENSE_MAGAZINE_UNSUPPORTED);
    rig_write();model();assert(mstatus().magazine_support==HALO_SENSE_MAGAZINE_READY);
    /* Off again: exactly the disabled rig. */
    left_trigger=0;memcpy(grip_positions[0],far_left,12);halo_sense_set_physical_magazine(0);model();assert(!memcmp(off,GPTR(0x41000),sizeof off));
    puts("PASS: physical magazine on original 004D6880 rig: only frame magazine subtree held/hidden, gun/wrist/shot unchanged, one original reload per new-magazine insertion, engine confirmation/refusal, busy retry, re-seat no commit, cancels, unsupported/full/no-reserve.");
}
/* Coordinator repro (mag-aim-transition-probe.c): two-hand support engaged,
 * then the magazine grab. Every accepted shot must equal the rendered gun
 * axis and the published reticle of the same build; a refused shot only
 * while the gun is drawn without its magazine. */
static unsigned aim_frame(const char *label){
    now+=HALO_SENSE_GRIP_EASE_NS; /* let the cache's support ease complete between renders */
    model();SenseArmNode gun=node_at(GUN);EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);
    unsigned before=fires;fire_in_epoch();int accepted=fires==before+1; /* game ticks before the next render */
    float d=accepted?sense_dot(gun.c[0],observed_forward):1;float angle=acosf(fminf(1,fmaxf(-1,d)))*57.2957795f;
    if(reticles.hands[1].valid){float r=sense_dot(gun.c[0],reticles.hands[1].direction);if(!(r>.99999f)){printf("reticle mismatch %s r=%.7f dir=%f,%f,%f\n",label,r,reticles.hands[1].direction[0],reticles.hands[1].direction[1],reticles.hands[1].direction[2]);}assert(r>.99999f);}
    printf("aim %-22s stage=%d owned=%d support=%d accepted=%d gun=%.6f,%.6f,%.6f angle=%.6f\n",label,sense_mag.state.stage,sense_physical_left_busy(),
        sense_support.engaged,accepted,gun.c[0][0],gun.c[0][1],gun.c[0][2],angle);
    /* build118 support composition alone shows ~0.03 deg float noise; the
     * reproduced ownership defect was 9.73 deg. */
    if(accepted)assert(angle<.1f);else assert(sense_physical_fire_blocked());
    return (unsigned)accepted;
}
static void support_aim_authority(void){
    float saved[3];memcpy(saved,rig_local[MAG],12);rig_local[MAG][0]=.10f;rig_local[MAG][1]=.01f;rig_local[MAG][2]=0;
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);mag_fixture(0,120,10);custom_positions=1;
    float hands[2][3]={{.85f,-.14f,-.3f},{.5f,-.2f,-.3f}};memcpy(grip_positions,hands,sizeof hands);
    S32(0x006B2D98,0x58000);S8(0x58000,1);S32(0x58004,0x23450000);S32(0x58008,0x34560001);S16(0x5800C,0);
    sense_support.valid=0;left_trigger=right_trigger=0;physical_commits=0;halo_sense_set_physical_magazine(1);
    assert(aim_frame("support"));assert(sense_support.engaged);
    left_trigger=1;assert(aim_frame("grab decided"));assert(sense_mag.state.stage==SENSE_MAG_GRIPPED&&sense_support.engaged);
    assert(!aim_frame("held one-handed")&&!sense_support.engaged);
    /* Release at the well: seated in this build (still one-handed), then
     * support returns on the next build; each accepted shot coincides. */
    left_trigger=0;assert(aim_frame("released at well"));assert(!sense_support.engaged);
    assert(aim_frame("support again")&&sense_support.engaged);
    /* R2 while held: the shot is refused while drawn out, the stage cancels,
     * the next build seats it and the shot is accepted on the drawn gun. */
    left_trigger=1;aim_frame("grab 2");assert(!aim_frame("held 2"));right_trigger=1;assert(aim_frame("R2 cancel build")); /* cancelled + seated in this build: one-handed shot on the drawn gun */
    assert(sense_mag.state.stage==SENSE_MAG_IDLE&&sense_mag.state.result==SENSE_MAG_RESULT_CANCELLED);
    assert(aim_frame("seated after cancel"));right_trigger=0;left_trigger=0;aim_frame("settle");aim_frame("settle 2");
    /* Settings off mid-frame: the latched build keeps its authority; the
     * next build is the disabled path. */
    left_trigger=1;aim_frame("grab 3");assert(!aim_frame("held 3"));
    halo_sense_set_physical_magazine(0);{unsigned before=fires;fire_in_epoch();assert(fires==before);} /* same epoch: still refused */
    assert(aim_frame("disabled next build"));left_trigger=0;
    S32(0x006B2D98,0);memcpy(rig_local[MAG],saved,12);rig_write();sense_support.valid=0;
    puts("PASS: one aim authority: support/one-hand ownership latched per build; every accepted shot equals the rendered gun and reticle across grab/hold/release/R2-cancel/settings.");
}
/* Real engine-side delivery validator (registered at load) and reach
 * pickup over fixture player/object memory using the PC1.10 layout. */
static void world_weapon(uint32_t parent){
    S16(0x13020,4);S16(0x14024,0x5678);S16(0x1402A,0x340);S32(0x1402C,0x18000);memset(GPTR(0x18000),0,0x340);
    S16(0x180B4,2);S32(0x1811C,parent);float p[3]={10+1.8f/3.048f,20+.25f/3.048f,30.6f-.5f/3.048f};memcpy(GPTR(0x1805C),p,12);
    S32(0x18000,0x45670001);
    /* Its own node palette at +0x340 (object size 0x400): three rigid nodes. */
    S16(0x1402A,0x400);S16(0x181F2,0x340);
    for(unsigned i=0;i<3;i++){uint32_t n=0x18340+i*52;memset(GPTR(n),0,52);putf(n,1);putf(n+4,1);putf(n+20,1);putf(n+36,1);
        putf(n+40,p[0]+.1f*(float)i);putf(n+44,p[1]);putf(n+48,p[2]);}
}
/* Outer epochs of at most 100 ms (each: boundary + accepted model build),
 * as rendered frames; a longer gap would be a stalled game. */
static void pepoch(uint64_t ns){do{uint64_t step=ns>100000000u?100000000u:ns;now+=step;ns-=step;model();}while(ns);}
static HaloSensePickupStatus pstatus(void){HaloSensePickupStatus st;host_sense_pickup_status(&st);return st;}
/* Original 004D6FC0 fixture: the selected weapon's own palette, moved
 * rigidly by the wrapper, is what the original consumer multiplies by the
 * model's bind matrices and passes to drawing. Other objects and the
 * caller's stack are untouched; both eyes see the same palette. */
/* Publish the forced render pose the way the epoch update does: stamped with
 * this epoch's live accepted frame (the fixture's unit and selected weapon). */
static void stamp_render(void){
    SenseAcceptedWeaponFrame f;assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&f));
    sense_pick_render.epoch=f.epoch;sense_pick_render.session=f.session;sense_pick_render.generation=f.generation;
    sense_pick_render.unit=f.unit;sense_pick_render.weapon=f.weapon;
}
static int expect_entry_ecx;
static void draw_object(uint32_t object,uint32_t body,uint32_t ret,uint32_t flags){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x27000;S32(0x27000,ret);putf(0x27004,1);
    S32(0x27018,0x27100);float center[3];memcpy(center,GPTR(body+0x5C),12);memcpy(GPTR(0x27100),center,12);
    S32(0x27024,object);S32(0x2702C,flags);c.gpr[0]=0x45670004;c.gpr[1]=body+(uint32_t)(int16_t)G16(body+0x1F2);
    uint32_t entry_ecx=c.gpr[1];
    draws_seen=0;engine_dispatch(&c,0x004D6FC0);assert(c.pc==ret&&c.gpr[4]==0x27004&&draws_seen==1);
    assert(G32(0x27018)==0x27100); /* caller's center slot restored */
    if(expect_entry_ecx)assert(c.gpr[1]==entry_ecx); /* the shared wrapper restores entry ECX when it applied */
}
static void pickup_render_fixture(void){
    /* Weapon definition (tag 3) -> model (tag 4): three nodes, bind inverses. */
    S32(0x3006C,0x45670003);S32(0x30074,0x3B000);S32(0x3008C,0x45670004);S32(0x30094,0x3C000);
    S32(0x3B034,0x45670004);putf(0x3C008,0);S32(0x3C0B8,3);S32(0x3C0BC,0x3D000);
    for(unsigned i=0;i<3;i++){uint32_t b=0x3D000+i*0x9C+0x68;memset(GPTR(b),0,52);putf(b,1);putf(b+4,1);putf(b+20,1);putf(b+36,1);putf(b+40,-.01f*(float)i);}
    S32(0x18000,0x45670003);S16(0x1402A,0x400);S16(0x181F2,0x340);
    float ground[3];memcpy(ground,GPTR(0x1805C),12);
    for(unsigned i=0;i<3;i++){uint32_t n=0x18340+i*52;memset(GPTR(n),0,52);putf(n,1);putf(n+4,1);putf(n+20,1);putf(n+36,1);
        putf(n+40,ground[0]+.1f*(float)i);putf(n+44,ground[1]);putf(n+48,ground[2]);}
    unsigned char object_before[0x400];memcpy(object_before,GPTR(0x18000),sizeof object_before);
    /* Ground: no pull active -> untouched original palette. */
    sense_pick_render.active=0;draw_object(0x56780003,0x18000,0x0050F049,0);
    unsigned char ground_drawn[3*52];memcpy(ground_drawn,drawn_palette,sizeof ground_drawn);
    /* Held pose: reference = the original run on a manually moved palette. */
    HaloSenseTransform pose={0};float f[3]={0,1,0},u[3]={0,0,1};assert(sense_basis(f,u,&pose));pose.p[0]=11;pose.p[1]=21;pose.p[2]=30.2f;
    HaloSenseTransform root;assert(sense_node(0x18340,&root));HaloSenseTransform d=sense_delta(&root,&pose,root.p,pose.p);
    unsigned char saved[3*52];memcpy(saved,GPTR(0x18340),sizeof saved);
    for(unsigned i=0;i<3;i++)sense_apply_node(0x18340+i*52,&d);
    draw_object(0x56780003,0x18000,0x0050F049,0);unsigned char reference[3*52];memcpy(reference,drawn_palette,sizeof reference);
    memcpy(GPTR(0x18340),saved,sizeof saved);
    sense_pick_render.active=1;sense_pick_render.object=0x56780003;sense_pick_render.pose=pose;stamp_render();
    expect_entry_ecx=1;
    for(int eye=0;eye<2;eye++){draw_object(0x56780003,0x18000,0x0050F049,0);assert(drawn_count==3&&!memcmp(reference,drawn_palette,sizeof reference));}
    assert(memcmp(ground_drawn,drawn_palette,sizeof ground_drawn)); /* visibly moved */
    /* Bone lengths/scale preserved in the consumed palette. */
    for(unsigned i=1;i<3;i++){float a[13],b[13],g0[13],gi[13];memcpy(a,drawn_palette,52);memcpy(b,drawn_palette+i*52,52);memcpy(g0,ground_drawn,52);memcpy(gi,ground_drawn+i*52,52);
        float da[3]={b[10]-a[10],b[11]-a[11],b[12]-a[12]},dg[3]={gi[10]-g0[10],gi[11]-g0[11],gi[12]-g0[12]};near(sense_dot(da,da),sense_dot(dg,dg));near(b[0],gi[0]);}
    /* Shadow pass follows the same pose; flags/caller must agree. */
    draw_object(0x56780003,0x18000,0x0050F0BD,2);assert(!memcmp(reference,drawn_palette,sizeof reference));
    expect_entry_ecx=0;
    draw_object(0x56780003,0x18000,0x0050F0BD,0);assert(!memcmp(ground_drawn,drawn_palette,sizeof ground_drawn));
    /* Any other caller, object, model or palette is drawn unchanged. */
    draw_object(0x56780003,0x18000,0x00401234,0);assert(!memcmp(ground_drawn,drawn_palette,sizeof ground_drawn));
    sense_pick_render.object=0x56790003;draw_object(0x56780003,0x18000,0x0050F049,0);assert(!memcmp(ground_drawn,drawn_palette,sizeof ground_drawn));
    sense_pick_render.object=0x56780003;S32(0x1811C,0x23450000);draw_object(0x56780003,0x18000,0x0050F049,0);
    assert(!memcmp(ground_drawn,drawn_palette,sizeof ground_drawn));S32(0x1811C,UINT32_MAX); /* owned: no longer a ground weapon */
    S16(0x00719720,1);draw_object(0x56780003,0x18000,0x0050F049,0);assert(!memcmp(ground_drawn,drawn_palette,sizeof ground_drawn));S16(0x00719720,0);
    /* The original leaving a derived or unrelated ECX, or escaping: entry ECX
     * and the caller's center slot are restored either way. */
    expect_entry_ecx=1;model_ecx_mode=1;model_ecx_value=0x60000+52;draw_object(0x56780003,0x18000,0x0050F049,0);model_ecx_mode=0;expect_entry_ecx=0;
    {EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x27000;S32(0x27000,0x0050F049);putf(0x27004,1);S32(0x27018,0x27100);S32(0x27024,0x56780003);S32(0x2702C,0);
     c.gpr[0]=0x45670004;c.gpr[1]=0x18340;model_ecx_mode=2;engine_dispatch(&c,0x004D6FC0);model_ecx_mode=0;
     assert(c.pc==0xDEADu&&c.gpr[1]==0x18340&&G32(0x27018)==0x27100);}
    assert(!memcmp(object_before,GPTR(0x18000),sizeof object_before)); /* object memory never written */
    sense_pick_render.active=0;S32(0x18000,0x45670001);
    puts("PASS: original 004D6FC0 consumes the selected weapon's rigidly moved palette (both eyes, shadow), other callers/objects/owned/non-campaign untouched, object memory and caller stack unchanged.");
}
/* Rotation about a unit axis applied to the identity basis. */
static HaloSenseTransform rotated(const float axis[3],float angle){
    float r[3][3];sense_arm_axis_angle(axis,angle,r);HaloSenseTransform t={0};
    for(unsigned c=0;c<3;c++)for(unsigned k=0;k<3;k++)t.c[c][k]=r[k][c];return t;
}
static float basis_angle(const HaloSenseTransform *x,const HaloSenseTransform *y){
    float worst=0;for(unsigned c=0;c<3;c++){float d=acosf(fminf(1,fmaxf(-1,sense_dot(x->c[c],y->c[c]))))*57.2957795f;if(d>worst)worst=d;}return worst;
}
/* Coordinator repros (pickup-pi-probe.c, pickup-pi-recheck.c): a fixed 180
 * degree target across progress 0.5, and a target hand slowly turning
 * through 180 degrees relative to the source in adjacent frames at fixed
 * progress. Both must stay continuous within one flight. */
static void pickup_blend_tests(void){
    HaloSenseTransform a=rotated((float[3]){0,0,1},0),b=rotated((float[3]){0,0,1},3.14159265f),o0,o1;b.p[0]=1;
    float branch[4];int have=0;
    assert(sense_pick_blend(&a,&b,.499f,0,branch,&have,&o0)==1&&sense_pick_blend(&a,&b,.501f,0,branch,&have,&o1)==1);
    float step=basis_angle(&o0,&o1);printf("repro pi: progress .499->.501 step=%.4f deg\n",step);assert(step<1.f);
    /* Recheck: progress fixed 0.5, target yaw 179.885 -> 180.115 deg. */
    have=0;HaloSenseTransform t0=rotated((float[3]){0,0,1},3.14159265f-.002f),t1=rotated((float[3]){0,0,1},3.14159265f+.002f);
    assert(sense_pick_blend(&a,&t0,.5f,0,branch,&have,&o0)==1&&sense_pick_blend(&a,&t1,.5f,0,branch,&have,&o1)==1);
    step=basis_angle(&o0,&o1);printf("repro pi recheck: target yaw 179.885->180.115 at progress .5 step=%.4f deg\n",step);assert(step<1.f);
    /* Endpoints exact. */
    have=0;assert(sense_pick_blend(&a,&b,0,.1f,branch,&have,&o0)==1&&!memcmp(&o0,&a,sizeof a));
    assert(sense_pick_blend(&a,&b,1,.1f,branch,&have,&o0)==1&&!memcmp(&o0,&b,sizeof b));
    /* Whole flights: progress advancing while the target turns through pi
     * (forward and backward, several axes, with and without a tilted
     * source). Every adjacent frame is continuous; same-epoch repeats are
     * identical; the flight ends exactly on the target. */
    const float axes[][3]={{0,0,1},{1,0,0},{0,1,0},{.577350f,.577350f,.577350f},{.6f,0,.8f}};
    for(unsigned ax=0;ax<5;ax++)for(int dir=-1;dir<=1;dir+=2)for(int tilt=0;tilt<2;tilt++){
        HaloSenseTransform src=tilt?rotated((float[3]){0,1,0},.4f):rotated((float[3]){0,0,1},0);
        have=0;HaloSenseTransform prev;int first=1;float worst=0;
        for(int i=0;i<=400;i++){
            float sprog=(float)i/400.f,angle=3.14159265f+(float)dir*(-.3f+.6f*sprog);
            HaloSenseTransform tgt=rotated(axes[ax],angle);
            if(tilt){HaloSenseTransform both=tgt;for(unsigned c=0;c<3;c++){float v[3];sense_rotate(&tgt,src.c[c],v);memcpy(both.c[c],v,12);}tgt=both;}
            HaloSenseTransform cur,again;float b2[4];int h2;
            int r=sense_pick_blend(&src,&tgt,sprog,0,branch,&have,&cur);assert(r==1);
            memcpy(b2,branch,16);h2=have;assert(sense_pick_blend(&src,&tgt,sprog,0,b2,&h2,&again)==1&&!memcmp(&cur,&again,sizeof cur)); /* second eye */
            float cr[3];sense_cross(cur.c[0],cur.c[1],cr);assert(sense_dot(cr,cur.c[2])>.9999f);
            if(!first){float d=basis_angle(&prev,&cur);if(d>worst)worst=d;}
            first=0;prev=cur;
            if(i==400)assert(!memcmp(&cur,&tgt,sizeof tgt));
        }
        if(worst>=2.5f)printf("flight axis=%u dir=%d tilt=%d worst=%.3f\n",ax,dir,tilt,worst);
        assert(worst<2.5f); /* each frame: <=0.45 deg of progress over up to ~360 deg plus target motion */
    }
    /* A fresh flight re-chooses the short arc; branch state is per flight. */
    have=0;HaloSenseTransform near_pi=rotated((float[3]){0,0,1},3.14159265f+.002f);
    assert(sense_pick_blend(&a,&near_pi,.5f,0,branch,&have,&o0)==1);
    float qa_dot_branch=branch[3];assert(qa_dot_branch>=-1e-6f); /* shorter arc from the identity source */
    /* Reflected, skewed or non-finite bases fail closed. */
    HaloSenseTransform r=a;for(unsigned k=0;k<3;k++)r.c[1][k]=-r.c[1][k];have=0;assert(!sense_pick_blend(&a,&r,.5f,0,branch,&have,&o0));
    HaloSenseTransform k=a;k.c[0][1]=.2f;have=0;assert(!sense_pick_blend(&k,&b,.5f,0,branch,&have,&o0));
    HaloSenseTransform n=a;n.c[2][2]=NAN;have=0;assert(!sense_pick_blend(&a,&n,.5f,0,branch,&have,&o0));
    have=0;assert(!sense_pick_blend(&a,&b,NAN,0,branch,&have,&o0));
    puts("PASS: flight orientation: per-flight continuous quaternion branch; fixed and moving targets through 180 degrees about several axes, forward/backward, tilted sources, same-epoch repeats identical, exact endpoints, rotations only; reflected/skewed/non-finite fail closed.");
}
static void delivery_and_pickup_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);mag_fixture(0,120,10);custom_positions=1;
    assert(physical_check); /* registered by sense_physical_hooks.inc at load */
    float hands[2][3]={{.3f,.2f,-.3f},{.5f,-.2f,-.3f}};memcpy(grip_positions,hands,sizeof hands);model();
    /* Reload delivery: same unit, same weapon, idle, not full, reserve, option on. */
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001)); /* option off */
    halo_sense_set_physical_magazine(1);
    assert(physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x45670002)); /* other weapon */
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x99990000,0x34560001));
    S16(0x152F2,1);S16(0x152F4,1);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));S16(0x152F2,0);S16(0x152F4,0); /* switched slot */
    mag_fixture(1,120,10);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));
    mag_fixture(0,0,10);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));
    mag_fixture(0,120,60);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));
    mag_fixture(0,120,10);S8(0x15289,1);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));S8(0x15289,0);
    S16(0x152F0,0);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_RELOAD,0x23450000,0x34560001));S16(0x152F0,0xFFFF); /* vehicle */
    unsigned cancels=physical_cancels;halo_sense_set_physical_magazine(0);assert(physical_cancels==cancels+1);
    /* Pickup: only the engine's own selection (result 6/7 + salted datum). */
    world_weapon(UINT32_MAX);S16(0x12028,6);S32(0x12024,0x56780003);
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003)); /* option off */
    halo_sense_set_physical_pickup(1);
    assert(physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));
    S16(0x12028,7);assert(physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));
    S16(0x12028,8);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));S16(0x12028,6); /* vehicle entry */
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56790003)); /* stale salt */
    S16(0x14024,0x5679);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));S16(0x14024,0x5678);
    S32(0x1811C,0x23450000);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));S32(0x1811C,UINT32_MAX);
    S16(0x00719720,1);assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12340000,0x56780003));S16(0x00719720,0); /* not campaign */
    assert(!physical_check(HALO_SENSE_GESTURE_PHYSICAL_PICKUP,0x12350000,0x56780003)); /* other player */
    /* Physical pickup stages: one transaction step per outer epoch, after
     * that epoch's accepted frame (model build). */
    float pulled[3]={.1f,.15f,-.2f};
    #define LOCK() do{left_trigger=0;memcpy(grip_positions[0],hands[0],12);pepoch(0);left_trigger=1;pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED);}while(0)
    physical_commits=0;left_trigger=0;memset(head_pos,0,12);head_pos_valid=1;pepoch(0);HaloSensePickupStatus ps=pstatus();
    assert(ps.enabled&&ps.reason==HALO_SENSE_PICKUP_READY&&ps.stage==HALO_SENSE_PICKUP_AIMED&&ps.target_valid);
    near(ps.target[0],1.8f);near(ps.target[1],.25f);near(ps.target[2],-.5f);
    assert(!sense_pick_render.active); /* aiming draws nothing new */
    left_trigger=1;pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED&&physical_commits==0&&!sense_pick_render.active);
    /* Head moving 30 cm toward a stationary hand is not a pull. */
    head_pos[0]=.3f*.83f;head_pos[1]=.3f*.55f;
    for(unsigned k=0;k<3;k++)grip_positions[0][k]=hands[0][k]-head_pos[k];
    pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED&&!sense_pick_render.active);
    memset(head_pos,0,12);memcpy(grip_positions[0],hands[0],12);pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED);
    /* Hand pulled back in the room: the weapon flies, no commit yet. */
    memcpy(grip_positions[0],pulled,12);pepoch(0);ps=pstatus();
    assert(ps.stage==HALO_SENSE_PICKUP_FLYING&&physical_commits==0&&sense_pick_render.active&&sense_pick_render.object==0x56780003);
    HaloSenseTransform start=sense_pick_render.pose;near(start.p[0],10+1.8f/3.048f); /* starts where it lies */
    pepoch(150000000);HaloSenseTransform mid=sense_pick_render.pose;assert(physical_commits==0);
    HaloSenseTransform hand_now;assert(sense_world_hand(0,0x15000,0x23450000,&hand_now));
    assert(dist(mid.p,hand_now.p)<dist(start.p,hand_now.p)&&mid.p[2]>fminf(start.p[2],hand_now.p[2])); /* closer, arcing */
    /* Arrival with L2 held is the catch: exactly one original X. */
    pepoch(500000000);ps=pstatus();assert(ps.stage==HALO_SENSE_PICKUP_COMMITTED&&physical_commits==1&&physical_last_action==HALO_SENSE_GESTURE_PHYSICAL_PICKUP);
    assert(physical_last_owner==0x12340000&&physical_last_target==0x56780003&&sense_pick_render.active);
    for(unsigned k=0;k<3;k++)near(sense_pick_render.pose.p[k],hand_now.p[k]); /* drawn in the hand */
    pepoch(0);pepoch(0);assert(physical_commits==1);
    /* Only the original pickup makes it taken; then the ground draw stops. */
    S32(0x1811C,0x23450000);pepoch(0);ps=pstatus();assert(ps.result==HALO_SENSE_PICKUP_TAKEN&&ps.stage==HALO_SENSE_PICKUP_IDLE&&!sense_pick_render.active);
    assert(G16(0x12028)==6&&G32(0x12024)==0x56780003); /* engine selection never written */
    float lying[3];memcpy(lying,GPTR(0x1805C),12);near(lying[0],10+1.8f/3.048f); /* world object never moved */
    /* Not taken within the window: refused, drawn on the ground again at once. */
    S32(0x1811C,UINT32_MAX);LOCK();memcpy(grip_positions[0],pulled,12);pepoch(0);pepoch(700000000);assert(physical_commits==2&&sense_pick_render.active);
    pepoch(800000000);model();pepoch(0);assert(pstatus().result==HALO_SENSE_PICKUP_REFUSED&&!sense_pick_render.active);
    /* Releasing L2 mid-flight drops it back immediately; no commit. */
    LOCK();memcpy(grip_positions[0],pulled,12);pepoch(0);assert(sense_pick_render.active);left_trigger=0;pepoch(0);
    assert(!sense_pick_render.active&&pstatus().result==HALO_SENSE_PICKUP_CANCELLED&&physical_commits==2);
    /* Transport busy at the catch: back on the ground, new lock required. */
    physical_accept=0;LOCK();memcpy(grip_positions[0],pulled,12);pepoch(0);pepoch(700000000);
    assert(physical_commits==3&&pstatus().result==HALO_SENSE_PICKUP_BUSY&&!sense_pick_render.active);physical_accept=1;
    /* Cancels while locked or flying: selection changes, salt changes, not
     * campaign, vehicle, virtual turn, no head position. */
    for(int reason=0;reason<6;reason++)for(int flying=0;flying<2;flying++){
        if((reason==4||reason==5)&&flying)continue; /* turning / head position matter only for measuring the pull */
        LOCK();if(flying){memcpy(grip_positions[0],pulled,12);pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_FLYING);}
        if(reason==0)S16(0x12028,0);else if(reason==1){S16(0x14024,0x5679);S32(0x12024,0x56790003);}
        else if(reason==2)S16(0x00719720,1);else if(reason==3)S16(0x152F0,0);
        else if(reason==4){float cam[9]={10,20,30.6f,.9659f,.2588f,0,0,0,1};memcpy(GPTR(0x50004),cam,36);}
        else head_pos_valid=0;
        memcpy(grip_positions[0],pulled,12);pepoch(0);pepoch(700000000);
        if(!(physical_commits==3&&pstatus().stage!=HALO_SENSE_PICKUP_COMMITTED&&!sense_pick_render.active))printf("pickup cancel reason=%d flying=%d commits=%u stage=%d render=%d\n",reason,flying,physical_commits,pstatus().stage,sense_pick_render.active);
        assert(physical_commits==3&&pstatus().stage!=HALO_SENSE_PICKUP_COMMITTED&&!sense_pick_render.active);
        S16(0x12028,6);S16(0x14024,0x5678);S32(0x12024,0x56780003);S16(0x00719720,0);S16(0x152F0,0xFFFF);head_pos_valid=1;
        float cam[9]={10,20,30.6f,1,0,0,0,0,1};memcpy(GPTR(0x50004),cam,36);left_trigger=0;pepoch(0);
    }
    /* Linearization while X is pending: release, lost engine selection or
     * a conflicting control cancels it and the ground draw returns in the
     * same frame; delivery without the unit owning it is a refusal; an X
     * the original consumed just before the cancel is still reported taken. */
    #define CATCH() do{LOCK();memcpy(grip_positions[0],pulled,12);pepoch(0);pepoch(700000000);assert(pstatus().stage==HALO_SENSE_PICKUP_COMMITTED&&sense_pick_render.active);}while(0)
    for(int reason=0;reason<4;reason++){
        unsigned commits=physical_commits,cancels=physical_cancels;CATCH();assert(physical_commits==commits+1);
        if(reason==0)left_trigger=0;else if(reason==1)S16(0x12028,0);else if(reason==2)support_blocked=1;else physical_pending=0;
        pepoch(0);ps=pstatus();assert(!sense_pick_render.active&&ps.stage==HALO_SENSE_PICKUP_IDLE);
        assert(ps.result==(reason==3?HALO_SENSE_PICKUP_REFUSED:HALO_SENSE_PICKUP_CANCELLED));
        assert(physical_cancels==cancels+(reason==3?0u:1u));
        S16(0x12028,6);support_blocked=0;physical_pending=1;left_trigger=0;pepoch(0);
    }
    CATCH();left_trigger=0;pepoch(0);assert(pstatus().result==HALO_SENSE_PICKUP_CANCELLED&&!sense_pick_render.active);
    S32(0x1811C,0x23450000);pepoch(0);assert(pstatus().result==HALO_SENSE_PICKUP_TAKEN&&!sense_pick_render.active); /* late original take */
    S32(0x1811C,UINT32_MAX);pepoch(0);
    /* A held weapon occupies the left hand for the next build only. */
    CATCH();model();assert(sense_physical_left_busy());
    left_trigger=0;pepoch(0);model();assert(!sense_physical_left_busy());
    /* Not pointing at it: no aim. */
    left_trigger=0;memcpy(grip_positions[0],hands[0],12);aim_angle=1.2f;pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_IDLE);aim_angle=0;
    unsigned commits_before_cancels=physical_commits;
    /* Before admission the read-only getter is false and that is not a
     * cancellation: a locked grab survives an outer boundary whose model has
     * not been built yet, and progresses at that epoch's build. */
    LOCK();boundary();{SenseAcceptedWeaponFrame f;assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&f));}
    assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED);model_in_epoch();assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED);
    /* Repeated builds (views/eyes) of one epoch never step it twice. */
    memcpy(grip_positions[0],pulled,12);model_in_epoch();model_in_epoch();assert(pstatus().stage==HALO_SENSE_PICKUP_LOCKED&&!sense_pick_render.active);
    memcpy(grip_positions[0],hands[0],12);
    /* No game frame builds for >150 ms: the outer boundary lapses it. */
    for(int i=0;i<3;i++){now+=60000000;boundary();}
    assert(pstatus().stage==HALO_SENSE_PICKUP_IDLE&&!sense_pick_render.active);
    /* Menu, input generation and weapon change cancel at once, even mid-flight. */
    for(int reason=0;reason<3;reason++){
        LOCK();memcpy(grip_positions[0],pulled,12);pepoch(0);assert(pstatus().stage==HALO_SENSE_PICKUP_FLYING&&sense_pick_render.active);
        if(reason==0){S8(0x718FC9,1);boundary();S8(0x718FC9,0);}
        else if(reason==1){input_generation++;boundary();input_generation--;}
        else{S32(0x152F8,0x99990001);model();S32(0x152F8,0x34560001);} /* not this epoch's weapon: no admission */
        if(reason==2){for(int i=0;i<3;i++){now+=60000000;boundary();}}
        assert(pstatus().stage==HALO_SENSE_PICKUP_IDLE&&!sense_pick_render.active&&physical_commits==commits_before_cancels);
        left_trigger=0;pepoch(0);
    }
    pickup_render_fixture();
    halo_sense_set_physical_pickup(0);pepoch(0);assert(pstatus().reason==HALO_SENSE_PICKUP_OFF&&!pstatus().enabled&&!sense_pick_render.active);
    S16(0x13020,2);
    puts("PASS: delivery validator and physical pickup: room-frame pull (head motion alone never pulls), render-only flight from the ground pose, one original X only at the catch, taken only when the unit owns it, refusal/release/busy/cancels draw it on the ground again, world object/selection never written.");
}
/* Coordinator repro (mag-stereo-epoch-probe.c) and variants: within one
 * accepted epoch every first-person build/eye draws the same physical
 * magazine presentation; later raw L2/R2/setting changes only take effect
 * at the next outer epoch. */
static unsigned char epoch_nodes[NODES*52];
static void same_epoch_identical(const char *label,int stage){
    uint64_t epoch=sense_accepted_frame_epoch();memcpy(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes);
    unsigned commits=physical_commits;model_in_epoch();
    int same=!memcmp(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes);
    printf("epoch %-22s epoch=%llu->%llu stage=%d->%d identical=%d\n",label,(unsigned long long)epoch,(unsigned long long)sense_accepted_frame_epoch(),stage,sense_mag.state.stage,same);
    assert(epoch==sense_accepted_frame_epoch()&&sense_mag.state.stage==stage&&same&&physical_commits==commits);
}
static SenseArmNode mag_seat;
static void to_out(void){
    if(sense_mag.state.stage!=SENSE_MAG_IDLE){left_trigger=0;right_trigger=1;model();} /* R2 cancels a leftover stage */
    left_trigger=right_trigger=0;float far_left[3]={.45f,.3f,-.35f};memcpy(grip_positions[0],far_left,12);model();model();
    left_at(mag_seat.p,.04f,0,0);model();left_trigger=1;model();model();left_at(mag_seat.p,.04f,.05f,-.20f);model();
    assert(sense_mag.state.stage==SENSE_MAG_OUT&&node_at(MAG).scale==1&&sense_physical_fire_blocked());
}
static void to_new(void){
    to_out();left_trigger=0;model();assert(sense_mag.state.stage==SENSE_MAG_DISCARDED);
    left_at(mag_seat.p,.04f,.1f,-.30f);model();left_trigger=1;model();assert(sense_mag.state.stage==SENSE_MAG_NEW);
}
static void magazine_epoch_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);custom_positions=1;mag_fixture(0,120,10);
    memcpy(grip_positions[1],right_hand,12);halo_sense_set_physical_magazine(1);physical_accept=1;model();mag_seat=node_at(MAG);
    /* Exact repro: held OUT, release L2, same-epoch build. */
    to_out();left_trigger=0;same_epoch_identical("release",SENSE_MAG_OUT);
    {unsigned before=fires;fire_in_epoch();assert(fires==before);} /* still drawn without its magazine */
    model();assert(sense_mag.state.stage==SENSE_MAG_DISCARDED&&G32(0x41000+MAG*52)==0);
    /* Before the first build of a new epoch: previous state owns a removed
     * magazine, so a shot is still refused. */
    boundary();{unsigned before=fires;fire_in_epoch();assert(fires==before);}
    /* R2 while held: unchanged in this epoch; next epoch cancels and seats it,
     * and only then is a shot accepted. */
    to_out();right_trigger=1;same_epoch_identical("R2",SENSE_MAG_OUT);{unsigned before=fires;fire_in_epoch();assert(fires==before);}
    model();assert(sense_mag.state.stage==SENSE_MAG_IDLE&&!memcmp(&mag_seat,GPTR(0x41000+MAG*52),52)&&!sense_physical_fire_blocked());
    {unsigned before=fires;fire_in_epoch();assert(fires==before+1);}right_trigger=0;
    /* Setting off mid-epoch: same epoch keeps its presentation and refusal. */
    to_out();halo_sense_set_physical_magazine(0);same_epoch_identical("setting off",SENSE_MAG_OUT);
    {unsigned before=fires;fire_in_epoch();assert(fires==before);}
    model();assert(sense_mag.state.stage==SENSE_MAG_IDLE&&!sense_physical_fire_blocked());halo_sense_set_physical_magazine(1);model();
    /* Successful insert: one commit in its epoch, repeated builds add none. */
    to_new();unsigned commits=physical_commits;left_at(mag_seat.p,.04f,0,0);model();
    assert(physical_commits==commits+1&&sense_mag.state.stage==SENSE_MAG_COMMITTED);
    same_epoch_identical("insert committed",SENSE_MAG_COMMITTED);
    S16(0x162B0,1);model();S16(0x162B0,0);left_trigger=0;model();
    /* Refused insert (transport busy): still in hand, no second request in
     * the same epoch, a new insertion is required. */
    to_new();physical_accept=0;commits=physical_commits;left_at(mag_seat.p,.04f,0,0);model();
    assert(physical_commits==commits+1&&sense_mag.state.stage==SENSE_MAG_NEW);
    same_epoch_identical("insert refused",SENSE_MAG_NEW);model();assert(physical_commits==commits+1);
    physical_accept=1;left_trigger=0;model();model();
    halo_sense_set_physical_magazine(0);model();
    puts("PASS: physical magazine is one presentation per accepted epoch: release/R2/setting/insert/refusal never change a later build or eye of the same epoch; shots stay refused while it is out, including before the next epoch's first build.");
}
/* Charging phase (own setting) on the original 004D6880 rig: the new
 * magazine stays seated without a commit until the real 'frame ophandle'
 * subtree is pulled back by its travel and released; then exactly one
 * original reload. */
static void charge_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);custom_positions=1;mag_fixture(0,120,10);
    memcpy(grip_positions[1],right_hand,12);halo_sense_set_physical_magazine(1);halo_sense_set_physical_charge(1);physical_accept=1;
    left_trigger=right_trigger=0;model();mag_seat=node_at(MAG);SenseArmNode handle_seat=node_at(OPHANDLE),gun=node_at(GUN);
    HaloSensePhysicalStatus st=mstatus();assert(st.charge_enabled&&st.charge_supported);
    unsigned char seated_nodes[NODES*52];memcpy(seated_nodes,GPTR(0x41000),sizeof seated_nodes);
    /* Charging before an insertion is nothing: L2 at the handle of the
     * seated original magazine neither grabs nor commits. */
    unsigned commits=physical_commits;
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();
    assert(sense_mag.state.stage==SENSE_MAG_IDLE&&physical_commits==commits&&!memcmp(&handle_seat,GPTR(0x41000+OPHANDLE*52),52));
    left_trigger=0;model();
    /* Insert a new magazine: seated, not committed, can still fire (it was
     * never charged; the original magazine's rounds are what the game has). */
    to_new();left_at(mag_seat.p,.04f,0,0);model();
    assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&physical_commits==commits&&mstatus().magazine_stage==HALO_SENSE_MAGAZINE_INSERTED);
    model();assert(!memcmp(&mag_seat,GPTR(0x41000+MAG*52),52)&&!memcmp(&handle_seat,GPTR(0x41000+OPHANDLE*52),52));
    left_trigger=0;model();{unsigned before=fires;fire_in_epoch();assert(fires==before+1);}
    /* Partial pull and release: handle springs back, nothing committed. */
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED);
    float back[3]={-gun.c[0][0],-gun.c[0][1],-gun.c[0][2]};
    left_at(handle_seat.p,.02f+back[0]*.02f,back[1]*.02f,back[2]*.02f);model();assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED);
    SenseArmNode h=node_at(OPHANDLE);for(unsigned k=0;k<3;k++)near(h.p[k],handle_seat.p[k]+back[k]*.02f/3.048f); /* real node motion */
    {unsigned before=fires;fire_in_epoch();assert(fires==before);} /* not ready while drawn pulled */
    left_trigger=0;model();assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&physical_commits==commits);
    model();assert(!memcmp(&handle_seat,GPTR(0x41000+OPHANDLE*52),52));
    /* Full pull (6 cm hand, 5 cm travel): clamped offset; release commits once. */
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();
    left_at(handle_seat.p,.02f+back[0]*.06f,back[1]*.06f,back[2]*.06f);model();assert(sense_mag.state.stage==SENSE_MAG_CHARGE_PULLED);
    h=node_at(OPHANDLE);for(unsigned k=0;k<3;k++)near(h.p[k],handle_seat.p[k]+back[k]*.05f/3.048f);
    for(unsigned i=0;i<NODES;i++)if(i!=OPHANDLE&&i!=LWRIST&&i!=LUP&&i!=LFORE)assert(!memcmp(seated_nodes+i*52,GPTR(0x41000+i*52),52)); /* only the handle */
    /* Release seen in the same epoch changes nothing; the next epoch commits. */
    left_trigger=0;memcpy(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes);model_in_epoch();
    assert(!memcmp(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes)&&physical_commits==commits&&sense_mag.state.stage==SENSE_MAG_CHARGE_PULLED);
    model();assert(physical_commits==commits+1&&physical_last_action==HALO_SENSE_GESTURE_PHYSICAL_RELOAD&&
                   physical_last_owner==0x23450000&&physical_last_target==0x34560001&&sense_mag.state.stage==SENSE_MAG_COMMITTED);
    model();model();assert(physical_commits==commits+1);
    assert(G16(0x162B6)==120&&G16(0x162B8)==10&&G16(0x162B0)==0); /* ammo untouched: the original decides */
    /* Original reload confirms; its animation moving the handle is pinned,
     * not shown as a second charge. */
    S16(0x162B0,1);model();assert(mstatus().magazine_result==HALO_SENSE_MAGAZINE_CONFIRMED);
    {float saved[3];memcpy(saved,rig_local[OPHANDLE],12);rig_local[OPHANDLE][0]-=.04f/3.048f;rig_write();model();
     h=node_at(OPHANDLE);for(unsigned k=0;k<3;k++)near(h.p[k],handle_seat.p[k]);memcpy(rig_local[OPHANDLE],saved,12);rig_write();}
    S16(0x162B0,0);model();
    /* R2 during the charge cancels it without a commit. */
    commits=physical_commits;to_new();left_at(mag_seat.p,.04f,0,0);model();left_trigger=0;model();
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();right_trigger=1;model();
    assert(sense_mag.state.stage==SENSE_MAG_IDLE&&physical_commits==commits);right_trigger=0;left_trigger=0;model();
    /* Transport busy at release: back to inserted, charge again. */
    to_new();left_at(mag_seat.p,.04f,0,0);model();left_trigger=0;model();
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();
    left_at(handle_seat.p,.02f+back[0]*.06f,back[1]*.06f,back[2]*.06f);model();physical_accept=0;left_trigger=0;model();
    assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&mstatus().magazine_result==HALO_SENSE_MAGAZINE_BUSY&&physical_commits==commits+1);
    physical_accept=1;right_trigger=1;model();right_trigger=0;model();
    /* No verified handle: insertion commits at once (normal behaviour). */
    strcpy(GPTR(0x32000+OPHANDLE*64),"frame ophandel");model();assert(!mstatus().charge_supported);
    commits=physical_commits;to_new();left_at(mag_seat.p,.04f,0,0);model();
    assert(physical_commits==commits+1&&sense_mag.state.stage==SENSE_MAG_COMMITTED);
    S16(0x162B0,1);model();S16(0x162B0,0);left_trigger=0;model();rig_write();
    /* Charging setting off: insertion commits as before. */
    halo_sense_set_physical_charge(0);model();commits=physical_commits;to_new();left_at(mag_seat.p,.04f,0,0);model();
    assert(physical_commits==commits+1);S16(0x162B0,1);model();S16(0x162B0,0);left_trigger=0;model();
    halo_sense_set_physical_magazine(0);model();
    puts("PASS: charging phase: real frame ophandle subtree pulled (clamped to travel) and released, one original reload only after the full sequence, none before insertion/partial/R2/busy, ammo untouched, original reload's handle motion pinned, same-epoch freeze, unsupported/off fall back to commit on insertion.");
}
/* Coordinator repro (charge-body-translation-probe.c) and rigid-motion
 * variants: the charging pull is the hand relative to the weapon. Moving or
 * turning body, weapon and hand together never pulls; moving the hand back
 * relative to the weapon does. Release commits only if still past the
 * return share at that epoch. */
static void move_body(float dx,float dy,float dz,float yaw){
    float base[3]={10+dx,20+dy,30+dz},cam[9]={10+dx,20+dy,30.6f+dz,cosf(yaw),sinf(yaw),0,0,0,1};
    memcpy(GPTR(0x1505C),base,12);memcpy(GPTR(0x50004),cam,36);
}
static float handle_offset_m(const SenseArmNode *seat_in_gun_frame){
    SenseArmNode g=node_at(GUN),h=node_at(OPHANDLE);HaloSenseTransform gt;memcpy(gt.c,g.c,36);memcpy(gt.p,g.p,12);
    float rel[3]={h.p[0]-g.p[0],h.p[1]-g.p[1],h.p[2]-g.p[2]},local[3];sense_unrotate(&gt,rel,local);
    return (seat_in_gun_frame->p[0]-local[0])*3.048f; /* backward along the gun, metres */
}
static void charge_rigid_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);custom_positions=1;mag_fixture(0,120,10);
    memcpy(grip_positions[1],right_hand,12);halo_sense_set_physical_magazine(1);halo_sense_set_physical_charge(1);physical_accept=1;
    left_trigger=right_trigger=0;model();mag_seat=node_at(MAG);SenseArmNode handle_seat=node_at(OPHANDLE),gun=node_at(GUN);
    SenseArmNode seat_local;{HaloSenseTransform gt;memcpy(gt.c,gun.c,36);memcpy(gt.p,gun.p,12);float rel[3]={handle_seat.p[0]-gun.p[0],handle_seat.p[1]-gun.p[1],handle_seat.p[2]-gun.p[2]};sense_unrotate(&gt,rel,seat_local.p);}
    #define INSERT_AND_GRIP() do{to_new();left_at(mag_seat.p,.04f,0,0);model();left_trigger=0;model();left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model(); \
        assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED);}while(0)
    float grip[3];
    /* Body (and camera) 6 cm back along the gun, controllers unchanged. */
    unsigned commits=physical_commits;INSERT_AND_GRIP();memcpy(grip,grip_positions[0],12);
    move_body(-gun.c[0][0]*.06f/3.048f,-gun.c[0][1]*.06f/3.048f,-gun.c[0][2]*.06f/3.048f,0);model();
    printf("charge rigid translate: stage=%d offset=%.6f m\n",sense_mag.state.stage,handle_offset_m(&seat_local));
    assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED&&fabsf(handle_offset_m(&seat_local))<1e-4f);
    left_trigger=0;model();assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&physical_commits==commits);
    /* Body turned 30 degrees and moved: weapon and hand rotate together. */
    move_body(0,0,0,0);model();left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();
    assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED);memcpy(grip,grip_positions[0],12);
    for(int i=1;i<=6;i++){move_body(.01f*(float)i,-.005f*(float)i,0,.0873f*(float)i);model();
        assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED&&fabsf(handle_offset_m(&seat_local))<1e-4f);}
    printf("charge rigid rotate+translate: stage=%d offset=%.6f m\n",sense_mag.state.stage,handle_offset_m(&seat_local));
    /* Now a real pull relative to the weapon (in the turned body): the hand
     * moves back along the controller/weapon forward by 6 cm. */
    for(unsigned k=0;k<3;k++)grip_positions[0][k]=grip[k];grip_positions[0][0]-=.06f;model();
    printf("charge relative pull: stage=%d offset=%.6f m\n",sense_mag.state.stage,handle_offset_m(&seat_local));
    assert(sense_mag.state.stage==SENSE_MAG_CHARGE_PULLED&&fabsf(handle_offset_m(&seat_local)-.05f)<1e-4f); /* clamped to travel */
    /* Same epoch: another build, even after release, draws the same and commits nothing. */
    left_trigger=0;memcpy(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes);model_in_epoch();
    assert(!memcmp(epoch_nodes,GPTR(0x41000),sizeof epoch_nodes)&&physical_commits==commits);
    model();assert(physical_commits==commits+1&&sense_mag.state.stage==SENSE_MAG_COMMITTED); /* one original reload */
    model();model();assert(physical_commits==commits+1);
    S16(0x162B0,1);model();S16(0x162B0,0);move_body(0,0,0,0);memcpy(grip_positions[0],grip,12);model();
    /* Partial pull (60% < 80%): release springs back, nothing committed. */
    commits=physical_commits;INSERT_AND_GRIP();memcpy(grip,grip_positions[0],12);
    grip_positions[0][0]-=.03f;model();assert(sense_mag.state.stage==SENSE_MAG_CHARGE_GRIPPED&&fabsf(handle_offset_m(&seat_local)-.03f)<1e-4f);
    left_trigger=0;model();assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&physical_commits==commits);
    /* Full pull, then pushed forward below the return share and released in
     * the same epoch: the current pull decides first, so no commit. */
    left_at(handle_seat.p,.02f,0,0);model();left_trigger=1;model();model();memcpy(grip,grip_positions[0],12);
    grip_positions[0][0]-=.06f;model();assert(sense_mag.state.stage==SENSE_MAG_CHARGE_PULLED);
    grip_positions[0][0]+=.05f;left_trigger=0;model();assert(sense_mag.state.stage==SENSE_MAG_INSERTED&&physical_commits==commits);
    /* Full pull and release in one epoch still charges. */
    memcpy(grip_positions[0],grip,12);left_trigger=1;model();model();grip_positions[0][0]-=.06f;left_trigger=0;model();
    assert(physical_commits==commits+1&&sense_mag.state.stage==SENSE_MAG_COMMITTED);
    S16(0x162B0,1);model();S16(0x162B0,0);model();
    /* The gun, wrist and shot are never moved by any of this. */
    move_body(0,0,0,0);memcpy(grip_positions[0],grip,12);model();
    halo_sense_set_physical_charge(0);halo_sense_set_physical_magazine(0);model();
    puts("PASS: charging is weapon-relative: rigid body translation and rotation of weapon+hand never pull; relative pulls do (clamped), partial or returned-before-release pulls never commit, same-epoch builds identical, one original reload per completed charge.");
}
/* Pickup render qualification (pickup-epoch-lease-probe.c and variants):
 * the flight palette is applied only while this outer epoch's live accepted
 * frame for the published unit/weapon/session/generation exists. A relevant
 * draw that falls back keeps the rest of its epoch on the original palette;
 * the next epoch recovers. Unrelated draws never latch it, a taken target is
 * drawn normally at once, and the getter never changes the accepted frame. */
static int pickup_prepare(uint32_t ret,uint32_t flags,uint32_t object){
    EngineCPU c={.fp_control=0x37F};c.gpr[4]=0x27000;S32(0x27000,ret);putf(0x27004,1);S32(0x27018,0x27100);S32(0x27024,object);S32(0x2702C,flags);
    c.gpr[0]=0x45670004;c.gpr[1]=0x18340;SenseWorldModelOverride out={0};
    unsigned char before[sizeof sense_accepted],guest[0x400];memcpy(before,&sense_accepted,sizeof before);memcpy(guest,GPTR(0x18000),sizeof guest);
    int r=sense_pickup_model_prepare(&c,&out);
    assert(!memcmp(before,&sense_accepted,sizeof before)); /* accepted aim cache untouched */
    assert(!memcmp(guest,GPTR(0x18000),sizeof guest)&&c.gpr[1]==0x18340&&G32(0x27018)==0x27100); /* no object/CPU/stack writes */
    assert(r==SENSE_WORLD_MODEL_NONE||r==SENSE_WORLD_MODEL_APPLY);
    return r;
}
static int both_passes(void){
    int normal=pickup_prepare(0x0050F049,0,0x56780003),shadow=pickup_prepare(0x0050F0BD,2,0x56780003);
    assert(normal==shadow); /* eye/shadow calls of one epoch agree */
    return normal==SENSE_WORLD_MODEL_APPLY;
}
static void to_flying(void){
    left_trigger=0;memcpy(grip_positions[0],(float[3]){.3f,.2f,-.3f},12);pepoch(0);pepoch(0);left_trigger=1;pepoch(0);
    memcpy(grip_positions[0],(float[3]){.1f,.15f,-.2f},12);pepoch(0);pepoch(50000000);
    assert(sense_pick.state.stage==SENSE_PICKUP_FLYING&&sense_pick_render.active);
}
static void pickup_render_epoch_tests(void){
    rig_setup();float identity[4]={0,0,0,1};memcpy(GPTR(0x36010),identity,16);mag_fixture(0,120,10);custom_positions=1;
    float hands[2][3]={{.3f,.2f,-.3f},{.5f,-.2f,-.3f}};memcpy(grip_positions,hands,sizeof hands);model();
    world_weapon(UINT32_MAX);halo_sense_set_physical_pickup(1);physical_accept=1;physical_pending=1;
    pickup_render_fixture();S32(0x18000,0x45670003);S16(0x12028,6);S32(0x12024,0x56780003);
    to_flying();
    /* Valid: this epoch's accepted frame; normal and shadow apply, and the
     * original 004D6FC0 draws the moved palette for both. */
    assert(both_passes());draw_object(0x56780003,0x18000,0x0050F049,0);unsigned char moved[3*52];memcpy(moved,drawn_palette,sizeof moved);
    draw_object(0x56780003,0x18000,0x0050F0BD,2);assert(!memcmp(moved,drawn_palette,sizeof moved));
    /* Exact repro: next outer boundary, no admission yet -> original palette. */
    boundary();assert(!both_passes());
    {SenseAcceptedWeaponFrame f;assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&f));}
    assert(sense_pick.state.stage==SENSE_PICKUP_FLYING); /* rendering fallback is not a transaction cancel */
    /* Same epoch: admission and a fresh publication later never resurrect it. */
    model_in_epoch();{SenseAcceptedWeaponFrame f;assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&f));}
    assert(!both_passes());draw_object(0x56780003,0x18000,0x0050F049,0);assert(memcmp(moved,drawn_palette,sizeof moved));
    /* Next valid epoch recovers. */
    pepoch(0);assert(both_passes());
    /* Transient invalidations within an epoch latch it; the next epoch recovers. */
    for(int reason=0;reason<6;reason++){
        if(sense_pick.state.stage!=SENSE_PICKUP_FLYING&&sense_pick.state.stage!=SENSE_PICKUP_COMMITTED){pepoch(0);to_flying();}
        pepoch(0);assert(both_passes());
        if(reason==0)input_generation++;else if(reason==1)input_session++;else if(reason==2)fresh=0;
        else if(reason==3)app_blocked=1;else if(reason==4)S8(0x718FC9,1);else now+=200000000; /* absolute age > 150 ms */
        assert(!both_passes());
        if(reason==0)input_generation--;else if(reason==1)input_session--;else if(reason==2)fresh=1;
        else if(reason==3)app_blocked=0;else if(reason==4)S8(0x718FC9,0);
        assert(!both_passes()); /* restored in the same epoch: still the original palette */
        if(reason==5)now-=200000000;
        printf("pickup render qualification reason=%d: lost and same-epoch restore fall back\n",reason);
    }
    /* Unit or selected primary weapon change. */
    for(int which=0;which<2;which++){
        pepoch(0);if(sense_pick.state.stage!=SENSE_PICKUP_FLYING&&sense_pick.state.stage!=SENSE_PICKUP_COMMITTED)to_flying();
        assert(both_passes());
        if(which)S32(0x152F8,0x99990001);else S32(0x12034,0x23460000);
        assert(!both_passes());
        if(which)S32(0x152F8,0x34560001);else S32(0x12034,0x23450000);
        assert(!both_passes());
    }
    /* Unrelated draws (other caller, other object) never latch pickup; a
     * target the unit now owns is drawn normally at once without latching. */
    pepoch(0);if(sense_pick.state.stage!=SENSE_PICKUP_FLYING&&sense_pick.state.stage!=SENSE_PICKUP_COMMITTED)to_flying();
    assert(pickup_prepare(0x00401234,0,0x56780003)==SENSE_WORLD_MODEL_NONE);
    assert(pickup_prepare(0x0050F049,0,0x56790003)==SENSE_WORLD_MODEL_NONE);
    assert(pickup_prepare(0x0050F0BD,0,0x56780003)==SENSE_WORLD_MODEL_NONE); /* shadow caller without shadow flag */
    assert(both_passes());
    S32(0x1811C,0x23450000);assert(!both_passes());S32(0x1811C,UINT32_MAX);assert(both_passes());
    /* A new epoch with no update (no first-person build): the old stamp never applies. */
    boundary();boundary();assert(!both_passes());
    left_trigger=0;pepoch(0);pepoch(0);halo_sense_set_physical_pickup(0);pepoch(0);assert(!both_passes());
    S16(0x13020,2);S32(0x18000,0x45670001);
    puts("PASS: pickup flight render qualified by this epoch's live accepted frame (unit/weapon/session/generation): pre-admission, generation/session/freshness/age/app/menu/unit/weapon losses fall back for the rest of the epoch, next epoch recovers; normal and shadow agree; unrelated draws and an owned target never latch; accepted frame, object, CPU and stack untouched.");
}
static void pure_solver(void){
    SenseArmNode up=arm_node(0,0,0),fo=arm_node(.07f,0,-.0714143f),wr=arm_node(.17f,0,-.0714143f),target,ou,of;float down[3]={0,0,-1};
    /* Coordinator repro (arm-adjacent-probe.c): a hand crossing the
     * authored elbow's collinearity must not flip the elbow. */
    {SenseArmNode u=arm_node(0,0,0),f=arm_node(.5f,.5f,0),w=arm_node(1,0,0),t[2],F[2],U;float pole[3]={0,0,-1};int r[2];
     for(int i=0;i<2;i++){float ang=.785398163f+(i?.002f:-.002f);t[i]=arm_node(.8f*cosf(ang),.8f*sinf(ang),0);r[i]=sense_arm_solve(&u,&f,&w,&t[i],pole,&U,&F[i]);}
     assert(r[0]==SENSE_ARM_SOLVED&&r[1]==SENSE_ARM_SOLVED);float td=dist(t[0].p,t[1].p),ed=dist(F[0].p,F[1].p);
     printf("repro collinearity: target_delta=%g elbow_delta=%g\n",td,ed);assert(ed<=2*td);
     for(int i=0;i<2;i++){t[i]=arm_node(1.41421356f-(i?.00005f:.00025f),0,0);r[i]=sense_arm_solve(&u,&f,&w,&t[i],pole,&U,&F[i]);}
     assert(r[0]==SENSE_ARM_SOLVED&&r[1]==SENSE_ARM_SOLVED&&dist(F[0].p,F[1].p)<=4*dist(t[0].p,t[1].p)+1e-6f);
     printf("repro extension: both solved, elbow_delta=%g\n",dist(F[0].p,F[1].p));
     /* Sweep across that collinearity in 3D, in fine steps. */
     Sweep sw={0};for(int i=0;i<=4000;i++){float ang=.6f+.4f*(float)i/4000.f;SenseArmNode tt=arm_node(.8f*cosf(ang),.8f*sinf(ang),.05f*sinf(3*ang));sweep_step(&sw,&u,&f,&w,&tt,6);}
     assert(sw.solved==4001&&!sw.transitions);printf("collinearity sweep worst ratio=%g\n",sw.worst);}
    /* Angular sweeps at several radii, including inside the reach knee,
     * across full extension and past it: continuity wherever solved. */
    float total=dist(up.p,fo.p)+dist(fo.p,wr.p);float worst=0;
    const float radii[]={.2f,.5f,.8f,.94f,.96f,.99f,1.0f,1.02f,1.08f,1.11f};
    for(unsigned r=0;r<sizeof radii/sizeof *radii;r++)for(unsigned plane=0;plane<3;plane++){
        Sweep sw={0};for(int i=0;i<=6283;i++){float ang=(float)i*.001f,e=(float)plane*.6f-.6f;
            SenseArmNode tt=arm_node(radii[r]*total*cosf(ang)*cosf(e),radii[r]*total*sinf(ang)*cosf(e),radii[r]*total*sinf(e));
            float roll=ang*.5f;tt.c[2][0]=0;tt.c[2][1]=-sinf(roll);tt.c[2][2]=cosf(roll);tt.c[1][0]=0;tt.c[1][1]=cosf(roll);tt.c[1][2]=sinf(roll);
            sweep_step(&sw,&up,&fo,&wr,&tt,6);}
        if(sw.worst>worst)worst=sw.worst;
        if(radii[r]<=1.02f)assert(sw.solved>3000);
    }
    printf("angular sweeps worst ratio=%g\n",worst);
    /* Radial in/out through full extension and the folded limit: no snap,
     * and leaving/re-entering is hysteretic (exit farther than re-entry). */
    {Sweep sw={0};float exit_d=0,enter_d=0;int was=-1;
     for(int pass=0;pass<2;pass++)for(int i=0;i<=3000;i++){float t=pass?1.f-(float)i/3000.f:(float)i/3000.f;float dd=total*(.05f+1.3f*t);
        SenseArmNode tt=arm_node(dd*.8f,dd*.36f,-dd*.48f);sweep_step(&sw,&up,&fo,&wr,&tt,8);
        if(was==1&&!sw.held&&!pass)exit_d=dd;if(was==0&&sw.held&&pass)enter_d=dd;was=sw.held;}
     printf("radial exit=%g enter=%g total=%g worst=%g\n",exit_d,enter_d,total,sw.worst);
     assert(exit_d>enter_d&&exit_d>=1.1f*total&&enter_d>=total);}
    /* Randomized reachable targets with arbitrary wrist bases. */
    unsigned solved=0,unreachable=0;
    for(unsigned i=0;i<2000;i++){
        float r1=(float)((i*7919u)%1000u)/1000.f,r2=(float)((i*104729u)%1000u)/1000.f,r3=(float)((i*1299709u)%1000u)/1000.f;
        target=wr;target.p[0]=r1*.3f-.05f;target.p[1]=r2*.3f-.15f;target.p[2]=r3*.3f-.2f;
        float yaw=r2*3.f,roll=r3*2.f-1.f;target.c[0][0]=cosf(yaw);target.c[0][1]=sinf(yaw);target.c[0][2]=0;
        target.c[2][0]=sinf(roll)*sinf(yaw);target.c[2][1]=-sinf(roll)*cosf(yaw);target.c[2][2]=cosf(roll);
        sense_cross(target.c[2],target.c[0],target.c[1]);
        memset(&ou,0x7F,52);memset(&of,0x7F,52);SenseArmNode su=ou,sf=of;float shift=0;
        int result=sense_arm_solve_held(&up,&fo,&wr,&target,down,0,&ou,&of,&shift);
        if(result!=SENSE_ARM_SOLVED){assert(!memcmp(&ou,&su,52)&&!memcmp(&of,&sf,52));unreachable+=result==SENSE_ARM_UNREACHABLE;continue;}
        solved++;float a=dist(up.p,fo.p),c=dist(fo.p,wr.p);
        if(dist(up.p,target.p)<=SENSE_ARM_KNEE*total&&dist(up.p,target.p)>=fabsf(a-c)+(1-SENSE_ARM_KNEE)*total)assert(!memcmp(ou.p,up.p,12)&&shift==0);
        assert(fabsf(dist(ou.p,of.p)-a)<2e-4f*a&&fabsf(dist(of.p,target.p)-c)<2e-4f*c&&ou.scale==1&&of.scale==1);
        for(unsigned m=0;m<3;m++){float l=sqrtf(sense_dot(of.c[m],of.c[m]));assert(fabsf(l-1)<1e-4f);}
        float mapped[3];sense_arm_apply((float(*)[3])(float[3][3]){{ou.c[0][0],ou.c[1][0],ou.c[2][0]},{ou.c[0][1],ou.c[1][1],ou.c[2][1]},{ou.c[0][2],ou.c[1][2],ou.c[2][2]}},fo.p,mapped);
        for(unsigned k=0;k<3;k++)assert(fabsf(mapped[k]+ou.p[k]-of.p[k])<1e-4f);
    }
    assert(solved>200&&unreachable>200);
    /* Invalid input never writes. */
    target=wr;target.p[1]=NAN;assert(sense_arm_solve(&up,&fo,&wr,&target,down,&ou,&of)==SENSE_ARM_DEGENERATE);
    SenseArmNode zero=fo;memcpy(zero.p,up.p,12);assert(sense_arm_solve(&up,&zero,&wr,&wr,down,&ou,&of)==SENSE_ARM_DEGENERATE);
    float nan_pole[3]={NAN,0,0};assert(sense_arm_solve(&up,&fo,&wr,&wr,nan_pole,&ou,&of)==SENSE_ARM_DEGENERATE);
    SenseArmNode collapsed=up;collapsed.scale=0;assert(sense_arm_solve(&collapsed,&fo,&wr,&wr,down,&ou,&of)==SENSE_ARM_DEGENERATE);
    /* Straight authored chain uses the fallback pole; a pole along that
     * chain is degenerate rather than an arbitrary flip. */
    SenseArmNode sf=arm_node(.1f,0,0),sw2=arm_node(.2f,0,0);target=arm_node(.15f,.02f,-.05f);
    assert(sense_arm_solve(&up,&sf,&sw2,&target,down,&ou,&of)==SENSE_ARM_SOLVED);
    float along[3]={1,0,0};assert(sense_arm_solve(&up,&sf,&sw2,&target,along,&ou,&of)==SENSE_ARM_DEGENERATE);
    /* Hand directly opposite the authored reach: unreachable, never flipped. */
    target=arm_node(-.12f,0,.05f);assert(sense_arm_solve(&up,&fo,&wr,&target,down,&ou,&of)==SENSE_ARM_UNREACHABLE);
}
int main(void){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);setenv("HALO_SENSE_WEAPON","1",1);setenv("HALO_SENSE_PHYSICAL_TRACE","1",1);
    pure_solver();
    float identity[4]={0,0,0,1};
    rig_setup();memcpy(GPTR(0x36010),identity,16);custom_positions=1;
    /* Right hand slightly forward/down, left hand in front: both reachable. */
    float hands[2][3]={{.45f,.05f,-.35f},{.5f,-.2f,-.3f}};memcpy(grip_positions,hands,sizeof hands);
    capture_off();
    HaloSensePhysicalStatus st;assert(host_sense_physical_status(&st)&&!st.arms_enabled&&st.arm[0]==HALO_SENSE_ARM_OFF&&st.arm[1]==HALO_SENSE_ARM_OFF);
    /* Disabled: build118 collapse. Upper arms hidden, forearms collapsed at the wrist. */
    for(unsigned i=LUP;i<=RFORE;i++)assert(G32(0x41000+i*52)==0);
    /* Enabled: connected chains with authored lengths, original shoulders,
     * tracked wrists; weapon/reticle/shot byte-identical. */
    halo_sense_set_connected_arms(1);
    halo_sense_set_connected_arms(0);model();assert_identical_to_off();halo_sense_set_connected_arms(1);
    model();assert(host_sense_physical_status(&st)&&st.arms_enabled&&st.arm[0]==HALO_SENSE_ARM_CONNECTED&&st.arm[1]==HALO_SENSE_ARM_CONNECTED);
    unsigned char on_nodes[NODES*52];memcpy(on_nodes,GPTR(0x41000),sizeof on_nodes);
    /* Original animated shoulders: re-run disabled original to read them. */
    halo_sense_set_connected_arms(0);active=0;model();SenseArmNode orig[NODES];for(unsigned i=0;i<NODES;i++)orig[i]=node_at(i);active=1;
    memcpy(GPTR(0x41000),on_nodes,sizeof on_nodes);halo_sense_set_connected_arms(1);
    const unsigned up_i[2]={LUP,RUP},fo_i[2]={LFORE,RFORE},wr_i[2]={LWRIST,RWRIST};
    for(unsigned h=0;h<2;h++){
        SenseArmNode u=node_at(up_i[h]),f=node_at(fo_i[h]),w=node_at(wr_i[h]);
        assert(u.scale==1&&f.scale==1&&w.scale==1);
        assert(!memcmp(u.p,orig[up_i[h]].p,12)); /* shoulder root is the original animation's */
        float a=dist(orig[up_i[h]].p,orig[fo_i[h]].p),c=dist(orig[fo_i[h]].p,orig[wr_i[h]].p);
        assert(fabsf(dist(u.p,f.p)-a)<1e-4f*a+1e-6f&&fabsf(dist(f.p,w.p)-c)<1e-4f*c+1e-6f);
        /* Elbow keeps the authored side (original elbow pole). */
        float line[3],old_offset[3],new_offset[3];for(unsigned k=0;k<3;k++){line[k]=w.p[k]-u.p[k];old_offset[k]=orig[fo_i[h]].p[k]-u.p[k];new_offset[k]=f.p[k]-u.p[k];}
        assert(sense_normalize(line));float pa[3],pb[3];
        for(unsigned k=0;k<3;k++){pa[k]=old_offset[k]-sense_dot(old_offset,line)*line[k];pb[k]=new_offset[k]-sense_dot(new_offset,line)*line[k];}
        assert(sense_dot(pa,pb)>0);
    }
    assert_weapon_identical();
    /* Deterministic: the same original frame and tracked pose gives the same
     * bytes (panorama builds this once per source view for both eyes). */
    model();assert(!memcmp(on_nodes,GPTR(0x41000),sizeof on_nodes));
    /* Out of reach: that arm keeps the exact build118 collapse; gun unchanged. */
    float far[2][3]={{.45f,.05f,-.35f},{1.2f,-.2f,-.3f}};memcpy(grip_positions,far,sizeof far);
    capture_off();halo_sense_set_connected_arms(1);model();assert(host_sense_physical_status(&st)&&st.arm[1]==HALO_SENSE_ARM_OUT_OF_REACH&&st.arm[0]==HALO_SENSE_ARM_CONNECTED);
    assert(!memcmp(off_nodes+RUP*52,GPTR(0x41000+RUP*52),52)&&!memcmp(off_nodes+RFORE*52,GPTR(0x41000+RFORE*52),52));
    assert_weapon_identical();
    memcpy(grip_positions,hands,sizeof hands);
    /* Unknown rig names or parents fail closed into the exact disabled bytes. */
    capture_off();strcpy(GPTR(0x32000+RFORE*64),"frame r forarm");strcpy(GPTR(0x32000+LFORE*64),"frame l forarm");
    halo_sense_set_connected_arms(0);model();memcpy(off_nodes,GPTR(0x41000),sizeof off_nodes);
    halo_sense_set_connected_arms(1);model();assert_identical_to_off();
    assert(host_sense_physical_status(&st)&&st.arm[0]==HALO_SENSE_ARM_UNKNOWN_RIG&&st.arm[1]==HALO_SENSE_ARM_UNKNOWN_RIG);
    rig_write();S16(0x32000+RWRIST*64+0x24,LFORE);halo_sense_set_connected_arms(0);model();memcpy(off_nodes,GPTR(0x41000),sizeof off_nodes);
    halo_sense_set_connected_arms(1);model();assert(host_sense_physical_status(&st)&&st.arm[1]==HALO_SENSE_ARM_UNKNOWN_RIG);
    rig_write();
    /* Degenerate authored chain (zero forearm) fails closed too. */
    float saved=rig_local[RWRIST][0];rig_local[RWRIST][0]=0;rig_write();capture_off();halo_sense_set_connected_arms(1);model();
    assert(host_sense_physical_status(&st)&&st.arm[1]==HALO_SENSE_ARM_DEGENERATE);
    assert(!memcmp(off_nodes+RUP*52,GPTR(0x41000+RUP*52),52)&&!memcmp(off_nodes+RFORE*52,GPTR(0x41000+RFORE*52),52));
    rig_local[RWRIST][0]=saved;rig_write();
    /* Real 004D6880 sweep: the right hand moves forward through full
     * extension and back in 2 mm steps. Gun/wrist nodes and the final shot
     * equal the disabled path frame by frame; the arm never snaps. */
    {enum{STEPS=300};static unsigned char off_gun[STEPS+1][4][52];static float off_shot[STEPS+1][6];
     for(int pass=0;pass<2;pass++){halo_sense_set_connected_arms(pass);Sweep sw={0};SenseArmNode prev_u={0},prev_f={0},prev_w={0};int prev=0;
        unsigned connected=0,out=0,changes=0;int last=-1;
        for(int i=0;i<=STEPS;i++){float t=i<=STEPS/2?(float)i/(STEPS/2):(float)(STEPS-i)/(STEPS/2);
            float pos[2][3]={{.45f,.05f,-.35f},{.40f+.45f*t,-.2f,-.3f}};memcpy(grip_positions,pos,sizeof pos);model();
            unsigned keep[4]={RWRIST,GUN,MAG,LWRIST};
            unsigned before=fires;fire();assert(fires==before+1);
            if(!pass){for(unsigned k=0;k<4;k++)memcpy(off_gun[i][k],GPTR(0x41000+keep[k]*52),52);memcpy(off_shot[i],observed_origin,12);memcpy(off_shot[i]+3,observed_forward,12);continue;}
            for(unsigned k=0;k<4;k++)assert(!memcmp(off_gun[i][k],GPTR(0x41000+keep[k]*52),52));
            assert(!memcmp(off_shot[i],observed_origin,12)&&!memcmp(off_shot[i]+3,observed_forward,12));
            HaloSensePhysicalStatus st;assert(host_sense_physical_status(&st));
            if(st.arm[1]!=last){changes++;last=st.arm[1];}
            SenseArmNode u=node_at(RUP),f=node_at(RFORE),w=node_at(RWRIST);
            if(st.arm[1]==HALO_SENSE_ARM_CONNECTED){connected++;
                if(prev){float step=dist(prev_w.p,w.p);assert(dist(prev_f.p,f.p)<=8*step+1e-5f&&dist(prev_u.p,u.p)<=8*step+1e-5f);}
                prev=1;prev_u=u;prev_f=f;prev_w=w;}
            else{out++;prev=0;assert(st.arm[1]==HALO_SENSE_ARM_OUT_OF_REACH);assert(G32(0x41000+RUP*52)==0&&G32(0x41000+RFORE*52)==0);}
        }
        if(pass){printf("rig sweep: connected=%u out=%u state_changes=%u\n",connected,out,changes);assert(connected>100&&out>20&&changes==3);}
        (void)sw;}
     halo_sense_set_connected_arms(1);memcpy(grip_positions,hands,sizeof hands);
     /* Hysteresis identity: a new input generation forgets 'held'. */
     model();assert(sense_arm_identity.held[1]);input_generation++;model();assert(sense_arm_identity.generation==input_generation);input_generation--;}
    /* Melee keeps the complete original animated rig: arm code never runs. */
    capture_off();halo_sense_set_connected_arms(1);active=0;model();unsigned char original_nodes[NODES*52];memcpy(original_nodes,GPTR(0x41000),sizeof original_nodes);active=1;
    S8(0x15289,1);model();assert(!memcmp(original_nodes,GPTR(0x41000),sizeof original_nodes)&&!sense_muzzle.valid);S8(0x15289,0);
    /* Mirrored offhand pass never solves arms (reflected shoulders unqualified). */
    S16(0x13020,3);S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);
    memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);S32(0x152FC,0x45670002);
    sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;dual_enabled=1;
    halo_sense_set_connected_arms(0);sense_model_hand=0;model();memcpy(off_nodes,GPTR(0x41000),sizeof off_nodes);
    halo_sense_set_connected_arms(1);model();assert_identical_to_off();
    sense_model_hand=1;dual_enabled=0;
    /* Stale tracking: status expires; no arm writes without a tracked pair. */
    fresh=0;model();for(unsigned i=0;i<NODES;i++)assert(G32(0x41000+i*52)==0);fresh=1;
    now+=300000000;assert(!host_sense_physical_status(&st)&&st.arm[0]==0&&st.arm[1]==0);now-=300000000;
    halo_sense_set_connected_arms(0);
    magazine_tests();
    magazine_epoch_tests();
    charge_tests();
    charge_rigid_tests();
    support_aim_authority();
    pickup_blend_tests();
    delivery_and_pickup_tests();
    pickup_render_epoch_tests();
    puts("PASS: connected arms on original 004D6880 rig: authored lengths, original shoulders/elbow side, tracked wrists; gun/reticle/final shot byte-identical; disabled/unknown/degenerate/out-of-reach/mirrored fail closed; melee original priority.");
    return 0;
}
