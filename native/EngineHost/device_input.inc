/* Hardware keyboard/mouse/touch source; see device_input.h. Included once by
 * dinput8.c so DirectInput can merge it without another translation unit. */
#include "device_input.h"
#include "input_prompt.h"   /* accepted presses/motion choose the prompt source */
#include <math.h>
#include <pthread.h>

/* USB HID keyboard page (UIKeyboardHIDUsage, GCKeyCode) -> DIK scan code. */
static const uint8_t device_hid_dik[256] = {
    [0x04]=0x1E,[0x05]=0x30,[0x06]=0x2E,[0x07]=0x20,[0x08]=0x12,[0x09]=0x21,[0x0A]=0x22,[0x0B]=0x23, /* a-h */
    [0x0C]=0x17,[0x0D]=0x24,[0x0E]=0x25,[0x0F]=0x26,[0x10]=0x32,[0x11]=0x31,[0x12]=0x18,[0x13]=0x19, /* i-p */
    [0x14]=0x10,[0x15]=0x13,[0x16]=0x1F,[0x17]=0x14,[0x18]=0x16,[0x19]=0x2F,[0x1A]=0x11,[0x1B]=0x2D, /* q-x */
    [0x1C]=0x15,[0x1D]=0x2C,                                                                         /* y z */
    [0x1E]=0x02,[0x1F]=0x03,[0x20]=0x04,[0x21]=0x05,[0x22]=0x06,[0x23]=0x07,[0x24]=0x08,[0x25]=0x09, /* 1-8 */
    [0x26]=0x0A,[0x27]=0x0B,                                                                         /* 9 0 */
    [0x28]=0x1C,[0x29]=0x01,[0x2A]=0x0E,[0x2B]=0x0F,[0x2C]=0x39,               /* Return Esc Backspace Tab Space */
    [0x2D]=0x0C,[0x2E]=0x0D,[0x2F]=0x1A,[0x30]=0x1B,[0x31]=0x2B,[0x32]=0x2B,   /* - = [ ] \ non-US # */
    [0x33]=0x27,[0x34]=0x28,[0x35]=0x29,[0x36]=0x33,[0x37]=0x34,[0x38]=0x35,   /* ; ' ` , . / */
    [0x39]=0x3A,                                                               /* Caps Lock */
    [0x3A]=0x3B,[0x3B]=0x3C,[0x3C]=0x3D,[0x3D]=0x3E,[0x3E]=0x3F,[0x3F]=0x40,   /* F1-F6 */
    [0x40]=0x41,[0x41]=0x42,[0x42]=0x43,[0x43]=0x44,[0x44]=0x57,[0x45]=0x58,   /* F7-F12 */
    [0x46]=0xB7,[0x47]=0x46,[0x48]=0xC5,                                       /* PrintScreen ScrollLock Pause */
    [0x49]=0xD2,[0x4A]=0xC7,[0x4B]=0xC9,[0x4C]=0xD3,[0x4D]=0xCF,[0x4E]=0xD1,   /* Ins Home PgUp Del End PgDn */
    [0x4F]=0xCD,[0x50]=0xCB,[0x51]=0xD0,[0x52]=0xC8,                           /* Right Left Down Up */
    [0x53]=0x45,[0x54]=0xB5,[0x55]=0x37,[0x56]=0x4A,[0x57]=0x4E,[0x58]=0x9C,   /* NumLock KP/ KP* KP- KP+ KPEnter */
    [0x59]=0x4F,[0x5A]=0x50,[0x5B]=0x51,[0x5C]=0x4B,[0x5D]=0x4C,[0x5E]=0x4D,   /* KP1-KP6 */
    [0x5F]=0x47,[0x60]=0x48,[0x61]=0x49,[0x62]=0x52,[0x63]=0x53,[0x64]=0x56,   /* KP7-KP9 KP0 KP. non-US \ */
    [0x67]=0x8D,[0x68]=0x64,[0x69]=0x65,[0x6A]=0x66,                           /* KP= F13-F15 */
    [0xE0]=0x1D,[0xE1]=0x2A,[0xE2]=0x38,                                       /* LCtrl LShift LAlt */
    [0xE4]=0x9D,[0xE5]=0x36,[0xE6]=0xB8,                                       /* RCtrl RShift RAlt */
    /* 0xE3/0xE7 Command (GUI) stay unmapped: system shortcuts, never Halo. */
};
enum { DEVICE_HID_PERIOD = 0x37, DEVICE_DIK_ESCAPE = 0x01 };
uint8_t host_device_dik_for_hid(uint32_t usage) { return usage < 256u ? device_hid_dik[usage] : 0; }

