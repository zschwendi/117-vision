/* Rigid transforms for the independent tracked weapon path. Halo world units
 * are 3.048 metres. Basis columns are forward, left, up; no camera/input state
 * is changed by these helpers. */
#ifndef HALO_SENSE_WEAPON_H
#define HALO_SENSE_WEAPON_H
#include <math.h>
#include <stdint.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Neutral panorama coordinates: +X forward, +Y left, +Z up. All points are
 * metres relative to the engine eye, and both hands use the actual barrel.
 * The pose bridge rejects stale/session-mismatched publications. */
typedef struct {
    int valid, hit;
    float origin[3], direction[3], point[3];
    uint32_t weapon;
} HaloSenseReticle;
typedef struct {
    uint64_t session, generation;
    HaloSenseReticle hands[2];
} HaloSenseReticleFrame;
void halo_sense_reticle_publish(const HaloSenseReticleFrame *frame);
int halo_sense_reticle_read(HaloSenseReticleFrame *frame);
/* Mirrored secondary geometry reverses winding only for its draw scope. */
int host_sense_weapon_mirrored_draw(void);
/* Read-only, timestamped presentation diagnostics. No guest pointers or
 * inventory objects cross the engine thread boundary. */
enum { HALO_SENSE_SUPPORT_NONE=0, HALO_SENSE_SUPPORT_READY=1,
       HALO_SENSE_SUPPORT_HELD=2, HALO_SENSE_SUPPORT_INVALID_SPAN=3,
       HALO_SENSE_SUPPORT_WAITING_POSE=4 };
enum { HALO_SENSE_DUAL_OFF=0, HALO_SENSE_DUAL_WAITING_TRACKING=1,
       HALO_SENSE_DUAL_NO_PLAYER=2, HALO_SENSE_DUAL_UNSUPPORTED_PRIMARY=3,
       HALO_SENSE_DUAL_NO_SECOND_WEAPON=4, HALO_SENSE_DUAL_UNSUPPORTED_SECONDARY=5,
       HALO_SENSE_DUAL_SECONDARY_BUSY=6, HALO_SENSE_DUAL_AMBIGUOUS=7,
       HALO_SENSE_DUAL_INVALID_ATTACHMENT=8, HALO_SENSE_DUAL_PREPARING_MODEL=9,
       HALO_SENSE_DUAL_READY=10 };
typedef struct { int support, dual; } HaloSenseWeaponStatus;
void host_sense_weapon_status(HaloSenseWeaponStatus *out);
/* RELOAD and SWAP: the original first-person reload (empty/full, or the
 * enter/exit loop) or put-away/ready animation owns the whole viewmodel rig,
 * like MELEE, for exactly as long as the original plays it. */
enum { HALO_SENSE_ACTION_NONE=0, HALO_SENSE_ACTION_MELEE=1, HALO_SENSE_ACTION_SCOPE=2,
       HALO_SENSE_ACTION_RELOAD=3, HALO_SENSE_ACTION_SWAP=4 };
typedef struct {
    uint64_t session, generation, mode_generation;
    uint32_t weapon;
    int action, zoom_level;
} HaloSenseWeaponActionStatus;
/* Coherent, timestamped state; no live guest-memory reads off engine thread. */
int host_sense_weapon_action_status(HaloSenseWeaponActionStatus *out);
struct EngineCPU;
int host_sense_scope_dispatch(struct EngineCPU *cpu, uint32_t address);
int host_sense_scope_rendering(void);
/* Completed original scope render, cleared before the next outer render. */
int host_sense_scope_frame(HaloSenseWeaponActionStatus *out);

