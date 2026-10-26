import SwiftUI

struct EnginePadSettingsView: View {
    @ObservedObject var assets: EngineAssetStore
    @ObservedObject var engine: EnginePadModel
    @ObservedObject var cloudSaves: EngineCloudSaveStore
    @Environment(\.dismiss) private var dismiss
    @AppStorage("HaloPadRenderResolution") private var resolution = "1600x1200"
    @State private var confirmStop = false

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Render resolution", selection: $resolution) {
                        ForEach(["1280x960", "1600x1200", "1920x1440", "2048x1536"], id: \.self) { value in
                            Text(value.replacingOccurrences(of: "x", with: " × ")).tag(value)
                        }
                    }
                    NavigationLink { EngineGraphicsModsView() } label: { Label("Graphics mods", systemImage: "sparkles") }
                } header: { Text("Graphics") } footer: {
                    Text("Changes apply the next time the app launches. Higher resolution trades performance for detail; the picture keeps Halo’s 4:3 layout.")
                }
                Section("Controller") {
                    Label(engine.controllerName ?? "Connect a Bluetooth controller", systemImage: "gamecontroller")
                    Text("Use the left stick to move, right stick to look, A to confirm and B to go back. The controller’s Menu button opens Halo’s pause menu.")
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
