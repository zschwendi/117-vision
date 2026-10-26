/* The armed grenade in the tracked left hand, render only (engine thread;
 * included by sense_weapon_hooks.inc after sense_physical_hooks.inc).
 *
 * While L1 is held as the throw clutch (halo_sense_grenade_hold), the
 * selected grenade's own model is drawn in the palm of the left hand the
 * viewmodel draws. Nothing is spent, created, attached, moved or delivered:
 * the release, its single original debit (0056E280), the hand-directed launch
 * and every acceptance contract stay in sense_grenade.inc / the bridge.
 *
 * Selection, verified on c9acf0c4...: 0056E280 reads the type as the int8 at
 * unit +0x31C and debits the count byte at unit +0x31E+type; the globals tag
 * [0x00746FA0] grenades block is count +0x128, elements +0x12C (stride 0x44),
 * and 0056E359 passes element +0x40 (the 'proj' reference's datum) to the
 * object placement 004F53A0. The retail globals hold frag (0) and plasma (1);
 * both projectiles' object definition +0x34 is a one-node model ('frame
 * grenade') with LOD cutoffs 0/10/25/50/100 at model +0x08..+0x18. Only
 * types 0 and 1 have count bytes; +0x320/+0x321 are zoom (005659C0).
 *
 * Draw: 004924B0 draws the viewmodel with two 004D6FC0 calls, the gun
 * (returning to 0049266F) and then the arms (returning to 004926DA), with
 * flags 8, the camera 007C3114 as centre and the gun's salted datum. After
 * the original arms draw returns, one more original 004D6FC0 submission uses
 * those same entry arguments with EAX = the grenade model, ECX = a private
 * palette (sense_grenade_hold.h), the model's own highest-detail cutoff as
 * the pixel size (004D70E0 then selects its finest level; the arms' 0 would
 * select the coarsest) and 0 for the two per-object permutation/colour
 * records, for which 004D703D/004D704B substitute the engine defaults (the
 * grenade has no object). The mirrored offhand pass never draws it.
 *
 * One decision per outer epoch, at the first primary viewmodel build after
 * this epoch's accepted frame: the hold must belong to the frame's own
 * session and input generation; the tracked left hand must exist (not
 * holding a physical magazine/pickup); no melee, reload, swap or scope; the
 * unit not inside an original throw (+0x28D 1 or 2, when the real grenade
 * exists); a grenade of the selected type left. Dual wielding: the held
 * grenade takes the left hand for that epoch. The primary rig draws its
 * tracked left hand, the mirrored offhand gun and its reticle are not drawn,
 * and the offhand cannot fire or request a reload (its own original update
 * still runs); afterwards it re-arms only after L2 is released (dual_input.h).
 * Every injected draw re-qualifies everything live, before and after the
 * original arms draw; any loss fails closed for the rest of the epoch. */
#include "sense_grenade_hold.h"
static int sense_mirror_scope; /* sense_dual_render.inc */
static struct {
    uint64_t epoch, session, generation;
    uint32_t unit, weapon, projectile, model;
    int type, active, palm, dual;
    unsigned count;
    float lod;
    float palette[SENSE_GRENADE_HOLD_MAX_NODES][13];
} sense_hold;
static uint64_t sense_hold_decided, sense_hold_failed;
static int sense_hold_nested;
static unsigned sense_hold_draws;
static void sense_grenade_hold_clear(void) {
    uint64_t epoch = sense_accepted_frame_epoch();
    sense_hold.active = 0; if (epoch) sense_hold_decided = epoch;
}
/* This epoch's admitted armed grenade owns the left hand, whether or not the
 * dual offhand was ready at admission: an offhand that first becomes ready
 * later in the same epoch must not draw or fire over it. Frozen with the
 * epoch's presentation (a later draw failure hides only the grenade). */
