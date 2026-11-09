// Headless tests for glide on change (ROADMAP 5.16): v2Sequencer.h (planner), v2Engine.h
// (transposition), v2Draw.h CPU renderer (partials bend with the fundamental).
#include "v2Engine.h"
#include "v2Draw.h"
#include <cstdio>

using namespace forms2;

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do{ if(c) g_pass++; else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

static Instance inst(const std::string& id, json seq, Follow f = Follow::Chord){
    Instance in; in.id = id; in.type = "melody"; in.seq = std::move(seq); in.follow = f; return in;
}
static Phrase phr(int k, Beat start, Beat len, const Instance& in){
    Phrase p; p.index = k; p.start = start; p.length = len; p.seed = basePhraseSeed(1, in.id, k); return p;
}
static json withGlide(json seq, json extra = json::object()){
    seq["glideOn"] = true; seq["glideTime"] = 1; seq["glideCurve"] = "linear"; seq["glideNote"] = false;
    for(auto it = extra.begin(); it != extra.end(); ++it) seq[it.key()] = it.value();
    return seq;
}
static bool bent(const Event& e){ return e.p.count("glideTo") > 0; }
static double P(const Event& e, const char* k){ return e.param(k, -999); }
static std::vector<Event> notesFrom(const std::vector<Event>& v, Beat a, Beat b){
    std::vector<Event> o; for(auto& e : v) if(e.start >= a - 1e-9 && e.start < b - 1e-9) o.push_back(e); return o;
}
static std::string sig(const std::vector<Event>& v){
    std::string s;
    for(auto& e : v){ s += std::to_string(e.id) + ":" + std::to_string(e.start) + ":" + std::to_string(e.length) + ":" + std::to_string(e.degree);
        for(auto& kv : e.p) s += "," + kv.first + "=" + std::to_string(kv.second); s += ";"; }
    return s;
}

// a held bass: one hit per bar (4 steps per bar), long notes (8 steps = 8 beats) crossing the bar line
static json heldBass(){ return {{"mode", "pattern"}, {"pattern", "x..."}, {"steps", 4}, {"pitchSource", "root"}, {"noteLength", 4}}; }

static void testRootTie(){
    Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    // tie: the bass at 0 continues as one line bending 0 -> 3 at beat 4 (its next onset merges into it)
    auto in = inst("bass", withGlide(heldBass()));
    auto ev = planPhrase(in, phr(0, 0, 8, in), ctx);
    CHECK(ev.size() == 1);
    if(ev.size() == 1){
        const Event& e = ev[0];
        CHECK(bent(e) && P(e, "glideFrom") == 0 && P(e, "glideTo") == 3 && P(e, "glideStart") == 4);
        CHECK(P(e, "glideBeats") == 1 && P(e, "glideCurve") == 0 && P(e, "glideTrigger") == GlideRoot);
        CHECK(e.start == 0 && e.length >= 8 - 1e-9 && e.degree == 0 && !e.p.count("tied"));
    }
    // trigger off: no bend anywhere
    auto off = inst("bass", withGlide(heldBass(), {{"glideRoot", false}, {"glideChord", false}}));
    auto eo = planPhrase(off, phr(0, 0, 8, off), ctx);
    CHECK(eo.size() == 2); for(auto& e : eo) CHECK(!bent(e));
    // glide off entirely: as before
    auto plain = inst("bass", heldBass());
    auto ep = planPhrase(plain, phr(0, 0, 8, plain), ctx);
    CHECK(ep.size() == 2); for(auto& e : ep) CHECK(!bent(e) && !e.p.count("gDegOff"));
    // re-attack: the note at 4 starts from the old pitch (0) and glides to 3
    auto ra = inst("bass", withGlide(heldBass(), {{"glideMode", "reattack"}}));
    auto er = planPhrase(ra, phr(0, 0, 8, ra), ctx);
    CHECK(er.size() == 2);
    if(er.size() == 2){
        CHECK(!bent(er[0]) && er[0].degree == 0);
        CHECK(er[1].start == 4 && bent(er[1]) && P(er[1], "glideFrom") == 0 && P(er[1], "glideTo") == 3 && P(er[1], "glideStart") == 4);
        CHECK(!er[1].p.count("tied") && P(er[1], "glideTrigger") == GlideRoot);
    }
    // a single long note across the change (no onset at 4): tie bends it, re-attack splits it
    json one = heldBass(); one["pattern"] = "x.......";   // 8 steps / 2 bars
    one["steps"] = 4; one["patternLength"] = 8; one["noteLength"] = 8;
    auto t1 = inst("bass", withGlide(one));
    auto e1 = planPhrase(t1, phr(0, 0, 8, t1), ctx);
    CHECK(e1.size() == 1 && bent(e1[0]) && P(e1[0], "glideStart") == 4 && P(e1[0], "glideTo") == 3);
    auto r1 = inst("bass", withGlide(one, {{"glideMode", "reattack"}}));
    auto f1 = planPhrase(r1, phr(0, 0, 8, r1), ctx);
    CHECK(f1.size() == 2);
    if(f1.size() == 2){
        CHECK(std::fabs(f1[0].length - 4) < 1e-9 && !bent(f1[0]));
        CHECK(f1[1].start == 4 && f1[1].degree == 0 && P(f1[1], "glideTo") == 3 && f1[1].id != f1[0].id);
        CHECK(std::fabs(f1[1].length - 4) < 1e-9);
    }
}

static void testChordChange(){
    // block chords held to the change (hold): tie merges each voice into one line to the nearest tone
    Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json chord = {{"mode", "pattern"}, {"pattern", "x..."}, {"steps", 4}, {"pitchSource", "chord"}, {"voices", 3}, {"hold", true}};
    auto plain = inst("ch", chord);
    auto ep = planPhrase(plain, phr(0, 0, 8, plain), ctx);
    CHECK(ep.size() == 6);
    auto tie = inst("ch", withGlide(chord));
    auto et = planPhrase(tie, phr(0, 0, 8, tie), ctx);
    CHECK(et.size() == 3);
    for(auto& e : et){
        CHECK(bent(e) && P(e, "glideStart") == 4 && P(e, "glideTrigger") == GlideChord && e.length >= 8 - 1e-9);
        bool target = false; for(auto& q : ep) if(q.start == 4 && q.degree == (int)P(e, "glideTo")) target = true;
        CHECK(target);   // lands on the planned voice of the next chord
    }
    auto ra = inst("ch", withGlide(chord, {{"glideMode", "reattack"}}));
    auto er = planPhrase(ra, phr(0, 0, 8, ra), ctx);
    CHECK(er.size() == 6);
    int bends = 0; for(auto& e : er) if(e.start == 4 && bent(e) && P(e, "glideStart") == 4) bends++;
    CHECK(bends >= 2);   // voices that move start from the old pitch (a common tone needs no bend)
    // off
    auto off = inst("ch", withGlide(chord, {{"glideChord", false}, {"glideRoot", false}}));
    for(auto& e : planPhrase(off, phr(0, 0, 8, off), ctx)) CHECK(!bent(e));
    // any held note, not only chord voices: a third held through a colour change (root stays)
    Harmony hc; hc.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{0, 4, -1, ChordColour::Sus4}};
    HarmonyTimeline HC(hc, 1); PlanContext c2; c2.harmony = &HC;
    json third = heldBass(); third["degreeOffset"] = 0; third["pitchSource"] = "scale"; third["scaleMode"] = "riff"; third["range"] = 1;
    third["octave"] = 0;
    auto t3 = inst("t", withGlide({{"mode", "pattern"}, {"pattern", "x......."}, {"patternLength", 8}, {"steps", 4}, {"pitchSource", "root"}, {"noteLength", 8}}));
    // the root note (degree 0) is a chord tone of the sus4 too: nothing to do
    auto e3 = planPhrase(t3, phr(0, 0, 8, t3), c2);
    CHECK(e3.size() == 1 && !bent(e3[0]));
    // a third (2) is not in sus4 (0 3 4): it moves to the nearest tone
    Instance t4 = t3; t4.follow = Follow::Pedal; t4.pedalDegree = 2;   // pedal: never moves with the root
    auto e4 = planPhrase(t4, phr(0, 0, 8, t4), c2);
    CHECK(e4.size() == 1 && !bent(e4[0]));   // pedal does not follow chords
    Instance t5 = t3; t5.seq["degreeOffset"] = 2;
    auto e5 = planPhrase(t5, phr(0, 0, 8, t5), c2);
    CHECK(e5.size() == 1 && bent(e5[0]) && P(e5[0], "glideFrom") == 2 && P(e5[0], "glideTrigger") == GlideChord);
    if(!e5.empty()){ int to = (int)P(e5[0], "glideTo"); CHECK(to == 3 || to == 1); CHECK(P(e5[0], "glideStart") == 4); }
}

