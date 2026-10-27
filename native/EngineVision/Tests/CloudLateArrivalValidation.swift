import Foundation
import Combine

private final class LateLocationBox: @unchecked Sendable {
    private let lock = NSLock()
    private var value: EngineCloudSaveLocation?
    init(_ value: EngineCloudSaveLocation?) { self.value = value }
    func get() -> EngineCloudSaveLocation? { lock.lock(); defer { lock.unlock() }; return value }
}

/// Holds one real store operation at its metadata-discovery seam. The caller
/// can observe a waiting launch without relying on disk timing or real iCloud.
@MainActor private final class PlayMetadataGate {
    var holdNext = false
    private(set) var isHeld = false
    private(set) var calls = 0
    private var continuation: CheckedContinuation<Void, Never>?
    func discover() async -> [URL] {
        calls += 1
        if holdNext {
            holdNext = false
            isHeld = true
            await withCheckedContinuation { continuation = $0 }
            isHeld = false
        }
        return []
    }
    func release() { continuation?.resume(); continuation = nil }
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

        // Play requested during automatic sync waits for that operation, then
        // runs the ordinary save-safety check exactly once before admission.
        let playRoot = work.appendingPathComponent("play-wait"), playCloud = work.appendingPathComponent("play-cloud")
        try fm.createDirectory(at: playCloud, withIntermediateDirectories: true)
        try write("preserved-campaign", at: save(playRoot))
        let gate = PlayMetadataGate()
        let playStore = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("play-recovery"),
            requiresVisionSeed: false, sourcePlatform: "Vision Pro",
            cloudLocationProvider: { .init(folder: playCloud, account: "play-wait-account") },
            metadataDiscovery: { _ in await gate.discover() })
        playStore.engineActivityCheck = { false }
        gate.holdNext = true
        let automatic = Task { await playStore.sync(gameRoot: playRoot, engineActive: false) }
        require(await wait { gate.isHeld && playStore.isBusy }, "Automatic sync owns the store before Play")
        var launchFinished = false
        let launch = Task {
            let allowed = await playStore.prepareForPlay(gameRoot: playRoot, engineActive: false, currentOperationTimeout: 2)
            launchFinished = true
            return allowed
        }
        try await Task.sleep(nanoseconds: 100_000_000)
        require(!launchFinished && playStore.isBusy && gate.calls == 1, "Play remains pending without stealing the save operation")
        gate.release()
        await automatic.value
        require(await launch.value && !playStore.isBusy && gate.calls == 2,
                "One pending Play performs one fresh safety check after automatic sync and is admitted")
        require(read(save(playRoot)) == "preserved-campaign", "Waiting for Play preserves campaign bytes")

        // A bounded timeout reports the wait, leaving the original owner in
        // charge. Releasing that owner proves it can still finish normally.
        gate.holdNext = true
        let slow = Task { await playStore.sync(gameRoot: playRoot, engineActive: false) }
        require(await wait { gate.isHeld && playStore.isBusy }, "Timeout fixture owns a real save operation")
        let timeoutStart = ProcessInfo.processInfo.systemUptime
        let timedOut = await playStore.prepareForPlay(gameRoot: playRoot, engineActive: false, currentOperationTimeout: 0.05)
        require(!timedOut && playStore.isBusy && gate.isHeld && playStore.status.contains("taking longer"),
                "Timed-out Play explains the wait without clearing busy or cancelling the owner")
        require(ProcessInfo.processInfo.systemUptime - timeoutStart < 1, "Busy wait timeout is bounded")
        gate.release(); await slow.value
        require(!playStore.isBusy && read(save(playRoot)) == "preserved-campaign", "Original sync completes safely after Play times out")

        // Cancelling the intent must not cancel a save mutation or start a
        // second reconciliation after the original owner completes.
        gate.holdNext = true
        let cancellingOwner = Task { await playStore.sync(gameRoot: playRoot, engineActive: false) }
        require(await wait { gate.isHeld && playStore.isBusy }, "Cancellation fixture owns a real save operation")
        let callsBeforeCancel = gate.calls
        let cancelledLaunch = Task { await playStore.prepareForPlay(gameRoot: playRoot, engineActive: false, currentOperationTimeout: 2) }
        try await Task.sleep(nanoseconds: 50_000_000)
        cancelledLaunch.cancel()
        require(!(await cancelledLaunch.value) && playStore.isBusy && gate.isHeld,
                "Cancelled Play is refused while the original save operation remains owned")
        gate.release(); await cancellingOwner.value
        require(gate.calls == callsBeforeCancel && !playStore.isBusy,
                "Cancelled Play never starts another save check or admits the engine")

        // Waiting cannot turn a known conflict or a missing Vision seed into
        // permission to start. Exercise the same held-operation path for both.
        let conflictRoot = work.appendingPathComponent("conflict-source")
        try write("unrelated-cloud-progress", at: save(conflictRoot))
        _ = try EngineSaveArchive.capture(gameRoot: conflictRoot, device: "Other Vision", engineActive: false).writeNew(in: playCloud)
        gate.holdNext = true
        let conflictOwner = Task { await playStore.sync(gameRoot: playRoot, engineActive: false) }
        require(await wait { gate.isHeld && playStore.isBusy }, "Conflict fixture starts with an existing operation")
        let conflictLaunch = Task { await playStore.prepareForPlay(gameRoot: playRoot, engineActive: false, currentOperationTimeout: 2) }
        gate.release(); await conflictOwner.value
        require(!(await conflictLaunch.value) && playStore.hasConflicts && read(save(playRoot)) == "preserved-campaign",
                "Waiting Play still rejects conflicting cloud heads without changing local progress")
        let seedRoot = work.appendingPathComponent("seed-wait"), seedCloud = work.appendingPathComponent("seed-cloud")
        try fm.createDirectory(at: seedCloud, withIntermediateDirectories: true)
        try write("bundled-default", at: save(seedRoot))
        let seedGate = PlayMetadataGate()
        let seedStore = EngineCloudSaveStore(defaults: defaults(), recoveryFolder: work.appendingPathComponent("seed-recovery"),
            requiresVisionSeed: true, sourcePlatform: "iPad", migrationArchiveURL: work.appendingPathComponent("absent-play-seed"),
            cloudLocationProvider: { .init(folder: seedCloud, account: "seed-wait-account") },
            metadataDiscovery: { _ in await seedGate.discover() })
        seedStore.engineActivityCheck = { false }
        seedGate.holdNext = true
        let seedOwner = Task { await seedStore.sync(gameRoot: seedRoot, engineActive: false) }
        require(await wait { seedGate.isHeld && seedStore.isBusy }, "Seed fixture starts with an existing operation")
        let seedLaunch = Task { await seedStore.prepareForPlay(gameRoot: seedRoot, engineActive: false, currentOperationTimeout: 2) }
        seedGate.release(); await seedOwner.value
        let seedAllowed = await seedLaunch.value
        let seedArchives = try archives(seedCloud)
        require(!seedAllowed && !seedStore.migrationReady && seedArchives.isEmpty,
                "Waiting Play still requires the existing Vision save and never publishes bundled defaults")
        print("PASS: \(checks) synthetic late-arrival, coalescing, busy-deferral, pending-Play, timeout, cancellation, save-safety and placeholder checks")
    }
}
