/* Semantic weapon haptics on each half of a Sense pair through the production
 * selection, link guard, queue and engine cache paths. Controllers and Core
 * Haptics engines are mocks: no hardware, discovery or playback. The Sense
 * owner lease is a controllable stub of halo_sense_weapon_haptics_admit.
 * clang -fobjc-arc -fblocks -framework Foundation -framework GameController
 *   -framework CoreHaptics test_controller_spatial_haptics.m (macOS 26+). */
#define HOSTGC_SPATIAL_TESTING 1
#import "../gamecontroller.m"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

void host_log(const char *fmt, ...) {}
void halo_sense_controller_changed(uint64_t generation) {}
void halo_sense_app_input_changed(int blocked) {}
static uint64_t live_session = 1, live_generation = 1, live_lease = 1;
static int toggle_on = 1;
static unsigned admit_calls;
static int master_on = 1;
int halo_haptics_enabled(void) { return master_on; }
int halo_sense_weapon_haptics_admit(uint64_t session, uint64_t generation, uint64_t lease) {
    admit_calls++;
    return toggle_on && session == live_session && generation == live_generation && lease == live_lease;
}
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); abort(); } } while (0)

@interface MockEngine : NSObject
@property(copy) CHHapticEngineResetHandler resetHandler;
@property(copy) CHHapticEngineStoppedHandler stoppedHandler;
@property(copy) void (^onStart)(void);
@property(copy) void (^onCreatePlayer)(void);
@property BOOL autoShutdownEnabled;
@property NSUInteger starts, stops, plays, lastEventCount;
@property float lastIntensity;
@end
@interface MockPlayer : NSObject
@property(weak) MockEngine *engine;
@end
@implementation MockEngine
- (BOOL)startAndReturnError:(NSError **)error {
    self.starts++;
    void (^callback)(void) = self.onStart; self.onStart = nil;
    if (callback) callback(); /* a framework Start that blocked while ownership changed */
    return YES;
}
- (void)stopWithCompletionHandler:(CHHapticCompletionHandler)completion { self.stops++; if (completion) completion(nil); }
- (id<CHHapticPatternPlayer>)createPlayerWithPattern:(CHHapticPattern *)pattern error:(NSError **)error {
    NSDictionary *exported = [pattern exportDictionaryAndReturnError:nil];
    NSArray *events = exported[CHHapticPatternKeyPattern];
    self.lastEventCount = events.count;
    for (NSDictionary *entry in events) {
        NSDictionary *event = entry[CHHapticPatternKeyEvent];
        if (![event[CHHapticPatternKeyEventType] isEqual:CHHapticEventTypeHapticTransient]) continue;
        for (NSDictionary *parameter in event[CHHapticPatternKeyEventParameters])
            if ([parameter[CHHapticPatternKeyParameterID] isEqual:CHHapticEventParameterIDHapticIntensity])
                self.lastIntensity = [parameter[CHHapticPatternKeyParameterValue] floatValue];
    }
    MockPlayer *player = [MockPlayer new]; player.engine = self;
    void (^callback)(void) = self.onCreatePlayer; self.onCreatePlayer = nil;
    if (callback) callback(); /* player creation that blocked while ownership changed */
    return (id<CHHapticPatternPlayer>)player;
}
@end
@implementation MockPlayer
- (BOOL)startAtTime:(NSTimeInterval)time error:(NSError **)error { self.engine.plays++; return YES; }
@end
@interface MockHaptics : NSObject
@property(strong) NSMutableArray<MockEngine *> *engines;
@end
@implementation MockHaptics
- (instancetype)init { if ((self = [super init])) _engines = [NSMutableArray array]; return self; }
- (CHHapticEngine *)createEngineWithLocality:(GCHapticsLocality)locality {
    MockEngine *engine = [MockEngine new]; [self.engines addObject:engine]; return (CHHapticEngine *)engine;
}
@end
@interface TestController : NSObject
@property(nonatomic, strong) MockHaptics *mockHaptics;
@property(nonatomic) BOOL ordinary;
@end
@implementation TestController
- (NSString *)productCategory { return self.ordinary ? @"DualSense" : GCProductCategorySpatialController; }
- (NSString *)vendorName { return self.ordinary ? @"pad test" : @"Spatial test"; }
- (GCExtendedGamepad *)extendedGamepad { return self.ordinary ? (GCExtendedGamepad *)self : nil; }
- (GCDeviceHaptics *)haptics { return (GCDeviceHaptics *)self.mockHaptics; }
@end

