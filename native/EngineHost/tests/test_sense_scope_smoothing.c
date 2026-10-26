/* Scoped-aim smoothing math (sense_scope_smoothing.h): exact when off or on
 * the first scoped epoch, bounded lag, convergence at 30/60/90 Hz, hand
 * position untouched, camera-relative (turning is never smoothed), and
 * resets on identity, zoom, gap and camera jumps. Pure; no engine/device. */
#include "../sense_scope_smoothing.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static HaloSenseTransform yawed(float yaw, float pitch) {
    float f[3] = {cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), sinf(pitch)}, u[3] = {-sinf(pitch) * cosf(yaw), -sinf(pitch) * sinf(yaw), cosf(pitch)};
    HaloSenseTransform t; assert(sense_basis(f, u, &t)); t.p[0] = 10.2f; t.p[1] = 19.9f; t.p[2] = 30.4f; return t;
}
static float between(const HaloSenseTransform *a, const HaloSenseTransform *b) {
    float axis[3], angle; return sense_grip_correction(a, b, axis, &angle) ? angle : NAN;
}
static HaloSenseScopeInputPose in(uint64_t ns, int enabled, int zoom, const HaloSenseTransform *camera, const HaloSenseTransform *pose) {
    return (HaloSenseScopeInputPose){.session = 1, .generation = 2, .epoch_ns = ns, .unit = 3, .weapon = 4,
                                     .enabled = enabled, .zoom = zoom, .camera = camera, .accepted = pose};
}

