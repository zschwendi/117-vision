import Foundation

struct EnginePresentationPreferences: Codable, Equatable {
    var version = 1
    var stereo = true
    var separationMillimetres = 63.0
    var verticalFieldDegrees = 105.0
    var hapticStrength = 1.0
    var surroundBrightness = 1.0
    var spatialMenu = true
    var frameRateTarget = 30.0
    var ownWeaponDecibels = 18.0
    var gazePointer = false
    var layerAlign = false
    var framePacing = false

    func validated() -> Self {
        var value = self
        func bounded(_ n: Double, _ range: ClosedRange<Double>, _ fallback: Double) -> Double {
            n.isFinite ? min(range.upperBound, max(range.lowerBound, n)) : fallback
        }
        value.separationMillimetres = bounded(separationMillimetres, 45...80, 63)
        value.verticalFieldDegrees = bounded(verticalFieldDegrees, 90...175, 105)
        value.hapticStrength = bounded(hapticStrength, 0...2, 1)
        value.surroundBrightness = bounded(surroundBrightness, 0...2, 1)
        value.frameRateTarget = bounded(frameRateTarget, 20...30, 30)
        value.ownWeaponDecibels = bounded(ownWeaponDecibels, 0...24, 18)
        return value
    }
}
