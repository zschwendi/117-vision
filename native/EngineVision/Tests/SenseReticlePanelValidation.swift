// Same-target witness for the tracked reticle. For each eye, the place the
// shipped panel mesh shows the engine's own picture of the weapon's target
// (found by searching that mesh's own triangles for the target's texel) must
// be where the reticle's aim anchor is drawn, seen from static and moving
// heads, stereo (level and rolled) and mono. The same assertion is applied to
// the earlier true-depth placement, which must fail it.
import Foundation
import simd

/// Triangles of one shipped panel mesh, bucketed by texture coordinate.
final class MeshIndex {
    let mesh: (vertices: [EngineImmersiveScreenVertex], indices: [UInt32])
    var buckets = [[Int]](repeating: [], count: 128 * 128)
    init(_ mesh: (vertices: [EngineImmersiveScreenVertex], indices: [UInt32])) {
        self.mesh = mesh
        func bucket(_ x: Float) -> Int { min(127, max(0, Int((x * 128).rounded(.down)))) }
        for t in stride(from: 0, to: mesh.indices.count - 2, by: 3) {
            let uvs = (0..<3).map { mesh.vertices[Int(mesh.indices[t + $0])].uv }
            let lo = uvs.reduce(SIMD2<Float>(repeating: 1), { simd_min($0, $1) }), hi = uvs.reduce(SIMD2<Float>(repeating: 0), { simd_max($0, $1) })
            for y in bucket(lo.y)...bucket(hi.y) { for x in bucket(lo.x)...bucket(hi.x) { buckets[y * 128 + x].append(t) } }
        }
    }
    /// The mesh position the rasterizer gives texture coordinate `uv`.
    func locate(_ uv: SIMD2<Float>) -> SIMD3<Float>? {
        guard uv.x >= 0, uv.x <= 1, uv.y >= 0, uv.y <= 1 else { return nil }
        let x = min(127, Int(uv.x * 128)), y = min(127, Int(uv.y * 128))
        for t in buckets[y * 128 + x] {
            let a = mesh.vertices[Int(mesh.indices[t])], b = mesh.vertices[Int(mesh.indices[t + 1])], c = mesh.vertices[Int(mesh.indices[t + 2])]
            let v0 = b.uv - a.uv, v1 = c.uv - a.uv, v2 = uv - a.uv
            let den = v0.x * v1.y - v1.x * v0.y
            guard abs(den) > 1e-14 else { continue }
            let wb = (v2.x * v1.y - v1.x * v2.y) / den, wc = (v0.x * v2.y - v2.x * v0.y) / den, wa = 1 - wb - wc
            if wa >= -1e-5 && wb >= -1e-5 && wc >= -1e-5 {
                return wa * simd_make_float3(a.position) + wb * simd_make_float3(b.position) + wc * simd_make_float3(c.position)
            }
        }
        return nil
    }
}

@main struct SenseReticlePanelValidation {
    typealias G = EngineSenseReticleGeometry
    typealias S = EngineImmersiveScreenGeometry
    static var checks = 0
    static func require(_ value: Bool, _ message: @autoclosure () -> String) { checks += 1; precondition(value, message()) }

    static let separation: Float = 0.0103346457 * 3.048   // 63 mm IPD, half per eye, metres
    static let artPixel: Float = 2 * tan(G.verticalHalfAngle) / 1080   // radians per art pixel

    /// Projections as the host reports them for 64-degree bands and a
    /// 105-degree vertical field; the centre may be letterboxed.
    static func sources(letterbox: Bool) -> [S.PanoramaSource] {
        let band = SIMD2<Float>(1 / tan(32 * .pi / 180), 1 / tan(52.5 * .pi / 180))
        let cap = SIMD2<Float>(1 / tan(52.5 * .pi / 180), 1 / tan(52.5 * .pi / 180))
        let projections = Array(repeating: band, count: 6) + [cap, cap]
        var viewports = Array(repeating: SIMD4<Float>(0, 0, 1, 1), count: 8)
        if letterbox { viewports[1] = SIMD4(0, 0.1, 1, 0.9) }
        return S.panoramaSources(projections: projections, viewports: viewports)
    }

