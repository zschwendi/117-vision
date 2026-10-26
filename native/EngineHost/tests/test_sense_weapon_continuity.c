/* Native integration of the production weapon hooks around the authoritative
 * accepted frame caches (sense_weapon_frame.inc). One immutable primary frame
 * and one separately keyed offhand frame per outer epoch feed the viewmodel,
 * reticle, scope camera and the final retail pre-spread shot ray (004593B0
 * from 004C500F). Also covers grip continuity, optional scoped smoothing and
 * semantic shot/charge haptics. Original routines are mocked at their call
 * boundaries: no generated engine source, device, save or game payload. */
#include "../host.h"
#include <assert.h>
#include <stdarg.h>
#include "../sense_pose.h"
#include "../sense_weapon.h"
#include "../sense_reticle_art.h"
uint8_t *engine_flat_base;uint32_t engine_trace_lo,engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu){(void)cpu;}
void host_log(const char *f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a);}
static int active=1,fresh=1,dual_enabled,panorama_nested,panorama_fp_cache_nested,support_blocked,app_blocked,scope_active;
static int grip_on=1,scope_smooth,weapon_haptics_on=1,read_bump;
static uint64_t input_generation=1,now=1000000000;
static float grip_positions[2][3],hand_forward[2][3]={{1,0,0},{1,0,0}};
bool hostgc_poll(HostGCSnapshot *out){memset(out,0,sizeof *out);return false;}
uint64_t host_monotonic_ns(void){return now;}
int halo_sense_active(void){return active;}
int halo_sense_requested(void){return 1;}
int halo_sense_dual_requested(void){return dual_enabled;}
int halo_sense_grip_continuity(void){return grip_on;}
int halo_sense_scope_smoothing(void){return scope_smooth;}
int halo_sense_weapon_haptics(void){return weapon_haptics_on;}
void halo_sense_set_scope_active(int value){scope_active=value;}
void halo_sense_status(HaloSenseStatus *s){memset(s,0,sizeof *s);s->session=1;s->generation=input_generation;s->active=active;s->requested=1;s->gameplay_supported=1;}
int halo_sense_head_read(HaloSenseHeadPose *h){memset(h,0,sizeof *h);h->forward[0]=1;h->up[2]=1;h->generation=h->controller_generation=1;return 1;}
int halo_sense_app_input_blocked(void){return app_blocked;}
int halo_sense_scope_input(HaloSenseScopeInput *i){memset(i,0,sizeof *i);i->session=1;i->generation=input_generation;i->armed=1;halo_sense_head_read(&i->head);return !app_blocked&&scope_active&&fresh;}
int halo_sense_dual_input(HaloSenseDualInput *p){memset(p,0,sizeof *p);p->session=1;p->generation=input_generation;p->fresh=fresh;p->gameplay_supported=1;
    p->support_blocked=support_blocked;p->armed[0]=p->armed[1]=1;return !app_blocked&&fresh;}
int host_sense_reticle_art_begin(unsigned h,uint32_t w,const HaloSenseReticleFrame*t,float x,float y){(void)h;(void)w;(void)t;(void)x;(void)y;return 0;}
int host_sense_reticle_art_end(int complete){(void)complete;return 1;}
static HaloSenseReticleFrame reticles;
void halo_sense_reticle_publish(const HaloSenseReticleFrame *p){if(p)reticles=*p;else memset(&reticles,0,sizeof reticles);}
int halo_sense_reticle_read(HaloSenseReticleFrame *p){*p=reticles;return fresh&&(p->hands[0].valid||p->hands[1].valid);}
void halo_sense_observe_controller(const HostGCSnapshot *p){(void)p;}
void halo_sense_set_gameplay_supported(int s){(void)s;}
void halo_sense_set_native_controls(int n,uint64_t g){(void)n;(void)g;}
int halo_sense_gameplay_supported(void){return 1;}
/* The publication can change between any two reads; read_bump models a
 * recenter landing between the before-lease and the pair read. */
