/* Mac GPU measurements of the graphics paths, taken with the renderer's own
 * GPU timing (metalrenderer_timing.inc). Synthetic content, not Halo: these
 * numbers say what each pass costs on this Mac's GPU, not what the headset
 * will gain. Not part of the source checks; run by hand:
 *
 * clang -O2 -fobjc-arc -fblocks tests/bench_graphics_post.m
 *   -framework Foundation -framework Metal -o /tmp/bench-graphics && /tmp/bench-graphics
 *
 * 1. Publication of one world layer at 2048x1536: plain copy, FXAA, SMAA;
 *    and at 1536x1152 (75 %) upscaled to 2048x1536: MetalFX, bilinear, each
 *    after FXAA. Post+copy GPU time per layer.
 * 2. A fragment-bound synthetic world pass at 2048x1536 (blended full-screen
 *    layers) with and without the BALANCED centre-bearing rate map, and the
 *    AGGRESSIVE rear map: scene GPU time per pass, and the resolve.
 * Configurations are interleaved frame by frame so drift, thermal state and
 * other GPU users affect each alike; medians are reported. */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main
#include "../panorama_foveation.h"

static void layer_quads(mr_context *c, int layers) {
    for (int i = 0; i < layers; ++i) {
        uint32_t color = 0x30000000u | (uint32_t)((i * 37) & 255) << 16 | (uint32_t)((i * 91) & 255) << 8 | (uint32_t)((i * 13) & 255);
        float w = (float)c->w, h = (float)c->h;
        mr_vertex_rhw v[4] = { { 0, 0, 0, 1, color, 0, 0 }, { w, 0, 0, 1, color ^ 0x00FFFFFFu, 1, 0 },
                               { 0, h, 0, 1, color ^ 0x00FF00FFu, 0, 1 }, { w, h, 0, 1, color, 1, 1 } };
        mr_draw_state st = {0}; st.blend = MR_BLEND_SRC_ALPHA; st.color_write_mask = 0xF; st.alpha_test_ref = -1; st.depth_compare = 8;
        assert(mr_draw_rhw(c, &st, MR_TRIANGLE_STRIP, v, sizeof v[0], 4, NULL, 0) == MR_OK);
    }
}
static void frame(void) {
    id<MTLCommandBuffer> cb = g_shared->pending_cb; mr_commit_async(NULL, NULL); if (cb) [cb waitUntilCompleted];
    for (int i = 0; i < 100; ++i) {
        os_unfair_lock_lock(&g_timing.lock); int busy = 0;
        for (int k = 0; k < MR_TIMING_FRAMES; ++k) busy |= g_timing.records[k].frame != 0;
        os_unfair_lock_unlock(&g_timing.lock); if (!busy) break; usleep(1000);
    }
}
static id<MTLTexture> layer_texture(mr_context *c, int w, int h, int upscale) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    d.usage = (MTLTextureUsage)mr_scene_destination_usage(upscale); d.storageMode = MTLStorageModePrivate;
    return [c->s->dev newTextureWithDescriptor:d];
}
static void wait_post(void) {
    for (int i = 0; i < 2000; ++i) { mr_post_status_t s; mr_post_status(&s); if (s.smaa != MR_POST_PREPARING && s.metalfx != MR_POST_PREPARING) return; usleep(10000); }
}
/* The latest frame's own record, if it was measured since `before`. */
static int last_frame(uint64_t before, mr_gpu_timing_frame *f) {
    mr_gpu_timing_stats st; mr_gpu_timing_read(&st);
    if (st.frames_measured <= before) return 0;
    *f = st.last; return 1;
}
static int cmp_double(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static void summary(const char *label, const char *what, double *v, int n, int unmeasured) {
    qsort(v, n, sizeof *v, cmp_double);
    double sum = 0; for (int i = 0; i < n; ++i) sum += v[i];
    printf("  %-56s %s median %.3f ms, mean %.3f, p90 %.3f (%d frames measured, %d not)\n", label, what,
           n ? v[n / 2] : 0, n ? sum / n : 0, n ? v[n * 9 / 10] : 0, n, unmeasured);
}

int main(void) { @autoreleasepool {
    mr_context *native = mr_create(2048, 1536), *reduced = mr_create(1536, 1152);
    if (!native || !reduced) { puts("SKIP no Metal"); return 77; }
    mr_gpu_timing_set_enabled(1);
    printf("device %s\n", native->s->dev.name.UTF8String);
    mr_post_prepare(MR_SCENE_AA_SMAA, MR_SCENE_UPSCALE_METALFX, 1536, 1152, 2048, 1536);
    mr_post_prepare(MR_SCENE_AA_SMAA, MR_SCENE_UPSCALE_NONE, 2048, 1536, 2048, 1536);
    wait_post();
    enum { FRAMES = 200 };
    id<MTLTexture> out = layer_texture(native, 2048, 1536, MR_SCENE_UPSCALE_METALFX);
    printf("1. world layer publication (post-processing and copies only)\n");
    struct { const char *label; mr_context *c; int aa, upscale; } cases[] = {
        { "2048x1536 copy (AA off)", native, MR_SCENE_AA_OFF, MR_SCENE_UPSCALE_NONE },
        { "2048x1536 FXAA (current default)", native, MR_SCENE_AA_FXAA, MR_SCENE_UPSCALE_NONE },
        { "2048x1536 SMAA", native, MR_SCENE_AA_SMAA, MR_SCENE_UPSCALE_NONE },
        { "1536x1152 FXAA + MetalFX spatial -> 2048x1536", reduced, MR_SCENE_AA_FXAA, MR_SCENE_UPSCALE_METALFX },
        { "1536x1152 SMAA + MetalFX spatial -> 2048x1536", reduced, MR_SCENE_AA_SMAA, MR_SCENE_UPSCALE_METALFX },
        { "1536x1152 FXAA + bilinear -> 2048x1536", reduced, MR_SCENE_AA_FXAA, MR_SCENE_UPSCALE_BILINEAR },
    };
    enum { CASES = sizeof cases / sizeof cases[0] };
    double values[CASES][FRAMES]; int counts[CASES] = {0}, misses[CASES] = {0};
    for (int f = -1; f < FRAMES; ++f) for (unsigned k = 0; k < CASES; ++k) {   /* interleaved; f = -1 warms */
        mr_scene_publish p = { .aa = cases[k].aa, .fxaa_subpix = .5f, .upscale = cases[k].upscale };
        layer_quads(cases[k].c, 1);
        mr_gpu_timing_stats st; mr_gpu_timing_read(&st); uint64_t before = st.frames_measured;
        assert(mr_publish_scene_layer(cases[k].c, (__bridge void *)out, &p) == MR_OK);
        assert(p.aa_effective == cases[k].aa && (cases[k].upscale == MR_SCENE_UPSCALE_NONE || p.upscale_effective == cases[k].upscale));
        frame();
        mr_gpu_timing_frame last;
        if (f < 0) continue;
        if (last_frame(before, &last)) values[k][counts[k]++] = (last.category_ns[MR_GPU_POST] + last.category_ns[MR_GPU_COPY]) / 1e6;
        else misses[k]++;
    }
    for (unsigned k = 0; k < CASES; ++k) summary(cases[k].label, "post+copy", values[k], counts[k], misses[k]);
    printf("2. fragment-bound synthetic world pass, 2048x1536, 24 blended full-screen layers\n");
    HaloFoveationInput in; memset(&in, 0, sizeof in);
    for (int l = 0; l < HALO_PANORAMA_LAYERS; ++l) {
        in.projection_x[l] = 1.f / tanf(32.f * (float)M_PI / 180.f); in.projection_y[l] = 1.f / tanf(52.5f * (float)M_PI / 180.f);
        in.viewport[l][2] = in.viewport[l][3] = 1.f;
    }
    in.projection_x[5] = in.projection_x[6] = 1.f / tanf(48.f * (float)M_PI / 180.f);
    HaloFoveationRates balanced, aggressive;
    assert(halo_foveation_rates(&in, 1, HALO_FOVEATION_BALANCED, &balanced) && halo_foveation_rates(&in, 8, HALO_FOVEATION_AGGRESSIVE, &aggressive));
    mr_rate_map *maps[3] = { NULL, mr_rate_map_create(2048, 1536, balanced.columns, 32, balanced.rows, 32),
                             mr_rate_map_create(2048, 1536, aggressive.columns, 32, aggressive.rows, 32) };
    const char *labels[3] = { "no rate map", "BALANCED centre-bearing map", "AGGRESSIVE rear-bearing map" };
    mr_scene_publish p = { .aa = MR_SCENE_AA_OFF, .upscale = MR_SCENE_UPSCALE_NONE };
    double scene[3][FRAMES], post[3][FRAMES]; int counts2[3] = {0}, misses2[3] = {0};
    for (int f = -1; f < FRAMES; ++f) for (int k = 0; k < 3; ++k) {
        mr_clear(native, 0xFF000000u); mr_clear_depth(native, 1.f); mr_clear_stencil(native, 0);
        mr_set_world_rate_map(maps[k]); layer_quads(native, 24); mr_set_world_rate_map(NULL);
        mr_gpu_timing_stats st; mr_gpu_timing_read(&st); uint64_t before = st.frames_measured;
        assert(mr_publish_scene_layer(native, (__bridge void *)out, &p) == MR_OK);
        frame();
        mr_gpu_timing_frame last;
        if (f < 0) continue;
        if (last_frame(before, &last)) { scene[k][counts2[k]] = last.category_ns[MR_GPU_SCENE] / 1e6;
            post[k][counts2[k]++] = (last.category_ns[MR_GPU_POST] + last.category_ns[MR_GPU_COPY]) / 1e6; }
        else misses2[k]++;
    }
    for (int k = 0; k < 3; ++k) {
        char label[128]; snprintf(label, sizeof label, "%s (shades %.3f)", labels[k], maps[k] ? mr_rate_map_shading_fraction(maps[k]) : 1.0);
        summary(label, "scene", scene[k], counts2[k], misses2[k]);
        summary("", "  resolve/copy", post[k], counts2[k], 0);
    }
    mr_rate_map_release(maps[1]); mr_rate_map_release(maps[2]);
    puts("DONE Mac GPU measurements (synthetic; not headset evidence)");
}}
