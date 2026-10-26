import SwiftUI

private enum EngineSettingsPage: Hashable { case sense, live, orientation, controller, quality, graphics, cloud, about }

struct EngineSettingsView: View {
    @ObservedObject var session: EngineAppSession
    @State private var showingReset = false
    @State private var path: [EngineSettingsPage] = []
    @AppStorage(EngineLaunchPreferences.visionResolutionKey) private var resolution = EngineLaunchPreferences.visionHighestResolution
    @AppStorage(EngineLaunchPreferences.visionFrameRateKey) private var frameRate = EngineVisionFrameRate.maximum.rawValue

    var body: some View {
        NavigationStack(path: $path) {
            List {
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("Make Halo feel right").font(.title2.bold())
                        Text("Adjust your view while you play. Presentation settings are remembered on this device.")
                            .foregroundStyle(.secondary)
                    }.padding(.vertical, 8)
                }
                Section("Play") {
                    NavigationLink(value: EngineSettingsPage.sense) { Label("Sense Controls & Turning", systemImage: "gamecontroller") }
                        .senseControllerTarget("settings.sense")
                    NavigationLink(value: EngineSettingsPage.live) { Label("Depth, View & Sound", systemImage: "slider.horizontal.3") }
                        .senseControllerTarget("settings.live")
                    NavigationLink(value: EngineSettingsPage.orientation) { Label("Orientation & Recenter", systemImage: "scope") }
                        .senseControllerTarget("settings.orientation")
                    NavigationLink(value: EngineSettingsPage.controller) { Label("Controller Help", systemImage: "questionmark.circle") }
                        .senseControllerTarget("settings.controller")
                }
                Section {
                    Picker("Resolution at next launch", selection: $resolution) {
                        Text("1280 × 960 · Lighter").tag("1280x960")
                        Text("1600 × 1200").tag("1600x1200")
                        Text("1920 × 1440").tag("1920x1440")
                        Text("2048 × 1536").tag("2048x1536")
                        Text("2560 × 1920 · Heavier").tag("2560x1920")
                    }
                    .senseControllerTarget("settings.resolution")
                    EngineRenderResolutionStatus(engine: session.engine)
                    Picker("Frame rate at next launch", selection: $frameRate) {
                        ForEach(EngineVisionFrameRate.allCases, id: \.rawValue) { Text($0.title).tag($0.rawValue) }
                    }
                    .senseControllerTarget("settings.frameRate")
                    EngineFrameLimitStatus(engine: session.engine)
                    NavigationLink(value: EngineSettingsPage.quality) { Label("Image Quality", systemImage: "camera.filters") }
                        .senseControllerTarget("settings.quality")
                    NavigationLink(value: EngineSettingsPage.graphics) { Label("Graphics Mods", systemImage: "sparkles") }
                        .senseControllerTarget("settings.graphics")
                } header: { Text("Graphics") } footer: {
                    Text("Resolution and frame rate apply after you fully close and reopen the app. Halo’s Video Setup can change the current resolution while playing. Immersive mode uses this size for each rendered view; higher sizes cost more memory and GPU time. With scene upscaling (Image Quality) the world renders smaller and is upscaled to this size. Maximum asks to lift the 30 fps limit Halo saves with your profile; Original limiter keeps Halo’s own limiter choice. Cutscenes keep Halo’s pace, and the frame rate you see depends on the scene and the headset. Visual packs also require a restart.")
                }
                Section("Your game") {
                    NavigationLink(value: EngineSettingsPage.cloud) { Label("Cloud Saves", systemImage: "icloud") }
                        .senseControllerTarget("settings.cloud")
                    NavigationLink(value: EngineSettingsPage.about) { Label("About \(EngineLibraryTheme.productName)", systemImage: "info.circle") }
                        .senseControllerTarget("settings.about")
                    Button("Done", action: session.hideSettings).senseControllerTarget("settings.done")
                }
                Section {
                    Button("Reset Presentation Settings", role: .destructive) { showingReset = true }
                } footer: { Text("Resets depth, view and sound. Game progress and graphics packs stay as they are.") }
            }
            .senseControllerNavigation(controllerItems, back: {
                if showingReset { showingReset = false } else { session.hideSettings() }
            })
            .navigationDestination(for: EngineSettingsPage.self) { page in
                switch page {
                case .sense: EngineSenseSettingsView(model: session.senseSettings) { session.immersive.recenter() }
                case .live: EngineLiveSettingsView(model: session.settings)
                case .orientation: EngineOrientationSettingsView(immersive: session.immersive)
                case .controller: secondaryPage { EngineControllerHelpView(engine: session.engine, senseSettings: session.senseSettings) }
                case .quality: EngineGraphicsQualityView(model: session.graphics)
                case .graphics: secondaryPage { EngineGraphicsModsView() }
                case .cloud: secondaryPage { EngineCloudSavesView(store: session.cloudSaves, gameRoot: session.assets.rootURL, engineActive: !session.engine.canStart || session.assets.phase != .ready) }
                case .about: secondaryPage { EngineAboutView() }
                }
            }
            .navigationTitle("Settings")
            .tint(EngineLibraryTheme.accent)
            .confirmationDialog("Reset depth, view and sound settings?", isPresented: $showingReset) {
                Button("Reset Settings", role: .destructive) { session.settings.reset() }
            }
        }
        .preferredColorScheme(.dark)
    }
    private var controllerItems: [EngineControllerMenuItem] {
        if showingReset { return [.init(id: "settings.cancelReset", title: "Cancel reset", activate: { showingReset = false })] }
        return [
            route("sense", "Sense Controls & Turning", .sense), route("live", "Depth, View & Sound", .live),
            route("orientation", "Orientation & Recenter", .orientation),
            route("controller", "Controller Help", .controller),
            .init(id: "settings.resolution", title: "Resolution at next launch", value: resolution,
                  activate: { cycleResolution(1) }, adjust: cycleResolution),
            .init(id: "settings.frameRate", title: "Frame rate at next launch",
                  value: EngineVisionFrameRate(rawValue: frameRate)?.title ?? frameRate,
                  activate: { cycleFrameRate(1) }, adjust: cycleFrameRate),
            route("quality", "Image Quality", .quality),
            route("graphics", "Graphics Mods", .graphics), route("cloud", "Cloud Saves", .cloud),
            route("about", "About \(EngineLibraryTheme.productName)", .about),
            .init(id: "settings.done", title: "Done", activate: session.hideSettings)
        ]
    }
    private func route(_ id: String, _ title: String, _ page: EngineSettingsPage) -> EngineControllerMenuItem {
        .init(id: "settings." + id, title: title, activate: { path.append(page) })
    }
    private func cycleResolution(_ direction: Int) {
        let values = EngineLaunchPreferences.visionResolutions
        let index = values.firstIndex(of: resolution) ?? 3
        resolution = values[(index + (direction < 0 ? values.count - 1 : 1)) % values.count]
    }
    private func cycleFrameRate(_ direction: Int) {
        let values = EngineVisionFrameRate.allCases.map(\.rawValue)
        let index = values.firstIndex(of: frameRate) ?? 0
        frameRate = values[(index + (direction < 0 ? values.count - 1 : 1)) % values.count]
    }
    private func secondaryPage<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        content()
            .toolbar { ToolbarItem(placement: .topBarTrailing) {
                Button("Back") { if !path.isEmpty { path.removeLast() } }.senseControllerTarget("settings.back")
            } }
            .senseControllerNavigation([.init(id: "settings.back", title: "Back", activate: { if !path.isEmpty { path.removeLast() } })],
                                       back: { if !path.isEmpty { path.removeLast() } })
    }
}

