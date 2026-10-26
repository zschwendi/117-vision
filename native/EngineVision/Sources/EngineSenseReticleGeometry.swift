import Foundation
import simd

/// Reticles use the weapon's collision ray in the same calibrated basis as
/// the panorama. Head orientation only affects projection, never the aim.
enum EngineSenseReticleGeometry {
    struct Sample {
        var point: SIMD3<Float>
        var hand: Int
        /// Exact captured source pixels, including the original aim offset.
        var bounds: SIMD4<Float>
        var anchor: SIMD2<Float>
        var viewport: SIMD2<Float>
    }

    /// Matches the original HUD's 38-degree vertical presentation. Artwork
    /// retains its native pixel proportions, spread and weapon-specific size.
    static let verticalHalfAngle = Float.pi * 19 / 180

    static func compositorVector(_ halo: SIMD3<Float>) -> SIMD3<Float> {
        SIMD3(-halo.y, halo.z, -halo.x)
    }

    static func vertices(samples: [Sample], headPosition: SIMD3<Float>) -> [EngineImmersiveScreenVertex] {
        guard headPosition.x.isFinite, headPosition.y.isFinite, headPosition.z.isFinite else { return [] }
        var result: [EngineImmersiveScreenVertex] = []
        for sample in samples {
            guard sample.hand == 0 || sample.hand == 1,
                  sample.point.x.isFinite, sample.point.y.isFinite, sample.point.z.isFinite else { continue }
            let b = sample.bounds, a = sample.anchor, v = sample.viewport
            guard [b.x, b.y, b.z, b.w, a.x, a.y, v.x, v.y].allSatisfy(\.isFinite),
                  v.x >= 1, v.y >= 1, v.x <= 8192, v.y <= 8192,
                  b.z > b.x, b.w > b.y, b.z - b.x <= 1024, b.w - b.y <= 1024,
                  abs(b.x - a.x) <= 1024, abs(b.y - a.y) <= 1024,
                  abs(b.z - a.x) <= 1024, abs(b.w - a.y) <= 1024 else { continue }
            let relative = compositorVector(sample.point)
            let distance = simd_length(relative)
            guard distance.isFinite, distance >= 0.08, distance <= 10_000 else { continue }
            let forward = relative / distance
            let referenceUp = abs(forward.y) > 0.98 ? SIMD3<Float>(0, 0, 1) : SIMD3<Float>(0, 1, 0)
            let right = simd_normalize(simd_cross(forward, referenceUp))
            let up = simd_cross(right, forward)
            let center = headPosition + relative
            let pixelScale = distance * 2 * tan(verticalHalfAngle) / v.y
            let corners: [(Float, Float)] = [(0, 1), (1, 1), (0, 0), (0, 0), (1, 1), (1, 0)]
            for (u, v) in corners {
                let x = b.x + u * (b.z - b.x) - a.x
                let y = a.y - (b.y + v * (b.w - b.y))
                let position = center + pixelScale * (x * right + y * up)
                result.append(.init(position: SIMD4(position, 1), uv: SIMD2(u, v),
                                    weight: SIMD2(Float(sample.hand), 0)))
            }
        }
        return result
    }

    /// One vertex list per eye, interleaved (left, right) for
    /// `sense_reticle_vertex`.
    static func interleaved(_ vertices: [EngineImmersiveScreenVertex]) -> [EngineImmersiveScreenVertex] {
        vertices.flatMap { [$0, $0] }
    }

    /// The panel the picture at a direction comes from, and how much of it.
    static func dominantPanel(sources: [EngineImmersiveScreenGeometry.PanoramaSource],
                              direction: SIMD3<Float>) -> (index: Int, share: Float)? {
        let shares = EngineImmersiveScreenGeometry.panelShares(sources: sources, direction: direction)
        guard let best = shares.indices.max(by: { shares[$0] < shares[$1] }), shares[best] > 0 else { return nil }
        return (best, shares[best])
    }

    /// The source index of the centre bearing, the only one drawn per eye.
    static let centrePanel = 1

    /// Largest cell of the panel-mapped artwork grid, in source art pixels.
    /// Between grid lines a point keeps its own ray but sits on the cell's
    /// chord, nearer than the panel; seen from another eye that depth error
    /// shows as a shift that grows with the square of the cell's span.
    static let cellArtPixels: Float = 128

    /// Most vertices `panelVertices` returns per hand (both eyes): a 1024
    /// crop is cut into at most 9 cells per axis.
    static let maximumPanelVerticesPerHand = 9 * 9 * 6 * 2

