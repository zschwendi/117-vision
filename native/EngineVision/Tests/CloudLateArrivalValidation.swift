import Foundation
import Combine

private final class LateLocationBox: @unchecked Sendable {
    private let lock = NSLock()
    private var value: EngineCloudSaveLocation?
    init(_ value: EngineCloudSaveLocation?) { self.value = value }
    func get() -> EngineCloudSaveLocation? { lock.lock(); defer { lock.unlock() }; return value }
}

/// Synthetic late-arrival, coalescing, busy-deferral, iPhone-label and
/// placeholder checks. Temporary folders only; no iCloud, device or real saves.
@main struct CloudLateArrivalValidation {
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
        try FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension == EngineSaveArchive.fileExtension }.map(EngineSaveArchive.read)
    }
    static func heads(_ archives: [EngineSaveArchive]) -> [EngineSaveArchive] {
        let parents = Set(archives.flatMap { $0.parentSnapshotIDs ?? [] })
        return archives.filter { !parents.contains($0.id) }
    }
    @MainActor static func wait(_ seconds: Double = 5, until condition: () -> Bool) async -> Bool {
        let deadline = Date().addingTimeInterval(seconds)
        while !condition() {
            guard Date() < deadline else { return false }
            try? await Task.sleep(nanoseconds: 20_000_000)
        }
        return true
    }

    @MainActor static func main() async throws {
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appendingPathComponent("MasterCheef-Late-Synthetic-\(UUID().uuidString)")
        defer { try? fm.removeItem(at: work) }
        let cloud = work.appendingPathComponent("cloud")
        try fm.createDirectory(at: cloud, withIntermediateDirectories: true)
        let box = LateLocationBox(.init(folder: cloud, account: "late-account"))
        var suites: [String] = []
        defer { for name in suites { UserDefaults.standard.removePersistentDomain(forName: name) } }
        func defaults() -> UserDefaults {
            let name = "MasterCheef.LateValidation.\(UUID().uuidString)"
            suites.append(name)
            return UserDefaults(suiteName: name)!
        }

        // Signature ignores progress-only noise but sees arrival and download.
        typealias Item = EngineCloudChangeSignature.Item
        let base = EngineCloudChangeSignature.of([.init(name: "a.mc-save", downloaded: false, uploaded: true)])
        require(base == EngineCloudChangeSignature.of([.init(name: "a.mc-save", downloaded: false, uploaded: true),
                                                       .init(name: "notes.txt", downloaded: true, uploaded: true)]),
                "Unrelated files do not trigger reconciliation")
        require(base != EngineCloudChangeSignature.of([.init(name: "a.mc-save", downloaded: true, uploaded: true)]),
                "Download completion triggers reconciliation")
        require(base != EngineCloudChangeSignature.of([.init(name: "a.mc-save", downloaded: false, uploaded: true),
                                                       .init(name: "b.mc-save", downloaded: false, uploaded: false)]),
                "A newly arrived snapshot triggers reconciliation")
        let link = URL(fileURLWithPath: "/tmp/MasterCheefLate/Documents/Master Cheef Saves")
        let predicate = EngineCloudSaveStore.metadataPredicate(for: link)
        require(predicate.evaluate(with: [NSMetadataItemPathKey: link.path + "/x.mc-save"])
                && predicate.evaluate(with: [NSMetadataItemPathKey: link.resolvingSymlinksInPath().path + "/x.mc-save"]),
                "Metadata discovery matches both container path spellings")
        require(!predicate.evaluate(with: [NSMetadataItemPathKey: link.path + " 2/x.mc-save"]),
                "Metadata discovery excludes sibling folders")
        require(EngineCloudSaveStore.placeholderTarget(cloud.appendingPathComponent(".abc.mc-save.icloud"))?.lastPathComponent == "abc.mc-save"
                && EngineCloudSaveStore.placeholderTarget(cloud.appendingPathComponent("abc.mc-save")) == nil,
                "Evicted iCloud placeholders map to their snapshot name")

        // A fresh iPhone waits; the Vision snapshot arrives later in the session.
        let phoneRoot = work.appendingPathComponent("iphone"), visionRoot = work.appendingPathComponent("vision")
        try write("bundled-default", at: save(phoneRoot))
        let phone = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("phone-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("no-seed"),
            cloudLocationProvider: { box.get() }, deviceLabel: "iPhone", cloudChangeDebounce: 0.05)
        var haloStarted = false
        phone.engineActivityCheck = { haloStarted }
        var deliveries = 0, deliveredWhileBusy = false
        // Mirrors the app: reconcile only while this process never started Halo.
        let subscription = phone.cloudChanges.sink {
            deliveries += 1
            deliveredWhileBusy = deliveredWhileBusy || phone.isBusy
            guard !haloStarted else { return }
            Task { @MainActor in await phone.sync(gameRoot: phoneRoot, engineActive: false) }
        }
        defer { subscription.cancel() }
        let waiting = await phone.prepareForPlay(gameRoot: phoneRoot, engineActive: false)
        let cloudAfterWait = try archives(cloud)
        require(!waiting && !phone.migrationReady && cloudAfterWait.isEmpty, "Fresh iPhone waits without publishing defaults")

        try write("vision-campaign-A", at: save(visionRoot))
        let vision = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("vision-recovery"),
            requiresVisionSeed: false, sourcePlatform: "Vision Pro", cloudLocationProvider: { box.get() })
        vision.engineActivityCheck = { false }
        let visionReady = await vision.prepareForPlay(gameRoot: visionRoot, engineActive: false)
        let visionCloud = try archives(cloud)
        require(visionReady && visionCloud.count == 1, "Vision publishes its campaign after the iPhone checked")

        phone.noteCloudChange(); phone.noteCloudChange(); phone.noteCloudChange()
        require(await wait { phone.migrationReady && !phone.isBusy }, "Late Vision arrival completes migration without reopening")
        require(deliveries == 1, "Rapid cloud notifications coalesce into one reconciliation")
        require(read(save(phoneRoot)) == "vision-campaign-A", "Late arrival restores actual Vision progress")
        let readyAfterLate = await phone.prepareForPlay(gameRoot: phoneRoot, engineActive: false)
        require(readyAfterLate, "Play is allowed after the late migration")

        // A change seen during an operation is delivered after it, not dropped.
        try write("iphone-progress-B", at: save(phoneRoot))
        let running = Task { await phone.sync(gameRoot: phoneRoot, engineActive: false) }
        var spins = 0
        while !phone.isBusy && spins < 10_000 { await Task.yield(); spins += 1 }
        require(phone.isBusy, "Fixture observes an in-flight sync")
        await phone.restore(snapshot: phone.snapshots[0], gameRoot: phoneRoot, engineActive: false)
        require(phone.status.contains("Nothing was restored") && read(save(phoneRoot)) == "iphone-progress-B",
                "A restore confirmed during another operation reports refusal and changes nothing")
        let before = deliveries
        phone.noteCloudChange()
        await running.value
        require(await wait { deliveries == before + 1 } && !deliveredWhileBusy, "Busy-time change is delivered after the operation")
        require(await wait { !phone.isBusy }, "Deferred reconciliation finishes")

        let published = heads(try archives(cloud))
        require(published.count == 1 && published[0].sourcePlatform == "iPad" && published[0].device == "iPhone",
                "iPhone revisions keep the validated iPad family and an iPhone label")
        let phoneLocal = try EngineSaveArchive.capture(gameRoot: phoneRoot, device: "x", engineActive: false)
        require(published[0].fingerprint == phoneLocal.fingerprint,
                "iPhone progress is published")
        let visionFollow = await vision.prepareForPlay(gameRoot: visionRoot, engineActive: false)
        require(visionFollow && read(save(visionRoot)) == "iphone-progress-B", "Unchanged Vision follows the iPhone descendant")

        // Once Halo starts, late arrivals never touch this process's saves.
        haloStarted = true
        try write("vision-campaign-C", at: save(visionRoot))
        _ = await vision.prepareForPlay(gameRoot: visionRoot, engineActive: false)
        let beforeStarted = deliveries
        phone.noteCloudChange()
        require(await wait { deliveries == beforeStarted + 1 }, "Change is still observed while Halo runs")
        await phone.sync(gameRoot: phoneRoot, engineActive: false)
        require(read(save(phoneRoot)) == "iphone-progress-B", "Running-session gate refuses late cloud restore")
        phone.noteCloudChange()
        phone.stopCloudMonitoring()
        try? await Task.sleep(nanoseconds: 300_000_000)
        require(deliveries == beforeStarted + 1, "Stopping monitoring cancels a pending late-arrival signal")
        haloStarted = false

        // An evicted placeholder is an undownloaded snapshot, never an empty slot.
        let countBefore = try archives(cloud).count
        let placeholder = cloud.appendingPathComponent(".\(UUID().uuidString.lowercased()).mc-save.icloud")
        try Data([1]).write(to: placeholder)
        try write("iphone-local-D", at: save(phoneRoot))
        await phone.sync(gameRoot: phoneRoot, engineActive: false)
        let countAfter = try archives(cloud).count
        require(phone.pendingSync && countAfter == countBefore && read(save(phoneRoot)) == "iphone-local-D",
                "Placeholder holds publication and keeps local progress")
        try fm.removeItem(at: placeholder)
        print("PASS: \(checks) synthetic late-arrival, coalescing, busy-deferral, running-gate, iPhone-label and placeholder checks")
    }
}