/* Consumer timing (traced in the original executable, read-only): the main
 * loop (004C7610) reads DirectInput once per rendered frame (00490760:
 * keyboard via GetDeviceData only, mouse via GetDeviceState only), rebuilds
 * the action packet (0048CCA0) and overwrites the control record
 * (00471AE0 -> 00471EA0 `mov [ebp+4]`), then runs 0..N 30 Hz game ticks
 * (004C7EDA). A press shown only in frames without a tick never reaches the
 * simulation, at any frame rate above 30 Hz. Each press is therefore a
 * queued pulse: down until a tick has passed since the first read showing
 * it, then (unless the key is still held with nothing queued) up until
 * another tick passes, so consecutive presses stay distinct edges. */
typedef struct {
    uint64_t now_ns;
    uint32_t tick;       /* original game tick, when tick_valid */
    int tick_valid, menu;
    uint64_t pump;       /* input pumps seen (mouse reads); set under device_lock */
} DeviceClock;
enum { PULSE_IDLE = 0, PULSE_DOWN, PULSE_RELEASE };
typedef struct {
    uint8_t held;        /* hardware currently holds it */
    uint8_t pending;     /* presses not yet shown, bounded */
    uint8_t phase, reads;
    uint32_t tick; int tick_valid; uint64_t start_ns, start_pump;
} DevicePulse;

typedef struct {
    int game_owns, keyboard_source, pointer_source;
    uint64_t generation, accepted, rejected, fences, keyboard_reads, mouse_reads;
    uint8_t usage_dik[256];   /* the DIK each held HID usage pressed, 0 = not held */
    uint8_t key_count[256];   /* held usages per DIK */
    DevicePulse key[256], button[HOST_DEVICE_BUTTON_COUNT];
    double dx, dy;            /* pending counts, fractional remainder carried */
} DeviceInput;
static pthread_mutex_t device_lock = PTHREAD_MUTEX_INITIALIZER;
static DeviceInput device = { .generation = 1 };

static void device_fence_locked(void) {
    memset(device.usage_dik, 0, sizeof device.usage_dik);
    memset(device.key_count, 0, sizeof device.key_count);
    memset(device.key, 0, sizeof device.key);
    memset(device.button, 0, sizeof device.button);
    device.dx = device.dy = 0;
    if (++device.generation == 0) ++device.generation;
    device.fences++;
}
static int pulse_active(const DevicePulse *p) { return p->held || p->pending || p->phase != PULSE_IDLE; }
static int device_has_state_locked(void) {
    for (int i = 0; i < 256; i++) if (pulse_active(&device.key[i])) return 1;
    for (int b = 0; b < HOST_DEVICE_BUTTON_COUNT; b++) if (pulse_active(&device.button[b])) return 1;
    return device.dx != 0 || device.dy != 0;
}
/* A held press that already crossed a tick ends: its release must also
 * cross one before a quick re-press, or the two presses would merge. */
static void pulse_release_hold(DevicePulse *p) {
    p->held = 0;
    if (p->phase == PULSE_IDLE && !p->pending) { p->phase = PULSE_RELEASE; p->reads = 0; }
}
static int pulse_queue(DevicePulse *p) {
    if (p->pending >= HOST_DEVICE_PULSE_QUEUE) return 0;
    p->pending++; return 1;
}
static int pulse_satisfied(const DevicePulse *p, const DeviceClock *c) {
    if (!p->reads) return 0;
    if (c->now_ns - p->start_ns >= HOST_DEVICE_PULSE_MAX_NS) return 1;   /* stalled tick: bounded */
    if (c->menu || !c->tick_valid || !p->tick_valid)   /* menus/no clock: whole input pumps */
        return c->pump - p->start_pump >= HOST_DEVICE_MENU_PUMPS && c->now_ns - p->start_ns >= HOST_DEVICE_MENU_MIN_NS;
    return c->tick != p->tick;   /* a game tick ran with this state in its record */
}
/* One reported value. consume=0 (USER32 peek) never advances the machine. */
static int pulse_read(DevicePulse *p, const DeviceClock *c, int consume) {
    if (!consume)
        return p->phase == PULSE_DOWN || (p->phase == PULSE_IDLE && p->pending) ||
               (p->held && p->phase != PULSE_RELEASE);
    for (int guard = 0; guard < 4; guard++) {
        if (p->phase == PULSE_IDLE) {
            if (!p->pending) break;
            p->pending--; p->phase = PULSE_DOWN; p->reads = 0;
        }
        if (!pulse_satisfied(p, c)) break;
        if (p->phase == PULSE_DOWN && (!p->held || p->pending)) { p->phase = PULSE_RELEASE; p->reads = 0; break; }
        p->phase = PULSE_IDLE;   /* shown across a tick and still held, or release shown */
    }
    if (p->phase != PULSE_IDLE) {
        if (!p->reads) { p->tick = c->tick; p->tick_valid = c->tick_valid && !c->menu; p->start_ns = c->now_ns; p->start_pump = c->pump; }
        if (p->reads < UINT8_MAX) p->reads++;
    }
    return p->phase == PULSE_DOWN || (p->held && p->phase != PULSE_RELEASE);
}
/* The lease is read before taking device_lock: never nest the two locks. */
static int device_open_locked(int lease) { return device.game_owns && !lease; }
static int device_reject_locked(void) { device.rejected++; pthread_mutex_unlock(&device_lock); return 0; }
static int device_accept_locked(void) { device.accepted++; pthread_mutex_unlock(&device_lock); return 1; }

