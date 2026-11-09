//
//  v2Assistant.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The headless core of the AI assistant (ROADMAP 5.11): the system prompt generated from the one
//  schema (never out of date), the requests per mode, robust JSON extraction from an answer, the
//  check with v2Format's validator and the one retry. No network code: the node sends the requests
//  through Oceanode's ofxOceanodeAIAssistant.
//
//  Header-only, plain C++17 + nlohmann::json.
//

#pragma once

#include "v2Engine.h"

namespace forms2 {

enum class AssistMode : int { CreateInstrument = 0, CreateKit = 1, CreateScene = 2, Modify = 3, Fix = 4, Explain = 5 };

inline const char* assistModeName(AssistMode m){
    switch(m){
        case AssistMode::CreateInstrument: return "create-instrument";
        case AssistMode::CreateKit: return "create-kit";
        case AssistMode::CreateScene: return "create-scene";
        case AssistMode::Modify: return "modify";
        case AssistMode::Fix: return "fix";
        default: return "explain";
    }
}

// What to send: system prompt + one user message; `kind` is what the answer must be
// ("instrument", "kit", "scene", or "" for prose)
struct AssistRequest {
    AssistMode mode = AssistMode::Explain;
    std::string system, user, kind;
    bool expectsJson() const { return !kind.empty(); }
};

namespace assist_detail {

inline std::string fmt(double v){ char b[32]; std::snprintf(b, sizeof b, "%g", v); return b; }
inline std::string levels(unsigned l){
    std::string s;
    if(l & LevelEvent) s += "E"; if(l & LevelPhrase) s += "P"; if(l & LevelLayer) s += "L"; if(l & LevelScene) s += "S";
    return s;
}
// one line per parameter: key (label) type range/options default [main] levels: help
inline std::string paramLine(const ParamDef& p, const std::string& prefix = ""){
    std::string s = "  - " + prefix + p.key + " (" + p.label + "): " + p.type;
    if(p.type == "enum"){
        s += " {";
        for(size_t i = 0; i < p.options.size(); i++) s += (i ? "|" : "") + p.options[i];
        s += "}";
    } else if(p.type != "bool" && p.type != "string") s += " [" + fmt(p.min) + ", " + fmt(p.max) + "]";
    if(!p.unit.empty()) s += " " + p.unit;
    s += ", default " + p.def.dump();
    if(p.main) s += ", main";
    s += ", levels " + levels(p.levels);
    if(!p.help.empty()) s += ". " + p.help;
    return s + "\n";
}

// short, valid examples (checked by the tests)
inline const char* exampleScene(){
    return R"({"format":"espectres.scene","formatVersion":1,"name":"night","version":1,"bpm":90,"seed":7,
 "harmony":{"sections":[
  {"name":"A","meter":{"num":4,"den":4},"scale":{"name":"minor","semis":[0,2,3,5,7,8,10],"tonic":9},
   "progression":"I vi IV V","steps":[{"degree":0,"length":4},{"degree":5,"length":4},{"degree":3,"length":4},{"degree":4,"length":4}]},
  {"name":"B","meter":{"num":7,"den":8,"grouping":[2,2,3]},"scale":{"name":"minor","semis":[0,2,3,5,7,8,10],"tonic":9},
   "progression":"I:2 V:2","steps":[{"degree":0,"length":14},{"degree":4,"length":14}]}]},
 "layers":[{"zoomBeats":16,"blend":"lighten","gain":1},{"zoomBeats":8,"blend":"add","gain":1}],
 "instances":[
  {"id":"pad","type":"chords","layer":0,"follow":"chord","params":{"bloom":0.6},
   "seq":{"mode":"pattern","pattern":"x","steps":1,"pitchSource":"chord","voices":4,"hold":true},"phrase":{"length":16}},
  {"id":"stars","type":"hats","layer":1,"follow":"off","params":{"cell":"dot"},
   "seq":{"mode":"probability","density":0.5,"steps":16},"phrase":{"length":8},
   "routings":[{"source":"random1","dest":"depth","amount":1,"level":"event","min":0.2,"max":3,"sourceParams":{"scope":"event"}}]}],
 "routings":[{"source":"macro1","dest":"stars:seq.density","amount":1,"level":"phrase","min":0.1,"max":0.9}],
 "macros":{"macro1":0.5}})";
}
inline const char* examplePreset(){
    return R"({"format":"espectres.instrument","formatVersion":1,"type":"bass","name":"dark walk","version":1,
 "tags":["bass","dark"],"notes":"sparse walking bass",
 "look":{"thickness":18,"partials":2,"partialDecay":0.8,"colourRole":"bass"},
 "sequencer":{"mode":"probability","density":0.35,"pitchSource":"scale","scaleMode":"walk","octave":-1},
 "phrase":{"length":[8,16],"quant":"bars","variation":{"rhythm":"mutate","pitch":"new","amount":0.25}},
 "modulation":[{"source":"lfo1","dest":"pitchFine","amount":0.05,"level":"layer","render":"animated","sourceParams":{"shape":"sine","rateBeats":0.5}}],
 "knobs":[{"name":"dark","value":0.5,"targets":[{"dest":"look.partialDecay","min":0.2,"max":1}]}]})";
}

} // namespace assist_detail

