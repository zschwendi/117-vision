import Foundation
import CoreGraphics

/// Complete direct-touch fallback. Roles are assigned at touch-down and never
/// change when fingers cross: moving, aiming and holding an action coexist.
/// Native commands use a separate touch lane, never a fake hardware device.
struct EnginePadTouchControls {
    enum Action: String, CaseIterable {
        case fire, zoom, jump, crouch, reload, use, swap, grenade, grenadeCycle, melee, flashlight
        var title: String {
            switch self {
            case .fire: return "Fire"
            case .zoom: return "Zoom"
            case .jump: return "Jump"
            case .crouch: return "Crouch"
            case .reload: return "Reload"
            case .use: return "Use / Pick up"
            case .swap: return "Swap"
            case .grenade: return "Grenade"
            case .grenadeCycle: return "Grenade type"
            case .melee: return "Melee"
            case .flashlight: return "Light"
            }
        }
        /// Retail PC1.10 semantic IDs, resolved against live saved bindings
        /// on the native input pump. No synthetic keyboard device or profile edits.
        var actions: [Int] {
            switch self {
            case .fire: return [7]
            case .zoom: return [11]
            case .jump: return [0]
            case .crouch: return [10]
            case .reload: return [13]
            case .use: return [2, 14]
            case .swap: return [3]
            case .grenade: return [6]
            case .grenadeCycle: return [1]
            case .melee: return [4]
            case .flashlight: return [5]
            }
        }
    }
    enum Target { case movement(center: CGPoint, radius: CGFloat), look, action(Action) }
    enum Command: Equatable {
        case enabled(Bool), releaseAll
        case action(id: Int, down: Bool)
        case motion(dx: Double, dy: Double)
    }
    private struct Contact { let target: Target; var point: CGPoint }
    private var contacts: [Int: Contact] = [:]
    private var actionOwners: [Action: Set<Int>] = [:]
    private var movementID: Int?, lookID: Int?
    private var movementKeys = Set<Int>()
    private(set) var enabled = false
    private(set) var movementOffset = CGPoint.zero
    var heldActions: Set<Action> { Set(actionOwners.keys) }
    static let lookCountsPerPoint = 1.5

    static func available(gameOwnsInput: Bool, keyboardConnected: Bool,
                          controllerConnected: Bool, nativeMenuActive: Bool) -> Bool {
        gameOwnsInput && !keyboardConnected && !controllerConnected && !nativeMenuActive
    }

    mutating func update(enabled value: Bool) -> [Command] {
        guard value != enabled else { return [] }
        enabled = value
        clear()
        return [.enabled(value)]
    }

    /// Clear contacts too: delayed move/end callbacks cannot rearm old fingers.
    mutating func cancelAll() -> [Command] {
        clear()
        return [.releaseAll]
    }
    private mutating func clear() {
        contacts.removeAll(); actionOwners.removeAll(); movementKeys.removeAll()
        movementID = nil; lookID = nil; movementOffset = .zero
    }

    mutating func began(_ id: Int, target: Target, at point: CGPoint) -> [Command] {
        guard enabled, contacts[id] == nil, point.x.isFinite, point.y.isFinite else { return [] }
        switch target {
        case let .movement(center, radius):
            guard movementID == nil, radius.isFinite, radius > 0,
                  center.x.isFinite, center.y.isFinite else { return [] }
            movementID = id
        case .look:
            guard lookID == nil else { return [] }
            lookID = id
        case let .action(action):
            let first = actionOwners[action, default: []].isEmpty
            actionOwners[action, default: []].insert(id)
            contacts[id] = Contact(target: target, point: point)
            return first ? actionCommands(action, down: true) : []
        }
        contacts[id] = Contact(target: target, point: point)
        return moved(id, to: point)
    }

    mutating func moved(_ id: Int, to point: CGPoint) -> [Command] {
        guard enabled, var contact = contacts[id], point.x.isFinite, point.y.isFinite else { return [] }
        defer { contact.point = point; contacts[id] = contact }
        switch contact.target {
        case let .movement(center, radius):
            var x = (point.x - center.x) / radius, y = (point.y - center.y) / radius
            let length = hypot(x, y)
            if length > 1 { x /= length; y /= length }
            movementOffset = CGPoint(x: x * radius, y: y * radius)
            // Eight-way movement follows the four saved directional actions. Hysteresis
            // prevents a resting thumb from chattering at the dead zone.
            var wanted = Set<Int>()
            for (usage, amount) in [(21, -x), (22, x), (19, -y), (20, y)] {
                if amount > (movementKeys.contains(usage) ? 0.18 : 0.28) { wanted.insert(usage) }
            }
            let releases = movementKeys.subtracting(wanted).sorted().map { Command.action(id: $0, down: false) }
            let presses = wanted.subtracting(movementKeys).sorted().map { Command.action(id: $0, down: true) }
            movementKeys = wanted
            return releases + presses
        case .look, .action(.fire):
            // Fire can be dragged to aim too; it remains held until lifted.
            let dx = Double(point.x - contact.point.x) * Self.lookCountsPerPoint
            let dy = Double(point.y - contact.point.y) * Self.lookCountsPerPoint
            return dx == 0 && dy == 0 ? [] : [.motion(dx: dx, dy: dy)]
        case .action: return []
        }
    }

    mutating func ended(_ id: Int) -> [Command] {
        guard let contact = contacts.removeValue(forKey: id) else { return [] }
        switch contact.target {
        case .movement:
            movementID = nil; movementOffset = .zero
            let result = movementKeys.sorted().map { Command.action(id: $0, down: false) }
            movementKeys.removeAll()
            return result
        case .look: lookID = nil; return []
        case let .action(action):
            actionOwners[action]?.remove(id)
            guard actionOwners[action]?.isEmpty == true else { return [] }
            actionOwners[action] = nil
            return actionCommands(action, down: false)
        }
    }

    private func actionCommands(_ action: Action, down: Bool) -> [Command] {
        action.actions.map { .action(id: $0, down: down) }
    }
}
