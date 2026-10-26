import Foundation

// Compile the production physical settings model with recorded native setters.
@MainActor private var recordedArms: Int32 = -1
@MainActor private var recordedMagazine: Int32 = -1
@MainActor private var recordedPickup: Int32 = -1
@MainActor private var recordedCharge: Int32 = -1
@MainActor func halo_sense_set_physical_charge(_ value: Int32) { recordedCharge = value }
@MainActor func halo_sense_set_connected_arms(_ value: Int32) { recordedArms = value }
@MainActor func halo_sense_set_physical_magazine(_ value: Int32) { recordedMagazine = value }
@MainActor func halo_sense_set_physical_pickup(_ value: Int32) { recordedPickup = value }

@main struct SensePhysicalSettingsValidation {
    @MainActor static func main() {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) { precondition(condition(), message); count += 1 }
        let suite = "MasterCheef.SensePhysicalSettingsValidation.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }

        // Default off, and the native layer receives that at launch.
        var model = EngineSensePhysicalSettingsModel(defaults: defaults)
        check(!model.connectedArms && !model.physicalMagazine && !model.reachPickup && !model.physicalCharge, "all physical options default off")
        check(recordedArms == 0 && recordedMagazine == 0 && recordedPickup == 0 && recordedCharge == 0, "launch applies off to native")
        // Charging is a phase of the physical magazine: unavailable without it.
        model.toggle("sense.physical.charge")
        check(!model.physicalCharge && model.descriptors.first { $0.id == "sense.physical.charge" }?.enabled == false, "charge needs magazine")
        // Each descriptor toggles exactly its own option and native setting.
        model.toggle("sense.physical.arms")
        check(model.connectedArms && recordedArms == 1 && recordedMagazine == 0 && recordedPickup == 0, "arms toggle")
        model.toggle("sense.physical.magazine")
        check(model.physicalMagazine && recordedMagazine == 1 && recordedPickup == 0, "magazine toggle")
        model.set("sense.physical.pickup", true)
        check(model.reachPickup && recordedPickup == 1, "pickup set")
        model.toggle("sense.physical.charge")
        check(model.physicalCharge && recordedCharge == 1, "charge toggle with magazine on")
        model.toggle("sense.physical.charge")
        check(!model.physicalCharge && recordedCharge == 0, "charge toggles off")
        model.toggle("sense.physical.magazine")
        check(!model.physicalMagazine && recordedMagazine == 0 && model.connectedArms, "independent options")
        // Persisted and reapplied on the next launch.
        recordedArms = -1; recordedMagazine = -1; recordedPickup = -1
        model = EngineSensePhysicalSettingsModel(defaults: defaults)
        check(model.connectedArms && !model.physicalMagazine && model.reachPickup, "persisted values")
        check(recordedArms == 1 && recordedMagazine == 0 && recordedPickup == 1, "reapplied at launch")
        model.toggle("sense.unknown")
        check(model.connectedArms && !model.physicalMagazine && model.reachPickup, "unknown id ignored")

        // Every toggle is reachable by Sense controller navigation: its id is
        // a navigation descriptor and the same id targets its on-screen control.
        let source = try! String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
        let ids = model.descriptors.map(\.id)
        check(Set(ids).count == 4, "four distinct navigation ids")
        for id in ids { check(source.contains(".senseControllerTarget(\"\(id)\")"), "controller target for \(id)") }
        check(source.contains("descriptors.map"), "navigation items derive from the same descriptors")
        let settingsView = try! String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
        check(settingsView.contains("EngineSensePhysicalSection(model: physical)"), "section inserted in Sense Controls")
        check(settingsView.contains("+ physical.controllerItems"), "navigation items inserted in Sense Controls")
        check(!source.contains("D-pad") && !source.contains("touchpad"), "hints use buttons Sense actually has")

        // Truthful status text: nothing claimed while off.
        var status = HaloSensePhysicalStatus()
        check(EngineSensePhysicalSettingsModel.armsMessage(status) == nil, "no arm status while off")
        check(EngineSensePhysicalSettingsModel.magazineMessage(status) == nil, "no magazine status while off")
        status.arms_enabled = 1; status.arm = (Int32(HALO_SENSE_ARM_BODY), Int32(HALO_SENSE_ARM_CONNECTED))
        check(EngineSensePhysicalSettingsModel.armsMessage(status) == "Arms — left: drawn by the body, right: connected", "body draws arms")
        status.arm = (Int32(HALO_SENSE_ARM_OUT_OF_REACH), Int32(HALO_SENSE_ARM_CONNECTED))
        check(EngineSensePhysicalSettingsModel.armsMessage(status) == "Arms — left: out of reach, right: connected", "arm states")
        status.magazine_enabled = 1; status.magazine_support = Int32(HALO_SENSE_MAGAZINE_UNSUPPORTED)
        check(EngineSensePhysicalSettingsModel.magazineMessage(status) == "This weapon uses the normal reload.", "unsupported weapon")
        status.magazine_stage = Int32(HALO_SENSE_MAGAZINE_INSERTED)
        check(EngineSensePhysicalSettingsModel.magazineMessage(status) == "Pull the charging handle back with L2, then let go.", "charge hint")
        status.magazine_stage = Int32(HALO_SENSE_MAGAZINE_DISCARDED); status.magazine_result = Int32(HALO_SENSE_MAGAZINE_NO_RESERVE)
        check(EngineSensePhysicalSettingsModel.magazineMessage(status)?.contains("No reserve") == true, "no reserve")
        var pickup = HaloSensePickupStatus()
        check(EngineSensePhysicalSettingsModel.pickupMessage(pickup) == nil, "no pickup status while off")
        pickup.enabled = 1; pickup.reason = Int32(HALO_SENSE_PICKUP_NOT_CAMPAIGN)
        check(EngineSensePhysicalSettingsModel.pickupMessage(pickup) == "Physical pickup works in campaign only.", "campaign gate")
        pickup.stage = Int32(HALO_SENSE_PICKUP_FLYING)
        check(EngineSensePhysicalSettingsModel.pickupMessage(pickup) == "Keep holding L2 to catch it.", "flying hint")
        pickup.stage = 0
        pickup.reason = Int32(HALO_SENSE_PICKUP_READY); pickup.stage = Int32(HALO_SENSE_PICKUP_LOCKED)
        check(EngineSensePhysicalSettingsModel.pickupMessage(pickup) == "Pull your hand back toward you.", "locked hint")
        print("PASS \(count) physical settings: default off, independent, persisted, native-applied, Sense navigation ids/targets, truthful status")
    }
}
