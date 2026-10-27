# 117 validation

## Current preview: build 127

Version 1.0.27 replaces the C-shaped plasma loop with a bold 117 numeral mark. Both platform asset catalogs compile without diagnostic errors or warnings. The visionOS icon keeps the complete opaque artwork on its back layer and uses a fully transparent required front layer. The mobile icon uses the same artwork. The native numeral 1 cover and custom-artwork preferences are unchanged.

Build 127 is an artwork and package-version update using build 126's verified executable. Independent review confirms all 48 Mach-O sections and every non-signature byte remain identical; no engine or Swift sources were recompiled. Strict signature verification passes, with the same signing certificate, provisioning profile and cloud entitlements. Gameplay, launcher code, bundle identity and save locations are unchanged.

Build 127 is installed on Vision Pro without an automated launch. All 55 local save files match their pre-install hashes. The iPhone/iPad source artwork is updated and its asset catalog compiles, but no new mobile package or physical iPad installation is claimed. Physical appearance and launcher acceptance remain pending.

## Previous preview: build 126

Version 1.0.26 addresses the build 125 launcher feedback: one fitted, non-scrolling game card starts or resumes Halo; the centered 117 menu contains Settings, including the preferred window/immersive mode. The duplicate controller status and decorative launcher animations are removed. Artwork customization remains available.

A Play request made during an existing save operation is retained instead of silently dropped. The session coalesces repeated requests and suppresses new automatic checks while preparing Play. A bounded wait reports slow saves without clearing another operation's busy state or cancelling its writes; the normal migration, conflict, local-recovery and actual-runtime checks still gate the engine. The held-operation fixture passes 38 checks for late arrivals, pending Play, timeout, cancellation and save safety; the existing synchronization fixture passes 55 checks.

Both build 126 device-SDK packages compile and sign as 117 / 1.0.26 / 126. Each platform compiled 60 native units, with 55 Vision Swift sources and 22 mobile Swift sources. The existing bundle/save identities and the mobile Vision-save seeding requirement remain intact. Build 126 is installed on Vision Pro without an automated launch. Physical launcher acceptance is pending; source checks do not establish that the reported on-device launch failure is resolved. New launch-state diagnostics distinguish a received tap, save wait and rejected engine start.

## Previous preview: build 125

Version 1.0.25 adds original plasma app icons, a numeral 1 default cover, and locally stored custom launcher artwork. The image-import fixture passes 21 checks covering orientation, downsampling, bounded decoding, metadata removal, persistence, invalid-input preservation and reset isolation. Full visionOS and iPad/iPhone Swift device-SDK typechecks pass.

Both build 125 device-SDK packages compile and are signed. The Vision build freshly compiled all 60 native units and 55 Swift sources; the mobile build compiled 60 native units and 22 Swift sources. Build 125 was installed on Vision Pro, with all 55 existing save files verified unchanged, and was not automatically launched. The iPad/iPhone package is built but not installed. Physical launcher artwork, picker and controller interaction still require testing; source checks do not establish device acceptance. Custom artwork is local per device and is not part of cloud saves.

User testing of build 125 reported a stuck launcher, unresponsive Play/card actions, duplicate controller status and unwanted scrolling/animation. Build 126 is the corrective update for that report; physical confirmation remains pending.

## Previous preview: build 124

Version 1.0.24 adds controller-aware gameplay prompts and maximum startup graphics. Fresh physical-device SDK builds compiled all 60 native units for both platforms, plus 54 Vision Swift sources and 21 mobile Swift sources. Both packages report 117 / 1.0.24 / 124, are signed, pass strict signature verification, and retain the existing private bundle/save identities. Recursive compiled-input hashes match the reviewed source.

Build 124 is installed on the physical Vision Pro. A device query confirms its name and version. Two matching save backups were captured immediately before installation; a post-install readback confirms all 55 save files remain byte-identical. The app was not launched. The iPad/iPhone package is built locally but has not been installed; no physical iPad interaction occurred for this update.

The new prompt tests cover input provenance, controller metadata, bounded labels, Sense context and accepted device ingress. Normal and sanitizer runs pass; the original formatter fixture passes 39 ABI/output checks using privately generated input and explicitly skips without it. An independent review checked the formatter's callers, buffer bounds, mappings and final merged dispatch routes. Platform compilation covers macOS, iPhoneOS and xrOS. Spatial metadata tests require macOS/SDK 26 or later and explicitly skip on older CI hosts.

