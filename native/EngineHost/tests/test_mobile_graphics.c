/* iPad/iPhone launch-graphics policy (mobile_graphics.h): native aspect fit,
 * stored and explicit sizes, refresh clamping, memory estimate and the
 * producer cadence. Pure; no display, device or engine. */
#include "../mobile_graphics.h"
#include <assert.h>
#include <stdio.h>

static HaloMobileSize fit(int64_t w, int64_t h, int *ok, uint32_t *flags) {
    HaloMobileSize s; *flags = 0; *ok = halo_mobile_native_fit(w, h, &s, flags); return s;
}

static void native_fits(void) {
    int ok; uint32_t flags; HaloMobileSize s;
    /* 13-inch iPad Pro (M4/M5) full-screen landscape window: 1:1. */
    s = fit(2752, 2064, &ok, &flags); assert(ok && s.width == 2752 && s.height == 2064 && !flags);
    /* A portrait window retains its aspect, uniformly reduced to the bound. */
    s = fit(2064, 2752, &ok, &flags); assert(ok && s.width == 1616 && s.height == 2155);
    s = fit(900, 1200, &ok, &flags); assert(ok && s.width == 896 && s.height == 1195);
    s = fit(1200, 900, &ok, &flags); assert(ok && s.width == 1200 && s.height == 900);
    /* Full width on the 11-inch iPad and iPhone; only pixel alignment reduces
     * the measured viewport, never a forced 4:3 crop or stretched frame. */
    s = fit(2420, 1668, &ok, &flags); assert(ok && s.width == 2416 && s.height == 1665);
    s = fit(2732, 2048, &ok, &flags); assert(ok && s.width == 2720 && s.height == 2039);
    s = fit(1366, 1024, &ok, &flags); assert(ok && s.width == 1360 && s.height == 1020);
    s = fit(2868, 1320, &ok, &flags); assert(ok && s.width == 2864 && s.height == 1318);
    /* Sweep both orientations, phone-wide windows, and memory-bound cases:
     * the render aspect differs from the target by at most half a pixel. */
    for (int64_t w = 600; w <= 6000; w += 37) for (int64_t h = 400; h <= 4000; h += 41) {
        s = fit(w, h, &ok, &flags);
        if (!ok) { assert(s.width == 0 && s.height == 0); continue; }
        int64_t aspect_error = (int64_t)s.height * w - (int64_t)s.width * h;
        assert(aspect_error * 2 <= w && aspect_error * 2 >= -w);
        assert(s.width % 16 == 0 && ((uint64_t)s.width * 4u) % 64 == 0);
        assert(s.width <= (uint64_t)w && s.height <= (uint64_t)h);
        assert(halo_mobile_size_allowed(s.width, s.height));
    }
    s = fit(5120, 3840, &ok, &flags); assert(ok && s.width == 2880 && s.height == 2160 && (flags & HALO_MOBILE_FLAG_NATIVE_CLAMPED));
    s = fit(16384, 16384, &ok, &flags); assert(ok && s.width == 2160 && s.height == 2160);
    /* Not a window: zero, negative, absurd, overflow-sized or too small. */
    const int64_t bad[][2] = { {0, 0}, {-2752, 2064}, {2752, -1}, {16385, 2064}, {2752, INT64_MAX},
                               {INT64_MIN, 5}, {639, 2000}, {2000, 479}, {640, 470}, {1, 1} };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) { s = fit(bad[i][0], bad[i][1], &ok, &flags); assert(!ok && !s.width && !s.height); }
    s = fit(640, 480, &ok, &flags); assert(ok && s.width == 640 && s.height == 480);
    /* The one bound: pixel count and both edges. */
    assert(halo_mobile_size_allowed(2880, 2160) && !halo_mobile_size_allowed(2881, 2160) && !halo_mobile_size_allowed(2880, 2161));
    assert(!halo_mobile_size_allowed(4095, 3071) && !halo_mobile_size_allowed(UINT32_MAX, UINT32_MAX) && !halo_mobile_size_allowed(639, 480));
}

