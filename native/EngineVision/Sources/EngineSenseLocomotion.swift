import simd

/// Virtual turning changes the shared panorama basis, not Halo's camera and
/// then the panorama a second time. Physical head yaw is already represented
/// by neutralFromHead; it must never be integrated as a stick delta.
/// Continuous modes also tilt that same basis (virtual pitch), so the view,
/// both hands and the reticle stay rigidly together. Vertical input tilts
/// about the level right axis of the CURRENT gaze, so it raises or lowers
/// a level-facing view in any physical heading without adding roll. Near
/// vertical, the bounded pole fallback below trades exact roll preservation
/// for a continuous axis when the head is rolled.
///
/// Invariant: panoramaFromNeutral = Ry(-yaw) * tiltBasis. `yaw` is only the
/// deliberately commanded turn (stick, edge, snap). `tiltBasis` is the exact
/// composition of every admitted vertical step (normalised, never
/// re-canonicalised), and angle(panorama up, neutral up) never exceeds
/// `pitchLimit`. Tilting at different physical headings can leave a twist
/// about up inside tiltBasis; it is part of the rendered world, never moved
/// into `yaw`. Both change only on admitted input, recenter, or choosing a
/// mode without tilt; head motion alone never rewrites them, so the world
/// stays fixed. A tilted world therefore looks rolled after the player
/// physically turns away from where they tilted; that is bounded by the
/// limit and is never re-levelled automatically. Recenter levels it.
struct EngineSenseLocomotion {
    /// The actual right-weapon reticle projected into the current eye FOV.
    /// This is not a controller angle or the direction of the user's head.
    struct EdgeAim {
        var horizontal: Float
        var forwardDistance: Float
    }

    struct Tuning: Equatable {
        /// The saved Sense "stick speed" (native 30...120). The right stick
        /// turns at twice this and tilts at this rate; point-to-turn is half.
        var stickDegreesPerSecond: Float = 90
        var edgeBoundary: Float = 0.72

        fileprivate var bounded: Self {
            Self(stickDegreesPerSecond: stickDegreesPerSecond.isFinite
                 ? min(120, max(30, stickDegreesPerSecond)) : 90,
                 edgeBoundary: edgeBoundary.isFinite ? min(0.90, max(0.50, edgeBoundary)) : 0.72)
        }
        fileprivate var edgeExit: Float { edgeBoundary - 0.14 }
        fileprivate var edgeCenter: Float { min(0.45, edgeExit - 0.13) }
        var horizontalStickDegreesPerSecond: Float { 2 * stickDegreesPerSecond }
        var verticalStickDegreesPerSecond: Float { stickDegreesPerSecond }
        var edgeDegreesPerSecond: Float { 0.5 * stickDegreesPerSecond }
    }

    /// Finite world tilt limit in any direction. Physical head pitch adds on top.
    static let pitchLimit: Float = 45 * .pi / 180

