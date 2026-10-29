import Foundation
import CoreFoundation

/// The allowlist is deliberately independent of UserDefaults' complete domain.
/// Saves already carry Halo's profiles/bindings. Device geometry, IPD, tracking,
/// bookmarks, credentials, migration flags and artwork paths never enter this file.
enum EngineSyncedPreferences {
    enum Platform: String, Codable, Sendable { case vision, mobile }
    enum Value: Codable, Equatable, Sendable {
        case bool(Bool), number(Double), text(String)
        var object: Any {
            switch self { case .bool(let v): v; case .number(let v): v; case .text(let v): v }
        }
    }
    enum Rule: Sendable {
        case bool, number(ClosedRange<Double>), numbers(Set<Double>), texts(Set<String>)
        func accepts(_ value: Value) -> Bool {
            switch (self, value) {
            case (.bool, .bool): true
            case (.number(let range), .number(let v)): v.isFinite && range.contains(v)
            case (.numbers(let allowed), .number(let v)): v.isFinite && allowed.contains(v)
            case (.texts(let allowed), .text(let v)): allowed.contains(v)
            default: false
            }
        }
    }
    struct Field: Sendable {
        let id: String
        let key: String
        var member: String? = nil
        var platform: Platform? = nil
        let initial: Value
        let rule: Rule
        func available(on device: Platform) -> Bool { platform == nil || platform == device }
    }
    static let presentationKey = "MasterCheefPresentationV1"
    static let graphicsKey = "MasterCheefGraphicsV1"
    static let fields: [Field] = [
        .init(id: "haptics", key: presentationKey, member: "hapticStrength", initial: .number(1), rule: .number(0...2)),
        .init(id: "weaponAudio", key: presentationKey, member: "ownWeaponDecibels", initial: .number(18), rule: .number(0...24)),
        .init(id: "sceneAA", key: graphicsKey, member: "sceneAA", initial: .number(1), rule: .numbers([0, 1, 2])),
        .init(id: "textureMods", key: "HaloTextureModsEnabled", initial: .bool(true), rule: .bool),
        .init(id: "shaderMods", key: "HaloShaderModsEnabled", initial: .bool(true), rule: .bool),
        .init(id: "mobile.resolution", key: "HaloPadRenderResolution", platform: .mobile, initial: .text("native"),
              rule: .texts(["native", "1280x960", "1600x1200", "1920x1440", "2048x1536"])),
        .init(id: "mobile.frameRate", key: "HaloPadFrameRate", platform: .mobile, initial: .text("display"), rule: .texts(["display", "60", "30"])),
        .init(id: "sense.turnMode", key: "MasterCheefSenseTurnMode.v2", platform: .vision, initial: .number(2), rule: .numbers([0, 1, 2, 30, 45])),
        .init(id: "sense.turnSpeed", key: "MasterCheefSenseTurnSpeed.v1", platform: .vision, initial: .number(90), rule: .number(30...120)),
        .init(id: "sense.turnBoundary", key: "MasterCheefSenseTurnBoundary.v1", platform: .vision, initial: .number(0.72), rule: .number(0.5...0.9)),
        .init(id: "sense.weaponMode", key: "MasterCheefSenseWeaponMode.v2", platform: .vision, initial: .text("single"), rule: .texts(["single", "dual"]))
    ] + [
        ("trackedAim", "MasterCheefSenseTrackedAim.v1", true),
        ("realHands", "MasterCheefSenseRealHandsVisible.v1", true),
        ("gestures", "MasterCheefSenseQuickGestures.v1", true),
        ("fullBody", "MasterCheefSenseFullBody.v1", false),
        ("gripContinuity", "MasterCheefSenseGripContinuity.v1", true),
        ("scopeSmoothing", "MasterCheefSenseScopedSmoothing.v1", false),
        ("weaponHaptics", "MasterCheefSenseWeaponHaptics.v1", true),
        ("physicalArms", "MasterCheefSensePhysicalArms.v1", false),
        ("physicalMagazine", "MasterCheefSensePhysicalMagazine.v1", false),
        ("reachPickup", "MasterCheefSenseReachPickup.v1", false),
        ("physicalCharge", "MasterCheefSensePhysicalCharge.v1", false)
    ].map { Field(id: "sense." + $0.0, key: $0.1, platform: .vision, initial: .bool($0.2), rule: .bool) }

