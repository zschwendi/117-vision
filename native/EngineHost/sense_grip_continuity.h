/* Grab/release continuity for the tracked two-hand rifle pose.
 *
 * Design reference, math idea only (no code copied): virtual_stock_aim_continuity.h
 * from moistman42069/MCCVR-Halo-Build b243af2256a1a3b424cbccd940f8e8a674cef608
 * (MIT). That implementation corrects presentation only. Here the corrected
 * transform IS the accepted weapon pose: the model, reticle, scope camera and
 * projectile ray all consume the value returned below, so they cannot differ.
 *
 *   presented  = live (x) correction        (correction in the gun's own frame)
 *   correction = live^T (x) anchor          (seeded on a support edge)
 *
 * The anchor is what the player was being shown: the other mode's same-frame
 * solve under its running correction, else the last accepted pose. A fixed
 * 200 ms smoothstep then shrinks the correction's shortest arc to identity,
 * after which presented == live exactly, so the current right->left span stays
 * the authoritative two-hand ray. Translation never changes: the right grip is
 * the anchor, the muzzle follows the presented barrel rigidly, nothing scales.
 *
 * Time is the engine frame epoch, not each caller's clock, so every consumer in
 * one frame receives the same rotation for the same live input. A change of
 * session, input generation (recenter, reconnect, tracking loss, scope), unit
 * or weapon, a missing epoch, or a gap longer than HALO_SENSE_GRIP_GAP_NS
 * rebaselines without an edge: those never animate. Pure: no globals, clocks,
 * logging or allocation. */
#ifndef HALO_SENSE_GRIP_CONTINUITY_H
#define HALO_SENSE_GRIP_CONTINUITY_H
#include "sense_weapon.h"

#define HALO_SENSE_GRIP_EASE_NS UINT64_C(200000000)
#define HALO_SENSE_GRIP_GAP_NS UINT64_C(250000000)
#define HALO_SENSE_GRIP_DONE_RADIANS 1e-5f

typedef struct {
    uint64_t session, generation, epoch, epoch_ns, seed_ns;
    uint32_t unit, weapon;
    int initialized, supported, active, presented_valid;
    float axis[3], angle;            /* seeded correction: unit axis, [0, pi] */
    HaloSenseTransform presented;    /* last accepted pose */
} HaloSenseGripContinuity;

typedef struct {
    uint64_t session, generation, epoch, epoch_ns;
    uint32_t unit, weapon;
    int supported;
    const HaloSenseTransform *live;  /* this mode's solve: two-hand when supported */
    const HaloSenseTransform *other; /* same-frame solve of the other mode, or NULL */
} HaloSenseGripInput;

static inline void sense_grip_reset(HaloSenseGripContinuity *s) { memset(s, 0, sizeof *s); }

static inline int sense_grip_finite_basis(const HaloSenseTransform *m) {
    for (unsigned j = 0; j < 3; j++)
        for (unsigned i = 0; i < 3; i++) if (!isfinite(m->c[j][i])) return 0;
    return 1;
}
/* Shortest arc of live^T*anchor as axis/angle. Shepperd's branch choice keeps
 * the antipodal (180 degree) case accurate; the axis is then still finite. */
