/* Seat (turret/vehicle) boundaries for the tracked weapon lanes. Included by
 * sense_weapon_hooks.inc after every lane it resets.
 *
 * Original ABI (retail 1.10). Entry 00566970 is the only seat-entry writer:
 * unit+0x2F0=seat (00566A72), unit+0x11C=vehicle (00566A79); the seat block
 * is the unit tag's +0x2E4 count/+0x2E8 pointer, stride 0x11C (0056A310).
 * A biped's exit is requested by 0056C470, which starts the exit animation
 * (unit+0x2A3=0x1B) and leaves the biped seated; the exit completes in
 * 0056C640 (or another inlined exit: 005590A0, 0056AB50, 0056A4C0, ...).
 * Every exit detaches (004F6610), writes unit+0x2F0=-1 and stamps the last
 * vehicle +0x32C and game tick +0x330 (0056C78E/0056C79D), then 0056D6A0
 * re-selects the held weapon. The mod only reads these fields.
 *
 * While seated the original seat owns aim, fire and its crosshair (native
 * controls). Identity is observed live at every poll and refresh, and
 * before and after the original entry (00566970), so a seated->on-foot->
 * seated cycle between polls is always distinct, even within one game tick;
 * the exit stamp is supplementary evidence only. Every identity change -- entry, exit completion, seat change,
 * death, respawn, another player unit -- invalidates each tracked lease, so
 * the next on-foot epoch admits a fresh pose from the original post-exit
 * state and nothing latched before the boundary is drawn, fired or thrown. */
#include "sense_mount.h"
typedef struct { HaloSenseMountIdentity id; uint32_t last_vehicle, exit_tick; } SenseMountState;
static struct { SenseMountState state; int observed; uint64_t counter; } sense_mount;
static int sense_mount_nested; /* inside the original seat entry witness */
/* The local player's live unit, as sense_local_body but without its menu /
 * input-disabled gates: pausing is not a seat boundary. */
static uint32_t sense_mount_body(uint32_t *datum) {
    uint32_t globals=G32(0x0087A478u),array=G32(0x0087A480u);
    if(!sense_range(globals,0x12)||!sense_range(array,0x38))return 0;
    uint32_t player=G32(globals+4),rows=G32(array+0x34);unsigned count=G16(array+0x20),i=player&65535u;
    if(player==UINT32_MAX||!count||count>16||i>=count||G16(array+0x22)!=0x200||!sense_range(rows,count*0x200u))return 0;
    uint32_t row=rows+i*0x200u;if(G16(row)!=(player>>16))return 0;
    *datum=G32(row+0x34);uint32_t unit=sense_object(*datum,0x334,0); /* through the exit stamp +0x32C/+0x330 */
    float health=0;if(unit)memcpy(&health,GPTR(unit+0xE0),4);
    return isfinite(health)&&health>0.f?unit:0;
}
static void sense_mount_read(SenseMountState *out) {
    memset(out,0,sizeof *out);HaloSenseMountIdentity *id=&out->id;
    id->unit=id->vehicle=UINT32_MAX;id->seat=-1;id->mode=HALO_SENSE_MOUNT_NONE;
    uint32_t datum,unit=sense_mount_body(&datum);
    if(!unit||G16(0x00719720u)!=0)return;
    uint32_t parent=G32(unit+0x11C);int seat=(int16_t)G16(unit+0x2F0);
    id->unit=datum;id->vehicle=parent;id->seat=seat;out->last_vehicle=G32(unit+0x32C);out->exit_tick=G32(unit+0x330);
    if(parent==UINT32_MAX&&seat==-1){id->mode=HALO_SENSE_MOUNT_ON_FOOT;return;}
    uint32_t vehicle=sense_object(parent,0x32C,1),tag=vehicle?sense_tag(G32(vehicle),0x2EC):0;
    int32_t seats=tag?(int32_t)G32(tag+0x2E4):0;
    if(seat>=0&&seat<seats&&seats<=64&&sense_range(G32(tag+0x2E8),(size_t)seats*0x11Cu))id->mode=HALO_SENSE_MOUNT_SEATED;
}
static int sense_mount_same(const SenseMountState *a,const SenseMountState *b) {
    return a->id.unit==b->id.unit&&a->id.vehicle==b->id.vehicle&&a->id.seat==b->id.seat&&a->id.mode==b->id.mode&&
        a->last_vehicle==b->last_vehicle&&a->exit_tick==b->exit_tick;
}
/* Physical lanes end completely, including a COMMITTED magazine (which
 * sense_physical_interrupt keeps for in-place builds), its queued DirectInput
 * commit, this epoch's latched left-hand/drawn state and any pickup. An
 * original reload already delivered continues untouched in the engine. */
