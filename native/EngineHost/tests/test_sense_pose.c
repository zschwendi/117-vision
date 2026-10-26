#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
static uint64_t test_time = UINT64_C(1000000000);
static int test_clock(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec = test_time / 1000000000; out->tv_nsec = test_time % 1000000000; return 0;
}
#define clock_gettime test_clock
#include "../halo_settings.c"
#undef clock_gettime
static unsigned checks;
#define CHECK(condition) do { checks++; assert(condition); } while (0)
static HostGCSnapshot pad(void) {
    HostGCSnapshot p = {.connected = true, .spatial = true, .ly = 1, .rx = .8f, .ry = -.5f, .rt = 1, .sequence = 42, .source_generation = 1};
    p.buttons[HOSTGC_BTN_RTRIGGER] = true; p.buttons[HOSTGC_BTN_MENU] = true; return p;
}
int main(void) {
    unsetenv("HALO_SENSE_WEAPON"); halo_sense_set_active(1);
    CHECK(halo_sense_requested() && !halo_sense_active()); /* no immersive session */
    halo_sense_set_enabled(0); CHECK(!halo_sense_requested());
    setenv("HALO_SENSE_WEAPON", "1", 1); CHECK(!halo_sense_requested()); /* cannot override saved off */
    halo_sense_set_enabled(1); CHECK(halo_sense_requested());
    setenv("HALO_SENSE_WEAPON", "0", 1); CHECK(!halo_sense_requested());
    unsetenv("HALO_SENSE_WEAPON"); CHECK(halo_sense_requested()); /* icon launch */
    uint64_t session = halo_sense_begin_session(); CHECK(session != 0 && halo_sense_session_active());
    HostGCSnapshot initial = pad(); halo_sense_observe_controller_for_session(session, &initial);
    halo_sense_set_gameplay_supported(1); CHECK(halo_sense_gameplay_supported());
    HaloSenseHandPose hand = {.position = {.1f,.2f,.3f}, .forward = {1,0,0}, .up = {0,0,1}, .trigger = 1, .valid = 1};
    HaloSenseFrame frame = {.left = hand, .right = hand, .head_yaw = (float)M_PI / 2, .generation = 1, .controller_generation = 1}, read;
    CHECK(halo_sense_turn_mode() == 0);
    for (int mode = -1; mode < 100; mode++) { halo_sense_set_turn_mode(mode); CHECK(halo_sense_turn_mode() == (mode == 1 || mode == 2 || mode == 30 || mode == 45 ? mode : 0)); }
    CHECK(halo_sense_turn_speed() == 90.f && halo_sense_turn_boundary() == .72f);
    halo_sense_set_turn_speed(60.f); CHECK(halo_sense_turn_speed() == 60.f);
    halo_sense_set_turn_speed(-1.f); CHECK(halo_sense_turn_speed() == 30.f);
    halo_sense_set_turn_speed(500.f); CHECK(halo_sense_turn_speed() == 120.f);
    halo_sense_set_turn_speed(NAN); CHECK(halo_sense_turn_speed() == 90.f);
    halo_sense_set_turn_speed(INFINITY); CHECK(halo_sense_turn_speed() == 90.f);
    halo_sense_set_turn_boundary(.8f); CHECK(halo_sense_turn_boundary() == .8f);
    halo_sense_set_turn_boundary(-1.f); CHECK(halo_sense_turn_boundary() == .5f);
    halo_sense_set_turn_boundary(2.f); CHECK(halo_sense_turn_boundary() == .9f);
    halo_sense_set_turn_boundary(NAN); CHECK(halo_sense_turn_boundary() == .72f);
    halo_sense_set_turn_boundary(INFINITY); CHECK(halo_sense_turn_boundary() == .72f);
    halo_sense_set_active(1); halo_sense_publish(session, &frame);
    CHECK(halo_sense_active() && halo_sense_read(&read));
    HostGCSnapshot p = pad(); halo_sense_filter_gamepad(&p);
    CHECK(fabsf(p.lx - 1) < .00001f && fabsf(p.ly) < .00001f && p.rx == 0 && p.ry == 0);
    CHECK(p.rt == 0 && !p.buttons[HOSTGC_BTN_RTRIGGER]); /* held on entry */
    frame.right.trigger = 0; halo_sense_publish(session, &frame);
    frame.right.trigger = 1; halo_sense_publish(session, &frame);
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1 && p.buttons[HOSTGC_BTN_RTRIGGER]);
    float position[3], forward[3], up[3];
    CHECK(halo_sense_weapon_pose(1, position, forward, up));
    CHECK(position[1] == .2f && forward[0] == 1 && up[2] == 1);
    CHECK(!halo_sense_weapon_pose(2, position, forward, up));
    test_time += UINT64_C(150000001);
    CHECK(!halo_sense_read(&read) && read.right.valid == 0 && halo_sense_active());
    p = pad(); p.buttons[HOSTGC_BTN_X] = true; halo_sense_filter_gamepad(&p);
    CHECK(p.lx == 0 && p.ly == 0 && p.rx == 0 && p.ry == 0 && p.rt == 0);
    CHECK(!p.buttons[HOSTGC_BTN_X]);
    CHECK(p.connected && p.spatial && p.sequence == 42 && p.buttons[HOSTGC_BTN_MENU]);
    CHECK(p.source_generation == 1); /* gating must not erase connection identity */
    halo_sense_publish(session, &frame); p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    frame.right.trigger = 0; halo_sense_publish(session, &frame);
    frame.right.trigger = 1; halo_sense_publish(session, &frame);
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1);
    frame.generation++; halo_sense_publish(session, &frame); p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    frame.right.trigger = 0; halo_sense_publish(session, &frame);
    halo_sense_publish(session, NULL); CHECK(!halo_sense_read(&read));
    frame.right.trigger = 1; halo_sense_publish(session, &frame); p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    p = pad(); p.spatial = false; HostGCSnapshot ordinary = p; halo_sense_filter_gamepad(&p); CHECK(memcmp(&p, &ordinary, sizeof p) == 0);
    CHECK(!halo_sense_active());
    halo_sense_set_active(1); halo_sense_publish(session, &frame); CHECK(halo_sense_read(&read));
    frame.right.forward[0] = NAN; halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read));
    frame.right = hand; frame.left.up[0] = 1; halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read));
    frame.left = hand; frame.right.position[0] = 5; halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read));
    frame.right = hand; frame.head_yaw = NAN; halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read));
    halo_sense_set_active(0); CHECK(!halo_sense_active() && !halo_sense_read(&read));
    /* Switching back from an ordinary pad claims a native Sense poll before
     * the compositor can publish, suppressing held fire in that interval. */
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(halo_sense_active() && p.rt == 0);
    halo_sense_set_enabled(0); CHECK(!halo_sense_enabled() && !halo_sense_active() && !halo_sense_requested());
    p = pad(); ordinary = p; halo_sense_filter_gamepad(&p); CHECK(memcmp(&p, &ordinary, sizeof p) == 0);
    halo_sense_set_enabled(1); CHECK(halo_sense_enabled() && halo_sense_requested());
    frame.left = frame.right = hand; frame.head_yaw = 0;
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(halo_sense_active() && p.rt == 0);
    halo_sense_publish(session, &frame); p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    frame.right.trigger = 0; halo_sense_publish(session, &frame); frame.right.trigger = 1; halo_sense_publish(session, &frame);
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1);
    halo_sense_set_gameplay_supported(0); CHECK(!halo_sense_gameplay_supported());
    p = pad(); p.buttons[HOSTGC_BTN_X] = true; p.buttons[HOSTGC_BTN_A] = true;
    halo_sense_filter_gamepad(&p); CHECK(p.rt == 0 && p.ly == 0 && p.buttons[HOSTGC_BTN_MENU]);
    CHECK(p.buttons[HOSTGC_BTN_X] && !p.buttons[HOSTGC_BTN_A]);
    halo_sense_set_gameplay_supported(1); p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    frame.right.trigger = 0; halo_sense_publish(session, &frame); frame.right.trigger = 1; halo_sense_publish(session, &frame);
    p = pad(); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1);
    uint64_t old_session = session; session = halo_sense_begin_session(); CHECK(session != old_session);
    CHECK(!halo_sense_gameplay_supported()); halo_sense_set_gameplay_supported(1);
    p = pad(); halo_sense_observe_controller_for_session(session, &p);
    frame.right.trigger = 0; halo_sense_publish(session, &frame); frame.right.trigger = 1; halo_sense_publish(session, &frame);
    CHECK(halo_sense_read(&read));
    halo_sense_publish(old_session, NULL); CHECK(halo_sense_read(&read));
    HaloSenseFrame stale = frame; stale.generation = 999; halo_sense_publish(old_session, &stale);
    CHECK(halo_sense_read(&read) && read.generation == frame.generation);
    ordinary = pad(); ordinary.spatial = false;
    halo_sense_observe_controller_for_session(old_session, &ordinary); CHECK(halo_sense_active());
    halo_sense_end_session(old_session); CHECK(halo_sense_active() && halo_sense_read(&read) && halo_sense_gameplay_supported());
    halo_sense_end_session(session); CHECK(!halo_sense_session_active() && !halo_sense_active());
    CHECK(!halo_sense_gameplay_supported());
    p = pad(); ordinary = p; halo_sense_filter_gamepad(&p); CHECK(memcmp(&p, &ordinary, sizeof p) == 0 && !halo_sense_active());

    /* Dual controls are a persisted opt-in, independent from process launch
     * flags. Capture precedes L2 consumption; both hand gates survive loss. */
    CHECK(!halo_sense_dual_enabled() && !halo_sense_dual_requested());
    halo_sense_set_dual_enabled(1); CHECK(halo_sense_dual_enabled() && halo_sense_dual_requested());
    session = halo_sense_begin_session(); halo_sense_set_gameplay_supported(1);
    p = pad(); p.lt = .9f; p.buttons[HOSTGC_BTN_LTRIGGER] = true; p.buttons[HOSTGC_BTN_X] = true;
    halo_sense_observe_controller_for_session(session, &p);
    frame.left = frame.right = hand; frame.head_yaw = 0; frame.generation = 7;
    halo_sense_publish(session, &frame);
    HaloSenseDualInput input;
    CHECK(halo_sense_dual_input(&input) && input.enabled && input.spatial && input.gameplay_supported);
    CHECK(input.session == session && input.generation && !input.armed[0] && !input.armed[1]);
    CHECK(input.trigger[0] == .9f && input.trigger[1] == 1 && input.reload[0] && input.reload[1]);
    uint64_t epoch = input.generation;
    halo_sense_filter_gamepad(&p);
    CHECK(p.lt == 0 && !p.buttons[HOSTGC_BTN_LTRIGGER] && p.rt == 0 && p.buttons[HOSTGC_BTN_X]);
    CHECK(halo_sense_dual_input(&input) && input.trigger[0] == .9f && input.trigger[1] == 1);
    frame.left.trigger = 0; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && !input.armed[1]);
    frame.left.trigger = 1; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && !input.armed[1]);
    frame.right.trigger = 0; halo_sense_publish(session, &frame);
    frame.right.trigger = 1; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && input.armed[1] && input.generation == epoch);
    HaloSenseStatus status; halo_sense_status(&status);
    CHECK(status.requested && status.enabled && status.dual_requested && status.active && status.valid);
    CHECK(status.session == session && status.generation == epoch && status.published_age_ns == 0);

    /* Actual muzzle publication is coherent and scoped to this input epoch;
     * one invalid hand must not erase the other hand's valid reticle. */
    HaloSenseReticleFrame reticles = {.session = session, .generation = epoch}, result;
    reticles.hands[0] = (HaloSenseReticle){.valid=1,.hit=1,.origin={.2f,0,0},.direction={1,0,0},.point={2,0,0}};
    reticles.hands[1] = reticles.hands[0];
    halo_sense_reticle_publish(&reticles);
    CHECK(halo_sense_reticle_read(&result) && result.hands[0].valid && result.hands[1].valid);
    CHECK(result.hands[0].hit && result.hands[0].point[0] == 2 && result.session == session);
    HaloSenseReticleFrame wrong = reticles; wrong.session--; wrong.hands[0].point[0] = 9;
    halo_sense_reticle_publish(&wrong); CHECK(halo_sense_reticle_read(&result) && result.hands[0].point[0] == 2);
    wrong = reticles; wrong.generation--; wrong.hands[0].point[0] = 9;
    halo_sense_reticle_publish(&wrong); CHECK(halo_sense_reticle_read(&result) && result.hands[0].point[0] == 2);
    wrong = reticles; wrong.hands[0].direction[0] = NAN;
    halo_sense_reticle_publish(&wrong); CHECK(halo_sense_reticle_read(&result) && !result.hands[0].valid && result.hands[1].valid);
    wrong.hands[1].origin[1] = 5; halo_sense_reticle_publish(&wrong);
    CHECK(!halo_sense_reticle_read(&result) && !result.hands[0].valid && !result.hands[1].valid);
    halo_sense_reticle_publish(&reticles); CHECK(halo_sense_reticle_read(&result));
    halo_sense_set_dual_enabled(0);
    CHECK(halo_sense_dual_input(&input) && !input.enabled && input.generation != epoch && !input.armed[0] && !input.armed[1]);
    CHECK(!halo_sense_reticle_read(&result)); /* mode changes cannot retain old aim */
    p = pad(); p.lt = .9f; p.buttons[HOSTGC_BTN_LTRIGGER] = true;
    halo_sense_filter_gamepad(&p);
    CHECK(p.lt == 0 && !p.buttons[HOSTGC_BTN_LTRIGGER] && !p.buttons[HOSTGC_BTN_LSHOULDER]); /* held on mode entry */
    frame.left.trigger = 0; halo_sense_publish(session, &frame);
    p = pad(); p.buttons[HOSTGC_BTN_MENU] = false; halo_sense_observe_controller(&p);
    HaloSenseHeadPose head = {.forward={1,0,0},.up={0,0,1},.generation=frame.generation,.controller_generation=frame.controller_generation};
    halo_sense_publish_head(session,&head);
    p.buttons[HOSTGC_BTN_LSHOULDER] = true;
    halo_sense_filter_gamepad(&p);
    CHECK(!p.buttons[HOSTGC_BTN_LSHOULDER]); /* L1 hold waits for DI release pulse */
    CHECK(halo_sense_dual_input(&input) && input.support_grip && !input.enabled);
    halo_sense_status(&status); CHECK(status.support_grip);
    frame.left.trigger = 1; halo_sense_publish(session, &frame);
    p = pad(); p.lt = .9f; p.buttons[HOSTGC_BTN_LTRIGGER] = true;
    halo_sense_filter_gamepad(&p);
    CHECK(!p.buttons[HOSTGC_BTN_LSHOULDER] && p.lt == 0 && !p.buttons[HOSTGC_BTN_LTRIGGER]);
    CHECK(halo_sense_dual_input(&input) && !input.support_grip); /* L2 never impersonates L1 */
    halo_sense_set_dual_enabled(1); CHECK(halo_sense_dual_input(&input) && !input.armed[0] && !input.armed[1]);
    frame.left.trigger = frame.right.trigger = 0; halo_sense_publish(session, &frame);
    frame.left.trigger = frame.right.trigger = 1; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && input.armed[1]);
    epoch = input.generation; reticles.generation = epoch; halo_sense_reticle_publish(&reticles);
    test_time += UINT64_C(150000001); halo_sense_status(&status);
    CHECK(!status.valid && !status.armed[0] && !status.armed[1] && status.generation != epoch);
    CHECK(!halo_sense_reticle_read(&result) && !halo_sense_dual_input(&input));
    CHECK(!input.support_grip && !status.support_grip);
    halo_sense_publish(session, &frame); p = pad(); p.lt = 1; halo_sense_observe_controller(&p);
    CHECK(halo_sense_dual_input(&input) && !input.armed[0] && !input.armed[1]);
    frame.left.trigger = frame.right.trigger = 0; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && input.armed[1]);
    /* Fresh poses alone cannot revive an expired native input capture. */
    test_time += UINT64_C(150000001); halo_sense_publish(session, &frame);
    CHECK(!halo_sense_dual_input(&input) && !input.armed[0] && !input.armed[1]);
    p = pad(); p.spatial = false; ordinary = p; halo_sense_filter_gamepad(&p);
    CHECK(memcmp(&p, &ordinary, sizeof p) == 0 && !halo_sense_active());
    CHECK(!halo_sense_dual_input(&input) && !input.spatial);
    p = pad(); p.lt = 1; p.buttons[HOSTGC_BTN_LTRIGGER] = true; halo_sense_filter_gamepad(&p);
    CHECK(p.lt == 0 && p.rt == 0 && halo_sense_active());
    frame.left.trigger = frame.right.trigger = 1; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && !input.armed[0] && !input.armed[1]);
    setenv("HALO_SENSE_WEAPON", "0", 1); CHECK(!halo_sense_requested() && !halo_sense_dual_requested());
    CHECK(!halo_sense_read(&read) && !halo_sense_dual_input(&input));
    unsetenv("HALO_SENSE_WEAPON"); halo_sense_end_session(session);
    CHECK(!halo_sense_dual_input(&input) && !input.session && !input.spatial);
    /* A native reconnect invalidates before Swift sees its notification. Old
     * pose frames and in-flight old pad captures cannot re-arm the new pair. */
    session = halo_sense_begin_session(); halo_sense_set_gameplay_supported(1);
    p = pad(); halo_sense_observe_controller_for_session(session, &p);
    frame.left.trigger = frame.right.trigger = 0; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && input.armed[1]);
    epoch = input.generation; halo_sense_controller_changed(2);
    CHECK(!halo_sense_read(&read) && !halo_sense_dual_input(&input) && input.generation != epoch);
    halo_sense_status(&status); CHECK(status.controller_generation == 2 && status.published_controller_generation == 1 && !status.valid);
    halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read));
    halo_sense_observe_controller(&p); CHECK(!halo_sense_dual_input(&input)); /* old native capture */
    p.source_generation = 2; p.lt = p.rt = 1; halo_sense_observe_controller(&p);
    halo_sense_publish(session, &frame); CHECK(!halo_sense_read(&read)); /* new input cannot bless old pose */
    frame.controller_generation = 2; frame.left.trigger = frame.right.trigger = 1;
    halo_sense_publish(session, &frame); CHECK(halo_sense_dual_input(&input) && !input.armed[0] && !input.armed[1]);
    frame.left.trigger = frame.right.trigger = 0; halo_sense_publish(session, &frame);
    CHECK(halo_sense_dual_input(&input) && input.armed[0] && input.armed[1]);
    epoch = input.generation; halo_sense_controller_changed(1); /* delayed older callback */
    CHECK(halo_sense_dual_input(&input) && input.generation == epoch && input.armed[1]);
    p.sequence++; halo_sense_observe_controller(&p);
    CHECK(halo_sense_dual_input(&input) && input.generation == epoch); /* polling is not a connection */
    reticles.session = session; reticles.generation = epoch; halo_sense_reticle_publish(&reticles);
    CHECK(halo_sense_reticle_read(&result));
    test_time += UINT64_C(100000000); halo_sense_publish(session, &frame); halo_sense_observe_controller(&p);
    test_time += UINT64_C(51000000); halo_sense_publish(session, &frame); halo_sense_observe_controller(&p);
    CHECK(halo_sense_dual_input(&input) && input.generation == epoch);
    CHECK(!halo_sense_reticle_read(&result)); /* reticle ages independently of fresh poses */
    halo_sense_end_session(session);
    printf("PASS %u Sense bridge, freshness, movement and fire safety checks\n", checks);
}
