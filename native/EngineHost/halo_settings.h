#ifndef HALO_SETTINGS_H
#define HALO_SETTINGS_H
/* Live settings for the port's own presentation features, shared between the
 * engine thread that renders and the UI that adjusts them. Every field starts
 * from its environment variable so existing scripted runs behave as before,
 * and every field may then be changed while the game runs. Values are read
 * once per frame at well-defined points, never mid-pass, so a change can not
 * tear a frame between the two eyes. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Half the eye separation in Halo world units (its unit is ten feet).
     * Zero renders mono, which halves the world passes. */
    float stereo_separation;
    /* Vertical field of view of each panorama view, in radians. The
     * horizontal field comes from the projection rectangle, so this only
     * trades vertical resolution for vertical coverage. */
    float panorama_vfov;
    /* Scales synthesised controller haptics. Zero disables them. */
    float haptics_strength;
    /* Brightness of the surround that continues the picture past its edges. */
    float backdrop_brightness;
    /* Render the front-end menu through the panorama path instead of as a
     * flat panel, so its 3D backdrop surrounds the viewer. */
    int spatial_shell;
    /* The frame rate the bearing budget keeps: it draws more of the sphere
     * each frame while the engine's busy time stays under this, and less as
     * soon as it runs over. Halo's own cap is 30. */
    float panorama_target_fps;
    /* Decibels added to the sounds Halo places on the listener, which are
     * the player's own weapon, reload and melee. Halo hands those to
     * DirectSound twenty decibels down (the assault rifle at -2000 mB), and
     * in a headset firing that cannot be heard over the music is a fault. */
    float self_gain_db;
    /* With a menu up, the engine's cursor follows the gaze and a pinch is a
     * click (pointer.c). Off, menus take the pad and the mouse only. */
    int gaze_pointer;
    /* Turn each panorama layer drawn in an earlier engine frame by the
     * rotation from the camera it was drawn with to the newest centre
     * camera, so the joins meet while the player turns (EngineLayerAlignment
     * in the presenter). Presentation only; the engine draws the same.
     * Off unless HALO_LAYER_ALIGN=1 until a headset A/B supports it. */
    int layer_align;
    /* Optional frame pacing (0 off, 1 on). HALO_FRAME_PACING seeds this
     * setting; the controller uses 45/30/22.5/18/15 fps on a 90 Hz grid. */
    int frame_pacing;
} HaloSettings;

/* Where the viewer is looking inside the panorama, in radians, measured from
 * the recentred forward direction. The display thread owns this; the audio
 * mixer reads it so a sound stays with its object when only the head turns
 * and the game camera does not. Zero means looking straight ahead. */
void halo_settings_set_head(float yaw, float pitch);
float halo_settings_head_yaw(void);
float halo_settings_head_pitch(void);
/* How far the head is tilted about the line of sight, in radians, positive
 * when the right eye is the higher one. The two centre passes place their
 * cameras along this tilted baseline, so a near object such as the weapon
 * keeps corresponding between the eyes when the head rolls. */
void halo_settings_set_head_roll(float roll);
float halo_settings_head_roll(void);

/* Both are safe from any thread. get() never blocks. */
void halo_settings_get(HaloSettings *out);
void halo_settings_set(const HaloSettings *in);
/* Clamped accessors the renderer uses directly. */
float halo_settings_stereo_separation(void);
float halo_settings_panorama_vfov(void);
float halo_settings_haptics_strength(void);
float halo_settings_backdrop_brightness(void);
int halo_settings_spatial_shell(void);
float halo_settings_panorama_target_fps(void);
/* As a linear gain, for the mixer. */
float halo_settings_self_gain(void);
int halo_settings_gaze_pointer(void);
int halo_settings_layer_align(void);
/* Experimental bearing-invariant fog. Defaults off; only HALO_RADIAL_FOG=1
 * enables it. Cached once for the process, so all bearings and diagnostics
 * use the same effective setting even if the environment later changes. */
int halo_settings_radial_fog(void);
int halo_settings_frame_pacing(void);

/* ---- Graphics quality ------------------------------------------------------
 * Kept apart from HaloSettings, which the presentation panel writes whole, so
 * neither panel can reset the other's fields. Live values are read once per
 * engine frame. Each is independent; the defaults reproduce the renderer as
 * it was: FXAA on world layers, no foveation, no GPU timing, no upscaling. */
