# Install 117 from scratch

117 runs **Halo: Combat Evolved** natively on Apple devices: immersive play on
Vision Pro and full-screen play on iPad and iPhone, with shared saves and settings.

**This is a source build. There is no ready-to-install 117 app in this repository.**
You prepare your own game files on a Mac, build the app in Xcode, and install it
on your devices. You do not need a gaming PC to stream the game while playing.

## 1. Get the prerequisites

| You need | Details |
| --- | --- |
| Apple Silicon Mac | Allow at least 12 GiB of free space in addition to your game installer. Game files, generated code and build products stay on this Mac. |
| Xcode | Install Xcode, open it once to finish setup, and install the visionOS platform/SDK. The shared setup currently requires that SDK even if you only want the mobile app. Also install iOS platform support for iPad/iPhone. |
| Apple Developer Program team | The supported signing route uses an enrolled team with its own app identifiers and iCloud container. A free Personal Team cannot provision the unchanged app's full capabilities. |
| Supported device | Vision Pro with visionOS 26 or later, or iPad/iPhone with iPadOS/iOS 18 or later. These are deployment minimums, not a promise that every device model has been tested. |
| Your Halo PC installation | The original **retail Halo: Combat Evolved for Windows, updated to 1.10**, plus its installation registration. You can prepare this from your disc/ISO and product key, or bring an already installed copy. |
| Build tools | Python 3.12 and XcodeGen. If you use Homebrew: `brew install python@3.12 xcodegen`. |