int main(void) {
    HaloSenseScopeFilter f; sense_scope_filter_reset(&f);
    HaloSenseTransform camera = yawed(0, 0), pose = yawed(.1f, .05f), out;
    /* Off, or not scoped: the input bit for bit. */
    HaloSenseScopeInputPose p = in(1000000000, 0, 0, &camera, &pose);
    CHECK(sense_scope_filter(&f, &p, &out) && !memcmp(&out, &pose, sizeof out) && !f.valid);
    p = in(1000000000, 1, -1, &camera, &pose);
    CHECK(sense_scope_filter(&f, &p, &out) && !memcmp(&out, &pose, sizeof out) && !f.valid);
    /* First scoped epoch is exact; same-epoch reuse is a zero step. */
    p = in(1000000000, 1, 0, &camera, &pose);
    CHECK(sense_scope_filter(&f, &p, &out) && !memcmp(&out, &pose, sizeof out) && f.valid);
    /* Convergence toward a held pose at 30/60/90 Hz, never overshooting,
     * and the residual after 300 ms is under exp(-5) of the start. */
    const unsigned rates[3] = {30, 60, 90};
    for (unsigned r = 0; r < 3; r++) {
        sense_scope_filter_reset(&f);
        uint64_t ns = 1000000000, step = UINT64_C(1000000000) / rates[r];
        p = in(ns, 1, 0, &camera, &pose); sense_scope_filter(&f, &p, &out);
        HaloSenseTransform target = yawed(.1f + .03f, .05f);
        float start = between(&pose, &target), previous = start;
        for (uint64_t t = step; t <= UINT64_C(300000000); t += step) {
            p = in(ns + t, 1, 0, &camera, &target);
            CHECK(sense_scope_filter(&f, &p, &out));
            float remaining = between(&out, &target);
            CHECK(remaining <= previous + 1e-6f && remaining <= HALO_SENSE_SCOPE_MAX_LAG_RADIANS + 1e-6f);
            CHECK(!memcmp(out.p, target.p, 12)); /* position is never filtered */
            float expected = start * expf(-(float)t * 1e-9f / HALO_SENSE_SCOPE_TAU_SECONDS);
            CHECK(fabsf(remaining - expected) < 2e-4f);
            previous = remaining;
        }
        CHECK(previous < start * expf(-5.f) + 1e-5f);
    }
    /* A large swing is bounded to the 4 degree lag, even near-antipodal. */
    sense_scope_filter_reset(&f); p = in(1000000000, 1, 0, &camera, &pose); sense_scope_filter(&f, &p, &out);
    const float swings[3] = {.5f, 2.f, 3.1f};
    uint64_t ns = 1000000000;
    for (unsigned s = 0; s < 3; s++) {
        HaloSenseTransform target = yawed(.1f + swings[s], .05f); ns += 11000000;
        p = in(ns, 1, 0, &camera, &target); CHECK(sense_scope_filter(&f, &p, &out));
        float lag = between(&out, &target);
        CHECK(isfinite(lag) && lag <= HALO_SENSE_SCOPE_MAX_LAG_RADIANS + 1e-5f && lag > 1e-3f);
        for (unsigned i = 0; i < 3; i++) CHECK(fabsf(sense_dot(out.c[i], out.c[i]) - 1) < 1e-5f);
    }
    /* Camera-relative: the same tracked motion seen under a turned camera
     * gives the turned result, and a smooth turn alone is not smoothed. */
    HaloSenseScopeFilter a, b; sense_scope_filter_reset(&a); sense_scope_filter_reset(&b);
    HaloSenseTransform turned = yawed(.2f, 0), rel0 = yawed(.1f, .05f), rel1 = yawed(.13f, .04f), w0, w1, oa, ob;
    sense_scope_world(&turned, &rel0, &w0); sense_scope_world(&turned, &rel1, &w1);
    p = in(1000000000, 1, 0, &camera, &rel0); sense_scope_filter(&a, &p, &oa);
    p = in(1000000000, 1, 0, &turned, &w0); sense_scope_filter(&b, &p, &ob);
    p = in(1011000000, 1, 0, &camera, &rel1); sense_scope_filter(&a, &p, &oa);
    p = in(1011000000, 1, 0, &turned, &w1); sense_scope_filter(&b, &p, &ob);
    HaloSenseTransform expected; sense_scope_world(&turned, &oa, &expected); memcpy(expected.p, oa.p, 12);
    CHECK(between(&expected, &ob) < 2e-5f);
    sense_scope_filter_reset(&a);
    HaloSenseTransform cam_t = camera, world_t;
    for (unsigned n = 0; n < 10; n++) {
        cam_t = yawed(.01f * (float)n, 0); sense_scope_world(&cam_t, &rel0, &world_t);
        p = in(1000000000 + n * 11000000ull, 1, 0, &cam_t, &world_t); CHECK(sense_scope_filter(&a, &p, &oa));
        CHECK(between(&oa, &world_t) < 2e-5f);
    }
    /* Resets to the exact input: identity, zoom level, gap, camera jump,
     * time reversal; non-finite input reports failure and forgets state. */
    for (int kind = 0; kind < 8; kind++) {
        sense_scope_filter_reset(&f); p = in(1000000000, 1, 0, &camera, &pose); sense_scope_filter(&f, &p, &out);
        HaloSenseTransform target = yawed(.15f, .05f), cam = camera;
        p = in(1011000000, 1, 0, &cam, &target);
        if (kind == 0) p.session++;
        if (kind == 1) p.generation++;
        if (kind == 2) p.unit++;
        if (kind == 3) p.weapon++;
        if (kind == 4) p.zoom = 1;
        if (kind == 5) p.epoch_ns += HALO_SENSE_SCOPE_GAP_NS;
        if (kind == 6) { cam = yawed(.3f, 0); sense_scope_world(&cam, &target, &target); p.accepted = &target; }
        if (kind == 7) p.epoch_ns = 999000000;
        CHECK(sense_scope_filter(&f, &p, &out) && !memcmp(&out, p.accepted, sizeof out));
    }
    HaloSenseTransform bad = pose; bad.c[1][2] = NAN; p = in(1022000000, 1, 0, &camera, &bad);
    CHECK(!sense_scope_filter(&f, &p, &out) && !f.valid);
    printf("PASS %u scoped smoothing checks: exact off/first epoch, 30/60/90 Hz convergence, 4 degree bound incl. near-antipodal, camera-relative, resets\n", checks);
    return 0;
}