static void parsing(void) {
    HaloMobileSize s = {0, 0};
    assert(halo_mobile_parse_stored("native", &s) == 2);
    assert(halo_mobile_parse_stored("1600x1200", &s) == 1 && s.width == 1600 && s.height == 1200);
    assert(halo_mobile_parse_stored("2048x1536", &s) == 1 && s.width == 2048);
    /* The earlier picker's values only; anything else is rejected. */
    const char *rejected[] = { "", "Native", "native ", "2752x2064", "1600X1200", "1600x1200 ", " 1600x1200",
                               "+1600x1200", "1600x-1200", "1600x", "x1200", "1600*1200", "99999999999x1",
                               "4294967297x1200", "1600x1200x1", "2560x1920", "0x0" };
    for (unsigned i = 0; i < sizeof rejected / sizeof rejected[0]; i++) assert(halo_mobile_parse_stored(rejected[i], &s) == 0);
    assert(halo_mobile_parse_stored(NULL, &s) == 0);
    /* Explicit modes: inside the one bound only. */
    assert(halo_mobile_parse_override("2752,2064,60", &s) && s.width == 2752 && s.height == 2064);
    assert(halo_mobile_parse_override("1024,768", &s) && s.width == 1024);
    const char *unsafe[] = { "4096,3072,60", "4095,3071,60", "8192,6144", "3840,2160,60", "639,480", "640,479", "1600,1200,0",
                             "1600,1200,1001", "1600,1200,", "1600;1200", "-1600,1200", "1600,1200,60,1",
                             "99999,1200", "1600 ,1200", "9000,9000,60", "" };
    for (unsigned i = 0; i < sizeof unsafe / sizeof unsafe[0]; i++) assert(!halo_mobile_parse_override(unsafe[i], &s));
    assert(!halo_mobile_parse_override(NULL, &s));
    assert(halo_mobile_parse_rate("60") == 60 && halo_mobile_parse_rate("30") == 30);
    assert(halo_mobile_parse_rate("display") == HALO_MOBILE_RATE_DISPLAY && halo_mobile_parse_rate(NULL) == HALO_MOBILE_RATE_DISPLAY);
    assert(halo_mobile_parse_rate("120") == HALO_MOBILE_RATE_DISPLAY && halo_mobile_parse_rate("6") == HALO_MOBILE_RATE_DISPLAY);
}

