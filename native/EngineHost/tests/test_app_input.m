/* Actual native lease + raw snapshot + Sense bridge + DirectInput integration.
 * Hardware and time are the only replaced boundaries. No app/device launch. */
#import <Foundation/Foundation.h>
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#include <time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
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
#define CHECK(x) do {checks++; if(!(x)){fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort();}}while(0)
static HostGCSnapshot pad;
static HaloSenseFrame pose;
static uint64_t session;
static void poll_raw(void) {HostGCSnapshot raw;hostgc_inject_test_snapshot(&pad);CHECK(hostgc_poll(&raw)==pad.connected);CHECK(!memcmp(&raw,&pad,sizeof raw));}
static void publish(void) {poll_raw();halo_sense_observe_controller_for_session(session,&pad);halo_sense_publish(session,&pose);}
static void neutral(void) {pad=(HostGCSnapshot){.connected=true,.spatial=true,.source_generation=hostgc_source_generation()};test_ns+=1000000;publish();}
static uint32_t call(HostShim fn,unsigned count,...) {
    EngineCPU cpu={0};cpu.gpr[4]=0x1000;S32(0x1000,0x1234);
    va_list a;va_start(a,count);for(unsigned i=0;i<count;i++)S32(0x1004+4*i,va_arg(a,uint32_t));va_end(a);
    fn(&cpu);CHECK(cpu.pc==0x1234);return cpu.gpr[0];
}
static void none(const uint8_t *p,unsigned n) {for(unsigned i=0;i<n;i++)CHECK(!p[i]);}
static void lease_and_bridge(void) {
    uint64_t first=hostgc_app_input_begin();CHECK(first&&hostgc_app_input_blocked()&&halo_sense_app_input_blocked());
    HaloSenseDualInput input;CHECK(!halo_sense_dual_input(&input));
    pad.rt=1;pad.buttons[HOSTGC_BTN_RTRIGGER]=true;pose.right.trigger=1;publish();
    CHECK(!halo_sense_dual_input(&input));HostGCSnapshot filtered=pad;
    CHECK(hostgc_app_input_filter(&filtered)&&filtered.connected&&filtered.spatial&&filtered.rt==0);
    CHECK(filtered.source_generation==pad.source_generation);
    uint64_t second=hostgc_app_input_begin();CHECK(second!=first);hostgc_app_input_end(first);
    CHECK(!hostgc_app_input_renew(first)&&hostgc_app_input_renew(second));
    hostgc_app_input_end(second);CHECK(hostgc_app_input_blocked());poll_raw();CHECK(hostgc_app_input_blocked());
    pose.right.trigger=0;neutral();CHECK(!hostgc_app_input_blocked()&&!halo_sense_app_input_blocked());
    CHECK(halo_sense_dual_input(&input)&&input.armed[1]);
    uint64_t expiry=hostgc_app_input_begin();test_ns+=500000001;CHECK(!hostgc_app_input_renew(expiry));
    CHECK(hostgc_app_input_blocked());pad.spatial_raw_lx=.6f;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.spatial_raw_lx=0;pad.rx=.5f;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.rx=0;pad.lt=NAN;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.lt=0;pad.buttons[HOSTGC_BTN_A]=true;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.connected=false;poll_raw();CHECK(hostgc_app_input_blocked());
    neutral();CHECK(!hostgc_app_input_blocked());
    /* Cached neutral from before exit cannot release the handback gate. */
    uint64_t t=hostgc_app_input_begin();uint64_t before=test_ns;test_ns+=1000000;hostgc_app_input_end(t);
    app_input_observe(&pad,before);CHECK(hostgc_app_input_blocked());neutral();CHECK(!hostgc_app_input_blocked());
    /* Selecting an ordinary controller clears the Sense bridge block but the
     * native UI still owns it: the engine sees a neutral ordinary pad. */
    t=hostgc_app_input_begin();pad.spatial=false;pad.rt=1;pad.buttons[HOSTGC_BTN_RTRIGGER]=true;poll_raw();
    CHECK(!halo_sense_app_input_blocked());filtered=pad;
    CHECK(hostgc_app_input_filter(&filtered)&&filtered.connected&&!filtered.spatial&&filtered.rt==0&&!filtered.buttons[HOSTGC_BTN_RTRIGGER]);
    pad.spatial=true;poll_raw();CHECK(halo_sense_app_input_blocked());hostgc_app_input_end(t);neutral();
    CHECK(!halo_sense_app_input_blocked());
    /* Chord is neutralized before the Swift UI creates a token. */
    pad.buttons[HOSTGC_BTN_OPTIONS]=pad.buttons[HOSTGC_BTN_MENU]=true;poll_raw();
    CHECK(hostgc_app_input_blocked()&&halo_sense_app_input_blocked());
    filtered=pad;CHECK(hostgc_app_input_filter(&filtered)&&!filtered.buttons[HOSTGC_BTN_MENU]);
    neutral();CHECK(!hostgc_app_input_blocked());
}
static void directinput(void) {
    uint8_t keys[256],mouse[8];HaloSenseDualInput input;
    neutral();halo_sense_set_gameplay_supported(1);publish();CHECK(halo_sense_dual_input(&input));
    uint64_t t=hostgc_app_input_begin();
    for(unsigned i=0;i<HOSTGC_BUTTON_COUNT;i++)pad.buttons[i]=true;
    pad.rt=pad.lt=1;pad.lx=pad.rx=1;pad.ly=pad.ry=-1;poll_raw();
    for(unsigned menu=0;menu<2;menu++) {
        S16(0x718FA6,menu);host_dinput_keyboard_state(keys);host_dinput_mouse_buttons_state(mouse);none(keys,256);none(mouse,8);
        setenv("HALO_PAD2KEY","0",1);HostGCSnapshot joy;CHECK(joy_read_gameplay_snapshot(&joy));
        CHECK(joy.rt==0&&joy.lt==0&&joy.lx==0&&joy.rx==0);for(unsigned i=0;i<HOSTGC_BUTTON_COUNT;i++)CHECK(!joy.buttons[i]);
        unsetenv("HALO_PAD2KEY");
    }
    /* The physical keyboard stays independent; a conventional controller is
     * now owned by the native UI too, so nothing it holds reaches Halo. */
    host_keyboard_state[0x20]=0x80;host_dinput_keyboard_state(keys);CHECK(keys[0x20]==0x80);host_keyboard_state[0x20]=0;
    pad.spatial=false;poll_raw();S16(0x718FA6,0);host_dinput_keyboard_state(keys);host_dinput_mouse_buttons_state(mouse);
    none(keys,256);none(mouse,8);
    hostgc_app_input_end(t);neutral();halo_sense_set_gameplay_supported(1);publish();
    /* Old queued Sense Enter cannot survive UI claim, even after lease ends. */
    S16(0x718FA6,1);DIObj *key=obj_new(DEV_KEYBOARD,0);CHECK(key);key->buffer_size=16;
    CHECK(call(shim_dev_7,1,key->guest)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();keyboard_buffer_events(key);CHECK(key->buf_count==1&&key->buf[key->buf_head].sense_app_epoch);
    t=hostgc_app_input_begin();hostgc_app_input_end(t);neutral();keyboard_buffer_events(key);
    for(int i=0;i<key->buf_count;i++)CHECK(!(key->buf[(key->buf_head+i)%MAX_BUF_EVENTS].data&0x80));
    CHECK(call(shim_dev_2,1,key->guest)==0);
    /* A physical key taking over the queued same-key edge is retained. */
    key=obj_new(DEV_KEYBOARD,0);key->buffer_size=16;CHECK(call(shim_dev_7,1,key->guest)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();keyboard_buffer_events(key);CHECK(key->buf_count==1);
    t=hostgc_app_input_begin();host_keyboard_state[0x1C]=0x80;keyboard_buffer_events(key);
    CHECK(key->buf_count==1&&key->buf[key->buf_head].data==0x80&&!key->buf[key->buf_head].sense_app_epoch);
    host_keyboard_state[0x1C]=0;hostgc_app_input_end(t);neutral();CHECK(call(shim_dev_2,1,key->guest)==0);
    /* A conventional pad cannot take over a pending Sense key while native UI
     * owns input; it may only after the lease ends and it fully releases. */
    key=obj_new(DEV_KEYBOARD,0);key->buffer_size=16;CHECK(call(shim_dev_7,1,key->guest)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();keyboard_buffer_events(key);CHECK(key->buf_count==1);
    t=hostgc_app_input_begin();pad.spatial=false;poll_raw();keyboard_buffer_events(key);
    for(int i=0;i<key->buf_count;i++)CHECK(!(key->buf[(key->buf_head+i)%MAX_BUF_EVENTS].data&0x80));
    hostgc_app_input_end(t);keyboard_buffer_events(key);
    for(int i=0;i<key->buf_count;i++)CHECK(!(key->buf[(key->buf_head+i)%MAX_BUF_EVENTS].data&0x80));
    neutral();CHECK(call(shim_dev_2,1,key->guest)==0);
    /* Assigned joystick queued button cannot escape the lease either. */
    S16(0x718FA6,0);setenv("HALO_PAD2KEY","0",1);halo_sense_set_enabled(0);
    DIObj *joy=obj_new(DEV_JOYSTICK,0);joy->buffer_size=16;joy->fmt_count=1;joy->fmt[0]=(FmtObj){0,SRC_BUTTON,HOSTGC_BTN_A};joy->data_size=1;
    CHECK(call(shim_dev_7,1,joy->guest)==DI_OK);pad.buttons[HOSTGC_BTN_A]=true;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);CHECK(joy->buf_count==1);
    t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    for(int i=0;i<joy->buf_count;i++)CHECK(!(joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS].data&0x80));
    hostgc_app_input_end(t);neutral();CHECK(call(shim_dev_2,1,joy->guest)==0);
    /* The same ownership rule applies to an assigned joystick button. */
    joy=obj_new(DEV_JOYSTICK,0);joy->buffer_size=16;joy->fmt_count=1;joy->fmt[0]=(FmtObj){0,SRC_BUTTON,HOSTGC_BTN_A};joy->data_size=1;
    CHECK(call(shim_dev_7,1,joy->guest)==DI_OK);pad.buttons[HOSTGC_BTN_A]=true;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);CHECK(joy->buf_count==1);
    t=hostgc_app_input_begin();pad.spatial=false;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    for(int i=0;i<joy->buf_count;i++)CHECK(!(joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS].data&0x80));
    hostgc_app_input_end(t);neutral();CHECK(call(shim_dev_2,1,joy->guest)==0);unsetenv("HALO_PAD2KEY");
}
/* Ordinary analog axes: a deflection queued before the UI claim never reaches
 * Halo, an already delivered deflection is followed by its neutral value, and
 * neutral/release values are never pruned. Stick and trigger axes alike. */
