import simd

/// Measure physical travel before applying camera/head/virtual-turn transforms.
/// Predicted presentation poses must never become throw velocity samples.
struct EngineSenseThrowMotion {
    struct Source: Equatable {
        var accessory: EngineSenseMenuTrigger.Source
        var controllerGeneration: UInt64
        var recenter: Int
    }
    struct Sample {
        var source: Source
        var timestamp: Double
        var position: SIMD3<Float>
    }
    private var samples: [Sample] = []

    mutating func reset() { samples.removeAll(keepingCapacity: true) }

    /// Returns metres/second in Halo's +forward,+left,+up panorama basis.
    /// A short measured window rejects single-frame noise without averaging
    /// the backswing into the forward release. No head position is subtracted.
    mutating func update(_ sample: Sample?, at timestamp: Double,
                         panoramaFromOrigin: simd_float4x4) -> SIMD3<Float>? {
        guard let sample, timestamp.isFinite, sample.timestamp.isFinite,
              EngineSensePose.isFresh(anchorTimestamp: sample.timestamp, presentationTimestamp: timestamp),
              timestamp - sample.timestamp <= 0.075,
              sample.position.x.isFinite, sample.position.y.isFinite, sample.position.z.isFinite,
              sample.source.controllerGeneration != 0, sample.source.accessory.generation != 0,
              EngineImmersiveOrientation.isRigidPose(panoramaFromOrigin) else { reset(); return nil }
        if let last = samples.last {
            let dt = sample.timestamp - last.timestamp
            if last.source != sample.source || dt < 0 || dt > 0.10 {
                reset()
            } else if dt > 0 {
                let speed = simd_distance(sample.position, last.position) / Float(dt)
                // A tracking jump must re-prime, never become a powerful throw.
                if dt < 0.001 || !speed.isFinite || speed > 12 { reset() }
            } else if sample.position != last.position {
                reset()
            }
        }
        if samples.last?.timestamp != sample.timestamp { samples.append(sample) }
        // Keep the nearest sample at least 25ms old, and at most 75ms old.
        // Duplicate render queries do not extend the physical sample window.
        while samples.count > 2 && sample.timestamp - samples[1].timestamp >= 0.025 {
            samples.removeFirst()
        }
        guard let first = samples.first else { return nil }
        let dt = sample.timestamp - first.timestamp
        guard dt >= 0.012, dt <= 0.075 else { return nil }
        let world = (sample.position - first.position) / Float(dt)
        let turned = panoramaFromOrigin * SIMD4(world.x, world.y, world.z, 0)
        let velocity = SIMD3<Float>(-turned.z, -turned.x, turned.y)
        guard simd_length(velocity).isFinite, simd_length(velocity) <= 12 else { reset(); return nil }
        return velocity
    }
}
