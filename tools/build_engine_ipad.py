#!/usr/bin/env python3
"""Build the native iPad/iPhone app from the verified local whole-engine sources.

This performs no device or simulator actions. Signing is optional and requires
an explicit matching profile and identity; private data stays in .setup.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import time

from build_engine_vision import generated_inventory
from prepare_engine_vision_device import clone_copy
from visual_assets import bundle_visual_assets, ensure_visual_assets

REPO = Path(__file__).resolve().parents[1]
APP = REPO / "native/EnginePad"
VISION = REPO / "native/EngineVision/Sources"
HOST = REPO / "native/EngineHost"
GEN = REPO / "native/build/engine-reuse/whole-exe"
# Never default into a dated evidence folder: old candidates and receipts there
# are historical records, not a reusable build cache.
DEFAULT_OUTPUT = REPO / ".setup/MasterCheefMobile/direct-build"
PROTECTED_OUTPUTS = [REPO / ".setup/MasterCheefIPadCloud-20261020"]
DEVICE_FAMILIES = [1, 2]  # iPhone and iPad share one bundle identifier and cloud container
DEFAULT_DISPLAY_NAME = "117"  # visible name only; bundle/executable/container stay technical
SHARED_SWIFT = ["EngineAssets.swift", "EngineFrameView.swift", "EngineFrameTexture.swift",
                "EngineLibraryStyle.swift",
                "EngineHaptics.swift", "EngineAboutView.swift", "EngineGraphicsModsView.swift",
                "EngineSaveArchive.swift", "EngineCloudSaves.swift", "EngineCloudSavesView.swift"]
HOST_C = ["host.c", "shims_kernel32.c", "shims_misc.c", "winsock.c", "directsound.c", "directsound_mixer.c",
          "vorbis_shim.c", "d3d9.c", "texture_decode.c", "metalshader.c", "dinput8.c", "ddraw.c",
          "resources.c", "overrides.c", "threading.c", "pointer.c", "haptics.c", "halo_settings.c"]
FRAMEWORKS = ["Foundation", "SwiftUI", "UIKit", "Metal", "MetalFX", "MetalKit", "QuartzCore",
              "GameController", "CoreHaptics", "AudioToolbox", "AVFAudio", "UniformTypeIdentifiers"]


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def local_input_inventory(source: Path, include_dirs: list[Path], *, root: Path = REPO) -> dict[str, str]:
    """Seal a translation unit and its recursively quoted local includes.

    Generated chunks include hundreds of sub_*.c bodies. Their chunk wrapper
    can remain byte-identical when translated behavior changes, so a wrapper
    hash alone must never authorize reuse. Include inactive local branches too:
    the conservative superset also invalidates when a future flag enables one.
    SDK/system headers are covered by the selected SDK/toolchain, not copied.
    """
    quoted = re.compile(r'^\s*#\s*(?:include|import)\s*"([^"\n]+)"', re.MULTILINE)
    pending = [source.resolve()]
    visited: set[Path] = set()
    inventory: dict[str, str] = {}
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        content = path.read_bytes()
        try: name = str(path.relative_to(root.resolve()))
        except ValueError: name = str(path)
        inventory[name] = hashlib.sha256(content).hexdigest()
        for name in quoted.findall(content.decode("utf-8", errors="replace")):
            candidates = [path.parent / name, *(directory / name for directory in include_dirs)]
            included = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if included is not None:
                pending.append(included)
            elif Path(name).suffix == ".c":
                raise RuntimeError(f"Missing included C input {name} from {path}")
    return dict(sorted(inventory.items()))


def native_fingerprint(command: list[str], common_dependency_hash: str, local_inputs: dict[str, str]) -> str:
    # Version 2 intentionally rejects old cache records with no include seal.
    return hashlib.sha256(json.dumps([2, command, common_dependency_hash, local_inputs], sort_keys=True).encode()).hexdigest()


def changed_inputs(inventory: dict[str, str], *, root: Path = REPO) -> list[str]:
    return [name for name, expected in inventory.items()
            if not (root / name).is_file() or digest(root / name) != expected]


def authorized_capabilities(allowed: dict, required: dict) -> dict:
    """Fail before signing when a profile cannot supply a source capability."""
    for key, value in required.items():
        permission = allowed.get(key)
        if value is True:
            valid = permission is True
        elif isinstance(value, list):
            # Apple profiles may grant all iCloud service types with a scalar
            # wildcard, even though the signed app requests a concrete array.
            # Do not generalize that representation to other capabilities.
            patterns = permission if isinstance(permission, list) else (
                ["*"] if key == "com.apple.developer.icloud-services" and permission == "*" else [])
            valid = bool(patterns) and all(
                isinstance(item, str) and any(
                    isinstance(pattern, str) and fnmatch.fnmatchcase(item, pattern)
                    for pattern in patterns
                ) for item in value
            )
        else:
            valid = permission == value
        if not valid:
            raise RuntimeError(f"Profile does not authorize required capability: {key}")
    return dict(required)


def project_versions(spec: Path = APP / "project.yml") -> tuple[str, str]:
    """Read the Xcode target's versions so both build paths stamp the same app."""
    text = spec.read_text()
    values = {}
    for key in ("MARKETING_VERSION", "CURRENT_PROJECT_VERSION"):
        match = re.search(rf'^\s*{key}:\s*"?([0-9][0-9.]*)"?\s*$', text, re.MULTILINE)
        if not match:
            raise RuntimeError(f"{spec} does not define {key}")
        values[key] = match.group(1)
    return values["MARKETING_VERSION"], values["CURRENT_PROJECT_VERSION"]


