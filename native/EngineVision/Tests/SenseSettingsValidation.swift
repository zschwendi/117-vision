import Foundation

// Compile the actual settings view/model with recorded native setters.
@MainActor private var recordedAim: Int32 = -1
@MainActor private var recordedTurn: Int32 = -1
@MainActor private var recordedDual: Int32 = -1
@MainActor private var recordedSpeed: Float = -1
@MainActor private var recordedBoundary: Float = -1
@MainActor private var recordedGestures: Int32 = -1
@MainActor func halo_sense_set_enabled(_ value: Int32) { recordedAim = value }
@MainActor func halo_sense_set_turn_mode(_ value: Int32) { recordedTurn = value }
@MainActor func halo_sense_set_dual_enabled(_ value: Int32) { recordedDual = value }
@MainActor func halo_sense_set_turn_speed(_ value: Float) { recordedSpeed = value }
@MainActor func halo_sense_set_turn_boundary(_ value: Float) { recordedBoundary = value }
@MainActor func halo_sense_set_gestures_enabled(_ value: Int32) { recordedGestures = value }
@MainActor private var recordedGrip: Int32 = -1
@MainActor private var recordedScope: Int32 = -1
@MainActor private var recordedWeaponHaptics: Int32 = -1
@MainActor func halo_sense_set_grip_continuity(_ value: Int32) { recordedGrip = value }
@MainActor func halo_sense_set_scope_smoothing(_ value: Int32) { recordedScope = value }
@MainActor func halo_sense_set_weapon_haptics(_ value: Int32) { recordedWeaponHaptics = value }
@MainActor private var recordedBody: Int32 = -1
@MainActor func halo_sense_set_body_enabled(_ value: Int32) { recordedBody = value }
func halo_sense_active() -> Int32 { 0 }
func halo_sense_native_controls() -> Int32 { 0 }
func enginevision_menu_active() -> Int32 { 0 }
func halo_sense_gameplay_supported() -> Int32 { 1 }
@_cdecl("host_sense_weapon_status")
func senseSettingsStatusStub(_ status: UnsafeMutablePointer<HaloSenseWeaponStatus>?) {
    status?.pointee = HaloSenseWeaponStatus()
}

