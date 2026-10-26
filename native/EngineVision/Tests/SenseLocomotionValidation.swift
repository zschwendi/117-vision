import Foundation
import simd

extension EngineSenseLocomotion {
    /// Behavioural tilt for a recentred, forward-facing head: elevation of
    /// the gaze through the shared basis (positive looks up).
    var forwardElevation: Float { asin(max(-1, min(1, -panoramaFromNeutral.columns.2.y))) }
}

@main struct SenseLocomotionValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(condition(), message)
    }
    static func near(_ a: SIMD3<Float>, _ b: SIMD3<Float>) -> Bool { simd_length(a-b) < 0.0001 }

    struct ContinuousRun {
        var state = EngineSenseLocomotion()
        var source = EngineSenseMenuTrigger.Source(accessoryID: UUID(), generation: 1)
        var time = 0.0

        @discardableResult mutating func step(
            dt: Double = 1.0 / 90, stick: SIMD2<Float>? = .zero,
            mode: Int = 2, enabled: Bool = true, recenter: Int = 0,
            aim: Float? = 0, distance: Float = 2, head: Float? = 0, pose: simd_float4x4? = nil,
            tuning: EngineSenseLocomotion.Tuning = .init(), at: Double? = nil
        ) -> Float {
            time = at ?? time + dt
            _ = state.update(enabled: enabled, stick: stick, source: source,
                mode: mode, recenter: recenter, timestamp: time,
                edgeAim: aim.map { .init(horizontal: $0, forwardDistance: distance) },
                physicalHeadYaw: head, neutralFromHead: pose, tuning: tuning)
            return state.yaw
        }
    }

    static func continuousChecks() {
        var held = ContinuousRun()
        for _ in 0..<120 { held.step(stick: SIMD2(1,0), mode: 1) }
        require(held.state.yaw == 0, "Smooth entry with held stick requires release")
        held.step(mode: 1)
        let first = held.step(stick: SIMD2(1,0), mode: 1)
        require(first > 0 && first < Float.pi / 180, "First smooth frame accelerates gently")
        let generation = held.state.generation
        for _ in 0..<90 { held.step(stick: SIMD2(1,0), mode: 1) }
        require(held.state.yaw > .pi / 3, "Holding smooth stick continues turning")
        require(held.state.generation == generation, "Continuous yaw does not change the source epoch")
        let beforeRelease = held.state.yaw
        for _ in 0..<30 { held.step(mode: 1) }
        require(held.state.yaw == beforeRelease, "Releasing smooth stick stops immediately without coasting")
        held.step(stick: SIMD2(-1,0), mode: 1)
        require(held.state.yaw < beforeRelease, "Opposite stick never continues the previous turn")

        func smoothYaw(fps: Int, seconds: Int = 1, x: Float = 1) -> Float {
            var run = ContinuousRun()
            run.step(mode: 1, at: 0)
            for frame in 1...(fps * seconds) {
                run.step(stick: SIMD2(x,0), mode: 1, at: Double(frame) / Double(fps))
            }
            return run.state.yaw
        }
        let reference = smoothYaw(fps: 90)
        // Default saved speed 90 -> full right stick turns at 180 deg/s.
        // One second keeps the doubled turn below the pi wrap.
        let exact = Float(Double.pi * (1 - 0.12 * (1 - exp(-1 / 0.12))))
        require(abs(reference - exact) < 0.00001, "Smooth turn is twice the configured speed")
        for fps in [30, 60, 120, 144] {
            require(abs(smoothYaw(fps: fps) - reference) < 0.00001,
                    "Smooth turn distance is independent of refresh rate \(fps)")
        }
        require(abs(smoothYaw(fps: 90, x: 0.6) - reference * 0.25) < 0.00001,
                "Small stick movement gives a controllable nonlinear speed")
        var longRun = ContinuousRun()
        longRun.step(mode: 1, at: 0)
        for frame in 1...108_000 {
            longRun.step(stick: SIMD2(1,0), mode: 1, at: Double(frame) / 90)
        }
        let longExpected = Float((Double.pi * (1_200 - 0.12)).remainder(dividingBy: 2 * .pi))
        require(longRun.state.yaw.isFinite && abs(longRun.state.yaw) <= .pi,
                "Twenty minutes of continuous turning keeps a finite wrapped yaw")
        require(abs(longRun.state.yaw - longExpected) < 0.003,
                "Long-running integration does not accumulate material drift")

        var edge = ContinuousRun()
        for _ in 0..<120 { edge.step(aim: 1) }
        require(edge.state.yaw == 0, "Point-turn entry held at edge waits for centered aim")
        edge.step()
        edge.step(aim: 1, at: 2)
        require(edge.state.yaw == 0, "Time gap cancels arming instead of catching up")
        edge.step(at: 2.01)
        edge.step(aim: 1, at: 2.02)
        edge.step(aim: 1, at: 2.10)
        edge.step(aim: 1, at: 2.19)
        require(edge.state.yaw == 0, "Point-turn requires the full 180ms dwell")
        edge.step(aim: 1, at: 2.21)
        require(edge.state.yaw > 0 && edge.state.yaw < 0.001,
                "Only the part of the frame after the dwell is integrated")
        let beforeDuplicate = edge.state.yaw
        edge.step(aim: 1, at: 2.21)
        require(edge.state.yaw == beforeDuplicate, "Duplicate drawable timestamp never turns twice")
        edge.step(aim: 1, at: 2.22)
        require(edge.state.yaw > beforeDuplicate, "Duplicate timestamp preserves completed dwell")
        let beforeHysteresis = edge.state.yaw
        edge.step(aim: 0.65, at: 2.23)
        require(edge.state.yaw > beforeHysteresis, "Active point turn remains stable in the hysteresis band")
        let beforeStop = edge.state.yaw
        edge.step(aim: 0.55, at: 2.24)
        edge.step(aim: 0.65, at: 2.25)
        require(edge.state.yaw == beforeStop, "Leaving edge zone stops and cannot restart below entry boundary")
        edge.step(aim: 1, at: 2.26)
        edge.step(aim: 1, at: 2.30)
        require(edge.state.yaw == beforeStop, "Re-entering the edge requires a new dwell")

        func edgeYaw(fps: Int) -> Float {
            var run = ContinuousRun()
            run.step(at: 0)
            run.step(aim: 1, at: 0.01)
            for frame in 1...(fps * 2) {
                run.step(aim: 1, at: 0.01 + Double(frame) / Double(fps))
            }
            return run.state.yaw
        }
        let edgeReference = edgeYaw(fps: 90)
        let edgeExact = Float(Double.pi / 4 * (1.82 - 0.12 * (1 - exp(-1.82 / 0.12))))
        require(abs(edgeReference - edgeExact) < 0.00001,
                "Dwell and acceleration give the expected point-turn angle")
        for fps in [30, 60, 120, 144] {
            require(abs(edgeYaw(fps: fps) - edgeReference) < 0.00001,
                    "Point turn distance is independent of refresh rate \(fps)")
        }

        var priority = ContinuousRun()
        priority.step()
        for _ in 0..<60 { priority.step(aim: 1) }
        let beforeStick = priority.state.yaw
        priority.step(stick: SIMD2(-1,0), aim: 1)
        require(priority.state.yaw < beforeStick, "Intentional left stick overrides right edge intent")
        let afterStick = priority.state.yaw
        for _ in 0..<60 { priority.step(aim: 1) }
        require(priority.state.yaw == afterStick, "Held edge cannot resume after stick release until recentered")
        priority.step()
        for _ in 0..<60 { priority.step(aim: -1) }
        require(priority.state.yaw < afterStick, "Centered aim rearms a deliberate opposite edge turn")

        var verticalEntry = ContinuousRun()
        for _ in 0..<30 { verticalEntry.step(stick: SIMD2(0,1)) }
        for _ in 0..<60 { verticalEntry.step(stick: SIMD2(0,1), aim: 1) }
        require(verticalEntry.state.yaw == 0, "Held vertical stick on entry cannot arm point turning")
        verticalEntry.step()
        for _ in 0..<60 { verticalEntry.step(aim: 1) }
        require(verticalEntry.state.yaw > 0, "Centering both aim and stick enables point turning")
        let beforeVertical = verticalEntry.state.yaw
        for _ in 0..<60 { verticalEntry.step(stick: SIMD2(0,1), aim: 1) }
        for _ in 0..<60 { verticalEntry.step(aim: 1) }
        require(verticalEntry.state.yaw == beforeVertical,
                "Vertical stick intent suppresses active edge turning and requires centered aim")
        verticalEntry.step(stick: SIMD2(0,1), enabled: false)
        for _ in 0..<30 { verticalEntry.step(stick: SIMD2(0,1)) }
        for _ in 0..<60 { verticalEntry.step(stick: SIMD2(0,1), aim: 1) }
        require(verticalEntry.state.yaw == beforeVertical,
                "Tracking recovery with held vertical stick cannot arm point turning")

        var duplicateDwell = ContinuousRun()
        duplicateDwell.step(at: 0)
        duplicateDwell.step(aim: 1, at: 0.01)
        duplicateDwell.step(aim: 1, at: 0.10)
        for _ in 0..<10 { duplicateDwell.step(aim: 1, at: 0.10) }
        duplicateDwell.step(aim: 1, at: 0.19)
        duplicateDwell.step(aim: 1, at: 0.20)
        require(duplicateDwell.state.yaw > 0, "Duplicate drawables preserve a partially completed dwell")

        for change in 0..<3 {
            var run = ContinuousRun()
            run.step()
            for _ in 0..<60 { run.step(aim: 1) }
            let before = run.state.yaw
            switch change {
            case 0: run.source = .init(accessoryID: UUID(), generation: 1)
            case 1: run.step(mode: 1, aim: 1)
            default: run.step(dt: 0.2, aim: 1)
            }
            for _ in 0..<60 { run.step(aim: 1) }
            require(run.state.yaw == before, "Source/mode/clock change \(change) cancels held edge intent")
            run.step()
            for _ in 0..<60 { run.step(aim: 1) }
            require(run.state.yaw > before, "Centered aim rearms point turn after change \(change)")
        }

        for failure in 0..<10 {
            var run = ContinuousRun()
            run.step()
            for _ in 0..<60 { run.step(aim: 1) }
            let before = run.state.yaw
            switch failure {
            case 0: run.step(aim: nil)
            case 1: run.step(aim: .nan)
            case 2: run.step(aim: 1, distance: 0.35)
            case 3: run.step(aim: 1, distance: -1)
            case 4: run.step(aim: 1, distance: .infinity)
            case 5: run.step(aim: 1, head: nil)
            case 6: run.step(aim: 1, head: .nan)
            case 7: run.step(stick: nil, aim: 1)
            case 8: run.step(enabled: false, aim: 1)
            default: run.step(stick: SIMD2(.nan,0), aim: 1)
            }
            for _ in 0..<60 { run.step(aim: 1) }
            require(run.state.yaw == before, "Invalid/stale/near/rear input \(failure) stops and requires centered aim")
            run.step()
            for _ in 0..<60 { run.step(aim: 1) }
            require(run.state.yaw > before, "Fresh centered input rearms after failure \(failure)")
        }
        // Edge validity is deliberately separate from stick validity. A gun
        // pointed behind or above the eye FOV must not disable the fallback.
        var noAim = ContinuousRun()
        noAim.step(aim: nil, head: nil)
        for _ in 0..<30 { noAim.step(stick: SIMD2(1,0), aim: nil, head: nil) }
        require(noAim.state.yaw > 0, "Smooth fallback works with no visible forward reticle")

        for failure in 0..<6 {
            var run = ContinuousRun()
            run.step(mode: 1)
            for _ in 0..<30 { run.step(stick: SIMD2(1,0), mode: 1) }
            let before = run.state.yaw
            switch failure {
            case 0: run.step(stick: SIMD2(1,0), mode: 1, enabled: false)
            case 1: run.source = .init(accessoryID: run.source.accessoryID, generation: 2)
            case 2: run.step(stick: SIMD2(1,0), mode: 0)
            case 3: run.step(dt: 3, stick: SIMD2(1,0), mode: 1)
            case 4: run.step(stick: SIMD2(1,0), mode: 1, at: run.time - 0.1)
            default: run.step(stick: SIMD2(1,0), mode: 1, at: .nan); run.time = 10
            }
            for _ in 0..<30 { run.step(stick: SIMD2(1,0), mode: 1) }
            require(run.state.yaw == before, "Smooth stale/reconnect/clock/mode input \(failure) requires neutral")
            run.step(mode: 1)
            run.step(stick: SIMD2(1,0), mode: 1)
            require(run.state.yaw > before, "Smooth mode resumes only after release \(failure)")
        }
        var reset = ContinuousRun()
        reset.step()
        for _ in 0..<60 { reset.step(aim: 1) }
        reset.step(recenter: 1, aim: 1)
        for _ in 0..<60 { reset.step(recenter: 1, aim: 1) }
        require(reset.state.yaw == 0, "Recenter resets yaw and disarms an active point turn")

        for degreesPerSecond: Float in [5, 30, 90] {
            var physical = ContinuousRun()
            physical.step(at: 0)
            // Fixed gun, rotating head: the actual projected gun drifts to an
            // edge. Even slow head rotation must not add virtual body turning.
            for frame in 1...720 {
                let degrees = Float(frame) / 90 * degreesPerSecond
                let ndc = min(1, degrees / 45)
                physical.step(aim: ndc, head: -degrees * .pi / 180, at: Double(frame) / 90)
            }
            require(physical.state.yaw == 0, "Physical head turn at \(degreesPerSecond) deg/s induces no virtual turn")
        }
        var centered360 = ContinuousRun()
        for frame in 0...720 {
            centered360.step(head: Float(frame) * .pi / 180, at: Double(frame) / 90)
        }
        require(centered360.state.yaw == 0, "Two full physical turns with centered gun induce no virtual turn")
        var wrappingHead = ContinuousRun()
        wrappingHead.step(head: .pi - 0.0005)
        for _ in 0..<60 { wrappingHead.step(aim: 1, head: -.pi + 0.0005) }
        require(wrappingHead.state.yaw > 0, "Head yaw wrap at pi is not mistaken for a full physical rotation")

        var tuned = ContinuousRun()
        tuned.step(mode: 1)
        for _ in 0..<30 { tuned.step(stick: SIMD2(1,0), mode: 1) }
        let beforeTuning = tuned.state.yaw
        let tunedGeneration = tuned.state.generation
        let changed = EngineSenseLocomotion.Tuning(stickDegreesPerSecond: 120, edgeBoundary: 0.5)
        for _ in 0..<30 { tuned.step(stick: SIMD2(1,0), mode: 1, tuning: changed) }
        require(tuned.state.yaw == beforeTuning && tuned.state.generation > tunedGeneration,
                "Changing sensitivity disarms held input and advances the discontinuity epoch")
        tuned.step(tuning: changed)
        for _ in 0..<60 { tuned.step(aim: 0.6, tuning: changed) }
        require(tuned.state.yaw > beforeTuning, "Configured edge boundary actually changes activation")
        var invalidTuning = ContinuousRun()
        invalidTuning.step(mode: 1, tuning: .init(stickDegreesPerSecond: .nan, edgeBoundary: .infinity), at: 0)
        for frame in 1...90 {
            invalidTuning.step(stick: SIMD2(1,0), mode: 1,
                tuning: .init(stickDegreesPerSecond: .nan, edgeBoundary: .infinity), at: Double(frame) / 90)
        }
        require(abs(invalidTuning.state.yaw - reference) < 0.00001, "Invalid tuning uses finite defaults without repeated rearming")
    }

    static func pitchChecks() {
        let degree = Float.pi / 180, limit = EngineSenseLocomotion.pitchLimit
        func ramp(_ degreesPerSecond: Float, _ t: Double) -> Float {
            Float(Double(degreesPerSecond * degree) * (t - 0.12 * (1 - exp(-t / 0.12))))
        }
        func hold(_ stick: SIMD2<Float>, fps: Int = 90, frames: Int, mode: Int = 1,
                  tuning: EngineSenseLocomotion.Tuning = .init()) -> EngineSenseLocomotion {
            var run = ContinuousRun()
            run.step(mode: mode, tuning: tuning, at: 0)
            for frame in 1...frames {
                run.step(stick: stick, mode: mode, tuning: tuning, at: Double(frame) / Double(fps))
            }
            return run.state
        }
        for speed: Float in [30, 60, 90, 120] {
            let tuning = EngineSenseLocomotion.Tuning(stickDegreesPerSecond: speed)
            let turned = hold(SIMD2(1,0), frames: 45, tuning: tuning)
            require(abs(turned.yaw - ramp(2 * speed, 0.5)) < 0.00001,
                    "Right stick turns at twice saved speed \(speed)")
            require(turned.forwardElevation == 0, "Horizontal stick never tilts at speed \(speed)")
            let tilted = hold(SIMD2(0,1), frames: 27, tuning: tuning)
            require(abs(tilted.forwardElevation - ramp(speed, 27.0 / 90)) < 0.00001,
                    "Vertical stick tilts at the saved speed \(speed)")
            require(tilted.yaw == 0, "Vertical stick never turns at speed \(speed)")
            let pointMode = hold(SIMD2(0,1), frames: 27, mode: 2, tuning: tuning)
            require(pointMode.forwardElevation == tilted.forwardElevation, "Point-to-turn + stick tilts identically at speed \(speed)")
        }
        var slowEdge = ContinuousRun()
        let slow = EngineSenseLocomotion.Tuning(stickDegreesPerSecond: 60)
        slowEdge.step(tuning: slow, at: 0)
        slowEdge.step(aim: 1, tuning: slow, at: 0.01)
        for frame in 1...180 { slowEdge.step(aim: 1, tuning: slow, at: 0.01 + Double(frame) / 90) }
        require(abs(slowEdge.state.yaw - ramp(30, 1.82)) < 0.00001,
                "Point-turn edge stays at half the saved speed, not doubled")

        let reference = hold(SIMD2(0,1), frames: 45)
        for fps in [30, 60, 120, 144] {
            let sample = hold(SIMD2(0,1), fps: fps, frames: fps / 2)
            require(abs(sample.forwardElevation - reference.forwardElevation) < 0.00001, "Tilt is independent of refresh rate \(fps)")
        }
        let diagonal = hold(SIMD2(1,1), frames: 45)
        let horizontal = hold(SIMD2(1,0), frames: 45)
        require(diagonal.yaw == horizontal.yaw && abs(diagonal.forwardElevation - reference.forwardElevation) < 0.00001,
                "Full diagonal turns and tilts at both full rates; deliberate yaw is exactly the yaw-only turn")
        let partial = hold(SIMD2(0.6,-0.6), frames: 45)
        require(abs(partial.yaw - horizontal.yaw * 0.25) < 0.00001 && abs(partial.forwardElevation + reference.forwardElevation * 0.25) < 0.00001,
                "Partial diagonal uses the same curve per axis; down tilts down")
        let drift = hold(SIMD2(1,0.15), frames: 45)
        require(drift.forwardElevation == 0 && drift.yaw == horizontal.yaw, "Small vertical drift while turning does not tilt")
        let lean = hold(SIMD2(0.15,1), frames: 45)
        require(lean.yaw == 0 && lean.forwardElevation == reference.forwardElevation, "Small horizontal drift while tilting does not turn")

        var clamp = ContinuousRun()
        clamp.step(mode: 1)
        for _ in 0..<270 { clamp.step(stick: SIMD2(0,1), mode: 1) }
        require(abs(clamp.state.forwardElevation - limit) < 0.00001 && clamp.state.tilt <= limit + 0.00001,
                "Holding up stops at the finite tilt limit")
        let atLimit = clamp.state.panoramaFromNeutral
        for _ in 0..<30 { clamp.step(mode: 1) }
        require(clamp.state.panoramaFromNeutral == atLimit, "Release at the limit does not coast or bounce")
        clamp.step(stick: SIMD2(0,-1), mode: 1)
        require(clamp.state.forwardElevation < limit, "Reversing at the limit responds immediately")
        for _ in 0..<540 { clamp.step(stick: SIMD2(0,-1), mode: 1) }
        require(abs(clamp.state.forwardElevation + limit) < 0.00001 && clamp.state.tilt <= limit + 0.00001,
                "Holding down stops at the lower limit")

        var coast = ContinuousRun()
        coast.step(mode: 1)
        for _ in 0..<20 { coast.step(stick: SIMD2(0,1), mode: 1) }
        let released = coast.state.forwardElevation
        for _ in 0..<30 { coast.step(mode: 1) }
        require(released > 0 && coast.state.forwardElevation == released, "Releasing the tilt stops immediately")
        var fresh = ContinuousRun()
        fresh.step(mode: 1); fresh.step(stick: SIMD2(0,1), mode: 1)
        coast.step(stick: SIMD2(0,1), mode: 1)
        require(abs(coast.state.forwardElevation - released - fresh.state.forwardElevation) < 0.0000001,
                "Tilting again after release starts from rest, with no stored speed")
        let repushed = coast.state.forwardElevation
        coast.step(stick: SIMD2(0,-1), mode: 1)
        require(coast.state.forwardElevation < repushed, "Opposite tilt never continues the previous direction")
        coast.step(mode: 1)
        coast.step(stick: SIMD2(0,-1), mode: 1)
        require(coast.state.forwardElevation < released, "Opposite tilt never continues the previous direction")
        let beforeDuplicate = coast.state.forwardElevation
        coast.step(stick: SIMD2(0,-1), mode: 1, at: coast.time)
        require(coast.state.forwardElevation == beforeDuplicate, "Duplicate drawable timestamp never tilts twice")

        var entry = ContinuousRun()
        for _ in 0..<60 { entry.step(stick: SIMD2(0,1), mode: 1) }
        require(entry.state.forwardElevation == 0, "Held vertical stick on entry cannot tilt")
        entry.step(mode: 1)
        for _ in 0..<10 { entry.step(stick: SIMD2(0,1), mode: 1) }
        require(entry.state.forwardElevation > 0, "Centered stick arms tilting")

        for failure in 0..<7 {
            var run = ContinuousRun()
            run.step(mode: 1)
            for _ in 0..<20 { run.step(stick: SIMD2(0,1), mode: 1) }
            let before = run.state.forwardElevation
            switch failure {
            case 0: run.step(stick: SIMD2(0,1), mode: 1, enabled: false)
            case 1: run.source = .init(accessoryID: run.source.accessoryID, generation: 2)
            case 2: run.step(stick: nil, mode: 1)
            case 3: run.step(dt: 3, stick: SIMD2(0,1), mode: 1)
            case 4: run.step(stick: SIMD2(0,1), mode: 1, at: run.time - 0.1)
            case 5: run.step(stick: SIMD2(0,.nan), mode: 1)
            default: run.step(stick: SIMD2(0,1), mode: 1, tuning: .init(stickDegreesPerSecond: 60))
            }
            let tuning: EngineSenseLocomotion.Tuning = failure == 6 ? .init(stickDegreesPerSecond: 60) : .init()
            for _ in 0..<30 { run.step(stick: SIMD2(0,1), mode: 1, tuning: tuning) }
            require(run.state.forwardElevation == before, "State loss \(failure) keeps the view but cancels the held tilt")
            run.step(mode: 1, tuning: tuning)
            run.step(stick: SIMD2(0,1), mode: 1, tuning: tuning)
            require(run.state.forwardElevation > before, "Tilt resumes only after a centered stick \(failure)")
        }

        var recenter = ContinuousRun()
        recenter.step(mode: 1)
        for _ in 0..<20 { recenter.step(stick: SIMD2(1,1), mode: 1) }
        let recenterGeneration = recenter.state.generation
        recenter.step(stick: SIMD2(0,1), mode: 1, recenter: 1)
        for _ in 0..<30 { recenter.step(stick: SIMD2(0,1), mode: 1, recenter: 1) }
        require(recenter.state.forwardElevation == 0 && recenter.state.yaw == 0 && recenter.state.generation != recenterGeneration,
                "Recenter levels and faces forward, then requires a centered stick")

        var physical = ContinuousRun()
        physical.step(mode: 0)
        for _ in 0..<60 { physical.step(stick: SIMD2(0,1), mode: 0) }
        for _ in 0..<60 { physical.step(stick: SIMD2(1,1), mode: 0) }
        require(physical.state.forwardElevation == 0 && physical.state.yaw == 0, "Physical-only ignores both stick axes")
        for snap in [30, 45] {
            var run = ContinuousRun()
            run.step(mode: 1)
            for _ in 0..<20 { run.step(stick: SIMD2(0,1), mode: 1) }
            require(run.state.forwardElevation > 0, "Smooth tilt before choosing snap \(snap)")
            run.step(mode: snap)
            require(run.state.forwardElevation == 0, "Choosing \(snap) degree snap levels the view")
            run.step(stick: SIMD2(0,1), mode: snap); run.step(mode: snap); run.step(stick: SIMD2(0,-1), mode: snap)
            require(run.state.forwardElevation == 0 && run.state.yaw == 0, "Snap \(snap) vertical flick neither tilts nor turns")
            run.step(mode: snap); run.step(stick: SIMD2(1,0), mode: snap)
            require(abs(run.state.yaw - Float(snap) * degree) < 0.0001, "Snap \(snap) keeps its exact angle")
        }
        var modeSwap = ContinuousRun()
        modeSwap.step(mode: 1)
        for _ in 0..<20 { modeSwap.step(stick: SIMD2(0,1), mode: 1) }
        let keptPitch = modeSwap.state.forwardElevation
        modeSwap.step(mode: 2)
        require(modeSwap.state.forwardElevation == keptPitch, "Switching between continuous modes keeps the tilt")

        // Point turning and physical-head inhibition still work while tilted.
        var tiltedEdge = ContinuousRun()
        tiltedEdge.step(at: 0)
        for frame in 1...27 { tiltedEdge.step(stick: SIMD2(0,1), at: Double(frame) / 90) }
        let edgePitch = tiltedEdge.state.forwardElevation
        tiltedEdge.step(at: 0.31)
        tiltedEdge.step(aim: 1, at: 0.32)
        for frame in 1...180 { tiltedEdge.step(aim: 1, at: 0.32 + Double(frame) / 90) }
        require(abs(tiltedEdge.state.yaw - ramp(45, 1.82)) < 0.00001 && tiltedEdge.state.forwardElevation == edgePitch,
                "Edge turning while tilted keeps its normal speed and leaves tilt unchanged")
        var tiltedHead = ContinuousRun()
        tiltedHead.step(at: 0)
        for frame in 1...27 { tiltedHead.step(stick: SIMD2(0,1), at: Double(frame) / 90) }
        for frame in 1...720 {
            let degrees = Float(frame) / 90 * 30
            tiltedHead.step(aim: min(1, degrees / 45), head: -degrees * degree, at: 0.31 + Double(frame) / 90)
        }
        require(tiltedHead.state.yaw == 0, "Physical head turn while tilted induces no virtual turn")

        // One rigid transform: yaw about vertical after the bounded tilt.
        var shared = ContinuousRun()
        shared.step(mode: 1)
        for _ in 0..<40 { shared.step(stick: SIMD2(0.9,0.9), mode: 1) }
        let state = shared.state
        let basis = state.panoramaFromNeutral
        require(EngineImmersiveOrientation.isRigidPose(basis), "Yaw and tilt remain a rigid rotation")
        require(abs(basis.columns.0.y) < 0.000001, "Tilt adds no roll for the recentred forward view")
        let gaze = -simd_make_float3(basis.columns.2)
        require(near(gaze, SIMD3(sin(state.yaw) * cos(state.forwardElevation), sin(state.forwardElevation), -cos(state.yaw) * cos(state.forwardElevation))),
                "Forward gaze is turned right and tilted up by exactly the stored angles")
        var cycle = ContinuousRun()
        cycle.step(mode: 1, at: 0)
        var frame = 0
        for round in 0..<200 {
            let stick: SIMD2<Float> = [SIMD2(1,1), SIMD2(-1,-1), SIMD2(1,-1), SIMD2(-1,1)][round % 4]
            for _ in 0..<20 { frame += 1; cycle.step(stick: stick, mode: 1, at: Double(frame) / 90) }
            frame += 1; cycle.step(mode: 1, at: Double(frame) / 90)
        }
        let cycled = cycle.state.panoramaFromNeutral
        require(EngineImmersiveOrientation.isRigidPose(cycled) && abs(cycled.columns.0.y) < 0.000001,
                "Long mixed turning and tilting never accumulates roll")

        var heading = state
        let neutralLook = simd_float4x4(simd_quatf(angle: -30 * degree, axis: SIMD3(0,1,0)))
        require(abs(heading.horizontalHeadYaw(basis * neutralLook) - (state.yaw + 30 * degree)) < 0.0001,
                "Movement heading is physical heading plus virtual yaw, unaffected by tilt")
        for up: Float in [60, 80, 89] {
            let look = simd_float4x4(simd_quatf(angle: up * degree, axis: SIMD3(1,0,0)))
            require(abs(heading.horizontalHeadYaw(basis * look) - state.yaw) < 0.0001,
                    "Looking up \(up) degrees while tilted never flips movement")
        }
        var hand = matrix_identity_float4x4; hand.columns.3 = SIMD4(0.3,-0.2,-0.5,1)
        let still = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: basis, panoramaFromHead: basis)!
        let rotated = simd_make_float3(basis * SIMD4(0.3,-0.2,-0.5,0))
        require(near(still.position, EngineSenseLocomotion.cameraVector(rotated)),
                "Tilt moves the held weapon with the view, not relative to it")
        require(near(still.forward, EngineSenseLocomotion.cameraVector(gaze)),
                "A gun pointing at recentred forward aims where the tilted view looks")
        for degrees in stride(from: -180, through: 180, by: 30) {
            let look = basis * simd_float4x4(simd_quatf(angle: Float(degrees) * degree, axis: SIMD3(0,1,0)))
            let sample = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: basis, panoramaFromHead: look)!
            require(near(sample.forward, still.forward), "Physical head turns while tilted leave a fixed gun fixed")
        }
    }

    /// Current-facing tilt, deliberate-yaw movement authority and the
    /// bounded shared basis. Expected values come from the stick ramp
    /// formula, the physical pose or closed-form rotations, never from the
    /// stored decomposition.
    static func headingChecks() {
        let degree = Float.pi / 180, limit = EngineSenseLocomotion.pitchLimit
        let upAxis = SIMD3<Float>(0, 1, 0)
        func ramp(_ degreesPerSecond: Float, _ t: Double) -> Float {
            Float(Double(degreesPerSecond * degree) * (t - 0.12 * (1 - exp(-t / 0.12))))
        }
        func wrap(_ a: Float) -> Float { a.remainder(dividingBy: 2 * .pi) }
        func same(_ a: Float, _ b: Float, _ e: Float = 0.00005) -> Bool { abs(wrap(a - b)) < e }
        /// Physical head: heading (positive right), elevation, then roll about the gaze.
        func pose(_ heading: Float, _ elevation: Float = 0, _ roll: Float = 0) -> simd_float4x4 {
            simd_float4x4(simd_quatf(angle: -heading * degree, axis: upAxis)
                * simd_quatf(angle: elevation * degree, axis: SIMD3(1, 0, 0))
                * simd_quatf(angle: -roll * degree, axis: SIMD3(0, 0, 1)))
        }
        func gaze(_ m: simd_float4x4) -> SIMD3<Float> { -simd_make_float3(m.columns.2) }
        func elevation(_ m: simd_float4x4) -> Float { asin(max(-1, min(1, gaze(m).y))) }
        func azimuth(_ m: simd_float4x4) -> Float { let g = gaze(m); return atan2(g.x, -g.z) }
        func bank(_ m: simd_float4x4) -> Float { atan2(m.columns.0.y, m.columns.1.y) }
        /// Head roll measured against the gaze's own vertical plane.
        func gazeRoll(_ m: simd_float4x4) -> Float {
            let g = gaze(m), r = simd_normalize(SIMD3(-g.z, 0, g.x)), planeUp = simd_cross(r, g)
            let right = simd_make_float3(m.columns.0)
            return atan2(simd_dot(right, planeUp), simd_dot(right, r))
        }
        func equal(_ a: simd_float4x4, _ b: simd_float4x4, _ e: Float = 0.00001) -> Bool {
            (0..<4).allSatisfy { simd_length(a[$0] - b[$0]) < e }
        }
        func bitwise(_ a: simd_float4x4, _ b: simd_float4x4) -> Bool { (0..<4).allSatisfy { a[$0] == b[$0] } }
        /// A stick hold from rest with the head held still: one centred frame, then input.
        func hold(_ run: inout ContinuousRun, _ stick: SIMD2<Float>, frames: Int, heading: Float,
                  elevation: Float = 0, roll: Float = 0, mode: Int = 1) {
            let head = pose(heading, elevation, roll)
            run.step(mode: mode, aim: 0, head: heading * degree, pose: head)
            for _ in 0..<frames { run.step(stick: stick, mode: mode, aim: 0, head: heading * degree, pose: head) }
        }
        /// What EngineImmersive publishes as movement_yaw/head_yaw for this head.
        func movement(_ run: ContinuousRun, heading: Float, elevation: Float = 0) -> Float {
            var state = run.state
            return state.horizontalHeadYaw(state.panoramaFromNeutral * pose(heading, elevation))
        }

        // Up/down at any physical facing raises/lowers that gaze, with no bank,
        // no azimuth change and no movement turn.
        for heading: Float in [0, 90, -90, 180] {
            for direction: Float in [1, -1] {
                var run = ContinuousRun()
                hold(&run, SIMD2(0, direction), frames: 27, heading: heading)
                let view = run.state.panoramaFromNeutral * pose(heading)
                require(abs(elevation(view) - direction * ramp(90, 27.0 / 90)) < 0.00001,
                        "Vertical \(direction) at heading \(heading) changes that gaze's elevation by the stick integral")
                require(abs(bank(view)) < 0.00001, "Vertical \(direction) at heading \(heading) adds no bank")
                require(same(azimuth(view), heading * degree), "Vertical \(direction) at heading \(heading) keeps the gaze azimuth")
                require(run.state.yaw == 0 && same(movement(run, heading: heading), heading * degree),
                        "Vertical \(direction) at heading \(heading) never turns movement")
            }
        }

        // Reviewer's closed form through real stick frames: Rx(a1), turn the
        // head 90 degrees left, tilt a2 about the current level right (-Z).
        var witness = ContinuousRun()
        hold(&witness, SIMD2(0, 1), frames: 40, heading: 0)
        let a1 = ramp(90, 40.0 / 90)
        require(equal(witness.state.panoramaFromNeutral, simd_float4x4(simd_quatf(angle: a1, axis: SIMD3(1, 0, 0)))),
                "Facing forward, vertical input is Rx(stick integral)")
        let frozen = witness.state.panoramaFromNeutral
        for step in 1...30 {
            let heading = Float(step) * -3
            witness.step(mode: 1, aim: 0, head: heading * degree, pose: pose(heading, Float(step) * 0.5, Float(step) * 0.3))
        }
        require(bitwise(witness.state.panoramaFromNeutral, frozen) && witness.state.yaw == 0,
                "Turning, raising and rolling the head with a centred stick leaves the shared basis bitwise unchanged")
        let left = pose(-90)
        let beforeView = witness.state.panoramaFromNeutral * left
        let beforeMove = movement(witness, heading: -90)
        require(near(gaze(beforeView), SIMD3(-1, 0, 0)) && same(beforeMove, -90 * degree),
                "Before: gaze left and movement = physical heading + no commanded yaw")
        hold(&witness, SIMD2(0, 1), frames: 40, heading: -90)
        let a2 = ramp(90, 40.0 / 90)
        let closed = simd_quatf(angle: -a2, axis: SIMD3(0, 0, 1)) * simd_quatf(angle: a1, axis: SIMD3(1, 0, 0))
        require(equal(witness.state.panoramaFromNeutral, simd_float4x4(closed)),
                "Turned left, vertical input composes exactly Rz(-a2) * Rx(a1)")
        let afterView = witness.state.panoramaFromNeutral * left
        require(near(gaze(afterView), SIMD3(-cos(a2), sin(a2), 0)), "The left gaze rises in its own vertical plane")
        require(same(azimuth(afterView), azimuth(beforeView)), "Vertical-only input keeps the current gaze azimuth")
        require(witness.state.yaw == 0 && same(movement(witness, heading: -90), beforeMove),
                "Vertical-only input never turns movement, despite the basis twist")
        require(abs(2 * atan2(closed.imag.y, closed.real)) > 5 * degree,
                "The witness basis holds a real twist about up that folding would have turned into movement")
        require(abs(witness.state.tilt - acos(cos(a1) * cos(a2))) < 0.00001 && witness.state.tilt < limit,
                "Tilt is acos(cos a1 cos a2), inside the limit")

        // Independent mixed-heading witness with deliberate yaw in between.
        var mixed = ContinuousRun()
        hold(&mixed, SIMD2(0, 1), frames: 30, heading: 0)
        for step in 1...20 { let heading = Float(step) * 3; mixed.step(mode: 1, aim: 0, head: heading * degree, pose: pose(heading)) }
        let azimuth0 = azimuth(mixed.state.panoramaFromNeutral * pose(60))
        require(same(movement(mixed, heading: 60), 60 * degree), "Turned right 60: movement = physical heading")
        hold(&mixed, SIMD2(0, 1), frames: 20, heading: 60)
        require(same(azimuth(mixed.state.panoramaFromNeutral * pose(60)), azimuth0) && mixed.state.yaw == 0 &&
                same(movement(mixed, heading: 60), 60 * degree), "At 60 degrees, more tilt moves neither gaze azimuth nor movement")
        hold(&mixed, SIMD2(1, 0), frames: 30, heading: 60)
        let commanded = ramp(180, 30.0 / 90)
        let azimuth1 = azimuth(mixed.state.panoramaFromNeutral * pose(60))
        require(same(mixed.state.yaw, commanded) && same(movement(mixed, heading: 60), 60 * degree + commanded) &&
                same(azimuth1, azimuth0 + commanded), "Deliberate yaw alone turns movement and view by the stick integral")
        hold(&mixed, SIMD2(0, -1), frames: 20, heading: 60)
        require(same(azimuth(mixed.state.panoramaFromNeutral * pose(60)), azimuth1) && same(mixed.state.yaw, commanded) &&
                same(movement(mixed, heading: 60), 60 * degree + commanded), "Tilting back down keeps the commanded heading")

        // Simultaneous yaw+tilt against a yaw-only reference.
        var both = ContinuousRun(), turnOnly = ContinuousRun()
        hold(&both, SIMD2(1, 1), frames: 36, heading: 60)
        hold(&turnOnly, SIMD2(1, 0), frames: 36, heading: 60)
        let turned = ramp(180, 36.0 / 90)
        require(both.state.yaw == turnOnly.state.yaw && same(both.state.yaw, turned),
                "Simultaneous tilt leaves the deliberate turn exactly the yaw-only turn")
        require(same(movement(both, heading: 60), movement(turnOnly, heading: 60)) &&
                same(movement(both, heading: 60), 60 * degree + turned), "Movement authority is physical heading + commanded yaw")
        let bothView = both.state.panoramaFromNeutral * pose(60)
        require(abs(elevation(bothView) - ramp(90, 36.0 / 90)) < 0.00001 && abs(bank(bothView)) < 0.00001 &&
                same(azimuth(bothView), 60 * degree + turned), "Diagonal at 60 degrees tilts that gaze at the base rate, no bank")

        // Mixed turn and tilt are refresh-rate independent at a turned head.
        func mixedRun(fps: Int) -> ContinuousRun {
            var run = ContinuousRun()
            run.step(mode: 1, aim: 0, head: 90 * degree, pose: pose(90), at: 0)
            for frame in 1...(fps / 2) {
                run.step(stick: SIMD2(0.8, -0.9), mode: 1, aim: 0, head: 90 * degree, pose: pose(90),
                         at: Double(frame) / Double(fps))
            }
            return run
        }
        let mixedReference = mixedRun(fps: 90)
        let response: Float = (0.7 / 0.8) * (0.7 / 0.8)
        require(abs(elevation(mixedReference.state.panoramaFromNeutral * pose(90)) + response * ramp(90, 0.5)) < 0.00001,
                "Mixed input tilts the turned gaze down by the curved stick integral")
        for fps in [60, 120] {
            let sample = mixedRun(fps: fps)
            require(equal(sample.state.panoramaFromNeutral, mixedReference.state.panoramaFromNeutral) &&
                    abs(sample.state.yaw - mixedReference.state.yaw) < 0.00001, "Mixed turn and tilt at \(fps) Hz equal 90 Hz")
        }

        // No admitted input: bitwise freeze under head motion, lost input,
        // disabled tracking and mode churn between the two continuous modes.
        var still = witness
        let held = still.state.panoramaFromNeutral, heldYaw = still.state.yaw
        for frame in 0..<360 {
            let t = Float(frame)
            let heading = -180 + t
            let head = pose(heading, 60 * sin(t * 0.05), 30 * sin(t * 0.03))
            switch frame % 4 {
            case 0: still.step(mode: 1, aim: 0, head: heading * degree, pose: head)
            case 1: still.step(stick: nil, mode: 1, aim: 0, pose: head)
            case 2: still.step(stick: SIMD2(0, 1), mode: 1, enabled: false, aim: 0, pose: head)
            default: still.step(mode: 2, aim: 0, head: heading * degree, pose: head)
            }
        }
        require(bitwise(still.state.panoramaFromNeutral, held) && still.state.yaw == heldYaw,
                "No admitted input: the shared basis never follows the head")

        // The bound holds under repeated tilts at many headings, and is reached.
        var bounded = ContinuousRun()
        var worst: Float = 0
        for (index, heading) in ([0, 90, 180, -90, 45, -135, 30, 120, -60, 150] as [Float]).enumerated() {
            let head = pose(heading)
            bounded.step(mode: 1, aim: 0, head: heading * degree, pose: head)
            for _ in 0..<135 {
                bounded.step(stick: SIMD2(0, index % 3 == 2 ? -1 : 1), mode: 1, aim: 0, head: heading * degree, pose: head)
                worst = max(worst, bounded.state.tilt)
                require(EngineImmersiveOrientation.isRigidPose(bounded.state.panoramaFromNeutral),
                        "Repeated tilts keep a rigid, finite shared basis")
            }
        }
        require(worst <= limit + 0.00001 && worst > limit - 0.001, "Repeated tilts at different headings reach but never pass the bound")

        // Recenter, ordinary-pad (disabled), neutral arming and reconnect with a twisted basis.
        var reset = witness
        reset.step(stick: SIMD2(0, 1), mode: 1, recenter: 1, aim: 0, head: -90 * degree, pose: left)
        for _ in 0..<20 { reset.step(stick: SIMD2(0, 1), mode: 1, recenter: 1, aim: 0, head: -90 * degree, pose: left) }
        require(bitwise(reset.state.panoramaFromNeutral, matrix_identity_float4x4) && reset.state.yaw == 0 && reset.state.tilt == 0,
                "Recenter returns the exact identity basis and a held stick waits for centre")
        var ordinary = witness
        for _ in 0..<60 { ordinary.step(stick: SIMD2(0, 1), mode: 1, enabled: false, aim: 0, pose: left) }
        for _ in 0..<60 { ordinary.step(stick: SIMD2(1, 1), mode: 1, enabled: false, aim: 0, pose: left) }
        require(bitwise(ordinary.state.panoramaFromNeutral, witness.state.panoramaFromNeutral),
                "Disabled (ordinary gamepad) path never changes the basis")
        var reconnect = witness
        reconnect.source = .init(accessoryID: reconnect.source.accessoryID, generation: 2)
        for _ in 0..<30 { reconnect.step(stick: SIMD2(0, 1), mode: 1, aim: 0, head: -90 * degree, pose: left) }
        require(bitwise(reconnect.state.panoramaFromNeutral, witness.state.panoramaFromNeutral),
                "Reconnect with a held stick waits for centre")
        hold(&reconnect, SIMD2(0, 1), frames: 1, heading: -90)
        require(!bitwise(reconnect.state.panoramaFromNeutral, witness.state.panoramaFromNeutral), "Centred stick re-arms tilt")

        // Explicit level reset (choosing snap) drops tilt and residual twist
        // together, keeps commanded yaw, and re-aligns view with movement.
        var levelled = mixed
        let offFacing = levelled.state.panoramaFromNeutral * pose(-30)
        require(abs(wrap(azimuth(offFacing) - movement(levelled, heading: -30))) > 1 * degree,
                "Before reset, a tilted basis shows the world off the movement heading at another facing")
        levelled.step(mode: 30, aim: 0, head: 0, pose: pose(0))
        require(bitwise(levelled.state.panoramaFromNeutral, simd_float4x4(simd_quatf(angle: -levelled.state.yaw, axis: upAxis))) &&
                levelled.state.tilt == 0 && same(levelled.state.yaw, commanded), "Snap levels to the commanded-yaw basis exactly")
        for heading: Float in [0, 45, -120, 180] {
            require(same(azimuth(levelled.state.panoramaFromNeutral * pose(heading)), movement(levelled, heading: heading)),
                    "After the explicit reset, view azimuth equals movement at heading \(heading)")
        }

        // Point-to-turn while tilted at a turned head keeps its half-speed
        // integral and leaves the tilt alone.
        var edgeTilt = ContinuousRun()
        hold(&edgeTilt, SIMD2(0, 1), frames: 27, heading: 90, mode: 2)
        let tiltBefore = edgeTilt.state.tilt
        let elevationBefore = elevation(edgeTilt.state.panoramaFromNeutral * pose(90))
        let start = edgeTilt.time + 1.0 / 90
        edgeTilt.step(mode: 2, aim: 0, head: 90 * degree, pose: pose(90), at: start)
        edgeTilt.step(mode: 2, aim: 1, head: 90 * degree, pose: pose(90), at: start + 0.01)
        for frame in 1...180 {
            edgeTilt.step(mode: 2, aim: 1, head: 90 * degree, pose: pose(90), at: start + 0.01 + Double(frame) / 90)
        }
        require(abs(edgeTilt.state.yaw - ramp(45, 1.82)) < 0.00001, "Point-to-turn while tilted keeps its half-speed integral")
        require(abs(edgeTilt.state.tilt - tiltBefore) < 0.000001 &&
                abs(elevation(edgeTilt.state.panoramaFromNeutral * pose(90)) - elevationBefore) < 0.00001 &&
                same(movement(edgeTilt, heading: 90), 90 * degree + edgeTilt.state.yaw),
                "Point-to-turn is yaw only: tilt and gaze elevation unchanged, movement follows the commanded turn")

        // Movement inverse at physical poles, and the weapon in the same basis.
        var poles = witness.state
        _ = poles.horizontalHeadYaw(poles.panoramaFromNeutral * left)
        for up: Float in [60, 80, 89] {
            require(same(poles.horizontalHeadYaw(poles.panoramaFromNeutral * pose(-90, up)), -90 * degree),
                    "Looking up \(up) degrees through a twisted tilt keeps the physical movement heading")
        }
        var hand = matrix_identity_float4x4; hand.columns.3 = SIMD4(0.2, -0.2, -0.4, 1)
        let basis = witness.state.panoramaFromNeutral
        let weapon = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: basis,
                                                       panoramaFromHead: basis * left)!
        require(near(weapon.forward, EngineSenseLocomotion.cameraVector(gaze(basis))),
                "The weapon ray uses the same full basis as the view")

        // Reclined and rolled heads away from the pole: elevation changes by
        // exactly the integral, azimuth and roll about the gaze plane do not.
        for (raised, roll) in [(30, 0), (-20, 0), (10, 15), (25, -20)] as [(Float, Float)] {
            var run = ContinuousRun()
            let head = pose(-60, raised, roll)
            let before = run.state.panoramaFromNeutral * head
            hold(&run, SIMD2(0, 1), frames: 18, heading: -60, elevation: raised, roll: roll)
            let after = run.state.panoramaFromNeutral * head
            require(abs(elevation(after) - elevation(before) - ramp(90, 0.2)) < 0.00002,
                    "Head raised \(raised) rolled \(roll): elevation follows the stick integral")
            require(same(azimuth(after), azimuth(before)) && abs(gazeRoll(after) - gazeRoll(before)) < 0.00002,
                    "Head raised \(raised) rolled \(roll): no azimuth change and no input-induced roll")
        }

        // Through an already tilted, twisted basis the actual raised head (not
        // a level head at the same heading) defines the gaze plane to tilt in.
        for (heading, raised, roll) in [(45, 40, 0), (-150, 25, 10), (120, -30, 0)] as [(Float, Float, Float)] {
            var run = witness
            let head = pose(heading, raised, roll)
            let before = run.state.panoramaFromNeutral * head
            hold(&run, SIMD2(0, 1), frames: 10, heading: heading, elevation: raised, roll: roll)
            let after = run.state.panoramaFromNeutral * head
            require(abs(elevation(after) - elevation(before) - ramp(90, 10.0 / 90)) < 0.00002 &&
                    same(azimuth(after), azimuth(before)) && abs(gazeRoll(after) - gazeRoll(before)) < 0.00002 &&
                    run.state.yaw == 0, "Tilted basis, head \(heading)/\(raised)/\(roll): the actual gaze rises in place")
        }

        // Through the pole: finite, bounded, and the axis turns continuously.
        // Exact zero roll is claimed only for an unrolled head here.
        for roll: Float in [0, 20, 85] {
            var run = ContinuousRun()
            run.step(mode: 1, aim: 0, head: nil, pose: pose(40, 78, roll))
            var previous = run.state.panoramaFromNeutral
            var previousAxis: SIMD3<Float>?
            var closest: Float = 1
            for frame in 0..<96 {
                let head = pose(40, 78 + Float(frame) * 0.25, roll)
                run.step(stick: SIMD2(0, 1), mode: 1, aim: 0, head: nil, pose: head)
                let current = run.state.panoramaFromNeutral
                require(EngineImmersiveOrientation.isRigidPose(current) && run.state.tilt <= limit + 0.00001,
                        "Near the pole with roll \(roll): finite rigid basis inside the bound")
                let step = simd_quatf(current * previous.inverse)
                let s = simd_length(step.imag)
                if s > 0.0001 {
                    let axis = step.imag / s * (step.real < 0 ? -1 : 1)
                    require(abs(axis.y) < 0.001, "Near the pole with roll \(roll): the tilt axis stays level")
                    if let previousAxis { closest = min(closest, simd_dot(previousAxis, axis)) }
                    previousAxis = axis
                }
                previous = current
            }
            require(run.state.tilt > 30 * degree, "Near the pole with roll \(roll): vertical input is still admitted")
            if roll == 0 {
                require(closest > cos(0.5 * degree), "Unrolled head through vertical: the tilt axis never flips or swings")
            } else if roll == 20 {
                require(closest > cos(6 * degree), "Rolled head through vertical: the tilt axis turns continuously (closest \(acos(min(1, closest)) / degree) deg)")
            }
        }
    }

    static func main() {
        var state = EngineSenseLocomotion()
        let source = EngineSenseMenuTrigger.Source(accessoryID: UUID(), generation: 1)
        func update(_ stick: SIMD2<Float>?, mode: Int = 30, enabled: Bool = true,
                    recenter: Int = 0, owner: EngineSenseMenuTrigger.Source? = nil) -> simd_float4x4 {
            state.update(enabled: enabled, stick: stick, source: owner ?? source, mode: mode, recenter: recenter)
        }
        _ = update(SIMD2(1,0)); require(state.yaw == 0, "Held stick on entry cannot snap")
        _ = update(.zero); _ = update(SIMD2(1,0)); require(abs(state.yaw - .pi/6) < 0.0001, "Right snap is exactly thirty degrees")
        let gaze = -simd_make_float3(state.panoramaFromNeutral.columns.2)
        require(near(gaze, SIMD3(0.5,0,-sqrt(0.75))), "Right snap looks right in panorama coordinates")
        for _ in 0..<20 { _ = update(SIMD2(1,0)) }
        require(abs(state.yaw - .pi/6) < 0.0001, "Holding never repeats")
        _ = update(SIMD2(0.4,0)); _ = update(SIMD2(1,0)); require(abs(state.yaw - .pi/6) < 0.0001, "Partial release never rearms")
        _ = update(.zero); _ = update(SIMD2(-1,0)); require(abs(state.yaw) < 0.0001, "Opposite deliberate push cancels turn")
        _ = update(.zero, mode: 45); _ = update(SIMD2(1,0), mode: 45); require(abs(state.yaw - .pi/4) < 0.0001, "Forty five degree choice works")
        _ = update(.zero, mode: 0); _ = update(SIMD2(1,0), mode: 0); require(abs(state.yaw - .pi/4) < 0.0001, "Physical-only mode ignores stick")
        _ = update(.zero); _ = update(SIMD2(1,1)); require(abs(state.yaw - .pi/4) < 0.0001, "Vertical or diagonal push is not accidental turn")
        _ = update(nil); _ = update(SIMD2(1,0)); require(abs(state.yaw - .pi/4) < 0.0001, "Tracking loss requires neutral again")
        _ = update(.zero); _ = update(SIMD2(.nan,0)); _ = update(SIMD2(1,0)); require(abs(state.yaw - .pi/4) < 0.0001, "Nonfinite stick cannot arm")
        _ = update(.zero); _ = update(SIMD2(1,0), recenter: 1); require(state.yaw == 0, "Recenter resets virtual turn and requires release")
        _ = update(.zero, recenter: 1); _ = update(SIMD2(1,0), recenter: 1, owner: .init(accessoryID: source.accessoryID, generation: 2));
        require(state.yaw == 0, "Same controller reconnect requires release")
        for _ in 0..<24 { _ = update(.zero); _ = update(SIMD2(1,0)) }
        require(abs(state.yaw) < 0.0001, "Two full turns wrap without accumulated drift")
        let identity = matrix_identity_float4x4
        var hand = identity; hand.columns.3 = SIMD4(0.3,-0.2,-0.5,1)
        let headTurn = simd_float4x4(simd_quatf(angle: -.pi/2, axis: SIMD3(0,1,0)))
        require(abs(state.horizontalHeadYaw(headTurn) - .pi/2) < 0.0001, "Movement follows physical right head heading")
        let straightUp = simd_float4x4(simd_quatf(angle: .pi/2, axis: SIMD3(1,0,0)))
        require(abs(state.horizontalHeadYaw(straightUp) - .pi/2) < 0.0001, "Looking straight up preserves stable movement heading")
        let aim = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: identity, panoramaFromHead: headTurn)!
        require(near(aim.position, SIMD3(0.5,-0.3,-0.2)), "Metres and camera axes are mapped once")
        require(near(aim.forward, SIMD3(1,0,0)), "Looking sideways does not rotate independent gun ray")
        require(near(aim.up, SIMD3(0,0,1)), "Neutral weapon up preserved")
        var grip = simd_float4x4(simd_quatf(angle: .pi/3, axis: SIMD3(1,0,0)))
        grip.columns.3 = SIMD4(0.25,-0.3,-0.35,1)
        let held = EngineSenseLocomotion.weaponPose(originFromAim: hand, originFromGrip: grip,
            panoramaFromOrigin: identity, panoramaFromHead: headTurn)!
        require(near(held.position, SIMD3(0.35,-0.25,-0.3)), "Weapon is anchored to controller handle, not forward ray origin")
        require(near(held.forward, aim.forward) && near(held.up, aim.up), "Grip orientation never corrupts intended aim or roll")
        let turnedGrip = EngineSenseLocomotion.weaponPose(originFromAim: hand, originFromGrip: grip,
            panoramaFromOrigin: headTurn, panoramaFromHead: headTurn)!
        require(near(turnedGrip.position, SIMD3(-0.25,-0.35,-0.3)), "Snap turn rotates handle and aim through the same basis")
        var invalidGrip = grip; invalidGrip.columns.1.y = .nan
        require(EngineSenseLocomotion.weaponPose(originFromAim: hand, originFromGrip: invalidGrip,
            panoramaFromOrigin: identity, panoramaFromHead: identity) == nil, "Invalid handle does not fall back to ray origin")
        var shiftedHead = headTurn; shiftedHead.columns.3 = SIMD4(0.1,0.2,0.1,1)
        let shifted = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: identity, panoramaFromHead: shiftedHead)!
        require(near(shifted.position, SIMD3(0.6,-0.2,-0.4)), "Hand minus current head translation, no fake roomscale movement")
        let snapped = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: headTurn, panoramaFromHead: headTurn)!
        require(near(snapped.forward, SIMD3(0,-1,0)), "Virtual right turn rotates both view and ray right")
        let rolledHand = hand * simd_float4x4(simd_quatf(angle: .pi/2, axis: SIMD3(0,0,1)))
        let rolled = EngineSenseLocomotion.weaponPose(originFromAim: rolledHand, panoramaFromOrigin: identity, panoramaFromHead: identity)!
        require(near(rolled.up, SIMD3(0,1,0)), "Controller roll survives weapon basis conversion")
        var invalid = hand; invalid.columns.0.x = .nan
        require(EngineSenseLocomotion.weaponPose(originFromAim: invalid, panoramaFromOrigin: identity, panoramaFromHead: identity) == nil, "Bad transforms never reach native ray")
        for degree in stride(from: -360, through: 360, by: 15) {
            let look = simd_float4x4(simd_quatf(angle: Float(degree) * .pi / 180, axis: SIMD3(0,1,0)))
            let sample = EngineSenseLocomotion.weaponPose(originFromAim: hand, panoramaFromOrigin: identity, panoramaFromHead: look)!
            require(near(sample.forward, aim.forward), "Physical full-circle head turns leave fixed hand ray fixed")
        }
        continuousChecks()
        pitchChecks()
        headingChecks()
        // A paired right controller may have no pose while the left hand
        // throws. Preserve its identity but disable turning until neutral.
        var missingRight = EngineSenseLocomotion()
        _ = missingRight.update(enabled: true, stick: .zero, source: source, mode: 45, recenter: 1)
        let ownerGeneration = missingRight.generation
        _ = missingRight.update(enabled: false, stick: nil, source: source, mode: 45, recenter: 1)
        require(missingRight.generation == ownerGeneration, "Missing right pose does not invalidate a fresh left grenade source")
        _ = missingRight.update(enabled: true, stick: SIMD2(1,0), source: source, mode: 45, recenter: 1)
        require(missingRight.yaw == 0, "Right pose recovery still needs neutral before turning")
        _ = missingRight.update(enabled: false, stick: nil, source: nil, mode: 45, recenter: 1)
        require(missingRight.generation != ownerGeneration, "Actual ownership loss still invalidates grenade context")
        print("PASS \(checks) Sense locomotion and independent pose checks")
    }
}
