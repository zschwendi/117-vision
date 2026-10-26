/* Optional physical Sense features at the verified Halo PC 1.10 boundaries.
 * Included by sense_weapon_hooks.inc (engine thread only). Default off.
 *
 * Connected arms: after original004D6880 animates the first-person graph
 * and the tracked wrist/gun deltas are applied, the upper arm and forearm
 * are solved from the original shoulder to the tracked wrist. Wrist, gun,
 * marker and every firing value are read only here, so the build118 muzzle,
 * reticle, scope and final pre-spread ray stay identical. The chain must be
 * exactly 'frame X upperarm' -> 'frame X forearm' -> 'frame X wriste'
 * (HCEEK fixtures; PC1.10 runtime AR graph count42/left5/right6/gun7). */
#include "sense_arm_ik.h"
#include "sense_physical.h"
/* When the body lane draws the real third-person body with its arms, the
 * connected first-person upper arms/forearms would be a second pair: keep
 * the existing collapse then. Without that lane the gate is off. */
#if __has_include("sense_body.h")
#include "sense_body.h"
#define SENSE_PHYSICAL_BODY_ACTIVE() host_sense_body_active()
#else
#define SENSE_PHYSICAL_BODY_ACTIVE() 0
#endif

static _Atomic int sense_connected_arms_enabled;
void halo_sense_set_connected_arms(int enabled) { atomic_store(&sense_connected_arms_enabled, !!enabled); }
int halo_sense_connected_arms(void) { return atomic_load(&sense_connected_arms_enabled); }

int halo_sense_physical_magazine(void);
static _Atomic int sense_mag_status_support, sense_mag_status_stage, sense_mag_status_result, sense_mag_status_charge;
static _Atomic uint64_t sense_mag_status_ns;
static _Atomic int sense_physical_arm_state[2];
static _Atomic float sense_physical_arm_shift[2];
static _Atomic uint64_t sense_physical_arm_ns;
int host_sense_physical_status(HaloSensePhysicalStatus *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    out->arms_enabled = halo_sense_connected_arms();
    uint64_t now = host_monotonic_ns(), stamp = atomic_load(&sense_physical_arm_ns);
    int fresh = halo_sense_active() && stamp && now >= stamp && now - stamp <= UINT64_C(250000000);
    if (fresh) for (unsigned h = 0; h < 2; ++h) {
        out->arm[h] = atomic_load(&sense_physical_arm_state[h]);
        out->shoulder_shift_m[h] = atomic_load(&sense_physical_arm_shift[h]);
    }
    out->magazine_enabled = halo_sense_physical_magazine();
    out->charge_enabled = halo_sense_physical_magazine() && halo_sense_physical_charge();
    stamp = atomic_load(&sense_mag_status_ns);
    if (halo_sense_active() && stamp && now >= stamp && now - stamp <= UINT64_C(250000000)) {
        out->magazine_support = atomic_load(&sense_mag_status_support);
        out->magazine_stage = atomic_load(&sense_mag_status_stage);
        out->magazine_result = atomic_load(&sense_mag_status_result);
        out->charge_supported = atomic_load(&sense_mag_status_charge);
        fresh = 1;
    }
    return fresh;
}

typedef struct {
    int upper[2], fore[2], wrist[2], qualified[2];
    SenseArmNode base_upper[2], base_fore[2], base_wrist[2];
} SenseArmCapture;

static int sense_arm_named(uint32_t bones, unsigned count, const char *wanted) {
    int found = -1;
    for (unsigned i = 0; i < count; ++i) {
        char name[33] = {0}; memcpy(name, GPTR(bones + i*64u), 32);
        if (!strcmp(name, wanted)) { if (found >= 0) return -2; found = (int)i; }
    }
    return found;
}
/* Qualify both chains against the actual graph and save the original
 * animated chain before any tracked delta. Unknown rigs stay unqualified. */
static void sense_arms_capture(SenseArmCapture *arms, uint32_t output, uint32_t bones, unsigned count,
                               const int16_t *parents, int left, int right) {
    memset(arms, 0, sizeof *arms);
    if (!halo_sense_connected_arms()) return;
    static const char *names[2][3] = {{"frame l upperarm", "frame l forearm", "frame l wriste"},
                                      {"frame r upperarm", "frame r forearm", "frame r wriste"}};
    const int wrists[2] = {left, right};
    for (unsigned h = 0; h < 2; ++h) {
        int u = sense_arm_named(bones, count, names[h][0]), f = sense_arm_named(bones, count, names[h][1]);
        int w = sense_arm_named(bones, count, names[h][2]);
        arms->upper[h] = u; arms->fore[h] = f; arms->wrist[h] = w;
        if (u < 0 || f < 0 || w < 0 || w != wrists[h] || parents[w] != f || parents[f] != u) continue;
        memcpy(&arms->base_upper[h], GPTR(output + (unsigned)u*52u), 52);
        memcpy(&arms->base_fore[h], GPTR(output + (unsigned)f*52u), 52);
        memcpy(&arms->base_wrist[h], GPTR(output + (unsigned)w*52u), 52);
        arms->qualified[h] = sense_arm_node_valid(&arms->base_upper[h]) && sense_arm_node_valid(&arms->base_fore[h]) &&
            sense_arm_node_valid(&arms->base_wrist[h]);
    }
}
/* Hysteresis is valid only for the same unit, weapon, rig graph, Sense
 * session and input generation (recenter/tracking/scope/controller epoch). */
static struct {
    uint32_t unit, weapon, graph; uint64_t session, generation; int held[2];
    uint64_t epoch; int epoch_held[2];
} sense_arm_identity;
/* Runs after the existing collapse. visible[h] is 1 only for a tracked,
 * shown hand on the primary (unreflected) rig. Writes only the upper-arm
 * and forearm records of a solved chain. */
static void sense_arms_connect(const SenseArmCapture *arms, uint32_t output, const int visible[2],
                               uint32_t unit, uint32_t weapon, uint32_t graph) {
    if (!halo_sense_connected_arms()) {
        atomic_store(&sense_physical_arm_state[0], HALO_SENSE_ARM_OFF);
        atomic_store(&sense_physical_arm_state[1], HALO_SENSE_ARM_OFF);
        atomic_store(&sense_physical_arm_ns, host_monotonic_ns());
        memset(&sense_arm_identity, 0, sizeof sense_arm_identity); return;
    }
    HaloSenseDualInput input = {0};
    int have_input = halo_sense_dual_input(&input);
    if (!have_input || sense_arm_identity.unit != unit || sense_arm_identity.weapon != weapon ||
        sense_arm_identity.graph != graph || sense_arm_identity.session != input.session ||
        sense_arm_identity.generation != input.generation) {
        memset(&sense_arm_identity, 0, sizeof sense_arm_identity);
        sense_arm_identity.unit = unit; sense_arm_identity.weapon = weapon; sense_arm_identity.graph = graph;
        sense_arm_identity.session = input.session; sense_arm_identity.generation = input.generation;
    }
    float down[3] = {-sense_camera.c[2][0], -sense_camera.c[2][1], -sense_camera.c[2][2]};
    int state[2], body = SENSE_PHYSICAL_BODY_ACTIVE();
    /* Hysteresis enters each epoch once: repeated builds/eyes of one epoch
     * solve with the same thresholds. */
    uint64_t epoch = sense_accepted_frame_epoch();
    if (sense_arm_identity.epoch != epoch) {
        sense_arm_identity.epoch = epoch;
        for (unsigned h = 0; h < 2; ++h) sense_arm_identity.epoch_held[h] = sense_arm_identity.held[h];
    }
    for (unsigned h = 0; h < 2; ++h) {
        if (!visible[h]) { state[h] = HALO_SENSE_ARM_HIDDEN; sense_arm_identity.held[h] = 0; continue; }
        if (body) { state[h] = HALO_SENSE_ARM_BODY; sense_arm_identity.held[h] = 0; continue; }
        if (!arms->qualified[h]) { state[h] = HALO_SENSE_ARM_UNKNOWN_RIG; sense_arm_identity.held[h] = 0; continue; }
        SenseArmNode wrist, upper, fore; float shift = 0;
        memcpy(&wrist, GPTR(output + (unsigned)arms->wrist[h]*52u), 52);
        int result = sense_arm_solve_held(&arms->base_upper[h], &arms->base_fore[h], &arms->base_wrist[h], &wrist, down,
                                          have_input && sense_arm_identity.epoch_held[h], &upper, &fore, &shift);
        sense_arm_identity.held[h] = result == SENSE_ARM_SOLVED;
        if (result == SENSE_ARM_SOLVED) {
            atomic_store(&sense_physical_arm_shift[h], shift*3.048f);
            memcpy(GPTR(output + (unsigned)arms->upper[h]*52u), &upper, 52);
            memcpy(GPTR(output + (unsigned)arms->fore[h]*52u), &fore, 52);
            state[h] = HALO_SENSE_ARM_CONNECTED;
        } else state[h] = result == SENSE_ARM_UNREACHABLE ? HALO_SENSE_ARM_OUT_OF_REACH : HALO_SENSE_ARM_DEGENERATE;
    }
    for (unsigned h = 0; h < 2; ++h) {
        atomic_store(&sense_physical_arm_state[h], state[h]);
        if (state[h] != HALO_SENSE_ARM_CONNECTED) atomic_store(&sense_physical_arm_shift[h], 0.f);
    }
    atomic_store(&sense_physical_arm_ns, host_monotonic_ns());
    if (HOST_ENV("HALO_SENSE_PHYSICAL_TRACE")) {
        static unsigned logs;
        if (logs++ < 24) {
            float reach[2] = {0, 0}, need[2] = {0, 0};
            for (unsigned h = 0; h < 2; ++h) if (arms->qualified[h]) {
                float s[3], e[3], w[3]; SenseArmNode wn; memcpy(&wn, GPTR(output + (unsigned)arms->wrist[h]*52u), 52);
                for (unsigned k = 0; k < 3; ++k) { s[k] = arms->base_fore[h].p[k]-arms->base_upper[h].p[k];
                    e[k] = arms->base_wrist[h].p[k]-arms->base_fore[h].p[k]; w[k] = wn.p[k]-arms->base_upper[h].p[k]; }
                reach[h] = (sqrtf(sense_dot(s, s))+sqrtf(sense_dot(e, e)))*3.048f; need[h] = sqrtf(sense_dot(w, w))*3.048f;
            }
            host_log("[sense-arms] chain L=%d/%d/%d R=%d/%d/%d qualified=%d,%d state=%d,%d reach_m=%.3f,%.3f needed_m=%.3f,%.3f",
                     arms->upper[0], arms->fore[0], arms->wrist[0], arms->upper[1], arms->fore[1], arms->wrist[1],
                     arms->qualified[0], arms->qualified[1], state[0], state[1], reach[0], reach[1], need[0], need[1]);
        }
    }
}

