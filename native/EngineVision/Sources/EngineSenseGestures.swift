import simd

/// Deliberate quick actions, not a physical magazine or world-object system.
/// The caller supplies fresh, owned Sense poses before applying virtual yaw.
struct EngineSenseGestures {
    enum Action: String { case reload, swap, pickup, melee }
    enum Phase: String { case idle, needsRelease, armed, reloading, holstering, swapping, reaching, swinging, completed }

    struct Configuration: Equatable {
        var enabled = true
        var reload = true
        var swap = true
        var pickup = true
        var melee = true
    }
    struct Source: Equatable {
        var session: UInt64
        var controllerGeneration: UInt64
        var inputGeneration: UInt64
        var left: EngineSenseMenuTrigger.Source
        var right: EngineSenseMenuTrigger.Source
        var recenter: Int
    }
    struct Frame {
        var timestamp: Double
        var source: Source
        var eligible: Bool
        var clutch: Bool
        var supportGrip: Bool
        var blockingButtons: Bool
        var triggers: SIMD2<Float>
        var leftStick: SIMD2<Float>
        var rightStick: SIMD2<Float>
        var originFromHead: simd_float4x4
        var originFromLeftGrip: simd_float4x4
        var originFromRightGrip: simd_float4x4
        /// Physical torso basis aligned with the current physical heading:
        /// Ry(+physicalHeadYaw) * pose.neutralFromOrigin, before virtual yaw.
        /// A near-vertical/unstable physical heading must make eligible false.
        /// This rotation and the head origin freeze when a motion starts.
        var bodyFromOrigin: simd_float4x4
    }
    struct Output {
        var action: Action?
        var sequence: UInt64
        var intentActive: Bool
        var phase: Phase
    }
    private enum Motion { case choosing, reload, swapIn, swapOut, pickup, finished }
    private struct Attempt {
        var started: Double
        var bodyRotation: simd_float3x3
        var headStart: SIMD3<Float>
        var leftStart: SIMD3<Float>
        var rightStart: SIMD3<Float>
        var previousLeft: SIMD3<Float>
        var previousRight: SIMD3<Float>
        var tapStart: SIMD3<Float>
        var tapStartDistance: Float
        var tapCount = 0
        var tapContactSince: Double?
        var firstTapAt: Double?
        var needsTapWithdrawal = false
        var motion = Motion.choosing
        var holsterPosition: SIMD3<Float>?
        var goalSince: Double?
    }
    private enum NaturalMotion { case waiting, ready, swingLeft, swingRight, holster }
    private struct NaturalAttempt {
        var rotation: simd_float3x3
        var head: SIMD3<Float>
        var leftStart: SIMD3<Float>
        var rightStart: SIMD3<Float>
        var previousLeft: SIMD3<Float>
        var previousRight: SIMD3<Float>
        var started: Double
        var stableSince: Double?
        var goalSince: Double?
        var peakSpeed: Float = 0
        var motion = NaturalMotion.waiting
    }
    private var natural: NaturalAttempt?
    private var naturalCooldownUntil: Double = 0
    private var source: Source?
    private var configuration = Configuration()
    private var lastTimestamp: Double?
    private var released = false
    private var attempt: Attempt?
    private(set) var sequence: UInt64 = 0

