// Run with a writable Forms data copy. No live application data is changed.
#include "formsLiveEdit.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
using namespace forms;
int main(int argc,char**argv){
    assert(argc==2);Engine e;e.rendererFolder=argv[1];e.loadAll();
    auto layer=e.scenes.at("ECHOFLOW_MULTILAYER").layers[0];layer.effectPreset="effects/noise filter";
    assert(json(layer).get<LayerDef>().effectPreset==layer.effectPreset);
    json plain={{"seed",123},{"bars",8},{"items",e.presetItemsSeeded(e.presets.at("hits/batucada"),123)}};
    auto req=plain;auto rng=e.rng;int oldBars=e.itemBars;e.applyLayerEffect(req,layer,123);assert(e.rng==rng && e.itemBars==oldBars);
    assert(req["items"].size()==plain["items"].size()+1);assert(req["items"].back()["layerEffect"]==true);
    for(size_t i=0;i<plain["items"].size();++i)assert(req["items"][i]==plain["items"][i]);
    auto once=req;e.applyLayerEffect(req,layer,123);assert(req==once);
    auto frozen=layer;frozen.fixed={{"items",req["items"]},{"layerEffectPreset",layer.effectPreset}};
    e.applyLayerEffect(req,frozen,456);assert(req==once); // preserve exact frozen effect draws

    ItemFilter filter;filter.solo.insert("hits/batucada");auto solo=filterItems(req,filter);assert(solo["items"].back()["layerEffect"]==true);
    auto before=layer;layer.cells.push_back({"hits/open hats",100,0});
    auto patched=patchSelection(e,req,before,layer,123,0);assert(patched["items"].back()["layerEffect"]==true);
    layer.effectPreset="effects/echo full";e.applyLayerEffect(req,layer,123);assert(req["items"].back()["preset"]=="effects/echo full");assert(req["items"].size()==once["items"].size());
    layer.effectPreset.clear();e.applyLayerEffect(req,layer,123);assert(req["items"]==plain["items"]);
    layer.effectPreset="hits/open hats";e.applyLayerEffect(req,layer,123);assert(req["items"]==plain["items"]); // reject drawing presets
    // Exercise the production request path, including selection, metadata and final ordering.
    e.useSetlist=false;e.manualScene="ECHOFLOW_MULTILAYER";e.scenes[e.manualScene].layers[0].effectPreset="effects/noise filter";e.newSession(321);
    json staged;e.requestSink=[&](const fs::path&,json j){staged=std::move(j);};assert(!e.makeRequest(true).empty());
    assert(staged["items"].back()["layerEffect"]==true && staged["layerEffectPreset"]=="effects/noise filter");
    formsgl::Schemas schemas;assert(schemas.loadEmbedded());staged["size"]={480,128};auto rendered=formsgl::Sketch(schemas).render(staged);assert(rendered.error.empty());
    std::puts("PASS: effect-only selection, scene serialization, actual request rendering, last-item ordering, solo, replacement/removal and independent RNG");
}
