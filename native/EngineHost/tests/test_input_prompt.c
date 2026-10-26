/* Native prompt labels: the active-source arbiter (input_prompt.inc), the
 * per-family/per-context resolver (input_prompt_label.h) and the 0048C7F0
 * hook (input_prompt_hooks.inc) on synthetic guest memory. Controller names,
 * the Sense filter branch, the app lease and Halo's menu state are test
 * inputs; no game files, generated code, device or simulator.
 * clang -O2 -pthread -DENGINE_FLAT_MEMORY=1 -I native/EngineHost -I native/EngineReuse \
 *   native/EngineHost/tests/test_input_prompt.c -lm -o /tmp/input-prompt */
#include "host.h"
#include "engine_hooks.h"
#include "../input_prompt.inc"
#include "sense_pose.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static HostGCPromptNames test_names;
static int test_app_blocked, test_menu, test_pad2key = 1;
static HaloSensePromptContext test_sense;
int hostgc_prompt_names(HostGCPromptNames *out) { *out = test_names; return out->family != HOSTGC_PROMPT_FAMILY_NONE; }
int hostgc_app_input_blocked(void) { return test_app_blocked; }
int host_dinput_menu_active(void) { return test_menu; }
int host_dinput_pad2key_enabled(void) { return test_pad2key; }
int halo_sense_prompt_context(HaloSensePromptContext *out) { *out = test_sense; return out->context; }
#include "../input_prompt_hooks.inc"
uint8_t *engine_flat_base;

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)

/* ---- names ------------------------------------------------------------- */
static void set_names(HostGCPromptNames *n, int family, int spatial, const char *const list[HOSTGC_PROMPT_ELEMENTS]) {
    memset(n, 0, sizeof *n); n->family = family; n->spatial = spatial; n->source_generation = 7;
    for (int i = 0; i < HOSTGC_PROMPT_ELEMENTS; i++) if (list[i]) snprintf(n->name[i], sizeof n->name[i], "%s", list[i]);
}
static const char *const dualsense[HOSTGC_PROMPT_ELEMENTS] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2",
    "L3", "R3", "Options", "Create", "", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Left Stick", "Right Stick", "" };
