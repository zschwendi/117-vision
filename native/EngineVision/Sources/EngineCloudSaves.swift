import Foundation
import Combine
#if os(iOS)
import UIKit
#endif

struct EngineSaveSnapshotSummary: Identifiable, Sendable {
    let id: UUID
    let created: Date
    let device: String
    let fileCount: Int
    let bytes: Int
    let filename: String
    let fingerprint: String
    var isRecovery = false
}

/// A dependency seam for local, synthetic tests. Production resolves the signed
/// ubiquity container named by MasterCheefCloudContainer in Info.plist.
struct EngineCloudSaveLocation: Sendable {
    let folder: URL
    let account: String
    var ubiquitous = false
}

/// Observes a cloud save folder and only signals; it never reads or writes saves.
/// Production uses a live NSMetadataQuery; synthetic tests inject a recorder.
@MainActor
protocol EngineSaveCloudWatching: AnyObject {
    /// Returns false if observation could not begin; the store then owns
    /// nothing and retries on the next foreground, identity change or start.
    func start(folder: URL) -> Bool
    func stop()
}
typealias EngineSaveCloudWatcherFactory = @MainActor (EngineCloudSaveLocation, @escaping @MainActor () -> Void) -> EngineSaveCloudWatching?

@MainActor
final class EngineCloudSaveStore: ObservableObject {
    @Published private(set) var isBusy = false
    @Published private(set) var folderName: String?
    @Published private(set) var status = "Checking cloud saves…"
    @Published private(set) var snapshots: [EngineSaveSnapshotSummary] = []
    @Published private(set) var recoverySnapshots: [EngineSaveSnapshotSummary] = []
    @Published private(set) var migrationReady: Bool
    @Published private(set) var hasConflicts = false {
        didSet {
            state.unresolvedConflict = hasConflicts
            saveState()
        }
    }
    @Published private(set) var lastChecked: Date?
    @Published private(set) var pendingSync = false
    @Published var automaticSyncEnabled: Bool {
        didSet { defaults.set(automaticSyncEnabled, forKey: "MasterCheefAutomaticSaves.v2") }
    }
    /// This checks the actual runtime, not the view's possibly stale Boolean.
    var engineActivityCheck: (@MainActor () -> Bool)?
    /// Debounced late iCloud arrivals and account changes. A change observed
    /// during an operation is delivered after it, never dropped. Subscribers
    /// must still reconcile only while this process has never started Halo.
    let cloudChanges = PassthroughSubject<Void, Never>()

    private struct State: Codable {
        var scope: String?
        var baselineID: UUID?
        var baselineFingerprint: String?
        var lineageID: UUID?
        var seedFingerprint: String?
        var seedArchiveFilename: String?
        var migrationConsumed = false
        var unresolvedConflict = false
    }
    private struct Transport: Sendable {
        let folder: URL
        let scope: String
        let securityRoot: URL?
        let ubiquitous: Bool
        let automatic: Bool
        var canReconcileAutomatically: Bool { automatic || ubiquitous }
    }
    private struct Inventory: Sendable {
        var archives: [EngineSaveArchive]
        var unavailable: Int
        var uploadPending: Bool
    }
    private let defaults: UserDefaults
    private let bookmarkKey = "MasterCheefSaveFolderBookmark.v1"
    private let labelKey = "MasterCheefSaveFolderLabel.v1"
    private let stateKey = "MasterCheefSaveSyncState.v2"
    private let recoveryFolder: URL
    private let requiresVisionSeed: Bool
    private let sourcePlatform: String
    private let deviceLabel: String
    private let containerIdentifier: String?
    private let migrationArchiveURL: URL
    private let cloudLocationProvider: (@Sendable () -> EngineCloudSaveLocation?)?
    private var bookmark: Data?
    private var state: State
    private var localSafetyReady = false
    private let cloudChangeDebounce: TimeInterval
    private var cloudChangeTask: Task<Void, Never>?
    private let cloudWatcherFactory: EngineSaveCloudWatcherFactory?
    private let metadataDiscovery: (@Sendable (URL) async throws -> [URL])?
    private var cloudWatcher: EngineSaveCloudWatching?
    private var watchedFolder: URL?
    private var monitorGeneration = 0
    private var monitorResolving = false
    private var monitoringRetired = false
    private var identityObserver: NSObjectProtocol?
    private nonisolated static let subfolder = "Master Cheef Saves"

    init(defaults: UserDefaults = .standard, recoveryFolder: URL? = nil,
         requiresVisionSeed: Bool? = nil, sourcePlatform: String? = nil,
         migrationArchiveURL: URL? = nil,
         cloudContainerIdentifier: String? = nil,
         cloudLocationProvider: (@Sendable () -> EngineCloudSaveLocation?)? = nil,
         deviceLabel: String? = nil, cloudChangeDebounce: TimeInterval = 2,
         cloudWatcherFactory: EngineSaveCloudWatcherFactory? = nil,
         metadataDiscovery: (@Sendable (URL) async throws -> [URL])? = nil) {
        self.defaults = defaults
        self.cloudChangeDebounce = cloudChangeDebounce
        self.metadataDiscovery = metadataDiscovery
        self.bookmark = defaults.data(forKey: bookmarkKey)
        self.recoveryFolder = recoveryFolder ?? FileManager.default.urls(for: .applicationSupportDirectory,
            in: .userDomainMask)[0].appendingPathComponent("MasterCheefSaveRecovery", isDirectory: true)
        self.requiresVisionSeed = requiresVisionSeed
            ?? (Bundle.main.object(forInfoDictionaryKey: "MasterCheefRequireVisionSeed") as? Bool ?? false)
        self.containerIdentifier = cloudContainerIdentifier
            ?? Bundle.main.object(forInfoDictionaryKey: "MasterCheefCloudContainer") as? String
        self.cloudLocationProvider = cloudLocationProvider
        // Only the signed ubiquity container is watched in production.
        if let cloudWatcherFactory {
            self.cloudWatcherFactory = cloudWatcherFactory
        } else if cloudLocationProvider == nil && self.containerIdentifier != nil {
            self.cloudWatcherFactory = Self.ubiquitousWatcher
        } else {
            self.cloudWatcherFactory = nil
        }
        self.migrationArchiveURL = migrationArchiveURL ?? FileManager.default.urls(for: .documentDirectory,
            in: .userDomainMask)[0].appendingPathComponent("MasterCheefMigration/vision-pro-save.mc-save")
        #if os(visionOS)
        let platform = "Vision Pro", label = platform
        #elseif os(iOS)
        // Every iOS device publishes as the validated "iPad" family; installed
        // Vision builds reject unknown platforms. Only the label names iPhone.
        let platform = "iPad", label = UIDevice.current.userInterfaceIdiom == .phone ? "iPhone" : "iPad"
        #else
        let platform = "Mac", label = platform
        #endif
        self.sourcePlatform = sourcePlatform ?? platform
        self.deviceLabel = deviceLabel ?? (sourcePlatform ?? label)
        self.state = defaults.data(forKey: stateKey).flatMap { try? JSONDecoder().decode(State.self, from: $0) } ?? State()
        self.migrationReady = !self.requiresVisionSeed || self.state.seedFingerprint != nil
        self.automaticSyncEnabled = defaults.object(forKey: "MasterCheefAutomaticSaves.v2") == nil
            || defaults.bool(forKey: "MasterCheefAutomaticSaves.v2")
        self.folderName = defaults.string(forKey: labelKey)
            ?? ((self.containerIdentifier != nil || cloudLocationProvider != nil) ? "iCloud Drive" : nil)
        self.hasConflicts = self.state.unresolvedConflict
        status = migrationReady ? "Saves will be checked before playing."
            : "Waiting for your existing Vision Pro save. New \(self.deviceLabel) saves will not replace it."
    }