// ---------------------------------------------------------------- system prompt
inline std::string systemPrompt(){
    using namespace assist_detail;
    installSequencerSchema();
    std::string s;
    s += "You are the assistant of ESPECTRES v2 (Forms GL2 Box), a spectral score instrument: instruments emit "
         "events that scroll past a playhead; each screen column is read as sound (height = pitch, brightness = "
         "level). You design or modify instruments (presets), kits and scenes as JSON in the v2 format below, or "
         "explain them in prose.\n\n";
    s += "## Units\nTime in beats (of the meter's denominator unit). Pitch in scale degrees only (0 = tonic, never "
         "semitones); continuous material uses height 0..1 from the bottom. Layers 0..3, each with its zoom (beats "
         "across the screen); every event has a depth (multiplier on its layer's zoom).\n\n";
    s += "## Instrument types (\"type\") and their parameters (instance \"params\" / preset \"look\")\n"
         "Line format: key (label): type [min, max] or {options}, default, main (main page) or advanced, levels a "
         "modulation routing may use (E event, P phrase, L layer, S scene).\n";
    for(auto& t : instrumentTypes()){
        s += "### " + t.name + " (" + t.label + ", " + t.family + ")\n";
        for(auto& p : t.params) s += paramLine(p);
    }
    s += "\n## Event destinations shared by every type (routing dest, bare key)\n";
    for(auto& p : eventParams()) s += paramLine(p);
    s += "\n## Layer parameters (dest layer.<key> inside an instance, layerN.<key> at scene level)\n";
    for(auto& p : layerParams()) s += paramLine(p, "layer.");
    s += "\n## Scene parameters (dest scene.<key>)\n";
    for(auto& p : sceneParams()) s += paramLine(p, "scene.");
    s += "\n## Sequencer (instance \"seq\" / preset \"sequencer\"; routing dest seq.<key>)\n";
    for(auto& p : sequencerParams()) s += paramLine(p);
    s += "\n## Layer-bus effects (types timefx and filterfx; placed in beats / phrases on the layer, no screen-space effects)\n";
    for(auto& e : effectTypes()){
        s += "  - " + e.name + " (" + e.owner + "): " + e.help + " Settings:";
        for(auto& k : e.settings) s += " " + k;
        s += "\n";
    }
    s += "\n## Modulation\nA routing: {\"source\", \"dest\", \"amount\" -1..1, \"level\": event|phrase|layer|scene, "
         "optional \"min\"/\"max\" (then amount 1 puts the value in [min, max]), \"curve\", \"invert\", \"smooth\" (beats, "
         "layer level), \"render\": drawn|animated, \"sourceParams\"}. Levels: event = read once per event, phrase = "
         "once per phrase, layer = every frame, scene = once. A level must be allowed by the destination (see levels above).\n"
         "Sources:\n"
         "  - lfo1.. {\"shape\":\"sine|triangle|saw|square|smoothRandom\",\"rateBeats\":4 or \"ratePhrases\":1,\"phase\":0,\"reset\":\"phrase|free\",\"bipolar\":false}\n"
         "  - phraseEnv1.. {\"shape\":\"rise|fall|swell|steps|drawn\",\"steps\":4,\"points\":[[x,y],...]}\n"
         "  - noteEnv1.. same shapes over each event\n"
         "  - random1.. {\"scope\":\"event|beats|phrase|scene\",\"everyBeats\":4,\"smooth\":false,\"bipolar\":false}\n"
         "  - macro1..macro8 (scene performance faders, 0..1)\n"
         "  - follow:<instanceId>:density|level|pitch {\"window\":4}\n"
         "  - brightness, arc, lane:<name>\n"
         "  - playhead {\"window\":2,\"shape\":\"approach|peak|ringout|distance\"} (mainly animated)\n"
         "Render: drawn (default) bakes the modulation into the drawn shape; animated moves every frame and is only "
         "allowed for level, pitchFine, depth, size, brightness, height, partialCount, partialBright.\n"
         "Instance routings: dest is a key of the type (bare or look.<key>), an event destination, layer.<key>, "
         "scene.<key> or seq.<key>. Scene routings: dest \"<instanceId>:<dest>\", \"layerN.<key>\" or \"scene.<key>\". "
         "Preset routings may target \"knob.<name>\".\n";
    s += "\n## Harmony\nScene-level, shared by every instance; pitch never leaves the scale. harmony.sections: each "
         "{\"name\", \"meter\":{\"num\",\"den\",\"grouping\":[2,2,3],\"swing\"}, \"scale\":{\"name\",\"semis\":[...],\"tonic\":semitone}, "
         "\"source\":\"typed|rules|timeline\", \"progression\": text, \"steps\":[{\"degree\" 0-based,\"length\" beats,"
         "\"colour\":\"triad|7|9|sus2|sus4\"}], \"lengthBeats\": 0 = one pass}. Sections follow each other, the last loops. "
         "Progression text (always also write the matching steps): degrees 1-7 or I-VII (case ignored), lengths in bars "
         "\"1:2\", random lengths \"4:1-2\", colours \"5(7)\" \"2(9)\" \"4(sus2)\" \"1(sus4)\", e.g. \"I vi IV V\" or \"1:2 6 4 5:2\". "
         "Degrees above the scale size are invalid (a 5-note scale has degrees 1-5). Meter: a bar is num beats; strong "
         "beats are the first of each group. Instances follow the harmony with \"follow\": chord|scale|pedal|off "
         "(\"pedalDegree\" for pedal).\n";
    s += "\n## Files\nScene: {\"format\":\"espectres.scene\",\"formatVersion\":1,\"name\",\"version\",\"bpm\",\"seed\",\"harmony\","
         "\"layers\":[{\"zoomBeats\",\"blend\":\"lighten|add|normal\",\"gain\"}] (max 4),\"instances\":[{\"id\" unique,\"type\","
         "\"layer\" 0-3,\"params\",\"seq\",\"phrase\":{\"length\":16 or [8,16],\"quant\":beats|\"halfBars\"|\"bars\","
         "\"variation\":{\"rhythm\":\"keep|mutate|new\",\"pitch\":...,\"amount\"}},\"follow\",\"accent\",\"swing\",\"routings\"}],"
         "\"routings\",\"macros\":{\"macro1\":0.5}}.\n"
         "Instrument preset: {\"format\":\"espectres.instrument\",\"formatVersion\":1,\"type\",\"name\",\"version\",\"tags\",\"notes\","
         "\"look\",\"sequencer\",\"phrase\",\"modulation\",\"knobs\":[{\"name\",\"value\",\"targets\":[{\"dest\",\"min\",\"max\"}]}]}.\n"
         "Kit: {\"format\":\"espectres.kit\",\"formatVersion\":1,\"name\",\"version\",\"tags\",\"notes\",\"instances\":[...as in a scene]}.\n"
         "Only keys listed above; values inside their ranges; enum values from their options.\n"
         "Example scene:\n```json\n" + std::string(exampleScene()) + "\n```\n"
         "Example instrument preset:\n```json\n" + std::string(examplePreset()) + "\n```\n";
    s += "\n## Rules\n"
         "- create / modify / fix: answer with the complete JSON object only, in one ```json fenced block, nothing else.\n"
         "- modify: return the whole modified object, keeping everything the user did not ask to change (ids, seed, other instances).\n"
         "- fix: return the whole repaired object, changing as little as possible.\n"
         "- explain: answer in short plain prose, no JSON unless asked.\n"
         "- Never invent keys, types, sources or effects not listed here.\n";
    return s;
}

