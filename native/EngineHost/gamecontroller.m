/* Apple GameController -> HostGCSnapshot bridge.  See gamecontroller.h.
 *
 * Works with GCExtendedGamepad devices and a left/right spatial controller
 * pair on visionOS 26. We poll the profiles on demand from the
 * game thread rather than depending on value-changed handlers, so no run loop
 * is required for input to flow; connect/disconnect notifications are only used
 * to keep a cached "current controller" pointer warm and to log hot-plugs.
 */
#import <Foundation/Foundation.h>
#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>
#import <os/lock.h>
#import <TargetConditionals.h>
#if TARGET_OS_VISION
#import <ARKit/ARKit.h>
#endif
#include "gamecontroller.h"
#include "gamecontroller_guard.h"
#include "haptics.h"
#include "sense_pose.h"
#include <dispatch/dispatch.h>
#include <time.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

void host_log(const char *fmt, ...);

static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;
static bool           g_inited = false;
static bool           g_have_test = false;      /* a test snapshot is active   */
static HostGCSnapshot g_test;                   /* the injected snapshot       */
static uint64_t       g_sequence = 0;           /* hardware poll counter       */
static HostGCSnapshot g_recent;                 /* the newest hardware reading */
static uint64_t       g_recent_ns = 0;          /* when it was taken; 0 = none */
static uint64_t       g_captures = 0, g_reuses = 0;
static id             g_connect_obs = nil;
static id             g_disconnect_obs = nil;
static id             g_current_obs = nil;
static uint64_t       g_topology = 0;
static _Atomic uint64_t g_source_generation = 1;

uint64_t hostgc_source_generation(void) {
    return atomic_load_explicit(&g_source_generation, memory_order_acquire);
}

/* Separate from the capture lock: callbacks take only the Sense bridge lock.
 * Never call back into GameController from halo_sense_app_input_changed. */
static pthread_mutex_t g_app_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_app_counter, g_app_token, g_app_deadline_ns;
static uint64_t g_app_wait_ns, g_app_observed_ns, g_app_epoch = 1;
static int g_app_wait_release, g_app_ordinary, g_app_notified;
static uint64_t app_input_now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static void app_input_notify_locked(void) {
    int blocked = (g_app_token || g_app_wait_release) && !g_app_ordinary;
    if (blocked != g_app_notified) {
        g_app_notified = blocked;
        halo_sense_app_input_changed(blocked);
    }
}
static void app_input_expire_locked(uint64_t now) {
    if (g_app_token && (!now || now >= g_app_deadline_ns)) {
        g_app_token = 0; g_app_wait_release = 1; g_app_wait_ns = now;
    }
    app_input_notify_locked();
}
uint64_t hostgc_app_input_begin(void) {
    pthread_mutex_lock(&g_app_lock);
    if (++g_app_counter == 0) ++g_app_counter;
    if (++g_app_epoch == 0) ++g_app_epoch;
    g_app_token = g_app_counter; g_app_deadline_ns = app_input_now() + UINT64_C(500000000);
    g_app_wait_release = 1;
    uint64_t token = g_app_token;
    app_input_notify_locked(); pthread_mutex_unlock(&g_app_lock); return token;
}
int hostgc_app_input_renew(uint64_t token) {
    pthread_mutex_lock(&g_app_lock); uint64_t now = app_input_now();
    app_input_expire_locked(now);
    int valid = token && token == g_app_token;
    if (valid) g_app_deadline_ns = now + UINT64_C(500000000);
    pthread_mutex_unlock(&g_app_lock); return valid;
}
void hostgc_app_input_end(uint64_t token) {
    pthread_mutex_lock(&g_app_lock);
    if (token && token == g_app_token) {
        g_app_token = 0; g_app_wait_release = 1; g_app_wait_ns = app_input_now();
    }
    app_input_notify_locked(); pthread_mutex_unlock(&g_app_lock);
}
int hostgc_app_input_blocked(void) {
    pthread_mutex_lock(&g_app_lock); app_input_expire_locked(app_input_now());
    int blocked = g_app_token || g_app_wait_release;
    pthread_mutex_unlock(&g_app_lock); return blocked;
}
uint64_t hostgc_app_input_epoch(void) {
    pthread_mutex_lock(&g_app_lock); uint64_t epoch = g_app_epoch;
    pthread_mutex_unlock(&g_app_lock); return epoch;
}
static int app_input_neutral(const HostGCSnapshot *p) {
    const float axes[] = {p->lx,p->ly,p->rx,p->ry,p->spatial_raw_lx,p->spatial_raw_ly};
    for (unsigned i=0;i<sizeof axes/sizeof *axes;i++) if (!isfinite(axes[i]) || fabsf(axes[i]) > .2f) return 0;
    if (!isfinite(p->lt) || !isfinite(p->rt) || p->lt < 0 || p->rt < 0 || p->lt > .1f || p->rt > .1f ||
        p->dpad_up || p->dpad_down || p->dpad_left || p->dpad_right) return 0;
    for (unsigned i=0;i<HOSTGC_BUTTON_COUNT;i++) if (p->buttons[i]) return 0;
    return 1;
}
static void app_input_observe(const HostGCSnapshot *p, uint64_t captured_ns) {
    pthread_mutex_lock(&g_app_lock); uint64_t now = app_input_now();
    app_input_expire_locked(now);
    if (captured_ns && captured_ns >= g_app_observed_ns && captured_ns <= now && now-captured_ns <= UINT64_C(150000000)) {
        g_app_observed_ns = captured_ns;
        g_app_ordinary = p->connected && !p->spatial;
        if (p->connected && p->source_generation == hostgc_source_generation()) {
            /* Consume Create+Options before a DI read can pause the game; the
             * Swift UI recognizer owns the actual lease/window transition.
             * Ordinary Menu/Options presses stay with Halo when no lease exists. */
            if (p->spatial && !g_app_token && !g_app_wait_release && p->buttons[HOSTGC_BTN_OPTIONS] && p->buttons[HOSTGC_BTN_MENU]) {
                g_app_wait_release = 1; g_app_wait_ns = now;
                if (++g_app_epoch == 0) ++g_app_epoch;
            }
            /* Either controller kind hands input back only after a fresh, fully
             * released reading, so a press held through dismissal never lands. */
            if (!g_app_token && g_app_wait_release && captured_ns >= g_app_wait_ns && app_input_neutral(p))
                g_app_wait_release = 0;
        }
        app_input_notify_locked();
    }
    pthread_mutex_unlock(&g_app_lock);
}
/* While native UI owns input (lease or post-close handback), the engine sees a
 * connected, fully neutral controller of the same kind: Sense or ordinary. */
