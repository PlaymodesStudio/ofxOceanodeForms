//
//  v2Legacy.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Legacy mapping (ROADMAP 5.5): v1 presets (pulse / hits / masses / sweep / effects / shapes /
//  stack, params + "process" block) and v1 scenes (layers of cells naming "<gen>/<name>") become v2
//  instances and a v2 SceneDoc. The mapping is by meaning, not by pixels: v2 drops v1's pixel
//  emulation, so every result carries a confidence (0..1) and the list of what could not be mapped.
//
//  Value forms in v1 presets:
//    number / string / bool        a fixed value
//    [a, b]                        a per-note range (drawn for each note / hit)
//    {"min": a, "max": b}          a per-image range (drawn once per image)  -> per-phrase random routing
//    {"options": [...]}            a per-image choice                         -> per-phrase random routing
//  Keys absent from a preset take the generator default from v1's generators.json.
//
//  Main rules (see mapPulse / mapHits / ... for details):
//    pulse   -> bass / melody / chords / arpeggio / bells (name hints, then arp, voices, register)
//    hits    -> kick / snare / hats / texture (cell, band height, length)
//    masses  -> mass, sweep -> sweep, shapes -> shapes
//    effects + process blocks -> timefx / filterfx instances on the same layer (one per active process)
//    stack   -> one instance per stack layer (pick / weights approximated with phrase chances)
//    rhythm  euclidean steps / fills / shift -> steps / hits / rotation, patternSteps -> patternLength,
//            random fills -> probability density, chance list -> stepChances, density -> chance (note),
//            "same as the previous layer" -> follow mode
//    pitch   degree / octave / voices / arp -> pitchSource, scaleMode, voices, order; v1 octave - 2 ->
//            v2 octave; v1 per-bar semitone transposes become step key shifts in the scene harmony
//    scene   tempo, phrase = image bars, scale from the library folded into one octave (degree-only),
//            progression = the transpose semitones as per-step key shifts (tonic moves), one bar each,
//            layers zoom = image beats, palette -> colour roles (kept in a "legacy" block of the file)
//
//  Header-only, plain C++17 + nlohmann::json. Reads v1 files, never writes them.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "v2Format.h"
#include "v2Sequencer.h"

