import Foundation

private func expect(_ condition: @autoclosure () -> Bool, _ message: String) {
    if !condition() { fatalError(message) }
}

@MainActor
private final class TestCloud: EngineSettingsCloudTransport {
    var account = "account-one"
    var documents: [EnginePreferenceDocument] = []
    var writes: [EnginePreferenceDocument] = []
    var failRead = false
    var failWrite = false
    var duringRead: (() -> Void)?
    var duringWrite: (() -> Void)?
    func read() async throws -> EngineSettingsCloudSnapshot {
        if failRead { throw EnginePreferenceError.unavailable("Offline") }
        await Task.yield()
        duringRead?(); duringRead = nil
        return .init(account: account, documents: documents)
    }
    func write(_ document: EnginePreferenceDocument, account: String) async throws {
        expect(account == self.account, "write is scoped to the read account")
        if failWrite { throw EnginePreferenceError.unavailable("Offline") }
        await Task.yield()
        duringWrite?(); duringWrite = nil
        writes.append(document); documents.append(document)
    }
}

@main
struct SettingsSyncValidation {
    static let writerA = "00000000-0000-0000-0000-000000000001"
    static let writerB = "00000000-0000-0000-0000-000000000002"
    static func doc(_ key: String, _ value: EngineSyncedPreferences.Value, clock: UInt64 = 1, writer: String = writerA) -> EnginePreferenceDocument {
        .init(entries: [key: .init(value: value, clock: clock, writer: writer)])
    }
    @MainActor static func main() async throws {
        try policy()
        try runtime()
        try await store()
        print("PASS: settings allowlist, bootstrap, conflicts, offline edits, idle-only apply, account isolation and device families")
    }
    static func policy() throws {
        let suite = "117-settings-policy-" + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        defaults.set("native", forKey: "HaloPadRenderResolution")
        defaults.set("display", forKey: "HaloPadFrameRate")
        defaults.set(90, forKey: "MasterCheefSenseTurnSpeed.v1")
        var fresh = EnginePreferenceState(writer: writerA)
        fresh.bootstrap(EngineSyncedPreferences.read(defaults, platform: .mobile))
        expect(fresh.entries.isEmpty && fresh.pending.isEmpty, "startup defaults and migrations must never publish")
        var old = EnginePreferenceState(writer: writerA)
        old.bootstrap(["sense.turnSpeed": .number(100)])
        expect(old.entries["sense.turnSpeed"]?.clock == 0, "existing meaningful preferences have seed rank")
        try old.merge([doc("sense.turnSpeed", .number(60), clock: 9, writer: writerB)])
        expect(old.values["sense.turnSpeed"] == .number(60) && old.pending.isEmpty, "existing cloud beats a legacy seed")
        // A cloud value may arrive after a successful but empty first inventory.
        var late = EnginePreferenceState(writer: writerA)
        late.bootstrap(["sense.turnSpeed": .number(100)])
        late.acknowledge(late.pendingDocument!)
        try late.merge([doc("sense.turnSpeed", .number(70), writer: writerB)])
        expect(late.values["sense.turnSpeed"] == .number(70), "late authored cloud beats already-published seed")
        let left = doc("haptics", .number(0.5), clock: 4, writer: writerA)
        let right = doc("haptics", .number(1.5), clock: 4, writer: writerB)
        let other = doc("mobile.frameRate", .text("60"), clock: 3)
        var a = EnginePreferenceState(), b = EnginePreferenceState()
        try a.merge([left, other, right]); try b.merge([right, left, other])
        expect(a.entries == b.entries && a.values["haptics"] == .number(1.5), "concurrent same-key conflicts converge deterministically")
        expect(a.values["mobile.frameRate"] == .text("60"), "disjoint settings merge")
        try a.merge([right, other, left]); expect(a.entries == b.entries, "merge is idempotent")
        a.edit("haptics", value: .number(0.25))
        let pending = a.pendingDocument!
        try a.merge([doc("haptics", .number(2), clock: 200)])
        expect(a.values["haptics"] == .number(0.25) && a.entries["haptics"]!.clock > 200, "local unuploaded edit survives late remote read")
        a.acknowledge(pending)
        expect(a.pending.contains("haptics"), "an old upload acknowledgment must not erase a newer edit")
        let before = a.entries
        do { try a.merge([doc("IPD", .number(70))]); fatalError("unknown field accepted") } catch {}
        expect(a.entries == before, "invalid inventory cannot partially apply")
        for invalid in [doc("haptics", .number(12)), doc("mobile.resolution", .text("99999x99999")), doc("sense.turnMode", .bool(true))] {
            do { _ = try invalid.validated(); fatalError("invalid value accepted") } catch {}
        }
        let calibration: [String: Any] = ["version": 1, "separationMillimetres": 71, "verticalFieldDegrees": 110, "hapticStrength": 1, "futureSetting": "keep"]
        defaults.set(try JSONSerialization.data(withJSONObject: calibration), forKey: EngineSyncedPreferences.presentationKey)
        EngineSyncedPreferences.apply(["haptics": .number(0.5), "mobile.frameRate": .text("60"), "sense.turnSpeed": .number(100)], to: defaults, platform: .mobile)
        let data = try JSONSerialization.jsonObject(with: defaults.data(forKey: EngineSyncedPreferences.presentationKey)!) as! [String: Any]
        expect(data["separationMillimetres"] as? Int == 71 && data["futureSetting"] as? String == "keep", "field patch preserves calibration and future keys")
        expect(defaults.integer(forKey: "MasterCheefSenseTurnSpeed.v1") == 90, "mobile does not apply Sense values")
        expect(defaults.string(forKey: "HaloPadFrameRate") == "60", "mobile applies family preference")
        defaults.set(Data("broken".utf8), forKey: EngineSyncedPreferences.presentationKey)
        EngineSyncedPreferences.apply(["haptics": .number(0)], to: defaults, platform: .vision)
        expect(defaults.data(forKey: EngineSyncedPreferences.presentationKey) == Data("broken".utf8), "malformed local blob is not replaced")
    }
    @MainActor static func runtime() throws {
        let suite = "117-settings-runtime-" + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        var native = HaloSettings()
        halo_settings_get(&native)
        native.stereo_separation = 0.012
        native.panorama_vfov = 1.9
        native.backdrop_brightness = 0.4
        native.frame_pacing = 1
        native.haptics_strength = 1.2
        native.self_gain_db = 20
        halo_settings_set(&native)
        var graphics = HaloGraphicsSettings()
        halo_graphics_get(&graphics)
        graphics.engine_foveation = 2
        graphics.gpu_timing = 1
        halo_graphics_set(&graphics)
        EngineSyncedPreferences.apply(["haptics": .number(0.5), "weaponAudio": .number(12), "sceneAA": .number(2)], to: defaults, platform: .mobile)
        EngineSyncedPreferencesRuntime.applyMobile(defaults: defaults)
        halo_settings_get(&native); halo_graphics_get(&graphics)
        expect(native.haptics_strength == 0.5 && native.self_gain_db == 12 && graphics.scene_aa == 2, "mobile preferences reach actual native setters")
        expect(abs(native.stereo_separation - 0.012) < 0.0001 && native.panorama_vfov == 1.9 && native.backdrop_brightness == 0.4 && native.frame_pacing == 1, "mobile hook preserves device presentation and calibration")
        expect(graphics.engine_foveation == 2 && graphics.gpu_timing == 1, "mobile hook preserves foveation and diagnostics")
        setenv("HALO_HAPTICS", "1", 1); setenv("HALO_SELF_GAIN_DB", "20", 1); setenv("HALO_SCENE_AA", "off", 1)
        defer { unsetenv("HALO_HAPTICS"); unsetenv("HALO_SELF_GAIN_DB"); unsetenv("HALO_SCENE_AA") }
        EngineSyncedPreferences.apply(["haptics": .number(0), "weaponAudio": .number(0), "sceneAA": .number(0)], to: defaults, platform: .mobile)
        EngineSyncedPreferencesRuntime.applyMobile(defaults: defaults)
        halo_settings_get(&native); halo_graphics_get(&graphics)
        expect(native.haptics_strength == 0.5 && native.self_gain_db == 12 && graphics.scene_aa == 2, "explicit process environment retains priority over synced preferences")
    }
    @MainActor static func store() async throws {
        var suites: [String] = []
        func isolated() -> UserDefaults {
            let name = "117-settings-store-" + UUID().uuidString; suites.append(name)
            return UserDefaults(suiteName: name)!
        }
        defer { for suite in suites { UserDefaults(suiteName: suite)?.removePersistentDomain(forName: suite) } }
        let defaults = isolated(), cloud = TestCloud()
        cloud.documents = [doc("sense.turnSpeed", .number(60)), doc("mobile.frameRate", .text("60"))]
        let store = EngineSettingsSyncStore(platform: .vision, defaults: defaults, cloud: cloud)
        var idle = false, reloads = 0
        store.engineIsIdle = { idle }; store.didApply = { _ in reloads += 1 }
        await store.syncNow()
        expect(defaults.object(forKey: "MasterCheefSenseTurnSpeed.v1") == nil, "running or paused engine must not receive remote controls")
        expect(store.hasPendingApplication && cloud.writes.isEmpty, "download is pending, fresh defaults are not uploaded")
        idle = true; cloud.failRead = true
        store.applyPendingIfIdle()
        cloud.failRead = false; await store.syncNow()
        expect(defaults.double(forKey: "MasterCheefSenseTurnSpeed.v1") == 60 && reloads == 1, "idle applies remote then reloads model")
        expect(defaults.object(forKey: "HaloPadFrameRate") == nil, "Vision does not apply mobile graphics")
        idle = false; defaults.set(110.0, forKey: "MasterCheefSenseTurnSpeed.v1")
        await store.syncNow()
        expect(cloud.writes.last?.entries["sense.turnSpeed"]?.value == .number(110), "local app-setting edits may publish while playing")
        cloud.failRead = true; defaults.set(80.0, forKey: "MasterCheefSenseTurnSpeed.v1")
        await store.syncNow()
        expect(defaults.double(forKey: "MasterCheefSenseTurnSpeed.v1") == 80 && store.status.contains("Offline"), "offline edits remain local")
        cloud.failRead = false; cloud.documents.append(doc("sense.turnSpeed", .number(40), clock: 100))
        await store.syncNow()
        expect(cloud.writes.last?.entries["sense.turnSpeed"]?.value == .number(80), "pending offline choice survives reconciliation")
        cloud.duringRead = { defaults.set(95.0, forKey: "MasterCheefSenseTurnSpeed.v1") }
        cloud.documents.append(doc("sense.turnSpeed", .number(30), clock: 300))
        idle = true; cloud.failRead = true
        store.applyPendingIfIdle()
        cloud.failRead = false; await store.syncNow()
        expect(defaults.double(forKey: "MasterCheefSenseTurnSpeed.v1") == 95, "edit during cloud read survives remote application")
        cloud.duringWrite = { defaults.set(105.0, forKey: "MasterCheefSenseTurnSpeed.v1") }
        defaults.set(100.0, forKey: "MasterCheefSenseTurnSpeed.v1"); await store.syncNow(); await store.syncNow()
        expect(cloud.writes.last?.entries["sense.turnSpeed"]?.value == .number(105), "edit during upload is retried, not acknowledged away")
        let writesBeforeAccountChange = cloud.writes.count
        cloud.account = "account-two"; cloud.documents = []
        await store.syncNow()
        expect(cloud.writes.count == writesBeforeAccountChange, "account switch does not export prior account controls")
        expect(defaults.double(forKey: "MasterCheefSenseTurnSpeed.v1") == 105, "account switch leaves current device settings intact")
        let mobileDefaults = isolated(), mobileCloud = TestCloud()
        mobileCloud.documents = [doc("mobile.resolution", .text("native")), doc("mobile.frameRate", .text("60")), doc("sense.turnSpeed", .number(40))]
        let mobile = EngineSettingsSyncStore(platform: .mobile, defaults: mobileDefaults, cloud: mobileCloud)
        mobile.engineIsIdle = { true }; await mobile.syncNow()
        expect(mobileDefaults.string(forKey: "HaloPadFrameRate") == "60", "iPad and iPhone share frame rate choice")
        expect(mobileDefaults.object(forKey: "MasterCheefSenseTurnSpeed.v1") == nil, "mobile must not initialize Vision defaults")
        // Local next-launch state survives a store/process recreation.
        mobileCloud.failRead = true; mobileDefaults.set("30", forKey: "HaloPadFrameRate"); await mobile.syncNow()
        let restarted = EngineSettingsSyncStore(platform: .mobile, defaults: mobileDefaults, cloud: mobileCloud)
        restarted.engineIsIdle = { true }; mobileCloud.failRead = false; await restarted.syncNow()
        expect(mobileCloud.writes.last?.entries["mobile.frameRate"]?.value == .text("30"), "offline queue persists across restart")
        let writeDefaults = isolated(), writeCloud = TestCloud()
        let failedWrite = EngineSettingsSyncStore(platform: .mobile, defaults: writeDefaults, cloud: writeCloud)
        failedWrite.engineIsIdle = { true }
        writeCloud.failWrite = true; writeDefaults.set("60", forKey: "HaloPadFrameRate")
        await failedWrite.syncNow()
        expect(writeCloud.writes.isEmpty && writeDefaults.string(forKey: "HaloPadFrameRate") == "60", "failed upload keeps the local choice and queue")
        writeCloud.failWrite = false; await failedWrite.syncNow()
        expect(writeCloud.writes.last?.entries["mobile.frameRate"]?.value == .text("60"), "failed upload is retried")
        // Simulate termination before the defaults notification could capture a
        // write: the persisted observation differs from the actual preference.
        let gapDefaults = isolated(), gapCloud = TestCloud()
        gapCloud.documents = [doc("mobile.frameRate", .text("60"), clock: 10)]
        var beforeCrash: EngineSettingsSyncStore? = EngineSettingsSyncStore(platform: .mobile, defaults: gapDefaults, cloud: gapCloud)
        beforeCrash!.engineIsIdle = { true }; await beforeCrash!.syncNow()
        beforeCrash = nil
        gapDefaults.set("30", forKey: "HaloPadFrameRate")
        let afterCrash = EngineSettingsSyncStore(platform: .mobile, defaults: gapDefaults, cloud: gapCloud)
        afterCrash.engineIsIdle = { true }; await afterCrash.syncNow()
        expect(gapDefaults.string(forKey: "HaloPadFrameRate") == "30" && gapCloud.writes.last?.entries["mobile.frameRate"]?.value == .text("30"), "unobserved local write survives termination and older cloud")
        let deferredDefaults = isolated(), deferredCloud = TestCloud()
        deferredCloud.documents = [doc("mobile.frameRate", .text("60"), clock: 10)]
        var playing: EngineSettingsSyncStore? = EngineSettingsSyncStore(platform: .mobile, defaults: deferredDefaults, cloud: deferredCloud)
        playing!.engineIsIdle = { false }; await playing!.syncNow(); playing = nil
        let nextLaunch = EngineSettingsSyncStore(platform: .mobile, defaults: deferredDefaults, cloud: deferredCloud)
        nextLaunch.engineIsIdle = { true }; nextLaunch.applyPendingIfIdle()
        await nextLaunch.syncNow()
        expect(deferredDefaults.string(forKey: "HaloPadFrameRate") == "60" && deferredCloud.writes.isEmpty, "deferred remote is applied after restart without becoming a local edit")
        // Defaults notification may arrive after didApply: it must never echo
        // imported settings back into a newer cloud revision.
        let count = mobileCloud.writes.count
        await Task.yield(); await restarted.syncNow()
        expect(mobileCloud.writes.count == count, "remote apply does not produce sync echo")
    }
}
