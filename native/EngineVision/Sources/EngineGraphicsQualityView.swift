import SwiftUI

/// Live image-quality settings (halo_graphics_* in halo_settings.c) and what
/// the renderer reports it actually ran. Every value starts from its
/// environment variable, then from the saved preference unless that variable
/// was set, exactly as the presentation settings do.
@MainActor
final class EngineGraphicsQualityModel: ObservableObject {
    @Published var sceneAA: Int { didSet { push() } }
    @Published var foveation: Int { didSet { push() } }
    @Published var gpuTiming: Bool { didSet { push() } }
    @Published private(set) var report = EngineGraphicsReport()

    static let preferenceKey = "MasterCheefGraphicsV1"
    private let defaults: UserDefaults
    private var restoring = false

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        var current = HaloGraphicsSettings()
        halo_graphics_get(&current)
        sceneAA = Int(current.scene_aa); foveation = Int(current.engine_foveation); gpuTiming = current.gpu_timing != 0
        restoring = true
        if let data = defaults.data(forKey: Self.preferenceKey),
           let saved = try? JSONDecoder().decode(EngineGraphicsPreferences.self, from: data), saved.version == 1 {
            let valid = saved.validated()
            if !Self.overridden("HALO_SCENE_AA", "HALO_FXAA") { sceneAA = valid.sceneAA }
            if !Self.overridden("HALO_ENGINE_FOVEATION") { foveation = valid.foveation }
            if !Self.overridden("HALO_GPU_TIMING") { gpuTiming = valid.gpuTiming }
        }
        restoring = false
        push(persist: false)
    }

    private static func overridden(_ names: String...) -> Bool {
        names.contains { name in getenv(name).map { $0.pointee != 0 } ?? false }
    }

    func reset() {
        restoring = true
        sceneAA = Int(HALO_SCENE_AA_FXAA); foveation = Int(HALO_FOVEATION_OFF); gpuTiming = false
        restoring = false
        push()
    }

    private func push(persist: Bool = true) {
        guard !restoring else { return }
        var settings = HaloGraphicsSettings()
        settings.scene_aa = Int32(sceneAA); settings.engine_foveation = Int32(foveation); settings.gpu_timing = gpuTiming ? 1 : 0
        halo_graphics_set(&settings)
        guard persist else { return }
        let saved = EngineGraphicsPreferences(sceneAA: sceneAA, foveation: foveation, gpuTiming: gpuTiming).validated()
        if let data = try? JSONEncoder().encode(saved) { defaults.set(data, forKey: Self.preferenceKey) }
    }

    func refresh() { report = EngineGraphicsReport.current() }
}

/// One reading of the renderer's own account, already in words.
struct EngineGraphicsReport {
    var showing = "Nothing presented yet"
    var antiAliasing = "Not yet drawn"
    var upscaling = "Not yet drawn"
    var sizes = "—"
    var hud = "—"
    var reticle = "—"
    var foveation = "Not yet drawn"
    var timingStatus = "off"
    var timing: [String] = []
    var detail = ""

