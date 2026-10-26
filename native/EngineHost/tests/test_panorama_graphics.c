/* Production panorama producer with the graphics port stubbed
 * (tests/panorama_graphics_stub.h): what the producer asks of the renderer
 * and the pool for anti-aliasing, upscaling, the HUD raster and foveation.
 *  - upscaled frames acquire pool slots and publish at the output size, the
 *    HUD raster is the output size, tracked reticle art stays logical;
 *  - one configuration per frame, kept for all its passes;
 *  - a failed HUD raster falls back and says so;
 *  - foveation maps are set for world passes and cleared after each;
 *  - the readback path (no sink) never upscales;
 *  - status is published only at Present: an upscaled panorama, then a flat
 *    frame, then a dropped frame, then a panorama again and a dropped one,
 *    each report exactly what is on screen.
 * No Metal or game files. clang -O2 -I.. tests/test_panorama_graphics.c -lm */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../panorama.h"
typedef struct { uint32_t guest,data,width,height,format,size; void *renderer; int gpu_dirty; } D3DObj;
/* The readback case copies the 96 x 72 x 4 backbuffer at guest 64.
 * Stub-created surfaces start at 1024 and can reach the 128 x 96 HUD
 * raster, so reserve that complete payload as well. */
enum { fixture_surface_base = 1024, fixture_max_width = 128, fixture_max_height = 96 };
static uint8_t memory[fixture_surface_base + fixture_max_width * fixture_max_height * 4];
#define GPTR(p) (memory+(p))
static D3DObj device={.data=1},backbuffer={.guest=1,.data=64,.width=4,.height=4,.size=64};
static D3DObj *the_device=&device;
static unsigned frames_presented;
static D3DObj *obj_from_guest(uint32_t guest){return guest==1?&backbuffer:NULL;}
static D3DObj *rt_surface(void){return &backbuffer;}
static int surface_prepare(D3DObj *s){if(!s->renderer)s->renderer=(void*)2;return 0;}
static int surface_flush(D3DObj *s){(void)s;return 0;}
static void mr_clear(void *r,uint32_t c){(void)r;(void)c;}
static void mr_clear_depth(void *r,float d){(void)r;(void)d;}
static void mr_destroy(void *r){(void)r;}
static uint32_t guest_page_alloc(uint32_t size){assert(size <= sizeof memory - fixture_surface_base);return fixture_surface_base;}
static int guest_page_free(uint32_t base){(void)base;return 1;}
static void host_log(const char *fmt,...){(void)fmt;}
#define HOST_ENV getenv
#define G8(addr) 0
static struct { int scope; } fp_capture;
static unsigned blits, filters;
static int mr_blit_target_to(void *a,void *b){(void)a;(void)b;blits++;return 0;}
static int mr_fxaa_target_to(void *a,void *b,float subpix){(void)a;(void)b;(void)subpix;filters++;return 0;}
#include "panorama_graphics_stub.h"
#include "../panorama_render.inc"

