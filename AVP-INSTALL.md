For the IPA route and its current limits, see [sideloading](docs/SIDELOADING.md).
The guide below covers building from source.

# Install 117 on Apple Vision Pro

117 is a native Apple port of **Halo: Combat Evolved**, built on
[Master Chef](https://github.com/mitchaiet/master-chef). Windowed play is
established; immersive play has remaining bugs and limitations.

## Requirements

- Apple silicon Mac with Xcode and the visionOS SDK. The project targets
  **visionOS 26 or newer**; the documented Build 130 used the visionOS 27 SDK.
- At least **12 GiB free disk space**, in addition to your game installer.
- Python 3.12 and XcodeGen: `brew install python@3.12 xcodegen`.
- An enrolled Apple Developer Program team with your own app identifier
  and iCloud container. A free Personal Team cannot provision the unchanged
  app's full capabilities.
- A physical Vision Pro paired with Xcode, with Developer Mode enabled.
- A compatible gamepad, or paired PSVR2 Sense controllers for tracked input.

## Game files

Supply your own **retail Halo: Combat Evolved for Windows, updated to 1.10**,
with its valid installation registration. This repository contains no retail
game files, registration, or ready-to-install app.

Xbox, Mac-edition, Custom Edition, Anniversary, and MCC files are not compatible.
An ISO is installation media; it is not imported directly in the headset app.
The supported Windows executable's SHA-256 is:

```text
c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545
```

Choose one of the two routes in
[Prepare your game files](docs/INSTALL.md#3-prepare-your-game-files):

- Use your retail disc/ISO, own product key, and retail PC 1.10 patch with
  a compatible Wine installation on the Mac.
- Copy an already installed PC 1.10 game folder and your private Halo
  registration export from Windows. This route does not require Wine on the Mac.

Keep product keys and registry exports private.

## Build from an existing checkout

If you used the AVP Ports Index's clone command, keep its exact scanned commit.
Skip the clone commands in the linked guides; do not pull or switch revisions.

From the repository root, check the prerequisites:

```sh
./setup.sh --stage check --json
```

For the already installed Windows route, prepare your local inputs:

```sh
./setup.sh --game-dir "/path/to/your/Halo" \
  --registry "/path/to/halo-install.reg" --no-open
```

For the disc/ISO route, use the alternative command in the
[full installation guide](docs/INSTALL.md#3-prepare-your-game-files).
Setup validates the executable and maps, prepares your private game payload,
installs pinned Python dependencies, generates the native engine, downloads
and verifies visual packs, and generates the Xcode project.

Follow [Configure your signing and shared iCloud container](docs/INSTALL.md#4-configure-your-signing-and-shared-icloud-container).
Use identifiers owned by your team; do not reuse the maintainer's container.
Regenerate the Vision Pro project after changing configuration:

```sh
source .venv/bin/activate
python tools/build_engine_vision.py --generate-only
open native/EngineVision/EngineVision.xcodeproj
```

## Install on Apple Vision Pro

1. Select the **EngineVision** target/scheme and your Team. Keep automatic
   signing enabled and configure the iCloud Documents capability as described
   in the full guide.
2. Pair the headset in Xcode, complete trust prompts, enable Developer Mode,
   and select the **physical Vision Pro** as the run destination.
3. Set **Product → Scheme → Edit Scheme → Run → Info → Build Configuration**
   to **Release**. The default Debug configuration is unoptimized.
4. Unlock the headset and press **Run**. Wait for build, signing, and installation.
   The app includes your prepared game payload, so the first transfer is large.
5. Open **117**, wait for data preparation and the save check, then select
   the **1** game card to start in a window.

## Controls, limits, and updates

Use **Expand** for immersive play; leaving immersion returns to the running
game. Pair both PSVR2 Sense controllers and grant tracking permissions for
tracked aiming, or use a normal gamepad.

Sense tracking targets on-foot campaign play. Vehicles and turrets use
conventional stick controls; turn tracked aiming off for multiplayer.
See [controls](docs/CONTROLS.md) and [known issues](docs/KNOWN_ISSUES.md).
Only Halo: Combat Evolved is implemented.

Back up saves before updating, keep signing and container identifiers stable,
and install over the existing app. See
[cloud saves](docs/INSTALL.md#7-carry-your-progress-between-devices)
for cross-device setup and its current validation limits.