static void sense_mount_cancel_physical(void) {
    halo_sense_physical_cancel(HALO_SENSE_GESTURE_PHYSICAL_RELOAD);
    if(sense_mag.state.stage!=SENSE_MAG_IDLE)sense_mag_reset(&sense_mag.state,SENSE_MAG_RESULT_CANCELLED);
    sense_mag.have_grip=sense_mag.pinned=sense_mag.charge_support=0;
    sense_mag_left_owned=sense_mag_drawn_removed=0;sense_mag_drawn_epoch=0;
    sense_mag_epoch.epoch=0;sense_mag_epoch.draw=SENSE_MAG_EPOCH_SEATED;sense_mag_epoch.handle_draw=SENSE_HANDLE_NONE;
    sense_mag_publish(HALO_SENSE_MAGAZINE_BLOCKED);
    sense_pickup_cancel_now(SENSE_PICKUP_RESULT_CANCELLED);sense_pick_left_owned=0;sense_pick.update_epoch=0;
}
/* Cancel every tracked lease keyed before the boundary. Original unit,
 * inventory, parent and aim are untouched; a shot already inside the
 * original fire call keeps its own copied ray. */
static void sense_mount_reset_lanes(void) {
    sense_frame_fail();sense_offhand_accepted.attempted=sense_frame_epoch;sense_offhand_accepted.accepted=0;
    sense_camera_unit=UINT32_MAX;sense_muzzles[0].valid=sense_muzzles[1].valid=0;
    sense_support.valid=sense_support.engaged=0;
    sense_grip_reset(&sense_grip);sense_scope_filter_reset(&sense_scope_aim);
    sense_mount_cancel_physical();
#ifdef HALO_SENSE_GRENADE_CANCEL_MOUNT
    sense_grenade_cancel_for_mount(); /* grenade120: poisons a claimed release; never a head-ray launch */
#else
    sense_grenade_release.valid=0;    /* build119 grenade module only: no frozen release survives a boundary */
#endif
    sense_dual_reset_model();
    halo_sense_set_scope_active(0);halo_sense_reticle_publish(NULL);
}
/* Reads the live identity; a change starts a new mount generation. Returns
 * the mode and always republishes native control ownership (the input side
 * acts only on a change). Engine thread. */
static int sense_mount_observe(void) {
    SenseMountState now;sense_mount_read(&now);SenseMountState *s=&sense_mount.state;
    if(!sense_mount.observed||!sense_mount_same(&now,s)){
        if(++sense_mount.counter==0)++sense_mount.counter;
        now.id.generation=sense_mount.counter;*s=now;sense_mount.observed=1;
        sense_mount_reset_lanes();
        if(HOST_ENV("HALO_SENSE_WEAPON_TRACE"))host_log("[sense-mount] generation=%llu mode=%d unit=%08x vehicle=%08x seat=%d exit=%u",
            (unsigned long long)s->id.generation,s->id.mode,s->id.unit,s->id.vehicle,s->id.seat,s->exit_tick);
    }
    halo_sense_set_native_controls(s->id.mode==HALO_SENSE_MOUNT_SEATED,s->id.generation);
    return s->id.mode;
}
/* Tracked on-foot control this epoch: the observed biped on foot (menus and
 * cutscenes stay sense_local_body's gates, original selection checks stay
 * sense_local_unit's). */
static int sense_mount_on_foot(uint32_t datum) {
    return sense_mount.observed&&sense_mount.state.id.mode==HALO_SENSE_MOUNT_ON_FOOT&&sense_mount.state.id.unit==datum;
}
/* Explicit boundary for admission points outside these hooks (grenade
 * hold/release). Only while Sense owns the weapon path; never call it from
 * inside an in-flight original action whose leases it would cancel. */
int host_sense_mount_refresh(void) {
    if(host_sense_weapon_rendering())return sense_mount_observe();
    return sense_mount.observed?sense_mount.state.id.mode:HALO_SENSE_MOUNT_NONE;
}
int host_sense_mount_identity(HaloSenseMountIdentity *out) { /* read only */
    if(!out)return 0;
    if(!sense_mount.observed){memset(out,0,sizeof *out);out->unit=out->vehicle=UINT32_MAX;out->seat=-1;return 0;}
    *out=sense_mount.state.id;return 1;
}