    static func field(_ id: String) -> Field? { fields.first { $0.id == id } }
    static func valid(_ id: String, _ value: Value) -> Bool { field(id)?.rule.accepts(value) ?? false }
    static func value(_ object: Any?) -> Value? {
        if let v = object as? String { return .text(v) }
        guard let v = object as? NSNumber else { return nil }
        if CFGetTypeID(v) == CFBooleanGetTypeID() { return .bool(v.boolValue) }
        return v.doubleValue.isFinite ? .number(v.doubleValue) : nil
    }
    static func read(_ defaults: UserDefaults, platform: Platform) -> [String: Value] {
        Dictionary(uniqueKeysWithValues: fields.filter { $0.available(on: platform) }.map { field in
            let raw: Any?
            if let member = field.member {
                raw = json(defaults, field.key)?[member]
            } else { raw = defaults.object(forKey: field.key) }
            let v = value(raw)
            return (field.id, v.flatMap { field.rule.accepts($0) ? $0 : nil } ?? field.initial)
        })
    }
    private static func json(_ defaults: UserDefaults, _ key: String) -> [String: Any]? {
        guard let data = defaults.data(forKey: key),
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let version = object["version"] as? NSNumber,
              CFGetTypeID(version) != CFBooleanGetTypeID(), version.doubleValue == 1 else { return nil }
        return object
    }
    /// Patch individual fields; keep the receiving device's calibration and any
    /// fields introduced by a newer version. A malformed local blob is preserved.
    @discardableResult
    static func apply(_ values: [String: Value], to defaults: UserDefaults, platform: Platform) -> Set<String> {
        var changed: Set<String> = []
        let before = read(defaults, platform: platform)
        for field in fields where field.available(on: platform) {
            guard let value = values[field.id], field.rule.accepts(value), before[field.id] != value else { continue }
            if let member = field.member {
                var object: [String: Any]
                if defaults.object(forKey: field.key) != nil {
                    guard let existing = json(defaults, field.key) else { continue }
                    object = existing
                } else {
                    object = field.key == presentationKey ? presentationDefaults : graphicsDefaults
                }
                object[member] = value.object
                guard let data = try? JSONSerialization.data(withJSONObject: object, options: .sortedKeys) else { continue }
                defaults.set(data, forKey: field.key)
            } else { defaults.set(value.object, forKey: field.key) }
            changed.insert(field.id)
        }
        return changed
    }
    private static var presentationDefaults: [String: Any] {
        ["version": 1, "stereo": true, "separationMillimetres": 63.0, "verticalFieldDegrees": 105.0,
         "hapticStrength": 1.0, "surroundBrightness": 1.0, "spatialMenu": true, "frameRateTarget": 30.0,
         "ownWeaponDecibels": 18.0, "gazePointer": false, "layerAlign": false, "framePacing": false]
    }
    private static var graphicsDefaults: [String: Any] { ["version": 1, "sceneAA": 1, "foveation": 0, "gpuTiming": false] }
}

/// Logical clocks avoid device wall-clock skew. Rank zero is an imported legacy
/// choice, not a newly authored edit: even a late cloud edit outranks it.
struct EnginePreferenceRevision: Codable, Equatable, Sendable {
    var value: EngineSyncedPreferences.Value
    var clock: UInt64
    var writer: String
    func supersedes(_ other: Self) -> Bool {
        if clock != other.clock { return clock > other.clock }
        if writer != other.writer { return writer > other.writer }
        // A writer must not reuse a revision. Deterministic tie handling also
        // makes a corrupt duplicate converge instead of oscillating by read order.
        let encoder = JSONEncoder(); encoder.outputFormatting = .sortedKeys
        return (try? encoder.encode(value).lexicographicallyPrecedes(encoder.encode(other.value))) == false && value != other.value
    }
}

