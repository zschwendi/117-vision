#!/usr/bin/env python3
"""Exercise the production window draw body on an offscreen Mac Metal target.

The harness extracts draw(in:) and its shader verbatim. Only the MTKView/drawable
shell and presentation call are replaced; command encoding remains production
code. No window, simulator, headset, or game data is used.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native/EngineVision/Sources/EngineFrameView.swift"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract(source: str) -> tuple[str, str, str]:
    draw = re.search(
        r"func draw\(in view: MTKView\) \{(.*?)\n        \}\n"
        r"\s*private static let shader",
        source,
        re.DOTALL,
    )
    shader = re.search(r'private static let shader = """(.*?)"""', source, re.DOTALL)
    clear = re.search(r"view\.clearColor = (MTLClearColor\([^\n]+\))", source)
    if not draw or not shader or not clear:
        raise ValueError("Production draw/shader/clear shape changed; review harness extraction.")
    body = draw.group(1)
    if body.count("command.present(drawable)") != 1:
        raise ValueError("Expected exactly one production drawable presentation.")
    # This is the sole transformation of the production draw body. Keep its
    # normal command.commit(), then await completion outside the extracted body.
    body = body.replace("command.present(drawable)", "view.submitted = command")
    return body, shader.group(1), clear.group(1)


SWIFT = r'''
import Foundation
import Metal

final class FixtureFrame {
    let texture: MTLTexture?
    init(_ texture: MTLTexture?) { self.texture = texture }
    func refresh() -> Bool { false }
}

final class OffscreenDrawable {
    let texture: MTLTexture
    init(_ texture: MTLTexture) { self.texture = texture }
}

// The view shell supplies the same pass fields and size that MTKView supplies.
// Neither this shell nor the texture fixture performs any render encoding.
final class OffscreenView {
    var currentRenderPassDescriptor: MTLRenderPassDescriptor?
    var currentDrawable: OffscreenDrawable?
    let drawableSize: CGSize
    var submitted: MTLCommandBuffer?

    init(device: MTLDevice) {
        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm_srgb, width: 32, height: 16, mipmapped: false)
        descriptor.storageMode = .shared
        descriptor.usage = [.renderTarget]
        let texture = device.makeTexture(descriptor: descriptor)!
        let poisonPixel: [UInt8] = [255, 0, 255, 255]
        let poison = Array(repeating: poisonPixel,
                           count: texture.width * texture.height).flatMap { $0 }
        poison.withUnsafeBytes {
            texture.replace(region: MTLRegionMake2D(0, 0, texture.width, texture.height),
                            mipmapLevel: 0, withBytes: $0.baseAddress!,
                            bytesPerRow: texture.width * 4)
        }
        currentDrawable = OffscreenDrawable(texture)
        drawableSize = CGSize(width: Double(texture.width), height: Double(texture.height))
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = texture
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor = __CLEAR__
        currentRenderPassDescriptor = pass
    }
}

final class Renderer {
    let queue: MTLCommandQueue?
    let pipeline: MTLRenderPipelineState?
    let frame: FixtureFrame?

    init(queue: MTLCommandQueue, pipeline: MTLRenderPipelineState?, frame: FixtureFrame?) {
        self.queue = queue; self.pipeline = pipeline; self.frame = frame
    }

    func draw(in view: OffscreenView) {
__DRAW__
    }

    static let shader = """
