/* iPad/iPhone only. Halo's original bitmap HUD uses virtual 640x480 units,
 * so rendering a wide world also widens the crosshair unless its authored
 * horizontal size is compensated. Restrict this to the weapon crosshair's
 * exact caller; menus, text, scope overlays and damage flashes retain their
 * original layout. The original constructor/animation/anchor still run.
 * All guest access is synchronous on the engine thread; the tag is restored
 * even if the original dispatcher returns through an unexpected address. */
#ifndef HALO_MOBILE_RETICLE
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS && !(defined(TARGET_OS_VISION) && TARGET_OS_VISION)
#define HALO_MOBILE_RETICLE 1
#else
#define HALO_MOBILE_RETICLE 0
#endif
#endif
#if HALO_MOBILE_RETICLE
#include "mobile_graphics.h"
static int mobile_reticle_nested;
#endif
static int host_mobile_reticle_dispatch(EngineCPU *cpu, uint32_t address) {
#if HALO_MOBILE_RETICLE
    if (address != 0x004ACAD0u || mobile_reticle_nested || G32(cpu->gpr[4]) != 0x004B33C6u) return 0;
    int w = (int16_t)G16(0x007C125Au) - (int16_t)G16(0x007C1256u);
    int h = (int16_t)G16(0x007C1258u) - (int16_t)G16(0x007C1254u);
    if (!halo_mobile_size_allowed((uint32_t)w, (uint32_t)h) || w * 3 == h * 4) return 0;
    uint32_t tag = cpu->gpr[2];
    if (!tag || tag > UINT32_MAX - 12u) return 0;
    uint32_t saved = G32(tag + 4); float original;
    memcpy(&original, &saved, sizeof original);
    if (!isfinite(original) || original <= 0.f) return 0;
    float corrected = original * (4.f * (float)h) / (3.f * (float)w);
    if (!isfinite(corrected) || corrected <= 0.f) return 0;
    memcpy(GPTR(tag + 4), &corrected, sizeof corrected);
    mobile_reticle_nested = 1;
    engine_dispatch(cpu, address);
    mobile_reticle_nested = 0;
    S32(tag + 4, saved);
    return 1;
#else
    (void)cpu; (void)address; return 0;
#endif
}
