#!/usr/bin/env python3
"""Regression coverage for incremental translated-C include invalidation."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import plistlib

import build_engine_ipad
from build_engine_ipad import (local_input_inventory, native_fingerprint, changed_inputs, authorized_capabilities,
                               project_versions, validate_versions, require_writable_output, bundle_info,
                               uncovered_devices, compose_info, validate_display_name, build_parser)

REPO = Path(__file__).resolve().parents[1]
PAD = REPO / "native/EnginePad"


class CapabilityTests(unittest.TestCase):
    def test_memory_only_profile_cannot_silently_drop_cloud_saves(self):
        memory = "com.apple.developer.kernel.increased-memory-limit"
        cloud = "com.apple.developer.ubiquity-container-identifiers"
        with self.assertRaisesRegex(RuntimeError, cloud):
            authorized_capabilities({memory: True}, {
                memory: True, cloud: ["iCloud.com.example.game"]
            })

    def test_unrelated_cloud_container_is_rejected(self):
        cloud = "com.apple.developer.icloud-container-identifiers"
        with self.assertRaisesRegex(RuntimeError, cloud):
            authorized_capabilities({cloud: ["iCloud.com.example.other"]}, {
                cloud: ["iCloud.com.example.game"]
            })

    def test_requested_cloud_and_memory_entitlements_are_retained(self):
        requested = {
            "com.apple.developer.kernel.increased-memory-limit": True,
            "com.apple.developer.icloud-container-identifiers": ["iCloud.com.example.game"],
            "com.apple.developer.icloud-services": ["CloudDocuments"],
        }
        self.assertEqual(authorized_capabilities(requested, requested), requested)

    def test_apple_scalar_icloud_services_wildcard_retains_concrete_request(self):
        key = "com.apple.developer.icloud-services"
        requested = {key: ["CloudDocuments"]}
        self.assertEqual(authorized_capabilities({key: "*"}, requested), requested)

    def test_scalar_service_wildcard_does_not_grant_other_capabilities(self):
        for key, value in [("com.apple.developer.icloud-container-identifiers", ["iCloud.com.example.game"]),
                           ("com.apple.developer.kernel.increased-memory-limit", True)]:
            with self.assertRaisesRegex(RuntimeError, key):
                authorized_capabilities({key: "*"}, {key: value})

    def test_missing_or_wrong_icloud_service_is_rejected(self):
        key = "com.apple.developer.icloud-services"
        for permission in (None, "CloudDocuments", ["CloudKit"], []):
            with self.assertRaisesRegex(RuntimeError, key):
                authorized_capabilities({key: permission}, {key: ["CloudDocuments"]})


class MobileIdentityTests(unittest.TestCase):
    """Existing identity, shared container and migration gate stay fixed."""

    def test_identity_and_container_are_unchanged(self):
        project = (PAD / "project.yml").read_text()
        self.assertIn("PRODUCT_BUNDLE_IDENTIFIER: com.zephyrstudios.mastercheef.ipad", project)
        entitlements = plistlib.loads((PAD / "EnginePad.entitlements").read_bytes())
        container = "iCloud.com.zephyrstudios.mastercheef"
        for key in ("com.apple.developer.icloud-container-identifiers", "com.apple.developer.ubiquity-container-identifiers"):
            self.assertEqual(entitlements[key], [container])
        info = plistlib.loads((PAD / "Info.plist").read_bytes())
        self.assertEqual(info["MasterCheefCloudContainer"], container)
        self.assertIn(container, info["NSUbiquitousContainers"])
        # The iCloud Drive folder name is part of the shared container's presentation.
        self.assertEqual(info["NSUbiquitousContainers"][container]["NSUbiquitousContainerName"], "Master Cheef")

    def test_public_source_default_does_not_require_a_vision_save(self):
        info = plistlib.loads((PAD / "Info.plist").read_bytes())
        self.assertIs(info["MasterCheefRequireVisionSeed"], False)
        self.assertFalse(build_parser().parse_args([]).require_vision_seed)

    def compose(self, app_info, **overrides):
        options = dict(bundle_id="com.zephyrstudios.mastercheef.ipad", marketing="1.0.22", build="122",
                       display_name="117", require_vision_seed=False)
        options.update(overrides)
        return compose_info(app_info, **options)

    def test_packaging_choices_win_after_info_plist_merge(self):
        source = plistlib.loads((PAD / "Info.plist").read_bytes())
        public = self.compose(source)
        self.assertIs(public["MasterCheefRequireVisionSeed"], False)
        migration = self.compose(source, require_vision_seed=True)
        self.assertIs(migration["MasterCheefRequireVisionSeed"], True)
        # Even a stale/hostile source plist cannot override explicit packaging.
        hostile = dict(source, MasterCheefRequireVisionSeed=True, CFBundleDisplayName="Old",
                       CFBundleIdentifier="com.example.other", CFBundleVersion="108")
        merged = self.compose(hostile)
        self.assertIs(merged["MasterCheefRequireVisionSeed"], False)
        self.assertEqual((merged["CFBundleDisplayName"], merged["CFBundleIdentifier"], merged["CFBundleVersion"]),
                         ("117", "com.zephyrstudios.mastercheef.ipad", "122"))
        self.assertTrue(self.compose(dict(source, MasterCheefRequireVisionSeed=False),
                                     require_vision_seed=True)["MasterCheefRequireVisionSeed"])

    def test_display_name_is_visible_only(self):
        args = build_parser().parse_args([])
        self.assertEqual(args.display_name, "117")
        info = self.compose(plistlib.loads((PAD / "Info.plist").read_bytes()), display_name="  Final Name ")
        self.assertEqual(info["CFBundleDisplayName"], "Final Name")
        self.assertEqual((info["CFBundleName"], info["CFBundleExecutable"]), ("MasterCheef", "MasterCheef"))
        self.assertEqual(info["MasterCheefCloudContainer"], "iCloud.com.zephyrstudios.mastercheef")
        for bad in ["", "   ", "a\nb", "x" * 65]:
            with self.assertRaises(RuntimeError):
                validate_display_name(bad)
        self.assertIn('INFOPLIST_KEY_CFBundleDisplayName: "117"', (PAD / "project.yml").read_text())

    def test_bundle_targets_iphone_and_ipad_with_one_identity(self):
        info = self.compose(plistlib.loads((PAD / "Info.plist").read_bytes()))
        self.assertEqual(info["UIDeviceFamily"], [1, 2])
        self.assertEqual(info["CFBundleIdentifier"], "com.zephyrstudios.mastercheef.ipad")
        self.assertEqual((info["CFBundleShortVersionString"], info["CFBundleVersion"]), ("1.0.22", "122"))
        self.assertEqual(bundle_info("x", "1", "2")["UIDeviceFamily"], [1, 2])
        self.assertIn('TARGETED_DEVICE_FAMILY: "1,2"', (PAD / "project.yml").read_text())
        source = Path(build_engine_ipad.__file__).read_text()
        self.assertIn('"--target-device", "iphone", "--target-device", "ipad"', source)

    def test_versions_follow_xcode_target_and_reject_malformed_overrides(self):
        marketing, build = project_versions()
        self.assertEqual((marketing, build), ("1.0.23", "123"))
        validate_versions(marketing, build)
        args = build_parser().parse_args(["--build-number", "122", "--marketing-version", "1.0.22"])
        self.assertEqual((args.build_number, args.marketing_version), ("122", "1.0.22"))
        validate_versions("1.2.1", "121")
        for bad in [("1.0.9", "109a"), ("v1", "109"), ("1.0.9", "")]:
            with self.assertRaises(RuntimeError):
                validate_versions(*bad)

    def test_historical_evidence_folder_is_never_an_output(self):
        historical = build_engine_ipad.PROTECTED_OUTPUTS[0].resolve()
        for output in (historical, historical / "direct-build", historical / "candidate"):
            with self.assertRaisesRegex(RuntimeError, "historical evidence"):
                require_writable_output(output)
        require_writable_output(build_engine_ipad.DEFAULT_OUTPUT.resolve())
        self.assertNotIn(historical, [build_engine_ipad.DEFAULT_OUTPUT.resolve(), *build_engine_ipad.DEFAULT_OUTPUT.resolve().parents])

    def test_profile_must_cover_every_requested_device(self):
        profile = {"ProvisionedDevices": ["IPAD"]}
        self.assertEqual(uncovered_devices(profile, ["IPAD"]), [])
        self.assertEqual(uncovered_devices(profile, ["IPAD", "IPHONE"]), ["IPHONE"])
        self.assertEqual(uncovered_devices({"ProvisionsAllDevices": True}, ["IPHONE"]), [])

    def test_launcher_reconciles_only_before_halo_starts(self):
        app = (PAD / "Sources/MasterCheefPadApp.swift").read_text()
        self.assertEqual(app.count("cloudSaves.sync("), 1, "every launcher sync goes through syncIfIdle")
        idle = app[app.index("private func syncIfIdle()"):]
        idle = idle[:idle.index("\n    }\n")]
        self.assertIn("enginevision_runtime_state() == ENGINEVISION_IDLE", idle)
        self.assertIn("bindRuntimeGate()", idle)
        self.assertLess(app.index("cloudSaves.stopCloudMonitoring()"), app.index("engine.start(root:"))
        self.assertIn(".onReceive(cloudSaves.cloudChanges)", app)

    def test_post_game_guidance_precedes_controller_hint(self):
        app = (PAD / "Sources/MasterCheefPadApp.swift").read_text()
        self.assertIn('reopenToSync = "Close and reopen to sync this session"', app)
        notice = app[app.index("private var shelfNotice"):]
        self.assertLess(notice.index("engine.state != ENGINEVISION_IDLE"), notice.index("engine.controllerName == nil"))
        self.assertIn("EnginePadBrand.reopenToSync", notice[:notice.index("engine.controllerName == nil")])
        view = (REPO / "native/EngineVision/Sources/EngineCloudSavesView.swift").read_text()
        self.assertIn("Close and reopen to sync this session", view)
        self.assertIn("there is no live or checkpoint sync", view)

    def test_launcher_leases_controller_input_only_for_app_ui(self):
        app = (PAD / "Sources/MasterCheefPadApp.swift").read_text()
        model = (PAD / "Sources/EnginePadModel.swift").read_text()
        self.assertIn(".onChange(of: uiOwnsInput, initial: true) { _, owned in engine.uiOwnsInput = owned }", app)
        self.assertIn("gameVisible: showingGame", app)
        for call in ("hostgc_app_input_begin()", "hostgc_app_input_renew($0)", "hostgc_app_input_end($0)"):
            self.assertIn(call, model)
        refresh = model[model.index("func refresh()"):]
        self.assertIn("inputLease.update(owned: uiOwnsInput)", refresh)
        self.assertIn("inputLease.release()", model[model.index("deinit"):])


class NativeInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ipad-build-inputs-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def put(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def inputs(self, source):
        return local_input_inventory(source, [self.root], root=self.root)

    def test_changed_generated_body_invalidates_only_its_chunk(self):
        a = self.put("chunk_a.c", '#include "leaf_a.c"\n')
        b = self.put("chunk_b.c", '#include "leaf_b.c"\n')
        self.put("leaf_a.c", "int value_a(void) { return 17; }\n")
        self.put("leaf_b.c", "int value_b(void) { return 31; }\n")
        before_a, before_b = self.inputs(a), self.inputs(b)
        command = ["clang", "-c", str(a)]
        prior = native_fingerprint(command, "common", before_a)
        self.put("leaf_a.c", "int value_a(void) { return 41; }\n")
        after_a, after_b = self.inputs(a), self.inputs(b)
        self.assertEqual(before_a["chunk_a.c"], after_a["chunk_a.c"])
        self.assertNotEqual(prior, native_fingerprint(command, "common", after_a))
        self.assertEqual(before_b, after_b)
        self.assertEqual(changed_inputs(before_a, root=self.root), ["leaf_a.c"])

    def test_nested_include_and_cycle_are_recorded_once(self):
        source = self.put("chunk.c", '#include "bridge.h"\n')
        self.put("bridge.h", '#include "nested/body.c"\n')
        self.put("nested/body.c", '#include "../bridge.h"\nint item;\n')
        self.assertEqual(set(self.inputs(source)), {"chunk.c", "bridge.h", "nested/body.c"})

    def test_missing_generated_body_fails_closed(self):
        source = self.put("chunk.c", '#include "missing.c"\n')
        with self.assertRaisesRegex(RuntimeError, "Missing included C input"):
            self.inputs(source)

    def test_include_search_path_and_spaces(self):
        source = self.put("chunk.c", '#include "nested body.c"\n')
        self.put("includes/nested body.c", "int item;\n")
        inputs = local_input_inventory(source, [self.root / "includes"], root=self.root)
        self.assertIn("includes/nested body.c", inputs)

    @unittest.skipUnless(shutil.which("clang"), "requires local clang")
    def test_compiled_behavior_changes_without_wrapper_edit(self):
        source = self.put("chunk.c", '#include "body.c"\n')
        self.put("body.c", "int answer(void) { return 17; }\n")
        main = self.put("main.c", 'int answer(void); int main(void) { return answer(); }\n')
        obj, binary = self.root / "chunk.o", self.root / "fixture"
        command = ["clang", "-c", str(source), "-o", str(obj)]
        sealed = self.inputs(source)
        first = native_fingerprint(command, "common", sealed)
        subprocess.run(command, check=True, capture_output=True)
        subprocess.run(["clang", str(main), str(obj), "-o", str(binary)], check=True, capture_output=True)
        self.assertEqual(subprocess.run([str(binary)]).returncode, 17)
        self.put("body.c", "int answer(void) { return 41; }\n")
        updated = self.inputs(source)
        self.assertEqual(sealed["chunk.c"], updated["chunk.c"])
        self.assertNotEqual(first, native_fingerprint(command, "common", updated))
        subprocess.run(command, check=True, capture_output=True)
        subprocess.run(["clang", str(main), str(obj), "-o", str(binary)], check=True, capture_output=True)
        self.assertEqual(subprocess.run([str(binary)]).returncode, 41)


if __name__ == "__main__":
    unittest.main()