static void ordinary_neutral(void);
static uint32_t buffered_value(DIObj *o,unsigned i) {return o->buf[(o->buf_head+i)%MAX_BUF_EVENTS].data;}
static void ordinary_axes(void) {
    S16(0x718FA6,0);setenv("HALO_PAD2KEY","0",1);ordinary_neutral();
    DIObj *joy=obj_new(DEV_JOYSTICK,0);CHECK(joy);joy->buffer_size=16;joy->fmt_count=2;joy->data_size=8;
    joy->fmt[0]=(FmtObj){0,SRC_X,0};joy->fmt[1]=(FmtObj){4,SRC_Z,0}; /* left stick X, right trigger */
    CHECK(call(shim_dev_7,1,joy->guest)==DI_OK);CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    HostGCSnapshot released={0};
    uint32_t mid=(uint32_t)axis_value_for_src(&released,SRC_X,joy->axis_min,joy->axis_max,joy->deadzone);
    uint32_t rest=(uint32_t)axis_value_for_src(&released,SRC_Z,joy->axis_min,joy->axis_max,joy->deadzone);
    CHECK(mid!=rest&&joy_neutral_value(joy,&joy->fmt[0])==mid&&joy_neutral_value(joy,&joy->fmt[1])==rest);
    /* Queued full-right movement and full trigger, then the UI claims input. */
    pad.lx=1;pad.rt=1;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==2);for(unsigned i=0;i<2;i++)CHECK(joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS].sense_app_epoch);
    uint64_t t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==2);
    for(unsigned i=0;i<2;i++){BufEvent *e=&joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS];CHECK(!e->sense_app_epoch&&(e->ofs==0?e->data==mid:e->data==rest));}
    HostGCSnapshot state;CHECK(joy_poll_acquired(joy,&state)==DI_OK&&state.lx==0&&state.rt==0); /* immediate state neutral too */
    /* Held through dismissal: no deflection until a fresh release. */
    hostgc_app_input_end(t);joy_clear_buffer(joy);CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==0);
    ordinary_neutral();CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==0);
    /* Already delivered deflection: the claim delivers its neutral value. */
    pad.lx=-1;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==1&&buffered_value(joy,0)!=mid);
    joy_clear_buffer(joy); /* Halo read it */
    t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==1&&buffered_value(joy,0)==mid&&!joy->buf[joy->buf_head].sense_app_epoch);
    hostgc_app_input_end(t);ordinary_neutral();joy_clear_buffer(joy);CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==0);
    /* A queued return-to-centre from before the claim is kept. */
    pad.lx=1;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);joy_clear_buffer(joy);
    pad.lx=0;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==1&&!joy->buf[joy->buf_head].sense_app_epoch);
    t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==1&&buffered_value(joy,0)==mid);
    hostgc_app_input_end(t);ordinary_neutral();joy_clear_buffer(joy);
    /* No lease: ordinary deflection is delivered untouched. */
    pad.ry=0;pad.lx=1;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==1&&buffered_value(joy,0)!=mid);
    CHECK(call(shim_dev_2,1,joy->guest)==0);unsetenv("HALO_PAD2KEY");ordinary_neutral();
}
/* The renderer's PAD2KEY movement/look bridge (d3d9.c) is a raw consumer:
 * it must ask the lease filter before any mapping, so native UI ownership
 * blocks ordinary movement and look there too. The filter's own ordinary
 * behaviour is exercised in ordinary_input(). */
