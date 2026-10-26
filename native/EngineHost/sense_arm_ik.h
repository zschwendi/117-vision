/* Cosmetic two-bone arm solve for the tracked first-person rig.
 *
 * Operates on Halo PC 1.10 first-person node records exactly as original
 * 004D6880 writes them: float scale, then forward/left/up basis columns and
 * a position (52 bytes). The shoulder (upper-arm node position) stays where
 * the original animation put it; the wrist target is the already tracked
 * wrist node. Each segment moves rigidly, so its authored length, its skin
 * and the original elbow pole are preserved. Nothing here reads or writes
 * the wrist, gun, marker or any firing state: an unreachable, degenerate or
 * non-finite chain returns without writing, and the caller keeps its
 * previous presentation.
 *
 * Independently derived (law of cosines plus least rotations). Design
 * reference only: VISR first_person_arms.c (Apache-2.0) and Halo-MCC-VR
 * ik.cpp (MIT); no code copied. Unlike VISR, no reach correction ever moves
 * the gun or wrist: the authoritative gun/reticle/shot pose is untouched. */
#ifndef HALO_SENSE_ARM_IK_H
#define HALO_SENSE_ARM_IK_H
#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct { float scale, c[3][3], p[3]; } SenseArmNode;
typedef char sense_arm_node_size[sizeof(SenseArmNode) == 52 ? 1 : -1];

enum {
    SENSE_ARM_SOLVED = 0,
    SENSE_ARM_UNREACHABLE = 1, /* wrist outside the arm's authored reach */
    SENSE_ARM_DEGENERATE = 2   /* zero/non-finite segment, pole or rotation */
};
/* Fraction of the wrist's roll about the forearm the forearm carries, so a
 * rolled hand does not fold the skin at the wrist. */
#define SENSE_ARM_TWIST_SHARE .5f

