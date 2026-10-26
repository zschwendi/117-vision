/* halo_sense_prompt_context (sense_prompt.inc) against the real Sense bridge
 * (halo_settings.c -> sense_pose.inc): each reported context matches what
 * halo_sense_filter_gamepad then does with the controls the prompt labels
 * name, Quick gestures' Create ownership matches the filter's clutch and
 * scoreboard handling, the grenade clutch follows fresh head/pad ownership,
 * and reading the context never invalidates, disarms or cancels anything.
 * Synthetic poses and a test clock; no device.
 * clang -O2 -pthread -I native/EngineHost native/EngineHost/tests/test_sense_prompt_context.c -lm */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
static uint64_t test_time = UINT64_C(1000000000);
static int test_clock(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec = test_time / 1000000000; out->tv_nsec = test_time % 1000000000; return 0;
}
#define clock_gettime test_clock
#include "../halo_settings.c"
#undef clock_gettime
#include "../input_prompt_label.h"
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)

static uint64_t session;
static HaloSenseFrame hands;
static HaloSenseHeadPose head;
static HostGCSnapshot neutral(void) { return (HostGCSnapshot){ .connected = true, .spatial = true, .source_generation = 1, .sequence = 1 }; }
/* One fresh publication of pad, hands and head at the current time. */
static void fresh(const HostGCSnapshot *pad, int with_hands) {
    halo_sense_observe_controller(pad);
    halo_sense_publish(session, with_hands ? &hands : NULL);
    halo_sense_publish_head(session, &head);
}
static HaloSensePromptContext context(void) { HaloSensePromptContext c; halo_sense_prompt_context(&c); return c; }
static HostGCSnapshot filtered(HostGCSnapshot p) { halo_sense_filter_gamepad(&p); return p; }
static HostGCSnapshot pressed(int button) { HostGCSnapshot p = neutral(); p.buttons[button] = true; return p; }

/* Everything the prompt context reads or the fresh helpers would reset. */
typedef struct { uint64_t generation; int valid, head_valid, grenade_armed, scope_armed, fire0, fire1; } SenseSideState;
static SenseSideState side(void) {
    return (SenseSideState){ sense_input_generation, sense_valid, sense_head_valid, sense_grenade_armed, sense_scope_armed,
                             sense_fire_armed[0], sense_fire_armed[1] };
}
static int same(SenseSideState a, SenseSideState b) { return !memcmp(&a, &b, sizeof a); }

