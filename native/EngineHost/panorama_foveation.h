/* panorama_foveation.h - rasterization rates for the engine's world passes,
 * derived from how the presenter composites them.
 *
 * The presenter (EngineImmersiveScreenGeometry.swift) lays the eight world
 * pictures on one sphere in a fixed order, six ring bearings then the sky and
 * the floor, each with "over" blending and an edge fade that only spends
 * itself over what is already beneath. A picture drawn later covers the ones
 * before it, so parts of a layer can never reach the eye: chiefly the top and
 * bottom rows of the ring bearings, which the caps cover. This file is a C
 * port of that composite (panel order, camera bases, image points, fades) so
 * the host can give each layer's pass a rate map that lowers the shading
 * rate only where the composite says so:
 *
 *   BALANCED    rows/columns whose composite weight never exceeds 1/255
 *               (invisible in an 8-bit result) at a quarter rate, with a
 *               safety margin of zones at full rate around anything visible;
 *   AGGRESSIVE  also half rate where the layer contributes less than half
 *               (cross-fade zones), and half rate everywhere on the three
 *               bearings behind the viewer and on the sky and floor.
 *
 * Metal rate maps are separable (a rate per column and per row), so a row is
 * lowered only when every column of it qualifies, and vice versa. Metal also
 * interpolates rates across zone changes; at the panorama sizes (32 zones
 * over 1152..1536 rows) the measured transition stays within the lowered
 * zones, so the one-zone margin keeps every visible zone at full rate
 * (tests/test_panorama_foveation_maps.m checks this against Metal's own
 * mapping). Pure C, no Metal: the renderer turns the rates into an
 * MTLRasterizationRateMap. */
#ifndef PANORAMA_FOVEATION_H
#define PANORAMA_FOVEATION_H
#include <math.h>
#include <string.h>
#include "panorama.h"

#define HALO_FOVEATION_ZONES 32
#define HALO_FOVEATION_SAMPLES 3            /* per zone and axis */
#define HALO_FOVEATION_MARGIN 1             /* full-rate zones kept around visible ones */
#define HALO_FOVEATION_HIDDEN (1.0f / 255.0f)
#define HALO_FOVEATION_HIDDEN_RATE 0.25f
#define HALO_FOVEATION_REDUCED_RATE 0.5f

typedef struct {
    /* Per layer, as published (panorama.h / HaloPanoramaInfo). */
    float projection_x[HALO_PANORAMA_LAYERS], projection_y[HALO_PANORAMA_LAYERS];
    float viewport[HALO_PANORAMA_LAYERS][4];   /* u min, v min, u max, v max */
} HaloFoveationInput;

typedef struct {
    float columns[HALO_FOVEATION_ZONES], rows[HALO_FOVEATION_ZONES];
    float shading;                              /* estimated shaded fraction (Metal's physical size is the measure) */
    /* The largest composite weight found in each column / row zone, before
     * the margin: what the rates were derived from (tests check them). */
    float column_weight[HALO_FOVEATION_ZONES], row_weight[HALO_FOVEATION_ZONES];
} HaloFoveationRates;

/* The presenter's panel order: ring layers by bearing, then sky, floor. */
static const int halo_foveation_sources[8] = { 0, 1, 2, 7, 8, 9, 5, 6 };
static const float halo_foveation_yaw_degrees[8] = { -60, 0, 60, 120, 180, -120, 0, 0 };
static const float halo_foveation_pitch_degrees[8] = { 0, 0, 0, 0, 0, 0, 90, -90 };
static const float halo_foveation_feather = 0.15f;   /* panelFeatherAcross / UpDown */

typedef struct { float right[3], up[3], forward[3], px, py; } HaloFoveationCamera;

