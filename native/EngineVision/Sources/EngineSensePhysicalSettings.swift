import SwiftUI

/// Optional physical Sense features. Each is default off, persisted
/// independently and applied to the native layer at launch and on change.
/// None changes aiming, firing, scope, grenades, melee or the existing
/// gestures and buttons; disabling one restores the previous behavior.
@MainActor
final class EngineSensePhysicalSettingsModel: ObservableObject {
    static let shared = EngineSensePhysicalSettingsModel()
    static let armsKey = "MasterCheefSensePhysicalArms.v1"
    static let magazineKey = "MasterCheefSensePhysicalMagazine.v1"
    static let pickupKey = "MasterCheefSenseReachPickup.v1"
    static let chargeKey = "MasterCheefSensePhysicalCharge.v1"
    private let defaults: UserDefaults

    @Published var connectedArms: Bool {
        didSet { halo_sense_set_connected_arms(connectedArms ? 1 : 0); defaults.set(connectedArms, forKey: Self.armsKey) }
    }
    @Published var physicalMagazine: Bool {
        didSet { halo_sense_set_physical_magazine(physicalMagazine ? 1 : 0); defaults.set(physicalMagazine, forKey: Self.magazineKey) }
    }
    @Published var physicalCharge: Bool {
        didSet { halo_sense_set_physical_charge(physicalCharge ? 1 : 0); defaults.set(physicalCharge, forKey: Self.chargeKey) }
    }
    @Published var reachPickup: Bool {
        didSet { halo_sense_set_physical_pickup(reachPickup ? 1 : 0); defaults.set(reachPickup, forKey: Self.pickupKey) }
    }

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        connectedArms = defaults.object(forKey: Self.armsKey) as? Bool ?? false
        physicalMagazine = defaults.object(forKey: Self.magazineKey) as? Bool ?? false
        reachPickup = defaults.object(forKey: Self.pickupKey) as? Bool ?? false
        physicalCharge = defaults.object(forKey: Self.chargeKey) as? Bool ?? false
        halo_sense_set_connected_arms(connectedArms ? 1 : 0)
        halo_sense_set_physical_magazine(physicalMagazine ? 1 : 0)
        halo_sense_set_physical_pickup(reachPickup ? 1 : 0)
        halo_sense_set_physical_charge(physicalCharge ? 1 : 0)
    }

    /// Called after idle-only CloudDocuments settings application.
    func reloadFromDefaults() {
        connectedArms = defaults.object(forKey: Self.armsKey) as? Bool ?? false
        physicalMagazine = defaults.object(forKey: Self.magazineKey) as? Bool ?? false
        reachPickup = defaults.object(forKey: Self.pickupKey) as? Bool ?? false
        physicalCharge = defaults.object(forKey: Self.chargeKey) as? Bool ?? false
    }

    /// Sense controller navigation: id, title and value for every toggle.
    /// Each id is also attached to its control with senseControllerTarget.
    struct Descriptor: Equatable { let id: String; let title: String; let value: Bool; var enabled = true }
    var descriptors: [Descriptor] {
        [.init(id: "sense.physical.arms", title: "Connected arms", value: connectedArms),
         .init(id: "sense.physical.magazine", title: "Physical magazine reload", value: physicalMagazine),
         .init(id: "sense.physical.charge", title: "Charge after reloading", value: physicalCharge, enabled: physicalMagazine),
         .init(id: "sense.physical.pickup", title: "Physical weapon pickup", value: reachPickup)]
    }
    func toggle(_ id: String) { set(id, !(descriptors.first { $0.id == id }?.value ?? true)) }
    func set(_ id: String, _ value: Bool) {
        switch id {
        case "sense.physical.arms": connectedArms = value
        case "sense.physical.magazine": physicalMagazine = value
        case "sense.physical.charge": if physicalMagazine { physicalCharge = value }
        case "sense.physical.pickup": reachPickup = value
        default: break
        }
    }

    /// Truthful live state from the native layer; nil when nothing applies.
    static func armsMessage(_ status: HaloSensePhysicalStatus) -> String? {
        guard status.arms_enabled != 0 else { return nil }
        func hand(_ state: Int32) -> String {
            switch Int(state) {
            case HALO_SENSE_ARM_CONNECTED: return "connected"
            case HALO_SENSE_ARM_OUT_OF_REACH: return "out of reach"
            case HALO_SENSE_ARM_UNKNOWN_RIG: return "not supported by this weapon"
            case HALO_SENSE_ARM_DEGENERATE: return "unavailable"
            case HALO_SENSE_ARM_HIDDEN: return "hidden"
            case HALO_SENSE_ARM_BODY: return "drawn by the body"
            default: return "waiting"
            }
        }
        return "Arms — left: \(hand(status.arm.0)), right: \(hand(status.arm.1))"
    }
    static func magazineMessage(_ status: HaloSensePhysicalStatus) -> String? {
        guard status.magazine_enabled != 0 else { return nil }
        switch Int(status.magazine_stage) {
        case HALO_SENSE_MAGAZINE_GRIPPED, HALO_SENSE_MAGAZINE_OUT: return "Magazine in hand. Release L2 away from the gun to drop it."
        case HALO_SENSE_MAGAZINE_DISCARDED: return status.magazine_result == Int32(HALO_SENSE_MAGAZINE_NO_RESERVE)
            ? "No reserve ammunition for a new magazine." : "Well empty. Squeeze L2 away from the gun to take a new magazine."
        case HALO_SENSE_MAGAZINE_NEW: return status.magazine_result == Int32(HALO_SENSE_MAGAZINE_BUSY)
            ? "Busy; pull the magazine back and insert again." : "Insert the new magazine into the gun."
        case HALO_SENSE_MAGAZINE_INSERTED: return status.magazine_result == Int32(HALO_SENSE_MAGAZINE_BUSY)
            ? "Busy; pull the charging handle again." : "Pull the charging handle back with L2, then let go."
        case HALO_SENSE_MAGAZINE_CHARGE_GRIPPED: return "Pull it all the way back."
        case HALO_SENSE_MAGAZINE_CHARGE_PULLED: return "Let go to chamber."
        case HALO_SENSE_MAGAZINE_COMMITTED: return "Reloading…"
        default: break
        }
        switch Int(status.magazine_support) {
        case HALO_SENSE_MAGAZINE_READY: return "Squeeze L2 with your left hand on the magazine to remove it."
        case HALO_SENSE_MAGAZINE_FULL: return "Magazine full."
        case HALO_SENSE_MAGAZINE_ENGINE_BUSY: return "Weapon busy."
        case HALO_SENSE_MAGAZINE_UNSUPPORTED: return "This weapon uses the normal reload."
        default: return nil
        }
    }
    static func pickupMessage(_ status: HaloSensePickupStatus) -> String? {
        guard status.enabled != 0 else { return nil }
        switch Int(status.stage) {
        case HALO_SENSE_PICKUP_AIMED: return "Squeeze L2 to grab the weapon you are pointing at."
        case HALO_SENSE_PICKUP_LOCKED: return "Pull your hand back toward you."
        case HALO_SENSE_PICKUP_FLYING: return "Keep holding L2 to catch it."
        case HALO_SENSE_PICKUP_COMMITTED: return "Picking up…"
        default: break
        }
        switch Int(status.reason) {
        case HALO_SENSE_PICKUP_NOT_CAMPAIGN: return "Physical pickup works in campaign only."
        case HALO_SENSE_PICKUP_READY: return "Point your left hand at the weapon Halo offers to pick up."
        default: return nil
        }
    }
}

