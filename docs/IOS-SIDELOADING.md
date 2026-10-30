# Install 117 on iPhone or iPad

Download [117-iOS-130.1.ipa](https://github.com/zschwendi/117-vision/releases/tag/ios-v130.1-preview).
This **one iOS IPA supports both iPhone and iPad** (native arm64, device families 1 and 2). Its minimum OS is **iOS/iPadOS 18.0**; that minimum does not guarantee enough RAM on every older device. Use the separate [Vision Pro guide](SIDELOADING.md) for visionOS.

**Paid developer signing is required for this package's Extended Virtual Addressing entitlement.** Increased Memory Limit is also preserved. Do not remove either capability to force an install. Apparition's [capability notes](https://github.com/illixion/Apparition#what-it-does) explain the paid-account requirement. These downloads are experimental; a fresh recipient's re-sign, install, import and gameplay have not been validated.

## Install from Windows or Linux

1. Download the desktop installer from [Apparition's official releases](https://github.com/illixion/Apparition/releases/latest). The [existing guide](SIDELOADING.md#windows-walkthrough) covers Windows startup and sign-in troubleshooting.
2. Connect the iPhone or iPad by **USB**, unlock it and accept **Trust This Computer** locally. Windows may need Apple's **Apple Devices** app/drivers; Linux needs **usbmuxd** running. Select the device in Apparition. There is no headset-style Remote Devices pairing code. See [Apparition's pairing instructions](https://github.com/illixion/Apparition).
3. Sign with **your own paid developer identity** and a profile covering this device and the app's required memory capabilities. Apparition supports Apple Account signing or importing your own certificate/profile. Never send your password, certificate or private key to the project maintainer. Certificate signing avoids the anisette sign-in dependency.
4. Install the downloaded **iOS IPA**. For managed refresh, first install Apparition on the phone/tablet, connect its companion, then use its Library's **Install from File**. A direct desktop installation must be renewed from the desktop unless separately imported into the managed library.
5. Follow any Developer Mode/trust prompts locally, then open the game normally. These iOS ports do not need **Launch with JIT**.

Keep the same signing identity and bundle identifier for updates; back up saves and refresh before the profile expires. Do not uninstall to solve a signing error.

## Add your owned Halo files

Prepare your supported **retail Halo PC 1.10** installation and your own registration using [these Windows instructions](SIDELOADING.md#prepare-your-halo-installation-on-windows). Keep the registration export private. The prepared folder must contain halo.exe, strings.dll, maps/, shaders/ and halo-vision-registry.txt; Custom Edition, MCC and Xbox data are not compatible.

Make the folder available locally in the iPhone/iPad's Files app. In 117, open **Game Data → Import Halo PC Game Folder**, select it, and leave the app open until validation finishes. Allow space for the source and imported copy. Use local saves; this package does not require a Vision Pro save migration. A controller is recommended, and the app also has its existing touch controls.

## What is in this preview

This is the existing physical iOS **Build 130** runtime repackaged as **130.1**, using [public source checkpoint cf548b4](https://github.com/zschwendi/117-vision/commit/cf548b4af133cd40a6c652de501dfef242c8243c). All 47 non-signature Mach-O sections match the original device binary. Bundled retail files, registration and optional texture/shader replacement packs are removed so the folder importer is available. The private Vision Pro save-seeding requirement is disabled; developer signing and iCloud capabilities are removed. The compiled engine remains embedded.

The release contains a complete file manifest and SHA256SUMS. On Windows, compare Get-FileHash -Algorithm SHA256 on the downloaded IPA with that checksum; on Linux, use sha256sum -c SHA256SUMS. Maintainer recipe: [sideload-ios.json](../packaging/sideload-ios.json), consumed by tools/package_sideload.py with --recipe packaging/sideload-ios.json.
