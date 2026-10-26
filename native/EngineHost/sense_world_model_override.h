/* Render-only redirection of one original world-model submission.
 *
 * Retail 004D6FC0 render_model (verified PC 1.10): EAX model tag datum, ECX
 * 52-byte node palette, cdecl [esp+04h] LOD .. [esp+18h] centre pointer ..
 * [esp+24h] salted object datum, [esp+2Ch] flags (2 shadow); caller pops 2Ch.
 * Object-list callers: 0050F044 (returns 0050F049) and the shadow pass
 * 0050F0B8 (returns 0050F0BD). 004D7071..004D70A7 multiplies every palette
 * node through [00696664] into the callee's own stack, so a scratch palette is
 * read only during the call. Each lane's prepare helper chooses ONE object by
 * its own salted identity and only fills scratch guest pointers; it never
 * touches registers, the stack or guest object bytes and never dispatches. */
#ifndef HALO_SENSE_WORLD_MODEL_OVERRIDE_H
#define HALO_SENSE_WORLD_MODEL_OVERRIDE_H
#include <stdint.h>
typedef struct {
    uint32_t palette; /* guest node palette for ECX; required for APPLY */
    uint32_t center;  /* guest real_point3d for [esp+18h]; 0 keeps the original */
} SenseWorldModelOverride;
enum {
    SENSE_WORLD_MODEL_DROP = -1, /* claimed but unverifiable: skip this draw, no original */
    SENSE_WORLD_MODEL_NONE = 0,  /* not this lane's object: untouched */
    SENSE_WORLD_MODEL_APPLY = 1  /* one original call with the override */
};
#endif
