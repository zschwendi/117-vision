/* Physical grenade release on the original PC1.10 transaction (verified
 * c9acf0c4...). Original unit+0x28D state machine: the retail caller
 * 005625B0 switches on +0x28D (005630FE, table 00563850) and only its state-0
 * case (00563116, grenade control bit set) calls 0056E080 (EDI unit, one
 * cdecl argument, at 0056311F); 0056E080 itself does not test the state. It
 * accepts a throw (AL=1, state 1, throw animation) or rejects it (no selected
 * type or none left, weapon busy 004C2F30, animation refused 00565F90); at
 * animation frame 2 0056E280 debits one grenade and attaches it to the unit's
 * 'left hand' marker (state 2); at the throw keyframe (or when interrupted,
 * arg1=1) 0056E440 detaches it, computes the desired velocity unit-tag+0x2C0
 * * [0x672ACC](=1/30) along unit+0x23C, passes desired-minus-current as a
 * DELTA to 004BEF80 (adds it, adds spin), sets state 3, then sweeps
 * eye->projectile with 004F7B70 (collision clamp, or delete via 004F5BD0).
 *
 * A Sense release binds to the throw the original accepted in the tick its
 * DI pulse was delivered, and is frozen there in world space from the outer
 * render at or before its sample: origin, basis and measured velocity never
 * pass through a later camera or unit position. The native release latency
 * is kept; nothing is simulated across it. Launch speed (user choice
 * "Halo-like range, controlled by my throw"): the measured direction at
 * clamp(hand speed / 5 m/s, 0, 1) of the original transaction's own desired
 * speed (stock unit-tag speed, or its lower interrupted blend); a still hand
 * drops the grenade. 5 m/s is a game-feel calibration, not a measured human
 * comfort limit. A bound throw is never handed
 * back to the original aim: before creation it cancels with the original
 * creation-failure state (no debit, no object); after creation with the
 * original's own detach (004F6610) and delete (004F5BD0), state 3 and no
 * stale pointer, without refunding the original's single debit. Ordinary
 * input binds nothing and is untouched. */
#if !defined(HALO_SENSE_MOUNT_H)&&!defined(HALO_SENSE_GRENADE_LOCAL_MOUNT)&&defined(__has_include)
#if __has_include("sense_mount.h")
#include "sense_mount.h"
#endif
#endif
static int sense_grenade_in_accept,sense_grenade_in_create,sense_grenade_in_release;

/* Mount gate: same mount generation and on foot. The turret producer
 * (sense_mount.h) is authoritative when visible; the standalone fallback
 * observes the original parent (+0x11C) / seat (+0x2F0) bytes at each render
 * and gate, which cannot see a seat cycle completed between observations. */
/* Full launch strength at this measured hand speed (metres/second). */
#define SENSE_GRENADE_FULL_STRENGTH_MPS 5.f
#ifdef HALO_SENSE_MOUNT_H
/* Admission boundary: observe the live seat identity before any new grenade
 * authority (the producer poisons this binding on a change). Never from
 * inside an in-flight original action. */
static void sense_grenade_mount_refresh(void){(void)host_sense_mount_refresh();}
static int sense_grenade_mount(uint32_t datum,uint32_t unit,uint64_t *generation) {
    (void)unit;HaloSenseMountIdentity id;*generation=0;
    if(!host_sense_mount_identity(&id)||id.mode!=HALO_SENSE_MOUNT_ON_FOOT||id.unit!=datum||!id.generation)return 0;
    *generation=id.generation;return 1;
}
#else
static void sense_grenade_mount_refresh(void){}
static uint64_t sense_grenade_mount_generation;
static uint32_t sense_grenade_mount_unit=UINT32_MAX;
static int sense_grenade_mount_foot=-1;
static int sense_grenade_mount(uint32_t datum,uint32_t unit,uint64_t *generation) {
    int foot=unit&&G32(unit+0x11C)==UINT32_MAX&&G16(unit+0x2F0)==UINT16_MAX;
    if(datum!=sense_grenade_mount_unit||foot!=sense_grenade_mount_foot){
        sense_grenade_mount_unit=datum;sense_grenade_mount_foot=foot;
        if(++sense_grenade_mount_generation==0)++sense_grenade_mount_generation;
    }
    *generation=sense_grenade_mount_generation;return foot;
}
#endif

