import simd

/// Projects the actual solved weapon reticle through the same eye transforms
/// and perspective matrices as the compositor. This helper never changes yaw.
/// Ownership, session freshness, gameplay and dwell gates belong to the caller.
enum EngineSensePointTurnGeometry {
    struct Eye {
        /// CompositorServices drawable.views[i].transform (eye -> device/head).
        var headFromEye: simd_float4x4
        /// drawable.computeProjection(convention: .rightUpBack, viewIndex: i).
        var projection: simd_float4x4
    }
    struct Projection {
        /// Binocular mean NDC: -1 left edge, 0 middle, +1 right edge.
        /// Values past the horizontal edge remain meaningful; no clamping.
        let normalizedX: Float
        /// Nearest eye-space forward depth, in metres. Not ray length.
        let forwardDistance: Float
    }

    static func project(haloTarget: SIMD3<Float>, panoramaFromHead: simd_float4x4,
                        eyes: [Eye]) -> Projection? {
        guard (1...2).contains(eyes.count),
              EngineImmersiveOrientation.isRigidPose(panoramaFromHead),
              haloTarget.x.isFinite, haloTarget.y.isFinite, haloTarget.z.isFinite else { return nil }
        // Same head-relative point conversion used by the visible reticle.
        // The supplied head transform includes the EXISTING virtual yaw;
        // projecting against only the physical head would count that yaw twice.
        let relative = SIMD3<Float>(-haloTarget.y, haloTarget.z, -haloTarget.x)
        let distance = simd_length(relative)
        guard distance.isFinite, distance > 0.35, distance <= 10_000 else { return nil }
        let target = SIMD4(simd_make_float3(panoramaFromHead.columns.3) + relative, 1)
        var horizontal: Float = 0
        var forwardDistance = Float.greatestFiniteMagnitude
        for eye in eyes {
            guard EngineImmersiveOrientation.isRigidPose(eye.headFromEye) else { return nil }
            let p = eye.projection
            guard [p.columns.0, p.columns.1, p.columns.2, p.columns.3].allSatisfy({
                $0.x.isFinite && $0.y.isFinite && $0.z.isFinite && $0.w.isFinite
            }), p.columns.0.x > 0, p.columns.1.y > 0,
                  abs(p.columns.0.w) < 0.0001, abs(p.columns.1.w) < 0.0001,
                  abs(p.columns.2.w + 1) < 0.0001, abs(p.columns.3.w) < 0.0001 else { return nil }
            let eyePoint = simd_inverse(panoramaFromHead * eye.headFromEye) * target
            // Reject close, behind-head and almost sideways aim. A horizontal
            // edge is eligible only while the reticle is vertically visible.
            guard eyePoint.z < -0.35 else { return nil }
            let clip = p * eyePoint
            guard clip.w.isFinite, clip.w > 0.35 else { return nil }
            let ndc = SIMD2(clip.x, clip.y) / clip.w
            guard ndc.x.isFinite, ndc.y.isFinite, abs(ndc.y) <= 1 else { return nil }
            horizontal += ndc.x
            forwardDistance = min(forwardDistance, -eyePoint.z)
        }
        return Projection(normalizedX: horizontal / Float(eyes.count), forwardDistance: forwardDistance)
    }
}
