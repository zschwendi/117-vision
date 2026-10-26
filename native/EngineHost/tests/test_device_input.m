/* Hardware keyboard/mouse/touch source (device_input.inc) through the actual
 * native app lease (gamecontroller.m) and DirectInput keyboard/mouse devices
 * (dinput8.c). Controller hardware is replaced by injected snapshots and time
 * by a test clock; no app, device or simulator is involved.
 * clang -O2 -fobjc-arc -fblocks -I native/EngineHost -I native/EngineReuse -lm \
 *   native/EngineHost/tests/test_device_input.m -framework Foundation \
 *   -framework CoreHaptics -framework GameController -o /tmp/device-input */
#import <Foundation/Foundation.h>
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>
static uint64_t test_ns = UINT64_C(1000000000);
static uint64_t test_uptime(clockid_t clock) { (void)clock; return test_ns; }
static int test_monotonic(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec=test_ns/1000000000; out->tv_nsec=test_ns%1000000000; return 0;
}
#define clock_gettime_nsec_np test_uptime
#include "../gamecontroller.m"
#undef clock_gettime_nsec_np
#define clock_gettime test_monotonic
#include "../halo_settings.c"
#undef clock_gettime
#define HOST_DEVICE_INPUT_TESTING 1 /* the fence-after-snapshot hook only */
#include "../dinput8.c"
uint8_t *engine_flat_base;
static uint32_t heap_next=0x10000;
void host_log(const char *fmt,...) {(void)fmt;}
int halo_haptics_enabled(void) {return 1;}
_Noreturn void host_exit(int code) {(void)code;abort();}
uint32_t guest_alloc(uint32_t size) {uint32_t p=heap_next;heap_next+=(size+15)&~15u;assert(heap_next<0x100000);return p;}
void guest_free(uint32_t p) {(void)p;}
uint32_t host_proc_address(const char *a,const char *b) {(void)a;(void)b;return 0;}
uint64_t host_monotonic_ns(void) {return test_ns;}
uint32_t host_call_guest(EngineCPU *c,uint32_t fn,int n,const uint32_t *args,int pops) {(void)c;(void)fn;(void)n;(void)args;(void)pops;return 0;}

static unsigned checks;
#define CHECK(x) do {checks++; if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort();}}while(0)
static uint32_t call(HostShim fn,unsigned count,...) {
    EngineCPU cpu={0};cpu.gpr[4]=0x1000;S32(0x1000,0x1234);
    va_list a;va_start(a,count);for(unsigned i=0;i<count;i++)S32(0x1004+4*i,va_arg(a,uint32_t));va_end(a);
    fn(&cpu);CHECK(cpu.pc==0x1234);return cpu.gpr[0];
}
static HostGCSnapshot pad;
static void inject(void) {hostgc_inject_test_snapshot(&pad);HostGCSnapshot raw;hostgc_poll(&raw);}
static void no_controller(void) {pad=(HostGCSnapshot){0};inject();}
static void neutral_controller(void) {
    pad=(HostGCSnapshot){.connected=true,.source_generation=hostgc_source_generation()};test_ns+=1000000;inject();
}
/* HID usages (UIKeyboardHIDUsage / GCKeyCode) used below. */
enum { U_A=0x04,U_D=0x07,U_E=0x08,U_S=0x16,U_W=0x1A,U_PERIOD=0x37,U_SPACE=0x2C,U_LCTRL=0xE0 };
enum { KB_GC=HOST_DEVICE_KEYBOARD_GC, KB_UI=HOST_DEVICE_KEYBOARD_UIKIT, PT_GC=HOST_DEVICE_POINTER_GC,
       PT_UI=HOST_DEVICE_POINTER_UIKIT, TOUCH=HOST_DEVICE_TOUCH };

