import Foundation

/// Read-only tracking evidence for the launcher, settings and diagnostic report.
/// Session ownership prevents a retired renderer from replacing newer status.
final class EngineSenseTrackingStatus: @unchecked Sendable {
    struct Hand: Codable, Sendable, Equatable {
        var tracked = false
        var state = "No anchor"
        var ageMilliseconds: Double?
        var predicted = false
        var lowAccuracy = false
    }
    struct Snapshot: Codable, Sendable, Equatable {
        var phase = "stopped"
        var summary = "Enter immersive Halo to start Sense tracking."
        var authorization = "notRequested"
        var providerState = "notStarted"
        var spatialControllers = 0
        var accessories = 0
        var left = Hand()
        var right = Hand()
        var lastError: String?
        var framesQueried: UInt64 = 0
        var updatedUptime = 0.0
    }
    static let shared = EngineSenseTrackingStatus()
    private let lock = NSLock()
    private var generation: UInt64 = 0
    private var value = Snapshot()

    func begin() -> UInt64 {
        lock.withLock {
            generation &+= 1
            value = Snapshot()
            value.phase = "starting"
            value.summary = "Starting Sense tracking…"
            value.updatedUptime = ProcessInfo.processInfo.systemUptime
            return generation
        }
    }

    func update(_ session: UInt64, _ change: (inout Snapshot) -> Void) {
        var message: String?
        lock.withLock {
            guard session != 0, session == generation else { return }
            let previous = value
            change(&value)
            value.updatedUptime = ProcessInfo.processInfo.systemUptime
            if previous.phase != value.phase || previous.lastError != value.lastError {
                message = "[sense] \(value.phase): \(value.summary) permission=\(value.authorization) provider=\(value.providerState) controllers=\(value.spatialControllers) accessories=\(value.accessories)"
            }
        }
        if let message { NSLog("%@", message) }
    }

    func end(_ session: UInt64) {
        lock.withLock {
            guard session != 0, session == generation else { return }
            generation &+= 1
            value.phase = "stopped"
            value.summary = "Enter immersive Halo to start Sense tracking."
            value.providerState = "stopped"
            value.left = Hand(); value.right = Hand()
            value.updatedUptime = ProcessInfo.processInfo.systemUptime
        }
    }

    func snapshot(at uptime: TimeInterval = ProcessInfo.processInfo.systemUptime) -> Snapshot {
        lock.withLock {
            var result = value
            if result.framesQueried > 0,
               uptime - result.updatedUptime > 1,
               result.phase == "tracking" || result.phase == "degraded" {
                result.phase = "paused"
                result.summary = "Sense updates paused. Return to immersive Halo to resume."
                result.left.tracked = false; result.right.tracked = false
            }
            return result
        }
    }
}