    mutating func update(_ frame: Frame?, configuration: Configuration = .init()) -> Output {
        if self.configuration != configuration {
            invalidate(); self.configuration = configuration
        }
        guard configuration.enabled, configuration.reload || configuration.swap || configuration.pickup || configuration.melee,
              let frame, frame.eligible, Self.valid(frame) else {
            invalidate(); return output(.idle)
        }
        if source != frame.source { invalidate(); source = frame.source; naturalCooldownUntil = 0 }
        // Action buttons, firing, grenade hold and right-stick turning win.
        // In particular Create+R3 is available to the caller for scoreboard.
        guard !frame.blockingButtons, !frame.supportGrip,
              frame.triggers.x < 0.25, frame.triggers.y < 0.25,
              (!frame.clutch || simd_length(frame.leftStick) < 0.20), simd_length(frame.rightStick) < 0.20 else {
            invalidate(); return output(.idle)
        }
        if let lastTimestamp, frame.timestamp == lastTimestamp {
            return output(frame.clutch ? currentPhase : naturalPhase, intent: frame.clutch || naturalIntent)
        }
        let elapsed = lastTimestamp.map { frame.timestamp - $0 } ?? 0
        if elapsed < 0 || elapsed > 0.10 {
            invalidate(); source = frame.source
            if elapsed < 0 { naturalCooldownUntil = 0 }
        }
        lastTimestamp = frame.timestamp
        if !frame.clutch {
            attempt = nil; released = true
            return updateNatural(frame, elapsed: elapsed)
        }
        natural = nil
        guard released else { return output(.needsRelease, intent: true) }
        let head = simd_make_float3(frame.originFromHead.columns.3)
        if attempt == nil {
            let b = frame.bodyFromOrigin
            let rotation = simd_float3x3(simd_make_float3(b.columns.0),
                                        simd_make_float3(b.columns.1), simd_make_float3(b.columns.2))
            let left = rotation * (simd_make_float3(frame.originFromLeftGrip.columns.3) - head)
            let right = rotation * (simd_make_float3(frame.originFromRightGrip.columns.3) - head)
            guard Self.inFront(left), Self.inFront(right) else { return cancel() }
            attempt = Attempt(started: frame.timestamp, bodyRotation: rotation,
                headStart: head, leftStart: left, rightStart: right,
                previousLeft: left, previousRight: right,
                tapStart: left, tapStartDistance: simd_distance(left, right))
            return output(.armed, intent: true)
        }
        guard var attempt else { return cancel() }
        if case .finished = attempt.motion { return output(.completed, intent: true) }
        guard frame.timestamp - attempt.started <= 3,
              simd_distance(head, attempt.headStart) <= 0.10 else { return cancel() }
        // Freeze BOTH origin and rotation. Subtracting a moving head would
        // turn a lean into fake hand travel. Whole-body motion is cancelled
        // before it can meet any gesture's minimum travel distance.
        let left = attempt.bodyRotation * (simd_make_float3(frame.originFromLeftGrip.columns.3) - attempt.headStart)
        let right = attempt.bodyRotation * (simd_make_float3(frame.originFromRightGrip.columns.3) - attempt.headStart)
        guard Self.inFront(left), Self.inTrackedReach(right), elapsed > 0,
              simd_distance(left, attempt.previousLeft) <= Float(elapsed) * 3 + 0.02,
              simd_distance(right, attempt.previousRight) <= Float(elapsed) * 3 + 0.02 else { return cancel() }
        attempt.previousLeft = left; attempt.previousRight = right
        let leftTravel = left - attempt.leftStart
        let rightTravel = right - attempt.rightStart
        // A tolerant neighborhood around the tracked gun grip; physical
        // controller collision is neither necessary nor measured here.
        let gunDistance = simd_distance(left, right)
        let nearGun = gunDistance <= 0.14
        let rightSteady = simd_distance(right, attempt.rightStart) <= 0.12
        let forwardReach = -leftTravel.z
        if case .choosing = attempt.motion {
            // A partial deliberate motion claims the attempt. It cannot morph
            // into a different action while the same clutch press is held.
            if configuration.swap, attempt.rightStart.z <= -0.30, rightTravel.z >= 0.12 {
                attempt.motion = .swapIn
            } else if configuration.reload, rightSteady, attempt.tapStartDistance >= 0.23,
                      attempt.tapStartDistance - gunDistance >= 0.06, simd_length(leftTravel) >= 0.06 {
                attempt.motion = .reload
            } else if configuration.pickup, rightSteady, forwardReach >= 0.10,
                      forwardReach >= abs(leftTravel.y) * 1.2, !nearGun {
                attempt.motion = .pickup
            }
        }
        var action: Action?
        var inGoal = false
        switch attempt.motion {
        case .choosing: break
        case .reload:
            guard rightSteady, Self.inFront(right) else { return cancel() }
            if let first = attempt.firstTapAt, frame.timestamp - first > 1.2 { return cancel() }
            if attempt.needsTapWithdrawal {
                // Hysteresis prevents proximity jitter from counting as more
                // hits. A second tap requires a real withdrawal and approach.
                if gunDistance >= 0.23 {
                    attempt.needsTapWithdrawal = false
                    attempt.tapStart = left; attempt.tapStartDistance = gunDistance
                }
            } else if nearGun, attempt.tapStartDistance - gunDistance >= 0.08,
                      simd_distance(left, attempt.tapStart) >= 0.08 {
                if attempt.tapContactSince == nil { attempt.tapContactSince = frame.timestamp }
                if let since = attempt.tapContactSince, frame.timestamp - since >= 0.04 {
                    attempt.tapCount += 1; attempt.tapContactSince = nil
                    if attempt.tapCount == 2 { return complete(.reload, attempt: attempt) }
                    attempt.firstTapAt = frame.timestamp; attempt.needsTapWithdrawal = true
                }
            } else { attempt.tapContactSince = nil }
        case .swapIn:
            // The front edge of the shoulder zone remains visible to the
            // headset. Fully behind-the-back motion is optional and must
            // remain tracked; a missing frame always invalidates the attempt.
            if rightTravel.z >= 0.30, Self.atHolster(right) {
                if attempt.goalSince == nil { attempt.goalSince = frame.timestamp }
                if let since = attempt.goalSince, frame.timestamp - since >= 0.08 {
                    attempt.motion = .swapOut; attempt.holsterPosition = right
                    attempt.goalSince = nil
                }
            } else { attempt.goalSince = nil }
        case .swapOut:
            if let holster = attempt.holsterPosition {
                inGoal = Self.inFront(right) && right.z <= -0.30 &&
                    holster.z - right.z >= 0.25 && right.y >= -0.75
                action = .swap
            }
        case .pickup:
            guard rightSteady else { return cancel() }
            inGoal = forwardReach >= 0.20 && !nearGun && abs(leftTravel.x) <= 0.25
            action = .pickup
        case .finished: break
        }
        if inGoal {
            if attempt.goalSince == nil { attempt.goalSince = frame.timestamp }
            if let since = attempt.goalSince, frame.timestamp - since >= 0.12, let action {
                return complete(action, attempt: attempt)
            }
        } else if case .swapIn = attempt.motion {
            // Preserve the shoulder dwell independently of the return dwell.
        } else { attempt.goalSince = nil }
        self.attempt = attempt
        return output(currentPhase, intent: true)
    }

