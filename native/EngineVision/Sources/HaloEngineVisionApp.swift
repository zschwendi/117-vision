import SwiftUI
import CompositorServices
import GameController
import Combine

let haloBuildLabel = "Build \(Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "—")"

@main
struct HaloEngineVisionApp: App {
    @StateObject private var session = EngineAppSession()
    @Environment(\.scenePhase) private var scenePhase

    /// Before SwiftUI creates the session, whose settings models read the
    /// saved presentation, and long before the engine reads launch graphics.
    init() {
        let outcome = EngineLaunchPreferences.migrate(.vision)
        if outcome.applied { NSLog("[launch-graphics] highest settings applied once; presentation: %@", outcome.presentation.rawValue) }
    }

    var body: some Scene {
        WindowGroup(EngineLibraryTheme.productName, id: EngineAppSession.libraryWindowID) {
            EngineContentView(session: session, assets: session.assets, engine: session.engine, immersive: session.immersive)
                .modifier(EngineSceneActions(session: session))
        }
        .defaultSize(width: 980, height: 700)
        .defaultLaunchBehavior(.presented)
        .restorationBehavior(.disabled)
        .onChange(of: scenePhase, initial: true) { _, phase in
            EngineAppControllerNavigation.shared.setApplicationActive(phase == .active)
            EngineImmersiveTrace.shared.record("app.scenePhase: \(phase)")
            session.engine.diagnostics.flush(reason: "scenePhase: \(phase)", wait: phase == .background)
            session.setApplicationActive(phase == .active)
        }

        ImmersiveSpace(id: EngineImmersiveSession.spaceID) {
            CompositorLayer(configuration: EngineImmersiveLayerConfiguration()) { layer in session.startImmersive(layer) }
        }
        .immersionStyle(selection: .constant(.full), in: .full)
        .upperLimbVisibility(session.senseSettings.showHandsAndControllers ? .visible : .hidden)
        .defaultLaunchBehavior(.suppressed)

        // The game menu opens only when asked for: the Settings affordance,
        // the launcher menu, or the deliberate Sense Create + Options chord.
        Window("Halo Menu", id: EngineAppSession.controlsWindowID) {
            EngineGameMenuView(session: session, engine: session.engine, immersive: session.immersive)
                .modifier(EngineSceneActions(session: session))
        }
        .defaultSize(width: 440, height: 660)
        .windowResizability(.contentSize)
        .defaultLaunchBehavior(.suppressed)
        .restorationBehavior(.disabled)
        .defaultWindowPlacement { _, _ in WindowPlacement(.utilityPanel) }

        // Tiny immersive-only affordance, shown while Halo's own menu is up.
        Window("Halo Settings Button", id: EngineAppSession.pauseWindowID) {
            EnginePauseAffordancePanel(session: session)
                .modifier(EngineSceneActions(session: session))
        }
        .windowStyle(.plain)
        .windowResizability(.contentSize)
        .defaultLaunchBehavior(.suppressed)
        .restorationBehavior(.disabled)
        .defaultWindowPlacement { _, _ in WindowPlacement(.utilityPanel) }

        Window("Halo Settings", id: EngineAppSession.settingsWindowID) {
            EngineSettingsView(session: session)
                .handlesGameControllerEvents(matching: .gamepad)
                .modifier(EngineSceneActions(session: session))
        }
        .defaultSize(width: 620, height: 820)
        .defaultLaunchBehavior(.suppressed)
        .restorationBehavior(.disabled)
    }
}

enum EngineDisplayMode: String, CaseIterable, Identifiable {
    case window, immersive
    var id: Self { self }
    var label: String { self == .window ? "Window" : "Immersive" }
    var symbol: String { self == .window ? "macwindow" : "visionpro" }
}

enum EnginePresentationDestination { case library, window, immersive }

/// Actions are installed by a visible scene and retained by the app session.
/// Async presentation work survives dismissal of the launcher and always
/// passes through one serialized owner.
@MainActor
final class EngineAppSession: ObservableObject {
    static let libraryWindowID = "HaloSetup"
    static let controlsWindowID = "HaloControls"
    static let settingsWindowID = "HaloSettings"
    static let pauseWindowID = "HaloPause"