static void testKeyChange(){
    // same root, a new scale at the section boundary: the third bends major -> minor (4 -> 3 semitones)
    Harmony h;
    HarmonySection a; a.lengthBeats = 4; a.steps = {ProgressionStep{0, 4}};
    HarmonySection b = a; b.scale.semis = {0, 2, 3, 5, 7, 8, 10}; b.lengthBeats = 4;
    h.sections = {a, b};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json s = {{"mode", "pattern"}, {"pattern", "x......."}, {"patternLength", 8}, {"steps", 4}, {"pitchSource", "root"}, {"noteLength", 8}, {"degreeOffset", 2}};
    auto in = inst("k", withGlide(s));
    auto ev = planPhrase(in, phr(0, 0, 8, in), ctx);
    CHECK(ev.size() == 1);
    if(!ev.empty()){
        const Event& e = ev[0];
        CHECK(bent(e) && P(e, "glideTrigger") == GlideKey && P(e, "glideStart") == 4 && P(e, "glideFrom") == 2 && P(e, "glideTo") == 2);
        CHECK(P(e, "glideFromSemis") == 4 && P(e, "glideToSemis") == 3);
    }
    auto off = inst("k", withGlide(s, {{"glideKey", false}}));
    auto eo = planPhrase(off, phr(0, 0, 8, off), ctx);
    CHECK(eo.size() == 1 && !bent(eo[0]));
}

