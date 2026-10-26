import Foundation

@main struct CloudSaveValidation {
    static var checks = 0
    static func require(_ value: Bool, _ message: String) {
        checks += 1; precondition(value, message)
    }
    static func reject(_ message: String, _ body: () throws -> Void) {
        do { try body(); fatalError("Accepted: \(message)") }
        catch { checks += 1 }
    }
    static func write(_ text: String, _ path: URL) throws {
        try FileManager.default.createDirectory(at: path.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(text.utf8).write(to: path)
    }
    static func contents(_ path: URL) throws -> String { String(decoding: try Data(contentsOf: path), as: UTF8.self) }
    static func changed(_ archive: EngineSaveArchive, game: String? = nil,
                        entries: [EngineSaveArchive.Entry]? = nil) -> EngineSaveArchive {
        .init(version: archive.version, game: game ?? archive.game, id: archive.id,
              created: archive.created, device: archive.device, entries: entries ?? archive.entries)
    }
    @MainActor static func main() async throws {
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appendingPathComponent("MasterCheef-Synthetic-Saves-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: work) }
        let first = work.appendingPathComponent("vision"), second = work.appendingPathComponent("ipad")
        let cloud = work.appendingPathComponent("fake-cloud"), recovery = work.appendingPathComponent("recovery")
        try fm.createDirectory(at: cloud, withIntermediateDirectories: true)
        let saveA = first.appendingPathComponent(EngineSaveArchive.mutablePath)
        let saveB = second.appendingPathComponent(EngineSaveArchive.mutablePath)
        let checkpoint = "saved/player_profiles/Chief/savegame.bin"
        try write("checkpoint-A", saveA.appendingPathComponent(checkpoint))
        try write("profile-A", saveA.appendingPathComponent("saved/player_profiles/Chief/blam.sav"))
        try write("keep hidden data", saveA.appendingPathComponent(".user-note"))
        try write("private fake registration", first.appendingPathComponent("halo-vision-registry.txt"))
        try write("fake map", first.appendingPathComponent("maps/ui.map"))
        let a = try EngineSaveArchive.capture(gameRoot: first, device: "Synthetic Vision", engineActive: false)
        require(a.entries.count == 3, "Only the exact save subtree is captured, including hidden user files")
        require(!a.entries.contains { $0.path.contains("registry") || $0.path.contains("ui.map") }, "No registration or maps")
        reject("active engine capture") { _ = try EngineSaveArchive.capture(gameRoot: first, device: "test", engineActive: true) }
        let uploaded = try a.writeNew(in: cloud)
        let downloaded = try EngineSaveArchive.read(uploaded)
        require(downloaded.fingerprint == a.fingerprint, "Roundtrip preserves every checksum/path")
        reject("snapshot overwrite") { _ = try a.writeNew(in: cloud) }
        try write("checkpoint-B-newer", saveB.appendingPathComponent(checkpoint))
        try write("ipad-only-profile", saveB.appendingPathComponent("saved/player_profiles/Other/blam.sav"))
        try write("unchanged iPad map", second.appendingPathComponent("maps/ui.map"))
        reject("active engine restore") { _ = try a.restore(gameRoot: second, recoveryFolder: recovery, engineActive: true) }
        require(try contents(saveB.appendingPathComponent(checkpoint)) == "checkpoint-B-newer", "Active refusal preserves progress")
        let beforeURL = try a.restore(gameRoot: second, recoveryFolder: recovery, engineActive: false)!
        require(try contents(saveB.appendingPathComponent(checkpoint)) == "checkpoint-A", "Explicit restore updates save tree")
        require(try contents(second.appendingPathComponent("maps/ui.map")) == "unchanged iPad map", "Maps remain unchanged")
        let before = try EngineSaveArchive.read(beforeURL)
        require(before.entries.contains { String(decoding: $0.data, as: UTF8.self) == "ipad-only-profile" }, "Recovery preserves independent profile")
        _ = try before.restore(gameRoot: second, recoveryFolder: recovery, engineActive: false)
        require(try contents(saveB.appendingPathComponent(checkpoint)) == "checkpoint-B-newer", "Recovery restores former progress")
        require(try EngineSaveArchive.read(uploaded).fingerprint == a.fingerprint, "Restore never changes cloud archive")
        for path in ["../escape", "/absolute", "a//b", "a/./b", "a/../b", "a\\b", "C:foo", "null\0path"] {
            reject("unsafe path \(path)") {
                try changed(a, entries: [.init(path: path, data: Data(), sha256: EngineSaveArchive.digest(Data()))]).validate()
            }
        }
        reject("checksum corruption") { try changed(a, entries: [.init(path: "blam.sav", data: Data([1]), sha256: String(repeating: "0", count: 64))]).validate() }
        reject("wrong game") { try changed(a, game: "halo-xbox").validate() }
        let emptyHash = EngineSaveArchive.digest(Data())
        reject("case collision") { try changed(a, entries: [.init(path: "A.sav", data: Data(), sha256: emptyHash), .init(path: "a.sav", data: Data(), sha256: emptyHash)]).validate() }
        reject("Unicode normalization collision") { try changed(a, entries: [.init(path: "é.sav", data: Data(), sha256: emptyHash), .init(path: "e\u{0301}.sav", data: Data(), sha256: emptyHash)]).validate() }
        reject("file/directory collision") { try changed(a, entries: [.init(path: "A", data: Data(), sha256: emptyHash), .init(path: "a/file", data: Data(), sha256: emptyHash)]).validate() }
        reject("too many files") { try changed(a, entries: (0...EngineSaveArchive.maximumFiles).map { .init(path: "f\($0)", data: Data(), sha256: emptyHash) }).validate() }
        let link = saveA.appendingPathComponent("linked")
        try fm.createSymbolicLink(at: link, withDestinationURL: first.appendingPathComponent("halo-vision-registry.txt"))
        reject("save symlink") { _ = try EngineSaveArchive.capture(gameRoot: first, device: "test", engineActive: false) }
        try fm.removeItem(at: link)
        try fm.linkItem(at: saveA.appendingPathComponent(checkpoint), to: link)
        reject("save hardlink") { _ = try EngineSaveArchive.capture(gameRoot: first, device: "test", engineActive: false) }
        try fm.removeItem(at: link)
        let brokenRoot = work.appendingPathComponent("broken")
        try fm.createDirectory(at: brokenRoot, withIntermediateDirectories: true)
        try fm.createSymbolicLink(at: brokenRoot.appendingPathComponent("Users"), withDestinationURL: work.appendingPathComponent("missing"))
        reject("dangling parent link") { _ = try a.restore(gameRoot: brokenRoot, recoveryFolder: recovery, engineActive: false) }
        let bad = cloud.appendingPathComponent("truncated.mc-save")
        try Data([0, 1, 2]).write(to: bad)
        reject("truncated snapshot") { _ = try EngineSaveArchive.read(bad) }

        // Real bookmark + NSFileCoordinator transport, entirely in the synthetic
        // temporary directory. No iCloud, device, real saves, or user defaults.
        let suite = "MasterCheefCloudSaveTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = EngineCloudSaveStore(defaults: defaults, recoveryFolder: recovery)
        await store.connect(folder: cloud)
        require(store.folderName != nil && !store.isBusy, "Folder permission connects")
        await store.backup(gameRoot: first, engineActive: false)
        require(store.snapshots.count == 1, "Coordinated backup publishes snapshot")
        let reopened = EngineCloudSaveStore(defaults: defaults, recoveryFolder: recovery)
        await reopened.refresh()
        require(reopened.snapshots.count == 1 && reopened.recoverySnapshots.count == 2, "Bookmark reload and recovery history")
        reopened.engineActivityCheck = { true }
        await reopened.restore(snapshot: reopened.snapshots[0], gameRoot: second, engineActive: false)
        require(try contents(saveB.appendingPathComponent(checkpoint)) == "checkpoint-B-newer", "Live runtime check overrides stale UI Boolean")
        reopened.engineActivityCheck = { false }
        await reopened.restore(snapshot: reopened.snapshots[0], gameRoot: second, engineActive: false)
        require(try contents(saveB.appendingPathComponent(checkpoint)) == "checkpoint-A", "Coordinated restore reaches second device tree")
        reopened.disconnect()
        require(reopened.folderName == nil && fm.fileExists(atPath: uploaded.path), "Disconnect preserves saves and archives")
        print("PASS: \(checks) synthetic archive, recovery, path, integrity, activity-gate and Files transport checks")
    }
}
