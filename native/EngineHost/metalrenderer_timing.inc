/* metalrenderer_timing.inc - GPU timing of the renderer's own work, included
 * by metalrenderer.m after the shared/context structures.
 *
 * What it measures. Every pass the renderer encodes while timing is on
 * carries stage-boundary timestamps (the only counter sampling point Apple
 * GPUs offer): start and end of the vertex and fragment stages for render
 * passes, start and end of the encoder for blits. Each pass is tagged with
 * what it is for: the game's world (scene), the HUD and other overlay
 * targets, post-processing (anti-aliasing, upscaling, foveation resolves)
 * and copies (pool blits, uploads, carries). MetalFX encodes its own passes,
 * so it is bracketed by two one-texel marker blits that are ordered against
 * its input and output, and recorded as one envelope.
 *
 * When it is reported. A frame ends at mr_commit_async (published),
 * mr_finish_dropped_frame (dropped) or mr_gpu_timing_end_frame. Its numbers
 * are kept only when every command buffer created for it was instrumented
 * (timing was on and a sample buffer was free when each began), every pass
 * was sampled, and every command buffer completed without error. Otherwise
 * the frame is counted as incomplete, truncated or failed, never estimated.
 * Passes overlap on the GPU, so time is attributed exclusively, to the
 * earliest-starting pass covering it: the categories add up to the union of
 * all sampled passes, never to a sum of envelopes. VISR's host_perf_metal.m
 * (Apache-2.0) uses the same union idea; this implementation is independent.
 *
 * Stale samples. A stage that does not run leaves its sample slot as it was;
 * each sample buffer's high-water mark rejects such leftovers (see
 * mr_timing_buffer).
 *
 * Clock. Intervals are kept in raw GPU ticks for the whole frame: unions and
 * attribution are exact in that one clock, whichever command buffer a pass
 * came from. Only durations are converted, once per frame, with the anchored
 * calibration Apple documents ("Converting GPU timestamps into CPU time"):
 * cpu = cpuStart + (gpu - gpuStart) * cpuSpan / gpuSpan, CPU timestamps in
 * nanoseconds. A duration needs only cpuSpan / gpuSpan, taken from the
 * reference pair sampled when timing started and a pair sampled when the
 * frame completes; converting absolute ticks with a per-callback ratio would
 * turn parts-per-million drift at long uptime into large false offsets. */

#define MR_TIMING_BUFFERS 8
#define MR_TIMING_SAMPLES 4096                      /* 32 KiB, the most Apple GPUs accept */
#define MR_TIMING_INTERVALS (MR_TIMING_SAMPLES / 4)
#define MR_TIMING_FRAMES 8                          /* frames whose command buffers may still run */
#define MR_TIMING_HISTORY 128
#define MR_TIMING_FRAME_INTERVALS 16384
#define MR_TIMING_MIN_CALIBRATION 1000000ull        /* ticks and ns: at least 1 ms between the pairs */
enum { MR_TIMING_RENDER = 0, MR_TIMING_BLIT = 1, MR_TIMING_ENVELOPE_OPEN = 2, MR_TIMING_ENVELOPE_CLOSE = 3 };
enum { MR_TIMING_UNKNOWN = 0, MR_TIMING_SUPPORTED = 1, MR_TIMING_UNSUPPORTED = 2 };

typedef struct { uint64_t start, end; uint8_t category; } mr_timing_interval;   /* GPU ticks */
typedef struct {
    id<MTLCounterSampleBuffer> buffer;
    _Atomic int busy;
    /* The latest timestamp this buffer has ever returned. Metal leaves a
     * sample untouched when its stage does not run (a clear-only pass has no
     * fragment work), so a reused buffer can hand back a value from an
     * earlier command buffer. A buffer is reused only after the previous
     * command buffer completed, so every genuine new sample is later than
     * this mark, and anything at or below it is stale. */
    _Atomic uint64_t high_water;
    unsigned count;                 /* engine thread while the command buffer is encoded */
    uint8_t category[MR_TIMING_INTERVALS], kind[MR_TIMING_INTERVALS];
} mr_timing_buffer;
/* What the engine encoded into one command buffer, copied when it is
 * committed: the completion handler never reads engine-thread state. The
 * copy is published with release/acquire so the handoff is ordered by the
 * memory model itself, not only by Metal's commit-to-completion ordering. */
