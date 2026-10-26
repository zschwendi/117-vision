/* Physical commit transport through the production Sense bridge and the
 * DirectInput COM shim, deterministic time. One completed physical
 * interaction becomes exactly one original key tick (R or X). No hardware
 * or original-game acceptance is implied. */
#include <time.h>
#include <stdint.h>
static uint64_t physical_test_time = UINT64_C(1000000000);
static int physical_test_clock(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec = physical_test_time / UINT64_C(1000000000);
    out->tv_nsec = physical_test_time % UINT64_C(1000000000); return 0;
}
#define clock_gettime physical_test_clock
#include "../halo_settings.c"
#undef clock_gettime
#define main existing_dinput_lifecycle_main
#include "test_dinput_lifecycle.c"
#undef main
static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
static uint64_t session, topology;
static HaloSenseFrame frame;
static DIObj *keyboard;
static void refresh(void) {
    halo_sense_observe_controller_for_session(session, &controller);
    halo_sense_publish(session, &frame);
}
static void begin(void) {
    if (keyboard) { CHECK(call(shim_dev_2, 1, keyboard->guest) == 0); keyboard = NULL; }
    memset(host_keyboard_state, 0, sizeof host_keyboard_state);
    unsetenv("HALO_PAD2KEY"); unsetenv("HALO_SENSE_WEAPON");
    S8(0x00718FC9, 0); S16(0x00718FA6, 0);
    S32(0x006F1D6C, 0x30000); S32(0x3000C, 10);
    controller = (HostGCSnapshot){.connected=true, .spatial=true, .source_generation=++topology};
    /* The magazine/pickup grip is L2, held while the commit is made. */
    controller.lt = 1; controller.buttons[HOSTGC_BTN_LTRIGGER] = true;
    halo_sense_set_enabled(1); halo_sense_set_dual_enabled(0); halo_sense_set_gestures_enabled(1);
    session = halo_sense_begin_session(); halo_sense_observe_controller_for_session(session, &controller);
    halo_sense_set_gameplay_supported(1);
    HaloSenseHandPose hand = {.position={.3f,.1f,-.2f}, .forward={1,0,0}, .up={0,0,1}, .valid=1};
    frame = (HaloSenseFrame){.left=hand,.right=hand,.generation=1,.controller_generation=topology};
    refresh();
    keyboard = obj_new(DEV_KEYBOARD, 0); CHECK(keyboard != NULL); keyboard->buffer_size = 16;
    CHECK(call(shim_dev_7, 1, keyboard->guest) == DI_OK);
}
/* Stand-in for the engine-thread owner/target validator (the real one is
 * exercised against original rig/engine memory in test_sense_physical_original). */
