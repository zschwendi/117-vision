import Foundation
import simd

/// A scope image and its original HUD are one completed engine render. A
/// current zoom flag alone cannot identify the pixels in an older flat frame.
struct EngineSenseScopePresentation {
    struct Identity: Equatable {
        var session, generation, mode: UInt64
        var weapon: UInt32
        var zoom: Int32
    }
    private var sequence: UInt64 = 0
    private var firstSeen: Double = -.infinity
    private var observed: Identity?
    private var lastTime: Double = -.infinity

    mutating func accepts(sequence next: UInt64, frame: Identity?, current: Identity?, now: Double) -> Bool {
        guard now.isFinite, now >= 0, now >= lastTime else { return false }
        lastTime = now
        if next > sequence { sequence = next; firstSeen = now; observed = frame }
        guard next == sequence, frame == observed else { return false }
        guard next > 0, let frame, let current, frame == current,
              current.session != 0, current.generation != 0, current.mode != 0,
              current.weapon != UInt32.max, (0...3).contains(current.zoom),
              now >= firstSeen, now - firstSeen <= 0.25 else { return false }
        return true
    }

    /// Head-local, square-pixel scope panel. Both eyes see the same original
    /// image at one stable vergence depth. Their real projection and eye offset
    /// are still applied; copying identical NDC coordinates would be wrong for
    /// the headset's asymmetric frusta. No separate reticle or HUD shadow.
    static func vertices(aspect: Float) -> [EngineImmersiveScreenVertex] {
        let ratio = aspect.isFinite && aspect > 0 ? aspect : 4 / 3
        let depth: Float = 3.1
        let y = depth * tan(26 * Float.pi / 180)
        let x = y * ratio
        let corners: [(SIMD4<Float>, SIMD2<Float>)] = [
            (SIMD4(-x, y, -depth, 1), SIMD2(0, 0)),
            (SIMD4(-x, -y, -depth, 1), SIMD2(0, 1)),
            (SIMD4(x, y, -depth, 1), SIMD2(1, 0)),
            (SIMD4(x, -y, -depth, 1), SIMD2(1, 1))
        ]
        return [0, 1, 2, 2, 1, 3].map {
            EngineImmersiveScreenVertex(position: corners[$0].0, uv: corners[$0].1)
        }
    }
    static func eyeTransform(projection: simd_float4x4, headFromEye: simd_float4x4) -> simd_float4x4 {
        projection * simd_inverse(headFromEye)
    }
}
