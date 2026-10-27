# Building and installing

For the easiest route, use [the guided ISO setup](SETUP.md):
`./setup.sh "/path/to/HALO.iso"`. [Agent-specific instructions](AGENT_SETUP.md)
include a copy/paste prompt. The manual steps below remain available for
existing installations and explicit signing workflows.

## Requirements

- An Apple Silicon Mac, Xcode with the visionOS SDK, and command-line tools.
  The app targets visionOS 26.0. Build121 was built with the 27.0
  SDK; older toolchain combinations have not been qualified.
- Python 3.12 and the dependencies below. XcodeGen is needed for the Xcode
  project route. The direct SDK build does not require XcodeGen.
- Your own Halo: Combat Evolved for PC retail disc or ISO and a valid product
  key. Install your copy and prepare the supported PC 1.10 executable and
  complete retail game data described below. The Git checkout contains no game
  files. The [historical upstream archive](COMPLETE_RELEASE.md) is documented
  separately and is not a 117 release. Keys and registration values are never supplied.
- For device installation, your Apple development signing identity, an app
  identifier you control, a provisioning profile containing your Vision Pro,
  and a paired headset with Developer Mode enabled.

```sh
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-development.txt
```

## Supply the executable

Start with your own installed Halo PC copy, obtained from your disc or ISO
using your valid product key. An ISO is installation media; the app does not load it directly.
`setup.sh` can mount it and guide the original installer, or you can supply
an existing installation for the manual steps here. Preparing the retail PC 1.10
installation is a prerequisite to the source-generation steps below.

Generation supports the executable with SHA-256:

```text
c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545
```

Put your own matching executable at `game/halo.exe` and verify the digest with
`shasum -a 256 game/halo.exe`. Alternatively, set `HALO_EXE` to the absolute
path of your executable and use `shasum -a 256 "$HALO_EXE"`. The `game`
directory is ignored. Address lists are specific to that executable;
Halo Custom Edition, Anniversary, and other builds are not interchangeable.
The manual generation tools do not download or patch game files.
The setup wizard can run your locally supplied retail PC update installer.

## Generate native engine sources

From the repository root:

```sh
python tools/generate_engine_reuse.py \
  @decompilation/c9acf0c46954/function-addresses.txt \
  @decompilation/c9acf0c46954/extra-function-entries.txt \
  --label whole-exe --max-functions 10000 --trap-unsupported --discover --chunks 32
python tools/export_engine_imports.py
```

Inspect `native/build/engine-reuse/whole-exe/generation.json` for generation
errors and unresolved boundaries. Generated code and receipts are local
artifacts and are excluded from the public source package.

## Build

For a native macOS diagnostic host:

```sh
make -C native/EngineHost -j2
```

For an unsigned visionOS application using only the SDK compiler:

```sh
python tools/build_engine_vision.py --configuration Release --direct --clean
```

The output is `native/EngineVision/.build/DirectXROS/HaloVision.app`.
This direct build command does not add game data and is not yet signed for a
headset. The guided setup separately stages a private payload; Xcode includes
that payload when present, and `setup.sh --stage build` adds it to the unsigned
app explicitly.

To generate an editable Xcode project:

```sh
python tools/build_engine_vision.py --generate-only
open native/EngineVision/EngineVision.xcodeproj
```

Set the application's bundle identifier to one you control, select your
team, and enable automatic signing in Xcode. The public default,
`org.example.halovision`, is a placeholder. The CLI normally performs unsigned
builds; an Xcode development run supplies a profile for your account.
The project version is 1.0.27 (build 127), updating the app icon while preserving
build 126's launcher and gameplay behavior. See
[validation](VALIDATION.md) for build and physical-device acceptance.
Generated app artwork is included;
see [artwork provenance](assets/README.md).

### Configure your own iCloud container

The original author's `iCloud.com.zephyrstudios.mastercheef` identifier is
retained for existing private installations. A different developer team must
register its own iCloud container and replace that identifier consistently in:

- `native/EngineVision/Info.plist`: `MasterCheefCloudContainer` and the matching `NSUbiquitousContainers` dictionary key.
- `native/EngineVision/EngineVision.entitlements`: both container identifier arrays.
- `native/EnginePad/Info.plist`: the same two Info.plist entries.
- `native/EnginePad/EnginePad.entitlements`: both container identifier arrays.

