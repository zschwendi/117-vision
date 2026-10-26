/* The tracked-haptics charge reader (sense_weapon_charge.h) against the
 * retail 1.10 charge getter 004C3100 itself, compiled from the locally
 * generated engine (read only; nothing copied). Both triggers, every state,
 * signed timers, several charging times and two sets of image constants:
 * the reader must equal the original fraction (clamped to 0..1) wherever the
 * original reports charging/charged, report "not charging" for every other
 * state, and refuse non-finite or non-positive definitions instead of
 * producing the original's division by zero. No game data is used. */
#include "host.h"
#include "engine_flags.h"
#include "engine_functions.h"
#include "engine_registers.h"
#include "sub_004C3100.c"
#undef eax
#undef ecx
#undef edx
#undef ebx
#undef esp
#undef ebp
#undef esi
#undef edi
#undef eflags
#include "../sense_weapon_charge.h"
#include <assert.h>
uint8_t *engine_flat_base;uint32_t engine_trace_lo,engine_trace_hi;
void engine_pc_trace(EngineCPU *cpu){(void)cpu;}
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);abort();}}while(0)
static void putf(uint32_t p,float f){memcpy(GPTR(p),&f,4);}
enum { BODY=0x16000, DEFINITION=0x33000, TRIGGERS=0x34000 };
static void set_trigger(unsigned t,int state,int timer,float charging){
    S8(BODY+0x261+t*0x28,(uint8_t)state);S16(BODY+0x262+t*0x28,(uint16_t)timer);putf(TRIGGERS+t*0x114+0x48,charging);
}
static double original(unsigned trigger){
    EngineCPU cpu={.fp_control=0x37f};cpu.gpr[0]=0x34560001;cpu.gpr[4]=0x20000;cpu.pc=0x004C3100;
    S32(0x20000,0x401234);S32(0x20004,trigger);sub_004C3100(&cpu);CHECK(cpu.pc==0x401234);
    return engine_fp_read(&cpu,0);
}
int main(void){
    engine_flat_base=calloc(1,0x900000);assert(engine_flat_base);
    /* Object table entry 1 -> body; tag row 1 -> definition -> trigger block. */
    S32(0x8603B0,0x13000);S32(0x13034,0x14000);S32(0x14014,BODY);S32(BODY,0x45670001);
    S32(0x87BC14,0x30000);S32(0x30034,DEFINITION);S32(DEFINITION+0x4FC,2);S32(DEFINITION+0x500,TRIGGERS);
    const float ticks[2]={1.f/30.f,1.f/60.f},times[3]={.5f,1.f,2.f};
    const int timers[]={-5,0,1,7,15,29,30,45,60,120};
    unsigned compared=0;
    for(unsigned k=0;k<2;k++){
        putf(0x672AC0,0);putf(0x672AC4,1);putf(0x672ACC,ticks[k]);
        for(unsigned t=0;t<2;t++)for(int state=0;state<=5;state++)for(unsigned i=0;i<sizeof timers/sizeof *timers;i++)
            for(unsigned c=0;c<3;c++){
                set_trigger(0,0,0,1);set_trigger(1,0,0,1);set_trigger(t,state,timers[i],times[c]);
                double o=original(t);int s;float f;int charging=sense_weapon_charge_read(BODY,DEFINITION,&s,&f);
                if(state==2||state==3){
                    CHECK(charging&&s==state);
                    float clamped=(float)fmin(1.0,fmax(0.0,o));CHECK(fabsf(f-clamped)<=2e-6f);
                }else CHECK(!charging&&s==0&&f==0&&o==0.0);
                compared++;
            }
    }
    /* Both triggers charging: the stronger state, then fraction, is reported,
     * and each equals what the original returns for that trigger. */
    putf(0x672ACC,1.f/30.f);set_trigger(0,2,15,1);set_trigger(1,2,6,1);
    int s;float f;CHECK(sense_weapon_charge_read(BODY,DEFINITION,&s,&f)&&s==2&&fabsf(f-(float)original(1))<=2e-6f);
    set_trigger(0,3,0,1);CHECK(sense_weapon_charge_read(BODY,DEFINITION,&s,&f)&&s==3&&f==(float)original(0));
    /* Definitions the original would divide by or overflow on: refused. */
    set_trigger(0,0,0,1);set_trigger(1,2,15,0);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    set_trigger(1,2,15,-1);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    set_trigger(1,2,15,NAN);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    set_trigger(1,2,15,1);putf(0x672ACC,NAN);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));putf(0x672ACC,1.f/30.f);
    S32(DEFINITION+0x4FC,0);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    S32(DEFINITION+0x4FC,3);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    S32(DEFINITION+0x4FC,2);S32(DEFINITION+0x500,0);CHECK(!sense_weapon_charge_read(BODY,DEFINITION,&s,&f));
    free(engine_flat_base);
    printf("PASS %u checks: tracked charge reader equals original 004C3100 on %u state/timer/time/trigger/constant cases; stronger trigger reported; invalid definitions refused\n",checks,compared);
    return 0;
}
