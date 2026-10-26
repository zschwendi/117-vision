/* The prompt source (input_prompt.inc) through the production input routes:
 * the hardware keyboard/mouse/touch source (device_input.inc), the native app
 * lease (gamecontroller.m) and DirectInput's controller admission
 * (dinput8.c keyboard_base_state). Controller hardware is replaced by
 * injected snapshots and time by a test clock; no app, device or simulator.
 * clang -O2 -fobjc-arc -fblocks -I native/EngineHost -I native/EngineReuse -lm \
 *   native/EngineHost/tests/test_input_prompt_device.m -framework Foundation \
 *   -framework CoreHaptics -framework GameController -o /tmp/input-prompt-device */
#import <Foundation/Foundation.h>
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
static uint64_t test_ns = UINT64_C(1000000000);
static uint64_t test_uptime(clockid_t clock) { (void)clock; return test_ns; }
static int test_monotonic(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec = test_ns / 1000000000; out->tv_nsec = test_ns % 1000000000; return 0;
}
#define clock_gettime_nsec_np test_uptime
#include "../gamecontroller.m"
#undef clock_gettime_nsec_np
#define clock_gettime test_monotonic
#include "../halo_settings.c"
#undef clock_gettime
#include "../dinput8.c"
uint8_t *engine_flat_base;
static uint32_t heap_next = 0x10000;
void host_log(const char *fmt, ...) { (void)fmt; }
int halo_haptics_enabled(void) { return 1; }
_Noreturn void host_exit(int code) { (void)code; abort(); }
uint32_t guest_alloc(uint32_t size) { uint32_t p = heap_next; heap_next += (size + 15) & ~15u; assert(heap_next < 0x100000); return p; }
void guest_free(uint32_t p) { (void)p; }
uint32_t host_proc_address(const char *a, const char *b) { (void)a; (void)b; return 0; }
uint64_t host_monotonic_ns(void) { return test_ns; }
uint32_t host_call_guest(EngineCPU *c, uint32_t fn, int n, const uint32_t *args, int pops) { (void)c; (void)fn; (void)n; (void)args; (void)pops; return 0; }

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
static HostGCSnapshot pad;
static uint8_t keys[256];
/* One admitted controller reading, as Halo's DirectInput keyboard read takes
 * it: a new hardware capture has a new sequence (remember_reading). */
static uint64_t captures;
static void read_with(HostGCSnapshot p) {
    pad = p; pad.source_generation = hostgc_source_generation(); pad.sequence = ++captures; test_ns += 5000000;
    hostgc_inject_test_snapshot(&pad); host_dinput_keyboard_state(keys);
}
static HostGCSnapshot neutral(void) { return (HostGCSnapshot){ .connected = true }; }
static HostGCSnapshot pressing(int button) { HostGCSnapshot p = neutral(); p.buttons[button] = true; return p; }
static int controller(void) { return host_prompt_controller_active(NULL, NULL); }
static uint64_t device_activity(void) { HostPromptStats st; host_prompt_stats(&st); return st.device_activity; }
enum { U_E = 0x08, U_W = 0x1A, KB_GC = HOST_DEVICE_KEYBOARD_GC, KB_UI = HOST_DEVICE_KEYBOARD_UIKIT,
       PT_GC = HOST_DEVICE_POINTER_GC, TOUCH = HOST_DEVICE_TOUCH };

