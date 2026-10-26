/* Actual production bridge + DirectInput COM transport, deterministic time.
 * No hardware or original-game acceptance is implied by these host tests. */
#include <time.h>
#include <stdint.h>
static uint64_t gesture_test_time = UINT64_C(1000000000);
static int gesture_test_clock(clockid_t clock, struct timespec *out) {
    (void)clock; out->tv_sec = gesture_test_time / UINT64_C(1000000000);
    out->tv_nsec = gesture_test_time % UINT64_C(1000000000); return 0;
}
#define clock_gettime gesture_test_clock
#include "../halo_settings.c"
#undef clock_gettime
#define main existing_dinput_lifecycle_main
#include "test_dinput_lifecycle.c"
#undef main
static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
static uint64_t session, topology;
static HaloSenseFrame frame;
static DIObj *keyboard;
static void refresh(void) {
    halo_sense_observe_controller_for_session(session, &controller);
    halo_sense_publish(session, &frame);
}
static void begin(void) {
    if (keyboard) { CHECK(call(shim_dev_2, 1, keyboard->guest) == 0); keyboard = NULL; }
    memset(host_keyboard_state, 0, sizeof host_keyboard_state);
    unsetenv("HALO_PAD2KEY"); unsetenv("HALO_SENSE_WEAPON");
    S8(0x00718FC9, 0); S16(0x00718FA6, 0);
    S32(0x006F1D6C, 0x30000); S32(0x3000C, 10);
    controller = (HostGCSnapshot){.connected=true, .spatial=true, .source_generation=++topology};
    controller.buttons[HOSTGC_BTN_OPTIONS] = true;
    halo_sense_set_enabled(1); halo_sense_set_dual_enabled(0); halo_sense_set_gestures_enabled(1);
    session = halo_sense_begin_session(); halo_sense_observe_controller_for_session(session, &controller);
    halo_sense_set_gameplay_supported(1);
    HaloSenseHandPose hand = {.position={.3f,.1f,-.2f}, .forward={1,0,0}, .up={0,0,1}, .valid=1};
    frame = (HaloSenseFrame){.left=hand,.right=hand,.generation=1,.controller_generation=topology};
    refresh();
    keyboard = obj_new(DEV_KEYBOARD, 0); CHECK(keyboard != NULL); keyboard->buffer_size = 16;
    CHECK(call(shim_dev_7, 1, keyboard->guest) == DI_OK);
}
static int commit(uint64_t sequence, int action) {
    HaloSenseStatus status; halo_sense_status(&status);
    return halo_sense_gesture_enqueue(session, topology, status.generation, sequence, action);
}
static void next_tick(void) {
    gesture_test_time += UINT64_C(10000000); refresh();
    S32(0x3000C, G32(0x3000C) + 1);
}
static unsigned events(unsigned flags) {
    S32(0x240, 16);
    CHECK(call(shim_dev_10, 5, keyboard->guest, 16u, 0x400u, 0x240u, flags) == DI_OK);
    return G32(0x240);
}
static void snapshot(uint8_t keys[256]) {
    CHECK(call(shim_dev_9, 3, keyboard->guest, 256u, 0x2000u) == DI_OK);
    memcpy(keys, GPTR(0x2000), 256);
}
static void no_down(unsigned scan) {
    unsigned count = events(0);
    for (unsigned i=0; i<count; ++i) CHECK(G32(0x400+i*16) != scan || G32(0x404+i*16) == 0);
}
static void one_original_action(void) {
    uint8_t keys[256]; begin();
    CHECK(commit(1, HALO_SENSE_GESTURE_RELOAD)); CHECK(!commit(1, HALO_SENSE_GESTURE_RELOAD));
    controller.buttons[HOSTGC_BTN_OPTIONS] = false; refresh(); /* commit survives release */
    for (unsigned i=0; i<32; ++i) {
        host_dinput_keyboard_state(keys); /* same entry used by USER32 */
        CHECK(!keys[0x13] && !keys[0x12] && !keys[0x2D] && !keys[0x0F]);
    }
    next_tick(); /* USER32-only reads cannot spend the pulse before next DI read */
    CHECK(call(shim_dev_9, 3, keyboard->guest, 256u, 0x2000u) == DI_OK && G8(0x2013) == 0x80);
    for(unsigned i=0;i<8;i++) { snapshot(keys); CHECK(keys[0x13]==0x80); }
    CHECK(events(DIGDD_PEEK) == 1 && G32(0x400) == 0x13 && G32(0x404) == 0x80);
    uint32_t sequence = G32(0x40C);
    CHECK(events(DIGDD_PEEK) == 1 && G32(0x40C) == sequence);
    CHECK(events(0) == 1 && G32(0x40C) == sequence); CHECK(events(0) == 0);
    CHECK(call(shim_dev_7, 1, keyboard->guest) == DI_OK);
    CHECK(events(0) == 0); /* redundant Acquire cannot repeat the consumed down */
    next_tick(); CHECK(events(0) == 1 && G32(0x400) == 0x13 && G32(0x404) == 0);
    CHECK(events(0) == 0);
    for (int action=HALO_SENSE_GESTURE_SWAP; action<=HALO_SENSE_GESTURE_PICKUP; ++action) {
        begin(); CHECK(commit(1, action)); snapshot(keys);
        uint8_t expected = action == HALO_SENSE_GESTURE_SWAP ? 0x0F : 0x2D;
        CHECK(keys[expected] == 0x80 && !keys[0x12] && !keys[0x13]);
        CHECK(events(0) == 1 && G32(0x400) == expected && G32(0x404) == 0x80);
        next_tick(); CHECK(events(0) == 1 && G32(0x400) == expected && G32(0x404) == 0);
    }
}
static void cancel_queued(void) {
    uint8_t keys[256];
    for (unsigned reason=0; reason<10; ++reason) {
        begin(); CHECK(commit(1, HALO_SENSE_GESTURE_SWAP));
        CHECK(events(DIGDD_PEEK) == 1); /* owned down remains queued */
        switch (reason) {
        case 0: S16(0x00718FA6, 1); break;
        case 1: halo_sense_publish(session, NULL); break;
        case 2: controller.spatial=false; break;
        case 3: halo_sense_end_session(session); break;
        case 4: halo_sense_set_gestures_enabled(0); break;
        case 5: halo_sense_set_gameplay_supported(0); break;
        case 6: controller.source_generation=++topology; halo_sense_controller_changed(topology); break;
        case 7: S32(0x006F1D6C, 0x31000); S32(0x3100C, 0); break;
        case 8: S32(0x3000C, 9); break;
        case 9: controller.buttons[HOSTGC_BTN_MENU]=true; break;
        }
        snapshot(keys); CHECK(!keys[0x0F]); no_down(0x0F);
        CHECK(!commit(1, HALO_SENSE_GESTURE_SWAP));
    }
    begin(); CHECK(commit(1, HALO_SENSE_GESTURE_PICKUP));
    /* Keep valid controller/poses fresh while the committed action itself ages. */
    for (int i=0; i<3; ++i) { gesture_test_time += UINT64_C(90000000); refresh(); }
    snapshot(keys); CHECK(!keys[0x2D]); CHECK(!commit(1, HALO_SENSE_GESTURE_PICKUP));
    begin(); CHECK(commit(1, HALO_SENSE_GESTURE_RELOAD));
    setenv("HALO_PAD2KEY", "0", 1); snapshot(keys); CHECK(!keys[0x13]);
    unsetenv("HALO_PAD2KEY"); snapshot(keys); CHECK(!keys[0x13]);
    begin(); CHECK(commit(1, HALO_SENSE_GESTURE_RELOAD));
    CHECK(call(shim_dev_8, 1, keyboard->guest) == DI_OK);
    CHECK(call(shim_dev_7, 1, keyboard->guest) == DI_OK);
    CHECK(events(0) == 0); CHECK(!commit(1, HALO_SENSE_GESTURE_RELOAD));
}
static void ownership_and_release(void) {
    uint8_t keys[256]; begin(); HaloSenseStatus status; halo_sense_status(&status);
    CHECK(status.gesture_clutch && status.gestures_enabled);
    CHECK(!halo_sense_gesture_enqueue(session+1,topology,status.generation,1,1));
    CHECK(!halo_sense_gesture_enqueue(session,topology+1,status.generation,1,1));
    CHECK(!halo_sense_gesture_enqueue(session,topology,status.generation+1,1,1));
    CHECK(!commit(0,1)); CHECK(!commit(1,99));
    CHECK(commit(1,1)); snapshot(keys); CHECK(keys[0x13]);
    CHECK(!commit(2,1)); /* busy sequence cannot be retried later */
    next_tick(); snapshot(keys); CHECK(!keys[0x13]);
    CHECK(!commit(2,1)); CHECK(commit(3,1));
    snapshot(keys); CHECK(!keys[0x13]); /* complete release tick */
    next_tick(); snapshot(keys); CHECK(keys[0x13]);
    begin(); controller.buttons[HOSTGC_BTN_OPTIONS]=false; refresh(); CHECK(!commit(1,1));
    controller.buttons[HOSTGC_BTN_OPTIONS]=true; controller.rt=.5f; refresh(); CHECK(!commit(1,1));
    controller.rt=0; controller.buttons[HOSTGC_BTN_LSHOULDER]=true; refresh(); CHECK(!commit(1,1));
    controller.buttons[HOSTGC_BTN_LSHOULDER]=false; controller.rx=.3f; refresh(); CHECK(!commit(1,1));
    controller.rx=0; controller.ry=NAN; refresh(); CHECK(!commit(1,1));
    controller.ry=0; controller.buttons[HOSTGC_BTN_RTHUMB]=true; refresh(); CHECK(!commit(1,1));
    controller.buttons[HOSTGC_BTN_RTHUMB]=false;
    controller.spatial_raw_lx=.4f; refresh(); CHECK(!commit(1,1)); /* mapped lx is already0 */
    controller.spatial_raw_lx=0; controller.spatial_raw_ly=.3f; refresh(); CHECK(!commit(1,1));
    controller.spatial_raw_ly=0;
    const unsigned blockers[]={HOSTGC_BTN_A,HOSTGC_BTN_B,HOSTGC_BTN_X,HOSTGC_BTN_Y,
        HOSTGC_BTN_RSHOULDER,HOSTGC_BTN_LTHUMB,HOSTGC_BTN_HOME};
    for(unsigned i=0;i<sizeof blockers/sizeof *blockers;i++) {
        controller.buttons[blockers[i]]=true; refresh(); CHECK(!commit(1,1));
        controller.buttons[blockers[i]]=false;
    }
}
static void preserve_stock_controls(void) {
    uint8_t keys[256]; begin(); snapshot(keys); CHECK(!keys[0x3B]);
    controller.buttons[HOSTGC_BTN_RTHUMB]=true; snapshot(keys);
    CHECK(keys[0x3B] && !keys[0x2C]); /* Create+R3 scoreboard, no zoom */
    halo_sense_set_gestures_enabled(0); snapshot(keys);
    CHECK(keys[0x3B] && keys[0x2C]); /* original mapping when option is off */
    halo_sense_set_gestures_enabled(1); halo_sense_set_enabled(0); snapshot(keys);
    CHECK(keys[0x3B] && keys[0x2C]);
    begin(); controller.spatial=false; controller.buttons[HOSTGC_BTN_RTHUMB]=true;
    snapshot(keys); CHECK(keys[0x3B] && keys[0x2C]);
    begin(); controller.rx=.8f; snapshot(keys); CHECK(keys[0x3B]);
    begin(); controller.spatial_raw_lx=.4f; snapshot(keys); CHECK(keys[0x3B]);
    begin(); CHECK(commit(1,1)); host_keyboard_state[0x13]=0x80;
    CHECK(events(DIGDD_PEEK) == 1 && !keyboard->buf[keyboard->buf_head].gesture.action);
    halo_sense_publish(session,NULL);
    CHECK(events(0) == 1 && G32(0x400)==0x13 && G32(0x404)==0x80); /* physical key survives */
    for (int mapped=0; mapped<2; ++mapped) {
        begin(); CHECK(commit(1,1)); CHECK(events(DIGDD_PEEK)==1);
        if(mapped) {controller.spatial=false; controller.buttons[HOSTGC_BTN_X]=true;}
        else {host_keyboard_state[0x13]=0x80; halo_sense_publish(session,NULL);}
        unsigned count=events(0), reloads=0;
        for(unsigned i=0;i<count;i++) if(G32(0x400+i*16)==0x13&&G32(0x404+i*16)==0x80) ++reloads;
        CHECK(reloads==1); CHECK(events(0)==0); /* queued gesture transfers to real key once */
    }
    begin(); controller.buttons[HOSTGC_BTN_OPTIONS]=false; controller.buttons[HOSTGC_BTN_X]=true;
    snapshot(keys); CHECK(keys[0x12] && keys[0x13] && keys[0x2D]); /* legacy Square */
    halo_sense_set_dual_enabled(1); refresh(); HaloSenseDualInput dual;
    CHECK(halo_sense_dual_input(&dual) && dual.reload[0] && dual.reload[1]);
}
static void natural_input_delivery(void) {
    uint8_t keys[256];
    for(int action=HALO_SENSE_GESTURE_SWAP;action<=HALO_SENSE_GESTURE_MELEE;action+=2) {
        begin();controller.buttons[HOSTGC_BTN_OPTIONS]=false;refresh();
        CHECK(commit(1,action));CHECK(!commit(1,action));
        unsigned scan=action==HALO_SENSE_GESTURE_MELEE?0x21:0x0F;
        /* USER32 cannot consume the pulse before the original DI tick. */
        host_dinput_keyboard_state(keys);CHECK(!keys[scan]);next_tick();
        snapshot(keys);CHECK(keys[scan]==0x80);
        for(unsigned i=0;i<5;i++){snapshot(keys);CHECK(keys[scan]==0x80);}
        CHECK(events(DIGDD_PEEK)==1&&G32(0x400)==scan&&G32(0x404)==0x80);
        CHECK(events(0)==1);CHECK(events(0)==0);
        next_tick();snapshot(keys);CHECK(!keys[scan]);
        CHECK(events(0)==1&&G32(0x400)==scan&&G32(0x404)==0);
        CHECK(!commit(2,action)); /* distinct sequence still respects cooldown */
        for(unsigned i=0;i<8;i++){gesture_test_time+=UINT64_C(100000000);refresh();}
        CHECK(!commit(2,action)); /* a rejected retry is not resurrected */
        CHECK(commit(3,action));next_tick();snapshot(keys);CHECK(keys[scan]);
    }
    for(unsigned blocker=0;blocker<9;blocker++) {
        begin();controller.buttons[HOSTGC_BTN_OPTIONS]=false;refresh();
        CHECK(commit(1,HALO_SENSE_GESTURE_MELEE));CHECK(events(DIGDD_PEEK)==1);
        switch(blocker){
        case 0:controller.rt=.6f;break;
        case 1:controller.lt=.6f;break;
        case 2:controller.buttons[HOSTGC_BTN_MENU]=true;break;
        case 3:controller.rx=.7f;break;
        case 4:controller.buttons[HOSTGC_BTN_LSHOULDER]=true;break;
        case 5:halo_sense_app_input_changed(1);break;
        case 6:halo_sense_publish(session,NULL);break;
        case 7:controller.spatial=false;break;
        default:halo_sense_set_gestures_enabled(0);break;
        }
        if(blocker!=6)refresh();snapshot(keys);CHECK(!keys[0x21]);no_down(0x21);
        if(blocker==5)halo_sense_app_input_changed(0);
    }
    for(int action=HALO_SENSE_GESTURE_SWAP;action<=HALO_SENSE_GESTURE_MELEE;action+=2) {
        begin();controller.buttons[HOSTGC_BTN_OPTIONS]=false;controller.spatial_raw_ly=controller.ly=1;refresh();
        CHECK(commit(1,action));snapshot(keys);CHECK(keys[action==HALO_SENSE_GESTURE_MELEE?0x21:0x0F]);
    }
    /* Scoped admission still feeds the original F input, not a direct hit or
     * fabricated animation flag. The original engine owns unzoom/damage. */
    begin();controller.buttons[HOSTGC_BTN_OPTIONS]=false;refresh();
    HaloSenseHeadPose head={.forward={1,0,0},.up={0,0,1},.generation=1,.controller_generation=topology};
    halo_sense_publish_head(session,&head);halo_sense_set_scope_active(1);
    CHECK(commit(1,HALO_SENSE_GESTURE_MELEE));snapshot(keys);CHECK(keys[0x21]);
    halo_sense_set_scope_active(0);
    /* R1 remains the same original F input, irrespective of gesture setting. */
    begin();halo_sense_set_gestures_enabled(0);controller.buttons[HOSTGC_BTN_OPTIONS]=false;
    controller.buttons[HOSTGC_BTN_RSHOULDER]=true;snapshot(keys);CHECK(keys[0x21]);
    controller.spatial=false;snapshot(keys);CHECK(keys[0x21]);
    /* Reload and pickup still need deliberate Create; new melee does not. */
    begin();controller.buttons[HOSTGC_BTN_OPTIONS]=false;refresh();
    CHECK(!commit(1,HALO_SENSE_GESTURE_RELOAD));CHECK(!commit(1,HALO_SENSE_GESTURE_PICKUP));
    CHECK(commit(1,HALO_SENSE_GESTURE_MELEE));
}
int main(void) {
    engine_flat_base=calloc(1,8*1024*1024); assert(engine_flat_base);
    one_original_action(); cancel_queued(); ownership_and_release(); preserve_stock_controls(); natural_input_delivery();
    CHECK(call(shim_dev_2,1,keyboard->guest)==0); halo_sense_end_session(session); free(engine_flat_base);
    printf("PASS %u production Sense gesture bridge/DirectInput checks\n",checks);
    return 0;
}