private struct EngineRenderResolutionStatus: View {
    @ObservedObject var engine: EngineRuntimeModel
    var body: some View {
        LabeledContent("Current render size", value: engine.renderResolution)
            .font(.footnote).foregroundStyle(.secondary)
    }
}

/// What this launch asked of Halo's limiter; not a measured frame rate.
private struct EngineFrameLimitStatus: View {
    @ObservedObject var engine: EngineRuntimeModel
    var body: some View {
        LabeledContent("This launch", value: engine.frameLimit)
            .font(.footnote).foregroundStyle(.secondary)
    }
}

struct EngineOrientationSettingsView: View {
    @ObservedObject var immersive: EngineImmersiveSession
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        List {
            Section {
                Button("Recenter Here", systemImage: "scope") { immersive.recenter() }
                    .buttonStyle(.borderedProminent).controlSize(.large)
                    .disabled(!immersive.active)
                    .senseControllerTarget("orientation.recenter")
                Text("Look where you want Halo to appear, then recenter. You can do this seated or reclined.")
                    .foregroundStyle(.secondary)
                if !immersive.active {
                    Label("Open an immersive game to adjust orientation.", systemImage: "visionpro")
                        .font(.footnote).foregroundStyle(.secondary)
                }
            }
            Section("Fine adjustment") {
                HStack {
                    Text("Horizontal"); Spacer()
                    Button("Left", systemImage: "arrow.left") { immersive.nudgeOrientation(yawDegrees: -5) }.senseControllerTarget("orientation.left")
                    Text("\(Int(immersive.orientationYawDegrees))°").monospacedDigit().frame(minWidth: 40)
                    Button("Right", systemImage: "arrow.right") { immersive.nudgeOrientation(yawDegrees: 5) }.senseControllerTarget("orientation.right")
                }
                HStack {
                    Text("Vertical"); Spacer()
                    Button("Down", systemImage: "arrow.down") { immersive.nudgeOrientation(pitchDegrees: -5) }.senseControllerTarget("orientation.down")
                    Text("\(Int(immersive.orientationPitchDegrees))°").monospacedDigit().frame(minWidth: 40)
                    Button("Up", systemImage: "arrow.up") { immersive.nudgeOrientation(pitchDegrees: 5) }.senseControllerTarget("orientation.up")
                }
                Button("Reset Fine Adjustment") { immersive.resetOrientation() }.senseControllerTarget("orientation.reset")
            }.disabled(!immersive.active)
            Section {
                Text("These controls move the presentation. Your controller still aims and moves your character.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .senseControllerNavigation([
            .init(id: "orientation.recenter", title: "Recenter", enabled: immersive.active, activate: { immersive.recenter() }),
            .init(id: "orientation.left", title: "Left", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: -5) }),
            .init(id: "orientation.right", title: "Right", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: 5) }),
            .init(id: "orientation.down", title: "Down", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: -5) }),
            .init(id: "orientation.up", title: "Up", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: 5) }),
            .init(id: "orientation.reset", title: "Reset", enabled: immersive.active, activate: { immersive.resetOrientation() })
        ], back: { dismiss() })
        .navigationTitle("Orientation")
    }
}

