/* Hardware-free integration test of production selection/capture/cache paths.
 * clang -fobjc-arc -fblocks -framework Foundation -framework GameController
 *   -framework CoreHaptics test_gamecontroller_spatial.m -o /tmp/test-spatial
 * Run on macOS 26+ (spatial GameController input aliases). */
#define HOSTGC_SPATIAL_TESTING 1
#define halo_sense_controller_changed test_sense_controller_changed
#import "../gamecontroller.m"
#undef halo_sense_controller_changed
#include "../halo_settings.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>

void host_log(const char *fmt, ...) {}
int halo_haptics_enabled(void) { return 1; }
static uint64_t notified_generation;
static unsigned source_notifications;
void test_sense_controller_changed(uint64_t generation) {
    notified_generation = generation; source_notifications++;
    halo_sense_controller_changed(generation);
}
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); abort(); } } while (0)

/* These objects model Apple's public selector contract, not hardware. Values
 * change only through the test, and capture returns an isolated copy. */
@interface TestValue : NSObject
@property(nonatomic) float value;
@property(nonatomic, getter=isPressed) BOOL pressed;
@end
@implementation TestValue
- (id)pressedInput { return self; }
@end
@interface TestStick : NSObject
@property(nonatomic, strong) TestValue *xAxis, *yAxis;
@end
@implementation TestStick
@end
@interface TestInput : NSObject
@property(nonatomic, strong) NSMutableDictionary *buttons, *dpads;
@property(nonatomic, copy) void (^duringCapture)(void);
@end
@implementation TestInput
- (id)capture {
    if (self.duringCapture) self.duringCapture();
    TestInput *copy = [TestInput new];
    copy.buttons = [NSMutableDictionary new]; copy.dpads = [NSMutableDictionary new];
    for (NSString *key in self.buttons) {
        TestValue *value = [TestValue new];
        TestValue *old = self.buttons[key];
        value.value = old.value; value.pressed = old.pressed;
        copy.buttons[key] = value;
    }
    for (NSString *key in self.dpads) {
        TestStick *stick = [TestStick new], *old = self.dpads[key];
        stick.xAxis = [TestValue new]; stick.yAxis = [TestValue new];
        stick.xAxis.value = old.xAxis.value; stick.yAxis.value = old.yAxis.value;
        copy.dpads[key] = stick;
    }
    return copy;
}
@end
@interface TestController : NSObject
@property(nonatomic, strong) TestInput *input;
@end
@implementation TestController
- (NSString *)productCategory { return GCProductCategorySpatialController; }
- (NSString *)vendorName { return @"Spatial test"; }
- (GCExtendedGamepad *)extendedGamepad { return nil; }
@end

static TestController *half(void) {
    TestController *device = [TestController new];
    device.input = [TestInput new];
    device.input.buttons = [NSMutableDictionary new];
    for (NSString *alias in @[GCInputButtonA, GCInputButtonB, GCInputGripButton, GCInputTrigger,
                             GCInputThumbstickButton, GCInputButtonMenu, GCInputButtonHome])
        device.input.buttons[alias] = [TestValue new];
    TestStick *stick = [TestStick new];
    stick.xAxis = [TestValue new]; stick.yAxis = [TestValue new];
    device.input.dpads = [@{GCInputThumbstick:stick} mutableCopy];
    return device;
}
static HostGCSpatialBinding *binding(TestController *controller) {
    (void)spatial_side((GCController *)controller);
    for (HostGCSpatialBinding *entry in g_spatial_bindings)
        if (entry.controller == (id)controller) return entry;
    abort();
}
static void assign_side(TestController *controller, NSInteger side) {
    complete_spatial_binding(binding(controller), side);
}
static void devices(NSArray *controllers, id current) {
    g_test_devices = controllers; g_test_current = current;
    controller_connection_changed(nil);
}
static void press(TestController *device, NSString *alias, float value) {
    TestValue *button = device.input.buttons[alias];
    button.value = value; button.pressed = value > .5f;
    controller_topology_changed(nil); /* force a fresh report for each assertion */
}
static void stick(TestController *device, float x, float y) {
    TestStick *stick = device.input.dpads[GCInputThumbstick];
    stick.xAxis.value = x; stick.yAxis.value = y;
    controller_topology_changed(nil);
}
static void released(const HostGCSnapshot *snapshot) {
    CHECK(!snapshot->connected && !snapshot->spatial);
    CHECK(snapshot->lx == 0 && snapshot->ly == 0 && snapshot->rx == 0 && snapshot->ry == 0);
    CHECK(snapshot->lt == 0 && snapshot->rt == 0);
    for (unsigned i = 0; i < HOSTGC_BUTTON_COUNT; ++i) CHECK(!snapshot->buttons[i]);
}

