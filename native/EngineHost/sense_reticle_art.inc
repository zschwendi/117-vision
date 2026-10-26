/* Included after panorama_render.inc. Only an explicit original HUD call
 * redirects draws. No guest draw state, shader, UV, colour or blend changes. */
static D3DObj sense_art_scratch[2];
static void *sense_art_destinations[2];
static HaloSenseReticleArtFrame sense_art_frame;
static int sense_art_active = -1;
static uint32_t sense_art_draw_start;
static int sense_art_initialized[2];

void host_sense_reticle_art_targets(void *left, void *right) {
    /* Swift binds a complete pair. Partial allocation disables capture;
     * tracked draws stay hidden instead of jumping to the central HUD. */
    if(!left||!right)left=right=NULL;
    sense_art_destinations[0]=left;sense_art_destinations[1]=right;
    memset(&sense_art_frame,0,sizeof sense_art_frame);
    memset(sense_art_initialized,0,sizeof sense_art_initialized);
    sense_art_active=-1;
}
int host_sense_reticle_art_begin(unsigned hand,uint32_t weapon,
                               const HaloSenseReticleFrame *targets,float ax,float ay) {
    if(hand>1||sense_art_active>=0||!targets||!targets->session||!targets->generation||
       !targets->hands[hand].valid||targets->hands[hand].weapon!=weapon||!sense_art_destinations[hand]||panorama_pass!=1||
       !panorama_ui_active||!panorama_hud||host_panorama_gpu_slot()<0||
       !isfinite(ax)||!isfinite(ay))return 0;
    /* Original0051C9A0 maps the logical 640x480 HUD into the
     * active viewport, including D3D's half-pixel adjustment. */
    int hw=(int16_t)G16(0x007C125Au)-(int16_t)G16(0x007C1256u);
    int hh=(int16_t)G16(0x007C1258u)-(int16_t)G16(0x007C1254u);
    if(hw<=0||hh<=0||!draw_state.viewport[2]||!draw_state.viewport[3])return 0;
    ax=(float)draw_state.viewport[0]+ax*(float)draw_state.viewport[2]/640.f-(float)draw_state.viewport[2]/(2.f*hw);
    ay=(float)draw_state.viewport[1]+ay*(float)draw_state.viewport[3]/480.f-(float)draw_state.viewport[3]/(2.f*hh);
    const unsigned w=panorama_hud->width,h=panorama_hud->height;
    if(!w||!h||w>4096||h>4096||ax<0||ay<0||ax>w||ay>h)return 0;
    if(sense_art_frame.session&&(sense_art_frame.session!=targets->session||
       sense_art_frame.generation!=targets->generation))return 0;
    D3DObj *s=&sense_art_scratch[hand];
    if(!s->renderer||s->width!=w||s->height!=h){
        uint32_t data=guest_page_alloc(w*h*4u);if(!data)return 0;
        if(s->renderer)mr_destroy(s->renderer);
        if(s->data)guest_page_free(s->data);
        memset(s,0,sizeof *s);s->width=w;s->height=h;s->format=21;s->size=w*h*4u;s->data=data;s->gpu_dirty=1;
        if(surface_prepare(s))return 0;
        sense_art_initialized[hand]=0;
    }
    HaloSenseReticleArtHand *a=&sense_art_frame.hands[hand];
    if(!sense_art_initialized[hand]){
        mr_clear(s->renderer,0);mr_clear_depth(s->renderer,1.f);mr_set_overlay_target(s->renderer);
        memset(a,0,sizeof *a);a->weapon=weapon;
        a->anchor[0]=ax;a->anchor[1]=ay;a->viewport[0]=(float)w;a->viewport[1]=(float)h;
        a->bounds[0]=floorf(ax-HALO_SENSE_RETICLE_ART_SIZE*.5f);
        a->bounds[1]=floorf(ay-HALO_SENSE_RETICLE_ART_SIZE*.5f);
        a->bounds[2]=a->bounds[0]+HALO_SENSE_RETICLE_ART_SIZE;
        a->bounds[3]=a->bounds[1]+HALO_SENSE_RETICLE_ART_SIZE;
        sense_art_initialized[hand]=1;
    } else if(a->weapon!=weapon||a->anchor[0]!=ax||a->anchor[1]!=ay)return 0;
    sense_art_frame.session=targets->session;sense_art_frame.generation=targets->generation;
    sense_art_frame.source_epoch=panorama_info.source_epoch;
    memcpy(a->point,targets->hands[hand].point,sizeof a->point);
    sense_art_draw_start=draw_calls;sense_art_active=(int)hand;return 1;
}
static D3DObj *sense_art_surface(D3DObj *original) {
    return sense_art_active>=0&&original==panorama_hud?&sense_art_scratch[sense_art_active]:original;
}
static int sense_art_overlay(D3DObj *s) {
    return s==panorama_hud||s==&sense_art_scratch[0]||s==&sense_art_scratch[1];
}
int host_sense_reticle_art_end(int completed) {
    int hand=sense_art_active;sense_art_active=-1;if(hand<0||hand>1)return 0;
    HaloSenseReticleArtHand *a=&sense_art_frame.hands[hand];
    if(!completed){a->valid=0;return 0;}
    if(draw_calls==sense_art_draw_start)return 1; /* original hid this element */
    int ok=!mr_blit_target_region_to(sense_art_scratch[hand].renderer,sense_art_destinations[hand],
                                     (int)a->bounds[0],(int)a->bounds[1]);
    a->valid=ok;return ok;
}
void host_sense_reticle_art_frame(HaloSenseReticleArtFrame *out) {
    if(!out)return;
    *out=sense_art_frame;
    if(sense_art_active>=0||out->source_epoch!=panorama_info.source_epoch){memset(out,0,sizeof *out);return;}
    /* Frozen with the pixels: the centre eyes these same passes were drawn
     * from, never live settings read against an older picture. */
    out->eye_valid=panorama_centre_eyes(&out->stereo,out->eye_offset);
}