    deinit {
        if let identityObserver { NotificationCenter.default.removeObserver(identityObserver) }
    }

    var canPlay: Bool { migrationReady && !hasConflicts && !isBusy && localSafetyReady }
    var localRecoveryFolder: URL { recoveryFolder }

    /// The caller must still check its actual runtime before starting the game.
    /// A cloud outage permits local play only after the required source was captured.
    func prepareForPlay(gameRoot: URL, engineActive: Bool, currentOperationTimeout: TimeInterval = 30) async -> Bool {
        // An automatic launch/foreground check may already own the save tree.
        // Keep the user's play request while that owner finishes; never clear
        // its busy flag, cancel its writes, or start Halo alongside it.
        let timeout = currentOperationTimeout.isFinite ? max(0, currentOperationTimeout) : 30
        let deadline = ProcessInfo.processInfo.systemUptime + timeout
        while isBusy {
            guard !Task.isCancelled else { return false }
            guard ProcessInfo.processInfo.systemUptime < deadline else {
                status = "Save checking is taking longer than expected. Wait for it to finish, then try again."
                return false
            }
            do { try await Task.sleep(nanoseconds: 50_000_000) }
            catch { return false }
        }
        guard !Task.isCancelled else { return false }
        await sync(gameRoot: gameRoot, engineActive: engineActive)
        return !Task.isCancelled && canPlay && !engineActive && !(engineActivityCheck?() ?? false)
    }

    func sync(gameRoot: URL, engineActive: Bool) async {
        guard !isBusy else { return }
        guard !engineActive, !(engineActivityCheck?() ?? false) else {
            status = EngineSaveError.engineActive.localizedDescription
            return
        }
        isBusy = true
        localSafetyReady = false
        defer { isBusy = false }
        await synchronizeIdle(gameRoot: gameRoot)
        await refreshRecovery()
    }

    private func synchronizeIdle(gameRoot: URL) async {
        do {
            var local = try await captureLocal(gameRoot)
            if let local { try await preserve(local) }
            // A previously captured campaign going missing is not a fresh
            // installation, even when sync is disabled or iCloud is offline.
            localSafetyReady = local != nil
                || (state.baselineFingerprint == nil && state.seedFingerprint == nil)
            // A trusted, separately copied device export is consumed once. Retain
            // the original archive, including after a successful migration.
            if requiresVisionSeed, !state.migrationConsumed,
               FileManager.default.fileExists(atPath: migrationArchiveURL.path) {
                let url = migrationArchiveURL
                let seed = try await Task.detached { try EngineSaveArchive.read(url) }.value
                guard seed.isVisionProSource else {
                    throw EngineSaveError.invalid("The migration archive does not identify a Vision Pro source.")
                }
                if migrationReady {
                    // Never replay an old migration file over subsequent gameplay.
                    state.migrationConsumed = true
                    saveState()
                } else {
                    try await preserveSeed(seed)
                    try await apply(seed, gameRoot: gameRoot)
                    state.seedFingerprint = seed.fingerprint
                    state.lineageID = seed.lineageID ?? seed.id
                    state.migrationConsumed = true
                    migrationReady = true
                    accept(seed)
                    local = try await captureLocal(gameRoot)
                    if let local { try await preserve(local) }
                }
            }
            guard automaticSyncEnabled else {
                pendingSync = true
                status = localSafetyReady
                    ? (migrationReady ? "Automatic sync is off. A local recovery copy is safe."
                        : "Waiting for your Vision Pro save. Enable sync or restore its snapshot.")
                    : "Local saves are missing. Restore preserved progress before playing."
                return
            }
            guard let transport = try await resolveTransport() else {
                pendingSync = true
                status = localSafetyReady
                    ? (migrationReady ? "iCloud is unavailable. Local saves are safe; sync is pending."
                        : "Waiting for your Vision Pro save. iCloud is unavailable on this device.")
                    : "Local saves are missing. Restore preserved progress before playing."
                return
            }
            let access = transport.securityRoot?.startAccessingSecurityScopedResource() ?? false
            defer { if access { transport.securityRoot?.stopAccessingSecurityScopedResource() } }
            adoptScope(transport.scope)
            var inventory = try await loadInventory(transport)
            snapshots = inventory.archives.map(Self.summary).sorted { $0.created > $1.created }
            lastChecked = .now
            guard transport.canReconcileAutomatically else {
                pendingSync = true
                status = localSafetyReady
                    ? "This Files provider cannot confirm a complete cloud inventory. Use manual snapshots here, or the automatic iCloud folder."
                    : "Local saves are missing. Restore a preserved snapshot before playing."
                return
            }
            guard inventory.unavailable == 0 else {
                pendingSync = true
                status = localSafetyReady
                    ? "Some cloud snapshots are still downloading or invalid. No cloud saves were replaced."
                    : "Local saves are missing and cloud snapshots are incomplete. Restore preserved progress before playing."
                return
            }
            // Publish the exact retained Vision source, including when it was
            // captured while offline. A fresh iPad revision must not hide its
            // provenance or become the only cloud copy.
            try await publishRetainedSeed(into: &inventory, transport: transport)
            let heads = try Self.heads(inventory.archives)
            let distinct = Set(heads.map(\.fingerprint))
            guard migrationReady else {
                // Only an unambiguous Vision Pro revision may establish the first
                // iPad authority. Bundled/default data never becomes a seed.
                guard distinct.count == 1, let selected = heads.first,
                      let seed = Self.visionSource(of: selected, archives: inventory.archives) else {
                    hasConflicts = distinct.count > 1
                    status = hasConflicts ? "Choose the Vision Pro snapshot to continue. Both versions are preserved."
                        : "Waiting for your existing Vision Pro save. New \(deviceLabel) saves will not replace it."
                    pendingSync = true
                    return
                }
                try await validateTransport(transport)
                try await preserveSeed(seed)
                try await apply(selected, gameRoot: gameRoot)
                state.seedFingerprint = seed.fingerprint
                state.migrationConsumed = true
                state.lineageID = seed.lineageID ?? seed.id
                migrationReady = true
                accept(selected)
                hasConflicts = false
                pendingSync = inventory.uploadPending
                status = "Vision Pro save restored and preserved. Ready to play."
                return
            }
            guard let local else {
                // A missing local tree after previous play is not deletion intent.
                if heads.count == 1, state.baselineFingerprint == nil {
                    try await validateTransport(transport)
                    try await apply(heads[0], gameRoot: gameRoot)
                    accept(heads[0])
                    hasConflicts = false
                    status = "Cloud save restored. Ready to play."
                    pendingSync = inventory.uploadPending
                } else if !heads.isEmpty || state.baselineFingerprint != nil || state.seedFingerprint != nil {
                    hasConflicts = true
                    status = "Local saves are missing. Choose a preserved snapshot before playing."
                } else {
                    hasConflicts = false
                    status = "No campaign saves yet. Your first save will be preserved on the next app launch."
                }
                return
            }
            if heads.isEmpty {
                let published = try await publish(local, parents: [], transport: transport)
                accept(published)
                hasConflicts = false
                pendingSync = true
                status = "Save preserved locally and sent to iCloud. Upload may still be pending."
                return
            }
            if distinct.count == 1, let same = heads.first(where: { $0.fingerprint == local.fingerprint }) {
                accept(same)
                hasConflicts = false
                pendingSync = inventory.uploadPending
                status = pendingSync ? "Local save is safe. Waiting for iCloud upload."
                    : "Available cloud saves match this device."
                return
            }
            if distinct.count == 1, let head = heads.first,
               local.fingerprint == state.baselineFingerprint,
               let baselineID = state.baselineID,
               Self.descends(head, from: baselineID, archives: inventory.archives) {
                try await validateTransport(transport)
                try await apply(head, gameRoot: gameRoot)
                accept(head)
                hasConflicts = false
                pendingSync = inventory.uploadPending
                status = "Newer cloud revision restored. Previous progress is kept in recovery."
                return
            }
            if distinct.count == 1, let baseline = state.baselineFingerprint,
               heads.allSatisfy({ $0.fingerprint == baseline }), local.fingerprint != baseline {
                let published = try await publish(local, parents: heads, transport: transport)
                accept(published)
                hasConflicts = false
                pendingSync = true
                status = "Your new save is preserved locally and queued for iCloud."
                return
            }
            // Do not manufacture a timestamp winner or change either branch.
            hasConflicts = true
            pendingSync = true
            status = "Different progress exists on this device and in iCloud. Choose a snapshot; all copies are preserved."
        } catch {
            pendingSync = true
            status = "Save sync needs attention. \(error.localizedDescription)"
            // Local safety remains true if only cloud access failed after the
            // stable local recovery copy was made. Fresh migration still gates play.
        }
    }

