//
//  formsCore.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Scene / setlist data, the Director's selection logic and the importer of the 1920 Director
//  snapshots. Plain C++ and nlohmann::json only (no OF, no GUI), so it can be tested on its own.
//

#pragma once

#include "formsCover.h"
#include "formsKickSample.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace forms {

using json = nlohmann::json;
using Rng = std::mt19937;

// ---------------------------------------------------------------- bar lengths
// A layer's image may last a fraction of a bar (0.25 = one beat). Lengths are kept on a grid of
// 1/16 bar (a power of two, so sums and multiples of them stay exact in a double); the shortest
// is 1/16. Harmony stays per whole bar: an image's local bar k plays the absolute bar
// floor(start + k), and its roots / trans have barCount() entries (the renderer's ceil(bars)).
constexpr double kBarGrid = 16;
inline double quantBars(double b){
    if(!std::isfinite(b)) return 8;
    return std::clamp(std::round(b * kBarGrid) / kBarGrid, 1.0 / kBarGrid, 256.0);
}
inline int barCount(double b){ return std::max(1, (int)std::ceil(b - 1e-9)); }   // bars it touches (from a bar line)
inline int barIndex(double b){ return (int)std::floor(b + 1e-9); }               // the whole bar a position is in
// whole lengths are written as integers, so existing files and requests don't change
inline nlohmann::json barsJson(double b){ return b == std::floor(b) ? nlohmann::json((long long)b) : nlohmann::json(b); }
inline double barsFrom(const nlohmann::json& j, const char* key, double fallback = 8){
    auto it = j.find(key);
    if(it == j.end() || !it->is_number()) return fallback;
    return quantBars(it->get<double>());
}
inline std::string barsText(double b){
    if(b == std::floor(b)) return std::to_string((long long)b);
    char buf[32]; snprintf(buf, sizeof(buf), "%g", b); return buf;
}

// Uniform in [0, max), like ofRandom(max)
inline float uniform(Rng& rng, float max){
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) * max;
}

// ---------------------------------------------------------------- scene data

struct Cell {
    std::string item;   // legacy method name (later: an instrument preset)
    float prob = 0;     // 0..100
    int group = 0;      // index into LayerDef::groupProb
    bool solo = false;
    float gain = 1;     // volume / opacity of this instrument in this scene (0..1); 1 = as drawn
};

