//
//  legacy_map.cpp — ESPECTRES v2 legacy mapping tool (ROADMAP 5.5)
//
//  legacy_map map  <sketchDir> <outDir>                  map every v1 scene, validate, write <outDir>/<NAME>.json
//  legacy_map test <sketchDir>                           every scene maps + validates, mapping is deterministic
//  legacy_map sim  <sketchDir> <scenes.txt> <workDir> <pngDir> <results.json>
//        renders each scene's resolved v1 requests with FormsGL and the mapped v2 scene with the v2 engine +
//        CPU renderer (same size, bars, tempo, harmony; only the cells the v1 image actually drew), computes
//        the similarity components and writes side-by-side sheets (PPM in workDir, PNG via sips in pngDir).
//
//  Build: clang++ -std=c++17 -O2 -I src/formsV2 -I src/formsGL -I src/formsBox -I <of>/libs/json/include
//

#include "formsGLSketch.h"
#include "v2Engine.h"
#include "v2Draw.h"
#include "v2Legacy.h"
#include <cstdio>
#include <iostream>
#include <unordered_map>

using forms2::json;
namespace L = forms2::legacy;

static std::vector<std::string> sceneFiles(const std::string& dir){
    std::vector<std::string> v;
    for(auto& f : std::filesystem::directory_iterator(dir + "/scenes")) if(f.path().extension() == ".json") v.push_back(f.path().string());
    std::sort(v.begin(), v.end());
    return v;
}

static int runMap(const std::string& sketch, const std::string& out, bool write){
    L::Library lib;
    if(!L::loadLibrary(sketch, lib)){ std::cerr << "library: " << lib.error << "\n"; return 1; }
    int ok = 0, n = 0;
    if(write) std::filesystem::create_directories(out);
    for(auto& f : sceneFiles(sketch)){
        json v1; if(!L::readJsonFile(f, v1)) continue;
        std::string name = std::filesystem::path(f).stem().string();
        L::SceneMap M = L::mapScene(v1, lib, name);
        n++;
        if(M.errors.empty()) ok++;
        std::printf("%-26s inst %3zu  conf %.2f  unmapped %3zu  %s\n", name.c_str(), M.doc.scene.instances.size(), M.confidence, M.unmapped.size(), M.errors.empty() ? "valid" : "INVALID");
        for(size_t i = 0; i < M.errors.size() && i < 8; i++) std::printf("    %s\n", M.errors[i].c_str());
        if(write){ std::ofstream o(out + "/" + name + ".json"); o << L::sceneFile(M).dump(1) << "\n"; }
    }
    std::printf("%d / %d scenes validate\n", ok, n);
    return ok == n ? 0 : 2;
}

