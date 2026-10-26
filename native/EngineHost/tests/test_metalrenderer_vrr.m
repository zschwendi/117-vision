/* Engine-pass foveation (metalrenderer_vrr.inc) on the Mac GPU.
 *
 * Every step runs twice in lockstep: into a context drawn through a
 * rasterization rate map and into a reference context drawn without one.
 * Where the map is at full rate the two must agree exactly (8-bit channels,
 * zones with a margin from their boundaries); elsewhere they must agree
 * closely (the reduced zones are shaded at a quarter rate and resolved).
 *
 *  - draw and readback; publication (mr_publish_scene_layer, AA off)
 *  - D3D viewport/scissor rectangles under the map
 *  - partial clears: depth only, colour only, with a different world map,
 *    then a full clear and an unmapped (screen-layout) pass
 *  - a render-target ID fetched once and sampled by later draws into
 *    another context after the source is redrawn, cleared, moves from
 *    physical to screen layout, and after a same-size resize
 *  - CPU upload into a target whose attachments are in physical layout
 * With the fast paths off (clears as passes) and on (clears folded into
 * load actions). Prints SKIP without Metal or rate-map support.
 *
 * clang -O2 -fobjc-arc -fblocks tests/test_metalrenderer_vrr.m
 *   -framework Foundation -framework Metal -o /tmp/mr-vrr */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main

enum { W = 1024, H = 768, ZONES_X = 8, ZONES_Y = 8 };
static const float columns[ZONES_X] = { 0.25f, 0.25f, 1, 1, 1, 1, 0.25f, 0.25f };
static const float rows[ZONES_Y] = { 0.25f, 1, 1, 1, 1, 1, 1, 0.25f };
static const float rows_alt[ZONES_Y] = { 0.5f, 0.5f, 1, 1, 1, 1, 1, 1 };

/* Full rate as Metal actually maps it: rates are interpolated near zone
 * changes, so a pixel counts only where screen -> physical has slope 1 at it
 * and at both neighbours (the resolve samples bilinearly), in x and in y. */