int halo_sense_read(HaloSenseFrame *frame){memset(frame,0,sizeof *frame);if(!fresh||app_blocked)return 0;
    if(read_bump){read_bump=0;input_generation++;}
    frame->generation=frame->controller_generation=1;
    HaloSenseHandPose *hands[2]={&frame->left,&frame->right};
    for(unsigned h=0;h<2;h++){memcpy(hands[h]->position,grip_positions[h],12);memcpy(hands[h]->forward,hand_forward[h],12);
        hands[h]->up[2]=1;hands[h]->valid=1;}
    return 1;
}
int halo_sense_grenade_accept(uint32_t c,uint32_t t,HaloSenseGrenadeRelease *out){(void)c;(void)t;if(out)memset(out,0,sizeof *out);return 0;}
int halo_sense_grenade_hold(HaloSenseGrenadeHold *out){if(out)memset(out,0,sizeof *out);return 0;} /* no armed hold in this fixture */
uint32_t guest_alloc(uint32_t size){assert(size==0x100);return 0x60000;}
static int collision_hit;
uint32_t host_call_guest(EngineCPU *cpu,uint32_t fn,int n,const uint32_t *args,int pops){
    (void)cpu;assert(fn==0x00505880&&n==5&&pops==0&&args[3]==0x23450000);return collision_hit;
}
static void ret_to_caller(EngineCPU *cpu){cpu->pc=engine_pop(cpu,4);}
/* Physical commit transport lives in halo_settings.c; physical options stay off here. */
int halo_sense_physical_enqueue(uint64_t s,uint64_t g,uint64_t q,int a,uint32_t o,uint32_t t){(void)s;(void)g;(void)q;(void)a;(void)o;(void)t;assert(!"physical options are off in this fixture");return 0;}
int halo_sense_physical_pending(int a){(void)a;return 0;}
void halo_sense_physical_cancel(int a){(void)a;}
void halo_sense_physical_set_delivery_check(int (*c)(int,uint32_t,uint32_t)){(void)c;}
/* Controller, bridge and audio boundaries record exactly what is handed over. */
static HostGCSpatialHaptic haptic_events[512];static unsigned haptic_count,cancel_all,cancel_charge,owner_renews,owner_releases,shot_begins,shot_accepts;
static float haptic_strength=1;static int bridge_owned;static uint64_t bridge_session,bridge_generation,bridge_lease;
int hostgc_play_spatial_haptic(const HostGCSpatialHaptic *e){assert(haptic_count<512);haptic_events[haptic_count++]=*e;return 1;}
void hostgc_cancel_spatial_haptics(int kind){if(kind==HOSTGC_SPATIAL_ALL)cancel_all++;else if(kind==HOSTGC_SPATIAL_CHARGE)cancel_charge++;}
uint64_t hostgc_source_generation(void){return 9;}
uint64_t halo_sense_set_weapon_haptic_owner(int owned,uint64_t s,uint64_t g){
    if(!owned||!weapon_haptics_on){bridge_owned=0;return 0;}
    if(!bridge_owned||s!=bridge_session||g!=bridge_generation)bridge_lease++;
    bridge_owned=1;bridge_session=s;bridge_generation=g;return bridge_lease;
}
int halo_haptics_enabled(void){return 1;}
void halo_haptics_weapon_owner_renew(void){owner_renews++;}
void halo_haptics_weapon_owner_release(void){owner_releases++;}
void halo_haptics_weapon_shot_begin(void){shot_begins++;}
void halo_haptics_weapon_shot_end(int accepted){shot_accepts+=accepted!=0;}
float halo_settings_haptics_strength(void){return haptic_strength;}
/* Body rendering is disabled in the weapon/physical fixtures; its original-engine path has a separate fixture. */
int host_sense_body_active(void){return 0;}
#include "../sense_weapon_hooks.inc"
#include "../dual_wield.inc"
static unsigned fires,rays;static float observed_origin[3],observed_forward[3];
static unsigned shot_projectiles=1;static int shot_escape,shot_mid_generation;
static float scope_origin[3],scope_forward[3];
/* Rest rig the original skeleton routine would produce, in world units:
 * root, right wrist, gun, barrel, left wrist on the foregrip. */
