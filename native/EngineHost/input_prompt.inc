/* Active input source for native prompt labels; see input_prompt.h. Included
 * once by dinput8.c, before device_input.inc, so both input routes can report
 * accepted input without another translation unit. */
#include "input_prompt.h"
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>

enum { PROMPT_LEFT_STICK = 0, PROMPT_RIGHT_STICK, PROMPT_LEFT_TRIGGER, PROMPT_RIGHT_TRIGGER, PROMPT_AXES };
typedef struct {
    int have;                         /* a baseline reading exists */
    uint64_t sequence, generation;
    bool buttons[HOSTGC_BUTTON_COUNT];
    bool out[PROMPT_AXES];            /* stick deflected / trigger pulled, with hysteresis */
    double motion; uint64_t motion_ns;   /* counts since motion_ns, the start of a fixed window */
} PromptEdges;
static pthread_mutex_t prompt_lock = PTHREAD_MUTEX_INITIALIZER;
static PromptEdges prompt_edges;
static _Atomic int prompt_mode = HOST_PROMPT_MODE_CONTROLLER;
static _Atomic int prompt_connected, prompt_spatial;
static _Atomic uint64_t prompt_generation, prompt_controller_activity, prompt_device_activity;

static bool prompt_axis_out(float x, float y, bool was, float on, float off) {
    if (!isfinite(x) || !isfinite(y)) return false;
    float magnitude = hypotf(x, y);
    return was ? magnitude > off : magnitude >= on;
}
void host_prompt_note_controller(const HostGCSnapshot *pad) {
    if (!pad || !pad->connected) {
        atomic_store(&prompt_connected, 0);
        pthread_mutex_lock(&prompt_lock); prompt_edges.have = 0; pthread_mutex_unlock(&prompt_lock);
        return;
    }
    atomic_store(&prompt_spatial, pad->spatial ? 1 : 0);
    atomic_store(&prompt_generation, pad->source_generation);
    atomic_store(&prompt_connected, 1);
    pthread_mutex_lock(&prompt_lock);
    PromptEdges *e = &prompt_edges;
    if (e->have && e->sequence == pad->sequence && e->generation == pad->source_generation) {
        pthread_mutex_unlock(&prompt_lock); return;   /* the same reading handed out again */
    }
    bool baseline = !e->have || e->generation != pad->source_generation, activity = false;
    float lx = pad->spatial ? pad->spatial_raw_lx : pad->lx, ly = pad->spatial ? pad->spatial_raw_ly : pad->ly;
    bool out[PROMPT_AXES] = {
        prompt_axis_out(lx, ly, !baseline && e->out[PROMPT_LEFT_STICK], .5f, .3f),
        prompt_axis_out(pad->rx, pad->ry, !baseline && e->out[PROMPT_RIGHT_STICK], .5f, .3f),
        prompt_axis_out(pad->lt, 0, !baseline && e->out[PROMPT_LEFT_TRIGGER], .5f, .2f),
        prompt_axis_out(pad->rt, 0, !baseline && e->out[PROMPT_RIGHT_TRIGGER], .5f, .2f) };
    for (int i = 0; i < HOSTGC_BUTTON_COUNT; i++) {
        bool down = pad->buttons[i] && i != HOSTGC_BTN_HOME;
        if (!baseline && down && !e->buttons[i]) activity = true;
        e->buttons[i] = down;
    }
    for (int i = 0; i < PROMPT_AXES; i++) {
        if (!baseline && out[i] && !e->out[i]) activity = true;
        e->out[i] = out[i];
    }
    e->have = 1; e->sequence = pad->sequence; e->generation = pad->source_generation;
    pthread_mutex_unlock(&prompt_lock);
    if (activity) {
        atomic_store(&prompt_mode, HOST_PROMPT_MODE_CONTROLLER);
        atomic_fetch_add(&prompt_controller_activity, 1);
    }
}
void host_prompt_note_controller_blocked(void) {
    pthread_mutex_lock(&prompt_lock); prompt_edges.have = 0; pthread_mutex_unlock(&prompt_lock);
}
static void prompt_device_active(void) {
    atomic_store(&prompt_mode, HOST_PROMPT_MODE_DEVICE);
    atomic_fetch_add(&prompt_device_activity, 1);
}
void host_prompt_note_device(int kind) {
    if (kind == HOST_PROMPT_DEVICE_KEY || kind == HOST_PROMPT_DEVICE_BUTTON || kind == HOST_PROMPT_DEVICE_CLICK)
        prompt_device_active();
}
void host_prompt_note_motion(double dx, double dy, uint64_t now_ns) {
    if (!isfinite(dx) || !isfinite(dy)) return;
    pthread_mutex_lock(&prompt_lock);
    PromptEdges *e = &prompt_edges;
    /* A window starts with the first sample after the previous one ended
     * (or after the clock went backwards) and never slides, so continuous
     * jitter can add at most one window's worth before it is discarded. */
    if (!e->motion_ns || now_ns < e->motion_ns || now_ns - e->motion_ns >= HOST_PROMPT_MOTION_WINDOW_NS) {
        e->motion = 0; e->motion_ns = now_ns ? now_ns : 1;
    }
    e->motion += fabs(dx) + fabs(dy);
    bool active = e->motion >= HOST_PROMPT_MOTION_COUNTS;
    if (active) { e->motion = 0; e->motion_ns = 0; }
    pthread_mutex_unlock(&prompt_lock);
    if (active) prompt_device_active();
}
int host_prompt_controller_active(int *spatial, uint64_t *source_generation) {
    int active = atomic_load(&prompt_mode) == HOST_PROMPT_MODE_CONTROLLER && atomic_load(&prompt_connected);
    if (spatial) *spatial = atomic_load(&prompt_spatial);
    if (source_generation) *source_generation = atomic_load(&prompt_generation);
    return active;
}
void host_prompt_stats(HostPromptStats *out) {
    if (!out) return;
    out->mode = atomic_load(&prompt_mode); out->connected = atomic_load(&prompt_connected);
    out->spatial = atomic_load(&prompt_spatial); out->source_generation = atomic_load(&prompt_generation);
    out->controller_activity = atomic_load(&prompt_controller_activity);
    out->device_activity = atomic_load(&prompt_device_activity);
}
