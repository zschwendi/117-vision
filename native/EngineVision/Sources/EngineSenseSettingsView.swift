import SwiftUI

enum EngineSenseTurnMode: Int, CaseIterable, Identifiable {
    case pointToTurn = 2
    case smoothStick = 1
    case physical = 0
    case snap30 = 30
    case snap45 = 45

    var id: Int { rawValue }
    var label: String {
        switch self {
        case .pointToTurn: "Point-to-turn + stick"
        case .smoothStick: "Smooth stick"
        case .physical: "Physical turning only"
        case .snap30: "Physical + 30° snap turns"
        case .snap45: "Physical + 45° snap turns"
        }
    }
}

enum EngineSenseWeaponMode: String, CaseIterable, Identifiable {
    case single, dual
    var id: String { rawValue }
    var label: String { self == .single ? "Single weapon" : "Dual wielding" }
}

@MainActor
final class EngineSenseSettingsModel: ObservableObject {
    private let defaults: UserDefaults
    private static let turnKey = "MasterCheefSenseTurnMode.v2"
    private static let legacyTurnKey = "MasterCheefSenseTurnMode.v1"
    private static let speedKey = "MasterCheefSenseTurnSpeed.v1"
    private static let boundaryKey = "MasterCheefSenseTurnBoundary.v1"
    private static func bounded(_ value: Double, _ range: ClosedRange<Double>, fallback: Double) -> Double {
        value.isFinite ? min(range.upperBound, max(range.lowerBound, value)) : fallback
    }
    private static let aimKey = "MasterCheefSenseTrackedAim.v1"
    // Build 111 enabled dual wielding by default. The new mode intentionally
    // starts with one rifle; that old default was not a choice to carry two.
    private static let weaponModeKey = "MasterCheefSenseWeaponMode.v2"
    private static let visibilityKey = "MasterCheefSenseRealHandsVisible.v1"
    private static let gesturesKey = "MasterCheefSenseQuickGestures.v1"
    private static let fullBodyKey = "MasterCheefSenseFullBody.v1"
    /// Experimental; off unless the player chooses it.
    @Published var fullBody: Bool {
        didSet {
            halo_sense_set_body_enabled(fullBody ? 1 : 0)
            defaults.set(fullBody, forKey: Self.fullBodyKey)
        }
    }
    private static let gripContinuityKey = "MasterCheefSenseGripContinuity.v1"
    private static let scopedSmoothingKey = "MasterCheefSenseScopedSmoothing.v1"
    private static let weaponHapticsKey = "MasterCheefSenseWeaponHaptics.v1"
    /// Eases a rifle into and out of two-handed support (default on).
    @Published var gripContinuity: Bool {
        didSet {
            halo_sense_set_grip_continuity(gripContinuity ? 1 : 0)
            defaults.set(gripContinuity, forKey: Self.gripContinuityKey)
        }
    }
    /// Filters hand tremor from the aim while zoomed (default off).
    @Published var scopedSmoothing: Bool {
        didSet {
            halo_sense_set_scope_smoothing(scopedSmoothing ? 1 : 0)
            defaults.set(scopedSmoothing, forKey: Self.scopedSmoothingKey)
        }
    }
    /// Shot and charge feedback on the firing Sense controller (default on).
    /// The master haptics strength still applies; ordinary gamepads are unaffected.
    @Published var trackedWeaponHaptics: Bool {
        didSet {
            halo_sense_set_weapon_haptics(trackedWeaponHaptics ? 1 : 0)
            defaults.set(trackedWeaponHaptics, forKey: Self.weaponHapticsKey)
        }
    }
    @Published var quickGestures: Bool {
        didSet {
            halo_sense_set_gestures_enabled(quickGestures ? 1 : 0)
            defaults.set(quickGestures, forKey: Self.gesturesKey)
        }
    }
    @Published var showHandsAndControllers: Bool {
        didSet { defaults.set(showHandsAndControllers, forKey: Self.visibilityKey) }
    }
    @Published var trackedAim: Bool {
        didSet {
            halo_sense_set_enabled(trackedAim ? 1 : 0)
            defaults.set(trackedAim, forKey: Self.aimKey)
        }
    }
    @Published var turnMode: EngineSenseTurnMode {
        didSet {
            halo_sense_set_turn_mode(Int32(turnMode.rawValue))
            defaults.set(turnMode.rawValue, forKey: Self.turnKey)
        }
    }
    @Published var turnSpeed: Double {
        didSet {
            let validated = Self.bounded(turnSpeed, 30...120, fallback: 90)
            if turnSpeed != validated { turnSpeed = validated; return }
            halo_sense_set_turn_speed(Float(turnSpeed))
            defaults.set(turnSpeed, forKey: Self.speedKey)
        }
    }
    @Published var turnBoundary: Double {
        didSet {
            let validated = Self.bounded(turnBoundary, 0.50...0.90, fallback: 0.72)
            if turnBoundary != validated { turnBoundary = validated; return }
            halo_sense_set_turn_boundary(Float(turnBoundary))
            defaults.set(turnBoundary, forKey: Self.boundaryKey)
        }
    }
    @Published var weaponMode: EngineSenseWeaponMode {
        didSet {
            halo_sense_set_dual_enabled(weaponMode == .dual ? 1 : 0)
            defaults.set(weaponMode.rawValue, forKey: Self.weaponModeKey)
        }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        quickGestures = defaults.object(forKey: Self.gesturesKey) as? Bool ?? true
        fullBody = defaults.object(forKey: Self.fullBodyKey) as? Bool ?? false
        gripContinuity = defaults.object(forKey: Self.gripContinuityKey) as? Bool ?? true
        scopedSmoothing = defaults.object(forKey: Self.scopedSmoothingKey) as? Bool ?? false
        trackedWeaponHaptics = defaults.object(forKey: Self.weaponHapticsKey) as? Bool ?? true
        showHandsAndControllers = defaults.object(forKey: Self.visibilityKey) == nil
            ? true : defaults.bool(forKey: Self.visibilityKey)
        trackedAim = defaults.object(forKey: Self.aimKey) == nil ? true : defaults.bool(forKey: Self.aimKey)
        // Replace the old snap default once, preserving an explicit physical
        // choice. Later v2 choices, including snap turning, always take priority.
        if let saved = defaults.object(forKey: Self.turnKey) as? Int {
            turnMode = EngineSenseTurnMode(rawValue: saved) ?? .pointToTurn
        } else {
            let legacy = defaults.object(forKey: Self.legacyTurnKey) as? Int
            turnMode = legacy == EngineSenseTurnMode.physical.rawValue ? .physical : .pointToTurn
        }
        turnSpeed = Self.bounded(defaults.object(forKey: Self.speedKey) as? Double ?? 90,
                                 30...120, fallback: 90)
        turnBoundary = Self.bounded(defaults.object(forKey: Self.boundaryKey) as? Double ?? 0.72,
                                    0.50...0.90, fallback: 0.72)
        weaponMode = defaults.string(forKey: Self.weaponModeKey)
            .flatMap(EngineSenseWeaponMode.init(rawValue:)) ?? .single
        defaults.set(turnMode.rawValue, forKey: Self.turnKey)
        defaults.set(turnSpeed, forKey: Self.speedKey)
        defaults.set(turnBoundary, forKey: Self.boundaryKey)
        halo_sense_set_turn_speed(Float(turnSpeed))
        halo_sense_set_turn_boundary(Float(turnBoundary))
        halo_sense_set_enabled(trackedAim ? 1 : 0)
        halo_sense_set_turn_mode(Int32(turnMode.rawValue))
        halo_sense_set_dual_enabled(weaponMode == .dual ? 1 : 0)
        halo_sense_set_gestures_enabled(quickGestures ? 1 : 0)
        halo_sense_set_body_enabled(fullBody ? 1 : 0)
        halo_sense_set_grip_continuity(gripContinuity ? 1 : 0)
        halo_sense_set_scope_smoothing(scopedSmoothing ? 1 : 0)
        halo_sense_set_weapon_haptics(trackedWeaponHaptics ? 1 : 0)
    }

