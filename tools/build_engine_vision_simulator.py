#!/usr/bin/env python3
"""Build a genuine arm64 visionOS Simulator app without contacting a simulator.

All native sources are compiled for xrsimulator. The dedicated output directory
owns its cache, app, logs, and input receipts; device products are never reused.
The bundle includes the existing locally owned, manifest-verified game payload.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import time

from build_engine_ipad import HOST_C, changed_inputs, digest, local_input_inventory, native_fingerprint
from build_engine_vision import generated_inventory
from prepare_engine_vision_device import clone_copy
from visual_assets import bundle_visual_assets, ensure_visual_assets

REPO = Path(__file__).resolve().parents[1]
APP = REPO / "native/EngineVision"
VISION = APP / "Sources"
HOST = REPO / "native/EngineHost"
GEN = REPO / "native/build/engine-reuse/whole-exe"
SDK = "xrsimulator"
TARGET = "arm64-apple-xros26.0-simulator"
DEFAULT_OUTPUT = REPO / ".setup/MasterCheefSimulator-20261020/build"
FRAMEWORKS = ["Foundation", "SwiftUI", "UIKit", "Metal", "MetalKit", "QuartzCore",
              "CompositorServices", "ARKit", "GameController", "CoreHaptics",
              "AudioToolbox", "AVFAudio", "UniformTypeIdentifiers"]


def window_scene_manifest() -> dict:
    # SwiftUI's defaultLaunchBehavior only selects scenes within the preferred
    # role. The immersive space remains declared by the production Swift App.
    return {"UIApplicationSupportsMultipleScenes": True,
            "UIApplicationPreferredDefaultSceneSessionRole": "UIWindowSceneSessionRoleApplication",
            "UISceneConfigurations": {}}


def execute(command: list[str], log: Path) -> None:
    result = subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); {log}\n{result.stdout[-12000:]}")


def verify_simulator_macho(path: Path) -> None:
    commands = subprocess.check_output(["xcrun", "otool", "-l", str(path)], text=True)
    platforms = re.findall(r"^\s*platform\s+(\S+)\s*$", commands, re.MULTILINE)
    if platforms != ["12"]:
        raise RuntimeError(f"Expected genuine XROSSIMULATOR platform 12 in {path}; got {platforms}")
    architecture = subprocess.check_output(["xcrun", "lipo", "-archs", str(path)], text=True).strip()
    if architecture != "arm64":
        raise RuntimeError(f"Expected arm64 in {path}; got {architecture}")


def claim_output(path: Path) -> None:
    if not path.is_relative_to(REPO / ".setup"):
        raise RuntimeError("Simulator build output must be a dedicated directory below this repo's .setup")
    owner = path / "simulator-builder.json"
    expected = {"formatVersion": 1, "repo": str(REPO), "target": TARGET}
    if path.exists() and not owner.exists() and any(path.iterdir()):
        raise RuntimeError(f"Refusing to reuse an unowned nonempty output directory: {path}")
    path.mkdir(mode=0o700, parents=True, exist_ok=True)
    if owner.exists() and json.loads(owner.read_text()) != expected:
        raise RuntimeError(f"Output directory belongs to another build: {path}")
    owner.write_text(json.dumps(expected, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--jobs", type=int, choices=[1, 2], default=2)
    parser.add_argument("--bundle-id", default="com.zephyrstudios.mastercheef.simulator")
    parser.add_argument("--version", default="1.0.7")
    parser.add_argument("--build-number", default="107")
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--typecheck", action="store_true")
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    output = args.output.expanduser().resolve()
    claim_output(output)
    objects, logs = output / "objects", output / "logs"
    objects.mkdir(exist_ok=True)
    logs.mkdir(exist_ok=True)
    inventory = generated_inventory()
    generation_metadata = GEN / "generation.json"
    inventory["generationMetadataSHA256"] = digest(generation_metadata)
    sdk = subprocess.check_output(["xcrun", "--sdk", SDK, "--show-sdk-path"], text=True).strip()
    toolchain = subprocess.check_output(["xcrun", "--sdk", SDK, "clang", "--version"], text=True)
    sdk_version = subprocess.check_output(["xcrun", "--sdk", SDK, "--show-sdk-version"], text=True).strip()
    c_sources = [HOST / name for name in HOST_C]
    mojo = REPO / "third_party/mojoshader"
    c_sources += [mojo / name for name in ["mojoshader.c", "mojoshader_common.c",
                   "profiles/mojoshader_profile_common.c", "profiles/mojoshader_profile_metal.c"]]
    c_sources += sorted(GEN.glob("chunk_*.c")) + [GEN / "engine_bundle.c", GEN / "engine_imports.c"]
    c_sources += [VISION / "EngineVisionRuntime.m", VISION / "EngineDiagnosticsBridge.m",
                  HOST / "gamecontroller.m", HOST / "metalrenderer.m"]
    swift_sources = sorted(VISION.glob("*.swift"))
    build_tools = {str(path.relative_to(REPO)): digest(path) for path in [
        Path(__file__), REPO / "tools/build_engine_ipad.py", REPO / "tools/build_engine_vision.py",
        REPO / "tools/prepare_engine_vision_device.py", REPO / "tools/visual_assets.py"]}
    preflight = {"sdk": sdk, "sdkVersion": sdk_version, "target": TARGET, "jobs": args.jobs,
                 "nativeSources": len(c_sources), "swiftSources": len(swift_sources),
                 "freeBytes": shutil.disk_usage(output).free, "output": str(output), **inventory}
    (output / "preflight.json").write_text(json.dumps(preflight, indent=2) + "\n")
    if args.preflight:
        print(json.dumps(preflight, indent=2))
        return 0
    includes = [VISION, HOST, REPO / "native/EngineReuse", GEN, mojo]
    flags = ["-target", TARGET, "-isysroot", sdk, f"-ffile-prefix-map={REPO}=.",
             "-DENGINE_FLAT_MEMORY=1", "-DHALO_ARM64_FENV_FAST=1", "-frounding-math",
             "-ffp-contract=off", "-DMOJOSHADER_NO_VERSION_INCLUDE=1",
             "-O2" if args.configuration == "Release" else "-O0",
             *[arg for path in includes for arg in ("-I", str(path))]]
    for name in ["D3D", "BYTECODE", "HLSL", "GLSL120", "GLSLES", "GLSLES3", "GLSL",
                 "ARB1", "ARB1_NV", "SPIRV", "GLSPIRV"]:
        flags.append(f"-DSUPPORT_PROFILE_{name}=0")
    swift_base = ["xcrun", "--sdk", SDK, "swiftc", "-target", TARGET, "-sdk", sdk,
                  "-parse-as-library", "-swift-version", "5", "-Xfrontend", "-strict-concurrency=minimal",
                  "-import-objc-header", str(VISION / "EngineVision-Bridging-Header.h"),
                  "-Xcc", f"-I{VISION}", "-Xcc", f"-I{HOST}", "-module-name", "EngineVision"]
    if args.typecheck:
        execute([*swift_base, "-typecheck", *map(str, swift_sources)], logs / "swift-typecheck.log")
        print("Production visionOS Swift simulator typecheck passed")
        return 0
    if shutil.disk_usage(output).free < 6 * 1024**3:
        raise RuntimeError("At least 6 GiB free is required before compiling the engine")
    started = time.time()
    dependencies = sorted(set([*HOST.glob("*.h"), *HOST.glob("*.inc"), *GEN.glob("*.h"),
                               *VISION.glob("*.h"), VISION / "audio_session.inc",
                               *mojo.glob("*.h"), *mojo.glob("profiles/*.h")]))
    common_inputs = {str(p.relative_to(REPO)): digest(p) for p in dependencies}
    dependency_hash = hashlib.sha256(json.dumps([common_inputs, toolchain, sdk_version],
                                               sort_keys=True).encode()).hexdigest()

    def compile_one(index_source: tuple[int, Path]) -> dict:
        index, source = index_source
        obj = objects / f"native_{index:03d}_{source.stem}.o"
        command = ["xcrun", "--sdk", SDK, "clang", *flags]
        if source.suffix == ".m":
            command += ["-fobjc-arc", "-fblocks"]
        command += ["-c", str(source), "-o", str(obj)]
        local_inputs = local_input_inventory(source, includes)
        fingerprint = native_fingerprint(command, dependency_hash, local_inputs)
        seal = obj.with_suffix(".json")
        cached = json.loads(seal.read_text()) if seal.is_file() else {}
        if not (obj.is_file() and cached.get("cacheVersion") == 2
                and cached.get("localInputs") == local_inputs and cached.get("fingerprint") == fingerprint
                and cached.get("objectSHA256") == digest(obj)):
            execute(command, logs / f"native_{index:03d}_{source.stem}.log")
            if local_input_inventory(source, includes) != local_inputs or changed_inputs(common_inputs):
                raise RuntimeError(f"Input changed during compile: {source}")
            verify_simulator_macho(obj)
            cached = {"source": str(source.relative_to(REPO)),
                      "sourceSHA256": local_inputs[str(source.relative_to(REPO))],
                      "object": str(obj), "objectSHA256": digest(obj), "fingerprint": fingerprint,
                      "cacheVersion": 2, "localInputs": local_inputs, "platform": "XROSSIMULATOR"}
            seal.write_text(json.dumps(cached, indent=2) + "\n")
        else:
            verify_simulator_macho(obj)
        return cached

    print(f"Simulator native compile: {len(c_sources)} units; {args.jobs} jobs", flush=True)
    native_receipts = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, job) for job in enumerate(c_sources)]
        for future in concurrent.futures.as_completed(futures):
            native_receipts.append(future.result())
            print(f"native {len(native_receipts)}/{len(c_sources)}", flush=True)
    (output / "native-receipt.json").write_text(json.dumps(native_receipts, indent=2) + "\n")
    stale = set(changed_inputs(common_inputs))
    for record in native_receipts:
        stale.update(changed_inputs(record["localInputs"]))
    if stale:
        raise RuntimeError("Native inputs changed during build: " + ", ".join(sorted(stale)))
    if args.native_only:
        print("Genuine simulator native objects complete; Swift/link/package pending")
        return 0
    swift_map, swift_objects = {}, []
    for index, source in enumerate(swift_sources):
        obj = objects / f"swift_{index:03d}_{source.stem}.o"
        swift_map[str(source)] = {"object": str(obj)}
        swift_objects.append(obj)
    map_path = output / "swift-output-map.json"
    map_path.write_text(json.dumps(swift_map, indent=2) + "\n")
    swift_hashes = {str(p.relative_to(REPO)): digest(p) for p in swift_sources}
    print(f"Simulator Swift compile: {len(swift_sources)} production sources", flush=True)
    execute([*swift_base, "-O" if args.configuration == "Release" else "-Onone", "-emit-module",
             "-emit-module-path", str(objects / "EngineVision.swiftmodule"), "-c", *map(str, swift_sources),
             "-output-file-map", str(map_path), "-j", str(args.jobs)], logs / "swift-compile.log")
    product = output / "MasterCheef.app"
    product.mkdir(exist_ok=True)
    executable = product / "MasterCheef"
    command = ["xcrun", "--sdk", SDK, "swiftc", "-target", TARGET, "-sdk", sdk, "-o", str(executable),
               *[r["object"] for r in sorted(native_receipts, key=lambda r: r["object"])],
               *map(str, swift_objects), "-Xlinker", "-rpath", "-Xlinker", "@executable_path/Frameworks"]
    for framework in FRAMEWORKS:
        command += ["-framework", framework]
    print("Simulator link", flush=True)
    execute([*command, "-Xlinker", "-lm"], logs / "link.log")
    verify_simulator_macho(executable)
    info = {"CFBundleDevelopmentRegion": "en", "CFBundleDisplayName": "Master Cheef",
            "CFBundleExecutable": "MasterCheef", "CFBundleIdentifier": args.bundle_id,
            "CFBundleInfoDictionaryVersion": "6.0", "CFBundleName": "MasterCheef",
            "CFBundlePackageType": "APPL", "CFBundleShortVersionString": args.version,
            "CFBundleVersion": args.build_number, "CFBundleSupportedPlatforms": ["XRSimulator"],
            "DTPlatformName": SDK, "MinimumOSVersion": "26.0", "UIDeviceFamily": [7],
            "UIApplicationSceneManifest": window_scene_manifest(),
            "GCSupportedGameControllers": [{"ProfileName": "ExtendedGamepad"}],
            "GCSupportsControllerUserInteraction": True,
            "GCRequiresControllerUserInteraction": {"visionOS": True}, "UILaunchScreen": {},
            "NSHandsTrackingUsageDescription": "A pinch selects the menu item you are looking at.",
            "NSWorldSensingUsageDescription": "Head tracking aligns the game with your chosen pose.",
            "HaloBuildID": dt.datetime.now(dt.timezone.utc).isoformat()}
    # Keep controller profiles and tracking permission text identical to the
    # production app. Simulator builds still use their own bundle identifier.
    info.update(plistlib.loads((APP / "Info.plist").read_bytes()))
    icon_info = output / "app-icon-info.plist"
    resource_hashes = {str(path.relative_to(REPO)): digest(path)
                       for path in sorted((APP / "Resources").rglob("*")) if path.is_file()}
    execute(["xcrun", "--sdk", SDK, "actool", "--compile", str(product), "--platform", SDK,
             "--minimum-deployment-target", "26.0", "--target-device", "vision", "--app-icon", "AppIcon",
             "--output-partial-info-plist", str(icon_info), str(APP / "Resources/Assets.xcassets")],
            logs / "assets.log")
    info.update(plistlib.loads(icon_info.read_bytes()))
    (product / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    (product / "PkgInfo").write_bytes(b"APPL????")
    for name in ["ThirdPartyNotices.txt", "PrivacyInfo.xcprivacy"]:
        shutil.copy2(APP / "Resources" / name, product / name)
    bundle_visual_assets(product, ensure_visual_assets())
    manifest_path = REPO / ".setup/GamePayloadManifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("formatVersion") != 1:
        raise RuntimeError("Unsupported game payload manifest")
    for row in manifest["files"]:
        relative = Path(row["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("Unsafe payload path")
        source = REPO / ".setup/GamePayload" / relative
        if source.is_symlink() or source.stat().st_size != row["bytes"] or digest(source) != row["sha256"]:
            raise RuntimeError(f"Game payload verification failed: {relative}")
        destination = product / "GamePayload" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists():
            clone_copy(source, destination)
        elif digest(destination) != row["sha256"]:
            raise RuntimeError(f"Existing candidate data differs: {relative}")
    shutil.copy2(manifest_path, product / manifest_path.name)
    execute(["codesign", "--force", "--sign", "-", str(product)], logs / "sign.log")
    execute(["codesign", "--verify", "--deep", "--strict", str(product)], logs / "signature-verify.log")
    stale = set(changed_inputs(common_inputs))
    for record in native_receipts:
        stale.update(changed_inputs(record["localInputs"]))
    stale.update(changed_inputs(swift_hashes))
    stale.update(changed_inputs(resource_hashes))
    stale.update(changed_inputs(build_tools))
    if digest(generation_metadata) != inventory["generationMetadataSHA256"]:
        stale.add(str(generation_metadata.relative_to(REPO)))
    if stale:
        raise RuntimeError("Inputs changed during build; rebuild candidate: " + ", ".join(sorted(stale)))
    receipt = {"builtAtUTC": dt.datetime.now(dt.timezone.utc).isoformat(),
               "platform": "XROSSIMULATOR", "target": TARGET, "product": str(product),
               "bundleID": args.bundle_id, "buildNumber": args.build_number, "sdkVersion": sdk_version,
               "adHocSigned": True, "installed": False, "gameplayValidated": False,
               "deviceOperationsPerformed": False, "simulatorOperationsPerformed": False,
               "elapsedSeconds": round(time.time() - started, 2), "jobs": args.jobs,
               "executableSHA256": digest(executable), "nativeSources": native_receipts,
               "swiftSources": swift_hashes, "commonInputs": common_inputs, "cacheVersion": 2,
               "builderSHA256": digest(Path(__file__)), "buildTools": build_tools,
               "resources": resource_hashes, "toolchain": toolchain, **inventory}
    (output / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({k: v for k, v in receipt.items()
                      if k not in ("nativeSources", "swiftSources", "commonInputs", "resources",
                                   "buildTools", "toolchain")}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"build_engine_vision_simulator.py: {error}", file=sys.stderr)
        raise SystemExit(2)
