import Foundation
import Combine
import CryptoKit

struct EngineSettingsCloudSnapshot: Sendable {
    var account: String
    var documents: [EnginePreferenceDocument]
}

@MainActor
protocol EngineSettingsCloudTransport {
    func read() async throws -> EngineSettingsCloudSnapshot
    func write(_ document: EnginePreferenceDocument, account: String) async throws
}

/// Separate from immutable game-save archives: app preferences may upload while
/// playing, but a remote preference is never applied to a running Halo engine.
@MainActor
final class EngineSettingsSyncStore: ObservableObject {
    @Published private(set) var status = "Settings sync when iCloud is available."
    @Published private(set) var hasPendingApplication = false
    @Published private(set) var lastSync: Date?
    /// Must check the actual native runtime. Paused is not idle.
    var engineIsIdle: () -> Bool = { false }
    var didApply: (Set<String>) -> Void = { _ in }

    private let defaults: UserDefaults
    private let platform: EngineSyncedPreferences.Platform
    private let cloud: any EngineSettingsCloudTransport
    private var state: EnginePreferenceState
    private var baseline: [String: EngineSyncedPreferences.Value]
    private var active = false
    private var epoch = 0
    private var operation: Task<Void, Never>?
    private var debounce: Task<Void, Never>?
    private var subscriptions: Set<AnyCancellable> = []
    private var timer: AnyCancellable?
    private static let stateKey = "MasterCheefSettingsSync.v1"

    init(platform: EngineSyncedPreferences.Platform, defaults: UserDefaults = .standard,
         cloud: (any EngineSettingsCloudTransport)? = nil) {
        self.platform = platform; self.defaults = defaults
        self.cloud = cloud ?? EngineSettingsCloudDocuments()
        baseline = EngineSyncedPreferences.read(defaults, platform: platform)
        if let data = defaults.data(forKey: Self.stateKey),
           let saved = try? JSONDecoder().decode(EnginePreferenceState.self, from: data),
           UUID(uuidString: saved.writer) != nil, saved.clock < 1_000_000_000_000,
           saved.pending.isSubset(of: Set(saved.entries.keys)),
           (try? EnginePreferenceDocument(entries: saved.entries).validated()) != nil {
            state = saved
            if let observed = saved.observed,
               observed.allSatisfy({ EngineSyncedPreferences.valid($0.key, $0.value) }) {
                // New catalog fields use their current baseline, so an upgrade
                // cannot convert newly introduced defaults into authored edits.
                baseline.merge(observed.filter { baseline[$0.key] != nil }) { _, previous in previous }
            }
        } else {
            state = EnginePreferenceState()
            state.bootstrap(baseline)
        }
        captureLocalChanges()
        persist()
        NotificationCenter.default.publisher(for: UserDefaults.didChangeNotification, object: defaults)
            .receive(on: DispatchQueue.main).sink { [weak self] _ in
                MainActor.assumeIsolated { self?.captureLocalChanges() }
            }.store(in: &subscriptions)
        NotificationCenter.default.publisher(for: NSNotification.Name.NSUbiquityIdentityDidChange)
            .receive(on: DispatchQueue.main).sink { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self else { return }
                    self.epoch += 1
                    self.status = "Checking iCloud settings account…"
                    self.schedule()
                }
            }.store(in: &subscriptions)
    }

    func setActive(_ active: Bool) {
        self.active = active
        timer?.cancel(); timer = nil
        if active {
            // A bounded gather also catches cloud-only settings arriving after
            // launch; background apps do not poll or keep a metadata query alive.
            timer = Timer.publish(every: 20, on: .main, in: .common).autoconnect().sink { [weak self] _ in
                MainActor.assumeIsolated { self?.schedule() }
            }
            schedule()
        } else { debounce?.cancel(); debounce = nil; captureLocalChanges() }
    }

    /// Network reconciliation is independent of Play. The launcher starts it
    /// when active; play applies cached settings synchronously instead of waiting
    /// for an unavailable iCloud account, query or file coordinator.
    func syncNow() async {
        debounce?.cancel(); debounce = nil
        if let operation {
            await operation.value
            applyPendingIfIdle() // the engine may have become idle while waiting
            return
        }
        let task = Task { await self.reconcile() }
        operation = task
        await task.value
        operation = nil
        applyPendingIfIdle()
    }

    /// Synchronous and safe immediately before native launch. A paused running
    /// game still fails engineIsIdle and retains incoming choices for next launch.
    func applyPendingIfIdle() {
        captureLocalChanges()
        let wasPending = hasPendingApplication
        applyIfIdle()
        if wasPending && !hasPendingApplication { status = "Settings applied on this device." }
    }

    private func schedule() {
        guard active else { return }
        debounce?.cancel()
        debounce = Task { [weak self] in
            do { try await Task.sleep(nanoseconds: 900_000_000) } catch { return }
            guard !Task.isCancelled else { return }
            // syncNow clears this debounce reference; don't cancel the task
            // currently executing it.
            self?.debounce = nil
            await self?.syncNow()
        }
    }

    private func captureLocalChanges() {
        let current = EngineSyncedPreferences.read(defaults, platform: platform)
        var changed = false
        for (key, value) in current where baseline[key] != value {
            state.edit(key, value: value); changed = true
        }
        baseline = current
        if changed { persist(); status = "Settings saved on this device. Waiting to sync…"; schedule() }
    }

    private func reconcile() async {
        captureLocalChanges()
        let generation = epoch
        do {
            let snapshot = try await cloud.read()
            guard generation == epoch else { throw EnginePreferenceError.accountChanged }
            captureLocalChanges() // account for edits during the asynchronous read
            if let previous = state.account, previous != snapshot.account {
                // Never carry the prior account's imported settings or queued
                // writes into another account. Current local values stay put.
                state = EnginePreferenceState()
                state.account = snapshot.account
                baseline = EngineSyncedPreferences.read(defaults, platform: platform)
            } else { state.account = snapshot.account }
            try state.merge(snapshot.documents)
            persist()
            applyIfIdle()
            if let document = state.pendingDocument {
                try await cloud.write(document, account: snapshot.account)
                guard generation == epoch else { throw EnginePreferenceError.accountChanged }
                captureLocalChanges() // acknowledgment must not erase a newer edit
                state.acknowledge(document)
                persist()
            }
            lastSync = Date()
            status = hasPendingApplication
                ? "New settings are ready for the next game launch."
                : (state.pending.isEmpty ? "Settings synced with iCloud." : "New settings saved locally. Waiting to sync…")
        } catch {
            status = "Settings saved locally. \(error.localizedDescription)"
        }
    }

    private func applyIfIdle() {
        let available = state.values.filter { key, _ in EngineSyncedPreferences.field(key)?.available(on: platform) == true }
        hasPendingApplication = available.contains { baseline[$0.key] != $0.value }
        guard hasPendingApplication, engineIsIdle() else { return }
        let changed = EngineSyncedPreferences.apply(available, to: defaults, platform: platform)
        baseline = EngineSyncedPreferences.read(defaults, platform: platform)
        // Reload models synchronously while idle; observers see our new baseline
        // and do not republish downloaded values as new player choices.
        if !changed.isEmpty { didApply(changed) }
        baseline = EngineSyncedPreferences.read(defaults, platform: platform)
        hasPendingApplication = available.contains { baseline[$0.key] != $0.value }
        persist()
    }
    private func persist() {
        state.observed = baseline
        if let data = try? JSONEncoder().encode(state) { defaults.set(data, forKey: Self.stateKey) }
    }
}