static const float rig[5][3]={{10,20,30},{10.3f,19.8f,30.4f},{10.35f,19.8f,30.4f},{10.5f,19.8f,30.4f},{10.55f,19.8f,30.37f}};
void engine_dispatch(EngineCPU *cpu,uint32_t address){
    cpu->pc=address;if(host_sense_weapon_dispatch(cpu,address))return;
    if(address==0x0050BEA0u){
        if(!host_sense_scope_rendering()&&host_sense_scope_dispatch(cpu,address))return;
        if(host_sense_scope_rendering()){memcpy(scope_origin,GPTR(ARG(0)+4),12);memcpy(scope_forward,GPTR(ARG(0)+16),12);}
        ret_to_caller(cpu);return;
    }
    if(address==0x004D6880u){
        uint32_t output=ARG(0);
        for(unsigned i=0;i<5;i++){float node[13]={1,1,0,0,0,1,0,0,0,1};memcpy(node+10,rig[i],12);memcpy(GPTR(output+i*52),node,52);}
        ret_to_caller(cpu);return;
    }
    if(address==0x005658F0u||address==0x004C6E80u){ret_to_caller(cpu);return;}
    assert(address==0x004C3F10u);fires++;
    EngineCPU child=*cpu;child.gpr[4]=0x23000;S32(0x23000,0x004C4EE2);S32(0x23004,0x23450000);
    S32(0x23008,0x23200);S32(0x2300C,1);S32(0x23010,1);child.gpr[0]=0x23100;child.gpr[3]=0x23120;
    engine_dispatch(&child,0x005658F0);assert(child.pc==0x004C4EE2&&G32(0x2300C)==1&&G32(0x23010)==1);
    if(shot_mid_generation)input_generation++;
    for(unsigned n=0;n<shot_projectiles;n++){
        /* Retail applies tag offsets and body autoaim before this boundary. */
        float shifted[3]={19,-27,43};memcpy(GPTR(0x23100),shifted,12);memcpy(GPTR(0x23120),shifted,12);
        child.gpr[4]=0x23400;S32(0x23400,0x004C500F);S32(0x23404,0x23100);S32(0x23408,0x23120);
        engine_dispatch(&child,0x004593B0u);assert(child.pc==0x004C500F&&child.gpr[0]==UINT32_MAX);
        memcpy(observed_origin,GPTR(0x23100),12);memcpy(observed_forward,GPTR(0x23120),12);rays++;
    }
    if(shot_escape)cpu->pc=0xDEAD;else ret_to_caller(cpu);
}
static void putf(uint32_t p,float f){memcpy(GPTR(p),&f,4);}
static void near(float actual,float wanted,float tolerance){
    if(!(fabsf(actual-wanted)<=tolerance)){fprintf(stderr,"near: %.9g vs %.9g\n",actual,wanted);abort();}
}
static float angle(const float a[3],const float b[3]){float d=sense_dot(a,b);return acosf(fmaxf(-1.f,fminf(1.f,d)));}
static void weapon_path(const char *path){memset(GPTR(0x39000),0,80);strcpy(GPTR(0x39000),path);}
static void tick(void){EngineCPU t={0};t.gpr[4]=0x21000;S32(0x21000,0x401234);engine_dispatch(&t,0x004C6E80);}
/* One main-loop tick, then the outer render boundary: a new epoch. */
static void frame(void){
    now+=UINT64_C(11000000);tick();
    EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,0x50000);S32(0x20008,1);engine_dispatch(&c,0x0050BEA0);
}
static const float right_hand[3]={.5f,-.25f,-.3f},left_hand[3]={.85f,-.25f,-.15f};
static void setup(void){
    memset(engine_flat_base,0,0x900000);
    S32(0x87A478,0x10000);S32(0x87A480,0x11000);S32(0x10004,0x12340000);
    S16(0x11020,1);S16(0x11022,0x200);S32(0x11034,0x12000);S16(0x12000,0x1234);S32(0x12034,0x23450000);
    S32(0x8603B0,0x13000);S16(0x13020,3);S16(0x13022,12);S32(0x13034,0x14000);
    S16(0x14000,0x2345);S16(0x14006,0x550);S32(0x14008,0x15000);S16(0x150B4,0);putf(0x150E0,1);
    S16(0x1400C,0x3456);S16(0x14012,0x340);S32(0x14014,0x16000);S16(0x160B4,2);S32(0x1611C,0x23450000);
    S32(0x1511C,UINT32_MAX); /* retail on-foot biped: no parent (cleared by exit 0056AB50) */
    S16(0x152F0,0xFFFF);S16(0x152F2,0);S16(0x152F4,0);S32(0x152F8,0x34560001);S8(0x15320,0xFF);S8(0x15321,0xFF);
    float origin[3]={10,20,30};memcpy(GPTR(0x1505C),origin,12);
    S32(0x87BC14,0x30000);S32(0x3000C,0x45670000);S32(0x30014,0x31000);S32(0x31068,5);S32(0x3106C,0x32000);
    S32(0x16000,0x45670001);S32(0x3002C,0x45670001);S32(0x30030,0x39000);S32(0x30034,0x33000);S32(0x33468,0x45670002);
    S32(0x3004C,0x45670002);S32(0x30054,0x34000);S32(0x340AC,1);S32(0x340B0,0x35000);
    memcpy(GPTR(0x35000),"primary trigger",16);S32(0x35034,1);S32(0x35038,0x36000);putf(0x36004,.2f);S8(0x36002,2);putf(0x3601C,1);
    S32(0x340B8,5);S32(0x340BC,0x37000);
    const char *names[]={"root","r wrist","gun","barrel","l wrist"};int parent[]={-1,0,1,2,0};
    for(unsigned i=0;i<5;i++){uint32_t b=0x32000+i*64;memcpy(GPTR(b),names[i],strlen(names[i])+1);S16(b+0x24,parent[i]);
        memcpy(GPTR(0x37000+i*0x9C),names[i],strlen(names[i])+1);}
    /* A second inventory rifle (identical tags): weapon switch and offhand. */
    S16(0x14018,0x4567);S16(0x1401E,0x340);S32(0x14020,0x17000);memcpy(GPTR(0x17000),GPTR(0x16000),0x340);S32(0x1711C,UINT32_MAX);
    float cam[9]={10,20,30.6f,1,0,0,0,0,1};memcpy(GPTR(0x50004),cam,36);
    /* Charge definition and image constants (fixture values; see the
     * original-getter fixture for the retail instructions). */
    S32(0x33000+0x4FC,1);S32(0x33000+0x500,0x3B000);putf(0x3B000+0x48,1.f);putf(0x672AC4,1.f);putf(0x672ACC,1.f/30.f);
    weapon_path("weapons\\assault rifle\\assault rifle");
    S32(0x006B2D98,0x58000);S8(0x58000,1);S32(0x58004,0x23450000);S32(0x58008,0x34560001);S16(0x5800C,0);
    memcpy(grip_positions[1],right_hand,12);memcpy(grip_positions[0],left_hand,12);
    for(unsigned h=0;h<2;h++){hand_forward[h][0]=1;hand_forward[h][1]=hand_forward[h][2]=0;}
    fresh=active=1;support_blocked=app_blocked=collision_hit=read_bump=0;shot_projectiles=1;shot_escape=shot_mid_generation=0;
    grip_on=1;scope_smooth=0;weapon_haptics_on=1;dual_enabled=0;
    sense_support.valid=sense_support.engaged=0;sense_grip_reset(&sense_grip);sense_scope_filter_reset(&sense_scope_aim);haptic_count=0;
    sense_model_hand=1;sense_secondary_ready=0;sense_offhand_claimed=sense_offhand_allow=0;
    frame();assert(sense_camera_unit==0x23450000);
}
static void model(void){
    EngineCPU c={0};c.gpr[4]=0x20000;c.gpr[0]=0x45670000;
    S32(0x20000,0x00493C49);S32(0x20004,0x41000);S32(0x20008,0x40000);
    engine_dispatch(&c,0x004D6880);assert(c.pc==0x00493C49&&c.gpr[4]==0x20004);
}
static int fire_weapon(uint32_t weapon){
    unsigned before=fires;EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20000,0x401234);S32(0x20004,weapon);
    engine_dispatch(&c,0x004C3F10);
    assert(!sense_fire_nested&&!sense_ray_nested&&!sense_shot.armed);
    if(!shot_escape)assert(c.pc==0x401234&&c.gpr[4]==0x20004);
    return fires!=before;
}
static int fire(void){return fire_weapon(0x34560001);}
static void publish(void){EngineCPU rc={0};rc.gpr[4]=0x22000;sense_publish_reticles(&rc);}
/* Barrel, muzzle, the final shot ray and the reticle are one transform. */
static void agreement(void){
    HaloSenseTransform gun;assert(sense_node(0x41000+2*52,&gun));
    float local[3]={.2f,0,0},muzzle[3],eye[3]={10,20,30.6f};sense_point(&gun,local,muzzle);
    const HaloSenseReticle *r=&reticles.hands[1];assert(r->valid&&r->weapon==0x34560001);
    for(unsigned k=0;k<3;k++){
        near(observed_forward[k],gun.c[0][k],2e-6f);near(sense_firing_ray.c[0][k],gun.c[0][k],2e-6f);
        near(observed_origin[k],muzzle[k],2e-5f);
        near(r->direction[k],gun.c[0][k],2e-6f);near(r->origin[k],(muzzle[k]-eye[k])*3.048f,1e-4f);
    }
}
static void gun_forward(float out[3]){HaloSenseTransform gun;assert(sense_node(0x41000+2*52,&gun));memcpy(out,gun.c[0],12);}
static void world_hand(unsigned h,HaloSenseTransform *out){
    float f[3]={hand_forward[h][0],hand_forward[h][1],hand_forward[h][2]},u[3]={0,0,1};assert(sense_basis(f,u,out));
    for(unsigned k=0;k<3;k++)out->p[k]=(k==0?10.f:k==1?20.f:30.6f)+grip_positions[h][k]/3.048f;
}
static void same_transform(const HaloSenseTransform *a,const HaloSenseTransform *b,float tolerance){
    for(unsigned k=0;k<3;k++){near(a->p[k],b->p[k],tolerance);for(unsigned j=0;j<3;j++)near(a->c[j][k],b->c[j][k],tolerance);}
}

