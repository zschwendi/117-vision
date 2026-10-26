/* GPU timing (metalrenderer_timing.inc) on the Mac GPU and with synthetic
 * timestamps:
 *  1. Long-uptime clock with parts-per-million calibration drift between two
 *     overlapping command buffers: the union stays the true 1.5 s (the
 *     first-pass absolute scaling reported ~2.0 s), categories add up.
 *  2. Admission: a frame whose first command buffer began with timing off is
 *     rejected after timing turns on, as is a rapid off/on toggle inside one
 *     frame; the next fully instrumented frame is measured.
 *  3. A real frame: scene, HUD, post and copy passes all measured, categories
 *     summing to the busy union, busy within the command-buffer span.
 *  4. More passes than one sample buffer holds: truncated, never reported.
 *  5. 16384 intervals in adversarial order: exact union, bounded time.
 *  6. Readers on other threads while the renderer starts, measures and
 *     resets (build with -fsanitize=thread to check the publication).
 * Prints SKIP without a Metal device.
 *
 * clang -O2 -fobjc-arc -fblocks tests/test_metalrenderer_gpu_timing.m
 *   -framework Foundation -framework Metal -o /tmp/mr-gpu-timing */
#define main renderer_fixture_main
#include "test_metalrenderer_fastpaths.m"
#undef main
#include <pthread.h>

static void frame_and_wait(void) {
    id<MTLCommandBuffer> last = g_shared->pending_cb;
    mr_commit_async(NULL, NULL);
    if (last) [last waitUntilCompleted];
    if (g_shared->last_cb) [g_shared->last_cb waitUntilCompleted];
    for (int i = 0; i < 200; ++i) {   /* completion handlers can run just after the wait */
        os_unfair_lock_lock(&g_timing.lock);
        int pending = 0; for (int k = 0; k < MR_TIMING_FRAMES; ++k) pending |= g_timing.records[k].frame != 0;
        os_unfair_lock_unlock(&g_timing.lock);
        if (!pending) return;
        usleep(1000);
    }
    assert(!"timing records did not finalize");
}
static void draw_quad(mr_context *c, uint32_t color, float x0, float y0, float x1, float y1) {
    mr_vertex_rhw v[4] = { { x0, y0, 0, 1, color, 0, 0 }, { x1, y0, 0, 1, color, 1, 0 },
                           { x0, y1, 0, 1, color, 0, 1 }, { x1, y1, 0, 1, color, 1, 1 } };
    mr_draw_state st = {0}; st.blend = MR_BLEND_NONE; st.color_write_mask = 0xF; st.alpha_test_ref = -1; st.depth_compare = 8;
    assert(mr_draw_rhw(c, &st, MR_TRIANGLE_STRIP, v, sizeof v[0], 4, NULL, 0) == MR_OK);
}
static uint64_t brute_union(const mr_timing_interval *v, unsigned n) {
    /* Merge sorted copies, independently of timing_attribute. */
    mr_timing_interval *s = malloc(n * sizeof *s); memcpy(s, v, n * sizeof *s);
    for (unsigned i = 0; i < n; ++i) for (unsigned j = i + 1; j < n; ++j) if (s[j].start < s[i].start) { mr_timing_interval t = s[i]; s[i] = s[j]; s[j] = t; }
    uint64_t total = 0, a = s[0].start, b = s[0].end;
    for (unsigned i = 1; i < n; ++i) { if (s[i].start > b) { total += b - a; a = s[i].start; b = s[i].end; } else if (s[i].end > b) b = s[i].end; }
    total += b - a; free(s); return total;
}

static _Atomic int readers_run = 1;
static void *reader(void *arg) {
    (void)arg; unsigned reads = 0;
    while (atomic_load(&readers_run)) { mr_gpu_timing_stats st; mr_gpu_timing_read(&st); mr_vrr_stats_t v; mr_vrr_stats(&v); reads++; }
    return (void *)(uintptr_t)reads;
}

