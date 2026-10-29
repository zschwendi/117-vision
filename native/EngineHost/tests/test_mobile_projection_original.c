/* Execute Halo's actual projection builder with the launch plan's sizes.
 * No game assets, renderer, emulator or device. Generated original code is a
 * local optional input, never bundled in this source-only test. */
#if !__has_include("sub_0050CC40.c")
#include <stdio.h>
int main(void) { puts("SKIP: mobile projection needs locally generated engine sources"); return 0; }
#else
#include "../host.h"
#include "../mobile_graphics.h"
#include <assert.h>
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
#include "sub_0050CC40.c"
#include "sub_00401990.c"
#include "sub_004CB7A0.c"
#include "sub_004CBDE0.c"
#include "sub_004CBF10.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
uint8_t *engine_flat_base;
uint32_t engine_trace_lo, engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu) { (void)cpu; }
void engine_record(uint32_t address) { (void)address; }
void engine_dispatch(EngineCPU *cpu, uint32_t address) {
    cpu->pc=address;
    switch(address) {
    case 0x00401990u: sub_00401990(cpu); return;
    case 0x004CB7A0u: sub_004CB7A0(cpu); return;
    case 0x004CBDE0u: sub_004CBDE0(cpu); return;
    case 0x004CBF10u: sub_004CBF10(cpu); return;
    default: fprintf(stderr,"unexpected original call %08x\n",address); abort();
    }
}
static void putf(uint32_t at, float f) { memcpy(GPTR(at),&f,4); }
static float getf(uint32_t at) { float f;memcpy(&f,GPTR(at),4);return f; }
static void projection(uint32_t w, uint32_t h, float fov, float *x, float *y) {
    enum { INPUT=0x30000, OUTPUT=0x40000, STACK=0x20000, RETURN=0xDEADC0DE };
    memset(GPTR(INPUT),0,0x54); memset(GPTR(OUTPUT),0,0x18C);
    putf(INPUT+0x0C,1.f); putf(INPUT+0x20,1.f); putf(INPUT+0x28,fov);
    putf(INPUT+0x3C,0.1f);putf(INPUT+0x40,1000.f);
    S16(INPUT+0x30,h); S16(INPUT+0x32,w);
    unsigned char before[0x54];memcpy(before,GPTR(INPUT),sizeof before);
    S32(STACK,RETURN); S32(STACK+4,1);
    EngineCPU cpu={0};cpu.pc=0x0050CC40u;cpu.gpr[4]=STACK;cpu.gpr[1]=INPUT;cpu.gpr[6]=OUTPUT;
    cpu.fp_control=0x027F;cpu.flags=0x202;
    sub_0050CC40(&cpu);
    assert(cpu.pc==RETURN && cpu.gpr[4]==STACK+4 && G8(OUTPUT+0x140));
    assert(!memcmp(before,GPTR(INPUT),sizeof before));
    *x=getf(OUTPUT+0x144);*y=getf(OUTPUT+0x158);
    assert(isfinite(*x) && isfinite(*y) && *x>0 && *y>0);
    /* Equal length horizontal and vertical scene vectors occupy equal
     * pixel distances: circles do not become widescreen ellipses. */
    assert(fabsf(*x*w-*y*h) < 0.002f * h);
    assert(fabsf(*y - 1.f/tanf(fov*0.5f)) < 0.0001f);
    assert(getf(OUTPUT+0x164)==0 && getf(OUTPUT+0x168)==0);
}
int main(void) {
    engine_flat_base=calloc(0x900000,1);assert(engine_flat_base);
    /* The original scalar constants used by the projection and vector helpers. */
    putf(0x672ABC,0.5f); putf(0x672AC0,0.f); putf(0x672AC4,1.f); putf(0x672BF4,-0.5f);
    double two=2.0, epsilon=0.00001;memcpy(GPTR(0x672AF8),&two,8);memcpy(GPTR(0x672BD8),&epsilon,8);
    const int64_t windows[][2]={{2752,2064},{2420,1668},{2868,1320},{1500,1100},{900,1200}};
    const char *qualities[]={"native","1280x960","1600x1200","1920x1440","2048x1536"};
    float base_x,base_y;projection(640,480,1.2f,&base_x,&base_y);
    for(unsigned v=0;v<sizeof windows/sizeof windows[0];v++) for(unsigned q=0;q<sizeof qualities/sizeof qualities[0];q++) {
        HaloMobilePlan plan;halo_mobile_plan(qualities[q],"display",NULL,windows[v][0],windows[v][1],120,&plan);
        float x,y;projection(plan.render.width,plan.render.height,1.2f,&x,&y);
        assert(fabsf(base_y-y)<0.0001f); /* Native vertical FOV retained. */
        if((uint64_t)plan.render.width*3>(uint64_t)plan.render.height*4) assert(x<base_x); /* Wider view reveals more world. */
        projection(plan.render.width,plan.render.height,0.2f,&x,&y); /* Zoom uses the same correct geometry. */
    }
    free(engine_flat_base);
    puts("PASS: actual Halo projection preserves circles, center, vertical FOV and zoom across adaptive iPad, iPhone and window sizes");
    return 0;
}
#endif
