/* Experimental tracked full body for the verified retail Halo PC 1.10 guest
 * (c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545).
 * Included by overrides.c after sense_weapon_hooks.inc, whose camera, object
 * and action helpers are reused read-only. ABI: BODY_ABI_AUDIT.json.
 *
 * 0050EA50 (esi=object, eax=0/1) is the original first-person object test.
 * Its two render callers are 0050EE20 at 0050EE5F (skips the object and its
 * whole carried subtree) and 0050EBA0's shadow gate at 0050EBCA. The original
 * still runs; only its result is changed, only for those callers, only for the
 * epoch's latched local biped (admitted) and its salted children (hidden).
 * 004D6FC0 (eax=model, ecx=palette, [esp+24h]=object) copies the palette
 * through [00696664] into its own stack at 004D7071..004D70A7, so the body is
 * drawn from a retained scratch palette through the shared world-model
 * wrapper (sense_world_model_override.inc); object and node bytes never change.
 *
 * One snapshot per outer render epoch decides admission and both palettes,
 * including an absent outcome, so every eye and bearing draws the same body.
 * Arms are cosmetic: solved only to the weapon layer's read-only accepted
 * first-person wrists. Without that pose (first frame, weapon change, melee)
 * the eye palette hides the body's arms and the shadow keeps the original rig.
 * Default off until the physical headset acceptance test. */
#include "sense_body.h"
#include <stdatomic.h>

#ifdef SENSE_ACCEPTED_WEAPON_FRAME_DEFINED
/* The weapon module's outer epoch (sense_weapon_frame.inc) is the only epoch. */
static uint64_t sense_body_epoch_token(void){return sense_accepted_frame_epoch();}
#else
/* Standalone seam until the weapon module (sense_weapon_frame.inc) defining
 * SENSE_ACCEPTED_WEAPON_FRAME_DEFINED is merged before this file: no pose is
 * ever accepted and the body never derives its own gun pose. The epoch is
 * then this file's own outer 0050BEA0 count. */
typedef struct {
    uint64_t epoch, epoch_ns, session, generation;
    uint32_t unit, weapon;
    int supported;
    HaloSenseTransform primary, left, visual_left;
} SenseAcceptedWeaponFrame;
static int sense_accepted_weapon_frame(uint32_t datum,uint32_t weapon,SenseAcceptedWeaponFrame *out) {
    (void)datum;(void)weapon;if(out)memset(out,0,sizeof *out);return 0;
}
static uint64_t sense_body_local_epoch;
static uint64_t sense_body_epoch_token(void){return sense_body_local_epoch;}
#endif
#include "sense_world_model_override.h"

static _Atomic int sense_body_user_enabled;
void halo_sense_set_body_enabled(int enabled){atomic_store(&sense_body_user_enabled,!!enabled);}
int halo_sense_body_enabled(void){return atomic_load(&sense_body_user_enabled);}

#define SENSE_BODY_MAX_NODES 64u
typedef struct {
    uint64_t epoch, session, generation, latched_ns;
    uint64_t accepted_epoch, accepted_session, accepted_generation;
    int latched, admitted, arms, state, invalidated;
    uint32_t datum, unit, nodes, model, weapon;
    unsigned count;
} SenseBodySnapshot;
typedef struct { int16_t parent[SENSE_BODY_MAX_NODES]; int head, arm[2][3]; } SenseBodyRig;
static SenseBodySnapshot sense_body;
static uint32_t sense_body_eye, sense_body_shadow; /* guest scratch, retained */
static uint32_t sense_body_pending=UINT32_MAX;
static int sense_body_predicate_nested;
static _Atomic int sense_body_status_state;
static _Atomic uint64_t sense_body_status_epoch, sense_body_status_ns;

