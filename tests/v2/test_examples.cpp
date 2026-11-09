// test_examples.cpp — the v2 examples collection (ROADMAP 5.5)
// Loads every tests/v2/scenes/examples/*.json, validates it (v2Format validate), plays it in the
// engine for 32 beats and renders 3 frames each with the CPU renderer (960x540) to PPM; then
// converts them to PNG with sips (macOS) when a PNG folder is given.
//   usage: test_examples [examplesDir] [ppmDir] [pngDir|-]
// A scene with a top-level "kit" (espectres.kit) gets that kit dropped twice (dropKit) before playing.
#include "v2Engine.h"
#include "v2Draw.h"
#ifndef FORMS2_HEADLESS
#define FORMS2_HEADLESS
#endif
#include "v2DrawGL.h"
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

using namespace forms2;

static int passed = 0, failed = 0;
#define CHECK(c, ...) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } }while(0)

static const char* kScratch = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2/examples/";
static const char* kPng = "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027/notes/v2_examples/";

// the CPU branch of formsGL2Box::renderFrame, headless
struct Player {
    Engine engine;
    draw::Renderer cpu;
    std::unordered_map<uint64_t, std::pair<DrawnFn, AnimatedFn>> fns;
    int W = 960, H = 540; double hx = 480;
    int frameDrawn = 0;
    bool has3D = false; draw::ThreeD td;     // scene "view3d" (5.14), absent = flat
    draw::Paper paper;                       // scene "paper" (5.18)
    void step(double beat, bool render){
        std::vector<LayerView> views; std::vector<LayerDef> layers = engine.scene().layers; layers.resize(4);
        for(int l = 0; l < 4; l++){ views.push_back(engine.viewFor(l, beat, W, H, hx)); layers[(size_t)l].zoomBeats = engine.layerZoom(l, beat); }
        draw::ThreeD t = td;
        if(has3D) for(int l = 0; l < 4; l++){
            views[(size_t)l].width = (int)(W * std::max(1.0, td.lookAhead));   // far events converge: plan farther ahead
            t.layerCamera[(size_t)l] = engine.layerParam("camera", td.layerCamera[(size_t)l], l, beat);
            t.planeZ[(size_t)l] = engine.layerParam("planeZ", td.planeZ[(size_t)l], l, beat);
        }
        engine.update(beat, views);
        if(!render) return;
        std::vector<Event> evs; std::unordered_map<uint64_t, std::pair<DrawnFn, AnimatedFn>> next;
        for(int l = 0; l < 4; l++) for(auto& ve : engine.visible(l, views[(size_t)l])){
            evs.push_back(ve.ev->e);
            auto f = fns.find(ve.ev->e.id);
            next[ve.ev->e.id] = f != fns.end() ? f->second : std::make_pair(engine.drawnFn(*ve.ev), engine.animatedFn(*ve.ev));
        }
        fns.swap(next);
        draw::Frame f; f.width = W; f.height = H; f.headerX = hx; f.nowBeat = beat; f.td = t; f.paper = paper;
        auto ha = engine.harmonyAt(beat); if(ha.scale) f.scale = *ha.scale;
        f.mod.drawn = [this](const Event& e, const std::string& d, double u, double base){
            auto it = fns.find(e.id); return it == fns.end() || !it->second.first ? base : it->second.first(d, base, u); };
        f.mod.animated = [this](const Event& e, const std::string& d, Beat now, double base){
            auto it = fns.find(e.id); return it == fns.end() || !it->second.second ? base : it->second.second(d, base, now); };
        cpu.frame = f; cpu.layers = layers;
        cpu.render(evs);
        frameDrawn = cpu.stats.drawn;
    }
};

static std::vector<std::string> listJson(const std::string& dir){
    std::vector<std::string> v;
    if(DIR* d = opendir(dir.c_str())){ while(auto* e = readdir(d)){ std::string n = e->d_name; if(n.size() > 5 && n.substr(n.size() - 5) == ".json") v.push_back(n); } closedir(d); }
    std::sort(v.begin(), v.end()); return v;
}

