/* Production capture state machine; real pixel/crop coverage is in the Metal
 * companion test. These checks focus on frame/weapon/hand isolation. */
#include "../host.h"
#include "../metalrenderer.h"
#include "../panorama.h"
#include "../sense_reticle_art.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
uint8_t *engine_flat_base;
typedef struct {uint32_t width,height,format,size,data;int gpu_dirty;mr_context *renderer;} D3DObj;
static D3DObj hud={.width=1920,.height=1080};
static D3DObj *panorama_hud=&hud;
static HaloPanoramaInfo panorama_info;
static int panorama_pass=1,panorama_ui_active=1,slot=0;
static uint32_t draw_calls;
static struct {uint32_t viewport[6];}draw_state;
static unsigned allocations,clears,crops;
static int crop_x,crop_y;
int host_panorama_gpu_slot(void){return slot;}
uint32_t guest_page_alloc(uint32_t n){assert(n==1920u*1080u*4u);return 0x10000u+4096u*++allocations;}
int guest_page_free(uint32_t p){assert(p);return 1;}
void mr_destroy(mr_context *c){assert(c);}
void mr_clear(mr_context *c,uint32_t color){assert(c&&color==0);clears++;}
void mr_clear_depth(mr_context *c,float value){assert(c&&value==1);}
void mr_set_overlay_target(mr_context *c){assert(c);}
int mr_blit_target_region_to(mr_context *c,void *t,int x,int y){assert(c&&t);crops++;crop_x=x;crop_y=y;return 0;}
static int surface_prepare(D3DObj *s){s->renderer=(mr_context*)(uintptr_t)s->data;return 0;}
static float eye_offsets[2][3]={{0,.0315f,-.004f},{0,-.0315f,.004f}};
static inline unsigned panorama_centre_eyes(uint32_t *stereo,float offset[2][3]){*stereo=1;memcpy(offset,eye_offsets,sizeof eye_offsets);return 3;}
#include "../sense_reticle_art.inc"
int main(void){
 engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);
 draw_state.viewport[2]=1920;draw_state.viewport[3]=1080;
 S16(0x7C1256,0);S16(0x7C1254,0);S16(0x7C125A,1920);S16(0x7C1258,1080);
 panorama_info.source_epoch=17;
 HaloSenseReticleFrame targets={.session=4,.generation=8};
 targets.hands[0].valid=targets.hands[1].valid=1;
 targets.hands[0].weapon=42;targets.hands[1].weapon=43;
 targets.hands[0].point[0]=7;targets.hands[1].point[0]=11;
 host_sense_reticle_art_targets((void*)1,(void*)2);
 assert(!host_sense_reticle_art_begin(1,42,&targets,320,240));
 assert(host_sense_reticle_art_begin(1,43,&targets,320,240));
 assert(sense_art_surface(&hud)==&sense_art_scratch[1]);
 assert(!host_sense_reticle_art_begin(0,42,&targets,320,240));
 draw_calls++;assert(host_sense_reticle_art_end(1));assert(crops==1&&crop_x==703&&crop_y==283);
 assert(sense_art_surface(&hud)==&hud);
 HaloSenseReticleArtFrame art;host_sense_reticle_art_frame(&art);
 assert(art.hands[1].valid&&art.hands[1].weapon==43&&art.hands[1].point[0]==11&&!art.hands[0].valid);
 assert(art.hands[1].anchor[0]==959.5f&&art.hands[1].anchor[1]==539.5f);
 assert(host_sense_reticle_art_begin(1,43,&targets,320,240));
 assert(host_sense_reticle_art_end(1));host_sense_reticle_art_frame(&art);
 assert(art.hands[1].valid&&crops==1&&clears==1); /* hidden next element retains prior art */
 assert(host_sense_reticle_art_begin(0,42,&targets,320,240));draw_calls++;assert(host_sense_reticle_art_end(1));
 host_sense_reticle_art_frame(&art);assert(art.hands[0].valid&&art.hands[0].point[0]==7&&art.hands[1].point[0]==11);
 /* The frame's own centre eyes travel with its pixels. */
 assert(art.stereo==1&&art.eye_valid==3&&!memcmp(art.eye_offset,eye_offsets,sizeof eye_offsets));
 targets.generation++;assert(!host_sense_reticle_art_begin(1,43,&targets,320,240));targets.generation--;
 panorama_info.source_epoch++;host_sense_reticle_art_frame(&art);assert(!art.session&&!art.stereo&&!art.eye_valid);
 host_sense_reticle_art_targets((void*)1,(void*)2);host_sense_reticle_art_frame(&art);assert(!art.hands[0].valid&&!art.hands[1].valid);
 assert(host_sense_reticle_art_begin(1,43,&targets,320,240));assert(!host_sense_reticle_art_end(0));host_sense_reticle_art_frame(&art);assert(!art.hands[1].valid);
 panorama_pass=2;assert(!host_sense_reticle_art_begin(1,43,&targets,320,240));panorama_pass=1;
 slot=-1;assert(!host_sense_reticle_art_begin(1,43,&targets,320,240));slot=0;
 host_sense_reticle_art_targets(NULL,(void*)2);assert(!host_sense_reticle_art_begin(1,43,&targets,320,240));
 host_sense_reticle_art_targets((void*)1,NULL);assert(!host_sense_reticle_art_begin(0,42,&targets,320,240));
 host_sense_reticle_art_targets(NULL,NULL);assert(!host_sense_reticle_art_begin(1,43,&targets,320,240));
 free(engine_flat_base);puts("PASS capture hand/weapon/session/frame isolation, original viewport anchor, hidden elements, abort/fallback boundaries and frame-frozen centre eyes");
}