void host_sense_body_status(HaloSenseBodyStatus *out) {
    if(!out)return;memset(out,0,sizeof *out);
    uint64_t now=host_monotonic_ns(),stamp=atomic_load(&sense_body_status_ns);
    if(!halo_sense_body_enabled()||!stamp||now<stamp||now-stamp>UINT64_C(250000000))return;
    out->state=atomic_load(&sense_body_status_state);out->epoch=atomic_load(&sense_body_status_epoch);
}
/* Any object type, salted, at least n bytes. */
static uint32_t sense_body_object(uint32_t datum,size_t n) {
    uint32_t table=G32(0x008603B0u);
    if(datum==UINT32_MAX||!sense_range(table,0x38))return 0;
    unsigned count=G16(table+0x20),index=datum&65535u;uint32_t entries=G32(table+0x34);
    if(!count||count>4096||index>=count||G16(table+0x22)!=12||!sense_range(entries,count*12u))return 0;
    uint32_t e=entries+index*12u,body=G32(e+8);
    return G16(e)&&G16(e)==(datum>>16)&&G16(e+6)>=n&&sense_range(body,n)?body:0;
}
/* Whole-word match in a biped node name such as "bip01 l forearm". */
static int sense_body_word(const char *name,const char *word) {
    size_t n=strlen(word);
    for(const char *s=strstr(name,word);s;s=strstr(s+1,word))
        if((s==name||s[-1]==' ')&&(s[n]==0||s[n]==' '))return 1;
    return 0;
}
static int sense_body_part(const char *name,char side,const char *part) {
    char word[24];snprintf(word,sizeof word,"%c %s",side,part);return sense_body_word(name,word);
}
/* Model node metadata (0x9C): name +0, parent int16 +24h (004F8310 at
 * 004F8A00). Unknown, ambiguous or cyclic rigs refuse the body. */
static int sense_body_rig(uint32_t meta,unsigned count,SenseBodyRig *rig) {
    char names[SENSE_BODY_MAX_NODES][33];
    rig->head=-1;for(unsigned s=0;s<2;s++)for(unsigned b=0;b<3;b++)rig->arm[s][b]=-1;
    for(unsigned i=0;i<count;i++){
        memcpy(names[i],GPTR(meta+i*0x9Cu),32);names[i][32]=0;
        rig->parent[i]=(int16_t)G16(meta+i*0x9Cu+0x24);
        if(i?rig->parent[i]<0||rig->parent[i]>=(int)count:rig->parent[i]!=-1)return 0;
    }
    for(unsigned i=0;i<count;i++){
        unsigned at=i,steps=0;while(at&&steps++<count)at=(unsigned)rig->parent[at];
        if(at)return 0;
    }
    static const char *parts[3]={"upperarm","forearm","hand"};
    for(unsigned i=0;i<count;i++){
        if(sense_body_word(names[i],"head")){
            int top=1;for(int a=rig->parent[i];a>=0;a=rig->parent[a])if(sense_body_word(names[a],"head"))top=0;
            if(top){if(rig->head>=0)return 0;rig->head=(int)i;}
        }
        for(unsigned s=0;s<2;s++)for(unsigned b=0;b<3;b++)if(sense_body_part(names[i],s?'r':'l',parts[b])){
            if(rig->arm[s][b]>=0)return 0;rig->arm[s][b]=(int)i;
        }
    }
    if(rig->head<0)return 0;
    for(unsigned s=0;s<2;s++){
        const int *a=rig->arm[s];
        if(a[0]<0||a[1]<0||a[2]<0||rig->parent[a[1]]!=a[0]||rig->parent[a[2]]!=a[1])return 0;
        if(sense_descendant((unsigned)rig->head,a[0],rig->parent,count)||sense_descendant((unsigned)a[0],rig->head,rig->parent,count))return 0;
    }
    return 1;
}
static void sense_body_rotation(const float a[3],const float b[3],float r[3][3]) {
    float v[3];sense_cross(a,b,v);float c=sense_dot(a,b);
    if(c<-.9999f){
        /* Half turn about any axis perpendicular to a. */
        float axis[3]={0,0,1};if(fabsf(a[2])>.9f){axis[0]=1;axis[2]=0;}
        sense_cross(a,axis,v);sense_normalize(v);
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)r[i][j]=2.f*v[i]*v[j]-(i==j);
        return;
    }
    float k=1.f/(1.f+c),x[3][3]={{0,-v[2],v[1]},{v[2],0,-v[0]},{-v[1],v[0],0}};
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++){
        float x2=0;for(unsigned t=0;t<3;t++)x2+=x[i][t]*x[t][j];
        r[i][j]=(i==j)+x[i][j]+x2*k;
    }
}
static void sense_body_mul(const float r[3][3],const float v[3],float out[3]) {
    float o[3];for(unsigned i=0;i<3;i++)o[i]=r[i][0]*v[0]+r[i][1]*v[1]+r[i][2]*v[2];memcpy(out,o,12);
}
/* Rigid move of a whole subtree: basis rotated, origin about from -> to. */
static void sense_body_move(float m[][13],const SenseBodyRig *rig,unsigned count,int root,
                            const float r[3][3],const float from[3],const float to[3]) {
    float f[3],t[3];memcpy(f,from,12);memcpy(t,to,12);
    for(unsigned i=0;i<count;i++)if(sense_descendant(i,root,rig->parent,count)){
        for(unsigned c=0;c<3;c++)sense_body_mul(r,m[i]+1+3*c,m[i]+1+3*c);
        float d[3];for(unsigned k=0;k<3;k++)d[k]=m[i][10+k]-f[k];
        sense_body_mul(r,d,d);for(unsigned k=0;k<3;k++)m[i][10+k]=d[k]+t[k];
    }
}
static void sense_body_collapse(float m[][13],const SenseBodyRig *rig,unsigned count,int root,const float at[3]) {
    float p[3];memcpy(p,at,12);
    for(unsigned i=0;i<count;i++)if(sense_descendant(i,root,rig->parent,count)){m[i][0]=0;memcpy(m[i]+10,p,12);}
}
static float sense_body_distance(const float a[3],const float b[3]) {
    float d[3]={a[0]-b[0],a[1]-b[1],a[2]-b[2]};return sqrtf(sense_dot(d,d));
}
/* Two-bone reach preserving the animated upper-arm and forearm lengths. The
 * wrist is clamped inside reach; the elbow bends down, out and back. */
