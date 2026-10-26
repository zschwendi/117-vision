/* metalrenderer_post.inc - scene publication: anti-aliasing and upscaling,
 * included by metalrenderer.m after the FXAA pass and the conversions.
 *
 * A world layer goes to the presenter through, in order:
 *   1. a foveation resolve when its target was drawn with a rate map,
 *   2. FXAA, SMAA 1x or nothing, at the size the engine rendered,
 *   3. MetalFX spatial upscaling when the destination is larger; until the
 *      scaler for those sizes has been built, or where MetalFX is
 *      unsupported, a bilinear resample, and the caller is told which ran.
 * Each step writes the next step's input, or the destination directly when
 * it is the last. The HUD never comes through here (mr_publish_overlay_layer).
 *
 * Heavy objects are built off the engine thread: the SMAA library (about
 * 0.36 s to compile on an M4) and MetalFX scalers (0.7 s for the first).
 * Until ready, publication uses FXAA and bilinear respectively and reports
 * that as the effective mode.
 *
 * SMAA is the reference implementation (third_party/smaa, MIT) rewritten
 * for MSL by tools/generate_smaa_msl.py. The MetalFX approach (perceptual
 * colour processing, a scaler rebuilt for new sizes, bilinear fallback)
 * follows Apple's documentation; VISR's gpu_metal.m (Apache-2.0) does the
 * same and was read for comparison, not copied. */
#include <dlfcn.h>
#include "smaa_msl.inc"

static struct {
    os_unfair_lock lock;
    /* SMAA: library (built asynchronously), lookup textures, and per-size
     * pipelines and intermediate textures (engine thread). */
    int smaa_state;                         /* MR_POST_* */
    id<MTLLibrary> smaa_library;
    id<MTLTexture> smaa_area, smaa_search;
    uint32_t smaa_width, smaa_height;
    id<MTLRenderPipelineState> smaa_edges, smaa_weights, smaa_blend;
    id<MTLTexture> smaa_edges_texture, smaa_weights_texture;
    /* MetalFX: one scaler for the current input/output sizes. */
    int metalfx_state;
    Class metalfx_descriptor;
    id metalfx_scaler;                      /* id<MTLFXSpatialScaler> */
    uint32_t metalfx_in_w, metalfx_in_h, metalfx_out_w, metalfx_out_h;
    uint64_t metalfx_generation;
    unsigned long metalfx_output_usage;
    /* The intermediate between steps, at the engine's size. */
    id<MTLTexture> intermediate[2];
    char detail[128];
} g_post = { .lock = OS_UNFAIR_LOCK_INIT, .metalfx_output_usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget };

static void post_detail(const char *format, ...) {
    va_list args; va_start(args, format);
    os_unfair_lock_lock(&g_post.lock);
    vsnprintf(g_post.detail, sizeof g_post.detail, format, args);
    os_unfair_lock_unlock(&g_post.lock);
    va_end(args);
}

void mr_post_status(mr_post_status_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    os_unfair_lock_lock(&g_post.lock);
    out->smaa = g_post.smaa_state; out->metalfx = g_post.metalfx_state;
    out->metalfx_input_width = g_post.metalfx_in_w; out->metalfx_input_height = g_post.metalfx_in_h;
    out->metalfx_output_width = g_post.metalfx_out_w; out->metalfx_output_height = g_post.metalfx_out_h;
    memcpy(out->detail, g_post.detail, sizeof out->detail);
    os_unfair_lock_unlock(&g_post.lock);
}

