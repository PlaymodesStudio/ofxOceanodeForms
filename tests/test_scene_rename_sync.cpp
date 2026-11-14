// clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I ../../libs/json/include tests/test_scene_rename_sync.cpp -o /tmp/test_scene_rename_sync
// Run with a disposable directory as the only argument.
#include "formsEngine.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include <cassert>
#include <cstdio>
using namespace forms;

int main(int argc, char** argv){
    assert(argc == 2);
    Engine e; e.rendererFolder = argv[1];
    Scene s; s.name = "ECHOFLOW"; s.root = 27; s.bpm = 95;
    s.layers.resize(4);
    const int bars[] = {8, 10, 6, 5};
    for(int i = 0; i < 4; ++i) s.layers[i].bars = bars[i];
    s.performance["scaleQuant"] = 0.2463;
    e.scenes[s.name] = s; e.saveScene(s);
    e.manualScene = s.name; e.useSetlist = false;
    e.setlist = {{s.name, 3, false}};
    e.newSession(7);
    e.makeRequest(true);
    auto im = e.lastImage;
    assert(im && im->scene == s.name);
    e.onReady(im->id, "test.png", "test.json");
    ScrollerCore scroller;
    scroller.immediate = true;
    scroller.update(&e.schedule->layers[0], 0, 250, 1000, 100);
    scroller.update(&e.schedule->layers[0], 0, 250, 1000, 100);
    scroller.immediate = false;
    assert(e.schedule->layers[0].header == im);
    // The loading slot belongs to the player, outside the engine's queues.
    auto held = std::make_shared<Image>(*im); held->id = "loading";
    e.schedule->layers[0].ready.push_back(held);
    scroller.update(&e.schedule->layers[0], 0, 250, 1000, 100);
    assert(e.schedule->layers[0].ready.empty());
    e.makeRequest(true);
    auto pending = e.lastImage;
    auto extra = std::make_shared<Image>(*im); extra->layer = 2;
    e.schedule->layer(1).slots.push_back({extra, 0, 40});
    e.schedule->layer(1).header = extra;
    auto harmony = e.harmonyBars;
    auto plan = e.layer1Plan;
    auto root = im->roots; auto trans = im->trans;
    bool notified = false;
    e.sceneRenamed = [&](const std::string& from, const std::string& to){
        notified = true;
        for(auto& page : scroller.heldImages()) if(page->scene == from) page->scene = to;
    };
    assert(e.renameScene("ECHOFLOW", "AAA renamed"));
    assert(notified && e.manualScene == "AAA renamed" && e.setlist[0].scene == "AAA renamed");
    assert(im->scene == "AAA renamed" && held->scene == "AAA renamed");
    assert(extra->scene == "AAA renamed" && pending->scene == "AAA renamed");
    assert(e.sceneAtHeader() && e.sceneAtHeader()->root == 27);
    assert(e.scenes.at("AAA renamed").performance == s.performance);
    assert(e.harmonyBars == harmony && im->roots == root && im->trans == trans);
    assert(e.layer1Plan.size() == plan.size());
    for(size_t i = 0; i < plan.size(); ++i){
        assert(e.layer1Plan[i].scene == "AAA renamed");
        assert(e.layer1Plan[i].startBar == plan[i].startBar);
    }
    assert(readJsonFile(e.scenePath(e.scenes.at("AAA renamed"))).at("root") == 27);
    assert(!fs::exists(e.scenePath(s)));
    // The opacity slider must continue controlling the current and loading pages
    // after a rename, instead of falling back to their baked-in gain of 1.
    for(float opacity : {0.0f, 0.27f, 1.0f}){
        e.scenes.at("AAA renamed").layers[0].gain = opacity;
        assert(e.imageOpacity(*im) == opacity && e.imageOpacity(*held) == opacity);
    }
    // Late pages remain on their original layer cycles, at every parallax length.
    for(int b : {10, 6, 5}){
        LayerRing ring; Schedule::Layer L;
        auto page = std::make_shared<Image>(); page->bars = b; page->startBeat = 0; page->png = "late.png";
        L.ready.push_back(page);
        ring.update(L, 5, 250, 1000, 100);
        assert(L.retimed == 1 && !L.slots.empty());
        assert(std::fabs(std::remainder(L.slots.front().start, b * 4.0)) < 1e-9);
        // Its left edge stays outside the view until it scrolls in.
        assert(ring.screenX(L, 5, L.slots.front().start, 250, 1000, b * 4.0) >= 1000);
    }
    // Fresh extra layers use the same scene origin as layer 1.
    e.pending.clear(); e.schedule->layer(1).slots.clear(); e.layerNextBar.clear();
    e.schedule->headerBeat = 0;
    assert(!e.requestLayers().empty());
    assert(e.lastImage->layer > 1 && e.lastImage->startBeat == 0);
    e.pending.clear(); e.layerNextBar.clear(); e.schedule->headerBeat = 5;
    assert(!e.requestLayers().empty());
    assert(std::fmod(e.lastImage->startBar, e.lastImage->bars) == 0);
    std::puts("Scene rename and multilayer cycle checks passed");
}
