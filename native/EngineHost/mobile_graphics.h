#ifndef HALO_MOBILE_GRAPHICS_H
#define HALO_MOBILE_GRAPHICS_H
/* iPad/iPhone launch graphics: the engine back buffer and the display rate.
 *
 * Pure policy, no UIKit, no environment and no clock, so every rule here runs
 * in a host test. EngineVisionRuntime.m measures the game's window on the
 * main thread, asks this header for a plan, admits or refuses the launch
 * command line, writes -vidmode once before the engine starts, and paces the
 * engine's flat presents to the plan's refresh.
 *
 * Resolution. The engine fixes its back buffer when it creates the device,
 * so every choice applies at the next launch; nothing resizes a live back
 * buffer. Halo lays out its projection and HUD for 4:3 and the presenter
 * letterboxes, so every size is exactly 4:3: "native" is the largest 4:3
 * game viewport inside the window's pixels as the window is oriented now
 * (a landscape full-screen window, a narrower Stage Manager window, or a
 * portrait one), height a multiple of 12 (width a multiple of 16, a 64-byte
 * BGRA row). It is never a stretched or full-panel-wide picture.
 *
 * One bound admits every engine back buffer on iPad/iPhone: the native fit,
 * the presets, an explicit HALO_VIDMODE, a -vidmode already in
 * HALO_CMDLINE_EXTRA, and (d3d9.c) every CreateDevice and Reset:
 * HALO_MOBILE_MIN_* ... HALO_MOBILE_MAX_* (640 x 480 ... 2880 x 2160, at most
 * 6,220,800 pixels). It covers the largest current iPad panel (2752 x 2064)
 * at 1:1 and refuses 4K-class sizes: Halo itself only accepts a mode
 * strictly inside its 4096 x 3072 virtual desktop (shims_misc.c), the back
 * buffer and depth surface live in the 752 MiB guest page space, and nothing
 * larger has been run. The stored presets keep their meaning; an unknown
 * stored value is the old default, 1600 x 1200.
 *
 * Refresh. Halo's simulation runs 30 ticks a second whatever the frame rate.
 * Its own 30 fps limiter is the wait loop in its time update (004C9F90),
 * on while the profile's frame-rate throttle (profile +0xA6F == 2, copied to
 * 0x6894BA) or a cinematic asks for it. The request is the current display's
 * own maximum unless the player picked a lower cap, and never above a
 * positive reported maximum. Above 30 fps the iOS host runs that original
 * time update with the saved throttle lifted for the one call (frame_limit.h)
 * so gameplay can follow the request; cinematics and timedemo keep Halo's own
 * cadence, and the profile byte is restored before anything else reads it.
 * The host holds flat presents to the request (HaloMobileCadence) and the
 * presenter asks the display for the same rate. Nothing here says what rate
 * is achieved.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    HALO_MOBILE_MIN_WIDTH = 640, HALO_MOBILE_MIN_HEIGHT = 480,
    HALO_MOBILE_MAX_WIDTH = 2880, HALO_MOBILE_MAX_HEIGHT = 2160,
    /* Halo rejects a mode that is not strictly inside its virtual desktop. */
    HALO_MOBILE_DESKTOP_WIDTH = 4096, HALO_MOBILE_DESKTOP_HEIGHT = 3072,
    /* Window or display metrics past this are a bad measurement. */
    HALO_MOBILE_DISPLAY_LIMIT = 16384,
    HALO_MOBILE_DEFAULT_WIDTH = 1600, HALO_MOBILE_DEFAULT_HEIGHT = 1200,
    /* The size step of a native fit: 12 rows keep width = 4/3 height a whole
     * multiple of 16 pixels. */
    HALO_MOBILE_HEIGHT_STEP = 12,
    HALO_MOBILE_MAX_FPS = 240, HALO_MOBILE_FALLBACK_FPS = 60,
    /* Halo's own limiter rate; requests above it lift the saved throttle. */
    HALO_MOBILE_ENGINE_CAP_FPS = 30,
    /* The engine-side refresh in -vidmode. Halo's adapter reports 60 Hz modes
     * and only hands this to the D3D full-screen refresh, which the host
     * ignores; any other value is untested guest mode validation. */
    HALO_MOBILE_VIDMODE_REFRESH = 60
};
#define HALO_MOBILE_MAX_PIXELS ((uint64_t)HALO_MOBILE_MAX_WIDTH * HALO_MOBILE_MAX_HEIGHT)

