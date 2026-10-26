import SwiftUI
import UniformTypeIdentifiers

struct EngineCloudSavesView: View {
    @ObservedObject var store: EngineCloudSaveStore
    let gameRoot: URL
    let engineActive: Bool
    @State private var chooseFolder = false
    @State private var selectedRestore: EngineSaveSnapshotSummary?
    @State private var pickerError: String?

    var body: some View {
        Form {
            Section {
                Label("Your campaign, on every device", systemImage: "icloud")
                    .font(.headline)
                Text("The app checks iCloud before you play and keeps an independent recovery copy. It restores a newer revision only when this device’s progress has not changed.")
                    .foregroundStyle(.secondary)
                Text("Saves sync only before you press Play. After playing, close the app from the app switcher and reopen it, then wait for sync before switching devices.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Section("iCloud") {
                Toggle("Sync Saves Automatically", isOn: $store.automaticSyncEnabled)
                Button("Sync Now", systemImage: "arrow.triangle.2.circlepath.icloud") {
                    Task { await store.sync(gameRoot: gameRoot, engineActive: engineActive) }
                }.disabled(engineActive)
                if !store.migrationReady {
                    Label("Waiting for your Vision Pro save", systemImage: "visionpro")
                    Text("This device will wait for your existing progress. Its new or bundled profiles will not replace your Vision Pro save.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                if store.hasConflicts {
                    Label("Choose which progress to continue", systemImage: "arrow.triangle.branch")
                    Text("Different versions are preserved below. Restoring one keeps a recovery copy of this device’s progress.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                if let date = store.lastChecked {
                    LabeledContent("Last checked", value: date.formatted(date: .abbreviated, time: .shortened))
                }
                if store.isBusy { ProgressView() }
                Text(pickerError ?? store.status).font(.footnote).foregroundStyle(.secondary)
            }
            Section("Files fallback") {
                if let name = store.folderName { Label(name, systemImage: "folder") }
                Button("Choose a shared folder") { chooseFolder = true }
                if store.folderName != nil {
                    Button("Refresh snapshots", systemImage: "arrow.clockwise") { Task { await store.refresh() } }
                    Button("Back up this device", systemImage: "icloud.and.arrow.up") {
                        Task { await store.backup(gameRoot: gameRoot, engineActive: engineActive) }
                    }.disabled(engineActive || !store.migrationReady)
                    Button("Use automatic iCloud folder") { store.disconnect() }
                }
                Text("If automatic iCloud is unavailable, choose the same iCloud Drive folder on both devices. A folder stored only on this device does not provide cloud delivery.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            if engineActive {
                Section {
                    Label("Close and reopen to sync this session", systemImage: "arrow.clockwise.icloud")
                        .font(.headline)
                    Text("Saves can't sync while this app session has the game loaded. Close the app from the app switcher, reopen it, and let Cloud Saves finish before pressing Play or switching devices. Stopping the game or returning to the launcher isn't enough, and there is no live or checkpoint sync.")
                        .foregroundStyle(.secondary)
                }
            }
            if !store.snapshots.isEmpty {
                Section("Preserved cloud versions") {
                    ForEach(store.snapshots) { snapshot in snapshotRow(snapshot) }
                }
            }
            if !store.recoverySnapshots.isEmpty {
                Section("Local recovery copies") {
                    Text("These include saves preserved before playing and before each restore. They stay on this device and can be restored here.")
                        .font(.footnote).foregroundStyle(.secondary)
                    ForEach(store.recoverySnapshots) { snapshot in snapshotRow(snapshot) }
                }
            }
            Section {
                Text("Snapshots are immutable and include verified checksums. Cloud transfer can take time; a completed local backup does not prove another device has received it.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .navigationTitle("Cloud Saves")
        .disabled(store.isBusy)
        .fileImporter(isPresented: $chooseFolder, allowedContentTypes: [.folder]) { result in
            switch result {
            case .success(let url):
                pickerError = nil
                Task { await store.connect(folder: url); await store.refresh() }
            case .failure(let error): pickerError = error.localizedDescription
            }
        }
        .confirmationDialog("Restore this snapshot?", isPresented: Binding(
            get: { selectedRestore != nil }, set: { if !$0 { selectedRestore = nil } }
        ), titleVisibility: .visible) {
            if let selected = selectedRestore {
                Button("Restore selected snapshot", role: .destructive) {
                    Task { await store.restore(snapshot: selected, gameRoot: gameRoot, engineActive: engineActive) }
                    selectedRestore = nil
                }
            }
            Button("Cancel", role: .cancel) { selectedRestore = nil }
        } message: {
            Text("This replaces this device's Halo profiles and campaign progress with the selected version. A local recovery copy of the current profiles is kept first. Game files and cloud snapshots are preserved.")
        }
        .task { await store.refresh() }
    }

    private func snapshotRow(_ snapshot: EngineSaveSnapshotSummary) -> some View {
        Button {
            selectedRestore = snapshot
        } label: {
            VStack(alignment: .leading, spacing: 4) {
                Text(snapshot.created.formatted(date: .abbreviated, time: .shortened))
                    .font(.headline)
                Text("\(snapshot.fileCount) files · \(ByteCountFormatter.string(fromByteCount: Int64(snapshot.bytes), countStyle: .file))")
                    .font(.subheadline).foregroundStyle(.secondary)
                Text(snapshot.device).font(.caption).foregroundStyle(.secondary)
            }
        }.disabled(engineActive)
    }
}
