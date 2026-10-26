/* metalrenderer_vrr.inc - engine-pass foveation with rasterization rate maps,
 * included by metalrenderer.m after the shared/context structures.
 *
 * A pass drawn with a rate map stores its attachments in "physical" layout:
 * smaller than the screen where the rate is below one. Viewport and scissor
 * stay in screen coordinates (Metal maps them), so the D3D draw code needs no
 * change; everything that reads the attachments afterwards must convert.
 *
 * Each context remembers, per attachment, what its contents are:
 *   UNDEFINED  never written (any layout may load it: garbage either way)
 *   UNIFORM    cleared to one value (identical in every layout)
 *   SCREEN     drawn without a map
 *   PHYSICAL   drawn with layout_map
 * A pass may use a map only when nothing it loads is SCREEN; contents already
 * in a map's layout keep that map until they are cleared, so a depth or
 * stencil buffer is never read through the wrong layout (neither can be
 * converted: they are not shader-readable). Colour can be converted both
 * ways: resolve (physical -> screen, rate map decoder) for every consumer,
 * and re-encode (screen -> physical, a full-screen draw through the map) for
 * CPU uploads into a physical target. */

/* MR_LAYOUT_*, MR_ATTACH_* and struct mr_rate_map are declared in metalrenderer.m. */

int mr_rate_maps_supported(void) {
    mr_shared *s = shared_state();
    return s && [s->dev supportsRasterizationRateMapWithLayerCount:1];
}
static mr_rate_map *rate_map_retain(mr_rate_map *m) { if (m) m->refs++; return m; }
void mr_rate_map_release(mr_rate_map *m) {
    if (!m || --m->refs > 0) return;
    /* Encoded passes retain the Metal objects themselves (retainedReferences). */
    m->map = nil; m->parameters = nil; free(m);
}
mr_rate_map *mr_rate_map_create(int width, int height, const float *columns, int column_count, const float *rows, int row_count) {
    @autoreleasepool {
    mr_shared *s = shared_state();
    if (!s || width <= 0 || height <= 0 || width > 8192 || height > 8192 || !columns || !rows ||
        column_count < 1 || row_count < 1 || column_count > 256 || row_count > 256) { g_err = "bad rate map arguments"; return NULL; }
    if (![s->dev supportsRasterizationRateMapWithLayerCount:1]) { g_err = "rasterization rate maps are unsupported"; return NULL; }
    MTLRasterizationRateLayerDescriptor *layer = [[MTLRasterizationRateLayerDescriptor alloc]
        initWithSampleCount:MTLSizeMake((NSUInteger)column_count, (NSUInteger)row_count, 0)];
    for (int i = 0; i < column_count; ++i) { float r = columns[i]; layer.horizontalSampleStorage[i] = r > 0.f && r <= 1.f ? r : 1.f; }
    for (int i = 0; i < row_count; ++i) { float r = rows[i]; layer.verticalSampleStorage[i] = r > 0.f && r <= 1.f ? r : 1.f; }
    MTLRasterizationRateMapDescriptor *d = [MTLRasterizationRateMapDescriptor
        rasterizationRateMapDescriptorWithScreenSize:MTLSizeMake((NSUInteger)width, (NSUInteger)height, 0) layer:layer];
    id<MTLRasterizationRateMap> map = [s->dev newRasterizationRateMapWithDescriptor:d];
    if (!map) { g_err = "rate map allocation failed"; return NULL; }
    MTLSizeAndAlign size = map.parameterBufferSizeAndAlign;
    id<MTLBuffer> parameters = [s->dev newBufferWithLength:size.size options:MTLResourceStorageModeShared];
    if (!parameters) { g_err = "rate map parameter buffer allocation failed"; return NULL; }
    [map copyParameterDataToBuffer:parameters offset:0];
    mr_rate_map *m = calloc(1, sizeof *m);
    if (!m) { g_err = "rate map allocation failed"; return NULL; }
    m->refs = 1; m->map = map; m->parameters = parameters; m->width = width; m->height = height;
    m->physical = [map physicalSizeForLayer:0];
    m->shading_fraction = (double)m->physical.width * m->physical.height / ((double)width * height);
    return m;
    }
}
double mr_rate_map_shading_fraction(const mr_rate_map *m) { return m ? m->shading_fraction : 1.0; }
void mr_set_world_rate_map(mr_rate_map *m) {
    mr_shared *s = shared_state(); if (!s) return;
    if (s->world_map == m) return;
    rate_map_retain(m); mr_rate_map_release(s->world_map); s->world_map = m;
}
void mr_vrr_stats(mr_vrr_stats_t *out) {
    if (!out) return;
    out->mapped_passes = atomic_load_explicit(&g_vrr_stats.mapped_passes, memory_order_relaxed);
    out->unmapped_passes = atomic_load_explicit(&g_vrr_stats.unmapped_passes, memory_order_relaxed);
    out->resolves = atomic_load_explicit(&g_vrr_stats.resolves, memory_order_relaxed);
    out->upload_reencodes = atomic_load_explicit(&g_vrr_stats.upload_reencodes, memory_order_relaxed);
    out->retained_layout_passes = atomic_load_explicit(&g_vrr_stats.retained_layout_passes, memory_order_relaxed);
    uint64_t bits = atomic_load_explicit(&g_vrr_stats.last_shading_bits, memory_order_relaxed);
    double shading; memcpy(&shading, &bits, sizeof shading);
    out->last_shading_fraction = bits ? shading : 1.0;
}

