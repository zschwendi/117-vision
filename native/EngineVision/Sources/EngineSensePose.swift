import Foundation
import simd

/// A fresh accessory aim ray in the same world-reference coordinates as ARKit's
/// device anchor. This value contains no engine camera or weapon mutation.
struct EngineSensePose: Sendable {
    enum TrackingQuality { case untracked, orientationOnly, full, fullLowAccuracy }
    enum SampleChoice { case measured, predicted }

    /// Prediction is optional; current measured 6-DoF data remains useful when
    /// the prediction horizon is unavailable. Never rescue an old/lost sample.
    static func selectSample(measured: TrackingQuality, measuredTimestamp: TimeInterval,
                             predicted: TrackingQuality?, predictedTimestamp: TimeInterval?,
                             targetTimestamp: TimeInterval) -> SampleChoice? {
        func hasPosition(_ quality: TrackingQuality) -> Bool {
            quality == .full || quality == .fullLowAccuracy
        }
        guard hasPosition(measured), isFresh(anchorTimestamp: measuredTimestamp, presentationTimestamp: targetTimestamp) else { return nil }
        if let predicted, let predictedTimestamp, hasPosition(predicted),
           isFresh(anchorTimestamp: predictedTimestamp, presentationTimestamp: targetTimestamp) { return .predicted }
        return .measured
    }

    struct Sample {
        let quality: TrackingQuality
        let timestamp: TimeInterval
    }

    /// A rifle span must never combine one predicted hand with the other
    /// hand's older measured position. Prediction is a pair-wide choice;
    /// measured fallback remains useful when its two samples are close in time.
    static func selectPair(left: Sample, right: Sample,
                           predictedLeft: Sample?, predictedRight: Sample?,
                           targetTimestamp: TimeInterval) -> SampleChoice? {
        guard selectSample(measured: left.quality, measuredTimestamp: left.timestamp,
                           predicted: nil, predictedTimestamp: nil, targetTimestamp: targetTimestamp) != nil,
              selectSample(measured: right.quality, measuredTimestamp: right.timestamp,
                           predicted: nil, predictedTimestamp: nil, targetTimestamp: targetTimestamp) != nil else { return nil }
        func coherent(_ a: Sample, _ b: Sample) -> Bool {
            a.timestamp.isFinite && b.timestamp.isFinite && abs(a.timestamp - b.timestamp) <= 0.033
        }
        if let predictedLeft, let predictedRight, coherent(predictedLeft, predictedRight),
           selectSample(measured: left.quality, measuredTimestamp: left.timestamp,
                        predicted: predictedLeft.quality, predictedTimestamp: predictedLeft.timestamp,
                        targetTimestamp: targetTimestamp) == .predicted,
           selectSample(measured: right.quality, measuredTimestamp: right.timestamp,
                        predicted: predictedRight.quality, predictedTimestamp: predictedRight.timestamp,
                        targetTimestamp: targetTimestamp) == .predicted { return .predicted }
        return coherent(left, right) ? .measured : nil
    }

    /// Full rigid aim transform, including controller roll for a held weapon.
    let originFromAim: simd_float4x4
    /// Physical handle position, distinct from the forward pointing ray.
    let originFromGrip: simd_float4x4
    let origin: SIMD3<Float>
    let direction: SIMD3<Float>

    init?(originFromAim: simd_float4x4, originFromGrip: simd_float4x4? = nil) {
        let grip = originFromGrip ?? originFromAim
        guard EngineImmersiveOrientation.isRigidPose(originFromAim),
              EngineImmersiveOrientation.isRigidPose(grip) else { return nil }
        let origin = simd_make_float3(originFromAim.columns.3)
        let direction = -simd_make_float3(originFromAim.columns.2)
        guard simd_length_squared(direction) > 0.0001 else { return nil }
        self.originFromAim = originFromAim
        self.originFromGrip = grip
        self.origin = origin
        self.direction = simd_normalize(direction)
    }

    func transformed(by targetFromOrigin: simd_float4x4) -> EngineSensePose? {
        guard EngineImmersiveOrientation.isRigidPose(targetFromOrigin) else { return nil }
        return EngineSensePose(originFromAim: targetFromOrigin * originFromAim,
                               originFromGrip: targetFromOrigin * originFromGrip)
    }

    static func isFresh(anchorTimestamp: TimeInterval, presentationTimestamp: TimeInterval) -> Bool {
        guard anchorTimestamp.isFinite, presentationTimestamp.isFinite else { return false }
        let age = presentationTimestamp - anchorTimestamp
        return age >= -0.05 && age <= 0.15
    }
}

/// Menu clicks require a release after entry, tracking loss, or leaving the
/// panel. Holding R2 while opening a menu cannot select an unintended item.
struct EngineSenseMenuTrigger {
    struct Source: Equatable, Sendable {
        let accessoryID: UUID
        let generation: UInt64
    }
    private var armed = false
    private var pressed = false
    private var source: Source?

    mutating func reset() { armed = false; pressed = false; source = nil }

    mutating func update(enabled: Bool, onPanel: Bool, value: Float?, source: Source?) -> Bool {
        guard enabled, onPanel, let value, value.isFinite, let source else { reset(); return false }
        if self.source != source { reset(); self.source = source }
        if value < 0.25 { armed = true; pressed = false; return false }
        guard value > 0.65 else { return false }
        guard armed, !pressed else { return false }
        pressed = true
        return true
    }
}