static unsigned char column_full[W], row_full[H];
static void classify(mr_rate_map *map) {
    for (int axis = 0; axis < 2; ++axis) {
        int n = axis ? H : W; unsigned char *out = axis ? row_full : column_full;
        double slope[2048];
        for (int i = 0; i < n; ++i) {
            MTLCoordinate2D a = axis ? MTLCoordinate2DMake(W / 2, i) : MTLCoordinate2DMake(i, H / 2);
            MTLCoordinate2D b = axis ? MTLCoordinate2DMake(W / 2, i + 1) : MTLCoordinate2DMake(i + 1, H / 2);
            MTLCoordinate2D pa = [map->map mapScreenToPhysicalCoordinates:a forLayer:0], pb = [map->map mapScreenToPhysicalCoordinates:b forLayer:0];
            slope[i] = axis ? pb.y - pa.y : pb.x - pa.x;
        }
        for (int i = 0; i < n; ++i) {
            int ok = 1;
            for (int j = i - 1; j <= i + 1; ++j) if (j >= 0 && j < n && fabs(slope[j] - 1.0) > 1e-4) ok = 0;
            out[i] = (unsigned char)ok;
        }
    }
}
static int full_rate(int x, int y) { return column_full[x] && row_full[y]; }
static void quad(mr_context *c, uint32_t c0, uint32_t c1, float x0, float y0, float x1, float y1, float z, int depth, uint32_t texture) {
    mr_vertex_rhw v[4] = { { x0, y0, z, 1, c0, 0, 0 }, { x1, y0, z, 1, c1, 1, 0 },
                           { x0, y1, z, 1, c1, 0, 1 }, { x1, y1, z, 1, c0, 1, 1 } };
    mr_draw_state st = {0}; st.blend = MR_BLEND_NONE; st.color_write_mask = 0xF; st.alpha_test_ref = -1;
    st.texture = texture; st.linear_filter = 0; st.address_clamp = 1;
    st.depth_enable = depth; st.depth_write = depth; st.depth_compare = depth ? 2 /* LESS */ : 8;
    assert(mr_draw_rhw(c, &st, MR_TRIANGLE_STRIP, v, sizeof v[0], 4, NULL, 0) == MR_OK);
}
typedef struct { unsigned full_mismatch, full_pixels; double mean; int worst; } diff_t;
static diff_t compare(const uint32_t *a, const uint32_t *b) {
    diff_t d = {0}; double sum = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        uint32_t p = a[y * W + x], q = b[y * W + x]; int m = 0;
        for (int k = 0; k < 32; k += 8) { int e = abs((int)((p >> k) & 255) - (int)((q >> k) & 255)); if (e > m) m = e; sum += e; }
        if (full_rate(x, y)) { d.full_pixels++; if (m > 1) { if (!d.full_mismatch && getenv("VRR_DEBUG")) printf("    first mismatch (%d,%d) mapped %08x reference %08x\n", x, y, p, q); d.full_mismatch++; } }
        if (m > d.worst) d.worst = m;
    }
    d.mean = sum / (W * H * 4.0); return d;
}
static void expect_same(const char *step, mr_context *mapped, mr_context *reference, double tolerance) {
    static uint32_t a[W * H], b[W * H];
    assert(!mr_read_framebuffer(mapped, a, sizeof a) && !mr_read_framebuffer(reference, b, sizeof b));
    diff_t d = compare(a, b);
    printf("  %-44s full-rate mismatches %u/%u, mean |d| %.3f, worst %d\n", step, d.full_mismatch, d.full_pixels, d.mean, d.worst);
    assert(d.full_pixels > 100000 && d.full_mismatch == 0 && d.mean <= tolerance);
}
static mr_rate_map *map_a, *map_b;
/* The same draw into both, the map applied to `mapped` only. */
#define BOTH(map, call_mapped, call_reference) do { mr_set_world_rate_map(map); call_mapped; mr_set_world_rate_map(NULL); call_reference; } while (0)

