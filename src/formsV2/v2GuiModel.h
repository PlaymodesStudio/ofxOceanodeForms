//
//  v2GuiModel.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The non-drawing side of the v2 GUI (ROADMAP 5.6): selection, undo / redo by scene snapshots,
//  scene edits on the scene JSON (parameters, routings, instances), the modulation matrix view,
//  the instrument preset library (scan, filter, seed from scenes) and plain-language helpers.
//  No ImGui and no openFrameworks here, so it is tested headless (tests/v2/test_gui_model.cpp).
//

#pragma once

#include "v2Format.h"
#include "v2Sequencer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace forms2 {
namespace gui {

// ---------------------------------------------------------------- undo / redo
// Each step is the scene text before an edit. Push once when an edit starts (a knob drag is one step).
struct History {
    struct Step { std::string text, label; };
    std::vector<Step> undo, redo;
    size_t limit = 200;

    void push(const std::string& before, const std::string& label){
        if(!undo.empty() && undo.back().text == before && undo.back().label == label) return;
        undo.push_back({before, label});
        if(undo.size() > limit) undo.erase(undo.begin());
        redo.clear();
    }
    bool canUndo() const { return !undo.empty(); }
    bool canRedo() const { return !redo.empty(); }
    std::string undoLabel() const { return undo.empty() ? "" : undo.back().label; }
    std::string redoLabel() const { return redo.empty() ? "" : redo.back().label; }
    // current: the scene text now; out: the text to load
    bool stepBack(const std::string& current, std::string& out){
        if(undo.empty()) return false;
        Step s = undo.back(); undo.pop_back();
        redo.push_back({current, s.label});
        out = s.text; return true;
    }
    bool stepForward(const std::string& current, std::string& out){
        if(redo.empty()) return false;
        Step s = redo.back(); redo.pop_back();
        undo.push_back({current, s.label});
        out = s.text; return true;
    }
    void clear(){ undo.clear(); redo.clear(); }
};

// ---------------------------------------------------------------- selection and view state
enum class Screen : int { Arrangement = 0, Instrument = 1, Collection = 2, Matrix = 3, Assistant = 4 };
enum class Mode : int { Edit = 0, Perform = 1 };

struct Selection {
    std::string instance;                 // instance id ("" none)
    int matrixSource = -1;                // row of the matrix view
    std::string matrixDest;               // column key ("<id>:<dest>" or "scene.<k>")
    std::string preset;                   // collection tile (file path)
};

// ---------------------------------------------------------------- small helpers
inline json* findInstance(json& scene, const std::string& id){
    if(!scene.contains("instances") || !scene["instances"].is_array()) return nullptr;
    for(auto& e : scene["instances"]) if(e.value("id", std::string()) == id) return &e;
    return nullptr;
}
inline const json* findInstance(const json& scene, const std::string& id){
    if(!scene.contains("instances") || !scene["instances"].is_array()) return nullptr;
    for(auto& e : scene["instances"]) if(e.value("id", std::string()) == id) return &e;
    return nullptr;
}
inline std::vector<std::string> instanceIds(const json& scene){
    std::vector<std::string> v;
    if(scene.contains("instances") && scene["instances"].is_array())
        for(auto& e : scene["instances"]) v.push_back(e.value("id", std::string()));
    return v;
}
inline std::string uniqueId(const json& scene, const std::string& base0){
    std::string base = base0.empty() ? "inst" : base0;
    for(auto& c : base) if(!(std::isalnum((unsigned char)c) || c == '_')) c = '_';
    auto ids = instanceIds(scene);
    std::set<std::string> used(ids.begin(), ids.end());
    std::string id = base; int n = 2;
    while(used.count(id)) id = base + "_" + std::to_string(n++);
    return id;
}
inline std::string familyOf(const std::string& type){ auto t = findType(type); return t ? t->family : ""; }

// section "look" | "seq" | "phrase" -> instance json key
inline const char* sectionKey(const std::string& section){
    return section == "seq" ? "seq" : section == "phrase" ? "phrase" : "params";
}
// the schema of a key in a section ("phrase" has none)
inline const ParamDef* paramOf(const std::string& type, const std::string& section, const std::string& key){
    if(section == "seq") return sequencerParam(key);
    if(section == "look") return findParam(type, "look." + key);
    return nullptr;
}
// the value of a key with the schema default
inline json valueOf(const json& inst, const std::string& section, const std::string& key){
    const char* sk = sectionKey(section);
    if(inst.contains(sk) && inst[sk].is_object() && inst[sk].contains(key)) return inst[sk][key];
    const ParamDef* d = paramOf(inst.value("type", std::string()), section, key);
    return d ? d->def : json();
}
inline double numberOf(const json& inst, const std::string& section, const std::string& key, double def = 0){
    json v = valueOf(inst, section, key);
    if(v.is_number()) return v.get<double>();
    if(v.is_boolean()) return v.get<bool>() ? 1 : 0;
    if(v.is_array() && !v.empty() && v[0].is_number()) return v[0].get<double>();
    return def;
}

// ---------------------------------------------------------------- scene edits (return "" or a problem)
// a parameter of an instance; marks it as a local override when the instance came from a preset
inline std::string setParam(json& scene, const std::string& id, const std::string& section, const std::string& key, const json& v){
    json* in = findInstance(scene, id);
    if(!in) return "no instance '" + id + "'";
    if(const ParamDef* d = paramOf(in->value("type", std::string()), section, key)){
        std::string e = checkValue(*d, v);
        if(!e.empty()) return d->label + ": " + e;
    }
    const char* sk = sectionKey(section);
    if(!in->contains(sk) || !(*in)[sk].is_object()) (*in)[sk] = json::object();
    (*in)[sk][key] = v;
    if(in->contains("origin") && (*in)["origin"].is_object()){
        auto& o = (*in)["origin"];
        std::set<std::string> ov;
        for(auto& s : o.value("overrides", std::vector<std::string>{})) ov.insert(s);
        ov.insert(section + "." + key);
        o["overrides"] = std::vector<std::string>(ov.begin(), ov.end());
        o["linked"] = false;
    }
    return "";
}
// top-level instance fields: layer, follow, pedalDegree, accent, swing
inline std::string setField(json& scene, const std::string& id, const std::string& field, const json& v){
    json* in = findInstance(scene, id);
    if(!in) return "no instance '" + id + "'";
    if(field == "layer"){ if(!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 3) return "layer must be 1 to 4"; }
    else if(field == "follow"){ if(!v.is_string()) return "follow expects a name"; }
    else if(field != "pedalDegree" && field != "accent" && field != "swing" && field != "id") return "unknown field '" + field + "'";
    (*in)[field] = v;
    return "";
}

inline std::string addInstanceFromPreset(json& scene, const Preset& p, int layer, std::string* newId = nullptr){
    if(!findType(p.type)) return "unknown instrument type '" + p.type + "'";
    if(!scene.contains("instances") || !scene["instances"].is_array()) scene["instances"] = json::array();
    if(layer < 0){
        int counts[4] = {0, 0, 0, 0};
        for(auto& e : scene["instances"]) counts[std::clamp(e.value("layer", 0), 0, 3)]++;
        layer = 0; for(int l = 1; l < 4; l++) if(counts[l] < counts[layer]) layer = l;
    }
    InstanceOrigin o;
    Instance i = instanceFromPreset(p, uniqueId(scene, p.type), o);
    i.layer = std::clamp(layer, 0, 3);
    json e = toJson(i); e["origin"] = toJson(o);
    scene["instances"].push_back(e);
    if(newId) *newId = i.id;
    return "";
}
inline std::string addKit(json& scene, const Kit& k, std::vector<std::string>* added = nullptr){
    SceneDoc d = sceneDocFromJson(scene);
    auto ids = dropKit(d, k);
    if(!scene.contains("instances") || !scene["instances"].is_array()) scene["instances"] = json::array();
    for(auto& id : ids) for(auto& in : d.scene.instances) if(in.id == id){
        json e = toJson(in);
        auto o = d.origins.find(id); if(o != d.origins.end()) e["origin"] = toJson(o->second);
        scene["instances"].push_back(e);
    }
    if(added) added->insert(added->end(), ids.begin(), ids.end());
    return ids.empty() ? "the kit has no instruments" : "";
}
inline std::string removeInstance(json& scene, const std::string& id){
    if(!scene.contains("instances") || !scene["instances"].is_array()) return "no instances";
    auto& a = scene["instances"];
    for(size_t i = 0; i < a.size(); i++) if(a[i].value("id", std::string()) == id){ a.erase(a.begin() + (long)i); return ""; }
    return "no instance '" + id + "'";
}
// move an instance to another one's place in the scene's list (the drawing order of its layer, which
// "earlier"-scoped effects follow); both must be on the same layer
inline std::string moveInstance(json& scene, const std::string& id, const std::string& toId){
    if(!scene.contains("instances") || !scene["instances"].is_array()) return "no instances";
    auto& a = scene["instances"];
    long from = -1, to = -1;
    for(size_t i = 0; i < a.size(); i++){ std::string x = a[i].value("id", std::string()); if(x == id) from = (long)i; if(x == toId) to = (long)i; }
    if(from < 0) return "no instance '" + id + "'";
    if(to < 0) return "no instance '" + toId + "'";
    if(a[(size_t)from].value("layer", 0) != a[(size_t)to].value("layer", 0)) return "instances on different layers";
    if(from == to) return "";
    json moved = a[(size_t)from];
    a.erase(a.begin() + from);
    a.insert(a.begin() + to, moved);
    return "";
}
inline std::string duplicateInstance(json& scene, const std::string& id, std::string* newId = nullptr){
    const json* in = findInstance(scene, id);
    if(!in) return "no instance '" + id + "'";
    json c = *in; c["id"] = uniqueId(scene, id);
    if(newId) *newId = c["id"].get<std::string>();
    scene["instances"].push_back(c);
    return "";
}

// ---------------------------------------------------------------- routings (the matrix)
// Where a routing lives: an instance (owner = id) or the scene (owner = "")
struct RoutingRef { std::string owner; size_t index = 0; };

inline json* routingsOf(json& scene, const std::string& owner){
    json* holder = owner.empty() ? &scene : findInstance(scene, owner);
    if(!holder) return nullptr;
    if(!holder->contains("routings") || !(*holder)["routings"].is_array()) (*holder)["routings"] = json::array();
    return &(*holder)["routings"];
}
// the ParamDef of a routing destination inside an owner
inline const ParamDef* destDef(const json& scene, const std::string& owner, const std::string& dest){
    std::string type;
    if(!owner.empty()){ if(auto in = findInstance(scene, owner)) type = in->value("type", std::string()); }
    if(startsWith(dest, "seq.")) return sequencerParam(dest.substr(4));
    return findParam(type, dest);
}
// checks a routing for its owner: level allowed, render allowed; "" when fine
inline std::string checkRouting(const json& scene, const std::string& owner, const Routing& r){
    std::vector<std::string> out;
    std::string type;
    if(!owner.empty()){ if(auto in = findInstance(scene, owner)) type = in->value("type", std::string()); else return "no instance '" + owner + "'"; }
    if(startsWith(r.dest, "seq.")){
        const ParamDef* d = sequencerParam(r.dest.substr(4));
        if(!d) return "unknown destination '" + r.dest + "'";
        if(!(d->levels & r.level)) return std::string("level ") + levelName(r.level) + " is not allowed for " + d->label;
        if(routingRender(r) == "animated") return d->label + " cannot be animated";
        return "";
    }
    validateRouting(r, findType(type), nullptr, owner.empty() ? "scene" : owner, out);
    return out.empty() ? "" : out.front();
}
inline std::string addRouting(json& scene, const std::string& owner, const Routing& r0, size_t* index = nullptr){
    Routing r = r0;
    if(const ParamDef* d = destDef(scene, owner, r.dest)) if(!(d->levels & r.level)) r.level = d->defaultLevel;
    std::string e = checkRouting(scene, owner, r);
    if(!e.empty()) return e;
    json* a = routingsOf(scene, owner);
    if(!a) return "no instance '" + owner + "'";
    a->push_back(toJson(r));
    if(index) *index = a->size() - 1;
    return "";
}
inline bool getRouting(const json& scene, const RoutingRef& ref, Routing& out){
    json& s = const_cast<json&>(scene);
    json* holder = ref.owner.empty() ? &s : findInstance(s, ref.owner);
    if(!holder || !holder->contains("routings") || !(*holder)["routings"].is_array() || ref.index >= (*holder)["routings"].size()) return false;
    out = routingFromJson((*holder)["routings"][ref.index]);
    return true;
}
inline std::string setRouting(json& scene, const RoutingRef& ref, const Routing& r){
    std::string e = checkRouting(scene, ref.owner, r);
    if(!e.empty()) return e;
    json* a = routingsOf(scene, ref.owner);
    if(!a || ref.index >= a->size()) return "no such routing";
    (*a)[ref.index] = toJson(r);
    return "";
}
inline std::string removeRouting(json& scene, const RoutingRef& ref){
    json* a = routingsOf(scene, ref.owner);
    if(!a || ref.index >= a->size()) return "no such routing";
    a->erase(a->begin() + (long)ref.index);
    return "";
}
// the levels a destination accepts (for the options popover: others are struck through)
inline unsigned allowedLevels(const json& scene, const std::string& owner, const std::string& dest){
    const ParamDef* d = destDef(scene, owner, dest);
    return d ? d->levels : 0u;
}

// ---------------------------------------------------------------- matrix view
// Sources are rows (same source + params = one row), destinations are columns grouped by owner.
struct MatrixColumn { std::string owner, dest, label, group; const ParamDef* def = nullptr; };
struct MatrixCell { bool used = false; RoutingRef ref; Routing r; bool valid = true; };
struct MatrixView {
    std::vector<std::string> rowKeys, rowLabels;   // key: source + params dump
    std::vector<Routing> rowProto;                 // a routing of each row (its source and params)
    std::vector<MatrixColumn> cols;
    std::vector<std::vector<MatrixCell>> cells;    // [row][col]
    int routingCount = 0;
    int colOf(const std::string& owner, const std::string& dest) const {
        for(size_t i = 0; i < cols.size(); i++) if(cols[i].owner == owner && cols[i].dest == dest) return (int)i;
        return -1;
    }
};

inline std::string sourceKey(const Routing& r){
    json sp = r.sourceParams.is_object() ? r.sourceParams : json::object();
    sp.erase("_render"); sp.erase("_level");
    return r.source + "|" + sp.dump();
}
// plain-language name of a source
inline std::string sourceLabel(const Routing& r){
    const json& p = r.sourceParams;
    auto ps = [&](const char* k, const std::string& d){ return p.is_object() && p.contains(k) && p[k].is_string() ? p[k].get<std::string>() : d; };
    auto pn = [&](const char* k, double d){ return p.is_object() && p.contains(k) && p[k].is_number() ? p[k].get<double>() : d; };
    auto num = [](double v){ char b[32]; std::snprintf(b, sizeof b, "%g", v); return std::string(b); };
    std::string s = r.source;
    std::string t = ps("type", "");
    std::string k = t.empty() ? s : t;
    if(startsWith(k, "lfo")) return "LFO " + (s.size() > 3 && std::isdigit((unsigned char)s[3]) ? s.substr(3) + " " : std::string()) + "· " + ps("shape", "sine") + " · " + num(pn("rateBeats", 4)) + " beats";
    if(startsWith(k, "random")) return "Random" + (s.size() > 6 ? " " + s.substr(6) : std::string()) + " · " + ps("scope", "phrase");
    if(startsWith(k, "phraseEnv")) return "Phrase envelope · " + ps("shape", "rise");
    if(startsWith(k, "noteEnv")) return "Note envelope · " + ps("shape", "rise");
    if(startsWith(s, "macro:")) return "Macro · " + s.substr(6);
    if(startsWith(k, "macro")) return "Macro · " + ps("name", s);
    if(startsWith(s, "follow:")) return "Follow · " + s.substr(7);
    if(startsWith(s, "lane:")) return "Lane · " + s.substr(5);
    if(startsWith(k, "playhead")) return "Playhead · " + ps("shape", "peak");
    if(startsWith(k, "brightness")) return "Screen brightness";
    if(startsWith(k, "arc")) return "Arc";
    return s;
}
inline std::string destLabel(const std::string& dest, const ParamDef* d){
    if(d && !d->label.empty()) return d->label;
    return dest;
}

// filter: "" all owners, else only routings / columns of this instance (scene columns stay)
inline MatrixView buildMatrix(const json& scene, const std::string& filterOwner = "", bool allDestinations = false){
    MatrixView v;
    auto addRow = [&](const Routing& r)->int{
        std::string k = sourceKey(r);
        for(size_t i = 0; i < v.rowKeys.size(); i++) if(v.rowKeys[i] == k) return (int)i;
        v.rowKeys.push_back(k); v.rowLabels.push_back(sourceLabel(r)); v.rowProto.push_back(r);
        return (int)v.rowKeys.size() - 1;
    };
    auto addCol = [&](const std::string& owner, const std::string& dest, const std::string& group)->int{
        int c = v.colOf(owner, dest);
        if(c >= 0) return c;
        MatrixColumn m; m.owner = owner; m.dest = dest; m.group = group;
        m.def = destDef(scene, owner, dest);
        m.label = destLabel(dest, m.def);
        v.cols.push_back(m);
        return (int)v.cols.size() - 1;
    };
    struct Item { RoutingRef ref; Routing r; std::string group; };
    std::vector<Item> items;
    if(scene.contains("instances") && scene["instances"].is_array())
        for(auto& e : scene["instances"]){
            std::string id = e.value("id", std::string());
            if(!filterOwner.empty() && id != filterOwner) continue;
            if(e.contains("routings") && e["routings"].is_array())
                for(size_t i = 0; i < e["routings"].size(); i++) items.push_back({{id, i}, routingFromJson(e["routings"][i]), id});
            if(allDestinations)
                for(auto& kv : destinations(e.value("type", std::string()))){
                    if(startsWith(kv.first, "scene.")) continue;
                    if(!kv.second->main && !startsWith(kv.first, "layer.") && kv.first != "depth" && kv.first != "size" && kv.first != "brightness") continue;
                    addCol(id, kv.first, id);
                }
        }
    if(scene.contains("routings") && scene["routings"].is_array())
        for(size_t i = 0; i < scene["routings"].size(); i++) items.push_back({{"", i}, routingFromJson(scene["routings"][i]), "Scene"});
    for(auto& it : items){ addRow(it.r); addCol(it.ref.owner, it.r.dest, it.group); }
    // group columns by owner, keeping first-seen order of owners
    std::vector<std::string> owners;
    for(auto& c : v.cols) if(std::find(owners.begin(), owners.end(), c.owner) == owners.end()) owners.push_back(c.owner);
    std::stable_sort(v.cols.begin(), v.cols.end(), [&](const MatrixColumn& a, const MatrixColumn& b){
        auto ia = std::find(owners.begin(), owners.end(), a.owner) - owners.begin();
        auto ib = std::find(owners.begin(), owners.end(), b.owner) - owners.begin();
        if(a.owner.empty() != b.owner.empty()) return b.owner.empty();
        return ia < ib;
    });
    v.cells.assign(v.rowKeys.size(), std::vector<MatrixCell>(v.cols.size()));
    for(auto& it : items){
        int r = addRow(it.r), c = v.colOf(it.ref.owner, it.r.dest);
        MatrixCell& cell = v.cells[(size_t)r][(size_t)c];
        cell.used = true; cell.ref = it.ref; cell.r = it.r;
        cell.valid = checkRouting(scene, it.ref.owner, it.r).empty();
        v.routingCount++;
    }
    return v;
}
// adds a routing from a row's source to a column
inline std::string connectCell(json& scene, const MatrixView& v, int row, int col, double amount = 0.5){
    if(row < 0 || col < 0 || row >= (int)v.rowProto.size() || col >= (int)v.cols.size()) return "no such cell";
    Routing r = v.rowProto[(size_t)row];
    r.dest = v.cols[(size_t)col].dest; r.amount = amount; r.hasRange = false; r.invert = false; r.curve = 1;
    if(r.sourceParams.is_object()) r.sourceParams.erase("_render");
    if(const ParamDef* d = v.cols[(size_t)col].def) r.level = d->defaultLevel;
    return addRouting(scene, v.cols[(size_t)col].owner, r);
}
// a fresh source routing (for "+ Source"): kind lfo | random | phraseEnv | noteEnv | macro
inline Routing newSource(const std::string& kind, int n = 1){
    Routing r; r.amount = 0.5;
    if(kind == "lfo"){ r.source = "lfo" + std::to_string(n); r.sourceParams = {{"type", "lfo"}, {"shape", "sine"}, {"rateBeats", 4}}; r.level = LevelLayer; }
    else if(kind == "random"){ r.source = "random" + std::to_string(n); r.sourceParams = {{"type", "random"}, {"scope", "event"}}; r.level = LevelEvent; }
    else if(kind == "phraseEnv"){ r.source = "phraseEnv"; r.sourceParams = {{"shape", "swell"}}; r.level = LevelPhrase; }
    else if(kind == "noteEnv"){ r.source = "noteEnv"; r.sourceParams = {{"shape", "rise"}}; r.level = LevelEvent; }
    else { r.source = "macro" + std::to_string(n); r.sourceParams = {{"type", "macro"}, {"name", "macro" + std::to_string(n)}}; r.level = LevelLayer; }
    return r;
}

// ---------------------------------------------------------------- plain-language labels
inline std::string fmtNum(double v, int prec = 2){
    char b[32]; std::snprintf(b, sizeof b, "%.*f", prec, v);
    std::string s = b;
    if(s.find('.') != std::string::npos){ while(!s.empty() && s.back() == '0') s.pop_back(); if(!s.empty() && s.back() == '.') s.pop_back(); }
    return s;
}
inline std::string followSentence(const std::string& follow){
    if(follow == "chord") return "Plays the tones of the current chord. Changes when the chord changes.";
    if(follow == "scale") return "Uses the scale of the section; ignores the chord.";
    if(follow == "pedal") return "Stays on one degree (the pedal) whatever the chord.";
    if(follow == "off") return "Ignores the harmony: plays its own pitches.";
    return "Follows the root of the current chord.";
}
inline std::string levelWords(Level l){
    switch(l){ case LevelEvent: return "Each event"; case LevelLayer: return "Continuous"; case LevelScene: return "Scene"; default: return "Each phrase"; }
}
inline std::string phraseLengthText(const json& inst){
    json L = inst.contains("phrase") && inst["phrase"].is_object() && inst["phrase"].contains("length") ? inst["phrase"]["length"] : json(16);
    if(L.is_array() && L.size() == 2) return "random " + fmtNum(L[0].get<double>()) + "–" + fmtNum(L[1].get<double>()) + " beats";
    if(L.is_number()) return fmtNum(L.get<double>()) + " beats";
    return "16 beats";
}
inline std::string variationText(const json& inst){
    std::string r = valueOf(inst, "seq", "varRhythm").is_string() ? valueOf(inst, "seq", "varRhythm").get<std::string>() : "new";
    std::string p = valueOf(inst, "seq", "varPitch").is_string() ? valueOf(inst, "seq", "varPitch").get<std::string>() : "new";
    double m = numberOf(inst, "seq", "mutateAmount", 0.25);
    auto w = [&](const std::string& s){ return s == "mutate" ? "mutate " + fmtNum(m * 100, 0) + "%" : s; };
    if(r == p) return w(r);
    return "rhythm " + w(r) + " · pitch " + w(p);
}
inline std::string valueText(const ParamDef* d, const json& v){
    if(v.is_boolean()) return v.get<bool>() ? "on" : "off";
    if(v.is_string()) return v.get<std::string>();
    if(v.is_array() && v.size() == 2 && v[0].is_number()) return fmtNum(v[0].get<double>()) + "–" + fmtNum(v[1].get<double>());
    if(v.is_number()){
        std::string s = fmtNum(v.get<double>(), d && d->type == "int" ? 0 : 2);
        if(d && !d->unit.empty() && d->unit != "0-1" && d->unit != "x") s += " " + d->unit;
        return s;
    }
    return v.dump();
}

// readable diff between two scene JSONs (assistant answers, undo labels)
inline std::vector<std::string> describeChanges(const json& a, const json& b){
    std::vector<std::string> out;
    auto ida = instanceIds(a), idb = instanceIds(b);
    for(auto& id : idb) if(std::find(ida.begin(), ida.end(), id) == ida.end()){
        const json* in = findInstance(b, id);
        out.push_back("+ new instrument " + id + " (" + (in ? in->value("type", std::string()) : std::string()) + ")");
    }
    for(auto& id : ida) if(std::find(idb.begin(), idb.end(), id) == idb.end()) out.push_back("- removed " + id);
    for(auto& id : ida){
        const json* x = findInstance(a, id); const json* y = findInstance(b, id);
        if(!x || !y) continue;
        std::string type = y->value("type", std::string());
        for(const char* sec : {"params", "seq", "phrase"}){
            json ex = x->value(sec, json::object()), ey = y->value(sec, json::object());
            std::string section = std::string(sec) == "params" ? "look" : sec;
            std::set<std::string> keys;
            for(auto it = ex.begin(); it != ex.end(); ++it) keys.insert(it.key());
            for(auto it = ey.begin(); it != ey.end(); ++it) keys.insert(it.key());
            for(auto& k : keys){
                json vx = ex.contains(k) ? ex[k] : json(), vy = ey.contains(k) ? ey[k] : json();
                if(vx == vy) continue;
                const ParamDef* d = paramOf(type, section, k);
                std::string name = d ? d->label : k;
                out.push_back(id + ": " + name + " " + (vx.is_null() ? "(default)" : valueText(d, vx)) + " → " + (vy.is_null() ? "(default)" : valueText(d, vy)));
            }
        }
        for(const char* f : {"layer", "follow", "accent", "swing", "pedalDegree"})
            if(x->value(f, json()) != y->value(f, json())) out.push_back(id + ": " + f + " " + x->value(f, json()).dump() + " → " + y->value(f, json()).dump());
        auto ra = routingsFromJson(x->value("routings", json::array())), rb = routingsFromJson(y->value("routings", json::array()));
        auto line = [&](const Routing& r){
            const ParamDef* d = destDef(b, id, r.dest);
            std::string s = sourceLabel(r) + " → " + destLabel(r.dest, d);
            if(r.hasRange) s += " " + fmtNum(r.min) + "–" + fmtNum(r.max);
            else s += " amount " + fmtNum(r.amount);
            return s + " (" + levelWords(r.level) + ")";
        };
        std::vector<std::string> la, lb;
        for(auto& r : ra) la.push_back(line(r));
        for(auto& r : rb) lb.push_back(line(r));
        for(auto& s : lb) if(std::find(la.begin(), la.end(), s) == la.end()) out.push_back(id + ": + " + s);
        for(auto& s : la) if(std::find(lb.begin(), lb.end(), s) == lb.end()) out.push_back(id + ": - " + s);
    }
    if(a.value("bpm", 0.0) != b.value("bpm", 0.0)) out.push_back("tempo " + fmtNum(a.value("bpm", 0.0)) + " → " + fmtNum(b.value("bpm", 0.0)));
    if(a.value("routings", json::array()) != b.value("routings", json::array())) out.push_back("scene routings changed");
    if(a.value("harmony", json()) != b.value("harmony", json())) out.push_back("harmony changed");
    return out;
}

// ---------------------------------------------------------------- step grid model
// A preview of the rhythm of the seq settings: per step 0 rest, 1 hit, 2 accent, 3 maybe.
inline std::vector<int> stepPreview(const json& inst, int maxSteps = 32){
    std::string mode = valueOf(inst, "seq", "mode").is_string() ? valueOf(inst, "seq", "mode").get<std::string>() : "euclidean";
    int steps = (int)numberOf(inst, "seq", "steps", 0);
    if(steps <= 0) steps = 16;
    steps = std::clamp(steps, 1, maxSteps);
    std::vector<int> v((size_t)steps, 0);
    if(mode == "pattern"){
        std::string p = valueOf(inst, "seq", "pattern").is_string() ? valueOf(inst, "seq", "pattern").get<std::string>() : "x...";
        size_t bar = p.find('|'); if(bar != std::string::npos) p = p.substr(0, bar);
        if(p.empty()) return v;
        for(int i = 0; i < steps; i++){
            char c = p[(size_t)i % p.size()];
            v[(size_t)i] = c == 'X' ? 2 : c == 'x' ? 1 : c == '?' ? 3 : 0;
        }
    } else if(mode == "euclidean"){
        int hits = std::clamp((int)numberOf(inst, "seq", "hits", 3), 0, steps);
        int rot = (int)numberOf(inst, "seq", "rotation", 0);
        for(int i = 0; i < steps; i++){
            int j = ((i - rot) % steps + steps) % steps;
            if(hits > 0 && (j * hits) % steps < hits) v[(size_t)i] = 1;
        }
    } else if(mode == "probability"){
        json ch = valueOf(inst, "seq", "stepChances");
        double d = numberOf(inst, "seq", "density", 0.5);
        for(int i = 0; i < steps; i++){
            double c = ch.is_array() && !ch.empty() ? ch[(size_t)i % ch.size()].get<double>() / 100.0 : d;
            v[(size_t)i] = c >= 0.99 ? 1 : c > 0.01 ? 3 : 0;
        }
    } else {
        for(int i = 0; i < steps; i += 4) v[(size_t)i] = 3;
    }
    return v;
}
// toggles a step in the typed pattern (switches the rhythm to "pattern" mode)
inline std::string toggleStep(json& scene, const std::string& id, int step){
    json* in = findInstance(scene, id);
    if(!in) return "no instance '" + id + "'";
    auto v = stepPreview(*in, 64);
    if(step < 0 || step >= (int)v.size()) return "no such step";
    std::string p;
    for(size_t i = 0; i < v.size(); i++){
        int s = v[i];
        if((int)i == step) s = s == 0 ? 1 : s == 1 ? 2 : 0;
        p += s == 2 ? 'X' : s == 1 ? 'x' : s == 3 ? '?' : '.';
    }
    std::string e = setParam(scene, id, "seq", "mode", "pattern");
    if(e.empty()) e = setParam(scene, id, "seq", "pattern", p);
    if(e.empty()) e = setParam(scene, id, "seq", "steps", (int)v.size());
    return e;
}

// ---------------------------------------------------------------- instrument preset library
struct LibraryItem {
    std::string path, name, type, family, source;   // source: subfolder ("mine", "factory", "assistant", "")
    std::vector<std::string> tags;
    std::string notes;
    bool kit = false;
    int count = 1;                                   // instruments (kits)
    int version = 1;
    json data;
};

struct Library {
    std::string dir;
    std::vector<LibraryItem> items;
    std::vector<std::string> errors;