    static func current() -> EngineGraphicsReport {
        var status = HaloGraphicsStatus(); halo_graphics_status_read(&status)
        var post = mr_post_status_t(); mr_post_status(&post)
        var timing = mr_gpu_timing_stats(); mr_gpu_timing_read(&timing)
        var r = EngineGraphicsReport()
        let shown = Int(status.presentation)
        r.showing = EngineGraphicsText.presentation(shown)
        let smaaReason = post.smaa == Int32(MR_POST_UNAVAILABLE) ? "unavailable" : "preparing"
        r.antiAliasing = EngineGraphicsText.running(selected: EngineGraphicsText.antiAliasing(Int(status.scene_aa_selected)),
                                                    effective: EngineGraphicsText.antiAliasing(Int(status.scene_aa_effective)),
                                                    reason: EngineGraphicsText.reason(presentation: shown,
                                                        building: status.scene_aa_selected == Int32(HALO_SCENE_AA_SMAA) ? smaaReason : "not applied"))
        let metalfxReason = post.metalfx == Int32(MR_POST_UNAVAILABLE) ? "unavailable" : "preparing"
        r.upscaling = EngineGraphicsText.running(selected: EngineGraphicsText.upscale(Int(status.upscale_selected)),
                                                 effective: EngineGraphicsText.upscale(Int(status.upscale_effective)),
                                                 reason: EngineGraphicsText.reason(presentation: shown,
                                                     building: status.upscale_selected == Int32(HALO_UPSCALE_METALFX) ? metalfxReason : "not applied"))
        r.sizes = status.output_width == status.render_width && status.output_height == status.render_height
            ? EngineGraphicsText.size(status.render_width, status.render_height)
            : "\(EngineGraphicsText.size(status.render_width, status.render_height)) → \(EngineGraphicsText.size(status.output_width, status.output_height))"
        r.hud = EngineGraphicsText.size(status.hud_width, status.hud_height)
        r.reticle = EngineGraphicsText.size(status.reticle_art_width, status.reticle_art_height)
        let selected = EngineGraphicsText.foveation(Int(status.foveation_selected))
        if status.foveation_effective == -1 { r.foveation = "\(selected): unsupported on this device; drawing at full rate" }
        else if status.foveated_layers > 0 {
            r.foveation = "\(EngineGraphicsText.foveation(Int(status.foveation_effective))) · \(status.foveated_layers) of \(status.world_layers) layers this frame · about \(Int((status.foveation_shading * 100).rounded())) % of their pixels shaded"
        } else if status.foveation_selected == Int32(HALO_FOVEATION_OFF) { r.foveation = "Off" }
        else if shown == Int(HALO_PRESENTATION_PANORAMA) || shown == Int(HALO_PRESENTATION_PANORAMA_RETAINED) {
            r.foveation = "\(selected) · no layer drawn through a rate map yet"
        } else { r.foveation = "\(selected) · \(EngineGraphicsText.reason(presentation: shown, building: ""))" }
        r.detail = Self.string(status.detail)
        r.timingStatus = Self.string(timing.status)
        if timing.window > 0 {
            let mean = Self.doubles(timing.mean_ms), p95 = Self.doubles(timing.p95_ms)
            let names = ["Scene", "HUD", "Post", "Copies"]
            r.timing = names.indices.map { "\(names[$0]) \(EngineGraphicsText.milliseconds(mean[$0])) (p95 \(EngineGraphicsText.milliseconds(p95[$0])))" }
            r.timing.append("Busy \(EngineGraphicsText.milliseconds(timing.busy_mean_ms)) (p95 \(EngineGraphicsText.milliseconds(timing.busy_p95_ms)))")
            r.timing.append("Engine command buffers: first start to last end \(EngineGraphicsText.milliseconds(timing.command_span_mean_ms)), summed durations \(EngineGraphicsText.milliseconds(timing.command_sum_mean_ms))")
            r.timing.append("\(timing.window) recent frames · \(timing.frames_measured) measured · \(timing.frames_incomplete + timing.frames_truncated + timing.frames_failed) not measurable")
        }
        return r
    }
    static func string<T>(_ tuple: T) -> String {
        withUnsafeBytes(of: tuple) { raw in String(cString: raw.bindMemory(to: CChar.self).baseAddress!) }
    }
    static func doubles<T>(_ tuple: T) -> [Double] { withUnsafeBytes(of: tuple) { Array($0.bindMemory(to: Double.self)) } }

