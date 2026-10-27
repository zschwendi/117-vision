import Foundation
import CoreGraphics

/// Native input sources (device_input.h). Raw values match the C enum; the
/// router checks that once. Pure Swift so the policy below is testable on a Mac.
enum EnginePadInputSource: Int32 {
    case none = 0, keyboardGC = 1, keyboardUIKit = 2, pointerGC = 3, pointerUIKit = 4, touch = 5
}

/// Direct-screen touch: one-finger drag aims; a one-finger tap is a primary
/// click; a confirmed simultaneous two-finger tap is a secondary click. Clicks
/// are decided only when every finger has lifted, so a second finger joining
/// never produces an early primary. Any drag past `slop`, a late second finger,
/// a third finger, a cancellation or a long hold produces no click. There is
/// no touch hold-to-fire. Indirect pointer touches never reach this type.
struct EnginePadTouchAim {
    enum Output: Equatable { case aim(dx: Double, dy: Double), primaryClick, secondaryClick }
    static let slop: CGFloat = 10                  // points before a touch is a drag
    static let tapDuration: TimeInterval = 0.35    // first down to last up
    static let joinWindow: TimeInterval = 0.2      // second finger must land this soon

    private struct Track { var start: CGPoint; var last: CGPoint; var moved: Bool }
    private var tracks: [Int: Track] = [:]
    private var firstDown: TimeInterval = 0
    private var seen = 0, peak = 0
    private var eligible = false
    private var aimID: Int?

    var isIdle: Bool { tracks.isEmpty }

    mutating func began(_ id: Int, at point: CGPoint, time: TimeInterval) {
        if tracks.isEmpty { firstDown = time; seen = 0; peak = 0; eligible = true; aimID = id }
        guard tracks[id] == nil else { return }
        tracks[id] = Track(start: point, last: point, moved: false)
        seen += 1; peak = max(peak, tracks.count)
        if seen > 2 || time - firstDown > Self.joinWindow { eligible = false }
    }

    mutating func moved(_ id: Int, to point: CGPoint) -> [Output] {
        guard var track = tracks[id] else { return [] }
        var out: [Output] = []
        if !track.moved, hypot(point.x - track.start.x, point.y - track.start.y) > Self.slop {
            track.moved = true; eligible = false
        }
        if track.moved {
            // The first aim step includes the slop, so the aim does not lag the finger.
            if id == aimID, tracks.count < 3, point != track.last {
                out.append(.aim(dx: Double(point.x - track.last.x), dy: Double(point.y - track.last.y)))
            }
            track.last = point
        }
        tracks[id] = track
        return out
    }

    mutating func ended(_ id: Int, at point: CGPoint, time: TimeInterval) -> [Output] {
        guard tracks[id] != nil else { return [] }
        var out = moved(id, to: point)
        tracks[id] = nil
        if id == aimID { aimID = nil }
        guard tracks.isEmpty, eligible, time - firstDown <= Self.tapDuration else { return out }
        if seen == 1 { out.append(.primaryClick) } else if seen == 2, peak == 2 { out.append(.secondaryClick) }
        return out
    }

    mutating func cancelled(_ id: Int) {
        guard tracks[id] != nil else { return }
        tracks[id] = nil; eligible = false
        if id == aimID { aimID = nil }
    }

    /// Ownership changed: forget every finger; their later events are ignored.
    mutating func reset() { tracks = [:]; aimID = nil; eligible = false }
}

enum EnginePadInputPolicy {
    /// Hardware and touch input reach Halo only while the scene is active
    /// (not .inactive or .background), the game surface is visible in a
    /// window with no app sheet/importer/alert over it, and Halo is running.
    static func gameOwnsInput(sceneActive: Bool, gameVisible: Bool, surfaceAttached: Bool, engineActive: Bool) -> Bool {
        sceneActive && gameVisible && surfaceAttached && engineActive
    }

    /// One keyboard authority: GCKeyboard when connected, else UIKit presses.
    static func keyboardSource(gameControllerKeyboard: Bool) -> EnginePadInputSource {
        gameControllerKeyboard ? .keyboardGC : .keyboardUIKit
    }

    /// One pointer authority: raw GCMouse only while the system actually holds
    /// the pointer lock; otherwise UIKit indirect pointer events.
    static func pointerSource(pointerLocked: Bool, gameControllerMouse: Bool) -> EnginePadInputSource {
        pointerLocked && gameControllerMouse ? .pointerGC : .pointerUIKit
    }

    /// Ask for the lock only while the game owns input, a GCMouse can deliver
    /// relative motion, and the player has not released the pointer.
    static func prefersPointerLock(gameOwnsInput: Bool, gameControllerMouse: Bool, userReleased: Bool) -> Bool {
        gameOwnsInput && gameControllerMouse && !userReleased
    }

    /// Command-L releases or recaptures the pointer (L is HID usage 0x0F).
    static let captureToggleUsage = 0x0F
    static func isCaptureToggle(usage: Int, command: Bool) -> Bool { command && usage == captureToggleUsage }

    /// Launcher text shown when no controller is connected: what was detected
    /// and what touch can and cannot do.
    static func launcherSummary(keyboard: Bool, pointer: Bool) -> String {
        let touch = "Touch: drag to aim, tap to fire, two-finger tap to throw a grenade."
        switch (keyboard, pointer) {
        case (true, true):
            return "Keyboard and mouse or trackpad detected. Esc or ⌘. pauses; ⌘L releases the pointer."
        case (true, false):
            return "Keyboard detected: use it to move and play. \(touch)"
        case (false, true):
            return "Mouse or trackpad detected. Full touch controls are available for movement and actions while no keyboard or controller is connected."
        case (false, false):
            return "Touch controls available: left stick to move, drag right to aim, and hold Fire. Jump, use, reload, grenades and other actions are on screen."
        }
    }
}