void host_device_input_set_game_owns(int owns) {
    pthread_mutex_lock(&device_lock);
    owns = owns != 0;
    if (owns != device.game_owns) { device.game_owns = owns; device_fence_locked(); }
    pthread_mutex_unlock(&device_lock);
}
void host_device_input_release(void) {
    pthread_mutex_lock(&device_lock); device_fence_locked(); pthread_mutex_unlock(&device_lock);
}
void host_device_input_select(int keyboard_source, int pointer_source) {
    pthread_mutex_lock(&device_lock);
    if (keyboard_source != device.keyboard_source || pointer_source != device.pointer_source) {
        device.keyboard_source = keyboard_source; device.pointer_source = pointer_source;
        device_fence_locked();
    }
    pthread_mutex_unlock(&device_lock);
}
int host_device_input_key(int source, uint32_t usage, int down, int system_chord) {
    int lease = hostgc_app_input_lease_active();
    pthread_mutex_lock(&device_lock);
    if (source == HOST_DEVICE_SOURCE_NONE || source != device.keyboard_source || usage >= 256u) return device_reject_locked();
    if (!down) {
        /* Release whatever this usage pressed, even if Command or ownership
         * changed meanwhile; a release with no accepted press is stale. */
        uint8_t dik = device.usage_dik[usage];
        if (!dik) return device_reject_locked();
        device.usage_dik[usage] = 0;
        if (device.key_count[dik] && --device.key_count[dik] == 0) pulse_release_hold(&device.key[dik]);
        return device_accept_locked();
    }
    uint8_t dik = system_chord ? (usage == DEVICE_HID_PERIOD ? DEVICE_DIK_ESCAPE : 0) : device_hid_dik[usage];
    if (!dik || !device_open_locked(lease) || device.usage_dik[usage]) return device_reject_locked();
    if (device.key_count[dik] == 0) {
        if (!pulse_queue(&device.key[dik])) return device_reject_locked();
        device.key[dik].held = 1;
    }
    device.usage_dik[usage] = dik; device.key_count[dik]++;
    device_accept_locked();
    host_prompt_note_device(HOST_PROMPT_DEVICE_KEY);   /* after device_lock: input_prompt.h */
    return 1;
}
int host_device_input_key_pulse(uint8_t dik) {
    int lease = hostgc_app_input_lease_active();
    pthread_mutex_lock(&device_lock);
    if (!dik || !device_open_locked(lease) || !pulse_queue(&device.key[dik])) return device_reject_locked();
    return device_accept_locked();
}
int host_device_input_button(int source, int button, int down) {
    int lease = hostgc_app_input_lease_active();
    pthread_mutex_lock(&device_lock);
    if (source == HOST_DEVICE_SOURCE_NONE || source != device.pointer_source ||
        button < 0 || button >= HOST_DEVICE_BUTTON_COUNT) return device_reject_locked();
    DevicePulse *p = &device.button[button];
    if (!down) {
        if (!p->held) return device_reject_locked();
        pulse_release_hold(p);
        return device_accept_locked();
    }
    if (!device_open_locked(lease) || p->held || !pulse_queue(p)) return device_reject_locked();
    p->held = 1;
    device_accept_locked();
    host_prompt_note_device(HOST_PROMPT_DEVICE_BUTTON);
    return 1;
}
int host_device_input_button_cancel(int source, int button) {
    pthread_mutex_lock(&device_lock);
    if (source == HOST_DEVICE_SOURCE_NONE || source != device.pointer_source ||
        button < 0 || button >= HOST_DEVICE_BUTTON_COUNT) return device_reject_locked();
    DevicePulse *p = &device.button[button];
    int was_held = p->held;
    p->held = 0; p->pending = 0;
    if (p->phase == PULSE_DOWN) { if (p->reads) { p->phase = PULSE_RELEASE; p->reads = 0; } else p->phase = PULSE_IDLE; }
    else if (p->phase == PULSE_IDLE && was_held) { p->phase = PULSE_RELEASE; p->reads = 0; }
    return device_accept_locked();
}
int host_device_input_click(int source, int button) {
    int lease = hostgc_app_input_lease_active();
    pthread_mutex_lock(&device_lock);
    if (source != HOST_DEVICE_TOUCH || button < 0 || button >= HOST_DEVICE_BUTTON_COUNT ||
        !device_open_locked(lease)) return device_reject_locked();
    /* A tap on a button the mouse already holds adds nothing: queueing it
     * would insert a release into the held press. */
    if (!device.button[button].held && !pulse_queue(&device.button[button])) return device_reject_locked();
    device_accept_locked();
    host_prompt_note_device(HOST_PROMPT_DEVICE_CLICK);
    return 1;
}
static double device_bound(double v) {
    return v > HOST_DEVICE_DELTA_LIMIT ? HOST_DEVICE_DELTA_LIMIT : v < -HOST_DEVICE_DELTA_LIMIT ? -HOST_DEVICE_DELTA_LIMIT : v;
}
int host_device_input_motion(int source, double dx, double dy) {
    int lease = hostgc_app_input_lease_active();
    pthread_mutex_lock(&device_lock);
    if ((source != HOST_DEVICE_TOUCH && (source == HOST_DEVICE_SOURCE_NONE || source != device.pointer_source)) ||
        !isfinite(dx) || !isfinite(dy) || !device_open_locked(lease)) return device_reject_locked();
    device.dx = device_bound(device.dx + dx); device.dy = device_bound(device.dy + dy);
    device_accept_locked();
    host_prompt_note_motion(dx, dy, host_monotonic_ns());
    return 1;
}
uint64_t host_device_input_generation(void) {
    pthread_mutex_lock(&device_lock); uint64_t g = device.generation; pthread_mutex_unlock(&device_lock); return g;
}
void host_device_input_stats(HostDeviceInputStats *out) {
    if (!out) return;
    pthread_mutex_lock(&device_lock);
    memset(out, 0, sizeof *out);
    out->game_owns = device.game_owns; out->keyboard_source = device.keyboard_source; out->pointer_source = device.pointer_source;
    out->generation = device.generation; out->accepted = device.accepted; out->rejected = device.rejected;
    out->fences = device.fences; out->keyboard_reads = device.keyboard_reads; out->mouse_reads = device.mouse_reads;
    for (int i = 0; i < 256; i++) out->keys_down += pulse_active(&device.key[i]);
    for (int b = 0; b < HOST_DEVICE_BUTTON_COUNT; b++) out->buttons_down += pulse_active(&device.button[b]);
    pthread_mutex_unlock(&device_lock);
}

