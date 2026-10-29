import Foundation
import CoreGraphics

/// Every input decision the router makes, as a pure value: events in, native
/// commands out. The router only executes the commands (device_input.h,
/// first responder, pointer-lock preference), so this is the code under test.
///
/// - Ownership: input reaches Halo only while EnginePadInputPolicy says the
///   game owns it and the game controller is presented. Each change, focus
///   loss, source change, disconnect or explicit pointer release is a fence.
/// - Quarantine: a key or button held (or physically pressed) at a fence
///   stays dead until it is released and pressed again. Repeated downs and
///   callbacks from a replaced device generation are dropped.
/// - Arbitration: only the selected keyboard and pointer sources reach
///   Halo. UIKit hover is ignored while an indirect pointer stream (a press or
///   drag) is active, so one movement is never added twice.
struct EnginePadInputSession {
    enum Command: Equatable {
        case setGameOwns(Bool)
        case release
        case select(keyboard: EnginePadInputSource, pointer: EnginePadInputSource)
        case key(EnginePadInputSource, usage: Int, down: Bool, chord: Bool)
        case button(EnginePadInputSource, index: Int, down: Bool)
        case cancelButton(EnginePadInputSource, index: Int)
        case motion(EnginePadInputSource, dx: Double, dy: Double)
        case click(index: Int)
        case keyPulse(UInt8)
        case pointerLockPreference(Bool)
        case focus(Bool)
    }
    static let escapeDIK: UInt8 = 0x01, periodUsage = 0x37, buttonCount = 8
    static let touchCountsPerPoint = 1.5, pointerCountsPerPoint = 1.5, mouseCountsPerDelta = 1.0

    /// HID usage -> whether Halo has a key for it (host_device_dik_for_hid).
    let isMapped: (Int) -> Bool
    /// Keys and mouse buttons physically down right now (GameController), read
    /// at each fence so in-flight presses are quarantined too.
    let physicallyPressed: () -> (keys: Set<Int>, buttons: Set<Int>)

    private(set) var sceneActive = false, gameVisible = false, surfaceAttached = false
    private(set) var presented = false, engineActive = false
    private(set) var gamePaused = false
    private(set) var keyboardConnected = false, mouseConnected = false
    private(set) var pointerLocked = false, pointerReleased = false, pointerSeen = false
    private(set) var gameOwnsInput = false, prefersPointerLock = false
    private(set) var keyboardSource = EnginePadInputSource.none, pointerSource = EnginePadInputSource.none
    private(set) var keyboardEpoch: UInt64 = 0, mouseEpoch: UInt64 = 0
    private var heldKeys: [EnginePadInputSource: Set<Int>] = [:]
    private var heldButtons: [EnginePadInputSource: Set<Int>] = [:]
    private(set) var quarantinedKeys = Set<Int>(), quarantinedButtons = Set<Int>()
    private var captureChord = Set<EnginePadInputSource>()
    private var touch = EnginePadTouchAim()
    private var hoverLast: CGPoint?
    private struct Stream { var buttons: Int; var last: CGPoint; var swallowed: Bool }
    private var streams: [Int: Stream] = [:]
    /// Fenced UIKit streams are release-only tombstones. UIKit-only pointers
    /// have no GCMouse up callback, so dropping their identity strands the
    /// held-button quarantine. They may clear old holds, never inject input.
    private var retiredStreams: [Int: Int] = [:]

    init(isMapped: @escaping (Int) -> Bool,
         physicallyPressed: @escaping () -> (keys: Set<Int>, buttons: Set<Int>)) {
        self.isMapped = isMapped
        self.physicallyPressed = physicallyPressed
    }

    // MARK: Lifecycle

    mutating func update(sceneActive: Bool) -> [Command] { self.sceneActive = sceneActive; return reconcile() }
    mutating func update(gameVisible: Bool) -> [Command] { self.gameVisible = gameVisible; return reconcile() }
    mutating func update(engineActive: Bool) -> [Command] { self.engineActive = engineActive; return reconcile() }
    mutating func update(surfaceAttached: Bool) -> [Command] { self.surfaceAttached = surfaceAttached; return reconcile() }
    /// The full-screen game controller appeared or is disappearing.
    mutating func update(presented: Bool) -> [Command] { self.presented = presented; return reconcile() }
    mutating func update(pointerLocked: Bool) -> [Command] { self.pointerLocked = pointerLocked; return reconcile() }