static void run(int fast) {
    mr_set_fast_paths(fast);
    printf(" fast paths %s\n", fast ? "on" : "off");
    mr_vrr_stats_t before; mr_vrr_stats(&before);
    mr_context *m = mr_create(W, H), *r = mr_create(W, H), *dm = mr_create(W, H), *dr = mr_create(W, H);
    assert(m && r && dm && dr);
    /* 1. Draw and readback. */
    BOTH(map_a, quad(m, 0xFF2040A0u, 0xFFE0C010u, 0, 0, W, H, .5f, 1, 0), quad(r, 0xFF2040A0u, 0xFFE0C010u, 0, 0, W, H, .5f, 1, 0));
    BOTH(map_a, quad(m, 0xFFFF0000u, 0xFF00FF00u, 150.f, 85.f, 804.f, 602.f, .25f, 1, 0), quad(r, 0xFFFF0000u, 0xFF00FF00u, 150.f, 85.f, 804.f, 602.f, .25f, 1, 0));
    assert(m->layout[MR_ATTACH_COLOR] == MR_LAYOUT_PHYSICAL || m->s->pending_target == m);
    expect_same("draw + readback", m, r, 6.0);
    /* 2. Viewport/scissor. */
    mr_set_viewport(m, 160, 120, 600, 480); mr_set_viewport(r, 160, 120, 600, 480);
    BOTH(map_a, quad(m, 0xFF00FFFFu, 0xFFFF00FFu, 0, 0, W, H, .1f, 0, 0), quad(r, 0xFF00FFFFu, 0xFFFF00FFu, 0, 0, W, H, .1f, 0, 0));
    mr_set_viewport(m, 0, 0, W, H); mr_set_viewport(r, 0, 0, W, H);
    expect_same("viewport/scissor rectangle", m, r, 6.0);
    /* 3. Partial clears and loads. Depth only: colour and stencil stay in the
     * map's layout, and the next pass must keep that map even though the
     * world map is now a different one. */
    mr_clear_depth(m, 1.f); mr_clear_depth(r, 1.f);
    BOTH(map_b, quad(m, 0xFF808080u, 0xFF404040u, 240, 160, 720, 560, .75f, 1, 0), quad(r, 0xFF808080u, 0xFF404040u, 240, 160, 720, 560, .75f, 1, 0));
    expect_same("depth-only clear, retained layout", m, r, 6.0);
    mr_clear(m, 0xFF000000u); mr_clear(r, 0xFF000000u);   /* colour only: depth keeps its layout */
    BOTH(map_b, quad(m, 0xFFFFFFFFu, 0xFF000080u, 80, 80, 944, 688, .6f, 1, 0), quad(r, 0xFFFFFFFFu, 0xFF000080u, 80, 80, 944, 688, .6f, 1, 0));
    expect_same("colour-only clear, depth test through layout", m, r, 6.0);
    /* 4. A target ID fetched once, sampled later from another context. */
    uint32_t id_m = mr_target_texture(m), id_r = mr_target_texture(r);
    assert(id_m == m->target_id && id_r == r->target_id);
    mr_set_world_rate_map(NULL);   /* the destinations are drawn without a map */
    quad(dm, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_m); quad(dr, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_r);
    expect_same("sampled through a retained ID", dm, dr, 6.0);
    BOTH(map_a, quad(m, 0xFF103070u, 0xFFF0F0F0u, 0, 0, W, H, .05f, 0, 0), quad(r, 0xFF103070u, 0xFFF0F0F0u, 0, 0, W, H, .05f, 0, 0));
    quad(dm, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_m); quad(dr, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_r);
    expect_same("retained ID after the source is redrawn", dm, dr, 6.0);
    mr_clear(m, 0xFF336699u); mr_clear(r, 0xFF336699u);
    quad(dm, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_m); quad(dr, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_r);
    expect_same("retained ID after a colour clear", dm, dr, 0.0);
    /* Full clear, then a pass without a map: physical -> screen layout. */
    mr_clear(m, 0xFF000000u); mr_clear_depth(m, 1.f); mr_clear_stencil(m, 0);
    mr_clear(r, 0xFF000000u); mr_clear_depth(r, 1.f); mr_clear_stencil(r, 0);
    quad(m, 0xFFAA5500u, 0xFF0055AAu, 40, 40, 984, 728, .5f, 1, 0); quad(r, 0xFFAA5500u, 0xFF0055AAu, 40, 40, 984, 728, .5f, 1, 0);
    end_pending_encoder(m->s);
    assert(m->layout[MR_ATTACH_COLOR] == MR_LAYOUT_SCREEN && !m->layout_map);
    quad(dm, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_m); quad(dr, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_r);
    expect_same("retained ID after physical -> screen", dm, dr, 0.0);
    /* Back to physical, then a same-size resize (drains, keeps contents). */
    mr_clear(m, 0xFF000000u); mr_clear_depth(m, 1.f); mr_clear(r, 0xFF000000u); mr_clear_depth(r, 1.f);
    BOTH(map_a, quad(m, 0xFF00C000u, 0xFFC00000u, 0, 0, W, H, .5f, 1, 0), quad(r, 0xFF00C000u, 0xFFC00000u, 0, 0, W, H, .5f, 1, 0));
    assert(mr_resize(m, W, H) == MR_OK && mr_resize(r, W, H) == MR_OK);
    assert(m->layout[MR_ATTACH_COLOR] == MR_LAYOUT_PHYSICAL && m->layout_map == map_a);
    quad(dm, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_m); quad(dr, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, W, H, 0, 0, id_r);
    expect_same("retained ID after a same-size resize", dm, dr, 6.0);
    /* 5. CPU upload into a physical target, then a draw over it. */
    static uint32_t pixels[W * H];
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) pixels[y * W + x] = 0xFF000000u | (uint32_t)((x & 255) << 16) | (uint32_t)((y * 255 / H) << 8) | (uint32_t)((x ^ y) & 255);
    assert(!mr_write_framebuffer(m, pixels, sizeof pixels) && !mr_write_framebuffer(r, pixels, sizeof pixels));
    assert(m->layout[MR_ATTACH_COLOR] == MR_LAYOUT_PHYSICAL);
    expect_same("CPU upload into physical layout, readback", m, r, 12.0);
    BOTH(map_a, quad(m, 0xFF0000FFu, 0xFFFFFF00u, 360, 240, 680, 520, .4f, 1, 0), quad(r, 0xFF0000FFu, 0xFFFFFF00u, 360, 240, 680, 520, .4f, 1, 0));
    expect_same("draw after the upload (depth through layout)", m, r, 12.0);
    /* 6. Publication decodes; it equals the readback exactly. */
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:W height:H mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
    id<MTLTexture> layer = [m->s->dev newTextureWithDescriptor:d];
    mr_scene_publish pub = { .aa = MR_SCENE_AA_OFF, .upscale = MR_SCENE_UPSCALE_NONE };
    assert(mr_publish_scene_layer(m, (__bridge void *)layer, &pub) == MR_OK && pub.foveation_resolved == 1);
    static uint32_t published[W * H], read[W * H];
    assert(!mr_read_framebuffer(m, read, sizeof read));
    [layer getBytes:published bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
    assert(!memcmp(published, read, sizeof read));
    printf("  %-44s identical to the readback\n", "publication (resolve, AA off)");
    mr_vrr_stats_t after; mr_vrr_stats(&after);
    printf("  stats: mapped %llu unmapped %llu resolves %llu upload re-encodes %llu retained-layout passes %llu, last shading %.3f\n",
           (unsigned long long)(after.mapped_passes - before.mapped_passes), (unsigned long long)(after.unmapped_passes - before.unmapped_passes),
           (unsigned long long)(after.resolves - before.resolves), (unsigned long long)(after.upload_reencodes - before.upload_reencodes),
           (unsigned long long)(after.retained_layout_passes - before.retained_layout_passes), after.last_shading_fraction);
    assert(after.mapped_passes > before.mapped_passes && after.resolves > before.resolves &&
           after.upload_reencodes > before.upload_reencodes && after.retained_layout_passes > before.retained_layout_passes);
    mr_set_world_rate_map(NULL);
    mr_destroy(dr); mr_destroy(dm); mr_destroy(r); mr_destroy(m);
}