struct LayerDef {
    json cover = json::object(); // shared cover keys; disabled unless explicitly enabled
    std::string effectPreset;         // effect-only instrument appended after this whole layer
    double bars = 8;                  // real bar count of each image (fractions: quantBars)
    int repeat = 1;                   // each image (page) plays this many times before a new one
    bool blocking = true;             // off: this layer's instruments never draw their black background (no blank, no erasing of the layers below)
    float beatOffset = 0;             // beats this layer plays later (+) or earlier (-) than the others; layers 2 and up
    // Phrase (0 = off): with a phrase longer than the page, one render draws the whole phrase and
    // the layer shows it page by page (each page the next window of it), so patterns and notes
    // run on across short pages. Layer 1: rounded up to a whole number of pages (phraseSlices).
    // Layers 2 and up: exactly the phrase, the last page cut short (Engine::layerSpan exact).
    double phrase = 0;
    int everyN = 1;                   // stored by the Director, not used by its selection
    float globalProb = 100;           // 0..100: whole-image gate
    std::vector<float> groupProb {1}; // 0..1 per group
    int rows = 96;                    // only the first `rows` cells take part (Director "Rows")
    std::vector<Cell> cells;
    std::string blend = "lighten";    // Phase 3
    float gain = 1;                   // Phase 3
    bool kick = false;                // Phase 3: kick detection reads this layer (none set: layer 1)
    int z = -1;                       // stacking order (higher = on top); -1: its position (layer 1 at the bottom)
    // A frozen layer (Gallery "Freeze"): always this exact image: {"seed", "items" (resolved),
    // "roots", "trans", "from"}. Nothing is chosen or drawn at random for it.
    json fixed;
    // The layer's effects before the sound (blur, trail, Perlin mask): only the values that are not
    // the defaults (LayerFxValues); empty: none. They switch with the scene.
    json fx;
};
// A layer's effects as numbers (the scene's "fx" object <-> the node's controls)
struct LayerFxValues {
    float hBlur = 0, vBlur = 0, trail = 0, trailLevel = 0.8f;
    float maskAmount = 0, maskWidth = 4, maskHeight = 0.25f, maskContrast = 1, maskThreshold = 0, maskBlur = 0, maskSpeed = 0.25f;
    int maskSeed = 1; bool maskInvert = false, maskAnimate = false;
    bool operator==(const LayerFxValues& o) const {
        return hBlur == o.hBlur && vBlur == o.vBlur && trail == o.trail && trailLevel == o.trailLevel && maskAmount == o.maskAmount
            && maskWidth == o.maskWidth && maskHeight == o.maskHeight && maskContrast == o.maskContrast && maskThreshold == o.maskThreshold
            && maskBlur == o.maskBlur && maskSpeed == o.maskSpeed && maskSeed == o.maskSeed && maskInvert == o.maskInvert && maskAnimate == o.maskAnimate;
    }
    bool any() const { return hBlur > 0.05f || vBlur > 0.05f || trail > 0.01f || maskAmount > 0; }
    // layer: its index (the mask seed's default is layer + 1, as the controls)
    static LayerFxValues from(const json& j, int layer){
        LayerFxValues v; v.maskSeed = layer + 1;
        if(!j.is_object()) return v;
        auto f = [&](const char* k, float& x, float lo, float hi){ auto it = j.find(k); if(it != j.end() && it->is_number()) x = std::clamp(it->get<float>(), lo, hi); };
        f("hBlur", v.hBlur, 0, 60); f("vBlur", v.vBlur, 0, 60); f("trail", v.trail, 0, 16); f("trailLevel", v.trailLevel, 0, 1);
        f("maskAmount", v.maskAmount, 0, 1); f("maskWidth", v.maskWidth, 0.125f, 32); f("maskHeight", v.maskHeight, 0.01f, 2);
        f("maskContrast", v.maskContrast, 0.1f, 8); f("maskThreshold", v.maskThreshold, 0, 1); f("maskBlur", v.maskBlur, 0, 60); f("maskSpeed", v.maskSpeed, 0, 4);
        if(j.contains("maskSeed") && j["maskSeed"].is_number()) v.maskSeed = std::clamp(j["maskSeed"].get<int>(), 0, 65535);
        v.maskInvert = j.value("maskInvert", false); v.maskAnimate = j.value("maskAnimate", false);
        return v;
    }
    json toJson(int layer) const {
        const LayerFxValues d = from(json(), layer);
        json j = json::object();
        auto f = [&](const char* k, float x, float dx){ if(x != dx) j[k] = x; };
        f("hBlur", hBlur, d.hBlur); f("vBlur", vBlur, d.vBlur); f("trail", trail, d.trail); f("trailLevel", trailLevel, d.trailLevel);
        f("maskAmount", maskAmount, d.maskAmount); f("maskWidth", maskWidth, d.maskWidth); f("maskHeight", maskHeight, d.maskHeight);
        f("maskContrast", maskContrast, d.maskContrast); f("maskThreshold", maskThreshold, d.maskThreshold); f("maskBlur", maskBlur, d.maskBlur); f("maskSpeed", maskSpeed, d.maskSpeed);
        if(maskSeed != d.maskSeed) j["maskSeed"] = maskSeed;
        if(maskInvert) j["maskInvert"] = true;
        if(maskAnimate) j["maskAnimate"] = true;
        return j;
    }
};
// Pages per render (1 = no phrase) and the bars one render covers
inline int phraseSlices(const LayerDef& l){
    if(l.phrase <= l.bars + 1e-9) return 1;
    return std::clamp((int)std::ceil(l.phrase / l.bars - 1e-9), 1, 64);
}
inline double phraseBars(const LayerDef& l){ return l.bars * phraseSlices(l); }
// The layers of a scene from the bottom of the stack to the top
inline int kickLayer(const std::vector<LayerDef>& layers){
    for(int i=0;i<(int)layers.size();++i)if(layers[i].kick)return i;
    return 0;
}
inline std::vector<int> stackOrder(const std::vector<LayerDef>& ls){
    std::vector<int> o(ls.size());
    for(size_t i = 0; i < o.size(); i++) o[i] = (int)i;
    std::stable_sort(o.begin(), o.end(), [&](int a, int b){
        int za = ls[a].z >= 0 ? ls[a].z : a, zb = ls[b].z >= 0 ? ls[b].z : b; return za < zb; });
    return o;
}