    /// The report's "graphics" group: selected and effective modes, sizes,
    /// foveation and measured GPU time, never an estimate.
    static func diagnostics() -> [String: Any] {
        var status = HaloGraphicsStatus(); halo_graphics_status_read(&status)
        var post = mr_post_status_t(); mr_post_status(&post)
        var timing = mr_gpu_timing_stats(); mr_gpu_timing_read(&timing)
        var vrr = mr_vrr_stats_t(); mr_vrr_stats(&vrr)
        return [
            "presentation": status.presentation, "presentationName": EngineGraphicsText.presentation(Int(status.presentation)),
            "sceneAASelected": status.scene_aa_selected, "sceneAAEffective": status.scene_aa_effective,
            "upscaleSelected": status.upscale_selected, "upscaleEffective": status.upscale_effective,
            "upscaleNextLaunch": UserDefaults.standard.string(forKey: EngineSceneUpscaleChoice.preferenceKey) ?? "off",
            "foveationSelected": status.foveation_selected, "foveationEffective": status.foveation_effective,
            "renderWidth": status.render_width, "renderHeight": status.render_height,
            "outputWidth": status.output_width, "outputHeight": status.output_height,
            "hudWidth": status.hud_width, "hudHeight": status.hud_height,
            "trackedReticleArtRasterWidth": status.reticle_art_width, "trackedReticleArtRasterHeight": status.reticle_art_height,
            "upscaleScope": "Immersive panorama only. Flat menu/loading frames, the window view and iPad present the engine back buffer at its render size; tracked Sense reticle artwork stays at the logical HUD raster.",
            "worldLayers": status.world_layers, "foveatedLayers": status.foveated_layers,
            "foveationShading": Double(status.foveation_shading), "detail": string(status.detail),
            "smaaState": post.smaa, "metalfxState": post.metalfx, "postDetail": string(post.detail),
            "vrrMappedPasses": vrr.mapped_passes, "vrrUnmappedPasses": vrr.unmapped_passes,
            "vrrResolves": vrr.resolves, "vrrUploadReencodes": vrr.upload_reencodes,
            "vrrRetainedLayoutPasses": vrr.retained_layout_passes,
            "gpuTiming": [
                "supported": timing.supported, "enabled": timing.enabled != 0, "status": string(timing.status),
                "framesMeasured": timing.frames_measured, "framesIncomplete": timing.frames_incomplete,
                "framesTruncated": timing.frames_truncated, "framesFailed": timing.frames_failed,
                "emptyIntervals": timing.empty_intervals, "window": timing.window,
                "meanMilliseconds": doubles(timing.mean_ms), "p95Milliseconds": doubles(timing.p95_ms),
                "maxMilliseconds": doubles(timing.max_ms), "categories": ["scene", "hud", "post", "copy"],
                "busyMeanMilliseconds": timing.busy_mean_ms, "busyP95Milliseconds": timing.busy_p95_ms,
                "commandSpanMeanMilliseconds": timing.command_span_mean_ms, "commandSpanP95Milliseconds": timing.command_span_p95_ms,
                "commandSumMeanMilliseconds": timing.command_sum_mean_ms, "nanosecondsPerTick": timing.ns_per_tick,
                "scope": "Engine renderer command buffers only. Categories are exclusive shares of the union of sampled passes (busy); commandSpan is first GPU start to last GPU end of the frame's engine command buffers, including gaps; commandSum adds each buffer's duration. None is compositor time (immersiveGPUMilliseconds)."
            ] as [String: Any]
        ]
    }
}

