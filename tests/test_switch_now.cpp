// Switch now (Perform), headless: the real Transport, Header, ScrollerCore (layer 1), LayerRing
// (layers 2..4) and Engine, frame by frame as Forms Box's runPlayer() drives them, with a fake
// renderer answering in order after a short delay (Forms GL Box: tens of ms).
// Scripted scene changes: a scene pad (manual scene), a setlist jump, "Render next now" after the
// Next menu changed, and one more pad. With SWITCH=1 (Switch now on) each one calls
// Engine::switchNow as Forms Box's switchNowRun() does; otherwise only the selection changes
// (as before: the scene comes with the next images).
// Checks with Switch now:
//   gone     right after the switch no old image is ready and every pending one is cancelled
//   header   the new scene's image is under the header within 3 frames of its render, on layer 1
//            and on every extra layer of the new scene; a layer the scene lacks shows nothing
//   old      no image asked for before the switch is under the header after it (any layer)
//   beat     the beat under the header is continuous (no beat repeats in these scenes)
//   hole     every layer of the scene at the header has an image under it
//   flow     layer 1 scrolls on normally after (at most 1 fallback repeat per switch)
// Without: the same, except the old images play on (counted, not a failure) and the switch image
// is never placed at once.
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_switch_now.cpp
// Run:   [SWITCH=1] [REPEAT=1] ./a.out <data folder copy> [render latency s]
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
    bool sw = getenv("SWITCH") && *getenv("SWITCH");
    bool repeatOn = getenv("REPEAT") && *getenv("REPEAT");
    const int W = 1920, H = 1080;
    const float dt = 1.0f / 60;

    Engine e; e.rendererFolder = folder; e.loadAll(); e.newSession(7);
    e.maxPending = 2; e.width = W; e.height = H;
    Scene a = e.scenes.at("PARALLAX_TEST");
    a.name = "A"; a.layers[0].bars = 8; a.layers[1].bars = 6; a.layers[2].bars = 4;
    a.performance = completePerformance(a.performance);
    a.performance["repeatChance"] = 0.0;   // no beat repeats: the beat must be continuous
    Scene b = a; b.name = "B"; b.layers.resize(2); b.layers[0].bars = 4; b.layers[1].bars = 3;
    if(repeatOn){ a.layers[0].repeat = 2; a.layers[2].repeat = 3; b.layers[0].repeat = 3; b.layers[1].repeat = 2; }
    e.scenes["A"] = a; e.scenes["B"] = b;
    e.setlist = {{"A", 3, false}, {"B", 3, false}, {"A", 2, false}};
    e.useSetlist = true; e.setlistEntry = 0; e.imagesInEntry = 0;

    Transport clock; Header head; head.rng.seed(7);
    ScrollerCore scroller; LayerRing rings[3];
    scroller.keepAlpha = true;
    for(auto& r : rings){ r.hardCutIn = [&](double x, double y){ return e.hardCutIn(x, y); }; r.sceneAt = [&](double beat){ return e.sceneAtBeat(beat); }; }
    std::map<std::string, double> askedAt, readyAt;
    std::shared_ptr<Image> lastSynced;
    int goDebt = 0;
    auto request = [&](bool force){
        std::string p = e.makeRequest(force);
        if(!p.empty()) goDebt += std::max(0, e.lastRequestRepeat - 1);
    };
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

    // the script: (time, what)
    struct Step { double t; int kind; std::string scene; int entry; };   // 0 pad, 1 setlist jump, 2 Render next now
    std::vector<Step> script = {{40, 0, "B", 0}, {70, 1, "", 0}, {100, 2, "A", 0}, {130, 0, "B", 0}};
    size_t nextStep = 0;
    struct Watch { double at; int counterAt; std::string scene; std::map<int, std::string> ids; std::map<int, bool> done; int l0repeats; };
    std::vector<Watch> watches;
    long frames = 0, goneBad = 0, headerBad = 0, oldBad = 0, beatBad = 0, flowBad = 0, emptyBad = 0, holeBad = 0, oldShown = 0;
    double lastB = 0, fastest = 1e9, slowest = 0;
    std::vector<std::string> notes;
    auto note = [&](const std::string& s){ if(notes.size() < 30) notes.push_back(s); };
    auto idNum = [](const std::string& id){ return std::atoi(id.c_str()); };

    for(double t = 0; t < 160; t += dt, frames++){
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
        int nLayers = 1;
        for(int i = 1; i < 4; i++){
            auto& Li = e.schedule->layers[i];
            if((cs && i < (int)cs->layers.size()) || !Li.slots.empty() || !Li.ready.empty()) nLayers = i + 1;
        }
        for(int i = 1; i < nLayers && i < 4; i++){
            rings[i - 1].active = cs && i < (int)cs->layers.size();
            rings[i - 1].update(e.schedule->layer(i), B, head.headerPos, W, H);
        }

        // ---- the script (end of Forms Box's update, as switchNowRun)
        if(nextStep < script.size() && t >= script[nextStep].t){
            auto st = script[nextStep++];
            bool doSwitch = sw;
            if(st.kind == 0){ e.useSetlist = false; e.manualScene = st.scene; }
            else if(st.kind == 1){ e.useSetlist = true; e.setlistEntry = st.entry; e.imagesInEntry = 0; }
            else{
                e.useSetlist = false; e.manualScene = st.scene;
                // Render next now: another scene than the playing one switches, Switch now or not
                doSwitch = sw || (L->header && L->header->scene != st.scene);
                if(!doSwitch) request(true);
            }
            if(doSwitch){
                int before = e.counter, debt = 0;
                float c = clock.phase + head.headerPos / W;   // as Forms Box's switchNowRun()
                auto paths = e.switchNow((int)std::lround(scroller.headerStart / 4.0), debt, c < 1 ? 3 : 2);
                goDebt = debt;
                Watch w; w.at = t; w.counterAt = before; w.scene = e.lastImage ? "" : "";
                for(int i = 0; i < 4; i++) if(e.schedule->layers[i].replaceNext){ w.ids[i] = e.schedule->layers[i].replaceNext->id; w.done[i] = false; w.scene = e.schedule->layers[i].replaceNext->scene; }
                w.l0repeats = L->repeats;
                // gone: nothing old ready, every old pending cancelled
                for(auto& Ls : e.schedule->layers) for(auto& im : Ls.ready) if(idNum(im->id) < before){ goneBad++; note("old image " + im->id + " still ready"); }
                for(auto& [id, im] : e.pending) if(idNum(id) < before && !im->cancelled){ goneBad++; note("old pending " + id + " not cancelled"); }
                printf("t %6.2f switch (%s) to %s at bar %ld: %d requests, layers %d\n", t, st.kind == 0 ? "pad" : st.kind == 1 ? "setlist jump" : "Render next now", w.scene.c_str(), std::lround(scroller.headerStart / 4.0), (int)paths.size(), (int)w.ids.size());
                watches.push_back(w);
            }else printf("t %6.2f selection (%s), no switch\n", t, st.kind == 0 ? "pad" : st.kind == 1 ? "setlist jump" : "Render next now");
        }

        // ---- the fake renderer: in order, `latency` after asked
        for(auto& [id, img] : e.pending) if(!askedAt.count(id)) askedAt[id] = t;
        std::vector<std::string> done;
        for(auto& [id, img] : e.pending) if(t - askedAt[id] >= latency - 1e-9) done.push_back(id);
        for(auto& id : done){ readyAt[id] = t; e.onReady(id, "/fake/" + id + ".png", "/fake/" + id + ".json"); }

        if(getenv("TRACE") && t > atof(getenv("TRACE")) && t < atof(getenv("TRACE")) + 1.5 && frames % 6 == 0){
            int tl = getenv("TL") ? atoi(getenv("TL")) : 2; auto& Ls = e.schedule->layers[tl];
            printf("t %.2f B %.2f L%d cur %s slots", t, B, tl + 1, rings[tl - 1].current ? rings[tl - 1].current->id.c_str() : "-");
            for(auto& x : Ls.slots) printf(" [%s %.1f-%.1f%s%s]", x.img->id.c_str(), x.start, x.end(), x.repeat ? " r" : "", x.seen ? " s" : "");
            printf(" ready %d\n", (int)Ls.ready.size());
        }
        // ---- checks
        if(std::fabs(B - lastB) > 0.2 && t > 1){ beatBad++; note("beat jumped " + std::to_string(lastB) + " -> " + std::to_string(B) + " at t " + std::to_string(t)); }
        lastB = B;
        if(!watches.empty()){
            auto& w = watches.back();
            for(int li = 0; li < 4; li++){
                std::shared_ptr<Image> cur = li == 0 ? L->header : (li < nLayers ? rings[li - 1].current : nullptr);
                if(cur && idNum(cur->id) < w.counterAt && t > w.at + 0.5){
                    if(sw){ oldBad++; if(oldBad < 5) note("L" + std::to_string(li + 1) + " shows old " + cur->id + " at t " + std::to_string(t)); }
                }
                auto it = w.ids.find(li);
                if(it != w.ids.end() && !w.done[li] && readyAt.count(it->second)){
                    if(cur && cur->id == it->second){
                        w.done[li] = true;
                        int f = (int)std::lround((t - readyAt[it->second]) / dt);
                        fastest = std::min(fastest, (double)f); slowest = std::max(slowest, (double)f);
                    }else if(t - readyAt[it->second] > 3.5 * dt){ w.done[li] = true; headerBad++; note("L" + std::to_string(li + 1) + ": switch image " + it->second + " not under the header 3 frames after its render"); }
                }
                // hole: a layer of the scene at the header with nothing under it
                if(sw && li > 0 && t > w.at + 1 && !cur && cs && li < (int)cs->layers.size()){ holeBad++; if(holeBad < 3) note("hole L" + std::to_string(li + 1) + " at t " + std::to_string(t)); }
                // a layer the new scene lacks shows nothing once the switch is done
                if(sw && li > 0 && !w.ids.count(li) && t > w.at + 0.2 && cur && idNum(cur->id) < w.counterAt){ emptyBad++; }
            }
            if(sw && t > w.at + 20 && w.l0repeats >= 0){
                if(L->repeats - w.l0repeats > 1){ flowBad++; note("layer 1 fell back to repeats after the switch at t " + std::to_string(w.at)); }
                w.l0repeats = -1;
            }
        }
        if(!sw) for(auto& w : watches) (void)w;
    }
    // without Switch now, how many old images played after each selection (as before)
    std::cout << (sw ? "Switch now ON" : "Switch now OFF") << (repeatOn ? ", repeated pages" : "") << ", render latency " << latency << " s, " << frames << " frames\n";
    std::cout << "layer-1 plan:";
    for(auto& p : e.layer1Plan) std::cout << " " << p.scene << "@" << p.startBar;
    std::cout << "\n";
    if(sw) std::cout << "switch image under the header " << fastest << ".." << slowest << " frames after its render\n";
    std::cout << "layer 1: shown " << e.schedule->layers[0].shown << ", fallback repeats " << e.schedule->layers[0].repeats << "\n";
    std::cout << "problems: gone " << goneBad << ", header " << headerBad << ", old " << oldBad << ", empty " << emptyBad << ", hole " << holeBad << ", beat " << beatBad << ", flow " << flowBad << "\n";
    if(getenv("SYNCLOG")) for(auto& l : e.syncLog) std::cout << "  sync: " << l << "\n";
    for(auto& n : notes) std::cout << "  " << n << "\n";
    bool bad = goneBad || headerBad || oldBad || emptyBad || holeBad || beatBad || flowBad || (sw && watches.size() != script.size());
    return bad ? 1 : 0;
}
