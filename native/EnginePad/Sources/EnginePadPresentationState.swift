import Foundation

/// Desired versus actual full-screen game presentation. Present and dismiss
/// never overlap: while one is animating, later requests only update the
/// wish, and the latest wish is reconciled when the transition completes (or
/// when presenting was refused). Pure, so it is tested on a Mac.
struct EnginePadPresentationState {
    enum Action: Equatable { case none, present, dismiss }
    private(set) var wanted = false
    /// A game controller is presented, or being presented.
    private(set) var shown = false
    private(set) var transitioning = false

    /// The SwiftUI binding changed. `canPresent`: the anchor is in a window.
    mutating func desire(_ value: Bool, canPresent: Bool) -> Action { wanted = value; return next(canPresent) }
    /// The anchor appeared or another chance to act arrived.
    mutating func poke(canPresent: Bool) -> Action { next(canPresent) }
    /// The in-flight present or dismiss completed.
    mutating func finished(canPresent: Bool) -> Action { transitioning = false; return next(canPresent) }
    /// UIKit refused the presentation: nothing is shown; retry on the next
    /// desire/poke rather than immediately.
    mutating func presentRefused() { transitioning = false; shown = false }

    private mutating func next(_ canPresent: Bool) -> Action {
        guard !transitioning else { return .none }
        if wanted, !shown, canPresent { shown = true; transitioning = true; return .present }
        if !wanted, shown { shown = false; transitioning = true; return .dismiss }
        return .none
    }
}