static void testKeyShiftStep(){
    // same root, step 2 shifted +1 semitone: a held third bends 4 -> 5 semitones (GlideKey)
    Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{0, 4, -1, ChordColour::Triad, 1}};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json s = {{"mode", "pattern"}, {"pattern", "x......."}, {"patternLength", 8}, {"steps", 4}, {"pitchSource", "root"}, {"noteLength", 8}, {"degreeOffset", 2}};
    auto in = inst("k", withGlide(s));
    auto ev = planPhrase(in, phr(0, 0, 8, in), ctx);
    CHECK(ev.size() == 1);
    if(!ev.empty()){
        const Event& e = ev[0];
        CHECK(bent(e) && P(e, "glideTrigger") == GlideKey && P(e, "glideStart") == 4);
        CHECK(P(e, "glideFromSemis") == 4 && P(e, "glideToSemis") == 5);
    }
    // glide off: two notes, the second carries its shifted pitch flat (5 semitones)
    json s2 = s; s2["noteLength"] = 4; s2["pattern"] = "x...x...";
    auto ev2 = planPhrase(inst("k2", s2), phr(0, 0, 8, inst("k2", s2)), ctx);
    CHECK(ev2.size() == 2 && !ev2[0].p.count("glideToSemis") && P(ev2[1], "glideFromSemis") == 5 && P(ev2[1], "glideToSemis") == 5);
}