static DIObj *keyboard, *mouse;
static void open_devices(void) {
    keyboard=obj_new(DEV_KEYBOARD,0);CHECK(keyboard);keyboard->buffer_size=32;
    CHECK(call(shim_dev_7,1,keyboard->guest)==DI_OK);
    mouse=obj_new(DEV_MOUSE,0);CHECK(mouse);CHECK(call(shim_dev_7,1,mouse->guest)==DI_OK);
}
/* Halo's immediate keyboard read: GetDeviceState(256). */
static uint8_t key_state(uint8_t dik) {
    CHECK(call(shim_dev_9,3,keyboard->guest,256u,0x4000u)==DI_OK);return G8(0x4000u+dik);
}
/* Halo's buffered keyboard read: GetDeviceData; returns the event count. */
static unsigned key_events(uint32_t ofs[8],uint32_t data[8]) {
    S32(0x5800,8);CHECK(call(shim_dev_10,5,keyboard->guest,16u,0x5000u,0x5800u,0u)==DI_OK);
    unsigned n=G32(0x5800);CHECK(n<=8);
    for(unsigned i=0;i<n;i++){ofs[i]=G32(0x5000+16*i);data[i]=G32(0x5000+16*i+4);}
    return n;
}
static void drain_keys(void) {uint32_t o[8],d[8];while(key_events(o,d)){}}
/* Halo's actual drain: GetDeviceData for ONE event per call (00490760). */
static int key_event_one(uint32_t *ofs,uint32_t *data) {
    S32(0x5800,1);CHECK(call(shim_dev_10,5,keyboard->guest,16u,0x5000u,0x5800u,0u)==DI_OK);
    if(!G32(0x5800))return 0;*ofs=G32(0x5000);*data=G32(0x5004);return 1;
}
typedef struct { int32_t dx,dy,dz; uint8_t b[4]; } MouseRead;
/* Halo's mouse read: GetDeviceState(DIMOUSESTATE, 16 bytes). */
static MouseRead mouse_state(void) {
    CHECK(call(shim_dev_9,3,mouse->guest,16u,0x3000u)==DI_OK);
    MouseRead r={(int32_t)G32(0x3000),(int32_t)G32(0x3004),(int32_t)G32(0x3008),{G8(0x300C),G8(0x300D),G8(0x300E),G8(0x300F)}};
    return r;
}
static void game(int owns) {host_device_input_set_game_owns(owns);}
/* Original game-time structure device_clock() reads: *(0x006F1D6C)+12. */
enum { GAME_TIME = 0x200000 };
static void gameplay_clock(int on) {S32(0x6F1D6C,on?GAME_TIME:0);if(on)S32(GAME_TIME+12,1000);}
static void game_tick(void) {S32(GAME_TIME+12,G32(GAME_TIME+12)+1);}
/* Let short presses finish in any mode: ticks, wall time and input pumps. */
static void settle_all(void) {
    for(int i=0;i<4;i++){if(G32(0x6F1D6C))game_tick();test_ns+=HOST_DEVICE_MENU_MIN_NS;key_state(0);mouse_state();}
}

static void mapping(void) {
    /* The DIK codes the existing PAD2KEY bridge emits for Halo's default PC
     * bindings (dinput8.c keyboard_base_state, d3d9.c Present). */
    static const struct { uint32_t hid; uint8_t dik; } expected[] = {
        {0x1A,0x11},{0x16,0x1F},{0x04,0x1E},{0x07,0x20}, /* W S A D */
        {0x2C,0x39},{0xE0,0x1D},{0x08,0x12},{0x15,0x13}, /* Space LCtrl E R */
        {0x09,0x21},{0x0A,0x22},{0x14,0x10},{0x1D,0x2C}, /* F G Q Z */
        {0x2B,0x0F},{0x1B,0x2D},{0x3A,0x3B},{0x29,0x01}, /* Tab X F1 Esc */
        {0x28,0x1C},{0x52,0xC8},{0x51,0xD0},{0x50,0xCB},{0x4F,0xCD}, /* Return arrows */
        {0xE1,0x2A},{0xE5,0x36},{0xE2,0x38},{0xE4,0x9D},{0x1E,0x02},{0x27,0x0B},{0x45,0x58},
    };
    for(unsigned i=0;i<sizeof expected/sizeof expected[0];i++)CHECK(host_device_dik_for_hid(expected[i].hid)==expected[i].dik);
    CHECK(host_device_dik_for_hid(0xE3)==0&&host_device_dik_for_hid(0xE7)==0); /* Command */
    CHECK(host_device_dik_for_hid(0)==0&&host_device_dik_for_hid(0x300)==0&&host_device_dik_for_hid(UINT32_MAX)==0);
    unsigned seen[256]={0},mapped=0;
    for(uint32_t u=0;u<256;u++){uint8_t d=host_device_dik_for_hid(u);if(d){seen[d]++;mapped++;}}
    for(unsigned d=0;d<256;d++)CHECK(seen[d]<=1||d==0x2B); /* only \ and non-US # share Backslash */
    CHECK(mapped>=100);
}