    /// Pause exposes native app controls. Release the system pointer without
    /// changing the player's capture preference, and quarantine a held click
    /// on both edges so Resume cannot become a shot in the next game frame.
    mutating func update(gamePaused: Bool) -> [Command] {
        guard self.gamePaused != gamePaused else { return [] }
        self.gamePaused = gamePaused
        fence()
        return [.release] + reconcile()
    }

    /// UIKit presses stop once the surface loses first responder.
    mutating func focusLost() -> [Command] { fence(); return [.release] }

    /// Devices were (re)enumerated and their handlers replaced: callbacks of
    /// earlier generations, including pending releases, are now ignored, so
    /// this always fences (no native hold can be stranded). The replaced
    /// handlers' held cache is dropped; only what GameController reports
    /// physically down now stays quarantined, so a new device's first press
    /// is not suppressed by a disconnected device's stale state.
    mutating func devicesChanged(keyboard: Bool, mouse: Bool) -> [Command] {
        keyboardConnected = keyboard; mouseConnected = mouse
        keyboardEpoch &+= 1; mouseEpoch &+= 1
        heldKeys[.keyboardGC] = nil; heldButtons[.pointerGC] = nil; captureChord.remove(.keyboardGC)
        fence()
        return [.release] + reconcile()
    }

    /// Command-L, the overlay button, or a click on the game while released.
    /// Releasing fences at once, before any pointer-lock notification.
    mutating func togglePointerCapture() -> [Command] {
        pointerReleased.toggle()
        var commands: [Command] = []
        if pointerReleased { fence(); commands.append(.release) }
        return commands + reconcile()
    }

    mutating func pause() -> [Command] { gameOwnsInput ? [.keyPulse(Self.escapeDIK)] : [] }

    /// Changing between menu touch and gameplay controls invalidates old fingers.
    mutating func cancelDirectTouch() { touch.reset() }

    private mutating func reconcile() -> [Command] {
        var commands: [Command] = []
        let owns = presented && EnginePadInputPolicy.gameOwnsInput(sceneActive: sceneActive, gameVisible: gameVisible,
                                                                   surfaceAttached: surfaceAttached, engineActive: engineActive)
        if owns != gameOwnsInput {
            gameOwnsInput = owns; fence()
            commands += [.setGameOwns(owns), .focus(owns)]
        }
        let keyboard = EnginePadInputPolicy.keyboardSource(gameControllerKeyboard: keyboardConnected)
        let pointer = EnginePadInputPolicy.pointerSource(pointerLocked: pointerLocked, gameControllerMouse: mouseConnected)
        if keyboard != keyboardSource || pointer != pointerSource {
            keyboardSource = keyboard; pointerSource = pointer; fence()
            commands.append(.select(keyboard: keyboard, pointer: pointer))
        }
        let wants = EnginePadInputPolicy.prefersPointerLock(gameOwnsInput: owns && !gamePaused, gameControllerMouse: mouseConnected,
                                                            userReleased: pointerReleased)
        if wants != prefersPointerLock { prefersPointerLock = wants; commands.append(.pointerLockPreference(wants)) }
        return commands
    }

    /// Local half of a fence (the native half is .release/.setGameOwns/.select).
    private mutating func fence() {
        let physical = physicallyPressed()
        quarantinedKeys = heldKeys.values.reduce(physical.keys) { $0.union($1) }
        quarantinedButtons = heldButtons.values.reduce(physical.buttons) { $0.union($1) }
        let uikitHeldMask = heldButtons[.pointerUIKit, default: []].reduce(0) { $0 | (1 << $1) }
        for (id, stream) in streams where !stream.swallowed {
            let held = stream.buttons & uikitHeldMask
            if held != 0 { retiredStreams[id] = held }
        }
        for mask in retiredStreams.values {
            for bit in 0..<Self.buttonCount where mask & (1 << bit) != 0 { quarantinedButtons.insert(bit) }
        }
        // Their holds now belong to the tombstones, not a live source cache.
        heldButtons[.pointerUIKit] = nil
        touch.reset(); streams.removeAll(); hoverLast = nil
    }

    // MARK: Keyboard

