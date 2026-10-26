import Foundation
import Metal

let HALO_SENSE_ACTION_SCOPE: Int32 = 2
struct HaloSenseWeaponActionStatus {
    var session: UInt64 = 0
    var generation: UInt64 = 0
    var mode_generation: UInt64 = 0
    var weapon: UInt32 = 0
    var action: Int32 = 0
    var zoom_level: Int32 = 0
}
struct EngineVisionFrameInfo {
    var sequence: UInt64 = 1
    var width: Int32 = 2
    var height: Int32 = 2
    var byte_count: Int = 16
    var scope = HaloSenseWeaponActionStatus()
}
func scoped(_ mode: UInt64) -> HaloSenseWeaponActionStatus {
    HaloSenseWeaponActionStatus(session: 9, generation: 8, mode_generation: mode,
                               weapon: UInt32(mode), action: HALO_SENSE_ACTION_SCOPE, zoom_level: 1)
}
var fixture = EngineVisionFrameInfo(scope: scoped(1))
var copyFixture: EngineVisionFrameInfo?
var fixtureAvailable = true
var copySucceeds = true
var fixturePixel: UInt32 = 0xff102030
func enginevision_frame_info(_ info: inout EngineVisionFrameInfo) -> Bool { info = fixture; return fixtureAvailable }
func enginevision_copy_latest_frame(_ target: UnsafeMutableRawPointer?, _ count: Int, _ info: inout EngineVisionFrameInfo) -> Bool {
    info = copyFixture ?? fixture
    guard copySucceeds, count == 16, let target else { return false }
    target.bindMemory(to: UInt32.self, capacity: 4).initialize(repeating: fixturePixel, count: 4)
    return true
}
let device = MTLCreateSystemDefaultDevice()!
let frame = EngineFrameTexture(device: device)
precondition(frame.refresh())
precondition(frame.scope.mode_generation == 1 && frame.scope.weapon == 1)
let old = frame.texture!
let command = device.makeCommandQueue()!.makeCommandBuffer()!
let output = device.makeBuffer(length: 512, options: .storageModeShared)!
let blit = command.makeBlitCommandEncoder()!
blit.copy(from: old, sourceSlice: 0, sourceLevel: 0, sourceOrigin: MTLOrigin(x:0,y:0,z:0),
          sourceSize: MTLSize(width:2,height:2,depth:1), to: output, destinationOffset:0, destinationBytesPerRow:256, destinationBytesPerImage:512)
blit.endEncoding()
// Refresh between encoding a GPU read and executing it, reproducing the
// exact ownership hazard without relying on thread scheduling or timing.
fixture.sequence = 2; fixture.scope = scoped(2); fixturePixel = 0xffaabbcc
precondition(frame.refresh())
precondition(frame.scope.mode_generation == 2 && frame.sequence == 2)
precondition(old !== frame.texture, "frame refresh mutated an in-flight texture")
command.commit(); command.waitUntilCompleted()
precondition(command.status == .completed)
let pixels = output.contents().bindMemory(to: UInt32.self, capacity: 128)
precondition(pixels[0] == 0xff102030 && pixels[64] == 0xff102030, "GPU read a later frame")
var latest = [UInt32](repeating: 0, count: 4)
frame.texture!.getBytes(&latest, bytesPerRow: 8, from: MTLRegionMake2D(0,0,2,2), mipmapLevel: 0)
precondition(latest.allSatisfy { $0 == 0xffaabbcc })
let same = frame.texture!
precondition(!frame.refresh() && same === frame.texture, "duplicate sequence reallocated")
precondition(frame.scope.mode_generation == 2, "Duplicate reads must preserve coherent scope metadata")
// The producer advances between info and copy. Only copied metadata belongs
// to the uploaded pixels; the hint cannot be used as the scope identity.
fixture.sequence = 3; fixture.scope = scoped(3)
copyFixture = EngineVisionFrameInfo(sequence: 4, scope: scoped(4))
precondition(frame.refresh() && frame.sequence == 4 && frame.scope.mode_generation == 4)
precondition(frame.scope.weapon == 4)
copyFixture = nil; fixture.sequence = 5; fixture.scope = scoped(5)
copySucceeds = false
let failedTexture = frame.texture
precondition(!frame.refresh() && frame.sequence == 4 && failedTexture === frame.texture)
precondition(frame.scope.action == 0, "Failed copy must retire the previous scope")
copySucceeds = true
precondition(frame.refresh() && frame.scope.mode_generation == 5)
// Invalid publication can retire scope without replacing the retained pixels.
fixture.scope = HaloSenseWeaponActionStatus()
precondition(!frame.refresh() && frame.sequence == 5 && frame.scope.action == 0)
fixture.sequence = 6; fixture.scope = scoped(6)
precondition(frame.refresh() && frame.scope.mode_generation == 6)
fixture.sequence = 7; fixture.scope = HaloSenseWeaponActionStatus()
precondition(frame.refresh() && frame.sequence == 7 && frame.scope.action == 0,
             "GPU/incomplete/ordinary publication must not inherit a scope marker")
fixture.sequence = 8; fixture.scope = scoped(8)
precondition(frame.refresh() && frame.scope.mode_generation == 8)
fixture.sequence = 9; fixture.scope = scoped(9)
precondition(!frame.refresh(expectedSequence: 8) && frame.sequence == 8 && frame.scope.action == 0,
             "Wrong expected sequence must retire scope without committing a mismatched texture")
precondition(frame.refresh(expectedSequence: 9) && frame.scope.mode_generation == 9)
fixtureAvailable = false
precondition(!frame.refresh() && frame.scope.action == 0)
fixtureAvailable = true; fixture.sequence = 10; fixture.scope = scoped(10)
precondition(frame.refresh() && frame.scope.mode_generation == 10)
fixture.sequence = 11; fixture.byte_count = 0
precondition(!frame.refresh() && frame.scope.action == 0)
print("IMMUTABLE_GPU_FRAME_PASS coherent scope metadata, hint/copy race, failed/invalid/unscoped transitions, expected sequence; immutable in-flight GPU snapshot")
