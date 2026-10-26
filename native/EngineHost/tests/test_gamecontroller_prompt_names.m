/* Prompt names from GameController metadata (gamecontroller.m prompt_read,
 * prompt_observe, hostgc_prompt_names) with stand-in objects for Apple's
 * public selectors: fixed names by productCategory, system remaps blanked,
 * generic and Sense names only from the system's own ASCII metadata, Sense
 * chirality prefixes and chords within 23 characters, refresh on selection
 * change or after two seconds. No hardware.
 * clang -fobjc-arc -fblocks -I native/EngineHost -framework Foundation -framework GameController \
 *   -framework CoreHaptics native/EngineHost/tests/test_gamecontroller_prompt_names.m
 * Run on macOS 26+ (spatial GameController input names). */
#define HOSTGC_SPATIAL_TESTING 1
#include <time.h>
#include <stdint.h>
static uint64_t test_now = 1000000000ull;
static uint64_t test_uptime(clockid_t clock) { (void)clock; return test_now; }
#define clock_gettime_nsec_np test_uptime
#import "../gamecontroller.m"
#undef clock_gettime_nsec_np
#include "../halo_settings.c"
#include "../input_prompt_label.h"
#include <assert.h>
#include <stdio.h>

void host_log(const char *fmt, ...) { (void)fmt; }
int halo_haptics_enabled(void) { return 1; }
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); abort(); } } while (0)

@interface TestElement : NSObject
@property(nonatomic, copy) NSString *localizedName, *unmappedLocalizedName, *sfSymbolsName;
@end
@implementation TestElement
@end
static TestElement *element(NSString *name) { TestElement *e = [TestElement new]; e.localizedName = name; e.unmappedLocalizedName = name; return e; }
@interface TestDpad : NSObject
@property(nonatomic, strong) TestElement *up, *down, *left, *right;
@end
@implementation TestDpad
@end
@interface TestGamepad : NSObject
@property(nonatomic, strong) TestElement *buttonA, *buttonB, *buttonX, *buttonY, *leftShoulder, *rightShoulder,
    *leftTrigger, *rightTrigger, *leftThumbstickButton, *rightThumbstickButton, *buttonMenu, *buttonOptions,
    *leftThumbstick, *rightThumbstick;
