// Real Metal validation of the exact shipped reticle fragment, including
// original additive pixels and premultiplied-alpha decoding for each hand.
import Foundation
import Metal
import simd

@main struct SenseReticleGPUValidation {
    struct Vertex { var position: SIMD4<Float>; var uv: SIMD2<Float>; var weight: SIMD2<Float> }
    static func main() throws {
        let source = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
        let shader = source.components(separatedBy: "private static let shader = \"\"\"")[1].components(separatedBy: "\"\"\"")[0]
        guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else { fatalError("Metal unavailable") }
        let library = try device.makeLibrary(source: shader, options: nil)
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = library.makeFunction(name: "curve_vertex")
        descriptor.fragmentFunction = library.makeFunction(name: "sense_reticle_fragment")
        descriptor.colorAttachments[0].pixelFormat = .rgba32Float
        let pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        let inputDesc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: 1, height: 1, mipmapped: false)
        inputDesc.storageMode = .shared; inputDesc.usage = .shaderRead
        let left = device.makeTexture(descriptor: inputDesc)!, right = device.makeTexture(descriptor: inputDesc)!
        var l: UInt32 = 0x80400000, r: UInt32 = 0x000000ff
        left.replace(region: MTLRegionMake2D(0,0,1,1), mipmapLevel: 0, withBytes: &l, bytesPerRow: 4)
        right.replace(region: MTLRegionMake2D(0,0,1,1), mipmapLevel: 0, withBytes: &r, bytesPerRow: 4)
        let outputDesc = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba32Float, width: 8, height: 8, mipmapped: false)
        outputDesc.storageMode = .shared; outputDesc.usage = .renderTarget
        let output = device.makeTexture(descriptor: outputDesc)!
        for hand in 0...1 {
            let vertices = [(Float(-1),Float(-1)),(1, -1),(-1,1),(-1,1),(1,-1),(1,1)].map { x,y in
                Vertex(position: SIMD4(x,y,0,1), uv: SIMD2(0.5,0.5), weight: SIMD2(Float(hand),0))
            }
            let command = queue.makeCommandBuffer()!, pass = MTLRenderPassDescriptor()
            pass.colorAttachments[0].texture = output; pass.colorAttachments[0].loadAction = .clear; pass.colorAttachments[0].storeAction = .store
            let encoder = command.makeRenderCommandEncoder(descriptor: pass)!
            encoder.setRenderPipelineState(pipeline); encoder.setCullMode(.none)
            vertices.withUnsafeBytes { encoder.setVertexBytes($0.baseAddress!, length: $0.count, index: 0) }
            var matrix = matrix_identity_float4x4, base: UInt32 = 0
            encoder.setVertexBytes(&matrix, length: MemoryLayout<simd_float4x4>.stride, index: 1)
            encoder.setVertexBytes(&base, length: 4, index: 2)
            encoder.setFragmentTexture(left, index: 0); encoder.setFragmentTexture(right, index: 1)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: vertices.count); encoder.endEncoding()
            command.commit(); command.waitUntilCompleted(); precondition(command.status == .completed)
            var pixels = [SIMD4<Float>](repeating: .zero, count: 64)
            pixels.withUnsafeMutableBytes { output.getBytes($0.baseAddress!, bytesPerRow: 8*16, from: MTLRegionMake2D(0,0,8,8), mipmapLevel: 0) }
            let expected = hand == 0 ? SIMD4<Float>(pow((0.5+0.055)/1.055,2.4)*(128.0/255),0,0,128.0/255) : SIMD4<Float>(0,0,1,0)
            precondition(pixels.allSatisfy { simd_length($0-expected) < 0.002 }, "Original artwork color/alpha or hand selection changed")
        }
        print("PASS exact production reticle shader: distinct hand textures, premultiplied RGB decoded once, additive zero-alpha art remains visible")
    }
}
