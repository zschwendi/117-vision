import Foundation

/// Only deliberate, freshly released controller input can operate app
/// controls: a Sense pair (raw left stick) or an ordinary gamepad (left stick
/// or D-pad). Changing controller kind needs a fresh release, like a new
/// source. This reducer does not touch a controller, the engine, or SwiftUI focus.
struct EngineControllerNavigationInput {
    enum Event: Equatable { case move(Int), adjust(Int), activate, back }
    struct Sample {
        var source: UInt64
        var connected = true
        var spatial = true
        var x: Float = 0
        var y: Float = 0
        var activate = false
        var back = false
        var otherHeld = false
    }
    private var source: UInt64 = 0
    private var spatial = true
    private var lastTime: Double?
    private var armed = false
    private var direction = 0
    private var repeatAt = Double.infinity

    mutating func reset() { self = Self() }

    mutating func update(_ input: Sample, now: Double) -> Event? {
        guard now.isFinite, now >= 0 else { reset(); return nil }
        if let lastTime, now == lastTime { return nil }
        let discontinuity = lastTime.map { now < $0 || now - $0 > 0.3 } ?? true
        lastTime = now
        guard input.connected, input.source != 0,
              input.x.isFinite, input.y.isFinite, abs(input.x) <= 1, abs(input.y) <= 1 else {
            armed = false; source = 0; direction = 0; return nil
        }
        if discontinuity || input.source != source || input.spatial != spatial {
            armed = false; direction = 0; source = input.source; spatial = input.spatial
        }
        let centered = abs(input.x) <= 0.2 && abs(input.y) <= 0.2
        let neutral = centered && !input.activate && !input.back && !input.otherHeld
        if !armed {
            if neutral { armed = true }
            return nil
        }
        if input.otherHeld { armed = false; direction = 0; return nil }
        if input.activate || input.back {
            armed = false; direction = 0
            guard centered, input.activate != input.back else { return nil }
            return input.back ? .back : .activate
        }
        if centered { direction = 0; repeatAt = .infinity; return nil }
        let next: Int
        if abs(input.y) >= abs(input.x), abs(input.y) >= 0.55 { next = input.y > 0 ? -1 : 1 }
        else if abs(input.x) >= 0.55 { next = input.x > 0 ? 2 : -2 }
        else { return nil }
        if next != direction { direction = next; repeatAt = now + 0.42 }
        else if now >= repeatAt { repeatAt = now + 0.14 }
        else { return nil }
        return abs(next) == 1 ? .move(next) : .adjust(next / 2)
    }
}

struct EngineControllerFocus {
    struct Item: Equatable { var id: String; var enabled: Bool }
    private(set) var selected: String?
    mutating func reconcile(_ items: [Item]) {
        let enabled = items.filter(\.enabled)
        if !enabled.contains(where: { $0.id == selected }) { selected = enabled.first?.id }
    }
    mutating func move(_ direction: Int, in items: [Item]) {
        reconcile(items)
        let enabled = items.filter(\.enabled)
        guard !enabled.isEmpty, let index = enabled.firstIndex(where: { $0.id == selected }) else { return }
        selected = enabled[(index + (direction < 0 ? enabled.count - 1 : 1)) % enabled.count].id
    }
}

/// Controller focus is drawn only while a controller is actually navigating.
/// Touch, gaze and pinch users never see a stale selection outline, and the
/// first stick or Cross press after an idle period only reveals the current
/// selection: nothing activates or changes that the user could not see.
struct EngineControllerHighlight {
    static let idleTimeout = 10.0
    private(set) var visible = false
    private var lastInput = -Double.infinity

    mutating func reset() { self = Self() }
    /// A deliberate controller gesture opened the surface (the Sense chord).
    mutating func reveal(now: Double) { visible = true; lastInput = now }
    /// Returns true when the event should act on the current selection.
    mutating func accept(_ event: EngineControllerNavigationInput.Event, now: Double) -> Bool {
        let wasVisible = visible && now.isFinite && now - lastInput <= Self.idleTimeout
        visible = true; lastInput = now
        if event == .back { return true }
        return wasVisible
    }
    /// A touch or gaze-and-pinch hides the highlight; the next controller
    /// input reveals it again without acting.
    mutating func pointerInteraction() { visible = false }
    /// Hides an idle highlight. Returns true when visibility changed.
    mutating func expire(now: Double) -> Bool {
        guard visible, !now.isFinite || now < lastInput || now - lastInput > Self.idleTimeout else { return false }
        visible = false
        return true
    }
}

/// Connected-controller state for launcher cues. Halo needs a controller to
/// play; this only describes the hardware and never blocks setup or import.
struct EngineControllerPresence: Equatable {
    enum Kind: Equatable { case none, gamepad, sense }
    var kind: Kind
    var detail: String

    init(connected: Bool, spatial: Bool, diagnostic: String) {
        kind = !connected ? .none : spatial ? .sense : .gamepad
        let trimmed = diagnostic.trimmingCharacters(in: .whitespacesAndNewlines)
        detail = trimmed.isEmpty ? (connected ? "Ready" : "No controller connected") : trimmed
    }
    static let checking = EngineControllerPresence(connected: false, spatial: false, diagnostic: "Checking controller")