    /// Remote settings are applied only at the idle launcher, never mid-game.
    func reloadFromDefaults() {
        quickGestures = defaults.object(forKey: Self.gesturesKey) as? Bool ?? true
        fullBody = defaults.object(forKey: Self.fullBodyKey) as? Bool ?? false
        gripContinuity = defaults.object(forKey: Self.gripContinuityKey) as? Bool ?? true
        scopedSmoothing = defaults.object(forKey: Self.scopedSmoothingKey) as? Bool ?? false
        trackedWeaponHaptics = defaults.object(forKey: Self.weaponHapticsKey) as? Bool ?? true
        showHandsAndControllers = defaults.object(forKey: Self.visibilityKey) as? Bool ?? true
        trackedAim = defaults.object(forKey: Self.aimKey) as? Bool ?? true
        turnMode = (defaults.object(forKey: Self.turnKey) as? Int).flatMap(EngineSenseTurnMode.init(rawValue:)) ?? .pointToTurn
        turnSpeed = Self.bounded(defaults.object(forKey: Self.speedKey) as? Double ?? 90, 30...120, fallback: 90)
        turnBoundary = Self.bounded(defaults.object(forKey: Self.boundaryKey) as? Double ?? 0.72, 0.50...0.90, fallback: 0.72)
        weaponMode = defaults.string(forKey: Self.weaponModeKey).flatMap(EngineSenseWeaponMode.init(rawValue:)) ?? .single
    }

}

