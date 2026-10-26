/* Semantic weapon haptic profiles and charge pulses (sense_weapon_haptics.h),
 * the audio path standing down for tracked shots without losing world cues
 * (haptics.c), and the real Sense bridge settings and owner lease
 * (halo_settings.c / sense_pose.inc). No controller, engine or audio device.
 * clang -O2 -I native/EngineHost native/EngineHost/tests/test_weapon_haptics.c \
 *   native/EngineHost/haptics.c native/EngineHost/halo_settings.c -lm */
#include "../sense_weapon_haptics.h"
#include "../haptics.h"
#include "../halo_settings.h"
#include "../sense_pose.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
enum { RATE = 48000 };
static float silence[2 * 24000], thump[2 * 2400];
/* The onset path needs ~45 ms of rendered audio since the last event. */
static void settle(void) { halo_haptics_observe(silence, 24000, RATE); halo_haptics_take(NULL, NULL); }
static int onset(float level, int at_listener) {
    settle(); halo_haptics_voice_onset(level, level / 12.f, at_listener); return halo_haptics_take(NULL, NULL);
}
static int blast(void) {
    settle(); halo_haptics_observe(thump, 2400, RATE); return halo_haptics_take(NULL, NULL);
}
static uint64_t generation(void) { HaloSenseStatus s; halo_sense_status(&s); return s.generation; }

