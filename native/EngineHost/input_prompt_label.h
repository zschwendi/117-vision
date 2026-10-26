/* Which physical control produces a given Halo input, for native prompt labels.
 *
 * Halo's prompt formatter names the keyboard key or mouse input bound to an
 * action (input_prompt_hooks.inc). With the default controller bridge
 * (HALO_PAD2KEY) the controller produces exactly those keys and mouse inputs,
 * so the truthful label is the control that emits the named input during
 * gameplay. These tables mirror the bridge; keep them in step with:
 *   dinput8.c keyboard_base_state (buttons -> DIK) and
 *     host_dinput_mouse_buttons_state (R2/L1 -> mouse buttons 0/1, a Sense
 *     L1 release -> the grenade pulse),
 *   d3d9.c Present (left stick -> W/A/S/D, right stick -> mouse motion),
 *   sense_pose.inc halo_sense_filter_gamepad, whose current branch
 *     halo_sense_prompt_context reports (sense_pose.h).
 * The first emitter with a known, unremapped name that currently reaches the
 * game wins, so the documented primary (Square for reload) is preferred and
 * an alternative (D-pad Left) is used only when the primary is unavailable.
 * An input nothing emits, a withheld control or an unknown name yields no
 * label: original text. Ordinary pads ignore the Sense context. Pure and
 * allocation-free. */
#ifndef HALO_INPUT_PROMPT_LABEL_H
#define HALO_INPUT_PROMPT_LABEL_H
#include <stdint.h>
#include <string.h>
#include "gamecontroller.h"
#include "sense_pose.h"

enum { HOST_PROMPT_LABEL_MAX = HOSTGC_PROMPT_NAME_MAX };   /* 23 ASCII + NUL */

typedef struct {
    uint8_t dik;
    int8_t ordinary[2], spatial[2];   /* element indices in priority order, -1 = none */
    const char *suffix;               /* stick direction, only where the stick is unrotated */
} HostPromptEmitter;

#define HP_NONE (-1)
static const HostPromptEmitter host_prompt_emitters[] = {
    { 0x12, { HOSTGC_BTN_X, HP_NONE },              { HOSTGC_BTN_X, HP_NONE }, NULL },                 /* E: use */
    { 0x13, { HOSTGC_BTN_X, HOSTGC_BTN_DPAD_LEFT }, { HOSTGC_BTN_X, HOSTGC_BTN_DPAD_LEFT }, NULL },     /* R: reload */
    { 0x2D, { HOSTGC_BTN_DPAD_RIGHT, HP_NONE },     { HOSTGC_BTN_X, HOSTGC_BTN_DPAD_RIGHT }, NULL },    /* X: exchange (Sense Square too) */
    { 0x39, { HOSTGC_BTN_A, HP_NONE },              { HOSTGC_BTN_A, HP_NONE }, NULL },                 /* Space: jump */
    { 0x1D, { HOSTGC_BTN_B, HOSTGC_BTN_LTHUMB },    { HOSTGC_BTN_B, HOSTGC_BTN_LTHUMB }, NULL },        /* LCtrl: crouch */
    { 0x0F, { HOSTGC_BTN_Y, HP_NONE },              { HOSTGC_BTN_Y, HP_NONE }, NULL },                 /* Tab: switch weapon */
    { 0x21, { HOSTGC_BTN_RSHOULDER, HP_NONE },      { HOSTGC_BTN_RSHOULDER, HP_NONE }, NULL },         /* F: melee */
    { 0x2C, { HOSTGC_BTN_LTRIGGER, HOSTGC_BTN_RTHUMB }, { HOSTGC_BTN_RTHUMB, HOSTGC_BTN_LTRIGGER }, NULL }, /* Z: zoom */
    { 0x10, { HOSTGC_BTN_DPAD_UP, HP_NONE },        { HOSTGC_BTN_DPAD_UP, HP_NONE }, NULL },           /* Q: flashlight */
    { 0x22, { HOSTGC_BTN_DPAD_DOWN, HP_NONE },      { HOSTGC_BTN_DPAD_DOWN, HP_NONE }, NULL },         /* G: grenade type */
    { 0x3B, { HOSTGC_BTN_OPTIONS, HP_NONE },        { HOSTGC_BTN_OPTIONS, HOSTGC_PROMPT_CREATE_R3 }, NULL }, /* F1: scores */
    { 0x01, { HOSTGC_BTN_MENU, HP_NONE },           { HOSTGC_BTN_MENU, HP_NONE }, NULL },              /* Esc: pause */
    { 0x11, { HOSTGC_PROMPT_LSTICK, HP_NONE },      { HOSTGC_PROMPT_LSTICK, HP_NONE }, " Up" },        /* W */
    { 0x1F, { HOSTGC_PROMPT_LSTICK, HP_NONE },      { HOSTGC_PROMPT_LSTICK, HP_NONE }, " Down" },      /* S */
    { 0x1E, { HOSTGC_PROMPT_LSTICK, HP_NONE },      { HOSTGC_PROMPT_LSTICK, HP_NONE }, " Left" },      /* A */
    { 0x20, { HOSTGC_PROMPT_LSTICK, HP_NONE },      { HOSTGC_PROMPT_LSTICK, HP_NONE }, " Right" },     /* D */
};
enum { HOST_PROMPT_EMITTER_COUNT = (int)(sizeof host_prompt_emitters / sizeof host_prompt_emitters[0]) };

