# Mobile app (iPad and iPhone)

This target runs the same translated retail Halo PC 1.10 engine as the Vision Pro
app, using one flat Metal view and the existing Apple GameController bridge.
It requires iPadOS/iOS 18 or later. Play with an extended game controller,
a hardware keyboard with mouse, trackpad or touch aiming, or the automatic on-screen controls ([INPUT.md](INPUT.md)). iPad and
iPhone share one mobile bundle identifier. The original author’s defaults are
`com.zephyrstudios.mastercheef.ipad` and the
`iCloud.com.zephyrstudios.mastercheef` save container. New developers must
configure their own identifiers as described below. The visible name is
`117` (`--display-name` changes only that). The launcher uses a centered
shelf on iPad, and the cover beside its notice
in compact-height iPhone landscape. While the Library or an app sheet is
visible, the app holds the native controller-input lease so controller presses
cannot reach Halo; gameplay releases it. Owned game data
and the verified texture/shader packs come from the existing private `.setup`
inputs.

The launcher includes game-data preparation, graphics options, controller status,
the native About page, and the shared Files cloud-save interface. The in-game
Settings button keeps these controls accessible during play. Graphics changes
apply when the app is fully closed and reopened. The original 4:3 game picture
is fitted to the display; the app does not stretch the HUD. Build 124 selects
Native resolution and Display maximum once on its first launch. Any lower
settings chosen afterward are retained. The actual frame rate depends on
the device and scene.

## Build

After the existing setup readiness check and engine generation:

```sh
./setup.sh --stage check --json
python3 tools/build_engine_ipad.py --preflight
python3 tools/build_engine_ipad.py --typecheck
python3 tools/build_engine_ipad.py
```

The builder targets the device SDK directly and uses at most two compiler jobs.
It records per-source hashes, including every generated C body included by each
chunk, and reuses only matching iOS object files. Included-input hashes are
checked after compilation and before the final receipt. A changed body invalidates
its owning chunk even when the chunk wrapper is unchanged. It also verifies the
game manifest and visual packs, and uses APFS copies for private resources.
Default output is `.setup/MasterCheefMobile/direct-build`; the builder refuses the
historical `.setup/MasterCheefIPadCloud-20261020` evidence folder. Versions come
from `project.yml` (`--marketing-version`/`--build-number` override them).
`--typecheck` needs only Swift sources and headers; `--preflight` exits non-zero
and names any missing generated engine input. It never contacts a device or
simulator. Successful compilation and signing do not prove gameplay.

For local development signing, supply an explicitly selected matching profile
and signing identity. First replace the original author's bundle and cloud
identifiers using [the shared container setup](../../docs/BUILDING.md#configure-your-own-icloud-container).

```sh
python3 tools/build_engine_ipad.py \
  --bundle-id YOUR_BUNDLE_IDENTIFIER \
  --profile /path/to/your.mobileprovision \
  --identity YOUR_SIGNING_IDENTITY \
  --device-udid YOUR_IPAD_UDID --device-udid YOUR_IPHONE_UDID
```

The profile must cover the app identifier and every requested device, and authorize
`com.apple.developer.kernel.extended-virtual-addressing` and
`com.apple.developer.kernel.increased-memory-limit`. The engine reserves a
4 GiB virtual guest address space and a 1.5 GiB worker stack. A valid signature
with those capabilities still needs a physical-device memory and gameplay check,
separately on each iPhone model.

An Xcode target is also available:

```sh
xcodegen generate --spec native/EnginePad/project.yml --project native/EnginePad
```

Select the user's existing Apple development team in Xcode before building.
Keep signed apps, provisioning files, registration data, and generated engine
sources private. No release publication is part of these commands.

## Saves

The configured shared iCloud container supports automatic reconciliation before
Play. An explicitly chosen folder in **Settings → Cloud saves** is an alternative;
choose the same folder on both devices. Recovery snapshots are versioned and
conflicting progress requires an explicit choice. Manual restore requires
confirmation. Close and reopen
the app before a backup or restore, leaving the engine unstarted. Merely
returning to Library or pressing Stop does not make a running session safe to
replace. Import and game startup are blocked while a save operation is active.

Until Play is pressed, the launcher also watches the iCloud folder. A Vision Pro
snapshot that arrives late, a download that outlasts the launch check, or an
iCloud account change triggers the same idle reconciliation without reopening.
Monitoring stops when Halo starts; nothing is captured from a running session.

Source builds do not wait for a Vision Pro save, so a new player can start
immediately. A personal migration build passes `--require-vision-seed`; see
docs/BUILDING.md for the migration handoff.

The service copies only Halo's writable player-data subtree, excluding the game
installation and registration. Cloud transfer completion and restore on the
second physical device must be checked separately.
