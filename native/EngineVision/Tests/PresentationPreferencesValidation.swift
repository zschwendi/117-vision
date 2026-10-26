import Foundation

@main
struct PresentationPreferencesValidation {
    @MainActor static func main() throws {
        let name = "MasterCheefTests." + UUID().uuidString
        let store = UserDefaults(suiteName: name)!
        defer { store.removePersistentDomain(forName: name) }
        var invalid = EnginePresentationPreferences()
        invalid.separationMillimetres = .infinity
        invalid.verticalFieldDegrees = -30
        invalid.hapticStrength = .nan
        invalid.surroundBrightness = 900
        invalid.frameRateTarget = 2
        invalid.ownWeaponDecibels = -1
        let valid = invalid.validated()
        precondition(valid.separationMillimetres == 63 && valid.verticalFieldDegrees == 90)
        precondition(valid.hapticStrength == 1 && valid.surroundBrightness == 2)
        precondition(valid.frameRateTarget == 20 && valid.ownWeaponDecibels == 0)
        let model = EngineSettingsModel(defaults: store)
        model.stereo = false
        model.separationMillimetres = 55
        model.verticalFieldDegrees = 120
        model.hapticStrength = 0.4
        let reopened = EngineSettingsModel(defaults: store)
        precondition(!reopened.stereo && reopened.separationMillimetres == 55)
        precondition(reopened.verticalFieldDegrees == 120 && reopened.hapticStrength == 0.4)
        var native = HaloSettings()
        halo_settings_get(&native)
        precondition(native.stereo_separation == 0)
        precondition(abs(native.panorama_vfov - Float(120 * Double.pi / 180)) < 0.0001)
        reopened.reset()
        let reset = EngineSettingsModel(defaults: store)
        precondition(reset.stereo && reset.separationMillimetres == 63 && reset.verticalFieldDegrees == 105)
        precondition(!reset.framePacing && !reset.layerAlign)
        let savedBeforeOverride = store.data(forKey: EngineSettingsModel.preferenceKey)
        setenv("HALO_STEREO_IPD", "80", 1)
        _ = EngineSettingsModel(defaults: store)
        precondition(store.data(forKey: EngineSettingsModel.preferenceKey) == savedBeforeOverride)
        unsetenv("HALO_STEREO_IPD")
        store.set(Data("broken".utf8), forKey: EngineSettingsModel.preferenceKey)
        let recovered = EngineSettingsModel(defaults: store)
        precondition(recovered.stereo && abs(recovered.verticalFieldDegrees - 105) < 0.001)
        print("PASS: invalid preference bounds, persistence, native live apply, reset, corrupt storage recovery")
    }
}
