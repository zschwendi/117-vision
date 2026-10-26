/* Test-only bridge for FoveationGeometryValidation.swift: the C composite
 * weight (panorama_foveation.h) for one direction, with the layer cameras
 * built from a HaloFoveationInput exactly as halo_foveation_rates does. */
#include "panorama_foveation.h"
static inline float halo_foveation_test_weight(const HaloFoveationInput *in, int position, float x, float y, float z) {
    HaloFoveationCamera cams[8];
    for (int i = 0; i < 8; ++i) {
        int l = halo_foveation_sources[i];
        halo_foveation_camera(i, in->projection_x[l], in->projection_y[l], &cams[i]);
    }
    float d[3] = { x, y, z };
    return halo_foveation_weight(cams, position, d);
}
static inline HaloFoveationInput halo_foveation_test_dense(void) {
    HaloFoveationInput in; memset(&in, 0, sizeof in);
    for (int l = 0; l < HALO_PANORAMA_LAYERS; ++l) {
        in.projection_x[l] = 1.f / tanf(32.f * 3.14159265f / 180.f); in.projection_y[l] = 1.f / tanf(52.5f * 3.14159265f / 180.f);
        in.viewport[l][2] = in.viewport[l][3] = 1.f;
    }
    in.projection_x[HALO_PANORAMA_UP] = in.projection_x[HALO_PANORAMA_DOWN] = 1.f / tanf(48.f * 3.14159265f / 180.f);
    return in;
}