/// Uses the already-signed CloudDocuments container. No KVS capability or new
/// identity is required. Each small write is immutable, preserving conflicting
/// offline choices instead of replacing a shared mutable JSON file.
@MainActor
final class EngineSettingsCloudDocuments: EngineSettingsCloudTransport {
    struct Location: Sendable { let folder: URL; let account: String }
    private let container: String?
    init(container: String? = Bundle.main.object(forInfoDictionaryKey: "MasterCheefCloudContainer") as? String) {
        self.container = container
    }
    private func resolve() async throws -> Location {
        let container = container
        return try await Task.detached(priority: .utility) {
            let fm = FileManager.default
            guard let container, !container.isEmpty,
                  let identity = fm.ubiquityIdentityToken,
                  let token = try? NSKeyedArchiver.archivedData(withRootObject: identity, requiringSecureCoding: false),
                  let root = fm.url(forUbiquityContainerIdentifier: container) else {
                throw EnginePreferenceError.unavailable("iCloud settings are unavailable right now.")
            }
            let account = SHA256.hash(data: token).map { String(format: "%02x", $0) }.joined()
            return Location(folder: root.appendingPathComponent("Documents/117 Settings", isDirectory: true), account: account)
        }.value
    }
    func read() async throws -> EngineSettingsCloudSnapshot {
        let location = try await resolve()
        let discovered = try await EngineSettingsMetadataDiscovery.gather(in: location.folder)
        let documents = try await Task.detached(priority: .utility) {
            let fm = FileManager.default
            let local: [URL]
            if fm.fileExists(atPath: location.folder.path) {
                local = try fm.contentsOfDirectory(at: location.folder, includingPropertiesForKeys: nil)
            } else { local = [] }
            let urls = Set(discovered + local).filter { $0.pathExtension == EnginePreferenceDocument.fileExtension }
            guard urls.count <= 4096 else { throw EnginePreferenceError.unavailable("The settings history needs review; local settings are unchanged.") }
            return try urls.sorted { $0.path < $1.path }.map { url in
                guard url.deletingLastPathComponent().standardizedFileURL == location.folder.standardizedFileURL else {
                    throw EnginePreferenceError.invalidDocument
                }
                let values = try url.resourceValues(forKeys: [.isUbiquitousItemKey, .ubiquitousItemDownloadingStatusKey])
                if values.isUbiquitousItem == true && values.ubiquitousItemDownloadingStatus != .current {
                    try fm.startDownloadingUbiquitousItem(at: url)
                    throw EnginePreferenceError.unavailable("iCloud is still downloading settings.")
                }
                var coordination: NSError?
                var result: Result<EnginePreferenceDocument, Error>?
                NSFileCoordinator().coordinate(readingItemAt: url, options: [], error: &coordination) { coordinated in
                    result = Result {
                        let info = try coordinated.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
                        guard info.isRegularFile == true, info.isSymbolicLink != true,
                              let size = info.fileSize, size > 0, size <= 32_768 else { throw EnginePreferenceError.invalidDocument }
                        return try JSONDecoder().decode(EnginePreferenceDocument.self, from: Data(contentsOf: coordinated)).validated()
                    }
                }
                if let coordination { throw coordination }
                guard let result else { throw EnginePreferenceError.invalidDocument }
                return try result.get()
            }
        }.value
        guard try await resolve().account == location.account else { throw EnginePreferenceError.accountChanged }
        return .init(account: location.account, documents: documents)
    }
    func write(_ document: EnginePreferenceDocument, account: String) async throws {
        let location = try await resolve()
        guard location.account == account else { throw EnginePreferenceError.accountChanged }
        let data = try JSONEncoder().encode(document.validated())
        try await Task.detached(priority: .utility) {
            let fm = FileManager.default
            try fm.createDirectory(at: location.folder, withIntermediateDirectories: true)
            let target = location.folder.appendingPathComponent(document.id).appendingPathExtension(EnginePreferenceDocument.fileExtension)
            var coordination: NSError?
            var writing: Error?
            NSFileCoordinator().coordinate(writingItemAt: target, options: [], error: &coordination) { coordinated in
                do {
                    guard let identity = fm.ubiquityIdentityToken,
                          let token = try? NSKeyedArchiver.archivedData(withRootObject: identity, requiringSecureCoding: false),
                          SHA256.hash(data: token).map({ String(format: "%02x", $0) }).joined() == account else {
                        throw EnginePreferenceError.accountChanged
                    }
                    if fm.fileExists(atPath: coordinated.path) {
                        guard try Data(contentsOf: coordinated) == data else { throw EnginePreferenceError.invalidDocument }
                    } else {
                        // Atomic installation prevents a process interruption from
                        // leaving a truncated immutable document in cloud history.
                        try data.write(to: coordinated, options: .atomic)
                    }
                } catch { writing = error }
            }
            if let coordination { throw coordination }
            if let writing { throw writing }
        }.value
    }
}

