// Template for tools/test_window_gpu_frame.py. The production draw body and
// flat shader are inserted; the world/HUD encoder is compiled unchanged.
import Foundation
import Metal

let HALO_PANORAMA_CENTRE_LEFT = 1

final class FixtureFrame {
    let texture: MTLTexture?
    let view: MTLTexture?
    var sequence: UInt64
    let copiedSequence: UInt64
    var refreshes = 0
    init(_ texture: MTLTexture?, sequence: UInt64 = 1, copiedSequence: UInt64 = 1) {
        self.texture = texture; self.view = texture?.makeTextureView(pixelFormat: .bgra8Unorm_srgb)
        self.sequence = sequence; self.copiedSequence = copiedSequence
    }
    func refresh(expectedSequence: UInt64? = nil) -> Bool {
        refreshes += 1
        guard expectedSequence == nil || expectedSequence == copiedSequence else { return false }
        sequence = copiedSequence; return true
    }
}

struct FixtureSnapshot {
    let layers: [MTLTexture]
    struct Projections {
        var rect = SIMD4<Float>(0, 0, 1, 1)
        func viewport(_ layer: Int) -> SIMD4<Float> { precondition(layer == 1); return rect }
    }
    var projections = Projections()
    func view(eye: Int, sector: Int) -> MTLTexture {
        precondition(eye == 0 && sector == 1)
        return layers[sector].makeTextureView(pixelFormat: .bgra8Unorm_srgb)!
    }
    var hud: MTLTexture { layers[3] }
}

final class FixturePanorama: @unchecked Sendable {
    var snapshot: FixtureSnapshot?
    var allowsFlatFallback = false
    var latestFlatSequence: UInt64 = 1
    var acquisitions = 0
    let released = DispatchSemaphore(value: 0)
    func refresh(leasedTo command: MTLCommandBuffer) -> FixtureSnapshot? {
        guard let snapshot else { return nil }
        acquisitions += 1
        command.addCompletedHandler { [self] _ in released.signal() }
        return snapshot
    }
}

final class OffscreenDrawable {
    let texture: MTLTexture
    init(_ texture: MTLTexture) { self.texture = texture }
}

final class OffscreenView {
    var currentRenderPassDescriptor: MTLRenderPassDescriptor?
    var currentDrawable: OffscreenDrawable?
    let drawableSize = CGSize(width: 32, height: 16)
    var submitted: MTLCommandBuffer?
    init(device: MTLDevice) {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm_srgb, width: 32, height: 16, mipmapped: false)
        descriptor.storageMode = .shared; descriptor.usage = [.renderTarget]
        let texture = device.makeTexture(descriptor: descriptor)!
        let poison = Array(repeating: [UInt8](arrayLiteral: 255, 0, 255, 255), count: 512).flatMap { $0 }
        poison.withUnsafeBytes { texture.replace(region: MTLRegionMake2D(0, 0, 32, 16), mipmapLevel: 0,
                                                 withBytes: $0.baseAddress!, bytesPerRow: 128) }
        currentDrawable = OffscreenDrawable(texture)
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = texture
        // Poisoned .load/.dontCare verify that production installs clear/store.
        pass.colorAttachments[0].loadAction = .load
        pass.colorAttachments[0].storeAction = .dontCare
        pass.colorAttachments[0].clearColor = __CLEAR__
        currentRenderPassDescriptor = pass
    }
}

final class Renderer {
    var queue: MTLCommandQueue?
    var pipeline: MTLRenderPipelineState?
    var frame: FixtureFrame?
    var panorama: FixturePanorama?
    var windowComposition: EngineWindowComposition?
    func draw(in view: OffscreenView) {
// __DRAW__
    }
    static let shader = """
// __SHADER__
    """
}

@main struct WindowFrameGPUValidation {
    static func main() throws {
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
            fputs("Mac Metal device unavailable\n", stderr); exit(2)
        }
        let renderer = Renderer()
        renderer.queue = queue
        renderer.windowComposition = try EngineWindowComposition(device: device, format: .bgra8Unorm_srgb)
        let library = try device.makeLibrary(source: Renderer.shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "window_vertex")
        descriptor.fragmentFunction = library.makeFunction(name: "frame_fragment")
        descriptor.colorAttachments[0].pixelFormat = .bgra8Unorm_srgb
        renderer.pipeline = try device.makeRenderPipelineState(descriptor: descriptor)

