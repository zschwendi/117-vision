/* Native seat controls on the production input bridge (halo_settings.c):
 * the Sense pair becomes an ordinary pad for the original seat, R2 must be
 * released after every identity change, every identity change ends held
 * input, and the compositor's seat snapshot is one locked pair. */
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
static HostGCSnapshot pad(float rt) {
    HostGCSnapshot p = {.connected = true, .spatial = true, .lx = .3f, .ly = 1, .rx = .8f, .ry = -.5f, .rt = rt, .lt = 1,
                        .sequence = 42, .source_generation = 1};
    p.buttons[HOSTGC_BTN_RTRIGGER] = rt >= .25f; p.buttons[HOSTGC_BTN_A] = true; p.buttons[HOSTGC_BTN_X] = true;
    p.buttons[HOSTGC_BTN_LSHOULDER] = true; p.buttons[HOSTGC_BTN_LTRIGGER] = true; p.buttons[HOSTGC_BTN_RTHUMB] = true;
    return p;
}
static uint64_t generation(void) { HaloSenseStatus s; halo_sense_status(&s); return s.generation; }
int main(void) {
    unsetenv("HALO_SENSE_WEAPON"); halo_sense_set_enabled(1);
    uint64_t session = halo_sense_begin_session();
    HostGCSnapshot owner = pad(0); halo_sense_observe_controller_for_session(session, &owner);
    CHECK(halo_sense_active());
    HaloSenseSeatSnapshot snap; halo_sense_seat_snapshot(&snap); CHECK(!snap.native && snap.transition == 0);

    /* On foot, unsupported gameplay is still the build119 dead end. */
    halo_sense_set_native_controls(0, 1); halo_sense_set_gameplay_supported(0);
    HostGCSnapshot p = pad(1); halo_sense_filter_gamepad(&p); CHECK(p.rx == 0 && p.ly == 0 && p.rt == 0);

    /* Seat entry: native controls without tracking; sticks unrotated. */
    uint64_t before = generation(); halo_sense_set_native_controls(1, 2);
    halo_sense_seat_snapshot(&snap); CHECK(snap.native && snap.transition == 1 && halo_sense_native_controls());
    CHECK(halo_sense_seat_transition() == 1 && generation() > before);
    p = pad(1); halo_sense_filter_gamepad(&p);
    CHECK(p.rx == .8f && p.ry == -.5f && p.lx == .3f && p.ly == 1 && p.buttons[HOSTGC_BTN_A] && p.buttons[HOSTGC_BTN_X] && p.buttons[HOSTGC_BTN_RTHUMB]);
    CHECK(p.rt == 0 && !p.buttons[HOSTGC_BTN_RTRIGGER]);        /* R2 held through entry: not armed */
    CHECK(!p.buttons[HOSTGC_BTN_LSHOULDER] && p.lt == 0 && !p.buttons[HOSTGC_BTN_LTRIGGER]);
    p = pad(0); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    p = pad(1); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1 && p.buttons[HOSTGC_BTN_RTRIGGER]); /* released, then fires */
    /* Republishing the same identity is not a transition. */
    before = generation(); halo_sense_set_native_controls(1, 2); CHECK(generation() == before && halo_sense_seat_transition() == 1);
    p = pad(1); halo_sense_filter_gamepad(&p); CHECK(p.rt == 1);
    /* Seat change: new token, held R2 disarmed again. */
    halo_sense_set_native_controls(1, 3); CHECK(halo_sense_seat_transition() == 2);
    p = pad(1); halo_sense_filter_gamepad(&p); CHECK(p.rt == 0);
    /* Launcher ownership keeps the build119 block, seated or not. */
    halo_sense_app_input_changed(1); p = pad(0); halo_sense_filter_gamepad(&p); CHECK(p.rx == 0 && p.ly == 0);
    halo_sense_app_input_changed(0);
    /* Ordinary pads are never filtered. */
    p = pad(1); p.spatial = false; HostGCSnapshot ordinary = p; halo_sense_filter_gamepad(&p); CHECK(!memcmp(&p, &ordinary, sizeof p));
    halo_sense_observe_controller_for_session(session, &owner); CHECK(halo_sense_active());

    /* Exit: token moves once, controls return to the tracked filter. */
    before = generation(); halo_sense_set_native_controls(0, 4);
    halo_sense_seat_snapshot(&snap); CHECK(!snap.native && snap.transition == 3 && generation() > before);
    p = pad(1); halo_sense_filter_gamepad(&p); CHECK(p.rx == 0 && p.rt == 0);
    /* On-foot identity changes (death, respawn, ownership) end held input
     * but are not seat transitions: the display basis is left alone. */
    before = generation(); halo_sense_set_native_controls(0, 5);
    CHECK(generation() > before && halo_sense_seat_transition() == 3);
    HaloSenseDualInput dual; halo_sense_dual_input(&dual); CHECK(dual.generation == generation() && !dual.armed[1]);
    /* Death while seated leaves the seat: one transition. */
    halo_sense_set_native_controls(1, 6); halo_sense_set_native_controls(0, 7); CHECK(halo_sense_seat_transition() == 5);
    /* Lifecycle: a seated publication never outlives its session or
     * controller connection; nothing here moves the seat token. */
    halo_sense_set_native_controls(1, 8); uint64_t token = halo_sense_seat_transition();
    halo_sense_seat_snapshot(&snap); CHECK(snap.native);
    halo_sense_end_session(session); session = halo_sense_begin_session();
    halo_sense_observe_controller_for_session(session, &owner);
    halo_sense_seat_snapshot(&snap); CHECK(!snap.native && !halo_sense_native_controls());
    p = pad(0); halo_sense_filter_gamepad(&p); CHECK(p.rx == 0 && p.ly == 0);       /* fails closed, unsupported */
    halo_sense_set_native_controls(1, 8);                                          /* fresh publication */
    CHECK(halo_sense_native_controls() && halo_sense_seat_transition() == token);
    p = pad(0); halo_sense_filter_gamepad(&p); CHECK(p.rx == .8f);
    /* Sense -> ordinary pad -> Sense (new connections): off at handover,
     * ordinary input untouched, still off for the new pair until published. */
    HostGCSnapshot gamepad = pad(1); gamepad.spatial = false; gamepad.source_generation = 2;
    HostGCSnapshot copy = gamepad; halo_sense_filter_gamepad(&gamepad); CHECK(!memcmp(&gamepad, &copy, sizeof gamepad));
    halo_sense_seat_snapshot(&snap); CHECK(!snap.native && !halo_sense_native_controls() && snap.transition == token);
    HostGCSnapshot pair = pad(0); pair.source_generation = 3; halo_sense_observe_controller_for_session(session, &pair);
    CHECK(halo_sense_active() && !halo_sense_native_controls());
    p = pad(0); p.source_generation = 3; halo_sense_filter_gamepad(&p); CHECK(p.rx == 0);
    halo_sense_set_native_controls(1, 8); CHECK(halo_sense_native_controls() && halo_sense_seat_transition() == token);
    /* Sense switched off and on with the same session and connection: the
     * lease ends with ownership and returns only with a new publication. */
    halo_sense_set_enabled(0); halo_sense_set_enabled(1); halo_sense_observe_controller_for_session(session, &pair);
    halo_sense_seat_snapshot(&snap); CHECK(halo_sense_active() && !snap.native && snap.transition == token);
    p = pad(0); p.source_generation = 3; halo_sense_filter_gamepad(&p); CHECK(p.rx == 0);
    halo_sense_set_native_controls(1, 8); p = pad(0); p.source_generation = 3; halo_sense_filter_gamepad(&p);
    CHECK(p.rx == .8f && halo_sense_seat_transition() == token);
    /* Committed seat presentation is stable through tracking churn: a new
     * pose generation, an invalid publication and lost hands change the input
     * generation (R2 must be released again) but not native mode or token. */
    HaloSenseHandPose hand = {.position = {.1f,.2f,.3f}, .forward = {1,0,0}, .up = {0,0,1}, .trigger = 1, .valid = 1};
    HaloSenseFrame frame = {.left = hand, .right = hand, .generation = 5, .controller_generation = 3};
    uint64_t input = generation();
    halo_sense_publish(session, &frame); frame.generation = 6; halo_sense_publish(session, &frame);
    halo_sense_publish(session, NULL); frame.right.valid = 0; halo_sense_publish(session, &frame);
    halo_sense_seat_snapshot(&snap); CHECK(generation() != input && snap.native && snap.transition == token);
    p = pad(1); p.source_generation = 3; halo_sense_filter_gamepad(&p); CHECK(p.rx == .8f && p.rt == 0);   /* no valid hands needed */
    p = pad(0); p.source_generation = 3; halo_sense_filter_gamepad(&p);
    p = pad(1); p.source_generation = 3; halo_sense_filter_gamepad(&p); CHECK(p.rt == 1);                  /* re-armed by release */
    /* A seat identity change while seated moves the token as usual. */
    halo_sense_set_native_controls(1, 10); CHECK(halo_sense_seat_transition() == token + 1 && halo_sense_native_controls());
    halo_sense_set_native_controls(0, 9);                                          /* the seat changed meanwhile: on foot */
    CHECK(!halo_sense_native_controls() && halo_sense_seat_transition() == token + 2);
    halo_sense_end_session(session);
    printf("PASS %u checks: native seat pad (unrotated sticks, all buttons, L1/L2 withheld), R2 re-armed per identity, every identity change ends held input, seat token only at seat boundaries, launcher block and ordinary pads unchanged; native never outlives its session, controller connection or Sense ownership (same-connection off/on revoked), stays stable through tracking churn and lost hands, and handover moves no seat token\n", checks);
    return 0;
}
