//
//  v2Format.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The versioned file format (ROADMAP 5.3, 5.1): instrument presets, scenes and kits as JSON,
//  validation against the schema (v2Schema.h), migration hook, the typed progression parser (5.9)
//  and instance creation from a preset (copy, "based on <preset> vN", update keeping local
//  overrides, linked instances).
//
//  Things the contract (v2Core.h) has no field for, kept here instead:
//  - InstanceOrigin: the preset an instance came from, its version, linked flag, overridden keys and
//    a copy of the preset's knobs. A SceneDoc pairs a Scene with the origins of its instances; the
//    scene file writes the origin inside each instance entry.
//  - Routing render ("drawn" | "animated"): stored in Routing::sourceParams under "_render" in
//    memory (see routingRender()), written as a top-level "render" field in files.
//
//  Header-only, plain C++17 + nlohmann::json.
//

#pragma once

#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>
#include "v2Schema.h"

namespace forms2 {

static constexpr int kFormatVersion = 1;

// ---------------------------------------------------------------- extra structs
// A performance knob of a preset: one named control mapped to several destinations
struct KnobTarget { std::string dest; double min = 0, max = 1; };
struct Knob { std::string name; double value = 0.5; std::vector<KnobTarget> targets; };

// An instrument preset (one file per preset)
struct Preset {
    std::string type, name;
    int version = 1;
    std::vector<std::string> tags;
    std::string notes;
    json look = json::object();       // keys of the type's params
    json seq = json::object();        // sequencer keys
    json phrase = json::object();
    std::vector<Routing> modulation;
    std::vector<Knob> knobs;
};

// Where an instance came from (5.1 library vs scene)
struct InstanceOrigin {
    std::string preset;               // preset name ("" = made from scratch)
    int version = 0;
    bool linked = false;              // always follows the library
    std::set<std::string> overrides;  // "look.<k>", "seq.<k>", "phrase.<k>", "routings"
    std::vector<Knob> knobs;          // copied from the preset
};

// A scene file in memory
struct SceneDoc {
    Scene scene;
    std::map<std::string, InstanceOrigin> origins;   // by instance id
};

// A kit: instances saved together with their internal links (Follow, routings by id)
struct Kit {
    std::string name;
    int version = 1;
    std::vector<std::string> tags;
    std::string notes;
    std::vector<Instance> instances;
    std::map<std::string, InstanceOrigin> origins;
};

// ---------------------------------------------------------------- routing render option
inline std::string routingRender(const Routing& r){
    if(r.sourceParams.is_object() && r.sourceParams.contains("_render")) return r.sourceParams["_render"].get<std::string>();
    return "drawn";
}
inline void setRoutingRender(Routing& r, const std::string& mode){
    if(!r.sourceParams.is_object()) r.sourceParams = json::object();
    if(mode == "drawn") r.sourceParams.erase("_render"); else r.sourceParams["_render"] = mode;
    if(r.sourceParams.empty()) r.sourceParams = json();
}

// ---------------------------------------------------------------- enum names
inline bool levelFromName(const std::string& s, Level& out){
    if(s == "event") out = LevelEvent; else if(s == "phrase") out = LevelPhrase;
    else if(s == "layer") out = LevelLayer; else if(s == "scene") out = LevelScene;
    else return false;
    return true;
}
// tolerant: unknown names read as phrase (validate() reports them, see routingLevelName)
inline Level levelFromName(const std::string& s){ Level l = LevelPhrase; levelFromName(s, l); return l; }
// An unknown level name read from a file is kept in sourceParams "_level" (written back as is)
inline std::string routingLevelName(const Routing& r){
    if(r.sourceParams.is_object() && r.sourceParams.contains("_level") && r.sourceParams["_level"].is_string()) return r.sourceParams["_level"].get<std::string>();
    return levelName(r.level);
}
inline const char* followName(Follow f){
    switch(f){ case Follow::ScaleOnly: return "scale"; case Follow::Pedal: return "pedal"; case Follow::Off: return "off"; default: return "chord"; }
}
inline Follow followFromName(const std::string& s){
    if(s == "scale") return Follow::ScaleOnly; if(s == "pedal") return Follow::Pedal; if(s == "off") return Follow::Off;
    return Follow::Chord;
}
inline const char* colourName(ChordColour c){
    switch(c){ case ChordColour::Add7: return "7"; case ChordColour::Add9: return "9"; case ChordColour::Sus2: return "sus2"; case ChordColour::Sus4: return "sus4"; default: return "triad"; }
}
inline bool colourFromName(std::string s, ChordColour& out){
    for(auto& c : s) c = (char)std::tolower((unsigned char)c);
    if(s == "" || s == "triad") out = ChordColour::Triad;
    else if(s == "7" || s == "add7") out = ChordColour::Add7;
    else if(s == "9" || s == "add9") out = ChordColour::Add9;
    else if(s == "sus2") out = ChordColour::Sus2;
    else if(s == "sus4") out = ChordColour::Sus4;
    else return false;
    return true;
}

// ---------------------------------------------------------------- JSON: routing
inline json toJson(const Routing& r){
    json j{{"source", r.source}, {"dest", r.dest}, {"amount", r.amount}, {"level", routingLevelName(r)},
           {"curve", r.curve}, {"invert", r.invert}, {"smooth", r.smooth}, {"render", routingRender(r)}};
    if(r.hasRange){ j["min"] = r.min; j["max"] = r.max; }
    json sp = r.sourceParams;
    if(sp.is_object()){ sp.erase("_render"); sp.erase("_level"); }
    if(!sp.is_null() && !(sp.is_object() && sp.empty())) j["sourceParams"] = sp;
    return j;
}
inline Routing routingFromJson(const json& j){
    Routing r;
    r.source = j.value("source", "");
    r.dest = j.value("dest", "");
    r.amount = j.value("amount", 0.0);
    // level / render: non-strings and unknown names are kept for validate() to report
    auto name = [&](const char* k, const char* def){
        auto f = j.find(k); if(f == j.end()) return std::string(def);
        return f->is_string() ? f->get<std::string>() : f->dump();
    };
    std::string lv = name("level", "phrase");
    if(!levelFromName(lv, r.level)){ r.level = LevelPhrase; r.sourceParams["_level"] = lv; }
    r.curve = j.value("curve", 1.0);
    r.invert = j.value("invert", false);
    r.smooth = j.value("smooth", 0.0);
    if(j.contains("min") || j.contains("max")){ r.hasRange = true; r.min = j.value("min", 0.0); r.max = j.value("max", 1.0); }
    if(j.contains("sourceParams")){
        json lvKeep = r.sourceParams;
        r.sourceParams = j["sourceParams"];
        if(lvKeep.is_object() && lvKeep.contains("_level")){ if(!r.sourceParams.is_object()) r.sourceParams = json::object(); r.sourceParams["_level"] = lvKeep["_level"]; }
    }
    setRoutingRender(r, name("render", "drawn"));
    return r;
}
inline json toJson(const std::vector<Routing>& v){ json a = json::array(); for(auto& r : v) a.push_back(toJson(r)); return a; }
inline std::vector<Routing> routingsFromJson(const json& j){
    std::vector<Routing> v; if(j.is_array()) for(auto& e : j) v.push_back(routingFromJson(e)); return v;
}

// ---------------------------------------------------------------- JSON: harmony
inline json toJson(const Meter& m){ return {{"num", m.num}, {"den", m.den}, {"grouping", m.grouping}, {"swing", m.swing}}; }
inline Meter meterFromJson(const json& j){
    Meter m; m.num = j.value("num", 4); m.den = j.value("den", 4);
    m.grouping = j.value("grouping", std::vector<int>{}); m.swing = j.value("swing", 0.0); return m;
}
inline json toJson(const Scale& s){
    return {{"name", s.name}, {"semis", s.semis}, {"tonic", s.tonic},
            {"tuning", {{"cents", s.tuning.cents}, {"periodCents", s.tuning.periodCents}}}};
}
inline Scale scaleFromJson(const json& j){
    Scale s; s.name = j.value("name", ""); s.semis = j.value("semis", s.semis); s.tonic = j.value("tonic", 0);
    if(j.contains("tuning")){ s.tuning.cents = j["tuning"].value("cents", std::vector<double>{}); s.tuning.periodCents = j["tuning"].value("periodCents", 1200.0); }
    return s;
}
inline json toJson(const ProgressionStep& p){
    json j{{"degree", p.degree}, {"length", p.length}, {"colour", colourName(p.colour)}};
    if(p.lengthMax > p.length) j["lengthMax"] = p.lengthMax;
    if(p.keyShift != 0) j["keyShift"] = p.keyShift;
    return j;
}
inline ProgressionStep stepFromJson(const json& j){
    ProgressionStep p; p.degree = j.value("degree", 0); p.length = j.value("length", 4.0);
    p.lengthMax = j.value("lengthMax", -1.0);
    p.keyShift = j.value("keyShift", 0);
    colourFromName(j.value("colour", "triad"), p.colour);
    return p;
}
inline json toJson(const HarmonySection& s){
    json steps = json::array(); for(auto& p : s.steps) steps.push_back(toJson(p));
    return {{"name", s.name}, {"meter", toJson(s.meter)}, {"scale", toJson(s.scale)}, {"source", s.source},
            {"steps", steps}, {"lengthBeats", s.lengthBeats},
            {"rules", {{"table", s.rules.table}, {"cadenceDegree", s.rules.cadenceDegree},
                       {"tension", s.rules.tension}, {"stepBeats", s.rules.stepBeats}}}};
}
inline HarmonySection sectionFromJson(const json& j){
    HarmonySection s;
    s.name = j.value("name", "");
    if(j.contains("meter")) s.meter = meterFromJson(j["meter"]);
    if(j.contains("scale")) s.scale = scaleFromJson(j["scale"]);
    s.source = j.value("source", "typed");
    if(j.contains("steps")){ s.steps.clear(); for(auto& e : j["steps"]) s.steps.push_back(stepFromJson(e)); }
    s.lengthBeats = j.value("lengthBeats", 0.0);
    if(j.contains("rules")){
        auto& r = j["rules"];
        s.rules.table = r.value("table", std::vector<std::vector<double>>{});
        s.rules.cadenceDegree = r.value("cadenceDegree", 0);
        s.rules.tension = r.value("tension", 0.3);
        s.rules.stepBeats = r.value("stepBeats", 4.0);
    }
    return s;
}
inline json toJson(const Harmony& h){
    json a = json::array(); for(auto& s : h.sections) a.push_back(toJson(s));
    return {{"sections", a}, {"override", {{"hold", h.override_.hold}, {"jumpDegree", h.override_.jumpDegree}}}};
}
inline Harmony harmonyFromJson(const json& j){
    Harmony h;
    if(j.contains("sections") && !j["sections"].empty()){ h.sections.clear(); for(auto& e : j["sections"]) h.sections.push_back(sectionFromJson(e)); }
    if(j.contains("override")){ h.override_.hold = j["override"].value("hold", false); h.override_.jumpDegree = j["override"].value("jumpDegree", -1000); }
    return h;
}

// ---------------------------------------------------------------- JSON: knobs, origin, instance
inline json toJson(const Knob& k){
    json t = json::array(); for(auto& x : k.targets) t.push_back({{"dest", x.dest}, {"min", x.min}, {"max", x.max}});
    return {{"name", k.name}, {"value", k.value}, {"targets", t}};
}
inline Knob knobFromJson(const json& j){
    Knob k; k.name = j.value("name", ""); k.value = j.value("value", 0.5);
    if(j.contains("targets")) for(auto& t : j["targets"]) k.targets.push_back({t.value("dest", ""), t.value("min", 0.0), t.value("max", 1.0)});
    return k;
}
inline json knobsToJson(const std::vector<Knob>& v){ json a = json::array(); for(auto& k : v) a.push_back(toJson(k)); return a; }
inline std::vector<Knob> knobsFromJson(const json& j){ std::vector<Knob> v; if(j.is_array()) for(auto& e : j) v.push_back(knobFromJson(e)); return v; }

inline json toJson(const InstanceOrigin& o){
    return {{"preset", o.preset}, {"version", o.version}, {"linked", o.linked},
            {"overrides", std::vector<std::string>(o.overrides.begin(), o.overrides.end())}, {"knobs", knobsToJson(o.knobs)}};
}
inline InstanceOrigin originFromJson(const json& j){
    InstanceOrigin o; o.preset = j.value("preset", ""); o.version = j.value("version", 0); o.linked = j.value("linked", false);
    for(auto& s : j.value("overrides", std::vector<std::string>{})) o.overrides.insert(s);
    if(j.contains("knobs")) o.knobs = knobsFromJson(j["knobs"]);
    return o;
}

inline json toJson(const Instance& i){
    return {{"id", i.id}, {"type", i.type}, {"preset", i.preset}, {"layer", i.layer}, {"params", i.params},
            {"seq", i.seq}, {"phrase", i.phrase}, {"follow", followName(i.follow)}, {"pedalDegree", i.pedalDegree},
            {"accent", i.accent}, {"swing", i.swing}, {"routings", toJson(i.routings)}, {"muted", i.muted}, {"locked", i.locked}};
}
inline Instance instanceFromJson(const json& j){
    Instance i;
    i.id = j.value("id", ""); i.type = j.value("type", ""); i.preset = j.value("preset", "");
    i.layer = j.value("layer", 0);
    i.params = j.value("params", json::object()); i.seq = j.value("seq", json::object()); i.phrase = j.value("phrase", json::object());
    i.follow = followFromName(j.value("follow", "chord")); i.pedalDegree = j.value("pedalDegree", 0);
    i.accent = j.value("accent", 0.0); i.swing = j.value("swing", -1.0);
    if(j.contains("routings")) i.routings = routingsFromJson(j["routings"]);
    i.muted = j.value("muted", false); i.locked = j.value("locked", false);
    return i;
}

// ---------------------------------------------------------------- JSON: scene, scene doc
inline json toJson(const Scene& s){
    json layers = json::array();
    for(auto& l : s.layers) layers.push_back({{"zoomBeats", l.zoomBeats}, {"blend", l.blend}, {"gain", l.gain}});
    json inst = json::array(); for(auto& i : s.instances) inst.push_back(toJson(i));
    return {{"format", "espectres.scene"}, {"formatVersion", kFormatVersion}, {"name", s.name}, {"version", s.version},
            {"bpm", s.bpm}, {"seed", s.seed}, {"harmony", toJson(s.harmony)}, {"layers", layers}, {"instances", inst},
            {"routings", toJson(s.routings)}, {"macros", s.macros}};
}
inline Scene sceneFromJson(const json& j){
    Scene s;
    s.name = j.value("name", ""); s.version = j.value("version", 1); s.bpm = j.value("bpm", 120.0);
    s.seed = j.value("seed", (uint64_t)1);
    if(j.contains("harmony")) s.harmony = harmonyFromJson(j["harmony"]);
    if(j.contains("layers")){
        s.layers.clear();
        for(auto& l : j["layers"]){ LayerDef d; d.zoomBeats = l.value("zoomBeats", 16.0); d.blend = l.value("blend", "lighten"); d.gain = l.value("gain", 1.0); s.layers.push_back(d); }
    }
    if(j.contains("instances")) for(auto& e : j["instances"]) s.instances.push_back(instanceFromJson(e));
    if(j.contains("routings")) s.routings = routingsFromJson(j["routings"]);
    s.macros = j.value("macros", std::map<std::string, double>{});
    return s;
}
inline json toJson(const SceneDoc& d){
    json j = toJson(d.scene);
    for(auto& e : j["instances"]){
        auto f = d.origins.find(e["id"].get<std::string>());
        if(f != d.origins.end()) e["origin"] = toJson(f->second);
    }
    return j;
}
inline SceneDoc sceneDocFromJson(const json& j){
    SceneDoc d; d.scene = sceneFromJson(j);
    if(j.contains("instances")) for(auto& e : j["instances"]) if(e.contains("origin")) d.origins[e.value("id", "")] = originFromJson(e["origin"]);
    return d;
}

// ---------------------------------------------------------------- JSON: preset, kit
inline json toJson(const Preset& p){
    return {{"format", "espectres.instrument"}, {"formatVersion", kFormatVersion}, {"type", p.type}, {"name", p.name},
            {"version", p.version}, {"tags", p.tags}, {"notes", p.notes}, {"look", p.look}, {"sequencer", p.seq},
            {"phrase", p.phrase}, {"modulation", toJson(p.modulation)}, {"knobs", knobsToJson(p.knobs)}};
}
inline Preset presetFromJson(const json& j){
    Preset p; p.type = j.value("type", ""); p.name = j.value("name", ""); p.version = j.value("version", 1);
    p.tags = j.value("tags", std::vector<std::string>{}); p.notes = j.value("notes", "");
    p.look = j.value("look", json::object()); p.seq = j.value("sequencer", json::object()); p.phrase = j.value("phrase", json::object());
    if(j.contains("modulation")) p.modulation = routingsFromJson(j["modulation"]);
    if(j.contains("knobs")) p.knobs = knobsFromJson(j["knobs"]);
    return p;
}
inline json toJson(const Kit& k){
    json inst = json::array();
    for(auto& i : k.instances){
        json e = toJson(i);
        auto f = k.origins.find(i.id); if(f != k.origins.end()) e["origin"] = toJson(f->second);
        inst.push_back(e);
    }
    return {{"format", "espectres.kit"}, {"formatVersion", kFormatVersion}, {"name", k.name}, {"version", k.version},
            {"tags", k.tags}, {"notes", k.notes}, {"instances", inst}};
}
inline Kit kitFromJson(const json& j){
    Kit k; k.name = j.value("name", ""); k.version = j.value("version", 1);
    k.tags = j.value("tags", std::vector<std::string>{}); k.notes = j.value("notes", "");
    if(j.contains("instances")) for(auto& e : j["instances"]){
        k.instances.push_back(instanceFromJson(e));
        if(e.contains("origin")) k.origins[k.instances.back().id] = originFromJson(e["origin"]);
    }
    return k;
}

// ---------------------------------------------------------------- migration
// Bring a file written by an older format version up to kFormatVersion. Version 1 is current.
inline json migrate(const json& j, int fromVersion){
    json out = j;
    // (future: if(fromVersion < 2){ ... })
    (void)fromVersion;
    out["formatVersion"] = kFormatVersion;
    return out;
}
inline json loadMigrated(const json& j){ return migrate(j, j.value("formatVersion", 1)); }

// ---------------------------------------------------------------- validation
// Check a value against a ParamDef; returns "" when fine
inline std::string checkValue(const ParamDef& d, const json& v){
    auto num = [&](const json& x, std::string& err)->bool{
        if(!x.is_number()){ err = "expects a number"; return false; }
        double f = x.get<double>();
        if(d.type == "int" || d.type == "intList" || d.type == "intRange"){
            if(std::floor(f) != f){ err = "expects an integer"; return false; }
        }
        if(f < d.min || f > d.max){
            char b[96]; std::snprintf(b, sizeof b, "value %g out of range [%g, %g]", f, d.min, d.max); err = b; return false;
        }
        return true;
    };
    std::string err;
    if(d.type == "float" || d.type == "int"){ num(v, err); return err; }
    if(d.type == "bool") return v.is_boolean() ? "" : "expects true / false";
    if(d.type == "string") return v.is_string() ? "" : "expects text";
    if(d.type == "enum"){
        if(!v.is_string()) return "expects one of its options";
        for(auto& o : d.options) if(o == v.get<std::string>()) return "";
        return "unknown option '" + v.get<std::string>() + "'";
    }
    if(d.type == "intList"){
        if(!v.is_array()) return "expects a list";
        for(auto& e : v) if(!num(e, err)) return err;
        return "";
    }
    if(d.type == "floatRange" || d.type == "intRange"){
        if(!v.is_array() || v.size() != 2) return "expects [min, max]";
        for(auto& e : v) if(!num(e, err)) return err;
        if(v[0].get<double>() > v[1].get<double>()) return "range min above max";
        return "";
    }
    return "";
}

// Check one routing against its destination (inside an instance of `type`)
inline void validateRouting(const Routing& r, const TypeDef* type, const std::vector<Knob>* knobs,
                            const std::string& where, std::vector<std::string>& out){
    std::string dest = r.dest;
    std::string rm = routingRender(r);
    if(rm != "drawn" && rm != "animated") out.push_back(where + ": routing to '" + dest + "' has unknown render '" + rm + "'");
    if(routingLevelName(r) != levelName(r.level)) out.push_back(where + ": routing to '" + dest + "' has unknown level '" + routingLevelName(r) + "'");
    if(startsWith(dest, "knob.")){
        bool ok = false;
        if(knobs) for(auto& k : *knobs) if(k.name == dest.substr(5)) ok = true;
        if(!ok) out.push_back(where + ": routing to unknown knob '" + dest + "'");
        if(rm == "animated") out.push_back(where + ": routing to '" + dest + "' cannot be animated");
        return;
    }
    if(startsWith(dest, "seq.") && !sequencerSchema()) return;   // owned by the sequencer; unchecked here
    const ParamDef* d = findParam(type, dest);
    if(!d){ out.push_back(where + ": routing to unknown destination '" + dest + "'"); return; }
    if(!(d->levels & r.level))
        out.push_back(where + ": routing to '" + dest + "' uses level " + levelName(r.level) + ", not allowed there");
    if(rm == "animated" && !allowsAnimated(dest))
        out.push_back(where + ": routing to '" + dest + "' cannot be animated (only level / pitchFine / depth / size / brightness / height / partialCount / partialBright)");
}

inline void validateParams(const json& params, const TypeDef* t, const std::string& where, std::vector<std::string>& out){
    if(!t || !params.is_object()) return;
    for(auto it = params.begin(); it != params.end(); ++it){
        const ParamDef* d = t->param(it.key());
        if(!d && it.key().compare(0, 4, "path") == 0) d = findIn(eventParams(), it.key());   // 3D path (5.14)
        if(!d){ out.push_back(where + ": unknown key '" + it.key() + "' for type " + t->name); continue; }
        std::string e = checkValue(*d, it.value());
        if(!e.empty()) out.push_back(where + ": '" + it.key() + "' " + e);
    }
}
inline void validateSeq(const json& seq, const std::string& where, std::vector<std::string>& out){
    if(!sequencerSchema() || !seq.is_object()) return;
    for(auto it = seq.begin(); it != seq.end(); ++it){
        const ParamDef* d = sequencerSchema()(it.key());
        if(!d){ out.push_back(where + ": unknown sequencer key '" + it.key() + "'"); continue; }
        std::string e = checkValue(*d, it.value());
        if(!e.empty()) out.push_back(where + ": seq '" + it.key() + "' " + e);
    }
}

inline void validateInstance(const Instance& i, const InstanceOrigin* o, std::vector<std::string>& out){
    std::string where = "instance '" + i.id + "'";
    if(i.id.empty()) out.push_back("instance without id");
    const TypeDef* t = findType(i.type);
    if(!t){ out.push_back(where + ": unknown type '" + i.type + "'"); return; }
    if(i.layer < 0 || i.layer > 3) out.push_back(where + ": layer " + std::to_string(i.layer) + " out of range [0, 3]");
    validateParams(i.params, t, where, out);
    validateSeq(i.seq, where, out);
    for(auto& r : i.routings) validateRouting(r, t, o ? &o->knobs : nullptr, where, out);
}

inline void validateHarmony(const Harmony& h, std::vector<std::string>& out){
    for(size_t s = 0; s < h.sections.size(); ++s){
        auto& sec = h.sections[s];
        std::string where = "harmony section " + std::to_string(s);
        if(sec.meter.num < 1 || sec.meter.den < 1) out.push_back(where + ": bad meter");
        if(!sec.meter.grouping.empty()){
            int sum = 0; for(int g : sec.meter.grouping) sum += g;
            if(sum != sec.meter.num) out.push_back(where + ": meter grouping does not add up to the numerator");
        }
        if(!sec.scale.tuning.cents.empty() && sec.scale.tuning.cents.size() != sec.scale.semis.size() && !sec.scale.semis.empty())
            ; // a tuning may define its own step count; allowed
        if(sec.steps.empty()) out.push_back(where + ": no progression steps");
        for(auto& p : sec.steps) if(p.length <= 0) out.push_back(where + ": step with length <= 0");
        for(auto& p : sec.steps) if(std::abs(p.keyShift) > 24) out.push_back(where + ": step key shift beyond +-24 semitones");
    }
}

// Scene-level routings point at "<instanceId>:<dest>", "layerN.<k>" or "scene.<k>"
inline std::vector<std::string> validate(const SceneDoc& d){
    std::vector<std::string> out;
    const Scene& s = d.scene;
    std::set<std::string> ids;
    for(auto& i : s.instances){
        if(!ids.insert(i.id).second) out.push_back("duplicate instance id '" + i.id + "'");
        auto f = d.origins.find(i.id);
        validateInstance(i, f == d.origins.end() ? nullptr : &f->second, out);
    }
    if(s.layers.size() > 4) out.push_back("more than 4 layers");
    for(auto& l : s.layers) if(l.blend != "lighten" && l.blend != "add" && l.blend != "normal") out.push_back("layer with unknown blend '" + l.blend + "'");
    validateHarmony(s.harmony, out);
    for(auto& r : s.routings){
        size_t c = r.dest.find(':');
        if(c != std::string::npos){
            std::string id = r.dest.substr(0, c);
            const Instance* inst = nullptr;
            for(auto& i : s.instances) if(i.id == id) inst = &i;
            if(!inst){ out.push_back("scene routing to unknown instance '" + id + "'"); continue; }
            Routing sub = r; sub.dest = r.dest.substr(c + 1);
            auto f = d.origins.find(id);
            validateRouting(sub, findType(inst->type), f == d.origins.end() ? nullptr : &f->second.knobs, "scene routing", out);
        } else {
            validateRouting(r, nullptr, nullptr, "scene routing", out);
        }
    }
    return out;
}
inline std::vector<std::string> validate(const Scene& s){ SceneDoc d; d.scene = s; return validate(d); }

inline std::vector<std::string> validate(const Preset& p){
    std::vector<std::string> out;
    const TypeDef* t = findType(p.type);
    std::string where = "preset '" + p.name + "'";
    if(!t){ out.push_back(where + ": unknown type '" + p.type + "'"); return out; }
    validateParams(p.look, t, where, out);
    validateSeq(p.seq, where, out);
    for(auto& r : p.modulation) validateRouting(r, t, &p.knobs, where, out);
    for(auto& k : p.knobs) for(auto& x : k.targets)
        if(!(startsWith(x.dest, "seq.") && !sequencerSchema()) && !findParam(t, x.dest))
            out.push_back(where + ": knob '" + k.name + "' targets unknown destination '" + x.dest + "'");
    return out;
}

inline std::vector<std::string> validate(const Kit& k){
    SceneDoc d; d.scene.instances = k.instances; d.origins = k.origins;
    return validate(d);
}

// ---------------------------------------------------------------- typed progressions (5.9)
// "1 6 4 5", "I vi IV V", lengths in bars "1:2 6 4 5:2", random ranges "4:1-2", colours "5(7)",
// "2(9)", "4(sus2)", "1(sus4)", key shifts in semitones "IV[+2]", "5:2(7)[-1]" (moves the tonic for
// that step; suffixes in any order). Degrees are 1-based when typed, 0-based in the result. Lengths are
// converted to beats with the meter's bar. Returns false and fills `error` on malformed input.
inline bool parseProgression(const std::string& text, const Meter& meter, std::vector<ProgressionStep>& steps,
                             std::string& error, int scaleSize = 7){
    steps.clear(); error.clear();
    std::istringstream in(text);
    std::string tok;
    auto fail = [&](const std::string& t, const std::string& why){ error = "'" + t + "': " + why; steps.clear(); return false; };
    auto parseNum = [](const std::string& s, double& v)->bool{
        if(s.empty()) return false;
        char* end = nullptr; v = std::strtod(s.c_str(), &end);
        return end && *end == 0 && v > 0;
    };
    while(in >> tok){
        size_t i = 0;
        ProgressionStep st;
        int deg = 0;
        if(std::isdigit((unsigned char)tok[0])){
            while(i < tok.size() && std::isdigit((unsigned char)tok[i])) deg = deg * 10 + (tok[i++] - '0');
        } else {
            std::string r;
            while(i < tok.size() && (std::tolower((unsigned char)tok[i]) == 'i' || std::tolower((unsigned char)tok[i]) == 'v'))
                r += (char)std::tolower((unsigned char)tok[i++]);
            static const char* romans[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
            for(int k = 0; k < 7; ++k) if(r == romans[k]) deg = k + 1;
            if(deg == 0) return fail(tok, "not a degree (use 1-7 or I-VII)");
        }
        if(deg < 1 || deg > scaleSize) return fail(tok, "degree out of the scale (1-" + std::to_string(scaleSize) + ")");
        st.degree = deg - 1;
        bool hadColour = false, hadLen = false, hadKey = false;
        while(i < tok.size()){
            if(tok[i] == '(' && !hadColour){
                size_t close = tok.find(')', i);
                if(close == std::string::npos) return fail(tok, "missing ')'");
                if(!colourFromName(tok.substr(i + 1, close - i - 1), st.colour) || close == i + 1)
                    return fail(tok, "unknown colour (7, 9, sus2, sus4)");
                hadColour = true; i = close + 1;
            } else if(tok[i] == '[' && !hadKey){
                size_t close = tok.find(']', i);
                if(close == std::string::npos) return fail(tok, "missing ']'");
                std::string k = tok.substr(i + 1, close - i - 1);
                char* end = nullptr; long v = std::strtol(k.c_str(), &end, 10);
                if(k.size() < 2 || (k[0] != '+' && k[0] != '-') || !end || *end != 0) return fail(tok, "bad key shift (use [+2] / [-1])");
                if(std::labs(v) > 24) return fail(tok, "key shift beyond +-24 semitones");
                st.keyShift = (int)v; hadKey = true; i = close + 1;
            } else if(tok[i] == ':' && !hadLen){
                size_t end = tok.find_first_of("([", i);
                std::string len = tok.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
                size_t dash = len.find('-');
                double a = 0, b = 0;
                if(dash == std::string::npos){
                    if(!parseNum(len, a)) return fail(tok, "bad length");
                } else {
                    if(!parseNum(len.substr(0, dash), a) || !parseNum(len.substr(dash + 1), b)) return fail(tok, "bad length range");
                    if(b <= a) return fail(tok, "length range must go up");
                    st.lengthMax = b * meter.barBeats();
                }
                st.length = a * meter.barBeats();
                hadLen = true; i = (end == std::string::npos) ? tok.size() : end;
            } else {
                return fail(tok, "unexpected '" + std::string(1, tok[i]) + "'");
            }
        }
        if(!hadLen) st.length = meter.barBeats();
        steps.push_back(st);
    }
    if(steps.empty()){ error = "empty progression"; return false; }
    return true;
}

// ---------------------------------------------------------------- instances from presets (5.1)
inline std::string basedOnLabel(const Preset& p){ return p.name + " v" + std::to_string(p.version); }

// Copy a preset into a new instance (editing the library later never changes it)
inline Instance instanceFromPreset(const Preset& p, const std::string& id, InstanceOrigin& origin, bool linked = false){
    Instance i;
    i.id = id; i.type = p.type; i.preset = basedOnLabel(p);
    i.params = p.look; i.seq = p.seq; i.phrase = p.phrase; i.routings = p.modulation;
    origin = InstanceOrigin{};
    origin.preset = p.name; origin.version = p.version; origin.linked = linked; origin.knobs = p.knobs;
    return i;
}

// Set a local override. Section is "look", "seq" or "phrase". Editing a linked instance unlinks it
// (copy on write), so a house sound never changes under other scenes because of one local tweak.
inline void setOverride(Instance& i, InstanceOrigin& o, const std::string& section, const std::string& key, const json& v){
    json& target = section == "seq" ? i.seq : section == "phrase" ? i.phrase : i.params;
    target[key] = v;
    o.overrides.insert(section + "." + key);
    o.linked = false;
}
inline void overrideRoutings(Instance& i, InstanceOrigin& o, std::vector<Routing> r){
    i.routings = std::move(r); o.overrides.insert("routings"); o.linked = false;
}

// Is the library preset newer than what the instance is based on?
inline bool updateAvailable(const InstanceOrigin& o, const Preset& p){ return o.preset == p.name && p.version > o.version; }

// Apply a (newer) preset, keeping local overrides. Returns false if it is another preset.
inline bool updateInstance(Instance& i, InstanceOrigin& o, const Preset& p){
    if(o.preset != p.name || i.type != p.type) return false;
    auto merge = [&](json& target, const json& from, const std::string& section){
        json next = from.is_object() ? from : json::object();
        for(auto& k : o.overrides)
            if(startsWith(k, section + ".")){
                std::string key = k.substr(section.size() + 1);
                if(target.contains(key)) next[key] = target[key];
            }
        target = next;
    };
    merge(i.params, p.look, "look");
    merge(i.seq, p.seq, "seq");
    merge(i.phrase, p.phrase, "phrase");
    if(!o.overrides.count("routings")) i.routings = p.modulation;
    o.knobs = p.knobs;
    o.version = p.version;
    i.preset = basedOnLabel(p);
    return true;
}

// Linked instances always follow the library: update every linked instance of the doc from it
inline int refreshLinked(SceneDoc& d, const std::vector<Preset>& library){
    int n = 0;
    for(auto& i : d.scene.instances){
        auto f = d.origins.find(i.id);
        if(f == d.origins.end() || !f->second.linked) continue;
        for(auto& p : library) if(updateAvailable(f->second, p) && updateInstance(i, f->second, p)) ++n;
    }
    return n;
}

// Turn a tweaked instance into a new preset ("save as new")
inline Preset presetFromInstance(const Instance& i, const InstanceOrigin* o, const std::string& name){
    Preset p; p.type = i.type; p.name = name; p.version = 1;
    p.look = i.params; p.seq = i.seq; p.phrase = i.phrase; p.modulation = i.routings;
    if(o) p.knobs = o->knobs;
    return p;
}

// Drop a kit into a scene: ids get a unique suffix and the kit's internal links (Follow sources,
// routing destinations, string values in seq equal to an id) are rewritten, so a kit can be dropped twice
inline std::vector<std::string> dropKit(SceneDoc& d, const Kit& k){
    std::set<std::string> used;
    for(auto& i : d.scene.instances) used.insert(i.id);
    std::map<std::string, std::string> rename;
    for(auto& i : k.instances){
        std::string id = i.id; int n = 2;
        while(used.count(id)) id = i.id + "_" + std::to_string(n++);
        used.insert(id); rename[i.id] = id;
    }
    auto fix = [&](std::string s){
        for(auto& kv : rename){
            if(s == kv.first) return kv.second;
            std::string pre = "follow:" + kv.first + ":";
            if(startsWith(s, pre)) return "follow:" + kv.second + ":" + s.substr(pre.size());
            if(startsWith(s, kv.first + ":")) return kv.second + s.substr(kv.first.size());
        }
        return s;
    };
    std::vector<std::string> added;
    for(auto i : k.instances){
        std::string old = i.id;
        i.id = rename[old];
        for(auto& r : i.routings){ r.source = fix(r.source); r.dest = fix(r.dest); }
        for(auto it = i.seq.begin(); it != i.seq.end(); ++it) if(it.value().is_string()) it.value() = fix(it.value().get<std::string>());
        auto f = k.origins.find(old);
        if(f != k.origins.end()) d.origins[i.id] = f->second;
        d.scene.instances.push_back(i);
        added.push_back(i.id);
    }
    return added;
}

} // namespace forms2