int hostgc_app_input_filter(HostGCSnapshot *snapshot) {
    if (!snapshot || !snapshot->connected || !hostgc_app_input_blocked()) return 0;
    uint64_t sequence=snapshot->sequence, source=snapshot->source_generation; bool spatial=snapshot->spatial;
    memset(snapshot,0,sizeof *snapshot); snapshot->connected=true; snapshot->spatial=spatial;
    snapshot->sequence=sequence; snapshot->source_generation=source; return 1;
}

/* Spatial controllers do not expose an extended gamepad. Both halves expose
 * the same aliases (.a, .b, .trigger, .grip, .thumbstick). Use ARKit's inherent
 * chirality, never connection order, a localized name, or the hand currently
 * holding the controller, to decide which half owns each game action. */
@interface HostGCSpatialBinding : NSObject
@property(nonatomic, strong) GCController *controller;
@property(nonatomic) NSInteger side; /* 0 pending/unknown, 1 left, 2 right */
@end
@implementation HostGCSpatialBinding
@end
static NSMutableArray<HostGCSpatialBinding *> *g_spatial_bindings;

/* Called while g_lock is held. Also rejects in-flight captures from before a
 * disconnect, so a late capture cannot put a held trigger back in the cache. */
static void invalidate_reading_locked(void) {
    ++g_topology;
    g_recent_ns = 0;
    memset(&g_recent, 0, sizeof g_recent);
}

static bool spatial_controller(GCController *controller) {
#if TARGET_OS_VISION || defined(HOSTGC_SPATIAL_TESTING)
    if (@available(macOS 26.0, iOS 26.0, visionOS 26.0, *))
        return [controller.productCategory isEqualToString:GCProductCategorySpatialController];
#endif
    return false;
}

#if TARGET_OS_VISION || defined(HOSTGC_SPATIAL_TESTING)
static void complete_spatial_binding(HostGCSpatialBinding *binding, NSInteger side) {
    os_unfair_lock_lock(&g_lock);
    /* The same GCController object may reconnect while ARKit is loading. The
     * record, not just the controller pointer, identifies this connection. */
    if ([g_spatial_bindings containsObject:binding]) {
        binding.side = side;
        invalidate_reading_locked();
    }
    os_unfair_lock_unlock(&g_lock);
}
#endif

static NSInteger spatial_side(GCController *controller) {
    os_unfair_lock_lock(&g_lock);
    for (HostGCSpatialBinding *binding in g_spatial_bindings) {
        if (binding.controller == controller) {
            NSInteger side = binding.side;
            os_unfair_lock_unlock(&g_lock);
            return side;
        }
    }
    HostGCSpatialBinding *binding = [HostGCSpatialBinding new];
    binding.controller = controller;
    if (!g_spatial_bindings) g_spatial_bindings = [NSMutableArray new];
    [g_spatial_bindings addObject:binding];
    os_unfair_lock_unlock(&g_lock);
#if TARGET_OS_VISION
    if (@available(visionOS 26.0, *)) {
        ar_accessory_load_from_device(controller,
            ^(id<GCDevice> device, bool successful, ar_error_t error, ar_accessory_t accessory) {
                NSInteger side = 0;
                if (successful && accessory) {
                    ar_accessory_chirality_t chirality = ar_accessory_get_inherent_chirality(accessory);
                    if (chirality == ar_accessory_chirality_left) side = 1;
                    if (chirality == ar_accessory_chirality_right) side = 2;
                }
                complete_spatial_binding(binding, side);
                host_log("Spatial controller side: %s", side == 1 ? "left" : side == 2 ? "right" : "unavailable; reconnect controller");
            });
    }
#endif
    return 0;
}

static void controller_topology_changed(GCController *disconnected) {
    os_unfair_lock_lock(&g_lock);
    if (disconnected) {
        for (NSInteger i = (NSInteger)g_spatial_bindings.count - 1; i >= 0; --i)
            if (g_spatial_bindings[(NSUInteger)i].controller == disconnected)
                [g_spatial_bindings removeObjectAtIndex:(NSUInteger)i];
    }
    invalidate_reading_locked();
    os_unfair_lock_unlock(&g_lock);
}

/* A connection lifetime differs from a cache invalidation (current-pad
 * changes, chirality discovery or button tests). Finish pose invalidation
 * before a new native capture can pass g_lock. The bridge callback never
 * polls GameController or takes g_lock; lock order is g_lock -> sense_lock. */
static void controller_connection_changed(GCController *disconnected) {
    os_unfair_lock_lock(&g_lock);
    uint64_t generation = atomic_fetch_add_explicit(&g_source_generation, 1, memory_order_acq_rel) + 1;
    if (!generation) { generation = 1; atomic_store_explicit(&g_source_generation, generation, memory_order_release); }
    if (disconnected) {
        for (NSInteger i = (NSInteger)g_spatial_bindings.count - 1; i >= 0; --i)
            if (g_spatial_bindings[(NSUInteger)i].controller == disconnected)
                [g_spatial_bindings removeObjectAtIndex:(NSUInteger)i];
    }
    invalidate_reading_locked();
    halo_sense_controller_changed(generation);
    os_unfair_lock_unlock(&g_lock);
}

/* A test supplies only the framework discovery boundary; selection, capture,
 * mapping and invalidation below are the production paths. */
#ifdef HOSTGC_SPATIAL_TESTING
static NSArray<GCController *> *g_test_devices;
static GCController *g_test_current;
#endif
static NSArray<GCController *> *attached_controllers(void) {
#ifdef HOSTGC_SPATIAL_TESTING
    return g_test_devices ?: @[];
#else
    return GCController.controllers;
#endif
}
static GCController *active_controller(void) {
#ifdef HOSTGC_SPATIAL_TESTING
    return g_test_current;
#else
    if (@available(macOS 11.0, iOS 14.0, *)) return GCController.current;
    return nil;
#endif
}