/* Where the plan's size came from. */
enum {
    HALO_MOBILE_SIZE_PRESET = 0,     /* a stored preset */
    HALO_MOBILE_SIZE_NATIVE = 1,     /* fitted to the measured window */
    HALO_MOBILE_SIZE_DEFAULT = 2,    /* nothing (valid) stored */
    HALO_MOBILE_SIZE_OVERRIDE = 3,   /* explicit HALO_VIDMODE */
    HALO_MOBILE_SIZE_NATIVE_FALLBACK = 4, /* native chosen, window unknown: default size */
    HALO_MOBILE_SIZE_EXTERNAL = 5    /* an admitted -vidmode already in HALO_CMDLINE_EXTRA */
};
/* The player's refresh choice. */
enum { HALO_MOBILE_RATE_DISPLAY = 0, HALO_MOBILE_RATE_60 = 60, HALO_MOBILE_RATE_30 = 30 };

/* Plan flags. */
enum {
    HALO_MOBILE_FLAG_DISPLAY_UNKNOWN = 1u << 0,   /* no usable window size */
    HALO_MOBILE_FLAG_NATIVE_CLAMPED = 1u << 1,    /* window larger than the policy bound */
    HALO_MOBILE_FLAG_RATE_UNKNOWN = 1u << 2,      /* no usable display maximum; 60 assumed */
    HALO_MOBILE_FLAG_RATE_CLAMPED = 1u << 3,      /* display maximum above 240 */
    HALO_MOBILE_FLAG_OVERRIDE_REJECTED = 1u << 4, /* HALO_VIDMODE present but unsafe */
    HALO_MOBILE_FLAG_STORED_REJECTED = 1u << 5,   /* stored resolution not recognised */
    HALO_MOBILE_FLAG_EXTRA_REJECTED = 1u << 6,    /* HALO_CMDLINE_EXTRA -vidmode unsafe or repeated */
    HALO_MOBILE_FLAG_COMMAND_TOO_LONG = 1u << 7   /* the command line would not fit */
};

typedef struct {
    uint32_t width, height;
} HaloMobileSize;

/* Bytes the chosen size costs, steady state and at a resize peak: a
 * conservative account of the frame-sized buffers the iPad path keeps.
 *   guest_color / guest_depth   back buffer and depth surface in guest pages
 *                               (X8R8G8B8, D24S8: 4 bytes a pixel each)
 *   metal_color                 the back buffer's shared BGRA8 render target
 *   metal_depth                 its Depth32Float_Stencil8, 8 bytes a pixel bound
 *                               (Apple M4 allocates about 5.4)
 * Every Metal-allocated colour texture (render target, presenter textures,
 * drawables) is counted at 4 bytes a pixel plus 1/16 for alignment and
 * compression metadata (Apple M4 allocates about 4.2).
 *   host_frame                  the runtime's latest published frame copy
 *   presenter_cpu               the presenter's transient copy of one frame
 *   presenter_textures          five frame textures: the one being made before
 *                               a drawable is acquired, the current one, and up
 *                               to three still sampled by in-flight drawables
 *   drawables                   three BGRA8 drawables at the panel size (a
 *                               window can grow to the full panel)
 *   arenas                      the renderer's three 32 MiB transient vertex
 *                               and upload arenas (resolution independent)
 * reset_extra: a same-size estimate of a live D3D Reset (Halo's own Video
 * Setup), which stages the new guest surfaces and Metal attachments before it
 * releases the old ones, assuming the old mode is this size. It is not a
 * ceiling for every transition: coming from a larger mode, or from guest
 * storage retained after a shrink, overlaps more. reset_worst_extra bounds
 * any transition into this size with the old surfaces at the admission
 * bound (2880 x 2160 guest colour and depth, Metal colour and depth).
 * These are estimates of frame-sized buffers; allocation bounds are guards,
 * not proof of device memory headroom, and the measured live page and
 * render-target bytes are reported separately at run time.
 * Not included, because they do not scale with this choice or are measured
 * at run time instead: Halo's own extra render targets (each with its own
 * colour and depth attachments; the runtime reports their measured bytes),
 * texture and geometry caches, shader pipelines, and the system compositor. */
