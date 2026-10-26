import Foundation

@MainActor
final class EngineRuntimeModel: ObservableObject {
    @Published var state = Int32(ENGINEVISION_IDLE)
    @Published var status = "Engine has not started."
    @Published var frameSequence: UInt64 = 0
    @Published var frameSize = "No engine frame"
    @Published var renderResolution = "Waiting for the engine"
    /// What this launch asked of Halo's 30 fps limiter and whether the hook
    /// has acted on it yet, in words; never a measured frame rate.
    @Published var frameLimit = "Starts with Halo"
    @Published var controllerStatus = "Checking controller"
    @Published var diagnosticsStatus = "Preparing test report"
    let diagnostics = EngineDiagnostics()
    private var timer: Timer?

    init() {
        refresh()
        timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.refresh() }
        }
    }
    deinit { timer?.invalidate() }

    var canStart: Bool { state == ENGINEVISION_IDLE }
    var isActive: Bool { state == ENGINEVISION_STARTING || state == ENGINEVISION_RUNNING }

    func start(root: URL) {
        guard canStart else { return }
        let result = root.path.withCString { enginevision_start($0) }
        refresh()
        if result != 0 && state != ENGINEVISION_FAILED { status = "Engine start failed with POSIX status \(result)." }
    }

    func stop() { if isActive { enginevision_request_stop(); refresh() } }

    func refresh() {
        state = Int32(enginevision_runtime_state())
        var bytes = [CChar](repeating: 0, count: 512)
        enginevision_copy_status(&bytes, bytes.count)
        status = String(cString: bytes)
        var frame = EngineVisionFrameInfo()
        if enginevision_frame_info(&frame) {
            frameSequence = frame.sequence
            frameSize = "\(frame.width) × \(frame.height) · \(ByteCountFormatter.string(fromByteCount: Int64(frame.byte_count), countStyle: .memory))"
        }
        // Flat-frame bytes can be older than the current zero-copy panorama.
        // Keep their copy contract intact and report the actual world target separately.
        var panorama = EngineVisionPanoramaInfo()
        _ = enginevision_panorama_info(&panorama)
        if panorama.status != 0, panorama.width > 0, panorama.height > 0 {
            renderResolution = "\(panorama.width) × \(panorama.height) per view"
            frameSize = renderResolution
        } else if frame.width > 0, frame.height > 0 {
            renderResolution = "\(frame.width) × \(frame.height)"
        }
        // The request, until the hook's own counts show it took effect.
        var limit = EngineVisionFrameLimitInfo()
        if state == ENGINEVISION_IDLE || !enginevision_frame_limit_info(&limit) { frameLimit = "Starts with Halo" }
        else if limit.lifts_throttle == 0 { frameLimit = "Halo’s limiter unchanged" }
        else if limit.lifted > 0 { frameLimit = "Maximum · 30 fps limit lifted" }
        else if limit.unthrottled > 0 { frameLimit = "Maximum · Halo’s profile has no 30 fps limit" }
        else if limit.declined > 0 { frameLimit = "Maximum requested · couldn’t lift Halo’s limit" }
        else { frameLimit = "Maximum requested" }
        diagnostics.sample(state: state, status: status, frame: frame)
        controllerStatus = diagnostics.controllerStatus
        diagnosticsStatus = diagnostics.saveStatus
    }
}
