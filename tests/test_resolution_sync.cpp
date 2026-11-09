// clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I ../../libs/json/include tests/test_resolution_sync.cpp -o /tmp/forms-test-resolution-sync
#include "formsClock.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include <cassert>
#include <cstdio>
using namespace forms;
int main(){
    Header reference, resized;
    reference.rng.seed(123); resized.rng.seed(123);
    reference.smooth=resized.smooth=.9f;
    reference.header=resized.header=.218182f;
    reference.perf=resized.perf={{"gateChance",1},{"gateMults",{4}},{"repeatChance",1},{"repeatMults",{2}},{"repeatBarStart",.5}};
    for(int frame=0;frame<600;++frame){
        reference.width=1920;
        resized.width=frame<80?1920:frame<200?3840:frame<320?4096:frame<440?1280:1919;
        reference.phase=resized.phase=std::fmod(frame/1800.f,1.f);
        reference.step(frame/60.,1.f/60);resized.step(frame/60.,1.f/60);
        if(std::fabs(reference.headerNorm-resized.headerNorm)>1e-5){
            std::printf("resize shifts playhead at frame %d: %.7f vs %.7f\n",frame,reference.headerNorm,resized.headerNorm);return 1;
        }
        assert(reference.gate==resized.gate);
        assert(std::fabs(reference.repeatPx/reference.width-resized.repeatPx/resized.width)<1e-5);
    }
    for(auto size:std::vector<std::pair<int,int>>{{1280,720},{1920,1080},{3840,2160},{4096,2160},{1919,1079}}){
        int w=size.first,h=size.second;float hp=w*.218182f;
        for(int bars:{4,3,2,1,8,5})for(double beat:{40.125,42.25,43.75,44.0,47.375}){
            LayerRing ring;Schedule::Layer layer;
            auto im=std::make_shared<Image>();im->bars=bars;im->png="grid";im->texture=std::make_shared<ofTexture>();
            Schedule::Layer::Slot slot;slot.img=im;slot.start=40;slot.len=bars*4.;layer.slots.push_back(slot);
            ring.update(layer,beat,hp,w,h);
            for(auto& d:ring.lastDrawn)if(d.start<=beat && beat<d.start+d.len){
                double at=(hp-d.x)/w*d.len+d.start;
                assert(std::fabs(at-beat)<1e-5);
            }
        }
    }
    std::puts("PASS: normalized playhead, gate/repeat and TOMITA layer lengths across resolutions and live resizing");
}