@main struct SenseSettingsValidation {
    @MainActor static func main() {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            precondition(condition(), message)
            count += 1
        }
        func withDefaults(_ body: (UserDefaults) -> Void) {
            let suite = "MasterCheef.SenseSettingsValidation.\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defer { defaults.removePersistentDomain(forName: suite) }
            body(defaults)
        }
        let legacyKey = "MasterCheefSenseTurnMode.v1"
        let turnKey = "MasterCheefSenseTurnMode.v2"
        let speedKey = "MasterCheefSenseTurnSpeed.v1"
        let boundaryKey = "MasterCheefSenseTurnBoundary.v1"
        let gesturesKey = "MasterCheefSenseQuickGestures.v1"
        withDefaults { defaults in
            let model = EngineSenseSettingsModel(defaults: defaults)
            check(model.turnMode == .pointToTurn && recordedTurn == 2, "New profile selects point-to-turn")
            check(defaults.integer(forKey: turnKey) == 2, "New default is persisted once")
            check(defaults.object(forKey: legacyKey) == nil, "No legacy preference is created")
            check(model.trackedAim && recordedAim == 1, "Tracked aim default is retained")
            check(model.weaponMode == .single && recordedDual == 0, "Single weapon default is retained")
            check(model.showHandsAndControllers, "Real hands default is retained")
            check(model.turnSpeed == 90 && recordedSpeed == 90, "Default speed reaches native setter")
            check(model.stickSpeedSummary == "Turn 180°/s · Tilt 90°/s", "Default stick speed explains doubled turn and tilt")
            check(model.turnBoundary == 0.72 && abs(recordedBoundary - 0.72) < 0.0001, "Default normalized boundary reaches native setter")
            check(model.quickGestures && recordedGestures == 1, "Quick gestures default on reaches native setter")
            check(!model.fullBody && recordedBody == 0, "Experimental full body defaults off natively")
        }
        withDefaults { defaults in
            defaults.set("dual", forKey: "MasterCheefSenseWeaponMode.v2")
            let model = EngineSenseSettingsModel(defaults: defaults)
            model.fullBody = true
            check(recordedBody == 1 && defaults.bool(forKey: "MasterCheefSenseFullBody.v1"), "Full body toggle reaches native and persists")
            let restored = EngineSenseSettingsModel(defaults: defaults)
            check(restored.fullBody && recordedBody == 1, "Full body choice is restored natively on launch")
            check(restored.weaponMode == .dual && restored.quickGestures, "Full body leaves other choices unchanged")
            restored.fullBody = false
            let off = EngineSenseSettingsModel(defaults: defaults)
            check(!off.fullBody && recordedBody == 0, "Disabling full body survives relaunch")
            defaults.set("invalid", forKey: "MasterCheefSenseFullBody.v1")
            let malformed = EngineSenseSettingsModel(defaults: defaults)
            check(!malformed.fullBody && recordedBody == 0, "Malformed full body preference stays off")
        }
        for old in [0, 30, 45] {
            withDefaults { defaults in
                defaults.set(old, forKey: legacyKey)
                defaults.set(false, forKey: "MasterCheefSenseTrackedAim.v1")
                defaults.set("dual", forKey: "MasterCheefSenseWeaponMode.v2")
                defaults.set(false, forKey: "MasterCheefSenseRealHandsVisible.v1")
                defaults.set(0.73, forKey: "ordinaryGamepad.lookSensitivity")
                let expected: EngineSenseTurnMode = old == 0 ? .physical : .pointToTurn
                let model = EngineSenseSettingsModel(defaults: defaults)
                check(model.turnMode == expected && recordedTurn == Int32(expected.rawValue), "Legacy turn migration \(old)")
                check(defaults.integer(forKey: turnKey) == expected.rawValue, "Migration writes v2")
                check(defaults.integer(forKey: legacyKey) == old, "Migration preserves v1")
                check(!model.trackedAim && recordedAim == 0, "Migration preserves disabled aim")
                check(model.weaponMode == .dual && recordedDual == 1, "Migration preserves explicit weapon mode")
                check(!model.showHandsAndControllers, "Migration preserves visibility")
                check(defaults.double(forKey: "ordinaryGamepad.lookSensitivity") == 0.73, "Unrelated gamepad preference retained")
                model.turnMode = .snap45
                check(recordedTurn == 45 && defaults.integer(forKey: turnKey) == 45, "Explicit new snap choice is saved")
                let restored = EngineSenseSettingsModel(defaults: defaults)
                check(restored.turnMode == .snap45 && recordedTurn == 45, "Migration does not repeat")
                check(defaults.integer(forKey: legacyKey) == old, "New choice leaves old key unchanged")
            }
        }
        for selected in EngineSenseTurnMode.allCases {
            withDefaults { defaults in
                defaults.set(45, forKey: legacyKey)
                defaults.set(selected.rawValue, forKey: turnKey)
                let model = EngineSenseSettingsModel(defaults: defaults)
                check(model.turnMode == selected && recordedTurn == Int32(selected.rawValue), "Every v2 mode overrides legacy")
                model.turnMode = .smoothStick
                check(recordedTurn == 1, "Smooth mode reaches native setter")
                let restored = EngineSenseSettingsModel(defaults: defaults)
                check(restored.turnMode == .smoothStick, "Smooth mode persists")
            }
        }
        for corrupt: Any in [999, "invalid"] {
            withDefaults { defaults in
                defaults.set(corrupt, forKey: turnKey)
                let model = EngineSenseSettingsModel(defaults: defaults)
                check(model.turnMode == .pointToTurn && recordedTurn == 2, "Invalid mode recovers to point-to-turn")
                check(defaults.integer(forKey: turnKey) == 2, "Recovered mode is persisted")
            }
        }
        withDefaults { defaults in
            let model = EngineSenseSettingsModel(defaults: defaults)
            model.trackedAim = false; model.weaponMode = .dual; model.showHandsAndControllers = false
            model.turnMode = .physical
            let restored = EngineSenseSettingsModel(defaults: defaults)
            check(!restored.trackedAim && recordedAim == 0, "Aim setting still persists")
            check(restored.weaponMode == .dual && recordedDual == 1, "Weapon setting still persists")
            check(!restored.showHandsAndControllers, "Visibility setting still persists")
            check(restored.turnMode == .physical && recordedTurn == 0, "Physical-only choice persists")
        }
        let tuningCases: [(Any, Double, Any, Double)] = [
            (75.0, 75, 0.80, 0.80),
            (-100.0, 30, -1.0, 0.50),
            (1000.0, 120, 2.0, 0.90),
            ("invalid", 90, "invalid", 0.72),
            (Double.nan, 90, Double.nan, 0.72),
            (Double.infinity, 90, Double.infinity, 0.72),
            (-Double.infinity, 90, -Double.infinity, 0.72)
        ]
        for (speed, expectedSpeed, boundary, expectedBoundary) in tuningCases {
            withDefaults { defaults in
                defaults.set(speed, forKey: speedKey)
                defaults.set(boundary, forKey: boundaryKey)
                let model = EngineSenseSettingsModel(defaults: defaults)
                check(model.turnSpeed == expectedSpeed, "Saved speed is bounded and finite")
                check(model.turnBoundary == expectedBoundary, "Saved boundary is bounded and finite")
                check(recordedSpeed == Float(expectedSpeed), "Validated speed reaches native")
                check(recordedBoundary == Float(expectedBoundary), "Validated boundary reaches native")
                check(defaults.double(forKey: speedKey) == expectedSpeed, "Validated speed is persisted")
                check(defaults.double(forKey: boundaryKey) == expectedBoundary, "Validated boundary is persisted")
            }
        }
        withDefaults { defaults in
            defaults.set(45, forKey: legacyKey)
            defaults.set(1, forKey: turnKey)
            defaults.set(75.0, forKey: speedKey)
            defaults.set(0.84, forKey: boundaryKey)
            defaults.set(false, forKey: "MasterCheefSenseTrackedAim.v1")
            defaults.set("dual", forKey: "MasterCheefSenseWeaponMode.v2")
            defaults.set(false, forKey: "MasterCheefSenseRealHandsVisible.v1")
            defaults.set(0.73, forKey: "ordinaryGamepad.lookSensitivity")
            let model = EngineSenseSettingsModel(defaults: defaults)
            model.quickGestures = false
            check(recordedGestures == 0 && !defaults.bool(forKey: gesturesKey), "Gesture toggle disables native and persists")
            let restored = EngineSenseSettingsModel(defaults: defaults)
            check(!restored.quickGestures && recordedGestures == 0, "Disabled gestures survive relaunch")
            check(restored.turnMode == .smoothStick && recordedTurn == 1, "Gesture change preserves v2 turning")
            check(restored.turnSpeed == 75 && restored.turnBoundary == 0.84, "Gesture change preserves turning tuning")
            check(defaults.integer(forKey: legacyKey) == 45, "Gesture change preserves legacy turning")
            check(!restored.trackedAim && recordedAim == 0, "Gesture change preserves disabled tracked aim")
            check(restored.weaponMode == .dual && recordedDual == 1, "Gesture change preserves dual choice")
            check(!restored.showHandsAndControllers, "Gesture change preserves visibility")
            check(defaults.double(forKey: "ordinaryGamepad.lookSensitivity") == 0.73, "Gesture change preserves ordinary gamepad preferences")
            restored.quickGestures = true
            check(recordedGestures == 1 && defaults.bool(forKey: gesturesKey), "Gesture toggle enables native and persists")
            let enabled = EngineSenseSettingsModel(defaults: defaults)
            check(enabled.quickGestures && recordedGestures == 1, "Enabled gestures survive relaunch")
            enabled.trackedAim = true; enabled.weaponMode = .single; enabled.turnMode = .physical
            check(enabled.quickGestures && defaults.bool(forKey: gesturesKey), "Aim, weapon and turning changes preserve gesture choice")
        }
        withDefaults { defaults in
            defaults.set("invalid", forKey: gesturesKey)
            let model = EngineSenseSettingsModel(defaults: defaults)
            check(model.quickGestures && recordedGestures == 1, "Malformed gesture preference uses enabled default")
        }
        withDefaults { defaults in
            let model = EngineSenseSettingsModel(defaults: defaults)
            model.turnSpeed = 110; model.turnBoundary = 0.83
            let restored = EngineSenseSettingsModel(defaults: defaults)
            check(restored.turnSpeed == 110 && recordedSpeed == 110, "Tuned speed persists")
            check(restored.stickSpeedSummary == "Turn 220°/s · Tilt 110°/s", "Saved speed is the tilt rate; turning is twice it")
            check(restored.turnBoundary == 0.83 && recordedBoundary == Float(0.83), "Tuned boundary persists")
            restored.turnSpeed = -1; restored.turnBoundary = 2
            check(restored.turnSpeed == 30 && recordedSpeed == 30, "Speed setter clamps low values")
            check(restored.turnBoundary == 0.90 && recordedBoundary == Float(0.90), "Boundary setter clamps high values")
            restored.turnSpeed = 999; restored.turnBoundary = -1
            check(restored.turnSpeed == 120 && recordedSpeed == 120, "Speed setter clamps high values")
            check(restored.turnBoundary == 0.50 && recordedBoundary == Float(0.50), "Boundary setter clamps low values")
            restored.turnSpeed = .nan; restored.turnBoundary = .infinity
            check(restored.turnSpeed == 90 && recordedSpeed == 90, "Speed setter rejects nonfinite values")
            check(restored.turnBoundary == 0.72 && recordedBoundary == Float(0.72), "Boundary setter rejects nonfinite values")
            restored.turnMode = .physical
            let physical = EngineSenseSettingsModel(defaults: defaults)
            check(physical.turnMode == .physical && physical.turnSpeed == 90 && physical.turnBoundary == 0.72,
                  "Changing mode retains valid independent tuning")
        }
        // Weapon feel toggles: defaults, native setters, persistence, and the
        // actual Sense-controller navigation rows both app panels use.
        let gripKey = "MasterCheefSenseGripContinuity.v1"
        let scopeKey = "MasterCheefSenseScopedSmoothing.v1"
        let hapticsKey = "MasterCheefSenseWeaponHaptics.v1"
        withDefaults { defaults in
            let model = EngineSenseSettingsModel(defaults: defaults)
            check(model.gripContinuity && recordedGrip == 1, "Grip continuity defaults on and reaches native")
            check(!model.scopedSmoothing && recordedScope == 0, "Scope steadying defaults off and reaches native")
            check(model.trackedWeaponHaptics && recordedWeaponHaptics == 1, "Tracked weapon haptics defaults on and reaches native")
            var recentered = 0
            var rows = model.controlRows { recentered += 1 }
            let ids = rows.map(\.id)
            for id in ["sense.body", "sense.grip", "sense.scope", "sense.weaponHaptics"] {
                check(ids.contains(id), "Navigation row \(id) present with tracked aiming")
            }
            check(ids.firstIndex(of: "sense.weapons")! < ids.firstIndex(of: "sense.grip")! &&
                  ids.last == "sense.hands", "Feel rows sit with the tracked weapon rows")
            @MainActor func row(_ id: String) -> EngineSenseControlRow { model.controlRows { recentered += 1 }.first { $0.id == id }! }
            row("sense.body").activate()
            check(model.fullBody && recordedBody == 1, "Cross enables body rendering through shared controller rows")
            row("sense.body").adjust(-1)
            check(!model.fullBody && recordedBody == 0, "Stick left disables body rendering through shared controller rows")
            row("sense.grip").activate()
            check(!model.gripContinuity && recordedGrip == 0 && defaults.object(forKey: gripKey) as? Bool == false,
                  "Cross on the grip row turns it off, natively and persisted")
            row("sense.grip").adjust(1)
            check(model.gripContinuity && recordedGrip == 1, "Stick right turns grip smoothing on")
            row("sense.scope").adjust(1)
            check(model.scopedSmoothing && recordedScope == 1 && defaults.bool(forKey: scopeKey), "Stick right turns scope steadying on")
            row("sense.scope").activate()
            check(!model.scopedSmoothing && recordedScope == 0, "Cross toggles scope steadying off")
            row("sense.weaponHaptics").adjust(-1)
            check(!model.trackedWeaponHaptics && recordedWeaponHaptics == 0 && defaults.object(forKey: hapticsKey) as? Bool == false,
                  "Stick left turns tracked weapon haptics off")
            check(row("sense.weaponHaptics").value == "false" && row("sense.grip").enabled, "Row values and enablement reflect state")
            model.scopedSmoothing = true
            let restored = EngineSenseSettingsModel(defaults: defaults)
            check(restored.gripContinuity && restored.scopedSmoothing && !restored.trackedWeaponHaptics,
                  "All three choices survive relaunch")
            check(recordedGrip == 1 && recordedScope == 1 && recordedWeaponHaptics == 0, "Relaunch applies saved choices natively")
            check(restored.turnMode == .pointToTurn && restored.weaponMode == .single && restored.quickGestures,
                  "Feel toggles leave other Sense preferences untouched")
            restored.trackedAim = false
            rows = restored.controlRows { recentered += 1 }
            check(!rows.contains { ["sense.body", "sense.grip", "sense.scope", "sense.weaponHaptics"].contains($0.id) },
                  "Tracked weapon rows are hidden while tracked aiming is off")
            check(recordedWeaponHaptics == 0 && recordedScope == 1, "Turning tracked aim off does not rewrite feel choices")
            rows.first { $0.id == "sense.aim" }!.activate()
            check(restored.trackedAim, "Aim row still toggles tracked aiming")
            rows = restored.controlRows { recentered += 1 }
            rows.first { $0.id == "sense.aim" }!.adjust(-1)
            check(!restored.trackedAim && recentered == 1, "Turning aim off from a row recenters, as before")
        }
        withDefaults { defaults in
            defaults.set("invalid", forKey: gripKey); defaults.set(3, forKey: scopeKey); defaults.set("x", forKey: hapticsKey)
            let model = EngineSenseSettingsModel(defaults: defaults)
            check(model.gripContinuity && !model.scopedSmoothing && model.trackedWeaponHaptics,
                  "Malformed feel preferences use their defaults")
        }
        print("PASS \(count) actual settings model defaults, migration, bounded tuning, gesture persistence, weapon feel toggles, navigation rows and retained preferences checks")
    }
}