/* ---- Physical magazine (default off) ----------------------------------
 * Supported only for the stock assault rifle, pistol and sniper rifle whose
 * first-person graph and first-person model both name 'frame magazine'
 * under the gun node (HCEEK model audit: magazine vertices exclusively
 * weighted to that node, none blended with the gun). Engine values are the
 * PC1.10 magazine ABI verified in original 004C29D0: weapon object +0x2B0
 * magazine[0] {state +0 (0 idle, 1 reloading, 3 chambering), reserve +6,
 * loaded +8}; weapon tag +0x4F0 count, +0x4F4 block (stride 0x70),
 * rounds_loaded_maximum +0xA. Values are only read. The commit is one
 * original R through the Sense DirectInput bridge; the original game then
 * runs its own reload gates and ammo transfer exactly once. */
#include "sense_magazine.h"
/* One sequence space for every physical commit, as the transport has. */
static uint64_t sense_physical_sequence;
static _Atomic int sense_physical_magazine_enabled;
void halo_sense_set_physical_magazine(int enabled) {
    atomic_store(&sense_physical_magazine_enabled, !!enabled);
    if (!enabled) halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_RELOAD);
}
int halo_sense_physical_magazine(void) { return atomic_load(&sense_physical_magazine_enabled); }
/* Optional charging phase of the physical magazine (its own setting). */
static _Atomic int sense_physical_charge_enabled;
void halo_sense_set_physical_charge(int enabled) {
    atomic_store(&sense_physical_charge_enabled, !!enabled);
    if (!enabled) halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_RELOAD);
}
int halo_sense_physical_charge(void) { return atomic_load(&sense_physical_charge_enabled); }
static struct {
    SenseMagState state;
    uint32_t unit, weapon; uint64_t session, generation;
    HaloSenseTransform grip; /* magazine relative to the tracked left grip */
    HaloSenseTransform seat; /* last idle seated magazine relative to the gun node */
    HaloSenseTransform handle_seat; /* last idle charging handle/slide relative to the gun node */
    float charge_start[3]; /* hand at the handle grip, in the gun node's own frame */
    int have_grip, have_seat, have_handle_seat, pinned, charge_support;
} sense_mag;
/* One aim authority per accepted frame. Left-hand ownership (which turns
 * the two-hand support off) and 'magazine drawn out of the gun' are latched
 * once at the start of each primary first-person build, before any support,
 * muzzle, reticle or scope solve, and read unchanged by every consumer
 * until the next build, including the original fire boundary. A stage
 * change decided during build N therefore draws seated in N and changes
 * support only from N+1; the settings value is latched the same way. */
static int sense_mag_feature, sense_charge_feature, sense_mag_left_owned, sense_mag_drawn_removed;
static int sense_mag_owned_stage(int stage) {
    return stage == SENSE_MAG_GRIPPED || stage == SENSE_MAG_OUT || stage == SENSE_MAG_DISCARDED || stage == SENSE_MAG_NEW ||
        stage == SENSE_MAG_CHARGE_GRIPPED || stage == SENSE_MAG_CHARGE_PULLED;
}
static int sense_pick_left_owned;
static int sense_pickup_left_stage(void);
static int sense_pickup_holds_left_intent(void);
static void sense_physical_frame_begin(void) {
    sense_mag_feature = halo_sense_physical_magazine();
    sense_charge_feature = sense_mag_feature && halo_sense_physical_charge();
    sense_mag_left_owned = sense_mag_feature && sense_mag_owned_stage(sense_mag.state.stage);
    sense_pick_left_owned = sense_pickup_left_stage(); /* a weapon flying to / held in the left hand */
    sense_mag_drawn_removed = 0;
}
static int sense_physical_left_busy(void) { return sense_mag_left_owned || sense_pick_left_owned; }
/* The original reload a physical insertion requested for this same unit,
 * weapon, Sense session and input generation (awaiting confirmation, or
 * confirmed and pinned): its magazine stays seated in the tracked hands, so
 * the native reload animation does not take that one reload over. Any
 * other reload (button, gesture, another weapon/owner, an older lease) is
 * native even with the option on. */
static int sense_physical_reload_owned(uint32_t datum, uint32_t weapon) {
    if (!sense_mag_feature || sense_mag.unit != datum || sense_mag.weapon != weapon ||
        (sense_mag.state.stage != SENSE_MAG_COMMITTED && !sense_mag.pinned)) return 0;
    HaloSenseDualInput input;
    return halo_sense_dual_input(&input) && input.session == sense_mag.session && input.generation == sense_mag.generation;
}
/* The rendered gun has no magazine (held, discarded or new in hand): the
 * original trigger must not fire it. R2 also cancels the stage, and the
 * next build seats the magazine before a shot is accepted. */
static uint64_t sense_mag_drawn_epoch;
static int sense_physical_fire_blocked(void) {
    /* This epoch's drawn presentation when its build ran; before that (a
     * shot admitted ahead of the first build) the latched ownership, whose
     * stages are exactly those drawn out of the gun. */
    if (sense_mag_drawn_epoch && sense_mag_drawn_epoch == sense_accepted_frame_epoch()) return sense_mag_drawn_removed;
    return sense_mag_left_owned;
}
static void sense_mag_publish(int support) {
    atomic_store(&sense_mag_status_support, support);
    atomic_store(&sense_mag_status_stage, sense_mag.state.stage);
    atomic_store(&sense_mag_status_result, sense_mag.state.result);
    atomic_store(&sense_mag_status_charge, sense_mag.charge_support);
    atomic_store(&sense_mag_status_ns, host_monotonic_ns());
}
static void sense_mag_cancel(int support) {
    if (sense_mag.state.stage != SENSE_MAG_IDLE) sense_mag_reset(&sense_mag.state, SENSE_MAG_RESULT_CANCELLED);
    sense_mag.have_grip = 0; sense_mag.pinned = 0; sense_mag_publish(support);
}
/* Weapon qualification shared by rig and engine checks. Returns the weapon
 * object body and the tag's rounds_loaded_maximum, or 0. */
