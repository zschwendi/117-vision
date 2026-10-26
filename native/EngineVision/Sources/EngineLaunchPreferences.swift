import Foundation

/// Launch graphics saved for the next launch, and the one-time move of an
/// existing install to the highest of them.
///
/// Build124 starts each device once at its highest launch graphics. On Apple
/// Vision Pro: 2560 × 1920 per view with the world drawn at that size (scene
/// upscaling off), Halo's 30 fps limit lifted, and Even frame cadence off
/// (its steps top out at 45 fps on a 90 Hz display). On iPad and iPhone: the
/// Native resolution at the display's maximum frame rate. Each device family
/// records the move under its own versioned marker, so a lower choice made
/// afterwards is kept. The apps run this from their `App` initializer, before
/// any settings model or launch plan reads these keys. Every other setting,
/// any field this build does not know, and all saves are left as they are.
/// Foundation only: shared by both apps and their tests.
enum EngineLaunchPreferences {
    enum Platform: String { case vision, pad }

    /// Raise to apply a future one-time change again.
    static let migrationVersion = 1
    static func markerKey(_ platform: Platform) -> String {
        platform == .vision ? "HaloVisionLaunchMaximumVersion" : "HaloPadLaunchMaximumVersion"
    }

    // Apple Vision Pro. The keys are read by EngineVisionRuntime.m at launch;
    // the upscale and presentation keys match EngineSceneUpscaleChoice and
    // EngineSettingsModel.
    static let visionResolutionKey = "HaloRenderResolution"
    static let visionResolutions = ["1280x960", "1600x1200", "1920x1440", "2048x1536", "2560x1920"]
    static let visionHighestResolution = "2560x1920"
    static let sceneUpscaleKey = "HaloSceneUpscale"
    static let sceneUpscaleOff = "off"
    static let visionFrameRateKey = "HaloVisionFrameRate"
    static let presentationKey = "MasterCheefPresentationV1"

    // iPad and iPhone (mobile_graphics.h parses these).
    static let padResolutionKey = "HaloPadRenderResolution"
    static let padFrameRateKey = "HaloPadFrameRate"
    static let padHighestResolution = "native"
    static let padHighestFrameRate = "display"

    struct Outcome: Equatable {
        enum Presentation: String { case absent, unchanged, cadenceTurnedOff, unreadable }
        /// False when this device family had already moved.
        var applied = false
        /// Vision only: what happened to the saved Even frame cadence.
        var presentation = Presentation.absent
    }

    @discardableResult
    static func migrate(_ platform: Platform, defaults: UserDefaults = .standard) -> Outcome {
        let marker = markerKey(platform)
        var outcome = Outcome()
        guard defaults.integer(forKey: marker) < migrationVersion else { return outcome }
        switch platform {
        case .vision:
            defaults.set(visionHighestResolution, forKey: visionResolutionKey)
            defaults.set(sceneUpscaleOff, forKey: sceneUpscaleKey)
            defaults.set(EngineVisionFrameRate.maximum.rawValue, forKey: visionFrameRateKey)
            outcome.presentation = turnOffEvenCadence(defaults)
        case .pad:
            defaults.set(padHighestResolution, forKey: padResolutionKey)
            defaults.set(padHighestFrameRate, forKey: padFrameRateKey)
        }
        // Recorded even when the presentation record could not be read, so
        // later choices are never overwritten by a second pass.
        defaults.set(migrationVersion, forKey: marker)
        outcome.applied = true
        return outcome
    }

    /// Clears `framePacing` in the saved presentation record and nothing else.
    /// Only a record whose version is the JSON integer 1 (not a boolean, not
    /// 1.5 or 1.0) and whose framePacing is a JSON `true` is rewritten; every other
    /// field, known or not, is carried over. A record that is not a JSON
    /// object is left byte for byte, as EngineSettingsModel would ignore it.
    static func turnOffEvenCadence(_ defaults: UserDefaults) -> Outcome.Presentation {
        guard let stored = defaults.object(forKey: presentationKey) else { return .absent }
        guard let data = stored as? Data,
              var fields = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return .unreadable }
        guard let version = fields["version"] as? NSNumber, !isBoolean(version), !CFNumberIsFloatType(version), version.intValue == 1,
              let pacing = fields["framePacing"] as? NSNumber, isBoolean(pacing), pacing.boolValue else { return .unchanged }
        fields["framePacing"] = false
        guard let rewritten = try? JSONSerialization.data(withJSONObject: fields) else { return .unreadable }
        defaults.set(rewritten, forKey: presentationKey)
        return .cadenceTurnedOff
    }

    private static func isBoolean(_ number: NSNumber) -> Bool {
        CFGetTypeID(number as CFTypeRef) == CFBooleanGetTypeID()
    }
}

/// Apple Vision Pro frame rate at the next launch (HaloVisionFrameRate).
/// Maximum asks the host to lift the 30 fps limit a player's profile saves;
/// Original limiter leaves Halo's own limiter choice as it is (a profile
/// already set unthrottled stays unthrottled). Either way the rate reached
/// depends on the scene and the headset and is measured separately.
enum EngineVisionFrameRate: String, CaseIterable {
    case maximum, original

    var title: String { self == .maximum ? "Maximum" : "Original limiter" }
}
