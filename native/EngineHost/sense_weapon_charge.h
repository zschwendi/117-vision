/* Weapon trigger charge, read exactly as the retail 1.10 getter computes it.
 *
 * Verified ABI (coordinator-abi.json; original 004C3100 getter, 004C3BC0
 * fully-charged writer, 004C2EE0 state checks): trigger t of a weapon object
 * keeps an int8 state at body+0x261+0x28t (2 charging, 3 charged) and an
 * int16 timer at body+0x262+0x28t. The weapon definition's trigger block
 * (int32 count at +0x4FC, pointer at +0x500, 0x114 bytes per trigger) holds
 * the charging time as a float at +0x48. The original fraction is
 *   charging: [0x672AC4] - timer * [0x672ACC] / charging time
 *   charged:  [0x672AC4]
 * with both constants read from the loaded image, never assumed. Callers
 * validate the object, its owner and the definition tag first. Reads only;
 * any non-finite or out-of-range value reports "not charging". The strongest
 * charging trigger wins (charged over charging, then larger fraction). */
#ifndef HALO_SENSE_WEAPON_CHARGE_H
#define HALO_SENSE_WEAPON_CHARGE_H
#include "host.h"
#include <math.h>

static inline int sense_charge_range(uint64_t p, uint64_t n) {
    return p >= 0x10000u && p <= UINT32_MAX && n <= UINT64_C(0x100000000) - p;
}
static inline int sense_weapon_charge_read(uint32_t body, uint32_t definition, int *state, float *fraction) {
    *state = 0; *fraction = 0;
    if (!sense_charge_range(body, 0x262u + 0x28u * 2u) || !sense_charge_range(definition, 0x504)) return 0;
    int32_t count = (int32_t)G32(definition + 0x4FC); uint32_t triggers = G32(definition + 0x500);
    if (count <= 0 || count > 2 || !sense_charge_range(triggers, (uint64_t)count * 0x114u)) return 0;
    float full, tick; memcpy(&full, GPTR(0x00672AC4u), 4); memcpy(&tick, GPTR(0x00672ACCu), 4);
    if (!isfinite(full) || !isfinite(tick) || tick <= 0.f) return 0;
    for (int t = 0; t < count; t++) {
        int s = (int8_t)G8(body + 0x261u + 0x28u * (unsigned)t);
        if (s != 2 && s != 3) continue;
        float f = full;
        if (s == 2) {
            float charging; memcpy(&charging, GPTR(triggers + 0x114u * (unsigned)t + 0x48u), 4);
            int timer = (int16_t)G16(body + 0x262u + 0x28u * (unsigned)t);
            if (!isfinite(charging) || charging <= 0.f) continue;
            f = full - (float)timer * tick / charging;
        }
        if (!isfinite(f)) continue;
        f = fminf(1.f, fmaxf(0.f, f));
        if (s > *state || (s == *state && f > *fraction)) { *state = s; *fraction = f; }
    }
    return *state != 0;
}
#endif
