// bench_v2.cpp — ESPECTRES v2 per-frame CPU cost, headless (notes/v2_performance.md)
// Simulates the Forms GL2 Box frame (formsGL2Box::update / renderFrame / visibleEvents) at 60 fps
// for 60 s on each stress scene and times, per frame:
//   A  engine.update (with the 4 layer views)               B  visible events + fnCache (visibleEvents)
//   K  kick scan of renderFrame (all stored events)          C  CPU side of GLRenderer::render (pack per
//      instance: animated values + 8 drawn samples per event, covers packed again)
//   D  CPU renderer at 1/4 size (480x270, sampled every 10th frame)
// plus prototypes of optimisations (same frames, results compared to C):
//   P1 drawn-sample memo per event (bake drawn modulation once per event)
//   P2 skip animated callbacks for instances with no animated routing
//   P3 P1+P2 + full instance cache while the layer zoom does not change (only x re-placed)
//   -newnode: the proposed node change (no fnCache rebuild: a persistent id -> Engine::modOf map,
//   visibleInto with a reused buffer; Event copies kept since the renderer takes std::vector<Event>)
// pack() calls the real drawgl::packEvent (v2DrawGL.h, headless part); the render() mirror follows
// GLRenderer::renderPass / applyCovers (cover events packed once and reused).
//   build: clang++ -std=c++17 -O2 -g -I src/formsV2 -I <of>/libs/json/include tests/v2/bench_v2.cpp -o bench
//   usage: bench [scene.json ...]   (default: tests/v2/scenes/stress/stress_{medium,dense,extreme}.json)
#include "v2Engine.h"
#include "v2Draw.h"
#define FORMS2_HEADLESS
#include "v2DrawGL.h"   // packEvent and Inst only (the GL class needs OF)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <map>
#include <set>
#include <algorithm>

