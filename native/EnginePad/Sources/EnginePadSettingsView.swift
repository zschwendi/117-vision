import SwiftUI

struct EnginePadSettingsView: View {
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EnginePadModel
    @ObservedObject var cloudSaves: EngineCloudSaveStore
    @Environment(\.dismiss) private var dismiss
    @AppStorage("HaloPadRenderResolution") private var resolution = "1600x1200"
    @AppStorage("HaloPadFrameRate") private var frameRate = "display"
    @State private var confirmStop = false

    var body: some View {
        // The saved choices on this display now, and what this launch fixed
        // (mobile_graphics.h). Both are read on the main thread.
        let next = EnginePadGraphicsPlan.nextLaunch(), launched = EnginePadGraphicsPlan.launched()
        NavigationStack {
            Form {
                Section {
                    Picker("Render resolution", selection: $resolution) {
                        Text(EnginePadGraphicsText.native(next)).tag("native")
                        ForEach(EnginePadGraphicsText.presets, id: \.self) { value in
                            Text(value.replacingOccurrences(of: "x", with: " × ")).tag(value)
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
                    Text("Play with a controller, or with a keyboard to move and a mouse, trackpad or touch to aim. Touch only aims: it has no movement controls. On a controller, the left stick moves, the right stick looks, A confirms, B goes back and Menu opens Halo’s pause menu.")
                        .foregroundStyle(.secondary)
                    Button("Test rumble") { EngineHaptics.testPulse() }.disabled(engine.controllerName == nil)
                }
                Section("Your progress") {
                    NavigationLink {
                        EngineCloudSavesView(store: cloudSaves, gameRoot: assets.rootURL, engineActive: !engine.canStart || assets.phase != .ready)
                    } label: { Label("Cloud saves", systemImage: "icloud") }
                }
                Section(EnginePadBrand.productName) {
                    NavigationLink { EngineAboutView() } label: { Label("About", systemImage: "info.circle") }
                    if engine.isActive {
                        Button("Stop Halo", role: .destructive) { confirmStop = true }
                    }
                }
            }
            .navigationTitle("Settings").navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
            .confirmationDialog("Stop Halo?", isPresented: $confirmStop, titleVisibility: .visible) {
                Button("Stop Halo", role: .destructive) { engine.stop() }
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

    static func size(_ width: Int32, _ height: Int32) -> String {
        width > 0 && height > 0 ? "\(width) × \(height)" : "unknown"
    }
    static func native(_ plan: EngineMobileGraphicsPlan) -> String {
        plan.native_width > 0 ? "Native · fits this window: \(size(plan.native_width, plan.native_height))"
                              : "Native · window not measured (1600 × 1200)"
    }
    static func displayRate(_ plan: EngineMobileGraphicsPlan) -> String {
        plan.display_max_fps > 0 ? "Display maximum · requests \(plan.refreshForDisplayChoice) fps" : "Display maximum"
    }
    static func footer(next: EngineMobileGraphicsPlan, launched: EngineMobileGraphicsPlan?) -> String {
        var lines = ["Changes apply the next time the app launches. Native is the largest 4:3 picture that fits this window."]
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