/* Keep conventional-pad selection unchanged. A spatial source requires
 * exactly two connected halves with opposite known chirality. With duplicate
 * halves there is no public pair ID: refusing the ambiguous set prevents
 * silently combining different players' controllers. */
static NSArray<GCController *> *controllers_for_input(void) {
    NSArray<GCController *> *devices = attached_controllers();
    GCController *current = active_controller();
    if (![devices containsObject:current]) current = nil;
    if (current && !spatial_controller(current) && current.extendedGamepad) return @[current];
    GCController *left = nil, *right = nil, *fallback = nil;
    NSUInteger spatialCount = 0;
    for (GCController *controller in devices) {
        if (spatial_controller(controller)) {
            ++spatialCount;
            NSInteger side = spatial_side(controller);
            if (side == 1) left = controller;
            if (side == 2) right = controller;
        } else if (!fallback && controller.extendedGamepad) fallback = controller;
    }
    if (spatialCount == 2 && left && right) return @[left, right];
    if (spatial_controller(current)) return @[];
    return fallback ? @[fallback] : @[];
}

/* The link guard (gamecontroller_guard.h) and the queue pulses play on. The
 * presenter used to call Core Haptics itself, 90 times a second, and a Start
 * could stall its frame; pulses now go to one serial queue. HALO_HAPTICS_GUARD=0
 * turns the pacing and backoff off (pulses still leave the render thread);
 * HALO_HAPTIC_MIN_INTERVAL_MS (default 250) and HALO_HAPTIC_CONNECT_GRACE_MS
 * (default 3000) tune it. */