static uint32_t sense_mag_weapon(uint32_t weapon, unsigned *loaded_max) {
    uint32_t body = sense_object(weapon, 0x340, 2), tags = G32(0x0087BC14u);
    uint32_t data = body ? sense_tag(G32(body), 0x4F8) : 0;
    if (!data) return 0;
    uint32_t name = G32(tags + (G32(body) & 65535u)*32u + 0x10); char path[80];
    if (!sense_range(name, sizeof path)) return 0;
    memcpy(path, GPTR(name), sizeof path); path[sizeof path - 1] = 0;
    if (strcmp(path, "weapons\\assault rifle\\assault rifle") && strcmp(path, "weapons\\pistol\\pistol") &&
        strcmp(path, "weapons\\sniper rifle\\sniper rifle")) return 0;
    int32_t count = (int32_t)G32(data + 0x4F0); uint32_t block = G32(data + 0x4F4);
    if (count < 1 || count > 2 || !sense_range(block, 0x70u*(unsigned)count)) return 0;
    *loaded_max = G16(block + 0xA);
    return *loaded_max ? body : 0;
}
/* 'frame magazine' in the graph (unique, under the gun, not an ancestor of
 * the muzzle marker bone) and in the first-person model's node names. */
static int sense_mag_node(uint32_t weapon, uint32_t bones, unsigned count, const int16_t *parents, int gun, int marker_bone) {
    int node = sense_arm_named(bones, count, "frame magazine");
    if (node < 0 || gun < 0 || !sense_descendant((unsigned)node, gun, parents, count) || node == gun) return -1;
    if (marker_bone >= 0 && sense_descendant((unsigned)marker_bone, node, parents, count)) return -1;
    uint32_t w = sense_object(weapon, 0x340, 2), data = w ? sense_tag(G32(w), 0x46C) : 0;
    uint32_t model = data ? sense_tag(G32(data + 0x468), 0xC0) : 0;
    uint32_t nodes = model ? G32(model + 0xBC) : 0, node_count = model ? G32(model + 0xB8) : 0;
    if (!node_count || node_count > 64 || !sense_range(nodes, node_count*0x9Cu)) return -1;
    for (unsigned i = 0; i < node_count; ++i) {
        char name[33] = {0}; memcpy(name, GPTR(nodes + i*0x9Cu), 32);
        if (!strcmp(name, "frame magazine")) return node;
    }
    return -1;
}
/* Primary, unreflected rig only; after the tracked gun delta, before the
 * muzzle is measured. raw_left is the tracked left grip (never the
 * support-constrained pose). Moves or hides only the magazine subtree. */
/* Charging handle/slide for the charging phase. Node names are those of the
 * stock first-person models (HCEEK model catalog: assault rifle and sniper
 * rifle 'frame ophandle', pistol 'frame slide'); each must be unique in the
 * graph, under the gun, disjoint from the magazine subtree, not carry the
 * muzzle marker, and be a node of the first-person model. Travel is the
 * physical pull distance in metres. Others: no charging phase. */
static int sense_charge_node(uint32_t weapon, uint32_t bones, unsigned count, const int16_t *parents,
                             int gun, int marker_bone, int magazine, float *travel) {
    char path[80]; const char *name; float t;
    if (!sense_weapon_path(weapon, path)) return -1;
    if (!strcmp(path, "weapons\\assault rifle\\assault rifle")) { name = "frame ophandle"; t = .05f; }
    else if (!strcmp(path, "weapons\\sniper rifle\\sniper rifle")) { name = "frame ophandle"; t = .06f; }
    else if (!strcmp(path, "weapons\\pistol\\pistol")) { name = "frame slide"; t = .03f; }
    else return -1;
    int node = sense_arm_named(bones, count, name);
    if (node < 0 || node == gun || !sense_descendant((unsigned)node, gun, parents, count)) return -1;
    if (magazine >= 0 && (sense_descendant((unsigned)node, magazine, parents, count) ||
                          sense_descendant((unsigned)magazine, node, parents, count))) return -1;
    if (marker_bone >= 0 && sense_descendant((unsigned)marker_bone, node, parents, count)) return -1;
    uint32_t w = sense_object(weapon, 0x340, 2), data = w ? sense_tag(G32(w), 0x46C) : 0;
    uint32_t model = data ? sense_tag(G32(data + 0x468), 0xC0) : 0;
    uint32_t nodes = model ? G32(model + 0xBC) : 0, node_count = model ? G32(model + 0xB8) : 0;
    if (!node_count || node_count > 64 || !sense_range(nodes, node_count*0x9Cu)) return -1;
    for (unsigned i = 0; i < node_count; ++i) {
        char model_name[33] = {0}; memcpy(model_name, GPTR(nodes + i*0x9Cu), 32);
        if (!strcmp(model_name, name)) { *travel = t; return node; }
    }
    return -1;
}
/* Original animation priority (melee) or a frame without the tracked
 * model: any physical stage is abandoned, the magazine is seated. */
static void sense_physical_interrupt(void) {
    if (sense_mag.state.stage != SENSE_MAG_IDLE && sense_mag.state.stage != SENSE_MAG_COMMITTED)
        sense_mag_cancel(HALO_SENSE_MAGAZINE_BLOCKED);
}
/* One physical presentation per accepted outer epoch. The stage machine
 * steps, and its draw (seated / in hand / hidden / pinned) and target
 * transform are frozen, only at the first primary build of the epoch; every
 * later build or eye of that epoch re-applies exactly that presentation
 * without reading L2/R2 again. Live DirectInput delivery still revalidates a
 * pending commit at once; the next epoch reflects any cancellation. */
enum { SENSE_MAG_EPOCH_SEATED = 0, SENSE_MAG_EPOCH_IN_HAND = 1, SENSE_MAG_EPOCH_HIDDEN = 2, SENSE_MAG_EPOCH_PINNED = 3 };
/* Handle presentation: none, offset (world translation, manual charge) or
 * pinned (its idle seat on the gun during the original reload). */
