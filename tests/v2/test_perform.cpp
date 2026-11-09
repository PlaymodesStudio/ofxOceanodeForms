// test_perform.cpp — headless tests for v2Perform.h (snapshots, queued scenes, setlist, templates, timeline)
#include "v2Perform.h"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace forms2;

static int passed = 0, failed = 0;
#define CHECK(c) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

static json readJson(const std::string& p){
    std::ifstream f(p); if(!f){ std::printf("  missing %s (run from the addon folder)\n", p.c_str()); return json(); }
    std::stringstream ss; ss << f.rdbuf(); return json::parse(ss.str());
}
static std::string signature(const Engine& e, Beat from = -1e18){
    std::ostringstream o; o.precision(17);
    for(int l = 0; l < 4; l++) for(auto& s : e.events(l)){
        if(s.e.start < from) continue;
        o << l << ' ' << s.e.instance << ' ' << s.e.id << ' ' << s.e.start << ' ' << s.e.length << ' ' << s.e.degree << ' '
          << s.e.depth << ' ' << s.e.level << ' ' << s.e.seed;
        for(auto& kv : s.e.p) o << ' ' << kv.first << '=' << kv.second;
        o << ';';
    }
    return o.str();
}
static std::string signature(Performer& p){
    std::ostringstream o;
    for(int i = 0; i < p.slotCount(); i++) o << "[" << p.slot(i).origin << "]" << signature(p.engine(i));
    return o.str();
}
static size_t eventCount(const Engine& e){ size_t n = 0; for(int l = 0; l < 4; l++) n += e.events(l).size(); return n; }

static const std::string kTpl = "tests/v2/templates/";
static json example(){ return readJson("tests/v2/scenes/example_v2.json"); }

static void testSnapshot(){
    Engine a; CHECK(a.loadJson(example()));
    for(double t = 0; t <= 20; t += 0.25){
        a.update(t);
        if(t == 3) a.setLocked("bass", true);
        if(t == 6) a.reroll("bass");
        if(t == 7) a.setMacro("macro1", 0.9);
        if(t == 9) a.setMuted("hats", true);
        if(t == 10) a.setHarmonyOverride({true, -1000}, 10);
        if(t == 13){ a.setHarmonyOverride({true, 2}, 13); a.reroll("kick"); }
        if(t == 15){ Instance k = *a.instance("kick"); k.phrase = {{"length", 6}}; CHECK(a.setInstance(k)); }
        if(t == 17) a.setMuted("hats", false);
    }
    json snap = snapshot(a);
    std::string text = snap.dump();
    Engine b; std::string err;
    CHECK(restoreSnapshot(b, json::parse(text), &err));
    if(!err.empty()) std::printf("  restore: %s\n", err.c_str());
    CHECK(signature(a) == signature(b));
    CHECK(b.locked("bass") && !b.muted("hats"));
    CHECK(std::fabs(b.macro("macro1") - 0.9) < 1e-12);
    CHECK(b.harmonyOverride().hold && b.harmonyOverride().jumpDegree == 2);
    CHECK(a.phrase("kick", 6).length == b.phrase("kick", 6).length);
    bool same = true;
    for(double t = 20.25; t <= 90; t += 0.25){
        a.update(t); b.update(t);
        if(t == 40){ a.reroll("bass"); b.reroll("bass"); }
        if(signature(a) != signature(b)) same = false;
    }
    CHECK(same);
    CHECK(snapshot(a) == snapshot(b));   // state equal after running too
    // a bad snapshot fails cleanly
    Engine c; CHECK(!restoreSnapshot(c, json::object(), &err));
}