    var connected: Bool { kind != .none }
    var title: String {
        switch kind {
        case .none: "No controller detected"
        case .gamepad: "Controller connected"
        case .sense: "PS VR2 Sense connected"
        }
    }
    var symbol: String {
        switch kind {
        case .none: "gamecontroller"
        case .gamepad, .sense: "gamecontroller.fill"
        }
    }
    /// One line for the launcher; the hardware diagnostic explains partial
    /// Sense pairs instead of reporting them as ready.
    var summary: String {
        switch kind {
        case .none: detail == "No controller connected" ? "Halo needs a game controller to play." : detail
        case .gamepad, .sense: detail
        }
    }
}

/// When the small in-game Settings affordance is offered. `menuActive` is the
/// engine's genuine in-game menu state (a pausing widget or non-campaign UI
/// root, never the front-end shell), not a guessed button press. Only the tiny
/// affordance appears; the settings menu itself always waits for the user.
struct EnginePauseAffordance {
    enum Surface: Equatable { case none, window, immersive }
    struct Inputs: Equatable {
        var surface: Surface = .none
        var engineRunning = false
        var hasFrame = false
        var menuActive = false
        var transitionBusy = false
        var quitBlocked = false
        /// The app's own game menu or settings window is already showing.
        var appMenuOpen = false
        /// Background or inactive apps never open an auxiliary scene. No
        /// default: every caller must state the real app scene phase.
        var applicationActive: Bool
    }
    static let showDelay = 0.3
    static let hideDelay = 0.4
    private(set) var visible = false
    private var menuSince: Double?
    private var idleSince: Double?
    private var suppressed = false

    /// The user closed the affordance; keep it away for this menu visit.
    mutating func userDismissed() { suppressed = true; visible = false }

    mutating func update(_ inputs: Inputs, now: Double) -> Bool {
        let eligible = inputs.applicationActive && inputs.surface != .none && inputs.engineRunning
            && inputs.hasFrame && !inputs.quitBlocked && now.isFinite
        guard eligible else { self = Self(); return false }
        if inputs.menuActive {
            idleSince = nil
            if menuSince == nil || now < menuSince! { menuSince = now }
        } else {
            menuSince = nil
            if idleSince == nil || now < idleSince! { idleSince = now }
            if now - idleSince! >= Self.hideDelay { visible = false; suppressed = false }
        }
        if inputs.transitionBusy || inputs.appMenuOpen || suppressed {
            visible = false
        } else if let menuSince, now - menuSince >= Self.showDelay {
            visible = true
        }
        return visible
    }
}

/// Quit Game is terminal for this process: the original engine cannot be
/// restarted in place. Presentation closes first through the serialized
/// owner, then a stop is requested and the outcome is reported truthfully.
struct EngineQuitFlow {
    enum Phase: Equatable { case idle, closingPresentation, stopping, stopped, stillStopping }
    static let stopTimeout = 12.0
    private(set) var phase = Phase.idle
    private var stopRequestedAt: Double?

    /// Every presentation or play request is refused once Quit begins.
    var blocksPresentation: Bool { phase != .idle }
    var inProgress: Bool { phase == .closingPresentation || phase == .stopping }

    static func canBegin(engineActive: Bool, presentationBusy: Bool, preparingPlay: Bool, saveBusy: Bool) -> Bool {
        engineActive && !presentationBusy && !preparingPlay && !saveBusy
    }
    mutating func begin(engineActive: Bool, presentationBusy: Bool, preparingPlay: Bool, saveBusy: Bool) -> Bool {
        guard phase == .idle, Self.canBegin(engineActive: engineActive, presentationBusy: presentationBusy,
                                            preparingPlay: preparingPlay, saveBusy: saveBusy) else { return false }
        phase = .closingPresentation
        return true
    }
    /// The game presentation has closed. Returns true exactly once, when the
    /// terminal engine stop should be requested.
    mutating func presentationClosed(now: Double) -> Bool {
        guard phase == .closingPresentation else { return false }
        phase = .stopping; stopRequestedAt = now
        return true
    }
    mutating func observe(engineActive: Bool, now: Double) {
        guard phase == .stopping || phase == .stillStopping else { return }
        if !engineActive { phase = .stopped; return }
        if phase == .stopping, let stopRequestedAt, !now.isFinite || now - stopRequestedAt >= Self.stopTimeout {
            phase = .stillStopping
        }
    }
    func message(appName: String) -> String? {
        switch phase {
        case .idle: nil
        case .closingPresentation: "Closing the game view…"
        case .stopping: "Quitting Halo…"
        case .stopped: "Halo has quit. Saved games stay on this headset. Close and reopen \(appName) to play again."
        case .stillStopping: "Halo is still closing. Saved games stay on this headset. Close and reopen \(appName) to play again."
        }
    }
}

/// Entry chord is independent of surface navigation, so it also works when
/// the controls window is closed. Held buttons at connection never open it.
struct EngineControllerEntryChord {
    private var source: UInt64 = 0
    private var armed = false
    private var lastTime: Double?
    mutating func update(source next: UInt64, spatial: Bool, create: Bool, options: Bool,
                         otherwiseNeutral: Bool, now: Double) -> Bool {
        guard now.isFinite, now >= 0 else { self = Self(); return false }
        if now == lastTime { return false }
        if next != source || lastTime.map({ now < $0 || now - $0 > 0.3 }) ?? true {
            armed = false; source = next
        }
        lastTime = now
        guard spatial, next != 0 else { armed = false; return false }
        if !create && !options && otherwiseNeutral { armed = true; return false }
        guard otherwiseNeutral else { armed = false; return false }
        if create && options && armed { armed = false; return true }
        return false
    }
}