static int sense_body_arm(float m[][13],const SenseBodyRig *rig,unsigned count,unsigned side,
                          const float target[3],const float forward[3],const float left[3]) {
    const int *chain=rig->arm[side];float s[3],e[3],w[3];
    memcpy(s,m[chain[0]]+10,12);memcpy(e,m[chain[1]]+10,12);memcpy(w,m[chain[2]]+10,12);
    float l1=sense_body_distance(e,s),l2=sense_body_distance(w,e);
    if(!isfinite(l1)||!isfinite(l2)||l1<.001f||l2<.001f||l1>.5f||l2>.5f)return 0;
    float u[3]={target[0]-s[0],target[1]-s[1],target[2]-s[2]},d=sqrtf(sense_dot(u,u));
    if(!isfinite(d)||d<1e-4f||d>1.f)return 0;
    for(unsigned k=0;k<3;k++)u[k]/=d;
    float low=fabsf(l1-l2)+1e-4f,high=(l1+l2)*.999f;if(high<=low)return 0;
    float reach=fminf(high,fmaxf(low,d)),pole[3],wn[3],en[3];
    for(unsigned k=0;k<3;k++){
        pole[k]=(side?-left[k]:left[k])*.6f-forward[k]*.3f-(k==2);
        wn[k]=s[k]+u[k]*reach;
    }
    float along=sense_dot(pole,u);for(unsigned k=0;k<3;k++)pole[k]-=along*u[k];
    if(!sense_normalize(pole)){sense_cross(u,left,pole);if(!sense_normalize(pole))return 0;}
    float cosine=(l1*l1+reach*reach-l2*l2)/(2.f*l1*reach);cosine=fminf(1.f,fmaxf(-1.f,cosine));
    float sine=sqrtf(fmaxf(0.f,1.f-cosine*cosine));
    for(unsigned k=0;k<3;k++)en[k]=s[k]+l1*(cosine*u[k]+sine*pole[k]);
    float a[3],b[3],r[3][3];
    for(unsigned k=0;k<3;k++){a[k]=e[k]-s[k];b[k]=en[k]-s[k];}
    if(!sense_normalize(a)||!sense_normalize(b))return 0;
    sense_body_rotation(a,b,r);sense_body_move(m,rig,count,chain[0],r,s,s);
    float moved_e[3],moved_w[3];memcpy(moved_e,m[chain[1]]+10,12);memcpy(moved_w,m[chain[2]]+10,12);
    for(unsigned k=0;k<3;k++){a[k]=moved_w[k]-moved_e[k];b[k]=wn[k]-en[k];}
    if(!sense_normalize(a)||!sense_normalize(b))return 0;
    sense_body_rotation(a,b,r);sense_body_move(m,rig,count,chain[1],r,moved_e,en);
    for(unsigned i=0;i<count;i++)for(unsigned k=0;k<13;k++)if(!isfinite(m[i][k]))return 0;
    return fabsf(sense_body_distance(m[chain[1]]+10,s)-l1)<=l1*1e-3f+1e-5f&&
           fabsf(sense_body_distance(m[chain[2]]+10,m[chain[1]]+10)-l2)<=l2*1e-3f+1e-5f;
}
/* The local on-foot campaign biped of the live immersive world render, or 0.
 * Checked at the latch and again at every submission: a recycled slot, a new
 * owner, a session/generation change or a mode change fails closed. */
