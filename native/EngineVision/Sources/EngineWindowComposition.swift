import Metal

/// The window's forward picture from the same leased panorama used immersively.
/// This encoder owns no engine state or texture lifetime; the caller keeps the
/// panorama lease until its command buffer completes.
final class EngineWindowComposition {
    private let worldPipeline: MTLRenderPipelineState
    private let hudPipeline: MTLRenderPipelineState

    init(device: MTLDevice, format: MTLPixelFormat) throws {
        let library = try device.makeLibrary(source: Self.shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "window_world_vertex")
        descriptor.fragmentFunction = library.makeFunction(name: "window_world_fragment")
        descriptor.colorAttachments[0].pixelFormat = format
        worldPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        descriptor.fragmentFunction = library.makeFunction(name: "window_hud_fragment")
        descriptor.colorAttachments[0].isBlendingEnabled = true
        descriptor.colorAttachments[0].sourceRGBBlendFactor = .one
        descriptor.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
        descriptor.colorAttachments[0].sourceAlphaBlendFactor = .one
        descriptor.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
        hudPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    }

    private struct Uniforms {
        var viewport: SIMD4<Float>
        var aspect: SIMD2<Float>
        var texelSize: SIMD2<Float>
    }

    /// Preserve Halo's full image aspect and cinematic viewport. Only the world
    /// is clipped to its valid source rectangle; the HUD retains its full raster.
    func encode(_ encoder: MTLRenderCommandEncoder, world: MTLTexture, hud: MTLTexture,
                viewport: SIMD4<Float>, drawableSize: SIMD2<Float>) {
        guard drawableSize.x.isFinite, drawableSize.y.isFinite, drawableSize.x > 0, drawableSize.y > 0,
              viewport.x.isFinite, viewport.y.isFinite, viewport.z.isFinite, viewport.w.isFinite,
              viewport.x >= 0, viewport.y >= 0, viewport.z <= 1, viewport.w <= 1,
              viewport.z > viewport.x, viewport.w > viewport.y else { return }
        var uniforms = Uniforms(viewport: viewport,
                                aspect: SIMD2(Float(world.width) / Float(world.height), drawableSize.x / drawableSize.y),
                                texelSize: SIMD2(1 / Float(world.width), 1 / Float(world.height)))
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 0)
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 0)
        encoder.setRenderPipelineState(worldPipeline)
        encoder.setFragmentTexture(world, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.setRenderPipelineState(hudPipeline)
        encoder.setFragmentTexture(hud, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
    }

    private static let shader = """
    #include <metal_stdlib>
    using namespace metal;
    struct WindowUniforms { float4 viewport; float2 aspect; float2 texelSize; };
    struct WindowRaster { float4 position [[position]]; float2 uv; };
    vertex WindowRaster window_world_vertex(uint id [[vertex_id]], constant WindowUniforms &u [[buffer(0)]]) {
        const float2 positions[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
        const float2 uv[3] = { float2(0,1), float2(2,1), float2(0,-1) };
        float2 p = positions[id];
        if (u.aspect.y > u.aspect.x) p.x *= u.aspect.x / u.aspect.y;
        else p.y *= u.aspect.y / u.aspect.x;
        return { float4(p, 0, 1), uv[id] };
    }
    fragment float4 window_world_fragment(WindowRaster in [[stage_in]], texture2d<float> world [[texture(0)]],
                                          constant WindowUniforms &u [[buffer(0)]]) {
        // The oversized triangle extends beyond the aspect-fitted image.
        // Leave those letterbox pixels at the render pass's clear colour.
        if (any(in.uv < 0.0) || any(in.uv > 1.0)) discard_fragment();
        // Clamp by half a texel inside the actual world viewport, as the
        // immersive panels do. Cinematic bars stay black instead of sampling
        // clear/stale pixels or stretching a reduced viewport over the window.
        if (any(in.uv < u.viewport.xy) || any(in.uv > u.viewport.zw)) return float4(0,0,0,1);
        constexpr sampler sample(filter::linear, address::clamp_to_edge);
        float2 middle = (u.viewport.xy + u.viewport.zw) * 0.5;
        float2 lo = min(u.viewport.xy + u.texelSize * 0.5, middle);
        float2 hi = max(u.viewport.zw - u.texelSize * 0.5, middle);
        return world.sample(sample, clamp(in.uv, lo, hi));
    }
    fragment float4 window_hud_fragment(WindowRaster in [[stage_in]], texture2d<float> hud [[texture(0)]]) {
        if (any(in.uv < 0.0) || any(in.uv > 1.0)) discard_fragment();
        constexpr sampler sample(filter::linear, address::clamp_to_edge);
        float4 picture = hud.sample(sample, in.uv);
        // Same premultiplied sRGB/additive decoding as the immersive HUD.
        // Zero-alpha colour is Halo's additive reticle/glow, not transparency.
        float cover = max(picture.a, max(picture.r, max(picture.g, picture.b)));
        float3 straight = cover > 0.0005 ? picture.rgb / cover : float3(0.0);
        float3 linear = select(pow((straight + 0.055) / 1.055, 2.4), straight / 12.92, straight <= 0.04045);
        return float4(linear * cover, picture.a);
    }
    """
}
