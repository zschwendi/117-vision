/* Experimental tracked full body: the player's own third-person biped drawn in
 * the immersive first-person view, plus its shadow. Render-only: the body is
 * submitted through the original object list with a copied node palette; no
 * object, node, input, aim or save bytes are written. Default off. */
#ifndef HALO_SENSE_BODY_H
#define HALO_SENSE_BODY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Persisted user choice (default off). Thread-safe. */
void halo_sense_set_body_enabled(int enabled);
int halo_sense_body_enabled(void);

enum { HALO_SENSE_BODY_OFF=0, HALO_SENSE_BODY_UNAVAILABLE=1, HALO_SENSE_BODY_UNSUPPORTED=2,
       HALO_SENSE_BODY_UNKNOWN_RIG=3, HALO_SENSE_BODY_ARMS_WAITING=4, HALO_SENSE_BODY_ACTIVE=5 };
/* Timestamped result of the latest render epoch. No guest pointers. */
typedef struct { int state; uint64_t epoch; } HaloSenseBodyStatus;
void host_sense_body_status(HaloSenseBodyStatus *out);

/* Engine thread only. True when this render epoch draws the body with arms
 * solved to the accepted first-person wrists, so a connected first-person
 * upper-arm/forearm postpass must not draw a second pair. Latches the epoch's
 * single body snapshot if it was not latched yet. */
int host_sense_body_active(void);

#ifdef __cplusplus
}
#endif
#endif