static void testMeterChange(){
    // 4/4 (root 0, one bar) -> 7/8 (root 4): a bass held across the section change bends at beat 4
    Harmony h;
    HarmonySection a; a.lengthBeats = 4; a.steps = {ProgressionStep{0, 4}};
    HarmonySection b; b.meter.num = 7; b.meter.den = 8; b.meter.grouping = {2, 2, 3}; b.lengthBeats = 7; b.steps = {ProgressionStep{4, 7}};
    h.sections = {a, b};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json s = {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 1}, {"pitchSource", "root"}, {"noteLength", 1.75}};
    auto in = inst("b", withGlide(s));
    auto ev = planPhrase(in, phr(0, 0, 11, in), ctx);
    // hits on each bar start: 0 (4/4) and 4 (7/8); the first rings 7 beats over the change
    CHECK(!ev.empty());
    if(!ev.empty()){
        const Event& e = ev[0];
        CHECK(e.start == 0 && bent(e) && P(e, "glideStart") == 4 && P(e, "glideFrom") == 0 && P(e, "glideTo") == 4 && P(e, "glideTrigger") == GlideRoot);
        CHECK(e.length >= 11 - 1e-6);   // merged with the 7/8 bar's note
    }
    CHECK(ev.size() == 1);
    CHECK(H.barStart(4.5) == 4 && H.barBeatsAt(5) == 7);
    auto ra = inst("b", withGlide(s, {{"glideMode", "reattack"}}));
    auto er = planPhrase(ra, phr(0, 0, 11, ra), ctx);
    CHECK(er.size() == 2 && er.size() == 2 && er[1].start == 4 && P(er[1], "glideFrom") == 0 && P(er[1], "glideTo") == 4);
}

static void testDegreeShift(){
    HarmonyTimeline H; PlanContext ctx; ctx.harmony = &H;
    // mid-phrase: an event-level modulation lifts the degree offset by 2 from beat 2
    ctx.modAt = [](const std::string& k, double base, Level lv, uint64_t, Beat at){
        if(k == "seq.degreeOffset" && lv == LevelEvent && at >= 2 - 1e-9) return base + 2;
        return base;
    };
    json s = {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 4}, {"pitchSource", "root"}, {"legato", true}};
    auto in = inst("d", withGlide(s));
    auto ev = planPhrase(in, phr(0, 0, 4, in), ctx);
    // 0, 1 at degree 0 (same pitch: no trigger); the note at 1 merges the one at 2, bending 0 -> 2 at
    // beat 2; the note at 3 (same pitch, no change) is a new note
    CHECK(ev.size() == 3);
    if(ev.size() == 3){
        CHECK(!bent(ev[0]) && ev[1].start == 1 && bent(ev[1]) && P(ev[1], "glideStart") == 2 && P(ev[1], "glideTo") == 2);
        CHECK(P(ev[1], "glideTrigger") == GlideDegree && std::fabs(ev[1].start + ev[1].length - 3) < 1e-9);
        CHECK(ev[2].start == 3 && ev[2].degree == 2 && !bent(ev[2]));
    }
    auto off = inst("d", withGlide(s, {{"glideDegree", false}}));
    auto eo = planPhrase(off, phr(0, 0, 4, off), ctx);
    CHECK(eo.size() == 4); for(auto& e : eo) CHECK(!bent(e));
    CHECK(eo.size() == 4 && eo[2].degree == 2 && eo[1].degree == 0);
    // between phrases (modulation per phrase): the previous phrase's last note leads into the new one
    PlanContext c0; c0.harmony = &H;
    auto p0 = inst("d", withGlide(s));
    auto e0 = planPhrase(p0, phr(0, 0, 4, p0), c0);
    Instance p1 = p0; p1.seq["degreeOffset"] = 3;
    c0.prevPhrase = &e0;
    auto e1 = planPhrase(p1, phr(1, 4, 4, p1), c0);
    CHECK(!e1.empty() && e1[0].start == 4 && bent(e1[0]) && P(e1[0], "glideFrom") == 0 && P(e1[0], "glideTo") == 3);
    CHECK(!e1.empty() && P(e1[0], "glideTrigger") == GlideDegree && P(e1[0], "tied") == 1);   // tie across phrases: no new attack
    Instance p2 = p1; p2.seq["glideMode"] = "reattack";
    auto e2 = planPhrase(p2, phr(1, 4, 4, p2), c0);
    CHECK(!e2.empty() && bent(e2[0]) && !e2[0].p.count("tied"));
}