// Library entries (library/scales.json, library/palettes.json in the renderer folder). They are
// sent with every request, so the renderer doesn't need its own tables. legacyIndex: the index
// of the 1920 table this entry came from (-1 for new ones); fx1_transpose needs it.
struct ScaleDef {
    std::string name;
    std::vector<int> notes {0};    // semitones from the root
    int legacyIndex = -1;
    std::string source;            // "" = this library (library/scales.json); else "catalog scale #n" /
                                   // "catalog chord #n" (the shared Pitchclass catalog, read-only here)
};
struct PaletteDef {
    std::string name;
    std::vector<std::string> colors {"FFFFFF"};   // RRGGBB; the renderer cycles them over 5 slots
    int legacyIndex = -1;
};
inline void to_json(json& j, const ScaleDef& d){ j = json{{"name", d.name}, {"notes", d.notes}, {"legacyIndex", d.legacyIndex}}; }
inline void from_json(const json& j, ScaleDef& d){
    d.name = j.value("name", std::string());
    d.notes = j.value("notes", std::vector<int>{0});
    d.legacyIndex = j.value("legacyIndex", -1);
}
inline void to_json(json& j, const PaletteDef& d){ j = json{{"name", d.name}, {"colors", d.colors}, {"legacyIndex", d.legacyIndex}}; }
inline void from_json(const json& j, PaletteDef& d){
    d.name = j.value("name", std::string());
    d.colors = j.value("colors", std::vector<std::string>{"FFFFFF"});
    d.legacyIndex = j.value("legacyIndex", -1);
}

// A scene refers to library entries by name. Scenes saved before the library existed stored the
// 1920 index; it is kept as "#<index>" until the engine resolves it to a name.
inline std::string libraryRef(const json& j){
    if(j.is_number()) return "#" + std::to_string(j.get<int>());
    if(j.is_string()) return j.get<std::string>();
    return "";
}

struct Scene {
    std::string name;
    std::string bank;                 // its folder under scenes/ ("" = scenes/ itself: the legacy bank); not in the file
    double bpm = 120;                 // may be a fraction (112.5); whole tempos are written as integers
    std::string palette = "#0";
    std::string scale = "#0";
    int notegrid = 100;
    int root = 24;                    // Director "Root" (not sent to the renderer in 1920)
    int snapshot = -1;                // Director slot this scene came from (drives Macro_8 in the patch)
    std::vector<float> degreeWeights = std::vector<float>(12, 0.0f);
    std::vector<float> transposeWeights = std::vector<float>(12, 0.0f);
    float lineWeight = 1;             // multiplies the stroke of this scene's line instruments (1 = as the instruments say)
    int bassDegree = 0;               // scale degree the bass instruments add to the bar's degree (0 = the root)
    // Effect-only instruments applied to every layer, bar by bar: each bar of the timeline gets one
    // resolution of the effect (its own seed), so layers of different lengths get the same effect at
    // the same moment.
    std::vector<std::string> globalEffects;
    // This scene's own version of some instruments: per instrument ref, only what differs from the
    // shared instrument (presetOwnDiff / presetWithOwn in formsGenerators.h). Laid over the shared
    // one whenever this scene asks for images; the instrument's file and the other scenes never see it.
    json instruments = json::object();
    std::vector<LayerDef> layers;
    json performance = json::object();
    std::shared_ptr<const formskick::Asset> kickSample;
    // Renderer compat flags, default = 1920 behaviour (fx1TransposeMeta, fx34LastHalfOnly, midRenderReseed)
    json compat = json::object();
};

