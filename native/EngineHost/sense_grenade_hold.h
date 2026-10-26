/* Armed grenade drawn in the tracked left hand (pure geometry; no guest or
 * bridge state). Coordinates are Halo world units; metre constants are
 * divided by 3.048 exactly once, as every tracked pose is.
 *
 * Placement: the palm of the left hand the viewmodel actually draws, from the
 * first-person graph's own 'frame l wriste' child knuckles ('frame l index
 * low', 'frame l middlelow', 'frame l ring low', 'frame l pinky low'; names
 * from the retail first-person graphs). The palm normal of a LEFT hand is
 * -(index-pinky) x (knuckles-wrist). A rig without those nodes uses the
 * tracked left grip with a fixed controller-frame offset instead. Both offsets
 * are cosmetic calibrations, not measurements.
 *
 * Model nodes: the original pose of an unanimated model. 004D7610 copies each
 * model node's default rotation (+0x34, quaternion) and translation (+0x28)
 * with scale 1; 004D7690 builds node 0 from its root (004CB970: forward, up x
 * forward, up; translation) and every node as parent x local, converting the
 * quaternion with 004CBAD0 and multiplying 52-byte matrices with [00696664]
 * (004CC0D0). sense_grenade_compose reproduces that composition. */
#ifndef HALO_SENSE_GRENADE_HOLD_H
#define HALO_SENSE_GRENADE_HOLD_H
#include "sense_weapon.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SENSE_GRENADE_HOLD_MAX_NODES 4u
#define SENSE_GRENADE_PALM_SHARE .55f   /* wrist -> knuckles */
#define SENSE_GRENADE_PALM_LIFT_M .035f /* out of the palm: about a grenade radius */
static const float sense_grenade_grip_offset_m[3] = {.06f, -.02f, -.03f}; /* forward, left, up */

/* Palm frame: forward along the fingers, up out of the back of the hand,
 * origin a grenade radius out of the palm. */
static inline int sense_grenade_palm(const float wrist[3], const float index[3], const float middle[3],
                                     const float ring[3], const float pinky[3], HaloSenseTransform *out) {
    float knuckles[3], fingers[3], across[3], back[3];
    for (unsigned k = 0; k < 3; ++k) {
        knuckles[k] = (index[k] + middle[k] + ring[k] + pinky[k]) * .25f;
        fingers[k] = knuckles[k] - wrist[k]; across[k] = index[k] - pinky[k];
    }
    float span = sqrtf(sense_dot(fingers, fingers)) * 3.048f;
    /* A real hand: wrist to knuckles 3..20 cm, a measurable knuckle line. */
    if (!isfinite(span) || span < .03f || span > .20f) return 0;
    sense_cross(across, fingers, back); /* left hand: out of the back of the hand */
    if (!sense_normalize(back) || !sense_basis(fingers, back, out)) return 0;
    for (unsigned k = 0; k < 3; ++k)
        out->p[k] = wrist[k] + fingers[k] * SENSE_GRENADE_PALM_SHARE - out->c[2][k] * SENSE_GRENADE_PALM_LIFT_M / 3.048f;
    return isfinite(out->p[0]) && isfinite(out->p[1]) && isfinite(out->p[2]);
}
/* Tracked left grip (accepted frame, Halo units) -> grenade frame. */
static inline int sense_grenade_grip(const HaloSenseTransform *grip, HaloSenseTransform *out) {
    if (!sense_basis(grip->c[0], grip->c[2], out)) return 0;
    float offset[3]; for (unsigned k = 0; k < 3; ++k) offset[k] = sense_grenade_grip_offset_m[k] / 3.048f;
    sense_rotate(out, offset, out->p);
    for (unsigned k = 0; k < 3; ++k) out->p[k] += grip->p[k];
    return isfinite(out->p[0]) && isfinite(out->p[1]) && isfinite(out->p[2]);
}
/* 004CBAD0's quaternion (x, y, z, w) to the column basis (the same
 * conversion sense_muzzle_marker verified for marker rotations). */
static inline int sense_grenade_quaternion(const float q[4], float c[3][3]) {
    float n2 = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
    if (!isfinite(n2) || n2 < 1e-8f) return 0;
    float t = 2.f / n2, x = q[0], y = q[1], z = q[2], w = q[3];
    float m[3][3] = {{1-t*(y*y+z*z), t*(x*y-w*z), t*(x*z+w*y)},
                     {t*(x*y+w*z), 1-t*(x*x+z*z), t*(y*z-w*x)},
                     {t*(x*z-w*y), t*(y*z+w*x), 1-t*(x*x+y*y)}};
    memcpy(c, m, sizeof m); return 1;
}
/* palette rows: scale, c[0..2] (forward, left, up), translation. parents[i]
 * must precede i (node 0 is the root's child). Returns 0 on any invalid or
 * non-finite input; nothing is written then. */
static inline int sense_grenade_compose(const HaloSenseTransform *root, unsigned count, const int16_t *parents,
                                        const float (*rotation)[4], const float (*translation)[3],
                                        float palette[][13]) {
    float out[SENSE_GRENADE_HOLD_MAX_NODES][13];
    if (!count || count > SENSE_GRENADE_HOLD_MAX_NODES) return 0;
    for (unsigned i = 0; i < count; ++i) {
        float parent[13], local[3][3];
        if (!i) { if (parents[0] != -1) return 0; parent[0] = 1; memcpy(parent + 1, root->c, 36); memcpy(parent + 10, root->p, 12); }
        else { if (parents[i] < 0 || parents[i] >= (int)i) return 0; memcpy(parent, out[parents[i]], 52); }
        if (!sense_grenade_quaternion(rotation[i], local)) return 0;
        float *node = out[i]; node[0] = parent[0];
        for (unsigned col = 0; col < 3; ++col) for (unsigned row = 0; row < 3; ++row)
            node[1 + col*3 + row] = parent[1 + row]*local[col][0] + parent[4 + row]*local[col][1] + parent[7 + row]*local[col][2];
        for (unsigned row = 0; row < 3; ++row)
            node[10 + row] = parent[10 + row] + parent[0]*(parent[1 + row]*translation[i][0] + parent[4 + row]*translation[i][1] +
                                                          parent[7 + row]*translation[i][2]);
        for (unsigned k = 0; k < 13; ++k) if (!isfinite(node[k])) return 0;
    }
    memcpy(palette, out, count * 52u); return 1;
}
#endif
