// Headless integration check: different layer bar counts, a shared frame clock,
// real Forms SynthDefs -> float WAV -> MP4 with embedded AAC.
#include "formsGLExportMedia.h"
#include <cassert>
#include <iostream>
#include <thread>
int main(int argc, char** argv){
    using namespace forms::exportmedia;
    assert((videoSize(1920,1080)==std::make_pair(1920,1080)));
    assert((videoSize(4096,4096)==std::make_pair(1080,1080)));
    assert((videoSize(1080,1920)==std::make_pair(608,1080)));
    assert((videoSize(64,64)==std::make_pair(64,64)));
    assert(loopBars({4,6,8})==24); assert(loopBars({7,5})==35);
    assert(quote("a'b")=="'a'\\''b'");
    if(argc<3) return 0;
    std::string base=argv[1];
    Layer l; l.w=64; l.h=64;
    l.recipe={{"size",{64,64}},{"bars",1},{"notegrid",12},{"trans",{0}},{"scaleNotes",{0,2,4,5,7,9,11}}};
    Layer second=l; second.recipe["bars"]=3; second.index=1;
    double duration=loopBars({1,3})*240.0/480;
    json j={{"duration",duration},{"bpm",480},{"bars",1},{"loopBars",3},{"size",{64,64}},{"performance",{{"reverbAmt",0.0}}},
        {"fx",{{"gain",1.0}}},{"master",{{"bcon",0.0}}}};
    forms::SoundSettings s; s.kickOn=false; s.lag=0; s.limit=.98f;
    VideoWriter writer(base,64,64,duration);
    for(int frame=0;frame<90;){
        if(!writer.available()){ std::this_thread::yield(); continue; }
        bool pulse=frame>=30 && frame<60;
        std::vector<uint8_t> rgba(64*64*4,255);
        // Visual and sonic pulse both begin at frame 30 (0.5 s).
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) for(int c=0;c<3;++c)
            rgba[(y*64+x)*4+c]=(x==16 || pulse) ? 255 : 0;
        writer.push(std::move(rgba));
        std::vector<float> amps(512,0); amps[64]=pulse ? 1.f : 0.f;
        l.scanAmps.insert(l.scanAmps.end(),amps.begin(),amps.end()); l.kickLevels.push_back(0);
        second.scanAmps.insert(second.scanAmps.end(),512,0); second.kickLevels.push_back(0);
        ++frame;
    }
    writer.finish(); while(!writer.done()) std::this_thread::yield();
    assert(writer.result().empty());
    std::vector<Layer> layers{l,second};
    if(const char* count=getenv("TEST_AUDIO_LAYERS")){
        int n=std::max(2,atoi(count)); s.layers.resize(n);
        for(int i=2;i<n;++i){Layer extra=second;extra.index=i;layers.push_back(std::move(extra));}
    }
    std::cout<<render(j,layers,s,base,argv[2],"",true,true)<<std::endl;
}