static void ownership_and_sources(void) {
    no_controller();
    HostDeviceInputStats st;
    game(0);host_device_input_select(KB_GC,PT_GC);
    CHECK(!host_device_input_key(KB_GC,U_W,1,0));CHECK(!key_state(0x11)); /* app owns: nothing reaches Halo */
    game(1);
    CHECK(!host_device_input_key(KB_UI,U_W,1,0)); /* unselected source rejected */
    CHECK(host_device_input_key(KB_GC,U_W,1,0));CHECK(!host_device_input_key(KB_GC,U_W,1,0)); /* duplicate down */
    CHECK(!host_device_input_key(KB_UI,U_W,1,0));CHECK(key_state(0x11)==0x80);
    /* Switching sources is a fence; the old source's release is then stale. */
    uint64_t g=host_device_input_generation();
    host_device_input_select(KB_UI,PT_GC);CHECK(host_device_input_generation()!=g);CHECK(!key_state(0x11));
    CHECK(!host_device_input_key(KB_GC,U_W,0,0));CHECK(!host_device_input_key(KB_UI,U_W,0,0));
    CHECK(host_device_input_key(KB_UI,U_D,1,0));CHECK(key_state(0x20)==0x80);
    CHECK(host_device_input_key(KB_UI,U_D,0,0));settle_all();CHECK(!key_state(0x20));
    host_device_input_select(KB_GC,PT_GC);
    /* Native lease: new hardware ingress is refused while a token is held,
     * even though the app flag still says the game owns input. */
    uint64_t t=hostgc_app_input_begin();CHECK(hostgc_app_input_lease_active());
    CHECK(!host_device_input_key(KB_GC,U_W,1,0));CHECK(!host_device_input_motion(PT_GC,5,5));
    CHECK(!host_device_input_click(TOUCH,0));CHECK(!key_state(0x11));
    /* Legacy physical keyboard globals are untouched and still pass, as before. */
    uint8_t keys[256];host_keyboard_state[0x20]=0x80;host_dinput_keyboard_state(keys);CHECK(keys[0x20]==0x80);host_keyboard_state[0x20]=0;
    hostgc_app_input_end(t);CHECK(!hostgc_app_input_lease_active());
    /* Keyboard-only user: with no controller the pad handback gate stays
     * closed indefinitely, but it does not hold the hardware source. */
    no_controller();test_ns+=1000000;inject();
    CHECK(hostgc_app_input_blocked());
    CHECK(host_device_input_key(KB_GC,U_W,1,0));CHECK(key_state(0x11)==0x80);
    drain_keys();
    uint32_t o[8],d[8];CHECK(host_device_input_key(KB_GC,U_SPACE,1,0));
    unsigned n=key_events(o,d);CHECK(n==1&&o[0]==0x39&&d[0]==0x80); /* not pruned by the closed pad gate */
    /* Keyboard activity and keyboard neutral never clear the controller gate. */
    CHECK(host_device_input_key(KB_GC,U_SPACE,0,0));CHECK(host_device_input_key(KB_GC,U_W,0,0));
    settle_all();
    host_device_input_release();CHECK(hostgc_app_input_blocked());
    neutral_controller();CHECK(!hostgc_app_input_blocked()); /* only a fresh neutral controller does */
    no_controller();
    /* The lease racing ahead of the app's own fence: the engine read fences. */
    CHECK(host_device_input_key(KB_GC,U_W,1,0));CHECK(host_device_input_button(PT_GC,0,1));
    CHECK(host_device_input_motion(PT_GC,40,0));
    g=host_device_input_generation();t=hostgc_app_input_begin();
    CHECK(!key_state(0x11));MouseRead m=mouse_state();CHECK(m.dx==0&&m.b[0]==0);
    host_device_input_stats(&st);CHECK(st.keys_down==0&&st.buttons_down==0&&st.generation!=g);
    hostgc_app_input_end(t);CHECK(!key_state(0x11)); /* no resume after the lease: press again */
    CHECK(!host_device_input_key(KB_GC,U_W,0,0)); /* stale release */
    m=mouse_state();CHECK(m.b[0]==0&&m.dx==0);
    /* Lease expiry (app stalled) leaves the app's own flag in charge. */
    t=hostgc_app_input_begin();game(0);test_ns+=600000000;CHECK(!hostgc_app_input_lease_active());
    CHECK(!host_device_input_key(KB_GC,U_W,1,0));game(1);hostgc_app_input_end(t);
    drain_keys();
}

/* ---- Delivery timing -------------------------------------------------------
 * Model of the traced consumer: each rendered frame performs one DirectInput
 * read (mouse GetDeviceState; keyboard GetDeviceData drained), then runs the
 * game ticks due by then; each tick sees the state that frame's read left in
 * the control record. The tick counter is the original game-time structure
 * (*(0x006F1D6C)+12) that device_clock() reads. */
typedef struct { unsigned frames, ticks, rising[2], ticks_down[2]; int last[2]; } TickTrace;
/* One rendered frame at `hz`: mouse read, then the 30 Hz ticks due. */
static void frame(TickTrace *t, unsigned hz) {
    MouseRead m=mouse_state();
    unsigned before=t->frames*30u/hz; t->frames++; unsigned after=t->frames*30u/hz;
    for(unsigned k=before;k<after;k++){
        for(int b=0;b<2;b++){int down=m.b[b]==0x80;if(down&&!t->last[b])t->rising[b]++;if(down)t->ticks_down[b]++;t->last[b]=down;}
        t->ticks++;game_tick();
    }
}
static void settle(TickTrace *t, unsigned hz) {for(int i=0;i<4*(int)hz/30+4;i++)frame(t,hz);}

