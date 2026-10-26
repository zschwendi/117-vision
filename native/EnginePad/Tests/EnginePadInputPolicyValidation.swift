import Foundation
import CoreGraphics

// Synthetic validation of EnginePadInputPolicy.swift (pure Swift; no UIKit,
// device or simulator):
// xcrun swiftc -parse-as-library native/EnginePad/Sources/EnginePadInputPolicy.swift \
//   native/EnginePad/Sources/EnginePadPresentationState.swift \
//   native/EnginePad/Tests/EnginePadInputPolicyValidation.swift -o /tmp/pad-input-policy
@main struct EnginePadInputPolicyValidation {
    static var checks = 0
    static func check(_ condition: @autoclosure () -> Bool, _ message: String, line: Int = #line) {
        checks += 1
        if !condition() { fputs("line \(line): \(message)\n", stderr); exit(1) }
    }
    typealias Aim = EnginePadTouchAim
    static func clicks(_ outputs: [Aim.Output]) -> [Aim.Output] { outputs.filter { $0 == .primaryClick || $0 == .secondaryClick } }
    static func aims(_ outputs: [Aim.Output]) -> [(Double, Double)] {
        outputs.compactMap { if case let .aim(dx, dy) = $0 { return (dx, dy) } else { return nil } }
    }
    static let p0 = CGPoint(x: 100, y: 100)

