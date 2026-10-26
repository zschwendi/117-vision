#ifndef HALO_SENSE_MOUNT_H
#define HALO_SENSE_MOUNT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Seat identity of the local campaign biped, read from the original unit:
 * parent +0x11C and seat +0x2F0, written by the retail seat entry 00566970
 * and cleared by completed exits (including biped completion 0056C640).
 * The mod never writes either. */
enum {
    HALO_SENSE_MOUNT_NONE = 0,    /* no live local campaign biped, or attached without a seat */
    HALO_SENSE_MOUNT_ON_FOOT = 1, /* tracked weapon control */
    HALO_SENSE_MOUNT_SEATED = 2   /* native seat controls: original aim, fire and crosshair */
};
typedef struct {
    uint64_t generation;  /* changes on every unit/vehicle/seat change, incl. death, respawn, ownership */
    uint32_t unit;        /* biped datum, UINT32_MAX when none */
    uint32_t vehicle;     /* parent vehicle datum, UINT32_MAX on foot */
    int seat;             /* original seat index, -1 on foot */
    int mode;             /* HALO_SENSE_MOUNT_* */
} HaloSenseMountIdentity;
/* Engine thread, read only: the identity of the latest observation, made at
 * the outer render boundary (0050BEA0), input/control tick (004C6E80), fire
 * (004C3F10), crosshair (004ACAD0), before and after original seat entry
 * (00566970), or by host_sense_mount_refresh. Returns 0 and NONE before the
 * first observation. Never resets anything. */
int host_sense_mount_identity(HaloSenseMountIdentity *out);
/* Engine thread: observe the live identity now (an admission boundary). A
 * change starts a new generation and cancels every tracked lease. Only
 * while Sense owns the weapon path; otherwise returns the last mode and
 * mutates nothing. Not from inside an in-flight original action. */
int host_sense_mount_refresh(void);

/* Input side, published by the engine with each observation. A publication
 * belongs to the current Sense session, controller connection and Sense
 * ownership: after an immersive end/begin, a handover or Sense switched
 * off/on, native controls stay off until the engine publishes again. Hand
 * tracking loss or pose changes never end it. Native seat
 * controls give the Sense pair ordinary gamepad aim, movement and fire for
 * the original seat; tracked gameplay stays unsupported, so no virtual turn,
 * gesture or tracked reticle runs with it. Any change of mode or mount
 * generation (seat, death, respawn, ownership) changes the Sense input
 * generation: fire arms, throw/hold, gestures and published reticles end,
 * and R2 must be released before the seat can fire. */
void halo_sense_set_native_controls(int native, uint64_t mount_generation);
int halo_sense_native_controls(void);
/* Seat boundary token: changes on entry, exit, seat change and leaving a
 * seat by death. The immersive renderer adds it to its recenter request, so
 * the whole display basis (physical neutral, virtual yaw/pitch, visor HUD,
 * gestures) is recentred once: the original seat camera and its crosshair
 * start at the viewer's front, and the on-foot view after exit does too. */
uint64_t halo_sense_seat_transition(void);
/* Both, read under one lock: the compositor samples this once per frame and
 * every eye and display drawable of that frame uses the same snapshot. */
typedef struct { int native; uint64_t transition; } HaloSenseSeatSnapshot;
void halo_sense_seat_snapshot(HaloSenseSeatSnapshot *out);

#ifdef __cplusplus
}
#endif
#endif