static inline void halo_foveation_camera(int source, float px, float py, HaloFoveationCamera *c) {
    const float d2r = 3.14159265358979f / 180.f;
    float yaw = halo_foveation_yaw_degrees[source] * d2r, pitch = halo_foveation_pitch_degrees[source] * d2r;
    float forward[3] = { sinf(yaw), 0.f, -cosf(yaw) }, up[3] = { 0.f, 1.f, 0.f };
    if (pitch != 0.f) {
        float cp = cosf(pitch), sp = sinf(pitch), f[3], u[3];
        for (int k = 0; k < 3; ++k) { f[k] = forward[k] * cp + up[k] * sp; u[k] = up[k] * cp - forward[k] * sp; }
        memcpy(forward, f, sizeof f); memcpy(up, u, sizeof u);
    }
    float r[3] = { forward[1] * up[2] - forward[2] * up[1], forward[2] * up[0] - forward[0] * up[2], forward[0] * up[1] - forward[1] * up[0] };
    float n = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; ++k) { c->right[k] = r[k] / n; c->up[k] = up[k]; c->forward[k] = forward[k]; }
    c->px = px > 0.0001f ? px : 1.f; c->py = py > 0.0001f ? py : 1.f;
}
static inline int halo_foveation_image_point(const HaloFoveationCamera *c, const float d[3], float *x, float *y) {
    float f = c->forward[0] * d[0] + c->forward[1] * d[1] + c->forward[2] * d[2];
    if (!(f > 1e-5f)) return 0;
    *x = c->px * (c->right[0] * d[0] + c->right[1] * d[1] + c->right[2] * d[2]) / f;
    *y = -c->py * (c->up[0] * d[0] + c->up[1] * d[1] + c->up[2] * d[2]) / f;
    return 1;
}
static inline float halo_foveation_ramp(float distance) {
    float x = distance / halo_foveation_feather; x = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
    return x * x * (3.f - 2.f * x);
}
/* The final weight of `position` (panel order) in the composite at d. */
static inline float halo_foveation_weight(const HaloFoveationCamera cams[8], int position, const float d[3]) {
    float laid = 0.f, alpha[8];
    for (int i = 0; i < 8; ++i) {
        float x, y, a = 0.f;
        if (halo_foveation_image_point(&cams[i], d, &x, &y) && fabsf(x) <= 1.f && fabsf(y) <= 1.f) {
            float edge = halo_foveation_ramp(x + 1.f) * halo_foveation_ramp(1.f - x) * halo_foveation_ramp(y + 1.f) * halo_foveation_ramp(1.f - y);
            a = 1.f - (1.f - edge) * laid;
        }
        alpha[i] = a; laid += a * (1.f - laid);
    }
    float w = alpha[position];
    for (int i = position + 1; i < 8; ++i) w *= 1.f - alpha[i];
    return w;
}
static inline int halo_foveation_position(int layer) {
    if (layer == HALO_PANORAMA_CENTRE_RIGHT) return 1;   /* the right eye's centre bearing */
    for (int i = 0; i < 8; ++i) if (halo_foveation_sources[i] == layer) return i;
    return -1;
}
static inline int halo_foveation_valid_input(const HaloFoveationInput *in, int layer) {
    for (int i = 0; i < 8; ++i) {
        int l = i == 1 && layer == HALO_PANORAMA_CENTRE_RIGHT ? layer : halo_foveation_sources[i];
        const float *v = in->viewport[l];
        if (!(in->projection_x[l] > 0.f) || !(in->projection_y[l] > 0.f) || !isfinite(in->projection_x[l]) || !isfinite(in->projection_y[l]) ||
            !(v[2] > v[0]) || !(v[3] > v[1]) || v[0] < 0.f || v[1] < 0.f || v[2] > 1.f || v[3] > 1.f) return 0;
    }
    return 1;
}
/* Rates for one world layer; 0 when the inputs cannot describe a sphere or
 * the mode asks for nothing (the caller then draws without a map). */
