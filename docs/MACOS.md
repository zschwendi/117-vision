# 117 on Apple Silicon Mac

Download the [native macOS preview](https://github.com/zschwendi/117-vision/releases/tag/macos-v130.1-preview).
It is a ZIP containing **117.app**, for **Apple Silicon / arm64 and macOS 26.0 or newer**. It is separate from the iOS/iPadOS and Vision Pro IPAs; it does not support Intel Macs, Windows or Linux.

1. Compare the ZIP's SHA-256 with SHA256SUMS, then extract it.
2. Put the app in a location you choose without overwriting an existing copy you want to keep.
3. Open it. This preview is ad-hoc signed and is **not notarized**. If macOS blocks a known, verified download, use **System Settings → Privacy & Security → Open Anyway** for this app, following [Apple’s instructions](https://support.apple.com/en-us/102445). Do not disable Gatekeeper globally.
4. Supply your own matching game files as described below. No sideloader, Apple developer account or device pairing is needed to launch the Mac app.

This is an experimental native desktop build. Architecture, dependency linkage, signature, archive integrity and input checks are separate from full gameplay acceptance. The native engine presented three frames and exited cleanly in a bounded startup check using an isolated working copy. Launcher input validation also passed. This packaged Mac preview has not passed an end-to-end game session.

## Prepare your working game folder

Use your own supported retail Halo PC 1.10 installation and registration. Follow [Prepare your Halo installation on Windows](SIDELOADING.md#prepare-your-halo-installation-on-windows) to create the private halo-vision-registry.txt file. The folder needs halo.exe, strings.dll, maps/, shaders/ and that registration file. Custom Edition, MCC and Xbox data do not match this engine.

Make a **separate writable working copy** on the Mac. The native host writes saves and game settings into its selected working tree, including updates to its registration seed. Do not point it at your only original installation, a read-only mounted ISO or an incomplete cloud placeholder.

Open 117 and select the working folder. The launcher verifies the retail 1.10 executable hash and required paths before starting the native game engine. It does not create or supply registration. Keep that working folder when updating the app; back it up before testing a newer preview.

## Desktop-preview limits

This packages the project's existing native macOS host with a small folder-selection launcher. It does not include the iPhone/iPad/visionOS SwiftUI settings, mobile cloud-save migration or immersive UI. It contains the compiled engine but excludes retail files, registration and optional texture/shader replacement packs. Current campaign, rendering, audio and controller behavior need fresh Mac gameplay testing.

The local engine log is Library/Logs/117-macOS-preview.log. Review/redact logs before posting; keep your game files, registration and saved profiles private.

Maintainers build the reviewed generated inputs with MACOSX_DEPLOYMENT_TARGET=26.0 make -C native/EngineHost -j2 and package the resulting halo-host using tools/package_macos.py. The source and generated-input hashes are recorded with the release; the generated C and private game data are not source-release assets.