typedef struct {
    uint64_t guest_color, guest_depth, metal_color, metal_depth;
    uint64_t host_frame, presenter_cpu, presenter_textures, drawables, arenas;
    uint64_t steady_total, reset_extra, peak_total;          /* same-size Reset overlap */
    uint64_t reset_worst_extra, peak_worst_total;            /* any transition, old mode at the bound */
    uint64_t guest_total; /* guest page space only */
} HaloMobileMemory;
enum { HALO_MOBILE_PRESENTER_TEXTURES = 5, HALO_MOBILE_DRAWABLES = 3, HALO_MOBILE_ARENAS = 3 };
#define HALO_MOBILE_ARENA_BYTES (32ull * 1024ull * 1024ull)

typedef struct {
    /* Inputs as measured; 0 when unknown. The window is as oriented now;
     * the panel is the whole display, long edge first (metadata, drawables). */
    uint32_t view_width, view_height;
    uint32_t display_width, display_height;
    int32_t display_max_fps;
    /* Choices. */
    uint32_t size_source;            /* HALO_MOBILE_SIZE_* */
    int32_t rate_choice;             /* HALO_MOBILE_RATE_* */
    /* Results. */
    HaloMobileSize render;           /* the engine back buffer to request */
    HaloMobileSize native;           /* the native fit, 0 x 0 when unknown */
    int32_t refresh;                 /* presenter and producer rate request */
    uint32_t flags;                  /* HALO_MOBILE_FLAG_* */
    HaloMobileMemory memory;
} HaloMobilePlan;

/* The one admission bound for every iPad/iPhone engine back buffer. */
static inline int halo_mobile_size_allowed(uint32_t w, uint32_t h) {
    return w >= HALO_MOBILE_MIN_WIDTH && h >= HALO_MOBILE_MIN_HEIGHT &&
           w <= HALO_MOBILE_MAX_WIDTH && h <= HALO_MOBILE_MAX_HEIGHT &&
           (uint64_t)w * h <= HALO_MOBILE_MAX_PIXELS &&
           w < HALO_MOBILE_DESKTOP_WIDTH && h < HALO_MOBILE_DESKTOP_HEIGHT;
}

/* The largest exact 4:3 game viewport inside a window of w x h pixels as it
 * is oriented (no sorting: a 900 x 1200 portrait window fits 896 x 672,
 * never 1200 x 900), within the policy bound. Returns 0 (and 0 x 0) when
 * the window cannot hold the minimum or the numbers are not a window. */
static inline int halo_mobile_native_fit(int64_t w, int64_t h, HaloMobileSize *out, uint32_t *flags) {
    if (out) out->width = out->height = 0;
    if (!out || w <= 0 || h <= 0 || w > HALO_MOBILE_DISPLAY_LIMIT || h > HALO_MOBILE_DISPLAY_LIMIT) return 0;
    uint64_t width_limited = (uint64_t)w * 3u / 4u;
    uint64_t height = (uint64_t)h < width_limited ? (uint64_t)h : width_limited;
    if (height > HALO_MOBILE_MAX_HEIGHT) { height = HALO_MOBILE_MAX_HEIGHT; if (flags) *flags |= HALO_MOBILE_FLAG_NATIVE_CLAMPED; }
    height -= height % HALO_MOBILE_HEIGHT_STEP;
    uint64_t width = height / 3u * 4u;
    if (!halo_mobile_size_allowed((uint32_t)width, (uint32_t)height)) return 0;
    out->width = (uint32_t)width; out->height = (uint32_t)height;
    return 1;
}

