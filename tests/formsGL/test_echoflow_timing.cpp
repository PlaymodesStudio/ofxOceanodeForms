// Read-only audit of every active ECHOFLOW_MULTILAYER instrument and its stack children.
// clang++ -std=c++17 -O2 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_echoflow_timing.cpp -o /tmp/test_echoflow_timing
// Run on a copy of the Forms data folder.
#include "formsEngine.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>

using forms::json;
static std::vector<int> litColumns(const formsgl::RenderResult& image){
    std::vector<int> xs;
    for(int x=0;x<image.w;++x){
        bool lit=false;
        for(int y=0;y<image.h && !lit;++y){
            size_t i=((size_t)y*image.w+x)*4;
            lit=image.rgba[i+3]>0 && (image.rgba[i] || image.rgba[i+1] || image.rgba[i+2]);
        }
        if(lit) xs.push_back(x);
    }
    return xs;
}
int main(int argc,char** argv){
    assert(argc==2);
    forms::Engine engine; engine.rendererFolder=argv[1]; engine.loadAll();
    const auto& scene=engine.scenes.at("ECHOFLOW_MULTILAYER");
    formsgl::Schemas schemas; assert(schemas.loadEmbedded());
    std::set<std::string> seen;
    int instruments=0,items=0;
    for(size_t layer=0;layer<scene.layers.size();++layer){
        const auto& l=scene.layers[layer]; engine.itemBars=l.bars;
        for(int c=0;c<std::min<int>(l.rows,l.cells.size());++c){
            const auto& cell=l.cells[c];
            if(cell.item.empty() || cell.prob<=0 || !seen.insert(cell.item).second) continue;
            auto found=engine.presets.find(cell.item); assert(found!=engine.presets.end());
            auto resolved=engine.presetItemsSeeded(found->second,123);
            json request={{"size",{1920,1080}},{"bars",l.bars},{"seed",123},{"layer",layer+1},
                {"scaleNotes",{0,1,5,7,10}},{"items",resolved}};
            auto result=formsgl::Sketch(schemas).render(request);
            assert(result.error.empty());
            for(auto& item:result.itemResults) assert(!item.contains("error"));
            std::printf("L%zu %2d bars  %-30s %zu resolved items, %d ms\n",layer+1,l.bars,cell.item.c_str(),resolved.size(),result.renderMs);
            ++instruments; items+=(int)resolved.size();
        }
    }
    // Isolate a sixteenth-note onset from all envelopes, LFOs and effects.
    json probe={{"gen","pulse"},{"params",{
        {"steps",16},{"patternSteps",16},{"fills",{1}},{"shift",{0,0}},
        {"length",{0.02,0.02}},{"partials",{2,2}},{"partialMode","all"},
        {"octave",2},{"envelope","saw"},{"envPower",0},{"lfoPower",0},
        {"hicut",0},{"locut",0},{"detune",0},{"thickness",1},{"colour","white"}}}};
    for(int bars:{8,10,6,4}){
        json request={{"size",{1920,1080}},{"bars",bars},{"seed",123},{"items",json::array({probe})}};
        auto result=formsgl::Sketch(schemas).render(request); assert(result.error.empty());
        auto cols=litColumns(result); assert(!cols.empty());
        double maxError=0;
        int previous=-2;
        for(int x:cols){
            bool onset=x>previous+1; previous=x;
            if(!onset) continue;
            double bar=x*bars/1920.0;
            maxError=std::max(maxError,std::fabs(bar-std::round(bar))*4);
        }
        // One raster pixel, rather than a persistent fraction of a beat.
        assert(maxError<=bars*4.0/1920+1e-6);
        std::printf("Grid probe %2d bars: maximum onset raster error %.6f beats\n",bars,maxError);
    }
    // The existing echo preset intentionally allows continuous (fractional-step)
    // delays: demonstrate this independently of the scroller.
    probe["params"]["bars"]="first";
    json request={{"size",{1920,1080}},{"bars",8},{"seed",123},{"items",json::array({probe})}};
    auto dry=formsgl::Sketch(schemas).render(request);
    auto echo=engine.presetItemsSeeded(engine.presets.at("effects/echo full"),123);
    echo[0]["params"]["echoRepeats"]=1;
    request["items"].push_back(echo[0]);
    auto wet=formsgl::Sketch(schemas).render(request);
    auto before=litColumns(dry),after=litColumns(wet);
    assert(!before.empty() && after.size()>before.size());
    int delay=0;
    for(int x:after) if(x>before.back()){delay=x-before.front();break;}
    assert(delay>0);
    std::printf("Echo probe: %d px delay = %.6f sixteenths (continuous range [1,8], not integer choices)\n",delay,delay/15.0);
    std::printf("Audited %d unique active presets, %d resolved generator/process items\n",instruments,items);
}