// ---------------------------------------------------------------- requests
// the kind of a JSON object from its "format" field ("" if unknown)
inline std::string kindOfJson(const json& j){
    if(!j.is_object()) return "";
    std::string f = j.value("format", "");
    if(f == "espectres.scene") return "scene";
    if(f == "espectres.instrument") return "instrument";
    if(f == "espectres.kit") return "kit";
    if(j.contains("instances") && j.contains("harmony")) return "scene";
    if(j.contains("instances")) return "kit";
    if(j.contains("type") && (j.contains("look") || j.contains("sequencer"))) return "instrument";
    return "";
}

inline AssistRequest buildRequest(AssistMode mode, const std::string& userText, const std::string& currentJson = ""){
    AssistRequest r;
    r.mode = mode;
    r.system = systemPrompt();
    std::string task;
    switch(mode){
        case AssistMode::CreateInstrument: task = "Create a new instrument preset (format espectres.instrument)."; r.kind = "instrument"; break;
        case AssistMode::CreateKit: task = "Create a new kit (format espectres.kit) of instances that work together."; r.kind = "kit"; break;
        case AssistMode::CreateScene: task = "Create a new scene (format espectres.scene), with its harmony, layers and instances."; r.kind = "scene"; break;
        case AssistMode::Modify: task = "Modify the current object as asked. Return the whole object."; break;
        case AssistMode::Fix: task = "The current object is invalid. Repair it, changing as little as possible. Return the whole object."; break;
        case AssistMode::Explain: task = "Explain, in short plain prose."; break;
    }
    if(mode == AssistMode::Modify || mode == AssistMode::Fix){
        json cur; bool ok = false;
        try { cur = json::parse(currentJson); ok = true; } catch(...){}
        r.kind = ok ? kindOfJson(cur) : "";
        if(r.kind.empty()) r.kind = "scene";
    }
    r.user = "Mode: " + std::string(assistModeName(mode)) + ". " + task + "\n";
    if(!userText.empty()) r.user += "Request: " + userText + "\n";
    if(!currentJson.empty()) r.user += "Current object:\n```json\n" + currentJson + "\n```\n";
    if(r.expectsJson()) r.user += "Answer with the JSON only, in one ```json fenced block.\n";
    return r;
}