#if os(visionOS)
/// Inserted into Sense Controls. Every toggle has a Sense controller target.
struct EngineSensePhysicalSection: View {
    @ObservedObject var model: EngineSensePhysicalSettingsModel

    var body: some View {
        Section("Physical interactions (experimental)") {
            Toggle("Connected arms", isOn: $model.connectedArms)
                .senseControllerTarget("sense.physical.arms")
            Text("Shows your arms reaching to your tracked hands. Near full reach the shoulder eases forward a little; farther than that, the forearm is hidden as before. Aiming and shots are unchanged.")
                .font(.caption).foregroundStyle(.secondary)
            Toggle("Physical magazine reload", isOn: $model.physicalMagazine)
                .senseControllerTarget("sense.physical.magazine")
            Text("Assault rifle, pistol and sniper rifle. Squeeze L2 on the magazine and pull it out, let go away from the gun to drop it, squeeze L2 away from the gun for a new one and push it in. Halo then reloads normally, with its usual time and ammunition. The gun can't fire while the magazine is out; R2 puts it back. Square still reloads. Not while dual wielding.")
                .font(.caption).foregroundStyle(.secondary)
            Toggle("Charge after reloading", isOn: $model.physicalCharge)
                .disabled(!model.physicalMagazine)
                .senseControllerTarget("sense.physical.charge")
            Text("After inserting a magazine, pull the charging handle (pistol: the slide) back with L2 and let go before Halo reloads.")
                .font(.caption).foregroundStyle(.secondary)
            Toggle("Physical weapon pickup", isOn: $model.reachPickup)
                .senseControllerTarget("sense.physical.pickup")
            Text("When Halo offers a weapon to pick up, point your left hand at it, squeeze L2 and pull back. It flies to your hand; keep holding L2 to catch it and Halo picks it up normally. Campaign only; Square still picks up.")
                .font(.caption).foregroundStyle(.secondary)
            TimelineView(.periodic(from: .now, by: 0.5)) { _ in
                let messages = liveMessages()
                ForEach(messages, id: \.self) { message in
                    Label(message, systemImage: "hand.raised").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
    private func liveMessages() -> [String] {
        var status = HaloSensePhysicalStatus()
        var pickup = HaloSensePickupStatus()
        _ = host_sense_physical_status(&status)
        _ = host_sense_pickup_status(&pickup)
        return [EngineSensePhysicalSettingsModel.armsMessage(status),
                EngineSensePhysicalSettingsModel.magazineMessage(status),
                EngineSensePhysicalSettingsModel.pickupMessage(pickup)].compactMap { $0 }
    }
}

@MainActor
extension EngineSensePhysicalSettingsModel {
    var controllerItems: [EngineControllerMenuItem] {
        descriptors.map { item in
            EngineControllerMenuItem(id: item.id, title: item.title, value: "\(item.value)", enabled: item.enabled,
                                     activate: { self.toggle(item.id) }, adjust: { self.set(item.id, $0 > 0) })
        }
    }
}
#endif
