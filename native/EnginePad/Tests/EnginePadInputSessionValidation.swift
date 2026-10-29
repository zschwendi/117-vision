import Foundation
import CoreGraphics

// Synthetic validation of EnginePadInputSession, the decisions the router
// executes (no UIKit, GameController hardware, device or simulator):
// xcrun swiftc -parse-as-library native/EnginePad/Sources/EnginePadInputPolicy.swift \
//   native/EnginePad/Sources/EnginePadInputSession.swift \
//   native/EnginePad/Tests/EnginePadInputSessionValidation.swift -o /tmp/pad-input-session
@main struct EnginePadInputSessionValidation {
    typealias S = EnginePadInputSession
    typealias C = EnginePadInputSession.Command
    static var checks = 0
    static func check(_ condition: @autoclosure () -> Bool, _ message: String, line: Int = #line) {
        checks += 1
        if !condition() { fputs("line \(line): \(message)\n", stderr); exit(1) }
    }
    static var physicalKeys = Set<Int>(), physicalButtons = Set<Int>()
    static func session() -> S {
        S(isMapped: { (0x04...0xE7).contains($0) && $0 != 0xE3 && $0 != 0xE7 },
          physicallyPressed: { (physicalKeys, physicalButtons) })
    }
    /// A session in play: keyboard + GCMouse connected, game owns input.
    static func playing(keyboard: Bool = true, mouse: Bool = true) -> S {
        var s = session()
        _ = s.devicesChanged(keyboard: keyboard, mouse: mouse)
        _ = s.update(sceneActive: true); _ = s.update(gameVisible: true); _ = s.update(surfaceAttached: true)
        _ = s.update(engineActive: true); _ = s.update(presented: true)
        return s
    }
    static let W = 0x1A, SPACE = 0x2C, L = 0x0F, TAB = 0x2B, ESC = 0x29, PERIOD = 0x37
    static let p = CGPoint(x: 10, y: 10)
    static func keys(_ c: [C]) -> [C] { c.filter { if case .key = $0 { return true } else { return false } } }
    static func motions(_ c: [C]) -> [C] { c.filter { if case .motion = $0 { return true } else { return false } } }
    static func buttons(_ c: [C]) -> [C] { c.filter { if case .button = $0 { return true } else { return false } } }