static void click_delivery(void) {
    no_controller();game(1);host_device_input_select(KB_GC,PT_GC);gameplay_clock(1);
    static const unsigned rates[]={30,60,90,120,240};
    for(unsigned r=0;r<5;r++) for(unsigned phase=0;phase<rates[r]/30;phase++) {
        unsigned hz=rates[r];TickTrace t={0};settle(&t,hz);t=(TickTrace){0};
        for(unsigned i=0;i<phase;i++)frame(&t,hz);
        /* One direct-touch tap: exactly one primary press seen by the
         * simulation, never secondary. */
        CHECK(host_device_input_click(TOUCH,HOST_DEVICE_BUTTON_PRIMARY));settle(&t,hz);
        CHECK(t.rising[0]==1&&t.ticks_down[0]>=1&&t.rising[1]==0&&!t.last[0]);
        /* Two rapid taps queued before any read: two distinct presses. */
        t=(TickTrace){0};
        CHECK(host_device_input_click(TOUCH,0)&&host_device_input_click(TOUCH,0));settle(&t,hz);settle(&t,hz);
        CHECK(t.rising[0]==2&&t.rising[1]==0);
        /* A two-finger tap: exactly one secondary (grenade) press, no fire. */
        t=(TickTrace){0};CHECK(host_device_input_click(TOUCH,HOST_DEVICE_BUTTON_SECONDARY));settle(&t,hz);
        CHECK(t.rising[1]==1&&t.rising[0]==0);
        /* Hardware click entirely between two reads, then a quick second
         * click: two presses, each seen by a tick. */
        t=(TickTrace){0};
        CHECK(host_device_input_button(PT_GC,0,1)&&host_device_input_button(PT_GC,0,0));frame(&t,hz);
        CHECK(host_device_input_button(PT_GC,0,1)&&host_device_input_button(PT_GC,0,0));settle(&t,hz);settle(&t,hz);
        CHECK(t.rising[0]==2);
        /* Held primary: continuous fire for every tick until released. */
        t=(TickTrace){0};CHECK(host_device_input_button(PT_GC,0,1));
        for(unsigned i=0;i<hz;i++)frame(&t,hz);
        CHECK(t.rising[0]==1&&t.ticks_down[0]>=t.ticks-1&&t.ticks>=29);
        /* Release, then re-press within the same tick interval: a release
         * tick separates them. */
        CHECK(host_device_input_button(PT_GC,0,0));frame(&t,hz);CHECK(host_device_input_button(PT_GC,0,1));
        settle(&t,hz);CHECK(t.rising[0]==2&&t.last[0]);
        CHECK(host_device_input_button(PT_GC,0,0));settle(&t,hz);CHECK(!t.last[0]);
        /* A touch tap never releases a held mouse button: tap before the
         * press's first tick, and tap after it, are both absorbed. */
        t=(TickTrace){0};CHECK(host_device_input_button(PT_GC,0,1));
        CHECK(host_device_input_click(TOUCH,0));for(unsigned i=0;i<hz/5+2;i++)frame(&t,hz);
        CHECK(host_device_input_click(TOUCH,0));for(unsigned i=0;i<hz/5+2;i++)frame(&t,hz);
        CHECK(t.rising[0]==1&&t.last[0]&&t.ticks_down[0]==t.ticks);
        /* A tap while the release is still being shown: a separate press after
         * the release has crossed a tick. */
        CHECK(host_device_input_button(PT_GC,0,0));frame(&t,hz);
        CHECK(host_device_input_click(TOUCH,0));settle(&t,hz);settle(&t,hz);
        CHECK(t.rising[0]==2&&!t.last[0]&&t.rising[1]==0);
    }
    /* The queue is bounded; overflow is refused, never wraps. */
    for(int i=0;i<HOST_DEVICE_PULSE_QUEUE;i++)CHECK(host_device_input_click(TOUCH,0));
    CHECK(!host_device_input_click(TOUCH,0));
    TickTrace t={0};for(int i=0;i<4;i++)settle(&t,60);CHECK(t.rising[0]==HOST_DEVICE_PULSE_QUEUE);
    /* Native menus (pause/title): game time may stop, so each phase spans
     * input pumps (mouse reads) plus a minimum time, never ticks; it cannot
     * stick, and queued clicks come out spaced, not as a burst on resume. */
    S16(0x718FA6,1);
    CHECK(host_device_input_click(TOUCH,0));
    MouseRead m=mouse_state();CHECK(m.b[0]==0x80);
    for(int i=0;i<6;i++){m=mouse_state();CHECK(m.b[0]==0x80);} /* pumps alone do not end it */
    test_ns+=HOST_DEVICE_MENU_MIN_NS;m=mouse_state();CHECK(m.b[0]==0);
    test_ns+=HOST_DEVICE_MENU_MIN_NS;m=mouse_state();m=mouse_state();CHECK(m.b[0]==0);
    for(int i=0;i<HOST_DEVICE_PULSE_QUEUE;i++)CHECK(host_device_input_click(TOUCH,0));
    unsigned edges=0;int last=0;
    for(int i=0;i<40;i++){test_ns+=HOST_DEVICE_MENU_MIN_NS/2;m=mouse_state();int down=m.b[0]==0x80;if(down&&!last)edges++;last=down;}
    CHECK(edges==HOST_DEVICE_PULSE_QUEUE&&!last);
    S16(0x718FA6,0);
    t=(TickTrace){0};for(int i=0;i<4;i++)settle(&t,60);CHECK(t.rising[0]==0); /* nothing left for gameplay */
    /* No valid game clock outside menus (startup, loading): same fallback. */
    gameplay_clock(0);CHECK(host_device_input_click(TOUCH,1));
    m=mouse_state();CHECK(m.b[1]==0x80);m=mouse_state();CHECK(m.b[1]==0x80);
    test_ns+=HOST_DEVICE_MENU_MIN_NS;m=mouse_state();CHECK(m.b[1]==0);
    test_ns+=HOST_DEVICE_MENU_MIN_NS;mouse_state();mouse_state();gameplay_clock(1);
    /* A tick jump (or a replaced clock with another tick) ends the phase at
     * once; a replaced clock showing the same tick is bounded in time. */
    CHECK(host_device_input_click(TOUCH,0));m=mouse_state();CHECK(m.b[0]==0x80);
    S32(GAME_TIME+12,G32(GAME_TIME+12)+500);m=mouse_state();CHECK(m.b[0]==0);
    game_tick();mouse_state();
    CHECK(host_device_input_click(TOUCH,0));m=mouse_state();CHECK(m.b[0]==0x80);
    uint32_t same=G32(GAME_TIME+12);S32(0x6F1D6C,GAME_TIME+0x100);S32(GAME_TIME+0x10C,same);
    for(int i=0;i<5;i++){m=mouse_state();CHECK(m.b[0]==0x80);}
    test_ns+=HOST_DEVICE_PULSE_MAX_NS;m=mouse_state();CHECK(m.b[0]==0);
    gameplay_clock(1);test_ns+=HOST_DEVICE_PULSE_MAX_NS;mouse_state();mouse_state();
    /* A stalled tick outside menus (loading) is bounded in wall time. */
    CHECK(host_device_input_click(TOUCH,0));m=mouse_state();CHECK(m.b[0]==0x80);
    for(int i=0;i<10;i++){m=mouse_state();CHECK(m.b[0]==0x80);}
    test_ns+=HOST_DEVICE_PULSE_MAX_NS;m=mouse_state();CHECK(m.b[0]==0);
    test_ns+=HOST_DEVICE_PULSE_MAX_NS;m=mouse_state();CHECK(m.b[0]==0);
    /* No buffered mouse route: GetDeviceData always reports zero events, so
     * secondary/grenade has exactly one route (immediate state). */
    CHECK(host_device_input_click(TOUCH,1));S32(0x5800,8);
    CHECK(call(shim_dev_10,5,mouse->guest,16u,0x5000u,0x5800u,0u)==DI_OK&&G32(0x5800)==0);
    settle(&t,60);gameplay_clock(0);
}

