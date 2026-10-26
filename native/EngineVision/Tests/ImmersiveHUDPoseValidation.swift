import Foundation
import simd

@main struct ImmersiveHUDPoseValidation {
    static var checks = 0
    static func require(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        precondition(value(), message)
    }
    static func pose(yaw: Float = 0, pitch: Float = 0, roll: Float = 0,
                     position: SIMD3<Float> = .zero) -> simd_float4x4 {
        var value = simd_float4x4(simd_quatf(angle: yaw, axis: SIMD3(0, 1, 0)) *
            simd_quatf(angle: pitch, axis: SIMD3(1, 0, 0)) * simd_quatf(angle: roll, axis: SIMD3(0, 0, 1)))
        value.columns.3 = SIMD4(position, 1)
        return value
    }
    static func angle(_ a: simd_float4x4, _ b: simd_float4x4) -> Float {
        2 * acos(min(1, abs(simd_dot(simd_normalize(simd_quatf(a)).vector, simd_normalize(simd_quatf(b)).vector))))
    }
    static func near(_ a: simd_float4x4, _ b: simd_float4x4) -> Bool {
        (0..<4).allSatisfy { simd_length(a[$0] - b[$0]) < 0.0001 }
    }
    static func main() {
        let degrees: Float = .pi / 180
        var hud = EngineImmersiveHUDPose()
        let first = pose(yaw: 25 * degrees, pitch: 40 * degrees, roll: 12 * degrees,
                         position: SIMD3(0.2, -0.4, 0.1))
        require(near(hud.update(neutralFromHead: first, timestamp: 1, recenter: 0)!, first),
                "HUD starts at current full head orientation, including reclining")
        let turned = pose(yaw: 29 * degrees, pitch: 40 * degrees, roll: 12 * degrees,
                          position: SIMD3(0.5, -0.6, 0.2))
        let following = hud.update(neutralFromHead: turned, timestamp: 1.01, recenter: 0)!
        require(angle(following, first) > 0.001 && angle(following, turned) > 0.001,
                "Normal head motion settles instead of locking instantly or remaining world-frozen")
        require(following.columns.3 == turned.columns.3, "Leaning retains correct visor depth without translation lag")
        require(near(hud.update(neutralFromHead: turned, timestamp: 1.01, recenter: 0)!, following),
                "Duplicate drawable times do not apply smoothing twice")
        var settled = following
        for i in 2...100 { settled = hud.update(neutralFromHead: turned, timestamp: 1 + Double(i) / 100, recenter: 0)! }
        require(angle(settled, turned) < 0.3 * degrees, "HUD settles within subdegree deadband")
        let jitter = pose(yaw: 29.05 * degrees, pitch: 40 * degrees, roll: 12 * degrees, position: SIMD3(0.5, -0.6, 0.2))
        let quiet = hud.update(neutralFromHead: jitter, timestamp: 2.01, recenter: 0)!
        require(angle(quiet, settled) < 0.001, "Tiny tracking noise does not shake the text")

        let fast = pose(yaw: 150 * degrees, pitch: 0.2, roll: -0.1)
        let caught = hud.update(neutralFromHead: fast, timestamp: 2.02, recenter: 0)!
        require(angle(caught, fast) <= 10.01 * degrees, "Rapid physical turn cannot leave HUD behind the user")
        require(EngineImmersiveOrientation.isRigidPose(caught), "Quaternion smoothing preserves rigid stereo geometry")
        let reset = hud.update(neutralFromHead: first, timestamp: 2.03, recenter: 1)!
        require(near(reset, first), "Recenter resets filter immediately")
        require(near(hud.update(neutralFromHead: turned, timestamp: 3, recenter: 1)!, turned),
                "Tracking gap cannot drag stale HUD pose into resumed scene")
        require(near(hud.update(neutralFromHead: first, timestamp: 2, recenter: 1)!, first),
                "Clock discontinuity reanchors instead of extrapolating")
        var invalid = first; invalid.columns.0.x = .nan
        require(hud.update(neutralFromHead: invalid, timestamp: 4, recenter: 1) == nil,
                "Nonfinite tracking is rejected")
        require(near(hud.update(neutralFromHead: turned, timestamp: 4.01, recenter: 1)!, turned),
                "Tracking recovery starts at present head")
        require(hud.update(neutralFromHead: first, timestamp: .nan, recenter: 1) == nil,
                "Invalid time cannot poison filter")

        var wrap = EngineImmersiveHUDPose()
        _ = wrap.update(neutralFromHead: pose(yaw: 179 * degrees), timestamp: 1, recenter: 0)
        let wrapped = wrap.update(neutralFromHead: pose(yaw: -179 * degrees), timestamp: 1.01, recenter: 0)!
        require(angle(wrapped, pose(yaw: .pi)) < 1.1 * degrees,
                "Full 360-degree physical turning crosses the angle boundary on the short arc")

        // The compositor's HUD transform cancels any virtual body turn while
        // preserving each eye's actual position. A weapon reticle deliberately
        // continues using the world transform without this HUD multiplication.
        let virtualTurn = pose(yaw: 1.4)
        var eyes = [matrix_identity_float4x4, matrix_identity_float4x4]
        eyes[0].columns.3.x = -0.032; eyes[1].columns.3.x = 0.032
        for eye in eyes {
            let base = simd_inverse(turned * eye) * settled
            let spun = simd_inverse(virtualTurn * turned * eye) * virtualTurn * settled
            require(near(base, spun), "Virtual stick/snap turns do not push HUD away from face")
            require(!near(simd_inverse(turned * eye), base), "Weapon and HUD transforms remain separate")
        }
        require(abs(simd_length(simd_make_float3(eyes[0].columns.3 - eyes[1].columns.3)) - 0.064) < 0.00001,
                "Real binocular eye separation remains intact")

        func atRate(_ rate: Int) -> simd_float4x4 {
            var filter = EngineImmersiveHUDPose()
            _ = filter.update(neutralFromHead: pose(), timestamp: 0, recenter: 0)
            var last = pose()
            for i in 1...rate { last = filter.update(neutralFromHead: pose(yaw: 0.1), timestamp: Double(i) / Double(rate), recenter: 0)! }
            return last
        }
        require(angle(atRate(60), atRate(90)) < 0.1 * degrees && angle(atRate(90), atRate(120)) < 0.1 * degrees,
                "HUD settling stays consistent across headset frame rates")
        print("PASS \(checks) HUD pose checks: smoothed head-follow, bounds, stereo, 360 wrap, recenter, tracking loss, virtual turn cancellation")
    }
}
