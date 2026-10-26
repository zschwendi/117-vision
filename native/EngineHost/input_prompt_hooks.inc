/* Native controller labels in Halo's gameplay prompts. Included once by
 * overrides.c; the address is routed through engine_dispatch by
 * engine_hooks.h.
 *
 * Traced in the original 1.10 executable (read-only, generated instructions):
 *   0048C7F0  binding descriptor -> display name. eax = descriptor, ecx =
 *             UTF-16 output, plain ret, ebx saved. Keyboard keys (kind 1,
 *             object 0) go to 00490E30 and mouse buttons (kind 2, object 0)
 *             to 00490F20; both wcsncpy 0x18 units and terminate at +0x2E, so
 *             they write exactly 24 units. Mouse axes (00491010) write 33.
 *   descriptor {u16 kind (1 keyboard, 2 mouse, 3 joystick), slot, object
 *             (0 button/key, 1 axis, 2 POV), index; u32 direction}.
 *   0048BDE0  action -> descriptor, through the per-action cache at
 *             007127D4 + action*12 (27 actions), which 0048BAE0 fills from
 *             the bindings and the action packet 0048CCA0 refreshes with the
 *             input that last performed each action.
 *   bindings  keyboard u16 action at 00710330 + key*2 (0x6D keys), mouse
 *             buttons 0071040A + button*2 (8), mouse axes 0071041A + axis*4
 *             (+0 direction 1, +2 direction 2; 3 axes).
 *   00490760  DirectInput DIK -> Halo key index: u16 at 0065BD58 + dik*2.
 * Callers of 0048C7F0, by return address:
 *   004AEA6B  004AE550 HUD message draw (HUD 00494730 -> 004A99F0), per draw
 *   0049AF85  0049ADE0 message text expansion (HUD 00494730 -> 0045D700, and
 *             004AE550's text appender 004AD8E0; also UI widget text)
 *   0045F088  0045E680 game-engine message, the scores action
 *   004B44EB  004B44C0 controls binding capture: never relabelled
 * Only the first three are touched. The bindings dump (0048BEA0) and profile
 * data never go through here, and nothing is ever written to Halo's tables.
 *
 * Everything below must be proven, or the original runs untouched: the
 * default controller bridge feeds gameplay (PAD2KEY, no Halo menu), the
 * controller is the active source and its names belong to the same reading,
 * exactly one action's cache entry equals the descriptor and the live
 * binding of that key/button/axis is that action, exactly one bridge-emitted
 * DIK translates to the named key through Halo's own table, the native app
 * does not own the controller, a Sense pair's current filter branch passes
 * that control (halo_sense_prompt_context), and the family names it
 * (input_prompt_label.h).
 * The one action-specific hint is the tracked Sense grenade clutch ("Hold
 * L1"), given only when the proven action is throw grenade (6). The label is
 * ASCII, at most 23 characters, written as 24 zero-padded UTF-16 units:
 * never more than the original writes into the same buffer. */
#include "input_prompt.h"
#include "input_prompt_label.h"
#include <stdatomic.h>

enum {
    HOST_PROMPT_CACHE = 0x007127D4u, HOST_PROMPT_ACTIONS = 27, HOST_PROMPT_DESCRIPTOR = 12,
    HOST_PROMPT_KEYS = 0x00710330u, HOST_PROMPT_KEY_COUNT = 0x6D,
    HOST_PROMPT_MOUSE_BUTTONS = 0x0071040Au, HOST_PROMPT_MOUSE_BUTTON_COUNT = 8,
    HOST_PROMPT_MOUSE_AXES = 0x0071041Au, HOST_PROMPT_MOUSE_AXIS_COUNT = 3,
    HOST_PROMPT_DIK_TO_KEY = 0x0065BD58u, HOST_PROMPT_OUTPUT_UNITS = 24
};
static const uint32_t host_prompt_sites[] = { 0x004AEA6Bu, 0x0049AF85u, 0x0045F088u };
static _Atomic uint64_t host_prompt_labelled, host_prompt_declined;

