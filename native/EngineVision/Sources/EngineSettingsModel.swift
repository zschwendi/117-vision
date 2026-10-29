import SwiftUI
import Foundation

/// Live controls for the port's presentation features.
///
/// These are the port's own settings rather than Halo's: the original game
/// has no notion of a second eye, of how much sky its camera should cover, or
/// of a controller that can be felt. Every value starts from its environment
/// variable, so scripted runs behave as before, and every change takes effect
/// on the next frame without restarting the engine.
@MainActor
final class EngineSettingsModel: ObservableObject {
    /// Halo's world unit is ten feet, so millimetres convert through 3.048.
    private static let metresPerWorldUnit = 3.048

    @Published var stereo: Bool { didSet { push() } }
    @Published var separationMillimetres: Double { didSet { push() } }
    @Published var verticalFieldDegrees: Double { didSet { push() } }
    @Published var hapticStrength: Double { didSet { push() } }
    @Published var surroundBrightness: Double { didSet { push() } }
    @Published var spatialMenu: Bool { didSet { push() } }
    @Published var frameRateTarget: Double { didSet { push() } }
    @Published var ownWeaponDecibels: Double { didSet { push() } }
    @Published var gazePointer: Bool { didSet { push() } }
    @Published var layerAlign: Bool { didSet { push() } }
    @Published var framePacing: Bool { didSet { push() } }

    private let defaults: UserDefaults
    private var restoring = false
    static let preferenceKey = "MasterCheefPresentationV1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        var current = HaloSettings()
        halo_settings_get(&current)
        let separation = Double(current.stereo_separation)
        stereo = separation > 0
        separationMillimetres = separation > 0
            ? separation * 2 * Self.metresPerWorldUnit * 1000 : 63
        verticalFieldDegrees = Double(current.panorama_vfov) * 180 / .pi
        hapticStrength = Double(current.haptics_strength)
        surroundBrightness = Double(current.backdrop_brightness)
        spatialMenu = current.spatial_shell != 0
        frameRateTarget = Double(current.panorama_target_fps)
        ownWeaponDecibels = Double(current.self_gain_db)
        gazePointer = current.gaze_pointer != 0
        layerAlign = current.layer_align != 0
        framePacing = current.frame_pacing != 0
        reloadFromDefaults()
    }

    /// Called by settings sync only while the native runtime is IDLE.
    func reloadFromDefaults() {
        restoring = true
        if let data = defaults.data(forKey: Self.preferenceKey),
           let saved = try? JSONDecoder().decode(EnginePresentationPreferences.self, from: data), saved.version == 1 {
            let valid = saved.validated()
            if !Self.overridden("HALO_STEREO", "HALO_STEREO_IPD", "HALO_STEREO_SEPARATION") { stereo = valid.stereo; separationMillimetres = valid.separationMillimetres }
            if !Self.overridden("HALO_PANORAMA_VFOV") { verticalFieldDegrees = valid.verticalFieldDegrees }
            if !Self.overridden("HALO_HAPTICS") { hapticStrength = valid.hapticStrength }
            if !Self.overridden("HALO_BACKDROP") { surroundBrightness = valid.surroundBrightness }
            if !Self.overridden("HALO_SPATIAL_MENU") { spatialMenu = valid.spatialMenu }
            if !Self.overridden("HALO_PANORAMA_TARGET_FPS") { frameRateTarget = valid.frameRateTarget }
            if !Self.overridden("HALO_SELF_GAIN_DB") { ownWeaponDecibels = valid.ownWeaponDecibels }
            if !Self.overridden("HALO_GAZE_POINTER") { gazePointer = valid.gazePointer }
            if !Self.overridden("HALO_LAYER_ALIGN") { layerAlign = valid.layerAlign }
            if !Self.overridden("HALO_FRAME_PACING") { framePacing = valid.framePacing }
        }
        restoring = false
        push(persist: false)
    }

    private static func overridden(_ names: String...) -> Bool {
        names.contains { name in
            guard let value = getenv(name) else { return false }
            return value.pointee != 0
        }
    }

    func reset() {
        restoring = true
        stereo = true; separationMillimetres = 63; verticalFieldDegrees = 105
        hapticStrength = 1; surroundBrightness = 1; spatialMenu = true
        frameRateTarget = 30; ownWeaponDecibels = 18; gazePointer = false
        layerAlign = false; framePacing = false
        restoring = false
        push()
    }

    private func push(persist: Bool = true) {
        guard !restoring else { return }

        var settings = HaloSettings()
        settings.stereo_separation = stereo
            ? Float(separationMillimetres / 1000 / 2 / Self.metresPerWorldUnit) : 0
        settings.panorama_vfov = Float(verticalFieldDegrees * .pi / 180)
        settings.haptics_strength = Float(hapticStrength)
        settings.backdrop_brightness = Float(surroundBrightness)
        settings.spatial_shell = spatialMenu ? 1 : 0
        settings.panorama_target_fps = Float(frameRateTarget)
        settings.self_gain_db = Float(ownWeaponDecibels)
        settings.gaze_pointer = gazePointer ? 1 : 0
        settings.layer_align = layerAlign ? 1 : 0
        settings.frame_pacing = framePacing ? 1 : 0
        halo_settings_set(&settings)
        guard persist else { return }
        let saved = EnginePresentationPreferences(stereo: stereo, separationMillimetres: separationMillimetres,
            verticalFieldDegrees: verticalFieldDegrees, hapticStrength: hapticStrength,
            surroundBrightness: surroundBrightness, spatialMenu: spatialMenu, frameRateTarget: frameRateTarget,
            ownWeaponDecibels: ownWeaponDecibels, gazePointer: gazePointer, layerAlign: layerAlign, framePacing: framePacing)
        if let data = try? JSONEncoder().encode(saved.validated()) { defaults.set(data, forKey: Self.preferenceKey) }
    }
}