static void key_delivery(void) {
    no_controller();game(1);host_device_input_select(KB_GC,PT_GC);gameplay_clock(1);drain_keys();
    uint32_t o[8],d[8];
    /* A key tap between reads: one down event; its up only after a tick. */
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    unsigned n=key_events(o,d);CHECK(n==1&&o[0]==0x12&&d[0]==0x80);
    for(int i=0;i<5;i++)CHECK(key_events(o,d)==0); /* same frame/tick: still down */
    game_tick();n=key_events(o,d);CHECK(n==1&&o[0]==0x12&&d[0]==0);
    /* Two quick taps: down, up (after a tick), down (after another), up. */
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    unsigned downs=0,ups=0;
    for(int f=0;f<12;f++){n=key_events(o,d);for(unsigned i=0;i<n;i++){CHECK(o[i]==0x12);if(d[i])downs++;else ups++;}game_tick();}
    CHECK(downs==2&&ups==2);
    /* USER32 peeks never advance delivery. */
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    uint8_t keys[256];for(int i=0;i<5;i++){host_dinput_keyboard_state(keys);CHECK(keys[0x12]==0x80);}
    n=key_events(o,d);CHECK(n==1&&d[0]==0x80);game_tick();n=key_events(o,d);CHECK(n==1&&d[0]==0);game_tick();key_events(o,d);
    /* Fixed tick, Halo's one-event drain with unrelated keys queued: short
     * presses do not expire from API polling alone. */
    game_tick();drain_keys();
    CHECK(host_device_input_key(KB_GC,U_W,1,0));
    CHECK(host_device_input_key(KB_GC,U_SPACE,1,0)&&host_device_input_key(KB_GC,U_SPACE,0,0));
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    uint32_t oo,dd;unsigned seen=0,released=0;
    for(int i=0;i<40;i++)if(key_event_one(&oo,&dd)){if(dd)seen++;else released++;}
    CHECK(seen==3&&released==0);
    game_tick();seen=released=0;
    for(int i=0;i<40;i++)if(key_event_one(&oo,&dd)){CHECK(oo==0x39||oo==0x12);CHECK(!dd);released++;}
    CHECK(released==2);
    CHECK(host_device_input_key(KB_GC,U_W,0,0));game_tick();drain_keys();game_tick();drain_keys();
    /* Native menu: the same drain within one pump keeps the press; it ends
     * only after whole pumps (mouse reads) and the minimum time. */
    S16(0x718FA6,1);
    CHECK(host_device_input_key(KB_GC,U_E,1,0)&&host_device_input_key(KB_GC,U_E,0,0));
    seen=released=0;for(int i=0;i<40;i++)if(key_event_one(&oo,&dd)){if(dd)seen++;else released++;}
    CHECK(seen==1&&released==0);
    test_ns+=HOST_DEVICE_MENU_MIN_NS;mouse_state();mouse_state();
    CHECK(key_event_one(&oo,&dd)&&oo==0x12&&dd==0);
    test_ns+=HOST_DEVICE_MENU_MIN_NS;mouse_state();mouse_state();drain_keys();S16(0x718FA6,0);
    /* Held: down until released, release immediate. */
    CHECK(host_device_input_key(KB_GC,U_W,1,0));
    for(int i=0;i<20;i++){CHECK(key_state(0x11)==0x80);game_tick();}
    CHECK(host_device_input_key(KB_GC,U_W,0,0));CHECK(!key_state(0x11));game_tick();key_state(0);
    drain_keys();
    /* Command chords belong to the system; Command-period is one Escape. */
    CHECK(!host_device_input_key(KB_GC,U_W,1,1));CHECK(!key_state(0x11));
    CHECK(host_device_input_key(KB_GC,U_PERIOD,1,1));
    n=key_events(o,d);CHECK(n==1&&o[0]==0x01&&d[0]==0x80);
    CHECK(host_device_input_key(KB_GC,U_PERIOD,0,0)); /* Command released first: Escape still releases */
    game_tick();n=key_events(o,d);CHECK(n==1&&o[0]==0x01&&d[0]==0);
    /* The overlay Pause affordance: one Escape press, same gates. */
    game_tick(); /* the previous Escape's release crosses a tick first */
    CHECK(host_device_input_key_pulse(0x01));n=key_events(o,d);CHECK(n==1&&o[0]==0x01&&d[0]==0x80);
    game_tick();n=key_events(o,d);CHECK(n==1&&d[0]==0);
    game(0);CHECK(!host_device_input_key_pulse(0x01));game(1);
    /* Two usages on one DIK: released only when both are up. */
    CHECK(host_device_input_key(KB_GC,0x31,1,0)&&host_device_input_key(KB_GC,0x32,1,0));
    CHECK(host_device_input_key(KB_GC,0x31,0,0));game_tick();key_state(0);game_tick();CHECK(key_state(0x2B)==0x80);
    CHECK(host_device_input_key(KB_GC,0x32,0,0));CHECK(!key_state(0x2B));
    game_tick();drain_keys();gameplay_clock(0);
}

