/* Transactional IDirect3D9::CreateDevice and IDirect3DDevice9::Reset: the
 * implicit back buffer and depth surface are staged with guest_page_try_alloc
 * first, the renderer's targets are replaced next (Reset), and only then is
 * anything published. Deterministic failure of the first or second staged
 * allocation, and of the Metal resize, must return an error, free every
 * staged page, and leave the previous device, mode, surfaces, storage,
 * contents, bindings and renderer usable. guest_page_alloc (fatal) is never
 * reached on these paths. */
#include "../d3d9.c"
#include <assert.h>
void mr_destroy(mr_context *context){(void)context;abort();}
void mr_buffer_release(void *buffer){(void)buffer;abort();}
uint8_t *engine_flat_base;
void guest_free(uint32_t p){(void)p;abort();}
void host_log(const char *format, ...) { (void)format; }
static uint32_t heap_top = 0x00020000u;
uint32_t guest_alloc(uint32_t size) { uint32_t r = heap_top; heap_top += (size + 15u) & ~15u; assert(heap_top < 0x00080000u); return r; }
uint32_t host_proc_address(const char *dll, const char *name) { (void)dll; (void)name; return 0x00F00000u; }
_Noreturn void host_exit(int code) { (void)code; abort(); }

/* A tracking page allocator: every live allocation is known, so a staged
 * page that is not freed on failure is a counted leak. */
enum { MEMORY = 0x20000000u, PAGE_BASE = 0x01000000u };
static struct { uint32_t base, size; int live; } pages[64]; static unsigned page_records;
static uint32_t page_top = PAGE_BASE; static int try_calls, fail_try_call;
uint32_t guest_page_try_alloc(uint32_t size) {
    try_calls++;
    if (fail_try_call && try_calls == fail_try_call) return 0;
    uint32_t base = page_top, need = (size + 0xffffu) & ~0xffffu;
    assert(size && (uint64_t)page_top + need < MEMORY && page_records < 64);
    page_top += need; memset(GPTR(base), 0, need);
    pages[page_records].base = base; pages[page_records].size = need; pages[page_records].live = 1; page_records++;
    return base;
}
uint32_t guest_page_alloc(uint32_t size) { (void)size; abort(); }   /* the fatal entry is never used here */
int guest_page_free(uint32_t base) {
    for (unsigned i = 0; i < page_records; i++) if (pages[i].base == base && pages[i].live) { pages[i].live = 0; return 1; }
    abort();   /* freeing storage that is not a live allocation */
}
static unsigned live_pages(void) { unsigned n = 0; for (unsigned i = 0; i < page_records; i++) n += pages[i].live; return n; }
static int page_live(uint32_t base) { for (unsigned i = 0; i < page_records; i++) if (pages[i].base == base) return pages[i].live; return 0; }

static int resize_calls, resize_fail, resized_w, resized_h; static int window_inits;
int mr_resize(mr_context *context, int width, int height) {
    assert(context == (mr_context *)(uintptr_t)0x1234u); resize_calls++;
    if (resize_fail) return 1;   /* make_target keeps the old attachments on failure */
    resized_w = width; resized_h = height; return 0;
}
int metalwin_init(int width, int height, const char *title) { (void)width; (void)height; (void)title; window_inits++; return 0; }

enum { STACK = 0x8000, PP = 0x9000, OUT = 0x9100, RETURN_PC = 0x12345678 };
static uint32_t create_device(uint32_t w, uint32_t h) {
    EngineCPU cpu = {0}; cpu.gpr[4] = STACK; S32(STACK, RETURN_PC);
    for (int i = 0; i < 7; i++) S32(STACK + 4 + 4 * i, 0);
    S32(STACK + 4 + 4 * 5, PP); S32(STACK + 4 + 4 * 6, OUT);
    memset(GPTR(PP), 0, 64); S32(PP, w); S32(PP + 4, h); S32(PP + 40, 75); S32(OUT, 0xA5A5A5A5u);
    method_d3d(&cpu, NULL, 16);
    assert(cpu.pc == RETURN_PC && cpu.gpr[4] == STACK + 4 + 4 * 7);
    return cpu.gpr[0];
}
static void inject(int nth) { try_calls = 0; fail_try_call = nth; }

/* Everything a failed Reset must leave exactly as it was. */
typedef struct {
    uint32_t data, depth_data, size, depth_size, capacity, depth_capacity, width, height, depth_w, depth_h, format;
    uint32_t bw, bh, rt, ds, viewport_w, viewport_h, pp8; unsigned live; int resizes; mr_context *renderer; uint32_t pattern[4];
} ResetState;
static D3DObj *device, *backbuffer, *depth;
static ResetState capture(void) {
    ResetState r = { backbuffer->data, depth->data, backbuffer->size, depth->size, backbuffer->data_capacity, depth->data_capacity,
                     backbuffer->width, backbuffer->height, depth->width, depth->height, backbuffer->format,
                     backbuffer_w, backbuffer_h, current_rt_guest, current_depth_guest, draw_state.viewport[2], draw_state.viewport[3],
                     0 /* pp8: checked directly */, live_pages(), resize_calls, backbuffer->renderer, {0} };
    for (int i = 0; i < 4; i++) r.pattern[i] = G32(backbuffer->data + (backbuffer->size / 4u) * (uint32_t)i);
    return r;
}
static void unchanged(const ResetState *a) {
    ResetState b = capture();
    assert(!memcmp(a, &b, offsetof(ResetState, resizes)) || (fprintf(stderr, "state moved\n"), 0));
    assert(a->renderer == b.renderer && !memcmp(a->pattern, b.pattern, sizeof a->pattern));
    assert(page_live(a->data) && page_live(a->depth_data));   /* the old storage is still owned and usable */
}
static void paint(void) { for (int i = 0; i < 4; i++) S32(backbuffer->data + (backbuffer->size / 4u) * (uint32_t)i, 0xC0DE0000u + (uint32_t)i); }
static uint32_t reset_to(uint32_t w, uint32_t h) { S32(PP, w); S32(PP + 4, h); S32(PP + 8, 0); S32(PP + 40, 75); return reset_device(device, PP); }