def validate_versions(marketing: str, build: str) -> None:
    if not re.fullmatch(r"\d+(\.\d+){0,2}", marketing):
        raise RuntimeError(f"Invalid marketing version: {marketing}")
    if not re.fullmatch(r"\d+(\.\d+){0,2}", build):
        raise RuntimeError(f"Invalid build number: {build}")


def require_writable_output(output: Path) -> None:
    for protected in PROTECTED_OUTPUTS:
        resolved = protected.resolve()
        if output == resolved or resolved in output.parents:
            raise RuntimeError(f"Refusing to build inside historical evidence: {protected}")


def validate_display_name(name: str) -> str:
    value = name.strip()
    if not value or len(value) > 64 or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise RuntimeError("Display name must be 1-64 printable characters")
    return value


def bundle_info(bundle_id: str, marketing: str, build: str) -> dict:
    return {"CFBundleDevelopmentRegion": "en", "CFBundleDisplayName": DEFAULT_DISPLAY_NAME,
            "CFBundleExecutable": "MasterCheef", "CFBundleIdentifier": bundle_id,
            "CFBundleInfoDictionaryVersion": "6.0", "CFBundleName": "MasterCheef",
            "CFBundlePackageType": "APPL", "CFBundleShortVersionString": marketing, "CFBundleVersion": build,
            "CFBundleSupportedPlatforms": ["iPhoneOS"], "DTPlatformName": "iphoneos",
            "MinimumOSVersion": "18.0", "UIDeviceFamily": DEVICE_FAMILIES, "UILaunchScreen": {},
            "UISupportedInterfaceOrientations": ["UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"],
            "UIRequiresFullScreen": True, "UIApplicationSceneManifest": {"UIApplicationSupportsMultipleScenes": False},
            "GCSupportedGameControllers": [{"ProfileName": "ExtendedGamepad"}],
            "GCSupportsControllerUserInteraction": True,
            "HaloBuildID": dt.datetime.now(dt.timezone.utc).isoformat()}


def compose_info(app_info: dict, *, bundle_id: str, marketing: str, build: str,
                 display_name: str, require_vision_seed: bool) -> dict:
    """Merge source Info.plist, then apply explicit packaging choices last.

    The public default lets a new iPhone/iPad player start without any Vision
    save. --require-vision-seed is only for a personal migration candidate.
    """
    info = bundle_info(bundle_id, marketing, build)
    info.update(app_info)
    info.update({"CFBundleIdentifier": bundle_id, "CFBundleExecutable": "MasterCheef",
                 "CFBundleName": "MasterCheef", "CFBundleShortVersionString": marketing,
                 "CFBundleVersion": build, "CFBundleDisplayName": validate_display_name(display_name),
                 "MasterCheefRequireVisionSeed": bool(require_vision_seed)})
    return info


