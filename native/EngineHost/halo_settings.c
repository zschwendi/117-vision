#include "halo_settings.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>

/* Floats are carried as atomic 32-bit words so no lock is needed on the
 * render path; each field is independent and a torn pair is harmless because
 * every value is read through its own clamped accessor. */
static _Atomic uint32_t field_separation, field_vfov, field_haptics, field_backdrop, field_target_fps, field_self_gain_db;
static _Atomic uint32_t field_head_yaw, field_head_pitch, field_head_roll;
static _Atomic int field_spatial_shell, field_gaze_pointer, field_layer_align, field_frame_pacing;
static _Atomic int initialised;

/* This experimental render switch is immutable for the process. Keep its
 * publication independent of the live settings so simultaneous diagnostic
 * and renderer reads cannot see different values during initialization. */
static pthread_once_t radial_fog_once = PTHREAD_ONCE_INIT;
static int radial_fog_enabled;

static void initialize_radial_fog(void) {
    const char *value = getenv("HALO_RADIAL_FOG");
    radial_fog_enabled = value && strcmp(value, "1") == 0;
}

int halo_settings_radial_fog(void) {
    pthread_once(&radial_fog_once, initialize_radial_fog);
    return radial_fog_enabled;
}

static uint32_t pack(float v) { uint32_t w; memcpy(&w, &v, 4); return w; }
static float unpack(uint32_t w) { float v; memcpy(&v, &w, 4); return v; }

/* Match the Float radians produced by the presentation controls at exactly
 * 60 and 175 degrees. Rounded decimal limits rejected both endpoints and
 * silently replaced them with the 150-degree fallback. */
static int valid_panorama_vfov(float value) {
    return value >= 1.0471975511965977462f && value <= 3.0543261909900767596f;
}

static double env_number(const char *name, double fallback, double low, double high) {
    const char *text = getenv(name);
    if (!text || !text[0]) return fallback;
    double value = atof(text);
    return (value >= low && value <= high) ? value : fallback;
}

static void ensure(void) {
    if (atomic_load_explicit(&initialised, memory_order_acquire)) return;
    /* Defaults match the documented environment behaviour. */
    const char *stereo = getenv("HALO_STEREO");
    double millimetres = env_number("HALO_STEREO_IPD", 63.0, 40.0, 90.0);
    double separation = millimetres / 1000.0 / 2.0 / 3.048;
    separation = env_number("HALO_STEREO_SEPARATION", separation, 0.0, 1.0);
    if (stereo && stereo[0] == '0') separation = 0.0;
    atomic_store_explicit(&field_separation, pack((float)separation), memory_order_relaxed);
    /* Rectilinear projection spreads a wide vertical field thinly: at 150
     * degrees the centre of view gets under 3 pixels per degree, which reads
     * as a soft, low-resolution image. 105 degrees nearly triples that for
     * the same pixels, and the cap beyond it is faded rather than shown. */
    double degrees = env_number("HALO_PANORAMA_VFOV", 105.0, 60.0, 175.0);
    atomic_store_explicit(&field_vfov, pack((float)(degrees * 3.14159265358979 / 180.0)), memory_order_relaxed);
    const char *haptics = getenv("HALO_HAPTICS");
    atomic_store_explicit(&field_haptics, pack(haptics && haptics[0] == '0' ? 0.f : 1.f), memory_order_relaxed);
    atomic_store_explicit(&field_backdrop, pack((float)env_number("HALO_BACKDROP", 1.0, 0.0, 2.0)), memory_order_relaxed);
    const char *shell = getenv("HALO_SPATIAL_MENU");
    atomic_store_explicit(&field_spatial_shell, shell && shell[0] == '0' ? 0 : 1, memory_order_relaxed);
    atomic_store_explicit(&field_target_fps, pack((float)env_number("HALO_PANORAMA_TARGET_FPS", 30.0, 10.0, 60.0)), memory_order_relaxed);
    atomic_store_explicit(&field_self_gain_db, pack((float)env_number("HALO_SELF_GAIN_DB", 18.0, 0.0, 24.0)), memory_order_relaxed);
    const char *gaze = getenv("HALO_GAZE_POINTER");
    /* Head-follow for the menu cursor, off unless HALO_GAZE_POINTER=1: the
     * eyes choose (the system's gaze ray at the pinch, its hover glow between). */
    atomic_store_explicit(&field_gaze_pointer, gaze && gaze[0] == '1' ? 1 : 0, memory_order_relaxed);
    /* Layer alignment, off unless HALO_LAYER_ALIGN=1 (or the settings
     * toggle). It has only been checked on synthetic pictures. While the
     * stick turns it also bends the side bands at eye level (the weapon
     * zone's blend) and sharpens the trailing join's cross-fade. It stays off
     * until a b30 stick-turn A/B on the headset shows the joins improve
     * without those side effects. */
    const char *align = getenv("HALO_LAYER_ALIGN");
    atomic_store_explicit(&field_layer_align, align && align[0] == '1' ? 1 : 0, memory_order_relaxed);
    /* Aggregate headset traces cannot establish a clear per-frame win.
     * Keep experimental pacing opt-in, including after malformed input. */
    const char *paced = getenv("HALO_FRAME_PACING");
    int pacing = paced && (!strcmp(paced, "1") || !strcmp(paced, "even") || !strcmp(paced, "on"));
    atomic_store_explicit(&field_frame_pacing, pacing, memory_order_relaxed);
    atomic_store_explicit(&initialised, 1, memory_order_release);
}