private enum EngineGameMenuPage: Hashable { case live, sense, controller }

/// The in-game menu opened from the Settings affordance, the launcher menu or
/// the Sense chord. Resume, Return to Library and Quit Game are distinct:
/// only Quit Game ends the engine, and it says so before it does.
struct EngineGameMenuView: View {
    @ObservedObject var session: EngineAppSession
    @ObservedObject var engine: EngineRuntimeModel
    @ObservedObject var immersive: EngineImmersiveSession
    @ObservedObject private var controller = EngineAppControllerNavigation.shared
    @AppStorage("HaloPreferredPresentation") private var preferredPresentation = EngineDisplayMode.immersive.rawValue
    @State private var path: [EngineGameMenuPage] = []
    @State private var confirmingQuit = false
    @State private var showingAdjustment = false

    private var live: Bool { engine.isActive && !session.quitBlocksPresentation }
    private var locked: Bool { session.presentationBusy || session.preparingPlay || session.quitBlocksPresentation }
    private var surface: EnginePauseAffordance.Surface { session.gameSurface }

    var body: some View {
        NavigationStack(path: $path) {
            List {
                if confirmingQuit { quitConfirmation }
                Section {
                    VStack(alignment: .leading, spacing: 6) {
                        Text(headline).font(.title3.bold())
                        Text(subheadline).font(.callout).foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .padding(.vertical, 4)
                    if live {
                        Button(action: resume) {
                            Label("Back to Halo", systemImage: "arrow.uturn.backward").frame(maxWidth: .infinity)
                        }
                        .buttonStyle(.borderedProminent)
                        .controlSize(.large)
                        .disabled(locked || confirmingQuit)
                        .senseControllerTarget("menu.resume")
                    }
                }
                if live {
                    Section {
                        displayButton(.window, title: "Play in Window", id: "menu.window")
                        displayButton(.immersive, title: "Play Immersively", id: "menu.immersive")
                        if surface == .immersive {
                            Button("Recenter View", systemImage: "scope") { immersive.recenter() }
                                .disabled(!immersive.active || confirmingQuit)
                                .senseControllerTarget("menu.recenter")
                            DisclosureGroup("Adjust View", isExpanded: $showingAdjustment) { adjustment }
                                .disabled(confirmingQuit)
                                .senseControllerTarget("menu.adjust")
                        }
                    } header: { Text("Display") } footer: {
                        if surface == .immersive {
                            Text("Recenter places Halo ahead of your current seated or reclined position.")
                        }
                    }
                }
                Section("Live Settings") {
                    NavigationLink(value: EngineGameMenuPage.live) { Label("Depth, View & Sound", systemImage: "slider.horizontal.3") }
                        .disabled(confirmingQuit)
                        .senseControllerTarget("menu.live")
                    EngineDepthControls(model: session.settings).disabled(confirmingQuit)
                }
                Section("Controller") {
                    Label {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(controller.presence.title)
                            Text(controller.presence.summary).font(.caption).foregroundStyle(.secondary)
                        }
                    } icon: {
                        Image(systemName: controller.presence.symbol)
                            .foregroundStyle(controller.presence.connected ? EngineLibraryTheme.success : EngineLibraryTheme.warning)
                    }
                    NavigationLink(value: EngineGameMenuPage.sense) { Label("Sense Controls & Turning", systemImage: "gamecontroller") }
                        .disabled(confirmingQuit)
                        .senseControllerTarget("menu.sense")
                    NavigationLink(value: EngineGameMenuPage.controller) { Label("Controller Help", systemImage: "questionmark.circle") }
                        .disabled(confirmingQuit)
                        .senseControllerTarget("menu.help")
                }
                Section {
                    Button("All Settings", systemImage: "gearshape") { session.showSettings() }
                        .disabled(confirmingQuit)
                        .senseControllerTarget("menu.settings")
                    if live && surface != .none {
                        Button("Return to Library", systemImage: "square.grid.2x2") { session.request(.library) }
                            .disabled(locked || confirmingQuit)
                            .senseControllerTarget("menu.library")
                    }
                } footer: {
                    if live && surface != .none { Text("Returning to the Library keeps Halo running, so you can resume right where you left off.") }
                }
                if live {
                    Section {
                        Button("Quit Game…", systemImage: "power", role: .destructive) { confirmingQuit = true }
                            .disabled(!session.canQuit || confirmingQuit)
                            .senseControllerTarget("menu.quit")
                    } footer: {
                        Text("Quitting ends this Halo session. Close and reopen \(EngineLibraryTheme.productName) to play again.")
                    }
                }
            }
            .senseControllerNavigation(controllerItems, back: back)
            .navigationDestination(for: EngineGameMenuPage.self) { page in
                switch page {
                case .live: EngineLiveSettingsView(model: session.settings)
                case .sense: EngineSenseSettingsView(model: session.senseSettings) { immersive.recenter() }
                case .controller:
                    EngineControllerHelpView(engine: engine, senseSettings: session.senseSettings)
                        .toolbar { ToolbarItem(placement: .topBarTrailing) {
                            Button("Back") { popPage() }.senseControllerTarget("menu.back")
                        } }
                        .senseControllerNavigation([.init(id: "menu.back", title: "Back", activate: popPage)], back: popPage)
                }
            }
            .navigationTitle("Halo Menu")
        }
        .frame(width: 440, height: 660)
        .tint(EngineLibraryTheme.accent)
        .preferredColorScheme(.dark)
        // Explicit Sense selection owns Cross here; ordinary gamepads keep
        // their Halo controls and gaze/pinch remains available.
        .handlesGameControllerEvents(matching: .gamepad)
        .onChange(of: session.canQuit) { _, can in if !can { confirmingQuit = false } }
        .onAppear { session.setAppMenuVisible(EngineAppSession.controlsWindowID, true) }
        .onDisappear {
            confirmingQuit = false
            session.setAppMenuVisible(EngineAppSession.controlsWindowID, false)
        }
    }

