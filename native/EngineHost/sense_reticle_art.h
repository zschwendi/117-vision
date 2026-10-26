/* Original Halo HUD artwork, captured independently of the world/HUD. */
#ifndef HALO_SENSE_RETICLE_ART_H
#define HALO_SENSE_RETICLE_ART_H
#include <stdint.h>
#include "sense_weapon.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { HALO_SENSE_RETICLE_ART_SIZE = 512 };
typedef struct {
    uint32_t valid, weapon;
    /* Source rectangle and aim anchor in actual render-target pixels. */
    float bounds[4], anchor[2], viewport[2];
    float point[3];
} HaloSenseReticleArtHand;
typedef struct {
    uint64_t session, generation, source_epoch;
    HaloSenseReticleArtHand hands[2];
    /* Where this frame's centre pictures were drawn from: metres from the
     * reticle's eye (the unshifted camera) in its forward/left/up basis, as
     * the panorama hook shifted each eye. Bit e of eye_valid is set once eye
     * e's centre pass has been drawn; mono draws only eye 0, at zero. */
    uint32_t stereo, eye_valid;
    float eye_offset[2][3];
} HaloSenseReticleArtFrame;
/* Engine thread only. Targets belong to the currently reserved panorama slot;
 * their publication and leases follow that slot's existing GPU completion. */
void host_sense_reticle_art_targets(void *left, void *right);
int host_sense_reticle_art_begin(unsigned hand, uint32_t weapon,
                               const HaloSenseReticleFrame *targets,
                               float logical_anchor_x, float logical_anchor_y);
int host_sense_reticle_art_end(int completed);
void host_sense_reticle_art_frame(HaloSenseReticleArtFrame *out);
#ifdef __cplusplus
}
#endif
#endif