enum { SENSE_HANDLE_NONE = 0, SENSE_HANDLE_OFFSET = 1, SENSE_HANDLE_PINNED = 2 };
static struct {
    uint64_t epoch; int draw, handle_draw, handle_node; float handle; /* metres */
    HaloSenseTransform target, handle_target; float back[3];
} sense_mag_epoch;
static void sense_mag_apply_handle(uint32_t output, unsigned count, const int16_t *parents) {
    int node = sense_mag_epoch.handle_node; HaloSenseTransform seated;
    if (node < 0 || (unsigned)node >= count || !sense_node(output + (unsigned)node*52u, &seated)) return;
    HaloSenseTransform delta;
    if (sense_mag_epoch.handle_draw == SENSE_HANDLE_OFFSET) {
        HaloSenseTransform moved = seated;
        for (unsigned k = 0; k < 3; ++k) moved.p[k] += sense_mag_epoch.back[k]*sense_mag_epoch.handle/3.048f;
        delta = sense_delta(&seated, &moved, seated.p, moved.p);
    } else if (sense_mag_epoch.handle_draw == SENSE_HANDLE_PINNED) {
        delta = sense_delta(&seated, &sense_mag_epoch.handle_target, seated.p, sense_mag_epoch.handle_target.p);
    } else return;
    for (unsigned i = 0; i < count; ++i) if (sense_descendant(i, node, parents, count)) sense_apply_node(output + i*52u, &delta);
}
static void sense_mag_apply(uint32_t output, unsigned count, const int16_t *parents, int node,
                            const HaloSenseTransform *seated, int draw, const HaloSenseTransform *target) {
    if (draw == SENSE_MAG_EPOCH_IN_HAND || draw == SENSE_MAG_EPOCH_PINNED) {
        HaloSenseTransform delta = sense_delta(seated, target, seated->p, target->p);
        for (unsigned i = 0; i < count; ++i) if (sense_descendant(i, node, parents, count)) sense_apply_node(output + i*52u, &delta);
    } else if (draw == SENSE_MAG_EPOCH_HIDDEN) {
        for (unsigned i = 0; i < count; ++i) if (sense_descendant(i, node, parents, count)) S32(output + i*52u, 0);
    }
    sense_mag_apply_handle(output, count, parents);
}
static void sense_magazine_frame(uint32_t output, uint32_t bones, unsigned count, const int16_t *parents,
                                 int gun, int marker_bone, uint32_t unit_body, uint32_t datum, uint32_t weapon,
                                 const HaloSenseTransform *raw_left, int left_valid, int right_valid) {
    if (sense_mag_feature && !right_valid) { sense_physical_interrupt(); sense_mag_publish(HALO_SENSE_MAGAZINE_BLOCKED); return; }
    if (!sense_mag_feature) {
        if (sense_mag.state.stage != SENSE_MAG_IDLE || sense_mag.have_grip) sense_mag_cancel(HALO_SENSE_MAGAZINE_OFF);
        else sense_mag_publish(HALO_SENSE_MAGAZINE_OFF);
        return;
    }
    HaloSenseDualInput input = {0};
    unsigned loaded_max = 0;
    uint32_t body = sense_mag_weapon(weapon, &loaded_max);
    int node = body ? sense_mag_node(weapon, bones, count, parents, gun, marker_bone) : -1;
    int have_input = halo_sense_dual_input(&input);
    if (sense_mag.unit != datum || sense_mag.weapon != weapon || sense_mag.session != input.session ||
        sense_mag.generation != input.generation) {
        sense_mag_cancel(HALO_SENSE_MAGAZINE_OFF); sense_mag.have_seat = 0;
        sense_mag.unit = datum; sense_mag.weapon = weapon; sense_mag.session = input.session; sense_mag.generation = input.generation;
    }
    if (!body || node < 0) { sense_mag_cancel(HALO_SENSE_MAGAZINE_UNSUPPORTED); return; }
    unsigned state = G16(body + 0x2B0), reserve = G16(body + 0x2B6), loaded = G16(body + 0x2B8);
    /* support_blocked: L1, Create, Square, Triangle, Home (or app ownership)
     * belong to an original action; never admit a grab over them. */
    int blocked = !have_input || !left_valid || input.support_blocked || halo_sense_dual_requested() || halo_sense_app_input_blocked() ||
        sense_pick_left_owned || sense_pickup_holds_left_intent() ||
        sense_action_for_unit(unit_body, datum, weapon, NULL) != HALO_SENSE_ACTION_NONE;
    int support = blocked ? HALO_SENSE_MAGAZINE_BLOCKED : state != 0 ? HALO_SENSE_MAGAZINE_ENGINE_BUSY :
        loaded >= loaded_max ? HALO_SENSE_MAGAZINE_FULL : HALO_SENSE_MAGAZINE_READY;
    HaloSenseTransform seated;
    if (!sense_node(output + (unsigned)node*52u, &seated)) { sense_mag_cancel(HALO_SENSE_MAGAZINE_UNSUPPORTED); return; }
    uint64_t epoch = sense_accepted_frame_epoch();
    if (epoch && sense_mag_epoch.epoch == epoch) {
        sense_mag_apply(output, count, parents, node, &seated, sense_mag_epoch.draw, &sense_mag_epoch.target);
        return; /* a later build/eye of the same epoch: same presentation, no step */
    }
    float travel = 0;
    int handle = sense_charge_node(weapon, bones, count, parents, gun, marker_bone, node, &travel);
    HaloSenseTransform handle_pose;
    int have_handle = handle >= 0 && sense_node(output + (unsigned)handle*52u, &handle_pose);
    sense_mag.charge_support = have_handle;
    SenseMagInput in = {0};
    in.charge = sense_charge_feature && have_handle; in.travel = travel > 0 ? travel : 1;
    if (have_handle && left_valid) {
        float d[3]; for (unsigned k = 0; k < 3; ++k) d[k] = (raw_left->p[k]-handle_pose.p[k])*3.048f;
        in.to_handle = sqrtf(sense_dot(d, d));
    } else in.to_handle = 1e9f;
    /* Weapon-relative pull: the hand expressed in the rendered gun node's own
     * frame, compared with where it closed on the handle in that frame. A
     * rigid move/turn of body, weapon and hand together leaves it at zero. */
    HaloSenseTransform gun_pose;
    int have_gun = sense_node(output + (unsigned)gun*52u, &gun_pose);
    if (left_valid && have_gun && (sense_mag.state.stage == SENSE_MAG_CHARGE_GRIPPED || sense_mag.state.stage == SENSE_MAG_CHARGE_PULLED)) {
        float rel[3], local[3]; for (unsigned k = 0; k < 3; ++k) rel[k] = raw_left->p[k]-gun_pose.p[k];
        sense_unrotate(&gun_pose, rel, local);
        in.pull = (sense_mag.charge_start[0]-local[0])*3.048f; /* back = the gun's -forward */
    } else if (sense_mag.state.stage == SENSE_MAG_CHARGE_GRIPPED || sense_mag.state.stage == SENSE_MAG_CHARGE_PULLED) in.pull = NAN; /* fail closed */
    in.eligible = support == HALO_SENSE_MAGAZINE_READY && reserve > 0; /* nothing to load without reserve */
    in.reserve = reserve > 0; in.engine_reloading = state == 1;
    in.firing = have_input && input.trigger[1] > .5f; in.grip = have_input ? input.trigger[0] : 0;
    in.now = host_monotonic_ns();
    float offset[3]; for (unsigned k = 0; k < 3; ++k) offset[k] = (raw_left->p[k]-seated.p[k])*3.048f;
    in.to_well = left_valid ? sqrtf(sense_dot(offset, offset)) : INFINITY;
    HaloSenseTransform held = seated;
    if (sense_mag.have_grip && left_valid) {
        for (unsigned k = 0; k < 3; ++k) sense_rotate(raw_left, sense_mag.grip.c[k], held.c[k]);
        sense_point(raw_left, sense_mag.grip.p, held.p);
        float in_hand[3]; for (unsigned k = 0; k < 3; ++k) in_hand[k] = (held.p[k]-seated.p[k])*3.048f;
        /* While held, proximity is the held magazine to its seat. */
        in.to_well = sqrtf(sense_dot(in_hand, in_hand));
        in.align = fminf(sense_dot(held.c[0], seated.c[0]), sense_dot(held.c[2], seated.c[2]));
    } else in.align = 1;
    if (!isfinite(in.to_well)) in.to_well = 1e9f;
    if (have_gun && state == 0 && sense_mag.state.stage == SENSE_MAG_IDLE) {
        /* Latest original idle seat, relative to the gun (no smoothing). */
        float rel[3]; for (unsigned k = 0; k < 3; ++k) rel[k] = seated.p[k]-gun_pose.p[k];
        for (unsigned k = 0; k < 3; ++k) sense_unrotate(&gun_pose, seated.c[k], sense_mag.seat.c[k]);
        sense_unrotate(&gun_pose, rel, sense_mag.seat.p); sense_mag.have_seat = 1;
        if (have_handle) {
            for (unsigned k = 0; k < 3; ++k) rel[k] = handle_pose.p[k]-gun_pose.p[k];
            for (unsigned k = 0; k < 3; ++k) sense_unrotate(&gun_pose, handle_pose.c[k], sense_mag.handle_seat.c[k]);
            sense_unrotate(&gun_pose, rel, sense_mag.handle_seat.p); sense_mag.have_handle_seat = 1;
        } else sense_mag.have_handle_seat = 0;
    }
    int was_committed = sense_mag.state.stage == SENSE_MAG_COMMITTED;
    SenseMagOutput out = sense_mag_step(&sense_mag.state, &in);
    if (was_committed && sense_mag.state.result == SENSE_MAG_RESULT_CONFIRMED) sense_mag.pinned = 1;
    if (state == 0) sense_mag.pinned = 0;
    if (out.capture_handle) {
        /* Grip point in the gun's own frame; the pull is measured from here. */
        if (left_valid && have_gun) {
            float rel[3]; for (unsigned k = 0; k < 3; ++k) rel[k] = raw_left->p[k]-gun_pose.p[k];
            sense_unrotate(&gun_pose, rel, sense_mag.charge_start);
        } else sense_mag_reset(&sense_mag.state, SENSE_MAG_RESULT_CANCELLED);
    }
    if (out.capture_grip && left_valid) {
        /* Keep the magazine exactly where the hand closed on it. */
        float rel[3]; for (unsigned k = 0; k < 3; ++k) rel[k] = seated.p[k]-raw_left->p[k];
        for (unsigned k = 0; k < 3; ++k) sense_unrotate(raw_left, seated.c[k], sense_mag.grip.c[k]);
        sense_unrotate(raw_left, rel, sense_mag.grip.p); sense_mag.have_grip = 1; held = seated;
    }
    if (out.commit) {
        uint64_t sequence = ++sense_physical_sequence;
        int accepted = halo_sense_physical_enqueue(input.session, input.generation, sequence, HALO_SENSE_GESTURE_PHYSICAL_RELOAD, datum, weapon);
        sense_mag_committed(&sense_mag.state, accepted, in.now);
        if (!accepted) out.draw = SENSE_MAG_DRAW_IN_HAND;
        if (HOST_ENV("HALO_SENSE_PHYSICAL_TRACE"))
            host_log("[sense-magazine] insert weapon=%08x loaded=%u/%u reserve=%u commit=%d", weapon, loaded, loaded_max, reserve, accepted);
    }
    if (sense_mag.state.stage == SENSE_MAG_IDLE) sense_mag.have_grip = 0;
    /* Draw out of the gun only when this epoch already owns the left hand. */
    if (!sense_mag_left_owned || !sense_mag_owned_stage(sense_mag.state.stage)) out.draw = SENSE_MAG_DRAW_SEATED;
    int draw = SENSE_MAG_EPOCH_SEATED; HaloSenseTransform target = seated;
    if (out.draw == SENSE_MAG_DRAW_IN_HAND && sense_mag.have_grip && left_valid) { draw = SENSE_MAG_EPOCH_IN_HAND; target = held; }
    else if (out.draw == SENSE_MAG_DRAW_HIDDEN) draw = SENSE_MAG_EPOCH_HIDDEN;
    else if (sense_mag.pinned && sense_mag.have_seat && have_gun) {
        /* The player inserted this magazine; while the original reload
         * runs its timer and ammo transfer, keep the subtree seated instead
         * of replaying the animation's removal. Gun/wrist/marker untouched. */
        for (unsigned k = 0; k < 3; ++k) sense_rotate(&gun_pose, sense_mag.seat.c[k], target.c[k]);
        sense_point(&gun_pose, sense_mag.seat.p, target.p); draw = SENSE_MAG_EPOCH_PINNED;
    }
    /* Charging handle: drawn at the manual pull while this epoch owns the
     * left hand; pinned to its idle seat while the original reload that a
     * physical insertion started runs (no second, native charge motion). */
    sense_mag_epoch.handle_draw = SENSE_HANDLE_NONE; sense_mag_epoch.handle_node = have_handle ? handle : -1;
    sense_mag_epoch.handle = 0;
    if (have_handle && sense_mag_left_owned && out.handle > 0 &&
        (sense_mag.state.stage == SENSE_MAG_CHARGE_GRIPPED || sense_mag.state.stage == SENSE_MAG_CHARGE_PULLED)) {
        sense_mag_epoch.handle_draw = SENSE_HANDLE_OFFSET; sense_mag_epoch.handle = out.handle;
        for (unsigned k = 0; k < 3; ++k) sense_mag_epoch.back[k] = -gun_pose.c[0][k]; /* this epoch's gun */
    } else if (have_handle && draw == SENSE_MAG_EPOCH_PINNED && sense_mag.have_handle_seat) {
        for (unsigned k = 0; k < 3; ++k) sense_rotate(&gun_pose, sense_mag.handle_seat.c[k], sense_mag_epoch.handle_target.c[k]);
        sense_point(&gun_pose, sense_mag.handle_seat.p, sense_mag_epoch.handle_target.p);
        sense_mag_epoch.handle_draw = SENSE_HANDLE_PINNED;
    }
    sense_mag_drawn_removed = draw == SENSE_MAG_EPOCH_IN_HAND || draw == SENSE_MAG_EPOCH_HIDDEN ||
        sense_mag_epoch.handle_draw == SENSE_HANDLE_OFFSET; /* not ready to fire while charging */
    sense_mag_drawn_epoch = epoch;
    sense_mag_epoch.epoch = epoch; sense_mag_epoch.draw = draw; sense_mag_epoch.target = target;
    sense_mag_apply(output, count, parents, node, &seated, draw, &target);
    sense_mag_publish(support);
}