static uint32_t acquired_w, acquired_h; static unsigned acquires;
static int acquire(uint32_t w,uint32_t h,void **textures,int *slot){
    acquired_w=w;acquired_h=h;acquires++;*slot=1;for(int k=0;k<HALO_PANORAMA_LAYERS;k++)textures[k]=(void*)(uintptr_t)(16+k);return 1;
}
static void release(int slot){(void)slot;}
static uint32_t carry(int slot,uint32_t mask,HaloPanoramaInfo *metadata){(void)slot;(void)mask;(void)metadata;return 0;}
/* The d3d9.c Present boundary for each kind of frame. */
static void present(int kind){
    unsigned before=panorama_gfx_test.statuses;
    if(kind==HALO_PRESENTATION_PANORAMA)panorama_graphics_presented(kind,panorama_info.width,panorama_info.height);
    else panorama_graphics_presented(kind,backbuffer.width,backbuffer.height);
    assert(panorama_gfx_test.statuses==before+1);
    host_panorama_gpu_handoff();host_panorama_present_complete();
}
static void frame(unsigned mask){
    host_panorama_reset();
    for(int k=0;k<HALO_PANORAMA_LAYERS;k++)if(mask&(1u<<k)){
        host_panorama_begin(k);host_panorama_projection(1.6f,0.77f,0,0,(int)backbuffer.width,(int)backbuffer.height,0);host_panorama_end(k);
    }
}
int main(void){
    backbuffer.renderer=(void*)1;backbuffer.width=96;backbuffer.height=72;backbuffer.size=96*72*4;
    HaloPanoramaGPUSink sink={acquire,release,carry};
    host_panorama_set_gpu_sink(&sink);
    /* Upscaled: 96x72 engine, 128x96 presented. */
    panorama_gfx_test.upscale=HALO_UPSCALE_METALFX;panorama_gfx_test.output_width=128;panorama_gfx_test.output_height=96;
    panorama_gfx_test.scene_aa=HALO_SCENE_AA_SMAA;
    unsigned statuses=panorama_gfx_test.statuses;
    frame(HALO_PANORAMA_MONO_MASK);
    assert(panorama_gfx_test.statuses==statuses);   /* nothing published mid-frame */
    assert(panorama_info.valid&&panorama_info.width==128&&panorama_info.height==96);
    present(HALO_PRESENTATION_PANORAMA);
    assert(acquires==1&&acquired_w==128&&acquired_h==96);
    assert(panorama_gfx_test.configures==1);                 /* once per frame */
    assert(panorama_gfx_test.hud_rasters==1&&panorama_gfx_test.hud_raster_width==128&&panorama_gfx_test.hud_raster_height==96);
    assert(panorama_gfx_test.publishes==8&&panorama_gfx_test.hud_publishes==1);
    HaloGraphicsStatus *s=&panorama_gfx_test.status;
    assert(s->render_width==96&&s->render_height==72&&s->output_width==128&&s->output_height==96);
    assert(s->hud_width==128&&s->hud_height==96);
    assert(s->reticle_art_width==96&&s->reticle_art_height==72);   /* tracked reticle stays logical */
    assert(s->scene_aa_effective==HALO_SCENE_AA_SMAA&&s->upscale_effective==HALO_UPSCALE_METALFX&&s->world_layers==8);
    assert(s->presentation==HALO_PRESENTATION_PANORAMA&&!s->detail[0]);
    /* Adversarial: a flat menu/loading frame next. Nothing of the upscaled
     * panorama may still read as running. */
    host_panorama_reset();present(HALO_PRESENTATION_FLAT);
    assert(s->presentation==HALO_PRESENTATION_FLAT&&s->upscale_effective==HALO_UPSCALE_OFF&&s->scene_aa_effective==HALO_SCENE_AA_OFF);
    assert(s->foveation_effective==HALO_FOVEATION_OFF&&s->world_layers==0&&s->foveated_layers==0);
    assert(s->render_width==96&&s->output_width==96&&s->output_height==72&&s->hud_width==96&&s->hud_height==72);
    assert(s->reticle_art_width==0&&strstr(s->detail,"Flat frame"));
    /* A dropped world frame after the flat one: the flat frame superseded the
     * panorama, so there is nothing to keep. */
    host_panorama_reset();present(HALO_PRESENTATION_PANORAMA_RETAINED);
    assert(s->presentation==HALO_PRESENTATION_NONE&&s->output_width==0&&s->upscale_effective==HALO_UPSCALE_OFF);
    /* The next panorama is the active report again. */
    frame(HALO_PANORAMA_MONO_MASK);present(HALO_PRESENTATION_PANORAMA);
    assert(s->presentation==HALO_PRESENTATION_PANORAMA&&s->output_width==128&&s->hud_width==128&&s->upscale_effective==HALO_UPSCALE_METALFX);
    /* Dropped now: the panorama on screen is that one, and says so. */
    host_panorama_reset();present(HALO_PRESENTATION_PANORAMA_RETAINED);
    assert(s->presentation==HALO_PRESENTATION_PANORAMA_RETAINED&&s->output_width==128&&s->upscale_effective==HALO_UPSCALE_METALFX&&strstr(s->detail,"dropped"));
    /* The HUD raster cannot be made: published anyway, and reported. */
    panorama_gfx_test.hud_raster_fails=1;
    frame(HALO_PANORAMA_MONO_MASK);present(HALO_PRESENTATION_PANORAMA);
    assert(panorama_info.valid==0&&s->hud_width==96&&s->hud_height==72&&strstr(s->detail,"HUD resampled"));
    panorama_gfx_test.hud_raster_fails=0;
    /* Foveation: one map per world pass, cleared after each pass. */
    panorama_gfx_test.foveation=HALO_FOVEATION_BALANCED;panorama_gfx_test.foveation_supported=1;
    unsigned begins=panorama_gfx_test.world_begins,ends=panorama_gfx_test.world_ends,maps=panorama_gfx_test.world_maps;
    frame(HALO_PANORAMA_MONO_MASK);present(HALO_PRESENTATION_PANORAMA);
    assert(panorama_gfx_test.world_begins-begins==8&&panorama_gfx_test.world_maps-maps==8);
    assert(panorama_gfx_test.world_ends-ends>=8);
    assert(s->foveated_layers==8&&s->foveation_effective==HALO_FOVEATION_BALANCED&&fabsf(s->foveation_shading-0.75f)<1e-6f);
    /* Unsupported: selected but not run, reported as such. */
    panorama_gfx_test.foveation_supported=0;
    maps=panorama_gfx_test.world_maps;
    frame(HALO_PANORAMA_MONO_MASK);present(HALO_PRESENTATION_PANORAMA);
    assert(panorama_gfx_test.world_maps==maps&&s->foveation_effective==-1&&s->foveated_layers==0);
    /* The readback path: no sink, never upscaled, layers at the back buffer size. */
    host_panorama_set_gpu_sink(NULL);
    unsigned before=acquires;
    frame(HALO_PANORAMA_MONO_MASK);
    assert(acquires==before&&panorama_info.valid&&panorama_info.width==96&&panorama_info.height==72);
    present(HALO_PRESENTATION_PANORAMA);
    assert(panorama_gfx_test.hud_raster_width==96&&s->upscale_effective==HALO_UPSCALE_OFF&&s->scene_aa_effective==HALO_SCENE_AA_OFF);
    assert(s->presentation==HALO_PRESENTATION_PANORAMA&&s->hud_width==96);
    puts("PASS panorama graphics wiring: upscaled pool/publication/HUD raster at the output size, tracked reticle logical, "
         "one configuration per frame, HUD fallback reported, per-pass foveation set and cleared, unsupported reported, readback path unscaled, "
         "status only at Present: panorama -> flat -> dropped -> panorama -> dropped each report what is on screen");
    return 0;
}