/* The stored launch presets, unchanged from the earlier picker. */
static const HaloMobileSize halo_mobile_presets[] = {
    { 1280, 960 }, { 1600, 1200 }, { 1920, 1440 }, { 2048, 1536 }
};
enum { HALO_MOBILE_PRESET_COUNT = (int)(sizeof halo_mobile_presets / sizeof halo_mobile_presets[0]) };

/* Strict unsigned decimal: digits only, at most five, no sign or spaces. */
static inline const char *halo_mobile_parse_u32(const char *p, uint32_t *out) {
    uint32_t value = 0; int digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 5) return NULL;
        value = value * 10u + (uint32_t)(*p++ - '0');
    }
    if (!digits) return NULL;
    *out = value; return p;
}

/* A stored resolution: "native", or one of the presets as "WxH". Returns 1
 * for a preset (size set), 2 for native, 0 for anything else. */
static inline int halo_mobile_parse_stored(const char *text, HaloMobileSize *size) {
    if (!text || !size) return 0;
    if (!strcmp(text, "native")) return 2;
    uint32_t w = 0, h = 0; const char *p = halo_mobile_parse_u32(text, &w);
    if (!p || *p != 'x') return 0;
    p = halo_mobile_parse_u32(p + 1, &h);
    if (!p || *p) return 0;
    for (int i = 0; i < HALO_MOBILE_PRESET_COUNT; i++)
        if (halo_mobile_presets[i].width == w && halo_mobile_presets[i].height == h) { *size = halo_mobile_presets[i]; return 1; }
    return 0;
}

/* A mode "W,H" or "W,H,R" (R 1...1000), as HALO_VIDMODE or after -vidmode.
 * Any aspect, but only inside the one admission bound; R is kept as typed. */
static inline int halo_mobile_parse_override(const char *text, HaloMobileSize *size) {
    if (!text || !size) return 0;
    uint32_t w = 0, h = 0, r = 0; const char *p = halo_mobile_parse_u32(text, &w);
    if (!p || *p != ',') return 0;
    p = halo_mobile_parse_u32(p + 1, &h);
    if (!p) return 0;
    if (*p == ',') { p = halo_mobile_parse_u32(p + 1, &r); if (!p || !r || r > 1000) return 0; }
    if (*p || !halo_mobile_size_allowed(w, h)) return 0;
    size->width = w; size->height = h; return 1;
}

/* A -vidmode in launch command-line extras. The engine receives the whole
 * command line from GetCommandLineA and splits it itself (CRT rules: spaces
 * and tabs separate, double quotes group and backslashes escape them), so
 * this helper only admits what it can read unambiguously:
 *   UNSAFE_SYNTAX  a double quote or a control character anywhere: quoting
 *                  could hide a -vidmode, or swallow one appended after it.
 *   REJECTED       "-vidmode" (any case) appearing other than as exactly one
 *                  standalone token followed by an admitted mode: repeated,
 *                  embedded ("x-vidmode", "--vidmode", "-vidmode=..."),
 *                  malformed, missing or outside the bound.
 *   NONE           no "-vidmode" anywhere.
 *   ADMITTED       exactly one "-vidmode MODE" with an admitted MODE (size set). */
