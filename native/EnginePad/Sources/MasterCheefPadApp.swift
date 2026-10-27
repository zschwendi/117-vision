import SwiftUI
import UniformTypeIdentifiers

/// Visible product name only. Bundle ID, iCloud container, save folders and
/// defaults keys stay technical.
enum EnginePadBrand {
    static let productName = EngineLibraryTheme.productName
    /// Shown whenever this process has started Halo: native STOPPED is not
    /// quiescent, so saves sync only after a full close and reopen.
    static let reopenToSync = "Close and reopen to sync this session"
}

@main
struct MasterCheefPadApp: App {
    @StateObject private var assets = EngineAssetStore()
    @StateObject private var engine = EnginePadModel()
    @StateObject private var cloudSaves = EngineCloudSaveStore()
    /// Before the settings sheet or the launch plan reads the saved choices.
    init() {
        if EngineLaunchPreferences.migrate(.pad).applied { NSLog("[launch-graphics] Native at the display maximum applied once") }
    }
    var body: some Scene {
        WindowGroup {
            EnginePadLibraryView(assets: assets, engine: engine, cloudSaves: cloudSaves, input: engine.input)
                .preferredColorScheme(.dark)
                .tint(EngineLibraryTheme.accent)
        }
    }
}

private struct EnginePadLibraryView: View {
    @Environment(\.scenePhase) private var scenePhase
    @Environment(\.verticalSizeClass) private var verticalSizeClass
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EnginePadModel
    @ObservedObject var cloudSaves: EngineCloudSaveStore
    @ObservedObject var input: EnginePadInputRouter
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared
    @State private var showingImport = false
    @State private var showingSettings = false
    @State private var showingGame = false
    @State private var showingGameSettings = false
    @State private var showingDataStatus = false
    @State private var showingCloudSaves = false
    @State private var preparingPlay = false
    @State private var importError: String?
    private let accent = EngineLibraryTheme.accent
    private let deviceName = UIDevice.current.localizedModel
    private let productName = EnginePadBrand.productName
    /// iPhone landscape: the card's Play status must stay above the fold.
    private var compact: Bool { verticalSizeClass == .compact }

    /// Any app surface that can take controller focus away from Halo.
    private var appSurfaceVisible: Bool {
        showingSettings || showingCloudSaves || showingImport || showingDataStatus
            || showingGameSettings || importError != nil || artwork.isImporterPresented || artwork.errorMessage != nil
    }
    private var uiOwnsInput: Bool {
        EnginePadInputLease.uiOwnsInput(background: scenePhase == .background,
                                        gameVisible: showingGame, appSurfaceVisible: appSurfaceVisible)
    }

    private var canPlay: Bool {
        assets.phase == .ready && (engine.canStart || engine.isActive) && !cloudSaves.isBusy && !preparingPlay
    }

