/* Halo's original binding-name routine 0048C7F0 (lifted, read-only generated
 * source on the include path) behind the prompt hook, routed as the
 * translated callers route it (ENGINE_DIRECT -> engine_dispatch). Shows the
 * register contract the hook relies on (eax descriptor, ecx output passed on
 * as ebx to the key/mouse-button namers, plain ret, ebx restored), that the
 * controls binding caller and every unproven case run the original, and that
 * a proven gameplay prompt returns the label without running it. The name
 * routines it calls are recorded stand-ins: no game data is needed. SKIP
 * without the generated sources; never embeds them. */
#if !__has_include("sub_0048C7F0.c")
#include <stdio.h>
int main(void) { puts("SKIP: generated sub_0048C7F0.c is not on the include path"); return 0; }
#else
#include "host.h"
#include "engine_hooks.h"
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
#include "sub_0048C7F0.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#include "../input_prompt.inc"
#include "sense_pose.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static HostGCPromptNames test_names;
static HaloSensePromptContext test_sense;
int hostgc_prompt_names(HostGCPromptNames *out) { *out = test_names; return out->family != HOSTGC_PROMPT_FAMILY_NONE; }
int hostgc_app_input_blocked(void) { return 0; }
int host_dinput_menu_active(void) { return 0; }
int host_dinput_pad2key_enabled(void) { return 1; }
int halo_sense_prompt_context(HaloSensePromptContext *out) { *out = test_sense; return out->context; }
#include "../input_prompt_hooks.inc"
uint8_t *engine_flat_base;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)

/* Recorded stand-ins for the name routines the original dispatches to. */
static struct { uint32_t callee, eax, ebx, esi, arg0, arg1; unsigned calls; } seen;
static void named(EngineCPU *cpu, uint32_t callee, uint32_t out) {
    seen.callee = callee; seen.eax = cpu->gpr[0]; seen.ebx = cpu->gpr[3]; seen.esi = cpu->gpr[6];
    seen.arg0 = G32(cpu->gpr[4] + 4); seen.arg1 = G32(cpu->gpr[4] + 8); seen.calls++;
    static const char orig[] = "ORIG";
    for (unsigned i = 0; i < sizeof orig; i++) S16(out + 2 * i, (uint8_t)orig[i]);
    cpu->pc = engine_pop(cpu, 4);
}
void sub_00490E30(EngineCPU *cpu) { named(cpu, 0x00490E30u, cpu->gpr[3]); }   /* key: eax index, ebx out */
void sub_00490F20(EngineCPU *cpu) { named(cpu, 0x00490F20u, cpu->gpr[3]); }   /* mouse button */
void sub_00491010(EngineCPU *cpu) { named(cpu, 0x00491010u, cpu->gpr[6]); }   /* mouse axis: esi out */
void sub_00491270(EngineCPU *cpu) { named(cpu, 0x00491270u, cpu->gpr[3]); }
void sub_00491340(EngineCPU *cpu) { named(cpu, 0x00491340u, cpu->gpr[3]); }
void sub_004914C0(EngineCPU *cpu) { named(cpu, 0x004914C0u, cpu->gpr[3]); }
/* What engine_dispatch_override does for this entry, then the original. */
void engine_dispatch(EngineCPU *cpu, uint32_t address) {
    CHECK(address == 0x0048C7F0u);
    if (host_prompt_dispatch(cpu, address)) return;
    cpu->pc = address; sub_0048C7F0(cpu);
}

enum { STACK = 0x100000, DESCRIPTOR = 0x300000, OUTPUT = 0x310000, CALLER_EBX = 0x0BADF00Du };
static void put(uint16_t kind, uint16_t object, uint16_t index, uint32_t direction) {
    S16(DESCRIPTOR, kind); S16(DESCRIPTOR + 2, 0); S16(DESCRIPTOR + 4, object); S16(DESCRIPTOR + 6, index); S32(DESCRIPTOR + 8, direction);
}
/* A translated caller: push the return address, ENGINE_DIRECT the entry. */
static const char *call_from(uint32_t ra) {
    static char text[32];
    memset(GPTR(OUTPUT), 0, 0x80); memset(&seen, 0, sizeof seen);
    EngineCPU cpu = {0};
    cpu.gpr[4] = STACK; cpu.gpr[0] = DESCRIPTOR; cpu.gpr[1] = OUTPUT; cpu.gpr[3] = CALLER_EBX; cpu.gpr[6] = 0x66; cpu.gpr[7] = 0x77;
    engine_push(&cpu, ra, 4);
    ENGINE_DIRECT(&cpu, 0x0048C7F0u, sub_0048C7F0);
    CHECK(cpu.pc == ra && cpu.gpr[4] == STACK && cpu.gpr[3] == CALLER_EBX && cpu.gpr[7] == 0x77);
    unsigned i = 0;
    for (; i < 31 && G16(OUTPUT + 2 * i); i++) text[i] = (char)G16(OUTPUT + 2 * i);
    text[i] = 0; return text;
}

