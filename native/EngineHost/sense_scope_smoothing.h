/* Optional scoped-aim smoothing for the accepted tracked weapon pose.
 *
 * While zoomed, small hand tremor is magnified by the scope. This filters
 * only the orientation, in the camera-relative (tracked) frame, so physical
 * and stick turning reach the view at once and hand position is untouched.
 * Each outer frame epoch moves the filtered orientation toward the accepted
 * one by 1 - exp(-dt / tau) of the remaining shortest arc, and never lets it
 * trail by more than HALO_SENSE_SCOPE_MAX_LAG_RADIANS: a deliberate swing
 * still arrives, only tremor is removed.
 *
 * Any change of session, input generation (recenter, reconnect, tracking
 * loss, setting toggle), unit, weapon or zoom level, a camera jump (snap
 * turn, cut, respawn), a gap over HALO_SENSE_SCOPE_GAP_NS, or leaving the
 * scope rebaselines to the exact input. With the setting off the input is
 * returned bit for bit. Pure: no globals, clocks or allocation. */
#ifndef HALO_SENSE_SCOPE_SMOOTHING_H
#define HALO_SENSE_SCOPE_SMOOTHING_H
#include "sense_grip_continuity.h"

#define HALO_SENSE_SCOPE_TAU_SECONDS .06f
#define HALO_SENSE_SCOPE_MAX_LAG_RADIANS .0698131701f  /* 4 degrees */
#define HALO_SENSE_SCOPE_GAP_NS UINT64_C(100000000)
#define HALO_SENSE_SCOPE_CAMERA_JUMP_RADIANS .261799388f /* 15 degrees between epochs */

typedef struct {
    uint64_t session, generation, epoch_ns;
    uint32_t unit, weapon;
    int zoom, valid;
    HaloSenseTransform relative, camera; /* filtered camera-relative pose; camera it was taken in */
} HaloSenseScopeFilter;

typedef struct {
    uint64_t session, generation, epoch_ns;
    uint32_t unit, weapon;
    int enabled, zoom;                  /* zoom < 0: not scoped */
    const HaloSenseTransform *camera;   /* world basis of the engine camera this epoch */
    const HaloSenseTransform *accepted; /* world pose after support and grip continuity */
} HaloSenseScopeInputPose;

static inline void sense_scope_filter_reset(HaloSenseScopeFilter *f) { memset(f, 0, sizeof *f); }
/* Rotation-only products in the column convention of sense_weapon.h. */
static inline void sense_scope_relative(const HaloSenseTransform *camera, const HaloSenseTransform *world, HaloSenseTransform *out) {
    HaloSenseTransform r = {0};
    for (unsigned j = 0; j < 3; j++) sense_unrotate(camera, world->c[j], r.c[j]);
    memcpy(r.p, world->p, 12); *out = r;
}
static inline void sense_scope_world(const HaloSenseTransform *camera, const HaloSenseTransform *relative, HaloSenseTransform *out) {
    HaloSenseTransform r = {0};
    for (unsigned j = 0; j < 3; j++) sense_rotate(camera, relative->c[j], r.c[j]);
    memcpy(r.p, relative->p, 12); *out = r;
}
/* Writes the pose all consumers use. Returns 0 only for unusable input. */
static inline int sense_scope_filter(HaloSenseScopeFilter *f, const HaloSenseScopeInputPose *in, HaloSenseTransform *out) {
    if (!in || !in->accepted || !sense_grip_finite_basis(in->accepted)) { sense_scope_filter_reset(f); return 0; }
    *out = *in->accepted;
    if (!in->enabled || in->zoom < 0 || !in->camera || !sense_grip_finite_basis(in->camera) || !in->epoch_ns) {
        sense_scope_filter_reset(f); return 1;
    }
    HaloSenseTransform target; sense_scope_relative(in->camera, in->accepted, &target);
    float axis[3], jump = 0, remaining = 0;
    int same = f->valid && f->session == in->session && f->generation == in->generation && f->unit == in->unit &&
        f->weapon == in->weapon && f->zoom == in->zoom && in->epoch_ns >= f->epoch_ns &&
        in->epoch_ns - f->epoch_ns <= HALO_SENSE_SCOPE_GAP_NS &&
        sense_grip_correction(&f->camera, in->camera, axis, &jump) && jump <= HALO_SENSE_SCOPE_CAMERA_JUMP_RADIANS &&
        sense_grip_correction(&f->relative, &target, axis, &remaining);
    if (!same) {
        sense_scope_filter_reset(f);
        f->valid = 1; f->session = in->session; f->generation = in->generation; f->unit = in->unit;
        f->weapon = in->weapon; f->zoom = in->zoom; f->epoch_ns = in->epoch_ns;
        f->relative = target; f->camera = *in->camera;
        return 1;                       /* first scoped epoch: exactly the accepted pose */
    }
    float dt = (float)(in->epoch_ns - f->epoch_ns) * 1e-9f;
    float keep = dt > 0 ? expf(-dt / HALO_SENSE_SCOPE_TAU_SECONDS) : 1.f;
    float lag = fminf(remaining * keep, HALO_SENSE_SCOPE_MAX_LAG_RADIANS);
    HaloSenseTransform filtered = target;
    /* filtered = target (x) rotation(-axis, lag) keeps `lag` of the arc. */
    if (lag > HALO_SENSE_GRIP_DONE_RADIANS) {
        float back[3] = {-axis[0], -axis[1], -axis[2]};
        if (!sense_grip_apply(&target, back, lag, &filtered)) filtered = target;
    }
    f->relative = filtered; f->camera = *in->camera; f->epoch_ns = in->epoch_ns;
    sense_scope_world(in->camera, &filtered, out);
    HaloSenseTransform rebuilt;
    if (!sense_basis(out->c[0], out->c[2], &rebuilt)) { *out = *in->accepted; return 1; }
    memcpy(rebuilt.p, in->accepted->p, 12); *out = rebuilt;
    return 1;
}
#endif
