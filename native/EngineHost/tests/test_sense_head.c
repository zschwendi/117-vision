#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
static uint64_t head_test_time=UINT64_C(1000000000);
static int head_clock(clockid_t clock,struct timespec *out){(void)clock;out->tv_sec=head_test_time/1000000000;out->tv_nsec=head_test_time%1000000000;return 0;}
#define clock_gettime head_clock
#include "../halo_settings.c"
#undef clock_gettime
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static void publish(uint64_t session,HostGCSnapshot *p,HaloSenseHeadPose *h,HaloSenseFrame *f){
    halo_sense_observe_controller(p);if(f){f->left.trigger=p->lt;f->right.trigger=p->rt;halo_sense_publish(session,f);}halo_sense_publish_head(session,h);
}
/* Filtered left stick for one scoped read; the caller's pad is unchanged. */
static void move(HostGCSnapshot p,float lx,float ly,float *x,float *y){p.lx=lx;p.ly=ly;halo_sense_filter_gamepad(&p);*x=p.lx;*y=p.ly;}
static int near(float a,float b){return isfinite(a)&&fabsf(a-b)<.00001f;}
int main(void){
    unsetenv("HALO_SENSE_WEAPON");halo_sense_set_enabled(1);uint64_t s=halo_sense_begin_session();
    HostGCSnapshot p={.connected=true,.spatial=true,.source_generation=1,.rt=1};p.buttons[HOSTGC_BTN_RTRIGGER]=true;
    HaloSenseHeadPose h={.forward={0,-1,0},.up={0,0,1},.generation=1,.controller_generation=1,
        .movement_yaw=(float)M_PI_2,.movement_yaw_valid=1},read;
    HaloSenseFrame hands={.generation=1,.controller_generation=1};
    hands.left=hands.right=(HaloSenseHandPose){.forward={1,0,0},.up={0,0,1},.valid=1};
    HaloSenseScopeInput input;halo_sense_observe_controller_for_session(s,&p);halo_sense_set_gameplay_supported(1);
    publish(s,&p,&h,NULL);CHECK(halo_sense_head_read(&read));halo_sense_set_scope_active(1);
    publish(s,&p,&h,NULL);CHECK(!halo_sense_scope_input(&input)); /* no head-aim fallback */
    publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input)&&!input.armed);
    HostGCSnapshot q=p;q.lx=.2f;q.ly=1;q.rx=.8f;halo_sense_filter_gamepad(&q);
    CHECK(q.rt==0&&q.rx==0);CHECK(fabsf(q.lx-1)<.00001f&&fabsf(q.ly+.2f)<.00001f);
    p.rt=0;p.buttons[HOSTGC_BTN_RTRIGGER]=false;publish(s,&p,&h,&hands);
    CHECK(halo_sense_scope_input(&input)&&input.armed);uint64_t epoch=input.generation;
    p.rt=1;p.buttons[HOSTGC_BTN_RTRIGGER]=true;publish(s,&p,&h,&hands);q=p;halo_sense_filter_gamepad(&q);CHECK(q.rt==1);
    halo_sense_publish_head(s+1,NULL);CHECK(halo_sense_scope_input(&input)&&input.armed&&input.generation==epoch);
    halo_sense_publish(s,NULL);CHECK(!halo_sense_scope_input(&input));
    /* Lowered hands cannot fire, but fresh head permits zoom exit, melee,
     * and melee; raw L1 waits for a deliberate tracked throw release. */
    p.buttons[HOSTGC_BTN_LSHOULDER]=false;publish(s,&p,&h,NULL);
    p.buttons[HOSTGC_BTN_LSHOULDER]=true;p.buttons[HOSTGC_BTN_RTHUMB]=true;p.buttons[HOSTGC_BTN_RSHOULDER]=true;
    for(unsigned frame=0;frame<4;frame++){
        head_test_time+=UINT64_C(10000000);halo_sense_observe_controller(&p);
        halo_sense_publish_head(s,&h);halo_sense_publish(s,NULL);CHECK(!halo_sense_scope_input(&input));
        q=p;halo_sense_filter_gamepad(&q);CHECK(q.rt==0&&q.buttons[HOSTGC_BTN_RTHUMB]&&q.buttons[HOSTGC_BTN_RSHOULDER]&&!q.buttons[HOSTGC_BTN_LSHOULDER]);
    }
    publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input)&&!input.armed);q=p;halo_sense_filter_gamepad(&q);CHECK(!q.buttons[HOSTGC_BTN_LSHOULDER]);
    p.rt=0;p.buttons[HOSTGC_BTN_RTRIGGER]=p.buttons[HOSTGC_BTN_LSHOULDER]=false;publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input)&&input.armed);
    /* Menu ownership invalidates both direction sources and queued actions.
     * Publishing while blocked cannot rearm; normal controls remain intact. */
    halo_sense_app_input_changed(1);CHECK(halo_sense_app_input_blocked());publish(s,&p,&h,&hands);CHECK(!halo_sense_head_read(&read)&&!halo_sense_scope_input(&input));
    q=p;q.buttons[HOSTGC_BTN_LSHOULDER]=true;q.rt=1;halo_sense_filter_gamepad(&q);CHECK(!q.buttons[HOSTGC_BTN_LSHOULDER]&&q.rt==0);
    halo_sense_app_input_changed(0);p.rt=1;p.buttons[HOSTGC_BTN_RTRIGGER]=p.buttons[HOSTGC_BTN_LSHOULDER]=true;publish(s,&p,&h,&hands);
    CHECK(halo_sense_scope_input(&input)&&!input.armed);q=p;halo_sense_filter_gamepad(&q);CHECK(!q.buttons[HOSTGC_BTN_LSHOULDER]&&q.rt==0);
    p.rt=0;p.buttons[HOSTGC_BTN_RTRIGGER]=p.buttons[HOSTGC_BTN_LSHOULDER]=false;publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input)&&input.armed);
    HaloSenseHeadPose bad=h;bad.controller_generation=2;halo_sense_publish_head(s,&bad);CHECK(!halo_sense_scope_input(&input));
    bad=h;bad.forward[0]=NAN;halo_sense_publish_head(s,&bad);CHECK(!halo_sense_head_read(&read));
    bad=h;bad.up[1]=-1;halo_sense_publish_head(s,&bad);CHECK(!halo_sense_head_read(&read));
    publish(s,&p,&h,&hands);head_test_time+=UINT64_C(150000001);CHECK(!halo_sense_scope_input(&input));
    p.rt=1;p.buttons[HOSTGC_BTN_RTRIGGER]=true;publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input)&&!input.armed);
    p.buttons[HOSTGC_BTN_MENU]=true;halo_sense_observe_controller(&p);CHECK(!halo_sense_scope_input(&input));p.buttons[HOSTGC_BTN_MENU]=false;
    halo_sense_set_gameplay_supported(0);CHECK(!halo_sense_scope_input(&input));halo_sense_set_gameplay_supported(1);
    halo_sense_controller_changed(2);CHECK(!halo_sense_head_read(&read)&&!halo_sense_scope_input(&input));
    p.source_generation=h.controller_generation=hands.controller_generation=2;publish(s,&p,&h,&hands);halo_sense_set_scope_active(1);CHECK(halo_sense_scope_input(&input)&&!input.armed);
    /* Scoped movement uses the published pitch-free heading. Gaze tilted 105
     * degrees up (physical plus virtual pitch past vertical) points forward
     * backwards; stick-forward must still move along the heading. */
    float x,y;HaloSenseHeadPose over=h;
    over.forward[0]=-.25881905f;over.forward[1]=0;over.forward[2]=.96592583f;
    over.up[0]=.96592583f;over.up[1]=0;over.up[2]=.25881905f;over.movement_yaw=0;
    publish(s,&p,&over,&hands);CHECK(halo_sense_scope_input(&input));
    move(p,0,1,&x,&y);CHECK(near(x,0)&&near(y,1)); /* old forward-derived yaw gave y=-1 */
    move(p,1,0,&x,&y);CHECK(near(x,1)&&near(y,0));
    CHECK(input.head.forward[0]==over.forward[0]&&input.head.forward[2]==over.forward[2]&&input.head.up[0]==over.up[0]);
    CHECK(near(input.head.movement_yaw,0)&&input.head.movement_yaw_valid); /* aim keeps tilted gaze */
    over.forward[0]=0;over.forward[1]=.25881905f;over.up[0]=0;over.up[1]=.96592583f;over.up[2]=-.25881905f;
    over.movement_yaw=(float)M_PI_2;publish(s,&p,&over,&hands);CHECK(halo_sense_scope_input(&input));
    move(p,.2f,1,&x,&y);CHECK(near(x,1)&&near(y,-.2f)); /* right heading over the top: not reversed */
    /* Right-only/lowered left: no strict pair, so no scoped movement, no held
     * frame and no forward fallback; zoom exit and melee remain. */
    halo_sense_publish(s,NULL);halo_sense_publish_head(s,&over);CHECK(halo_sense_head_read(&read)&&!halo_sense_scope_input(&input));
    CHECK(read.movement_yaw_valid&&near(read.movement_yaw,(float)M_PI_2));
    q=p;q.lx=.2f;q.ly=1;halo_sense_filter_gamepad(&q);CHECK(q.lx==0&&q.ly==0&&q.buttons[HOSTGC_BTN_RTHUMB]&&q.buttons[HOSTGC_BTN_RSHOULDER]);
    /* Invalid headings keep scope aim but stop movement; wraps normalise. */
    float invalid[]={NAN,INFINITY,-INFINITY,7.f,-7.f};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++){
        over.movement_yaw=invalid[i];publish(s,&p,&over,&hands);
        CHECK(halo_sense_scope_input(&input)&&!input.head.movement_yaw_valid&&input.head.movement_yaw==0);
        move(p,.2f,1,&x,&y);CHECK(x==0&&y==0);
    }
    over.movement_yaw=(float)M_PI_2;over.movement_yaw_valid=0;publish(s,&p,&over,&hands);
    CHECK(halo_sense_scope_input(&input)&&!input.head.movement_yaw_valid);move(p,.2f,1,&x,&y);CHECK(x==0&&y==0);
    over.movement_yaw=6.18318531f;over.movement_yaw_valid=1;publish(s,&p,&over,&hands);
    CHECK(halo_sense_scope_input(&input)&&near(input.head.movement_yaw,-.1f));
    /* A rejected or unheaded packet never inherits the previous heading. */
    HaloSenseHeadPose broken=over;broken.forward[0]=NAN;halo_sense_publish_head(s,&broken);
    CHECK(!halo_sense_head_read(&read)&&!read.movement_yaw_valid);
    HaloSenseHeadPose unheaded=over;unheaded.movement_yaw_valid=0;publish(s,&p,&unheaded,&hands);
    CHECK(halo_sense_scope_input(&input));move(p,0,1,&x,&y);CHECK(x==0&&y==0);
    /* Stale head: scope and movement stop; reads return no heading. */
    publish(s,&p,&over,&hands);CHECK(halo_sense_scope_input(&input));head_test_time+=UINT64_C(150000001);
    CHECK(!halo_sense_scope_input(&input)&&!halo_sense_head_read(&read)&&!read.movement_yaw_valid&&read.movement_yaw==0);
    move(p,0,1,&x,&y);CHECK(x==0&&y==0);
    publish(s,&p,&h,&hands);CHECK(halo_sense_scope_input(&input));move(p,.2f,1,&x,&y);CHECK(near(x,1)&&near(y,-.2f));
    p.spatial=false;q=p;halo_sense_filter_gamepad(&q);CHECK(!memcmp(&p,&q,sizeof p)&&!halo_sense_head_read(&read));
    halo_sense_end_session(s);CHECK(!halo_sense_head_read(&read));
    printf("PASS %u head lifetime, weapon-directed scope rearm, L1 grenade and app ownership checks\n",checks);
}
