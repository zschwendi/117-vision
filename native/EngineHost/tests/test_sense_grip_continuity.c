/* Grab/release continuity math (sense_grip_continuity.h): shortest arc,
 * antipodal stability, exact latch, smooth release, identity reset and
 * same-frame idempotence. Pure; no engine, device or clock. */
#include "../sense_grip_continuity.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define MS UINT64_C(1000000)

static HaloSenseTransform pose(float yaw, float pitch, float roll, float x) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    float f[3] = {cp * cy, cp * sy, sp}, u0[3] = {-sp * cy, -sp * sy, cp}, l0[3];
    sense_cross(u0, f, l0);
    float u[3]; for (unsigned k = 0; k < 3; k++) u[k] = u0[k] * cosf(roll) + l0[k] * sinf(roll);
    HaloSenseTransform t; assert(sense_basis(f, u, &t));
    t.p[0] = x; t.p[1] = .25f; t.p[2] = -.5f; return t;
}
/* Rotation angle between two frames, robust near zero. */
static float angle_between(const HaloSenseTransform *a, const HaloSenseTransform *b) {
    float axis[3], angle; if (!sense_grip_correction(a, b, axis, &angle)) return NAN; return angle;
}
static int orthonormal(const HaloSenseTransform *t) {
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++) {
        if (!isfinite(t->c[i][j])) return 0;
        float d = sense_dot(t->c[i], t->c[j]);
        if (fabsf(d - (i == j)) > 2e-5f) return 0;
    }
    float cross[3]; sense_cross(t->c[0], t->c[1], cross);
    return sense_dot(cross, t->c[2]) > .9999f; /* right-handed, no reflection */
}
static HaloSenseGripInput frame(uint64_t epoch, uint64_t ms, int supported,
                                const HaloSenseTransform *live, const HaloSenseTransform *other) {
    return (HaloSenseGripInput){.session = 7, .generation = 3, .epoch = epoch, .epoch_ns = UINT64_C(5000000000) + ms * MS,
                                .unit = 0x23450000u, .weapon = 0x34560001u, .supported = supported, .live = live, .other = other};
}