static HostGCGuard      g_guard;
static os_unfair_lock   g_guard_lock = OS_UNFAIR_LOCK_INIT;
static bool             g_guard_enabled = true;
static dispatch_queue_t haptic_queue(void) {
    static dispatch_queue_t queue; static dispatch_once_t once;
    dispatch_once(&once, ^{
        queue = dispatch_queue_create("halo.controller.haptics",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
    });
    return queue;
}
static _Atomic uint64_t g_last_played_ns;
static _Atomic uint64_t g_last_connect_ns, g_last_disconnect_ns;
static _Atomic uint64_t g_haptic_failures, g_haptic_resets, g_haptic_stops;
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static long env_long(const char *name, long fallback, long low, long high) {
    const char *value = getenv(name);
    long v = value && value[0] ? strtol(value, NULL, 10) : fallback;
    return v < low ? low : v > high ? high : v;
}
static void haptic_release_on_disconnect(void);

/* One hardware reading serves every caller for a moment.
 *
 * A frame asks for the pad from half a dozen places: the keyboard and mouse
 * synthesis behind DirectInput's GetDeviceState and GetDeviceData, the
 * joystick's Acquire, Poll and GetDeviceState, USER32's message pump and the
 * pad-to-keyboard bridge at Present. Each used to look the controller up and
 * take its own [controller capture], which copies every element of the
 * profile into new objects, and on the headset that was several captures a
 * frame of readings microseconds apart. The Mac probe injects a test snapshot
 * and never paid it. A reading younger than this window is handed out again
 * instead. The window is well under the shortest frame the engine makes, so
 * the reads a frame makes together share one reading and none handed out is
 * more than the window old. HALO_PAD_REUSE_US sets it in microseconds (at
 * most 20000); 0 captures on every call, as before. */
static _Atomic int64_t g_window_ns = -1;         /* -1 until HALO_PAD_REUSE_US is read */
static uint64_t reuse_window_ns(void) {
    int64_t window = atomic_load_explicit(&g_window_ns, memory_order_relaxed);
    if (window < 0) {
        const char *option = getenv("HALO_PAD_REUSE_US");
        long us = option && option[0] ? strtol(option, NULL, 10) : 4000;
        if (us < 0) us = 0;
        if (us > 20000) us = 20000;
        window = (int64_t)us * 1000;
        atomic_store_explicit(&g_window_ns, window, memory_order_relaxed);
    }
    return (uint64_t)window;
}

/* For a spatial pair, impacts go to the right/weapon hand. Semantic weapon
 * events address each half directly (hostgc_play_spatial_haptic). */
static GCController *current_input_controller(void) {
    return controllers_for_input().lastObject;
}

void hostgc_init(void) {
    @autoreleasepool {
    os_unfair_lock_lock(&g_lock);
    if (g_inited) { os_unfair_lock_unlock(&g_lock); return; }
    g_inited = true;
    os_unfair_lock_unlock(&g_lock);

    const char *guard = getenv("HALO_HAPTICS_GUARD");
    g_guard_enabled = !(guard && guard[0] == '0');
    hostgc_guard_init(&g_guard, (uint64_t)env_long("HALO_HAPTIC_MIN_INTERVAL_MS", 250, 0, 5000) * 1000000ull,
                      (uint64_t)env_long("HALO_HAPTIC_CONNECT_GRACE_MS", 3000, 0, 60000) * 1000000ull);

    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    g_connect_obs = [nc addObserverForName:GCControllerDidConnectNotification
                                    object:nil queue:nil
                                usingBlock:^(NSNotification *n) {
        GCController *c = n.object;
        controller_connection_changed(nil);
        if (spatial_controller(c)) (void)spatial_side(c);
        host_log("GameController connected: %s (%s)",
                 c.vendorName.UTF8String ?: "?",
                 spatial_controller(c) ? "spatial controller" : c.extendedGamepad ? "extended gamepad" : "no supported profile");
        /* Discovery scans for new pads, and a scan takes radio time from the
         * link to the pad already in hand. Stop it once one is here. */
        [GCController stopWirelessControllerDiscovery];
        os_unfair_lock_lock(&g_guard_lock);
        hostgc_guard_connected(&g_guard, now_ns());
        atomic_store_explicit(&g_last_connect_ns, now_ns(), memory_order_relaxed);
        os_unfair_lock_unlock(&g_guard_lock);
    }];
    g_disconnect_obs = [nc addObserverForName:GCControllerDidDisconnectNotification
                                       object:nil queue:nil
                                   usingBlock:^(NSNotification *n) {
        GCController *c = n.object;
        controller_connection_changed(c);
        uint64_t now = now_ns(), played = atomic_load_explicit(&g_last_played_ns, memory_order_relaxed);
        bool another = current_input_controller() != nil;
        os_unfair_lock_lock(&g_guard_lock);
        uint64_t suspensions = g_guard.suspensions;
        hostgc_guard_disconnected(&g_guard, now, played);
        atomic_store_explicit(&g_last_disconnect_ns, now, memory_order_relaxed);
        bool suspended = g_guard.suspensions != suspensions;
        uint64_t until = g_guard.suspended_until_ns;
        /* Another pad is still here: it starts its own grace period. */
        if (another) hostgc_guard_connected(&g_guard, now);
        os_unfair_lock_unlock(&g_guard_lock);
        host_log("GameController disconnected: %s (last pulse %.1f s before)%s", c.vendorName.UTF8String ?: "?",
                 played ? (double)(now - played) * 1e-9 : -1.0, suspended ? "; haptics suspended" : "");
        if (suspended) host_log("[haptics] two drops within two minutes of pulses: haptics off for %.0f s",
                                (double)(until - now) * 1e-9);
        /* The engine belongs to the pad that just went away. Kept running, it
         * was still there when the pad came back. */
        haptic_release_on_disconnect();
    }];
    g_current_obs = [nc addObserverForName:GCControllerDidBecomeCurrentNotification
                                   object:nil queue:nil usingBlock:^(NSNotification *n) {
        controller_topology_changed(nil);
    }];
    /* Paired pads connect by themselves. Discovery is only for finding a new
     * one, so run it only while none is here; the connect handler stops it. */
    if (!current_input_controller()) [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{}];
    else {
        os_unfair_lock_lock(&g_guard_lock);
        hostgc_guard_connected(&g_guard, now_ns());
        atomic_store_explicit(&g_last_connect_ns, now_ns(), memory_order_relaxed);
        os_unfair_lock_unlock(&g_guard_lock);
    }
    host_log("GameController subsystem initialised (%lu controller(s) present)",
             (unsigned long)GCController.controllers.count);
    }
}

/* Haptics on the pad the host already reads.
 *
 * The engine is a translated 32-bit binary with no notion of force feedback,
 * so impacts are synthesised from the mixed audio. All this has to do is put
 * them on the hardware. The engine is kept and started once; Core Haptics
 * wants its setup off the audio path, so the first pulse builds it and later
 * ones reuse it. */
/* Only pulse callers own the cached engines/pads. Framework callbacks touch the
 * small state lock, never start/stop an engine or wait for a pulse to finish.
 * Slot 1 is the pad the host reads (an ordinary pad, or the right Sense half);
 * slot 0 is the left Sense half, which only semantic weapon events address. */
typedef struct {
    CHHapticEngine *engine;
    GCController   *pad;
    uint64_t        generation;
    bool            needs_start;
} HostGCHapticSlot;
static pthread_mutex_t  g_haptic_play_lock = PTHREAD_MUTEX_INITIALIZER;
static HostGCHapticSlot g_haptic_slots[2] = {{.needs_start = true}, {.needs_start = true}};
#define g_haptic_engine (g_haptic_slots[1].engine)
#define g_haptic_needs_start (g_haptic_slots[1].needs_start)
static char            g_haptic_state[96] = "not started";
static uint64_t        g_haptics_played = 0;
static os_unfair_lock  g_haptic_lock = OS_UNFAIR_LOCK_INIT;

static void haptic_note(const char *text) {
    os_unfair_lock_lock(&g_haptic_lock);
    snprintf(g_haptic_state, sizeof g_haptic_state, "%s", text);
    os_unfair_lock_unlock(&g_haptic_lock);
}

static void haptic_needs_restart(unsigned slot, uint64_t generation, const char *reason) {
    os_unfair_lock_lock(&g_haptic_lock);
    if (generation == g_haptic_slots[slot].generation) {
        g_haptic_slots[slot].needs_start = true;
        snprintf(g_haptic_state, sizeof g_haptic_state, "%s", reason);
    }
    os_unfair_lock_unlock(&g_haptic_lock);
}

/* Called with play_lock held. Invalidate callbacks before asynchronous Stop;
 * a late callback from the old pad must not stop the replacement's pulses. */
static void haptic_discard_slot(unsigned slot) {
    HostGCHapticSlot *h = &g_haptic_slots[slot];
    os_unfair_lock_lock(&g_haptic_lock);
    h->generation++;
    h->needs_start = true;
    os_unfair_lock_unlock(&g_haptic_lock);
    [h->engine stopWithCompletionHandler:nil];
    h->engine = nil;
    h->pad = nil;
}

/* Returns an engine ready to play, or nil with the reason recorded.
 * Restart only when a pulse is requested. A reset during suspension should not
 * itself reactivate controller hardware, and players are recreated per pulse. */
static CHHapticEngine *haptic_engine_for(unsigned slot, GCController *pad) {
    HostGCHapticSlot *h = &g_haptic_slots[slot];
    if (h->pad != pad) haptic_discard_slot(slot);
    if (!pad) { haptic_note("no controller"); return nil; }
    if (!h->engine) {
        GCDeviceHaptics *caps = pad.haptics;
        if (!caps) {
            char text[96];
            snprintf(text, sizeof text, "%s has no haptics", pad.vendorName.UTF8String ?: "pad");
            haptic_note(text); return nil;
        }
        CHHapticEngine *engine = [caps createEngineWithLocality:GCHapticsLocalityDefault];
        if (!engine) { atomic_fetch_add_explicit(&g_haptic_failures, 1, memory_order_relaxed); haptic_note("no haptic engine for this pad"); return nil; }
        h->pad = pad;
        h->engine = engine;
        /* Let an idle engine stop: a running one keeps a haptics stream open
         * to the pad between pulses. The stopped handler marks it for a
         * restart on the next pulse. */
        engine.autoShutdownEnabled = YES;
        os_unfair_lock_lock(&g_haptic_lock);
        uint64_t generation = ++h->generation;
        h->needs_start = true;
        os_unfair_lock_unlock(&g_haptic_lock);
        engine.resetHandler = ^{ atomic_fetch_add_explicit(&g_haptic_resets, 1, memory_order_relaxed); haptic_needs_restart(slot, generation, "engine reset; awaiting pulse"); };
        engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
            atomic_fetch_add_explicit(&g_haptic_stops, 1, memory_order_relaxed);
            char text[96];
            snprintf(text, sizeof text, "engine stopped (%ld); awaiting pulse", (long)reason);
            haptic_needs_restart(slot, generation, text);
        };
    }
    os_unfair_lock_lock(&g_haptic_lock);
    bool needs_start = h->needs_start;
    /* Clear before Start so a stop/reset arriving during Start is preserved. */
    h->needs_start = false;
    os_unfair_lock_unlock(&g_haptic_lock);
    if (needs_start) {
        NSError *error = nil;
        if (![h->engine startAndReturnError:&error]) {
            atomic_fetch_add_explicit(&g_haptic_failures, 1, memory_order_relaxed);
            char text[96];
            snprintf(text, sizeof text, "engine start failed: %s",
                     error.localizedDescription.UTF8String ?: "?");
            haptic_discard_slot(slot);
            haptic_note(text); return nil;
        }
    }
    return h->engine;
}

