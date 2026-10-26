import simd

@main struct SensePointTurnGeometryValidation {
    static var checks = 0
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(condition(), message)
    }
    static func main() {
        typealias G = EngineSensePointTurnGeometry
        func projection(_ xScale: Float = 1, _ offset: Float = 0) -> simd_float4x4 {
            simd_float4x4(columns: (SIMD4(xScale,0,0,0), SIMD4(0,1,0,0), SIMD4(offset,0,0,-1), SIMD4(0,0,0.1,0)))
        }
        func eye(_ x: Float = 0, _ scale: Float = 1, _ offset: Float = 0) -> G.Eye {
            var pose = matrix_identity_float4x4; pose.columns.3.x = x
            return .init(headFromEye: pose, projection: projection(scale, offset))
        }
        func halo(_ compositor: SIMD3<Float>) -> SIMD3<Float> { SIMD3(-compositor.z,-compositor.x,compositor.y) }
        let center = matrix_identity_float4x4, mono = [eye()], stereo = [eye(-0.032),eye(0.032)]
        func test(_ point: SIMD3<Float>, _ head: simd_float4x4 = matrix_identity_float4x4, _ eyes: [G.Eye]? = nil) -> G.Projection? {
            G.project(haloTarget: halo(point), panoramaFromHead: head, eyes: eyes ?? stereo)
        }
        require(abs(test(SIMD3(0,0,-10))!.normalizedX) < 0.00001, "Binocular eye separation cancels at center")
        for x: Float in [-1.2,-1,-0.72,0,0.72,1,1.2] {
            let result = test(SIMD3(x*10,0,-10))!
            require(abs(result.normalizedX-x) < 0.00001, "Actual edge and beyond-edge coordinates preserve sign and scale")
            require(abs(result.forwardDistance-10) < 0.00001, "Depth is eye-forward distance")
        }
        require(abs(test(SIMD3(5,0,-10),center,[eye(0,2)])!.normalizedX-1) < 0.00001, "Narrower actual eye FOV moves point to edge")
        require(abs(test(SIMD3(5,0,-10),center,[eye(0,1,0.1)])!.normalizedX-0.4) < 0.00001, "Asymmetric projection offset is respected")
        require(abs(test(SIMD3(0,0,-10),center,[eye(-0.032,1,0.2),eye(0.032,1,-0.2)])!.normalizedX) < 0.00001, "Opposite eye frusta produce a stable binocular center")
        var translated = center; translated.columns.3 = SIMD4(3,1.8,-2,1)
        require(abs(test(SIMD3(8,0,-10),translated)!.normalizedX-0.8) < 0.00001, "Head translation is restored exactly once")
        let yaw = simd_float4x4(simd_quatf(angle: -.pi/3,axis:SIMD3(0,1,0)))
        let local = SIMD4<Float>(8,0,-10,0), turned = simd_make_float3(yaw*local)
        require(abs(test(turned,yaw)!.normalizedX-0.8) < 0.00001, "Shared virtual yaw on gun and head never double-turns the screen point")
        // Locomotion's yaw-after-pitch basis, applied to both head and reticle.
        let tilt = simd_float4x4(simd_quatf(angle: -.pi/3,axis:SIMD3(0,1,0)) * simd_quatf(angle: .pi/5,axis:SIMD3(1,0,0)))
        let tiltedPoint = simd_make_float3(tilt*local)
        require(abs(test(tiltedPoint,tilt)!.normalizedX-0.8) < 0.00001, "Shared virtual yaw and tilt never double-turn the screen point")
        // Tilt made at another physical heading (twist about up included).
        let twisted = simd_float4x4(simd_quatf(angle: -0.5,axis:SIMD3(0,0,1)) * simd_quatf(angle: 0.5,axis:SIMD3(1,0,0)) * simd_quatf(angle: -.pi/3,axis:SIMD3(0,1,0)))
        require(abs(test(simd_make_float3(twisted*local),twisted)!.normalizedX-0.8) < 0.00001, "Any shared tilt basis, including one made at another heading, never double-turns the screen point")
        let tiltedPhysical = tilt * simd_float4x4(simd_quatf(angle: -.pi/6,axis:SIMD3(0,1,0)))
        let ahead = simd_make_float3(tiltedPhysical*SIMD4<Float>(0,0,-10,0))
        require(abs(test(ahead,tiltedPhysical)!.normalizedX) < 0.00001, "A head-centred reticle stays centred with physical yaw under tilt")
        let physical = simd_float4x4(simd_quatf(angle: -.pi/4,axis:SIMD3(0,1,0)))
        require(abs(test(SIMD3(0,0,-10),physical)!.normalizedX+1) < 0.0001, "Physical head turn moves a stationary target across screen; caller inhibits artificial turn")
        for point in [SIMD3<Float>(0,0,10),SIMD3(2,0,0),SIMD3(0,0,-0.35),SIMD3(10,0,-0.2),SIMD3(0,11,-10),SIMD3(0,-11,-10),SIMD3(.nan,0,-10),SIMD3<Float>(0,0,-Float.infinity)] {
            require(test(point) == nil, "Rear, near, vertical offscreen and nonfinite aiming cannot request turning")
        }
        require(test(SIMD3(0,9.9,-10)) != nil, "Vertically visible target remains valid")
        require(test(SIMD3(0,0,-10),center,[]) == nil, "Missing frusta rejected")
        var bad = eye(); bad.projection.columns.2.w = 1
        require(test(SIMD3(0,0,-10),center,[bad]) == nil, "Wrong handedness/perspective rejected")
        bad=eye();bad.headFromEye.columns.0.x=2
        require(test(SIMD3(0,0,-10),center,[bad]) == nil, "Scaled eye pose rejected")
        require(test(SIMD3(8,0,-10),center,mono)!.normalizedX == 0.8, "Single-view projection supported without fake second eye")
        print("PASS \(checks) actual-reticle point-turn projection checks")
    }
}