int main(void) { @autoreleasepool {
    if (@available(macOS 26.0, *)) {} else { fprintf(stderr, "requires macOS 26+\n"); return 77; }
    TestController *left = half(), *right = half();
    CHECK(hostgc_source_generation() != 0);
    HostGCSnapshot out;
    devices(@[right, left], right); /* reversed discovery order is deliberate */
    CHECK(!hostgc_poll(&out)); released(&out);
    CHECK(strstr(hostgc_input_state(), "Identifying"));
    assign_side(left, 1); assign_side(right, 2);
    CHECK(hostgc_poll(&out) && hostgc_connected() && out.spatial);
    CHECK(out.source_generation == hostgc_source_generation() && notified_generation == out.source_generation);
    uint64_t source_generation = out.source_generation;
    unsigned notifications = source_notifications;
    controller_topology_changed(nil); /* current controller/cache-only change */
    CHECK(hostgc_source_generation() == source_generation && source_notifications == notifications);
    CHECK(hostgc_poll(&out) && out.source_generation == source_generation);
    /* Exercise the production native notification -> pose bridge path with
     * its real mutex, before any Swift observer/compositor can intervene. */
    uint64_t session = halo_sense_begin_session();
    halo_sense_set_enabled(1); unsetenv("HALO_SENSE_WEAPON");
    halo_sense_observe_controller_for_session(session, &out);
    HaloSenseHandPose hand = {.forward={1,0,0},.up={0,0,1},.trigger=0,.valid=1};
    HaloSenseFrame pose = {.left=hand,.right=hand,.generation=1,.controller_generation=source_generation}, observed_pose;
    halo_sense_publish(session, &pose); CHECK(halo_sense_read(&observed_pose));
    HostGCSnapshot stale_capture = out;
    controller_connection_changed(nil);
    CHECK(!halo_sense_read(&observed_pose) && halo_sense_active());
    halo_sense_observe_controller(&stale_capture); halo_sense_publish(session, &pose);
    CHECK(!halo_sense_read(&observed_pose));
    CHECK(hostgc_poll(&out) && out.source_generation != source_generation);
    halo_sense_observe_controller(&out); halo_sense_publish(session, &pose);
    CHECK(!halo_sense_read(&observed_pose));
    pose.controller_generation = out.source_generation; halo_sense_publish(session, &pose);
    CHECK(halo_sense_read(&observed_pose));
    halo_sense_end_session(session);
    source_generation = out.source_generation;
    CHECK(strstr(hostgc_input_state(), "pair ready"));
    stick(left, -.8f, .6f); stick(right, .3f, -.7f);
    CHECK(hostgc_poll(&out));
    CHECK(out.lx == -.8f && out.ly == .6f && out.rx == .3f && out.ry == -.7f);
    struct { TestController *device; NSString *alias; int button; } mapping[] = {
        {left, GCInputButtonA, HOSTGC_BTN_X}, {left, GCInputButtonB, HOSTGC_BTN_Y},
        {right, GCInputButtonA, HOSTGC_BTN_A}, {right, GCInputButtonB, HOSTGC_BTN_B},
        {left, GCInputGripButton, HOSTGC_BTN_LSHOULDER}, {right, GCInputGripButton, HOSTGC_BTN_RSHOULDER},
        {left, GCInputTrigger, HOSTGC_BTN_LTRIGGER}, {right, GCInputTrigger, HOSTGC_BTN_RTRIGGER},
        {left, GCInputThumbstickButton, HOSTGC_BTN_LTHUMB}, {right, GCInputThumbstickButton, HOSTGC_BTN_RTHUMB},
        {right, GCInputButtonMenu, HOSTGC_BTN_MENU}, {left, GCInputButtonHome, HOSTGC_BTN_HOME}
    };
    for (unsigned n = 0; n < sizeof mapping / sizeof mapping[0]; ++n) {
        press(mapping[n].device, mapping[n].alias, 1);
        CHECK(hostgc_poll(&out));
        for (int i = 0; i < HOSTGC_BUTTON_COUNT; ++i) CHECK(out.buttons[i] == (i == mapping[n].button));
        press(mapping[n].device, mapping[n].alias, 0);
    }
    press(left, GCInputTrigger, .4f); press(right, GCInputTrigger, .8f);
    CHECK(hostgc_poll(&out) && out.lt == .4f && out.rt == .8f);
    CHECK(!out.buttons[HOSTGC_BTN_LTRIGGER] && out.buttons[HOSTGC_BTN_RTRIGGER]);
    /* Create modifier substitutes D-pad and suppresses locomotion. */
    press(left, GCInputButtonMenu, 1); stick(left, 0, 1);
    CHECK(hostgc_poll(&out) && out.dpad_up && out.buttons[HOSTGC_BTN_DPAD_UP]);
    CHECK(out.lx == 0 && out.ly == 0 && !out.buttons[HOSTGC_BTN_MENU] && !out.buttons[HOSTGC_BTN_OPTIONS]);
    CHECK(out.spatial_raw_lx == 0 && out.spatial_raw_ly == 1);
    stick(left, 0, -1); CHECK(hostgc_poll(&out) && out.dpad_down);
    stick(left, -1, 0); CHECK(hostgc_poll(&out) && out.dpad_left);
    stick(left, 1, 0); CHECK(hostgc_poll(&out) && out.dpad_right);
    stick(left, 0, 0); CHECK(hostgc_poll(&out) && out.buttons[HOSTGC_BTN_OPTIONS]);
    stick(left, .4f, .3f); CHECK(hostgc_poll(&out) && out.buttons[HOSTGC_BTN_OPTIONS]);
    CHECK(out.lx == 0 && out.ly == 0 && out.spatial_raw_lx == .4f && out.spatial_raw_ly == .3f);
    stick(left, 0, 0); CHECK(hostgc_poll(&out) && out.spatial_raw_lx == 0 && out.spatial_raw_ly == 0);
    press(left, GCInputButtonMenu, 0);
    /* Reuse cannot retain held triggers after a disconnect. */
    CHECK(hostgc_poll(&out) && out.rt == .8f);
    g_test_devices = @[(id)left]; controller_connection_changed((id)right);
    CHECK(hostgc_source_generation() != source_generation && notified_generation == hostgc_source_generation());
    CHECK(!hostgc_poll(&out)); released(&out);
    CHECK(strstr(hostgc_input_state(), "both left and right"));
    /* Late ARKit completion for the previous connection cannot assign a new
     * connection of the same controller pointer to the wrong hand. */
    devices(@[left, right], left);
    HostGCSpatialBinding *old = binding(right);
    controller_connection_changed((id)right);
    HostGCSpatialBinding *replacement = binding(right);
    complete_spatial_binding(old, 1);
    CHECK(replacement.side == 0 && !hostgc_poll(&out));
    assign_side(right, 2); CHECK(hostgc_poll(&out) && out.rt == .8f);
    /* A third controller is ambiguous, regardless of arrival/current order. */
    TestController *otherLeft = half(); assign_side(otherLeft, 1);
    devices(@[left, right, otherLeft], right);
    CHECK(!hostgc_poll(&out)); released(&out);
    CHECK(strstr(hostgc_input_state(), "ambiguous"));
    devices(@[left, otherLeft], left); CHECK(!hostgc_poll(&out));
    /* A disconnect between left/right capture cannot republish stale input. */
    devices(@[left, right], right);
    left.input.duringCapture = ^{ controller_connection_changed((id)right); };
    CHECK(!hostgc_poll(&out)); released(&out);
    left.input.duringCapture = nil;
    assign_side(right, 2); CHECK(hostgc_poll(&out));
    /* A connected device with a missing mandatory element is not a full pair. */
    [right.input.buttons removeObjectForKey:GCInputTrigger]; controller_topology_changed(nil);
    CHECK(!hostgc_poll(&out)); released(&out);
    /* A normal pad remains authoritative when it is current, even with
     * incomplete/ambiguous spatial controllers in the same device list. */
    GCController *pad = [GCController controllerWithExtendedGamepad];
    [pad.extendedGamepad.leftThumbstick setValueForXAxis:.25f yAxis:-.5f];
    [pad.extendedGamepad.buttonA setValue:1.f];
    devices(@[left, otherLeft, pad], pad);
    CHECK(hostgc_poll(&out) && !out.spatial && out.lx == .25f && out.ly == -.5f && out.buttons[HOSTGC_BTN_A]);
    CHECK(strstr(hostgc_input_state(), "Gamepad ready"));
    devices(@[pad], nil); CHECK(hostgc_poll(&out) && out.buttons[HOSTGC_BTN_A]);
    devices(@[], nil); CHECK(!hostgc_poll(&out)); released(&out);
    printf("PASS: %u spatial input integration assertions\n", checks);
} return 0; }