static void mouse_motion(void) {
    no_controller();game(1);host_device_input_select(KB_GC,PT_GC);
    MouseRead m=mouse_state();CHECK(m.dx==0&&m.dy==0);
    /* +x right, +y down (DirectInput); whole counts out, remainder carried. */
    CHECK(host_device_input_motion(PT_GC,10.6,-3.2));m=mouse_state();CHECK(m.dx==10&&m.dy==-3);
    CHECK(host_device_input_motion(PT_GC,0.6,0));m=mouse_state();CHECK(m.dx==1&&m.dy==0);
    m=mouse_state();CHECK(m.dx==0&&m.dy==0); /* consumed exactly once */
    /* Remainders (+0.2, -0.2) carry until a fence drops them. */
    CHECK(host_device_input_motion(PT_GC,0.9,-0.9));m=mouse_state();CHECK(m.dx==1&&m.dy==-1);
    host_device_input_release();CHECK(host_device_input_motion(PT_GC,0.9,-0.9));m=mouse_state();CHECK(m.dx==0&&m.dy==0);
    host_device_input_release();
    /* The unselected pointer source cannot add motion; touch is its own source. */
    CHECK(!host_device_input_motion(PT_UI,100,100));CHECK(host_device_input_motion(TOUCH,-7,2));
    CHECK(host_device_input_motion(PT_GC,3,0));m=mouse_state();CHECK(m.dx==-4&&m.dy==2);
    /* Bounded and finite. */
    CHECK(host_device_input_motion(PT_GC,1e9,-1e9));m=mouse_state();CHECK(m.dx==2048&&m.dy==-2048);
    CHECK(!host_device_input_motion(PT_GC,NAN,0)&&!host_device_input_motion(PT_GC,0,INFINITY));
    /* Legacy producers (PAD2KEY look, gaze servo) keep their globals and reset. */
    host_mouse_dx=5;host_mouse_dy=-1;CHECK(host_device_input_motion(PT_GC,3,1));
    m=mouse_state();CHECK(m.dx==8&&m.dy==0&&host_mouse_dx==0&&host_mouse_dy==0);
    /* Sources and gates for buttons. */
    CHECK(!host_device_input_button(PT_UI,0,1)&&!host_device_input_button(PT_GC,8,1)&&!host_device_input_button(PT_GC,-1,1));
    CHECK(!host_device_input_click(PT_GC,0));
    /* Mouse clicks still reach Halo's own menus (its cursor uses them). */
    S16(0x718FA6,1);CHECK(host_device_input_click(TOUCH,0));m=mouse_state();CHECK(m.b[0]==0x80);
    settle_all();m=mouse_state();CHECK(m.b[0]==0);S16(0x718FA6,0);
    /* An app-ownership change drops a pending pulse, motion and held button. */
    CHECK(host_device_input_click(TOUCH,0));CHECK(host_device_input_button(PT_GC,1,1));CHECK(host_device_input_motion(TOUCH,30,30));
    game(0);game(1);m=mouse_state();CHECK(m.b[0]==0&&m.b[1]==0&&m.dx==0&&m.dy==0);
    CHECK(!host_device_input_button(PT_GC,1,0)); /* stale release after the fence */
    /* Pointer source change (lock lost: GCMouse -> UIKit) releases held fire. */
    CHECK(host_device_input_button(PT_GC,0,1));host_device_input_select(KB_GC,PT_UI);
    m=mouse_state();CHECK(m.b[0]==0);CHECK(!host_device_input_button(PT_GC,0,0));
    CHECK(host_device_input_button(PT_UI,0,1));m=mouse_state();CHECK(m.b[0]==0x80);
    CHECK(host_device_input_button(PT_UI,0,0));settle_all();m=mouse_state();CHECK(m.b[0]==0);
    /* A cancelled pointer stream: the held button releases, queued presses
     * are dropped, nothing new fires. */
    CHECK(host_device_input_button(PT_UI,1,1));m=mouse_state();CHECK(m.b[1]==0x80);
    CHECK(host_device_input_button_cancel(PT_UI,1));m=mouse_state();CHECK(m.b[1]==0);
    CHECK(!host_device_input_button(PT_UI,1,0)); /* its late release is stale */
    settle_all();
    CHECK(host_device_input_button(PT_UI,0,1)&&host_device_input_button(PT_UI,0,0)); /* queued, not yet read */
    CHECK(host_device_input_button_cancel(PT_UI,0));
    for(int i=0;i<8;i++){test_ns+=HOST_DEVICE_MENU_MIN_NS;m=mouse_state();CHECK(m.b[0]==0);}
    CHECK(!host_device_input_button_cancel(PT_GC,0)); /* unselected source */
    host_device_input_select(KB_GC,PT_GC);
}

