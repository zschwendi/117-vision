import Foundation
import simd

@main struct SenseThrowMotionValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(condition(), message)
    }
    static func main() {
        let source = EngineSenseThrowMotion.Source(accessory: .init(accessoryID: UUID(), generation: 1),
            controllerGeneration: 1, recenter: 0)
        func sample(_ time: Double, _ position: SIMD3<Float>, _ owner: EngineSenseThrowMotion.Source? = nil) -> EngineSenseThrowMotion.Sample {
            .init(source: owner ?? source, timestamp: time, position: position)
        }
        let identity = matrix_identity_float4x4
        for fps in [30, 60, 90, 120] {
            var motion = EngineSenseThrowMotion()
            for frame in 0..<20 {
                let t = Double(frame) / Double(fps)
                let v = motion.update(sample(t, SIMD3(0, Float(t), -Float(t)*2)), at: t, panoramaFromOrigin: identity)
                if frame >= 3 { require(v != nil && simd_distance(v!, SIMD3(2,0,1)) < 0.0001, "physical throw at \(fps)fps") }
            }
        }
        var motion = EngineSenseThrowMotion()
        _ = motion.update(sample(0, .zero), at: 0, panoramaFromOrigin: identity)
        let forward = sample(0.025, SIMD3(0,0,-0.05))
        require(motion.update(forward, at: 0.025, panoramaFromOrigin: identity) == SIMD3(2,0,0), "forward in Halo coordinates")
        var translated = identity; translated.columns.3 = SIMD4(100,-100,33,1)
        require(motion.update(forward, at: 0.030, panoramaFromOrigin: translated) == SIMD3(2,0,0), "camera translation cannot inject motion")
        let yaw = simd_float4x4(simd_quatf(angle: .pi/2, axis: SIMD3(0,1,0)))
        let turned = motion.update(forward, at: 0.031, panoramaFromOrigin: yaw)
        require(turned != nil && simd_distance(turned!, SIMD3(0,2,0)) < 0.0001, "virtual turn rotates existing velocity once")
        require(motion.update(forward, at: 0.102, panoramaFromOrigin: identity) == nil, "stale measured sample never refreshed by display query")
        require(motion.update(sample(0.11, .zero), at: 0.11, panoramaFromOrigin: identity) == nil, "stale recovery re-primes")
        require(motion.update(sample(0.135, SIMD3(10,0,0)), at: 0.135, panoramaFromOrigin: identity) == nil, "tracking jump cannot throw")
        require(motion.update(sample(0.135, SIMD3(11,0,0)), at: 0.135, panoramaFromOrigin: identity) == nil, "duplicate timestamp changed position rejected")
        var changed = source; changed.recenter += 1
        require(motion.update(sample(0.16, SIMD3(11,0,0), changed), at: 0.16, panoramaFromOrigin: identity) == nil, "recenter re-primes")
        changed.controllerGeneration += 1
        require(motion.update(sample(0.185, SIMD3(11,0,-0.03), changed), at: 0.185, panoramaFromOrigin: identity) == nil, "new controller re-primes")
        require(motion.update(nil, at: 0.2, panoramaFromOrigin: identity) == nil, "tracking loss resets")
        require(motion.update(sample(0.21, SIMD3(.nan,0,0)), at: 0.21, panoramaFromOrigin: identity) == nil, "invalid position rejected")
        _ = motion.update(sample(0.3, .zero), at: 0.3, panoramaFromOrigin: identity)
        require(motion.update(sample(0.2, .zero), at: 0.3, panoramaFromOrigin: identity) == nil, "old clock sample rejected")
        motion.reset()
        for i in 0..<10 {
            let t = Double(i) / 90
            let rotated = simd_float4x4(simd_quatf(angle: Float(i) * 0.1, axis: SIMD3(0,1,0)))
            let value = motion.update(sample(t, SIMD3(0.3,-0.3,-0.3)), at: t, panoramaFromOrigin: rotated)
            if i > 2 { require(value == .zero, "turning a stationary hand cannot throw") }
        }
        // Root stick contract: panoramaFromNeutral = Ry(-yaw) * Rx(pitch), one rigid
        // transform. Velocity is differentiated in measured origin space first,
        // then rotated by this publication's transform exactly once.
        func virtualView(yaw: Float, pitch: Float) -> simd_float4x4 {
            simd_float4x4(simd_quatf(angle: -yaw, axis: SIMD3(0, 1, 0))) *
                simd_float4x4(simd_quatf(angle: pitch, axis: SIMD3(1, 0, 0)))
        }
        let view = virtualView(yaw: 0.6, pitch: .pi / 4)
        let physical = SIMD3<Float>(0.8, 1.2, -3.0) // m/s in ARKit origin space
        motion.reset()
        _ = motion.update(sample(1.0, .zero), at: 1.0, panoramaFromOrigin: view)
        let viewed = motion.update(sample(1.025, physical * 0.025), at: 1.025, panoramaFromOrigin: view)
        let rotated = view * SIMD4(physical.x, physical.y, physical.z, 0)
        let expected = SIMD3<Float>(-rotated.z, -rotated.x, rotated.y)
        require(viewed != nil && simd_distance(viewed!, expected) < 0.0001, "yaw+pitch applied exactly once to measured velocity")
        require(abs(simd_length(viewed!) - simd_length(physical)) < 0.0001, "virtual view never changes physical speed")
        motion.reset()
        for i in 0..<12 {
            let t = 2.0 + Double(i) / 90
            let moving = virtualView(yaw: Float(i) * 0.2, pitch: Float(i) * 0.07)
            let value = motion.update(sample(t, SIMD3(0.2, -0.4, -0.3)), at: t, panoramaFromOrigin: moving)
            if i > 2 { require(value == .zero, "virtual yaw/pitch of a still hand creates no velocity") }
        }
        // Unsmoothed: a reversal is the window's own finite difference, with no
        // carry-over from the earlier forward motion.
        motion.reset()
        var position = SIMD3<Float>.zero
        for i in 0..<6 { position.z -= 0.03; _ = motion.update(sample(3.0 + Double(i) / 100, position), at: 3.0 + Double(i) / 100, panoramaFromOrigin: identity) }
        for i in 6..<10 { position.z += 0.03; _ = motion.update(sample(3.0 + Double(i) / 100, position), at: 3.0 + Double(i) / 100, panoramaFromOrigin: identity) }
        let reversed = motion.update(sample(3.10, position + SIMD3(0, 0, 0.03)), at: 3.10, panoramaFromOrigin: identity)
        require(reversed != nil && simd_distance(reversed!, SIMD3(-3, 0, 0)) < 0.0001, "reversal reported raw, not averaged")
        print("Sense throw motion: \(checks) checks passed")
    }
}