    /// Returns the commands and whether a UIKit press belongs to the game (the
    /// surface then keeps it from the responder chain). `epoch` is checked for
    /// GameController callbacks only.
    mutating func key(from source: EnginePadInputSource, epoch: UInt64? = nil, usage: Int, down: Bool,
                      command: Bool) -> (commands: [Command], swallow: Bool) {
        if let epoch, epoch != keyboardEpoch { return ([], false) }
        guard (0..<256).contains(usage) else { return ([], false) }
        let repeated = down && heldKeys[source, default: []].contains(usage)
        if down { heldKeys[source, default: []].insert(usage) } else { heldKeys[source]?.remove(usage); quarantinedKeys.remove(usage) }
        // Command-L: one toggle per physical press of L, even if Command is
        // released before L or the press repeats.
        if usage == EnginePadInputPolicy.captureToggleUsage, down ? command || captureChord.contains(source) : captureChord.contains(source) {
            var commands: [Command] = []
            if down, !captureChord.contains(source) {
                // An L already held (repeat) or quarantined is not a new edge;
                // its release then takes the ordinary path below next time.
                guard !repeated, !quarantinedKeys.contains(usage) else { return ([], gameOwnsInput) }
                captureChord.insert(source)
                if source == keyboardSource, gameOwnsInput { commands = togglePointerCapture() }
            } else if !down {
                captureChord.remove(source)
            }
            return (commands, gameOwnsInput)
        }
        let swallow = gameOwnsInput && isMapped(usage) && (!command || usage == Self.periodUsage)
        if down, repeated || quarantinedKeys.contains(usage) { return ([], swallow) }
        guard source == keyboardSource, !down || gameOwnsInput else { return ([], swallow) }
        return ([.key(source, usage: usage, down: down, chord: command)], swallow)
    }

    // MARK: GameController mouse

    mutating func mouseButton(epoch: UInt64, index: Int, down: Bool) -> [Command] {
        guard epoch == mouseEpoch, (0..<Self.buttonCount).contains(index) else { return [] }
        return pointerButton(.pointerGC, index: index, down: down)
    }
    mutating func mouseMoved(epoch: UInt64, dx: Double, dy: Double) -> [Command] {
        // Released for app controls: raw motion stops at once, before iPadOS
        // reports the lock change.
        guard epoch == mouseEpoch, gameOwnsInput, pointerSource == .pointerGC, !pointerReleased, !gamePaused else { return [] }
        // GameController reports +y up; DirectInput mouse +y is down.
        return [.motion(.pointerGC, dx: dx * Self.mouseCountsPerDelta, dy: -dy * Self.mouseCountsPerDelta)]
    }

    private mutating func pointerButton(_ source: EnginePadInputSource, index: Int, down: Bool) -> [Command] {
        let repeated = down && heldButtons[source, default: []].contains(index)
        if down { heldButtons[source, default: []].insert(index) } else { pointerReleased(index: index) }
        if down, repeated || quarantinedButtons.contains(index) { return [] }
        guard source == pointerSource, !down || (gameOwnsInput && !(source == .pointerGC && (pointerReleased || gamePaused))) else { return [] }
        return [.button(source, index: index, down: down)]
    }

    /// Both APIs describe the same system pointer. Either API's physical up
    /// retires a fenced hold in the other, so a later source change cannot
    /// resurrect it. Live UIKit streams retain their own end bookkeeping.
    private mutating func pointerReleased(index: Int) {
        heldButtons[.pointerGC]?.remove(index)
        heldButtons[.pointerUIKit]?.remove(index)
        quarantinedButtons.remove(index)
        for id in Array(retiredStreams.keys) {
            let remaining = (retiredStreams[id] ?? 0) & ~(1 << index)
            retiredStreams[id] = remaining == 0 ? nil : remaining
        }
    }

    private mutating func releaseRetiredButtons(_ mask: Int) {
        for bit in 0..<Self.buttonCount where mask & (1 << bit) != 0 { pointerReleased(index: bit) }
    }

    // MARK: UIKit pointer fallback

    private var uikitPointerDrivesGame: Bool { gameOwnsInput && pointerSource == .pointerUIKit && (!pointerReleased || gamePaused) }

    mutating func pointerBegan(_ id: Int, at point: CGPoint, buttons: Int) -> [Command] {
        pointerSeen = true; hoverLast = nil
        guard gameOwnsInput, retiredStreams[id] == nil else { return [] }
        if pointerReleased && !gamePaused {
            // A click on the game recaptures the pointer; it is not a shot.
            streams[id] = Stream(buttons: 0, last: point, swallowed: true)
            return togglePointerCapture()
        }
        streams[id] = Stream(buttons: 0, last: point, swallowed: false)
        return pointerMoved(id, to: point, buttons: buttons)
    }