Use the same new container for both apps and enable iCloud Documents in each
target's signing capabilities. Set both app bundle identifiers to identifiers
you control; the mobile target's `com.zephyrstudios.mastercheef.ipad` is also
the original author's namespace. Profiles must authorize those identifiers,
the common container, and the intended devices. Keep existing identifiers
unchanged when updating an already installed private copy.

Both build routes currently download roughly 0.9 GiB of checksum-pinned
upstream visual packs. These builds depend on the upstream archive remaining
available, or on your providing a previously downloaded, verified copy.
They are stored under
`.setup/VisualMods`, outside Git. Setup downloads the checksum-pinned archive
when necessary; for an offline download run
`python3 tools/visual_assets.py --archive /path/to/MasterChef-v1.0.3-visual-assets.zip`.
The historical upstream Complete archive can supply those files directly;
it is not a 117 release. `--bundled` is upstream-archive-only.

## Supply game data and sign locally

Use a clean directory containing your owned game installation, including
`halo.exe`, `maps`, `shaders`, and `strings.dll`. Copy
`tools/halo-vision-registry.template.txt` to `halo-vision-registry.txt` in that
directory and replace its placeholders with values from your own installation.
The encoded registry values are not the printed product key; do not paste
your product key into the `DigitalProductID` field.
Keep `ExitFlag` set to `clean`. That registry file contains private product
information and must stay outside public source and release uploads.

Build the app with your chosen bundle ID in Xcode. Export that app's signing
entitlements to a local plist and identify your signing identity and device
with Xcode or these read-only commands:

```sh
security find-identity -v -p codesigning
xcrun devicectl list devices
codesign -d --entitlements :- "$PROFILE_APP" > "$ENTITLEMENTS"
```

`PROFILE_APP` is an app signed by Xcode for your team, chosen bundle ID and
headset. `APP` is the compiled app with the same bundle ID. Supply every signing
input explicitly; the tool contains no default personal identity or device.

```sh
python tools/prepare_engine_vision_device.py \
  --app "$APP" --profile-app "$PROFILE_APP" --game "$GAME_DIR" \
  --output native/EngineVision/.build/local-device \
  --team-id "$TEAM_ID" --bundle-id "$BUNDLE_ID" \
  --device-udid "$DEVICE_UDID" --core-device-id "$DEVICE_ID" \
  --identity-sha1 "$IDENTITY_SHA1" --entitlements "$ENTITLEMENTS"
```

Use `--preflight-only` to validate those inputs first. Choose a new output
folder for each package. The tool checks executable identity, file hashes,
profile expiry, device inclusion, signing identity, entitlements and the final
signature. Its output contains your private game files and signing metadata;
it is a local installation package, not the public source release.

With the headset unlocked and on the same network:

```sh
xcrun devicectl device install app --device "$DEVICE_ID" \
  native/EngineVision/.build/local-device/HaloVision.app
xcrun devicectl device process launch --device "$DEVICE_ID" "$BUNDLE_ID"
```

The app imports bundled data into its sandbox. Its emulated user is named
`Player`. Existing development builds that used a different emulated user
folder require an explicit save migration; this source cleanup does not
modify existing installations or saves.

## Dedicated visionOS simulator

With the generated engine and local `.setup/GamePayloadManifest.json` ready:

```sh
python3 tools/build_engine_vision_simulator.py \
  --output .setup/SimulatorBuild --jobs 2
```

This compiles the production app and engine for `arm64-apple-xros26.0-simulator`,
checks each native object's simulator platform, and writes a source/build receipt
next to `MasterCheef.app`. Native objects are cached with their recursively
included source hashes. It uses ad-hoc signing and makes no device or simulator
calls. `--typecheck` checks only the production Swift sources.

Create a simulator dedicated to this app, using identifiers from
`xcrun simctl list runtimes` and `xcrun simctl list devicetypes`. Use its exact
returned UDID for `simctl boot`, `install`, `launch` and `io screenshot`; avoid
the shared `booted` alias. The default simulator bundle ID is
`com.zephyrstudios.mastercheef.simulator`. Open that simulator in its own
Device Hub window so other app sessions remain separate.

