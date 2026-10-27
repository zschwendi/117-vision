// Template for tools/test_scene_transitions.py. Production callback bodies are
// inserted in AppFixture; both generation policies are compiled unchanged.
import Foundation

struct LayerRenderer {}
@MainActor final class EngineImmersiveTrace {
    static let shared = EngineImmersiveTrace()
    func record(_ event: String) {}
}
@MainActor final class ImmersiveFixture {
    var active = false, opening = false
    var starts = 0, finishes = 0, gpuCompleted: UInt64 = 0
    var frameChecks = 0
    var ownership = EngineImmersiveOwnership()
    var operationGeneration: UInt64 { ownership.generation }
    func prepare() -> UInt64 {
        let token = ownership.beginOpen(active: active, opening: opening)!
        opening = true; return token
    }
    func ownsOperation(_ generation: UInt64) -> Bool { ownership.owns(generation) }
    func start(layer: LayerRenderer) {
        starts += 1; _ = ownership.advance(); active = true; opening = false
    }
    func finish() {
        finishes += 1; _ = ownership.advance(); active = false; opening = false
    }
    struct FrameCounts { let gpuCompleted: UInt64 }
    func frameCounts() -> FrameCounts { frameChecks += 1; return FrameCounts(gpuCompleted: gpuCompleted) }
}
@MainActor final class AppFixture {
    struct Quit { var blocksPresentation = false }
    struct Engine { var isActive = true }
    var quit = Quit(), engine = Engine()
    let immersive = ImmersiveFixture()
    var sceneTransitions = EngineSceneTransitions()
    var pendingCloseGeneration: UInt64?
    var pendingCloseAlreadyDismissed = false
    var pendingDestination: EnginePresentationDestination?
    var drains = 0, refreshes = 0
    func drainPresentationRequests() { drains += 1 }
    func refresh() { refreshes += 1 }
    func begin() -> UInt64 {
        let generation = immersive.prepare()
        sceneTransitions.beginScene(generation)
        return generation
    }
    var fallback: EnginePresentationDestination { returnDestination }
    func wait(_ generation: UInt64) async -> Bool { await waitForImmersiveFrame(generation) }
// __APP_CALLBACKS__
}

