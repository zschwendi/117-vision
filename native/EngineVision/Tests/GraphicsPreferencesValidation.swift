import Foundation

/// The image-quality preferences and words (EngineGraphicsPreferences.swift)
/// against the C settings they name: conservative defaults, validation of
/// stored values, the launcher's upscale sizes, and selected vs effective.
@main struct GraphicsPreferencesValidation {
    static func main() {
        let defaults = EngineGraphicsPreferences()
        precondition(defaults.sceneAA == Int(HALO_SCENE_AA_FXAA) && defaults.foveation == Int(HALO_FOVEATION_OFF) && !defaults.gpuTiming)
        var stored = EngineGraphicsPreferences(sceneAA: 9, foveation: -1, gpuTiming: true)
        stored = stored.validated()
        precondition(stored.sceneAA == Int(HALO_SCENE_AA_FXAA) && stored.foveation == Int(HALO_FOVEATION_OFF) && stored.gpuTiming)
        let smaa = EngineGraphicsPreferences(sceneAA: Int(HALO_SCENE_AA_SMAA), foveation: Int(HALO_FOVEATION_BALANCED), gpuTiming: false)
        let decoded = try! JSONDecoder().decode(EngineGraphicsPreferences.self, from: try! JSONEncoder().encode(smaa))
        precondition(decoded == smaa && decoded.validated() == smaa)
        precondition(EngineSceneUpscaleChoice.off.renderSize(output: "2048x1536") == nil)
        let sizes = EngineSceneUpscaleChoice.allCases.dropFirst().map { $0.renderSize(output: "2048x1536")! }
        precondition(sizes.map(\.width) == [1536, 1376, 1024] && sizes.map(\.height) == [1152, 1032, 768])
        precondition(EngineSceneUpscaleChoice.metalfx50.renderSize(output: "1280x960")!.width == 640)
        precondition(EngineSceneUpscaleChoice.metalfx75.renderSize(output: "bad") == nil)
        precondition(EngineSceneUpscaleChoice(rawValue: "metalfx67") == .metalfx67)
        precondition(EngineGraphicsText.running(selected: "SMAA", effective: "FXAA", reason: "preparing") == "FXAA (SMAA preparing)")
        precondition(EngineGraphicsText.running(selected: "SMAA", effective: "SMAA", reason: "preparing") == "SMAA")
        precondition(EngineGraphicsText.foveation(-1) == "Unsupported on this device")
        // A flat frame after an immersive one: a selected mode is "not applied
        // to flat frames", never still running or merely "preparing".
        let flat = Int(HALO_PRESENTATION_FLAT)
        precondition(EngineGraphicsText.running(selected: "MetalFX spatial", effective: EngineGraphicsText.upscale(Int(HALO_UPSCALE_OFF)),
            reason: EngineGraphicsText.reason(presentation: flat, building: "preparing")) == "Off (MetalFX spatial not applied to flat frames)")
        precondition(EngineGraphicsText.reason(presentation: Int(HALO_PRESENTATION_PANORAMA), building: "preparing") == "preparing")
        precondition(EngineGraphicsText.reason(presentation: Int(HALO_PRESENTATION_NONE), building: "preparing") == "no world frame on screen")
        precondition(EngineGraphicsText.presentation(flat) == "Flat frame at the engine size")
        precondition(EngineGraphicsText.presentation(Int(HALO_PRESENTATION_PANORAMA_RETAINED)).hasPrefix("Last immersive world"))
        print("PASS graphics preferences: conservative defaults, stored values validated, launcher sizes shared with C, selected vs effective named, presentation-aware reasons")
    }
}
