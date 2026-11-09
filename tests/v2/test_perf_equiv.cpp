// test_perf_equiv.cpp — the optimised engine / modulation give identical results (ESPECTRES v2)
// For every example scene and the stress scenes, runs N frames and hashes, per frame: the layer
// views (zoom), every visible event (all fields, params, x0 / x1) and drawn / animated modulation
// values read for it. Compared against tests/v2/perf_equiv_ref.txt, recorded with the code before
// the optimisation (run with -record to write it again). Also runs each scene twice in-process,
// fast path vs the reference (slow) path, and compares the hashes.
//   build: clang++ -std=c++17 -O2 -DV2AUDIO_NO_OF -I src/formsV2 -I src/formsGL -I src/formsBox -I <of>/libs/json/include tests/v2/test_perf_equiv.cpp
#include "v2Engine.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace forms2;

struct H {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n){ auto c = (const unsigned char*)p; for(size_t i = 0; i < n; i++){ h ^= c[i]; h *= 1099511628211ull; } }
    void d(double x){ if(x == 0) x = 0; bytes(&x, sizeof x); }
    void i(int64_t x){ bytes(&x, sizeof x); }
    void s(const std::string& x){ i((int64_t)x.size()); bytes(x.data(), x.size()); }
};

static void hashEvent(H& h, const Event& e){
    h.i((int64_t)e.id); h.s(e.instance); h.i(e.layer); h.i((int)e.kind); h.s(e.look);
    h.d(e.start); h.d(e.length); h.i(e.pitched); h.i(e.degree); h.d(e.height); h.d(e.height2);
    h.d(e.level); h.d(e.depth); h.s(e.colourRole); h.i((int64_t)e.seed); h.i(e.transient);
    h.i((int64_t)e.p.size()); for(auto& kv : e.p){ h.s(kv.first); h.d(kv.second); }
}

static std::string readFile(const std::string& p){ std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }

static bool gSlowAvail =
#ifdef V2_HAS_SLOWPATH
    true;
#else
    false;
#endif

// per scene: hashes every 10 frames (cumulative)
static std::vector<uint64_t> runScene(const std::string& path, int frames, int modStride, bool slow){
    std::vector<uint64_t> out;
#ifdef V2_HAS_SLOWPATH
    v2SlowPath() = slow;
#else
    (void)slow;
#endif
    Engine en;
    if(!en.loadText(readFile(path))){ out.push_back(0); return out; }
    double bpm = en.scene().bpm > 0 ? en.scene().bpm : 120;
    double dt = bpm / 60.0 / 60.0;
    H h;
    std::vector<std::string> ids; for(auto& in : en.scene().instances) ids.push_back(in.id);
    std::map<std::string, std::vector<std::string>> destsOf;
    auto refreshDests = [&]{
        destsOf.clear();
        for(auto& id : ids){
            std::set<std::string> d{"pitchFine", "height", "size", "look.brightness", "level", "depth"};
            for(auto& e : en.modulation().all()) if(e.owner == id || e.owner.empty()) d.insert(e.r.dest);
            destsOf[id] = {d.begin(), d.end()};
        }
    };
    refreshDests();
    for(int f = 0; f < frames; f++){
        double beat = f * dt;
        if(!ids.empty()){
            if(f == 100) en.reroll(ids[0]);
            if(f == 150) en.setMacro("macro1", 0.7);
            if(f == 200 && ids.size() > 1) en.setMuted(ids[1], true);
            if(f == 260 && ids.size() > 1) en.setMuted(ids[1], false);
            if(f == 300) en.setLocked(ids[0], true);
        }
        std::vector<LayerView> views;
        for(int l = 0; l < 4; l++){ views.push_back(en.viewFor(l, beat, 1920, 1080, 960)); h.d(views.back().pxPerBeat); }
        en.update(beat, views);
        for(int l = 0; l < 4; l++){
            auto vis = en.visible(l, views[(size_t)l]);
            h.i((int64_t)vis.size());
            for(auto& ve : vis){
                hashEvent(h, ve.ev->e); h.d(ve.x0); h.d(ve.x1);
                if(f % modStride) continue;
                DrawnFn dfn = en.drawnFn(*ve.ev); AnimatedFn afn = en.animatedFn(*ve.ev);
                for(auto& d : destsOf[ve.ev->e.instance]){
                    for(double u : {0.0, 0.5, 1.0}) h.d(dfn(d, 0.3, u));
                    h.d(afn(d, 0.3, beat)); h.d(afn(d, 1.0, beat + 0.25));
                }
            }
        }
        h.i((int64_t)en.events(0).size() + (int64_t)en.events(1).size() + (int64_t)en.events(2).size() + (int64_t)en.events(3).size());
        if(f % 10 == 9) out.push_back(h.h);
    }
    return out;
}

static std::vector<std::string> scenes(){
    std::vector<std::string> out;
    for(const char* dir : {"tests/v2/scenes/examples", "tests/v2/scenes/stress"}){
        DIR* d = opendir(dir); if(!d) continue;
        std::vector<std::string> v;
        while(dirent* e = readdir(d)){ std::string n = e->d_name; if(n.size() > 5 && n.substr(n.size() - 5) == ".json") v.push_back(std::string(dir) + "/" + n); }
        closedir(d);
        std::sort(v.begin(), v.end());
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

int main(int argc, char** argv){
    bool record = argc > 1 && std::string(argv[1]) == "-record";
    const char* refPath = "tests/v2/perf_equiv_ref.txt";
    std::map<std::string, std::vector<uint64_t>> ref;
    if(!record){
        std::ifstream f(refPath); std::string line;
        while(std::getline(f, line)){ std::istringstream s(line); std::string n; s >> n; uint64_t x; while(s >> std::hex >> x) ref[n].push_back(x); }
        if(ref.empty()){ std::printf("FAIL no reference %s\n", refPath); return 1; }
    }
    std::ofstream rec; if(record) rec.open(refPath);
    int fails = 0, checks = 0;
    for(auto& p : scenes()){
        bool stress = p.find("/stress/") != std::string::npos;
        int frames = stress ? 360 : 600, stride = stress ? 6 : 1;
        auto got = runScene(p, frames, stride, false);
        if(record){ rec << p; for(auto x : got) rec << " " << std::hex << x; rec << "\n"; std::printf("recorded %s\n", p.c_str()); continue; }
        checks++;
        auto r = ref.find(p);
        bool ok = r != ref.end() && r->second == got;
        if(!ok){
            size_t k = 0; if(r != ref.end()) while(k < got.size() && k < r->second.size() && got[k] == r->second[k]) k++;
            std::printf("FAIL %s differs from the recorded reference from frame ~%zu\n", p.c_str(), k * 10);
            fails++;
        } else std::printf("ok   %s (%d frames, recorded reference)\n", p.c_str(), frames);
        if(gSlowAvail){
            checks++;
            auto slow = runScene(p, frames, stride, true);
            if(slow != got){ std::printf("FAIL %s fast path != slow path\n", p.c_str()); fails++; }
            else std::printf("ok   %s fast == slow path\n", p.c_str());
        }
    }
    if(record) return 0;
    std::printf("%s: %d / %d checks passed\n", fails ? "FAIL" : "PASS", checks - fails, checks);
    return fails ? 1 : 0;
}