@MainActor
private final class EngineSettingsMetadataDiscovery {
    private let query = NSMetadataQuery()
    private var observer: NSObjectProtocol?
    private var timeout: Task<Void, Never>?
    private var continuation: CheckedContinuation<[URL], Error>?
    static func gather(in folder: URL) async throws -> [URL] {
        try await EngineSettingsMetadataDiscovery().run(folder)
    }
    private func run(_ folder: URL) async throws -> [URL] {
        try await withCheckedThrowingContinuation { continuation in
            self.continuation = continuation
            query.searchScopes = [NSMetadataQueryUbiquitousDocumentsScope]
            query.predicate = NSPredicate(format: "%K BEGINSWITH %@ AND %K ENDSWITH %@", NSMetadataItemPathKey,
                                          folder.path + "/", NSMetadataItemFSNameKey, "." + EnginePreferenceDocument.fileExtension)
            query.operationQueue = .main
            observer = NotificationCenter.default.addObserver(forName: .NSMetadataQueryDidFinishGathering, object: query, queue: .main) { [weak self] _ in
                Task { @MainActor in self?.complete() }
            }
            guard query.start() else {
                finish(.failure(EnginePreferenceError.unavailable("iCloud could not check settings."))); return
            }
            timeout = Task { [weak self] in
                do { try await Task.sleep(nanoseconds: 5_000_000_000) } catch { return }
                self?.finish(.failure(EnginePreferenceError.unavailable("iCloud has not finished checking settings.")))
            }
        }
    }
    private func complete() {
        let urls = (0..<query.resultCount).compactMap { (query.result(at: $0) as? NSMetadataItem)?.value(forAttribute: NSMetadataItemURLKey) as? URL }
        finish(.success(urls))
    }
    private func finish(_ result: Result<[URL], Error>) {
        guard let continuation else { return }
        self.continuation = nil; query.stop()
        if let observer { NotificationCenter.default.removeObserver(observer) }
        observer = nil; timeout?.cancel(); timeout = nil
        continuation.resume(with: result)
    }
}
