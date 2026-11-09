//
//  v2Sequencer.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The shared sequencer (ROADMAP 5.1, 5.4, 5.10): "when" (rhythm modes and modifiers) and "which
//  pitch" (root, scale degrees, chord / arp) for every instrument, planned one phrase at a time into
//  an event list. Also phrase lengths, phrase seeds (keep / mutate / new) and the look-ahead helper.
//
//  Seeds: phrase seed -> per-stream lineage (rhythm, pitch) -> group seeds (every n beats, counted
//  from the phrase start) and step seeds -> event seeds. Keep reuses the lineage, mutate redraws a
//  fraction of the step / group seeds, new draws a fresh one.
//

#pragma once

#include "v2Core.h"
#include "v2Harmony.h"
#include <functional>
#include <sstream>

namespace forms2 {

// Reads a modulated value: (key with the "seq." prefix, base value, level, seed) -> value.
// The modulation engine provides it; empty = identity.
using ModHook = std::function<double(const std::string& key, double base, Level level, uint64_t seed)>;
// Same with the event's start beat (event-level reads); see Modulation::hookAt
using ModHookAt = std::function<double(const std::string& key, double base, Level level, uint64_t seed, Beat start)>;

// ---------------------------------------------------------------- the sequencer schema
namespace seqdetail {
inline ParamDef P(const std::string& key, const std::string& label, const std::string& group, const std::string& type,
                  double mn, double mx, json def, unsigned levels, Level defLevel, bool main = false, bool degrees = false,
                  std::vector<std::string> options = {}, const std::string& unit = "", const std::string& help = ""){
    ParamDef p; p.key = key; p.label = label; p.group = group; p.type = type; p.min = mn; p.max = mx; p.def = std::move(def);
    p.levels = levels; p.defaultLevel = defLevel; p.main = main; p.degrees = degrees; p.options = std::move(options);
    p.unit = unit; p.help = help; return p;
}
constexpr unsigned PS = LevelPhrase | LevelScene;
constexpr unsigned EPL = LevelEvent | LevelPhrase | LevelLayer;
constexpr unsigned EP = LevelEvent | LevelPhrase;
}

// Keys are bare here; the modulation matrix uses them as "seq.<key>"
inline const std::vector<ParamDef>& sequencerParams(){
    using namespace seqdetail;
    static const std::vector<ParamDef> defs = {
        // rhythm
        P("mode", "Rhythm", "rhythm", "enum", 0, 5, "euclidean", PS, LevelPhrase, true, false, {"euclidean", "probability", "pattern", "follow", "external", "position"}, "",
          "external: steps given from outside (PlanContext::externalSteps); position: slots in the phrase (positions)"),
        P("positions", "Positions", "rhythm", "string", 0, 0, "lastBar", PS, LevelPhrase, false, false, {}, "",
          "position mode: any of firstBeat lastBar beforeEnd barLastBeat beforeFollowed (before each hit of followId)"),
        P("positionBeats", "Position length", "rhythm", "float", 0.0625, 16, 1, PS, LevelPhrase, false, false, {}, "beats", "slot length of firstBeat / beforeEnd / barLastBeat / beforeFollowed"),
        P("steps", "Steps per bar", "rhythm", "int", 0, 64, 0, PS, LevelPhrase, true, false, {}, "", "0 = follow the meter (numerator x subdivision)"),
        P("subdivision", "Steps per beat", "rhythm", "int", 0, 8, 0, PS, LevelPhrase, false, false, {}, "", "0 = auto: 8 / denominator (4/4: 2, 7/8: 1)"),
        P("hits", "Hits", "rhythm", "int", 0, 64, 3, PS, LevelPhrase, true),
        P("rotation", "Rotation", "rhythm", "int", 0, 64, 0, PS, LevelPhrase),
        P("hitsList", "Hits list", "rhythm", "intList", 0, 64, json::array(), PS, LevelPhrase, false, false, {}, "", "hits drawn from this list instead (one per phrase, or per pattern repetition with reroll; v1 fills)"),
        P("rotationList", "Rotation list", "rhythm", "intList", 0, 64, json::array(), PS, LevelPhrase, false, false, {}, "", "rotation drawn from this list instead (as hits list; v1 shift)"),
        P("reroll", "Reroll per repetition", "rhythm", "bool", 0, 1, false, PS, LevelPhrase, false, false, {}, "", "hits list / rotation list drawn again at every pattern repetition (v1 reroll)"),
        P("density", "Density", "rhythm", "float", 0, 1, 0.5, EPL, LevelPhrase, true, false, {}, "0-1"),
        P("stepChances", "Step chances", "rhythm", "intList", 0, 100, json::array(), PS, LevelPhrase, false, false, {}, "%", "a chance per step (cycles); overrides density"),
        P("pattern", "Pattern", "rhythm", "string", 0, 0, "x...x...", PS, LevelPhrase, false, false, {}, "", "x hit, X accented hit, . rest, ? half chance; | separates alternatives (one per phrase)"),
        P("followId", "Follow", "rhythm", "string", 0, 0, "", PS, LevelPhrase, false, false, {}, "", "instance ID to follow"),
        P("followMode", "Follow mode", "rhythm", "enum", 0, 2, "hits", PS, LevelPhrase, false, false, {"hits", "skipped", "both"}),
        P("rollChance", "Roll chance", "rhythm", "float", 0, 100, 0, EP, LevelEvent, false, false, {}, "%"),
        P("rollStrokes", "Roll strokes", "rhythm", "int", 2, 64, 3, EP, LevelPhrase),
        P("rollShape", "Roll shape", "rhythm", "enum", 0, 3, "even", PS, LevelPhrase, false, false, {"even", "accel", "ritard", "bunch"}),
        P("densityRamp", "Density ramp", "rhythm", "float", -1, 1, 0, LevelPhrase, LevelPhrase, false, false, {}, "", "thins the start (+) or the end (-) of the phrase"),
        P("noRepeat", "No two in a row", "rhythm", "bool", 0, 1, false, PS, LevelPhrase),
        P("patternLength", "Pattern length", "rhythm", "int", 0, 64, 0, PS, LevelPhrase, false, false, {}, "steps", "0 = the bar; else independent of the bar (polymetre)"),
        P("rests", "Rests", "rhythm", "intList", 0, 63, json::array(), PS, LevelPhrase, false, false, {}, "steps", "pattern steps (from 0) that never play"),
        P("barMask", "Bars that play", "rhythm", "string", 0, 0, "", PS, LevelPhrase, false, false, {}, "", "1 / 0 per bar from the phrase start, cycling, e.g. 1 0 1 1; empty = all"),
        P("phaseShift", "Phase shift", "rhythm", "float", 0, 16, 0, PS, LevelPhrase, false, false, {}, "beats",
          "moves every onset later; a step at the phrase start leaves a short lead-in step there"),
        P("repMask", "Repetitions", "rhythm", "enum", 0, 2, "all", PS, LevelPhrase, false, false, {"all", "odd", "even"}, "", "play on odd (1st, 3rd...) or even pattern repetitions only"),
        // pitch
        P("pitchSource", "Pitch", "pitch", "enum", 0, 2, "scale", PS, LevelPhrase, true, false, {"root", "scale", "chord"}),
        P("scaleMode", "Scale mode", "pitch", "enum", 0, 2, "random", PS, LevelPhrase, false, false, {"random", "walk", "riff"}),
        P("degreeOffset", "Degree offset", "pitch", "int", -24, 24, 0, EP, LevelPhrase, false, true, {}, "degrees", "shifts the pitch by scale degrees"),
        P("octave", "Octave", "pitch", "int", -4, 4, 0, EP, LevelPhrase, true, true, {}, "octaves"),
        P("range", "Range", "pitch", "int", 1, 36, 7, EP, LevelPhrase, false, true, {}, "degrees"),
        P("walkStep", "Walk step", "pitch", "int", 1, 7, 2, EP, LevelPhrase, false, true, {}, "degrees"),
        P("riffLength", "Riff length", "pitch", "int", 1, 16, 4, PS, LevelPhrase),
        P("voices", "Voices", "pitch", "int", 1, 8, 3, PS, LevelPhrase, true, true),
        P("voicing", "Voicing", "pitch", "enum", 0, 3, "close", PS, LevelPhrase, false, true, {"close", "open", "drop2", "scattered"}, "", "scattered: each voice a random degree of the octave (v1 scattered voicing)"),
        P("octaveChance", "Octave jump", "pitch", "float", 0, 100, 0, PS, LevelPhrase, false, false, {}, "%", "chance a pattern step plays an octave up, decided once per phrase per step (v1 octave-up)"),
        P("notesPerStep", "Notes per step", "pitch", "int", 0, 8, 0, PS, LevelPhrase, false, false, {}, "", "0 = all (block chord), 1 = arpeggio"),
        P("order", "Order", "pitch", "enum", 0, 4, "up", PS, LevelPhrase, false, false, {"up", "down", "updown", "random", "typed"}),
        P("typedOrder", "Typed order", "pitch", "string", 0, 0, "1 2 3", PS, LevelPhrase, false, false, {}, "", "voice numbers from 1, e.g. 1 3 2 4"),
        P("advance", "Advance", "pitch", "int", 0, 8, 0, PS, LevelPhrase, false, false, {}, "", "voices the arp moves per step (0 = notes per step)"),
        P("hold", "Hold", "pitch", "bool", 0, 1, false, PS, LevelPhrase, false, false, {}, "", "notes ring to the chord change"),
        P("strum", "Strum", "pitch", "float", 0, 0.5, 0, EP, LevelPhrase, false, false, {}, "beats"),
        P("voiceLeading", "Voice leading", "pitch", "bool", 0, 1, false, PS, LevelPhrase, false, false, {}, "", "held voices glide to the nearest tone of the next chord"),
        P("voiceLeadGlide", "Voice lead glide", "pitch", "float", 0, 4, 0.5, PS, LevelPhrase, false, false, {}, "beats"),
        P("glide", "Glide", "pitch", "float", 0, 2, 0, EP, LevelPhrase, false, false, {}, "beats"),
        P("glideCurve", "Glide curve", "pitch", "enum", 0, 3, "ease", PS, LevelPhrase, false, false, {"linear", "ease", "exp", "sCurve"}, "", "bend shape of glides and voice leading (Event p glideCurve = index, as v2Draw glideShape)"),
        // glide on change (5.16): one switch, triggers, tie / re-attack, time and curve (glideCurve above)
        P("glideOn", "Glide on change", "glide", "bool", 0, 1, false, PS, LevelPhrase, true, false, {}, "", "notes bend to the new pitch when the music changes under them"),
        P("glideMode", "Glide mode", "glide", "enum", 0, 1, "tie", PS, LevelPhrase, false, false, {"tie", "reattack"}, "", "tie: one line bending; reattack: a new note starting from the old pitch"),
        P("glideTime", "Glide time", "glide", "float", -1, 8, -1, EP, LevelPhrase, false, false, {}, "beats", "-1 = legacy times (glide, voiceLeadGlide, scene retuneBeats; else 0.5)"),
        P("glideNote", "Glide: next note", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "portamento between consecutive notes"),
        P("glideChord", "Glide: chord change", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "a sounding note moves to the nearest tone of the new chord"),
        P("glideRoot", "Glide: root change", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "a sounding note moves with the root"),
        P("glideKey", "Glide: key change", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "a new key / scale at a section boundary"),
        P("glideTranspose", "Glide: transposition", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "harmony override jump / clear"),
        P("glideDegree", "Glide: degree shift", "glide", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "degree offset changes"),
        P("arpReset", "Arp reset", "pitch", "bool", 0, 1, true, PS, LevelPhrase, false, false, {}, "", "the arpeggio restarts at each chord change"),
        // mass pitch
        P("massPitch", "Mass pitch", "mass", "enum", 0, 1, "band", PS, LevelPhrase, false, false, {"band", "harmonics"}, "", "harmonics: one event per harmonic of the current root"),
        P("harmonics", "Harmonics", "mass", "int", 1, 64, 6, PS, LevelPhrase),
        P("harmonicStep", "Harmonic step", "mass", "int", 1, 4, 1, PS, LevelPhrase, false, false, {}, "", "2 = odd harmonics only"),
        P("harmonicFade", "Harmonic fade", "mass", "float", 0, 8, 1, PS, LevelPhrase, false, false, {}, "", "level (1 - j / n) ^ fade for harmonic j"),
        P("harmonicTaper", "Harmonic taper", "mass", "float", 0, 1, 0, PS, LevelPhrase, false, false, {}, "0-1", "line width x (1 - taper (j / n) ^ power) for harmonic j"),
        P("harmonicTaperPower", "Taper power", "mass", "float", 0.1, 8, 1, PS, LevelPhrase),
        // notes
        P("noteLength", "Note length", "notes", "float", 0.05, 64, 0.9, EPL, LevelPhrase, true, false, {}, "steps"),
        P("legato", "Legato", "notes", "bool", 0, 1, false, PS, LevelPhrase, false, false, {}, "", "each note lasts to the next"),
        P("overlap", "Overlap", "notes", "float", 0, 2, 0, EP, LevelPhrase, false, false, {}, "beats"),
        P("level", "Level", "notes", "float", 0, 1, 0.8, EPL, LevelEvent, false, false, {}, "0-1"),
        P("accent", "Accent", "notes", "float", -1, 1, -1, LevelPhrase, LevelPhrase, false, false, {}, "0-1", "-1 = the instance's accent"),
        P("swing", "Swing", "notes", "float", -1, 1, -1, LevelPhrase | LevelScene, LevelPhrase, false, false, {}, "0-1", "-1 = the instance's, else the section's"),
        // chance
        P("chance", "Chance", "chance", "float", 0, 100, 100, EP, LevelPhrase, true, false, {}, "%"),
        P("chanceScope", "Chance scope", "chance", "enum", 0, 3, "note", PS, LevelPhrase, false, false, {"note", "beats", "phrase", "scene"}),
        P("chanceBeats", "Chance every", "chance", "float", 0.25, 64, 8, PS, LevelPhrase, false, false, {}, "beats"),
        P("chanceGroup", "Chance group", "chance", "string", 0, 0, "", PS, LevelPhrase, false, false, {}, "", "instances with the same group share a phrase / scene roll"),
        // variation
        P("varRhythm", "Rhythm variation", "variation", "enum", 0, 2, "new", LevelScene, LevelScene, true, false, {"keep", "mutate", "new"}),
        P("varPitch", "Pitch variation", "variation", "enum", 0, 2, "new", LevelScene, LevelScene, true, false, {"keep", "mutate", "new"}),
        P("mutateAmount", "Mutate amount", "variation", "float", 0, 1, 0.25, PS, LevelPhrase, false, false, {}, "0-1"),
    };
    return defs;
}
inline const ParamDef* sequencerParam(const std::string& key){
    for(auto& p : sequencerParams()) if(p.key == key) return &p;
    return nullptr;
}

// ---------------------------------------------------------------- reading settings
// Reads an instance's seq json with the schema defaults, through the modulation hook
struct SeqReader {
    const json& seq;
    const ModHook& hook;
    const ModHookAt* hookAt = nullptr;   // used for reads given a beat
    double num(const std::string& k, Level lv = LevelPhrase, uint64_t seed = 0, Beat at = NAN) const {
        const ParamDef* d = sequencerParam(k);
        double base = 0;
        if(seq.contains(k) && (seq[k].is_number() || seq[k].is_boolean())) base = seq[k].is_boolean() ? (seq[k].get<bool>() ? 1 : 0) : seq[k].get<double>();
        else if(d && (d->def.is_number() || d->def.is_boolean())) base = d->def.is_boolean() ? (d->def.get<bool>() ? 1 : 0) : d->def.get<double>();
        double v = (hookAt && *hookAt && !std::isnan(at)) ? (*hookAt)("seq." + k, base, lv, seed, at) : hook ? hook("seq." + k, base, lv, seed) : base;
        if(d && d->max > d->min) v = std::clamp(v, d->min, d->max);
        return v;
    }
    int integer(const std::string& k, Level lv = LevelPhrase, uint64_t seed = 0) const { return (int)std::lround(num(k, lv, seed)); }
    bool flag(const std::string& k) const { return num(k) > 0.5; }
    int option(const std::string& k, uint64_t seed = 0) const {
        const ParamDef* d = sequencerParam(k);
        if(!d) return 0;
        auto idxOf = [&](const json& v){
            if(v.is_number()) return (int)v.get<double>();
            if(v.is_string()) for(size_t i = 0; i < d->options.size(); i++) if(d->options[i] == v.get<std::string>()) return (int)i;
            return -1;
        };
        int i = seq.contains(k) ? idxOf(seq[k]) : -1;
        if(i < 0) i = std::max(0, idxOf(d->def));
        double v = hook ? hook("seq." + k, (double)i, LevelPhrase, seed) : (double)i;
        return std::clamp((int)std::lround(v), 0, std::max(0, (int)d->options.size() - 1));
    }
    std::string str(const std::string& k) const {
        if(seq.contains(k) && seq[k].is_string()) return seq[k].get<std::string>();
        const ParamDef* d = sequencerParam(k);
        return d && d->def.is_string() ? d->def.get<std::string>() : std::string();
    }
    std::vector<int> ints(const std::string& k) const {
        std::vector<int> out;
        if(seq.contains(k) && seq[k].is_array()) for(auto& v : seq[k]) if(v.is_number()) out.push_back((int)v.get<double>());
        return out;
    }
};

// ---------------------------------------------------------------- variation and phrase seeds
enum class VarMode : int { Keep = 0, Mutate = 1, New = 2 };
struct Variation { VarMode rhythm = VarMode::New, pitch = VarMode::New; double amount = 0.25; };

inline VarMode varModeOf(const json& v, VarMode def){
    if(v.is_string()){ auto s = v.get<std::string>(); if(s == "keep") return VarMode::Keep; if(s == "mutate") return VarMode::Mutate; if(s == "new") return VarMode::New; }
    if(v.is_number()) return (VarMode)std::clamp((int)v.get<double>(), 0, 2);
    return def;
}
// phrase.variation {"rhythm","pitch","amount"} wins over the seq.varRhythm / varPitch / mutateAmount
inline Variation variationOf(const Instance& in){
    static const ModHook none;
    SeqReader r{in.seq, none};
    Variation v;
    v.rhythm = (VarMode)r.option("varRhythm");
    v.pitch = (VarMode)r.option("varPitch");
    v.amount = r.num("mutateAmount");
    if(in.phrase.is_object() && in.phrase.contains("variation") && in.phrase["variation"].is_object()){
        const json& j = in.phrase["variation"];
        if(j.contains("rhythm")) v.rhythm = varModeOf(j["rhythm"], v.rhythm);
        if(j.contains("pitch")) v.pitch = varModeOf(j["pitch"], v.pitch);
        if(j.contains("amount") && j["amount"].is_number()) v.amount = std::clamp(j["amount"].get<double>(), 0.0, 1.0);
    }
    return v;
}

// The base seed of phrase k of an instance
inline uint64_t instanceSeed(uint64_t sceneSeed, const std::string& id){ return mixSeed(sceneSeed, hashTag(id)); }
inline uint64_t basePhraseSeed(uint64_t sceneSeed, const std::string& id, int k){ return mixSeed(instanceSeed(sceneSeed, id), 0x9A5E, (uint64_t)k + 1); }

// One stream's seeds for one phrase. get(key) gives the seed of a step / group / phrase-level
// decision: keep and new read it from one anchor; mutate walks back through the phrases since the
// first, each of which redrew a fraction `amount` of the keys.
struct SeedLineage {
    VarMode mode = VarMode::New;
    double amount = 0;
    uint64_t anchor = 0;
    std::vector<uint64_t> fresh;      // mutate: one seed per phrase 1..k
    uint64_t get(uint64_t key) const {
        if(mode == VarMode::Mutate)
            for(size_t j = fresh.size(); j-- > 0;)
                if(Rng(mixSeed(fresh[j], key, 1)).uniform() < amount) return mixSeed(fresh[j], key, 2);
        return mixSeed(anchor, key, 2);
    }
};
// key spaces: steps from 1, groups, phrase-level tags
inline uint64_t stepKey(int i){ return (uint64_t)i + 1; }
inline uint64_t groupKey(int g){ return 0x100000000ull + (uint64_t)g; }
inline uint64_t tagKey(const char* t){ return 0x200000000ull ^ hashTag(t); }

struct PhraseSeeds { uint64_t phrase = 0; SeedLineage rhythm, pitch; };

inline SeedLineage makeLineage(uint64_t sceneSeed, const std::string& id, int k, VarMode m, double amount, Stream s){
    SeedLineage L; L.mode = m; L.amount = amount;
    if(m == VarMode::New) L.anchor = streamSeed(basePhraseSeed(sceneSeed, id, k), s);
    else L.anchor = streamSeed(basePhraseSeed(sceneSeed, id, 0), s);
    if(m == VarMode::Mutate) for(int j = 1; j <= k; j++) L.fresh.push_back(streamSeed(basePhraseSeed(sceneSeed, id, j), s));
    return L;
}
// Phrase seeds for phrase k of an instance from the scene seed, its ID and the variation settings
inline PhraseSeeds phraseSeeds(uint64_t sceneSeed, const std::string& id, int k, const Variation& v){
    PhraseSeeds p;
    p.phrase = basePhraseSeed(sceneSeed, id, k);
    p.rhythm = makeLineage(sceneSeed, id, k, v.rhythm, v.amount, Stream::Rhythm);
    p.pitch = makeLineage(sceneSeed, id, k, v.pitch, v.amount, Stream::Pitch);
    return p;
}

// ---------------------------------------------------------------- phrase lengths
// Instance.phrase: {"length": 16 or [8,16], "quant": beats (number) or "beats" / "halfBars" / "bars",
// "restartAtSections": false, "offset": 0}
struct PhraseSpec {
    double lo = 16, hi = 16;
    double quantBeats = 1;
    int quantMode = 0;                // 0 beats (quantBeats), 1 half bars, 2 bars
    bool restartAtSections = false;
    double offset = 0;
};
inline PhraseSpec phraseSpecOf(const Instance& in){
    PhraseSpec s;
    const json& j = in.phrase;
    if(!j.is_object()) return s;
    if(j.contains("length")){
        const json& L = j["length"];
        if(L.is_number()) s.lo = s.hi = L.get<double>();
        else if(L.is_array() && L.size() >= 2 && L[0].is_number() && L[1].is_number()){ s.lo = L[0].get<double>(); s.hi = L[1].get<double>(); }
    }
    if(s.hi < s.lo) std::swap(s.lo, s.hi);
    s.lo = std::max(0.25, s.lo); s.hi = std::max(s.lo, s.hi);
    if(j.contains("quant")){
        const json& q = j["quant"];
        if(q.is_number()) s.quantBeats = std::max(0.0, q.get<double>());
        else if(q.is_string()){ auto t = q.get<std::string>(); if(t == "halfBars") s.quantMode = 1; else if(t == "bars") s.quantMode = 2; }
    }
    if(j.contains("restartAtSections") && j["restartAtSections"].is_boolean()) s.restartAtSections = j["restartAtSections"].get<bool>();
    if(j.contains("offset") && j["offset"].is_number()) s.offset = j["offset"].get<double>();
    return s;
}

// The phrases of one instance, laid out from its offset: lengths fixed or drawn from the phrase stream
class PhraseClock {
public:
    PhraseClock(const Instance& in, const HarmonyTimeline& h, uint64_t sceneSeed)
        : id_(in.id), spec_(phraseSpecOf(in)), h_(&h), sceneSeed_(sceneSeed) {}
    const PhraseSpec& spec() const { return spec_; }
    const Phrase& phrase(int k){
        while((int)ph_.size() <= k) append();
        return ph_[(size_t)k];
    }
    int indexAt(Beat b){
        if(b < spec_.offset) return 0;
        while(ph_.empty() || ph_.back().end() <= b) append();
        for(size_t i = ph_.size(); i-- > 0;) if(ph_[i].start <= b) return (int)i;
        return 0;
    }
    // phrases overlapping [a, b)
    std::vector<Phrase> covering(Beat a, Beat b){
        std::vector<Phrase> out;
        for(int k = indexAt(a); ; k++){ const Phrase& p = phrase(k); if(p.start >= b) break; if(p.end() > a) out.push_back(p); }
        return out;
    }
private:
    std::string id_;
    PhraseSpec spec_;
    const HarmonyTimeline* h_;
    uint64_t sceneSeed_;
    std::vector<Phrase> ph_;
    void append(){
        Phrase p;
        p.index = (int)ph_.size();
        p.start = ph_.empty() ? spec_.offset : ph_.back().end();
        p.seed = basePhraseSeed(sceneSeed_, id_, p.index);
        double q = spec_.quantMode == 2 ? h_->barBeatsAt(p.start) : spec_.quantMode == 1 ? h_->barBeatsAt(p.start) * 0.5 : spec_.quantBeats;
        double len = spec_.lo;
        if(spec_.hi > spec_.lo){
            Rng r(streamSeed(p.seed, Stream::Phrase));
            len = r.uniform(spec_.lo, spec_.hi);
        }
        if(q > 1e-9){
            double lo = std::max(q, std::ceil(spec_.lo / q - 1e-9) * q), hi = std::max(lo, std::floor(spec_.hi / q + 1e-9) * q);
            if(spec_.hi > spec_.lo){ Rng r(streamSeed(p.seed, Stream::Phrase)); len = std::clamp(lo + std::floor(r.uniform() * ((hi - lo) / q + 1)) * q, lo, hi); }
            else len = lo;
        }
        if(spec_.restartAtSections){
            auto bs = h_->sectionBoundaries(p.start + 1e-9, p.start + len);
            if(!bs.empty()) len = bs.front() - p.start;
        }
        p.length = std::max(1e-3, len);
        ph_.push_back(p);
    }
};

// Look-ahead (ROADMAP 5.4): the phrases to plan so [nowBeat, nowBeat + beatsAhead] is covered, and at
// least the phrase after the current one
inline std::vector<Phrase> phrasesToPlan(PhraseClock& clock, Beat nowBeat, double beatsAhead){
    const Phrase& cur = clock.phrase(clock.indexAt(nowBeat));
    Beat until = std::max(nowBeat + beatsAhead, cur.end() + 1e-6);
    return clock.covering(nowBeat, until + 1e-9);
}
// Beats ahead to plan for a layer view and the smallest per-event depth (the depth floor)
inline double lookAheadBeats(const LayerView& v, double minDepth = 0.2){ return v.beatsAhead(std::max(0.05, minDepth)); }

// ---------------------------------------------------------------- instrument types
// type name -> event kind (the 14 roles; unknown names are pitched notes)
inline EventKind kindForType(const std::string& t){
    if(t == "kick" || t == "snare" || t == "noise" || t == "hats" || t == "ticks" || t == "perc") return EventKind::Hit;
    if(t == "mass") return EventKind::Mass;
    if(t == "sweep" || t == "texture") return EventKind::Sweep;
    if(t == "shapes" || t == "shape") return EventKind::Shape;
    if(t == "timefx" || t == "filterfx" || t == "fx") return EventKind::Effect;
    return EventKind::Note;
}
// default band (bottom, top) per type; instance params "band": [lo, hi] or "height" override it
inline std::pair<double, double> bandOf(const Instance& in){
    const json& p = in.params;
    if(p.is_object() && p.contains("band") && p["band"].is_array() && p["band"].size() >= 2 && p["band"][0].is_number() && p["band"][1].is_number())
        return {p["band"][0].get<double>(), p["band"][1].get<double>()};
    if(p.is_object() && p.contains("height") && p["height"].is_number()){ double h = p["height"].get<double>(); return {h, h}; }
    const std::string& t = in.type;
    if(t == "kick") return {0.02, 0.15};
    if(t == "snare" || t == "noise") return {0.3, 0.5};
    if(t == "hats" || t == "ticks") return {0.8, 0.95};
    if(t == "mass") return {0.2, 0.8};
    if(t == "sweep" || t == "texture") return {0.3, 0.7};
    return {0.4, 0.6};
}

// ---------------------------------------------------------------- the phrase planner
// External rhythm (mode "external"): one step given from outside (Oceanode step lanes, MIDI)
struct ExternalStep {
    Beat beat = 0;                    // from the phrase start
    double velocity = 1;              // 0..1, the event level factor
    bool hasDegree = false;           // pitched instruments: degree from the instance origin (+ octave)
    int degree = 0;
    double length = 0;                // beats; 0 = to the next step (or the phrase end)
};
using ExternalStepsFn = std::function<std::vector<ExternalStep>(const Instance& in, const Phrase& ph)>;

// A simple store of external steps per instance and phrase index (-1 = every phrase)
class ExternalSteps {
public:
    void set(const std::string& id, int phraseIndex, std::vector<ExternalStep> steps){ m_[{id, phraseIndex}] = std::move(steps); }
    void clear(){ m_.clear(); }
    std::vector<ExternalStep> get(const std::string& id, int phraseIndex) const {
        auto f = m_.find({id, phraseIndex}); if(f != m_.end()) return f->second;
        f = m_.find({id, -1}); return f == m_.end() ? std::vector<ExternalStep>() : f->second;
    }
    ExternalStepsFn fn() const { return [this](const Instance& in, const Phrase& ph){ return get(in.id, ph.index); }; }
private:
    std::map<std::pair<std::string, int>, std::vector<ExternalStep>> m_;
};

// Position slots (mode "position"); Event p "slot" holds the code
enum class Slot : int { FirstBeat = 0, LastBar = 1, BeforeEnd = 2, BarLastBeat = 3, BeforeFollowed = 4 };

struct PlanContext {
    const HarmonyTimeline* harmony = nullptr;     // null: a default (C major, 4/4, degree 0)
    uint64_t sceneSeed = 1;
    PitchSpace pitch;
    ModHook mod;                                  // empty = identity
    ModHookAt modAt;                              // optional: event-level reads at the step's beat
    ExternalStepsFn externalSteps;                // mode "external"
    const std::vector<Event>* prevPhrase = nullptr;   // glide on change: the previous phrase's events (optional)
};

// A planned step that sounds (before pitch)
struct SeqHit {
    Beat start = 0, stepLen = 1;
    int step = 0;                     // step index within the phrase (seed key)
    int stroke = 0, strokes = 1;      // rolls
    double level = 1;
    bool strong = false;
    uint64_t seed = 0;                // the step seed (rhythm stream)
    bool hasDegree = false;           // external steps
    int degree = 0;
    int slot = -1;                    // position mode: Slot code, length = stepLen exactly
};

// the hits of a phrase (rhythm only); exposed for tests and the arrangement view
inline std::vector<SeqHit> planRhythm(const Instance& in, const Phrase& ph, const PlanContext& ctx,
                                      const std::vector<Event>* followed = nullptr, const PhraseSeeds* seedsIn = nullptr){
    static const HarmonyTimeline defaultH;
    const HarmonyTimeline& H = ctx.harmony ? *ctx.harmony : defaultH;
    PhraseSeeds seeds = seedsIn ? *seedsIn : phraseSeeds(ctx.sceneSeed, in.id, ph.index, variationOf(in));
    const SeedLineage& R = seeds.rhythm;
    SeqReader rd{in.seq, ctx.mod, &ctx.modAt};
    uint64_t pseed = seeds.phrase;
    int mode = rd.option("mode", pseed);
    int stepsP = rd.integer("steps", LevelPhrase, pseed), subdivP = rd.integer("subdivision", LevelPhrase, pseed);
    int hits = rd.integer("hits", LevelPhrase, pseed), rot = rd.integer("rotation", LevelPhrase, pseed);
    std::vector<int> hitsL = rd.ints("hitsList"), rotL = rd.ints("rotationList");
    bool reroll = rd.flag("reroll");
    std::vector<int> stepChances = rd.ints("stepChances");
    int followMode = rd.option("followMode", pseed);
    double rollStrokesD = rd.num("rollStrokes", LevelPhrase, pseed);
    int rollShape = rd.option("rollShape", pseed);
    double ramp = rd.num("densityRamp", LevelPhrase, pseed);
    bool noRepeat = rd.flag("noRepeat");
    int patLen = rd.integer("patternLength", LevelPhrase, pseed);
    std::vector<int> rests = rd.ints("rests");
    std::vector<int> barMask;
    for(char c : rd.str("barMask")) if(c == '0' || c == '1') barMask.push_back(c - '0');
    int repMask = rd.option("repMask", pseed);
    double accent = rd.num("accent", LevelPhrase, pseed); if(accent < 0) accent = in.accent;
    double swingP = rd.num("swing", LevelPhrase, pseed);
    double swing = swingP >= 0 ? swingP : in.swing;   // < 0: the section's
    int chanceScope = rd.option("chanceScope", pseed);
    double chanceBeats = std::max(0.25, rd.num("chanceBeats", LevelPhrase, pseed));
    std::string chanceGroup = rd.str("chanceGroup");

    // typed pattern: one alternative per phrase
    std::string pat;
    if(mode == 2){
        std::vector<std::string> alts; std::string cur;
        for(char c : rd.str("pattern")){ if(c == '|'){ alts.push_back(cur); cur.clear(); } else if(c != ' ') cur += c; }
        alts.push_back(cur);
        alts.erase(std::remove(alts.begin(), alts.end(), std::string()), alts.end());
        if(!alts.empty()) pat = alts[(size_t)Rng(R.get(tagKey("patternAlt"))).below((int)alts.size())];
    }
    // phrase / scene chance rolls
    auto chanceAt = [&](uint64_t s, Beat at = NAN){ return rd.num("chance", LevelEvent, s, at); };
    bool phraseRoll = true, sceneRoll = true;
    if(chanceScope == 2){
        uint64_t s = chanceGroup.empty() ? R.get(tagKey("phraseChance")) : mixSeed(ctx.sceneSeed, hashTag(chanceGroup), (uint64_t)ph.index + 1);
        phraseRoll = Rng(s).chance(chanceAt(s));
    }
    if(chanceScope == 3){
        uint64_t s = mixSeed(ctx.sceneSeed, hashTag(chanceGroup.empty() ? in.id : chanceGroup), 0x5CE7E);
        sceneRoll = Rng(s).chance(chanceAt(s));
    }

    std::vector<SeqHit> out;
    if(!phraseRoll || !sceneRoll) return out;

    // external steps: placed as given (sorted), no other rhythm rules
    if(mode == 4){
        std::vector<ExternalStep> st = ctx.externalSteps ? ctx.externalSteps(in, ph) : std::vector<ExternalStep>();
        st.erase(std::remove_if(st.begin(), st.end(), [&](const ExternalStep& x){ return x.beat < -1e-9 || x.beat >= ph.length - 1e-9 || x.velocity <= 0; }), st.end());
        std::stable_sort(st.begin(), st.end(), [](const ExternalStep& a, const ExternalStep& b){ return a.beat < b.beat; });
        for(size_t i = 0; i < st.size(); i++){
            SeqHit h; h.start = ph.start + st[i].beat; h.step = (int)i;
            Beat next = ph.end();
            for(size_t j = i + 1; j < st.size(); j++) if(st[j].beat > st[i].beat + 1e-9){ next = ph.start + st[j].beat; break; }
            h.stepLen = st[i].length > 0 ? st[i].length : std::max(1e-3, next - h.start);
            h.level = std::clamp(st[i].velocity, 0.0, 1.0);
            h.strong = H.isStrongBeat(h.start); h.seed = R.get(stepKey((int)i));
            h.hasDegree = st[i].hasDegree; h.degree = st[i].degree;
            out.push_back(h);
        }
        return out;
    }
    // phrase-position slots (effects): [start, start + len) relative to the phrase / its bars
    if(mode == 5){
        double pb = std::max(0.0625, rd.num("positionBeats", LevelPhrase, pseed));
        std::vector<std::pair<Slot, std::pair<Beat, Beat>>> sl;   // slot, (start, end)
        auto add = [&](Slot s, Beat a, Beat b){ a = std::max(a, ph.start); b = std::min(b, ph.end()); if(b - a > 1e-9) sl.push_back({s, {a, b}}); };
        std::istringstream ss(rd.str("positions")); std::string tok;
        while(ss >> tok){
            if(tok == "firstBeat") add(Slot::FirstBeat, ph.start, ph.start + pb);
            else if(tok == "beforeEnd") add(Slot::BeforeEnd, ph.end() - pb, ph.end());
            else if(tok == "lastBar"){
                Beat last = ph.start;
                for(Beat bar : H.barStarts(H.barStart(ph.start), ph.end())) if(bar < ph.end() - 1e-9) last = std::max(last, bar);
                add(Slot::LastBar, last, ph.end());
            } else if(tok == "barLastBeat"){
                for(Beat bar : H.barStarts(H.barStart(ph.start), ph.end())){
                    Beat be = bar + H.barBeatsAt(bar + 1e-9);
                    if(be > ph.start + 1e-9 && be <= ph.end() + 1e-9) add(Slot::BarLastBeat, be - pb, be);
                }
            } else if(tok == "beforeFollowed" && followed){
                std::vector<Beat> starts;
                for(auto& e : *followed) if(e.start > ph.start + 1e-9 && e.start <= ph.end() + 1e-9) starts.push_back(e.start);
                std::sort(starts.begin(), starts.end());
                starts.erase(std::unique(starts.begin(), starts.end(), [](Beat a, Beat b){ return std::fabs(a - b) < 1e-6; }), starts.end());
                for(Beat b : starts) add(Slot::BeforeFollowed, b - pb, b);
            }
        }
        std::stable_sort(sl.begin(), sl.end(), [](const auto& a, const auto& b){ return a.second.first < b.second.first; });
        for(size_t i = 0; i < sl.size(); i++){
            SeqHit h; h.start = sl[i].second.first; h.stepLen = sl[i].second.second - h.start; h.step = (int)i;
            h.strong = H.isStrongBeat(h.start); h.seed = R.get(stepKey((int)i)); h.slot = (int)sl[i].first;
            if(chanceScope == 0){ double c = chanceAt(h.seed, h.start); if(c < 100 && !Rng(mixSeed(h.seed, 4)).chance(c)) continue; }
            out.push_back(h);
        }
        return out;
    }
    bool prevHit = false;
    int counter = 0, barIdx = -1;
    for(Beat bar : H.barStarts(H.barStart(ph.start), ph.end())){
        if(bar > ph.start - 1e-9 || barIdx < 0) barIdx++;
        const Meter& m = H.meterAt(bar + 1e-9);
        double bb = H.barBeatsAt(bar + 1e-9);
        int subdiv = subdivP > 0 ? subdivP : std::max(1, 8 / std::max(1, m.den));
        int n = stepsP > 0 ? stepsP : std::max(1, m.num * subdiv);
        double dur = bb / n;
        double sw = swing >= 0 ? swing : m.swing;
        for(int i = 0; i < n; i++){
            Beat t = bar + i * dur;
            if(t < ph.start - 1e-9 || t >= ph.end() - 1e-9) continue;
            int k = counter++;
            int N = patLen > 0 ? patLen : n;
            int idx = patLen > 0 ? k % patLen : i;
            uint64_t sR = R.get(stepKey(k));
            bool strong = H.isStrongBeat(t);
            double pos = (t - ph.start) / std::max(1e-9, ph.length);
            double rampMul = std::clamp(1.0 + ramp * (2 * pos - 1), 0.0, 2.0);
            // hits / rotation from their lists: one draw per phrase, or per pattern repetition (reroll)
            int hitsK = hits, rotK = rot;
            if(!hitsL.empty() || !rotL.empty()){
                uint64_t rs = R.get(tagKey("reroll") + (uint64_t)(reroll ? k / std::max(1, N) : 0));
                if(!hitsL.empty()) hitsK = hitsL[(size_t)Rng(mixSeed(rs, 1)).below((int)hitsL.size())];
                if(!rotL.empty()) rotK = rotL[(size_t)Rng(mixSeed(rs, 2)).below((int)rotL.size())];
            }
            auto euclid = [&](int j){
                int hk = std::clamp(hitsK, 0, N);
                int r = ((j - rotK) % N + N) % N;
                return (r * hk) % N < hk;
            };
            bool hit = false;
            switch(mode){
                case 0: hit = euclid(idx); break;
                case 1: {
                    double c = stepChances.empty() ? rd.num("density", LevelEvent, sR, t) : stepChances[(size_t)idx % stepChances.size()] / 100.0;
                    c *= rampMul;
                    if(accent > 0) c *= strong ? (1 + accent) : (1 - accent * 0.5);
                    hit = Rng(mixSeed(sR, 1)).uniform() < c;
                    break;
                }
                case 2: {
                    if(pat.empty()) break;
                    char ch = pat[(size_t)idx % pat.size()];
                    hit = ch == 'x' || ch == 'X' || (ch == '?' && Rng(mixSeed(sR, 1)).chance(50));
                    break;
                }
                default: {
                    bool th = false;
                    if(followed) for(auto& e : *followed) if(e.start >= t - 1e-6 && e.start < t + dur - 1e-6){ th = true; break; }
                    hit = followMode == 0 ? th : followMode == 1 ? !th : (th && euclid(idx));
                    break;
                }
            }
            if(hit && !rests.empty() && std::find(rests.begin(), rests.end(), idx) != rests.end()) hit = false;
            if(hit && !barMask.empty() && !barMask[(size_t)barIdx % barMask.size()]) hit = false;
            if(hit && repMask > 0 && (k / std::max(1, N)) % 2 != repMask - 1) hit = false;
            if(hit && mode != 1 && rampMul < 1) hit = Rng(mixSeed(sR, 2)).uniform() < rampMul;
            if(hit && chanceScope == 0){ double c = chanceAt(sR, t); if(c < 100) hit = Rng(mixSeed(sR, 4)).chance(c); }
            if(hit && chanceScope == 1){
                int g = (int)std::floor((t - ph.start) / chanceBeats + 1e-9);
                uint64_t gs = R.get(groupKey(g));
                double c = chanceAt(gs);
                if(c < 100) hit = Rng(gs).chance(c);
            }
            if(hit && noRepeat && prevHit) hit = false;
            prevHit = hit;
            if(!hit) continue;
            double level = 1;
            if(accent > 0) level = strong ? 1 : 1 - accent * 0.5;
            if(mode == 2 && !pat.empty() && pat[(size_t)idx % pat.size()] == 'x' && pat.find('X') != std::string::npos) level *= 0.7;
            Beat t0 = t + ((i % 2 == 1 && sw > 0) ? sw * dur * 0.5 : 0);
            double rc = rd.num("rollChance", LevelEvent, sR, t);
            int strokes = (rc > 0 && Rng(mixSeed(sR, 3)).chance(rc)) ? std::clamp((int)std::lround(rollStrokesD), 2, 64) : 1;
            for(int s = 0; s < strokes; s++){
                double u = (double)s / strokes, f = u;
                if(rollShape == 1) f = std::sqrt(u);          // accelerando: gaps shrink
                else if(rollShape == 2) f = u * u;            // ritardando: gaps grow
                else if(rollShape == 3) f = u * 0.35;         // bunched at the start
                SeqHit h; h.start = t0 + f * dur; h.stepLen = strokes > 1 ? dur / strokes : dur; h.step = k;
                h.stroke = s; h.strokes = strokes; h.level = level; h.strong = strong; h.seed = sR;
                out.push_back(h);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- glide on change (5.16)
// Triggers (Event p glideTrigger): 1 next note, 2 chord change, 3 root change, 4 key / scale change,
// 5 transposition (engine), 6 degree shift. A bend uses the portamento keys glideFrom / glideTo
// (degrees), glideStart, glideBeats, glideCurve; key changes also write glideFromSemis / glideToSemis
// (semitones, old scale -> new scale). Tie: one event bends (a second change in it continues in a
// "tied" event, p tied = 1, no new attack). Re-attack: a new event from the old pitch.
enum GlideTrigger { GlideNone = 0, GlideNote = 1, GlideChord = 2, GlideRoot = 3, GlideKey = 4, GlideTranspose = 5, GlideDegree = 6 };
struct GlideOpts {
    bool on = false, tie = true;
    bool trig[7] = {false, false, false, false, false, false, false};
    double time = -1;                 // -1 = legacy per trigger
    int curve = 1;
    bool enabled(int t) const { return t > 0 && t < 7 && trig[t]; }
    // the bend time of a trigger: glideTime, else the legacy value, else 0.5 beats
    double timeFor(double legacy) const { return time >= 0 ? time : legacy > 0 ? legacy : 0.5; }
};
inline GlideOpts glideOptsOf(const SeqReader& rd, uint64_t seed = 0){
    GlideOpts g;
    g.on = rd.flag("glideOn");
    g.tie = rd.option("glideMode", seed) == 0;
    g.time = rd.num("glideTime", LevelPhrase, seed);
    g.curve = rd.option("glideCurve", seed);
    const char* keys[7] = {"", "glideNote", "glideChord", "glideRoot", "glideKey", "glideTranspose", "glideDegree"};
    for(int t = 1; t < 7; t++) g.trig[t] = g.on && rd.flag(keys[t]);
    return g;
}
inline GlideOpts glideOptsOf(const Instance& in){ static const ModHook none; return glideOptsOf(SeqReader{in.seq, none}); }

namespace glidedetail {
inline bool sameScale(const Scale* a, const Scale* b){
    if(a == b) return true;
    if(!a || !b || a->size() != b->size()) return false;
    for(int d = 0; d < a->size(); d++) if(std::fabs(a->semitonesOf(d) - b->semitonesOf(d)) > 1e-9) return false;
    return true;
}
inline int endDegree(const Event& e){ return e.p.count("glideTo") ? (int)std::lround(e.p.at("glideTo")) : e.degree; }
inline bool hasBend(const Event& e){ return e.p.count("glideTo") > 0; }
inline void writeBend(Event& e, int from, int to, Beat at, double beats, int curve, int trigger){
    e.p["glideFrom"] = from; e.p["glideTo"] = to; e.p["glideStart"] = at;
    e.p["glideBeats"] = std::max(0.0, std::min(beats, e.start + e.length - at));
    e.p["glideCurve"] = curve; e.p["glideTrigger"] = trigger;
}
// the second half of e from beat c (tied or re-attacked), starting at e's end pitch
inline Event splitAt(Event& e, Beat c, bool tied){
    Event f = e;
    f.id = mixSeed(e.id, 0x611DE, (uint64_t)std::llround(c * 1024));
    f.start = c; f.length = e.start + e.length - c;
    f.degree = endDegree(e);
    for(const char* k : {"glideFrom", "glideTo", "glideStart", "glideBeats", "glideCurve", "glideTrigger", "glide", "glideFromSemis", "glideToSemis", "tied"}) f.p.erase(k);
    if(tied) f.p["tied"] = 1;
    e.length = c - e.start;
    return f;
}
// candidate triggers for a change between two harmony points, in priority order
inline std::vector<int> changeKinds(const HarmonyAt& A, const HarmonyAt& B, int degA, int degB, bool chordFollow, bool chordSource){
    std::vector<int> k;
    if(!sameScale(A.scale, B.scale) || A.keyShift != B.keyShift) k.push_back(GlideKey);   // scale, tonic or step key shift
    if(degA != degB) k.push_back(GlideDegree);
    if(chordFollow && A.root != B.root){
        if(chordSource){ k.push_back(GlideChord); k.push_back(GlideRoot); } else { k.push_back(GlideRoot); k.push_back(GlideChord); }
    } else if(chordFollow && A.colour != B.colour) k.push_back(GlideChord);
    return k;
}
inline int firstEnabled(const std::vector<int>& k, const GlideOpts& g){ for(int t : k) if(g.enabled(t)) return t; return GlideNone; }
// nearest tone of a chord to a degree (lower on ties), skipping taken tones
inline int nearestChordTone(int deg, int root, ChordColour c, int n, const std::vector<int>& taken){
    int best = deg, bestD = 1 << 30;
    for(int cd : chordDegrees(c))
        for(int o = -4; o <= 4; o++){
            int d = root + cd + o * n, dist = std::abs(d - deg);
            if(std::find(taken.begin(), taken.end(), d) != taken.end()) dist += 1000;
            if(dist < bestD || (dist == bestD && d < best)){ bestD = dist; best = d; }
        }
    return best;
}
} // namespace glidedetail

// Applies glide on change to a planned phrase. meta per event: {hit group, degree offset} (-1 group:
// not a note). prev: the previous phrase's events (may be null).
inline void applyGlideOnChange(std::vector<Event>& out, std::vector<std::pair<int, int>>& meta, const GlideOpts& g,
                               const HarmonyTimeline& H, const PitchSpace& ps, Follow follow, bool chordSource,
                               double legacyNote, const std::vector<Event>* prev){
    using namespace glidedetail;
    if(!g.on || out.empty()) return;
    bool chordFollow = follow == Follow::Chord;
    const double eps = 1e-6;
    std::vector<bool> gone(out.size(), false);
    std::vector<Beat> settled(out.size(), -1e18);   // merged without a bend: no change before this beat
    std::vector<Event> extra; std::vector<std::pair<int, int>> extraMeta;

    // ---- pairs: each voice of a hit group with the same voice of the previous group
    std::vector<std::vector<int>> groups;
    for(size_t i = 0; i < out.size(); i++){
        if(meta[i].first < 0 || !out[i].pitched) continue;
        if(groups.empty() || meta[groups.back().front()].first != meta[i].first) groups.push_back({});
        groups.back().push_back((int)i);
    }
    // previous phrase: its last onset group (same instance)
    std::vector<const Event*> prevLast;
    if(prev){
        Beat last = -1e18;
        for(auto& e : *prev) if(e.kind == EventKind::Note && e.pitched && e.instance == out.front().instance) last = std::max(last, e.start);
        for(auto& e : *prev) if(e.kind == EventKind::Note && e.pitched && e.instance == out.front().instance && e.start > last - 0.25) prevLast.push_back(&e);
    }
    std::vector<int> line;   // per voice: index of the event carrying the line so far (-1 none)
    for(size_t gi = 0; gi < groups.size(); gi++){
        auto& G = groups[gi];
        std::vector<int> nextLine;
        for(size_t v = 0; v < G.size(); v++){
            int gx = G[v];
            Event& e = out[(size_t)gx];
            const Event* p = nullptr; int pIdx = -1, degP = meta[(size_t)gx].second;
            if(gi > 0 && !line.empty()){ pIdx = line[std::min(v, line.size() - 1)]; p = &out[(size_t)pIdx]; degP = meta[(size_t)pIdx].second; }
            else if(gi == 0 && !prevLast.empty()){ p = prevLast[std::min(v, prevLast.size() - 1)]; degP = (int)std::lround(p->param("gDegOff", meta[(size_t)gx].second)); }
            nextLine.push_back(gx);
            if(!p) continue;
            int from = endDegree(*p);
            Beat pEnd = p->start + p->length;
            bool sounding = pEnd > e.start - eps && p->start < e.start - eps;
            HarmonyAt A = H.at(hasBend(*p) ? std::max(p->start, p->p.at("glideStart")) : p->start), B = H.at(e.start);
            int t = sounding ? firstEnabled(changeKinds(A, B, degP, meta[(size_t)gx].second, chordFollow, chordSource), g) : GlideNone;
            if(t == GlideNone && g.enabled(GlideNote)) t = GlideNote;
            if(t == GlideNone) continue;
            double beats = g.timeFor(t == GlideNote ? legacyNote : 0);
            if(g.tie && sounding && !(t == GlideNote && from == e.degree)){
                if(pIdx >= 0 && !hasBend(*p)){
                    // merge: the earlier event goes on as one line bending to this note
                    Event& q = out[(size_t)pIdx];
                    q.length = std::max(q.length, e.start + e.length - q.start);
                    if(from != e.degree) writeBend(q, from, e.degree, e.start, beats, g.curve, t);
                    q.height2 = e.height2;
                    gone[(size_t)gx] = true;
                    meta[(size_t)pIdx].second = meta[(size_t)gx].second;
                    settled[(size_t)pIdx] = std::max(settled[(size_t)pIdx], e.start);
                    nextLine.back() = pIdx;
                    continue;
                }
                e.p["tied"] = 1;   // the line already bends (or lies in the previous phrase): continue tied
            }
            // the old note hands over to this one at a change (no doubled line)
            if(sounding && pIdx >= 0 && t != GlideNote) out[(size_t)pIdx].length = e.start - out[(size_t)pIdx].start;
            if(!hasBend(e) && (from != e.degree || (t == GlideNote && legacyNote > 0))) writeBend(e, from, e.degree, e.start, std::min(beats, e.length), g.curve, t);
        }
        line = nextLine;
    }

    // ---- changes under a sounding note (step / section boundaries inside it)
    for(size_t i = 0; i < out.size(); i++){
        if(gone[i] || meta[i].first < 0 || !out[i].pitched) continue;
        std::vector<Event> chain{out[i]};   // the event and its continuations
        for(size_t ci = 0; ci < chain.size(); ci++){
            Beat scan = hasBend(chain[ci]) ? chain[ci].p["glideStart"] : chain[ci].start;
            if(ci == 0) scan = std::max(scan, settled[i]);
            while(true){
                Event& e = chain[ci];
                Beat end = e.start + e.length, c = NAN;
                for(auto& s : H.steps(scan + eps, end - eps)) if(s.start > scan + eps && s.start < end - eps){ c = s.start; break; }
                if(std::isnan(c)) break;
                HarmonyAt A = H.at(c - eps), B = H.at(c);
                int deg = endDegree(e);
                int t = firstEnabled(changeKinds(A, B, meta[i].second, meta[i].second, chordFollow, chordSource), g);
                int to = deg;
                if(t == GlideRoot || t == GlideKey) to = deg + (chordFollow ? B.root - A.root : 0);
                else if(t == GlideChord) to = nearestChordTone(deg, B.root + (chordSource ? meta[i].second : 0), B.colour, B.scale->size(), {});
                if(t == GlideNone || (to == deg && t != GlideKey)){ scan = c; continue; }
                auto bend = [&](Event& x){
                    writeBend(x, deg, to, c, g.timeFor(0), g.curve, t);
                    if(t == GlideKey){ x.p["glideFromSemis"] = A.semitonesOf(deg); x.p["glideToSemis"] = B.semitonesOf(to); }
                    x.height2 = ps.heightOf(B.semitonesOf(to));
                };
                if(g.tie && !hasBend(e)){ bend(e); scan = c; continue; }
                Event f = splitAt(e, c, g.tie);
                f.height = ps.heightOf(A.semitonesOf(deg));
                bend(f);
                chain.push_back(f);
                break;
            }
        }
        out[i] = chain.front();
        for(size_t k = 1; k < chain.size(); k++){ extra.push_back(chain[k]); extraMeta.push_back(meta[i]); }
    }
    std::vector<Event> res; std::vector<std::pair<int, int>> rm;
    for(size_t i = 0; i < out.size(); i++) if(!gone[i]){ res.push_back(out[i]); rm.push_back(meta[i]); }
    for(size_t i = 0; i < extra.size(); i++){ res.push_back(extra[i]); rm.push_back(extraMeta[i]); }
    std::stable_sort(res.begin(), res.end(), [](const Event& a, const Event& b){ return a.start < b.start; });
    out.swap(res);
}

// Step key shifts (ROADMAP 5.9): the drawer pitches notes from (degree, frame scale), so notes in a
// shifted step (or bending into / out of one) carry their semitones in glideFromSemis / glideToSemis
// (equal values: a flat line). Bends read the old key just before glideStart, the new one at it.
inline void stampKeyShift(std::vector<Event>& out, const HarmonyTimeline& H){
    for(auto& e : out){
        if(e.kind != EventKind::Note || !e.pitched || e.p.count("glideFromSemis") || e.p.count("glideToSemis")) continue;
        bool bend = e.p.count("glideTo") > 0;
        Beat at = bend ? e.param("glideStart", e.start) : e.start;
        HarmonyAt A = H.at(bend ? std::max(e.start, at - 1e-6) : e.start), B = H.at(at);
        if(A.keyShift == 0 && B.keyShift == 0) continue;
        int from = bend && e.p.count("glideStart") ? (int)std::lround(e.param("glideFrom", e.degree)) : e.degree;
        int to = bend ? (int)std::lround(e.param("glideTo", e.degree)) : e.degree;
        e.p["glideFromSemis"] = A.semitonesOf(from);
        e.p["glideToSemis"] = B.semitonesOf(to);
    }
}

// phase shift (beats): onsets move later, those past the phrase end drop; a hit at the phrase start
// also leaves a lead-in of `shift` beats there (v1 segment offsets)
inline void applyPhaseShift(std::vector<SeqHit>& hits, const Phrase& ph, double shift){
    if(!(shift > 1e-9)) return;
    std::vector<SeqHit> o;
    for(auto& h : hits){
        if(std::fabs(h.start - ph.start) < 1e-9){ SeqHit l = h; l.stepLen = std::min(shift, h.stepLen); o.push_back(l); }
        SeqHit m = h; m.start += shift;
        if(m.start < ph.end() - 1e-9) o.push_back(m);
    }
    std::stable_sort(o.begin(), o.end(), [](const SeqHit& a, const SeqHit& b){ return a.start < b.start; });
    hits.swap(o);
}

// Plans one phrase of an instance into events. `followed`: the events of the instance named by
// seq.followId (Follow mode), already planned by the caller.
inline std::vector<Event> planPhrase(const Instance& in, const Phrase& ph, const PlanContext& ctx,
                                     const std::vector<Event>* followed = nullptr, const PhraseSeeds* seedsIn = nullptr){
    static const HarmonyTimeline defaultH;
    const HarmonyTimeline& H = ctx.harmony ? *ctx.harmony : defaultH;
    PhraseSeeds seeds = seedsIn ? *seedsIn : phraseSeeds(ctx.sceneSeed, in.id, ph.index, variationOf(in));
    std::vector<SeqHit> hits = planRhythm(in, ph, ctx, followed, &seeds);
    applyPhaseShift(hits, ph, SeqReader{in.seq, ctx.mod}.num("phaseShift"));
    std::vector<Event> out;
    if(in.muted || hits.empty()) return out;
    const SeedLineage& Pl = seeds.pitch;
    SeqReader rd{in.seq, ctx.mod, &ctx.modAt};
    uint64_t pseed = seeds.phrase;
    EventKind kind = kindForType(in.type);
    auto band = bandOf(in);
    int source = rd.option("pitchSource", pseed), scaleMode = rd.option("scaleMode", pseed);
    int degOff = rd.integer("degreeOffset", LevelPhrase, pseed);
    int massPitch = rd.option("massPitch", pseed), nHarm = std::max(1, rd.integer("harmonics", LevelPhrase, pseed));
    int harmStep = std::max(1, rd.integer("harmonicStep", LevelPhrase, pseed));
    double harmFade = rd.num("harmonicFade", LevelPhrase, pseed);
    double harmTaper = rd.num("harmonicTaper", LevelPhrase, pseed), harmTaperPow = rd.num("harmonicTaperPower", LevelPhrase, pseed);
    int octave = rd.integer("octave", LevelPhrase, pseed), range = std::max(1, rd.integer("range", LevelPhrase, pseed));
    int walkStep = std::max(1, rd.integer("walkStep", LevelPhrase, pseed)), riffLen = std::max(1, rd.integer("riffLength", LevelPhrase, pseed));
    int voices = std::max(1, rd.integer("voices", LevelPhrase, pseed)), voicing = rd.option("voicing", pseed);
    // octave jumps (v1 octaveUp): decided per pattern step for the phrase, shared by the step's voices
    double octChance = rd.num("octaveChance", LevelPhrase, pseed);
    int octPat = rd.integer("patternLength", LevelPhrase, pseed);
    if(octPat <= 0) octPat = rd.integer("steps", LevelPhrase, pseed);
    if(octPat <= 0) octPat = 8;
    int nps = rd.integer("notesPerStep", LevelPhrase, pseed), order = rd.option("order", pseed), adv = rd.integer("advance", LevelPhrase, pseed);
    bool hold = rd.flag("hold"), legato = rd.flag("legato"), vlead = rd.flag("voiceLeading");
    double vlGlide = rd.num("voiceLeadGlide", LevelPhrase, pseed);
    int glideCurve = rd.option("glideCurve", pseed); bool arpReset = rd.flag("arpReset");
    double strum = rd.num("strum", LevelPhrase, pseed), glide = rd.num("glide", LevelPhrase, pseed), overlap = rd.num("overlap", LevelPhrase, pseed);
    GlideOpts gl = glideOptsOf(rd, pseed);
    std::vector<std::pair<int, int>> gmeta;   // glide on change: hit group, degree offset per event
    // degree offset per hit: phrase value + event-level routings (read at the hit's beat)
    static const ModHook noHook;
    double degBase = SeqReader{in.seq, noHook}.num("degreeOffset");
    std::vector<int> typed;
    { std::istringstream ss(rd.str("typedOrder")); int v; while(ss >> v) typed.push_back(v - 1); }
    // the event's look: "look" (pitched types), "cell" (percussive / texture), "shape" (shapes) or
    // "effect" (time / filter effects), whichever the type has
    std::string look;
    if(in.params.is_object())
        for(const char* k : {"look", "cell", "shape", "effect"})
            if(in.params.contains(k) && in.params[k].is_string()){ look = in.params[k].get<std::string>(); break; }
    std::string role = in.params.is_object() && in.params.contains("colourRole") && in.params["colourRole"].is_string() ? in.params["colourRole"].get<std::string>() : "neutral";
    bool pitchedKind = kind == EventKind::Note && in.follow != Follow::Off;
    uint64_t idBase = mixSeed(hashTag(in.id), 0xE7E7, (uint64_t)ph.index + 1);

    // scale-degree helpers (riff, walk start) drawn once per phrase from the pitch stream
    std::vector<int> riff;
    for(int j = 0; j < riffLen; j++) riff.push_back(Rng(Pl.get(tagKey("riff") + (uint64_t)j)).below(range));
    int walk = Rng(Pl.get(tagKey("walkStart"))).below(range);
    int prevDegree = -100000, hitIndex = -1, lastStep = -1;
    int arpIdx = -1, arpStep = -1; double chordStart = NAN;   // arp position, reset at chord changes
    std::vector<int> stepDegrees;

    for(size_t hi = 0; hi < hits.size(); hi++){
        const SeqHit& h = hits[hi];
        uint64_t sP = Pl.get(stepKey(h.step));
        if(h.step != lastStep){ hitIndex++; lastStep = h.step; stepDegrees.clear(); }
        // next onset (legato)
        Beat next = ph.end();
        for(size_t j = hi + 1; j < hits.size(); j++) if(hits[j].start > h.start + 1e-9){ next = hits[j].start; break; }
        double nl = rd.num("noteLength", LevelEvent, h.seed, h.start);
        double len = h.slot >= 0 ? h.stepLen : legato ? (next - h.start) + overlap : nl * h.stepLen;
        double lvl = rd.num("level", LevelEvent, h.seed, h.start) * h.level;

        auto base = [&](uint64_t seed, int voice){
            Event e;
            e.id = mixSeed(idBase, (uint64_t)out.size() + 1);
            e.instance = in.id; e.layer = in.layer; e.kind = kind; e.look = look; e.colourRole = role;
            e.start = h.start; e.length = std::max(1e-3, len); e.level = std::clamp(lvl, 0.0, 1.0);
            e.seed = mixSeed(seed, (uint64_t)voice + 1);
            e.depth = ctx.modAt ? ctx.modAt("depth", 1.0, LevelEvent, e.seed, h.start) : ctx.mod ? ctx.mod("depth", 1.0, LevelEvent, e.seed) : 1.0;
            e.transient = kind == EventKind::Hit;
            if(h.strokes > 1){ e.p["roll"] = h.strokes; e.p["stroke"] = h.stroke; }
            if(h.slot >= 0){ e.p["slot"] = h.slot; e.p["slotBeats"] = h.stepLen; }
            return e;
        };

        if(!pitchedKind){
            Event e = base(h.seed, 0);
            e.pitched = false;
            if(kind == EventKind::Mass && massPitch == 1){
                // harmonics of the root: height of root x (j * step + 1), fading up the series
                auto hOf = [&](Beat b, int hn){
                    HarmonyAt a = H.at(b);
                    int root = (in.follow == Follow::Off ? 0 : a.root) + octave * a.scale->size() + degOff;
                    return ctx.pitch.heightOf(a.semitonesOf(root) + 12.0 * std::log2((double)hn));
                };
                for(int j = 0; j < nHarm; j++){
                    int hn = j * harmStep + 1;
                    Event m = base(h.seed, j);
                    m.pitched = false;
                    m.height = hOf(h.start, hn); m.height2 = hOf(h.start + m.length, hn);   // v1: glide to the next segment root
                    m.level = std::clamp(e.level * std::pow(1.0 - (double)j / nHarm, harmFade), 0.0, 1.0);
                    m.p["harmonic"] = hn;
                    if(harmTaper > 0) m.p["widthScale"] = std::max(0.0, 1 - harmTaper * std::pow((double)j / nHarm, harmTaperPow));
                    if(m.level > 0) out.push_back(m);
                }
                continue;
            } else if(kind == EventKind::Mass){
                Rng r(sP);
                e.height = r.uniform(band.first, band.second); e.height2 = r.uniform(band.first, band.second);
            } else if(kind == EventKind::Hit || kind == EventKind::Sweep){
                e.height = band.first; e.height2 = band.second;
            } else {
                e.height = e.height2 = 0.5 * (band.first + band.second);
            }
            out.push_back(e);
            continue;
        }

        // pitched: harmony at the onset (chords rebuild at bar changes even mid-phrase)
        HarmonyAt ha = H.at(h.start);
        const Scale& sc = *ha.scale;
        int n = sc.size();
        int degOffPhrase = degOff;
        if(ctx.modAt) degOff = degOffPhrase + (int)std::lround(rd.num("degreeOffset", LevelEvent, h.seed, h.start) - degBase);
        int origin = (in.follow == Follow::Chord ? ha.root : in.follow == Follow::Pedal ? in.pedalDegree : 0) + degOff;
        int oct = octave * n;
        if(octChance > 0 && Rng(Pl.get(tagKey("octaveUp") + (uint64_t)(((h.step % octPat) + octPat) % octPat))).uniform() * 100 < octChance) oct += n;
        std::vector<std::pair<int, double>> notes;   // degree, start offset (strum)
        if(h.stroke > 0 && !stepDegrees.empty()){
            for(int d : stepDegrees) notes.push_back({d, 0.0});   // roll strokes repeat the step's pitch
        } else if(h.hasDegree){
            notes.push_back({origin + oct + h.degree, 0.0});      // external step with its own degree
        } else if(source == 0 || (in.follow == Follow::Pedal && source == 1)){   // root, or a pedal
            notes.push_back({origin + oct, 0.0});
        } else if(source == 1){
            int rel = 0;
            if(scaleMode == 0) rel = Rng(sP).below(range);
            else if(scaleMode == 1){
                if(hitIndex > 0){ walk += Rng(sP).below(2 * walkStep + 1) - walkStep; }
                while(walk < 0 || walk >= range){ if(walk < 0) walk = -walk; if(walk >= range) walk = 2 * (range - 1) - walk; if(range == 1){ walk = 0; break; } }
                rel = walk;
            } else rel = riff[(size_t)hitIndex % riff.size()];
            notes.push_back({origin + oct + rel, 0.0});
        } else {
            // chord / arp: a stack of degrees on the root
            std::vector<int> cd = chordDegrees(in.follow == Follow::Chord ? ha.colour : ChordColour::Triad);
            std::vector<int> st;
            for(int v = 0; v < voices; v++) st.push_back(cd[(size_t)v % cd.size()] + n * (v / (int)cd.size()));
            if(voicing == 1) for(size_t v = 1; v < st.size(); v += 2) st[v] += n;
            else if(voicing == 2 && st.size() >= 3) st[st.size() - 2] -= n;
            else if(voicing == 3) for(size_t v = 0; v < st.size(); v++) st[v] = Rng(mixSeed(sP, (uint64_t)v + 0x5CA7)).below(n);   // scattered: random degrees of the octave
            std::sort(st.begin(), st.end());
            int V = (int)st.size();
            std::vector<int> seqIdx;
            if(order == 1) for(int v = V - 1; v >= 0; v--) seqIdx.push_back(v);
            else if(order == 2){ for(int v = 0; v < V; v++) seqIdx.push_back(v); for(int v = V - 2; v > 0; v--) seqIdx.push_back(v); }
            else if(order == 4 && !typed.empty()) for(int t : typed) seqIdx.push_back(((t % V) + V) % V);
            else for(int v = 0; v < V; v++) seqIdx.push_back(v);
            int k = (nps <= 0 || nps >= V) ? V : nps;
            int a = adv > 0 ? adv : k;
            if(h.step != arpStep){
                arpStep = h.step;
                arpIdx = (arpReset && chordStart != ha.stepStart) ? 0 : arpIdx + 1;
                chordStart = ha.stepStart;
            }
            for(int j = 0; j < k; j++){
                int v;
                if(k == V && nps <= 0) v = j;
                else if(order == 3) v = Rng(mixSeed(sP, (uint64_t)j + 7)).below(V);
                else v = seqIdx[(size_t)((arpIdx * a + j) % (int)seqIdx.size())];
                notes.push_back({origin + oct + st[(size_t)v], j * strum});
            }
        }
        if(h.stroke == 0){ stepDegrees.clear(); for(auto& nt : notes) stepDegrees.push_back(nt.first); }
        int vi = 0;
        std::vector<int> vlTaken;   // voice leading: voices of one step land on different tones
        for(auto& nt : notes){
            Event e = base(sP, vi++);
            e.pitched = true;
            e.degree = nt.first;
            e.start = h.start + nt.second;
            if(hold){
                Beat endB = std::isfinite(ha.stepEnd) ? ha.stepEnd : ph.end();
                e.length = std::max(e.length, endB - e.start);
            }
            if(hold && vlead && std::isfinite(ha.stepEnd) && ha.stepEnd < ph.end() - 1e-9){
                // ring through the next chord too, bending to its nearest tone
                HarmonyAt nx = H.at(ha.stepEnd + 1e-9);
                Beat endB = std::isfinite(nx.stepEnd) ? std::min(nx.stepEnd, ph.end()) : ph.end();
                e.length = std::max(e.length, endB - e.start);
            }
            e.height = e.height2 = ctx.pitch.heightOf(ha.semitonesOf(e.degree));
            if(vlead && std::isfinite(ha.stepEnd) && e.start + e.length > ha.stepEnd + 1e-9){
                HarmonyAt nx = H.at(ha.stepEnd + 1e-9);
                int nn = nx.scale->size();
                int nroot = in.follow == Follow::Chord ? nx.root + degOff : origin;
                int best = e.degree, bestD = 1 << 30;
                for(int c : chordDegrees(in.follow == Follow::Chord ? nx.colour : ChordColour::Triad))
                    for(int o = -3; o <= 3; o++){
                        int d = nroot + c + o * nn, dist = std::abs(d - e.degree);
                        if(std::find(vlTaken.begin(), vlTaken.end(), d) != vlTaken.end()) dist += 1000;
                        if(dist < bestD || (dist == bestD && d < best)){ bestD = dist; best = d; }
                    }
                vlTaken.push_back(best);
                e.p["glideFrom"] = e.degree;
                e.p["glideTo"] = best;
                e.p["glideStart"] = ha.stepEnd;
                e.p["glideBeats"] = vlGlide;
                e.p["glideCurve"] = glideCurve;
                e.height2 = ctx.pitch.heightOf(nx.semitonesOf(best));
            }
            // portamento from the previous note: bends glideFrom -> glideTo over [glideStart, +glideBeats)
            // (voice leading writes the same keys); glideCurve 0 linear, 1 ease, 2 exp, 3 sCurve
            if(!gl.on && glide > 0 && prevDegree > -100000 && !e.p.count("glideTo")){
                e.p["glide"] = glide; e.p["glideFrom"] = prevDegree; e.p["glideTo"] = e.degree;
                e.p["glideStart"] = e.start; e.p["glideBeats"] = std::min(glide, e.length); e.p["glideCurve"] = glideCurve;
            }
            if(gl.on) e.p["gDegOff"] = degOff;
            out.push_back(e);
            gmeta.push_back({hitIndex * 64 + h.stroke, degOff});
        }
        if(!notes.empty()) prevDegree = notes.front().first;
        degOff = degOffPhrase;
    }
    if(gl.on && pitchedKind) applyGlideOnChange(out, gmeta, gl, H, ctx.pitch, in.follow, source == 2, glide, ctx.prevPhrase);
    if(pitchedKind) stampKeyShift(out, H);
    return out;
}

// Convenience: plan phrase k of an instance with its clock
inline std::vector<Event> planPhraseK(const Instance& in, PhraseClock& clock, int k, const PlanContext& ctx,
                                      const std::vector<Event>* followed = nullptr){
    return planPhrase(in, clock.phrase(k), ctx, followed);
}

} // namespace forms2
