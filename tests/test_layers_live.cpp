// Phase 3 check, headless: the real Transport, Header (with beat repeats), ScrollerCore (layer 1),
// LayerRing (layers 2..4) and Engine, frame by frame as Forms Box's runPlayer() drives them, with a
// fake renderer that answers after a delay. openFrameworks is stubbed (tests/stub): nothing is
// drawn, but every ring reports what it would draw and where (lastDrawn).
//
// Checks, per frame:
//   cut      after a hard cut bar, no layer still shows an image from before the cut
//   repeat   while a beat repeat sends the header back, every layer shows the image it showed at
//            that beat the first time (the layers repeat together)
//   hole     a layer of the scene at the header has an image under the header
//   pop      no image appears for the first time with its left edge inside the view
//   beat     layer 1's beat under the header only goes back by a beat repeat (< 2 bars)
// REPEAT=1: pages repeat (LayerDef::repeat: A layer 1 x2, layer 3 x3; B layer 1 x3, layer 2 x2),
// with Forms Box's GO pacing (one request per repeated page). Also checked:
//   pages    layer 1 shows each page's copies in order, then a new page; no fallback repeat
//            (the next page was rendered before the repetition ended)
//   chords   every extra-layer page has layer 1's chords at its bars (layer 1's repeated harmony)
//
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_layers_live.cpp
// Run:   ./a.out <data folder copy> [minutes] [render latency s] [seed]
#include "formsEngine.h"
#include "formsClock.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include "formsTransportFeed.h"
#include <cstdio>
#include <iostream>
using namespace forms;

static int L0repeats(Engine& e){ return e.schedule->layers[0].repeats; }