/// One Sense-controller navigation row, independent of SwiftUI so the same
/// rows drive both app panels and can be validated off the headset.
struct EngineSenseControlRow {
    var id: String
    var title: String
    var value: String
    var enabled = true
    var activate: () -> Void
    var adjust: (Int) -> Void
}

@MainActor
extension EngineSenseSettingsModel {
    func controlRows(recenter: @escaping () -> Void) -> [EngineSenseControlRow] {
        var rows: [EngineSenseControlRow] = [
            .init(id: "sense.aim", title: "Tracked aiming", value: "\(trackedAim)", activate: {
                self.trackedAim.toggle(); if !self.trackedAim { recenter() }
            }, adjust: { direction in
                self.trackedAim = direction > 0; if !self.trackedAim { recenter() }
            })
        ]
        if trackedAim {
            rows += [
                .init(id: "sense.turning", title: "Turning", value: turnMode.label,
                      activate: { self.cycleTurn(1) }, adjust: { self.cycleTurn($0) }),
                .init(id: "sense.weapons", title: "Weapons", value: weaponMode.label,
                      activate: { self.weaponMode = self.weaponMode == .single ? .dual : .single },
                      adjust: { self.weaponMode = $0 > 0 ? .dual : .single }),
                .init(id: "sense.body", title: "Body & shadows (experimental)", value: "\(fullBody)",
                      activate: { self.fullBody.toggle() }, adjust: { self.fullBody = $0 > 0 }),
                .init(id: "sense.grip", title: "Two-hand grip smoothing", value: "\(gripContinuity)",
                      activate: { self.gripContinuity.toggle() }, adjust: { self.gripContinuity = $0 > 0 }),
                .init(id: "sense.scope", title: "Scope steadying", value: "\(scopedSmoothing)",
                      activate: { self.scopedSmoothing.toggle() }, adjust: { self.scopedSmoothing = $0 > 0 }),
                .init(id: "sense.weaponHaptics", title: "Tracked weapon haptics", value: "\(trackedWeaponHaptics)",
                      activate: { self.trackedWeaponHaptics.toggle() }, adjust: { self.trackedWeaponHaptics = $0 > 0 })
            ]
        }
        rows.append(.init(id: "sense.hands", title: "Show hands & controllers", value: "\(showHandsAndControllers)",
                          activate: { self.showHandsAndControllers.toggle() }, adjust: { self.showHandsAndControllers = $0 > 0 }))
        return rows
    }
    /// The saved value is the tilt speed; turning is twice it.
    var stickSpeedSummary: String {
        "Turn \(Int(turnSpeed) * 2)°/s · Tilt \(Int(turnSpeed))°/s"
    }
    private func cycleTurn(_ direction: Int) {
        let modes = EngineSenseTurnMode.allCases
        let index = modes.firstIndex(of: turnMode) ?? 0
        turnMode = modes[(index + (direction < 0 ? modes.count - 1 : 1)) % modes.count]
    }
}