Launch-preference tests verify the one-time maximum migration, preservation of unrelated preferences and later lower choices. The original timing fixture verifies the limiter hook keeps simulation time, profile state and cinematic/timedemo behavior; actual iOS and Vision SDK routing checks confirm the hook is enabled for those builds and the Mac keeps its direct path. All 22 mobile builder tests pass.

Visible prompts, maximum-setting stability, sustained FPS and new control feel still require a physical gameplay test. Maximum is a requested configuration, not a measured rate. Mobile Magic Keyboard/touch gameplay and the live cross-device iCloud roundtrip remain unverified.

## Previous mobile package: build 123

Version 1.0.23 (build 123) adds iPad/iPhone keyboard, mouse, trackpad and touch aiming, Native resolution and display-maximum frame-rate requests. A fresh iPhoneOS device-SDK build compiled all 60 native units and 20 Swift sources, linked and signed successfully. The package reports 117 / 1.0.23 / 123, includes both indirect-input and high-refresh opt-ins, and passes strict signature verification. Compiled input hashes were checked against the final source. This is a local package, not a device test.

Focused source checks cover input ownership, stale-event fencing, queued clicks, pointer capture, two-finger secondary clicks, presentation transitions, graphics admission, display-rate changes and allocation rollback. The native and Swift input fixtures and graphics checks pass; native checks also pass with ASan/UBSan. The original timing-function fixture verifies the mobile rate hook preserves elapsed game time, cinematic/timedemo behavior and the saved limiter setting; that fixture explicitly skips without privately generated game code. Synthetic Metal checks exercise the resize/readback path. All 22 mobile builder tests pass.

Build 123 has not been installed or launched on a device. Magic Keyboard pointer capture, tap settings, aiming feel, sustained performance at Native/Display maximum, and mobile gameplay still need physical acceptance. The shared visionOS source is SDK-checked; no build 123 Vision Pro package or installation is claimed. At build 123 publication, the latest installed build was Build121, whose display name is Keyship. Build 124 above is the current installation. Existing save restoration is verified below; live iCloud delivery remains unverified.

The public GitHub source workflows for builds 122 and 123 passed. Build 122 was the source-only rename to 117 / zschwendi/117-vision. The Build120/Build121 evidence below is historical and unchanged.

Build120 was built, signed, installed and played on a physical Vision Pro. User feedback described substantially improved handling.

Build121 (1.0.21) compiles and signs for physical visionOS and iOS. The Vision build includes 60 native units and 53 Swift sources; the iPad/iPhone build includes 60 native units and 14 Swift sources. The display name is Keyship; existing private bundle and save identities are retained.

The merged native sanitizer checks pass, including 2,207 input ownership and pause checks. Independent UI-state checks and the combined device-SDK build pass. A clean source-only checkout passes the portable normal and sanitizer suites; tests needing private translated game code explicitly skip there. These checks ran locally on macOS 27, not on the remote GitHub runner.

Build 121 is installed on a physical Vision Pro and iPad Pro. The Vision app was not launched during this update. On the iPad, the app restored the current Vision migration archive before gameplay; a device readback confirmed all 55 save files match the source byte-for-byte. An independent source backup is retained.

A local migration rehearsal also preserves both histories in a conflict. These checks establish restoration and conflict behavior, not iCloud delivery. Physical iPad/iPhone gameplay, the live cross-device iCloud roundtrip, and the new 121 held-grenade/animation/interface feel still require device acceptance.

No full-campaign completion or stable performance across every graphics preset is claimed. Historical evidence below belongs to the upstream foundation and its old version series.

---

# Source-release validation

## Source completeness follow-up

The comparison with the development tree covered 271 public native files.
Of those, 260 are byte-identical; the other 11 have reviewed
anonymization, source portability, notices, signing or resource-packaging
differences. No missing runtime fix was found in this comparison.