void halo_settings_get(HaloSettings *out) {
    if (!out) return;
    ensure();
    out->stereo_separation = unpack(atomic_load_explicit(&field_separation, memory_order_relaxed));
    out->panorama_vfov = unpack(atomic_load_explicit(&field_vfov, memory_order_relaxed));
    out->haptics_strength = unpack(atomic_load_explicit(&field_haptics, memory_order_relaxed));
    out->backdrop_brightness = unpack(atomic_load_explicit(&field_backdrop, memory_order_relaxed));
    out->spatial_shell = atomic_load_explicit(&field_spatial_shell, memory_order_relaxed);
    out->panorama_target_fps = unpack(atomic_load_explicit(&field_target_fps, memory_order_relaxed));
    out->self_gain_db = unpack(atomic_load_explicit(&field_self_gain_db, memory_order_relaxed));
    out->gaze_pointer = atomic_load_explicit(&field_gaze_pointer, memory_order_relaxed);
    out->layer_align = atomic_load_explicit(&field_layer_align, memory_order_relaxed);
    out->frame_pacing = atomic_load_explicit(&field_frame_pacing, memory_order_relaxed);
}

void halo_settings_set(const HaloSettings *in) {
    if (!in) return;
    ensure();
    float separation = in->stereo_separation;
    if (!(separation >= 0.f) || separation > 1.f) separation = 0.f;
    float vfov = in->panorama_vfov;
    if (!valid_panorama_vfov(vfov)) vfov = 2.6179939f;   /* 60..175 degrees */
    float haptics = in->haptics_strength;
    if (!(haptics >= 0.f) || haptics > 4.f) haptics = 1.f;
    float backdrop = in->backdrop_brightness;
    if (!(backdrop >= 0.f) || backdrop > 2.f) backdrop = 1.f;
    float target = in->panorama_target_fps;
    if (!(target >= 10.f) || target > 60.f) target = 27.f;
    atomic_store_explicit(&field_target_fps, pack(target), memory_order_relaxed);
    float self_db = in->self_gain_db;
    if (!(self_db >= 0.f) || self_db > 24.f) self_db = 18.f;
    atomic_store_explicit(&field_self_gain_db, pack(self_db), memory_order_relaxed);
    atomic_store_explicit(&field_separation, pack(separation), memory_order_relaxed);
    atomic_store_explicit(&field_vfov, pack(vfov), memory_order_relaxed);
    atomic_store_explicit(&field_haptics, pack(haptics), memory_order_relaxed);
    atomic_store_explicit(&field_backdrop, pack(backdrop), memory_order_relaxed);
    atomic_store_explicit(&field_spatial_shell, in->spatial_shell ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&field_gaze_pointer, in->gaze_pointer ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&field_layer_align, in->layer_align ? 1 : 0, memory_order_relaxed);
    atomic_store_explicit(&field_frame_pacing, in->frame_pacing == 1, memory_order_relaxed);
}