int main(void) {
    engine_flat_base = calloc(1, 8 * 1024 * 1024); CHECK(engine_flat_base);
    CHECK(engine_hooked(0x0048C7F0u));
    /* Tables as Halo's search leaves them: Tab (key 30) is switch weapon (3). */
    for (unsigned d = 0; d < 256; d++) S16(HOST_PROMPT_DIK_TO_KEY + 2 * d, 0xFFFF);
    for (unsigned k = 0; k < HOST_PROMPT_KEY_COUNT; k++) S16(HOST_PROMPT_KEYS + 2 * k, 0x7FFF);
    S16(HOST_PROMPT_KEYS + 2 * 30, 3); S16(HOST_PROMPT_DIK_TO_KEY + 2 * 0x0F, 30);
    put(1, 0, 30, 0); memcpy(GPTR(HOST_PROMPT_CACHE + 3 * 12), GPTR(DESCRIPTOR), 12);
    static const char *const dualsense[HOSTGC_PROMPT_ELEMENTS] = { "Cross", "Circle", "Square", "Triangle" };
    test_names.family = HOSTGC_PROMPT_FAMILY_DUALSENSE; test_names.source_generation = 7;
    for (int i = 0; i < 4; i++) snprintf(test_names.name[i], HOSTGC_PROMPT_NAME_MAX, "%s", dualsense[i]);
    HostGCSnapshot pad = { .connected = true, .source_generation = 7, .sequence = 1 };
    host_prompt_note_controller(&pad);

    /* Controls binding capture (004B44C0): the original runs, and its key
     * branch hands the index in eax and the caller's ecx as ebx. */
    CHECK(!strcmp(call_from(0x004B44EBu), "ORIG"));
    CHECK(seen.calls == 1 && seen.callee == 0x00490E30u && (seen.eax & 0xFFFF) == 30 && seen.ebx == OUTPUT);
    /* Gameplay HUD prompt (004AE550), controller active: label, no original. */
    CHECK(!strcmp(call_from(0x004AEA6Bu), "Triangle") && seen.calls == 0);
    CHECK(!strcmp(call_from(0x0049AF85u), "Triangle") && seen.calls == 0);
    CHECK(!strcmp(call_from(0x0045F088u), "Triangle") && seen.calls == 0);
    /* Keyboard/mouse active: the same prompt runs the original. */
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY);
    CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.calls == 1 && seen.callee == 0x00490E30u);
    pad.sequence = 2; host_prompt_note_controller(&pad);
    pad.sequence = 3; pad.buttons[HOSTGC_BTN_Y] = true; host_prompt_note_controller(&pad);
    CHECK(!strcmp(call_from(0x004AEA6Bu), "Triangle"));
    /* The rest of the original's dispatch, for shapes the hook never claims. */
    put(2, 0, 1, 0); CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.callee == 0x00490F20u && (seen.eax & 0xFFFF) == 1 && seen.ebx == OUTPUT);
    put(2, 1, 0, 1); CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.callee == 0x00491010u && seen.esi == OUTPUT && seen.arg0 == 0 && seen.arg1 == 1);
    put(3, 0, 3, 0); CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.callee == 0x00491270u);
    put(3, 1, 2, 1); CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.callee == 0x00491340u);
    put(3, 2, 0, 4); CHECK(!strcmp(call_from(0x004AEA6Bu), "ORIG") && seen.callee == 0x004914C0u);
    /* A keyboard descriptor with a non-key object: the original writes nothing. */
    put(1, 1, 30, 0); CHECK(!strcmp(call_from(0x004AEA6Bu), "") && seen.calls == 0);
    free(engine_flat_base);
    printf("PASS %u checks against the original 0048C7F0: register contract, controls caller and unproven cases "
           "keep the original, proven gameplay prompts take the label without it (generated source, read only)\n", checks);
    return 0;
}
#endif