    static std::string defaultDir(){
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p = fs::path(__FILE__).parent_path() / ".." / ".." / "tests" / "v2" / "presets_v2";
        fs::path c = fs::weakly_canonical(p, ec);
        return (ec ? p : c).string();
    }
    // only folders named presets_v2 (or inside one) are written to
    static bool writable(const std::string& d){ return d.find("presets_v2") != std::string::npos; }

    bool ensureDir(){
        namespace fs = std::filesystem;
        if(!writable(dir)) return false;
        std::error_code ec;
        fs::create_directories(dir, ec);
        return !ec;
    }
    void scan(){
        namespace fs = std::filesystem;
        items.clear(); errors.clear();
        std::error_code ec;
        if(dir.empty() || !fs::exists(dir, ec)) return;
        for(auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)){
            if(!it->is_regular_file() || it->path().extension() != ".json") continue;
            std::ifstream f(it->path().string());
            json j;
            try { f >> j; } catch(...){ errors.push_back(it->path().filename().string() + ": not JSON"); continue; }
            LibraryItem li;
            li.path = it->path().string();
            fs::path rel = fs::relative(it->path().parent_path(), dir, ec);
            li.source = rel.empty() || rel == "." ? "" : rel.begin()->string();
            std::string fmt = j.value("format", std::string());
            li.name = j.value("name", it->path().stem().string());
            li.tags = j.value("tags", std::vector<std::string>{});
            li.notes = j.value("notes", std::string());
            li.version = j.value("version", 1);
            if(fmt == "espectres.kit"){
                li.kit = true; li.family = "kit"; li.type = "kit";
                li.count = j.contains("instances") && j["instances"].is_array() ? (int)j["instances"].size() : 0;
            } else if(fmt == "espectres.instrument" || j.contains("type")){
                li.type = j.value("type", std::string());
                li.family = familyOf(li.type);
                if(li.family.empty()){ errors.push_back(li.name + ": unknown type '" + li.type + "'"); continue; }
            } else { errors.push_back(it->path().filename().string() + ": not a preset or kit"); continue; }
            li.data = j;
            items.push_back(li);
        }
        std::sort(items.begin(), items.end(), [](const LibraryItem& a, const LibraryItem& b){
            if(a.kit != b.kit) return !a.kit;
            if(a.family != b.family) return a.family < b.family;
            return a.name < b.name;
        });
    }
    // writes a preset as a new file (never over an existing one); returns the path or ""
    std::string savePreset(const Preset& p, const std::string& sub = "mine"){
        namespace fs = std::filesystem;
        if(!ensureDir()) return "";
        fs::path d = sub.empty() ? fs::path(dir) : fs::path(dir) / sub;
        std::error_code ec; fs::create_directories(d, ec);
        std::string base = p.name.empty() ? p.type : p.name;
        for(auto& c : base) if(c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
        fs::path f = d / (base + ".json");
        int n = 2;
        while(fs::exists(f)) f = d / (base + "_" + std::to_string(n++) + ".json");
        std::ofstream o(f.string(), std::ios::binary);
        if(!o) return "";
        o << toJson(p).dump(2);
        return f.string();
    }
    // seeds the library from scenes: one preset per instance, named <scene>_<id> (skipped if present)
    int seedFromScene(const json& scene, const std::string& sceneName, const std::string& sub = "factory"){
        namespace fs = std::filesystem;
        if(!ensureDir()) return 0;
        fs::path d = fs::path(dir) / sub;
        std::error_code ec; fs::create_directories(d, ec);
        int n = 0;
        SceneDoc doc = sceneDocFromJson(scene);
        for(auto& in : doc.scene.instances){
            std::string name = sceneName + "_" + in.id;
            for(auto& c : name) if(c == '/' || c == '\\' || c == ':' || c == ' ') c = '_';
            fs::path f = d / (name + ".json");
            if(fs::exists(f)) continue;
            auto o = doc.origins.find(in.id);
            Preset p = presetFromInstance(in, o == doc.origins.end() ? nullptr : &o->second, name);
            p.notes = "From the scene \"" + sceneName + "\" (" + in.id + ").";
            p.tags = {sceneName};
            if(!validate(p).empty()) continue;
            std::ofstream out(f.string(), std::ios::binary);
            if(!out) continue;
            out << toJson(p).dump(2);
            n++;
        }
        return n;
    }
    int seedFromFiles(const std::vector<std::string>& files, const std::string& sub = "factory"){
        int n = 0;
        for(auto& path : files){
            std::ifstream f(path);
            json j;
            try { f >> j; } catch(...){ continue; }
            if(!j.contains("instances")) continue;
            std::string nm = j.value("name", std::filesystem::path(path).stem().string());
            n += seedFromScene(j, nm, sub);
        }
        return n;
    }
};

// what the collection shows
struct LibraryFilter {
    std::string text;
    std::set<std::string> families;     // empty = all; "kit" for kits
    std::set<std::string> tags;
    std::string source;                 // "" all
    bool match(const LibraryItem& i) const {
        if(!families.empty() && !families.count(i.family)) return false;
        if(!source.empty() && i.source != source) return false;
        for(auto& t : tags) if(std::find(i.tags.begin(), i.tags.end(), t) == i.tags.end()) return false;
        if(!text.empty()){
            auto low = [](std::string s){ for(auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            std::string q = low(text), hay = low(i.name + " " + i.type + " " + i.family + " " + i.notes);
            for(auto& t : i.tags) hay += " " + low(t);
            if(hay.find(q) == std::string::npos) return false;
        }
        return true;
    }
};
inline std::vector<std::string> allTags(const Library& l){
    std::set<std::string> s;
    for(auto& i : l.items) for(auto& t : i.tags) s.insert(t);
    return {s.begin(), s.end()};
}
inline std::map<std::string, int> familyCounts(const Library& l){
    std::map<std::string, int> m;
    for(auto& i : l.items) m[i.family]++;
    return m;
}
// adds a library item to the scene (preset -> one instance, kit -> its instances)
inline std::string dropItem(json& scene, const LibraryItem& it, int layer, std::vector<std::string>* added = nullptr){
    if(it.kit) return addKit(scene, kitFromJson(it.data), added);
    Preset p = presetFromJson(it.data);
    std::vector<std::string> pv = validate(p);
    if(!pv.empty()) return pv.front();
    std::string id;
    std::string e = addInstanceFromPreset(scene, p, layer, &id);
    if(e.empty() && added) added->push_back(id);
    return e;
}
// replaces an instance with a preset, keeping its id, layer and harmony settings
inline std::string replaceWith(json& scene, const std::string& id, const LibraryItem& it){
    if(it.kit) return "a kit cannot replace one instrument";
    json* in = findInstance(scene, id);
    if(!in) return "no instance '" + id + "'";
    Preset p = presetFromJson(it.data);
    InstanceOrigin o;
    Instance old = instanceFromJson(*in);
    Instance ni = instanceFromPreset(p, id, o);
    ni.layer = old.layer; ni.follow = old.follow; ni.pedalDegree = old.pedalDegree; ni.accent = old.accent; ni.swing = old.swing;
    json e = toJson(ni); e["origin"] = toJson(o);
    *in = e;
    return "";
}

// ---------------------------------------------------------------- an instance's main / advanced params
struct ParamRow { std::string section, key; const ParamDef* def = nullptr; };
inline std::vector<ParamRow> instanceParams(const std::string& type, bool advanced){
    std::vector<ParamRow> v;
    if(auto t = findType(type)) for(auto& p : t->params) if(p.main != advanced) v.push_back({"look", p.key, &p});
    return v;
}
inline std::vector<ParamRow> seqParams(const std::string& group){
    std::vector<ParamRow> v;
    for(auto& p : sequencerParams()) if(p.group == group) v.push_back({"seq", p.key, &p});
    return v;
}
// up to n performance knobs: the preset's knobs if any (by origin), else the first n main numeric params
inline std::vector<ParamRow> performanceKnobs(const std::string& type, int n = 6){
    std::vector<ParamRow> v;
    if(auto t = findType(type)) for(auto& p : t->params){
        if(!p.main || (p.type != "float" && p.type != "int")) continue;
        v.push_back({"look", p.key, &p});
        if((int)v.size() >= n) break;
    }
    return v;
}
// how many keys differ from the preset the instance came from (origin overrides)
inline int changesFromPreset(const json& inst){
    if(!inst.contains("origin") || !inst["origin"].is_object()) return 0;
    return (int)inst["origin"].value("overrides", std::vector<std::string>{}).size();
}

} // namespace gui
} // namespace forms2