struct EngineGraphicsQualityView: View {
    @ObservedObject var model: EngineGraphicsQualityModel
    @AppStorage(EngineSceneUpscaleChoice.preferenceKey) private var upscale = EngineSceneUpscaleChoice.off.rawValue
    @AppStorage("HaloRenderResolution") private var resolution = "2048x1536"
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        List {
            Section {
                Picker("Scene anti-aliasing", selection: $model.sceneAA) {
                    Text("FXAA").tag(Int(HALO_SCENE_AA_FXAA))
                    Text("SMAA").tag(Int(HALO_SCENE_AA_SMAA))
                    Text("Off").tag(Int(HALO_SCENE_AA_OFF))
                }.senseControllerTarget("quality.aa")
                LabeledContent("Running", value: model.report.antiAliasing).font(.footnote).foregroundStyle(.secondary)
            } header: { Text("Edges") } footer: {
                Text("Applies to the game world only; the HUD, text and reticle are never filtered. SMAA keeps more texture detail than FXAA and takes a moment to prepare the first time.")
            }
            Section {
                Picker("Scene upscaling at next launch", selection: $upscale) {
                    ForEach(EngineSceneUpscaleChoice.allCases, id: \.rawValue) { Text($0.title).tag($0.rawValue) }
                }.senseControllerTarget("quality.upscale")
                if let choice = EngineSceneUpscaleChoice(rawValue: upscale), let size = choice.renderSize(output: resolution) {
                    LabeledContent("Next launch", value: "World \(size.width) × \(size.height) → \(resolution.replacingOccurrences(of: "x", with: " × "))")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                LabeledContent("Showing", value: model.report.showing).font(.footnote).foregroundStyle(.secondary)
                LabeledContent("Running", value: model.report.upscaling).font(.footnote).foregroundStyle(.secondary)
                LabeledContent("World layers", value: model.report.sizes).font(.footnote).foregroundStyle(.secondary)
                LabeledContent("HUD raster", value: model.report.hud).font(.footnote).foregroundStyle(.secondary)
                LabeledContent("Tracked reticle raster", value: model.report.reticle).font(.footnote).foregroundStyle(.secondary)
            } header: { Text("Upscaling · immersive play") } footer: {
                Text("In immersive play the world renders smaller and MetalFX scales it to your resolution, while Halo draws its HUD at the full resolution. Tracked Sense reticles stay at the smaller engine size, and flat menus and loading screens are shown at the engine size. Takes effect after the app is fully closed and reopened. Fine texture detail cannot be recovered by upscaling.")
            }
            Section {
                Picker("Engine foveation", selection: $model.foveation) {
                    Text("Off").tag(Int(HALO_FOVEATION_OFF))
                    Text("Balanced").tag(Int(HALO_FOVEATION_BALANCED))
                    Text("Aggressive").tag(Int(HALO_FOVEATION_AGGRESSIVE))
                }.senseControllerTarget("quality.foveation")
                Text(model.report.foveation).font(.footnote).foregroundStyle(.secondary)
            } header: { Text("Foveation") } footer: {
                Text("Experimental. Balanced lowers the shading rate only in parts of each view the headset never shows; Aggressive also halves the rate of the views behind you and of the sky and floor, which looks softer when you turn to them. Each foveated view needs an extra resolve pass, so measure GPU time to see whether it helps.")
            }
            Section {
                Toggle("Measure GPU time", isOn: $model.gpuTiming).senseControllerTarget("quality.timing")
                Text(model.report.timingStatus).font(.footnote).foregroundStyle(.secondary)
                ForEach(model.report.timing, id: \.self) { Text($0).font(.footnote.monospacedDigit()) }
            } header: { Text("Measurement") } footer: {
                Text("Times the game's own rendering on the GPU for each completed frame. Leave it off when not comparing settings.")
            }
            if !model.report.detail.isEmpty {
                Section { Text(model.report.detail).font(.footnote).foregroundStyle(.secondary) }
            }
            Section {
                Button("Reset Image Quality") { model.reset(); upscale = EngineSceneUpscaleChoice.off.rawValue }
                    .senseControllerTarget("quality.reset")
            }
        }
        .task {
            while !Task.isCancelled {
                model.refresh()
                try? await Task.sleep(nanoseconds: 1_000_000_000)
            }
        }
        .senseControllerNavigation(controllerItems, back: { dismiss() })
        .navigationTitle("Image Quality")
    }

    private var controllerItems: [EngineControllerMenuItem] {
        [
            cycle("aa", "Scene anti-aliasing", EngineGraphicsText.antiAliasing(model.sceneAA),
                  [Int(HALO_SCENE_AA_FXAA), Int(HALO_SCENE_AA_SMAA), Int(HALO_SCENE_AA_OFF)], \.sceneAA),
            .init(id: "quality.upscale", title: "Scene upscaling at next launch", value: upscale,
                  activate: { cycleUpscale(1) }, adjust: cycleUpscale),
            cycle("foveation", "Engine foveation", EngineGraphicsText.foveation(model.foveation),
                  [Int(HALO_FOVEATION_OFF), Int(HALO_FOVEATION_BALANCED), Int(HALO_FOVEATION_AGGRESSIVE)], \.foveation),
            .init(id: "quality.timing", title: "Measure GPU time", value: "\(model.gpuTiming)",
                  activate: { model.gpuTiming.toggle() }, adjust: { model.gpuTiming = $0 > 0 }),
            .init(id: "quality.reset", title: "Reset Image Quality", activate: { model.reset(); upscale = EngineSceneUpscaleChoice.off.rawValue })
        ]
    }
    private func cycle(_ id: String, _ title: String, _ value: String, _ values: [Int],
                       _ key: ReferenceWritableKeyPath<EngineGraphicsQualityModel, Int>) -> EngineControllerMenuItem {
        let step: (Int) -> Void = { direction in
            let index = values.firstIndex(of: model[keyPath: key]) ?? 0
            model[keyPath: key] = values[(index + (direction < 0 ? values.count - 1 : 1)) % values.count]
        }
        return .init(id: "quality." + id, title: title, value: value, activate: { step(1) }, adjust: step)
    }
    private func cycleUpscale(_ direction: Int) {
        let values = EngineSceneUpscaleChoice.allCases.map(\.rawValue)
        let index = values.firstIndex(of: upscale) ?? 0
        upscale = values[(index + (direction < 0 ? values.count - 1 : 1)) % values.count]
    }
}