/* Outer render cameras: the panorama basis a published sample refers to. */
#define SENSE_GRENADE_CAMERAS 128u
typedef struct { uint64_t ns,mount,session; uint32_t unit; HaloSenseTransform camera; float eye[3]; } SenseGrenadeCamera;
static SenseGrenadeCamera sense_grenade_cameras[SENSE_GRENADE_CAMERAS];
static unsigned sense_grenade_camera_next;
/* After the outer 0050BEA0 camera update. */
static void sense_grenade_render_record(void) {
    uint32_t datum;uint64_t mount=0;uint32_t unit=sense_local_body(&datum);
    if(!sense_grenade_mount(unit?datum:UINT32_MAX,unit,&mount)||!unit||sense_camera_unit!=datum||G16(0x00719720u)!=0)return;
    HaloSenseStatus status;halo_sense_status(&status);if(!status.session)return;
    SenseGrenadeCamera *c=&sense_grenade_cameras[sense_grenade_camera_next++%SENSE_GRENADE_CAMERAS];
    c->ns=host_monotonic_ns();c->mount=mount;c->session=status.session;c->unit=datum;c->camera=sense_camera;
    memcpy(c->eye,GPTR(unit+0x5C),12);for(unsigned k=0;k<3;k++)c->eye[k]+=sense_camera_from_unit[k];
}
static int sense_grenade_camera_at(uint32_t datum,uint64_t session,uint64_t ns,SenseGrenadeCamera *out) {
    const SenseGrenadeCamera *best=NULL;
    for(unsigned i=0;i<SENSE_GRENADE_CAMERAS;i++){
        const SenseGrenadeCamera *c=&sense_grenade_cameras[i];
        if(c->ns&&c->unit==datum&&c->session==session&&c->ns<=ns&&ns-c->ns<=UINT64_C(250000000)&&(!best||c->ns>best->ns))best=c;
    }
    if(!best||!sense_finite3(best->eye))return 0;
    *out=*best;return 1;
}

static struct {
    uint64_t epoch,mount,session,controller;
    uint32_t unit,projectile;
    float origin[3],direction[3],velocity[3]; /* world units; raw measured velocity per original tick */
    float strength; /* clamp(measured m/s / SENSE_GRENADE_FULL_STRENGTH_MPS, 0, 1) */
    int bound;    /* the original accepted this unit's throw on a Sense pulse */
    int poisoned; /* bound but unhonourable: cancel at creation or release */
    int armed;    /* created by the original under this binding */
    int applied;  /* frozen origin and velocity reached the original's 004BEF80 */
} sense_grenade_release;
static void sense_grenade_clear(void){memset(&sense_grenade_release,0,sizeof sense_grenade_release);sense_grenade_release.projectile=UINT32_MAX;}
/* Shared seat transition contract (turret lane): poison, never release
 * ownership to the original aim. */
#define HALO_SENSE_GRENADE_CANCEL_MOUNT 1
static void sense_grenade_cancel_for_mount(void){if(sense_grenade_release.bound)sense_grenade_release.poisoned=1;}

/* Measured release -> world, against the engine-clock anchor read before the
 * bridge computed the sample's age. Metres to Halo units: /3.048 (as every
 * tracked pose); per second to per original tick: *[0x672ACC]=1/30, the
 * factor 0056E440 applies to the unit tag speed. */
