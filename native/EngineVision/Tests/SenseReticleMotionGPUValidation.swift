// Actual-Metal multi-frame witness for the tracked reticle in motion.
//
// Replays a configured cadence, not a measured one: one accepted source frame
// (weapon hit point plus the original HUD art) per 30 Hz engine frame, shown
// for three 90 Hz compositor frames. It uses the production render-target
// setup (EngineImmersiveRenderTargets.makePass) with pooled outputs reused
// every third frame, the shipped centre panel mesh with curve_vertex/
// curve_fragment and their depth, the shipped reticle vertex stage
// (curve_vertex in the baseline, sense_reticle_vertex in the candidate) and
// sense_reticle_fragment with production blending and overlay depth, thin
// original-style additive art in three art slots cleared and copied as
// mr_blit_target_region_to does, both eyes independently, 48 display frames:
//
//  A  gun sweep, head still: 30 deg/s yaw, hit alternating 2 m / 150 m;
//  B1 gun still, head translating (late head position, no rotation);
//  B2 gun still, head rotating (late head orientation, no translation);
//  C  detector control: scenario A with the output loaded, not cleared, and
//     no world drawn over it, so retained footprints must be detected.
//
// The output pitch (20 degrees over 768 pixels, 0.026 deg) is close to the
// headset's, so a one-texel original stroke (0.0365 deg) is magnified about
// 1.4x as it is there. At a coarser pitch the stroke is minified without mips
// and whole rows of it fall between samples, which is a fixture artifact.
//
// The aim anchor is located from the two strokes themselves: the horizontal
// arm's centre line gives y and the vertical arm's gives x, so a footprint
// that perspective stretches unevenly cannot pull it. The per-frame CSV is
// written after each scenario, before any assertion, and the worst case of
// each scenario is printed.
//
// Usage: SenseReticleMotionGPUValidation <EngineImmersive.swift> [csv path]
// Build with EngineImmersiveRenderTargets.swift, EngineImmersiveScreenGeometry
// .swift and EngineSenseReticleGeometry.swift from the tree under test; add
// -D RETICLE_CANDIDATE for a tree with the panel-mapped reticle.
import Foundation
import Metal
import simd

final class MeshIndex {
    let mesh: (vertices: [EngineImmersiveScreenVertex], indices: [UInt32])
    var buckets = [[Int]](repeating: [], count: 128 * 128)
    init(_ mesh: (vertices: [EngineImmersiveScreenVertex], indices: [UInt32])) {
        self.mesh = mesh
        func bucket(_ x: Float) -> Int { min(127, max(0, Int((x * 128).rounded(.down)))) }
        for t in stride(from: 0, to: mesh.indices.count - 2, by: 3) {
            let uvs = (0..<3).map { mesh.vertices[Int(mesh.indices[t + $0])].uv }
            let lo = uvs.reduce(SIMD2<Float>(repeating: 1), { simd_min($0, $1) })
            let hi = uvs.reduce(SIMD2<Float>(repeating: 0), { simd_max($0, $1) })
            for y in bucket(lo.y)...bucket(hi.y) { for x in bucket(lo.x)...bucket(hi.x) { buckets[y * 128 + x].append(t) } }
        }
    }
    func locate(_ uv: SIMD2<Float>) -> SIMD3<Float>? {
        guard uv.x >= 0, uv.x <= 1, uv.y >= 0, uv.y <= 1 else { return nil }
        let x = min(127, Int(uv.x * 128)), y = min(127, Int(uv.y * 128))
        for t in buckets[y * 128 + x] {
            let a = mesh.vertices[Int(mesh.indices[t])], b = mesh.vertices[Int(mesh.indices[t + 1])], c = mesh.vertices[Int(mesh.indices[t + 2])]
            let v0 = b.uv - a.uv, v1 = c.uv - a.uv, v2 = uv - a.uv
            let den = v0.x * v1.y - v1.x * v0.y
            guard abs(den) > 1e-14 else { continue }
            let wb = (v2.x * v1.y - v1.x * v2.y) / den, wc = (v0.x * v2.y - v2.x * v0.y) / den, wa = 1 - wb - wc
            if wa >= -1e-5 && wb >= -1e-5 && wc >= -1e-5 {
                return wa * simd_make_float3(a.position) + wb * simd_make_float3(b.position) + wc * simd_make_float3(c.position)
            }
        }
        return nil
    }
}