    static func halo(_ c: SIMD3<Float>) -> SIMD3<Float> { SIMD3(-c.z, -c.x, c.y) }
    static func direction(yaw: Float, pitch: Float) -> SIMD3<Float> {
        let y = yaw * .pi / 180, p = pitch * .pi / 180
        return SIMD3(sin(y) * cos(p), sin(p), -cos(y) * cos(p))
    }

    /// Where the shipped mesh of `panel` shows the texel the engine drew for
    /// `target` from `camera` (compositor coordinates).
    static func shown(_ meshes: [MeshIndex], _ sources: [S.PanoramaSource], panel: Int,
                      camera: SIMD3<Float>, target: SIMD3<Float>) -> SIMD3<Float>? {
        let source = sources[panel]
        guard let p = S.imagePoint(of: source, simd_normalize(target - camera)) else { return nil }
        let vp = source.viewport
        return meshes[panel].locate(SIMD2((vp.x + vp.z) / 2 + p.x / 2 * (vp.z - vp.x), (vp.y + vp.w) / 2 + p.y / 2 * (vp.w - vp.y)))
    }

    static func angle(_ a: SIMD3<Float>, _ b: SIMD3<Float>, from eye: SIMD3<Float>) -> Float {
        let x = simd_normalize(a - eye), y = simd_normalize(b - eye)
        return atan2(simd_length(simd_cross(x, y)), simd_dot(x, y))
    }

    struct Head {
        var position: SIMD3<Float>
        var rotation: simd_quatf
        func eye(_ k: Int) -> SIMD3<Float> { position + rotation.act(SIMD3(k == 0 ? -0.0315 : 0.0315, 0, 0)) }
    }

    /// Where a triangle list places texture coordinate `uv`.
    static func place(_ uv: SIMD2<Float>, in vertices: [EngineImmersiveScreenVertex]) -> SIMD3<Float>? {
        var t = 0
        while t + 2 < vertices.count {
            let a = vertices[t], b = vertices[t + 1], c = vertices[t + 2]; t += 3
            let v0 = b.uv - a.uv, v1 = c.uv - a.uv, v2 = uv - a.uv
            let den = v0.x * v1.y - v1.x * v0.y
            guard abs(den) > 1e-12 else { continue }
            let wb = (v2.x * v1.y - v1.x * v2.y) / den, wc = (v0.x * v2.y - v2.x * v0.y) / den, wa = 1 - wb - wc
            if wa >= -1e-5 && wb >= -1e-5 && wc >= -1e-5 {
                // Rasterization interpolates the homogeneous position.
                let h = wa * a.position + wb * b.position + wc * c.position
                return simd_make_float3(h) / h.w
            }
        }
        return nil
    }
    /// A vertex's point after the homogeneous divide.
    static func point(_ v: EngineImmersiveScreenVertex) -> SIMD3<Float> { simd_make_float3(v.position) / v.position.w }