enum { HALO_MOBILE_EXTRA_UNSAFE_SYNTAX = -2, HALO_MOBILE_EXTRA_REJECTED = -1, HALO_MOBILE_EXTRA_NONE = 0, HALO_MOBILE_EXTRA_ADMITTED = 1 };
static inline int halo_mobile_extra_vidmode(const char *extra, HaloMobileSize *size) {
    if (!extra) return HALO_MOBILE_EXTRA_NONE;
    unsigned mentions = 0;
    for (const char *c = extra; *c; c++) {
        unsigned char ch = (unsigned char)*c;
        if (ch == '"' || (ch < 0x20 && ch != '\t') || ch == 0x7f) return HALO_MOBILE_EXTRA_UNSAFE_SYNTAX;
        if (!strncasecmp(c, "-vidmode", 8)) mentions++;
    }
    if (mentions > 1) return HALO_MOBILE_EXTRA_REJECTED;
    int found = 0, ok = 0; HaloMobileSize parsed = { 0, 0 };
    const char *p = extra;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *token = p; size_t n = 0;
        while (token[n] && token[n] != ' ' && token[n] != '\t') n++;
        p = token + n;
        if (n < 8 || strncasecmp(token, "-vidmode", 8)) continue;
        if (++found > 1 || n != 8) return HALO_MOBILE_EXTRA_REJECTED;
        while (*p == ' ' || *p == '\t') p++;
        char mode[32]; size_t m = 0;
        while (p[m] && p[m] != ' ' && p[m] != '\t') m++;
        if (!m || m >= sizeof mode) return HALO_MOBILE_EXTRA_REJECTED;
        memcpy(mode, p, m); mode[m] = 0; p += m;
        ok = halo_mobile_parse_override(mode, &parsed);
        if (!ok) return HALO_MOBILE_EXTRA_REJECTED;
    }
    if (found != (int)mentions) return HALO_MOBILE_EXTRA_REJECTED;   /* embedded in another token */
    if (!found) return HALO_MOBILE_EXTRA_NONE;
    if (size) *size = parsed;
    return HALO_MOBILE_EXTRA_ADMITTED;
}

/* A stored refresh choice: "display", "60" or "30"; anything else is display. */
static inline int32_t halo_mobile_parse_rate(const char *text) {
    if (text && !strcmp(text, "60")) return HALO_MOBILE_RATE_60;
    if (text && !strcmp(text, "30")) return HALO_MOBILE_RATE_30;
    return HALO_MOBILE_RATE_DISPLAY;
}

/* The rate to request: the display maximum, or the player's lower cap, never
 * above a positive reported maximum (a 24 Hz display requests 24). Unknown
 * (0, negative, above 1000) assumes 60; a maximum above 240 is held at 240. */
static inline int32_t halo_mobile_refresh(int32_t display_max, int32_t choice, uint32_t *flags) {
    int32_t display = display_max;
    if (display <= 0 || display > 1000) { display = HALO_MOBILE_FALLBACK_FPS; if (flags) *flags |= HALO_MOBILE_FLAG_RATE_UNKNOWN; }
    else if (display > HALO_MOBILE_MAX_FPS) { display = HALO_MOBILE_MAX_FPS; if (flags) *flags |= HALO_MOBILE_FLAG_RATE_CLAMPED; }
    if (choice == HALO_MOBILE_RATE_60 || choice == HALO_MOBILE_RATE_30) return choice < display ? choice : display;
    return display;
}

/* Whether a rate request needs Halo's saved 30 fps throttle lifted. */
static inline int halo_mobile_lifts_throttle(int32_t refresh) { return refresh > HALO_MOBILE_ENGINE_CAP_FPS; }

/* The presenter's (and producer's) rate on whatever screen the view is on
 * now: the launch request, never above that screen's positive maximum. */
static inline int32_t halo_mobile_presenter_fps(int32_t request, int32_t screen_max) {
    if (request <= 0) request = HALO_MOBILE_FALLBACK_FPS;
    if (screen_max > 0 && screen_max <= 1000 && screen_max < request) request = screen_max;
    if (request < 1) request = 1;
    return request;
}