static void pad2key_consumer_filtered(void) {
    char path[4096];snprintf(path,sizeof path,"%s",__FILE__);char *slash=strrchr(path,'/');
    CHECK(slash&&(size_t)(slash-path)+12<sizeof path);strcpy(slash,"/../d3d9.c");
    FILE *file=fopen(path,"rb");CHECK(file);static char source[4<<20];size_t n=fread(source,1,sizeof source-1,file);fclose(file);source[n]=0;
    const char *site=strstr(source,"HostGCSnapshot pad; if(hostgc_poll(&pad) && pad.connected");CHECK(site);
    const char *end=strchr(site,'{');CHECK(end);
    const char *filter=strstr(site,"!hostgc_app_input_filter(&pad)");CHECK(filter&&filter<end);
    const char *mapping=strstr(site,"halo_sense_filter_gamepad(&pad)");CHECK(mapping&&filter<mapping);
    CHECK(!strstr(end,"HostGCSnapshot pad; if(hostgc_poll(&pad) && pad.connected")); /* one PAD2KEY site */
}
/* Ordinary controllers obey the same native-UI lease as Sense: raw, mapped
 * keyboard, mouse buttons and joystick, immediate and buffered. Without a
 * lease, ordinary gameplay and the Menu pause are exactly as before. */