    static func main() {
        // One-finger tap: a primary click only when the finger lifts.
        var t = Aim()
        t.began(1, at: p0, time: 0)
        check(t.moved(1, to: CGPoint(x: 104, y: 103)).isEmpty, "jitter inside slop does not aim")
        check(t.ended(1, at: CGPoint(x: 104, y: 103), time: 0.12) == [.primaryClick], "one-finger tap is primary")
        check(t.isIdle, "idle after tap")

        // Two rapid one-finger taps: two primaries, never secondary.
        var out: [Aim.Output] = []
        t.began(2, at: p0, time: 1); out += t.ended(2, at: p0, time: 1.08)
        t.began(3, at: p0, time: 1.15); out += t.ended(3, at: p0, time: 1.22)
        check(out == [.primaryClick, .primaryClick], "two rapid taps are two primaries: \(out)")

        // Simultaneous two-finger tap: exactly one secondary, no early primary
        // at any step (including when the first finger lifts).
        t.began(4, at: p0, time: 2)
        t.began(5, at: CGPoint(x: 160, y: 100), time: 2.05)
        check(t.ended(4, at: p0, time: 2.15).isEmpty, "no click while a finger remains")
        check(t.ended(5, at: CGPoint(x: 160, y: 100), time: 2.2) == [.secondaryClick], "two-finger tap is secondary")
        // Both lift in one event batch, opposite order.
        t.began(6, at: p0, time: 3); t.began(7, at: p0, time: 3)
        out = t.ended(7, at: p0, time: 3.1) + t.ended(6, at: p0, time: 3.1)
        check(out == [.secondaryClick], "simultaneous lift is one secondary: \(out)")

        // Not a one-finger double tap: a second finger that lands late is no tap.
        t.began(8, at: p0, time: 4); t.began(9, at: p0, time: 4.3)
        out = t.ended(8, at: p0, time: 4.32) + t.ended(9, at: p0, time: 4.33)
        check(clicks(out).isEmpty, "late second finger is not a two-finger tap: \(out)")

        // Drag aims and never clicks, including move away and back.
        t.began(10, at: p0, time: 5)
        out = t.moved(10, to: CGPoint(x: 120, y: 95))
        let first = aims(out)
        check(first.count == 1 && first[0].0 == 20 && first[0].1 == -5, "first aim step includes the slop, +x right, -y up: \(first)")
        out = t.moved(10, to: CGPoint(x: 125, y: 110))
        check(aims(out).first.map { $0 == (5, 15) } ?? false, "incremental aim +y down: \(out)")
        out = t.moved(10, to: p0) + t.ended(10, at: p0, time: 5.2)
        check(clicks(out).isEmpty && !aims(out).isEmpty, "drag away and back is aim only: \(out)")

        // A second finger joining a drag: no click, aim continues with the first.
        t.began(11, at: p0, time: 6); _ = t.moved(11, to: CGPoint(x: 140, y: 100))
        t.began(12, at: p0, time: 6.05)
        out = t.moved(11, to: CGPoint(x: 150, y: 100))
        check(aims(out).count == 1, "first finger still aims")
        check(t.moved(12, to: CGPoint(x: 101, y: 101)).isEmpty, "second finger never aims")
        out = t.ended(12, at: p0, time: 6.1) + t.ended(11, at: CGPoint(x: 150, y: 100), time: 6.15)
        check(clicks(out).isEmpty, "drag + second finger: no click: \(out)")

        // Two-finger movement cancels the secondary tap.
        t.began(13, at: p0, time: 7); t.began(14, at: p0, time: 7.02)
        _ = t.moved(14, to: CGPoint(x: 100, y: 140))
        out = t.ended(13, at: p0, time: 7.1) + t.ended(14, at: CGPoint(x: 100, y: 140), time: 7.12)
        check(clicks(out).isEmpty, "two-finger swipe: no click")

        // Three fingers: nothing, and no aim while three are down.
        t.began(15, at: p0, time: 8); t.began(16, at: p0, time: 8.01); t.began(17, at: p0, time: 8.02)
        check(t.moved(15, to: CGPoint(x: 200, y: 100)).isEmpty, "no aim with three fingers")
        out = t.ended(15, at: p0, time: 8.1) + t.ended(16, at: p0, time: 8.1) + t.ended(17, at: p0, time: 8.1)
        check(clicks(out).isEmpty, "three-finger tap: no click")
        // Two fingers, one lifts, a different finger lands: three seen, no click.
        t.began(18, at: p0, time: 9); t.began(19, at: p0, time: 9.01)
        _ = t.ended(18, at: p0, time: 9.05); t.began(20, at: p0, time: 9.1)
        out = t.ended(19, at: p0, time: 9.15) + t.ended(20, at: p0, time: 9.16)
        check(clicks(out).isEmpty, "re-landed finger: no click")

        // Long hold (ambiguous) does not fire; there is no touch hold-to-fire.
        t.began(21, at: p0, time: 10)
        check(t.ended(21, at: p0, time: 10.6).isEmpty, "long hold: no click")
        // Cancellation (system gesture, ownership loss) never fires.
        t.began(22, at: p0, time: 11); t.cancelled(22)
        check(t.ended(22, at: p0, time: 11.05).isEmpty && t.isIdle, "cancelled: no click")
        t.began(23, at: p0, time: 12); t.began(24, at: p0, time: 12.01); t.cancelled(23)
        check(t.ended(24, at: p0, time: 12.05).isEmpty, "one cancelled finger voids the two-finger tap")
        // reset() on app-UI ownership mid-tap: its later end is ignored.
        t.began(25, at: p0, time: 13); t.reset()
        check(t.ended(25, at: p0, time: 13.05).isEmpty && t.isIdle, "reset forgets fingers")
        t.began(26, at: p0, time: 14)
        check(t.ended(26, at: p0, time: 14.05) == [.primaryClick], "taps work again after reset")

        // Ownership: every condition required; .inactive is not active.
        typealias P = EnginePadInputPolicy
        check(P.gameOwnsInput(sceneActive: true, gameVisible: true, surfaceAttached: true, engineActive: true), "game owns")
        for i in 0..<4 {
            var f = [true, true, true, true]; f[i] = false
            check(!P.gameOwnsInput(sceneActive: f[0], gameVisible: f[1], surfaceAttached: f[2], engineActive: f[3]), "condition \(i) required")
        }
        // Arbitration: exactly one keyboard and one pointer authority.
        check(P.keyboardSource(gameControllerKeyboard: true) == .keyboardGC, "GCKeyboard preferred")
        check(P.keyboardSource(gameControllerKeyboard: false) == .keyboardUIKit, "UIKit fallback")
        check(P.pointerSource(pointerLocked: true, gameControllerMouse: true) == .pointerGC, "raw mouse while locked")
        check(P.pointerSource(pointerLocked: false, gameControllerMouse: true) == .pointerUIKit, "unlocked: UIKit pointer")
        check(P.pointerSource(pointerLocked: true, gameControllerMouse: false) == .pointerUIKit, "no GCMouse: UIKit")
        check(EnginePadInputSource.touch.rawValue == 5 && EnginePadInputSource.pointerGC.rawValue == 3, "raw values match device_input.h")
        // Pointer lock request.
        check(P.prefersPointerLock(gameOwnsInput: true, gameControllerMouse: true, userReleased: false), "request lock")
        check(!P.prefersPointerLock(gameOwnsInput: false, gameControllerMouse: true, userReleased: false), "app UI: no lock")
        check(!P.prefersPointerLock(gameOwnsInput: true, gameControllerMouse: false, userReleased: false), "no mouse: no lock")
        check(!P.prefersPointerLock(gameOwnsInput: true, gameControllerMouse: true, userReleased: true), "released: no lock")
        check(P.isCaptureToggle(usage: 0x0F, command: true) && !P.isCaptureToggle(usage: 0x0F, command: false), "Command-L only")
        // Launcher text is truthful: no controller hard requirement, and touch
        // is described as aim/click only, never full movement.
        let keyboardOnly = P.launcherSummary(keyboard: true, pointer: false)
        check(keyboardOnly.contains("Keyboard detected") && keyboardOnly.contains("two-finger tap") && !keyboardOnly.contains("Pair a controller"), keyboardOnly)
        let both = P.launcherSummary(keyboard: true, pointer: true)
        check(both.contains("mouse or trackpad") && both.contains("⌘."), both)
        let none = P.launcherSummary(keyboard: false, pointer: false)
        check(none.contains("Touch cannot move") && none.contains("tap to fire"), none)
        let pointerOnly = P.launcherSummary(keyboard: false, pointer: true)
        check(pointerOnly.contains("no keyboard or controller to move"), pointerOnly)
        // Presentation: present/dismiss never overlap; the latest wish wins.
        var g = EnginePadPresentationState()
        check(g.desire(true, canPresent: false) == .none, "no window: wait")
        check(g.poke(canPresent: true) == .present, "anchor appeared: present")
        check(g.desire(false, canPresent: true) == .none && g.desire(true, canPresent: true) == .none, "close/reopen during present: no overlap")
        check(g.finished(canPresent: true) == .none && g.shown, "reopened before completion: stays shown")
        check(g.desire(false, canPresent: true) == .dismiss, "Library: dismiss")
        check(g.desire(true, canPresent: true) == .none, "Resume during dismiss waits")
        check(g.finished(canPresent: true) == .present, "then presents a fresh controller")
        check(g.desire(false, canPresent: true) == .none, "close during that present waits")
        check(g.finished(canPresent: true) == .dismiss && g.finished(canPresent: true) == .none && !g.shown, "then dismisses; settles")
        check(g.desire(true, canPresent: true) == .present, "present")
        g.presentRefused()
        check(!g.shown && !g.transitioning && g.poke(canPresent: true) == .present, "refused: retried on the next chance, not looped")
        print("PASS \(checks) touch tap/two-finger/drag/cancel, input ownership/arbitration/lock policy and presentation-serialization checks (synthetic; no device)")
    }
}