int main(void) {
    engine_flat_base = calloc(1, MEMORY); assert(engine_flat_base);
    obj_count = 1;
    unsigned checked = 0;

    /* CreateDevice: first and second staged allocations fail. */
    for (int nth = 1; nth <= 2; nth++) {
        int objects = obj_count; unsigned live = live_pages();
        inject(nth);
        assert(create_device(1600, 1200) == D3DERR_OUTOFVIDEOMEMORY);
        assert(try_calls == nth && obj_count == objects && the_device == NULL && live_pages() == live);
        assert(G32(OUT) == 0 && G32(PP) == 1600 && G32(PP + 4) == 1200 && backbuffer_w == 640 && window_inits == 0);
        checked++;
    }
    inject(0);
    assert(create_device(1600, 1200) == D3D_OK && the_device && live_pages() == 2 && window_inits == 1);
    device = the_device; backbuffer = obj_from_guest(device->data); depth = obj_from_guest(device->level_surface[0]);
    assert(backbuffer && depth && backbuffer->size == 1600u * 1200u * 4u && page_live(backbuffer->data) && page_live(depth->data));
    backbuffer->renderer = (mr_context *)(uintptr_t)0x1234u;
    paint();

    /* Reset to a larger mode: the first staged allocation (back buffer)
     * fails, then the second (depth), then the Metal resize. */
    for (int step = 0; step < 3; step++) {
        ResetState before = capture();
        inject(step < 2 ? step + 1 : 0); resize_fail = step == 2;
        uint32_t hr = reset_to(2048, 1536);
        assert(hr == (step == 2 ? D3DERR_NOTAVAILABLE : D3DERR_OUTOFVIDEOMEMORY));
        assert(try_calls == (step == 0 ? 1 : 2));
        assert(resize_calls == before.resizes + (step == 2));   /* Metal is never touched before staging succeeds */
        unchanged(&before);                                      /* staged pages freed: live count is unchanged */
        assert(G32(PP) == 2048 && G32(PP + 4) == 1536 && G32(PP + 8) == 0);   /* the caller's parameters are not rewritten */
        checked++;
    }
    resize_fail = 0; inject(0);
    /* The previous mode is still fully usable: a same-size Reset reuses its
     * storage without allocating, and a real resize then succeeds. */
    {
        uint32_t old = backbuffer->data;
        assert(reset_to(1600, 1200) == D3D_OK && try_calls == 0 && backbuffer->data == old && live_pages() == 2);
        paint();
        uint32_t old_depth = depth->data;
        assert(reset_to(2048, 1536) == D3D_OK && try_calls == 2 && resized_w == 2048 && resized_h == 1536);
        assert(!page_live(old) && !page_live(old_depth) && live_pages() == 2);    /* replaced storage released */
        assert(backbuffer->size == 2048u * 1536u * 4u && backbuffer_w == 2048 && G32(backbuffer->data) == 0);
        assert(current_rt_guest == device->data && draw_state.viewport[2] == 2048 && G32(PP + 8) == FMT_X8R8G8B8);
        checked++;
    }
    /* Shrinking reuses the larger storage: no allocation can fail it. */
    {
        uint32_t data = backbuffer->data; inject(1);
        assert(reset_to(1280, 960) == D3D_OK && try_calls == 0 && backbuffer->data == data && backbuffer->data_capacity == 2048u * 1536u * 4u);
        assert(backbuffer->size == 1280u * 960u * 4u && live_pages() == 2);
        checked++;
    }
    /* Growing back past the retained capacity stages again; its failure
     * keeps the 1280x960 mode on the retained storage. */
    {
        paint(); ResetState before = capture(); inject(2);
        assert(reset_to(2560, 1920) == D3DERR_OUTOFVIDEOMEMORY);
        unchanged(&before);
        inject(0); assert(reset_to(2560, 1920) == D3D_OK && live_pages() == 2 && backbuffer_w == 2560);
        checked++;
    }
    /* Out-of-bound Resets are refused before any staging. */
    { ResetState before = capture(); inject(0); assert(reset_to(8193, 1200) == D3DERR_INVALIDCALL && try_calls == 0); unchanged(&before); checked++; }
    for (unsigned i = 0; i < page_records; i++) if (pages[i].live) assert(pages[i].base == backbuffer->data || pages[i].base == depth->data);
    free(engine_flat_base);
    printf("PASS d3d9 transactions: CreateDevice first/second staged allocation failures and Reset first/second allocation and Metal "
           "failures return errors, free every staged page and keep the previous mode, storage, contents, bindings and renderer usable "
           "(%u cases); success publishes and releases replaced storage; shrink reuses storage\n", checked);
    return 0;
}
