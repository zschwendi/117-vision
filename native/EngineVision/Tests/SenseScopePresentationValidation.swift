import Foundation
import simd

@main struct SenseScopePresentationValidation {
    typealias Scope = EngineSenseScopePresentation
    static var checks = 0
    static var failures: [String] = []
    static func check(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1
        if !value() { failures.append(message) }
    }
    static func close(_ a: Float, _ b: Float, _ tolerance: Float = 0.00002) -> Bool {
        a.isFinite && b.isFinite && abs(a-b) <= tolerance
    }
    static func close(_ a: SIMD3<Float>, _ b: SIMD3<Float>, _ tolerance: Float = 0.00005) -> Bool {
        simd_distance(a,b).isFinite && simd_distance(a,b) <= tolerance
    }
    static let identity = Scope.Identity(session: 17, generation: 31, mode: 9, weapon: 0xABCD1234, zoom: 0)

    static func freshnessAndIdentity() {
        var held = Scope()
        check(held.accepts(sequence: 100, frame: identity, current: identity, now: 10), "Fresh matching completed scope accepted")
        for now in [10.0, 10.05, 10.10, 10.20, 10.25] {
            check(held.accepts(sequence: 100, frame: identity, current: identity, now: now), "Same sequence allowed only within250ms at\(now)")
        }
        check(!held.accepts(sequence: 100, frame: identity, current: identity, now: 10.25001), "Frozen sequence expires beyond250ms")
        check(!held.accepts(sequence: 100, frame: identity, current: identity, now: 11), "Repeated reads cannot renew frozen pixels")
        check(held.accepts(sequence: 101, frame: identity, current: identity, now: 11.01), "Genuinely new completed frame renews presentation")
        check(!held.accepts(sequence: 100, frame: identity, current: identity, now: 11.02), "Older sequence cannot replay after newer frame")
        check(held.accepts(sequence: 101, frame: identity, current: identity, now: 11.03), "Rejected replay cannot replace valid current sequence")
        check(!held.accepts(sequence: 0, frame: identity, current: identity, now: 11.04), "Zero sequence never identifies completed pixels")

        var missing = Scope()
        check(!missing.accepts(sequence: 1, frame: nil, current: identity, now: 20), "Missing completed-frame metadata rejected")
        check(!missing.accepts(sequence: 1, frame: identity, current: identity, now: 20.01), "Same pixels cannot gain a scope identity later")
        check(missing.accepts(sequence: 2, frame: identity, current: identity, now: 20.02), "New scope frame can follow an unmarked frame")
        check(!missing.accepts(sequence: 2, frame: identity, current: nil, now: 20.03), "No live scope means reject old scope pixels")
        check(!missing.accepts(sequence: 3, frame: nil, current: nil, now: 20.04), "Flat/menu frame never accepted as scope")
        check(!missing.accepts(sequence: 2, frame: identity, current: identity, now: 20.05), "Menu transition prevents old sequence resurrection")

        for component in 0..<5 {
            var nextIdentity = identity
            switch component {
            case 0: nextIdentity.session += 1
            case 1: nextIdentity.generation += 1
            case 2: nextIdentity.mode += 1
            case 3: nextIdentity.weapon += 1
            default: nextIdentity.zoom += 1
            }
            var transition = Scope()
            check(transition.accepts(sequence: 100, frame: identity, current: identity, now: 30), "Transition\(component) starts with matching frame")
            check(!transition.accepts(sequence: 100, frame: identity, current: nextIdentity, now: 30.01), "Changed live identity\(component) rejects old pixels")
            check(!transition.accepts(sequence: 100, frame: nextIdentity, current: nextIdentity, now: 30.02), "Same sequence cannot be retagged for identity\(component)")
            check(transition.accepts(sequence: 101, frame: nextIdentity, current: nextIdentity, now: 30.03), "New exact identity\(component) frame accepted")
            check(!transition.accepts(sequence: 102, frame: identity, current: nextIdentity, now: 30.04), "Even newer pixels with stale identity\(component) rejected")
        }
        for zoom: Int32 in [-2,-1,0,1,2,3,4,Int32.max] {
            var value = identity; value.zoom = zoom
            var state = Scope()
            check(state.accepts(sequence: 1, frame: value, current: value, now: 40) == (0...3).contains(zoom),
                  "Only actual scope levels0...3 accepted: \(zoom)")
        }
        for invalid in 0..<4 {
            var value = identity
            switch invalid {
            case 0: value.session = 0
            case 1: value.generation = 0
            case 2: value.mode = 0
            default: value.weapon = UInt32.max
            }
            var state = Scope()
            check(!state.accepts(sequence: 1, frame: value, current: value, now: 50), "Invalid identity component\(invalid) rejected")
        }
    }

    static func clocks() {
        for invalid in [-Double.infinity, Double.infinity, Double.nan, -1] {
            var state = Scope()
            check(!state.accepts(sequence: 1, frame: identity, current: identity, now: invalid), "Invalid uptime\(invalid) rejected")
        }
        var rollback = Scope()
        check(rollback.accepts(sequence: 1, frame: identity, current: identity, now: 60), "Clock test begins valid")
        check(!rollback.accepts(sequence: 1, frame: identity, current: identity, now: 60.30), "Clock test frame expires")
        check(!rollback.accepts(sequence: 1, frame: identity, current: identity, now: 60.10), "Clock rollback cannot resurrect expired pixels")
        check(!rollback.accepts(sequence: 2, frame: identity, current: identity, now: 59), "New sequence cannot bypass clock rollback")
        check(rollback.accepts(sequence: 2, frame: identity, current: identity, now: 60.31), "Valid monotonic new frame recovers from rejected clock")
        var future = Scope()
        check(future.accepts(sequence: 1, frame: identity, current: identity, now: 100), "Future-clock test begins valid")
        check(!future.accepts(sequence: 1, frame: identity, current: identity, now: 100.251), "Future clock beyond retained age rejects pixels")
        check(!future.accepts(sequence: 1, frame: identity, current: identity, now: Double.greatestFiniteMagnitude), "Far-future clock cannot leave old image valid")
        check(!future.accepts(sequence: 1, frame: identity, current: identity, now: 100.2), "Returning from future clock cannot revive old pixels")
    }