int main(void) {
    unsetenv("HALO_SENSE_WEAPON"); unsetenv("HALO_HAPTICS");
    for (unsigned i = 0; i < 2400; i++) thump[2 * i] = thump[2 * i + 1] = .9f * sinf(2.f * 3.14159265f * 40.f * i / RATE);

    /* Profiles by tag path; anything else is the default signature. */
    CHECK(halo_weapon_haptic_profile("weapons\\assault rifle\\assault rifle") == 1);
    CHECK(halo_weapon_haptic_profile("weapons\\sniper rifle\\sniper rifle") == 3);
    CHECK(halo_weapon_haptic_profile("weapons\\rocket launcher\\rocket launcher") == 6);
    CHECK(halo_weapon_haptic_profile("weapons\\plasma pistol\\plasma pistol") == 8);
    CHECK(halo_weapon_haptic_profile("weapons\\pistol\\pistol ") == 0 && halo_weapon_haptic_profile(NULL) == 0);
    for (unsigned i = 0; i < HALO_WEAPON_HAPTIC_PROFILES; i++) {
        const HaloWeaponHapticProfile *p = &halo_weapon_haptic_profiles[i];
        CHECK(p->intensity > 0 && p->intensity <= 1 && p->sharpness >= 0 && p->sharpness <= 1 && p->tail >= 0 && p->tail <= .5f);
    }
    /* Firing hand only; a supported right-hand shot echoes lighter on the left. */
    HostGCSpatialHaptic e;
    CHECK(halo_weapon_haptic_event(1, 1, 0, 1, 9, &e) && e.intensity[1] == .40f && e.intensity[0] == 0 && e.source_generation == 9);
    CHECK(e.kind == HOSTGC_SPATIAL_SHOT);
    CHECK(halo_weapon_haptic_event(1, 1, 1, 1, 9, &e) && e.intensity[1] == .40f);
    CHECK(fabsf(e.intensity[0] - .40f * HALO_WEAPON_HAPTIC_SUPPORT_SCALE) < 1e-6f && e.intensity[0] < e.intensity[1] && e.tail[0] == 0);
    CHECK(halo_weapon_haptic_event(2, 0, 1, 1, 9, &e) && e.intensity[0] == .60f && e.intensity[1] == 0); /* offhand: no echo */
    CHECK(halo_weapon_haptic_event(6, 1, 0, 2, 9, &e) && e.intensity[1] == 1 && e.tail[1] > 0);           /* strength clamps */
    CHECK(halo_weapon_haptic_event(99, 1, 0, 1, 9, &e) && e.intensity[1] == halo_weapon_haptic_profiles[0].intensity);
    CHECK(!halo_weapon_haptic_event(1, 1, 0, 0, 9, &e) && !halo_weapon_haptic_event(1, 1, 0, NAN, 9, &e));
    CHECK(!halo_weapon_haptic_event(1, 2, 0, 1, 9, &e) && !halo_weapon_haptic_event(1, 1, 0, 1, 0, &e));
    CHECK(e.intensity[0] == 0 && e.intensity[1] == 0);

    /* Charge step: entry pulse, bounded period ramp, one full pulse, sparse
     * hold, cancel on release/identity/lease/time reversal/non-finite. */
    HaloWeaponChargeHaptic c = {0}; float in, sh; uint64_t t = 1000000000;
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 0, 0, t, &in, &sh) == HALO_WEAPON_CHARGE_IDLE);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, 0, t, &in, &sh) == HALO_WEAPON_CHARGE_PULSE && fabsf(in - .12f) < 1e-6f);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, .5f, t + 270000000, &in, &sh) == HALO_WEAPON_CHARGE_IDLE);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, .5f, t + 280000000, &in, &sh) == HALO_WEAPON_CHARGE_PULSE && fabsf(in - .27f) < 1e-6f);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, 7.f, t + 560000000, &in, &sh) == HALO_WEAPON_CHARGE_PULSE && fabsf(in - .42f) < 1e-6f);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 3, 1, t + 570000000, &in, &sh) == HALO_WEAPON_CHARGE_PULSE && in == .55f);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 3, 1, t + 1200000000, &in, &sh) == HALO_WEAPON_CHARGE_IDLE);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 3, 1, t + 1270000000, &in, &sh) == HALO_WEAPON_CHARGE_PULSE && in == .30f);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 0, 0, t + 1280000000, &in, &sh) == HALO_WEAPON_CHARGE_CANCEL && !c.active);
    uint64_t deltas[6][5] = {{2,1,5,7,8},{1,2,5,7,8},{1,1,6,7,8},{1,1,5,9,8},{1,1,5,7,9},{1,1,5,7,8}};
    for (unsigned n = 0; n < 6; n++) {
        memset(&c, 0, sizeof c);
        CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, 0, t, &in, &sh) == HALO_WEAPON_CHARGE_PULSE);
        uint64_t when = n == 5 ? t - 1 : t + 1;
        CHECK(halo_weapon_charge_step(&c, deltas[n][0], deltas[n][1], deltas[n][2], (uint32_t)deltas[n][3], (uint32_t)deltas[n][4],
                                      2, .1f, when, &in, &sh) == HALO_WEAPON_CHARGE_CANCEL);
    }
    memset(&c, 0, sizeof c);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 5, 7, 8, 2, NAN, t, &in, &sh) == HALO_WEAPON_CHARGE_IDLE);
    CHECK(halo_weapon_charge_step(&c, 1, 1, 0, 7, 8, 2, .3f, t, &in, &sh) == HALO_WEAPON_CHARGE_IDLE); /* no lease */

    /* Without a semantic owner the audio path is unchanged. */
    halo_haptics_note_fire();
    CHECK(onset(.01f, 0));
    CHECK(blast());
    /* Owned: a held trigger no longer turns quiet positional sounds into
     * buzzes; listener and world cues keep their own thresholds. */
    halo_haptics_weapon_owner_renew(); halo_haptics_note_fire();
    CHECK(!onset(.01f, 0));
    CHECK(onset(.015f, 1));
    CHECK(onset(.25f, 0));
    /* During the original fire call nothing audio-derived is published or
     * taken; an audio guess raised before acceptance never reaches the guard. */
    settle(); halo_haptics_weapon_shot_begin();
    halo_haptics_voice_onset(.3f, .01f, 0); CHECK(!halo_haptics_take(NULL, NULL));
    halo_haptics_weapon_shot_end(0); settle();
    halo_haptics_weapon_shot_begin(); halo_haptics_weapon_shot_end(1);
    /* After acceptance: the own weapon's voice, whether at the listener or
     * positional (headset), and its blast stay quiet for the window. */
    CHECK(!onset(.015f, 1));
    CHECK(!onset(.25f, 0));
    CHECK(!blast());
    /* The window is bounded: world cues return, unowned the old path does. */
    usleep(220000); halo_haptics_weapon_owner_renew();
    CHECK(onset(.015f, 1) && onset(.25f, 0) && blast());
    usleep(260000);
    halo_haptics_note_fire();
    CHECK(onset(.01f, 0));
    /* Toggle off releases ownership at once, not after the 250 ms lease. */
    halo_haptics_weapon_owner_renew(); halo_haptics_weapon_shot_begin(); halo_haptics_weapon_shot_end(1);
    CHECK(!onset(.25f, 0));
    halo_sense_set_weapon_haptics(0);
    CHECK(onset(.25f, 0));
    halo_haptics_note_fire(); CHECK(onset(.01f, 0));
    halo_sense_set_weapon_haptics(1);
    halo_haptics_weapon_owner_renew(); halo_haptics_weapon_owner_release(); halo_haptics_note_fire();
    CHECK(onset(.01f, 0));

    /* Real bridge settings and owner lease. */
    CHECK(halo_sense_grip_continuity() == 1 && halo_sense_scope_smoothing() == 0 && halo_sense_weapon_haptics() == 1);
    uint64_t session = halo_sense_begin_session();
    HostGCSnapshot pad = {.connected = true, .spatial = true, .source_generation = 1};
    halo_sense_observe_controller_for_session(session, &pad);
    HaloSenseHandPose hand = {.forward = {1, 0, 0}, .up = {0, 0, 1}, .valid = 1};
    HaloSenseFrame frame = {.left = hand, .right = hand, .generation = 1, .controller_generation = 1};
    halo_sense_set_gameplay_supported(1);
    halo_sense_publish(session, &frame);
    uint64_t g = generation();
    uint64_t lease = halo_sense_set_weapon_haptic_owner(1, session, g);
    CHECK(lease && halo_sense_weapon_haptics_admit(session, g, lease));
    CHECK(halo_sense_set_weapon_haptic_owner(1, session, g) == lease); /* renewal keeps the lease */
    CHECK(!halo_sense_weapon_haptics_admit(session + 1, g, lease) && !halo_sense_weapon_haptics_admit(session, g + 1, lease));
    CHECK(!halo_sense_weapon_haptics_admit(session, g, lease + 1) && !halo_sense_weapon_haptics_admit(session, g, 0));
    CHECK(!halo_sense_set_weapon_haptic_owner(1, session + 1, g) && !halo_sense_weapon_haptics_admit(session, g, lease));
    lease = halo_sense_set_weapon_haptic_owner(1, session, g); CHECK(halo_sense_weapon_haptics_admit(session, g, lease));
    /* The haptics toggle revokes the lease without touching held input. */
    halo_sense_set_weapon_haptics(0);
    CHECK(generation() == g && !halo_sense_weapon_haptics_admit(session, g, lease) && !halo_sense_set_weapon_haptic_owner(1, session, g));
    halo_sense_set_weapon_haptics(1);
    CHECK(!halo_sense_weapon_haptics_admit(session, g, lease));
    uint64_t renewed = halo_sense_set_weapon_haptic_owner(1, session, g);
    CHECK(renewed && renewed != lease && halo_sense_weapon_haptics_admit(session, g, renewed));
    /* Master strength is authoritative at playback. */
    HaloSettings settings; halo_settings_get(&settings); float strength = settings.haptics_strength;
    settings.haptics_strength = 0; halo_settings_set(&settings);
    CHECK(!halo_sense_weapon_haptics_admit(session, g, renewed));
    settings.haptics_strength = strength; halo_settings_set(&settings);
    CHECK(halo_sense_weapon_haptics_admit(session, g, renewed));
    /* Native UI and lost tracking revoke playback. */
    halo_sense_app_input_changed(1); CHECK(!halo_sense_weapon_haptics_admit(session, generation(), renewed));
    halo_sense_app_input_changed(0); halo_sense_publish(session, &frame); g = generation();
    renewed = halo_sense_set_weapon_haptic_owner(1, session, g); CHECK(halo_sense_weapon_haptics_admit(session, g, renewed));
    /* Feel toggles change the input generation (stale poses and held input
     * are invalidated); setting the same value again does not. */
    halo_sense_set_grip_continuity(1); CHECK(generation() == g);
    halo_sense_set_grip_continuity(0); CHECK(halo_sense_grip_continuity() == 0 && generation() != g);
    CHECK(!halo_sense_weapon_haptics_admit(session, g, renewed)); g = generation();
    halo_sense_set_scope_smoothing(1); CHECK(halo_sense_scope_smoothing() == 1 && generation() != g);
    halo_sense_set_scope_smoothing(0); halo_sense_set_grip_continuity(1);
    halo_sense_end_session(session);
    CHECK(!halo_sense_weapon_haptics_admit(session, generation(), renewed));
    printf("PASS %u weapon haptic profile, charge step, audio ownership and Sense lease/settings checks\n", checks);
    return 0;
}