struct EnginePreferenceDocument: Codable, Equatable, Sendable {
    static let fileExtension = "117settings"
    var version = 1
    var id = UUID().uuidString
    var entries: [String: EnginePreferenceRevision]
    func validated() throws -> Self {
        guard version == 1, UUID(uuidString: id) != nil, entries.count <= EngineSyncedPreferences.fields.count else {
            throw EnginePreferenceError.invalidDocument
        }
        for (key, revision) in entries {
            guard EngineSyncedPreferences.valid(key, revision.value), UUID(uuidString: revision.writer) != nil,
                  revision.clock < 1_000_000_000_000 else { throw EnginePreferenceError.invalidDocument }
        }
        return self
    }
}

enum EnginePreferenceError: Error, LocalizedError {
    case invalidDocument, unavailable(String), accountChanged
    var errorDescription: String? {
        switch self {
        case .invalidDocument: "A settings document could not be read safely. Local settings are unchanged."
        case .unavailable(let text): text
        case .accountChanged: "The iCloud account changed. Settings will sync again shortly."
        }
    }
}

/// Pure merge policy, shared by the app and deterministic multi-device tests.
struct EnginePreferenceState: Codable, Sendable {
    var writer = UUID().uuidString
    var account: String?
    /// Last actual local defaults, distinct from remote entries deferred during
    /// play. Detects writes followed by termination before a defaults notification.
    var observed: [String: EngineSyncedPreferences.Value]?
    var entries: [String: EnginePreferenceRevision] = [:]
    var pending: Set<String> = []
    var clock: UInt64 = 0

    mutating func bootstrap(_ values: [String: EngineSyncedPreferences.Value]) {
        guard entries.isEmpty else { return }
        for (key, value) in values where EngineSyncedPreferences.valid(key, value) && value != EngineSyncedPreferences.field(key)?.initial {
            entries[key] = .init(value: value, clock: 0, writer: writer)
            pending.insert(key)
        }
    }
    mutating func edit(_ key: String, value: EngineSyncedPreferences.Value) {
        guard EngineSyncedPreferences.valid(key, value), entries[key]?.value != value else { return }
        clock += 1
        entries[key] = .init(value: value, clock: clock, writer: writer)
        pending.insert(key)
    }
    mutating func merge(_ documents: [EnginePreferenceDocument]) throws {
        let validated = try documents.map { try $0.validated() }
        var remote: [String: EnginePreferenceRevision] = [:]
        for document in validated {
            for (key, revision) in document.entries {
                clock = max(clock, revision.clock)
                if remote[key].map({ revision.supersedes($0) }) ?? true { remote[key] = revision }
            }
        }
        for (key, revision) in remote {
            guard let local = entries[key] else { entries[key] = revision; continue }
            // An unpublished intentional change stays the user's latest choice,
            // including an edit made while this read was suspended. Legacy seeds
            // never get this privilege over an established cloud entry.
            if pending.contains(key), local.clock > 0, local.writer == writer {
                if revision.supersedes(local) { clock += 1; entries[key]?.clock = clock }
            } else if revision.supersedes(local) || (pending.contains(key) && local.clock == 0 && revision.writer != writer) {
                entries[key] = revision; pending.remove(key)
            }
        }
    }
    var pendingDocument: EnginePreferenceDocument? {
        let changes = entries.filter { pending.contains($0.key) }
        return changes.isEmpty ? nil : EnginePreferenceDocument(entries: changes)
    }
    mutating func acknowledge(_ document: EnginePreferenceDocument) {
        for (key, revision) in document.entries where entries[key] == revision { pending.remove(key) }
    }
    var values: [String: EngineSyncedPreferences.Value] { entries.mapValues(\.value) }
}
