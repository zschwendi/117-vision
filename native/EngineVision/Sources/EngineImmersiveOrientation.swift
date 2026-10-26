import simd

/// A presentation adjustment in the calibrated viewer's own axes. Positive
/// yaw moves Halo right; positive pitch moves it up. This never changes FOV,
/// zoom, controller bindings or the engine's ordinary aiming policy.
struct EngineImmersiveOrientationOffset: Equatable, Sendable {
    static let maximumYawDegrees: Float = 180
    static let maximumPitchDegrees: Float = 80
    private(set) var yawDegrees: Float = 0
    private(set) var pitchDegrees: Float = 0

    init(yawDegrees: Float = 0, pitchDegrees: Float = 0) {
        self.yawDegrees = Self.bounded(yawDegrees, limit: Self.maximumYawDegrees)
        self.pitchDegrees = Self.bounded(pitchDegrees, limit: Self.maximumPitchDegrees)
    }

    mutating func nudge(yawDegrees: Float, pitchDegrees: Float) {
        // Reject a malformed request as a whole, keeping one coherent pose.
        guard yawDegrees.isFinite, pitchDegrees.isFinite else { return }
        // Bound before addition, so even Float.greatestFiniteMagnitude cannot
        // overflow the accumulator or leak a nonfinite transform to the GPU.
        self.yawDegrees = Self.bounded(self.yawDegrees + Self.bounded(yawDegrees, limit: Self.maximumYawDegrees),
                                       limit: Self.maximumYawDegrees)
        self.pitchDegrees = Self.bounded(self.pitchDegrees + Self.bounded(pitchDegrees, limit: Self.maximumPitchDegrees),
                                         limit: Self.maximumPitchDegrees)
    }

    var calibratedFromAdjusted: simd_float4x4 {
        let yaw = simd_quatf(angle: -yawDegrees * .pi / 180, axis: SIMD3<Float>(0, 1, 0))
        let pitch = simd_quatf(angle: pitchDegrees * .pi / 180, axis: SIMD3<Float>(1, 0, 0))
        return simd_float4x4(yaw * pitch)
    }

    private static func bounded(_ value: Float, limit: Float) -> Float {
        value.isFinite ? min(limit, max(-limit, value)) : 0
    }
}

/// Render-thread state. A recenter request is consumed only by a finite,
/// tracked rigid pose supplied by the caller. Full pitch and roll are retained
/// so a reclined viewer can choose their current pose as the comfortable front.
struct EngineImmersiveOrientation {
    private(set) var calibratedOriginFromHead: simd_float4x4?
    private(set) var consumedRecenter: Int?
    private var hasCalibratedContent = false
    private var hasCalibratedPanorama = false

    enum Content {
        case waiting, flat, panorama

        static func current(loading: Bool, hasFlatFrame: Bool, hasPanorama: Bool) -> Self {
            if loading { return .waiting }
            if hasPanorama { return .panorama }
            return hasFlatFrame ? .flat : .waiting
        }
    }

    enum RecenterReason: String {
        case initialTracking, requested, firstContent, firstPanorama
    }

    struct Pose {
        let originFromNeutral: simd_float4x4
        let neutralFromOrigin: simd_float4x4
        let neutralFromHead: simd_float4x4
        let recenterReason: RecenterReason?
        var didRecenter: Bool { recenterReason != nil }

        func neutralFromEye(_ headFromEye: simd_float4x4) -> simd_float4x4 {
            neutralFromHead * headFromEye
        }
    }

    mutating func pose(originFromHead: simd_float4x4, recenter: Int,
                       content: Content = .waiting,
                       offset: EngineImmersiveOrientationOffset = .init()) -> Pose? {
        guard Self.isRigidPose(originFromHead) else { return nil }
        // A loading frame can arrive while ARKit is establishing its origin.
        // Choose front again when the first actual menu or game image arrives.
        // This is a one-time boundary, not a head-locked menu. Preserve the
        // existing separate first-panorama calibration when gameplay starts.
        let reason: RecenterReason?
        if calibratedOriginFromHead == nil { reason = .initialTracking }
        else if consumedRecenter != recenter { reason = .requested }
        else if content == .panorama && !hasCalibratedPanorama { reason = .firstPanorama }
        else if content != .waiting && !hasCalibratedContent { reason = .firstContent }
        else { reason = nil }
        guard let calibration = reason != nil ? originFromHead : calibratedOriginFromHead else { return nil }
        let originFromNeutral = calibration * offset.calibratedFromAdjusted
        let neutralFromOrigin = simd_inverse(originFromNeutral)
        let neutralFromHead = neutralFromOrigin * originFromHead
        guard Self.isRigidPose(originFromNeutral), Self.isRigidPose(neutralFromOrigin),
              Self.isRigidPose(neutralFromHead) else { return nil }
        if reason != nil {
            calibratedOriginFromHead = originFromHead
            consumedRecenter = recenter
        }
        // Commit only after all transforms pass validation. A lost/invalid
        // tracking sample must not consume the first-content opportunity.
        if content != .waiting { hasCalibratedContent = true }
        if content == .panorama { hasCalibratedPanorama = true }
        return Pose(originFromNeutral: originFromNeutral, neutralFromOrigin: neutralFromOrigin,
                    neutralFromHead: neutralFromHead, recenterReason: reason)
    }

    static func isRigidPose(_ pose: simd_float4x4) -> Bool {
        let columns = [pose.columns.0, pose.columns.1, pose.columns.2, pose.columns.3]
        guard columns.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite && $0.w.isFinite }),
              abs(pose.columns.0.w) < 0.001, abs(pose.columns.1.w) < 0.001,
              abs(pose.columns.2.w) < 0.001, abs(pose.columns.3.w - 1) < 0.001 else { return false }
        let x = simd_make_float3(pose.columns.0), y = simd_make_float3(pose.columns.1), z = simd_make_float3(pose.columns.2)
        return abs(simd_length_squared(x) - 1) < 0.01 && abs(simd_length_squared(y) - 1) < 0.01 &&
            abs(simd_length_squared(z) - 1) < 0.01 && abs(simd_dot(x, y)) < 0.01 &&
            abs(simd_dot(x, z)) < 0.01 && abs(simd_dot(y, z)) < 0.01 &&
            simd_dot(simd_cross(x, y), z) > 0.99
    }
}