static void testQueued(){
    json other = readJson(kTpl + "techno_4-4.json");
    struct Case { Boundary b; double expect; };
    // example scene at 5.3: next bar 8, phrase (pad 16, bass 8, kick 4, hats 8) 16, 4 bars 16, now 5.3
    Case cs[] = {{Boundary::Now, 5.3}, {Boundary::Bar, 8}, {Boundary::Phrase, 16}, {Boundary::FourBars, 16}, {Boundary::At, 11}};
    for(auto& c : cs){
        Performer p; CHECK(p.loadScene(example()));
        for(double t = 0; t <= 5.3 + 1e-9; t += 0.1) p.update(t);
        p.update(5.3);
        Beat at = p.queueScene(other, c.b, 0, 11);
        CHECK(std::fabs(at - c.expect) < 1e-6);
        if(c.b != Boundary::Now){
            CHECK(p.engine().scene().name == "example v2");
            p.update(c.expect - 0.05); CHECK(p.engine().scene().name == "example v2");
        }
        p.update(c.expect + 0.05);
        CHECK(p.slotCount() == 1 && p.engine().scene().name == "techno 4-4");
        CHECK(std::fabs(p.slot(0).origin - c.expect) < 1e-6);
        CHECK(p.engine().now() < 0.1 + 1e-9);   // the new scene starts at its beat 0
    }
    // 4 bars from bar 5 (beat 20): next multiple of 4 bars is bar 8 = beat 32 (A is 16 beats, B 7/8 bars of 7)
    Performer p; p.loadScene(readJson(kTpl + "techno_4-4.json"));
    p.update(20.5);
    CHECK(std::fabs(p.nextBoundary(Boundary::FourBars) - 32) < 1e-6);
    CHECK(std::fabs(p.nextBoundary(Boundary::Bar) - 24) < 1e-6);
}

static void testCrossfade(){
    Performer p; p.loadScene(example());
    p.update(5);
    p.queueScene(readJson(kTpl + "ambient_drift.json"), Boundary::Bar, 4);
    p.update(7.9); CHECK(p.slotCount() == 1);
    p.update(9);
    CHECK(p.slotCount() == 2);
    CHECK(std::fabs(p.slotGain(0) - 0.75) < 1e-9 && std::fabs(p.slotGain(1) - 0.25) < 1e-9);
    auto g = p.layerGains(1); CHECK(std::fabs(g[0] - 0.25) < 1e-9);
    CHECK(eventCount(p.engine(0)) > 0 && eventCount(p.engine(1)) > 0);   // both planned
    CHECK(p.engine(1).now() == 1 && p.engine(0).now() == 9);
    p.update(11); CHECK(std::fabs(p.slotGain(1) - 0.75) < 1e-9);
    p.update(12.1);
    CHECK(p.slotCount() == 1 && p.engine().scene().name == "ambient drift" && p.slotGain(0) == 1);
}

static void testSetlist(){
    std::vector<SetEntry> sl(3);
    sl[0].scene = example(); sl[0].bars = 4;                          // 16 beats (4/4)
    sl[1].scene = readJson(kTpl + "techno_4-4.json"); sl[1].minutes = 0.5; sl[1].crossfadeBeats = 8;   // 64 beats at 128
    sl[2].scene = readJson(kTpl + "spectral_drone.json");
    Performer p; CHECK(p.setSetlist(sl));
    CHECK(std::fabs(p.slot(0).endBeat - 16) < 1e-9);
    for(double t = 0; t <= 15.9; t += 0.5) p.update(t);
    CHECK(p.currentSetIndex() == 0);
    p.update(16.2); CHECK(p.currentSetIndex() == 1 && p.slotCount() == 1 && std::fabs(p.slot(0).origin - 16) < 1e-9);
    CHECK(std::fabs(p.slot(0).endBeat - 80) < 1e-9);
    p.update(79.9); CHECK(p.currentSetIndex() == 1);
    p.update(84); CHECK(p.slotCount() == 2 && p.currentSetIndex() == 2 && std::fabs(p.slotGain(1) - 0.5) < 1e-9);
    p.update(88.5); CHECK(p.slotCount() == 1 && p.currentSetIndex() == 2);
    // 7/8 bars count too: breakbeat A = 4 bars of 4 (16), B = 2 bars of 7 -> 6 bars end at 30
    std::vector<SetEntry> s2(2);
    s2[0].scene = readJson(kTpl + "breakbeat.json"); s2[0].bars = 6;
    s2[1].scene = example();
    Performer q; q.setSetlist(s2);
    CHECK(std::fabs(q.slot(0).endBeat - 30) < 1e-9);
}

