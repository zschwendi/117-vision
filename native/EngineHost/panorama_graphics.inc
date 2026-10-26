/* panorama_graphics.inc - the producer's graphics port, included by
 * panorama_render.inc unless a test supplies its own (PANORAMA_GRAPHICS_PORT).
 * Every renderer and settings call the panorama producer makes for anti-
 * aliasing, upscaling, the HUD raster and engine-pass foveation is here. */

/* Defaults for builds that include d3d9.c without halo_settings.c (device
 * tests); the app and the probe link the real settings. */
__attribute__((weak)) int halo_graphics_scene_aa(void) { return HALO_SCENE_AA_FXAA; }
__attribute__((weak)) int halo_graphics_engine_foveation(void) { return HALO_FOVEATION_OFF; }
__attribute__((weak)) int halo_graphics_gpu_timing(void) { return 0; }
__attribute__((weak)) void halo_graphics_upscale(HaloSceneUpscale *out) { if (out) memset(out, 0, sizeof *out); }
__attribute__((weak)) int halo_graphics_upscale_output(unsigned w, unsigned h, unsigned *ow, unsigned *oh) {
    if (ow) *ow = w;
    if (oh) *oh = h;
    return 0;
}
__attribute__((weak)) void halo_graphics_status_publish(const HaloGraphicsStatus *status) { (void)status; }

static int panorama_gfx_renderer_aa(int aa) {
    return aa == HALO_SCENE_AA_SMAA ? MR_SCENE_AA_SMAA : aa == HALO_SCENE_AA_OFF ? MR_SCENE_AA_OFF : MR_SCENE_AA_FXAA;
}
static int panorama_gfx_renderer_upscale(int upscale) {
    return upscale == HALO_UPSCALE_METALFX ? MR_SCENE_UPSCALE_METALFX :
           upscale == HALO_UPSCALE_BILINEAR ? MR_SCENE_UPSCALE_BILINEAR : MR_SCENE_UPSCALE_NONE;
}

/* Once per engine frame, at its first pass. */
static void panorama_gfx_configure(PanoramaGraphicsFrame *f, uint32_t width, uint32_t height, int sink) {
    memset(f, 0, sizeof *f);
    f->configured = 1; f->source_width = width; f->source_height = height;
    f->aa = halo_graphics_scene_aa();
    f->foveation_selected = halo_graphics_engine_foveation();
    f->foveation_supported = mr_rate_maps_supported();
    f->foveation = f->foveation_supported ? f->foveation_selected : HALO_FOVEATION_OFF;
    unsigned ow = width, oh = height;
    HaloSceneUpscale u; halo_graphics_upscale(&u);
    /* Upscaling needs the zero-copy pool: the CPU readback path publishes
     * the back buffer as it is. */
    f->upscale = sink && halo_graphics_upscale_output(width, height, &ow, &oh) ? u.mode : HALO_UPSCALE_OFF;
    if (f->upscale == HALO_UPSCALE_OFF) { ow = width; oh = height; }
    f->output_width = ow; f->output_height = oh;
    mr_post_prepare(panorama_gfx_renderer_aa(f->aa), panorama_gfx_renderer_upscale(f->upscale),
                    (int)width, (int)height, (int)ow, (int)oh);
}

/* The HUD raster: the presented size when the scene is upscaled, so Halo's
 * own interface draws into the HUD layer (artwork, text, the untracked
 * reticle) are rasterized there rather than resampled. Tracked Sense reticle
 * artwork is captured separately (sense_reticle_art.inc) at the logical
 * size and is not affected. Returns 1 when the raster has the wanted size. */
static int panorama_gfx_hud_raster(void *renderer, const PanoramaGraphicsFrame *f, uint32_t width, uint32_t height) {
    uint32_t w = f->upscale != HALO_UPSCALE_OFF ? f->output_width : width;
    uint32_t h = f->upscale != HALO_UPSCALE_OFF ? f->output_height : height;
    if (mr_set_render_scale(renderer, (int)w, (int)h) == MR_OK) return 1;
    /* Allocation failed: keep the logical raster (already allocated), and
     * let publication resample it. */
    mr_set_render_scale(renderer, (int)width, (int)height);
    return 0;
}

