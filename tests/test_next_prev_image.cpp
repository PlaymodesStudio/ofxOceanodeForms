// "Next image now" (with Switch now, on the scene that plays) and "Previous image", headless: the
// real Transport, Header, ScrollerCore, LayerRing and Engine, frame by frame as Forms Box drives
// them, with a fake renderer (as tests/test_switch_now.cpp).
//   next   the image that replaces the one playing stays for the rest of that page AND one whole
//          page (KEEP=0: the old behaviour, it is gone when the page ends)
//   prev   one press: layer 1's previous image is under the header within a few frames; a second
//          press: the one before; then new images come again; the beat never jumps
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_next_prev_image.cpp
// Run:   [KEEP=0] ./a.out <data folder copy> [render latency s]
#include "formsEngine.h"
#include "formsClock.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include <cstdio>
#include <iostream>
using namespace forms;
int main(int argc, char** argv){
    std::string folder = argv[1];
    double latency = argc > 2 ? std::stod(argv[2]) : 0.05;
    int keep = getenv("KEEP") ? atoi(getenv("KEEP")) : 1;
    const int W = 1920, H = 1080; const float dt = 1.0f / 60;
    Engine e; e.rendererFolder = folder; e.loadAll(); e.newSession(7);
    e.maxPending = 2; e.width = W; e.height = H;
    Scene a = e.scenes.at("PARALLAX_TEST");
    a.name = "A"; a.layers[0].bars = 8; a.layers[1].bars = 6; a.layers[2].bars = 4;
    a.performance = completePerformance(a.performance); a.performance["repeatChance"] = 0.0;
    e.scenes["A"] = a; e.useSetlist = false; e.manualScene = "A";
    Transport clock; Header head; head.rng.seed(7);
    ScrollerCore scroller; LayerRing rings[3]; scroller.keepAlpha = true;
    for(auto& r : rings){ r.hardCutIn = [&](double x, double y){ return e.hardCutIn(x, y); }; r.sceneAt = [&](double beat){ return e.sceneAtBeat(beat); }; }
    std::map<std::string, double> askedAt;
    std::shared_ptr<Image> lastSynced;
    int goDebt = 0;
    auto request = [&](bool force){ std::string p = e.makeRequest(force); if(!p.empty()) goDebt += std::max(0, e.lastRequestRepeat - 1); };
    auto goTick = [&](){
        if(goDebt > 0){
            bool queued = !e.schedule->layers[0].ready.empty();
            for(auto& [id, im] : e.pending) if(im->layer <= 1 && !im->preview) queued = true;
            if(queued){ goDebt--; return; }
            goDebt = 0;
        }
        request(false);
    };
    for(int i = 0; i < 2; i++) request(true);
    struct Step { double t; int kind; };   // 0 Next image now, 1 Previous image, 2 Previous while a switch is pending
    std::vector<Step> script = {{30, 0}, {83.7, 0}, {131.2, 0}, {176.9, 0}, {230, 1}, {231, 1}, {300, 1}, {350.2, 2}};   // pages are 20 s: each press has time to play out
    size_t nextStep = 0;
    std::map<std::string, double> beatsUnder;         // layer-1 image id -> beats it spent under the header
    std::vector<std::string> order;                   // layer-1 ids in the order they were under the header
    std::vector<std::string> switchIds;
    struct Want { double at; std::string id; bool done = false; int frames = 0; };
    std::vector<Want> wants;
    double recallBeats = 0; std::string recallId;      // the last recall: how long it then stays
    int bad = 0; double lastB = 0; long frames = 0;
    const double firstRecall = 230;
    std::string lastBeforeRecall;
    for(double t = 0; t < 420; t += dt, frames++){
        e.now = t;
        auto* L = &e.schedule->layers[0];
        if(L->header){ clock.bars = L->header->bars; clock.targetBpm = L->header->bpm; clock.perf = L->header->perf; head.bars = L->header->bars; head.perf = L->header->perf; }
        int g = clock.goCount;
        clock.step(t, dt);
        for(int i = g; i < clock.goCount; i++) goTick();
        head.width = W; head.phase = clock.phase; head.bpm = clock.bpm; head.header = 0.218182f;
        head.step(t, dt);
        scroller.defaultBars = barCount(clock.bars);
        scroller.update(L, clock.phase, head.headerPos, W, H);
        double B = scroller.headerBeat;
        e.schedule->headerBeat = B;
        if(L->header && L->header != lastSynced){ lastSynced = L->header; e.syncLayer1(L->header, scroller.headerStart); }
        e.requestLayers();
        const Scene* cs = e.sceneAtHeader();
        for(int i = 1; i < 3; i++){ rings[i - 1].active = cs && i < (int)cs->layers.size(); rings[i - 1].update(e.schedule->layer(i), B, head.headerPos, W, H); }
        e.trackPlayed();
        if(L->header){
            if(order.empty() || order.back() != L->header->id) order.push_back(L->header->id);
            if(frames > 0 && B > lastB && t < firstRecall) beatsUnder[L->header->id] += B - lastB;   // (before the recalls bring some back)
            if(t < firstRecall) lastBeforeRecall = L->header->id;
            if(frames > 0 && B > lastB && L->header->id == recallId) recallBeats += B - lastB;
        }
        if(frames > 60 && (B < lastB - 1e-6 || B - lastB > 0.5)){ bad++; printf("t %.2f beat jumps %.3f -> %.3f\n", t, lastB, B); }
        lastB = B;
        for(auto& w : wants) if(!w.done){ w.frames++; if(L->header && L->header->id == w.id){ w.done = true; printf("t %6.2f   previous image %s under the header after %d frames\n", t, w.id.c_str(), w.frames); } }
        if(nextStep < script.size() && t >= script[nextStep].t){
            auto st = script[nextStep++];
            if(st.kind == 0 || st.kind == 2){
                int debt = 0; float c = clock.phase + head.headerPos / W;
                auto paths = e.switchNow((int)std::lround(scroller.headerStart / 4.0), debt, c < 1 ? 3 : 2, keep);
                goDebt = debt;
                std::string id = e.schedule->layers[0].replaceNext ? e.schedule->layers[0].replaceNext->id : "?";
                switchIds.push_back(id);
                printf("t %6.2f Next image now (header %s of the view): %d requests, new image %s\n", t, c < 1 ? "left" : "right", (int)paths.size(), id.c_str());
                if(st.kind == 2){ bool ok = e.previousImage(); if(ok){ bad++; printf("  previous image accepted while a switch is pending\n"); } else printf("         Previous image while that switch is pending: refused (as it should)\n"); }
            }else{
                std::string cur = L->header ? L->header->id : "-";
                std::string expect = e.playedImages.size() >= 2 ? e.playedImages[e.playedImages.size() - 2]->id : "";
                bool ok = e.previousImage();
                printf("t %6.2f Previous image: playing %s, asked back %s%s\n", t, cur.c_str(), expect.c_str(), ok ? "" : "  (REFUSED)");
                if(!ok || expect.empty() || expect == cur) bad++; else wants.push_back({t, expect});
                recallId = expect; recallBeats = 0;
            }
        }
        for(auto& [id, img] : e.pending) if(!askedAt.count(id)) askedAt[id] = t;
        std::vector<std::string> done;
        for(auto& [id, img] : e.pending) if(t - askedAt[id] >= latency - 1e-9) done.push_back(id);
        for(auto& id : done) e.onReady(id, "/fake/" + id + ".png", "/fake/" + id + ".json");
    }
    printf("\nlayer 1, in order under the header:"); for(auto& id : order) printf(" %s", id.substr(id.size() > 4 ? id.size() - 4 : 0).c_str()); printf("\n");
    for(auto& id : switchIds){
        double b = beatsUnder[id];
        printf("Next image now -> image %s was under the header for %.1f beats (a page is 32)\n", id.c_str(), b);
        if(id == switchIds.back()) continue;   // (the last press only checks the refusal)
        if(keep > 0 && (b < 32 - 0.5 || b > 64 + 0.5)){ bad++; printf("  FAIL: it must play the rest of the page it replaces and one whole page\n"); }
    }
    for(auto& w : wants) if(!w.done || w.frames > 6){ bad++; printf("FAIL: previous image %s %s\n", w.id.c_str(), w.done ? "came late" : "never came"); }
    printf("the last recovered image (%s) then stayed %.1f beats\n", recallId.c_str(), recallBeats);
    // (one more page when the image that was waiting to come next had to be dropped: the next one is asked for at the wrap)
    if(recallBeats < 32 - 0.5 || recallBeats > 96 + 0.5){ bad++; printf("FAIL: a recovered image must play the rest of the page and one or two whole pages\n"); }
    // after the recalls new images came again
    { int fresh = 0; bool after = false; for(auto& id : order){ if(after && std::atoi(id.c_str()) > std::atoi(lastBeforeRecall.c_str())) fresh++; if(id == lastBeforeRecall) after = true; }
      printf("new images under the header after the recalls: %d\n", fresh);
      if(fresh < 3){ bad++; printf("FAIL: new images must come again after a recall\n"); } }
    printf("layer-1 fallback repeats %d, %d failures\n", e.schedule->layers[0].repeats, bad);
    return bad ? 1 : 0;
}