// ---------------------------------------------------------------- answers
namespace assist_detail {
// the end of a balanced {...} starting at i (strings and escapes respected), npos if none
inline size_t matchBrace(const std::string& s, size_t i){
    int depth = 0; bool str = false, esc = false;
    for(size_t k = i; k < s.size(); k++){
        char c = s[k];
        if(str){ if(esc) esc = false; else if(c == '\\') esc = true; else if(c == '"') str = false; continue; }
        if(c == '"') str = true;
        else if(c == '{') depth++;
        else if(c == '}'){ if(--depth == 0) return k; }
    }
    return std::string::npos;
}
inline bool parseObject(const std::string& t, json& out){
    try { json j = json::parse(t); if(j.is_object()){ out = j; return true; } } catch(...){}
    return false;
}
// first parsable object in a text: whole text, else each balanced {...} in order
inline bool firstObject(const std::string& t, json& out){
    if(parseObject(t, out)) return true;
    for(size_t i = t.find('{'); i != std::string::npos; i = t.find('{', i + 1)){
        size_t e = matchBrace(t, i);
        if(e != std::string::npos && parseObject(t.substr(i, e - i + 1), out)) return true;
    }
    return false;
}
} // namespace assist_detail

// The JSON object of a model answer: fenced blocks first (```json or ```), the largest that parses;
// else the first balanced object in the text. False (and error) when there is none.
inline bool extractJson(const std::string& answer, json& out, std::string* error = nullptr){
    using namespace assist_detail;
    json best; size_t bestSize = 0; bool found = false;
    size_t p = 0;
    while((p = answer.find("```", p)) != std::string::npos){
        size_t bodyStart = answer.find('\n', p + 3);
        if(bodyStart == std::string::npos) break;
        size_t end = answer.find("```", bodyStart);
        std::string body = answer.substr(bodyStart + 1, end == std::string::npos ? std::string::npos : end - bodyStart - 1);
        json j;
        if(firstObject(body, j) && body.size() > bestSize){ best = j; bestSize = body.size(); found = true; }
        if(end == std::string::npos) break;
        p = end + 3;
    }
    if(!found) found = firstObject(answer, best);
    if(found){ out = best; return true; }
    if(error) *error = "no JSON object found in the answer";
    return false;
}

