/* Which builds send Halo's time update (004C9F90) through the frame-rate
 * hook (frame_limit_hooks.inc). Built once per target with
 * -DEXPECT_ROUTED=0 (the Mac: the direct call, unchanged) or 1 (iPhone, iPad
 * and visionOS). A wrong routing fails to compile, so the device targets need
 * only a compile; on the Mac the binary also runs. No game files. */
#include <assert.h>
#include <stdio.h>
#include "engine_hooks.h"

#ifndef EXPECT_ROUTED
#error "Build with -DEXPECT_ROUTED=0 or 1"
#endif
#if ENGINE_IOS_HOOKS != EXPECT_ROUTED
#error "ENGINE_IOS_HOOKS does not match the expected frame-limit routing for this target"
#endif

int main(void) {
    assert(engine_hooked(0x004C9F90u) == EXPECT_ROUTED);
    assert(engine_hooked(0x004C6E80u));    /* an always-listed hook is unaffected */
    assert(!engine_hooked(0x004C9F91u));   /* only the entry itself */
    printf("PASS frame-limit routing: 004C9F90 %s\n", EXPECT_ROUTED ? "routed through the hook" : "direct call");
    return 0;
}