static void layout_set_map(mr_context *c, mr_rate_map *m) {
    if (c->layout_map == m) return;
    rate_map_retain(m); mr_rate_map_release(c->layout_map); c->layout_map = m;
}
static void layout_forget(mr_context *c) {
    for (int a = 0; a < 3; ++a) c->layout[a] = MR_LAYOUT_UNDEFINED;
    layout_set_map(c, NULL); c->resolved_valid = 0;
}
static int layout_any(const mr_context *c, int state, int cleared) {
    for (int a = 0; a < 3; ++a) if (!(cleared & (1 << a)) && c->layout[a] == state) return 1;
    return 0;
}
/* Attachments a clear-only pass set to one value. */
static void layout_cleared(mr_context *c, int clears) {
    for (int a = 0; a < 3; ++a) if (clears & (1 << a)) c->layout[a] = MR_LAYOUT_UNIFORM;
    if (!layout_any(c, MR_LAYOUT_PHYSICAL, 0)) layout_set_map(c, NULL);
    if (clears & 1) c->resolved_valid = 0;
}
/* The map a draw pass into c must use, given the clears it will load. */
static mr_rate_map *layout_choose(mr_context *c, int clears) {
    mr_shared *s = c->s;
    if (layout_any(c, MR_LAYOUT_PHYSICAL, clears)) {
        if (s->world_map != c->layout_map) VRR_COUNT(retained_layout_passes);
        return c->layout_map;
    }
    mr_rate_map *wanted = s->world_map;
    int eligible = wanted && !c->overlay_target && c->pw == c->w && c->ph == c->h &&
                   wanted->width == c->w && wanted->height == c->h;
    if (!eligible) return NULL;
    if (layout_any(c, MR_LAYOUT_SCREEN, clears)) return NULL;
    return wanted;
}
/* After a draw pass into c with map m (colour and depth are taken as written;
 * stencil only when a draw enabled it). */
static void layout_drawn(mr_context *c, mr_rate_map *m, int stencil_written) {
    int state = m ? MR_LAYOUT_PHYSICAL : MR_LAYOUT_SCREEN;
    c->layout[MR_ATTACH_COLOR] = state; c->layout[MR_ATTACH_DEPTH] = state;
    if (stencil_written) c->layout[MR_ATTACH_STENCIL] = state;
    if (m) layout_set_map(c, m); else if (!layout_any(c, MR_LAYOUT_PHYSICAL, 0)) layout_set_map(c, NULL);
    c->resolved_valid = 0;
}