int main(int argc, char** argv){
    std::string dir = argc > 1 ? argv[1] : "tests/v2/scenes/examples/";
    std::string ppm = argc > 2 ? argv[2] : kScratch;
    std::string png = argc > 3 ? argv[3] : kPng;
    if(dir.back() != '/') dir += '/'; if(ppm.back() != '/') ppm += '/'; if(png != "-" && png.back() != '/') png += '/';
    std::system(("mkdir -p '" + ppm + "'").c_str());
    if(png != "-") std::system(("mkdir -p '" + png + "'").c_str());
    installSequencerSchema();
    auto files = listJson(dir);
    CHECK(files.size() >= 14, "only %zu examples", files.size());
    const double frames[3] = {8, 20, 31.5};
    int okAll = 0;
    for(auto& name : files){
        std::string stem = name.substr(0, name.size() - 5);
        std::ifstream in(dir + name); std::stringstream ss; ss << in.rdbuf();
        json j; try { j = json::parse(ss.str()); } catch(...){ CHECK(false, "%s: bad json", name.c_str()); continue; }
        CHECK(j.contains("notes") && j["notes"].is_string() && !j["notes"].get<std::string>().empty(), "%s: no notes", name.c_str());
        SceneDoc doc = sceneDocFromJson(j);
        if(j.contains("kit")){
            Kit k = kitFromJson(j["kit"]);
            CHECK(validate(k).empty(), "%s: kit invalid", name.c_str());
            auto a = dropKit(doc, k), b = dropKit(doc, k);
            CHECK(a.size() == 2 && b.size() == 2 && b[0] == "k_2" && b[1] == "ghost_2", "%s: dropKit ids", name.c_str());
            for(auto& i : doc.scene.instances) if(i.id == "ghost_2") CHECK(i.seq.value("followId", "") == "k_2", "%s: follow not renamed", name.c_str());
        }
        auto probs = validate(doc);
        for(auto& p : probs) std::printf("  %s: %s\n", stem.c_str(), p.c_str());
        CHECK(probs.empty(), "%s: does not validate", name.c_str());
        Player P;
        if(j.contains("view3d")){ P.has3D = true; P.td = draw::threeDFromJson(j["view3d"]); }
        if(j.contains("paper")) P.paper = draw::paperFromJson(j["paper"]);
        bool loaded = j.contains("kit") ? P.engine.load(doc) : P.engine.loadJson(j);
        for(auto& p : P.engine.problems()) std::printf("  %s problem: %s\n", stem.c_str(), p.c_str());
        for(auto& w : P.engine.warnings()) std::printf("  %s warning: %s\n", stem.c_str(), w.c_str());
        CHECK(loaded && P.engine.ready(), "%s: engine did not load", name.c_str());
        if(!loaded) continue;
        int fi = 0, drawnFrames = 0; size_t maxStored = 0;
        for(double t = 0; t <= 32 + 1e-9; t += 0.25){
            bool r = fi < 3 && std::fabs(t - frames[fi]) < 1e-9;
            P.step(t, r);
            size_t n = 0; for(int l = 0; l < 4; l++) n += P.engine.events(l).size();
            maxStored = std::max(maxStored, n);
            if(r){
                char fn[64]; std::snprintf(fn, sizeof fn, "_f%d.ppm", fi + 1);
                if(P.paper.hasBackground){   // shown over the scene's paper colour
                    auto b = drawgl::rgb8OverBackground(P.cpu.display(), P.paper.background[0], P.paper.background[1], P.paper.background[2]);
                    FILE* pf = std::fopen((ppm + stem + fn).c_str(), "wb");
                    CHECK(pf, "%s: ppm write", name.c_str());
                    if(pf){ std::fprintf(pf, "P6\n%d %d\n255\n", P.cpu.display().w, P.cpu.display().h); std::fwrite(b.data(), 1, b.size(), pf); std::fclose(pf); }
                } else CHECK(P.cpu.writePPM(ppm + stem + fn), "%s: ppm write", name.c_str());
                if(P.frameDrawn > 0) drawnFrames++;
                if(png != "-"){
                    std::string out = png + stem + "_f" + std::to_string(fi + 1) + ".png";
                    std::system(("sips -s format png '" + ppm + stem + fn + "' --out '" + out + "' >/dev/null 2>&1").c_str());
                }
                fi++;
            }
        }
        CHECK(maxStored > 0, "%s: no events in 32 beats", name.c_str());
        CHECK(drawnFrames > 0, "%s: nothing drawn", name.c_str());
        std::printf("example %-28s validate %s  events %4zu  frames drawn %d/3\n", stem.c_str(), probs.empty() ? "ok" : "FAIL", maxStored, drawnFrames);
        if(probs.empty() && maxStored > 0 && drawnFrames > 0) okAll++;
    }
    std::printf("test_examples: %d examples ok of %zu, %d passed, %d failed\n", okAll, files.size(), passed, failed);
    return failed ? 1 : 0;
}