int main(void) { @autoreleasepool {
    g_inited = true; engine_flat_base = calloc(1, 8 * 1024 * 1024); CHECK(engine_flat_base);
    unsetenv("HALO_PAD2KEY");
    host_device_input_set_game_owns(1); host_device_input_select(KB_GC, PT_GC);
    /* No controller: Halo's own text. */
    read_with((HostGCSnapshot){0}); CHECK(!controller());
    /* A connected controller is the default, before any press, even with an
     * idle hardware keyboard selected. */
    read_with(neutral()); CHECK(controller());
    for (int i = 0; i < 10; i++) read_with(neutral());
    CHECK(controller() && device_activity() == 0);
    /* The bridge's own keys from the controller (Triangle -> Tab) and legacy
     * host_keyboard_state writers are never keyboard use. */
    read_with(pressing(HOSTGC_BTN_Y)); CHECK(keys[0x0F] == 0x80 && controller());
    host_keyboard_state[0x12] = 0x80; read_with(neutral()); CHECK(keys[0x12] == 0x80 && controller()); host_keyboard_state[0x12] = 0;
    CHECK(device_activity() == 0);
    /* The overlay's Pause pulse is an app affordance, not keyboard use. */
    CHECK(host_device_input_key_pulse(0x01)); CHECK(controller() && device_activity() == 0);
    /* Rejected keyboard input never counts: unselected source, unmapped key. */
    CHECK(!host_device_input_key(KB_UI, U_E, 1, 0)); CHECK(!host_device_input_key(KB_GC, 0xE3, 1, 0));
    CHECK(controller() && device_activity() == 0);
    /* An accepted key takes over; its repeat and release add nothing. */
    CHECK(host_device_input_key(KB_GC, U_E, 1, 0)); CHECK(!controller() && device_activity() == 1);
    CHECK(!host_device_input_key(KB_GC, U_E, 1, 0)); CHECK(device_activity() == 1);
    CHECK(host_device_input_key(KB_GC, U_E, 0, 0)); CHECK(device_activity() == 1);
    /* The same capture handed out again (reuse window) is not a new press. */
    read_with(neutral()); pad.buttons[HOSTGC_BTN_A] = true; hostgc_inject_test_snapshot(&pad); host_dinput_keyboard_state(keys);
    CHECK(!controller());
    /* Drift and a held control never take it back; a fresh press does. */
    HostGCSnapshot drift = neutral(); drift.lx = .25f; drift.rt = .15f; read_with(drift); read_with(neutral());
    read_with(pressing(HOSTGC_BTN_A)); CHECK(controller());
    /* Touch taps and real touch/pointer movement are keyboard/mouse use. */
    CHECK(host_device_input_click(TOUCH, 0)); CHECK(!controller());
    read_with(neutral()); read_with(pressing(HOSTGC_BTN_X)); CHECK(controller());
    /* Continuous touch/trackpad jitter (0.1 count every 10 ms for 3 s) is
     * accepted input but never real movement. */
    for (int i = 0; i < 300; i++) { CHECK(host_device_input_motion(TOUCH, i & 1 ? .1 : -.1, 0)); test_ns += 10000000; }
    CHECK(controller());
    for (int i = 0; i < 300; i++) { CHECK(host_device_input_motion(PT_GC, 0, .1)); test_ns += 10000000; }
    CHECK(controller());
    test_ns += 400000000;
    CHECK(host_device_input_motion(TOUCH, 2, 2)); CHECK(controller());          /* 4 counts: jitter */
    test_ns += 400000000;
    CHECK(host_device_input_motion(TOUCH, 3, 0)); CHECK(controller());          /* window expired: 3 */
    CHECK(host_device_input_motion(TOUCH, 6, 0)); CHECK(!controller());         /* 9 within 250 ms */
    read_with(neutral()); read_with(pressing(HOSTGC_BTN_B)); CHECK(controller());
    CHECK(host_device_input_button(PT_GC, 0, 1)); CHECK(!controller());
    CHECK(host_device_input_button(PT_GC, 0, 0));
    uint64_t before = device_activity();
    CHECK(!host_device_input_button(TOUCH, 0, 1)); CHECK(device_activity() == before);   /* not the selected pointer */
    /* Native app lease: keyboard/mouse input is refused and does not count. */
    read_with(neutral()); read_with(pressing(HOSTGC_BTN_Y)); CHECK(controller());
    uint64_t token = hostgc_app_input_begin();
    CHECK(!host_device_input_key(KB_GC, U_W, 1, 0)); CHECK(!host_device_input_click(TOUCH, 0));
    CHECK(!host_device_input_motion(PT_GC, 50, 0)); CHECK(controller());
    /* Controller input under the lease is neutralized: a press held through
     * the panel and its release before handback never counts either. */
    host_device_input_set_game_owns(0); host_device_input_set_game_owns(1);
    CHECK(host_device_input_key(KB_GC, U_W, 0, 0) == 0);
    hostgc_app_input_end(token);
    CHECK(host_device_input_key(KB_GC, U_W, 1, 0)); CHECK(!controller());
    CHECK(host_device_input_key(KB_GC, U_W, 0, 0));
    token = hostgc_app_input_begin();
    read_with(pressing(HOSTGC_BTN_Y)); CHECK(!controller());                  /* filtered: no activity */
    hostgc_app_input_end(token);
    read_with(pressing(HOSTGC_BTN_Y)); CHECK(!controller());                  /* handback waits for neutral */
    read_with(neutral()); read_with(pressing(HOSTGC_BTN_Y)); CHECK(controller());
    /* Game surface not owning input (focus loss): nothing accepted. */
    host_device_input_set_game_owns(0); CHECK(!host_device_input_key(KB_GC, U_E, 1, 0)); CHECK(controller());
    host_device_input_set_game_owns(1);
    /* Disconnect: no controller labels; reconnect resumes the default. */
    read_with((HostGCSnapshot){0}); CHECK(!controller());
    read_with(neutral()); CHECK(controller());
    hostgc_clear_test_snapshot(); free(engine_flat_base);
    printf("PASS %u prompt-source checks through device_input, the app lease and DirectInput admission: default "
           "controller, bridge/legacy/pulse keys ignored, accepted key/button/tap/motion takeover, repeats/rejects/"
           "lease/focus ignored, drift ignored, press recovery, disconnect (synthetic; no device)\n", checks);
}}