static const char *const dualshock[HOSTGC_PROMPT_ELEMENTS] = { "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2",
    "L3", "R3", "Options", "Share", "", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Left Stick", "Right Stick", "" };
static const char *const xbox[HOSTGC_PROMPT_ELEMENTS] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
    "LS", "RS", "Menu", "View", "", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Left Stick", "Right Stick", "" };
/* What gamecontroller.m composes for a pair whose system metadata gives the
 * PlayStation shapes and these element names (test_gamecontroller_prompt_names.m). */
static const char *const sense[HOSTGC_PROMPT_ELEMENTS] = { "Right Cross", "Right Circle", "Left Square", "Left Triangle",
    "L1", "R1", "L2", "R2", "L3", "R3", "Right Options", "Left Create", "",
    "Create+Left Stick Up", "Create+Left Stick Down", "Create+Left Stick Left", "Create+Left Stick Right",
    "Left Stick", "Right Stick", "Create+R3" };

static const char *dik(const HostGCPromptNames *n, const HaloSensePromptContext *c, uint8_t code) {
    static char out[HOST_PROMPT_LABEL_MAX]; return host_prompt_label_for_dik(n, c, code, out) ? out : NULL;
}
static const char *mouse_action(const HostGCPromptNames *n, const HaloSensePromptContext *c, unsigned b, int action) {
    static char out[HOST_PROMPT_LABEL_MAX]; return host_prompt_label_for_mouse_button(n, c, b, action, out) ? out : NULL;
}
/* Halo's defaults: fire (7) on mouse button 0, throw grenade (6) on button 1. */
static const char *mouse(const HostGCPromptNames *n, const HaloSensePromptContext *c, unsigned b) {
    return mouse_action(n, c, b, b == 1 ? HOST_PROMPT_ACTION_THROW_GRENADE : 7);
}
static const char *axis(const HostGCPromptNames *n, const HaloSensePromptContext *c, unsigned a) {
    static char out[HOST_PROMPT_LABEL_MAX]; return host_prompt_label_for_mouse_axis(n, c, a, out) ? out : NULL;
}
static int is(const char *got, const char *want) { return want ? got && !strcmp(got, want) : !got; }

enum { E = 0x12, R = 0x13, X_KEY = 0x2D, SPACE = 0x39, LCTRL = 0x1D, TAB = 0x0F, F = 0x21, Z = 0x2C,
       Q = 0x10, G = 0x22, F1 = 0x3B, ESC = 0x01, W = 0x11, S_KEY = 0x1F, A_KEY = 0x1E, D = 0x20, ONE = 0x02 };

static void ordinary_families(void) {
    HostGCPromptNames n;
    set_names(&n, HOSTGC_PROMPT_FAMILY_DUALSENSE, 0, dualsense);
    /* Tab (switch weapon) is Triangle; E/R (use/reload) are Square. */
    CHECK(is(dik(&n, NULL, TAB), "Triangle")); CHECK(is(dik(&n, NULL, E), "Square")); CHECK(is(dik(&n, NULL, R), "Square"));
    CHECK(is(dik(&n, NULL, X_KEY), "D-pad Right")); CHECK(is(dik(&n, NULL, SPACE), "Cross"));
    CHECK(is(dik(&n, NULL, LCTRL), "Circle")); CHECK(is(dik(&n, NULL, F), "R1")); CHECK(is(dik(&n, NULL, Z), "L2"));
    CHECK(is(dik(&n, NULL, Q), "D-pad Up")); CHECK(is(dik(&n, NULL, G), "D-pad Down"));
    CHECK(is(dik(&n, NULL, F1), "Create")); CHECK(is(dik(&n, NULL, ESC), "Options"));
    CHECK(is(dik(&n, NULL, W), "Left Stick Up")); CHECK(is(dik(&n, NULL, S_KEY), "Left Stick Down"));
    CHECK(is(dik(&n, NULL, A_KEY), "Left Stick Left")); CHECK(is(dik(&n, NULL, D), "Left Stick Right"));
    CHECK(is(mouse(&n, NULL, 0), "R2")); CHECK(is(mouse(&n, NULL, 1), "L1")); CHECK(is(mouse(&n, NULL, 2), NULL));
    CHECK(is(mouse_action(&n, NULL, 1, 4), "L1"));             /* whatever Halo binds there, L1 presses it */
    CHECK(is(axis(&n, NULL, 0), "Right Stick")); CHECK(is(axis(&n, NULL, 1), "Right Stick")); CHECK(is(axis(&n, NULL, 2), NULL));
    /* Keys the bridge never emits keep Halo's text. */
    CHECK(is(dik(&n, NULL, ONE), NULL)); CHECK(is(dik(&n, NULL, 0x1C), NULL)); CHECK(is(dik(&n, NULL, 0x2A), NULL));
    /* A system-remapped (blank) primary falls to the next real emitter, else none. */
    n.name[HOSTGC_BTN_X][0] = 0;
    CHECK(is(dik(&n, NULL, R), "D-pad Left")); CHECK(is(dik(&n, NULL, E), NULL));
    n.name[HOSTGC_BTN_LTRIGGER][0] = 0; CHECK(is(dik(&n, NULL, Z), "R3"));
    n.name[HOSTGC_BTN_Y][0] = 0; CHECK(is(dik(&n, NULL, TAB), NULL));

    set_names(&n, HOSTGC_PROMPT_FAMILY_DUALSHOCK4, 0, dualshock);
    CHECK(is(dik(&n, NULL, F1), "Share")); CHECK(is(dik(&n, NULL, TAB), "Triangle"));
    set_names(&n, HOSTGC_PROMPT_FAMILY_XBOX, 0, xbox);
    CHECK(is(dik(&n, NULL, TAB), "Y")); CHECK(is(dik(&n, NULL, E), "X")); CHECK(is(dik(&n, NULL, SPACE), "A"));
    CHECK(is(dik(&n, NULL, LCTRL), "B")); CHECK(is(dik(&n, NULL, F), "RB")); CHECK(is(dik(&n, NULL, Z), "LT"));
    CHECK(is(dik(&n, NULL, F1), "View")); CHECK(is(dik(&n, NULL, ESC), "Menu"));
    CHECK(is(mouse(&n, NULL, 0), "RT")); CHECK(is(mouse(&n, NULL, 1), "LB"));
    /* Generic pad: only the system's own unremapped names; missing ones fall back. */
    static const char *const generic[HOSTGC_PROMPT_ELEMENTS] = { "Button A", "Button B", "Button X", NULL };
    set_names(&n, HOSTGC_PROMPT_FAMILY_GENERIC, 0, generic);
    CHECK(is(dik(&n, NULL, E), "Button X")); CHECK(is(dik(&n, NULL, TAB), NULL)); CHECK(is(mouse(&n, NULL, 0), NULL));
    /* No family: nothing. */
    memset(&n, 0, sizeof n); CHECK(is(dik(&n, NULL, TAB), NULL));
    /* 23-character bound with a direction suffix. */
    set_names(&n, HOSTGC_PROMPT_FAMILY_GENERIC, 0, generic);
    snprintf(n.name[HOSTGC_PROMPT_LSTICK], HOSTGC_PROMPT_NAME_MAX, "%s", "Left Thumbstick 12");   /* 18 */
    CHECK(is(dik(&n, NULL, W), "Left Thumbstick 12 Up")); CHECK(is(dik(&n, NULL, D), NULL));     /* 18+6 = 24 */
}

static HaloSensePromptContext ctx(int context, int gestures, int clutch) {
    return (HaloSensePromptContext){ .context = context, .gestures_own_create = gestures, .grenade_clutch = clutch, .controller_generation = 7 };
}
static void sense_contexts(void) {
    HostGCPromptNames n; set_names(&n, HOSTGC_PROMPT_FAMILY_SPATIAL, 1, sense);
    HaloSensePromptContext c = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 1);
    /* Tracked on foot, Quick gestures on. */
    CHECK(is(dik(&n, &c, TAB), "Left Triangle")); CHECK(is(dik(&n, &c, E), "Left Square"));
    CHECK(is(dik(&n, &c, R), "Left Square")); CHECK(is(dik(&n, &c, X_KEY), "Left Square"));
    CHECK(is(dik(&n, &c, SPACE), "Right Cross")); CHECK(is(dik(&n, &c, LCTRL), "Right Circle"));
    CHECK(is(dik(&n, &c, F), "R1")); CHECK(is(dik(&n, &c, Z), "R3"));            /* L2 is the offhand */
    CHECK(is(dik(&n, &c, Q), "Create+Left Stick Up")); CHECK(is(dik(&n, &c, G), "Create+Left Stick Down"));
    CHECK(is(dik(&n, &c, F1), "Create+R3"));                                     /* centered Create is the clutch */
    CHECK(is(dik(&n, &c, ESC), "Right Options"));
    CHECK(is(dik(&n, &c, W), "Left Stick"));                                     /* head-relative: no direction */
    CHECK(is(mouse(&n, &c, 0), "R2")); CHECK(is(mouse(&n, &c, 1), "Hold L1"));
    CHECK(is(axis(&n, &c, 0), NULL));                                            /* tracked aim, stick turns */
    /* Quick gestures off (or not live): centered Create keeps the scoreboard. */
    c = ctx(HALO_SENSE_PROMPT_TRACKED, 0, 1); CHECK(is(dik(&n, &c, F1), "Left Create"));
    /* The clutch hint is for the verified grenade action only: anything else
     * bound to mouse button 1 is never sent by a tracked pair. */
    CHECK(is(mouse_action(&n, &c, 1, 4), NULL)); CHECK(is(mouse_action(&n, &c, 1, 12), NULL));
    CHECK(is(mouse_action(&n, &c, 1, -1), NULL));
    /* The clutch cannot deliver: grenade keeps Halo's text, never a press claim. */
    c = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 0); CHECK(is(mouse(&n, &c, 1), NULL));
    /* Tracking off: the pair is used unchanged, as an ordinary pad. */
    c = ctx(HALO_SENSE_PROMPT_PASSTHROUGH, 0, 0);
    CHECK(is(mouse(&n, &c, 1), "L1")); CHECK(is(dik(&n, &c, Z), "R3")); CHECK(is(axis(&n, &c, 0), "Right Stick"));
    CHECK(is(mouse_action(&n, &c, 1, 4), "L1"));               /* the grip really presses mouse button 1 */
    CHECK(is(dik(&n, &c, W), "Left Stick Up")); CHECK(is(dik(&n, &c, F1), "Left Create")); CHECK(is(dik(&n, &c, X_KEY), "Left Square"));
    n.name[HOSTGC_BTN_RTHUMB][0] = 0; CHECK(is(dik(&n, &c, Z), "L2")); snprintf(n.name[HOSTGC_BTN_RTHUMB], HOSTGC_PROMPT_NAME_MAX, "R3");
    /* Seat: L1/L2 withheld, sticks unrotated. */
    c = ctx(HALO_SENSE_PROMPT_SEATED, 0, 0);
    CHECK(is(mouse(&n, &c, 1), NULL)); CHECK(is(axis(&n, &c, 1), "Right Stick")); CHECK(is(dik(&n, &c, Z), "R3"));
    CHECK(is(dik(&n, &c, F1), "Left Create")); CHECK(is(dik(&n, &c, D), "Left Stick Right"));
    /* Scoped, fresh. */
    c = ctx(HALO_SENSE_PROMPT_SCOPE, 1, 1);
    CHECK(is(dik(&n, &c, TAB), "Left Triangle")); CHECK(is(axis(&n, &c, 0), NULL)); CHECK(is(dik(&n, &c, F1), "Left Create"));
    CHECK(is(mouse(&n, &c, 1), "Hold L1"));
    /* Scoped after hand loss: Menu, zoom and melee only (and the clutch if head ownership holds). */
    c = ctx(HALO_SENSE_PROMPT_SCOPE_HEAD, 0, 1);
    CHECK(is(dik(&n, &c, TAB), NULL)); CHECK(is(dik(&n, &c, E), NULL)); CHECK(is(dik(&n, &c, F), "R1"));
    CHECK(is(dik(&n, &c, Z), "R3")); CHECK(is(dik(&n, &c, ESC), "Right Options")); CHECK(is(mouse(&n, &c, 0), NULL));
    CHECK(is(mouse(&n, &c, 1), "Hold L1"));
    /* Unsupported gameplay with fresh tracking: Use and Menu only. */
    c = ctx(HALO_SENSE_PROMPT_USE_ONLY, 0, 0);
    CHECK(is(dik(&n, &c, E), "Left Square")); CHECK(is(dik(&n, &c, R), "Left Square")); CHECK(is(dik(&n, &c, TAB), NULL));
    CHECK(is(dik(&n, &c, SPACE), NULL)); CHECK(is(mouse(&n, &c, 1), NULL)); CHECK(is(dik(&n, &c, Q), NULL));
    /* Lost tracking: Menu only. Unknown context or none: nothing. */
    c = ctx(HALO_SENSE_PROMPT_MENU_ONLY, 0, 0);
    CHECK(is(dik(&n, &c, ESC), "Right Options")); CHECK(is(dik(&n, &c, E), NULL)); CHECK(is(mouse(&n, &c, 1), NULL));
    c = ctx(HALO_SENSE_PROMPT_NONE, 0, 0); CHECK(is(dik(&n, &c, ESC), NULL));
    CHECK(is(dik(&n, NULL, TAB), NULL)); CHECK(is(mouse(&n, NULL, 1), NULL));
    /* "Hold " + grip fits only within 23 characters. */
    c = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 1);
    snprintf(n.name[HOSTGC_BTN_LSHOULDER], HOSTGC_PROMPT_NAME_MAX, "%s", "Left Grip Button 12");  /* 19: 24 total */
    CHECK(is(mouse(&n, &c, 1), NULL));
    snprintf(n.name[HOSTGC_BTN_LSHOULDER], HOSTGC_PROMPT_NAME_MAX, "%s", "Left Grip Button 1");   /* 18: 23 total */
    CHECK(is(mouse(&n, &c, 1), "Hold Left Grip Button 1"));
    n.name[HOSTGC_BTN_LSHOULDER][0] = 0; CHECK(is(mouse(&n, &c, 1), NULL));
    /* A missing chord name (too long or unknown metadata) falls back. */
    n.name[HOSTGC_PROMPT_CREATE_R3][0] = 0; CHECK(is(dik(&n, &c, F1), NULL));
}