static void testTemplates(){
    const char* names[] = {"techno_4-4", "ambient_drift", "breakbeat", "spectral_drone"};
    for(auto n : names){
        json t = readJson(kTpl + n + ".json");
        CHECK(t.is_object());
        json s = sceneFromTemplate(t, std::string("my ") + n, 99);
        CHECK(s["name"] == std::string("my ") + n && s["seed"] == 99 && !s.contains("template"));
        Engine e;
        bool ok = e.loadJson(s);
        CHECK(ok);
        for(auto& pr : e.problems()) std::printf("  %s: %s\n", n, pr.c_str());
        if(!ok) continue;
        CHECK(validate(sceneDocFromJson(e.captureState().scene)).empty());
        CHECK(e.scene().instances.size() >= 4);
        std::set<std::string> types; std::set<double> lens;
        for(auto& in : e.scene().instances){ types.insert(in.type); }
        CHECK(types.size() >= 3);
        CHECK(!e.scene().routings.empty());
        for(double b = 0; b <= 64; b += 0.5) e.update(b);
        std::set<std::string> sounding;
        for(int l = 0; l < 4; l++) for(auto& ev : e.events(l)) sounding.insert(ev.e.instance);
        CHECK(sounding.size() >= 3);
        for(auto& in : e.scene().instances) lens.insert(e.phrase(in.id, 0).length);
        CHECK(lens.size() >= 2);
    }
    {   // the drone template reads a lane
        Engine e; e.loadJson(readJson(kTpl + "spectral_drone.json"));
        auto ln = e.laneNames(); CHECK(ln.size() == 1 && ln[0] == "swell");
    }
}

static std::vector<TimelineAction> tl(){
    auto mk = [](Beat b, const char* a, const char* t, double v = 0){ TimelineAction x; x.beat = b; x.action = a; x.target = t; x.value = v; return x; };
    std::vector<TimelineAction> v{mk(10, "mute", "hats"), mk(12, "macro", "macro1", 0.2), mk(14, "lock", "bass"),
                                  mk(22, "reroll", "bass"), mk(26, "unmute", "hats"), mk(30, "jump", "", 3)};
    TimelineAction sc = mk(36, "scene", "", -1); sc.data = {{"scene", readJson(kTpl + "breakbeat.json")}, {"boundary", "bar"}, {"crossfade", 4}};
    v.push_back(sc);
    return v;
}

static void testTimelineSeek(){
    Performer p; p.loadScene(example()); p.setTimeline(tl());
    p.play();
    for(int i = 0; i < 44; i++) p.tick(0.25);
    CHECK(std::fabs(p.position() - 11) < 1e-9);
    CHECK(p.engine().muted("hats") && !p.engine().locked("bass"));
    for(int i = 0; i < 16; i++) p.tick(0.25);   // 15
    CHECK(p.engine().locked("bass") && std::fabs(p.engine().macro("macro1") - 0.2) < 1e-12);
    p.stop(); p.tick(1); CHECK(std::fabs(p.position() - 15) < 1e-9);
    p.update(27); CHECK(!p.engine().muted("hats"));
    p.update(31); CHECK(p.engine().harmonyAt(31).root == 3);
    p.update(36.5); CHECK(std::fabs(p.pending().at - 37) < 1e-9 && p.pending().active);
    p.update(38); CHECK(p.slotCount() == 2 && p.engine().scene().name == "breakbeat");
    p.update(45); CHECK(p.slotCount() == 1);

    // seek determinism: same state from any prior position, and from a fresh performer
    p.seek(24); std::string a = signature(p);
    CHECK(p.engine().locked("bass") && p.engine().muted("hats"));
    for(double t = 24; t <= 60; t += 0.5) p.update(t);
    p.seek(24); std::string b = signature(p);
    CHECK(a == b && !a.empty());
    p.update(8); p.seek(24); CHECK(signature(p) == a);
    p.update(5); CHECK(!p.engine().muted("hats"));   // backwards = seek
    Performer f; f.loadScene(example()); f.setTimeline(tl()); f.seek(24);
    CHECK(signature(f) == a);
    p.seek(38); f.seek(38); CHECK(signature(p) == signature(f));
    p.seek(39); f.seek(39); CHECK(p.slotCount() == 2 && signature(p) == signature(f));
    // after a seek the run continues the same as a fresh seek
    for(double t = 39; t <= 70; t += 0.5){ p.update(t); f.update(t); }
    CHECK(signature(p) == signature(f));

    // lanes: registered by name, read in global beats
    Performer d; d.loadScene(readJson(kTpl + "spectral_drone.json"));
    CHECK(d.missingLanes() == std::vector<std::string>{"swell"});
    Beat seen = -1;
    d.registerLane("swell", [&](Beat b){ seen = b; return 1.0; });
    CHECK(d.missingLanes().empty() && d.laneNames().size() == 1);
    d.update(3); d.engine().layerZoom(0, 3);
    CHECK(std::fabs(seen - 3) < 1e-9);
    CHECK(d.engine().layerZoom(0, 3) < 64);
}