    private(set) var yaw: Float = 0
    /// Vertical input's share of the shared basis, expressed after `yaw`.
    private(set) var tiltBasis = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)
    /// Angle between the panorama's up and the recentred up (0...pitchLimit).
    var tilt: Float { acos(min(1, max(-1, tiltBasis.act(Self.up).y))) }
    private(set) var generation: UInt64 = 0
    private var armed = false
    private var source: EngineSenseMenuTrigger.Source?
    private var recenter: Int?
    private var mode = 0
    private var lastHeadYaw: Float = 0
    private var tuning = Tuning()
    private var lastTimestamp: Double?
    private var angularVelocity: Double = 0
    private var pitchVelocity: Double = 0
    private var edgeArmed = false
    private var edgeSign: Float = 0
    private var edgeStartedAt: Double?
    private var lastPhysicalHeadYaw: Float?
    private var centeredPhysicalHeadYaw: Float?

    mutating func update(enabled: Bool, stick: SIMD2<Float>?,
                         source: EngineSenseMenuTrigger.Source?, mode: Int,
                         recenter: Int, timestamp: Double? = nil,
                         edgeAim: EdgeAim? = nil, physicalHeadYaw: Float? = nil,
                         neutralFromHead: simd_float4x4? = nil,
                         tuning: Tuning = .init()) -> simd_float4x4 {
        if self.recenter != recenter {
            self.recenter = recenter; yaw = 0; tiltBasis = Self.level; lastHeadYaw = 0
            resetInput(); generation &+= 1
        }
        let effectiveMode = [0, 1, 2, 30, 45].contains(mode) ? mode : 0
        let boundedTuning = tuning.bounded
        if self.source != source || self.mode != effectiveMode || self.tuning != boundedTuning {
            self.source = source; self.mode = effectiveMode; self.tuning = boundedTuning
            // Physical-only and snap modes have no stick tilt control, so
            // choosing one levels the view instead of leaving it stuck tilted.
            // This explicit reset also drops any twist left by tilting at
            // different headings, so the view again faces the movement heading.
            if effectiveMode != 1 && effectiveMode != 2 { tiltBasis = Self.level }
            resetInput(); generation &+= 1
        }
        guard enabled, source != nil, let stick, stick.x.isFinite, stick.y.isFinite else {
            resetInput()
            return panoramaFromNeutral
        }
        // A neutral release is required after entry, reconnect, recenter,
        // mode/tuning changes and tracking loss. Snap never auto-repeats.
        if effectiveMode != 1 && effectiveMode != 2 {
            if simd_length(stick) < 0.25 { armed = true }
            else if effectiveMode != 0 && armed && abs(stick.x) > 0.75 && abs(stick.y) < 0.5 {
                applyYaw((stick.x > 0 ? 1 : -1) * Float(effectiveMode) * .pi / 180)
                armed = false
            }
            return panoramaFromNeutral
        }

        guard let timestamp, timestamp.isFinite, timestamp >= 0 else {
            resetInput()
            return panoramaFromNeutral
        }
        // A layer may contain more than one drawable with the same time.
        // Do not integrate twice or erase a partially completed edge dwell.
        if lastTimestamp == timestamp { return panoramaFromNeutral }
        var elapsed = lastTimestamp.map { timestamp - $0 } ?? 0
        if elapsed < 0 || elapsed > 0.10 {
            resetInput()
            elapsed = 0
        }
        lastTimestamp = timestamp
        if simd_length(stick) < 0.20 { armed = true }

        let physical = physicalHeadYaw.flatMap { $0.isFinite ? $0 : nil }
        let physicalRate = physical.flatMap { current in
            lastPhysicalHeadYaw.map { previous in
                elapsed > 0 ? abs(Self.angleDifference(current, previous)) / Float(elapsed) : 0
            }
        } ?? 0
        lastPhysicalHeadYaw = physical

        // Stick intent always wins, even if entry/reconnect has not yet armed
        // the stick. Return to centered aim before edge turning can resume.
        // Each axis has its own deadzone, so a diagonal turns and tilts.
        if simd_length(stick) >= 0.20 {
            resetEdge()
            let yawTarget = Self.axisResponse(stick.x)
                * Double(boundedTuning.horizontalStickDegreesPerSecond) * .pi / 180
            let pitchTarget = Self.axisResponse(stick.y)
                * Double(boundedTuning.verticalStickDegreesPerSecond) * .pi / 180
            // Tilt first, about the gaze before this frame's yaw. Yaw about the
            // vertical commutes with it (the axis turns with the gaze), so the
            // order does not change the result.
            integratePitch(target: armed ? pitchTarget : 0, elapsed: elapsed,
                           axis: pitchTarget == 0 ? nil : tiltAxis(neutralFromHead: neutralFromHead,
                                                                   physicalHeadYaw: physicalHeadYaw))
            integrate(target: armed ? yawTarget : 0, elapsed: elapsed)
            return panoramaFromNeutral
        }
        pitchVelocity = 0
        guard effectiveMode == 2, armed, let edgeAim,
              edgeAim.horizontal.isFinite, edgeAim.forwardDistance.isFinite,
              edgeAim.forwardDistance > 0.35, let physical else {
            resetEdge(); angularVelocity = 0
            return panoramaFromNeutral
        }

        let horizontal = min(1, max(-1, edgeAim.horizontal))
        let magnitude = abs(horizontal)
        if magnitude <= boundedTuning.edgeCenter {
            // Follow the real head only while safely centered. Once the gun
            // leaves center, head motion cannot masquerade as pointing intent.
            centeredPhysicalHeadYaw = physical
            edgeArmed = true; edgeSign = 0; edgeStartedAt = nil
            angularVelocity = 0
            return panoramaFromNeutral
        }
        let physicalDisplacement = centeredPhysicalHeadYaw.map { abs(Self.angleDifference(physical, $0)) }
        guard edgeArmed, physicalRate <= 10 * .pi / 180,
              let physicalDisplacement, physicalDisplacement <= 3 * .pi / 180 else {
            resetEdge(); angularVelocity = 0
            return panoramaFromNeutral
        }
        let sign: Float = horizontal > 0 ? 1 : -1
        if magnitude <= boundedTuning.edgeExit || (edgeSign != 0 && edgeSign != sign) {
            edgeSign = 0; edgeStartedAt = nil; angularVelocity = 0
        }
        if magnitude >= boundedTuning.edgeBoundary, edgeStartedAt == nil {
            edgeSign = sign; edgeStartedAt = timestamp
        }
        guard let started = edgeStartedAt, edgeSign == sign, timestamp - started >= 0.18 else {
            angularVelocity = 0
            return panoramaFromNeutral
        }
        // Integrate only the portion after the dwell completed; otherwise the
        // first turning frame would depend on the display's refresh rate.
        let activeElapsed = min(elapsed, timestamp - started - 0.18)
        let amount = Double(max(0, (magnitude - boundedTuning.edgeExit) / (1 - boundedTuning.edgeExit)))
        let target = Double(sign) * amount * amount
            * Double(boundedTuning.edgeDegreesPerSecond) * .pi / 180
        integrate(target: target, elapsed: activeElapsed)
        return panoramaFromNeutral
    }

    /// 0.20 axial deadzone, then a squared curve for fine control.
    private static func axisResponse(_ value: Float) -> Double {
        guard abs(value) > 0.20 else { return 0 }
        let amount = Double(min(1, (abs(value) - 0.20) / 0.80))
        return Double(value > 0 ? 1 : -1) * amount * amount
    }

    private static func angleDifference(_ a: Float, _ b: Float) -> Float {
        (a - b).remainder(dividingBy: 2 * .pi)
    }

    private mutating func resetEdge() {
        edgeArmed = false; edgeSign = 0; edgeStartedAt = nil; centeredPhysicalHeadYaw = nil
    }

    private mutating func resetInput() {
        armed = false; angularVelocity = 0; pitchVelocity = 0; lastTimestamp = nil; lastPhysicalHeadYaw = nil
        resetEdge()
    }

    private mutating func applyYaw(_ delta: Float) {
        guard delta != 0 else { return }
        yaw = (yaw + delta).remainder(dividingBy: 2 * .pi)
        lastHeadYaw = (lastHeadYaw + delta).remainder(dividingBy: 2 * .pi)
    }

    private mutating func integrate(target: Double, elapsed: Double) {
        applyYaw(Float(Self.ramp(velocity: &angularVelocity, target: target, elapsed: elapsed)))
    }

    /// Without a current facing there is no meaningful "up"; refuse tilt.
    private mutating func integratePitch(target: Double, elapsed: Double, axis: SIMD3<Float>?) {
        guard let axis else { pitchVelocity = 0; return }
        let delta = Float(Self.ramp(velocity: &pitchVelocity, target: target, elapsed: elapsed))
        // Stop at the limit; holding into it stores no speed to release later.
        if delta != 0, applyTilt(delta, about: axis) { pitchVelocity = 0 }
    }

    private static let level = simd_quatf(ix: 0, iy: 0, iz: 0, r: 1)

    /// Level right axis of the current gaze in the panorama: perpendicular to
    /// the gaze's vertical plane, oriented like the head's right axis so it
    /// stays continuous past vertical. Within about 10 degrees of vertical it
    /// blends smoothly toward the head's level right axis, which remains
    /// defined at the pole; there a rolled head is continuous and bounded but
    /// not guaranteed zero roll. Uses the actual physical head when given,
    /// else a level head at the physical heading.
    private func tiltAxis(neutralFromHead: simd_float4x4?, physicalHeadYaw: Float?) -> SIMD3<Float>? {
        let head: simd_float4x4
        if let neutralFromHead, EngineImmersiveOrientation.isRigidPose(neutralFromHead) {
            head = neutralFromHead
        } else if let physicalHeadYaw, physicalHeadYaw.isFinite {
            head = simd_float4x4(simd_quatf(angle: -physicalHeadYaw, axis: Self.up))
        } else { return nil }
        let panoramaFromHead = panoramaFromNeutral * head
        let gaze = -simd_make_float3(panoramaFromHead.columns.2)
        let right = simd_make_float3(panoramaFromHead.columns.0)
        let levelRight = SIMD3<Float>(-gaze.z, 0, gaze.x) // gaze x up
        let headRight = SIMD3<Float>(right.x, 0, right.z)
        let horizontal = simd_length_squared(levelRight)
        var axis = SIMD3<Float>(repeating: 0)
        if horizontal > 1e-10 {
            axis = levelRight / horizontal.squareRoot()
            // Past vertical the gaze's level right reverses; the head's does not.
            // An uninformative (rolled ~90 degree) head keeps the natural side.
            if simd_dot(axis, headRight) < -0.05 { axis = -axis }
        }
        let t = min(1, max(0, (horizontal - 0.0025) / (0.03 - 0.0025)))
        let blend = t * t * (3 - 2 * t)
        if blend < 1, simd_length_squared(headRight) > 1e-6 {
            axis = blend * axis + (1 - blend) * simd_normalize(headRight)
        }
        let length = simd_length(axis)
        guard length.isFinite, length > 0.001 else { return nil }
        return axis / length
    }

    /// Rotates the shared basis by `angle` about a level panorama axis, held
    /// to the tilt limit exactly. Returns true when the limit stopped it.
    private mutating func applyTilt(_ angle: Float, about axis: SIMD3<Float>) -> Bool {
        let upNow = (simd_quatf(angle: -yaw, axis: Self.up) * tiltBasis).act(Self.up)
        // up.y after rotating by a about the level axis = r cos(a - offset).
        let b = upNow.y, c = simd_cross(axis, upNow).y
        let reach = (b * b + c * c).squareRoot(), floor = cos(Self.pitchLimit)
        var admitted: Float = 0
        if reach > floor {
            let offset = atan2(c, b), margin = acos(min(1, floor / reach))
            admitted = min(offset + margin, max(offset - margin, angle))
            // At the bound, rounding must not step against the request.
            if admitted * angle < 0 { admitted = 0 }
        }
        if admitted != 0 {
            // Same rotation expressed after the yaw: R(axis) * Ry(-yaw) = Ry(-yaw) * R(Ry(yaw) axis).
            // Exact composition: the rendered basis rotates by exactly this
            // step, and deliberate yaw (the movement authority) is untouched.
            let local = simd_quatf(angle: yaw, axis: Self.up).act(axis)
            tiltBasis = simd_normalize(simd_quatf(angle: admitted, axis: local) * tiltBasis)
        }
        return admitted != angle
    }

    /// Returns the angle travelled this frame and advances the velocity.
    private static func ramp(velocity: inout Double, target: Double, elapsed: Double) -> Double {
        // Stops and direction reversals never coast in the previous direction.
        guard target != 0 else { velocity = 0; return 0 }
        guard elapsed > 0 else { return 0 }
        if velocity * target < 0 { velocity = 0 }
        let timeConstant = 0.12
        let blend = 1 - exp(-elapsed / timeConstant)
        // Exact integral of the bounded exponential ramp, not rate*frameCount.
        let delta = target * elapsed + (velocity - target) * timeConstant * blend
        velocity += (target - velocity) * blend
        return delta
    }

    private static let up = SIMD3<Float>(0, 1, 0)

    /// Deliberate yaw about the panorama's vertical after the vertical-input
    /// basis; positive yaw looks right. With no tilt this is exactly the
    /// yaw-only basis.
    var panoramaFromNeutral: simd_float4x4 {
        simd_float4x4(simd_quatf(angle: -yaw, axis: Self.up) * tiltBasis)
    }

    /// Movement heading for a head pose in the CURRENT panorama basis: the
    /// full basis is removed and only deliberate yaw reapplied, so the heading
    /// is the physical heading plus commanded yaw. Vertical input never turns
    /// movement, and it cannot flip when physical and virtual pitch add past
    /// vertical. Looking almost straight up/down must not make locomotion flip
    /// from floating-point noise in the head's horizontal forward projection.
    mutating func horizontalHeadYaw(_ panoramaFromHead: simd_float4x4) -> Float {
        // Ry(-yaw) * basis^-1 * panoramaFromHead, with basis = Ry(-yaw) * tilt:
        // Ry(-yaw) * tilt^-1 * Ry(yaw) * (Ry(-yaw) * tilt * neutralFromHead)
        // = Ry(-yaw) * neutralFromHead.
        let level = tiltBasis == Self.level ? panoramaFromHead : simd_float4x4(
            simd_quatf(angle: -yaw, axis: Self.up) * tiltBasis.inverse
                * simd_quatf(angle: yaw, axis: Self.up)) * panoramaFromHead
        let forward = -simd_make_float3(level.columns.2)
        if forward.x.isFinite, forward.z.isFinite, forward.x * forward.x + forward.z * forward.z > 0.0025 {
            lastHeadYaw = atan2(forward.x, -forward.z)
        }
        return lastHeadYaw
    }

    /// ARKit -Z forward, +X right, +Y up -> Halo camera +X forward,
    /// +Y left, +Z up. No game-world camera rotation or unit scale here.
    static func cameraVector(_ vector: SIMD3<Float>) -> SIMD3<Float> {
        SIMD3(-vector.z, -vector.x, vector.y)
    }

    struct WeaponPose {
        let position: SIMD3<Float>
        let forward: SIMD3<Float>
        let up: SIMD3<Float>
    }

    static func weaponPose(originFromAim: simd_float4x4,
                           originFromGrip: simd_float4x4? = nil,
                           panoramaFromOrigin: simd_float4x4,
                           panoramaFromHead: simd_float4x4) -> WeaponPose? {
        let grip = originFromGrip ?? originFromAim
        guard EngineImmersiveOrientation.isRigidPose(originFromAim),
              EngineImmersiveOrientation.isRigidPose(grip),
              EngineImmersiveOrientation.isRigidPose(panoramaFromOrigin),
              EngineImmersiveOrientation.isRigidPose(panoramaFromHead) else { return nil }
        let aim = panoramaFromOrigin * originFromAim
        let held = panoramaFromOrigin * grip
        let relative = simd_make_float3(held.columns.3 - panoramaFromHead.columns.3)
        guard simd_length_squared(relative) <= 16 else { return nil }
        return WeaponPose(position: cameraVector(relative),
                          forward: cameraVector(-simd_make_float3(aim.columns.2)),
                          up: cameraVector(simd_make_float3(aim.columns.1)))
    }
}
