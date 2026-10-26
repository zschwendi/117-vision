import SwiftUI
import UniformTypeIdentifiers
import GameController

private enum EngineLibrarySheet: String, Identifiable {
    case gameData, controller, cloudSaves, graphics, about
    var id: String { rawValue }
}

struct EngineContentView: View {
    @ObservedObject var session: EngineAppSession
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EngineRuntimeModel
    @ObservedObject var immersive: EngineImmersiveSession
    @ObservedObject private var controller = EngineAppControllerNavigation.shared
    @ObservedObject private var artwork = EngineLibraryArtworkStore.shared
    @AppStorage("HaloPreferredPresentation") private var preferredPresentation = EngineDisplayMode.immersive.rawValue
    @State private var showingImporter = false
    @State private var importAlert: String?
    @State private var activeSheet: EngineLibrarySheet?
    @State private var importAfterSheetDismissal = false

    private var displayMode: EngineDisplayMode { EngineDisplayMode(rawValue: preferredPresentation) ?? .immersive }

    var body: some View {
        NavigationStack {
            Group {
                if session.destination == .window { gameWindow }
                else { library }
            }
            .navigationTitle(session.destination == .window ? "Halo: Combat Evolved" : "Library")
            .toolbar { toolbar }
        }
        .libraryArtworkImporter()
        .tint(EngineLibraryTheme.accent)
        .preferredColorScheme(.dark)
        .handlesGameControllerEvents(matching: .gamepad)
        .ornament(visibility: topOrnamentVisible ? .visible : .hidden,
                  attachmentAnchor: .scene(.top), contentAlignment: .bottom) {
            HStack(spacing: 12) {
                if controllerMissing { EngineControllerStatusCue(presence: controller.presence) }
                if windowAffordanceVisible {
                    EngineSettingsAffordanceButton { session.showControls() }
                }
            }
            .padding(.bottom, 12)
            // Gamepad events keep reaching Halo while the ornament is shown.
            .handlesGameControllerEvents(matching: .gamepad)
        }
        .fileImporter(isPresented: $showingImporter, allowedContentTypes: [.folder], allowsMultipleSelection: false) { result in
            guard !session.cloudSaves.isBusy, engine.canStart else {
                importAlert = "Wait for save syncing to finish and close and reopen the app before replacing game data."
                return
            }
            do { if let folder = try result.get().first { assets.importFolder(folder) } }
            catch { importAlert = error.localizedDescription }
        }
        .alert("Game import failed", isPresented: Binding(get: { importAlert != nil }, set: { if !$0 { importAlert = nil } })) {
            Button("OK", role: .cancel) { importAlert = nil }
        } message: { Text(importAlert ?? "Unknown import failure") }
        .sheet(item: $activeSheet, onDismiss: {
            guard importAfterSheetDismissal else { return }
            importAfterSheetDismissal = false
            showingImporter = true
        }) { sheet in
            NavigationStack {
                Group {
                    switch sheet {
                    case .gameData: EngineGameDataView(assets: assets, engine: engine, saveOperationBusy: session.cloudSaves.isBusy, importGame: {
                        importAfterSheetDismissal = true
                        activeSheet = nil
                    })
                    case .controller: EngineControllerHelpView(engine: engine, senseSettings: session.senseSettings)
                    case .cloudSaves: EngineCloudSavesView(store: session.cloudSaves, gameRoot: assets.rootURL, engineActive: !engine.canStart || assets.phase != .ready)
                    case .graphics: EngineGraphicsModsView()
                    case .about: EngineAboutView()
                    }
                }
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { activeSheet = nil }.senseControllerTarget("sheet.done") } }
            }
            .senseControllerNavigation([.init(id: "sheet.done", title: "Done", activate: { activeSheet = nil })],
                                       back: { activeSheet = nil })
            .frame(width: 620, height: 680)
        }
        .senseControllerNavigation(controllerItems, enabled: session.destination == .library && activeSheet == nil,
                                   back: {
                                       if artwork.errorMessage != nil { artwork.errorMessage = nil }
                                       else if artwork.isImporterPresented { artwork.isImporterPresented = false }
                                       else if importAlert != nil { importAlert = nil }
                                       else if showingImporter { showingImporter = false }
                                       else if engine.isActive && session.canPlay { session.play(in: displayMode) }
                                   })
        .onAppear { EngineImmersiveTrace.shared.record("library.appear") }
        .onDisappear { EngineImmersiveTrace.shared.record("library.disappear") }
    }

    /// Halo needs a controller; the cue informs without blocking setup or import.
    private var controllerMissing: Bool {
        controller.presence != .checking && !controller.presence.connected
    }
    private var windowAffordanceVisible: Bool {
        session.destination == .window && session.pauseAffordanceVisible
    }
    private var topOrnamentVisible: Bool { controllerMissing || windowAffordanceVisible }

    @ToolbarContentBuilder
    private var toolbar: some ToolbarContent {
        if session.destination == .window {
            ToolbarItem(placement: .topBarLeading) {
                Button("Library", systemImage: "square.grid.2x2") { session.request(.library) }
                    .disabled(session.presentationBusy || session.quitBlocksPresentation)
                    .help("Return to the Library. Halo keeps running.")
            }
            ToolbarItem(placement: .topBarTrailing) {
                Button("Immersive", systemImage: "visionpro") {
                    preferredPresentation = EngineDisplayMode.immersive.rawValue
                    session.play(in: .immersive)
                }
                .disabled(!session.canPlay)
            }
        }
        ToolbarItem(placement: .principal) {
            Menu {
                if engine.isActive && !session.quitBlocksPresentation {
                    Button("Halo Menu", systemImage: "slider.horizontal.3") { session.showControls() }
                }
                Button("Settings", systemImage: "gearshape") { session.showSettings() }
                if session.destination == .library { EngineLibraryArtworkActions() }
                Button("Game Data", systemImage: "externaldrive") { activeSheet = .gameData }
                Button("Controller", systemImage: "gamecontroller") { activeSheet = .controller }
                Button("Cloud Saves", systemImage: "icloud") { activeSheet = .cloudSaves }
                Button("Graphics Mods", systemImage: "sparkles") { activeSheet = .graphics }
                Button("About \(EngineLibraryTheme.productName)", systemImage: "info.circle") { activeSheet = .about }
            } label: {
                EngineLibraryTitleLabel()
                    .contentShape([.interaction, .hoverEffect], Capsule())
                    .hoverEffect(.highlight)
            }
            .menuStyle(.button)
            .buttonStyle(.plain)
            .accessibilityLabel("\(EngineLibraryTheme.productName) menu")
        }
    }

    private var library: some View {
        GeometryReader { proxy in
            // One centered cover card, sized like the Dark Eco shelf: short
            // windows scroll rather than shrinking touch targets.
            let coverWidth = min(250, max(170, (proxy.size.height - 250) / 1.5), max(170, proxy.size.width - 64))
            ScrollView {
                VStack(spacing: 18) {
                    Spacer(minLength: 0)
                    EngineLibraryGameCard(
                        coverWidth: coverWidth,
                        accessibilityLabel: cardAccessibilityLabel,
                        enabled: primaryEnabled,
                        action: activateCard
                    ) { readiness }
                    HStack(spacing: 12) {
                        Button(primaryTitle, systemImage: primarySymbol, action: activateCard)
                            .buttonStyle(.borderedProminent)
                            .controlSize(.large)
                            .disabled(!primaryEnabled)
                            .senseControllerTarget("library.play")
                        Button("Settings", systemImage: "gearshape") { session.showSettings() }
                            .buttonStyle(.bordered)
                            .controlSize(.large)
                            .senseControllerTarget("library.settings")
                    }
                    Picker("Play in", selection: $preferredPresentation) {
                        ForEach(EngineDisplayMode.allCases) { mode in
                            Label(mode.label, systemImage: mode.symbol).tag(mode.rawValue)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: max(coverWidth + 12, 240))
                    .disabled(session.quitBlocksPresentation)
                    .accessibilityLabel("Choose Halo view")
                    .senseControllerTarget("library.mode")
                    controllerStatus
                    if let message = session.presentationMessage {
                        Text(message)
                            .font(.callout).foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .fixedSize(horizontal: false, vertical: true)
                            .frame(maxWidth: 420)
                    }
                    Spacer(minLength: 0)
                }
                .padding(.horizontal, 24)
                .padding(.vertical, 20)
                .frame(maxWidth: .infinity)
                .frame(minHeight: proxy.size.height)
            }
            .scrollIndicators(.hidden)
        }
        .safeAreaInset(edge: .bottom, spacing: 0) {
            Text(haloBuildLabel)
                .font(.caption2).monospacedDigit().foregroundStyle(.secondary)
                .accessibilityIdentifier("launcher.buildNumber")
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
        }
        .background { EngineLibraryBackground().ignoresSafeArea() }
    }

    /// Accurate reader state; Halo is played with a controller, not touch.
    private var controllerStatus: some View {
        let presence = controller.presence
        return Button { activeSheet = .controller } label: {
            HStack(spacing: 8) {
                Image(systemName: presence.symbol)
                    .foregroundStyle(presence.connected ? EngineLibraryTheme.success : EngineLibraryTheme.warning)
                VStack(alignment: .leading, spacing: 1) {
                    Text(presence == .checking ? "Checking controller…" : presence.title).font(.caption.weight(.semibold))
                    if presence != .checking {
                        Text(presence.summary).font(.caption2).foregroundStyle(.secondary).lineLimit(2)
                    }
                }
            }
            .padding(.horizontal, 14).padding(.vertical, 8)
            .background(EngineLibraryTheme.surface.opacity(0.8), in: Capsule())
            .contentShape([.interaction, .hoverEffect], Capsule())
            .hoverEffect(.highlight)
        }
        .buttonStyle(.plain)
        .accessibilityElement(children: .combine)
        .accessibilityHint("Shows controller help")
    }

    private var controllerItems: [EngineControllerMenuItem] {
        if artwork.errorMessage != nil {
            return [.init(id: "library.modal", title: "Dismiss artwork error", activate: { artwork.errorMessage = nil })]
        }
        if artwork.isImporterPresented {
            return [.init(id: "library.modal", title: "Cancel artwork import", activate: { artwork.isImporterPresented = false })]
        }
        if importAlert != nil {
            return [.init(id: "library.modal", title: "Dismiss import error", activate: { importAlert = nil })]
        }
        if showingImporter {
            return [.init(id: "library.modal", title: "Cancel file import", activate: { showingImporter = false })]
        }
        return [
            .init(id: "library.play", title: primaryTitle, enabled: primaryEnabled, activate: activateCard),
            .init(id: "library.settings", title: "Settings", activate: { session.showSettings() }),
            .init(id: "library.artwork", title: "Change Artwork", enabled: !artwork.isBusy, activate: { artwork.chooseArtwork() }),
            .init(id: "library.mode", title: "Play in", value: displayMode.label,
                  enabled: !session.quitBlocksPresentation,
                  activate: cycleDisplayMode, adjust: { _ in cycleDisplayMode() })
        ]
    }
    private func cycleDisplayMode() {
        preferredPresentation = (displayMode == .window ? EngineDisplayMode.immersive : .window).rawValue
    }

    private var busy: Bool {
        session.presentationBusy || session.preparingPlay || session.cloudSaves.isBusy || session.quitBlocksPresentation
    }
    private var engineEnded: Bool { engine.state == ENGINEVISION_FAILED || engine.state == ENGINEVISION_STOPPED }
    /// Setup and game details stay reachable without a controller; only an
    /// in-flight transition, save operation or Quit disables the action.
    private var primaryEnabled: Bool { !busy }
    private var primaryTitle: String {
        if session.quitBlocksPresentation { return session.quitPhase == .stopped ? "Halo Has Quit" : "Quitting…" }
        guard assets.phase == .ready else { return assets.phase == .missing ? "Import Game" : "Game Setup" }
        if engine.isActive { return "Resume" }
        if engineEnded { return "Details" }
        return "Play"
    }
    private var primarySymbol: String {
        if session.quitBlocksPresentation { return "power" }
        guard assets.phase == .ready else { return assets.phase == .missing ? "folder.badge.plus" : "externaldrive" }
        return engineEnded ? "info.circle" : "play.fill"
    }

    private var cardAccessibilityLabel: String {
        if let message = session.quitMessage { return "Halo: Combat Evolved, \(message)" }
        if session.cloudSaves.isBusy { return "Halo: Combat Evolved, save operation in progress" }
        if session.preparingPlay { return "Halo: Combat Evolved, checking cloud saves before play" }
        if session.presentationBusy { return "Halo: Combat Evolved, opening game" }
        switch assets.phase {
        case .checking: return "Halo: Combat Evolved, checking game data"
        case .importing:
            let progress = assets.total > 0 ? ", \(Int(min(100, max(0, 100 * Double(assets.copied) / Double(assets.total))))) percent" : ""
            return "Halo: Combat Evolved, preparing game data\(progress), open game setup"
        case .missing: return "Halo: Combat Evolved, import game data"
        case .failed: return "Halo: Combat Evolved, game data needs attention, open game setup"
        case .ready: break
        }
        if !engine.canStart && !engine.isActive { return "Halo: Combat Evolved, stopped, open game details" }
        if engine.isActive && engine.frameSequence == 0 { return "Halo: Combat Evolved, starting game" }
        return "\(engine.isActive ? "Resume" : "Play") Halo: Combat Evolved, \(displayMode.label)"
    }

    private func activateCard() {
        guard !busy else { return }
        if session.canPlay { session.play(in: displayMode) }
        else { activeSheet = .gameData }
    }

    @ViewBuilder
    private var readiness: some View {
        if session.quitBlocksPresentation {
            if session.quitPhase == .stopped || session.quitPhase == .stillStopping {
                Label("Quit · close and reopen to play", systemImage: "power")
                    .foregroundStyle(EngineLibraryTheme.warning)
            } else {
                HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Quitting Halo…") }
            }
        } else if session.cloudSaves.isBusy {
            Label("Working with saves…", systemImage: "icloud")
                .foregroundStyle(EngineLibraryTheme.accent)
        } else if session.preparingPlay {
            HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Checking cloud saves…") }
        } else if session.presentationBusy {
            HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Opening Halo…") }
        } else {
            switch assets.phase {
            case .checking:
                HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Checking game data…") }
            case .importing:
                VStack(alignment: .leading, spacing: 5) {
                    Text("Preparing game data…")
                    if assets.total > 0 {
                        ProgressView(value: min(1, max(0, Double(assets.copied) / Double(assets.total))))
                    } else { ProgressView().controlSize(.mini) }
                }
            case .missing:
                Label("Import game data", systemImage: "folder.badge.plus")
                    .foregroundStyle(EngineLibraryTheme.accent)
            case .failed:
                Label("Game data needs attention", systemImage: "exclamationmark.triangle")
                    .foregroundStyle(EngineLibraryTheme.warning)
            case .ready:
                if engine.isActive && engine.frameSequence == 0 {
                    HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Starting Halo…") }
                } else if engineEnded {
                    Label("Stopped · close and reopen to play", systemImage: "exclamationmark.triangle")
                        .foregroundStyle(EngineLibraryTheme.warning)
                } else {
                    Label(engine.isActive ? "Running · Resume" : "Ready", systemImage: "play.fill")
                }
            }
        }
    }

    private var gameWindow: some View {
        ZStack {
            EngineFrameView()
            if engine.frameSequence == 0 || !engine.isActive {
                VStack(spacing: 12) {
                    if engine.isActive { ProgressView() }
                    Text(engine.isActive ? "Starting Halo…" : "Halo stopped").font(.headline)
                    if !engine.isActive {
                        Text("Close and reopen \(EngineLibraryTheme.productName) to play again.")
                            .font(.callout).foregroundStyle(.secondary)
                        Button("Return to Library") { session.request(.library) }
                            .buttonStyle(.borderedProminent)
                            .disabled(session.presentationBusy)
                    }
                }
                .padding(30)
                .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 20))
            }
        }
        .background(.black)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// Dark Eco-style shimmering cue shown above the window when Halo has no
