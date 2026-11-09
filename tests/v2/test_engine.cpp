// test_engine.cpp — headless tests for v2Engine.h and v2Assistant.h
#include "v2Assistant.h"
#include "v2Draw.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace forms2;

static int passed = 0, failed = 0;
#define CHECK(c) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

// the example scene (also tests/v2/scenes/example_v2.json)
static const char* kScene = R"JSON({
  "format": "espectres.scene",
  "formatVersion": 1,
  "name": "example v2",
  "version": 1,
  "bpm": 96,
  "seed": 2027,
  "harmony": {
    "sections": [
      {
        "name": "A",
        "meter": {"num": 4, "den": 4},
        "scale": {"name": "natural minor", "semis": [0, 2, 3, 5, 7, 8, 10], "tonic": 9},
        "progression": "I vi IV V",
        "steps": [
          {"degree": 0, "length": 4}, {"degree": 5, "length": 4},
          {"degree": 3, "length": 4}, {"degree": 4, "length": 4}
        ],
        "lengthBeats": 16
      },
      {
        "name": "B",
        "meter": {"num": 7, "den": 8, "grouping": [2, 2, 3]},
        "scale": {"name": "natural minor", "semis": [0, 2, 3, 5, 7, 8, 10], "tonic": 9},
        "progression": "I:2 V:2",
        "steps": [{"degree": 0, "length": 14}, {"degree": 4, "length": 14}]
      }
    ]
  },
  "layers": [
    {"zoomBeats": 16, "blend": "lighten", "gain": 1},
    {"zoomBeats": 16, "blend": "lighten", "gain": 1},
    {"zoomBeats": 8, "blend": "add", "gain": 1},
    {"zoomBeats": 16, "blend": "lighten", "gain": 1}
  ],
  "instances": [
    {
      "id": "pad", "type": "chords", "layer": 0, "follow": "chord",
      "params": {"look": "additive", "thickness": 10, "partials": 6, "bloom": 0.5, "colourRole": "highlight"},
      "seq": {"mode": "pattern", "pattern": "x", "steps": 1, "pitchSource": "chord", "voices": 4, "voicing": "open", "hold": true, "level": 0.6},
      "phrase": {"length": 16}
    },
    {
      "id": "bass", "type": "bass", "layer": 1, "follow": "chord",
      "params": {"thickness": 16, "partials": 3, "colourRole": "bass"},
      "seq": {"mode": "probability", "density": 0.45, "pitchSource": "scale", "scaleMode": "walk", "range": 5, "octave": -1, "noteLength": 0.8},
      "phrase": {"length": 8, "variation": {"rhythm": "new", "pitch": "mutate", "amount": 0.3}}
    },
    {
      "id": "kick", "type": "kick", "layer": 2, "follow": "off",
      "params": {"cell": "taiko", "width": 0.3},
      "seq": {"mode": "euclidean", "hits": 3, "rotation": 0},
      "phrase": {"length": 4}
    },
    {
      "id": "hats", "type": "hats", "layer": 3, "follow": "off",
      "params": {"cell": "dot", "dotSize": 3, "scatter": 0.6},
      "seq": {"mode": "probability", "density": 0.6, "steps": 16},
      "phrase": {"length": 8},
      "routings": [
        {"source": "random1", "dest": "depth", "amount": 1, "level": "event", "min": 0.2, "max": 3,
         "sourceParams": {"scope": "event"}}
      ]
    }
  ],
  "routings": [
    {"source": "macro1", "dest": "hats:seq.level", "amount": 1, "level": "event", "min": 0.2, "max": 1},
    {"source": "macro2", "dest": "layer0.zoom", "amount": 1, "level": "layer", "min": 16, "max": 4}
  ],
  "macros": {"macro1": 0.5, "macro2": 0}
}
)JSON";

static json sceneJson(){ return json::parse(kScene); }

// runs an engine from `from` to `to` in frame steps, calling f(now) after each update
template<class F>
static void run(Engine& e, double from, double to, double step, F f){
    for(double t = from; t <= to + 1e-9; t += step){ e.update(t); f(t); }
}
static void run(Engine& e, double from, double to, double step){ run(e, from, to, step, [](double){}); }

