#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static int lease, menu;
static uint64_t now = 1000000000;
int hostgc_app_input_lease_active(void) { return lease; }
int host_dinput_menu_active(void) { return menu; }
uint64_t host_monotonic_ns(void) { return now; }
void host_prompt_note_device(int kind) { (void)kind; }
void host_prompt_note_motion(double x, double y, uint64_t t) { (void)x; (void)y; (void)t; }
static uint8_t guest[8*1024*1024];
uint8_t *engine_flat_base = guest;
static unsigned binding_reads;
static uint16_t G16(uint32_t address) { uint16_t v; ++binding_reads; memcpy(&v, engine_flat_base+address,2); return v; }
static void put16(uint32_t address,uint16_t value) { memcpy(guest+address,&value,2); }
#include "../device_input.inc"
static void bind_key(unsigned dik,unsigned action) {
    put16(0x0065BD58u+2*dik,(uint16_t)dik);
    put16(0x00710330u+2*dik,(uint16_t)action);
}
static void stock_bindings(void) {
    memset(guest+0x0065BD58u,0xff,512); memset(guest+0x00710330u,0xff,0x6D*2);
    memset(guest+0x0071040Au,0xff,16);
    static const unsigned pairs[][2]={{0x39,0},{0x22,1},{0x12,2},{0x0F,3},{0x21,4},{0x10,5},
        {0x1D,10},{0x2C,11},{0x13,13},{0x2D,14},{0x11,19},{0x1F,20},{0x1E,21},{0x20,22}};
    for(unsigned i=0;i<sizeof pairs/sizeof pairs[0];++i) bind_key(pairs[i][0],pairs[i][1]);
    put16(0x0071040Au,7); put16(0x0071040Cu,6);
}

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    DeviceClock clock = {.now_ns=now, .tick=1, .tick_valid=1};
    uint8_t keys[256], buttons[8]; int32_t dx, dy;
    stock_bindings();
    host_device_input_select(HOST_DEVICE_KEYBOARD_GC, HOST_DEVICE_POINTER_GC);
    host_device_input_set_game_owns(1);
    CHECK(!host_device_input_touch_action(19, 1));
    host_device_input_touch_enable(1);
    /* A held joystick begins before the first pump: do not lose its down. */
    CHECK(host_device_input_touch_actions_available()==0);
    unsigned first_reads = binding_reads;
    CHECK(host_device_input_touch_action(19, 1));
    CHECK(binding_reads==first_reads);
    device_input_keyboard(keys, &clock, 1);
    CHECK(keys[0x11]);
    CHECK(host_device_input_touch_action(7, 1));
    CHECK(!host_device_input_touch_action(7, 1));
    for (int i=0; i<90; ++i) {
        clock.tick++; clock.now_ns += 33333334;
        device_input_keyboard(keys, &clock, 1);
        device_input_mouse(&dx, &dy, buttons, &clock);
        CHECK(keys[0x11] && buttons[0]);
    }
    CHECK(host_device_input_touch_action(7, 0));
    CHECK(host_device_input_touch_action(19, 0));
    for (int i=0; i<3; ++i) {
        clock.tick++; device_input_keyboard(keys, &clock, 1); device_input_mouse(&dx, &dy, buttons, &clock);
    }
    CHECK(!keys[0x11] && !buttons[0]);
    /* Cancellation clears only touch. The hardware owner of the same key and
     * button, and its pending motion, survive without an artificial release. */
    CHECK(host_device_input_key(HOST_DEVICE_KEYBOARD_GC, 0x1A, 1, 0));
    CHECK(host_device_input_button(HOST_DEVICE_POINTER_GC, 0, 1));
    CHECK(host_device_input_touch_action(19, 1));
    CHECK(host_device_input_touch_action(7, 1));
    CHECK(host_device_input_motion(HOST_DEVICE_POINTER_GC, 9, -4));
    CHECK(host_device_input_touch_motion(40, 50));
    uint64_t generation = host_device_input_generation();
    host_device_input_touch_release();
    CHECK(host_device_input_generation() != generation);
    device_input_keyboard(keys, &clock, 1); device_input_mouse(&dx, &dy, buttons, &clock);
    CHECK(keys[0x11] && buttons[0] && dx==9 && dy==-4);
    /* A direct touch short press uses the same tick-spanning queue as keys. */
    CHECK(host_device_input_touch_action(0, 1)); CHECK(host_device_input_touch_action(0, 0));
    device_input_keyboard(keys, &clock, 1); CHECK(keys[0x39]);
    clock.tick++; device_input_keyboard(keys, &clock, 1); CHECK(!keys[0x39]);
    /* Native title/pause transitions clear existing holds before UI polling. */
    CHECK(host_device_input_touch_action(6, 1)); CHECK(host_device_input_touch_action(10, 1));
    menu=1; clock.menu=1;
    CHECK(!host_device_input_touch_action(0, 1) && !host_device_input_touch_action(6, 1));
    CHECK(!host_device_input_touch_motion(1, 2));
    device_input_keyboard(keys, &clock, 1); device_input_mouse(&dx, &dy, buttons, &clock);
    CHECK(!keys[0x1D] && !buttons[1] && keys[0x11] && buttons[0]);
    menu=0; clock.menu=0; clock.tick++;
    device_input_keyboard(keys, &clock, 1); CHECK(!keys[0x1D]);
    /* App UI lease and scene ownership remain independent safety gates. */
    lease=1; CHECK(!host_device_input_touch_action(0, 1));
    device_input_keyboard(keys, &clock, 1); CHECK(!keys[0x11]);
    lease=0; host_device_input_set_game_owns(0);
    CHECK(!host_device_input_touch_action(7, 1));
    host_device_input_set_game_owns(1); host_device_input_touch_enable(0);
    CHECK(!host_device_input_touch_action(7, 1));
    host_device_input_touch_enable(1);
    device_input_keyboard(keys, &clock, 1);
    CHECK(host_device_input_touch_action(7, 1));
    host_device_input_touch_enable(0);
    device_input_mouse(&dx, &dy, buttons, &clock); CHECK(!buttons[0]);
    CHECK(!host_device_input_touch_action(99, 1));
    CHECK(!host_device_input_touch_action(23, 1));
    CHECK(!host_device_input_touch_motion(NAN, 0));
    /* Custom mappings: resolve only in a pump, never in UI ingress. */
    host_device_input_touch_enable(1); device_input_keyboard(keys,&clock,1);
    put16(0x00710330u+2*0x13,0xffff); bind_key(0x2F,HOST_TOUCH_RELOAD);
    put16(0x0071040Au,0xffff); put16(0x0071040Au+2*5,HOST_TOUCH_FIRE);
    device_input_keyboard(keys,&clock,1);
    unsigned reads_before = binding_reads;
    CHECK(host_device_input_touch_action(HOST_TOUCH_RELOAD,1));
    CHECK(host_device_input_touch_action(HOST_TOUCH_FIRE,1));
    CHECK(binding_reads==reads_before);
    device_input_keyboard(keys,&clock,1); device_input_mouse(&dx,&dy,buttons,&clock);
    CHECK(keys[0x2F] && !keys[0x13] && buttons[5] && !buttons[0]);
    CHECK(binding_reads>reads_before);
    /* Changing a binding mid-hold never strands the old emitter. */
    put16(0x00710330u+2*0x2F,HOST_TOUCH_MELEE); bind_key(0x30,HOST_TOUCH_RELOAD);
    put16(0x00710330u+2*0x21,0xffff);
    device_input_keyboard(keys,&clock,1);
    CHECK(keys[0x2F] && !keys[0x30]);
    CHECK(host_device_input_touch_action(HOST_TOUCH_MELEE,1));
    CHECK(host_device_input_touch_action(HOST_TOUCH_RELOAD,0));
    for(int i=0;i<4;++i){clock.tick++;device_input_keyboard(keys,&clock,1);}
    CHECK(keys[0x2F]); /* melee now shares the pinned old emitter */
    CHECK(host_device_input_touch_action(HOST_TOUCH_MELEE,0));
    for(int i=0;i<4;++i){clock.tick++;device_input_keyboard(keys,&clock,1);}
    CHECK(!keys[0x2F]);
    CHECK(host_device_input_touch_action(HOST_TOUCH_RELOAD,1));
    device_input_keyboard(keys,&clock,1); CHECK(keys[0x30] && !keys[0x2F]);
    host_device_input_touch_release();
    /* Unbound actions do not fall back to a default key now bound elsewhere. */
    put16(0x00710330u+2*0x30,0xffff); bind_key(0x13,HOST_TOUCH_LIGHT);
    device_input_keyboard(keys,&clock,1);
    CHECK(!(host_device_input_touch_actions_available() & (1u<<HOST_TOUCH_RELOAD)));
    CHECK(!host_device_input_touch_action(HOST_TOUCH_RELOAD,1));
    device_input_keyboard(keys,&clock,1); CHECK(!keys[0x13]);
    put16(0x0065BD58u+2*0x13,0xffff); /* invalid reverse key is never followed */
    CHECK(!host_touch_resolve(HOST_TOUCH_RELOAD,device_touch_read16,NULL).kind);
    engine_flat_base=NULL; device_input_keyboard(keys,&clock,1);
    CHECK(host_device_input_touch_actions_available()==0);
    CHECK(!host_device_input_touch_action(HOST_TOUCH_FIRE,1));
    engine_flat_base=guest;
    host_device_input_touch_enable(0); reads_before=binding_reads;
    device_input_keyboard(keys,&clock,1); device_input_mouse(&dx,&dy,buttons,&clock);
    CHECK(binding_reads==reads_before); /* inactive hardware/Vision routes incur no scans */
    printf("PASS %u native touch holds/isolation/menu/lease/lifecycle checks\n", checks);
    return 0;
}
