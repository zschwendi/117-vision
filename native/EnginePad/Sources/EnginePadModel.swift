import Foundation
import Combine
import GameController

@MainActor
final class EnginePadModel: ObservableObject {
    @Published private(set) var state = Int32(ENGINEVISION_IDLE)
    @Published private(set) var status = "Ready when you are."
    @Published private(set) var controllerName: String?
    @Published private(set) var frameSize = ""
    /// Original in-game pause/widgets only; the title screen does not qualify.
    @Published private(set) var pauseMenuActive = false
    private var timer: Timer?
    private let haptics = EngineHaptics()
    /// Hardware keyboard, mouse/trackpad and touch (device_input.h). Separate
    /// from the controller lease below; it keeps its own ownership fence.
    let input = EnginePadInputRouter()
    private var inputLease = EnginePadInputLease(begin: { hostgc_app_input_begin() },
                                                 renew: { Int32(hostgc_app_input_renew($0)) },
                                                 end: { hostgc_app_input_end($0) })
    /// Set by the launcher while app UI covers Halo; the poll timer renews it.
    var uiOwnsInput = false {
        didSet { if uiOwnsInput != oldValue { inputLease.update(owned: uiOwnsInput) } }
    }

    init() {
        enginepad_prepare()
        refresh()
        let poll = Timer(timeInterval: 0.2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
        // Keep the native input lease alive while UIKit tracks a settings scroll.
        RunLoop.main.add(poll, forMode: .common)
        timer = poll
    }
    deinit {
        timer?.invalidate()
        inputLease.release()
    }
    var canStart: Bool { state == ENGINEVISION_IDLE }
    var isActive: Bool { state == ENGINEVISION_STARTING || state == ENGINEVISION_RUNNING }
    func start(root: URL) {
        guard canStart else { return }
        let result = root.path.withCString { enginepad_start($0) }
        refresh()
        if result != 0 { status = "Unable to start Halo (\(result)). \(status)" }
    }
    func stop() { enginevision_request_stop(); refresh() }
    func refresh() {
        state = Int32(enginevision_runtime_state())
        var bytes = [CChar](repeating: 0, count: 512)
        enginevision_copy_status(&bytes, bytes.count)
        status = String(cString: bytes)
        controllerName = GCController.controllers().first(where: { $0.extendedGamepad != nil })?.vendorName
        var frame = EngineVisionFrameInfo()
        if enginevision_frame_info(&frame) { frameSize = "\(frame.width) × \(frame.height)" }
        haptics.pump()
        inputLease.update(owned: uiOwnsInput)
        // Only a running engine takes hardware input: nothing typed or tapped
        // over the startup overlay can queue and arrive once Halo is ready.
        input.update(engineActive: state == ENGINEVISION_RUNNING)
        // RUNNING also includes Halo's title and pause menus. Use the original
        // front-end/pause-widget state, including non-pausing multiplayer UI.
        pauseMenuActive = state == ENGINEVISION_RUNNING && enginevision_pause_menu_active() != 0
        input.update(gamePaused: pauseMenuActive)
        input.update(nativeMenuActive: state != ENGINEVISION_RUNNING || enginevision_menu_active() != 0)
        input.refreshTouchBindings()
    }
}