/// usable controller. Informational only; it never takes input.
struct EngineControllerStatusCue: View {
    let presence: EngineControllerPresence
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var highlightLocation: CGFloat = -1

    var body: some View {
        VStack(spacing: 2) {
            Text("No controller detected.")
                .font(.headline.weight(.semibold))
                .foregroundStyle(gradient)
            Text(presence.summary)
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(.horizontal, 18)
        .padding(.vertical, 10)
        .glassBackgroundEffect()
        .allowsHitTesting(false)
        .accessibilityElement(children: .combine)
        .onAppear { animate() }
        .onChange(of: reduceMotion) { _, _ in animate() }
    }

    private var gradient: LinearGradient {
        let accent = EngineLibraryTheme.accent
        guard !reduceMotion else { return LinearGradient(colors: [accent], startPoint: .leading, endPoint: .trailing) }
        return LinearGradient(colors: [accent, accent, .white.opacity(0.85), EngineLibraryTheme.accentGlow, accent],
                              startPoint: UnitPoint(x: highlightLocation - 1, y: 0.5),
                              endPoint: UnitPoint(x: highlightLocation + 1, y: 0.5))
    }
    private func animate() {
        highlightLocation = -1
        guard !reduceMotion else { return }
        withAnimation(.linear(duration: 1.6).repeatForever(autoreverses: false)) { highlightLocation = 1 }
    }
}

/// The only thing that appears automatically during play: a small Settings
/// button. It opens the game menu; it never opens settings by itself.
struct EngineSettingsAffordanceButton: View {
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Label("Settings", systemImage: "gearshape.fill")
                .font(.headline)
                .padding(.horizontal, 20)
                .padding(.vertical, 12)
                .frame(minHeight: 44)
        }
        .buttonStyle(.plain)
        .glassBackgroundEffect(in: Capsule())
        .contentShape([.interaction, .hoverEffect], Capsule())
        .hoverEffect(.highlight)
        .accessibilityLabel("Halo settings")
        .accessibilityHint("Opens the game menu with display, live settings and Quit Game")
    }
}