/* ---- Physical pickup (default off) ------------------------------------
 * Target is only the engine's own pickup selection for the local player:
 * PC1.10 player row (0x0087A480 table, stride 0x200) action_result at +0x28
 * and salted action object at +0x24. Original 00478E00 sets them (result 11
 * special case, distance tie-break on object +0x5C); original 004790D0
 * consumes results 6 (swap) and 7 (add to inventory) from +0x24 when the
 * exchange control is set. Campaign only (game mode word 0x00719720 == 0,
 * the same gate build118 uses for tracked weapons).
 *
 * Render-only flight: original 0050EE20 draws a world object by calling
 * 004D6FC0 (returning to 0050F049, or 0050F0BD for its shadow) with
 * EAX = model tag (object definition +0x34), ECX = the object's own 52-byte
 * node palette (object + int16 [object+0x1F2]) and [esp+0x24] = the salted
 * object datum; 004D6FC0 copies ECX to EDI and multiplies each palette node
 * by the model node's bind matrix (+0x68 of the 0x9C rows at model +0xBC,
 * count +0xB8) at 004D7071..004D70A7. For the selected weapon only, the
 * wrapper below passes a private copy of that palette, rigidly moved to the
 * flight/held pose, and a matching center; it restores the caller's stack
 * slot afterwards. The world object, its physics, inventory and ammo are
 * never written; the original exchange performs the actual pickup. */
#include "sense_pickup.h"
static _Atomic int sense_physical_pickup_enabled;
void halo_sense_set_physical_pickup(int enabled) {
    atomic_store(&sense_physical_pickup_enabled, !!enabled);
    if (!enabled) halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_PICKUP);
}
int halo_sense_physical_pickup(void) { return atomic_load(&sense_physical_pickup_enabled); }
static struct {
    SensePickupState state;
    uint32_t unit, player; uint64_t session, generation;
    float room_lock[3], pull_dir[3], yaw_lock;
    HaloSenseTransform ground; /* node 0 where the weapon lay when the pull began */
    uint32_t late_object; uint64_t late_ns; /* a cancelled X the original may still have consumed */
    float branch[4]; int have_branch;       /* this flight's target quaternion sign */
    HaloSenseTransform last_pose; int have_last;
    uint64_t update_epoch, update_ns;       /* last epoch that progressed the transaction */
    uint32_t weapon;                        /* the right-hand weapon of that epoch */
} sense_pick = {.state = {.object = UINT32_MAX}, .late_object = UINT32_MAX};
/* One render pose per outer frame, shared by every view/eye and shadow. */
/* The render pose carries the exact accepted frame it was built from. */
static struct {
    int active; uint32_t object; HaloSenseTransform pose;
    uint64_t epoch, session, generation; uint32_t unit, weapon;
} sense_pick_render;
/* Outer epoch in which a relevant pickup draw had to fall back to the
 * original palette: every later draw (eye, view, shadow) of that epoch also
 * falls back, even if the accepted pose is admitted afterwards. */
static uint64_t sense_pick_render_fallback_epoch;
static int sense_pick_render_nested;
static uint32_t sense_pick_scratch;
static pthread_mutex_t sense_pick_lock = PTHREAD_MUTEX_INITIALIZER;
static HaloSensePickupStatus sense_pick_status; static uint64_t sense_pick_status_ns;
/* Local player row and datum, with salt; 0 when absent. */
static uint32_t sense_pickup_row(uint32_t *player) {
    uint32_t globals = G32(0x0087A478u), array = G32(0x0087A480u);
    if (!sense_range(globals, 0x12) || !sense_range(array, 0x38)) return 0;
    *player = G32(globals + 4); uint32_t rows = G32(array + 0x34);
    unsigned count = G16(array + 0x20), i = *player & 65535u;
    if (*player == UINT32_MAX || !count || count > 16 || i >= count || G16(array + 0x22) != 0x200 ||
        !sense_range(rows, count*0x200u)) return 0;
    uint32_t row = rows + i*0x200u;
    return G16(row) == (*player >> 16) ? row : 0;
}
/* The engine-selected, unowned weapon object for this player row. */
static uint32_t sense_pickup_selected(uint32_t row, uint32_t *object) {
    int16_t result = (int16_t)G16(row + 0x28); *object = G32(row + 0x24);
    if (result != 6 && result != 7) return 0;
    uint32_t w = sense_object(*object, 0x340, 2);
    return w && G32(w + 0x11C) == UINT32_MAX ? w : 0;
}
static int sense_pickup_campaign(void) { return G16(0x00719720u) == 0; }
static int sense_pickup_delivery_check(uint32_t owner, uint32_t target) {
    if (!halo_sense_physical_pickup() || !sense_pickup_campaign()) return 0;
    uint32_t player, datum, object, row = sense_pickup_row(&player);
    if (!row || player != owner || !sense_local_body(&datum) || G32(row + 0x34) != datum) return 0;
    return sense_pickup_selected(row, &object) && object == target;
}
int host_sense_pickup_status(HaloSensePickupStatus *out) {
    if (!out) return 0;
    pthread_mutex_lock(&sense_pick_lock); uint64_t now = host_monotonic_ns();
    int fresh = halo_sense_active() && sense_pick_status_ns && now >= sense_pick_status_ns && now - sense_pick_status_ns <= UINT64_C(250000000);
    if (fresh) *out = sense_pick_status; else memset(out, 0, sizeof *out);
    out->enabled = halo_sense_physical_pickup();
    pthread_mutex_unlock(&sense_pick_lock); return fresh;
}
static void sense_pickup_publish(int reason, const float target[3]) {
    pthread_mutex_lock(&sense_pick_lock);
    memset(&sense_pick_status, 0, sizeof sense_pick_status);
    sense_pick_status.reason = reason; sense_pick_status.stage = sense_pick.state.stage;
    sense_pick_status.result = sense_pick.state.result;
    if (target && sense_pick.state.stage != SENSE_PICKUP_IDLE) { memcpy(sense_pick_status.target, target, 12); sense_pick_status.target_valid = 1; }
    sense_pick_status_ns = host_monotonic_ns();
    pthread_mutex_unlock(&sense_pick_lock);
}
/* Locked, flying or caught: the left hand's L2 belongs to the pickup. */
static int sense_pickup_holds_left_intent(void) {
    return halo_sense_physical_pickup() && sense_pick.state.stage >= SENSE_PICKUP_LOCKED;
}
static int sense_pickup_left_stage(void) {
    return halo_sense_physical_pickup() && (sense_pick.state.stage == SENSE_PICKUP_FLYING || sense_pick.state.stage == SENSE_PICKUP_COMMITTED);
}
/* The left hand for weapon-attached and pickup visuals: the raw left of
 * this epoch's accepted primary pair (read only; never admits or seeds). */