/* A transient, optionally followed by a softer continuous tail (seconds).
 * admit, when given, is asked after the engine is ready (its Start may block)
 * and again after the player is created (another framework call that may
 * block), immediately before the player starts; a "no" plays nothing. */
static bool haptic_play_slot_locked(unsigned slot, GCController *pad, float intensity, float sharpness, float tail,
                                    bool (^admit)(void)) {
    CHHapticEngine *engine = haptic_engine_for(slot, pad);
    if (!engine) return false;
    CHHapticEventParameter *i =
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                      value:intensity];
    CHHapticEventParameter *s =
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                      value:sharpness];
    NSMutableArray<CHHapticEvent *> *events = [NSMutableArray arrayWithObject:
        [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient
                                      parameters:@[i, s] relativeTime:0]];
    if (tail > 0.f) {
        CHHapticEventParameter *ti =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                          value:intensity * 0.6f];
        CHHapticEventParameter *ts =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                          value:sharpness * 0.5f];
        [events addObject:[[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                        parameters:@[ti, ts] relativeTime:0 duration:tail]];
    }
    NSError *error = nil;
    CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:events
                                                            parameters:@[] error:&error];
    if (!pattern) { atomic_fetch_add_explicit(&g_haptic_failures, 1, memory_order_relaxed); haptic_note("pattern build failed"); return false; }
    if (admit && !admit()) return false;
    id<CHHapticPatternPlayer> player = [engine createPlayerWithPattern:pattern error:&error];
    if (player && admit && !admit()) return false;
    if (!player || ![player startAtTime:CHHapticTimeImmediate error:&error]) {
        atomic_fetch_add_explicit(&g_haptic_failures, 1, memory_order_relaxed);
        /* A disconnected or reset engine may remain unusable even if Start
         * succeeds. Drop it so the next pulse can acquire a fresh engine. */
        haptic_discard_slot(slot);
        haptic_note("play failed; retrying next pulse");
        return false;
    }
    atomic_store_explicit(&g_last_played_ns, now_ns(), memory_order_relaxed);
    os_unfair_lock_lock(&g_haptic_lock);
    g_haptics_played++;
    if (!g_haptic_slots[slot].needs_start)
        snprintf(g_haptic_state, sizeof g_haptic_state, "ready (%s)", pad.vendorName.UTF8String ?: "controller");
    os_unfair_lock_unlock(&g_haptic_lock);
    return true;
}

static void haptic_play_locked(float intensity, float sharpness) {
    (void)haptic_play_slot_locked(1, current_input_controller(), intensity, sharpness, 0.f, nil);
}

void hostgc_play_haptic(float intensity, float sharpness) {
    if (!(intensity > 0.f)) return;
    if (intensity > 1.f) intensity = 1.f;
    if (!(sharpness >= 0.f)) sharpness = 0.f;
    if (sharpness > 1.f) sharpness = 1.f;
    hostgc_init();
    if (g_guard_enabled) {
        os_unfair_lock_lock(&g_guard_lock);
        int verdict = hostgc_guard_admit(&g_guard, now_ns());
        os_unfair_lock_unlock(&g_guard_lock);
        if (verdict == HOSTGC_GUARD_SUSPENDED) haptic_note("suspended after controller drops");
        if (verdict != HOSTGC_GUARD_PLAY) return;
    }
    dispatch_async(haptic_queue(), ^{
        @autoreleasepool {
            pthread_mutex_lock(&g_haptic_play_lock);
            haptic_play_locked(intensity, sharpness);
            pthread_mutex_unlock(&g_haptic_play_lock);
        }
    });
}

/* Semantic weapon events (gamecontroller.h). The admission thread checks only
 * what it can know; the queue rechecks cancellation, topology, age and native
 * UI ownership, because any of them may change while the event waits. */
static _Atomic uint64_t g_spatial_epoch[2] = {1, 1}; /* per HOSTGC_SPATIAL_* kind */
static _Atomic uint64_t g_spatial_requested, g_spatial_queued, g_spatial_played, g_spatial_dropped_guard,
                        g_spatial_dropped_stale, g_spatial_dropped_cancelled, g_spatial_dropped_not_spatial;
static void spatial_count(_Atomic uint64_t *counter) { atomic_fetch_add_explicit(counter, 1, memory_order_relaxed); }
static float unit_clamp(float v) { return v > 0.f ? (v < 1.f ? v : 1.f) : 0.f; } /* NaN -> 0 */
typedef struct { HostGCSpatialHaptic event; uint64_t epoch, queued_ns; } HostGCSpatialLease;
enum { SPATIAL_VALID = 0, SPATIAL_CANCELLED, SPATIAL_STALE, SPATIAL_NOT_SPATIAL };
/* Everything that can end ownership while an event waits or an engine starts. */
static int spatial_lease_check(const HostGCSpatialLease *lease, unsigned side, GCController *pad) {
    if (lease->epoch != atomic_load_explicit(&g_spatial_epoch[lease->event.kind], memory_order_acquire)) return SPATIAL_CANCELLED;
    uint64_t now = now_ns();
    if (now < lease->queued_ns || now - lease->queued_ns > UINT64_C(100000000) ||
        lease->event.source_generation != hostgc_source_generation()) return SPATIAL_STALE;
    /* Master enable (haptics.c) as well as the bridge's toggle and strength. */
    if (hostgc_app_input_blocked() || !halo_haptics_enabled() ||
        !halo_sense_weapon_haptics_admit(lease->event.session, lease->event.generation, lease->event.lease)) return SPATIAL_CANCELLED;
    NSArray<GCController *> *pair = controllers_for_input();
    if (pair.count != 2 || (pad && pair[side] != pad)) return SPATIAL_NOT_SPATIAL;
    return SPATIAL_VALID;
}
static void spatial_drop(int verdict) {
    spatial_count(verdict == SPATIAL_STALE ? &g_spatial_dropped_stale :
                  verdict == SPATIAL_NOT_SPATIAL ? &g_spatial_dropped_not_spatial : &g_spatial_dropped_cancelled);
}