struct Head {
    var position: SIMD3<Float>
    var rotation: simd_quatf
    func eye(_ k: Int) -> SIMD3<Float> { position + rotation.act(SIMD3(k == 0 ? -0.0315 : 0.0315, 0, 0)) }
}

struct Measure {
    var expected = SIMD2<Float>(repeating: 0)  // px, this frame's accepted source point as drawn
    var anchor = SIMD2<Float>(repeating: -1)   // px, from the two strokes' centre lines
    var anchorError: Float = 0                 // px
    var worldErrorDegrees: Float = 0           // anchor ray vs the ray to where the world shows the target
    var outside: Float = 0                     // fraction of band energy outside the current footprint
    var span: Float = 0                        // measured / expected horizontal arm length
    var depthDistance: Float = 0               // metres implied by the depth under the aimed pixel (0: none)
    var strokeWeights = SIMD2<Float>(repeating: 0)
    var historyPixels = 0                      // band pixels whose colour differs from a freshly cleared render of this frame
    var historyDifference: Float = 0           // largest such colour difference
}

@main struct SenseReticleMotionGPUValidation {
    typealias G = EngineSenseReticleGeometry
    typealias S = EngineImmersiveScreenGeometry
    static let size = 768, frames = 48, band = 60
    static let fov: Float = 20 * .pi / 180
    static let near: Float = 0.05
    static let focal = 1 / tan(fov / 2)
    static let pixelTangent = 2 * tan(fov / 2) / Float(size)
    static let artTangent = 2 * tan(G.verticalHalfAngle) / 1080   // one art pixel
    static let arm = 24                                           // art pixels each side of the anchor
    static let armPixels = Float(arm) * artTangent / pixelTangent
    static let separation: Float = 0.0103346457 * 3.048

    static func perspective() -> simd_float4x4 {
        // Reversed depth, as CompositorServices: clear 0, nearer is greater.
        simd_float4x4(columns: (SIMD4<Float>(focal, 0, 0, 0), SIMD4<Float>(0, focal, 0, 0),
                                SIMD4<Float>(0, 0, 0, -1), SIMD4<Float>(0, 0, near, 0)))
    }
    static func view(_ head: Head, _ k: Int) -> simd_float4x4 {
        var m = simd_float4x4(head.rotation)
        m.columns.3 = SIMD4(head.eye(k), 1)
        return m.inverse
    }
    static func project(_ p: SIMD3<Float>, _ m: simd_float4x4) -> SIMD2<Float> {
        let c = m * SIMD4(p, 1)
        return SIMD2((c.x / c.w + 1) / 2 * Float(size), (1 - c.y / c.w) / 2 * Float(size))
    }
    /// Angle between the eye ray through a pixel and the ray to a point.
    static func degrees(pixel: SIMD2<Float>, point: SIMD3<Float>, view: simd_float4x4) -> Float {
        let ndc = SIMD2(pixel.x / Float(size) * 2 - 1, 1 - pixel.y / Float(size) * 2)
        let ray = simd_normalize(SIMD3(ndc.x / focal, ndc.y / focal, -1))
        let p = simd_normalize(simd_make_float3(view * SIMD4(point, 1)))
        return atan2(simd_length(simd_cross(ray, p)), simd_dot(ray, p)) * 180 / .pi
    }
    static func direction(yaw: Float, pitch: Float) -> SIMD3<Float> {
        let y = yaw * .pi / 180, p = pitch * .pi / 180
        return SIMD3(sin(y) * cos(p), sin(p), -cos(y) * cos(p))
    }
    static func halo(_ c: SIMD3<Float>) -> SIMD3<Float> { SIMD3(-c.z, -c.x, c.y) }
    static func half(_ h: UInt16) -> Float {
        let sign: Float = (h & 0x8000) != 0 ? -1 : 1
        let e = Int((h >> 10) & 0x1F), m = Float(h & 0x3FF)
        if e == 0 { return sign * m * pow(2, -24) }
        if e == 31 { return m == 0 ? sign * .infinity : .nan }
        return sign * (1 + m / 1024) * pow(2, Float(e - 15))
    }