typedef struct { unsigned count; uint8_t category[MR_TIMING_INTERVALS], kind[MR_TIMING_INTERVALS]; _Atomic int published; } mr_timing_plan;
typedef struct {
    uint64_t frame;                 /* 0: slot free */
    int closed, published, incomplete, truncated, failed;
    unsigned committed, completed;
    double command_sum, command_first, command_last;   /* seconds, GPUStartTime/GPUEndTime clock */
    uint64_t empty;
    MTLTimestamp cpu_now, gpu_now;  /* the latest calibration pair of the frame */
    mr_timing_interval *intervals; unsigned count, capacity;
} mr_timing_record;

static struct {
    _Atomic int capability;         /* MR_TIMING_*; published after everything below is set */
    id<MTLCounterSet> counters;
    mr_timing_buffer buffers[MR_TIMING_BUFFERS];
    id<MTLTexture> marker;          /* 1x1 destination of the MetalFX envelope blits */
    MTLTimestamp reference_cpu, reference_gpu;   /* written before capability is published */
    _Atomic int enabled;            /* -1: not yet seeded from HALO_GPU_TIMING */
    /* Engine thread. */
    uint64_t frame;                 /* the frame being encoded */
    int record;                     /* its record, or -1 when it is not instrumented */
    int buffer;                     /* sample buffer of the pending command buffer, or -1 */
    int tainted;                    /* this frame already has a command buffer without timing */
    int unsampled;                  /* the pending command buffer began with timing off */
    /* Completion threads and readers, under lock. */
    os_unfair_lock lock;
    mr_timing_record records[MR_TIMING_FRAMES];
    mr_gpu_timing_frame history[MR_TIMING_HISTORY];
    unsigned history_next, history_count;
    uint64_t measured, incomplete, truncated, failed, empty;
    double ns_per_tick;
} g_timing = { .enabled = -1, .frame = 1, .record = -1, .buffer = -1, .lock = OS_UNFAIR_LOCK_INIT };

int mr_gpu_timing_enabled(void) {
    int enabled = atomic_load_explicit(&g_timing.enabled, memory_order_relaxed);
    if (enabled < 0) {
        const char *e = getenv("HALO_GPU_TIMING");
        int wanted = e && e[0] == '1';
        atomic_compare_exchange_strong_explicit(&g_timing.enabled, &enabled, wanted, memory_order_relaxed, memory_order_relaxed);
        enabled = atomic_load_explicit(&g_timing.enabled, memory_order_relaxed);
    }
    return enabled;
}
void mr_gpu_timing_set_enabled(int enabled) {
    atomic_store_explicit(&g_timing.enabled, enabled ? 1 : 0, memory_order_relaxed);
}