/* ---- SMAA ---------------------------------------------------------------- */
static id<MTLTexture> smaa_lookup_texture(mr_shared *s, const unsigned char *deflate, size_t deflate_bytes, size_t bytes,
                                          int width, int height, MTLPixelFormat format, int bytes_per_pixel) {
    NSError *e = nil;
    NSData *data = [[NSData dataWithBytes:deflate length:deflate_bytes] decompressedDataUsingAlgorithm:NSDataCompressionAlgorithmZlib error:&e];
    if (!data || data.length != bytes) return nil;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [s->dev newTextureWithDescriptor:td];
    [t replaceRegion:MTLRegionMake2D(0, 0, width, height) mipmapLevel:0 withBytes:data.bytes bytesPerRow:(NSUInteger)width * bytes_per_pixel];
    return t;
}
static void smaa_request(mr_shared *s) {
    os_unfair_lock_lock(&g_post.lock);
    int state = g_post.smaa_state;
    if (state == MR_POST_NOT_REQUESTED) g_post.smaa_state = MR_POST_PREPARING;
    os_unfair_lock_unlock(&g_post.lock);
    if (state != MR_POST_NOT_REQUESTED) return;
    @autoreleasepool {
    g_post.smaa_area = smaa_lookup_texture(s, mr_smaa_area_deflate, sizeof mr_smaa_area_deflate, MR_SMAA_AREA_BYTES,
                                           MR_SMAA_AREA_WIDTH, MR_SMAA_AREA_HEIGHT, MTLPixelFormatRG8Unorm, 2);
    g_post.smaa_search = smaa_lookup_texture(s, mr_smaa_search_deflate, sizeof mr_smaa_search_deflate, MR_SMAA_SEARCH_BYTES,
                                             MR_SMAA_SEARCH_WIDTH, MR_SMAA_SEARCH_HEIGHT, MTLPixelFormatR8Unorm, 1);
    if (!g_post.smaa_area || !g_post.smaa_search) {
        os_unfair_lock_lock(&g_post.lock); g_post.smaa_state = MR_POST_UNAVAILABLE; os_unfair_lock_unlock(&g_post.lock);
        post_detail("SMAA lookup textures could not be prepared");
        return;
    }
    [s->dev newLibraryWithSource:@(mr_smaa_msl) options:nil completionHandler:^(id<MTLLibrary> library, NSError *error) {
        os_unfair_lock_lock(&g_post.lock);
        g_post.smaa_library = library;
        g_post.smaa_state = library ? MR_POST_READY : MR_POST_UNAVAILABLE;
        os_unfair_lock_unlock(&g_post.lock);
        if (!library) { NSLog(@"metalrenderer: SMAA unavailable: %@", error); post_detail("SMAA failed to compile"); }
    }];
    }
}
static int smaa_state_now(void) {
    os_unfair_lock_lock(&g_post.lock); int state = g_post.smaa_state; os_unfair_lock_unlock(&g_post.lock); return state;
}
/* Pipelines and textures for one size; engine thread, after READY. */
static int smaa_ready_for(mr_shared *s, uint32_t w, uint32_t h, MTLPixelFormat output) {
    if (smaa_state_now() != MR_POST_READY) return 0;
    if (output != MTLPixelFormatBGRA8Unorm) return 0;
    if (g_post.smaa_width == w && g_post.smaa_height == h && g_post.smaa_blend) return 1;
    @autoreleasepool {
    os_unfair_lock_lock(&g_post.lock); id<MTLLibrary> library = g_post.smaa_library; os_unfair_lock_unlock(&g_post.lock);
    MTLFunctionConstantValues *values = [[MTLFunctionConstantValues alloc] init];
    float metrics[4] = { 1.f / (float)w, 1.f / (float)h, (float)w, (float)h };
    [values setConstantValue:metrics type:MTLDataTypeFloat4 atIndex:0];
    NSError *e = nil;
    id<MTLFunction> vertex = [library newFunctionWithName:@"smaa_vertex" constantValues:values error:&e];
    const char *names[3] = { "smaa_edges", "smaa_weights", "smaa_blend" };
    MTLPixelFormat formats[3] = { MTLPixelFormatRG8Unorm, MTLPixelFormatRGBA8Unorm, MTLPixelFormatBGRA8Unorm };
    id<MTLRenderPipelineState> made[3] = { nil, nil, nil };
    for (int i = 0; i < 3 && vertex; ++i) {
        id<MTLFunction> f = [library newFunctionWithName:@(names[i]) constantValues:values error:&e];
        if (!f) break;
        MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vertex; pd.fragmentFunction = f; pd.colorAttachments[0].pixelFormat = formats[i];
        made[i] = [s->dev newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!made[i]) break;
    }
    MTLTextureDescriptor *ed = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:w height:h mipmapped:NO];
    ed.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; ed.storageMode = MTLStorageModePrivate;
    MTLTextureDescriptor *wd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
    wd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; wd.storageMode = MTLStorageModePrivate;
    id<MTLTexture> edges = made[2] ? [s->dev newTextureWithDescriptor:ed] : nil;
    id<MTLTexture> weights = edges ? [s->dev newTextureWithDescriptor:wd] : nil;
    if (!weights) {
        NSLog(@"metalrenderer: SMAA for %ux%u unavailable: %@", w, h, e);
        os_unfair_lock_lock(&g_post.lock); g_post.smaa_state = MR_POST_UNAVAILABLE; os_unfair_lock_unlock(&g_post.lock);
        post_detail("SMAA pipelines or textures for %ux%u could not be made", w, h);
        return 0;
    }
    g_post.smaa_edges = made[0]; g_post.smaa_weights = made[1]; g_post.smaa_blend = made[2];
    g_post.smaa_edges_texture = edges; g_post.smaa_weights_texture = weights;
    g_post.smaa_width = w; g_post.smaa_height = h;
    return 1;
    }
}
static int smaa_pass(mr_shared *s, id<MTLRenderPipelineState> p, id<MTLTexture> dst, int clear,
                     id<MTLTexture> t0, id<MTLTexture> t1, id<MTLTexture> t2) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = clear ? MTLLoadActionClear : MTLLoadActionDontCare;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = timing_render_encoder(s, rp, MR_GPU_POST);
    if (!enc) { g_err = "SMAA encoder allocation failed"; return MR_ERR_DEVICE; }
    [enc setRenderPipelineState:p];
    if (t0) [enc setFragmentTexture:t0 atIndex:0];
    if (t1) [enc setFragmentTexture:t1 atIndex:1];
    if (t2) [enc setFragmentTexture:t2 atIndex:2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    return MR_OK;
}
static int encode_smaa(mr_shared *s, id<MTLTexture> src, id<MTLTexture> dst) {
    end_pending_encoder(s);
    if (ensure_command_buffer(s)) return MR_ERR_DEVICE;
    /* Edges are discarded where there are none, so that target starts clear. */
    if (smaa_pass(s, g_post.smaa_edges, g_post.smaa_edges_texture, 1, src, nil, nil) ||
        smaa_pass(s, g_post.smaa_weights, g_post.smaa_weights_texture, 1, g_post.smaa_edges_texture, g_post.smaa_area, g_post.smaa_search) ||
        smaa_pass(s, g_post.smaa_blend, dst, 0, src, g_post.smaa_weights_texture, nil)) return MR_ERR_DEVICE;
    return MR_OK;
}

