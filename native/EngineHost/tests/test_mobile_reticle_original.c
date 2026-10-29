/* The original Halo bitmap constructor and geometry, with only its final
 * renderer replaced by a vertex recorder. No game assets or device. */
#if !__has_include("sub_004ACAD0.c")
#include <stdio.h>
int main(void) { puts("SKIP: mobile reticle needs locally generated engine sources"); return 0; }
#else
#include "../host.h"
#include <assert.h>
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
#include "sub_004ACAD0.c"
#include "sub_004AB690.c"
#include "sub_004ACC50.c"
#include "sub_004ACD50.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#include "../mobile_reticle_hooks.inc"
uint8_t *engine_flat_base;
uint32_t engine_trace_lo, engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu) { (void)cpu; }
void engine_record(uint32_t address) { (void)address; }
static float vertices[4][6];static int calls,escape;
void sub_0051C9A0(EngineCPU *cpu) {
    memcpy(vertices,GPTR(G32(cpu->gpr[4]+4)),sizeof vertices);calls++;
    cpu->pc=engine_pop(cpu,4);
}
void engine_dispatch(EngineCPU *cpu, uint32_t address) {
    if(host_mobile_reticle_dispatch(cpu,address))return;
    cpu->pc=address;
    switch(address) {
    case 0x004ACAD0u: if(escape){cpu->pc=0xDEAD;return;}sub_004ACAD0(cpu);return;
    case 0x004AB690u: sub_004AB690(cpu);return;
    case 0x004ACC50u: sub_004ACC50(cpu);return;
    case 0x004ACD50u: sub_004ACD50(cpu);return;
    default: fprintf(stderr,"unexpected original call %08x\n",address);abort();
    }
}
static void putf(uint32_t at,float f){memcpy(GPTR(at),&f,4);}
static void draw(uint32_t w,uint32_t h,uint32_t caller,int expected_correction) {
    enum {STACK=0x20000,TAG=0x30000,BITMAP=0x30100,ANCHOR=0x30200};
    memset(GPTR(STACK-0x1000),0,0x2000);memset(GPTR(TAG),0,0x300);
    S32(STACK,caller);S32(STACK+8,BITMAP);S32(STACK+12,ANCHOR);putf(STACK+16,1.f);S32(STACK+28,0xFFFFFFFF);
    S16(BITMAP+4,32);S16(BITMAP+6,32);S16(ANCHOR,4);putf(TAG+4,1.f);putf(TAG+8,1.f);
    S16(0x7C1254,0);S16(0x7C1256,0);S16(0x7C1258,h);S16(0x7C125A,w);
    unsigned char tag_before[16];memcpy(tag_before,GPTR(TAG),16);
    EngineCPU cpu={0};cpu.pc=0x004ACAD0u;cpu.gpr[4]=STACK;cpu.gpr[2]=TAG;cpu.fp_control=0x027F;cpu.flags=0x202;
    calls=0;engine_dispatch(&cpu,0x004ACAD0u);
    assert(cpu.pc==caller && cpu.gpr[4]==STACK+4 && calls==1);
    assert(!memcmp(tag_before,GPTR(TAG),16));
    float minx=vertices[0][0],maxx=minx,miny=vertices[0][1],maxy=miny;
    for(int i=0;i<4;i++){minx=fminf(minx,vertices[i][0]);maxx=fmaxf(maxx,vertices[i][0]);miny=fminf(miny,vertices[i][1]);maxy=fmaxf(maxy,vertices[i][1]);}
    assert((minx+maxx)*0.5f==320.f && (miny+maxy)*0.5f==240.f);
    float px=(maxx-minx)*w/640.f,py=(maxy-miny)*h/480.f;
    if(expected_correction) assert(fabsf(px-py)<=w/320.f+0.01f); /* Original rounds vertices to virtual pixels. */
    else {assert(maxx-minx==32.f && maxy-miny==32.f);}
    /* UVs and original geometry remain finite; art is neither replaced nor cropped. */
    for(int i=0;i<4;i++)for(int j=0;j<6;j++)assert(isfinite(vertices[i][j]));
}
int main(void) {
    engine_flat_base=calloc(0x900000,1);assert(engine_flat_base);
    putf(0x672ABC,0.5f);putf(0x672AC4,1.f);putf(0x672BF4,-0.5f);
    S32(0x004ACD34+4*4,0x004ACCFF); /* Original center-anchor jump-table case. */
    draw(1600,1200,0x004B33C6,0);
    draw(2416,1665,0x004B33C6,HALO_MOBILE_RETICLE);
    draw(2864,1318,0x004B33C6,HALO_MOBILE_RETICLE);
    draw(896,1195,0x004B33C6,HALO_MOBILE_RETICLE);
    draw(2864,1318,0x00400000,0); /* Menus/damage/other callers are unchanged. */
#if HALO_MOBILE_RETICLE
    /* Unexpected dispatch exit still restores the authored tag and recursion guard. */
    EngineCPU cpu={0};cpu.gpr[4]=0x20000;cpu.gpr[2]=0x30000;S32(0x20000,0x004B33C6);
    putf(0x30004,1.f);escape=1;
    assert(host_mobile_reticle_dispatch(&cpu,0x004ACAD0u)==1 && cpu.pc==0xDEAD && !mobile_reticle_nested);
    assert(G32(0x30004)==0x3F800000);
#endif
    free(engine_flat_base);
    printf("PASS: original mobile reticle geometry center, aspect, art and tag restoration; correction=%d\n",HALO_MOBILE_RETICLE);
    return 0;
}
#endif