The CEnshine shader adapter already present in the release's `CEnshineSources.zip` is
now also available as `tools/import_censhine_pack.py`, with four synthetic
regressions registered in the source suite. Those four checks, the 24 setup
regressions and nine visual-asset/content checks passed. Source hygiene and
secret scans passed. No runtime behavior, release binary or existing ZIP
checksum changed during this follow-up; it is not a new headset acceptance run.

## 1.0.3 complete packaging update

- Fresh public-source engine generation completed with 8,336 functions and
  32 chunks. An unsigned ARM64 visionOS Release build compiled 59 native units
  and 24 Swift sources, version 1.0.3/build 103, SDK 26.5.
- The direct app contains the exact Build91 texture and shader packs: 1,068
  texture entries and 13 shader replacements. All pack hashes match the
  installed baseline. The CEnshine source/license archive is also included.
- Xcode project generation confirms every visual pack and manifest in Copy
  Bundle Resources. Previously these resources were omitted by public builds.
- All nine visual-asset/content tests passed: exact import/resume/bundling, outer
  checksum rejection, changed-pack preservation, unsafe and symbolic entry
  rejection, invalid inner content, symbolic destination rejection and local
  Complete-bundle reuse without a network request, plus preservation of the
  original configuration and movie assets while excluding saves/registration.
  The 24 setup regressions
  and portable source suite (32 C targets plus additional checks) also passed.
- The renderer, audio recovery, native settings and settings UI sources match
  the frozen Build91 sources. The sampled headset preferences contained no
  saved resolution override; the recorded startup resolution was 2048x1536.
  Device tracking history and physical pose are not copied into the release.

The new build is package-verified and unsigned. Build91 installation and
startup evidence below remains the headset evidence for these runtime fixes;
it does not prove a new Build103 installation, full mission performance or
audible continuity. Other users supply their own registration and signing.

## 1.0.2 rendering and audio recovery update

Validated on an Apple Silicon Mac:

- Full source suite: 59 C checks plus Objective-C, Metal, Swift and Python
  checks passed. The portable subset also passed normally and with ASan/UBSan
  (32 C checks). Optional comparisons requiring generated engine code were
  skipped in the public checkout.
- Real-Metal regressions reproduced the previous sampler/depth cache failures
  and the rejected mixed texture address modes. The fixes passed 768 sampler
  requests and 192 depth/stencil states, including actual queued draws beyond
  both cache capacities. Cache storage remained bounded.
- All 25 U/V address combinations passed nine out-of-bounds pixel comparisons
  with each renderer path: 450 exact GPU pixel checks. Nonzero border colors
  remain unsupported.
- The production audio coordinator passed injected activation, rebuild and
  queue-start failures, two-second retry timing, background/interruption
  suppression, denied resume permission and stopped-runtime checks. The old
  coordinator reproduced the permanent-suspension failure. The new renderer
  and audio tests also passed focused ASan/UBSan runs. Audio framework calls
  in this lifecycle test are mocked; this is not an audible-output test.
- All 24 setup regressions and four documentation-art hygiene regressions
  passed. The reviewed header requires an exact path and SHA-256 match.

The corresponding owner Build91 was compiled from a frozen private snapshot
with fresh objects: 59 native units, 32 translated chunks and 24 Swift sources.
Signing and the complete local game-payload manifest passed. That bundle is
private. The public source app uses version 1.0.2/build 102, a separate build
number series, and requires the user's game files and signing.

Desktop entry probes for Halo (a30) and The Maw (d40) each completed 2,400 frames
with scripted controller turns and normal audio mixing enabled. All ten
captured GPU layers were reviewed for each scene. Median measured entry-scene
rates were 29.78 and 29.86 FPS respectively. These short desktop probes do not
measure firefights, full missions, audible output or headset performance, and
do not establish an improvement over an equivalent baseline.

The initial owner installation attempt could not reach the headset. A later
attempt on September 29, 2026 (Central time) installed and launched Build91;
device inventory independently confirmed version 1.0.2/build 91. Five startup
diagnostic snapshots matched the expected BuildID. The final snapshot reached
engine frame 1,226 with immersive presentation active, 2,708 completed GPU
frames and zero GPU failures. Its audio queue was running with nonzero samples,
zero enqueue failures and zero watchdog rebuilds. This verifies installation
and startup, not audible continuity, visual acceptance or campaign performance.
Known limitations remain in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

