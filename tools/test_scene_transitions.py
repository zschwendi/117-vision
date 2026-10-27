#!/usr/bin/env python3
"""Exercise actual app scene callbacks with the production generation policies.

SwiftUI/ARKit and the engine are fixtures; callback bodies and the asynchronous
first-GPU-frame gate are extracted unchanged from HaloEngineVisionApp.swift.
This is source lifecycle verification, not headset presentation acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VISION = ROOT / "native/EngineVision"
APP = VISION / "Sources/HaloEngineVisionApp.swift"
POLICIES = [VISION / "Sources/EngineSceneTransitions.swift", VISION / "Sources/EngineImmersiveOwnership.swift"]
FIXTURE = VISION / "Tests/SceneTransitionsValidation.swift"


def member(source: str, marker: str) -> str:
    """Extract a balanced Swift member, ignoring braces in strings/comments."""
    start = source.index(marker)
    opening = source.index("{", start)
    depth, quoted, escaped, line_comment, block_depth = 1, False, False, False, 0
    index = opening + 1
    while index < len(source):
        char = source[index]
        pair = source[index:index + 2]
        if line_comment:
            if char == "\n": line_comment = False
        elif block_depth:
            if pair == "/*": block_depth += 1; index += 1
            elif pair == "*/": block_depth -= 1; index += 1
        elif quoted:
            if escaped: escaped = False
            elif char == "\\": escaped = True
            elif char == '"': quoted = False
        elif pair == "//": line_comment = True; index += 1
        elif pair == "/*": block_depth = 1; index += 1
        elif char == '"': quoted = True
        elif char == "{": depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0: return source[start:index + 1]
        index += 1
    raise ValueError(f"Unclosed Swift member: {marker}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".setup/scene-transition-tests")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    inputs = {path: path.read_bytes() for path in [APP, *POLICIES, FIXTURE]}
    source = inputs[APP].decode()
    callbacks = [member(source, marker) for marker in (
        "func startImmersive(_ layer: LayerRenderer, generation: UInt64)",
        "func immersiveSceneDisappeared(generation: UInt64)",
        "private var returnDestination:",
        "private func waitForImmersiveFrame(_ generation: UInt64)",
    )]
    harness = inputs[FIXTURE].decode().replace("// __APP_CALLBACKS__", "\n\n".join(callbacks))
    generated = output / "SceneTransitionsValidation.swift"; generated.write_text(harness)
    executable = output / "scene-transitions-validation"
    build = subprocess.run(["xcrun", "swiftc", "-parse-as-library", "-swift-version", "5",
                            *(["-sanitize=address"] if args.sanitize else []),
                            "-module-cache-path", str(output / "swift-cache"),
                            *map(str, POLICIES), str(generated), "-o", str(executable)],
                           text=True, capture_output=True, timeout=120)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError(f"Scene transition compile failed; see {output / 'compile.log'}")
    run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
    (output / "stderr.log").write_text(run.stderr)
    if run.returncode not in (0, 1):
        raise RuntimeError(f"Scene transition fixture exited {run.returncode}: {run.stderr}")
    report = json.loads(run.stdout)
    report.update({"sanitizer": "address" if args.sanitize else "none", "exit_code": run.returncode,
                   "source_sha256": {str(path): hashlib.sha256(data).hexdigest() for path, data in inputs.items()},
                   "harness_sha256": hashlib.sha256(harness.encode()).hexdigest()})
    for path, data in inputs.items():
        if path.read_bytes() != data:
            raise RuntimeError(f"Source changed during verification: {path}")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return run.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