    static func main() throws {
        let source = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
        let shader = source.components(separatedBy: "private static let shader = \"\"\"")[1].components(separatedBy: "\"\"\"")[0]
        let csvPath = CommandLine.arguments.count > 2 ? CommandLine.arguments[2]
            : (NSTemporaryDirectory() as NSString).appendingPathComponent("reticle-motion.csv")
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { print("SKIP no Metal"); exit(77) }
        let library = try device.makeLibrary(source: shader, options: nil)
        // The shipped reticle vertex stage: per-eye vertices where the tree has
        // them, otherwise the shared curve vertex the baseline draws it with.
        let perEyeVertices = shader.contains("vertex Raster sense_reticle_vertex")
        let worldDescriptor = MTLRenderPipelineDescriptor()
        worldDescriptor.vertexFunction = library.makeFunction(name: "curve_vertex")
        worldDescriptor.fragmentFunction = library.makeFunction(name: "curve_fragment")
        worldDescriptor.colorAttachments[0].pixelFormat = .rgba16Float
        worldDescriptor.depthAttachmentPixelFormat = .depth32Float
        let worldPipeline = try device.makeRenderPipelineState(descriptor: worldDescriptor)
        let reticleDescriptor = MTLRenderPipelineDescriptor()
        reticleDescriptor.vertexFunction = library.makeFunction(name: perEyeVertices ? "sense_reticle_vertex" : "curve_vertex")
        reticleDescriptor.fragmentFunction = library.makeFunction(name: "sense_reticle_fragment")
        reticleDescriptor.colorAttachments[0].pixelFormat = .rgba16Float
        reticleDescriptor.depthAttachmentPixelFormat = .depth32Float
        let blend = reticleDescriptor.colorAttachments[0]!
        blend.isBlendingEnabled = true
        blend.sourceRGBBlendFactor = .one; blend.destinationRGBBlendFactor = .oneMinusSourceAlpha
        blend.sourceAlphaBlendFactor = .one; blend.destinationAlphaBlendFactor = .oneMinusSourceAlpha
        let reticlePipeline = try device.makeRenderPipelineState(descriptor: reticleDescriptor)
        let worldDepthDescriptor = MTLDepthStencilDescriptor()
        worldDepthDescriptor.depthCompareFunction = .always; worldDepthDescriptor.isDepthWriteEnabled = true
        let overlayDepthDescriptor = MTLDepthStencilDescriptor()
        overlayDepthDescriptor.depthCompareFunction = .always; overlayDepthDescriptor.isDepthWriteEnabled = false
        let worldDepth = device.makeDepthStencilState(descriptor: worldDepthDescriptor)!
        let overlayDepth = device.makeDepthStencilState(descriptor: overlayDepthDescriptor)!

        // The world: the shipped centre panel mesh, a black picture, its own depth.
        let bandProjection = SIMD2<Float>(1 / tan(32 * .pi / 180), 1 / tan(52.5 * .pi / 180))
        let cap = SIMD2<Float>(1 / tan(52.5 * .pi / 180), 1 / tan(52.5 * .pi / 180))
        let sources = S.panoramaSources(projections: Array(repeating: bandProjection, count: 6) + [cap, cap],
                                        viewports: Array(repeating: SIMD4<Float>(0, 0, 1, 1), count: 8))
        let meshes = S.sphericalPanorama(sources: sources, texelSize: SIMD2(1.0 / 1536, 1.0 / 1536))
        let centre = MeshIndex(meshes[1])
        let meshVertices = device.makeBuffer(bytes: meshes[1].vertices, length: meshes[1].vertices.count * MemoryLayout<EngineImmersiveScreenVertex>.stride)!
        let meshIndices = device.makeBuffer(bytes: meshes[1].indices, length: meshes[1].indices.count * 4)!
        let pictureDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: 64, height: 64, mipmapped: false)
        pictureDescriptor.usage = .shaderRead; pictureDescriptor.storageMode = .shared
        let picture = device.makeTexture(descriptor: pictureDescriptor)!
        let black = [UInt32](repeating: 0xFF000000, count: 64 * 64)
        black.withUnsafeBytes { picture.replace(region: MTLRegionMake2D(0, 0, 64, 64), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 256) }