/// Immersive host for the Settings affordance. It deliberately registers no
/// Sense navigation surface, so Halo's own menus keep every controller event.
struct EnginePauseAffordancePanel: View {
    @ObservedObject var session: EngineAppSession
    var body: some View {
        EngineSettingsAffordanceButton { session.showControls() }
            .padding(10)
            .preferredColorScheme(.dark)
            .tint(EngineLibraryTheme.accent)
            .handlesGameControllerEvents(matching: .gamepad)
            .onDisappear { session.pausePanelDisappeared() }
    }
}

private struct EngineGameDataView: View {
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EngineRuntimeModel
    let saveOperationBusy: Bool
    let importGame: () -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                Label(assets.phase == .ready ? "Game data is ready" : "Game Data", systemImage: "externaldrive")
                    .font(.title2.weight(.semibold))
                Text(assets.detail).textSelection(.enabled)
                if assets.phase == .importing {
                    if assets.total > 0 { ProgressView(value: Double(assets.copied), total: Double(assets.total)) }
                    else { ProgressView() }
                    Text(assets.currentFile).font(.footnote).foregroundStyle(.secondary)
                }
                if let receipt = assets.receipt {
                    LabeledContent("Imported files", value: "\(receipt.fileCount)")
                    LabeledContent("Storage", value: ByteCountFormatter.string(fromByteCount: receipt.totalBytes, countStyle: .file))
                }
                if !assets.hasBundledPayload {
                    Button("Import Halo PC Game Folder", systemImage: "folder.badge.plus", action: importGame)
                        .disabled(assets.phase == .importing || !engine.canStart || saveOperationBusy)
                    Text("Choose your installed Halo PC folder. Replacing game data requires closing and reopening the app first.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                if assets.hasBundledPayload, case .failed = assets.phase {
                    Button("Retry Game Preparation", systemImage: "arrow.clockwise") {
                        guard engine.canStart, !saveOperationBusy else { return }
                        assets.prepareBundledGame()
                    }
                    .disabled(!engine.canStart || saveOperationBusy)
                }
                Text("Game data and saves use the existing private app storage on this headset.")
                    .font(.footnote).foregroundStyle(.secondary)
                DisclosureGroup("Diagnostics") {
                    VStack(alignment: .leading, spacing: 12) {
                        Text(haloBuildLabel)
                        Text(engine.status)
                        Text(engine.frameSize)
                        Text("Frame \(engine.frameSequence)")
                        Text(engine.diagnosticsStatus)
                        if let report = engine.diagnostics.reportURL {
                            ShareLink(item: report) { Label("Share Test Report", systemImage: "square.and.arrow.up") }
                        }
                    }
                    .font(.footnote).foregroundStyle(.secondary).textSelection(.enabled).padding(.top, 12)
                }
            }
            .padding(28)
        }
        .navigationTitle("Game Data")
    }
}

