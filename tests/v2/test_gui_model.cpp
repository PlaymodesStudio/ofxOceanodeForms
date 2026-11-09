// test_gui_model.cpp — headless tests for v2GuiModel.h (the GUI's non-drawing logic)
#include "v2GuiModel.h"
#include <cstdio>

using namespace forms2;
using namespace forms2::gui;
namespace fs = std::filesystem;

static int passed = 0, failed = 0;
#define CHECK(c) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

static json loadFile(const std::string& p){ std::ifstream f(p); json j; f >> j; return j; }
static std::string here(){ return fs::path(__FILE__).parent_path().string(); }

int main(){
    json scene = loadFile(here() + "/scenes/example_v2.json");
    CHECK(scene.contains("instances"));
    auto ids = instanceIds(scene);
    CHECK(ids.size() == 4);

    // ---- history
    {
        History h;
        CHECK(!h.canUndo());
        h.push("A", "edit 1");
        h.push("A", "edit 1");            // same step twice = one
        CHECK(h.undo.size() == 1);
        h.push("B", "edit 2");
        std::string out;
        CHECK(h.stepBack("C", out) && out == "B");
        CHECK(h.canRedo() && h.redoLabel() == "edit 2");
        CHECK(h.stepForward("B", out) && out == "C");
        CHECK(h.stepBack("C", out) && h.stepBack("B", out) && out == "A");
        CHECK(!h.stepBack("A", out));
        h.push("A", "x");
        CHECK(!h.canRedo());               // a new edit clears redo
    }

    // ---- params
    {
        json s = scene;
        { json t = scene; CHECK(setParam(t, "hats", "look", "nope_key", 1) == ""); CHECK(!validate(sceneDocFromJson(t)).empty()); }
        CHECK(!setParam(s, "nobody", "look", "x", 1).empty());
        CHECK(setParam(s, "bass", "seq", "density", 0.7) == "");
        CHECK(numberOf(*findInstance(s, "bass"), "seq", "density") == 0.7);
        CHECK(!setParam(s, "bass", "seq", "density", 7.0).empty());   // out of range
        CHECK(!setParam(s, "bass", "seq", "mode", "bogus").empty());
        CHECK(setField(s, "bass", "layer", 2) == "");
        CHECK(!setField(s, "bass", "layer", 9).empty());
        // origin overrides
        Preset p; p.type = "hats"; p.name = "ticks";
        std::string nid;
        CHECK(addInstanceFromPreset(s, p, -1, &nid) == "");
        CHECK(nid == "hats_2");
        CHECK(setParam(s, nid, "seq", "density", 0.4) == "");
        CHECK(changesFromPreset(*findInstance(s, nid)) == 1);
        CHECK(findInstance(s, nid)->at("origin").value("preset", std::string()) == "ticks");
        CHECK(validate(sceneDocFromJson(s)).empty());
        CHECK(removeInstance(s, nid) == "");
        CHECK(findInstance(s, nid) == nullptr);
        CHECK(duplicateInstance(s, "kick", &nid) == "" && nid == "kick_2");
        {   // order (lane drag): moves within a layer only
            json t = s;
            auto order = [&](){ std::vector<std::string> o; for(auto& in : t["instances"]) o.push_back(in.value("id", std::string())); return o; };
            auto o0 = order();
            std::string a = o0.back(), b2;
            int la = findInstance(t, a)->value("layer", 0);
            for(auto& x : o0) if(x != a && findInstance(t, x)->value("layer", 0) == la){ b2 = x; break; }
            if(!b2.empty()){
                CHECK(moveInstance(t, a, b2) == "");
                auto o1 = order();
                CHECK(std::find(o1.begin(), o1.end(), a) < std::find(o1.begin(), o1.end(), b2));
                CHECK(o1.size() == o0.size() && validate(sceneDocFromJson(t)).empty());
            }
            for(auto& x : o0) if(findInstance(t, x)->value("layer", 0) != la){ CHECK(!moveInstance(t, a, x).empty()); break; }
            CHECK(!moveInstance(t, "nobody", a).empty());
        }
    }

    // ---- routings and the matrix
    {
        json s = scene;
        MatrixView v0 = buildMatrix(s);
        int before = v0.routingCount;
        Routing r = newSource("random", 1);
        r.dest = "depth";
        size_t idx = 0;
        CHECK(addRouting(s, "hats", r, &idx) == "");
        MatrixView v = buildMatrix(s);
        CHECK(v.routingCount == before + 1);
        int row = -1; for(size_t i = 0; i < v.rowKeys.size(); i++) if(v.rowKeys[i] == sourceKey(r)) row = (int)i;
        CHECK(row >= 0);
        int col = v.colOf("hats", "depth");
        CHECK(col >= 0 && v.cells[(size_t)row][(size_t)col].used);
        // the same source to size: same row
        CHECK(connectCell(s, v, row, col) == "");     // duplicate cell allowed by the format
        Routing r2 = r; r2.dest = "size";
        CHECK(addRouting(s, "hats", r2) == "");
        MatrixView v2 = buildMatrix(s);
        int rows = 0; for(auto& k : v2.rowKeys) if(k == sourceKey(r)) rows++;
        CHECK(rows == 1);
        // levels
        RoutingRef ref{"hats", idx};
        Routing got;
        CHECK(getRouting(s, ref, got) && got.dest == "depth");
        got.level = LevelScene;                       // depth: event / phrase / layer only
        CHECK(!setRouting(s, ref, got).empty());
        got.level = LevelLayer;
        CHECK(setRouting(s, ref, got) == "");
        setRoutingRender(got, "animated");
        CHECK(setRouting(s, ref, got) == "");        // depth may be animated
        CHECK((allowedLevels(s, "hats", "depth") & LevelScene) == 0);
        CHECK((allowedLevels(s, "hats", "depth") & LevelEvent) != 0);
        Routing bad = r; bad.dest = "look.cell"; setRoutingRender(bad, "animated");
        CHECK(!checkRouting(s, "hats", bad).empty());
        Routing sq = r; sq.dest = "seq.density"; sq.level = LevelPhrase;
        CHECK(addRouting(s, "hats", sq) == "");
        Routing sqBad = sq; sqBad.dest = "seq.nothing";
        CHECK(!addRouting(s, "hats", sqBad).empty());
        CHECK(removeRouting(s, ref) == "");
        CHECK(!removeRouting(s, RoutingRef{"hats", 999}).empty());
        CHECK(validate(sceneDocFromJson(s)).empty());
        // filter
        MatrixView vf = buildMatrix(s, "hats");
        for(auto& c : vf.cols) CHECK(c.owner == "hats" || c.owner.empty());
        MatrixView va = buildMatrix(s, "hats", true);
        CHECK(va.cols.size() > vf.cols.size());
        CHECK(sourceLabel(newSource("lfo", 2)).find("LFO 2") == 0);
    }

    // ---- diff and labels
    {
        json a = scene, b = scene;
        setParam(b, "bass", "seq", "density", 0.9);
        Routing r = newSource("random", 1); r.dest = "brightness";
        addRouting(b, "hats", r);
        auto d = describeChanges(a, b);
        bool dens = false, rout = false;
        for(auto& s : d){ if(s.find("Density") != std::string::npos) dens = true; if(s.find("+ Random 1") != std::string::npos) rout = true; }
        CHECK(dens && rout);
        CHECK(describeChanges(a, a).empty());
        CHECK(fmtNum(0.50) == "0.5" && fmtNum(3.0) == "3");
        CHECK(!followSentence("chord").empty());
        CHECK(phraseLengthText(*findInstance(a, "pad")) == "16 beats");
    }

    // ---- step grid
    {
        json s = scene;
        setParam(s, "kick", "seq", "mode", "pattern");
        setParam(s, "kick", "seq", "pattern", "X.x.");
        setParam(s, "kick", "seq", "steps", 8);
        auto v = stepPreview(*findInstance(s, "kick"));
        CHECK(v.size() == 8 && v[0] == 2 && v[1] == 0 && v[2] == 1 && v[4] == 2);
        CHECK(toggleStep(s, "kick", 1) == "");
        CHECK(valueOf(*findInstance(s, "kick"), "seq", "pattern") == "Xxx.X.x.");
        setParam(s, "kick", "seq", "mode", "euclidean");
        setParam(s, "kick", "seq", "hits", 4);
        setParam(s, "kick", "seq", "steps", 16);
        auto e = stepPreview(*findInstance(s, "kick"));
        int n = 0; for(int x : e) n += x > 0;
        CHECK(e.size() == 16 && n == 4);
    }

    // ---- library (a temp presets_v2 folder, removed afterwards)
    {
        fs::path tmp = fs::temp_directory_path() / "gui_model_test" / "presets_v2";
        std::error_code ec; fs::remove_all(tmp.parent_path(), ec);
        Library lib; lib.dir = tmp.string();
        CHECK(Library::writable(lib.dir));
        Library bad; bad.dir = (fs::temp_directory_path() / "elsewhere").string();
        CHECK(!bad.ensureDir());
        int n = lib.seedFromScene(scene, "example");
        CHECK(n == 4);
        CHECK(lib.seedFromScene(scene, "example") == 0);   // idempotent
        std::vector<std::string> tmpl;
        for(auto& f : fs::directory_iterator(here() + "/templates")) tmpl.push_back(f.path().string());
        int m = lib.seedFromFiles(tmpl);
        CHECK(m > 0);
        Kit k; k.name = "Boom kit"; k.tags = {"drums"};
        k.instances.push_back(instanceFromJson(*findInstance(scene, "kick")));
        k.instances.push_back(instanceFromJson(*findInstance(scene, "hats")));
        { std::ofstream o((tmp / "boom_kit.json").string()); o << toJson(k).dump(2); }
        { std::ofstream o((tmp / "broken.json").string()); o << "{nope"; }
        lib.scan();
        CHECK((int)lib.items.size() == n + m + 1);
        CHECK(lib.errors.size() == 1);
        CHECK(lib.items.back().kit && lib.items.back().count == 2);
        auto fc = familyCounts(lib);
        CHECK(fc["kit"] == 1 && fc["percussive"] >= 2);
        LibraryFilter f; f.families = {"percussive"};
        int perc = 0; for(auto& i : lib.items) if(f.match(i)) perc++;
        CHECK(perc == fc["percussive"]);
        f = LibraryFilter{}; f.text = "example_hats";
        int hits = 0; for(auto& i : lib.items) if(f.match(i)) hits++;
        CHECK(hits == 1);
        f = LibraryFilter{}; f.source = "factory";
        int fact = 0; for(auto& i : lib.items) if(f.match(i)) fact++;
        CHECK(fact == n + m);
        CHECK(!allTags(lib).empty());
        // drop a preset and a kit into a scene
        json s = scene;
        const LibraryItem* hatsItem = nullptr; const LibraryItem* kitItem = nullptr;
        for(auto& i : lib.items){ if(i.name == "example_hats") hatsItem = &i; if(i.kit) kitItem = &i; }
        CHECK(hatsItem && kitItem);
        std::vector<std::string> added;
        CHECK(dropItem(s, *hatsItem, 3, &added) == "" && added.size() == 1);
        CHECK(dropItem(s, *kitItem, -1, &added) == "" && added.size() == 3);
        CHECK(validate(sceneDocFromJson(s)).empty());
        CHECK(replaceWith(s, "bass", *hatsItem) == "");
        CHECK(findInstance(s, "bass")->value("type", std::string()) == "hats");
        CHECK(findInstance(s, "bass")->value("layer", 0) == 1);
        CHECK(!replaceWith(s, "bass", *kitItem).empty());
        // save as new preset
        Preset p = presetFromInstance(instanceFromJson(*findInstance(scene, "pad")), nullptr, "My pad");
        std::string path = lib.savePreset(p);
        CHECK(!path.empty() && fs::exists(path));
        std::string path2 = lib.savePreset(p);
        CHECK(path2 != path);                                 // never over an existing file
        fs::remove_all(tmp.parent_path(), ec);
    }

    // ---- params lists
    {
        CHECK(!instanceParams("hats", false).empty());
        CHECK(performanceKnobs("chords").size() <= 6);
        CHECK(!seqParams("rhythm").empty());
        CHECK(Library::defaultDir().find("tests/v2/presets_v2") != std::string::npos);
    }

    std::printf("test_gui_model: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
