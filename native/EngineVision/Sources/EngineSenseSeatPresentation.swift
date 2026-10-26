import simd

/// Display basis for the original seat controls (sense_mount.h). Sampled
/// once per compositor frame from one locked snapshot; every eye and display
/// drawable of the frame uses the same value.
///
/// A seat transition is a recenter request: added to the user's own recenter
/// generation it reaches EngineImmersiveOrientation, whose `.requested`
/// recenter then resets the physical neutral, virtual yaw/pitch, visor HUD
/// and gestures together. Orientation consumes the token only with a valid
/// tracked pose, so a transition during tracking loss is applied on recovery.
struct EngineSenseSeatPresentation: Equatable {
    var native = false
    var transition: UInt64 = 0

    func orientationRecenter(_ requested: Int) -> Int {
        requested &+ Int(truncatingIfNeeded: transition)
    }
    /// Virtual turning and gestures need tracked on-foot play. The seat
    /// snapshot is published at once, while gameplay_supported only clears at
    /// the next outer render, so this frame's snapshot gates them directly:
    /// native seat sticks and the virtual turn never run together.
    func allowsVirtualTurn(_ trackedTurn: Bool) -> Bool {
        trackedTurn && !native
    }
    /// The visor HUD follows the head only for tracked on-foot play. While
    /// seated the original HUD and crosshair stay on the seat camera's front.
    func hudFollowsHead(trackedGameplay: Bool, menuActive: Bool, senseActive: Bool) -> Bool {
        trackedGameplay && !menuActive && senseActive && !native
    }
}
