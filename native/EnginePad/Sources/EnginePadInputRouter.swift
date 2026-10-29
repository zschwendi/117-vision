import UIKit
import GameController
import Combine

/// Connects GameController keyboards/mice, the UIKit input surface, scene
/// lifecycle and the game view controller to EnginePadInputSession, and
/// executes its commands against the native device source (device_input.h).
/// All decisions live in the session; this adapter only translates events and
/// commands. The controller path and its app lease are untouched.
@MainActor
final class EnginePadInputRouter: ObservableObject {
    @Published private(set) var keyboardConnected = false
    @Published private(set) var mouseConnected = false
    @Published private(set) var controllerConnected = false
    @Published private(set) var touchControlsEnabled = false
    @Published private(set) var touchActionsAvailable: UInt32 = 0
    func touchActionAvailable(_ action: EnginePadTouchControls.Action) -> Bool {
        action.actions.contains { touchActionsAvailable & (1 << $0) != 0 }
    }
    func refreshTouchBindings() {
        let available = host_device_input_touch_actions_available()
        if available != touchActionsAvailable { touchActionsAvailable = available }
    }
    private var nativeMenuActive = true
    private var touchControls = EnginePadTouchControls()
    var touchMovementOffset: CGPoint { touchControls.movementOffset }
    var touchHeldActions: Set<EnginePadTouchControls.Action> { touchControls.heldActions }
    /// A UIKit pointer (for example a trackpad that is not a GCMouse) was used.
    @Published private(set) var pointerSeen = false
    /// Resolved scene state from UIPointerLockState, not the preference.
    @Published private(set) var pointerLocked = false
    /// The player released the pointer for app controls (Command-L, overlay).
    @Published private(set) var pointerReleased = false
    @Published private(set) var gameOwnsInput = false
    var prefersPointerLock: Bool { session.prefersPointerLock }

    private var session = EnginePadInputSession(
        isMapped: { host_device_dik_for_hid(UInt32(clamping: $0)) != 0 },
        physicallyPressed: EnginePadInputRouter.physicallyPressed)
    private weak var surface: EnginePadInputSurface?
    private weak var gameController: UIViewController?
    /// The scene the game is shown in; notifications for other scenes are ignored.
    private weak var scene: UIScene?
    private var observers: [NSObjectProtocol] = []

