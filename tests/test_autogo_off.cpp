// Auto GO off, headless (the real Transport, Header, ScrollerCore, LayerRing and Engine, frame by
// frame as Forms Box drives them, with a fake renderer; as tests/test_next_prev_image.cpp):
//   layers  a multilayer scene loaded with Auto GO off: every layer must still be drawn many pages
//           later (they went away one by one once layer 1's plan ran out)
//   next    "Next image now": ONE new layer-1 image, which then stays (it gave 2 or 3 in a row, and
//           the pressed one was gone after a page or two)
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_autogo_off.cpp
// Run:   [OLD=1] [HARDCUT=1] [SCENE=name] ./a.out <data folder copy> [render latency s]
//        OLD=1: the caller asks for images ahead as with Auto GO on (the behaviour before the fix)
#include "formsEngine.h"
#include "formsClock.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include <cstdio>
#include <iostream>
using namespace forms;
int main(int argc, char** argv){
    std::string folder = argv[1];
    double latency = argc > 2 ? std::stod(argv[2]) : 0.3;
    const bool old = getenv("OLD") && atoi(getenv("OLD"));
    const std::string sceneName = getenv("SCENE") ? getenv("SCENE") : "TOMITA_MULTILAYER";
    const int W = 1920, H = 1080; const float dt = 1.0f / 60;
    Engine e; e.rendererFolder = folder; e.loadAll(); e.newSession(7);
    e.maxPending = 2; e.width = W; e.height = H;
    if(!e.scenes.count(sceneName)){ printf("no scene %s\n", sceneName.c_str()); return 2; }
    e.scenes.at(sceneName).performance = completePerformance(e.scenes.at(sceneName).performance);
    e.scenes.at(sceneName).performance["repeatChance"] = 0.0;
    e.useSetlist = false; e.manualScene = sceneName;
    if(getenv("HARDCUT") && atoi(getenv("HARDCUT"))) e.hardCutManual = true;   // the node's Hard cut
    const int nL = (int)e.scenes.at(sceneName).layers.size();
    Transport clock; Header head; head.rng.seed(7);
    clock.autoGo = false;
    ScrollerCore scroller; std::vector<LayerRing> rings(std::max(1, nL - 1)); scroller.keepAlpha = true;
    for(auto& r : rings){ r.hardCutIn = [&](double x, double y){ return e.hardCutIn(x, y); }; r.sceneAt = [&](double beat){ return e.sceneAtBeat(beat); }; }
    std::map<std::string, double> askedAt;
    std::shared_ptr<Image> lastSynced;
    for(int i = 0; i < 2; i++) e.makeRequest(true);   // the prime
    // presses: the scene is loaded (a switch on every layer), then Next image now at several places in the page
    struct Step { double t; bool sceneLoad; };
    std::vector<Step> script = {{0.4, true}, {42.0, false}, {100.3, false}, {163.9, false}, {221.0, false}, {290.0, false}};
    size_t nextStep = 0;
    struct Press { double t; std::string id; bool load; std::vector<std::string> after; };
    std::vector<Press> presses;
    std::vector<long> missing(nL, 0); std::vector<double> goneAt(nL, -1);
    int bad = 0; double lastB = 0; long frames = 0; long checked = 0; bool hadHeader = false;
    const double T = 360;
    for(double t = 0; t < T; t += dt, frames++){
        e.now = t;
        auto* L = &e.schedule->layers[0];
        if(L->header){ clock.bars = L->header->bars; clock.targetBpm = L->header->bpm; clock.perf = L->header->perf; head.bars = L->header->bars; head.perf = L->header->perf; }
        clock.step(t, dt);   // Auto GO off: no GOs, no layer requests
        head.width = W; head.phase = clock.phase; head.bpm = clock.bpm; head.header = 0.218182f;
        head.step(t, dt);
        scroller.defaultBars = barCount(clock.bars);
        scroller.update(L, clock.phase, head.headerPos, W, H);
        double B = scroller.headerBeat;
        e.schedule->headerBeat = B;
        if(L->header && L->header != lastSynced){ lastSynced = L->header; e.syncLayer1(L->header, scroller.headerStart); }
        const Scene* cs = e.sceneAtHeader();
        for(int i = 1; i < nL; i++){ rings[i - 1].active = cs && i < (int)cs->layers.size(); rings[i - 1].update(e.schedule->layer(i), B, head.headerPos, W, H); }
        e.trackPlayed();
        if(L->header && !presses.empty()){
            auto& a = presses.back().after;
            if(a.empty() || a.back() != L->header->id) a.push_back(L->header->id);
        }
        // every layer drawn, from 3 s after the scene was loaded
        if(t > 3.4){
            checked++;
            for(int i = 1; i < nL; i++) if(!rings[i - 1].current || rings[i - 1].lastDrawn.empty()){ missing[i]++; if(goneAt[i] < 0) goneAt[i] = B; }
        }
        if(frames > 60 && hadHeader && (B < lastB - 1e-6 || B - lastB > 0.5)){ bad++; printf("t %.2f beat jumps %.3f -> %.3f\n", t, lastB, B); }
        lastB = B; hadHeader = (bool)L->header;   // (before the first image the clock runs on nothing)
        if(nextStep < script.size() && t >= script[nextStep].t){
            auto st = script[nextStep++];
            int debt = 0; float c = clock.phase + head.headerPos / W;
            int ahead = old ? (c < 1 ? 3 : 2) : 1;
            auto paths = e.switchNow(std::round(scroller.headerStart / 4.0 * kBarGrid) / kBarGrid, debt, ahead, st.sceneLoad ? 0 : 1);
            std::string id = e.schedule->layers[0].replaceNext ? e.schedule->layers[0].replaceNext->id : "?";
            presses.push_back({t, id, st.sceneLoad, {}});
            printf("t %6.2f beat %7.2f %s (header %s of the view, %.0f%% into the page): %d requests, new layer-1 image %s\n", t, B,
                   st.sceneLoad ? "scene loaded" : "Next image now", c < 1 ? "left" : "right", 100 * (B - scroller.headerStart) / std::max(1e-9, scroller.headerLen), (int)paths.size(), id.c_str());
        }
        for(auto& [id, img] : e.pending) if(!askedAt.count(id)) askedAt[id] = t;
        std::vector<std::string> done;
        for(auto& [id, img] : e.pending) if(t - askedAt[id] >= latency - 1e-9) done.push_back(id);
        for(auto& id : done) e.onReady(id, "/fake/" + id + ".png", "/fake/" + id + ".json");
    }
    printf("\n");
    for(auto& p : presses){
        printf("%s at t %.1f -> layer 1 under the header until the next press:", p.load ? "scene loaded   " : "Next image now ", p.t);
        for(auto& id : p.after) printf(" %s", id.c_str());
        // the image playing at the press, then the new one, and nothing else
        std::vector<std::string> fresh;
        for(auto& id : p.after) if(std::atoi(id.c_str()) >= std::atoi(p.id.c_str())) fresh.push_back(id);
        bool ok = fresh.size() == 1 && fresh[0] == p.id && p.after.back() == p.id;
        printf("   %s\n", ok ? "ok" : "FAIL: one new image, which stays");
        if(!ok) bad++;
    }
    for(int i = 1; i < nL; i++){
        if(missing[i]){ bad++; printf("FAIL: layer %d not drawn in %ld of %ld frames (first at beat %.1f)\n", i + 1, missing[i], checked, goneAt[i]); }
        else printf("layer %d drawn in every frame\n", i + 1);
    }
    printf("%d failures\n", bad);
    return bad ? 1 : 0;
}