/* Engine thread, with the shared device; the first call decides. */
static int timing_supported(mr_shared *s) {
    int capability = atomic_load_explicit(&g_timing.capability, memory_order_acquire);
    if (capability != MR_TIMING_UNKNOWN) return capability == MR_TIMING_SUPPORTED;
    int supported = 0;
    @autoreleasepool {
    if (s && s->dev && [s->dev supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        for (id<MTLCounterSet> set in s->dev.counterSets)
            if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
                for (id<MTLCounter> counter in set.counters)
                    if ([counter.name isEqualToString:MTLCommonCounterTimestamp]) g_timing.counters = set;
    }
    if (g_timing.counters) {
        MTLCounterSampleBufferDescriptor *d = [[MTLCounterSampleBufferDescriptor alloc] init];
        d.counterSet = g_timing.counters; d.storageMode = MTLStorageModeShared; d.sampleCount = MR_TIMING_SAMPLES;
        supported = 1;
        for (int i = 0; i < MR_TIMING_BUFFERS && supported; ++i) {
            NSError *e = nil;
            g_timing.buffers[i].buffer = [s->dev newCounterSampleBufferWithDescriptor:d error:&e];
            if (!g_timing.buffers[i].buffer) { NSLog(@"metalrenderer: GPU timing unavailable: %@", e); supported = 0; }
        }
        MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        md.storageMode = MTLStorageModePrivate; md.usage = MTLTextureUsageShaderRead;
        if (supported) g_timing.marker = [s->dev newTextureWithDescriptor:md];
        if (!g_timing.marker) supported = 0;
        if (!supported) for (int i = 0; i < MR_TIMING_BUFFERS; ++i) g_timing.buffers[i].buffer = nil;
        else {
            [s->dev sampleTimestamps:&g_timing.reference_cpu gpuTimestamp:&g_timing.reference_gpu];
            /* Whatever a new buffer holds predates this moment. */
            for (int i = 0; i < MR_TIMING_BUFFERS; ++i) atomic_store(&g_timing.buffers[i].high_water, g_timing.reference_gpu);
        }
    }
    }
    atomic_store_explicit(&g_timing.capability, supported ? MR_TIMING_SUPPORTED : MR_TIMING_UNSUPPORTED, memory_order_release);
    return supported;
}

static int timing_interval_order(const void *a, const void *b) {
    const mr_timing_interval *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start;
}
/* Nanoseconds per GPU tick from the reference pair and a later pair, or 0
 * when they are too close together to say. */
static double timing_slope(MTLTimestamp reference_cpu, MTLTimestamp reference_gpu, MTLTimestamp cpu, MTLTimestamp gpu) {
    if (cpu <= reference_cpu || gpu <= reference_gpu) return 0.0;
    uint64_t dc = cpu - reference_cpu, dg = gpu - reference_gpu;
    if (dc < MR_TIMING_MIN_CALIBRATION || dg < MR_TIMING_MIN_CALIBRATION) return 0.0;
    return (double)dc / (double)dg;
}
/* Exclusive attribution of a frame's intervals, in ticks, then one
 * conversion. Pure apart from sorting `v` in place; tests call it. */
static void timing_attribute(mr_timing_interval *v, unsigned n, double ns_per_tick, mr_gpu_timing_frame *f) {
    qsort(v, n, sizeof *v, timing_interval_order);
    uint64_t ticks[MR_GPU_CATEGORIES] = {0}, busy = 0, covered = 0;
    for (unsigned i = 0; i < n; ++i) {
        f->category_intervals[v[i].category]++;
        uint64_t start = v[i].start > covered ? v[i].start : covered;
        if (v[i].end > start) { ticks[v[i].category] += v[i].end - start; busy += v[i].end - start; }
        if (v[i].end > covered) covered = v[i].end;
    }
    for (int c = 0; c < MR_GPU_CATEGORIES; ++c) f->category_ns[c] = (uint64_t)((double)ticks[c] * ns_per_tick + 0.5);
    f->busy_ns = (uint64_t)((double)busy * ns_per_tick + 0.5);
}

static mr_timing_record *timing_record_for(uint64_t frame) {   /* under lock */
    for (int i = 0; i < MR_TIMING_FRAMES; ++i) if (g_timing.records[i].frame == frame) return &g_timing.records[i];
    return NULL;
}
static void timing_finalize(mr_timing_record *r) {   /* under lock */
    g_timing.empty += r->empty;
    double ns_per_tick = timing_slope(g_timing.reference_cpu, g_timing.reference_gpu, r->cpu_now, r->gpu_now);
    if (r->failed) g_timing.failed++;
    else if (r->truncated) g_timing.truncated++;
    else if (r->incomplete || !r->count || !(ns_per_tick > 0)) g_timing.incomplete++;
    else {
        mr_gpu_timing_frame f; memset(&f, 0, sizeof f);
        timing_attribute(r->intervals, r->count, ns_per_tick, &f);
        f.frame = r->frame; f.published = (uint32_t)r->published; f.intervals = r->count;
        f.command_buffers = r->committed;
        f.command_sum_ns = r->command_sum > 0 ? (uint64_t)(r->command_sum * 1e9 + 0.5) : 0;
        f.command_span_ns = r->command_last > r->command_first ? (uint64_t)((r->command_last - r->command_first) * 1e9 + 0.5) : 0;
        g_timing.ns_per_tick = ns_per_tick;
        g_timing.history[g_timing.history_next] = f;
        g_timing.history_next = (g_timing.history_next + 1) % MR_TIMING_HISTORY;
        if (g_timing.history_count < MR_TIMING_HISTORY) g_timing.history_count++;
        g_timing.measured++;
    }
    free(r->intervals);
    memset(r, 0, sizeof *r);
}

/* A new pending command buffer (engine thread). Timing is decided here, once
 * per command buffer: a later toggle does not split one. */
static void timing_command_buffer_began(mr_shared *s) {
    g_timing.buffer = -1; g_timing.unsampled = 0;
    int on = mr_gpu_timing_enabled() && timing_supported(s);
    if (!on) {
        g_timing.unsampled = 1;   /* its passes are not sampled: incomplete, not truncated */
        if (g_timing.record >= 0) {
            os_unfair_lock_lock(&g_timing.lock);
            g_timing.records[g_timing.record].incomplete = 1;
            os_unfair_lock_unlock(&g_timing.lock);
        } else g_timing.tainted = 1;   /* work of this frame nobody measures */
        return;
    }
    if (g_timing.record < 0) {
        os_unfair_lock_lock(&g_timing.lock);
        for (int i = 0; i < MR_TIMING_FRAMES; ++i) if (!g_timing.records[i].frame) {
            memset(&g_timing.records[i], 0, sizeof g_timing.records[i]);
            g_timing.records[i].frame = g_timing.frame;
            /* Timing came on after this frame had already committed or
             * begun work: what it reported would leave that out. */
            g_timing.records[i].incomplete = g_timing.tainted;
            g_timing.record = i; break;
        }
        if (g_timing.record < 0) g_timing.incomplete++;   /* eight frames still in flight */
        os_unfair_lock_unlock(&g_timing.lock);
        if (g_timing.record < 0) { g_timing.tainted = 1; return; }
    }
    for (int i = 0; i < MR_TIMING_BUFFERS; ++i) {
        int idle = 0;
        if (atomic_compare_exchange_strong(&g_timing.buffers[i].busy, &idle, 1)) {
            g_timing.buffers[i].count = 0; g_timing.buffer = i; return;
        }
    }
    os_unfair_lock_lock(&g_timing.lock);
    g_timing.records[g_timing.record].truncated = 1;   /* no sample buffer free */
    os_unfair_lock_unlock(&g_timing.lock);
}

/* The next interval index of the pending command buffer, or -1. */
static int timing_take(int category, int kind) {
    if (g_timing.record < 0 || g_timing.unsampled) return -1;
    mr_timing_buffer *b = g_timing.buffer >= 0 ? &g_timing.buffers[g_timing.buffer] : NULL;
    if (!b || b->count >= MR_TIMING_INTERVALS) {
        os_unfair_lock_lock(&g_timing.lock);
        g_timing.records[g_timing.record].truncated = 1;
        os_unfair_lock_unlock(&g_timing.lock);
        return -1;
    }
    int index = (int)b->count++;
    b->category[index] = (uint8_t)category; b->kind[index] = (uint8_t)kind;
    return index;
}
static void timing_attach_render(MTLRenderPassDescriptor *rp, int category) {
    int index = timing_take(category, MR_TIMING_RENDER);
    if (index < 0) return;
    MTLRenderPassSampleBufferAttachmentDescriptor *a = rp.sampleBufferAttachments[0];
    a.sampleBuffer = g_timing.buffers[g_timing.buffer].buffer;
    a.startOfVertexSampleIndex = (NSUInteger)index * 4;
    a.endOfVertexSampleIndex = (NSUInteger)index * 4 + 1;
    a.startOfFragmentSampleIndex = (NSUInteger)index * 4 + 2;
    a.endOfFragmentSampleIndex = (NSUInteger)index * 4 + 3;
}
static id<MTLRenderCommandEncoder> timing_render_encoder(mr_shared *s, MTLRenderPassDescriptor *rp, int category) {
    timing_attach_render(rp, category);
    return [s->pending_cb renderCommandEncoderWithDescriptor:rp];
}
static id<MTLBlitCommandEncoder> timing_blit_encoder_kind(mr_shared *s, int category, int kind) {
    int index = timing_take(category, kind);
    if (index < 0) return [s->pending_cb blitCommandEncoder];
    MTLBlitPassDescriptor *bp = [MTLBlitPassDescriptor blitPassDescriptor];
    MTLBlitPassSampleBufferAttachmentDescriptor *a = bp.sampleBufferAttachments[0];
    a.sampleBuffer = g_timing.buffers[g_timing.buffer].buffer;
    a.startOfEncoderSampleIndex = (NSUInteger)index * 4;
    a.endOfEncoderSampleIndex = (NSUInteger)index * 4 + 1;
    return [s->pending_cb blitCommandEncoderWithDescriptor:bp];
}
static id<MTLBlitCommandEncoder> timing_blit_encoder(mr_shared *s, int category) {
    return timing_blit_encoder_kind(s, category, MR_TIMING_BLIT);
}
/* One-texel blit from `texture`, ordered after its writers by Metal's hazard
 * tracking: opens or closes the envelope of work the renderer cannot sample. */
static void timing_marker(mr_shared *s, id<MTLTexture> texture, int category, int close) {
    if (g_timing.record < 0 || !g_timing.marker || !texture) return;
    id<MTLBlitCommandEncoder> blit = timing_blit_encoder_kind(s, category, close ? MR_TIMING_ENVELOPE_CLOSE : MR_TIMING_ENVELOPE_OPEN);
    if (!blit) return;
    if (texture.pixelFormat == MTLPixelFormatBGRA8Unorm && texture.textureType == MTLTextureType2D)
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(1, 1, 1)
                    toTexture:g_timing.marker destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
}

static uint64_t timing_valid_sample(uint64_t value) { return value && value != MTLCounterErrorValue ? value : 0; }

/* One completed command buffer of `frame`: its raw samples (four per planned
 * interval, NULL when it was not instrumented), whether it completed, its
 * GPU start/end (seconds) and a calibration pair taken now. Called from the
 * completion handler; tests call it with synthetic samples. */
static void timing_command_completed(uint64_t frame, const mr_timing_plan *plan, const uint64_t *samples, size_t sample_count,
                                     uint64_t stale_at_or_below,
                                     int ok, double gpu_start, double gpu_end, MTLTimestamp cpu_now, MTLTimestamp gpu_now) {
    mr_timing_interval local[64]; mr_timing_interval *intervals = local; unsigned count = 0, capacity = 64;
    uint64_t empty = 0; int truncated = 0;
    unsigned n = plan ? plan->count : 0;
    if (ok && n && (!samples || sample_count < (size_t)n * 4)) truncated = 1;
    uint64_t open_start = 0; int open_category = -1;
    for (unsigned i = 0; ok && samples && !truncated && i < n; ++i) {
        const uint64_t *t = samples + (size_t)i * 4;
        uint64_t start = UINT64_MAX, end = 0;
        int pairs = plan->kind[i] == MR_TIMING_RENDER ? 2 : 1;
        for (int p = 0; p < pairs; ++p) {
            uint64_t a = timing_valid_sample(t[p * 2]), z = timing_valid_sample(t[p * 2 + 1]);
            if (!a || !z || z < a || a <= stale_at_or_below) continue;
            if (a < start) start = a;
            if (z > end) end = z;
        }
        if (!end) { empty++; continue; }   /* no work reached a GPU stage */
        if (plan->kind[i] == MR_TIMING_ENVELOPE_OPEN) { open_start = start; open_category = plan->category[i]; continue; }
        if (plan->kind[i] == MR_TIMING_ENVELOPE_CLOSE) {
            if (open_category < 0 || end < open_start) { truncated = 1; break; }
            start = open_start; open_category = -1;
        }
        if (count == capacity) {
            unsigned grown = capacity * 2;
            mr_timing_interval *next = intervals == local ? malloc(grown * sizeof *next) : realloc(intervals, grown * sizeof *next);
            if (!next) { truncated = 1; break; }
            if (intervals == local) memcpy(next, local, sizeof local);
            intervals = next; capacity = grown;
        }
        intervals[count++] = (mr_timing_interval){ start, end, plan->category[i] };
    }
    if (open_category >= 0) truncated = 1;
    os_unfair_lock_lock(&g_timing.lock);
    mr_timing_record *r = timing_record_for(frame);
    if (r) {
        if (!ok) r->failed = 1;
        if (truncated) r->truncated = 1;
        r->empty += empty;
        if (ok && gpu_end > gpu_start) {
            r->command_sum += gpu_end - gpu_start;
            if (!r->command_first || gpu_start < r->command_first) r->command_first = gpu_start;
            if (gpu_end > r->command_last) r->command_last = gpu_end;
        }
        if (gpu_now > r->gpu_now) { r->cpu_now = cpu_now; r->gpu_now = gpu_now; }
        if (count && r->count + count <= MR_TIMING_FRAME_INTERVALS) {
            if (r->count + count > r->capacity) {
                unsigned grown = r->capacity ? r->capacity : 256;
                while (grown < r->count + count) grown *= 2;
                mr_timing_interval *next = realloc(r->intervals, grown * sizeof *next);
                if (next) { r->intervals = next; r->capacity = grown; }
            }
            if (r->count + count <= r->capacity) { memcpy(r->intervals + r->count, intervals, count * sizeof *intervals); r->count += count; }
            else r->truncated = 1;
        } else if (count) r->truncated = 1;
        r->completed++;
        if (r->closed && r->completed == r->committed) timing_finalize(r);
    }
    os_unfair_lock_unlock(&g_timing.lock);
    if (intervals != local) free(intervals);
}

/* Before the pending command buffer is committed (engine thread). */
static void timing_command_buffer_committing(mr_shared *s, id<MTLCommandBuffer> cb) {
    int buffer = g_timing.buffer; g_timing.buffer = -1;
    if (g_timing.record < 0) return;
    uint64_t frame = g_timing.records[g_timing.record].frame;
    mr_timing_plan *plan = NULL;
    if (buffer >= 0) {
        mr_timing_buffer *b = &g_timing.buffers[buffer];
        plan = malloc(sizeof *plan);
        if (plan) {
            plan->count = b->count; memcpy(plan->category, b->category, b->count); memcpy(plan->kind, b->kind, b->count);
            atomic_store_explicit(&plan->published, 1, memory_order_release);
        }
    }
    os_unfair_lock_lock(&g_timing.lock);
    g_timing.records[g_timing.record].committed++;
    if (buffer >= 0 && !plan) g_timing.records[g_timing.record].truncated = 1;
    os_unfair_lock_unlock(&g_timing.lock);
    id<MTLDevice> device = s->dev;
    id<MTLCounterSampleBuffer> samples = buffer >= 0 ? g_timing.buffers[buffer].buffer : nil;
    [cb addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        @autoreleasepool {
        int ok = finished.status == MTLCommandBufferStatusCompleted;
        int planned = plan && atomic_load_explicit(&plan->published, memory_order_acquire);
        unsigned n = planned ? plan->count : 0;
        NSData *data = ok && samples && n ? [samples resolveCounterRange:NSMakeRange(0, (NSUInteger)n * 4)] : nil;
        MTLTimestamp cpu = 0, gpu = 0; [device sampleTimestamps:&cpu gpuTimestamp:&gpu];
        uint64_t stale = 0;
        if (buffer >= 0) {
            mr_timing_buffer *b = &g_timing.buffers[buffer];
            stale = atomic_load_explicit(&b->high_water, memory_order_acquire);
            uint64_t latest = stale;
            const uint64_t *v = data ? (const uint64_t *)data.bytes : NULL;
            for (size_t i = 0; v && i < data.length / sizeof *v; ++i) if (timing_valid_sample(v[i]) && v[i] > latest) latest = v[i];
            atomic_store_explicit(&b->high_water, latest, memory_order_release);
            atomic_store_explicit(&b->busy, 0, memory_order_release);
        }
        timing_command_completed(frame, planned ? plan : NULL, data ? (const uint64_t *)data.bytes : NULL,
                                 data ? data.length / sizeof(uint64_t) : 0, stale, ok,
                                 finished.GPUStartTime, finished.GPUEndTime, cpu, gpu);
        free(plan);
        }
    }];
}