float halo_settings_stereo_separation(void) { ensure(); float v = unpack(atomic_load_explicit(&field_separation, memory_order_relaxed)); return (v >= 0.f && v <= 1.f) ? v : 0.f; }
float halo_settings_panorama_vfov(void) { ensure(); float v = unpack(atomic_load_explicit(&field_vfov, memory_order_relaxed)); return valid_panorama_vfov(v) ? v : 2.6179939f; }
float halo_settings_haptics_strength(void) { ensure(); float v = unpack(atomic_load_explicit(&field_haptics, memory_order_relaxed)); return (v >= 0.f && v <= 4.f) ? v : 1.f; }
float halo_settings_backdrop_brightness(void) { ensure(); float v = unpack(atomic_load_explicit(&field_backdrop, memory_order_relaxed)); return (v >= 0.f && v <= 2.f) ? v : 1.f; }
int halo_settings_spatial_shell(void) { ensure(); return atomic_load_explicit(&field_spatial_shell, memory_order_relaxed); }
int halo_settings_gaze_pointer(void) { ensure(); return atomic_load_explicit(&field_gaze_pointer, memory_order_relaxed); }
int halo_settings_layer_align(void) { ensure(); return atomic_load_explicit(&field_layer_align, memory_order_relaxed); }
int halo_settings_frame_pacing(void) { ensure(); return atomic_load_explicit(&field_frame_pacing, memory_order_relaxed); }
float halo_settings_panorama_target_fps(void) { ensure(); float v = unpack(atomic_load_explicit(&field_target_fps, memory_order_relaxed)); return (v >= 10.f && v <= 60.f) ? v : 27.f; }
float halo_settings_self_gain(void) { ensure(); float db = unpack(atomic_load_explicit(&field_self_gain_db, memory_order_relaxed)); if (!(db >= 0.f && db <= 24.f)) db = 18.f; return powf(10.f, db / 20.f); }

/* The pose is not a setting, so it is neither seeded from the environment nor
 * carried in HaloSettings; it starts at zero and is replaced every frame. */
void halo_settings_set_head(float yaw, float pitch) {
    if (!isfinite(yaw)) yaw = 0.f;
    if (!isfinite(pitch)) pitch = 0.f;
    atomic_store_explicit(&field_head_yaw, pack(yaw), memory_order_relaxed);
    atomic_store_explicit(&field_head_pitch, pack(pitch), memory_order_relaxed);
}
float halo_settings_head_yaw(void) { float v = unpack(atomic_load_explicit(&field_head_yaw, memory_order_relaxed)); return isfinite(v) ? v : 0.f; }
float halo_settings_head_pitch(void) { float v = unpack(atomic_load_explicit(&field_head_pitch, memory_order_relaxed)); return isfinite(v) ? v : 0.f; }
void halo_settings_set_head_roll(float roll) {
    if (!isfinite(roll)) return;
    if (roll > 1.5f) roll = 1.5f;
    if (roll < -1.5f) roll = -1.5f;
    atomic_store_explicit(&field_head_roll, pack(roll), memory_order_relaxed);
}
float halo_settings_head_roll(void) { float v = unpack(atomic_load_explicit(&field_head_roll, memory_order_relaxed)); return isfinite(v) && v >= -1.5f && v <= 1.5f ? v : 0.f; }