static void fence_after_snapshot(void) {host_device_input_release();device_input_test_after_snapshot=NULL;}
static void queue_invalidation(void) {
    no_controller();game(1);host_device_input_select(KB_GC,PT_GC);drain_keys();
    uint32_t o[8],d[8];
    /* A queued down from before a fence is dropped; its release follows. */
    CHECK(host_device_input_key(KB_GC,U_W,1,0));keyboard_buffer_events(keyboard);
    CHECK(keyboard->buf_count==1);BufEvent *e=&keyboard->buf[keyboard->buf_head];
    CHECK(e->device_generation==host_device_input_generation()&&e->sense_app_epoch==0);
    game(0);game(1);
    unsigned n=key_events(o,d);CHECK(n==1&&o[0]==0x11&&d[0]==0); /* up only: no stale press */
    /* Keys and generation come from one snapshot: a fence landing right
     * after the read cannot relabel the old key as current. */
    CHECK(host_device_input_key(KB_GC,U_W,1,0));uint64_t g=host_device_input_generation();
    device_input_test_after_snapshot=fence_after_snapshot;keyboard_buffer_events(keyboard);
    CHECK(keyboard->buf_count==1);e=&keyboard->buf[keyboard->buf_head];
    CHECK(e->data==0x80&&e->device_generation==g&&host_device_input_generation()!=g);
    n=key_events(o,d);CHECK(n==1&&o[0]==0x11&&d[0]==0);
    /* A fresh post-fence press of the same key keeps its edge. */
    CHECK(host_device_input_key(KB_GC,U_S,1,0));keyboard_buffer_events(keyboard);
    host_device_input_release();CHECK(host_device_input_key(KB_GC,U_S,1,0));
    n=key_events(o,d);CHECK(n==1&&o[0]==0x1F&&d[0]==0x80);
    CHECK(host_device_input_key(KB_GC,U_S,0,0));settle_all();drain_keys();
    /* The same queued down across a lease: dropped even though the controller
     * gate stays closed afterwards (no controller); later presses flow. */
    CHECK(host_device_input_key(KB_GC,U_A,1,0));keyboard_buffer_events(keyboard);
    uint64_t t=hostgc_app_input_begin();game(0);hostgc_app_input_end(t);game(1);
    CHECK(hostgc_app_input_blocked());
    n=key_events(o,d);for(unsigned i=0;i<n;i++)CHECK(!(d[i]&0x80));
    CHECK(host_device_input_key(KB_GC,U_D,1,0));n=key_events(o,d);CHECK(n==1&&o[0]==0x20&&d[0]==0x80);
    CHECK(host_device_input_key(KB_GC,U_D,0,0));settle_all();drain_keys();
    /* Library -> game with zero controllers: GetDeviceData PEEK shows the
     * new press without consuming it; the following read delivers it once. */
    t=hostgc_app_input_begin();game(0);hostgc_app_input_end(t);game(1);CHECK(hostgc_app_input_blocked());
    CHECK(host_device_input_key(KB_GC,U_SPACE,1,0));
    S32(0x5800,8);CHECK(call(shim_dev_10,5,keyboard->guest,16u,0x5000u,0x5800u,1u)==DI_OK);
    CHECK(G32(0x5800)==1&&G32(0x5000)==0x39&&G32(0x5004)==0x80);
    n=key_events(o,d);CHECK(n==1&&o[0]==0x39&&d[0]==0x80);CHECK(key_events(o,d)==0);
    CHECK(host_device_input_key(KB_GC,U_SPACE,0,0));settle_all();drain_keys();
    /* A controller holding the same key keeps a pruned hardware edge's owner
     * (ordinary pad Cross -> Space while the gate is open). */
    neutral_controller();CHECK(!hostgc_app_input_blocked());
    CHECK(host_device_input_key(KB_GC,U_SPACE,1,0));keyboard_buffer_events(keyboard);
    host_device_input_release();pad.buttons[HOSTGC_BTN_A]=true;test_ns+=1000000;inject();
    n=key_events(o,d);CHECK(n==1&&o[0]==0x39&&d[0]==0x80);
    pad.buttons[HOSTGC_BTN_A]=false;test_ns+=1000000;inject();drain_keys();
    /* Pad -> device takeover: a queued pad Enter whose app epoch is then
     * invalidated is kept because the hardware Enter now holds it, and takes
     * the device generation, so the device's own fence still removes it. */
    S16(0x718FA6,1); /* menu: pad Cross is Return */
    pad.buttons[HOSTGC_BTN_A]=true;test_ns+=1000000;inject();keyboard_buffer_events(keyboard);
    CHECK(keyboard->buf_count==1);e=&keyboard->buf[keyboard->buf_head];
    CHECK(e->ofs==0x1C&&e->data==0x80&&e->sense_app_epoch&&!e->device_generation);
    CHECK(host_device_input_key(KB_GC,0x28,1,0)); /* hardware Return */
    t=hostgc_app_input_begin();hostgc_app_input_end(t); /* pad epoch invalidated; device not fenced */
    pad.buttons[HOSTGC_BTN_A]=false;test_ns+=1000000;inject();
    keyboard_buffer_events(keyboard);
    CHECK(keyboard->buf_count==1);e=&keyboard->buf[keyboard->buf_head];
    CHECK(e->ofs==0x1C&&e->data==0x80&&!e->sense_app_epoch&&e->device_generation==host_device_input_generation());
    host_device_input_release(); /* e.g. focus lost */
    n=key_events(o,d);for(unsigned i=0;i<n;i++)CHECK(!(d[i]&0x80)); /* pending down removed, release delivered */
    CHECK(n==1&&o[0]==0x1C);
    S16(0x718FA6,0);drain_keys();no_controller();
}