static void spatial_play_locked(HostGCSpatialLease lease) {
    int verdict = spatial_lease_check(&lease, 0, nil);
    if (verdict != SPATIAL_VALID) { spatial_drop(verdict); return; }
    NSArray<GCController *> *pair = controllers_for_input();
    bool played = false;
    __block int late = SPATIAL_VALID;
    for (unsigned side = 0; side < 2 && late == SPATIAL_VALID; ++side) {
        if (!(lease.event.intensity[side] > 0.f)) continue;
        GCController *pad = pair[side];
        bool (^admit)(void) = ^bool {
            late = spatial_lease_check(&lease, side, pad);
            return late == SPATIAL_VALID;
        };
        if (haptic_play_slot_locked(side, pad, lease.event.intensity[side], lease.event.sharpness[side],
                                    lease.event.tail[side], admit))
            played = true;
    }
    if (late != SPATIAL_VALID) spatial_drop(late);
    if (played) spatial_count(&g_spatial_played);
}

int hostgc_play_spatial_haptic(const HostGCSpatialHaptic *in) {
    if (!in) return 0;
    HostGCSpatialHaptic event = *in;
    for (unsigned side = 0; side < 2; ++side) {
        event.intensity[side] = unit_clamp(event.intensity[side]);
        event.sharpness[side] = unit_clamp(event.sharpness[side]);
        event.tail[side] = unit_clamp(event.tail[side] * 2.f) * 0.5f; /* 0...0.5 s */
    }
    if (!(event.intensity[0] > 0.f) && !(event.intensity[1] > 0.f)) return 0;
    if (event.kind != HOSTGC_SPATIAL_SHOT && event.kind != HOSTGC_SPATIAL_CHARGE) return 0;
    spatial_count(&g_spatial_requested);
    if (!event.source_generation || event.source_generation != hostgc_source_generation()) {
        spatial_count(&g_spatial_dropped_stale); return 0;
    }
    /* A disabled master or a lease that is already gone never takes a guard slot. */
    if (!halo_haptics_enabled() || !halo_sense_weapon_haptics_admit(event.session, event.generation, event.lease)) {
        spatial_count(&g_spatial_dropped_cancelled); return 0;
    }
    hostgc_init();
    if (g_guard_enabled) {
        os_unfair_lock_lock(&g_guard_lock);
        int verdict = hostgc_guard_admit(&g_guard, now_ns());
        os_unfair_lock_unlock(&g_guard_lock);
        if (verdict == HOSTGC_GUARD_SUSPENDED) haptic_note("suspended after controller drops");
        if (verdict != HOSTGC_GUARD_PLAY) { spatial_count(&g_spatial_dropped_guard); return 0; }
    }
    HostGCSpatialLease lease = {.event = event, .queued_ns = now_ns(),
        .epoch = atomic_load_explicit(&g_spatial_epoch[event.kind], memory_order_acquire)};
    spatial_count(&g_spatial_queued);
    dispatch_async(haptic_queue(), ^{
        @autoreleasepool {
            pthread_mutex_lock(&g_haptic_play_lock);
            spatial_play_locked(lease);
            pthread_mutex_unlock(&g_haptic_play_lock);
        }
    });
    return 1;
}

void hostgc_cancel_spatial_haptics(int kind) {
    for (int k = HOSTGC_SPATIAL_SHOT; k <= HOSTGC_SPATIAL_CHARGE; ++k)
        if (kind == HOSTGC_SPATIAL_ALL || kind == k)
            atomic_fetch_add_explicit(&g_spatial_epoch[k], 1, memory_order_acq_rel);
}

void hostgc_spatial_haptic_stats(HostGCSpatialHapticStats *out) {
    if (!out) return;
    *out = (HostGCSpatialHapticStats){
        .requested = atomic_load_explicit(&g_spatial_requested, memory_order_relaxed),
        .queued = atomic_load_explicit(&g_spatial_queued, memory_order_relaxed),
        .played = atomic_load_explicit(&g_spatial_played, memory_order_relaxed),
        .dropped_guard = atomic_load_explicit(&g_spatial_dropped_guard, memory_order_relaxed),
        .dropped_stale = atomic_load_explicit(&g_spatial_dropped_stale, memory_order_relaxed),
        .dropped_cancelled = atomic_load_explicit(&g_spatial_dropped_cancelled, memory_order_relaxed),
        .dropped_not_spatial = atomic_load_explicit(&g_spatial_dropped_not_spatial, memory_order_relaxed),
    };
}

static void haptic_release_on_disconnect(void) {
    dispatch_async(haptic_queue(), ^{
        @autoreleasepool {
            pthread_mutex_lock(&g_haptic_play_lock);
            bool released = false;
            for (unsigned slot = 0; slot < 2; ++slot)
                if (g_haptic_slots[slot].engine) { haptic_discard_slot(slot); released = true; }
            if (released) haptic_note("released: controller disconnected");
            pthread_mutex_unlock(&g_haptic_play_lock);
        }
    });
}