static uint32_t sense_body_owner(uint32_t *datum,HaloSenseStatus *status,int *reason) {
    *reason=HALO_SENSE_BODY_UNAVAILABLE;
    /* Panorama's immersive world passes only: a flat window keeps the original
     * first-person view even while trackers are live. The scope's narrow
     * original render is reported, never drawn with a body. */
    if(host_sense_scope_rendering()){*reason=HALO_SENSE_BODY_UNSUPPORTED;return 0;}
    if(!panorama_nested||!host_sense_weapon_rendering()||halo_sense_app_input_blocked())return 0;
    uint32_t unit=sense_local_body(datum);if(!unit)return 0;
    if(G16(0x00719720u)!=0||G16(unit+0x2F0)!=UINT16_MAX||G32(unit+0x11C)!=UINT32_MAX){*reason=HALO_SENSE_BODY_UNSUPPORTED;return 0;}
    halo_sense_status(status);
    if(!status->active||!status->requested||!status->session)return 0;
    return unit;
}
static int sense_body_compute(SenseBodySnapshot *snap) {
    if(!halo_sense_body_enabled())return HALO_SENSE_BODY_OFF;
    if(!sense_body_epoch_token())return HALO_SENSE_BODY_UNAVAILABLE;
    HaloSenseStatus status;int reason;uint32_t datum,unit=sense_body_owner(&datum,&status,&reason);
    if(!unit)return reason;
    HaloSenseFrame frame;uint64_t now=host_monotonic_ns();
    if(!halo_sense_read(&frame)||sense_camera_unit!=datum||now<sense_camera_ns||now-sense_camera_ns>UINT64_C(250000000))
        return HALO_SENSE_BODY_UNAVAILABLE;
    snap->session=status.session;snap->generation=status.generation;
    /* render_object_list: nodes = object + sign-extended word[+1F2h];
     * model = definition+34h; 004D6FC0 reads model+B8h nodes. */
    uint32_t definition=sense_tag(G32(unit),0x38),model_datum=definition?G32(definition+0x34):UINT32_MAX;
    uint32_t model=sense_tag(model_datum,0xC0);if(!model)return HALO_SENSE_BODY_UNKNOWN_RIG;
    unsigned count=G32(model+0xB8);uint32_t meta=G32(model+0xBC);int offset=(int16_t)G16(unit+0x1F2);
    if(!count||count>SENSE_BODY_MAX_NODES||!sense_range(meta,count*0x9Cu)||offset<0x100||
       sense_body_object(datum,(size_t)offset+count*52u)!=unit)return HALO_SENSE_BODY_UNKNOWN_RIG;
    SenseBodyRig rig;if(!sense_body_rig(meta,count,&rig))return HALO_SENSE_BODY_UNKNOWN_RIG;
    float shadow[SENSE_BODY_MAX_NODES][13],eye[SENSE_BODY_MAX_NODES][13];
    uint32_t nodes=unit+(uint32_t)offset;memcpy(shadow,GPTR(nodes),count*52u);
    for(unsigned i=0;i<count;i++){
        for(unsigned k=0;k<13;k++)if(!isfinite(shadow[i][k]))return HALO_SENSE_BODY_UNKNOWN_RIG;
        if(shadow[i][0]<0||shadow[i][0]>16.f)return HALO_SENSE_BODY_UNKNOWN_RIG;
    }
    float eye_point[3];memcpy(eye_point,GPTR(unit+0x5C),12);
    for(unsigned k=0;k<3;k++)eye_point[k]+=sense_camera_from_unit[k];
    float up[3]={0,0,1},left[3]={sense_camera.c[1][0],sense_camera.c[1][1],0},forward[3];
    if(!sense_finite3(eye_point)||!sense_normalize(left))return HALO_SENSE_BODY_UNAVAILABLE;
    sense_cross(left,up,forward);
    /* Arms follow only the accepted first-person wrists; never during an
     * original melee/reload/swap animation, never from a body-side gun solve. Dual
     * wielding mode always keeps its own first-person arms: the offhand pose
     * may be accepted after this latch, so the body never aligns to it. */
    int slot=(int16_t)G16(unit+0x2F2),arms=0;
    uint32_t weapon=slot>=0&&slot<4?G32(unit+0x2F8+4u*(unsigned)slot):UINT32_MAX;
    SenseAcceptedWeaponFrame accepted;
    if(weapon!=UINT32_MAX&&!halo_sense_dual_requested()&&!sense_action_native(sense_action_for_unit(unit,datum,weapon,NULL))&&
       sense_accepted_weapon_frame(datum,weapon,&accepted)&&accepted.unit==datum&&accepted.weapon==weapon&&
       sense_finite3(accepted.primary.p)&&sense_finite3(accepted.visual_left.p)&&
       sense_body_distance(accepted.primary.p,eye_point)<=1.f&&sense_body_distance(accepted.visual_left.p,eye_point)<=1.f){
        float solved[SENSE_BODY_MAX_NODES][13];memcpy(solved,shadow,count*52u);
        if(sense_body_arm(solved,&rig,count,0,accepted.visual_left.p,forward,left)&&
           sense_body_arm(solved,&rig,count,1,accepted.primary.p,forward,left)){
            memcpy(shadow,solved,count*52u);arms=1;snap->weapon=weapon;
            snap->accepted_epoch=accepted.epoch;snap->accepted_session=accepted.session;snap->accepted_generation=accepted.generation;
        }
    }
    memcpy(eye,shadow,count*52u);
    for(unsigned s=0;s<2;s++){
        int root=rig.arm[s][arms?2:0];
        /* Eye view: first-person hands remain authoritative. Unsolved arms
         * hide at the shoulder rather than hold an invisible third-person gun. */
        sense_body_collapse(eye,&rig,count,root,eye[root]+10);
    }
    sense_body_collapse(eye,&rig,count,rig.head,eye[rig.head]+10);
    if(!sense_body_eye)sense_body_eye=guest_alloc(SENSE_BODY_MAX_NODES*52u);
    if(!sense_body_shadow)sense_body_shadow=guest_alloc(SENSE_BODY_MAX_NODES*52u);
    if(!sense_range(sense_body_eye,count*52u)||!sense_range(sense_body_shadow,count*52u))return HALO_SENSE_BODY_UNAVAILABLE;
    memcpy(GPTR(sense_body_eye),eye,count*52u);memcpy(GPTR(sense_body_shadow),shadow,count*52u);
    snap->admitted=1;snap->arms=arms;snap->datum=datum;snap->unit=unit;snap->nodes=nodes;snap->model=model_datum;snap->count=count;
    return arms?HALO_SENSE_BODY_ACTIVE:HALO_SENSE_BODY_ARMS_WAITING;
}
static int sense_body_latch(void) {
    uint64_t epoch=sense_body_epoch_token();
    if(sense_body.latched&&sense_body.epoch==epoch)return sense_body.admitted;
    SenseBodySnapshot next={.epoch=epoch,.latched=1,.datum=UINT32_MAX,.latched_ns=host_monotonic_ns()};
    next.state=sense_body_compute(&next);
    if(!next.admitted){next.arms=0;next.datum=UINT32_MAX;}
    sense_body=next;
    atomic_store(&sense_body_status_state,next.state);atomic_store(&sense_body_status_epoch,next.epoch);
    atomic_store(&sense_body_status_ns,host_monotonic_ns());
    if(HOST_ENV("HALO_SENSE_BODY_TRACE")){
        static unsigned logs;if(logs++<32)host_log("[sense-body] epoch=%llu state=%d unit=%08x nodes=%u arms=%d",
            (unsigned long long)next.epoch,next.state,next.datum,next.count,next.arms);
    }
    return next.admitted;
}
/* Cosmetic only: may latch this epoch's body snapshot, never the gun frame.
 * Call after the epoch's primary weapon admission (the first-person rig). */