/* The one action whose cached binding is this descriptor, or -1. */
static int host_prompt_cached_action(uint32_t descriptor) {
    int action = -1;
    for (int a = 0; a < HOST_PROMPT_ACTIONS; a++)
        if (!memcmp(GPTR(HOST_PROMPT_CACHE + (uint32_t)a * HOST_PROMPT_DESCRIPTOR), GPTR(descriptor), HOST_PROMPT_DESCRIPTOR)) {
            if (action >= 0) return -1;
            action = a;
        }
    return action;
}
/* The one controller-emitted DIK that Halo translates to `key`, or 0. */
static uint8_t host_prompt_dik_for_key(unsigned key) {
    uint8_t found = 0;
    for (int i = 0; i < HOST_PROMPT_EMITTER_COUNT; i++) {
        uint8_t dik = host_prompt_emitters[i].dik;
        if ((int16_t)G16(HOST_PROMPT_DIK_TO_KEY + 2u * dik) != (int16_t)key) continue;
        if (found && found != dik) return 0;
        found = dik;
    }
    return found;
}
/* The label for this descriptor, proven against Halo's live tables. */
static int host_prompt_label(uint32_t descriptor, const HostGCPromptNames *names, const HaloSensePromptContext *sense,
                             char out[HOST_PROMPT_LABEL_MAX]) {
    uint16_t kind = G16(descriptor), slot = G16(descriptor + 2), object = G16(descriptor + 4), index = G16(descriptor + 6);
    uint32_t direction = G32(descriptor + 8);
    if (slot != 0 || (kind != 1 && kind != 2)) return 0;
    int action = host_prompt_cached_action(descriptor);
    if (action < 0) return 0;
    if (kind == 1 && object == 0 && index < HOST_PROMPT_KEY_COUNT) {
        if (G16(HOST_PROMPT_KEYS + 2u * index) != (uint16_t)action) return 0;
        uint8_t dik = host_prompt_dik_for_key(index);
        return dik && host_prompt_label_for_dik(names, sense, dik, out);
    }
    if (kind == 2 && object == 0 && index < HOST_PROMPT_MOUSE_BUTTON_COUNT)
        return G16(HOST_PROMPT_MOUSE_BUTTONS + 2u * index) == (uint16_t)action &&
               host_prompt_label_for_mouse_button(names, sense, index, action, out);
    if (kind == 2 && object == 1 && index < HOST_PROMPT_MOUSE_AXIS_COUNT && (direction == 1 || direction == 2))
        return G16(HOST_PROMPT_MOUSE_AXES + 4u * index + (direction == 2 ? 2u : 0u)) == (uint16_t)action &&
               host_prompt_label_for_mouse_axis(names, sense, index, out);
    return 0;
}
static int host_prompt_site(uint32_t return_address) {
    for (size_t i = 0; i < sizeof host_prompt_sites / sizeof *host_prompt_sites; i++)
        if (host_prompt_sites[i] == return_address) return 1;
    return 0;
}
static int host_prompt_dispatch(EngineCPU *cpu, uint32_t address) {
    if (address != 0x0048C7F0u || !engine_flat_base) return 0;
    if (!host_prompt_site(G32(cpu->gpr[4]))) return 0;            /* controls UI and anything else: original */
    uint32_t descriptor = cpu->gpr[0], output = cpu->gpr[1];
    int spatial = 0; uint64_t generation = 0;
    HostGCPromptNames names; HaloSensePromptContext sense = {0}; char label[HOST_PROMPT_LABEL_MAX];
    if (!descriptor || !output || descriptor > UINT32_MAX - HOST_PROMPT_DESCRIPTOR ||
        output > UINT32_MAX - 2u * HOST_PROMPT_OUTPUT_UNITS ||
        !host_dinput_pad2key_enabled() || host_dinput_menu_active() || hostgc_app_input_blocked() ||
        !host_prompt_controller_active(&spatial, &generation) || !hostgc_prompt_names(&names) ||
        names.spatial != (spatial != 0) || names.source_generation != generation ||
        (names.spatial && (!halo_sense_prompt_context(&sense) || sense.controller_generation != generation)) ||
        !host_prompt_label(descriptor, &names, names.spatial ? &sense : NULL, label)) {
        atomic_fetch_add_explicit(&host_prompt_declined, 1, memory_order_relaxed);
        return 0;
    }
    size_t length = strnlen(label, HOST_PROMPT_LABEL_MAX - 1);
    for (uint32_t i = 0; i < HOST_PROMPT_OUTPUT_UNITS; i++)
        S16(output + 2u * i, i < length ? (uint16_t)(uint8_t)label[i] : 0);
    atomic_fetch_add_explicit(&host_prompt_labelled, 1, memory_order_relaxed);
    cpu->pc = engine_pop(cpu, 4);   /* plain ret; ebx, esi, edi, ebp untouched */
    return 1;
}
__attribute__((unused)) static void host_prompt_hook_stats(uint64_t *labelled, uint64_t *declined) {
    if (labelled) *labelled = atomic_load_explicit(&host_prompt_labelled, memory_order_relaxed);
    if (declined) *declined = atomic_load_explicit(&host_prompt_declined, memory_order_relaxed);
}