/* ---- Graphics quality ------------------------------------------------------ */
static _Atomic int graphics_aa = -1, graphics_foveation = -1, graphics_timing = -1;
static pthread_once_t graphics_once = PTHREAD_ONCE_INIT;
static pthread_once_t upscale_once = PTHREAD_ONCE_INIT;
static HaloSceneUpscale upscale_setting;
static pthread_mutex_t graphics_status_lock = PTHREAD_MUTEX_INITIALIZER;
static HaloGraphicsStatus graphics_status = {
    .scene_aa_effective = -1, .upscale_effective = -1, .foveation_effective = -1 };

static int env_choice(const char *name, const char *const *names, int count, int fallback) {
    const char *text = getenv(name);
    if (!text || !text[0]) return fallback;
    for (int i = 0; i < count; i++) if (!strcmp(text, names[i])) return i;
    return fallback;
}
static void initialize_graphics(void) {
    static const char *const aa[] = { "off", "fxaa", "smaa" };
    static const char *const foveation[] = { "off", "balanced", "aggressive" };
    const char *fxaa = getenv("HALO_FXAA");
    int scene = fxaa && fxaa[0] == '0' ? HALO_SCENE_AA_OFF : HALO_SCENE_AA_FXAA;
    atomic_store_explicit(&graphics_aa, env_choice("HALO_SCENE_AA", aa, 3, scene), memory_order_relaxed);
    atomic_store_explicit(&graphics_foveation, env_choice("HALO_ENGINE_FOVEATION", foveation, 3, HALO_FOVEATION_OFF), memory_order_relaxed);
    const char *timing = getenv("HALO_GPU_TIMING");
    atomic_store_explicit(&graphics_timing, timing && timing[0] == '1', memory_order_relaxed);
}
static int clamp_choice(int value, int high, int fallback) { return value >= 0 && value <= high ? value : fallback; }
void halo_graphics_get(HaloGraphicsSettings *out) {
    if (!out) return;
    out->scene_aa = halo_graphics_scene_aa();
    out->engine_foveation = halo_graphics_engine_foveation();
    out->gpu_timing = halo_graphics_gpu_timing();
}
void halo_graphics_set(const HaloGraphicsSettings *in) {
    if (!in) return;
    pthread_once(&graphics_once, initialize_graphics);
    atomic_store_explicit(&graphics_aa, clamp_choice(in->scene_aa, HALO_SCENE_AA_SMAA, HALO_SCENE_AA_FXAA), memory_order_relaxed);
    atomic_store_explicit(&graphics_foveation, clamp_choice(in->engine_foveation, HALO_FOVEATION_AGGRESSIVE, HALO_FOVEATION_OFF), memory_order_relaxed);
    atomic_store_explicit(&graphics_timing, in->gpu_timing ? 1 : 0, memory_order_relaxed);
}
int halo_graphics_scene_aa(void) {
    pthread_once(&graphics_once, initialize_graphics);
    return clamp_choice(atomic_load_explicit(&graphics_aa, memory_order_relaxed), HALO_SCENE_AA_SMAA, HALO_SCENE_AA_FXAA);
}
int halo_graphics_engine_foveation(void) {
    pthread_once(&graphics_once, initialize_graphics);
    return clamp_choice(atomic_load_explicit(&graphics_foveation, memory_order_relaxed), HALO_FOVEATION_AGGRESSIVE, HALO_FOVEATION_OFF);
}
int halo_graphics_gpu_timing(void) {
    pthread_once(&graphics_once, initialize_graphics);
    return atomic_load_explicit(&graphics_timing, memory_order_relaxed) == 1;
}