/* ---- FXAA (the existing pass, from any source texture) ------------------- */
static int encode_fxaa(mr_shared *s, id<MTLTexture> src, id<MTLTexture> dst, float subpix) {
    id<MTLRenderPipelineState> p = fxaa_pipeline(s, dst.pixelFormat);
    if (!p) return MR_ERR_UNSUPPORTED;
    end_pending_encoder(s);
    if (ensure_command_buffer(s)) return MR_ERR_DEVICE;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> enc = timing_render_encoder(s, rp, MR_GPU_POST);
    if (!enc) { g_err = "FXAA encoder allocation failed"; return MR_ERR_DEVICE; }
    float parameters[4] = { 1.f / (float)src.width, 1.f / (float)src.height, subpix, 0.f };
    [enc setRenderPipelineState:p];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentSamplerState:s->samplers[3] atIndex:0];
    [enc setFragmentBytes:parameters length:sizeof parameters atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];
    return MR_OK;
}

/* ---- MetalFX ------------------------------------------------------------- */
static Class metalfx_descriptor_class(void) {
    static int looked; static Class found;
    if (!looked) {
        looked = 1;
        /* Linked by the app; loaded here when a probe or test did not link it. */
        found = NSClassFromString(@"MTLFXSpatialScalerDescriptor");
        if (!found) {
            dlopen("/System/Library/Frameworks/MetalFX.framework/MetalFX", RTLD_LAZY);
            found = NSClassFromString(@"MTLFXSpatialScalerDescriptor");
        }
    }
    return found;
}
static void metalfx_request(mr_shared *s, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h) {
    os_unfair_lock_lock(&g_post.lock);
    int same = g_post.metalfx_in_w == in_w && g_post.metalfx_in_h == in_h &&
               g_post.metalfx_out_w == out_w && g_post.metalfx_out_h == out_h &&
               g_post.metalfx_state != MR_POST_NOT_REQUESTED;
    if (same) { os_unfair_lock_unlock(&g_post.lock); return; }
    g_post.metalfx_in_w = in_w; g_post.metalfx_in_h = in_h; g_post.metalfx_out_w = out_w; g_post.metalfx_out_h = out_h;
    g_post.metalfx_scaler = nil;
    uint64_t generation = ++g_post.metalfx_generation;
    Class descriptor = metalfx_descriptor_class();
    int supported = descriptor && [descriptor respondsToSelector:@selector(supportsDevice:)] &&
                    ((BOOL (*)(id, SEL, id))objc_msgSend)(descriptor, @selector(supportsDevice:), s->dev);
    g_post.metalfx_state = supported ? MR_POST_PREPARING : MR_POST_UNAVAILABLE;
    os_unfair_lock_unlock(&g_post.lock);
    if (!supported) { post_detail("MetalFX spatial scaling is unavailable on this device"); return; }
    id<MTLDevice> device = s->dev;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        @autoreleasepool {
        MTLFXSpatialScalerDescriptor *d = [[descriptor alloc] init];
        d.colorTextureFormat = MTLPixelFormatBGRA8Unorm; d.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
        d.inputWidth = in_w; d.inputHeight = in_h; d.outputWidth = out_w; d.outputHeight = out_h;
        /* The layers hold gamma-encoded bytes (the presenter's sRGB view
         * decodes them): perceptual processing, as MetalFX defines it. */
        d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
        id<MTLFXSpatialScaler> scaler = [d newSpatialScalerWithDevice:device];
        os_unfair_lock_lock(&g_post.lock);
        if (generation == g_post.metalfx_generation) {
            g_post.metalfx_scaler = scaler;
            g_post.metalfx_state = scaler ? MR_POST_READY : MR_POST_UNAVAILABLE;
            if (scaler) g_post.metalfx_output_usage |= (unsigned long)scaler.outputTextureUsage;
        }
        os_unfair_lock_unlock(&g_post.lock);
        if (!scaler) post_detail("MetalFX could not build a %ux%u to %ux%u scaler", in_w, in_h, out_w, out_h);
        }
    });
}
static id metalfx_scaler_for(uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h) {
    os_unfair_lock_lock(&g_post.lock);
    id scaler = g_post.metalfx_state == MR_POST_READY && g_post.metalfx_in_w == in_w && g_post.metalfx_in_h == in_h &&
                g_post.metalfx_out_w == out_w && g_post.metalfx_out_h == out_h ? g_post.metalfx_scaler : nil;
    os_unfair_lock_unlock(&g_post.lock);
    return scaler;
}
static int encode_metalfx(mr_shared *s, id scaler, id<MTLTexture> src, id<MTLTexture> dst) {
    id<MTLFXSpatialScaler> fx = scaler;
    if ((src.usage & fx.colorTextureUsage) != fx.colorTextureUsage || (dst.usage & fx.outputTextureUsage) != fx.outputTextureUsage)
        return MR_ERR_UNSUPPORTED;
    end_pending_encoder(s);
    if (ensure_command_buffer(s)) return MR_ERR_DEVICE;
    timing_marker(s, src, MR_GPU_POST, 0);
    fx.colorTexture = src; fx.outputTexture = dst;
    fx.inputContentWidth = src.width; fx.inputContentHeight = src.height;
    [fx encodeToCommandBuffer:s->pending_cb];
    fx.colorTexture = nil; fx.outputTexture = nil;
    timing_marker(s, dst, MR_GPU_POST, 1);
    return MR_OK;
}