inline void to_json(json& j, const Cell& c){
    j = json{{"item", c.item}, {"prob", c.prob}, {"group", c.group}};
    if(c.solo) j["solo"] = true;
    if(c.gain != 1) j["gain"] = c.gain;   // only when set: scenes stay as they were
}
inline void from_json(const json& j, Cell& c){
    c.item = j.value("item", std::string());
    c.prob = j.value("prob", 0.0f);
    c.group = j.value("group", 0);
    c.solo = j.value("solo", false);
    c.gain = std::clamp(j.value("gain", 1.0f), 0.0f, 1.0f);
}
inline void to_json(json& j, const LayerDef& l){
    j = json{{"bars", barsJson(l.bars)}, {"everyN", l.everyN}, {"globalProb", l.globalProb},
             {"groupProb", l.groupProb}, {"rows", l.rows}, {"blend", l.blend}, {"gain", l.gain},
             {"cells", l.cells}};
    if(!l.effectPreset.empty()) j["effectPreset"]=l.effectPreset;
    if(l.kick) j["kick"] = true;
    if(!l.cover.empty()) j["cover"] = l.cover;
    if(l.repeat > 1) j["repeat"] = l.repeat;
    if(l.beatOffset != 0) j["beatOffset"] = l.beatOffset;
    if(!l.blocking) j["blocking"] = false;   // only when off: scenes stay as they were
    if(l.phrase > 0) j["phrase"] = barsJson(l.phrase);
    if(l.z >= 0) j["z"] = l.z;
    if(l.fixed.is_object()) j["fixed"] = l.fixed;
    if(l.fx.is_object() && !l.fx.empty()) j["fx"] = l.fx;
}
inline void from_json(const json& j, LayerDef& l){
    l.bars = barsFrom(j, "bars", 8);
    l.cover = j.value("cover", json::object());
    l.blocking = formscover::blocking(j);
    l.effectPreset=j.value("effectPreset",std::string());
    l.everyN = j.value("everyN", 1);
    l.globalProb = j.value("globalProb", 100.0f);
    l.groupProb = j.value("groupProb", std::vector<float>{1});
    l.rows = j.value("rows", 96);
    l.blend = j.value("blend", std::string("lighten"));
    l.gain = std::clamp(j.value("gain", 1.0f),0.f,2.f);
    l.kick = j.value("kick", false);
    l.repeat = std::max(1, std::min(16, j.value("repeat", 1)));
    l.beatOffset = std::clamp(j.value("beatOffset", 0.0f), -64.0f, 64.0f);
    l.phrase = j.contains("phrase") ? std::min(64.0, barsFrom(j, "phrase", 0)) : 0;
    l.z = j.value("z", -1);
    l.fixed = j.contains("fixed") ? j["fixed"] : json();
    l.fx = (j.contains("fx") && j["fx"].is_object()) ? j["fx"] : json();
    l.cells = j.value("cells", std::vector<Cell>());
}
inline void to_json(json& j, const Scene& s){
    j = json{{"name", s.name}, {"bpm", barsJson(s.bpm)}, {"palette", s.palette}, {"scale", s.scale},
             {"notegrid", s.notegrid}, {"root", s.root}, {"snapshot", s.snapshot},
             {"degreeWeights", s.degreeWeights}, {"transposeWeights", s.transposeWeights},
             {"layers", s.layers}, {"performance", s.performance}, {"compat", s.compat}};
    if(s.bassDegree) j["bassDegree"] = s.bassDegree;
    if(s.lineWeight != 1) j["lineWeight"] = s.lineWeight;
    if(!s.globalEffects.empty()) j["globalEffects"] = s.globalEffects;
    if(s.instruments.is_object() && !s.instruments.empty()) j["instruments"] = s.instruments;   // only when there are any: scenes stay as they were
    if(s.kickSample) j["kickSampleAsset"]=formskick::serialize(*s.kickSample);
}
inline void from_json(const json& j, Scene& s){
    s.name = j.value("name", std::string());
    s.bpm = std::clamp(j.value("bpm", 120.0), 1.0, 999.0);
    s.palette = libraryRef(j.value("palette", json(0)));
    s.scale = libraryRef(j.value("scale", json(0)));
    s.notegrid = j.value("notegrid", 100);
    s.root = j.value("root", 24);
    s.snapshot = j.value("snapshot", -1);
    s.degreeWeights = j.value("degreeWeights", std::vector<float>(12, 0.0f));
    s.transposeWeights = j.value("transposeWeights", std::vector<float>(12, 0.0f));
    s.bassDegree = std::clamp(j.value("bassDegree", 0), 0, 11);
    s.lineWeight = std::clamp(j.value("lineWeight", 1.0f), 0.1f, 16.0f);
    s.globalEffects = j.value("globalEffects", std::vector<std::string>());
    s.instruments = (j.contains("instruments") && j["instruments"].is_object()) ? j["instruments"] : json::object();
    s.layers = j.value("layers", std::vector<LayerDef>());
    s.performance = j.value("performance", json::object());
    s.kickSample=j.contains("kickSampleAsset")?formskick::read(j["kickSampleAsset"]):nullptr;
    s.compat = j.value("compat", json::object());
    if(s.compat.is_object()) s.compat.erase("harmonyFirstHalfOnly");   // a removed feature: old scene files drop it when saved
}