    func connect(folder: URL) async {
        guard !isBusy else { return }
        isBusy = true; defer { isBusy = false }
        do {
            let access = folder.startAccessingSecurityScopedResource()
            defer { if access { folder.stopAccessingSecurityScopedResource() } }
            #if os(macOS)
            let data = try folder.bookmarkData(options: .withSecurityScope, includingResourceValuesForKeys: nil, relativeTo: nil)
            #else
            let data = try folder.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil)
            #endif
            let transport = try Self.folderTransport(data)
            try await Task.detached { try Self.ensureFolder(transport.folder) }.value
            bookmark = data
            folderName = folder.lastPathComponent
            defaults.set(data, forKey: bookmarkKey)
            defaults.set(folderName, forKey: labelKey)
            clearBaseline()
            snapshots = []
            status = "Files folder connected. Existing snapshots are preserved."
        } catch { status = "Could not connect: \(error.localizedDescription)" }
    }

    func disconnect() {
        guard !isBusy else { return }
        bookmark = nil
        defaults.removeObject(forKey: bookmarkKey)
        defaults.removeObject(forKey: labelKey)
        folderName = containerIdentifier != nil || cloudLocationProvider != nil ? "iCloud Drive" : nil
        snapshots = []
        clearBaseline()
        status = "Files fallback disconnected. Existing saves are untouched."
    }

    func refresh() async {
        guard !isBusy else { return }
        isBusy = true; defer { isBusy = false }
        await refreshRecovery()
        do {
            guard let transport = try await resolveTransport() else {
                status = "iCloud is unavailable. Local saves are safe."
                pendingSync = true
                return
            }
            let access = transport.securityRoot?.startAccessingSecurityScopedResource() ?? false
            defer { if access { transport.securityRoot?.stopAccessingSecurityScopedResource() } }
            adoptScope(transport.scope)
            let inventory = try await loadInventory(transport)
            snapshots = inventory.archives.map(Self.summary).sorted { $0.created > $1.created }
            let cloudHasConflicts = Set(try Self.heads(inventory.archives).map(\.fingerprint)).count > 1
            hasConflicts = hasConflicts || cloudHasConflicts
            pendingSync = inventory.uploadPending || inventory.unavailable > 0
            lastChecked = .now
            status = inventory.unavailable == 0
                ? "\(snapshots.count) preserved snapshots available."
                    + (transport.canReconcileAutomatically ? "" : " Manual Files transport; cloud inventory cannot be confirmed.")
                : "\(snapshots.count) available; \(inventory.unavailable) still downloading or invalid."
        } catch { status = "Cloud unavailable. Local saves are safe. \(error.localizedDescription)" }
    }

    /// Explicit backup adds an immutable branch; it never resolves a conflict.
    func backup(gameRoot: URL, engineActive: Bool) async {
        guard !isBusy else { return }
        guard migrationReady else { status = "Restore your Vision Pro save before publishing from this \(deviceLabel)."; return }
        guard !engineActive, !(engineActivityCheck?() ?? false) else { status = EngineSaveError.engineActive.localizedDescription; return }
        isBusy = true; defer { isBusy = false }
        do {
            guard let local = try await captureLocal(gameRoot) else { throw EngineSaveError.empty }
            try await preserve(local)
            localSafetyReady = true
            guard let transport = try await resolveTransport() else {
                pendingSync = true; status = "Backup kept in local recovery. iCloud is unavailable."; return
            }
            let access = transport.securityRoot?.startAccessingSecurityScopedResource() ?? false
            defer { if access { transport.securityRoot?.stopAccessingSecurityScopedResource() } }
            adoptScope(transport.scope)
            var inventory = try await loadInventory(transport)
            guard inventory.unavailable == 0 else { throw EngineSaveError.invalid("Wait for cloud snapshots to finish downloading before publishing.") }
            try await publishRetainedSeed(into: &inventory, transport: transport)
            var parents = inventory.archives.filter { $0.id == state.baselineID }
            if parents.isEmpty, let seedName = state.seedArchiveFilename,
               let seed = inventory.archives.first(where: { $0.filename == seedName }) {
                // An explicit backup into a newly selected Files folder is a
                // new branch of the retained source, never an originless copy.
                parents = [seed]
            }
            let published = try await publish(local, parents: parents, transport: transport)
            let heads = try Self.heads(inventory.archives + [published])
            hasConflicts = Set(heads.map(\.fingerprint)).count > 1
            if !hasConflicts { accept(published) }
            pendingSync = true
            status = "Snapshot preserved locally and in Files. Cloud delivery may still be pending."
        } catch { status = "Backup failed; local saves are unchanged. \(error.localizedDescription)" }
        await refreshRecovery()
    }

    /// Explicit selection records a merge revision consuming the observed heads.
    /// Every source archive and the previous local tree remain recoverable.
    func restore(snapshot: EngineSaveSnapshotSummary, gameRoot: URL, engineActive: Bool) async {
        guard !isBusy else {
            status = "Another save check is running. Nothing was restored; choose the snapshot again when it finishes."
            return
        }
        guard !engineActive, !(engineActivityCheck?() ?? false) else { status = EngineSaveError.engineActive.localizedDescription; return }
        isBusy = true; defer { isBusy = false }
        var restoredLocally = false
        do {
            let transport = try await resolveTransport()
            let access = transport?.securityRoot?.startAccessingSecurityScopedResource() ?? false
            defer { if access { transport?.securityRoot?.stopAccessingSecurityScopedResource() } }
            var inventory = Inventory(archives: [], unavailable: 0, uploadPending: false)
            if let transport {
                adoptScope(transport.scope)
                inventory = try await loadInventory(transport)
            }
            let archive: EngineSaveArchive
            if snapshot.isRecovery {
                let url = recoveryFolder.appendingPathComponent(snapshot.filename)
                archive = try await Task.detached { try EngineSaveArchive.read(url) }.value
            } else {
                guard let selected = inventory.archives.first(where: { $0.id == snapshot.id }) else {
                    throw EngineSaveError.invalid("That cloud snapshot is unavailable. Refresh after downloading.")
                }
                archive = selected
            }
            guard archive.id == snapshot.id, archive.fingerprint == snapshot.fingerprint else {
                throw EngineSaveError.invalid("The selected snapshot changed. Refresh and choose it again.")
            }
            let seed = Self.visionSource(of: archive, archives: inventory.archives + (snapshot.isRecovery ? [archive] : []))
            guard migrationReady || seed != nil else {
                throw EngineSaveError.invalid("Choose the original Vision Pro snapshot to finish first-time setup.")
            }
            if !migrationReady { try await preserveSeed(seed!) }
            if let transport { try await validateTransport(transport) }
            try await apply(archive, gameRoot: gameRoot)
            restoredLocally = true
            if !migrationReady {
                state.seedFingerprint = seed!.fingerprint
                state.migrationConsumed = true
                migrationReady = true
            }
            state.lineageID = archive.lineageID ?? archive.id
            accept(archive)
            hasConflicts = false
            localSafetyReady = true
            if let transport, inventory.unavailable == 0, transport.canReconcileAutomatically {
                // The selected local recovery may not exist in cloud yet.
                // Publish its exact Vision origin and complete ancestry before
                // creating a new iPad revision; never turn it into an orphan.
                try await publishRetainedSeed(into: &inventory, transport: transport)
                try await ensureCloudArchive(archive, inventory: &inventory, transport: transport)
                var parents = try Self.heads(inventory.archives)
                for parent in parents {
                    try await ensureCloudArchive(parent, inventory: &inventory, transport: transport)
                }
                parents.removeAll { $0.id == archive.id }
                parents.insert(archive, at: 0)
                let revision = try await publish(archive, parents: parents, transport: transport)
                accept(revision)
                pendingSync = true
                status = "Selected save restored. Previous progress is preserved; cloud update is pending."
            } else {
                pendingSync = true
                status = "Selected save restored locally. Other cloud revisions still need checking."
            }
        } catch {
            pendingSync = true
            status = restoredLocally
                ? "Selected save is restored locally; cloud sync is pending. \(error.localizedDescription)"
                : "Restore failed. \(error.localizedDescription)"
        }
        await refreshRecovery()
    }

    /// Watches the automatic iCloud folder for snapshots that arrive after the
    /// bounded launch check (a Vision export, a slow download, another device's
    /// revision) and for account changes. It only signals `cloudChanges`.
    /// Idempotent: call at launch and on foreground; an unavailable account
    /// leaves nothing owned, so a later call or identity change can retry.
    func startCloudMonitoring() {
        guard !monitoringRetired, let factory = cloudWatcherFactory else { return }
        observeCloudIdentity()
        guard cloudWatcher == nil, !monitorResolving else { return }
        monitorResolving = true
        let generation = monitorGeneration
        Task { [weak self] in
            guard let self else { return }
            let location = await self.resolveCloudLocation()
            // A newer identity change owns resolution; never start a stale root.
            guard generation == self.monitorGeneration else { return }
            self.monitorResolving = false
            guard !self.monitoringRetired, self.cloudWatcher == nil, let location,
                  let watcher = factory(location, { [weak self] in self?.noteCloudChange() }) else { return }
            // Ownership only after a successful start; no immediate retry loop.
            guard watcher.start(folder: location.folder) else { watcher.stop(); return }
            self.cloudWatcher = watcher
            self.watchedFolder = location.folder
        }
    }

    /// The account changed: drop the previous container's watcher, resolve the
    /// current one at this layer, and reconcile once (debounced with its gather).
    func cloudIdentityDidChange() {
        guard !monitoringRetired else { return }
        monitorGeneration += 1
        monitorResolving = false
        cloudWatcher?.stop()
        cloudWatcher = nil
        watchedFolder = nil
        startCloudMonitoring()
        noteCloudChange()
    }

    /// The folder currently watched, if any (diagnostics and tests).
    var monitoredFolder: URL? { watchedFolder }

    /// Call once Halo starts: this process can no longer reconcile saves, so
    /// monitoring is retired permanently for it.
    func stopCloudMonitoring() {
        monitoringRetired = true
        monitorGeneration += 1
        monitorResolving = false
        cloudWatcher?.stop()
        cloudWatcher = nil
        watchedFolder = nil
        if let identityObserver { NotificationCenter.default.removeObserver(identityObserver) }
        identityObserver = nil
        cloudChangeTask?.cancel()
        cloudChangeTask = nil
    }

    func noteCloudChange() {
        guard !monitoringRetired else { return }
        cloudChangeTask?.cancel()
        let delay = UInt64(max(0, cloudChangeDebounce) * 1_000_000_000)
        cloudChangeTask = Task { [weak self] in
            do { try await Task.sleep(nanoseconds: delay) } catch { return }
            while true {
                guard let self else { return }
                if !self.isBusy { break }
                do { try await Task.sleep(nanoseconds: 250_000_000) } catch { return }
            }
            guard let self, !Task.isCancelled, !self.monitoringRetired else { return }
            self.cloudChangeTask = nil
            self.cloudChanges.send()
        }
    }

    private func observeCloudIdentity() {
        guard identityObserver == nil else { return }
        identityObserver = NotificationCenter.default.addObserver(forName: .NSUbiquityIdentityDidChange,
            object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.cloudIdentityDidChange() }
        }
    }

    private func checkIdle() throws {
        guard !(engineActivityCheck?() ?? false) else { throw EngineSaveError.engineActive }
    }

    private func captureLocal(_ gameRoot: URL) async throws -> EngineSaveArchive? {
        try checkIdle()
        let platform = deviceLabel
        let archive = try await Task.detached(priority: .userInitiated) {
            let root = try EngineSaveArchive.mutableRoot(gameRoot: gameRoot)
            guard FileManager.default.fileExists(atPath: root.path) else { return Optional<EngineSaveArchive>.none }
            do {
                let first = try EngineSaveArchive.capture(gameRoot: gameRoot, device: platform, engineActive: false)
                let second = try EngineSaveArchive.capture(gameRoot: gameRoot, device: platform, engineActive: false)
                guard first.fingerprint == second.fingerprint else { throw EngineSaveError.changed }
                return first
            } catch EngineSaveError.empty { return nil }
        }.value
        try checkIdle()
        return archive
    }

    private func preserve(_ archive: EngineSaveArchive) async throws {
        let recovery = recoveryFolder
        try await Task.detached(priority: .utility) {
            try Self.ensureFolder(recovery)
            // Deduplicate identical recovery bytes, but never delete history.
            let entries = try FileManager.default.contentsOfDirectory(at: recovery, includingPropertiesForKeys: nil)
            for url in entries where url.pathExtension == EngineSaveArchive.fileExtension {
                if let old = try? EngineSaveArchive.read(url), old.fingerprint == archive.fingerprint { return }
            }
            _ = try archive.writeNew(in: recovery)
        }.value
    }

    private func preserveSeed(_ archive: EngineSaveArchive) async throws {
        let recovery = recoveryFolder
        try await Task.detached {
            try Self.ensureFolder(recovery)
            let destination = recovery.appendingPathComponent(archive.filename)
            if FileManager.default.fileExists(atPath: destination.path) {
                let existing = try EngineSaveArchive.read(destination)
                guard existing.id == archive.id, existing.fingerprint == archive.fingerprint,
                      existing.isVisionProSource else { throw EngineSaveError.invalid("The Vision Pro recovery identity does not match.") }
            } else { _ = try archive.writeNew(in: recovery) }
        }.value
        state.seedArchiveFilename = archive.filename
    }

    private func publishRetainedSeed(into inventory: inout Inventory, transport: Transport) async throws {
        guard let name = state.seedArchiveFilename else { return }
        let url = recoveryFolder.appendingPathComponent(name)
        let seed = try await Task.detached { try EngineSaveArchive.read(url) }.value
        guard seed.isVisionProSource, seed.fingerprint == state.seedFingerprint else {
            throw EngineSaveError.invalid("The preserved Vision Pro source could not be verified.")
        }
        try await ensureCloudArchive(seed, inventory: &inventory, transport: transport)
    }

    /// Materializes a complete immutable chain from cloud and local recovery.
    /// Missing ancestry holds publication pending instead of inventing a root.
    private func ensureCloudArchive(_ archive: EngineSaveArchive, inventory: inout Inventory, transport: Transport) async throws {
        let existing = inventory.archives, recovery = recoveryFolder
        let additions = try await Task.detached { () throws -> [EngineSaveArchive] in
            let cloud = Dictionary(uniqueKeysWithValues: existing.map { ($0.id, $0) })
            var ordered: [EngineSaveArchive] = [], visited = Set<UUID>(), stack = Set<UUID>()
            func visit(_ item: EngineSaveArchive) throws {
                guard !stack.contains(item.id) else { throw EngineSaveError.invalid("Snapshot ancestry contains a cycle.") }
                guard !visited.contains(item.id) else { return }
                guard visited.count + stack.count < 500 else { throw EngineSaveError.invalid("Snapshot ancestry exceeds its limit.") }
                if let stored = cloud[item.id] {
                    guard stored.version == item.version, stored.fingerprint == item.fingerprint,
                          stored.created == item.created, stored.device == item.device,
                          stored.sourcePlatform == item.sourcePlatform,
                          stored.parentSnapshotIDs == item.parentSnapshotIDs,
                          stored.parentFingerprint == item.parentFingerprint,
                          stored.lineageID == item.lineageID else {
                        throw EngineSaveError.invalid("An immutable cloud revision changed. Its local copy is preserved.")
                    }
                }
                stack.insert(item.id)
                for (index, id) in (item.parentSnapshotIDs ?? []).enumerated() {
                    let parent: EngineSaveArchive
                    if let stored = cloud[id] { parent = stored }
                    else {
                        let url = recovery.appendingPathComponent(id.uuidString.lowercased() + "." + EngineSaveArchive.fileExtension)
                        guard FileManager.default.fileExists(atPath: url.path) else {
                            throw EngineSaveError.invalid("A selected snapshot's earlier revision is unavailable. Its local restore is safe; cloud publication is pending.")
                        }
                        parent = try EngineSaveArchive.read(url)
                        guard parent.id == id else { throw EngineSaveError.invalid("A preserved revision identity does not match.") }
                    }
                    if index == 0, parent.fingerprint != item.parentFingerprint {
                        throw EngineSaveError.invalid("A preserved ancestry checksum does not match.")
                    }
                    try visit(parent)
                }
                stack.remove(item.id)
                visited.insert(item.id)
                if cloud[item.id] == nil { ordered.append(item) }
            }
            try visit(archive)
            return ordered
        }.value
        for item in additions {
            try await validateTransport(transport)
            try await Task.detached { try Self.coordinatedWrite(item, folder: transport.folder) }.value
            inventory.archives.append(item)
            if !snapshots.contains(where: { $0.id == item.id }) { snapshots.append(Self.summary(item)) }
        }
    }

    private func apply(_ archive: EngineSaveArchive, gameRoot: URL) async throws {
        try checkIdle()
        localSafetyReady = false
        let recovery = recoveryFolder
        _ = try await Task.detached(priority: .userInitiated) {
            try archive.restore(gameRoot: gameRoot, recoveryFolder: recovery, engineActive: false)
        }.value
        try checkIdle()
        localSafetyReady = true
    }

    private func publish(_ archive: EngineSaveArchive, parents: [EngineSaveArchive], transport: Transport) async throws -> EngineSaveArchive {
        try checkIdle()
        try await validateTransport(transport)
        let revision = archive.revision(platform: sourcePlatform, parents: parents, lineage: state.lineageID, device: deviceLabel)
        try await preserve(revision)
        try await Task.detached(priority: .userInitiated) {
            try Self.coordinatedWrite(revision, folder: transport.folder)
        }.value
        snapshots.removeAll { $0.id == revision.id }
        snapshots.insert(Self.summary(revision), at: 0)
        return revision
    }

    private func accept(_ archive: EngineSaveArchive) {
        state.baselineID = archive.id
        state.baselineFingerprint = archive.fingerprint
        state.lineageID = archive.lineageID ?? state.lineageID ?? archive.id
        saveState()
    }

    private func saveState() {
        if let data = try? JSONEncoder().encode(state) { defaults.set(data, forKey: stateKey) }
    }

    private func clearBaseline() {
        state.scope = nil
        state.baselineID = nil
        state.baselineFingerprint = nil
        // Keep the captured Vision source and local saves across account changes.
        hasConflicts = false
        pendingSync = true
        saveState()
    }

    private func adoptScope(_ scope: String) {
        if state.scope != scope {
            // The first connection after a local device migration can retain
            // the seed baseline: the exact seed is published into that scope
            // before reconciliation. A later account/folder change cannot.
            if state.scope != nil || state.seedArchiveFilename == nil { clearBaseline() }
            state.scope = scope
            saveState()
        }
    }

    private func resolveTransport() async throws -> Transport? {
        if let bookmark { return try Self.folderTransport(bookmark) }
        guard let location = await resolveCloudLocation() else { return nil }
        return Transport(folder: location.folder,
                         scope: "cloud:" + EngineSaveArchive.digest(Data((location.account + "\n" + location.folder.path).utf8)),
                         securityRoot: nil, ubiquitous: location.ubiquitous, automatic: true)
    }

    /// The automatic container for the current account, ignoring any Files bookmark.
    private func resolveCloudLocation() async -> EngineCloudSaveLocation? {
        let identifier = containerIdentifier, provider = cloudLocationProvider
        return await Task.detached(priority: .utility) { () -> EngineCloudSaveLocation? in
            if let provider { return provider() }
            guard let identifier,
                  let token = FileManager.default.ubiquityIdentityToken,
                  let tokenData = try? NSKeyedArchiver.archivedData(withRootObject: token, requiringSecureCoding: false),
                  let root = FileManager.default.url(forUbiquityContainerIdentifier: identifier) else { return nil }
            return EngineCloudSaveLocation(folder: root.appendingPathComponent("Documents/\(Self.subfolder)", isDirectory: true),
                                           account: EngineSaveArchive.digest(tokenData), ubiquitous: true)
        }.value
    }

    private func validateTransport(_ transport: Transport) async throws {
        guard let current = try await resolveTransport(), current.scope == transport.scope else {
            clearBaseline()
            throw EngineSaveError.invalid("Your cloud account or folder changed. Check saves again before continuing.")
        }
        try checkIdle()
    }

    private nonisolated static func folderTransport(_ bookmark: Data) throws -> Transport {
        var stale = false
        #if os(macOS)
        let options: URL.BookmarkResolutionOptions = [.withSecurityScope, .withoutUI]
        #else
        let options: URL.BookmarkResolutionOptions = [.withoutUI]
        #endif
        let folder = try URL(resolvingBookmarkData: bookmark, options: options, relativeTo: nil, bookmarkDataIsStale: &stale)
        guard !stale else { throw EngineSaveError.invalid("The folder permission expired. Choose the folder again.") }
        let access = folder.startAccessingSecurityScopedResource()
        defer { if access { folder.stopAccessingSecurityScopedResource() } }
        let values = try folder.resourceValues(forKeys: [.isUbiquitousItemKey])
        return Transport(folder: folder.appendingPathComponent(subfolder, isDirectory: true),
                         scope: "folder:" + EngineSaveArchive.digest(Data(folder.standardizedFileURL.path.utf8)),
                         securityRoot: folder, ubiquitous: values.isUbiquitousItem == true, automatic: false)
    }

    private func loadInventory(_ transport: Transport) async throws -> Inventory {
        let metadataURLs: [URL]
        if let metadataDiscovery {
            metadataURLs = try await metadataDiscovery(transport.folder)
        } else if transport.ubiquitous {
            metadataURLs = try await EngineSaveMetadataDiscovery.gather(in: transport.folder)
        } else { metadataURLs = [] }
        return try await Task.detached(priority: .utility) {
            try Self.ensureFolder(transport.folder)
            let local = try FileManager.default.contentsOfDirectory(at: transport.folder, includingPropertiesForKeys: nil)
            // An evicted iCloud item is listed only as ".<name>.icloud". It is an
            // undownloaded snapshot, never an absent one.
            let placeholders = local.compactMap(Self.placeholderTarget)
            for url in placeholders { try? FileManager.default.startDownloadingUbiquitousItem(at: url) }
            // Metadata may spell the container /private/var/... while the listing
            // says /var/.... Compare physical parent directories, then read only
            // the trusted spelling: an alias is deduplicated by name, and nothing
            // outside the folder is ever read. A file symlink is still rejected
            // by EngineSaveArchive.read, which never follows the final link.
            let trusted = try Self.physicalDirectory(transport.folder)
            var byName: [String: URL] = [:], escaped = 0
            for url in local + placeholders + metadataURLs where url.pathExtension == EngineSaveArchive.fileExtension {
                guard let parent = try? Self.physicalDirectory(url.deletingLastPathComponent()), parent == trusted else {
                    escaped += 1
                    continue
                }
                byName[url.lastPathComponent] = transport.folder.appendingPathComponent(url.lastPathComponent)
            }
            let candidates = Array(byName.values)
            guard candidates.count <= 500 else { throw EngineSaveError.invalid("More than 500 cloud snapshots are present. Archive older snapshots in Files before syncing.") }
            // An item reported outside the folder is unexplained: hold publication.
            var result = Inventory(archives: [], unavailable: escaped, uploadPending: false)
            let deadline = ProcessInfo.processInfo.systemUptime + 8
            for url in candidates {
                do {
                    try Task.checkCancellation()
                    guard ProcessInfo.processInfo.systemUptime < deadline else {
                        result.unavailable += 1
                        continue
                    }
                    // Compare physical directories, never URL spellings: a
                    // folder URL made before mkdir lacks the trailing slash a
                    // child's parent URL has. The file itself is not resolved;
                    // read() still rejects a snapshot that is a symlink.
                    guard try Self.physicalDirectory(url.deletingLastPathComponent()) == trusted else {
                        throw EngineSaveError.invalid("A cloud snapshot escaped its folder.")
                    }
                    try await Self.materialize(url, deadline: deadline)
                    let archive = try Self.readCoordinated(url)
                    guard archive.filename == url.lastPathComponent else { throw EngineSaveError.invalid("Snapshot filename does not match its identity.") }
                    result.archives.append(archive)
                    let values = try url.resourceValues(forKeys: [.isUbiquitousItemKey, .ubiquitousItemIsUploadedKey, .ubiquitousItemUploadingErrorKey])
                    if let error = values.ubiquitousItemUploadingError { throw error }
                    if values.isUbiquitousItem == true && values.ubiquitousItemIsUploaded != true { result.uploadPending = true }
                } catch { result.unavailable += 1 }
            }
            _ = try Self.heads(result.archives)
            return result
        }.value
    }

    private static func ubiquitousWatcher(_ location: EngineCloudSaveLocation,
                                          _ changed: @escaping @MainActor () -> Void) -> EngineSaveCloudWatching? {
        location.ubiquitous ? EngineSaveCloudMonitor(changed: changed) : nil
    }

    /// realpath(3) of a directory: one spelling for /var and /private/var.
    nonisolated static func physicalDirectory(_ url: URL) throws -> String {
        guard let resolved = realpath(url.path, nil) else {
            throw EngineSaveError.invalid("A cloud save folder is unavailable.")
        }
        defer { free(resolved) }
        return String(cString: resolved)
    }

    nonisolated static func placeholderTarget(_ url: URL) -> URL? {
        let name = url.lastPathComponent, suffix = "." + EngineSaveArchive.fileExtension + ".icloud"
        guard name.hasPrefix("."), name.hasSuffix(suffix), name.count > suffix.count + 1 else { return nil }
        return url.deletingLastPathComponent().appendingPathComponent(String(name.dropFirst().dropLast(".icloud".count)))
    }

    /// Spotlight may report either the container path or its /private form.
    nonisolated static func metadataPredicate(for folder: URL) -> NSPredicate {
        var paths = Set([folder.path, folder.resolvingSymlinksInPath().path, folder.standardizedFileURL.path])
        if let physical = try? physicalDirectory(folder) { paths.insert(physical) }
        return NSCompoundPredicate(orPredicateWithSubpredicates: paths.sorted().map {
            NSPredicate(format: "%K BEGINSWITH %@", NSMetadataItemPathKey, $0 + "/")
        })
    }

    private func refreshRecovery() async {
        let recovery = recoveryFolder
        recoverySnapshots = (try? await Task.detached(priority: .utility) {
            guard FileManager.default.fileExists(atPath: recovery.path) else { return [EngineSaveSnapshotSummary]() }
            return try FileManager.default.contentsOfDirectory(at: recovery, includingPropertiesForKeys: nil)
                .filter { $0.pathExtension == EngineSaveArchive.fileExtension }
                .compactMap { url -> EngineSaveSnapshotSummary? in
                    guard let archive = try? EngineSaveArchive.read(url) else { return nil }
                    var value = Self.summary(archive); value.isRecovery = true; return value
                }.sorted { $0.created > $1.created }
        }.value) ?? []
    }

    private nonisolated static func summary(_ archive: EngineSaveArchive) -> EngineSaveSnapshotSummary {
        .init(id: archive.id, created: archive.created, device: archive.device,
              fileCount: archive.entries.count, bytes: archive.totalBytes,
              filename: archive.filename, fingerprint: archive.fingerprint)
    }

    private nonisolated static func heads(_ archives: [EngineSaveArchive]) throws -> [EngineSaveArchive] {
        var byID: [UUID: EngineSaveArchive] = [:]
        for archive in archives {
            guard byID.updateValue(archive, forKey: archive.id) == nil else { throw EngineSaveError.invalid("Duplicate cloud snapshot identities were found.") }
        }
        var parents = Set<UUID>()
        for archive in archives {
            if let first = archive.parentSnapshotIDs?.first, let parent = byID[first],
               archive.parentFingerprint != parent.fingerprint {
                throw EngineSaveError.invalid("A snapshot ancestry checksum does not match.")
            }
            var visited = Set<UUID>()
            func visit(_ id: UUID, stack: Set<UUID>) throws {
                guard !stack.contains(id) else { throw EngineSaveError.invalid("Cloud revision ancestry contains a cycle.") }
                guard visited.insert(id).inserted, let item = byID[id] else { return }
                var stack = stack; stack.insert(id)
                for parent in item.parentSnapshotIDs ?? [] { try visit(parent, stack: stack) }
            }
            try visit(archive.id, stack: [])
            parents.formUnion(archive.parentSnapshotIDs ?? [])
        }
        return archives.filter { !parents.contains($0.id) }.sorted { $0.id.uuidString < $1.id.uuidString }
    }

    private nonisolated static func descends(_ archive: EngineSaveArchive, from baseline: UUID, archives: [EngineSaveArchive]) -> Bool {
        let byID = Dictionary(uniqueKeysWithValues: archives.map { ($0.id, $0) })
        var pending = archive.parentSnapshotIDs ?? [], seen = Set<UUID>()
        while let id = pending.popLast() {
            if id == baseline { return true }
            if seen.insert(id).inserted { pending += byID[id]?.parentSnapshotIDs ?? [] }
        }
        return false
    }

    private nonisolated static func visionSource(of archive: EngineSaveArchive, archives: [EngineSaveArchive]) -> EngineSaveArchive? {
        var byID: [UUID: EngineSaveArchive] = [:]
        for item in archives { byID[item.id] = item }
        var pending = [archive], seen = Set<UUID>()
        while let item = pending.popLast() {
            guard seen.insert(item.id).inserted else { continue }
            if item.isVisionProSource { return item }
            pending += (item.parentSnapshotIDs ?? []).compactMap { byID[$0] }
        }
        return nil
    }

    private nonisolated static func ensureFolder(_ folder: URL) throws {
        let fm = FileManager.default
        if fm.fileExists(atPath: folder.path) {
            guard try fm.attributesOfItem(atPath: folder.path)[.type] as? FileAttributeType == .typeDirectory else {
                throw EngineSaveError.invalid("Save folders must be real directories, not links.")
            }
        } else { try fm.createDirectory(at: folder, withIntermediateDirectories: true) }
    }

    private nonisolated static func readCoordinated(_ url: URL) throws -> EngineSaveArchive {
        var error: NSError?, result: Result<EngineSaveArchive, Error>?
        NSFileCoordinator().coordinate(readingItemAt: url, options: [], error: &error) { coordinated in
            result = Result { try EngineSaveArchive.read(coordinated) }
        }
        if let error { throw error }
        guard let result else { throw EngineSaveError.invalid("Files did not provide the selected snapshot.") }
        return try result.get()
    }

    private nonisolated static func coordinatedWrite(_ archive: EngineSaveArchive, folder: URL) throws {
        try ensureFolder(folder)
        var error: NSError?, result: Result<Void, Error>?
        NSFileCoordinator().coordinate(writingItemAt: folder, options: .forMerging, error: &error) { coordinated in
            result = Result { _ = try archive.writeNew(in: coordinated) }
        }
        if let error { throw error }
        guard let result else { throw EngineSaveError.invalid("Files did not accept the snapshot.") }
        try result.get()
    }

    private nonisolated static func materialize(_ url: URL, deadline: TimeInterval) async throws {
        var current = url
        let keys: Set<URLResourceKey> = [.isUbiquitousItemKey, .ubiquitousItemDownloadingStatusKey, .ubiquitousItemDownloadingErrorKey]
        var values = try current.resourceValues(forKeys: keys)
        guard values.isUbiquitousItem == true, values.ubiquitousItemDownloadingStatus != .current else { return }
        try FileManager.default.startDownloadingUbiquitousItem(at: current)
        for _ in 0..<40 {
            try Task.checkCancellation()
            guard ProcessInfo.processInfo.systemUptime < deadline else { break }
            current.removeAllCachedResourceValues()
            values = try current.resourceValues(forKeys: keys)
            if let error = values.ubiquitousItemDownloadingError { throw error }
            if values.ubiquitousItemDownloadingStatus == .current { return }
            try await Task.sleep(nanoseconds: 100_000_000)
        }
        throw EngineSaveError.invalid("iCloud is still downloading a save snapshot. Try again shortly.")
    }
}