// a signature of every stored event (for determinism)
static std::string signature(const Engine& e){
    std::ostringstream o;
    for(int l = 0; l < 4; l++) for(auto& s : e.events(l))
        o << l << ' ' << s.e.instance << ' ' << s.e.id << ' ' << s.e.start << ' ' << s.e.length << ' '
          << s.e.degree << ' ' << s.e.depth << ' ' << s.e.level << ';';
    return o.str();
}
// onsets of one phrase of an instance, relative to the phrase start
static std::vector<double> onsets(Engine& e, const std::string& id, int k){
    std::vector<double> v; Phrase ph = e.phrase(id, k);
    for(auto* s : e.eventsOf(id)) if(s->phrase == k) v.push_back(std::round((s->e.start - ph.start) * 1000) / 1000);
    std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
static Instance mkFollower(const std::string& id, const std::string& target, int layer){
    Instance i; i.id = id; i.type = "snare"; i.layer = layer; i.follow = Follow::Off;
    i.seq = {{"mode", "follow"}, {"followId", target}, {"followMode", "skipped"}};
    i.phrase = {{"length", 4}};
    return i;
}

static void testEngine(){
    // ---- load
    Engine e;
    CHECK(e.loadJson(sceneJson()));
    for(auto& p : e.problems()) std::printf("  problem: %s\n", p.c_str());
    CHECK(e.ready());
    {   // the scene file matches the embedded scene
        std::ifstream f("tests/v2/scenes/example_v2.json");
        if(f){ std::stringstream ss; ss << f.rdbuf(); CHECK(json::parse(ss.str()) == sceneJson()); }
        else std::printf("  (scene file not found from this directory; skipped)\n");
    }
    // harmony: typed progression I vi IV V, then 7/8
    CHECK(e.harmonyAt(1).root == 0); CHECK(e.harmonyAt(5).root == 5); CHECK(e.harmonyAt(9).root == 3); CHECK(e.harmonyAt(13).root == 4);
    CHECK(e.meterAt(17).num == 7 && e.meterAt(17).den == 8);
    CHECK(e.harmonyAt(31).root == 4);
    {   // grid: section start at 16, 7/8 bars of 7 beats with strong beats 2+2+3
        auto g = e.grid(0, 31);
        bool sec16 = false, bar23 = false, strong18 = false, strong20 = false, weak19 = false;
        for(auto& l : g){
            if(std::fabs(l.beat - 16) < 1e-6 && l.kind == 0) sec16 = true;
            if(std::fabs(l.beat - 23) < 1e-6 && l.kind == 1) bar23 = true;
            if(std::fabs(l.beat - 18) < 1e-6 && l.kind == 2) strong18 = true;
            if(std::fabs(l.beat - 20) < 1e-6 && l.kind == 2) strong20 = true;
            if(std::fabs(l.beat - 19) < 1e-6 && l.kind == 3) weak19 = true;
        }
        CHECK(sec16); CHECK(bar23); CHECK(strong18); CHECK(strong20); CHECK(weak19);
    }
    // depth floor from the hats' random depth routing
    CHECK(std::fabs(e.minDepth("hats") - 0.2) < 1e-9);
    CHECK(std::fabs(e.minDepth("kick") - 1) < 1e-9);

    // ---- run over 64 beats at a few frame steps: coverage, depth, no phrase twice
    const char* ids[] = {"pad", "bass", "kick", "hats"};
    std::string sigs[3];
    double steps[3] = {1.0 / 30, 0.37, 1.0};
    for(int si = 0; si < 3; si++){
        Engine r; r.loadJson(sceneJson());
        bool covered = true, depthOk = true, hatsSpread = false, dropOk = true;
        int checks = 0;
        run(r, 0, 64, steps[si], [&](double t){
            for(auto id : ids){
                const Instance* in = r.instance(id);
                LayerView v = r.viewFor(in->layer, t);
                double need = t + lookAheadBeats(v, r.minDepth(id));
                if(r.plannedUntil(id) < need - 1e-6) covered = false;
                checks++;
            }
            for(int l = 0; l < 4; l++){
                LayerView v = r.viewFor(l, t);
                double md = l == 3 ? 0.2 : 1;
                double limit = t - v.headerX / (v.pxPerBeat * md) - r.options.ringOutBeats - 1e-6;
                for(auto& s : r.events(l)){
                    double lo = r.minDepth(s.e.instance), hi = s.e.instance == "hats" ? 3 : 1;
                    if(s.e.depth < lo - 1e-9 || s.e.depth > hi + 1e-9) depthOk = false;
                    if(s.e.instance == "hats" && s.e.depth < 0.6) hatsSpread = true;
                    if(s.e.start + s.e.length + s.e.param("release", 0) < limit - 1) dropOk = false;
                }
            }
        });
        CHECK(covered); CHECK(depthOk); CHECK(hatsSpread); CHECK(dropOk); CHECK(checks > 0);
        bool once = true;
        for(auto id : ids) for(auto& kv : r.planCounts(id)) if(kv.second != 1) once = false;
        CHECK(once);
        r.update(64);   // end every run on the same beat
        CHECK(r.stats().replannedPhrases == 0);
        // old events are dropped: nothing on the kick layer (zoom 8) from the first half
        bool old = false; for(auto& s : r.events(2)) if(s.e.start < 40) old = true;
        CHECK(!old);
        for(int l = 0; l < 4; l++) CHECK(r.stats().liveEvents[l] > 0);
        sigs[si] = signature(r);
        if(si == 0) std::printf("  64 beats at 1/30: %ld phrases, live events %d %d %d %d, plan %.3f ms total, max %.3f ms\n",
                                r.stats().plannedPhrases, r.stats().liveEvents[0], r.stats().liveEvents[1], r.stats().liveEvents[2],
                                r.stats().liveEvents[3], r.stats().totalPlanMs, r.stats().maxPlanMs);
    }
    // determinism: two runs identical, and the frame step does not change the events
    {
        Engine a, b; a.loadJson(sceneJson()); b.loadJson(sceneJson());
        run(a, 0, 64, 1.0 / 30); run(b, 0, 64, 1.0 / 30);
        CHECK(signature(a) == signature(b));
        CHECK(!signature(a).empty());
        CHECK(sigs[0] == sigs[1]); CHECK(sigs[0] == sigs[2]);
    }
    // pad follows the chord: its notes at beat 4 hold the vi chord (root degree 5 within the octave)
    {
        Engine a; a.loadJson(sceneJson()); run(a, 0, 8, 0.5);
        bool found = false, ok = true;
        for(auto& s : a.events(0)) if(std::fabs(s.e.start - 4) < 1e-6){ found = true; int d = ((s.e.degree % 7) + 7) % 7; if(d != 5 && d != 0 && d != 2) ok = false; }
        CHECK(found); CHECK(ok);
    }

    // ---- lock keeps the rhythm, reroll changes it (hats: probability rhythm, 4/4 section)
    {
        Engine base; base.loadJson(sceneJson()); run(base, 0, 15, 0.25);
        auto b0 = onsets(base, "hats", 0), b1 = onsets(base, "hats", 1);
        CHECK(b0 != b1);   // variation "new": phrases differ by default

        Engine lk; lk.loadJson(sceneJson()); lk.update(0.5);
        lk.setLocked("hats", true);
        run(lk, 0.75, 15, 0.25);
        CHECK(lk.locked("hats"));
        CHECK(onsets(lk, "hats", 0) == b0);
        CHECK(onsets(lk, "hats", 1) == b0);

        Engine rr; rr.loadJson(sceneJson()); rr.update(0.5);
        rr.reroll("hats");
        run(rr, 0.75, 15, 0.25);
        CHECK(onsets(rr, "hats", 0) == b0);
        CHECK(onsets(rr, "hats", 1) != b1);
        CHECK(!onsets(rr, "hats", 1).empty());
        // determinism of reroll
        Engine rr2; rr2.loadJson(sceneJson()); rr2.update(0.5); rr2.reroll("hats"); run(rr2, 0.75, 15, 0.25);
        CHECK(signature(rr) == signature(rr2));
    }

    // ---- mute removes; solo hides the others
    {
        Engine m; m.loadJson(sceneJson()); run(m, 0, 10, 0.25);
        int cur = m.phraseIndexAt("kick", 10);
        m.setMuted("kick", true);
        run(m, 10.25, 20, 0.25);
        LayerView v = m.viewFor(2, 20);
        CHECK(m.visible(2, v).empty());
        bool future = false; for(auto* s : m.eventsOf("kick")) if(s->phrase > cur) future = true;
        CHECK(!future);
        CHECK(m.muted("kick"));
        m.setMuted("kick", false); run(m, 20.25, 24, 0.25);
        CHECK(!m.visible(2, m.viewFor(2, 24)).empty());
        m.setSolo("pad", true);
        CHECK(m.visible(3, m.viewFor(3, 24)).empty());
        CHECK(!m.visible(0, m.viewFor(0, 24)).empty());
    }

    // ---- live edit re-plans only future phrases
    {
        Engine base; base.loadJson(sceneJson()); run(base, 0, 30, 0.25);
        Engine ed; ed.loadJson(sceneJson()); run(ed, 0, 20, 0.25);
        int cur = ed.phraseIndexAt("bass", 20);
        CHECK(cur == 2);
        auto before = onsets(ed, "bass", cur);
        Instance b = *ed.instance("bass");
        b.seq["density"] = 1.0;
        CHECK(ed.setInstance(b));
        run(ed, 20.25, 30, 0.25);
        CHECK(onsets(ed, "bass", cur) == before);
        CHECK(onsets(ed, "bass", cur + 1) != onsets(base, "bass", cur + 1));
        CHECK(onsets(ed, "bass", cur + 1).size() > onsets(base, "bass", cur + 1).size());
        CHECK(ed.planCounts("bass")[cur] == 1);
        CHECK(ed.planCounts("bass")[cur + 1] >= 1);
        // replanCurrent re-plans the current phrase too
        Instance b2 = b; b2.seq["density"] = 0.1;
        int c2 = ed.phraseIndexAt("bass", 30), n2 = ed.planCounts("bass")[c2];
        CHECK(ed.setInstance(b2, true));
        ed.update(30.25);
        CHECK(ed.planCounts("bass")[c2] == n2 + 1);
        // an invalid edit is refused
        Instance bad = b; bad.params["nope"] = 1;
        CHECK(!ed.setInstance(bad));
        // phrase length edit: the current phrase stays, the next starts at its end
        Instance b3 = b; b3.phrase["length"] = 4;
        int c3 = ed.phraseIndexAt("bass", 30.25);
        Phrase pc = ed.phrase("bass", c3);
        CHECK(ed.setInstance(b3));
        run(ed, 30.5, 40, 0.25);
        CHECK(std::fabs(ed.phrase("bass", c3 + 1).start - pc.end()) < 1e-9);
        CHECK(std::fabs(ed.phrase("bass", c3 + 1).length - 4) < 1e-9);
    }

    // ---- macros reach destinations
    {
        Engine m0, m1; m0.loadJson(sceneJson()); m1.loadJson(sceneJson());
        m0.setMacro("macro1", 0); m1.setMacro("macro1", 1);
        run(m0, 0, 8, 0.5); run(m1, 0, 8, 0.5);
        double hi0 = 0, lo1 = 1; int n0 = 0, n1 = 0;
        for(auto* s : m0.eventsOf("hats")){ hi0 = std::max(hi0, s->e.level); n0++; }
        for(auto* s : m1.eventsOf("hats")){ lo1 = std::min(lo1, s->e.level); n1++; }
        CHECK(n0 > 0 && n1 > 0);
        CHECK(hi0 <= 0.2 + 1e-9);
        CHECK(lo1 >= 1 - 1e-9);
        // layer zoom (scene routing to layer0.zoom at layer level)
        CHECK(std::fabs(m0.layerZoom(0, 8) - 16) < 1e-9);
        m0.setMacro("macro2", 1);
        CHECK(std::fabs(m0.layerZoom(0, 8) - 4) < 1e-9);
        CHECK(std::fabs(m0.layerZoom(1, 8) - 16) < 1e-9);
        CHECK(std::fabs(m0.viewFor(0, 8).pxPerBeat - 1920.0 / 4) < 1e-9);
    }

    // ---- drawn / animated callbacks
    {
        json j = sceneJson();
        j["instances"][3]["routings"].push_back({{"source", "lfo1"}, {"dest", "brightness"}, {"amount", 0.5}, {"level", "layer"},
                                                 {"render", "animated"}, {"sourceParams", {{"shape", "square"}, {"rateBeats", 1}}}});
        j["instances"][1]["routings"] = json::array({ json{{"source", "noteEnv1"}, {"dest", "pitchFine"}, {"amount", 0.25}, {"level", "layer"},
                                                 {"sourceParams", {{"shape", "rise"}}}} });
        Engine a; CHECK(a.loadJson(j)); run(a, 0, 8, 0.5);
        CHECK(a.hasAnimated("hats"));
        auto hv = a.visible(3, a.viewFor(3, 8));
        CHECK(!hv.empty());
        if(!hv.empty()){
            auto fn = a.animatedFn(*hv[0].ev);
            double x = fn("brightness", 1, 8.25), y = fn("brightness", 1, 8.75);
            CHECK(std::fabs(x - 2) < 1e-9); CHECK(std::fabs(y - 1) < 1e-9);
            CHECK(std::fabs(a.drawnFn(*hv[0].ev)("brightness", 1, 0.5) - 1) < 1e-9);   // animated is not drawn
        }
        auto bv = a.events(1);
        CHECK(!bv.empty());
        if(!bv.empty()){
            auto fn = a.drawnFn(bv.front());
            CHECK(std::fabs(fn("pitchFine", 0, 0) - 0) < 1e-9);
            CHECK(std::fabs(fn("pitchFine", 0, 1) - 100) < 1e-9);
        }
    }

    // ---- Follow order and cycles
    {
        json j = sceneJson();
        Instance g = mkFollower("ghost", "kick", 2);
        j["instances"].insert(j["instances"].begin(), toJson(g));   // listed before its target
        Engine f; CHECK(f.loadJson(j));
        for(auto& p : f.problems()) std::printf("  problem: %s\n", p.c_str());
        auto order = f.planOrder();
        auto pos = [&](const std::string& id){ return std::find(order.begin(), order.end(), id) - order.begin(); };
        CHECK(pos("kick") < pos("ghost"));
        CHECK(f.cycles().empty());
        run(f, 0, 32, 0.25);
        std::set<long> kicks; for(auto* s : f.eventsOf("kick")) kicks.insert(std::lround(s->e.start * 1000));
        bool clash = false; int ng = 0;
        for(auto* s : f.eventsOf("ghost")){ ng++; if(kicks.count(std::lround(s->e.start * 1000))) clash = true; }
        CHECK(ng > 0); CHECK(!clash); CHECK(!kicks.empty());
        bool once = true; for(auto& kv : f.planCounts("kick")) if(kv.second != 1) once = false;
        CHECK(once);

        json c = sceneJson();
        c["instances"].push_back(toJson(mkFollower("a", "b", 2)));
        c["instances"].push_back(toJson(mkFollower("b", "a", 2)));
        Engine cy; CHECK(cy.loadJson(c));
        CHECK(cy.cycles().size() == 1);
        CHECK(!cy.warnings().empty());
        run(cy, 0, 16, 0.5);
        CHECK(!cy.eventsOf("a").empty() || !cy.eventsOf("b").empty());
        bool once2 = true; for(auto id : {"a", "b"}) for(auto& kv : cy.planCounts(id)) if(kv.second != 1) once2 = false;
        CHECK(once2);
    }

    // ---- harmony override (jump) re-plans future phrases on the new root
    {
        Engine h; h.loadJson(sceneJson()); run(h, 0, 2, 0.5);
        HarmonyOverride o; o.jumpDegree = 2;
        h.setHarmonyOverride(o, 2);
        CHECK(h.harmonyAt(3).root == 2);
        run(h, 2.5, 20, 0.5);
        bool ok = true; for(auto* s : h.eventsOf("pad")) if(s->phrase >= 1 && ((s->e.degree % 7) + 7) % 7 != 2 && ((s->e.degree % 7) + 7) % 7 != 4 && ((s->e.degree % 7) + 7) % 7 != 6) ok = false;
        CHECK(ok);
    }

    // ---- invalid scenes are refused
    {
        json j = sceneJson(); j["instances"][0]["params"]["bogus"] = 1;
        Engine x; CHECK(!x.loadJson(j)); CHECK(!x.problems().empty()); CHECK(!x.ready());
        json k = sceneJson(); k["harmony"]["sections"][0]["progression"] = "I IX";
        Engine y; CHECK(!y.loadJson(k));
        json s = sceneJson(); s["instances"][3]["routings"][0]["source"] = "wobbler";
        Engine z; CHECK(!z.loadJson(s));
        Engine w; CHECK(!w.loadText("{ not json"));
    }
}

static void testAssistant(){
    std::string sp = systemPrompt();
    bool all = true;
    for(auto& t : instrumentTypes()){
        if(sp.find("### " + t.name + " ") == std::string::npos){ all = false; std::printf("  missing type %s\n", t.name.c_str()); }
        for(auto& p : t.params) if(sp.find("- " + p.key + " (") == std::string::npos){ all = false; std::printf("  missing %s.%s\n", t.name.c_str(), p.key.c_str()); }
    }
    CHECK(all);
    CHECK(instrumentTypes().size() == 14);
    bool seqAll = true; for(auto& p : sequencerParams()) if(sp.find("- " + p.key + " (") == std::string::npos) seqAll = false;
    CHECK(seqAll);
    bool evAll = true; for(auto& p : eventParams()) if(sp.find("- " + p.key + " (") == std::string::npos) evAll = false;
    CHECK(evAll);
    for(auto& e : effectTypes()) CHECK(sp.find("- " + e.name + " (") != std::string::npos);
    CHECK(sp.find("I vi IV V") != std::string::npos);
    CHECK(sp.find("animated") != std::string::npos);
    CHECK(sp.find("```json") != std::string::npos);

    // the prompt's examples are valid
    CHECK(checkAnswer(json::parse(assist_detail::exampleScene()), "scene").empty());
    CHECK(checkAnswer(json::parse(assist_detail::examplePreset()), "instrument").empty());
    for(auto& p : checkAnswer(json::parse(assist_detail::exampleScene()), "scene")) std::printf("  example scene: %s\n", p.c_str());
    for(auto& p : checkAnswer(json::parse(assist_detail::examplePreset()), "instrument")) std::printf("  example preset: %s\n", p.c_str());
    { Engine e; CHECK(e.loadText(assist_detail::exampleScene())); }

    // extractJson
    json out;
    CHECK(extractJson("Here you go:\n```json\n{\"a\": 1, \"s\": \"x}y\"}\n```\nEnjoy.", out) && out["a"] == 1 && out["s"] == "x}y");
    CHECK(extractJson("Sure! {\"type\": \"bass\", \"look\": {\"thickness\": 4}} hope it helps", out) && out["look"]["thickness"] == 4);
    CHECK(extractJson("{\"bare\": true}", out) && out["bare"] == true);
    CHECK(extractJson("```\n{\"plain\": [1,2]}\n```", out) && out["plain"].size() == 2);
    CHECK(extractJson("```json\n{\"small\":1}\n```\nand the full one\n```json\n{\"big\":1,\"more\":{\"x\":2}}\n```", out) && out.contains("big"));
    CHECK(extractJson("first {not json} then {\"ok\": 1}", out) && out["ok"] == 1);
    std::string err;
    CHECK(!extractJson("no json here, sorry", out, &err)); CHECK(!err.empty());
    CHECK(!extractJson("```json\n{broken: \n```", out));
    CHECK(!extractJson("[1, 2, 3]", out));

    // checkAnswer
    json scene = sceneJson();
    CHECK(checkAnswer(scene, "scene").empty());
    CHECK(checkAnswer(scene, "").empty());
    { json j = scene; j["instances"][1]["params"]["darkness"] = 0.5; auto p = checkAnswer(j, "scene"); CHECK(!p.empty()); }
    { json j = scene; j["instances"][3]["routings"][0]["level"] = "sometimes"; CHECK(!checkAnswer(j, "scene").empty()); }
    { json j = scene; j["routings"][1]["level"] = "event"; CHECK(!checkAnswer(j, "scene").empty()); }   // layer zoom at event level
    { json j = scene; j["instances"][2]["seq"]["hits"] = 999; CHECK(!checkAnswer(j, "scene").empty()); }
    { json j = scene; j["instances"][2]["seq"]["wobble"] = 1; CHECK(!checkAnswer(j, "scene").empty()); }
    { json j = scene; j["harmony"]["sections"][0]["progression"] = "I vi IX"; CHECK(!checkAnswer(j, "scene").empty()); }
    { json j = scene; j["instances"][0]["type"] = "theremin"; CHECK(!checkAnswer(j, "scene").empty()); }
    CHECK(!checkAnswer(scene, "instrument").empty());
    CHECK(!checkAnswer(json::array(), "scene").empty());
    { json kit = {{"format", "espectres.kit"}, {"formatVersion", 1}, {"name", "k"}, {"instances", json::array({scene["instances"][2], scene["instances"][3]})}};
      CHECK(checkAnswer(kit, "kit").empty()); }
    { json pr = json::parse(assist_detail::examplePreset()); pr["look"]["thickness"] = 500; CHECK(!checkAnswer(pr, "instrument").empty()); }

    // requests
    auto r = buildRequest(AssistMode::Modify, "make the bass darker", scene.dump());
    CHECK(r.kind == "scene"); CHECK(r.expectsJson()); CHECK(r.user.find("make the bass darker") != std::string::npos);
    CHECK(r.user.find("\"bass\"") != std::string::npos); CHECK(!r.system.empty());
    CHECK(buildRequest(AssistMode::CreateInstrument, "glassy arp").kind == "instrument");
    CHECK(buildRequest(AssistMode::CreateKit, "drums").kind == "kit");
    CHECK(buildRequest(AssistMode::CreateScene, "ambient").kind == "scene");
    CHECK(!buildRequest(AssistMode::Explain, "why busy?", scene.dump()).expectsJson());
    CHECK(buildRequest(AssistMode::Fix, "", json::parse(assist_detail::examplePreset()).dump()).kind == "instrument");
    auto rt = retryRequest(r, {"instance 'bass': unknown key 'darkness' for type bass"}, "```json\n{}\n```");
    CHECK(rt.user.find("unknown key 'darkness'") != std::string::npos); CHECK(rt.kind == "scene");
    CHECK(retryRequest({"x"}, "y").user.find("- x") != std::string::npos);
    std::printf("  system prompt: %zu chars\n", sp.size());
}

// ---------------------------------------------------------------- retune (5.13), mass heights, pitch space
static json smallScene(const std::string& retune, const json& extra = json::object()){
    json j = json::parse(R"({"format":"espectres.scene","formatVersion":1,"name":"rt","version":1,"bpm":120,"seed":5,
      "harmony":{"sections":[{"name":"A","meter":{"num":4,"den":4},"scale":{"name":"major","semis":[0,2,4,5,7,9,11],"tonic":0},
        "steps":[{"degree":0,"length":64}]}]},
      "layers":[{"zoomBeats":16,"blend":"lighten","gain":1}],
      "instances":[{"id":"mel","type":"melody","layer":0,"follow":"chord",
        "seq":{"mode":"probability","density":1,"steps":16,"pitchSource":"scale"},"phrase":{"length":8}},
        {"id":"m","type":"mass","layer":0,"follow":"chord","seq":{"mode":"pattern","pattern":"x","steps":1,"massPitch":"harmonics","harmonics":4},"phrase":{"length":8}}],
      "routings":[],"macros":{}})");
    if(!retune.empty()){ j["retune"] = retune; j["retuneBeats"] = 2; j["retuneCurve"] = "linear"; }
    for(auto it = extra.begin(); it != extra.end(); ++it) j[it.key()] = it.value();
    return j;
}
static std::vector<Event> melEvents(Engine& E){ std::vector<Event> v; for(auto s : E.eventsOf("mel")) v.push_back(s->e); return v; }
// a key-only change (same root, step key shift) retunes by semitones
static void testRetuneKeyShift(){
    json j = smallScene("ahead");
    j["harmony"]["sections"][0]["steps"] = json::parse(R"([{"degree":0,"length":8},{"degree":0,"length":56,"keyShift":1}])");
    Engine E; CHECK(E.loadJson(j));
    for(double t = 0; t <= 6; t += 0.5) E.update(t);
    int shifted = 0; for(auto& e : melEvents(E)) if(e.start >= 8 && e.p.count("glideToSemis")) shifted++;
    HarmonyOverride o; o.hold = true;
    E.setHarmonyOverride(o, 6);   // hold the unshifted step: planned shifted notes come back down 1 semitone
    int moved = 0;
    for(auto& e : melEvents(E)) if(e.start >= 8 && e.p.count("retuneStart")){
        moved++;
        CHECK(std::fabs(e.param("retuneFrom", 0) - e.param("retuneTo", 0) - 1) < 1e-9);
        CHECK(std::fabs(e.param("glideToSemis", 0) - e.param("retuneTo", 0)) < 1e-9);
    }
    CHECK(shifted > 0 && moved == shifted);
}
static void testRetune(){
    for(std::string mode : {"new", "ahead", "all"}){
        Engine E; CHECK(E.loadJson(smallScene(mode)));
        for(double t = 0; t <= 8; t += 0.5) E.update(t);
        auto before = melEvents(E);
        CHECK(!before.empty());
        HarmonyOverride o; o.jumpDegree = 3;
        E.setHarmonyOverride(o, 8);
        auto after = melEvents(E);
        int morphed = 0, passedMoved = 0, futureSame = 0, nFuture = 0;
        for(auto& a : after) for(auto& b : before) if(a.id == b.id){
            bool m = a.p.count("retuneStart") > 0;
            morphed += m;
            if(a.start < 8 && a.degree != b.degree) passedMoved++;
            if(a.start >= 8){ nFuture++; if(a.degree == b.degree) futureSame++; }
            if(m){
                CHECK(std::fabs(draw::retuneOffset(a, 8) - (a.param("retuneFrom", 0) - a.param("retuneTo", 0))) < 1e-9);
                CHECK(std::fabs(draw::retuneOffset(a, 10)) < 1e-9 && std::fabs(draw::retuneOffset(a, 9)) > 0);   // reached after 2 beats
            }
        }
        if(mode == "new") CHECK(morphed == 0 && passedMoved == 0);
        if(mode == "ahead") CHECK(morphed > 0 && passedMoved == 0 && nFuture > 0 && futureSame == 0);
        if(mode == "all") CHECK(morphed > 0 && passedMoved > 0 && futureSame == 0);
        // continuous material (mass) never morphs
        for(auto s : E.eventsOf("m")) CHECK(!s->e.p.count("retuneStart"));
        // deterministic
        Engine F; F.loadJson(smallScene(mode)); for(double t = 0; t <= 8; t += 0.5) F.update(t); F.setHarmonyOverride(o, 8);
        auto again = melEvents(F);
        bool same = again.size() == after.size();
        for(size_t i = 0; same && i < again.size(); i++) same = again[i].degree == after[i].degree && again[i].p == after[i].p;
        CHECK(same);
    }
    Engine bad; CHECK(!bad.loadJson(smallScene("sideways")));
}
static void testMassHeights(){
    Engine E; CHECK(E.loadJson(smallScene("")));
    E.update(0);
    std::set<double> hs; int n = 0;
    for(auto s : E.eventsOf("m")){ n++; CHECK(s->e.p.count("harmonic") && !s->e.p.count("heightFrom")); hs.insert(s->e.height); }
    CHECK(n >= 4 && hs.size() >= 4);   // each harmonic keeps its own planned height
    // the scene's pitch space reaches the planner
    Engine F; CHECK(F.loadJson(smallScene("", {{"pitchSpace", {{"lowSemi", 0}, {"highSemi", 36}}}})));
    F.update(0);
    auto a = E.eventsOf("m"), b = F.eventsOf("m");
    CHECK(!a.empty() && a.size() == b.size() && std::fabs(a[0]->e.height - b[0]->e.height) > 1e-3);
    double expect = PitchSpace{0, 36}.heightOf(F.harmony().at(0).scale->semitonesOf(F.harmony().at(0).root));
    bool found = false; for(auto s : b) if(std::fabs(s->e.height - expect) < 0.05) found = true;
    CHECK(found);
}