/* Cache contract: one immutable primary frame per outer epoch. */
static void primary_cache(void){
    setup();support_blocked=1;frame();model();assert(fire());publish();agreement();
    float shot_forward[3],shot_origin[3];memcpy(shot_forward,observed_forward,12);memcpy(shot_origin,observed_origin,12);
    HaloSenseReticleFrame shown=reticles;unsigned char nodes[5*52];memcpy(nodes,GPTR(0x41000),sizeof nodes);
    SenseAcceptedWeaponFrame f,g;assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&f));
    assert(f.epoch==sense_accepted_frame_epoch()&&f.unit==0x23450000&&f.weapon==0x34560001&&!f.supported&&f.session==1&&f.generation==input_generation);
    HaloSenseTransform raw_left;world_hand(0,&raw_left);same_transform(&f.left,&raw_left,1e-6f);
    assert(!memcmp(&f.left,&f.visual_left,sizeof f.left));
    for(unsigned k=0;k<3;k++)near(f.primary.c[0][k],shot_forward[k],0);
    /* Adapted coordinator_pose_coherence_probe: new raw publications inside
     * the epoch change nothing any consumer receives. */
    hand_forward[1][0]=0;hand_forward[1][1]=1;hand_forward[0][0]=0;hand_forward[0][1]=-1;grip_positions[1][2]+=.2f;grip_positions[0][0]-=.3f;
    assert(fire()&&!memcmp(shot_forward,observed_forward,12)&&!memcmp(shot_origin,observed_origin,12));
    publish();assert(!memcmp(&shown,&reticles,sizeof reticles));
    model();assert(!memcmp(nodes,GPTR(0x41000),sizeof nodes));
    HaloSenseTransform w;assert(sense_world_weapon(1,0x15000,0x23450000,0x34560001,&w)&&!memcmp(&w,&f.primary,sizeof w));
    assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&g)&&!memcmp(&f,&g,sizeof f));
    /* The next epoch accepts the new publication, raw left included. */
    frame();model();assert(fire());publish();agreement();
    near(observed_forward[1],1,1e-6f);
    assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&g)&&g.epoch==f.epoch+1);
    world_hand(0,&raw_left);same_transform(&g.left,&raw_left,1e-6f);
    for(unsigned h=0;h<2;h++){hand_forward[h][0]=1;hand_forward[h][1]=0;}memcpy(grip_positions[1],right_hand,12);memcpy(grip_positions[0],left_hand,12);
    /* Fire first: the tick's shot seeds the frame the later model draws. */
    frame();assert(fire());memcpy(shot_forward,observed_forward,12);
    hand_forward[1][0]=0;hand_forward[1][1]=1;model();publish();agreement();assert(!memcmp(shot_forward,observed_forward,12));
    near(shot_forward[0],1,1e-6f);hand_forward[1][0]=1;hand_forward[1][1]=0;
    /* Read-only accessor: never seeds; current epoch, identity, live lease,
     * menu and 150 ms age, else zeroed false. */
    frame();memset(&g,0x5A,sizeof g);
    assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g));
    for(unsigned i=0;i<sizeof g;i++)assert(((unsigned char*)&g)[i]==0);
    assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g)&&!sense_frame_attempted());
    assert(fire()&&sense_accepted_weapon_frame(0x23450000,0x34560001,&g));
    assert(!sense_accepted_weapon_frame(0x23450000,0x45670002,&g)&&!sense_accepted_weapon_frame(0x23450001,0x34560001,&g));
    /* The getter is pure: it reports each invalid state but records nothing. */
    now+=UINT64_C(139000000);assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&g));
    now+=UINT64_C(12000000);assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g));now-=UINT64_C(151000000);
    S8(0x00718FC9,1);assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g));S8(0x00718FC9,0);
    input_generation++;assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g));input_generation--;
    assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&g));
    /* A substantive consumer that observes any invalid state latches the
     * epoch closed: restoring the exact unit/weapon/menu/lease/age in the
     * same epoch never resurrects the old frame; the next epoch recovers. */
    for(int kind=0;kind<6;kind++){
        frame();model();assert(fire());publish();assert(reticles.hands[1].valid);
        switch(kind){
        case 0:S8(0x00718FC9,1);tick();break;                         /* menu/cutscene */
        case 1:putf(0x150E0,0);publish();break;                        /* local player dead */
        case 2:S32(0x152F8,0x45670002);S32(0x1711C,0x23450000);assert(!fire());break; /* weapon switch */
        case 3:input_generation++;assert(!fire());break;               /* lease generation */
        case 4:now+=UINT64_C(160000000);assert(!sense_world_weapon(1,0x15000,0x23450000,0x34560001,&w));break; /* stale */
        case 5:S16(0x152F0,0);model();break;                           /* seated in a vehicle */
        }
        switch(kind){
        case 0:S8(0x00718FC9,0);break; case 1:putf(0x150E0,1);break;
        case 2:S32(0x152F8,0x34560001);S32(0x1711C,UINT32_MAX);break; case 3:input_generation--;break;
        case 4:now-=UINT64_C(160000000);break; case 5:S16(0x152F0,0xFFFF);break;
        }
        assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g)&&!fire());
        assert(!sense_world_weapon(1,0x15000,0x23450000,0x34560001,&w));
        publish();assert(!reticles.hands[1].valid);model();assert(!sense_muzzle.valid);
        frame();model();assert(fire()&&sense_accepted_weapon_frame(0x23450000,0x34560001,&g));publish();agreement();
    }
    /* A recenter between the before-lease and the pair read fails the whole
     * epoch closed; nothing is drawn or fired from a second pose. */
    frame();read_bump=1;model();assert(!sense_muzzle.valid);
    for(unsigned i=0;i<5;i++)assert(G32(0x41000+i*52)==0);
    assert(!sense_accepted_weapon_frame(0x23450000,0x34560001,&g)&&!fire());
    model();assert(!sense_muzzle.valid); /* one attempt per epoch */
    frame();model();assert(sense_muzzle.valid&&fire());publish();agreement();
    puts("  primary cache: same-epoch publication immutable, next epoch accepted, fire-first seed, accessor read-only/validated, lease race fails closed");
}
/* Two-hand support is accepted with the pose: a busy/eligibility change
 * after the model draws takes effect only at the next epoch. */