/// A bounded metadata gather discovers cloud-only placeholders before the first
/// sync decision. A timeout is an unavailable inventory, never an empty cloud.
@MainActor
private final class EngineSaveMetadataDiscovery {
    private let query = NSMetadataQuery()
    private var observer: NSObjectProtocol?
    private var timeout: Task<Void, Never>?
    private var continuation: CheckedContinuation<[URL], Error>?

    static func gather(in folder: URL) async throws -> [URL] {
        let discovery = EngineSaveMetadataDiscovery()
        return try await discovery.run(folder)
    }

    private func run(_ folder: URL) async throws -> [URL] {
        try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            query.searchScopes = [NSMetadataQueryUbiquitousDocumentsScope]
            query.predicate = EngineCloudSaveStore.metadataPredicate(for: folder)
            query.operationQueue = .main
            observer = NotificationCenter.default.addObserver(forName: .NSMetadataQueryDidFinishGathering,
                object: query, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.complete() }
            }
            guard query.start() else {
                finish(.failure(EngineSaveError.invalid("iCloud could not start checking saves.")))
                return
            }
            timeout = Task { [weak self] in
                do { try await Task.sleep(nanoseconds: 5_000_000_000) } catch { return }
                self?.finish(.failure(EngineSaveError.invalid("iCloud has not finished checking saves. Try again shortly.")))
            }
        }
    }

    private func complete() {
        let urls = (0..<query.resultCount).compactMap { index -> URL? in
            (query.result(at: index) as? NSMetadataItem)?.value(forAttribute: NSMetadataItemURLKey) as? URL
        }
        finish(.success(urls))
    }

    private func finish(_ result: Result<[URL], Error>) {
        guard let continuation else { return }
        self.continuation = nil
        query.stop()
        if let observer { NotificationCenter.default.removeObserver(observer) }
        observer = nil
        timeout?.cancel(); timeout = nil
        continuation.resume(with: result)
    }
}