static inline void halo_mobile_memory(uint32_t w, uint32_t h, uint32_t display_w, uint32_t display_h, HaloMobileMemory *m) {
    memset(m, 0, sizeof *m);
    if (w > HALO_MOBILE_DISPLAY_LIMIT || h > HALO_MOBILE_DISPLAY_LIMIT) return;
    if (display_w > HALO_MOBILE_DISPLAY_LIMIT || display_h > HALO_MOBILE_DISPLAY_LIMIT) display_w = display_h = 0;
    uint64_t frame = (uint64_t)w * h * 4u, gpu_frame = frame + frame / 16u;
    uint64_t drawable = (uint64_t)display_w * display_h * 4u;
    m->guest_color = frame; m->guest_depth = frame;
    m->metal_color = gpu_frame; m->metal_depth = (uint64_t)w * h * 8u;
    m->host_frame = frame; m->presenter_cpu = frame;
    m->presenter_textures = HALO_MOBILE_PRESENTER_TEXTURES * gpu_frame;
    m->drawables = HALO_MOBILE_DRAWABLES * (drawable + drawable / 16u);
    m->arenas = frame ? HALO_MOBILE_ARENAS * HALO_MOBILE_ARENA_BYTES : 0;
    m->guest_total = m->guest_color + m->guest_depth;
    m->steady_total = m->guest_total + m->metal_color + m->metal_depth + m->host_frame +
                      m->presenter_cpu + m->presenter_textures + m->drawables + m->arenas;
    m->reset_extra = m->guest_total + m->metal_color + m->metal_depth;
    m->peak_total = m->steady_total + m->reset_extra;
    if (frame) {
        uint64_t old_frame = HALO_MOBILE_MAX_PIXELS * 4u;
        m->reset_worst_extra = 2u * old_frame + (old_frame + old_frame / 16u) + HALO_MOBILE_MAX_PIXELS * 8u;
        if (m->reset_worst_extra < m->reset_extra) m->reset_worst_extra = m->reset_extra;
    }
    m->peak_worst_total = m->steady_total + m->reset_worst_extra;
}

/* The whole launch decision from the stored choices and the measurements.
 * view_w/h: the window's pixels as oriented now; display_w/h: the panel in
 * either orientation (drawables, metadata), 0 to use the window. */
static inline void halo_mobile_plan_view(const char *stored, const char *rate, const char *override,
                                         int64_t view_w, int64_t view_h, int64_t display_w, int64_t display_h,
                                         int32_t display_max_fps, HaloMobilePlan *plan) {
    memset(plan, 0, sizeof *plan);
    if (halo_mobile_native_fit(view_w, view_h, &plan->native, &plan->flags)) {
        plan->view_width = (uint32_t)view_w; plan->view_height = (uint32_t)view_h;
    } else plan->flags |= HALO_MOBILE_FLAG_DISPLAY_UNKNOWN;
    if (display_w <= 0 || display_h <= 0 || display_w > HALO_MOBILE_DISPLAY_LIMIT || display_h > HALO_MOBILE_DISPLAY_LIMIT) {
        display_w = plan->view_width; display_h = plan->view_height;
    }
    plan->display_width = (uint32_t)(display_w > display_h ? display_w : display_h);
    plan->display_height = (uint32_t)(display_w > display_h ? display_h : display_w);
    plan->display_max_fps = display_max_fps;
    HaloMobileSize size = { HALO_MOBILE_DEFAULT_WIDTH, HALO_MOBILE_DEFAULT_HEIGHT };
    plan->size_source = HALO_MOBILE_SIZE_DEFAULT;
    int kind = halo_mobile_parse_stored(stored, &size);
    if (kind == 1) plan->size_source = HALO_MOBILE_SIZE_PRESET;
    else if (kind == 2) {
        if (plan->native.width) { size = plan->native; plan->size_source = HALO_MOBILE_SIZE_NATIVE; }
        else plan->size_source = HALO_MOBILE_SIZE_NATIVE_FALLBACK;
    } else if (stored && stored[0]) plan->flags |= HALO_MOBILE_FLAG_STORED_REJECTED;
    if (override && override[0]) {
        HaloMobileSize explicit_size;
        if (halo_mobile_parse_override(override, &explicit_size)) { size = explicit_size; plan->size_source = HALO_MOBILE_SIZE_OVERRIDE; }
        else plan->flags |= HALO_MOBILE_FLAG_OVERRIDE_REJECTED;
    }
    plan->render = size;
    plan->rate_choice = halo_mobile_parse_rate(rate);
    plan->refresh = halo_mobile_refresh(display_max_fps, plan->rate_choice, &plan->flags);
    halo_mobile_memory(size.width, size.height, plan->display_width, plan->display_height, &plan->memory);
}
/* The window and the panel are the same measurement. */
static inline void halo_mobile_plan(const char *stored, const char *rate, const char *override,
                                    int64_t display_w, int64_t display_h, int32_t display_max_fps,
                                    HaloMobilePlan *plan) {
    halo_mobile_plan_view(stored, rate, override, display_w, display_h, display_w, display_h, display_max_fps, plan);
}