static void testNextNote(){
    // next note = the existing portamento: same bend keys as the legacy "glide" param
    HarmonyTimeline H; PlanContext ctx; ctx.harmony = &H;
    json s = {{"mode", "probability"}, {"density", 1}, {"steps", 8}, {"pitchSource", "scale"}, {"glide", 0.25}};
    auto legacy = inst("m", s);
    json g = s; g["glideOn"] = true; g["glideMode"] = "reattack";
    auto uni = inst("m", g);
    auto a = planPhrase(legacy, phr(0, 0, 4, legacy), ctx), b = planPhrase(uni, phr(0, 0, 4, uni), ctx);
    CHECK(a.size() == b.size() && a.size() > 2);
    int nb = 0;
    for(size_t i = 0; i < std::min(a.size(), b.size()); i++){
        CHECK(bent(a[i]) == bent(b[i]));
        if(bent(a[i]) && bent(b[i])){
            nb++;
            CHECK(P(a[i], "glideFrom") == P(b[i], "glideFrom") && P(a[i], "glideTo") == P(b[i], "glideTo"));
            CHECK(P(a[i], "glideStart") == P(b[i], "glideStart") && P(a[i], "glideBeats") == P(b[i], "glideBeats"));
            CHECK(P(b[i], "glideTrigger") == GlideNote);
        }
    }
    CHECK(nb > 0);
    // note trigger off: nothing
    json o = g; o["glideNote"] = false; o["glide"] = 0;
    auto off = inst("m", o);
    for(auto& e : planPhrase(off, phr(0, 0, 4, off), ctx)) CHECK(!bent(e));
    // tie + legato: the melody is one line
    json t = s; t["glideOn"] = true; t["legato"] = true; t["glide"] = 0; t["glideTime"] = 0.25;
    auto tl = inst("m", t);
    auto et = planPhrase(tl, phr(0, 0, 4, tl), ctx);
    CHECK(et.size() < a.size());
    for(size_t i = 1; i < et.size(); i++) CHECK(et[i].p.count("tied") || !bent(et[i]) || P(et[i], "glideStart") >= et[i].start);
}

static void testDeterminism(){
    Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}, ProgressionStep{4, 4}, ProgressionStep{5, 4}};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json s = withGlide({{"mode", "probability"}, {"density", 0.6}, {"steps", 8}, {"pitchSource", "chord"}, {"voices", 3}, {"hold", true}, {"glideNote", true}});
    auto in = inst("x", s);
    for(int k = 0; k < 4; k++){
        auto a = planPhrase(in, phr(k, k * 16.0, 16, in), ctx), b = planPhrase(in, phr(k, k * 16.0, 16, in), ctx);
        CHECK(!a.empty() && sig(a) == sig(b));
        std::set<uint64_t> ids; for(auto& e : a) ids.insert(e.id);
        CHECK(ids.size() == a.size());   // unique ids (re-attack / tied events included)
    }
}

