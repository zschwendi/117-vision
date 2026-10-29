#!/usr/bin/env python3
"""Validate preference sync using isolated defaults and an in-memory cloud.

No account, device, simulator, real preference domain, saves or cloud files
are accessed. The real CloudDocuments adapter is typechecked, not invoked.
"""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "native/EngineVision/Sources"
with tempfile.TemporaryDirectory(prefix="117-settings-tests-") as work:
    binary = Path(work) / "settings-sync"
    native = Path(work) / "halo_settings.o"
    subprocess.run(["xcrun", "clang", "-std=c11", "-c",
                    str(ROOT / "native/EngineHost/halo_settings.c"), "-o", str(native)], check=True, cwd=ROOT)
    subprocess.run(["xcrun", "swiftc", "-parse-as-library", "-swift-version", "5",
                    "-strict-concurrency=complete", "-warnings-as-errors",
                    str(SOURCES / "EngineSyncedPreferences.swift"),
                    str(SOURCES / "EngineSettingsSync.swift"),
                    str(SOURCES / "EngineSyncedPreferencesRuntime.swift"),
                    "-import-objc-header", str(ROOT / "native/EngineHost/halo_settings.h"), str(native),
                    str(ROOT / "native/EngineVision/Tests/SettingsSyncValidation.swift"),
                    "-o", str(binary)], check=True, cwd=ROOT)
    subprocess.run([str(binary)], check=True, cwd=work, timeout=60)