    /// Asymmetric perspective with real-style unequal eye FOV and optical
    /// centers. Test-generated input only; production helpers generate all
    /// panel vertices and the actual eye matrices under test.
    static func projection(_ sx: Float, _ sy: Float, _ ox: Float, _ oy: Float) -> simd_float4x4 {
        simd_float4x4(columns: (SIMD4(sx,0,0,0), SIMD4(0,sy,0,0),
                               SIMD4(ox,oy,0,-1), SIMD4(0,0,0.05,0)))
    }
    static func geometry() {
        for aspect: Float in [1,4/3,16/9,0.75,2.4] {
            let vertices = Scope.vertices(aspect: aspect)
            check(vertices.count == 6, "Exactly two triangles form full original scope image")
            let a = vertices[0], b = vertices[1], c = vertices[2], d = vertices[5]
            let width = c.position.x-a.position.x, height = a.position.y-b.position.y
            check(close(width/height,aspect), "Source aspect\(aspect) remains square-pixel geometry")
            check(a.uv == SIMD2(0,0) && b.uv == SIMD2(0,1) && c.uv == SIMD2(1,0) && d.uv == SIMD2(1,1),
                  "Scope image uses full UVs with correct top/bottom orientation")
            check(vertices[2].position == vertices[3].position && vertices[1].position == vertices[4].position,
                  "Two triangles share identical diagonal vertices")
            let center = (a.position+d.position)/2
            check(close(simd_make_float3(center),SIMD3(0,0,-3.1)) && center.w == 1,
                  "Source center is one common head-forward point at3.1m")
            check(vertices.allSatisfy { $0.position.z == -3.1 && $0.position.w == 1 && $0.weight == SIMD2(1,0) },
                  "Panel is planar and opaque, without separate curved HUD/reticle depth")
        }
        let fallback = Scope.vertices(aspect: 4/3)
        for aspect: Float in [0,-1,.nan,.infinity,-.infinity] {
            let vertices = Scope.vertices(aspect: aspect)
            check(zip(vertices,fallback).allSatisfy { $0.position == $1.position && $0.uv == $1.uv },
                  "Invalid aspect\(aspect) uses safe4:3 geometry")
        }
        let vertices = Scope.vertices(aspect: 4/3)
        let center = (vertices[0].position+vertices[5].position)/2
        let projections = [projection(1.11,1.36,0.13,-0.035),projection(1.18,1.32,-0.10,0.025)]
        var ndcCenters: [SIMD3<Float>] = []
        for (index,p) in projections.enumerated() {
            var eye = matrix_identity_float4x4
            eye.columns.3 = SIMD4(index == 0 ? -0.033 : 0.031, index == 0 ? 0.001 : -0.002,0.004,1)
            let matrix = Scope.eyeTransform(projection: p,headFromEye: eye)
            let clip = matrix*center, ndc = simd_make_float3(clip)/clip.w
            ndcCenters.append(ndc)
            // Independently invert the actual lens projection and intersect
            // its ray with the head-local scope plane. Both eyes must recover
            // the SAME target, rather than sharing artificial equal NDCs.
            let eyePoint = simd_inverse(p)*SIMD4(ndc,1)
            let headPoint = eye*(eyePoint/eyePoint.w)
            check(close(simd_make_float3(headPoint),simd_make_float3(center)), "Eye\(index) original source center converges on same head-forward target")
            let left = matrix*vertices[0].position, right = matrix*vertices[2].position
            let bottom = matrix*vertices[1].position
            let lensCorrectedWidth = (right.x/right.w-left.x/left.w)/p.columns.0.x
            let lensCorrectedHeight = (left.y/left.w-bottom.y/bottom.w)/p.columns.1.y
            check(close(lensCorrectedWidth/lensCorrectedHeight,4/3), "Eye\(index) real asymmetric projection preserves corrected image aspect")
            for yaw: Float in [0,.pi/2,.pi] {
                for recline: Float in [0,.pi/3] {
                    var world = simd_float4x4(simd_quatf(angle:yaw,axis:SIMD3(0,1,0))) *
                        simd_float4x4(simd_quatf(angle:recline,axis:SIMD3(1,0,0)))
                    world.columns.3 = SIMD4(2,1.7,-4,1)
                    let physical = p*simd_inverse(world*eye)*(world*center)
                    let expected = simd_make_float3(physical)/physical.w
                    check(close(ndc,expected,0.0001), "Eye\(index) yaw\(yaw)/recline\(recline) cannot leak into head-local scope panel")
                }
            }
        }
        check(abs(ndcCenters[0].x-ndcCenters[1].x) > 0.10 && abs(ndcCenters[0].y-ndcCenters[1].y) > 0.01,
              "Asymmetric eyes require distinct NDC centers, despite common head-forward image target")
    }
    static func main() {
        freshnessAndIdentity(); clocks(); geometry()
        if !failures.isEmpty {
            failures.forEach { print("FAIL: \($0)") }
            print("FAIL \(failures.count) of \(checks) scope presentation checks")
            exit(1)
        }
        print("PASS \(checks) scope frame identity, freshness and real-eye geometry checks")
    }
}