using namespace forms2;
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t){ return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

using drawgl::Inst;
static long gDrawnCalls = 0, gAnimCalls = 0;

struct GpuMirror {
    int W = 1920, H = 1080;
    draw::Renderer ref;
    long packs = 0;
    bool pack(const Event& e, Inst& in){ return drawgl::packEvent(ref, H, e, in); }   // the real one (v2DrawGL.h)

    // the CPU side of GLRenderer::render: transposes, per-layer packing, cover groups packed again
    size_t render(const std::vector<Event>& events0, const draw::Frame& f, const std::vector<LayerDef>& layers, std::vector<Inst>& sink){
        ref.frame = f; ref.layers = layers; W = f.width; H = f.height;
        int nl = std::clamp((int)layers.size(), 1, 4);
        std::vector<const Event*> transposes;
        for(auto& e : events0) if(e.kind == EventKind::Effect && e.look == "transpose") transposes.push_back(&e);
        std::vector<Event> shifted;
        if(!transposes.empty()){
            shifted = events0;
            for(auto& e : shifted){
                if(e.kind == EventKind::Effect || !e.pitched) continue;
                for(auto t : transposes) if(t->layer == e.layer && e.start >= t->start && e.start < t->start + t->length)
                    e.degree += (int)std::lround(t->param("transposeDegrees", 2));
            }
        }
        const std::vector<Event>& events = transposes.empty() ? events0 : shifted;
        sink.clear();
        std::vector<int> coverOf(events.size(), -1); std::vector<Inst> coverPk;
        for(int L = 0; L < nl; L++){
            std::vector<Inst> maxI, addI;
            for(auto& e : events) if(std::clamp(e.layer, 0, nl - 1) == L && e.kind != EventKind::Effect && ref.visible(e)){
                Inst i; if(!packAny(e, i)) continue;
                if(e.param("coverEnable", 0) >= 0.5){ coverOf[(size_t)(&e - events.data())] = (int)coverPk.size(); coverPk.push_back(i); }
                bool add = e.look == "additive";
                (add ? addI : maxI).push_back(i);
            }
            sink.insert(sink.end(), maxI.begin(), maxI.end()); sink.insert(sink.end(), addI.begin(), addI.end());
        }
        std::map<std::pair<int, draw::CoverKey>, std::vector<Inst>> groups;
        for(size_t n = 0; n < events0.size(); n++){
            const Event& e = events0[n];
            if(e.kind == EventKind::Effect || e.param("coverEnable", 0) < 0.5 || !ref.visible(e)) continue;
            draw::CoverKey key{e.param("coverDilate", 2), e.param("coverFeather", 4), e.param("coverRelease", 0.25), e.param("coverAmount", 1),
                               (int)e.param("coverMode", 0), (int)e.param("coverLayers", 0)};
            auto& g = groups[{std::clamp(e.layer, 0, nl - 1), key}];
            if(coverOf[n] >= 0){ g.push_back(coverPk[(size_t)coverOf[n]]); continue; }   // packed once
            Inst i; if(packAny(e, i)) g.push_back(i);
        }
        size_t mainN = sink.size();
        coverGroups = groups.size(); coverInst = 0;
        for(auto& g : groups){ coverInst += g.second.size(); sink.insert(sink.end(), g.second.begin(), g.second.end()); }
        // GPU estimate: fragments = clipped quad width x its (tight) height; ALU ops per fragment by look
        // (rough counts read off eventFrag: line() ~18 ops per partial x voice, vnoise ~30, disc ~25).
        // Notes evaluate only the partials in reach of the row (about 2-3, plus the sub) since the
        // analytic lookup; cover masks are drawn at half resolution (1/4 of the fragments).
        frags = 0; fragOps = 0;
        for(size_t i = 0; i < sink.size(); i++){
            const Inst& in = sink[i];
            double w = std::max(0.0, std::min<double>(W, in.v[0][2]) - std::max<double>(0, in.v[0][0]));
            double hgt = std::max(0.0, std::min<double>(H, in.v[0][3]) - std::max<double>(0, in.v[0][1]));
            double n = w * hgt; int kind = (int)in.v[3][0];
            double ops = 40;
            if(i >= mainN){ ops = 15; n *= 0.25; }                     // mask-only coverage, half resolution
            else if(kind == 0) ops = 60 + (std::min(in.v[4][2] + 1.0, 3.0) + (in.v[3][3] > 0 ? 1 : 0)) * in.v[5][2] * 18;
            else if(kind == 1) ops = (in.v[3][1] == 7 ? 25 : (in.v[3][1] == 2 || in.v[3][1] == 4 ? 80 : 45));
            else if(kind == 2) ops = 30 + in.v[4][2] * 45;
            else if(kind == 3) ops = in.v[6][3] == 1 ? 200 : 120;
            frags += n; fragOps += n * ops;
        }
        return sink.size();
    }
    size_t coverGroups = 0, coverInst = 0;
    double frags = 0, fragOps = 0;
    // P3: instance cache (valid while the layer's px per beat and the event's animated-ness allow)
    bool useInstCache = false;
    std::function<bool(const Event&)> isStatic;    // instance has no animated routing
    struct Cached { Inst in; double ppbKey; float x0; };
    std::unordered_map<uint64_t, Cached> icache;
    long cacheHits = 0;
    bool packAny(const Event& e, Inst& in){
        packs++;
        if(!useInstCache || !isStatic || !isStatic(e)) return pack(e, in);
        LayerView v = ref.viewOf(e.layer);
        double depth = draw::eventDepth(e, ref.frame);
        double ppbKey = v.pxPerBeat * depth;
        auto it = icache.find(e.id);
        if(it != icache.end() && it->second.ppbKey == ppbKey){
            in = it->second.in;
            float x0 = (float)v.xOf(e.start, depth), dx = x0 - it->second.x0;
            in.v[0][0] += dx; in.v[0][2] += dx; in.v[1][0] = x0;   // whole quad moves rigidly with the scroll
            cacheHits++;
            return true;
        }
        if(!pack(e, in)) return false;
        icache[e.id] = {in, ppbKey, in.v[1][0]};
        return true;
    }
    void pruneCache(const std::vector<Event>& evs){
        if(icache.size() < evs.size() * 2 + 1024) return;
        std::unordered_map<uint64_t, Cached> keep;
        for(auto& e : evs){ auto it = icache.find(e.id); if(it != icache.end()) keep.insert(*it); }
        icache.swap(keep);
    }
};

// ---------------------------------------------------------------- the box, headless
struct Stat {
    std::vector<double> v;
    void add(double x){ v.push_back(x); }
    double mean() const { double s = 0; for(double x : v) s += x; return v.empty() ? 0 : s / v.size(); }
    double pct(double p) const { if(v.empty()) return 0; auto c = v; std::sort(c.begin(), c.end()); return c[std::min(c.size() - 1, (size_t)(p * c.size()))]; }
    double max() const { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
};

struct Box {
    Engine en;
    std::unordered_map<uint64_t, std::pair<DrawnFn, AnimatedFn>> fnCache;
    int W = 1920, H = 1080; double hx = 960;
    int mode = 0;            // 0 baseline, 1 P1, 2 P1+P2, 3 P1+P2+P3
    bool persistentFn = false;
    bool newNode = false;
    struct ModRef { std::shared_ptr<const EventMod> m; double org = 0; };
    std::unordered_map<uint64_t, ModRef> mods;
    std::vector<VisibleEvent> visBuf;
    // P1 memo: (id, dest, sample index, base) -> value
    struct MemoKey { uint64_t id; size_t dest; int ui;
        bool operator==(const MemoKey& o) const { return id == o.id && dest == o.dest && ui == o.ui; } };
    struct MemoHash { size_t operator()(const MemoKey& k) const { return std::hash<uint64_t>()(k.id * 1000003u ^ k.dest ^ (size_t)(k.ui * 7919)); } };
    std::unordered_map<MemoKey, double, MemoHash> memo;
    std::unordered_map<std::string, bool> animOf;
    bool instAnimated(const std::string& id){ auto f = animOf.find(id); if(f != animOf.end()) return f->second; bool a = en.hasAnimated(id); animOf[id] = a; return a; }

    std::vector<Event> visibleEvents(double beat){
        std::vector<Event> evs;
        std::vector<LayerView> views;
        for(int l = 0; l < 4; l++) views.push_back(en.viewFor(l, beat, W, H, hx));
        auto t0 = Clock::now();
        en.update(beat, views);
        tA = msSince(t0); t0 = Clock::now();
        std::unordered_map<uint64_t, std::pair<DrawnFn, AnimatedFn>> next;
        if(newNode){
            evs.reserve(4096);
            for(int l = 0; l < 4; l++){
                visBuf.clear();
                en.visibleInto(l, views[(size_t)l], visBuf);
                for(auto& ve : visBuf){
                    const StoredEvent& s = *ve.ev;
                    evs.push_back(s.e);
                    ModRef& r = mods[s.e.id];
                    r.m = en.modOf(s); r.org = 0;
                }
            }
            if(mods.size() > evs.size() * 2 + 1024){ std::unordered_map<uint64_t, ModRef> k; for(auto& e : evs){ auto f = mods.find(e.id); if(f != mods.end()) k.insert(*f); } mods.swap(k); }
            tB = msSince(t0);
            return evs;
        }
        for(int l = 0; l < 4; l++){
            for(auto& ve : en.visible(l, views[(size_t)l])){
                const StoredEvent& s = *ve.ev;
                Event e = s.e;
                evs.push_back(e);
                if(persistentFn){ if(!fnCache.count(e.id)) fnCache[e.id] = {en.drawnFn(s), en.animatedFn(s)}; continue; }
                auto f = fnCache.find(e.id);
                if(f != fnCache.end()) next[e.id] = f->second;
                else next[e.id] = {en.drawnFn(s), en.animatedFn(s)};
            }
        }
        if(!persistentFn) fnCache.swap(next);
        else if(fnCache.size() > evs.size() * 2 + 1024){ std::unordered_map<uint64_t, std::pair<DrawnFn, AnimatedFn>> k; for(auto& e : evs){ auto f = fnCache.find(e.id); if(f != fnCache.end()) k.insert(*f); } fnCache.swap(k); }
        tB = msSince(t0);
        return evs;
    }
    draw::Frame makeFrame(double beat, int w, int h, double headerX){
        draw::Frame f;
        f.width = w; f.height = h; f.headerX = headerX; f.nowBeat = beat;
        auto ha = en.harmonyAt(beat); if(ha.scale) f.scale = *ha.scale;
        if(newNode){
            f.mod.drawn = [this](const Event& e, const std::string& dest, double u, double base){
                gDrawnCalls++;
                auto it = mods.find(e.id);
                return it == mods.end() ? base : it->second.m->drawn(dest, base, u);
            };
            f.mod.animated = [this](const Event& e, const std::string& dest, Beat now, double base){
                gAnimCalls++;
                auto it = mods.find(e.id);
                return it == mods.end() ? base : it->second.m->animated(dest, base, now - it->second.org);
            };
            return f;
        }
        f.mod.drawn = [this](const Event& e, const std::string& dest, double u, double base){
            gDrawnCalls++;
            if(mode >= 1){
                // offset baked once per (event, dest, sample); approximate when the clamp bites
                MemoKey k{e.id, std::hash<std::string>()(dest), (int)std::lround(u * 7000)};
                auto m = memo.find(k); if(m != memo.end()) return base + m->second;
                auto it = fnCache.find(e.id);
                double r = it == fnCache.end() || !it->second.first ? base : it->second.first(dest, base, u);
                memo.emplace(k, r - base); return r;
            }
            auto it = fnCache.find(e.id);
            return it == fnCache.end() || !it->second.first ? base : it->second.first(dest, base, u);
        };
        f.mod.animated = [this](const Event& e, const std::string& dest, Beat now, double base){
            if(mode >= 2 && !instAnimated(e.instance)) return base;
            gAnimCalls++;
            auto it = fnCache.find(e.id);
            return it == fnCache.end() || !it->second.second ? base : it->second.second(dest, base, now);
        };
        return f;
    }
    double tA = 0, tB = 0;
};

struct Result {
    std::string name; int mode; Stat A, B, K, C, D, total, vis, inst, frag, ops, cg, fxp, own, ownT, fxOld, fxNew, fxBatched;
    long planned = 0; size_t maxStored = 0, storedBytes = 0; double drawnPerFrame = 0, animPerFrame = 0, hitRate = 0;
    size_t coverGroups = 0; double ppm = 0;
};

static size_t eventBytes(const Event& e){
    size_t b = sizeof(StoredEvent);
    auto str = [](const std::string& s){ return s.capacity() > 15 ? s.capacity() + 1 : 0; };
    b += str(e.instance) + str(e.look) + str(e.colourRole);
    for(auto& kv : e.p) b += 32 + sizeof(kv) + str(kv.first);   // rb-tree node header + pair
    return b;
}

static bool gNewNode = false;
static double gHorizon = 0;   // -horizon=N: EngineOptions::planHorizonBeats
static Result runScene(const json& j, const std::string& name, int mode, bool persistentFn, int frames, bool cpuRender){
    Result R; R.name = name; R.mode = mode;
    Box box; box.mode = mode; box.persistentFn = persistentFn; box.newNode = gNewNode && mode == 0;
    box.en.options.planHorizonBeats = gHorizon;
    if(!box.en.loadJson(j)){ std::printf("%s: load failed\n", name.c_str()); for(auto& p : box.en.problems()) std::printf("  %s\n", p.c_str()); return R; }
    GpuMirror gm; gm.useInstCache = mode >= 3;
    gm.isStatic = [&box](const Event& e){ return !box.instAnimated(e.instance); };
    draw::Renderer cpu;
    std::vector<Inst> sink;
    double bpm = box.en.scene().bpm;
    double prevBeat = -1e9;
    long d0 = gDrawnCalls, a0 = gAnimCalls;
    for(int fi = 0; fi < frames; fi++){
        double beat = fi / 60.0 * bpm / 60.0;
        auto tf = Clock::now();
        std::vector<LayerDef> layers = box.en.scene().layers; layers.resize(4);
        for(int l = 0; l < 4; l++) layers[(size_t)l].zoomBeats = box.en.layerZoom(l, beat);
        auto evs = box.visibleEvents(beat);
        // K: kick scan as renderFrame (every stored event from the start of each layer)
        auto t0 = Clock::now();
        bool kick = false;
        if(beat > prevBeat && beat - prevBeat < 4)
            for(int l = 0; l < 4 && !kick; l++)
                for(auto& s : box.en.events(l)){
                    if(s.e.start > beat) break;
                    if(s.e.start > prevBeat && s.e.kind == EventKind::Hit && (s.e.look == "kick" || s.e.look == "taiko") && !box.en.muted(s.e.instance)){ kick = true; break; }
                }
        double tK = msSince(t0);
        t0 = Clock::now();
        auto f = box.makeFrame(beat, box.W, box.H, box.hx);
        size_t ni = gm.render(evs, f, layers, sink);
        double tC = msSince(t0);
        R.total.add(msSince(tf));
        double tD = 0;
        if(cpuRender && fi % 10 == 0){
            t0 = Clock::now();
            cpu.frame = box.makeFrame(beat, 480, 270, 120); cpu.layers = layers;
            cpu.render(evs);
            tD = msSince(t0); R.D.add(tD);
        }
        {   // GPU passes of renderPass: one full-height fx pass per visible bus / earlier / scoped effect
            // event, plus one FBO group (clear, draw, composite) per instance targeted by a scoped effect
            auto t1 = Clock::now();
            draw::Renderer vr; vr.frame = f; vr.layers = layers;
            std::vector<const Event*> scoped; size_t fxp = 0; double fxo = 0, fxn = 0;
            for(auto& e : evs) if(e.kind == EventKind::Effect && e.look != "transpose" && vr.visible(e)){
                int sc = blob::scopeOf(e);
                if(sc == 1) scoped.push_back(&e);
                if(sc != 2 || !blob::isCopyEffect(e)) fxp++;
                // pixel cost in full-screen passes: before every pass was full-screen; now a bus /
                // earlier pass on a narrow slot copies and redraws only its columns (applyEffect)
                if(sc == 2 && blob::isCopyEffect(e)) continue;
                fxo += 1;
                int c0 = 0, c1 = f.width; LayerView lv = vr.viewOf(e.layer);
                bool ok = drawgl::fxColumns(e, lv, (float)lv.xOf(e.start), (float)lv.xOf(e.start + e.length), f.width, c0, c1);
                if(sc == 1 || e.look == "blob" || !ok || c1 - c0 > f.width / 2) fxn += 1; else fxn += 2.0 * (c1 - c0) / f.width;
            }
            std::set<std::string> own;
            for(auto& e : evs){ if(e.kind == EventKind::Effect) continue; for(auto f2 : scoped) if(blob::targets(*f2, e)){ own.insert(e.instance); break; } }
            // batched passes (GLRenderer::fxQueue / fxFlush): per layer, the "earlier" effects in draw order
            // with a flush wherever instruments are drawn between them, then the bus effects; a slot joins
            // the open batch unless it overlaps an earlier slot (gains with gains and echoes with echoes may)
            double batched = 0;
            {
                struct Sl { int c0, c1, mode; bool gain; };
                std::vector<Sl> batch; int bl = -1;
                auto flush = [&](){ if(!batch.empty()) batched += 1; batch.clear(); bl = -1; };
                auto queue = [&](int L, const Event& e){
                    if(e.look == "blob"){ flush(); batched += 1; return; }
                    LayerView lv = vr.viewOf(L); int mode = 0; float pp[4], perm[32];
                    if(!drawgl::packEffect(e, lv, mode, pp, perm)) return;
                    Sl s; s.mode = mode; s.gain = mode == 4 || (mode >= 10 && mode <= 17);
                    if(!drawgl::fxColumns(e, lv, (float)lv.xOf(e.start), (float)lv.xOf(e.start + e.length), f.width, s.c0, s.c1)) return;
                    if(mode == 6 || mode == 18){ flush(); batched += 1; return; }
                    bool ok = bl == L && batch.size() < 64;
                    if(ok) for(auto& o : batch) if(o.c1 > s.c0 && s.c1 > o.c0 && !((o.gain && s.gain) || (o.mode == 5 && s.mode == 5))){ ok = false; break; }
                    if(!ok) flush();
                    bl = L; batch.push_back(s);
                };
                int nl = std::clamp((int)layers.size(), 1, 4);
                for(int L = 0; L < nl; L++){
                    std::vector<const Event*> early; std::vector<double> drawn;
                    for(auto& e : evs){
                        if(std::clamp(e.layer, 0, nl - 1) != L) continue;
                        if(blob::isEarlier(e) && e.look != "transpose") early.push_back(&e);
                        else if(e.kind != EventKind::Effect && vr.visible(e)) drawn.push_back(blob::drawOrderOf(e));
                    }
                    std::stable_sort(early.begin(), early.end(), [](const Event* a, const Event* b){ return blob::drawOrderOf(*a) < blob::drawOrderOf(*b); });
                    for(size_t st = 0; st <= early.size(); st++){
                        double lo = st > 0 ? blob::drawOrderOf(*early[st - 1]) : -1e300, hi = st < early.size() ? blob::drawOrderOf(*early[st]) : 1e300;
                        bool draws = false; if(st == 0 || hi > lo) for(double o : drawn) if(early.empty() || (o >= lo && o < hi)){ draws = true; break; }
                        if(draws) flush();
                        if(st < early.size()) queue(L, *early[st]);
                    }
                    for(auto& e : evs) if(std::clamp(e.layer, 0, nl - 1) == L && e.kind == EventKind::Effect && e.look != "transpose" && blob::scopeOf(e) == 0) queue(L, e);
                    flush();
                }
            }
            R.fxBatched.add(batched);
            R.fxp.add((double)fxp); R.fxOld.add(fxo); R.fxNew.add(fxn); R.own.add((double)own.size()); R.ownT.add(msSince(t1));
        }
        R.A.add(box.tA); R.B.add(box.tB); R.K.add(tK); R.C.add(tC); R.vis.add((double)evs.size()); R.inst.add((double)ni); R.frag.add(gm.frags / 1e6); R.ops.add(gm.fragOps / 1e9); R.cg.add((double)gm.coverGroups);
        size_t n = 0, bytes = 0;
        if(fi % 60 == 0){
            for(int l = 0; l < 4; l++){ n += box.en.events(l).size(); for(auto& s : box.en.events(l)) bytes += eventBytes(s.e); }
            if(n > R.maxStored){ R.maxStored = n; R.storedBytes = bytes; }
        }
        if(fi % 600 == 599 && mode >= 1) box.memo.clear();   // bounded memo (events scroll away)
        gm.pruneCache(evs);
        R.coverGroups = std::max(R.coverGroups, gm.coverGroups);
        prevBeat = beat;
    }
    R.planned = box.en.stats().plannedPhrases;
    R.drawnPerFrame = double(gDrawnCalls - d0) / frames; R.animPerFrame = double(gAnimCalls - a0) / frames;
    R.hitRate = gm.packs ? double(gm.cacheHits) / gm.packs : 0;
    return R;
}

static void print(const Result& R){
    auto row = [](const char* n, const Stat& s){ std::printf("    %-34s mean %8.3f  p95 %8.3f  max %8.3f ms\n", n, s.mean(), s.pct(0.95), s.max()); };
    static const char* modes[] = {"baseline", "P1 drawn memo", "P1+P2 skip animated", "P1+P2+P3 instance cache"};
    std::printf("== %s  [%s%s]\n", R.name.c_str(), modes[R.mode], gNewNode && R.mode == 0 ? ", new node path" : "");
    row("A engine.update", R.A); row("B visible + fnCache", R.B); row("K kick scan", R.K); row("C GPU-path CPU (pack)", R.C);
    if(!R.D.v.empty()) row("D CPU renderer 480x270 (1/10 fr)", R.D);
    row("total A+B+K+C", R.total);
    double over = 0; for(double x : R.total.v) if(x > 16.7) over++;
    std::printf("    frames over 16.7 ms: %.1f%%   visible ev/frame mean %.0f max %.0f   instances packed/frame mean %.0f max %.0f\n",
                100 * over / std::max<size_t>(1, R.total.v.size()), R.vis.mean(), R.vis.max(), R.inst.mean(), R.inst.max());
    std::printf("    drawn calls/frame %.0f  animated calls/frame %.0f  cover groups %zu  planned phrases %ld  stored events max %zu (~%.1f MB)  inst-cache hit %.0f%%\n",
                R.drawnPerFrame, R.animPerFrame, R.coverGroups, R.planned, R.maxStored, R.storedBytes / 1048576.0, 100 * R.hitRate);
    std::printf("    GPU passes: effect passes/frame mean %.1f max %.0f   own FBO groups/frame mean %.1f max %.0f   (own-detection scan %.2f ms)\n",
                R.fxp.mean(), R.fxp.max(), R.own.mean(), R.own.max(), R.ownT.mean());
    std::printf("    effect pixel cost (full-screen pass equivalents)/frame: full-screen passes %.1f, with narrow in-place passes %.1f;  batched effect passes/frame (fxQueue) mean %.1f max %.0f\n", R.fxOld.mean(), R.fxNew.mean(), R.fxBatched.mean(), R.fxBatched.max());
    std::printf("    GPU est @1920x1080: event fragments/frame mean %.1f M p95 %.1f M max %.1f M; ALU mean %.1f G p95 %.1f G max %.1f Gops; cover groups mean %.2f p95 %.0f\n",
                R.frag.mean(), R.frag.pct(0.95), R.frag.max(), R.ops.mean(), R.ops.pct(0.95), R.ops.max(), R.cg.mean(), R.cg.pct(0.95));
}

int main(int argc, char** argv){
    installSequencerSchema();
    std::vector<std::string> files;
    for(int i = 1; i < argc; i++) if(argv[i][0] != '-') files.push_back(argv[i]);
    if(files.empty()) files = {"tests/v2/scenes/stress/stress_medium.json", "tests/v2/scenes/stress/stress_dense.json", "tests/v2/scenes/stress/stress_extreme.json"};
    int frames = 3600; bool protos = true, cpuR = true;
    for(int i = 1; i < argc; i++){ std::string a = argv[i]; if(a.rfind("-frames=", 0) == 0) frames = std::atoi(a.c_str() + 8); if(a == "-noproto") protos = false; if(a == "-nocpu") cpuR = false; if(a == "-newnode") gNewNode = true; if(a.rfind("-horizon=", 0) == 0) gHorizon = std::atof(a.c_str() + 9); }
    for(auto& fn : files){
        std::ifstream in(fn); std::stringstream ss; ss << in.rdbuf();
        json j; try { j = json::parse(ss.str()); } catch(...){ std::printf("%s: bad json\n", fn.c_str()); continue; }
        SceneDoc doc = sceneDocFromJson(j);
        auto probs = validate(doc);
        std::printf("\n#### %s  (%zu instances, validate %s)\n", fn.c_str(), j["instances"].size(), probs.empty() ? "ok" : "FAIL");
        for(auto& p : probs) std::printf("   problem: %s\n", p.c_str());
        print(runScene(j, fn, 0, false, frames, cpuR));
        if(protos){
            print(runScene(j, fn, 1, false, frames, false));
            print(runScene(j, fn, 2, false, frames, false));
            print(runScene(j, fn, 3, true, frames, false));
        }
    }
    return 0;
}
