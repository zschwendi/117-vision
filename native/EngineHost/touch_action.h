#ifndef HALO_TOUCH_ACTION_H
#define HALO_TOUCH_ACTION_H
#include <stdint.h>

/* Retail PC1.10 semantic action IDs (a10_gamepad.inc). No profile writes. */
enum {
    HOST_TOUCH_JUMP=0, HOST_TOUCH_GRENADE_TYPE=1, HOST_TOUCH_USE=2,
    HOST_TOUCH_SWAP=3, HOST_TOUCH_MELEE=4, HOST_TOUCH_LIGHT=5,
    HOST_TOUCH_GRENADE=6, HOST_TOUCH_FIRE=7, HOST_TOUCH_CROUCH=10,
    HOST_TOUCH_ZOOM=11, HOST_TOUCH_RELOAD=13, HOST_TOUCH_PICKUP=14,
    HOST_TOUCH_FORWARD=19, HOST_TOUCH_BACK=20, HOST_TOUCH_LEFT=21, HOST_TOUCH_RIGHT=22,
    HOST_TOUCH_ACTION_COUNT=23
};
enum { HOST_TOUCH_EMITTER_NONE=0, HOST_TOUCH_EMITTER_KEY=1, HOST_TOUCH_EMITTER_BUTTON=2 };
typedef struct { uint8_t kind, index; } HostTouchBinding;
typedef uint16_t (*HostTouchRead16)(uint32_t address, void *context);

static inline int host_touch_action_supported(uint32_t action) {
    const uint32_t mask = (1u<<0)|(1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|
        (1u<<10)|(1u<<11)|(1u<<13)|(1u<<14)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<22);
    return action < HOST_TOUCH_ACTION_COUNT && (mask & (1u << action)) != 0;
}

static inline int host_touch_key_matches(uint8_t dik, uint32_t action, HostTouchRead16 read16, void *context) {
    uint16_t key = read16(0x0065BD58u + 2u * dik, context);
    return key < 0x6Du && read16(0x00710330u + 2u * key, context) == action;
}

/* Called only by the native input pump. Prefer the familiar binding only if
 * it still names this action; never fall back to a key now bound elsewhere. */
static inline HostTouchBinding host_touch_resolve(uint32_t action, HostTouchRead16 read16, void *context) {
    HostTouchBinding none = {0,0};
    if (!read16 || !host_touch_action_supported(action)) return none;
    static const uint8_t preferred[HOST_TOUCH_ACTION_COUNT] = {
        [0]=0x39,[1]=0x22,[2]=0x12,[3]=0x0F,[4]=0x21,[5]=0x10,
        [10]=0x1D,[11]=0x2C,[13]=0x13,[14]=0x2D,[19]=0x11,[20]=0x1F,[21]=0x1E,[22]=0x20
    };
    if (action == HOST_TOUCH_FIRE || action == HOST_TOUCH_GRENADE) {
        uint8_t button = action == HOST_TOUCH_FIRE ? 0 : 1;
        if (read16(0x0071040Au + 2u * button, context) == action)
            return (HostTouchBinding){HOST_TOUCH_EMITTER_BUTTON,button};
    }
    if (preferred[action] && host_touch_key_matches(preferred[action], action, read16, context))
        return (HostTouchBinding){HOST_TOUCH_EMITTER_KEY,preferred[action]};
    for (unsigned dik=1; dik<256; ++dik)
        if (host_touch_key_matches((uint8_t)dik, action, read16, context))
            return (HostTouchBinding){HOST_TOUCH_EMITTER_KEY,(uint8_t)dik};
    for (unsigned button=0; button<8; ++button)
        if (read16(0x0071040Au + 2u * button, context) == action)
            return (HostTouchBinding){HOST_TOUCH_EMITTER_BUTTON,(uint8_t)button};
    return none;
}
#endif
