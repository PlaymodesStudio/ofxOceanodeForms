//
//  formsAssistantV1.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport: the assistant for v1 presets and scenes)
//
//  The text side of the v1 assistant (the window side is formsEditorAssistant.h): the system
//  prompt, written from v1's schema (generators.json as Engine::gens reads it, the preset and
//  scene formats, the library and the presets there are), the request, and the validation of an
//  answer against that schema. An answer that fails is shown with its problems, never applied.
//  Plain C++ and nlohmann::json (tests/test_v1_backports.cpp).
//

#pragma once

#include "formsEngine.h"
#include <sstream>
#include <set>

namespace forms {

enum class AssistV1Mode { NewPreset = 0, ModifyPreset, NewScene, ModifyScene };
inline const char* assistV1ModeName(AssistV1Mode m){
    switch(m){ case AssistV1Mode::NewPreset: return "New instrument preset"; case AssistV1Mode::ModifyPreset: return "Modify the selected preset";
               case AssistV1Mode::NewScene: return "New scene"; default: return "Modify the selected scene"; }
}
inline bool assistV1IsScene(AssistV1Mode m){ return m == AssistV1Mode::NewScene || m == AssistV1Mode::ModifyScene; }

inline std::string assistV1SystemPrompt(const Engine& e){
    std::ostringstream o;
    o << "You write JSON for ESPECTRES v1 (Forms Box): spectral scores drawn as images (X = time, Y = pitch) by "
         "parameterised generators, scrolled and sonified. Answer with ONE JSON object only, no prose, no code fences.\n\n"
         "INSTRUMENT PRESET: {\"gen\": <generator>, \"name\": <text>, \"notes\": <optional text>, \"params\": {<key>: <value>}}.\n"
         "Parameter values: int / float: a number, {\"min\": a, \"max\": b} (a new value per image) or {\"options\": [numbers]}; "
         "enum: one of its options, or {\"options\": [...]}; bool: true / false or {\"either\": true}; intRange / floatRange: [lo, hi]; "
         "intList: a list of ints, or {\"options\": [[...], [...]]}. Keys left out take the default. Only the keys listed below exist; "
         "numbers stay inside their [min, max].\n"
         "Optional: \"linked\": [{<key>: <value>, \"weight\": w}, ...] (one set chosen per image, laid over params). "
         "A layered preset has \"gen\": \"stack\", \"layers\": [{\"gen\": g, \"params\": {...}} or {\"preset\": \"<gen>/<name>\"}, ...], "
         "\"pick\": \"all\" | \"one\" | \"group\", each layer optionally \"chance\" (0-100), \"group\", \"weight\".\n\n"
         "SCENE: {\"name\", \"bpm\" (20-300), \"palette\" (a name from the library), \"scale\" (a name from the library), \"notegrid\" (12-400), "
         "\"root\" (MIDI, 0-96), \"degreeWeights\" (12 numbers: chance of each scale degree per bar), \"transposeWeights\" (12 numbers: "
         "chance of each transposition 0..11 per bar), \"layers\": [1 to 4 of {\"bars\" (1-64), \"repeat\" (1-16), \"globalProb\" (0-100), "
         "\"groupProb\": [0..1 per group], \"rows\" (cells used), \"blend\" (lighten | add | normal), \"gain\" (0-2), "
         "\"cells\": [{\"item\": <a preset ref \"<gen>/<name>\" or a legacy method>, \"prob\" (0-100), \"group\" (int)}]}], "
         "\"performance\": {} (leave empty unless asked)}.\n\n";
    o << "GENERATORS:\n";
    for(auto& g : e.gens){
        o << "- " << g.name << " (" << g.family << "): " << g.description << "\n  params:";
        for(auto& p : g.params){
            o << " " << p.key << ":" << p.type;
            if(p.isNumber() || p.isRange()) o << "[" << p.min << "," << p.max << "]";
            if(!p.options.empty()){ o << "{"; for(size_t i = 0; i < p.options.size(); i++) o << (i ? "|" : "") << p.options[i]; o << "}"; }
        }
        o << "\n";
    }
    o << "\nPALETTES:";
    for(auto& p : e.paletteLib) o << " \"" << p.name << "\"";
    o << "\nSCALES:";
    int n = 0;
    for(auto& s : e.scaleLib){ if(n++ > 120) break; o << " \"" << s.name << "\""; }
    o << "\nPRESETS (refs for scene cells and stack layers):";
    for(auto& [r, p] : e.presets) o << " \"" << r << "\"";
    o << "\n";
    return o.str();
}

inline std::string assistV1UserPrompt(AssistV1Mode m, const std::string& request, const std::string& current){
    std::string u;
    switch(m){
        case AssistV1Mode::NewPreset: u = "Write a new instrument preset.\n"; break;
        case AssistV1Mode::ModifyPreset: u = "Modify this instrument preset; keep its gen and name, change only what the request asks.\nCURRENT:\n" + current + "\n"; break;
        case AssistV1Mode::NewScene: u = "Write a new scene.\n"; break;
        case AssistV1Mode::ModifyScene: u = "Modify this scene; keep its name, change only what the request asks.\nCURRENT:\n" + current + "\n"; break;
    }
    return u + "REQUEST: " + request;
}

// The first JSON object in a text (code fences and prose around it are dropped)
inline bool extractJsonObject(const std::string& text, json& out){
    size_t a = text.find('{');
    while(a != std::string::npos){
        int depth = 0; bool str = false, esc = false;
        for(size_t i = a; i < text.size(); i++){
            char c = text[i];
            if(str){ if(esc) esc = false; else if(c == '\\') esc = true; else if(c == '"') str = false; continue; }
            if(c == '"') str = true;
            else if(c == '{') depth++;
            else if(c == '}' && --depth == 0){
                try { out = json::parse(text.substr(a, i - a + 1)); return out.is_object(); } catch(...) { break; }
            }
        }
        a = text.find('{', a + 1);
    }
    return false;
}

inline void validateParams(const GenDef& g, const json& params, const std::string& where, std::vector<std::string>& bad){
    if(!params.is_object()){ bad.push_back(where + "params is not an object"); return; }
    for(auto& [k, v] : params.items()){
        const ParamDef* p = g.param(k);
        if(!p){ bad.push_back(where + "unknown parameter \"" + k + "\" for " + g.name); continue; }
        std::string at = where + k + ": ";
        auto inRange = [&](const json& x){ return x.is_number() && x.get<double>() >= p->min - 1e-6 && x.get<double>() <= p->max + 1e-6; };
        if(p->isNumber()){
            if(v.is_number()){ if(!inRange(v)) bad.push_back(at + "out of [" + std::to_string(p->min) + ", " + std::to_string(p->max) + "]"); }
            else if(v.is_object() && v.contains("options")){
                if(!v["options"].is_array() || v["options"].empty()) bad.push_back(at + "options must be a list of numbers");
                else for(auto& x : v["options"]) if(!inRange(x)){ bad.push_back(at + "an option is not a number in range"); break; }
            }
            else if(v.is_object() && v.contains("min") && v.contains("max")){
                if(!inRange(v["min"]) || !inRange(v["max"])) bad.push_back(at + "min / max out of range");
                else if(v["min"].get<double>() > v["max"].get<double>()) bad.push_back(at + "min above max");
            }
            else bad.push_back(at + "expected a number, {min, max} or {options}");
        }else if(p->type == "enum"){
            auto ok = [&](const json& x){ return x.is_string() && std::find(p->options.begin(), p->options.end(), x.get<std::string>()) != p->options.end(); };
            if(v.is_string()){ if(!ok(v)) bad.push_back(at + "\"" + v.get<std::string>() + "\" is not one of its options"); }
            else if(v.is_object() && v.contains("options") && v["options"].is_array()){ for(auto& x : v["options"]) if(!ok(x)){ bad.push_back(at + "an option is not valid"); break; } }
            else bad.push_back(at + "expected one of its options");
        }else if(p->type == "bool"){
            if(!v.is_boolean() && !(v.is_object() && v.contains("either"))) bad.push_back(at + "expected true / false or {\"either\": true}");
        }else if(p->isRange()){
            if(!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) bad.push_back(at + "expected [lo, hi]");
        }else if(p->type == "intList"){
            bool ok = v.is_array() || (v.is_object() && v.contains("options") && v["options"].is_array());
            if(!ok) bad.push_back(at + "expected a list of ints or {options: [[...]]}");
        }
    }
}

// A preset answer: problems (empty = valid)
inline std::vector<std::string> validatePresetV1(const Engine& e, const json& j){
    std::vector<std::string> bad;
    if(!j.is_object()){ bad.push_back("not a JSON object"); return bad; }
    std::string gen = j.value("gen", std::string()), name = j.value("name", std::string());
    if(name.empty() || name.find('/') != std::string::npos) bad.push_back("name missing or contains '/'");
    if(gen == "stack"){
        json layers = j.value("layers", json::array());
        if(!layers.is_array() || layers.empty()) bad.push_back("a stack needs layers");
        int i = 0;
        for(auto& l : layers){
            std::string w = "layer " + std::to_string(++i) + ": ";
            if(!l.is_object()){ bad.push_back(w + "not an object"); continue; }
            if(l.contains("preset")){
                std::string r = l.value("preset", std::string());
                if(!e.presets.count(r)) bad.push_back(w + "preset \"" + r + "\" does not exist");
                continue;
            }
            const GenDef* g = e.findGen(l.value("gen", std::string()));
            if(!g){ bad.push_back(w + "unknown generator \"" + l.value("gen", std::string()) + "\""); continue; }
            validateParams(*g, l.value("params", json::object()), w, bad);
        }
        std::string pick = j.value("pick", std::string("all"));
        if(pick != "all" && pick != "one" && pick != "group") bad.push_back("pick must be all, one or group");
        return bad;
    }
    const GenDef* g = e.findGen(gen);
    if(!g){ bad.push_back("unknown generator \"" + gen + "\""); return bad; }
    validateParams(*g, j.value("params", json::object()), "", bad);
    if(j.contains("linked")){
        if(!j["linked"].is_array()) bad.push_back("linked must be a list");
        else for(auto& s : j["linked"]) if(s.is_object()){ json p = s; p.erase("weight"); validateParams(*g, p, "linked: ", bad); }
    }
    return bad;
}

inline std::vector<std::string> validateSceneV1(const Engine& e, const json& j){
    std::vector<std::string> bad;
    if(!j.is_object()){ bad.push_back("not a JSON object"); return bad; }
    Scene s;
    try { s = j.get<Scene>(); } catch(std::exception& ex){ bad.push_back(std::string("not a scene: ") + ex.what()); return bad; }
    if(s.name.empty()) bad.push_back("name missing");
    if(s.bpm < 20 || s.bpm > 300) bad.push_back("bpm outside 20-300");
    if(s.notegrid < 12 || s.notegrid > 400) bad.push_back("notegrid outside 12-400");
    if(s.root < 0 || s.root > 96) bad.push_back("root outside 0-96");
    bool pal = false, sc = false;
    for(auto& p : e.paletteLib) if(p.name == s.palette) pal = true;
    for(auto& x : e.scaleLib) if(x.name == s.scale) sc = true;
    if(!pal) bad.push_back("palette \"" + s.palette + "\" is not in the library");
    if(!sc) bad.push_back("scale \"" + s.scale + "\" is not in the library");
    if(s.degreeWeights.size() != 12 || s.transposeWeights.size() != 12) bad.push_back("degreeWeights / transposeWeights need 12 numbers");
    if(s.layers.empty() || s.layers.size() > 4) bad.push_back("1 to 4 layers");
    static const std::vector<std::string> blends = {"lighten", "add", "normal"};
    for(size_t i = 0; i < s.layers.size(); i++){
        auto& l = s.layers[i];
        std::string w = "layer " + std::to_string(i + 1) + ": ";
        if(l.bars < 1 || l.bars > 64) bad.push_back(w + "bars outside 1-64");
        if(std::find(blends.begin(), blends.end(), l.blend) == blends.end()) bad.push_back(w + "unknown blend \"" + l.blend + "\"");
        if(l.cells.empty()) bad.push_back(w + "no cells");
        // known items: presets, the method list, and anything a scene already plays
        std::set<std::string> known;
        for(auto& m : e.methods) known.insert(m.first);
        for(auto& [n, os] : e.scenes) for(auto& ol : os.layers) for(auto& oc : ol.cells) known.insert(oc.item);
        for(auto& c : l.cells){
            if(c.item.empty()) continue;   // an empty row
            if(!e.presets.count(c.item) && !known.count(c.item)) bad.push_back(w + "item \"" + c.item + "\" does not exist");
            if(c.prob < 0 || c.prob > 100) bad.push_back(w + "prob outside 0-100");
        }
        if(l.fixed.is_object()) bad.push_back(w + "frozen layers are not written by the assistant");
    }
    return bad;
}

} // namespace forms
