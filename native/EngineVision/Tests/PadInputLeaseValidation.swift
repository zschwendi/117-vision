import Foundation

/// Deterministic checks for the mobile UI controller lease policy. A fake
/// bridge models the native 500 ms lease; no controller or device is used.
@main struct PadInputLeaseValidation {
    static var checks = 0
    static func require(_ value: Bool, _ message: String) {
        checks += 1
        precondition(value, message)
    }

    final class FakeBridge {
        var next: UInt64 = 1
        var live: UInt64 = 0
        var begins = 0, renews = 0
        var ended: [UInt64] = []
        var refuseBegin = false
        func begin() -> UInt64 {
            begins += 1
            if refuseBegin { return 0 }
            live = next; next += 1
            return live
        }
        func renew(_ token: UInt64) -> Int32 { renews += 1; return token == live && token != 0 ? 1 : 0 }
        func end(_ token: UInt64) { ended.append(token); if token == live { live = 0 } }
        func expire() { live = 0 }
    }

    static func main() {
        typealias Lease = EnginePadInputLease
        require(Lease.uiOwnsInput(background: false, gameVisible: false, appSurfaceVisible: false), "Library owns input")
        require(Lease.uiOwnsInput(background: false, gameVisible: true, appSurfaceVisible: true), "Settings sheet over gameplay owns input")
        require(!Lease.uiOwnsInput(background: false, gameVisible: true, appSurfaceVisible: false), "Plain gameplay with overlay buttons does not")
        require(!Lease.uiOwnsInput(background: true, gameVisible: false, appSurfaceVisible: true), "Background never owns input")

        let bridge = FakeBridge()
        var lease = Lease(begin: bridge.begin, renew: bridge.renew, end: bridge.end)
        lease.update(owned: true)
        require(lease.token == 1 && bridge.begins == 1, "Entering app UI begins one lease")
        for _ in 0..<5 { lease.update(owned: true) }
        require(bridge.begins == 1 && bridge.renews == 5 && lease.token == 1, "Each poll renews the same lease")
        bridge.expire()
        lease.update(owned: true)
        require(bridge.begins == 2 && lease.token == 2, "An expired lease is reacquired on the next poll")
        lease.update(owned: false)
        require(bridge.ended == [2] && !lease.held, "Gameplay ends the exact current token")
        lease.update(owned: false)
        require(bridge.ended == [2] && bridge.begins == 2, "Remaining in gameplay neither ends nor begins again")
        lease.update(owned: true)
        require(lease.token == 3 && bridge.begins == 3, "Returning to app UI begins a fresh lease")

        bridge.refuseBegin = true
        lease.release()
        lease.update(owned: true)
        require(!lease.held, "A refused begin holds no token")
        bridge.refuseBegin = false
        lease.update(owned: true)
        require(lease.held && lease.token == bridge.live, "A refused begin is retried on the next poll")
        let last = lease.token
        lease.release()
        require(bridge.ended.last == last && !lease.held, "Release (teardown) ends the held token exactly once")
        lease.release()
        require(bridge.ended.filter { $0 == last }.count == 1, "Release is idempotent")
        print("PASS: \(checks) synthetic mobile controller-lease policy checks")
    }
}