static int sense_grenade_freeze(const HaloSenseGrenadeRelease *r,uint32_t datum,uint64_t mount,uint64_t anchor) {
    const HaloSenseGrenadeSample *s=&r->sample;const float *p=s->hand.position;SenseGrenadeCamera cam;
    if(!s->hand.valid||!s->motion_valid||!sense_finite3(p)||sense_dot(p,p)>9.f||!sense_finite3(s->velocity)||
       r->age_ns>anchor||!sense_grenade_camera_at(datum,r->session,anchor-r->age_ns,&cam)||cam.mount!=mount)return 0;
    float offset[3],velocity[3],direction[3];
    sense_rotate(&cam.camera,p,offset);sense_rotate(&cam.camera,s->velocity,velocity);
    for(unsigned k=0;k<3;k++){
        sense_grenade_release.origin[k]=cam.eye[k]+offset[k]/3.048f;
        sense_grenade_release.velocity[k]=velocity[k]/3.048f/30.f;
    }
    /* Direction only orients the original's own spawn basis before the
     * frozen origin replaces it; a still hand keeps its pointing basis and
     * its zero velocity (a drop from the hand). */
    float mps=sqrtf(sense_dot(velocity,velocity));if(!isfinite(mps))return 0;
    sense_grenade_release.strength=fminf(mps/SENSE_GRENADE_FULL_STRENGTH_MPS,1.f);
    memcpy(direction,velocity,12);
    if(!sense_normalize(direction)){sense_grenade_release.strength=0;sense_rotate(&cam.camera,s->hand.forward,direction);if(!sense_normalize(direction))return 0;}
    memcpy(sense_grenade_release.direction,direction,12);
    return sense_finite3(sense_grenade_release.origin)&&sense_finite3(sense_grenade_release.velocity);
}
/* The local player's unit datum, alive or not. */
static uint32_t sense_grenade_player(uint32_t *datum) {
    uint32_t globals=G32(0x0087A478u),array=G32(0x0087A480u);
    if(!sense_range(globals,0x12)||!sense_range(array,0x38))return 0;
    uint32_t player=G32(globals+4),rows=G32(array+0x34);unsigned count=G16(array+0x20),i=player&65535u;
    if(player==UINT32_MAX||!count||count>16||i>=count||G16(array+0x22)!=0x200||!sense_range(rows,count*0x200u))return 0;
    uint32_t row=rows+i*0x200u;if(G16(row)!=(player>>16))return 0;
    *datum=G32(row+0x34);return sense_object(*datum,0x308,0);
}
/* The bound throw may still be honoured: same living on-foot unit, mount
 * generation, Sense session/controller ownership, no launcher UI. */
static int sense_grenade_honourable(uint32_t datum,uint32_t unit) {
    uint32_t alive;uint64_t mount=0;HaloSenseStatus status;halo_sense_status(&status);
    return !sense_grenade_release.poisoned&&sense_local_body(&alive)==unit&&alive==datum&&G16(0x00719720u)==0&&
        !halo_sense_app_input_blocked()&&status.active&&status.session==sense_grenade_release.session&&
        status.controller_generation==sense_grenade_release.controller&&
        sense_grenade_mount(datum,unit,&mount)&&mount==sense_grenade_release.mount;
}
static void sense_grenade_trace(const char *event,uint32_t unit) {
    if(!HOST_ENV("HALO_SENSE_WEAPON_TRACE"))return;
    static unsigned logs;const float *o=sense_grenade_release.origin,*v=sense_grenade_release.velocity;
    if(logs++<64)host_log("[sense-grenade] %s unit=%08x epoch=%llu origin=%.4f,%.4f,%.4f velocity=%.5f,%.5f,%.5f",
        event,unit,(unsigned long long)sense_grenade_release.epoch,o[0],o[1],o[2],v[0],v[1],v[2]);
}
/* Marked by the original 004F59D0 (entry byte +2 bit 0x08), freed later:
 * sense_object still resolves it until then, but it must not be cleaned again. */
static int sense_grenade_deleting(uint32_t datum) {
    uint32_t table=G32(0x008603B0u),entries=sense_range(table,0x38)?G32(table+0x34):0;
    return entries&&sense_range(entries+(datum&65535u)*12u,12)&&(G8(entries+(datum&65535u)*12u+2)&8u);
}
/* The original's own cleanup of an attached grenade that must not fly:
 * detach (004F6610) then delete (004F5BD0 -> 004F59D0), once. */