#if os(visionOS)
struct EngineSenseSettingsView: View {
    @ObservedObject var model: EngineSenseSettingsModel
    @ObservedObject private var physical = EngineSensePhysicalSettingsModel.shared
    var recenter: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        Form {
            Section {
                EngineSenseModeControls(model: model, recenter: recenter)
                Button("Recenter View", systemImage: "scope", action: recenter)
                    .senseControllerTarget("sense.recenter")
            } header: { Text("Controllers") }
            Section("Turning feel") {
                HStack {
                    Text("Stick speed")
                    Spacer()
                    Text(model.stickSpeedSummary).foregroundStyle(.secondary)
                }
                Slider(value: $model.turnSpeed, in: 30...120, step: 5) {
                    Text("Stick speed")
                }
                .disabled(!model.trackedAim || (model.turnMode != .smoothStick && model.turnMode != .pointToTurn))
                .senseControllerTarget("sense.speed")
                Text("Right stick left or right turns at up to \(Int(model.turnSpeed) * 2)°/s. Up or down tilts your view at up to \(Int(model.turnSpeed))°/s, at most 45° either way; Recenter View levels it. Point-to-turn turns at up to \((model.turnSpeed / 2).formatted())°/s.")
                    .font(.caption).foregroundStyle(.secondary)
                HStack {
                    Text("Point-turn boundary")
                    Spacer()
                    Text("\(Int((model.turnBoundary * 100).rounded()))%").foregroundStyle(.secondary)
                }
                Slider(value: $model.turnBoundary, in: 0.50...0.90, step: 0.01) {
                    Text("Point-turn boundary")
                }
                .disabled(!model.trackedAim || model.turnMode != .pointToTurn)
                .senseControllerTarget("sense.boundary")
                Text("A higher value lets you aim farther from the center before turning starts.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section("Immersive aim & movement") {
                Text(model.trackedAim
                     ? "Aim with your right Sense controller. Bring your left hand forward toward a supported rifle's foregrip to steady it with both hands; move it away to aim with one hand. Your head moves the view independently."
                     : "Tracked aiming is off. Your Sense controllers use the sticks to aim and move, with L1 for grenades and L2 or R3 for zoom.")
                EngineSenseButtonGuide(mode: model.weaponMode, trackedAim: model.trackedAim)
            }
            Section("Quick gestures") {
                Toggle("Quick gestures", isOn: $model.quickGestures)
                    .senseControllerTarget("sense.gestures")
                Text("Start with your hands comfortably in front, release the triggers and buttons, and leave the right stick centered. You can keep moving with the left stick. Melee and weapon switching work without holding Create.")
                    .font(.caption).foregroundStyle(.secondary)
                LabeledContent("Melee", value: "Make a firm forward, sideways or downward swing with either hand. Your other hand can keep supporting the rifle.")
                LabeledContent("Switch weapon", value: "Reach your right hand beside your shoulder and pause briefly to switch. Return it in front to ready the next gesture.")
                LabeledContent("Reload", value: "Hold Create and tap near the right gun's grip twice with your left hand, pulling away between taps.")
                LabeledContent("Pick up weapon", value: "Hold Create, look at a nearby weapon and reach your left hand forward.")
                Text("After a strike or swap, settle your hands in front before another action. Slow aiming movements do not count as strikes. Pause beside your visible shoulder before reaching behind your back. Losing tracking cancels any undelivered action.")
                    .font(.caption).foregroundStyle(.secondary)
                Text("Melee uses Halo's normal animation, range and damage. Reload uses its normal animation and reloads the right gun only; Square still reloads both in Dual wielding. Pickup uses the game's normal targeting and range. All button controls remain available.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            EngineSensePhysicalSection(model: physical)
            Section("Setup") {
                Text("Pair one left and one right PS VR2 Sense controller in visionOS Settings → Bluetooth. Allow accessory tracking when Master Cheef asks.")
                Text("Use Recenter View in Halo Controls whenever you change your seated or standing position.")
                Text("Automatic support is available for the assault rifle, sniper rifle, shotgun, plasma rifle, rocket launcher and flamethrower when their original weapon animation is ready. Individual weapon feel still needs headset testing.")
                Text("If a controller is still hidden, check visionOS Settings → Awareness & Safety → Reveal Game Controllers → Always.")
            }
        }
        .senseControllerNavigation(controllerItems, back: { dismiss() })
        .navigationTitle("Sense Controls")
        .tint(.cyan)
    }
    private var controllerItems: [EngineControllerMenuItem] {
        model.controllerItems(recenter: recenter) + [
            .init(id: "sense.recenter", title: "Recenter View", activate: recenter),
            .init(id: "sense.speed", title: "Stick speed", value: model.stickSpeedSummary,
                  enabled: model.trackedAim && (model.turnMode == .smoothStick || model.turnMode == .pointToTurn),
                  activate: {}, adjust: { model.turnSpeed = min(120, max(30, model.turnSpeed + Double($0) * 5)) }),
            .init(id: "sense.boundary", title: "Point-turn boundary", value: "\(model.turnBoundary)",
                  enabled: model.trackedAim && model.turnMode == .pointToTurn,
                  activate: {}, adjust: { model.turnBoundary = min(0.9, max(0.5, model.turnBoundary + Double($0) * 0.01)) }),
            .init(id: "sense.gestures", title: "Quick gestures", value: "\(model.quickGestures)",
                  activate: { model.quickGestures.toggle() }, adjust: { model.quickGestures = $0 > 0 })
        ] + physical.controllerItems
    }
}

/// Shared by the in-game controls panel and the full Sense settings window.
struct EngineSenseTurningControls: View {
    @ObservedObject var model: EngineSenseSettingsModel

    var body: some View {
        Picker("Turning", selection: $model.turnMode) {
            ForEach(EngineSenseTurnMode.allCases) { mode in
                Text(mode.label).tag(mode)
            }
        }
        .pickerStyle(.menu)
        .senseControllerTarget("sense.turning")
        Text(help).font(.caption).foregroundStyle(.secondary)
        Text("Physical 360° turning is available in every mode. After changing modes or recovering tracking, center the stick and your aim to resume turning.")
            .font(.caption).foregroundStyle(.secondary)
    }

    private var help: String {
        switch model.turnMode {
        case .pointToTurn:
            "Hold the right weapon's reticle near either side of your view briefly to turn smoothly. Bring it back toward the center to stop. The right stick takes over when you use it: left or right turns faster, up or down tilts your view."
        case .smoothStick:
            "Push the right stick left or right to turn smoothly, or up or down to tilt your view, while aiming freely. Center the stick to stop."
        case .physical:
            "Turn your head and body to look around. The right stick does not turn or tilt your view."
        case .snap30, .snap45:
            "Flick the right stick left or right to turn by \(model.turnMode.rawValue)°. Center it before turning again. Up and down do not tilt your view."
        }
    }
}

struct EngineSenseModeControls: View {
    @ObservedObject var model: EngineSenseSettingsModel
    var recenter: (() -> Void)?

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Tracked aiming", isOn: $model.trackedAim)
                .senseControllerTarget("sense.aim")
                .onChange(of: model.trackedAim) { _, enabled in
                    if !enabled { recenter?() }
                }
            Text("Experimental: on foot in campaign. Switch off for vehicle or multiplayer controls; the right stick then aims and turns as a gamepad.")
                .font(.caption).foregroundStyle(.secondary)
            if model.trackedAim {
                EngineSenseTurningControls(model: model)
                Picker("Weapons", selection: $model.weaponMode) {
                    ForEach(EngineSenseWeaponMode.allCases) { mode in
                        Text(mode.label).tag(mode)
                    }
                }
                .pickerStyle(.segmented)
                .senseControllerTarget("sense.weapons")
                Text(model.weaponMode == .single
                     ? "Bring your left hand forward toward the rifle's foregrip for two-handed support. Move it away for one-handed aiming. The pistol stays one-handed. Hold L1, make a throwing motion, then release L1 for a grenade; L2 is reserved for a second gun."
                     : "Carry an assault rifle and pistol to hold one in each hand. R2 and L2 fire independently; Square reloads both. Without that pair, a supported rifle can still use two-handed support. Hold L1, make a throwing motion, then release L1 for a grenade; L2 does nothing until a second gun is ready.")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("Two-hand grip smoothing", isOn: $model.gripContinuity)
                    .senseControllerTarget("sense.grip")
                Text("Eases the rifle into and out of two-handed support over a fifth of a second. Off snaps straight to your hands.")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("Scope steadying", isOn: $model.scopedSmoothing)
                    .senseControllerTarget("sense.scope")
                Text("While zoomed, filters small hand tremor from the weapon's aim. The scope view, your shots and the reticle follow the same steadied aim.")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("Tracked weapon haptics", isOn: $model.trackedWeaponHaptics)
                    .senseControllerTarget("sense.weaponHaptics")
                Text("Feel each shot the game fires, and plasma charge, on the controller that fired it. Uses the Haptics strength setting; off returns to sound-based feedback.")
                    .font(.caption).foregroundStyle(.secondary)
                Text(model.quickGestures
                     ? "Quick gestures on: swing to melee; reach to your shoulder to switch weapons. No button is needed. Hold Create for reload and pickup gestures."
                     : "Quick gestures off. Button controls remain available. Enable gestures in Sense Controls.")
                    .font(.caption).foregroundStyle(.secondary)
                Toggle("Body & shadows (experimental)", isOn: $model.fullBody)
                    .senseControllerTarget("sense.body")
                Text("Shows Master Chief's body, legs and shadow when you look down. Arms follow your hand positions; the head and wrist roll keep the game's animation, and weapon shadows are not included. Dual wielding keeps the usual first-person hands and weapons. On-foot campaign only; your view, aim and shots are unchanged. Not yet tested on a headset.")
                    .font(.caption).foregroundStyle(.secondary)
                TimelineView(.periodic(from: .now, by: 0.5)) { _ in
                    let tracking = EngineSenseTrackingStatus.shared.snapshot()
                    Label(tracking.summary, systemImage: tracking.left.tracked && tracking.right.tracked ? "checkmark.circle" : "sensor")
                        .font(.caption)
                        .foregroundStyle(tracking.left.tracked && tracking.right.tracked ? Color.cyan : Color.secondary)
                    if tracking.spatialControllers > 0 || tracking.framesQueried > 0 {
                        Text("Left: \(tracking.left.state) · Right: \(tracking.right.state)")
                            .font(.caption2).foregroundStyle(.secondary)
                    }
                    if tracking.left.tracked && tracking.right.tracked && halo_sense_active() != 0 {
                        let message = weaponStatusMessage()
                        if let message {
                            Label(message, systemImage: model.weaponMode == .dual ? "gamecontroller" : "hand.raised")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    if halo_sense_active() != 0 && enginevision_menu_active() == 0 {
                        if halo_sense_native_controls() != 0 {
                            Label("Vehicle controls active. Right stick aims; R2 fires mounted weapons. Tracked weapon aiming resumes when you get out.", systemImage: "gamecontroller")
                                .font(.caption).foregroundStyle(.secondary)
                        } else if halo_sense_gameplay_supported() == 0 {
                            Label("Tracked controls are unavailable here. Switch off to use gamepad controls.", systemImage: "gamecontroller")
                                .font(.caption).foregroundStyle(.orange)
                        }
                    }
                }
            }
            Toggle("Show real hands & controllers", isOn: $model.showHandsAndControllers)
                .senseControllerTarget("sense.hands")
            Text("Let visionOS show your hands and Sense controllers over the game.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private func weaponStatusMessage() -> String? {
        var status = HaloSenseWeaponStatus()
        host_sense_weapon_status(&status)
        if model.weaponMode == .single {
            switch Int(status.support) {
            case HALO_SENSE_SUPPORT_READY: return "Bring your left hand forward to support this rifle."
            case HALO_SENSE_SUPPORT_HELD: return "Two-handed rifle support active."
            case HALO_SENSE_SUPPORT_WAITING_POSE: return "Waiting for the rifle to finish its animation."
            case HALO_SENSE_SUPPORT_INVALID_SPAN: return "Adjust the spacing between your hands to engage support."
            default: return nil
            }
        }
        switch Int(status.dual) {
        case HALO_SENSE_DUAL_WAITING_TRACKING: return "Waiting for current controller poses."
        case HALO_SENSE_DUAL_NO_PLAYER: return "Enter an on-foot campaign to use two weapons."
        case HALO_SENSE_DUAL_UNSUPPORTED_PRIMARY, HALO_SENSE_DUAL_UNSUPPORTED_SECONDARY:
            return "Dual wielding needs an assault rifle and pistol."
        case HALO_SENSE_DUAL_NO_SECOND_WEAPON: return "Pick up an assault rifle and pistol to use dual wielding."
        case HALO_SENSE_DUAL_SECONDARY_BUSY: return "Waiting for the second weapon to finish its action."
        case HALO_SENSE_DUAL_AMBIGUOUS: return "Cannot choose a second weapon from this inventory."
        case HALO_SENSE_DUAL_INVALID_ATTACHMENT: return "The second weapon is not ready to equip."
        case HALO_SENSE_DUAL_PREPARING_MODEL: return "Preparing the second weapon…"
        case HALO_SENSE_DUAL_READY: return "Both weapons ready."
        default: return nil
        }
    }
}

/// These are the physical Sense buttons, not a DualSense/D-pad layout.
struct EngineSenseButtonGuide: View {
    let mode: EngineSenseWeaponMode
    var trackedAim = true

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            row("Create + Options", "Open app controls; Circle returns to Halo")
            row("Flashlight (Q)", "Hold left Create + left stick up")
            row("Left stick", trackedAim ? "Move where you are looking" : "Move")
            row("Right stick", trackedAim ? "Turn using the selected mode; smooth modes also tilt up and down" : "Look and aim")
            row("R2", "Fire right weapon")
            row("L1", trackedAim ? "Hold, throw with your left hand, then release" : "Throw grenade")
            row("L2", trackedAim ? "Fire left weapon when a dual pair is ready" : "Zoom")
            if trackedAim { row("Left hand forward", "Support a rifle when not holding a second gun") }
            row("R1", "Melee")
            row("Square", trackedAim && mode == .dual ? "Use / pick up / reload both" : "Use / pick up / reload")
            row("Triangle", "Switch equipped weapon")
            row("Cross", "Jump / confirm")
            row("Circle or L3", "Crouch; Circle goes back in menus")
            row("R3", "Zoom")
            row("Options", "Pause")
            if trackedAim {
                Text("Hold L1, make a throwing motion with your left hand, then release L1. The grenade leaves from that hand in the direction it was moving; throwing at 5 m/s or faster gives Halo's normal full throw, and slower throws are proportionally gentler. Releasing a still hand drops the grenade. Halo's throw animation still plays first, so the grenade leaves a moment after release. Keep the controller visible through release.")
                    .font(.caption).foregroundStyle(.secondary)
                Text("Melee temporarily uses Halo's original weapon animation. While zoomed, the scope stays centered and aiming follows your weapon, including two-handed support.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Text("Other left Create + left stick shortcuts: down to change grenade, left to reload, right to pick up a weapon.")
                .font(.caption).foregroundStyle(.secondary)
            Text(trackedAim
                 ? "With Quick gestures on, hold Create and click R3 to show the scoreboard. With gestures off, hold Create with the left stick centered."
                 : "Hold Create with the left stick centered to show the scoreboard.")
                .font(.caption).foregroundStyle(.secondary)
            Text("Sense has no D-pad or touchpad. L3 and R3 mean clicking the sticks.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private func row(_ input: String, _ action: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(input).fontWeight(.medium).frame(width: 105, alignment: .leading)
            Text(action).foregroundStyle(.secondary).frame(maxWidth: .infinity, alignment: .leading)
        }
        .font(.subheadline)
        .fixedSize(horizontal: false, vertical: true)
    }
}

@MainActor
extension EngineSenseSettingsModel {
    func controllerItems(recenter: @escaping () -> Void) -> [EngineControllerMenuItem] {
        controlRows(recenter: recenter).map { row in
            EngineControllerMenuItem(id: row.id, title: row.title, value: row.value, enabled: row.enabled,
                                     activate: row.activate, adjust: row.adjust)
        }
    }
}

#endif
