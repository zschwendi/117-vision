import SwiftUI
import GameController

struct EngineHaloControlsView: View {
    @ObservedObject var session: EngineAppSession
    @ObservedObject var engine: EngineRuntimeModel
    @ObservedObject var immersive: EngineImmersiveSession
    @State private var showingAdjustment = false
    @State private var showingSenseButtons = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    Label("Master Cheef", systemImage: "visionpro").font(.headline)
                    Spacer()
                    Button("Settings", systemImage: "gearshape") { session.showSettings() }
                        .senseControllerTarget("controls.settings")
                        .labelStyle(.iconOnly)
                        .accessibilityLabel("Halo settings")
                }
                Button("Resume Halo", systemImage: "play.fill", action: session.hideControls)
                    .buttonStyle(.borderedProminent)
                    .senseControllerTarget("controls.resume")
                Button("Recenter View", systemImage: "scope") { immersive.recenter() }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .frame(maxWidth: .infinity)
                    .disabled(!immersive.active)
                    .accessibilityHint("Places Halo ahead of your current seated or reclined position")
                    .senseControllerTarget("controls.recenter")
                HStack {
                    Button("Return to Library", systemImage: "square.grid.2x2") { session.request(.library) }
                        .senseControllerTarget("controls.library")
                    Button("Play in Window", systemImage: "macwindow") { session.request(.window) }
                        .disabled(!engine.isActive)
                        .senseControllerTarget("controls.window")
                }
                .buttonStyle(.bordered)
                .disabled(session.presentationBusy)
                if !immersive.active {
                    Button("Play Immersively", systemImage: "visionpro") { session.play(in: .immersive) }
                        .disabled(!session.canPlay)
                        .senseControllerTarget("controls.immersive")
                }
                Divider()
                EngineSenseModeControls(model: session.senseSettings) { immersive.recenter() }
                DisclosureGroup("Sense button guide", isExpanded: $showingSenseButtons) {
                    EngineSenseButtonGuide(mode: session.senseSettings.weaponMode, trackedAim: session.senseSettings.trackedAim)
                        .padding(.top, 10)
                }
                .senseControllerTarget("controls.guide")
                Divider()
                EngineDepthControls(model: session.settings)
                DisclosureGroup("Adjust View", isExpanded: $showingAdjustment) {
                    VStack(spacing: 10) {
                        HStack {
                            adjustmentButton("Move Halo left", symbol: "arrow.left") { immersive.nudgeOrientation(yawDegrees: -5) }
                            adjustmentButton("Move Halo down", symbol: "arrow.down") { immersive.nudgeOrientation(pitchDegrees: -5) }
                            adjustmentButton("Move Halo up", symbol: "arrow.up") { immersive.nudgeOrientation(pitchDegrees: 5) }
                            adjustmentButton("Move Halo right", symbol: "arrow.right") { immersive.nudgeOrientation(yawDegrees: 5) }
                        }
                        HStack {
                            Text("\(Int(immersive.orientationYawDegrees))° horizontal · \(Int(immersive.orientationPitchDegrees))° vertical")
                                .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                            Spacer()
                            Button("Reset") { immersive.resetOrientation() }.senseControllerTarget("controls.reset")
                                .accessibilityLabel("Reset view adjustments")
                        }
                    }
                    .padding(.top, 10)
                    .disabled(!immersive.active)
                }
                .senseControllerTarget("controls.adjust")
                Text("Use the left stick to choose or adjust, Cross to select, and Circle to resume Halo. Create + Options reopens these controls. Sense gameplay input is held while app controls are open; Halo itself keeps running.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(22)
        }
        .senseControllerNavigation(controllerItems, back: session.hideControls)
        .frame(width: 430, height: 640)
        .tint(.cyan)
        .preferredColorScheme(.dark)
        // Explicit controller selection owns Cross here; system gaze/pinch
        // remains available independently.
        .handlesGameControllerEvents(matching: .gamepad)
    }

    private var controllerItems: [EngineControllerMenuItem] {
        var items: [EngineControllerMenuItem] = [
            .init(id: "controls.resume", title: "Resume Halo", activate: session.hideControls),
            .init(id: "controls.settings", title: "Settings", activate: session.showSettings),
            .init(id: "controls.recenter", title: "Recenter View", enabled: immersive.active, activate: { immersive.recenter() }),
            .init(id: "controls.library", title: "Return to Library", enabled: !session.presentationBusy, activate: { session.request(.library) }),
            .init(id: "controls.window", title: "Play in Window", enabled: engine.isActive && !session.presentationBusy, activate: { session.request(.window) })
        ]
        if !immersive.active {
            items.append(.init(id: "controls.immersive", title: "Play Immersively", enabled: session.canPlay,
                               activate: { session.play(in: .immersive) }))
        }
        items += session.senseSettings.controllerItems { immersive.recenter() }
        items.append(.init(id: "controls.guide", title: "Sense button guide", value: "\(showingSenseButtons)", activate: { showingSenseButtons.toggle() }))
        items += session.settings.controllerDepthItems()
        items.append(.init(id: "controls.adjust", title: "Adjust View", value: "\(showingAdjustment)", activate: { showingAdjustment.toggle() }))
        if showingAdjustment { items += [
            .init(id: "controls.arrow.left", title: "Move left", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: -5) }),
            .init(id: "controls.arrow.down", title: "Move down", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: -5) }),
            .init(id: "controls.arrow.up", title: "Move up", enabled: immersive.active, activate: { immersive.nudgeOrientation(pitchDegrees: 5) }),
            .init(id: "controls.arrow.right", title: "Move right", enabled: immersive.active, activate: { immersive.nudgeOrientation(yawDegrees: 5) }),
            .init(id: "controls.reset", title: "Reset view", enabled: immersive.active, activate: { immersive.resetOrientation() })
        ] }
        return items
    }

    private func adjustmentButton(_ label: String, symbol: String, action: @escaping () -> Void) -> some View {
        Button(label, systemImage: symbol, action: action)
            .labelStyle(.iconOnly)
            .buttonStyle(.bordered)
            .accessibilityLabel(label)
            .accessibilityHint("Adjusts the view by five degrees")
            .senseControllerTarget("controls." + symbol)
    }
}
