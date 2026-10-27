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
    @State private var showingImporter = false
    @State private var importAlert: String?
    @State private var activeSheet: EngineLibrarySheet?
    @State private var importAfterSheetDismissal = false

    var body: some View {
        NavigationStack {
            Group {
                if session.destination == .window { gameWindow }
                else { library }
            }
            .toolbar(.hidden, for: .navigationBar)
        }
        .libraryArtworkImporter()
        .tint(EngineLibraryTheme.accent)
        .preferredColorScheme(.dark)
        .handlesGameControllerEvents(matching: .gamepad)
        .ornament(visibility: topOrnamentVisible ? .visible : .hidden,
                  attachmentAnchor: .scene(.top), contentAlignment: .bottom) {
            HStack(spacing: 12) {
                if controllerMissing { EngineControllerStatusCue(presence: controller.presence) }
                if windowControlsVisible {
                    windowControl("Expand", symbol: "arrow.up.left.and.arrow.down.right") {
                        session.play(in: .immersive)
                    }
                    .accessibilityHint("Continue this game in immersive view")
                    .accessibilityIdentifier("game.expand")
                    .disabled(busy || !session.canPlay)
                    windowControl("Settings", symbol: "gearshape") { session.showControls() }
                        .accessibilityHint("Open the game menu")
                        .accessibilityIdentifier("game.settings")
                        .disabled(busy)
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
                                       else if engine.isActive && session.canPlay { session.play(in: .window) }
                                   })
        .onAppear { EngineImmersiveTrace.shared.record("library.appear") }
        .onDisappear { EngineImmersiveTrace.shared.record("library.disappear") }
    }

    /// Halo needs a controller; the cue informs without blocking setup or import.
    private var controllerMissing: Bool {
        controller.presence != .checking && !controller.presence.connected
    }
    private var windowControlsVisible: Bool {
        session.destination == .window && engine.isActive
    }
    private var topOrnamentVisible: Bool { controllerMissing || windowControlsVisible }

    private func windowControl(_ title: String, symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 17, weight: .semibold))
                .frame(width: 44, height: 44)
                .contentShape(Circle())
        }
        .buttonStyle(.plain)
        .glassBackgroundEffect(in: Circle())
        .accessibilityLabel(title)
    }

    private var libraryMenu: some View {
        Menu {
            Button("Settings", systemImage: "gearshape") { session.showSettings() }
            if engine.isActive && !session.quitBlocksPresentation {
                Button("Halo Menu", systemImage: "slider.horizontal.3") { session.showControls() }
            }
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
        .accessibilityIdentifier("launcher.menu")
        .senseControllerTarget("library.settings")
    }

    private var library: some View {
        VStack(spacing: 0) {
            // Keep the menu geometrically centered, independent of navigation
            // toolbar placement and the card's width.
            HStack {
                Spacer(minLength: 0)
                libraryMenu
                Spacer(minLength: 0)
            }
            .padding(.vertical, 8)

            GeometryReader { proxy in
                // The card adapts to the remaining window height. There is no
                // scrolling shelf or second Play control below it.
                let noticeHeight: CGFloat = session.presentationMessage == nil ? 0 : 52
                let coverWidth = max(64, min(250, proxy.size.width - 48,
                                            (proxy.size.height - 106 - noticeHeight) / 1.5))
                VStack(spacing: 12) {
                    Spacer(minLength: 0)
                    EngineLibraryGameCard(
                        coverWidth: coverWidth,
                        accessibilityLabel: cardAccessibilityLabel,
                        enabled: primaryEnabled,
                        action: activateCard
                    ) { readiness }
                    if let message = session.presentationMessage {
                        Text(message)
                            .font(.callout).foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                            .lineLimit(2)
                            .frame(maxWidth: 420)
                            .padding(.horizontal, 16)
                    }
                    Spacer(minLength: 0)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            Text(haloBuildLabel)
                .font(.caption2).monospacedDigit().foregroundStyle(.secondary)
                .accessibilityIdentifier("launcher.buildNumber")
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
        }
        .background { EngineLibraryBackground().ignoresSafeArea() }
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
            .init(id: "library.artwork", title: "Change Artwork", enabled: !artwork.isBusy, activate: { artwork.chooseArtwork() })
        ]
    }
    private var busy: Bool {
        session.presentationBusy || session.preparingPlay || session.quitBlocksPresentation
    }
    private var engineEnded: Bool { engine.state == ENGINEVISION_FAILED || engine.state == ENGINEVISION_STOPPED }
    /// An automatic cloud check must not swallow Play. The session accepts
    /// that intent, then finishes its save checks before starting the engine.
    private var primaryEnabled: Bool { !busy }
    private var primaryTitle: String {
        if session.quitBlocksPresentation { return session.quitPhase == .stopped ? "Halo Has Quit" : "Quitting…" }
        guard assets.phase == .ready else { return assets.phase == .missing ? "Import Game" : "Game Setup" }
        if engine.isActive { return "Resume" }
        if engineEnded { return "Details" }
        return "Play"
    }
    private var cardAccessibilityLabel: String {
        if let message = session.quitMessage { return "Halo: Combat Evolved, \(message)" }
        if session.preparingPlay { return "Halo: Combat Evolved, checking cloud saves before play" }
        if session.cloudSaves.isBusy { return "Play Halo: Combat Evolved after save syncing finishes" }
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
        return "\(engine.isActive ? "Resume" : "Play") Halo: Combat Evolved in a window"
    }

    private func activateCard() {
        EngineImmersiveTrace.shared.record(
            "library.play.request assetsReady=\(assets.phase == .ready) cloudBusy=\(session.cloudSaves.isBusy) preparing=\(session.preparingPlay) presentationBusy=\(session.presentationBusy) quit=\(session.quitBlocksPresentation)",
            state: UInt32(bitPattern: engine.state))
        guard !busy else { return }
        if assets.phase == .ready && (engine.canStart || engine.isActive) { session.play(in: .window) }
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
        } else if session.preparingPlay {
            HStack(spacing: 6) { ProgressView().controlSize(.mini); Text("Checking cloud saves…") }
        } else if session.cloudSaves.isBusy {
            Label("Play · syncing saves…", systemImage: "icloud")
                .foregroundStyle(EngineLibraryTheme.accent)
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
                    Label(engine.isActive ? "Resume" : "Play", systemImage: "play.fill")
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
        .overlay(alignment: .top) {
            if let message = session.presentationMessage {
                Text(message)
                    .font(.callout)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 500)
                    .padding(12)
                    .glassBackgroundEffect()
                    .padding(16)
            }
        }
        .background(.black)
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .padding(12)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// One static controller notice above the window. Informational only;
/// it never takes input or prevents the card from opening the game.
struct EngineControllerStatusCue: View {
    let presence: EngineControllerPresence

    var body: some View {
        Label("No controller detected", systemImage: presence.symbol)
            .font(.callout.weight(.medium))
            .foregroundStyle(.secondary)
            .padding(.horizontal, 18)
            .padding(.vertical, 10)
            .glassBackgroundEffect()
            .allowsHitTesting(false)
            .accessibilityElement(children: .combine)
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