__SHADER__
        """
}

func pixels(_ texture: MTLTexture) -> [UInt8] {
    var data = [UInt8](repeating: 0, count: texture.width * texture.height * 4)
    data.withUnsafeMutableBytes {
        texture.getBytes($0.baseAddress!, bytesPerRow: texture.width * 4,
                         from: MTLRegionMake2D(0, 0, texture.width, texture.height),
                         mipmapLevel: 0)
    }
    return data
}

func pixelMatches(_ data: [UInt8], offset: Int, expected: [UInt8], tolerance: Int = 0) -> Bool {
    (0..<4).allSatisfy { abs(Int(data[offset + $0]) - Int(expected[$0])) <= tolerance }
}

func srgb(_ value: Double) -> UInt8 {
    let encoded = value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055
    return UInt8(min(255, max(0, (encoded * 255).rounded())))
}

guard let device = MTLCreateSystemDefaultDevice(),
      let queue = device.makeCommandQueue() else {
    fputs("No Mac Metal device/queue available.\n", stderr)
    exit(2)
}
let library = try device.makeLibrary(source: Renderer.shader, options: nil)
let pipelineDescriptor = MTLRenderPipelineDescriptor()
pipelineDescriptor.vertexFunction = library.makeFunction(name: "window_vertex")
pipelineDescriptor.fragmentFunction = library.makeFunction(name: "frame_fragment")
pipelineDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm_srgb
let pipeline = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
let sourceDescriptor = MTLTextureDescriptor.texture2DDescriptor(
    pixelFormat: .bgra8Unorm, width: 2, height: 2, mipmapped: false)
sourceDescriptor.storageMode = .shared
sourceDescriptor.usage = [.shaderRead]
let sourceTexture = device.makeTexture(descriptor: sourceDescriptor)!
let green: [UInt8] = [0, 255, 0, 255]
let sourcePixels = Array(repeating: green, count: 4).flatMap { $0 }
sourcePixels.withUnsafeBytes {
    sourceTexture.replace(region: MTLRegionMake2D(0, 0, 2, 2), mipmapLevel: 0,
                          withBytes: $0.baseAddress!, bytesPerRow: 8)
}
let clear = __CLEAR__
let clearBytes: [UInt8] = [srgb(clear.blue), srgb(clear.green), srgb(clear.red),
                          UInt8((255 * clear.alpha).rounded())]
let cases: [(String, FixtureFrame?, MTLRenderPipelineState?, Bool)] = [
    ("nil_frame", nil, pipeline, false),
    ("empty_frame_texture", FixtureFrame(nil), pipeline, false),
    ("nil_pipeline_with_texture", FixtureFrame(sourceTexture), nil, false),
    ("nil_frame_and_pipeline", nil, nil, false),
    ("textured_center", FixtureFrame(sourceTexture), pipeline, true),
]
var results = [[String: Any]]()
var allPassed = true
for (name, frame, selectedPipeline, expectsImage) in cases {
    let view = OffscreenView(device: device)
    let target = view.currentDrawable!.texture
    let initial = pixels(target)
    let pixelCount = target.width * target.height
    let poisonInitialized = (0..<pixelCount).allSatisfy {
        pixelMatches(initial, offset: 4 * $0, expected: [255, 0, 255, 255])
    }
    precondition(poisonInitialized, "Magenta poison was not initialized: \(initial.prefix(32))")
    Renderer(queue: queue, pipeline: selectedPipeline, frame: frame).draw(in: view)
    view.submitted?.waitUntilCompleted()
    let completed = view.submitted?.status == .completed
    let output = pixels(target)
    let clearCount = (0..<pixelCount).filter {
        pixelMatches(output, offset: 4 * $0, expected: clearBytes, tolerance: 1)
    }.count
    let magentaCount = (0..<pixelCount).filter {
        pixelMatches(output, offset: 4 * $0, expected: [255, 0, 255, 255])
    }.count
    let centerOffset = 4 * ((target.height / 2) * target.width + target.width / 2)
    let centerPassed = pixelMatches(output, offset: centerOffset, expected: green, tolerance: 1)
    let passed = completed && magentaCount == 0 &&
        (expectsImage ? centerPassed : clearCount == pixelCount)
    allPassed = allPassed && passed
    results.append([
        "case": name, "passed": passed, "command_completed": completed,
        "pixel_count": pixelCount, "clear_pixel_count": clearCount,
        "magenta_pixel_count": magentaCount,
        "center_bgra": Array(output[centerOffset..<(centerOffset + 4)]),
        "expected_clear_bgra": clearBytes,
    ])
}
let result: [String: Any] = [
    "passed": allPassed, "metal_device": device.name,
    "test_type": "real Mac Metal, extracted production draw body and shader",
    "cases": results,
]
let report = try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys])
FileHandle.standardOutput.write(report)
FileHandle.standardOutput.write(Data("\n".utf8))
exit(allPassed ? 0 : 1)
'''


def run_case(source_path: Path, output_dir: Path, label: str) -> dict:
    original = source_path.read_bytes()
    body, shader, clear = extract(original.decode())
    harness = SWIFT.replace("__DRAW__", body).replace("__SHADER__", shader).replace("__CLEAR__", clear)
    swift_path = output_dir / f"{label}-window-frame-clear.swift"
    executable = output_dir / f"{label}-window-frame-clear"
    swift_path.write_text(harness)
    compile_result = subprocess.run(
        ["xcrun", "swiftc", "-g", str(swift_path), "-o", str(executable)],
        text=True, capture_output=True, timeout=120,
    )
    (output_dir / f"{label}-compile.log").write_text(compile_result.stdout + compile_result.stderr)
    if compile_result.returncode:
        raise RuntimeError(f"{label} compile failed; see {output_dir / f'{label}-compile.log'}")
    run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
    (output_dir / f"{label}-stderr.log").write_text(run.stderr)
    if run.returncode not in (0, 1):
        raise RuntimeError(f"{label} GPU harness exit {run.returncode}: {run.stderr}")
    report = json.loads(run.stdout)
    if source_path.read_bytes() != original:
        raise RuntimeError(f"{source_path} changed during validation.")
    report.update({
        "source": str(source_path), "source_sha256": sha256(original),
        "harness_sha256": sha256(harness.encode()), "exit_code": run.returncode,
    })
    (output_dir / f"{label}-result.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--before", type=Path, help="Optional pre-fix source, expected to fail clear cases.")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".setup/window-frame-clear-tests")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    result = {"after": run_case(args.source.resolve(), args.output_dir.resolve(), "after")}
    passed = result["after"]["passed"]
    if args.before:
        before = run_case(args.before.resolve(), args.output_dir.resolve(), "before")
        result["before"] = before
        expected_failure = (
            not before["passed"]
            and all(not case["passed"] and case["magenta_pixel_count"] == case["pixel_count"]
                    for case in before["cases"] if case["case"] != "textured_center")
            and next(case["passed"] for case in before["cases"] if case["case"] == "textured_center")
        )
        result["before_failed_expected_cases"] = expected_failure
        passed = passed and expected_failure
    result["passed"] = passed
    (args.output_dir / "window-frame-clear-report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
