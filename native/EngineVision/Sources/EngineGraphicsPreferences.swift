import Foundation

/// The image-quality choices that apply while the game runs: scene
/// anti-aliasing, engine-pass foveation and GPU timing. Scene upscaling is a
/// next-launch choice (`EngineSceneUpscaleChoice`) because it changes the
/// engine's back buffer. Values are the C enums in halo_settings.h.
struct EngineGraphicsPreferences: Codable, Equatable {
    var version = 1
    var sceneAA = Int(HALO_SCENE_AA_FXAA)
    var foveation = Int(HALO_FOVEATION_OFF)
    var gpuTiming = false

    func validated() -> Self {
        var value = self
        if !(Int(HALO_SCENE_AA_OFF)...Int(HALO_SCENE_AA_SMAA)).contains(sceneAA) { value.sceneAA = Int(HALO_SCENE_AA_FXAA) }
        if !(Int(HALO_FOVEATION_OFF)...Int(HALO_FOVEATION_AGGRESSIVE)).contains(foveation) { value.foveation = Int(HALO_FOVEATION_OFF) }
        return value
    }
}

/// Scene upscaling at the next launch: the world renders at a percentage of
/// the chosen resolution, MetalFX upscales it, and the HUD is rasterized at
/// the full resolution. Stored as a string under `preferenceKey`, read by
/// EngineVisionRuntime.m when the engine starts.
enum EngineSceneUpscaleChoice: String, CaseIterable {
    case off, metalfx75, metalfx67, metalfx50
    static let preferenceKey = "HaloSceneUpscale"

    var percent: UInt32 {
        switch self { case .off: 0; case .metalfx75: 75; case .metalfx67: 67; case .metalfx50: 50 }
    }
    var title: String {
        switch self {
        case .off: "Off · native"
        case .metalfx75: "MetalFX · 75 %"
        case .metalfx67: "MetalFX · 67 %"
        case .metalfx50: "MetalFX · 50 % · Lightest"
        }
    }
    /// The engine size this choice renders for an output resolution string
    /// such as "2048x1536", or nil when it would not be smaller.
    func renderSize(output: String) -> (width: UInt32, height: UInt32)? {
        let parts = output.split(separator: "x").compactMap { UInt32($0) }
        guard percent > 0, parts.count == 2 else { return nil }
        var width: UInt32 = 0, height: UInt32 = 0
        guard halo_graphics_upscale_render_size(parts[0], parts[1], percent, &width, &height) != 0 else { return nil }
        return (width, height)
    }
}

/// Words for the settings page and the report. Selected and effective modes
/// are always named separately: a mode that is selected but cannot run says
/// what runs instead.
enum EngineGraphicsText {
    static func antiAliasing(_ value: Int) -> String {
        switch value {
        case Int(HALO_SCENE_AA_OFF): "Off"
        case Int(HALO_SCENE_AA_SMAA): "SMAA"
        case Int(HALO_SCENE_AA_FXAA): "FXAA"
        default: "Not yet drawn"
        }
    }
    static func foveation(_ value: Int) -> String {
        switch value {
        case Int(HALO_FOVEATION_OFF): "Off"
        case Int(HALO_FOVEATION_BALANCED): "Balanced"
        case Int(HALO_FOVEATION_AGGRESSIVE): "Aggressive"
        case -1: "Unsupported on this device"
        default: "Not yet drawn"
        }
    }
    static func upscale(_ value: Int) -> String {
        switch value {
        case Int(HALO_UPSCALE_OFF): "Off"
        case Int(HALO_UPSCALE_METALFX): "MetalFX spatial"
        case Int(HALO_UPSCALE_BILINEAR): "Bilinear"
        default: "Not yet drawn"
        }
    }
    /// What the latest Present showed (HALO_PRESENTATION_*).
    static func presentation(_ value: Int) -> String {
        switch value {
        case Int(HALO_PRESENTATION_PANORAMA): "Immersive world"
        case Int(HALO_PRESENTATION_PANORAMA_RETAINED): "Last immersive world (newest frame dropped)"
        case Int(HALO_PRESENTATION_FLAT): "Flat frame at the engine size"
        default: "Nothing presented yet"
        }
    }
    /// Why a selected mode is not running: the build state while a world
    /// frame is on screen, otherwise that there is no world frame to apply it to.
    static func reason(presentation: Int, building: String) -> String {
        switch presentation {
        case Int(HALO_PRESENTATION_PANORAMA), Int(HALO_PRESENTATION_PANORAMA_RETAINED): building
        case Int(HALO_PRESENTATION_FLAT): "not applied to flat frames"
        default: "no world frame on screen"
        }
    }
    static func size(_ width: UInt32, _ height: UInt32) -> String {
        width > 0 && height > 0 ? "\(width) × \(height)" : "—"
    }
    /// "SMAA", or "FXAA (SMAA preparing)" when the selection is not running.
    static func running(selected: String, effective: String, reason: String) -> String {
        selected == effective || effective == "Not yet drawn" ? effective : "\(effective) (\(selected) \(reason))"
    }
    static func milliseconds(_ value: Double) -> String {
        value.isFinite ? String(format: "%.2f ms", value) : "—"
    }
}