void hostgc_link_stats(HostGCLinkStats *out) {
    if (!out) return;
    os_unfair_lock_lock(&g_guard_lock);
    uint64_t now = now_ns();
    *out = (HostGCLinkStats){
        .connects = g_guard.connects, .disconnects = g_guard.disconnects,
        .linked_disconnects = g_guard.linked_disconnects, .suspensions = g_guard.suspensions,
        .pulses_admitted = g_guard.admitted, .pulses_dropped_rate = g_guard.dropped_rate,
        .pulses_dropped_grace = g_guard.dropped_grace, .pulses_dropped_suspended = g_guard.dropped_suspended,
        .pulses_dropped_disconnected = g_guard.dropped_disconnected,
        .suspended_ms_left = g_guard.suspended_until_ns > now ? (uint32_t)((g_guard.suspended_until_ns - now) / 1000000ull) : 0,
        .guard_enabled = g_guard_enabled,
        .haptic_failures = atomic_load_explicit(&g_haptic_failures, memory_order_relaxed),
        .haptic_resets = atomic_load_explicit(&g_haptic_resets, memory_order_relaxed),
        .haptic_stops = atomic_load_explicit(&g_haptic_stops, memory_order_relaxed),
        .last_connect_ns = atomic_load_explicit(&g_last_connect_ns, memory_order_relaxed),
        .last_disconnect_ns = atomic_load_explicit(&g_last_disconnect_ns, memory_order_relaxed),
        .last_played_ns = atomic_load_explicit(&g_last_played_ns, memory_order_relaxed),
    };
    os_unfair_lock_unlock(&g_guard_lock);
}

const char *hostgc_haptic_state(void) {
    /* Returning the shared buffer exposed writes to Swift's String(cString:).
     * Keep the existing ABI, but hand each reader its own stable snapshot. */
    static _Thread_local char snapshot[sizeof g_haptic_state];
    os_unfair_lock_lock(&g_haptic_lock);
    memcpy(snapshot, g_haptic_state, sizeof snapshot);
    os_unfair_lock_unlock(&g_haptic_lock);
    return snapshot;
}
uint64_t hostgc_haptics_played(void) {
    os_unfair_lock_lock(&g_haptic_lock);
    uint64_t n = g_haptics_played;
    os_unfair_lock_unlock(&g_haptic_lock);
    return n;
}

/* Keep a hardware reading as the one to hand out. A reading that started
 * before the kept one (two threads capturing at once) does not replace it. */
static void remember_reading(HostGCSnapshot *reading, uint64_t started_ns, uint64_t topology) {
    os_unfair_lock_lock(&g_lock);
    if (topology != g_topology) memset(reading, 0, sizeof *reading);
    reading->sequence = ++g_sequence;
    reading->source_generation = hostgc_source_generation();
    g_captures++;
    if (topology == g_topology && started_ns >= g_recent_ns) { g_recent = *reading; g_recent_ns = started_ns; }
    os_unfair_lock_unlock(&g_lock);
}

static bool capture_reading(HostGCSnapshot *out);

bool hostgc_poll(HostGCSnapshot *out) {
    if (!out) return false;
    /* Timed from before the capture, so a reading's age is never understated. */
    uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), window = reuse_window_ns();

    os_unfair_lock_lock(&g_lock);
    if (g_have_test) { *out = g_test; os_unfair_lock_unlock(&g_lock); app_input_observe(out, now); return out->connected; }
    if (window && g_recent_ns && (now < g_recent_ns || now - g_recent_ns < window)) {
        *out = g_recent; g_reuses++;
        uint64_t captured_ns = g_recent_ns;
        os_unfair_lock_unlock(&g_lock);
        app_input_observe(out, captured_ns);
        return out->connected;
    }
    uint64_t topology = g_topology;
    os_unfair_lock_unlock(&g_lock);

    /* The capture runs unlocked: the main thread's diagnostics may read the
     * pad at the same moment and must not wait on GameController here. */
    capture_reading(out);
    remember_reading(out, now, topology);
    app_input_observe(out, now);
    return out->connected;
}

void hostgc_poll_stats(uint64_t *captures, uint64_t *reuses, uint32_t *window_us) {
    uint64_t window = reuse_window_ns();
    os_unfair_lock_lock(&g_lock);
    if (captures) *captures = g_captures;
    if (reuses) *reuses = g_reuses;
    os_unfair_lock_unlock(&g_lock);
    if (window_us) *window_us = (uint32_t)(window / 1000u);
}

/* The two captures are individually coherent; a topology change during either
 * capture rejects the whole reading in remember_reading(). No previous half's
 * values are retained when a device is missing or reconnecting. */
static bool capture_spatial_pair(NSArray<GCController *> *controllers, HostGCSnapshot *out) {
#if TARGET_OS_VISION || defined(HOSTGC_SPATIAL_TESTING)
    if (@available(macOS 26.0, iOS 26.0, visionOS 26.0, *)) {
        GCControllerInputState *left = [controllers[0].input capture];
        GCControllerInputState *right = [controllers[1].input capture];
        if (!left || !right || !left.dpads[GCInputThumbstick] || !right.dpads[GCInputThumbstick]
            || !left.buttons[GCInputTrigger] || !right.buttons[GCInputTrigger]) return false;
        out->connected = true;
        out->spatial = true;
        out->lx = left.dpads[GCInputThumbstick].xAxis.value;
        out->ly = left.dpads[GCInputThumbstick].yAxis.value;
        out->spatial_raw_lx = out->lx; out->spatial_raw_ly = out->ly;
        out->rx = right.dpads[GCInputThumbstick].xAxis.value;
        out->ry = right.dpads[GCInputThumbstick].yAxis.value;
        out->lt = left.buttons[GCInputTrigger].pressedInput.value;
        out->rt = right.buttons[GCInputTrigger].pressedInput.value;
        out->buttons[HOSTGC_BTN_X] = left.buttons[GCInputButtonA].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_Y] = left.buttons[GCInputButtonB].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_A] = right.buttons[GCInputButtonA].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_B] = right.buttons[GCInputButtonB].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_LSHOULDER] = left.buttons[GCInputGripButton].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_RSHOULDER] = right.buttons[GCInputGripButton].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_LTRIGGER] = left.buttons[GCInputTrigger].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_RTRIGGER] = right.buttons[GCInputTrigger].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_LTHUMB] = left.buttons[GCInputThumbstickButton].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_RTHUMB] = right.buttons[GCInputThumbstickButton].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_MENU] = right.buttons[GCInputButtonMenu].pressedInput.pressed;
        out->buttons[HOSTGC_BTN_HOME] = left.buttons[GCInputButtonHome].pressedInput.pressed
                                   || right.buttons[GCInputButtonHome].pressedInput.pressed;
        /* Sense has no D-pad. Create + left stick exposes Halo's four extra
         * actions without firing the grip's grenade or moving the player.
         * Centered Create retains the scoreboard action. */
        if (left.buttons[GCInputButtonMenu].pressedInput.pressed) {
            out->dpad_up = out->ly > 0.55f;
            out->dpad_down = out->ly < -0.55f;
            out->dpad_left = out->lx < -0.55f;
            out->dpad_right = out->lx > 0.55f;
            out->buttons[HOSTGC_BTN_OPTIONS] = !(out->dpad_up || out->dpad_down || out->dpad_left || out->dpad_right);
            out->lx = out->ly = 0.f;
        }
        out->buttons[HOSTGC_BTN_DPAD_UP] = out->dpad_up;
        out->buttons[HOSTGC_BTN_DPAD_DOWN] = out->dpad_down;
        out->buttons[HOSTGC_BTN_DPAD_LEFT] = out->dpad_left;
        out->buttons[HOSTGC_BTN_DPAD_RIGHT] = out->dpad_right;
        return true;
    }