    struct SceneActions {
        let openSpace: () async -> OpenImmersiveSpaceAction.Result
        let dismissSpace: () async -> Void
        let openWindow: (String) -> Void
        let dismissWindow: (String) -> Void
    }

    let assets = EngineAssetStore()
    let engine = EngineRuntimeModel()
    let immersive = EngineImmersiveSession()
    let settings = EngineSettingsModel()
    let graphics = EngineGraphicsQualityModel()
    let senseSettings = EngineSenseSettingsModel()
    let physicalSettings = EngineSensePhysicalSettingsModel.shared /* applies optional physical settings at launch */
    let cloudSaves = EngineCloudSaveStore()
    @Published private(set) var immersiveActive = false
    @Published private(set) var destination: EnginePresentationDestination = .library
    @Published private(set) var presentationBusy = false
    @Published private(set) var preparingPlay = false
    @Published var presentationMessage: String?
    /// True while Halo has a genuine in-game menu up and the small Settings affordance applies.
    @Published private(set) var pauseAffordanceVisible = false
    @Published private(set) var quitPhase = EngineQuitFlow.Phase.idle
    private var timer: Timer?
    private var immersiveWatch: AnyCancellable?
    private var closeWatch: AnyCancellable?
    private var actions: SceneActions?
    private var pendingDestination: EnginePresentationDestination?
    private var pendingCloseGeneration: UInt64?
    private var presentationTask: Task<Void, Never>?
    private var childChanges = Set<AnyCancellable>()
    private var quit = EngineQuitFlow()
    private var pendingQuit = false
    private var pause = EnginePauseAffordance()
    private var pausePanelRequested = false
    private var visibleAppMenus = Set<String>()
    /// The app scene is active (foreground). Cloud reconciliation and the
    /// passive Settings affordance act only while it is.
    private var applicationActive = false