static int sense_grenade_hold_owns_left(void) {
    uint64_t epoch = sense_accepted_frame_epoch();
    return epoch && sense_hold.active && sense_hold.epoch == epoch;
}
/* The selected grenade type with one left, and its projectile's model. */
static int sense_grenade_hold_selection(uint32_t datum, uint32_t unit, int *type, uint32_t *projectile, uint32_t *model) {
    if (sense_object(datum, 0x322, 0) != unit) return 0;
    int t = (int8_t)G8(unit + 0x31C); uint32_t globals = G32(0x00746FA0u);
    /* Two count bytes only (+0x31E frag, +0x31F plasma; +0x320/+0x321 are the
     * zoom bytes), whatever a globals block advertises. */
    if (t < 0 || t > 1 || !sense_range(globals, 0x130)) return 0;
    int32_t n = (int32_t)G32(globals + 0x128); uint32_t block = G32(globals + 0x12C);
    if (n < 1 || n > 4 || t >= n || !sense_range(block, (uint32_t)n*0x44u)) return 0;
    if (!G8(unit + 0x31E + (unsigned)t)) return 0; /* none left: the original refuses the throw */
    uint32_t proj = G32(block + (unsigned)t*0x44u + 0x40), definition = sense_tag(proj, 0x38);
    if (!definition || G16(definition) != 5) return 0; /* a projectile definition */
    uint32_t m = G32(definition + 0x34);
    if (!sense_tag(m, 0xC0)) return 0;
    *type = t; *projectile = proj; *model = m; return 1;
}
/* The hold may be shown for this unit/weapon now (decision and every draw). */
static int sense_grenade_hold_allowed(uint32_t unit, uint32_t datum, uint32_t weapon, const SenseAcceptedWeaponFrame *frame,
                                      int *type, uint32_t *projectile, uint32_t *model) {
    HaloSenseGrenadeHold hold; unsigned state = G8(unit + 0x28D);
    return halo_sense_grenade_hold(&hold) && hold.held && hold.session == frame->session && hold.generation == frame->generation &&
        !halo_sense_app_input_blocked() && !sense_physical_left_busy() &&
        sense_action_for_unit(unit, datum, weapon, NULL) == HALO_SENSE_ACTION_NONE && (state == 0 || state == 3) &&
        sense_grenade_hold_selection(datum, unit, type, projectile, model);
}
/* Rendered left palm of this build's output (world units), from the graph's
 * own wrist and knuckle nodes; each knuckle must hang under the wrist. */
static int sense_grenade_hold_palm(uint32_t output, uint32_t bones, unsigned count, const int16_t *parents,
                                   int left, HaloSenseTransform *out) {
    static const char *names[4] = {"frame l index low", "frame l middlelow", "frame l ring low", "frame l pinky low"};
    float p[4][3], wrist[3];
    if (left < 0 || (unsigned)left >= count) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        int node = sense_arm_named(bones, count, names[i]); HaloSenseTransform t;
        if (node < 0 || node == left || !sense_descendant((unsigned)node, left, parents, count) ||
            !sense_node(output + (unsigned)node*52u, &t)) return 0;
        memcpy(p[i], t.p, 12);
    }
    HaloSenseTransform w; if (!sense_node(output + (unsigned)left*52u, &w)) return 0;
    memcpy(wrist, w.p, 12);
    return sense_grenade_palm(wrist, p[0], p[1], p[2], p[3], out);
}
/* Primary build, after the tracked deltas and before the left hand is hidden.
 * dual_hidden: only the dual offhand would hide the tracked left hand. */
