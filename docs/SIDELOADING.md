# Install 117 without Xcode

Download the [117 Build 130.1 visionOS preview](https://github.com/zschwendi/117-vision/releases/tag/visionos-v130.1).
This package contains the Build 130 runtime and uses the existing game-folder
importer. **It does not include Halo, your product registration, or a texture
mod pack.** You supply your own supported Halo PC installation.

You need Vision Pro on **visionOS 26 or later**, a gamepad, and the original
**retail Halo: Combat Evolved for PC, updated to 1.10**. Custom Edition, MCC,
Anniversary and Xbox disc images are not supported by this runtime.

## Install the IPA with Windows, Linux or a Mac

You need a Vision Pro, an Apple account, a computer for the initial setup, and
both devices on the same Wi-Fi. Xcode is not needed for this route.

1. Download the desktop installer from [Apparition](https://apparition.illixion.com/).
   Use its current instructions for your operating system. Windows also needs
   an anisette service for Apple sign-in; its [setup documentation](https://github.com/illixion/Apparition#anisette)
   explains the available and self-hosted options.
2. On Vision Pro, open **Settings → General → Remote Devices**. Select the
   headset in Apparition on your computer and enter the pairing code.
3. Follow Apparition's prompts to sign in with **your own Apple account** and
   install Apparition on the headset. Complete Apple's verification prompts
   yourself. Enable **Developer Mode** under **Privacy & Security** if prompted;
   the headset restarts. A developer-trust prompt may also appear.
4. Download this repo's **visionOS `.ipa`** from the release linked above. The
   automatically generated **Source code.zip** is not the app.
5. On the headset, open the IPA from Files with Apparition, or use
   **Library → Install from a File**. Let Apparition sign it for your account
   and install it. You can also use Apparition's desktop IPA installer.
6. Open the installed app from Home View, then supply your game files using
   the instructions below.

These are native visionOS builds. Do not use an iPhone/iPad IPA or a simulator
build. No JIT/debugger step is required for these ports.

## Keep the app working

With a free Apple account, apps normally need refreshing every **7 days** and
there is a **3-app limit**, including the sideloader. Use Apparition's refresh
feature before expiry. Its desktop companion or LocalDevVPN must be available
for refreshes; follow its guide for a Shortcuts automation. Paid developer
profiles have different limits and must support the app's capabilities.

Keep the same signing account and app identifier for updates. A new signer or
identifier can produce a separate app with separate saves. Back up first and
install updates over the existing app. **Do not uninstall to fix a refresh
problem.** These preview packages use local saves; the maintainer's iCloud
containers and development profiles are not included.

## If something goes wrong

- **It will not install:** check that you selected the visionOS IPA, enabled
  Developer Mode, completed trust prompts, and have a free signing slot.
- **It stopped opening after a week:** refresh its signature in Apparition.
- **There is no game / Play is unavailable:** installing the app does not
  supply your game files. Complete this repo's data steps below.
- **The sideloader rejects a capability:** do not blindly remove entitlements.
  Report the exact error and signing method, without account details.
- **A newer install has no saves:** check the signing account and identifier;
  preserve the old installation until its saves are backed up.

This route is an **experimental preview**. Package structure, code signatures
and contents were checked locally. A fresh Windows-to-headset installation,
free-account launch, game import, and save-preserving update have **not** been
validated end to end. The earlier development builds' gameplay results do not
establish those checks. Report reproducible problems through this repo's Issues.

## Prepare your Halo installation on Windows

1. Install your owned retail Halo PC copy and apply the retail PC 1.10 update.
   Enter your own valid key in its original installer. Do not post the key or
   your registry export in an issue or chat.
2. Export only Halo's installation key. In **Command Prompt** on 64-bit Windows:

   ```bat
   reg export "HKLM\SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Halo" "%USERPROFILE%\halo-install.reg"
   ```

   On 32-bit Windows, omit `WOW6432Node`. Keep this export private.
3. Download and extract this repository's source ZIP. Install Python 3.11 or
   newer. From the repository folder, convert the export for the importer:

   ```powershell
   py tools/prepare_sideload_registry.py --registry "$env:USERPROFILE\halo-install.reg" --output "C:\Games\Halo\halo-vision-registry.txt"
   ```

   Replace the Halo directory with your actual installation folder. The helper
   only writes the new private file you name, does not print its values, and
   refuses to overwrite an existing file. It does not activate Halo or supply
   registration for you.
4. Make that Halo folder available in the headset's Files app through your own
   file share or cloud storage. Apple's [Connect to Server guide](https://support.apple.com/guide/apple-vision-pro/connect-external-servers-tan7acdaa0e3/visionos)
   covers access from Files. Download/copy the folder locally before importing
   if your file provider cannot reliably stream the whole folder.

At the folder's top level, the importer expects `halo.exe`, `strings.dll`,
`maps/`, `shaders/`, and the new `halo-vision-registry.txt`. Keep all campaign
maps. The required executable SHA-256 is:

```text
c9acf0c469543283cfed6d7dc04ade976dbdfc7cb4532cf070386de169c19545
```

## Import on Vision Pro

Open **117**, go to its **Game Data** settings, choose **Import Halo PC Game
Folder**, and select the folder containing those files. Keep the app open
until copying and validation finish. Then start in the ordinary game window
with your controller; try immersive play after that works.

The importer copies files into app storage, so allow space for the source
folder and another copy. This preview omits the optional texture/shader
replacement packs. Those controls do not mean the packs are included. Your
own retail assets provide the game content.

## Package details and source builds

This is the original Build 130 executable with a packaging revision to **130.1**.
The app's compiled translation of the supported executable remains embedded;
retail files and registration are imported separately. The package removes
both `GamePayload` and `GamePayloadManifest.json` so the existing importer is
available, along with optional visual-mod resources and developer signing.

The transport signature is ad-hoc; Apparition must sign the IPA for you. The
release includes a file manifest and SHA-256 checksum. The matching public
source checkpoint is
[`cf548b4`](https://github.com/zschwendi/117-vision/commit/cf548b4af133cd40a6c652de501dfef242c8243c).
See [IPA-RELEASE.md](IPA-RELEASE.md) for packaging details and
[INSTALL.md](INSTALL.md) for the original Mac source-build route.