// Validate an answer as `kind` ("instrument", "kit", "scene" or "" = from its format). Empty = valid.
inline std::vector<std::string> checkAnswer(const json& j, const std::string& kindIn){
    installSequencerSchema();
    std::vector<std::string> out;
    if(!j.is_object()) return {"the answer is not a JSON object"};
    std::string kind = kindIn.empty() ? kindOfJson(j) : kindIn;
    std::string found = kindOfJson(j);
    if(kind.empty()) return {"cannot tell whether the answer is an instrument, a kit or a scene (missing \"format\")"};
    if(!found.empty() && found != kind) out.push_back("expected a " + kind + " but got a " + found);
    // raw checks the parsers would silently default
    auto checkRoutings = [&](const json& a, const std::string& where){
        if(!a.is_array()) return;
        for(auto& r : a){
            if(!r.is_object()){ out.push_back(where + ": routing is not an object"); continue; }
            if(r.contains("level")){
                std::string l = r["level"].is_string() ? r["level"].get<std::string>() : "";
                if(l != "event" && l != "phrase" && l != "layer" && l != "scene") out.push_back(where + ": routing to '" + r.value("dest", "") + "' has unknown level " + r["level"].dump());
            }
            Routing rr; rr.source = r.value("source", "");
            if(r.contains("sourceParams") && r["sourceParams"].is_object()) rr.sourceParams = r["sourceParams"];
            if(mod::kindOf(rr) == mod::Kind::Unknown) out.push_back(where + ": unknown modulation source '" + rr.source + "'");
        }
    };
    auto checkInstances = [&](const json& a){
        if(!a.is_array()) return;
        for(auto& e : a){
            if(!e.is_object()) continue;
            std::string where = "instance '" + e.value("id", std::string()) + "'";
            if(e.contains("routings")) checkRoutings(e["routings"], where);
            if(e.contains("follow") && e["follow"].is_string()){
                std::string f = e["follow"].get<std::string>();
                if(f != "chord" && f != "scale" && f != "pedal" && f != "off") out.push_back(where + ": unknown follow '" + f + "'");
            }
        }
    };
    try {
        if(kind == "instrument"){
            if(j.contains("modulation")) checkRoutings(j["modulation"], "preset");
            for(auto& p : validate(presetFromJson(j))) out.push_back(p);
        } else if(kind == "kit"){
            if(j.contains("instances")) checkInstances(j["instances"]);
            for(auto& p : validate(kitFromJson(j))) out.push_back(p);
        } else {
            if(j.contains("instances")) checkInstances(j["instances"]);
            if(j.contains("routings")) checkRoutings(j["routings"], "scene");
            // typed progressions
            if(j.contains("harmony") && j["harmony"].contains("sections") && j["harmony"]["sections"].is_array()){
                auto& secs = j["harmony"]["sections"];
                for(size_t s = 0; s < secs.size(); s++){
                    if(!secs[s].is_object() || !secs[s].contains("progression")) continue;
                    if(!secs[s]["progression"].is_string()){ out.push_back("harmony section " + std::to_string(s) + ": progression is not text"); continue; }
                    Meter m = secs[s].contains("meter") ? meterFromJson(secs[s]["meter"]) : Meter{};
                    Scale sc = secs[s].contains("scale") ? scaleFromJson(secs[s]["scale"]) : Scale{};
                    std::vector<ProgressionStep> st; std::string err;
                    if(!parseProgression(secs[s]["progression"].get<std::string>(), m, st, err, sc.size()))
                        out.push_back("harmony section " + std::to_string(s) + ": progression " + err);
                }
            }
            for(auto& p : validate(sceneDocFromJson(j))) out.push_back(p);
        }
    } catch(const std::exception& e){
        out.push_back(std::string("malformed ") + kind + ": " + e.what());
    }
    return out;
}

// Extract + check in one go; problems empty = ok (out holds the object)
inline std::vector<std::string> checkAnswerText(const std::string& answer, const std::string& kind, json& out){
    std::string err;
    if(!extractJson(answer, out, &err)) return {err};
    return checkAnswer(out, kind);
}

// The one retry: the problems go back with the previous answer
inline AssistRequest retryRequest(const AssistRequest& original, const std::vector<std::string>& problems, const std::string& previousAnswer){
    AssistRequest r = original;
    r.user = original.user + "\nYour previous answer:\n" + previousAnswer + "\n\nIt has these problems:\n";
    for(auto& p : problems) r.user += "- " + p + "\n";
    r.user += "Fix every problem and answer again with the complete corrected JSON only, in one ```json fenced block.\n";
    return r;
}
inline AssistRequest retryRequest(const std::vector<std::string>& problems, const std::string& previousAnswer, const std::string& kind = "scene"){
    AssistRequest base;
    base.mode = AssistMode::Fix; base.system = systemPrompt(); base.kind = kind;
    base.user = "Mode: fix.\n";
    return retryRequest(base, problems, previousAnswer);
}

} // namespace forms2
