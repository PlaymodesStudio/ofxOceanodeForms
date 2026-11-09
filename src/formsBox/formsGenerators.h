//
//  formsGenerators.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Parameterised generators (Phase 4): their schema, read from the renderer's generators.json,
//  and instrument presets (presets/<gen>/<name>.json in the renderer folder). A preset gives each
//  parameter a fixed value or a range / a set of options; resolve() turns it into concrete values
//  for one image, which travel in the render request:  { "gen", "preset", "params" }.
//  Plain C++ and nlohmann::json only, so it can be tested on its own.
//
//  Parameter types (see Generators.pde in the renderer):
//    int, float   one value per image. Preset: a number, {"min", "max"} or {"options": [numbers]}
//                 (picked per image).
//    enum         one option per image. Preset: a string, or {"options": [...]} (picked per image).
//    bool         Preset: true / false, or {"either": true} (picked per image).
//    intRange, floatRange   [lo, hi], passed as is: the generator draws per element in it.
//    intList      a list of ints, passed as is, or {"options": [[...], [...]]} (one list picked per image).
//

#pragma once

#include "formsCore.h"

namespace forms {

struct ParamDef {
    std::string key, type, label, group;
    float min = 0, max = 1;
    json def;                          // schema default (number, string, bool or array)
    std::vector<std::string> options;  // enum
    bool isNumber() const { return type == "int" || type == "float"; }
    bool isRange() const { return type == "intRange" || type == "floatRange"; }
};

struct GenDef {
    std::string name, family, label, description;
    std::vector<ParamDef> params;
    // The process slot is defined once, by the "effects" generator. Forms Box appends its
    // parameters (all but "steps") to every other generator, so a preset edits and resolves them
    // with the generator's own; they are kept apart in preset files and requests ("process").
    std::vector<std::string> processKeys;
    bool isProcessKey(const std::string& k) const { return std::find(processKeys.begin(), processKeys.end(), k) != processKeys.end(); }
    const ParamDef* param(const std::string& k) const {
        for(auto& p : params) if(p.key == k) return &p;
        return nullptr;
    }
};

inline GenDef genDefFromJson(const json& g){
    GenDef d;
    d.name = g.value("name", std::string());
    d.family = g.value("family", std::string());
    d.label = g.value("label", d.name);
    d.description = g.value("description", std::string());
    const json ps = g.value("params", json::array());
    for(auto& p : ps){
        ParamDef pd;
        pd.key = p.value("key", std::string());
        pd.type = p.value("type", std::string("float"));
        pd.label = p.value("label", pd.key);
        pd.group = p.value("group", std::string());
        pd.min = p.value("min", 0.0f);
        pd.max = p.value("max", 1.0f);
        pd.def = p.contains("default") ? p["default"] : json();
        if(p.contains("options")) pd.options = p["options"].get<std::vector<std::string>>();
        // Forms GL's kick sweep now respects the shared Hits intensity control.
        // Upgrade older on-disk schemas too, so their editor cannot clamp boosts to 1.
        if(d.name=="hits" && pd.key=="level"){
            pd.max=4;pd.label="Volume (above 1 saturates)";
        }
        if(d.name=="effects" && pd.key=="echoDelay")pd.label="Echo delay (grid steps; dotted allowed)";
        d.params.push_back(pd);
    }
    return d;
}

struct Preset {
    std::string gen, name, notes;
    std::string legacy;                // the legacy method this preset ports (for the A/B), if any
    // Musical role, the Processing families extended: bass, melody, chords, pads, arps (Harmony),
    // rhythm, texture, geometry, effects. Empty: derived from the legacy method / generator (presetCategory)
    std::string category;
    // A layered preset (gen "stack"): several generator items drawn in order, each {gen, params}.
    // A layer may have "chance" (% per image) and "group" (layers of one group share one roll);
    // with "pick": "one" a single layer is drawn per image (weighted by "weight", default 1); with
    // "pick": "group" a single group is drawn (layers without a group are a group of their own;
    // a group's weight is its first layer's). A layer may be {"preset": "<gen>/<name>"} instead
    // of {gen, params}: that preset's items, resolved for this image (a stack inside a stack).
    json layers = json::array();
    std::string pick = "all";
    // Linked picks (any preset, and stack layers): "linked": [{...}, {...}] is a list of parameter
    // sets, one chosen per image (weighted by an optional "weight" key) and laid over params before
    // they resolve, so several values change together. A list of such lists makes several
    // independent choices.
    json linked = json::array();
    // Per-bar choices: "perBar": n resolves the preset again for every n bars (ranges, options and
    // linked picks drawn anew), each result drawn only in its bars ("clipBars" in the request).
    // 0: once per image. On a stack it applies to every layer (and the stack's shared pick).
    int perBar = 0;
    bool isStack() const { return gen == "stack"; }
    json params = json::object();      // per key: value, {"min","max"}, {"options"}, {"either"}
    std::string ref() const { return gen + "/" + name; }   // how scenes refer to it (Cell::item)
};
inline void to_json(json& j, const Preset& p){
    j = json{{"gen", p.gen}, {"name", p.name}, {"params", p.params}};
    if(!p.notes.empty()) j["notes"] = p.notes;
    if(!p.legacy.empty()) j["legacy"] = p.legacy;
    if(!p.category.empty()) j["category"] = p.category;
    if(p.isStack()){ j["layers"] = p.layers; if(p.pick != "all") j["pick"] = p.pick; }
    if(!p.linked.empty()) j["linked"] = p.linked;
    if(p.perBar > 0) j["perBar"] = p.perBar;
}
inline void from_json(const json& j, Preset& p){
    p.gen = j.value("gen", std::string());
    p.name = j.value("name", std::string());
    p.notes = j.value("notes", std::string());
    p.legacy = j.value("legacy", std::string());
    p.category = j.value("category", std::string());
    p.layers = j.value("layers", json::array());
    p.pick = j.value("pick", std::string("all"));
    p.params = j.value("params", json::object());
    p.linked = j.value("linked", json::array());
    p.perBar = std::max(0, j.value("perBar", 0));
}

// The categories (the Processing families harmony / rhythm / texture / effects, harmony split into
// bass, melody, chords, pads, arps; the geometric textures apart), their family letter and label
inline const std::vector<std::string>& presetCategories(){
    static const std::vector<std::string> c{"bass", "melody", "chords", "pads", "arps", "harmony", "rhythm", "texture", "geometry", "effects"};
    return c;
}
// the family a category belongs to, as the legacy method prefix: h, r, t, g (geometry), fx
inline std::string categoryFamily(const std::string& cat){
    if(cat == "rhythm") return "r";
    if(cat == "texture") return "t";
    if(cat == "geometry") return "g";
    if(cat == "effects") return "fx";
    return "h";
}
inline std::string categoryLabel(const std::string& cat){
    if(cat.empty()) return "";
    if(cat == "effects") return "FX";
    std::string s = cat; s[0] = (char)toupper(s[0]); return s;
}
// A preset's category: its tag, else derived from the legacy method it ports and its generator
inline std::string presetCategory(const Preset& p){
    if(!p.category.empty()) return p.category;
    if(p.gen == "effects") return "effects";
    if(p.gen == "shapes") return "geometry";
    std::string pre;
    for(char c : p.legacy){ if(isalpha((unsigned char)c)) pre += c; else break; }
    if(pre == "fx") return "effects";
    if(pre == "r") return "rhythm";
    if(pre == "t") return "texture";
    if(p.gen == "hits" || p.gen == "sweep") return pre == "h" ? "harmony" : "rhythm";
    if(p.params.is_object() && p.params.value("bass", false)) return "bass";
    return "harmony";
}

// A preset item reference ("<gen>/<preset>"); legacy method names never contain '/'
inline bool isPresetRef(const std::string& item){ return item.find('/') != std::string::npos; }

inline float genClamp(float v, float lo, float hi){ return std::max(lo, std::min(hi, v)); }

// One image's values for a preset. Keys the schema doesn't know are dropped; missing keys get the
// schema default. Randomness only from rng.
// One choice of linked parameter sets: a set picked (weighted) and laid over params
inline void applyLinked(json& params, const json& sets, Rng& rng){
    if(!sets.is_array() || sets.empty()) return;
    if(sets[0].is_array()){ for(auto& s : sets) applyLinked(params, s, rng); return; }
    float total = 0;
    for(auto& s : sets) if(s.is_object()) total += std::max(0.0f, s.value("weight", 1.0f));
    if(total <= 0) return;
    float r = uniform(rng, total), acc = 0;
    const json* chosen = nullptr;
    for(auto& s : sets){
        if(!s.is_object()) continue;
        chosen = &s;
        acc += std::max(0.0f, s.value("weight", 1.0f));
        if(r < acc) break;
    }
    if(chosen) for(auto& [k, v] : chosen->items()) if(k != "weight") params[k] = v;
}

inline json resolve(const GenDef& g, const Preset& prIn, Rng& rng){
    json out = json::object();
    // linked picks first (only when present, so presets without them draw the same numbers)
    Preset linkedCopy;
    const Preset* prp = &prIn;
    if(!prIn.linked.empty()){
        linkedCopy = prIn;
        applyLinked(linkedCopy.params, prIn.linked, rng);
        prp = &linkedCopy;
    }
    const Preset& pr = *prp;
    for(auto& p : g.params){
        const json v = pr.params.contains(p.key) ? pr.params[p.key] : p.def;
        if(p.isNumber()){
            float x = p.def.is_number() ? p.def.get<float>() : p.min;
            if(v.is_number()) x = v.get<float>();
            else if(v.is_object() && v.contains("options") && v["options"].is_array() && !v["options"].empty()){
                std::vector<float> ok;
                for(auto& o : v["options"]) if(o.is_number()) ok.push_back(o.get<float>());
                if(!ok.empty()) x = ok[std::min<size_t>(ok.size() - 1, (size_t)uniform(rng, (float)ok.size()))];
            }
            else if(v.is_object() && v.contains("min") && v.contains("max")){
                float a = v["min"].get<float>(), b = v["max"].get<float>();
                float lo = std::min(a, b), hi = std::max(a, b);
                if(p.type == "int"){
                    int ilo = (int)std::lround(lo), ihi = (int)std::lround(hi);
                    x = (float)(ilo + (int)std::min<float>((float)(ihi - ilo), std::floor(uniform(rng, (float)(ihi - ilo + 1)))));
                }else x = lo + uniform(rng, hi - lo);
            }
            x = genClamp(x, p.min, p.max);
            if(p.type == "int") out[p.key] = (int)std::lround(x);
            else out[p.key] = x;
        }else if(p.type == "enum"){
            std::string s = p.def.is_string() ? p.def.get<std::string>() : (p.options.empty() ? "" : p.options[0]);
            if(v.is_string()) s = v.get<std::string>();
            else if(v.is_object() && v.contains("options")){
                std::vector<std::string> ok;
                for(auto& o : v["options"]) if(o.is_string() && std::find(p.options.begin(), p.options.end(), o.get<std::string>()) != p.options.end()) ok.push_back(o.get<std::string>());
                if(!ok.empty()) s = ok[std::min<size_t>(ok.size() - 1, (size_t)uniform(rng, (float)ok.size()))];
            }
            if(std::find(p.options.begin(), p.options.end(), s) == p.options.end() && !p.options.empty()) s = p.options[0];
            out[p.key] = s;
        }else if(p.type == "bool"){
            bool b = p.def.is_boolean() ? p.def.get<bool>() : false;
            if(v.is_boolean()) b = v.get<bool>();
            else if(v.is_object() && v.value("either", false)) b = uniform(rng, 1.0f) < 0.5f;
            out[p.key] = b;
        }else if(p.isRange()){
            json pick = v;
            if(v.is_object() && v.contains("options") && v["options"].is_array() && !v["options"].empty()){
                std::vector<json> ok;
                for(auto& o : v["options"]) if(o.is_array() && !o.empty()) ok.push_back(o);
                if(!ok.empty()) pick = ok[std::min<size_t>(ok.size() - 1, (size_t)uniform(rng, (float)ok.size()))];
            }
            json r = (pick.is_array() && !pick.empty()) ? pick : p.def;
            float a = r.is_array() && !r.empty() ? r[0].get<float>() : p.min;
            float b = r.is_array() && r.size() > 1 ? r[1].get<float>() : a;
            a = genClamp(a, p.min, p.max); b = genClamp(b, p.min, p.max);
            if(p.type == "intRange") out[p.key] = {(int)std::lround(std::min(a, b)), (int)std::lround(std::max(a, b))};
            else out[p.key] = {std::min(a, b), std::max(a, b)};
        }else if(p.type == "intList"){
            json l = v.is_array() ? v : (p.def.is_array() ? p.def : json::array());
            if(v.is_object() && v.contains("options") && v["options"].is_array() && !v["options"].empty()){
                std::vector<json> ok;   // {"options": [[...], [...]]}: one list per image
                for(auto& c : v["options"]) if(c.is_array()) ok.push_back(c);
                if(!ok.empty()) l = ok[std::min<size_t>(ok.size() - 1, (size_t)uniform(rng, (float)ok.size()))];
            }
            json o = json::array();
            for(auto& x : l) if(x.is_number()) o.push_back((int)std::lround(genClamp(x.get<float>(), p.min, p.max)));
            out[p.key] = o;
        }else{
            out[p.key] = v;
        }
    }
    if(out.contains("coverGrowHeight") && !pr.params.contains("coverGrowHeight") && out.contains("coverDilate"))
        out["coverGrowHeight"] = out["coverDilate"];
    return out;
}

} // namespace forms