/* Rate maps per world layer, rebuilt only when what they depend on changes. */
static struct {
    mr_rate_map *map;
    uint64_t signature;
} panorama_gfx_maps[HALO_PANORAMA_LAYERS];
static uint64_t panorama_gfx_signature(const HaloFoveationInput *g, int mode, uint32_t w, uint32_t h) {
    uint64_t hash = UINT64_C(1469598103934665603);
    int32_t words[4 + HALO_PANORAMA_LAYERS * 6];
    words[0] = mode; words[1] = (int32_t)w; words[2] = (int32_t)h; words[3] = 1;
    for (int k = 0; k < HALO_PANORAMA_LAYERS; ++k) {
        words[4 + k * 6] = (int32_t)lroundf(g->projection_x[k] * 10000.f);
        words[5 + k * 6] = (int32_t)lroundf(g->projection_y[k] * 10000.f);
        for (int j = 0; j < 4; ++j) words[6 + k * 6 + j] = (int32_t)lroundf(g->viewport[k][j] * 10000.f);
    }
    const uint8_t *bytes = (const uint8_t *)words;
    for (size_t i = 0; i < sizeof words; ++i) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    return hash ? hash : 1;
}
/* Before a world pass draws. Returns the shaded fraction of the map set
 * (1 when the pass draws without one). */
static int panorama_gfx_map_set;   /* a world rate map is set in the renderer */
static void panorama_gfx_world_end(void) {
    if (panorama_gfx_map_set) { mr_set_world_rate_map(NULL); panorama_gfx_map_set = 0; }
}
static float panorama_gfx_world_begin(int layer, uint32_t width, uint32_t height, const PanoramaGraphicsFrame *f,
                                      const HaloFoveationInput *geometry) {
    panorama_gfx_world_end();
    if (f->foveation == HALO_FOVEATION_OFF || layer < 0 || layer >= HALO_PANORAMA_LAYERS || layer == HALO_PANORAMA_HUD_LAYER) return 1.f;
    uint64_t signature = panorama_gfx_signature(geometry, f->foveation, width, height);
    if (panorama_gfx_maps[layer].signature != signature) {
        mr_rate_map_release(panorama_gfx_maps[layer].map);
        panorama_gfx_maps[layer].map = NULL;
        HaloFoveationRates rates;
        if (halo_foveation_rates(geometry, layer, f->foveation, &rates))
            panorama_gfx_maps[layer].map = mr_rate_map_create((int)width, (int)height, rates.columns, HALO_FOVEATION_ZONES,
                                                              rates.rows, HALO_FOVEATION_ZONES);
        panorama_gfx_maps[layer].signature = signature;
    }
    if (!panorama_gfx_maps[layer].map) return 1.f;
    mr_set_world_rate_map(panorama_gfx_maps[layer].map); panorama_gfx_map_set = 1;
    return (float)mr_rate_map_shading_fraction(panorama_gfx_maps[layer].map);
}

static int panorama_gfx_publish_world(void *renderer, void *texture, const PanoramaGraphicsFrame *f, PanoramaGraphicsLayer *out) {
    mr_scene_publish p = { .aa = panorama_gfx_renderer_aa(f->aa), .fxaa_subpix = 0.5f,
                           .upscale = panorama_gfx_renderer_upscale(f->upscale) };
    int status = mr_publish_scene_layer(renderer, texture, &p);
    out->aa = p.aa_effective == MR_SCENE_AA_SMAA ? HALO_SCENE_AA_SMAA : p.aa_effective == MR_SCENE_AA_FXAA ? HALO_SCENE_AA_FXAA : HALO_SCENE_AA_OFF;
    out->upscale = p.upscale_effective == MR_SCENE_UPSCALE_METALFX ? HALO_UPSCALE_METALFX :
                   p.upscale_effective == MR_SCENE_UPSCALE_BILINEAR ? HALO_UPSCALE_BILINEAR : HALO_UPSCALE_OFF;
    out->foveated = p.foveation_resolved;
    return status;
}
static int panorama_gfx_publish_hud(void *renderer, void *texture) { return mr_publish_overlay_layer(renderer, texture); }
static void panorama_gfx_status(const HaloGraphicsStatus *status) {
    HaloGraphicsStatus s = *status;
    mr_post_status_t post; mr_post_status(&post);
    if (!s.detail[0] && post.detail[0]) snprintf(s.detail, sizeof s.detail, "%s", post.detail);
    halo_graphics_status_publish(&s);
}