static int sense_physical_left_world(uint32_t datum, uint32_t weapon, HaloSenseTransform *hand) {
    SenseAcceptedWeaponFrame frame;
    if (!sense_accepted_weapon_frame(datum, weapon, &frame)) return 0;
    *hand = frame.left; return 1;
}
static float sense_pick_yaw(void) { return atan2f(sense_camera.c[0][1], sense_camera.c[0][0]); }
/* Rigid interpolation for the flight. Orientation: unit quaternions with
 * the shorter-arc sign, spherical interpolation (stable through a 180
 * degree relative turn, where the quaternions are 90 degrees apart);
 * exact endpoints. Position: linear plus a low arc in Halo +Z. Bases must
 * be orthonormal and right-handed; anything else fails closed (ground). */
static int sense_pick_quat(const HaloSenseTransform *m, float q[4]) {
    const float (*c)[3] = m->c; float d[3];
    sense_cross(c[0], c[1], d);
    for (unsigned i = 0; i < 3; ++i) {
        float l = sense_dot(c[i], c[i]); if (!isfinite(l) || fabsf(l-1) > 1e-3f) return 0;
    }
    if (fabsf(sense_dot(c[0], c[1])) > 1e-3f || fabsf(sense_dot(c[0], c[2])) > 1e-3f ||
        fabsf(sense_dot(c[1], c[2])) > 1e-3f || sense_dot(d, c[2]) < .999f) return 0; /* reflected or skewed */
    /* Matrix element r[row][col] = c[col][row]. Shepperd's largest-term form. */
    float r00 = c[0][0], r11 = c[1][1], r22 = c[2][2], t = r00+r11+r22;
    if (t > 0) { float s = sqrtf(t+1)*2; q[3] = .25f*s; q[0] = (c[1][2]-c[2][1])/s; q[1] = (c[2][0]-c[0][2])/s; q[2] = (c[0][1]-c[1][0])/s; }
    else if (r00 > r11 && r00 > r22) { float s = sqrtf(1+r00-r11-r22)*2; q[3] = (c[1][2]-c[2][1])/s; q[0] = .25f*s; q[1] = (c[1][0]+c[0][1])/s; q[2] = (c[2][0]+c[0][2])/s; }
    else if (r11 > r22) { float s = sqrtf(1+r11-r00-r22)*2; q[3] = (c[2][0]-c[0][2])/s; q[0] = (c[1][0]+c[0][1])/s; q[1] = .25f*s; q[2] = (c[2][1]+c[1][2])/s; }
    else { float s = sqrtf(1+r22-r00-r11)*2; q[3] = (c[0][1]-c[1][0])/s; q[0] = (c[2][0]+c[0][2])/s; q[1] = (c[2][1]+c[1][2])/s; q[2] = .25f*s; }
    float n = sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (!isfinite(n) || n < .5f) return 0;
    for (unsigned k = 0; k < 4; ++k) q[k] /= n; return 1;
}
static void sense_pick_from_quat(const float q[4], HaloSenseTransform *m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float r[3][3] = {{1-2*(y*y+z*z), 2*(x*y-w*z), 2*(x*z+w*y)},
                     {2*(x*y+w*z), 1-2*(x*x+z*z), 2*(y*z-w*x)},
                     {2*(x*z-w*y), 2*(y*z+w*x), 1-2*(x*x+y*y)}};
    for (unsigned col = 0; col < 3; ++col) for (unsigned row = 0; row < 3; ++row) m->c[col][row] = r[row][col];
}
/* Flight-stable spherical interpolation. The target quaternion's sign is
 * chosen once at the start of the flight (shorter arc from the source) and
 * afterwards kept continuous with the previous frame's target (branch), so
 * a hand turning through 180 degrees relative to the source never makes the
 * path jump to the opposite arc. Returns 0 on unqualified bases (draw on the
 * ground), 2 when the path is ambiguous (target exactly a full turn from
 * the source along the tracked branch: hold the previous orientation). */
static int sense_pick_blend(const HaloSenseTransform *a, const HaloSenseTransform *b, float s, float arc,
                            float branch[4], int *have_branch, HaloSenseTransform *out) {
    float qa[4], qb[4];
    if (!sense_pick_quat(a, qa) || !sense_pick_quat(b, qb) || !isfinite(s) || !isfinite(arc)) return 0;
    float reference[4];
    if (*have_branch) memcpy(reference, branch, 16); else memcpy(reference, qa, 16);
    if (qb[0]*reference[0]+qb[1]*reference[1]+qb[2]*reference[2]+qb[3]*reference[3] < 0)
        for (unsigned k = 0; k < 4; ++k) qb[k] = -qb[k];
    memcpy(branch, qb, 16); *have_branch = 1;
    if (s <= 0) { *out = *a; return 1; }
    if (s >= 1) { *out = *b; return 1; }
    float d = qa[0]*qb[0]+qa[1]*qb[1]+qa[2]*qb[2]+qa[3]*qb[3];
    if (d < -.9995f) return 2;
    float q[4], wa, wb;
    if (d > .9995f) { wa = 1-s; wb = s; }
    else { float th = acosf(d), sn = sinf(th); wa = sinf((1-s)*th)/sn; wb = sinf(s*th)/sn; }
    float n = 0; for (unsigned k = 0; k < 4; ++k) { q[k] = wa*qa[k]+wb*qb[k]; n += q[k]*q[k]; }
    n = sqrtf(n); if (!isfinite(n) || n < .5f) return 0;
    for (unsigned k = 0; k < 4; ++k) q[k] /= n;
    sense_pick_from_quat(q, out);
    for (unsigned k = 0; k < 3; ++k) out->p[k] = a->p[k]+(b->p[k]-a->p[k])*s;
    out->p[2] += arc*sinf(3.14159265f*s);
    return 1;
}
static void sense_pickup_cancel_now(int result) {
    if (sense_pick.state.stage == SENSE_PICKUP_COMMITTED) {
        halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_PICKUP);
        sense_pick.late_object = sense_pick.state.object; sense_pick.late_ns = host_monotonic_ns();
    }
    if (sense_pick.state.stage != SENSE_PICKUP_IDLE) sense_pickup_reset(&sense_pick.state, result);
    sense_pick_render.active = 0; sense_pick.have_branch = sense_pick.have_last = 0;
}
/* Outer render boundary, before any admission: never progresses a pickup.
 * A frame that cannot be the same transaction cancels it at once (feature
 * off, no local on-foot unit, menu/app ownership, session or input
 * generation change); otherwise it lapses only if no epoch has progressed
 * it for 150 ms. A getter that is false before this epoch's admission is
 * not a cancellation. */