    private var headline: String {
        if let message = session.quitMessage, session.quitBlocksPresentation { return message.hasPrefix("Halo has quit") ? "Halo has quit" : "Quitting Halo" }
        if !engine.isActive { return engineEndedTitle }
        if engine.frameSequence == 0 { return "Starting Halo" }
        if enginevision_pause_menu_active() != 0 { return "Halo's game menu is open" }
        return enginevision_menu_active() != 0 ? "Halo is at its title menu" : "Halo is running"
    }
    private var engineEndedTitle: String {
        engine.state == ENGINEVISION_FAILED || engine.state == ENGINEVISION_STOPPED ? "Halo has stopped" : "Halo isn't running"
    }
    private var subheadline: String {
        if session.quitBlocksPresentation { return session.quitMessage ?? "" }
        if !engine.isActive {
            return engine.state == ENGINEVISION_IDLE ? "Start Halo from the Library." : "Close and reopen \(EngineLibraryTheme.productName) to play again."
        }
        // Closing this menu never unpauses Halo; its own pause menu stays put.
        let paused = enginevision_pause_menu_active() != 0
        switch surface {
        case .immersive, .window:
            let mode = surface == .immersive ? "Playing immersively." : "Playing in a window."
            return paused ? "\(mode) Halo's pause menu stays open; return to it when you're done here."
                          : "\(mode) Halo keeps running while this menu is open."
        case .none:
            return paused ? "Halo is paused in the background. Back to Halo returns you to its pause menu."
                          : "Halo is running in the background. Back to Halo returns you to it."
        }
    }

