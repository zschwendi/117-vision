import Foundation

/// Mobile has no Vision presentation-settings model. Apply only the portable
/// values, retaining native stereo, camera, pacing, foveation and tracking state.
/// Invoke at mobile startup/before Play and after idle-only remote application.
@MainActor
enum EngineSyncedPreferencesRuntime {
    static func applyMobile(defaults: UserDefaults = .standard) {
        let values = EngineSyncedPreferences.read(defaults, platform: .mobile)
        var settings = HaloSettings()
        halo_settings_get(&settings)
        if !overridden("HALO_HAPTICS"), case .number(let strength) = values["haptics"] {
            settings.haptics_strength = Float(strength)
        }
        if !overridden("HALO_SELF_GAIN_DB"), case .number(let gain) = values["weaponAudio"] {
            settings.self_gain_db = Float(gain)
        }
        halo_settings_set(&settings)
        var graphics = HaloGraphicsSettings()
        halo_graphics_get(&graphics)
        if !overridden("HALO_SCENE_AA", "HALO_FXAA"), case .number(let aa) = values["sceneAA"] {
            graphics.scene_aa = Int32(aa)
        }
        halo_graphics_set(&graphics)
    }
    private static func overridden(_ names: String...) -> Bool {
        names.contains { getenv($0).map { $0.pointee != 0 } ?? false }
    }
}