    static func main() {
        // Ownership needs every condition, including the presented controller.
        var s = session()
        _ = s.devicesChanged(keyboard: true, mouse: false)
        _ = s.update(sceneActive: true); _ = s.update(gameVisible: true); _ = s.update(surfaceAttached: true)
        _ = s.update(engineActive: true)
        check(!s.gameOwnsInput, "not owned before the game controller is presented")
        var out = s.update(presented: true)
        check(out.contains(.setGameOwns(true)) && out.contains(.focus(true)) && s.gameOwnsInput, "owned: \(out)")
        out = s.update(sceneActive: false)   // .inactive, not only background
        check(out.contains(.setGameOwns(false)) && out.contains(.focus(false)), "inactive fences: \(out)")
        _ = s.update(sceneActive: true)
        out = s.update(presented: false)     // controller disappearing: immediate
        check(out.contains(.setGameOwns(false)) && s.gameOwnsInput == false, "disappearing fences at once")
        _ = s.update(presented: true)
        out = s.update(gameVisible: false)   // app sheet / Library
        check(out.contains(.setGameOwns(false)), "app UI fences")
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands.isEmpty, "no keys while app UI owns")
        out = s.update(gameVisible: true)
        check(out.contains(.setGameOwns(true)), "game regained")
        // W pressed while the app owned input is quarantined after regain.
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).isEmpty, "held across UI: dead until released")
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: false, command: false).commands).count == 1, "release passes")
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands == [.key(.keyboardGC, usage: W, down: true, chord: false)], "fresh press re-arms")

        // Held during play, then a focus loss: release required before re-arm,
        // even if a duplicate down or a queued callback arrives afterwards.
        out = s.focusLost()
        check(out == [.release], "focus loss releases")
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).isEmpty, "late duplicate down after fence dropped")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: false, command: false)
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).count == 1, "re-armed after release")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: false, command: false)
        // A press physically down at the fence whose callback is still queued.
        physicalKeys = [SPACE]; _ = s.focusLost(); physicalKeys = []
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: true, command: false).commands).isEmpty, "in-flight press quarantined")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: false, command: false)
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: true, command: false).commands).count == 1, "then works")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: false, command: false)

        // Device generation: callbacks from a replaced/disconnected keyboard are stale.
        let old = s.keyboardEpoch
        out = s.devicesChanged(keyboard: true, mouse: false)
        check(out.first == .release && s.keyboardEpoch != old, "re-enumeration releases and advances the generation")
        check(s.key(from: .keyboardGC, epoch: old, usage: TAB, down: true, command: false).commands.isEmpty, "stale-generation callback ignored")
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: TAB, down: true, command: false).commands).count == 1, "current generation accepted")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: TAB, down: false, command: false)
        // Accepted W down, then an unrelated mouse connects (handlers replaced):
        // the old queued W-up is stale, so the change itself releases natively
        // (no stranded hold) and W, no longer physically down, works at once.
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).count == 1, "W down")
        let beforeMouse = s.keyboardEpoch
        out = s.devicesChanged(keyboard: true, mouse: true)
        check(out.first == .release, "rebind fences natively: \(out)")
        check(s.key(from: .keyboardGC, epoch: beforeMouse, usage: W, down: false, command: false).commands.isEmpty, "old queued up ignored")
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).count == 1,
              "a fresh press is not suppressed by the replaced handler's stale cache")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: false, command: false)
        // A key still physically down across the rebind stays dead until released.
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: true, command: false)
        physicalKeys = [SPACE]; _ = s.devicesChanged(keyboard: true, mouse: false); physicalKeys = []
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: true, command: false).commands).isEmpty, "held across rebind quarantined")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: SPACE, down: false, command: false)
        // Keyboard unplugged with W held, another keyboard plugged in: its first W works.
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false)
        _ = s.devicesChanged(keyboard: false, mouse: false); _ = s.devicesChanged(keyboard: true, mouse: false)
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: true, command: false).commands).count == 1, "new keyboard's first press")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: W, down: false, command: false)

        // One keyboard authority: with a GCKeyboard, UIKit presses are kept from
        // the responder chain when they are Halo keys but never injected.
        var r = s.key(from: .keyboardUIKit, usage: SPACE, down: true, command: false)
        check(r.commands.isEmpty && r.swallow, "UIKit duplicate swallowed, not injected")
        _ = s.key(from: .keyboardUIKit, usage: SPACE, down: false, command: false)
        r = s.key(from: .keyboardUIKit, usage: TAB, down: true, command: true)
        check(!r.swallow, "Command-Tab goes to the system")
        _ = s.key(from: .keyboardUIKit, usage: TAB, down: false, command: false)
        r = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: PERIOD, down: true, command: true)
        check(r.commands == [.key(.keyboardGC, usage: PERIOD, down: true, chord: true)] && r.swallow, "Command-period forwarded as chord (native Escape)")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: PERIOD, down: false, command: false)
        // UIKit fallback (no GCKeyboard): repeats are dropped.
        var u = playing(keyboard: false, mouse: false)
        check(u.keyboardSource == .keyboardUIKit, "UIKit keyboard source")
        check(keys(u.key(from: .keyboardUIKit, usage: W, down: true, command: false).commands).count == 1, "UIKit press")
        check(keys(u.key(from: .keyboardUIKit, usage: W, down: true, command: false).commands).isEmpty, "UIKit repeat dropped")
        check(u.key(from: .keyboardGC, epoch: u.keyboardEpoch, usage: W, down: true, command: false).commands.isEmpty, "GC not selected")
        _ = u.update(gameVisible: false)
        r = u.key(from: .keyboardUIKit, usage: ESC, down: true, command: false)
        check(!r.swallow && r.commands.isEmpty, "app UI keeps Escape for its own navigation")
        _ = u.key(from: .keyboardUIKit, usage: ESC, down: false, command: false)

        // Command-L: one toggle per physical L press; release fences at once.
        s = playing()
        _ = s.update(pointerLocked: true)
        check(s.pointerSource == .pointerGC && s.prefersPointerLock, "locked: raw mouse")
        out = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: true).commands
        check(s.pointerReleased && out.first == .release && out.contains(.pointerLockPreference(false)), "release fences before the lock notification: \(out)")
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: true).commands.isEmpty && s.pointerReleased, "repeat does not toggle")
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: false).commands.isEmpty && s.pointerReleased,
              "Command released first: L repeat neither toggles nor types L")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: false, command: false)
        // Duplicate delivery: the unselected UIKit copy arrives first.
        _ = s.key(from: .keyboardUIKit, usage: L, down: true, command: true)
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: true)
        check(!s.pointerReleased, "exactly one toggle for one press delivered twice")
        _ = s.key(from: .keyboardUIKit, usage: L, down: false, command: false)
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: false, command: false)
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: false).commands).count == 1, "plain L is a Halo key")
        // L held first, then Command: the repeat is not a capture edge, and L's
        // release still reaches Halo.
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: true).commands.isEmpty && !s.pointerReleased, "held L + Command does not toggle")
        check(keys(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: false, command: true).commands) == [.key(.keyboardGC, usage: L, down: false, chord: true)], "L release delivered")
        // L held across a focus loss (quarantined) cannot toggle from a repeat.
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: false)
        _ = s.focusLost()
        check(s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: true, command: true).commands.isEmpty && !s.pointerReleased, "quarantined L does not toggle")
        _ = s.key(from: .keyboardGC, epoch: s.keyboardEpoch, usage: L, down: false, command: false)
        // Explicit release: raw GCMouse motion and new clicks stop before the
        // asynchronous lock notification arrives.
        check(s.pointerSource == .pointerGC, "still locked")
        out = s.togglePointerCapture()
        check(s.pointerReleased && s.pointerSource == .pointerGC && out.first == .release, "released, lock not yet reported")
        check(s.mouseMoved(epoch: s.mouseEpoch, dx: 9, dy: 9).isEmpty, "no motion after release")
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true).isEmpty, "no fire after release")
        // Button still held through the release: recapture replays nothing.
        out = s.update(pointerLocked: false) + s.togglePointerCapture() + s.update(pointerLocked: true)
        check(buttons(out).isEmpty && motions(out).isEmpty && keys(out).isEmpty, "recapture replays no input: \(out)")
        check(!s.pointerReleased && s.mouseMoved(epoch: s.mouseEpoch, dx: 1, dy: 0).count == 1, "recaptured: motion resumes")
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true).isEmpty, "button held through release stays dead")
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false) == [.button(.pointerGC, index: 0, down: false)], "its release")
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true).count == 1, "fresh click fires")
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false)

        // GCMouse: only while locked; held button quarantined across a fence.
        let me = s.mouseEpoch
        check(s.mouseButton(epoch: me, index: 0, down: true) == [.button(.pointerGC, index: 0, down: true)], "locked click")
        check(s.mouseMoved(epoch: me, dx: 3, dy: 2) == [.motion(.pointerGC, dx: 3, dy: -2)], "GC +y up becomes DirectInput +y down")
        out = s.update(pointerLocked: false)   // capture loss
        check(out.contains(.select(keyboard: .keyboardGC, pointer: .pointerUIKit)), "lock loss switches source (native fence)")
        check(s.mouseButton(epoch: me, index: 0, down: true).isEmpty && s.mouseMoved(epoch: me, dx: 1, dy: 1).isEmpty, "raw mouse ignored unlocked")
        _ = s.update(pointerLocked: true)
        check(s.mouseButton(epoch: me, index: 0, down: true).isEmpty, "button held across capture loss stays dead")
        check(s.mouseButton(epoch: me, index: 0, down: false) == [.button(.pointerGC, index: 0, down: false)], "its release passes")
        check(s.mouseButton(epoch: me, index: 0, down: true).count == 1, "fresh click works")
        _ = s.mouseButton(epoch: me, index: 0, down: false)
        // A mouse press physically down at a fence, its callback still queued.
        physicalButtons = [1]; _ = s.focusLost(); physicalButtons = []
        check(s.mouseButton(epoch: me, index: 1, down: true).isEmpty, "in-flight mouse press quarantined")
        _ = s.mouseButton(epoch: me, index: 1, down: false)
        check(s.mouseButton(epoch: me, index: 1, down: true).count == 1, "then clicks")
        _ = s.mouseButton(epoch: me, index: 1, down: false)
        _ = s.devicesChanged(keyboard: true, mouse: true)
        check(s.mouseButton(epoch: me, index: 1, down: true).isEmpty, "stale mouse generation ignored")

        // UIKit pointer: released pointer recaptures without a shot; buttons
        // never inject while released; cancel adds no motion or click.
        s = playing(keyboard: true, mouse: false)
        check(s.pointerSource == .pointerUIKit && !s.prefersPointerLock, "no GCMouse: UIKit pointer, no lock request")
        out = s.togglePointerCapture()
        check(out.first == .release && s.pointerReleased, "explicit release fences")
        check(s.hover(.began, at: p).isEmpty && s.hover(.changed, at: CGPoint(x: 40, y: 10)).isEmpty, "released pointer does not aim")
        out = s.pointerBegan(1, at: p, buttons: 1)
        check(!s.pointerReleased && buttons(out).isEmpty, "click on game recaptures, no shot: \(out)")
        check(s.pointerMoved(1, to: CGPoint(x: 30, y: 30), buttons: 3).isEmpty && s.pointerEnded(1, at: p).isEmpty, "swallowed stream stays inert")
        out = s.pointerBegan(2, at: p, buttons: 2)
        check(out == [.button(.pointerUIKit, index: 1, down: true)], "secondary click (system two-finger trackpad click) is button 1")
        out = s.pointerMoved(2, to: CGPoint(x: 14, y: 10), buttons: 2)
        check(out == [.motion(.pointerUIKit, dx: 6, dy: 0)], "drag aims")
        // Hover during the stream adds nothing; after it, hover re-baselines.
        check(s.hover(.changed, at: CGPoint(x: 99, y: 99)).isEmpty && s.hover(.changed, at: CGPoint(x: 120, y: 99)).isEmpty,
              "hover suppressed during a press stream (no double motion)")
        out = s.pointerCancelled(2)
        check(out == [.cancelButton(.pointerUIKit, index: 1)] && motions(out).isEmpty, "cancel: no motion, cancels the button")
        check(s.pointerEnded(2, at: CGPoint(x: 50, y: 50)).isEmpty, "late end after cancel ignored")
        check(s.hover(.changed, at: CGPoint(x: 60, y: 60)).isEmpty, "first hover after a stream only re-baselines")
        check(s.hover(.changed, at: CGPoint(x: 62, y: 60)) == [.motion(.pointerUIKit, dx: 3, dy: 0)], "hover aims")
        // A fence mid-stream drops the stream: its later events inject nothing.
        _ = s.pointerBegan(3, at: p, buttons: 1)
        _ = s.update(gameVisible: false); _ = s.update(gameVisible: true)
        check(s.pointerMoved(3, to: CGPoint(x: 90, y: 90), buttons: 1).isEmpty && s.pointerEnded(3, at: p).isEmpty, "stream dropped by the fence")

        // Touch: rapid taps and a two-finger tap; nothing when not owned.
        out = s.touchBegan(10, at: p, time: 0) + s.touchEnded(10, at: p, time: 0.1)
        out += s.touchBegan(11, at: p, time: 0.2) + s.touchEnded(11, at: p, time: 0.3)
        check(out == [.click(index: 0), .click(index: 0)], "two rapid taps, two primaries")
        out = s.touchBegan(12, at: p, time: 1) + s.touchBegan(13, at: p, time: 1.02)
        out += s.touchEnded(12, at: p, time: 1.1) + s.touchEnded(13, at: p, time: 1.12)
        check(out == [.click(index: 1)], "two-finger tap, one secondary")
        _ = s.touchBegan(14, at: p, time: 2)
        _ = s.update(sceneActive: false)
        check(s.touchEnded(14, at: p, time: 2.1).isEmpty, "inactive mid-tap: no click")
        _ = s.update(sceneActive: true)
        check(s.pause() == [.keyPulse(0x01)], "overlay Pause is one Escape press")
        _ = s.update(gameVisible: false)
        check(s.pause().isEmpty, "Pause ignored while app UI owns input")

        // Real pause temporarily frees the system cursor for app controls.
        // A held trigger/click must not survive either edge or fire on Resume.
        s = playing()
        _ = s.update(pointerLocked: true)
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true)
        physicalButtons = [0]
        out = s.update(gamePaused: true)
        check(out.first == .release && out.contains(.pointerLockPreference(false)), "pause releases before OS lock notification")
        check(s.gameOwnsInput && !s.pointerReleased && !s.prefersPointerLock, "pause keeps Halo menu input and capture intent")
        check(s.mouseMoved(epoch: s.mouseEpoch, dx: 8, dy: 4).isEmpty, "late raw motion during unlock cannot move aim")
        check(s.mouseButton(epoch: s.mouseEpoch, index: 1, down: true).isEmpty, "late raw button during unlock cannot fire")
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 1, down: false)
        _ = s.update(pointerLocked: false)
        physicalButtons = []
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false)
        check(s.pointerSource == .pointerUIKit, "paused pointer uses UIKit cursor")
        check(s.pointerBegan(70, at: p, buttons: 1).contains(.button(.pointerUIKit, index: 0, down: true)), "menu can be clicked without recapturing")
        physicalButtons = [0]
        out = s.update(gamePaused: false)
        check(out.first == .release && out.contains(.pointerLockPreference(true)), "resume restores prior capture request")
        check(s.pointerMoved(70, to: CGPoint(x: 50, y: 50), buttons: 1).isEmpty, "resume drops old click stream")
        _ = s.update(pointerLocked: true)
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true).isEmpty, "resume click quarantined until physical release")
        physicalButtons = []
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false)
        check(s.mouseButton(epoch: s.mouseEpoch, index: 0, down: true) == [.button(.pointerGC, index: 0, down: true)], "fresh click fires normally after resume")
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false)
        _ = s.togglePointerCapture()
        _ = s.update(pointerLocked: false)
        _ = s.update(gamePaused: true)
        check(s.pointerReleased && !s.prefersPointerLock, "user's released-cursor choice survives pause")
        check(s.pointerBegan(71, at: p, buttons: 1).contains(.button(.pointerUIKit, index: 0, down: true)) && s.pointerReleased,
              "paused menu click does not change user's release preference")
        _ = s.pointerEnded(71, at: p)
        _ = s.update(gamePaused: false)
        check(s.pointerReleased && !s.prefersPointerLock, "resume preserves deliberate cursor release")
        check(s.update(gamePaused: false).isEmpty, "repeated pause polls do not clear new input")

        // A UIKit-only trackpad has no GC up callback. Preserve only enough
        // stream identity across fences to observe its eventual release.
        s = playing(mouse: false)
        check(s.pointerBegan(80, at: p, buttons: 1) == [.button(.pointerUIKit, index: 0, down: true)], "UIKit-only primary begins")
        _ = s.update(gamePaused: true)
        _ = s.update(gamePaused: false)
        _ = s.focusLost()
        check(s.quarantinedButtons.contains(0), "multiple fences retain a still-held UIKit button")
        check(s.pointerBegan(80, at: p, buttons: 1).isEmpty, "late begin cannot revive a retired stream")
        check(s.pointerMoved(80, to: CGPoint(x: 200, y: 300), buttons: 1).isEmpty, "old UIKit motion and down remain inert")
        check(s.hover(.changed, at: p).isEmpty && s.hover(.changed, at: CGPoint(x: 90, y: 80)).isEmpty, "hover cannot bypass a fenced drag")
        check(s.pointerEnded(80, at: CGPoint(x: 400, y: 500)).isEmpty && !s.quarantinedButtons.contains(0), "UIKit-only end rearms without motion or a click")
        _ = s.focusLost()
        check(!s.quarantinedButtons.contains(0), "later fence cannot resurrect released UIKit hold")
        check(s.pointerBegan(81, at: p, buttons: 1) == [.button(.pointerUIKit, index: 0, down: true)], "fresh UIKit click works after end")
        _ = s.pointerEnded(81, at: p)

        // Cancellation is also a release, including while app UI owns input.
        _ = s.pointerBegan(82, at: p, buttons: 2)
        _ = s.update(gameVisible: false)
        check(s.pointerCancelled(82).isEmpty && !s.quarantinedButtons.contains(1), "cancelled fenced stream only clears the old hold")
        _ = s.update(gameVisible: true)
        check(s.pointerMoved(82, to: CGPoint(x: 300, y: 400), buttons: 2).isEmpty, "cancelled old stream never moves again")
        check(s.pointerBegan(83, at: p, buttons: 2) == [.button(.pointerUIKit, index: 1, down: true)], "fresh secondary click works after cancellation")
        _ = s.pointerEnded(83, at: p)

        // Button-mask updates may release just one button on a retired stream.
        // A new click must survive the old stream's later end.
        _ = s.pointerBegan(84, at: p, buttons: 3)
        _ = s.focusLost()
        check(s.pointerMoved(84, to: CGPoint(x: 300, y: 400), buttons: 2).isEmpty, "retired mask change adds no movement")
        check(!s.quarantinedButtons.contains(0) && s.quarantinedButtons.contains(1), "partial release only rearms its own button")
        check(s.pointerBegan(85, at: p, buttons: 1) == [.button(.pointerUIKit, index: 0, down: true)], "primary works while old secondary is held")
        check(s.pointerEnded(84, at: p).isEmpty && !s.quarantinedButtons.contains(1), "old end releases only its remaining secondary")
        _ = s.focusLost()
        check(s.quarantinedButtons == [0], "old end cannot clear a newer primary hold")
        _ = s.pointerEnded(85, at: p)
        check(s.quarantinedButtons.isEmpty, "all UIKit-only holds can rearm")

        // GC can report the same physical release before UIKit ends the
        // active stream. A fence between those callbacks must not resurrect it.
        s = playing()
        _ = s.update(gamePaused: true)
        _ = s.pointerBegan(86, at: p, buttons: 1)
        _ = s.mouseButton(epoch: s.mouseEpoch, index: 0, down: false)
        _ = s.focusLost()
        check(!s.quarantinedButtons.contains(0), "GC up already retired the UIKit held cache before a fence")
        check(s.pointerMoved(86, to: CGPoint(x: 500, y: 500), buttons: 1).isEmpty && s.pointerEnded(86, at: p).isEmpty, "late UIKit tail after GC up is inert")
        check(s.pointerBegan(87, at: p, buttons: 1) == [.button(.pointerUIKit, index: 0, down: true)], "new menu click works after GC-first release")
        _ = s.pointerEnded(87, at: p)

        print("PASS \(checks) input session lifecycle checks: ownership/presented/inactive fences, release-to-rearm quarantine, device generations, single key/pointer authority, edge-triggered Command-L, recapture, cancel, hover dedupe, touch, pause pointer release, UIKit-only retired stream releases (synthetic; no device)")
    }
}