Apple documents the required account capabilities for
[iOS](https://developer.apple.com/help/account/reference/supported-capabilities-ios/)
and [visionOS](https://developer.apple.com/help/account/reference/supported-capabilities-visionos/).
Mobile builds also require Extended Virtual Addressing and Increased Memory Limit;
keep both entitlements enabled.

**Xbox discs, the Mac edition, Custom Edition, Anniversary, and MCC are not inputs
for this build.** An ISO is installation media, not a file you open inside 117.
The supported Windows executable has this SHA-256:

```text
c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545
```

## 2. Download the source

In Terminal on your Mac:

```sh
git clone https://github.com/zschwendi/117-vision.git
cd 117-vision
./setup.sh --stage check --json
```

The check lists missing prerequisites; it does not install or build anything.
Exit code 2 means something still needs attention. Choose **one** of the game
preparation routes below. A bare check assumes the ISO route and may list Wine
as missing; Wine is only needed for Route A. For Route B, check your actual inputs:

```sh
./setup.sh --game-dir "/path/to/your/Halo" \
  --registry "/path/to/halo-install.reg" --stage check --json
```

## 3. Prepare your game files

### Route A: use your retail PC disc/ISO on the Mac

Install a macOS Wine distribution capable of running 32-bit Windows installers
(and Rosetta if it requires it). See the
[WineHQ macOS guide](https://wiki.winehq.org/MacOS). Have your own retail PC 1.10
updater, `halopc-patch-1.0.10.exe`, ready if the disc contains an older version.
Setup does not download the game, supply a key, or fetch a replacement executable.

```sh
./setup.sh "/path/to/HALO.iso" \
  --patch "/path/to/halopc-patch-1.0.10.exe" \
  --no-open
```

Replace the example paths with your actual files. Setup opens the original
installer in an isolated Wine prefix. Enter your own key there, keep the default
installation location, and skip the optional DirectX/GameSpy extras. Do not launch
the Windows game. Finish the patch installer when prompted.

Wine compatibility varies. If the installer does not work, use Route B rather
than repeatedly starting more installer instances. The [guided setup guide](SETUP.md)
has troubleshooting and options for an existing Wine installation.

### Route B: use an already installed Windows copy

Install your retail copy on Windows and apply the **retail PC** 1.10 update.
Copy its complete installed Halo directory to your Mac. Export only Halo's
registration from Command Prompt on 64-bit Windows:

```bat
reg export "HKLM\SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Halo" "%USERPROFILE%\Desktop\halo-install.reg"
```

On 32-bit Windows, omit `WOW6432Node`. Copy that private export to the Mac, then run:

```sh
./setup.sh --game-dir "/path/to/your/Halo" \
  --registry "/path/to/halo-install.reg" \
  --no-open
```

Keep the registry export private. It contains product information and is not the
same as the printed product key. Do not attach it to an issue or commit it to Git.

### Let setup finish

Both routes validate the executable and campaign maps, import your game data,
install the pinned Python dependencies in `.venv`, generate the native engine,
download and verify the visual packs, and create the Vision Pro Xcode project.
The visual-pack download is roughly 0.9 GiB and expands to several GiB locally.
Your original installation is kept intact. Generated code and private game data
are ignored by Git.

Wait for setup to report success before opening a project. If it stops, fix the
named prerequisite and rerun the same command; valid prepared inputs are reused.

## 4. Configure your signing and shared iCloud container

Do this before the first installation. If you are updating an existing private
copy, **keep its app and container identifiers** to preserve its data access.

1. Add your Apple Developer Program account in **Xcode → Settings → Accounts**.
2. Choose two app bundle identifiers you control: one for Vision Pro, one for
   the shared iPad/iPhone app. Set `PRODUCT_BUNDLE_IDENTIFIER` in
   `native/EngineVision/project.yml` and `native/EnginePad/project.yml` respectively.
   The checked-in identifiers are a placeholder or the original author's namespace.
3. Register one iCloud container under your developer team and use it for both
   apps. Replace the existing container identifier consistently in these files:

   | File | Entries to change |
   | --- | --- |
   | `native/EngineVision/Info.plist` | `MasterCheefCloudContainer` and the matching `NSUbiquitousContainers` dictionary key |
   | `native/EngineVision/EngineVision.entitlements` | Both iCloud container identifier arrays |
   | `native/EnginePad/Info.plist` | The same two Info.plist entries |
   | `native/EnginePad/EnginePad.entitlements` | Both iCloud container identifier arrays |

4. Generate the project for your platform in the next step. In its target's
   **Signing & Capabilities**, select your Team, keep **Automatically manage
   signing** enabled, and enable **iCloud → iCloud Documents** with that same
   container. Ensure your provisioning profiles authorize it for both apps.

For example, your own container might be `iCloud.com.yourname.halo117`; it must
actually be registered to your team. Changing only the visible app name does
not change signing or cloud access. The [manual signing guide](BUILDING.md#configure-your-own-icloud-container)
covers the shared configuration and direct-build alternatives.

## 5. Build and install on your device

### Vision Pro

From the repository root, regenerate the project after the configuration changes:

```sh
source .venv/bin/activate
python tools/build_engine_vision.py --generate-only
open native/EngineVision/EngineVision.xcodeproj
```

Select the **EngineVision** target/scheme and configure signing as above. Pair
your headset in Xcode's **Devices and Simulators** window, enable Developer Mode,
and complete any trust prompts. Select the physical Vision Pro as the run
destination. Before playing, open **Product → Scheme → Edit Scheme → Run → Info**
and change **Build Configuration** to **Release**; the default Debug build is
unoptimized. Put on the headset, unlock it and press **Run**. Xcode builds, signs,
installs and launches your private copy.

### iPad or iPhone

From the same prepared checkout:

```sh
xcodegen generate --spec native/EnginePad/project.yml --project native/EnginePad
open native/EnginePad/EnginePad.xcodeproj
```

Select the **EnginePad** target/scheme, your Team and the physical iPad or iPhone
as the run destination. Retain the iCloud, Extended Virtual Addressing and
Increased Memory Limit capabilities. Connect by USB for the first installation,
unlock the device, trust the Mac, and enable Developer Mode when requested.
Set **Product → Scheme → Edit Scheme → Run → Info → Build Configuration** to
**Release**, then press **Run**. Repeat with the other mobile device to install
the same app there.

The app includes your game data and visual packs, so the first transfer is large.
Use landscape orientation. The [mobile build guide](../native/EnginePad/README.md)
also documents the direct builder and explicit provisioning-profile route.

## 6. Start playing

Open **117**, wait for game-data preparation and the initial save check, then
select the **1** game card. A new player does not need an existing Vision Pro save.

| Device | First controls to know |
| --- | --- |
| Vision Pro | The game starts in a window. Use **Expand** to enter immersive play; leaving immersive returns to the same running game. Pair both PSVR2 Sense controllers and grant the requested tracking permissions for tracked aiming, or use a normal gamepad. |
| iPad/iPhone | Use a gamepad, keyboard and mouse/trackpad, or the automatic on-screen controls when no supported controller or physical keyboard is connected. Pause to reveal the app's Library and Settings controls. |

On Sense, **R2 fires**, **Square uses/reloads**, **Triangle swaps weapons**,
**R1 melees**, and **R3 zooms**. To throw a grenade, **hold L1, make a throwing
motion with the left hand, then release L1**. Settings include tracked aiming,
turning, gestures, dual wielding and presentation options. See the complete
[controller/Sense guide](CONTROLS.md) and [mobile input guide](../native/EnginePad/INPUT.md).

Graphics preferences are in Settings. Native resolution and display-maximum
refresh are available; actual performance depends on the device and scene.
Fully close and reopen the app after changing graphics settings. Mobile aspect
is selected at launch, so reopen after resizing a Stage Manager window.

## 7. Carry your progress between devices

Sign into the **same iCloud account** on your devices, enable iCloud Drive, and
build both apps with access to the same container from step 4. Your Xcode
developer team supplies signing; the iCloud account on the devices supplies
your personal cloud storage.

1. After playing, **fully close 117 and reopen it**. Stay in the launcher while
   Cloud Saves finishes. Returning to Library alone does not sync that session.
2. Open 117 on the next device and let its save check finish before selecting the
   game. If two devices have different progress, choose the save you want in
   **Settings → Cloud saves**; the app keeps recovery snapshots.
3. Supported app settings sync separately and apply before a new game session.
   Mobile quality preferences are shared between iPad and iPhone; headset
   calibration and local artwork remain device-specific. Halo profiles and
   saved control bindings travel with the game saves.

Cloud saves are **not live checkpoint streaming**. An explicitly chosen shared
folder in **Settings → Cloud saves** is another save-transfer option. Use the
same folder on both devices. This folder option transfers game saves; app-settings
sync still uses the configured iCloud container. For an existing campaign, make a backup before
migration and follow [save setup and migration](BUILDING.md#ipad-iphone-and-shared-cloud-saves).

## If something does not work

- **Setup fails:** start with its named failed stage and [setup troubleshooting](SETUP.md#resume-and-troubleshooting).
- **Signing fails:** check the selected team, unique app IDs, registered container,
  required capabilities and device inclusion. A successful build alone does not
  mean it was installed.
- **Controller tracking fails:** confirm both Sense controllers are paired, tracking
  permission is granted, and tracked aiming is enabled; see [controls](CONTROLS.md).
- **Unexpected saves:** stop at the launcher and inspect Cloud Saves before playing
  or restoring. Preserve a backup rather than deleting the app.

This is an active development project. Vision Pro gameplay has user testing;
the latest mobile gameplay and cross-device cloud roundtrip still need physical
acceptance. Read [known issues](KNOWN_ISSUES.md) and [validation](VALIDATION.md),
then [open an issue](https://github.com/zschwendi/117-vision/issues) with the device,
OS, build number and reproduction steps. Review logs before sharing them; keep
game files, registration, saves and signing material private.
