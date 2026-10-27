# 117

**Halo: Combat Evolved on Apple devices, with tracked VR controls on Vision Pro.**

117 is an independent, unofficial project built on [Master Chef](https://github.com/mitchaiet/master-chef). It extends that foundation with PSVR2 Sense aiming, physical interactions, dual wielding, an immersive interface, and a shared mobile/save system.

This repository contains source. Supply your own supported retail Halo PC installation and Apple signing. **Only Halo: Combat Evolved is implemented.** Halo 2, 3, Reach, and 4 are future interests, not supported games.

## Play

- **Vision Pro:** start in a window, then Expand into immersive play. Closing immersive returns to the same running game window. PSVR2 Sense controllers provide tracked weapon aiming; a normal gamepad remains supported.
- **iPad and iPhone:** a native Metal app with controller or keyboard/mouse input. Without a supported controller or physical keyboard, on-screen controls provide movement, aiming and the main gameplay actions. See the [mobile controls](native/EnginePad/INPUT.md).
- **Saves:** local saves plus optional iCloud sync, immutable recovery snapshots, and explicit conflict handling. After gameplay, fully close and reopen the app to sync before switching devices. See [save setup and migration](docs/BUILDING.md).

These are development builds. Physical iPad/iPhone gameplay and the live cross-device iCloud roundtrip have not yet been accepted. Source tests and compilation are separate from physical-device gameplay acceptance. See [validation](docs/VALIDATION.md) and [known issues](docs/KNOWN_ISSUES.md) for the tested boundaries.

## What this project adds

- PSVR2 Sense tracked aiming, support-hand aiming, and physical gestures.
- Optional dual wielding and native animation takeover for actions.
- Hand-directed grenade throws with strength scaled to Halo-like range.
- Smooth or snap turning, physical turning, recentering, and presentation settings.
- A touch- and controller-friendly launcher, controller status, and in-game settings.
- A shared iPad/iPhone app and cloud-save recovery/migration workflow.
- Mobile keyboard and mouse/trackpad controls, automatic touch gameplay controls, and native-resolution/display-refresh options.
- Gameplay prompts that name the active controller controls, with keyboard labels after keyboard or mouse input.
- Maximum graphics selected once on the first build 124 launch; later lower choices are kept.

## Build from your own game installation

```sh
git clone https://github.com/zschwendi/117-vision.git
cd 117-vision
./setup.sh --stage check --json
```

Continue with the [setup guide](docs/SETUP.md), [agent setup guide](docs/AGENT_SETUP.md), or [manual build guide](docs/BUILDING.md). The original supported executable is **retail Halo PC 1.10**. Xbox, Custom Edition, Anniversary, and MCC assets are not interchangeable with that executable.

For the iPad/iPhone target, see [mobile build instructions](native/EnginePad/README.md). Apple bundle identifiers, signing, and an iCloud container must belong to your developer account. Existing private installations must retain their identifiers to keep their data.

The builders also use roughly 0.9 GiB of checksum-pinned upstream visual packs; see [build dependencies and signing](docs/BUILDING.md). Those downloads are separate from this repository. [Graphics settings](docs/GRAPHICS_QUALITY.md) describe the rendering options.

The repository does not include the retail executable, maps, product registration, saves, generated executable translation, or signed apps. Existing upstream release assets are separate from this project's source release.

## Checks and contributions

```sh
python3 tools/run_source_checks.py --portable-only
```

On an Apple Silicon Mac, run the full source checks for native, Metal, and Swift coverage. Original-engine fixtures need locally generated code. Report your build number, mission/checkpoint, controller type, and steps to reproduce. Do not attach product keys, registry exports, signing files, or unreviewed diagnostic logs.

## Foundation and credits

This project started from **[mitchaiet/master-chef](https://github.com/mitchaiet/master-chef)**. Its executable translation, native runtime, Metal renderer, and original visionOS presentation are the foundation for this work. This is a separate development direction, not an official replacement for upstream.

The original MIT [license](LICENSE) and copyright notice are retained. See [THIRD_PARTY.md](THIRD_PARTY.md) for dependency notices and [asset notices](docs/ASSET_NOTICES.md) for material with separate rights. Original game content is not covered by this repository's MIT license.

Halo: Combat Evolved and related names/assets belong to their respective owners. 117 is not affiliated with or endorsed by Microsoft, Halo Studios, Bungie, Gearbox, Sony, or Apple.