    @ViewBuilder
    private var quitConfirmation: some View {
        Section {
            VStack(alignment: .leading, spacing: 8) {
                Label("Quit Halo?", systemImage: "power").font(.headline)
                Text("Saved games stay on this headset. Progress since the last checkpoint will be lost.")
                Text("To play again after quitting, close and reopen \(EngineLibraryTheme.productName).")
            }
            .font(.callout)
            .padding(.vertical, 4)
            HStack(spacing: 12) {
                Button("Keep Playing") { confirmingQuit = false }
                    .buttonStyle(.bordered)
                    .senseControllerTarget("quit.cancel")
                Button("Quit Game", role: .destructive) { confirmQuit() }
                    .buttonStyle(.borderedProminent)
                    .tint(.red)
                    .disabled(!session.canQuit)
                    .senseControllerTarget("quit.confirm")
            }
        }
    }

    private func displayButton(_ mode: EngineDisplayMode, title: String, id: String) -> some View {
        let current = isCurrent(mode)
        return Button { choose(mode) } label: {
            HStack {
                Label(title, systemImage: mode.symbol)
                Spacer()
                if current { Image(systemName: "checkmark").foregroundStyle(EngineLibraryTheme.accent) }
            }
        }
        .disabled(locked || confirmingQuit || current || (mode == .immersive && !session.canPlay))
        .accessibilityAddTraits(current ? .isSelected : [])
        .senseControllerTarget(id)
    }
    private func isCurrent(_ mode: EngineDisplayMode) -> Bool {
        mode == .window ? surface == .window : surface == .immersive
    }
    private func choose(_ mode: EngineDisplayMode) {
        guard !locked, !isCurrent(mode) else { return }
        preferredPresentation = mode.rawValue
        if mode == .immersive { session.play(in: .immersive) } else { session.request(.window) }
    }
    private func resume() {
        guard !locked else { return }
        if surface == .none { session.play(in: EngineDisplayMode(rawValue: preferredPresentation) ?? .immersive) }
        // Closes app panels only; it never sends Halo an unpause, so a genuine
        // pause stays with Halo's own menu. App menus hold Halo's controller
        // input while visible, so close both.
        session.hideSettings()
        session.hideControls()
    }
    private func confirmQuit() {
        confirmingQuit = false
        session.quitGame()
    }
    private func popPage() { if !path.isEmpty { path.removeLast() } }
    private func back() {
        if confirmingQuit { confirmingQuit = false }
        else if live { resume() }
        else { session.hideControls() }
    }

