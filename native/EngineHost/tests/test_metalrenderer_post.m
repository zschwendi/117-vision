/* Scene publication and the HUD render scale (metalrenderer_post.inc,
 * mr_set_render_scale) on the Mac GPU.
 *
 *  - A HUD context with logical 160x120 rasterized at 320x240: pretransformed
 *    and clip-space draws, and the D3D viewport/scissor, land on exactly the
 *    scaled pixels; the same size again neither drains nor reallocates; an
 *    impossible size fails and leaves the context untouched; readback and
 *    upload resample to and from the logical size.
 *  - Overlay publication: copied as drawn at the physical size, resampled
 *    when the destination differs.
 *  - Scene anti-aliasing: Off is an exact copy, FXAA and SMAA leave flat
 *    regions untouched and blend edges, and report what ran.
 *  - Upscaling: MetalFX spatial (once built) and bilinear fill the larger
 *    destination, and a marker drawn at one logical position in the world and
 *    in the native-resolution HUD lands at the same output pixel.
 * Prints SKIP without a Metal device.
 *
 * clang -O2 -fobjc-arc -fblocks tests/test_metalrenderer_post.m
 *   -framework Foundation -framework Metal -o /tmp/mr-post */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main

static void rect(mr_context *c, uint32_t color, float x0, float y0, float x1, float y1) {
    mr_vertex_rhw v[4] = { { x0, y0, 0, 1, color, 0, 0 }, { x1, y0, 0, 1, color, 1, 0 },
                           { x0, y1, 0, 1, color, 0, 1 }, { x1, y1, 0, 1, color, 1, 1 } };
    mr_draw_state st = {0}; st.blend = MR_BLEND_NONE; st.color_write_mask = 0xF; st.alpha_test_ref = -1; st.depth_compare = 8;
    assert(mr_draw_rhw(c, &st, MR_TRIANGLE_STRIP, v, sizeof v[0], 4, NULL, 0) == MR_OK);
}
static id<MTLTexture> make_layer(mr_context *c, int w, int h, MTLTextureUsage usage) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    d.usage = usage | MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget; d.storageMode = MTLStorageModeShared;
    return [c->s->dev newTextureWithDescriptor:d];
}
static void finish(void) {
    id<MTLCommandBuffer> cb = g_shared->pending_cb; mr_commit_async(NULL, NULL); if (cb) [cb waitUntilCompleted];
}
static void read_layer(id<MTLTexture> t, uint32_t *out) {
    [t getBytes:out bytesPerRow:t.width * 4 fromRegion:MTLRegionMake2D(0, 0, t.width, t.height) mipmapLevel:0];
}
/* Centroid of pixels whose red channel exceeds 128. */
static void centroid(const uint32_t *p, int w, int h, double *cx, double *cy) {
    double sx = 0, sy = 0, n = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double r = (double)((p[y * w + x] >> 16) & 255) / 255.0;
        if (r > 0.5) { sx += (x + 0.5) * r; sy += (y + 0.5) * r; n += r; }
    }
    assert(n > 0); *cx = sx / n; *cy = sy / n;
}
static int wait_ready(int smaa, int metalfx, double seconds) {
    for (double t = 0; t < seconds; t += 0.01) {
        mr_post_status_t st; mr_post_status(&st);
        if ((!smaa || st.smaa != MR_POST_PREPARING) && (!metalfx || st.metalfx != MR_POST_PREPARING)) return 1;
        usleep(10000);
    }
    return 0;
}