/* Whether a Sense element reaches the game in the filter's current branch. */
static inline int host_prompt_spatial_live(const HaloSensePromptContext *c, int e) {
    if (!c || e < 0 || e >= HOSTGC_PROMPT_ELEMENTS || e == HOSTGC_BTN_HOME) return 0;
    int withheld = e == HOSTGC_BTN_LSHOULDER || e == HOSTGC_BTN_LTRIGGER;   /* clutch, offhand */
    switch (c->context) {
    case HALO_SENSE_PROMPT_PASSTHROUGH: return e != HOSTGC_PROMPT_CREATE_R3;
    case HALO_SENSE_PROMPT_SEATED:      return !withheld && e != HOSTGC_PROMPT_CREATE_R3;
    case HALO_SENSE_PROMPT_SCOPE:       return !withheld && e != HOSTGC_PROMPT_RSTICK && e != HOSTGC_PROMPT_CREATE_R3;
    case HALO_SENSE_PROMPT_TRACKED:
        if (withheld || e == HOSTGC_PROMPT_RSTICK) return 0;
        if (e == HOSTGC_BTN_OPTIONS) return !c->gestures_own_create;      /* centered Create is the clutch */
        if (e == HOSTGC_PROMPT_CREATE_R3) return c->gestures_own_create;  /* Create + R3 is the scoreboard */
        return 1;
    case HALO_SENSE_PROMPT_SCOPE_HEAD:  return e == HOSTGC_BTN_MENU || e == HOSTGC_BTN_RTHUMB || e == HOSTGC_BTN_RSHOULDER;
    case HALO_SENSE_PROMPT_USE_ONLY:    return e == HOSTGC_BTN_X || e == HOSTGC_BTN_MENU;
    case HALO_SENSE_PROMPT_MENU_ONLY:   return e == HOSTGC_BTN_MENU;
    default:                            return 0;
    }
}
/* Movement keys come from the left stick unrotated only when the filter
 * leaves it alone; tracked and scoped movement follows the head. */