static TestController *device(BOOL ordinary) {
    TestController *c = [TestController new]; c.mockHaptics = [MockHaptics new]; c.ordinary = ordinary; return c;
}
static MockEngine *engine(TestController *c) { return c.mockHaptics.engines.lastObject; }
static void devices(NSArray *controllers, id current) {
    g_test_devices = controllers; g_test_current = current;
    controller_connection_changed(nil);
}
static void assign_side(TestController *controller, NSInteger side) {
    (void)spatial_side((GCController *)controller);
    for (HostGCSpatialBinding *entry in g_spatial_bindings)
        if (entry.controller == (id)controller) { complete_spatial_binding(entry, side); return; }
    abort();
}
static void drain(void) { dispatch_sync(haptic_queue(), ^{}); }
static HostGCSpatialHaptic shot(float left, float right) {
    return (HostGCSpatialHaptic){.intensity = {left, right}, .sharpness = {.48f, .6f}, .tail = {0, .06f},
        .source_generation = hostgc_source_generation(), .session = live_session, .generation = live_generation,
        .lease = live_lease, .kind = HOSTGC_SPATIAL_SHOT};
}
static void needs_start(TestController *c) { engine(c).stoppedHandler(CHHapticEngineStoppedReasonSystemError); }

int main(void) { @autoreleasepool {
    if (@available(macOS 26.0, *)) {} else { fprintf(stderr, "requires macOS 26+\n"); return 77; }
    g_inited = true; g_guard_enabled = false; /* off hardware discovery; guard tested below */
    TestController *left = device(NO), *right = device(NO), *pad = device(YES);
    devices(@[right, left], right); /* discovery order must not decide sides */
    assign_side(left, 1); assign_side(right, 2);

    /* One event: firing right hand plus a lighter supporting left echo, each
     * on its own controller's actuator; the right adds its continuous tail. */
    HostGCSpatialHaptic event = shot(.18f, .40f);
    CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == 1 && engine(right).plays == 1);
    CHECK(fabsf(engine(left).lastIntensity - .18f) < 1e-6f && fabsf(engine(right).lastIntensity - .40f) < 1e-6f);
    CHECK(engine(left).lastEventCount == 1 && engine(right).lastEventCount == 2);
    CHECK(g_haptic_slots[0].pad == (id)left && g_haptic_slots[1].pad == (id)right);
    event = shot(0, .40f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == 1 && engine(right).plays == 2);
    event = shot(.6f, 0); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == 2 && engine(right).plays == 2);
    /* The audio-derived pulse shares the right slot's engine, not a second one. */
    hostgc_play_haptic(.5f, .5f); drain();
    CHECK(engine(right).plays == 3 && right.mockHaptics.engines.count == 1);
    HostGCSpatialHapticStats stats; hostgc_spatial_haptic_stats(&stats);
    CHECK(stats.requested == 3 && stats.queued == 3 && stats.played == 3);

    /* Nothing to play, an unknown kind, a topology change or a revoked
     * lease before admission never queues. */
    event = shot(0, 0); CHECK(!hostgc_play_spatial_haptic(&event));
    event = shot(NAN, -1); CHECK(!hostgc_play_spatial_haptic(&event));
    event = shot(.2f, .4f); event.kind = 7; CHECK(!hostgc_play_spatial_haptic(&event));
    event = shot(.2f, .4f); event.source_generation--; CHECK(!hostgc_play_spatial_haptic(&event));
    event = shot(.2f, .4f); event.lease++; CHECK(!hostgc_play_spatial_haptic(&event));
    hostgc_spatial_haptic_stats(&stats); CHECK(stats.dropped_stale == 1 && stats.dropped_cancelled == 1 && stats.queued == 3);

    /* Queued work is rechecked when it runs: a reconnect, an explicit
     * cancel, native UI taking input, arriving too late, and -- without any
     * engine tick -- a changed Sense session, input generation or toggle. */
    struct { int what; } cases[] = {{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}};
    unsigned l = engine(left).plays, r = engine(right).plays;
    for (unsigned n = 0; n < sizeof cases / sizeof *cases; ++n) {
        dispatch_suspend(haptic_queue());
        event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event));
        uint64_t token = 0;
        switch (cases[n].what) {
        case 0: controller_connection_changed(nil); break;
        case 1: hostgc_cancel_spatial_haptics(HOSTGC_SPATIAL_ALL); break;
        case 2: token = hostgc_app_input_begin(); break;
        case 3: usleep(120000); break;
        case 4: live_session++; break;
        case 5: live_generation++; break;
        case 6: toggle_on = 0; break;
        case 7: master_on = 0; break; /* halo_haptics_set_enabled(0) while queued */
        }
        dispatch_resume(haptic_queue()); drain();
        CHECK(engine(left).plays == l && engine(right).plays == r);
        if (token) { hostgc_app_input_end(token); g_app_wait_release = 0; }
        toggle_on = 1; master_on = 1;
    }
    /* A cancel of charge pulses leaves an accepted shot queued with it. */
    dispatch_suspend(haptic_queue());
    HostGCSpatialHaptic charge = shot(0, .3f); charge.kind = HOSTGC_SPATIAL_CHARGE;
    CHECK(hostgc_play_spatial_haptic(&charge));
    event = shot(0, .4f); CHECK(hostgc_play_spatial_haptic(&event));
    hostgc_cancel_spatial_haptics(HOSTGC_SPATIAL_CHARGE);
    dispatch_resume(haptic_queue()); drain();
    CHECK(engine(right).plays == r + 1 && fabsf(engine(right).lastIntensity - .4f) < 1e-6f); r++;

    /* Ownership can end while a (possibly blocking) engine Start runs: the
     * lease is rechecked after it, immediately before the player. */
    needs_start(left); needs_start(right);
    engine(left).onStart = ^{ live_generation++; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l && engine(right).plays == r);
    /* ...and between the two hands: the left played, the right must not. */
    needs_start(right);
    engine(right).onStart = ^{ controller_connection_changed(nil); };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r); l++;
    needs_start(right);
    engine(right).onStart = ^{ live_session++; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r); l++;
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r + 1); l++; r++;
    /* The master enable switched off during a blocking Start cancels too. */
    needs_start(left);
    engine(left).onStart = ^{ master_on = 0; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l && engine(right).plays == r); master_on = 1;
    /* Player creation is another framework call: the lease is rechecked
     * after it returns, immediately before the player starts. */
    engine(left).onCreatePlayer = ^{ live_session++; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l && engine(right).plays == r);
    engine(right).onCreatePlayer = ^{ live_lease++; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r); l++;
    engine(left).onCreatePlayer = ^{ controller_connection_changed(nil); };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l && engine(right).plays == r);
    engine(right).onCreatePlayer = ^{ master_on = 0; };
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r); l++; master_on = 1;
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r + 1); l++; r++;
    hostgc_spatial_haptic_stats(&stats);
    CHECK(stats.dropped_stale == 5 && stats.dropped_cancelled == 14 && stats.dropped_not_spatial == 0);

    /* The link guard admits one pulse per interval for the whole event, so
     * both links still see at most one; the audio path shares that budget. A
     * revoked lease never takes the slot. */
    g_guard_enabled = true; hostgc_guard_init(&g_guard, 250000000ull, 0); hostgc_guard_connected(&g_guard, now_ns());
    toggle_on = 0; event = shot(.2f, .4f); CHECK(!hostgc_play_spatial_haptic(&event) && g_guard.admitted == 0); toggle_on = 1;
    master_on = 0; event = shot(.2f, .4f); CHECK(!hostgc_play_spatial_haptic(&event) && g_guard.admitted == 0); master_on = 1;
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event));
    event = shot(.2f, .4f); CHECK(!hostgc_play_spatial_haptic(&event));
    hostgc_play_haptic(1, .5f); drain();
    CHECK(engine(left).plays == l + 1 && engine(right).plays == r + 1 && g_guard.admitted == 1 && g_guard.dropped_rate == 2);
    hostgc_guard_disconnected(&g_guard, now_ns(), 0);
    event = shot(.2f, .4f); CHECK(!hostgc_play_spatial_haptic(&event) && g_guard.dropped_disconnected == 1);
    g_guard_enabled = false;

    /* A disconnect releases both halves' engines at once. */
    haptic_release_on_disconnect(); drain();
    CHECK(!g_haptic_slots[0].engine && !g_haptic_slots[1].engine);

    /* An ordinary gamepad keeps its audio-derived path; semantic Sense events
     * are not redirected to it. */
    devices(@[pad], pad);
    event = shot(.2f, .4f); CHECK(hostgc_play_spatial_haptic(&event)); drain();
    hostgc_spatial_haptic_stats(&stats); CHECK(stats.dropped_not_spatial == 1 && !pad.mockHaptics.engines.count);
    hostgc_play_haptic(.8f, .3f); drain();
    CHECK(engine(pad).plays == 1 && !g_haptic_slots[0].engine);
    pthread_mutex_lock(&g_haptic_play_lock); haptic_discard_slot(1); pthread_mutex_unlock(&g_haptic_play_lock);
    printf("PASS %u spatial weapon haptic checks: per-side actuators, support echo, shared right engine, leased events rechecked at queue, after engine Start, after player creation (before player start) and between hands (session, generation, lease, toggle, master enable, topology, cancel, UI, age), charge-only cancel, one guard admission per event, revoked lease takes no slot, disconnect release, ordinary pad unchanged; mocked hardware\n", checks);
}}