    /// No clutch for melee or swapping. Arming requires a settled front-ready
    /// pose; a completed or interrupted motion must settle again before retry.
    /// The physical basis freezes during the motion, so looking/turning cannot
    /// manufacture hand velocity. The support hand may remain on the rifle.
    private mutating func updateNatural(_ frame: Frame, elapsed: Double) -> Output {
        guard configuration.melee || configuration.swap else { natural = nil; return output(.armed) }
        let head = simd_make_float3(frame.originFromHead.columns.3)
        if natural == nil {
            let b = frame.bodyFromOrigin
            let rotation = simd_float3x3(simd_make_float3(b.columns.0), simd_make_float3(b.columns.1), simd_make_float3(b.columns.2))
            let left = rotation * (simd_make_float3(frame.originFromLeftGrip.columns.3) - head)
            let right = rotation * (simd_make_float3(frame.originFromRightGrip.columns.3) - head)
            natural = NaturalAttempt(rotation: rotation, head: head, leftStart: left, rightStart: right,
                previousLeft: left, previousRight: right, started: frame.timestamp)
            return output(.needsRelease)
        }
        guard var n = natural, elapsed > 0 else { return output(.needsRelease) }
        let left = n.rotation * (simd_make_float3(frame.originFromLeftGrip.columns.3) - n.head)
        let right = n.rotation * (simd_make_float3(frame.originFromRightGrip.columns.3) - n.head)
        let lv = (left - n.previousLeft) / Float(elapsed)
        let rv = (right - n.previousRight) / Float(elapsed)
        let speed = max(simd_length(lv), simd_length(rv))
        let b = frame.bodyFromOrigin
        let currentRotation = simd_float3x3(simd_make_float3(b.columns.0), simd_make_float3(b.columns.1), simd_make_float3(b.columns.2))
        let relativeRotation = currentRotation * n.rotation.transpose
        let rotationCosine = (relativeRotation.columns.0.x + relativeRotation.columns.1.y + relativeRotation.columns.2.z - 1) * 0.5
        // Carrying both hands through a physical body turn is not a bash.
        // A fresh front-ready sample rearms after the heading settles.
        guard Self.inTrackedReach(left), Self.inTrackedReach(right),
              simd_distance(head, n.head) <= 0.12, rotationCosine >= 0.9659, speed <= 6 else {
            natural = nil; return output(.needsRelease)
        }
        n.previousLeft = left; n.previousRight = right
        let ready = Self.inFront(left) && Self.inFront(right) && right.z <= -0.25
        var phase = Phase.armed
        var intent = false
        switch n.motion {
        case .waiting:
            if ready && speed < 0.55 && frame.timestamp >= naturalCooldownUntil {
                if n.stableSince == nil { n.stableSince = frame.timestamp }
                if frame.timestamp - n.stableSince! >= 0.18 {
                    n.motion = .ready; n.leftStart = left; n.rightStart = right
                    n.started = frame.timestamp
                }
            } else { n.stableSince = nil }
        case .ready:
            if configuration.swap && n.rightStart.z <= -0.30 && rv.z > 0.40 && right.z - n.rightStart.z >= 0.025 {
                n.motion = .holster; n.started = frame.timestamp; phase = .holstering; intent = true
            } else if configuration.melee && speed >= 0.95 {
                // An upward/backward backswing is not itself a strike. A
                // shoulder reach takes precedence over its forward return.
                func strikeSpeed(_ v: SIMD3<Float>) -> Float { max(-v.z, abs(v.x), -v.y) }
                let l = strikeSpeed(lv), r = strikeSpeed(rv)
                if max(l,r) >= 0.85 {
                    n.motion = l > r ? .swingLeft : .swingRight
                    n.started = frame.timestamp; n.peakSpeed = speed; phase = .swinging; intent = true
                }
            } else if ready && speed < 0.45 {
                // Quiet hand repositioning is not accumulated into a strike.
                n.leftStart = left; n.rightStart = right
            }
            if !ready && n.motion == .ready { natural = nil; return output(.needsRelease) }
        case .swingLeft, .swingRight:
            phase = .swinging; intent = true
            n.peakSpeed = max(n.peakSpeed, n.motion == .swingLeft ? simd_length(lv) : simd_length(rv))
            let delta = n.motion == .swingLeft ? left - n.leftStart : right - n.rightStart
            let duration = frame.timestamp - n.started
            if duration > 0.35 || !Self.inFront(n.motion == .swingLeft ? left : right) {
                natural = nil; return output(.needsRelease)
            }
            let worldGrip = n.motion == .swingLeft ? frame.originFromLeftGrip : frame.originFromRightGrip
            let currentDelta = currentRotation * (simd_make_float3(worldGrip.columns.3) - n.head) -
                (n.motion == .swingLeft ? n.leftStart : n.rightStart)
            func qualifyingStrike(_ displacement: SIMD3<Float>) -> Bool {
                simd_length(displacement) >= 0.22 &&
                    (-displacement.z >= 0.16 || abs(displacement.x) >= 0.22 || -displacement.y >= 0.22)
            }
            // Require physical travel in both bases. A small, fast body turn
            // can sweep an extended hand >22cm in the frozen basis alone;
            // looking with stationary hands can only move the current basis.
            if duration >= 0.045 && n.peakSpeed >= 1.35 &&
                qualifyingStrike(delta) && qualifyingStrike(currentDelta) {
                return completeNatural(.melee, frame: frame)
            }
        case .holster:
            phase = .holstering; intent = true
            // Commit while the shoulder is still visible. Waiting for a
            // fully hidden back pose would make a camera-tracked controller
            // impossible to use here. A fresh front-ready pose rearms later.
            if right.z - n.rightStart.z >= 0.25 && (Self.atVisibleShoulder(right) || Self.atHolster(right)) {
                if n.goalSince == nil { n.goalSince = frame.timestamp }
                if frame.timestamp - n.goalSince! >= 0.06 { return completeNatural(.swap, frame: frame) }
            } else { n.goalSince = nil }
            if frame.timestamp - n.started > 2 { natural = nil; return output(.needsRelease) }

        }
        natural = n
        return output(phase, intent: intent)
    }
    private mutating func completeNatural(_ action: Action, frame: Frame) -> Output {
        sequence &+= 1; if sequence == 0 { sequence = 1 }
        naturalCooldownUntil = frame.timestamp + 0.75
        natural = nil
        return Output(action: action, sequence: sequence, intentActive: true, phase: .completed)
    }

