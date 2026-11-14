//
//  formsAssistantV1.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport: the assistant for v1 presets and scenes)
//
//  The text side of the v1 assistant (the window side is formsEditorBackports.h): the system
//  prompt, written from v1's schema (generators.json as Engine::gens reads it, the preset and
//  scene formats, the library and the presets there are, one example preset per generator), the
//  request, the retry after a rejected answer, the normalisation of an answer's obvious slips
//  (a bare number where a range goes, a label used as a key, a value out of bounds...) and the
//  validation against the schema. An answer that still fails is shown with its problems, never
//  applied. Plain C++ and nlohmann::json (tests/test_v1_backports.cpp).
//

#pragma once

#include "formsEngine.h"
#include <sstream>
#include <set>
#include <cstdio>

namespace forms {

enum class AssistV1Mode { NewPreset = 0, ModifyPreset, NewScene, ModifyScene };
inline const char* assistV1ModeName(AssistV1Mode m){
    switch(m){ case AssistV1Mode::NewPreset: return "New instrument preset"; case AssistV1Mode::ModifyPreset: return "Modify the selected preset";
               case AssistV1Mode::NewScene: return "New scene"; default: return "Modify the selected scene"; }
}
inline bool assistV1IsScene(AssistV1Mode m){ return m == AssistV1Mode::NewScene || m == AssistV1Mode::ModifyScene; }

inline std::string assistV1Num(double x){ char b[32]; snprintf(b, sizeof(b), "%g", x); return b; }
inline bool assistV1IsLegacy(const GenDef& g){ return g.family.rfind("legacy", 0) == 0; }

// One parameter's value form, for the prompt: "[lo, hi] ints 0..31, default [0, 0]: shift (per lane)"
inline std::string assistV1ParamLine(const ParamDef& p){
    std::string f;
    std::string b = assistV1Num(p.min) + ".." + assistV1Num(p.max);
    if(p.type == "int") f = "int " + b;
    else if(p.type == "float") f = "number " + b;
    else if(p.type == "intRange") f = "[lo, hi] ints " + b;
    else if(p.type == "floatRange") f = "[lo, hi] numbers " + b;
    else if(p.type == "bool") f = "true | false";
    else if(p.type == "intList") f = "[ints " + b + "]";
    else if(p.type == "enum"){ for(size_t i = 0; i < p.options.size(); i++) f += (i ? " | \"" : "\"") + p.options[i] + "\""; }
    else f = p.type;
    std::string d = p.def.is_null() ? "" : p.def.dump();
    if(!d.empty()) f += ", default " + d;
    return "  " + p.key + " = " + f + (p.label.empty() ? "" : ": " + p.label);
}

// A generator's own parameters, grouped (process keys left out: they are listed once)
inline void assistV1GenParams(std::ostream& o, const GenDef& g, bool own){
    std::string group;
    for(auto& p : g.params){
        if(own && g.isProcessKey(p.key)) continue;
        if(p.group != group){
            group = p.group;
            o << "  [" << (group.empty() ? "params" : group) << "]";
            auto it = g.groupNotes.find(group);
            if(it != g.groupNotes.end()) o << " " << it->second;
            o << "\n";
        }
        o << assistV1ParamLine(p) << "\n";
    }
}

// One example preset per generator (the shortest real one that sets a few parameters), in the
// in-memory form the answer must use
inline std::string assistV1Examples(const Engine& e){
    std::map<std::string, std::string> best;
    for(auto& [r, p] : e.presets){
        json j = p;
        j.erase("legacy"); j.erase("notes");
        std::string s = j.dump();
        size_t n = p.isStack() ? p.layers.size() : p.params.size();
        if(n < (p.isStack() ? 2u : 4u) || s.size() > 700) continue;
        auto it = best.find(p.gen);
        if(it == best.end() || s.size() < it->second.size()) best[p.gen] = s;
    }
    std::string o;
    for(auto& [g, s] : best) o += s + "\n";
    return o;
}

inline std::string assistV1SystemPrompt(const Engine& e){
    std::ostringstream o;
    o << "You write JSON for ESPECTRES v1 (Forms Box): spectral scores drawn as images (X = time, Y = pitch, one image = "
         "several bars) by parameterised generators, scrolled and sonified. Answer with ONE JSON object only, no prose, no code fences.\n\n"
         "INSTRUMENT PRESET: {\"gen\": <generator>, \"name\": <short text>, \"notes\": <one line, optional>, \"category\": <one of";
    for(auto& c : presetCategories()) o << " " << c;
    o << ">, \"params\": {<key>: <value>, ...}}.\n"
         "VALUE FORMS (the listing below gives each key's form):\n"
         "- int / number: a number, or {\"min\": a, \"max\": b} (a new number drawn per image), or {\"options\": [n1, n2, ...]} (one drawn per image).\n"
         "- [lo, hi]: ALWAYS a two-element list [lo, hi] with lo <= hi, also for a fixed value ([4, 4]); or {\"options\": [[lo, hi], [lo, hi]]}. "
         "Never a bare number, never {min, max}.\n"
         "- enum: one of its options spelled exactly, or {\"options\": [\"a\", \"b\"]}.\n"
         "- true | false: a bool, or {\"either\": true}.\n"
         "- [ints]: a flat list of ints, or {\"options\": [[...], [...]]}.\n"
         "Write only the keys the request needs: every key left out takes its default. Only the keys listed exist (a generator's own keys "
         "and the PROCESS keys, all directly in \"params\"); never invent keys or nest objects under them; numbers stay inside their bounds.\n"
         "Optional \"linked\": [{<key>: <value>, ..., \"weight\": w}, ...]: one set chosen per image (by weight), laid over params.\n"
         "LAYERED PRESET (several instruments drawn together as one): {\"gen\": \"stack\", \"name\", \"category\", \"layers\": [{\"gen\": g, \"params\": {...}} "
         "or {\"preset\": \"<gen>/<name>\"}, ...], \"pick\": \"all\" | \"one\" | \"group\"}; each layer optionally \"name\" (beside its params, never inside), "
         "\"chance\" (0-100), \"group\", \"weight\".\n\n"
         "WHICH GENERATOR:\n"
         "- pulse (Notes): pitched notes on scale degrees from a step pattern: bass lines, melodies, riffs, arpeggios, chords, pads, stabs.\n"
         "- hits (Percussion): unpitched hits in a band of the image: kick sweeps, noise-band hats and snares, klanks, blocks. Drums.\n"
         "- masses (Lines): many lines gliding together: Xenakis masses, chord clouds, harmonic fans.\n"
         "- sweep (Noise sweeps): risers and falls over bars.\n"
         "- shapes (Geometry): free geometry, letters, flow fields.\n"
         "- effects (Processes): a layer that only processes what is drawn under it (freeze, beat repeat, gate, echo, mirror, masks, fades).\n"
         "- stack: a drum kit or a whole groove = one stack with one hits layer per drum (kick, snare, hats) and pulse layers for bass or chords.\n"
         "STEP PATTERNS (pulse and hits): \"steps\" per bar (16 = sixteenths, 8 = eighths), \"patternSteps\" = the pattern's length in steps "
         "(it repeats; only the first patternSteps entries of a chance list are read), \"fills\" = [hits per lane] spread euclidean-ly, "
         "\"shift\" = [lo, hi] rotation in whole steps. For an exact rhythm use \"pattern\": \"chance list\" and \"stepChances\": one 0-100 chance "
         "per pattern step (100 = always, 0 = never), with \"patternSteps\" = the list's length; e.g. \"steps\": 16, \"patternSteps\": 16, "
         "kick on the 1 and the and-of-2: [100,0,0,0,0,0,100,0,0,0,0,0,0,0,0,0], snare on 2 and 4: [0,0,0,0,100,0,0,0,0,0,0,0,100,0,0,0]. "
         "\"lanes\" > 1: lane 1 plays every other pattern and the others answer (A B A C).\n"
         "In hits, \"cell\" picks the drum (kick sweep, noise band, snare, taiko, klank...), \"bandLow\"/\"bandHigh\" its register "
         "(0 = bottom), \"length\" its length in steps.\n\n"
         "SCENE: {\"name\", \"bpm\" (20-300), \"palette\" (a name from the library), \"scale\" (a name from the library), \"notegrid\" (12-400), "
         "\"root\" (MIDI, 0-96), \"degreeWeights\" (12 numbers: chance of each scale degree per bar), \"transposeWeights\" (12 numbers: "
         "chance of each transposition 0..11 per bar), \"layers\": [one or more of {\"bars\" (1-64; 0.5 or 0.25 only for a very fast layer), \"repeat\" (1-16), \"globalProb\" (0-100), "
         "\"groupProb\": [0..1 per group], \"rows\" (cells used), \"blend\" (lighten | add | normal), \"gain\" (0-2), "
         "\"cells\": [{\"item\": <a preset ref \"<gen>/<name>\" from PRESETS, or a legacy method>, \"prob\" (0-100), \"group\" (int)}]}], "
         "\"performance\": {} (leave empty unless asked)}. A cell's item is always a ref string, never an inline preset: pick the closest "
         "existing presets (new instruments are written apart, as presets).\n\n";
    o << "GENERATORS (key = form, default: meaning):\n";
    const GenDef* fx = nullptr;
    for(auto& g : e.gens){
        if(assistV1IsLegacy(g)) continue;
        if(g.name == "effects"){ fx = &g; continue; }
        o << "- " << g.name << " (" << g.label << "): " << g.description << "\n";
        assistV1GenParams(o, g, true);
    }
    if(fx){
        o << "- effects (" << fx->label << "): " << fx->description << "\n  Its params are the PROCESS keys below plus:\n";
        for(auto& p : fx->params) if(!fx->isProcessKey(p.key) || p.key == "steps") o << assistV1ParamLine(p) << "\n";
        o << "PROCESS keys (every generator takes them in its params, acting on its own notes; 0 chance = off):\n";
        assistV1GenParams(o, *fx, false);
    }
    o << "\nEXAMPLES (real presets, one per generator):\n" << assistV1Examples(e);
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
        case AssistV1Mode::NewPreset: u = "Write a new instrument preset. Choose the generator by WHICH GENERATOR (a drum groove is a stack of hits layers), "
                                          "name it after the request, set only the keys that matter, with the exact value forms.\n"; break;
        case AssistV1Mode::ModifyPreset: u = "Modify this instrument preset; keep its gen and name, change only what the request asks and return the whole preset.\nCURRENT:\n" + current + "\n"; break;
        case AssistV1Mode::NewScene: u = "Write a new scene.\n"; break;
        case AssistV1Mode::ModifyScene: u = "Modify this scene; keep its name, change only what the request asks and return the whole scene.\nCURRENT:\n" + current + "\n"; break;
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

// What is appended to the request when an answer was rejected: the answer, its problems, and the
// schema lines of the parameters concerned
inline std::string assistV1RetryPrompt(const Engine& e, const std::string& answer, const std::vector<std::string>& problems){
    std::string u = "\n\nYOUR PREVIOUS ANSWER WAS REJECTED. IT WAS:\n" + (answer.size() > 6000 ? answer.substr(0, 6000) + "..." : answer) + "\nPROBLEMS:";
    for(auto& p : problems) u += "\n- " + p;
    json j; const GenDef* g = nullptr;
    if(extractJsonObject(answer, j)) g = e.findGen(j.value("gen", std::string()));
    std::set<std::string> keys;
    for(auto& p : problems){
        std::string s = p;
        // "linked: key: ..." / "layer n: key: ..." -> key
        for(;;){
            size_t c = s.find(": ");
            if(c == std::string::npos) break;
            std::string head = s.substr(0, c);
            s = s.substr(c + 2);
            if(head == "linked" || head.rfind("layer ", 0) == 0) continue;
            if(!head.empty() && head.find(' ') == std::string::npos && head.find('"') == std::string::npos) keys.insert(head);
            break;
        }
    }
    std::string lines;
    for(auto& k : keys){
        const ParamDef* p = g ? g->param(k) : nullptr;
        if(!p) for(auto& gg : e.gens) if(!assistV1IsLegacy(gg) && (p = gg.param(k))) break;
        if(p) lines += assistV1ParamLine(*p) + "\n";
    }
    if(!lines.empty()) u += "\nTHE PARAMETERS CONCERNED (key = form, default: meaning):\n" + lines;
    u += "\nAnswer again with ONE corrected JSON object: the whole preset, only existing keys, [lo, hi] lists for every range, numbers inside their bounds.";
    return u;
}

// ---------------------------------------------------------------- normalisation

// A key or label, comparable: lower case, letters and digits only ("low cut" == "lowcut" == "lowCut")
inline std::string assistV1Norm(const std::string& s){
    std::string o;
    for(unsigned char c : s) if(isalnum(c)) o += (char)tolower(c);
    return o;
}

inline const ParamDef* assistV1FindParam(const GenDef& g, const std::string& k){
    if(const ParamDef* p = g.param(k)) return p;
    std::string n = assistV1Norm(k);
    if(n.empty()) return nullptr;
    for(auto& p : g.params) if(assistV1Norm(p.key) == n) return &p;
    for(auto& p : g.params) if(assistV1Norm(p.label) == n) return &p;
    return nullptr;
}

inline const GenDef* assistV1FindGen(const Engine& e, const std::string& name){
    if(const GenDef* g = e.findGen(name)) return g;
    std::string n = assistV1Norm(name);
    if(n.empty()) return nullptr;
    for(auto& g : e.gens) if(!assistV1IsLegacy(g) && (assistV1Norm(g.name) == n || assistV1Norm(g.label) == n)) return &g;
    return nullptr;
}

// A number written as a string becomes a number
inline void assistV1ToNumber(json& x){
    if(!x.is_string()) return;
    const std::string s = x.get<std::string>();
    try { size_t n = 0; double d = std::stod(s, &n); if(n == s.size()) x = d; } catch(...) {}
}

// The obvious slips in a params object, fixed in place and noted: label used as key, a bare number or
// {min, max} where [lo, hi] goes, [a, b] where a number goes, strings for numbers / bools, an enum
// option in other spelling, values out of bounds (clamped), null values (dropped)
inline void normalizeParams(const GenDef& g, json& params, const std::string& where, std::vector<std::string>& notes, json* aside = nullptr){
    if(!params.is_object()) return;
    json out = json::object();
    for(auto& [k, v0] : params.items()){
        json v = v0;
        const ParamDef* p = assistV1FindParam(g, k);
        std::string key = p ? p->key : k;
        if(p && key != k) notes.push_back(where + "\"" + k + "\" read as \"" + key + "\"");
        if(!p && v.is_string()){
            std::string n = assistV1Norm(k);   // a label written inside params ("name": "kick"): kept aside, not a parameter
            if(n == "name" || n == "notes" || n == "label" || n == "comment" || n == "description"){
                if(aside) (*aside)[n == "label" ? "name" : n] = v;
                notes.push_back(where + "\"" + k + "\" is not a parameter (" + (aside ? "kept on the layer" : "dropped") + ")");
                continue;
            }
        }
        if(v.is_null()){ notes.push_back(where + key + ": null dropped (the default plays)"); continue; }
        if(p){
            std::string at = where + key + ": ";
            auto clamp = [&](json& x, const char* what){
                if(!x.is_number()) return;
                double d = x.get<double>();
                if(d < p->min - 1e-6 || d > p->max + 1e-6){
                    double c = std::min(std::max(d, (double)p->min), (double)p->max);
                    notes.push_back(at + what + " " + assistV1Num(d) + " clamped to " + assistV1Num(c));
                    if(p->type == "int" || p->type == "intRange" || p->type == "intList") x = (int)std::lround(c); else x = c;
                }
            };
            if(p->isNumber()){
                assistV1ToNumber(v);
                if(v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()){ v = json{{"min", v[0]}, {"max", v[1]}}; notes.push_back(at + "[a, b] read as {min, max}"); }
                else if(v.is_array() && v.size() == 1){ v = v[0]; assistV1ToNumber(v); notes.push_back(at + "[a] read as a"); }
                if(v.is_number()) clamp(v, "value");
                else if(v.is_object() && v.contains("min") && v.contains("max")){
                    assistV1ToNumber(v["min"]); assistV1ToNumber(v["max"]);
                    clamp(v["min"], "min"); clamp(v["max"], "max");
                    if(v["min"].is_number() && v["max"].is_number() && v["min"].get<double>() > v["max"].get<double>()){ std::swap(v["min"], v["max"]); notes.push_back(at + "min and max swapped"); }
                }
                else if(v.is_object() && v.contains("options") && v["options"].is_array()){ for(auto& x : v["options"]){ assistV1ToNumber(x); clamp(x, "an option"); } }
            }else if(p->isRange()){
                assistV1ToNumber(v);
                if(v.is_number()){ v = json::array({v, v}); notes.push_back(at + "a single number read as [" + assistV1Num(v[0].get<double>()) + ", " + assistV1Num(v[0].get<double>()) + "]"); }
                else if(v.is_object() && v.contains("min") && v.contains("max")){ v = json::array({v["min"], v["max"]}); notes.push_back(at + "{min, max} read as [lo, hi]"); }
                else if(v.is_object() && v.contains("lo") && v.contains("hi")){ v = json::array({v["lo"], v["hi"]}); notes.push_back(at + "{lo, hi} read as [lo, hi]"); }
                else if(v.is_array() && v.size() == 1){ v = json::array({v[0], v[0]}); notes.push_back(at + "[a] read as [a, a]"); }
                auto fixPair = [&](json& r){
                    if(!r.is_array() || r.size() != 2) return;
                    assistV1ToNumber(r[0]); assistV1ToNumber(r[1]);
                    clamp(r[0], "lo"); clamp(r[1], "hi");
                    if(r[0].is_number() && r[1].is_number() && r[0].get<double>() > r[1].get<double>()){ std::swap(r[0], r[1]); notes.push_back(at + "lo and hi swapped"); }
                };
                if(v.is_array()) fixPair(v);
                else if(v.is_object() && v.contains("options") && v["options"].is_array()) for(auto& x : v["options"]) fixPair(x);
            }else if(p->type == "enum"){
                auto canon = [&](json& x){
                    if(!x.is_string()) return;
                    std::string s = x.get<std::string>();
                    if(std::find(p->options.begin(), p->options.end(), s) != p->options.end()) return;
                    std::string n = assistV1Norm(s);
                    for(auto& opt : p->options) if(assistV1Norm(opt) == n){ notes.push_back(at + "\"" + s + "\" read as \"" + opt + "\""); x = opt; return; }
                };
                if(v.is_string()) canon(v);
                else if(v.is_object() && v.contains("options") && v["options"].is_array()) for(auto& x : v["options"]) canon(x);
            }else if(p->type == "bool"){
                if(v.is_string()){
                    std::string n = assistV1Norm(v.get<std::string>());
                    if(n == "true" || n == "yes" || n == "on" || n == "1"){ v = true; notes.push_back(at + "read as true"); }
                    else if(n == "false" || n == "no" || n == "off" || n == "0"){ v = false; notes.push_back(at + "read as false"); }
                    else if(n == "either" || n == "random"){ v = json{{"either", true}}; notes.push_back(at + "read as {either}"); }
                }else if(v.is_number()){ v = v.get<double>() != 0; notes.push_back(at + "a number read as " + (v.get<bool>() ? "true" : "false")); }
            }else if(p->type == "intList"){
                assistV1ToNumber(v);
                if(v.is_number()){ v = json::array({v}); notes.push_back(at + "a single number read as a one-item list"); }
                if(v.is_array()) for(auto& x : v){ assistV1ToNumber(x); clamp(x, "an item"); }
            }
        }
        out[key] = v;
    }
    // a chance list is read over patternSteps entries: without patternSteps the list's length is meant
    if(out.value("pattern", std::string()) == "chance list" && out.contains("stepChances") && out["stepChances"].is_array() && !out["stepChances"].empty() && !out.contains("patternSteps")){
        const ParamDef* ps = g.param("patternSteps");
        if(ps){
            int n = std::min((int)out["stepChances"].size(), (int)ps->max);
            out["patternSteps"] = n;
            notes.push_back(where + "patternSteps set to " + std::to_string(n) + " (the chance list's length)");
        }
    }
    params = out;
}

// An answer's slips fixed in place (notes say what changed); what remains wrong is left for the validator
inline void normalizePresetV1(const Engine& e, json& j, std::vector<std::string>& notes){
    if(!j.is_object()) return;
    auto mergeProcess = [&](json& o, const std::string& where){
        if(!o.contains("process") || !o["process"].is_object()) return;
        if(!o.contains("params") || !o["params"].is_object()) o["params"] = json::object();
        for(auto& [k, v] : o["process"].items()) o["params"][k] = v;
        o.erase("process");
        notes.push_back(where + "\"process\" merged into params");
    };
    auto fixGen = [&](json& o, const std::string& where) -> const GenDef* {
        std::string gen = o.value("gen", std::string());
        if(gen == "stack") return nullptr;
        const GenDef* g = assistV1FindGen(e, gen);
        if(g && g->name != gen){ notes.push_back(where + "gen \"" + gen + "\" read as \"" + g->name + "\""); o["gen"] = g->name; }
        return g;
    };
    mergeProcess(j, "");
    const GenDef* g = fixGen(j, "");
    if(j.value("gen", std::string()) == "stack"){
        if(j.contains("layers") && j["layers"].is_array()){
            int i = 0;
            for(auto& l : j["layers"]){
                std::string w = "layer " + std::to_string(++i) + ": ";
                if(!l.is_object() || l.contains("preset")) continue;
                mergeProcess(l, w);
                if(const GenDef* lg = fixGen(l, w)) if(l.contains("params")) normalizeParams(*lg, l["params"], w, notes, &l);
            }
        }
        if(j.contains("pick") && j["pick"].is_string()){
            std::string p = assistV1Norm(j["pick"].get<std::string>());
            if(p != j["pick"].get<std::string>() && (p == "all" || p == "one" || p == "group")){ notes.push_back("pick read as \"" + p + "\""); j["pick"] = p; }
        }
    }else if(g){
        if(j.contains("params")) normalizeParams(*g, j["params"], "", notes);
        if(j.contains("linked") && j["linked"].is_array())
            for(auto& s : j["linked"]) if(s.is_object()){
                json w = s.contains("weight") ? s["weight"] : json();
                s.erase("weight");
                normalizeParams(*g, s, "linked: ", notes);
                if(!w.is_null()) s["weight"] = w;
            }
    }
    if(j.contains("category")){
        std::string c = j["category"].is_string() ? j["category"].get<std::string>() : std::string(), n = assistV1Norm(c), found;
        for(auto& k : presetCategories()) if(assistV1Norm(k) == n) found = k;
        if(found.empty()){ notes.push_back("category \"" + c + "\" dropped (not one of the categories)"); j.erase("category"); }
        else if(found != c){ notes.push_back("category \"" + c + "\" read as \"" + found + "\""); j["category"] = found; }
    }
}

// ---------------------------------------------------------------- validation

inline void validateParams(const GenDef& g, const json& params, const std::string& where, std::vector<std::string>& bad, const Engine* e = nullptr){
    if(!params.is_object()){ bad.push_back(where + "params is not an object"); return; }
    for(auto& [k, v] : params.items()){
        const ParamDef* p = g.param(k);
        if(!p){
            std::string hint;   // a key of another generator, named
            if(e) for(auto& og : e->gens) if(!assistV1IsLegacy(og) && og.name != g.name && og.param(k)){ hint = " (it belongs to " + og.name + ")"; break; }
            bad.push_back(where + "unknown parameter \"" + k + "\" for " + g.name + hint);
            continue;
        }
        std::string at = where + k + ": ";
        auto inRange = [&](const json& x){ return x.is_number() && x.get<double>() >= p->min - 1e-6 && x.get<double>() <= p->max + 1e-6; };
        if(p->isNumber()){
            if(v.is_number()){ if(!inRange(v)) bad.push_back(at + "out of [" + assistV1Num(p->min) + ", " + assistV1Num(p->max) + "]"); }
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
            auto pair = [](const json& x){ return x.is_array() && x.size() == 2 && x[0].is_number() && x[1].is_number(); };
            if(v.is_object() && v.contains("options") && v["options"].is_array() && !v["options"].empty()){
                for(auto& x : v["options"]) if(!pair(x)){ bad.push_back(at + "every option must be [lo, hi]"); break; }
            }
            else if(!pair(v)) bad.push_back(at + "expected [lo, hi]");
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
            validateParams(*g, l.value("params", json::object()), w, bad, &e);
        }
        std::string pick = j.value("pick", std::string("all"));
        if(pick != "all" && pick != "one" && pick != "group") bad.push_back("pick must be all, one or group");
        return bad;
    }
    const GenDef* g = e.findGen(gen);
    if(!g){ bad.push_back("unknown generator \"" + gen + "\""); return bad; }
    validateParams(*g, j.value("params", json::object()), "", bad, &e);
    if(j.contains("linked")){
        if(!j["linked"].is_array()) bad.push_back("linked must be a list");
        else for(auto& s : j["linked"]) if(s.is_object()){ json p = s; p.erase("weight"); validateParams(*g, p, "linked: ", bad, &e); }
    }
    return bad;
}

inline std::vector<std::string> validateSceneV1(const Engine& e, const json& j){
    std::vector<std::string> bad;
    if(!j.is_object()){ bad.push_back("not a JSON object"); return bad; }
    // the slips a model makes, named before the parse would fail on them
    if(j.contains("layers") && j["layers"].is_array()){
        int li = 0;
        for(auto& l : j["layers"]){
            std::string w = "layer " + std::to_string(++li) + ": ";
            if(!l.is_object() || !l.contains("cells") || !l["cells"].is_array()) continue;
            int ci = 0;
            for(auto& c : l["cells"]){
                ci++;
                if(c.is_object() && c.contains("item") && !c["item"].is_string())
                    bad.push_back(w + "cell " + std::to_string(ci) + ": item must be a preset ref \"<gen>/<name>\" from PRESETS (a string), not an inline preset");
                else if(c.is_string()) bad.push_back(w + "cell " + std::to_string(ci) + ": a cell is {\"item\": <ref>, \"prob\", \"group\"}, not a bare string");
            }
        }
    }
    if(!bad.empty()) return bad;
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
    if(s.layers.empty()) bad.push_back("at least one layer is required");
    static const std::vector<std::string> blends = {"lighten", "add", "normal"};
    for(size_t i = 0; i < s.layers.size(); i++){
        auto& l = s.layers[i];
        std::string w = "layer " + std::to_string(i + 1) + ": ";
        if(l.bars < 1.0 / kBarGrid || l.bars > 64) bad.push_back(w + "bars outside 1/16-64");
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