/* "-vidmode W,H,60" for the plan, or the override as typed. 0 if it does not fit. */
static inline int halo_mobile_vidmode(const HaloMobilePlan *plan, const char *override, char *out, size_t capacity) {
    if (!plan || !out || !capacity) return 0;
    int n = plan->size_source == HALO_MOBILE_SIZE_OVERRIDE && override
        ? snprintf(out, capacity, "-vidmode %s", override)
        : snprintf(out, capacity, "-vidmode %u,%u,%d", plan->render.width, plan->render.height, HALO_MOBILE_VIDMODE_REFRESH);
    return n > 0 && (size_t)n < capacity;
}

/* ---- Producer cadence -------------------------------------------------------
 * Holds the engine thread's flat presents to at most one per display period
 * at the requested rate. It only ever adds a wait when the engine is ahead of
 * that rate, so Halo's own 30 fps limiter (when on) and any slower frame are
 * untouched: game time keeps coming from Halo's clock, and a late frame is
 * never made up by shortening the next one. A frame more than a period late
 * (a slow producer or a stall) restarts the grid instead of letting frames
 * run back to back. */
typedef struct {
    uint64_t period_ns;      /* 0: off */
    uint64_t next_ns;        /* the earliest release of the next frame; 0 none */
    uint64_t frames, waits, wait_ns, late, restarts;
} HaloMobileCadence;

static inline void halo_mobile_cadence_set(HaloMobileCadence *c, int32_t fps) {
    uint64_t period = fps > 0 && fps <= 1000 ? 1000000000ull / (uint64_t)fps : 0;
    if (period != c->period_ns) { c->period_ns = period; c->next_ns = 0; }
}

/* One flat present at now_ns. Returns when to release it (now_ns when no
 * wait), and advances the grid. */
static inline uint64_t halo_mobile_cadence_release(HaloMobileCadence *c, uint64_t now_ns) {
    c->frames++;
    if (!c->period_ns) return now_ns;
    if (!c->next_ns || now_ns >= c->next_ns + c->period_ns || c->next_ns > now_ns + c->period_ns) {
        /* First frame, a frame more than a period late, or a clock that moved. */
        if (c->next_ns) c->restarts++;
        c->next_ns = now_ns + c->period_ns;
        return now_ns;
    }
    if (now_ns < c->next_ns) {
        uint64_t release = c->next_ns;
        c->waits++; c->wait_ns += release - now_ns;
        c->next_ns += c->period_ns;
        return release;
    }
    /* Late by less than a period: go now, keep the grid. */
    c->late++;
    c->next_ns += c->period_ns;
    return now_ns;
}

#ifdef __cplusplus
}
#endif
#endif
