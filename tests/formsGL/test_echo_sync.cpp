// clang++ -std=c++17 -O1 -I src/formsGL -I ../../libs/json/include tests/formsGL/test_echo_sync.cpp -o /tmp/forms-test-echo-sync
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace formsgl;
    for(float x=.25f;x<=32;x+=.0037f){float q=quantizedEchoSteps(x);assert(q>=.25 && q<=32);assert(q==.25f||q==.375f||q==.5f||q==.75f||q==1.5f||q==std::round(q));}
    assert(quantizedEchoSteps(1.47f)==1.5f);assert(quantizedEchoSteps(6.13f)==6);assert(quantizedEchoSteps(3.91f)==4);
    Schemas schemas;assert(schemas.loadEmbedded());
    for(int w:{1919,1920,3840,4096})for(float delay:{1.f,1.5f,2.27f,3.f}){
        json req={{"size",{w,32}},{"bars",1},{"seed",123},{"items",json::array({
            {{"gen","sweep"},{"params",{{"count",1},{"start",{0,0}},{"length",{1.f/w,1.f/w}},{"fromLow",{0,0}},{"fromHigh",{1,1}},{"toLow",{0,0}},{"toHigh",{1,1}},{"powW",0},{"powH",0},{"opStart",1},{"opEnd",1}}}},
            {{"gen","effects"},{"params",{{"steps",16},{"echoDelay",{delay,delay}},{"echoRepeats",8},{"echoFeedback",.8}}}}
        })}};
        req["items"][0]["clipBars"]={0,1.f/w};
        auto r=Sketch(schemas).render(req);assert(r.error.empty());
        std::vector<int> edges;bool prev=false;
        for(int x=0;x<w;++x){bool on=false;for(int y=0;y<32;++y)on|=r.rgba[((size_t)y*w+x)*4+3]>0;if(on&&!prev)edges.push_back(x);prev=on;}
        assert(edges.size()>3);assert(edges[0]==0);
        for(size_t k=1;k<edges.size();++k){int expected=pjRound(quantizedEchoSteps(delay)*w/16*k);if(edges[k]!=expected){std::printf("width %d delay %.2f repeat %zu at %d expected %d\n",w,delay,k,edges[k],expected);return 1;}}
    }
    std::puts("PASS: whole/dotted echo steps and rendered repeats without cumulative pixel-rounding drift at FHD, UHD, DCI and odd widths");
}