static int runTest(const std::string& sketch){
    L::Library lib;
    if(!L::loadLibrary(sketch, lib)){ std::cerr << "library\n"; return 1; }
    int fails = 0, n = 0;
    for(auto& f : sceneFiles(sketch)){
        json v1; L::readJsonFile(f, v1);
        std::string name = std::filesystem::path(f).stem().string();
        auto a = L::mapScene(v1, lib, name), b = L::mapScene(v1, lib, name);
        n++;
        std::string da = L::sceneFile(a).dump(), db = L::sceneFile(b).dump();
        if(da != db){ std::printf("FAIL %s: not deterministic\n", name.c_str()); fails++; }
        if(!a.errors.empty()){ std::printf("FAIL %s: %s\n", name.c_str(), a.errors[0].c_str()); fails++; }
        bool v1HasCells = false;
        for(auto& L1 : v1.value("layers", json::array())) for(auto& c : L1.value("cells", json::array())) if(!c.value("item", "").empty()) v1HasCells = true;
        if(a.doc.scene.instances.empty() && v1HasCells){ std::printf("FAIL %s: no instances\n", name.c_str()); fails++; }
        // round trip through the file format keeps the doc valid and loadable by the engine
        auto back = forms2::sceneDocFromJson(L::sceneFile(a));
        if(!forms2::validate(back).empty()){ std::printf("FAIL %s: round trip invalid\n", name.c_str()); fails++; }
        forms2::Engine e;
        if(!e.loadJson(L::sceneFile(a))){ std::printf("FAIL %s: engine refuses it (%s)\n", name.c_str(), e.problems().empty() ? "" : e.problems()[0].c_str()); fails++; }
    }
    std::printf("%s: %d scenes, %d failures\n", fails ? "FAILED" : "PASSED", n, fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- similarity
struct Gray { int w = 0, h = 0; std::vector<float> v; float at(int x, int y) const { return v[(size_t)y * w + x]; } };

static Gray downscale(const Gray& g, int W, int H){
    Gray o; o.w = W; o.h = H; o.v.assign((size_t)W * H, 0);
    for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
        int x0 = x * g.w / W, x1 = std::max(x0 + 1, (x + 1) * g.w / W), y0 = y * g.h / H, y1 = std::max(y0 + 1, (y + 1) * g.h / H);
        double s = 0; for(int yy = y0; yy < y1; yy++) for(int xx = x0; xx < x1; xx++) s += g.at(xx, yy);
        o.v[(size_t)y * W + x] = (float)(s / ((x1 - x0) * (y1 - y0)));
    }
    return o;
}
static double pearson(const std::vector<double>& a, const std::vector<double>& b){
    size_t n = a.size(); double ma = 0, mb = 0; for(size_t i = 0; i < n; i++){ ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0; for(size_t i = 0; i < n; i++){ sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
    if(saa < 1e-12 && sbb < 1e-12) return (std::fabs(ma - mb) < 0.02) ? 1 : 0.5;
    if(saa < 1e-12 || sbb < 1e-12) return 0;
    return sab / std::sqrt(saa * sbb);
}
static double cosine(const std::vector<double>& a, const std::vector<double>& b){
    double ab = 0, aa = 0, bb = 0; for(size_t i = 0; i < a.size(); i++){ ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
    if(aa < 1e-12 && bb < 1e-12) return 1;
    if(aa < 1e-12 || bb < 1e-12) return 0;
    return ab / std::sqrt(aa * bb);
}
struct Sim { double a = 0, aCol = 0, aBand = 0, b = 0, c = 0, d = 0, score = 0; };

static Sim similarity(const Gray& A, const Gray& B, int bars){
    Sim s;
    const int NB = 96;
    auto colProf = [&](const Gray& g, int y0, int y1){
        std::vector<double> p(NB, 0);
        for(int x = 0; x < g.w; x++){ double m = 0; for(int y = y0; y < y1; y++) m += g.at(x, y); p[(size_t)(x * NB / g.w)] += m / std::max(1, y1 - y0); }
        return p;
    };
    s.aCol = std::max(0.0, pearson(colProf(A, 0, A.h), colProf(B, 0, B.h)));
    const int bands = 9;
    double wsum = 0, acc = 0;
    for(int k = 0; k < bands; k++){
        int y0 = k * A.h / bands, y1 = (k + 1) * A.h / bands;
        auto pa = colProf(A, y0, y1), pb = colProf(B, y0, y1);
        double e = 0; for(int i = 0; i < NB; i++) e += pa[i] + pb[i];
        if(e < 1e-6) continue;
        acc += e * std::max(0.0, pearson(pa, pb)); wsum += e;
    }
    s.aBand = wsum > 0 ? acc / wsum : 1;
    s.a = 0.5 * s.aCol + 0.5 * s.aBand;
    // onsets per 16th
    int slots = std::max(1, bars * 16);
    auto onsets = [&](const Gray& g){
        std::vector<double> o((size_t)slots, 0);
        for(int x = 1; x < g.w; x++){ double v = 0; for(int y = 0; y < g.h; y++) v += std::max(0.f, g.at(x, y) - g.at(x - 1, y)); o[(size_t)std::min(slots - 1, x * slots / g.w)] += v; }
        return o;
    };
    s.b = cosine(onsets(A), onsets(B));
    // pitch distribution (brightness per row)
    const int NR = 54;
    auto rows = [&](const Gray& g){
        std::vector<double> r(NR, 0); double t = 0;
        for(int y = 0; y < g.h; y++){ double m = 0; for(int x = 0; x < g.w; x++) m += g.at(x, y); r[(size_t)(y * NR / g.h)] += m; t += m; }
        if(t > 0) for(auto& x : r) x /= t;
        return std::make_pair(r, t);
    };
    auto ra = rows(A), rb = rows(B);
    if(ra.second < 1e-6 && rb.second < 1e-6) s.c = 1;
    else if(ra.second < 1e-6 || rb.second < 1e-6) s.c = 0;
    else { double inter = 0; for(int i = 0; i < NR; i++) inter += std::min(ra.first[i], rb.first[i]); s.c = inter; }
    // SSIM on 96 x 54 grey
    Gray a = downscale(A, 96, 54), b = downscale(B, 96, 54);
    const double C1 = 0.01 * 0.01, C2 = 0.03 * 0.03;
    double tot = 0; int cnt = 0;
    for(int y = 0; y + 8 <= 54; y += 4) for(int x = 0; x + 8 <= 96; x += 4){
        double ma = 0, mb = 0; for(int j = 0; j < 8; j++) for(int i = 0; i < 8; i++){ ma += a.at(x + i, y + j); mb += b.at(x + i, y + j); }
        ma /= 64; mb /= 64;
        double va = 0, vb = 0, cv = 0; for(int j = 0; j < 8; j++) for(int i = 0; i < 8; i++){ double da = a.at(x + i, y + j) - ma, db = b.at(x + i, y + j) - mb; va += da * da; vb += db * db; cv += da * db; }
        va /= 63; vb /= 63; cv /= 63;
        tot += ((2 * ma * mb + C1) * (2 * cv + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2)); cnt++;
    }
    s.d = std::max(0.0, cnt ? tot / cnt : 0);
    s.score = 100 * (0.30 * s.a + 0.20 * s.b + 0.25 * s.c + 0.25 * s.d);
    return s;
}

struct RGB { int w = 0, h = 0; std::vector<uint8_t> px; };

static RGB boxRGB(const std::vector<uint8_t>& src, int sw, int sh, int ch, int W, int H, bool premulAlpha){
    RGB o; o.w = W; o.h = H; o.px.assign((size_t)W * H * 3, 0);
    for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
        int x0 = x * sw / W, x1 = std::max(x0 + 1, (x + 1) * sw / W), y0 = y * sh / H, y1 = std::max(y0 + 1, (y + 1) * sh / H);
        double s[3] = {0, 0, 0};
        for(int yy = y0; yy < y1; yy++) for(int xx = x0; xx < x1; xx++){
            const uint8_t* p = &src[((size_t)yy * sw + xx) * ch];
            double a = premulAlpha && ch == 4 ? p[3] / 255.0 : 1;
            for(int k = 0; k < 3; k++) s[k] += p[k] * a;
        }
        int n = (x1 - x0) * (y1 - y0);
        for(int k = 0; k < 3; k++) o.px[((size_t)y * W + x) * 3 + k] = (uint8_t)std::lround(s[k] / n);
    }
    return o;
}
static Gray grayOf(const RGB& c){
    Gray g; g.w = c.w; g.h = c.h; g.v.resize((size_t)c.w * c.h);
    for(size_t i = 0; i < g.v.size(); i++) g.v[i] = std::max({c.px[i * 3], c.px[i * 3 + 1], c.px[i * 3 + 2]}) / 255.f;
    return g;
}

// v2 render of a doc over [0, imageBeats) across the whole width
// the scene's role colours (the converted v1 palette); LEGACY_NOCOLOURS=1 draws the built-in roles
static std::map<std::string, std::array<float, 3>> gColours;
static RGB renderV2(const forms2::SceneDoc& doc, int W, int H, double imageBeats, int tonic, std::string& err, int& events){
    forms2::Engine e;
    if(!e.loadJson(forms2::toJson(doc))){ err = e.problems().empty() ? "load failed" : e.problems()[0]; return RGB{}; }
    std::vector<forms2::LayerView> views;
    for(int l = 0; l < 4; l++) views.push_back(e.viewFor(l, 0, W, H, 0));
    e.update(0, views);
    std::vector<forms2::Event> evs;
    std::unordered_map<uint64_t, std::pair<forms2::DrawnFn, forms2::AnimatedFn>> fns;
    for(int l = 0; l < 4; l++) for(auto& ve : e.visible(l, views[(size_t)l])){
        const forms2::StoredEvent& s = *ve.ev;
        if(s.e.start >= imageBeats - 1e-9) continue;
        forms2::Event ev = s.e;
        // harness: the engine leaves the look empty for types whose look key is cell / effect / shape
        if(ev.look.empty()){
            if(auto* in = e.instance(ev.instance)) for(auto k : {"cell", "effect", "shape"})
                if(in->params.is_object() && in->params.contains(k) && in->params[k].is_string()){ ev.look = in->params[k].get<std::string>(); break; }
        }
        // harness: the engine stamps the instance heightFrom / heightTo over mass harmonics; put the
        // planned heights back, moved from the engine's default pitch space to the render's
        if(ev.p.count("harmonic")){
            forms2::PitchSpace eng, ren; ren.lowSemi = tonic - 25; ren.highSemi = tonic + 75;
            ev.p["heightFrom"] = ren.heightOf(eng.semisOf(ev.height));
            ev.p["heightTo"] = ren.heightOf(eng.semisOf(ev.height2));
        }
        if(std::getenv("LEGACY_EVDBG")) std::printf("  ev kind %d start %.2f len %.2f deg %d pitched %d h %.3f level %.2f look %s\n", (int)ev.kind, ev.start, ev.length, ev.degree, (int)ev.pitched, ev.height, ev.level, ev.look.c_str());
        evs.push_back(ev);
        fns[ev.id] = {e.drawnFn(s), e.animatedFn(s)};
    }
    events = (int)evs.size();
    forms2::draw::Renderer r;
    r.frame.width = W; r.frame.height = H; r.frame.headerX = 0; r.frame.nowBeat = 0;
    if(!std::getenv("LEGACY_NOCOLOURS")) r.frame.paper.colours = gColours;
    auto ha = e.harmonyAt(0); if(ha.scale) r.frame.scale = *ha.scale;
    // v1 pitch rows: height = (semitones above the root + 1) / 100; v2 octave = v1 octave - 2
    r.frame.space = forms2::draw::v1PitchSpace(100, tonic);
    r.frame.mod.drawn = [&](const forms2::Event& ev, const std::string& dest, double u, double base){ auto it = fns.find(ev.id); return it == fns.end() || !it->second.first ? base : it->second.first(dest, base, u); };
    r.frame.mod.animated = [&](const forms2::Event& ev, const std::string& dest, forms2::Beat now, double base){ auto it = fns.find(ev.id); return it == fns.end() || !it->second.second ? base : it->second.second(dest, base, now); };
    r.layers = doc.scene.layers; r.layers.resize(4);
    for(auto& L2 : r.layers) L2.zoomBeats = imageBeats;
    r.render(evs);
    RGB o; o.w = W; o.h = H; o.px = r.rgb8();
    return o;
}

static void writePPM(const std::string& path, const RGB& c){
    FILE* f = std::fopen(path.c_str(), "wb"); if(!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", c.w, c.h); std::fwrite(c.px.data(), 1, c.px.size(), f); std::fclose(f);
}

static int runSim(const std::string& sketch, const std::string& listPath, const std::string& work, const std::string& pngDir, const std::string& resultsPath){
    L::Library lib;
    if(!L::loadLibrary(sketch, lib)){ std::cerr << "library\n"; return 1; }
    formsgl::Schemas sch; if(!sch.loadEmbedded()){ std::cerr << "schemas\n"; return 1; }
    std::filesystem::create_directories(work); std::filesystem::create_directories(pngDir);
    std::map<std::string, std::vector<std::string>> reqs;
    { std::ifstream in(listPath); std::string line;
      while(std::getline(in, line)){ if(line.empty()) continue; auto t = line.find('\t'); std::string p = t == std::string::npos ? line : line.substr(t + 1);
          json r; if(!L::readJsonFile(p, r)) continue; reqs[r.value("scene", "")].push_back(p); } }
    const int W = 480, H = 270;
    json results = json::array();
    for(auto& f : sceneFiles(sketch)){
        std::string name = std::filesystem::path(f).stem().string();
        json v1; L::readJsonFile(f, v1);
        L::SceneMap M = L::mapScene(v1, lib, name);
        gColours = forms2::draw::coloursFromJson(M.legacy.value("colourRoles", json::object()));
        json sres{{"scene", name}, {"confidence", M.confidence}, {"valid", M.errors.empty()}, {"instances", M.doc.scene.instances.size()},
                  {"unmapped", M.unmapped}, {"approx", M.legacy.value("approx", json::array())}, {"images", json::array()}};
        std::vector<RGB> rowsV1, rowsV2;
        auto& rl = reqs[name];
        for(size_t ri = 0; ri < rl.size() && ri < 2; ri++){
            json req; L::readJsonFile(rl[ri], req);
            int bars = std::max(1, (int)std::lround(L::numOf(req.value("bars", json(8)), 8)));
            double imageBeats = bars * 4.0;
            // v1
            json rq = req; formsgl::substituteUnderlay(rq);
            formsgl::Sketch sk(sch);
            formsgl::RenderResult rr = sk.render(rq);
            if(!rr.error.empty() || rr.rgba.empty()){ std::printf("%s: v1 error %s\n", name.c_str(), rr.error.c_str()); continue; }
            RGB a = boxRGB(rr.rgba, rr.w, rr.h, 4, W, H, true);
            // v2: only the cells this image drew, same bars, tempo and per-bar harmony
            forms2::SceneDoc d = M.doc;
            int layerIdx = std::max(0, req.value("layer", 1) - 1);
            std::vector<std::string> keep;
            auto canon = [&](const std::string& k){ auto l = lib.byLegacy.find(k); return l == lib.byLegacy.end() ? k : l->second; };
            if(layerIdx < (int)M.cellItems.size()){
                std::vector<bool> usedCell(M.cellItems[(size_t)layerIdx].size(), false);
                for(auto& it : req.value("items", json::array())){
                    std::string pr = it.value("preset", "");
                    for(size_t c = 0; c < usedCell.size(); c++) if(!usedCell[c] && canon(M.cellItems[(size_t)layerIdx][c]) == canon(pr)){ usedCell[c] = true; for(auto& id : M.cellIds[(size_t)layerIdx][c]) keep.push_back(id); break; }
                }
            }
            std::vector<forms2::Instance> ins;
            const char* only = std::getenv("LEGACY_ONLY");   // diagnostics: "nofx" drops effect instances
            for(auto& in : d.scene.instances) if(std::find(keep.begin(), keep.end(), in.id) != keep.end()){
                if(only && std::string(only) == "nofx" && (in.type == "timefx" || in.type == "filterfx")) continue;
                forms2::Instance x = in;
                if(x.seq.value("chanceScope", "") == "phrase"){ x.seq.erase("chance"); x.seq.erase("chanceScope"); }   // the image already rolled its cells
                x.phrase = json{{"length", imageBeats}};
                if(const char* dr = std::getenv("LEGACY_DROP")){   // diagnostics: comma list of seq keys to drop
                    std::string t; for(const char* q = dr; ; q++){ if(*q == ',' || !*q){ if(!t.empty()) x.seq.erase(t); t.clear(); if(!*q) break; } else t += *q; }
                }
                ins.push_back(x);
            }
            // follow ids that point at dropped instances fall back to euclidean
            for(auto& x : ins) if(x.seq.value("mode", "") == "follow"){
                std::string fid = x.seq.value("followId", "");
                bool found = false; for(auto& y : ins) if(y.id == fid) found = true;
                if(!found){ x.seq["mode"] = "euclidean"; x.seq.erase("followId"); }
            }
            d.scene.instances = ins;
            d.scene.bpm = req.value("bpm", d.scene.bpm);
            d.scene.seed = (uint64_t)req.value("seed", (int64_t)1);
            std::vector<int> shifts;   // v1 per-bar transposes -> step key shifts
            for(auto& t : req.value("trans", json::array())) shifts.push_back((int)std::lround(L::numOf(t)));
            d.scene.harmony.sections[0] = L::makeSection(d.scene.harmony.sections[0].scale.name, M.scaleSemis, M.tonic, {}, imageBeats, shifts);
            d.scene.layers.assign(4, forms2::LayerDef{}); for(auto& l2 : d.scene.layers) l2.zoomBeats = imageBeats;
            std::string err; int nev = 0;
            if(const char* dg = std::getenv("LEGACY_DIAG")) if(name == dg){   // diagnostics: each instance alone
                std::vector<forms2::Instance> base;
                for(auto& x : ins) if(x.type != "timefx" && x.type != "filterfx") base.push_back(x);
                for(auto& x : ins){
                    forms2::SceneDoc one = d; one.scene.instances = {x};
                    if(x.type == "timefx" || x.type == "filterfx"){ one.scene.instances = base; one.scene.instances.push_back(x); }
                    std::string e2; int n2 = 0; RGB r1 = renderV2(one, W, H, imageBeats, M.tonic, e2, n2);
                    double m = 0; for(auto v : r1.px) m += v; m /= std::max<size_t>(1, r1.px.size()) * 255.0;
                    std::printf("  diag %-34s %-9s ev %4d mean %.3f %s\n", x.id.c_str(), x.type.c_str(), n2, m, e2.c_str());
                }
            }
            // v2 at full size, boxed down like v1 (thin lines keep their v1 weight)
            RGB big = renderV2(d, 1920, 1080, imageBeats, M.tonic, err, nev);
            RGB b;
            if(big.px.empty()){ std::printf("%s: v2 error %s\n", name.c_str(), err.c_str()); b.w = W; b.h = H; b.px.assign((size_t)W * H * 3, 0); }
            else b = boxRGB(big.px, 1920, 1080, 3, W, H, false);
            Sim s = similarity(grayOf(a), grayOf(b), bars);
            // v1 against itself: the same request with another seed (only the random stream differs)
            Sim self;
            { json rq2 = req; rq2["seed"] = (int64_t)req.value("seed", (int64_t)1) + 7919; formsgl::substituteUnderlay(rq2);
              formsgl::Sketch sk2(sch); formsgl::RenderResult r2 = sk2.render(rq2);
              if(r2.error.empty() && !r2.rgba.empty()) self = similarity(grayOf(a), grayOf(boxRGB(r2.rgba, r2.w, r2.h, 4, W, H, true)), bars); }
            auto meanOf = [](const RGB& c){ double m = 0; for(auto v : c.px) m += v; return m / std::max<size_t>(1, c.px.size()) / 255.0; };
            sres["images"].push_back({{"request", rl[ri]}, {"bars", bars}, {"items", req.value("items", json::array()).size()}, {"instances", ins.size()}, {"events", nev},
                                      {"a", s.a}, {"aCol", s.aCol}, {"aBand", s.aBand}, {"b", s.b}, {"c", s.c}, {"d", s.d}, {"score", s.score}, {"v2error", err}, {"self", {{"a", self.a}, {"b", self.b}, {"c", self.c}, {"d", self.d}, {"score", self.score}}}, {"meanV1", meanOf(a)}, {"meanV2", meanOf(b)}, {"trivial", meanOf(a) < 0.006 && meanOf(b) < 0.006}});
            rowsV1.push_back(a); rowsV2.push_back(b);
        }
        // sheet: rows of [v1 | v2]
        if(!rowsV1.empty()){
            int gap = 8, SW = 2 * W + gap, SH = (int)rowsV1.size() * (H + gap) - gap;
            RGB sheet; sheet.w = SW; sheet.h = SH; sheet.px.assign((size_t)SW * SH * 3, 40);
            for(size_t r = 0; r < rowsV1.size(); r++) for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) for(int k = 0; k < 3; k++){
                sheet.px[(((size_t)(r * (H + gap) + y)) * SW + x) * 3 + k] = rowsV1[r].px[((size_t)y * W + x) * 3 + k];
                sheet.px[(((size_t)(r * (H + gap) + y)) * SW + x + W + gap) * 3 + k] = rowsV2[r].px[((size_t)y * W + x) * 3 + k];
            }
            std::string ppm = work + "/" + name + ".ppm";
            writePPM(ppm, sheet);
            std::string cmd = "sips -s format png '" + ppm + "' --out '" + pngDir + "/" + name + ".png' >/dev/null 2>&1";
            if(std::system(cmd.c_str()) != 0) std::printf("%s: sips failed\n", name.c_str());
        }
        double sc = 0, n = 0; for(auto& im : sres["images"]){ sc += im["score"].get<double>(); n++; }
        sres["score"] = n ? sc / n : 0;
        bool triv = n > 0; for(auto& im : sres["images"]) if(!im["trivial"].get<bool>()) triv = false;
        sres["trivial"] = triv;   // both renders (nearly) black: the components say nothing
        std::printf("%-26s score %5.1f  conf %.2f\n", name.c_str(), sres["score"].get<double>(), M.confidence);
        results.push_back(sres);
    }
    std::ofstream o(resultsPath); o << results.dump(1) << "\n";
    return 0;
}

int main(int argc, char** argv){
    if(argc < 3){ std::cerr << "usage: legacy_map map|test|sim <sketchDir> ...\n"; return 1; }
    std::string mode = argv[1], sketch = argv[2];
    if(mode == "map") return runMap(sketch, argc > 3 ? argv[3] : "", argc > 3);
    if(mode == "test") return runTest(sketch);
    if(mode == "sim" && argc >= 7) return runSim(sketch, argv[3], argv[4], argv[5], argv[6]);
    std::cerr << "bad arguments\n"; return 1;
}