int main(void) { @autoreleasepool {
    mr_context *probe = mr_create(8, 8);
    if (!probe) { puts("SKIP no Metal"); return 77; }
    mr_destroy(probe);
    if (!mr_rate_maps_supported()) { puts("SKIP no rasterization rate maps"); return 77; }
    map_a = mr_rate_map_create(W, H, columns, ZONES_X, rows, ZONES_Y);
    float columns_b[ZONES_X]; for (int i = 0; i < ZONES_X; ++i) columns_b[i] = 1;
    map_b = mr_rate_map_create(W, H, columns_b, ZONES_X, rows_alt, ZONES_Y);
    assert(map_a && map_b);
    printf("map A shades %.3f of the pixels, map B %.3f\n", mr_rate_map_shading_fraction(map_a), mr_rate_map_shading_fraction(map_b));
    assert(mr_rate_map_shading_fraction(map_a) < 0.7);
    classify(map_a);
    { unsigned fc = 0, fr = 0; for (int i = 0; i < W; ++i) fc += column_full[i]; for (int i = 0; i < H; ++i) fr += row_full[i];
      printf("full-rate by the map's own mapping: %u of %d columns, %u of %d rows\n", fc, W, fr, H); }
    assert(mr_rate_map_create(W, H, columns, 0, rows, ZONES_Y) == NULL);
    run(0); run(1);
    mr_rate_map_release(map_a); mr_rate_map_release(map_b);
    puts("PASS foveation: exact full-rate agreement through draws, viewport/scissor, partial clears with retained layouts, "
         "retained target IDs across redraw/clear/layout change/resize, uploads and publication, fast paths off and on");
}}