    mutating func pointerMoved(_ id: Int, to point: CGPoint, buttons: Int) -> [Command] {
        if let retired = retiredStreams[id] {
            // A button-mask change can deliver an up before touchesEnded.
            // Ignore its position and any new downs from this old stream.
            releaseRetiredButtons(retired & ~buttons)
            return []
        }
        guard var stream = streams[id], !stream.swallowed, uikitPointerDrivesGame else { return [] }
        var commands: [Command] = []
        if point != stream.last {
            commands.append(.motion(.pointerUIKit, dx: Double(point.x - stream.last.x) * Self.pointerCountsPerPoint,
                                    dy: Double(point.y - stream.last.y) * Self.pointerCountsPerPoint))
        }
        stream.last = point
        let changed = stream.buttons ^ buttons
        for bit in 0..<Self.buttonCount where changed & (1 << bit) != 0 {
            commands += pointerButton(.pointerUIKit, index: bit, down: buttons & (1 << bit) != 0)
        }
        stream.buttons = buttons
        streams[id] = stream
        return commands
    }

    mutating func pointerEnded(_ id: Int, at point: CGPoint) -> [Command] {
        if let retired = retiredStreams.removeValue(forKey: id) {
            releaseRetiredButtons(retired)
            hoverLast = nil
            return []
        }
        guard streams[id] != nil else { return [] }
        let commands = pointerMoved(id, to: point, buttons: 0)
        streams[id] = nil; hoverLast = nil
        return commands
    }

    /// A cancelled stream adds no motion and no click; its held buttons are
    /// cancelled natively (queued presses dropped, a seen press released).
    mutating func pointerCancelled(_ id: Int) -> [Command] {
        if let retired = retiredStreams.removeValue(forKey: id) {
            releaseRetiredButtons(retired)
            hoverLast = nil
            return []
        }
        guard let stream = streams.removeValue(forKey: id) else { return [] }
        hoverLast = nil
        var commands: [Command] = []
        for bit in 0..<Self.buttonCount where stream.buttons & (1 << bit) != 0 {
            pointerReleased(index: bit)
            commands.append(.cancelButton(.pointerUIKit, index: bit))
        }
        return commands
    }

    enum HoverPhase { case began, changed, ended }
    mutating func hover(_ phase: HoverPhase, at point: CGPoint) -> [Command] {
        pointerSeen = true
        guard streams.isEmpty && retiredStreams.isEmpty else { hoverLast = nil; return [] } // a live or fenced press owns pointer motion
        switch phase {
        case .began: hoverLast = point; return []
        case .ended: hoverLast = nil; return []
        case .changed:
            defer { hoverLast = point }
            guard let last = hoverLast, uikitPointerDrivesGame, point != last else { return [] }
            return [.motion(.pointerUIKit, dx: Double(point.x - last.x) * Self.pointerCountsPerPoint,
                            dy: Double(point.y - last.y) * Self.pointerCountsPerPoint)]
        }
    }

    // MARK: Direct touch

    mutating func touchBegan(_ id: Int, at point: CGPoint, time: TimeInterval) -> [Command] {
        if gameOwnsInput { touch.began(id, at: point, time: time) }
        return []
    }
    mutating func touchMoved(_ id: Int, to point: CGPoint) -> [Command] {
        gameOwnsInput ? touchCommands(touch.moved(id, to: point)) : []
    }
    mutating func touchEnded(_ id: Int, at point: CGPoint, time: TimeInterval) -> [Command] {
        gameOwnsInput ? touchCommands(touch.ended(id, at: point, time: time)) : []
    }
    mutating func touchCancelled(_ id: Int) -> [Command] { touch.cancelled(id); return [] }

    private func touchCommands(_ outputs: [EnginePadTouchAim.Output]) -> [Command] {
        outputs.map {
            switch $0 {
            case let .aim(dx, dy): return .motion(.touch, dx: dx * Self.touchCountsPerPoint, dy: dy * Self.touchCountsPerPoint)
            case .primaryClick: return .click(index: 0)
            case .secondaryClick: return .click(index: 1)
            }
        }
    }
}
