/* Deterministic tracked input: deterministic input ownership/release logic.
 * Caller supplies already-validated freshness, spatial ownership and actual
 * per-hand model readiness. No device polling or guest memory writes here. */
#ifndef HALO_DUAL_INPUT_H
#define HALO_DUAL_INPUT_H
#include <stdint.h>
#include <string.h>
#include <math.h>
typedef struct {
    uint64_t session, generation;
    uint32_t unit, primary, offhand;
} HaloDualOwnership;
typedef struct {
    HaloDualOwnership owner;
    int owns, armed[2];
} HaloDualInputGate;
typedef struct {
    HaloDualOwnership owner;
    int enabled, spatial, fresh, gameplay_supported;
    int model_ready[2], reload[2];
    float trigger[2];
} HaloDualInputSample;
typedef struct {
    int consume_left_trigger; /* suppress stock grenade whenever dual owns L2 */
    int fire[2], reload[2];
} HaloDualInputResult;
static inline int dual_owner_equal(HaloDualOwnership a,HaloDualOwnership b) {
    return a.session==b.session&&a.generation==b.generation&&a.unit==b.unit&&
        a.primary==b.primary&&a.offhand==b.offhand;
}
static inline HaloDualInputResult dual_input_update(HaloDualInputGate *gate,const HaloDualInputSample *sample) {
    HaloDualInputResult out={0};
    if(!gate)return out;
    if(!sample||!sample->enabled||!sample->spatial||!sample->owner.session) {
        memset(gate,0,sizeof *gate);return out;
    }
    out.consume_left_trigger=1;
    if(!gate->owns||!dual_owner_equal(gate->owner,sample->owner)) {
        memset(gate,0,sizeof *gate);gate->owner=sample->owner;gate->owns=1;
    }
    if(!sample->fresh||!sample->gameplay_supported||!sample->owner.generation||
       sample->owner.unit==UINT32_MAX||sample->owner.primary==UINT32_MAX||
       sample->owner.primary==sample->owner.offhand) {
        gate->armed[0]=gate->armed[1]=0;return out;
    }
    for(unsigned hand=0;hand<2;hand++) {
        const float trigger=sample->trigger[hand];
        if((hand==0&&sample->owner.offhand==UINT32_MAX)||!sample->model_ready[hand]||!isfinite(trigger)||trigger<0||trigger>1) {
            gate->armed[hand]=0;continue;
        }
        if(trigger<.25f)gate->armed[hand]=1;
        /* Each hand must release after ownership or readiness changes.
         * Reload wins, so holding a trigger cannot interrupt a requested
         * independent reload through this input path. */
        out.reload[hand]=!!sample->reload[hand];
        out.fire[hand]=gate->armed[hand]&&trigger>.5f&&!out.reload[hand];
    }
    return out;
}
#endif