@property(nonatomic, strong) TestDpad *dpad;
@end
@implementation TestGamepad
@end
@interface TestPad : NSObject
@property(nonatomic, copy) NSString *productCategory;
@property(nonatomic, strong) TestGamepad *extendedGamepad;
@end
@implementation TestPad
@end
static TestPad *pad(NSString *category) {
    TestGamepad *g = [TestGamepad new];
    g.buttonA = element(@"Button A"); g.buttonB = element(@"Button B"); g.buttonX = element(@"Button X"); g.buttonY = element(@"Button Y");
    g.leftShoulder = element(@"Left Shoulder"); g.rightShoulder = element(@"Right Shoulder");
    g.leftTrigger = element(@"Left Trigger"); g.rightTrigger = element(@"Right Trigger");
    g.leftThumbstickButton = element(@"Left Thumbstick Button"); g.rightThumbstickButton = element(@"Right Thumbstick Button");
    g.buttonMenu = element(@"Menu"); g.buttonOptions = element(@"Options");
    g.leftThumbstick = element(@"Left Thumbstick"); g.rightThumbstick = element(@"Right Thumbstick");
    g.dpad = [TestDpad new]; g.dpad.up = element(@"Direction Pad Up"); g.dpad.down = element(@"Direction Pad Down");
    g.dpad.left = element(@"Direction Pad Left"); g.dpad.right = element(@"Direction Pad Right");
    TestPad *p = [TestPad new]; p.productCategory = category; p.extendedGamepad = g; return p;
}
@interface TestInputNames : NSObject
@property(nonatomic, strong) NSMutableDictionary *buttons, *dpads;
@end
@implementation TestInputNames
@end
@interface TestHalf : NSObject
@property(nonatomic, strong) TestInputNames *input;
@end
@implementation TestHalf
- (NSString *)productCategory { return GCProductCategorySpatialController; }
@end
static TestElement *shaped(NSString *symbol, NSString *name) { TestElement *e = element(name); e.sfSymbolsName = symbol; return e; }
static TestHalf *half(BOOL left) {
    TestHalf *h = [TestHalf new]; h.input = [TestInputNames new];
    h.input.buttons = [@{
        GCInputButtonA: left ? shaped(@"square.circle", @"Button A") : shaped(@"xmark.circle", @"Button A"),
        GCInputButtonB: left ? shaped(@"triangle.circle", @"Button B") : shaped(@"circle.circle", @"Button B"),
        GCInputGripButton: element(left ? @"L1" : @"R1"), GCInputTrigger: element(left ? @"L2" : @"R2"),
        GCInputThumbstickButton: element(left ? @"L3" : @"R3"), GCInputButtonMenu: element(left ? @"Create" : @"Options"),
    } mutableCopy];
    h.input.dpads = [@{ GCInputThumbstick: element(left ? @"Left Stick" : @"Right Stick") } mutableCopy];
    return h;
}
static int named(const HostGCPromptNames *n, int e, const char *want) { return !strcmp(n->name[e], want); }
static HostGCPromptNames read_names(NSArray *controllers) { HostGCPromptNames n; prompt_read(controllers, &n); return n; }
static TestHalf *realistic(BOOL left, BOOL glyphs) {
    NSString *s = left ? @"l" : @"r", *side = left ? @"Left" : @"Right";
    TestHalf *h = [TestHalf new]; h.input = [TestInputNames new];
    TestElement *(^el)(NSString *, NSString *) = ^(NSString *symbol, NSString *name) { return shaped(glyphs ? symbol : nil, name); };
    h.input.buttons = [@{
        GCInputButtonA: shaped(left ? @"square.circle.fill" : @"xmark.circle.fill", @"Button A"),
        GCInputButtonB: shaped(left ? @"triangle.circle.fill" : @"circle.circle.fill", @"Button B"),
        GCInputGripButton: el([s stringByAppendingString:@"1.rectangle.roundedbottom"], [side stringByAppendingString:@" Grip"]),
        GCInputTrigger: el([s stringByAppendingString:@"2.rectangle.roundedtop"], [side stringByAppendingString:@" Trigger"]),
        GCInputThumbstickButton: el([s stringByAppendingFormat:@"3.button.angledbottom.horizontal.%@", left ? @"left" : @"right"],
                                    [side stringByAppendingString:@" Thumbstick Button"]),
        GCInputButtonMenu: el(@"capsule.portrait", left ? @"Create Button" : @"Options Button"),
        GCInputButtonHome: el(@"house", @"Home Button"),
    } mutableCopy];
    h.input.dpads = [@{ GCInputThumbstick: el([s stringByAppendingString:@".joystick"], [side stringByAppendingString:@" Thumbstick"]) } mutableCopy];
    return h;
}
/* Every label the resolver can give these names, in every Sense context, fits. */
static void all_labels_fit(const HostGCPromptNames *n) {
    char label[HOST_PROMPT_LABEL_MAX];
    for (int context = HALO_SENSE_PROMPT_NONE; context <= HALO_SENSE_PROMPT_MENU_ONLY; context++)
        for (int flags = 0; flags < 4; flags++) {
            HaloSensePromptContext c = { .context = context, .gestures_own_create = flags & 1, .grenade_clutch = (flags >> 1) & 1 };
            for (int d = 0; d < 256; d++)
                if (host_prompt_label_for_dik(n, &c, (uint8_t)d, label)) CHECK(strlen(label) <= 23);
            for (unsigned b = 0; b < 8; b++) for (int a = -1; a < 27; a++)
                if (host_prompt_label_for_mouse_button(n, &c, b, a, label)) CHECK(strlen(label) <= 23);
            for (unsigned a = 0; a < 3; a++) if (host_prompt_label_for_mouse_axis(n, &c, a, label)) CHECK(strlen(label) <= 23);
        }
}
static int labelled(const HostGCPromptNames *n, HaloSensePromptContext c, int kind, int code, int action, const char *want) {
    char label[HOST_PROMPT_LABEL_MAX];
    int ok = kind == 0 ? host_prompt_label_for_dik(n, &c, (uint8_t)code, label) :
             kind == 1 ? host_prompt_label_for_mouse_button(n, &c, (unsigned)code, action, label) :
                         host_prompt_label_for_mouse_axis(n, &c, (unsigned)code, label);
    return want ? ok && !strcmp(label, want) : !ok;
}
static void realistic_checks(void) {
    HaloSensePromptContext tracked = { .context = HALO_SENSE_PROMPT_TRACKED, .gestures_own_create = 1, .grenade_clutch = 1 };
    HaloSensePromptContext plain = { .context = HALO_SENSE_PROMPT_TRACKED, .grenade_clutch = 1 };
    HaloSensePromptContext untracked = { .context = HALO_SENSE_PROMPT_PASSTHROUGH };
    /* With control-label glyphs. */
    HostGCPromptNames n = read_names(@[realistic(YES, YES), realistic(NO, YES)]);
    CHECK(named(&n, HOSTGC_BTN_X, "Left Square") && named(&n, HOSTGC_BTN_Y, "Left Triangle"));
    CHECK(named(&n, HOSTGC_BTN_A, "Right Cross") && named(&n, HOSTGC_BTN_B, "Right Circle"));
    CHECK(named(&n, HOSTGC_BTN_LSHOULDER, "L1") && named(&n, HOSTGC_BTN_RSHOULDER, "R1") && named(&n, HOSTGC_BTN_LTRIGGER, "L2"));
    CHECK(named(&n, HOSTGC_BTN_RTRIGGER, "R2") && named(&n, HOSTGC_BTN_LTHUMB, "L3") && named(&n, HOSTGC_BTN_RTHUMB, "R3"));
    CHECK(named(&n, HOSTGC_BTN_OPTIONS, "Left Create Button") && named(&n, HOSTGC_BTN_MENU, "Right Options Button"));
    CHECK(named(&n, HOSTGC_PROMPT_LSTICK, "Left Thumbstick") && named(&n, HOSTGC_PROMPT_RSTICK, "Right Thumbstick"));
    CHECK(named(&n, HOSTGC_BTN_DPAD_UP, "Create+L Stick Up") && named(&n, HOSTGC_BTN_DPAD_DOWN, "Create+L Stick Down"));
    CHECK(named(&n, HOSTGC_PROMPT_CREATE_R3, "Create+R3"));
    CHECK(labelled(&n, tracked, 1, 1, 6, "Hold L1") && labelled(&n, tracked, 1, 1, 4, NULL));
    CHECK(labelled(&n, tracked, 0, 0x3B, 0, "Create+R3") && labelled(&n, plain, 0, 0x3B, 0, "Left Create Button"));
    CHECK(labelled(&n, tracked, 0, 0x10, 0, "Create+L Stick Up") && labelled(&n, tracked, 0, 0x22, 0, "Create+L Stick Down"));
    CHECK(labelled(&n, tracked, 0, 0x0F, 0, "Left Triangle") && labelled(&n, tracked, 0, 0x2C, 0, "R3"));
    CHECK(labelled(&n, untracked, 0, 0x20, 0, "Left Thumbstick Right") && labelled(&n, untracked, 1, 1, 4, "L1"));
    all_labels_fit(&n);
    /* Names only (no control-label glyphs): long names, proven short chords. */
    n = read_names(@[realistic(YES, NO), realistic(NO, NO)]);
    CHECK(named(&n, HOSTGC_BTN_LSHOULDER, "Left Grip") && named(&n, HOSTGC_BTN_RTHUMB, "Right Thumbstick Button"));
    CHECK(named(&n, HOSTGC_BTN_DPAD_LEFT, "Create+L Stick Left") && named(&n, HOSTGC_PROMPT_CREATE_R3, "Create+R Stick Button"));
    CHECK(labelled(&n, tracked, 1, 1, 6, "Hold Left Grip") && labelled(&n, tracked, 0, 0x2C, 0, "Right Thumbstick Button"));
    CHECK(labelled(&n, tracked, 0, 0x3B, 0, "Create+R Stick Button") && labelled(&n, tracked, 0, 0x10, 0, "Create+L Stick Up"));
    all_labels_fit(&n);
}
static void observe(NSArray *controllers) { prompt_observe(controllers); }

