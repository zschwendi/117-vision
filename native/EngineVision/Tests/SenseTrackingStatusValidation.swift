import Foundation

@main struct SenseTrackingStatusValidation {
    static var checks = 0
    static func require(_ value: @autoclosure () -> Bool, _ message: String) {
        checks += 1; precondition(value(), message)
    }

    static func main() throws {
        let status = EngineSenseTrackingStatus()
        require(status.snapshot().phase == "stopped", "Launcher starts with no claimed tracked pose")
        let old = status.begin()
        require(old != 0 && status.snapshot().phase == "starting", "New renderer owns starting status")
        status.update(old) {
            $0.phase = "tracking"; $0.authorization = "allowed"; $0.providerState = "running"
            $0.left.tracked = true; $0.right.tracked = true; $0.framesQueried = 10
            $0.left.ageMilliseconds = 20; $0.right.lowAccuracy = true
        }
        let tracked = status.snapshot()
        require(tracked.left.tracked && tracked.right.tracked, "Current observations available to UI")
        let encoded = try JSONEncoder().encode(tracked)
        let decoded = try JSONDecoder().decode(EngineSenseTrackingStatus.Snapshot.self, from: encoded)
        require(decoded == tracked, "Diagnostic JSON preserves complete status and accuracy evidence")
        let expired = status.snapshot(at: tracked.updatedUptime + 2)
        require(expired.phase == "paused" && !expired.left.tracked && !expired.right.tracked, "No new frames cannot keep showing active tracking")
        require(status.snapshot(at: tracked.updatedUptime).phase == "tracking", "Reading an aged projection does not mutate the writer")
        let current = status.begin()
        require(current != old && status.snapshot().framesQueried == 0, "New renderer clears retired evidence")
        status.update(old) { $0.phase = "failed"; $0.lastError = "retired callback" }
        require(status.snapshot().phase == "starting" && status.snapshot().lastError == nil, "Late callback cannot replace new renderer status")
        status.end(old)
        require(status.snapshot().phase == "starting", "Retired renderer cannot stop newer tracking status")
        status.update(current) { $0.phase = "denied"; $0.authorization = "denied" }
        require(status.snapshot().phase == "denied", "Permission failure is observable")
        status.end(current)
        require(status.snapshot().phase == "stopped", "Current owner closes tracking status")
        status.update(current) { $0.phase = "tracking"; $0.right.tracked = true }
        require(status.snapshot().phase == "stopped" && !status.snapshot().right.tracked, "Ended owner cannot resurrect a tracked pose")
        let live = status.begin()
        DispatchQueue.concurrentPerform(iterations: 200) { _ in
            status.update(live) { $0.framesQueried &+= 1 }
            _ = status.snapshot()
        }
        require(status.snapshot().framesQueried == 200, "Concurrent diagnostics retain every frame without a torn snapshot")
        print("PASS \(checks) Sense tracking status checks")
    }
}
