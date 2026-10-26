import Foundation
import simd

/// A readable visor overlay follows physical head rotation with a short,
/// bounded settling time. Filter in calibrated physical space: virtual turns
/// must move the viewer and HUD together, never drag the overlay off screen.
/// This transform is presentation-only and never changes a weapon or shot.
struct EngineImmersiveHUDPose {
    private var rotation: simd_quatf?
    private var lastTime: Double?
    private var recenter: Int?

    mutating func reset() {
        rotation = nil
        lastTime = nil
        recenter = nil
    }

    mutating func update(neutralFromHead: simd_float4x4, timestamp: Double,
                         recenter nextRecenter: Int) -> simd_float4x4? {
        guard timestamp.isFinite, timestamp >= 0,
              EngineImmersiveOrientation.isRigidPose(neutralFromHead) else {
            reset()
            return nil
        }
        let target = simd_normalize(simd_quatf(neutralFromHead))
        let elapsed = lastTime.map { timestamp - $0 }
        if rotation == nil || recenter != nextRecenter || elapsed == nil ||
            elapsed! < 0 || elapsed! > 0.25 {
            rotation = target
        } else if let elapsed, elapsed > 0, let current = rotation {
            // Shortest-path interpolation is continuous across +/-180°.
            let angle = 2 * acos(min(1, abs(simd_dot(current.vector, target.vector))))
            let deadband: Float = 0.25 * .pi / 180
            if angle > deadband {
                let settle = Float(1 - exp(-elapsed / 0.10))
                // Fast turns must keep the visor within 10° of the eyes.
                let catchUp = max(0, 1 - (10 * Float.pi / 180) / angle)
                rotation = simd_normalize(simd_slerp(current, target, max(settle, catchUp)))
            }
        }
        lastTime = timestamp
        recenter = nextRecenter
        var result = simd_float4x4(rotation!)
        // Preserve eye-relative depth while leaning. Translational lag would
        // make nearby UI swim and introduce unnecessary stereo disparity.
        result.columns.3 = neutralFromHead.columns.3
        return result
    }
}
