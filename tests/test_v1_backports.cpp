// The v1 backports' headless parts (formsUndo.h, formsPerform.h, formsHarmonyText.h,
// formsAssistantV1.h) on a copy of a renderer folder:
//   clang++ -std=c++17 -I src/formsBox -I <OF>/libs/json/include tests/test_v1_backports.cpp && ./a.out <renderer copy>
// The copy is written to (undo of a created / deleted scene writes / removes its file).
#include "formsUndo.h"
#include "formsPerform.h"
#include "formsHarmonyText.h"
#include "formsAssistantV1.h"
#include <iostream>
using namespace forms;

static int fails = 0;
#define CHECK(c) do { if(!(c)){ std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; fails++; } } while(0)

int main(int argc, char** argv){
    if(argc < 2){ std::cerr << "usage: test_v1_backports <renderer copy>\n"; return 2; }
    Engine e; e.rendererFolder = argv[1]; e.loadAll();
    CHECK(!e.scenes.empty());
    std::set<std::string> dirtyS, dirtyP; bool slDirty = false, libDirty = false;
    UndoStack u; u.engine = &e; u.dirtyScenes = &dirtyS; u.dirtyPresets = &dirtyP; u.setlistDirty = &slDirty; u.libraryDirty = &libDirty;
    u.track(false);   // start
    UndoState s0 = captureState(e);
    std::string name = e.scenes.begin()->first;
    std::string bank = e.scenes[name].bank;
    int bpm0 = e.scenes[name].bpm;

    // an edit held (busy) is not a step; let go, it is one
    e.scenes[name].bpm = bpm0 + 7;
    CHECK(!u.track(true));
    CHECK(u.track(false));
    CHECK(u.canUndo());
    // undo: back, unsaved, bank kept
    CHECK(u.undo());
    CHECK(e.scenes[name].bpm == bpm0);
    CHECK(e.scenes[name].bank == bank);
    CHECK(dirtyS.count(name));
    CHECK(captureState(e) == s0);
    CHECK(u.redo());
    CHECK(e.scenes[name].bpm == bpm0 + 7);
    CHECK(u.undo());

    // setlist and presets
    auto sl0 = e.setlist;
    e.setlist.push_back({name, 3});
    u.commit("setlist");
    CHECK(u.undo());
    CHECK(json(e.setlist) == json(sl0) && slDirty);
    if(!e.presets.empty()){
        std::string ref = e.presets.begin()->first;
        json before = e.presets[ref];
        e.presets[ref].params["__test"] = 1;
        u.commit("preset");
        CHECK(u.undo());
        CHECK(json(e.presets[ref]) == before && dirtyP.count(ref));
    }
    // a created scene: undo removes it (and its file), redo brings it back
    Scene ns = e.scenes[name]; ns.name = "zz undo test"; e.scenes[ns.name] = ns; e.saveScene(ns);
    u.commit("new");
    CHECK(u.undo());
    CHECK(!e.scenes.count("zz undo test") && !fs::exists(e.scenePath(ns)));
    CHECK(u.redo());
    CHECK(e.scenes.count("zz undo test") && fs::exists(e.scenePath(ns)));
    CHECK(u.undo());
    // compare with saved: the file against memory
    e.scenes[name].bpm = bpm0 + 1;
    auto lines = diffLines(savedScene(e, e.scenes[name]), json(e.scenes[name]));
    CHECK(lines.size() == 1 && lines[0].find("/bpm") == 0);
    e.reloadScene(name);
    CHECK(e.scenes[name].bpm == bpm0);

    // perform: identity by default, save / load
    PerformState ps;
    CHECK(ps.neutral());
    for(auto& p : perfParams()) if(p.type == 'f'){ float v = (p.min + p.max) * 0.37f; CHECK(ps.apply(p, v) == v); }
    ps.faders[0].gain = 0.5f; ps.snaps[2].used = true; ps.snaps[2].scene = name; ps.snaps[2].setlistEntry = 4;
    PerformState ps2; ps2.load(ps.save());
    CHECK(ps2.faders[0].gain == 0.5f && ps2.snaps[2].used && ps2.snaps[2].scene == name && ps2.snaps[2].setlistEntry == 4 && !ps2.snaps[1].used);
    CHECK(!ps2.switchNow);                                  // Switch now: off by default, saved
    ps.switchNow = true; { PerformState ps3; ps3.load(ps.save()); CHECK(ps3.switchNow); }
    const PerfParam* sg = PerformState::param("SpectrumGain");
    CHECK(sg && ps2.apply(*sg, 2.0f) == 1.0f);

    // typed harmony
    auto h = parseHarmonyText("I vi IV V");
    CHECK(h.error.empty() && h.rootsFor(8) == std::vector<int>({0, 5, 3, 4, 0, 5, 3, 4}));
    h = parseHarmonyText("I:2 vi7 V*2 ii");
    CHECK(h.error.empty() && h.rootsFor(6) == std::vector<int>({0, 0, 5, 4, 4, 1}));
    CHECK(harmonyToText(h.rootsFor(6)) == "I:2 vi V:2 ii");
    CHECK(!parseHarmonyText("I foo").error.empty());
    CHECK(!parseHarmonyText("I:0").error.empty());

    // assistant: prompt from the schema, validation
    std::string sys = assistV1SystemPrompt(e);
    CHECK(!e.gens.empty() && sys.find(e.gens[0].name) != std::string::npos);
    json j;
    CHECK(extractJsonObject("Here:\n```json\n{\"a\": {\"b\": \"}\"}}\n```", j) && j["a"]["b"] == "}");
    if(!e.gens.empty()){
        const GenDef& g = e.gens[0];
        json ok{{"gen", g.name}, {"name", "t"}, {"params", json::object()}};
        CHECK(validatePresetV1(e, ok).empty());
        json bad = ok; bad["params"]["no_such_key"] = 1;
        CHECK(!validatePresetV1(e, bad).empty());
        for(auto& p : g.params) if(p.isNumber()){ json b2 = ok; b2["params"][p.key] = p.max + 1000; CHECK(!validatePresetV1(e, b2).empty()); break; }
        json b3 = ok; b3["gen"] = "nope"; CHECK(!validatePresetV1(e, b3).empty());
    }
    json sc = e.scenes[name];
    auto sv = validateSceneV1(e, sc);
    for(auto& p : sv) std::cerr << "  (existing scene note: " << p << ")\n";
    json sb = sc; sb["palette"] = "no such palette"; CHECK(!validateSceneV1(e, sb).empty());
    json sb2 = sc; sb2["layers"] = json::array(); CHECK(!validateSceneV1(e, sb2).empty());

    std::cout << (fails ? "FAILED " : "ok ") << fails << "\n";
    return fails ? 1 : 0;
}