@main struct SceneTransitionsValidation {
    @MainActor static func main() async throws {
        var assertions = 0, failures = [String](), cases = [String]()
        func check(_ condition: Bool, _ message: String) {
            assertions += 1
            if !condition { failures.append(message) }
        }

        // The OS can close the space while tracking is awaiting authorization,
        // before CompositorLayer ever calls startImmersive.
        let startup = AppFixture(), opening = startup.begin()
        startup.immersiveSceneDisappeared(generation: opening)
        check(startup.immersive.finishes == 1 && startup.drains == 1, "startup close must perform one cleanup and queue return")
        check(startup.pendingCloseAlreadyDismissed, "system-closed space must not be dismissed a second time")
        check(startup.pendingCloseGeneration == startup.immersive.operationGeneration, "queued close must carry the post-cleanup generation")
        check(startup.fallback == .window, "startup close with live engine returns its game window")
        startup.startImmersive(LayerRenderer(), generation: opening)
        check(startup.immersive.starts == 0, "late renderer must not resurrect system-closed scene")
        startup.immersiveSceneDisappeared(generation: opening)
        check(startup.immersive.finishes == 1 && startup.drains == 1, "duplicate disappearance cannot clean up twice")
        check(!(await startup.wait(opening)), "closed startup cannot hide the surviving game window")
        cases.append("system_close_before_renderer_and_duplicate_callback")

        // Scene and renderer tokens deliberately diverge when the compositor
        // arrives. The original scene still owns its eventual disappearance.
        let running = AppFixture(), scene = running.begin()
        running.startImmersive(LayerRenderer(), generation: scene)
        check(running.immersive.starts == 1 && running.refreshes == 1, "current opening renderer must start exactly once")
        check(running.immersive.operationGeneration != scene, "renderer fixture must exercise real generation advance")
        check(running.sceneTransitions.ownsScene(scene), "renderer handoff must not orphan scene ownership")
        running.startImmersive(LayerRenderer(), generation: scene)
        check(running.immersive.starts == 1, "duplicate compositor callback must not create another renderer")
        running.immersiveSceneDisappeared(generation: scene)
        check(running.immersive.finishes == 1 && running.drains == 1, "scene close after renderer handoff must still return once")
        cases.append("renderer_handoff_keeps_scene_identity")

        let explicit = AppFixture(), explicitScene = explicit.begin()
        explicit.startImmersive(LayerRenderer(), generation: explicitScene)
        explicit.sceneTransitions.beginDismissal(explicitScene)
        explicit.immersiveSceneDisappeared(generation: explicitScene)
        check(explicit.immersive.finishes == 0 && explicit.drains == 0, "expected dismissal callback must leave cleanup to serialized owner")
        check(!(await explicit.wait(explicitScene)), "explicit return must not hide the main window")
        explicit.immersive.finish(); explicit.sceneTransitions.finishScene(explicitScene)
        explicit.immersiveSceneDisappeared(generation: explicitScene)
        check(explicit.immersive.finishes == 1 && explicit.drains == 0, "post-dismiss callback must not duplicate explicit cleanup")
        cases.append("explicit_dismissal_has_one_cleanup_owner")

        // All permutations of late old-scene notifications around a new first
        // frame: stale start, disappearance, dismissal and finish must be inert.
        func permutations(_ values: [Int]) -> [[Int]] {
            if values.isEmpty { return [[]] }
            return values.flatMap { value in permutations(values.filter { $0 != value }).map { [value] + $0 } }
        }
        let orders = permutations([0, 1, 2, 3])
        for order in orders {
            let app = AppFixture(), old = app.begin()
            app.startImmersive(LayerRenderer(), generation: old)
            app.sceneTransitions.beginDismissal(old)
            app.immersive.finish(); app.sceneTransitions.finishScene(old)
            let replacement = app.begin()
            app.startImmersive(LayerRenderer(), generation: replacement)
            app.immersive.gpuCompleted = 1
            for event in order {
                switch event {
                case 0: app.startImmersive(LayerRenderer(), generation: old)
                case 1: app.immersiveSceneDisappeared(generation: old)
                case 2: app.sceneTransitions.beginDismissal(old)
                default: app.sceneTransitions.finishScene(old)
                }
                check(app.sceneTransitions.ownsScene(replacement), "old event \(event) stole replacement in order \(order)")
                check(app.immersive.starts == 2 && app.immersive.finishes == 1 && app.drains == 0,
                      "stale callback changed live replacement in order \(order)")
            }
            check(await app.wait(replacement), "completed replacement must remain presentable after order \(order)")
            app.immersiveSceneDisappeared(generation: replacement)
            app.immersiveSceneDisappeared(generation: old)
            check(app.immersive.finishes == 2 && app.drains == 1, "replacement close must act once after stale callback order \(order)")
        }
        cases.append("24_stale_callback_permutations_preserve_replacement")

        let frame = AppFixture(), frameScene = frame.begin()
        frame.startImmersive(LayerRenderer(), generation: frameScene)
        var returnedBeforeGPU = false
        let firstFrame = Task { @MainActor in
            let ready = await frame.wait(frameScene)
            returnedBeforeGPU = true
            return ready
        }
        try await Task.sleep(for: .milliseconds(80))
        check(frame.immersive.frameChecks > 0 && !returnedBeforeGPU,
              "active renderer must actually poll GPU completion while keeping game window visible")
        frame.immersive.gpuCompleted = 1
        check(await firstFrame.value, "first completed GPU frame must permit window handoff")
        cases.append("waits_for_first_completed_gpu_frame")

        let returnPending = AppFixture(), returnScene = returnPending.begin()
        returnPending.startImmersive(LayerRenderer(), generation: returnScene)
        returnPending.immersive.gpuCompleted = UInt64.max
        returnPending.pendingDestination = .window
        check(!(await returnPending.wait(returnScene)), "queued window return overrides completed GPU frames")
        returnPending.pendingDestination = nil; returnPending.pendingCloseGeneration = returnPending.immersive.operationGeneration
        check(!(await returnPending.wait(returnScene)), "queued renderer close overrides completed GPU frames")
        cases.append("pending_return_and_close_override_gpu_ready")

        let quit = AppFixture(), quitScene = quit.begin()
        quit.startImmersive(LayerRenderer(), generation: quitScene)
        quit.immersive.gpuCompleted = 1; quit.quit.blocksPresentation = true
        check(!(await quit.wait(quitScene)), "quit must not hide main window for an already-ready renderer")
        check(quit.fallback == .library, "terminal quit returns library even while native stop is pending")
        quit.immersiveSceneDisappeared(generation: quitScene)
        check(quit.fallback == .library, "system close while quitting must never resume game window")
        let idle = AppFixture(); idle.engine.isActive = false
        check(idle.fallback == .library, "idle or stopped engine returns library")
        idle.quit.blocksPresentation = true
        check(idle.fallback == .library, "completed quit stays in library")
        cases.append("live_engine_return_vs_terminal_quit")

        let cancelled = AppFixture(), cancelScene = cancelled.begin()
        let waiting = Task { @MainActor in await cancelled.wait(cancelScene) }
        await Task.yield(); waiting.cancel()
        check(!(await waiting.value), "cancelled opening wait must not hide main window")
        let stopped = AppFixture(), stoppedScene = stopped.begin()
        stopped.immersive.opening = false
        check(!(await stopped.wait(stoppedScene)), "failed startup exits frame gate without waiting for timeout")
        cases.append("cancelled_and_failed_startup_leave_window")

        let report: [String: Any] = ["passed": failures.isEmpty, "assertions": assertions,
                                    "cases": cases, "failures": failures,
                                    "validation": "production app callbacks and policies with fixture engine and renderer"]
        let json = try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(json); print("")
        exit(failures.isEmpty ? 0 : 1)
    }
}
