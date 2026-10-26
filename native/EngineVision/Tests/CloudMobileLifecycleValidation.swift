import Foundation
import Combine

private final class LifecycleBox<Value>: @unchecked Sendable {
    private let lock = NSLock()
    private var value: Value
    init(_ value: Value) { self.value = value }
    func get() -> Value { lock.lock(); defer { lock.unlock() }; return value }
    func set(_ value: Value) { lock.lock(); self.value = value; lock.unlock() }
}

@MainActor
private final class RecordingWatcher: EngineSaveCloudWatching {
    let changed: @MainActor () -> Void
    var started: [URL] = []
    var stops = 0
    let succeeds: Bool
    init(_ changed: @escaping @MainActor () -> Void, succeeds: Bool = true) { self.changed = changed; self.succeeds = succeeds }
    func start(folder: URL) -> Bool { started.append(folder); return succeeds }
    func stop() { stops += 1 }
}

/// Synthetic monitor lifecycle, inventory alias/confinement and first-run
/// policy checks. Temporary folders only; no iCloud, device or real saves.
@main struct CloudMobileLifecycleValidation {
    static var checks = 0
    static func require(_ value: Bool, _ message: String) {
        checks += 1
        precondition(value, message)
    }
    static func write(_ value: String, at url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(value.utf8).write(to: url, options: .atomic)
    }
    static func read(_ url: URL) -> String? { (try? Data(contentsOf: url)).map { String(decoding: $0, as: UTF8.self) } }
    static func save(_ root: URL) -> URL { root.appendingPathComponent(EngineSaveArchive.mutablePath + "/saved/player_profiles/Chief/savegame.bin") }
    static func archives(_ folder: URL) throws -> [EngineSaveArchive] {
        guard FileManager.default.fileExists(atPath: folder.path) else { return [] }
        return try FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension == EngineSaveArchive.fileExtension }.map(EngineSaveArchive.read)
    }
    @MainActor static func wait(_ seconds: Double = 5, until condition: () -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(seconds)
        while !condition() {
            guard Date() < deadline else { return false }
            try? await Task.sleep(nanoseconds: 20_000_000)
        }
        return true
    }
    @MainActor static func settle() async { try? await Task.sleep(nanoseconds: 300_000_000) }

    @MainActor static func main() async throws {
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appendingPathComponent("MasterCheef-Lifecycle-Synthetic-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: work) }
        var suites: [String] = []
        defer { for name in suites { UserDefaults.standard.removePersistentDomain(forName: name) } }
        func defaults() -> UserDefaults {
            let name = "MasterCheef.LifecycleValidation.\(UUID().uuidString)"
            suites.append(name)
            return UserDefaults(suiteName: name)!
        }

        // MARK: Monitor ownership: unavailable -> available -> account change.
        let accountA = work.appendingPathComponent("account-A"), accountB = work.appendingPathComponent("account-B")
        for folder in [accountA, accountB] { try fm.createDirectory(at: folder, withIntermediateDirectories: true) }
        let location = LifecycleBox<EngineCloudSaveLocation?>(nil)
        var watchers: [RecordingWatcher] = []
        var failNextStarts = 0
        let monitored = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("monitor-recovery"),
            requiresVisionSeed: false, sourcePlatform: "iPad", cloudLocationProvider: { location.get() },
            cloudChangeDebounce: 0.05,
            cloudWatcherFactory: { _, changed in
                let watcher = RecordingWatcher(changed, succeeds: failNextStarts == 0)
                failNextStarts = max(0, failNextStarts - 1)
                watchers.append(watcher)
                return watcher
            })
        var deliveries = 0
        let subscription = monitored.cloudChanges.sink { deliveries += 1 }
        defer { subscription.cancel() }

        monitored.startCloudMonitoring()
        await settle()
        require(watchers.isEmpty && monitored.monitoredFolder == nil, "Unavailable iCloud owns no watcher")
        location.set(.init(folder: accountA, account: "A"))
        failNextStarts = 1
        monitored.startCloudMonitoring()
        require(await wait { watchers.count == 1 }, "Fixture attempts one watcher")
        await settle()
        require(watchers.count == 1 && watchers[0].stops >= 1 && monitored.monitoredFolder == nil,
                "A failed watcher start owns nothing and does not spin retrying")
        watchers.removeAll()
        monitored.startCloudMonitoring()   // next foreground
        require(await wait { watchers.count == 1 }, "A later start retries after a failed start")
        require(watchers[0].started == [accountA] && monitored.monitoredFolder == accountA, "Watcher observes the resolved account root")
        monitored.startCloudMonitoring(); monitored.startCloudMonitoring()
        await settle()
        require(watchers.count == 1, "Repeated starts do not create duplicate watchers")
        watchers[0].changed()
        require(await wait { deliveries == 1 }, "Watcher change signals one reconciliation")

        location.set(.init(folder: accountB, account: "B"))
        monitored.cloudIdentityDidChange()
        require(await wait { watchers.count == 2 }, "Identity change starts a fresh watcher")
        require(watchers[0].stops >= 1, "Previous account watcher is stopped")
        require(watchers[1].started == [accountB] && monitored.monitoredFolder == accountB,
                "Identity change re-resolves the current account, not the previous root")
        require(await wait { deliveries == 2 }, "Identity change reconciles once")
        watchers[1].changed()   // the new watcher's first gather
        require(await wait { deliveries == 3 }, "First gather after an account change is delivered")
        await settle()
        require(deliveries == 3 && watchers.count == 2, "No self-sustaining restart or gather loop")

        location.set(nil)
        monitored.cloudIdentityDidChange()
        await settle()
        require(watchers.count == 2 && watchers[1].stops >= 1 && monitored.monitoredFolder == nil,
                "Signing out leaves no stale watcher")
        location.set(.init(folder: accountA, account: "A"))
        monitored.cloudIdentityDidChange()
        require(await wait { watchers.count == 3 && watchers[2].started == [accountA] }, "Signing back in retries monitoring")

        // A slow previous-account resolution never starts after a newer change.
        location.set(.init(folder: accountB, account: "B"))
        monitored.cloudIdentityDidChange()
        monitored.cloudIdentityDidChange()
        await settle()
        require(watchers.count == 4 && watchers[3].started == [accountB], "Superseded resolutions collapse to one current watcher")

        monitored.stopCloudMonitoring()
        let deliveriesAtStop = deliveries
        require(watchers[3].stops >= 1 && monitored.monitoredFolder == nil, "Starting Halo stops the watcher")
        monitored.cloudIdentityDidChange(); monitored.startCloudMonitoring(); monitored.noteCloudChange()
        await settle()
        require(watchers.count == 4 && deliveries == deliveriesAtStop, "After Halo starts, nothing restarts or signals")

        // MARK: Inventory alias and confinement.
        let cloud = work.appendingPathComponent("alias-cloud"), outside = work.appendingPathComponent("outside")
        try fm.createDirectory(at: cloud, withIntermediateDirectories: true)
        try fm.createDirectory(at: outside, withIntermediateDirectories: true)
        let reported = LifecycleBox<[URL]>([])
        let root = work.appendingPathComponent("alias-device")
        try write("device-progress-A", at: save(root))
        let aliasStore = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("alias-recovery"),
            requiresVisionSeed: false, sourcePlatform: "iPad",
            cloudLocationProvider: { .init(folder: cloud, account: "alias") },
            metadataDiscovery: { _ in reported.get() })
        aliasStore.engineActivityCheck = { false }
        let firstPublish = await aliasStore.prepareForPlay(gameRoot: root, engineActive: false)
        let published = try archives(cloud)
        require(firstPublish && published.count == 1, "Fixture publishes one snapshot")
        let name = published[0].filename
        let physical = try EngineCloudSaveStore.physicalDirectory(cloud)
        let link = work.appendingPathComponent("cloud-link")
        try fm.createSymbolicLink(at: link, withDestinationURL: cloud)
        var aliases = [link.appendingPathComponent(name), cloud.appendingPathComponent(name)]
        if physical != cloud.path { aliases.append(URL(fileURLWithPath: physical).appendingPathComponent(name)) }
        reported.set(aliases)
        await aliasStore.sync(gameRoot: root, engineActive: false)
        require(!aliasStore.pendingSync && !aliasStore.hasConflicts && aliasStore.snapshots.count == 1,
                "Metadata aliases of a listed snapshot are one complete item, not duplicates or pending")
        require(aliasStore.status.contains("match"), "Aliased inventory reconciles normally")

        // A real archive outside the folder, by direct path, is never adopted.
        try write("outside-progress", at: save(work.appendingPathComponent("outside-device")))
        let foreign = try EngineSaveArchive.capture(gameRoot: work.appendingPathComponent("outside-device"), device: "Vision Pro", engineActive: false)
        let foreignURL = try foreign.writeNew(in: outside)
        let escapeLink = work.appendingPathComponent("escape-link")
        try fm.createSymbolicLink(at: escapeLink, withDestinationURL: outside)
        let cloudCount = try archives(cloud).count
        for (label, url) in [("escaped parent", foreignURL),
                             ("symlinked parent outside", escapeLink.appendingPathComponent(foreign.filename)),
                             ("dot-dot spelling", cloud.appendingPathComponent("../outside/" + foreign.filename))] {
            reported.set([url])
            await aliasStore.sync(gameRoot: root, engineActive: false)
            require(aliasStore.pendingSync && !aliasStore.snapshots.contains { $0.id == foreign.id }, "\(label) is held, not adopted")
            let countNow = try archives(cloud).count
            require(read(save(root)) == "device-progress-A" && countNow == cloudCount, "\(label) changes nothing")
        }
        reported.set([])
        let fileLink = cloud.appendingPathComponent(foreign.filename)
        try fm.createSymbolicLink(at: fileLink, withDestinationURL: foreignURL)
        await aliasStore.sync(gameRoot: root, engineActive: false)
        require(aliasStore.pendingSync && !aliasStore.snapshots.contains { $0.id == foreign.id } && read(save(root)) == "device-progress-A",
                "A snapshot-file symlink inside the folder is rejected, not followed")
        try fm.removeItem(at: fileLink)
        await aliasStore.sync(gameRoot: root, engineActive: false)
        require(!aliasStore.pendingSync, "Removing the anomaly restores a complete inventory")

        // MARK: A folder URL made before mkdir has no directory flag. Listed
        // children report a slash-terminated parent; the same physical folder
        // must still yield a complete inventory (migration rehearsal regression).
        let plainCloud = URL(fileURLWithPath: work.appendingPathComponent("plain-cloud").path, isDirectory: false)
        require(!plainCloud.hasDirectoryPath, "Fixture folder URL has no directory flag")
        try fm.createDirectory(at: plainCloud, withIntermediateDirectories: true)
        let plainVision = work.appendingPathComponent("plain-vision")
        try write("vision-current-campaign", at: save(plainVision))
        try write("vision-current-profile", at: plainVision.appendingPathComponent(EngineSaveArchive.mutablePath + "/saved/player_profiles/Chief/blam.sav"))
        let plainSeed = try EngineSaveArchive.capture(gameRoot: plainVision, device: "Vision Pro", engineActive: false)
        let plainMigration = work.appendingPathComponent("plain-migration/vision-pro-save.mc-save")
        try fm.createDirectory(at: plainMigration.deletingLastPathComponent(), withIntermediateDirectories: true)
        try plainSeed.encoded().write(to: plainMigration)
        let plainRoot = work.appendingPathComponent("plain-ipad")
        try write("bundled-default", at: save(plainRoot))
        let plainStore = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("plain-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: plainMigration,
            cloudLocationProvider: { .init(folder: plainCloud, account: "plain") })
        plainStore.engineActivityCheck = { false }
        let plainSeeded = await plainStore.prepareForPlay(gameRoot: plainRoot, engineActive: false)
        let plainAfterSeed = try archives(plainCloud)
        require(plainSeeded && plainStore.migrationReady && read(save(plainRoot)) == "vision-current-campaign",
                "Seed restores every byte through a non-directory folder URL")
        require(!plainStore.pendingSync && !plainStore.status.contains("still downloading"),
                "Seed inventory is complete, not held as downloading/invalid: \(plainStore.status)")
        require(plainAfterSeed.count == 1 && plainAfterSeed[0].id == plainSeed.id && plainAfterSeed[0].isVisionProSource,
                "Exact Vision seed is published once")
        try write("ipad-continued-campaign", at: save(plainRoot))
        let plainContinued = await plainStore.prepareForPlay(gameRoot: plainRoot, engineActive: false)
        let plainHeads = try archives(plainCloud).filter { candidate in
            !(try archives(plainCloud)).contains { ($0.parentSnapshotIDs ?? []).contains(candidate.id) }
        }
        require(plainContinued && !plainStore.hasConflicts && plainHeads.count == 1
                && plainHeads[0].parentSnapshotIDs == [plainSeed.id] && plainHeads[0].parentFingerprint == plainSeed.fingerprint,
                "Changed local progress publishes as the seed's immutable descendant: \(plainStore.status)")

        // MARK: Public first run (no Vision seed) versus explicit migration gate.
        let publicCloud = work.appendingPathComponent("public-cloud")
        try fm.createDirectory(at: publicCloud, withIntermediateDirectories: true)
        let publicRoot = work.appendingPathComponent("public-phone")
        try fm.createDirectory(at: publicRoot, withIntermediateDirectories: true)
        let publicDefaults = defaults()
        func publicStore() -> EngineCloudSaveStore {
            let store = EngineCloudSaveStore(defaults: publicDefaults, recoveryFolder: work.appendingPathComponent("public-recovery"),
                requiresVisionSeed: false, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("public-no-seed"),
                cloudLocationProvider: { .init(folder: publicCloud, account: "public") }, deviceLabel: "iPhone")
            store.engineActivityCheck = { false }
            return store
        }
        let fresh = publicStore()
        require(fresh.migrationReady, "Generic build does not wait for a Vision save")
        let freshPlay = await fresh.prepareForPlay(gameRoot: publicRoot, engineActive: false)
        let publicBefore = try archives(publicCloud)
        require(freshPlay && fresh.canPlay && publicBefore.isEmpty,
                "Brand-new player with no saves anywhere can play; nothing is invented or published")
        try write("first-public-campaign", at: save(publicRoot))
        let reopened = publicStore()   // close and reopen after the first session
        let reopenedPlay = await reopened.prepareForPlay(gameRoot: publicRoot, engineActive: false)
        let publicArchives = try archives(publicCloud)
        require(reopenedPlay && publicArchives.count == 1 && publicArchives[0].sourcePlatform == "iPad"
                && publicArchives[0].device == "iPhone" && publicArchives[0].parentSnapshotIDs == [],
                "First public progress is preserved and published as a root after reopening")
        try fm.removeItem(at: publicRoot.appendingPathComponent(EngineSaveArchive.mutablePath))
        let missingAfterPlay = publicStore()
        let missingPlay = await missingAfterPlay.prepareForPlay(gameRoot: publicRoot, engineActive: false)
        require(!missingPlay && missingAfterPlay.hasConflicts && !fm.fileExists(atPath: save(publicRoot).path),
                "Generic build keeps the missing-local safeguard after prior play")

        let gatedRoot = work.appendingPathComponent("gated-ipad")
        try write("bundled-default", at: save(gatedRoot))
        let gated = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("gated-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("gated-no-seed"),
            cloudLocationProvider: { .init(folder: publicCloud, account: "public") })
        gated.engineActivityCheck = { false }
        let gatedPlay = await gated.prepareForPlay(gameRoot: gatedRoot, engineActive: false)
        require(!gatedPlay && !gated.migrationReady && read(save(gatedRoot)) == "bundled-default",
                "Explicit migration build still refuses a non-Vision cloud root and leaves local data alone")
        let cloudAfterGate = try archives(publicCloud)
        require(cloudAfterGate.count == 1, "Explicit migration build publishes nothing before a Vision seed")

        print("PASS: \(checks) synthetic monitor-lifecycle, inventory-alias/confinement and first-run policy checks")
    }
}