int main(void) {
    unsetenv("HALO_SENSE_WEAPON");
    HaloSensePromptContext c;
    /* Tracked aiming off: the pair passes unchanged. */
    halo_sense_set_enabled(0);
    session = halo_sense_begin_session();
    HostGCSnapshot p = neutral(); halo_sense_observe_controller(&p);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_PASSTHROUGH && !c.grenade_clutch && !c.gestures_own_create);
    p = pressed(HOSTGC_BTN_LSHOULDER); p.rx = .8f; p.lt = 1; p.buttons[HOSTGC_BTN_OPTIONS] = true;
    HostGCSnapshot q = filtered(p); CHECK(!memcmp(&q, &p, sizeof p));
    for (int e = 0; e < HOSTGC_BUTTON_COUNT; e++) CHECK(host_prompt_spatial_live(&c, e) == (e != HOSTGC_BTN_HOME));

    /* Tracked, on foot, fresh hands and head. */
    halo_sense_set_enabled(1); halo_sense_set_gameplay_supported(1);
    HaloSenseHandPose hand = { .position = { .1f, .2f, .3f }, .forward = { 1, 0, 0 }, .up = { 0, 0, 1 }, .valid = 1 };
    hands = (HaloSenseFrame){ .left = hand, .right = hand, .generation = 1, .controller_generation = 1 };
    head = (HaloSenseHeadPose){ .forward = { 1, 0, 0 }, .up = { 0, 0, 1 }, .generation = 1, .controller_generation = 1,
                                .movement_yaw = 0, .movement_yaw_valid = 1 };
    p = neutral(); fresh(&p, 1);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_TRACKED && c.gestures_own_create && c.grenade_clutch && c.controller_generation == 1);
    /* The labelled controls reach the game; the withheld ones do not. */
    static const int passes[] = { HOSTGC_BTN_X, HOSTGC_BTN_Y, HOSTGC_BTN_A, HOSTGC_BTN_B, HOSTGC_BTN_RSHOULDER,
                                  HOSTGC_BTN_RTHUMB, HOSTGC_BTN_LTHUMB, HOSTGC_BTN_DPAD_UP, HOSTGC_BTN_DPAD_LEFT };
    for (unsigned i = 0; i < sizeof passes / sizeof *passes; i++) {
        CHECK(host_prompt_spatial_live(&c, passes[i]));
        q = filtered(pressed(passes[i])); CHECK(q.buttons[passes[i]]);
    }
    CHECK(!host_prompt_spatial_live(&c, HOSTGC_BTN_LSHOULDER) && !filtered(pressed(HOSTGC_BTN_LSHOULDER)).buttons[HOSTGC_BTN_LSHOULDER]);
    CHECK(!host_prompt_spatial_live(&c, HOSTGC_BTN_LTRIGGER) && !filtered(pressed(HOSTGC_BTN_LTRIGGER)).buttons[HOSTGC_BTN_LTRIGGER]);
    p = neutral(); p.rx = .9f; CHECK(!host_prompt_spatial_live(&c, HOSTGC_PROMPT_RSTICK) && filtered(p).rx == 0);
    /* Quick gestures own centered Create: alone it is the clutch (no
     * scoreboard), with R3 it is the scoreboard (and R3 does not zoom). */
    CHECK(!host_prompt_spatial_live(&c, HOSTGC_BTN_OPTIONS) && host_prompt_spatial_live(&c, HOSTGC_PROMPT_CREATE_R3));
    p = pressed(HOSTGC_BTN_OPTIONS); fresh(&p, 1); q = filtered(p); CHECK(!q.buttons[HOSTGC_BTN_OPTIONS]);
    p.buttons[HOSTGC_BTN_RTHUMB] = true; fresh(&p, 1); q = filtered(p); CHECK(q.buttons[HOSTGC_BTN_OPTIONS] && !q.buttons[HOSTGC_BTN_RTHUMB]);
    /* Quick gestures off: centered Create keeps its scoreboard. */
    halo_sense_set_gestures_enabled(0);
    p = neutral(); fresh(&p, 1); c = context(); CHECK(c.context == HALO_SENSE_PROMPT_TRACKED && !c.gestures_own_create);
    CHECK(host_prompt_spatial_live(&c, HOSTGC_BTN_OPTIONS) && !host_prompt_spatial_live(&c, HOSTGC_PROMPT_CREATE_R3));
    p = pressed(HOSTGC_BTN_OPTIONS); fresh(&p, 1); CHECK(filtered(p).buttons[HOSTGC_BTN_OPTIONS]);
    halo_sense_set_gestures_enabled(1);
    /* Gesture delivery needs the published frame on this connection. */
    hands.controller_generation = 2; p = neutral(); halo_sense_observe_controller(&p); halo_sense_publish(session, &hands);
    c = context(); CHECK(!c.gestures_own_create);
    hands.controller_generation = 1; fresh(&p, 1); CHECK(context().gestures_own_create);

    /* Reading the context never invalidates, disarms or cancels anything,
     * even once tracking is stale (the filter's helpers would). */
    SenseSideState before = side();
    for (int i = 0; i < 20; i++) context();
    CHECK(same(before, side()));
    test_time += UINT64_C(200000000);
    before = side();
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_MENU_ONLY && !c.grenade_clutch && !c.gestures_own_create);
    CHECK(same(before, side()));
    HaloSenseStatus status; halo_sense_status(&status);       /* existing diagnostic: does invalidate */
    CHECK(!status.valid && !sense_valid);
    q = filtered(pressed(HOSTGC_BTN_X)); CHECK(!q.buttons[HOSTGC_BTN_X] && host_prompt_spatial_live(&c, HOSTGC_BTN_MENU));
    q = filtered(pressed(HOSTGC_BTN_MENU)); CHECK(q.buttons[HOSTGC_BTN_MENU]);

    /* Fresh tracking, unsupported gameplay: Use and Menu only, no clutch. */
    p = neutral(); fresh(&p, 1); halo_sense_set_gameplay_supported(0); fresh(&p, 1);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_USE_ONLY && !c.grenade_clutch);
    CHECK(filtered(pressed(HOSTGC_BTN_X)).buttons[HOSTGC_BTN_X] && !filtered(pressed(HOSTGC_BTN_Y)).buttons[HOSTGC_BTN_Y]);
    CHECK(host_prompt_spatial_live(&c, HOSTGC_BTN_X) && !host_prompt_spatial_live(&c, HOSTGC_BTN_Y));
    halo_sense_set_gameplay_supported(1);

    /* Grenade clutch: fresh head and pad, Menu not held. A held Menu or a
     * stale head ends it. */
    p = neutral(); fresh(&p, 1); CHECK(context().grenade_clutch);
    p = pressed(HOSTGC_BTN_MENU); halo_sense_observe_controller(&p); CHECK(!context().grenade_clutch);
    p = neutral(); fresh(&p, 1); CHECK(context().grenade_clutch);
    halo_sense_publish_head(session, NULL); CHECK(!context().grenade_clutch);
    halo_sense_publish_head(session, &head); CHECK(context().grenade_clutch);

    /* Scoped: fresh, then after hand loss with head ownership. */
    halo_sense_set_scope_active(1); fresh(&p, 1);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_SCOPE && c.grenade_clutch);
    CHECK(filtered(pressed(HOSTGC_BTN_Y)).buttons[HOSTGC_BTN_Y] && host_prompt_spatial_live(&c, HOSTGC_BTN_Y));
    CHECK(filtered(pressed(HOSTGC_BTN_OPTIONS)).buttons[HOSTGC_BTN_OPTIONS] && host_prompt_spatial_live(&c, HOSTGC_BTN_OPTIONS));
    halo_sense_observe_controller(&p); halo_sense_publish(session, NULL); halo_sense_publish_head(session, &head);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_SCOPE_HEAD && c.grenade_clutch);
    CHECK(filtered(pressed(HOSTGC_BTN_RTHUMB)).buttons[HOSTGC_BTN_RTHUMB] && host_prompt_spatial_live(&c, HOSTGC_BTN_RTHUMB));
    CHECK(filtered(pressed(HOSTGC_BTN_RSHOULDER)).buttons[HOSTGC_BTN_RSHOULDER] && host_prompt_spatial_live(&c, HOSTGC_BTN_RSHOULDER));
    CHECK(!filtered(pressed(HOSTGC_BTN_X)).buttons[HOSTGC_BTN_X] && !host_prompt_spatial_live(&c, HOSTGC_BTN_X));
    halo_sense_set_scope_active(0);

    /* Native seat: L1/L2 withheld, right stick and centered Create pass. */
    p = neutral(); fresh(&p, 1); halo_sense_set_native_controls(1, 8); fresh(&p, 1);
    c = context(); CHECK(c.context == HALO_SENSE_PROMPT_SEATED && !c.grenade_clutch && !c.gestures_own_create);
    q = pressed(HOSTGC_BTN_LSHOULDER); q.rx = .8f; q.buttons[HOSTGC_BTN_OPTIONS] = true; q = filtered(q);
    CHECK(!q.buttons[HOSTGC_BTN_LSHOULDER] && q.rx == .8f && q.buttons[HOSTGC_BTN_OPTIONS]);
    CHECK(!host_prompt_spatial_live(&c, HOSTGC_BTN_LSHOULDER) && host_prompt_spatial_live(&c, HOSTGC_PROMPT_RSTICK) &&
          host_prompt_spatial_live(&c, HOSTGC_BTN_OPTIONS));
    halo_sense_set_native_controls(0, 9);

    /* Native app owns input: never tracked, no clutch. */
    p = neutral(); fresh(&p, 1); CHECK(context().context == HALO_SENSE_PROMPT_TRACKED);
    halo_sense_app_input_changed(1); c = context(); CHECK(c.context != HALO_SENSE_PROMPT_TRACKED && !c.grenade_clutch);
    halo_sense_app_input_changed(0);

    /* Session end: passthrough (an untracked pair is used unchanged). */
    halo_sense_end_session(session); p = neutral(); halo_sense_observe_controller(&p);
    CHECK(context().context == HALO_SENSE_PROMPT_PASSTHROUGH);
    CHECK(halo_sense_prompt_context(NULL) == 0);
    printf("PASS %u Sense prompt context checks: passthrough/tracked/scope/scope-head/seated/use-only/menu-only "
           "match the gamepad filter, Quick gestures Create ownership, grenade clutch freshness, read-only (synthetic)\n", checks);
    return 0;
}