static void sense_grenade_delete(EngineCPU *cpu,uint32_t projectile) {
    uint32_t body=sense_object(projectile,0x1F4,5);if(!body||sense_grenade_deleting(projectile))return;
    if(G32(body+0x11C)!=UINT32_MAX){EngineCPU detach=*cpu;uint32_t arg=projectile;host_call_guest(&detach,0x004F6610u,1,&arg,0);cpu->instruction_count=detach.instruction_count;}
    if(!sense_object(projectile,0x1F4,5)||sense_grenade_deleting(projectile))return;
    EngineCPU remove=*cpu;remove.gpr[0]=projectile;host_call_guest(&remove,0x004F5BD0u,0,NULL,0);cpu->instruction_count=remove.instruction_count;
}
static int sense_grenade_dispatch(EngineCPU *cpu,uint32_t address) {
    if(address==0x004BEF80u){
        /* Only the original's own release (return 0056E75A) of this throw. */
        uint32_t projectile=sense_grenade_release.projectile,delta=cpu->gpr[2];
        if(!sense_grenade_in_release||!sense_grenade_release.armed||G32(cpu->gpr[4])!=0x0056E75Au||cpu->gpr[0]!=projectile)return 0;
        static uint32_t scratch;if(!scratch)scratch=guest_alloc(0x100);
        uint32_t body=sense_object(projectile,0x1F4,5);int placed=0;
        if(body&&G32(body+0x11C)==UINT32_MAX&&sense_range(delta,12)&&sense_range(scratch,0x100)){
            memcpy(GPTR(scratch),sense_grenade_release.origin,12);
            EngineCPU move=*cpu;move.gpr[7]=projectile;move.gpr[6]=scratch;uint32_t arg=0;
            host_call_guest(&move,0x004F5350u,1,&arg,0);cpu->instruction_count=move.instruction_count;
            placed=sense_object(projectile,0x1F4,5)==body&&!memcmp(GPTR(body+0x5C),sense_grenade_release.origin,12);
        }
        float d[3]={0},current[3]={0},cap=NAN,strength=sense_grenade_release.strength;
        if(placed){
            memcpy(d,GPTR(delta),12);memcpy(current,GPTR(body+0x68),12);
            float native[3]={d[0]+current[0],d[1]+current[1],d[2]+current[2]};
            cap=sqrtf(sense_dot(native,native));
        }
        if(!placed||!sense_finite3(current)||!sense_finite3(d)||!isfinite(cap)||!isfinite(strength)||strength<0.f||strength>1.f){
            /* Fail closed: no original velocity is added; the caller deletes it. */
            sense_grenade_trace("release-failed",sense_grenade_release.unit);ret_to_caller(cpu);return 1;
        }
        /* The original's own desired speed for this call (stock, or its
         * interrupted blend) scaled by the measured strength, along the
         * measured direction. The original adds this delta and derives its
         * spin from it. */
        for(unsigned k=0;k<3;k++)d[k]=sense_grenade_release.direction[k]*cap*strength-current[k];
        memcpy(GPTR(delta),d,12);sense_grenade_release.applied=1;
        return 0; /* original velocity add, spin and the final eye sweep */
    }
    if(address==0x0056E080u){
        if(sense_grenade_in_accept)return 0;
        uint32_t datum,unit=sense_grenade_player(&datum),stack=cpu->gpr[4];
        if(!unit||cpu->gpr[7]!=datum||!sense_range(stack,8))return 0;
        uint32_t caller=G32(stack);
        if(sense_grenade_release.bound&&sense_grenade_release.unit==datum&&(G8(unit+0x28D)==1||G8(unit+0x28D)==2)){
            /* A start callback during this unit's bound, in-progress throw
             * (the retail caller never makes one: it calls only from state 0).
             * Keep the first token: no restart of the original throw, no new
             * token consumed; reported to the caller as not accepted. */
            cpu->gpr[0]&=~255u;ret_to_caller(cpu);sense_grenade_trace("duplicate-start-refused",datum);return 1;
        }
        sense_grenade_mount_refresh();
        sense_grenade_in_accept=1;engine_dispatch(cpu,address);sense_grenade_in_accept=0;
        /* Only the original's actual acceptance (AL=1 and state 1) binds; a
         * rejected pulse is spent by the bridge and never reaches a later throw. */
        if(cpu->pc!=caller||(cpu->gpr[0]&255u)!=1||G8(unit+0x28D)!=1)return 1;
        uint64_t anchor=host_monotonic_ns(); /* before the bridge measures the sample age */
        uint32_t time=G32(0x006F1D6Cu),clock=sense_range(time,16)?time:0,tick=clock?G32(time+12u):0;
        HaloSenseGrenadeRelease release;int result=halo_sense_grenade_accept(clock,tick,&release);
        if(sense_grenade_release.armed&&sense_grenade_release.unit==datum){
            /* Reached only if the unit left its throw states without our
             * release or cancel (the duplicate guard above covers states 1/2).
             * Never strand its grenade: original cleanup, no throw. */
            if(G32(unit+0x294)==sense_grenade_release.projectile)S32(unit+0x294,UINT32_MAX);
            sense_grenade_delete(cpu,sense_grenade_release.projectile);sense_grenade_trace("superseded-created",datum);
        }
        sense_grenade_clear(); /* this accepted throw supersedes any older, uncreated binding */
        if(!result)return 1; /* ordinary input: the original, unchanged */
        HaloSenseStatus status;halo_sense_status(&status);uint64_t mount=0;
        sense_grenade_release.bound=1;sense_grenade_release.unit=datum;sense_grenade_release.epoch=release.epoch;
        sense_grenade_release.session=status.session;sense_grenade_release.controller=status.controller_generation;
        int ok=result>0&&release.session==status.session&&sense_grenade_mount(datum,unit,&mount)&&
            sense_grenade_freeze(&release,datum,mount,anchor);
        sense_grenade_release.mount=mount;sense_grenade_release.poisoned=!ok;
        sense_grenade_trace(ok?"accepted":"accepted-cancel",datum);
        return 1;
    }
    if(address==0x0056E280u){
        if(sense_grenade_in_create||!sense_grenade_release.bound||ARG(0)!=sense_grenade_release.unit)return 0;
        uint32_t datum=ARG(0),unit=sense_object(datum,0x308,0);
        if(sense_grenade_release.armed){
            /* A second creation call for a created binding (the original calls
             * 0056E280 only from state 1): refuse it, keep the first grenade and
             * its single debit; never a second debit or a stranded attachment. */
            ret_to_caller(cpu);sense_grenade_trace("create-refused",datum);return 1;
        }
        if(!unit){sense_grenade_clear();return 0;}
        sense_grenade_mount_refresh();
        if(!sense_grenade_honourable(datum,unit)){
            /* Original creation-failure state (0056E42C): no debit, no object. */
            S8(unit+0x28D,3);ret_to_caller(cpu);sense_grenade_trace("cancel-before-create",datum);sense_grenade_clear();return 1;
        }
        sense_grenade_in_create=1;engine_dispatch(cpu,address);sense_grenade_in_create=0;
        uint32_t grenade=G32(unit+0x294);
        if(G8(unit+0x28D)==2&&sense_object(grenade,0x1F4,5)){sense_grenade_release.projectile=grenade;sense_grenade_release.armed=1;sense_grenade_trace("created",datum);}
        else sense_grenade_clear();
        return 1;
    }
    if(address!=0x0056E440u||sense_grenade_in_release||!sense_grenade_release.armed||ARG(0)!=sense_grenade_release.unit)return 0;
    uint32_t datum=ARG(0),unit=sense_object(datum,0x308,0),projectile=sense_grenade_release.projectile;
    uint32_t body=sense_object(projectile,0x1F4,5);
    /* Not this transaction (another projectile): ownership never transfers;
     * the original handles what it references. */
    if(!unit||G8(unit+0x28D)!=2||G32(unit+0x294)!=projectile||!body){sense_grenade_clear();return 0;}
    sense_grenade_mount_refresh();
    float from_unit[3];memcpy(from_unit,GPTR(unit+0x5C),12);
    for(unsigned k=0;k<3;k++)from_unit[k]=sense_grenade_release.origin[k]-from_unit[k];
    if(sense_grenade_deleting(projectile)||!sense_grenade_honourable(datum,unit)||G32(unit+0x1F4)!=UINT32_MAX||
       !sense_finite3(from_unit)||sense_dot(from_unit,from_unit)>16.f){
        /* Death, mount/seat or ownership change after the debit: the original's
         * cleanup (skipped if the original already marked it deleted), the
         * original's released state (0056E440: +0x294=-1, state 3), no throw
         * and no refund. */
        sense_grenade_delete(cpu,projectile);
        S32(unit+0x294,UINT32_MAX);S8(unit+0x28D,3);ret_to_caller(cpu);
        sense_grenade_trace("cancel-after-create",datum);sense_grenade_clear();return 1;
    }
    unsigned char saved[12];memcpy(saved,GPTR(unit+0x23C),sizeof saved);
    memcpy(GPTR(unit+0x23C),sense_grenade_release.direction,12);
    sense_grenade_trace("release",datum);
    sense_grenade_release.applied=0;
    sense_grenade_in_release=1;engine_dispatch(cpu,address);sense_grenade_in_release=0;
    if(sense_object(datum,0x308,0)==unit)memcpy(GPTR(unit+0x23C),saved,sizeof saved);
    if(!sense_grenade_release.applied)sense_grenade_delete(cpu,projectile); /* fail closed: never a stock-power flight */
    sense_grenade_clear();
    return 1;
}