#endif
    return false;
}

static bool capture_reading(HostGCSnapshot *out) {
    @autoreleasepool {
    memset(out, 0, sizeof *out);
    NSArray<GCController *> *controllers = controllers_for_input();
    if (controllers.count == 2) return capture_spatial_pair(controllers, out);
    GCController *controller = controllers.firstObject;
    /* capture returns an immutable, internally consistent reading.  Reading
     * each live element directly could otherwise mix two hardware reports. */
    GCController *captured = controller ? [controller capture] : nil;
    GCExtendedGamepad *g = captured.extendedGamepad;
    if (!g) return false;

    out->connected = true;
    out->lx = g.leftThumbstick.xAxis.value;
    out->ly = g.leftThumbstick.yAxis.value;
    out->rx = g.rightThumbstick.xAxis.value;
    out->ry = g.rightThumbstick.yAxis.value;
    out->lt = g.leftTrigger.value;
    out->rt = g.rightTrigger.value;
    out->dpad_up    = g.dpad.up.pressed;
    out->dpad_down  = g.dpad.down.pressed;
    out->dpad_left  = g.dpad.left.pressed;
    out->dpad_right = g.dpad.right.pressed;

    out->buttons[HOSTGC_BTN_A]         = g.buttonA.pressed;
    out->buttons[HOSTGC_BTN_B]         = g.buttonB.pressed;
    out->buttons[HOSTGC_BTN_X]         = g.buttonX.pressed;
    out->buttons[HOSTGC_BTN_Y]         = g.buttonY.pressed;
    out->buttons[HOSTGC_BTN_LSHOULDER] = g.leftShoulder.pressed;
    out->buttons[HOSTGC_BTN_RSHOULDER] = g.rightShoulder.pressed;
    out->buttons[HOSTGC_BTN_LTRIGGER]  = g.leftTrigger.pressed;
    out->buttons[HOSTGC_BTN_RTRIGGER]  = g.rightTrigger.pressed;
    out->buttons[HOSTGC_BTN_LTHUMB]    = g.leftThumbstickButton.pressed;
    out->buttons[HOSTGC_BTN_RTHUMB]    = g.rightThumbstickButton.pressed;
    if (@available(macOS 10.15, iOS 13.0, *)) {
        out->buttons[HOSTGC_BTN_MENU]    = g.buttonMenu.pressed;
        out->buttons[HOSTGC_BTN_OPTIONS] = g.buttonOptions.pressed;
    }
    if (@available(macOS 11.0, iOS 14.0, *))
        out->buttons[HOSTGC_BTN_HOME]    = g.buttonHome.pressed;
    out->buttons[HOSTGC_BTN_DPAD_UP]    = out->dpad_up;
    out->buttons[HOSTGC_BTN_DPAD_DOWN]  = out->dpad_down;
    out->buttons[HOSTGC_BTN_DPAD_LEFT]  = out->dpad_left;
    out->buttons[HOSTGC_BTN_DPAD_RIGHT] = out->dpad_right;
    return true;
    }
}

bool hostgc_connected(void) {
    @autoreleasepool {
    os_unfair_lock_lock(&g_lock);
    if (g_have_test) { bool c = g_test.connected; os_unfair_lock_unlock(&g_lock); return c; }
    os_unfair_lock_unlock(&g_lock);
    return current_input_controller() != nil;
    }
}

const char *hostgc_input_state(void) {
    static _Thread_local char text[160];
    @autoreleasepool {
        NSArray<GCController *> *selected = controllers_for_input();
        if (selected.count == 2) {
            snprintf(text, sizeof text, "PS VR2 Sense pair ready");
        } else if (selected.count == 1) {
            snprintf(text, sizeof text, "Gamepad ready (%s)", selected.firstObject.vendorName.UTF8String ?: "controller");
        } else {
            NSUInteger count = 0, left = 0, right = 0;
            for (GCController *controller in attached_controllers()) if (spatial_controller(controller)) {
                ++count;
                NSInteger side = spatial_side(controller);
                left += side == 1; right += side == 2;
            }
            const char *state = !count ? "No controller connected"
                : count > 2 || left > 1 || right > 1 ? "Sense pair ambiguous; connect one left and one right"
                : count < 2 ? "Sense needs both left and right controllers"
                : left + right < 2 ? "Identifying Sense controllers; reconnect if unresolved"
                : "Sense pair unavailable";
            snprintf(text, sizeof text, "%s", state);
        }
    }
    return text;
}

void hostgc_inject_test_snapshot(const HostGCSnapshot *snap) {
    if (!snap) { hostgc_clear_test_snapshot(); return; }
    os_unfair_lock_lock(&g_lock);
    g_have_test = true;
    g_test = *snap;
    os_unfair_lock_unlock(&g_lock);
}

void hostgc_clear_test_snapshot(void) {
    os_unfair_lock_lock(&g_lock);
    g_have_test = false;
    os_unfair_lock_unlock(&g_lock);
}