int main(int argc, char** argv){
    std::string folder = argv[1];
    double minutes = argc > 2 ? std::stod(argv[2]) : 6;
    double latency = argc > 3 ? std::stod(argv[3]) : 1.5;
    unsigned seed = argc > 4 ? (unsigned)std::stoul(argv[4]) : 7;
    const int W = 1920, H = 1080;
    const float dt = 1.0f / 60;

    Engine e; e.rendererFolder = folder; e.loadAll(); e.newSession(seed);
    e.maxPending = 2; e.width = W; e.height = H;
    // two parallax scenes with beat repeats always on
    Scene a = e.scenes.at("PARALLAX_TEST");
    a.name = "A"; a.layers[0].bars = 8; a.layers[1].bars = 6; a.layers[2].bars = 4;
    a.performance = completePerformance(a.performance);
    a.performance["repeatChance"] = 1.0; a.performance["repeatBarStart"] = 0.5;
    a.performance["repeatMults"] = json::array({0.5, 1, 2}); a.performance["repeatNum"] = json::array({3});
    Scene b = a; b.name = "B"; b.layers.resize(2); b.layers[0].bars = 4; b.layers[1].bars = 3;
    bool repeatOn = getenv("REPEAT") && *getenv("REPEAT");
    if(repeatOn){ a.layers[0].repeat = 2; a.layers[2].repeat = 3; b.layers[0].repeat = 3; b.layers[1].repeat = 2; }
    e.scenes["A"] = a; e.scenes["B"] = b;
    e.setlist = {{"A", 3, false}, {"B", 3, true}, {"A", 2, true}, {"B", 2, false}, {"A", 3, true}};
    e.useSetlist = true; e.setlistEntry = 0; e.imagesInEntry = 0;

    Transport clock; Header head; head.rng.seed(seed);
    ScrollerCore scroller; LayerRing rings[3];
    scroller.keepAlpha = true;
    // as Forms Box sets it (formsBox.h hardCutIn)
    auto hardCutIn = [&](double a, double b){ return e.hardCutIn(a, b); };
    // LANE=1: a scene lane (as Forms Box reads it ahead): bars 16..39 scene B with a hard cut,
    // 64..89 scene A, nothing elsewhere (the setlist plays)
    bool laneOn = getenv("LANE") && *getenv("LANE");
    auto laneAt = [](int bar, bool& cut) -> std::string {
        if(bar >= 16 && bar < 40){ cut = true; return "B"; }
        if(bar >= 64 && bar < 90){ cut = false; return "A"; }
        cut = false; return "";
    };
    if(laneOn) e.laneSceneAt = laneAt;
    TransportFeed feed;
    long feedJumps = 0, feedBack = 0, bpmFrames = 0; double bpmErr = 0, lastFeedBeat = -1;
    for(auto& r : rings){ r.hardCutIn = hardCutIn; r.sceneAt = [&](double beat){ return e.sceneAtBeat(beat); }; }
    std::map<std::string, double> askedAt;
    std::shared_ptr<Image> lastSynced;
    // Forms Box's request() / goTick(): a page repeated N times stands for N GOs
    int goDebt = 0; long l1Requests = 0;
    auto request = [&](bool force){
        std::string p = e.makeRequest(force);
        if(!p.empty()){ l1Requests++; goDebt += std::max(0, e.lastRequestRepeat - 1); }
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
    for(int i = 0; i < 2; i++) request(true);   // prime
    // pages / chords
    long pageBad = 0, l1Pages = 0, l1Copies = 0;
    std::shared_ptr<Image> prevL1;
    std::map<int, std::pair<int, int>> l1Chord;                 // absolute bar -> layer 1's (root, trans)
    std::map<std::string, std::shared_ptr<Image>> extraSeen;    // extra-layer pages (first copies) that reached the header
    long extraCopies = 0;

    // checks
    long jumpArrivals = 0;
    std::map<int, long> holeSince;
    long frames = 0, cutBad = 0, repeatBad = 0, repeatChecked = 0, holes = 0, pops = 0, beatBack = 0, repeatFrames = 0;
    double maxB = -1e9, firstImageAt[4] = {1e9, 1e9, 1e9, 1e9};
    std::map<std::pair<int, long>, std::string> firstPass;   // (layer, beat x 8) -> image id
    std::set<std::string> everDrawn[4];
    std::vector<std::string> notes;
    auto note = [&](const std::string& s){ if(notes.size() < 25) notes.push_back(s); };

    double t = 0, lastB = 0, maxLoad = 0;
    FILE* idTrace = getenv("IDTRACE") ? fopen(getenv("IDTRACE"), "w") : nullptr;
    for(; t < minutes * 60; t += dt, frames++){
        e.now = t;
        auto* L = &e.schedule->layers[0];
        if(L->header){ clock.bars = L->header->bars; clock.targetBpm = L->header->bpm; clock.perf = L->header->perf; head.bars = L->header->bars; head.perf = L->header->perf; }
        int g = clock.goCount;
        clock.step(t, dt);
        for(int i = g; i < clock.goCount; i++) goTick();
        head.width = W; head.phase = clock.phase; head.bpm = clock.bpm; head.header = 0.218182f;
        head.step(t, dt);
        scroller.defaultBars = std::max(1, clock.bars);
        scroller.update(L, clock.phase, head.headerPos, W, H);
        double B = scroller.headerBeat;
        e.schedule->headerBeat = B;
        if(L->header && L->header != lastSynced){
            lastSynced = L->header; e.syncLayer1(L->header, scroller.headerStart);
            // pages: copy k follows copy k-1 of the same page; copy 0 follows the previous page's last copy
            auto& h = L->header;
            l1Copies++;
            if(h->repeatIndex == 0) l1Pages++;
            if(prevL1 && t > 5){
                bool ok = h->repeatIndex == 0 ? true : (prevL1->id == h->id && prevL1->repeatIndex == h->repeatIndex - 1);
                if(!ok){ pageBad++; note("page: " + h->id + " copy " + std::to_string(h->repeatIndex) + " after " + prevL1->id + " copy " + std::to_string(prevL1->repeatIndex)); }
            }
            prevL1 = h;
            for(int k = 0; k < (int)h->roots.size(); k++) l1Chord[h->startBar + k] = {h->roots[k], h->trans[k]};
        }
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
        // Drive Transport: what Forms Box would report
        feed.step(scroller.headerBeat, scroller.headerLen, head.repeatPx, W, clock.bpm, false, dt);
        if(feed.jump) feedJumps++;
        if(feed.beat < lastFeedBeat - 1e-9) feedBack++;
        lastFeedBeat = feed.beat;
        if(getenv("FEEDTRACE") && frames % 600 == 0) printf("t %6.1f B %8.2f feed beat %8.2f bpm %7.2f clock bpm %7.2f speedOut %5.2f phase %.3f\n", t, B, feed.beat, feed.bpm, clock.bpm, clock.speedOut, clock.phase);
        if(t > 10 && clock.bpm > 0 && feed.playing){ bpmErr = std::max(bpmErr, std::fabs(feed.bpm / clock.bpm - 1)); bpmFrames++; }
        // the fake renderer
        for(auto& [id, img] : e.pending) if(!askedAt.count(id)){
            askedAt[id] = t;
            if(getenv("TRACE") && img->layer == 2 && img->startBeat > 100 && img->startBeat < 200)
                printf("t %6.1f B %6.1f  ask L2 %s start %5.0f cut %5.0f scene %s  (pending %d)\n", t, B, id.c_str(), img->startBeat, img->cutBeat, img->scene.c_str(), (int)e.pending.size());
        }
        std::vector<std::string> done;
        for(auto& [id, img] : e.pending) if(t - askedAt[id] >= latency) done.push_back(id);
        for(auto& id : done){
            auto img = e.pending[id];
            if(getenv("TRACE2") && img->layer == 2 && id == "0010") printf("t %6.1f B %6.1f lastB %6.1f ready %s start %.1f bars %d\n", t, B, lastB, id.c_str(), img->startBeat, img->bars);
            if(getenv("TRACE") && img->layer == 2 && img->startBeat > 100 && img->startBeat < 200)
                printf("t %6.1f B %6.1f  ready L2 %s start %5.0f%s\n", t, B, id.c_str(), img->startBeat, img->cancelled ? " (cancelled)" : "");
            e.onReady(id, "/fake/" + id + ".png", "/fake/" + id + ".json");
        }

        for(int li = 1; li < nLayers && li < 4; li++){
            auto& c = rings[li - 1].current;
            if(c && c->startBar >= 0){ if(c->repeatIndex == 0) extraSeen[c->id] = c; else extraCopies++; }
        }
        // ---- checks
        // the whole view jumps when a beat repeat starts or ends (the header moves by whole steps)
        bool jumped = std::fabs(B - lastB) > 0.1;
        if(B < lastB - 1e-6){
            if(lastB - B > 8.0 + 1e-6){ beatBack++; note("beat went back " + std::to_string(lastB - B) + " beats at t " + std::to_string(t)); }
        }
        lastB = B;
        bool repeating = B < maxB - 0.05;
        if(repeating) repeatFrames++;
        int bar = (int)std::floor(B / 4.0);
        const Engine::PlanEntry* pe = e.planAt(bar);
        // the latest hard cut at or before this bar
        const Engine::PlanEntry* cut = nullptr;
        for(auto& p : e.layer1Plan) if(p.hardCut && p.startBar <= bar && (!cut || p.startBar > cut->startBar)) cut = &p;
        for(int li = 0; li < 4; li++){
            std::shared_ptr<Image> cur = li == 0 ? L->header : (li < nLayers ? rings[li - 1].current : nullptr);
            bool sceneHas = pe && e.scenes.count(pe->scene) && li < (int)e.scenes.at(pe->scene).layers.size();
            if(cur) firstImageAt[li] = std::min(firstImageAt[li], t);
            if(li > 0 && cur && cut){
                double imgStart = cur->startBeat;
                // an image that started before the cut and is still under the header after it
                if(imgStart < cut->startBar * 4.0 - 1e-6 && B >= cut->startBar * 4.0 + 1e-6 && !repeating){
                    cutBad++; note("L" + std::to_string(li + 1) + " still shows " + cur->scene + " (start beat " + std::to_string(imgStart) + ") after the cut at bar " + std::to_string(cut->startBar));
                }
            }
            if(li > 0 && sceneHas && !cur && t > firstImageAt[li] + 30 && !repeating){
                holes++;
                // where: bars since the latest scene change, and what the layer has
                int since = 999; for(auto& p : e.layer1Plan) if(p.startBar <= bar && (pe ? p.scene != "" : true)) {}
                const Engine::PlanEntry* chg = nullptr; std::string prev;
                for(auto& p : e.layer1Plan){ if(p.startBar > bar) break; if(prev != "" && p.scene != prev) chg = &p; prev = p.scene; }
                if(chg) since = bar - chg->startBar;
                holeSince[std::min(since, 20)]++;
                if(holes % 120 == 1){
                    auto& Ls = e.schedule->layers[li];
                    std::string sl; for(auto& x : Ls.slots) sl += " [" + std::to_string((int)x.start) + "," + std::to_string((int)x.end()) + (x.repeat ? "r" : "") + "]";
                    int pend = 0; for(auto& [id, im] : e.pending) if(im->layer == li + 1) pend++;
                    note("hole L" + std::to_string(li + 1) + " beat " + std::to_string((int)B) + " bar " + std::to_string(bar) + " (" + std::to_string(since) + " bars after a change" + (chg && chg->hardCut ? ", hard cut" : "") + "), slots" + sl + ", pending " + std::to_string(pend));
                }
            }
            long key = (long)std::floor(B * 8);
            if(!repeating && cur) firstPass[{li, key}] = cur->id;
            else if(repeating && cur){
                auto it = firstPass.find({li, key});
                if(it != firstPass.end()){
                    repeatChecked++;
                    if(it->second != cur->id){ repeatBad++; note("repeat L" + std::to_string(li + 1) + " beat " + std::to_string(B) + ": " + cur->id + " instead of " + it->second); }
                }
            }
        }
        for(int li = 1; li < nLayers && li < 4; li++){
            for(auto& d : rings[li - 1].lastDrawn){
                if(getenv("TRACE2") && li == 1 && d.png == "/fake/0010.png" && everDrawn[li].count(d.png) == 0)
                    printf("t %6.1f B %6.1f lastB %6.1f first drawn x %.1f start %.1f len %.1f header %.1f\n", t, B, lastB, d.x, d.start, d.len, head.headerPos);
                if(everDrawn[li].insert(d.png).second && d.x < W - 12 && t > 20){
                    // arriving with the view's jump at the end of a repeat is the jump, not a pop-in:
                    // it is where it would have been without the repeat
                    if(jumped){ jumpArrivals++; continue; }
                    pops++; note("pop-in L" + std::to_string(li + 1) + " " + d.png + " at x " + std::to_string(d.x));
                }
            }
        }
        if(idTrace){   // IDTRACE=<file>: what each layer shows, every frame (to compare runs)
            fprintf(idTrace, "%ld %.4f", frames, B);
            for(int li = 0; li < 4; li++){ std::shared_ptr<Image> c = li == 0 ? L->header : (li < nLayers ? rings[li - 1].current : nullptr); fprintf(idTrace, " %s", c ? (c->id + "." + std::to_string(c->repeatIndex)).c_str() : "-"); }
            fprintf(idTrace, "\n");
        }
        maxB = std::max(maxB, B);
        if(t > 30) maxLoad = std::max(maxLoad, e.renderLoad());
    }
    std::cout << "simulated " << minutes << " min at 60 fps, render latency " << latency << " s, seed " << seed << "\n";
    std::cout << "layer-1 plan:";
    for(auto& p : e.layer1Plan) std::cout << " " << p.scene << "@" << p.startBar << (p.hardCut ? "(cut)" : "");
    std::cout << "\n";
    for(int i = 1; i < 4; i++){ auto& Li = e.schedule->layers[i]; std::cout << "layer " << i + 1 << ": shown " << Li.shown << ", repeated " << Li.repeats << ", moved on " << Li.retimed << ", images drawn " << everDrawn[i].size() << "\n"; }
    std::cout << "frames " << frames << ", in a beat repeat " << repeatFrames << " (" << (100.0 * repeatFrames / frames) << "%), repeat frames checked " << repeatChecked << "\n";
    std::cout << "images that came in with a repeat's jump of the whole view: " << jumpArrivals << "\n";
    std::cout << "transport feed: jumps " << feedJumps << " (1 = only the start), went back " << feedBack << ", tempo within " << (100 * bpmErr) << "% of Forms Box (during its tempo glides; the beat itself is sent every frame)\n";
    long laneBad = 0, laneImages = 0;
    if(laneOn){
        for(auto& p : e.layer1Plan){
            bool cut; std::string want = laneAt(p.startBar, cut);
            if(want.empty()) continue;
            laneImages++;
            if(p.scene != want){ laneBad++; std::cout << "  lane: image at bar " << p.startBar << " is " << p.scene << ", the lane says " << want << "\n"; }
        }
        std::cout << "scene lane: " << laneImages << " layer-1 images inside lane blocks, " << laneBad << " with another scene\n";
    }
    if(feedJumps > 1 || feedBack > 0 || bpmErr > 0.10 || laneBad) cutBad += 1000000;   // fail
    std::cout << "measured render time " << e.renderSeconds << " s, lead " << e.leadBeats() << " beats, renderer load " << maxLoad * 100 << "% (most)\n";
    long chordBad = 0, chordChecked = 0;
    for(auto& [id, im] : extraSeen){
        for(int k = 0; k < (int)im->roots.size(); k++){
            auto it = l1Chord.find(im->startBar + k);
            if(it == l1Chord.end()) continue;
            chordChecked++;
            if(it->second != std::make_pair(im->roots[k], im->trans[k])){
                chordBad++;
                if(chordBad < 6) note("chord: layer " + std::to_string(im->layer) + " page " + id + " bar " + std::to_string(im->startBar + k) + " plays " + std::to_string(im->roots[k]) + "/" + std::to_string(im->trans[k]) + ", layer 1 " + std::to_string(it->second.first) + "/" + std::to_string(it->second.second));
            }
        }
    }
    std::cout << "layer 1: " << l1Pages << " pages, " << l1Copies << " shown (copies), " << l1Requests << " requests, fallback repeats " << L0repeats(e) << "\n";
    std::cout << "chords: " << chordChecked << " extra-layer bars checked against layer 1, " << chordBad << " differ (" << extraSeen.size() << " extra pages, " << extraCopies << " frames on extra copies)\n";
    if(repeatOn && (pageBad || chordBad || L0repeats(e) > 2 || l1Requests > l1Pages + 3)) cutBad += 1000000;
    if(repeatOn) std::cout << "pages wrong " << pageBad << "\n";
    std::cout << "problems: cut " << cutBad << ", repeat " << repeatBad << ", hole " << holes << ", pop " << pops << ", beat " << beatBack << "\n";
    if(!holeSince.empty()){ std::cout << "hole frames by bars since the last scene change:"; for(auto& [k, v] : holeSince) std::cout << " " << k << ":" << v; std::cout << "\n"; }
    for(auto& n : notes) std::cout << "  " << n << "\n";
    return (cutBad || repeatBad || holes || pops || beatBack) ? 1 : 0;
}
