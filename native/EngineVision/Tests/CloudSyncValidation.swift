import Foundation

private final class CloudLocationBox: @unchecked Sendable {
    private let lock = NSLock()
    private var value: EngineCloudSaveLocation?
    init(_ value: EngineCloudSaveLocation?) { self.value = value }
    func get() -> EngineCloudSaveLocation? { lock.lock(); defer { lock.unlock() }; return value }
    func set(_ value: EngineCloudSaveLocation?) { lock.lock(); self.value = value; lock.unlock() }
}

@main struct CloudSyncValidation {
    static var checks = 0
    static func require(_ value: Bool, _ message: String) {
        checks += 1
        precondition(value, message)
    }
    static func write(_ value: String, at url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(value.utf8).write(to: url, options: .atomic)
    }
    static func read(_ url: URL) throws -> String { String(decoding: try Data(contentsOf: url), as: UTF8.self) }
    static func save(_ root: URL) -> URL { root.appendingPathComponent(EngineSaveArchive.mutablePath + "/saved/player_profiles/Chief/savegame.bin") }
    static func archives(_ folder: URL) throws -> [EngineSaveArchive] {
        guard FileManager.default.fileExists(atPath: folder.path) else { return [] }
        return try FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension == EngineSaveArchive.fileExtension }.map(EngineSaveArchive.read)
    }
    static func heads(_ archives: [EngineSaveArchive]) -> [EngineSaveArchive] {
        let parents = Set(archives.flatMap { $0.parentSnapshotIDs ?? [] })
        return archives.filter { !parents.contains($0.id) }
    }
    @MainActor static func main() async throws {
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appendingPathComponent("MasterCheef-Sync-Synthetic-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: work) }
        let cloud = work.appendingPathComponent("cloud"), root = work.appendingPathComponent("ipad")
        let recovery = work.appendingPathComponent("recovery")
        let migration = work.appendingPathComponent("Documents/MasterCheefMigration/vision-pro-save.mc-save")
        let suite = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let box = CloudLocationBox(.init(folder: cloud, account: "account-A"))
        let store = EngineCloudSaveStore(defaults: defaults, recoveryFolder: recovery,
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: migration,
            cloudLocationProvider: { box.get() })
        store.engineActivityCheck = { false }
        try write("bundled-default", at: save(root))
        let readyBeforeSeed = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(!readyBeforeSeed && !store.migrationReady, "Fresh iPad waits for actual Vision source")
        require(try archives(cloud).isEmpty, "Fresh default data never gets published")
        require(try archives(recovery).contains { $0.entries.contains { String(decoding: $0.data, as: UTF8.self) == "bundled-default" } }, "Default local tree is preserved without becoming cloud authority")

        let otherRoot = work.appendingPathComponent("test-game")
        try write("untrusted-default", at: save(otherRoot))
        let badSeed = try EngineSaveArchive.capture(gameRoot: otherRoot, device: "Synthetic Vision", engineActive: false)
        try fm.createDirectory(at: migration.deletingLastPathComponent(), withIntermediateDirectories: true)
        try badSeed.encoded().write(to: migration)
        let rejectedSeed = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(!rejectedSeed && !store.migrationReady, "A test label cannot establish Vision origin")
        require(try read(save(root)) == "bundled-default", "Rejected seed leaves device saves unchanged")

        let vision = work.appendingPathComponent("vision")
        try write("vision-campaign-A", at: save(vision))
        let seed = try EngineSaveArchive.capture(gameRoot: vision, device: "Vision Pro", engineActive: false)
        try seed.encoded().write(to: migration, options: .atomic)
        let originalBytes = try Data(contentsOf: migration)
        box.set(nil)
        let offlineSeeded = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(offlineSeeded && store.migrationReady && store.pendingSync, "Verified local migration works before iCloud is available")
        require(try read(save(root)) == "vision-campaign-A", "Actual Vision bytes replace bundled data")
        require(try Data(contentsOf: migration) == originalBytes, "Original device export is retained byte for byte")
        require(try archives(recovery).contains { $0.id == seed.id && $0.isVisionProSource }, "Independent exact Vision source recovery remains")
        try write("ipad-offline-progress", at: save(root))
        let offlineProgress = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(offlineProgress && store.pendingSync, "Seeded device can keep playing offline")
        require(try read(save(root)) == "ipad-offline-progress", "Old migration file is never replayed over progress")
        require(try archives(recovery).contains { $0.entries.contains { String(decoding: $0.data, as: UTF8.self) == "ipad-offline-progress" } }, "Offline progress gets recovery before Play")

        box.set(.init(folder: cloud, account: "account-A"))
        let reconnected = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(reconnected && !store.hasConflicts, "Offline descendant reconciles with original seed")
        let firstCloud = try archives(cloud)
        require(firstCloud.contains { $0.id == seed.id && $0.isVisionProSource }, "Exact Vision origin is published, not just an iPad copy")
        let firstHead = heads(firstCloud).first!
        require(heads(firstCloud).count == 1 && firstHead.parentSnapshotIDs == [seed.id], "Revision carries immutable seed ancestry")
        require(firstHead.parentFingerprint == seed.fingerprint && firstHead.sourcePlatform == "iPad", "Parent checksum and publishing platform are explicit")

        // A second fresh iPad accepts only a descendant with a preserved Vision ancestor.
        let suite2 = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let defaults2 = UserDefaults(suiteName: suite2)!
        defer { defaults2.removePersistentDomain(forName: suite2) }
        let root2 = work.appendingPathComponent("ipad2")
        try write("new-ipad-default", at: save(root2))
        let store2 = EngineCloudSaveStore(defaults: defaults2, recoveryFolder: work.appendingPathComponent("recovery2"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-seed"),
            cloudLocationProvider: { box.get() })
        store2.engineActivityCheck = { false }
        let secondReady = await store2.prepareForPlay(gameRoot: root2, engineActive: false)
        require(secondReady && store2.migrationReady, "Fresh device can verify a descendant's retained Vision source")
        require(try read(save(root2)) == "ipad-offline-progress", "Latest unambiguous descendant is restored")

        // Independent post-seed branches cannot be ordered by wall clock.
        try write("vision-branch-B", at: save(vision))
        let remote = try EngineSaveArchive.capture(gameRoot: vision, device: "Vision Pro", engineActive: false)
            .revision(platform: "Vision Pro", parents: [firstHead], lineage: seed.id)
        _ = try remote.writeNew(in: cloud)
        try write("ipad-independent-C", at: save(root))
        let conflicted = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(!conflicted && store.hasConflicts, "Independent local and remote modifications require selection")
        require(try read(save(root)) == "ipad-independent-C", "Conflict never overwrites independent local progress")
        require(try archives(cloud).contains { $0.id == remote.id }, "Conflict leaves cloud progress intact")
        let localC = try archives(recovery).first { $0.entries.contains { String(decoding: $0.data, as: UTF8.self) == "ipad-independent-C" } }!
        require(localC.fingerprint != remote.fingerprint, "Independent local progress has its own recovery")
        box.set(nil)
        let reopenedDuringConflict = EngineCloudSaveStore(defaults: defaults, recoveryFolder: recovery,
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: migration,
            cloudLocationProvider: { box.get() })
        reopenedDuringConflict.engineActivityCheck = { false }
        let conflictOffline = await reopenedDuringConflict.prepareForPlay(gameRoot: root, engineActive: false)
        require(!conflictOffline && reopenedDuringConflict.hasConflicts, "Known unresolved conflict survives app restart and offline mode")
        box.set(.init(folder: cloud, account: "account-A"))
        let remoteSummary = store.snapshots.first { $0.id == remote.id }!
        await store.restore(snapshot: remoteSummary, gameRoot: root, engineActive: false)
        require(!store.hasConflicts && tryRead(save(root)) == "vision-branch-B", "Explicit choice restores selected branch")
        let resolved = heads(try archives(cloud))
        require(resolved.count == 1 && resolved[0].parentSnapshotIDs?.contains(remote.id) == true, "Explicit decision produces an immutable descendant")
        require(try archives(recovery).contains { $0.fingerprint == localC.fingerprint }, "Losing local branch remains recoverable")
        let followed = await store2.prepareForPlay(gameRoot: root2, engineActive: false)
        require(followed && tryRead(save(root2)) == "vision-branch-B", "Unchanged second device follows proven descendant")

        // Real simultaneous cloud heads remain explicit conflicts.
        let base = resolved[0]
        try write("fork-one", at: save(vision))
        let fork1 = try EngineSaveArchive.capture(gameRoot: vision, device: "Vision Pro", engineActive: false)
            .revision(platform: "Vision Pro", parents: [base], lineage: seed.id)
        _ = try fork1.writeNew(in: cloud)
        try write("fork-two", at: save(otherRoot))
        let fork2 = try EngineSaveArchive.capture(gameRoot: otherRoot, device: "iPad", engineActive: false)
            .revision(platform: "iPad", parents: [base], lineage: seed.id)
        _ = try fork2.writeNew(in: cloud)
        let concurrent = await store.prepareForPlay(gameRoot: root, engineActive: false)
        let concurrentHeadCount = heads(try archives(cloud)).count
        require(!concurrent && store.hasConflicts && concurrentHeadCount == 2, "Concurrent heads never get timestamp-ranked")
        await store.restore(snapshot: store.snapshots.first { $0.id == fork1.id }!, gameRoot: root, engineActive: false)
        let joined = heads(try archives(cloud))
        require(joined.count == 1 && Set(joined[0].parentSnapshotIDs ?? []) == Set([fork1.id, fork2.id]), "Explicit resolution consumes both observed heads")
        require(try archives(cloud).contains { $0.id == fork2.id }, "Unselected cloud branch stays immutable")

        // Current runtime authority overrides a stale caller Boolean.
        store.engineActivityCheck = { true }
        let before = try Data(contentsOf: save(root))
        let refused = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(!refused && tryData(save(root)) == before, "Live runtime gate refuses synchronization")
        store.engineActivityCheck = { false }

        // An incomplete provider revision cannot be mistaken for absent progress.
        let invalid = cloud.appendingPathComponent("\(UUID().uuidString.lowercased()).mc-save")
        try Data([0, 1, 2]).write(to: invalid)
        let countBefore = try fm.contentsOfDirectory(atPath: cloud.path).count
        try write("new-local-with-placeholder", at: save(root))
        await store.sync(gameRoot: root, engineActive: false)
        let countAfter = try fm.contentsOfDirectory(atPath: cloud.path).count
        require(store.pendingSync && countAfter == countBefore, "Incomplete inventory does not publish a false authoritative branch")
        require(try read(save(root)) == "new-local-with-placeholder", "Incomplete provider keeps local progress intact")
        try fm.removeItem(at: invalid)

        // Account change invalidates baseline; it does not rank unrelated saves.
        let cloudB = work.appendingPathComponent("cloud-B")
        try fm.createDirectory(at: cloudB, withIntermediateDirectories: true)
        try write("unrelated-account-progress", at: save(vision))
        let accountB = try EngineSaveArchive.capture(gameRoot: vision, device: "Vision Pro", engineActive: false)
        _ = try accountB.writeNew(in: cloudB)
        box.set(.init(folder: cloudB, account: "account-B"))
        let changedAccount = await store.prepareForPlay(gameRoot: root, engineActive: false)
        require(!changedAccount && store.hasConflicts, "New account invalidates old baseline instead of overwriting")
        require(try read(save(root)) == "new-local-with-placeholder", "Account change preserves local authority")
        require(try archives(cloudB).contains { $0.id == accountB.id }, "New account's existing save remains intact")
        require(try Data(contentsOf: migration) == originalBytes, "Migration export remains unchanged after all sync operations")

        // Loss of a previously seeded local tree must not become a new game
        // just because a cloud decision returns early.
        let missingDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let missingDefaults = UserDefaults(suiteName: missingDefaultsName)!
        defer { missingDefaults.removePersistentDomain(forName: missingDefaultsName) }
        let missingRoot = work.appendingPathComponent("missing-local")
        let missingRecovery = work.appendingPathComponent("missing-recovery")
        let missingMigration = work.appendingPathComponent("missing-migration.mc-save")
        let missingCloud = work.appendingPathComponent("missing-cloud")
        try write("new-default", at: save(missingRoot))
        try seed.encoded().write(to: missingMigration)
        let missingBox = CloudLocationBox(nil)
        let missingStore = EngineCloudSaveStore(defaults: missingDefaults, recoveryFolder: missingRecovery,
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: missingMigration,
            cloudLocationProvider: { missingBox.get() })
        missingStore.engineActivityCheck = { false }
        let missingSeeded = await missingStore.prepareForPlay(gameRoot: missingRoot, engineActive: false)
        require(missingSeeded, "Missing-local fixture first captures real Vision progress")
        try fm.removeItem(at: missingRoot.appendingPathComponent(EngineSaveArchive.mutablePath))
        let missingOffline = await missingStore.prepareForPlay(gameRoot: missingRoot, engineActive: false)
        require(!missingOffline && !missingStore.canPlay, "Seeded missing tree blocks Play offline")
        missingStore.automaticSyncEnabled = false
        let missingDisabled = await missingStore.prepareForPlay(gameRoot: missingRoot, engineActive: false)
        require(!missingDisabled && !missingStore.canPlay, "Seeded missing tree blocks Play with sync disabled")
        missingStore.automaticSyncEnabled = true
        try fm.createDirectory(at: missingCloud, withIntermediateDirectories: true)
        try Data([0, 1]).write(to: missingCloud.appendingPathComponent(UUID().uuidString.lowercased() + ".mc-save"))
        missingBox.set(.init(folder: missingCloud, account: "missing-account"))
        let missingIncomplete = await missingStore.prepareForPlay(gameRoot: missingRoot, engineActive: false)
        require(!missingIncomplete && !missingStore.canPlay, "Seeded missing tree blocks Play with incomplete cloud inventory")
        require(!fm.fileExists(atPath: save(missingRoot).path), "None of the missing-tree paths creates default progress")
        await missingStore.restore(snapshot: missingStore.recoverySnapshots.first { $0.id == seed.id }!,
                                   gameRoot: missingRoot, engineActive: false)
        require(missingStore.canPlay && tryRead(save(missingRoot)) == "vision-campaign-A", "Explicit verified recovery clears the missing-tree Play gate")

        // A first seed selected from LOCAL RECOVERY must be published verbatim,
        // then parent the new revision, before another fresh iPad can follow it.
        let recoveryDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let recoveryDefaults = UserDefaults(suiteName: recoveryDefaultsName)!
        defer { recoveryDefaults.removePersistentDomain(forName: recoveryDefaultsName) }
        let recoveryRoot = work.appendingPathComponent("local-seed-ipad")
        let recoveryOnly = work.appendingPathComponent("local-seed-recovery")
        let recoveryCloud = work.appendingPathComponent("local-seed-cloud")
        try write("bundled-default", at: save(recoveryRoot))
        try fm.createDirectory(at: recoveryOnly, withIntermediateDirectories: true)
        _ = try seed.writeNew(in: recoveryOnly)
        let recoveryBox = CloudLocationBox(.init(folder: recoveryCloud, account: "recovery-account"))
        let recoveryStore = EngineCloudSaveStore(defaults: recoveryDefaults, recoveryFolder: recoveryOnly,
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-local-seed-file"),
            cloudLocationProvider: { recoveryBox.get() })
        recoveryStore.engineActivityCheck = { false }
        await recoveryStore.refresh()
        await recoveryStore.restore(snapshot: recoveryStore.recoverySnapshots.first { $0.id == seed.id }!,
                                    gameRoot: recoveryRoot, engineActive: false)
        require(recoveryStore.migrationReady && recoveryStore.canPlay, "Local-recovery Vision seed completes first-time setup")
        let recoveryCloudArchives = try archives(recoveryCloud)
        require(recoveryCloudArchives.contains { $0.id == seed.id && $0.isVisionProSource }, "Local-recovery migration publishes original Vision archive separately")
        let recoveryHeads = heads(recoveryCloudArchives)
        require(recoveryHeads.count == 1 && recoveryHeads[0].parentSnapshotIDs == [seed.id], "Local-recovery migration revision is parented by selected seed")
        require(recoveryHeads[0].parentFingerprint == seed.fingerprint, "Local-recovery parent checksum matches original Vision data")
        let nextDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let nextDefaults = UserDefaults(suiteName: nextDefaultsName)!
        defer { nextDefaults.removePersistentDomain(forName: nextDefaultsName) }
        let nextRoot = work.appendingPathComponent("local-seed-next-ipad")
        try write("another-default", at: save(nextRoot))
        let nextStore = EngineCloudSaveStore(defaults: nextDefaults, recoveryFolder: work.appendingPathComponent("next-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-next-seed"),
            cloudLocationProvider: { recoveryBox.get() })
        nextStore.engineActivityCheck = { false }
        let nextReady = await nextStore.prepareForPlay(gameRoot: nextRoot, engineActive: false)
        require(nextReady && tryRead(save(nextRoot)) == "vision-campaign-A", "Fresh second iPad traces local-recovery migration to the preserved Vision origin")
        let incompleteLineage = try EngineSaveArchive.capture(gameRoot: vision, device: "Vision Pro", engineActive: false)
            .revision(platform: "Vision Pro", parents: [badSeed], lineage: seed.id)
        _ = try incompleteLineage.writeNew(in: recoveryOnly)
        await recoveryStore.refresh()
        let beforeIncompleteLineage = try archives(recoveryCloud).count
        await recoveryStore.restore(snapshot: recoveryStore.recoverySnapshots.first { $0.id == incompleteLineage.id }!,
                                    gameRoot: recoveryRoot, engineActive: false)
        let afterIncompleteLineage = try archives(recoveryCloud)
        require(recoveryStore.pendingSync && afterIncompleteLineage.count == beforeIncompleteLineage,
                "A local recovery with missing ancestry is held pending instead of published as a new root")
        require(!afterIncompleteLineage.contains { $0.id == incompleteLineage.id },
                "Incomplete ancestry never leaves an orphan revision in cloud")

        let fallbackDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let fallbackDefaults = UserDefaults(suiteName: fallbackDefaultsName)!
        defer { fallbackDefaults.removePersistentDomain(forName: fallbackDefaultsName) }
        let fallbackFolder = work.appendingPathComponent("manual-files")
        try fm.createDirectory(at: fallbackFolder, withIntermediateDirectories: true)
        let fallbackStore = EngineCloudSaveStore(defaults: fallbackDefaults,
            recoveryFolder: work.appendingPathComponent("fallback-recovery"),
            requiresVisionSeed: false, sourcePlatform: "Mac")
        fallbackStore.engineActivityCheck = { false }
        await fallbackStore.connect(folder: fallbackFolder)
        let fallbackReady = await fallbackStore.prepareForPlay(gameRoot: nextRoot, engineActive: false)
        let fallbackArchives = try archives(fallbackFolder.appendingPathComponent("Master Cheef Saves"))
        require(fallbackReady && fallbackStore.pendingSync && fallbackArchives.isEmpty,
                "A non-ubiquitous Files listing is not automatically treated as complete cloud inventory")
        await fallbackStore.backup(gameRoot: nextRoot, engineActive: false)
        require(try archives(fallbackFolder.appendingPathComponent("Master Cheef Saves")).count == 1,
                "Unconfirmed Files providers still support explicit immutable backup")

        let manualSeedDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let manualSeedDefaults = UserDefaults(suiteName: manualSeedDefaultsName)!
        defer { manualSeedDefaults.removePersistentDomain(forName: manualSeedDefaultsName) }
        let manualSeedFolder = work.appendingPathComponent("manual-seed-files")
        let manualSeedRecovery = work.appendingPathComponent("manual-seed-recovery")
        let manualSeedRoot = work.appendingPathComponent("manual-seed-ipad")
        try fm.createDirectory(at: manualSeedFolder, withIntermediateDirectories: true)
        try fm.createDirectory(at: manualSeedRecovery, withIntermediateDirectories: true)
        _ = try seed.writeNew(in: manualSeedRecovery)
        try write("default-manual", at: save(manualSeedRoot))
        let manualSeedStore = EngineCloudSaveStore(defaults: manualSeedDefaults, recoveryFolder: manualSeedRecovery,
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-manual-seed"))
        manualSeedStore.engineActivityCheck = { false }
        await manualSeedStore.connect(folder: manualSeedFolder)
        await manualSeedStore.refresh()
        await manualSeedStore.restore(snapshot: manualSeedStore.recoverySnapshots.first { $0.id == seed.id }!,
                                      gameRoot: manualSeedRoot, engineActive: false)
        await manualSeedStore.backup(gameRoot: manualSeedRoot, engineActive: false)
        let manualArchives = try archives(manualSeedFolder.appendingPathComponent("Master Cheef Saves"))
        require(manualArchives.contains { $0.id == seed.id && $0.isVisionProSource },
                "Manual fallback backup also preserves original Vision provenance")
        let manualHeads = heads(manualArchives)
        require(manualHeads.count == 1 && manualHeads[0].parentSnapshotIDs == [seed.id],
                "Manual fallback backup is a child of the exact Vision source")
        let manualNextDefaultsName = "MasterCheef.SyncValidation.\(UUID().uuidString)"
        let manualNextDefaults = UserDefaults(suiteName: manualNextDefaultsName)!
        defer { manualNextDefaults.removePersistentDomain(forName: manualNextDefaultsName) }
        let manualNextRoot = work.appendingPathComponent("manual-next-ipad")
        try write("default-next-manual", at: save(manualNextRoot))
        let manualNextStore = EngineCloudSaveStore(defaults: manualNextDefaults,
            recoveryFolder: work.appendingPathComponent("manual-next-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-manual-next-seed"))
        manualNextStore.engineActivityCheck = { false }
        await manualNextStore.connect(folder: manualSeedFolder)
        await manualNextStore.refresh()
        await manualNextStore.restore(snapshot: manualNextStore.snapshots.first { $0.id == manualHeads[0].id }!,
                                      gameRoot: manualNextRoot, engineActive: false)
        require(manualNextStore.migrationReady && manualNextStore.canPlay && tryRead(save(manualNextRoot)) == "vision-campaign-A",
                "A fresh iPad can explicitly restore a manual descendant with verified Vision origin")
        print("PASS: \(checks) synthetic migration, offline recovery, ancestry, conflict, account and runtime-gate checks")
    }
    static func tryRead(_ url: URL) -> String? { try? read(url) }
    static func tryData(_ url: URL) -> Data? { try? Data(contentsOf: url) }
}
