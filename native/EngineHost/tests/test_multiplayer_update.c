/* Compare the app's update policy with Halo's actual no-update callback.
 * The translated callback is an optional private local input, never bundled.
 * Adapted from mitchaiet/master-chef PR #1 (MIT, same project). */
#include "multiplayer_update.h"
#include "engine_hooks.h"
#include <assert.h>
#include <stdlib.h>

uint8_t *engine_flat_base;
#if __has_include("sub_005777D0.c")
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
/* The callback's positive branch calls this helper directly. The tested
 * no-update branch never runs it, but the translation unit must link. */
#if __has_include("sub_00623A90.c")
#include "sub_00623A90.c"
#endif
#include "sub_005777D0.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#define HAVE_ORIGINAL 1
#endif
void engine_dispatch(EngineCPU *cpu,uint32_t address) {
    (void)cpu;(void)address;assert(!"no-update callback must not call network or updater code");abort();
}
int main(void) {
    enum { SIZE=0x800000, STACK=0x10000, RETURN_PC=0x12345678 };
    engine_flat_base=malloc(SIZE);assert(engine_flat_base);
    memset(engine_flat_base,0x5a,SIZE);
    S32(STACK,RETURN_PC);S32(STACK+4,0);
    uint8_t *before=malloc(SIZE);assert(before);memcpy(before,engine_flat_base,SIZE);
    EngineCPU cpu={0};cpu.gpr[4]=STACK;cpu.gpr[3]=0x87654321;
    cpu.gpr[5]=0x11111111;cpu.gpr[6]=0x22222222;cpu.gpr[7]=0x33333333;
    host_multiplayer_update_poll(&cpu);
    assert(engine_hooked(0x00577240));
    assert(cpu.gpr[0]==2 && cpu.pc==RETURN_PC && cpu.gpr[4]==STACK+4);
    assert(cpu.gpr[3]==0x87654321 && cpu.gpr[5]==0x11111111 && cpu.gpr[6]==0x22222222 && cpu.gpr[7]==0x33333333);
    /* Exactly the four no-update fields change; nothing else in guest memory. */
    assert(G32(0x007228D4)==0 && G8(0x007228D8)==0 && G8(0x007229D8)==0 && G32(0x0069FE04)==2);
    memset(before+0x007228D4,0,4);before[0x007228D8]=0;before[0x007229D8]=0;
    uint32_t two=2;memcpy(before+0x0069FE04,&two,4);
    assert(!memcmp(before,engine_flat_base,SIZE));
    free(before);
    puts("PASS native update policy cdecl return, preserved registers, exact writes and interception");
#ifdef HAVE_ORIGINAL
    uint8_t *expected=malloc(SIZE);assert(expected);memcpy(expected,engine_flat_base,SIZE);
    memset(engine_flat_base,0x5a,SIZE);S32(STACK,RETURN_PC);S32(STACK+4,0);
    cpu=(EngineCPU){0};cpu.gpr[4]=STACK;cpu.gpr[3]=0x87654321;cpu.pc=0x005777D0;
    sub_005777D0(&cpu);
    assert(cpu.pc==RETURN_PC && cpu.gpr[4]==STACK+4 && cpu.gpr[3]==0x87654321);
    /* The original callback saves EBX below ESP, unlike the native leaf. */
    S32(STACK-4,0x5a5a5a5a);
    assert(!memcmp(expected,engine_flat_base,SIZE));
    free(expected);puts("PASS all guest memory matches the original 1.10 no-update result");
#else
    puts("SKIP original callback comparison: private generated engine not present");
#endif
    free(engine_flat_base);return 0;
}
