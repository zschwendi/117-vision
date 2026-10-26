/* Semantic weapon haptics for the tracked Sense weapon path.
 *
 * Halo PC 1.10 has no rumble code to forward, so each accepted shot is mapped
 * to one tactile signature for its weapon. It is played on the controller of
 * the hand whose shot the game accepted; while the left hand supports a
 * rifle that hand receives a lighter echo. Values are Core Haptics intensity
 * and sharpness (0..1) and an optional continuous tail in seconds. Profiles
 * are keyed by tag path; an unrecognised path uses the default signature, so
 * a wrong or absent name only changes feel, never acceptance.
 *
 * Design reference for envelope-per-weapon and support echo (no code or
 * offsets copied): haloce_weapon_haptics.inl from moistman42069/MCCVR-Halo-Build
 * b243af2256a1a3b424cbccd940f8e8a674cef608 (MIT). That build reads MCC's
 * authored vibration envelopes, which retail Halo PC does not contain.
 *
 * Charge feedback (plasma pistol and any trigger the original charges) is
 * driven by the weapon's real per-trigger charge state 2/3 and its original
 * fraction (sense_weapon_feedback.inc reads the verified ABI); the held
 * trigger level is never the witness. Pure: no globals or clocks. */
#ifndef HALO_SENSE_WEAPON_HAPTICS_H
#define HALO_SENSE_WEAPON_HAPTICS_H
#include "gamecontroller.h"
#include <math.h>
#include <string.h>

typedef struct { const char *path; float intensity, sharpness, tail; } HaloWeaponHapticProfile;
/* Index 0 is the default. The first seven paths are the ones
 * sense_weapon_kind() already verifies; the last three are the stock
 * campaign names and fall back to the default if a map uses others. */
static const HaloWeaponHapticProfile halo_weapon_haptic_profiles[] = {
    {NULL,                                         .45f, .55f, 0.f},
    {"weapons\\assault rifle\\assault rifle",      .40f, .60f, 0.f},
    {"weapons\\pistol\\pistol",                    .60f, .85f, 0.f},
    {"weapons\\sniper rifle\\sniper rifle",        .95f, .90f, .06f},
    {"weapons\\shotgun\\shotgun",                  1.f,  .45f, .08f},
    {"weapons\\plasma rifle\\plasma rifle",        .30f, .35f, 0.f},
    {"weapons\\rocket launcher\\rocket launcher",  1.f,  .25f, .14f},
    {"weapons\\flamethrower\\flamethrower",        .25f, .15f, .05f},
    {"weapons\\plasma pistol\\plasma pistol",      .35f, .40f, 0.f},
    {"weapons\\needler\\needler",                  .30f, .70f, 0.f},
    {"weapons\\plasma_cannon\\plasma_cannon",      .90f, .20f, .12f},
};
enum { HALO_WEAPON_HAPTIC_PROFILES = sizeof halo_weapon_haptic_profiles / sizeof *halo_weapon_haptic_profiles };
/* Fraction of the firing hand's signature felt by a supporting left hand. */
#define HALO_WEAPON_HAPTIC_SUPPORT_SCALE .45f

static inline unsigned halo_weapon_haptic_profile(const char *path) {
    if (path) for (unsigned i = 1; i < HALO_WEAPON_HAPTIC_PROFILES; ++i)
        if (!strcmp(path, halo_weapon_haptic_profiles[i].path)) return i;
    return 0;
}
/* hand: 0 left, 1 right. supported applies only to a right-hand shot. A
 * non-positive or non-finite strength (the haptics setting) builds nothing. */
static inline int halo_weapon_haptic_event(unsigned profile, unsigned hand, int supported, float strength,
                                           uint64_t source_generation, HostGCSpatialHaptic *out) {
    memset(out, 0, sizeof *out);
    if (hand > 1 || !(strength > 0.f) || !isfinite(strength) || !source_generation) return 0;
    const HaloWeaponHapticProfile *p = &halo_weapon_haptic_profiles[profile < HALO_WEAPON_HAPTIC_PROFILES ? profile : 0];
    float intensity = fminf(1.f, p->intensity * strength);
    out->intensity[hand] = intensity; out->sharpness[hand] = p->sharpness; out->tail[hand] = p->tail;
    if (hand == 1 && supported) {
        out->intensity[0] = intensity * HALO_WEAPON_HAPTIC_SUPPORT_SCALE;
        out->sharpness[0] = p->sharpness * .8f;
    }
    out->source_generation = source_generation;
    out->kind = HOSTGC_SPATIAL_SHOT;
    return 1;
}

/* Charge pulses: bounded, periodic, tied to the engine's charge state.
 * Charging (2) ramps with the original fraction every
 * HALO_WEAPON_CHARGE_PERIOD_NS; reaching charged (3) gives one firm pulse,
 * then a sparse hold pulse. Any release (state leaves 2/3), identity change
 * (unit, weapon, session, generation, owner lease) or time reversal resets
 * and asks the caller to cancel queued charge pulses. Periods stay above the
 * link guard's default 250 ms spacing. */
#define HALO_WEAPON_CHARGE_PERIOD_NS UINT64_C(280000000)
#define HALO_WEAPON_CHARGE_HOLD_PERIOD_NS UINT64_C(700000000)
enum { HALO_WEAPON_CHARGE_IDLE = 0, HALO_WEAPON_CHARGE_PULSE = 1, HALO_WEAPON_CHARGE_CANCEL = 2 };
typedef struct {
    uint64_t session, generation, lease, last_ns;
    uint32_t unit, weapon;
    int state, active;
} HaloWeaponChargeHaptic;
static inline int halo_weapon_charge_step(HaloWeaponChargeHaptic *s, uint64_t session, uint64_t generation, uint64_t lease,
                                          uint32_t unit, uint32_t weapon, int state, float fraction, uint64_t now,
                                          float *intensity, float *sharpness) {
    *intensity = *sharpness = 0;
    int charging = (state == 2 || state == 3) && isfinite(fraction) && now && lease;
    int same = s->active && s->session == session && s->generation == generation && s->lease == lease &&
        s->unit == unit && s->weapon == weapon && now >= s->last_ns;
    if (s->active && (!charging || !same)) { memset(s, 0, sizeof *s); return HALO_WEAPON_CHARGE_CANCEL; }
    if (!charging) return HALO_WEAPON_CHARGE_IDLE;
    float f = fminf(1.f, fmaxf(0.f, fraction));
    int entering = !s->active, full = state == 3 && s->state != 3;
    uint64_t period = state == 3 ? HALO_WEAPON_CHARGE_HOLD_PERIOD_NS : HALO_WEAPON_CHARGE_PERIOD_NS;
    if (!entering && !full && now - s->last_ns < period) return HALO_WEAPON_CHARGE_IDLE;
    s->active = 1; s->session = session; s->generation = generation; s->lease = lease;
    s->unit = unit; s->weapon = weapon; s->state = state; s->last_ns = now;
    if (full) { *intensity = .55f; *sharpness = .8f; }
    else if (state == 3) { *intensity = .30f; *sharpness = .5f; }
    else { *intensity = .12f + .30f * f; *sharpness = .25f + .45f * f; }
    return HALO_WEAPON_CHARGE_PULSE;
}
#endif
