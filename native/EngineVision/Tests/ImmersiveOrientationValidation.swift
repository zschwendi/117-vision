import Foundation
import simd

@main struct ImmersiveOrientationValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        precondition(condition(), message)
    }
    static func near(_ a: SIMD3<Float>, _ b: SIMD3<Float>, tolerance: Float = 0.0002) -> Bool {
        simd_length(a - b) < tolerance
    }
    static func near(_ a: simd_float4x4, _ b: simd_float4x4, tolerance: Float = 0.0002) -> Bool {
        (0..<4).allSatisfy { simd_length(a[$0] - b[$0]) < tolerance }
    }
    static func forward(_ m: simd_float4x4) -> SIMD3<Float> {
        simd_make_float3(m * SIMD4<Float>(0, 0, -1, 0))
    }
    static func main() {
        var orientation = EngineImmersiveOrientation()
        let identity = matrix_identity_float4x4
        require(orientation.calibratedOriginFromHead == nil, "No pose is calibrated before tracking")
        let first = orientation.pose(originFromHead: identity, recenter: 0)!
        require(first.didRecenter, "First tracked frame chooses the initial front")
        require(near(first.originFromNeutral, identity) && near(first.neutralFromHead, identity), "Identity preserves the existing front")
        require(!orientation.pose(originFromHead: identity, recenter: 0)!.didRecenter, "Ordinary frames do not keep recentering")

        // Full reclined pitch and roll, with translation: removing only yaw
        // would fail this identity and leave the picture over the viewer.
        let yaw = simd_quatf(angle: 0.65, axis: SIMD3<Float>(0, 1, 0))
        let pitch = simd_quatf(angle: 1.20, axis: SIMD3<Float>(1, 0, 0))
        let roll = simd_quatf(angle: -0.45, axis: SIMD3<Float>(0, 0, 1))
        var reclined = simd_float4x4(yaw * pitch * roll)
        reclined.columns.3 = SIMD4<Float>(2.4, 0.6, -1.1, 1)
        let recentered = orientation.pose(originFromHead: reclined, recenter: 1)!
        require(recentered.didRecenter && orientation.consumedRecenter == 1, "Recenter is consumed by a tracked frame")
        require(near(recentered.originFromNeutral, reclined), "Recenter retains the complete reclined basis")
        require(near(recentered.neutralFromHead, identity), "Reclined head becomes neutral without altering camera/FOV")
        var next = reclined
        next.columns.3 += SIMD4<Float>(0.03, -0.02, 0.01, 0)
        let moving = orientation.pose(originFromHead: next, recenter: 1)!
        require(!moving.didRecenter, "Normal head movement does not move the calibrated front")
        require(near(moving.originFromNeutral, reclined), "Calibration is stable after movement")
        require(near(moving.neutralFromHead, simd_inverse(reclined) * next), "Rendering and head-relative sound share the same pose")

        var offset = EngineImmersiveOrientationOffset()
        offset.nudge(yawDegrees: 30, pitchDegrees: 20)
        let shifted = orientation.pose(originFromHead: reclined, recenter: 1, offset: offset)!
        let radians: Float = .pi / 180
        let expectedForward = SIMD3<Float>(sin(30 * radians) * cos(20 * radians),
                                          sin(20 * radians), -cos(30 * radians) * cos(20 * radians))
        require(near(forward(offset.calibratedFromAdjusted), expectedForward), "Positive yaw/pitch move the picture right/up in calibrated axes")
        require(near(simd_inverse(reclined) * shifted.originFromNeutral, offset.calibratedFromAdjusted), "Nudges respect a reclined viewer's own axes")
        require(near(shifted.neutralFromHead, simd_inverse(offset.calibratedFromAdjusted)), "Head/sound basis is inverse to the visible picture shift")
        let yawOnly = EngineImmersiveOrientationOffset(yawDegrees: 90)
        let pitchOnly = EngineImmersiveOrientationOffset(pitchDegrees: 30)
        require(near(forward(yawOnly.calibratedFromAdjusted), SIMD3<Float>(1, 0, 0)), "Right button means right")
        require(near(forward(pitchOnly.calibratedFromAdjusted), SIMD3<Float>(0, 0.5, -sqrt(0.75))), "Up button means up")
        require(forward(simd_inverse(yawOnly.calibratedFromAdjusted)).x < -0.99, "Sound/head gaze uses the inverse yaw")
        require(forward(simd_inverse(pitchOnly.calibratedFromAdjusted)).y < -0.49, "Sound/head gaze uses the inverse pitch")

        // Reset removes only manual placement; it does not silently recenter
        // the head or reset a current game/controller aim.
        offset = .init()
        let reset = orientation.pose(originFromHead: next, recenter: 1, offset: offset)!
        require(!reset.didRecenter && near(reset.originFromNeutral, reclined), "Reset restores the existing calibration")
        require(near(reset.neutralFromHead, moving.neutralFromHead), "Reset does not change the tracked head pose")

        var bounded = EngineImmersiveOrientationOffset()
        bounded.nudge(yawDegrees: .greatestFiniteMagnitude, pitchDegrees: .greatestFiniteMagnitude)
        require(bounded.yawDegrees == 180 && bounded.pitchDegrees == 80, "Large finite values saturate within comfortable bounds")
        bounded.nudge(yawDegrees: 100, pitchDegrees: 100)
        require(bounded.yawDegrees == 180 && bounded.pitchDegrees == 80, "Repeated presses do not overflow bounds")
        let stable = bounded
        bounded.nudge(yawDegrees: .nan, pitchDegrees: -5)
        require(bounded == stable, "A malformed request is rejected atomically")
        bounded.nudge(yawDegrees: 0, pitchDegrees: .infinity)
        require(bounded == stable, "Infinity never enters a render transform")
        bounded.nudge(yawDegrees: -.greatestFiniteMagnitude, pitchDegrees: -.greatestFiniteMagnitude)
        bounded.nudge(yawDegrees: -.greatestFiniteMagnitude, pitchDegrees: -.greatestFiniteMagnitude)
        require(bounded.yawDegrees == -180 && bounded.pitchDegrees == -80, "Negative limits also saturate")
        require(EngineImmersiveOrientation.isRigidPose(bounded.calibratedFromAdjusted), "Bounded orientation remains finite and rigid")
        require(EngineImmersiveOrientationOffset(yawDegrees: .nan, pitchDegrees: .infinity) == .init(), "Invalid construction uses zero offsets")

        var badPose = next
        badPose.columns.3.x = .nan
        require(orientation.pose(originFromHead: badPose, recenter: 2) == nil, "A nonfinite tracked matrix is rejected")
        require(orientation.consumedRecenter == 1 && near(orientation.calibratedOriginFromHead!, reclined), "Tracking failure leaves a recenter request pending")
        badPose = next; badPose.columns.0 = SIMD4<Float>(0, 0, 0, 0)
        require(orientation.pose(originFromHead: badPose, recenter: 2) == nil, "A singular tracking matrix cannot poison the calibration")
        let restored = orientation.pose(originFromHead: next, recenter: 2)!
        require(restored.didRecenter && near(restored.neutralFromHead, identity), "Next valid tracked frame performs the pending recenter")

        // Both compositor eyes retain their own offset in the same new basis.
        var leftEye = identity, rightEye = identity
        leftEye.columns.3.x = -0.032; rightEye.columns.3.x = 0.032
        let stereo = orientation.pose(originFromHead: next, recenter: 2,
                                      offset: .init(yawDegrees: -25, pitchDegrees: 15))!
        let left = stereo.neutralFromEye(leftEye), right = stereo.neutralFromEye(rightEye)
        require(near(left, stereo.neutralFromOrigin * next * leftEye), "Left eye uses the common adjusted pose")
        require(near(right, stereo.neutralFromOrigin * next * rightEye), "Right eye uses the common adjusted pose")
        require(abs(simd_length(simd_make_float3(right.columns.3 - left.columns.3)) - 0.064) < 0.0002, "Reorientation preserves eye separation")
        require(near(simd_inverse(left) * right, simd_inverse(leftEye) * rightEye), "Reorientation preserves the relative stereo pose")
        let menuPoint = SIMD4<Float>(0.1, 0.2, -1.8, 1)
        require(simd_length(stereo.neutralFromOrigin * stereo.originFromNeutral * menuPoint - menuPoint) < 0.0002,
                "World-space menu targets map into the same adjusted frame")
        require(orientation.pose(originFromHead: next, recenter: 2, content: .panorama)!.didRecenter,
                "The existing first-panorama auto-recenter remains available")

        // A transient startup coordinate basis must not become the permanent
        // front for a later flat main menu. This represents a tracking-origin
        // correction, not an assertion that the user moved their head.
        var startup = EngineImmersiveOrientation()
        let loading = startup.pose(originFromHead: identity, recenter: 0)!
        require(loading.recenterReason == .initialTracking, "Loading can establish a provisional tracked front")
        require(!near(simd_inverse(identity) * reclined, identity),
                "The old first-loading-frame-only calibration would retain a menu offset")
        let menu = startup.pose(originFromHead: reclined, recenter: 0, content: .flat)!
        require(menu.recenterReason == .firstContent, "First real flat menu recalibrates after loading")
        require(near(menu.neutralFromHead, identity), "First flat menu faces the current tracked head")
        require(near(menu.originFromNeutral, reclined), "First content preserves full reclined pitch and roll")
        require(!startup.pose(originFromHead: next, recenter: 0, content: .flat)!.didRecenter,
                "Later menu frames remain world-stable when the head moves")
        require(near(startup.calibratedOriginFromHead!, reclined), "The menu does not follow subsequent head movement")
        _ = startup.pose(originFromHead: next, recenter: 0, content: .waiting)
        require(!startup.pose(originFromHead: next, recenter: 0, content: .flat)!.didRecenter,
                "A later loading overlay does not reset the one-time menu boundary")

        let firstWorld = startup.pose(originFromHead: next, recenter: 0, content: .panorama)!
        require(firstWorld.recenterReason == .firstPanorama && near(firstWorld.neutralFromHead, identity),
                "The first gameplay panorama retains its deliberate independent calibration")
        require(!startup.pose(originFromHead: reclined, recenter: 0, content: .panorama)!.didRecenter,
                "Subsequent panoramas do not auto-recenter")
        require(!startup.pose(originFromHead: reclined, recenter: 0, content: .flat)!.didRecenter,
                "A pause or return to a flat menu cannot consume startup calibration twice")

        var stationary = EngineImmersiveOrientation()
        _ = stationary.pose(originFromHead: reclined, recenter: 0)
        let stationaryMenu = stationary.pose(originFromHead: reclined, recenter: 0, content: .flat)!
        require(near(stationaryMenu.neutralFromHead, identity), "Unchanged tracking yields no visual jump at menu arrival")
        let menuCenter = SIMD4<Float>(0, 0, -3.1, 1)
        require(near(simd_make_float3(stationaryMenu.neutralFromHead * menuCenter),
                     simd_make_float3(menuCenter)), "The panel center remains on the calibrated forward ray")

        var delayed = EngineImmersiveOrientation()
        _ = delayed.pose(originFromHead: identity, recenter: 0)
        require(delayed.pose(originFromHead: badPose, recenter: 0, content: .flat) == nil,
                "Invalid tracking at first content is rejected")
        require(delayed.pose(originFromHead: reclined, recenter: 0, content: .flat)!.recenterReason == .firstContent,
                "Invalid tracking does not consume first-content calibration")

        var directWorld = EngineImmersiveOrientation()
        _ = directWorld.pose(originFromHead: identity, recenter: 0)
        require(directWorld.pose(originFromHead: reclined, recenter: 0, content: .panorama)!.recenterReason == .firstPanorama,
                "Direct-to-game startup calibrates on the first panorama")
        require(!directWorld.pose(originFromHead: next, recenter: 0, content: .flat)!.didRecenter,
                "A direct panorama also consumes first-content calibration")

        var adjustedStartup = EngineImmersiveOrientation()
        _ = adjustedStartup.pose(originFromHead: identity, recenter: 0)
        let adjustedMenu = adjustedStartup.pose(originFromHead: reclined, recenter: 1, content: .flat,
                                               offset: .init(yawDegrees: 10, pitchDegrees: -5))!
        require(adjustedMenu.recenterReason == .requested, "Explicit recenter takes precedence at a content boundary")
        require(near(simd_inverse(reclined) * adjustedMenu.originFromNeutral,
                     EngineImmersiveOrientationOffset(yawDegrees: 10, pitchDegrees: -5).calibratedFromAdjusted),
                "Content calibration preserves intentional manual placement offsets")
        require(!adjustedStartup.pose(originFromHead: next, recenter: 1, content: .flat)!.didRecenter,
                "Explicit recenter on first content consumes the boundary only once")
        require(adjustedStartup.pose(originFromHead: next, recenter: 2, content: .flat)!.recenterReason == .requested,
                "Manual recenter remains available after automatic startup calibration")

        typealias Content = EngineImmersiveOrientation.Content
        require(Content.current(loading: true, hasFlatFrame: true, hasPanorama: true) == .waiting,
                "Loading cannot consume a content calibration even if stale textures exist")
        require(Content.current(loading: false, hasFlatFrame: false, hasPanorama: false) == .waiting,
                "Black fallback cannot consume content calibration")
        require(Content.current(loading: false, hasFlatFrame: true, hasPanorama: false) == .flat,
                "A valid flat engine frame is content")
        require(Content.current(loading: false, hasFlatFrame: true, hasPanorama: true) == .panorama,
                "A complete panorama is the selected content when both textures exist")
        print("PASS \(checks) immersive orientation assertions: reclined recenter, signs, finite bounds, pending tracking, first-content calibration, stable menus and stereo coherence")
    }
}