static void support_busy_after_model(void){
    setup();frame();model();
    for(unsigned n=0;n<24;n++){frame();model();}
    assert(sense_support.engaged&&!sense_grip.active);
    frame();model();float drawn[3];gun_forward(drawn);
    SenseAcceptedWeaponFrame f;assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&f)&&f.supported);
    HaloSenseTransform left_node;assert(sense_node(0x41000+4*52,&left_node));
    for(unsigned k=0;k<3;k++)near(left_node.p[k],f.visual_left.p[k],2e-5f);
    float foregrip[3];sense_point(&f.primary,sense_support.foregrip,foregrip);for(unsigned k=0;k<3;k++)near(f.visual_left.p[k],foregrip[k],0);
    support_blocked=1; /* e.g. a physical magazine grab after the gun was drawn */
    unsigned before=haptic_count;assert(fire());publish();agreement();
    for(unsigned k=0;k<3;k++)near(observed_forward[k],drawn[k],0);
    assert(haptic_count==before+1&&haptic_events[before].intensity[0]>0);
    frame();model();assert(!sense_support.engaged&&sense_grip.active);
    float released[3];gun_forward(released);near(angle(released,drawn),0,2e-5f); /* eased, not snapped */
    assert(fire());publish();agreement();
    puts("  support busy false-after-true: cached pose/support/shot unchanged in-epoch; next epoch eases release");
}
/* Separately keyed offhand frame: raw left only, own lease, own identity. */
static void offhand_cache(void){
    setup();S32(0x152FC,0x45670002);dual_enabled=1;
    sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;sense_secondary_ready=0;
    frame();
    SenseAcceptedOffhandFrame readback,zero={0};
    memset(&readback,0xff,sizeof readback);
    uint64_t attempted=sense_offhand_accepted.attempted;
    assert(!sense_accepted_offhand_frame(0x23450000,0x45670002,&readback));
    assert(!memcmp(&readback,&zero,sizeof readback)&&sense_offhand_accepted.attempted==attempted);
    assert(!sense_accepted_offhand_frame(0x23450000,0x45670002,NULL));
    sense_model_hand=0;model();sense_model_hand=1;
    assert(sense_offhand_accepted.accepted&&!sense_secondary_ready&&sense_muzzles[0].valid&&sense_muzzles[0].weapon==0x45670002);
    sense_secondary_ready=1;
    HaloSenseTransform raw_left,cached;world_hand(0,&raw_left);
    assert(sense_world_weapon(0,0x15000,0x23450000,0x45670002,&cached));same_transform(&cached,&raw_left,1e-6f);
    assert(!sense_frame_attempted()); /* no primary frame was created or shared this epoch */
    assert(sense_accepted_offhand_frame(0x23450000,0x45670002,&readback));
    assert(readback.epoch==sense_accepted_frame_epoch()&&!memcmp(&readback.left,&cached,sizeof cached));
    /* A cosmetic read is pure, even for transient invalid ownership; only
     * seeding consumers/audit latch a failed epoch. */
    S32(0x1711C,0x99990000);memset(&readback,0xff,sizeof readback);
    assert(!sense_accepted_offhand_frame(0x23450000,0x45670002,&readback)&&!memcmp(&readback,&zero,sizeof readback));
    assert(sense_offhand_accepted.accepted);
    S32(0x1711C,UINT32_MAX);assert(sense_accepted_offhand_frame(0x23450000,0x45670002,&readback));
    sense_offhand_claimed=sense_offhand_allow=1;sense_offhand_unit=0x23450000;sense_offhand_weapon=0x45670002;S32(0x1711C,0x23450000);
    unsigned before=haptic_count;assert(fire_weapon(0x45670002));
    float origin[3],forward[3],muzzle[3];memcpy(origin,observed_origin,12);memcpy(forward,observed_forward,12);
    sense_point(&cached,sense_muzzles[0].muzzle,muzzle);
    for(unsigned k=0;k<3;k++){near(forward[k],cached.c[0][k],0);near(origin[k],muzzle[k],0);}
    assert(haptic_count==before+1&&haptic_events[before].intensity[0]>0&&haptic_events[before].intensity[1]==0);
    publish();assert(reticles.hands[0].valid&&reticles.hands[0].weapon==0x45670002);
    for(unsigned k=0;k<3;k++)near(reticles.hands[0].direction[k],forward[k],2e-6f);
    /* Adversarial same-epoch raw-left change: model, reticle and shot keep
     * the accepted transform; the final pre-spread restore writes it. */
    hand_forward[0][0]=0;hand_forward[0][1]=1;grip_positions[0][2]-=.25f;
    HaloSenseReticleFrame shown=reticles;
    assert(fire_weapon(0x45670002)&&!memcmp(origin,observed_origin,12)&&!memcmp(forward,observed_forward,12));
    publish();assert(!memcmp(&shown,&reticles,sizeof reticles));
    unsigned char nodes[5*52];memcpy(nodes,GPTR(0x41000),sizeof nodes);
    sense_model_hand=0;model();sense_model_hand=1;assert(!memcmp(nodes,GPTR(0x41000),sizeof nodes));
    /* Next epoch accepts the new left; primary admission is independent. */
    frame();sense_model_hand=0;model();sense_model_hand=1;assert(fire_weapon(0x45670002));near(observed_forward[1],1,1e-6f);
    /* The selected primary is never an offhand, and asking for another key
     * does not touch the accepted offhand frame. */
    HaloSenseTransform t;assert(!sense_world_weapon(0,0x15000,0x23450000,0x34560001,&t));
    assert(sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t)&&!memcmp(&t,&sense_offhand_accepted.frame.left,sizeof t));
    /* Fail closed and latch: offhand not held, owner changed, generation,
     * menu, stale epoch. Restoring the exact state in the same epoch stays
     * blocked; the next epoch recovers. */
    for(int kind=0;kind<5;kind++){
        frame();sense_model_hand=0;model();sense_model_hand=1;assert(fire_weapon(0x45670002));
        switch(kind){
        case 0:S32(0x152FC,0);assert(!fire_weapon(0x45670002));break;
        case 1:S32(0x1711C,0x99990000);assert(!sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t));break;
        case 2:input_generation++;assert(!sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t));break;
        case 3:S8(0x00718FC9,1);tick();break;
        case 4:now+=UINT64_C(160000000);assert(!sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t));break;
        }
        switch(kind){
        case 0:S32(0x152FC,0x45670002);break; case 1:S32(0x1711C,0x23450000);break; case 2:input_generation--;break;
        case 3:S8(0x00718FC9,0);break; case 4:now-=UINT64_C(160000000);break;
        }
        assert(!sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t)&&!fire_weapon(0x45670002));
        /* Next epoch; a menu also ran the original dual reset, so the
         * secondary model is rebuilt as the next rendered frame does. */
        sense_secondary_unit=0x23450000;sense_secondary_weapon=0x45670002;
        frame();sense_model_hand=0;model();sense_model_hand=1;
        assert(sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t)&&fire_weapon(0x45670002));
    }
    /* The holstered state (no parent) is the inventory weapon's; a valid
     * frame never bypasses the original claim and muzzle gates. */
    frame();S32(0x1711C,UINT32_MAX);assert(sense_world_weapon(0,0x15000,0x23450000,0x45670002,&t));S32(0x1711C,0x23450000);
    sense_offhand_claimed=0;assert(!fire_weapon(0x45670002));sense_offhand_claimed=1;
    sense_muzzles[0].valid=0;assert(!fire_weapon(0x45670002));
    sense_offhand_claimed=sense_offhand_allow=0;dual_enabled=0;S32(0x152FC,0);S32(0x1711C,UINT32_MAX);
    puts("  offhand cache: raw left only, own lease/identity, available before secondary-ready, same-epoch change immutable, final restore uses it, fails closed");
}
/* Grip continuity through grab/release, recenter, weapon switch, gap, scope. */
static void continuity(void){
    const float one_hand[3]={1,0,0};float two_hand[3]={.35f,0,.15f};assert(sense_normalize(two_hand));
    float full=angle(one_hand,two_hand),forward[3];
    setup();support_blocked=1;frame();model();assert(fire());publish();agreement();
    support_blocked=0;frame();model();assert(sense_support.valid&&sense_support.engaged&&sense_grip.active);
    gun_forward(forward);near(angle(forward,one_hand),0,2e-5f);assert(fire());publish();agreement();
    float previous=full;
    for(unsigned n=0;n<24;n++){
        frame();model();assert(fire());publish();agreement();
        gun_forward(forward);float remaining=angle(forward,two_hand);
        assert(remaining<=previous+1e-6f&&previous-remaining<full*.1f);previous=remaining;
    }
    /* Analytic span from world-unit floats: ~1e-5 representation error. */
    for(unsigned k=0;k<3;k++)near(observed_forward[k],two_hand[k],2e-5f);
    assert(!sense_grip.active);
    support_blocked=1;frame();model();assert(!sense_support.engaged&&sense_grip.active);
    gun_forward(forward);near(angle(forward,two_hand),0,2e-5f);assert(fire());publish();agreement();
    previous=angle(forward,one_hand);
    for(unsigned n=0;n<21;n++){frame();model();assert(fire());publish();agreement();
        float remaining=angle(observed_forward,one_hand);assert(remaining<=previous+1e-6f);previous=remaining;}
    for(unsigned k=0;k<3;k++)near(observed_forward[k],one_hand[k],1e-6f);
    /* Toggle off: exactly the live solve (raw two-hand line), no easing. */
    grip_on=0;support_blocked=0;frame();model();assert(!sense_grip.active&&sense_support.engaged);
    gun_forward(forward);near(angle(forward,two_hand),0,2e-5f);
    /* Re-enabled: a fresh baseline, never an edge from the disabled period. */
    grip_on=1;frame();model();assert(!sense_grip.active);
    support_blocked=1;frame();model();assert(sense_grip.active);
    input_generation++;frame();model();assert(fire());publish();agreement();assert(!sense_grip.active);
    for(unsigned k=0;k<3;k++)near(observed_forward[k],one_hand[k],1e-6f);
    fresh=0;assert(!fire());fresh=1;
    /* Weapon switch is an identity change: the new gun appears on its live
     * line, never animating from the previous weapon. */
    support_blocked=0;frame();model();assert(sense_grip.active);
    S32(0x152F8,0x45670002);S32(0x1711C,0x23450000);S32(0x1611C,UINT32_MAX);S32(0x58008,0x45670002);
    frame();model();assert(sense_grip.weapon==0x45670002&&!sense_grip.active&&sense_support.engaged);
    gun_forward(forward);near(angle(forward,two_hand),0,2e-5f);
    S32(0x152F8,0x34560001);S32(0x1611C,0x23450000);S32(0x1711C,UINT32_MAX);S32(0x58008,0x34560001);
    input_generation++;frame();model();
    support_blocked=1;frame();model();assert(sense_grip.active);
    now+=UINT64_C(300000000);frame();model();assert(!sense_grip.active);
    gun_forward(forward);near(angle(forward,one_hand),0,2e-6f);
    /* Scope during a transient: the narrow camera, shot and model share it. */
    support_blocked=0;frame();model();assert(sense_grip.active);
    S8(0x15320,0);frame();assert(scope_active);
    assert(fire());for(unsigned k=0;k<3;k++){near(scope_forward[k],observed_forward[k],0);near(scope_origin[k],observed_origin[k],0);}
    S8(0x15320,0xFF);input_generation++;frame();assert(!scope_active);
    puts("  grip continuity: exact latch, 200 ms ease, toggle-off raw, recenter/weapon/gap resets, scope shares the transient");
}
/* Default-off scoped smoothing: one filtered pose for scope, shot and frame. */
static void scoped_smoothing(void){
    setup();support_blocked=1;S8(0x15320,0);
    float tremor[3]={1,0,0},raw[3];
    for(int pass=0;pass<2;pass++){
        scope_smooth=pass;input_generation++;frame();assert(scope_active);
        float max_lag=0,max_raw=0,first[3];memcpy(first,scope_forward,12);
        for(unsigned n=0;n<30;n++){
            float a=.012f*sinf((float)n*1.9f);tremor[0]=cosf(a);tremor[1]=sinf(a);memcpy(hand_forward[1],tremor,12);
            frame();assert(fire());SenseAcceptedWeaponFrame f;assert(sense_accepted_weapon_frame(0x23450000,0x34560001,&f));
            for(unsigned k=0;k<3;k++){near(scope_forward[k],observed_forward[k],0);near(f.primary.c[0][k],observed_forward[k],0);
                near(scope_origin[k],sense_accepted.raw_right.p[k],0);} /* hand position is never filtered */
            memcpy(raw,sense_accepted.raw_right.c[0],12);float lag=angle(observed_forward,raw);
            if(lag>max_lag)max_lag=lag;if(fabsf(a)>max_raw)max_raw=fabsf(a);
            if(!pass)assert(!memcmp(&f.primary,&sense_accepted.raw_right,sizeof f.primary)); /* off: raw, bit for bit */
        }
        if(pass)assert(max_lag>1e-3f&&max_lag<=HALO_SENSE_SCOPE_MAX_LAG_RADIANS+1e-5f);else assert(max_lag==0);
        (void)max_raw;(void)first;
    }
    /* A large swing still arrives: the lag never exceeds 4 degrees. */
    float swing=.5f;hand_forward[1][0]=cosf(swing);hand_forward[1][1]=sinf(swing);frame();assert(fire());
    memcpy(raw,sense_accepted.raw_right.c[0],12);float lag=angle(observed_forward,raw);
    assert(lag>.01f&&lag<=HALO_SENSE_SCOPE_MAX_LAG_RADIANS+1e-5f);
    /* Zoom change, scope exit and generation (toggle/recenter) rebaseline exactly. */
    S8(0x15320,1);frame();assert(fire()&&!memcmp(&sense_accepted.frame.primary,&sense_accepted.raw_right,sizeof(HaloSenseTransform)));
    hand_forward[1][0]=1;hand_forward[1][1]=0;frame();input_generation++;frame();
    assert(fire()&&!memcmp(&sense_accepted.frame.primary,&sense_accepted.raw_right,sizeof(HaloSenseTransform)));
    S8(0x15320,0xFF);frame();model();assert(!scope_active&&fire()&&!sense_scope_aim.valid);
    scope_smooth=0;
    puts("  scoped smoothing: off is raw, on bounds lag to 4 degrees, scope/shot/frame share it, hand position unfiltered, resets");
}
static void haptics(void){
    setup();support_blocked=1;frame();model();
    weapon_path("weapons\\shotgun\\shotgun");shot_projectiles=3;unsigned pellets=rays,accepts=shot_accepts;
    assert(fire()&&rays==pellets+3&&haptic_count==1&&shot_accepts==accepts+1);
    HostGCSpatialHaptic *e=&haptic_events[0];
    near(e->intensity[1],1,0);near(e->tail[1],.08f,1e-6f);assert(e->intensity[0]==0&&e->kind==HOSTGC_SPATIAL_SHOT);
    assert(e->session==1&&e->generation==input_generation&&e->lease==bridge_lease&&e->lease&&e->source_generation==9);
    shot_projectiles=0;assert(fire()&&haptic_count==1);shot_projectiles=1;
    shot_escape=1;assert(fire()&&haptic_count==1);shot_escape=0;frame();model();
    shot_mid_generation=1;assert(fire()&&haptic_count==1);shot_mid_generation=0;frame();model();
    collision_hit=1;assert(!fire()&&haptic_count==1);collision_hit=0;
    weapon_path("weapons\\unknown\\unknown");assert(fire()&&haptic_count==2);
    near(haptic_events[1].intensity[1],halo_weapon_haptic_profiles[0].intensity,0);
    haptic_strength=0;assert(fire()&&haptic_count==2);haptic_strength=1;
    /* Toggle off: owner revoked at the next tick, queued work cancelled,
     * audio released, no event; on again re-leases. */
    unsigned all=cancel_all,releases=owner_releases;weapon_haptics_on=0;tick();
    assert(cancel_all==all+1&&owner_releases==releases+1&&!sense_haptic_owner.owned);
    assert(fire()&&haptic_count==2);weapon_haptics_on=1;uint64_t old=bridge_lease;tick();assert(sense_haptic_owner.lease==old+1);
    /* Menu, inactive and generation changes cancel; repeat ticks do not. */
    unsigned renews=owner_renews;all=cancel_all;
    tick();assert(owner_renews==renews+1&&cancel_all==all);
    input_generation++;tick();assert(cancel_all==all+1);
    active=0;tick();assert(cancel_all==all+2);tick();assert(cancel_all==all+2);active=1;tick();
    S8(0x00718FC9,1);tick();assert(cancel_all==all+3);
    {   /* A shot accepted just before the menu opened no longer qualifies. */
        HaloSenseDualInput input;assert(halo_sense_dual_input(&input));
        EngineCPU c={0};c.gpr[4]=0x20000;S32(0x20000,0x401234);unsigned before=haptic_count;
        sense_shot_begin(&c,1,0x23450000,0x34560001,&input);sense_shot_projectile();c.pc=0x401234;sense_shot_end(&c);
        assert(haptic_count==before);
    }
    S8(0x00718FC9,0);
    puts("  semantic shots: one per accepted call, leased events, escape/no-projectile/mid-call/blocked/strength/toggle/menu suppression");
}
/* Charge pulses from the real per-trigger engine state, not the trigger. */
static void charge(void){
    setup();weapon_path("weapons\\plasma pistol\\plasma pistol");tick();
    unsigned base=haptic_count;assert(base==0);
    S8(0x16261,2);S16(0x16262,30);tick();assert(haptic_count==1&&haptic_events[0].kind==HOSTGC_SPATIAL_CHARGE);
    near(haptic_events[0].intensity[1],.12f,1e-6f);assert(haptic_events[0].intensity[0]==0&&haptic_events[0].lease==bridge_lease);
    now+=UINT64_C(100000000);S16(0x16262,15);tick();assert(haptic_count==1); /* bounded period */
    now+=UINT64_C(200000000);tick();assert(haptic_count==2);near(haptic_events[1].intensity[1],.12f+.30f*.5f,1e-5f);
    S8(0x16261,3);tick();assert(haptic_count==3);near(haptic_events[2].intensity[1],.55f,0); /* fully charged */
    now+=UINT64_C(300000000);tick();assert(haptic_count==3);now+=UINT64_C(450000000);tick();assert(haptic_count==4);
    /* Release cancels queued charge pulses; a held trigger alone is nothing. */
    unsigned charge_cancels=cancel_charge;S8(0x16261,0);tick();assert(cancel_charge==charge_cancels+1&&haptic_count==4);
    tick();assert(cancel_charge==charge_cancels+1);
    /* Corrupt definition values never produce a pulse. */
    S8(0x16261,2);putf(0x3B048,0);tick();assert(haptic_count==4);putf(0x3B048,NAN);tick();assert(haptic_count==4);
    putf(0x3B048,1);S32(0x33000+0x4FC,7);tick();assert(haptic_count==4);S32(0x33000+0x4FC,1);
    S32(0x1611C,UINT32_MAX);tick();assert(haptic_count==4);S32(0x1611C,0x23450000); /* not the player's */
    tick();assert(haptic_count==5);
    /* Generation (recenter) and weapon switch cancel; toggle off stops it. */
    charge_cancels=cancel_charge;unsigned all=cancel_all;input_generation++;tick();assert(cancel_all==all+1&&cancel_charge==charge_cancels+1);
    S32(0x152F8,0x45670002);S32(0x1711C,0x23450000);tick();assert(cancel_charge==charge_cancels+2);
    S32(0x152F8,0x34560001);tick();
    unsigned count=haptic_count;weapon_haptics_on=0;now+=UINT64_C(400000000);tick();assert(haptic_count==count);weapon_haptics_on=1;
    S8(0x16261,0);
    puts("  charge: original state 2/3 + fraction, bounded period, full pulse, release/corrupt/owner/recenter/switch/toggle cancel");
}

int main(void){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);setenv("HALO_SENSE_WEAPON","1",1);
    primary_cache();support_busy_after_model();offhand_cache();continuity();scoped_smoothing();haptics();charge();
    free(engine_flat_base);
    puts("PASS sense weapon frame caches: immutable primary/offhand epoch poses shared by viewmodel, reticle, scope and final pre-spread shot; read-only accessor; continuity; scoped smoothing; semantic shot and charge haptics");
}
