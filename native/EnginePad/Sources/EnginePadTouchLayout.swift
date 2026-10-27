import CoreGraphics

/// Geometry shared by drawing, hit-testing and small-window fixtures.
struct EnginePadTouchLayout {
    let stickCenter: CGPoint
    let stickRadius: CGFloat
    let buttons: [EnginePadTouchControls.Action: CGRect]
    var stickHitFrame: CGRect {
        CGRect(x: stickCenter.x - stickRadius - 16, y: stickCenter.y - stickRadius - 16,
               width: 2 * (stickRadius + 16), height: 2 * (stickRadius + 16))
    }
    init(size: CGSize, top: CGFloat = 0, left: CGFloat = 0, bottom: CGFloat = 0, right: CGFloat = 0) {
        let bottom = max(18, bottom + 12), right = max(18, right + 12), left = max(18, left + 12)
        stickRadius = size.width < 500 ? 52 : 60
        stickCenter = CGPoint(x: left + stickRadius + 10, y: size.height - bottom - stickRadius - 12)
        var frames: [EnginePadTouchControls.Action: CGRect] = [:]
        func put(_ action: EnginePadTouchControls.Action, x: CGFloat, y: CGFloat, size: CGFloat) {
            frames[action] = CGRect(x: x - size / 2, y: y - size / 2, width: size, height: size)
        }
        let x = size.width - right, y = size.height - bottom
        put(.fire, x: x - 38, y: y - 118, size: 72)
        put(.jump, x: x - 35, y: y - 40, size: 56)
        put(.grenade, x: x - 104, y: y - 40, size: 56)
        put(.use, x: x - 114, y: y - 112, size: 60)
        let secondary: [EnginePadTouchControls.Action] = [.zoom, .reload, .swap, .crouch, .grenadeCycle, .flashlight]
        if size.height < 370 && size.width >= 480 {
            // Short phone landscape: secondary actions occupy the upper left,
            // away from both thumbs and the existing app toolbar at upper right.
            for (index, action) in secondary.enumerated() {
                put(action, x: left + 24 + CGFloat(index % 3) * 57,
                    y: top + 30 + CGFloat(index / 3) * 54, size: 48)
            }
            put(.melee, x: left + 216, y: y - 40, size: 50)
        } else {
            for (index, action) in secondary.enumerated() {
                put(action, x: x - 26 - CGFloat(index % 3) * 57,
                    y: y - 190 - CGFloat(index / 3) * 54, size: 48)
            }
            // Melee above the left stick prevents overlap at 320pt widths.
            put(.melee, x: stickCenter.x, y: y - 180, size: 50)
        }
        buttons = frames
    }
}