    private var adjustment: some View {
        VStack(spacing: 10) {
            HStack {
                nudge("Move Halo left", "arrow.left") { immersive.nudgeOrientation(yawDegrees: -5) }
                nudge("Move Halo down", "arrow.down") { immersive.nudgeOrientation(pitchDegrees: -5) }
                nudge("Move Halo up", "arrow.up") { immersive.nudgeOrientation(pitchDegrees: 5) }
                nudge("Move Halo right", "arrow.right") { immersive.nudgeOrientation(yawDegrees: 5) }
            }
            HStack {
                Text("\(Int(immersive.orientationYawDegrees))° horizontal · \(Int(immersive.orientationPitchDegrees))° vertical")
                    .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                Spacer()
                Button("Reset") { immersive.resetOrientation() }
                    .buttonStyle(.bordered)
                    .accessibilityLabel("Reset view adjustments")
                    .senseControllerTarget("menu.reset")
            }
        }
        .padding(.top, 8)
        .disabled(!immersive.active)
    }
    private func nudge(_ label: String, _ symbol: String, action: @escaping () -> Void) -> some View {
        Button(label, systemImage: symbol, action: action)
            .labelStyle(.iconOnly)
            .buttonStyle(.bordered)
            .accessibilityHint("Adjusts the view by five degrees")
            .senseControllerTarget("menu." + symbol)
    }

    private var controllerItems: [EngineControllerMenuItem] {
        if confirmingQuit {
            // Keep Playing is first, so the initial controller selection is safe.
            return [.init(id: "quit.cancel", title: "Keep Playing", activate: { confirmingQuit = false }),
                    .init(id: "quit.confirm", title: "Quit Game", enabled: session.canQuit, activate: confirmQuit)]
        }
        var items: [EngineControllerMenuItem] = []
        if live {
            items.append(.init(id: "menu.resume", title: "Back to Halo", enabled: !locked, activate: resume))
            for (mode, id, title) in [(EngineDisplayMode.window, "menu.window", "Play in Window"),
                                      (EngineDisplayMode.immersive, "menu.immersive", "Play Immersively")] {
                items.append(.init(id: id, title: title,
                                   enabled: !locked && !isCurrent(mode) && (mode == .window || session.canPlay),
                                   activate: { choose(mode) }))
            }
            if surface == .immersive {
                items.append(.init(id: "menu.recenter", title: "Recenter View", enabled: immersive.active, activate: { immersive.recenter() }))
                items.append(.init(id: "menu.adjust", title: "Adjust View", value: "\(showingAdjustment)", activate: { showingAdjustment.toggle() }))
                if showingAdjustment { items += [
                    .init(id: "menu.arrow.left", title: "Move left", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: -5) }),
                    .init(id: "menu.arrow.down", title: "Move down", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: -5) }),
                    .init(id: "menu.arrow.up", title: "Move up", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: 5) }),
                    .init(id: "menu.arrow.right", title: "Move right", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: 5) }),
                    .init(id: "menu.reset", title: "Reset view", enabled: immersive.active, activate: { immersive.resetOrientation() })
                ] }
            }
        }
        items.append(.init(id: "menu.live", title: "Depth, View & Sound", activate: { path.append(.live) }))
        items += session.settings.controllerDepthItems()
        items += [
            .init(id: "menu.sense", title: "Sense Controls & Turning", activate: { path.append(.sense) }),
            .init(id: "menu.help", title: "Controller Help", activate: { path.append(.controller) }),
            .init(id: "menu.settings", title: "All Settings", activate: { session.showSettings() })
        ]
        if live && surface != .none {
            items.append(.init(id: "menu.library", title: "Return to Library", enabled: !locked, activate: { session.request(.library) }))
        }
        if live {
            items.append(.init(id: "menu.quit", title: "Quit Game", enabled: session.canQuit, activate: { confirmingQuit = true }))
        }
        return items
    }
}