namespace forms2 {
namespace legacy {

// ---------------------------------------------------------------- the v1 library
struct Library {
    std::map<std::string, json> presets;                  // "<gen>/<name>" -> preset json
    std::map<std::string, std::string> byLegacy;          // legacy method name ("h67_tranceBass2") -> "<gen>/<name>"
    std::map<std::string, json> defaults;                 // gen -> {key: default}
    std::map<std::string, std::vector<int>> scales;       // name -> semitones
    std::map<std::string, std::vector<std::string>> palettes;   // name -> hex colours
    std::string error;
};

inline bool readJsonFile(const std::string& path, json& out){
    std::ifstream f(path); if(!f) return false;
    try { f >> out; } catch(...){ return false; }
    return true;
}

// sketchDir: the v1 sketch folder (presets/, library/, generators.json)
inline bool loadLibrary(const std::string& sketchDir, Library& lib){
    namespace fs = std::filesystem;
    json g;
    if(!readJsonFile(sketchDir + "/generators.json", g)){ lib.error = "generators.json"; return false; }
    if(g.contains("generators")) for(auto& G : g["generators"]){
        json d = json::object();
        if(G.contains("params")) for(auto& p : G["params"]) if(p.contains("key") && p.contains("default")) d[p["key"].get<std::string>()] = p["default"];
        lib.defaults[G.value("name", "")] = d;
    }
    json sc, pa;
    if(readJsonFile(sketchDir + "/library/scales.json", sc) && sc.contains("scales"))
        for(auto& s : sc["scales"]) lib.scales[s.value("name", "")] = s.value("notes", std::vector<int>{});
    if(readJsonFile(sketchDir + "/library/palettes.json", pa) && pa.contains("palettes"))
        for(auto& p : pa["palettes"]) lib.palettes[p.value("name", "")] = p.value("colors", std::vector<std::string>{});
    std::error_code ec;
    std::vector<fs::path> files;
    for(auto& gd : fs::directory_iterator(sketchDir + "/presets", ec)){
        if(!gd.is_directory()) continue;
        for(auto& f : fs::directory_iterator(gd.path(), ec)) if(f.path().extension() == ".json") files.push_back(f.path());
    }
    std::sort(files.begin(), files.end());
    for(auto& f : files){
        json j; if(!readJsonFile(f.string(), j)) continue;
        std::string gen = j.value("gen", f.parent_path().filename().string());
        std::string name = j.value("name", f.stem().string());
        lib.presets[gen + "/" + name] = j;
        // older scenes name cells by the legacy method; the first preset porting it wins (files sorted)
        std::string leg = j.value("legacy", "");
        if(!leg.empty() && !lib.byLegacy.count(leg)) lib.byLegacy[leg] = gen + "/" + name;
    }
    if(lib.presets.empty()){ lib.error = "no presets"; return false; }
    return true;
}

// ---------------------------------------------------------------- results
struct MapResult {
    std::vector<Instance> instances;
    double confidence = 1;
    std::vector<std::string> unmapped;    // v1 keys / features with no v2 counterpart
    std::vector<std::string> approx;      // mapped, but only roughly
};

struct SceneMap {
    SceneDoc doc;
    double confidence = 1;
    std::vector<std::string> unmapped;    // "<preset>: <what>" (deduplicated)
    std::vector<std::string> errors;      // validation problems (empty when the doc validates)
    json legacy = json::object();         // palette, colour roles, source info (written in the file)
    // per v1 layer, per cell index: the ids made from that cell (empty: skipped)
    std::vector<std::vector<std::vector<std::string>>> cellIds;
    std::vector<std::vector<std::string>> cellItems;
    std::vector<int> scaleSemis;
    int tonic = 0;
};

// ---------------------------------------------------------------- value access
struct V {
    bool has = false;            // present in the preset (else the default)
    int form = 0;                // 0 fixed, 1 per-note range, 2 per-image range, 3 per-image options
    double v = 0, lo = 0, hi = 0;
    std::string s;
    json raw;
    bool perImage() const { return (form == 2 || form == 3) && hi > lo; }
    bool spread() const { return hi > lo; }
};

inline double numOf(const json& x, double def = 0){
    if(x.is_number()) return x.get<double>();
    if(x.is_boolean()) return x.get<bool>() ? 1 : 0;
    if(x.is_array() && !x.empty()){ double s = 0; int n = 0; for(auto& e : x) if(e.is_number()){ s += e.get<double>(); n++; } return n ? s / n : def; }
    return def;
}

inline V parseValue(const json& x){
    V r; r.raw = x;
    if(x.is_number() || x.is_boolean()){ r.v = r.lo = r.hi = numOf(x); }
    else if(x.is_string()){ r.s = x.get<std::string>(); }
    else if(x.is_array()){
        r.form = 1;
        if(!x.empty() && x[0].is_number()){
            r.lo = 1e300; r.hi = -1e300; double s = 0;
            for(auto& e : x) if(e.is_number()){ double f = e.get<double>(); r.lo = std::min(r.lo, f); r.hi = std::max(r.hi, f); s += f; }
            r.v = x.size() == 2 ? (r.lo + r.hi) * 0.5 : s / x.size();
        } else if(x.empty()){ r.form = 0; }
    } else if(x.is_object()){
        if(x.contains("min") || x.contains("max")){
            r.form = 2; r.lo = numOf(x.value("min", json(0))); r.hi = numOf(x.value("max", json(r.lo)));
            if(r.hi < r.lo) std::swap(r.lo, r.hi);
            r.v = (r.lo + r.hi) * 0.5;
        } else if(x.contains("options") && x["options"].is_array() && !x["options"].empty()){
            r.form = 3; const json& o = x["options"];
            if(o[0].is_string()){ r.s = o[0].get<std::string>(); }
            else {
                // the mean of the options (one is chosen per image; the most frequent one made a
                // chance list [0, 0, 0, 25, 25] a flat 0); options may be [a, b] ranges
                double sum = 0; r.lo = 1e300; r.hi = -1e300;
                for(auto& e : o){ double f = numOf(e); sum += f; r.lo = std::min(r.lo, f); r.hi = std::max(r.hi, f); }
                r.v = sum / o.size();
            }
        }
    }
    return r;
}

// Parameters of one v1 generator call: the preset's params over the generator defaults
struct Params {
    json p = json::object(), d = json::object();
    std::set<std::string> used;
    V get(const std::string& k){
        used.insert(k);
        if(p.contains(k)){ V v = parseValue(p[k]); v.has = true; return v; }
        if(d.contains(k)) return parseValue(d[k]);
        return V{};
    }
    bool has(const std::string& k) const { return p.contains(k); }
    double num(const std::string& k, double def = 0){ V v = get(k); return (v.form == 0 && v.s.empty() && !v.raw.is_null()) || v.form > 0 ? v.v : def; }
    std::string str(const std::string& k, const std::string& def = ""){ V v = get(k); return v.s.empty() ? def : v.s; }
    std::vector<std::string> unusedKeys() const {
        std::vector<std::string> out;
        for(auto it = p.begin(); it != p.end(); ++it) if(!used.count(it.key())) out.push_back(it.key());
        return out;
    }
};

// ---------------------------------------------------------------- mapping context
struct MapCtx {
    int layer = 0;
    int bars = 8;
    double imageBeats = 32;
    int scaleSize = 5;
    std::vector<int> v1Scale, v2Scale;   // library scale notes / folded degrees (degree offsets)
    std::string prevId;            // previous rhythmic instance on the layer (follow modes)
    std::set<std::string>* used = nullptr;   // ids already taken in the scene
    int routingN = 0;              // random source counter inside the current instance
};

inline int snapDegree(int semis, const std::vector<int>& scale);

inline std::string slug(const std::string& s){
    std::string o;
    for(char c : s){
        if(std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
        else if(!o.empty() && o.back() != '_') o += '_';
    }
    while(!o.empty() && o.back() == '_') o.pop_back();
    if(o.empty()) o = "inst";
    if(std::isdigit((unsigned char)o[0])) o = "p" + o;
    return o;
}
inline std::string uniqueId(MapCtx& c, const std::string& base){
    std::string id = slug(base); int n = 2;
    if(c.used){ while(c.used->count(id)) id = slug(base) + "_" + std::to_string(n++); c.used->insert(id); }
    return id;
}
inline double clampd(double v, double a, double b){ return std::max(a, std::min(b, v)); }
inline int clampi(double v, int a, int b){ return std::max(a, std::min(b, (int)std::lround(v))); }
inline bool hasWord(const std::string& name, const char* w){ return name.find(w) != std::string::npos; }

// One mapping in progress
struct Builder {
    MapCtx& c;
    MapResult& R;
    Instance in;
    std::string name;
    Builder(MapCtx& ctx, MapResult& res, const std::string& type, const std::string& nm) : c(ctx), R(res), name(nm) {
        in.type = type; in.layer = c.layer; c.routingN = 0;
        in.params = json::object(); in.seq = json::object();
        in.phrase = json{{"length", c.imageBeats}};
    }
    void look(const std::string& k, const json& v){ in.params[k] = v; }
    void seq(const std::string& k, const json& v){ in.seq[k] = v; }
    // a per-phrase (or per-event) random routing over [lo, hi]; checked against the schema levels
    bool randomRouting(const std::string& dest, double lo, double hi, Level lv = LevelPhrase){
        if(!(hi > lo)) return false;
        const ParamDef* d = findParam(findType(in.type), dest);
        if(!d || !(d->levels & lv)){ R.approx.push_back(name + ": no " + std::string(lv == LevelEvent ? "per-note" : "per-image") + " range for " + dest); return false; }
        lo = clampd(lo, d->min, d->max); hi = clampd(hi, d->min, d->max);
        if(!(hi > lo)) return false;
        Routing r;
        r.source = "random" + std::to_string(++c.routingN);
        r.dest = dest; r.amount = 1; r.level = lv; r.hasRange = true; r.min = lo; r.max = hi;
        r.sourceParams = json{{"scope", lv == LevelEvent ? "event" : "phrase"}};
        in.routings.push_back(r);
        return true;
    }
    bool lfoRouting(const std::string& dest, double lo, double hi, double rateBeats, const std::string& shape){
        const ParamDef* d = findParam(findType(in.type), dest);
        if(!d || !(d->levels & (LevelEvent | LevelLayer))) return false;
        Routing r;
        r.source = "lfo" + std::to_string(++c.routingN);
        // drawn along each event: the engine reads layer-level routings at every position
        r.dest = dest; r.amount = 1; r.level = (d->levels & LevelLayer) ? LevelLayer : LevelEvent; r.hasRange = true;
        r.min = clampd(lo, d->min, d->max); r.max = clampd(hi, d->min, d->max);
        r.sourceParams = json{{"shape", shape}, {"rateBeats", clampd(rateBeats, 0.0625, 64)}, {"reset", "phrase"}};
        in.routings.push_back(r);
        return true;
    }
    void approx(const std::string& what){ R.approx.push_back(name + ": " + what); }
    void unmapped(const std::string& what){ R.unmapped.push_back(name + ": " + what); }
};

// value with its per-image range as a routing (fixed base = the most likely value)
inline double rangedLook(Builder& b, V v, const std::string& key, double scale, double mn, double mx, bool isInt = false){
    double base = clampd(v.v * scale, mn, mx);
    if(isInt) base = std::lround(base);
    if(v.perImage()) b.randomRouting(key, v.lo * scale, v.hi * scale, LevelPhrase);
    else if(v.form == 1 && v.spread()) b.randomRouting(key, v.lo * scale, v.hi * scale, LevelEvent);
    return base;
}

// ---------------------------------------------------------------- colour roles
inline std::string colourRole(const std::string& v1colour, const std::string& type){
    if(v1colour == "white" || v1colour.empty()) return "neutral";
    if(type == "bass" || type == "kick") return "bass";
    if(type == "chords" || type == "bells" || type == "sweep") return "highlight";
    if(type == "mass" || type == "texture") return "shadow";
    return "accent";
}

// ---------------------------------------------------------------- rhythm (pulse / hits)
// steps per bar of the pattern grid
inline double stepBeatsOf(Params& P){
    double steps = std::max(1.0, P.num("steps", 16)), sb = std::max(1.0, P.num("stepBars", 1));
    return 4.0 * sb / steps;
}

inline void mapRhythm(Params& P, Builder& b){
    V steps = P.get("steps"), patSteps = P.get("patternSteps"), stepBars = P.get("stepBars");
    double sb = std::max(1.0, stepBars.v);
    int st = clampi(steps.v / sb, 1, 64);
    if(sb > 1 && std::fabs(steps.v / sb - st) > 1e-6) b.approx("steps spanning " + std::to_string((int)sb) + " bars rounded to " + std::to_string(st) + " per bar");
    b.seq("steps", st);
    if(steps.perImage()) b.randomRouting("seq.steps", steps.lo / sb, steps.hi / sb);
    int pl = clampi(patSteps.v, 1, 64);
    if(pl != st) b.seq("patternLength", pl);
    if(patSteps.perImage()) b.randomRouting("seq.patternLength", patSteps.lo, patSteps.hi);
    std::string pattern = P.str("pattern", "euclidean");
    std::string follow = P.str("follow", "off");
    V density = P.get("density");
    if(pattern == "same as the previous layer" || follow != "off"){
        if(!b.c.prevId.empty()){
            b.seq("mode", "follow"); b.seq("followId", b.c.prevId);
            b.seq("followMode", follow.find("skipped") != std::string::npos ? "skipped" : (pattern == "same as the previous layer" ? "hits" : "both"));
            if(follow.find("skipped") != std::string::npos) b.approx("follow 'skipped' ignores its own pattern");
        } else { b.unmapped("follows an earlier layer but none precedes it"); pattern = "euclidean"; }
    }
    if(!b.in.seq.contains("mode")){
        if(pattern == "random fills"){
            V fc = P.get("fillChance");
            b.seq("mode", "probability"); b.seq("density", clampd(fc.v / 100, 0, 1));
            if(fc.perImage()) b.randomRouting("seq.density", fc.lo / 100, fc.hi / 100);
        } else if(pattern == "chance list"){
            V sc = P.get("stepChances");
            json list = sc.raw;
            if(sc.form == 3) list = sc.raw["options"][0], b.approx("step chance lists chosen per image: first list kept");
            std::vector<int> ch;
            if(list.is_array()) for(auto& e : list) ch.push_back(clampi(numOf(e), 0, 100));
            b.seq("mode", "probability");
            if(!ch.empty()){ b.seq("stepChances", ch); if(!b.in.seq.contains("patternLength") && (int)ch.size() != st) b.seq("patternLength", clampi(pl, 1, 64)); }
            else b.seq("density", 1.0);
        } else {
            V fills = P.get("fills"), shift = P.get("shift");
            b.seq("mode", "euclidean");
            double h = fills.v;
            bool reroll = P.num("reroll", 0) > 0.5;   // v1: new fills and shift every pattern repetition
            if(fills.form == 1 && fills.raw.size() > 1){
                // one of the listed fills per image (per repetition with reroll): seq hitsList
                std::vector<int> f; for(auto& e : fills.raw) f.push_back(clampi(numOf(e), 0, 64));
                h = f[0];
                b.seq("hitsList", f);
            }
            b.seq("hits", clampi(h, 0, 64));
            b.seq("rotation", clampi(shift.lo, 0, 64));
            if(shift.spread()){
                // one shift of the range per image (per repetition with reroll): seq rotationList
                std::vector<int> r; for(int v = clampi(shift.lo, 0, 64); v <= clampi(shift.hi, 0, 64); v++) r.push_back(v);
                b.seq("rotationList", r);
            }
            if(reroll && (fills.raw.size() > 1 || shift.spread())) b.seq("reroll", true);
        }
    }
    // density thins the pattern (percent of steps kept)
    if(density.v < 100 - 1e-9 && b.in.seq.value("mode", "") != "probability"){
        b.seq("chance", clampd(density.v, 0, 100));
        if(density.perImage()) b.randomRouting("seq.chance", density.lo, density.hi);
    } else if(density.has && b.in.seq.value("mode", "") == "probability" && density.v < 100){
        b.seq("chance", clampd(density.v, 0, 100));
    }
    V rep = P.get("repChance");
    if(rep.v < 100 - 1e-9 || (rep.perImage() && rep.lo < 100 - 1e-9)){
        if(!b.in.seq.contains("chance")){
            b.seq("chance", clampd(rep.v, 0, 100)); b.seq("chanceScope", "beats");
            b.seq("chanceBeats", clampd(pl * stepBeatsOf(P), 0.25, 64));
            if(rep.perImage()) b.randomRouting("seq.chance", clampd(rep.lo, 0, 100), clampd(rep.hi, 0, 100), LevelPhrase);
        } else b.approx("repetition chance folded away (density already uses the chance)");
    }
    // bars that play -> barMask over the image bars (v1 counts bars from 0: "even" = 0, 2, ...)
    std::string bars = P.str("bars", "all");
    if(bars != "all"){
        std::string m;
        int nb = std::max(1, b.c.bars);
        for(int i = 0; i < nb; i++){
            bool on = bars == "first" ? i == 0 : bars == "last" ? i == nb - 1 : bars == "even" ? i % 2 == 0 : i % 2 == 1;
            if(i) m += ' ';
            m += on ? '1' : '0';
        }
        b.seq("barMask", m);
    }
    // v1 counts repetitions from 0 ("odd" = 2nd, 4th...); v2 counts from 1
    std::string playOn = P.str("playOn", "all");
    if(playOn == "odd") b.seq("repMask", "even");
    else if(playOn == "even") b.seq("repMask", "odd");
    V rests = P.get("rests");
    if(rests.raw.is_array() && !rests.raw.empty()){
        json r = json::array();
        for(auto& e : rests.raw) if(e.is_number()) r.push_back(clampi(numOf(e), 0, 63));
        b.seq("rests", r);
    }
    if(P.str("trigger", "hits") != "hits") b.unmapped("trigger = " + P.str("trigger"));
    if(P.num("noRepeat", 0) > 0.5) b.seq("noRepeat", true);
    if(P.num("reroll", 0) > 0.5 && !b.in.seq.value("reroll", false)) b.approx("reroll per pattern repetition: nothing to reroll in this pattern (or a probability pattern, drawn per step anyway)");
    P.get("clearChance");   // mapped by addClearBlank (a blank effect listed before the instrument)
    P.get("lanes"); if(P.num("lanes", 1) > 1) b.approx("lanes: " + std::to_string((int)P.num("lanes", 1)) + " lanes share one pattern");
    P.get("patternStart"); P.get("followBack"); P.get("fillChance"); P.get("stepChances"); P.get("clearScope"); P.get("clearLength");
}

// ---------------------------------------------------------------- effects / process blocks
// A v1 process block (or effects preset) -> timefx / filterfx instances. Each active process is one
// instance whose slots are planned by the sequencer (pattern "x" per slot, chance = v1 chance).
// inBar: the bar's slots that play ('x' / '.', one per slot; empty = all)
inline void slotSeq(Builder& b, double slotBeats, double chance, const std::string& barsMode, int bars, const std::string& inBar = ""){
    int perBar = slotBeats <= 4 ? clampi(4.0 / std::max(0.0625, slotBeats), 1, 64) : 1;
    int span = slotBeats > 4 ? clampi(slotBeats / 4, 1, 64) : 1;
    b.seq("steps", perBar);
    std::string pat;
    std::string barPat = (int)inBar.size() == perBar && span == 1 ? inBar : std::string((size_t)perBar, 'x');
    bool partial = barPat.find('.') != std::string::npos;
    if(barsMode == "all" || barsMode.empty()){
        pat = partial ? barPat : "x" + std::string((size_t)std::max(0, span - 1), '.');
    } else if(bars * perBar <= 64){
        for(int bar = 0; bar < bars; bar++){
            bool on = barsMode == "first" ? bar == 0 : barsMode == "last" ? bar == bars - 1 : barsMode == "even" ? bar % 2 == 0 : bar % 2 == 1;
            pat += on ? barPat : std::string((size_t)perBar, '.');
        }
    } else {
        // grid too long for a pattern: the bar mask picks the bars
        pat = "x"; std::string m;
        for(int bar = 0; bar < bars; bar++){
            bool on = barsMode == "first" ? bar == 0 : barsMode == "last" ? bar == bars - 1 : barsMode == "even" ? bar % 2 == 0 : bar % 2 == 1;
            m += std::string(bar ? " " : "") + (on ? "1" : "0");
        }
        b.seq("barMask", m);
    }
    b.seq("mode", "pattern"); b.seq("pattern", pat);
    if(pat.size() > 1 && (int)pat.size() != perBar) b.seq("patternLength", clampi((double)pat.size(), 1, 64));
    b.seq("noteLength", (double)span);   // slots longer than a bar span `span` one-bar steps
    if(chance < 100 - 1e-9) b.seq("chance", clampd(chance, 0, 100));
    if(b.in.type == "timefx") b.look("slotBeats", clampd(slotBeats, 0.25, 64));
}

inline void mapProcess(const json& params, const json& defaults, MapCtx& c, MapResult& R, const std::string& name, double stepBeats){
    Params P; P.p = params.is_object() ? params : json::object(); P.d = defaults;
    auto active = [&](std::initializer_list<const char*> keys){ for(auto k : keys) if(P.has(k)) return true; return false; };
    std::string barsMode = P.str("processBars", "all");
    if(P.str("processOn", "own layer") != "own layer") R.approx.push_back(name + ": process acts on the whole image (v2: its own layer bus)");
    auto add = [&](Builder& b){ b.in.id = uniqueId(c, name + " " + b.in.params.value("effect", "fx")); R.instances.push_back(b.in); };
    // chances take the v1 generator default when absent (0 for every process: off unless set)
    auto pct = [&](const char* k, double def){ V v = P.get(k); return P.has(k) || P.d.contains(k) ? v.v : def; };

    // band filter
    if(active({"bandMode", "bandChance", "bandCenter", "bandHeight", "bandDivisions"}) && pct("bandChance", 100) > 0){
        std::string mode = P.str("bandMode", "pass");
        Builder b(c, R, "filterfx", name);
        b.look("effect", mode == "notch" ? "notch" : mode == "noise" ? "mask" : "band");
        V ce = P.get("bandCenter"), he = P.get("bandHeight"), fa = P.get("bandFade");
        b.look("bandLow", clampd(ce.v - he.v * 0.5, 0, 1)); b.look("bandHigh", clampd(ce.v + he.v * 0.5, 0, 1));
        if(ce.spread()){
            // one random value moves both edges (same source name = same draw), so the band keeps its height
            int n0 = b.c.routingN;
            Level lv = ce.form == 1 ? LevelEvent : LevelPhrase;   // v1 per-slot range: a new band per slot
            b.randomRouting("bandLow", ce.lo - he.v * 0.5, ce.hi - he.v * 0.5, lv);
            b.c.routingN = n0;
            b.randomRouting("bandHigh", ce.lo + he.v * 0.5, ce.hi + he.v * 0.5, lv);
        }
        b.look("feather", clampd(fa.v * 2, 0, 1)); b.look("amount", 1.0);
        // v1: slices of bar / divisions; bandPart [from, length] of the bar; bandUnit "bar" rolls the
        // chance once per bar (all its slices together), "slice" per slice
        int div = clampi(P.num("bandDivisions", 4), 1, 64);
        std::string unit = P.str("bandUnit", "slice");
        V part = P.get("bandPart");
        double p0 = P.has("bandPart") ? part.lo : 0, p1 = P.has("bandPart") ? part.hi : 1;
        if(part.raw.is_array() && part.raw.size() == 2){ p0 = numOf(part.raw[0], 0); p1 = numOf(part.raw[1], 1); }
        std::string inBar;
        for(int k = 0; k < div; k++){ double fr = k / (double)div; inBar += (fr >= p0 - 1e-9 && fr < p0 + p1 - 1e-9) ? 'x' : '.'; }
        double ch = pct("bandChance", 100);
        slotSeq(b, 4.0 / div, unit == "bar" ? 100 : ch, barsMode, c.bars, inBar);
        if(unit == "bar" && ch < 100 - 1e-9){ b.seq("chance", clampd(ch, 0, 100)); b.seq("chanceScope", "beats"); b.seq("chanceBeats", 4.0); }
        if(P.has("bandWander")) b.look("shape", "lfo"), b.approx("band wander becomes an lfo amount shape");
        P.get("bandCurve"); P.get("bandPart"); P.get("bandNoiseScale"); P.get("bandNoiseContrast");
        add(b);
    }
    // gate
    if(active({"gateChance", "gateDivisions", "gateOpen", "gateShape", "gateEuclidSteps"}) && pct("gateChance", 100) > 0){
        Builder b(c, R, "filterfx", name);
        b.look("effect", "gate");
        V dv = P.get("gateDivisions"), op = P.get("gateOpen");
        b.look("gateRate", clampd(4.0 / std::max(1.0, dv.v), 0.0625, 8));
        if(dv.spread()) b.randomRouting("gateRate", 4.0 / std::max(1.0, dv.hi), 4.0 / std::max(1.0, dv.lo));
        std::string sh = P.str("gateShape", "hard");
        // hard gates keep the open part; fades (saw / tri per division) keep about half of each division
        b.look("gateDuty", clampd(sh == "hard" ? op.v : 0.5 * op.v + 0.25, 0.05, 0.95));
        b.look("amount", sh == "hard" ? 1.0 : 0.7);
        if(sh != "hard") b.approx("gate shape '" + sh + "' drawn as a softened hard gate");
        if(P.has("gateEuclidSteps")) b.approx("euclidean gate becomes a regular gate");
        slotSeq(b, 4, pct("gateChance", 100), barsMode, c.bars);
        P.get("gateCurve"); P.get("gatePart"); P.get("gateSwellChance"); P.get("gateEuclidFills"); P.get("gateEuclidDivs"); P.get("gateEuclidPower");
        add(b);
    }
    // high / low pass as a soft band
    if((P.has("fadeTop") && P.num("fadeTop") > 0) || (P.has("fadeBottom") && P.num("fadeBottom") > 0)){
        Builder b(c, R, "filterfx", name);
        b.look("effect", "band");
        double fb = P.num("fadeBottom", 0), ft = P.num("fadeTop", 0);
        b.look("bandLow", clampd(fb * 0.5, 0, 0.9)); b.look("bandHigh", clampd(1 - ft * 0.5, 0.1, 1));
        b.look("feather", 0.5); b.look("amount", 1.0);
        if(P.num("fadeWander", 0) > 0){ b.look("shape", "lfo"); b.look("gateRate", clampd(P.num("fadeWanderBars", 2) * 4, 0.0625, 8)); b.approx("fade wander becomes an lfo amount shape"); }
        slotSeq(b, 4, 100, barsMode, c.bars);
        P.get("fadeCurve"); P.get("fadeShape"); P.get("fadeTopEnd"); P.get("fadeBottomEnd"); P.get("fadeWanderSplit"); P.get("fadeWanderBars");
        if(P.has("fadeTopEnd") || P.has("fadeBottomEnd")) b.approx("fade that changes across the image kept steady");
        add(b);
    }
    // edge fade -> vignette
    if(P.has("edgeFade") && P.num("edgeFade") > 0){
        // v1: both image edges fade over edgeFade x width; v2 vignette: that portion of the slot at each
        // edge, the slot being the whole image (a shorter slot cut the clouds mid-image with hard seams)
        Builder b(c, R, "filterfx", name);
        V ef = P.get("edgeFade");
        b.look("effect", "vignette"); b.look("vignetteSize", clampd(ef.v, 0, 1)); b.look("amount", 1.0);
        if(ef.perImage()) b.randomRouting("vignetteSize", clampd(ef.lo, 0, 1), clampd(ef.hi, 0, 1), LevelPhrase);
        slotSeq(b, c.imageBeats, 100, "all", c.bars);
        P.get("edgeCurve"); P.get("edgeWander"); P.get("edgeWanderRate");
        if(P.num("edgeWander", 0) > 0) b.approx("edge fade wandering along pitch becomes a straight fade");
        add(b);
    }
    // noise mask
    if(P.has("maskAmount") && P.num("maskAmount") > 0){
        Builder b(c, R, "filterfx", name);
        // v1 noise mask: perlin over cells of maskScale note rows, gain, contrast, out, invert, threshold
        b.look("effect", "noise"); b.look("amount", clampd(P.num("maskAmount"), 0, 1));
        b.look("maskInvert", P.num("maskInvert", 0) > 0.5);
        // cell size in rows (of 100): chosen per image in v1 -> a per-image range
        V ms = P.get("maskScale");
        b.look("noiseScale", clampd(ms.v / 100.0, 0.002, 1));
        if(ms.perImage()) b.randomRouting("noiseScale", clampd(ms.lo / 100.0, 0.002, 1), clampd(ms.hi / 100.0, 0.002, 1), LevelPhrase);
        double con = clampd(P.num("maskContrast", 1), 0.1, 16);
        b.look("noiseContrast", con);
        // v1: (n x gain)^contrast x out; v2 has one gain before the power: gain x out^(1 / contrast)
        // Processing's noise() stays within about 0.3..0.7 where v2's value noise spans 0..1: 0.7 x the gain puts the brightest cells at v1's level
        b.look("noiseGain", clampd(0.7 * P.num("maskGain", 1) * std::pow(std::max(1e-6, P.num("maskOut", 1)), 1.0 / con), 0, 4));
        b.look("noiseThreshold", clampd(P.num("maskThreshold", 0), 0, 1));
        slotSeq(b, c.imageBeats, 100, barsMode, c.bars);
        P.get("maskScale"); P.get("maskContrast"); P.get("maskGain"); P.get("maskOut"); P.get("maskThreshold");
        add(b);
    }
    // repeat / loop
    if(active({"repeatChance", "repeatLength", "repeatCount"}) && pct("repeatChance", 100) > 0){
        Builder b(c, R, "timefx", name);
        b.look("effect", "repeat");
        V L = P.get("repeatLength"), n = P.get("repeatCount");
        b.look("repeats", clampi(n.v, 1, 16));
        if(n.spread()) b.randomRouting("repeats", n.lo, n.hi, LevelEvent);
        slotSeq(b, L.v * 4, pct("repeatChance", 100), barsMode, c.bars);
        if(P.has("repeatPitch")) b.unmapped("repeat pitch shift");
        P.get("repeatGate"); P.get("repeatStart"); P.get("repeatAt"); P.get("repeatSource"); P.get("repeatSourceSlots"); P.get("repeatSourceAt"); P.get("repeatPitch");
        if(P.has("repeatGate")) b.approx("repeat gate ignored");
        add(b);
    }
    // echo / throws
    if((P.has("echoRepeats") && P.num("echoRepeats") > 0) || P.has("throwChance")){
        Builder b(c, R, "timefx", name);
        b.look("effect", "echo");
        bool thr = P.has("throwChance");
        double reps = thr ? P.num("throwRepeats", 6) : P.num("echoRepeats", 4);
        double fb = thr ? P.num("throwFeedback", 0.6) : P.num("echoFeedback", 0.5);
        V dl = P.get(thr ? "throwDelay" : "echoDelay");
        b.look("echoCount", clampi(reps, 1, 16)); b.look("echoDecay", clampd(fb, 0, 1));
        b.look("echoSpacing", clampd(dl.v * stepBeats, 0.0625, 4));
        if(reps > 16) b.approx("echo repeats " + std::to_string((int)reps) + " capped at 16");
        slotSeq(b, 4, thr ? P.num("throwChance", 100) : 100, barsMode, c.bars);
        P.get("throwSource"); P.get("throwDuck");
        add(b);
    }
    // freeze
    if(P.has("freezeChance") && P.num("freezeChance") > 0){
        // v1: with this chance per bar, hold one column for freezeLength bars from a note onset (or the
        // bar start / a grid slice). v2 freeze holds its slot's first column: a slot of freezeLength bars
        // at a random place in the bar (one per bar at most), not a whole bar as before
        Builder b(c, R, "timefx", name);
        b.look("effect", "freeze");
        V fl = P.get("freezeLength");
        double lenBars = clampd(P.has("freezeLength") ? fl.v : 0.5, 1.0 / 16, 4);
        int per = clampi(1.0 / std::min(1.0, lenBars), 1, 16);
        b.seq("mode", "euclidean"); b.seq("steps", per); b.seq("hits", 1); b.seq("rotation", 0);
        if(per > 1 && P.str("freezeStart", "a note onset") != "the bar start"){
            std::vector<int> rl; for(int i = 0; i < per; i++) rl.push_back(i);
            b.seq("rotationList", rl); b.seq("reroll", true);
        }
        b.seq("noteLength", clampd(lenBars * per, 0.05, 64));
        if(fl.spread()) b.randomRouting("seq.noteLength", clampd(fl.lo * per, 0.05, 64), clampd(fl.hi * per, 0.05, 64), LevelEvent);
        b.seq("chance", clampd(P.num("freezeChance"), 0, 100));
        if(barsMode != "all" && !barsMode.empty()){
            std::string m; for(int bar = 0; bar < c.bars; bar++){ bool on = barsMode == "first" ? bar == 0 : barsMode == "last" ? bar == c.bars - 1 : barsMode == "even" ? bar % 2 == 0 : bar % 2 == 1; m += std::string(bar ? " " : "") + (on ? "1" : "0"); }
            b.seq("barMask", m);
        }
        b.look("slotBeats", clampd(lenBars * 4, 0.25, 16));
        P.get("freezeStart"); P.get("freezeGrid"); P.get("freezeChain"); P.get("freezeChainFirst");
        if(P.has("freezeChain") || P.has("freezeGrid")) b.approx("freeze chains / grids become one hold per bar");
        if(P.str("freezeTo", "freeze length") != "freeze length") b.approx("freeze to the end of the unit becomes the freeze length");
        P.get("freezeTo");
        add(b);
    }
    // reverse
    if(P.has("reverseChance") && P.num("reverseChance") > 0){
        Builder b(c, R, "timefx", name);
        b.look("effect", "reverse");
        double slots = std::max(1.0, P.num("reverseSlots", 8));
        slotSeq(b, 4.0 / slots, P.num("reverseChance"), barsMode, c.bars);
        P.get("reverseOddOnly"); P.get("reverseMode"); P.get("reverseSlotList");
        if(P.has("reverseSlotList") || P.num("reverseOddOnly", 0) > 0.5) b.approx("reverse slot choice approximated by chance");
        add(b);
    }
    // sequence / shuffle
    if(active({"seqSource", "seqOrder", "seqChunk", "seqRandomCount"})){
        Builder b(c, R, "timefx", name);
        b.look("effect", "shuffle");
        V o = P.get("seqOrder");
        b.look("shuffleSlices", clampi(o.raw.is_array() && !o.raw.empty() ? (double)o.raw.size() : 4, 2, 32));
        slotSeq(b, 4, 100, barsMode, c.bars);
        b.approx("ordered sequencing becomes a shuffle of slices");
        P.get("seqSource"); P.get("seqChunk"); P.get("seqGate"); P.get("seqRandomCount");
        add(b);
    }
    // tears
    if(P.num("tearWindow", 0) > 0){
        // v1 tear windows: the cell seen through soft raised-cosine windows (per image): a blob mask
        Builder b(c, R, "filterfx", name);
        V tw = P.get("tearWidth"), th = P.get("tearHeight"), tl = P.get("tearLevel");
        b.look("effect", "blob"); b.look("blob", "custom"); b.look("blobMode", "in"); b.look("blobScope", "phrase");
        b.look("blobPlacement", "zone"); b.look("blobCount", clampi(P.num("tearCount", 1), 1, 32));
        // v1 window: a raised cosine over the width (half level at a quarter of it); the v2 blob is at
        // half level at its size / 2 from the centre: half the v1 width and height
        b.look("blobSizeBeats", clampd(tw.v * c.imageBeats * 0.5, 0.05, 32)); b.look("blobSizeHeight", clampd(th.v * 0.5, 0.01, 1.2));
        b.look("blobIrregularity", 0.0); b.look("blobSoftness", 1.0); b.look("blobAmount", 1.0); (void)tl;
        slotSeq(b, c.imageBeats, 100, "all", c.bars);
        for(auto k : {"tearPlace", "tearX", "tearPowerX", "tearPowerY", "tearColour"}) P.get(k);
        b.approx("tear windows become a soft blob mask");
        add(b);
    } else if(P.has("tearCount") || P.has("tearPlace")){
        Builder b(c, R, "timefx", name);
        b.look("effect", "tears"); b.look("tearAmount", 0.5);
        slotSeq(b, 4, 100, barsMode, c.bars);
        for(auto k : {"tearWindow", "tearPlace", "tearCount", "tearX", "tearWidth", "tearHeight", "tearPowerY"}) P.get(k);
        b.approx("tear geometry approximated");
        add(b);
    }
    // micro stutters
    if(P.has("microChance") && P.num("microChance") > 0){
        Builder b(c, R, "timefx", name);
        b.look("effect", "stutter"); b.look("stutterDiv", 8);
        slotSeq(b, 1, P.num("microChance"), barsMode, c.bars);
        P.get("microWidth");
        add(b);
    }
    // transpose
    if(P.has("transposeChance") && P.num("transposeChance") > 0){
        Builder b(c, R, "timefx", name);
        b.look("effect", "transpose");
        V ts = P.get("transposeSemis");
        double k = c.scaleSize / 12.0;
        b.look("transposeDegrees", clampi(ts.v * k, -14, 14));
        if(ts.spread()) b.randomRouting("transposeDegrees", ts.lo * k, ts.hi * k);
        slotSeq(b, 4, P.num("transposeChance"), barsMode, c.bars);
        b.approx("semitone transposes snapped to scale degrees");
        add(b);
    }
    // silence a slot
    if(P.has("slotChance") && P.num("slotChance") > 0){
        Builder b(c, R, "timefx", name);
        b.look("effect", "blank");
        double len = clampd(P.num("slotLength", 0.25), 1.0 / 64, 1), start = clampd(P.num("slotStart", 0.75), 0, 1);
        int per = clampi(1.0 / len, 1, 64);
        std::string pat((size_t)per, '.');
        pat[(size_t)clampi(start * per, 0, per - 1)] = 'x';
        b.seq("steps", per); b.seq("mode", "pattern"); b.seq("pattern", pat); b.seq("noteLength", 1.0);
        double ch = P.num("slotChance");
        if(ch < 100) b.seq("chance", ch);
        b.look("slotBeats", clampd(4.0 * len, 0.25, 16));
        if(4.0 * len < 0.25) b.approx("slot shorter than a 16th widened");
        add(b);
    }
    // compress and tile the whole image n times (v1 copyCompressed): a time effect over the image
    if(P.has("tileCount") && P.num("tileCount", 0) > 1){
        Builder b(c, R, "timefx", name);
        b.look("effect", "tile"); b.look("repeats", clampi(P.num("tileCount"), 2, 16));
        slotSeq(b, c.imageBeats, 100, "all", c.bars);
        add(b);
    }
    // what v2 has no counterpart for (screen-space / colour)
    if(P.has("mirrorAxis") || P.has("mirrorStrips")) R.unmapped.push_back(name + ": mirror (screen-space)");
    if(P.has("cellCopies") || P.has("cellGrid")) R.unmapped.push_back(name + ": cell copies (pixel cells)");
    if(P.has("saturation") && P.num("saturation", 1) < 1) R.unmapped.push_back(name + ": saturation / grey (colour process)");
    if(P.has("scaleFilter")) R.unmapped.push_back(name + ": scale filter");
    for(auto k : {"mirrorAxis", "mirrorStrips", "mirrorSource", "tileCount", "cellCopies", "cellGrid", "cellDest", "cellWidth", "cellHeight", "cellStretchW", "cellStretchH", "saturation", "scaleFilter", "scaleWidth", "processOn", "processBars", "pageWindow"}) P.get(k);
    if(P.num("pageWindow", 0) > 0){
        // v1 page window: (1 - cos 2 pi x) / 2 ^ power across the image (span: approximated by the image)
        Builder b(c, R, "filterfx", name);
        b.look("effect", "page"); b.look("amount", 1.0); b.look("pagePower", clampd(P.num("pageWindow"), 0, 16));
        slotSeq(b, c.imageBeats, 100, "all", c.bars);
        V sp = P.get("pageSpan");
        if(sp.lo > 0 || sp.hi < 1) b.approx("page span other than the whole image");
        add(b);
    }
    for(auto& k : P.unusedKeys()) R.unmapped.push_back(name + ": process key " + k);
}

// ---------------------------------------------------------------- pulse
inline std::string pulseType(Params& P, const std::string& name, double& certainty){
    std::string arp = P.str("arp", "none");
    V voices = P.get("voices"), offs = P.get("voiceOffsets");
    double nv = offs.raw.is_array() && !offs.raw.empty() ? (double)offs.raw.size() : voices.hi;
    double oct = P.num("octave", 3);
    V parts = P.get("partials");
    std::string cell = P.str("cell", "note");
    certainty = 1.0;
    if(hasWord(name, "bell")) return "bells";
    if(arp != "none" && arp != "walk") return "arpeggio";
    if(hasWord(name, "arp")) return "arpeggio";
    if(hasWord(name, "bass") || hasWord(name, "sub")) return "bass";
    if(hasWord(name, "chord") || hasWord(name, "stab") || hasWord(name, "pad") || hasWord(name, "cluster")) return "chords";
    certainty = 0.85;
    if(nv >= 3) return "chords";
    if(oct <= 2) return "bass";
    if(cell == "additive" && parts.hi >= 16 && oct >= 4) return "bells";
    return "melody";
}

// v1 clearChance: a note / hit first blanks what was drawn under its slot (blankUnder, full height),
// with this chance. v2: a "blank" effect on the layer bus with scope "earlier", listed before the
// instrument (so it is applied before the instrument is drawn), following its hits with the chance
// per hit; clearScope "the repetition" blanks whole pattern repetitions instead (one slot per
// repetition). lenSteps: the hit's length in steps (the v1 default clear length).
inline void addClearBlank(Params& P, MapCtx& c, MapResult& R, const Instance& inst, const std::string& name, double lenSteps){
    double cc = P.num("clearChance", 0);
    if(cc <= 0) return;
    bool rep = P.str("clearScope", "the note") == "the repetition";
    V cl = P.get("clearLength");
    int steps = inst.seq.value("steps", 16), pl = inst.seq.value("patternLength", steps);
    Builder b(c, R, "timefx", name + " clear");
    b.look("effect", "blank"); b.look("fxScope", "earlier");
    b.in.follow = Follow::Off;
    // follows the instrument's hits (so its chances, bar masks and repetition gating carry over);
    // the blank's step grid is the instrument's, so its length is in the same steps
    b.seq("mode", "follow"); b.seq("followId", inst.id); b.seq("followMode", "hits");
    b.seq("steps", clampi(steps, 1, 64));
    if(!rep){
        b.seq("noteLength", clampd(cl.hi > 0 ? cl.v : lenSteps, 0.05, 64));
        if(cl.hi > 0 && cl.spread()) b.randomRouting("seq.noteLength", clampd(cl.lo, 0.05, 8), clampd(cl.hi, 0.05, 8), LevelEvent);
    } else {
        // v1 blanks the whole repetition when it plays: here from each of its hits over a pattern length
        b.seq("noteLength", clampd(pl, 0.05, 64));
        b.approx("clear per repetition becomes a blank of one pattern length from each hit");
    }
    b.seq("chance", clampd(cc, 0, 100));
    b.in.id = uniqueId(c, name + " clear");
    R.instances.push_back(b.in);
}

inline void mapPulse(Params& P, MapCtx& c, MapResult& R, const std::string& name){
    double cert = 1;
    std::string type = pulseType(P, name, cert);
    Builder b(c, R, type, name);
    mapRhythm(P, b);
    double stepBeats = stepBeatsOf(P);
    // ---- look
    std::string cell = P.str("cell", "note");
    b.look("look", cell == "ribbon" ? "ribbon" : cell == "additive" ? "additive" : "note");
    V th = P.get("thickness");
    // v1 draws additive cells with 2 px points whatever the thickness (drawAdditive: strokeWeight 2 x UY)
    if(cell == "additive") b.look("thickness", 2.0), P.get("thickness");
    else b.look("thickness", rangedLook(b, th, "thickness", 1, 1, 80));
    // v1 paints points of the thickness every pixel, and the note twice (y +- detune): each pixel gets
    // about 2 x thickness layers of the same alpha (additive: 2 px points, three lines): 1 - (1 - a)^k
    // measured against v1 renders: the thickness itself (the pair only half overlaps), 3 for additive
    // v1 volume > 1 multiplies the alpha before the 255 clamp: more of the same saturation
    double volK = std::max(1.0, P.num("volume", 1));
    b.look("alphaStack", clampd((cell == "additive" ? 2.5 : (1 + std::max(1.0, th.v)) / 2) * volK, 1, 8));
    V parts = P.get("partials");
    double skew = std::max(0.0, P.num("partialsSkew", 1));
    double pm = parts.lo + (parts.hi - parts.lo) / (skew + 1);
    if(parts.hi > 160) b.approx("partials up to " + std::to_string((int)parts.hi) + " capped at 160");
    b.look("partials", clampi(pm, 0, 160));
    if(parts.perImage()) b.randomRouting("partials", parts.lo, std::min(160.0, parts.hi));
    // a per-note range: each note its own count (v1 lo + random^skew x range); a uniform draw with
    // the same mean (its top at lo + 2 x range / (skew + 1)), so some notes reach high as in v1
    else if(parts.form == 1 && parts.spread()) b.randomRouting("partials", parts.lo, std::min(160.0, parts.lo + 2 * (parts.hi - parts.lo) / (skew + 1)), LevelEvent);
    std::string env = P.str("envelope", "saw");
    double envPow = P.num("envPower", 0);
    // v1 envelope along the note (5.15): same shape and power, no tail
    b.look("envShape", envPow <= 0 ? "hold" : env == "isaw" ? "rise" : env == "cos" ? "swell" : "decay");
    b.look("envPower", clampd(envPow, 0, 16));   // volume > 1 (alpha saturation) goes into the overlap gain below
    b.look("attack", 0.0); b.look("release", 0.0);
    double hic = P.num("hicut", 0);
    b.look("partialCut", clampd(hic, 0, 8)); b.look("partialDecay", 0.0); b.look("partialThin", 0.0); b.look("softness", 0.1);
    // v1 draws every partial twice, at y +- detune rows (2 x detune semitones apart); v2's two detune
    // voices sit +- detune / 2 cents, so the v2 value is 200 x the v1 rows
    V det = P.get("detune");
    if(det.hi > 0) b.look("detune", rangedLook(b, det, "detune", 200, 0, 100)), b.look("detuneVoices", 2);
    // v1 partialDetune: each partial drawn random(partDet) rows lower (fixed per note); v2 partialDetune in cents, the same draw
    V pdet = P.get("partialDetune");
    if(pdet.hi > 0) b.look("partialDetune", rangedLook(b, pdet, "partialDetune", 100, 0, 200));
    V vib = P.get("vibrato"), vc = P.get("vibCycles");
    V len = P.get("length"), lenImg = P.get("lengthImage");
    double lenSteps = lenImg.hi > 0 ? lenImg.v * c.imageBeats / stepBeats : len.v;
    if(vib.hi > 0 && vc.v > 0) b.lfoRouting("pitchFine", -vib.v * 100, vib.v * 100, std::max(0.0625, lenSteps * stepBeats / vc.v), "sine");
    V trem = P.get("tremolo"), tc = P.get("tremCycles");
    if(trem.hi > 0 && tc.v > 0) b.lfoRouting("brightness", 1 - trem.v, 1, std::max(0.0625, lenSteps * stepBeats / tc.v), "sine");
    // amplitude lfo along each note (lfoShape / lfoCycles / lfoPower): a drawn brightness lfo, the power as its curve
    V lfoPow = P.get("lfoPower"), lfoCyc = P.get("lfoCycles");
    if(lfoPow.v > 0 && lfoCyc.v > 0){
        std::string ls = P.str("lfoShape", "saw");
        // v1 shapes: saw 1 - ph, isaw ph, cos / icos (1 - cos 2 pi ph) / 2; v1 raises it to
        // 1 + partial * power, so the fundamental stays linear: curve 1
        std::string sh = ls == "perlin" ? "smoothRandom" : (ls == "saw" || ls == "isaw") ? "saw" : "sine";
        if(b.lfoRouting("brightness", 0, 1, std::max(0.0625, lenSteps * stepBeats / lfoCyc.v), sh)){
            Routing& r = b.in.routings.back();
            r.curve = 1; r.invert = ls == "saw";
            if(sh == "sine") r.sourceParams["phase"] = 0.75;
            // v1's phase starts with each note: a drawn note envelope, 16 points per cycle
            if(ls != "perlin" && lfoCyc.v <= 32){
                json pts = json::array();
                int np = std::max(16, (int)std::ceil(16 * lfoCyc.v));
                for(int q = 0; q <= np; q++){
                    double x = q / (double)np, ph = x * lfoCyc.v, f = ph - std::floor(ph);
                    if(q == np && f < 1e-9 && ph > 0) f = 1;   // the cycle's last point
                    pts.push_back(ls == "saw" ? 1 - f : ls == "isaw" ? f : 0.5 - 0.5 * std::cos(2 * M_PI * ph));
                }
                r.source = "noteEnv" + r.source.substr(3); r.invert = false;
                r.sourceParams = json{{"shape", "drawn"}, {"points", pts}};
            }
        }
        // v1 raises the lfo to 1 + partial / partials x lfoPower: higher partials fade faster (partialEnvPower)
        b.look("partialEnvPower", clampd(lfoPow.v, 0, 64));
        if(lfoPow.perImage()) b.randomRouting("partialEnvPower", clampd(lfoPow.lo, 0, 64), clampd(lfoPow.hi, 0, 64), LevelPhrase);
        if(ls == "perlin" || lfoCyc.v > 32) b.approx("amplitude lfo along the note becomes a brightness lfo in beats");
    }
    double vol = P.num("volume", 1);
    b.look("level", clampd(vol, 0.1, 1));
    b.look("colourRole", colourRole(P.str("colour", "white"), type));
    // ---- note length (in steps)
    double nl = clampd(lenSteps, 0.05, 64);
    if(lenSteps > 64) b.approx("note length " + std::to_string(lenSteps).substr(0, 5) + " steps capped at 64");
    if(P.num("legato", 0) > 0.5) b.seq("legato", true);
    b.seq("noteLength", nl);
    if(lenImg.hi <= 0 && len.spread()) b.randomRouting("seq.noteLength", clampd(len.lo, 0.05, 8), clampd(len.hi, 0.05, 8), LevelEvent);
    V glide = P.get("glide");
    if(std::fabs(glide.lo) + std::fabs(glide.hi) > 0) b.seq("glide", 0.5), b.approx("glide in semitones becomes a glide time");
    // ---- pitch
    std::string arp = P.str("arp", "none");
    // a v1 degree (library scale index) -> v2 degrees of the folded scale
    auto v2deg = [&](int d){
        if(c.v1Scale.empty() || c.v2Scale.empty()) return d;
        int n = (int)c.v1Scale.size(), o = (int)std::floor((double)d / n);
        int semis = c.v1Scale[(size_t)(d - o * n)] + 12 * o;
        int so = (int)std::floor(semis / 12.0);
        return snapDegree(semis, c.v2Scale) + so * (int)c.v2Scale.size();
    };
    V deg = P.get("degree"), oct = P.get("octave"), voices = P.get("voices"), vo = P.get("voiceOctaves");
    int v2oct = clampi(oct.v - 2, -4, 4);
    b.seq("octave", v2oct);
    if(oct.perImage()) b.randomRouting("seq.octave", oct.lo - 2, oct.hi - 2);
    std::string chordStart = P.str("chordStart", "the pattern's degree");
    std::string voicing = P.str("voicing", "stacked");
    double nvMean = std::max(1.0, voices.v);
    if(type == "arpeggio"){
        b.seq("pitchSource", "chord"); b.seq("notesPerStep", 1);
        b.seq("voices", clampi(std::max(3.0, P.num("arpSteps", 4)), 1, 8));
        std::string order = arp == "down" ? "down" : (arp == "updown" || arp == "downup") ? "updown" : arp == "random" ? "random" : "up";
        b.seq("order", order);
        int as = clampi(P.num("arpStep", 1), 0, 8);
        if(as > 1) b.seq("advance", as);
        if(P.num("arpSteps", 4) > 8) b.approx("arp of " + std::to_string((int)P.num("arpSteps", 4)) + " steps folded into 8 voices");
    } else if(type == "chords" || nvMean >= 2){
        b.seq("pitchSource", "chord");
        b.seq("voices", clampi(nvMean, 1, 8));
        if(voices.perImage()) b.randomRouting("seq.voices", voices.lo, std::min(8.0, voices.hi));
        // v1 scattered: each voice a random degree of the octave (v2 voicing "scattered")
        b.seq("voicing", voicing == "scattered" ? "scattered" : vo.hi > 0 ? "open" : "close");
        if(chordStart == "the pattern's degree" && !deg.spread() && std::lround(deg.v) != 0) b.seq("degreeOffset", clampi(v2deg((int)std::lround(deg.v)), -24, 24));
        if(P.has("voiceOffsets")) b.approx("voice offsets " + P.p["voiceOffsets"].dump() + " become chord voices");
    } else if(arp == "walk"){
        b.seq("pitchSource", "scale"); b.seq("scaleMode", "walk");
        b.seq("walkStep", clampi(P.num("arpStep", 1), 1, 7)); b.seq("range", clampi(c.scaleSize * 2.0, 1, 36));
    } else if(P.num("riffChance", 0) > 0){
        b.seq("pitchSource", "scale"); b.seq("scaleMode", "riff");
        b.seq("riffLength", clampi(P.num("patternSteps", 4), 1, 16));
        double rr = P.num("riffRange", 0);
        b.seq("range", clampi(rr > 0 ? rr : c.scaleSize, 1, 36));
        if(P.num("riffChance", 0) < 100) b.approx("riff chance " + std::to_string((int)P.num("riffChance")) + "% becomes an always-on riff");
    } else if(chordStart == "a random degree" || voicing == "scattered"){
        b.seq("pitchSource", "scale"); b.seq("scaleMode", "random"); b.seq("range", clampi(c.scaleSize, 1, 36));
    } else if(deg.spread()){
        b.seq("pitchSource", "scale"); b.seq("scaleMode", "random");
        b.seq("range", clampi(deg.hi - deg.lo + 1, 1, 36));
        if(deg.lo > 0) b.seq("degreeOffset", clampi(v2deg((int)std::lround(deg.lo)), -24, 24));
    } else {
        b.seq("pitchSource", "root");
        if(std::lround(deg.v) != 0) b.seq("degreeOffset", clampi(v2deg((int)std::lround(deg.v)), -24, 24));
    }
    b.in.follow = Follow::Chord;
    // v1 octaveUp: each pattern step an octave up with this chance, drawn once per image (v2 seq octaveChance)
    if(P.num("octaveUp", 0) > 0) b.seq("octaveChance", clampd(P.num("octaveUp"), 0, 100));
    if(type == "chords" && P.has("voiceVolume")) b.look("voiceFade", clampd(1 - P.get("voiceVolume").lo, 0, 1));
    // read and judged elsewhere / no counterpart
    for(auto k : {"partialMode", "locut", "lfoShape", "lfoCycles", "lfoPower", "anchor", "transposeAlongNote", "followRoot",
                  "chordStep", "chordPerStep", "arpStart", "walkMin", "walkLoop", "legatoOverlap", "ribbonGrain", "ribbonMin", "ribbonPower",
                  "partialSpan", "partialAlign", "partialLfoCycles", "partialTaper", "additiveTilt", "voiceVolume", "riffRange", "arpSteps", "arpStep"}) P.get(k);
    // v1 partial modes as general partial settings: "even" (harmonics 1, 3, 5 ...) = spread 2;
    // "random" / "thin" (50 % / 20 % of partials) = partial count; "random amps" = steeper roll-off
    // "random" / "thin": every partial (fundamental included) drawn with a 50 % / 20 % chance, decided per
    // note = partialChance; several modes chosen per image = a per-image range of the chance
    std::string pmode = P.str("partialMode", "all");
    V pmV = P.get("partialMode");
    std::vector<std::string> pmodes;
    if(pmV.form == 3 && pmV.raw.is_object()) for(auto& o : pmV.raw["options"]) if(o.is_string()) pmodes.push_back(o.get<std::string>());
    if(pmodes.empty()) pmodes.push_back(pmode);
    auto chanceOf = [](const std::string& m){ return m == "random" ? 0.5 : m == "thin" ? 0.2 : 1.0; };
    double cmin = 1, cmax = 0, csum = 0;
    for(auto& m : pmodes){ double ch = chanceOf(m); cmin = std::min(cmin, ch); cmax = std::max(cmax, ch); csum += ch; }
    if(cmin < 1){
        b.look("partialChance", csum / pmodes.size());
        if(cmax > cmin){ b.randomRouting("partialChance", cmin, cmax, LevelPhrase); b.approx("partial modes chosen per image become a per-image partial chance range"); }
    }
    if(pmode == "even") b.look("partialSpread", 2.0);
    else if(pmode == "random amps") b.look("partialCut", clampd(P.num("hicut", 0) / std::max(1.0, P.num("volume", 1)) + 0.5, 0, 8));
    if(pmode == "even" || pmode == "random amps") b.approx("partial mode '" + pmode + "' as partial spread / roll-off");
    for(auto k : {"anchor", "partialLfoCycles", "partialSpan"})
        if(P.has(k)) b.unmapped(std::string(k) + " (pixel-level note drawing)");
    b.in.id = uniqueId(c, name);
    c.prevId = b.in.id;
    addClearBlank(P, c, R, b.in, name, nl);   // listed before the note: blanks under it first
    for(auto k : {"clearScope", "clearLength"}) P.get(k);
    R.instances.push_back(b.in);
    R.confidence *= cert;
}

// ---------------------------------------------------------------- hits
inline void mapHits(Params& P, MapCtx& c, MapResult& R, const std::string& name){
    std::string cell = P.str("cell", "noise band");
    V bl = P.get("bandLow"), bh = P.get("bandHigh");
    double lo = clampd(std::min(bl.v, bh.v), 0, 1), hi = clampd(std::max(bl.v, bh.v), 0, 1);
    if(hi - lo < 0.01) hi = std::min(1.0, lo + 0.02);
    double stepBeats = stepBeatsOf(P);
    V len = P.get("length");
    double lenBeats = len.v * stepBeats;
    double cert = 0.9;
    std::string type;
    if(cell == "kick sweep" || cell == "taiko" || hasWord(name, "kick")) type = "kick", cert = 1;
    else if(hasWord(name, "hat") || hasWord(name, "tick")) type = "hats", cert = 1;
    else if(hasWord(name, "snare") || hasWord(name, "clap")) type = "snare", cert = 1;
    else if(hi <= 0.3) type = "kick";
    else if(cell == "lfo band" || (lenBeats >= 2 && (cell == "noise band" || lenBeats > 8))) type = "texture";
    else if((cell == "speckle" || cell == "dot grid") && lo >= 0.5) type = "hats";
    else if(lo >= 0.65 && lenBeats <= 0.5) type = "hats";
    else type = "snare";
    Builder b(c, R, type, name);
    mapRhythm(P, b);
    // level 0 in v1 is a hit that draws nothing (used with clearChance: it only blanks under itself)
    V lvl = P.get("level");
    double levelV = lvl.has && lvl.v <= 0 ? 0 : lvl.v > 0 ? lvl.v : 1, levelMul = 1;
    if(cell == "noise band"){
        // v1: alpha = (noise x noiseGain)^contrast, x 2 inside the vertical window, and the level only
        // applies with the window (or levelAlways); v2 draws (noise x 1.33)^contrast x level
        double ng = P.num("noiseGain", 1.33), ct = std::max(0.0, P.num("contrast", 4));
        bool win = P.num("window", 0) > 0.5;
        // the window doubles the alpha but fades it to the band's edges: about the same light overall
        levelMul = std::pow(ng / 1.33, ct);
        if(!win && P.num("levelAlways", 0) < 0.5) levelV = 1;
        if(win) b.approx("noise band window (vertical fade) drawn flat");
        P.get("window"); P.get("levelAlways"); P.get("noiseGain");
    }
    b.look("level", clampd(levelV * levelMul, 0, 1));
    if(lvl.perImage() && levelV > 0) b.randomRouting("level", clampd(lvl.lo * levelMul, 0, 1), clampd(lvl.hi * levelMul, 0, 1));
    b.look("colourRole", colourRole(P.str("colour", "white"), type));
    std::string envl = P.str("noiseEnvelope", "none");
    double envPow = std::max(0.0, P.num("envPower", 1));
    // v1 saw-out envelopes fall as (1 - u)^power: about 1 / (power + 1) of the length is bright
    // the v2 hit envelope is (1 - u)^k with k = width / decay - 1, as v1's power envelopes
    double decayFrac = envl == "saw out" ? 1.0 / (1.0 + envPow) : envl == "tri" ? 0.5 : 1.0;
    if(cell == "kick sweep") decayFrac = 1.0 / (1.0 + 2 * std::max(0.1, P.num("kickPower", 2)));
    else if(cell == "taiko") decayFrac = 0.1;
    else if(cell == "block") decayFrac = 1.0;
    else if(cell == "gradient") decayFrac = 1.0 / (1.0 + std::max(0.0, P.num("gradPowW", 1)));
    else if(cell == "klank" || cell == "soft klank") decayFrac = 1.0 / (1.0 + std::max(0.0, P.num("klankPower", 12)));
    if(type == "kick" || type == "snare"){
    b.look("contrast", clampd(P.num("contrast", 4), 0.25, 12));
    b.look("grainSize", clampd(1.0 / std::max(0.005, P.num("noiseScale", 0.05)), 1, 200));
    b.look("bandShape", clampd(cell == "gradient" ? P.num("gradPowH", 1) : P.num("blockPower", 1), 0, 8));
    }
    V contrast = P.get("contrast");
    V hj = P.get("heightJitter"), bj = P.get("bandJitter");
    if(type == "kick"){
        b.look("cell", cell == "block" ? "block" : cell == "gradient" ? "gradient" : "taiko");
        b.look("heightLow", lo); b.look("heightHigh", hi);
        b.look("width", clampd(lenBeats, 0.05, 8)); b.look("decay", clampd(lenBeats * decayFrac, 0.01, 4));
        b.look("sweep", cell == "kick sweep" ? 1.0 : 0.0);
    } else if(type == "snare"){
        std::string sc = cell == "block" ? "block" : cell == "gradient" ? "gradient" : (cell == "speckle" || cell == "dot grid") ? "speckle"
                       : (cell == "klank" || cell == "soft klank") ? "burst" : "noiseBand";
        b.look("cell", sc);
        b.look("bandLow", lo); b.look("bandHigh", hi);
        b.look("width", clampd(lenBeats, 0.05, 8)); b.look("decay", clampd(lenBeats * decayFrac, 0.01, 4));
        b.look("grain", (cell == "block" || cell == "gradient" || cell == "klank" || cell == "soft klank") ? 0.0 : 1.0);
        if(lenBeats > 8) b.approx("hit length " + std::to_string(lenBeats).substr(0, 4) + " beats capped at 8");
    } else if(type == "hats"){
        std::string hc = cell == "dot grid" ? "dot" : cell == "speckle" ? "speckle" : cell == "block" ? "block" : cell == "gradient" ? "line" : "tick";
        b.look("cell", hc);
        b.look("heightLow", lo); b.look("heightHigh", hi);
        b.look("width", clampd(lenBeats, 0.01, 1)); b.look("decay", clampd(lenBeats * decayFrac, 0.005, 2));
        b.look("scatter", clampd((hj.hi + bj.hi) * 2, 0, 1));
    } else {
        std::string tc = cell == "lfo band" ? "stripes" : cell == "gradient" ? "cloud" : (cell == "speckle" || cell == "dot grid") ? "grain" : "noise";
        b.look("cell", tc);
        b.look("bandLow", lo); b.look("bandHigh", hi);
        b.look("contrast", clampd(contrast.v / 4, 0, 2));
        b.look("grainSize", clampd(1.0 / std::max(0.005, P.num("noiseScale", 0.05)) / 3, 0.5, 64));
        b.look("density", 1.0); b.look("softness", 0.0);
        b.look("fade", envl == "none" ? 0.05 : 0.4);
        b.seq("noteLength", clampd(len.v, 0.05, 8));
        if(len.v > 8) b.approx("texture length capped at 8 steps");
        if(cell == "lfo band") b.look("drift", 0.6), b.approx("lfo band becomes a drifting stripe texture");
    }
    if(type != "texture" && len.spread()) b.randomRouting("width", len.lo * stepBeats, std::min(8.0, len.hi * stepBeats), LevelEvent);
    // v1 bandJitter moves the whole band up or down per hit; heightJitter extends its bottom down
    // (before, both became one position offset, which pushed high bands off the image)
    {
        std::string lowKey = (type == "kick" || type == "hats") ? "heightLow" : "bandLow";
        if(bj.hi > 0) b.randomRouting("height", -bj.hi, bj.hi, LevelEvent);
        if(hj.hi > 0) b.randomRouting(lowKey, clampd(lo - hj.hi, 0, 1), lo, LevelEvent);
    }
    b.in.follow = Follow::Off;
    for(auto k : {"window", "envPower", "noiseScale", "noiseGain", "noiseAxis", "blockPower", "gradEnd", "gradPowW", "gradPowH", "klankPower", "kickPower",
                  "feather", "lfoBandCycles", "lfoBandAmount", "lfoBandPower", "levelAlways", "bandPick"}) P.get(k);
    for(auto k : {"gradEnd", "noiseAxis", "lfoBandCycles", "strips", "burstCount"})
        if(P.has(k)) b.unmapped(std::string(k) + " (pixel-level cell drawing)");
    if(P.has("lengthImage")) b.approx("length as a fraction of the image");
    b.in.id = uniqueId(c, name);
    c.prevId = b.in.id;
    addClearBlank(P, c, R, b.in, name, len.v);   // listed before the hit: blanks under it first
    for(auto k : {"clearScope", "clearLength"}) P.get(k);
    R.instances.push_back(b.in);
    R.confidence *= cert;
}

// ---------------------------------------------------------------- masses
inline void mapMasses(Params& P, MapCtx& c, MapResult& R, const std::string& name){
    Builder b(c, R, "mass", name);
    V lines = P.get("lines");
    b.look("strands", rangedLook(b, lines, "strands", 1, 1, 64, true));
    if(lines.hi > 64) b.approx("lines above 64 capped");
    b.look("lineWidth", clampd(P.num("thickness", 1), 0.5, 40)); b.look("fade", 0.0); b.look("softness", 0.1);
    b.look("strandJitter", 0.0);   // v1 strands start together (no seams between segments)
    std::string curve = P.str("curve", "bezier");
    // v1 "glide" = portamentoPG's sigmoid: tension near 1 holds, steps at mid-segment, holds
    b.look("glideShape", curve == "glide" ? "snap" : "sCurve");
    if(curve == "glide"){
        V gt = P.get("glideTension");
        b.look("glideTension", clampd(gt.v, 0, 0.999));
        if(gt.spread()) b.randomRouting("glideTension", clampd(gt.lo, 0, 0.999), clampd(gt.hi, 0, 0.999), gt.perImage() ? LevelPhrase : LevelEvent);
    }
    double os = P.num("octaveStart", 2), orng = P.num("octaveRange", 3);
    bool harm = P.str("pitchMode", "") == "harmonics of the root";
    // v1 lines sit at pitches across the octave band: strands spread over the band (no band-wide glide)
    double lo = clampd((12 * os + 1) / 100.0, 0, 1), hi = clampd((12 * (os + orng)) / 100.0, 0, 1);
    if(!harm){ b.look("heightFrom", 0.5 * (lo + hi)); b.look("heightTo", 0.5 * (lo + hi)); }
    b.look("spread", harm ? clampd(orng / 8.0, 0, 1) : hi - lo);
    b.look("wobble", clampd(P.num("wobble", 0) * 0.3, 0, 1));
    V cp = P.get("lineCopies");
    if(cp.v > 1){ int nv = clampi(cp.v, 1, 3); b.look("detuneVoices", nv); b.look("detune", clampd(P.get("copyDetune").v * 100 * (nv == 2 ? 2 : 1), 0, 100)); }
    b.look("colourRole", colourRole(P.str("colour", "white"), "mass"));
    // segments: one event per segment
    V seg = P.get("segmentBars");
    double sb = std::max(0.25, seg.v);
    if(sb <= 1){ b.seq("steps", clampi(1.0 / sb, 1, 64)); b.seq("mode", "pattern"); b.seq("pattern", "x"); b.seq("noteLength", 1.0); }
    else { int n = clampi(sb, 1, 16); b.seq("steps", 1); b.seq("mode", "pattern"); b.seq("pattern", "x" + std::string((size_t)(n - 1), '.')); b.seq("patternLength", n); b.seq("noteLength", (double)std::min(8, n)); }
    if(seg.perImage()) b.approx("segment length chosen per image: most likely kept");
    if(P.num("holdChance", 0) > 0) b.approx("hold chance");
    if(harm){
        // harmonics of the root: one strand per harmonic, heights from the sequencer
        b.look("strands", 1); b.look("spread", 0.0);
        V hc = P.get("harmonics");
        b.seq("massPitch", "harmonics");
        b.seq("harmonics", clampi(hc.v, 1, 64));
        if(hc.perImage()) b.randomRouting("seq.harmonics", hc.lo, std::min(64.0, hc.hi));
        b.seq("harmonicStep", clampi(P.num("harmonicStep", 1), 1, 4));
        b.seq("harmonicFade", P.str("harmonicsFade", "power") == "power" ? clampd(P.get("harmonicsPower").v, 0, 8) : 0.5);
        b.seq("octave", clampi(os - 2, -4, 4));
        double off = P.num("segmentOffset", 0);   // v1: first segment boundary at `off` bars
        if(off > 0 && off < sb) b.seq("phaseShift", clampd(off * 4, 0, 16));
        if(P.num("harmonicsTaper", 0) > 0){ b.seq("harmonicTaper", clampd(P.num("harmonicsTaper", 0), 0, 1)); b.seq("harmonicTaperPower", clampd(P.num("harmonicsTaperPower", 1), 0.1, 8)); }
    }
    b.in.follow = (harm || P.num("followHarmony", 1) > 0.5) ? Follow::Chord : Follow::Off;
    for(auto k : {"glideTension", "harmonics", "harmonicsFade", "harmonicsPower", "harmonicStep", "harmonicsTaper", "harmonicsTaperPower", "segmentOffset",
                  "vibrato", "chordChance", "tension", "convergeChance", "convergeNote", "dash", "dashGap", "dashChance", "chordStep", "wobbleFreq", "holdChance", "pitchMode"}) P.get(k);
    for(auto k : {"dash", "convergeChance", "chordChance"}) if(P.has(k)) b.unmapped(std::string(k));
    if(!harm && P.has("segmentOffset")) b.unmapped("segmentOffset");
    b.in.id = uniqueId(c, name);
    R.instances.push_back(b.in);
    R.confidence *= 0.9;
}

// ---------------------------------------------------------------- sweep
inline void mapSweep(Params& P, MapCtx& c, MapResult& R, const std::string& name){
    Builder b(c, R, "sweep", name);
    double fl = P.get("fromLow").v, fh = P.get("fromHigh").v, tl = P.get("toLow").v, thv = P.get("toHigh").v;
    b.look("bottomFrom", clampd(fl, 0, 1)); b.look("topFrom", clampd(fh, 0, 1));
    b.look("bottomTo", clampd(tl, 0, 1)); b.look("topTo", clampd(thv, 0, 1));
    double os = P.num("opStart", 0), oe = P.num("opEnd", 1), lv = P.num("level", 1);
    b.look("levelFrom", clampd(os * lv, 0, 1)); b.look("levelTo", clampd(oe * lv, 0, 1));
    b.look("curve", clampd(P.num("powW", 1), 0.1, 10));
    b.look("bandShape", clampd(P.num("powH", 1), 0, 8)); b.look("softness", 0.0);
    double mid0 = (fl + fh) * 0.5, mid1 = (tl + thv) * 0.5;
    b.look("shape", mid1 > mid0 + 0.05 ? "rise" : mid1 < mid0 - 0.05 ? "fall" : "free");
    b.look("colourRole", colourRole(P.str("colour", "white"), "sweep"));
    // placement: start fraction of the image, length in bars
    V st = P.get("start"), ln = P.get("length"), cnt = P.get("count");
    int count = clampi(cnt.v, 1, 16);
    bool fine = ln.v < 2;
    int perBar = fine ? 4 : 1;
    int slots = std::min(64, c.bars * perBar);
    std::string pat((size_t)slots, '.');
    for(int i = 0; i < count; i++){
        double f = st.lo + (st.hi - st.lo) * (i + 0.5) / count;
        if(count == 1) f = st.lo;
        pat[(size_t)clampi(f * slots, 0, slots - 1)] = 'x';
    }
    b.seq("steps", perBar); b.seq("mode", "pattern"); b.seq("pattern", pat); b.seq("patternLength", slots);
    // v1 clips the sweep at the image end and spreads its gradient over what is left
    double lenSteps = std::min(ln.v, c.bars * (1 - st.lo)) * perBar;
    b.seq("noteLength", clampd(lenSteps, 0.05, 64));
    if(lenSteps > 64) b.approx("sweep length capped at 64 steps");
    b.seq("level", 1.0);   // v1: the sweep level only
    if(st.spread() && count == 1) b.approx("start chosen per image: earliest kept");
    P.get("powH");
    if(P.has("powH")) b.unmapped("band profile power");
    b.in.follow = Follow::Off;
    b.in.id = uniqueId(c, name);
    R.instances.push_back(b.in);
}

// ---------------------------------------------------------------- shapes
inline void mapShapes(Params& P, MapCtx& c, MapResult& R, const std::string& name){
    Builder b(c, R, "shapes", name);
    std::string sh = P.str("shape", "ellipse");
    std::string v2 = (sh == "line" || sh == "dashed line" || sh == "trace" || sh == "flow") ? "line" : (sh == "curve" || sh == "sine") ? "curve"
                   : (sh == "ellipse" || sh == "blob") ? "ellipse" : sh == "triangle" ? "triangle" : sh == "rect" ? "rect" : sh == "letter" ? "letter" : "rect";
    if(v2 != sh) b.approx("shape '" + sh + "' drawn as " + v2);
    b.look("shape", v2);
    bool closed = v2 == "rect" || v2 == "ellipse" || v2 == "triangle" || v2 == "letter";
    b.look("filled", closed && P.str("paint", "fill") == "fill");
    V thk = P.get("thickness");
    b.look("strokeWidth", clampd(thk.v * 10.8, 0.5, 40));   // v1 thickness is in note rows (1080 / 100 px)
    std::string so = P.str("sizeOf", "min side");
    double sf = (so == "max side" || so == "width") ? 1.78 : so == "edge distance" ? 0.5 : 1.0;
    V sz = P.get("size");
    double hs = clampd(sz.v * sf, 0.01, 1);
    b.look("heightSize", hs);
    if(sz.spread()) b.randomRouting("heightSize", clampd(sz.lo * sf, 0, 1), clampd(sz.hi * sf, 0, 1), LevelEvent);
    b.look("shapeLength", clampd(hs * c.imageBeats * 1080.0 / 1920.0, 0.05, 16));
    b.randomRouting("heightCenter", 0.15, 0.85, LevelEvent);
    V rot = P.get("rotation");
    b.look("rotation", clampd(rot.v, -180, 180));
    if(rot.spread()) b.randomRouting("rotation", rot.lo, rot.hi, LevelEvent);
    // v1 "two points" lines join two random points: any angle
    else if((sh == "line" || sh == "dashed line") && P.str("lineMode", "two points") == "two points") b.randomRouting("rotation", -90, 90, LevelEvent);
    b.look("colourRole", colourRole(P.str("colour", "palette per note"), "shapes"));
    V cnt = P.get("count"), ch = P.get("chance");
    double expected = cnt.v * (P.has("chance") ? ch.v / 100 : 1);
    b.seq("mode", "probability"); b.seq("steps", 4);
    b.seq("density", clampd(expected / std::max(1.0, c.bars * 4.0), 0.01, 1));
    b.approx("shape placement (candidates, overlap, margins) becomes a probability rhythm with random heights");
    for(auto k : {"margin", "overlap", "attempts", "alpha", "gap", "lineMode", "minSize", "cap", "blend", "cycles", "amplitude", "rotationChance", "aspect", "aspectBias",
                  "corner", "particles", "flowSteps", "flowScale", "flowStrength", "letters", "thicknessNoise", "wander", "drift", "wobble", "symmetryChance", "dash", "dashGap", "spread", "bend", "paint"}) P.get(k);
    for(auto k : {"particles", "flowSteps", "symmetryChance", "dash", "cycles"}) if(P.has(k)) b.unmapped(k);
    b.in.follow = Follow::Off;
    b.in.id = uniqueId(c, name);
    R.instances.push_back(b.in);
}

// ---------------------------------------------------------------- one generator call
inline void mapGen(const std::string& gen, const json& params, const json* process, const Library& lib, MapCtx& c, MapResult& R, const std::string& name, int depth = 0);

inline void mapStack(const json& preset, const Library& lib, MapCtx& c, MapResult& R, const std::string& name, int depth){
    if(depth > 3 || !preset.contains("layers")){ R.unmapped.push_back(name + ": empty or recursive stack"); return; }
    std::string pick = preset.value("pick", "");
    double wsum = 0; for(auto& l : preset["layers"]) wsum += l.value("weight", 1.0);
    std::map<int, double> gw;
    for(auto& l : preset["layers"]) gw[l.value("group", 0)] = std::max(gw[l.value("group", 0)], l.value("weight", 1.0));
    double gsum = 0; for(auto& kv : gw) gsum += kv.second;
    int li = 0;
    for(auto& l : preset["layers"]){
        size_t before = R.instances.size();
        std::string sub = name + " " + std::to_string(++li);
        if(l.contains("preset")){
            std::string key = l["preset"].get<std::string>();
            auto f = lib.presets.find(key);
            if(f == lib.presets.end()){ R.unmapped.push_back(name + ": missing preset " + key); continue; }
            const json& pj = f->second;
            std::string g = pj.value("gen", "");
            if(g == "stack") mapStack(pj, lib, c, R, sub, depth + 1);
            else mapGen(g, pj.value("params", json::object()), pj.contains("process") ? &pj["process"] : nullptr, lib, c, R, sub, depth + 1);
        } else {
            mapGen(l.value("gen", ""), l.value("params", json::object()), l.contains("process") ? &l["process"] : nullptr, lib, c, R, sub, depth + 1);
        }
        double chance = l.contains("chance") ? numOf(l["chance"], 100) : 100;
        if(pick == "one") chance *= l.value("weight", 1.0) / std::max(1e-9, wsum);
        else if(pick == "group") chance *= gw[l.value("group", 0)] / std::max(1e-9, gsum);
        if(chance < 100 - 1e-9){
            for(size_t i = before; i < R.instances.size(); i++){
                Instance& in = R.instances[i];
                if(in.seq.contains("chance")){ in.seq["chance"] = clampd(in.seq["chance"].get<double>() * chance / 100, 0, 100); }
                else { in.seq["chance"] = clampd(chance, 0, 100); in.seq["chanceScope"] = "phrase"; }
                if(pick == "group") in.seq["chanceGroup"] = slug(name) + "_g" + std::to_string(l.value("group", 0));
            }
        }
        if(l.contains("perBar")) R.approx.push_back(sub + ": drawn per bar in v1 (v2: per phrase)");
    }
    if(!pick.empty()) R.approx.push_back(name + ": pick '" + pick + "' becomes independent phrase chances (not exclusive)");
}

inline void mapGen(const std::string& gen, const json& params, const json* process, const Library& lib, MapCtx& c, MapResult& R, const std::string& name, int depth){
    Params P; P.p = params.is_object() ? params : json::object();
    auto d = lib.defaults.find(gen); if(d != lib.defaults.end()) P.d = d->second;
    size_t before = R.instances.size();
    if(gen == "pulse") mapPulse(P, c, R, name);
    else if(gen == "hits") mapHits(P, c, R, name);
    else if(gen == "masses") mapMasses(P, c, R, name);
    else if(gen == "sweep") mapSweep(P, c, R, name);
    else if(gen == "shapes") mapShapes(P, c, R, name);
    else if(gen == "effects"){ mapProcess(params, P.d, c, R, name, 0.25); P.used.clear(); for(auto it = P.p.begin(); it != P.p.end(); ++it) P.used.insert(it.key()); }
    else { R.unmapped.push_back(name + ": unknown generator '" + gen + "'"); return; }
    if(gen != "effects") for(auto& k : P.unusedKeys()) R.unmapped.push_back(name + ": " + k);
    double sb = gen == "pulse" || gen == "hits" ? stepBeatsOf(P) : 0.25;
    json proc = process && process->is_object() ? *process : json::object();
    // opacity ramp across the image (rampStart / rampEnd / rampPower): folded into a sweep's level,
    // else a fade-in filter
    if(proc.contains("rampStart") || proc.contains("rampEnd")){
        double r0 = numOf(proc.value("rampStart", json(0))), r1 = numOf(proc.value("rampEnd", json(1)));
        V rp = parseValue(proc.value("rampPower", json(1)));
        if(gen == "sweep" && R.instances.size() > before){
            // v1: opacity = start + (end - start) x u^power along the image. The v2 sweep level is
            // lerp(from, to, u)^curve: with curve = power the end levels are taken to the 1 / power
            // (exact when the ramp starts at 0, which the legacy page ramps do)
            Instance& in = R.instances.back();
            double pw = clampd(rp.v, 0.1, 10), inv = 1.0 / pw;
            double lf = clampd(in.params.value("levelFrom", 1.0) * r0, 0, 1), lt = clampd(in.params.value("levelTo", 1.0) * r1, 0, 1);
            in.params["levelFrom"] = std::pow(lf, inv);
            in.params["levelTo"] = std::pow(lt, inv);
            in.params["curve"] = pw;
            if(rp.spread()) R.approx.push_back(name + ": ramp power chosen per image: the mean kept");
            if(r0 > 1e-6 && std::fabs(pw - 1) > 1e-6) R.approx.push_back(name + ": ramp from a non-zero start: shape approximated");
        } else if(R.instances.size() > before){
            // other items: the end level scales the instrument's level; the rise is a fade-in over
            // the image on the item's own drawing (added after the processes, scoped with them)
            double r1c = clampd(std::max(r0, r1), 0, 1);
            for(size_t i = before; i < R.instances.size(); i++){
                Instance& in = R.instances[i];
                if(in.type == "timefx" || in.type == "filterfx") continue;
                if(in.params.is_object() && in.params.contains("level") && in.params["level"].is_number()) in.params["level"] = clampd(in.params["level"].get<double>() * r1c, 0, 1);
            }
            if(r1 > r0 + 1e-6) proc["_rampFadeIn"] = json{{"amount", clampd(1 - r0 / std::max(1e-9, r1), 0, 1)}, {"power", rp.v}};
            else if(r0 > r1 + 1e-6) proc["_rampFadeOut"] = json{{"amount", clampd(1 - r1 / std::max(1e-9, r0), 0, 1)}, {"power", rp.v}};
        } else R.unmapped.push_back(name + ": opacity ramp process");
        for(auto k : {"rampStart", "rampEnd", "rampPower"}) proc.erase(k);
    }
    if(!proc.empty()){
        size_t mid = R.instances.size();
        for(const char* key : {"_rampFadeIn", "_rampFadeOut"}) if(proc.contains(key)){
            Builder b(c, R, "filterfx", name + " ramp");
            b.look("effect", std::string(key) == "_rampFadeIn" ? "fadeIn" : "fadeOut"); b.look("amount", proc[key].value("amount", 1.0));
            slotSeq(b, c.imageBeats, 100, "all", c.bars);
            if(std::fabs(proc[key].value("power", 1.0) - 1) > 1e-6) R.approx.push_back(name + ": opacity ramp power becomes a linear fade");
            b.in.id = uniqueId(c, name + " ramp"); R.instances.push_back(b.in);
            proc.erase(key);
        }
        bool whole = proc.contains("processOn") && proc["processOn"].is_string() && proc["processOn"].get<std::string>() == "whole image";
        if(!proc.empty()) mapProcess(proc, lib.defaults.count("effects") ? lib.defaults.at("effects") : json::object(), c, R, name + " fx", sb);
        // v1 runs an item's processes on the item's own buffer: instance scope on what it drew
        std::vector<std::string> targets;
        for(size_t i = before; i < mid; i++) if(R.instances[i].type != "timefx" && R.instances[i].type != "filterfx") targets.push_back(R.instances[i].id);
        if(!whole && !targets.empty()){
            std::vector<Instance> fx(R.instances.begin() + (long)mid, R.instances.end());
            R.instances.resize(mid);
            for(auto& f : fx) for(size_t t = 0; t < targets.size(); t++){
                Instance g = f;
                if(t > 0) g.id = uniqueId(c, f.id);
                g.params["fxScope"] = "instance"; g.params["fxTarget"] = targets[t];
                R.instances.push_back(g);
            }
        }
    }
    (void)depth;
}

// Map one v1 preset ("<gen>/<name>" from the library) into v2 instances
inline MapResult mapPreset(const std::string& key, const Library& lib, MapCtx& c){
    installSequencerSchema();   // seq.* routing destinations are checked against the sequencer schema
    MapResult R;
    auto f = lib.presets.find(key);
    if(f == lib.presets.end()){
        auto l = lib.byLegacy.find(key);
        if(l != lib.byLegacy.end()){ f = lib.presets.find(l->second); R.approx.push_back(key + ": legacy method name resolved to " + l->second); }
    }
    if(f == lib.presets.end()){ R.confidence = 0; R.unmapped.push_back(key + ": preset not found"); return R; }
    const json& p = f->second;
    std::string gen = p.value("gen", ""), name = p.value("name", key);
    if(gen == "stack") mapStack(p, lib, c, R, name, 0);
    else mapGen(gen, p.value("params", json::object()), p.contains("process") ? &p["process"] : nullptr, lib, c, R, name);
    // confidence: type certainty times the share of v1 settings that found a home
    double keys = 1;
    if(p.contains("params") && p["params"].is_object()) keys += p["params"].size();
    if(p.contains("process") && p["process"].is_object()) keys += p["process"].size();
    if(p.contains("layers")) for(auto& l : p["layers"]) if(l.contains("params")) keys += l["params"].size();
    double miss = (double)R.unmapped.size(), rough = (double)R.approx.size();
    R.confidence *= clampd(1.0 - 0.8 * miss / keys - 0.4 * rough / keys, 0.1, 1.0);
    if(R.instances.empty()) R.confidence = std::min(R.confidence, 0.1);
    return R;
}

// ---------------------------------------------------------------- harmony helpers
inline std::vector<int> foldScale(const std::vector<int>& notes){
    std::set<int> s; for(int n : notes) s.insert(((n % 12) + 12) % 12);
    if(s.empty()) s.insert(0);
    return std::vector<int>(s.begin(), s.end());
}
// a semitone transpose snapped to the nearest scale degree (ties to the lower degree)
inline int snapDegree(int semis, const std::vector<int>& scale){
    int t = ((semis % 12) + 12) % 12, best = 0, bd = 99;
    for(size_t i = 0; i < scale.size(); i++){
        int d = std::abs(scale[i] - t); d = std::min(d, 12 - d);
        if(d < bd){ bd = d; best = (int)i; }
    }
    return best;
}
// one step per bar: root degree and key shift (semitones); equal neighbours merge
inline HarmonySection makeSection(const std::string& scaleName, const std::vector<int>& semis, int tonic, const std::vector<int>& degreesPerBar, double imageBeats,
                                  const std::vector<int>& shiftsPerBar = {}){
    HarmonySection s;
    s.name = "A";
    s.meter.num = 4; s.meter.den = 4;
    s.scale.name = scaleName; s.scale.semis = semis; s.scale.tonic = tonic;
    s.source = "typed";
    s.steps.clear();
    size_t n = std::max(degreesPerBar.size(), shiftsPerBar.size());
    auto degAt = [&](size_t b){ return degreesPerBar.empty() ? 0 : degreesPerBar[b % degreesPerBar.size()]; };
    auto shAt = [&](size_t b){ return shiftsPerBar.empty() ? 0 : shiftsPerBar[b % shiftsPerBar.size()]; };
    bool allSame = true; for(size_t b = 0; b < n; b++) if(degAt(b) != degAt(0) || shAt(b) != shAt(0)) allSame = false;
    if(n == 0 || allSame){
        s.scale.tonic += shAt(0);   // a constant shift is just the key
        ProgressionStep p; p.degree = degAt(0); p.length = imageBeats; s.steps.push_back(p);
    } else {
        for(size_t b = 0; b < n; b++){
            if(!s.steps.empty() && s.steps.back().degree == degAt(b) && s.steps.back().keyShift == shAt(b)){ s.steps.back().length += 4; continue; }
            ProgressionStep p; p.degree = degAt(b); p.keyShift = shAt(b); p.length = 4; s.steps.push_back(p);
        }
    }
    return s;
}

inline uint64_t fnv64(const std::string& s){ uint64_t h = 1469598103934665603ull; for(unsigned char ch : s){ h ^= ch; h *= 1099511628211ull; } return h; }

// ---------------------------------------------------------------- scenes
inline SceneMap mapScene(const json& v1, const Library& lib, const std::string& fallbackName = ""){
    installSequencerSchema();
    SceneMap M;
    Scene& S = M.doc.scene;
    S.name = v1.value("name", fallbackName);
    S.version = 1;
    S.bpm = v1.value("bpm", 120.0);
    S.seed = fnv64(S.name) & 0xFFFFFFFFull;
    // scale (degree-only: folded into one octave) and tonic from the v1 root
    std::string scName = v1.value("scale", "japanese");
    auto sf = lib.scales.find(scName);
    std::vector<int> notes = sf != lib.scales.end() ? sf->second : std::vector<int>{0, 2, 4, 5, 7, 9, 11};
    if(sf == lib.scales.end()) M.unmapped.push_back("scene: unknown scale '" + scName + "' (major used)");
    M.scaleSemis = foldScale(notes);
    if(M.scaleSemis.size() != notes.size() || notes.back() >= 12) M.unmapped.push_back("scene: scale '" + scName + "' notes above the octave folded into it");
    M.tonic = ((v1.value("root", 0) % 12) + 12) % 12;
    int bars0 = 8;
    if(v1.contains("layers") && !v1["layers"].empty()) bars0 = std::max(1, (int)std::lround(numOf(v1["layers"][0].value("bars", json(8)), 8)));
    double imageBeats = bars0 * 4.0;
    // progression: transposes with weight, one bar each, as key shifts (root degree 0)
    std::vector<int> shifts;
    if(v1.contains("transposeWeights")){
        auto& tw = v1["transposeWeights"];
        for(size_t i = 0; i < tw.size(); i++) if(numOf(tw[i]) > 0) shifts.push_back((int)i);
    }
    if(shifts.size() > 1) M.unmapped.push_back("scene: random transposes per bar become a fixed cycle of key shifts");
    if(v1.contains("degreeWeights")){
        int nz = 0; for(auto& w : v1["degreeWeights"]) if(numOf(w) > 0) nz++;
        if(nz > 1) M.unmapped.push_back("scene: random root degrees");
    }
    S.harmony.sections.clear();
    S.harmony.sections.push_back(makeSection(scName, M.scaleSemis, M.tonic, {}, imageBeats, shifts.size() > 1 ? shifts : std::vector<int>{shifts.empty() ? 0 : shifts[0]}));
    // layers and instances
    std::set<std::string> used;
    S.layers.clear();
    S.instances.clear();
    double confSum = 0, confW = 0;
    std::set<std::string> seenUnmapped;
    int li = 0;
    for(auto& L : v1.value("layers", json::array())){
        int v2layer = std::min(li, 3);
        if(li > 3) M.unmapped.push_back("scene: v1 layer " + std::to_string(li) + " merged into layer 3");
        int bars = std::max(1, (int)std::lround(numOf(L.value("bars", json(8)), 8)));
        if(li < 4){
            LayerDef d; d.zoomBeats = bars * 4.0;
            std::string bl = L.value("blend", "lighten");
            d.blend = (bl == "add" || bl == "normal") ? bl : "lighten";
            if(bl != d.blend) M.unmapped.push_back("scene: layer blend '" + bl + "'");
            d.gain = clampd(L.value("gain", 1.0), 0, 2);
            S.layers.push_back(d);
        }
        if(numOf(L.value("globalProb", json(100)), 100) < 100) M.unmapped.push_back("scene: layer global probability");
        if(L.value("everyN", 1) > 1) M.unmapped.push_back("scene: layer plays every " + std::to_string(L.value("everyN", 1)) + " images");
        std::vector<double> gp; if(L.contains("groupProb")) for(auto& g : L["groupProb"]) gp.push_back(numOf(g, 1));
        MapCtx c; c.layer = v2layer; c.bars = bars; c.imageBeats = bars * 4.0; c.scaleSize = (int)M.scaleSemis.size(); c.used = &used;
        c.v1Scale = notes; c.v2Scale = M.scaleSemis;
        M.cellIds.emplace_back(); M.cellItems.emplace_back();
        // v1 effect cells process what the cells before them drew; v2 effects act on a layer bus. A
        // single-layer v1 scene is split at its effect cells into segments spread over the 4 v2 layers;
        // an effect is copied onto every layer holding a segment drawn before it.
        // v1 plays only the first `rows` cells of a layer (selectItems); the rest are kept, not played
        json cells = L.contains("cells") ? L["cells"] : json::array();
        {
            int rows = L.value("rows", 96);
            if(rows >= 0 && (int)cells.size() > rows){
                int dropped = 0;
                for(size_t k = (size_t)rows; k < cells.size(); k++){ std::string it = cells[k].value("item", ""); if(!it.empty() && it != "none" && numOf(cells[k].value("prob", json(100)), 100) > 0) dropped++; }
                cells.erase(cells.begin() + rows, cells.end());
                if(dropped > 0) M.legacy["approx"].push_back("scene: " + std::to_string(dropped) + " cells below row " + std::to_string(rows) + " not played (as v1)");
            }
        }
        bool split = false;   // effect cells reach only earlier cells through instance scope (below)
        auto isFx = [&](const json& cell){
            std::string it = cell.value("item", "");
            auto l = lib.byLegacy.find(it); if(l != lib.byLegacy.end()) it = l->second;
            return it.compare(0, 8, "effects/") == 0 && numOf(cell.value("prob", json(100)), 100) > 0;
        };
        std::vector<int> segOf(cells.size(), 0);
        int seg = 0; bool prevFx = false;
        for(size_t k = 0; k < cells.size(); k++){
            if(cells[k].value("item", "").empty()) { segOf[k] = seg; continue; }
            bool fx = isFx(cells[k]);
            if(!fx && prevFx) seg++;
            segOf[k] = seg; prevFx = fx;
        }
        int nSeg = seg + 1;
        auto groupOf = [&](int s){ return !split ? v2layer : (nSeg <= 4 ? s : std::min(3, s * 4 / nSeg)); };
        if(split && nSeg > 1){
            M.legacy["layerSplit"] = json{{"segments", nSeg}, {"layers", std::min(4, nSeg)}};
            if(nSeg > 4) M.unmapped.push_back("scene: " + std::to_string(nSeg) + " effect segments merged into 4 layers (some effects reach cells drawn after them)");
        }
        // last drawing cell: an effect cell before it must not reach the cells drawn after it
        int lastDraw = -1;
        for(size_t k = 0; k < cells.size(); k++){ std::string it = cells[k].value("item", ""); if(!it.empty() && it != "none" && numOf(cells[k].value("prob", json(100)), 100) > 0 && !isFx(cells[k])) lastDraw = (int)k; }
        std::vector<std::string> drawnIds;   // drawing instances of this v1 layer so far
        for(size_t k = 0; k < cells.size(); k++){
            const json& cell = cells[k];
            std::string item = cell.value("item", "");
            double prob = numOf(cell.value("prob", json(100)), 100);
            M.cellItems.back().push_back(item);
            M.cellIds.back().emplace_back();
            if(item.empty() || item == "none" || prob <= 0) continue;
            bool fx = isFx(cell);
            c.layer = groupOf(fx ? 0 : segOf[k]);
            MapResult R = mapPreset(item, lib, c);
            if(fx && split){
                // copies for the other layers whose segments come before this effect
                int last = groupOf(segOf[k]);
                std::vector<Instance> extra;
                for(int lay = groupOf(0) + 1; lay <= last; lay++)
                    for(auto in : R.instances){ in.layer = lay; in.id = uniqueId(c, in.id + " l" + std::to_string(lay)); extra.push_back(in); }
                R.instances.insert(R.instances.end(), extra.begin(), extra.end());
            }
            if(fx && !split && (int)k < lastDraw && !drawnIds.empty()){
                // v1 processes the pixels drawn so far: scope "earlier" (the layer bus as drawn by
                // the instances listed before this one; one effect, not one copy per instance)
                for(auto& in : R.instances){
                    if(in.params.is_object() && in.params.value("fxScope", std::string()) == "instance") continue;
                    in.params["fxScope"] = "earlier";
                }
            }
            if(!fx) for(auto& in : R.instances) if(in.type != "timefx" && in.type != "filterfx") drawnIds.push_back(in.id);
            int g = cell.value("group", 0);
            double gprob = (g >= 0 && g < (int)gp.size()) ? gp[(size_t)g] : 1;
            double ch = prob * gprob;
            std::set<std::string> rIds; for(auto& in : R.instances) rIds.insert(in.id);
            for(auto& in : R.instances){
                in.phrase = json{{"length", c.imageBeats}};
                // a follower of an instance mapped from the same cell plays only where that one plays: no second scaling
                bool followsOwn = in.seq.value("mode", std::string()) == "follow" && rIds.count(in.seq.value("followId", std::string()));
                if(ch < 100 - 1e-9 && !followsOwn){
                    if(in.seq.contains("chance")){ in.seq["chance"] = clampd(in.seq["chance"].get<double>() * ch / 100, 0, 100); }
                    else { in.seq["chance"] = clampd(ch, 0, 100); in.seq["chanceScope"] = "phrase"; }
                }
                in.preset = "legacy " + item;
                M.cellIds.back().back().push_back(in.id);
                InstanceOrigin o; o.preset = "legacy/" + item; o.version = 1;
                M.doc.origins[in.id] = o;
                S.instances.push_back(in);
            }
            if(gprob < 1) R.approx.push_back(item + ": group probability folded into the chance");
            confSum += R.confidence; confW += 1;
            for(auto& u : R.unmapped) if(seenUnmapped.insert(u).second) M.unmapped.push_back(u);
            for(auto& u : R.approx) if(seenUnmapped.insert("~" + u).second) M.legacy["approx"].push_back(u);
        }
        li++;
    }
    while(S.layers.size() < 4){ LayerDef d = S.layers.empty() ? LayerDef{} : S.layers[0]; if(S.layers.empty()) d.zoomBeats = imageBeats; S.layers.push_back(d); }
    M.confidence = confW > 0 ? confSum / confW : 0;
    // palette -> colour roles
    std::string pal = v1.value("palette", "");
    auto pf = lib.palettes.find(pal);
    json roles = json::object();
    if(pf != lib.palettes.end()){
        static const char* names[] = {"neutral", "highlight", "accent", "bass", "shadow"};
        for(size_t i = 0; i < 5 && i < pf->second.size(); i++) roles[names[i]] = "#" + pf->second[i];
    } else if(!pal.empty()) M.unmapped.push_back("scene: unknown palette '" + pal + "'");
    M.legacy["source"] = "v1 scene " + S.name;
    M.legacy["palette"] = pal;
    M.legacy["colourRoles"] = roles;
    M.legacy["v1Scale"] = scName;
    M.legacy["v1Root"] = v1.value("root", 0);
    M.legacy["confidence"] = std::round(M.confidence * 1000) / 1000;
    if(!M.legacy.contains("approx")) M.legacy["approx"] = json::array();
    M.legacy["unmapped"] = M.unmapped;
    for(auto k : {"performance", "compat", "snapshot", "notegrid"}) if(v1.contains(k)) M.legacy["dropped"].push_back(k);
    M.errors = validate(M.doc);
    return M;
}

// The scene file: the SceneDoc plus the legacy block
inline json sceneFile(const SceneMap& M){
    json j = toJson(M.doc);
    j["legacy"] = M.legacy;
    // the v1 palette as the scene's role colours (renderers: Paper::colourOf)
    if(M.legacy.contains("colourRoles") && M.legacy["colourRoles"].is_object() && !M.legacy["colourRoles"].empty()) j["colours"] = M.legacy["colourRoles"];
    return j;
}

} // namespace legacy
} // namespace forms2
