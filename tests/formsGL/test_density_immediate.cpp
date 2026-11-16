// The node's Density on pages already drawn (Immediate; formsLiveEdit.h patchDensity), headless: real
// page requests from every scene's first layer, then the density moved around.
// Checks: the same value changes nothing; going down only removes instruments, going up only adds;
// -1 leaves no instrument of the layer, +1 has every one that is on; coming back to the first value
// gives the page exactly as it was drawn; a value gives the same page whatever the way there;
// over all pages the number of instruments follows the bent chances.
// Build: clang++ -std=c++17 -O2 -I tests/stub -I src/formsBox -I src/formsGL -I <OF>/libs/json/include
//        tests/formsGL/test_density_immediate.cpp -lz
// Run:   ./a.out <data folder copy>
#include "formsEngine.h"
#include "formsLiveEdit.h"
#include <cstdio>
using namespace forms;
int main(int, char** argv){
    Engine e; e.rendererFolder = argv[1]; e.loadAll(); e.useSetlist = false; e.newSession(77);
    int ok = 0, all = 0, fails = 0;
    auto check = [&](bool c, const std::string& what){ all++; if(c) ok++; else if(fails++ < 20) printf("  FAIL %s\n", what.c_str()); };
    double sumHas[5] = {0}, sumExp[5] = {0}; const float ds[5] = {-0.6f, -0.3f, 0.0f, 0.3f, 0.6f};
    int pages = 0;
    for(auto& [name, sc] : e.scenes){
        if(sc.layers.empty() || sc.layers[0].fixed.is_object()) continue;
        const LayerDef& L = sc.layers[0];
        if(L.globalProb < 100) continue;
        bool gated = false; for(float g : L.groupProb) if(g < 1) gated = true;
        e.manualScene = name;
        Engine::OwnScope own(e, &sc);
        auto chances = itemChances(L);
        auto has = [&](const json& r, const std::string& item){
            std::set<std::string> refs;
            if(isPresetRef(item)){ auto it = e.presets.find(item); if(it != e.presets.end()) refs = itemRefs(e, it->second); }
            for(auto& it : r["items"]) if(!it.value("layerEffect", false) && (it.contains("method") ? it["method"] == item : refs.count(it.value("preset", std::string())) > 0)) return true;
            return false;
        };
        auto count = [&](const json& r){ int n = 0; for(auto& [item, p] : chances) if(has(r, item)) n++; return n; };
        for(int k = 0; k < 6; k++){
            json req = readJsonFile(e.makeRequest(true));
            unsigned seed = req.value("seed", 0u);
            pages++;
            json same = patchDensity(e, req, L, 0, seed, 0);
            check(same["items"] == req["items"] && !same.contains("densityBase") && !same.contains("chanceDensity"), name + ": density 0 on a page drawn at 0 changes nothing");
            json lo = patchDensity(e, req, L, -0.5f, seed, 0), lower = patchDensity(e, lo, L, -0.8f, seed, 0), none = patchDensity(e, lower, L, -1, seed, 0);
            json hi = patchDensity(e, none, L, 0.5f, seed, 0), full = patchDensity(e, hi, L, 1, seed, 0);
            for(auto& [item, p] : chances){
                check(!has(lo, item) || has(req, item), name + " " + item + ": going down adds nothing");
                check(!has(lower, item) || has(lo, item), name + " " + item + ": further down adds nothing");
                check(!has(none, item), name + " " + item + ": -100 leaves none");
                check(!has(req, item) || has(hi, item), name + " " + item + ": going up keeps what was there");
                if(p > 0 && !gated) check(has(full, item), name + " " + item + ": +100 has it");
                if(p <= 0) check(!has(full, item), name + " " + item + ": off stays off at +100");
            }
            json back = patchDensity(e, full, L, 0, seed, 0);
            check(back["items"] == req["items"] && !back.contains("densityBase"), name + ": back at 0, the page as it was drawn");
            json direct = patchDensity(e, req, L, 0.3f, seed, 0), around = patchDensity(e, patchDensity(e, patchDensity(e, req, L, -0.7f, seed, 0), L, 0.9f, seed, 0), L, 0.3f, seed, 0);
            check(direct["items"] == around["items"], name + ": the same value gives the same page whatever the way there");
            if(!gated) for(int i = 0; i < 5; i++){
                sumHas[i] += count(patchDensity(e, lo, L, ds[i], seed, 0));
                for(auto& [item, p] : itemChances(withDensity(L, ds[i]))) sumExp[i] += p / 100;
            }
        }
    }
    printf("%d pages; checks %d / %d ok\n", pages, ok, all);
    for(int i = 0; i < 5; i++) printf("density %+4.0f: instruments on the pages %.0f, expected from the chances %.0f\n", ds[i] * 100, sumHas[i], sumExp[i]);
    printf(fails ? "FAIL\n" : "ok\n");
    return fails ? 1 : 0;
}