static void sense_pickup_outer_expire(uint32_t unit, uint32_t datum) {
    if (sense_pick.state.stage == SENSE_PICKUP_IDLE && !sense_pick_render.active) return;
    HaloSenseDualInput input = {0}; int have_input = halo_sense_dual_input(&input);
    uint64_t now = host_monotonic_ns();
    int lost = !halo_sense_physical_pickup() || !unit || datum != sense_pick.unit || G8(0x00718FC9u) ||
        halo_sense_app_input_blocked() || !have_input || input.session != sense_pick.session || input.generation != sense_pick.generation;
    int stale = !sense_pick.update_ns || now < sense_pick.update_ns || now - sense_pick.update_ns > UINT64_C(150000000);
    if (lost || stale) { sense_pickup_cancel_now(SENSE_PICKUP_RESULT_CANCELLED); sense_pickup_publish(lost ? HALO_SENSE_PICKUP_BLOCKED : HALO_SENSE_PICKUP_NO_HAND, NULL); }
}
static void sense_pickup_frame(uint32_t unit, uint32_t datum, uint32_t weapon);
/* Once per outer epoch, from the primary first-person build after this
 * epoch's accepted frame exists (before any world model draw). */
static void sense_pickup_epoch_update(uint32_t unit, uint32_t datum, uint32_t weapon) {
    uint64_t epoch = sense_accepted_frame_epoch();
    if (!epoch || sense_pick.update_epoch == epoch) return;
    SenseAcceptedWeaponFrame frame;
    if (!sense_accepted_weapon_frame(datum, weapon, &frame)) return; /* no admission yet: no progress, no cancel */
    sense_pick.update_epoch = epoch;
    sense_pickup_frame(unit, datum, weapon);
}
static void sense_pickup_frame(uint32_t unit, uint32_t datum, uint32_t weapon) {
    sense_pick_render.active = 0;
    if (!halo_sense_physical_pickup()) {
        sense_pickup_cancel_now(SENSE_PICKUP_RESULT_CANCELLED); sense_pick.late_object = UINT32_MAX;
        sense_pickup_publish(HALO_SENSE_PICKUP_OFF, NULL); return;
    }
    HaloSenseDualInput input = {0};
    int have_input = halo_sense_dual_input(&input);
    uint32_t player = UINT32_MAX, object = UINT32_MAX, row = sense_pickup_row(&player);
    if (sense_pick.unit != datum || sense_pick.player != player || sense_pick.session != input.session ||
        sense_pick.generation != input.generation) {
        sense_pickup_cancel_now(SENSE_PICKUP_RESULT_CANCELLED);
        sense_pick.unit = datum; sense_pick.player = player; sense_pick.session = input.session; sense_pick.generation = input.generation;
    }
    uint32_t target = row && sense_pickup_campaign() ? sense_pickup_selected(row, &object) : 0;
    HaloSenseTransform hand; float eye[3];
    int tracked = have_input && sense_physical_left_world(datum, weapon, &hand);
    sense_pick.update_ns = host_monotonic_ns();
    if (sense_pick.weapon != weapon && sense_pick.state.stage != SENSE_PICKUP_IDLE) {
        sense_pickup_cancel_now(SENSE_PICKUP_RESULT_CANCELLED); /* right-hand weapon changed */
    }
    sense_pick.weapon = weapon;
    memcpy(eye, GPTR(unit + 0x5C), 12); for (unsigned k = 0; k < 3; ++k) eye[k] += sense_camera_from_unit[k];
    int reason = !sense_pickup_campaign() ? HALO_SENSE_PICKUP_NOT_CAMPAIGN : !tracked ? HALO_SENSE_PICKUP_NO_HAND :
        input.support_blocked || halo_sense_dual_requested() || G16(unit + 0x2F0) != UINT16_MAX ? HALO_SENSE_PICKUP_BLOCKED :
        !target ? HALO_SENSE_PICKUP_NONE_SELECTED : HALO_SENSE_PICKUP_READY;
    SensePickupInput in = {0};
    in.eligible = reason == HALO_SENSE_PICKUP_READY; in.object = object; in.now = host_monotonic_ns();
    in.left_busy = sense_mag_left_owned || (sense_mag_feature && sense_mag.state.stage != SENSE_MAG_IDLE);
    in.grip = have_input ? input.trigger[0] : 0;
    float to[3] = {0, 0, 0}, relative[3] = {0, 0, 0};
    if (tracked && target) {
        float p[3]; memcpy(p, GPTR(target + 0x5C), 12);
        for (unsigned k = 0; k < 3; ++k) { to[k] = (p[k]-hand.p[k])*3.048f; relative[k] = (p[k]-eye[k])*3.048f; }
        in.distance = sqrtf(sense_dot(to, to));
        float dir[3]; memcpy(dir, to, 12); in.aim = sense_normalize(dir) ? sense_dot(dir, hand.c[0]) : -1;
    } else { in.distance = 1e9f; in.aim = -1; }
    /* Room-frame left grip: head position added back, so moving only the
     * head never moves it. A virtual turn (game yaw change) invalidates. */
    HaloSenseFrame frame; float room[3] = {0, 0, 0};
    int room_valid = tracked && halo_sense_read(&frame) && frame.head_position_valid && frame.left.valid;
    if (room_valid) for (unsigned k = 0; k < 3; ++k) room[k] = frame.head_position[k]+frame.left.position[k];
    if (room_valid && sense_pick.state.stage == SENSE_PICKUP_LOCKED) {
        float dy = sense_pick_yaw()-sense_pick.yaw_lock;
        while (dy > 3.14159265f) dy -= 6.28318531f;
        while (dy < -3.14159265f) dy += 6.28318531f;
        if (fabsf(dy) > .0524f) room_valid = 0;
        float moved[3]; for (unsigned k = 0; k < 3; ++k) moved[k] = room[k]-sense_pick.room_lock[k];
        in.pull = sense_dot(moved, sense_pick.pull_dir);
    }
    in.pull_valid = room_valid;
    if (sense_pick.state.stage == SENSE_PICKUP_COMMITTED) {
        uint32_t w = sense_object(sense_pick.state.object, 0x340, 2);
        in.taken = w && G32(w + 0x11C) == datum;
        /* While X is pending, its own conflicts also cancel it here. */
        if (input.support_blocked) in.eligible = 0;
        in.pending = halo_sense_physical_pending(HALO_SENSE_GESTURE_PHYSICAL_PICKUP);
    }
    int was_locked = sense_pick.state.stage == SENSE_PICKUP_LOCKED;
    SensePickupOutput out = sense_pickup_step(&sense_pick.state, &in);
    if (out.lock) {
        /* Toward the body: horizontal direction from the hand to the head. */
        memcpy(sense_pick.room_lock, room, 12);
        float toward[3] = {frame.head_position[0]-room[0], frame.head_position[1]-room[1], 0};
        if (!sense_normalize(toward)) sense_pickup_reset(&sense_pick.state, SENSE_PICKUP_RESULT_CANCELLED);
        else { memcpy(sense_pick.pull_dir, toward, 12); sense_pick.yaw_lock = sense_pick_yaw(); }
    }
    if (sense_pick.state.stage != SENSE_PICKUP_FLYING && sense_pick.state.stage != SENSE_PICKUP_COMMITTED) {
        sense_pick.have_branch = 0; sense_pick.have_last = 0; /* branch state lives only within one flight */
    }
    if (was_locked && sense_pick.state.stage == SENSE_PICKUP_FLYING) {
        /* Flight starts from where the weapon lies: its own node 0. */
        uint32_t w = sense_object(object, 0x340, 2); int16_t offset = w ? (int16_t)G16(w + 0x1F2) : 0;
        if (!w || offset < 0x1F4 || !sense_node(w + (uint32_t)offset, &sense_pick.ground))
            sense_pickup_reset(&sense_pick.state, SENSE_PICKUP_RESULT_CANCELLED);
    }
    if (out.cancel_pending) {
        halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_PICKUP);
        sense_pick.late_object = out.cancelled_object; sense_pick.late_ns = in.now;
    }
    if (sense_pick.late_object != UINT32_MAX) {
        /* An X consumed just before the cancel is still the original's
         * pickup: report it truthfully, never draw a duplicate. */
        uint32_t w = sense_object(sense_pick.late_object, 0x340, 2);
        if (w && G32(w + 0x11C) == datum) { sense_pick.state.result = SENSE_PICKUP_RESULT_TAKEN; sense_pick.late_object = UINT32_MAX; }
        else if (in.now < sense_pick.late_ns || in.now - sense_pick.late_ns > SENSE_PICKUP_CONFIRM_NS) sense_pick.late_object = UINT32_MAX;
    }
    if (out.commit) {
        uint64_t sequence = ++sense_physical_sequence;
        int accepted = halo_sense_physical_enqueue(input.session, input.generation, sequence, HALO_SENSE_GESTURE_PHYSICAL_PICKUP, player, object);
        sense_pickup_committed(&sense_pick.state, accepted, in.now);
        if (!accepted) out.draw = SENSE_PICKUP_DRAW_GROUND;
        if (HOST_ENV("HALO_SENSE_PHYSICAL_TRACE"))
            host_log("[sense-pickup] caught player=%08x object=%08x distance=%.2f commit=%d", player, object, in.distance, accepted);
    }
    /* Draw only what is true now: flying, held after the catch while the
     * original exchange is pending, otherwise on the ground. */
    if (tracked && out.draw != SENSE_PICKUP_DRAW_GROUND && sense_pick.state.stage != SENSE_PICKUP_IDLE) {
        HaloSenseTransform pose = hand;
        if (out.draw == SENSE_PICKUP_DRAW_FLYING) {
            float t = out.progress < 0 ? 0 : out.progress > 1 ? 1 : out.progress, s2 = t*t*(3-2*t);
            float span[3]; for (unsigned k = 0; k < 3; ++k) span[k] = (hand.p[k]-sense_pick.ground.p[k])*3.048f;
            float arc = fminf(.25f, .3f*sqrtf(sense_dot(span, span)))/3.048f;
            int blended = sense_pick_blend(&sense_pick.ground, &hand, s2, arc, sense_pick.branch, &sense_pick.have_branch, &pose);
            if (blended == 2 && sense_pick.have_last) {
                memcpy(pose.c, sense_pick.last_pose.c, sizeof pose.c); /* ambiguous instant: keep orientation */
                for (unsigned k = 0; k < 3; ++k) pose.p[k] = sense_pick.ground.p[k]+(hand.p[k]-sense_pick.ground.p[k])*s2;
                pose.p[2] += arc*sinf(3.14159265f*s2);
            } else if (blended != 1) pose.p[0] = NAN; /* unqualified: ground */
        }
        SenseAcceptedWeaponFrame accepted;
        if (sense_finite3(pose.p) && sense_accepted_weapon_frame(datum, weapon, &accepted)) {
            sense_pick_render.pose = pose; sense_pick_render.object = sense_pick.state.object; sense_pick_render.active = 1;
            sense_pick_render.epoch = accepted.epoch; sense_pick_render.session = accepted.session;
            sense_pick_render.generation = accepted.generation; sense_pick_render.unit = accepted.unit;
            sense_pick_render.weapon = accepted.weapon;
            sense_pick.last_pose = pose; sense_pick.have_last = 1;
        }
    }
    float neutral[3] = {0, 0, 0};
    if (target) sense_unrotate(&sense_camera, relative, neutral);
    sense_pickup_publish(reason, target ? neutral : NULL);
}
/* Shared 004D6FC0 override contract (body lane's sense_world_model_override
 * .h/.inc, included once in overrides.c after every helper): validate the
 * original PC1.10 normal/shadow call for the selected weapon only and
 * prepare a private palette (and center). Returns SENSE_WORLD_MODEL_NONE or
 * SENSE_WORLD_MODEL_APPLY (never DROP). Never touches the CPU, the stack or
 * the world object, and never dispatches. */