static void extras(void) {
    HaloMobileSize s = {0, 0};
    assert(halo_mobile_extra_vidmode(NULL, &s) == HALO_MOBILE_EXTRA_NONE);
    assert(halo_mobile_extra_vidmode("", &s) == HALO_MOBILE_EXTRA_NONE);
    assert(halo_mobile_extra_vidmode("-console -novideo", &s) == HALO_MOBILE_EXTRA_NONE);
    assert(halo_mobile_extra_vidmode("-vidmode 1600,1200,60", &s) == HALO_MOBILE_EXTRA_ADMITTED && s.width == 1600 && s.height == 1200);
    assert(halo_mobile_extra_vidmode("  -console\t-vidmode   2752,2064,60  ", &s) == HALO_MOBILE_EXTRA_ADMITTED && s.width == 2752);
    /* Coordinator's repro and every other way a mode can be unsafe, hidden or ambiguous. */
    const char *refused[] = { "-console -vidmode 9000,9000,60", "-vidmode 4095,3071,60", "-vidmode 3840,2880",
                              "-vidmode", "-vidmode ", "-console -vidmode", "-vidmode 1600x1200", "-vidmode=1600,1200,60",
                              "-VIDMODE 9000,9000,60", "-VidMode 4096,3072", "-vidmode 1600,1200,60 -vidmode 1600,1200,60",
                              "-vidmode 1600,1200 -vidmode 9000,9000", "-vidmodex 1600,1200", "-vidmode 1600,1200,60,1",
                              "-vidmode 1600,1200,0", "-vidmode 123456789012345678901234567890123,1" };
    for (unsigned i = 0; i < sizeof refused / sizeof refused[0]; i++) assert(halo_mobile_extra_vidmode(refused[i], &s) == HALO_MOBILE_EXTRA_REJECTED);
    assert(halo_mobile_extra_vidmode("-VIDMODE 1600,1200", &s) == HALO_MOBILE_EXTRA_ADMITTED && s.width == 1600);
    assert(halo_mobile_extra_vidmode("-novidmode", &s) == HALO_MOBILE_EXTRA_NONE);
    /* -vidmode embedded in another token is never silently ignored. */
    const char *embedded[] = { "x-vidmode 9000,9000,60", "--vidmode 1600,1200", "-console/-vidmode 1600,1200", "a-VIDMODE 4096,3072" };
    for (unsigned i = 0; i < sizeof embedded / sizeof embedded[0]; i++) assert(halo_mobile_extra_vidmode(embedded[i], &s) == HALO_MOBILE_EXTRA_REJECTED);
    /* Quotes and control characters: CRT quoting could hide a -vidmode
     * ("...") or swallow one appended after an unbalanced quote. */
    const char *syntax[] = { "-console \"-vidmode 9000,9000,60\"", "\"-vidmode\" 1600,1200", "-path \"C:\\Halo maps",
                             "-vidmode 1600,1200,60 \"", "-console\x01", "-console\n-vidmode 9000,9000", "-x\x7f", "\"\"" };
    for (unsigned i = 0; i < sizeof syntax / sizeof syntax[0]; i++) assert(halo_mobile_extra_vidmode(syntax[i], &s) == HALO_MOBILE_EXTRA_UNSAFE_SYNTAX);
    assert(halo_mobile_extra_vidmode("-console\t-novideo", &s) == HALO_MOBILE_EXTRA_NONE);   /* tab is a CRT separator */
}

static void refresh(void) {
    uint32_t f = 0;
    /* The display's own maximum, whatever it is: no 60 or 120 assumed. */
    assert(halo_mobile_refresh(120, HALO_MOBILE_RATE_DISPLAY, &f) == 120 && !f);
    assert(halo_mobile_refresh(144, HALO_MOBILE_RATE_DISPLAY, &f) == 144 && !f);
    assert(halo_mobile_refresh(90, HALO_MOBILE_RATE_DISPLAY, &f) == 90);
    assert(halo_mobile_refresh(60, HALO_MOBILE_RATE_DISPLAY, &f) == 60);
    /* A cap never exceeds the display. */
    assert(halo_mobile_refresh(120, HALO_MOBILE_RATE_60, &f) == 60);
    assert(halo_mobile_refresh(120, HALO_MOBILE_RATE_30, &f) == 30);
    assert(halo_mobile_refresh(48, HALO_MOBILE_RATE_60, &f) == 48);
    /* Low positive maxima are never raised (coordinator's 24 Hz repro). */
    for (int32_t max = 1; max <= 240; max++) {
        f = 0;
        for (int32_t choice = 0; choice <= 60; choice += 30) {
            int32_t rate = halo_mobile_refresh(max, choice, &f);
            assert(rate > 0 && rate <= max && !(f & HALO_MOBILE_FLAG_RATE_UNKNOWN));
            if (choice == HALO_MOBILE_RATE_DISPLAY) assert(rate == max);
        }
    }
    f = 0; assert(halo_mobile_refresh(24, HALO_MOBILE_RATE_DISPLAY, &f) == 24 && !f);
    assert(halo_mobile_refresh(24, HALO_MOBILE_RATE_30, NULL) == 24 && halo_mobile_refresh(1, HALO_MOBILE_RATE_60, NULL) == 1);
    /* Unknown and absurd maxima. */
    f = 0; assert(halo_mobile_refresh(0, HALO_MOBILE_RATE_DISPLAY, &f) == 60 && (f & HALO_MOBILE_FLAG_RATE_UNKNOWN));
    f = 0; assert(halo_mobile_refresh(-5, HALO_MOBILE_RATE_60, &f) == 60 && (f & HALO_MOBILE_FLAG_RATE_UNKNOWN));
    f = 0; assert(halo_mobile_refresh(5000, HALO_MOBILE_RATE_DISPLAY, &f) == 60 && (f & HALO_MOBILE_FLAG_RATE_UNKNOWN));
    f = 0; assert(halo_mobile_refresh(480, HALO_MOBILE_RATE_DISPLAY, &f) == 240 && (f & HALO_MOBILE_FLAG_RATE_CLAMPED));
    /* Only requests above Halo's own 30 lift its throttle. */
    assert(halo_mobile_lifts_throttle(120) && halo_mobile_lifts_throttle(31) && !halo_mobile_lifts_throttle(30) && !halo_mobile_lifts_throttle(24));
    /* The presenter follows its current screen down, never up. */
    assert(halo_mobile_presenter_fps(120, 0) == 120 && halo_mobile_presenter_fps(120, 60) == 60);
    assert(halo_mobile_presenter_fps(60, 120) == 60 && halo_mobile_presenter_fps(0, 0) == 60);
    assert(halo_mobile_presenter_fps(120, 5000) == 120 && halo_mobile_presenter_fps(-1, 30) == 30);
}