    /// The reticle where this frame's world shows its target, per eye.
    ///
    /// The world is not at its true depth: each picture is laid on the
    /// panorama sphere along the rays from the camera it was drawn from, so
    /// one target is shown in two places for the two eyes. Every point of the
    /// original artwork, which `vertices` places around the weapon's true
    /// hit, is sent the same way: along its ray from the camera of the
    /// picture that covers it (this frame's left or right centre eye, or the
    /// head centre for the side bearings, the caps and mono frames) to where
    /// that panel's own mesh shows that ray. The aim, the hit, the image and
    /// its angular size are unchanged; only where each eye is shown them is.
    ///
    /// `sourceEyes` are the centre cameras in the reticle's own basis
    /// (metres; forward, left, up), frozen with the art. The artwork is
    /// split at its aim anchor, so the aimed pixel is a vertex and lands
    /// exactly; between vertices each pixel keeps its own ray from the source
    /// camera and lies within the cell's chord of the panel sphere. The
    /// panel is the one supplying most of the picture at the aim direction;
    /// where two blend, the other one's copy of the target is elsewhere and
    /// no single place is right for both. Layer alignment turns side panels
    /// but never the centre; that turn is not modelled here, so with
    /// `sidesAligned` a target a side panel supplies keeps the earlier
    /// true-depth quad (`vertices` at `headPosition`), exactly as before.
    /// Returns an empty list when nothing can be placed.
    static func panelVertices(samples: [Sample], sources: [EngineImmersiveScreenGeometry.PanoramaSource],
                              sourceEyes: [SIMD3<Float>], sidesAligned: Bool = false,
                              headPosition: SIMD3<Float> = .zero) -> [EngineImmersiveScreenVertex] {
        guard sources.count == EngineImmersiveScreenGeometry.panoramaAngles.count, sourceEyes.count == 2,
              sourceEyes.allSatisfy({ $0.x.isFinite && $0.y.isFinite && $0.z.isFinite && simd_length($0) < 0.5 })
        else { return [] }
        let eyes = sourceEyes.map { compositorVector($0) }
        var result: [EngineImmersiveScreenVertex] = []
        for sample in samples {
            guard sample.hand == 0 || sample.hand == 1,
                  sample.point.x.isFinite, sample.point.y.isFinite, sample.point.z.isFinite else { continue }
            let b = sample.bounds, a = sample.anchor, v = sample.viewport
            guard [b.x, b.y, b.z, b.w, a.x, a.y, v.x, v.y].allSatisfy(\.isFinite),
                  v.x >= 1, v.y >= 1, v.x <= 8192, v.y <= 8192,
                  b.z > b.x, b.w > b.y, b.z - b.x <= 1024, b.w - b.y <= 1024,
                  abs(b.x - a.x) <= 1024, abs(b.y - a.y) <= 1024,
                  abs(b.z - a.x) <= 1024, abs(b.w - a.y) <= 1024 else { continue }
            let relative = compositorVector(sample.point)
            let distance = simd_length(relative)
            guard distance.isFinite, distance >= 0.08, distance <= 10_000 else { continue }
            let forward = relative / distance
            guard let panel = dominantPanel(sources: sources, direction: forward) else { continue }
            if sidesAligned && panel.index != centrePanel {
                result += interleaved(vertices(samples: [sample], headPosition: headPosition))
                continue
            }
            let source = sources[panel.index]
            let referenceUp = abs(forward.y) > 0.98 ? SIMD3<Float>(0, 0, 1) : SIMD3<Float>(0, 1, 0)
            let right = simd_normalize(simd_cross(forward, referenceUp))
            let up = simd_cross(right, forward)
            let pixelScale = distance * 2 * tan(verticalHalfAngle) / v.y
            // Grid lines at the aim anchor (exact) and every cellArtPixels from
            // it, so no cell spans more than that much of the original crop.
            func splits(_ anchor: Float, _ extent: Float) -> [Float] {
                var points: Set<Float> = [0, 1]
                let inside = anchor > 0 && anchor < 1
                if inside { points.insert(anchor) }
                let origin = inside ? anchor * extent : 0
                var p = origin - cellArtPixels
                while p > 0 { points.insert(p / extent); p -= cellArtPixels }
                p = origin + cellArtPixels
                while p < extent { points.insert(p / extent); p += cellArtPixels }
                return points.sorted()
            }
            let us = splits((a.x - b.x) / (b.z - b.x), b.z - b.x), vs = splits((a.y - b.y) / (b.w - b.y), b.w - b.y)
            // Each grid point, as `vertices` places it around the true hit,
            // then per eye where its panel shows it. The position is written
            // homogeneously, scaled by its ray length from the source camera
            // over the panel radius: the rasterizer interpolates linearly in
            // homogeneous space, so every artwork point between vertices lands
            // along its own ray from that camera (the billboard is linear in
            // its pixels), not along a chord between the vertices' rays.
            var grid: [[[SIMD4<Float>]]] = []
            var mapped = true
            for gv in vs where mapped {
                var row: [[SIMD4<Float>]] = []
                for gu in us where mapped {
                    let x = b.x + gu * (b.z - b.x) - a.x
                    let y = a.y - (b.y + gv * (b.w - b.y))
                    let point = relative + pixelScale * (x * right + y * up)
                    var perEye: [SIMD4<Float>] = []
                    for eye in 0..<2 {
                        let origin = panel.index == centrePanel ? eyes[eye] : .zero
                        let ray = point - origin, length = simd_length(ray)
                        guard length.isFinite, length > 0,
                              let image = EngineImmersiveScreenGeometry.imagePoint(of: source, ray / length),
                              image.x.isFinite, image.y.isFinite, abs(image.x) < 4, abs(image.y) < 4
                        else { mapped = false; break }
                        let shown = EngineImmersiveScreenGeometry.panelPosition(source, image)
                        let w = length / EngineImmersiveScreenGeometry.panoramaRadius
                        perEye.append(SIMD4(shown * w, w))
                    }
                    row.append(perEye)
                }
                grid.append(row)
            }
            guard mapped else { continue }
            let corners: [(Int, Int)] = [(0, 1), (1, 1), (0, 0), (0, 0), (1, 1), (1, 0)]
            for r in 0..<(vs.count - 1) {
                for c in 0..<(us.count - 1) {
                    for (du, dv) in corners {
                        let column = c + du, line = r + dv
                        let uv = SIMD2(us[column], vs[line])
                        for eye in 0..<2 {
                            result.append(.init(position: grid[line][column][eye], uv: uv,
                                                weight: SIMD2(Float(sample.hand), 0)))
                        }
                    }
                }
            }
        }
        return result
    }
}
