#!/usr/bin/env python3
"""Run the production Vision window draw and composition on an offscreen Mac GPU.

Only MTKView/drawable and the producer snapshot are fixtures. The draw body is
extracted verbatim, with the visionOS conditional selected for this Mac test and
presentation captured offscreen. The actual composition source builds its own
Metal pipelines. No app, simulator, headset or game assets are used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from test_window_frame_clear import extract

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "native/EngineVision/Sources/EngineFrameView.swift"
COMPOSITION = ROOT / "native/EngineVision/Sources/EngineWindowComposition.swift"
FIXTURE = ROOT / "native/EngineVision/Tests/WindowFrameGPUValidation.swift"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".setup/window-gpu-frame-tests")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    inputs = {path: path.read_bytes() for path in (SOURCE, COMPOSITION, FIXTURE)}
    body, shader, clear = extract(inputs[SOURCE].decode())
    if "#if os(visionOS)" not in body:
        raise ValueError("Production Vision window branch is missing")
    body = body.replace("#if os(visionOS)", "#if WINDOW_GPU_FIXTURE")
    harness = inputs[FIXTURE].decode().replace("// __DRAW__", body).replace("// __SHADER__", shader).replace("__CLEAR__", clear)
    generated = output / "WindowFrameGPUValidation.swift"
    generated.write_text(harness)
    executable = output / "window-gpu-frame-validation"
    build = subprocess.run(["xcrun", "swiftc", "-parse-as-library", "-D", "WINDOW_GPU_FIXTURE",
                            str(COMPOSITION), str(generated), "-o", str(executable)],
                           text=True, capture_output=True, timeout=120)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    if build.returncode:
        raise RuntimeError(f"Compile failed; see {output / 'compile.log'}")
    run = subprocess.run([str(executable)], text=True, capture_output=True, timeout=30)
    (output / "stderr.log").write_text(run.stderr)
    if run.returncode not in (0, 1):
        raise RuntimeError(f"GPU harness exited {run.returncode}: {run.stderr}")
    result = json.loads(run.stdout)
    result["source_sha256"] = {str(path): hashlib.sha256(data).hexdigest() for path, data in inputs.items()}
    result["harness_sha256"] = hashlib.sha256(harness.encode()).hexdigest()
    result["exit_code"] = run.returncode
    for path, data in inputs.items():
        if path.read_bytes() != data:
            raise RuntimeError(f"Source changed during verification: {path}")
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return run.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