static int sense_body_current(void) {
    return sense_body.latched&&sense_body.epoch==sense_body_epoch_token()&&sense_body.admitted;
}
/* The latched body still belongs to the same live owner, slot and lease, its
 * epoch is recent (an epoch only advances while tracked rendering runs),
 * tracking is still fresh and, with solved arms, the same accepted frame is
 * still current. Read-only; the latched pose is never recomputed. */
static int sense_body_live_check(void) {
    HaloSenseStatus status;HaloSenseFrame frame;SenseAcceptedWeaponFrame accepted;
    int reason;uint32_t datum;uint64_t now=host_monotonic_ns();
    return now>=sense_body.latched_ns&&now-sense_body.latched_ns<=UINT64_C(150000000)&&
        sense_body_owner(&datum,&status,&reason)==sense_body.unit&&datum==sense_body.datum&&
        status.session==sense_body.session&&status.generation==sense_body.generation&&
        sense_body_object(datum,0x308)==sense_body.unit&&
        sense_body.unit+(uint32_t)(int16_t)G16(sense_body.unit+0x1F2)==sense_body.nodes&&halo_sense_read(&frame)&&
        (!sense_body.arms||(sense_accepted_weapon_frame(datum,sense_body.weapon,&accepted)&&accepted.unit==datum&&
            accepted.weapon==sense_body.weapon&&accepted.epoch==sense_body.accepted_epoch&&
            accepted.session==sense_body.accepted_session&&accepted.generation==sense_body.accepted_generation));
}
/* The first failed observation is sticky: a restored publication, lease or
 * menu state later in the same epoch never resurrects the cached body. Body
 * state only; the weapon cache is never touched. */
