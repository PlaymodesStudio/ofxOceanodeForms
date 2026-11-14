// clang++ -std=c++17 -O2 -ffp-contract=off -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/audit_hires_blocks.cpp
// Run: ./a.out <Forms data folder copy> [scale=3] [only refs containing this]   (DUMP=<dir> DUMPSEED=<seed>: raw rgba of the large render)
// Every instrument rendered at 1920x1080 and at SCALE x (refSize = 1920x1080), and the large render
// scored for blocks SCALE pixels wide / tall: where the picture only changes every SCALE-th pixel
// (stair-steps, cells), the brightness differences between neighbouring pixels all fall on one
// residue (x mod SCALE or y mod SCALE). ratio 1 = no such pattern, SCALE = everything on one residue.
#include "formsLiveEdit.h"
#include "formsGLSketch.h"
#include <cstdio>
using namespace forms;
struct Score { double ex = 1, ey = 1, covx = 0, covy = 0, ink = 0; };
static Score score(const std::vector<uint16_t>& px, int w, int h, int sc){   // 16 bits per channel; L on the 0..255 scale
    std::vector<int> L((size_t)w * h);
    double ink = 0;
    for(size_t q = 0; q < L.size(); q++){ const uint16_t* p = &px[q * 4]; L[q] = (int)((uint64_t)(p[0] + p[1] + p[2]) * p[3] / (765ull * 257 * 257)); ink += L[q] > 8; }
    Score s; s.ink = ink;
    std::vector<double> col(w, 0), row(h, 0);
    for(int y = 0; y < h; y++) for(int x = 0; x + 1 < w; x++) col[x] += std::abs(L[(size_t)y * w + x + 1] - L[(size_t)y * w + x]);
    for(int y = 0; y + 1 < h; y++) for(int x = 0; x < w; x++) row[y] += std::abs(L[(size_t)(y + 1) * w + x] - L[(size_t)y * w + x]);
    auto one = [&](const std::vector<double>& v, double& ratio, double& cov){
        std::vector<double> e(sc, 0), n(sc, 0); double tot = 0, used = 0;
        for(size_t i = 0; i < v.size(); i++){ e[i % sc] += v[i]; tot += v[i]; if(v[i] > 0){ n[i % sc]++; used++; } }
        if(tot <= 0){ ratio = 1; cov = 0; return; }
        int m = (int)(std::max_element(e.begin(), e.end()) - e.begin());
        ratio = e[m] / (tot / sc);
        // how much of the picture's extent has an edge on that residue (a few rectangle edges: little)
        size_t first = v.size(), last = 0; for(size_t i = 0; i < v.size(); i++) if(v[i] > 0){ first = std::min(first, i); last = i; }
        cov = last > first ? n[m] / ((last - first + 1) / (double)sc) : 0;
    };
    one(col, s.ex, s.covx); one(row, s.ey, s.covy);
    return s;
}
int main(int argc, char** argv){
    Engine e; e.rendererFolder = argv[1]; e.loadAll();
    int sc = argc > 2 ? atoi(argv[2]) : 3;
    std::string only = argc > 3 ? argv[3] : "";
    formsgl::Schemas schemas; schemas.loadEmbedded();
    // what effects act on: a few plain instruments
    json under = json::array();
    for(std::string r : {"pulse/fm bells", "hits/large batucada", "masses/sub bass page"}) if(e.presets.count(r)) for(auto& x : e.presetItemsSeeded(e.presets.at(r), 11)) under.push_back(x);
    for(auto& [ref, pr] : e.presets){
        if(!only.empty() && ref.find(only) == std::string::npos) continue;
        Score worst; int seedOf = 0; bool drawn = false; std::string err;
        for(int seed : {101, 202, 303}){
            e.itemBars = 4;
            json items = e.onlyEffects(pr) ? under : json::array();
            for(auto& x : e.presetItemsSeeded(pr, seed)) items.push_back(x);
            json req = {{"size", {1920 * sc, 1080 * sc}}, {"refSize", {1920, 1080}}, {"bars", 4}, {"seed", seed}, {"layer", 1}, {"notegrid", 100}, {"palette", 1}, {"scale", 5}, {"items", items}};
            auto r = formsgl::Sketch(schemas).render(req);
            if(!r.error.empty()){ err = r.error; continue; }
            if(getenv("DUMP") && seed == atoi(getenv("DUMPSEED") ? getenv("DUMPSEED") : "101")){ std::string n = ref; for(char& c : n) if(c == '/' || c == ' ') c = '_'; FILE* f = fopen((std::string(getenv("DUMP")) + "/" + n + ".rgba").c_str(), "wb"); { std::vector<uint8_t> b8(r.rgba.size()); for(size_t q = 0; q < b8.size(); q++) b8[q] = (uint8_t)(r.rgba[q] >> 8); fwrite(b8.data(), 1, b8.size(), f); } fclose(f); }   /* 8-bit raw, as audit_hires_sheet.py reads */
            Score s = score(r.rgba, r.w, r.h, sc);
            if(s.ink < 2000) continue;
            drawn = true;
            double a = std::max(s.ex * std::min(1.0, s.covx * 2), s.ey * std::min(1.0, s.covy * 2)), b = std::max(worst.ex * std::min(1.0, worst.covx * 2), worst.ey * std::min(1.0, worst.covy * 2));
            if(a > b || seedOf == 0){ worst = s; seedOf = seed; }
        }
        if(!drawn){ printf("%-40s  -  %s\n", ref.c_str(), err.empty() ? "nothing drawn" : err.c_str()); continue; }
        printf("%-40s x %.2f (cover %.2f)  y %.2f (cover %.2f)  seed %d  %s\n", ref.c_str(), worst.ex, worst.covx, worst.ey, worst.covy, seedOf, e.onlyEffects(pr) ? "effect" : pr.gen.c_str());
        fflush(stdout);
    }
}
