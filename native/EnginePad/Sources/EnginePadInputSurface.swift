import SwiftUI
import UIKit

/// Transparent layer between the game picture and the overlay controls. It
/// receives direct touches (aim and taps), indirect pointer touches with their
/// button mask (UIKit mouse/trackpad fallback, including the system secondary
/// click), pointer hover, and hardware key presses as first responder. The
/// overlay buttons sit above it and keep their own touches and clicks.
final class EnginePadInputSurface: UIView {
    private let router: EnginePadInputRouter
    private var gamePresses = Set<ObjectIdentifier>()

    init(router: EnginePadInputRouter) {
        self.router = router
        super.init(frame: .zero)
        backgroundColor = .clear
        isOpaque = false
        isMultipleTouchEnabled = true
        addGestureRecognizer(UIHoverGestureRecognizer(target: self, action: #selector(hovered(_:))))
    }
    required init?(coder: NSCoder) { nil }

    override var canBecomeFirstResponder: Bool { true }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window != nil { router.surfaceAttached(self) } else { router.surfaceDetached(self) }
    }
    override func resignFirstResponder() -> Bool {
        let resigned = super.resignFirstResponder()
        if resigned { router.surfaceLostFocus() }
        return resigned
    }

    @objc private func hovered(_ recognizer: UIHoverGestureRecognizer) {
        router.hover(recognizer.state, at: recognizer.location(in: self))
    }

    // MARK: Touches: direct fingers and indirect pointer are routed separately.

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        let buttons = event?.buttonMask.rawValue ?? 0
        for touch in touches {
            switch touch.type {
            case .direct: router.touchBegan(Self.id(touch), at: touch.location(in: self), time: touch.timestamp)
            case .indirectPointer: router.pointerBegan(Self.id(touch), at: touch.location(in: self), buttons: Self.buttons(buttons))
            default: break
            }
        }
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        let buttons = event?.buttonMask.rawValue ?? 0
        for touch in touches {
            switch touch.type {
            case .direct: router.touchMoved(Self.id(touch), to: touch.location(in: self))
            case .indirectPointer: router.pointerMoved(Self.id(touch), to: touch.location(in: self), buttons: Self.buttons(buttons))
            default: break
            }
        }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            switch touch.type {
            case .direct: router.touchEnded(Self.id(touch), at: touch.location(in: self), time: touch.timestamp)
            case .indirectPointer: router.pointerEnded(Self.id(touch), at: touch.location(in: self))
            default: break
            }
        }
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            switch touch.type {
            case .direct: router.touchCancelled(Self.id(touch))
            case .indirectPointer: router.pointerCancelled(Self.id(touch))   /* no motion, no click */
            default: break
            }
        }
    }
    private static func id(_ touch: UITouch) -> Int { ObjectIdentifier(touch).hashValue }
    /// UIEvent.ButtonMask: bit 0 primary, bit 1 secondary, bit n button n+1.
    private static func buttons(_ mask: Int) -> Int { mask & ((1 << Int(HOST_DEVICE_BUTTON_COUNT)) - 1) }

    // MARK: Presses (UIKit keyboard fallback; swallowed when they belong to Halo)

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var others = Set<UIPress>()
        for press in presses {
            if let key = press.key, router.key(from: .keyboardUIKit, usage: key.keyCode.rawValue, down: true,
                                                command: key.modifierFlags.contains(.command)) {
                gamePresses.insert(ObjectIdentifier(press))
            } else {
                others.insert(press)
            }
        }
        if !others.isEmpty { super.pressesBegan(others, with: event) }
    }
    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let others = release(presses)
        if !others.isEmpty { super.pressesEnded(others, with: event) }
    }
    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let others = release(presses)
        if !others.isEmpty { super.pressesCancelled(others, with: event) }
    }
    /// Every release reaches the router (stale ones are ignored natively);
    /// returns the presses whose begin went up the responder chain.
    private func release(_ presses: Set<UIPress>) -> Set<UIPress> {
        var others = Set<UIPress>()
        for press in presses {
            if let key = press.key {
                router.key(from: .keyboardUIKit, usage: key.keyCode.rawValue, down: false, command: false)
            }
            if gamePresses.remove(ObjectIdentifier(press)) == nil { others.insert(press) }
        }
        return others
    }
}

struct EnginePadInputSurfaceView: UIViewRepresentable {
    let router: EnginePadInputRouter
    func makeUIView(context: Context) -> EnginePadInputSurface { EnginePadInputSurface(router: router) }
    func updateUIView(_ uiView: EnginePadInputSurface, context: Context) {}
}