/* The physical callback thread races the engine's reads. Every count pushed
 * is delivered exactly once; key toggles never wedge state. */
static _Atomic int64_t consumed; static _Atomic int producer_done;
static void *producer(void *arg) {
    (void)arg; int64_t pushed=0;
    for(int batch=0;batch<200;batch++) {
        for(int i=0;i<1000;i++){CHECK(host_device_input_motion(PT_GC,1,-1));pushed++;}
        host_device_input_key(KB_GC,U_A,batch&1?0:1,0);
        host_device_input_click(TOUCH,batch&1);
        while(atomic_load(&consumed)<pushed) {}
    }
    host_device_input_key(KB_GC,U_A,0,0);
    atomic_store(&producer_done,1);return NULL;
}
static void concurrency(void) {
    no_controller();game(1);host_device_input_select(KB_GC,PT_GC);
    atomic_store(&consumed,0);atomic_store(&producer_done,0);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,producer,NULL)==0);
    int64_t dy=0;DeviceClock clock={.now_ns=1};
    while(!atomic_load(&producer_done)) {
        int32_t x,y;uint8_t b[HOST_DEVICE_BUTTON_COUNT];uint8_t keys[256];
        clock.now_ns+=1000;device_input_mouse(&x,&y,b,&clock);dy+=y;atomic_fetch_add(&consumed,x);device_input_keyboard(keys,&clock,1);
    }
    pthread_join(thread,NULL);
    int32_t x,y;uint8_t b[HOST_DEVICE_BUTTON_COUNT];device_input_mouse(&x,&y,b,&clock);atomic_fetch_add(&consumed,x);dy+=y;
    CHECK(atomic_load(&consumed)==200000&&dy==-200000);
    uint8_t keys[256];
    for(int i=0;i<40;i++){clock.now_ns+=HOST_DEVICE_PULSE_MAX_NS;device_input_keyboard(keys,&clock,1);device_input_mouse(&x,&y,b,&clock);}
    HostDeviceInputStats st;host_device_input_stats(&st);CHECK(st.keys_down==0&&st.buttons_down==0);
}

int main(void) {@autoreleasepool {
    g_inited=true;engine_flat_base=calloc(1,8*1024*1024);CHECK(engine_flat_base);
    no_controller();open_devices();
    mapping();ownership_and_sources();click_delivery();key_delivery();mouse_motion();queue_invalidation();concurrency();
    free(engine_flat_base);
    printf("PASS %u hardware keyboard/mouse/touch source checks: HID->DIK, source arbitration, app lease vs controller gate, "
           "tick-aligned click/key delivery at 30/60/90/120/240 Hz, menu/no-clock pump fallback, stalled/jumped/replaced clock, "
           "single grenade route, mixed hold+tap, cancel, fences, snapshot generation, pad->device takeover, deltas, concurrency "
           "(synthetic; no device)\n",checks);
}}
