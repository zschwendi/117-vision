import Foundation

@main
struct ControllerNavigationValidation {
    static func main() {
        var checks = 0
        func check(_ value: @autoclosure () -> Bool, _ message: String) {
            checks += 1
            precondition(value(), message)
        }
        typealias Input = EngineControllerNavigationInput
        var input = Input(), t = 1.0
        var sample = Input.Sample(source: 1, activate: true)
        check(input.update(sample, now: t) == nil, "held Cross on entry")
        t += 0.02; check(input.update(sample, now: t) == nil, "held Cross stays blocked")
        sample.activate = false; t += 0.02; check(input.update(sample, now: t) == nil, "neutral arms")
        sample.activate = true; t += 0.02; check(input.update(sample, now: t) == .activate, "fresh Cross selects")
        for _ in 0..<30 { t += 0.02; check(input.update(sample, now: t) == nil, "held Cross never repeats") }
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.y = -1; t += 0.02; check(input.update(sample, now: t) == .move(1), "down moves selection")
        check(input.update(sample, now: t) == nil, "duplicate timestamp does not repeat")
        for _ in 0..<20 { t += 0.02; check(input.update(sample, now: t) == nil, "repeat dwell") }
        t += 0.03; check(input.update(sample, now: t) == .move(1), "bounded held repeat")
        sample.y = 1; t += 0.02; check(input.update(sample, now: t) == .move(-1), "reverse moves immediately")
        sample.y = 0; t += 0.02; _ = input.update(sample, now: t)
        sample.x = -1; t += 0.02; check(input.update(sample, now: t) == .adjust(-1), "left adjusts")
        sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "held stick cannot activate")
        sample.x = 0; sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.back = true; t += 0.02; check(input.update(sample, now: t) == .back, "Circle returns")
        input.reset(); sample = .init(source: 1); t += 0.02; _ = input.update(sample, now: t)
        sample.activate = true; sample.back = true; t += 0.02; check(input.update(sample, now: t) == nil, "opposed action buttons ignored")
        sample = .init(source: 1); t += 0.02; _ = input.update(sample, now: t)
        sample.otherHeld = true; t += 0.02; _ = input.update(sample, now: t)
        sample.otherHeld = false; sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "trigger or blocker requires full release")
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.source = 2; sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "reconnect held action")
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.y = 1; t += 0.02; check(input.update(sample, now: t) == .move(-1), "new owner neutral rearms")
        t += 2; check(input.update(sample, now: t) == nil, "long pause never catches up")
        t += 0.02; check(input.update(sample, now: t) == nil, "held after time gap stays disarmed")
        sample.y = 0; t += 0.02; _ = input.update(sample, now: t)
        sample.spatial = false; sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "switching to ordinary pad with A held never activates")
        sample.activate = false; t += 0.02; check(input.update(sample, now: t) == nil, "ordinary pad neutral arms")
        sample.activate = true; t += 0.02; check(input.update(sample, now: t) == .activate, "ordinary pad A selects after release")
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.back = true; t += 0.02; check(input.update(sample, now: t) == .back, "ordinary pad B backs out")
        sample.back = false; t += 0.02; check(input.update(sample, now: t) == nil, "release after B rearms")
        sample.y = -1; t += 0.02; check(input.update(sample, now: t) == .move(1), "ordinary stick or D-pad down moves")
        sample.y = 0; sample.x = 1; t += 0.02; check(input.update(sample, now: t) == .adjust(1), "ordinary right adjusts")
        sample.x = 0; sample.otherHeld = true; t += 0.02; _ = input.update(sample, now: t)
        sample.otherHeld = false; sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "ordinary trigger or Menu disarms until release")
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.spatial = true; sample.activate = true; t += 0.02; check(input.update(sample, now: t) == nil, "Sense return held action blocked")
        sample = .init(source: 2); t += 0.02; _ = input.update(sample, now: t)
        sample.x = .nan; t += 0.02; check(input.update(sample, now: t) == nil, "NaN axis rejected")
        sample = .init(source: 2); t += 0.02; _ = input.update(sample, now: t)
        sample.activate = true; check(input.update(sample, now: .infinity) == nil, "invalid clock disarms")
        t += 0.02; check(input.update(sample, now: t) == nil, "no action after invalid clock")
        sample.activate = false; t += 0.02; _ = input.update(sample, now: t)
        sample.activate = true; check(input.update(sample, now: t - 0.1) == nil, "clock rollback disarms")

        var focus = EngineControllerFocus()
        var items: [EngineControllerFocus.Item] = [.init(id: "play", enabled: false), .init(id: "mode", enabled: true), .init(id: "settings", enabled: true)]
        focus.reconcile(items); check(focus.selected == "mode", "disabled initial item skipped")
        focus.move(1, in: items); check(focus.selected == "settings", "next enabled action")
        focus.move(1, in: items); check(focus.selected == "mode", "wrap excludes disabled action")
        focus.move(-1, in: items); check(focus.selected == "settings", "reverse wraps")
        items[0].enabled = true; focus.reconcile(items); check(focus.selected == "settings", "new enabled action keeps stable identity")
        items.removeLast(); focus.reconcile(items); check(focus.selected == "play", "removed selection repaired")
        focus.reconcile([]); focus.move(1, in: []); check(focus.selected == nil, "empty page safe")

        var chord = EngineControllerEntryChord(), ct = 20.0
        func step(_ create: Bool, _ options: Bool, neutral: Bool = true, source: UInt64 = 1, spatial: Bool = true) -> Bool {
            ct += 0.02
            return chord.update(source: source, spatial: spatial, create: create, options: options, otherwiseNeutral: neutral, now: ct)
        }
        check(!step(true, true), "held chord on connect rejected")
        check(!step(false, false), "neutral arms chord")
        check(!step(true, false), "Create alone preserved")
        check(step(true, true), "fresh chord opens app controls")
        for _ in 0..<20 { check(!step(true, true), "held chord one action") }
        check(!step(false, false), "release chord")
        check(!step(false, true), "Options alone preserved")
        check(step(true, true), "chord works in either press order")
        check(!step(false, false), "release again")
        check(!step(true, true, neutral: false), "shooting/moving chord rejected")
        check(!step(true, true), "blocking input release does not retrigger held chord")
        check(!step(false, false), "full neutral rearm")
        check(!step(true, true, source: 2), "new source held chord rejected")
        check(!step(false, false, source: 2), "new source neutral")
        check(!step(true, true, source: 2, spatial: false), "ordinary pad chord unchanged")
        check(!step(true, true, source: 2), "returning Sense requires release")
        check(!step(false, false, source: 2), "release")
        ct += 1
        check(!step(true, true, source: 2), "resume after gap does not open")

        // Selection outline appears only while a controller is navigating.
        var highlight = EngineControllerHighlight(), ht = 100.0
        check(!highlight.visible, "touch launch has no outline")
        check(!highlight.accept(.move(1), now: ht), "first stick input only reveals")
        check(highlight.visible, "controller input shows outline")
        ht += 0.5; check(highlight.accept(.move(1), now: ht), "visible outline moves")
        ht += 0.5; check(highlight.accept(.activate, now: ht), "visible selection activates")
        check(!highlight.expire(now: ht + EngineControllerHighlight.idleTimeout - 0.1), "active navigation keeps outline")
        ht += EngineControllerHighlight.idleTimeout + 0.1
        check(highlight.expire(now: ht), "idle outline hides")
        check(!highlight.visible && !highlight.expire(now: ht + 1), "expiry reports once")
        check(!highlight.accept(.activate, now: ht + 1), "Cross on hidden selection never activates")
        highlight.reset(); check(highlight.accept(.back, now: ht + 2), "Circle backs out without a visible selection")
        highlight.reset(); highlight.reveal(now: ht + 3)
        check(highlight.accept(.activate, now: ht + 3.1), "chord-opened surface is already controller-focused")
        check(highlight.expire(now: ht), "clock rollback hides outline")
        highlight.reset(); _ = highlight.accept(.move(1), now: ht + 4); check(highlight.accept(.move(1), now: ht + 4.1), "navigating")
        highlight.pointerInteraction(); check(!highlight.visible, "pinch or touch hides the outline at once")
        check(!highlight.accept(.activate, now: ht + 4.2), "Cross after a pinch reveals instead of acting")
        check(highlight.accept(.activate, now: ht + 4.3), "then acts on the visible selection")

        // Controller cue reflects the actual reader, including partial Sense pairs.
        let none = EngineControllerPresence(connected: false, spatial: false, diagnostic: "No controller connected")
        check(!none.connected && none.title == "No controller detected" && none.summary.contains("needs a game controller"), "missing controller cue")
        let partial = EngineControllerPresence(connected: false, spatial: true, diagnostic: "Sense needs both left and right controllers")
        check(!partial.connected && partial.summary == "Sense needs both left and right controllers", "partial Sense pair is not ready")
        let pad = EngineControllerPresence(connected: true, spatial: false, diagnostic: "Gamepad ready (DualSense Wireless Controller)")
        check(pad.kind == .gamepad && pad.summary.contains("DualSense"), "ordinary gamepad identified")
        check(EngineControllerPresence(connected: true, spatial: true, diagnostic: "PS VR2 Sense pair ready").kind == .sense, "Sense pair identified")
        check(EngineControllerPresence(connected: true, spatial: false, diagnostic: " ").detail == "Ready", "empty diagnostic never blank")

        // Settings affordance derives from real engine menu state.
        typealias Pause = EnginePauseAffordance
        var pause = Pause(), pt = 200.0
        var state = Pause.Inputs(surface: .immersive, engineRunning: true, hasFrame: false, menuActive: true, applicationActive: true)
        check(!pause.update(state, now: pt), "loading screen never shows affordance")
        state.hasFrame = true
        check(!pause.update(state, now: pt), "menu must persist before affordance appears")
        pt += Pause.showDelay; check(pause.update(state, now: pt), "genuine in-game pause offers affordance")
        state.menuActive = false; pt += 0.1
        check(pause.update(state, now: pt), "brief widget swap does not flicker")
        state.menuActive = true; pt += 0.1; check(pause.update(state, now: pt), "menu returns without delay while still shown")
        state.menuActive = false; pt += 0.1; _ = pause.update(state, now: pt)
        pt += Pause.hideDelay; check(!pause.update(state, now: pt), "gameplay hides affordance")
        state.menuActive = true; pt += 0.1; _ = pause.update(state, now: pt)
        pt += Pause.showDelay; check(pause.update(state, now: pt), "next pause shows again")
        state.appMenuOpen = true; pt += 0.1; check(!pause.update(state, now: pt), "settings menu replaces affordance")
        state.appMenuOpen = false; pt += 0.1; check(pause.update(state, now: pt), "closing settings during pause restores affordance")
        pause.userDismissed(); pt += 0.1; check(!pause.update(state, now: pt), "user-closed affordance stays closed")
        pt += 5; check(!pause.update(state, now: pt), "no reopen during same menu visit")
        state.menuActive = false; pt += Pause.hideDelay + 0.1; _ = pause.update(state, now: pt)
        _ = pause.update(state, now: pt + Pause.hideDelay)
        state.menuActive = true; pt += 1; _ = pause.update(state, now: pt)
        pt += Pause.showDelay; check(pause.update(state, now: pt), "next menu visit offers it again")
        state.transitionBusy = true; pt += 0.1; check(!pause.update(state, now: pt), "presentation transition hides affordance")
        state.transitionBusy = false; pt += 0.1; check(pause.update(state, now: pt), "affordance returns after transition")
        state.quitBlocked = true; pt += 0.1; check(!pause.update(state, now: pt), "quit hides affordance")
        state.quitBlocked = false; state.surface = .none; pt += 0.1; check(!pause.update(state, now: pt), "library has no in-game affordance")
        state.surface = .window; pt += 0.1; check(!pause.update(state, now: pt), "new surface restarts dwell")
        pt += Pause.showDelay; check(pause.update(state, now: pt), "window game offers ornament")
        state.applicationActive = false; pt += 0.1; check(!pause.update(state, now: pt), "inactive or background app opens no auxiliary scene")
        pt += 5; check(!pause.update(state, now: pt), "stays hidden while inactive even though Halo is paused")
        state.applicationActive = true; pt += 0.1; check(!pause.update(state, now: pt), "foreground restarts the dwell")
        pt += Pause.showDelay + 0.01; check(pause.update(state, now: pt), "foreground pause offers it again")
        state.engineRunning = false; pt += 0.1; check(!pause.update(state, now: pt), "stopped engine never offers affordance")
        check(!pause.update(state, now: .nan), "invalid clock safe")

        // App launch: loading, then Halo's front-end title shell. The host's
        // pause-only query reports 0 for the shell, so nothing appears until a
        // genuine in-game menu opens after gameplay begins.
        var launch = Pause(), lt = 400.0
        var boot = Pause.Inputs(surface: .immersive, engineRunning: true, hasFrame: false, menuActive: false, applicationActive: true)
        for _ in 0..<10 { lt += 0.2; check(!launch.update(boot, now: lt), "starting engine shows nothing") }
        boot.hasFrame = true
        for _ in 0..<50 { lt += 0.2; check(!launch.update(boot, now: lt), "title menus at startup never show the affordance") }
        boot.menuActive = true; lt += 0.2; _ = launch.update(boot, now: lt)
        lt += Pause.showDelay + 0.01; check(launch.update(boot, now: lt), "first in-game pause shows it")
        boot.menuActive = false; lt += 0.2; check(launch.update(boot, now: lt), "brief resume keeps it until the hide delay")
        lt += Pause.hideDelay + 0.01; check(!launch.update(boot, now: lt), "resume hides it")
        boot.menuActive = true; lt += 0.2; _ = launch.update(boot, now: lt)
        lt += Pause.showDelay + 0.01; check(launch.update(boot, now: lt), "pause re-entry shows it again")

        // The app scene graph: only the launcher opens at launch. Every other
        // window is suppressed and restoration-free, play never opens the game
        // menu or settings, and the affordance never opens a menu by itself.
        let sources = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("Sources")
        if let app = try? String(contentsOf: sources.appendingPathComponent("HaloEngineVisionApp.swift"), encoding: .utf8) {
            let body = app.components(separatedBy: "var body: some Scene {").dropFirst().first?
                .components(separatedBy: "\nenum EngineDisplayMode").first ?? ""
            let scenes = body.components(separatedBy: "\n        Window(").dropFirst()
            check(scenes.count == 3, "game menu, settings and Settings-button windows exist")
            for scene in scenes {
                let name = scene.prefix(while: { $0 != "," })
                check(scene.contains(".defaultLaunchBehavior(.suppressed)"), "\(name) never opens at launch")
                check(scene.contains(".restorationBehavior(.disabled)"), "\(name) is never restored at launch")
            }
            check(body.components(separatedBy: "ImmersiveSpace(").dropFirst().first?.contains(".defaultLaunchBehavior(.suppressed)") == true,
                  "immersive space never opens at launch")
            let present = app.components(separatedBy: "private func present(").dropFirst().first?
                .components(separatedBy: "private func refresh()").first ?? ""
            check(!present.isEmpty && !present.contains("openWindow(Self.controlsWindowID)") && !present.contains("openWindow(Self.settingsWindowID)"),
                  "entering play opens no settings or game menu")
            check(app.components(separatedBy: "openWindow(Self.controlsWindowID)").count == 2, "game menu opens only through showControls")
            check(app.contains("enginevision_pause_menu_active()") && !app.contains("enginevision_menu_active()"),
                  "affordance uses the pause-only query, not the front-end-inclusive one")
            // Cloud monitoring: started only on foreground while the engine has
            // never run, retired immediately before the engine starts, and
            // remote changes reconcile only while the app is active.
            let activate = app.components(separatedBy: "func setApplicationActive(").dropFirst().first?
                .components(separatedBy: "\n    }").first ?? ""
            check(activate.contains("if active && enginevision_runtime_state() == ENGINEVISION_IDLE")
                  && activate.contains("cloudSaves.startCloudMonitoring()"), "monitoring starts only when active and idle")
            check(app.components(separatedBy: "startCloudMonitoring()").count == 2, "monitoring starts in one place")
            let start = app.components(separatedBy: "self.engine.start(root:").first ?? ""
            let lastLine = start.components(separatedBy: "\n").filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty
                && !$0.trimmingCharacters(in: .whitespaces).hasPrefix("//") }.last ?? ""
            check(lastLine.contains("self.cloudSaves.stopCloudMonitoring()"), "watcher retired immediately before engine start")
            check(app.components(separatedBy: "stopCloudMonitoring()").count == 2, "watcher never retired on background")
            let changes = app.components(separatedBy: "cloudSaves.cloudChanges.sink").dropFirst().first?
                .components(separatedBy: "}.store").first ?? ""
            check(changes.contains("self.applicationActive") && changes.contains("syncSavesIfIdle()"), "cloud changes reconcile only while active")
            let idleSync = app.components(separatedBy: "func syncSavesIfIdle()").dropFirst().first?
                .components(separatedBy: "\n    }\n").first ?? ""
            check(idleSync.contains("applicationActive") && idleSync.contains("assets.phase == .ready")
                  && idleSync.contains("ENGINEVISION_IDLE"), "idle sync keeps active, ready and native-idle guards")
        } else {
            check(false, "HaloEngineVisionApp.swift is readable next to the validation")
        }

        // Quit Game is terminal and strictly ordered.
        var quit = EngineQuitFlow(), qt = 300.0
        check(!quit.begin(engineActive: false, presentationBusy: false, preparingPlay: false, saveBusy: false), "no quit without live engine")
        check(!quit.begin(engineActive: true, presentationBusy: true, preparingPlay: false, saveBusy: false), "no quit during presentation transition")
        check(!quit.begin(engineActive: true, presentationBusy: false, preparingPlay: true, saveBusy: false), "no quit while preparing play")
        check(!quit.begin(engineActive: true, presentationBusy: false, preparingPlay: false, saveBusy: true), "no quit during save work")
        check(quit.phase == .idle && !quit.blocksPresentation, "refused quit changes nothing")
        check(quit.begin(engineActive: true, presentationBusy: false, preparingPlay: false, saveBusy: false), "quit begins")
        check(quit.blocksPresentation && quit.inProgress, "quit blocks play and presentation")
        check(!quit.begin(engineActive: true, presentationBusy: false, preparingPlay: false, saveBusy: false), "quit is not reentrant")
        quit.observe(engineActive: false, now: qt); check(quit.phase == .closingPresentation, "stop is not assumed before presentation closes")
        check(quit.presentationClosed(now: qt), "stop requested after presentation closes")
        check(!quit.presentationClosed(now: qt), "stop requested once")
        quit.observe(engineActive: true, now: qt + 1); check(quit.phase == .stopping, "waits for engine")
        quit.observe(engineActive: true, now: qt + EngineQuitFlow.stopTimeout); check(quit.phase == .stillStopping, "slow stop reported honestly")
        check(quit.message(appName: "117")?.contains("still closing") == true, "slow stop does not claim completion")
        quit.observe(engineActive: false, now: qt + 20); check(quit.phase == .stopped, "late stop still completes")
        check(quit.blocksPresentation && !quit.inProgress, "finished quit never restarts in process")
        let done = quit.message(appName: "117") ?? ""
        check(done.contains("Close and reopen 117") && done.contains("Saved games"), "quit result explains relaunch with the product name")
        print("PASS: \(checks) Sense app navigation, identity, repeat, release, entry-chord, highlight, controller cue, pause affordance, launch and quit checks")
    }
}
