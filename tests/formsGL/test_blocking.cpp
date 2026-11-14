// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_blocking.cpp -o /tmp/test_blocking
// The "blocking" switch (instrument param, stack, scene layer -> request): off = the instrument's
// black background ("blank what is underneath") is not drawn: no blank in its own image, no eraser
// mask for the layers below, and the figures themselves are unchanged.
#include "formsEngine.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
#include <fstream>
using forms::json;
static json presetParams(const std::string& dir, const std::string& rel){
    std::ifstream in(dir + "/" + rel); assert(in); json j; in >> j; return j;
}
static size_t lit(const std::vector<uint16_t>& rgba){ size_t n=0; for(size_t q=3;q<rgba.size();q+=4) if(rgba[q]) n++; return n; }
int main(int argc, char** argv){
    assert(argc > 1);   // the Forms/presets folder
    formsgl::Schemas schemas; assert(schemas.loadEmbedded());
    // a base that fills the image, then the blocker, in ONE layer
    json base={{"gen","hits"},{"params",{{"steps",16},{"patternSteps",1},{"fills",{1}},{"cell","block"},{"blockPower",0},{"bandLow",0},{"bandHigh",1},{"length",{1,1}}}}};
    int withMask=0;
    for(std::string rel : {"hits/coin_snares.json","pulse/trance_bass_gate.json","hits/blocking_snares.json","pulse/trance_bass_block.json"}){
        json pr=presetParams(argv[1],rel);
        json params=pr["params"];
        // fixed values so the test does not depend on the engine's resolve; always fire
        for(auto& [k,v] : params.items()) if(v.is_object()){ if(v.contains("options")) v=v["options"][0]; else if(v.contains("min")) v=v["min"]; }
        if(params.contains("repChance")) params["repChance"]=100;
        if(params.contains("density")) params["density"]=100;
        params["clearChance"]=100;
        json item={{"gen",pr["gen"]},{"params",params}};
        for(int seed : {1,2,3}){
            json req={{"size",{480,270}},{"bars",4},{"seed",seed},{"layer",2},{"items",json::array({item})}};
            auto on=formsgl::Sketch(schemas).render(req); assert(on.error.empty());
            // 1. instrument switch: same figures, no mask
            json offReq=req; offReq["items"][0]["params"]["blocking"]=false;
            auto off=formsgl::Sketch(schemas).render(offReq); assert(off.error.empty());
            assert(on.covers.size()==1); withMask++;
            assert(off.covers.empty());
            assert(on.rgba==off.rgba);            // alone, the black background is invisible anyway: figures identical
            // 2. layer switch (request), instrument left on: same result
            json layerOff=req; layerOff["blocking"]=false;
            auto lo=formsgl::Sketch(schemas).render(layerOff);
            assert(lo.covers.empty() && lo.rgba==on.rgba);
            // 3. explicit true = default
            json t=req; t["blocking"]=true; t["items"][0]["params"]["blocking"]=true;
            auto tr=formsgl::Sketch(schemas).render(t); assert(tr.covers.size()==1 && tr.rgba==on.rgba);
            // 4. over a base in the same layer: on blanks the base, off keeps every base pixel
            json both=req; both["items"]=json::array({base,item});
            json baseOnly=req; baseOnly["items"]=json::array({base});
            auto b0=formsgl::Sketch(schemas).render(baseOnly), bOn=formsgl::Sketch(schemas).render(both);
            both["items"][1]["params"]["blocking"]=false;
            auto bOff=formsgl::Sketch(schemas).render(both);
            assert(b0.error.empty() && bOn.error.empty() && bOff.error.empty());
            size_t total=(size_t)480*270;
            assert(lit(b0.rgba)==total);
            assert(lit(bOn.rgba)<total);          // blocking on: holes in the base
            assert(lit(bOff.rgba)==total && bOff.covers.empty());   // off: the base is whole
            std::printf("%-28s seed %d: on blanks %5.1f%% of the base, off 0%%\n", rel.c_str(), seed, 100.0*(total-lit(bOn.rgba))/total);
        }
    }
    // scene layer json: only written when off; read back
    forms::LayerDef L; json j=L; assert(!j.contains("blocking"));
    L.blocking=false; j=L; assert(j["blocking"]==false);
    forms::LayerDef R=j.get<forms::LayerDef>(); assert(!R.blocking);
    assert(json(forms::LayerDef()).get<forms::LayerDef>().blocking);
    std::printf("PASS: blocking off removes the blank and the eraser mask (%d cases), figures unchanged; layer flag round-trips\n", withMask);
}