struct SetlistEntry {
    std::string scene;
    int images = 1;
    bool hardCut = false;             // Phase 3: arriving at this entry cuts every layer at its first bar
};
inline void to_json(json& j, const SetlistEntry& e){
    j = json{{"scene", e.scene}, {"images", e.images}};
    if(e.hardCut) j["cut"] = true;
}
inline void from_json(const json& j, SetlistEntry& e){
    e.scene = j.value("scene", std::string());
    e.images = std::max(1, j.value("images", 1));
    e.hardCut = j.value("cut", false);
}

// ---------------------------------------------------------------- selection (Director port)

// probabilityDropdownList::generateOutput(): global roll, one roll per group, then one roll per
// cell of an active group. Empty / "none" cells are skipped; solo isolates soloed cells.
// gains (optional): each selected item's cell gain, in the same order
inline std::vector<std::string> selectItems(const LayerDef& l, Rng& rng, std::vector<float>* gains = nullptr){
    std::vector<std::string> out;
    if(!(uniform(rng, 100) < l.globalProb)) return out;

    std::vector<bool> activeGroups(l.groupProb.size(), false);
    for(size_t i = 0; i < l.groupProb.size(); i++){
        activeGroups[i] = uniform(rng, 1.0f) < l.groupProb[i];
    }
    bool anySolo = std::any_of(l.cells.begin(), l.cells.end(), [](const Cell& c){ return c.solo; });

    int n = std::min<int>(l.rows, (int)l.cells.size());
    for(int i = 0; i < n; i++){
        const Cell& c = l.cells[i];
        if(c.item.empty() || c.item == "none") continue;
        if(anySolo && !c.solo) continue;
        bool groupActive = c.group >= (int)l.groupProb.size() || activeGroups[c.group];
        if(groupActive && uniform(rng, 100) < c.prob){
            out.push_back(c.item);
            if(gains) gains->push_back(c.gain);
        }
    }
    return out;
}

// probabilityDropdownList::getWeightedRandomIndex()
inline int weightedIndex(const std::vector<float>& w, Rng& rng){
    if(w.empty()) return 0;
    float sum = 0;
    for(float v : w) if(v >= 0) sum += v;
    if(sum <= 0) return 0;
    float r = uniform(rng, sum);
    float cum = 0;
    for(size_t i = 0; i < w.size(); i++){
        if(w[i] >= 0){
            cum += w[i];
            if(r <= cum) return (int)i;
        }
    }
    return (int)w.size() - 1;
}

// One image's harmony: per bar a degree index (roots) and a transpose in semitones (trans), every
// bar drawn from the scene's weights.
struct Harmony {
    std::vector<int> roots, trans;
};
inline Harmony makeHarmony(const Scene& s, int bars, Rng& rng){
    Harmony h;
    h.roots.assign(bars, 0);
    h.trans.assign(bars, 0);
    for(int i = 0; i < bars; i++){
        h.trans[i] = weightedIndex(s.transposeWeights, rng); // same call order as the Director
        h.roots[i] = weightedIndex(s.degreeWeights, rng);
    }
    return h;
}

// ---------------------------------------------------------------- import from 1920