/* ---- Publication ----------------------------------------------------------- */
static id<MTLTexture> post_intermediate(mr_shared *s, int index, NSUInteger w, NSUInteger h) {
    id<MTLTexture> t = g_post.intermediate[index];
    if (t && t.width == w && t.height == h) return t;
    @autoreleasepool {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; td.storageMode = MTLStorageModePrivate;
    t = [s->dev newTextureWithDescriptor:td];
    if (!t) { g_err = "scene intermediate allocation failed"; return nil; }
    t.label = @"Halo scene post intermediate";
    /* Encoded passes retain the texture they used; replacing it is safe. */
    g_post.intermediate[index] = t;
    return t;
    }
}
unsigned long mr_scene_destination_usage(int upscale) {
    unsigned long usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    if (upscale == MR_SCENE_UPSCALE_METALFX) {
        os_unfair_lock_lock(&g_post.lock); usage |= g_post.metalfx_output_usage; os_unfair_lock_unlock(&g_post.lock);
    }
    return usage;
}
void mr_post_prepare(int aa, int upscale, int in_w, int in_h, int out_w, int out_h) {
    mr_shared *s = shared_state(); if (!s) return;
    if (aa == MR_SCENE_AA_SMAA) smaa_request(s);
    if (upscale == MR_SCENE_UPSCALE_METALFX && in_w > 0 && in_h > 0 && out_w > 0 && out_h > 0 &&
        (out_w != in_w || out_h != in_h))
        metalfx_request(s, (uint32_t)in_w, (uint32_t)in_h, (uint32_t)out_w, (uint32_t)out_h);
}

