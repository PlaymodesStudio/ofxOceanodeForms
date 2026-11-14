// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_blocking_engine.cpp -o /tmp/test_blocking_engine
// Run with a writable copy of the Forms data folder. No live application data is changed.
// Blocking through the engine: the instrument option reaches the items (plain and layered
// instruments), the scene layer's switch reaches every request, and the renders carry no eraser.
#include "formsLiveEdit.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
using namespace forms;
int main(int argc,char**argv){
    assert(argc==2); Engine e; e.rendererFolder=argv[1]; e.loadAll();
    // the option is listed on the generators that can blank, right before "blank what is underneath"
    for(auto& g : e.gens){
        bool blanks = g.param("clearChance") != nullptr;
        assert((g.param("blocking") != nullptr) == blanks);
        if(!blanks) continue;
        auto it = std::find_if(g.params.begin(), g.params.end(), [](const ParamDef& p){ return p.key == "blocking"; });
        assert(it->type == "bool" && it->basic && (it + 1)->key == "clearChance" && it->group == (it + 1)->group);
        std::printf("generator %-6s: blocking listed in group '%s'\n", g.name.c_str(), it->group.c_str());
    }
    formsgl::Schemas schemas; assert(schemas.loadEmbedded());
    auto strip = [](json items){ for(auto& it : items) if(it.contains("params")) it["params"].erase("blocking"); return items; };
    auto masks = [&](json items, bool layerOn, int seed){
        json req={{"size",{240,136}},{"bars",8},{"seed",seed},{"layer",2},{"items",items}};
        if(!layerOn) req["blocking"]=false;
        auto r=formsgl::Sketch(schemas).render(req); assert(r.error.empty()); return (int)r.covers.size();
    };
    int checked=0;
    for(std::string ref : {"hits/blocking snares","pulse/trance bass block","stack/techno blocks","stack/blocked counter","stack/pickup blocks"}){
        assert(e.presets.count(ref));
        Preset on=e.presets.at(ref), off=on; off.params["blocking"]=false;
        int nOn=0, nOff=0, nLayer=0;
        for(int seed=1; seed<=80; seed++){
            json a=e.presetItemsSeeded(on,seed), b=e.presetItemsSeeded(off,seed);
            assert(a.size()==b.size() && !a.empty());
            for(auto& it : b) assert(it["params"].value("blocking",true)==false);     // reaches every item, also a stack's layers
            for(auto& it : a) assert(formscover::blocking(it["params"]));
            assert(strip(a)==strip(b));                                               // nothing else changes: same notes
            nOn+=masks(a,true,seed); nOff+=masks(b,true,seed); nLayer+=masks(a,false,seed);
        }
        std::printf("%-26s 80 images: %2d eraser masks with blocking on, %d off (instrument), %d off (layer)\n", ref.c_str(), nOn, nOff, nLayer);
        assert(nOn>0 && nOff==0 && nLayer==0);
        checked++;
    }
    // the production request path: the layer's switch travels with every request and with the preview
    e.useSetlist=false; e.manualScene=e.scenes.begin()->first;
    for(auto& kv : e.scenes) if(kv.first=="DUBTECH") e.manualScene=kv.first;
    e.newSession(321);
    json staged; e.requestSink=[&](const fs::path&,json j){ staged=std::move(j); };
    assert(!e.makeRequest(true).empty()); assert(!staged.contains("blocking"));
    Scene& sc=e.scenes[e.manualScene];
    // an Immediate-mode re-render picks the switch up, and drops it again
    json live=staged; sc.layers[0].blocking=false;
    assert(patchForScene(e,live,sc) && live["blocking"]==false);
    sc.layers[0].blocking=true; assert(patchForScene(e,live,sc) && !live.contains("blocking"));
    sc.layers[0].blocking=false;
    assert(!e.makeRequest(true).empty()); assert(staged.value("blocking",true)==false);
    staged["size"]={480,270}; auto r=formsgl::Sketch(schemas).render(staged); assert(r.error.empty());
    // saved with the scene only when off
    json sj=sc; assert(sj["layers"][0]["blocking"]==false);
    for(size_t i=1;i<sj["layers"].size();++i) assert(!sj["layers"][i].contains("blocking"));
    std::printf("PASS: blocking option on %d instruments (plain and layered), layer switch in requests (%s), Immediate patch, scene json\n", checked, e.manualScene.c_str());
}