def uncovered_devices(profile: dict, udids: list[str]) -> list[str]:
    provisioned = set(profile.get("ProvisionedDevices", []))
    if profile.get("ProvisionsAllDevices"):
        return []
    return [udid for udid in udids if udid not in provisioned]


def execute(command: list[str], log: Path) -> None:
    result = subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log.write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f"{command[0]} failed ({result.returncode}); {log}\n{result.stdout[-12000:]}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--configuration", choices=["Debug", "Release"], default="Release")
    parser.add_argument("--jobs", type=int, default=2, choices=[1, 2])
    parser.add_argument("--bundle-id", default="com.zephyrstudios.mastercheef.ipad")
    parser.add_argument("--profile", type=Path)
    parser.add_argument("--identity")
    parser.add_argument("--device-udid", action="append", default=[],
                        help="Check profile coverage only (repeat for iPad and iPhone); never contacts a device")
    parser.add_argument("--marketing-version", help="Defaults to MARKETING_VERSION in native/EnginePad/project.yml")
    parser.add_argument("--build-number", help="Defaults to CURRENT_PROJECT_VERSION in native/EnginePad/project.yml")
    parser.add_argument("--display-name", default=DEFAULT_DISPLAY_NAME,
                        help="Visible home-screen name only (default: %(default)s)")
    parser.add_argument("--require-vision-seed", action="store_true",
                        help="Personal migration candidate: wait for a verified Vision Pro save before first play")
    parser.add_argument("--preflight", action="store_true")
    parser.add_argument("--typecheck", action="store_true")
    parser.add_argument("--native-only", action="store_true", help="Compile native objects while shared Swift work is still in progress")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if bool(args.profile) != bool(args.identity):
        parser.error("--profile and --identity must be supplied together")
    default_marketing, default_build = project_versions()
    marketing, build = args.marketing_version or default_marketing, args.build_number or default_build
    validate_versions(marketing, build)
    display_name = validate_display_name(args.display_name)
    output = args.output.expanduser().resolve()
    require_writable_output(output)
    output.mkdir(parents=True, exist_ok=True)
    objects, logs = output / "objects", output / "logs"
    objects.mkdir(exist_ok=True); logs.mkdir(exist_ok=True)
    generation_metadata = GEN / "generation.json"
    try:
        inventory = generated_inventory()
        inventory["generationMetadataSHA256"] = digest(generation_metadata)
        generated_error = None
    except (RuntimeError, OSError) as error:
        # Swift typecheck needs only headers and Swift sources. Preflight
        # reports the missing generated engine; compilation still requires it.
        inventory, generated_error = {}, str(error)
    sdk = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"], text=True).strip()
    target = "arm64-apple-ios18.0"
    c_sources = [HOST / name for name in HOST_C]
    mojo = REPO / "third_party/mojoshader"
    c_sources += [mojo / name for name in ["mojoshader.c", "mojoshader_common.c", "profiles/mojoshader_profile_common.c", "profiles/mojoshader_profile_metal.c"]]
    c_sources += sorted(GEN.glob("chunk_*.c")) + [GEN / "engine_bundle.c", GEN / "engine_imports.c"]
    c_sources += [VISION / "EngineVisionRuntime.m", HOST / "gamecontroller.m", HOST / "metalrenderer.m", APP / "Sources/EnginePadRuntime.m"]
    swift_sources = sorted((APP / "Sources").glob("*.swift")) + [VISION / name for name in SHARED_SWIFT]
    preflight = {"sdk": sdk, "target": target, "jobs": args.jobs, "nativeSources": len(c_sources),
                 "swiftSources": len(swift_sources), "freeBytes": shutil.disk_usage(output).free,
                 "missingSwift": [str(p.relative_to(REPO)) for p in swift_sources if not p.is_file()],
                 "deviceFamilies": DEVICE_FAMILIES, "marketingVersion": marketing, "buildNumber": build,
                 "displayName": display_name, "requireVisionSeed": args.require_vision_seed,
                 "generatedEngineError": generated_error, "output": str(output), **inventory}
    (output / "preflight.json").write_text(json.dumps(preflight, indent=2) + "\n")
    if args.preflight:
        print(json.dumps(preflight, indent=2)); return 1 if generated_error or preflight["missingSwift"] else 0
    include_dirs = [APP / "Sources", VISION, HOST, REPO / "native/EngineReuse", GEN, mojo]
    includes = [argument for directory in include_dirs for argument in ("-I", str(directory))]
    flags = ["-target", target, "-isysroot", sdk, f"-ffile-prefix-map={REPO}=.",
             "-DENGINE_FLAT_MEMORY=1", "-DHALO_ARM64_FENV_FAST=1", "-frounding-math", "-ffp-contract=off",
             "-DMOJOSHADER_NO_VERSION_INCLUDE=1", "-O2" if args.configuration == "Release" else "-O0", *includes]
    for name in ["D3D", "BYTECODE", "HLSL", "GLSL120", "GLSLES", "GLSLES3", "GLSL", "ARB1", "ARB1_NV", "SPIRV", "GLSPIRV"]:
        flags.append(f"-DSUPPORT_PROFILE_{name}=0")
    swift_base = ["xcrun", "--sdk", "iphoneos", "swiftc", "-target", target, "-sdk", sdk,
                  "-parse-as-library", "-swift-version", "5", "-Xfrontend", "-strict-concurrency=minimal",
                  "-import-objc-header", str(APP / "Sources/EnginePad-Bridging-Header.h"),
                  "-Xcc", f"-I{VISION}", "-Xcc", f"-I{HOST}", "-module-name", "MasterCheefPad"]
    if args.typecheck:
        execute([*swift_base, "-typecheck", *map(str, swift_sources)], logs / "swift-typecheck.log")
        print("iPad/iPhone Swift SDK typecheck passed"); return 0
    if generated_error:
        raise RuntimeError(generated_error)
    if shutil.disk_usage(output).free < 6 * 1024**3:
        raise RuntimeError("At least6 GiB free is required before compiling the engine")
    started = time.time()
    # Hash transitive local headers/includes along with each compile command.
    # A resumed build never silently reuses another platform's object files.
    dependencies = sorted(set([*HOST.glob("*.h"), *HOST.glob("*.inc"), *GEN.glob("*.h"),
                               *VISION.glob("*.h"), VISION / "audio_session.inc",
                               *APP.glob("Sources/*.h"), *mojo.glob("*.h"), *mojo.glob("profiles/*.h")]))
    common_inputs = {str(p.relative_to(REPO)): digest(p) for p in dependencies}
    dependency_hash = hashlib.sha256(json.dumps(common_inputs, sort_keys=True).encode()).hexdigest()
    native_receipts = []
    def compile_one(index_source: tuple[int, Path]) -> dict:
        index, source = index_source
        obj = objects / f"native_{index:03d}_{source.stem}.o"
        command = ["xcrun", "--sdk", "iphoneos", "clang", *flags]
        if source.suffix == ".m": command += ["-fobjc-arc", "-fblocks"]
        command += ["-c", str(source), "-o", str(obj)]
        local_inputs = local_input_inventory(source, include_dirs)
        source_hash = local_inputs[str(source.relative_to(REPO))]
        fingerprint = native_fingerprint(command, dependency_hash, local_inputs)
        seal = obj.with_suffix(".json")
        cached = json.loads(seal.read_text()) if seal.is_file() else {}
        if not (obj.is_file() and cached.get("cacheVersion") == 2 and cached.get("localInputs") == local_inputs
                and cached.get("fingerprint") == fingerprint and cached.get("objectSHA256") == digest(obj)):
            execute(command, logs / f"native_{index:03d}_{source.stem}.log")
            if local_input_inventory(source, include_dirs) != local_inputs or changed_inputs(common_inputs):
                raise RuntimeError(f"Input changed during compile: {source}")
            cached = {"source": str(source.relative_to(REPO)), "sourceSHA256": source_hash,
                      "object": str(obj), "objectSHA256": digest(obj), "fingerprint": fingerprint,
                      "cacheVersion": 2, "localInputs": local_inputs}
            seal.write_text(json.dumps(cached, indent=2) + "\n")
        return cached
    print(f"iPad native compile: {len(c_sources)} units; {args.jobs} jobs", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, job) for job in enumerate(c_sources)]
        for future in concurrent.futures.as_completed(futures):
            native_receipts.append(future.result())
            print(f"native {len(native_receipts)}/{len(c_sources)}", flush=True)
    (output / "native-receipt.json").write_text(json.dumps(native_receipts, indent=2) + "\n")
    native_input_changes = set(changed_inputs(common_inputs))
    for record in native_receipts:
        native_input_changes.update(changed_inputs(record["localInputs"]))
    if native_input_changes:
        raise RuntimeError("Native inputs changed during build: " + ", ".join(sorted(native_input_changes)))
    if args.native_only:
        print("Native iPad objects complete; Swift/link/package pending"); return 0
    swift_map, swift_objects = {}, []
    for index, source in enumerate(swift_sources):
        obj = objects / f"swift_{index:03d}_{source.stem}.o"
        swift_map[str(source)] = {"object": str(obj)}; swift_objects.append(obj)
    map_path = output / "swift-output-map.json"
    map_path.write_text(json.dumps(swift_map, indent=2) + "\n")
    swift_hashes = {str(p.relative_to(REPO)): digest(p) for p in swift_sources}
    print(f"iPad Swift compile: {len(swift_sources)} sources", flush=True)
    execute([*swift_base, "-O" if args.configuration == "Release" else "-Onone", "-emit-module",
             "-emit-module-path", str(objects / "MasterCheefPad.swiftmodule"), "-c", *map(str, swift_sources),
             "-output-file-map", str(map_path), "-j", str(args.jobs)], logs / "swift-compile.log")
    product = output / "MasterCheef.app"
    product.mkdir(exist_ok=True)
    executable = product / "MasterCheef"
    command = ["xcrun", "--sdk", "iphoneos", "swiftc", "-target", target, "-sdk", sdk,
               "-o", str(executable), *[r["object"] for r in sorted(native_receipts, key=lambda r: r["object"])],
               *map(str, swift_objects), "-Xlinker", "-rpath", "-Xlinker", "@executable_path/Frameworks"]
    for framework in FRAMEWORKS: command += ["-framework", framework]
    command += ["-Xlinker", "-lm"]
    print("iPad link", flush=True)
    execute(command, logs / "link.log")
    info = compose_info(plistlib.loads((APP / "Info.plist").read_bytes()), bundle_id=args.bundle_id,
                        marketing=marketing, build=build, display_name=display_name,
                        require_vision_seed=args.require_vision_seed)
    icon_info = output / "app-icon-info.plist"
    execute(["xcrun", "--sdk", "iphoneos", "actool", "--compile", str(product), "--platform", "iphoneos",
             "--minimum-deployment-target", "18.0", "--target-device", "iphone", "--target-device", "ipad",
             "--app-icon", "AppIcon",
             "--output-partial-info-plist", str(icon_info), str(APP / "Resources/Assets.xcassets")], logs / "assets.log")
    icon_keys = plistlib.loads(icon_info.read_bytes())
    if set(icon_keys) & {"CFBundleDisplayName", "CFBundleIdentifier", "MasterCheefRequireVisionSeed"}:
        raise RuntimeError("Asset catalog output must not change identity or migration metadata")
    info.update(icon_keys)
    (product / "Info.plist").write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY))
    (product / "PkgInfo").write_bytes(b"APPL????")
    for name in ["ThirdPartyNotices.txt", "PrivacyInfo.xcprivacy"]:
        shutil.copy2(REPO / "native/EngineVision/Resources" / name, product / name)
    bundle_visual_assets(product, ensure_visual_assets())
    payload = REPO / ".setup/GamePayload"
    manifest_path = REPO / ".setup/GamePayloadManifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("formatVersion") != 1: raise RuntimeError("Unsupported game payload manifest")
    for row in manifest["files"]:
        relative = Path(row["path"])
        if relative.is_absolute() or ".." in relative.parts: raise RuntimeError("Unsafe payload path")
        source = payload / relative
        if source.is_symlink() or source.stat().st_size != row["bytes"] or digest(source) != row["sha256"]:
            raise RuntimeError(f"Game payload verification failed: {relative}")
        destination = product / "GamePayload" / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists(): clone_copy(source, destination)
        elif digest(destination) != row["sha256"]: raise RuntimeError(f"Existing candidate data differs: {relative}")
    shutil.copy2(manifest_path, product / manifest_path.name)
    signed = False
    if args.profile:
        profile = plistlib.loads(subprocess.check_output(["security", "cms", "-D", "-i", str(args.profile)]))
        allowed = profile.get("Entitlements", {})
        prefix = profile["ApplicationIdentifierPrefix"][0]
        wanted = f"{prefix}.{args.bundle_id}"
        pattern = allowed.get("application-identifier", "")
        if not (wanted == pattern or (pattern.endswith(".*") and wanted.startswith(pattern[:-1]))):
            raise RuntimeError("Profile does not authorize this app identifier")
        if "iOS" not in profile.get("Platform", []) or profile["ExpirationDate"] <= dt.datetime.now(dt.timezone.utc).replace(tzinfo=None):
            raise RuntimeError("Profile expired or does not support iOS")
        missing_devices = uncovered_devices(profile, args.device_udid)
        if missing_devices:
            raise RuntimeError("Profile does not cover requested device(s): " + ", ".join(missing_devices))
        entitlements = {"application-identifier": wanted, "com.apple.developer.team-identifier": profile["TeamIdentifier"][0],
                        "get-task-allow": bool(allowed.get("get-task-allow")), "keychain-access-groups": [wanted]}
        required = plistlib.loads((APP / "EnginePad.entitlements").read_bytes())
        entitlements.update(authorized_capabilities(allowed, required))
        entitlements_path = output / "signing-entitlements.plist"
        entitlements_path.write_bytes(plistlib.dumps(entitlements))
        shutil.copy2(args.profile, product / "embedded.mobileprovision")
        execute(["codesign", "--force", "--sign", args.identity, "--entitlements", str(entitlements_path),
                 "--generate-entitlement-der", str(product)], logs / "sign.log")
        execute(["codesign", "--verify", "--deep", "--strict", str(product)], logs / "signature-verify.log")
        signed = True
    # Integration must rebuild if another worker changed any compiled source.
    stale = set(changed_inputs(common_inputs))
    for record in native_receipts:
        stale.update(changed_inputs(record["localInputs"]))
    stale.update(changed_inputs(swift_hashes))
    if digest(generation_metadata) != inventory["generationMetadataSHA256"]:
        stale.add(str(generation_metadata.relative_to(REPO)))
    if stale: raise RuntimeError("Inputs changed during build; rebuild before using candidate: " + ", ".join(sorted(stale)))
    receipt = {"builtAtUTC": dt.datetime.now(dt.timezone.utc).isoformat(), "platform": "iOS", "target": target,
               "product": str(product), "bundleID": args.bundle_id, "signed": signed, "installed": False,
               "marketingVersion": marketing, "buildNumber": build, "deviceFamilies": DEVICE_FAMILIES,
               "displayName": display_name, "requireVisionSeed": args.require_vision_seed,
               "gameplayValidated": False, "elapsedSeconds": round(time.time()-started, 2), "jobs": args.jobs,
               "executableSHA256": digest(executable), "nativeSources": native_receipts, "swiftSources": swift_hashes,
               "dependencySHA256": dependency_hash, "commonInputs": common_inputs, "cacheVersion": 2, **inventory}
    (output / "build-receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(json.dumps({k: v for k, v in receipt.items() if k not in ("nativeSources", "swiftSources", "commonInputs")}, indent=2))
    return 0


if __name__ == "__main__":
    try: raise SystemExit(main())
    except Exception as error:
        print(f"build_engine_ipad.py: {error}", file=sys.stderr); raise SystemExit(2)
