/* Which input the player is actually using, for native prompt labels.
 *
 * Halo's prompts name keyboard keys because the default controller bridge
 * (HALO_PAD2KEY) feeds the controller to Halo as keys and mouse input. The
 * prompt hook (input_prompt_hooks.inc) relabels a prompt with the control
 * that produces the named input only while the controller is the active
 * source; everything else keeps Halo's original text.
 *
 * The controller is the active source by default whenever one is connected.
 * Accepted hardware keyboard, mouse/trackpad or touch input (device_input.h)
 * makes keyboard/mouse active; an intentional controller press or stick or
 * trigger movement makes the controller active again. Only accepted input
 * counts:
 *   controller: the reading DirectInput admits after the native app lease
 *     (hostgc_app_input_filter), never raw hostgc_poll, which also serves the
 *     app UI. A button rising (Home excluded), a stick leaving its centre
 *     (magnitude >= .5 after <= .3) or a trigger pulled (>= .5 after <= .2).
 *     A repeated reading (same sequence), the first reading after a
 *     connection change or an app-lease block, drift and held controls are
 *     never activity. Sense poses are not part of the reading at all.
 *   keyboard/mouse/touch: a new key or button down, a touch click, or at
 *     least HOST_PROMPT_MOTION_COUNTS of pointer/touch motion within one
 *     fixed HOST_PROMPT_MOTION_WINDOW_NS window (it starts at the first
 *     sample and never slides), each only after device_input accepted it
 *     (game owns input, no lease, selected source). Rejected events, key
 *     repeats, releases, fences, source selection and the overlay's Escape
 *     pulse never count, and keys the bridge itself synthesizes from the
 *     controller or Sense gestures never pass through here.
 *
 * While the native app owns the controller (its lease, or the wait for a
 * neutral reading after it closes: hostgc_app_input_blocked) prompts keep
 * Halo's text; the source itself does not change, and the first admitted
 * reading afterwards is only a baseline.
 *
 * Safe from any thread. One leaf mutex guards the controller edge and motion
 * state; it is never held while another lock is taken, and callers never hold
 * g_lock or device_lock when they call in. */
#ifndef HALO_INPUT_PROMPT_H
#define HALO_INPUT_PROMPT_H
#include <stdint.h>
#include "gamecontroller.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { HOST_PROMPT_MODE_CONTROLLER = 0, HOST_PROMPT_MODE_DEVICE = 1 };
enum { HOST_PROMPT_DEVICE_KEY = 1, HOST_PROMPT_DEVICE_BUTTON = 2, HOST_PROMPT_DEVICE_CLICK = 3 };
#define HOST_PROMPT_MOTION_COUNTS 8.0
#define HOST_PROMPT_MOTION_WINDOW_NS 250000000ull

/* An admitted controller reading; NULL or disconnected means no controller. */
void host_prompt_note_controller(const HostGCSnapshot *accepted);
/* The native app owns the controller: the next admitted reading is a baseline. */
void host_prompt_note_controller_blocked(void);
/* An accepted hardware keyboard/mouse/touch press (HOST_PROMPT_DEVICE_*). */
void host_prompt_note_device(int kind);
/* Accepted relative pointer or touch motion, in DirectInput counts. */
void host_prompt_note_motion(double dx, double dy, uint64_t now_ns);

/* 1 when prompts may name controller controls: the controller is the active
 * source and connected. Reports the reading's spatial flag and generation. */
int host_prompt_controller_active(int *spatial, uint64_t *source_generation);

typedef struct {
    int mode, connected, spatial;
    uint64_t source_generation, controller_activity, device_activity;
} HostPromptStats;
void host_prompt_stats(HostPromptStats *out);

/* dinput8.c: the controller bridge feeds Halo keys/mouse (HALO_PAD2KEY != 0),
 * and Halo's own menu state, which uses the bridge's menu mapping instead. */
int host_dinput_pad2key_enabled(void);
int host_dinput_menu_active(void);

#ifdef __cplusplus
}
#endif
#endif