inline float numberOf(const json& j, float fallback = 0){
    if(j.is_number()) return j.get<float>();
    if(j.is_string()){
        try { return std::stof(j.get<std::string>()); } catch(...) {}
    }
    if(j.is_boolean()) return j.get<bool>() ? 1.0f : 0.0f;
    return fallback;
}

// node = the FORMS_P5_PNG_Director_<n>.json of a preset. Returns scenes keyed by Director slot.
// The 1920 patch quirks are baked in: the renderer got Director bars x2 (so layer bars = x2,
// with harmony only for the first half), Global % / Group % / Rows were node-level, not per
// snapshot.
inline std::map<int, Scene> importDirector(const json& node){
    std::map<int, Scene> scenes;
    float globalProb = node.contains("Global_%") ? numberOf(node["Global_%"], 100) : 100;
    int rows = node.contains("Rows") ? (int)numberOf(node["Rows"], 96) : 96;
    std::vector<float> groupProb;
    if(node.contains("Group_%") && node["Group_%"].is_array()){
        for(auto& g : node["Group_%"]) groupProb.push_back(numberOf(g, 1));
    }
    if(groupProb.empty()) groupProb = {1};

    if(!node.contains("Snapshots")) return scenes;
    for(auto& [key, snap] : node["Snapshots"].items()){
        int slot = std::stoi(key);
        Scene s;
        s.name = snap.value("name", "SNAPSHOT_" + key);
        s.bpm = numberOf(snap.value("bpm", json(120)), 120);
        s.palette = "#" + std::to_string((int)numberOf(snap.value("palette", json(0))));
        s.scale = "#" + std::to_string((int)numberOf(snap.value("scale", json(0))));
        s.notegrid = (int)numberOf(snap.value("notegrid", json(100)), 100);
        s.root = (int)numberOf(snap.value("root", json(24)), 24);
        s.snapshot = slot;
        if(snap.contains("degreeWeights")) s.degreeWeights = snap["degreeWeights"].get<std::vector<float>>();
        if(snap.contains("transposeWeights")) s.transposeWeights = snap["transposeWeights"].get<std::vector<float>>();
        s.compat = json{{"fx1TransposeMeta", false}, {"fx34LastHalfOnly", true}, {"midRenderReseed", true}};

        LayerDef l;
        l.bars = 2 * (int)numberOf(snap.value("bars", json(4)), 4);
        l.everyN = (int)numberOf(snap.value("everyN", json(1)), 1);
        l.globalProb = globalProb;
        l.groupProb = groupProb;
        l.rows = rows;
        bool anySoloed = snap.value("anySoloed", false);
        auto contents = snap.value("cellContents", json::array());
        auto probs = snap.value("probabilities", json::array());
        auto groups = snap.value("groups", json::array());
        auto solos = snap.value("soloStates", json::array());
        for(size_t i = 0; i < contents.size(); i++){
            Cell c;
            const json& cc = contents[i];
            c.item = cc.is_object() ? cc.value("value", std::string()) : (cc.is_string() ? cc.get<std::string>() : std::string());
            c.prob = i < probs.size() ? numberOf(probs[i]) : 0;
            c.group = i < groups.size() ? (int)numberOf(groups[i]) : 0;
            c.solo = anySoloed && i < solos.size() && solos[i].is_boolean() && solos[i].get<bool>();
            l.cells.push_back(c);
        }
        s.layers.push_back(l);
        scenes[slot] = s;
    }
    return scenes;
}

// The 1920 setlist: CSV of Director slots + CSV of durations; each entry lasts
// clamp(duration / 4, 1, 32) images (Vector_Item_Operations_19 / Ranger_9 / Duplicator_2).
inline std::vector<SetlistEntry> importSetlist(const std::vector<int>& slots, const std::vector<float>& durations,
                                               const std::map<int, Scene>& scenes){
    std::vector<SetlistEntry> out;
    for(size_t i = 0; i < slots.size(); i++){
        float d = i < durations.size() ? durations[i] / 4.0f : 1.0f;
        d = std::min(32.0f, std::max(1.0f, d));
        auto it = scenes.find(slots[i]);
        out.push_back({it != scenes.end() ? it->second.name : std::to_string(slots[i]), (int)d});
    }
    return out;
}

} // namespace forms
