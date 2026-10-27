import Foundation
import CoreGraphics

@main struct EnginePadTouchControlsValidation {
    static var count = 0
    static func check(_ value: Bool, _ reason: String) { count += 1; precondition(value, reason) }
    static func main() {
        typealias T = EnginePadTouchControls
        for owns in [false, true] {
            for keyboard in [false, true] {
                for controller in [false, true] {
                    for menu in [false, true] {
                        check(T.available(gameOwnsInput: owns, keyboardConnected: keyboard,
                                          controllerConnected: controller, nativeMenuActive: menu)
                              == (owns && !keyboard && !controller && !menu), "presence/gameplay gate")
                    }
                }
            }
        }
        var state = T()
        let zero = CGPoint.zero
        check(state.began(1, target: .action(.fire), at: zero).isEmpty, "disabled ingress")
        check(state.update(enabled: true) == [.enabled(true)], "enable native lane")
        check(state.update(enabled: true).isEmpty, "stable ownership is not a fence")
        check(state.began(1, target: .movement(center: zero, radius: 50), at: CGPoint(x: 0, y: -50))
              == [.action(id: 19, down: true)], "movement W")
        check(state.began(2, target: .look, at: zero).isEmpty, "look anchors with no jump")
        check(state.began(3, target: .action(.fire), at: zero) == [.action(id: 7, down: true)], "hold fire")
        check(state.moved(2, to: CGPoint(x: 20, y: -10)) == [.motion(dx: 30, dy: -15)], "look while walking/firing")
        check(state.moved(3, to: CGPoint(x: 4, y: 2)) == [.motion(dx: 6, dy: 3)], "fire drag also aims")
        check(state.began(4, target: .action(.grenade), at: zero) == [.action(id: 6, down: true)], "grenade is secondary")
        check(state.heldActions == [.fire, .grenade], "independent held actions")
        check(state.ended(4) == [.action(id: 6, down: false)], "grenade releases independently")
        check(state.heldActions == [.fire], "grenade release does not cancel fire")
        check(state.moved(1, to: CGPoint(x: 50, y: 0)) == [.action(id: 19, down: false), .action(id: 22, down: true)], "turn stick direction releases old key first")
        check(state.moved(1, to: CGPoint(x: 11, y: 0)).isEmpty, "deadzone hysteresis keeps intentional hold")
        check(state.moved(1, to: CGPoint(x: 8, y: 0)) == [.action(id: 22, down: false)], "deadzone releases at rest")
        check(state.began(5, target: .movement(center: zero, radius: 50), at: CGPoint(x: 0, y: -50)).isEmpty, "second move finger never steals")
        check(state.ended(1).isEmpty && state.movementOffset == .zero, "stick recenters")
        check(state.moved(5, to: CGPoint(x: 0, y: -50)).isEmpty, "ignored finger does not become owner later")
        check(state.began(6, target: .action(.fire), at: zero).isEmpty, "two fingers on fire share a hold")
        check(state.ended(3).isEmpty && state.heldActions == [.fire], "first fire finger release keeps second")
        check(state.ended(6) == [.action(id: 7, down: false)], "last fire owner releases")
        check(state.ended(6).isEmpty, "duplicate end ignored")
        _ = state.began(8, target: .action(.crouch), at: zero)
        check(state.cancelAll() == [.releaseAll] && state.heldActions.isEmpty, "pause/cancel clears everything")
        check(state.moved(2, to: CGPoint(x: 100, y: 90)).isEmpty && state.ended(8).isEmpty, "old callbacks after cancellation are inert")
        _ = state.began(9, target: .action(.fire), at: zero)
        check(state.update(enabled: false) == [.enabled(false)], "hot-plug/background/menu hides and releases")
        check(state.heldActions.isEmpty && state.movementOffset == .zero, "hide clears feedback")
        _ = state.update(enabled: true)
        check(state.moved(9, to: CGPoint(x: 10, y: 10)).isEmpty && state.ended(9).isEmpty, "reappear requires a new touch-down")
        for action in T.Action.allCases {
            let down = state.began(20, target: .action(action), at: zero)
            let up = state.ended(20)
            check(down == action.actions.map { .action(id: $0, down: true) }
                  && up == action.actions.map { .action(id: $0, down: false) }, action.title)
        }
        check(T.Action.zoom.actions == [11] && T.Action.melee.actions == [4]
              && T.Action.grenadeCycle.actions == [1] && T.Action.flashlight.actions == [5], "actual semantic action IDs")
        check(T.Action.use.actions == [2, 14] && T.Action.reload.actions == [13], "use/pickup and reload remain distinct")
        check(state.began(21, target: .look, at: CGPoint(x: CGFloat.nan, y: 0)).isEmpty, "invalid coordinate rejected")
        check(state.began(22, target: .movement(center: zero, radius: 0), at: zero).isEmpty, "invalid stick rejected")
        for size in [CGSize(width: 320, height: 568), CGSize(width: 393, height: 852),
                     CGSize(width: 852, height: 393), CGSize(width: 568, height: 320),
                     CGSize(width: 507, height: 768), CGSize(width: 1024, height: 1366),
                     CGSize(width: 1366, height: 1024)] {
            let layout = EnginePadTouchLayout(size: size)
            let screen = CGRect(origin: .zero, size: size)
            check(layout.buttons.count == T.Action.allCases.count, "every action fits")
            check(screen.contains(layout.stickHitFrame), "stick hit region fits \(size)")
            for (action, frame) in layout.buttons {
                check(frame.width >= 44 && frame.height >= 44 && screen.contains(frame), "button target size/inbounds \(size) \(action)")
                check(!frame.intersects(layout.stickHitFrame), "button does not steal stick touches \(size) \(action)")
                for (other, otherFrame) in layout.buttons where action.rawValue < other.rawValue {
                    check(!frame.intersects(otherFrame), "buttons never overlap \(size) \(action)/\(other)")
                }
            }
        }
        print("PASS \(count) touch fallback lifecycle/action checks (synthetic; no device)")
    }
}
