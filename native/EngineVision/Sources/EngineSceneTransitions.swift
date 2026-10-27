import Foundation

enum EnginePresentationDestination: Equatable { case library, window, immersive }

/// One immersive scene belongs to one explicit expansion of the game window.
/// Scene disappearance and renderer failure can report the same close; stale
/// callbacks must never return a replacement scene to the window.
struct EngineSceneTransitions {
    private(set) var scene: UInt64?
    private var dismissing: UInt64?
    private var closeClaimed = false

    mutating func beginScene(_ generation: UInt64) {
        scene = generation
        dismissing = nil
        closeClaimed = false
    }

    func ownsScene(_ generation: UInt64) -> Bool {
        scene == generation && !closeClaimed && dismissing != generation
    }

    mutating func claimDisappearance(_ generation: UInt64) -> Bool {
        guard ownsScene(generation) else { return false }
        closeClaimed = true
        return true
    }

    mutating func beginDismissal(_ generation: UInt64) {
        guard scene == generation else { return }
        dismissing = generation
    }

    mutating func finishScene(_ generation: UInt64) {
        guard scene == generation else { return }
        scene = nil
        dismissing = nil
        closeClaimed = false
    }

    static func returnDestination(engineActive: Bool, quitting: Bool) -> EnginePresentationDestination {
        engineActive && !quitting ? .window : .library
    }

    static func canHideWindow(sceneOwned: Bool, immersiveActive: Bool,
                              completedFrames: UInt64, returnPending: Bool, quitting: Bool) -> Bool {
        sceneOwned && immersiveActive && completedFrames > 0 && !returnPending && !quitting
    }
}