static inline int halo_foveation_rates(const HaloFoveationInput *in, int layer, int mode, HaloFoveationRates *out) {
    enum { N = HALO_FOVEATION_ZONES, S = HALO_FOVEATION_SAMPLES };
    int position = halo_foveation_position(layer);
    if (!out || !in || position < 0 || mode <= 0 || mode > 2 || !halo_foveation_valid_input(in, layer)) return 0;
    HaloFoveationCamera cams[8];
    for (int i = 0; i < 8; ++i) {
        int l = i == 1 && layer == HALO_PANORAMA_CENTRE_RIGHT ? layer : halo_foveation_sources[i];
        halo_foveation_camera(i, in->projection_x[l], in->projection_y[l], &cams[i]);
    }
    const float *vp = in->viewport[layer];
    float weight[N][N];
    for (int r = 0; r < N; ++r) for (int c = 0; c < N; ++c) {
        /* Only the thresholds matter (1/255 and 1/2), so stop once past both. */
        float best = 0.f;
        for (int sy = 0; sy < S && best < 0.5f; ++sy) for (int sx = 0; sx < S && best < 0.5f; ++sx) {
            /* Texture coordinates of the sample, then the image point (s, t)
             * the presenter's mesh gives them (sphericalPanorama). */
            float u = (c + (sx + 0.5f) / S) / N, v = (r + (sy + 0.5f) / S) / N;
            if (u < vp[0] || u > vp[2] || v < vp[1] || v > vp[3]) continue;   /* never sampled */
            float s = ((u - vp[0]) / (vp[2] - vp[0])) * 2.f - 1.f, t = ((v - vp[1]) / (vp[3] - vp[1])) * 2.f - 1.f;
            const HaloFoveationCamera *k = &cams[position];
            float d[3];
            for (int a = 0; a < 3; ++a) d[a] = k->right[a] * (s / k->px) - k->up[a] * (t / k->py) + k->forward[a];
            float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (int a = 0; a < 3; ++a) d[a] /= n;
            float w = halo_foveation_weight(cams, position, d);
            if (w > best) best = w;
        }
        weight[r][c] = best;
    }
    float *row_weight = out->row_weight, *column_weight = out->column_weight;
    for (int i = 0; i < N; ++i) { row_weight[i] = column_weight[i] = 0.f; }
    for (int r = 0; r < N; ++r) for (int c = 0; c < N; ++c) {
        if (weight[r][c] > row_weight[r]) row_weight[r] = weight[r][c];
        if (weight[r][c] > column_weight[c]) column_weight[c] = weight[r][c];
    }
    int rear = layer == HALO_PANORAMA_REAR_RIGHT || layer == HALO_PANORAMA_REAR || layer == HALO_PANORAMA_REAR_LEFT ||
               layer == HALO_PANORAMA_UP || layer == HALO_PANORAMA_DOWN;
    for (int axis = 0; axis < 2; ++axis) {
        const float *w = axis ? row_weight : column_weight;
        float *rate = axis ? out->rows : out->columns;
        for (int i = 0; i < N; ++i) {
            float best = 0.f;
            for (int j = i - HALO_FOVEATION_MARGIN; j <= i + HALO_FOVEATION_MARGIN; ++j)
                if (j >= 0 && j < N && w[j] > best) best = w[j];
            float r = best < HALO_FOVEATION_HIDDEN ? HALO_FOVEATION_HIDDEN_RATE : 1.f;
            if (mode == 2 && r == 1.f && (rear || best < 0.5f)) r = HALO_FOVEATION_REDUCED_RATE;
            rate[i] = r;
        }
    }
    float cs = 0.f, rs = 0.f;
    for (int i = 0; i < N; ++i) { cs += out->columns[i]; rs += out->rows[i]; }
    out->shading = (cs / N) * (rs / N);
    for (int i = 0; i < N; ++i) if (out->columns[i] < 1.f || out->rows[i] < 1.f) return 1;
    return 0;   /* nothing to lower: draw without a map */
}
#endif