    init() {
        enginevision_prepare_controller()
        // The Sense chord remains a deliberate shortcut to the game menu.
        EngineAppControllerNavigation.shared.onOpenControls = { [weak self] in
            guard let self, !self.quit.blocksPresentation else { return }
            self.showControls()
        }
        EngineAppControllerNavigation.shared.start()
        cloudSaves.engineActivityCheck = { [weak self] in
            guard let self, self.assets.phase == .ready else { return true }
            return enginevision_runtime_state() != ENGINEVISION_IDLE
        }
        cloudSaves.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &childChanges)
        // Another device changed the cloud save: reconcile only before Halo
        // has ever started, while the app is active and game data is ready.
        cloudSaves.cloudChanges.sink { [weak self] in
            guard let self, self.applicationActive else { return }
            self.syncSavesIfIdle()
        }.store(in: &childChanges)
        // Scene visibility must react when the in-game settings toggle changes.
        senseSettings.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &childChanges)
        assets.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &childChanges)
        assets.$phase.removeDuplicates().sink { [weak self] phase in
            guard phase == .ready else { return }
            Task { @MainActor [weak self] in
                guard let self, self.assets.phase == .ready,
                      enginevision_runtime_state() == ENGINEVISION_IDLE else { return }
                await self.cloudSaves.sync(gameRoot: self.assets.rootURL, engineActive: false)
            }
        }.store(in: &childChanges)
        engine.objectWillChange.sink { [weak self] _ in self?.objectWillChange.send() }.store(in: &childChanges)
        immersiveWatch = immersive.$active.removeDuplicates().sink { [weak self] active in
            self?.immersiveActive = active
        }
        closeWatch = immersive.$closeRequest.dropFirst().sink { [weak self] _ in
            guard let self else { return }
            let generation = self.immersive.closeGeneration
            guard self.immersive.canDismissClosedSession(generation) else { return }
            self.pendingCloseGeneration = generation
            self.drainPresentationRequests()
        }
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
    }
    deinit { timer?.invalidate() }

    var canPlay: Bool {
        assets.phase == .ready && (engine.canStart || engine.isActive)
            && !presentationBusy && !preparingPlay && !immersive.opening && !cloudSaves.isBusy
            && !quit.blocksPresentation
    }

    /// Where Halo is visible right now, for the in-game Settings affordance.
    var gameSurface: EnginePauseAffordance.Surface {
        if destination == .immersive && immersive.active { return .immersive }
        return destination == .window ? .window : .none
    }

    /// Quit is offered only for a live engine with no transition in flight.
    var canQuit: Bool {
        quitPhase == .idle && EngineQuitFlow.canBegin(engineActive: engine.isActive, presentationBusy: presentationBusy,
                                                      preparingPlay: preparingPlay, saveBusy: cloudSaves.isBusy)
    }
    var quitMessage: String? { quit.message(appName: EngineLibraryTheme.productName) }
    /// Once Quit begins, nothing can resume or reopen Halo in this process.
    var quitBlocksPresentation: Bool { quitPhase != .idle }

    func bindSceneActions(_ actions: SceneActions) {
        self.actions = actions
        drainPresentationRequests()
    }

    /// Foreground and launch: (re)start cloud monitoring while the engine has
    /// never run, then reconcile. Monitoring is never stopped on background;
    /// it is retired permanently only just before the engine starts.
    func setApplicationActive(_ active: Bool) {
        applicationActive = active
        if active && enginevision_runtime_state() == ENGINEVISION_IDLE {
            cloudSaves.startCloudMonitoring()
            syncSavesIfIdle()
        }
        reconcilePauseAffordance()
    }

    func syncSavesIfIdle() {
        guard applicationActive, assets.phase == .ready, !preparingPlay,
              enginevision_runtime_state() == ENGINEVISION_IDLE else { return }
        Task { @MainActor [weak self] in
            guard let self, self.applicationActive, self.assets.phase == .ready, !self.preparingPlay,
                  enginevision_runtime_state() == ENGINEVISION_IDLE else { return }
            await self.cloudSaves.sync(gameRoot: self.assets.rootURL, engineActive: false)
        }
    }

    func play(in mode: EngineDisplayMode) {
        EngineImmersiveTrace.shared.record("play.request:\(mode.rawValue) savesBusy=\(cloudSaves.isBusy)")
        // A deliberate launch owns the next startup even if an automatic save
        // check is still finishing. prepareForPlay waits for that owner, then
        // rechecks the existing save/migration/conflict safety gates.
        guard assets.phase == .ready, engine.canStart || engine.isActive,
              !presentationBusy, !preparingPlay, !immersive.opening,
              !quit.blocksPresentation else {
            EngineImmersiveTrace.shared.record("play.rejected: transition or game not ready", state: UInt32(bitPattern: engine.state))
            if !preparingPlay && !presentationBusy {
                presentationMessage = quitMessage ?? "Halo is not ready to start. Open 117 → Game Data for details."
            }
            return
        }
        presentationMessage = nil
        if engine.isActive {
            request(mode == .window ? .window : .immersive)
            return
        }
        preparingPlay = true
        if cloudSaves.isBusy {
            presentationMessage = "Checking your saves before starting Halo…"
            EngineImmersiveTrace.shared.record("play.waitingForSaveCheck")
        }
        Task { @MainActor [weak self] in
            guard let self else { return }
            defer { self.preparingPlay = false }
            let allowed = await self.cloudSaves.prepareForPlay(gameRoot: self.assets.rootURL, engineActive: false)
            self.engine.refresh()
            guard allowed, self.applicationActive, !self.cloudSaves.isBusy, self.assets.phase == .ready,
                  self.engine.canStart, enginevision_runtime_state() == ENGINEVISION_IDLE else {
                self.presentationMessage = self.applicationActive ? self.cloudSaves.status : "Return to 117 and select Halo to play."
                EngineImmersiveTrace.shared.record("play.preparationStopped allowed=\(allowed) active=\(self.applicationActive) savesBusy=\(self.cloudSaves.isBusy)", state: UInt32(bitPattern: self.engine.state))
                return
            }
            // The cloud watcher is retired for the rest of this process just
            // before the engine starts; saves then sync on the next launch.
            self.cloudSaves.stopCloudMonitoring()
            self.presentationMessage = nil
            EngineImmersiveTrace.shared.record("play.startingEngine")
            self.engine.start(root: self.assets.rootURL)
            guard self.engine.isActive else {
                self.presentationMessage = self.engine.status
                EngineImmersiveTrace.shared.record("play.engineDidNotStart", state: UInt32(bitPattern: self.engine.state))
                return
            }
            EngineImmersiveTrace.shared.record("play.presenting:\(mode.rawValue)")
            self.request(mode == .window ? .window : .immersive)
        }
    }

    func request(_ destination: EnginePresentationDestination) {
        guard !quit.blocksPresentation else { return }
        pendingDestination = destination
        drainPresentationRequests()
    }

    /// Terminal Quit Game. The original engine cannot be restarted in this
    /// process, so this closes the game view through the serialized owner,
    /// then requests the engine stop. Saves on disk are left untouched; the
    /// user is told to close and reopen the app to play again.
    func quitGame() {
        guard quit.begin(engineActive: engine.isActive, presentationBusy: presentationBusy,
                         preparingPlay: preparingPlay, saveBusy: cloudSaves.isBusy) else { return }
        EngineImmersiveTrace.shared.record("app.quit.begin")
        quitPhase = quit.phase
        presentationMessage = nil
        pendingDestination = nil
        pendingQuit = true
        reconcilePauseAffordance()
        drainPresentationRequests()
    }

    /// The game menu replaces the small affordance while it is visible.
    func setAppMenuVisible(_ id: String, _ visible: Bool) {
        if visible { visibleAppMenus.insert(id) } else { visibleAppMenus.remove(id) }
        reconcilePauseAffordance()
    }

    /// The user closed the immersive Settings affordance themselves.
    func pausePanelDisappeared() {
        guard pausePanelRequested else { return }
        pausePanelRequested = false
        pause.userDismissed()
        reconcilePauseAffordance()
    }

    func showSettings() { actions?.openWindow(Self.settingsWindowID) }
    func showControls() {
        guard !quit.blocksPresentation else { return }
        actions?.openWindow(Self.controlsWindowID)
    }
    func hideSettings() { actions?.dismissWindow(Self.settingsWindowID) }
    func hideControls() { actions?.dismissWindow(Self.controlsWindowID) }

    func startImmersive(_ layer: LayerRenderer) {
        refresh()
        immersive.start(layer: layer)
    }

    private func drainPresentationRequests() {
        guard actions != nil, presentationTask == nil,
              pendingDestination != nil || pendingCloseGeneration != nil || pendingQuit else { return }
        presentationBusy = true
        presentationTask = Task { [weak self] in
            guard let self else { return }
            defer {
                self.presentationBusy = false
                self.presentationTask = nil
                self.drainPresentationRequests()
            }
            while let actions = self.actions {
                if let generation = self.pendingCloseGeneration {
                    self.pendingCloseGeneration = nil
                    await self.dismissClosedSession(generation, using: actions)
                    continue
                }
                if let destination = self.pendingDestination {
                    self.pendingDestination = nil
                    await self.present(destination, using: actions)
                    continue
                }
                guard self.pendingQuit else { break }
                self.pendingQuit = false
                await self.closeForQuit(using: actions)
            }
        }
    }

    private func dismissClosedSession(_ generation: UInt64, using actions: SceneActions) async {
        guard immersive.canDismissClosedSession(generation) else { return }
        // visionOS cannot dismiss the final scene: establish the ordinary
        // window before cleaning up an invalidated or failed immersive space.
        destination = .library
        actions.openWindow(Self.libraryWindowID)
        EngineImmersiveTrace.shared.record("app.dismissImmersive.begin (closeRequest)")
        await actions.dismissSpace()
        EngineImmersiveTrace.shared.record("app.dismissImmersive.end (closeRequest)")
        guard immersive.canDismissClosedSession(generation) else { return }
        immersive.finish()
        actions.dismissWindow(Self.controlsWindowID)
        dismissPausePanel(using: actions)
        if !quit.blocksPresentation { presentationMessage = immersive.status }
    }

    /// Runs after any earlier presentation work in the same serialized task.
    private func closeForQuit(using actions: SceneActions) async {
        await present(.library, using: actions)
        dismissPausePanel(using: actions)
        guard quit.presentationClosed(now: ProcessInfo.processInfo.systemUptime) else { return }
        EngineImmersiveTrace.shared.record("app.quit.stopRequested")
        engine.stop()
        quit.observe(engineActive: engine.isActive, now: ProcessInfo.processInfo.systemUptime)
        quitPhase = quit.phase
    }

    /// Entering play closes app menus: while one is visible its controller
    /// lease keeps Halo's input neutral, which would look like a dead pad.
    private func dismissAppMenus(using actions: SceneActions) {
        actions.dismissWindow(Self.controlsWindowID)
        actions.dismissWindow(Self.settingsWindowID)
    }

    private func dismissPausePanel(using actions: SceneActions) {
        pausePanelRequested = false
        actions.dismissWindow(Self.pauseWindowID)
    }

    private func present(_ requested: EnginePresentationDestination, using actions: SceneActions) async {
        if requested == .immersive {
            if immersive.active {
                destination = .immersive
                dismissAppMenus(using: actions)
                actions.dismissWindow(Self.libraryWindowID)
                return
            }
            guard let generation = immersive.prepare() else { return }
            EngineImmersiveTrace.shared.record("app.openImmersive.begin")
            let result = await actions.openSpace()
            EngineImmersiveTrace.shared.record("app.openImmersive.end: \(result)")
            switch result {
            case .opened:
                // start(layer:) advances the renderer generation. It may
                // run before openSpace returns; that legitimate handoff
                // must not be mistaken for a stale open operation.
                let rendererGeneration = generation &+ 1
                guard immersive.ownsOperation(generation)
                    || (immersive.active && immersive.ownsOperation(rendererGeneration)) else { return }
                // No settings panel opens here; the small affordance follows
                // Halo's own menu state once frames are presented.
                destination = .immersive
                dismissAppMenus(using: actions)
                actions.dismissWindow(Self.libraryWindowID)
            case .userCancelled, .error:
                guard immersive.ownsOperation(generation) else { return }
                immersive.finish()
                presentationMessage = result == .userCancelled
                    ? "Immersive view was cancelled. You can continue in a window."
                    : "Immersive view couldn't open. You can continue in a window."
                destination = .library
                actions.openWindow(Self.libraryWindowID)
            @unknown default:
                guard immersive.ownsOperation(generation) else { return }
                immersive.finish()
                presentationMessage = "Immersive view couldn't open."
                destination = .library
                actions.openWindow(Self.libraryWindowID)
            }
            return
        }

        destination = requested
        actions.openWindow(Self.libraryWindowID)
        if immersive.active || immersive.opening {
            let generation = immersive.operationGeneration
            EngineImmersiveTrace.shared.record("app.dismissImmersive.begin (return)")
            await actions.dismissSpace()
            EngineImmersiveTrace.shared.record("app.dismissImmersive.end (return)")
            if immersive.ownsOperation(generation) { immersive.finish() }
        }
        if requested == .window { dismissAppMenus(using: actions) }
        else { actions.dismissWindow(Self.controlsWindowID) }
        dismissPausePanel(using: actions)
        // The original engine and its saves remain owned by this session.
        // Returning to the library never calls the terminal engine stop API.
    }

    private func refresh() {
        engine.diagnostics.launcherState = [
            "applicationActive": applicationActive, "sceneActionsBound": actions != nil,
            "assetsReady": assets.phase == .ready, "canPlay": canPlay,
            "preparingPlay": preparingPlay, "presentationBusy": presentationBusy,
            "savesBusy": cloudSaves.isBusy, "savesPermitPlay": cloudSaves.canPlay,
            "saveConflicts": cloudSaves.hasConflicts, "saveMigrationReady": cloudSaves.migrationReady,
            "saveStatus": cloudSaves.status
        ]
        engine.diagnostics.preparationStatus = assets.detail
        engine.diagnostics.preparationCopiedBytes = assets.copied
        engine.diagnostics.preparationTotalBytes = assets.total
        engine.diagnostics.immersiveStatus = immersive.status
        engine.diagnostics.immersiveActive = immersive.active
        engine.diagnostics.immersiveLayerState = immersive.layerStateForDiagnostics
        let frameCounts = immersive.frameCounts()
        engine.diagnostics.immersiveSubmittedFrames = frameCounts.submitted
        engine.diagnostics.immersiveCancelledFrames = frameCounts.cancelled
        engine.diagnostics.immersiveTrackingLossFrames = frameCounts.trackingLoss
        engine.diagnostics.immersiveGPUCompletedFrames = frameCounts.gpuCompleted
        engine.diagnostics.immersiveGPUFailedFrames = frameCounts.gpuFailed
        engine.diagnostics.immersiveGPUError = frameCounts.gpuError
        engine.diagnostics.immersiveGPUMilliseconds = frameCounts.gpuMilliseconds
        engine.diagnostics.immersiveGPUMillisecondsMax = frameCounts.gpuMillisecondsMax
        engine.diagnostics.immersiveConfiguration = frameCounts.configuration
        engine.diagnostics.immersiveSubmission = immersive.submissionInfo()
        switch assets.phase {
        case .checking:
            immersive.setLoading(title: "Preparing Halo", detail: assets.detail, progress: nil, show: true)
        case .importing:
            let fraction = assets.total > 0 ? Double(assets.copied) / Double(assets.total) : nil
            immersive.setLoading(title: "Preparing Halo", detail: assets.detail, progress: fraction, show: true)
        case .missing, .failed:
            immersive.setLoading(title: "Game data needs attention", detail: assets.detail, progress: nil, show: true)
        case .ready:
            let failed = engine.state == ENGINEVISION_FAILED || engine.state == ENGINEVISION_STOPPED
            let show = failed || engine.frameSequence == 0
            immersive.setLoading(title: failed ? "Halo stopped" : "Starting Halo",
                                 detail: failed ? engine.status : "Loading Halo. Use the left stick to navigate, then press Cross or A to select.",
                                 progress: nil, show: show)
        }
        if quit.phase == .stopping || quit.phase == .stillStopping {
            quit.observe(engineActive: engine.isActive, now: ProcessInfo.processInfo.systemUptime)
            if quitPhase != quit.phase {
                quitPhase = quit.phase
                if quit.phase == .stopped { EngineImmersiveTrace.shared.record("app.quit.stopped") }
            }
        }
        if quit.blocksPresentation, presentationMessage != quitMessage { presentationMessage = quitMessage }
        reconcilePauseAffordance()
    }

    /// Visibility comes from the engine's genuine in-game menu state (a
    /// pausing widget or a non-campaign UI root, never the front-end title
    /// shell), polled read-only. Only the small affordance is opened or closed
    /// here, never the settings menu, and never during a transition.
    private func reconcilePauseAffordance() {
        let surface = gameSurface
        let running = engine.isActive
        let inputs = EnginePauseAffordance.Inputs(
            surface: surface, engineRunning: running, hasFrame: engine.frameSequence > 0,
            menuActive: surface != .none && running && enginevision_pause_menu_active() != 0,
            transitionBusy: presentationBusy || immersive.opening, quitBlocked: quit.blocksPresentation,
            appMenuOpen: !visibleAppMenus.isEmpty, applicationActive: applicationActive)
        let visible = pause.update(inputs, now: ProcessInfo.processInfo.systemUptime)
        if pauseAffordanceVisible != visible { pauseAffordanceVisible = visible }
        guard let actions, presentationTask == nil else { return }
        let wantsPanel = visible && surface == .immersive
        guard wantsPanel != pausePanelRequested else { return }
        pausePanelRequested = wantsPanel
        if wantsPanel { actions.openWindow(Self.pauseWindowID) } else { actions.dismissWindow(Self.pauseWindowID) }
    }
}

/// Every scene supplies environment actions to the same operation owner.
/// The controls window can return to the library after its launcher closes.
struct EngineSceneActions: ViewModifier {
    let session: EngineAppSession
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow
    @Environment(\.openImmersiveSpace) private var openImmersiveSpace
    @Environment(\.dismissImmersiveSpace) private var dismissImmersiveSpace

    func body(content: Content) -> some View {
        content.onAppear {
            let openImmersiveSpace = openImmersiveSpace
            let dismissImmersiveSpace = dismissImmersiveSpace
            let openWindow = openWindow
            let dismissWindow = dismissWindow
            session.bindSceneActions(.init(
                openSpace: { await openImmersiveSpace(id: EngineImmersiveSession.spaceID) },
                dismissSpace: { await dismissImmersiveSpace() },
                openWindow: { openWindow(id: $0) },
                dismissWindow: { dismissWindow(id: $0) }
            ))
        }
    }
}
