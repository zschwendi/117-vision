/* Producer tests: the panorama_graphics.inc port without a renderer or the
 * settings. Include after the test's mr_blit_target_to / mr_fxaa_target_to
 * stubs and before ../panorama_render.inc. World layers go through
 * mr_fxaa_target_to (or mr_blit_target_to with anti-aliasing off) and the HUD
 * through mr_blit_target_to, as the producer published them before the port;
 * the fields below let a test choose settings and observe every call. */
#define PANORAMA_GRAPHICS_PORT 1
#include "../panorama_graphics.h"

static struct {
    /* Settings the next frame's configure sees. */
    int scene_aa, foveation, foveation_supported, upscale;
    uint32_t output_width, output_height;     /* used when upscale != OFF */
    int hud_raster_fails;
    /* What the producer did. */
    unsigned configures, hud_rasters, world_begins, world_ends, world_maps, publishes, hud_publishes, statuses;
    uint32_t hud_raster_width, hud_raster_height;
    int last_map_layer;
    HaloGraphicsStatus status;
} panorama_gfx_test = { .scene_aa = HALO_SCENE_AA_FXAA, .last_map_layer = -1 };

static void panorama_gfx_configure(PanoramaGraphicsFrame *f, uint32_t width, uint32_t height, int sink) {
    memset(f, 0, sizeof *f);
    panorama_gfx_test.configures++;
    f->configured = 1; f->source_width = width; f->source_height = height;
    f->aa = panorama_gfx_test.scene_aa;
    f->foveation_selected = panorama_gfx_test.foveation;
    f->foveation_supported = panorama_gfx_test.foveation_supported;
    f->foveation = f->foveation_supported ? f->foveation_selected : HALO_FOVEATION_OFF;
    f->upscale = sink ? panorama_gfx_test.upscale : HALO_UPSCALE_OFF;
    f->output_width = f->upscale ? panorama_gfx_test.output_width : width;
    f->output_height = f->upscale ? panorama_gfx_test.output_height : height;
}
static int panorama_gfx_hud_raster(void *renderer, const PanoramaGraphicsFrame *f, uint32_t width, uint32_t height) {
    (void)renderer;
    panorama_gfx_test.hud_rasters++;
    int native = f->upscale && !panorama_gfx_test.hud_raster_fails;
    panorama_gfx_test.hud_raster_width = native ? f->output_width : width;
    panorama_gfx_test.hud_raster_height = native ? f->output_height : height;
    return native || !f->upscale;
}
static float panorama_gfx_world_begin(int layer, uint32_t width, uint32_t height, const PanoramaGraphicsFrame *f,
                                      const HaloFoveationInput *geometry) {
    (void)width; (void)height; (void)geometry;
    panorama_gfx_test.world_begins++;
    if (f->foveation == HALO_FOVEATION_OFF) return 1.f;
    panorama_gfx_test.world_maps++; panorama_gfx_test.last_map_layer = layer;
    return 0.75f;
}
static void panorama_gfx_world_end(void) { panorama_gfx_test.world_ends++; }
static int panorama_gfx_publish_world(void *renderer, void *texture, const PanoramaGraphicsFrame *f, PanoramaGraphicsLayer *out) {
    panorama_gfx_test.publishes++;
    out->aa = f->aa; out->upscale = f->upscale; out->foveated = f->foveation != HALO_FOVEATION_OFF;
    return f->aa == HALO_SCENE_AA_OFF ? mr_blit_target_to(renderer, texture) : mr_fxaa_target_to(renderer, texture, 0.5f);
}
static int panorama_gfx_publish_hud(void *renderer, void *texture) {
    panorama_gfx_test.hud_publishes++;
    return mr_blit_target_to(renderer, texture);
}
static void panorama_gfx_status(const HaloGraphicsStatus *status) { panorama_gfx_test.statuses++; panorama_gfx_test.status = *status; }