static inline int host_prompt_stick_unrotated(const HostGCPromptNames *names, const HaloSensePromptContext *c) {
    return !names->spatial || (c && (c->context == HALO_SENSE_PROMPT_PASSTHROUGH || c->context == HALO_SENSE_PROMPT_SEATED));
}
static inline int host_prompt_compose(const HostGCPromptNames *names, const HaloSensePromptContext *sense,
                                      const int8_t emitters[2], const char *prefix, const char *suffix,
                                      char out[HOST_PROMPT_LABEL_MAX]) {
    out[0] = 0;
    if (!names || names->family == HOSTGC_PROMPT_FAMILY_NONE) return 0;
    for (int i = 0; i < 2; i++) {
        int element = emitters[i];
        if (element < 0 || element >= HOSTGC_PROMPT_ELEMENTS) continue;
        if (names->spatial && !host_prompt_spatial_live(sense, element)) continue;
        const char *name = names->name[element];
        size_t n = strnlen(name, HOSTGC_PROMPT_NAME_MAX), p = prefix ? strlen(prefix) : 0, s = suffix ? strlen(suffix) : 0;
        if (!n || n >= HOSTGC_PROMPT_NAME_MAX || p + n + s >= HOST_PROMPT_LABEL_MAX) continue;
        if (p) memcpy(out, prefix, p);
        memcpy(out + p, name, n);
        if (s) memcpy(out + p + n, suffix, s);
        out[p + n + s] = 0;
        return 1;
    }
    return 0;
}
/* The control that emits keyboard DIK `dik`, or 0 (no label). */
static inline int host_prompt_label_for_dik(const HostGCPromptNames *names, const HaloSensePromptContext *sense,
                                            uint8_t dik, char out[HOST_PROMPT_LABEL_MAX]) {
    out[0] = 0;
    if (!names) return 0;
    for (int i = 0; i < HOST_PROMPT_EMITTER_COUNT; i++)
        if (host_prompt_emitters[i].dik == dik)
            return host_prompt_compose(names, sense, names->spatial ? host_prompt_emitters[i].spatial
                                                                    : host_prompt_emitters[i].ordinary,
                                       NULL, host_prompt_stick_unrotated(names, sense) ? host_prompt_emitters[i].suffix : NULL,
                                       out);
    return 0;
}
/* Halo's throw-grenade action (the action-name table at 0065B730; the same
 * ID a10_gamepad.inc binds to L1). */
enum { HOST_PROMPT_ACTION_THROW_GRENADE = 6 };
/* Mouse button 0 is R2 (fire) and mouse button 1 L1 for whatever action
 * Halo binds to them: the controller sends exactly those buttons. A tracked
 * Sense pair never presses mouse button 1; holding its left grip, making a
 * throwing motion and releasing requests one original grenade throw
 * (sense_throw_input.inc). That hint ("Hold L1") is given only for the
 * verified throw-grenade action while the clutch can deliver; anything else
 * bound to mouse button 1 keeps Halo's text there. */
static inline int host_prompt_label_for_mouse_button(const HostGCPromptNames *names, const HaloSensePromptContext *sense,
                                                     unsigned button, int action, char out[HOST_PROMPT_LABEL_MAX]) {
    int8_t emitters[2] = { HP_NONE, HP_NONE };
    out[0] = 0;
    if (!names) return 0;
    if (button == 0) emitters[0] = HOSTGC_BTN_RTRIGGER;
    else if (button == 1) {
        emitters[0] = HOSTGC_BTN_LSHOULDER;
        if (names->spatial && sense && sense->context != HALO_SENSE_PROMPT_PASSTHROUGH) {
            if (!sense->grenade_clutch || action != HOST_PROMPT_ACTION_THROW_GRENADE) return 0;
            /* The clutch is read from the raw grip, which the filter withholds
             * from ordinary mappings; name it directly. */
            const char *grip = names->name[HOSTGC_BTN_LSHOULDER];
            size_t n = strnlen(grip, HOSTGC_PROMPT_NAME_MAX);
            if (!n || n >= HOSTGC_PROMPT_NAME_MAX || 5 + n >= HOST_PROMPT_LABEL_MAX) return 0;
            memcpy(out, "Hold ", 5); memcpy(out + 5, grip, n); out[5 + n] = 0;
            return 1;
        }
    }
    return host_prompt_compose(names, sense, emitters, NULL, NULL, out);
}
/* Mouse X/Y motion comes from the right stick where the filter passes it. */
static inline int host_prompt_label_for_mouse_axis(const HostGCPromptNames *names, const HaloSensePromptContext *sense,
                                                   unsigned axis, char out[HOST_PROMPT_LABEL_MAX]) {
    int8_t emitters[2] = { HP_NONE, HP_NONE };
    if (axis < 2) emitters[0] = HOSTGC_PROMPT_RSTICK;
    return host_prompt_compose(names, sense, emitters, NULL, NULL, out);
}
#undef HP_NONE
#endif