// performance APIs (modOf / visibleInto / planHorizonBeats); values are covered by test_perf_equiv
static std::string readScene(const char* p){ std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void testPerfApis(){
    std::string txt = readScene("tests/v2/scenes/examples/04_starfield_depth.json");
    if(txt.empty()) txt = kScene;
    Engine A, B; CHECK(A.loadText(txt)); CHECK(B.loadText(txt));
    B.options.planHorizonBeats = 4; B.options.farPhrasesPerUpdate = 2;
    A.update(0); B.update(0);
    size_t na = 0, nb = 0; for(int l = 0; l < 4; l++){ na += A.events(l).size(); nb += B.events(l).size(); }
    CHECK(nb <= na);                   // spread: the far part comes later
    for(int f = 1; f <= 400; f++){ double b = f * 0.01; A.update(b); B.update(b); }
    bool same = true;                  // then the same events
    for(int l = 0; l < 4; l++){
        auto& x = A.events(l); auto& y = B.events(l);
        if(x.size() != y.size()){ same = false; break; }
        for(size_t i = 0; i < x.size(); i++) if(x[i].e.id != y[i].e.id || x[i].e.p != y[i].e.p) same = false;
    }
    CHECK(same);
    // visibleInto == visible; modOf shared per event and dropped after a re-plan
    LayerView v = A.viewFor(0, 4);
    std::vector<VisibleEvent> buf; A.visibleInto(0, v, buf);
    auto vis = A.visible(0, v);
    CHECK(buf.size() == vis.size());
    for(size_t i = 0; i < buf.size() && i < vis.size(); i++) CHECK(buf[i].ev == vis[i].ev && buf[i].x0 == vis[i].x0);
    if(!vis.empty()){
        auto m1 = A.modOf(*vis[0].ev), m2 = A.modOf(*vis[0].ev);
        CHECK(m1 == m2 && m1->ev.id == vis[0].ev->e.id);
        CHECK(m1->drawn("pitchFine", 0.3, 0.5) == A.drawnFn(*vis[0].ev)("pitchFine", 0.3, 0.5));
        StoredEvent keep = *vis[0].ev;   // the store changes on reroll
        A.reroll(A.scene().instances[0].id);
        CHECK(A.modOf(keep) != m1);
    }
}

int main(){
    testPerfApis();
    testEngine();
    testRetune();
    testRetuneKeyShift();
    testMassHeights();
    testAssistant();
    std::printf("test_engine: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
