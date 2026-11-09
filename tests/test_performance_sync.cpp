// clang++ -std=c++17 -O1 -I src/formsBox -I ../../libs/json/include tests/test_performance_sync.cpp -o /tmp/test_performance_sync
#include "formsClock.h"
#include "formsCore.h"
#include "formsSoundModel.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace forms;
    // Gate phase is the beat under the playhead, independent of wall time / Speed.
    for(float speed:{0.f,.5f,1.f,2.f})for(float mult:{.125f,1.f,4.f}){
        Header head;double beat=4.0;head.beatAt=[&](float,float){return beat;};
        head.perf={{"gateChance",1},{"gateMults",{mult}},{"repeatChance",0}};
        for(int frame=0;frame<400;++frame){
            beat=4+frame/600.0*speed;head.phase=(float)(beat/32);head.step(frame/60.,1.f/60);
            double ph=std::fmod((beat-4)*mult,1.0);
            assert(head.gate==(ph<.25?1.f:0.f));
        }
    }
    // TOMITA uses a 1-beat repeat (repeatMults [2]). At the same musical
    // positions, different frame rates, pauses and speeds must have identical offsets.
    for(float dt:{1.f/30,1.f/60,1.f/144}){
        Header head;head.width=1919;head.bars=8;head.header=.5f;
        double beat=24.1;
        head.beatAt=[&](float,float x){return beat+(x/head.width-.5)*32;};
        head.perf={{"repeatChance",1},{"repeatBarStart",.5},{"repeatMults",{2}},{"repeatNum",{4}},{"repeatPW",.75},{"gateChance",0}};
        for(int i=0;i<9;++i){
            beat=24.1+i*.25;head.phase=(float)((beat-16)/32);head.step(i*dt,dt);
            double offset=std::floor((beat-24)+1e-7);
            assert(std::fabs(head.repeatPx-offset*1919/32)<1e-4);
            float saved=head.repeatPx,op=head.repeatOpacity;
            for(int paused=0;paused<10;++paused)head.step(10+paused*dt,dt);
            assert(head.repeatPx==saved && head.repeatOpacity==op);
        }
    }
    // Live kick-layer selection is independent of layer order and supports >4 layers.
    std::vector<LayerDef> layers(12);assert(kickLayer(layers)==0);
    for(int i=0;i<12;++i){for(auto& l:layers)l.kick=false;layers[i].kick=true;assert(kickLayer(layers)==i);}
    SoundSettings sound;std::vector<float> pitch={27,48,84};
    auto full=imageDuckWeights(pitch,sound);assert(full==std::vector<float>(3,0));
    sound.duckFullBand=false;auto spectral=imageDuckWeights(pitch,sound);assert(spectral[0]>0 && spectral[2]==0);
    std::puts("PASS: beat-locked gate and repeats, pause/Speed/frame-rate invariance, live kick layers and duck modes");
}