// ---------------------------------------------------------------- engine: transposition
static json bassScene(const std::string& retune, const json& glide){
    json j = json::parse(R"({"format":"espectres.scene","formatVersion":1,"name":"g","version":1,"bpm":120,"seed":5,
      "harmony":{"sections":[{"name":"A","meter":{"num":4,"den":4},"scale":{"name":"major","semis":[0,2,4,5,7,9,11],"tonic":0},
        "steps":[{"degree":0,"length":64}]}]},
      "layers":[{"zoomBeats":16,"blend":"lighten","gain":1}],
      "instances":[{"id":"bass","type":"bass","layer":0,"follow":"chord",
        "seq":{"mode":"pattern","pattern":"x...","steps":4,"pitchSource":"root","noteLength":4},"phrase":{"length":8}}],
      "routings":[],"macros":{}})");
    if(!retune.empty()){ j["retune"] = retune; j["retuneBeats"] = 2; j["retuneCurve"] = "linear"; }
    for(auto it = glide.begin(); it != glide.end(); ++it) j["instances"][0]["seq"][it.key()] = it.value();
    return j;
}
static std::vector<Event> bassEvents(Engine& E){ std::vector<Event> v; for(auto s : E.eventsOf("bass")) v.push_back(s->e); return v; }
static void testTransposition(){
    json gl = {{"glideOn", true}, {"glideTime", 1}, {"glideCurve", "linear"}};
    for(std::string mode : {"ahead", "all", "new"}){
        Engine E; CHECK(E.loadJson(bassScene(mode, gl)));
        for(double t = 0; t <= 2; t += 0.5) E.update(t);
        HarmonyOverride o; o.jumpDegree = 3;
        E.setHarmonyOverride(o, 2);
        auto ev = bassEvents(E);
        const Event* sounding = nullptr; int ahead = 0, aheadMorph = 0;
        for(auto& e : ev){ if(e.start <= 2 && e.start + e.length > 2) sounding = &e; if(e.start > 2){ ahead++; aheadMorph += e.p.count("retuneStart") > 0; } }
        CHECK(sounding != nullptr);
        if(sounding){
            // tie: the note sounding at the jump bends 0 -> 3 from the playhead, one event
            CHECK(bent(*sounding) && P(*sounding, "glideFrom") == 0 && P(*sounding, "glideTo") == 3);
            CHECK(P(*sounding, "glideStart") == 2 && P(*sounding, "glideBeats") == 1 && P(*sounding, "glideTrigger") == GlideTranspose);
            CHECK(!sounding->p.count("retuneStart") && sounding->degree == 0);
        }
        if(mode != "new") CHECK(ahead > 0 && aheadMorph == ahead);   // planned notes ahead: the retune morph, as before
        // clearing jumps back with a second bend (split into a tied continuation: one bend per event)
        E.update(3);
        E.clearHarmonyOverride();
        auto ev2 = bassEvents(E);
        int tied = 0; for(auto& e : ev2) if(e.start == 3 && P(e, "tied") == 1 && P(e, "glideFrom") == 3 && P(e, "glideTo") == 0) tied++;
        CHECK(tied == 1);
    }
    // re-attack: a new event from the playhead, old pitch -> new
    Engine R; CHECK(R.loadJson(bassScene("ahead", {{"glideOn", true}, {"glideMode", "reattack"}, {"glideTime", 1}})));
    for(double t = 0; t <= 2; t += 0.5) R.update(t);
    size_t n0 = bassEvents(R).size();
    HarmonyOverride o; o.jumpDegree = 3; R.setHarmonyOverride(o, 2);
    auto er = bassEvents(R);
    CHECK(er.size() == n0 + 1);
    int split = 0; for(auto& e : er){ if(e.start == 0 && std::fabs(e.length - 2) < 1e-9 && !bent(e)) split++; if(e.start == 2 && P(e, "glideFrom") == 0 && P(e, "glideTo") == 3 && !e.p.count("tied")) split++; }
    CHECK(split == 2);
    // glide -1 time: the scene's retuneBeats (2)
    Engine L; CHECK(L.loadJson(bassScene("all", {{"glideOn", true}})));
    for(double t = 0; t <= 2; t += 0.5) L.update(t);
    L.setHarmonyOverride(o, 2);
    for(auto& e : bassEvents(L)) if(e.start <= 2 && e.start + e.length > 2) CHECK(P(e, "glideBeats") == 2 && P(e, "glideCurve") == 0);
    // transposition trigger off: the old retune morph for everything
    Engine F; CHECK(F.loadJson(bassScene("all", {{"glideOn", true}, {"glideTranspose", false}})));
    for(double t = 0; t <= 2; t += 0.5) F.update(t);
    F.setHarmonyOverride(o, 2);
    for(auto& e : bassEvents(F)) CHECK(!bent(e) && e.p.count("retuneStart"));
    // deterministic
    Engine A, B; A.loadJson(bassScene("ahead", gl)); B.loadJson(bassScene("ahead", gl));
    for(double t = 0; t <= 2; t += 0.5){ A.update(t); B.update(t); }
    A.setHarmonyOverride(o, 2); B.setHarmonyOverride(o, 2);
    CHECK(sig(bassEvents(A)) == sig(bassEvents(B)));
    // engine: across phrases (previous phrase passed to the planner) with a held, legato bass
    Engine X; json xs = bassScene("", {{"glideOn", true}, {"legato", true}, {"pattern", "x"}, {"steps", 1}});
    xs["harmony"]["sections"][0]["steps"] = json::parse(R"([{"degree":0,"length":8},{"degree":4,"length":8}])");
    CHECK(X.loadJson(xs));
    for(double t = 0; t <= 10; t += 0.5) X.update(t);
    int across = 0; for(auto& e : bassEvents(X)) if(e.start == 8 && P(e, "tied") == 1 && P(e, "glideFrom") == 0 && P(e, "glideTo") == 4) across++;
    CHECK(across == 1);
}