/* DirectInput keyboard read. consume=0 peeks (USER32 Return/Escape) without
 * advancing pulses. Keys and the generation they belong to come from one
 * locked snapshot, so a fence can never relabel old keys as current. */
static uint64_t device_input_keyboard(uint8_t keys[256], const DeviceClock *clock_in, int consume) {
    int lease = hostgc_app_input_lease_active();
    memset(keys, 0, 256);
    pthread_mutex_lock(&device_lock);
    DeviceClock clock = *clock_in; clock.pump = device.mouse_reads;
    if (!device_open_locked(lease)) {
        if (device_has_state_locked()) device_fence_locked();   /* lease raced ahead of the app's own fence */
    } else {
        for (int i = 0; i < 256; i++) if (pulse_read(&device.key[i], &clock, consume)) keys[i] = 0x80;
        if (consume) device.keyboard_reads++;
    }
    uint64_t generation = device.generation;
    pthread_mutex_unlock(&device_lock);
    return generation;
}
/* DirectInput mouse GetDeviceState: takes whole counts, keeps the remainder. */
/* Halo reads the mouse once per input pump, after draining the keyboard, so
 * mouse_reads also counts pumps for the menu/no-clock fallback. */
static void device_input_mouse(int32_t *dx, int32_t *dy, uint8_t buttons[HOST_DEVICE_BUTTON_COUNT], const DeviceClock *clock_in) {
    int lease = hostgc_app_input_lease_active();
    *dx = *dy = 0; memset(buttons, 0, HOST_DEVICE_BUTTON_COUNT);
    pthread_mutex_lock(&device_lock);
    DeviceClock clock = *clock_in; clock.pump = device.mouse_reads;
    if (!device_open_locked(lease)) {
        if (device_has_state_locked()) device_fence_locked();
    } else {
        *dx = (int32_t)trunc(device.dx); *dy = (int32_t)trunc(device.dy);
        device.dx -= *dx; device.dy -= *dy;
        for (int b = 0; b < HOST_DEVICE_BUTTON_COUNT; b++)
            if (pulse_read(&device.button[b], &clock, 1)) buttons[b] = 0x80;
    }
    device.mouse_reads++;
    pthread_mutex_unlock(&device_lock);
}
