import Foundation
import simd

/// The host's composite model (panorama_foveation.h, C) against the
/// presenter's own (EngineImmersiveScreenGeometry.panelAlphas): for many
/// directions over the sphere, every panel's final weight must agree, so
/// the rate maps lower only what the presenter really never shows.
@main struct FoveationGeometryValidation {
    static func main() {
        var input = halo_foveation_test_dense()
        let px = withUnsafeBytes(of: input.projection_x) { Array($0.bindMemory(to: Float.self)) }
        let py = withUnsafeBytes(of: input.projection_y) { Array($0.bindMemory(to: Float.self)) }
        let order = [0, 1, 2, 7, 8, 9, 5, 6]   // the presenter's source order, by layer
        let sources = EngineImmersiveScreenGeometry.panoramaSources(
            projections: order.map { SIMD2(px[$0], py[$0]) }, viewports: order.map { _ in SIMD4<Float>(0, 0, 1, 1) })
        var generator = SystemRandomNumberGenerator()
        var worst: Float = 0, compared = 0, hidden = 0
        for _ in 0..<20000 {
            let z = Float.random(in: -1...1, using: &generator), a = Float.random(in: 0..<(2 * .pi), using: &generator)
            let r = (1 - z * z).squareRoot()
            let d = SIMD3<Float>(r * cos(a), z, r * sin(a))
            let alphas = EngineImmersiveScreenGeometry.panelAlphas(sources: sources, direction: d)
            for position in 0..<8 {
                var swift = alphas[position]
                for later in (position + 1)..<8 { swift *= 1 - alphas[later] }
                let c = halo_foveation_test_weight(&input, Int32(position), d.x, d.y, d.z)
                worst = max(worst, abs(swift - c)); compared += 1
                if c < 1 / 255 && swift < 1 / 255 { hidden += 1 }
            }
        }
        precondition(worst < 2e-4, "C and Swift composite weights differ by \(worst)")
        print("PASS foveation geometry: \(compared) panel weights over the sphere agree with the presenter's (worst \(worst)); \(hidden) hidden in both")
    }
}
