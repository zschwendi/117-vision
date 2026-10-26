#!/usr/bin/env python3
"""Compile and run the synthetic mobile cloud-save and input-lease validations.

Each executable uses only temporary folders and private UserDefaults suites.
No iCloud account, device, simulator, game data or real save is touched.
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SOURCES = REPO / "native/EngineVision/Sources"
TESTS = REPO / "native/EngineVision/Tests"
SHARED = [SOURCES / "EngineSaveArchive.swift", SOURCES / "EngineCloudSaves.swift"]
LEASE = [REPO / "native/EnginePad/Sources/EnginePadInputLease.swift"]
VALIDATIONS = {"CloudSaveValidation.swift": SHARED, "CloudSyncValidation.swift": SHARED,
               "CloudLateArrivalValidation.swift": SHARED, "CloudMobileLifecycleValidation.swift": SHARED,
               "PadInputLeaseValidation.swift": LEASE}


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="mastercheef-cloud-tests-") as work:
        for name, sources in VALIDATIONS.items():
            binary = Path(work) / Path(name).stem
            compile_command = ["xcrun", "swiftc", "-parse-as-library", "-swift-version", "5",
                               *map(str, sources), str(TESTS / name), "-o", str(binary)]
            built = subprocess.run(compile_command, cwd=REPO, capture_output=True, text=True)
            print(f"$ {' '.join(compile_command)}\n{(built.stdout + built.stderr).strip() or '(no compiler output)'}")
            if built.returncode:
                print(built.stdout + built.stderr, file=sys.stderr)
                print(f"FAIL: {name} did not compile", file=sys.stderr)
                return 1
            ran = subprocess.run([str(binary)], cwd=work, capture_output=True, text=True, timeout=300)
            print(ran.stdout.strip() or f"{name}: no output")
            if ran.returncode:
                print(ran.stderr, file=sys.stderr)
                print(f"FAIL: {name} exited {ran.returncode}", file=sys.stderr)
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
