import SwiftUI
import MetalKit
import GameController

struct EngineFrameView: UIViewRepresentable {
    func makeCoordinator() -> Renderer { Renderer() }
    func makeUIView(context: Context) -> MTKView {
#if os(iOS)
        let view: MTKView = EngineFrameMTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
#else
        let view = MTKView(frame: .zero, device: MTLCreateSystemDefaultDevice())
#endif
        view.backgroundColor = UIColor(red: 0.025, green: 0.045, blue: 0.065, alpha: 1)
        view.isOpaque = true
        view.colorPixelFormat = .bgra8Unorm_srgb
        view.clearColor = MTLClearColor(red: 0.003, green: 0.008, blue: 0.014, alpha: 1)
#if os(iOS)
        // The launch request (or the saved choice before launch); the view
        // lowers it to its screen's maximum once it is in a window.
        view.preferredFramesPerSecond = Int(enginevision_mobile_presenter_fps(0))
#else
        // A requested upper target, not a detected maximum or a promised
        // rate. visionOS has no UIScreen to ask; MTKView picks the rate
        // closest to this that the display supports (MTKView.h).
        view.preferredFramesPerSecond = Self.visionRequestedFramesPerSecond
#endif
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        let controllerEvents = GCEventInteraction()
        controllerEvents.handledEventTypes = .gamepad
        if #available(iOS 26.0, *) { controllerEvents.receivesEventsInView = false }
        view.addInteraction(controllerEvents)
        context.coordinator.configure(view)
        return view
    }
    func updateUIView(_ uiView: MTKView, context: Context) {}
#if !os(iOS)
    /// The highest refresh rate Apple lists for any Apple Vision Pro display
    /// (120 Hz, M5; the M2 model lists up to 100 Hz). A request the system
    /// lowers to what the headset's display can do.
    static let visionRequestedFramesPerSecond = 120
#endif
    static func dismantleUIView(_ uiView: MTKView, coordinator: Renderer) { uiView.delegate = nil }

    final class Renderer: NSObject, MTKViewDelegate {
        private var queue: MTLCommandQueue?
        private var pipeline: MTLRenderPipelineState?
        private var frame: EngineFrameTexture?

        func configure(_ view: MTKView) {
            guard let device = view.device else { return }
            queue = device.makeCommandQueue(); frame = EngineFrameTexture(device: device)
            view.delegate = self
            do {
                let library = try device.makeLibrary(source: Self.shader, options: nil)
                let descriptor = MTLRenderPipelineDescriptor()
                descriptor.vertexFunction = library.makeFunction(name: "window_vertex")
                descriptor.fragmentFunction = library.makeFunction(name: "frame_fragment")
                descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
                pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
            } catch {
                // Keep clearing the surface even if the image pipeline fails.
                NSLog("Master Cheef window shader unavailable: %@", error.localizedDescription)
            }
        }
        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
        func draw(in view: MTKView) {
#if os(iOS)
            (view as? EngineFrameMTKView)?.drawn()
#endif
            _ = frame?.refresh()
            guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
                  let queue, let command = queue.makeCommandBuffer() else { return }
            // Creating the encoder executes the clear, including before Halo
            // supplies its first frame. Presenting an untouched drawable can
            // expose undefined (often magenta) contents during startup.
            pass.colorAttachments[0].loadAction = .clear
            pass.colorAttachments[0].storeAction = .store
            guard let encoder = command.makeRenderCommandEncoder(descriptor: pass) else { return }
            if let pipeline, let texture = frame?.texture {
                encoder.setRenderPipelineState(pipeline); encoder.setFragmentTexture(texture, index: 0)
                var aspect = SIMD2<Float>(Float(texture.width) / Float(texture.height), Float(view.drawableSize.width / max(1, view.drawableSize.height)))
                encoder.setVertexBytes(&aspect, length: MemoryLayout<SIMD2<Float>>.stride, index: 0)
                encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            }
            encoder.endEncoding()
            command.present(drawable); command.commit()
        }

        private static let shader = """
        #include <metal_stdlib>
        using namespace metal;
        struct Raster { float4 position [[position]]; float2 uv; };
        vertex Raster window_vertex(uint id [[vertex_id]], constant float2 &aspect [[buffer(0)]]) {
            const float2 positions[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };
            const float2 uv[3] = { float2(0,1), float2(2,1), float2(0,-1) };
            float2 p = positions[id];
            float source = aspect.x, target = aspect.y;
            if (target > source) p.x *= source / target; else p.y *= target / source;
            return { float4(p, 0, 1), uv[id] };
        }
        fragment float4 frame_fragment(Raster in [[stage_in]], texture2d<float> frame [[texture(0)]]) {
            constexpr sampler sample(filter::linear, address::clamp_to_zero);
            return frame.sample(sample, in.uv);
        }
        """
    }
}

#if os(iOS)
/// The iOS presenter: asks the runtime for its rate whenever it reaches a
/// window and whenever that window's screen, size or traits may have changed,
/// and once a second while drawing, so a move to a screen with a lower
/// maximum lowers both this view's request and the engine's present cadence
/// (enginevision_mobile_presenter_fps). The engine's back buffer never moves.
final class EngineFrameMTKView: MTKView {
    private var framesSinceCheck = 0
    override func didMoveToWindow() { super.didMoveToWindow(); updateFrameRate() }
    override func layoutSubviews() { super.layoutSubviews(); updateFrameRate() }
    /// Called from each draw; re-checks about once a second.
    func drawn() {
        framesSinceCheck += 1
        if framesSinceCheck >= max(preferredFramesPerSecond, 1) { updateFrameRate() }
    }
    private func updateFrameRate() {
        framesSinceCheck = 0
        guard let screen = window?.windowScene?.screen else { return }
        let rate = Int(enginevision_mobile_presenter_fps(Int32(clamping: screen.maximumFramesPerSecond)))
        if rate > 0, preferredFramesPerSecond != rate { preferredFramesPerSecond = rate }
    }
}
#endif