struct EngineControllerHelpView: View {
    @ObservedObject var engine: EngineRuntimeModel
    @ObservedObject var senseSettings: EngineSenseSettingsModel
    private var sensePairSelected: Bool {
        var input = HostGCSnapshot()
        return hostgc_poll(&input) && input.spatial
    }
    var body: some View {
        Form {
            Section {
                Label(engine.controllerStatus, systemImage: "gamecontroller.fill")
            }
            Section("PS VR2 Sense") {
                Text("Pair both controllers in visionOS Settings → Bluetooth.")
                EngineSenseButtonGuide(mode: senseSettings.weaponMode, trackedAim: senseSettings.trackedAim)
                Text("Tracked aiming is experimental and currently supports on-foot campaign play. Point the right controller to aim. Switch tracked aiming off in Halo Controls for vehicles or multiplayer.")
                Text("Point and press R2 to select items in Halo menus.")
                Text("Turn physically, use smooth right-stick turning, or point the reticle toward the edge of your view. Choose turning options in Settings → Sense Controls & Turning.")
            }
            if !sensePairSelected {
                Section("Xbox / PlayStation Controller") {
                    Text("Left stick: move · Right stick: look")
                    Text("A / Cross: jump and select")
                    Text("B / Circle: crouch and back")
                    Text("X / Square: use and reload")
                    Text("Y / Triangle: switch weapon")
                    Text("RT / R2: fire · LT / L2: zoom")
                    Text("LB / L1: grenade · RB / R1: melee")
                    Text("D-pad: up flashlight · down grenade type · left reload · right pick up weapon")
                    Text("Menu / Options: pause and skip cinematic")
                    Text("Press X / Square when Halo asks for E to exit the cryotube.")
                }
            }
            Section("Immersive View") {
                Text("Open the Halo Menu to recenter or adjust the view: pinch the Settings button that appears while Halo's own menu is open, or choose Halo Menu from the \(EngineLibraryTheme.productName) menu. Your head position doesn't aim or move the player.")
                Text("With Sense, hold Create + Options to open the Halo Menu. In app menus, use the left stick (or a gamepad's D-pad) to choose or adjust, Cross or A to select, and Circle or B to go back. The highlight appears only while you navigate with a controller; gaze and pinch always work.")
                Text("While an app menu or Settings is open, Halo doesn't receive controller input. After it closes, release every button and stick to hand control back to Halo.")
            }
        }
        .navigationTitle("Controller")
    }
}