static inline float sense_arm_dot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline void sense_arm_cross(const float a[3], const float b[3], float o[3]) {
    float r[3] = {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; memcpy(o, r, 12);
}
static inline int sense_arm_finite3(const float v[3]) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static inline float sense_arm_unit(float v[3]) {
    float n = sense_arm_dot(v, v);
    if (!isfinite(n) || n < 1e-12f) return 0;
    n = sqrtf(n); for (unsigned k = 0; k < 3; ++k) v[k] /= n; return n;
}
/* v without its component along unit axis, normalized; 0 when nothing is left. */
static inline int sense_arm_perpendicular(const float v[3], const float axis[3], float out[3], float minimum) {
    float along = sense_arm_dot(v, axis);
    for (unsigned k = 0; k < 3; ++k) out[k] = v[k] - along*axis[k];
    return sense_arm_unit(out) > minimum;
}
/* Rodrigues rotation r[row][column] about a unit axis. */
static inline void sense_arm_axis_angle(const float axis[3], float angle, float r[3][3]) {
    float c = cosf(angle), s = sinf(angle), t = 1-c, x = axis[0], y = axis[1], z = axis[2];
    float m[3][3] = {{t*x*x+c, t*x*y-s*z, t*x*z+s*y},
                     {t*x*y+s*z, t*y*y+c, t*y*z-s*x},
                     {t*x*z-s*y, t*y*z+s*x, t*z*z+c}};
    memcpy(r, m, sizeof m);
}
/* Least rotation from unit a to unit b. Nearly opposite vectors have no
 * stable least rotation; report failure rather than flip a limb. */
static inline int sense_arm_between(const float a[3], const float b[3], float r[3][3]) {
    float axis[3]; sense_arm_cross(a, b, axis);
    float s = sqrtf(sense_arm_dot(axis, axis)), c = sense_arm_dot(a, b);
    if (!isfinite(s) || !isfinite(c) || (s < 1e-6f && c < 0)) return 0;
    if (s < 1e-6f) { float id[3][3] = {{1,0,0},{0,1,0},{0,0,1}}; memcpy(r, id, sizeof id); return 1; }
    for (unsigned k = 0; k < 3; ++k) axis[k] /= s;
    sense_arm_axis_angle(axis, atan2f(s, c), r); return 1;
}
static inline void sense_arm_apply(const float r[3][3], const float v[3], float o[3]) {
    float t[3]; for (unsigned i = 0; i < 3; ++i) t[i] = r[i][0]*v[0]+r[i][1]*v[1]+r[i][2]*v[2]; memcpy(o, t, 12);
}
/* Rotate a node's basis columns; its scale is kept, its position is set. */
static inline void sense_arm_place(const SenseArmNode *base, const float r[3][3], const float position[3], SenseArmNode *out) {
    SenseArmNode n = *base;
    for (unsigned c = 0; c < 3; ++c) sense_arm_apply(r, base->c[c], n.c[c]);
    memcpy(n.p, position, 12); *out = n;
}
static inline int sense_arm_node_valid(const SenseArmNode *n) {
    if (!isfinite(n->scale) || fabsf(n->scale) < 1e-5f || !sense_arm_finite3(n->p)) return 0;
    for (unsigned c = 0; c < 3; ++c) if (!sense_arm_finite3(n->c[c])) return 0;
    return 1;
}

/* Reach shaping, as fractions of the authored chain length a+c. Inside
 * KNEE the shoulder is exactly the original animation's. Beyond it the
 * reach used by the solve approaches FULL asymptotically and the remainder
 * becomes a cosmetic shoulder translation along the shoulder-wrist line, so
 * a hand crossing full extension never snaps the elbow or switches state.
 * A translation above ENTER (not yet connected) or EXIT (connected) keeps
 * the caller's existing presentation; the gap between them is hysteresis.
 * The same shaping applies symmetrically at the inner (folded) limit. */
#define SENSE_ARM_KNEE .95f
#define SENSE_ARM_FULL .999f
#define SENSE_ARM_SHIFT_ENTER .12f
#define SENSE_ARM_SHIFT_EXIT .20f
/* Bend-plane rotation is the least rotation from the authored to the new
 * shoulder-wrist line; it is smooth away from the opposite direction. */
#define SENSE_ARM_COS_ENTER -.80f
#define SENSE_ARM_COS_EXIT -.90f

static inline float sense_arm_soft(float d, float knee, float limit) {
    /* Identity up to knee; then knee + span*(1-exp(-(d-knee)/span)), which
     * is C1 at the knee and never reaches limit. */
    float span = limit-knee;
    if (!(span > 0) || d <= knee) return d;
    return knee+span*(1-expf(-(d-knee)/span));
}
/* Fade weight for the forearm twist: full share for moderate roll, zero at
 * the +/-pi wrap and when either roll reference is near the axis, so the
 * forearm never jumps when the measured roll wraps or becomes undefined.
 * Both fades are smoothsteps, bounding the forearm's turn rate. */
static inline float sense_arm_twist_weight(float delta, float reference_length) {
    float m = fabsf(delta), w = m <= 1.5f ? 1.f : m >= 2.5f ? 0.f : (2.5f-m);
    float t = (reference_length-.35f)/.35f; t = t < 0 ? 0 : t > 1 ? 1 : t;
    return w*w*(3-2*w)*t*t*(3-2*t);
}
static inline float sense_arm_signed_roll(const float axis[3], const float reference[3], const float up[3], float *length) {
    float r[3], u[3], side[3];
    float rl = 0, ul = 0, ar = sense_arm_dot(reference, axis), au = sense_arm_dot(up, axis);
    for (unsigned k = 0; k < 3; ++k) { r[k] = reference[k]-ar*axis[k]; u[k] = up[k]-au*axis[k]; }
    rl = sqrtf(sense_arm_dot(r, r)); ul = sqrtf(sense_arm_dot(u, u));
    *length = rl < ul ? rl : ul;
    if (!(*length > 1e-6f)) { *length = 0; return 0; }
    sense_arm_cross(axis, r, side);
    return atan2f(sense_arm_dot(u, side), sense_arm_dot(u, r));
}

/* upper/forearm/base_wrist: the original animated chain. wrist: the final
 * (tracked) wrist node, never written. fallback_pole: only for an authored
 * chain whose elbow lies on its own shoulder-wrist line. held: whether this
 * same hand/rig identity was connected on the previous build (hysteresis).
 * On SENSE_ARM_SOLVED writes out_upper/out_forearm (and *shoulder_shift,
 * the cosmetic shoulder translation in rig units) and nothing else. */
static inline int sense_arm_solve_held(const SenseArmNode *upper, const SenseArmNode *forearm,
                                       const SenseArmNode *base_wrist, const SenseArmNode *wrist,
                                       const float fallback_pole[3], int held,
                                       SenseArmNode *out_upper, SenseArmNode *out_forearm, float *shoulder_shift) {
    if (!upper || !forearm || !base_wrist || !wrist || !fallback_pole || !out_upper || !out_forearm ||
        !sense_arm_node_valid(upper) || !sense_arm_node_valid(forearm) || !sense_arm_node_valid(base_wrist) ||
        !sense_arm_node_valid(wrist) || !sense_arm_finite3(fallback_pole)) return SENSE_ARM_DEGENERATE;
    const float *shoulder = upper->p, *elbow0 = forearm->p, *wrist0 = base_wrist->p, *target = wrist->p;
    float bone[3], fore[3], l0[3], line[3];
    for (unsigned k = 0; k < 3; ++k) {
        bone[k] = elbow0[k]-shoulder[k]; fore[k] = wrist0[k]-elbow0[k];
        l0[k] = wrist0[k]-shoulder[k]; line[k] = target[k]-shoulder[k];
    }
    float a = sqrtf(sense_arm_dot(bone, bone)), c = sqrtf(sense_arm_dot(fore, fore));
    float d0 = sense_arm_unit(l0), d = sense_arm_unit(line);
    if (!isfinite(a) || !isfinite(c) || a < 1e-4f || c < 1e-4f || d0 < 1e-5f || d < 1e-5f) return SENSE_ARM_DEGENERATE;
    /* Authored bend plane: the line and the elbow's offset from it. These
     * depend only on the original animation, not on the tracked target. */
    float p0[3];
    if (!sense_arm_perpendicular(bone, l0, p0, 1e-3f*a) &&
        !sense_arm_perpendicular(fallback_pole, l0, p0, 1e-3f)) return SENSE_ARM_DEGENERATE;
    float cosine = sense_arm_dot(l0, line);
    if (!(cosine > (held ? SENSE_ARM_COS_EXIT : SENSE_ARM_COS_ENTER))) return SENSE_ARM_UNREACHABLE;
    float R[3][3];
    if (!sense_arm_between(l0, line, R)) return SENSE_ARM_DEGENERATE;
    float pole[3], normal[3];
    sense_arm_apply(R, p0, pole); sense_arm_cross(line, pole, normal);
    if (sense_arm_unit(pole) < .5f || sense_arm_unit(normal) < .5f) return SENSE_ARM_DEGENERATE;
    /* Reach shaping: solve at d', move the shoulder by d-d' along the line. */
    float total = a+c, lo = fabsf(a-c), solve_d;
    float outer_knee = SENSE_ARM_KNEE*total, outer_full = SENSE_ARM_FULL*total;
    float inner_full = lo+(1-SENSE_ARM_FULL)*total, inner_knee = lo+(1-SENSE_ARM_KNEE)*total;
    if (d > outer_knee) solve_d = sense_arm_soft(d, outer_knee, outer_full);
    else if (d < inner_knee) solve_d = 2*inner_knee-sense_arm_soft(2*inner_knee-d, inner_knee, 2*inner_knee-inner_full);
    else solve_d = d;
    float shift = d-solve_d;
    if (!isfinite(shift) || !isfinite(solve_d) || solve_d <= lo || solve_d >= total) return SENSE_ARM_DEGENERATE;
    if (fabsf(shift) > (held ? SENSE_ARM_SHIFT_EXIT : SENSE_ARM_SHIFT_ENTER)*total) return SENSE_ARM_UNREACHABLE;
    float root[3]; for (unsigned k = 0; k < 3; ++k) root[k] = shoulder[k]+line[k]*shift;
    /* Law of cosines in the rotated plane. Angles are measured from the
     * line toward the pole; authored angles come from the same plane. */
    float x = (a*a-c*c+solve_d*solve_d)/(2*solve_d), h2 = a*a-x*x, h = h2 > 0 ? sqrtf(h2) : 0;
    float alpha = atan2f(h, x), beta = atan2f(-h, solve_d-x);
    float alpha0 = atan2f(sense_arm_dot(bone, p0), sense_arm_dot(bone, l0));
    float beta0 = atan2f(sense_arm_dot(fore, p0), sense_arm_dot(fore, l0));
    float bend_upper[3][3], bend_fore[3][3], q_upper[3][3], q_fore[3][3];
    sense_arm_axis_angle(normal, alpha-alpha0, bend_upper);
    sense_arm_axis_angle(normal, beta-beta0, bend_fore);
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) {
        q_upper[i][j] = bend_upper[i][0]*R[0][j]+bend_upper[i][1]*R[1][j]+bend_upper[i][2]*R[2][j];
        q_fore[i][j] = bend_fore[i][0]*R[0][j]+bend_fore[i][1]*R[1][j]+bend_fore[i][2]*R[2][j];
    }
    float elbow[3], fore_dir[3];
    for (unsigned k = 0; k < 3; ++k) elbow[k] = root[k]+line[k]*x+pole[k]*h;
    for (unsigned k = 0; k < 3; ++k) fore_dir[k] = target[k]-elbow[k];
    if (sense_arm_unit(fore_dir) < 1e-5f) return SENSE_ARM_DEGENERATE;
    SenseArmNode solved_upper, solved_fore;
    sense_arm_place(upper, q_upper, root, &solved_upper);
    sense_arm_place(forearm, q_fore, elbow, &solved_fore);
    /* Share the change of wrist roll about the forearm, faded to zero where
     * the roll wraps or is undefined so the forearm stays continuous. */
    float rb, ra, fore0_dir[3]; memcpy(fore0_dir, fore, 12); sense_arm_unit(fore0_dir);
    float before = sense_arm_signed_roll(fore0_dir, forearm->c[2], base_wrist->c[2], &rb);
    float after = sense_arm_signed_roll(fore_dir, solved_fore.c[2], wrist->c[2], &ra);
    float delta = after-before;
    while (delta > 3.14159265f) delta -= 6.28318531f;
    while (delta < -3.14159265f) delta += 6.28318531f;
    float weight = sense_arm_twist_weight(delta, rb < ra ? rb : ra);
    if (weight > 0) {
        float twist[3][3]; sense_arm_axis_angle(fore_dir, delta*SENSE_ARM_TWIST_SHARE*weight, twist);
        for (unsigned col = 0; col < 3; ++col) sense_arm_apply(twist, solved_fore.c[col], solved_fore.c[col]);
    }
    /* Final verification: authored lengths, forearm ends at the tracked
     * wrist, finite records. */
    float check_a[3], check_c[3];
    for (unsigned k = 0; k < 3; ++k) { check_a[k] = solved_fore.p[k]-solved_upper.p[k]; check_c[k] = target[k]-solved_fore.p[k]; }
    float la = sqrtf(sense_arm_dot(check_a, check_a)), lc = sqrtf(sense_arm_dot(check_c, check_c));
    float mapped[3]; sense_arm_apply(q_fore, fore, mapped);
    for (unsigned k = 0; k < 3; ++k) mapped[k] += elbow[k]-target[k];
    if (!sense_arm_node_valid(&solved_upper) || !sense_arm_node_valid(&solved_fore) ||
        !(fabsf(la-a) <= 1e-3f*a+1e-6f) || !(fabsf(lc-c) <= 1e-3f*c+1e-6f) ||
        !(sqrtf(sense_arm_dot(mapped, mapped)) <= 1e-3f*c+1e-6f)) return SENSE_ARM_DEGENERATE;
    *out_upper = solved_upper; *out_forearm = solved_fore;
    if (shoulder_shift) *shoulder_shift = shift;
    return SENSE_ARM_SOLVED;
}
/* Stateless form (no hysteresis): enter thresholds. */
static inline int sense_arm_solve(const SenseArmNode *upper, const SenseArmNode *forearm,
                                  const SenseArmNode *base_wrist, const SenseArmNode *wrist,
                                  const float fallback_pole[3],
                                  SenseArmNode *out_upper, SenseArmNode *out_forearm) {
    return sense_arm_solve_held(upper, forearm, base_wrist, wrist, fallback_pole, 0, out_upper, out_forearm, NULL);
}
#endif