static void testRecord(){
    Performer p; p.loadScene(example());
    p.setRecording(true);
    for(double t = 0; t <= 40; t += 0.5){
        p.update(t);
        if(t == 6) p.mute("kick", true);
        if(t == 9) p.macro("macro1", 0.1);
        if(t == 13) p.lock("bass", true);
        if(t == 18) p.reroll("bass");
        if(t == 21) p.hold(true);
        if(t == 25) p.queueSetEntry(-1, Boundary::Bar);   // invalid: ignored
        if(t == 27) p.queueScene(readJson(kTpl + "ambient_drift.json"), Boundary::Phrase, 2);
    }
    auto rec = p.recordedTimeline();
    CHECK(rec.size() == 7);
    CHECK(rec[0].action == "mute" && rec[0].target == "kick" && rec[0].beat == 6);
    CHECK(rec[3].action == "reroll" && rec[3].beat == 18);
    CHECK(p.engine().scene().name == "ambient drift");
    json j = Performer::timelineToJson(rec);
    auto back = Performer::timelineFromJson(json::parse(j.dump()));
    CHECK(back.size() == rec.size() && back[6].data["boundary"] == "phrase");
    // replaying the log as a timeline lands in the same state
    Performer r; r.loadScene(example()); r.setTimeline(back);
    r.seek(30);
    p.seek(30);   // p has no timeline: plain scene
    CHECK(r.engine().scene().name == "example v2" && r.pending().active && std::fabs(r.pending().at - 32) < 1e-9);
    r.seek(26);
    CHECK(r.engine().muted("kick") && r.engine().locked("bass") && r.engine().harmonyOverride().hold);
    CHECK(std::fabs(r.engine().macro("macro1") - 0.1) < 1e-12);
    r.update(40); CHECK(r.engine().scene().name == "ambient drift");
    CHECK(r.recorded().empty());   // timeline actions are not recorded

    // performance snapshot: restore mid-crossfade and continue identically
    Performer s; s.loadScene(example()); s.setTimeline(back);
    for(double t = 0; t <= 33; t += 0.5) s.update(t);
    CHECK(s.slotCount() == 2);
    json snap = s.snapshot();
    Performer u; u.setTimeline(back); CHECK(u.restore(json::parse(snap.dump())));
    CHECK(signature(s) == signature(u));
    bool same = true;
    for(double t = 33.5; t <= 60; t += 0.5){ s.update(t); u.update(t); if(signature(s) != signature(u)) same = false; }
    CHECK(same);
}

int main(){
    setvbuf(stdout, nullptr, _IONBF, 0);
    testSnapshot();
    testQueued();
    testCrossfade();
    testSetlist();
    testTemplates();
    testTimelineSeek();
    testRecord();
    std::printf("test_perform: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
