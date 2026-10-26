/* Actual shared bridge and DI mouse reads, deterministic original tick. */
#include <time.h>
#include <stdint.h>
static uint64_t throw_time=UINT64_C(1000000000);
static int throw_clock(clockid_t c,struct timespec *t){(void)c;t->tv_sec=throw_time/1000000000;t->tv_nsec=throw_time%1000000000;return 0;}
#define clock_gettime throw_clock
#include "../halo_settings.c"
#undef clock_gettime
#define main existing_dinput_lifecycle_main
#include "test_dinput_lifecycle.c"
#undef main
static unsigned checks;static uint64_t session;static HaloSenseFrame hands;static HaloSenseHeadPose head;static HaloSenseGrenadeSample sample;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void refresh(int visible){
    halo_sense_observe_controller(&controller);halo_sense_publish_head(session,&head);
    halo_sense_publish(session,visible?&hands:NULL);halo_sense_publish_grenade(session,visible?&sample:NULL);
}
static void begin(void){
    static uint64_t source;unsetenv("HALO_PAD2KEY");unsetenv("HALO_SENSE_WEAPON");
    controller=(HostGCSnapshot){.connected=true,.spatial=true,.source_generation=++source};
    session=halo_sense_begin_session();halo_sense_observe_controller(&controller);halo_sense_set_gameplay_supported(1);
    HaloSenseHandPose p={.position={.4f,.25f,-.1f},.forward={1,0,0},.up={0,0,1},.valid=1};
    hands=(HaloSenseFrame){.left=p,.right=p,.generation=1,.controller_generation=source};
    head=(HaloSenseHeadPose){.forward={1,0,0},.up={0,0,1},.generation=1,.controller_generation=source};
    sample=(HaloSenseGrenadeSample){.hand=p,.velocity={2,0,1},.motion_valid=1,.generation=1,.controller_generation=source};
    S32(0x006F1D6C,0x30000);S32(0x3000C,10);S8(0x718FC9,0);S16(0x718FA6,0);refresh(1);refresh(1);
}
static void hold(void){controller.buttons[HOSTGC_BTN_LSHOULDER]=true;refresh(1);}
static void release(void){controller.buttons[HOSTGC_BTN_LSHOULDER]=false;refresh(1);}
static int mouse(void){uint8_t m[8];host_dinput_mouse_buttons_state(m);return m[1]!=0;}
/* The engine passes the original game clock and its tick at acceptance. */
static int accept_now(HaloSenseGrenadeRelease *g){uint32_t c=G32(0x006F1D6C);return halo_sense_grenade_accept(c,G32(c+12),g);}
static DIObj *buffered_begin(void){
    begin();setenv("HALO_PAD2KEY","0",1);
    DIObj *joy=obj_new(DEV_JOYSTICK,0);CHECK(joy);joy->buffer_size=16;joy->fmt_count=1;
    joy->fmt[0]=(FmtObj){0,SRC_BUTTON,HOSTGC_BTN_LSHOULDER};joy->data_size=1;
    CHECK(call(shim_dev_7,1,joy->guest)==DI_OK);hold();CHECK(call(shim_dev_25,1,joy->guest)==DI_OK);CHECK(!joy->buf_count);
    release();CHECK(call(shim_dev_25,1,joy->guest)==DI_OK);CHECK(joy->buf_count==1);
    CHECK(joy->buf[joy->buf_head].data==0x80&&joy->buf[joy->buf_head].grenade_epoch);
    return joy;
}
static unsigned buffered_downs(DIObj *joy,int peek){
    S32(0x240,16);CHECK(call(shim_dev_10,5,joy->guest,16u,0x400u,0x240u,peek?DIGDD_PEEK:0u)==DI_OK);
    unsigned downs=0;for(unsigned i=0;i<G32(0x240);i++)if(G32(0x404+16*i)==0x80)downs++;return downs;
}
static void buffered_release_checks(void){
    DIObj *joy=buffered_begin();CHECK(buffered_downs(joy,1)==1);CHECK(buffered_downs(joy,1)==1);
    CHECK(buffered_downs(joy,0)==1);CHECK(!buffered_downs(joy,0));CHECK(call(shim_dev_2,1,joy->guest)==0);
    for(unsigned change=0;change<8;change++){
        joy=buffered_begin();
        switch(change){
        case 0:head.generation++;refresh(1);break;
        case 1:controller.source_generation++;halo_sense_observe_controller(&controller);break;
        case 2:controller.buttons[HOSTGC_BTN_MENU]=true;halo_sense_observe_controller(&controller);controller.buttons[HOSTGC_BTN_MENU]=false;refresh(1);break;
        case 3:halo_sense_app_input_changed(1);break;
        case 4:halo_sense_set_gameplay_supported(0);break;
        case 5:throw_time+=160000000;refresh(1);break;
        case 6:S16(0x718FA6,1);break;
        default:throw_time+=1100000000;break;
        }
        CHECK(!buffered_downs(joy,1));CHECK(!buffered_downs(joy,0));
        CHECK(call(shim_dev_2,1,joy->guest)==0);halo_sense_app_input_changed(0);S16(0x718FA6,0);
    }
    joy=buffered_begin();controller.spatial=false;controller.buttons[HOSTGC_BTN_LSHOULDER]=true;
    CHECK(buffered_downs(joy,1)==1);CHECK(joy->buf_count==1&&!joy->buf[joy->buf_head].grenade_epoch&&!joy->buf[joy->buf_head].sense_app_epoch);
    CHECK(buffered_downs(joy,0)==1);CHECK(!buffered_downs(joy,0));CHECK(call(shim_dev_2,1,joy->guest)==0);
    unsetenv("HALO_PAD2KEY");
}
int main(void){
    engine_flat_base=calloc(1,0x900000);CHECK(engine_flat_base);halo_sense_set_enabled(1);
    begin();hold();for(unsigned i=0;i<20;i++)CHECK(!mouse());release();
    uint8_t keys[256];for(unsigned i=0;i<10;i++)host_dinput_keyboard_state(keys);
    S32(0x3000C,11);CHECK(mouse());for(unsigned i=0;i<10;i++)CHECK(mouse());
    /* Committed throw survives ordinary lowering, including the real publish order. */
    refresh(0);CHECK(mouse());HaloSenseGrenadeRelease got;CHECK(accept_now(&got)==1);
    CHECK(!memcmp(&got.sample,&sample,sizeof sample)&&got.epoch&&got.session==session);CHECK(accept_now(&got)==0);
    S32(0x3000C,12);CHECK(!mouse());CHECK(!mouse());
    /* Right accessory loss is independent of a fresh left-hand throw. */
    begin();hold();halo_sense_publish(session,NULL);halo_sense_publish_head(session,&head);
    halo_sense_publish_grenade(session,&sample);controller.buttons[HOSTGC_BTN_LSHOULDER]=false;halo_sense_observe_controller(&controller);
    CHECK(mouse());CHECK(accept_now(&got)==1);CHECK(accept_now(&got)==0);
    begin();hold();throw_time+=160000000;controller.buttons[HOSTGC_BTN_LSHOULDER]=false;halo_sense_observe_controller(&controller);CHECK(!mouse());
    begin();hold();release();throw_time+=160000000;refresh(1);CHECK(!mouse()); /* stale head cannot revive */
    begin();hold();release();controller.buttons[HOSTGC_BTN_MENU]=true;halo_sense_observe_controller(&controller);
    controller.buttons[HOSTGC_BTN_MENU]=false;refresh(1);CHECK(!mouse());
    begin();hold();release();S16(0x718FA6,1);CHECK(!mouse());S16(0x718FA6,0);refresh(1);CHECK(!mouse());
    begin();hold();release();halo_sense_app_input_changed(1);CHECK(!mouse());CHECK(accept_now(&got)==0);halo_sense_app_input_changed(0); /* never delivered: nothing to cancel */
    begin();hold();release();head.generation++;refresh(1);CHECK(!mouse());
    begin();hold();refresh(0);release();CHECK(!mouse()); /* loss while held requires a new deliberate press */
    begin();hold();release();CHECK(mouse());S32(0x3000C,9);CHECK(!mouse());
    /* Clock discontinuity cancels this throw, and a later released/held/released
     * cycle on the new clock is usable instead of permanently wedged. */
    S32(0x006F1D6C,0x31000);S32(0x3100C,2);refresh(1);hold();release();CHECK(mouse());
    begin();hold();release();controller.source_generation++;halo_sense_observe_controller(&controller);CHECK(!mouse());
    begin();hold();release();throw_time+=1100000000;CHECK(!mouse());
    controller.spatial=false;controller.buttons[HOSTGC_BTN_LSHOULDER]=true;CHECK(mouse()); /* stock pad unchanged */
    /* The original accepts in the tick its pulse was delivered: a release lost
     * since delivery is attributed to that acceptance as -1 (cancel, never
     * head aim), distinct from 0 (no Sense pulse this tick), once only. */
    for(unsigned loss=0;loss<5;loss++){
        begin();hold();release();CHECK(mouse());
        switch(loss){
        case 0:head.generation++;refresh(1);break;
        case 1:throw_time+=160000000;break;
        case 2:controller.buttons[HOSTGC_BTN_MENU]=true;halo_sense_observe_controller(&controller);controller.buttons[HOSTGC_BTN_MENU]=false;break;
        case 3:halo_sense_set_gameplay_supported(0);halo_sense_set_gameplay_supported(1);break;
        default:throw_time+=1100000000;refresh(1);break;
        }
        CHECK(accept_now(&got)==-1);CHECK(accept_now(&got)==0);
    }
    /* Rejected by the original's gate (no grenades/busy): nothing accepts in
     * that tick, the next tick's poll spends the pulse, and an ordinary pad's
     * throw after a handover (well within 2 s) is untouched. */
    begin();hold();release();CHECK(mouse());S32(0x3000C,11);CHECK(!mouse());CHECK(accept_now(&got)==0);
    controller.spatial=false;controller.buttons[HOSTGC_BTN_LSHOULDER]=true;halo_sense_observe_controller(&controller);
    throw_time+=500000000;S32(0x3000C,12);CHECK(mouse());CHECK(accept_now(&got)==0);
    controller.spatial=true;controller.buttons[HOSTGC_BTN_LSHOULDER]=false;
    /* ...also when the rejected release was then lost (no stale cancellation). */
    begin();hold();release();CHECK(mouse());head.generation++;refresh(1);S32(0x3000C,11);CHECK(!mouse());
    CHECK(accept_now(&got)==0);
    /* Handover through the real ordinary DI routes, with no synthetic clear:
     * an unused Sense pulse (original busy) never cancels or binds the
     * ordinary pad's throw, in the next tick or the same tick; the ordinary
     * button reaches the game unchanged. */
    for(unsigned same_tick=0;same_tick<2;same_tick++){
        begin();hold();release();CHECK(mouse());
        controller.spatial=false;controller.source_generation++;controller.buttons[HOSTGC_BTN_LSHOULDER]=true;
        if(!same_tick)S32(0x3000C,11);
        CHECK(mouse());CHECK(accept_now(&got)==0);CHECK(mouse());
        controller.spatial=true;controller.buttons[HOSTGC_BTN_LSHOULDER]=false;
    }
    {   /* Buffered joystick route: same outcome, one ordinary down delivered. */
        DIObj *joy=buffered_begin();controller.spatial=false;controller.source_generation++;controller.buttons[HOSTGC_BTN_LSHOULDER]=true;
        S32(0x3000C,11);CHECK(call(shim_dev_25,1,joy->guest)==DI_OK);CHECK(accept_now(&got)==0);
        CHECK(buffered_downs(joy,0)==1);CHECK(call(shim_dev_2,1,joy->guest)==0);unsetenv("HALO_PAD2KEY");
        controller.spatial=true;controller.buttons[HOSTGC_BTN_LSHOULDER]=false;
    }
    /* Tied to the original game clock: a later tick of the same frame (no DI
     * poll between ticks) is still this delivery; another clock, an earlier
     * tick, or a Sense poll of a later tick spends it. */
    begin();hold();release();CHECK(mouse());
    CHECK(halo_sense_grenade_accept(0x30000,11,&got)==1);
    begin();hold();release();CHECK(mouse());CHECK(halo_sense_grenade_accept(0x31000,10,&got)==0);CHECK(accept_now(&got)==0);
    begin();S32(0x3000C,10);hold();release();CHECK(mouse());CHECK(halo_sense_grenade_accept(0x30000,9,&got)==0);CHECK(accept_now(&got)==0);
    begin();hold();release();CHECK(mouse());S32(0x3000C,11);CHECK(!mouse());CHECK(halo_sense_grenade_accept(0x30000,11,&got)==0);
    /* Rapid L1 cycles: a release delivered but not yet accepted is never
     * overwritten in its tick; the accepted one is the first. A second
     * release during the original throw pulses once, is not accepted (the
     * original is busy), and is spent at the next tick's poll. */
    begin();HaloSenseGrenadeSample first=sample;hold();release();CHECK(mouse());
    sample.velocity[0]=-1;refresh(1);hold();release();CHECK(mouse());
    CHECK(accept_now(&got)==1&&!memcmp(&got.sample,&first,sizeof first));
    hold();release();CHECK(mouse()); /* same tick: still the first pulse, nothing new committed */
    S32(0x3000C,11);CHECK(!mouse());refresh(1);hold();release();S32(0x3000C,12);CHECK(mouse());
    S32(0x3000C,13);CHECK(!mouse());CHECK(accept_now(&got)==0);
    sample=first;
    /* A newer release after a lost one commits normally on the new generation. */
    begin();hold();release();CHECK(mouse());head.generation++;sample.generation=hands.generation=head.generation;
    refresh(1);refresh(1); /* a new input generation re-arms only after an observed L1-up */
    S32(0x3000C,11);CHECK(!mouse());hold();release();S32(0x3000C,13);CHECK(mouse());
    CHECK(accept_now(&got)==1&&got.sample.generation==head.generation);
    /* No measured motion (occluded/stale anchor): nothing commits, no pulse. */
    begin();sample.motion_valid=0;refresh(1);hold();release();CHECK(!mouse());CHECK(accept_now(&got)==0);
    /* A valid still hand commits (the engine drops it from the hand). */
    begin();sample.velocity[0]=sample.velocity[1]=sample.velocity[2]=0;refresh(1);hold();release();CHECK(mouse());
    CHECK(accept_now(&got)==1&&got.sample.motion_valid&&got.sample.velocity[0]==0);
    /* Age is the release sample's own publication age on the bridge clock. */
    begin();hold();throw_time+=20000000;controller.buttons[HOSTGC_BTN_LSHOULDER]=false;halo_sense_observe_controller(&controller);
    CHECK(mouse());throw_time+=30000000;CHECK(accept_now(&got)==1&&got.age_ns==UINT64_C(50000000));
    /* The armed-hold read behind the held-grenade presentation: only an
     * armed L1 hold with fresh ownership; reading it never pulses, commits,
     * spends or alters the release that follows. */
    HaloSenseGrenadeHold held;
    begin();CHECK(!halo_sense_grenade_hold(&held)&&!held.held&&held.session==session);
    hold();CHECK(halo_sense_grenade_hold(&held)&&held.held&&held.session==session&&held.controller_generation==controller.source_generation);
    HaloSenseStatus st;halo_sense_status(&st);CHECK(held.generation==st.generation);
    for(unsigned i=0;i<6;i++){CHECK(halo_sense_grenade_hold(&held));CHECK(!mouse());}
    CHECK(accept_now(&got)==0);
    release();CHECK(!halo_sense_grenade_hold(&held));CHECK(mouse());CHECK(accept_now(&got)==1&&!memcmp(&got.sample,&sample,sizeof sample));
    hold();CHECK(halo_sense_grenade_hold(&held));release(); /* a later hold is armed by the observed release */
    begin();controller.buttons[HOSTGC_BTN_LSHOULDER]=true;head.generation++;sample.generation=hands.generation=head.generation;refresh(1);
    CHECK(!halo_sense_grenade_hold(&held)); /* L1 already down across a new generation: not armed */
    release();hold();CHECK(halo_sense_grenade_hold(&held));release();
    for(unsigned loss=0;loss<7;loss++){
        begin();hold();CHECK(halo_sense_grenade_hold(&held));
        switch(loss){
        case 0:throw_time+=160000000;break;                                         /* stale pad/head/hands */
        case 1:refresh(0);break;                                                    /* occluded hand */
        case 2:controller.buttons[HOSTGC_BTN_MENU]=true;halo_sense_observe_controller(&controller);break;
        case 3:halo_sense_app_input_changed(1);break;
        case 4:controller.source_generation++;halo_sense_observe_controller(&controller);break; /* disconnect/reconnect */
        case 5:halo_sense_set_gameplay_supported(0);break;
        default:controller.spatial=false;halo_sense_observe_controller(&controller);break; /* ordinary pad */
        }
        CHECK(!halo_sense_grenade_hold(&held));if(loss!=6)CHECK(!mouse()); /* the ordinary pad's own L1 stays stock */
        halo_sense_app_input_changed(0);controller.buttons[HOSTGC_BTN_MENU]=false;controller.spatial=true;
    }
    buffered_release_checks();
    free(engine_flat_base);printf("PASS %u armed-hold read (no pulse/commit, ownership loss), real DI hold/release, retained pose, ownership, acceptance attribution (accept/cancel/none) on the original clock, rejected-pulse expiry and real mouse/buffered-joystick pad handover, rapid L1 cycles, motion-required commit, sample age, clock recovery and ordinary-pad checks\n",checks);
}