/* ---- arbiter ------------------------------------------------------------ */
static uint64_t seq = 100;
static HostGCSnapshot reading(void) {
    HostGCSnapshot p = { .connected = true, .source_generation = 7 }; p.sequence = ++seq; return p;
}
static int active(void) { return host_prompt_controller_active(NULL, NULL); }
static void arbiter(void) {
    HostPromptStats st;
    CHECK(!active());                                           /* nothing connected yet */
    HostGCSnapshot p = reading(); host_prompt_note_controller(&p);
    CHECK(active());                                            /* default: a connected controller, no edge needed */
    int spatial = -1; uint64_t generation = 0;
    CHECK(host_prompt_controller_active(&spatial, &generation) && spatial == 0 && generation == 7);
    /* Accepted hardware key: keyboard/mouse wins. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY); CHECK(!active());
    /* Idle controller readings, drift below the stick/trigger thresholds,
     * a held button and a repeated reading never take it back. */
    for (int i = 0; i < 50; i++) { p = reading(); p.lx = .28f * ((i & 1) ? 1 : -1); p.ry = .29f; p.lt = .19f; host_prompt_note_controller(&p); }
    CHECK(!active());
    p.buttons[HOSTGC_BTN_HOME] = true; p.sequence = ++seq; host_prompt_note_controller(&p); CHECK(!active());   /* Home is the system's */
    HostGCSnapshot same = p; same.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&same);
    CHECK(!active());                                           /* same sequence: the reading handed out again */
    /* An intentional press takes it back. */
    p = reading(); p.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&p); CHECK(active());
    host_prompt_note_device(HOST_PROMPT_DEVICE_BUTTON); CHECK(!active());
    p = reading(); p.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&p); CHECK(!active());   /* still held: no edge */
    p = reading(); host_prompt_note_controller(&p); CHECK(!active());
    /* Stick deflection (with hysteresis) and trigger pull are activity. */
    p = reading(); p.lx = .55f; host_prompt_note_controller(&p); CHECK(active());
    host_prompt_note_device(HOST_PROMPT_DEVICE_CLICK); CHECK(!active());            /* touch tap */
    p = reading(); p.lx = .45f; host_prompt_note_controller(&p); CHECK(!active());  /* never re-centred below .3 */
    p = reading(); p.lx = .25f; host_prompt_note_controller(&p);
    p = reading(); p.lx = .6f; host_prompt_note_controller(&p); CHECK(active());
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY);
    p = reading(); p.rt = .6f; host_prompt_note_controller(&p); CHECK(active());
    /* Sense: the raw left stick counts even while Create zeroes movement. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY);
    p = reading(); p.spatial = true; host_prompt_note_controller(&p);
    p = reading(); p.spatial = true; p.lx = 0; p.spatial_raw_ly = .8f; host_prompt_note_controller(&p); CHECK(active());
    CHECK(host_prompt_controller_active(&spatial, NULL) && spatial == 1);
    /* Pointer/touch motion: jitter below 8 counts per 250 ms never switches. */
    uint64_t t = 1000000000;
    for (int i = 0; i < 40; i++) { host_prompt_note_motion(1.5, -1.5, t); t += 300000000; }   /* 3 counts, windows apart */
    CHECK(active());
    /* Continuous tiny jitter: 0.1 counts every 10 ms for 5 s is 2.5 per window
     * however long it lasts (the window never slides). */
    for (int i = 0; i < 500; i++) { host_prompt_note_motion(i & 1 ? .1 : -.1, 0, t); t += 10000000; }
    CHECK(active());
    /* The same per-sample size at 1 kHz is 25 counts per window: real motion. */
    for (int i = 0; i < 500; i++) { host_prompt_note_motion(.05, .05, t); t += 1000000; }
    CHECK(!active());
    p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p);
    p = reading(); host_prompt_note_controller(&p); p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p);
    CHECK(active());
    t += 1000000000;
    /* An expired window: 5 counts, then 5 more exactly one window later. */
    host_prompt_note_motion(5, 0, t); host_prompt_note_motion(5, 0, t + HOST_PROMPT_MOTION_WINDOW_NS); CHECK(active());
    host_prompt_note_motion(3, 0, t + HOST_PROMPT_MOTION_WINDOW_NS + 10000000); CHECK(!active());   /* 8 in the new window */
    p = reading(); host_prompt_note_controller(&p); p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p);
    CHECK(active());
    /* A clock that goes backwards starts a new window. */
    t += 1000000000;
    host_prompt_note_motion(6, 0, t); host_prompt_note_motion(6, 0, t - 1000000); CHECK(active());
    /* An intentional burst inside one window. */
    t += 1000000000;
    host_prompt_note_motion(3, 0, t); host_prompt_note_motion(0, 3, t + 100000000); CHECK(active());
    host_prompt_note_motion(-2.5, 0, t + 200000000); CHECK(!active());               /* 8.5 within 250 ms */
    host_prompt_note_motion(NAN, 50, t + 210000000);                                  /* non-finite ignored */
    /* App lease: the next admitted reading is only a baseline, so a press held
     * through the native panel never counts. */
    host_prompt_note_controller_blocked();
    p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p); CHECK(!active());
    p = reading(); host_prompt_note_controller(&p);
    p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p); CHECK(active());
    /* A new connection generation is a baseline too. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY);
    p = reading(); p.source_generation = 8; p.buttons[HOSTGC_BTN_B] = true; host_prompt_note_controller(&p); CHECK(!active());
    /* Disconnect: no controller labels; reconnect keeps keyboard/mouse until a press. */
    host_prompt_note_controller(NULL); CHECK(!active());
    host_prompt_stats(&st); CHECK(!st.connected && st.mode == HOST_PROMPT_MODE_DEVICE);
    p = reading(); host_prompt_note_controller(&p); CHECK(!active());
    p = reading(); p.buttons[HOSTGC_BTN_X] = true; host_prompt_note_controller(&p); CHECK(active());
    host_prompt_note_device(0); host_prompt_note_device(99); CHECK(active());         /* only real presses count */
    host_prompt_stats(&st); /* 1 kHz motion crosses the threshold once per 8 counts: 6 times. */
    CHECK(st.controller_activity == 10 && st.device_activity == 14);
}

