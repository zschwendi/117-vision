# Image quality options

Settings → Image Quality (Sense-controller navigable) holds four independent
options. The defaults reproduce the earlier renderer exactly: FXAA on world
layers, no upscaling, no engine foveation, no GPU timing.

| Option | Applies | Default | Environment seed |
| --- | --- | --- | --- |
| Scene anti-aliasing: FXAA, SMAA, Off | live, world layers only | FXAA | `HALO_SCENE_AA=fxaa\|smaa\|off` (legacy `HALO_FXAA=0` = off) |
| Scene upscaling: Off, MetalFX 75/67/50 % | next launch, immersive play only | Off | `HALO_SCENE_UPSCALE=metalfx\|bilinear` with `HALO_SCENE_OUTPUT=WxH`, `HALO_SCENE_RENDER=WxH` |
| Engine foveation: Off, Balanced, Aggressive | live, immersive world passes | Off | `HALO_ENGINE_FOVEATION=off\|balanced\|aggressive` |
| Measure GPU time | live, diagnostic | Off | `HALO_GPU_TIMING=1` |

The settings page and the diagnostics report (`graphics` group) show what was
selected and what actually ran, with sizes. Both are published at each Present
and name what is on screen: an immersive world frame, the last one kept after
a dropped frame, or a flat frame shown as drawn (then no option runs).

## Dimensions per mode

* Immersive panorama, upscaling off: world layers, HUD layer and tracked
  reticle crops are all at the engine resolution ("Resolution at next launch").
* Immersive panorama, upscaling on: the engine renders the world at the
  percentage shown; MetalFX spatial (bilinear while it is being prepared or
  where unsupported, reported as such) scales each world layer to the chosen
  resolution; Halo's own HUD draws are rasterized at the chosen resolution.
  Tracked Sense reticle artwork is drawn into separate logical-size scratch
  targets and cropped there, so it stays at the engine resolution; its
  placement uses the same logical pixels, so aim alignment is unchanged.
* Flat menu/loading frames, the window view and iPad: the engine back buffer
  at its render size (with upscaling on, that is the smaller size). Upscaling
  is not applied there.
* Output is bounded by 2560 × 1920 pixels per layer, so the pool never needs
  more memory than the largest resolution offered without upscaling.

## Foveation

Rates come from the presenter's own composite (tests check the C model
against `EngineImmersiveScreenGeometry`). Balanced lowers only rows the caps
always cover in the centre and rear bearings (about 14 % of their pixels);
Aggressive also halves rates on the rear bearings and the caps. Every
foveated layer needs a resolve pass before it is published or sampled. On an
Apple M4 with synthetic content Balanced did not save GPU time and Aggressive
did (see the receipt); decide on the headset with "Measure GPU time".

## GPU timing

Stage-boundary timestamps on every renderer pass, attributed exclusively to
scene, HUD, post-processing and copies; frames are reported only when every
command buffer was fully sampled and completed. "Command buffers" shows both
the span from first start to last end and the sum of durations; neither is
compositor time (`immersiveGPUMilliseconds`).
