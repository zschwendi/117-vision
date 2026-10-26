import Foundation
import simd

/// Seat transitions recentre the whole display basis exactly as a user
/// recenter does, through the production orientation and locomotion types
/// and EngineImmersive's own order: orientation.pose(recenter: seat token),
/// recenterReason -> sense recenter generation -> locomotion recenter.
@main struct SenseSeatPresentationValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(condition(), message)
    }
    static func head(yawDegrees: Float, pitchDegrees: Float = 0) -> simd_float4x4 {
        let yaw = simd_quatf(angle: yawDegrees * .pi / 180, axis: SIMD3<Float>(0, 1, 0))
        let pitch = simd_quatf(angle: pitchDegrees * .pi / 180, axis: SIMD3<Float>(1, 0, 0))
        var m = simd_float4x4(yaw * pitch); m.columns.3 = SIMD4(0.1, 1.2, -0.3, 1); return m
    }
    static let eyes: [simd_float4x4] = [Float(-0.032), Float(0.032)].map { x in
        var m = matrix_identity_float4x4; m.columns.3 = SIMD4(x, 0, 0, 1); return m
    }

    /// The renderer state EngineImmersive keeps between frames.
    struct Display {
        var orientation = EngineImmersiveOrientation()
        var locomotion = EngineSenseLocomotion()
        var senseRecenter = 0
        var userRecenter = 0
        var offset = EngineImmersiveOrientationOffset()
        let source = EngineSenseMenuTrigger.Source(accessoryID: UUID(), generation: 1)
        var time = 0.0
        var panoramaFromHead = matrix_identity_float4x4
        var recentred = 0

        /// One compositor frame; nil head = no device anchor (tracking loss).
        mutating func frame(_ originFromHead: simd_float4x4?, seat: EngineSenseSeatPresentation,
                            stick: SIMD2<Float>? = nil) {
            time += 1.0 / 90
            guard let originFromHead,
                  let pose = orientation.pose(originFromHead: originFromHead,
                                              recenter: seat.orientationRecenter(userRecenter),
                                              content: .panorama, offset: offset) else {
                _ = locomotion.update(enabled: false, stick: nil, source: nil, mode: 1, recenter: senseRecenter)
                return
            }
            if pose.recenterReason != nil { senseRecenter &+= 1; recentred += 1 }
            // EngineImmersive's turnEnabled: gameplay_supported clears only at the
            // next outer render, so here it stays (stale) true throughout; the
            // same frame's seat snapshot alone must stop the virtual turn.
            let staleGameplaySupported = true
            _ = locomotion.update(enabled: seat.allowsVirtualTurn(staleGameplaySupported && stick != nil),
                                  stick: stick, source: source, mode: 1,
                                  recenter: senseRecenter, timestamp: time)
            panoramaFromHead = locomotion.panoramaFromNeutral * pose.neutralFromHead
        }
        /// Direction of the original camera's front (panorama -Z) in each eye.
        func frontInEyes() -> [SIMD3<Float>] {
            eyes.map { headFromEye in
                let eyeFromPanorama = simd_inverse(panoramaFromHead * headFromEye)
                return simd_normalize(simd_make_float3(eyeFromPanorama * SIMD4<Float>(0, 0, -1, 0)))
            }
        }
    }
    static func centred(_ d: Display, _ label: String) {
        for (index, front) in d.frontInEyes().enumerated() {
            require(simd_length(front - SIMD3<Float>(0, 0, -1)) < 0.0005, "\(label): eye \(index) sees the seat front ahead \(front)")
        }
        require(simd_length(d.locomotion.panoramaFromNeutral.columns.0 - SIMD4(1, 0, 0, 0)) < 1e-6 &&
                simd_length(d.locomotion.panoramaFromNeutral.columns.1 - SIMD4(0, 1, 0, 0)) < 1e-6,
                "\(label): virtual yaw and pitch are zero")
    }

    static func main() {
        var onFoot = EngineSenseSeatPresentation(native: false, transition: 4)
        var display = Display()
        display.frame(head(yawDegrees: 0), seat: onFoot)
        // Build a real virtual turn and a real physical turn before entry.
        display.frame(head(yawDegrees: 0), seat: onFoot, stick: .zero) /* arm: neutral release */
        for _ in 0..<60 { display.frame(head(yawDegrees: 0), seat: onFoot, stick: SIMD2(1, 0)) }
        require(abs(display.locomotion.yaw) > 0.2, "stick turn accumulated virtual yaw \(display.locomotion.yaw)")
        display.frame(head(yawDegrees: 70, pitchDegrees: -20), seat: onFoot)
        let before = display.recentred
        require(display.frontInEyes().allSatisfy { simd_length($0 - SIMD3(0, 0, -1)) > 0.3 },
                "physically and virtually turned: the camera front is off-axis before entry")
        require(onFoot.hudFollowsHead(trackedGameplay: true, menuActive: false, senseActive: true),
                "on foot the visor HUD follows the head as in build119")

        // Seat entry: one snapshot, one recenter of the whole basis.
        var seated = EngineSenseSeatPresentation(native: true, transition: onFoot.transition &+ 1)
        display.frame(head(yawDegrees: 70, pitchDegrees: -20), seat: seated)
        require(display.recentred == before + 1, "entry recentres exactly once")
        centred(display, "entry")
        require(!seated.allowsVirtualTurn(true) && onFoot.allowsVirtualTurn(true) && !onFoot.allowsVirtualTurn(false),
                "seat snapshot gates virtual turn and gestures even while gameplay_supported is still true")
        require(!seated.hudFollowsHead(trackedGameplay: true, menuActive: false, senseActive: true),
                "seated: original HUD and crosshair stay on the seat camera front")
        // The same seat recentres the user's own way: identical basis.
        var reference = EngineImmersiveOrientation()
        _ = reference.pose(originFromHead: head(yawDegrees: 0), recenter: 0, content: .panorama)
        let manual = reference.pose(originFromHead: head(yawDegrees: 70, pitchDegrees: -20), recenter: 1, content: .panorama)!
        require(manual.recenterReason == .requested, "reference user recenter")
        require(simd_length(manual.neutralFromHead.columns.2 - (simd_inverse(display.locomotion.panoramaFromNeutral) *
                display.panoramaFromHead).columns.2) < 1e-5, "seat recenter equals a user recenter")
        // Not per frame: later frames, head motion and the right stick change nothing.
        for yaw in stride(from: Float(70), through: 110, by: 10) {
            display.frame(head(yawDegrees: yaw, pitchDegrees: -20), seat: seated, stick: SIMD2(1, 0))
        }
        require(display.recentred == before + 1 && display.locomotion.yaw == 0,
                "seated with stale gameplay_supported: no further recenter and no virtual turn (native seat yaw only)")
        require(simd_length(display.frontInEyes()[0] - SIMD3(0, 0, -1)) > 0.3,
                "the seat front stays world-fixed when the head turns")

        // Exit during tracking loss: the reset waits for the next tracked pose.
        onFoot = EngineSenseSeatPresentation(native: false, transition: seated.transition &+ 1)
        display.frame(nil, seat: onFoot); display.frame(nil, seat: onFoot)
        require(display.recentred == before + 1, "no pose: recenter pending, nothing consumed")
        display.frame(head(yawDegrees: -35, pitchDegrees: 15), seat: onFoot)
        require(display.recentred == before + 2, "recovery applies the pending exit recenter")
        centred(display, "exit after tracking loss")
        require(onFoot.hudFollowsHead(trackedGameplay: true, menuActive: false, senseActive: true), "on-foot visor resumes")
        // On-foot virtual turning resumes after exit.
        display.frame(head(yawDegrees: -35, pitchDegrees: 15), seat: onFoot, stick: .zero)
        for _ in 0..<30 { display.frame(head(yawDegrees: -35, pitchDegrees: 15), seat: onFoot, stick: SIMD2(1, 0)) }
        require(display.locomotion.yaw != 0, "virtual turn resumes on foot")

        // Menu presentation offsets are kept: the seat recentre places the
        // seat front where a user recenter with that offset would.
        display.offset = EngineImmersiveOrientationOffset(yawDegrees: 12, pitchDegrees: 5)
        seated = EngineSenseSeatPresentation(native: true, transition: onFoot.transition &+ 1)
        display.frame(head(yawDegrees: 20), seat: seated)
        var offsetReference = EngineImmersiveOrientation()
        _ = offsetReference.pose(originFromHead: head(yawDegrees: 0), recenter: 0, content: .panorama, offset: display.offset)
        let offsetManual = offsetReference.pose(originFromHead: head(yawDegrees: 20), recenter: 1, content: .panorama, offset: display.offset)!
        require(simd_length(offsetManual.neutralFromHead.columns.2 - display.panoramaFromHead.columns.2) < 1e-5 &&
                simd_length(display.locomotion.panoramaFromNeutral.columns.0 - SIMD4(1, 0, 0, 0)) < 1e-6,
                "offset preserved through seat recentre")
        // Both eyes agree on the seat front (same snapshot for every eye).
        let fronts = display.frontInEyes()
        require(simd_length(fronts[0] - fronts[1]) < 1e-6, "both eyes share one basis")
        // A user recenter and a seat transition in the same frame are one recenter.
        display.userRecenter += 1
        seated = EngineSenseSeatPresentation(native: true, transition: seated.transition &+ 1)
        let count = display.recentred
        display.frame(head(yawDegrees: 5), seat: seated); display.frame(head(yawDegrees: 5), seat: seated)
        require(display.recentred == count + 1, "combined request recentres once")
        print("PASS \(checks) checks: seat entry/exit recentre physical neutral, virtual yaw/pitch and both eyes once; native seat front world-fixed with no virtual turn; HUD head-follow only on foot; pending through tracking loss; offsets kept")
    }
}
