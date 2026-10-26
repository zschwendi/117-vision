/// Bounded app ownership of controller input while a launcher surface covers
/// Halo. The native lease expires after 500 ms unless renewed; ending it keeps
/// the native release gate until a fresh fully neutral snapshot, so a press
/// that dismisses UI never reaches the game. Pure Swift: native calls are
/// injected, and the caller drives `update` from its existing poll timer.
struct EnginePadInputLease {
    private let begin: () -> UInt64
    private let renew: (UInt64) -> Int32
    private let end: (UInt64) -> Void
    private(set) var token: UInt64 = 0

    init(begin: @escaping () -> UInt64, renew: @escaping (UInt64) -> Int32, end: @escaping (UInt64) -> Void) {
        self.begin = begin
        self.renew = renew
        self.end = end
    }

    /// Only actual app UI owns input: the Library, or a sheet/importer/alert.
    /// Gameplay with only the small overlay buttons visible does not, nor does
    /// a backgrounded scene.
    static func uiOwnsInput(background: Bool, gameVisible: Bool, appSurfaceVisible: Bool) -> Bool {
        !background && (!gameVisible || appSurfaceVisible)
    }

    var held: Bool { token != 0 }

    /// Begin on entry, renew on every poll (reacquire if expired), end the exact
    /// token on exit. A zero token means the bridge refused; retry next poll.
    mutating func update(owned: Bool) {
        if owned {
            if token == 0 || renew(token) == 0 { token = begin() }
        } else {
            release()
        }
    }

    mutating func release() {
        guard token != 0 else { return }
        end(token)
        token = 0
    }
}