int main(void) { @autoreleasepool {
    /* Fixed names by Apple's product category; HOME never named. */
    HostGCPromptNames n = read_names(@[pad(GCProductCategoryDualSense)]);
    CHECK(n.family == HOSTGC_PROMPT_FAMILY_DUALSENSE && !n.spatial);
    CHECK(named(&n, HOSTGC_BTN_Y, "Triangle") && named(&n, HOSTGC_BTN_X, "Square") && named(&n, HOSTGC_BTN_A, "Cross"));
    CHECK(named(&n, HOSTGC_BTN_OPTIONS, "Create") && named(&n, HOSTGC_BTN_MENU, "Options") && named(&n, HOSTGC_BTN_HOME, ""));
    CHECK(named(&n, HOSTGC_BTN_DPAD_LEFT, "D-pad Left") && named(&n, HOSTGC_PROMPT_LSTICK, "Left Stick") && named(&n, HOSTGC_PROMPT_CREATE_R3, ""));
    n = read_names(@[pad(GCProductCategoryDualShock4)]);
    CHECK(n.family == HOSTGC_PROMPT_FAMILY_DUALSHOCK4 && named(&n, HOSTGC_BTN_OPTIONS, "Share") && named(&n, HOSTGC_BTN_LSHOULDER, "L1"));
    n = read_names(@[pad(GCProductCategoryXboxOne)]);
    CHECK(n.family == HOSTGC_PROMPT_FAMILY_XBOX && named(&n, HOSTGC_BTN_Y, "Y") && named(&n, HOSTGC_BTN_LSHOULDER, "LB") && named(&n, HOSTGC_BTN_OPTIONS, "View"));
    /* A system remap (or a disabled element) blanks exactly that element. */
    TestPad *remapped = pad(GCProductCategoryDualSense);
    remapped.extendedGamepad.buttonY.localizedName = @"Circle";
    remapped.extendedGamepad.buttonX.localizedName = nil;
    n = read_names(@[remapped]);
    CHECK(named(&n, HOSTGC_BTN_Y, "") && named(&n, HOSTGC_BTN_X, "") && named(&n, HOSTGC_BTN_A, "Cross"));
    char label[HOST_PROMPT_LABEL_MAX];
    CHECK(!host_prompt_label_for_dik(&n, NULL, 0x0F, label));                         /* Tab: original text */
    CHECK(host_prompt_label_for_dik(&n, NULL, 0x13, label) && !strcmp(label, "D-pad Left"));
    /* Generic: the system's own names, printable ASCII only, 23 characters. */
    TestPad *generic = pad(GCProductCategoryMFi);
    generic.extendedGamepad.buttonB.localizedName = generic.extendedGamepad.buttonB.unmappedLocalizedName = @"Bouton Ⓑ";
    generic.extendedGamepad.buttonA.localizedName = generic.extendedGamepad.buttonA.unmappedLocalizedName = @"A very long element name!";
    n = read_names(@[generic]);
    CHECK(n.family == HOSTGC_PROMPT_FAMILY_GENERIC && named(&n, HOSTGC_BTN_X, "Button X") && named(&n, HOSTGC_BTN_B, "") &&
          named(&n, HOSTGC_BTN_A, "") && named(&n, HOSTGC_BTN_LTHUMB, "Left Thumbstick Button"));
    n = read_names(@[pad(nil)]); CHECK(n.family == HOSTGC_PROMPT_FAMILY_GENERIC && named(&n, HOSTGC_BTN_Y, "Button Y"));
    TestPad *bare = [TestPad new]; n = read_names(@[bare]); CHECK(n.family == HOSTGC_PROMPT_FAMILY_NONE);
    n = read_names(@[]); CHECK(n.family == HOSTGC_PROMPT_FAMILY_NONE);
    /* Sense pair: shapes from the system glyphs, sides from chirality, chords. */
    n = read_names(@[half(YES), half(NO)]);
    CHECK(n.family == HOSTGC_PROMPT_FAMILY_SPATIAL && n.spatial);
    CHECK(named(&n, HOSTGC_BTN_X, "Left Square") && named(&n, HOSTGC_BTN_Y, "Left Triangle"));
    CHECK(named(&n, HOSTGC_BTN_A, "Right Cross") && named(&n, HOSTGC_BTN_B, "Right Circle"));
    CHECK(named(&n, HOSTGC_BTN_LSHOULDER, "L1") && named(&n, HOSTGC_BTN_RSHOULDER, "R1") && named(&n, HOSTGC_BTN_RTHUMB, "R3"));
    CHECK(named(&n, HOSTGC_BTN_OPTIONS, "Left Create") && named(&n, HOSTGC_BTN_MENU, "Right Options"));
    CHECK(named(&n, HOSTGC_PROMPT_LSTICK, "Left Stick") && named(&n, HOSTGC_PROMPT_RSTICK, "Right Stick"));
    CHECK(named(&n, HOSTGC_BTN_DPAD_UP, "Create+Left Stick Up") && named(&n, HOSTGC_BTN_DPAD_RIGHT, "Create+Left Stick Right"));
    CHECK(named(&n, HOSTGC_PROMPT_CREATE_R3, "Create+R3"));
    HaloSensePromptContext tracked = { .context = HALO_SENSE_PROMPT_TRACKED, .gestures_own_create = 1, .grenade_clutch = 1 };
    CHECK(host_prompt_label_for_mouse_button(&n, &tracked, 1, HOST_PROMPT_ACTION_THROW_GRENADE, label) && !strcmp(label, "Hold L1"));
    CHECK(host_prompt_label_for_dik(&n, &tracked, 0x3B, label) && !strcmp(label, "Create+R3"));
    CHECK(host_prompt_label_for_dik(&n, &tracked, 0x0F, label) && !strcmp(label, "Left Triangle"));
    /* Without glyph metadata the pair is never assumed to be PlayStation. */
    TestHalf *left = half(YES), *right = half(NO);
    for (TestHalf *h in @[left, right]) for (TestElement *e in h.input.buttons.allValues) e.sfSymbolsName = nil;
    n = read_names(@[left, right]);
    CHECK(named(&n, HOSTGC_BTN_Y, "Left Button B") && named(&n, HOSTGC_BTN_A, "Right Button A"));
    /* A chord that would exceed 23 characters uses the proven short stick form. */
    ((TestElement *)left.input.dpads[GCInputThumbstick]).localizedName = @"Left Thumbstick";
    n = read_names(@[left, right]);
    CHECK(named(&n, HOSTGC_BTN_DPAD_UP, "Create+L Stick Up") && named(&n, HOSTGC_BTN_DPAD_RIGHT, "Create+L Stick Right"));
    CHECK(named(&n, HOSTGC_PROMPT_LSTICK, "Left Thumbstick"));
    /* With no short form that fits, the chord is not named (original text). */
    ((TestElement *)left.input.buttons[GCInputButtonMenu]).localizedName = @"Create And Share Keys";
    n = read_names(@[left, right]);
    CHECK(named(&n, HOSTGC_BTN_DPAD_UP, "") && named(&n, HOSTGC_PROMPT_CREATE_R3, ""));
    CHECK(named(&n, HOSTGC_BTN_OPTIONS, ""));                          /* "Left Create And Share Keys": 26 */

    /* Realistic system metadata: glyph names for the shapes and L1..R3,
     * long localized names elsewhere. */
    realistic_checks();
    /* No metadata at all: no names. */
    TestHalf *mute = [TestHalf new]; mute.input = [TestInputNames new];
    n = read_names(@[mute, [TestHalf new]]);
    for (int e = 0; e < HOSTGC_PROMPT_ELEMENTS; e++) CHECK(!n.name[e][0]);

    /* Published names: refreshed on a selection change or after two seconds. */
    CHECK(!hostgc_prompt_names(&n));
    TestPad *ds = pad(GCProductCategoryDualSense);
    observe(@[ds]); CHECK(hostgc_prompt_names(&n) && named(&n, HOSTGC_BTN_Y, "Triangle"));
    CHECK(n.source_generation == hostgc_source_generation());
    ds.extendedGamepad.buttonY.localizedName = @"Circle";                  /* remapped while selected */
    test_now += 1000000000ull; observe(@[ds]); hostgc_prompt_names(&n); CHECK(named(&n, HOSTGC_BTN_Y, "Triangle"));
    test_now += 1500000000ull; observe(@[ds]); hostgc_prompt_names(&n); CHECK(named(&n, HOSTGC_BTN_Y, ""));
    observe(@[pad(GCProductCategoryXboxOne)]); hostgc_prompt_names(&n); CHECK(n.family == HOSTGC_PROMPT_FAMILY_XBOX);
    controller_connection_changed(nil);                                     /* new generation: re-read */
    observe(@[pad(GCProductCategoryXboxOne)]); hostgc_prompt_names(&n); CHECK(n.source_generation == hostgc_source_generation());
    observe(@[]); CHECK(!hostgc_prompt_names(&n) && n.family == HOSTGC_PROMPT_FAMILY_NONE);
    CHECK(!hostgc_prompt_names(NULL));
    printf("PASS %u prompt-name checks: DualSense/DualShock 4/Xbox tables by category, remaps blanked, generic and Sense "
           "names from system metadata (glyph shapes, chirality, chords <= 23), refresh contract (synthetic objects)\n", checks);
}}