/// Summarizes only identity and download/upload state. Progress ticks and our
/// own unchanged inventory do not cause another reconciliation pass.
enum EngineCloudChangeSignature {
    struct Item: Hashable, Sendable {
        let name: String
        let downloaded: Bool
        let uploaded: Bool
    }
    static func of(_ items: [Item]) -> String {
        items.filter { $0.name.hasSuffix("." + EngineSaveArchive.fileExtension) }
            .sorted { $0.name < $1.name }
            .map { "\($0.name)|\($0.downloaded ? 1 : 0)\($0.uploaded ? 1 : 0)" }
            .joined(separator: "\n")
    }
}

/// A live NSMetadataQuery for snapshots that arrive after launch. Account
/// changes are handled by the store, which re-resolves the container root.
@MainActor
private final class EngineSaveCloudMonitor: EngineSaveCloudWatching {
    private let changed: @MainActor () -> Void
    private var query: NSMetadataQuery?
    private var observers: [NSObjectProtocol] = []
    private var signature: String?

    init(changed: @escaping @MainActor () -> Void) { self.changed = changed }

    func start(folder: URL) -> Bool {
        stop()
        signature = nil
        let query = NSMetadataQuery()
        query.searchScopes = [NSMetadataQueryUbiquitousDocumentsScope]
        query.predicate = EngineCloudSaveStore.metadataPredicate(for: folder)
        query.operationQueue = .main
        let center = NotificationCenter.default
        for name in [Notification.Name.NSMetadataQueryDidFinishGathering, .NSMetadataQueryDidUpdate] {
            observers.append(center.addObserver(forName: name, object: query, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.inspect() }
            })
        }
        guard query.start() else { stop(); return false }
        self.query = query
        return true
    }

    func stop() {
        query?.stop(); query = nil
        for observer in observers { NotificationCenter.default.removeObserver(observer) }
        observers = []
    }

    private func inspect() {
        guard let query else { return }
        query.disableUpdates(); defer { query.enableUpdates() }
        let items = (0..<query.resultCount).compactMap { index -> EngineCloudChangeSignature.Item? in
            guard let item = query.result(at: index) as? NSMetadataItem,
                  let name = item.value(forAttribute: NSMetadataItemFSNameKey) as? String else { return nil }
            let status = item.value(forAttribute: NSMetadataUbiquitousItemDownloadingStatusKey) as? String
            let uploaded = item.value(forAttribute: NSMetadataUbiquitousItemIsUploadedKey) as? Bool ?? false
            return .init(name: name, downloaded: status == NSMetadataUbiquitousItemDownloadingStatusCurrent, uploaded: uploaded)
        }
        let next = EngineCloudChangeSignature.of(items)
        guard next != signature else { return }
        signature = next
        // The first gather also signals: the bounded launch check may have
        // timed out before iCloud finished listing the folder.
        changed()
    }
}
