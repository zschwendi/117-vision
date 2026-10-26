/* panorama_graphics.h - what the panorama producer decides about image
 * quality for one engine frame, and what each layer's publication did.
 * Pure C. The producer (panorama_render.inc) talks to the renderer and the
 * settings only through the panorama_gfx_* port in panorama_graphics.inc, so
 * producer tests can substitute it (define PANORAMA_GRAPHICS_PORT). */
#ifndef PANORAMA_GRAPHICS_H
#define PANORAMA_GRAPHICS_H
#include <stdint.h>
#include "halo_settings.h"
#include "panorama_foveation.h"

typedef struct {
    int configured;
    /* The engine's back buffer and the layer size published to the pool.
     * They differ only when the scene is upscaled. */
    uint32_t source_width, source_height, output_width, output_height;
    int aa;                     /* HALO_SCENE_AA_* asked for */
    int upscale;                /* HALO_UPSCALE_* in effect (OFF without a GPU sink) */
    int foveation;              /* HALO_FOVEATION_* in effect (OFF where unsupported) */
    int foveation_selected;
    int foveation_supported;
} PanoramaGraphicsFrame;

typedef struct {
    int aa;                     /* HALO_SCENE_AA_* that ran */
    int upscale;                /* HALO_UPSCALE_* that ran (OFF: same size) */
    int foveated;               /* the layer was drawn through a rate map */
} PanoramaGraphicsLayer;

#endif
