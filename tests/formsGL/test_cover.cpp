// Headless contract checks: cover silhouettes, seed stability, persistence and >4-layer planning.
// clang++ -std=c++17 -O1 -I src/formsGL -I src/formsBox -I ../../libs/json/include tests/formsGL/test_cover.cpp -o /tmp/forms-cover-test
#include "formsGLSketch.h"
#include "formsEngine.h"
#include "formsSoundModel.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv){
    using formsgl::json;
    assert(argc==2);
    formsgl::Schemas schemas; assert(schemas.loadEmbedded());
    json req={{"size",{320,180}},{"seed",123},{"bars",4},{"layer",2},
        {"items",json::array({{{"gen","pulse"},{"params",json::object()}}})}};
    auto plain=formsgl::Sketch(schemas).render(req); assert(plain.error.empty());
    auto withCover=req; auto& p=withCover["items"][0]["params"];
    p["coverEnable"]=true; p["coverDilate"]=4; p["coverLayers"]="all";
    auto covered=formsgl::Sketch(schemas).render(withCover);
    assert(covered.error.empty() && covered.rgba==plain.rgba && covered.covers.size()==1);
    auto& mask=covered.covers[0];
    int lit=0; for(auto a:*mask.alpha) lit+=a>0;
    assert(lit>0 && lit<mask.w*mask.h); // a silhouette, not the entire page
    // Colour can be dark: masks describe coverage rather than brightness.
    auto black=withCover; black["paletteColors"]={"000000"};
    auto ink=formsgl::Sketch(schemas).render(black); assert(ink.covers.size()==1);
    int darkLit=0;for(auto a:*ink.covers[0].alpha) darkLit+=a>0;assert(darkLit>0);
    formsgl::Canvas dst,src;dst.allocateLayer(2,2);src.allocateLayer(2,2);
    src.blendLayer(0,0,0,0,128);dst.drawLayer(src);
    assert((dst.px[0]>>24)==128 && dst.px[1]==0); // black shapes survive process-layer compositing

    auto multi=withCover; multi["items"].insert(multi["items"].begin(),{{"gen","masses"},{"params",json::object()}});
    auto combined=formsgl::Sketch(schemas).render(multi); assert(combined.covers.size()==1);
    int combinedLit=0; for(auto a:*combined.covers[0].alpha) combinedLit+=a>0;
    assert(combinedLit>0 && combinedLit<mask.w*mask.h);
    // A per-bar instrument's mask cannot leak into its other bars.
    auto clip=withCover; clip["items"][0]["clipBars"]={1,2};
    auto clipped=formsgl::Sketch(schemas).render(clip); assert(clipped.covers.size()==1);
    for(int y=0;y<180;++y) for(int x=0;x<320;++x) if(x<80 || x>=160) assert((*clipped.covers[0].alpha)[y*320+x]==0);
    auto zero=withCover; zero["items"][0]["gain"]=0;
    auto silent=formsgl::Sketch(schemas).render(zero);
    for(auto a:*silent.covers[0].alpha) assert(a==0);
    // Cover rendering must not consume random numbers used by following instruments.
    auto two=req; two["items"].push_back({{"gen","hits"},{"params",json::object()}});
    auto twoCover=two; twoCover["items"][0]["params"]["coverEnable"]=true;
    assert(formsgl::Sketch(schemas).render(two).rgba==formsgl::Sketch(schemas).render(twoCover).rgba);

    formscover::Settings settings; settings.enabled=true;
    settings.growRight=12; settings.growHeight=3;
    assert(formscover::read(formscover::write(settings))==settings);
    auto legacy=formscover::read(json{{"coverDilate",6}});
    assert(legacy.growRight==6 && legacy.growHeight==6);
    auto separate=formscover::read(json{{"coverDilate",0},{"coverGrowHeight",7}});
    assert(separate.growRight==0 && separate.growHeight==7);
    assert(formscover::affects(settings,8,7) && !formscover::affects(settings,8,8));
    settings.target="layerBelow"; assert(formscover::affects(settings,8,7) && !formscover::affects(settings,8,6));
    settings.target="all"; assert(formscover::affects(settings,8,10) && !formscover::affects(settings,8,8));
    settings.mode="black"; settings.release=2; settings.amount=.4;
    forms::LayerDef layer; layer.cover=formscover::write(settings);
    forms::json layerJson=layer; auto recalled=layerJson.get<forms::LayerDef>();
    assert(recalled.cover==layer.cover && formscover::read(recalled.cover).mode=="black");
    forms::SoundSettings sound; sound.layers.resize(12); sound.layers[11].pan=.75;
    assert(sound.layers[11].pan==.75);

    namespace fs=std::filesystem;
    fs::path folder=argv[1]; fs::create_directories(folder);
    {std::ofstream out(folder/"generators.json");out<<formsgl::generatorSchemasJson();}
    forms::Engine e; e.rendererFolder=folder; e.loadAll();
    auto gen=e.findGen("pulse"); assert(gen && gen->param("coverEnable") && gen->param("coverRelease"));
    assert(gen->param("coverGrowHeight") && gen->param("coverDilate")->label=="Grow right (px)");
    forms::Preset preset; preset.gen="pulse"; preset.name="test_cover"; preset.params={{"coverEnable",true},{"coverLayers","all"}};
    e.presets[preset.ref()]=preset;
    auto items=e.presetItemsSeeded(preset,42);
    assert(items.size()==1 && items[0]["params"]["coverEnable"]==true && items[0]["params"]["coverLayers"]=="all");
    preset.params["coverDilate"]=9;
    auto legacyItems=e.presetItemsSeeded(preset,42);
    assert(legacyItems[0]["params"]["coverGrowHeight"]==9);
    preset.params["coverGrowHeight"]=1;
    auto separateItems=e.presetItemsSeeded(preset,42);
    assert(separateItems[0]["params"]["coverDilate"]==9 && separateItems[0]["params"]["coverGrowHeight"]==1);
    forms::Preset stack; stack.gen="stack"; stack.name="cover_stack";
    stack.params={{"coverEnable",true},{"coverAmount",.5}};
    stack.layers=json::array({{{"preset",preset.ref()}},{{"gen","hits"},{"params",json::object()}}});
    auto stackItems=e.presetItemsSeeded(stack,42);
    assert(stackItems.size()==2);
    for(auto& item:stackItems){assert(item["params"]["coverEnable"]==true);assert(item["params"]["coverAmount"]==.5);}
    forms::Scene scene; scene.name="twelve_layers";
    scene.layers.assign(12,layer);
    for(auto& l:scene.layers){l.bars=4;l.cells={forms::Cell{preset.ref(),100,0}};}
    e.scenes[scene.name]=scene; e.manualScene=scene.name; e.useSetlist=false; e.maxPending=64;
    e.newSession(123); e.width=320; e.height=180;
    auto first=e.makeRequest(true); assert(!first.empty());
    auto header=e.lastImage; e.onReady(header->id,"first.png","first.json");
    e.schedule->layers[0].header=header;
    std::set<int> requested;
    for(int k=0;k<80 && requested.size()<11;++k){
        auto path=e.requestLayers(); if(path.empty()) break;
        auto image=e.lastImage; requested.insert(image->layer);
        e.onReady(image->id,"layer"+image->id+".png","layer"+image->id+".json");
    }
    assert(requested.size()==11 && requested.count(12));
    auto* stable=&e.schedule->layer(0); e.schedule->layer(127);
    assert(stable==&e.schedule->layer(0)); // growing cannot invalidate the player's layer pointer
    std::cout<<"PASS: isolated cover masks, clipping, silent gain, seed invariance, settings, 12-layer planning, stable schedule growth\n";
}
