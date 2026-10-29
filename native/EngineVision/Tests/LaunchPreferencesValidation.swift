import Foundation

// The one-time move to the highest launch graphics (EngineLaunchPreferences),
// on a throwaway defaults suite that is removed afterwards; the user's own
// defaults are never opened. Also checks, from the sources, that fallback
// defaults agree with the highest choice and that both apps migrate before
// anything reads these keys.
//
// swiftc -parse-as-library native/EngineVision/Sources/EngineLaunchPreferences.swift \
//   native/EngineVision/Sources/EnginePresentationPreferences.swift \
//   native/EngineVision/Tests/LaunchPreferencesValidation.swift -o launch-preferences
// ./launch-preferences <repository root>

@main
struct LaunchPreferencesValidation {
    typealias P = EngineLaunchPreferences

    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : ".")
        // An absolute suite name is a plist at that path, so the suite lives in
        // the temporary directory, never in ~/Library/Preferences.
        let suite = (NSTemporaryDirectory() as NSString).appendingPathComponent("halo-launch-preferences-\(UUID().uuidString)")
        guard let defaults = UserDefaults(suiteName: suite) else { fatalError("could not open a test suite") }
        defer {
            defaults.removePersistentDomain(forName: suite)
            CFPreferencesAppSynchronize(suite as CFString)
            try? FileManager.default.removeItem(atPath: suite + ".plist")
        }
        var checks = 0
        func reset() { defaults.removePersistentDomain(forName: suite) }
        // Only this suite's own keys: no global or registration domain.
        func snapshot() -> [String: Any] { defaults.persistentDomain(forName: suite) ?? [:] }
        func check(_ condition: @autoclosure () -> Bool, _ what: String, line: Int = #line) {
            guard condition() else { fatalError("FAIL line \(line): \(what)") }
            checks += 1
        }

        // New install: the highest values are written once, and the marker.
        reset()
        var outcome = P.migrate(.vision, defaults: defaults)
        check(outcome == .init(applied: true, presentation: .absent), "vision new install applies once")
        check(defaults.string(forKey: P.visionResolutionKey) == "2560x1920", "vision resolution highest")
        check(defaults.string(forKey: P.sceneUpscaleKey) == "off", "scene upscaling off")
        check(defaults.string(forKey: P.visionFrameRateKey) == "maximum", "frame rate maximum")
        check(defaults.object(forKey: P.presentationKey) == nil, "no presentation record is created")
        check(defaults.integer(forKey: P.markerKey(.vision)) == P.migrationVersion, "vision marker")
        check(defaults.object(forKey: P.padResolutionKey) == nil && defaults.object(forKey: P.markerKey(.pad)) == nil,
              "vision migration leaves the iPad keys alone")
        check(!P.migrate(.vision, defaults: defaults).applied, "second launch does nothing")

        // Existing install with lower choices, Even cadence on, unknown
        // fields, and unrelated settings that must survive untouched.
        reset()
        let saved = EnginePresentationPreferences(stereo: false, separationMillimetres: 63.123456789012345,
            verticalFieldDegrees: 1.0 / 3.0 + 120, hapticStrength: 0.7, surroundBrightness: 1.9, spatialMenu: false,
            frameRateTarget: 24, ownWeaponDecibels: 7.25, gazePointer: true, layerAlign: true, framePacing: true)
        var record = try JSONSerialization.jsonObject(with: JSONEncoder().encode(saved)) as! [String: Any]
        record["futureField"] = ["nested": [1, 2.5, "three", true] as [Any], "flag": false] as [String: Any]
        record["futureNumber"] = 42
        defaults.set(try JSONSerialization.data(withJSONObject: record), forKey: P.presentationKey)
        let unrelated: [String: Any] = [
            "HaloTextureModsEnabled": false, "HaloShaderModsEnabled": true,
            "MasterCheefGraphicsV1": Data("{\"version\":1,\"sceneAA\":2,\"foveation\":1,\"gpuTiming\":true}".utf8),
            "HaloTrackingAreasAttempted": true, "HaloSenseTurnStyle": "smooth", "HaloPreferredPresentation": "window",
            "HaloCloudSavesEnabled": true, "HaloAudioRoute": ["speaker", 3] as [Any],
            P.padResolutionKey: "1280x960", P.padFrameRateKey: "30"]
        for (key, value) in unrelated { defaults.set(value, forKey: key) }
        defaults.set("1280x960", forKey: P.visionResolutionKey)
        defaults.set("metalfx50", forKey: P.sceneUpscaleKey)
        defaults.set("original", forKey: P.visionFrameRateKey)
        let before = snapshot()
        check(before.count == unrelated.count + 4, "snapshot holds exactly this suite's keys")
        outcome = P.migrate(.vision, defaults: defaults)
        check(outcome == .init(applied: true, presentation: .cadenceTurnedOff), "existing install migrates, cadence off")
        check(defaults.string(forKey: P.visionResolutionKey) == "2560x1920", "lower resolution raised once")
        check(defaults.string(forKey: P.sceneUpscaleKey) == "off", "upscaling turned off once")
        check(defaults.string(forKey: P.visionFrameRateKey) == "maximum", "Original limiter replaced once")
        let migrated = defaults.data(forKey: P.presentationKey)!
        var expected = saved; expected.framePacing = false
        let decoded = try JSONDecoder().decode(EnginePresentationPreferences.self, from: migrated)
        check(decoded == expected, "the settings model reads every presentation value unchanged except Even cadence")
        let fields = try JSONSerialization.jsonObject(with: migrated) as! NSDictionary
        var expectedFields = record; expectedFields["framePacing"] = false
        check(fields.isEqual(to: expectedFields), "unknown and known presentation fields carried over exactly")
        let after = snapshot()
        for (key, value) in unrelated {
            check((after[key] as AnyObject?)?.isEqual(value) == true, "unrelated key \(key) unchanged")
        }
        let changed: Set<String> = [P.visionResolutionKey, P.sceneUpscaleKey, P.visionFrameRateKey, P.presentationKey, P.markerKey(.vision)]
        check(Set(after.keys) == Set(unrelated.keys).union(changed), "only the migrated keys and the marker differ")

        // Once only: the player's later lower choices persist.
        defaults.set("1600x1200", forKey: P.visionResolutionKey)
        defaults.set("metalfx75", forKey: P.sceneUpscaleKey)
        defaults.set("original", forKey: P.visionFrameRateKey)
        let chosen = try JSONEncoder().encode(saved)   /* Even cadence chosen again */
        defaults.set(chosen, forKey: P.presentationKey)
        for _ in 0..<3 { check(!P.migrate(.vision, defaults: defaults).applied, "no repeat") }
        check(defaults.string(forKey: P.visionResolutionKey) == "1600x1200", "later resolution kept")
        check(defaults.string(forKey: P.sceneUpscaleKey) == "metalfx75", "later upscaling kept")
        check(defaults.string(forKey: P.visionFrameRateKey) == "original", "later Original limiter kept")
        check(defaults.data(forKey: P.presentationKey) == chosen, "later Even cadence kept byte for byte")
        // A future marker version is never rolled back; a newer marker skips this one.
        defaults.set(P.migrationVersion + 1, forKey: P.markerKey(.vision))
        check(!P.migrate(.vision, defaults: defaults).applied, "newer marker skips")

        // iPad and iPhone: Native at the display maximum once, then kept.
        reset()
        check(P.migrate(.pad, defaults: defaults) == .init(applied: true, presentation: .absent), "pad new install")
        check(defaults.string(forKey: P.padResolutionKey) == "native" && defaults.string(forKey: P.padFrameRateKey) == "display",
              "pad new install highest")
        check(defaults.object(forKey: P.visionResolutionKey) == nil && defaults.object(forKey: P.markerKey(.vision)) == nil,
              "pad migration leaves the Vision keys alone")
        reset()
        defaults.set("1600x1200", forKey: P.padResolutionKey); defaults.set("30", forKey: P.padFrameRateKey)
        defaults.set(try JSONEncoder().encode(saved), forKey: P.presentationKey)
        defaults.set("1280x960", forKey: P.visionResolutionKey)
        let padBefore = snapshot()
        check(P.migrate(.pad, defaults: defaults).applied, "pad existing install migrates")
        check(defaults.string(forKey: P.padResolutionKey) == "native" && defaults.string(forKey: P.padFrameRateKey) == "display",
              "pad 1600 x 1200 / 30 fps raised once")
        check(defaults.data(forKey: P.presentationKey) == padBefore[P.presentationKey] as? Data,
              "pad never touches the presentation record")
        check(defaults.string(forKey: P.visionResolutionKey) == "1280x960", "pad never touches Vision keys")
        check(Set(snapshot().keys) == Set(padBefore.keys).union([P.markerKey(.pad)]), "pad adds only its marker")
        defaults.set("1280x960", forKey: P.padResolutionKey); defaults.set("60", forKey: P.padFrameRateKey)
        check(!P.migrate(.pad, defaults: defaults).applied, "pad once only")
        check(defaults.string(forKey: P.padResolutionKey) == "1280x960" && defaults.string(forKey: P.padFrameRateKey) == "60",
              "pad later choices kept")
        // The two markers are independent.
        check(P.migrate(.vision, defaults: defaults).applied, "vision still migrates after pad in the same domain")
        check(defaults.string(forKey: P.padResolutionKey) == "1280x960", "vision leaves the pad choice")

        // Presentation records that cannot or must not be rewritten.
        let unreadable: [(String, Any)] = [
            ("not JSON", Data("{\"version\":1,\"framePacing\":tru".utf8)),
            ("a JSON array", Data("[1,true]".utf8)),
            ("empty data", Data()),
            ("a string, not data", "{\"version\":1,\"framePacing\":true}"),
            ("a number", 7)]
        for (name, value) in unreadable {
            reset()
            defaults.set(value, forKey: P.presentationKey)
            defaults.set("HaloKeepMe", forKey: "HaloUnrelated")
            let stored = defaults.object(forKey: P.presentationKey) as AnyObject
            let result = P.migrate(.vision, defaults: defaults)
            check(result == .init(applied: true, presentation: .unreadable), "\(name): reported unreadable")
            check((defaults.object(forKey: P.presentationKey) as AnyObject).isEqual(stored), "\(name): left as stored")
            check(defaults.string(forKey: "HaloUnrelated") == "HaloKeepMe", "\(name): nothing else reset")
            check(defaults.string(forKey: P.visionResolutionKey) == "2560x1920", "\(name): graphics still migrate")
            check(!P.migrate(.vision, defaults: defaults).applied, "\(name): marker still recorded")
        }
        let kept: [(String, String)] = [
            ("Even cadence already off", "{\"version\":1,\"framePacing\":false,\"stereo\":true}"),
            ("no framePacing field", "{\"version\":1,\"stereo\":false}"),
            ("framePacing as a number", "{\"version\":1,\"framePacing\":1}"),
            ("framePacing as a string", "{\"version\":1,\"framePacing\":\"true\"}"),
            ("a future record version", "{\"version\":2,\"framePacing\":true}"),
            ("a fractional version", "{\"version\":1.5,\"framePacing\":true,\"stereo\":false}"),
            ("version written as 1.0", "{\"version\":1.0,\"framePacing\":true}"),
            ("version as a string", "{\"version\":\"1\",\"framePacing\":true}"),
            ("version as a boolean", "{\"version\":true,\"framePacing\":true}"),
            ("no version", "{\"framePacing\":true}")]
        for (name, json) in kept {
            reset()
            let bytes = Data(json.utf8)
            defaults.set(bytes, forKey: P.presentationKey)
            check(P.migrate(.vision, defaults: defaults).presentation == .unchanged, "\(name): unchanged")
            check(defaults.data(forKey: P.presentationKey) == bytes, "\(name): byte for byte")
        }

        // Fallback defaults agree with the highest choice, and the constants
        // match the keys the rest of the app reads.
        check(P.visionResolutions.last == P.visionHighestResolution, "highest is the picker's last size")
        check(EnginePresentationPreferences().framePacing == false, "Even cadence is off by default")
        check(EngineVisionFrameRate(rawValue: "maximum") == .maximum && EngineVisionFrameRate.allCases.first == .maximum,
              "Maximum is the first frame-rate choice")
        func source(_ path: String) -> String {
            guard let text = try? String(contentsOf: root.appendingPathComponent(path), encoding: .utf8) else {
                fatalError("missing source \(path); pass the repository root")
            }
            return text
        }
        let vision = "native/EngineVision/Sources/", pad = "native/EnginePad/Sources/"
        let runtime = source(vision + "EngineVisionRuntime.m")
        for needle in ["unsigned output_w = 2560, output_h = 1920;", "chosen : \"2560,1920,60\"",
                       "stringForKey:@\"\(P.visionResolutionKey)\"", "stringForKey:@\"\(P.sceneUpscaleKey)\"",
                       "stringForKey:@\"\(P.visionFrameRateKey)\"", "isEqualToString:@\"\(EngineVisionFrameRate.original.rawValue)\"",
                       "#define MOBILE_RESOLUTION_KEY @\"\(P.padResolutionKey)\"", "#define MOBILE_RATE_KEY @\"\(P.padFrameRateKey)\""] {
            check(runtime.contains(needle), "EngineVisionRuntime.m: \(needle)")
        }
        check(!runtime.contains("\"2048,1536,60\"") && !runtime.contains("output_w = 2048"), "no 2048 x 1536 launch fallback left")
        let settingsView = source(vision + "EngineSettingsView.swift")
        check(settingsView.contains("@AppStorage(EngineLaunchPreferences.visionResolutionKey) private var resolution = EngineLaunchPreferences.visionHighestResolution"),
              "settings resolution default")
        check(settingsView.contains("Text(\"2560 × 1920 · Heavier\").tag(\"\(P.visionHighestResolution)\")"), "picker's highest tag")
        check(source(vision + "EngineGraphicsQualityView.swift").contains("@AppStorage(EngineLaunchPreferences.visionResolutionKey) private var resolution = EngineLaunchPreferences.visionHighestResolution"),
              "image quality resolution default")
        check(source(vision + "EngineDiagnostics.swift").contains("?? EngineLaunchPreferences.visionHighestResolution"), "report fallback")
        check(source(vision + "EngineSettingsModel.swift").contains("static let preferenceKey = \"\(P.presentationKey)\""), "presentation key")
        let graphics = source(vision + "EngineGraphicsPreferences.swift")
        check(graphics.contains("static let preferenceKey = \"\(P.sceneUpscaleKey)\"") && graphics.contains("case off, metalfx75"),
              "upscale key and its off value")
        check(source(pad + "EnginePadSettingsView.swift").contains("@AppStorage(EngineLaunchPreferences.padResolutionKey) private var resolution = EngineLaunchPreferences.padHighestResolution"),
              "pad resolution default")
        check(source("tools/build_engine_ipad.py").contains("\"EngineLaunchPreferences.swift\""), "iPad build includes the helper")

        // Startup order: each App initializer migrates before SwiftUI creates
        // the state objects whose models read these keys; nothing else builds them.
        func initializerFirst(_ text: String, app: String, platform: String, state: [String]) {
            guard let start = text.range(of: "struct \(app): App {"),
                  let body = text.range(of: "var body: some Scene", range: start.upperBound..<text.endIndex),
                  let initializer = text.range(of: "init() {", range: start.upperBound..<body.lowerBound),
                  let call = text.range(of: "EngineLaunchPreferences.migrate(.\(platform))", range: initializer.upperBound..<body.lowerBound)
            else { fatalError("FAIL: \(app) must migrate in init() before body") }
            _ = call
            for object in state {
                check(text.components(separatedBy: "\(object)()").count == 2, "\(app): \(object) built only by its @StateObject")
                check(text.contains("@StateObject private var") && text.contains("= \(object)()"), "\(app): \(object) is lazy state")
            }
            checks += 1
        }
        let visionApp = source(vision + "HaloEngineVisionApp.swift")
        initializerFirst(visionApp, app: "HaloEngineVisionApp", platform: "vision", state: ["EngineAppSession"])
        check(visionApp.contains("let settings = EngineSettingsModel()"), "the presentation model belongs to the session")
        initializerFirst(source(pad + "MasterCheefPadApp.swift"), app: "MasterCheefPadApp", platform: "pad",
                         state: ["EngineAssetStore", "EnginePadModel", "EngineCloudSaveStore"])
        // After migration, the settings model's own read sees Even cadence off.
        reset()
        defaults.set(try JSONEncoder().encode(saved), forKey: P.presentationKey)
        _ = P.migrate(.vision, defaults: defaults)
        let read = try JSONDecoder().decode(EnginePresentationPreferences.self, from: defaults.data(forKey: P.presentationKey)!)
        check(read.version == 1 && !read.framePacing && read.validated().frameRateTarget == 24, "model read after migration")

        reset()
        check(snapshot().isEmpty, "test suite emptied")
        print("PASS launch preferences: \(checks) checks — new installs, lower saved values raised once, later choices kept, "
              + "Vision and iPad markers independent, unrelated and unknown fields preserved, unreadable presentation left as stored, "
              + "fallback defaults at the highest choice, migration before any settings read (suite \(suite) removed)")
    }
}
