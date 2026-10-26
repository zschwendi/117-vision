#include "../dual_input.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
int main(void) {
    HaloDualInputGate g={0};
    HaloDualInputSample s={.owner={1,1,0xa0001,0xb0002,0xc0003},.enabled=1,.spatial=1,.fresh=1,.gameplay_supported=1,.model_ready={1,1},.trigger={1,1}};
    HaloDualInputResult o=dual_input_update(&g,&s);
    CHECK(o.consume_left_trigger&&!o.fire[0]&&!o.fire[1]);
    s.trigger[0]=0;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    s.trigger[0]=1;o=dual_input_update(&g,&s);CHECK(o.fire[0]&&!o.fire[1]);
    s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[1]=1;o=dual_input_update(&g,&s);CHECK(o.fire[0]&&o.fire[1]);
    s.reload[0]=1;o=dual_input_update(&g,&s);CHECK(o.reload[0]&&!o.fire[0]&&o.fire[1]);s.reload[0]=0;
    /* A single lost model blocks only its hand, and reappearance cannot fire
     * the still-held trigger. The unaffected hand remains independent. */
    s.model_ready[0]=0;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&o.fire[1]&&o.consume_left_trigger);
    s.model_ready[0]=1;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&o.fire[1]);
    s.trigger[0]=0;dual_input_update(&g,&s);s.trigger[0]=1;o=dual_input_update(&g,&s);CHECK(o.fire[0]&&o.fire[1]);
    s.fresh=0;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]&&o.consume_left_trigger);
    s.fresh=1;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    /* Every ownership component is independently capable of disarming. */
    for(unsigned mutation=0;mutation<5;mutation++){
        s.trigger[0]=s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[0]=s.trigger[1]=1;
        o=dual_input_update(&g,&s);CHECK(o.fire[0]&&o.fire[1]);
        switch(mutation){case 0:s.owner.session++;break;case 1:s.owner.generation++;break;case 2:s.owner.unit++;break;case 3:s.owner.primary++;break;default:s.owner.offhand++;break;}
        o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    }
    s.trigger[0]=s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[0]=s.trigger[1]=1;
    s.gameplay_supported=0;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    s.gameplay_supported=1;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    const float bad[]={NAN,INFINITY,-INFINITY,-.01f,1.01f};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
        s.trigger[0]=s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[0]=bad[i];s.trigger[1]=1;
        o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&o.fire[1]);
        s.trigger[0]=1;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&o.fire[1]);
    }
    s.spatial=0;o=dual_input_update(&g,&s);CHECK(!o.consume_left_trigger&&!g.owns); /* conventional pad */
    s.spatial=1;o=dual_input_update(&g,&s);CHECK(o.consume_left_trigger&&!o.fire[0]&&!o.fire[1]);
    s.enabled=0;o=dual_input_update(&g,&s);CHECK(!o.consume_left_trigger&&!g.owns);
    s.enabled=1;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    s.owner.session=0;o=dual_input_update(&g,&s);CHECK(!o.consume_left_trigger&&!g.owns); /* window */
    s.owner.session=5;s.owner.primary=s.owner.offhand;o=dual_input_update(&g,&s);CHECK(o.consume_left_trigger&&!o.fire[0]&&!o.fire[1]);
    /* No supported offhand leaves the tracked primary usable after a
     * release. Joining/leaving a pair resets both ownership gates. */
    s.owner.primary=0xabc;s.owner.offhand=UINT32_MAX;s.trigger[0]=s.trigger[1]=1;
    o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    s.trigger[0]=s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[0]=s.trigger[1]=1;
    o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&o.fire[1]&&!g.armed[0]);
    s.owner.offhand=0xdef;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    s.trigger[0]=s.trigger[1]=0;dual_input_update(&g,&s);s.trigger[0]=s.trigger[1]=1;
    o=dual_input_update(&g,&s);CHECK(o.fire[0]&&o.fire[1]);
    s.owner.offhand=UINT32_MAX;o=dual_input_update(&g,&s);CHECK(!o.fire[0]&&!o.fire[1]);
    o=dual_input_update(&g,0);CHECK(!g.owns&&!o.consume_left_trigger);
    printf("PASS %u dual input ownership checks\n",checks);
}
