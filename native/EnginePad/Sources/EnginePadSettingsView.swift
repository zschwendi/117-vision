import SwiftUI

struct EnginePadSettingsView: View {
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EnginePadModel
    @ObservedObject var cloudSaves: EngineCloudSaveStore
    @ObservedObject var settingsSync: EngineSettingsSyncStore
    @Environment(\.dismiss) private var dismiss
    @AppStorage(EngineLaunchPreferences.padResolutionKey) private var resolution = EngineLaunchPreferences.padHighestResolution
    @AppStorage(EngineLaunchPreferences.padFrameRateKey) private var frameRate = EngineLaunchPreferences.padHighestFrameRate
    @State private var confirmStop = false

    var body: some View {
        // The saved choices on this display now, and what this launch fixed
        // (mobile_graphics.h). Both are read on the main thread.
        let next = EnginePadGraphicsPlan.nextLaunch(), launched = EnginePadGraphicsPlan.launched()
        NavigationStack {
            Form {
                Section {
                    Picker("Render quality", selection: $resolution) {
                        Text(EnginePadGraphicsText.native(next)).tag("native")
                        ForEach(EnginePadGraphicsText.presets, id: \.self) { value in
                            Text(EnginePadGraphicsText.presetLabel(value)).tag(value)
                        }
                    }
                    Picker("Frame rate", selection: $frameRate) {
                        Text(EnginePadGraphicsText.displayRate(next)).tag("display")
                        Text("60 fps").tag("60")
                        Text("30 fps").tag("30")
                    }
                    NavigationLink { EngineGraphicsModsView() } label: { Label("Graphics mods", systemImage: "sparkles") }
                } header: { Text("Graphics") } footer: {
                    Text(EnginePadGraphicsText.footer(next: next, launched: launched))
                }
                Section("Controls") {
                    Label(engine.controllerName ?? "No controller connected", systemImage: "gamecontroller")
                    Text("Use a controller or a keyboard with a mouse or trackpad. Without a controller or keyboard, touch controls provide movement, aiming, firing, jumping, reload, weapons, grenades, melee and pause.")
                        .foregroundStyle(.secondary)
                    Text("Pause with your controller’s Menu button, Esc or ⌘. on a keyboard, or the touch Pause button. App Settings and Library controls appear only while Halo is paused. ⌘L releases or captures the mouse pointer during gameplay.")
                        .foregroundStyle(.secondary)
                    Button("Test rumble") { EngineHaptics.testPulse() }.disabled(engine.controllerName == nil)
                }
                Section("Your progress") {
                    NavigationLink {
                        EngineCloudSavesView(store: cloudSaves, gameRoot: assets.rootURL, engineActive: !engine.canStart || assets.phase != .ready)
                    } label: { Label("Cloud saves", systemImage: "icloud") }
                }
                Section("Settings sync") {
                    Text(settingsSync.status)
                    Text("App preferences sync through iCloud. iPhone and iPad share render quality choices, sized for each screen. Halo’s profiles and control bindings travel with cloud saves. Incoming changes apply before a new game session.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                Section(EnginePadBrand.productName) {
                    NavigationLink { EngineAboutView() } label: { Label("About", systemImage: "info.circle") }
                    if engine.isActive {
                        Button("Quit Game", role: .destructive) { confirmStop = true }
                    }
                }
            }
            .navigationTitle("Settings").navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .confirmationDialog("Quit Game?", isPresented: $confirmStop, titleVisibility: .visible) {
                Button("Quit Game", role: .destructive) { engine.stop() }
            } message: { Text("Save and quit from Halo first. Unsaved progress can be lost. Reopen the app to play again.") }
        }
    }
}

/// Reads of the native launch-graphics plan (EngineVisionBridge.h).
enum EnginePadGraphicsPlan {
    /// The saved choices measured on this display now: what the next launch fixes.
    static func nextLaunch() -> EngineMobileGraphicsPlan {
        var plan = EngineMobileGraphicsPlan()
        _ = enginevision_mobile_graphics_plan(&plan)
        return plan
    }
    /// What this process fixed when Halo started, or nil before that.
    static func launched() -> EngineMobileGraphicsPlan? {
        var plan = EngineMobileGraphicsPlan()
        return enginevision_mobile_graphics_launched(&plan) ? plan : nil
    }
}

/// Words for the graphics section. Requests are named as requests: nothing
/// here measures the rate the game reaches.
enum EnginePadGraphicsText {
    static let presets = ["1280x960", "1600x1200", "1920x1440", "2048x1536"]
    /// Stored tags preserve earlier choices; their pixel budgets now adapt to
    /// this display's aspect ratio instead of naming a fixed 4:3 picture.
    static func presetLabel(_ value: String) -> String {
        switch value {
        case "1280x960": return "Low · 1.2 MP"
        case "1600x1200": return "Medium · 1.9 MP"
        case "1920x1440": return "High · 2.8 MP"
        case "2048x1536": return "Very High · 3.1 MP"
        default: return value
        }
    }

    static func size(_ width: Int32, _ height: Int32) -> String {
        width > 0 && height > 0 ? "\(width) × \(height)" : "unknown"
    }
    static func native(_ plan: EngineMobileGraphicsPlan) -> String {
        plan.native_width > 0 ? "Native · \(size(plan.native_width, plan.native_height))"
                              : "Native · measured when you play"
    }
    static func displayRate(_ plan: EngineMobileGraphicsPlan) -> String {
        plan.display_max_fps > 0 ? "Display maximum · requests \(plan.refreshForDisplayChoice) fps" : "Display maximum"
    }
    static func footer(next: EngineMobileGraphicsPlan, launched: EngineMobileGraphicsPlan?) -> String {
        var lines = ["Native matches your display’s shape at launch. Lower quality reduces detail while keeping that shape. Reopen the app after changing quality or resizing an iPad window."]
        if let launched {
            let hasFrame = launched.current_width > 0 && launched.current_height > 0
            let dimensions = hasFrame ? size(launched.current_width, launched.current_height)
                                      : size(launched.render_width, launched.render_height)
            lines.append("\(hasFrame ? "Current render" : "Requested render"): \(dimensions). Target: \(launched.effective_refresh) fps.")
        } else {
            lines.append("Next launch: \(size(next.render_width, next.render_height)), targeting \(next.effective_refresh) fps.")
        }
        lines.append("Actual frame rate depends on game and device performance.")
        return lines.joined(separator: " ")
    }
}

extension EngineMobileGraphicsPlan {
    /// The display-maximum request for this measurement, whatever choice is saved.
    var refreshForDisplayChoice: Int32 { halo_mobile_refresh(display_max_fps, Int32(HALO_MOBILE_RATE_DISPLAY), nil) }
}