static void sense_grenade_hold_frame(uint32_t output, uint32_t bones, unsigned count, const int16_t *parents, int left,
                                     int left_tracked, int dual_hidden, uint32_t unit, uint32_t datum, uint32_t weapon,
                                     const HaloSenseTransform *raw_left) {
    uint64_t epoch = sense_accepted_frame_epoch();
    if (!epoch || sense_hold_decided == epoch) return; /* the epoch's presentation is frozen */
    sense_hold_decided = epoch; sense_hold.active = 0;
    SenseAcceptedWeaponFrame frame; int type; uint32_t projectile, model;
    if (!left_tracked || !sense_accepted_weapon_frame(datum, weapon, &frame) ||
        !sense_grenade_hold_allowed(unit, datum, weapon, &frame, &type, &projectile, &model)) return;
    uint32_t data = sense_tag(model, 0xC0), nodes = G32(data + 0xBC), node_count = G32(data + 0xB8);
    if (!node_count || node_count > SENSE_GRENADE_HOLD_MAX_NODES || !sense_range(nodes, node_count*0x9Cu)) return;
    int16_t node_parents[SENSE_GRENADE_HOLD_MAX_NODES]; float rotation[SENSE_GRENADE_HOLD_MAX_NODES][4], translation[SENSE_GRENADE_HOLD_MAX_NODES][3];
    for (unsigned i = 0; i < node_count; ++i) {
        node_parents[i] = (int16_t)G16(nodes + i*0x9Cu + 0x24);
        memcpy(rotation[i], GPTR(nodes + i*0x9Cu + 0x34), 16); memcpy(translation[i], GPTR(nodes + i*0x9Cu + 0x28), 12);
    }
    HaloSenseTransform root; int palm = sense_grenade_hold_palm(output, bones, count, parents, left, &root);
    if (!palm && !sense_grenade_grip(raw_left, &root)) return;
    if (!sense_grenade_compose(&root, node_count, node_parents, (const float (*)[4])rotation,
                               (const float (*)[3])translation, sense_hold.palette)) return;
    float lod; memcpy(&lod, GPTR(data + 0x18), 4);
    sense_hold.lod = isfinite(lod) && lod >= 0 && lod < 100000.f ? lod : 0;
    sense_hold.epoch = epoch; sense_hold.session = frame.session; sense_hold.generation = frame.generation;
    sense_hold.unit = datum; sense_hold.weapon = weapon; sense_hold.projectile = projectile; sense_hold.model = model;
    sense_hold.type = type; sense_hold.count = node_count; sense_hold.palm = palm; sense_hold.dual = dual_hidden;
    sense_hold.active = 1;
    if (HOST_ENV("HALO_SENSE_WEAPON_TRACE")) {
        static unsigned logs;
        if (logs++ < 24) host_log("[sense-grenade-hold] epoch=%llu type=%d projectile=%08x model=%08x palm=%d dual=%d at=%.4f,%.4f,%.4f",
                                  (unsigned long long)epoch, type, projectile, model, palm, dual_hidden,
                                  sense_hold.palette[0][10], sense_hold.palette[0][11], sense_hold.palette[0][12]);
    }
}
/* Everything the decision required, live, for the same epoch, lease, unit,
 * weapon (on foot: sense_local_unit), hold and selected model. */
static int sense_grenade_hold_live(uint64_t epoch) {
    uint32_t datum, weapon, unit = sense_local_unit(&datum, &weapon), projectile, model; int type;
    SenseAcceptedWeaponFrame live;
    return epoch && sense_hold.epoch == epoch && unit && datum == sense_hold.unit && weapon == sense_hold.weapon &&
        host_sense_weapon_rendering() && sense_accepted_weapon_frame(datum, weapon, &live) && live.epoch == epoch &&
        live.session == sense_hold.session && live.generation == sense_hold.generation &&
        sense_grenade_hold_allowed(unit, datum, weapon, &live, &type, &projectile, &model) &&
        type == sense_hold.type && model == sense_hold.model;
}
/* After the original arms draw of the primary viewmodel. */
static int sense_grenade_hold_dispatch(EngineCPU *cpu, uint32_t address) {
    if (address != 0x004D6FC0u || sense_hold_nested || !sense_hold.active) return 0;
    uint32_t stack = cpu->gpr[4];
    if (!sense_range(stack, 0x30) || G32(stack) != 0x004926DAu || sense_mirror_scope) return 0;
    uint64_t epoch = sense_accepted_frame_epoch();
    if (!epoch || sense_hold.epoch != epoch || sense_hold_failed == epoch || G32(stack + 0x24) != sense_hold.weapon) return 0;
    if (!sense_grenade_hold_live(epoch)) { sense_hold_failed = epoch; return 0; } /* the original arms alone, for the rest of the epoch */
    uint32_t caller = G32(stack), args[11];
    for (unsigned i = 0; i < 11; ++i) args[i] = G32(stack + 4 + 4*i); /* the callee reuses its argument slots */
    sense_hold_nested = 1; engine_dispatch(cpu, address); sense_hold_nested = 0;
    if (cpu->pc != caller) return 1;
    /* Anything the original arms draw (or a callback inside it) changed. */
    if (!sense_grenade_hold_live(epoch)) { sense_hold_failed = epoch; return 1; }
    static uint32_t palette;
    if (!palette) palette = guest_alloc(0x100);
    if (!sense_range(palette, 0x100)) { sense_hold_failed = epoch; return 1; }
    memcpy(GPTR(palette), sense_hold.palette, sense_hold.count*52u);
    memcpy(&args[0], &sense_hold.lod, 4); args[2] = args[3] = 0;
    EngineCPU q = *cpu; q.gpr[0] = sense_hold.model; q.gpr[1] = palette;
    sense_hold_nested = 1; host_call_guest(&q, 0x004D6FC0u, 11, args, 0); sense_hold_nested = 0;
    cpu->instruction_count = q.instruction_count; sense_hold_draws++;
    return 1;
}
