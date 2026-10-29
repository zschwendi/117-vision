/* The iPad native sizes on the real renderer (this Mac's GPU): every size the
 * mobile policy can pick, reached from the old default the way Halo's own
 * Video Setup reaches it (a live D3D Reset -> mr_resize), uploaded and read
 * back through the production row-pitch paths, plus the rejections that must
 * leave the context untouched. Prints SKIP without a Metal device.
 *
 * clang -O2 -fobjc-arc -fblocks tests/test_mobile_graphics_metal.m
 *   -framework Foundation -framework Metal -o /tmp/mobile-graphics-metal
 */
#include "../metalrenderer.m"
#include "../mobile_graphics.h"
#include <assert.h>

/* No shader is translated here; the renderer's built-in pipelines suffice. */
int ms_translate_shader(const uint32_t *tokens, size_t bytes, int stage, const char *entry, ms_shader *out) {
    (void)tokens; (void)bytes; (void)stage; (void)entry; memset(out, 0, sizeof *out); return -1;
}
int ms_translate_shader_mapped(const uint32_t *tokens, size_t bytes, int stage, const char *entry, const int *types, int count, ms_shader *out) {
    (void)types; (void)count; return ms_translate_shader(tokens, bytes, stage, entry, out);
}
void ms_shader_destroy(ms_shader *shader) { free(shader->source); memset(shader, 0, sizeof *shader); }

/* A pattern that differs in every row and column, so a wrong source or
 * destination row pitch cannot read back as a match. */
static uint32_t pattern(uint32_t x, uint32_t y, uint32_t seed) {
    return 0xFF000000u | ((x * 2654435761u ^ y * 40503u ^ seed) & 0x00FFFFFFu);
}

static void round_trip(mr_context *c, uint32_t w, uint32_t h, uint32_t seed) {
    size_t count = (size_t)w * h, bytes = count * 4;
    uint32_t *in = malloc(bytes), *out = malloc(bytes + 64);
    assert(in && out);
    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) in[(size_t)y * w + x] = pattern(x, y, seed);
    /* A short buffer is refused before anything is read or written. */
    assert(mr_write_framebuffer(c, in, bytes - 4) == MR_ERR_ARGS);
    memset(out, 0xA5, bytes + 64);
    assert(mr_read_framebuffer(c, out, bytes - 1) != 0);
    for (size_t i = 0; i < count + 16; i++) assert(out[i] == 0xA5A5A5A5u);
    /* Tight guest rows in, tight rows out, whatever the staging row pitch. */
    assert(mr_write_framebuffer(c, in, bytes) == MR_OK);
    assert(mr_read_framebuffer(c, out, bytes) == 0);
    assert(!memcmp(in, out, bytes));
    for (size_t i = count; i < count + 16; i++) assert(out[i] == 0xA5A5A5A5u);   /* nothing past the frame */
    free(in); free(out);
}