/* ---- concurrency: device (main thread) vs controller (engine) vs hook -- */
static _Atomic int stop;
static void *device_thread(void *arg) {
    (void)arg; uint64_t t = 1;
    while (!atomic_load(&stop)) { host_prompt_note_device(HOST_PROMPT_DEVICE_KEY); host_prompt_note_motion(9, 0, t += 1000); }
    return NULL;
}
static void *controller_thread(void *arg) {
    (void)arg; uint64_t s = 1u << 20;
    while (!atomic_load(&stop)) {
        HostGCSnapshot p = { .connected = true, .source_generation = 7, .sequence = ++s };
        p.buttons[HOSTGC_BTN_A] = s & 1; host_prompt_note_controller(&p);
        if ((s & 1023) == 0) host_prompt_note_controller_blocked();
    }
    return NULL;
}
static void *reader_thread(void *arg) {
    (void)arg; unsigned n = 0;
    while (!atomic_load(&stop)) { int spatial; uint64_t g; n += (unsigned)host_prompt_controller_active(&spatial, &g); HostPromptStats st; host_prompt_stats(&st); }
    return (void *)(uintptr_t)n;
}
static void concurrency(void) {
    pthread_t t[4];
    CHECK(!pthread_create(&t[0], NULL, device_thread, NULL)); CHECK(!pthread_create(&t[1], NULL, controller_thread, NULL));
    CHECK(!pthread_create(&t[2], NULL, reader_thread, NULL)); CHECK(!pthread_create(&t[3], NULL, reader_thread, NULL));
    struct timespec d = { 0, 200000000 }; nanosleep(&d, NULL);
    atomic_store(&stop, 1);
    for (int i = 0; i < 4; i++) CHECK(!pthread_join(t[i], NULL));
    /* Settled afterwards: the last real activity decides. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY); CHECK(!active());
    HostGCSnapshot p = reading(); host_prompt_note_controller(&p);
    p = reading(); p.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&p); CHECK(active());
}

/* ---- hook on synthetic guest memory -------------------------------------- */
enum { STACK = 0x100000, DESCRIPTOR = 0x300000, OUTPUT = 0x310000, RA_HUD = 0x004AEA6Bu, RA_TEXT = 0x0049AF85u,
       RA_SCORES = 0x0045F088u, RA_CONTROLS = 0x004B44EBu };
static void reset_tables(void) {
    for (unsigned d = 0; d < 256; d++) S16(HOST_PROMPT_DIK_TO_KEY + 2 * d, 0xFFFF);
    for (unsigned k = 0; k < HOST_PROMPT_KEY_COUNT; k++) S16(HOST_PROMPT_KEYS + 2 * k, 0x7FFF);
    for (unsigned b = 0; b < 8; b++) S16(HOST_PROMPT_MOUSE_BUTTONS + 2 * b, 0x7FFF);
    for (unsigned a = 0; a < 6; a++) S16(HOST_PROMPT_MOUSE_AXES + 2 * a, 0x7FFF);
    memset(GPTR(HOST_PROMPT_CACHE), 0, HOST_PROMPT_ACTIONS * HOST_PROMPT_DESCRIPTOR);
}
static void put_descriptor(uint32_t at, uint16_t kind, uint16_t object, uint16_t index, uint32_t direction) {
    S16(at, kind); S16(at + 2, 0); S16(at + 4, object); S16(at + 6, index); S32(at + 8, direction);
}
/* Halo's default key for an action, as 0048BAE0 caches it after a search. */
static void bind_key(uint16_t action, uint16_t key, uint8_t code) {
    S16(HOST_PROMPT_KEYS + 2u * key, action); S16(HOST_PROMPT_DIK_TO_KEY + 2u * code, key);
    put_descriptor(HOST_PROMPT_CACHE + action * 12u, 1, 0, key, 0);
}
static void bind_mouse(uint16_t action, uint16_t button) {
    S16(HOST_PROMPT_MOUSE_BUTTONS + 2u * button, action); put_descriptor(HOST_PROMPT_CACHE + action * 12u, 2, 0, button, 0);
}
static void ask(uint16_t action) { memcpy(GPTR(DESCRIPTOR), GPTR(HOST_PROMPT_CACHE + action * 12u), 12); }
/* 0048C7F0's register contract: eax descriptor, ecx output, return address on the stack. */
static const char *format(uint32_t ra) {
    static char text[32];
    memset(GPTR(OUTPUT), 0xA5, 0x80);
    EngineCPU cpu = {0};
    cpu.gpr[4] = STACK; S32(STACK, ra); S32(STACK + 4, 0x13579BDF);
    cpu.gpr[0] = DESCRIPTOR; cpu.gpr[1] = OUTPUT; cpu.gpr[3] = 0xB0B0B0B0; cpu.gpr[5] = 0x55; cpu.gpr[6] = 0x66; cpu.gpr[7] = 0x77;
    int taken = host_prompt_dispatch(&cpu, 0x0048C7F0u);
    CHECK(cpu.gpr[3] == 0xB0B0B0B0 && cpu.gpr[5] == 0x55 && cpu.gpr[6] == 0x66 && cpu.gpr[7] == 0x77);
    if (!taken) {
        /* The original runs: nothing written, stack untouched. */
        CHECK(cpu.gpr[4] == STACK && cpu.pc == 0);
        for (unsigned i = 0; i < 0x80; i++) CHECK(G8(OUTPUT + i) == 0xA5);
        return NULL;
    }
    CHECK(cpu.pc == ra && cpu.gpr[4] == STACK + 4 && G32(STACK + 4) == 0x13579BDF);
    unsigned i = 0;
    for (; i < 24 && G16(OUTPUT + 2 * i); i++) { CHECK(G16(OUTPUT + 2 * i) < 0x80); text[i] = (char)G16(OUTPUT + 2 * i); }
    text[i] = 0;
    CHECK(i <= 23);
    for (unsigned j = i; j < 24; j++) CHECK(G16(OUTPUT + 2 * j) == 0);            /* zero-padded like wcsncpy(0x18) */
    for (unsigned b = 48; b < 0x80; b++) CHECK(G8(OUTPUT + b) == 0xA5);         /* never beyond the original's 0x30 bytes */
    return text;
}
static void hook(void) {
    CHECK(engine_hooked(0x0048C7F0u));                         /* ENGINE_DIRECT routes it through dispatch */
    reset_tables();
    set_names(&test_names, HOSTGC_PROMPT_FAMILY_DUALSENSE, 0, dualsense);
    /* Halo key indices are arbitrary here: the hook only follows the game's table. */
    bind_key(3, 30, TAB); bind_key(2, 18, E); bind_key(13, 19, R); bind_key(12, 59, F1); bind_key(19, 17, W);
    bind_mouse(7, 0); bind_mouse(6, 1);
    S16(HOST_PROMPT_MOUSE_AXES + 0, 25); put_descriptor(HOST_PROMPT_CACHE + 25 * 12u, 2, 1, 0, 1);
    HostGCSnapshot p = reading(); host_prompt_note_controller(&p);
    p = reading(); p.buttons[HOSTGC_BTN_A] = true; host_prompt_note_controller(&p); CHECK(active());

    ask(3); CHECK(is(format(RA_HUD), "Triangle")); CHECK(is(format(RA_TEXT), "Triangle")); CHECK(is(format(RA_SCORES), "Triangle"));
    ask(2); CHECK(is(format(RA_HUD), "Square")); ask(13); CHECK(is(format(RA_HUD), "Square"));
    ask(12); CHECK(is(format(RA_SCORES), "Create")); ask(19); CHECK(is(format(RA_HUD), "Left Stick Up"));
    ask(7); CHECK(is(format(RA_HUD), "R2")); ask(6); CHECK(is(format(RA_HUD), "L1")); ask(25); CHECK(is(format(RA_HUD), "Right Stick"));
    /* The controls binding capture and any other caller keep the original. */
    ask(3); CHECK(is(format(RA_CONTROLS), NULL)); CHECK(is(format(0x004AEA66u), NULL)); CHECK(is(format(0), NULL));
    { EngineCPU cpu = {0}; cpu.gpr[4] = STACK; S32(STACK, RA_HUD); cpu.gpr[0] = DESCRIPTOR; cpu.gpr[1] = OUTPUT;
      CHECK(!host_prompt_dispatch(&cpu, 0x0048C7F1u) && cpu.gpr[4] == STACK); }
    /* Gates: keyboard/mouse active, PAD2KEY off, Halo menu, app lease, no names, stale names. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY); CHECK(is(format(RA_HUD), NULL));
    p = reading(); host_prompt_note_controller(&p); p = reading(); p.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&p);
    CHECK(is(format(RA_HUD), "Triangle"));
    test_pad2key = 0; CHECK(is(format(RA_HUD), NULL)); test_pad2key = 1;
    test_menu = 1; CHECK(is(format(RA_HUD), NULL)); test_menu = 0;
    /* hostgc_app_input_blocked covers the native lease and the post-close
     * wait for a neutral reading: labels are off for both, mode unchanged. */
    test_app_blocked = 1; CHECK(is(format(RA_HUD), NULL)); CHECK(active()); test_app_blocked = 0;
    test_names.source_generation = 6; CHECK(is(format(RA_HUD), NULL)); test_names.source_generation = 7;
    test_names.family = HOSTGC_PROMPT_FAMILY_NONE; CHECK(is(format(RA_HUD), NULL)); test_names.family = HOSTGC_PROMPT_FAMILY_DUALSENSE;
    host_prompt_note_controller(NULL); CHECK(is(format(RA_HUD), NULL));
    p = reading(); host_prompt_note_controller(&p); CHECK(is(format(RA_HUD), "Triangle"));
    /* Customized keyboard binding: Tab now does something else, so the cached
     * descriptor no longer proves switch weapon; Halo's text stays. */
    S16(HOST_PROMPT_KEYS + 2 * 30, 5); CHECK(is(format(RA_HUD), NULL));
    /* Switch weapon rebound to "1": the bridge never sends it. */
    bind_key(3, 2, ONE); ask(3); CHECK(is(format(RA_HUD), NULL));
    bind_key(3, 30, TAB); ask(3); CHECK(is(format(RA_HUD), "Triangle"));
    /* Two actions caching the same input are ambiguous. */
    put_descriptor(HOST_PROMPT_CACHE + 9 * 12u, 1, 0, 30, 0); CHECK(is(format(RA_HUD), NULL));
    memset(GPTR(HOST_PROMPT_CACHE + 9 * 12u), 0, 12); CHECK(is(format(RA_HUD), "Triangle"));
    /* Halo's own DIK table maps two bridge keys to the named key: ambiguous. */
    S16(HOST_PROMPT_DIK_TO_KEY + 2 * F, 30); CHECK(is(format(RA_HUD), NULL)); S16(HOST_PROMPT_DIK_TO_KEY + 2 * F, 0xFFFF);
    /* Descriptor shapes the hook never claims: joystick, POV, slot, bad direction, out of range. */
    put_descriptor(DESCRIPTOR, 3, 0, 3, 0); CHECK(is(format(RA_HUD), NULL));
    put_descriptor(DESCRIPTOR, 1, 2, 30, 0); CHECK(is(format(RA_HUD), NULL));
    ask(3); S16(DESCRIPTOR + 2, 1); CHECK(is(format(RA_HUD), NULL));
    put_descriptor(HOST_PROMPT_CACHE + 26 * 12u, 2, 1, 0, 3); ask(26); CHECK(is(format(RA_HUD), NULL));
    put_descriptor(HOST_PROMPT_CACHE + 26 * 12u, 1, 0, 0x6D, 0); ask(26); CHECK(is(format(RA_HUD), NULL));
    memset(GPTR(HOST_PROMPT_CACHE + 26 * 12u), 0, 12);
    /* A descriptor no action caches (stale) is not proven. */
    put_descriptor(DESCRIPTOR, 1, 0, 30, 1); CHECK(is(format(RA_HUD), NULL));

    /* Sense pair: context decides; the generation must match the reading. */
    set_names(&test_names, HOSTGC_PROMPT_FAMILY_SPATIAL, 1, sense);
    test_sense = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 1);
    ask(3); CHECK(is(format(RA_HUD), NULL));                    /* the reading is not spatial */
    p = reading(); p.spatial = true; host_prompt_note_controller(&p);
    CHECK(is(format(RA_HUD), "Left Triangle"));
    ask(6); CHECK(is(format(RA_HUD), "Hold L1")); ask(12); CHECK(is(format(RA_SCORES), "Create+R3"));
    bind_key(5, 16, Q); ask(5); CHECK(is(format(RA_HUD), "Create+Left Stick Up"));
    snprintf(test_names.name[HOSTGC_BTN_DPAD_UP], HOSTGC_PROMPT_NAME_MAX, "%s", "Create+Left Stick Right"); /* 23: fits */
    CHECK(is(format(RA_HUD), "Create+Left Stick Right"));
    ask(12);
    test_sense.gestures_own_create = 0; CHECK(is(format(RA_SCORES), "Left Create"));
    /* Melee rebound to mouse button 1: a tracked pair never presses it, so
     * the clutch hint is not given; untracked, the grip really presses it. */
    test_sense = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 1);
    memset(GPTR(HOST_PROMPT_CACHE + 6 * 12u), 0, 12); bind_mouse(4, 1); ask(4); CHECK(is(format(RA_HUD), NULL));
    test_sense = ctx(HALO_SENSE_PROMPT_PASSTHROUGH, 0, 0); CHECK(is(format(RA_HUD), "L1"));
    memset(GPTR(HOST_PROMPT_CACHE + 4 * 12u), 0, 12); bind_mouse(6, 1);
    test_sense = ctx(HALO_SENSE_PROMPT_TRACKED, 0, 0);
    test_sense.grenade_clutch = 0; ask(6); CHECK(is(format(RA_HUD), NULL));
    test_sense = ctx(HALO_SENSE_PROMPT_PASSTHROUGH, 0, 0); CHECK(is(format(RA_HUD), "L1"));
    test_sense = ctx(HALO_SENSE_PROMPT_SEATED, 0, 0); CHECK(is(format(RA_HUD), NULL));
    test_sense = ctx(HALO_SENSE_PROMPT_TRACKED, 1, 1); test_sense.controller_generation = 6; CHECK(is(format(RA_HUD), NULL));
    test_sense = ctx(HALO_SENSE_PROMPT_NONE, 0, 0); ask(3); CHECK(is(format(RA_HUD), NULL));

    uint64_t labelled, declined; host_prompt_hook_stats(&labelled, &declined); CHECK(labelled > 10 && declined > 20);
}

int main(void) {
    engine_flat_base = calloc(1, 8 * 1024 * 1024); CHECK(engine_flat_base);
    ordinary_families(); sense_contexts(); arbiter(); concurrency(); hook();
    free(engine_flat_base);
    printf("PASS %u native prompt label checks: default connected controller, keyboard/mouse/touch takeover and pad "
           "recovery, drift/held/repeat/lease/generation edges, per-family and per-Sense-context labels (Hold L1, "
           "Create+R3), rebinding/ambiguity/shape fallbacks, 24-unit UTF-16 bound, controls-caller exclusion, concurrency "
           "(synthetic; no device)\n", checks);
    return 0;
}
