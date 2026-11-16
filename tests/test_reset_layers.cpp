// test_layers_live.cpp plus the node's "Reset Layers" (formsBox.h resetLayersNow, copied here):
// RESETEVERY=<s> resets every so often. Checked on the frame of each reset:
//   layer 1's image under the header is the same one, it starts at the header, on its grid line
//   every extra layer's page under the header is the same one, and starts at the header
//   no layer is asked to play ahead of the plan (rewind >= 0)
// Passes when every reset is exact, no layer is ever left empty (hole 0) and the beat never jumps far back.
// Without RESETEVERY it is test_layers_live.cpp. Report line "reset: n done, m wrong".
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_reset_layers.cpp
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
    // BARMULT=n: the node's Bar Multiplier (every layer's bars and phrase times n)
    if(getenv("BARMULT")) e.barMult = quantBars(atof(getenv("BARMULT")));
    if(getenv("BARADD")) e.barAdd = atoi(getenv("BARADD"));   // the node's Bar Add (whole bars after the multiplier)
    // two parallax scenes with beat repeats always on
    Scene a = e.scenes.at("PARALLAX_TEST");
    a.name = "A"; a.layers[0].bars = 8; a.layers[1].bars = 6; a.layers[2].bars = 4;
    a.performance = completePerformance(a.performance);
    a.performance["repeatChance"] = 1.0; a.performance["repeatBarStart"] = 0.5;
    a.performance["repeatMults"] = json::array({0.5, 1, 2}); a.performance["repeatNum"] = json::array({3});
    // Exercise the same player path with arbitrary scene layer counts, including hard cuts
    // from a large scene to a small one and back.
    const int layerCount = std::max(4, getenv("TEST_LAYERS") ? atoi(getenv("TEST_LAYERS")) : 4);
    if(layerCount>4){
        LayerDef extra=a.layers.back(); a.layers.resize(layerCount,extra);
        for(int i=3;i<layerCount;++i) a.layers[i].bars=4+2*(i%3);
    }
    e.schedule->layer(layerCount-1);
    Scene b = a; b.name = "B"; b.layers.resize(2); b.layers[0].bars = 4; b.layers[1].bars = 3;
    bool repeatOn = getenv("REPEAT") && *getenv("REPEAT");
    if(repeatOn){ a.layers[0].repeat = 2; a.layers[2].repeat = 3; b.layers[0].repeat = 3; b.layers[1].repeat = 2; }
    // FRAC=1: fractional layer lengths on the extra layers (A: 8 / 0.25 / 1.5, B: 4 / 0.5);
    // FRAC=2: layer 1 too (A: 2 / 0.25 / 1.5, B: 0.5 / 0.75)
    const char* frac = getenv("FRAC");
    if(frac && *frac){
        a.layers[1].bars = 0.25; a.layers[2].bars = 1.5; b.layers[1].bars = 0.5;
        if(atoi(frac) > 1){ a.layers[0].bars = 2; b.layers[0].bars = 0.5; b.layers[1].bars = 0.75; }
    }
    // PHRASE=1: phrases on the extra layers (A layer 2: 0.5-bar pages of a 4-bar phrase, layer 3:
    // 1.5 x 4 = 6; B layer 2: 0.25 x 8 = 2); PHRASE=2: layer 1 too (A: 0.5 x 8 = 4, B: 1 x 4)
    const char* phr = getenv("PHRASE");
    if(phr && *phr){
        a.layers[1].bars = 0.5; a.layers[1].phrase = 4; a.layers[2].bars = 1.5; a.layers[2].phrase = 6;
        b.layers[1].bars = 0.25; b.layers[1].phrase = 2;
        if(atoi(phr) > 1){ a.layers[0].bars = 0.5; a.layers[0].phrase = 4; b.layers[0].bars = 1; b.layers[0].phrase = 4; }
    }
    // WRITTEN=n: the same lengths written into the scenes instead (must play exactly like BARMULT=n)
    if(getenv("WRITTEN")) for(Scene* sc : {&a, &b}) for(auto& l : sc->layers){ double m = atof(getenv("WRITTEN")); int n = phraseSlices(l); l.bars = quantBars(l.bars * m + (getenv("WRITTENADD") ? atoi(getenv("WRITTENADD")) : 0)); if(l.phrase > 0) l.phrase = l.bars * n; }
    e.scenes["A"] = a; e.scenes["B"] = b;
    e.setlist = {{"A", 3, false}, {"B", 3, true}, {"A", 2, true}, {"B", 2, false}, {"A", 3, true}};
    e.useSetlist = true; e.setlistEntry = 0; e.imagesInEntry = 0;

    Transport clock; Header head; head.rng.seed(seed);
    ScrollerCore scroller; std::deque<LayerRing> rings(layerCount-1);
    head.beatAt=[&](float phase,float x){return scroller.beatAt(phase,x,W);};
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
    std::map<int, std::shared_ptr<Image>> lastPage;   // per extra layer: the page under the header last frame
    long phrasePages = 0, phraseBad = 0;

    // checks
    long jumpArrivals = 0;
    std::map<int, long> holeSince;
    long frames = 0, cutBad = 0, repeatBad = 0, repeatChecked = 0, holes = 0, pops = 0, beatBack = 0, repeatFrames = 0;
    double maxB = -1e9; std::vector<double> firstImageAt(layerCount,1e9);
    std::map<std::pair<int, long>, std::string> firstPass;   // (layer, beat x 8) -> image id
    std::vector<std::set<std::string>> everDrawn(layerCount);
    std::vector<std::string> notes;
    auto note = [&](const std::string& s){ if(notes.size() < 25) notes.push_back(s); };

    double t = 0, lastB = 0, maxLoad = 0;
    const double resetEvery = getenv("RESETEVERY") ? atof(getenv("RESETEVERY")) : 0;
    double nextReset = 20; int resetPending = 0; long resets = 0, resetBad = 0;
    std::vector<double> rewind; std::string rewindScene;
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
        scroller.defaultBars = barCount(clock.bars);
        // ---- Reset Layers, as formsBox.h resetLayersNow
        bool resetNow = false; double resetHB = 0; std::string l1Before; std::vector<std::string> pageBefore(layerCount);
        if(resetEvery > 0 && t >= nextReset) resetPending = 30, nextReset = t + resetEvery;
        if(resetPending > 0){
            resetPending--;
            if(!scroller.wrapsAt(clock.phase)){
                const double hb = scroller.beatAt(clock.phase, head.headerPos, W);
                const Scene* rs = e.sceneAtHeader();
                if(rs && rs->name != rewindScene){ rewindScene = rs->name; rewind.clear(); }
                std::vector<double> start(layerCount, -1), off(layerCount, 0);
                double need = -1e18;
                for(int i = 1; i < layerCount; i++){
                    const double Bi = hb - off[i] - (i < (int)rewind.size() ? rewind[i] : 0.0);
                    for(auto& sl : e.schedule->layers[i].slots) if(sl.start <= Bi && Bi < sl.end()){ start[i] = sl.start; if(sl.img) pageBefore[i] = sl.img->id + "#" + std::to_string(sl.img->slice); break; }
                    if(start[i] >= 0) need = std::max(need, start[i] + off[i]);
                }
                const double grid = std::min(4.0, std::max(4.0 / kBarGrid, (double)scroller.headerLen));
                double HB = std::floor(hb / grid + 1e-9) * grid;
                if(HB < need - 1e-6) HB = std::ceil(hb / grid - 1e-9) * grid;
                if(HB < need - 1e-6) HB = std::ceil(need / grid - 1e-9) * grid;
                if(L->header) l1Before = L->header->id + "#" + std::to_string(L->header->repeatIndex);
                const float np = scroller.restartHeaderImage(L, clock.phase, head.headerPos, W, HB);
                if(np >= 0){
                    clock.phase = np; head.phase = np;
                    rewind.assign(layerCount, 0.0);
                    for(int i = 1; i < layerCount; i++) if(start[i] >= 0) rewind[i] = HB - off[i] - start[i];
                    lastSynced.reset(); resetPending = 0; resetNow = true; resetHB = HB; resets++;
                    for(int i = 1; i < layerCount; i++) if(rewind[i] < -1e-6){ resetBad++; note("reset: layer " + std::to_string(i + 1) + " ahead of the plan by " + std::to_string(-rewind[i])); }
                }
            }
        }
        scroller.update(L, clock.phase, head.headerPos, W, H);
        double B = scroller.headerBeat;
        if(resetNow){
            if(std::fabs(scroller.headerStart - resetHB) > 1e-6 || std::fabs(B - resetHB) > 0.02){ resetBad++; note("reset: layer 1 at beat " + std::to_string(B) + " (image start " + std::to_string(scroller.headerStart) + "), wanted " + std::to_string(resetHB)); }
            std::string now1 = L->header ? L->header->id + "#" + std::to_string(L->header->repeatIndex) : "";
            // (before the first image reached the header there is nothing to compare)
            if(!l1Before.empty() && now1 != l1Before){ resetBad++; note("reset: layer 1 shows " + now1 + ", was " + l1Before); }
        }
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
            for(int k = 0; k < (int)h->roots.size(); k++) l1Chord[barIndex(h->startBar + k)] = {h->roots[k], h->trans[k]};
        }
        e.requestLayers();
        const Scene* cs = e.sceneAtHeader();
        int nLayers = 1;
        for(int i = 1; i < layerCount; i++){
            auto& Li = e.schedule->layers[i];
            if((cs && i < (int)cs->layers.size()) || !Li.slots.empty() || !Li.ready.empty()) nLayers = i + 1;
        }
        for(int i = 1; i < nLayers; i++){
            rings[i - 1].active = cs && i < (int)cs->layers.size();
            if(cs && cs->name != rewindScene){ rewindScene = cs->name; rewind.clear(); }
            const double Bi = B - (i < (int)rewind.size() ? rewind[i] : 0.0);
            rings[i - 1].update(e.schedule->layer(i), Bi, head.headerPos, W, H);
            if(resetNow && !pageBefore[i].empty()){
                const Schedule::Layer::Slot* cur = nullptr;
                for(auto& sl : e.schedule->layers[i].slots) if(sl.start <= Bi && Bi < sl.end()){ cur = &sl; break; }
                std::string nowPage = cur && cur->img ? cur->img->id + "#" + std::to_string(cur->img->slice) : "";
                if(!cur || std::fabs(Bi - cur->start) > 0.02 || nowPage != pageBefore[i]){ resetBad++; note("reset: layer " + std::to_string(i + 1) + " at " + std::to_string(cur ? Bi - cur->start : -1.0) + " beats into " + nowPage + ", was on " + pageBefore[i]); }
            }
        }
        // Drive Transport: what Forms Box would report
        // the beat under the header without the beat repeat, read across page edges (short pages)
        if(resetNow) feed.restart();
        feed.step(scroller.beatAt(clock.phase, head.headerPos + head.repeatPx, W), scroller.headerLen, 0, W, clock.bpm, false, dt);
        // (a reset is a seek, as in the node: the feed starts again there)
        if(feed.jump && !resetNow) feedJumps++;
        if(feed.beat < lastFeedBeat - 1e-9 && !resetNow) feedBack++;
        lastFeedBeat = feed.beat;
        if(getenv("FEEDTRACE") && frames % 600 == 0) printf("t %6.1f B %8.2f feed beat %8.2f bpm %7.2f clock bpm %7.2f speedOut %5.2f phase %.3f\n", t, B, feed.beat, feed.bpm, clock.bpm, clock.speedOut, clock.phase);
        if(t > 10 && clock.bpm > 0 && feed.playing){ bpmErr = std::max(bpmErr, std::fabs(feed.bpm / clock.bpm - 1)); bpmFrames++; if(getenv("TRACE_BPM") && std::fabs(feed.bpm / clock.bpm - 1) > 0.1) printf("t %.3f feed %.2f clock %.2f B %.3f hb %.3f rep %.1f len %.1f\n", t, feed.bpm, clock.bpm, B, feed.beat, head.repeatPx, scroller.headerLen); }
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
            if(getenv("TRACE2") && img->layer == 2 && id == "0010") printf("t %6.1f B %6.1f lastB %6.1f ready %s start %.1f bars %g\n", t, B, lastB, id.c_str(), img->startBeat, img->bars);
            if(getenv("TRACE") && img->layer == 2 && img->startBeat > 100 && img->startBeat < 200)
                printf("t %6.1f B %6.1f  ready L2 %s start %5.0f%s\n", t, B, id.c_str(), img->startBeat, img->cancelled ? " (cancelled)" : "");
            e.onReady(id, "/fake/" + id + ".png", "/fake/" + id + ".json");
        }

        for(int li = 1; li < nLayers; li++){
            auto& c = rings[li - 1].current;
            if(c && c->startBar >= 0){
                if(c->repeatIndex < std::max(1, c->slices)) extraSeen[c->id + (c->slices > 1 ? "#" + std::to_string(c->slice) : std::string())] = c; else extraCopies++;
                // a phrase's pages in order: after page k of a phrase comes page k+1 of it (or a new phrase)
                auto& last = lastPage[li];
                // (not while a beat repeat moves the header back, or the view jumps at its end)
                if(last && last != c && c->slices > 1 && std::fabs(B - lastB) < 0.1 && B >= maxB - 0.05){
                    phrasePages++;
                    if(last->id == c->id && c->slice != (last->slice + 1) % c->slices){ phraseBad++; note("phrase L" + std::to_string(li + 1) + " " + c->id + ": page " + std::to_string(c->slice) + " after " + std::to_string(last->slice)); }
                }
                last = c;
            }
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
        const Engine::PlanEntry* pe = e.planAt(B / 4.0);
        // the latest hard cut at or before this bar
        const Engine::PlanEntry* cut = nullptr;
        for(auto& p : e.layer1Plan) if(p.hardCut && p.startBar <= B / 4.0 + 1e-9 && (!cut || p.startBar > cut->startBar)) cut = &p;
        for(int li = 0; li < layerCount; li++){
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
                if(chg) since = (int)std::floor(bar - chg->startBar + 1e-9);
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
        for(int li = 1; li < nLayers; li++){
            for(auto& d : rings[li - 1].lastDrawn){
                if(getenv("TRACE2") && li == 1 && d.png == "/fake/0010.png" && everDrawn[li].count(d.png) == 0)
                    printf("t %6.1f B %6.1f lastB %6.1f first drawn x %.1f start %.1f len %.1f header %.1f\n", t, B, lastB, d.x, d.start, d.len, head.headerPos);
                // (a short page scrolls a view per beat or so: its first frame may be one frame's travel in)
                // (it moves with the page it follows: the shortest page drawn sets the speed)
                double shortest = d.len; for(auto& o : rings[li - 1].lastDrawn) shortest = std::min(shortest, o.len);
                double perFrame = W / std::max(1e-6, shortest) * clock.bpm / 60.0 * dt * 1.5;
                if(everDrawn[li].insert(d.png + (d.slice ? "#" + std::to_string(d.slice) : std::string())).second && d.x < W - 12 - perFrame && t > 20){
                    // arriving with the view's jump at the end of a repeat is the jump, not a pop-in:
                    // it is where it would have been without the repeat
                    if(jumped){ jumpArrivals++; continue; }
                    pops++; note("pop-in L" + std::to_string(li + 1) + " " + d.png + " at x " + std::to_string(d.x));
                    if(getenv("TRACE_POP")){ printf("pop L%d %s x %.1f start %.2f len %.2f t %.2f B %.3f hdr %.1f slots:", li + 1, d.png.c_str(), d.x, d.start, d.len, t, B, head.headerPos); for(auto& x : e.schedule->layers[li].slots) printf(" [%.2f,%.2f%s%s]", x.start, x.end(), x.repeat ? "r" : "", x.seen ? "s" : ""); printf("\n"); }
                }
            }
        }
        if(idTrace){   // IDTRACE=<file>: what each layer shows, every frame (to compare runs)
            fprintf(idTrace, "%ld %.4f", frames, B);
            for(int li = 0; li < layerCount; li++){ std::shared_ptr<Image> c = li == 0 ? L->header : (li < nLayers ? rings[li - 1].current : nullptr); fprintf(idTrace, " %s", c ? (c->id + "." + std::to_string(c->repeatIndex)).c_str() : "-"); }
            fprintf(idTrace, "\n");
        }
        maxB = std::max(maxB, B);
        if(t > 30) maxLoad = std::max(maxLoad, e.renderLoad());
    }
    std::cout << "simulated " << minutes << " min at 60 fps, render latency " << latency << " s, seed " << seed << "\n";
    std::cout << "layer-1 plan:";
    for(auto& p : e.layer1Plan) std::cout << " " << p.scene << "@" << p.startBar << (p.hardCut ? "(cut)" : "");
    std::cout << "\n";
    for(int i = 1; i < layerCount; i++){ auto& Li = e.schedule->layers[i]; std::cout << "layer " << i + 1 << ": shown " << Li.shown << ", repeated " << Li.repeats << ", moved on " << Li.retimed << ", images drawn " << everDrawn[i].size() << "\n"; }
    std::cout << "frames " << frames << ", in a beat repeat " << repeatFrames << " (" << (100.0 * repeatFrames / frames) << "%), repeat frames checked " << repeatChecked << "\n";
    std::cout << "images that came in with a repeat's jump of the whole view: " << jumpArrivals << "\n";
    std::cout << "transport feed: jumps " << feedJumps << " (1 = only the start), went back " << feedBack << ", tempo within " << (100 * bpmErr) << "% of Forms Box (during its tempo glides; the beat itself is sent every frame)\n";
    long laneBad = 0, laneImages = 0;
    if(laneOn){
        for(auto& p : e.layer1Plan){
            bool cut; std::string want = laneAt(barIndex(p.startBar), cut);
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
            auto it = l1Chord.find(barIndex(im->startBar + k));
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
    {
        int inFlight = (int)e.schedule->layers[0].ready.size() > 0 ? 1 : 0;
        for(auto& [id, im] : e.pending) if(im->layer <= 1 && !im->preview) inFlight++;
        if(getenv("PHRASE")) std::cout << "layer 1 requests not shown yet at the end (rendering or queued): " << inFlight << "\n";
    }
    // (a layer-1 phrase is asked for before the one playing ends: one more may be in flight at the end)
    const int l1Slack = 3 + (phr && atoi(phr) > 1 ? 1 : 0);
    if(repeatOn && (pageBad || chordBad || L0repeats(e) > 2 || l1Requests > l1Pages + l1Slack)) cutBad += 1000000;
    if(repeatOn) std::cout << "pages wrong " << pageBad << "\n";
    if(phr && *phr){
        std::map<int, long> req;
        for(auto& [id, at] : askedAt) (void)id;
        std::cout << "phrases: " << phrasePages << " page changes inside phrases checked, " << phraseBad << " out of order\n";
        if(phraseBad) cutBad += 1000000;
    }
    std::cout << "reset: " << resets << " done, " << resetBad << " wrong\n";
    std::cout << "problems: cut " << cutBad << ", repeat " << repeatBad << ", hole " << holes << ", pop " << pops << ", beat " << beatBack << "\n";
    if(!holeSince.empty()){ std::cout << "hole frames by bars since the last scene change:"; for(auto& [k, v] : holeSince) std::cout << " " << k << ":" << v; std::cout << "\n"; }
    for(auto& n : notes) std::cout << "  " << n << "\n";
    if(getenv("TRACE_SYNC")) for(auto& l : e.syncLog) if(l.find("off its plan") != std::string::npos || l.find("cut") != std::string::npos) std::cout << "sync: " << l << "\n";
    // With resets the beat goes back (or on to the next bar line) and the extra layers play behind the
    // plan on purpose, so "cut", "repeat" and "pop" (which compare against the plan's beats) do not
    // apply: what must hold is that every reset was exact, no layer is left empty and no long jump back.
    if(resetEvery > 0) return (resetBad || holes || beatBack) ? 1 : 0;
    return (cutBad || repeatBad || holes || pops || beatBack) ? 1 : 0;
}