static void ordinary_neutral(void) {pad=(HostGCSnapshot){.connected=true,.source_generation=hostgc_source_generation()};test_ns+=1000000;poll_raw();}
static void ordinary_input(void) {
    uint8_t keys[256],mouse[8];HostGCSnapshot filtered;
    S16(0x718FA6,0);ordinary_neutral();CHECK(!hostgc_app_input_blocked());
    pad.buttons[HOSTGC_BTN_A]=pad.buttons[HOSTGC_BTN_MENU]=pad.buttons[HOSTGC_BTN_RTRIGGER]=true;pad.rt=1;poll_raw();
    filtered=pad;CHECK(!hostgc_app_input_filter(&filtered)&&filtered.rt==1);
    host_dinput_keyboard_state(keys);host_dinput_mouse_buttons_state(mouse);
    CHECK(keys[0x39]==0x80&&keys[0x01]==0x80&&mouse[0]==0x80); /* no lease: jump, Menu pause, fire */
    uint64_t t=hostgc_app_input_begin();CHECK(hostgc_app_input_blocked()&&!halo_sense_app_input_blocked());
    pad.dpad_up=pad.buttons[HOSTGC_BTN_DPAD_UP]=true;pad.lx=pad.ly=1;pad.ry=-1;poll_raw();
    for(unsigned menu=0;menu<2;menu++) {
        S16(0x718FA6,menu);host_dinput_keyboard_state(keys);host_dinput_mouse_buttons_state(mouse);none(keys,256);none(mouse,8);
        setenv("HALO_PAD2KEY","0",1);HostGCSnapshot joy;CHECK(joy_read_gameplay_snapshot(&joy)&&joy.connected&&!joy.spatial);
        CHECK(joy.rt==0&&joy.lx==0&&joy.ry==0&&!joy.dpad_up);for(unsigned i=0;i<HOSTGC_BUTTON_COUNT;i++)CHECK(!joy.buttons[i]);
        unsetenv("HALO_PAD2KEY");
    }
    host_keyboard_state[0x20]=0x80;host_dinput_keyboard_state(keys);CHECK(keys[0x20]==0x80);host_keyboard_state[0x20]=0; /* physical keyboard */
    /* Held through dismissal: withheld until a fresh, full release. */
    S16(0x718FA6,0);hostgc_app_input_end(t);poll_raw();CHECK(hostgc_app_input_blocked());
    host_dinput_keyboard_state(keys);none(keys,256);
    pad.dpad_up=pad.buttons[HOSTGC_BTN_DPAD_UP]=false;pad.lx=pad.ly=pad.ry=0;poll_raw();CHECK(hostgc_app_input_blocked());
    /* Disconnect never hands back; a stale-topology neutral cannot either. */
    pad.connected=false;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.connected=true;poll_raw();filtered=pad;CHECK(hostgc_app_input_filter(&filtered)&&!filtered.buttons[HOSTGC_BTN_A]);
    uint64_t old_source=pad.source_generation;atomic_fetch_add_explicit(&g_source_generation,1,memory_order_acq_rel);
    pad=(HostGCSnapshot){.connected=true,.source_generation=old_source};test_ns+=1000000;poll_raw();CHECK(hostgc_app_input_blocked());
    pad.source_generation=hostgc_source_generation();pad.buttons[HOSTGC_BTN_B]=true;test_ns+=1000000;poll_raw();
    CHECK(hostgc_app_input_blocked()); /* reconnected while holding B */
    ordinary_neutral();CHECK(!hostgc_app_input_blocked());
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();host_dinput_keyboard_state(keys);CHECK(keys[0x39]==0x80); /* handback */
    ordinary_neutral();
    /* Expiry without an explicit end behaves the same way. */
    t=hostgc_app_input_begin();test_ns+=500000001;CHECK(!hostgc_app_input_renew(t));
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();host_dinput_keyboard_state(keys);none(keys,256);ordinary_neutral();CHECK(!hostgc_app_input_blocked());

    /* Buffered keyboard: a queued ordinary press cannot cross into UI ownership. */
    S16(0x718FA6,1);DIObj *key=obj_new(DEV_KEYBOARD,0);CHECK(key);key->buffer_size=16;CHECK(call(shim_dev_7,1,key->guest)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();keyboard_buffer_events(key);
    CHECK(key->buf_count==1&&key->buf[key->buf_head].ofs==0x1C&&key->buf[key->buf_head].sense_app_epoch);
    t=hostgc_app_input_begin();keyboard_buffer_events(key);
    CHECK(key->buf_count==1&&key->buf[key->buf_head].data==0); /* press dropped; release kept */
    hostgc_app_input_end(t);keyboard_buffer_events(key);CHECK(key->buf_count==1); /* still held: withheld */
    ordinary_neutral();keyboard_buffer_events(key);CHECK(key->buf_count==1);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();keyboard_buffer_events(key);
    CHECK(key->buf_count==2&&key->buf[(key->buf_head+1)%MAX_BUF_EVENTS].data==0x80); /* fresh press after handback */
    CHECK(call(shim_dev_2,1,key->guest)==0);ordinary_neutral();

    /* Buffered joystick (PAD2KEY=0): presses are dropped, releases never are. */
    S16(0x718FA6,0);setenv("HALO_PAD2KEY","0",1);
    DIObj *joy=obj_new(DEV_JOYSTICK,0);CHECK(joy);joy->buffer_size=16;joy->fmt_count=2;joy->data_size=8;
    joy->fmt[0]=(FmtObj){0,SRC_BUTTON,HOSTGC_BTN_A};joy->fmt[1]=(FmtObj){4,SRC_POV,0};
    CHECK(call(shim_dev_7,1,joy->guest)==DI_OK);CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=true;pad.dpad_up=true;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==2);for(int i=0;i<2;i++)CHECK(joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS].sense_app_epoch); /* A and POV up */
    t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==2);
    for(int i=0;i<2;i++){BufEvent *e=&joy->buf[(joy->buf_head+i)%MAX_BUF_EVENTS];CHECK(!e->sense_app_epoch&&(e->ofs==0?e->data==0:e->data==0xFFFFFFFFu));}
    hostgc_app_input_end(t);ordinary_neutral();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);CHECK(joy->buf_count==2);
    joy_clear_buffer(joy);
    pad.buttons[HOSTGC_BTN_A]=true;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    pad.buttons[HOSTGC_BTN_A]=false;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==2&&!joy->buf[(joy->buf_head+1)%MAX_BUF_EVENTS].sense_app_epoch);
    t=hostgc_app_input_begin();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy->buf_count==1&&joy->buf[joy->buf_head].data==0); /* unread press pruned, its release kept */
    hostgc_app_input_end(t);ordinary_neutral();
    /* No lease: ordinary joystick presses survive untouched. */
    joy_clear_buffer(joy);pad.buttons[HOSTGC_BTN_A]=true;poll_raw();CHECK(joy_poll_acquired(joy,NULL)==DI_OK);
    CHECK(joy_poll_acquired(joy,NULL)==DI_OK&&joy->buf_count==1&&joy->buf[joy->buf_head].data==0x80);
    CHECK(call(shim_dev_2,1,joy->guest)==0);unsetenv("HALO_PAD2KEY");ordinary_neutral();
    ordinary_axes();
}
/* The Settings affordance follows genuine in-game menus only. */
static void pause_menu_query(void) {
    S8(0x718FC9,0);S16(0x718FA6,0);S16(0x719720,0);S32(0x718F94,0);
    CHECK(!host_dinput_gameplay_menu_active()&&!host_dinput_menu_active());
    S8(0x718FC9,1);CHECK(host_dinput_menu_active()&&!host_dinput_gameplay_menu_active()); /* startup title shell */
    S16(0x718FA6,1);CHECK(host_dinput_menu_active()&&!host_dinput_gameplay_menu_active()); /* shell's own dialog */
    S16(0x719720,1);S32(0x718F94,0x40001000);CHECK(!host_dinput_gameplay_menu_active());
    S8(0x718FC9,0);S16(0x719720,0);S32(0x718F94,0);CHECK(host_dinput_gameplay_menu_active()&&host_dinput_menu_active()); /* in-game pause */
    S16(0x718FA6,0);CHECK(!host_dinput_gameplay_menu_active()); /* resumed */
    S32(0x718F94,0x40001000);CHECK(!host_dinput_gameplay_menu_active()&&!host_dinput_menu_active()); /* root without mode flag */
    S16(0x719720,1);CHECK(host_dinput_gameplay_menu_active()&&host_dinput_menu_active()); /* mode-gated UI root */
    S16(0x719720,0);S32(0x718F94,0);S16(0x718FA6,1);CHECK(host_dinput_gameplay_menu_active()); /* pause re-entry */
    S16(0x718FA6,0);
    uint8_t *base=engine_flat_base;engine_flat_base=NULL;
    CHECK(!host_dinput_gameplay_menu_active());engine_flat_base=base;
}
int main(void) {@autoreleasepool {
    g_inited=true;engine_flat_base=calloc(1,8*1024*1024);CHECK(engine_flat_base);
    halo_sense_set_enabled(1);halo_sense_set_dual_enabled(0);session=halo_sense_begin_session();
    HaloSenseHandPose hand={.position={.3f,.1f,-.2f},.forward={1,0,0},.up={0,0,1},.valid=1};
    pose=(HaloSenseFrame){.left=hand,.right=hand,.generation=1,.controller_generation=hostgc_source_generation()};
    neutral();halo_sense_set_gameplay_supported(1);publish();lease_and_bridge();directinput();
    pause_menu_query();ordinary_input();pad2key_consumer_filtered();
    halo_sense_end_session(session);free(engine_flat_base);printf("PASS %u actual app lease, raw snapshot, Sense bridge, DirectInput, ordinary-pad isolation (buttons, POV, axes, PAD2KEY site) and pause-menu checks\n",checks);
}}