        // Original-style thin additive crosshair: one-texel arms, zero alpha,
        // centred on texel 256 (continuous 256.5), the capture's anchor in a 512 crop.
        let artSize = 512   // HALO_SENSE_RETICLE_ART_SIZE
        let stagingDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: artSize, height: artSize, mipmapped: false)
        stagingDescriptor.usage = .shaderRead; stagingDescriptor.storageMode = .shared
        let staging = device.makeTexture(descriptor: stagingDescriptor)!
        var art = [UInt32](repeating: 0, count: artSize * artSize)
        for d in -arm...arm { art[256 * artSize + 256 + d] = 0x00FFFFFF; art[(256 + d) * artSize + 256] = 0x00FFFFFF }
        art.withUnsafeBytes { staging.replace(region: MTLRegionMake2D(0, 0, artSize, artSize), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: artSize * 4) }
        let slotDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: artSize, height: artSize, mipmapped: false)
        slotDescriptor.usage = [.shaderRead, .renderTarget]; slotDescriptor.storageMode = .private
        let slots = (0..<3).map { _ in device.makeTexture(descriptor: slotDescriptor)! }

        // Pooled outputs reused every third display frame, per eye.
        let colorDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba16Float, width: size, height: size, mipmapped: false)
        colorDescriptor.usage = [.renderTarget, .shaderRead]; colorDescriptor.storageMode = .shared
        let depthDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .depth32Float, width: size, height: size, mipmapped: false)
        depthDescriptor.usage = .renderTarget; depthDescriptor.storageMode = .private
        let colors = (0..<6).map { _ in device.makeTexture(descriptor: colorDescriptor)! }
        let depths = (0..<6).map { _ in device.makeTexture(descriptor: depthDescriptor)! }
        let depthReadback = device.makeBuffer(length: 4, options: .storageModeShared)!
        let referenceColor = device.makeTexture(descriptor: colorDescriptor)!
        let referenceDepth = device.makeTexture(descriptor: depthDescriptor)!

        let bounds = SIMD4<Float>(703, 283, 1215, 795), anchor = SIMD2<Float>(959.5, 539.5)
        let ua = (anchor.x - bounds.x) / (bounds.z - bounds.x), va = (anchor.y - bounds.y) / (bounds.w - bounds.y)
        let sourceEyes = [SIMD3<Float>(0, separation, 0), SIMD3<Float>(0, -separation, 0)]
        let still = Head(position: .zero, rotation: simd_quatf(angle: 0, axis: SIMD3(0, 1, 0)))

        func publish(_ slot: Int) {
            // As mr_blit_target_region_to: clear the destination, then copy.
            let command = queue.makeCommandBuffer()!
            let pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = slots[slot]; pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0); pass.colorAttachments[0].storeAction = .store
            command.makeRenderCommandEncoder(descriptor: pass)!.endEncoding()
            let blit = command.makeBlitCommandEncoder()!
            blit.copy(from: staging, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: 0, y: 0, z: 0),
                      sourceSize: MTLSize(width: artSize, height: artSize, depth: 1), to: slots[slot],
                      destinationSlice: 0, destinationLevel: 0, destinationOrigin: MTLOrigin(x: 0, y: 0, z: 0))
            blit.endEncoding(); command.commit(); command.waitUntilCompleted()
            precondition(command.status == .completed, "art publication failed")
        }

        /// Draws one eye of one display frame and measures it.
        func frame(output: Int, eye k: Int, head: Head, slot: Int, target: SIMD3<Float>, load: Bool, world: Bool,
                   candidate: Bool) throws -> Measure {
            let eyeView = view(head, k), matrix = perspective() * eyeView
            let sample = G.Sample(point: halo(target), hand: 1, bounds: bounds, anchor: anchor, viewport: SIMD2(1920, 1080))
            // The earlier placement: one quad round the true hit, at the head.
            let legacy = G.vertices(samples: [sample], headPosition: head.position)
            precondition(legacy.count == 6, "true-depth quad not produced")
            var expected = target + head.position
            var vertices = perEyeVertices ? legacy.flatMap { [$0, $0] } : legacy
            #if RETICLE_CANDIDATE
            if candidate {
                vertices = G.panelVertices(samples: [sample], sources: sources, sourceEyes: sourceEyes)
                precondition(!vertices.isEmpty && vertices.count % 12 == 0, "panel-mapped reticle not produced")
                let mine = stride(from: k, to: vertices.count, by: 2).map { vertices[$0] }
                let h = mine.first { abs($0.uv.x - ua) < 1e-6 && abs($0.uv.y - va) < 1e-6 }!.position
                expected = simd_make_float3(h) / h.w
            }
            #else
            precondition(!candidate, "panel mapping needs -D RETICLE_CANDIDATE")
            #endif
            var m = Measure()
            m.expected = project(expected, matrix)
            let px = min(size - 1, max(0, Int(m.expected.x))), py = min(size - 1, max(0, Int(m.expected.y)))
            // One complete compositor pass for this eye into the given target.
            func encode(color: MTLTexture, depth: MTLTexture, load: Bool) throws {
                let pass = try EngineImmersiveRenderTargets.makePass(device: device, color: color, depth: depth, sampleCount: 1)
                if load { pass.colorAttachments[0].loadAction = .load }
                let command = queue.makeCommandBuffer()!
                let encoder = command.makeRenderCommandEncoder(descriptor: pass)!
                encoder.setCullMode(.none)
                var matrices = [matrix], base = UInt32(k), filter: UInt32 = 0
                if world {
                    encoder.setRenderPipelineState(worldPipeline); encoder.setDepthStencilState(worldDepth)
                    encoder.setVertexBuffer(meshVertices, offset: 0, index: 0)
                    encoder.setVertexBytes(&matrices, length: MemoryLayout<simd_float4x4>.stride, index: 1)
                    encoder.setVertexBytes(&base, length: 4, index: 2)
                    encoder.setFragmentTexture(picture, index: 0); encoder.setFragmentTexture(picture, index: 1)
                    encoder.setFragmentBytes(&filter, length: 4, index: 1)
                    encoder.drawIndexedPrimitives(type: .triangle, indexCount: meshes[1].indices.count, indexType: .uint32,
                                                  indexBuffer: meshIndices, indexBufferOffset: 0)
                }
                encoder.setRenderPipelineState(reticlePipeline); encoder.setDepthStencilState(overlayDepth)
                // As the compositor: one buffer per frame (the panel grid can pass 4 KB).
                let reticleBuffer = vertices.withUnsafeBytes {
                    device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared)!
                }
                encoder.setVertexBuffer(reticleBuffer, offset: 0, index: 0)
                encoder.setVertexBytes(&matrices, length: MemoryLayout<simd_float4x4>.stride, index: 1)
                encoder.setVertexBytes(&base, length: 4, index: 2)
                encoder.setFragmentTexture(slots[slot], index: 0); encoder.setFragmentTexture(slots[slot], index: 1)
                encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: perEyeVertices ? vertices.count / 2 : vertices.count)
                encoder.endEncoding()
                let blit = command.makeBlitCommandEncoder()!
                blit.copy(from: depth, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x: px, y: py, z: 0),
                          sourceSize: MTLSize(width: 1, height: 1, depth: 1), to: depthReadback, destinationOffset: 0,
                          destinationBytesPerRow: 4, destinationBytesPerImage: 4)
                blit.endEncoding(); command.commit(); command.waitUntilCompleted()
                precondition(command.status == .completed, "GPU frame failed")
            }
            // The horizontal band holding this footprint and the sweep's earlier ones.
            let top = max(0, py - band), bottom = min(size, py + band + 1), rows = bottom - top
            func readBand(_ color: MTLTexture) -> [UInt16] {
                var raw = [UInt16](repeating: 0, count: size * rows * 4)
                raw.withUnsafeMutableBytes { color.getBytes($0.baseAddress!, bytesPerRow: size * 8, from: MTLRegionMake2D(0, top, size, rows), mipmapLevel: 0) }
                return raw
            }
            // The same frame drawn into a target used for nothing else and
            // cleared as production clears: what this frame alone draws.
            try encode(color: referenceColor, depth: referenceDepth, load: false)
            let reference = readBand(referenceColor)
            // The frame as replayed into the pooled output.
            try encode(color: colors[output * 2 + k], depth: depths[output * 2 + k], load: load)
            let raw = readBand(colors[output * 2 + k])
            var differing = 0, difference: Float = 0
            for p in stride(from: 0, to: raw.count, by: 4) {
                var same = true
                for c in 0..<3 where raw[p + c] != reference[p + c] {
                    same = false; difference = max(difference, abs(half(raw[p + c]) - half(reference[p + c])))
                }
                if !same { differing += 1 }
            }
            m.historyPixels = differing; m.historyDifference = difference
            let e = m.expected, box = armPixels * 1.05 + 5
            var total: Float = 0, outside: Float = 0
            var hWeight: Float = 0, hY: Float = 0, hReach: Float = 0, vWeight: Float = 0, vX: Float = 0
            for r in 0..<rows { for x in 0..<size {
                let i = (r * size + x) * 4
                let value = half(raw[i]) + half(raw[i + 1]) + half(raw[i + 2])
                guard value > 0 else { continue }
                total += value
                let p = SIMD2(Float(x) + 0.5, Float(top + r) + 0.5), d = p - e
                if abs(d.x) > box || abs(d.y) > box { outside += value; continue }
                // Each stroke away from the crossing gives one coordinate.
                if abs(d.y) <= 4 && abs(d.x) > 4 { hWeight += value; hY += value * p.y; hReach += value * abs(d.x) }
                if abs(d.x) <= 4 && abs(d.y) > 4 { vWeight += value; vX += value * p.x }
            } }
            m.outside = total > 0 ? outside / total : 1
            m.strokeWeights = SIMD2(hWeight, vWeight)
            if hWeight > 0 && vWeight > 0 { m.anchor = SIMD2(vX / vWeight, hY / hWeight) }
            m.anchorError = simd_distance(m.anchor, e)
            // A uniform arm over 4 < |dx| <= L has mean |dx| = (L + 4) / 2.
            m.span = hWeight > 0 ? (2 * hReach / hWeight - 4) / armPixels : 0
            // Where the world shows the target (centre panel, this eye's source camera).
            let camera = SIMD3<Float>(k == 0 ? -separation : separation, 0, 0)
            if let image = S.imagePoint(of: sources[1], simd_normalize(target - camera)),
               let shown = centre.locate(SIMD2(0.5 + image.x / 2, 0.5 + image.y / 2)) {
                m.worldErrorDegrees = degrees(pixel: m.anchor, point: shown, view: eyeView)
            } else { m.worldErrorDegrees = .nan }
            let depth = depthReadback.contents().load(as: Float.self)
            m.depthDistance = depth > 0 ? near / depth : 0
            return m
        }

        var csv = "scenario,placement,frame,source,slot,age_display_frames,eye,expected_x,expected_y,anchor_x,anchor_y,anchor_error_px,world_error_deg,outside_fraction,span_ratio,depth_distance_m,hit_distance_m,h_weight,v_weight,history_pixels,history_difference\n"
        let tree = perEyeVertices ? "candidate-shader" : "baseline-shader"
        func run(_ name: String, candidate: Bool, load: Bool, world: Bool,
                 head: (Int) -> Head, target: (Int) -> SIMD3<Float>) throws -> [[Measure]] {
            var results: [[Measure]] = []
            var published = -1
            let placement = candidate ? "panel" : "true-depth-quad(\(tree))"
            for i in 0..<frames {
                let j = i / 3, slot = j % 3       // configured: 30 Hz source, 90 Hz display
                if j != published { publish(slot); published = j }
                var pair: [Measure] = []
                for k in 0..<2 {
                    let m = try frame(output: i % 3, eye: k, head: head(i), slot: slot, target: target(j),
                                      load: load, world: world, candidate: candidate)
                    pair.append(m)
                    csv += "\(name),\(placement),\(i),\(j),\(slot),\(i - 3 * j),\(k),\(m.expected.x),\(m.expected.y),\(m.anchor.x),\(m.anchor.y),\(m.anchorError),\(m.worldErrorDegrees),\(m.outside),\(m.span),\(m.depthDistance),\(simd_length(target(j))),\(m.strokeWeights.x),\(m.strokeWeights.y),\(m.historyPixels),\(m.historyDifference)\n"
                }
                results.append(pair)
            }
            // Written before any assertion on this scenario.
            try csv.write(toFile: csvPath, atomically: true, encoding: .utf8)
            var worst = (0, 0)
            for i in 0..<frames { for k in 0..<2 where results[i][k].anchorError > results[worst.0][worst.1].anchorError { worst = (i, k) } }
            let w = results[worst.0][worst.1]
            print("[\(name) \(placement)] worst anchor: frame \(worst.0) source \(worst.0 / 3) eye \(worst.1) expected \(w.expected) measured \(w.anchor) error \(w.anchorError) px, outside \(w.outside), strokes \(w.strokeWeights), span \(w.span), history \(w.historyPixels) px")
            return results
        }
        // A: gun sweeps 30 deg/s (1 deg per source frame), head still; hit alternates near/far.
        let sweep: (Int) -> SIMD3<Float> = { j in (j % 2 == 0 ? 2 : 150) * direction(yaw: -7.5 + Float(j), pitch: 1) }
        // B: gun still at 150 m or 2 m; B1 head translating 0.3 m/s, B2 head yawing 10 deg/s.
        let fixedFar: (Int) -> SIMD3<Float> = { _ in 150 * direction(yaw: 0, pitch: 1) }
        let fixedNear: (Int) -> SIMD3<Float> = { _ in 2 * direction(yaw: 0, pitch: 1) }
        let translating: (Int) -> Head = { i in Head(position: SIMD3(0.3 * Float(i) / 90, 0, 0), rotation: still.rotation) }
        let rotating: (Int) -> Head = { i in Head(position: .zero, rotation: simd_quatf(angle: -10 * .pi / 180 * Float(i) / 90, axis: SIMD3(0, 1, 0))) }

        var placements = [false]
        #if RETICLE_CANDIDATE
        placements.append(true)
        #endif
        var summary: [String] = []
        var failures: [String] = []
        func check(_ ok: Bool, _ message: String) { if !ok { failures.append(message); print("FAIL \(message)") } }
        for candidate in placements {
            let label = candidate ? "panel-mapped" : "true-depth quad (\(tree))"
            let a = try run("A_gun_sweep_head_still", candidate: candidate, load: false, world: true, head: { _ in still }, target: sweep)
            var maxOutside: Float = 0, maxAnchor: Float = 0, maxHoldMove: Float = 0, minStep: Float = .infinity
            var spanRange = (Float.infinity, -Float.infinity), worldRange = (Float.infinity, -Float.infinity), history = 0
            for i in 0..<frames { for k in 0..<2 {
                let m = a[i][k]
                history = max(history, m.historyPixels)
                maxOutside = max(maxOutside, m.outside); maxAnchor = max(maxAnchor, m.anchorError)
                worldRange = (min(worldRange.0, m.worldErrorDegrees), max(worldRange.1, m.worldErrorDegrees))
                spanRange = (min(spanRange.0, m.span), max(spanRange.1, m.span))
                if i > 0 {
                    let move = simd_distance(m.anchor, a[i - 1][k].anchor)
                    if i % 3 == 0 { minStep = min(minStep, move) } else { maxHoldMove = max(maxHoldMove, move) }
                }
            } }
            check(history == 0, "\(label) A: pooled output differs from a freshly cleared render of the same frame (\(history) px)")
            check(maxOutside < 1e-3, "\(label) A: reticle energy outside the current accepted footprint (max fraction \(maxOutside))")
            check(maxAnchor < 0.35, "\(label) A: drawn anchor off this frame's accepted source point (max \(maxAnchor) px)")
            check(maxHoldMove < 0.05, "\(label) A: drawn anchor moved between source frames (\(maxHoldMove) px)")
            check(spanRange.0 > 0.85 && spanRange.1 < 1.15, "\(label) A: original angular span not kept near and far (\(spanRange))")
            summary.append(String(format: "%@ A gun sweep, head still: %ld px differ from a freshly cleared render of the same frame; outside-footprint energy max %.2e; anchor vs accepted source max %.3f px; drift within a source frame %.3f px, smallest step at a new source frame %.2f px; span ratio %.3f-%.3f; anchor vs world-shown target %.3f-%.3f deg",
                                  label as NSString, history, Double(maxOutside), Double(maxAnchor), Double(maxHoldMove), Double(minStep),
                                  Double(spanRange.0), Double(spanRange.1), Double(worldRange.0), Double(worldRange.1)))
            for (name, head, target) in [("B1_head_translating_gun_far", translating, fixedFar),
                                         ("B1_head_translating_gun_near", translating, fixedNear),
                                         ("B2_head_rotating_gun_far", rotating, fixedFar)] {
                let b = try run(name, candidate: candidate, load: false, world: true, head: head, target: target)
                var range = (Float.infinity, -Float.infinity), outsideMax: Float = 0, anchorMax: Float = 0, depthSeen: Float = 0, history = 0
                for i in 0..<frames { for k in 0..<2 {
                    history = max(history, b[i][k].historyPixels)
                    range = (min(range.0, b[i][k].worldErrorDegrees), max(range.1, b[i][k].worldErrorDegrees))
                    outsideMax = max(outsideMax, b[i][k].outside); anchorMax = max(anchorMax, b[i][k].anchorError)
                    depthSeen = b[i][k].depthDistance
                } }
                check(history == 0, "\(label) \(name): pooled output differs from a freshly cleared render (\(history) px)")
                check(outsideMax < 1e-3, "\(label) \(name): energy outside the current footprint (\(outsideMax))")
                check(anchorMax < 0.35, "\(label) \(name): drawn anchor off its own placement (\(anchorMax) px)")
                if candidate { check(range.1 < 0.01, "\(label) \(name): reticle off the world-shown target (\(range.1) deg)") }
                if name.hasPrefix("B2") { check(range.1 - range.0 < 0.01, "\(label) \(name): head rotation alone moved the reticle against the world (\(range))") }
                summary.append(String(format: "%@ %@: reticle vs world-shown target %.3f-%.3f deg over 48 frames; depth under the aimed pixel %.2f m (reticle placed for %.1f m; it writes no depth)",
                                      label as NSString, name as NSString, Double(range.0), Double(range.1), Double(depthSeen), Double(simd_length(target(0)))))
            }
        }
        // C: the same sweep without clearing and without the world over it.
        let c = try run("C_detector_control_load_no_world", candidate: false, load: true, world: false, head: { _ in still }, target: sweep)
        // Every pooled output has been drawn before from frame 3 on, so each
        // later frame must differ from its cleared reference on both eyes.
        let detected = (3..<frames).filter { i in c[i][0].historyPixels > 0 && c[i][1].historyPixels > 0 }.count
        let footprintDetected = (3..<frames).filter { i in c[i][0].outside > 0.05 && c[i][1].outside > 0.05 }.count
        check(detected == frames - 3, "control C: retained pixels not detected against the cleared reference (\(detected) of \(frames - 3))")
        summary.append("control C (load, no world): retained pixels detected against the cleared reference in \(detected) of \(frames - 3) frames; by the footprint measure in \(footprintDetected)")
        for line in summary { print(line) }
        print("CSV: \(csvPath)")
        precondition(failures.isEmpty, "\(failures.count) check(s) failed; see FAIL lines and the CSV")
        print("PASS \(tree): moving-reticle replay, \(frames) display frames x 2 eyes per scenario (configured 30 Hz source at 90 Hz), 3 pooled outputs, 3 art slots. Excludes retained pixels/ghosts in this replay only; it does not measure the shipped build's timing.")
    }
}
