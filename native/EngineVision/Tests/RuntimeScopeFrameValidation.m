/* Production publication functions, with engine/GPU boundaries stubbed.
 * No game process, input, simulator or device is started. */
#include "../Sources/EngineVisionRuntime.m"
#include <assert.h>

int host_trace_imports, host_quit_requested, host_core_telemetry;
uint8_t *engine_flat_base;
uint64_t host_present_sleep_ns;
_Thread_local uint64_t host_thread_wait_ns, host_thread_waits;
uint64_t host_yield_spin_ns;
const char *mr_last_commit_error(void) { return "fixture failed commit"; }
static HaloPanoramaInfo fixture_panorama;
static HaloSenseWeaponActionStatus fixture_scope;
static int fixture_scope_valid;
static uint8_t fixture_layer[16];
int host_sense_scope_frame(HaloSenseWeaponActionStatus *out) { *out=fixture_scope; return fixture_scope_valid; }
int host_panorama_frame(HaloPanoramaInfo *out,const void *layers[HALO_PANORAMA_LAYERS]) {
    *out=fixture_panorama;
    for(unsigned i=0;i<HALO_PANORAMA_LAYERS;i++)layers[i]=fixture_layer;
    return fixture_panorama.status==HALO_PANORAMA_COMPLETE;
}
uint64_t host_pass_profile_total_ns(void) { return 0; }
uint64_t host_frame_pacer_present(const HaloFramePacerDisplay *d) { (void)d;return 0; }
void host_sense_reticle_art_frame(HaloSenseReticleArtFrame *out) { memset(out,0,sizeof *out); }
void host_log(const char *fmt,...) { (void)fmt; }
int mr_finish_dropped_frame(void) { return 0; }
int mr_commit_async(void (*callback)(void *,int),void *context) { callback(context,0);return 0; }

static EngineVisionFrameInfo read_info(void) {
    EngineVisionFrameInfo info={0};assert(enginevision_frame_info(&info));return info;
}
static void set_scope(uint64_t mode) {
    fixture_scope=(HaloSenseWeaponActionStatus){.session=1,.generation=2,.mode_generation=mode,.weapon=0x12340000,.action=HALO_SENSE_ACTION_SCOPE,.zoom_level=1};
    fixture_scope_valid=1;
    fixture_panorama=(HaloPanoramaInfo){.width=2,.height=2,.status=HALO_PANORAMA_FLAT};
}
static void expect_scope(uint64_t mode) {
    EngineVisionFrameInfo info=read_info();uint8_t bytes[16]={0};
    assert(enginevision_copy_latest_frame(bytes,sizeof bytes,&info));
    assert(info.scope.mode_generation==mode);
    if(mode)assert(info.scope.action==HALO_SENSE_ACTION_SCOPE&&info.scope.session==1&&info.scope.generation==2&&info.scope.weapon==0x12340000&&info.scope.zoom_level==1);
    else assert(info.scope.session==0&&info.scope.generation==0&&info.scope.weapon==0&&info.scope.action==0&&info.scope.zoom_level==0);
}
static void *publish_many(void *unused) {
    (void)unused;
    for(uint64_t mode=1001;mode<=1500;mode++){
        uint8_t pixels[16];memset(pixels,(unsigned char)mode,sizeof pixels);
        set_scope(mode);metalwin_present(pixels,2,2);
    }
    return NULL;
}
int main(void) {
    const uint8_t first[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    EngineVisionFrameInfo info={0};assert(!enginevision_frame_info(&info)&&info.scope.action==0);
    set_scope(1);metalwin_present(first,2,2);expect_scope(1);
    info=read_info();assert(info.sequence==1&&info.byte_count==16);
    uint8_t bytes[16]={0};assert(!enginevision_copy_latest_frame(bytes,15,&info));
    assert(info.scope.mode_generation==1); /* Metadata describes newest image even on short-buffer retry. */
    // Information and bytes may advance between calls; copy returns the exact
    // new frame's marker, never the marker from the earlier information hint.
    set_scope(2);metalwin_present(fixture_layer,2,2);
    assert(enginevision_copy_latest_frame(bytes,sizeof bytes,&info));
    assert(info.sequence==2&&info.scope.mode_generation==2&&!memcmp(bytes,fixture_layer,16));
    fixture_scope_valid=0;metalwin_present(first,2,2);expect_scope(0);
    set_scope(3);metalwin_present(first,2,2);expect_scope(3);
    fixture_panorama.status=HALO_PANORAMA_WORLD_INCOMPLETE;metalwin_present(first,2,2);expect_scope(0);
    set_scope(4);metalwin_present(first,2,2);expect_scope(4);
    fixture_panorama.status=HALO_PANORAMA_COMPLETE;metalwin_present(first,2,2);expect_scope(0);
    set_scope(5);metalwin_present(first,2,2);expect_scope(5);
    fixture_panorama.failure_reason=HALO_PANORAMA_RENDER_ABORTED;metalwin_present(first,2,2);expect_scope(0);
    set_scope(6);metalwin_present(first,2,2);expect_scope(6);
    fixture_panorama.status=HALO_PANORAMA_WORLD_INCOMPLETE;metalwin_present_dropped(2,2);expect_scope(0);
    set_scope(7);metalwin_present(first,2,2);expect_scope(7);
    fixture_panorama.status=HALO_PANORAMA_COMPLETE;metalwin_present_gpu(0,2,2);expect_scope(0);
    set_scope(8);metalwin_present(first,2,2);expect_scope(8);
    uint64_t old_sequence=read_info().sequence;metalwin_present(NULL,2,2);expect_scope(0);
    assert(read_info().sequence==old_sequence);
    set_scope(9);metalwin_present(first,2,2);expect_scope(9);
    metalwin_present(first,9000,2);expect_scope(0);
    for(int bad=0;bad<7;bad++){
        set_scope(10);
        switch(bad){case 0:fixture_scope.session=0;break;case 1:fixture_scope.generation=0;break;
        case 2:fixture_scope.mode_generation=0;break;case 3:fixture_scope.weapon=UINT32_MAX;break;
        case 4:fixture_scope.action=HALO_SENSE_ACTION_MELEE;break;case 5:fixture_scope.zoom_level=-1;break;
        default:fixture_scope.zoom_level=4;break;}
        metalwin_present(first,2,2);expect_scope(0);
    }
    uint8_t seed[16];memset(seed,(unsigned char)1000,sizeof seed);
    set_scope(1000);metalwin_present(seed,2,2);
    pthread_t producer;assert(!pthread_create(&producer,NULL,publish_many,NULL));
    for(unsigned read=0;read<2000;read++){
        EngineVisionFrameInfo racing_hint=read_info(),actual={0};uint8_t pixels[16];
        assert(enginevision_copy_latest_frame(pixels,sizeof pixels,&actual));
        assert(actual.sequence>=racing_hint.sequence&&actual.scope.mode_generation>=1000);
        for(unsigned i=0;i<sizeof pixels;i++)assert(pixels[i]==(unsigned char)actual.scope.mode_generation);
    }
    assert(!pthread_join(producer,NULL));
    puts("PASS production runtime scope frame: atomic copied identity, hint/copy advancement, ordinary/complete/incomplete/dropped/GPU/invalid transitions, malformed markers");
    return 0;
}