    private static func valid(_ frame: Frame) -> Bool {
        let s = frame.source
        return frame.timestamp.isFinite && frame.timestamp >= 0 &&
            s.session != 0 && s.controllerGeneration != 0 && s.inputGeneration != 0 &&
            s.left.generation != 0 && s.right.generation != 0 && s.left.accessoryID != s.right.accessoryID &&
            [frame.triggers.x, frame.triggers.y].allSatisfy { $0.isFinite && $0 >= 0 && $0 <= 1 } &&
            [frame.leftStick.x, frame.leftStick.y, frame.rightStick.x, frame.rightStick.y].allSatisfy {
                $0.isFinite && abs($0) <= 1
            } &&
            EngineImmersiveOrientation.isRigidPose(frame.originFromHead) &&
            EngineImmersiveOrientation.isRigidPose(frame.originFromLeftGrip) &&
            EngineImmersiveOrientation.isRigidPose(frame.originFromRightGrip) &&
            EngineImmersiveOrientation.isRigidPose(frame.bodyFromOrigin)
    }
    private static func inFront(_ position: SIMD3<Float>) -> Bool {
        simd_length_squared(position) <= 2.25 && abs(position.x) <= 1.2 &&
            position.y >= -1.2 && position.y <= 0.25 && position.z <= -0.04
    }
    private static func inTrackedReach(_ position: SIMD3<Float>) -> Bool {
        simd_length_squared(position) <= 2.25 && abs(position.x) <= 1.2 &&
            position.y >= -1.2 && position.y <= 0.45 && position.z <= 0.45
    }
    private static func atVisibleShoulder(_ position: SIMD3<Float>) -> Bool {
        position.x >= 0.18 && position.x <= 0.70 && position.y >= -0.20 &&
            position.y <= 0.35 && position.z >= -0.18 && position.z <= 0.20
    }
    private static func atHolster(_ position: SIMD3<Float>) -> Bool {
        let shoulder = position.x >= 0.18 && position.x <= 0.70 &&
            position.y >= -0.20 && position.y <= 0.35 &&
            position.z >= -0.12 && position.z <= 0.35
        let back = abs(position.x) <= 0.70 &&
            position.y >= -0.85 && position.y <= -0.20 &&
            position.z >= 0.10 && position.z <= 0.40
        return shoulder || back
    }
    private mutating func complete(_ action: Action, attempt: Attempt) -> Output {
        sequence &+= 1
        if sequence == 0 { sequence = 1 }
        var finished = attempt; finished.motion = .finished; self.attempt = finished
        return Output(action: action, sequence: sequence, intentActive: true, phase: .completed)
    }
    private var naturalIntent: Bool {
        guard let natural else { return false }
        return natural.motion != .waiting && natural.motion != .ready
    }
    private var naturalPhase: Phase {
        guard let natural else { return .needsRelease }
        switch natural.motion {
        case .waiting: return .needsRelease
        case .ready: return .armed
        case .swingLeft, .swingRight: return .swinging
        case .holster: return .holstering
        }
    }
    private var currentPhase: Phase {
        guard let attempt else { return released ? .armed : .needsRelease }
        switch attempt.motion {
        case .choosing: return .armed
        case .reload: return .reloading
        case .swapIn: return .holstering
        case .swapOut: return .swapping
        case .pickup: return .reaching
        case .finished: return .completed
        }
    }
    private func output(_ phase: Phase, intent: Bool = false) -> Output {
        Output(action: nil, sequence: sequence, intentActive: intent, phase: phase)
    }
    private mutating func cancel() -> Output {
        attempt = nil; released = false
        return output(.needsRelease, intent: true)
    }
    private mutating func invalidate() {
        released = false; attempt = nil; natural = nil; lastTimestamp = nil; source = nil
    }
}
