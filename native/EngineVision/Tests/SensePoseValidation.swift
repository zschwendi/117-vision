import Foundation
import simd

@main struct SensePoseValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        precondition(condition(), message)
    }
    static func near(_ a: SIMD3<Float>, _ b: SIMD3<Float>) -> Bool { simd_length(a - b) < 0.0002 }

    static func main() {
        let identity = matrix_identity_float4x4
        let straight = EngineSensePose(originFromAim: identity)!
        require(near(straight.origin, .zero), "Identity aim is at origin")
        require(near(straight.direction, SIMD3(0, 0, -1)), "Aim looks down negative Z")
        var translated = identity; translated.columns.3 = SIMD4(1, 2, 3, 1)
        let translatedPose = EngineSensePose(originFromAim: translated)!
        require(near(translatedPose.origin, SIMD3(1, 2, 3)), "Controller origin is retained")
        require(near(translatedPose.direction, straight.direction), "Position never changes direction")
        var handle = identity; handle.columns.3 = SIMD4(0.2, -0.1, -0.3, 1)
        let separated = EngineSensePose(originFromAim: translated, originFromGrip: handle)!
        require(near(separated.origin, SIMD3(1, 2, 3)), "Menu ray retains aim origin rather than handle")
        require(near(simd_make_float3(separated.originFromGrip.columns.3), SIMD3(0.2, -0.1, -0.3)), "Physical handle origin is preserved separately")
        let movedGrip = separated.transformed(by: translated)!
        require(near(simd_make_float3(movedGrip.originFromGrip.columns.3), SIMD3(1.2, 1.9, 2.7)), "Grip follows the same world-reference conversion as aim")
        var badGrip = handle; badGrip.columns.0.x = 2
        require(EngineSensePose(originFromAim: translated, originFromGrip: badGrip) == nil, "Malformed grip cannot masquerade as tracked position")

        // Whole reclined calibration is shared with the menu, not yaw-only.
        let reclined = simd_float4x4(simd_quatf(angle: 1.1, axis: SIMD3(1, 0, 0))
                                    * simd_quatf(angle: 0.5, axis: SIMD3(0, 0, 1)))
        let worldFromHand = translated * reclined
        let hand = EngineSensePose(originFromAim: worldFromHand)!
        let local = hand.transformed(by: simd_inverse(worldFromHand))!
        require(near(local.origin, .zero), "Inverse full calibration cancels hand translation")
        require(near(local.direction, straight.direction), "Inverse pitch and roll cancel orientation")
        require(near(simd_make_float3(local.originFromAim.columns.0), SIMD3(1, 0, 0)), "Inverse transform cancels full weapon roll")
        let rolled = EngineSensePose(originFromAim: simd_float4x4(simd_quatf(angle: .pi / 2, axis: SIMD3(0, 0, 1))))!
        let retained = rolled.transformed(by: translated)!
        require(near(simd_make_float3(retained.originFromAim.columns.0), SIMD3(0, 1, 0)), "Changing world origin preserves controller roll")
        require(near(retained.origin, SIMD3(1, 2, 3)), "Full transform preserves translation")
        require(near(retained.direction, SIMD3(0, 0, -1)), "Roll does not change weapon ray")
        for angle: Float in [-Float.pi, -.pi / 2, -0.2, 0, 0.2, .pi / 2, .pi] {
            let rotation = simd_float4x4(simd_quatf(angle: angle, axis: SIMD3(1, 0, 0)))
            let pose = straight.transformed(by: rotation)!
            require(abs(simd_length(pose.direction) - 1) < 0.0001, "Direction remains unit length including poles")
            require(near(pose.direction, SIMD3(0, sin(angle), -cos(angle))), "Vertical aim retains orientation")
        }
        var invalid = identity; invalid.columns.0.x = .nan
        require(EngineSensePose(originFromAim: invalid) == nil, "Nonfinite matrix rejected")
        invalid = identity; invalid.columns.1 = .zero
        require(EngineSensePose(originFromAim: invalid) == nil, "Singular matrix rejected")
        invalid = identity; invalid.columns.2.z = 2
        require(EngineSensePose(originFromAim: invalid) == nil, "Scaled input cannot masquerade as pose")
        invalid = identity; invalid.columns.3.x = .infinity
        require(straight.transformed(by: invalid) == nil, "Malformed calibration rejected")

        require(EngineSensePose.isFresh(anchorTimestamp: 20, presentationTimestamp: 20.04), "Recent measured pose accepted")
        require(EngineSensePose.isFresh(anchorTimestamp: 20.02, presentationTimestamp: 20), "Small prediction lead accepted")
        require(!EngineSensePose.isFresh(anchorTimestamp: 20, presentationTimestamp: 20.16), "Stale pose rejected")
        require(!EngineSensePose.isFresh(anchorTimestamp: 20.06, presentationTimestamp: 20), "Wrong-clock future pose rejected")
        require(!EngineSensePose.isFresh(anchorTimestamp: .nan, presentationTimestamp: 20), "Invalid sample time rejected")
        require(!EngineSensePose.isFresh(anchorTimestamp: 20, presentationTimestamp: .infinity), "Invalid render time rejected")

        require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: .full,
            predictedTimestamp: 20.04, targetTimestamp: 20.04) == .predicted, "Fresh predicted 6-DoF preferred for rendering")
        require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: nil,
            predictedTimestamp: nil, targetTimestamp: 20.04) == .measured, "Prediction failure retains a fresh measured aim")
        for prediction in [EngineSensePose.TrackingQuality.orientationOnly, .untracked] {
            require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: prediction,
                predictedTimestamp: 20.04, targetTimestamp: 20.04) == .measured, "Invalid prediction cannot replace fresh position")
        }
        for predictionTime in [19.0, 21.0, .nan, .infinity] {
            require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: .full,
                predictedTimestamp: predictionTime, targetTimestamp: 20.04) == .measured, "Invalid prediction clock falls back to current measured aim")
        }
        for measured in [EngineSensePose.TrackingQuality.orientationOnly, .untracked] {
            require(EngineSensePose.selectSample(measured: measured, measuredTimestamp: 20, predicted: .full,
                predictedTimestamp: 20.04, targetTimestamp: 20.04) == nil, "Prediction cannot recover a lost measured position")
        }
        for measuredTime in [19.0, 21.0, .nan, .infinity] {
            require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: measuredTime, predicted: .full,
                predictedTimestamp: 20.04, targetTimestamp: 20.04) == nil, "Prediction cannot launder a stale measured pose")
        }
        require(EngineSensePose.selectSample(measured: .fullLowAccuracy, measuredTimestamp: 20, predicted: nil,
            predictedTimestamp: nil, targetTimestamp: 20.04) == .measured, "Apple's lower accuracy 6-DoF retains position")
        require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: .fullLowAccuracy,
            predictedTimestamp: 20.04, targetTimestamp: 20.04) == .predicted, "Lower accuracy full prediction remains explicit usable data")
        require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 20, predicted: .full,
            predictedTimestamp: 20.04, targetTimestamp: .nan) == nil, "Invalid compositor time never yields an aim")

        // Relative support geometry is meaningful only with a shared sample
        // policy. A failed left prediction must also retire right prediction.
        let measuredLeft = EngineSensePose.Sample(quality: .full, timestamp: 30)
        let measuredRight = EngineSensePose.Sample(quality: .full, timestamp: 30.01)
        let prediction = EngineSensePose.Sample(quality: .full, timestamp: 30.04)
        func pair(_ l: EngineSensePose.Sample = measuredLeft,
                  _ r: EngineSensePose.Sample = measuredRight,
                  _ pl: EngineSensePose.Sample? = prediction,
                  _ pr: EngineSensePose.Sample? = prediction) -> EngineSensePose.SampleChoice? {
            EngineSensePose.selectPair(left: l, right: r, predictedLeft: pl, predictedRight: pr,
                                       targetTimestamp: 30.04)
        }
        require(pair() == .predicted, "Both valid predictions share the presentation horizon")
        require(pair(measuredLeft, measuredRight, nil, prediction) == .measured, "Left failure selects measured right too")
        require(pair(measuredLeft, measuredRight, prediction, nil) == .measured, "Right failure selects measured left too")
        require(pair(measuredLeft, measuredRight, nil, nil) == .measured, "Fresh measured pair works without predictions")
        for quality in [EngineSensePose.TrackingQuality.orientationOnly, .untracked] {
            require(pair(measuredLeft, measuredRight, .init(quality: quality, timestamp: 30.04), prediction) == .measured,
                    "Invalid predicted quality falls back as a pair")
            require(pair(.init(quality: quality, timestamp: 30), measuredRight) == nil,
                    "Prediction cannot restore a missing measured hand")
        }
        for time in [29.0, 31.0, .nan, .infinity] {
            require(pair(measuredLeft, measuredRight, .init(quality: .full, timestamp: time), prediction) == .measured,
                    "Bad prediction timestamps retire both predictions")
            require(pair(.init(quality: .full, timestamp: time), measuredRight) == nil,
                    "Bad measured timestamps reject gameplay pair")
        }
        let skewed = EngineSensePose.Sample(quality: .full, timestamp: 29.96)
        require(pair(skewed, measuredRight, nil, prediction) == nil, "Fresh but50ms skewed measured pair cannot drive support")
        require(pair(skewed, measuredRight) == .predicted, "Coherent fresh predictions resolve measured skew")
        require(pair(measuredLeft, .init(quality: .full, timestamp: 30.032), nil, nil) == .measured, "32ms measured skew accepted")
        require(pair(measuredLeft, .init(quality: .full, timestamp: 30.034), nil, nil) == nil, "34ms measured skew rejected")
        require(pair(measuredLeft, measuredRight, .init(quality: .full, timestamp: 29.99), prediction) == .measured,
                "Predicted pair timestamps must also be coherent")
        // A rigid physical translation has identical grip span under either
        // complete policy; the prior mixed policy manufactured a shorter gun.
        let measuredSpan = SIMD3<Float>(0, 0, -0.35)
        let move = SIMD3<Float>(0.06, 0, -0.1)
        let rightMeasured = SIMD3<Float>(0.2, -0.1, -0.3)
        let leftMeasured = rightMeasured + measuredSpan
        let choice = pair(measuredLeft, measuredRight, nil, prediction)!
        let rightPosition = choice == .predicted ? rightMeasured + move : rightMeasured
        let leftPosition = choice == .predicted ? leftMeasured + move : leftMeasured
        require(near(leftPosition - rightPosition, measuredSpan), "Prediction fallback preserves rigid two-hand span")
        require(!near(leftMeasured - (rightMeasured + move), measuredSpan), "Fixture detects old mixed-horizon support distortion")
        require(EngineSensePose.selectSample(measured: .full, measuredTimestamp: 30.01, predicted: nil,
            predictedTimestamp: nil, targetTimestamp: 30.04) == .measured, "Single-hand menu ray remains usable after pair rejects")

        var trigger = EngineSenseMenuTrigger()
        let source = EngineSenseMenuTrigger.Source(accessoryID: UUID(), generation: 1)
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Held R2 on menu entry does not click")
        require(!trigger.update(enabled: true, onPanel: true, value: 0, source: source), "Release arms selection")
        require(!trigger.update(enabled: true, onPanel: true, value: 0.5, source: source), "Trigger travel does not prematurely select")
        require(trigger.update(enabled: true, onPanel: true, value: 0.9, source: source), "Fresh press selects once")
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Held trigger never repeats")
        require(!trigger.update(enabled: true, onPanel: true, value: 0.4, source: source), "Hysteresis remains held")
        require(!trigger.update(enabled: true, onPanel: true, value: 0.8, source: source), "Threshold jitter cannot repeat")
        require(!trigger.update(enabled: true, onPanel: true, value: 0.1, source: source), "Release rearms")
        require(trigger.update(enabled: true, onPanel: true, value: 0.8, source: source), "Next deliberate press selects")
        require(!trigger.update(enabled: false, onPanel: true, value: 0, source: source), "Gameplay resets selection")
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Pause while firing does not click")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: source)
        require(!trigger.update(enabled: true, onPanel: true, value: nil, source: source), "Tracking loss disarms")
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Reacquisition while held does not click")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: source)
        require(!trigger.update(enabled: true, onPanel: false, value: 0, source: source), "Leaving menu surface disarms")
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Returning while held does not click")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: source)
        require(!trigger.update(enabled: true, onPanel: true, value: .nan, source: source), "Invalid trigger disarms")
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Invalid data cannot arm selection")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: source)
        trigger.reset()
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: source), "Compositor suspension disarms")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: source)
        let reconnected = EngineSenseMenuTrigger.Source(accessoryID: source.accessoryID, generation: 2)
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: reconnected), "Same accessory reconnect requires release")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: reconnected)
        let replacement = EngineSenseMenuTrigger.Source(accessoryID: UUID(), generation: 2)
        require(!trigger.update(enabled: true, onPanel: true, value: 1, source: replacement), "Replacement controller requires release")
        _ = trigger.update(enabled: true, onPanel: true, value: 0, source: replacement)
        require(trigger.update(enabled: true, onPanel: true, value: 1, source: replacement), "New owner selects after deliberate release")
        require(!trigger.update(enabled: true, onPanel: true, value: 0, source: nil), "Missing owner cannot arm selection")
        print("PASS \(checks) Sense pose and menu trigger checks")
    }
}