int mr_publish_scene_layer(mr_context *c, void *mtl_texture, mr_scene_publish *p) {
    @autoreleasepool {
    if (!c || !mtl_texture || !c->target || !p) { g_err = "bad publication arguments"; return MR_ERR_ARGS; }
    mr_shared *s = c->s;
    id<MTLTexture> dst = (__bridge id<MTLTexture>)mtl_texture;
    p->aa_effective = MR_SCENE_AA_OFF; p->upscale_effective = MR_SCENE_UPSCALE_NONE; p->foveation_resolved = 0;
    if (c->pw != c->w || c->ph != c->h || dst.pixelFormat != c->target.pixelFormat ||
        !(dst.usage & MTLTextureUsageRenderTarget) || dst.width < (NSUInteger)c->w || dst.height < (NSUInteger)c->h) {
        set_errorf("scene layer %lux%lu/%lu cannot take %dx%d", (unsigned long)dst.width, (unsigned long)dst.height,
                   (unsigned long)dst.pixelFormat, c->w, c->h);
        return MR_ERR_ARGS;
    }
    int upscale = dst.width != (NSUInteger)c->w || dst.height != (NSUInteger)c->h;
    int physical = c->layout[MR_ATTACH_COLOR] == MR_LAYOUT_PHYSICAL && c->layout_map;
    /* 1. Screen-space source. */
    id<MTLTexture> source = context_screen_color(c);
    if (!source) return MR_ERR_DEVICE;
    p->foveation_resolved = physical;
    /* 2. Anti-aliasing at the engine's size, into the destination when it is
     * the last step, else into the intermediate. */
    int aa = p->aa;
    if (aa == MR_SCENE_AA_SMAA) {
        smaa_request(s);
        if (!smaa_ready_for(s, (uint32_t)c->w, (uint32_t)c->h, MTLPixelFormatBGRA8Unorm)) aa = MR_SCENE_AA_FXAA;
    }
    id<MTLTexture> aa_out = upscale ? (aa != MR_SCENE_AA_OFF ? post_intermediate(s, 0, c->w, c->h) : source) : dst;
    if (!aa_out) return MR_ERR_DEVICE;
    if (aa == MR_SCENE_AA_SMAA) {
        if (encode_smaa(s, source, aa_out)) return MR_ERR_DEVICE;
        p->aa_effective = MR_SCENE_AA_SMAA;
    } else if (aa == MR_SCENE_AA_FXAA) {
        float subpix = p->fxaa_subpix >= 0.f && p->fxaa_subpix <= 1.f ? p->fxaa_subpix : 0.5f;
        int status = encode_fxaa(s, source, aa_out, subpix);
        if (status == MR_ERR_DEVICE) return status;
        if (status == MR_OK) p->aa_effective = MR_SCENE_AA_FXAA;
        else if (upscale) aa_out = source;
        else aa = MR_SCENE_AA_OFF;
    }
    if (!upscale) {
        if (p->aa_effective != MR_SCENE_AA_OFF) return MR_OK;
        /* Plain copy of the screen-space colour. */
        end_pending_encoder(s);
        if (ensure_command_buffer(s)) return MR_ERR_DEVICE;
        id<MTLBlitCommandEncoder> blit = timing_blit_encoder(s, MR_GPU_COPY);
        if (!blit) { g_err = "blit encoder allocation failed"; return MR_ERR_DEVICE; }
        [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake((NSUInteger)c->w, (NSUInteger)c->h, 1)
                    toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        return MR_OK;
    }
    /* 3. Upscale. */
    if (p->upscale == MR_SCENE_UPSCALE_METALFX) {
        metalfx_request(s, (uint32_t)c->w, (uint32_t)c->h, (uint32_t)dst.width, (uint32_t)dst.height);
        id scaler = metalfx_scaler_for((uint32_t)c->w, (uint32_t)c->h, (uint32_t)dst.width, (uint32_t)dst.height);
        if (scaler) {
            int status = encode_metalfx(s, scaler, aa_out, dst);
            if (status == MR_ERR_DEVICE) return status;
            if (status == MR_OK) { p->upscale_effective = MR_SCENE_UPSCALE_METALFX; return MR_OK; }
            post_detail("MetalFX texture usage mismatch; bilinear used");
        }
    }
    if (encode_convert(s, aa_out, dst, NULL, NULL, MR_GPU_POST)) return MR_ERR_DEVICE;
    p->upscale_effective = MR_SCENE_UPSCALE_BILINEAR;
    return MR_OK;
    }
}

int mr_publish_overlay_layer(mr_context *c, void *mtl_texture) {
    @autoreleasepool {
    if (!c || !mtl_texture || !c->target) { g_err = "bad overlay publication arguments"; return MR_ERR_ARGS; }
    id<MTLTexture> dst = (__bridge id<MTLTexture>)mtl_texture;
    if (dst.width == (NSUInteger)c->pw && dst.height == (NSUInteger)c->ph && dst.pixelFormat == c->target.pixelFormat &&
        c->layout[MR_ATTACH_COLOR] != MR_LAYOUT_PHYSICAL)
        return mr_blit_target_to(c, mtl_texture);
    mr_shared *s = c->s;
    if (!(dst.usage & MTLTextureUsageRenderTarget) || dst.pixelFormat != MTLPixelFormatBGRA8Unorm) { g_err = "overlay layer is not a BGRA8 render target"; return MR_ERR_ARGS; }
    id<MTLTexture> source = c->pw == c->w && c->ph == c->h ? context_screen_color(c) : c->target;
    if (!source) return MR_ERR_DEVICE;
    encode_context_clears(c);
    if (s->pending_encoder && s->pending_target == c) end_pending_encoder(s);
    return encode_convert(s, source, dst, NULL, NULL, MR_GPU_COPY);
    }
}
