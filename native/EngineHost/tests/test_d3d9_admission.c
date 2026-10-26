/* Back-buffer admission for IDirect3D9::CreateDevice and IDirect3DDevice9::Reset
 * (d3d9_backbuffer_admitted). Build with -DHALO_D3D9_MOBILE_POLICY=1 for the
 * iPad/iPhone bound (mobile_graphics.h) and 0 for every other platform's
 * unchanged 64 ... 8192 bound. A refused call changes nothing: no device,
 * surface, guest page, renderer resize or presentation size. */
#include "../d3d9.c"
#include <assert.h>
void mr_destroy(mr_context *context){(void)context;abort();}
void mr_buffer_release(void *buffer){(void)buffer;abort();}
uint8_t *engine_flat_base;
void guest_free(uint32_t p){(void)p;abort();}
static uint32_t test_page_top = 0x100000u; static unsigned page_allocs;
int guest_page_free(uint32_t base) { (void)base; return 1; }
uint32_t guest_page_alloc(uint32_t size) {
    uint32_t result=test_page_top,need=(size+0xffffu)&~0xffffu;
    assert(size&&(uint64_t)test_page_top+need<0x20000000u);test_page_top+=need;page_allocs++;return result;
}
uint32_t guest_page_try_alloc(uint32_t size) { return guest_page_alloc(size); }
static int resize_calls; static int window_inits, window_w, window_h;
int mr_resize(mr_context *context,int width,int height){(void)context;(void)width;(void)height;resize_calls++;return 0;}
int metalwin_init(int width,int height,const char *title){(void)title;window_inits++;window_w=width;window_h=height;return 0;}
void host_log(const char *format, ...) { (void)format; }
static uint32_t heap_top = 0x00020000u;
uint32_t guest_alloc(uint32_t size) { uint32_t r = heap_top; heap_top += (size + 15u) & ~15u; assert(heap_top < 0x00080000u); return r; }
uint32_t host_proc_address(const char *dll, const char *name) { (void)dll; (void)name; return 0x00F00000u; }
_Noreturn void host_exit(int code) { (void)code; abort(); }

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
typedef struct { int objects; uint32_t bw, bh, pages, top, pp_w, pp_h; D3DObj *device; int inits; } Snapshot;
static Snapshot snap(void) { return (Snapshot){ obj_count, backbuffer_w, backbuffer_h, page_allocs, test_page_top, G32(PP), G32(PP + 4), the_device, window_inits }; }
static void unchanged(const Snapshot *a) {
    Snapshot b = snap();
    assert(a->objects == b.objects && a->bw == b.bw && a->bh == b.bh && a->pages == b.pages && a->top == b.top);
    assert(a->device == b.device && a->inits == b.inits);
}

int main(void) {
    engine_flat_base = calloc(1, 0x20000000u); assert(engine_flat_base);
    obj_count = 1;   /* slot 0 is never an object */
    /* Refused CreateDevice requests, including ones whose 32-bit surface
     * size would wrap (70000 x 70000 x 4 and 2^30 squared). */
    const uint32_t refused_everywhere[][2] = { {8193, 1200}, {1600, 8193}, {70000, 70000}, {0x40000000u, 0x40000000u},
                                               {0xFFFFFFFFu, 0xFFFFFFFFu}, {65536, 64} };
    for (unsigned i = 0; i < sizeof refused_everywhere / sizeof *refused_everywhere; i++) {
        Snapshot before = snap();
        assert(create_device(refused_everywhere[i][0], refused_everywhere[i][1]) == D3DERR_NOTAVAILABLE);
        unchanged(&before);
        assert(G32(OUT) == 0 && G32(PP) == refused_everywhere[i][0] && G32(PP + 4) == refused_everywhere[i][1]);
    }
    const uint32_t mobile_only[][2] = { {4096, 3072}, {3840, 2880}, {2881, 2160}, {2880, 2161}, {639, 480}, {640, 479} };
    for (unsigned i = 0; i < sizeof mobile_only / sizeof *mobile_only; i++) {
        uint32_t want = HALO_D3D9_MOBILE_POLICY ? D3DERR_NOTAVAILABLE : D3D_OK;
        assert(d3d9_backbuffer_admitted(mobile_only[i][0], mobile_only[i][1]) == !HALO_D3D9_MOBILE_POLICY);
        (void)want;
    }
    /* Halo's below-64 fallback is unchanged: 0 x 0 still means 640 x 480. */
    assert(d3d9_backbuffer_admitted(640, 480));
    /* Admitted: the native 13-inch fit creates the device and its surfaces. */
    Snapshot before = snap();
    assert(create_device(2752, 2064) == D3D_OK);
    assert(the_device && the_device != before.device && obj_count > before.objects && G32(OUT) == the_device->guest);
    assert(backbuffer_w == 2752 && backbuffer_h == 2064 && window_inits == 1 && window_w == 2752 && window_h == 2064);
    D3DObj *device = the_device, *backbuffer = obj_from_guest(device->data), *depth = obj_from_guest(device->level_surface[0]);
    assert(backbuffer && depth && backbuffer->size == 2752u * 2064u * 4u && depth->size == 2752u * 2064u * 4u);
    backbuffer->renderer = (mr_context *)(uintptr_t)0x1234u;

    /* Reset: refused sizes change nothing; admitted ones resize. */
    const uint32_t reset_refused[][2] = { {8193, 1536}, {2048, 8193}, {70000, 70000}, {0xFFFFFFFFu, 2}, {63, 480} };
    for (unsigned i = 0; i < sizeof reset_refused / sizeof *reset_refused; i++) {
        uint32_t data = backbuffer->data, size = backbuffer->size; int calls = resize_calls; Snapshot b = snap();
        S32(PP, reset_refused[i][0]); S32(PP + 4, reset_refused[i][1]);
        assert(reset_device(device, PP) == D3DERR_INVALIDCALL);
        assert(backbuffer->data == data && backbuffer->size == size && resize_calls == calls && backbuffer_w == 2752);
        unchanged(&b);
    }
    for (unsigned i = 0; i < sizeof mobile_only / sizeof *mobile_only; i++) {
        uint32_t w = mobile_only[i][0], h = mobile_only[i][1]; int calls = resize_calls;
        S32(PP, w); S32(PP + 4, h); S32(PP + 8, 0); S32(PP + 40, 75);
        uint32_t hr = reset_device(device, PP);
        if (HALO_D3D9_MOBILE_POLICY) { assert(hr == D3DERR_INVALIDCALL && resize_calls == calls && backbuffer_w == 2752); }
        else { assert(hr == D3D_OK && backbuffer_w == w && backbuffer->size == w * h * 4u);
               S32(PP, 2752); S32(PP + 4, 2064); assert(reset_device(device, PP) == D3D_OK); }
    }
    /* Halo's own Video Setup modes all pass on every platform. */
    for (unsigned i = 0; i < N(modes); i++) {
        S32(PP, modes[i][0]); S32(PP + 4, modes[i][1]); S32(PP + 8, 0);
        assert(reset_device(device, PP) == D3D_OK && backbuffer_w == modes[i][0] && backbuffer_h == modes[i][1]);
    }
    S32(PP, 2752); S32(PP + 4, 2064); assert(reset_device(device, PP) == D3D_OK && backbuffer->size == 2752u * 2064u * 4u);
    free(engine_flat_base);
    printf("PASS d3d9 admission (mobile policy %d): CreateDevice/Reset refuse wrapping and out-of-bound sizes without "
           "touching device, surfaces, pages or renderer; admitted sizes and every listed mode create and reset\n", HALO_D3D9_MOBILE_POLICY);
    return 0;
}