static int sense_body_live(void) {
    if(!sense_body_current()||sense_body.invalidated)return 0;
    if(sense_body_live_check())return 1;
    sense_body.invalidated=1;
    atomic_store(&sense_body_status_state,HALO_SENSE_BODY_UNAVAILABLE);atomic_store(&sense_body_status_ns,host_monotonic_ns());
    return 0;
}
/* Cosmetic only: may latch this epoch's body snapshot, never the gun frame.
 * Call after the epoch's primary weapon admission (the first-person rig). */
int host_sense_body_active(void){return sense_body_latch()&&sense_body.arms&&sense_body_live();}
static int sense_body_predicate(EngineCPU *cpu) {
    uint32_t caller=G32(cpu->gpr[4]),object=cpu->gpr[6];
    if(sense_body_predicate_nested||(caller!=0x0050EE64u&&caller!=0x0050EBCFu))return 0;
    sense_body_pending=UINT32_MAX;
    /* A setting change mid-epoch applies next epoch: the latch decides. */
    if(sense_body.latched&&sense_body.epoch==sense_body_epoch_token()?!sense_body.admitted:!halo_sense_body_enabled())return 0;
    sense_body_predicate_nested=1;engine_dispatch(cpu,0x0050EA50u);sense_body_predicate_nested=0;
    if(cpu->pc!=caller||G8(0x007C3138u))return 1;
    if(cpu->gpr[0]&255u){
        if(sense_body_latch()&&object==sense_body.datum&&sense_body_live()){
            cpu->gpr[0]=0;if(caller==0x0050EE64u)sense_body_pending=object;
        }
    }else if(caller==0x0050EE64u&&sense_body_current()&&object!=sense_body.datum){
        /* Carried weapons/grenades: first-person models own them. */
        uint32_t child=sense_body_object(object,0x120);
        if(child&&G32(child+0x11C)==sense_body.datum&&sense_body_live())cpu->gpr[0]=1;
    }
    return 1;
}
/* Shared world-model wrapper helper: scratch pointers only, no CPU, stack or
 * guest-object writes and no dispatch. Only the object this list iteration
 * just admitted is claimed; everything else is NONE for other lanes. */
#define HALO_SENSE_HAVE_BODY_MODEL_PREPARE 1
static int sense_body_model_prepare(EngineCPU *cpu,SenseWorldModelOverride *out) {
    uint32_t caller=G32(cpu->gpr[4]),object=ARG(8);
    memset(out,0,sizeof *out);
    if(object==UINT32_MAX||object!=sense_body_pending)return SENSE_WORLD_MODEL_NONE;
    sense_body_pending=UINT32_MAX;
    int shadow=caller==0x0050F0BDu;
    if((!shadow&&caller!=0x0050F049u)||!sense_body_live()||object!=sense_body.datum||cpu->gpr[1]!=sense_body.nodes||
       cpu->gpr[0]!=sense_body.model||((ARG(10)&2u)!=0)!=shadow)
        return SENSE_WORLD_MODEL_DROP; /* Admitted but unverifiable: never the raw head. */
    out->palette=shadow?sense_body_shadow:sense_body_eye;
    return SENSE_WORLD_MODEL_APPLY;
}
static int host_sense_body_dispatch(EngineCPU *cpu,uint32_t address) {
    if(address==0x0050BEA0u){
        if(!panorama_nested&&!host_sense_scope_rendering()){
#ifndef SENSE_ACCEPTED_WEAPON_FRAME_DEFINED
            if(++sense_body_local_epoch==0)++sense_body_local_epoch;
#endif
            sense_body_pending=UINT32_MAX;
        }
        return 0;
    }
    if(address==0x0050EA50u)return sense_body_predicate(cpu);
    return 0;
}