int main(void) {
    HaloSenseTransform one = pose(0, 0, 0, .1f), two = pose(.35f, -.2f, .05f, .1f), out, again;
    float axis[3], angle;

    /* Shortest arc: a 190 degree yaw is a 170 degree correction. */
    HaloSenseTransform far = pose(3.3161256f, 0, 0, 0);
    CHECK(sense_grip_correction(&one, &far, axis, &angle) && fabsf(angle - 2.9670597f) < 1e-4f);
    CHECK(sense_grip_apply(&one, axis, angle, &out) && angle_between(&out, &far) < 1e-4f && orthonormal(&out));
    /* Antipodal: exactly 180 degrees about up, about forward and about a
     * skew axis all yield a finite unit axis and reach the anchor exactly. */
    HaloSenseTransform flips[3] = {pose(3.14159265f, 0, 0, 0), pose(0, 0, 3.14159265f, 0), one};
    float skew[3] = {.48f, -.6f, .64f}; assert(sense_normalize(skew));
    CHECK(sense_grip_apply(&one, skew, 3.14159265f, &flips[2]));
    for (unsigned n = 0; n < 3; n++) {
        CHECK(sense_grip_correction(&one, &flips[n], axis, &angle));
        CHECK(isfinite(axis[0]) && isfinite(axis[1]) && isfinite(axis[2]) && fabsf(sense_dot(axis, axis) - 1) < 1e-5f);
        CHECK(fabsf(angle - 3.14159265f) < 2e-3f);
        for (int step = 0; step <= 20; step++) {
            CHECK(sense_grip_apply(&one, axis, angle * (float)step / 20.f, &out) && orthonormal(&out));
            if (step == 20) CHECK(angle_between(&out, &flips[n]) < 2e-3f);
        }
    }
    /* Identity correction is exact and finite. */
    CHECK(sense_grip_correction(&one, &one, axis, &angle) && angle < 1e-6f);

    /* Exact latch: the grab frame presents the one-hand pose the player was
     * seeing; the transition then follows smoothstep and ends exactly live. */
    HaloSenseGripContinuity s; sense_grip_reset(&s);
    HaloSenseGripInput in = frame(1, 0, 0, &one, NULL);
    CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &one, sizeof out) && !s.active);
    in = frame(2, 11, 1, &two, &one);
    CHECK(sense_grip_present(&s, &in, &out) && s.active && angle_between(&out, &one) < 2e-5f);
    CHECK(!memcmp(out.p, two.p, 12) && orthonormal(&out));
    float total = angle_between(&one, &two);
    /* Same-frame idempotence: every consumer in this epoch gets these bits. */
    for (int n = 0; n < 4; n++) { CHECK(sense_grip_present(&s, &in, &again) && !memcmp(&again, &out, sizeof out)); }
    float previous = total;
    for (uint64_t ms = 11 + 11, epoch = 3; ms < 211; ms += 11, epoch++) {
        in = frame(epoch, ms, 1, &two, &one);
        CHECK(sense_grip_present(&s, &in, &out) && orthonormal(&out) && !memcmp(out.p, two.p, 12));
        float remaining = angle_between(&out, &two), t = (float)(ms - 11) / 200.f, u = t * t * (3 - 2 * t);
        CHECK(remaining <= previous + 1e-5f);
        CHECK(fabsf(remaining - total * (1 - u)) < 2e-4f);
        previous = remaining;
    }
    in = frame(30, 211, 1, &two, &one);
    CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &two, sizeof out) && !s.active);
    in = frame(31, 222, 1, &two, &one);
    CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &two, sizeof out));

    /* Smooth release: the release frame still shows the two-hand line, then
     * eases to the one-hand pose with bounded per-frame change. */
    HaloSenseTransform one_moved = pose(.05f, .02f, 0, .1f), two_cf = pose(.36f, -.19f, .05f, .1f);
    in = frame(32, 233, 0, &one_moved, &two_cf);
    CHECK(sense_grip_present(&s, &in, &out) && s.active && angle_between(&out, &two_cf) < 2e-5f);
    total = angle_between(&one_moved, &two_cf); previous = total;
    float largest_step = 0; HaloSenseTransform last = out;
    for (uint64_t ms = 244, epoch = 33; ms < 433; ms += 11, epoch++) {
        in = frame(epoch, ms, 0, &one_moved, &two_cf);
        CHECK(sense_grip_present(&s, &in, &out) && orthonormal(&out));
        float remaining = angle_between(&out, &one_moved);
        CHECK(remaining <= previous + 1e-5f); previous = remaining;
        float step = angle_between(&out, &last); if (step > largest_step) largest_step = step; last = out;
    }
    CHECK(largest_step < total * .1f); /* smoothstep peak is 1.5/frame-fraction: 1.5*11/200 */
    in = frame(60, 433, 0, &one_moved, &two_cf);
    CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &one_moved, sizeof out));

    /* Release without a same-frame counterfactual anchors to the last
     * accepted pose; with neither, it fails open to live (never fabricated). */
    in = frame(61, 444, 1, &two, &one_moved); sense_grip_present(&s, &in, &out);
    in = frame(62, 655, 1, &two, &one_moved); CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &two, sizeof out));
    HaloSenseTransform accepted = out;
    in = frame(63, 666, 0, &one, NULL);
    CHECK(sense_grip_present(&s, &in, &out) && angle_between(&out, &accepted) < 2e-5f);

    /* Re-entrant edges: grab during a release and release during a grab
     * continue from what is on screen in that same epoch. */
    in = frame(64, 677, 0, &one, NULL); HaloSenseTransform before; CHECK(sense_grip_present(&s, &in, &before));
    in = frame(64, 677, 1, &two, &one); CHECK(sense_grip_present(&s, &in, &out) && angle_between(&out, &before) < 3e-5f);
    in = frame(65, 688, 1, &two, &one); CHECK(sense_grip_present(&s, &in, &before));
    in = frame(65, 688, 0, &one, &two); CHECK(sense_grip_present(&s, &in, &out) && angle_between(&out, &before) < 3e-5f);
    /* Repeated identical edge-free calls stay idempotent mid-transition. */
    CHECK(sense_grip_present(&s, &in, &again) && !memcmp(&again, &out, sizeof out));

    /* Identity resets snap to live with no animation: weapon switch, unit,
     * session, input generation (recenter/reconnect/tracking loss), a long
     * gap, a missing epoch, and an epoch that runs backwards. */
    for (int kind = 0; kind < 7; kind++) {
        sense_grip_reset(&s);
        in = frame(100, 1000, 0, &one, NULL); sense_grip_present(&s, &in, &out);
        in = frame(101, 1011, 1, &two, &one); sense_grip_present(&s, &in, &out); CHECK(s.active);
        in = frame(102, 1022, 1, &two, &one);
        if (kind == 0) in.weapon++;
        if (kind == 1) in.unit++;
        if (kind == 2) in.session++;
        if (kind == 3) in.generation++;
        if (kind == 4) in.epoch_ns += HALO_SENSE_GRIP_GAP_NS;
        if (kind == 5) in.epoch = in.epoch_ns = 0;
        if (kind == 6) in.epoch_ns -= 20 * MS;
        CHECK(sense_grip_present(&s, &in, &out) && !memcmp(&out, &two, sizeof out) && !s.active);
        /* The rebaseline above did not animate; the next support change under
         * the new identity is an ordinary, anchored edge. */
        in.supported = 0; in.live = &one; in.other = &two; if (kind == 5) in.epoch = 103, in.epoch_ns = 1;
        CHECK(sense_grip_present(&s, &in, &out));
        if (kind == 5) CHECK(!memcmp(&out, &one, sizeof out));
        else CHECK(s.active && angle_between(&out, &two) < 2e-5f);
    }
    /* Non-finite live input never produces output or keeps stale state. */
    HaloSenseTransform bad = one; bad.c[0][1] = NAN;
    in = frame(200, 2000, 1, &bad, &one);
    CHECK(!sense_grip_present(&s, &in, &out) && !s.initialized);
    /* A non-finite counterfactual falls back to the last accepted pose. */
    in = frame(201, 2011, 0, &one, NULL); sense_grip_present(&s, &in, &out);
    in = frame(202, 2022, 1, &two, &bad);
    CHECK(sense_grip_present(&s, &in, &out) && angle_between(&out, &one) < 2e-5f && orthonormal(&out));

    printf("PASS %u sense grip continuity checks: shortest arc, antipodal, exact latch, smooth release, re-entrant, identity reset, idempotent\n", checks);
    return 0;
}