// ---------------------------------------------------------------- rendering: partials bend together
static std::vector<int> peaks(const std::vector<float>& c, float thr){
    std::vector<int> o; for(size_t i = 1; i + 1 < c.size(); i++) if(c[i] > thr && c[i] >= c[i - 1] && c[i] > c[i + 1]) o.push_back((int)i); return o;
}
static void testRender(){
    Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
    HarmonyTimeline H(h, 1); PlanContext ctx; ctx.harmony = &H;
    json one = {{"mode", "pattern"}, {"pattern", "x......."}, {"patternLength", 8}, {"steps", 4}, {"pitchSource", "root"}, {"noteLength", 8}};
    auto in = inst("bass", withGlide(one));
    auto ev = planPhrase(in, phr(0, 0, 8, in), ctx);
    CHECK(ev.size() == 1 && bent(ev[0]));
    if(ev.empty()) return;
    Event e = ev[0];
    e.look = "note"; e.p["partials"] = 3;   // v1 hicut: the top partial fades to 0, two remain e.p["partialSpread"] = 1; e.p["thickness"] = 2; e.p["softness"] = 0;
    e.p["attack"] = 0; e.p["release"] = 0; e.p["partialDecay"] = 0; e.p["partialThin"] = 0;
    draw::Renderer r; r.frame.width = 400; r.frame.height = 600; r.frame.headerX = 0; r.frame.nowBeat = 0;
    r.frame.space = PitchSpace{-12, 36};
    r.layers = {LayerDef{8, "lighten", 1}};   // 8 beats on screen: 50 px per beat
    r.render({e});
    auto before = peaks(r.column(100), 0.01f), after = peaks(r.column(300), 0.01f);   // beats 2 and 6
    CHECK(before.size() >= 3 && before.size() == after.size());   // fundamental + partials
    if(before.size() >= 3 && before.size() == after.size()){
        // degree 0 -> 3 (C -> F, 5 semitones): every line moves up by the same amount
        int d0 = before.back() - after.back();
        CHECK(d0 > 10);
        for(size_t i = 0; i < before.size(); i++) CHECK(std::abs((before[i] - after[i]) - d0) <= 2);
        std::printf("  rows before");
        for(int y : before) std::printf(" %d", y);
        std::printf(", after"); for(int y : after) std::printf(" %d", y); std::printf("\n");
    }
    // mid-glide (beat 4.5, linear over 1 beat): every line between
    auto mid = peaks(r.column(225), 0.01f);
    CHECK(mid.size() == before.size());
    if(mid.size() == before.size() && before.size() == after.size())
        for(size_t i = 0; i < mid.size(); i++) CHECK(mid[i] < before[i] && mid[i] > after[i]);
}

int main(){
    testRootTie();
    testChordChange();
    testKeyChange();
    testKeyShiftStep();
    testMeterChange();
    testDegreeShift();
    testNextNote();
    testDeterminism();
    testTransposition();
    testRender();
    std::printf("test_glide: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
