//
//  test_legacy.cpp — ESPECTRES v2 legacy mapping (ROADMAP 5.5)
//  Every v1 scene maps, validates, survives a file round trip, loads in the engine, and the mapping
//  is deterministic (two runs give byte-identical files). Also checks a few mapping rules.
//
//  Build: clang++ -std=c++17 -O2 -I src/formsV2 -I <of>/libs/json/include tests/v2/test_legacy.cpp
//  Run:   ./test_legacy [<v1 sketch dir>]
//

#include "v2Engine.h"
#include "v2Legacy.h"
#include <cstdio>

using namespace forms2;
namespace L = forms2::legacy;

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if(!(c)){ fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while(0)

int main(int argc, char** argv){
    std::string dir = argc > 1 ? argv[1] : "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027";
    L::Library lib;
    CHECK(L::loadLibrary(dir, lib));
    if(fails) return 1;

    // rules on single presets
    {
        std::set<std::string> used; L::MapCtx c; c.used = &used; c.bars = 8; c.imageBeats = 32; c.scaleSize = 5;
        auto r = L::mapPreset("hits/batucada", lib, c);
        CHECK(r.instances.size() == 1);
        CHECK(r.instances[0].seq.value("mode", "") == "euclidean");
        CHECK(r.instances[0].seq.value("steps", 0) == 16);
        CHECK(r.instances[0].seq.value("patternLength", 0) == 8);
        CHECK((r.instances[0].seq.value("hitsList", std::vector<int>{}) == std::vector<int>{2, 3, 5}));   // fills [2, 3, 5] -> one of them per phrase (hitsList)
        auto t = L::mapPreset("pulse/trance bass", lib, c);
        CHECK(!t.instances.empty() && t.instances[0].type == "bass");
        CHECK(t.confidence > 0 && t.confidence <= 1);
        auto e = L::mapPreset("effects/band pass 4th", lib, c);
        CHECK(e.instances.size() == 1 && e.instances[0].type == "filterfx" && e.instances[0].params.value("effect", "") == "band");
        auto m = L::mapPreset("no/such preset", lib, c);
        CHECK(m.instances.empty() && m.confidence == 0 && !m.unmapped.empty());
        CHECK(L::snapDegree(7, {0, 2, 3, 7, 10}) == 3);
        CHECK(L::snapDegree(2, {0, 1, 5, 7, 10}) == 1);
        CHECK((L::foldScale({0, 3, 7, 10, 14}) == std::vector<int>{0, 2, 3, 7, 10}));
        // v1 per-bar transposes -> step key shifts (roots stay degrees); a constant shift folds into the tonic
        auto ks = L::makeSection("major", {0, 2, 4, 5, 7, 9, 11}, 2, {}, 16, {0, 1, 1, 5});
        CHECK(ks.steps.size() == 3 && ks.steps[0].keyShift == 0 && ks.steps[1].keyShift == 1 && ks.steps[1].length == 8 && ks.steps[2].keyShift == 5 && ks.steps[1].degree == 0 && ks.scale.tonic == 2);
        auto k1 = L::makeSection("major", {0, 2, 4, 5, 7, 9, 11}, 2, {}, 16, {3});
        CHECK(k1.steps.size() == 1 && k1.steps[0].keyShift == 0 && k1.scale.tonic == 5 && k1.steps[0].length == 16);
    }

    // every scene
    int n = 0, valid = 0;
    for(auto& f : std::filesystem::directory_iterator(dir + "/scenes")){
        if(f.path().extension() != ".json") continue;
        json v1; CHECK(L::readJsonFile(f.path().string(), v1));
        std::string name = f.path().stem().string();
        auto a = L::mapScene(v1, lib, name), b = L::mapScene(v1, lib, name);
        n++;
        if(a.errors.empty()) valid++; else std::printf("  %s: %s\n", name.c_str(), a.errors[0].c_str());
        CHECK(L::sceneFile(a).dump() == L::sceneFile(b).dump());
        bool cells = false;
        for(auto& l : v1.value("layers", json::array())) for(auto& c : l.value("cells", json::array())) if(!c.value("item", "").empty()) cells = true;
        CHECK(!cells || !a.doc.scene.instances.empty());
        CHECK(validate(sceneDocFromJson(L::sceneFile(a))).empty());
        Engine eng; CHECK(eng.loadJson(L::sceneFile(a)));
        CHECK(a.doc.scene.harmony.sections.size() == 1 && !a.doc.scene.harmony.sections[0].steps.empty());
        CHECK(a.confidence >= 0 && a.confidence <= 1);
    }
    CHECK(n == 35);
    CHECK(valid == n);
    std::printf("%s: %d scenes, %d valid, %d checks, %d failures\n", fails ? "FAILED" : "PASSED", n, valid, checks, fails);
    return fails ? 1 : 0;
}