static inline int sense_grip_correction(const HaloSenseTransform *live, const HaloSenseTransform *anchor,
                                        float axis[3], float *angle) {
    if (!sense_grip_finite_basis(live) || !sense_grip_finite_basis(anchor)) return 0;
    float m[3][3];
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++) m[i][j] = sense_dot(live->c[i], anchor->c[j]);
    float q[4], trace = m[0][0] + m[1][1] + m[2][2], s;
    if (trace > 0) {
        s = sqrtf(trace + 1.f) * 2.f;
        q[3] = .25f * s; q[0] = (m[2][1] - m[1][2]) / s; q[1] = (m[0][2] - m[2][0]) / s; q[2] = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        s = sqrtf(fmaxf(1.f + m[0][0] - m[1][1] - m[2][2], 0.f)) * 2.f;
        q[3] = (m[2][1] - m[1][2]) / s; q[0] = .25f * s; q[1] = (m[0][1] + m[1][0]) / s; q[2] = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        s = sqrtf(fmaxf(1.f + m[1][1] - m[0][0] - m[2][2], 0.f)) * 2.f;
        q[3] = (m[0][2] - m[2][0]) / s; q[0] = (m[0][1] + m[1][0]) / s; q[1] = .25f * s; q[2] = (m[1][2] + m[2][1]) / s;
    } else {
        s = sqrtf(fmaxf(1.f + m[2][2] - m[0][0] - m[1][1], 0.f)) * 2.f;
        q[3] = (m[1][0] - m[0][1]) / s; q[0] = (m[0][2] + m[2][0]) / s; q[1] = (m[1][2] + m[2][1]) / s; q[2] = .25f * s;
    }
    if (q[3] < 0) for (unsigned k = 0; k < 4; k++) q[k] = -q[k]; /* shortest arc to identity */
    float v = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    if (!isfinite(v) || !isfinite(q[3])) return 0;
    *angle = 2.f * atan2f(v, q[3]);
    if (v < 1e-12f) { axis[0] = 1; axis[1] = axis[2] = 0; *angle = 0; return 1; }
    for (unsigned k = 0; k < 3; k++) axis[k] = q[k] / v;
    return isfinite(*angle);
}
/* out = live (x) rotation(axis, angle), rebuilt orthonormal. Position is live's. */
static inline int sense_grip_apply(const HaloSenseTransform *live, const float axis[3], float angle,
                                   HaloSenseTransform *out) {
    float h = .5f * angle, sn = sinf(h), w = cosf(h), x = axis[0] * sn, y = axis[1] * sn, z = axis[2] * sn;
    float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                     {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                     {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    float c[3][3];
    for (unsigned j = 0; j < 3; j++) for (unsigned i = 0; i < 3; i++)
        c[j][i] = live->c[0][i] * r[0][j] + live->c[1][i] * r[1][j] + live->c[2][i] * r[2][j];
    HaloSenseTransform result;
    if (!sense_basis(c[0], c[2], &result)) return 0;
    memcpy(result.p, live->p, 12); *out = result; return 1;
}
/* Remaining correction angle at an epoch; zero once the ease completes. */
static inline float sense_grip_remaining(const HaloSenseGripContinuity *s, uint64_t epoch_ns) {
    if (!s->active || epoch_ns < s->seed_ns) return s->active ? s->angle : 0.f;
    uint64_t elapsed = epoch_ns - s->seed_ns;
    if (elapsed >= HALO_SENSE_GRIP_EASE_NS) return 0.f;
    float t = (float)elapsed / (float)HALO_SENSE_GRIP_EASE_NS, u = t * t * (3.f - 2.f * t);
    return s->angle * (1.f - u);
}
/* Writes the accepted pose. Returns 0 only without a usable live solve. */
static inline int sense_grip_present(HaloSenseGripContinuity *s, const HaloSenseGripInput *in, HaloSenseTransform *out) {
    if (!in || !in->live || !sense_grip_finite_basis(in->live)) { sense_grip_reset(s); return 0; }
    *out = *in->live;
    if (!in->epoch || !in->epoch_ns) { sense_grip_reset(s); return 1; }
    int same = s->initialized && s->session == in->session && s->generation == in->generation &&
        s->unit == in->unit && s->weapon == in->weapon && in->epoch_ns >= s->epoch_ns &&
        in->epoch_ns - s->epoch_ns <= HALO_SENSE_GRIP_GAP_NS;
    if (!same) {
        sense_grip_reset(s);
        s->initialized = 1; s->session = in->session; s->generation = in->generation;
        s->unit = in->unit; s->weapon = in->weapon; s->supported = !!in->supported;
    } else if (!!in->supported != s->supported) {
        HaloSenseTransform anchor; int anchored = 0;
        if (in->other && sense_grip_finite_basis(in->other)) {
            float running = sense_grip_remaining(s, in->epoch_ns);
            anchored = running > 0 ? sense_grip_apply(in->other, s->axis, running, &anchor) : (anchor = *in->other, 1);
        }
        if (!anchored && s->presented_valid) { anchor = s->presented; anchored = 1; }
        float axis[3], angle;
        s->supported = !!in->supported; s->active = 0;
        if (anchored && sense_grip_correction(in->live, &anchor, axis, &angle) && angle > HALO_SENSE_GRIP_DONE_RADIANS) {
            memcpy(s->axis, axis, sizeof axis); s->angle = angle; s->seed_ns = in->epoch_ns; s->active = 1;
        }
    }
    if (s->active) {
        float remaining = sense_grip_remaining(s, in->epoch_ns);
        if (remaining <= HALO_SENSE_GRIP_DONE_RADIANS || !sense_grip_apply(in->live, s->axis, remaining, out)) {
            s->active = 0; *out = *in->live;
        }
    }
    s->epoch = in->epoch; s->epoch_ns = in->epoch_ns;
    s->presented = *out; s->presented_valid = 1;
    return 1;
}
#endif