#include "sense_world_model_override.h"
#define HALO_SENSE_HAVE_PICKUP_MODEL_PREPARE 1
static int sense_pickup_model_prepare(EngineCPU *cpu, SenseWorldModelOverride *out) {
    if (!out || !sense_pick_render.active || !halo_sense_physical_pickup()) return SENSE_WORLD_MODEL_NONE;
    memset(out, 0, sizeof *out);
    uint32_t sp = cpu->gpr[4];
    if (!sense_range(sp, 0x30)) return 0;
    uint32_t ret = G32(sp), object = G32(sp + 0x24), flags = G32(sp + 0x2C);
    if ((ret != 0x0050F049u && ret != 0x0050F0BDu) || (ret == 0x0050F0BDu) != ((flags & 2u) != 0) ||
        object != sense_pick_render.object || !sense_pickup_campaign()) return 0;
    uint32_t body = sense_object(object, 0x340, 2);
    if (!body || G32(body + 0x11C) != UINT32_MAX) return 0; /* still a parentless ground weapon */
    int16_t offset = (int16_t)G16(body + 0x1F2);
    uint32_t definition = sense_tag(G32(body), 0x38);
    uint32_t model = definition ? G32(definition + 0x34) : UINT32_MAX, data = sense_tag(model, 0xC0);
    uint32_t count = data ? G32(data + 0xB8) : 0;
    uint32_t entry = G32(G32(0x008603B0u) + 0x34) + (object & 65535u)*12u, size = G16(entry + 6);
    if (offset < 0x1F4 || cpu->gpr[1] != body + (uint32_t)offset || cpu->gpr[0] != model || !count || count > 64 ||
        (uint32_t)offset + count*52u > size) return 0;
    /* A relevant draw of the selected ground weapon. Cosmetic qualification
     * (read only; never seeds or changes the accepted frame or any guest
     * byte): the pose must have been published from this outer epoch's live
     * accepted frame for the same unit, weapon, session and input
     * generation. Otherwise this draw and every later one of the epoch use
     * the original palette; the next epoch qualifies afresh. */
    uint64_t epoch = sense_accepted_frame_epoch();
    if (!epoch || sense_pick_render_fallback_epoch == epoch) return SENSE_WORLD_MODEL_NONE;
    SenseAcceptedWeaponFrame live;
    if (sense_pick_render.epoch != epoch ||
        !sense_accepted_weapon_frame(sense_pick_render.unit, sense_pick_render.weapon, &live) ||
        live.epoch != epoch || live.session != sense_pick_render.session || live.generation != sense_pick_render.generation) {
        sense_pick_render_fallback_epoch = epoch; return SENSE_WORLD_MODEL_NONE;
    }
    if (!sense_pick_scratch) sense_pick_scratch = guest_alloc(0x100);
    static uint32_t palette; if (!palette) palette = guest_alloc(64*52+16);
    if (!sense_range(palette, 64*52+16) || !sense_range(sense_pick_scratch, 0x100)) return 0;
    HaloSenseTransform root;
    if (!sense_node(body + (uint32_t)offset, &root)) return 0;
    HaloSenseTransform delta = sense_delta(&root, &sense_pick_render.pose, root.p, sense_pick_render.pose.p);
    memcpy(GPTR(palette), GPTR(body + (uint32_t)offset), count*52u);
    for (uint32_t i = 0; i < count; ++i) sense_apply_node(palette + i*52u, &delta); /* rigid: scale and lengths kept */
    out->palette = palette;
    uint32_t center = G32(sp + 0x18);
    if (center && sense_range(center, 12)) {
        float c[3]; memcpy(c, GPTR(center), 12); sense_point(&delta, c, c); memcpy(GPTR(sense_pick_scratch), c, 12);
        out->center = sense_pick_scratch;
    }
    return 1;
}

/* Delivery-boundary check, engine thread, guest memory only (no locks).
 * Reload: the same local unit still holds the same weapon on foot, idle,
 * not full, with reserve, outside melee/scope; option still on. */
static int sense_physical_delivery_check(int action, uint32_t owner, uint32_t target) {
    if (!engine_flat_base) return 0;
    if (action == HALO_SENSE_GESTURE_PHYSICAL_RELOAD) {
        if (!halo_sense_physical_magazine()) return 0;
        uint32_t datum, weapon, unit = sense_local_unit(&datum, &weapon);
        if (!unit || datum != owner || weapon != target ||
            sense_action_for_unit(unit, datum, weapon, NULL) != HALO_SENSE_ACTION_NONE) return 0;
        unsigned loaded_max = 0; uint32_t body = sense_mag_weapon(weapon, &loaded_max);
        return body && G16(body + 0x2B0) == 0 && G16(body + 0x2B6) > 0 && G16(body + 0x2B8) < loaded_max;
    }
    if (action == HALO_SENSE_GESTURE_PHYSICAL_PICKUP) return sense_pickup_delivery_check(owner, target);
    return 0;
}
__attribute__((constructor)) static void sense_physical_register(void) {
    halo_sense_physical_set_delivery_check(sense_physical_delivery_check);
}