static uint32_t live_owner = 0x23450000, live_target = 0x34560001; static unsigned checks_called;
static int test_check(int action, uint32_t owner, uint32_t target) {
    (void)action; ++checks_called; return owner == live_owner && target == live_target;
}
static int physical(uint64_t sequence, int action) {
    HaloSenseStatus status; halo_sense_status(&status);
    return halo_sense_physical_enqueue(session, status.generation, sequence, action, 0x23450000, 0x34560001);
}
static void next_tick(void) {
    physical_test_time += UINT64_C(10000000); refresh();
    S32(0x3000C, G32(0x3000C) + 1);
}
static unsigned events(unsigned flags) {
    S32(0x240, 16);
    CHECK(call(shim_dev_10, 5, keyboard->guest, 16u, 0x400u, 0x240u, flags) == DI_OK);
    return G32(0x240);
}
static void snapshot(uint8_t keys[256]) {
    CHECK(call(shim_dev_9, 3, keyboard->guest, 256u, 0x2000u) == DI_OK);
    memcpy(keys, GPTR(0x2000), 256);
}
static unsigned downs(unsigned scan) {
    unsigned count = events(0), found = 0;
    for (unsigned i = 0; i < count; ++i) if (G32(0x400+i*16) == scan && G32(0x404+i*16) == 0x80) ++found;
    return found;
}
/* Count every key-down of `scan` over several original ticks. */
static unsigned ticks_with(unsigned scan, unsigned ticks) {
    unsigned found = 0; uint8_t keys[256];
    for (unsigned t = 0; t < ticks; ++t) { snapshot(keys); found += downs(scan); next_tick(); }
    return found;
}
int main(void) {
    engine_flat_base = calloc(1, 8*1024*1024); assert(engine_flat_base);
    /* No validator registered: fail closed. */
    begin(); CHECK(!physical(1, HALO_SENSE_GESTURE_PHYSICAL_RELOAD));
    halo_sense_physical_set_delivery_check(test_check);
    for (int action = HALO_SENSE_GESTURE_PHYSICAL_RELOAD; action <= HALO_SENSE_GESTURE_PHYSICAL_PICKUP; ++action) {
        unsigned scan = action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD ? 0x13 : 0x2D;
        /* One interaction, one original key edge, with L2 still held. */
        begin(); CHECK(physical(1, action)); CHECK(!physical(1, action));
        CHECK(ticks_with(scan, 6) == 1);
        CHECK(!physical(1, action)); /* spent sequence never replays */
        CHECK(physical(2, action)); CHECK(ticks_with(scan, 6) == 1);
        /* Independent of the user gesture toggle (Swift gestures only). */
        begin(); halo_sense_set_gestures_enabled(0); refresh();
        CHECK(physical(1, action)); CHECK(ticks_with(scan, 6) == 1);
        halo_sense_set_gestures_enabled(1);
        /* Wrong session/generation, Menu or Create held: rejected. */
        begin(); HaloSenseStatus status; halo_sense_status(&status);
        CHECK(!halo_sense_physical_enqueue(session + 1, status.generation, 1, action, 0x23450000, 0x34560001));
        CHECK(!halo_sense_physical_enqueue(session, status.generation + 1, 1, action, 0x23450000, 0x34560001));
        CHECK(!halo_sense_physical_enqueue(session, status.generation, 1, HALO_SENSE_GESTURE_RELOAD, 0x23450000, 0x34560001));
        CHECK(!halo_sense_physical_enqueue(session, status.generation, 1, action, UINT32_MAX, 0x34560001));
        CHECK(!halo_sense_physical_enqueue(session, status.generation, 1, action, 0x23450000, 0x11110001)); /* not the engine's */
        controller.buttons[HOSTGC_BTN_OPTIONS] = true; refresh(); CHECK(!physical(1, action));
        controller.buttons[HOSTGC_BTN_OPTIONS] = false; controller.buttons[HOSTGC_BTN_MENU] = true; refresh();
        CHECK(!physical(2, action));
        controller.buttons[HOSTGC_BTN_MENU] = false; refresh();
        /* A tracking/recenter discontinuity before delivery drops it. */
        begin(); CHECK(physical(1, action));
        frame.generation++; refresh();
        CHECK(ticks_with(scan, 6) == 0);
        /* Unsupported gameplay (menu/cutscene/vehicle) never delivers. */
        begin(); CHECK(physical(1, action)); halo_sense_set_gameplay_supported(0); refresh();
        CHECK(ticks_with(scan, 6) == 0);
        /* A busy Swift gesture makes the physical commit refuse (and spend). */
        begin(); controller.buttons[HOSTGC_BTN_OPTIONS] = true; controller.lt = 0;
        controller.buttons[HOSTGC_BTN_LTRIGGER] = false; refresh();
        halo_sense_status(&status);
        CHECK(halo_sense_gesture_enqueue(session, topology, status.generation, 1, HALO_SENSE_GESTURE_PICKUP));
        controller.buttons[HOSTGC_BTN_OPTIONS] = false; refresh(); CHECK(!physical(1, action));
    }
    /* Coordinator repro (transport-conflict-probe.c): a pending commit
     * followed by an original action before the next poll yields to it. */
    const unsigned buttons[] = {HOSTGC_BTN_RSHOULDER, HOSTGC_BTN_Y, HOSTGC_BTN_LSHOULDER, HOSTGC_BTN_RTHUMB,
                                HOSTGC_BTN_X, HOSTGC_BTN_OPTIONS, HOSTGC_BTN_MENU};
    const char *names[] = {"R1 melee", "Triangle swap", "L1 grenade", "R3 scope", "Square", "Create", "Menu"};
    for (int action = HALO_SENSE_GESTURE_PHYSICAL_RELOAD; action <= HALO_SENSE_GESTURE_PHYSICAL_PICKUP; ++action) {
        unsigned scan = action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD ? 0x13 : 0x2D;
        for (unsigned i = 0; i < sizeof buttons / sizeof *buttons; ++i) {
            begin(); CHECK(physical(1, action)); controller.buttons[buttons[i]] = true; refresh();
            uint8_t keys[256]; snapshot(keys); unsigned n = downs(scan);
            /* The original action's own key (melee F, Tab, Z, mouse) still
             * passes through untouched; only the physical commit yields. */
            printf("pending %s with %s: %s downs=%u\n", action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD ? "reload" : "pickup",
                   names[i], scan == 0x13 ? "R" : "X", n);
            if (buttons[i] == HOSTGC_BTN_X) CHECK(n <= 1); /* Square is itself the legacy R/E/X key */
            else CHECK(n == 0);
            if (buttons[i] == HOSTGC_BTN_RSHOULDER) CHECK(keys[0x21]);
            if (buttons[i] == HOSTGC_BTN_Y) CHECK(keys[0x0F]);
            if (buttons[i] == HOSTGC_BTN_RTHUMB) CHECK(keys[0x2C]);
            controller.buttons[buttons[i]] = false; refresh(); CHECK(ticks_with(scan, 4) == 0); /* never late */
        }
        /* Owner/target change before delivery (e.g. swapped weapon). */
        begin(); CHECK(physical(1, action)); live_target = 0x45670002; CHECK(ticks_with(scan, 4) == 0);
        live_target = 0x34560001; CHECK(ticks_with(scan, 4) == 0);
        begin(); CHECK(physical(1, action)); live_owner = 0x99990000; CHECK(ticks_with(scan, 4) == 0); live_owner = 0x23450000;
        /* Setting disabled: the matching pending commit is cancelled, others kept. */
        begin(); CHECK(physical(1, action));
        halo_sense_physical_cancel(action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD ? HALO_SENSE_GESTURE_PHYSICAL_PICKUP : HALO_SENSE_GESTURE_PHYSICAL_RELOAD);
        CHECK(ticks_with(scan, 4) == 1);
        begin(); CHECK(physical(1, action)); halo_sense_physical_cancel(action); CHECK(ticks_with(scan, 4) == 0);
        /* A busy/refused sequence never commits later. */
        begin(); CHECK(physical(5, action)); CHECK(!physical(6, action)); CHECK(ticks_with(scan, 6) == 1); CHECK(!physical(6, action));
        CHECK(ticks_with(scan, 6) == 0);
        /* L2 grip held and walking stay allowed. */
        begin(); controller.ly = .9f; controller.spatial_raw_ly = .9f; refresh(); CHECK(physical(1, action)); CHECK(ticks_with(scan, 4) == 1);
    }
    /* Pickup needs L2 still closed at consumption; a reload does not. */
    begin(); CHECK(physical(1, HALO_SENSE_GESTURE_PHYSICAL_PICKUP)); controller.lt = 0; controller.buttons[HOSTGC_BTN_LTRIGGER] = false; refresh();
    CHECK(ticks_with(0x2D, 4) == 0); controller.lt = 1; refresh(); CHECK(ticks_with(0x2D, 4) == 0); /* never late */
    begin(); CHECK(physical(1, HALO_SENSE_GESTURE_PHYSICAL_RELOAD)); controller.lt = 0; controller.buttons[HOSTGC_BTN_LTRIGGER] = false; refresh();
    CHECK(ticks_with(0x13, 4) == 1);
    begin(); controller.lt = 0; controller.buttons[HOSTGC_BTN_LTRIGGER] = false; refresh(); CHECK(!physical(1, HALO_SENSE_GESTURE_PHYSICAL_PICKUP));
    /* R2 owns the moment only against a reload. */
    begin(); CHECK(physical(1, HALO_SENSE_GESTURE_PHYSICAL_RELOAD)); controller.rt = 1; controller.buttons[HOSTGC_BTN_RTRIGGER] = true; refresh();
    CHECK(ticks_with(0x13, 4) == 0);
    begin(); controller.rt = 1; controller.buttons[HOSTGC_BTN_RTRIGGER] = true; refresh(); CHECK(physical(1, HALO_SENSE_GESTURE_PHYSICAL_PICKUP));
    CHECK(ticks_with(0x2D, 4) == 1);
    CHECK(checks_called > 0);
    /* Ordinary pad: no Sense session ownership, nothing synthesized. */
    begin(); controller.spatial = false; refresh(); CHECK(!physical(1, HALO_SENSE_GESTURE_PHYSICAL_RELOAD));
    printf("PASS: physical commit transport: one original R/X tick per sequence, L2-held, gesture-toggle independent, session/generation/menu/Create/discontinuity/busy refusals (%u checks)\n", checks);
    return 0;
}
