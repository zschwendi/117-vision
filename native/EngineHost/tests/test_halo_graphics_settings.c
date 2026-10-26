/* Graphics settings in halo_settings.c: environment seeds and the legacy
 * HALO_FXAA=0, clamping of live values, the launch-time upscale (complete
 * and possible or off), the presented size for other back buffers (launch
 * ratio, bounded memory), the launcher's render sizes, and the status
 * publication. Each process reads its seeds once, so the cases fork.
 * clang -O2 -I.. tests/test_halo_graphics_settings.c ../halo_settings.c -lm */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../halo_settings.h"

static void in_child(void (*body)(void)) {
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) { body(); _exit(0); }
    int status = 0; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void defaults(void) {
    assert(halo_graphics_scene_aa() == HALO_SCENE_AA_FXAA && halo_graphics_engine_foveation() == HALO_FOVEATION_OFF);
    assert(!halo_graphics_gpu_timing());
    HaloSceneUpscale u; halo_graphics_upscale(&u); assert(u.mode == HALO_UPSCALE_OFF);
    unsigned w, h; assert(!halo_graphics_upscale_output(2048, 1536, &w, &h) && w == 2048 && h == 1536);
    HaloGraphicsStatus s; halo_graphics_status_read(&s);
    assert(s.scene_aa_effective == -1 && s.upscale_effective == -1 && s.foveation_effective == -1);
    assert(s.scene_aa_selected == HALO_SCENE_AA_FXAA && s.upscale_selected == HALO_UPSCALE_OFF);
}
static void legacy_fxaa_off(void) { setenv("HALO_FXAA", "0", 1); assert(halo_graphics_scene_aa() == HALO_SCENE_AA_OFF); }
static void seeded(void) {
    setenv("HALO_FXAA", "0", 1); setenv("HALO_SCENE_AA", "smaa", 1);   /* the explicit choice wins */
    setenv("HALO_ENGINE_FOVEATION", "aggressive", 1); setenv("HALO_GPU_TIMING", "1", 1);
    assert(halo_graphics_scene_aa() == HALO_SCENE_AA_SMAA && halo_graphics_engine_foveation() == HALO_FOVEATION_AGGRESSIVE);
    assert(halo_graphics_gpu_timing());
    HaloGraphicsSettings g = { 7, -3, 5 }; halo_graphics_set(&g);   /* out of range: safe values */
    assert(halo_graphics_scene_aa() == HALO_SCENE_AA_FXAA && halo_graphics_engine_foveation() == HALO_FOVEATION_OFF && halo_graphics_gpu_timing());
    g = (HaloGraphicsSettings){ HALO_SCENE_AA_OFF, HALO_FOVEATION_BALANCED, 0 }; halo_graphics_set(&g);
    HaloGraphicsSettings r; halo_graphics_get(&r);
    assert(r.scene_aa == HALO_SCENE_AA_OFF && r.engine_foveation == HALO_FOVEATION_BALANCED && !r.gpu_timing);
}
static void upscale(void) {
    setenv("HALO_SCENE_UPSCALE", "metalfx", 1); setenv("HALO_SCENE_OUTPUT", "2048x1536", 1); setenv("HALO_SCENE_RENDER", "1536x1152", 1);
    HaloSceneUpscale u; halo_graphics_upscale(&u);
    assert(u.mode == HALO_UPSCALE_METALFX && u.output_width == 2048 && u.render_width == 1536);
    unsigned w, h;
    assert(halo_graphics_upscale_output(1536, 1152, &w, &h) && w == 2048 && h == 1536);
    /* Video Setup chose another mode: the launch ratio (4/3). */
    assert(halo_graphics_upscale_output(1200, 900, &w, &h) && w == 1600 && h == 1200);
    /* Far larger: bounded by HALO_SCENE_OUTPUT_MAX_PIXELS, aspect kept. */
    assert(halo_graphics_upscale_output(2400, 1800, &w, &h));
    assert((unsigned long long)w * h <= HALO_SCENE_OUTPUT_MAX_PIXELS && w > 2400 && h > 1800 && abs((int)(w * 3) - (int)(h * 4)) <= 8);
    /* Already at the cap: not upscaled at all. */
    assert(!halo_graphics_upscale_output(2560, 1920, &w, &h) && w == 2560 && h == 1920);
}
static void upscale_invalid(void) {
    setenv("HALO_SCENE_UPSCALE", "metalfx", 1); setenv("HALO_SCENE_OUTPUT", "4096x3072", 1); setenv("HALO_SCENE_RENDER", "2048x1536", 1);
    HaloSceneUpscale u; halo_graphics_upscale(&u); assert(u.mode == HALO_UPSCALE_OFF);   /* above the memory bound */
}
static void upscale_incomplete(void) {
    setenv("HALO_SCENE_UPSCALE", "metalfx", 1); setenv("HALO_SCENE_OUTPUT", "2048x1536", 1); unsetenv("HALO_SCENE_RENDER");
    HaloSceneUpscale u; halo_graphics_upscale(&u); assert(u.mode == HALO_UPSCALE_OFF);
}
static void render_sizes(void) {
    const unsigned table[][5] = {
        { 2048, 1536, 75, 1536, 1152 }, { 2048, 1536, 67, 1376, 1032 }, { 2048, 1536, 50, 1024, 768 },
        { 2560, 1920, 75, 1920, 1440 }, { 1280, 960, 50, 640, 480 }, { 1280, 960, 67, 856, 642 } };
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; ++i) {
        unsigned w, h; assert(halo_graphics_upscale_render_size(table[i][0], table[i][1], table[i][2], &w, &h));
        assert(w == table[i][3] && h == table[i][4]);
    }
    unsigned w, h;
    assert(!halo_graphics_upscale_render_size(640, 480, 50, &w, &h) && w == 640 && h == 480);   /* never below 640x480 */
    assert(!halo_graphics_upscale_render_size(2048, 1536, 100, &w, &h));
    HaloGraphicsStatus s; memset(&s, 0, sizeof s);
    s.scene_aa_effective = HALO_SCENE_AA_SMAA; s.render_width = 1536; s.hud_width = 2048; s.reticle_art_width = 1536;
    snprintf(s.detail, sizeof s.detail, "x");
    halo_graphics_status_publish(&s);
    HaloGraphicsStatus r; halo_graphics_status_read(&r);
    assert(r.scene_aa_effective == HALO_SCENE_AA_SMAA && r.hud_width == 2048 && r.reticle_art_width == 1536 && !strcmp(r.detail, "x"));
}
int main(void) {
    in_child(defaults); in_child(legacy_fxaa_off); in_child(seeded); in_child(upscale);
    in_child(upscale_invalid); in_child(upscale_incomplete); in_child(render_sizes);
    puts("PASS graphics settings: defaults reproduce the old renderer, HALO_FXAA=0 and explicit seeds, clamping, "
         "complete/bounded launch upscale or off, launch ratio for other modes with a memory cap, launcher sizes, status");
    return 0;
}