        func texture(_ pixel: [UInt8], image: [[UInt8]]? = nil) -> MTLTexture {
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: 8, height: 8, mipmapped: false)
            d.storageMode = .shared; d.usage = [.shaderRead, .pixelFormatView]
            let t = device.makeTexture(descriptor: d)!
            let bytes = (image ?? Array(repeating: pixel, count: 64)).flatMap { $0 }
            bytes.withUnsafeBytes { t.replace(region: MTLRegionMake2D(0, 0, 8, 8), mipmapLevel: 0,
                                              withBytes: $0.baseAddress!, bytesPerRow: 32) }
            return t
        }
        func encode(_ linear: Double) -> UInt8 {
            let v = min(1, max(0, linear))
            return UInt8(((v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1 / 2.4) - 0.055) * 255).rounded())
        }
        func decode(_ byte: UInt8) -> Double {
            let v = Double(byte) / 255
            return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4)
        }
        func blended(_ world: [UInt8], _ hud: [UInt8]) -> [UInt8] {
            let alpha = Double(hud[3]) / 255
            let cover = Double(hud.max()!) / 255
            let channels = (0..<3).map { i -> UInt8 in
                let straight = cover > 0.0005 ? Double(hud[i]) / 255 / cover : 0
                let linear = straight <= 0.04045 ? straight / 12.92 : pow((straight + 0.055) / 1.055, 2.4)
                return encode(linear * cover + decode(world[i]) * (1 - alpha))
            }
            return channels + [255]
        }
        let clear = __CLEAR__
        let clearPixel = [encode(clear.blue), encode(clear.green), encode(clear.red), UInt8(255)]
        let worldA: [UInt8] = [40, 80, 120, 255], worldB: [UInt8] = [160, 100, 60, 255]
        let staleCPU = texture([0, 0, 255, 255]), transparent = texture([0, 0, 0, 0])
        let decoy = texture([255, 0, 255, 255])
        let panorama = FixturePanorama(); renderer.panorama = panorama
        renderer.frame = FixtureFrame(staleCPU)
        func snapshot(_ world: MTLTexture, _ hud: MTLTexture, rect: SIMD4<Float> = SIMD4(0, 0, 1, 1)) -> FixtureSnapshot {
            FixtureSnapshot(layers: [decoy, world, decoy, hud], projections: .init(rect: rect))
        }
        var results = [[String: Any]]()
        func draw(_ name: String, expectations: [(Int, Int, [UInt8])], flatRefreshes: Int = 0) {
            let before = renderer.frame?.refreshes ?? 0, leased = panorama.snapshot != nil
            let view = OffscreenView(device: device)
            renderer.draw(in: view)
            view.submitted?.waitUntilCompleted()
            let releaseOK = !leased || panorama.released.wait(timeout: .now() + 2) == .success
            let target = view.currentDrawable!.texture
            var bytes = [UInt8](repeating: 0, count: 32 * 16 * 4)
            bytes.withUnsafeMutableBytes { target.getBytes($0.baseAddress!, bytesPerRow: 128,
                                                           from: MTLRegionMake2D(0, 0, 32, 16), mipmapLevel: 0) }
            let pixelsOK = expectations.allSatisfy { x, y, expected in
                let start = (y * 32 + x) * 4
                return (0..<4).allSatisfy { abs(Int(bytes[start + $0]) - Int(expected[$0])) <= 2 }
            }
            let actual = expectations.map { x, y, _ in Array(bytes[((y * 32 + x) * 4)..<((y * 32 + x) * 4 + 4)]) }
            let refreshOK = (renderer.frame?.refreshes ?? 0) - before == flatRefreshes
            let passed = pixelsOK && refreshOK && releaseOK && view.submitted?.status == .completed
            results.append(["case": name, "passed": passed, "actual_pixels": actual, "lease_completed": releaseOK,
                            "cpu_refreshes": (renderer.frame?.refreshes ?? 0) - before])
        }
        panorama.snapshot = snapshot(texture(worldA), transparent)
        draw("live_world_ignores_stale_cpu", expectations: [(16, 8, worldA), (0, 8, clearPixel)])
        panorama.snapshot = snapshot(texture(worldB), transparent)
        draw("next_gpu_world_advances", expectations: [(16, 8, worldB), (31, 8, clearPixel)])
        let premult: [UInt8] = [64, 32, 16, 128]
        panorama.snapshot = snapshot(texture(worldA), texture(premult))
        draw("premultiplied_hud_linear_blend", expectations: [(16, 8, blended(worldA, premult))])
        let additive: [UInt8] = [0, 0, 64, 0]
        panorama.snapshot = snapshot(texture(worldA), texture(additive))
        draw("zero_alpha_additive_reticle", expectations: [(16, 8, blended(worldA, additive))])
        var letterbox = Array(repeating: [UInt8](arrayLiteral: 255, 0, 255, 255), count: 64)
        for y in 2..<6 { for x in 0..<8 { letterbox[y * 8 + x] = worldA } }
        var hud = Array(repeating: [UInt8](arrayLiteral: 0, 0, 0, 0), count: 64)
        for x in 0..<8 { hud[x] = [0, 255, 0, 255] }
        panorama.snapshot = snapshot(texture(worldA, image: letterbox), texture([0, 0, 0, 0], image: hud), rect: SIMD4(0, 0.25, 1, 0.75))
        draw("cinematic_viewport_and_full_hud", expectations: [(16, 0, [0, 255, 0, 255]), (16, 3, [0, 0, 0, 255]),
                                                              (16, 4, worldA), (16, 11, worldA), (16, 14, [0, 0, 0, 255])])
        panorama.snapshot = nil
        draw("unavailable_world_clears_without_cpu_fallback", expectations: [(16, 8, clearPixel), (0, 0, clearPixel)])
        panorama.allowsFlatFallback = true; panorama.latestFlatSequence = 7
        renderer.frame = FixtureFrame(texture(worldB), sequence: 1, copiedSequence: 7)
        draw("confirmed_flat_menu", expectations: [(16, 8, worldB), (0, 8, clearPixel)], flatRefreshes: 1)
        panorama.latestFlatSequence = 8
        draw("flat_publication_race_clears", expectations: [(16, 8, clearPixel)], flatRefreshes: 1)
        // A CPU-retained panorama must not hide a newer genuine flat menu.
        panorama.snapshot = snapshot(texture(worldA), transparent); panorama.latestFlatSequence = 7
        draw("flat_supersedes_retained_world", expectations: [(16, 8, worldB)], flatRefreshes: 1)
        panorama.allowsFlatFallback = false; renderer.windowComposition = nil
        draw("missing_world_pipeline_clears_and_releases", expectations: [(16, 8, clearPixel)])
        let passed = results.allSatisfy { $0["passed"] as? Bool == true }
        let report: [String: Any] = ["passed": passed, "cases": results, "device": device.name]
        let json = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(json); print("")
        exit(passed ? 0 : 1)
    }
}