    static func main() {
        let heads = [Head(position: .zero, rotation: simd_quatf(angle: 0, axis: SIMD3(0, 1, 0))),
                     Head(position: SIMD3(0.25, -0.12, 0.3),
                          rotation: simd_quatf(angle: 0.35, axis: SIMD3(0, 0, -1)) * simd_quatf(angle: -0.3, axis: SIMD3(0, 1, 0)))]
        // (yaw, pitch, panel supplying the picture there)
        let directions: [(Float, Float, Int)] = [(0, 0, 1), (-12, 8, 1), (20, -25, 1), (60, 0, 2), (-60, 5, 0),
                                                 (0, 75, 6), (0, -78, 7), (180, 0, 4)]
        let distances: [Float] = [0.6, 2, 4.25, 12, 40, 152.4]
        let eyeModes: [(String, [SIMD3<Float>])] = [
            ("stereo level", [SIMD3(0, separation, 0), SIMD3(0, -separation, 0)]),
            ("stereo rolled 0.35", [SIMD3(0, separation * cos(0.35), -separation * sin(0.35)),
                                    SIMD3(0, -separation * cos(0.35), separation * sin(0.35))]),
            ("mono", [.zero, .zero]),
        ]
        let crops: [(SIMD4<Float>, SIMD2<Float>)] = [
            (SIMD4(703, 283, 1215, 795), SIMD2(959.5, 539.5)),     // the capture's own crop round the anchor
            (SIMD4(500, 300, 1012, 812), SIMD2(960, 540)),         // anchor far off the crop's centre
        ]
        var worstNew: Float = 0, worstVertex: Float = 0, worstInterior: Float = 0, worstLegacy: Float = 0, worstCrop: Float = 0
        var legacyFailures = 0, eyeCases = 0, cropOffMesh = 0, worstInteriorCase = ""
        // Half-size, in art pixels, of the artwork window round the anchor the
        // checks require: a reticle up to 128 art pixels across. The rest of
        // the 512 crop is transparent unless the original draws there.
        let visibleArt: Float = 64
        for letterbox in [false, true] {
            let src = sources(letterbox: letterbox)
            let meshes = S.sphericalPanorama(sources: src, texelSize: SIMD2(1.0 / 1536, 1.0 / 1536)).map { MeshIndex($0) }
            for (yaw, pitch, expected) in directions {
                for distance in distances {
                    let target = distance * direction(yaw: yaw, pitch: pitch)
                    for (mode, eyes) in eyeModes {
                        let cameras = eyes.map { G.compositorVector($0) }
                        for (bounds, anchor) in crops {
                            let sample = G.Sample(point: halo(target), hand: 1, bounds: bounds, anchor: anchor,
                                                  viewport: SIMD2(1920, 1080))
                            let dominant = G.dominantPanel(sources: src, direction: simd_normalize(target))
                            require(dominant?.index == expected && (dominant?.share ?? 0) > 0.999,
                                    "\(mode) \(yaw),\(pitch): picture comes from panel \(expected) alone")
                            let reticle = G.panelVertices(samples: [sample], sources: src, sourceEyes: eyes)
                            require(!reticle.isEmpty && reticle.count % 12 == 0 && reticle.count <= G.maximumPanelVerticesPerHand,
                                    "whole cells, 6 vertices each, both eyes (\(reticle.count))")
                            // No cell wider than cellArtPixels of the crop on either axis.
                            let gridU = Set(reticle.map(\.uv.x)).sorted(), gridV = Set(reticle.map(\.uv.y)).sorted()
                            require(zip(gridU, gridU.dropFirst()).allSatisfy { ($1 - $0) * (bounds.z - bounds.x) <= G.cellArtPixels + 1e-3 } &&
                                    zip(gridV, gridV.dropFirst()).allSatisfy { ($1 - $0) * (bounds.w - bounds.y) <= G.cellArtPixels + 1e-3 },
                                    "grid cells at most \(G.cellArtPixels) art px")
                            let legacy = G.vertices(samples: [sample], headPosition: .zero)
                            require(legacy.count == 6, "earlier placement draws one quad")
                            let ua = (anchor.x - bounds.x) / (bounds.z - bounds.x), va = (anchor.y - bounds.y) / (bounds.w - bounds.y)
                            // Any artwork point on the true-depth billboard the earlier
                            // placement drew, from its own corners.
                            func at(_ u: Float, _ v: Float) -> SIMD3<Float> {
                                simd_make_float3(legacy.first { $0.uv == SIMD2(u, v) }!.position)
                            }
                            func billboard(_ uv: SIMD2<Float>) -> SIMD3<Float> {
                                let top = at(0, 0) + uv.x * (at(1, 0) - at(0, 0)), bottom = at(0, 1) + uv.x * (at(1, 1) - at(0, 1))
                                return top + uv.y * (bottom - top)
                            }
                            let oldAnchor = billboard(SIMD2(ua, va))
                            require(simd_distance(oldAnchor, target) < 1e-3 * max(1, distance), "the anchor is the weapon's hit")
                            for k in 0..<2 {
                                let camera = expected == G.centrePanel ? cameras[k] : .zero
                                guard let truth = shown(meshes, src, panel: expected, camera: camera, target: target) else {
                                    preconditionFailure("\(mode) letterbox=\(letterbox) yaw=\(yaw) pitch=\(pitch) d=\(distance) eye=\(k): target texel not on the panel mesh")
                                }
                                let mine = stride(from: k, to: reticle.count, by: 2).map { reticle[$0] }
                                guard let anchorVertex = mine.first(where: { abs($0.uv.x - ua) < 1e-6 && abs($0.uv.y - va) < 1e-6 }) else {
                                    preconditionFailure("the aim anchor is a vertex")
                                }
                                let placed = point(anchorVertex)
                                let where_ = "\(mode) letterbox=\(letterbox) yaw=\(yaw) pitch=\(pitch) d=\(distance) crop=\(bounds) eye=\(k)"
                                let crop = SIMD2(bounds.z - bounds.x, bounds.w - bounds.y)
                                // Art-pixel offset of a crop point from the aim anchor.
                                func offset(_ uv: SIMD2<Float>) -> SIMD2<Float> { (uv - SIMD2(ua, va)) * crop }
                                // The crop's grid vertices (its corners and the anchor). Away
                                // from the artwork the crop is transparent; those points are
                                // reported, and may lie past the panel's mesh.
                                var seen = Set<SIMD2<Float>>()
                                for vertex in mine where seen.insert(vertex.uv).inserted {
                                    let o = offset(vertex.uv)
                                    guard let own = shown(meshes, src, panel: expected, camera: camera, target: billboard(vertex.uv)) else {
                                        require(max(abs(o.x), abs(o.y)) > visibleArt, "\(where_): visible art at offset \(o) is off the panel mesh")
                                        cropOffMesh += 1; continue
                                    }
                                    for head in heads {
                                        let e = angle(point(vertex), own, from: head.eye(k)) / artPixel
                                        if max(abs(o.x), abs(o.y)) <= visibleArt { worstVertex = max(worstVertex, e) } else { worstCrop = max(worstCrop, e) }
                                    }
                                }
                                // Artwork between vertices, within the visible window round
                                // the anchor (required) and out in the transparent crop (reported).
                                let visibleOffsets: [SIMD2<Float>] = [SIMD2(32, 0), SIMD2(-32, 0), SIMD2(0, 32), SIMD2(0, -32),
                                                                      SIMD2(16, -16), SIMD2(-48, 56), SIMD2(64, 64), SIMD2(-64, -64)]
                                let cropOffsets: [SIMD2<Float>] = [SIMD2(-200, -200), SIMD2(120, 180), SIMD2(-240, 100)]
                                for (o, visible) in visibleOffsets.map({ ($0, true) }) + cropOffsets.map({ ($0, false) }) {
                                    let uv = SIMD2(ua, va) + o / crop
                                    guard uv.x >= 0, uv.x <= 1, uv.y >= 0, uv.y <= 1 else { continue }
                                    guard let own = shown(meshes, src, panel: expected, camera: camera, target: billboard(uv)) else {
                                        require(!visible, "\(where_): visible art at offset \(o) is off the panel mesh")
                                        cropOffMesh += 1; continue
                                    }
                                    guard let located = place(uv, in: mine) else { preconditionFailure("\(where_): uv \(uv) not covered by the reticle") }
                                    for (h, head) in heads.enumerated() {
                                        let e = angle(located, own, from: head.eye(k)) / artPixel
                                        if visible {
                                            if e > worstInterior { worstInteriorCase = "\(where_) head=\(h) offset=\(o) art px" }
                                            worstInterior = max(worstInterior, e)
                                        } else { worstCrop = max(worstCrop, e) }
                                    }
                                }
                                for head in heads {
                                    // The same assertion for both placements: the aimed pixel
                                    // within a hundredth of an art pixel of the target's own texel.
                                    let error = angle(placed, truth, from: head.eye(k)) / artPixel
                                    worstNew = max(worstNew, error)
                                    require(error < 0.01, "\(mode) \(yaw),\(pitch) at \(distance) m, eye \(k): reticle off its target by \(error) px")
                                    // The earlier quad stood around the head it was computed for.
                                    let legacyError = angle(oldAnchor + head.position, truth, from: head.eye(k)) / artPixel
                                    worstLegacy = max(worstLegacy, legacyError)
                                    if legacyError >= 0.01 { legacyFailures += 1 }
                                }
                                eyeCases += 1
                            }
                        }
                    }
                }
            }
        }
        print("worst visible interior: \(worstInterior) art px at \(worstInteriorCase); worst visible vertex \(worstVertex) art px; anchor \(worstNew) art px")
        require(worstVertex < 0.01, "every vertex in the visible window on its own texel's place (\(worstVertex) px)")
        require(worstInterior < 0.5, "visible artwork between vertices within half an art pixel of its own texel (\(worstInterior) px)")
        print(String(format: "transparent crop beyond +/-%.0f art px: %ld points past the panel mesh (not checked), others within %.3f art px (reported only)",
                     Double(visibleArt), cropOffMesh, Double(worstCrop)))
        require(legacyFailures > eyeCases, "the earlier true-depth placement fails the same-target assertion (\(legacyFailures) of \(eyeCases * 2))")
        // Layer alignment turns side panels (never the centre) and is not
        // modelled: a side target keeps the earlier quad bit for bit, a centre
        // target is still mapped.
        do {
            let src = sources(letterbox: false), head = SIMD3<Float>(0.1, -0.05, 0.2)
            for (yaw, centre) in [(Float(60), false), (Float(0), true)] {
                let sample = G.Sample(point: halo(12 * direction(yaw: yaw, pitch: 0)), hand: 1,
                                      bounds: SIMD4(703, 283, 1215, 795), anchor: SIMD2(959.5, 539.5), viewport: SIMD2(1920, 1080))
                let aligned = G.panelVertices(samples: [sample], sources: src, sourceEyes: eyeModes[0].1,
                                              sidesAligned: true, headPosition: head)
                let unaligned = G.panelVertices(samples: [sample], sources: src, sourceEyes: eyeModes[0].1)
                let earlier = G.interleaved(G.vertices(samples: [sample], headPosition: head))
                if centre {
                    require(!aligned.isEmpty && aligned.count == unaligned.count && aligned.count != earlier.count && zip(aligned, unaligned).allSatisfy { $0.position == $1.position && $0.uv == $1.uv },
                            "aligned frames still map a centre-panel target")
                } else {
                    require(aligned.count == earlier.count && zip(aligned, earlier).allSatisfy { $0.position == $1.position && $0.uv == $1.uv && $0.weight == $1.weight },
                            "aligned side panels keep the earlier true-depth quad exactly")
                }
            }
        }
        // A blended join: both pictures supply some of it and their copies of
        // the target differ; the reticle follows the larger share.
        let src = sources(letterbox: false)
        let meshes = S.sphericalPanorama(sources: src, texelSize: SIMD2(1.0 / 1536, 1.0 / 1536)).map { MeshIndex($0) }
        let join = 12 * direction(yaw: 30, pitch: 0)
        let shares = S.panelShares(sources: src, direction: simd_normalize(join))
        let dominant = G.dominantPanel(sources: src, direction: simd_normalize(join))!
        require(shares[1] > 0.001 && shares[2] > 0.001 && dominant.share < 0.999, "30 degrees is inside the centre/right blend")
        let left = G.compositorVector(eyeModes[0].1[0])
        let centreCopy = shown(meshes, src, panel: 1, camera: left, target: join)!
        let sideCopy = shown(meshes, src, panel: 2, camera: .zero, target: join)!
        let joinSpread = angle(centreCopy, sideCopy, from: heads[0].eye(0)) / artPixel
        print(String(format: "PASS %ld checks: anchor within %.5f art px of its target texel for both eyes (static/moving head, stereo level/rolled, mono, letterboxed centre, 0.6-152.4 m, eight bearings); artwork vertices within %.5f px, interior within %.3f px; earlier placement off by up to %.1f px, failing %ld of %ld eye/head cases; at a 30-degree centre/right join (centre share %.2f, panel %ld chosen) the two pictures' copies of a 12 m target differ by %.2f px",
                     checks, Double(worstNew), Double(worstVertex), Double(worstInterior), Double(worstLegacy),
                     legacyFailures, eyeCases * 2, Double(shares[1]), dominant.index, Double(joinSpread)))
    }
}