    init() {
        precondition(EnginePadInputSource.keyboardGC.rawValue == Int32(HOST_DEVICE_KEYBOARD_GC)
                     && EnginePadInputSource.pointerUIKit.rawValue == Int32(HOST_DEVICE_POINTER_UIKIT)
                     && EnginePadInputSource.touch.rawValue == Int32(HOST_DEVICE_TOUCH)
                     && EnginePadInputSession.buttonCount == Int(HOST_DEVICE_BUTTON_COUNT))
        let center = NotificationCenter.default
        for name in [Notification.Name.GCKeyboardDidConnect, .GCKeyboardDidDisconnect, .GCMouseDidConnect, .GCMouseDidDisconnect, .GCControllerDidConnect, .GCControllerDidDisconnect] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.devicesChanged() }
            })
        }
        observers.append(center.addObserver(forName: UIPointerLockState.didChangeNotification, object: nil, queue: .main) { [weak self] note in
            let changed = note.userInfo?[UIPointerLockState.sceneUserInfoKey] as? UIScene
            MainActor.assumeIsolated { self?.pointerLockChanged(in: changed) }
        })
        // Faster than SwiftUI's scenePhase: drop input the moment our scene
        // deactivates (Control Center, app switcher, Siri, notifications).
        for (name, active) in [(UIScene.willDeactivateNotification, false), (UIScene.didEnterBackgroundNotification, false),
                               (UIScene.didActivateNotification, true)] {
            observers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                let changed = note.object as? UIScene
                MainActor.assumeIsolated { self?.sceneNotification(changed, active: active) }
            })
        }
        devicesChanged()
    }

    deinit { observers.forEach(NotificationCenter.default.removeObserver) }

    // MARK: Lifecycle inputs

    /// SwiftUI scenePhase of the window showing the Library and game.
    func update(sceneActive: Bool) { run(session.update(sceneActive: sceneActive)) }
    /// The game is shown with no app sheet, importer or alert over it.
    func update(gameVisible: Bool) { run(session.update(gameVisible: gameVisible)) }
    func update(engineActive: Bool) { if engineActive != session.engineActive { run(session.update(engineActive: engineActive)) } }
    func update(gamePaused: Bool) { run(session.update(gamePaused: gamePaused)) }
    func update(nativeMenuActive: Bool) {
        guard self.nativeMenuActive != nativeMenuActive else { return }
        self.nativeMenuActive = nativeMenuActive
        reconcileTouchControls()
    }

    func surfaceAttached(_ view: EnginePadInputSurface) {
        surface = view
        adopt(view.window?.windowScene)
        run(session.update(surfaceAttached: true))
    }
    func surfaceDetached(_ view: EnginePadInputSurface) {
        guard surface === view || surface == nil else { return }
        surface = nil
        run(session.update(surfaceAttached: false))
    }
    func surfaceLostFocus() { run(session.focusLost()) }

    func gameControllerAppeared(_ controller: UIViewController) {
        gameController = controller
        adopt(controller.view.window?.windowScene)
        run(session.update(presented: true))
    }
    /// Deactivate at once; do not wait for SwiftUI bindings or window detach.
    func gameControllerDisappearing(_ controller: UIViewController) {
        guard gameController === controller else { return }
        run(session.update(presented: false))
        gameController = nil
    }

    func togglePointerCapture() { run(session.togglePointerCapture()) }
    /// The overlay Pause button: one native Escape press (Halo's pause/back).
    func sendPause() {
        cancelTouchControls()
        run(session.pause())
    }

    private func adopt(_ windowScene: UIScene?) {
        guard let windowScene else { return }
        scene = windowScene
        run(session.update(pointerLocked: windowScene.pointerLockState?.isLocked ?? false))
    }
    private func sceneNotification(_ changed: UIScene?, active: Bool) {
        // Deactivation of an unknown scene is safe to honor; activation only of ours.
        if let scene, changed !== scene { return }
        if scene == nil && active { return }
        run(session.update(sceneActive: active))
    }
    private func pointerLockChanged(in changed: UIScene?) {
        guard let changed, changed === scene else { return }
        run(session.update(pointerLocked: changed.pointerLockState?.isLocked ?? false))
    }

    // MARK: GameController devices

    private func devicesChanged() {
        let keyboard = GCKeyboard.coalesced, mice = GCMouse.mice()
        controllerConnected = GCController.controllers().contains { $0.extendedGamepad != nil }
        run(session.devicesChanged(keyboard: keyboard != nil, mouse: !mice.isEmpty))
        // New handlers carry the new device generation; any callback still
        // queued from an earlier one is ignored by the session.
        if let keyboard { attach(keyboard, epoch: session.keyboardEpoch) }
        mice.forEach { attach($0, epoch: session.mouseEpoch) }
    }

    private func attach(_ keyboard: GCKeyboard, epoch: UInt64) {
        keyboard.handlerQueue = .main
        keyboard.keyboardInput?.keyChangedHandler = { [weak self, weak keyboard] input, _, code, pressed in
            let command = (input.button(forKeyCode: .leftGUI)?.isPressed ?? false) ||
                (input.button(forKeyCode: .rightGUI)?.isPressed ?? false)
            let current = keyboard != nil && keyboard === GCKeyboard.coalesced
            MainActor.assumeIsolated {
                guard let self, current else { return }
                _ = self.key(from: .keyboardGC, epoch: epoch, usage: code.rawValue, down: pressed, command: command)
            }
        }
    }

    private func attach(_ mouse: GCMouse, epoch: UInt64) {
        mouse.handlerQueue = .main
        guard let input = mouse.mouseInput else { return }
        input.mouseMovedHandler = { [weak self] _, dx, dy in
            MainActor.assumeIsolated { self.map { $0.run($0.session.mouseMoved(epoch: epoch, dx: Double(dx), dy: Double(dy))) } }
        }
        for (index, button) in Self.buttons(of: input).enumerated() {
            button?.pressedChangedHandler = { [weak self] _, _, pressed in
                MainActor.assumeIsolated { self.map { $0.run($0.session.mouseButton(epoch: epoch, index: index, down: pressed)) } }
            }
        }
    }

    private nonisolated static func buttons(of input: GCMouseInput) -> [GCDeviceButtonInput?] {
        [input.leftButton, input.rightButton, input.middleButton] +
            (input.auxiliaryButtons ?? []).prefix(EnginePadInputSession.buttonCount - 3).map { Optional($0) }
    }

    /// Physically held keys and mouse buttons at a fence (GameController state).
    private nonisolated static func physicallyPressed() -> (keys: Set<Int>, buttons: Set<Int>) {
        var keys = Set<Int>(), pressed = Set<Int>()
        if let input = GCKeyboard.coalesced?.keyboardInput, input.isAnyKeyPressed {
            for usage in 0x04...0xE7 where input.button(forKeyCode: GCKeyCode(rawValue: usage))?.isPressed == true { keys.insert(usage) }
        }
        for mouse in GCMouse.mice() {
            guard let input = mouse.mouseInput else { continue }
            for (index, button) in buttons(of: input).enumerated() where button?.isPressed == true { pressed.insert(index) }
        }
        return (keys, pressed)
    }

    // MARK: Events from the input surface

    /// Returns whether the press belongs to the game, so the surface keeps it
    /// from the responder chain (where Space/Return could activate a control).
    @discardableResult
    func key(from source: EnginePadInputSource, epoch: UInt64? = nil, usage: Int, down: Bool, command: Bool) -> Bool {
        let result = session.key(from: source, epoch: epoch, usage: usage, down: down, command: command)
        run(result.commands)
        return result.swallow
    }
    func pointerBegan(_ id: Int, at point: CGPoint, buttons: Int) { run(session.pointerBegan(id, at: point, buttons: buttons)) }
    func pointerMoved(_ id: Int, to point: CGPoint, buttons: Int) { run(session.pointerMoved(id, to: point, buttons: buttons)) }
    func pointerEnded(_ id: Int, at point: CGPoint) { run(session.pointerEnded(id, at: point)) }
    func pointerCancelled(_ id: Int) { run(session.pointerCancelled(id)) }
    func hover(_ state: UIGestureRecognizer.State, at point: CGPoint) {
        let phase: EnginePadInputSession.HoverPhase = state == .began ? .began : state == .changed ? .changed : .ended
        run(session.hover(phase, at: point))
    }
    func touchBegan(_ id: Int, at point: CGPoint, time: TimeInterval) { run(session.touchBegan(id, at: point, time: time)) }
    func touchMoved(_ id: Int, to point: CGPoint) { run(session.touchMoved(id, to: point)) }
    func touchEnded(_ id: Int, at point: CGPoint, time: TimeInterval) { run(session.touchEnded(id, at: point, time: time)) }
    func touchCancelled(_ id: Int) { run(session.touchCancelled(id)) }

    // MARK: Gameplay-only touch fallback

    func touchControlBegan(_ id: Int, target: EnginePadTouchControls.Target, at point: CGPoint) {
        if case let .action(action) = target, !touchActionAvailable(action) { return }
        runTouch(touchControls.began(id, target: target, at: point))
    }
    func touchControlMoved(_ id: Int, to point: CGPoint) { runTouch(touchControls.moved(id, to: point)) }
    func touchControlEnded(_ id: Int) { runTouch(touchControls.ended(id)) }
    func cancelTouchControls() { runTouch(touchControls.cancelAll()) }

    private func reconcileTouchControls() {
        let enabled = EnginePadTouchControls.available(gameOwnsInput: session.gameOwnsInput,
            keyboardConnected: session.keyboardConnected, controllerConnected: controllerConnected,
            nativeMenuActive: nativeMenuActive)
        if enabled != touchControlsEnabled {
            session.cancelDirectTouch()
            runTouch(touchControls.update(enabled: enabled))
            touchControlsEnabled = enabled
        }
    }
    private func runTouch(_ commands: [EnginePadTouchControls.Command]) {
        for command in commands {
            switch command {
            case let .enabled(value): host_device_input_touch_enable(value ? 1 : 0)
            case .releaseAll: host_device_input_touch_release()
            case let .action(id, down): _ = host_device_input_touch_action(UInt32(id), down ? 1 : 0)
            case let .motion(dx, dy): _ = host_device_input_touch_motion(dx, dy)
            }
        }
    }

    // MARK: Command execution

    private func run(_ commands: [EnginePadInputSession.Command]) {
        for command in commands {
            switch command {
            case .setGameOwns, .release, .select:
                session.cancelDirectTouch()
                cancelTouchControls()
            default: break
            }
            switch command {
            case let .setGameOwns(owns): host_device_input_set_game_owns(owns ? 1 : 0)
            case .release: host_device_input_release()
            case let .select(keyboard, pointer): host_device_input_select(keyboard.rawValue, pointer.rawValue)
            case let .key(source, usage, down, chord):
                _ = host_device_input_key(source.rawValue, UInt32(usage), down ? 1 : 0, chord ? 1 : 0)
            case let .button(source, index, down): _ = host_device_input_button(source.rawValue, Int32(index), down ? 1 : 0)
            case let .cancelButton(source, index): _ = host_device_input_button_cancel(source.rawValue, Int32(index))
            case let .motion(source, dx, dy): _ = host_device_input_motion(source.rawValue, dx, dy)
            case let .click(index): _ = host_device_input_click(EnginePadInputSource.touch.rawValue, Int32(index))
            case let .keyPulse(dik): _ = host_device_input_key_pulse(dik)
            case .pointerLockPreference: gameController?.setNeedsUpdateOfPrefersPointerLocked()
            case let .focus(owns):
                // Keys follow ownership: app sheets keep their navigation and text.
                if owns, let surface, !surface.isFirstResponder { surface.becomeFirstResponder() }
                if !owns, let surface, surface.isFirstResponder { _ = surface.resignFirstResponder() }
            }
        }
        publish()
    }

    private func publish() {
        if keyboardConnected != session.keyboardConnected { keyboardConnected = session.keyboardConnected }
        if mouseConnected != session.mouseConnected { mouseConnected = session.mouseConnected }
        if pointerSeen != session.pointerSeen { pointerSeen = session.pointerSeen }
        if pointerLocked != session.pointerLocked { pointerLocked = session.pointerLocked }
        if pointerReleased != session.pointerReleased { pointerReleased = session.pointerReleased }
        if gameOwnsInput != session.gameOwnsInput { gameOwnsInput = session.gameOwnsInput }
        reconcileTouchControls()
    }
}
