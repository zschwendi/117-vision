import Foundation
import simd

@main struct SenseReticleValidation {
    static var checks = 0
    static func require(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        precondition(value(), message)
    }
    static func center(_ vertices: [EngineImmersiveScreenVertex]) -> SIMD3<Float> {
        vertices.reduce(SIMD3<Float>.zero) { $0 + simd_make_float3($1.position) } / Float(vertices.count)
    }
    static func near(_ a: SIMD3<Float>, _ b: SIMD3<Float>) -> Bool { simd_distance(a, b) < 0.0001 }
    static func main() {
        typealias G = EngineSenseReticleGeometry
        func sample(point: SIMD3<Float>, hand: Int) -> G.Sample {
            .init(point: point, hand: hand, bounds: SIMD4(704, 284, 1216, 796), anchor: SIMD2(960, 540), viewport: SIMD2(1920, 1080))
        }
        let head = SIMD3<Float>(0.2, 1.7, -0.1)
        let point = SIMD3<Float>(10, -2, 1)
        let right = G.vertices(samples: [sample(point: point, hand: 1)], headPosition: head)
        require(right.count == 6, "One valid hand draws one quad")
        require(near(center(right), SIMD3(2.2, 2.7, -10.1)), "Weapon target converts forward/left/up and adds eye translation once")
        require(right.allSatisfy { $0.weight.x == 1 && $0.position.w == 1 }, "Right hand identity and homogeneous positions survive")
        let movedHead = G.vertices(samples: [sample(point: point, hand: 1)], headPosition: head + SIMD3(1, 0, 0))
        require(near(center(movedHead) - center(right), SIMD3(1, 0, 0)), "Head translation restores head-relative coordinates without changing ray")
        let leftPoint = SIMD3<Float>(8, 3, -2)
        let pair = G.vertices(samples: [sample(point: leftPoint, hand: 0), sample(point: point, hand: 1)], headPosition: head)
        require(pair.count == 12, "Independent hands each receive a marker")
        require(near(center(Array(pair.prefix(6))), head + SIMD3(-3, -2, -8)), "Left marker follows its own target rather than right gun or gaze")
        require(near(center(Array(pair.suffix(6))), center(right)), "Adding left gun never moves right reticle")
        for target in [SIMD3<Float>(0, 0, 10), SIMD3(0, 0, -10), SIMD3(-10, 0, 0)] {
            let vertices = G.vertices(samples: [sample(point: target, hand: 0)], headPosition: .zero)
            require(vertices.count == 6 && vertices.allSatisfy { $0.position.x.isFinite && $0.position.y.isFinite && $0.position.z.isFinite }, "Vertical and behind-head aiming produces finite geometry")
            require(near(center(vertices), G.compositorVector(target)), "Vertical/360-degree targets retain their actual direction")
        }
        let nearGun = G.vertices(samples: [sample(point: SIMD3(1, 0, 0), hand: 1)], headPosition: .zero)
        let farGun = G.vertices(samples: [sample(point: SIMD3(10, 0, 0), hand: 1)], headPosition: .zero)
        let nearWidth = simd_distance(nearGun[0].position, nearGun[1].position)
        let farWidth = simd_distance(farGun[0].position, farGun[1].position)
        require(abs(farWidth / nearWidth - 10) < 0.001, "Angular marker size stays constant across target distances")
        for invalid in [SIMD3<Float>(.nan, 0, 0), SIMD3(.infinity, 0, 0), .zero, SIMD3(10_001, 0, 0)] {
            require(G.vertices(samples: [sample(point: invalid, hand: 0)], headPosition: .zero).isEmpty, "Invalid/degenerate native targets cannot reach GPU")
        }
        require(G.vertices(samples: [sample(point: point, hand: 2)], headPosition: .zero).isEmpty, "Unknown hand rejected")
        require(G.vertices(samples: [sample(point: point, hand: 1)], headPosition: SIMD3(.nan, 0, 0)).isEmpty, "Invalid head translation rejected")
        require(right.allSatisfy { $0.uv.x >= 0 && $0.uv.x <= 1 && $0.uv.y >= 0 && $0.uv.y <= 1 }, "Original image UVs use exact full crop")
        require(abs(nearWidth - 512 * 2 * tan(G.verticalHalfAngle) / 1080) < 0.0001, "Original source pixels preserve the HUD angular scale")
        var invalidArt = sample(point: point, hand: 1); invalidArt.viewport.y = 0
        require(G.vertices(samples: [invalidArt], headPosition: .zero).isEmpty, "Invalid source dimensions rejected")
        invalidArt = sample(point: point, hand: 1); invalidArt.bounds.z = .nan
        require(G.vertices(samples: [invalidArt], headPosition: .zero).isEmpty, "Invalid source crop rejected")
        var asymmetric = sample(point: SIMD3(10, 0, 0), hand: 1)
        asymmetric.bounds += SIMD4(20, -10, 20, -10)
        let shifted = G.vertices(samples: [asymmetric], headPosition: .zero)
        let scale: Float = 10 * 2 * tan(G.verticalHalfAngle) / 1080
        require(near(center(shifted) - center(farGun), SIMD3(20 * scale, 10 * scale, 0)), "Original artwork offsets survive without moving its weapon aim anchor")
        print("PASS \(checks) Sense reticle geometry checks")
    }
}
