// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_kick_quarters.cpp -o /tmp/test_kick_quarters
// Run on a copy of Forms data. Checks the exact kick-quarters preset and detector strip.
#include "formsEngine.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
struct GpuFallbackProbe:formsgl::GpuEffects {
    int calls=0;
    bool kickSweep(formsgl::Canvas&,float,float,float,float,int) override {++calls;return false;}
    bool mask(std::vector<uint32_t>&,int,int,const formsgl::SketchPRNG&,float,float,float,float,float,float,float,bool,float,float,bool) override {return false;}
};
int main(int argc,char** argv){
    assert(argc==2);forms::Engine engine;engine.rendererFolder=argv[1];engine.loadAll();engine.itemBars=2;
    formsgl::Schemas schemas;assert(schemas.loadEmbedded());
    forms::json request={{"size",{1920,1080}},{"bars",2},{"seed",123},{"layer",3},
        {"items",engine.presetItemsSeeded(engine.presets.at("hits/kick quarters"),123)}};
    auto image=formsgl::Sketch(schemas).render(request);assert(image.error.empty());
    assert(engine.findGen("hits")->param("level")->max==4);
    auto implicit=request;implicit["items"][0]["params"].erase("level");
    assert(formsgl::Sketch(schemas).render(implicit).rgba==image.rgba);
    auto energy=[](const formsgl::RenderResult& r){
        double sum=0;for(size_t i=0;i<r.rgba.size();i+=4)sum+=(r.rgba[i]+r.rgba[i+1]+r.rgba[i+2])*r.rgba[i+3]/255.;return sum;
    };
    auto boosted=request;boosted["items"][0]["params"]["level"]=2;
    auto bright=formsgl::Sketch(schemas).render(boosted);assert(bright.error.empty());
    assert(energy(bright)>energy(image)*1.25);
    GpuFallbackProbe probe;formsgl::Sketch fallback(schemas);fallback.gpu=&probe;fallback.gpuMinPixels=1;
    assert(fallback.render(boosted).rgba==bright.rgba && probe.calls==0);
    fallback.render(request);assert(probe.calls>0);
    auto maximum=request;maximum["items"][0]["params"]["level"]=4;
    auto brightest=formsgl::Sketch(schemas).render(maximum);
    assert(brightest.error.empty() && energy(brightest)>energy(bright)*1.25);
    boosted["items"][0]["params"]["level"]=0;
    assert(energy(formsgl::Sketch(schemas).render(boosted))==0);
    std::printf("Kick volume: default 1 unchanged; volume 2 gives %.3fx, volume 4 gives %.3fx pixel energy; volume 0 is silent/transparent\n",energy(bright)/energy(image),energy(brightest)/energy(image));
    int strip=std::lround(1080*100.f/3840);
    for(float threshold:{.01f,.1f,.25f,.5f,.75f}){
        bool previous=false;int hits=0;float minimum=1,maximum=0;
        for(int x=0;x<image.w;++x){
            double sum=0;
            for(int y=image.h-strip;y<image.h;++y){
                size_t i=((size_t)y*image.w+x)*4;
                sum+=(image.rgba[i]+image.rgba[i+1]+image.rgba[i+2])/(3*255.)*image.rgba[i+3]/255.;
            }
            float level=sum/strip;minimum=std::min(minimum,level);maximum=std::max(maximum,level);
            bool on=level>threshold;if(on&&!previous)++hits;previous=on;
        }
        std::printf("kick quarters layer 3, 2 bars: threshold %.2f, %d rising edges, strip range %.3f..%.3f\n",threshold,hits,minimum,maximum);
        if(threshold==.5f)assert(hits==8);
    }
}