int main(void) { @autoreleasepool {
    if (!MTLCreateSystemDefaultDevice()) { puts("SKIP: no Metal device"); return 0; }
    /* Every native fit the policy can produce for real panels, the bound,
     * and the stored presets. 2416 x 4 = 9664 bytes is not a 256-byte row. */
    const int64_t displays[][2] = { {2752, 2064}, {2420, 1668}, {2732, 2048}, {2360, 1640}, {2266, 1488},
                                    {2868, 1320}, {5120, 3840}, {900, 1200}, {1500, 1100} };
    HaloMobileSize sizes[16]; unsigned n = 0;
    for (unsigned i = 0; i < sizeof displays / sizeof displays[0]; i++) {
        uint32_t flags = 0; assert(halo_mobile_native_fit(displays[i][0], displays[i][1], &sizes[n], &flags)); n++;
    }
    for (int i = 0; i < HALO_MOBILE_PRESET_COUNT; i++) sizes[n++] = halo_mobile_presets[i];
    uint32_t targets_before = 0; uint64_t bytes_before = mr_target_memory(&targets_before);
    mr_context *c = mr_create(HALO_MOBILE_DEFAULT_WIDTH, HALO_MOBILE_DEFAULT_HEIGHT); assert(c);
    uint32_t target_id = mr_target_texture(c);
    /* Measured attachment memory: Metal's own allocatedSize for both. */
    uint32_t targets = 0; uint64_t bytes = mr_target_memory(&targets);
    assert(targets == targets_before + 1 && bytes - bytes_before == (uint64_t)c->target.allocatedSize + c->depth_target.allocatedSize);
    assert(bytes - bytes_before >= 1600ull * 1200 * 4 + 1600ull * 1200 * 5);
    for (unsigned i = 0; i < n; i++) {
        uint32_t w = sizes[i].width, h = sizes[i].height;
        assert(mr_resize(c, (int)w, (int)h) == MR_OK);
        assert(mr_width(c) == (int)w && mr_height(c) == (int)h && mr_target_texture(c) == target_id);
        assert(c->target.width == w && c->depth_target.height == h);
        round_trip(c, w, h, i);
        bytes = mr_target_memory(&targets);
        assert(targets == targets_before + 1 && bytes - bytes_before == (uint64_t)c->target.allocatedSize + c->depth_target.allocatedSize);
        assert(bytes - bytes_before >= (uint64_t)w * h * 9u);   /* BGRA8 + 32-bit depth + 8-bit stencil at least */
        /* Halo's Sync toggle: a same-size Reset keeps the attachments. */
        id<MTLTexture> before = c->target;
        assert(mr_resize(c, (int)w, (int)h) == MR_OK && c->target == before);
        uint32_t *keep = malloc((size_t)w * h * 4); assert(keep);
        assert(mr_read_framebuffer(c, keep, (size_t)w * h * 4) == 0 && keep[0] == pattern(0, 0, i));
        free(keep);
        printf("size %ux%u: resize, same-size reset, %u-byte rows (staging %zu) round trip\n",
               w, h, w * 4, (((size_t)w * 4 + 255u) & ~(size_t)255u));
    }
    /* Malformed or oversized requests fail and leave the context as it was. */
    uint32_t w = (uint32_t)mr_width(c), h = (uint32_t)mr_height(c);
    id<MTLTexture> before = c->target;
    const int bad[][2] = { {0, 1200}, {1600, 0}, {-1600, 1200}, {8193, 1200}, {1600, 8193}, {INT32_MAX, INT32_MAX}, {INT32_MIN, 4} };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(mr_resize(c, bad[i][0], bad[i][1]) != MR_OK);
        assert(c->target == before && mr_width(c) == (int)w && mr_height(c) == (int)h);
    }
    assert(mr_create(8193, 8193) == NULL && mr_create(0, 480) == NULL && mr_create(-1, -1) == NULL);
    assert(mr_resize(NULL, 1600, 1200) != MR_OK);
    assert(mr_read_framebuffer(NULL, &w, 4) != 0 && mr_write_framebuffer(NULL, &w, 4) != MR_OK);
    assert(mr_read_framebuffer(c, NULL, (size_t)w * h * 4) != 0 && mr_write_framebuffer(c, NULL, (size_t)w * h * 4) != MR_OK);
    round_trip(c, w, h, 99);
    mr_context *second = mr_create(1280, 960); assert(second);
    assert(mr_target_memory(&targets) > bytes && targets == targets_before + 2);
    mr_destroy(second); mr_destroy(c);
    assert(mr_target_memory(&targets) == bytes_before && targets == targets_before);
    puts("PASS mobile graphics on Metal: native fits and presets resize from the default, same-size reset keeps targets, odd-pitch round trips exact, measured attachment memory follows create/resize/destroy, malformed sizes and short buffers rejected without side effects");
    return 0;
} }
