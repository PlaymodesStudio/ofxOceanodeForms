// test_schema.cpp — headless tests for v2Schema.h and v2Format.h (ESPECTRES v2, milestone 1)
#include <cstdio>
#include <iostream>
#include "v2Format.h"

using namespace forms2;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, msg) do{ if(c) ++g_pass; else { ++g_fail; std::cout << "FAIL: " << msg << "  (" #c ")\n"; } }while(0)

static bool hasProblem(const std::vector<std::string>& v, const std::string& part){
    for(auto& s : v) if(s.find(part) != std::string::npos) return true;
    return false;
}

static bool fx0(const TypeDef* t){ return t->family == "effects"; }
static void testBlobSchema(){
    for(auto& t : instrumentTypes()){
        auto b = t.param("blob");
        if(!b) continue;
        CHECK(b->options.size() == 7 && b->options[0] == "off" && b->options[6] == "custom", t.name + ": blob presets");
        for(auto& k : {"blobAmount", "blobMode", "blobCount", "blobScale", "blobSizeBeats", "blobSizeHeight", "blobAspect", "blobRotation",
                       "blobIrregularity", "blobDetail", "blobOctaves", "blobTail", "blobSoftness", "blobZoneLow", "blobZoneHigh", "blobThreshold", "blobWobble", "blobWobbleRate"})
            CHECK(t.param(k) != nullptr, t.name + ": " + k);
        auto pl = t.param("blobPlacement"); CHECK(pl && (pl->levels & (LevelEvent | LevelLayer)) == 0, t.name + ": placement is structure");
    }
    CHECK(checkValue(*findType("timefx")->param("fxTarget"), json("mel")).empty() && !checkValue(*findType("timefx")->param("fxTarget"), json(3)).empty(), "fxTarget is text");
}
static void testTypes(){
    const char* names[] = {"bass", "melody", "chords", "arpeggio", "bells", "mass", "kick", "snare", "hats",
                           "texture", "sweep", "shapes", "timefx", "filterfx"};
    CHECK(instrumentTypes().size() == 14, "14 types");
    for(auto n : names){
        const TypeDef* t = findType(n);
        CHECK(t != nullptr, std::string("type present: ") + n);
        if(!t) continue;
        int mains = 0;
        std::set<std::string> keys;
        for(auto& p : t->params){
            if(p.main) ++mains;
            CHECK(keys.insert(p.key).second, t->name + ": unique key " + p.key);
            CHECK(!findIn(eventParams(), p.key), t->name + ": key does not shadow a shared key " + p.key);
            CHECK(checkValue(p, p.def).empty(), t->name + "." + p.key + " default valid");
            CHECK((p.levels & p.defaultLevel) != 0, t->name + "." + p.key + " levels include default");
            CHECK(!p.degrees || p.unit == "degrees", t->name + "." + p.key + " degrees unit");
        }
        // 8-12 of the instrument's own + up to 3 blob mains (5.12: blob, amount, placement)
        CHECK(mains >= 8 && mains <= 15, t->name + ": 8-15 main params (has " + std::to_string(mains) + ")");
        CHECK(t->name == "timefx" || (t->param("blob") && t->param("blobPlacement") && t->param("blobScope")), t->name + ": blob mask params");
        if(fx0(t)) CHECK(t->param("fxScope") && t->param("fxTarget") && t->param("fxTarget")->type == "string", t->name + ": effect scope");
        CHECK(t->param("level") != nullptr, t->name + ": level");
        bool fx = t->family == "effects";
        CHECK(fx || t->param("coverEnable"), t->name + ": cover below");
        if(t->family == "percussive"){
            CHECK(t->param("drumVoice") && t->param("scRelease") && t->param("drumPitchStart"), t->name + ": drum voice");
            CHECK(t->kind == EventKind::Hit, t->name + ": hit kind");
        }
        if(t->family == "pitched"){
            auto l = t->param("look");
            CHECK(l && l->options.size() == 3, t->name + ": look choice");
            CHECK(t->param("detune") && t->param("detuneVoices") && t->param("partialDetune") && t->param("partialCount") && t->param("partialBright"), t->name + ": detune");
        }
        auto lk = t->param("look");
        if(lk) CHECK((lk->levels & (LevelEvent | LevelLayer)) == 0, t->name + ": look is structure");
    }
    CHECK(findType("bells")->param("detune")->main && findType("chords")->param("detune")->main, "detune main on bells / chords");
    CHECK(findType("mass")->param("detune") != nullptr, "mass detune");
    CHECK(allowsAnimated("look.partialCount") && findType("mass")->param("partialBright"), "partial animations");
    for(auto* l : {&eventParams(), &layerParams(), &sceneParams()})
        for(auto& p : *l){ CHECK(checkValue(p, p.def).empty(), "shared default " + p.key); CHECK((p.levels & p.defaultLevel) != 0, "shared level " + p.key); }
    auto pf = findParam("melody", "pitchFine");
    CHECK(pf && !pf->degrees && pf->defaultLevel == LevelEvent && pf->unit == "cents", "pitchFine");
    auto dp = findParam("hats", "depth");
    CHECK(dp && dp->min == 0.05 && dp->def == 1 && dp->defaultLevel == LevelEvent && (dp->levels & LevelLayer), "depth");
    auto z = findParam("bass", "layer.zoom");
    CHECK(z && !(z->levels & LevelEvent), "layer zoom levels");
    CHECK(findParam("bass", "layer2.gain") != nullptr && findParam("bass", "layer7.gain") == nullptr, "layerN");
    CHECK(findParam("bass", "scene.macro3") != nullptr, "macro");
    CHECK(findParam("bass", "look.thickness") == findParam("bass", "thickness"), "bare key");
    CHECK(findParam("bass", "look.cell") == nullptr, "bass has no cell");
    CHECK(findParam("bass", "seq.density") == nullptr, "seq unresolved without hook");
    for(auto& e : effectTypes()){
        auto t = findType(e.owner);
        auto ef = t->param("effect");
        CHECK(std::find(ef->options.begin(), ef->options.end(), e.name) != ef->options.end(), "effect listed " + e.name);
        for(auto& s : e.settings) CHECK(t->param(s) != nullptr, "effect setting " + e.name + "." + s);
    }
    int ndest = 0;
    for(auto& t : instrumentTypes())
        for(auto& d : destinations(t.name)){ ++ndest; CHECK(findParam(t.name, d.first) == d.second, "dest resolves " + t.name + " " + d.first); }
    CHECK(ndest > 300, "many destinations");
    CHECK(allowsAnimated("pitchFine") && allowsAnimated("level") && !allowsAnimated("look.thickness"), "animated set");
}

static Scene sampleScene(){
    Scene s; s.name = "test"; s.bpm = 97.5; s.seed = 0xDEADBEEFCAFEull;
    s.layers = {LayerDef{16, "lighten", 1}, LayerDef{4.5, "add", 0.7}};
    HarmonySection a; a.name = "A"; a.meter.num = 7; a.meter.den = 8; a.meter.grouping = {2, 2, 3}; a.meter.swing = 0.2;
    a.scale.name = "micro"; a.scale.tonic = 3; a.scale.tuning.cents = {0, 190.5, 386.3, 498, 701.9, 884.4, 1088.3};
    a.steps = {ProgressionStep{0, 7, -1, ChordColour::Triad}, ProgressionStep{4, 7, 14, ChordColour::Add7}, ProgressionStep{3, 3.5, -1, ChordColour::Sus2}};
    a.rules.table = {{0.1, 0.9}, {0.5, 0.5}}; a.source = "rules"; a.lengthBeats = 28;
    HarmonySection b; b.name = "B";
    s.harmony.sections = {a, b}; s.harmony.override_.hold = true;
    Instance k; k.id = "kick1"; k.type = "kick"; k.params = {{"cell", "block"}, {"width", 0.3}};
    k.follow = Follow::Off; k.accent = 0.5;
    Instance m; m.id = "mel"; m.type = "melody"; m.layer = 1; m.params = {{"thickness", 9.5}}; m.seq = {{"density", 0.4}};
    m.phrase = {{"length", {8, 16}}}; m.follow = Follow::Pedal; m.pedalDegree = 4; m.swing = 0.1; m.muted = true;
    Routing r; r.source = "lfo1"; r.dest = "pitchFine"; r.amount = -0.25; r.level = LevelEvent; r.hasRange = true; r.min = -50; r.max = 50;
    r.curve = 2; r.invert = true; r.smooth = 0.5; r.sourceParams = {{"shape", "sine"}, {"rateBeats", 0.25}};
    setRoutingRender(r, "animated");
    Routing r2; r2.source = "random"; r2.dest = "depth"; r2.amount = 0.5; r2.level = LevelEvent;
    m.routings = {r, r2};
    s.instances = {k, m};
    Routing sr; sr.source = "macro1"; sr.dest = "mel:look.thickness"; sr.amount = 1; sr.level = LevelPhrase;
    s.routings = {sr};
    s.macros = {{"macro1", 0.3}};
    return s;
}

static void testRoundTrip(){
    Scene s = sampleScene();
    json j = toJson(s);
    Scene s2 = sceneFromJson(json::parse(j.dump()));
    CHECK(toJson(s2) == j, "scene round trip");
    CHECK(s2.seed == s.seed && s2.harmony.sections[0].meter.grouping == s.harmony.sections[0].meter.grouping, "scene fields");
    CHECK(s2.harmony.sections[0].scale.tuning.cents[1] == 190.5, "tuning cents");
    CHECK(s2.harmony.sections[0].steps[1].lengthMax == 14 && s2.harmony.sections[0].steps[1].colour == ChordColour::Add7, "steps");
    CHECK(routingRender(s2.instances[1].routings[0]) == "animated" && s2.instances[1].routings[0].sourceParams["shape"] == "sine", "routing render");
    CHECK(routingRender(s2.instances[1].routings[1]) == "drawn", "routing default render");
    Instance i2 = instanceFromJson(toJson(s.instances[1]));
    CHECK(toJson(i2) == toJson(s.instances[1]), "instance round trip");
    Harmony h2 = harmonyFromJson(toJson(s.harmony));
    CHECK(toJson(h2) == toJson(s.harmony), "harmony round trip");
    CHECK(validate(s).empty(), "sample scene valid");
    for(auto& p : validate(s)) std::cout << "  problem: " << p << "\n";
    CHECK(loadMigrated(j) == j, "migrate v1 identity");
}

static void testValidation(){
    Scene s = sampleScene();
    { Scene x = s; x.instances[0].type = "tuba"; CHECK(hasProblem(validate(x), "unknown type"), "unknown type"); }
    { Scene x = s; x.instances[0].params["thickness"] = 3; CHECK(hasProblem(validate(x), "unknown key"), "unknown key"); }
    { Scene x = s; x.instances[0].params["width"] = 99; CHECK(hasProblem(validate(x), "out of range"), "out of range"); }
    { Scene x = s; x.instances[0].params["cell"] = "zzz"; CHECK(hasProblem(validate(x), "unknown option"), "bad enum"); }
    { Scene x = s; x.instances[1].id = "kick1"; CHECK(hasProblem(validate(x), "duplicate instance id"), "duplicate id"); }
    { Scene x = s; x.instances[1].routings[1].dest = "look.nope"; CHECK(hasProblem(validate(x), "unknown destination"), "unknown dest"); }
    { Scene x = s; x.routings[0].dest = "ghost:look.level"; CHECK(hasProblem(validate(x), "unknown instance"), "unknown instance"); }
    { Scene x = s; x.instances[1].routings[1].dest = "layer.zoom"; CHECK(hasProblem(validate(x), "not allowed"), "level not allowed"); }
    { Scene x = s; x.instances[1].routings[1].dest = "look.look"; x.instances[1].routings[1].level = LevelLayer;
      CHECK(hasProblem(validate(x), "not allowed"), "structure at layer level"); }
    { Scene x = s; setRoutingRender(x.instances[1].routings[1], "animated"); x.instances[1].routings[1].dest = "look.thickness";
      CHECK(hasProblem(validate(x), "cannot be animated"), "animated non-event dest"); }
    { Scene x = s; setRoutingRender(x.instances[1].routings[1], "wobbly"); CHECK(hasProblem(validate(x), "unknown render"), "bad render"); }
    // unknown level / render names from a file: read tolerantly, reported by validate, kept on save
    { json j{{"source", "lfo1"}, {"dest", "look.level"}, {"amount", 0.5}, {"level", "bar"}, {"render", "wobbly"}, {"sourceParams", {{"shape", "saw"}}}};
      Routing r = routingFromJson(j);
      CHECK(r.level == LevelPhrase && r.sourceParams.value("shape", "") == "saw", "unknown level tolerant");
      Scene x = s; x.instances[1].routings[1] = r; auto pr = validate(x);
      CHECK(hasProblem(pr, "unknown level 'bar'"), "unknown level reported");
      CHECK(hasProblem(pr, "unknown render 'wobbly'"), "unknown render reported");
      json back = toJson(r);
      CHECK(back["level"] == "bar" && back["render"] == "wobbly" && !back["sourceParams"].contains("_level"), "unknown names round-trip");
      Routing ok = routingFromJson({{"source", "lfo1"}, {"dest", "look.level"}, {"level", "event"}});
      CHECK(ok.level == LevelEvent && routingLevelName(ok) == "event", "known level");
      Routing nl = routingFromJson({{"source", "lfo1"}, {"dest", "look.level"}, {"level", 3}});
      CHECK(nl.level == LevelPhrase && routingLevelName(nl) == "3", "non-string level kept"); }
    { Scene x = s; x.harmony.sections[0].meter.grouping = {2, 2}; CHECK(hasProblem(validate(x), "grouping"), "grouping"); }
    { Scene x = s; x.instances[0].layer = 5; CHECK(hasProblem(validate(x), "layer"), "layer range"); }
    // seq hook
    ParamDef dens; dens.key = "density"; dens.min = 0; dens.max = 1; dens.def = 0.5; dens.levels = kEPL;
    setSequencerSchema([&](const std::string& k)->const ParamDef*{ return k == "density" ? &dens : nullptr; });
    { Scene x = s; CHECK(validate(x).empty(), "seq valid with hook"); x.instances[1].seq["bogus"] = 1; CHECK(hasProblem(validate(x), "unknown sequencer key"), "seq unknown"); }
    setSequencerSchema(nullptr);
}

static void testProgressions(){
    Meter m44; Meter m78; m78.num = 7; m78.den = 8;
    std::vector<ProgressionStep> st; std::string err;
    CHECK(parseProgression("1 6 4 5", m44, st, err) && st.size() == 4 && st[1].degree == 5 && st[0].length == 4, "numbers");
    CHECK(parseProgression("I vi IV V", m44, st, err) && st.size() == 4 && st[0].degree == 0 && st[1].degree == 5 && st[2].degree == 3 && st[3].degree == 4, "roman");
    CHECK(parseProgression("ii iii VII", m44, st, err) && st[0].degree == 1 && st[1].degree == 2 && st[2].degree == 6, "roman more");
    // key shifts per step (semitones on the tonic)
    CHECK(parseProgression("I IV[+2] V[-1] vi", m44, st, err) && st.size() == 4 && st[0].keyShift == 0 && st[1].keyShift == 2 && st[1].degree == 3 && st[2].keyShift == -1, "key shift");
    CHECK(parseProgression("5:2(7)[-1] 1[+3]:2", m44, st, err) && st[0].length == 8 && st[0].colour == ChordColour::Add7 && st[0].keyShift == -1 && st[1].keyShift == 3 && st[1].length == 8, "key shift with length / colour");
    CHECK(!parseProgression("IV[2]", m44, st, err) && !parseProgression("IV[+x]", m44, st, err) && !parseProgression("IV[+2", m44, st, err) && !parseProgression("IV[+30]", m44, st, err), "bad key shifts");
    {
        ProgressionStep p; p.degree = 3; p.keyShift = -2;
        CHECK(stepFromJson(toJson(p)).keyShift == -2 && !toJson(ProgressionStep{}).contains("keyShift"), "key shift json");
        Harmony hk; hk.sections[0].steps = {p};
        std::vector<std::string> ve; validateHarmony(hk, ve); CHECK(ve.empty(), "key shift valid");
        hk.sections[0].steps[0].keyShift = 25; ve.clear(); validateHarmony(hk, ve); CHECK(!ve.empty(), "key shift out of range");
    }
    CHECK(parseProgression("1:2 6 4 5:2", m44, st, err) && st[0].length == 8 && st[1].length == 4 && st[3].length == 8, "lengths");
    CHECK(parseProgression("1:2 6", m78, st, err) && st[0].length == 14 && st[1].length == 7, "meter bars");
    CHECK(parseProgression("4:1-2", m44, st, err) && st[0].length == 4 && st[0].lengthMax == 8, "random range");
    CHECK(parseProgression("5(7) 2(9) 4(sus2) 1(sus4)", m44, st, err) && st[0].colour == ChordColour::Add7 && st[1].colour == ChordColour::Add9
          && st[2].colour == ChordColour::Sus2 && st[3].colour == ChordColour::Sus4, "colours");
    CHECK(parseProgression("V(7):2 IV:0.5(sus4)", m44, st, err) && st[0].length == 8 && st[0].colour == ChordColour::Add7 && st[1].length == 2, "mixed suffixes");
    for(const char* bad : {"", "8", "0", "x", "1:", "1:0", "4:2-1", "5(11)", "5(7", "iiii", "1:2:3", "6#", "1(7)(9)"}){
        CHECK(!parseProgression(bad, m44, st, err) && !err.empty(), std::string("rejects '") + bad + "'");
    }
}

static void testPresets(){
    Preset p; p.type = "bass"; p.name = "dub bass"; p.version = 1; p.tags = {"dub"};
    p.look = {{"thickness", 20}, {"partials", 2}}; p.seq = {{"density", 0.3}};
    Routing r; r.source = "random"; r.dest = "size"; r.level = LevelEvent; r.amount = 0.2;
    p.modulation = {r};
    p.knobs = {Knob{"grit", 0.5, {{"look.shimmer", 0, 1}, {"look.partials", 0, 8}}}};
    CHECK(validate(p).empty(), "preset valid");
    CHECK(toJson(presetFromJson(toJson(p))) == toJson(p), "preset round trip");

    InstanceOrigin o;
    Instance i = instanceFromPreset(p, "bass1", o);
    CHECK(i.preset == "dub bass v1" && o.version == 1 && !o.linked && i.params["thickness"] == 20, "copy");
    setOverride(i, o, "look", "thickness", 30);
    Preset p2 = p; p2.version = 2; p2.look = {{"thickness", 25}, {"partials", 5}, {"sub", 0.8}};
    p2.modulation.clear();
    CHECK(updateAvailable(o, p2), "update available");
    // editing the library does not change the instance
    CHECK(i.params["partials"] == 2, "copy not affected by library");
    CHECK(updateInstance(i, o, p2), "update applied");
    CHECK(i.params["thickness"] == 30 && i.params["partials"] == 5 && i.params["sub"] == 0.8 && i.routings.empty() && o.version == 2
          && i.preset == "dub bass v2", "update keeps overrides");

    // linked
    InstanceOrigin lo; Instance li = instanceFromPreset(p, "bass2", lo, true);
    SceneDoc d; d.scene.instances = {i, li}; d.origins = {{"bass1", o}, {"bass2", lo}};
    Preset p3 = p2; p3.version = 3; p3.look["partials"] = 7;
    CHECK(refreshLinked(d, {p3}) == 1 && d.scene.instances[1].params["partials"] == 7 && d.scene.instances[0].params["partials"] == 5, "linked follows");
    setOverride(d.scene.instances[1], d.origins["bass2"], "look", "partials", 1);
    CHECK(!d.origins["bass2"].linked, "override unlinks");
    // scene routing to a knob
    Routing kr; kr.source = "macro1"; kr.dest = "bass1:knob.grit"; d.scene.routings = {kr};
    CHECK(validate(d).empty(), "knob routing valid");
    d.scene.routings[0].dest = "bass1:knob.nope";
    CHECK(hasProblem(validate(d), "unknown knob"), "unknown knob");
    d.scene.routings.clear();
    json dj = toJson(d);
    CHECK(toJson(sceneDocFromJson(json::parse(dj.dump()))) == dj, "scene doc round trip");

    // kit dropped twice keeps its links
    Kit kit; kit.name = "drums";
    Instance kk; kk.id = "k"; kk.type = "kick";
    Instance sn; sn.id = "s"; sn.type = "snare"; sn.seq = {{"followId", "k"}};
    Routing fr; fr.source = "follow:k:density"; fr.dest = "look.grain"; fr.level = LevelPhrase; sn.routings = {fr};
    kit.instances = {kk, sn};
    CHECK(toJson(kitFromJson(toJson(kit))) == toJson(kit), "kit round trip");
    SceneDoc kd;
    dropKit(kd, kit);
    auto added = dropKit(kd, kit);
    CHECK(kd.scene.instances.size() == 4 && added[0] == "k_2" && added[1] == "s_2", "kit ids unique");
    CHECK(kd.scene.instances[3].seq["followId"] == "k_2" && kd.scene.instances[3].routings[0].source == "follow:k_2:density", "kit links rewritten");
    CHECK(validate(kd).empty(), "kit scene valid");
}

int main(){
    testTypes();
    testBlobSchema();
    testRoundTrip();
    testValidation();
    testProgressions();
    testPresets();
    std::cout << "test_schema: " << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
