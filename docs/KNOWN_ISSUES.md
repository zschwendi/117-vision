# 117 current limitations

- Only the supported retail Halo PC 1.10 executable is implemented.
- Build 129 changes Vision Pro window/immersive transitions. System dismissal, rapid expand/return, headset sleep/wake and live window gameplay still need physical acceptance.
- Build 128 adds complete basic touch gameplay controls on iPhone/iPad when neither a supported controller nor a physical keyboard is connected. Touch layout/feel, hardware hot-plug, Magic Keyboard and pointer capture still need physical-device acceptance.
- Build 130 renders mobile gameplay at the landscape window’s launch aspect and corrects weapon crosshair proportions. Legacy menus, scope overlays and other HUD art retain Halo PC’s layout. Live Stage Manager aspect changes and quality changes require a fresh app launch; back buffers are not reset off-thread. Display maximum requests the supported rate, not a guaranteed sustained FPS.
- Settings sync is separate from save archives. Incoming app preferences apply only while the engine has never started in this process; after gameplay, reopen the app. The live iCloud roundtrip across physical devices still needs acceptance.
- Build 124 selects the highest launch graphics once per device family. Vision uses 2560 × 1920 per rendered view with scene upscaling off; mobile uses Native/Display maximum. If performance is poor, lower the settings and fully reopen the app; your lower choices are kept.
- Controller prompt replacement is limited to verified gameplay actions and controls. Unknown names, ambiguous bindings, unsupported controller mappings and the controls editor retain the original text.
- Save sync currently runs only before engine startup in a fresh process. After playing, fully close and reopen the app, then wait for sync before switching devices. Returning to the launcher alone is not a safe sync boundary.
- Grenades retain the game's original throw/keyframe delay.
- The held grenade preview disappears on release before the original throw finishes; the transition still needs headset tuning.
- Primary reload/switch animations take over the rig. The dual offhand reload remains tracked, and a primary reload in dual mode can show a moving magazine without its support hand.
- Some artificial vertical tilt can produce bank after physical turning; recenter clears it.
- The moving unzoomed sniper reticle trail and full weapon/vehicle coverage need ongoing hardware checks.
- A safe Stop/Quit may require reopening the app before starting another game session.

The upstream notes below are retained as historical context and may describe older builds.

---

# Known limitations

- Frame rate can fall in combat and busy rooms, including The Maw. A stable
  campaign-wide FPS target has not been established.
- Panorama seams, stale peripheral views, texture flashing, and some lighting
  differences remain under investigation.
- Audio interruptions have been reported. Synthetic mixer checks do not
  establish uninterrupted audible playback on a headset.
- The presenter uses a stereo forward view; the entire surrounding panorama
  is not full stereo.
- Complete campaign playthroughs, all checkpoints, extended sessions, and
  all controller/headset combinations have not been qualified.
- The supported executable is fixed by hash. Replacing it with another
  edition or patch can invalidate translation addresses and runtime hooks.
- Source checks and desktop GPU tests are separate from headset testing.
  An unsigned successful build is separate from provisioning and installation.

When reporting a problem, include the source version, app build identifier,
visionOS version, mission/checkpoint, reproduction steps, and whether it
occurs while stationary, turning the controller, or moving your head. Attach
only reviewed excerpts of diagnostics; omit game files and private identifiers.