int main(void) { @autoreleasepool {
    /* 6 starts first: readers race the renderer's first use of timing. */
    pthread_t threads[2];
    for (int i = 0; i < 2; ++i) pthread_create(&threads[i], NULL, reader, NULL);
    mr_gpu_timing_set_enabled(1);
    mr_context *c = mr_create(256, 192), *hud = mr_create(256, 192);
    if (!c || !hud) { atomic_store(&readers_run, 0); puts("SKIP no Metal"); return 77; }
    mr_set_overlay_target(hud);
    draw_quad(c, 0xFF102030u, 0, 0, 256, 192); frame_and_wait();
    if (!timing_supported(c->s)) { atomic_store(&readers_run, 0); puts("SKIP no stage-boundary timestamps"); return 77; }
    mr_gpu_timing_reset();

    /* 1. Synthetic: reference pair as in timestamp-adversarial-proof.json,
     * two command buffers of one frame each completing with its own
     * calibration pair (slopes 1.0 and 1.000001 against the reference). */
    {
        MTLTimestamp save_cpu = g_timing.reference_cpu, save_gpu = g_timing.reference_gpu;
        os_unfair_lock_lock(&g_timing.lock);
        g_timing.reference_gpu = 1000000000000000ull; g_timing.reference_cpu = 1000001000000000ull;
        mr_timing_record *r = &g_timing.records[0]; memset(r, 0, sizeof *r); r->frame = 0x51; r->committed = 2;
        os_unfair_lock_unlock(&g_timing.lock);
        mr_timing_plan plan = { .count = 1 }; plan.kind[0] = MR_TIMING_RENDER;
        uint64_t a[4] = { 1000001000000000ull, 1000002000000000ull, 1000001000000000ull, 1000002000000000ull };
        uint64_t b[4] = { 1000001500000000ull, 1000002500000000ull, 1000001500000000ull, 1000002500000000ull };
        /* gpu delta 10 s since the reference: slope 1.0, then 1.000001. */
        plan.category[0] = MR_GPU_SCENE;
        timing_command_completed(0x51, &plan, a, 4, 0, 1, 100.0, 101.0, 1000011000000000ull, 1000010000000000ull);
        plan.category[0] = MR_GPU_POST;
        timing_command_completed(0x51, &plan, b, 4, 0, 1, 100.5, 101.5, 1000011000010000ull, 1000010000000000ull + 1);
        os_unfair_lock_lock(&g_timing.lock);
        r->closed = 1; r->published = 1; timing_finalize(r);
        mr_gpu_timing_frame f = g_timing.history[(g_timing.history_next + MR_TIMING_HISTORY - 1) % MR_TIMING_HISTORY];
        g_timing.reference_cpu = save_cpu; g_timing.reference_gpu = save_gpu;
        os_unfair_lock_unlock(&g_timing.lock);
        int64_t error = (int64_t)f.busy_ns - 1500000000ll;
        printf("synthetic drift: busy %llu ns (error %lld ns), scene %llu post %llu, span %llu ns sum %llu ns\n",
               (unsigned long long)f.busy_ns, (long long)error, (unsigned long long)f.category_ns[MR_GPU_SCENE],
               (unsigned long long)f.category_ns[MR_GPU_POST], (unsigned long long)f.command_span_ns, (unsigned long long)f.command_sum_ns);
        assert(llabs(error) <= 2500);
        assert(f.category_ns[MR_GPU_SCENE] + f.category_ns[MR_GPU_POST] == f.busy_ns);
        assert(llabs((long long)f.category_ns[MR_GPU_SCENE] - 1000000000ll) <= 2500);
        assert(f.command_span_ns == 1500000000ull && f.command_sum_ns == 2000000000ull);
        /* What converting absolute ticks per callback would have reported. */
        double first = 1.0, second = 1.000001;
        double absolute = (b[1] * second) - (a[0] * first);
        printf("absolute per-callback scaling would report %.0f ns\n", absolute);
        assert(absolute > 1.9e9);
    }
    mr_gpu_timing_reset();

    /* 2. Admission. Off for the first command buffer of a frame, on before
     * a second one (the readback commits the first). */
    mr_gpu_timing_set_enabled(0);
    draw_quad(c, 0xFF00FF00u, 0, 0, 128, 96);
    uint32_t px[256 * 192]; assert(!mr_read_framebuffer(c, px, sizeof px));
    mr_gpu_timing_set_enabled(1);
    draw_quad(c, 0xFF0000FFu, 128, 96, 256, 192);
    frame_and_wait();
    mr_gpu_timing_stats st; mr_gpu_timing_read(&st);
    assert(st.frames_measured == 0 && st.frames_incomplete == 1);
    /* Rapid off/on between command buffers inside one instrumented frame. */
    draw_quad(c, 0xFF00FF00u, 0, 0, 64, 64); assert(!mr_read_framebuffer(c, px, sizeof px));
    mr_gpu_timing_set_enabled(0); draw_quad(c, 0xFF00FF00u, 0, 0, 64, 64); assert(!mr_read_framebuffer(c, px, sizeof px));
    mr_gpu_timing_set_enabled(1); draw_quad(c, 0xFF00FF00u, 0, 0, 64, 64);
    frame_and_wait();
    mr_gpu_timing_read(&st);
    printf("toggle: measured %llu incomplete %llu truncated %llu failed %llu\n",(unsigned long long)st.frames_measured,(unsigned long long)st.frames_incomplete,(unsigned long long)st.frames_truncated,(unsigned long long)st.frames_failed);
    assert(st.frames_measured == 0 && st.frames_incomplete == 2);

    /* 3. A fully instrumented frame of every category. */
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:256 height:192 mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> layer = [c->s->dev newTextureWithDescriptor:d], hud_layer = [c->s->dev newTextureWithDescriptor:d];
    for (int i = 0; i < 64; ++i) draw_quad(c, 0xFF000000u | (uint32_t)(i * 4), 0, 0, 256, 192);
    draw_quad(hud, 0x80FFFFFFu, 32, 32, 96, 96);
    assert(mr_fxaa_target_to(c, (__bridge void *)layer, .5f) == MR_OK);
    assert(mr_blit_target_to(hud, (__bridge void *)hud_layer) == MR_OK);
    frame_and_wait();
    mr_gpu_timing_read(&st);
    assert(st.frames_measured == 1 && st.window == 1);
    mr_gpu_timing_frame f = st.last;
    uint64_t sum = 0; for (int k = 0; k < MR_GPU_CATEGORIES; ++k) sum += f.category_ns[k];
    printf("real frame: scene %.3f ms hud %.3f post %.3f copy %.3f busy %.3f span %.3f sum %.3f ns/tick %.6f intervals %u buffers %u\n",
           f.category_ns[0] / 1e6, f.category_ns[1] / 1e6, f.category_ns[2] / 1e6, f.category_ns[3] / 1e6, f.busy_ns / 1e6,
           f.command_span_ns / 1e6, f.command_sum_ns / 1e6, st.ns_per_tick, f.intervals, f.command_buffers);
    printf("sampled passes: scene %u hud %u post %u copy %u\n", f.category_intervals[0], f.category_intervals[1], f.category_intervals[2], f.category_intervals[3]);
    for (int k = 0; k < MR_GPU_CATEGORIES; ++k) assert(f.category_intervals[k] >= 1);
    assert(f.category_ns[MR_GPU_SCENE] > 0);
    assert(llabs((long long)sum - (long long)f.busy_ns) <= MR_GPU_CATEGORIES);   /* per-category rounding only */
    { /* exclusive: no category exceeds the union, whatever overlapped */
      for (int k = 0; k < MR_GPU_CATEGORIES; ++k) assert(f.category_ns[k] <= f.busy_ns); }
    assert(f.busy_ns <= f.command_span_ns + f.command_span_ns / 10 + 200000);      /* different clocks: small slack */
    assert(st.ns_per_tick > 0.5 && st.ns_per_tick < 2e3);

    /* 3b. Clear-only passes leave their fragment samples untouched; with
     * reused sample buffers those would be timestamps of earlier frames.
     * Every frame's busy time must stay within its own command buffers. */
    for (int k = 0; k < 24; ++k) {
        mr_clear(c, 0xFF000000u); mr_clear_depth(c, 1.f); mr_clear_stencil(c, 0);
        draw_quad(c, 0xFF406080u, 0, 0, 256, 192);
        assert(mr_blit_target_to(c, (__bridge void *)layer) == MR_OK);
        uint64_t before = st.frames_measured;
        frame_and_wait(); usleep(20000);   /* time passes between frames: stale samples would be far off */
        mr_gpu_timing_read(&st);
        assert(st.frames_measured == before + 1);
        assert(st.last.busy_ns <= st.last.command_span_ns + st.last.command_span_ns / 10 + 200000);
    }
    printf("clear-heavy frames: busy %.3f ms mean, span %.3f ms mean, %llu empty (unrun) stage samples ignored\n",
           st.busy_mean_ms, st.command_span_mean_ms, (unsigned long long)st.empty_intervals);
    mr_gpu_timing_reset(); mr_gpu_timing_read(&st);
    /* 4. More passes than one sample buffer holds (1024): truncated. */
    for (int i = 0; i < MR_TIMING_INTERVALS + 8; ++i) { draw_quad(c, 0xFF101010u, 0, 0, 8, 8); end_pending_encoder(c->s); }
    frame_and_wait();
    mr_gpu_timing_read(&st);
    assert(st.frames_measured == 0 && st.frames_truncated == 1);

    /* 5. Capacity and adversarial ordering of the attribution. */
    {
        unsigned n = MR_TIMING_FRAME_INTERVALS;
        mr_timing_interval *v = malloc(n * sizeof *v);
        for (unsigned i = 0; i < n; ++i) {   /* descending starts, nested and disjoint mixed */
            uint64_t s0 = (uint64_t)(n - i) * 1000ull + 1000000000000ull;
            v[i] = (mr_timing_interval){ s0, s0 + (i % 3 == 0 ? 2500 : 700), (uint8_t)(i % MR_GPU_CATEGORIES) };
        }
        uint64_t expect = brute_union(v, 4096);   /* the brute force is quadratic: check a prefix exactly */
        mr_gpu_timing_frame g = {0};
        mr_timing_interval *prefix = malloc(4096 * sizeof *prefix); memcpy(prefix, v, 4096 * sizeof *prefix);
        timing_attribute(prefix, 4096, 1.0, &g);
        assert(g.busy_ns == expect);
        uint64_t total = 0; for (int k = 0; k < MR_GPU_CATEGORIES; ++k) total += g.category_ns[k];
        assert(total == g.busy_ns);
        uint64_t start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        memset(&g, 0, sizeof g); timing_attribute(v, n, 1.0, &g);
        uint64_t elapsed = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - start;
        printf("attribution of %u intervals: %.3f ms\n", n, elapsed / 1e6);
        assert(elapsed < 50000000ull);
        free(v); free(prefix);
    }

    /* 6. Reset while readers run, then stop them. */
    mr_gpu_timing_reset();
    draw_quad(c, 0xFF202020u, 0, 0, 256, 192); frame_and_wait();
    atomic_store(&readers_run, 0);
    unsigned long reads = 0; for (int i = 0; i < 2; ++i) { void *r; pthread_join(threads[i], &r); reads += (uintptr_t)r; }
    mr_gpu_timing_read(&st);
    assert(st.supported == 1 && st.frames_measured == 1);
    mr_destroy(hud); mr_destroy(c);
    printf("PASS GPU timing: anchored long-uptime drift (error within 2.5 us), off->on and rapid toggle frames rejected, "
           "all four categories measured and exclusive, truncation reported, %u-interval attribution, %lu concurrent reads\n",
           MR_TIMING_FRAME_INTERVALS, reads);
}}