The simulator package contains private local game data. Keep it in `.setup`.
Launcher and simulated rendering checks do not establish physical headset
tracking, controller, stereo, or performance acceptance.

## iPad, iPhone and shared cloud saves

One mobile target runs on iPad and iPhone (iOS/iPadOS 18+, landscape, extended
controller). It uses the same game payload and save format as the visionOS app.
The original author’s defaults are bundle identifier
`com.zephyrstudios.mastercheef.ipad` and the CloudDocuments container named by
`MasterCheefCloudContainer`. New developers must configure their own identifiers
as described above; existing installations must keep their current identifiers.
The
`--display-name` (default `117`) changes only the visible home-screen name.
Both targets must be signed with access to that container. The source
entitlements declare it; the provisioning profiles must authorize it too, and
the direct builder validates this before signing. Versions come from
`native/EnginePad/project.yml` (`--marketing-version`/`--build-number` override).

Cloud saves are checked when the launcher opens, when it returns to the
foreground, when a snapshot arrives in iCloud, and before Play, always while the
engine has not started in this process. Monitoring stops when Halo starts.
After playing, close the app from the app switcher, reopen it, and wait for
Cloud Saves to finish before switching devices; the launcher says "Close and
reopen to sync this session" once the game has run. Returning to the launcher or
stopping the engine is insufficient: guest save-writing threads may still be
running. Live checkpoint synchronization is not implemented.

**Public default.** Source builds do not require any Vision Pro save
(`MasterCheefRequireVisionSeed` is `false`). A new player can start immediately;
their first saves are preserved locally and published after the next reopen.
A missing local tree after earlier play, and different progress on two devices,
still require an explicit choice in Cloud Saves.

**Personal migration candidate.** To carry an existing Vision Pro campaign to a
first install, build with `python3 tools/build_engine_ipad.py --require-vision-seed`.
That flag is applied after the Info.plist merge and makes first play wait for a
validated Vision source. Before the first launch, copy a freshly exported and
verified archive to `Documents/MasterCheefMigration/vision-pro-save.mc-save`
inside the installed app (or let the Vision snapshot arrive through iCloud).
Produce the archive with `EngineSaveArchive` from the actual Vision save tree and
keep an independent original backup. The app retains the source archive, never
replays it over later progress, and makes recovery copies before replacing local
saves. Never bundle personal saves into the app, tests or release artifacts.

Automatic sync retains immutable revisions and their ancestry. Independent
changes on two devices require an explicit choice in Cloud Saves; device clocks
do not decide which progress survives. A device can play offline, with sync
marked pending. An iCloud upload or local fixture test alone does not prove
that another physical device has received the save. Verify restoration and
campaign continuation on each device separately.

## Optional assets and diagnostics

`tools/import_texmod_pack.py` and `tools/merge_hd_texture_pack.py` support
locally supplied texture packs. Run each with `--help`. No texture or shader
pack binaries are stored in Git; the checksum-pinned upstream visual-assets
archive supplies the configured packs. Retain the relevant notices
for any additional assets you use.

`tools/import_censhine_pack.py` adapts the reviewed 13-program CEnshine 1.0.0
subset to the retail PC shader interface. It takes a locally supplied,
Composer-decoded retail `fx.bin`, the decoded CEnshine collection, and an output
path. It does not download those inputs or include Composer:

```sh
python3 tools/import_censhine_pack.py /path/to/decoded/fx.bin \
  /path/to/decoded/collection /path/to/output/ShaderMods.hvs
python3 tools/test_censhine_import.py
```

This rebuild tool is optional: ordinary setup uses the already verified
release packs. The adapter is also included in `CEnshineSources.zip` inside the separate
upstream visual-assets archive, along with the upstream source/license; that ZIP
is not stored in this repository. See [feature coverage](FEATURES.md).

`tools/probe_engine_menu_input.py` runs bounded desktop diagnostics with a
cloned game directory. `tools/watch_engine_vision_telemetry.py` reads local
headset diagnostics when supplied `--device`, `--bundle-id`, `--build`, and
`--build-id`. Logs and reports can contain paths and device information;
review them before attaching them to public issues.
