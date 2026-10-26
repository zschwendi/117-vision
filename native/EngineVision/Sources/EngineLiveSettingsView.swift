import SwiftUI

struct EngineDepthControls: View {
    @ObservedObject var model: EngineSettingsModel
    var body: some View {
        Toggle("3D depth", isOn: $model.stereo).senseControllerTarget("depth.stereo")
        if model.stereo {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Eye separation")
                    Spacer()
                    Text("\(Int(model.separationMillimetres)) mm").monospacedDigit().foregroundStyle(.secondary)
                }
                Slider(value: $model.separationMillimetres, in: 45...80, step: 1)
                    .accessibilityLabel("3D eye separation")
                    .accessibilityValue("\(Int(model.separationMillimetres)) millimetres")
                    .senseControllerTarget("depth.separation")
            }
        }
    }
}

struct EngineLiveSettingsView: View {
    @ObservedObject var model: EngineSettingsModel
    @State private var advanced = false
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        List {
            #if os(visionOS)
            Section {
                EngineDepthControls(model: model)
            } header: { Text("Depth") } footer: {
                Text("Stereo renders a separate view for each eye. Lower separation makes depth subtler; turning stereo off can improve performance.")
            }
            Section {
                valueSlider("Vertical coverage", value: $model.verticalFieldDegrees, range: 90...175, step: 5, suffix: "°")
                valueSlider("Surround brightness", value: $model.surroundBrightness, range: 0...2, step: 0.05, suffix: "×", decimals: 2)
                Toggle("Spatial game menus", isOn: $model.spatialMenu).senseControllerTarget("live.spatial")
            } header: { Text("View") } footer: {
                Text("Wider coverage shows more sky and floor but spreads the same pixels over a larger view. The default is 105°.")
            }
            #endif
            Section("Sound & Controller") {
                valueSlider("Your weapon volume boost", value: $model.ownWeaponDecibels, range: 0...24, step: 1, suffix: " dB")
                valueSlider("Controller vibration", value: $model.hapticStrength, range: 0...2, step: 0.05, suffix: "×", decimals: 2)
                Button("Test Vibration") { EngineHaptics.testPulse() }.senseControllerTarget("live.test")
            }
            #if os(visionOS)
            Section {
                DisclosureGroup("Advanced", isExpanded: $advanced) {
                    valueSlider("Surround refresh budget", value: $model.frameRateTarget, range: 20...30, step: 1, suffix: " fps")
                    Text("This is a performance goal for side and rear views, not a frame-rate cap. Lower goals allow more surround updates.")
                        .font(.footnote).foregroundStyle(.secondary)
                    Toggle("Even frame cadence", isOn: $model.framePacing).senseControllerTarget("live.pacing")
                    Text("Halo’s Video Setup controls its original 30 fps limit. Its Sync option does not synchronize to the headset display. Even frame cadence uses the native presentation scheduler.")
                        .font(.footnote).foregroundStyle(.secondary)
                    Toggle("Align older side views", isOn: $model.layerAlign).senseControllerTarget("live.align")
                    Text("Experimental: side-view alignment can bend the picture or sharpen seams during turns. Both options start off.")
                        .font(.footnote).foregroundStyle(.secondary)
                    Toggle("Head pointer in menus", isOn: $model.gazePointer).senseControllerTarget("live.pointer")
                }
                .senseControllerTarget("live.advanced")
            }
            #endif
        }
        .senseControllerNavigation(controllerItems, back: { dismiss() })
        .navigationTitle("Depth, View & Sound")
    }

    private func valueSlider(_ label: String, value: Binding<Double>, range: ClosedRange<Double>, step: Double, suffix: String, decimals: Int = 0) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(label)
                Spacer()
                Text(value.wrappedValue.formatted(.number.precision(.fractionLength(decimals))) + suffix)
                    .monospacedDigit().foregroundStyle(.secondary)
            }
            Slider(value: value, in: range, step: step).accessibilityLabel(label)
        }.padding(.vertical, 4).senseControllerTarget("live." + label)
    }
    private var controllerItems: [EngineControllerMenuItem] {
        var items = model.controllerDepthItems()
        items += [
            slider("Vertical coverage", $model.verticalFieldDegrees, 90...175, 5),
            slider("Surround brightness", $model.surroundBrightness, 0...2, 0.05),
            toggle("spatial", "Spatial game menus", $model.spatialMenu),
            slider("Your weapon volume boost", $model.ownWeaponDecibels, 0...24, 1),
            slider("Controller vibration", $model.hapticStrength, 0...2, 0.05),
            .init(id: "live.test", title: "Test vibration", activate: { EngineHaptics.testPulse() }),
            .init(id: "live.advanced", title: "Advanced", value: "\(advanced)", activate: { advanced.toggle() })
        ]
        if advanced { items += [
            slider("Surround refresh budget", $model.frameRateTarget, 20...30, 1),
            toggle("pacing", "Even frame cadence", $model.framePacing),
            toggle("align", "Align older side views", $model.layerAlign),
            toggle("pointer", "Head pointer in menus", $model.gazePointer)
        ] }
        return items
    }
    private func slider(_ title: String, _ value: Binding<Double>, _ range: ClosedRange<Double>, _ step: Double) -> EngineControllerMenuItem {
        .init(id: "live." + title, title: title, value: "\(value.wrappedValue)", activate: {},
              adjust: { value.wrappedValue = min(range.upperBound, max(range.lowerBound, value.wrappedValue + Double($0) * step)) })
    }
    private func toggle(_ id: String, _ title: String, _ value: Binding<Bool>) -> EngineControllerMenuItem {
        .init(id: "live." + id, title: title, value: "\(value.wrappedValue)", activate: { value.wrappedValue.toggle() },
              adjust: { value.wrappedValue = $0 > 0 })
    }
}

@MainActor
extension EngineSettingsModel {
    func controllerDepthItems() -> [EngineControllerMenuItem] {
        var items: [EngineControllerMenuItem] = [
            .init(id: "depth.stereo", title: "3D depth", value: "\(stereo)", activate: { self.stereo.toggle() },
                  adjust: { self.stereo = $0 > 0 })
        ]
        if stereo { items.append(.init(id: "depth.separation", title: "Eye separation", value: "\(separationMillimetres)",
            activate: {}, adjust: { self.separationMillimetres = min(80, max(45, self.separationMillimetres + Double($0))) })) }
        return items
    }
}