    var body: some View {
        NavigationStack {
            GeometryReader { proxy in
                ScrollView {
                    Group {
                        if compact { compactShelf(in: proxy.size) } else { regularShelf(in: proxy.size) }
                    }
                    .frame(maxWidth: .infinity, minHeight: proxy.size.height)
                }
                .scrollIndicators(.hidden)
                .scrollBounceBehavior(.basedOnSize)
            }
            .background { EngineLibraryBackground().ignoresSafeArea() }
            .safeAreaInset(edge: .bottom, spacing: 0) {
                Text("Build \(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "—")")
                    .font(.caption2).foregroundStyle(.secondary)
                    .accessibilityIdentifier("launcher.buildNumber")
                    .frame(maxWidth: .infinity).padding(.vertical, 8)
                    .background(EngineLibraryTheme.background.opacity(0.92))
            }
            .navigationTitle("Library")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .principal) { libraryMenu }
            }
        }
        .libraryArtworkImporter()
        .sheet(isPresented: $showingSettings) { settings }
        .sheet(isPresented: $showingCloudSaves) {
            NavigationStack {
                EngineCloudSavesView(store: cloudSaves, gameRoot: assets.rootURL,
                                    engineActive: !engine.canStart || assets.phase != .ready)
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) {
                            Button("Done") { showingCloudSaves = false }
                        }
                    }
            }
        }
        .onChange(of: assets.phase, initial: true) { _, phase in
            guard phase == .ready else { return }
            syncIfIdle()
        }
        .onChange(of: scenePhase) { _, phase in
            guard phase == .active else { return }
            startMonitoringIfIdle()   // retries if iCloud was unavailable
            syncIfIdle()
        }
        .onReceive(cloudSaves.cloudChanges) { _ in syncIfIdle() }
        .onChange(of: uiOwnsInput, initial: true) { _, owned in engine.uiOwnsInput = owned }
        // Keyboard/mouse/touch: only an active scene (not .inactive) and the
        // visible game with no app surface over it own that input.
        .onChange(of: scenePhase, initial: true) { _, phase in input.update(sceneActive: phase == .active) }
        .onChange(of: showingGame && !appSurfaceVisible, initial: true) { _, visible in input.update(gameVisible: visible) }
        .task {
            bindRuntimeGate()
            startMonitoringIfIdle()
        }
        // A UIKit full-screen container (not fullScreenCover) so the game can
        // request pointer lock; the content below is unchanged SwiftUI.
        .background(EnginePadGamePresenter(isPresented: showingGame, input: input) {
            ZStack(alignment: .topTrailing) {
                Color.black.ignoresSafeArea()
                EngineFrameView().ignoresSafeArea()
                EnginePadInputSurfaceView(router: input).ignoresSafeArea()
                if input.touchControlsEnabled {
                    EnginePadTouchOverlay(input: input).ignoresSafeArea()
                }
                HStack {
                    Button { showingGame = false } label: { Label("Library", systemImage: "square.grid.2x2") }
                    Button { showingGameSettings = true } label: { Label("Settings", systemImage: "slider.horizontal.3") }
                    if engine.state == ENGINEVISION_RUNNING {
                        // Halo's Escape (pause/back) for keyboards without an Esc key, and touch.
                        Button { input.sendPause() } label: { Label("Pause", systemImage: "pause") }
                    }
                    if input.mouseConnected || input.pointerSeen {
                        Button { input.togglePointerCapture() } label: {
                            Label(input.pointerReleased ? "Capture pointer" : "Release pointer",
                                  systemImage: input.pointerReleased ? "cursorarrow.rays" : "cursorarrow")
                        }
                        .accessibilityHint("Command-L also releases or captures the pointer")
                    }
                }
                // iPhone pillarbox margins are narrow; icons keep the 4:3 picture clear.
                .labelStyle(EnginePadOverlayLabelStyle(iconOnly: compact))
                .buttonStyle(.bordered).padding(compact ? 8 : 16).background(.ultraThinMaterial, in: Capsule())
                .padding(compact ? 8 : 16)
                if engine.state == ENGINEVISION_STARTING {
                    ProgressView("Starting Halo…").padding(24).background(.regularMaterial, in: RoundedRectangle(cornerRadius: 18))
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                if engine.state == ENGINEVISION_FAILED || engine.state == ENGINEVISION_STOPPED {
                    // Centered when it fits; scrolls only in a short iPhone window.
                    ViewThatFits(in: .vertical) {
                        stoppedCard
                        ScrollView { stoppedCard }.scrollBounceBehavior(.basedOnSize)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .persistentSystemOverlays(.hidden)
            .sheet(isPresented: $showingGameSettings) { settings }
            .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
            .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
            .preferredColorScheme(.dark)
            .tint(EngineLibraryTheme.accent)
        })
        .fileImporter(isPresented: $showingImport, allowedContentTypes: [.folder]) { result in
            guard !cloudSaves.isBusy, engine.canStart else { return }
            switch result { case .success(let url): assets.importFolder(url); case .failure(let error): importError = error.localizedDescription }
        }
        .alert("Import failed", isPresented: Binding(get: { importError != nil }, set: { if !$0 { importError = nil } })) {
            Button("OK", role: .cancel) {}
        } message: { Text(importError ?? "") }
        .alert("Game data", isPresented: $showingDataStatus) {
            Button("OK", role: .cancel) {}
        } message: {
            if !engine.canStart && !engine.isActive {
                Text("\(engine.status)\n\n\(EnginePadBrand.reopenToSync). Saves sync before Play in a freshly opened app.")
            } else {
                Text(assets.detail.replacingOccurrences(of: "headset", with: deviceName))
            }
        }
    }

    private var stoppedCard: some View {
        VStack(spacing: compact ? 10 : 16) {
            Image(systemName: engine.state == ENGINEVISION_FAILED ? "exclamationmark.triangle" : "pause.circle")
                .font(.largeTitle).foregroundStyle(accent)
            Text(engine.state == ENGINEVISION_FAILED ? "Halo stopped unexpectedly" : "Halo has stopped")
                .font(.title2.bold())
            Text(engine.status).font(.callout).foregroundStyle(.secondary)
                .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)
            Text("\(EnginePadBrand.reopenToSync) and start a new one.")
                .font(.footnote).foregroundStyle(.secondary)
            Button("Return to Library") { showingGame = false }
                .buttonStyle(.borderedProminent).controlSize(.large)
        }
        .padding(compact ? 18 : 28).frame(maxWidth: 520)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 24))
        .padding(compact ? 12 : 32)
    }

    private var libraryMenu: some View {
        Menu {
            EngineLibraryArtworkActions()
            Button("Settings", systemImage: "slider.horizontal.3") { showingSettings = true }
            Button("Cloud Saves", systemImage: "icloud") { showingCloudSaves = true }
            Button("Game data", systemImage: "externaldrive") { showingDataStatus = true }
            if !assets.hasBundledPayload {
                Button("Import Halo PC folder…", systemImage: "folder.badge.plus") { showingImport = true }
                    .disabled(assets.phase == .importing || assets.phase == .checking || !engine.canStart || cloudSaves.isBusy)
            } else if case .failed = assets.phase {
                Button("Retry game setup", systemImage: "arrow.clockwise") {
                    guard engine.canStart, !cloudSaves.isBusy else { return }
                    assets.prepareBundledGame()
                }
                .disabled(!engine.canStart || cloudSaves.isBusy)
            }
        } label: {
            EngineLibraryTitleLabel()
        }
        .menuStyle(.button)
        .buttonStyle(.plain)
        .accessibilityLabel("\(productName) menu")
    }

    /// iPad (any orientation) and regular windows: one centered shelf, the
    /// Dark Eco library arrangement for a single title.
    private func regularShelf(in size: CGSize) -> some View {
        VStack(spacing: 22) {
            gameCard(in: size)
            shelfNotice
        }
        .padding(.horizontal, 32)
        .padding(.vertical, 24)
    }

    /// Compact-height iPhone landscape: cover beside its notice, both centered.
    private func compactShelf(in size: CGSize) -> some View {
        HStack(alignment: .center, spacing: 28) {
            gameCard(in: size)
            shelfNotice
                .frame(maxWidth: 340, alignment: .leading)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 10)
    }

    private func gameCard(in size: CGSize) -> some View {
        EngineLibraryGameCard(
            coverWidth: coverWidth(in: size),
            accessibilityLabel: shelfAccessibilityLabel,
            enabled: !cloudSaves.isBusy && !preparingPlay,
            action: openGame
        ) {
            shelfStatus
        }
    }

    private func coverWidth(in size: CGSize) -> CGFloat {
        // Keep the same cover shelf in a tall window, landscape, or Split View.
        // Short windows scroll instead of clipping controls or reducing tap areas.
        // Compact height sits beside the menu: cover, title and status fit.
        // The navigation bar is already outside `size`; reserve the card footer.
        if compact { return min(220, max(110, (size.height - 96) / 1.5)) }
        return min(300, max(155, min(size.width - 80, (size.height - 170) / 1.5)))
    }

    /// Install before any save operation; never rely on view appearance order.
    private func bindRuntimeGate() {
        cloudSaves.engineActivityCheck = { [weak assets = assets] in
            guard let assets, assets.phase == .ready else { return true }
            return enginevision_runtime_state() != ENGINEVISION_IDLE
        }
    }

    /// Launch, foreground and late iCloud arrivals reconcile only before this
    /// process starts Halo. Stop/Library is not quiescent: reopen instead.
    private func startMonitoringIfIdle() {
        guard enginevision_runtime_state() == ENGINEVISION_IDLE else { return }
        cloudSaves.startCloudMonitoring()
    }

    private func syncIfIdle() {
        bindRuntimeGate()
        guard scenePhase == .active, assets.phase == .ready, !preparingPlay,
              enginevision_runtime_state() == ENGINEVISION_IDLE else { return }
        Task { await cloudSaves.sync(gameRoot: assets.rootURL, engineActive: false) }
    }

    private var shelfAccessibilityLabel: String {
        let game = "Halo: Combat Evolved."
        if cloudSaves.isBusy || preparingPlay { return "\(game) Checking cloud saves. Unavailable until finished." }
        if assets.phase == .ready && !cloudSaves.migrationReady {
            return "\(game) Waiting for your Vision Pro save. Open Cloud Saves."
        }
        if engine.isActive {
            return "\(game) \(engine.state == ENGINEVISION_STARTING ? "Starting Halo." : "Game running.") Return to game. \(EnginePadBrand.reopenToSync) after playing."
        }
        if !engine.canStart {
            return "\(game) \(engine.state == ENGINEVISION_FAILED ? "Game failed to start." : "Game stopped.") \(EnginePadBrand.reopenToSync). Open details."
        }
        switch assets.phase {
        case .ready: return "\(game) Ready to play. Play."
        case .checking: return "\(game) Checking game data. Open setup details."
        case .importing: return "\(game) Preparing game data. Open setup details."
        case .missing:
            return "\(game) Game data missing. \(assets.hasBundledPayload ? "Open setup details." : "Import Halo PC folder.")"
        case .failed: return "\(game) Game data setup failed. Open setup details."
        }
    }

    @ViewBuilder private var shelfStatus: some View {
        if cloudSaves.isBusy || preparingPlay {
            Label("Checking cloud saves…", systemImage: "icloud")
        } else if assets.phase == .ready && !cloudSaves.migrationReady {
            Label("Waiting for Vision Pro save", systemImage: "icloud.and.arrow.down")
        } else if engine.isActive {
            Label("Return to game", systemImage: "play.fill")
        } else if !engine.canStart {
            Label("Restart app to play", systemImage: "arrow.clockwise")
        } else {
            switch assets.phase {
            case .ready:
                Label("Play", systemImage: "play.fill")
            case .checking:
                Label("Checking game data…", systemImage: "hourglass")
            case .importing:
                VStack(alignment: .leading, spacing: 6) {
                    Text("Preparing game data…")
                    ProgressView(value: Double(assets.copied), total: Double(max(1, assets.total)))
                        .tint(accent)
                }
            case .missing:
                Label("Import game data to play", systemImage: "folder.badge.plus")
            case .failed:
                Label("Game data needs attention", systemImage: "exclamationmark.circle")
            }
        }
    }

    @ViewBuilder private var shelfNotice: some View {
        if assets.phase == .ready && !cloudSaves.migrationReady {
            VStack(spacing: 10) {
                Text("Your Vision Pro campaign comes first.")
                    .font(.callout)
                Text(cloudSaves.status)
                    .font(.footnote).foregroundStyle(.secondary)
                Button("Cloud Saves", systemImage: "icloud") { showingCloudSaves = true }
                    .buttonStyle(.bordered)
            }
            .multilineTextAlignment(.center).frame(maxWidth: 400)
        } else if engine.state != ENGINEVISION_IDLE {
            // Takes precedence over the controller hint: once Halo has run in
            // this process, saves wait for a fresh launch.
            VStack(spacing: 6) {
                Label(EnginePadBrand.reopenToSync, systemImage: "arrow.clockwise.icloud")
                    .font(.footnote.weight(.semibold))
                if engine.state == ENGINEVISION_FAILED {
                    Text(engine.status)
                }
                Text(engine.isActive
                     ? "Halo is still running. Saves sync only before Play in a freshly opened app."
                     : "Halo may still be writing saves. Close \(productName) from the app switcher, reopen it, and wait for Cloud Saves before switching devices.")
                    .foregroundStyle(.secondary)
            }
            .font(.footnote)
            .multilineTextAlignment(.center).frame(maxWidth: 400)
        } else if assets.phase == .ready && engine.controllerName == nil {
            // Play never requires a controller; say what is actually connected.
            Label(EnginePadInputPolicy.launcherSummary(keyboard: input.keyboardConnected,
                                                       pointer: input.mouseConnected || input.pointerSeen),
                  systemImage: input.keyboardConnected ? "keyboard" : "hand.tap")
                .font(.footnote).foregroundStyle(.secondary)
                .multilineTextAlignment(.center).frame(maxWidth: 360)
        }
    }

    private var settings: some View {
        EnginePadSettingsView(assets: assets, engine: engine, cloudSaves: cloudSaves)
    }
    private func openGame() {
        guard !cloudSaves.isBusy, !preparingPlay else { return }
        if assets.phase == .ready && !cloudSaves.migrationReady {
            showingCloudSaves = true
            return
        }
        if canPlay {
            play()
        } else if assets.phase == .missing && !assets.hasBundledPayload && engine.canStart {
            showingImport = true
        } else {
            showingDataStatus = true
        }
    }

    private func play() {
        guard canPlay else { return }
        if engine.isActive { showingGame = true; return }
        bindRuntimeGate()
        preparingPlay = true
        Task { @MainActor in
            defer { preparingPlay = false }
            let allowed = await cloudSaves.prepareForPlay(gameRoot: assets.rootURL, engineActive: false)
            engine.refresh()
            guard allowed, cloudSaves.migrationReady, !cloudSaves.isBusy,
                  assets.phase == .ready, engine.canStart,
                  enginevision_runtime_state() == ENGINEVISION_IDLE else {
                showingCloudSaves = true
                return
            }
            // Any start attempt ends reconciliation for this process.
            cloudSaves.stopCloudMonitoring()
            engine.start(root: assets.rootURL)
            if engine.isActive { showingGame = true }
        }
    }
}

private struct EnginePadOverlayLabelStyle: LabelStyle {
    let iconOnly: Bool
    func makeBody(configuration: Configuration) -> some View {
        if iconOnly { Label(configuration).labelStyle(.iconOnly) }
        else { Label(configuration).labelStyle(.titleAndIcon) }
    }
}