static int parse_size(const char *text, unsigned *w, unsigned *h) {
    if (!text) return 0;
    char tail = 0;
    if (sscanf(text, "%ux%u%c", w, h, &tail) != 2) return 0;
    return *w >= 64 && *h >= 64 && *w <= 8192 && *h <= 8192;
}
static void initialize_upscale(void) {
    static const char *const modes[] = { "off", "metalfx", "bilinear" };
    HaloSceneUpscale u = { env_choice("HALO_SCENE_UPSCALE", modes, 3, HALO_UPSCALE_OFF), 0, 0, 0, 0 };
    if (u.mode != HALO_UPSCALE_OFF &&
        (!parse_size(getenv("HALO_SCENE_OUTPUT"), &u.output_width, &u.output_height) ||
         !parse_size(getenv("HALO_SCENE_RENDER"), &u.render_width, &u.render_height) ||
         u.output_width < u.render_width || u.output_height < u.render_height ||
         (u.output_width == u.render_width && u.output_height == u.render_height) ||
         (unsigned long long)u.output_width * u.output_height > HALO_SCENE_OUTPUT_MAX_PIXELS))
        memset(&u, 0, sizeof u);   /* incomplete or impossible: off, never a guess */
    upscale_setting = u;
}
void halo_graphics_upscale(HaloSceneUpscale *out) {
    if (!out) return;
    pthread_once(&upscale_once, initialize_upscale);
    *out = upscale_setting;
}
int halo_graphics_upscale_output(unsigned render_w, unsigned render_h, unsigned *out_w, unsigned *out_h) {
    HaloSceneUpscale u; halo_graphics_upscale(&u);
    unsigned w = render_w, h = render_h;
    int scaled = 0;
    if (u.mode != HALO_UPSCALE_OFF && render_w && render_h) {
        if (render_w == u.render_width && render_h == u.render_height) { w = u.output_width; h = u.output_height; }
        else {
            /* Halo's Video Setup changed the mode: keep the launch ratio. */
            double sx = (double)u.output_width / u.render_width, sy = (double)u.output_height / u.render_height;
            double fw = render_w * sx, fh = render_h * sy;
            double cap = sqrt((double)HALO_SCENE_OUTPUT_MAX_PIXELS / (fw * fh));
            if (cap < 1.0) { fw *= cap; fh *= cap; }   /* bounded pool memory, aspect kept */
            w = (unsigned)(fw / 2.0) * 2u; h = (unsigned)(fh / 2.0) * 2u;
        }
        scaled = w > render_w && h > render_h && w <= 8192 && h <= 8192 &&
                 (unsigned long long)w * h <= HALO_SCENE_OUTPUT_MAX_PIXELS;
        if (!scaled) { w = render_w; h = render_h; }
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return scaled;
}

int halo_graphics_upscale_render_size(unsigned output_w, unsigned output_h, unsigned percent, unsigned *render_w, unsigned *render_h) {
    unsigned w = output_w, h = output_h;
    int smaller = 0;
    if (output_w >= 64 && output_h >= 64 && output_w <= 8192 && output_h <= 8192 && percent >= 25 && percent < 100) {
        w = (unsigned)((double)output_w * percent / 100.0 / 8.0 + 0.5) * 8u;
        if (w < 640) w = 640;
        h = (unsigned)((double)w * output_h / output_w / 2.0 + 0.5) * 2u;
        if (h < 480) { h = 480; w = (unsigned)((double)h * output_w / output_h / 8.0 + 0.5) * 8u; }
        smaller = w < output_w && h < output_h;
        if (!smaller) { w = output_w; h = output_h; }
    }
    if (render_w) *render_w = w;
    if (render_h) *render_h = h;
    return smaller;
}

void halo_graphics_status_publish(const HaloGraphicsStatus *status) {
    if (!status) return;
    pthread_mutex_lock(&graphics_status_lock); graphics_status = *status; pthread_mutex_unlock(&graphics_status_lock);
}
void halo_graphics_status_read(HaloGraphicsStatus *out) {
    if (!out) return;
    pthread_mutex_lock(&graphics_status_lock); *out = graphics_status; pthread_mutex_unlock(&graphics_status_lock);
    out->scene_aa_selected = halo_graphics_scene_aa();
    out->foveation_selected = halo_graphics_engine_foveation();
    HaloSceneUpscale u; halo_graphics_upscale(&u); out->upscale_selected = u.mode;
}

#include "sense_pose.inc"