static uint64_t gpu(uint64_t f) { return f + f / 16; }   /* Metal colour textures: +1/16 headroom */
static void memory(void) {
    HaloMobileMemory m;
    halo_mobile_memory(2752, 2064, 2752, 2064, &m);
    const uint64_t frame = 2752ull * 2064ull * 4ull, g = gpu(frame); /* 22,720,512 */
    const uint64_t arenas = 3ull * 32 * 1024 * 1024;
    assert(m.guest_color == frame && m.guest_depth == frame && m.metal_color == g && m.metal_depth == 2 * frame);
    assert(m.host_frame == frame && m.presenter_cpu == frame && m.drawables == 3 * g && m.arenas == arenas);
    /* Five presenter textures: the new one before a drawable, the current one, three in flight. */
    assert(m.presenter_textures == 5 * g);
    assert(m.steady_total == 6 * frame + 9 * g + arenas && m.reset_extra == 4 * frame + g);
    assert(m.peak_total == 10 * frame + 10 * g + arenas);
    /* Any Reset into this size from a mode at the admission bound. */
    const uint64_t big = 2880ull * 2160 * 4;
    assert(m.reset_worst_extra == 2 * big + gpu(big) + 2 * big && m.peak_worst_total == m.steady_total + m.reset_worst_extra);
    assert(m.peak_worst_total >= m.peak_total);
    /* Never below what Metal allocated on the host GPU for these attachments
     * (logs-final/depth-size-probe.log: 4.19 and 5.36 bytes a pixel). */
    assert(m.metal_color >= 2752ull * 2064 * 419 / 100 && m.metal_depth >= 2752ull * 2064 * 536 / 100);
    assert(m.guest_total == 2 * frame && m.guest_total < (0x7F000000ull - 0x50000000ull) / 8);
    printf("memory: 2752x2064 on 2752x2064 steady %.1f MiB, peak %.1f MiB, guest %.1f MiB\n",
           m.steady_total / 1048576.0, m.peak_total / 1048576.0, m.guest_total / 1048576.0);
    /* Drawables follow the panel, not a smaller window. */
    halo_mobile_memory(1600, 1200, 2752, 2064, &m); assert(m.drawables == 3 * g);
    halo_mobile_memory(HALO_MOBILE_MAX_WIDTH, HALO_MOBILE_MAX_HEIGHT, 16384, 16384, &m);
    assert(m.steady_total > 0 && m.drawables == 3 * gpu(16384ull * 16384 * 4));
    halo_mobile_memory(16385, 2, 0, 0, &m); assert(m.steady_total == 0);
    halo_mobile_memory(0, 0, 2752, 2064, &m); assert(m.arenas == 0 && m.steady_total == 3 * g && m.reset_worst_extra == 0);
    /* At the bound itself the worst case is the same-size overlap. */
    halo_mobile_memory(2880, 2160, 2880, 2160, &m); assert(m.reset_worst_extra == m.reset_extra);
    const uint64_t small = 1600ull * 1200 * 4;
    halo_mobile_memory(1600, 1200, 99999, 99999, &m); assert(m.drawables == 0 && m.steady_total == 6 * small + 6 * gpu(small) + arenas);
}