/* End of a frame (engine thread). */
static void timing_frame_end(int published) {
    int record = g_timing.record;
    g_timing.record = -1; g_timing.frame++; g_timing.tainted = 0;
    if (record < 0) return;
    os_unfair_lock_lock(&g_timing.lock);
    mr_timing_record *r = &g_timing.records[record];
    r->closed = 1; r->published = published;
    if (!mr_gpu_timing_enabled()) r->incomplete = 1;
    if (r->completed == r->committed) timing_finalize(r);
    os_unfair_lock_unlock(&g_timing.lock);
}
void mr_gpu_timing_end_frame(int published) {
    /* Commit what is pending so the frame's last command buffer closes it. */
    if (g_shared && g_shared->pending_cb && g_timing.record >= 0) {
        mr_context frame = {0}; frame.s = g_shared;
        flush_pending(&frame, 0);
    }
    timing_frame_end(published);
}

static int timing_cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return x < y ? -1 : x > y;
}
static void timing_summary(const uint64_t *values, unsigned n, double *mean, double *p95, double *max) {
    *mean = *p95 = *max = 0;
    if (!n) return;
    uint64_t sorted[MR_TIMING_HISTORY]; memcpy(sorted, values, n * sizeof *values);
    qsort(sorted, n, sizeof *sorted, timing_cmp);
    double sum = 0; for (unsigned i = 0; i < n; ++i) sum += (double)sorted[i];
    *mean = sum / n / 1e6; *max = (double)sorted[n - 1] / 1e6;
    unsigned k = (unsigned)((n * 95 + 99) / 100); if (k) k--; if (k >= n) k = n - 1;
    *p95 = (double)sorted[k] / 1e6;
}
void mr_gpu_timing_read(mr_gpu_timing_stats *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->enabled = mr_gpu_timing_enabled();
    int capability = atomic_load_explicit(&g_timing.capability, memory_order_acquire);
    out->supported = capability == MR_TIMING_UNKNOWN ? -1 : capability == MR_TIMING_SUPPORTED;
    /* Copy under the lock, summarize outside it. */
    mr_gpu_timing_frame history[MR_TIMING_HISTORY];
    os_unfair_lock_lock(&g_timing.lock);
    out->frames_measured = g_timing.measured; out->frames_incomplete = g_timing.incomplete;
    out->frames_truncated = g_timing.truncated; out->frames_failed = g_timing.failed; out->empty_intervals = g_timing.empty;
    out->ns_per_tick = g_timing.ns_per_tick;
    unsigned n = g_timing.history_count, next = g_timing.history_next;
    memcpy(history, g_timing.history, sizeof history);
    os_unfair_lock_unlock(&g_timing.lock);
    out->window = n;
    uint64_t values[MR_TIMING_HISTORY];
    for (int c = 0; c < MR_GPU_CATEGORIES + 3; ++c) {
        for (unsigned i = 0; i < n; ++i) {
            const mr_gpu_timing_frame *f = &history[(next + MR_TIMING_HISTORY - 1 - i) % MR_TIMING_HISTORY];
            values[i] = c < MR_GPU_CATEGORIES ? f->category_ns[c] : c == MR_GPU_CATEGORIES ? f->busy_ns :
                        c == MR_GPU_CATEGORIES + 1 ? f->command_span_ns : f->command_sum_ns;
        }
        if (c < MR_GPU_CATEGORIES) timing_summary(values, n, &out->mean_ms[c], &out->p95_ms[c], &out->max_ms[c]);
        else if (c == MR_GPU_CATEGORIES) timing_summary(values, n, &out->busy_mean_ms, &out->busy_p95_ms, &out->busy_max_ms);
        else if (c == MR_GPU_CATEGORIES + 1) timing_summary(values, n, &out->command_span_mean_ms, &out->command_span_p95_ms, &out->command_span_max_ms);
        else { double p95, max; timing_summary(values, n, &out->command_sum_mean_ms, &p95, &max); }
    }
    if (n) out->last = history[(next + MR_TIMING_HISTORY - 1) % MR_TIMING_HISTORY];
    snprintf(out->status, sizeof out->status, "%s",
             out->supported == 0 ? "unsupported: no stage-boundary timestamps on this GPU" :
             !out->enabled ? "off" : out->supported < 0 ? "waiting for the renderer" :
             n ? "measuring completed frames" : "waiting for a completed frame");
}
void mr_gpu_timing_reset(void) {
    os_unfair_lock_lock(&g_timing.lock);
    g_timing.history_next = g_timing.history_count = 0;
    g_timing.measured = g_timing.incomplete = g_timing.truncated = g_timing.failed = g_timing.empty = 0;
    os_unfair_lock_unlock(&g_timing.lock);
}