#ifdef __cplusplus
}
#endif
typedef struct { float c[3][3], p[3]; } HaloSenseTransform;
static inline float sense_dot(const float a[3], const float b[3]) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
static inline void sense_cross(const float a[3], const float b[3], float out[3]) {
    float v[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};memcpy(out,v,sizeof v);
}
static inline int sense_normalize(float v[3]) {
    float n=sense_dot(v,v);if(!isfinite(n)||n<1e-8f)return 0;
    n=1.f/sqrtf(n);for(unsigned i=0;i<3;i++)v[i]*=n;return 1;
}
static inline int sense_basis(const float forward[3],const float up[3],HaloSenseTransform *out) {
    memcpy(out->c[0],forward,12);memcpy(out->c[2],up,12);
    if(!sense_normalize(out->c[0])||!sense_normalize(out->c[2]))return 0;
    sense_cross(out->c[2],out->c[0],out->c[1]);if(!sense_normalize(out->c[1]))return 0;
    sense_cross(out->c[0],out->c[1],out->c[2]);return 1;
}
static inline void sense_rotate(const HaloSenseTransform *m,const float v[3],float out[3]) {
    float r[3];for(unsigned i=0;i<3;i++)r[i]=m->c[0][i]*v[0]+m->c[1][i]*v[1]+m->c[2][i]*v[2];memcpy(out,r,12);
}
static inline void sense_unrotate(const HaloSenseTransform *m,const float v[3],float out[3]) {
    float r[3]={sense_dot(m->c[0],v),sense_dot(m->c[1],v),sense_dot(m->c[2],v)};memcpy(out,r,12);
}
static inline void sense_point(const HaloSenseTransform *m,const float v[3],float out[3]) {
    sense_rotate(m,v,out);for(unsigned i=0;i<3;i++)out[i]+=m->p[i];
}
/* Compose a rigid delta that maps source to target, preserving animation in
 * the descendants. Translation is independently anchored at the wrist. */
static inline HaloSenseTransform sense_delta(const HaloSenseTransform *source,const HaloSenseTransform *target,
                                             const float source_anchor[3],const float target_anchor[3]) {
    HaloSenseTransform d={0};
    for(unsigned j=0;j<3;j++)for(unsigned i=0;i<3;i++)
        for(unsigned k=0;k<3;k++)d.c[j][i]+=target->c[k][i]*source->c[k][j];
    sense_rotate(&d,source_anchor,d.p);for(unsigned i=0;i<3;i++)d.p[i]=target_anchor[i]-d.p[i];return d;
}
/* Deliberate front-hand reach engages automatically; a wider exit envelope
 * prevents jitter at the boundary. Behind/near hands and gesture clutch
 * never become an accidental foregrip. Coordinates here are Halo units. */
static inline int sense_support_span(const HaloSenseTransform *right,const HaloSenseTransform *left,int held) {
    float line[3];for(unsigned k=0;k<3;k++)line[k]=left->p[k]-right->p[k];
    float metres2=sense_dot(line,line)*3.048f*3.048f;
    float minimum=held?.12f:.20f,maximum=held?1.05f:.85f;
    if(!isfinite(metres2)||metres2<minimum*minimum||metres2>maximum*maximum||!sense_normalize(line))return 0;
    return sense_dot(line,right->c[0])>(held?.15f:.55f);
}
/* Two-hand sight line follows the physical hand span. Authored wrist offsets
 * calibrate the rendered foregrip, never add lateral/elevation bias to aim.
 * Only rotation changes; right remains the fixed grip anchor.
 * Halo world units are converted for span validation, never for model scale. */
static inline int sense_supported_pose(const HaloSenseTransform *right,
                                      const HaloSenseTransform *left,
                                      const float local_foregrip[3],
                                      HaloSenseTransform *out) {
    float line[3],up[3];
    for(unsigned k=0;k<3;k++)line[k]=left->p[k]-right->p[k];
    float metres2=sense_dot(line,line)*3.048f*3.048f;
    float original2=sense_dot(local_foregrip,local_foregrip)*3.048f*3.048f;
    if(!isfinite(metres2)||metres2<.12f*.12f||metres2>1.1f*1.1f||
       !isfinite(original2)||original2<.08f*.08f||original2>1.1f*1.1f)return 0;
    HaloSenseTransform target={0};
    memcpy(up,right->c[2],12);
    /* At a vertical aim, project the controller's left axis instead of
     * dividing by a nearly zero cross product. Roll stays controller-led. */
    float normalized[3];memcpy(normalized,line,12);if(!sense_normalize(normalized))return 0;
    if(fabsf(sense_dot(normalized,up))>.98f)sense_cross(normalized,right->c[1],up);
    if(!sense_basis(line,up,&target))return 0;
    *out=target;memcpy(out->p,right->p,12);
    return 1;
}
/* Implemented in the guest dispatch unit; used by panorama routing only. */
int host_sense_weapon_rendering(void);
#endif