CI now targets macOS because the host fixtures depend on Mach APIs and Apple's
linker. The local passes above are not hosted CI results; previous hosted runs
were blocked by the account's billing status. Source publication and device
acceptance remain separate gates.

## 1.0.1 setup update

- Passed 24 synthetic setup regressions: Windows/Wine registry parsing,
  private-value error handling, owner-only files, supported-file import,
  rollback, resume, preservation of changed files, payload manifests,
  low-space refusal, installer refusal without a human terminal, and ISO
  cleanup after failure/cancellation.
- Passed the portable source suite on an Apple Silicon Mac, including the
  new setup regressions.
- Checked a real owned retail ISO with `--stage check --json`: the layout was
  recognized, the original executable was identified as needing the PC 1.10
  update, and the temporary read-only mount was detached afterward.
- Verified XcodeGen with a synthetic local payload: both payload resources
  appear in Copy Bundle Resources and automatic signing is enabled.
- The current validation Mac had less than the required 12 GiB free. The
  preflight correctly reported this. A fresh Wine installer/key-entry/update
  session and the complete ISO-to-headset path were **not** run for this
  update. The Windows fallback export instructions were not exercised on a
  Windows machine. No new signed build or headset acceptance is claimed.

The earlier hosted GitHub Actions attempts were blocked by the account's
billing status before tests could start. Local test results above are not
hosted Linux CI results. Original runtime code is unchanged by this setup
update; app metadata is now 1.0.1 (build 101).

## 1.0.0 runtime/source baseline

The following checks were completed from the curated source tree on an
Apple Silicon Mac. Local game data was used only for generation and optional
reference comparisons; no game data or resulting binaries are distributed.

| Check | Result |
| --- | --- |
| Full source suite | Passed: 59 C targets plus additional Objective-C, Metal, Swift, and Python checks |
| Portable subset with ASan/UBSan | Passed: 32 C targets plus additional comparisons |
| Save preservation after generic player-path cleanup | Passed: reuse, missing-file detection, save-tree preservation, failed-promotion rollback |
| Fresh engine generation | 8,336 functions in 32 translation units; no unresolved function frontier |
| Unsigned visionOS Release compile and link | Passed: 59 native units and 24 Swift sources, ARM64, SDK 26.5, deployment 26.0 |
| Public version metadata | 1.0.0, build 100; no personal bundle, team, or device ID |
| Xcode project generation | Passed |
| Required signing/device inputs | Missing inputs rejected; no personal defaults |
| Source path/content hygiene | Passed |
| Gitleaks 8.30.1 with decoding | No unreviewed findings |
| Source ZIP integrity | Every archived file checked against the included SHA-256 manifest |

Two Gitleaks false positives were reviewed in unchanged xxHash AVX512
arithmetic. The checked-in configuration suppresses only those exact
expressions in that specific file. Third-party license notices are retained.
The privacy audit includes personal home paths, device/signing identifiers,
private-network addresses, secret patterns, binary artifacts, and archive
entry names. No automatic scan can guarantee discovery of every possible
secret; this export also uses an explicit source selection and manual review.

The full suite ran before generated code was present and reported skips for
optional original-engine comparisons. The later sanitizer subset ran with
freshly generated code, including native light/shadow gather comparisons.
A real-map gather test was skipped because no map fixture was supplied. The
portable subset was executed on macOS; the included Ubuntu CI workflow has
not yet run on a hosted runner for this release.

The generator records 15 undecodable entries and 47,843 explicit instruction
trap annotations in the discovered executable regions. These counts do not
establish whether the corresponding paths are reachable in gameplay. Keep
checking the generation receipt when changing the supported executable or
translator; unresolved instructions fail explicitly at runtime.

The freshly generated application was compiled and linked, but this public
source revision was not signed, installed, or played on a headset. No claim
of full-campaign completion, stable combat FPS, or resolved visual/audio
issues follows from these checks. See KNOWN_ISSUES.md.

Generated sources, app products, intermediate objects, project files, raw
reports, device data and private Git history were removed from the release
tree after validation. SOURCE_MANIFEST.json lists the source archive contents
except for the manifest itself; the external checksum covers the whole ZIP.