static void plans(void) {
    HaloMobilePlan p; char mode[64];
    /* Saved native in a landscape full-screen window on a 13-inch panel at 120 Hz. */
    halo_mobile_plan("native", "display", NULL, 2752, 2064, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_NATIVE && p.render.width == 2752 && p.render.height == 2064);
    assert(p.refresh == 120 && p.display_width == 2752 && p.display_height == 2064 && !p.flags);
    assert(halo_mobile_vidmode(&p, NULL, mode, sizeof mode) && !strcmp(mode, "-vidmode 2752,2064,60"));
    /* A Stage Manager window on that panel: the window drives the fit, the
     * panel (as UIKit reports it, portrait) drives the drawable estimate. */
    halo_mobile_plan_view("native", "display", NULL, 1500, 1100, 2064, 2752, 120, &p);
    assert(p.render.width == 1488 && p.render.height == 1091 && p.view_width == 1500 && p.view_height == 1100);
    assert(p.display_width == 2752 && p.display_height == 2064 && p.memory.drawables == 3 * gpu(2752ull * 2064 * 4));
    /* No window yet (no scene attached): native falls back to the old default. */
    halo_mobile_plan("native", NULL, NULL, 0, 0, 0, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_NATIVE_FALLBACK && p.render.width == 1600 && p.render.height == 1200);
    assert(p.refresh == 60 && (p.flags & HALO_MOBILE_FLAG_DISPLAY_UNKNOWN) && (p.flags & HALO_MOBILE_FLAG_RATE_UNKNOWN));
    /* Presets keep their pixel budgets while adopting the window aspect. */
    halo_mobile_plan("2048x1536", "60", NULL, 2868, 1320, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_PRESET && p.render.width == 2608 && p.render.height == 1200 && p.refresh == 60);
    /* Every saved quality budget preserves each supported device/window
     * aspect, and can never exceed the old preset's allocation budget. */
    const int64_t views[][2] = {{2752,2064}, {2420,1668}, {2868,1320}, {1500,1100}, {900,1200}};
    for (unsigned v = 0; v < sizeof views / sizeof views[0]; v++) for (int q = 0; q < HALO_MOBILE_PRESET_COUNT; q++) {
        char stored[32]; HaloMobileSize budget = halo_mobile_presets[q];
        snprintf(stored, sizeof stored, "%ux%u", budget.width, budget.height);
        halo_mobile_plan(stored, "display", NULL, views[v][0], views[v][1], 120, &p);
        assert(p.size_source == HALO_MOBILE_SIZE_PRESET && halo_mobile_size_allowed(p.render.width, p.render.height));
        assert((uint64_t)p.render.width * p.render.height <= (uint64_t)budget.width * budget.height);
        int64_t error = (int64_t)p.render.height * views[v][0] - (int64_t)p.render.width * views[v][1];
        assert(error * 2 <= views[v][0] && error * 2 >= -views[v][0]);
    }
    /* Nothing or garbage stored: the old default, garbage flagged. */
    halo_mobile_plan(NULL, NULL, NULL, 2752, 2064, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_DEFAULT && p.render.width == 1600 && !(p.flags & HALO_MOBILE_FLAG_STORED_REJECTED));
    halo_mobile_plan("8192x6144", NULL, NULL, 2752, 2064, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_DEFAULT && p.render.width == 1600 && (p.flags & HALO_MOBILE_FLAG_STORED_REJECTED));
    /* A safe explicit override wins and is passed as typed; an unsafe one is
     * flagged (prepare_launch refuses it) and never chosen. */
    halo_mobile_plan("native", NULL, "1024,768,75", 2752, 2064, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_OVERRIDE && p.render.width == 1024);
    assert(halo_mobile_vidmode(&p, "1024,768,75", mode, sizeof mode) && !strcmp(mode, "-vidmode 1024,768,75"));
    halo_mobile_plan("native", "display", "4095,3071,60", 2752, 2064, 120, &p);
    assert(p.size_source == HALO_MOBILE_SIZE_NATIVE && p.render.width == 2752 && (p.flags & HALO_MOBILE_FLAG_OVERRIDE_REJECTED));
    assert(halo_mobile_size_allowed(p.render.width, p.render.height));
    /* A short buffer is refused rather than truncated into a different mode. */
    assert(!halo_mobile_vidmode(&p, NULL, mode, 12));
    /* A 24 Hz display in a plan: requested 24. */
    halo_mobile_plan("native", "display", NULL, 2752, 2064, 24, &p); assert(p.refresh == 24);
}

