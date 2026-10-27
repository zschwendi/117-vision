import SwiftUI
import UIKit

struct EnginePadTouchOverlay: UIViewRepresentable {
    let input: EnginePadInputRouter
    func makeUIView(context: Context) -> EnginePadTouchOverlayView { EnginePadTouchOverlayView(input: input) }
    func updateUIView(_ view: EnginePadTouchOverlayView, context: Context) { view.setNeedsDisplay() }
    static func dismantleUIView(_ view: EnginePadTouchOverlayView, coordinator: ()) { view.cancelTouches() }
}

/// One multi-touch surface assigns stable roles at touch-down. UIKit delivers
/// simultaneous fingers independently; no tap recognizer delays a held shot.
final class EnginePadTouchOverlayView: UIView {
    private let input: EnginePadInputRouter
    private var touchIDs: [ObjectIdentifier: Int] = [:]
    private var nextID = 0
    private var pointerIDs = Set<Int>()
    private var laidOutSize = CGSize.zero
    private var laidOutInsets = UIEdgeInsets.zero
    private var stickCenter = CGPoint.zero, stickRadius: CGFloat = 56
    private var actionFrames: [EnginePadTouchControls.Action: CGRect] = [:]

    init(input: EnginePadInputRouter) {
        self.input = input
        super.init(frame: .zero)
        backgroundColor = .clear; isOpaque = false
        isMultipleTouchEnabled = true
        accessibilityLabel = "Touch game controls"
    }
    required init?(coder: NSCoder) { nil }
    override var canBecomeFirstResponder: Bool { false }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window == nil { cancelTouches() }
    }
    func cancelTouches() {
        touchIDs.removeAll()
        for id in pointerIDs { input.pointerCancelled(id) }
        pointerIDs.removeAll()
        input.cancelTouchControls()
        setNeedsDisplay()
    }
    override func safeAreaInsetsDidChange() { super.safeAreaInsetsDidChange(); setNeedsLayout() }
    override func layoutSubviews() {
        super.layoutSubviews()
        if laidOutSize != bounds.size || laidOutInsets != safeAreaInsets {
            cancelTouches() // rotation, resize or safe-area relocation requires fresh fingers
            laidOutSize = bounds.size
            laidOutInsets = safeAreaInsets
        }
        let layout = EnginePadTouchLayout(size: bounds.size, top: safeAreaInsets.top,
            left: safeAreaInsets.left, bottom: safeAreaInsets.bottom, right: safeAreaInsets.right)
        stickRadius = layout.stickRadius; stickCenter = layout.stickCenter
        actionFrames = layout.buttons
        setNeedsDisplay()
    }

    /// Leave actual mouse/trackpad events on the original input surface.
    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard input.touchControlsEnabled, event?.type != .hover else { return nil }
        // Do not swallow the first title/pause-menu tap during the UI poll gap.
        if enginevision_menu_active() != 0 { input.cancelTouchControls(); return nil }
        if let touches = event?.allTouches, !touches.isEmpty,
           touches.allSatisfy({ $0.type != .direct }) { return nil }
        return super.hitTest(point, with: event)
    }

    private func target(at point: CGPoint) -> EnginePadTouchControls.Target? {
        for action in EnginePadTouchControls.Action.allCases {
            if actionFrames[action]?.contains(point) == true { return .action(action) }
        }
        let stick = CGRect(x: stickCenter.x - stickRadius - 16, y: stickCenter.y - stickRadius - 16,
                           width: 2 * (stickRadius + 16), height: 2 * (stickRadius + 16))
        if stick.contains(point) { return .movement(center: stickCenter, radius: stickRadius) }
        if point.x > bounds.width * 0.38 { return .look }
        return nil
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            if touch.type == .indirectPointer {
                pointerIDs.insert(ObjectIdentifier(touch).hashValue)
                input.pointerBegan(ObjectIdentifier(touch).hashValue, at: touch.location(in: self),
                                   buttons: (event?.buttonMask.rawValue ?? 0) & 255)
                continue
            }
            guard touch.type == .direct else { continue }
            let point = touch.location(in: self)
            guard let target = target(at: point) else { continue }
            nextID &+= 1
            touchIDs[ObjectIdentifier(touch)] = nextID
            input.touchControlBegan(nextID, target: target, at: point)
        }
        setNeedsDisplay()
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        for touch in touches {
            if touch.type == .indirectPointer {
                input.pointerMoved(ObjectIdentifier(touch).hashValue, to: touch.location(in: self),
                                   buttons: (event?.buttonMask.rawValue ?? 0) & 255)
                continue
            }
            guard let id = touchIDs[ObjectIdentifier(touch)] else { continue }
            input.touchControlMoved(id, to: touch.location(in: self))
        }
        setNeedsDisplay()
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { end(touches) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        // A mixed direct + pointer event can hit this view. Forward each
        // indirect stream exactly once through the unchanged pointer router.
        for touch in touches where touch.type == .indirectPointer {
            pointerIDs.remove(ObjectIdentifier(touch).hashValue)
            input.pointerCancelled(ObjectIdentifier(touch).hashValue)
        }
        if touches.contains(where: { $0.type == .direct }) { cancelTouches() }
    }
    private func end(_ touches: Set<UITouch>) {
        for touch in touches {
            if touch.type == .indirectPointer {
                pointerIDs.remove(ObjectIdentifier(touch).hashValue)
                input.pointerEnded(ObjectIdentifier(touch).hashValue, at: touch.location(in: self))
                continue
            }
            guard let id = touchIDs.removeValue(forKey: ObjectIdentifier(touch)) else { continue }
            input.touchControlEnded(id)
        }
        setNeedsDisplay()
    }

    override func draw(_ rect: CGRect) {
        let accent = UIColor(red: 0.38, green: 0.86, blue: 0.94, alpha: 1)
        let background = UIColor(white: 0.03, alpha: 0.43)
        func disk(_ frame: CGRect, active: Bool) {
            (active ? accent.withAlphaComponent(0.28) : background).setFill()
            let path = UIBezierPath(ovalIn: frame); path.fill()
            (active ? accent : UIColor.white.withAlphaComponent(0.38)).setStroke()
            path.lineWidth = active ? 2 : 1; path.stroke()
        }
        let stickFrame = CGRect(x: stickCenter.x - stickRadius, y: stickCenter.y - stickRadius,
                                width: stickRadius * 2, height: stickRadius * 2)
        disk(stickFrame, active: input.touchMovementOffset != .zero)
        let offset = input.touchMovementOffset
        disk(CGRect(x: stickCenter.x + offset.x - 22, y: stickCenter.y + offset.y - 22, width: 44, height: 44), active: true)
        for action in EnginePadTouchControls.Action.allCases {
            guard let frame = actionFrames[action] else { continue }
            let available = input.touchActionAvailable(action)
            let context = UIGraphicsGetCurrentContext()
            context?.saveGState(); context?.setAlpha(available ? 1 : 0.4)
            disk(frame, active: input.touchHeldActions.contains(action))
            let paragraph = NSMutableParagraphStyle(); paragraph.alignment = .center
            let title = action == .grenadeCycle ? "Type" : action == .use ? "Use" : action.title
            let attributes: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: action == .fire ? 15 : 11, weight: .semibold),
                .foregroundColor: UIColor.white.withAlphaComponent(0.95), .paragraphStyle: paragraph]
            let height = (title as NSString).size(withAttributes: attributes).height
            (title as NSString).draw(in: CGRect(x: frame.minX + 2, y: frame.midY - height / 2,
                                               width: frame.width - 4, height: height + 1), withAttributes: attributes)
            if !available {
                let small: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: 8),
                    .foregroundColor: UIColor.white, .paragraphStyle: paragraph]
                ("Unbound" as NSString).draw(in: CGRect(x: frame.minX, y: frame.midY + 9,
                    width: frame.width, height: 11), withAttributes: small)
            }
            context?.restoreGState()
        }
    }
}