enum { HALO_SCENE_AA_OFF = 0, HALO_SCENE_AA_FXAA = 1, HALO_SCENE_AA_SMAA = 2 };
enum { HALO_FOVEATION_OFF = 0, HALO_FOVEATION_BALANCED = 1, HALO_FOVEATION_AGGRESSIVE = 2 };
enum { HALO_UPSCALE_OFF = 0, HALO_UPSCALE_METALFX = 1, HALO_UPSCALE_BILINEAR = 2 };
typedef struct {
    /* Scene anti-aliasing, HALO_SCENE_AA_*. Seeded by HALO_SCENE_AA=off|fxaa|smaa;
     * the older HALO_FXAA=0 still means off. */
    int scene_aa;
    /* Rasterization rate maps inside the engine's own world passes,
     * HALO_FOVEATION_*. Seeded by HALO_ENGINE_FOVEATION=off|balanced|aggressive. */
    int engine_foveation;
    /* GPU timestamps on the renderer's passes (diagnostic). HALO_GPU_TIMING=1. */
    int gpu_timing;
} HaloGraphicsSettings;
void halo_graphics_get(HaloGraphicsSettings *out);
void halo_graphics_set(const HaloGraphicsSettings *in);
int halo_graphics_scene_aa(void);
int halo_graphics_engine_foveation(void);
int halo_graphics_gpu_timing(void);

/* Scene upscaling is fixed for the process: the engine's back buffer is
 * chosen at launch. HALO_SCENE_UPSCALE=metalfx|bilinear, HALO_SCENE_OUTPUT=WxH
 * (the presented size of each view) and HALO_SCENE_RENDER=WxH (the engine's
 * back buffer the launcher chose for it). */
typedef struct {
    int mode;                                    /* HALO_UPSCALE_* */
    unsigned output_width, output_height;
    unsigned render_width, render_height;
} HaloSceneUpscale;
void halo_graphics_upscale(HaloSceneUpscale *out);
/* The presented size for a frame whose engine back buffer is render_w x
 * render_h: the launch output when the back buffer is the launch render
 * size, else the launch ratio applied (even sizes), never above
 * HALO_SCENE_OUTPUT_MAX_PIXELS. Returns 0 (and the render size) when the
 * frame is not upscaled. */
#define HALO_SCENE_OUTPUT_MAX_PIXELS (2560u * 1920u)
int halo_graphics_upscale_output(unsigned render_w, unsigned render_h, unsigned *out_w, unsigned *out_h);
/* The engine back buffer the launcher asks for to present output_w x
 * output_h with the scene at `percent` of it (75, 67 or 50): width a multiple
 * of 8, the output's aspect kept, never below 640 x 480. Returns 0 (and the
 * output size) when that would not be smaller. Shared by the launcher and
 * the settings panel so both name the same size. */
int halo_graphics_upscale_render_size(unsigned output_w, unsigned output_h, unsigned percent, unsigned *render_w, unsigned *render_h);

/* What the most recent Present showed, for the settings panel and the
 * report. Published only at the Present boundary, never mid-frame.
 * PANORAMA: this Present published an immersive world frame (values are
 * that frame's). PANORAMA_RETAINED: the world frame was dropped and the
 * presenter keeps the last complete panorama (values are that panorama's).
 * FLAT: a menu, loading or window frame, the engine back buffer shown as
 * drawn: no scene anti-aliasing, upscaling or foveation. NONE: nothing
 * presented yet, or a dropped frame with no panorama to keep. */
enum { HALO_PRESENTATION_NONE = 0, HALO_PRESENTATION_PANORAMA = 1, HALO_PRESENTATION_PANORAMA_RETAINED = 2,
       HALO_PRESENTATION_FLAT = 3 };
/* Selected values are what was asked for; effective values are what ran for
 * the presentation above (-1: nothing has run yet). */
typedef struct {
    int presentation;                            /* HALO_PRESENTATION_* */
    int scene_aa_selected, scene_aa_effective;
    int upscale_selected, upscale_effective;
    int foveation_selected, foveation_effective;
    unsigned render_width, render_height;        /* engine back buffer */
    unsigned output_width, output_height;        /* presented world layer */
    unsigned hud_width, hud_height;              /* HUD raster (the HUD layer) */
    /* Tracked (Sense) reticle artwork is redirected to its own scratch
     * targets at the logical HUD size and cropped from there, so it stays at
     * this raster even when the HUD layer is rasterized at the output size;
     * its placement uses the same logical pixels, so aim alignment holds. */
    unsigned reticle_art_width, reticle_art_height;
    unsigned world_layers, foveated_layers;      /* drawn this frame / with a rate map */
    float foveation_shading;                     /* mean shaded fraction over the foveated layers */
    unsigned long long frame;                    /* source epoch it describes */
    char detail[160];
} HaloGraphicsStatus;
void halo_graphics_status_publish(const HaloGraphicsStatus *status);
void halo_graphics_status_read(HaloGraphicsStatus *out);

#ifdef __cplusplus
}
#endif
#endif