static void cadence(void) {
    HaloMobileCadence c; memset(&c, 0, sizeof c);
    /* Off: never waits. */
    assert(halo_mobile_cadence_release(&c, 1000) == 1000);
    halo_mobile_cadence_set(&c, 0); assert(!c.period_ns);
    halo_mobile_cadence_set(&c, 5000); assert(!c.period_ns);
    halo_mobile_cadence_set(&c, 100); assert(c.period_ns == 10000000ull);
    uint64_t t = 1000000000ull;
    /* First frame goes at once; a faster engine is held to the 10 ms grid. */
    assert(halo_mobile_cadence_release(&c, t) == t);
    assert(halo_mobile_cadence_release(&c, t + 1000000) == t + 10000000);
    assert(halo_mobile_cadence_release(&c, t + 10500000) == t + 20000000);
    assert(c.waits == 2 && c.wait_ns == 9000000 + 9500000);
    /* 5 ms after the last release is still ahead of the grid: it waits. */
    assert(halo_mobile_cadence_release(&c, t + 25000000) == t + 30000000 && c.waits == 3);
    /* A frame behind its slot is never delayed further: late by less than a
     * period goes at once and keeps the grid. */
    assert(halo_mobile_cadence_release(&c, t + 45000000) == t + 45000000 && c.late == 1);
    /* The next grid point was 50 ms; a stall past a whole period restarts it
     * instead of releasing back-to-back catch-up frames. */
    assert(halo_mobile_cadence_release(&c, t + 90000000) == t + 90000000 && c.restarts == 1);
    assert(halo_mobile_cadence_release(&c, t + 91000000) == t + 100000000);
    /* A 30 fps producer under a 120 fps request: no waits at all. */
    HaloMobileCadence slow; memset(&slow, 0, sizeof slow); halo_mobile_cadence_set(&slow, 120);
    for (int i = 0; i < 300; i++) assert(halo_mobile_cadence_release(&slow, t + (uint64_t)i * 33333333ull) == t + (uint64_t)i * 33333333ull);
    assert(slow.waits == 0 && slow.frames == 300);
    /* An unthrottled producer averages exactly the request over time. */
    HaloMobileCadence fast; memset(&fast, 0, sizeof fast); halo_mobile_cadence_set(&fast, 120);
    uint64_t now = t, last = 0;
    for (int i = 0; i < 1200; i++) { last = halo_mobile_cadence_release(&fast, now); now = last + 2000000; }
    double seconds = (double)(last - t) * 1e-9;
    assert(seconds > 9.98 && seconds < 10.0 && fast.waits == 1199);
    /* A clock that moved backwards restarts rather than holding a frame long. */
    assert(halo_mobile_cadence_release(&fast, t) == t && fast.restarts == 1);
    /* Changing the rate restarts the grid. */
    halo_mobile_cadence_set(&fast, 60); assert(fast.next_ns == 0 && fast.period_ns == 16666666ull);
}

int main(void) {
    native_fits(); parsing(); extras(); refresh(); memory(); plans(); cadence();
    puts("mobile graphics: adaptive oriented aspect and preset pixel budgets, one admission bound, stored/override/extras parsing, refresh never above the display, conservative memory budget, cadence pass");
    return 0;
}