/* ---- Conversions --------------------------------------------------------- */
static NSString *const mr_convert_source = @
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"struct ConvertOut { float4 position [[position]]; float2 uv; };\n"
"vertex ConvertOut convert_vertex(uint vid [[vertex_id]]) {\n"
"    float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));\n"
"    ConvertOut o; o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0); o.uv = p; return o;\n"
"}\n"
"constexpr sampler convert_linear(filter::linear, address::clamp_to_edge);\n"
"/* Screen pixel -> physical texel through the map, sampled at its centre. */\n"
"fragment float4 convert_resolve(ConvertOut in [[stage_in]], texture2d<float> src [[texture(0)]],\n"
"                                constant rasterization_rate_map_data &map [[buffer(0)]],\n"
"                                constant float2 &physical [[buffer(1)]]) {\n"
"    rasterization_rate_map_decoder decoder(map);\n"
"    float2 p = decoder.map_screen_to_physical_coordinates(in.position.xy);\n"
"    p = clamp(p, float2(0.5), physical - 0.5);\n"
"    return src.sample(convert_linear, p / float2(src.get_width(), src.get_height()), level(0.0));\n"
"}\n"
"/* Plain resample (bilinear): uploads through a map, render-scale resizes and\n"
" * the bilinear upscale. Interpolated uv is screen space even under a map. */\n"
"fragment float4 convert_resample(ConvertOut in [[stage_in]], texture2d<float> src [[texture(0)]]) {\n"
"    return src.sample(convert_linear, in.uv, level(0.0));\n"
"}\n";

static id<MTLRenderPipelineState> convert_pipeline(mr_shared *s, int resolve) {
    id<MTLRenderPipelineState> __strong *slot = resolve ? &s->convert_resolve : &s->convert_resample;
    if (*slot) return *slot;
    if (s->convert_failed) return nil;
    @autoreleasepool {
    NSError *e = nil;
    if (!s->convert_library) s->convert_library = [s->dev newLibraryWithSource:mr_convert_source options:nil error:&e];
    if (!s->convert_library) { s->convert_failed = 1; NSLog(@"metalrenderer: conversion shaders unavailable: %@", e); return nil; }
    MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = [s->convert_library newFunctionWithName:@"convert_vertex"];
    pd.fragmentFunction = [s->convert_library newFunctionWithName:resolve ? @"convert_resolve" : @"convert_resample"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    *slot = [s->dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!*slot) { s->convert_failed = 1; NSLog(@"metalrenderer: conversion pipeline unavailable: %@", e); }
    return *slot;
    }
}
/* Encode one full-screen conversion pass into dst (BGRA8, render target).
 * map: decode src through it (resolve); through_map: draw dst through it. */
static int encode_convert(mr_shared *s, id<MTLTexture> src, id<MTLTexture> dst, mr_rate_map *decode, mr_rate_map *through_map, int category) {
    id<MTLRenderPipelineState> p = convert_pipeline(s, decode != NULL);
    if (!p || !src || !dst) return MR_ERR_DEVICE;
    end_pending_encoder(s);
    if (ensure_command_buffer(s)) return MR_ERR_DEVICE;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (through_map) rp.rasterizationRateMap = through_map->map;
    id<MTLRenderCommandEncoder> enc = timing_render_encoder(s, rp, category);
    if (!enc) { g_err = "conversion encoder allocation failed"; return MR_ERR_DEVICE; }
    [enc setRenderPipelineState:p];
    [enc setFragmentTexture:src atIndex:0];
    if (decode) {
        float physical[2] = { (float)decode->physical.width, (float)decode->physical.height };
        [enc setFragmentBuffer:decode->parameters offset:0 atIndex:0];
        [enc setFragmentBytes:physical length:sizeof physical atIndex:1];
    }
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    return MR_OK;
}
static id<MTLTexture> context_resolved_texture(mr_context *c) {
    if (c->resolved && c->resolved.width == (NSUInteger)c->w && c->resolved.height == (NSUInteger)c->h) return c->resolved;
    @autoreleasepool {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:c->w height:c->h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;            /* readback reads it directly */
    id<MTLTexture> t = [c->s->dev newTextureWithDescriptor:td];
    if (!t) { g_err = "foveation resolve texture allocation failed"; return nil; }
    c->resolved = t; c->resolved_valid = 0;
    return t;
    }
}
/* c's colour in screen layout at its logical size (encodes clears and ends
 * c's open encoder first). nil on failure. */
static id<MTLTexture> context_screen_color(mr_context *c) {
    mr_shared *s = c->s;
    encode_context_clears(c);
    if (s->pending_encoder && s->pending_target == c) end_pending_encoder(s);
    if (c->layout[MR_ATTACH_COLOR] != MR_LAYOUT_PHYSICAL || !c->layout_map) return c->target;
    id<MTLTexture> resolved = context_resolved_texture(c);
    if (!resolved) return nil;
    if (!c->resolved_valid) {
        if (encode_convert(s, c->target, resolved, c->layout_map, NULL, MR_GPU_POST)) return nil;
        c->resolved_valid = 1; VRR_COUNT(resolves);
    }
    return resolved;
}
