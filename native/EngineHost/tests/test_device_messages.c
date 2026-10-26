/* USER32 Return/Escape mirror (shims_misc.c) with hardware-keyboard
 * provenance: a queued WM_KEYDOWN, or the WM_CHAR translated from it, that only
 * the device source produced is removed by a device fence; releases and
 * legacy/controller messages are unchanged. The merged key state is a
 * controlled stub; the production shim code is included verbatim.
 * clang -O2 -DENGINE_FLAT_MEMORY=1 -I native/EngineHost -I native/EngineReuse -ffunction-sections \
 *   -fdata-sections -Wl,-dead_strip native/EngineHost/tests/test_device_messages.c -o /tmp/device-messages */
#include "../shims_misc.c"
#include <assert.h>

uint8_t *engine_flat_base;
uint32_t host_main_hwnd = 0x00010001u;
uint8_t host_keyboard_state[256];
static uint8_t merged[256], device[256];
static uint64_t generation = 1, now_ns = 1000000000ull;
void host_log(const char *fmt, ...) { (void)fmt; }
uint64_t host_monotonic_ns(void) { return now_ns; }
uint32_t host_call_guest(EngineCPU *cpu, uint32_t fn, int n, const uint32_t *args, int pops) {
    (void)cpu; (void)fn; (void)n; (void)args; (void)pops; return 0;
}
uint64_t host_dinput_keyboard_state_with_device(uint8_t out[256], uint8_t device_keys[256]) {
    for (int i = 0; i < 256; i++) out[i] = merged[i] | host_keyboard_state[i];
    if (device_keys) memcpy(device_keys, device, 256);
    return generation;
}

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
enum { MSG = 0x3000, WM_KEYDOWN = 0x100, WM_KEYUP = 0x101, WM_CHAR = 0x102, PM_REMOVE = 1 };
static uint32_t invoke(HostShim shim, unsigned count, const uint32_t *args) {
    EngineCPU cpu = {0}; cpu.gpr[4] = 0x1000; S32(0x1000, 0xDEADC0DEu);
    for (unsigned i = 0; i < count; i++) S32(0x1004 + 4*i, args[i]);
    shim(&cpu); CHECK(cpu.pc == 0xDEADC0DEu); return cpu.gpr[0];
}
/* PeekMessageA(MSG, 0, 0, 0, flags): returns message id or 0. */
static uint32_t peek(uint32_t flags) {
    uint32_t args[5] = { MSG, 0, 0, 0, flags };
    return invoke(shim_PeekMessageA, 5, args) ? G32(MSG + 4) : 0;
}
static void translate(void) { uint32_t args[1] = { MSG }; invoke(shim_TranslateMessage, 1, args); }
static void drain(void) { while (peek(PM_REMOVE)) {} }
static void device_key(uint8_t dik, int down) { merged[dik] = device[dik] = down ? 0x80 : 0; }
static void fence(void) { generation++; memset(merged, 0, sizeof merged); memset(device, 0, sizeof device); }

int main(void) {
    engine_flat_base = calloc(0x10000, 1); CHECK(engine_flat_base);
    main_window_active = 1; main_wndproc = 0x401000u;

    /* PM_NOREMOVE shows a hardware Return down; a fence before it is removed
     * means the next read returns the release, never the stale press. */
    device_key(0x1C, 1);
    CHECK(peek(0) == WM_KEYDOWN && G32(MSG + 8) == 0x0D);
    fence();
    CHECK(peek(PM_REMOVE) == WM_KEYUP && G32(MSG + 8) == 0x0D);
    CHECK(peek(PM_REMOVE) == 0);

    /* The WM_CHAR translated from a delivered hardware Escape is removed by a
     * fence that lands before it is read; the release still arrives. */
    device_key(0x01, 1);
    CHECK(peek(PM_REMOVE) == WM_KEYDOWN && G32(MSG + 8) == 0x1B); translate();
    fence();
    CHECK(peek(PM_REMOVE) == WM_KEYUP);
    CHECK(peek(PM_REMOVE) == 0);
    /* Without a fence the translated character is delivered as before. */
    device_key(0x1C, 1);
    CHECK(peek(PM_REMOVE) == WM_KEYDOWN); translate();
    CHECK(peek(PM_REMOVE) == WM_CHAR && G32(MSG + 8) == 0x0D);
    device_key(0x1C, 0);
    CHECK(peek(PM_REMOVE) == WM_KEYUP); CHECK(peek(PM_REMOVE) == 0);

    /* A fresh hardware press after the fence keeps the pending down once. */
    device_key(0x1C, 1); CHECK(peek(0) == WM_KEYDOWN);
    fence(); device_key(0x1C, 1);
    CHECK(peek(PM_REMOVE) == WM_KEYDOWN); CHECK(peek(PM_REMOVE) == 0);
    device_key(0x1C, 0); drain();

    /* Legacy physical keys and controller-derived keys are not tagged: a
     * device fence never removes them (prior behavior). */
    host_keyboard_state[0x1C] = 0x80; CHECK(peek(0) == WM_KEYDOWN);
    fence(); CHECK(peek(PM_REMOVE) == WM_KEYDOWN); host_keyboard_state[0x1C] = 0; drain();
    merged[0x01] = 0x80; /* e.g. controller Options -> Escape: merged, not device */
    CHECK(peek(0) == WM_KEYDOWN); fence(); merged[0x01] = 0x80;
    CHECK(peek(PM_REMOVE) == WM_KEYDOWN && G32(MSG + 8) == 0x1B); merged[0x01] = 0; drain();

    /* The guest MSG layout is unchanged: seven words, nothing written past it. */
    S32(MSG + 28, 0xA5A5A5A5u); device_key(0x01, 1); CHECK(peek(PM_REMOVE) == WM_KEYDOWN);
    CHECK(G32(MSG + 28) == 0xA5A5A5A5u); device_key(0x01, 0); drain();
    printf("PASS %u USER32 Return/Escape mirror checks: device-fence removal of stale downs/chars, releases kept, legacy/controller unchanged (synthetic; no device)\n", checks);
    return 0;
}
