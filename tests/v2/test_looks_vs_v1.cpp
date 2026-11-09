//
//  test_looks_vs_v1.cpp — per-instrument look check (ROADMAP 5.15)
//
//  For every drawing item of the resolved v1 scene requests, renders that v1 cell alone (FormsGL) and
//  its mapped v2 instance alone (v2Legacy + engine + v2Draw) with the same tempo / bars / harmony /
//  seed, scores them with the legacy similarity (brightness profile per band / over time, onsets,
//  row distribution, SSIM) and writes side-by-side sheets per v2 type to notes/v2_looks/.
//  Up to LOOKS_PER_TYPE (default 8) items per type. Prints per-type means and a total.
//  Needs -I src/formsGL. Paths: LOOKS_SKETCH, LOOKS_SCENES, LOOKS_OUT env overrides.
//
#define main legacy_map_main
#include "../../tools/v2_legacy/legacy_map.cpp"
#undef main

static std::string envOr(const char* k, const std::string& d){ const char* v = std::getenv(k); return v ? v : d; }

int main(){
    std::string sketch = envOr("LOOKS_SKETCH", "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027");
    std::string list = envOr("LOOKS_SCENES", "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/rf/before4/scenes.txt");
    std::string out = envOr("LOOKS_OUT", sketch + "/notes/v2_looks");
    int perType = std::atoi(envOr("LOOKS_PER_TYPE", "8").c_str());
    L::Library lib;
    if(!L::loadLibrary(sketch, lib)){ std::printf("SKIPPED: no v1 library at %s\n", sketch.c_str()); return 0; }
    formsgl::Schemas sch; if(!sch.loadEmbedded()){ std::printf("FAILED: schemas\n"); return 1; }
    std::ifstream in(list);
    if(!in){ std::printf("SKIPPED: no request list %s\n", list.c_str()); return 0; }
    std::filesystem::create_directories(out);
    const int W = 480, H = 270; const int RW = 1920, RH = 1080;   // v2 renders at full size, boxed down like v1
    std::map<std::string, std::vector<std::pair<RGB, RGB>>> sheets;
    std::map<std::string, std::vector<Sim>> byType, selfByType;
    std::set<std::string> seen;
    std::map<std::string, L::SceneMap> maps;
    std::string line;
    while(std::getline(in, line)){
        auto t = line.find('\t'); std::string p = t == std::string::npos ? line : line.substr(t + 1);
        json req; if(!L::readJsonFile(p, req)) continue;
        std::string scene = req.value("scene", "");
        if(!maps.count(scene)){ json v1; if(!L::readJsonFile(sketch + "/scenes/" + scene + ".json", v1)) continue; maps[scene] = L::mapScene(v1, lib, scene); }
        L::SceneMap& M = maps[scene];
        int layerIdx = std::max(0, req.value("layer", 1) - 1);
        if(layerIdx >= (int)M.cellItems.size()) continue;
        auto canon = [&](const std::string& k){ auto l = lib.byLegacy.find(k); return l == lib.byLegacy.end() ? k : l->second; };
        int bars = std::max(1, (int)std::lround(L::numOf(req.value("bars", json(8)), 8)));
        double imageBeats = bars * 4.0;
        for(auto& item : req.value("items", json::array())){
            std::string pr = item.value("preset", ""), gen = item.value("gen", "");
            if(gen == "effects" || pr.empty() || seen.count(canon(pr))) continue;
            std::vector<std::string> ids;
            for(size_t c = 0; c < M.cellItems[(size_t)layerIdx].size(); c++) if(canon(M.cellItems[(size_t)layerIdx][c]) == canon(pr)){ ids = M.cellIds[(size_t)layerIdx][c]; break; }
            std::vector<forms2::Instance> ins;
            // LOOKS_FX=1: keep the item's own processes on both sides (v2: its effect instances too)
            static const bool keepFx = std::getenv("LOOKS_FX") != nullptr;
            for(auto& x : M.doc.scene.instances) if(std::find(ids.begin(), ids.end(), x.id) != ids.end() && (keepFx || (x.type != "timefx" && x.type != "filterfx"))){
                forms2::Instance y = x;
                if(y.seq.value("chanceScope", "") == "phrase"){ y.seq.erase("chance"); y.seq.erase("chanceScope"); }
                if(y.seq.value("mode", "") == "follow"){ y.seq["mode"] = "euclidean"; y.seq.erase("followId"); }
                y.phrase = json{{"length", imageBeats}};
                ins.push_back(y);
            }
            if(ins.empty()) continue;
            std::string type; for(auto& x : ins) if(x.type != "timefx" && x.type != "filterfx"){ type = x.type; break; }
            if(type.empty()) continue;
            if((int)byType[type].size() >= perType) continue;
            seen.insert(canon(pr));
            // v1: this item alone (no processes)
            json rq = req; rq["items"] = json::array({item});
            if(!keepFx && rq["items"][0].contains("process")) rq["items"][0].erase("process");
            formsgl::substituteUnderlay(rq);
            formsgl::Sketch sk(sch);
            formsgl::RenderResult rr = sk.render(rq);
            if(!rr.error.empty() || rr.rgba.empty()) continue;
            RGB a = boxRGB(rr.rgba, rr.w, rr.h, 4, W, H, true);
            forms2::SceneDoc d = M.doc;
            d.scene.instances = ins;
            d.scene.bpm = req.value("bpm", d.scene.bpm);
            d.scene.seed = (uint64_t)req.value("seed", (int64_t)1);
            std::vector<int> perBar;
            for(auto& tr : req.value("trans", json::array())) perBar.push_back(L::snapDegree((int)L::numOf(tr), M.scaleSemis));
            d.scene.harmony.sections[0] = L::makeSection(d.scene.harmony.sections[0].scale.name, M.scaleSemis, M.tonic, perBar, imageBeats);
            d.scene.layers.assign(4, forms2::LayerDef{}); for(auto& l2 : d.scene.layers) l2.zoomBeats = imageBeats;
            std::string err; int nev = 0;
            RGB big = renderV2(d, RW, RH, imageBeats, M.tonic, err, nev);
            RGB b; if(big.px.empty()){ b.w = W; b.h = H; b.px.assign((size_t)W * H * 3, 0); } else b = boxRGB(big.px, RW, RH, 3, W, H, false);
            if(const char* dbg = std::getenv("LOOKS_DEBUG")) if(pr.find(dbg) != std::string::npos){
                std::printf("events %d err %s\n", nev, err.c_str());
                for(auto& x : ins) std::printf("seq %s\nparams %s\nroutings %s\n", x.seq.dump().c_str(), x.params.dump().c_str(), forms2::toJson(x.routings).dump().c_str());
                std::printf("v1 item %s\n", item.dump().substr(0, 3000).c_str());
            }
            Sim s = similarity(grayOf(a), grayOf(b), bars);
            if(const char* pf = std::getenv("LOOKS_PROF")) if(pr.find(pf) != std::string::npos){   // diagnostics: column brightness, 32 bins
                for(const RGB* r : {&a, &b}){ Gray g = grayOf(*r); std::printf("prof"); int nb = std::atoi(envOr("LOOKS_PROFN", "32").c_str()); for(int i = 0; i < nb; i++){ double m = 0; for(int x = i * g.w / nb; x < (i + 1) * g.w / nb; x++) for(int y = 0; y < g.h; y++) m += g.at(x, y); std::printf(" %.2f", m); } std::printf("\n"); }
            }
            auto meanOf = [](const RGB& c){ double m = 0; for(auto v : c.px) m += v; return m / std::max<size_t>(1, c.px.size()) / 255.0; };
            std::printf("%-9s %-40s score %5.1f  a %.2f b %.2f c %.2f d %.2f  mean v1 %.3f v2 %.3f ev %d\n", type.c_str(), pr.substr(0, 40).c_str(), s.score, s.a, s.b, s.c, s.d, meanOf(a), meanOf(b), nev);
            Sim self;   // v1 against itself: the same cell with another seed
            { json rq2 = rq; rq2["seed"] = (int64_t)req.value("seed", (int64_t)1) + 7919;
              formsgl::Sketch sk2(sch); formsgl::RenderResult r2 = sk2.render(rq2);
              if(r2.error.empty() && !r2.rgba.empty()) self = similarity(grayOf(a), grayOf(boxRGB(r2.rgba, r2.w, r2.h, 4, W, H, true)), bars); }
            std::printf("          v1 self %5.1f  a %.2f b %.2f c %.2f d %.2f\n", self.score, self.a, self.b, self.c, self.d);
            if(meanOf(a) < 0.0005 && meanOf(b) < 0.0005){ std::printf("          (trivial: both empty, not scored)\n"); continue; }
            byType[type].push_back(s); selfByType[type].push_back(self);
            sheets[type].push_back({a, b});
        }
    }
    double tot = 0, selfTot = 0; int n = 0;
    for(auto& [type, v] : byType){
        Sim m; for(auto& s : v){ m.score += s.score; m.a += s.a; m.b += s.b; m.c += s.c; m.d += s.d; }
        double k = std::max<size_t>(1, v.size());
        std::printf("TYPE %-9s n %2zu  score %5.1f  a %.2f b %.2f c %.2f d %.2f\n", type.c_str(), v.size(), m.score / k, m.a / k, m.b / k, m.c / k, m.d / k);
        Sim q; for(auto& x : selfByType[type]){ q.score += x.score; q.a += x.a; q.b += x.b; q.c += x.c; q.d += x.d; }
        std::printf("SELF %-9s n %2zu  score %5.1f  a %.2f b %.2f c %.2f d %.2f  ratio v2/self %.2f\n", type.c_str(), v.size(), q.score / k, q.a / k, q.b / k, q.c / k, q.d / k, m.score / std::max(1e-9, q.score));
        tot += m.score; selfTot += q.score; n += (int)v.size();
        auto& rows = sheets[type];
        int gap = 8, SW = 2 * W + gap, SH = (int)rows.size() * (H + gap) - gap;
        RGB sh; sh.w = SW; sh.h = SH; sh.px.assign((size_t)SW * SH * 3, 40);
        for(size_t r = 0; r < rows.size(); r++) for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) for(int c = 0; c < 3; c++){
            sh.px[(((size_t)(r * (H + gap) + y)) * SW + x) * 3 + c] = rows[r].first.px[((size_t)y * W + x) * 3 + c];
            sh.px[(((size_t)(r * (H + gap) + y)) * SW + x + W + gap) * 3 + c] = rows[r].second.px[((size_t)y * W + x) * 3 + c];
        }
        std::string ppm = out + "/" + type + ".ppm";
        writePPM(ppm, sh);
        std::string cmd = "sips -s format png '" + ppm + "' --out '" + out + "/" + type + ".png' >/dev/null 2>&1 && rm -f '" + ppm + "'";
        if(std::system(cmd.c_str()) != 0) std::printf("sips failed for %s\n", type.c_str());
    }
    std::printf("v1 self mean %.1f, ratio v2/self %.2f\n", n ? selfTot / n : 0, selfTot > 0 ? tot / selfTot : 0);
    std::printf("%s: %d instruments, mean score %.1f\n", n >= 40 ? "PASSED" : "FAILED (fewer than 40)", n, n ? tot / n : 0);
    return n >= 40 ? 0 : 1;
}