int main(void) { @autoreleasepool {
    mr_context *hud = mr_create(160, 120);
    if (!hud) { puts("SKIP no Metal"); return 77; }
    mr_set_overlay_target(hud);
    /* 1. Render scale. */
    assert(mr_set_render_scale(hud, 320, 240) == MR_OK);
    assert(mr_width(hud) == 160 && mr_physical_width(hud) == 320 && mr_physical_height(hud) == 240);
    mr_clear(hud, 0x00000000u);
    rect(hud, 0xFFFF0000u, 40, 30, 120, 90);               /* logical; physical 80..240 x 60..180 */
    mr_set_viewport(hud, 10, 100, 50, 15);                  /* scissor: physical 20..120 x 200..230 */
    rect(hud, 0xFF00FF00u, 0, 0, 160, 120);
    mr_set_viewport(hud, 0, 0, 160, 120);
    id<MTLTexture> layer = make_layer(hud, 320, 240, 0);
    assert(mr_publish_overlay_layer(hud, (__bridge void *)layer) == MR_OK);   /* as drawn: a copy */
    finish();
    static uint32_t px[320 * 240]; read_layer(layer, px);
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 320; ++x) {
        uint32_t want = (x >= 20 && x < 120 && y >= 200 && y < 230) ? 0xFF00FF00u :
                        (x >= 80 && x < 240 && y >= 60 && y < 180) ? 0xFFFF0000u : 0;
        if (px[y * 320 + x] != want) { printf("FAIL scaled HUD pixel (%d,%d) = %08x, want %08x\n", x, y, px[y * 320 + x], want); return 1; }
    }
    puts("  scaled HUD: pretransformed rectangle and D3D viewport/scissor exact at 2x");
    /* Same size: no drain, no new attachments. */
    rect(hud, 0xFF0000FFu, 0, 0, 4, 4);
    id<MTLCommandBuffer> pending = hud->s->pending_cb; id<MTLTexture> target = hud->target;
    assert(pending && mr_set_render_scale(hud, 320, 240) == MR_OK && hud->s->pending_cb == pending && hud->target == target);
    /* Impossible size: refused, context unchanged. */
    assert(mr_set_render_scale(hud, 0, 240) == MR_ERR_ARGS && hud->target == target && mr_physical_width(hud) == 320);
    puts("  same-size render scale: no drain, no reallocation; invalid size refused without change");
    /* Readback resamples to the logical size; upload back up. */
    static uint32_t logical[160 * 120];
    assert(!mr_read_framebuffer(hud, logical, sizeof logical));
    assert(logical[60 * 160 + 80] == 0xFFFF0000u && logical[107 * 160 + 30] == 0xFF00FF00u && logical[5 * 160 + 150] == 0);
    for (int i = 0; i < 160 * 120; ++i) logical[i] = 0xFF000000u | (uint32_t)((i % 160) << 8);
    assert(!mr_write_framebuffer(hud, logical, sizeof logical));
    assert(mr_publish_overlay_layer(hud, (__bridge void *)layer) == MR_OK); finish(); read_layer(layer, px);
    assert(((px[100 * 320 + 101] >> 8) & 255) == 50);       /* logical column 50 at physical 100..101 */
    /* A destination of another size is resampled. */
    id<MTLTexture> small = make_layer(hud, 160, 120, 0);
    assert(mr_publish_overlay_layer(hud, (__bridge void *)small) == MR_OK); finish();
    static uint32_t spx[160 * 120]; read_layer(small, spx);
    assert(((spx[60 * 160 + 50] >> 8) & 255) == 50);
    puts("  scaled HUD: readback and upload at the logical size; overlay published by copy or resample");

    /* 2. Scene anti-aliasing at one size. */
    enum { SW = 192, SH = 144 };
    mr_context *scene = mr_create(SW, SH);
    mr_clear(scene, 0xFF202020u);
    mr_vertex_rhw tri[3] = { { 20, 130, 0, 1, 0xFFF0F0F0u, 0, 0 }, { 170, 10, 0, 1, 0xFFF0F0F0u, 0, 0 }, { 175, 135, 0, 1, 0xFFF0F0F0u, 0, 0 } };
    mr_draw_state st = {0}; st.blend = MR_BLEND_NONE; st.color_write_mask = 0xF; st.alpha_test_ref = -1; st.depth_compare = 8;
    assert(mr_draw_rhw(scene, &st, MR_TRIANGLE_LIST, tri, sizeof tri[0], 3, NULL, 0) == MR_OK);
    static uint32_t source[SW * SH], out[SW * SH];
    assert(!mr_read_framebuffer(scene, source, sizeof source));
    mr_post_prepare(MR_SCENE_AA_SMAA, MR_SCENE_UPSCALE_NONE, SW, SH, SW, SH);
    assert(wait_ready(1, 0, 10.0));
    for (int aa = MR_SCENE_AA_OFF; aa <= MR_SCENE_AA_SMAA; ++aa) {
        id<MTLTexture> dst = make_layer(scene, SW, SH, 0);
        mr_scene_publish p = { .aa = aa, .fxaa_subpix = .5f, .upscale = MR_SCENE_UPSCALE_NONE };
        assert(mr_publish_scene_layer(scene, (__bridge void *)dst, &p) == MR_OK);
        if (aa == MR_SCENE_AA_SMAA && p.aa_effective != MR_SCENE_AA_SMAA) {   /* first use builds per-size pipelines */
            assert(mr_publish_scene_layer(scene, (__bridge void *)dst, &p) == MR_OK);
        }
        finish(); read_layer(dst, out);
        unsigned changed = 0, flat_changed = 0;
        for (int y = 1; y < SH - 1; ++y) for (int x = 1; x < SW - 1; ++x) {
            int i = y * SW + x, flat = 1;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                int yy = y + dy, xx = x + dx; if (yy < 0 || yy >= SH || xx < 0 || xx >= SW) continue;
                if (source[yy * SW + xx] != source[i]) flat = 0;
            }
            if (out[i] != source[i]) { changed++; if (flat) flat_changed++; }
        }
        const char *names[] = { "Off", "FXAA", "SMAA" };
        printf("  %s: effective %s, %u edge pixels blended, %u flat pixels changed\n", names[aa], names[p.aa_effective], changed, flat_changed);
        assert(p.aa_effective == aa && flat_changed == 0);
        if (aa == MR_SCENE_AA_OFF) assert(changed == 0); else assert(changed > 50);
    }
    mr_post_status_t ps; mr_post_status(&ps); assert(ps.smaa == MR_POST_READY);

    /* 3. Upscaling and HUD alignment: world 96x72 -> 128x96, HUD logical
     * 96x72 rasterized at 128x96, one marker at the same logical place. */
    mr_context *world = mr_create(96, 72), *native_hud = mr_create(96, 72);
    mr_set_overlay_target(native_hud);
    assert(mr_set_render_scale(native_hud, 128, 96) == MR_OK);
    mr_clear(world, 0xFF000000u); mr_clear(native_hud, 0x00000000u);
    rect(world, 0xFFFF0000u, 61, 23, 67, 29); rect(native_hud, 0xFFFF0000u, 61, 23, 67, 29);
    id<MTLTexture> hud_out = make_layer(world, 128, 96, 0);
    assert(mr_publish_overlay_layer(native_hud, (__bridge void *)hud_out) == MR_OK);
    double hx, hy; { finish(); static uint32_t h[128 * 96]; read_layer(hud_out, h); centroid(h, 128, 96, &hx, &hy); }
    mr_post_prepare(MR_SCENE_AA_OFF, MR_SCENE_UPSCALE_METALFX, 96, 72, 128, 96);
    int metalfx_known = wait_ready(0, 1, 20.0);
    for (int mode = MR_SCENE_UPSCALE_METALFX; mode <= MR_SCENE_UPSCALE_BILINEAR; ++mode) {
        id<MTLTexture> dst = make_layer(world, 128, 96, (MTLTextureUsage)mr_scene_destination_usage(mode));
        mr_scene_publish p = { .aa = MR_SCENE_AA_OFF, .upscale = mode };
        assert(mr_publish_scene_layer(world, (__bridge void *)dst, &p) == MR_OK);
        finish(); static uint32_t w[128 * 96]; read_layer(dst, w);
        double wx, wy; centroid(w, 128, 96, &wx, &wy);
        printf("  upscale requested %s: ran %s; world marker (%.2f, %.2f), native HUD marker (%.2f, %.2f), expected (%.2f, %.2f)\n",
               mode == MR_SCENE_UPSCALE_METALFX ? "MetalFX" : "bilinear",
               p.upscale_effective == MR_SCENE_UPSCALE_METALFX ? "MetalFX" : p.upscale_effective == MR_SCENE_UPSCALE_BILINEAR ? "bilinear" : "none",
               wx, wy, hx, hy, 64 * 128 / 96.0, 26 * 96 / 72.0);
        assert(fabs(wx - hx) < 0.75 && fabs(wy - hy) < 0.75);
        assert(fabs(hx - 64 * 128 / 96.0) < 0.5 && fabs(hy - 26 * 96 / 72.0) < 0.5);
        if (mode == MR_SCENE_UPSCALE_BILINEAR) assert(p.upscale_effective == MR_SCENE_UPSCALE_BILINEAR);
        else {
            mr_post_status(&ps);
            assert(metalfx_known && (ps.metalfx == MR_POST_UNAVAILABLE ? p.upscale_effective == MR_SCENE_UPSCALE_BILINEAR
                                                                        : p.upscale_effective == MR_SCENE_UPSCALE_METALFX));
        }
    }
    mr_post_status(&ps);
    printf("  MetalFX state %d (%ux%u -> %ux%u) %s\n", ps.metalfx, ps.metalfx_input_width, ps.metalfx_input_height,
           ps.metalfx_output_width, ps.metalfx_output_height, ps.detail);
    /* A destination smaller than the scene is refused. */
    id<MTLTexture> tiny = make_layer(world, 48, 36, 0);
    mr_scene_publish p = { .aa = MR_SCENE_AA_FXAA, .upscale = MR_SCENE_UPSCALE_METALFX };
    assert(mr_publish_scene_layer(world, (__bridge void *)tiny, &p) == MR_ERR_ARGS);
    mr_destroy(native_hud); mr_destroy(world); mr_destroy(scene); mr_destroy(hud);
    puts("PASS publication: 2x HUD raster exact with viewport/scissor, same-size no-op, failed scale leaves state, logical readback/upload, "
         "Off/FXAA/SMAA (flat regions untouched), MetalFX and bilinear upscaling with world and native HUD markers aligned");
}}
