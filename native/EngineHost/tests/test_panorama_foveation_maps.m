/* The panorama rate maps (panorama_foveation.h) as Metal builds them.
 *
 * For the dense production layout (ring bearings +/-32 degrees wide, caps
 * +/-48, 105 degree vertical field) at every engine size the settings offer,
 * each world layer's BALANCED map is built with mr_rate_map_create and
 * checked against Metal's own screen-to-physical mapping:
 *   - every row and column of a zone the composite can show (weight >=
 *     1/255) is at full rate (slope 1), whatever Metal's interpolation does;
 *   - the layers the analysis can lower (the centre bearings and the rear)
 *     are lowered, and the measured shaded fraction is reported;
 *   - AGGRESSIVE lowers the rear bearings and caps further.
 * Prints SKIP without Metal or rate-map support.
 *
 * clang -O2 -fobjc-arc -fblocks tests/test_panorama_foveation_maps.m
 *   -framework Foundation -framework Metal -o /tmp/panorama-foveation-maps */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main
#include "../panorama_foveation.h"

static HaloFoveationInput dense_layout(void) {
    HaloFoveationInput in; memset(&in, 0, sizeof in);
    for (int l = 0; l < HALO_PANORAMA_LAYERS; ++l) {
        in.projection_x[l] = 1.f / tanf(32.f * (float)M_PI / 180.f);
        in.projection_y[l] = 1.f / tanf(52.5f * (float)M_PI / 180.f);
        in.viewport[l][2] = in.viewport[l][3] = 1.f;
    }
    in.projection_x[HALO_PANORAMA_UP] = in.projection_x[HALO_PANORAMA_DOWN] = 1.f / tanf(48.f * (float)M_PI / 180.f);
    return in;
}
/* Slope of the map at screen coordinate i along one axis. */
static double slope(id<MTLRasterizationRateMap> map, int axis, double i, int w, int h) {
    MTLCoordinate2D a = axis ? MTLCoordinate2DMake(w / 2, i) : MTLCoordinate2DMake(i, h / 2);
    MTLCoordinate2D b = axis ? MTLCoordinate2DMake(w / 2, i + 1) : MTLCoordinate2DMake(i + 1, h / 2);
    MTLCoordinate2D pa = [map mapScreenToPhysicalCoordinates:a forLayer:0], pb = [map mapScreenToPhysicalCoordinates:b forLayer:0];
    return axis ? pb.y - pa.y : pb.x - pa.x;
}

int main(void) { @autoreleasepool {
    mr_context *probe = mr_create(8, 8);
    if (!probe) { puts("SKIP no Metal"); return 77; }
    mr_destroy(probe);
    if (!mr_rate_maps_supported()) { puts("SKIP no rasterization rate maps"); return 77; }
    HaloFoveationInput in = dense_layout();
    const int sizes[][2] = { { 640, 480 }, { 960, 720 }, { 1280, 960 }, { 1376, 1032 }, { 1536, 1152 }, { 1600, 1200 },
                             { 1920, 1440 }, { 2048, 1536 }, { 2560, 1920 } };
    const int layers[] = { 0, 1, 2, 4, 5, 6, 7, 8, 9 };
    unsigned checked_rows = 0, lowered_layers = 0;
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; ++s) {
        int w = sizes[s][0], h = sizes[s][1];
        double fractions[10] = {0};
        for (unsigned k = 0; k < sizeof layers / sizeof layers[0]; ++k) {
            int layer = layers[k];
            HaloFoveationRates r;
            if (!halo_foveation_rates(&in, layer, HALO_FOVEATION_BALANCED, &r)) { fractions[layer] = 1.0; continue; }
            mr_rate_map *map = mr_rate_map_create(w, h, r.columns, HALO_FOVEATION_ZONES, r.rows, HALO_FOVEATION_ZONES);
            assert(map);
            for (int axis = 0; axis < 2; ++axis) {
                int n = axis ? h : w;
                const float *weight = axis ? r.row_weight : r.column_weight;
                for (int i = 0; i < n; ++i) {
                    int zone = i * HALO_FOVEATION_ZONES / n;
                    if (weight[zone] < HALO_FOVEATION_HIDDEN) continue;
                    double d = slope(map->map, axis, i, w, h);
                    if (fabs(d - 1.0) > 1e-4) {
                        printf("FAIL %dx%d layer %d axis %d: visible line %d (zone %d, weight %.4f) at rate %.4f\n", w, h, layer, axis, i, zone, weight[zone], d);
                        return 1;
                    }
                    checked_rows++;
                }
            }
            fractions[layer] = mr_rate_map_shading_fraction(map);
            lowered_layers += fractions[layer] < 0.999;
            mr_rate_map_release(map);
        }
        printf("%4dx%-4d BALANCED shaded fraction per layer: 0 %.3f  1 %.3f  2 %.3f  4 %.3f  5 %.3f  6 %.3f  7 %.3f  8 %.3f  9 %.3f\n",
               w, h, fractions[0], fractions[1], fractions[2], fractions[4], fractions[5], fractions[6], fractions[7], fractions[8], fractions[9]);
        assert(fractions[1] < 0.95 && fractions[4] < 0.95 && fractions[8] < 0.95);
        double aggressive[10] = {0};
        for (int layer = 5; layer <= 9; ++layer) {
            HaloFoveationRates r;
            assert(halo_foveation_rates(&in, layer, HALO_FOVEATION_AGGRESSIVE, &r));
            mr_rate_map *map = mr_rate_map_create(w, h, r.columns, HALO_FOVEATION_ZONES, r.rows, HALO_FOVEATION_ZONES);
            aggressive[layer] = mr_rate_map_shading_fraction(map); mr_rate_map_release(map);
            assert(aggressive[layer] < 0.5);
        }
        printf("          AGGRESSIVE rear/caps: 5 %.3f  6 %.3f  7 %.3f  8 %.3f  9 %.3f\n",
               aggressive[5], aggressive[6], aggressive[7], aggressive[8], aggressive[9]);
    }
    printf("PASS panorama rate maps: %u visible rows/columns all at full rate under Metal's mapping, %u layer maps lowered\n",
           checked_rows, lowered_layers);
}}
