/* Hardware keyboard, mouse/trackpad and touch input for the mobile app.
 *
 * A distinct, synchronized source that DirectInput merges only when Halo
 * reads its keyboard or mouse device. It never writes the legacy
 * host_keyboard_state/host_mouse_* globals (PAD2KEY and the gaze pointer own
 * those), never reads or clears the controller handback gate, and is gated by
 * two independent conditions: the app says the game surface owns input, and
 * no native app-input lease token is held (hostgc_app_input_lease_active).
 *
 * Exactly one keyboard source and one pointer source are selected at a time;
 * events from any other source are rejected, so GCKeyboard/GCMouse and the
 * UIKit fallbacks cannot inject the same press twice. Direct touch is its own
 * source and only aims or sends click pulses.
 *
 * Every ownership change, explicit release and source change is a fence: held
 * keys/buttons, pulses and deltas are dropped and the generation advances.
 * A press held across a fence never resumes; it must be pressed again.
 * Buffered DirectInput keyboard events carry the generation and are pruned
 * when it changes. Safe from any thread. */
#ifndef HALO_DEVICE_INPUT_H
#define HALO_DEVICE_INPUT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HOST_DEVICE_SOURCE_NONE = 0,
    HOST_DEVICE_KEYBOARD_GC = 1,     /* GCKeyboard keyChangedHandler */
    HOST_DEVICE_KEYBOARD_UIKIT = 2,  /* UIPress/UIKey on the game surface */
    HOST_DEVICE_POINTER_GC = 3,      /* GCMouse (relative, pointer locked) */
    HOST_DEVICE_POINTER_UIKIT = 4,   /* indirect pointer touches and hover */
    HOST_DEVICE_TOUCH = 5            /* direct touch aim and click pulses */
};
enum {
    HOST_DEVICE_BUTTON_PRIMARY = 0,  /* DIMOUSESTATE rgbButtons[0] */
    HOST_DEVICE_BUTTON_SECONDARY = 1,
    HOST_DEVICE_BUTTON_MIDDLE = 2,
    HOST_DEVICE_BUTTON_COUNT = 8
};
/* Delivery of short presses. Halo overwrites its control record every frame
 * and runs 0..N 30 Hz ticks after it (see the timing note in
 * device_input.inc), so a press visible only between ticks is lost. Every
 * press (hardware edge or touch click) is queued, held down until a game
 * tick has run after the first read that showed it, then released for at
 * least one tick before the next queued press. In native menus (and with
 * no valid game clock) each phase spans HOST_DEVICE_MENU_PUMPS input pumps,
 * counted by Halo's once-per-pump mouse read (repeated keyboard reads within
 * one pump never end it), and at least HOST_DEVICE_MENU_MIN_NS, like a quick
 * physical click; menu pacing beyond the pump is not traced. A stalled tick or pump never
 * holds a press longer than HOST_DEVICE_PULSE_MAX_NS. At most
 * HOST_DEVICE_PULSE_QUEUE presses wait per key/button; real holds are
 * reported independently and are never released by a finishing pulse. */
enum { HOST_DEVICE_PULSE_QUEUE = 4, HOST_DEVICE_MENU_PUMPS = 2 };
#define HOST_DEVICE_MENU_MIN_NS 50000000ull
#define HOST_DEVICE_PULSE_MAX_NS 250000000ull
/* Pending relative motion is bounded per axis, in DirectInput mouse counts. */
#define HOST_DEVICE_DELTA_LIMIT 2048.0

/* USB HID keyboard usage (page 7; UIKeyboardHIDUsage and GCKeyCode share the
 * values) -> DirectInput DIK scan code, or 0 when unmapped. Command (GUI)
 * keys are unmapped: they belong to the system. */
uint8_t host_device_dik_for_hid(uint32_t usage);

/* Nonzero while the game surface owns input. Any change is a fence. */
void host_device_input_set_game_owns(int owns);
/* Fence without changing ownership: focus or capture loss, disconnect. */
void host_device_input_release(void);
/* Select the active keyboard and pointer sources. A change fences. */
void host_device_input_select(int keyboard_source, int pointer_source);

/* Returns 1 when the event changed game input. system_chord: Command is held,
 * so the press belongs to the system; Command-period alone maps to Escape
 * (the standard Apple cancel chord; iPad keyboards may have no Esc key). */
int host_device_input_key(int source, uint32_t hid_usage, int down, int system_chord);
/* One queued Escape-style press for an explicit app affordance (the overlay
 * Pause button); same gates and delivery as a key, no source required. */
int host_device_input_key_pulse(uint8_t dik);
int host_device_input_button(int source, int button, int down);
/* One complete click: queued, shown down across a game tick, then up. */
int host_device_input_click(int source, int button);
/* A cancelled pointer stream: drop the button's hold and queued presses
 * without a new press; a press Halo already saw still gets its release. */
int host_device_input_button_cancel(int source, int button);
/* Relative motion in mouse counts, +x right, +y down (DirectInput). */
int host_device_input_motion(int source, double dx, double dy);

uint64_t host_device_input_generation(void);
/* host_dinput_keyboard_state plus this source's keys (may be NULL) and their
 * generation from the same snapshot, for the USER32 Return/Escape mirror. */
uint64_t host_dinput_keyboard_state_with_device(uint8_t out[256], uint8_t device_keys[256]);
typedef struct {
    int game_owns, keyboard_source, pointer_source;
    uint64_t generation, accepted, rejected, fences, keyboard_reads, mouse_reads;
    int keys_down, buttons_down;
} HostDeviceInputStats;
void host_device_input_stats(HostDeviceInputStats *out);

#ifdef __cplusplus
}
#endif
#endif
