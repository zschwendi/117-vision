#!/usr/bin/env python3
"""Small source-only regression suite. No game files, signing or installs; one Metal check draws on the
Mac's GPU when it has one (synthetic shaders)."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'native/EngineHost'

def generated_engine():
    """The generated engine sources, for checks that run lifted original code:
    HALO_GENERATED_ENGINE, else this checkout's, else the main checkout's when
    this is a worktree. None when there are none; those checks print SKIP."""
    roots = [ROOT]
    try:
        common = subprocess.check_output(['git', 'rev-parse', '--git-common-dir'], cwd=ROOT, text=True).strip()
        roots.append((ROOT / common).resolve().parent)
    except (OSError, subprocess.CalledProcessError):
        pass
    candidates = [os.environ.get('HALO_GENERATED_ENGINE'), os.environ.get('HALO_ENGINE_GEN')]
    candidates += [root / 'native/build/engine-reuse/desktop-build75-direct' for root in roots]
    candidates += [root / 'native/build/engine-reuse/whole-exe' for root in roots]
    return next((Path(c) for c in candidates if c and (Path(c) / 'engine_functions.h').is_file()
                 and (Path(c) / 'sub_004CC0D0.c').is_file()), None)

def run(command, capture_output=False):
    print('+', ' '.join(map(str, command)), flush=True)
    result = subprocess.run(list(map(str, command)), cwd=ROOT, check=not capture_output, timeout=120,
                            capture_output=capture_output, text=capture_output)
    if capture_output:
        print(result.stdout, end='', flush=True)
        if result.stderr: print(result.stderr, end='', file=sys.stderr, flush=True)
        result.check_returncode()
    return result

# Mocked GameController/Core Haptics checks that compile gamecontroller.m:
# name, test source, extra flags, minimum macOS major version.
STRICT = ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
CONTROLLER_CHECKS = [
    ('controller-haptics', 'test_controller_haptics.m', [], 0),
    ('controller-snapshot', 'test_controller_snapshot.m', [], 0),
    ('sense-app-input', 'test_app_input.m', ['-I', HOST, '-I', ROOT / 'native/EngineReuse', '-lm'], 0),
    ('device-input', 'test_device_input.m', ['-I', HOST, '-I', ROOT / 'native/EngineReuse', '-lm'], 0),
    ('input-prompt-device', 'test_input_prompt_device.m',
     ['-I', HOST, '-I', ROOT / 'native/EngineReuse', '-lm', '-fno-sanitize-recover=undefined'], 0),
    ('controller-prompt-names', 'test_gamecontroller_prompt_names.m',
     [*STRICT, '-Wno-unused-function', '-I', HOST, '-I', ROOT / 'native/EngineReuse', '-lm',
      '-fno-sanitize-recover=undefined'], 26),
    ('gamecontroller-spatial', 'test_gamecontroller_spatial.m', STRICT, 26),
    ('controller-spatial-haptics', 'test_controller_spatial_haptics.m', STRICT, 26),
]

def run_controller_check(temp, flags, name, source, extra, minimum):
    """Returns True when the check ran and passed, False when skipped."""
    if platform.system() != 'Darwin' or int((platform.mac_ver()[0] or '0').split('.')[0]) < minimum:
        print(f'SKIP {name}: requires macOS {minimum} or later', flush=True)
        return False
    if name == 'controller-prompt-names':
        # Spatial metadata needs both the runtime and SDK. The macOS15 CI
        # host skips honestly; a capable local host runs the actual fixture.
        try:
            sdk = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-version'],
                                 capture_output=True, text=True, timeout=30)
            sdk_major = int(sdk.stdout.strip().split('.')[0]) if sdk.returncode == 0 else 0
        except (OSError, ValueError, subprocess.TimeoutExpired):
            sdk_major = 0
        if sdk_major < 26:
            print(f'SKIP {name}: requires macOS SDK26 or later', flush=True)
            return False
    binary = Path(temp) / name
    run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', *extra, HOST / 'tests' / source,
         '-framework', 'Foundation', '-framework', 'CoreHaptics', '-framework', 'GameController', '-o', binary])
    run([binary])
    return True

# Synthetic renderer checks on the Mac GPU, also selectable with --only.
METAL_CHECKS = {
    'metal-gpu-timing': 'test_metalrenderer_gpu_timing.m',
    'metal-foveation': 'test_metalrenderer_vrr.m',
    'panorama-foveation-maps': 'test_panorama_foveation_maps.m',
    'metal-publication': 'test_metalrenderer_post.m',
    # The visionOS runtime's slot publication and leases (also run in the full pass).
    'panorama-publication': '../../EngineVision/Tests/PanoramaPublicationValidation.m',
    'mobile-graphics-runtime': '../../EngineVision/Tests/MobileGraphicsRuntimeValidation.m',
    'mobile-graphics-metal': 'test_mobile_graphics_metal.m',
}

def run_metal_check(temp, flags, name):
    if platform.system() != 'Darwin':
        print(f'SKIP {name}: requires macOS Metal', flush=True)
        return False
    binary = Path(temp) / name
    extra = []
    if name in ('panorama-publication', 'mobile-graphics-runtime'):
        extra = ['-DENGINE_FLAT_MEMORY=1', '-I', HOST, '-I', ROOT / 'native/EngineReuse',
                 '-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip']
    if name == 'mobile-graphics-metal': extra = ['-w', '-DMR_TARGET_MEMORY=1']
    run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', *extra, HOST / 'tests' / METAL_CHECKS[name],
         '-framework', 'Foundation', '-framework', 'Metal', '-o', binary])
    result = run([binary], capture_output=True)
    return not any(line.startswith('SKIP:') for line in result.stdout.splitlines())

def run_frame_limit_sdk_check(temp):
    """Compile actual device target routing; no device, simulator or game code.

    The portable frame_limit_routing case executes the Mac/desktop route.
    These compile-only probes use each SDK's real TargetConditionals instead
    of overriding ENGINE_IOS_HOOKS. Missing SDKs are explicit non-passes.
    """
    complete = True
    for sdk, target in (('iphoneos', 'arm64-apple-ios18.0'), ('xros', 'arm64-apple-xros26.0')):
        try:
            lookup = subprocess.run(['xcrun', '--sdk', sdk, '--show-sdk-path'],
                                    capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.TimeoutExpired):
            lookup = None
        if lookup is None or lookup.returncode or not lookup.stdout.strip():
            print(f'SKIP frame-limit-sdk-routing {sdk}: device SDK unavailable', flush=True)
            complete = False
            continue
        run(['xcrun', '--sdk', sdk, 'clang', '-target', target, '-isysroot', lookup.stdout.strip(),
             '-O2', '-I', ROOT / 'native/EngineReuse', '-I', HOST, '-DEXPECT_ROUTED=1',
             '-c', HOST / 'tests/test_frame_limit_routing.c', '-o', Path(temp) / f'frame-limit-{sdk}.o'])
        print(f'PASS frame-limit-sdk-routing {sdk}: compiled only; no device execution', flush=True)
    return complete

# Swift validations selectable with --only: name -> (swiftc flags, sources).
VISION = ROOT / 'native/EngineVision'
PAD = ROOT / 'native/EnginePad'
SWIFT_CHECKS = {
    'launch-preferences': (['-parse-as-library'],
                           [VISION / 'Sources/EngineLaunchPreferences.swift', VISION / 'Sources/EnginePresentationPreferences.swift',
                            VISION / 'Tests/LaunchPreferencesValidation.swift']),
    'pad-input-policy': (['-parse-as-library', '-swift-version', '5'],
                         [PAD / 'Sources/EnginePadInputPolicy.swift', PAD / 'Sources/EnginePadPresentationState.swift',
                          PAD / 'Tests/EnginePadInputPolicyValidation.swift']),
    'pad-input-session': (['-parse-as-library', '-swift-version', '5'],
                          [PAD / 'Sources/EnginePadInputPolicy.swift', PAD / 'Sources/EnginePadInputSession.swift',
                           PAD / 'Tests/EnginePadInputSessionValidation.swift']),
    'foveation-geometry': (['-O', '-import-objc-header', VISION / 'Tests/FoveationGeometryBridge.h', '-Xcc', f'-I{HOST}'],
                          [VISION / 'Sources/EngineImmersiveScreenGeometry.swift', VISION / 'Tests/FoveationGeometryValidation.swift']),
    'graphics-preferences': (['-import-objc-header', HOST / 'halo_settings.h'],
                             [VISION / 'Sources/EngineGraphicsPreferences.swift', VISION / 'Tests/GraphicsPreferencesValidation.swift']),
    'sense-physical-settings': (['-parse-as-library', '-swift-version', '5', '-import-objc-header', HOST / 'sense_physical.h'],
                                [VISION / 'Sources/EngineSensePhysicalSettings.swift', VISION / 'Tests/SensePhysicalSettingsValidation.swift']),
    'sense-seat-presentation': (['-parse-as-library'],
                                [VISION / 'Sources/EngineImmersiveOrientation.swift', VISION / 'Sources/EngineSensePose.swift',
                                 VISION / 'Sources/EngineSenseLocomotion.swift', VISION / 'Sources/EngineSenseSeatPresentation.swift',
                                 VISION / 'Tests/SenseSeatPresentationValidation.swift']),
    'sense-settings': (['-parse-as-library', '-swift-version', '5', '-import-objc-header', HOST / 'sense_weapon.h'],
                       [VISION / 'Sources/EngineSenseSettingsView.swift', VISION / 'Sources/EngineSenseTrackingStatus.swift',
                        VISION / 'Tests/SenseSettingsValidation.swift']),
    # Tracked reticle on the shipped panel mesh: same target per eye.
    'reticle-panel': (['-O', '-parse-as-library'],
                      [VISION / 'Sources/EngineImmersiveScreenGeometry.swift', VISION / 'Sources/EngineSenseReticleGeometry.swift',
                       VISION / 'Tests/SenseReticlePanelValidation.swift']),
    # Moving reticle over 30 Hz sources shown at 90 Hz, actual Metal.
    'reticle-motion': (['-O', '-parse-as-library', '-D', 'RETICLE_CANDIDATE'],
                       [VISION / 'Sources/EngineImmersiveRenderTargets.swift', VISION / 'Sources/EngineImmersiveScreenGeometry.swift',
                        VISION / 'Sources/EngineSenseReticleGeometry.swift', VISION / 'Tests/SenseReticleMotionGPUValidation.swift']),
}

def run_swift_check(temp, name, sanitize=False):
    if platform.system() != 'Darwin':
        print(f'SKIP {name}: requires macOS swiftc', flush=True)
        return False
    swift_flags, sources = SWIFT_CHECKS[name]
    sources = list(sources)
    if name == 'graphics-preferences':
        settings_object = Path(temp) / 'halo_settings.o'
        run([os.environ.get('CC', 'clang'), '-O2', '-c', HOST / 'halo_settings.c', '-o', settings_object])
        sources.append(settings_object)
    binary = Path(temp) / name
    run(['swiftc', *swift_flags, *(['-sanitize=address'] if sanitize else []),
         '-module-cache-path', Path(temp) / 'swift-cache', *sources, '-o', binary])
    arguments = [VISION / 'Sources/EngineSensePhysicalSettings.swift', VISION / 'Sources/EngineSenseSettingsView.swift'] if name == 'sense-physical-settings' else []
    if name == 'reticle-motion': arguments = [VISION / 'Sources/EngineImmersive.swift']
    if name == 'launch-preferences': arguments = [ROOT]
    run([binary, *arguments])
    return True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--portable-only', action='store_true')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--only', help='comma-separated C, controller, Metal or Swift check names; runs just those')
    args = parser.parse_args()
    only = set(args.only.split(',')) if args.only else None
    if not only:
        run([sys.executable, ROOT / 'tools/check_repository_hygiene.py'])
        run([sys.executable, ROOT / 'tools/test_repository_hygiene.py'])
        run([sys.executable, ROOT / 'tools/check_probe_runner_exit.py'])
        run([sys.executable, ROOT / 'tools/test_setup_halo.py'])
        run([sys.executable, ROOT / 'tools/test_visual_assets.py'])
        run([sys.executable, ROOT / 'tools/test_censhine_import.py'])
        run([sys.executable, ROOT / 'tools/test_engine_vision_report_summary.py'])
        run([sys.executable, ROOT / 'tools/test_watch_engine_vision_telemetry.py'])
        run([sys.executable, ROOT / 'tools/test_build_engine_ipad.py'])
        if platform.system() == 'Darwin' and not args.portable_only:
            run([sys.executable, ROOT / 'tools/test_mobile_cloud_saves.py'])
        run([sys.executable, HOST / 'tests/test_dispatch_interest.py'])
        run([sys.executable, ROOT / 'tools/generate_smaa_msl.py', '--check'])
    cases = [(name, []) for name in ('frame_pacer', 'panorama_budget', 'pointer_step', 'pointer_input', 'panorama_epoch', 'panorama_gpu_carry', 'panorama_lifecycle', 'panorama_pose_export', 'panorama_camera_cut', 'panorama_motion_budget', 'panorama_graphics', 'texture_mips', 'dinput_lifecycle', 'winsock', 'multiplayer_update', 'x87_nearest_fast_path', 'engine_direct_calls', 'controller_link_guard')]
    cases = [(name, [HOST / 'halo_settings.c'] if name == 'dinput_lifecycle' else extra) for name, extra in cases]
    cases.append(('mobile_graphics', ['-std=c11', '-Wall', '-Wextra', '-Werror']))
    cases.append(('frame_limit_routing', ['-DEXPECT_ROUTED=0']))
    cases.append(('input_prompt', [*STRICT, '-fno-sanitize-recover=undefined']))
    cases.append(('sense_prompt_context', ['-fno-sanitize-recover=undefined']))
    cases.append(('sense_pose', []))
    cases.append(('sense_head', []))
    cases.append(('sense_gestures', []))
    cases.append(('sense_physical_input', []))
    cases.append(('sense_throw', []))
    cases.append(('sense_grip_continuity', []))
    cases.append(('sense_scope_smoothing', []))
    cases.append(('sense_mount_input', []))
    # Production weapon hooks with mocked original calls; no generated source.
    cases.append(('sense_weapon_continuity', ['-w']))
    cases.append(('dual_input', []))
    cases.append(('reticle_art_state', []))
    for name in ('panorama_reentry', 'panorama_projection_hook', 'panorama_letterbox_boundary', 'panorama_lod', 'panorama_interface_record', 'halo_settings_layer_align', 'halo_settings_fov', 'halo_graphics_settings'):
        cases.append((name, [HOST / 'halo_settings.c']))
    for fast in (0, 1):
        cases.append(('x87_rotating_stack', [HOST / 'tests/x87_stack_shifting.c',
                      f'-DHALO_ARM64_FENV_FAST={fast}', '-frounding-math', '-ffp-contract=off']))
    generated = generated_engine()
    # Original time-update fixtures are optional; both routing variants compile
    # and print SKIP without generated sources. Never embed a private path.
    for ios in (0, 1):
        cases.append(('frame_limit_original', ['-w', f'-DENGINE_IOS_HOOKS={ios}',
                      '-DHALO_ARM64_FENV_FAST=1', '-frounding-math', '-ffp-contract=off',
                      *(['-I', generated] if generated else [])]))
    # The original prompt formatter is optional in the public source checkout.
    cases.append(('input_prompt_original', ['-w', '-DHALO_ARM64_FENV_FAST=1',
                  '-frounding-math', '-ffp-contract=off', '-fno-sanitize-recover=undefined',
                  *(['-I', generated] if generated else [])]))
    if generated:
        for name in ("sense_weapon_original", "sense_weapon_charge_original", "sense_physical_original", "sense_mount_original"):
            cases.append((name, ["-I", generated, "-DHALO_ARM64_FENV_FAST=1", "-frounding-math", "-ffp-contract=off"]))
        # The grenade mount gate uses sense_mount.h when it is present; keep the standalone fallback covered too.
        cases.append(("sense_weapon_original", ["-I", generated, "-DHALO_ARM64_FENV_FAST=1", "-frounding-math", "-ffp-contract=off",
                                                "-DHALO_SENSE_GRENADE_LOCAL_MOUNT=1"]))
        # Tracked body: with the mocked accepted-pose API and pickup lane, then standalone.
        for variant in ([], ["-DBODY_TEST_API=0", "-DBODY_TEST_PICKUP=0"]):
            cases.append(("sense_body_original", ["-I", generated, "-DHALO_ARM64_FENV_FAST=1", "-frounding-math", "-ffp-contract=off", *variant]))
    else:
        print("SKIP: sense_weapon_original, sense_weapon_charge_original, sense_physical_original and sense_mount_original require locally generated verified engine sources", flush=True)
        print("SKIP: sense_body_original requires locally generated verified engine sources", flush=True)
    cases.append(('mixer_quality', [HOST / 'directsound_mixer.c', HOST / 'halo_settings.c']))
    cases.append(('texture_mod_pack', []))
    cases.append(('shader_mod_pack', []))
    cases.append(('d3d9_texture_mod', [HOST / 'texture_decode.c']))
    # Differential test against the generated engine (SKIP without it), built as the headset builds the engine.
    cases.append(('visible_surfaces', ['-DHALO_ARM64_FENV_FAST=1', '-frounding-math', '-ffp-contract=off']))
    cases.append(('radial_fog_settings', [HOST / 'halo_settings.c']))
    mojo = ROOT / 'third_party/mojoshader'
    mojo_sources = [HOST / 'metalshader.c', mojo / 'mojoshader.c', mojo / 'mojoshader_common.c',
                    mojo / 'profiles/mojoshader_profile_common.c', mojo / 'profiles/mojoshader_profile_metal.c']
    mojo_flags = ['-DMOJOSHADER_NO_VERSION_INCLUDE=1', '-I', mojo,
                  *(f'-DSUPPORT_PROFILE_{p}=0' for p in ('D3D', 'BYTECODE', 'HLSL', 'GLSL120', 'GLSLES', 'GLSLES3',
                                                       'GLSL', 'ARB1', 'ARB1_NV', 'SPIRV', 'GLSPIRV'))]
    cases.append(('radial_fog', mojo_sources))
    mac = platform.system() == 'Darwin' and not args.portable_only
    # Core telemetry: frame arithmetic, and Halo's own translated tick driver
    # against the game-time counter it reads (SKIP without the translation);
    # on the Mac, guest wait timing and the lifetime pass total too.
    cases += [('core_telemetry', []), ('game_time_telemetry', ['-DHALO_ARM64_FENV_FAST=1', '-frounding-math', '-ffp-contract=off',
                                                               *(['-I', generated] if generated else [])])]
    if mac:
        # These include Darwin host/renderer code and require Apple's linker.
        cases += [('guest_page_try', []), ('device_messages', ['-Wno-deprecated-declarations'])]
        for mobile in (0, 1):
            cases += [(name, [f'-DHALO_D3D9_MOBILE_POLICY={mobile}'])
                      for name in ('d3d9_transaction', 'd3d9_admission')]
        cases += [('thread_wait_telemetry', []), ('pass_time_telemetry', ['-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip'])]
        cases += [('haptics_mailbox', []), ('audio_watchdog', [HOST / 'directsound_mixer.c']),
                  ('directsound_lifetime', [HOST / 'directsound_mixer.c']),
                  ('directsound_voice_reuse', [HOST / 'directsound_mixer.c']),
                  ('guest_pages', []), ('win32_save_flush', []), ('d3d9_reset', []),
                  ('vb_resident', []), ('draw_indices', []), ('d3d9_radial_fog', []), ('win32_sleep_accounting', [])]
        cases.append(('frame_pacing_host', [HOST / 'halo_settings.c']))
        cases += [(name, []) for name in ('guest_heap', 'win32_heap_reuse', 'd3d9_resource_lifetime', 'd3d9_texture_lifetime',
                                          'thread_cancellation', 'win32_apc_queue', 'win32_tls_lifetime')]
        cases.append(('guest_heap_sound_owners', [HOST / 'directsound_mixer.c']))
        cases += [('guest_thread_qos', []), ('cache_read_wait', [])]
        for name in ('haptics_detector', 'haptics_onset', 'directsound_mixer'):
            extra = [HOST / 'haptics.c', HOST / 'halo_settings.c']
            if name != 'haptics_detector': extra.append(HOST / 'directsound_mixer.c')
            cases.append((name, extra))
        cases.append(('weapon_haptics', [HOST / 'haptics.c', HOST / 'halo_settings.c']))
    if only:
        known = ({name for name, _ in cases} | {check[0] for check in CONTROLLER_CHECKS} |
                 set(SWIFT_CHECKS) | set(METAL_CHECKS) | {'frame-limit-sdk-routing'})
        if only - known: parser.error('unknown or unavailable checks: ' + ', '.join(sorted(only - known)))
        cases = [case for case in cases if case[0] in only]
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='halo-source-checks-') as temp:
        skipped_cases = set()
        for name, extra in cases:
            binary = Path(temp) / name
            flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
            command = [os.environ.get('CC', 'clang'), *flags, '-pthread', '-DENGINE_FLAT_MEMORY=1', '-I', HOST,
                       '-I', ROOT / 'native/EngineReuse', HOST / 'tests' / f'test_{name}.c', *extra, '-lm']
            # Optional comparison with the original no-update callback; SKIP without it.
            if name == 'multiplayer_update' and generated: command += ['-I', generated]
            if name == 'radial_fog':
                command += ['-w', '-frounding-math', '-ffp-contract=off', *mojo_flags]
                # Optional read-only evidence from the local translated engine.
                radial_generated = os.environ.get('HALO_RADIAL_FOG_GENERATED_DIR') or generated
                if radial_generated:
                    command += ['-DRF_HAVE_ENGINE=1', '-I', Path(radial_generated).expanduser().resolve()]
            # Compares against translated engine code, so build it the way the engine is built.
            if name in ('panorama_lod', 'visible_surfaces'):
                command += ['-frounding-math', '-ffp-contract=off', *(['-I', generated] if generated else [])]
            if name in ('audio_watchdog', 'directsound_lifetime', 'directsound_voice_reuse', 'guest_heap_sound_owners'): command += ['-framework', 'AudioToolbox']
            if name == 'panorama_interface_record':
                # Lifted code, built as the engine is; SKIP without the sources.
                command += ['-DHALO_ARM64_FENV_FAST=1', '-frounding-math', '-ffp-contract=off']
                if generated: command += ['-I', generated]
            if name in ('guest_pages', 'guest_page_try', 'device_messages', 'd3d9_transaction', 'd3d9_admission',
                        'win32_save_flush', 'd3d9_reset', 'guest_heap', 'win32_heap_reuse',
                        'd3d9_resource_lifetime', 'd3d9_texture_lifetime', 'd3d9_texture_mod', 'guest_heap_sound_owners', 'win32_apc_queue', 'win32_tls_lifetime',
                        'vb_resident', 'draw_indices', 'd3d9_radial_fog', 'win32_sleep_accounting'):
                command += ['-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip']
            if name == 'cache_read_wait': command += ['-ffunction-sections', '-fdata-sections', '-Wl,-dead_strip']
            run([*command, '-o', binary])
            if name in ('frame_limit_original', 'input_prompt_original'):
                result = run([binary], capture_output=True)
                if any(line.startswith('SKIP:') for line in result.stdout.splitlines()): skipped_cases.add(name)
            else:
                run([binary])
        if only:
            passed = len({name for name, _ in cases} - skipped_cases)
            skipped = sorted(skipped_cases)
            for check in CONTROLLER_CHECKS:
                if check[0] in only:
                    if run_controller_check(temp, flags, *check): passed += 1
                    else: skipped.append(check[0])
            for name in METAL_CHECKS:
                if name in only:
                    if mac and run_metal_check(temp, flags, name): passed += 1
                    else: skipped.append(name)
            for name in SWIFT_CHECKS:
                if name in only:
                    if run_swift_check(temp, name, args.sanitize): passed += 1
                    else: skipped.append(name)
            if 'frame-limit-sdk-routing' in only:
                if mac and run_frame_limit_sdk_check(temp): passed += 1
                else: skipped.append('frame-limit-sdk-routing')
            print(f'PASS {passed} of {len(only)} selected checks' +
                  (f'; SKIPPED (not passed): {", ".join(skipped)}' if skipped else '') +
                  '; no headset validation implied')
            return
        # Runtime ABI remains logical ST(i), regardless of the internal ring.
        for engine_dir in ('EngineReuse',):
            binary = Path(temp) / ('runtime-contract-' + engine_dir.replace('/', '-'))
            run([os.environ.get('CC', 'clang'), *flags, '-DENGINE_STEP_FULL=1',
                 '-DHALO_STAGED_ENGINE=1' if engine_dir.startswith('HaloVision') else '-DHALO_STAGED_ENGINE=0',
                 ROOT / 'native/Tests/EngineRuntimeContract.c',
                 ROOT / 'native' / engine_dir / 'engine_runtime.c', '-lm', '-o', binary])
            run([binary])
        run([sys.executable, ROOT / 'tools/benchmark_x87_rotating_stack.py', '--check-only', '--allow-missing',
             *(['--generated', generated] if generated else []), *(['--sanitize'] if args.sanitize else [])])
        # Native leaves against the translated functions, with the release
        # flags and with the headset's (-frounding-math, the FPSR helper).
        engine = generated_engine()
        for variant, extra in (('native_leaves', []), ('native_leaves_headset_flags', ['-frounding-math', '-ffp-contract=off', '-DHALO_ARM64_FENV_FAST=1'])):
            binary = Path(temp) / variant
            flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
            run([os.environ.get('CC', 'clang'), *flags, *extra, '-DENGINE_FLAT_MEMORY=1', '-I', HOST, '-I', ROOT / 'native/EngineReuse',
                 *(['-I', engine] if engine else []), HOST / 'tests/test_native_leaves.c', '-lm', '-o', binary])
            run([binary])
        # Exact gather loops and the complete translated light/shadow pipeline.
        for name in ('native_gather', 'gather_pipeline'):
            binary = Path(temp) / name
            run([os.environ.get('CC', 'clang'), *flags, '-DENGINE_FLAT_MEMORY=1', '-DHALO_ARM64_FENV_FAST=1',
                 '-frounding-math', '-ffp-contract=off', '-I', HOST, '-I', ROOT / 'native/EngineReuse',
                 *(['-I', engine] if engine else []), HOST / 'tests' / f'test_{name}.c', '-lm', '-o', binary])
            run([binary])
        if mac:
            binary = Path(temp) / 'gather-diagnostics'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 '-import-objc-header', ROOT / 'native/EngineVision/Sources/EngineDiagnosticsBridge.h',
                 ROOT / 'native/EngineVision/Sources/EngineGatherDiagnostics.swift',
                 ROOT / 'native/EngineVision/Tests/GatherDiagnosticsValidation.swift', '-o', binary])
            run([binary])
            # The voice-reuse check again on the original allocator itself
            # (translated 005482E0 and 00547FF0), when a translation is here.
            # Read only; HALO_ENGINE_TRANSLATION names another directory.
            translation = Path(os.environ['HALO_ENGINE_TRANSLATION']) if os.environ.get('HALO_ENGINE_TRANSLATION') else None
            if translation is None:
                found = sorted((ROOT / 'native/build/engine-reuse').glob('*/sub_005482E0.c'), key=lambda f: f.stat().st_mtime)
                translation = found[-1].parent if found else None
            if translation and (translation / 'sub_005482E0.c').exists() and (translation / 'sub_00547FF0.c').exists():
                binary = Path(temp) / 'directsound_voice_reuse_translated'
                run([os.environ.get('CC', 'clang'), *flags, '-pthread', '-DENGINE_FLAT_MEMORY=1', '-DHALO_TRANSLATED_ALLOCATOR',
                     '-I', HOST, '-I', ROOT / 'native/EngineReuse', '-I', translation,
                     HOST / 'tests/test_directsound_voice_reuse.c', HOST / 'directsound_mixer.c',
                     '-framework', 'AudioToolbox', '-lm', '-o', binary])
                run([binary])
            else:
                print('SKIP voice reuse on the translated allocator: no engine translation found', flush=True)
            run([sys.executable, ROOT / 'tools/check_core_telemetry_xros.py'])
            binary = Path(temp) / 'core-telemetry-json'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 '-import-objc-header', ROOT / 'native/EngineVision/Sources/EngineVisionBridge.h',
                 '-Xcc', f'-I{HOST}',
                 ROOT / 'native/EngineVision/Sources/EngineCoreTelemetry.swift',
                 ROOT / 'native/EngineVision/Tests/CoreTelemetryValidation.swift', '-o', binary])
            run([binary])
            for check in CONTROLLER_CHECKS:
                run_controller_check(temp, flags, *check)
            binary = Path(temp) / 'metal-overlay-cache'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 HOST / 'tests/test_metalrenderer_overlay_cache.m', HOST / 'halo_settings.c', '-framework', 'Foundation',
                 '-framework', 'Metal', '-o', binary])
            run([binary])
            # Both draw paths on real Metal: exact framebuffer comparisons and CPU timings.
            binary = Path(temp) / 'metal-draw-fastpaths'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 HOST / 'tests/test_metalrenderer_fastpaths.m', '-framework', 'Foundation',
                 '-framework', 'Metal', '-o', binary])
            run([binary])
            binary = Path(temp) / 'reticle-art-crop'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 HOST / 'tests/test_reticle_art_crop.m', '-framework', 'Foundation',
                 '-framework', 'Metal', '-o', binary])
            run([binary])
            binary = Path(temp) / 'metal-texture-bindings'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 HOST / 'tests/test_metalrenderer_texture_bindings.m', '-framework', 'Foundation',
                 '-framework', 'Metal', '-o', binary])
            run([binary])
            binary = Path(temp) / 'metal-state-pressure'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 HOST / 'tests/test_metalrenderer_state_pressure.m', '-framework', 'Foundation',
                 '-framework', 'Metal', '-o', binary])
            run([binary])
            # Graphics improvements on the Mac GPU: timing (anchored clock,
            # admission, categories), engine-pass foveation against unmapped
            # references, the panorama rate maps under Metal's own mapping,
            # and publication (HUD raster, SMAA/FXAA, MetalFX/bilinear).
            for name in METAL_CHECKS:
                run_metal_check(temp, flags, name)
            binary = Path(temp) / 'audio-session-recovery'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks',
                 ROOT / 'native/EngineVision/Tests/AudioSessionRecoveryValidation.m',
                 '-framework', 'Foundation', '-o', binary])
            run([binary])
            # Real Metal and MojoShader with synthetic shaders; two sessions in a temporary cache directory.
            binary = Path(temp) / 'metal-pipeline-cache'
            mojo = ROOT / 'third_party/mojoshader'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', '-w', '-DMOJOSHADER_NO_VERSION_INCLUDE=1',
                 *(f'-DSUPPORT_PROFILE_{p}=0' for p in ('D3D', 'BYTECODE', 'HLSL', 'GLSL120', 'GLSLES', 'GLSLES3', 'GLSL',
                                                         'ARB1', 'ARB1_NV', 'SPIRV', 'GLSPIRV')),
                 '-I', mojo, HOST / 'tests/test_metalrenderer_pipeline_cache.m', HOST / 'metalshader.c', HOST / 'halo_settings.c',
                 mojo / 'mojoshader.c', mojo / 'mojoshader_common.c', mojo / 'profiles/mojoshader_profile_common.c',
                 mojo / 'profiles/mojoshader_profile_metal.c', '-framework', 'Foundation', '-framework', 'Metal', '-o', binary])
            run([binary])
            binary = Path(temp) / 'radial-fog-renderer'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', '-w', *mojo_flags,
                 HOST / 'tests/test_radial_fog_renderer.m', *mojo_sources, HOST / 'halo_settings.c',
                 '-framework', 'Foundation', '-framework', 'Metal', '-o', binary])
            run([binary])
            binary = Path(temp) / 'immersive-ownership'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOwnership.swift',
                 ROOT / 'native/EngineVision/Tests/ImmersiveOwnershipValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'runtime-scope-frame'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', '-DENGINE_FLAT_MEMORY=1',
                 '-I', HOST, '-I', ROOT / 'native/EngineReuse', '-ffunction-sections', '-fdata-sections',
                 ROOT / 'native/EngineVision/Tests/RuntimeScopeFrameValidation.m',
                 '-framework', 'Foundation', '-framework', 'Metal', '-Wl,-dead_strip', '-o', binary])
            run([binary])
            # Compile the production immutable texture class with deterministic
            # frame info/copy providers; real Metal tests an in-flight read.
            frame_main = Path(temp) / 'main.swift'
            frame_main.write_bytes((ROOT / 'native/EngineVision/Tests/FrameTextureValidation.swift').read_bytes())
            binary = Path(temp) / 'frame-texture'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineFrameTexture.swift', frame_main, '-o', binary])
            run([binary])
            binary = Path(temp) / 'panorama-publication'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', '-DENGINE_FLAT_MEMORY=1',
                 '-I', HOST, '-I', ROOT / 'native/EngineReuse', '-ffunction-sections', '-fdata-sections',
                 ROOT / 'native/EngineVision/Tests/PanoramaPublicationValidation.m',
                 '-framework', 'Foundation', '-framework', 'Metal', '-Wl,-dead_strip', '-o', binary])
            run([binary])
            binary = Path(temp) / 'core-telemetry-collection'
            run([os.environ.get('CC', 'clang'), *flags, '-fobjc-arc', '-fblocks', '-DENGINE_FLAT_MEMORY=1',
                 '-I', HOST, '-I', ROOT / 'native/EngineReuse', '-ffunction-sections', '-fdata-sections',
                 ROOT / 'native/EngineVision/Tests/CoreTelemetryCollectionValidation.m',
                 '-framework', 'Foundation', '-framework', 'Metal', '-framework', 'QuartzCore',
                 '-Wl,-dead_strip', '-o', binary])
            run([binary])
            binary = Path(temp) / 'world-cadence'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineWorldCadence.swift',
                 ROOT / 'native/EngineVision/Tests/WorldCadenceValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'menu-input'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineMenuInput.swift',
                 ROOT / 'native/EngineVision/Tests/MenuInputValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'controller-navigation'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineControllerNavigation.swift',
                 ROOT / 'native/EngineVision/Tests/ControllerNavigationValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'sense-pose'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSensePose.swift',
                 ROOT / 'native/EngineVision/Tests/SensePoseValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'sense-locomotion'
            status_binary = Path(temp) / 'sense-tracking-status'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineSenseTrackingStatus.swift',
                 ROOT / 'native/EngineVision/Tests/SenseTrackingStatusValidation.swift', '-o', status_binary])
            run([status_binary])
            reticle_binary = Path(temp) / 'sense-reticle'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveScreenGeometry.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSenseReticleGeometry.swift',
                 ROOT / 'native/EngineVision/Tests/SenseReticleValidation.swift', '-o', reticle_binary])
            run([reticle_binary])
            reticle_gpu_binary = Path(temp) / 'sense-reticle-gpu'
            run(['swiftc', '-parse-as-library', ROOT / 'native/EngineVision/Tests/SenseReticleGPUValidation.swift', '-o', reticle_gpu_binary])
            run([reticle_gpu_binary, ROOT / 'native/EngineVision/Sources/EngineImmersive.swift'])
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSensePose.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSenseLocomotion.swift',
                 ROOT / 'native/EngineVision/Tests/SenseLocomotionValidation.swift', '-o', binary])
            run([binary])
            point_turn_binary = Path(temp) / 'sense-point-turn'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSensePointTurnGeometry.swift',
                 ROOT / 'native/EngineVision/Tests/SensePointTurnGeometryValidation.swift', '-o', point_turn_binary])
            run([point_turn_binary])
            scope_binary = Path(temp) / 'sense-scope-presentation'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveScreenGeometry.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSenseScopePresentation.swift',
                 ROOT / 'native/EngineVision/Tests/SenseScopePresentationValidation.swift', '-o', scope_binary])
            run([scope_binary])
            hud_pose_binary = Path(temp) / 'immersive-hud-pose'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveHUDPose.swift',
                 ROOT / 'native/EngineVision/Tests/ImmersiveHUDPoseValidation.swift', '-o', hud_pose_binary])
            run([hud_pose_binary])
            gestures_binary = Path(temp) / 'sense-gestures'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSensePose.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSenseGestures.swift',
                 ROOT / 'native/EngineVision/Tests/SenseGesturesValidation.swift', '-o', gestures_binary])
            run([gestures_binary])
            throw_binary = Path(temp) / 'sense-throw-motion'
            run(['swiftc', '-parse-as-library', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineImmersiveOrientation.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSensePose.swift',
                 ROOT / 'native/EngineVision/Sources/EngineSenseThrowMotion.swift',
                 ROOT / 'native/EngineVision/Tests/SenseThrowMotionValidation.swift', '-o', throw_binary])
            run([throw_binary])
            run_swift_check(temp, 'sense-settings')
            run_swift_check(temp, 'sense-seat-presentation')
            run_swift_check(temp, 'sense-physical-settings')
            binary = Path(temp) / 'deep-telemetry'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineDeepTelemetry.swift',
                 ROOT / 'native/EngineVision/Tests/DeepTelemetryValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'diagnostic-history'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineDiagnosticHistory.swift',
                 ROOT / 'native/EngineVision/Tests/DiagnosticHistoryValidation.swift', '-o', binary])
            run([binary])
            binary = Path(temp) / 'diagnostic-report-writer'
            run(['swiftc', '-module-cache-path', Path(temp) / 'swift-cache',
                 ROOT / 'native/EngineVision/Sources/EngineDiagnosticHistory.swift',
                 ROOT / 'native/EngineVision/Sources/EngineDiagnosticReportWriter.swift',
                 ROOT / 'native/EngineVision/Tests/DiagnosticReportWriterValidation.swift', '-o', binary])
            run([binary])
            # Layer alignment: the turns against the host's own bearings, then
            # the shipped shader on the Mac's GPU. Optimised: the sphere-wide
            # coverage sweeps are too slow unoptimised.
            alignment = [ROOT / 'native/EngineVision/Sources/EngineImmersiveScreenGeometry.swift',
                         ROOT / 'native/EngineVision/Sources/EngineLayerAlignment.swift']
            binary = Path(temp) / 'layer-alignment'
            run(['swiftc', '-O', '-module-cache-path', Path(temp) / 'swift-cache', *alignment,
                 ROOT / 'native/EngineVision/Tests/LayerAlignmentValidation.swift', '-o', binary])
            run([binary])
            run_swift_check(temp, 'foveation-geometry')
            run_swift_check(temp, 'graphics-preferences')
            run_swift_check(temp, 'launch-preferences', args.sanitize)
            run_frame_limit_sdk_check(temp)
            run_swift_check(temp, 'pad-input-policy', args.sanitize)
            run_swift_check(temp, 'pad-input-session', args.sanitize)
            binary = Path(temp) / 'layer-alignment-gpu'
            run(['swiftc', '-O', '-module-cache-path', Path(temp) / 'swift-cache', *alignment,
                 ROOT / 'native/EngineVision/Tests/LayerAlignmentGPUValidation.swift', '-o', binary])
            run([binary, ROOT / 'native/EngineVision/Sources/EngineImmersive.swift'])
    passed_cases = sum(name not in skipped_cases for name, _ in cases)
    print(f'PASS {passed_cases} C checks and more' + (' plus mock Objective-C, Metal and Swift checks' if mac else '') +
          (f'; SKIPPED (not passed): {", ".join(sorted(skipped_cases))}' if skipped_cases else '') +
          '; no headset validation implied')

if __name__ == '__main__':
    main()
